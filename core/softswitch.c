#include "softswitch.h"

#include <string.h>

#include "bus.h"

static softswitch_state_t s;
static uint32_t frame_cycle;
static bus_banking_t applied;

static void remap(void)
{
    bus_banking_t b;

    memset(&b, 0, sizeof b);
    b.store80 = s.store80;
    b.ramrd = s.ramrd;
    b.ramwrt = s.ramwrt;
    b.altzp = s.altzp;
    b.page2 = s.page2;
    b.hires = s.hires;
    b.lc_ram_read = s.lc_ram_read;
    b.lc_write = s.lc_write;
    b.lc_bank2 = s.lc_bank2;

    if (memcmp(&b, &applied, sizeof b) != 0) {
        applied = b;
        bus_remap(&b);
    }
}

void softswitch_reset(void)
{
    memset(&s, 0, sizeof s);
    s.text = true;
    s.lc_bank2 = true;
    frame_cycle = 0;

    /* Force the first remap regardless of what was applied before. */
    memset(&applied, 0xFF, sizeof applied);
    remap();
}

static bool is_language_card(uint8_t reg)
{
    return reg >= 0x80 && reg <= 0x8F;
}

static bool is_video(uint8_t reg)
{
    return (reg >= 0x50 && reg <= 0x57) || reg == 0x5E || reg == 0x5F;
}

static void language_card(uint8_t reg, bool is_read)
{
    uint8_t mode = reg & 0x03;

    s.lc_bank2 = (reg & 0x08) == 0;
    s.lc_ram_read = (mode == 0x00) || (mode == 0x03);

    if (reg & 0x01) {
        /* Odd address: writes are enabled by the second consecutive read. */
        if (is_read) {
            if (s.lc_prewrite) {
                s.lc_write = true;
            }
            s.lc_prewrite = true;
        } else {
            s.lc_prewrite = false;
        }
    } else {
        s.lc_prewrite = false;
        s.lc_write = false;
    }
    remap();
}

static void video(uint8_t reg)
{
    switch (reg) {
    case 0x50: s.text = false; break;
    case 0x51: s.text = true; break;
    case 0x52: s.mixed = false; break;
    case 0x53: s.mixed = true; break;
    case 0x54: s.page2 = false; break;
    case 0x55: s.page2 = true; break;
    case 0x56: s.hires = false; break;
    case 0x57: s.hires = true; break;
    case 0x5E: s.dhires = true; break;
    case 0x5F: s.dhires = false; break;
    default: break;
    }
    remap();
}

static uint8_t flag(bool on)
{
    return on ? 0x80 : 0x00;
}

uint8_t softswitch_read(uint8_t reg)
{
    if (is_language_card(reg)) {
        language_card(reg, true);
        return 0x00;
    }
    if (is_video(reg)) {
        video(reg);
        return 0x00;
    }
    switch (reg) {
    case 0x11: return flag(s.lc_bank2);
    case 0x12: return flag(s.lc_ram_read);
    case 0x13: return flag(s.ramrd);
    case 0x14: return flag(s.ramwrt);
    case 0x15: return 0x00; /* INTCXROM */
    case 0x16: return flag(s.altzp);
    case 0x17: return 0x00; /* SLOTC3ROM */
    case 0x18: return flag(s.store80);
    case 0x19: return flag(frame_cycle < SOFTSWITCH_VBL_START_CYCLE);
    case 0x1A: return flag(s.text);
    case 0x1B: return flag(s.mixed);
    case 0x1C: return flag(s.page2);
    case 0x1D: return flag(s.hires);
    case 0x1E: return flag(s.altchar);
    case 0x1F: return flag(s.col80);
    default: return 0x00; /* keyboard, buttons, paddles, speaker: idle */
    }
}

void softswitch_write(uint8_t reg, uint8_t value)
{
    (void)value;

    if (is_language_card(reg)) {
        language_card(reg, false);
        return;
    }
    if (is_video(reg)) {
        video(reg);
        return;
    }
    switch (reg) {
    case 0x00: s.store80 = false; break;
    case 0x01: s.store80 = true; break;
    case 0x02: s.ramrd = false; break;
    case 0x03: s.ramrd = true; break;
    case 0x04: s.ramwrt = false; break;
    case 0x05: s.ramwrt = true; break;
    case 0x08: s.altzp = false; break;
    case 0x09: s.altzp = true; break;
    case 0x0C: s.col80 = false; break;
    case 0x0D: s.col80 = true; break;
    case 0x0E: s.altchar = false; break;
    case 0x0F: s.altchar = true; break;
    default: return;
    }
    remap();
}

void softswitch_set_frame_cycle(uint32_t cycle)
{
    frame_cycle = cycle;
}

const softswitch_state_t *softswitch_state(void)
{
    return &s;
}
