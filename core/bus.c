#include "bus.h"

#include <stddef.h>
#include <string.h>

#include "rom.h"
#include "softswitch.h"

static uint8_t main_mem[0x10000];
static uint8_t aux_mem[0x10000];
static uint8_t rom_mem[0x4000];

/* One entry per 256-byte page. A NULL write entry means writes are ignored.
   Page $C0 is never looked up: it is routed to the soft switches. */
static uint8_t *read_page[256];
static uint8_t *write_page[256];

void bus_remap(const bus_banking_t *b)
{
    uint8_t *zp = b->altzp ? aux_mem : main_mem;
    uint8_t *rd = b->ramrd ? aux_mem : main_mem;
    uint8_t *wr = b->ramwrt ? aux_mem : main_mem;
    int page;

    for (page = 0x00; page <= 0x01; page++) {
        read_page[page] = zp + page * 256;
        write_page[page] = zp + page * 256;
    }
    for (page = 0x02; page <= 0xBF; page++) {
        read_page[page] = rd + page * 256;
        write_page[page] = wr + page * 256;
    }
    if (b->store80) {
        uint8_t *sel = b->page2 ? aux_mem : main_mem;
        for (page = 0x04; page <= 0x07; page++) {
            read_page[page] = sel + page * 256;
            write_page[page] = sel + page * 256;
        }
        if (b->hires) {
            for (page = 0x20; page <= 0x3F; page++) {
                read_page[page] = sel + page * 256;
                write_page[page] = sel + page * 256;
            }
        }
    }
    read_page[0xC0] = NULL;
    write_page[0xC0] = NULL;
    for (page = 0xC1; page <= 0xCF; page++) {
        read_page[page] = rom_mem + (page - 0xC0) * 256;
        write_page[page] = NULL;
    }
    for (page = 0xD0; page <= 0xFF; page++) {
        uint8_t *ram;
        if (page <= 0xDF && !b->lc_bank2) {
            ram = zp + (page - 0x10) * 256; /* bank 1 is stored at $C000 */
        } else {
            ram = zp + page * 256;
        }
        read_page[page] = b->lc_ram_read ? ram : rom_mem + (page - 0xC0) * 256;
        write_page[page] = b->lc_write ? ram : NULL;
    }
}

void bus_init(void)
{
    bus_banking_t power_on;

    memset(main_mem, 0, sizeof main_mem);
    memset(aux_mem, 0, sizeof aux_mem);
    rom_fill(rom_mem);

    memset(&power_on, 0, sizeof power_on);
    power_on.lc_bank2 = true;
    bus_remap(&power_on);
}

uint8_t bus_read(uint16_t addr)
{
    uint8_t page = (uint8_t)(addr >> 8);

    if (page == 0xC0) {
        return softswitch_read((uint8_t)(addr & 0xFF));
    }
    return read_page[page][addr & 0xFF];
}

void bus_write(uint16_t addr, uint8_t value)
{
    uint8_t page = (uint8_t)(addr >> 8);
    uint8_t *target;

    if (page == 0xC0) {
        softswitch_write((uint8_t)(addr & 0xFF), value);
        return;
    }
    target = write_page[page];
    if (target != NULL) {
        target[addr & 0xFF] = value;
    }
}

uint8_t *bus_main(void) { return main_mem; }
uint8_t *bus_aux(void) { return aux_mem; }
uint8_t *bus_rom(void) { return rom_mem; }
