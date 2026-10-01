#include <string.h>

#include "machine.h"
#include "scale.h"
#include "test.h"

static uint8_t in[MACHINE_VIDEO_BUFFER_SIZE];
static uint8_t out[SCALE_ROW_BYTES * SCALE_HEIGHT];

static int get_in(int x, int y)
{
    return (in[y * MACHINE_VIDEO_ROW_BYTES + x / 8] >> (7 - x % 8)) & 1;
}

static int get_out(int x, int y)
{
    return (out[y * SCALE_ROW_BYTES + x / 8] >> (7 - x % 8)) & 1;
}

static void scale_all(bool rotate_180, bool invert)
{
    for (int y = 0; y < SCALE_HEIGHT; y++) {
        scale_row(in, y, rotate_180, invert, out + y * SCALE_ROW_BYTES);
    }
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
}

static void test_matches_reference_in_every_orientation(void)
{
    for (int rotate = 0; rotate < 2; rotate++) {
        for (int invert = 0; invert < 2; invert++) {
            fill_random(42u + (uint32_t)(rotate * 2 + invert));
            memset(out, 0, sizeof out);
            scale_all(rotate, invert);
            CHECK_EQ(count_mismatches(rotate, invert), 0);
        }
    }
}

static void test_single_dot_becomes_its_block(void)
{
    /* Game dot (7, 4) covers panel columns 10..11 (x*7/10 == 7) and rows
       5..6 (y*4/5 == 4). */
    memset(in, 0, sizeof in);
    in[4 * MACHINE_VIDEO_ROW_BYTES + 0] = 0x01; /* x = 7 */
    memset(out, 0, sizeof out);
    scale_all(false, false);
    CHECK_EQ(get_out(10, 5), 1);
    CHECK_EQ(get_out(10, 6), 1);
    CHECK_EQ(get_out(11, 6), 1);
    CHECK_EQ(get_out(9, 5), 0);
    CHECK_EQ(get_out(12, 5), 0);
    CHECK_EQ(get_out(10, 4), 0);
    CHECK_EQ(get_out(10, 7), 0);
}

static void test_last_column_reads_inside_the_row(void)
{
    /* The last 7-dot group of a row starts in its last byte; the dot after
       it must not come from the next row. Only row 1's first dot is lit. */
    memset(in, 0, sizeof in);
    in[1 * MACHINE_VIDEO_ROW_BYTES] = 0x80;
    memset(out, 0, sizeof out);
    scale_row(in, 0, false, false, out);
    CHECK_EQ(out[SCALE_ROW_BYTES - 1], 0);
}

static void test_source_row(void)
{
    CHECK_EQ(scale_source_row(0, false), 0);
    CHECK_EQ(scale_source_row(4, false), 3);
    CHECK_EQ(scale_source_row(5, false), 4);
    CHECK_EQ(scale_source_row(479, false), 383);
    CHECK_EQ(scale_source_row(0, true), 383);
    CHECK_EQ(scale_source_row(479, true), 0);
}

int main(void)
{
    test_source_row();
    test_sizes();
    test_matches_reference_in_every_orientation();
    test_single_dot_becomes_its_block();
    test_last_column_reads_inside_the_row();
    return TEST_RESULT();
}
