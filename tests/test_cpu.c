/* CPU tests. The functional test binary is passed as argv[1]. */
#include <stdio.h>
#include <string.h>

#include "bus.h"
#include "cpu.h"
#include "softswitch.h"
#include "test.h"

#define KLAUS_START 0x0400
#define KLAUS_SUCCESS 0x3469
#define KLAUS_MAX_STEPS 100000000L

static void test_documented_opcodes(void)
{
    int count = 0;
    int op;

    for (op = 0; op < 256; op++) {
        if (cpu_opcode_is_documented((uint8_t)op)) {
            count++;
        }
    }
    CHECK_EQ(count, 151);
    CHECK(cpu_opcode_is_documented(0xEA)); /* NOP */
    CHECK(cpu_opcode_is_documented(0x00)); /* BRK */
    CHECK(cpu_opcode_is_documented(0xA9)); /* LDA # */
    CHECK(!cpu_opcode_is_documented(0x02));
    CHECK(!cpu_opcode_is_documented(0x1A));
    CHECK(!cpu_opcode_is_documented(0xEB)); /* undocumented SBC alias */
    CHECK(!cpu_opcode_is_documented(0xFF));
}

static void test_registers_round_trip(void)
{
    cpu_regs_t in = { 0x1234, 0x11, 0x22, 0x33, 0x44, 0x01 };
    cpu_regs_t out;

    cpu_set_regs(&in);
    cpu_get_regs(&out);
    CHECK_EQ(out.pc, 0x1234);
    CHECK_EQ(out.a, 0x11);
    CHECK_EQ(out.x, 0x22);
    CHECK_EQ(out.y, 0x33);
    CHECK_EQ(out.sp, 0x44);
    CHECK_EQ(out.status, 0x01 | CPU_FLAG_CONSTANT);
    CHECK_EQ(cpu_pc(), 0x1234);
}

static void test_step_uses_bus_and_counts_cycles(void)
{
    cpu_regs_t r = { 0x0300, 0, 0, 0, 0xFF, CPU_FLAG_CONSTANT };
    cpu_regs_t out;

    bus_init();
    softswitch_reset();
    bus_main()[0x0300] = 0xA9; /* LDA #$42 */
    bus_main()[0x0301] = 0x42;
    bus_main()[0x0302] = 0x8D; /* STA $0310 */
    bus_main()[0x0303] = 0x10;
    bus_main()[0x0304] = 0x03;
    cpu_set_regs(&r);

    CHECK_EQ(cpu_step(), 2);
    CHECK_EQ(cpu_step(), 4);
    cpu_get_regs(&out);
    CHECK_EQ(out.a, 0x42);
    CHECK_EQ(out.pc, 0x0305);
    CHECK_EQ(bus_main()[0x0310], 0x42);
}

static void test_klaus_functional(const char *path)
{
    FILE *f = fopen(path, "rb");
    bus_banking_t ram_everywhere;
    cpu_regs_t r = { KLAUS_START, 0, 0, 0, 0xFF, CPU_FLAG_CONSTANT };
    uint16_t last_pc = 0xFFFF;
    long steps = 0;

    if (f == NULL) {
        printf("FAIL cannot open %s (run tests/fetch_klaus.sh)\n", path);
        test_failures++;
        return;
    }
    bus_init();
    if (fread(bus_main(), 1, 0x10000, f) != 0x10000) {
        printf("FAIL %s is not 65536 bytes\n", path);
        test_failures++;
        fclose(f);
        return;
    }
    fclose(f);

    /* The test binary carries its own vectors at $FFFA, so the language
       card must read and write RAM. */
    memset(&ram_everywhere, 0, sizeof ram_everywhere);
    ram_everywhere.lc_ram_read = true;
    ram_everywhere.lc_write = true;
    ram_everywhere.lc_bank2 = true;
    bus_remap(&ram_everywhere);

    cpu_set_regs(&r);
    /* Every failure and the final success are "jump to self" traps. */
    while (cpu_pc() != last_pc && steps < KLAUS_MAX_STEPS) {
        last_pc = cpu_pc();
        cpu_step();
        steps++;
    }
    CHECK_EQ(cpu_pc(), KLAUS_SUCCESS);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: test_cpu <6502_functional_test.bin>\n");
        return 2;
    }
    test_documented_opcodes();
    test_registers_round_trip();
    test_step_uses_bus_and_counts_cycles();
    test_klaus_functional(argv[1]);
    return TEST_RESULT();
}
