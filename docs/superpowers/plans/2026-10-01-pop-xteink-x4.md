# Prince of Persia on the Xteink X4 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Xteink X4 boots Prince of Persia, built from Jordan Mechner's Apple II source, and plays the title sequence and attract-mode demo on its e-ink panel with no input.

**Architecture:** A small portable C99 Apple IIe emulator core (`core/`) that emulates only what the game needs: a 6502, 128K banked memory, the soft switches the game touches, a stub ROM, a trap that serves disk blocks from an image, and a hi-res renderer. Two thin shells wrap it: host programs on the Mac for tests and viewing (`host/`), and PlatformIO firmware for the X4 that paces the emulator and pushes pictures to the panel (`firmware/`). The game image is built from source in Docker (`game/`).

**Tech Stack:** C99 (core, host, tests), GNU make, SDL2, Docker (`linux/386`), Python 3 (one vendoring script), PlatformIO with the Arduino framework for ESP32-C3, GxEPD2 1.6.9.

**Spec:** `docs/superpowers/specs/2026-10-01-pop-xteink-x4-design.md`. Read it before starting; this plan implements it section by section.

**How this plan was checked:** every source file below was built and run in a throwaway prototype during planning. On the Mac, all seven test programs pass, the real game image boots through the title sequence and the demo level, and a 260,000-frame run completes without a halt. The firmware compiles and links for the ESP32-C3 (58.5% static RAM, 37% of the app partition). Nothing has been run on the device: Tasks 10 and 11 are the first contact with real hardware.

## Global Constraints

- Never commit the game image, game sources, any Apple ROM, the CPU test binary, or a device flash backup. `game/work/`, `game/out/`, `tests/data/` and `backup/` are gitignored.
- `core/` is portable C99: no platform headers, no dynamic allocation. The shells use it only through `core/machine.h`.
- Host code compiles with `-std=c99 -Wall -Wextra` and produces no warnings.
- Game source: `https://github.com/adamgreen/Prince-of-Persia-Apple-II.git` at commit `ec78dbfd51013ba349cda8c51c3ce0595fe75342`, built unpatched.
- Game image: `game/out/PrinceOfPersia_3.5.hdv`, exactly 819,200 bytes, SHA-256 `21428d817130b9f69673266f9bca361bb3d28286b4a84ffe6524abe844da7080`.
- CPU core source: `fake6502.c` from `wutka/kim1-emulator` at commit `cf375bba81b86f69a8b0f72a64c967d87c39886a`, SHA-256 `c2e985a16ca8425284ff2172feb56862fb02b9ab57faa4d8cbffd6a5d5d2878c`. After patching, `core/fake6502.inc` has SHA-256 `99152219fdf79fcdcbc77e64373059612179d434035d9000ff0b82bfe409ab01`.
- CPU test binary: `6502_functional_test.bin`, SHA-256 `fa12bfc761e6f9057e4cc01a665a7b800ff01ae91f598af1e39a1201d01953fd`; success is the trap at `$3469`.
- Timing: 17,030 CPU cycles per frame; `$C019` bit 7 is set for cycles 0–12,479 and clear for 12,480–17,029.
- Picture: 560×384, 1 bit per pixel, 70 bytes per row, 26,880 bytes, most significant bit leftmost, 1 = lit (white).
- SmartPort entry address `$C583`; a call costs 1,000 emulated cycles.
- Firmware: PlatformIO, Arduino framework, board `esp32-c3-devkitm-1`, flash size 16 MB, app partition 3 MB. Verified with the pioarduino `espressif32` platform 55.03.37 (Arduino core 3.3.7), which is already installed on the development Mac.
- Panel wiring: SCLK = GPIO8, MOSI = GPIO10, CS = GPIO21, DC = GPIO4, RST = GPIO5, BUSY = GPIO6. Picture window at panel position (120, 48).
- Wi-Fi and Bluetooth are never initialised.
- Out of scope: input, sound, saved games, power management, SD card, colour or grayscale, text-mode rendering, any other Apple II software.
- **Anything that writes to the device needs your human partner's explicit go-ahead first.** Flashing replaces the firmware on their e-reader.

## Review Focus

These are the conditions the spec implies but that are easy to miss. Each one has a step in the task named.

1. **The device runs on battery with no USB host attached.** Serial logging must not stall the emulator. Pinned by `Serial.setTxTimeoutMs(0)` in Task 10 and the unplugged check in Task 11, Step 8.
2. **The demo is left running for more than 70 minutes.** The CPU core's cycle counter is 32 bits and wraps; emulation must carry on. Pinned by `make soak` in Task 8, Step 9.
3. **The game image is rebuilt, or missing, when the firmware is built.** The image is pulled in by the assembler, which the build system does not track. A missing image must fail the build with a clear message, and the boot log must show which image is running. Pinned in Task 10, Steps 5 and 9.
4. **A default game build follows a release-patch build.** The patch edits sources in place; the next default build must still produce the pinned hash. Pinned in Task 1, Step 7.
5. **The machine halts on the device.** This is the only error path that needs the panel. Pinned by the `SELF_TEST_HALT` check in Task 10, Step 10 (serial) and Task 11, Step 9 (panel).

---

## File Structure

```
.gitignore
Makefile                      host build: tests, host programs, soak run
README.md                     how to build, flash, restore (Task 11)
game/
  Dockerfile                  32-bit Debian with make and patch
  build.sh                    source -> game/out/PrinceOfPersia_3.5.hdv
core/
  rom.h, rom.c                stub ROM contents
  bus.h, bus.c                128K memory, page tables, banking
  softswitch.h, softswitch.c  $C0xx switches, vertical-blank flag
  fake6502.inc                vendored CPU core (generated, committed)
  cpu.h, cpu.c                wrapper over fake6502
  blockdev.h, blockdev.c      SmartPort trap serving image blocks
  video.h, video.c            hi-res / double hi-res -> 560x384 1-bit
  boot.h, boot.c              loads the boot file, sets registers
  machine.h, machine.c        public API and frame loop
host/
  headless.c                  pop-headless: run N frames, dump, hash
  sdl_main.c                  pop-sdl: window at real-time speed
tests/
  test.h                      CHECK / CHECK_EQ macros
  test_bus.c, test_softswitch.c, test_cpu.c, test_blockdev.c,
  test_video.c, test_machine.c, test_boot.c
  golden.txt                  frame number -> picture hash
  fetch_klaus.sh              downloads the CPU test binary
tools/
  vendor_fake6502.py          fetch + patch fake6502
  pbm2png.py                  convert dumped frames for viewing
firmware/
  platformio.ini, partitions.csv
  src/config.h                pins and tuning flags
  src/disk_image.S            embeds the game image in flash
  src/main.cpp                emulator loop, pacing, logging, display task
  src/display.h, display.cpp  panel driver glue and halt screen
```

Dependency direction inside `core/`: `machine` → `boot`, `blockdev`, `video`, `cpu` → `bus` ↔ `softswitch`, and `bus` → `rom`. The `bus` and `softswitch` units call each other (the bus routes page `$C0` to the switches; the switches tell the bus to remap), which is why `softswitch.h` is created in Task 2 and implemented in Task 3.

The `Makefile` created in Task 1 already names every test and host program. A target only builds once its sources exist, so each task builds and runs its own test with `make build/test_<name> && build/test_<name>`, and `make test` runs everything from Task 8 onward.

---

### Task 1: Repository scaffold and game build

**Files:**
- Create: `.gitignore`
- Create: `Makefile`
- Create: `tests/test.h`
- Create: `game/Dockerfile`
- Create: `game/build.sh`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `game/out/PrinceOfPersia_3.5.hdv` (819,200 bytes) for Tasks 8–11.
  - `make build/<target>`, `make test`, `make soak`, `make host`, `make game`, `make clean`.
  - `tests/test.h`: `CHECK(cond)`, `CHECK_EQ(actual, expected)`, `TEST_RESULT()` (returns the process exit code and prints `ok` or the failure count), and the counter `test_failures`.

- [ ] **Step 1: Create a working branch**

```bash
git switch -c pop-emulator
```

- [ ] **Step 2: Write `.gitignore`**

```
# Host build output
build/

# Game sources and the built disk image: never committed
game/work/
game/out/

# Test fixtures fetched on demand
tests/data/

# PlatformIO
firmware/.pio/

# Flash backups of the device
backup/
```

- [ ] **Step 3: Write `Makefile`**

Recipe lines must start with a tab character, not spaces.

```make
CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra
CPPFLAGS += -Icore -Itests

BUILD := build
IMAGE := game/out/PrinceOfPersia_3.5.hdv
KLAUS := tests/data/6502_functional_test.bin

CORE := core/cpu.c core/bus.c core/softswitch.c core/rom.c core/blockdev.c \
        core/boot.c core/video.c core/machine.c
CORE_DEPS := $(wildcard core/*.h core/*.inc)

TESTS := test_bus test_softswitch test_cpu test_blockdev test_video \
         test_machine test_boot

.PHONY: all test soak host game clean
all: host

# Each test links only the core files it exercises.
$(BUILD)/test_bus: tests/test_bus.c core/bus.c core/rom.c
$(BUILD)/test_softswitch: tests/test_softswitch.c core/bus.c core/rom.c core/softswitch.c
$(BUILD)/test_cpu: tests/test_cpu.c core/cpu.c core/bus.c core/rom.c core/softswitch.c
$(BUILD)/test_blockdev: tests/test_blockdev.c core/blockdev.c core/cpu.c core/bus.c core/rom.c core/softswitch.c
$(BUILD)/test_video: tests/test_video.c core/video.c core/bus.c core/rom.c core/softswitch.c
$(BUILD)/test_machine: tests/test_machine.c $(CORE)
$(BUILD)/test_boot: tests/test_boot.c $(CORE)

$(BUILD)/test_%: tests/test.h $(CORE_DEPS)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(filter %.c,$^)

$(KLAUS):
	tests/fetch_klaus.sh

test: $(addprefix $(BUILD)/,$(TESTS)) $(KLAUS)
	$(BUILD)/test_bus
	$(BUILD)/test_softswitch
	$(BUILD)/test_cpu $(KLAUS)
	$(BUILD)/test_blockdev
	$(BUILD)/test_video
	$(BUILD)/test_machine
	$(BUILD)/test_boot $(IMAGE) tests/golden.txt

host: $(BUILD)/pop-headless $(BUILD)/pop-sdl

$(BUILD)/pop-headless: host/headless.c $(CORE) $(CORE_DEPS)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ host/headless.c $(CORE)

$(BUILD)/pop-sdl: host/sdl_main.c $(CORE) $(CORE_DEPS)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(shell sdl2-config --cflags) -o $@ host/sdl_main.c $(CORE) $(shell sdl2-config --libs)

# 260,000 frames is 4.4 billion CPU cycles: past the point where the CPU
# core's 32-bit cycle counter wraps (about 70 minutes of emulated time).
soak: $(BUILD)/pop-headless
	$(BUILD)/pop-headless $(IMAGE) --frames 260000

game:
	game/build.sh

clean:
	rm -rf $(BUILD)
```

- [ ] **Step 4: Write `tests/test.h`**

```c
#ifndef TEST_H
#define TEST_H

#include <stdio.h>

static int test_failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            test_failures++; \
        } \
    } while (0)

#define CHECK_EQ(actual, expected) \
    do { \
        long actual_ = (long)(actual); \
        long expected_ = (long)(expected); \
        if (actual_ != expected_) { \
            printf("FAIL %s:%d: %s is 0x%lX, expected 0x%lX\n", __FILE__, __LINE__, \
                   #actual, actual_, expected_); \
            test_failures++; \
        } \
    } while (0)

#define TEST_RESULT() (test_failures ? (printf("%d failure(s)\n", test_failures), 1) : (printf("ok\n"), 0))

#endif
```

- [ ] **Step 5: Write `game/Dockerfile` and `game/build.sh`**

`game/Dockerfile`:

```dockerfile
# The build fork's assembler (snap) and disk imager (crackle) are 32-bit
# Linux binaries, so the build runs in a 32-bit userland.
FROM i386/debian:bookworm-slim
RUN apt-get update \
    && apt-get install -y --no-install-recommends make patch \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
```

`game/build.sh`:

```sh
#!/bin/sh
# Build the Prince of Persia 3.5" disk image from Jordan Mechner's source,
# using Adam Green's build fork inside a 32-bit Linux container.
#
#   game/build.sh                  unpatched repository source (hash-checked)
#   game/build.sh --release-patch  patched to match the retail 3.5" release
#
# Output: game/out/PrinceOfPersia_3.5.hdv (819,200 bytes, never committed).
set -eu

REPO="https://github.com/adamgreen/Prince-of-Persia-Apple-II.git"
COMMIT="ec78dbfd51013ba349cda8c51c3ce0595fe75342"
EXPECTED_SHA256="21428d817130b9f69673266f9bca361bb3d28286b4a84ffe6524abe844da7080"
IMAGE_NAME="PrinceOfPersia_3.5.hdv"

HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$HERE/work"
OUT="$HERE/out"

MAKE_ARGS="clean all"
CHECK_HASH=1
case "${1:-}" in
    "") ;;
    --release-patch) MAKE_ARGS="clean all RELEASE_PATCH=1"; CHECK_HASH=0 ;;
    *) echo "usage: build.sh [--release-patch]" >&2; exit 2 ;;
esac

if [ ! -d "$WORK/.git" ]; then
    git clone --quiet "$REPO" "$WORK"
fi
# Start from pristine sources every time: the release patch edits files in
# place and a previous build leaves objects behind.
git -C "$WORK" checkout --quiet --force "$COMMIT"
git -C "$WORK" clean -fdxq

docker build --quiet --platform linux/386 -t pop-build "$HERE" >/dev/null
# shellcheck disable=SC2086
docker run --rm --platform linux/386 -v "$WORK":/src -w /src pop-build make $MAKE_ARGS

mkdir -p "$OUT"
cp "$WORK/$IMAGE_NAME" "$OUT/$IMAGE_NAME"

ACTUAL_SHA256="$(shasum -a 256 "$OUT/$IMAGE_NAME" | cut -d' ' -f1)"
echo "built $OUT/$IMAGE_NAME"
echo "sha256 $ACTUAL_SHA256"
if [ "$CHECK_HASH" = 1 ] && [ "$ACTUAL_SHA256" != "$EXPECTED_SHA256" ]; then
    echo "error: image hash does not match the expected build" >&2
    echo "  expected $EXPECTED_SHA256" >&2
    echo "  got      $ACTUAL_SHA256" >&2
    exit 1
fi
```

```bash
chmod +x game/build.sh
```

- [ ] **Step 6: Build the game image**

Docker must be running. The first run clones the fork and builds the container image, which takes a minute or two.

Run: `game/build.sh`

Expected: the last lines are

```
built <repo>/game/out/PrinceOfPersia_3.5.hdv
sha256 21428d817130b9f69673266f9bca361bb3d28286b4a84ffe6524abe844da7080
```

and the exit status is 0. The assembler prints two warnings about `DO`/`FIN` directives in `GRAFIX.S` and `SPECIALK.S`; those are expected.

- [ ] **Step 7: Check that a release-patch build does not contaminate the next default build**

```bash
game/build.sh --release-patch | tail -1
game/build.sh | tail -1
game/build.sh --bogus; echo "exit=$?"
```

Expected, in order:

```
sha256 fb18591844f8928174b14893bdd815c7da62ea238fc50c1096226dd134364db2
sha256 21428d817130b9f69673266f9bca361bb3d28286b4a84ffe6524abe844da7080
usage: build.sh [--release-patch]
exit=2
```

- [ ] **Step 8: Confirm nothing from the game is tracked**

Run: `git status --short`

Expected: only `.gitignore`, `Makefile`, `game/Dockerfile`, `game/build.sh` and `tests/` appear. `game/work/` and `game/out/` do not.

- [ ] **Step 9: Commit**

```bash
git add .gitignore Makefile tests/test.h game/Dockerfile game/build.sh
git commit -m "Add repository scaffold and game build script"
```

---

### Task 2: Stub ROM and memory bus

**Files:**
- Create: `core/rom.h`, `core/rom.c`
- Create: `core/softswitch.h` (interface only; implemented in Task 3)
- Create: `core/bus.h`, `core/bus.c`
- Test: `tests/test_bus.c`

**Interfaces:**
- Consumes: `tests/test.h` from Task 1.
- Produces:
  - `ROM_SMARTPORT_ENTRY` (`0xC583`), `void rom_fill(uint8_t *rom)` (16 KB buffer, index 0 = `$C000`).
  - `bus_banking_t` with `bool` fields `store80, ramrd, ramwrt, altzp, page2, hires, lc_ram_read, lc_write, lc_bank2`.
  - `void bus_init(void)`, `void bus_remap(const bus_banking_t *)`, `uint8_t bus_read(uint16_t)`, `void bus_write(uint16_t, uint8_t)`, `uint8_t *bus_main(void)`, `uint8_t *bus_aux(void)`, `uint8_t *bus_rom(void)`.
  - The full `softswitch.h` interface that Task 3 implements: `softswitch_state_t`, `softswitch_reset`, `softswitch_read`, `softswitch_write`, `softswitch_set_frame_cycle`, `softswitch_state`, `SOFTSWITCH_CYCLES_PER_FRAME`, `SOFTSWITCH_VBL_START_CYCLE`.

