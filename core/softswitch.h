#ifndef SOFTSWITCH_H
#define SOFTSWITCH_H

#include <stdbool.h>
#include <stdint.h>

#define SOFTSWITCH_CYCLES_PER_FRAME 17030u
#define SOFTSWITCH_VBL_START_CYCLE 12480u

typedef struct {
    bool store80;
    bool ramrd;
    bool ramwrt;
    bool altzp;
    bool col80;
    bool altchar;
    bool text;
    bool mixed;
    bool page2;
    bool hires;
    bool dhires;
    bool lc_ram_read;
    bool lc_write;
    bool lc_bank2;
    bool lc_prewrite;
} softswitch_state_t;

/* Power-on state: everything off, text mode, language card reading ROM with
   bank 2 selected and writes disabled. Also remaps the bus. */
void softswitch_reset(void);

/* reg is the low byte of a $C0xx address. */
uint8_t softswitch_read(uint8_t reg);
void softswitch_write(uint8_t reg, uint8_t value);

/* Position within the current video frame, 0..17029. Drives $C019. */
void softswitch_set_frame_cycle(uint32_t cycle);

const softswitch_state_t *softswitch_state(void);

#endif
