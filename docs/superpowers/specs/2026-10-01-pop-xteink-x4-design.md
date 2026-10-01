# Prince of Persia (Apple II) on the Xteink X4: Design

Date: 2026-10-01
Status: approved in conversation, awaiting written-spec review

## 1. Goal

The Xteink X4 powers on, boots Prince of Persia built from Jordan Mechner's
published Apple II source, and plays the title sequence and the attract-mode
demo on its e-ink panel.

This is a fun hack / tech demo. Seeing the game boot and run its demo on e-ink
is the win. A low, choppy frame rate is accepted.

### Success criteria

1. `game/build.sh` produces the disk image from source on the developer's Mac.
2. On the Mac, the emulator boots that image and shows the title sequence and
   the demo level, verified by automated golden-frame tests.
3. On the X4, the same emulator core boots the same image and the title
   sequence and demo level are visible on the panel, with no user input.

### Out of scope

Input of any kind, sound, saved games, power management, SD card access,
colour or grayscale rendering, text-mode rendering, and running any other
Apple II software.

## 2. Decisions made with the user

| Decision | Choice |
|---|---|
| End goal | Fun hack / tech demo |
| Firmware model | Standalone firmware that boots straight into the game. Returning to an e-reader firmware means reflashing over USB |
| Game binary | Built from source on the host, not a downloaded disk image |
| Approach | Purpose-built minimal Apple IIe emulator core with high-level traps at its edges |
| CPU core | Vendored fake6502 (public domain), not written from scratch |
| Disk image location | Embedded in the firmware binary |
| Emulation speed | Real Apple speed; the panel shows whatever frames it can |
| Game build variant | Unpatched repository source; `RELEASE_PATCH=1` is the fallback |

## 3. Facts this design rests on

### 3.1 Target hardware (Xteink X4)

- ESP32-C3: single-core RISC-V RV32IMC at 160 MHz, about 380 KB usable RAM,
  no PSRAM, 16 MB flash.
- Panel: GDEQ0426T82, 4.26", 800×480, 1 bit per pixel, SSD1677 controller.
- Panel SPI pins: SCLK = GPIO8, MOSI = GPIO10, CS = GPIO21, DC = GPIO4,
  RST = GPIO5, BUSY = GPIO6.
- Documented refresh times: full about 1.6 s, partial about 0.3 to 0.6 s.
  Windowed (partial-region) updates are supported.
- No speaker.
- USB serial (CDC) is available for logging and flashing.

### 3.2 The game, verified in the source

Source: `github.com/jmechner/Prince-of-Persia-Apple-II`, built through Adam
Green's fork `github.com/adamgreen/Prince-of-Persia-Apple-II` (branch `build`,
commit `ec78dbfd51013ba349cda8c51c3ce0595fe75342`).

- **Machine:** 128K Apple IIe. 64 KB main and 64 KB auxiliary memory, with
  language-card banking in both. `POPBOOT35.S` refuses to run without it.
- **CPU:** NMOS 6502 opcodes only on the IIe path. 65C02 and 65816 opcodes
  appear only in routines guarded by the `IIGS` flag (`GRAFIX.S` FASTSPEED,
  NORMSPEED, getparam, setparam).
- **ROM use:** only during boot.
  - Calls to `$FB2F`, `$FC58`, `$FE84`, `$FE93`, `$FE89` (screen and I/O
    setup). Returning immediately is sufficient.
  - `$FBB3` must read `$06` (Apple IIe family).
  - `$FBC0` must read non-zero, otherwise the game selects IIc
    interrupt-driven vertical-blank handling (`GRAFIX.S` InitVBLANK).
  - `$FE1F` is called with carry set; returning with carry still set means
    "not a IIGS".
  - `$C017` must read with bit 7 clear.
- **Disk:** the 3.5" build is the fork's default. It produces one flat block
  image, `PrinceOfPersia_3.5.hdv`, 819,200 bytes (1,600 blocks of 512 bytes),
  holding all disk sides.
  - The boot file (`POPBOOT35` followed by `RW1835.POP`) is 4 KB at image
    blocks 8 to 15 and loads at `$2000`. Verified: block 8 begins
    `2C 14 21 A2 FF 9A A5 43 85 FD`.
  - `POPBOOT35` reads the boot unit from zero-page `$43` and stores it in
    `$FD`. The high nibble is the slot.
  - All later disk access goes through `RW1835.POP.S`, which issues SmartPort
    calls to the slot's firmware: entry address is
    `$Cn00 + (value at $CnFF) + 3`. It uses only READ BLOCK (`$01`) and
    WRITE BLOCK (`$02`). It forces main memory (`$C002`, `$C004`) before each
    call and reads `$C013` and `$C014` to restore banking afterwards.
  - The disk check reads block 331 and compares it with the signature text
    "Prince of Persia 3.5!". Verified present in the built image.
