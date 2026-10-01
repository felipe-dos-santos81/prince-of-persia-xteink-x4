#ifndef MACHINE_H
#define MACHINE_H

#include <stddef.h>
#include <stdint.h>

#define MACHINE_VIDEO_WIDTH 560
#define MACHINE_VIDEO_HEIGHT 384
#define MACHINE_VIDEO_BUFFER_SIZE 26880
#define MACHINE_CYCLES_PER_FRAME 17030u

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { MACHINE_RUNNING, MACHINE_HALTED } machine_status_t;

/* Reset the machine and boot the game from a 819,200-byte disk image. The
   image is read in place and must outlive the machine. */
void machine_init(const uint8_t *image, size_t image_len);

/* Run one video frame (17,030 CPU cycles). Does nothing once halted. */
machine_status_t machine_run_frame(void);

/* Render the visible page: 384 rows of 70 bytes, most significant bit
   leftmost, 1 = lit (white) dot. out must hold MACHINE_VIDEO_BUFFER_SIZE. */
void machine_render(uint8_t *out);

/* Why the machine halted. Empty string while running. */
const char *machine_halt_reason(void);

/* Total emulated CPU cycles since machine_init. */
uint64_t machine_cycles(void);

/* "blank", "hires" or "dhires": what machine_render currently draws. */
const char *machine_video_mode_name(void);

#ifdef __cplusplus
}
#endif

#endif
