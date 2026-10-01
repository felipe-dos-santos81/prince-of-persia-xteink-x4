#include <string.h>

#include "scale.h"
#include "test.h"
#include "video.h"

static uint8_t in[VIDEO_BUFFER_SIZE];
static uint8_t out[SCALE_BUFFER_SIZE];

static int get_in(int x, int y)
{
    return (in[y * VIDEO_ROW_BYTES + x / 8] >> (7 - x % 8)) & 1;
}

static int get_out(int x, int y)
{
    return (out[y * SCALE_ROW_BYTES + x / 8] >> (7 - x % 8)) & 1;
}

/* Nearest-neighbour reference: panel pixel (x, y) shows game pixel
   (x * 7 / 10, y * 4 / 5), before rotation and inversion. */
static int expected(int x, int y, bool rotate_180, bool invert)
{
    if (rotate_180) {
        x = SCALE_WIDTH - 1 - x;
        y = SCALE_HEIGHT - 1 - y;
    }
    return get_in(x * 7 / 10, y * 4 / 5) ^ (invert ? 1 : 0);
}

static void fill_random(uint32_t seed)
{
    for (size_t i = 0; i < sizeof in; i++) {
        seed = seed * 1103515245u + 12345u;
        in[i] = (uint8_t)(seed >> 16);
    }
}

static int count_mismatches(bool rotate_180, bool invert)
{
    int bad = 0;

    for (int y = 0; y < SCALE_HEIGHT; y++) {
        for (int x = 0; x < SCALE_WIDTH; x++) {
            bad += get_out(x, y) != expected(x, y, rotate_180, invert);
        }
    }
    return bad;
}

static void test_sizes(void)
{
    CHECK_EQ(SCALE_WIDTH, 800);
    CHECK_EQ(SCALE_HEIGHT, 480);
    CHECK_EQ(SCALE_ROW_BYTES, 100);
    CHECK_EQ(SCALE_BUFFER_SIZE, 48000);
}

static void test_matches_reference_in_every_orientation(void)
{
    for (int rotate = 0; rotate < 2; rotate++) {
        for (int invert = 0; invert < 2; invert++) {
            fill_random(42u + (uint32_t)(rotate * 2 + invert));
            memset(out, 0, sizeof out);
            scale_picture(in, out, rotate, invert);
            CHECK_EQ(count_mismatches(rotate, invert), 0);
        }
    }
}

static void test_single_dot_becomes_its_block(void)
{
    /* Game dot (7, 4) covers panel columns 10..11 (x*7/10 == 7) and rows
       5..6 (y*4/5 == 4). */
    memset(in, 0, sizeof in);
    in[4 * VIDEO_ROW_BYTES + 0] = 0x01; /* x = 7 */
    memset(out, 0, sizeof out);
    scale_picture(in, out, false, false);
    CHECK_EQ(get_out(10, 5), 1);
    CHECK_EQ(get_out(10, 6), 1);
    CHECK_EQ(get_out(11, 6), 1);
    CHECK_EQ(get_out(9, 5), 0);
    CHECK_EQ(get_out(12, 5), 0);
    CHECK_EQ(get_out(10, 4), 0);
    CHECK_EQ(get_out(10, 7), 0);
}

static void test_reports_whether_output_changed(void)
{
    fill_random(7u);
    memset(out, 0, sizeof out);
    CHECK(scale_picture(in, out, false, false));
    CHECK(!scale_picture(in, out, false, false));

    in[100] ^= 0x10;
    CHECK(scale_picture(in, out, false, false));
    CHECK_EQ(count_mismatches(false, false), 0);
}

static void test_blank_into_blank_is_no_change(void)
{
    memset(in, 0, sizeof in);
    memset(out, 0, sizeof out);
    CHECK(!scale_picture(in, out, false, false));
    memset(out, 0xFF, sizeof out);
    CHECK(!scale_picture(in, out, false, true));
}

int main(void)
{
    test_sizes();
    test_matches_reference_in_every_orientation();
    test_single_dot_becomes_its_block();
    test_reports_whether_output_changed();
    test_blank_into_blank_is_no_change();
    return TEST_RESULT();
}
