#ifndef CPU_H
#define CPU_H

#include <stdbool.h>
#include <stdint.h>

#define CPU_FLAG_CARRY 0x01
#define CPU_FLAG_INTERRUPT 0x04
#define CPU_FLAG_CONSTANT 0x20

typedef struct {
    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp;
    uint8_t status;
} cpu_regs_t;

void cpu_get_regs(cpu_regs_t *out);
void cpu_set_regs(const cpu_regs_t *in);
uint16_t cpu_pc(void);

/* True for the 151 documented NMOS 6502 opcodes. */
bool cpu_opcode_is_documented(uint8_t opcode);

/* Execute one instruction through the bus. Returns the cycles it took. */
uint32_t cpu_step(void);

#endif
