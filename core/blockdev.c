#include "blockdev.h"

#include "bus.h"
#include "cpu.h"

#define CMD_READ_BLOCK 0x01
#define CMD_WRITE_BLOCK 0x02

static const uint8_t *image;
static size_t image_len;

void blockdev_init(const uint8_t *data, size_t len)
{
    image = data;
    image_len = len;
}

static uint16_t read16(uint16_t addr)
{
    return (uint16_t)(bus_read(addr) | (bus_read((uint16_t)(addr + 1)) << 8));
}

static uint8_t read_block(uint16_t list)
{
    uint16_t buffer = read16((uint16_t)(list + 2));
    uint32_t block = (uint32_t)bus_read((uint16_t)(list + 4))
                   | ((uint32_t)bus_read((uint16_t)(list + 5)) << 8)
                   | ((uint32_t)bus_read((uint16_t)(list + 6)) << 16);
    size_t offset = (size_t)block * BLOCKDEV_BLOCK_SIZE;
    uint32_t i;

    if (image == NULL || offset + BLOCKDEV_BLOCK_SIZE > image_len) {
        return BLOCKDEV_ERR_IO;
    }
    /* Through the bus, so the caller's banking decides where it lands. */
    for (i = 0; i < BLOCKDEV_BLOCK_SIZE; i++) {
        bus_write((uint16_t)(buffer + i), image[offset + i]);
    }
    return BLOCKDEV_OK;
}

void blockdev_call(void)
{
    cpu_regs_t r;
    uint16_t ret;
    uint8_t command;
    uint16_t list;
    uint8_t result;

    cpu_get_regs(&r);

    /* Pull the JSR return address. It points at the last byte of the JSR,
       so the inline parameters start one byte after it. */
    r.sp++;
    ret = bus_read((uint16_t)(0x0100 + r.sp));
    r.sp++;
    ret |= (uint16_t)(bus_read((uint16_t)(0x0100 + r.sp)) << 8);

    command = bus_read((uint16_t)(ret + 1));
    list = read16((uint16_t)(ret + 2));

    switch (command) {
    case CMD_READ_BLOCK:
        result = read_block(list);
        break;
    case CMD_WRITE_BLOCK:
        result = BLOCKDEV_ERR_WRITE_PROTECTED;
        break;
    default:
        result = BLOCKDEV_ERR_BAD_COMMAND;
        break;
    }

    r.a = result;
    if (result == BLOCKDEV_OK) {
        r.status &= (uint8_t)~CPU_FLAG_CARRY;
    } else {
        r.status |= CPU_FLAG_CARRY;
    }
    r.pc = (uint16_t)(ret + 4); /* skip command byte and list pointer */
    cpu_set_regs(&r);
}
