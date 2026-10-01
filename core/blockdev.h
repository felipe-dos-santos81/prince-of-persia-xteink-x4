#ifndef BLOCKDEV_H
#define BLOCKDEV_H

#include <stddef.h>
#include <stdint.h>

#define BLOCKDEV_BLOCK_SIZE 512u

/* Cycles charged to the emulated machine for one SmartPort call. */
#define BLOCKDEV_CALL_CYCLES 1000u

#define BLOCKDEV_OK 0x00
#define BLOCKDEV_ERR_BAD_COMMAND 0x01
#define BLOCKDEV_ERR_IO 0x27
#define BLOCKDEV_ERR_WRITE_PROTECTED 0x2B

/* The image must stay valid for as long as the machine runs. */
void blockdev_init(const uint8_t *image, size_t image_len);

/* Service a SmartPort call. Call this instead of stepping the CPU when the
   program counter is ROM_SMARTPORT_ENTRY. Reads the inline parameters after
   the caller's JSR, performs the command, sets A and carry, and resumes the
   caller after the parameters. */
void blockdev_call(void);

#endif
