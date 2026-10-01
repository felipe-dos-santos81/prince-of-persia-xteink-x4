#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

/* Initialise the panel and clear it with one full refresh. */
void display_begin(void);

/* Show a 560x384 picture (MACHINE_VIDEO_BUFFER_SIZE bytes) stretched to the
   whole panel and block until the panel has finished refreshing. Returns
   false, without touching the panel, if the picture is what is already
   shown. */
bool display_show(const uint8_t *picture);

/* Replace the picture with a "HALTED" message and the start of reason,
   using one full refresh. Uses picture as scratch space. */
void display_show_halt(uint8_t *picture, const char *reason);

#endif
