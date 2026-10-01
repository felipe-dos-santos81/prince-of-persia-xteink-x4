#include "cpu.h"

#include "bus.h"

/* fake6502 expects these two functions. Defining them static before the
   include lets the compiler inline bus access into the CPU core. */
static inline uint8_t read6502(uint16_t address)
{
    return bus_read(address);
}

static inline void write6502(uint16_t address, uint8_t value)
{
    bus_write(address, value);
}

/* Vendored and patched by tools/vendor_fake6502.py. Included, not compiled
   separately, so its registers stay private to this file. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "fake6502.inc"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

void cpu_get_regs(cpu_regs_t *out)
{
    out->pc = pc;
    out->a = a;
    out->x = x;
    out->y = y;
    out->sp = sp;
    out->status = status;
}

void cpu_set_regs(const cpu_regs_t *in)
{
    pc = in->pc;
    a = in->a;
    x = in->x;
    y = in->y;
    sp = in->sp;
    status = in->status | CPU_FLAG_CONSTANT;
}

uint16_t cpu_pc(void)
{
    return pc;
}

bool cpu_opcode_is_documented(uint8_t op)
{
    /* With UNDOCUMENTED off, fake6502 maps undocumented opcodes to its nop
       handler, except $EB which it treats as a second SBC. $EA is the one
       real NOP. */
    if (op == 0xEA) {
        return true;
    }
    return op != 0xEB && optable[op] != nop;
}

uint32_t cpu_step(void)
{
    uint32_t before = clockticks6502;

    step6502();
    return clockticks6502 - before;
}
