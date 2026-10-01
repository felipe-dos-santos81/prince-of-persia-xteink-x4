#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>

#define VIDEO_WIDTH 560
#define VIDEO_HEIGHT 384
#define VIDEO_ROW_BYTES 70
#define VIDEO_BUFFER_SIZE (VIDEO_ROW_BYTES * VIDEO_HEIGHT) /* 26880 */

typedef enum { VIDEO_MODE_BLANK, VIDEO_MODE_HIRES, VIDEO_MODE_DHIRES } video_mode_t;

/* Which mode the current soft-switch state selects. */
video_mode_t video_mode(void);

/* Render the visible page into out (VIDEO_BUFFER_SIZE bytes): 384 rows of
   70 bytes, most significant bit leftmost, 1 = lit dot. */
void video_render(uint8_t *out);

#endif
