/* Machine tests that need no game image: they boot tiny synthetic images. */
#include <stdlib.h>
#include <string.h>

#include "bus.h"
#include "cpu.h"
#include "machine.h"
#include "softswitch.h"
#include "test.h"

#define IMAGE_SIZE 819200
#define BOOT_OFFSET (8 * 512)

static uint8_t *image;

/* A valid-looking image whose boot file is the given code, loaded at $2000. */
static void make_image(const uint8_t *code, size_t len)
{
    memset(image, 0, IMAGE_SIZE);
    memcpy(image + BOOT_OFFSET, code, len);
}

static void test_boot_state(void)
{
    /* BIT $0000 / JMP $2003 (spin) */
    static const uint8_t code[] = { 0x2C, 0x00, 0x00, 0x4C, 0x03, 0x20 };
    cpu_regs_t r;

    make_image(code, sizeof code);
    image[BOOT_OFFSET + 8 * 512 - 1] = 0xAB; /* last byte of the boot file */
    image[BOOT_OFFSET + 8 * 512] = 0xCD;     /* first byte after it */

    machine_init(image, IMAGE_SIZE);
    cpu_get_regs(&r);
    CHECK_EQ(r.pc, 0x2000);
    CHECK_EQ(r.sp, 0xFF);
    CHECK_EQ(r.x, 0x50);
    CHECK_EQ(r.status & CPU_FLAG_INTERRUPT, CPU_FLAG_INTERRUPT);
    CHECK_EQ(bus_main()[0x43], 0x50);
    CHECK_EQ(bus_main()[0x2000], 0x2C);
    CHECK_EQ(bus_main()[0x2FFF], 0xAB);
    CHECK_EQ(bus_main()[0x3000], 0x00); /* only 4 KB is loaded */
    CHECK(softswitch_state()->text);
    CHECK_EQ(machine_cycles(), 0);
    CHECK_EQ(machine_halt_reason()[0], '\0');
    CHECK(strcmp(machine_video_mode_name(), "blank") == 0);
}

static void test_run_frame_counts_cycles(void)
{
    static const uint8_t code[] = { 0x2C, 0x00, 0x00, 0x4C, 0x03, 0x20 };
    uint64_t after_one;

    make_image(code, sizeof code);
    machine_init(image, IMAGE_SIZE);

    CHECK_EQ(machine_run_frame(), MACHINE_RUNNING);
    after_one = machine_cycles();
    /* A frame is 17,030 cycles; the last instruction may overshoot by a few. */
    CHECK(after_one >= 17030 && after_one < 17030 + 7);

    /* The overshoot is carried, so 100 frames stay within one instruction
       of 100 * 17,030. */
    {
        int i;
        for (i = 1; i < 100; i++) {
            machine_run_frame();
        }
    }
    CHECK(machine_cycles() >= 1703000 && machine_cycles() < 1703000 + 7);
}

static void test_vertical_blank_is_visible_to_the_program(void)
{
    /* Wait for vertical blank the way the game does, then store a marker:
     *   2000 BIT $0000
     *   2003 LDA $C019 / BPL $2003   wait while in vertical blank
     *   2008 LDA $C019 / BMI $2008   wait for vertical blank to begin
     *   200D INC $0300
     *   2010 JMP $2003
     */
    static const uint8_t code[] = {
        0x2C, 0x00, 0x00,
        0xAD, 0x19, 0xC0, 0x10, 0xFB,
        0xAD, 0x19, 0xC0, 0x30, 0xFB,
        0xEE, 0x00, 0x03,
        0x4C, 0x03, 0x20,
    };
    int i;

    make_image(code, sizeof code);
    machine_init(image, IMAGE_SIZE);
    for (i = 0; i < 10; i++) {
        machine_run_frame();
    }
    /* Exactly one vertical blank per frame. */
    CHECK_EQ(bus_main()[0x0300], 10);
}

static void test_halts_on_brk(void)
{
    static const uint8_t code[] = { 0x2C, 0x00, 0x00, 0x00 };
    uint64_t cycles;

    make_image(code, sizeof code);
    machine_init(image, IMAGE_SIZE);
    CHECK_EQ(machine_run_frame(), MACHINE_HALTED);
    CHECK(strstr(machine_halt_reason(), "BRK") != NULL);
    CHECK(strstr(machine_halt_reason(), "pc=$2003") != NULL);
    CHECK(strstr(machine_halt_reason(), "opcode=$00") != NULL);
    CHECK(strstr(machine_halt_reason(), "ramrd=0") != NULL);

    /* Once halted, running does nothing. */
    cycles = machine_cycles();
    CHECK_EQ(machine_run_frame(), MACHINE_HALTED);
    CHECK_EQ(machine_cycles(), cycles);
    CHECK_EQ(cpu_pc(), 0x2003);
}

