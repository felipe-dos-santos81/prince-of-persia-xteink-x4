#include "read_file.h"

#include <stdio.h>
#include <stdlib.h>

uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *data;
    long size;

    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc(size > 0 ? (size_t)size : 1);
    if (data == NULL || fread(data, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(data);
        return NULL;
    }
    fclose(f);
    *len = (size_t)size;
    return data;
}
