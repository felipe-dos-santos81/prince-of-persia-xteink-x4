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
         test_scale test_machine test_boot

.PHONY: all test soak host game clean
all: host

# Each test links only the core files it exercises.
$(BUILD)/test_bus: tests/test_bus.c core/bus.c core/rom.c
$(BUILD)/test_softswitch: tests/test_softswitch.c core/bus.c core/rom.c core/softswitch.c
$(BUILD)/test_cpu: tests/test_cpu.c core/cpu.c core/bus.c core/rom.c core/softswitch.c
$(BUILD)/test_blockdev: tests/test_blockdev.c core/blockdev.c core/cpu.c core/bus.c core/rom.c core/softswitch.c
$(BUILD)/test_video: tests/test_video.c core/video.c core/bus.c core/rom.c core/softswitch.c
# The scaler belongs to the firmware but is plain C, so it is tested here.
$(BUILD)/test_scale: CPPFLAGS += -Ifirmware/src
$(BUILD)/test_scale: tests/test_scale.c firmware/src/scale.c
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
	$(BUILD)/test_scale
	$(BUILD)/test_machine
	$(BUILD)/test_boot $(IMAGE) tests/golden.txt

host: $(BUILD)/pop-headless $(BUILD)/pop-sdl

$(BUILD)/pop-headless: host/headless.c host/read_file.c host/read_file.h $(CORE) $(CORE_DEPS)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ host/headless.c host/read_file.c $(CORE)

$(BUILD)/pop-sdl: host/sdl_main.c host/read_file.c host/read_file.h $(CORE) $(CORE_DEPS)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(shell sdl2-config --cflags) -o $@ host/sdl_main.c host/read_file.c $(CORE) $(shell sdl2-config --libs)

# 260,000 frames is 4.4 billion CPU cycles: past the point where the CPU
# core's 32-bit cycle counter wraps (about 70 minutes of emulated time).
soak: $(BUILD)/pop-headless
	$(BUILD)/pop-headless $(IMAGE) --frames 260000

game:
	game/build.sh

clean:
	rm -rf $(BUILD)
