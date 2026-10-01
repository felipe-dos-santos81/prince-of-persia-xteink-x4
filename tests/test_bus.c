/* Bus tests. Links only core/bus.c and core/rom.c; the soft switches are
   replaced by the recording fakes below. */
#include <string.h>

#include "bus.h"
#include "rom.h"
#include "test.h"

static uint8_t last_read_reg;
static uint8_t last_write_reg;
static uint8_t last_write_value;
static int read_calls;
static int write_calls;

uint8_t softswitch_read(uint8_t reg)
{
    last_read_reg = reg;
    read_calls++;
    return 0xA5;
}

void softswitch_write(uint8_t reg, uint8_t value)
{
    last_write_reg = reg;
    last_write_value = value;
    write_calls++;
}

static bus_banking_t banking(void)
{
    bus_banking_t b;

    memset(&b, 0, sizeof b);
    b.lc_bank2 = true;
    return b;
}

static void test_rom_contents(void)
{
    uint8_t rom[0x4000];

    rom_fill(rom);
    CHECK_EQ(rom[0xFBB3 - 0xC000], 0x06);
    CHECK_EQ(rom[0xFBC0 - 0xC000], 0x60);
    CHECK_EQ(rom[0xFE1F - 0xC000], 0x60);
    CHECK_EQ(rom[0xFB2F - 0xC000], 0x60);
    CHECK_EQ(rom[0xC501 - 0xC000], 0x20);
    CHECK_EQ(rom[0xC503 - 0xC000], 0x00);
    CHECK_EQ(rom[0xC505 - 0xC000], 0x03);
    CHECK_EQ(rom[0xC507 - 0xC000], 0x00);
    CHECK_EQ(rom[0xC5FF - 0xC000], 0x80);
    CHECK_EQ(0xC500 + rom[0xC5FF - 0xC000] + 3, ROM_SMARTPORT_ENTRY);
}

static void test_power_on(void)
{
    bus_init();
    bus_write(0x0300, 0x11);
    CHECK_EQ(bus_main()[0x0300], 0x11);
    CHECK_EQ(bus_aux()[0x0300], 0x00);
    CHECK_EQ(bus_read(0x0300), 0x11);

    /* $D000-$FFFF reads stub ROM and ignores writes. */
    CHECK_EQ(bus_read(0xFBB3), 0x06);
    CHECK_EQ(bus_read(0xE000), 0x60);
    bus_write(0xE000, 0x99);
    CHECK_EQ(bus_read(0xE000), 0x60);
    CHECK_EQ(bus_main()[0xE000], 0x00);

    /* Slot ROM area reads stub ROM and ignores writes. */
    CHECK_EQ(bus_read(0xC5FF), 0x80);
    bus_write(0xC5FF, 0x12);
    CHECK_EQ(bus_read(0xC5FF), 0x80);
}

static void test_init_clears_memory(void)
{
    bus_init();
    bus_main()[0x1234] = 0x55;
    bus_aux()[0x1234] = 0x66;
    bus_init();
    CHECK_EQ(bus_main()[0x1234], 0x00);
    CHECK_EQ(bus_aux()[0x1234], 0x00);
}

static void test_io_page_routes_to_softswitch(void)
{
    bus_init();
    read_calls = 0;
    write_calls = 0;

    CHECK_EQ(bus_read(0xC019), 0xA5);
    CHECK_EQ(read_calls, 1);
    CHECK_EQ(last_read_reg, 0x19);

    bus_write(0xC005, 0x42);
    CHECK_EQ(write_calls, 1);
    CHECK_EQ(last_write_reg, 0x05);
    CHECK_EQ(last_write_value, 0x42);

    /* Neighbouring pages do not reach the soft switches. */
    (void)bus_read(0xBFFF);
    (void)bus_read(0xC100);
    CHECK_EQ(read_calls, 1);
}

static void test_ramrd_ramwrt(void)
{
    bus_banking_t b = banking();

    bus_init();
    bus_main()[0x0300] = 0x11;
    bus_aux()[0x0300] = 0x22;

    b.ramrd = true;
    bus_remap(&b);
    CHECK_EQ(bus_read(0x0300), 0x22);
    bus_write(0x0300, 0x33); /* RAMWRT still main */
    CHECK_EQ(bus_main()[0x0300], 0x33);
    CHECK_EQ(bus_aux()[0x0300], 0x22);

    b.ramrd = false;
    b.ramwrt = true;
    bus_remap(&b);
    CHECK_EQ(bus_read(0x0300), 0x33);
    bus_write(0x0300, 0x44);
    CHECK_EQ(bus_aux()[0x0300], 0x44);
    CHECK_EQ(bus_main()[0x0300], 0x33);

    /* The range is $0200-$BFFF: zero page and stack are not affected. */
    bus_write(0x0080, 0x55);
    bus_write(0x01FF, 0x66);
    bus_write(0xBFFF, 0x77);
    CHECK_EQ(bus_main()[0x0080], 0x55);
    CHECK_EQ(bus_main()[0x01FF], 0x66);
    CHECK_EQ(bus_aux()[0xBFFF], 0x77);
    bus_write(0x0200, 0x88);
    CHECK_EQ(bus_aux()[0x0200], 0x88);
}

