# Makefile for Prince of Persia on the Xteink X4
# Targets follow the workflow:
#   game → test → host tools → firmware → backup → flash → monitor
SERVICE = Prince of Persia on the Xteink X4

# Variables
CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra
CPPFLAGS += -Icore -Itests

BUILD := build
IMAGE := game/out/PrinceOfPersia_3.5.hdv
KLAUS := tests/data/6502_functional_test.bin
BACKUP := backup/x4-flash-backup.bin

CORE := core/cpu.c core/bus.c core/softswitch.c core/rom.c core/blockdev.c \
        core/boot.c core/video.c core/machine.c
CORE_DEPS := $(wildcard core/*.h core/*.inc)
HOST_COMMON := host/read_file.c
TESTS := test_bus test_softswitch test_cpu test_blockdev test_video \
         test_scale test_idle test_machine test_boot

# Device: first USB serial port unless port=... is given.
port ?= $(firstword $(wildcard /dev/cu.usbmodem*))
ESPTOOL := $(HOME)/.platformio/penv/bin/esptool --chip esp32c3 -p $(port) -b 921600
FW_BUILD := firmware/.pio/build/x4
BOOT_APP0 := $(HOME)/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin
# fast=1: custom fast waveform (MAY DAMAGE THE PANEL). halt=1: halt self-test.
FW_FLAGS := $(if $(fast),-DFAST_LUT=1) $(if $(halt),-DSELF_TEST_HALT=1)

.PHONY: help game test soak host run firmware backup flash monitor restore clean

# ── Help ─────────────────────────────────────────────────────────────────────

help: ## Print this help message
	@printf '\033[01;32m${SERVICE}\033[00;37m\n\n'
	@printf "\033[33mUsage:\033[0m\n  make [target] [arg=\"val\"...]\n\n\033[33mTargets:\033[0m\n"
	@grep -E '^[-a-zA-Z0-9_\.\/]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; \
		{printf "  \033[36m%-10s\033[0m %s\n", $$1, $$2}'

# ── Game image ───────────────────────────────────────────────────────────────

game: ## Build the disk image from source in Docker (release=1 for the retail patch)
	game/build.sh $(if $(release),--release-patch)

$(IMAGE):
	game/build.sh

# ── Tests ────────────────────────────────────────────────────────────────────

test: $(addprefix $(BUILD)/,$(TESTS)) $(KLAUS) ## Run unit tests, boot smoke test and golden frames
	$(BUILD)/test_bus
	$(BUILD)/test_softswitch
	$(BUILD)/test_cpu $(KLAUS)
	$(BUILD)/test_blockdev
	$(BUILD)/test_video
	$(BUILD)/test_scale
	$(BUILD)/test_idle
	$(BUILD)/test_machine
	$(BUILD)/test_boot $(IMAGE) tests/golden.txt

# 260,000 frames is 4.4 billion cycles: past the CPU core's 32-bit counter wrap.
soak: $(BUILD)/pop-headless $(IMAGE) ## Run 260,000 frames (~70 emulated minutes) headless
	$(BUILD)/pop-headless $(IMAGE) --frames 260000

# Each test links only the code it exercises.
$(BUILD)/test_bus: tests/test_bus.c core/bus.c core/rom.c
$(BUILD)/test_softswitch: tests/test_softswitch.c core/bus.c core/rom.c core/softswitch.c
$(BUILD)/test_cpu: tests/test_cpu.c core/cpu.c core/bus.c core/rom.c core/softswitch.c
$(BUILD)/test_blockdev: tests/test_blockdev.c core/blockdev.c core/cpu.c core/bus.c core/rom.c core/softswitch.c
$(BUILD)/test_video: tests/test_video.c core/video.c core/bus.c core/rom.c core/softswitch.c
$(BUILD)/test_scale: CPPFLAGS += -Ifirmware/src
$(BUILD)/test_scale: tests/test_scale.c firmware/src/scale.c
$(BUILD)/test_idle: CPPFLAGS += -Ifirmware/src
$(BUILD)/test_idle: tests/test_idle.c firmware/src/idle.c
$(BUILD)/test_machine: tests/test_machine.c $(CORE)
$(BUILD)/test_boot: tests/test_boot.c $(CORE)

$(BUILD)/test_%: tests/test.h $(CORE_DEPS)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(filter %.c,$^)

$(KLAUS):
	tests/fetch_klaus.sh

# ── Host tools ───────────────────────────────────────────────────────────────

host: $(BUILD)/pop-headless $(BUILD)/pop-sdl ## Build the headless runner and the SDL viewer

run: $(BUILD)/pop-sdl $(IMAGE) ## Watch the game in an SDL window at real-time speed
	$(BUILD)/pop-sdl $(IMAGE)

$(BUILD)/pop-headless: host/headless.c $(HOST_COMMON) $(CORE) $(CORE_DEPS)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ host/headless.c $(HOST_COMMON) $(CORE)

$(BUILD)/pop-sdl: host/sdl_main.c $(HOST_COMMON) $(CORE) $(CORE_DEPS)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(shell sdl2-config --cflags) -o $@ host/sdl_main.c $(HOST_COMMON) $(CORE) $(shell sdl2-config --libs)

# ── Device ───────────────────────────────────────────────────────────────────

# The image is embedded with .incbin, which PlatformIO does not track, so a
# newer image forces a clean build.
firmware: $(IMAGE) ## Build the X4 firmware (fast=1 fast waveform, halt=1 halt self-test)
	@if [ -f $(FW_BUILD)/firmware.bin ] && [ $(IMAGE) -nt $(FW_BUILD)/firmware.bin ]; then \
		cd firmware && pio run -t clean; \
	fi
	cd firmware && PLATFORMIO_BUILD_FLAGS="$(FW_FLAGS)" pio run

backup: ## Save the X4's whole 16 MB flash to backup/ (do this before the first flash)
	@test -n "$(port)" || { echo "No X4 found; plug it in or pass port=..."; exit 1; }
	@test ! -f $(BACKUP) || { echo "$(BACKUP) exists; move it before taking another"; exit 1; }
	@mkdir -p backup
	$(ESPTOOL) read-flash 0x000000 0x1000000 $(BACKUP)

flash: firmware ## Write the firmware to the X4 (same flags as firmware)
	@test -n "$(port)" || { echo "No X4 found; plug it in or pass port=..."; exit 1; }
	@test -f $(BACKUP) || { echo "No $(BACKUP): run 'make backup' first"; exit 1; }
	$(ESPTOOL) write-flash 0x0 $(FW_BUILD)/bootloader.bin 0x8000 $(FW_BUILD)/partitions.bin \
		0xe000 $(BOOT_APP0) 0x10000 $(FW_BUILD)/firmware.bin

monitor: ## Show the X4's serial log (Ctrl+C to quit)
	cd firmware && pio device monitor -p $(port) -b 115200

restore: ## Put the original e-reader firmware back from backup/
	@test -f $(BACKUP) || { echo "No $(BACKUP) to restore"; exit 1; }
	$(ESPTOOL) write-flash 0x000000 $(BACKUP)

# ── Cleanup ──────────────────────────────────────────────────────────────────

clean: ## Remove host and firmware build output
	rm -rf $(BUILD)
	cd firmware && pio run -t clean > /dev/null
