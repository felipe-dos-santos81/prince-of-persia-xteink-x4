#ifndef BOOT_H
#define BOOT_H

#include <stddef.h>
#include <stdint.h>

#define BOOT_IMAGE_SIZE 819200u

/* Put the machine in the state the ProDOS boot block would leave: the game's
   4 KB boot file (image blocks 8-15) at $2000, boot unit in $43, PC = $2000.
   Call after bus_init() and softswitch_reset().
   Returns NULL on success, or a static message describing why the image was
   rejected. */
const char *boot_load(const uint8_t *image, size_t image_len);

#endif
