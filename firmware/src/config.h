#ifndef CONFIG_H
#define CONFIG_H

/* Xteink X4 e-paper wiring (SSD1677 controller, GDEQ0426T82 panel). */
#define EPD_SCK 8
#define EPD_MOSI 10
#define EPD_CS 21
#define EPD_DC 4
#define EPD_RST 5
#define EPD_BUSY 6

/* Where the 560x384 picture sits on the 800x480 panel. X must be a
   multiple of 8. */
#define WINDOW_X 120
#define WINDOW_Y 48

/* Force a full (flashing) refresh after this many partial refreshes to clear
   ghosting. 0 = never. */
#define FULL_REFRESH_EVERY 0

/* 1 = black dots on white instead of white dots on black. */
#define INVERT_VIDEO 0

/* 1 = turn the picture upside down, if the panel is mounted that way. */
#define ROTATE_180 0

/* Frames to run flat-out at boot to measure emulation speed. 0 = skip. */
#define BOOT_BENCHMARK_FRAMES 300

/* 1 = boot with a deliberately wrong image size so the machine halts at
   once. Used to check the halt reporting on the device. */
#define SELF_TEST_HALT 0

#endif