- **Frame pacing:** the IIe path polls `$C019`. `VBLANK` in `GRAFIX.S` waits
  while bit 7 is clear, then waits while bit 7 is set. So bit 7 set means
  active display and bit 7 clear means vertical blank. No interrupts are used
  on the IIe path.
- **Graphics:** hi-res (280×192) with page flipping for gameplay; double
  hi-res (560×192) for title screens (`UNPACK.S`).
- **Self-modifying code** is used throughout, so the CPU must be interpreted.

### 3.3 Host toolchain, verified on the developer's Mac

- arm64 macOS with Docker, PlatformIO (`pio`), SDL2, Python 3 and git
  installed.
- The fork's bundled build tools are 32-bit binaries. The Linux ones run under
  Docker `--platform linux/386`. A full `make clean all` in an
  `i386/debian:bookworm-slim` container with `make` and `patch` installed
  completed with 0 errors and produced the image with SHA-256
  `21428d817130b9f69673266f9bca361bb3d28286b4a84ffe6524abe844da7080`.

## 4. Architecture

One portable C emulator core with two thin shells.

```
game/      Docker build: Mechner's source -> PrinceOfPersia_3.5.hdv
core/      portable C99, no platform dependencies, no dynamic allocation
host/      Mac shell: SDL2 window and a headless frame dumper
firmware/  X4 shell: PlatformIO project, e-ink output, pacing
tests/     host-side tests for the core
docs/      specs and plans
```

The shells see the core only through `machine.h`. The core never includes a
platform header.

### 4.1 Core units

| Unit | Responsibility | Depends on |
|---|---|---|
| `cpu` | Thin interface over vendored fake6502: reset, step one instruction, read and set registers, cycle counter | `bus` |
| `bus` | 128 KB of emulated memory behind 256-entry read and write page tables. Rebuilds the tables when banking state changes. Routes page `$C0` to `softswitch` | none |
| `softswitch` | State and behaviour of the `$C0xx` switches listed in 5.2 | `bus` |
| `rom` | Stub ROM contents for `$C100`–`$FFFF` | none |
| `blockdev` | Fake SmartPort card in slot 5 and its trap handler | `bus`, image pointer |
| `boot` | Puts the machine in the state the ProDOS boot block would leave, without running it | `bus`, `blockdev`, `cpu` |
| `video` | Renders the visible graphics page to a 560×384 1-bit buffer | `bus`, switch state |
| `machine` | Public API and the per-frame loop | all above |

### 4.2 Public API (`machine.h`)

```c
typedef enum { MACHINE_RUNNING, MACHINE_HALTED } machine_status_t;

void machine_init(const uint8_t *image, size_t image_len);
machine_status_t machine_run_frame(void);       /* 17,030 CPU cycles */
void machine_render(uint8_t *out);              /* 560x384, 1 bpp, 26,880 bytes */
const char *machine_halt_reason(void);          /* valid when halted */
uint64_t machine_cycles(void);
```

`machine_render` output: 384 rows of 70 bytes, most significant bit is the
leftmost dot, bit value 1 means a lit (white) dot.

## 5. Core behaviour

### 5.1 Memory and banking

- Storage: `main[65536]` and `aux[65536]`, statically allocated. The
  language-card area uses `$D000`–`$FFFF` of each array, plus a second 4 KB
  bank per side held at `$C000`–`$CFFF` of the same array (that range is never
  RAM on a real machine, so it is free to reuse).
- Zero page, stack and the language-card area select main or aux by ALTZP.
- `$0200`–`$BFFF` selects main or aux independently for reads (RAMRD) and
  writes (RAMWRT).
- With 80STORE on, PAGE2 selects main or aux for `$0400`–`$07FF`, and also for
  `$2000`–`$3FFF` when HIRES is on. This overrides RAMRD and RAMWRT for those
  ranges.
- `$C100`–`$CFFF` reads the stub ROM (which contains the fake card in the
  `$C500` page). Writes are ignored.
- `$D000`–`$FFFF` reads RAM or stub ROM, and accepts or ignores writes,
  according to language-card state.

### 5.2 Soft switches

Implemented with real behaviour:

| Address | Function |
|---|---|
| `$C000`/`$C001` (write) | 80STORE off/on |
| `$C002`/`$C003` (write) | RAMRD main/aux |
| `$C004`/`$C005` (write) | RAMWRT main/aux |
| `$C008`/`$C009` (write) | ALTZP off/on |
| `$C00C`/`$C00D` (write) | 80COL off/on |
| `$C00E`/`$C00F` (write) | ALTCHARSET off/on (state stored, no effect) |
| `$C011`–`$C018`, `$C01A`–`$C01F` (read) | Status flags in bit 7 for: LC bank 2, LC RAM read, RAMRD, RAMWRT, INTCXROM (0), ALTZP, SLOTC3ROM (0), 80STORE, TEXT, MIXED, PAGE2, HIRES, ALTCHARSET, 80COL |
| `$C019` (read) | Bit 7 set during the first 12,480 cycles of a frame, clear during the last 4,550 |
| `$C050`–`$C057` | TEXT off/on, MIXED off/on, PAGE2 off/on, HIRES off/on |
| `$C05E`/`$C05F` | DHIRES on/off |
| `$C080`–`$C08F` | Language card: bank select, read RAM or ROM, write enable requiring two consecutive reads of an odd address |

Read as idle, writes ignored:

| Address | Value |
|---|---|
| `$C000` (read) | `$00` (no key) |
| `$C010` | `$00` (clears nothing) |
| `$C030` | `$00` (speaker ignored) |
| `$C061`–`$C063` | `$00` (buttons up) |
| `$C064`–`$C067` | `$00` (paddle timers already expired) |
| `$C070` | `$00` |
| every other `$C0xx` address | reads `$00`, writes ignored |

Initial state at power-on: all switches off, text mode, language card reading
ROM with bank 2 selected and writes disabled.

### 5.3 Stub ROM

`$C100`–`$FFFF` is filled with `$60` (`RTS`), then:

- `$FBB3` = `$06`.
- Slot 5 card page: `$C501` = `$20`, `$C503` = `$00`, `$C505` = `$03`,
  `$C507` = `$00`, `$C5FF` = `$80`. This makes the SmartPort entry `$C583`.

Consequences, all intended: every ROM call returns at once; `$FBC0` reads
`$60` (non-zero, so "not a IIc"); `$FE1F` returns with carry unchanged (so
"not a IIGS"). No Apple ROM code is used or shipped.

### 5.4 Block device trap

Before executing each instruction, `machine` checks whether the program
counter equals `$C583`. If so, it runs the handler instead of the CPU:

1. Pull the return address from the emulated stack. The inline parameters
   start one byte after it: command byte, then a two-byte pointer to the
   parameter list.
2. Read the parameter list through the bus: byte 0 is the parameter count,
   byte 1 the unit, bytes 2–3 the buffer address, bytes 4–6 the block number.
3. Act on the command:
   - `$01` READ BLOCK: if the block is inside the image, write its 512 bytes
     to the buffer address **through the bus**, so current banking and
     language-card write state apply. Result: A = `$00`, carry clear.
     Otherwise A = `$27` (I/O error), carry set.
   - `$02` WRITE BLOCK: A = `$2B` (write-protected), carry set.
   - anything else: A = `$01` (bad command), carry set.
4. Set the program counter to the return address plus 4 (past the three
   parameter bytes), leaving the stack as an `RTS` would.

The handler charges a fixed 1,000 cycles per call so that time still advances.

### 5.5 Boot

`boot` replaces the ProDOS boot block. After `machine_init`:

1. Memory is zeroed and switches are in their power-on state.
2. Image blocks 8–15 (4,096 bytes) are copied to main memory at `$2000`.
3. Zero-page `$43` = `$50` (slot 5, drive 1).
4. Registers: PC = `$2000`, S = `$FF`, A = `$00`, X = `$50`, Y = `$00`,
   interrupts disabled, decimal mode off.

`machine_init` halts with a reason if the image is not exactly 819,200 bytes
or block 8 does not begin with `2C`.

### 5.6 Frame loop and timing

- One frame is 17,030 CPU cycles (262 lines of 65 cycles). `machine_run_frame`
  steps instructions until the frame's cycle budget is used, carrying any
  overshoot into the next frame.
- The frame-relative cycle position drives `$C019`.
- No interrupts are ever raised.

### 5.7 Video

`machine_render` chooses a mode from switch state:

| Condition | Output |
|---|---|
| TEXT on, or HIRES off | All dots off (blank) |
| HIRES on, TEXT off, not (80COL and DHIRES) | Hi-res |
| HIRES on, TEXT off, 80COL and DHIRES on | Double hi-res |

MIXED is ignored (the bottom four text rows are rendered as graphics).

Page selection: with 80STORE off, PAGE2 selects `$2000` or `$4000` in main
memory. With 80STORE on, page 1 is always displayed.