static void test_halts_on_illegal_opcode(void)
{
    static const uint8_t code[] = { 0x2C, 0x00, 0x00, 0xEA, 0x02 };

    make_image(code, sizeof code);
    machine_init(image, IMAGE_SIZE);
    CHECK_EQ(machine_run_frame(), MACHINE_HALTED);
    CHECK(strstr(machine_halt_reason(), "illegal opcode") != NULL);
    CHECK(strstr(machine_halt_reason(), "pc=$2004") != NULL);
    CHECK(strstr(machine_halt_reason(), "opcode=$02") != NULL);
}

static void test_rejects_bad_images(void)
{
    static const uint8_t code[] = { 0x2C, 0x00, 0x00, 0x4C, 0x03, 0x20 };

    make_image(code, sizeof code);

    machine_init(image, IMAGE_SIZE - 1);
    CHECK_EQ(machine_run_frame(), MACHINE_HALTED);
    CHECK(strstr(machine_halt_reason(), "819200") != NULL);

    machine_init(image, 0);
    CHECK_EQ(machine_run_frame(), MACHINE_HALTED);

    machine_init(NULL, 0);
    CHECK_EQ(machine_run_frame(), MACHINE_HALTED);
    CHECK(strstr(machine_halt_reason(), "no disk image") != NULL);

    image[BOOT_OFFSET] = 0x00;
    machine_init(image, IMAGE_SIZE);
    CHECK_EQ(machine_run_frame(), MACHINE_HALTED);
    CHECK(strstr(machine_halt_reason(), "boot file") != NULL);
    CHECK_EQ(machine_cycles(), 0);
}

static void test_init_recovers_from_halt(void)
{
    static const uint8_t bad[] = { 0x2C, 0x00, 0x00, 0x00 };
    static const uint8_t good[] = { 0x2C, 0x00, 0x00, 0x4C, 0x03, 0x20 };

    make_image(bad, sizeof bad);
    machine_init(image, IMAGE_SIZE);
    CHECK_EQ(machine_run_frame(), MACHINE_HALTED);

    make_image(good, sizeof good);
    machine_init(image, IMAGE_SIZE);
    CHECK_EQ(machine_run_frame(), MACHINE_RUNNING);
    CHECK_EQ(machine_halt_reason()[0], '\0');
}

static void test_smartport_trap_from_program(void)
{
    /* Read block 20 to $4000 through the fake card, then spin:
     *   2000 BIT $0000
     *   2003 JSR $C583 / db $01 / dw $2010
     *   2009 STA $0300        result code
     *   200C JMP $200C
     *   2010 db 3, 1 / dw $4000 / db 20, 0, 0
     */
    static const uint8_t code[] = {
        0x2C, 0x00, 0x00,
        0x20, 0x83, 0xC5, 0x01, 0x10, 0x20,
        0x8D, 0x00, 0x03,
        0x4C, 0x0C, 0x20,
        0x00,
        0x03, 0x01, 0x00, 0x40, 0x14, 0x00, 0x00,
    };

    make_image(code, sizeof code);
    memset(image + 20 * 512, 0x5A, 512);
    bus_init();
    machine_init(image, IMAGE_SIZE);
    bus_main()[0x0300] = 0xFF;

    CHECK_EQ(machine_run_frame(), MACHINE_RUNNING);
    CHECK_EQ(bus_main()[0x0300], 0x00);
    CHECK_EQ(bus_main()[0x4000], 0x5A);
    CHECK_EQ(bus_main()[0x41FF], 0x5A);
    CHECK_EQ(bus_main()[0x4200], 0x00);
}

int main(void)
{
    image = malloc(IMAGE_SIZE);
    if (image == NULL) {
        printf("FAIL out of memory\n");
        return 1;
    }
    test_boot_state();
    test_run_frame_counts_cycles();
    test_vertical_blank_is_visible_to_the_program();
    test_halts_on_brk();
    test_halts_on_illegal_opcode();
    test_rejects_bad_images();
    test_init_recovers_from_halt();
    test_smartport_trap_from_program();
    free(image);
    return TEST_RESULT();
}