Storage layout to know: each of main and aux is a 64 KB array indexed by address. Language-card bank 2 lives at `$D000`–`$DFFF` of the array and bank 1 at `$C000`–`$CFFF` (that range is never RAM on a real machine).

- [ ] **Step 1: Write the failing test**

This test links only `bus.c` and `rom.c`. It supplies its own recording versions of `softswitch_read` and `softswitch_write`, so the bus is tested in isolation.

`tests/test_bus.c`:

```c
/* Bus tests. Links only core/bus.c and core/rom.c; the soft switches are
   replaced by the recording fakes below. */
#include <string.h>

#include "bus.h"
#include "rom.h"
#include "test.h"

static uint8_t last_read_reg;
static uint8_t last_write_reg;
static uint8_t last_write_value;
static int read_calls;
static int write_calls;

uint8_t softswitch_read(uint8_t reg)
{
    last_read_reg = reg;
    read_calls++;
    return 0xA5;
}

void softswitch_write(uint8_t reg, uint8_t value)
{
    last_write_reg = reg;
    last_write_value = value;
    write_calls++;
}

static bus_banking_t banking(void)
{
    bus_banking_t b;

    memset(&b, 0, sizeof b);
    b.lc_bank2 = true;
    return b;
}

static void test_rom_contents(void)
{
    uint8_t rom[0x4000];

    rom_fill(rom);
    CHECK_EQ(rom[0xFBB3 - 0xC000], 0x06);
    CHECK_EQ(rom[0xFBC0 - 0xC000], 0x60);
    CHECK_EQ(rom[0xFE1F - 0xC000], 0x60);
    CHECK_EQ(rom[0xFB2F - 0xC000], 0x60);
    CHECK_EQ(rom[0xC501 - 0xC000], 0x20);
    CHECK_EQ(rom[0xC503 - 0xC000], 0x00);
    CHECK_EQ(rom[0xC505 - 0xC000], 0x03);
    CHECK_EQ(rom[0xC507 - 0xC000], 0x00);
    CHECK_EQ(rom[0xC5FF - 0xC000], 0x80);
    CHECK_EQ(0xC500 + rom[0xC5FF - 0xC000] + 3, ROM_SMARTPORT_ENTRY);
}

static void test_power_on(void)
{
    bus_init();
    bus_write(0x0300, 0x11);
    CHECK_EQ(bus_main()[0x0300], 0x11);
    CHECK_EQ(bus_aux()[0x0300], 0x00);
    CHECK_EQ(bus_read(0x0300), 0x11);

    /* $D000-$FFFF reads stub ROM and ignores writes. */
    CHECK_EQ(bus_read(0xFBB3), 0x06);
    CHECK_EQ(bus_read(0xE000), 0x60);
    bus_write(0xE000, 0x99);
    CHECK_EQ(bus_read(0xE000), 0x60);
    CHECK_EQ(bus_main()[0xE000], 0x00);

    /* Slot ROM area reads stub ROM and ignores writes. */
    CHECK_EQ(bus_read(0xC5FF), 0x80);
    bus_write(0xC5FF, 0x12);
    CHECK_EQ(bus_read(0xC5FF), 0x80);
}

static void test_init_clears_memory(void)
{
    bus_init();
    bus_main()[0x1234] = 0x55;
    bus_aux()[0x1234] = 0x66;
    bus_init();
    CHECK_EQ(bus_main()[0x1234], 0x00);
    CHECK_EQ(bus_aux()[0x1234], 0x00);
}

static void test_io_page_routes_to_softswitch(void)
{
    bus_init();
    read_calls = 0;
    write_calls = 0;

    CHECK_EQ(bus_read(0xC019), 0xA5);
    CHECK_EQ(read_calls, 1);
    CHECK_EQ(last_read_reg, 0x19);

    bus_write(0xC005, 0x42);
    CHECK_EQ(write_calls, 1);
    CHECK_EQ(last_write_reg, 0x05);
    CHECK_EQ(last_write_value, 0x42);

    /* Neighbouring pages do not reach the soft switches. */
    (void)bus_read(0xBFFF);
    (void)bus_read(0xC100);
    CHECK_EQ(read_calls, 1);
}

static void test_ramrd_ramwrt(void)
{
    bus_banking_t b = banking();

    bus_init();
    bus_main()[0x0300] = 0x11;
    bus_aux()[0x0300] = 0x22;

    b.ramrd = true;
    bus_remap(&b);
    CHECK_EQ(bus_read(0x0300), 0x22);
    bus_write(0x0300, 0x33); /* RAMWRT still main */
    CHECK_EQ(bus_main()[0x0300], 0x33);
    CHECK_EQ(bus_aux()[0x0300], 0x22);

    b.ramrd = false;
    b.ramwrt = true;
    bus_remap(&b);
    CHECK_EQ(bus_read(0x0300), 0x33);
    bus_write(0x0300, 0x44);
    CHECK_EQ(bus_aux()[0x0300], 0x44);
    CHECK_EQ(bus_main()[0x0300], 0x33);

    /* The range is $0200-$BFFF: zero page and stack are not affected. */
    bus_write(0x0080, 0x55);
    bus_write(0x01FF, 0x66);
    bus_write(0xBFFF, 0x77);
    CHECK_EQ(bus_main()[0x0080], 0x55);
    CHECK_EQ(bus_main()[0x01FF], 0x66);
    CHECK_EQ(bus_aux()[0xBFFF], 0x77);
    bus_write(0x0200, 0x88);
    CHECK_EQ(bus_aux()[0x0200], 0x88);
}

static void test_altzp(void)
{
    bus_banking_t b = banking();

    bus_init();
    b.altzp = true;
    bus_remap(&b);
    bus_write(0x0080, 0x11);
    bus_write(0x0180, 0x22);
    bus_write(0x0200, 0x33);
    CHECK_EQ(bus_aux()[0x0080], 0x11);
    CHECK_EQ(bus_aux()[0x0180], 0x22);
    CHECK_EQ(bus_main()[0x0080], 0x00);
    CHECK_EQ(bus_main()[0x0200], 0x33); /* not zero page: follows RAMWRT */
    CHECK_EQ(bus_read(0x0080), 0x11);
}

static void test_store80(void)
{
    bus_banking_t b = banking();

    bus_init();

    /* 80STORE + PAGE2: text page goes to aux, hi-res page does not. */
    b.store80 = true;
    b.page2 = true;
    bus_remap(&b);
    bus_write(0x0400, 0x11);
    bus_write(0x07FF, 0x12);
    bus_write(0x0800, 0x13);
    bus_write(0x2000, 0x14);
    CHECK_EQ(bus_aux()[0x0400], 0x11);
    CHECK_EQ(bus_aux()[0x07FF], 0x12);
    CHECK_EQ(bus_main()[0x0800], 0x13);
    CHECK_EQ(bus_main()[0x2000], 0x14);
    CHECK_EQ(bus_read(0x0400), 0x11);

    /* With HIRES too, $2000-$3FFF follows PAGE2. */
    b.hires = true;
    bus_remap(&b);
    bus_write(0x2000, 0x21);
    bus_write(0x3FFF, 0x22);
    bus_write(0x4000, 0x23);
    CHECK_EQ(bus_aux()[0x2000], 0x21);
    CHECK_EQ(bus_aux()[0x3FFF], 0x22);
    CHECK_EQ(bus_main()[0x4000], 0x23);

    /* 80STORE overrides RAMRD/RAMWRT for those ranges. */
    b.page2 = false;
    b.ramrd = true;
    b.ramwrt = true;
    bus_remap(&b);
    bus_write(0x0400, 0x31);
    bus_write(0x2000, 0x32);
    bus_write(0x0800, 0x33);
    CHECK_EQ(bus_main()[0x0400], 0x31);
    CHECK_EQ(bus_main()[0x2000], 0x32);
    CHECK_EQ(bus_aux()[0x0800], 0x33);
    CHECK_EQ(bus_read(0x0400), 0x31);

    /* Without 80STORE, PAGE2 has no effect on memory. */
    b = banking();
    b.page2 = true;
    b.hires = true;
    bus_remap(&b);
    bus_write(0x0400, 0x41);
    CHECK_EQ(bus_main()[0x0400], 0x41);
}

static void test_language_card(void)
{
    bus_banking_t b = banking();

    bus_init();

    /* Read RAM + write enabled, bank 2: array offsets equal addresses. */
    b.lc_ram_read = true;
    b.lc_write = true;
    bus_remap(&b);
    bus_write(0xD000, 0x11);
    bus_write(0xE000, 0x12);
    bus_write(0xFFFF, 0x13);
    CHECK_EQ(bus_main()[0xD000], 0x11);
    CHECK_EQ(bus_main()[0xE000], 0x12);
    CHECK_EQ(bus_main()[0xFFFF], 0x13);
    CHECK_EQ(bus_read(0xD000), 0x11);

    /* Bank 1: $D000-$DFFF is stored at $C000-$CFFF of the array. */
    b.lc_bank2 = false;
    bus_remap(&b);
    bus_write(0xD000, 0x21);
    bus_write(0xDFFF, 0x22);
    CHECK_EQ(bus_main()[0xC000], 0x21);
    CHECK_EQ(bus_main()[0xCFFF], 0x22);
    CHECK_EQ(bus_main()[0xD000], 0x11); /* bank 2 untouched */
    CHECK_EQ(bus_read(0xD000), 0x21);
    CHECK_EQ(bus_read(0xE000), 0x12); /* $E000+ is shared by both banks */

    /* Read ROM while writing RAM. */
    b.lc_bank2 = true;
    b.lc_ram_read = false;
    bus_remap(&b);
    bus_write(0xD000, 0x31);
    CHECK_EQ(bus_main()[0xD000], 0x31);
    CHECK_EQ(bus_read(0xD000), 0x60);

    /* Read RAM, write-protected. */
    b.lc_ram_read = true;
    b.lc_write = false;
    bus_remap(&b);
    bus_write(0xD000, 0x41);
    CHECK_EQ(bus_read(0xD000), 0x31);

    /* ALTZP moves the whole language card to aux. */
    b.lc_write = true;
    b.altzp = true;
    bus_remap(&b);
    bus_write(0xD000, 0x51);
    bus_write(0xF000, 0x52);
    CHECK_EQ(bus_aux()[0xD000], 0x51);
    CHECK_EQ(bus_aux()[0xF000], 0x52);
    CHECK_EQ(bus_main()[0xD000], 0x31);
}

int main(void)
{
    test_rom_contents();
    test_power_on();
    test_init_clears_memory();
    test_io_page_routes_to_softswitch();
    test_ramrd_ramwrt();
    test_altzp();
    test_store80();
    test_language_card();
    return TEST_RESULT();
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make build/test_bus`

Expected: make stops with `No rule to make target` naming `core/bus.c` (needed by `build/test_bus`).

- [ ] **Step 3: Write the stub ROM**

`core/rom.h`:

```c
#ifndef ROM_H
#define ROM_H

#include <stdint.h>

/* Address the game jumps to for SmartPort calls on the fake slot-5 card. */
#define ROM_SMARTPORT_ENTRY 0xC583

/* Fill a 16 KB buffer (index 0 = address $C000) with the stub ROM. */
void rom_fill(uint8_t *rom);

#endif
```

`core/rom.c`:

```c
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
```

- [ ] **Step 4: Write the soft-switch interface**

`core/softswitch.h`:

```c
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
```

- [ ] **Step 5: Write the bus**

`core/bus.h`:

```c
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
```

`core/bus.c`:

```c
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
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `make build/test_bus && build/test_bus`

Expected: the compile line prints with no warnings, then `ok`.

- [ ] **Step 7: Commit**

```bash
git add core/rom.h core/rom.c core/softswitch.h core/bus.h core/bus.c tests/test_bus.c
git commit -m "Add stub ROM and banked memory bus"
```

---

### Task 3: Soft switches

**Files:**
- Create: `core/softswitch.c`
- Test: `tests/test_softswitch.c`

**Interfaces:**
- Consumes: `bus_init`, `bus_remap`, `bus_read`, `bus_write`, `bus_main`, `bus_aux`, `bus_banking_t` and `core/softswitch.h` from Task 2.
- Produces: the implementation of `softswitch.h`:
  - `void softswitch_reset(void)`: power-on state (everything off, text mode, language card reading ROM, bank 2, writes disabled) and a bus remap.
  - `uint8_t softswitch_read(uint8_t reg)`, `void softswitch_write(uint8_t reg, uint8_t value)`: `reg` is the low byte of a `$C0xx` address.
  - `void softswitch_set_frame_cycle(uint32_t cycle)`: position in the frame, drives `$C019`.
  - `const softswitch_state_t *softswitch_state(void)`.

Behaviour to know:
- `$C000`–`$C00F` change memory flags **only when written**. Reading them returns `$00`.
- `$C050`–`$C057`, `$C05E`, `$C05F` and `$C080`–`$C08F` act on **any access**, read or write.
- Language card: bit 3 of the address selects bank 1 (set) or bank 2 (clear). Low two bits `00` or `11` read RAM; `01` or `10` read ROM. Writing is enabled by two consecutive **reads** of an odd address. An even address disables writing. A write to an odd address resets the count without changing the write-enable state.

- [ ] **Step 1: Write the failing test**

`tests/test_softswitch.c`:

```c
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
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make build/test_softswitch`

Expected: make stops with `No rule to make target` naming `core/softswitch.c` (needed by `build/test_softswitch`).

- [ ] **Step 3: Write the implementation**

`core/softswitch.c`:

```c
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
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `make build/test_softswitch build/test_bus && build/test_softswitch && build/test_bus`

Expected: `ok` twice.

- [ ] **Step 5: Commit**

```bash
git add core/softswitch.c tests/test_softswitch.c
git commit -m "Add Apple IIe soft switches"
```

---

### Task 4: CPU core

**Files:**
- Create: `tools/vendor_fake6502.py`
- Create: `core/fake6502.inc` (generated by the script, then committed)
- Create: `core/cpu.h`, `core/cpu.c`
- Create: `tests/fetch_klaus.sh`
- Test: `tests/test_cpu.c`

**Interfaces:**
- Consumes: `bus_read`, `bus_write`, `bus_init`, `bus_remap`, `bus_main`, `softswitch_reset` from Tasks 2–3.
- Produces:
  - `cpu_regs_t { uint16_t pc; uint8_t a, x, y, sp, status; }`
  - `void cpu_get_regs(cpu_regs_t *out)`, `void cpu_set_regs(const cpu_regs_t *in)`, `uint16_t cpu_pc(void)`
  - `bool cpu_opcode_is_documented(uint8_t opcode)`: true for the 151 documented NMOS opcodes.
  - `uint32_t cpu_step(void)`: runs one instruction, returns its cycle count.
  - `CPU_FLAG_CARRY` (`0x01`), `CPU_FLAG_INTERRUPT` (`0x04`), `CPU_FLAG_CONSTANT` (`0x20`).

Background: fake6502 is a public-domain single-file 6502 core by Mike Chambers. Its original host is offline, so the script fetches a byte-identical mirror at a pinned commit. Unmodified, it fails the functional test in decimal mode (it traps at `$3477`), and it declares its registers as globals named `a`, `x`, `y`, `pc`, `sp` and `status`. The script fixes both. `cpu.c` then includes the result as part of its own translation unit, so nothing else can see those names and the bus calls are inlined.

- [ ] **Step 1: Write the failing test**

`tests/test_cpu.c`:

