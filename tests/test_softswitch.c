#include "bus.h"
#include "softswitch.h"
#include "test.h"

static void power_on(void)
{
    bus_init();
    softswitch_reset();
}

static void test_reset_state(void)
{
    const softswitch_state_t *s;

    power_on();
    s = softswitch_state();
    CHECK(s->text);
    CHECK(s->lc_bank2);
    CHECK(!s->lc_ram_read);
    CHECK(!s->lc_write);
    CHECK(!s->hires);
    CHECK(!s->ramrd);
    CHECK_EQ(bus_read(0xC01A), 0x80); /* TEXT */
    CHECK_EQ(bus_read(0xC011), 0x80); /* LC bank 2 */
    CHECK_EQ(bus_read(0xC012), 0x00); /* LC reading ROM */
    CHECK_EQ(bus_read(0xC015), 0x00);
    CHECK_EQ(bus_read(0xC017), 0x00);
}

/* Each memory switch: write the "on" address, check the status flag and the
   state, write the "off" address, check again. */
static void check_memory_switch(uint16_t off_addr, uint16_t status_addr)
{
    bus_write((uint16_t)(off_addr + 1), 0);
    CHECK_EQ(bus_read(status_addr), 0x80);
    bus_write(off_addr, 0);
    CHECK_EQ(bus_read(status_addr), 0x00);
}

static void test_memory_switches(void)
{
    power_on();
    check_memory_switch(0xC000, 0xC018); /* 80STORE */
    check_memory_switch(0xC002, 0xC013); /* RAMRD */
    check_memory_switch(0xC004, 0xC014); /* RAMWRT */
    check_memory_switch(0xC008, 0xC016); /* ALTZP */
    check_memory_switch(0xC00C, 0xC01F); /* 80COL */
    check_memory_switch(0xC00E, 0xC01E); /* ALTCHARSET */

    /* Reading $C000-$C00F must not change anything. */
    (void)bus_read(0xC003);
    (void)bus_read(0xC005);
    (void)bus_read(0xC009);
    CHECK_EQ(bus_read(0xC013), 0x00);
    CHECK_EQ(bus_read(0xC014), 0x00);
    CHECK_EQ(bus_read(0xC016), 0x00);
}

static void test_memory_switches_move_memory(void)
{
    power_on();
    bus_write(0xC005, 0); /* RAMWRT aux */
    bus_write(0x0300, 0x11);
    CHECK_EQ(bus_aux()[0x0300], 0x11);
    CHECK_EQ(bus_read(0x0300), 0x00); /* RAMRD still main */
    bus_write(0xC003, 0); /* RAMRD aux */
    CHECK_EQ(bus_read(0x0300), 0x11);
    bus_write(0xC004, 0);
    bus_write(0xC002, 0);

    bus_write(0xC009, 0); /* ALTZP */
    bus_write(0x0080, 0x22);
    CHECK_EQ(bus_aux()[0x0080], 0x22);
    bus_write(0xC008, 0);

    /* 80STORE + HIRES + PAGE2 sends $2000 to aux. */
    bus_write(0xC001, 0);
    (void)bus_read(0xC057);
    (void)bus_read(0xC055);
    bus_write(0x2000, 0x33);
    CHECK_EQ(bus_aux()[0x2000], 0x33);
    (void)bus_read(0xC054);
    bus_write(0x2000, 0x44);
    CHECK_EQ(bus_main()[0x2000], 0x44);
}

static void test_video_switches(void)
{
    const softswitch_state_t *s;

    power_on();
    s = softswitch_state();

    (void)bus_read(0xC050);
    CHECK_EQ(bus_read(0xC01A), 0x00);
    (void)bus_read(0xC051);
    CHECK_EQ(bus_read(0xC01A), 0x80);

    (void)bus_read(0xC053);
    CHECK_EQ(bus_read(0xC01B), 0x80);
    (void)bus_read(0xC052);
    CHECK_EQ(bus_read(0xC01B), 0x00);

    (void)bus_read(0xC055);
    CHECK_EQ(bus_read(0xC01C), 0x80);
    (void)bus_read(0xC054);
    CHECK_EQ(bus_read(0xC01C), 0x00);

    (void)bus_read(0xC057);
    CHECK_EQ(bus_read(0xC01D), 0x80);
    (void)bus_read(0xC056);
    CHECK_EQ(bus_read(0xC01D), 0x00);

    (void)bus_read(0xC05E);
    CHECK(s->dhires);
    (void)bus_read(0xC05F);
    CHECK(!s->dhires);

    /* Writes trigger the video switches too. */
    bus_write(0xC057, 0);
    CHECK(s->hires);
    bus_write(0xC050, 0);
    CHECK(!s->text);
    bus_write(0xC05E, 0);
    CHECK(s->dhires);
}

