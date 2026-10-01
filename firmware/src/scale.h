#ifndef SCALE_H
#define SCALE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stretches the 560x384 game picture to the whole 800x480 panel: every 7
   dots become 10 across and every 4 rows become 5 down (nearest neighbour). */
#define SCALE_WIDTH 800
#define SCALE_HEIGHT 480
#define SCALE_ROW_BYTES (SCALE_WIDTH / 8)

/* The game picture row that panel row y shows. */
int scale_source_row(int y, bool rotate_180);

/* Writes panel row y (0..SCALE_HEIGHT-1) of the stretched picture into out
   (SCALE_ROW_BYTES bytes), turning the picture upside down and/or inverting
   it on the way. picture holds MACHINE_VIDEO_BUFFER_SIZE bytes. */
void scale_row(const uint8_t *picture, int y, bool rotate_180, bool invert, uint8_t *out);

#ifdef __cplusplus
}
#endif

#endif