```c
/* CPU tests. The functional test binary is passed as argv[1]. */
#include <stdio.h>
#include <string.h>

#include "bus.h"
#include "cpu.h"
#include "softswitch.h"
#include "test.h"

#define KLAUS_START 0x0400
#define KLAUS_SUCCESS 0x3469
#define KLAUS_MAX_STEPS 100000000L

static void test_documented_opcodes(void)
{
    int count = 0;
    int op;

    for (op = 0; op < 256; op++) {
        if (cpu_opcode_is_documented((uint8_t)op)) {
            count++;
        }
    }
    CHECK_EQ(count, 151);
    CHECK(cpu_opcode_is_documented(0xEA)); /* NOP */
    CHECK(cpu_opcode_is_documented(0x00)); /* BRK */
    CHECK(cpu_opcode_is_documented(0xA9)); /* LDA # */
    CHECK(!cpu_opcode_is_documented(0x02));
    CHECK(!cpu_opcode_is_documented(0x1A));
    CHECK(!cpu_opcode_is_documented(0xEB)); /* undocumented SBC alias */
    CHECK(!cpu_opcode_is_documented(0xFF));
}

static void test_registers_round_trip(void)
{
    cpu_regs_t in = { 0x1234, 0x11, 0x22, 0x33, 0x44, 0x01 };
    cpu_regs_t out;

    cpu_set_regs(&in);
    cpu_get_regs(&out);
    CHECK_EQ(out.pc, 0x1234);
    CHECK_EQ(out.a, 0x11);
    CHECK_EQ(out.x, 0x22);
    CHECK_EQ(out.y, 0x33);
    CHECK_EQ(out.sp, 0x44);
    CHECK_EQ(out.status, 0x01 | CPU_FLAG_CONSTANT);
    CHECK_EQ(cpu_pc(), 0x1234);
}

static void test_step_uses_bus_and_counts_cycles(void)
{
    cpu_regs_t r = { 0x0300, 0, 0, 0, 0xFF, CPU_FLAG_CONSTANT };
    cpu_regs_t out;

    bus_init();
    softswitch_reset();
    bus_main()[0x0300] = 0xA9; /* LDA #$42 */
    bus_main()[0x0301] = 0x42;
    bus_main()[0x0302] = 0x8D; /* STA $0310 */
    bus_main()[0x0303] = 0x10;
    bus_main()[0x0304] = 0x03;
    cpu_set_regs(&r);

    CHECK_EQ(cpu_step(), 2);
    CHECK_EQ(cpu_step(), 4);
    cpu_get_regs(&out);
    CHECK_EQ(out.a, 0x42);
    CHECK_EQ(out.pc, 0x0305);
    CHECK_EQ(bus_main()[0x0310], 0x42);
}

static void test_klaus_functional(const char *path)
{
    FILE *f = fopen(path, "rb");
    bus_banking_t ram_everywhere;
    cpu_regs_t r = { KLAUS_START, 0, 0, 0, 0xFF, CPU_FLAG_CONSTANT };
    uint16_t last_pc = 0xFFFF;
    long steps = 0;

    if (f == NULL) {
        printf("FAIL cannot open %s (run tests/fetch_klaus.sh)\n", path);
        test_failures++;
        return;
    }
    bus_init();
    if (fread(bus_main(), 1, 0x10000, f) != 0x10000) {
        printf("FAIL %s is not 65536 bytes\n", path);
        test_failures++;
        fclose(f);
        return;
    }
    fclose(f);

    /* The test binary carries its own vectors at $FFFA, so the language
       card must read and write RAM. */
    memset(&ram_everywhere, 0, sizeof ram_everywhere);
    ram_everywhere.lc_ram_read = true;
    ram_everywhere.lc_write = true;
    ram_everywhere.lc_bank2 = true;
    bus_remap(&ram_everywhere);

    cpu_set_regs(&r);
    /* Every failure and the final success are "jump to self" traps. */
    while (cpu_pc() != last_pc && steps < KLAUS_MAX_STEPS) {
        last_pc = cpu_pc();
        cpu_step();
        steps++;
    }
    CHECK_EQ(cpu_pc(), KLAUS_SUCCESS);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: test_cpu <6502_functional_test.bin>\n");
        return 2;
    }
    test_documented_opcodes();
    test_registers_round_trip();
    test_step_uses_bus_and_counts_cycles();
    test_klaus_functional(argv[1]);
    return TEST_RESULT();
}
```

- [ ] **Step 2: Write the test-binary fetch script**

`tests/fetch_klaus.sh`:

```sh
#!/bin/sh
# Download Klaus Dormann's 6502 functional test binary (GPL-3, test fixture
# only, never committed or shipped).
set -eu

URL="https://raw.githubusercontent.com/Klaus2m5/6502_65C02_functional_tests/master/bin_files/6502_functional_test.bin"
SHA256="fa12bfc761e6f9057e4cc01a665a7b800ff01ae91f598af1e39a1201d01953fd"
OUT="$(dirname "$0")/data/6502_functional_test.bin"

mkdir -p "$(dirname "$OUT")"
curl -fsSL -o "$OUT.tmp" "$URL"
ACTUAL="$(shasum -a 256 "$OUT.tmp" | cut -d' ' -f1)"
if [ "$ACTUAL" != "$SHA256" ]; then
    echo "hash mismatch for $URL" >&2
    echo "  expected $SHA256" >&2
    echo "  got      $ACTUAL" >&2
    rm -f "$OUT.tmp"
    exit 1
fi
mv "$OUT.tmp" "$OUT"
echo "wrote $OUT"
```

```bash
chmod +x tests/fetch_klaus.sh
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `make build/test_cpu`

Expected: make stops with `No rule to make target` naming `core/cpu.c` (needed by `build/test_cpu`).

- [ ] **Step 4: Write the vendoring script**

`tools/vendor_fake6502.py`:

```python
#!/usr/bin/env python3
"""Fetch fake6502 (public domain, Mike Chambers) at a pinned commit, patch it,
and write core/fake6502.inc.

Patches applied:
  1. Registers and helper variables become static, so their short names
     (a, x, y, pc, sp, status, ...) stay out of the link namespace.
  2. ADC and SBC get correct NMOS decimal-mode behaviour. The original fails
     the Klaus Dormann functional test in decimal mode.
"""
import hashlib
import pathlib
import sys
import urllib.request

URL = ("https://raw.githubusercontent.com/wutka/kim1-emulator/"
       "cf375bba81b86f69a8b0f72a64c967d87c39886a/fake6502.c")
SHA256 = "c2e985a16ca8425284ff2172feb56862fb02b9ab57faa4d8cbffd6a5d5d2878c"
OUT = pathlib.Path(__file__).resolve().parent.parent / "core" / "fake6502.inc"

STATIC_LINES = [
    "uint16_t pc;",
    "uint8_t sp, a, x, y, status;",
    "uint32_t instructions = 0; //keep track of total instructions executed",
    "uint32_t clockticks6502 = 0, clockgoal6502 = 0;",
    "uint16_t oldpc, ea, reladdr, value, result;",
    "uint8_t opcode, oldstatus;",
    "uint8_t penaltyop, penaltyaddr;",
    "uint8_t callexternal = 0;",
    "void (*loopexternal)();",
]

ADC = '''static void adc() {
    penaltyop = 1;
    value = getvalue();

    if (status & FLAG_DECIMAL) {
        /* NMOS 6502 decimal add: Z from the binary sum, N and V from the
           intermediate result, C from the decimal result. */
        uint16_t carry_in = (uint16_t)(status & FLAG_CARRY);
        uint16_t lo = (uint16_t)(a & 0x0F) + (value & 0x0F) + carry_in;
        uint16_t hi = (uint16_t)(a >> 4) + (value >> 4);
        result = (uint16_t)a + value + carry_in;
        zerocalc(result);
        if (lo > 0x09) { lo += 0x06; hi++; }
        result = (uint16_t)((hi << 4) | (lo & 0x0F));
        signcalc(result);
        overflowcalc(result, a, value);
        if (hi > 0x09) hi += 0x06;
        if (hi > 0x0F) setcarry();
            else clearcarry();
        result = (uint16_t)(((hi << 4) | (lo & 0x0F)) & 0x00FF);
    } else {
        result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
        carrycalc(result);
        zerocalc(result);
        overflowcalc(result, a, value);
        signcalc(result);
    }

    saveaccum(result);
}

'''

SBC = '''static void sbc() {
    uint16_t operand;
    uint16_t carry_in = (uint16_t)(status & FLAG_CARRY);
    penaltyop = 1;
    operand = getvalue();
    value = operand ^ 0x00FF;
    result = (uint16_t)a + value + carry_in;
    carrycalc(result);
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);

    if (status & FLAG_DECIMAL) {
        /* NMOS 6502 decimal subtract: the flags above come from the binary
           result; only the accumulator is decimal-adjusted. */
        int lo = (int)(a & 0x0F) - (int)(operand & 0x0F) + (int)carry_in - 1;
        int hi = (int)(a >> 4) - (int)(operand >> 4);
        if (lo < 0) { lo -= 0x06; hi--; }
        if (hi < 0) hi -= 0x06;
        result = (uint16_t)((((unsigned)hi << 4) | ((unsigned)lo & 0x0F)) & 0x00FF);
    }

    saveaccum(result);
}

'''


def replace_function(text, name, next_name, new_body):
    start = text.index("static void %s() {" % name)
    end = text.index("static void %s() {" % next_name)
    return text[:start] + new_body + text[end:]


def main():
    data = urllib.request.urlopen(URL, timeout=60).read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        sys.exit("fake6502.c hash mismatch\n  expected %s\n  got      %s" % (SHA256, digest))
    text = data.decode("utf-8")

    for line in STATIC_LINES:
        if text.count(line) != 1:
            sys.exit("expected exactly one occurrence of: %s" % line)
        text = text.replace(line, "static " + line)

    text = replace_function(text, "adc", "and", ADC)
    text = replace_function(text, "sbc", "sec", SBC)

    OUT.write_text(text)
    print("wrote %s (%d bytes)" % (OUT, len(text)))


if __name__ == "__main__":
    main()
```

- [ ] **Step 5: Generate `core/fake6502.inc`**

```bash
python3 tools/vendor_fake6502.py
shasum -a 256 core/fake6502.inc
```

Expected:

```
wrote <repo>/core/fake6502.inc (29198 bytes)
99152219fdf79fcdcbc77e64373059612179d434035d9000ff0b82bfe409ab01  core/fake6502.inc
```

If the script reports a hash mismatch or a missing line, stop: the upstream file is not the one this plan was checked against.

- [ ] **Step 6: Write the wrapper**

`core/cpu.h`:

```c
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
```

`core/cpu.c`:

```c
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
```

- [ ] **Step 7: Run the test to verify it passes**

```bash
make build/test_cpu tests/data/6502_functional_test.bin
build/test_cpu tests/data/6502_functional_test.bin
```

Expected: `make` runs `tests/fetch_klaus.sh` once (`wrote tests/data/6502_functional_test.bin`), the compile prints no warnings, and the test prints `ok`. The functional test executes about 30 million instructions and takes under a second.

- [ ] **Step 8: Commit**

`core/fake6502.inc` is public-domain code and is committed so that builds need no network. `tests/data/` stays untracked.

```bash
git add tools/vendor_fake6502.py core/fake6502.inc core/cpu.h core/cpu.c tests/fetch_klaus.sh tests/test_cpu.c
git commit -m "Add 6502 CPU core (vendored fake6502 with decimal-mode fix)"
```

---

### Task 5: Block device trap

**Files:**
- Create: `core/blockdev.h`, `core/blockdev.c`
- Test: `tests/test_blockdev.c`

**Interfaces:**
- Consumes: `bus_read`, `bus_write`, `bus_main`, `bus_aux`, `bus_init` (Task 2); `softswitch_reset` (Task 3); `cpu_get_regs`, `cpu_set_regs`, `cpu_step`, `cpu_pc`, `cpu_regs_t`, `CPU_FLAG_CARRY`, `CPU_FLAG_CONSTANT` (Task 4); `ROM_SMARTPORT_ENTRY` (Task 2).
- Produces:
  - `void blockdev_init(const uint8_t *image, size_t image_len)`
  - `void blockdev_call(void)`: service one SmartPort call. The caller invokes it instead of `cpu_step()` when `cpu_pc() == ROM_SMARTPORT_ENTRY`.
  - `BLOCKDEV_BLOCK_SIZE` (512), `BLOCKDEV_CALL_CYCLES` (1000), `BLOCKDEV_OK` (`0x00`), `BLOCKDEV_ERR_BAD_COMMAND` (`0x01`), `BLOCKDEV_ERR_IO` (`0x27`), `BLOCKDEV_ERR_WRITE_PROTECTED` (`0x2B`).

The SmartPort calling convention the game uses:

```
        JSR $C583        ; entry on the fake slot-5 card
        DB  command      ; $01 = READ BLOCK, $02 = WRITE BLOCK
        DW  list         ; address of the parameter list
        ; execution resumes here; A = result, carry set on error

list:   DB  3            ; parameter count
        DB  unit
        DW  buffer       ; where the 512 bytes go
        DB  lo, mid, hi  ; 24-bit block number
```

`JSR` pushes the address of its own last byte, so the inline parameters start at the pulled return address plus 1, and execution resumes at the pulled address plus 4.

- [ ] **Step 1: Write the failing test**

`tests/test_blockdev.c`:

```c
#include <string.h>

#include "blockdev.h"
#include "bus.h"
#include "cpu.h"
#include "rom.h"
#include "softswitch.h"
#include "test.h"

#define IMAGE_BLOCKS 4
#define CALL_ADDR 0x0300
#define LIST_ADDR 0x0310

static uint8_t image[IMAGE_BLOCKS * 512];

static void make_image(void)
{
    size_t i;

    for (i = 0; i < sizeof image; i++) {
        image[i] = (uint8_t)((i * 7 + i / 512) & 0xFF);
    }
}

/* Assemble "JSR $C583 / db command / dw LIST_ADDR" at CALL_ADDR and the
   parameter list at LIST_ADDR, then execute the JSR so the CPU is sitting
   on the SmartPort entry exactly as the game would be. */
static void prepare_call(uint8_t command, uint16_t buffer, uint32_t block)
{
    cpu_regs_t r = { CALL_ADDR, 0xEE, 0x12, 0x34, 0xFF, CPU_FLAG_CONSTANT };
    uint8_t *m = bus_main();

    m[CALL_ADDR + 0] = 0x20;
    m[CALL_ADDR + 1] = (uint8_t)(ROM_SMARTPORT_ENTRY & 0xFF);
    m[CALL_ADDR + 2] = (uint8_t)(ROM_SMARTPORT_ENTRY >> 8);
    m[CALL_ADDR + 3] = command;
    m[CALL_ADDR + 4] = (uint8_t)(LIST_ADDR & 0xFF);
    m[CALL_ADDR + 5] = (uint8_t)(LIST_ADDR >> 8);

    m[LIST_ADDR + 0] = 3;
    m[LIST_ADDR + 1] = 1;
    m[LIST_ADDR + 2] = (uint8_t)(buffer & 0xFF);
    m[LIST_ADDR + 3] = (uint8_t)(buffer >> 8);
    m[LIST_ADDR + 4] = (uint8_t)(block & 0xFF);
    m[LIST_ADDR + 5] = (uint8_t)((block >> 8) & 0xFF);
    m[LIST_ADDR + 6] = (uint8_t)((block >> 16) & 0xFF);

    cpu_set_regs(&r);
    cpu_step();
}

static void power_on(void)
{
    bus_init();
    softswitch_reset();
    blockdev_init(image, sizeof image);
}

static void test_read_block(void)
{
    cpu_regs_t r;

    power_on();
    prepare_call(0x01, 0x4000, 2);
    CHECK_EQ(cpu_pc(), ROM_SMARTPORT_ENTRY);

    blockdev_call();
    cpu_get_regs(&r);

    CHECK_EQ(r.pc, CALL_ADDR + 6); /* resumes after the inline parameters */
    CHECK_EQ(r.sp, 0xFF);          /* return address consumed */
    CHECK_EQ(r.a, BLOCKDEV_OK);
    CHECK_EQ(r.status & CPU_FLAG_CARRY, 0);
    CHECK_EQ(r.x, 0x12);
    CHECK_EQ(r.y, 0x34);
    CHECK(memcmp(bus_main() + 0x4000, image + 2 * 512, 512) == 0);
    CHECK_EQ(bus_main()[0x3FFF], 0x00); /* nothing before the buffer */
    CHECK_EQ(bus_main()[0x4200], 0x00); /* nothing after the buffer */
}

static void test_read_first_and_last_block(void)
{
    power_on();
    prepare_call(0x01, 0x4000, 0);
    blockdev_call();
    CHECK(memcmp(bus_main() + 0x4000, image, 512) == 0);

    prepare_call(0x01, 0x6000, IMAGE_BLOCKS - 1);
    blockdev_call();
    CHECK(memcmp(bus_main() + 0x6000, image + (IMAGE_BLOCKS - 1) * 512, 512) == 0);
}

static void test_read_follows_banking(void)
{
    cpu_regs_t r;

    /* RAMWRT aux: data lands in aux memory. */
    power_on();
    prepare_call(0x01, 0x4000, 1);
    bus_write(0xC005, 0);
    blockdev_call();
    CHECK(memcmp(bus_aux() + 0x4000, image + 512, 512) == 0);
    CHECK_EQ(bus_main()[0x4000], 0x00);

    /* Language card write-enabled: data lands in $E000 RAM. */
    power_on();
    prepare_call(0x01, 0xE000, 3);
    (void)bus_read(0xC083);
    (void)bus_read(0xC083);
    blockdev_call();
    CHECK(memcmp(bus_main() + 0xE000, image + 3 * 512, 512) == 0);

    /* Language card write-protected: the call succeeds, nothing is stored. */
    power_on();
    prepare_call(0x01, 0xE000, 3);
    blockdev_call();
    cpu_get_regs(&r);
    CHECK_EQ(r.a, BLOCKDEV_OK);
    CHECK_EQ(bus_main()[0xE000], 0x00);
}

