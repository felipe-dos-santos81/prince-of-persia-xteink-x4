#include "boot.h"

#include <string.h>

#include "bus.h"
#include "cpu.h"

#define BOOT_FILE_OFFSET (8u * 512u)
#define BOOT_FILE_SIZE (8u * 512u)
#define BOOT_LOAD_ADDR 0x2000
#define BOOT_UNIT 0x50 /* slot 5, drive 1 */

const char *boot_load(const uint8_t *image, size_t image_len)
{
    cpu_regs_t r;

    if (image == NULL) {
        return "no disk image";
    }
    if (image_len != BOOT_IMAGE_SIZE) {
        return "disk image is not 819200 bytes";
    }
    /* POPBOOT35 starts with BIT abs ($2C). */
    if (image[BOOT_FILE_OFFSET] != 0x2C) {
        return "disk image has no boot file at block 8";
    }

    memcpy(bus_main() + BOOT_LOAD_ADDR, image + BOOT_FILE_OFFSET, BOOT_FILE_SIZE);
    bus_main()[0x43] = BOOT_UNIT;

    r.pc = BOOT_LOAD_ADDR;
    r.a = 0x00;
    r.x = BOOT_UNIT;
    r.y = 0x00;
    r.sp = 0xFF;
    r.status = CPU_FLAG_INTERRUPT | CPU_FLAG_CONSTANT;
    cpu_set_regs(&r);
    return NULL;
}
