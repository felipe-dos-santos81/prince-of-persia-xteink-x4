#include <string.h>

#include "bus.h"
#include "softswitch.h"
#include "test.h"
#include "video.h"

static uint8_t out[VIDEO_BUFFER_SIZE];

static void power_on(void)
{
    bus_init();
    softswitch_reset();
}

static void hires_on(void)
{
    power_on();
    (void)bus_read(0xC050); /* TEXT off */
    (void)bus_read(0xC057); /* HIRES on */
}

static void dhires_on(void)
{
    hires_on();
    bus_write(0xC00D, 0);   /* 80COL on */
    (void)bus_read(0xC05E); /* DHIRES on */
}

static long lit_dots(void)
{
    long count = 0;
    size_t i;
    int bit;

    for (i = 0; i < sizeof out; i++) {
        for (bit = 0; bit < 8; bit++) {
            count += (out[i] >> bit) & 1;
        }
    }
    return count;
}

static const uint8_t *row(int output_row)
{
    return out + output_row * VIDEO_ROW_BYTES;
}

static void test_mode_selection(void)
{
    power_on();
    CHECK_EQ(video_mode(), VIDEO_MODE_BLANK); /* text mode */

    (void)bus_read(0xC050);
    CHECK_EQ(video_mode(), VIDEO_MODE_BLANK); /* graphics but lo-res */

    (void)bus_read(0xC057);
    CHECK_EQ(video_mode(), VIDEO_MODE_HIRES);

    (void)bus_read(0xC05E);
    CHECK_EQ(video_mode(), VIDEO_MODE_HIRES); /* DHIRES needs 80COL too */

    bus_write(0xC00D, 0);
    CHECK_EQ(video_mode(), VIDEO_MODE_DHIRES);

    (void)bus_read(0xC051);
    CHECK_EQ(video_mode(), VIDEO_MODE_BLANK); /* TEXT wins */
}

static void test_blank_clears_buffer(void)
{
    power_on();
    bus_main()[0x2000] = 0x7F;
    memset(out, 0xFF, sizeof out);
    video_render(out);
    CHECK_EQ(lit_dots(), 0);
}

static void test_hires_pixels(void)
{
    /* Pixel 0 -> dots 0 and 1, on both doubled rows. */
    hires_on();
    bus_main()[0x2000] = 0x01;
    video_render(out);
    CHECK_EQ(row(0)[0], 0xC0);
    CHECK_EQ(row(1)[0], 0xC0);
    CHECK_EQ(lit_dots(), 4);

    /* Pixel 6 -> dots 12 and 13. */
    hires_on();
    bus_main()[0x2000] = 0x40;
    video_render(out);
    CHECK_EQ(row(0)[0], 0x00);
    CHECK_EQ(row(0)[1], 0x0C);
    CHECK_EQ(lit_dots(), 4);

    /* All seven pixels -> dots 0..13. */
    hires_on();
    bus_main()[0x2000] = 0x7F;
    video_render(out);
    CHECK_EQ(row(0)[0], 0xFF);
    CHECK_EQ(row(0)[1], 0xFC);

    /* Second column starts at dot 14. */
    hires_on();
    bus_main()[0x2001] = 0x01;
    video_render(out);
    CHECK_EQ(row(0)[1], 0x03);
    CHECK_EQ(lit_dots(), 4);
}

static void test_hires_half_dot_shift(void)
{
    /* Bit 7 set: pixel 0 moves to dots 1 and 2. */
    hires_on();
    bus_main()[0x2000] = 0x81;
    video_render(out);
    CHECK_EQ(row(0)[0], 0x60);
    CHECK_EQ(lit_dots(), 4);

    /* Shifted pixel 6 is dots 13 and 14, but an unshifted next byte owns
       dot 14, so only dot 13 survives. */
    hires_on();
    bus_main()[0x2000] = 0xC0;
    bus_main()[0x2001] = 0x00;
    video_render(out);
    CHECK_EQ(row(0)[1], 0x04);
    CHECK_EQ(lit_dots(), 2);

    /* A shifted next byte keeps dot 14: it repeats the previous last dot. */
    hires_on();
    bus_main()[0x2000] = 0xC0;
    bus_main()[0x2001] = 0x80;
    video_render(out);
    CHECK_EQ(row(0)[1], 0x06);
    CHECK_EQ(lit_dots(), 4);

    /* Unshifted pixel 6 followed by a shifted byte: dots 12, 13 and 14. */
    hires_on();
    bus_main()[0x2000] = 0x40;
    bus_main()[0x2001] = 0x80;
    video_render(out);
    CHECK_EQ(row(0)[1], 0x0E);
    CHECK_EQ(lit_dots(), 6);

    /* First column shifted with nothing before it: dot 0 stays off. */
    hires_on();
    bus_main()[0x2000] = 0xFF;
    video_render(out);
    CHECK_EQ(row(0)[0], 0x7F);

    /* Last column shifted: the dot pushed past the right edge is dropped. */
    hires_on();
    bus_main()[0x2027] = 0xC0;
    video_render(out);
    CHECK_EQ(row(0)[69], 0x01);
    CHECK_EQ(lit_dots(), 2);
}