static void test_read_out_of_range(void)
{
    cpu_regs_t r;

    power_on();
    prepare_call(0x01, 0x4000, IMAGE_BLOCKS);
    blockdev_call();
    cpu_get_regs(&r);
    CHECK_EQ(r.a, BLOCKDEV_ERR_IO);
    CHECK_EQ(r.status & CPU_FLAG_CARRY, CPU_FLAG_CARRY);
    CHECK_EQ(r.pc, CALL_ADDR + 6);
    CHECK_EQ(bus_main()[0x4000], 0x00);

    /* The block number is 24 bits. */
    prepare_call(0x01, 0x4000, 0x010001);
    blockdev_call();
    cpu_get_regs(&r);
    CHECK_EQ(r.a, BLOCKDEV_ERR_IO);
}

static void test_write_is_protected(void)
{
    cpu_regs_t r;

    power_on();
    bus_main()[0x4000] = 0x99;
    prepare_call(0x02, 0x4000, 1);
    blockdev_call();
    cpu_get_regs(&r);
    CHECK_EQ(r.a, BLOCKDEV_ERR_WRITE_PROTECTED);
    CHECK_EQ(r.status & CPU_FLAG_CARRY, CPU_FLAG_CARRY);
    CHECK_EQ(r.pc, CALL_ADDR + 6);
    CHECK_EQ(image[512], (uint8_t)((512 * 7 + 1) & 0xFF)); /* image untouched */
}

static void test_unknown_command(void)
{
    cpu_regs_t r;

    power_on();
    prepare_call(0x00, 0x4000, 1);
    blockdev_call();
    cpu_get_regs(&r);
    CHECK_EQ(r.a, BLOCKDEV_ERR_BAD_COMMAND);
    CHECK_EQ(r.status & CPU_FLAG_CARRY, CPU_FLAG_CARRY);
    CHECK_EQ(r.pc, CALL_ADDR + 6);
}

static void test_no_image(void)
{
    cpu_regs_t r;

    power_on();
    blockdev_init(NULL, 0);
    prepare_call(0x01, 0x4000, 0);
    blockdev_call();
    cpu_get_regs(&r);
    CHECK_EQ(r.a, BLOCKDEV_ERR_IO);
    CHECK_EQ(r.status & CPU_FLAG_CARRY, CPU_FLAG_CARRY);
}

