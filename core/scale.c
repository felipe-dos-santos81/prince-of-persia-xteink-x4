#include "scale.h"

#include <string.h>

#include "video.h"

/* expand[c]: 7 dots (bit 6 leftmost) stretched to 10 (bit 9 leftmost). */
static uint16_t expand[128];
static bool expand_ready;

static void build_expand(void)
{
    int c;
    int d;

    for (c = 0; c < 128; c++) {
        uint16_t e = 0;
        for (d = 0; d < 10; d++) {
            e = (uint16_t)(e | ((c >> (6 - d * 7 / 10)) & 1) << (9 - d));
        }
        expand[c] = e;
    }
    expand_ready = true;
}

static uint8_t reverse_bits(uint8_t b)
{
    b = (uint8_t)(((b & 0xF0) >> 4) | ((b & 0x0F) << 4));
    b = (uint8_t)(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
    b = (uint8_t)(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
    return b;
}

/* One 560-dot source row -> one 800-dot row. */
static void scale_row(const uint8_t *src, uint8_t *dst)
{
    uint32_t acc = 0;
    int bits = 0;
    int g;

    for (g = 0; g < SCALE_WIDTH / 10; g++) {
        int bit = g * 7;
        int i = bit / 8;
        unsigned pair = (unsigned)src[i] << 8 | (i + 1 < VIDEO_ROW_BYTES ? src[i + 1] : 0u);
        unsigned chunk = (pair >> (9 - bit % 8)) & 0x7Fu;

        acc = (acc << 10) | expand[chunk];
        bits += 10;
        while (bits >= 8) {
            bits -= 8;
            *dst++ = (uint8_t)(acc >> bits);
        }
    }
}

bool scale_picture(const uint8_t *in, uint8_t *out, bool rotate_180, bool invert)
{
    uint8_t row[SCALE_ROW_BYTES];
    uint8_t final_row[SCALE_ROW_BYTES];
    uint8_t flip = invert ? 0xFF : 0x00;
    bool changed = false;
    int y;
    int i;

    if (!expand_ready) {
        build_expand();
    }

    for (y = 0; y < SCALE_HEIGHT; y++) {
        int src_y = (rotate_180 ? SCALE_HEIGHT - 1 - y : y) * 4 / 5;
        uint8_t *dst = out + y * SCALE_ROW_BYTES;

        scale_row(in + src_y * VIDEO_ROW_BYTES, row);
        for (i = 0; i < SCALE_ROW_BYTES; i++) {
            uint8_t b = rotate_180 ? reverse_bits(row[SCALE_ROW_BYTES - 1 - i]) : row[i];
            final_row[i] = (uint8_t)(b ^ flip);
        }
        if (memcmp(dst, final_row, SCALE_ROW_BYTES) != 0) {
            memcpy(dst, final_row, SCALE_ROW_BYTES);
            changed = true;
        }
    }
    return changed;
}
