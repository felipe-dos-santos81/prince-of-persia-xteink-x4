#include "display.h"

#include <Arduino.h>
#include <SPI.h>
#include <string.h>

#include <gdeq/GxEPD2_426_GDEQ0426T82.h>
#include <glcdfont.c> /* Adafruit GFX classic 5x7 font: font[ch * 5 + column] */

#include "config.h"
#include "machine.h"

/* A 4-frame waveform for the SSD1677, from the xteink-bad-apple project
   (https://github.com/CidVonHighwind/xteink-bad-apple, commit cb87824,
   src/epd.h, GPL-2.0). Its author warns that it drives the panel outside its
   normal operating parameters and may damage it. Layout: 105 bytes for
   command 0x32 (voltage patterns, timing groups, frame rates), then gate
   voltage (0x03), three source voltages (0x04) and VCOM (0x2C). */
static const uint8_t fast_lut[110] = {
    0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xB9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xEE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x86, 0x86, 0x86, 0x86, 0x86,
    0x17, 0x41, 0xA8, 0x32, 0x30,
};

/* GxEPD2's driver sends pictures one SPI call per byte. This subclass adds
   a bulk write of a whole window, and a partial update that can use the
   fast waveform above. */
class FastEpd : public GxEPD2_426_GDEQ0426T82 {
public:
    using GxEPD2_426_GDEQ0426T82::GxEPD2_426_GDEQ0426T82;

    /* Write a w x h picture (w a multiple of 8) at (x, y) into controller
       RAM: 0x24 = the new picture, 0x26 = the one already on the panel. */
    void write_window(uint8_t ram, const uint8_t *picture, uint16_t x, uint16_t y, uint16_t w,
                      uint16_t h)
    {
        set_area(x, y, w, h);
        _writeCommand(ram);
        _startTransfer();
        _pSPIx->writeBytes(picture, (uint32_t)w / 8 * h);
        _endTransfer();
    }

    /* Partial (differential) update of the window at (x, y). */
    void update_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
    {
        set_area(x, y, w, h);
        _writeCommand(0x21); /* display update control: RED normal */
        _writeData(0x00);
        _writeData(0x00);
        _writeCommand(0x22);
        /* 0xCC: clock and analog on, display mode 2 with the LUT already in
           the controller. 0xFC also loads the panel's own LUT from OTP. */
        _writeData(FAST_LUT ? 0xCC : 0xFC);
        _writeCommand(0x20);
        _waitWhileBusy("update_window", partial_refresh_time);
        _power_is_on = true;
    }

    /* Load the fast waveform. Any update that loads the OTP LUT (a full
       refresh) replaces it, so call this again after one. */
    void load_fast_lut()
    {
        _writeCommand(0x32);
        for (int i = 0; i < 105; i++) {
            _writeData(fast_lut[i]);
        }
        _writeCommand(0x03);
        _writeData(fast_lut[105]);
        _writeCommand(0x04);
        _writeData(fast_lut[106]);
        _writeData(fast_lut[107]);
        _writeData(fast_lut[108]);
        _writeCommand(0x2C);
        _writeData(fast_lut[109]);
    }

private:
    /* Same RAM window setup as the driver: the gates are reversed on this
       panel, so y counts down. */
    void set_area(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
    {
        y = HEIGHT - y - h;
        _writeCommand(0x11);
        _writeData(0x01);
        _writeCommand(0x44);
        _writeData(x % 256);
        _writeData(x / 256);
        _writeData((x + w - 1) % 256);
        _writeData((x + w - 1) / 256);
        _writeCommand(0x45);
        _writeData((y + h - 1) % 256);
        _writeData((y + h - 1) / 256);
        _writeData(y % 256);
        _writeData(y / 256);
        _writeCommand(0x4e);
        _writeData(x % 256);
        _writeData(x / 256);
        _writeCommand(0x4f);
        _writeData((y + h - 1) % 256);
        _writeData((y + h - 1) / 256);
    }
};

static FastEpd epd(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);
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
    epd.selectSPI(SPI, SPISettings(EPD_SPI_HZ, MSBFIRST, SPI_MODE0));
    epd.init(0, true, 10, false);
    epd.clearScreen(background);
    if (FAST_LUT) {
        epd.load_fast_lut();
    }
    partial_count = 0;
}

void display_show(uint8_t *picture)
{
    if (ROTATE_180) {
        rotate_180(picture);
    }
    if (INVERT_VIDEO) {
        for (size_t i = 0; i < MACHINE_VIDEO_BUFFER_SIZE; i++) {
            picture[i] = (uint8_t)~picture[i];
        }
    }

    epd.write_window(0x24, picture, WINDOW_X, WINDOW_Y, MACHINE_VIDEO_WIDTH, MACHINE_VIDEO_HEIGHT);

    if (FULL_REFRESH_EVERY > 0 && ++partial_count >= FULL_REFRESH_EVERY) {
        partial_count = 0;
        epd.refresh(false);
        if (FAST_LUT) {
            epd.load_fast_lut();
        }
    } else {
        epd.update_window(WINDOW_X, WINDOW_Y, MACHINE_VIDEO_WIDTH, MACHINE_VIDEO_HEIGHT);
    }

    /* Differential refresh compares against the "previous" RAM, so make it
       match what is now on the panel. */
    epd.write_window(0x26, picture, WINDOW_X, WINDOW_Y, MACHINE_VIDEO_WIDTH, MACHINE_VIDEO_HEIGHT);
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