- **Hi-res:** each of the 192 rows has 40 bytes. Bits 0–6 of each byte are
  seven pixels, bit 0 leftmost. Each pixel becomes two dots wide. If bit 7 of
  the byte is set, that byte's 14 dots shift right by one dot; the dot shifted
  in on the left repeats the last dot of the previous byte, and the dot shifted
  out past the right edge of the row is dropped.
- **Double hi-res:** each row has 80 bytes interleaved aux, main, aux, main…
  from the same row address. Bits 0–6 of each byte are seven dots, bit 0
  leftmost. Bit 7 is ignored.
- Every row is emitted twice, giving 384 output rows.
- Row addresses use the standard Apple hi-res row interleave.

## 6. Host shell

Two programs built from `host/` with a plain `Makefile` and the system
compiler:

- `pop-sdl <image>`: SDL2 window showing the 560×384 picture at real-time
  speed. For looking at the result by eye.
- `pop-headless <image> --frames N [--dump K:path.pbm ...] [--hash K ...]`:
  runs N frames with no window, optionally writes the picture at frame K as a
  PBM file, and prints an FNV-1a 64-bit hash of the picture at frame K. Exits
  non-zero if the machine halts, printing the halt reason.

## 7. Firmware shell

### 7.1 Project

- PlatformIO, Arduino framework, board `esp32-c3-devkitm-1` with the flash
  size overridden to 16 MB.
- Build flags enable USB CDC serial at boot.
- `core/` is compiled into the firmware as a library; no copy of the sources.
- The disk image is embedded with `board_build.embed_files`, pointing at
  `game/out/PrinceOfPersia_3.5.hdv`. It stays in flash and is read in place.
- A custom partition table gives the application 3 MB.
- Wi-Fi and Bluetooth are never initialised.

### 7.2 Display

- Driver: GxEPD2's `GxEPD2_426_GDEQ0426T82` class used directly through
  `writeImage`, `refresh(x, y, w, h)` and `writeImageAgain`. GxEPD2's paged
  drawing layer is not used.
- The 560×384 picture is placed at panel position (120, 48), landscape.
- At boot: clear the whole panel with one full refresh.
- After that: partial refresh of the game window only.
- A compile-time constant `FULL_REFRESH_EVERY` forces a full refresh after
  that many partial refreshes; 0 disables it. Initial value 0, tuned on the
  device.
- A compile-time flag `INVERT_VIDEO` swaps black and white. Default off (lit
  dots are white on black).

### 7.3 Tasks and pacing

Two FreeRTOS tasks:

- **Emulator task.** Loop: `machine_run_frame()`; if the display task has
  requested a picture, call `machine_render` into the shared buffer and
  signal it; then sleep until the next 16.69 ms tick. If the tick has already
  passed, do not sleep, and count the frame as late. Regardless of lateness,
  the task yields for one scheduler tick at least every 10 frames so the idle
  task and its watchdog are serviced.
- **Display task**, higher priority, mostly blocked. Loop: request a picture;
  wait for the signal; hash the buffer; if the hash differs from the last one
  shown, send it and refresh (the driver blocks until the panel is idle, during
  which the emulator task runs); repeat.

Only one picture buffer exists. The emulator task writes it only between a
request and its signal, and the display task reads it only after the signal,
so no lock is needed beyond the request/signal pair.

### 7.4 Logging

Over USB serial, once per second: emulated cycles per second, late-frame
count, panel refreshes per second, free heap. At boot: image size and the
first 16 bytes of block 8.

### 7.5 Memory budget

| Item | RAM |
|---|---|
| Emulated main and aux memory | 128 KB |
| Picture buffer (560×384) | 27 KB |
| Task stacks, driver state, logging | about 20 KB |
| Disk image | 0 (read from flash) |
| Total | about 175 KB of about 380 KB |

## 8. Game build

`game/` contains:

- `Dockerfile`: `i386/debian:bookworm-slim` with `make` and `patch`.
- `build.sh`: clones the fork at the pinned commit into `game/work/` (or
  reuses it), builds the Docker image for `linux/386`, runs `make clean all`
  inside it, copies `PrinceOfPersia_3.5.hdv` to `game/out/`, and compares its
  SHA-256 with the expected value from 3.3. A mismatch is reported as an error
  with both hashes shown.
- `build.sh --release-patch` runs `make clean all RELEASE_PATCH=1` instead and
  skips the hash comparison. This is the fallback if the unpatched build does
  not reach the title sequence.

`game/work/` and `game/out/` are gitignored. No game binary or game data is
committed to this repository.

## 9. Error handling

