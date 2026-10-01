/* Tests against the real game image.
 *
 *   test_boot <image> <golden.txt>
 *
 * Skips (exit 0) with a message when the image has not been built.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "test.h"

#define IMAGE_SIZE 819200
#define SMOKE_MAX_FRAMES 3000
#define SMOKE_MIN_LIT_DOTS 1000
#define MAX_GOLDEN 16

static uint8_t picture[MACHINE_VIDEO_BUFFER_SIZE];

static uint64_t fnv1a64(const uint8_t *data, size_t len)
{
    uint64_t hash = 0xcbf29ce484222325ULL;
    size_t i;

    for (i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

static long lit_dots(void)
{
    long count = 0;
    size_t i;
    int bit;

    for (i = 0; i < sizeof picture; i++) {
        for (bit = 0; bit < 8; bit++) {
            count += (picture[i] >> bit) & 1;
        }
    }
    return count;
}

static void test_smoke(const uint8_t *image)
{
    long frame;
    int reached = 0;

    machine_init(image, IMAGE_SIZE);
    for (frame = 1; frame <= SMOKE_MAX_FRAMES && !reached; frame++) {
        if (machine_run_frame() == MACHINE_HALTED) {
            printf("FAIL halted at frame %ld: %s\n", frame, machine_halt_reason());
            test_failures++;
            return;
        }
        if (strcmp(machine_video_mode_name(), "blank") != 0) {
            machine_render(picture);
            reached = lit_dots() >= SMOKE_MIN_LIT_DOTS;
        }
    }
    CHECK(reached);
}

static void test_golden(const uint8_t *image, const char *golden_path)
{
    long frames[MAX_GOLDEN];
    uint64_t hashes[MAX_GOLDEN];
    int count = 0;
    char line[128];
    FILE *f = fopen(golden_path, "r");
    long frame = 0;
    int i;

    if (f == NULL) {
        printf("FAIL cannot open %s\n", golden_path);
        test_failures++;
        return;
    }
    while (fgets(line, sizeof line, f) != NULL && count < MAX_GOLDEN) {
        unsigned long long hash;
        if (line[0] == '#' || sscanf(line, "%ld %llx", &frames[count], &hash) != 2) {
            continue;
        }
        hashes[count++] = hash;
    }
    fclose(f);
    CHECK(count > 0);

    /* Entries must be in increasing frame order. */
    machine_init(image, IMAGE_SIZE);
    for (i = 0; i < count; i++) {
        uint64_t actual;

        while (frame < frames[i]) {
            if (machine_run_frame() == MACHINE_HALTED) {
                printf("FAIL halted at frame %ld: %s\n", frame + 1, machine_halt_reason());
                test_failures++;
                return;
            }
            frame++;
        }
        machine_render(picture);
        actual = fnv1a64(picture, sizeof picture);
        if (actual != hashes[i]) {
            printf("FAIL frame %ld hash is %016llx, expected %016llx\n", frames[i],
                   (unsigned long long)actual, (unsigned long long)hashes[i]);
            test_failures++;
        }
    }
}

int main(int argc, char **argv)
{
    FILE *f;
    uint8_t *image;

    if (argc != 3) {
        printf("usage: test_boot <image> <golden.txt>\n");
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (f == NULL) {
        printf("SKIP %s not found (run game/build.sh)\n", argv[1]);
        return 0;
    }
    image = malloc(IMAGE_SIZE + 1);
    if (image == NULL || fread(image, 1, IMAGE_SIZE + 1, f) != IMAGE_SIZE) {
        printf("FAIL %s is not %d bytes\n", argv[1], IMAGE_SIZE);
        fclose(f);
        return 1;
    }
    fclose(f);

    test_smoke(image);
    test_golden(image, argv[2]);
    free(image);
    return TEST_RESULT();
}
