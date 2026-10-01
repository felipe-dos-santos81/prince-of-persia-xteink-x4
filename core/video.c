#include "video.h"

#include <stdbool.h>
#include <string.h>

#include "bus.h"
#include "softswitch.h"

#define APPLE_ROWS 192
#define APPLE_COLS 40

/* Address of hi-res row y relative to the start of its page. */
static uint16_t row_offset(int y)
{
    return (uint16_t)(((y & 0x07) << 10) | (((y >> 3) & 0x07) << 7)) + (uint16_t)((y >> 6) * 40);
}

video_mode_t video_mode(void)
{
    const softswitch_state_t *s = softswitch_state();

    if (s->text || !s->hires) {
        return VIDEO_MODE_BLANK;
    }
    if (s->col80 && s->dhires) {
        return VIDEO_MODE_DHIRES;
    }
    return VIDEO_MODE_HIRES;
}

/* Lookup tables, built on first use. Patterns are most significant bit
   leftmost, matching the output.
   doubled[b]: the 7 pixels of a hi-res byte as 14 dots, each pixel two dots.
   reversed[b]: the 7 bits of a double hi-res byte as 7 dots, bit 0 leftmost. */
static uint16_t doubled[128];
static uint8_t reversed[128];
static bool tables_ready;

static void build_tables(void)
{
    int b;
    int bit;

    for (b = 0; b < 128; b++) {
        uint16_t d = 0;
        uint8_t r = 0;
        for (bit = 0; bit < 7; bit++) {
            unsigned on = (unsigned)((b >> bit) & 1);
            d = (uint16_t)(d | (on * 3u) << (12 - bit * 2));
            r = (uint8_t)(r | on << (6 - bit));
        }
        doubled[b] = d;
        reversed[b] = r;
    }
    tables_ready = true;
}

/* Appends 14-dot groups to an output row, 8 dots per byte. */
typedef struct {
    uint8_t *out;
    uint32_t acc;
    int bits;
} row_writer_t;

static void put14(row_writer_t *w, uint32_t dots)
{
    w->acc = (w->acc << 14) | dots;
    w->bits += 14;
    while (w->bits >= 8) {
        w->bits -= 8;
        *w->out++ = (uint8_t)(w->acc >> w->bits);
    }
}

/* One hi-res row: 40 bytes -> 560 dots. Bit 7 delays the byte by one dot:
   its first dot repeats the previous byte's last pixel and its own last dot
   falls off. */
static void hires_row(const uint8_t *src, uint8_t *out)
{
    row_writer_t w = { out, 0, 0 };
    uint32_t prev_last = 0;
    int col;

    for (col = 0; col < APPLE_COLS; col++) {
        uint8_t b = src[col];
        uint32_t dots = doubled[b & 0x7F];

        if (b & 0x80) {
            dots = (prev_last << 13) | (dots >> 1);
        }
        put14(&w, dots);
        prev_last = (uint32_t)((b >> 6) & 1);
    }
}

/* One double hi-res row: aux byte then main byte per column, 7 dots each. */
static void dhires_row(const uint8_t *aux, const uint8_t *main, uint8_t *out)
{
    row_writer_t w = { out, 0, 0 };
    int col;

    for (col = 0; col < APPLE_COLS; col++) {
        put14(&w, (uint32_t)reversed[aux[col] & 0x7F] << 7 | reversed[main[col] & 0x7F]);
    }
}

void video_render(uint8_t *out)
{
    const softswitch_state_t *s = softswitch_state();
    video_mode_t mode = video_mode();
    uint16_t page;
    int y;

    if (mode == VIDEO_MODE_BLANK) {
        memset(out, 0, VIDEO_BUFFER_SIZE);
        return;
    }

    if (!tables_ready) {
        build_tables();
    }
    page = (!s->store80 && s->page2) ? 0x4000 : 0x2000;

    for (y = 0; y < APPLE_ROWS; y++) {
        uint16_t addr = (uint16_t)(page + row_offset(y));
        uint8_t *row = out + (y * 2) * VIDEO_ROW_BYTES;

        if (mode == VIDEO_MODE_DHIRES) {
            dhires_row(bus_aux() + addr, bus_main() + addr, row);
        } else {
            hires_row(bus_main() + addr, row);
        }
        memcpy(row + VIDEO_ROW_BYTES, row, VIDEO_ROW_BYTES);
    }
}
