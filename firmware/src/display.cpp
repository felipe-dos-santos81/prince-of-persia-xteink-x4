#include "display.h"

#include <Arduino.h>
#include <SPI.h>
#include <string.h>

#include <gdeq/GxEPD2_426_GDEQ0426T82.h>
#include <glcdfont.c> /* Adafruit GFX classic 5x7 font: font[ch * 5 + column] */

#include "config.h"
#include "machine.h"
#include "scale.h"

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

/* The controller's two picture RAMs. A partial update drives each dot
   from its value in RAM_PREVIOUS to its value in RAM_NEW. */
enum epd_ram : uint8_t {
    RAM_NEW = 0x24,
    RAM_PREVIOUS = 0x26,
};

/* GxEPD2's driver sends pictures one SPI call per byte. This subclass adds
   bulk writes of whole rows and a partial update that can use the fast
   waveform above. */
class FastEpd : public GxEPD2_426_GDEQ0426T82 {
public:
    using GxEPD2_426_GDEQ0426T82::GxEPD2_426_GDEQ0426T82;

    /* Start writing the whole panel into one RAM, top row first; send each
       row of SCALE_ROW_BYTES with write_row. */
    void begin_ram(epd_ram ram)
    {
        set_full_area();
        _writeCommand(ram);
        _startTransfer();
    }

    void write_row(const uint8_t *row)
    {
        _pSPIx->writeBytes(row, SCALE_ROW_BYTES);
    }

    void end_ram()
    {
        _endTransfer();
    }

    /* Partial (differential) update of the whole panel. */
    void update_partial()
    {
        set_full_area();
        _writeCommand(0x21); /* display update control: RED normal */
        _writeData(0x00);
        _writeData(0x00);
        _writeCommand(0x22);
        /* 0xCC: clock and analog on, display mode 2 with the LUT already in
           the controller. 0xFC also loads the panel's own LUT from OTP. */
        _writeData(FAST_LUT ? 0xCC : 0xFC);
        _writeCommand(0x20);
        _waitWhileBusy("update_partial", partial_refresh_time);
        _power_is_on = true;
    }

    /* Load the fast waveform and its voltages. A full refresh replaces the
       waveform with the panel's own, so call this again after one. */
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
    /* Same RAM window setup as the driver, for the whole panel: the gates
       are reversed on this panel, so y counts down. */
    void set_full_area()
    {
        _writeCommand(0x11);
        _writeData(0x01);
        _writeCommand(0x44);
        _writeData(0);
        _writeData(0);
        _writeData((WIDTH - 1) % 256);
        _writeData((WIDTH - 1) / 256);
        _writeCommand(0x45);
        _writeData((HEIGHT - 1) % 256);
        _writeData((HEIGHT - 1) / 256);
        _writeData(0);
        _writeData(0);
        _writeCommand(0x4e);
        _writeData(0);
        _writeData(0);
        _writeCommand(0x4f);
        _writeData((HEIGHT - 1) % 256);
        _writeData((HEIGHT - 1) / 256);
    }
};

static FastEpd epd(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);
static uint32_t partial_count;

static const uint8_t background = INVERT_VIDEO ? 0xFF : 0x00;

/* Stretch the picture to the whole panel and write it into one RAM. */
static void send_picture(epd_ram ram, const uint8_t *picture)
{
    uint8_t row[SCALE_ROW_BYTES];

    epd.begin_ram(ram);
    for (int y = 0; y < SCALE_HEIGHT; y++) {
        /* Every fifth panel row repeats the game row above it. */
        if (y == 0 || scale_source_row(y, ROTATE_180) != scale_source_row(y - 1, ROTATE_180)) {
            scale_row(picture, y, ROTATE_180, INVERT_VIDEO, row);
        }
        epd.write_row(row);
    }
    epd.end_ram();
}

/* Show a picture with one full (flashing) refresh, which clears ghosting.
   With FAST_LUT the refresh loads the panel's own waveform but keeps the
   fast waveform's voltages. Resetting the controller to its own voltages
   first was tried: the full refresh then cleared ghosting visibly worse, and
   the partial updates run at these voltages anyway. */
static void show_full(const uint8_t *picture)
{
    send_picture(RAM_NEW, picture);
    epd.refresh(false);
    send_picture(RAM_PREVIOUS, picture);
    if (FAST_LUT) {
        epd.load_fast_lut();
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

void display_show(const uint8_t *picture)
{
    if (FULL_REFRESH_EVERY > 0 && ++partial_count >= FULL_REFRESH_EVERY) {
        partial_count = 0;
        show_full(picture);
        return;
    }
    send_picture(RAM_NEW, picture);
    epd.update_partial();
    /* The next partial update starts from what is now on the panel. */
    send_picture(RAM_PREVIOUS, picture);
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

    show_full(picture);
}

void display_show_sleep(uint8_t *picture)
{
    static const char hint[] = "Press power to wake";

    /* A black box at the bottom centre, 256 dots wide (bytes 19..50 of each
       row), with the hint centred in it. */
    for (int y = MACHINE_VIDEO_HEIGHT - 32; y < MACHINE_VIDEO_HEIGHT; y++) {
        memset(picture + y * MACHINE_VIDEO_ROW_BYTES + 19, 0, 32);
    }
    draw_text(picture, (MACHINE_VIDEO_WIDTH - 12 * (int)(sizeof hint - 1)) / 2,
              MACHINE_VIDEO_HEIGHT - 24, hint, 2);

    show_full(picture);
    epd.hibernate();
}
