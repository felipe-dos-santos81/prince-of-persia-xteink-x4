/* pop-headless: run the machine without a window.
 *
 *   pop-headless <image> --frames N [--dump K:path.pbm]... [--hash K]...
 *
 * Frame numbers count from 1: "--hash 600" hashes the picture after the
 * 600th frame has run. Exits 1 if the machine halts, 2 on usage errors.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "read_file.h"

#define MAX_ACTIONS 64

typedef struct {
    long frame;
    const char *path; /* NULL = print hash */
} action_t;

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

static int write_pbm(const char *path, const uint8_t *picture)
{
    FILE *f = fopen(path, "wb");
    size_t i;

    if (f == NULL) {
        return -1;
    }
    fprintf(f, "P4\n%d %d\n", MACHINE_VIDEO_WIDTH, MACHINE_VIDEO_HEIGHT);
    /* PBM uses 1 = black; the picture uses 1 = lit. Invert so lit is white. */
    for (i = 0; i < MACHINE_VIDEO_BUFFER_SIZE; i++) {
        fputc((uint8_t)~picture[i], f);
    }
    fclose(f);
    return 0;
}

static int usage(void)
{
    fprintf(stderr, "usage: pop-headless <image> --frames N "
                    "[--dump K:path.pbm]... [--hash K]...\n");
    return 2;
}

int main(int argc, char **argv)
{
    static uint8_t picture[MACHINE_VIDEO_BUFFER_SIZE];
    action_t actions[MAX_ACTIONS];
    int action_count = 0;
    long frames = -1;
    uint8_t *image;
    size_t image_len = 0;
    long frame;
    int i;

    if (argc < 4) {
        return usage();
    }
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--hash") == 0 && i + 1 < argc && action_count < MAX_ACTIONS) {
            actions[action_count].frame = strtol(argv[++i], NULL, 10);
            actions[action_count].path = NULL;
            action_count++;
        } else if (strcmp(argv[i], "--dump") == 0 && i + 1 < argc && action_count < MAX_ACTIONS) {
            char *colon = strchr(argv[++i], ':');
            if (colon == NULL) {
                return usage();
            }
            actions[action_count].frame = strtol(argv[i], NULL, 10);
            actions[action_count].path = colon + 1;
            action_count++;
        } else {
            return usage();
        }
    }
    if (frames <= 0) {
        return usage();
    }

    image = read_file(argv[1], &image_len);
    if (image == NULL) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }

    machine_init(image, image_len);

    for (frame = 1; frame <= frames; frame++) {
        if (machine_run_frame() == MACHINE_HALTED) {
            fprintf(stderr, "halted at frame %ld: %s\n", frame, machine_halt_reason());
            return 1;
        }
        for (i = 0; i < action_count; i++) {
            if (actions[i].frame != frame) {
                continue;
            }
            machine_render(picture);
            if (actions[i].path == NULL) {
                printf("frame %ld hash %016llx\n", frame,
                       (unsigned long long)fnv1a64(picture, sizeof picture));
            } else if (write_pbm(actions[i].path, picture) != 0) {
                fprintf(stderr, "cannot write %s\n", actions[i].path);
                return 2;
            }
        }
    }

    printf("frames %ld cycles %llu mode %s\n", frames,
           (unsigned long long)machine_cycles(), machine_video_mode_name());
    free(image);
    return 0;
}
