#include "video.h"

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

/* One hi-res row: 40 bytes -> 560 dots (one byte per dot, 0 or 1). Each of
   the 7 pixels in a byte is two dots wide. Bit 7 delays the byte by one dot. */
static void hires_row(const uint8_t *src, uint8_t *dots)
{
    uint8_t prev_last = 0;
    int col;
    int bit;

    for (col = 0; col < APPLE_COLS; col++) {
        uint8_t b = src[col];
        uint8_t *d = dots + col * 14;

        if (b & 0x80) {
            d[0] = prev_last;
            for (bit = 0; bit < 7; bit++) {
                uint8_t on = (uint8_t)((b >> bit) & 1);
                d[1 + bit * 2] = on;
                /* The 14th dot spills into the next byte's first dot; the
                   scratch row has one spare dot so the last column is safe. */
                d[2 + bit * 2] = on;
            }
        } else {
            for (bit = 0; bit < 7; bit++) {
                uint8_t on = (uint8_t)((b >> bit) & 1);
                d[bit * 2] = on;
                d[bit * 2 + 1] = on;
            }
        }
        prev_last = (uint8_t)((b >> 6) & 1);
    }
}

/* One double hi-res row: aux byte then main byte per column, 7 dots each. */
static void dhires_row(const uint8_t *aux, const uint8_t *main, uint8_t *dots)
{
    int col;
    int bit;

    for (col = 0; col < APPLE_COLS; col++) {
        uint8_t *d = dots + col * 14;
        for (bit = 0; bit < 7; bit++) {
            d[bit] = (uint8_t)((aux[col] >> bit) & 1);
            d[7 + bit] = (uint8_t)((main[col] >> bit) & 1);
        }
    }
}

static void pack_row(const uint8_t *dots, uint8_t *out)
{
    int i;
    int bit;

    for (i = 0; i < VIDEO_ROW_BYTES; i++) {
        uint8_t packed = 0;
        for (bit = 0; bit < 8; bit++) {
            packed = (uint8_t)((packed << 1) | dots[i * 8 + bit]);
        }
        out[i] = packed;
    }
}

void video_render(uint8_t *out)
{
    const softswitch_state_t *s = softswitch_state();
    video_mode_t mode = video_mode();
    uint16_t page;
    uint8_t dots[VIDEO_WIDTH + 1];
    int y;

    if (mode == VIDEO_MODE_BLANK) {
        memset(out, 0, VIDEO_BUFFER_SIZE);
        return;
    }

    page = (!s->store80 && s->page2) ? 0x4000 : 0x2000;

    for (y = 0; y < APPLE_ROWS; y++) {
        uint16_t addr = (uint16_t)(page + row_offset(y));
        uint8_t *row = out + (y * 2) * VIDEO_ROW_BYTES;

        if (mode == VIDEO_MODE_DHIRES) {
            dhires_row(bus_aux() + addr, bus_main() + addr, dots);
        } else {
            hires_row(bus_main() + addr, dots);
        }
        pack_row(dots, row);
        memcpy(row + VIDEO_ROW_BYTES, row, VIDEO_ROW_BYTES);
    }
}
