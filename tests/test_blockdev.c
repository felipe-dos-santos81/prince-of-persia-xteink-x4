#include <string.h>

#include "blockdev.h"
#include "bus.h"
#include "cpu.h"
#include "rom.h"
#include "softswitch.h"
#include "test.h"

#define IMAGE_BLOCKS 4
#define CALL_ADDR 0x0300
#define LIST_ADDR 0x0310

static uint8_t image[IMAGE_BLOCKS * 512];

static void make_image(void)
{
    size_t i;

    for (i = 0; i < sizeof image; i++) {
        image[i] = (uint8_t)((i * 7 + i / 512) & 0xFF);
    }
}

/* Assemble "JSR $C583 / db command / dw LIST_ADDR" at CALL_ADDR and the
   parameter list at LIST_ADDR, then execute the JSR so the CPU is sitting
   on the SmartPort entry exactly as the game would be. */
static void prepare_call(uint8_t command, uint16_t buffer, uint32_t block)
{
    cpu_regs_t r = { CALL_ADDR, 0xEE, 0x12, 0x34, 0xFF, CPU_FLAG_CONSTANT };
    uint8_t *m = bus_main();

    m[CALL_ADDR + 0] = 0x20;
    m[CALL_ADDR + 1] = (uint8_t)(ROM_SMARTPORT_ENTRY & 0xFF);
    m[CALL_ADDR + 2] = (uint8_t)(ROM_SMARTPORT_ENTRY >> 8);
    m[CALL_ADDR + 3] = command;
    m[CALL_ADDR + 4] = (uint8_t)(LIST_ADDR & 0xFF);
    m[CALL_ADDR + 5] = (uint8_t)(LIST_ADDR >> 8);

    m[LIST_ADDR + 0] = 3;
    m[LIST_ADDR + 1] = 1;
    m[LIST_ADDR + 2] = (uint8_t)(buffer & 0xFF);
    m[LIST_ADDR + 3] = (uint8_t)(buffer >> 8);
    m[LIST_ADDR + 4] = (uint8_t)(block & 0xFF);
    m[LIST_ADDR + 5] = (uint8_t)((block >> 8) & 0xFF);
    m[LIST_ADDR + 6] = (uint8_t)((block >> 16) & 0xFF);

    cpu_set_regs(&r);
    cpu_step();
}

static void power_on(void)
{
    bus_init();
    softswitch_reset();
    blockdev_init(image, sizeof image);
}

static void test_read_block(void)
{
    cpu_regs_t r;

    power_on();
    prepare_call(0x01, 0x4000, 2);
    CHECK_EQ(cpu_pc(), ROM_SMARTPORT_ENTRY);

    blockdev_call();
    cpu_get_regs(&r);

    CHECK_EQ(r.pc, CALL_ADDR + 6); /* resumes after the inline parameters */
    CHECK_EQ(r.sp, 0xFF);          /* return address consumed */
    CHECK_EQ(r.a, BLOCKDEV_OK);
    CHECK_EQ(r.status & CPU_FLAG_CARRY, 0);
    CHECK_EQ(r.x, 0x12);
    CHECK_EQ(r.y, 0x34);
    CHECK(memcmp(bus_main() + 0x4000, image + 2 * 512, 512) == 0);
    CHECK_EQ(bus_main()[0x3FFF], 0x00); /* nothing before the buffer */
    CHECK_EQ(bus_main()[0x4200], 0x00); /* nothing after the buffer */
}

static void test_read_first_and_last_block(void)
{
    power_on();
    prepare_call(0x01, 0x4000, 0);
    blockdev_call();
    CHECK(memcmp(bus_main() + 0x4000, image, 512) == 0);

    prepare_call(0x01, 0x6000, IMAGE_BLOCKS - 1);
    blockdev_call();
    CHECK(memcmp(bus_main() + 0x6000, image + (IMAGE_BLOCKS - 1) * 512, 512) == 0);
}

static void test_read_follows_banking(void)
{
    cpu_regs_t r;

    /* RAMWRT aux: data lands in aux memory. */
    power_on();
    prepare_call(0x01, 0x4000, 1);
    bus_write(0xC005, 0);
    blockdev_call();
    CHECK(memcmp(bus_aux() + 0x4000, image + 512, 512) == 0);
    CHECK_EQ(bus_main()[0x4000], 0x00);

    /* Language card write-enabled: data lands in $E000 RAM. */
    power_on();
    prepare_call(0x01, 0xE000, 3);
    (void)bus_read(0xC083);
    (void)bus_read(0xC083);
    blockdev_call();
    CHECK(memcmp(bus_main() + 0xE000, image + 3 * 512, 512) == 0);

    /* Language card write-protected: the call succeeds, nothing is stored. */
    power_on();
    prepare_call(0x01, 0xE000, 3);
    blockdev_call();
    cpu_get_regs(&r);
    CHECK_EQ(r.a, BLOCKDEV_OK);
    CHECK_EQ(bus_main()[0xE000], 0x00);
}

static void test_read_out_of_range(void)
{
    cpu_regs_t r;

    power_on();
    prepare_call(0x01, 0x4000, IMAGE_BLOCKS);
    blockdev_call();
    cpu_get_regs(&r);
    CHECK_EQ(r.a, BLOCKDEV_ERR_IO);
    CHECK_EQ(r.status & CPU_FLAG_CARRY, CPU_FLAG_CARRY);
    CHECK_EQ(r.pc, CALL_ADDR + 6);
    CHECK_EQ(bus_main()[0x4000], 0x00);

    /* The block number is 24 bits. */
    prepare_call(0x01, 0x4000, 0x010001);
    blockdev_call();
    cpu_get_regs(&r);
    CHECK_EQ(r.a, BLOCKDEV_ERR_IO);
}

static void test_write_is_protected(void)
{
    cpu_regs_t r;

    power_on();
    bus_main()[0x4000] = 0x99;
    prepare_call(0x02, 0x4000, 1);
    blockdev_call();
    cpu_get_regs(&r);
    CHECK_EQ(r.a, BLOCKDEV_ERR_WRITE_PROTECTED);
    CHECK_EQ(r.status & CPU_FLAG_CARRY, CPU_FLAG_CARRY);
    CHECK_EQ(r.pc, CALL_ADDR + 6);
    CHECK_EQ(image[512], (uint8_t)((512 * 7 + 1) & 0xFF)); /* image untouched */
}

static void test_unknown_command(void)
{
    cpu_regs_t r;

    power_on();
    prepare_call(0x00, 0x4000, 1);
    blockdev_call();
    cpu_get_regs(&r);
    CHECK_EQ(r.a, BLOCKDEV_ERR_BAD_COMMAND);
    CHECK_EQ(r.status & CPU_FLAG_CARRY, CPU_FLAG_CARRY);
    CHECK_EQ(r.pc, CALL_ADDR + 6);
}

static void test_no_image(void)
{
    cpu_regs_t r;

    power_on();
    blockdev_init(NULL, 0);
    prepare_call(0x01, 0x4000, 0);
    blockdev_call();
    cpu_get_regs(&r);
    CHECK_EQ(r.a, BLOCKDEV_ERR_IO);
    CHECK_EQ(r.status & CPU_FLAG_CARRY, CPU_FLAG_CARRY);
}

int main(void)
{
    make_image();
    test_read_block();
    test_read_first_and_last_block();
    test_read_follows_banking();
    test_read_out_of_range();
    test_write_is_protected();
    test_unknown_command();
    test_no_image();
    return TEST_RESULT();
}
