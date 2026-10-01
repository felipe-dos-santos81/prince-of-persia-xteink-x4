#include "rom.h"

#include <string.h>

#define ROM_BASE 0xC000

void rom_fill(uint8_t *rom)
{
    /* Every ROM entry point is an RTS, so any ROM call returns at once. */
    memset(rom, 0x60, 0x4000);

    /* Machine ID: Apple IIe family. $FBC0 stays $60 (non-zero = not a IIc). */
    rom[0xFBB3 - ROM_BASE] = 0x06;

    /* Slot 5: SmartPort signature bytes and entry offset.
       ProDOS entry = $C500 + $80; SmartPort entry = that + 3 = $C583. */
    rom[0xC501 - ROM_BASE] = 0x20;
    rom[0xC503 - ROM_BASE] = 0x00;
    rom[0xC505 - ROM_BASE] = 0x03;
    rom[0xC507 - ROM_BASE] = 0x00;
    rom[0xC5FF - ROM_BASE] = 0x80;
}
