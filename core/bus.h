#ifndef BUS_H
#define BUS_H

#include <stdbool.h>
#include <stdint.h>

/* Banking flags that decide which physical memory an address maps to. */
typedef struct {
    bool store80;
    bool ramrd;
    bool ramwrt;
    bool altzp;
    bool page2;
    bool hires;
    bool lc_ram_read;
    bool lc_write;
    bool lc_bank2;
} bus_banking_t;

/* Zero main and aux memory, fill the stub ROM, apply power-on banking. */
void bus_init(void);

/* Rebuild the page tables for the given banking flags. */
void bus_remap(const bus_banking_t *banking);

uint8_t bus_read(uint16_t addr);
void bus_write(uint16_t addr, uint8_t value);

/* Raw 64 KB arrays, indexed by address. Language-card bank 2 lives at
   $D000-$DFFF and bank 1 at $C000-$CFFF of each array. */
uint8_t *bus_main(void);
uint8_t *bus_aux(void);

/* Raw 16 KB stub ROM; index 0 is address $C000. */
uint8_t *bus_rom(void);

#endif