static void test_language_card_write_enable(void)
{
    power_on();

    /* One read of $C083: RAM is readable but not yet writable. */
    (void)bus_read(0xC083);
    CHECK_EQ(bus_read(0xC012), 0x80);
    bus_write(0xD000, 0x55);
    CHECK_EQ(bus_main()[0xD000], 0x00);

    /* Second consecutive read enables writing. */
    (void)bus_read(0xC083);
    bus_write(0xD000, 0x55);
    CHECK_EQ(bus_main()[0xD000], 0x55);
    CHECK_EQ(bus_read(0xD000), 0x55);

    /* An even address write-protects again. */
    (void)bus_read(0xC080);
    bus_write(0xD000, 0x66);
    CHECK_EQ(bus_read(0xD000), 0x55);

    /* Two writes to an odd address do not enable writing. */
    power_on();
    bus_write(0xC083, 0);
    bus_write(0xC083, 0);
    bus_write(0xD000, 0x77);
    CHECK_EQ(bus_main()[0xD000], 0x00);

    /* A write between two reads resets the count. */
    power_on();
    (void)bus_read(0xC083);
    bus_write(0xC083, 0);
    (void)bus_read(0xC083);
    bus_write(0xD000, 0x78);
    CHECK_EQ(bus_main()[0xD000], 0x00);

    /* An even access between two odd reads resets the count. */
    power_on();
    (void)bus_read(0xC083);
    (void)bus_read(0xC080);
    (void)bus_read(0xC083);
    bus_write(0xD000, 0x79);
    CHECK_EQ(bus_main()[0xD000], 0x00);
}

static void test_language_card_modes(void)
{
    /* $C081 twice: read ROM, write RAM, bank 2. */
    power_on();
    (void)bus_read(0xC081);
    (void)bus_read(0xC081);
    bus_write(0xD000, 0x11);
    CHECK_EQ(bus_main()[0xD000], 0x11);
    CHECK_EQ(bus_read(0xD000), 0x60);
    CHECK_EQ(bus_read(0xC012), 0x00);
    CHECK_EQ(bus_read(0xC011), 0x80);

    /* $C08B twice: read and write RAM, bank 1. */
    (void)bus_read(0xC08B);
    (void)bus_read(0xC08B);
    CHECK_EQ(bus_read(0xC011), 0x00);
    bus_write(0xD000, 0x22);
    CHECK_EQ(bus_main()[0xC000], 0x22);
    CHECK_EQ(bus_main()[0xD000], 0x11);
    CHECK_EQ(bus_read(0xD000), 0x22);

    /* $C082: read ROM, write-protected. */
    (void)bus_read(0xC082);
    CHECK_EQ(bus_read(0xD000), 0x60);
    bus_write(0xD000, 0x33);
    CHECK_EQ(bus_main()[0xD000], 0x11);

    /* The boot code's single STA $C081: read ROM, bank 2, still protected. */
    power_on();
    bus_write(0xC081, 0xFF);
    CHECK_EQ(bus_read(0xFBB3), 0x06);
    bus_write(0xD000, 0x44);
    CHECK_EQ(bus_main()[0xD000], 0x00);
}

static void test_vertical_blank(void)
{
    power_on();
    softswitch_set_frame_cycle(0);
    CHECK_EQ(bus_read(0xC019), 0x80);
    softswitch_set_frame_cycle(12479);
    CHECK_EQ(bus_read(0xC019), 0x80);
    softswitch_set_frame_cycle(12480);
    CHECK_EQ(bus_read(0xC019), 0x00);
    softswitch_set_frame_cycle(17029);
    CHECK_EQ(bus_read(0xC019), 0x00);
}

static void test_idle_inputs(void)
{
    power_on();
    CHECK_EQ(bus_read(0xC000), 0x00); /* keyboard: no key */
    CHECK_EQ(bus_read(0xC010), 0x00);
    CHECK_EQ(bus_read(0xC030), 0x00); /* speaker */
    CHECK_EQ(bus_read(0xC061), 0x00); /* buttons */
    CHECK_EQ(bus_read(0xC062), 0x00);
    CHECK_EQ(bus_read(0xC064), 0x00); /* paddles */
    CHECK_EQ(bus_read(0xC065), 0x00);
    CHECK_EQ(bus_read(0xC070), 0x00);
    CHECK_EQ(bus_read(0xC036), 0x00); /* IIGS-only register */
    bus_write(0xC030, 0xFF);
    bus_write(0xC036, 0xFF);
    bus_write(0xC07E, 0xFF);
    CHECK(softswitch_state()->text); /* nothing changed */
}

static void test_reset_restores_power_on(void)
{
    power_on();
    bus_write(0xC005, 0);
    bus_write(0xC009, 0);
    (void)bus_read(0xC083);
    (void)bus_read(0xC083);
    (void)bus_read(0xC050);
    softswitch_set_frame_cycle(15000);

    softswitch_reset();
    CHECK_EQ(bus_read(0xC014), 0x00);
    CHECK_EQ(bus_read(0xC016), 0x00);
    CHECK_EQ(bus_read(0xC012), 0x00);
    CHECK_EQ(bus_read(0xC01A), 0x80);
    CHECK_EQ(bus_read(0xC019), 0x80);
    bus_write(0x0300, 0x12);
    CHECK_EQ(bus_main()[0x0300], 0x12);
    CHECK_EQ(bus_read(0xE000), 0x60);
}

int main(void)
{
    test_reset_state();
    test_memory_switches();
    test_memory_switches_move_memory();
    test_video_switches();
    test_language_card_write_enable();
    test_language_card_modes();
    test_vertical_blank();
    test_idle_inputs();
    test_reset_restores_power_on();
    return TEST_RESULT();
}
