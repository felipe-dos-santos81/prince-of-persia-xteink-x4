#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

/* Initialise the panel and clear it with one full refresh. */
void display_begin(void);

/* Show a 560x384 picture (MACHINE_VIDEO_BUFFER_SIZE bytes) in the game
   window and block until the panel has finished refreshing. The buffer may
   be modified. */
void display_show(uint8_t *picture);

/* Replace the picture with a "HALTED" message and the start of reason,
   using one full refresh. Uses picture as scratch space. */
void display_show_halt(uint8_t *picture, const char *reason);

#endif
