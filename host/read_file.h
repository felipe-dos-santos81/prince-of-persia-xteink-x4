#ifndef READ_FILE_H
#define READ_FILE_H

#include <stddef.h>
#include <stdint.h>

/* Reads a whole file into a malloc'd buffer and stores its size in *len.
   Returns NULL if the file cannot be read. The caller frees the buffer. */
uint8_t *read_file(const char *path, size_t *len);

#endif