static void test_hires_row_addresses(void)
{
    hires_on();
    bus_main()[0x2400] = 0x7F; /* row 1 */
    bus_main()[0x2080] = 0x7F; /* row 8 */
    bus_main()[0x2028] = 0x7F; /* row 64 */
    bus_main()[0x3FD0] = 0x7F; /* row 191 */
    video_render(out);
    CHECK_EQ(row(0)[0], 0x00);
    CHECK_EQ(row(2)[0], 0xFF);
    CHECK_EQ(row(3)[0], 0xFF);
    CHECK_EQ(row(4)[0], 0x00);
    CHECK_EQ(row(16)[0], 0xFF);
    CHECK_EQ(row(17)[0], 0xFF);
    CHECK_EQ(row(128)[0], 0xFF);
    CHECK_EQ(row(129)[0], 0xFF);
    CHECK_EQ(row(382)[0], 0xFF);
    CHECK_EQ(row(383)[0], 0xFF);
    CHECK_EQ(lit_dots(), 4 * 2 * 14);
}

static void test_page_selection(void)
{
    /* PAGE2 shows $4000. */
    hires_on();
    bus_main()[0x2000] = 0x01;
    bus_main()[0x4000] = 0x40;
    (void)bus_read(0xC055);
    video_render(out);
    CHECK_EQ(row(0)[0], 0x00);
    CHECK_EQ(row(0)[1], 0x0C);

    (void)bus_read(0xC054);
    video_render(out);
    CHECK_EQ(row(0)[0], 0xC0);
    CHECK_EQ(row(0)[1], 0x00);

    /* With 80STORE on, PAGE2 selects memory, not the displayed page. */
    bus_write(0xC001, 0);
    (void)bus_read(0xC055);
    video_render(out);
    CHECK_EQ(row(0)[0], 0xC0);
    CHECK_EQ(row(0)[1], 0x00);
}

static void test_mixed_is_ignored(void)
{
    hires_on();
    (void)bus_read(0xC053);
    bus_main()[0x2250] = 0x7F; /* row 160, inside the mixed-mode text area */
    video_render(out);
    CHECK_EQ(row(320)[0], 0xFF);
}

static void test_dhires(void)
{
    /* Aux byte is dots 0..6, main byte dots 7..13, one dot per bit. */
    dhires_on();
    bus_aux()[0x2000] = 0x01;
    video_render(out);
    CHECK_EQ(row(0)[0], 0x80);
    CHECK_EQ(row(1)[0], 0x80);
    CHECK_EQ(lit_dots(), 2);

    dhires_on();
    bus_main()[0x2000] = 0x01;
    video_render(out);
    CHECK_EQ(row(0)[0], 0x01);

    dhires_on();
    bus_aux()[0x2000] = 0x40;
    bus_main()[0x2000] = 0x40;
    video_render(out);
    CHECK_EQ(row(0)[0], 0x02);
    CHECK_EQ(row(0)[1], 0x04);

    /* Bit 7 is ignored in double hi-res. */
    dhires_on();
    bus_aux()[0x2000] = 0x80;
    bus_main()[0x2000] = 0x80;
    video_render(out);
    CHECK_EQ(lit_dots(), 0);

    /* Last column, last row. */
    dhires_on();
    bus_main()[0x3FF7] = 0x40;
    video_render(out);
    CHECK_EQ(row(383)[69], 0x01);
    CHECK_EQ(lit_dots(), 2);

    /* Page 2 in double hi-res. */
    dhires_on();
    (void)bus_read(0xC055);
    bus_aux()[0x4000] = 0x01;
    video_render(out);
    CHECK_EQ(row(0)[0], 0x80);
}

int main(void)
{
    test_mode_selection();
    test_blank_clears_buffer();
    test_hires_pixels();
    test_hires_half_dot_shift();
    test_hires_row_addresses();
    test_page_selection();
    test_mixed_is_ignored();
    test_dhires();
    return TEST_RESULT();
}
