# Sleep screen for Prince of Persia on the Xteink X4 — Design

Date: 2026-10-01. Extends `2026-10-01-pop-xteink-x4-design.md`, which listed
input and power management as out of scope; this adds the minimum of both.

## 1. Goal

When no button has been pressed for 30 seconds, the X4 shows a sleep screen and
powers down. Pressing the power button wakes it, and the demo starts again from
the beginning.

## 2. Decisions

| Topic | Decision |
|---|---|
| Sleep | ESP32-C3 deep sleep; the e-ink panel keeps the sleep screen with no power |
| Wake | Power button only (the only button wired to a digital pin) |
| Activity | Any of the seven buttons resets the 30 s timer; boot and wake count as activity |
| Sleep screen | The game's title screen (emulated frame 1200), stretched to the panel, with "Press power to wake" in a small black box |
| USB | While a USB host is connected the device stays awake, so it can be flashed and monitored; `SLEEP_ON_USB` = 1 sleeps anyway |

## 3. Verified facts

- Buttons (from the xteink-bad-apple firmware for this device): the four front
  buttons form a resistor ladder on GPIO1 (ADC1 channel 1), the two side buttons
  one on GPIO2 (ADC1 channel 2). With no button pressed a ladder reads above
  3,800 of 4,095; every button reads at or below 3,800. The power button is on
  GPIO3, active low, with the internal pull-up enabled.
- `esp_deep_sleep_enable_gpio_wakeup(mask, ESP_GPIO_WAKEUP_GPIO_LOW)` wakes the
  ESP32-C3 from deep sleep on a low level of an RTC-capable pin; GPIO3 is one
  (ESP-IDF 5.5 `esp_sleep.h`). Wake from deep sleep is a reset: `setup()` runs
  again.
- `HWCDC::isPlugged()` (Arduino core 3.3.7) reports a USB host connected while
  the host sends start-of-frame packets; a power bank does not count. In deep
  sleep the USB serial port disappears from the host.
- The emulation is deterministic: emulated frame 1200 is always the title
  screen (golden hash `7a2efcf3872da9fd`, double hi-res). It is reached about
  20 s after boot, before the earliest possible sleep at 30 s.

## 4. Design

### 4.1 Units

| Unit | Responsibility | Interface |
|---|---|---|
| `firmware/src/idle.c`, `idle.h` | Inactivity timer, plain C, tested on the host | `void idle_reset(idle_timer_t *t, uint32_t now_ms, uint32_t timeout_ms)`; `bool idle_update(idle_timer_t *t, uint32_t now_ms, bool active)`: true once `timeout_ms` has passed with `active` false throughout; correct across `millis()` wrap-around. The firmware passes `IDLE_SLEEP_MS` |
| `firmware/src/input.cpp`, `input.h` | Read the buttons | `void input_begin(void)`; `bool input_any_pressed(void)`; `bool input_power_pressed(void)` |
| `firmware/src/display.cpp` | Gains the sleep screen | `void display_show_sleep(uint8_t *picture)`: overlay the wake hint on the picture, show it with one full refresh, then power the panel controller off |
| `firmware/src/main.cpp` | Snapshot, timer, sleep sequence | — |
| `firmware/src/config.h` | `IDLE_SLEEP_MS` (30000), `SLEEP_ON_USB` (0, overridable with a build flag), button pins and the 3,800 threshold | — |

### 4.2 Flow

1. `setup()` starts input and the idle timer (boot counts as activity).
2. Every emulated frame the emulator task reads `input_any_pressed()` and feeds
   the idle timer. At emulated frame 1200 it also renders the picture into a
   second buffer, `sleep_picture` (26,880 bytes).
3. When the timer expires, and either no USB host is connected or
   `SLEEP_ON_USB` is 1, the emulator task stops emulating and asks the display
   task to sleep (the same hand-off as the halt screen).
4. The display task calls `display_show_sleep(sleep_picture)`: the hint text is
   drawn in a black box at the bottom of the 560×384 picture, the picture is
   stretched and shown with one full refresh, and the panel controller is
   powered off. If no snapshot exists yet (it always should), the current
   picture is used instead.
5. The display task waits until the power button is released, enables the GPIO3
   low-level wake source, and enters deep sleep.
6. The power button wakes the chip, which boots from the start.

The halt screen takes precedence: a halted machine never sleeps, so its message
stays readable while USB is connected and is not replaced by the title art.

### 4.3 Logging

When going to sleep: `sleep: idle for 30 s` and, if USB is connected and
`SLEEP_ON_USB` is 0, once: `sleep: postponed while USB is connected`.

## 5. Memory

`sleep_picture` adds 26,880 bytes of static RAM: free heap drops from about
103 KB to about 76 KB, inside the 72 KB budget of the base design.

## 6. Testing

- Host (`make test`): `tests/test_idle.c` covers: not expired before 30 s;
  expired at exactly 30 s; a single active sample restarts the count; correct
  across a `millis()` wrap.
- Device, unplugged: with no button pressed, the title sleep screen with the
  hint appears about 30 s after boot; the serial port is gone. Pressing power
  restarts the demo from the beginning. Pressing any other button every 20 s
  keeps it awake past 30 s.
- Device, plugged into the Mac: the demo keeps running past 30 s and the log
  shows the postponed message once.

## 7. Out of scope

Using buttons to play the game, battery level, sleep on low battery, a
configurable timeout at runtime, waking from buttons other than power.