static void test_altzp(void)
{
    bus_banking_t b = banking();

    bus_init();
    b.altzp = true;
    bus_remap(&b);
    bus_write(0x0080, 0x11);
    bus_write(0x0180, 0x22);
    bus_write(0x0200, 0x33);
    CHECK_EQ(bus_aux()[0x0080], 0x11);
    CHECK_EQ(bus_aux()[0x0180], 0x22);
    CHECK_EQ(bus_main()[0x0080], 0x00);
    CHECK_EQ(bus_main()[0x0200], 0x33); /* not zero page: follows RAMWRT */
    CHECK_EQ(bus_read(0x0080), 0x11);
}

static void test_store80(void)
{
    bus_banking_t b = banking();

    bus_init();

    /* 80STORE + PAGE2: text page goes to aux, hi-res page does not. */
    b.store80 = true;
    b.page2 = true;
    bus_remap(&b);
    bus_write(0x0400, 0x11);
    bus_write(0x07FF, 0x12);
    bus_write(0x0800, 0x13);
    bus_write(0x2000, 0x14);
    CHECK_EQ(bus_aux()[0x0400], 0x11);
    CHECK_EQ(bus_aux()[0x07FF], 0x12);
    CHECK_EQ(bus_main()[0x0800], 0x13);
    CHECK_EQ(bus_main()[0x2000], 0x14);
    CHECK_EQ(bus_read(0x0400), 0x11);

    /* With HIRES too, $2000-$3FFF follows PAGE2. */
    b.hires = true;
    bus_remap(&b);
    bus_write(0x2000, 0x21);
    bus_write(0x3FFF, 0x22);
    bus_write(0x4000, 0x23);
    CHECK_EQ(bus_aux()[0x2000], 0x21);
    CHECK_EQ(bus_aux()[0x3FFF], 0x22);
    CHECK_EQ(bus_main()[0x4000], 0x23);

    /* 80STORE overrides RAMRD/RAMWRT for those ranges. */
    b.page2 = false;
    b.ramrd = true;
    b.ramwrt = true;
    bus_remap(&b);
    bus_write(0x0400, 0x31);
    bus_write(0x2000, 0x32);
    bus_write(0x0800, 0x33);
    CHECK_EQ(bus_main()[0x0400], 0x31);
    CHECK_EQ(bus_main()[0x2000], 0x32);
    CHECK_EQ(bus_aux()[0x0800], 0x33);
    CHECK_EQ(bus_read(0x0400), 0x31);

    /* Without 80STORE, PAGE2 has no effect on memory. */
    b = banking();
    b.page2 = true;
    b.hires = true;
    bus_remap(&b);
    bus_write(0x0400, 0x41);
    CHECK_EQ(bus_main()[0x0400], 0x41);
}

static void test_language_card(void)
{
    bus_banking_t b = banking();

    bus_init();

    /* Read RAM + write enabled, bank 2: array offsets equal addresses. */
    b.lc_ram_read = true;
    b.lc_write = true;
    bus_remap(&b);
    bus_write(0xD000, 0x11);
    bus_write(0xE000, 0x12);
    bus_write(0xFFFF, 0x13);
    CHECK_EQ(bus_main()[0xD000], 0x11);
    CHECK_EQ(bus_main()[0xE000], 0x12);
    CHECK_EQ(bus_main()[0xFFFF], 0x13);
    CHECK_EQ(bus_read(0xD000), 0x11);

    /* Bank 1: $D000-$DFFF is stored at $C000-$CFFF of the array. */
    b.lc_bank2 = false;
    bus_remap(&b);
    bus_write(0xD000, 0x21);
    bus_write(0xDFFF, 0x22);
    CHECK_EQ(bus_main()[0xC000], 0x21);
    CHECK_EQ(bus_main()[0xCFFF], 0x22);
    CHECK_EQ(bus_main()[0xD000], 0x11); /* bank 2 untouched */
    CHECK_EQ(bus_read(0xD000), 0x21);
    CHECK_EQ(bus_read(0xE000), 0x12); /* $E000+ is shared by both banks */

    /* Read ROM while writing RAM. */
    b.lc_bank2 = true;
    b.lc_ram_read = false;
    bus_remap(&b);
    bus_write(0xD000, 0x31);
    CHECK_EQ(bus_main()[0xD000], 0x31);
    CHECK_EQ(bus_read(0xD000), 0x60);

    /* Read RAM, write-protected. */
    b.lc_ram_read = true;
    b.lc_write = false;
    bus_remap(&b);
    bus_write(0xD000, 0x41);
    CHECK_EQ(bus_read(0xD000), 0x31);

    /* ALTZP moves the whole language card to aux. */
    b.lc_write = true;
    b.altzp = true;
    bus_remap(&b);
    bus_write(0xD000, 0x51);
    bus_write(0xF000, 0x52);
    CHECK_EQ(bus_aux()[0xD000], 0x51);
    CHECK_EQ(bus_aux()[0xF000], 0x52);
    CHECK_EQ(bus_main()[0xD000], 0x31);
}

int main(void)
{
    test_rom_contents();
    test_power_on();
    test_init_clears_memory();
    test_io_page_routes_to_softswitch();
    test_ramrd_ramwrt();
    test_altzp();
    test_store80();
    test_language_card();
    return TEST_RESULT();
}