- **Halt conditions in the core:** executing an opcode that is not a
  documented NMOS 6502 instruction (checked by `cpu` against a 256-entry table
  before each step); executing `BRK` (`$00`), which the game uses only as a
  deliberate crash point and which is also what runaway execution into zeroed
  memory hits; a bad image at init. The machine status becomes `MACHINE_HALTED`, and the reason string
  records the cause, the program counter, the opcode, and the banking flags.
  `machine_run_frame` does nothing further once halted.
- **Host:** `pop-headless` prints the reason and exits non-zero. `pop-sdl`
  prints the reason and keeps the last picture on screen.
- **Device:** the reason goes to serial every second, and the panel shows a
  one-line "HALTED" message with the program counter, drawn with a small
  built-in bitmap font, via one full refresh.
- **Disk errors** are returned to the game as SmartPort error codes (5.4); the
  game shows its own "insert disk" behaviour.
- **Running slow** is logged (late-frame count), never hidden.

## 10. Testing

All automated tests run on the Mac with `make test` at the repository root.
Tests that need the disk image are skipped with a clear message if
`game/out/PrinceOfPersia_3.5.hdv` is absent.

| Test | Proves |
|---|---|
| Klaus Dormann 6502 functional test | The vendored CPU core executes documented opcodes correctly, including decimal mode |
| Bus banking tests | RAMRD, RAMWRT, ALTZP, 80STORE with PAGE2 and HIRES, and each language-card mode map reads and writes to the right memory |
| Language-card write-enable test | Two consecutive reads are needed to enable writes; one is not enough |
| Vertical-blank test | `$C019` bit 7 is set for cycles 0–12,479 of a frame and clear for 12,480–17,029 |
| Block device tests | READ copies the right 512 bytes through the bus, sets A and carry, and resumes after the inline parameters; WRITE and out-of-range reads return the right errors |
| Video tests | Known hi-res bytes, with and without bit 7, and known double hi-res byte pairs produce the expected dots; rows are doubled; page selection follows the switches |
| Boot smoke test | From the real image, within 3,000 frames the machine has not halted, is in a graphics mode, and renders a picture with at least 1,000 lit dots |
| Golden frames | Hashes of the picture at fixed frame numbers match recorded values for one title screen and one frame of the demo level |

The Klaus Dormann test binary is fetched by a script into a gitignored
directory, not committed.

Golden hashes are recorded once in milestone 4, after the pictures have been
checked by eye in the SDL window, and stored in `tests/golden.txt`.

On the device, verification is by eye plus the serial counters in 7.4.

## 11. Milestones

| # | Milestone | Done when |
|---|---|---|
| 1 | Repository scaffold and game build | `game/build.sh` produces the image with the expected hash |
| 2 | CPU and bus on host | Functional test, banking, language-card and vertical-blank tests pass |
| 3 | Boot on host | Block device tests pass and the boot smoke test reaches a graphics mode |
| 4 | Video on host | Video tests pass; title sequence and demo level visible in `pop-sdl`; golden hashes recorded and passing |
| 5 | Device bring-up | Existing flash backed up with `esptool`; firmware boots; serial shows emulated speed at or above 1,020,000 cycles per second |
| 6 | E-ink output | Title sequence and attract-mode demo visible on the X4 panel |

Milestones 1–4 need no hardware.

## 12. Risks

| Risk | Likelihood | Response |
|---|---|---|
| fake6502 fails the functional test | Medium | Fix the specific defects, or replace it behind the `cpu` interface with another permissively licensed single-file core |
| The unpatched build does not reach the title sequence | Low | Use `build.sh --release-patch` |
| The game depends on a soft switch or ROM behaviour not listed here | Medium | The halt reason and a host-side trace option locate it; add the behaviour to `softswitch` or `rom` |
| Emulated speed on the ESP32-C3 is below real time | Low | Measured in milestone 5 before display work; optimise the bus fast path or change CPU core |
| Partial refresh is slower or ghosts more than documented | Medium | Tune `FULL_REFRESH_EVERY` and `INVERT_VIDEO`; accept a lower frame rate |
| `embed_files` does not work with the Arduino framework build | Low | Embed the image with an assembly file using `.incbin` |
| GxEPD2's driver for this panel does not suit the X4's wiring or timing | Low | Fall back to a small direct SSD1677 driver using the command sequences documented by the Papyrix and CrossPoint projects |

## 13. Legal note

Jordan Mechner published the source for study and modification and stated
that this is not a grant of rights in Prince of Persia, which belongs to
Ubisoft. This project builds the game locally for personal use and never
commits or distributes the game binary, game data, or any Apple ROM.
