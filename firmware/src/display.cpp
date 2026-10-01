#include "display.h"

#include <Arduino.h>
#include <SPI.h>
#include <string.h>

#include <gdeq/GxEPD2_426_GDEQ0426T82.h>
#include <glcdfont.c> /* Adafruit GFX classic 5x7 font: font[ch * 5 + column] */

#include "config.h"
#include "machine.h"

static GxEPD2_426_GDEQ0426T82 epd(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);
static uint32_t partial_count;

static const uint8_t background = INVERT_VIDEO ? 0xFF : 0x00;

static uint8_t reverse_bits(uint8_t b)
{
    b = (uint8_t)(((b & 0xF0) >> 4) | ((b & 0x0F) << 4));
    b = (uint8_t)(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
    b = (uint8_t)(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
    return b;
}

/* Rotate a 1-bit picture by 180 degrees in place. */
static void rotate_180(uint8_t *picture)
{
    size_t i = 0;
    size_t j = MACHINE_VIDEO_BUFFER_SIZE - 1;

    while (i < j) {
        uint8_t tmp = reverse_bits(picture[i]);
        picture[i] = reverse_bits(picture[j]);
        picture[j] = tmp;
        i++;
        j--;
    }
}

void display_begin(void)
{
    /* The X4 does not use the board's default SPI pins. Starting SPI here
       makes GxEPD2's own SPI.begin() a no-op that keeps these pins. */
    SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
    epd.init(0, true, 10, false);
    epd.clearScreen(background);
    partial_count = 0;
}

void display_show(uint8_t *picture)
{
    if (ROTATE_180) {
        rotate_180(picture);
    }

    epd.writeImage(picture, WINDOW_X, WINDOW_Y, MACHINE_VIDEO_WIDTH, MACHINE_VIDEO_HEIGHT,
                   INVERT_VIDEO);

    if (FULL_REFRESH_EVERY > 0 && ++partial_count >= FULL_REFRESH_EVERY) {
        partial_count = 0;
        epd.refresh(false);
    } else {
        epd.refresh(WINDOW_X, WINDOW_Y, MACHINE_VIDEO_WIDTH, MACHINE_VIDEO_HEIGHT);
    }

    /* Differential refresh compares against the "previous" buffer, so make
       it match what is now on the panel. */
    epd.writeImageToPrevious(picture, WINDOW_X, WINDOW_Y, MACHINE_VIDEO_WIDTH,
                             MACHINE_VIDEO_HEIGHT, INVERT_VIDEO);
}

static void set_dot(uint8_t *picture, int x, int y)
{
    if (x < 0 || x >= MACHINE_VIDEO_WIDTH || y < 0 || y >= MACHINE_VIDEO_HEIGHT) {
        return;
    }
    picture[y * (MACHINE_VIDEO_WIDTH / 8) + x / 8] |= (uint8_t)(0x80 >> (x % 8));
}

static void draw_text(uint8_t *picture, int x, int y, const char *text, int scale)
{
    for (; *text != '\0'; text++, x += 6 * scale) {
        uint8_t ch = (uint8_t)*text;

        for (int col = 0; col < 5; col++) {
            uint8_t bits = pgm_read_byte(&font[ch * 5 + col]);

            for (int row = 0; row < 8; row++) {
                if (!(bits & (1 << row))) {
                    continue;
                }
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        set_dot(picture, x + col * scale + dx, y + row * scale + dy);
                    }
                }
            }
        }
    }
}

void display_show_halt(uint8_t *picture, const char *reason)
{
    char line[47]; /* 46 characters of 12 dots fit across 560 */

    memset(picture, 0, MACHINE_VIDEO_BUFFER_SIZE);
    draw_text(picture, 8, 120, "HALTED", 6);
    strncpy(line, reason, sizeof line - 1);
    line[sizeof line - 1] = '\0';
    draw_text(picture, 8, 200, line, 2);

    if (ROTATE_180) {
        rotate_180(picture);
    }
    epd.writeImageForFullRefresh(picture, WINDOW_X, WINDOW_Y, MACHINE_VIDEO_WIDTH,
                                 MACHINE_VIDEO_HEIGHT, INVERT_VIDEO);
    epd.refresh(false);
}
