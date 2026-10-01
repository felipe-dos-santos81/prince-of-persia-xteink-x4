#include "machine.h"

#include <stdio.h>

#include "blockdev.h"
#include "boot.h"
#include "bus.h"
#include "cpu.h"
#include "rom.h"
#include "softswitch.h"
#include "video.h"

static machine_status_t status;
static char halt_reason[160];
static uint32_t frame_cycle;
static uint64_t total_cycles;

static void halt(const char *what, uint16_t pc, uint8_t opcode)
{
    const softswitch_state_t *s = softswitch_state();

    status = MACHINE_HALTED;
    snprintf(halt_reason, sizeof halt_reason,
             "%s: pc=$%04X opcode=$%02X ramrd=%d ramwrt=%d altzp=%d store80=%d "
             "lcram=%d lcwrite=%d lcbank2=%d",
             what, pc, opcode, s->ramrd, s->ramwrt, s->altzp, s->store80,
             s->lc_ram_read, s->lc_write, s->lc_bank2);
}

void machine_init(const uint8_t *image, size_t image_len)
{
    const char *error;

    status = MACHINE_RUNNING;
    halt_reason[0] = '\0';
    frame_cycle = 0;
    total_cycles = 0;

    bus_init();
    softswitch_reset();
    blockdev_init(image, image_len);

    error = boot_load(image, image_len);
    if (error != NULL) {
        status = MACHINE_HALTED;
        snprintf(halt_reason, sizeof halt_reason, "bad image: %s", error);
    }
}

machine_status_t machine_run_frame(void)
{
    if (status == MACHINE_HALTED) {
        return status;
    }

    while (frame_cycle < MACHINE_CYCLES_PER_FRAME) {
        uint16_t pc = cpu_pc();
        uint32_t used;

        softswitch_set_frame_cycle(frame_cycle);

        if (pc == ROM_SMARTPORT_ENTRY) {
            blockdev_call();
            used = BLOCKDEV_CALL_CYCLES;
        } else {
            uint8_t opcode = bus_read(pc);

            if (opcode == 0x00) {
                halt("BRK", pc, opcode);
                return status;
            }
            if (!cpu_opcode_is_documented(opcode)) {
                halt("illegal opcode", pc, opcode);
                return status;
            }
            used = cpu_step();
        }
        frame_cycle += used;
        total_cycles += used;
    }

    /* Carry the overshoot into the next frame. */
    frame_cycle -= MACHINE_CYCLES_PER_FRAME;
    return status;
}

void machine_render(uint8_t *out)
{
    video_render(out);
}

const char *machine_halt_reason(void)
{
    return halt_reason;
}

uint64_t machine_cycles(void)
{
    return total_cycles;
}

const char *machine_video_mode_name(void)
{
    switch (video_mode()) {
    case VIDEO_MODE_HIRES: return "hires";
    case VIDEO_MODE_DHIRES: return "dhires";
    default: return "blank";
    }
}
