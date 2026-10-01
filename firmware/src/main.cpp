#include <Arduino.h>
#include <esp_sleep.h>

#include "config.h"
#include "display.h"
#include "idle.h"
#include "input.h"
#include "machine.h"

/* 17,030 cycles at 1,020,484 Hz. */
#define FRAME_US 16688
/* If emulation falls this far behind real time, stop trying to catch up. */
#define MAX_LAG_US 250000
#define YIELD_EVERY_FRAMES 10

/* Defined in disk_image.S: the game disk image, read in place from flash. */
extern "C" {
extern const uint8_t disk_image_start[];
extern const uint8_t disk_image_end[];
}

static uint8_t picture[MACHINE_VIDEO_BUFFER_SIZE];
static SemaphoreHandle_t frame_request;
static SemaphoreHandle_t frame_ready;
static volatile bool halted;
static volatile bool sleeping;
/* The title screen, kept for the sleep screen. */
static uint8_t sleep_picture[MACHINE_VIDEO_BUFFER_SIZE];
static bool have_sleep_picture;
static uint32_t frames_run;
static idle_timer_t idle;
static volatile uint32_t panel_refreshes;

static uint32_t next_frame_us;
static uint32_t late_frames;
static uint32_t frames_since_yield;

/* FNV-1a, 32-bit: cheap on this CPU and enough to spot a changed picture. */
static uint32_t picture_hash(const uint8_t *data, size_t len)
{
    uint32_t hash = 2166136261u;

    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

/* Show the sleep screen and power down until the power button is pressed.
   Waking is a reset: setup() runs again. Does not return. */
static void enter_deep_sleep(void)
{
    display_show_sleep(have_sleep_picture ? sleep_picture : picture);

    /* A held button would wake the chip straight away. */
    while (input_power_pressed()) {
        delay(10);
    }
    delay(50);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_POWER_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
}

/* Once IDLE_SLEEP_MS passes with no button pressed, hand over to the display
   task to sleep. While a USB host is connected, stay awake unless
   SLEEP_ON_USB is set, so the device can still be flashed and monitored.
   Unplugging counts as activity, so it does not fall asleep the moment the
   cable comes out. */
static void check_idle(void)
{
    static bool postponed_logged;
    static bool was_plugged;
    bool plugged = HWCDC::isPlugged();
    bool active = input_any_pressed() || idle_usb_unplugged(&was_plugged, plugged);

    if (!idle_update(&idle, millis(), active)) {
        return;
    }
    if (!SLEEP_ON_USB && plugged) {
        if (!postponed_logged) {
            Serial.println("sleep: postponed while USB is connected");
            postponed_logged = true;
        }
        return;
    }
    Serial.printf("sleep: idle for %u s\n", (unsigned)(IDLE_SLEEP_MS / 1000));
    sleeping = true;
    xSemaphoreGive(frame_ready); /* wake the display task to sleep */
}

/* Owns the panel. Asks the emulator for a picture, shows it if it changed,
   and repeats. The emulator keeps running while the panel refreshes. */
static void display_task(void *)
{
    uint32_t shown = 0;
    bool first = true;

    display_begin();
    for (;;) {
        xSemaphoreGive(frame_request);
        xSemaphoreTake(frame_ready, portMAX_DELAY);

        if (halted) {
            display_show_halt(picture, machine_halt_reason());
            vTaskDelete(NULL);
        }
        if (sleeping) {
            enter_deep_sleep();
        }

        uint32_t hash = picture_hash(picture, sizeof picture);
        if (first || hash != shown) {
            display_show(picture);
            shown = hash;
            first = false;
            panel_refreshes = panel_refreshes + 1;
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

static void log_image(size_t len)
{
    Serial.printf("image: %u bytes, block 8:", (unsigned)len);
    for (int i = 0; i < 16 && (size_t)(8 * 512 + i) < len; i++) {
        Serial.printf(" %02X", disk_image_start[8 * 512 + i]);
    }
    Serial.println();
}

static void boot_benchmark(void)
{
    if (BOOT_BENCHMARK_FRAMES <= 0) {
        return;
    }
    uint32_t start = micros();
    for (int i = 0; i < BOOT_BENCHMARK_FRAMES; i++) {
        if (machine_run_frame() == MACHINE_HALTED) {
            Serial.printf("benchmark: machine halted after %d frames\n", i + 1);
            return;
        }
    }
    uint32_t elapsed = micros() - start;
    uint32_t real_time = (uint32_t)BOOT_BENCHMARK_FRAMES * FRAME_US;
    Serial.printf("benchmark: %d frames in %u ms = %u%% of real-time speed\n",
                  BOOT_BENCHMARK_FRAMES, (unsigned)(elapsed / 1000),
                  (unsigned)((uint64_t)real_time * 100 / (elapsed ? elapsed : 1)));
}

static void log_stats(void)
{
    static uint32_t last_ms;
    static uint64_t last_cycles;
    static uint32_t last_refreshes;

    uint32_t now = millis();
    uint32_t elapsed = now - last_ms;
    if (elapsed < 1000) {
        return;
    }
    uint64_t cycles = machine_cycles();
    uint32_t refreshes = panel_refreshes;
    Serial.printf("cps=%u late=%u refresh/s=%u.%u heap=%u mode=%s\n",
                  (unsigned)((cycles - last_cycles) * 1000 / elapsed), (unsigned)late_frames,
                  (unsigned)((refreshes - last_refreshes) * 1000 / elapsed),
                  (unsigned)((refreshes - last_refreshes) * 10000 / elapsed % 10),
                  (unsigned)ESP.getFreeHeap(), machine_video_mode_name());
    last_ms = now;
    last_cycles = cycles;
    last_refreshes = refreshes;
}

/* Sleep until the next 16.69 ms tick, or note that we are running late. */
static void pace(void)
{
    next_frame_us += FRAME_US;
    int32_t ahead = (int32_t)(next_frame_us - micros());

    if (ahead >= 1000) {
        delay((uint32_t)ahead / 1000);
        frames_since_yield = 0;
        return;
    }
    if (ahead < 0) {
        late_frames++;
        if (ahead < -MAX_LAG_US) {
            next_frame_us = micros();
        }
    }
    /* Even when late, let the idle task (and its watchdog) run. */
    if (++frames_since_yield >= YIELD_EVERY_FRAMES) {
        vTaskDelay(1);
        frames_since_yield = 0;
    }
}

void setup()
{
    Serial.begin(115200);
    /* Never block on logging for long: on battery, or with a USB host that
       is not reading, nobody drains it. Not 0: Arduino core 3.3.7's HWCDC
       counts a 0 ms timeout down past zero and then waits forever for a host
       that is connected but not reading. */
    Serial.setTxTimeoutMs(1);
    delay(1500); /* let USB serial enumerate so the boot log is visible */
    input_begin();

    size_t image_len = (size_t)(disk_image_end - disk_image_start);
    log_image(image_len);
    if (SELF_TEST_HALT) {
        image_len -= 1;
    }

    machine_init(disk_image_start, image_len);
    boot_benchmark();
    machine_init(disk_image_start, image_len); /* start the demo from the top */

    frame_request = xSemaphoreCreateBinary();
    frame_ready = xSemaphoreCreateBinary();
    xTaskCreate(display_task, "display", 8192, NULL, 2, NULL);

    idle_reset(&idle, millis(), IDLE_SLEEP_MS); /* boot and wake count as activity */
    next_frame_us = micros();
}

/* The Arduino loop task is the emulator task: one video frame per pass. */
void loop()
{
    if (halted) {
        Serial.printf("HALTED: %s\n", machine_halt_reason());
        delay(1000);
        return;
    }
    if (sleeping) {
        delay(1000); /* the display task is putting the device to sleep */
        return;
    }

    if (machine_run_frame() == MACHINE_HALTED) {
        halted = true;
        xSemaphoreGive(frame_ready); /* wake the display task to show it */
        return;
    }

    if (++frames_run == SLEEP_PICTURE_FRAME) {
        machine_render(sleep_picture);
        have_sleep_picture = true;
    }

    if (xSemaphoreTake(frame_request, 0) == pdTRUE) {
        machine_render(picture);
        xSemaphoreGive(frame_ready);
    }

    check_idle();
    pace();
    log_stats();
}
