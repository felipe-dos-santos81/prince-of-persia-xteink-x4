#ifndef ROM_H
#define ROM_H

#include <stdint.h>

/* Address the game jumps to for SmartPort calls on the fake slot-5 card. */
#define ROM_SMARTPORT_ENTRY 0xC583

/* Fill a 16 KB buffer (index 0 = address $C000) with the stub ROM. */
void rom_fill(uint8_t *rom);

#endif
