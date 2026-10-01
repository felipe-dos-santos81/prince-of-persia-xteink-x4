#ifndef CONFIG_H
#define CONFIG_H

/* Xteink X4 e-paper wiring (SSD1677 controller, GDEQ0426T82 panel). */
#define EPD_SCK 8
#define EPD_MOSI 10
#define EPD_CS 21
#define EPD_DC 4
#define EPD_RST 5
#define EPD_BUSY 6

/* SPI clock for the panel. The SSD1677 is rated for 20 MHz writes; 40 MHz
   works on the X4 (xteink-bad-apple uses it too) and saves 10 ms a picture. */
#define EPD_SPI_HZ 40000000

/* 1 = picture updates use a custom 4-frame waveform instead of the panel's
   own (about 520 ms). Much faster, more ghosting, and it drives the panel
   outside its rated parameters: it MAY PERMANENTLY DAMAGE THE DISPLAY.
   Enable it for one build with: PLATFORMIO_BUILD_FLAGS=-DFAST_LUT=1 pio run */
#ifndef FAST_LUT
#define FAST_LUT 0
#endif

/* Force a full (flashing) refresh after this many partial refreshes to clear
   ghosting. 0 = never. */
#define FULL_REFRESH_EVERY 150

/* 1 = black dots on white instead of white dots on black. */
#define INVERT_VIDEO 0

/* 1 = turn the picture upside down, if the panel is mounted that way. */
#define ROTATE_180 0

/* Buttons: two resistor ladders read by the ADC, and the power button. */
#define BUTTON_FRONT_PIN 1 /* Back, Confirm, Left, Right */
#define BUTTON_SIDE_PIN 2  /* Up, Down */
#define BUTTON_POWER_PIN 3 /* active low; the only button that can wake deep sleep */
/* A ladder reading at or below this (of 4095) means one of its buttons is down. */
#define BUTTON_PRESSED_MAX 3800

/* Sleep after this long with no button pressed. */
#define IDLE_SLEEP_MS 30000
/* 1 = sleep even while a USB host is connected (the serial port then
   disappears). Enable it for one build with: make flash sleep_usb=1 */
#ifndef SLEEP_ON_USB
#define SLEEP_ON_USB 0
#endif
/* Emulated frame whose picture becomes the sleep screen: the title screen. */
#define SLEEP_PICTURE_FRAME 1200

/* Frames to run flat-out at boot to measure emulation speed. 0 = skip. */
#define BOOT_BENCHMARK_FRAMES 300

/* 1 = boot with a deliberately wrong image size so the machine halts at
   once. Used to check the halt reporting on the device. Enable it for one
   build with: PLATFORMIO_BUILD_FLAGS=-DSELF_TEST_HALT=1 pio run */
#ifndef SELF_TEST_HALT
#define SELF_TEST_HALT 0
#endif

#endif