int main(void)
{
    make_image();
    test_read_block();
    test_read_first_and_last_block();
    test_read_follows_banking();
    test_read_out_of_range();
    test_write_is_protected();
    test_unknown_command();
    test_no_image();
    return TEST_RESULT();
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make build/test_blockdev`

Expected: make stops with `No rule to make target` naming `core/blockdev.c` (needed by `build/test_blockdev`).

- [ ] **Step 3: Write the implementation**

`core/blockdev.h`:

```c
#ifndef BLOCKDEV_H
#define BLOCKDEV_H

#include <stddef.h>
#include <stdint.h>

#define BLOCKDEV_BLOCK_SIZE 512u

/* Cycles charged to the emulated machine for one SmartPort call. */
#define BLOCKDEV_CALL_CYCLES 1000u

#define BLOCKDEV_OK 0x00
#define BLOCKDEV_ERR_BAD_COMMAND 0x01
#define BLOCKDEV_ERR_IO 0x27
#define BLOCKDEV_ERR_WRITE_PROTECTED 0x2B

/* The image must stay valid for as long as the machine runs. */
void blockdev_init(const uint8_t *image, size_t image_len);

/* Service a SmartPort call. Call this instead of stepping the CPU when the
   program counter is ROM_SMARTPORT_ENTRY. Reads the inline parameters after
   the caller's JSR, performs the command, sets A and carry, and resumes the
   caller after the parameters. */
void blockdev_call(void);

#endif
```

`core/blockdev.c`:

```c
#include "blockdev.h"

#include "bus.h"
#include "cpu.h"

#define CMD_READ_BLOCK 0x01
#define CMD_WRITE_BLOCK 0x02

static const uint8_t *image;
static size_t image_len;

void blockdev_init(const uint8_t *data, size_t len)
{
    image = data;
    image_len = len;
}

static uint16_t read16(uint16_t addr)
{
    return (uint16_t)(bus_read(addr) | (bus_read((uint16_t)(addr + 1)) << 8));
}

static uint8_t read_block(uint16_t list)
{
    uint16_t buffer = read16((uint16_t)(list + 2));
    uint32_t block = (uint32_t)bus_read((uint16_t)(list + 4))
                   | ((uint32_t)bus_read((uint16_t)(list + 5)) << 8)
                   | ((uint32_t)bus_read((uint16_t)(list + 6)) << 16);
    size_t offset = (size_t)block * BLOCKDEV_BLOCK_SIZE;
    uint32_t i;

    if (image == NULL || offset + BLOCKDEV_BLOCK_SIZE > image_len) {
        return BLOCKDEV_ERR_IO;
    }
    /* Through the bus, so the caller's banking decides where it lands. */
    for (i = 0; i < BLOCKDEV_BLOCK_SIZE; i++) {
        bus_write((uint16_t)(buffer + i), image[offset + i]);
    }
    return BLOCKDEV_OK;
}

void blockdev_call(void)
{
    cpu_regs_t r;
    uint16_t ret;
    uint8_t command;
    uint16_t list;
    uint8_t result;

    cpu_get_regs(&r);

    /* Pull the JSR return address. It points at the last byte of the JSR,
       so the inline parameters start one byte after it. */
    r.sp++;
    ret = bus_read((uint16_t)(0x0100 + r.sp));
    r.sp++;
    ret |= (uint16_t)(bus_read((uint16_t)(0x0100 + r.sp)) << 8);

    command = bus_read((uint16_t)(ret + 1));
    list = read16((uint16_t)(ret + 2));

    switch (command) {
    case CMD_READ_BLOCK:
        result = read_block(list);
        break;
    case CMD_WRITE_BLOCK:
        result = BLOCKDEV_ERR_WRITE_PROTECTED;
        break;
    default:
        result = BLOCKDEV_ERR_BAD_COMMAND;
        break;
    }

    r.a = result;
    if (result == BLOCKDEV_OK) {
        r.status &= (uint8_t)~CPU_FLAG_CARRY;
    } else {
        r.status |= CPU_FLAG_CARRY;
    }
    r.pc = (uint16_t)(ret + 4); /* skip command byte and list pointer */
    cpu_set_regs(&r);
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `make build/test_blockdev && build/test_blockdev`

Expected: no warnings, then `ok`.

- [ ] **Step 5: Commit**

```bash
git add core/blockdev.h core/blockdev.c tests/test_blockdev.c
git commit -m "Add SmartPort block device trap"
```

---

### Task 6: Video renderer

**Files:**
- Create: `core/video.h`, `core/video.c`
- Test: `tests/test_video.c`

**Interfaces:**
- Consumes: `bus_main`, `bus_aux`, `bus_init`, `bus_read`, `bus_write` (Task 2); `softswitch_state`, `softswitch_reset`, `softswitch_state_t` (Task 3).
- Produces:
  - `VIDEO_WIDTH` (560), `VIDEO_HEIGHT` (384), `VIDEO_ROW_BYTES` (70), `VIDEO_BUFFER_SIZE` (26880)
  - `video_mode_t { VIDEO_MODE_BLANK, VIDEO_MODE_HIRES, VIDEO_MODE_DHIRES }`
  - `video_mode_t video_mode(void)`
  - `void video_render(uint8_t *out)`: 384 rows of 70 bytes, most significant bit leftmost, 1 = lit.

Rules to know:
- Blank when TEXT is on or HIRES is off. Double hi-res when HIRES, 80COL and DHIRES are all on and TEXT is off. Otherwise hi-res. MIXED is ignored.
- Displayed page: `$4000` when PAGE2 is on and 80STORE is off, otherwise `$2000`.
- Hi-res row `y` starts at page + `((y & 7) << 10) + (((y >> 3) & 7) << 7) + (y >> 6) * 40`.
- Hi-res byte: bits 0–6 are seven pixels, bit 0 leftmost, each two dots wide. If bit 7 is set, the byte's dots shift right by one; the vacated first dot repeats the previous byte's last dot.
- Double hi-res: per column, the aux byte gives dots 0–6 and the main byte dots 7–13, bit 0 leftmost, bit 7 ignored.
- Every Apple row is output twice.

- [ ] **Step 1: Write the failing test**

`tests/test_video.c`:

```c
#include <string.h>

#include "bus.h"
#include "softswitch.h"
#include "test.h"
#include "video.h"

static uint8_t out[VIDEO_BUFFER_SIZE];

static void power_on(void)
{
    bus_init();
    softswitch_reset();
}

static void hires_on(void)
{
    power_on();
    (void)bus_read(0xC050); /* TEXT off */
    (void)bus_read(0xC057); /* HIRES on */
}

static void dhires_on(void)
{
    hires_on();
    bus_write(0xC00D, 0);   /* 80COL on */
    (void)bus_read(0xC05E); /* DHIRES on */
}

static long lit_dots(void)
{
    long count = 0;
    size_t i;
    int bit;

    for (i = 0; i < sizeof out; i++) {
        for (bit = 0; bit < 8; bit++) {
            count += (out[i] >> bit) & 1;
        }
    }
    return count;
}

static const uint8_t *row(int output_row)
{
    return out + output_row * VIDEO_ROW_BYTES;
}

static void test_mode_selection(void)
{
    power_on();
    CHECK_EQ(video_mode(), VIDEO_MODE_BLANK); /* text mode */

    (void)bus_read(0xC050);
    CHECK_EQ(video_mode(), VIDEO_MODE_BLANK); /* graphics but lo-res */

    (void)bus_read(0xC057);
    CHECK_EQ(video_mode(), VIDEO_MODE_HIRES);

    (void)bus_read(0xC05E);
    CHECK_EQ(video_mode(), VIDEO_MODE_HIRES); /* DHIRES needs 80COL too */

    bus_write(0xC00D, 0);
    CHECK_EQ(video_mode(), VIDEO_MODE_DHIRES);

    (void)bus_read(0xC051);
    CHECK_EQ(video_mode(), VIDEO_MODE_BLANK); /* TEXT wins */
}

static void test_blank_clears_buffer(void)
{
    power_on();
    bus_main()[0x2000] = 0x7F;
    memset(out, 0xFF, sizeof out);
    video_render(out);
    CHECK_EQ(lit_dots(), 0);
}

static void test_hires_pixels(void)
{
    /* Pixel 0 -> dots 0 and 1, on both doubled rows. */
    hires_on();
    bus_main()[0x2000] = 0x01;
    video_render(out);
    CHECK_EQ(row(0)[0], 0xC0);
    CHECK_EQ(row(1)[0], 0xC0);
    CHECK_EQ(lit_dots(), 4);

    /* Pixel 6 -> dots 12 and 13. */
    hires_on();
    bus_main()[0x2000] = 0x40;
    video_render(out);
    CHECK_EQ(row(0)[0], 0x00);
    CHECK_EQ(row(0)[1], 0x0C);
    CHECK_EQ(lit_dots(), 4);

    /* All seven pixels -> dots 0..13. */
    hires_on();
    bus_main()[0x2000] = 0x7F;
    video_render(out);
    CHECK_EQ(row(0)[0], 0xFF);
    CHECK_EQ(row(0)[1], 0xFC);

    /* Second column starts at dot 14. */
    hires_on();
    bus_main()[0x2001] = 0x01;
    video_render(out);
    CHECK_EQ(row(0)[1], 0x03);
    CHECK_EQ(lit_dots(), 4);
}

static void test_hires_half_dot_shift(void)
{
    /* Bit 7 set: pixel 0 moves to dots 1 and 2. */
    hires_on();
    bus_main()[0x2000] = 0x81;
    video_render(out);
    CHECK_EQ(row(0)[0], 0x60);
    CHECK_EQ(lit_dots(), 4);

    /* Shifted pixel 6 is dots 13 and 14, but an unshifted next byte owns
       dot 14, so only dot 13 survives. */
    hires_on();
    bus_main()[0x2000] = 0xC0;
    bus_main()[0x2001] = 0x00;
    video_render(out);
    CHECK_EQ(row(0)[1], 0x04);
    CHECK_EQ(lit_dots(), 2);

    /* A shifted next byte keeps dot 14: it repeats the previous last dot. */
    hires_on();
    bus_main()[0x2000] = 0xC0;
    bus_main()[0x2001] = 0x80;
    video_render(out);
    CHECK_EQ(row(0)[1], 0x06);
    CHECK_EQ(lit_dots(), 4);

    /* Unshifted pixel 6 followed by a shifted byte: dots 12, 13 and 14. */
    hires_on();
    bus_main()[0x2000] = 0x40;
    bus_main()[0x2001] = 0x80;
    video_render(out);
    CHECK_EQ(row(0)[1], 0x0E);
    CHECK_EQ(lit_dots(), 6);

    /* First column shifted with nothing before it: dot 0 stays off. */
    hires_on();
    bus_main()[0x2000] = 0xFF;
    video_render(out);
    CHECK_EQ(row(0)[0], 0x7F);

    /* Last column shifted: the dot pushed past the right edge is dropped. */
    hires_on();
    bus_main()[0x2027] = 0xC0;
    video_render(out);
    CHECK_EQ(row(0)[69], 0x01);
    CHECK_EQ(lit_dots(), 2);
}

static void test_hires_row_addresses(void)
{
    hires_on();
    bus_main()[0x2400] = 0x7F; /* row 1 */
    bus_main()[0x2080] = 0x7F; /* row 8 */
    bus_main()[0x2028] = 0x7F; /* row 64 */
    bus_main()[0x3FD0] = 0x7F; /* row 191 */
    video_render(out);
    CHECK_EQ(row(0)[0], 0x00);
    CHECK_EQ(row(2)[0], 0xFF);
    CHECK_EQ(row(3)[0], 0xFF);
    CHECK_EQ(row(4)[0], 0x00);
    CHECK_EQ(row(16)[0], 0xFF);
    CHECK_EQ(row(17)[0], 0xFF);
    CHECK_EQ(row(128)[0], 0xFF);
    CHECK_EQ(row(129)[0], 0xFF);
    CHECK_EQ(row(382)[0], 0xFF);
    CHECK_EQ(row(383)[0], 0xFF);
    CHECK_EQ(lit_dots(), 4 * 2 * 14);
}

static void test_page_selection(void)
{
    /* PAGE2 shows $4000. */
    hires_on();
    bus_main()[0x2000] = 0x01;
    bus_main()[0x4000] = 0x40;
    (void)bus_read(0xC055);
    video_render(out);
    CHECK_EQ(row(0)[0], 0x00);
    CHECK_EQ(row(0)[1], 0x0C);

    (void)bus_read(0xC054);
    video_render(out);
    CHECK_EQ(row(0)[0], 0xC0);
    CHECK_EQ(row(0)[1], 0x00);

    /* With 80STORE on, PAGE2 selects memory, not the displayed page. */
    bus_write(0xC001, 0);
    (void)bus_read(0xC055);
    video_render(out);
    CHECK_EQ(row(0)[0], 0xC0);
    CHECK_EQ(row(0)[1], 0x00);
}

static void test_mixed_is_ignored(void)
{
    hires_on();
    (void)bus_read(0xC053);
    bus_main()[0x2250] = 0x7F; /* row 160, inside the mixed-mode text area */
    video_render(out);
    CHECK_EQ(row(320)[0], 0xFF);
}

static void test_dhires(void)
{
    /* Aux byte is dots 0..6, main byte dots 7..13, one dot per bit. */
    dhires_on();
    bus_aux()[0x2000] = 0x01;
    video_render(out);
    CHECK_EQ(row(0)[0], 0x80);
    CHECK_EQ(row(1)[0], 0x80);
    CHECK_EQ(lit_dots(), 2);

    dhires_on();
    bus_main()[0x2000] = 0x01;
    video_render(out);
    CHECK_EQ(row(0)[0], 0x01);

    dhires_on();
    bus_aux()[0x2000] = 0x40;
    bus_main()[0x2000] = 0x40;
    video_render(out);
    CHECK_EQ(row(0)[0], 0x02);
    CHECK_EQ(row(0)[1], 0x04);

    /* Bit 7 is ignored in double hi-res. */
    dhires_on();
    bus_aux()[0x2000] = 0x80;
    bus_main()[0x2000] = 0x80;
    video_render(out);
    CHECK_EQ(lit_dots(), 0);

    /* Last column, last row. */
    dhires_on();
    bus_main()[0x3FF7] = 0x40;
    video_render(out);
    CHECK_EQ(row(383)[69], 0x01);
    CHECK_EQ(lit_dots(), 2);

    /* Page 2 in double hi-res. */
    dhires_on();
    (void)bus_read(0xC055);
    bus_aux()[0x4000] = 0x01;
    video_render(out);
    CHECK_EQ(row(0)[0], 0x80);
}

int main(void)
{
    test_mode_selection();
    test_blank_clears_buffer();
    test_hires_pixels();
    test_hires_half_dot_shift();
    test_hires_row_addresses();
    test_page_selection();
    test_mixed_is_ignored();
    test_dhires();
    return TEST_RESULT();
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make build/test_video`

Expected: make stops with `No rule to make target` naming `core/video.c` (needed by `build/test_video`).

- [ ] **Step 3: Write the implementation**

`core/video.h`:

```c
#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>

#define VIDEO_WIDTH 560
#define VIDEO_HEIGHT 384
#define VIDEO_ROW_BYTES 70
#define VIDEO_BUFFER_SIZE (VIDEO_ROW_BYTES * VIDEO_HEIGHT) /* 26880 */

typedef enum { VIDEO_MODE_BLANK, VIDEO_MODE_HIRES, VIDEO_MODE_DHIRES } video_mode_t;

/* Which mode the current soft-switch state selects. */
video_mode_t video_mode(void);

/* Render the visible page into out (VIDEO_BUFFER_SIZE bytes): 384 rows of
   70 bytes, most significant bit leftmost, 1 = lit dot. */
void video_render(uint8_t *out);

#endif
```

`core/video.c`:

```c
#include "video.h"

#include <string.h>

#include "bus.h"
#include "softswitch.h"

#define APPLE_ROWS 192
#define APPLE_COLS 40

/* Address of hi-res row y relative to the start of its page. */
static uint16_t row_offset(int y)
{
    return (uint16_t)(((y & 0x07) << 10) | (((y >> 3) & 0x07) << 7)) + (uint16_t)((y >> 6) * 40);
}

video_mode_t video_mode(void)
{
    const softswitch_state_t *s = softswitch_state();

    if (s->text || !s->hires) {
        return VIDEO_MODE_BLANK;
    }
    if (s->col80 && s->dhires) {
        return VIDEO_MODE_DHIRES;
    }
    return VIDEO_MODE_HIRES;
}

/* One hi-res row: 40 bytes -> 560 dots (one byte per dot, 0 or 1). Each of
   the 7 pixels in a byte is two dots wide. Bit 7 delays the byte by one dot. */
static void hires_row(const uint8_t *src, uint8_t *dots)
{
    uint8_t prev_last = 0;
    int col;
    int bit;

    for (col = 0; col < APPLE_COLS; col++) {
        uint8_t b = src[col];
        uint8_t *d = dots + col * 14;

        if (b & 0x80) {
            d[0] = prev_last;
            for (bit = 0; bit < 7; bit++) {
                uint8_t on = (uint8_t)((b >> bit) & 1);
                d[1 + bit * 2] = on;
                /* The 14th dot spills into the next byte's first dot; the
                   scratch row has one spare dot so the last column is safe. */
                d[2 + bit * 2] = on;
            }
        } else {
            for (bit = 0; bit < 7; bit++) {
                uint8_t on = (uint8_t)((b >> bit) & 1);
                d[bit * 2] = on;
                d[bit * 2 + 1] = on;
            }
        }
        prev_last = (uint8_t)((b >> 6) & 1);
    }
}

/* One double hi-res row: aux byte then main byte per column, 7 dots each. */
static void dhires_row(const uint8_t *aux, const uint8_t *main, uint8_t *dots)
{
    int col;
    int bit;

    for (col = 0; col < APPLE_COLS; col++) {
        uint8_t *d = dots + col * 14;
        for (bit = 0; bit < 7; bit++) {
            d[bit] = (uint8_t)((aux[col] >> bit) & 1);
            d[7 + bit] = (uint8_t)((main[col] >> bit) & 1);
        }
    }
}

static void pack_row(const uint8_t *dots, uint8_t *out)
{
    int i;
    int bit;

    for (i = 0; i < VIDEO_ROW_BYTES; i++) {
        uint8_t packed = 0;
        for (bit = 0; bit < 8; bit++) {
            packed = (uint8_t)((packed << 1) | dots[i * 8 + bit]);
        }
        out[i] = packed;
    }
}

void video_render(uint8_t *out)
{
    const softswitch_state_t *s = softswitch_state();
    video_mode_t mode = video_mode();
    uint16_t page;
    uint8_t dots[VIDEO_WIDTH + 1];
    int y;

    if (mode == VIDEO_MODE_BLANK) {
        memset(out, 0, VIDEO_BUFFER_SIZE);
        return;
    }

    page = (!s->store80 && s->page2) ? 0x4000 : 0x2000;

    for (y = 0; y < APPLE_ROWS; y++) {
        uint16_t addr = (uint16_t)(page + row_offset(y));
        uint8_t *row = out + (y * 2) * VIDEO_ROW_BYTES;

        if (mode == VIDEO_MODE_DHIRES) {
            dhires_row(bus_aux() + addr, bus_main() + addr, dots);
        } else {
            hires_row(bus_main() + addr, dots);
        }
        pack_row(dots, row);
        memcpy(row + VIDEO_ROW_BYTES, row, VIDEO_ROW_BYTES);
    }
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `make build/test_video && build/test_video`

Expected: no warnings, then `ok`.

- [ ] **Step 5: Commit**

```bash
git add core/video.h core/video.c tests/test_video.c
git commit -m "Add hi-res and double hi-res renderer"
```

---

### Task 7: Boot and machine loop

**Files:**
- Create: `core/boot.h`, `core/boot.c`
- Create: `core/machine.h`, `core/machine.c`
- Test: `tests/test_machine.c`

**Interfaces:**
- Consumes: everything from Tasks 2–6: `bus_init`, `bus_read`, `bus_main`; `softswitch_reset`, `softswitch_set_frame_cycle`, `softswitch_state`; `cpu_pc`, `cpu_step`, `cpu_set_regs`, `cpu_get_regs`, `cpu_opcode_is_documented`; `blockdev_init`, `blockdev_call`, `BLOCKDEV_CALL_CYCLES`; `video_render`, `video_mode`; `ROM_SMARTPORT_ENTRY`.
- Produces:
  - `const char *boot_load(const uint8_t *image, size_t image_len)`: NULL on success, otherwise a static message. `BOOT_IMAGE_SIZE` (819200).
  - The public API in `machine.h`, usable from C and C++:
    - `machine_status_t { MACHINE_RUNNING, MACHINE_HALTED }`
    - `void machine_init(const uint8_t *image, size_t image_len)`
    - `machine_status_t machine_run_frame(void)`
    - `void machine_render(uint8_t *out)`
    - `const char *machine_halt_reason(void)`
    - `uint64_t machine_cycles(void)`
    - `const char *machine_video_mode_name(void)`: `"blank"`, `"hires"` or `"dhires"`
    - `MACHINE_VIDEO_WIDTH` (560), `MACHINE_VIDEO_HEIGHT` (384), `MACHINE_VIDEO_BUFFER_SIZE` (26880), `MACHINE_CYCLES_PER_FRAME` (17030)

Behaviour to know:
- Boot skips the ProDOS boot block. It copies image blocks 8–15 (4,096 bytes) to `$2000`, sets zero-page `$43` to `$50`, and starts at `$2000` with S = `$FF`, X = `$50`, interrupts disabled.
- The frame loop steps one instruction at a time. Before each, it sets the frame cycle for `$C019`, then: if the program counter is `$C583` it calls `blockdev_call()`; if the next opcode is `BRK` (`$00`) or undocumented it halts; otherwise it steps the CPU.
- A frame ends when 17,030 cycles have been used. The overshoot carries into the next frame.
- Once halted, `machine_run_frame` does nothing. `machine_init` always starts fresh.

These tests need no game image. They boot tiny hand-assembled programs placed where the boot file would be.

- [ ] **Step 1: Write the failing test**

`tests/test_machine.c`:

```c
/* Machine tests that need no game image: they boot tiny synthetic images. */
#include <stdlib.h>
#include <string.h>

#include "bus.h"
#include "cpu.h"
#include "machine.h"
#include "softswitch.h"
#include "test.h"

#define IMAGE_SIZE 819200
#define BOOT_OFFSET (8 * 512)

static uint8_t *image;

/* A valid-looking image whose boot file is the given code, loaded at $2000. */
static void make_image(const uint8_t *code, size_t len)
{
    memset(image, 0, IMAGE_SIZE);
    memcpy(image + BOOT_OFFSET, code, len);
}

static void test_boot_state(void)
{
    /* BIT $0000 / JMP $2003 (spin) */
    static const uint8_t code[] = { 0x2C, 0x00, 0x00, 0x4C, 0x03, 0x20 };
    cpu_regs_t r;

    make_image(code, sizeof code);
    image[BOOT_OFFSET + 8 * 512 - 1] = 0xAB; /* last byte of the boot file */
    image[BOOT_OFFSET + 8 * 512] = 0xCD;     /* first byte after it */

    machine_init(image, IMAGE_SIZE);
    cpu_get_regs(&r);
    CHECK_EQ(r.pc, 0x2000);
    CHECK_EQ(r.sp, 0xFF);
    CHECK_EQ(r.x, 0x50);
    CHECK_EQ(r.status & CPU_FLAG_INTERRUPT, CPU_FLAG_INTERRUPT);
    CHECK_EQ(bus_main()[0x43], 0x50);
    CHECK_EQ(bus_main()[0x2000], 0x2C);
    CHECK_EQ(bus_main()[0x2FFF], 0xAB);
    CHECK_EQ(bus_main()[0x3000], 0x00); /* only 4 KB is loaded */
    CHECK(softswitch_state()->text);
    CHECK_EQ(machine_cycles(), 0);
    CHECK_EQ(machine_halt_reason()[0], '\0');
    CHECK(strcmp(machine_video_mode_name(), "blank") == 0);
}

static void test_run_frame_counts_cycles(void)
{
    static const uint8_t code[] = { 0x2C, 0x00, 0x00, 0x4C, 0x03, 0x20 };
    uint64_t after_one;

    make_image(code, sizeof code);
    machine_init(image, IMAGE_SIZE);

    CHECK_EQ(machine_run_frame(), MACHINE_RUNNING);
    after_one = machine_cycles();
    /* A frame is 17,030 cycles; the last instruction may overshoot by a few. */
    CHECK(after_one >= 17030 && after_one < 17030 + 7);

    /* The overshoot is carried, so 100 frames stay within one instruction
       of 100 * 17,030. */
    {
        int i;
        for (i = 1; i < 100; i++) {
            machine_run_frame();
        }
    }
    CHECK(machine_cycles() >= 1703000 && machine_cycles() < 1703000 + 7);
}

static void test_vertical_blank_is_visible_to_the_program(void)
{
    /* Wait for vertical blank the way the game does, then store a marker:
     *   2000 BIT $0000
     *   2003 LDA $C019 / BPL $2003   wait while in vertical blank
     *   2008 LDA $C019 / BMI $2008   wait for vertical blank to begin
     *   200D INC $0300
     *   2010 JMP $2003
     */
    static const uint8_t code[] = {
        0x2C, 0x00, 0x00,
        0xAD, 0x19, 0xC0, 0x10, 0xFB,
        0xAD, 0x19, 0xC0, 0x30, 0xFB,
        0xEE, 0x00, 0x03,
        0x4C, 0x03, 0x20,
    };
    int i;

    make_image(code, sizeof code);
    machine_init(image, IMAGE_SIZE);
    for (i = 0; i < 10; i++) {
        machine_run_frame();
    }
    /* Exactly one vertical blank per frame. */
    CHECK_EQ(bus_main()[0x0300], 10);
}

static void test_halts_on_brk(void)
{
    static const uint8_t code[] = { 0x2C, 0x00, 0x00, 0x00 };
    uint64_t cycles;

    make_image(code, sizeof code);
    machine_init(image, IMAGE_SIZE);
    CHECK_EQ(machine_run_frame(), MACHINE_HALTED);
    CHECK(strstr(machine_halt_reason(), "BRK") != NULL);
    CHECK(strstr(machine_halt_reason(), "pc=$2003") != NULL);
    CHECK(strstr(machine_halt_reason(), "opcode=$00") != NULL);
    CHECK(strstr(machine_halt_reason(), "ramrd=0") != NULL);

    /* Once halted, running does nothing. */
    cycles = machine_cycles();
    CHECK_EQ(machine_run_frame(), MACHINE_HALTED);
    CHECK_EQ(machine_cycles(), cycles);
    CHECK_EQ(cpu_pc(), 0x2003);
}

static void test_halts_on_illegal_opcode(void)
{
    static const uint8_t code[] = { 0x2C, 0x00, 0x00, 0xEA, 0x02 };

    make_image(code, sizeof code);
    machine_init(image, IMAGE_SIZE);
    CHECK_EQ(machine_run_frame(), MACHINE_HALTED);
    CHECK(strstr(machine_halt_reason(), "illegal opcode") != NULL);
    CHECK(strstr(machine_halt_reason(), "pc=$2004") != NULL);
    CHECK(strstr(machine_halt_reason(), "opcode=$02") != NULL);
}

static void test_rejects_bad_images(void)
{
    static const uint8_t code[] = { 0x2C, 0x00, 0x00, 0x4C, 0x03, 0x20 };

    make_image(code, sizeof code);

    machine_init(image, IMAGE_SIZE - 1);
    CHECK_EQ(machine_run_frame(), MACHINE_HALTED);
    CHECK(strstr(machine_halt_reason(), "819200") != NULL);

    machine_init(image, 0);
    CHECK_EQ(machine_run_frame(), MACHINE_HALTED);

    machine_init(NULL, 0);
    CHECK_EQ(machine_run_frame(), MACHINE_HALTED);
    CHECK(strstr(machine_halt_reason(), "no disk image") != NULL);

    image[BOOT_OFFSET] = 0x00;
    machine_init(image, IMAGE_SIZE);
    CHECK_EQ(machine_run_frame(), MACHINE_HALTED);
    CHECK(strstr(machine_halt_reason(), "boot file") != NULL);
    CHECK_EQ(machine_cycles(), 0);
}

static void test_init_recovers_from_halt(void)
{
    static const uint8_t bad[] = { 0x2C, 0x00, 0x00, 0x00 };
    static const uint8_t good[] = { 0x2C, 0x00, 0x00, 0x4C, 0x03, 0x20 };

    make_image(bad, sizeof bad);
    machine_init(image, IMAGE_SIZE);
    CHECK_EQ(machine_run_frame(), MACHINE_HALTED);

    make_image(good, sizeof good);
    machine_init(image, IMAGE_SIZE);
    CHECK_EQ(machine_run_frame(), MACHINE_RUNNING);
    CHECK_EQ(machine_halt_reason()[0], '\0');
}

static void test_smartport_trap_from_program(void)
{
    /* Read block 20 to $4000 through the fake card, then spin:
     *   2000 BIT $0000
     *   2003 JSR $C583 / db $01 / dw $2010
     *   2009 STA $0300        result code
     *   200C JMP $200C
     *   2010 db 3, 1 / dw $4000 / db 20, 0, 0
     */
    static const uint8_t code[] = {
        0x2C, 0x00, 0x00,
        0x20, 0x83, 0xC5, 0x01, 0x10, 0x20,
        0x8D, 0x00, 0x03,
        0x4C, 0x0C, 0x20,
        0x00,
        0x03, 0x01, 0x00, 0x40, 0x14, 0x00, 0x00,
    };

    make_image(code, sizeof code);
    memset(image + 20 * 512, 0x5A, 512);
    bus_init();
    machine_init(image, IMAGE_SIZE);
    bus_main()[0x0300] = 0xFF;

    CHECK_EQ(machine_run_frame(), MACHINE_RUNNING);
    CHECK_EQ(bus_main()[0x0300], 0x00);
    CHECK_EQ(bus_main()[0x4000], 0x5A);
    CHECK_EQ(bus_main()[0x41FF], 0x5A);
    CHECK_EQ(bus_main()[0x4200], 0x00);
}

int main(void)
{
    image = malloc(IMAGE_SIZE);
    if (image == NULL) {
        printf("FAIL out of memory\n");
        return 1;
    }
    test_boot_state();
    test_run_frame_counts_cycles();
    test_vertical_blank_is_visible_to_the_program();
    test_halts_on_brk();
    test_halts_on_illegal_opcode();
    test_rejects_bad_images();
    test_init_recovers_from_halt();
    test_smartport_trap_from_program();
    free(image);
    return TEST_RESULT();
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make build/test_machine`

Expected: make stops with `No rule to make target` naming `core/boot.c` (needed by `build/test_machine`).

- [ ] **Step 3: Write the boot loader**

`core/boot.h`:

```c
#ifndef BOOT_H
#define BOOT_H

#include <stddef.h>
#include <stdint.h>

#define BOOT_IMAGE_SIZE 819200u

/* Put the machine in the state the ProDOS boot block would leave: the game's
   4 KB boot file (image blocks 8-15) at $2000, boot unit in $43, PC = $2000.
   Call after bus_init() and softswitch_reset().
   Returns NULL on success, or a static message describing why the image was
   rejected. */
const char *boot_load(const uint8_t *image, size_t image_len);

#endif
```

`core/boot.c`:

```c
#include "boot.h"

#include <string.h>

#include "bus.h"
#include "cpu.h"

#define BOOT_FILE_OFFSET (8u * 512u)
#define BOOT_FILE_SIZE (8u * 512u)
#define BOOT_LOAD_ADDR 0x2000
#define BOOT_UNIT 0x50 /* slot 5, drive 1 */

const char *boot_load(const uint8_t *image, size_t image_len)
{
    cpu_regs_t r;

    if (image == NULL) {
        return "no disk image";
    }
    if (image_len != BOOT_IMAGE_SIZE) {
        return "disk image is not 819200 bytes";
    }
    /* POPBOOT35 starts with BIT abs ($2C). */
    if (image[BOOT_FILE_OFFSET] != 0x2C) {
        return "disk image has no boot file at block 8";
    }

    memcpy(bus_main() + BOOT_LOAD_ADDR, image + BOOT_FILE_OFFSET, BOOT_FILE_SIZE);
    bus_main()[0x43] = BOOT_UNIT;

    r.pc = BOOT_LOAD_ADDR;
    r.a = 0x00;
    r.x = BOOT_UNIT;
    r.y = 0x00;
    r.sp = 0xFF;
    r.status = CPU_FLAG_INTERRUPT | CPU_FLAG_CONSTANT;
    cpu_set_regs(&r);
    return NULL;
}
```

- [ ] **Step 4: Write the machine**

`core/machine.h`:

```c
#ifndef MACHINE_H
#define MACHINE_H

#include <stddef.h>
#include <stdint.h>

#define MACHINE_VIDEO_WIDTH 560
#define MACHINE_VIDEO_HEIGHT 384
#define MACHINE_VIDEO_BUFFER_SIZE 26880
#define MACHINE_CYCLES_PER_FRAME 17030u

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { MACHINE_RUNNING, MACHINE_HALTED } machine_status_t;

/* Reset the machine and boot the game from a 819,200-byte disk image. The
   image is read in place and must outlive the machine. */
void machine_init(const uint8_t *image, size_t image_len);

/* Run one video frame (17,030 CPU cycles). Does nothing once halted. */
machine_status_t machine_run_frame(void);

/* Render the visible page: 384 rows of 70 bytes, most significant bit
   leftmost, 1 = lit (white) dot. out must hold MACHINE_VIDEO_BUFFER_SIZE. */
void machine_render(uint8_t *out);

/* Why the machine halted. Empty string while running. */
const char *machine_halt_reason(void);

/* Total emulated CPU cycles since machine_init. */
uint64_t machine_cycles(void);

/* "blank", "hires" or "dhires": what machine_render currently draws. */
const char *machine_video_mode_name(void);

#ifdef __cplusplus
}
#endif

#endif
```

`core/machine.c`:

```c
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
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `make build/test_machine && build/test_machine`

Expected: no warnings, then `ok`.

- [ ] **Step 6: Commit**

```bash
git add core/boot.h core/boot.c core/machine.h core/machine.c tests/test_machine.c
git commit -m "Add boot loader and machine frame loop"
```

---

### Task 8: Headless runner, boot smoke test and golden frames

**Files:**
- Create: `host/headless.c`
- Create: `tools/pbm2png.py`
- Create: `tests/golden.txt`
- Test: `tests/test_boot.c`

**Interfaces:**
- Consumes: the `machine.h` API from Task 7; `game/out/PrinceOfPersia_3.5.hdv` from Task 1.
- Produces:
  - `build/pop-headless <image> --frames N [--dump K:path.pbm]... [--hash K]...`. Frame numbers count from 1. Prints `frame K hash <16 hex digits>` for each `--hash`, and a final `frames N cycles C mode M` line. Exit 0 on success, 1 if the machine halts (reason on stderr), 2 on a usage or file error.
  - `tools/pbm2png.py file.pbm...`: writes `file.png` next to each input.
  - `tests/golden.txt`: lines of `<frame> <hash>`, increasing frame order, `#` comments.
  - `make test` now runs all seven test programs; `make soak` runs the long check.

The picture hash is FNV-1a, 64-bit, over the 26,880 picture bytes: start at `0xcbf29ce484222325`, and for each byte XOR it in and multiply by `0x100000001b3`.

- [ ] **Step 1: Write the failing test**

`tests/test_boot.c`:

```c
/* Tests against the real game image.
 *
 *   test_boot <image> <golden.txt>
 *
 * Skips (exit 0) with a message when the image has not been built.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"
#include "test.h"

#define IMAGE_SIZE 819200
#define SMOKE_MAX_FRAMES 3000
#define SMOKE_MIN_LIT_DOTS 1000
#define MAX_GOLDEN 16

static uint8_t picture[MACHINE_VIDEO_BUFFER_SIZE];

static uint64_t fnv1a64(const uint8_t *data, size_t len)
{
    uint64_t hash = 0xcbf29ce484222325ULL;
    size_t i;

    for (i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

static long lit_dots(void)
{
    long count = 0;
    size_t i;
    int bit;

    for (i = 0; i < sizeof picture; i++) {
        for (bit = 0; bit < 8; bit++) {
            count += (picture[i] >> bit) & 1;
        }
    }
    return count;
}

static void test_smoke(const uint8_t *image)
{
    long frame;
    int reached = 0;

    machine_init(image, IMAGE_SIZE);
    for (frame = 1; frame <= SMOKE_MAX_FRAMES && !reached; frame++) {
        if (machine_run_frame() == MACHINE_HALTED) {
            printf("FAIL halted at frame %ld: %s\n", frame, machine_halt_reason());
            test_failures++;
            return;
        }
        if (strcmp(machine_video_mode_name(), "blank") != 0) {
            machine_render(picture);
            reached = lit_dots() >= SMOKE_MIN_LIT_DOTS;
        }
    }
    CHECK(reached);
}

static void test_golden(const uint8_t *image, const char *golden_path)
{
    long frames[MAX_GOLDEN];
    uint64_t hashes[MAX_GOLDEN];
    int count = 0;
    char line[128];
    FILE *f = fopen(golden_path, "r");
    long frame = 0;
    int i;

    if (f == NULL) {
        printf("FAIL cannot open %s\n", golden_path);
        test_failures++;
        return;
    }
    while (fgets(line, sizeof line, f) != NULL && count < MAX_GOLDEN) {
        unsigned long long hash;
        if (line[0] == '#' || sscanf(line, "%ld %llx", &frames[count], &hash) != 2) {
            continue;
        }
        hashes[count++] = hash;
    }
    fclose(f);
    CHECK(count > 0);

    /* Entries must be in increasing frame order. */
    machine_init(image, IMAGE_SIZE);
    for (i = 0; i < count; i++) {
        uint64_t actual;

        while (frame < frames[i]) {
            if (machine_run_frame() == MACHINE_HALTED) {
                printf("FAIL halted at frame %ld: %s\n", frame + 1, machine_halt_reason());
                test_failures++;
                return;
            }
            frame++;
        }
        machine_render(picture);
        actual = fnv1a64(picture, sizeof picture);
        if (actual != hashes[i]) {
            printf("FAIL frame %ld hash is %016llx, expected %016llx\n", frames[i],
                   (unsigned long long)actual, (unsigned long long)hashes[i]);
            test_failures++;
        }
    }
}

int main(int argc, char **argv)
{
    FILE *f;
    uint8_t *image;

    if (argc != 3) {
        printf("usage: test_boot <image> <golden.txt>\n");
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (f == NULL) {
        printf("SKIP %s not found (run game/build.sh)\n", argv[1]);
        return 0;
    }
    image = malloc(IMAGE_SIZE + 1);
    if (image == NULL || fread(image, 1, IMAGE_SIZE + 1, f) != IMAGE_SIZE) {
        printf("FAIL %s is not %d bytes\n", argv[1], IMAGE_SIZE);
        fclose(f);
        return 1;
    }
    fclose(f);

    test_smoke(image);
    test_golden(image, argv[2]);
    free(image);
    return TEST_RESULT();
}
```

- [ ] **Step 2: Write the golden file**

These two hashes were recorded from the prototype of this exact code and image, after the dumped pictures were checked by eye.

`tests/golden.txt`:

```
# <frame number> <FNV-1a 64-bit hash of the 560x384 picture after that frame>
# Recorded from game/out/PrinceOfPersia_3.5.hdv with SHA-256
# 21428d817130b9f69673266f9bca361bb3d28286b4a84ffe6524abe844da7080.
# Frames must be listed in increasing order.
# 1200: title screen (double hi-res). 9000: demo level sword fight (hi-res).
1200 7a2efcf3872da9fd
9000 02b81175014e3b19
```

- [ ] **Step 3: Run the test against the real image**

Run: `make build/test_boot && build/test_boot game/out/PrinceOfPersia_3.5.hdv tests/golden.txt`

Expected: `ok`. The core from Tasks 2–7 is already complete, so this passes without new core code. If it prints `SKIP`, the image is missing: run `game/build.sh`.

If a golden hash does not match, do not edit `tests/golden.txt`. A mismatch means the core differs from this plan. Continue to Step 6 to dump the pictures and find out how.

- [ ] **Step 4: Check the skip path**

Run: `build/test_boot game/out/no-such-file.hdv tests/golden.txt; echo "exit=$?"`

Expected:

```
SKIP game/out/no-such-file.hdv not found (run game/build.sh)
exit=0
```

- [ ] **Step 5: Write the headless runner and the picture converter**

`host/headless.c`:

```c
/* pop-headless: run the machine without a window.
 *
 *   pop-headless <image> --frames N [--dump K:path.pbm]... [--hash K]...
 *
 * Frame numbers count from 1: "--hash 600" hashes the picture after the
 * 600th frame has run. Exits 1 if the machine halts, 2 on usage errors.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine.h"

#define MAX_ACTIONS 64

typedef struct {
    long frame;
    const char *path; /* NULL = print hash */
} action_t;

static uint64_t fnv1a64(const uint8_t *data, size_t len)
{
    uint64_t hash = 0xcbf29ce484222325ULL;
    size_t i;

    for (i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

static int write_pbm(const char *path, const uint8_t *picture)
{
    FILE *f = fopen(path, "wb");
    size_t i;

    if (f == NULL) {
        return -1;
    }
    fprintf(f, "P4\n%d %d\n", MACHINE_VIDEO_WIDTH, MACHINE_VIDEO_HEIGHT);
    /* PBM uses 1 = black; the picture uses 1 = lit. Invert so lit is white. */
    for (i = 0; i < MACHINE_VIDEO_BUFFER_SIZE; i++) {
        fputc((uint8_t)~picture[i], f);
    }
    fclose(f);
    return 0;
}

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *data;
    long size;

    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc(size > 0 ? (size_t)size : 1);
    if (data == NULL || fread(data, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(data);
        return NULL;
    }
    fclose(f);
    *len = (size_t)size;
    return data;
}

static int usage(void)
{
    fprintf(stderr, "usage: pop-headless <image> --frames N "
                    "[--dump K:path.pbm]... [--hash K]...\n");
    return 2;
}

int main(int argc, char **argv)
{
    static uint8_t picture[MACHINE_VIDEO_BUFFER_SIZE];
    action_t actions[MAX_ACTIONS];
    int action_count = 0;
    long frames = -1;
    uint8_t *image;
    size_t image_len = 0;
    long frame;
    int i;

    if (argc < 4) {
        return usage();
    }
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--hash") == 0 && i + 1 < argc && action_count < MAX_ACTIONS) {
            actions[action_count].frame = strtol(argv[++i], NULL, 10);
            actions[action_count].path = NULL;
            action_count++;
        } else if (strcmp(argv[i], "--dump") == 0 && i + 1 < argc && action_count < MAX_ACTIONS) {
            char *colon = strchr(argv[++i], ':');
            if (colon == NULL) {
                return usage();
            }
            actions[action_count].frame = strtol(argv[i], NULL, 10);
            actions[action_count].path = colon + 1;
            action_count++;
        } else {
            return usage();
        }
    }
    if (frames <= 0) {
        return usage();
    }

    image = read_file(argv[1], &image_len);
    if (image == NULL) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }

    machine_init(image, image_len);

    for (frame = 1; frame <= frames; frame++) {
        if (machine_run_frame() == MACHINE_HALTED) {
            fprintf(stderr, "halted at frame %ld: %s\n", frame, machine_halt_reason());
            return 1;
        }
        for (i = 0; i < action_count; i++) {
            if (actions[i].frame != frame) {
                continue;
            }
            machine_render(picture);
            if (actions[i].path == NULL) {
                printf("frame %ld hash %016llx\n", frame,
                       (unsigned long long)fnv1a64(picture, sizeof picture));
            } else if (write_pbm(actions[i].path, picture) != 0) {
                fprintf(stderr, "cannot write %s\n", actions[i].path);
                return 2;
            }
        }
    }

    printf("frames %ld cycles %llu mode %s\n", frames,
           (unsigned long long)machine_cycles(), machine_video_mode_name());
    free(image);
    return 0;
}
```

`tools/pbm2png.py`:

```python
import struct
import sys
import zlib


def conv(src, dst):
    d = open(src, 'rb').read()
    parts = d.split(b'\n', 2)
    w, h = map(int, parts[1].split())
    body = parts[2]
    rb = (w + 7) // 8
    raw = b''.join(b'\x00' + bytes((~b) & 0xFF for b in body[y * rb:(y + 1) * rb]) for y in range(h))

    def chunk(t, c):
        return struct.pack('>I', len(c)) + t + c + struct.pack('>I', zlib.crc32(t + c) & 0xffffffff)

    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 1, 0, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw))
           + chunk(b'IEND', b''))
    open(dst, 'wb').write(png)


for s in sys.argv[1:]:
    conv(s, s.replace('.pbm', '.png'))
```

- [ ] **Step 6: Run it and look at the pictures**

```bash
make build/pop-headless
build/pop-headless game/out/PrinceOfPersia_3.5.hdv --frames 9000 \
    --hash 1200 --hash 9000 \
    --dump 1200:build/f1200.pbm --dump 3600:build/f3600.pbm \
    --dump 7000:build/f7000.pbm --dump 9000:build/f9000.pbm
python3 tools/pbm2png.py build/f1200.pbm build/f3600.pbm build/f7000.pbm build/f9000.pbm
```

Expected output:

```
frame 1200 hash 7a2efcf3872da9fd
frame 9000 hash 02b81175014e3b19
frames 9000 cycles 153270001 mode hires
```

Open the four PNG files and confirm:
- `f1200.png`: the title screen, with the "Prince of Persia" logo over a palace seen through an arch and a copyright line at the bottom.
- `f3600.png`: the palace interior with columns and arches, the princess in the middle and Jaffar at the right edge.
- `f7000.png`: a framed page of story text beginning "marry Jaffar . . . or die within the hour."
- `f9000.png`: the demo level, a dungeon wall of stone blocks with two figures sword fighting on a ledge.

- [ ] **Step 7: Check the error exits**

```bash
build/pop-headless /dev/null --frames 10; echo "exit=$?"
build/pop-headless; echo "exit=$?"
```

Expected:

```
halted at frame 1: bad image: disk image is not 819200 bytes
exit=1
usage: pop-headless <image> --frames N [--dump K:path.pbm]... [--hash K]...
exit=2
```

- [ ] **Step 8: Run the whole suite**

Run: `make test`

Expected: seven lines of `ok`, one after each test program's command line, and exit status 0.

- [ ] **Step 9: Run the long check**

Run: `make soak`

Expected, after about 20 seconds:

```
frames 260000 cycles 4427800002 mode dhires
```

4,427,800,002 is larger than 2³² (4,294,967,296), so this run passes the point where the CPU core's cycle counter wraps.

- [ ] **Step 10: Commit**

```bash
git add host/headless.c tools/pbm2png.py tests/test_boot.c tests/golden.txt
git commit -m "Add headless runner, boot smoke test and golden frames"
```

---

### Task 9: SDL viewer

**Files:**
- Create: `host/sdl_main.c`

**Interfaces:**
- Consumes: the `machine.h` API from Task 7; SDL2 (`sdl2-config` on the PATH; installed with `brew install sdl2`).
- Produces: `build/pop-sdl <image>`: a 1120×768 window showing the game at real-time speed. Esc or closing the window quits.

There is no automated test for a window. The check is your human partner watching it.

- [ ] **Step 1: Write the viewer**

`host/sdl_main.c`:

```c
/* pop-sdl: show the machine in a window at real-time speed.
 *
 *   pop-sdl <image>
 *
 * Esc or closing the window quits.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL.h>

#include "machine.h"

#define SCALE 2
#define FRAME_MS 17 /* 16.69 ms rounded; close enough for looking at it */

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *data;
    long size;

    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc(size > 0 ? (size_t)size : 1);
    if (data == NULL || fread(data, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(data);
        return NULL;
    }
    fclose(f);
    *len = (size_t)size;
    return data;
}

static void to_pixels(const uint8_t *picture, uint32_t *pixels)
{
    int i;

    for (i = 0; i < MACHINE_VIDEO_WIDTH * MACHINE_VIDEO_HEIGHT; i++) {
        int lit = (picture[i / 8] >> (7 - (i % 8))) & 1;
        pixels[i] = lit ? 0xFFFFFFFFu : 0xFF000000u;
    }
}

int main(int argc, char **argv)
{
    static uint8_t picture[MACHINE_VIDEO_BUFFER_SIZE];
    static uint32_t pixels[MACHINE_VIDEO_WIDTH * MACHINE_VIDEO_HEIGHT];
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    uint8_t *image;
    size_t image_len = 0;
    int running = 1;
    int reported_halt = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: pop-sdl <image>\n");
        return 2;
    }
    image = read_file(argv[1], &image_len);
    if (image == NULL) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    window = SDL_CreateWindow("Prince of Persia (Apple II)", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, MACHINE_VIDEO_WIDTH * SCALE,
                              MACHINE_VIDEO_HEIGHT * SCALE, 0);
    renderer = SDL_CreateRenderer(window, -1, 0);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                MACHINE_VIDEO_WIDTH, MACHINE_VIDEO_HEIGHT);
    if (window == NULL || renderer == NULL || texture == NULL) {
        fprintf(stderr, "SDL setup: %s\n", SDL_GetError());
        return 1;
    }

    machine_init(image, image_len);

    while (running) {
        uint32_t start = SDL_GetTicks();
        uint32_t elapsed;
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                running = 0;
            }
        }

        if (machine_run_frame() == MACHINE_HALTED) {
            /* Keep the last picture on screen; report once. */
            if (!reported_halt) {
                fprintf(stderr, "halted: %s\n", machine_halt_reason());
                reported_halt = 1;
            }
        } else {
            machine_render(picture);
            to_pixels(picture, pixels);
            SDL_UpdateTexture(texture, NULL, pixels, MACHINE_VIDEO_WIDTH * (int)sizeof(uint32_t));
        }
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        elapsed = SDL_GetTicks() - start;
        if (elapsed < FRAME_MS) {
            SDL_Delay(FRAME_MS - elapsed);
        }
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    free(image);
    return 0;
}
```

- [ ] **Step 2: Build both host programs**

Run: `make host`

Expected: `build/pop-headless` and `build/pop-sdl` are built with no warnings.

- [ ] **Step 3: Check the usage path**

Run: `build/pop-sdl; echo "exit=$?"`

Expected:

```
usage: pop-sdl <image>
exit=2
```

- [ ] **Step 4: Ask your human partner to watch the attract loop**

Ask them to run:

```bash
build/pop-sdl game/out/PrinceOfPersia_3.5.hdv
```

and confirm that, over about three and a half minutes, they see in order: the title backdrop, the "Prince of Persia" logo, a page of story text ("In the Sultan's absence..."), the princess scene in the palace (Jaffar enters and an hourglass appears), a second page of story text ("marry Jaffar . . . or die within the hour"), the demo level with the prince running, jumping and sword fighting, and then the title again. Motion should look smooth and at a natural speed.

This is the by-eye confirmation of the golden frames that the spec asks for. If they report a wrong picture, stop and investigate with the frame dumps from Task 8, Step 6.

- [ ] **Step 5: Commit**

```bash
git add host/sdl_main.c
git commit -m "Add SDL viewer"
```

---

### Task 10: Firmware bring-up on the X4

**Files:**
- Create: `firmware/platformio.ini`
- Create: `firmware/partitions.csv`
- Create: `firmware/src/config.h`
- Create: `firmware/src/disk_image.S`
- Create: `firmware/src/main.cpp`

**Interfaces:**
- Consumes: the `machine.h` API from Task 7 (compiled into the firmware straight from `core/`); `game/out/PrinceOfPersia_3.5.hdv` from Task 1.
- Produces:
  - Linker symbols `disk_image_start` and `disk_image_end` (C linkage, `const uint8_t[]`), the image in flash.
  - `config.h` constants used by Task 11: `EPD_SCK`, `EPD_MOSI`, `EPD_CS`, `EPD_DC`, `EPD_RST`, `EPD_BUSY`, `WINDOW_X`, `WINDOW_Y`, `FULL_REFRESH_EVERY`, `INVERT_VIDEO`, `ROTATE_180`, `BOOT_BENCHMARK_FRAMES`, `SELF_TEST_HALT`.
  - Serial log lines (115200 baud):
    - `image: 819200 bytes, block 8: 2C 14 21 ...`
    - `benchmark: 300 frames in <ms> ms = <percent>% of real-time speed`
    - once per second: `cps=<cycles per second> late=<late frames> heap=<free bytes> mode=<blank|hires|dhires>`
    - when halted, once per second: `HALTED: <reason>`

This task has no display code. Its purpose is to get real numbers for emulation speed before any display work, as the spec requires. The panel will keep showing whatever the previous firmware left on it.

Notes:
- All `pio` commands run from the `firmware/` directory.
- `build_src_filter` compiles `core/*.c` directly into the firmware. `fake6502.inc` is not a `.c` file, so it is only compiled through `cpu.c`.
- The image is embedded by the assembler with `.incbin`. PlatformIO does not know the firmware depends on it, so **after rebuilding the game image, run `pio run -t clean` before `pio run`**.
- The serial port seen during planning was `/dev/cu.usbmodem21201` (USB ID `303A:1001`). Confirm it with `pio device list`; it can change between connections.

- [ ] **Step 1: Write the project configuration**

`firmware/platformio.ini`:

```ini
; Standalone Prince of Persia firmware for the Xteink X4 (ESP32-C3).
; Verified with the pioarduino espressif32 platform 55.03.37
; (Arduino core 3.3.7).

[platformio]
default_envs = x4

[env:x4]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino
board_upload.flash_size = 16MB
board_build.partitions = partitions.csv
build_src_filter = +<*> +<../../core/*.c>
build_flags =
    -I../core
    -I../game/out
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
lib_deps =
    zinggjm/GxEPD2@^1.6.9
    adafruit/Adafruit GFX Library@^1.12.1
monitor_speed = 115200
```

`firmware/partitions.csv`:

```
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x5000
otadata,  data, ota,     0xe000,   0x2000
app0,     app,  ota_0,   0x10000,  0x300000
```

- [ ] **Step 2: Write `config.h`**

`firmware/src/config.h`:

```c
#ifndef CONFIG_H
#define CONFIG_H

/* Xteink X4 e-paper wiring (SSD1677 controller, GDEQ0426T82 panel). */
#define EPD_SCK 8
#define EPD_MOSI 10
#define EPD_CS 21
#define EPD_DC 4
#define EPD_RST 5
#define EPD_BUSY 6

/* Where the 560x384 picture sits on the 800x480 panel. X must be a
   multiple of 8. */
#define WINDOW_X 120
#define WINDOW_Y 48

/* Force a full (flashing) refresh after this many partial refreshes to clear
   ghosting. 0 = never. */
#define FULL_REFRESH_EVERY 0

/* 1 = black dots on white instead of white dots on black. */
#define INVERT_VIDEO 0

/* 1 = turn the picture upside down, if the panel is mounted that way. */
#define ROTATE_180 0

/* Frames to run flat-out at boot to measure emulation speed. 0 = skip. */
#define BOOT_BENCHMARK_FRAMES 300

/* 1 = boot with a deliberately wrong image size so the machine halts at
   once. Used to check the halt reporting on the device. */
#define SELF_TEST_HALT 0

#endif
```

- [ ] **Step 3: Write the image embedding**

`firmware/src/disk_image.S`:

```asm
/* Embeds the game disk image in flash as read-only data.
   The file is found through the -I../game/out flag in platformio.ini.
   After rebuilding the game image, run "pio run -t clean" so this file is
   assembled again. */
    .section .rodata.disk_image, "a", %progbits
    .balign 4
    .global disk_image_start
disk_image_start:
    .incbin "PrinceOfPersia_3.5.hdv"
    .global disk_image_end
disk_image_end:
    .balign 4
```

- [ ] **Step 4: Write `main.cpp`**

`firmware/src/main.cpp`:

```cpp
#include <Arduino.h>

#include "config.h"
#include "machine.h"

/* 17,030 cycles at 1,020,484 Hz. */
#define FRAME_US 16688
/* If emulation falls this far behind real time, stop trying to catch up. */
#define MAX_LAG_US 250000
#define YIELD_EVERY_FRAMES 10

/* Defined in disk_image.S: the game disk image, read in place from flash. */
extern "C" {
extern const uint8_t disk_image_start[];
extern const uint8_t disk_image_end[];
}

static bool halted;
static uint32_t next_frame_us;
static uint32_t late_frames;
static uint32_t frames_since_yield;

static void log_image(size_t len)
{
    Serial.printf("image: %u bytes, block 8:", (unsigned)len);
    for (int i = 0; i < 16 && (size_t)(8 * 512 + i) < len; i++) {
        Serial.printf(" %02X", disk_image_start[8 * 512 + i]);
    }
    Serial.println();
}

static void boot_benchmark(void)
{
    if (BOOT_BENCHMARK_FRAMES <= 0) {
        return;
    }
    uint32_t start = micros();
    for (int i = 0; i < BOOT_BENCHMARK_FRAMES; i++) {
        if (machine_run_frame() == MACHINE_HALTED) {
            return;
        }
    }
    uint32_t elapsed = micros() - start;
    uint32_t real_time = (uint32_t)BOOT_BENCHMARK_FRAMES * FRAME_US;
    Serial.printf("benchmark: %d frames in %u ms = %u%% of real-time speed\n",
                  BOOT_BENCHMARK_FRAMES, (unsigned)(elapsed / 1000),
                  (unsigned)((uint64_t)real_time * 100 / (elapsed ? elapsed : 1)));
}

static void log_stats(void)
{
    static uint32_t last_ms;
    static uint64_t last_cycles;

    uint32_t now = millis();
    uint32_t elapsed = now - last_ms;
    if (elapsed < 1000) {
        return;
    }
    uint64_t cycles = machine_cycles();
    Serial.printf("cps=%u late=%u heap=%u mode=%s\n",
                  (unsigned)((cycles - last_cycles) * 1000 / elapsed), (unsigned)late_frames,
                  (unsigned)ESP.getFreeHeap(), machine_video_mode_name());
    last_ms = now;
    last_cycles = cycles;
}

/* Sleep until the next 16.69 ms tick, or note that we are running late. */
static void pace(void)
{
    next_frame_us += FRAME_US;
    int32_t ahead = (int32_t)(next_frame_us - micros());

    if (ahead >= 1000) {
        delay((uint32_t)ahead / 1000);
        frames_since_yield = 0;
        return;
    }
    if (ahead < 0) {
        late_frames++;
        if (ahead < -MAX_LAG_US) {
            next_frame_us = micros();
        }
    }
    /* Even when late, let the idle task (and its watchdog) run. */
    if (++frames_since_yield >= YIELD_EVERY_FRAMES) {
        vTaskDelay(1);
        frames_since_yield = 0;
    }
}

void setup()
{
    Serial.begin(115200);
    /* Never block on logging: on battery there is no USB host to read it. */
    Serial.setTxTimeoutMs(0);
    delay(1500); /* let USB serial enumerate so the boot log is visible */

    size_t image_len = (size_t)(disk_image_end - disk_image_start);
    log_image(image_len);
    if (SELF_TEST_HALT) {
        image_len -= 1;
    }

    machine_init(disk_image_start, image_len);
    boot_benchmark();
    machine_init(disk_image_start, image_len); /* start the demo from the top */

    next_frame_us = micros();
}

/* The Arduino loop task is the emulator task: one video frame per pass. */
void loop()
{
    if (halted) {
        Serial.printf("HALTED: %s\n", machine_halt_reason());
        delay(1000);
        return;
    }

    if (machine_run_frame() == MACHINE_HALTED) {
        halted = true;
        return;
    }

    pace();
    log_stats();
}
```

- [ ] **Step 5: Check that a missing image fails the build clearly**

```bash
cd firmware
mv ../game/out/PrinceOfPersia_3.5.hdv ../game/out/hidden.hdv
pio run 2>&1 | grep -E "Error|FAILED"
mv ../game/out/hidden.hdv ../game/out/PrinceOfPersia_3.5.hdv
```

Expected:

```
src/disk_image.S:9: Error: file not found: PrinceOfPersia_3.5.hdv
*** [.pio/build/x4/src/disk_image.S.o] Error 1
```

followed by a `[FAILED]` line. The first `pio run` also downloads the GxEPD2 and Adafruit GFX libraries.

- [ ] **Step 6: Build**

Run: `pio run`

Expected: `[SUCCESS]`, with lines close to

```
RAM:   [=====     ]  50.3% (used 164880 bytes from 327680 bytes)
Flash: [====      ]  36.8% (used 1157988 bytes from 3145728 bytes)
```

- [ ] **Step 7: Back up the device (needs your human partner)**

Stop and tell your human partner: the next two steps read and then replace the firmware on their X4. Ask them to connect it by USB-C, wake it, and confirm they want to proceed. Do not continue without a yes.

```bash
pio device list
```

Find the entry with `VID:PID=303A:1001` and use its path as the port below.

```bash
cd ..
mkdir -p backup
~/.platformio/penv/bin/esptool --chip esp32c3 -p /dev/cu.usbmodem21201 -b 921600 \
    read-flash 0x000000 0x1000000 backup/x4-flash-backup.bin
ls -l backup/x4-flash-backup.bin
```

Expected: the read takes a few minutes and the file is exactly 16,777,216 bytes. Ask your human partner to copy it somewhere outside the repository as well. `backup/` is gitignored.

To restore the device later:

```bash
~/.platformio/penv/bin/esptool --chip esp32c3 -p /dev/cu.usbmodem21201 -b 921600 \
    write-flash 0x000000 backup/x4-flash-backup.bin
```

If `esptool` cannot connect, the device is probably asleep: ask your human partner to wake it and try again.

- [ ] **Step 8: Flash and read the log**

```bash
cd firmware
pio run -t upload --upload-port /dev/cu.usbmodem21201
pio device monitor -p /dev/cu.usbmodem21201 -b 115200
```

Expected in the monitor (press the device's reset button, or re-run the monitor, if the first lines were missed):

```
image: 819200 bytes, block 8: 2C 14 21 A2 FF 9A A5 43 85 FD 20 29 20 20 56 20
benchmark: 300 frames in <ms> ms = <percent>% of real-time speed
cps=<about 1020000> late=<small, not growing> heap=<free bytes> mode=blank
```

Within a few seconds `mode` becomes `dhires` (the title screen), and later alternates between `hires` and `dhires` as the attract loop plays.

Pass criteria:
- the `block 8` bytes are exactly those above;
- `benchmark` reports 100% or more;
- `cps` stays between 1,000,000 and 1,040,000;
- `late` is not climbing steadily;
- no `HALTED` line appears in two minutes.

If `benchmark` is below 100%, stop and report the number to your human partner. That is the spec's "emulated speed is below real time" risk, and what to do about it is a new design decision, not part of this plan.

Write down the benchmark percentage and the free heap figure; Task 11 records them in the README.

- [ ] **Step 9: Check that the firmware picks up a rebuilt image**

```bash
pio run -t clean
pio run 2>&1 | grep -E "SUCCESS|FAILED"
```

Expected: `[SUCCESS]`. This confirms the clean-then-build sequence that must follow any `game/build.sh` run.

- [ ] **Step 10: Check halt reporting over serial**

In `firmware/src/config.h`, change `#define SELF_TEST_HALT 0` to `#define SELF_TEST_HALT 1`, then:

```bash
pio run -t upload --upload-port /dev/cu.usbmodem21201
pio device monitor -p /dev/cu.usbmodem21201 -b 115200
```

Expected, repeating once per second:

```
HALTED: bad image: disk image is not 819200 bytes
```

Change `SELF_TEST_HALT` back to `0`, upload again, and confirm the `cps=` lines return.

- [ ] **Step 11: Commit**

```bash
cd ..
git status --short
```

Confirm `firmware/.pio/` and `backup/` are not listed, and that `config.h` has `SELF_TEST_HALT 0`.

```bash
git add firmware/platformio.ini firmware/partitions.csv firmware/src/config.h firmware/src/disk_image.S firmware/src/main.cpp
git commit -m "Add X4 firmware: emulator loop, pacing and serial stats"
```

---

### Task 11: E-ink output

**Files:**
- Create: `firmware/src/display.h`, `firmware/src/display.cpp`
- Modify: `firmware/src/main.cpp` (replace the whole file)
- Create: `README.md`

**Interfaces:**
- Consumes: `config.h`, `disk_image_start`/`disk_image_end`, and the `machine.h` API from Task 10; GxEPD2's `GxEPD2_426_GDEQ0426T82` driver class; Adafruit GFX's `glcdfont.c` (the array `font`, 5 bytes per character, one byte per column, bit 0 at the top).
- Produces:
  - `void display_begin(void)`: start SPI on the X4's pins, initialise the panel, clear it with one full refresh.
  - `void display_show(uint8_t *picture)`: show a picture in the game window and block until the panel has refreshed. May modify the buffer.
  - `void display_show_halt(uint8_t *picture, const char *reason)`: show "HALTED" and the first 46 characters of the reason with one full refresh.
  - The once-per-second log line gains a field: `cps=... late=... refresh/s=<n.n> heap=... mode=...`.

How it works:
- The Arduino loop task is the emulator task (priority 1). A second task (priority 2) owns the panel.
- Hand-off uses two binary semaphores. The display task gives `frame_request` and waits on `frame_ready`. The emulator, at the end of a frame, sees the request, renders into the one shared picture buffer, and gives `frame_ready`. The emulator only writes the buffer between a request and its signal; the display task only reads it after the signal.
- The display task skips pictures whose hash equals the last one shown.
- GxEPD2 waits for the panel with `delay(1)` in a loop, which lets the emulator run during the refresh.
- Per picture: `writeImage` (new picture into the controller's current buffer), `refresh(x, y, w, h)` (partial refresh of the window), then `writeImageToPrevious` (so the next differential refresh compares against what is on the panel).
- GxEPD2 bitmaps use 1 = white, the same as the picture buffer, so no inversion is needed by default.

- [ ] **Step 1: Write the display module**

`firmware/src/display.h`:

```c
#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

/* Initialise the panel and clear it with one full refresh. */
void display_begin(void);

/* Show a 560x384 picture (MACHINE_VIDEO_BUFFER_SIZE bytes) in the game
   window and block until the panel has finished refreshing. The buffer may
   be modified. */
void display_show(uint8_t *picture);

/* Replace the picture with a "HALTED" message and the start of reason,
   using one full refresh. Uses picture as scratch space. */
void display_show_halt(uint8_t *picture, const char *reason);

#endif
```

`firmware/src/display.cpp`:

```cpp
#include "display.h"

#include <Arduino.h>
#include <SPI.h>
#include <string.h>

#include <gdeq/GxEPD2_426_GDEQ0426T82.h>
#include <glcdfont.c> /* Adafruit GFX classic 5x7 font: font[ch * 5 + column] */

#include "config.h"
#include "machine.h"

static GxEPD2_426_GDEQ0426T82 epd(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);
static uint32_t partial_count;

static const uint8_t background = INVERT_VIDEO ? 0xFF : 0x00;

static uint8_t reverse_bits(uint8_t b)
{
    b = (uint8_t)(((b & 0xF0) >> 4) | ((b & 0x0F) << 4));
    b = (uint8_t)(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
    b = (uint8_t)(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
    return b;
}

/* Rotate a 1-bit picture by 180 degrees in place. */
static void rotate_180(uint8_t *picture)
{
    size_t i = 0;
    size_t j = MACHINE_VIDEO_BUFFER_SIZE - 1;

    while (i < j) {
        uint8_t tmp = reverse_bits(picture[i]);
        picture[i] = reverse_bits(picture[j]);
        picture[j] = tmp;
        i++;
        j--;
    }
}

void display_begin(void)
{
    /* The X4 does not use the board's default SPI pins. Starting SPI here
       makes GxEPD2's own SPI.begin() a no-op that keeps these pins. */
    SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
    epd.init(0, true, 10, false);
    epd.clearScreen(background);
    partial_count = 0;
}

void display_show(uint8_t *picture)
{
    if (ROTATE_180) {
        rotate_180(picture);
    }

    epd.writeImage(picture, WINDOW_X, WINDOW_Y, MACHINE_VIDEO_WIDTH, MACHINE_VIDEO_HEIGHT,
                   INVERT_VIDEO);

    if (FULL_REFRESH_EVERY > 0 && ++partial_count >= FULL_REFRESH_EVERY) {
        partial_count = 0;
        epd.refresh(false);
    } else {
        epd.refresh(WINDOW_X, WINDOW_Y, MACHINE_VIDEO_WIDTH, MACHINE_VIDEO_HEIGHT);
    }

    /* Differential refresh compares against the "previous" buffer, so make
       it match what is now on the panel. */
    epd.writeImageToPrevious(picture, WINDOW_X, WINDOW_Y, MACHINE_VIDEO_WIDTH,
                             MACHINE_VIDEO_HEIGHT, INVERT_VIDEO);
}

static void set_dot(uint8_t *picture, int x, int y)
{
    if (x < 0 || x >= MACHINE_VIDEO_WIDTH || y < 0 || y >= MACHINE_VIDEO_HEIGHT) {
        return;
    }
    picture[y * (MACHINE_VIDEO_WIDTH / 8) + x / 8] |= (uint8_t)(0x80 >> (x % 8));
}

static void draw_text(uint8_t *picture, int x, int y, const char *text, int scale)
{
    for (; *text != '\0'; text++, x += 6 * scale) {
        uint8_t ch = (uint8_t)*text;

        for (int col = 0; col < 5; col++) {
            uint8_t bits = pgm_read_byte(&font[ch * 5 + col]);

            for (int row = 0; row < 8; row++) {
                if (!(bits & (1 << row))) {
                    continue;
                }
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        set_dot(picture, x + col * scale + dx, y + row * scale + dy);
                    }
                }
            }
        }
    }
}

void display_show_halt(uint8_t *picture, const char *reason)
{
    char line[47]; /* 46 characters of 12 dots fit across 560 */

    memset(picture, 0, MACHINE_VIDEO_BUFFER_SIZE);
    draw_text(picture, 8, 120, "HALTED", 6);
    strncpy(line, reason, sizeof line - 1);
    line[sizeof line - 1] = '\0';
    draw_text(picture, 8, 200, line, 2);

    if (ROTATE_180) {
        rotate_180(picture);
    }
    epd.writeImageForFullRefresh(picture, WINDOW_X, WINDOW_Y, MACHINE_VIDEO_WIDTH,
                                 MACHINE_VIDEO_HEIGHT, INVERT_VIDEO);
    epd.refresh(false);
}
```

- [ ] **Step 2: Replace `main.cpp`**

`firmware/src/main.cpp`, complete new contents:

```cpp
#include <Arduino.h>

#include "config.h"
#include "display.h"
#include "machine.h"

/* 17,030 cycles at 1,020,484 Hz. */
#define FRAME_US 16688
/* If emulation falls this far behind real time, stop trying to catch up. */
#define MAX_LAG_US 250000
#define YIELD_EVERY_FRAMES 10

/* Defined in disk_image.S: the game disk image, read in place from flash. */
extern "C" {
extern const uint8_t disk_image_start[];
extern const uint8_t disk_image_end[];
}

static uint8_t picture[MACHINE_VIDEO_BUFFER_SIZE];
static SemaphoreHandle_t frame_request;
static SemaphoreHandle_t frame_ready;
static volatile bool halted;
static volatile uint32_t panel_refreshes;

static uint32_t next_frame_us;
static uint32_t late_frames;
static uint32_t frames_since_yield;

static uint64_t fnv1a64(const uint8_t *data, size_t len)
{
    uint64_t hash = 0xcbf29ce484222325ULL;

    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

/* Owns the panel. Asks the emulator for a picture, shows it if it changed,
   and repeats. The emulator keeps running while the panel refreshes. */
static void display_task(void *)
{
    uint64_t shown = 0;
    bool first = true;

    display_begin();
    for (;;) {
        xSemaphoreGive(frame_request);
        xSemaphoreTake(frame_ready, portMAX_DELAY);

        if (halted) {
            display_show_halt(picture, machine_halt_reason());
            vTaskDelete(NULL);
        }

        uint64_t hash = fnv1a64(picture, sizeof picture);
        if (first || hash != shown) {
            display_show(picture);
            shown = hash;
            first = false;
            panel_refreshes = panel_refreshes + 1;
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

static void log_image(size_t len)
{
    Serial.printf("image: %u bytes, block 8:", (unsigned)len);
    for (int i = 0; i < 16 && (size_t)(8 * 512 + i) < len; i++) {
        Serial.printf(" %02X", disk_image_start[8 * 512 + i]);
    }
    Serial.println();
}

static void boot_benchmark(void)
{
    if (BOOT_BENCHMARK_FRAMES <= 0) {
        return;
    }
    uint32_t start = micros();
    for (int i = 0; i < BOOT_BENCHMARK_FRAMES; i++) {
        if (machine_run_frame() == MACHINE_HALTED) {
            return;
        }
    }
    uint32_t elapsed = micros() - start;
    uint32_t real_time = (uint32_t)BOOT_BENCHMARK_FRAMES * FRAME_US;
    Serial.printf("benchmark: %d frames in %u ms = %u%% of real-time speed\n",
                  BOOT_BENCHMARK_FRAMES, (unsigned)(elapsed / 1000),
                  (unsigned)((uint64_t)real_time * 100 / (elapsed ? elapsed : 1)));
}

static void log_stats(void)
{
    static uint32_t last_ms;
    static uint64_t last_cycles;
    static uint32_t last_refreshes;

    uint32_t now = millis();
    uint32_t elapsed = now - last_ms;
    if (elapsed < 1000) {
        return;
    }
    uint64_t cycles = machine_cycles();
    uint32_t refreshes = panel_refreshes;
    Serial.printf("cps=%u late=%u refresh/s=%u.%u heap=%u mode=%s\n",
                  (unsigned)((cycles - last_cycles) * 1000 / elapsed), (unsigned)late_frames,
                  (unsigned)((refreshes - last_refreshes) * 1000 / elapsed),
                  (unsigned)((refreshes - last_refreshes) * 10000 / elapsed % 10),
                  (unsigned)ESP.getFreeHeap(), machine_video_mode_name());
    last_ms = now;
    last_cycles = cycles;
    last_refreshes = refreshes;
}

/* Sleep until the next 16.69 ms tick, or note that we are running late. */
static void pace(void)
{
    next_frame_us += FRAME_US;
    int32_t ahead = (int32_t)(next_frame_us - micros());

    if (ahead >= 1000) {
        delay((uint32_t)ahead / 1000);
        frames_since_yield = 0;
        return;
    }
    if (ahead < 0) {
        late_frames++;
        if (ahead < -MAX_LAG_US) {
            next_frame_us = micros();
        }
    }
    /* Even when late, let the idle task (and its watchdog) run. */
    if (++frames_since_yield >= YIELD_EVERY_FRAMES) {
        vTaskDelay(1);
        frames_since_yield = 0;
    }
}

void setup()
{
    Serial.begin(115200);
    /* Never block on logging: on battery there is no USB host to read it. */
    Serial.setTxTimeoutMs(0);
    delay(1500); /* let USB serial enumerate so the boot log is visible */

    size_t image_len = (size_t)(disk_image_end - disk_image_start);
    log_image(image_len);
    if (SELF_TEST_HALT) {
        image_len -= 1;
    }

    machine_init(disk_image_start, image_len);
    boot_benchmark();
    machine_init(disk_image_start, image_len); /* start the demo from the top */

    frame_request = xSemaphoreCreateBinary();
    frame_ready = xSemaphoreCreateBinary();
    xTaskCreate(display_task, "display", 8192, NULL, 2, NULL);

    next_frame_us = micros();
}

/* The Arduino loop task is the emulator task: one video frame per pass. */
void loop()
{
    if (halted) {
        Serial.printf("HALTED: %s\n", machine_halt_reason());
        delay(1000);
        return;
    }

    if (machine_run_frame() == MACHINE_HALTED) {
        halted = true;
        xSemaphoreGive(frame_ready); /* wake the display task to show it */
        return;
    }

    if (xSemaphoreTake(frame_request, 0) == pdTRUE) {
        machine_render(picture);
        xSemaphoreGive(frame_ready);
    }

    pace();
    log_stats();
}
```

- [ ] **Step 3: Build**

Run (from `firmware/`): `pio run`

Expected: `[SUCCESS]`, with lines close to

```
RAM:   [======    ]  58.5% (used 191840 bytes from 327680 bytes)
Flash: [====      ]  37.1% (used 1168624 bytes from 3145728 bytes)
```

- [ ] **Step 4: Flash and watch the panel (needs your human partner)**

Confirm with your human partner before uploading.

```bash
pio run -t upload --upload-port /dev/cu.usbmodem21201
pio device monitor -p /dev/cu.usbmodem21201 -b 115200
```

Ask your human partner to hold the device in landscape and describe what they see. Expected:
1. The whole panel flashes and turns black (the full refresh from `display_begin`).
2. Within about 10 seconds the title backdrop appears in a window centred on the panel, with a black border around it.
3. The attract loop then plays as a slideshow: logo, story text, princess scene, more story text, demo level, and round again.

Expected in the log: a non-zero `refresh/s` while the picture is changing, and `cps` still between 1,000,000 and 1,040,000. The spec estimates 2 to 3 refreshes per second from the panel's documented timing; the real figure is what this step measures, and a lower one is acceptable for this project. `late` may now climb slowly, because each picture transfer briefly blocks the emulator; that is acceptable as long as `cps` holds.

- [ ] **Step 5: Fix the orientation if needed**

If your human partner reports the picture is upside down, set `#define ROTATE_180 1` in `firmware/src/config.h`, upload again, and confirm it is upright.

If they report it is mirrored left-to-right, or shifted or wrapped, stop and report exactly what they see. That means the panel wiring differs from GxEPD2's assumption, which is the spec's last listed risk.

- [ ] **Step 6: Tune ghosting**

Ask your human partner to watch a full attract loop and say whether leftover shadows from earlier pictures build up.
- If they do, set `FULL_REFRESH_EVERY` to `60` in `config.h` and upload. Raise or lower it to taste: lower clears shadows more often but flashes more often.
- If white-on-black looks muddy, try `#define INVERT_VIDEO 1` (black on white) and let them choose.

Keep whichever values they prefer.

- [ ] **Step 7: Record the measurements**

Note three numbers from the serial log while the demo level is playing: the `benchmark` percentage from boot, a typical `refresh/s`, and `heap`.

- [ ] **Step 8: Check it runs on battery**

Close the serial monitor and ask your human partner to unplug the USB cable and watch for one minute.

Expected: the attract loop keeps advancing at the same pace as when plugged in. If it freezes or slows sharply when unplugged, serial logging is blocking; check that `Serial.setTxTimeoutMs(0)` is present in `setup()`.

- [ ] **Step 9: Check the halt screen**

Reconnect USB. Set `#define SELF_TEST_HALT 1` in `config.h`, then upload.

Expected on the panel, after the initial black full refresh: the word `HALTED` in large letters and, below it in small letters, `bad image: disk image is not 819200 bytes`. Expected in the log: `HALTED: bad image: disk image is not 819200 bytes` once per second.

Set `SELF_TEST_HALT` back to `0`, upload, and confirm the title sequence returns.

- [ ] **Step 10: Write the README**

Fill in the three measured values from Step 7 and the flag values chosen in Steps 5 and 6 where the angle brackets are.

`README.md`:

````markdown
# Prince of Persia on the Xteink X4

Standalone firmware that boots Jordan Mechner's Apple II Prince of Persia on
an Xteink X4 e-reader and plays its title sequence and attract-mode demo on
the e-ink panel. No input, no sound.

It is a small Apple IIe emulator: a 6502, 128K of banked memory, the soft
switches the game uses, a stub ROM, and a disk trap that serves blocks from
an image built from the published source. Design:
`docs/superpowers/specs/2026-10-01-pop-xteink-x4-design.md`.

## Requirements

macOS with Docker, PlatformIO (`pio`), SDL2 (`brew install sdl2`), Python 3
and git.

## Build and test on the Mac

    game/build.sh      # builds game/out/PrinceOfPersia_3.5.hdv from source
    make test          # unit tests, boot smoke test, golden frames
    make soak          # 260,000-frame run (about 20 seconds)
    make host
    build/pop-sdl game/out/PrinceOfPersia_3.5.hdv

## Flash the X4

Back up the device first. This firmware replaces the e-reader firmware.

    ~/.platformio/penv/bin/esptool --chip esp32c3 -p <port> -b 921600 \
        read-flash 0x000000 0x1000000 backup/x4-flash-backup.bin

    cd firmware
    pio run -t upload --upload-port <port>
    pio device monitor -p <port> -b 115200

Find `<port>` with `pio device list` (USB ID `303A:1001`).

After rebuilding the game image, run `pio run -t clean` before `pio run`:
the build does not notice that the image changed.

Tuning flags are in `firmware/src/config.h`.

## Restore the e-reader firmware

    ~/.platformio/penv/bin/esptool --chip esp32c3 -p <port> -b 921600 \
        write-flash 0x000000 backup/x4-flash-backup.bin

## Measured on the device

- Emulation speed, unpaced: <benchmark percentage>% of real time
- Panel refreshes per second during the demo level: <refresh/s>
- Free heap while running: <heap> bytes
- `ROTATE_180` = <0 or 1>, `INVERT_VIDEO` = <0 or 1>,
  `FULL_REFRESH_EVERY` = <value>

## Legal

Jordan Mechner published the Apple II source for study and modification and
stated that this is not a grant of rights in Prince of Persia, which belongs
to Ubisoft. This repository contains no game code, game data or Apple ROM.
The game is built locally for personal use and is never committed.
````

- [ ] **Step 11: Final check and commit**

```bash
cd ..
make test
git status --short
```

Expected: seven `ok` lines; `firmware/.pio/`, `backup/`, `game/out/`, `game/work/` and `tests/data/` are not listed; `config.h` has `SELF_TEST_HALT 0`.

```bash
git add firmware/src/display.h firmware/src/display.cpp firmware/src/main.cpp firmware/src/config.h README.md
git commit -m "Add e-ink output, halt screen and README"
```
