#ifndef SCALE_H
#define SCALE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stretches the 560x384 picture to the whole 800x480 panel: every 7 dots
   become 10 across and every 4 rows become 5 down (nearest neighbour). */
#define SCALE_WIDTH 800
#define SCALE_HEIGHT 480
#define SCALE_ROW_BYTES (SCALE_WIDTH / 8)
#define SCALE_BUFFER_SIZE (SCALE_ROW_BYTES * SCALE_HEIGHT)

/* Scales `in` (VIDEO_BUFFER_SIZE bytes) into `out` (SCALE_BUFFER_SIZE bytes),
   turning it upside down and/or inverting it on the way. `out` is expected
   to hold the previous result: returns true if any of it changed. */
bool scale_picture(const uint8_t *in, uint8_t *out, bool rotate_180, bool invert);

#ifdef __cplusplus
}
#endif

#endif
