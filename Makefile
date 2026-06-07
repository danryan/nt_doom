# nt_doom: a from-scratch compact Doom-WAD engine for the Expert Sleepers disting NT.
.PHONY: help vendor host arm test clean deploy-sysex FORCE

ARM_CXX  := arm-none-eabi-c++
ARM_CC   := arm-none-eabi-gcc
ARM_LD   := arm-none-eabi-ld
HOST_CXX := $(shell command -v clang++ >/dev/null 2>&1 && echo clang++ || echo g++)

NT_API_INCLUDE := vendor/distingNT_API/include

# DRAM grant probe (Spike B): requested grant in MB, overridable per bisection step.
DRAM_MB ?= 4

ARM_FLAGS := -std=c++17 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard \
             -mthumb -fno-rtti -fno-exceptions -fno-threadsafe-statics \
             -fno-unwind-tables -fno-asynchronous-unwind-tables \
             -ffunction-sections -fdata-sections \
             -fvisibility=hidden -fvisibility-inlines-hidden \
             -fmerge-all-constants \
             -Os -fPIC -Wall \
             -I$(NT_API_INCLUDE)

# compiler-rt files compiled as C (no name mangling) with -fPIC to match the
# plug-in PIC code model. -fno-rtti etc are C++-only, so drop them here.
ARM_CFLAGS := -std=c99 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard \
              -mthumb -Os -fPIC -Wall -I$(NT_API_INCLUDE)

HOST_FLAGS := -std=c++17 -fno-rtti -fno-exceptions -Wall -O2 \
              -Iharness/include -I$(NT_API_INCLUDE)

# Doom host tests are pure host logic (header-only engine + std); they need only
# the Catch2 main, not an NT runtime simulator.
HARNESS_SRCS := harness/src/catch_main.cpp

help:
	@echo "make vendor       - provision toolchain checks + submodules (runs ./bootstrap.sh)"
	@echo "make test         - build and run all host Catch2 tests"
	@echo "make arm          - build all NT plug-ins under build/arm/*.o"
	@echo "make deploy-sysex SYSEX_PLUGIN=build/arm/<name>.o - push a plug-in over USB-MIDI sysex"
	@echo "make clean        - remove build/"

vendor:
	./bootstrap.sh

# ---------------------------------------------------------------------------
# compiler-rt builtins (PIC). The toolchain ships no PIC libgcc multilib for
# v7e-m+dp/hard, so the EABI 64-bit divide / float-to-int / popcount builtins
# are sourced from vendor/llvm-project/compiler-rt/lib/builtins (sparse,
# llvmorg-19.1.0) and partial-linked into each plug-in.
COMPILER_RT_DIR := vendor/llvm-project/compiler-rt/lib/builtins
COMPILER_RT_SRCS := \
    $(COMPILER_RT_DIR)/divdi3.c \
    $(COMPILER_RT_DIR)/udivdi3.c \
    $(COMPILER_RT_DIR)/divmoddi4.c \
    $(COMPILER_RT_DIR)/udivmoddi4.c \
    $(COMPILER_RT_DIR)/fixdfdi.c \
    $(COMPILER_RT_DIR)/fixunsdfdi.c \
    $(COMPILER_RT_DIR)/popcountdi2.c \
    $(COMPILER_RT_DIR)/arm/aeabi_div0.c \
    $(COMPILER_RT_DIR)/arm/aeabi_ldivmod.S \
    $(COMPILER_RT_DIR)/arm/aeabi_uldivmod.S
COMPILER_RT_OBJS := \
    $(patsubst $(COMPILER_RT_DIR)/%.c,build/arm/compiler_rt/%.o,$(filter %.c,$(COMPILER_RT_SRCS))) \
    $(patsubst $(COMPILER_RT_DIR)/%.S,build/arm/compiler_rt/%.o,$(filter %.S,$(COMPILER_RT_SRCS)))

build/arm/compiler_rt/%.o: $(COMPILER_RT_DIR)/%.c
	mkdir -p $(@D)
	$(ARM_CC) $(ARM_CFLAGS) -I$(COMPILER_RT_DIR) -c -o $@ $<

build/arm/compiler_rt/%.o: $(COMPILER_RT_DIR)/%.S
	mkdir -p $(@D)
	$(ARM_CC) $(ARM_CFLAGS) -I$(COMPILER_RT_DIR) -c -o $@ $<

# ---------------------------------------------------------------------------
# Games: standalone, vendor-source-free plug-ins under plugins/games/.
# Compile, partial-link with compiler-rt, drop COMDAT groups, merge the split
# .text.<mangled> sections into ~6 canonical sections, then strip metadata.
# $(1) = game name (file plugins/games/<name>.cpp).
define BUILD_GAME
build/arm/$(1).o: plugins/games/$(1).cpp $$(wildcard plugins/games/doom/*.h) plugins/games/doom_test_map.h $$(COMPILER_RT_OBJS) merge_sections.lds
	mkdir -p build/arm
	$$(ARM_CXX) $$(ARM_FLAGS) -Iplugins/games -c -o build/arm/$(1).raw.o $$<
	$$(ARM_LD) -r --strip-debug build/arm/$(1).raw.o $$(COMPILER_RT_OBJS) -o build/arm/$(1).merge1.o
	arm-none-eabi-objcopy --remove-section='.group' build/arm/$(1).merge1.o build/arm/$(1).nogroup.o
	$$(ARM_LD) -r -T merge_sections.lds build/arm/$(1).nogroup.o -o build/arm/$(1).linked.o
	arm-none-eabi-objcopy -R '.ARM.extab*' -R '.ARM.exidx*' -R '.rel.ARM.exidx*' -R '.ARM.attributes' -R '.comment' -R '.group' -R '.note.GNU-stack' -R '.eh_frame' -R '.eh_frame_hdr' build/arm/$(1).linked.o $$@
endef

GAME_LIST := doom_core_spike
PRESENT_GAMES := $(foreach g,$(GAME_LIST),$(if $(wildcard plugins/games/$(g).cpp),$(g),))
$(foreach g,$(PRESENT_GAMES),$(eval $(call BUILD_GAME,$(g))))

# ---------------------------------------------------------------------------
# Probes (hardware spikes). Single compile + strip; no compiler-rt needed (the
# probes use only integer + firmware-resolved symbols).
build/arm/wad_read_probe.o: plugins/probes/wad_read_probe.cpp
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS) -c -o build/arm/wad_read_probe.raw.o $<
	arm-none-eabi-objcopy -R '.ARM.extab*' -R '.ARM.exidx*' -R '.rel.ARM.exidx*' -R '.ARM.attributes' -R '.comment' -R '.note.GNU-stack' -R '.eh_frame' -R '.eh_frame_hdr' build/arm/wad_read_probe.raw.o $@

# FORCE: the requested size is a -D macro, not a tracked file dependency, so the
# rule must always rebuild or a DRAM_MB change silently reuses the prior size.
build/arm/dram_grant_probe.o: plugins/probes/dram_grant_probe.cpp FORCE
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS) -DDRAM_REQUEST_MB=$(DRAM_MB) -c -o build/arm/dram_grant_probe.raw.o $<
	arm-none-eabi-objcopy -R '.ARM.extab*' -R '.ARM.exidx*' -R '.rel.ARM.exidx*' -R '.ARM.attributes' -R '.comment' -R '.note.GNU-stack' -R '.eh_frame' -R '.eh_frame_hdr' build/arm/dram_grant_probe.raw.o $@

FORCE:

arm: $(addprefix build/arm/,$(addsuffix .o,$(PRESENT_GAMES))) build/arm/wad_read_probe.o build/arm/dram_grant_probe.o

# ---------------------------------------------------------------------------
# Host tests + tools.
build/host/test_wav_wrap: harness/tests/test_wav_wrap.cpp harness/tools/wav_wrap.h $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ harness/tests/test_wav_wrap.cpp $(HARNESS_SRCS)

build/host/test_wad: harness/tests/test_wad.cpp harness/tools/wad_build.h plugins/games/doom/wad.h $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ harness/tests/test_wad.cpp $(HARNESS_SRCS)

build/host/test_doom_render: harness/tests/test_doom_render.cpp harness/tools/wad_build.h plugins/games/doom/wad.h plugins/games/doom/geom.h plugins/games/doom/render.h plugins/games/doom/fb.h $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ harness/tests/test_doom_render.cpp $(HARNESS_SRCS)

build/host/test_palette: harness/tests/test_palette.cpp harness/tools/wad_build.h plugins/games/doom/wad.h $(wildcard plugins/games/doom/palette.h) $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ harness/tests/test_palette.cpp $(HARNESS_SRCS)

build/host/test_arena: harness/tests/test_arena.cpp $(wildcard plugins/games/doom/arena.h) $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ harness/tests/test_arena.cpp $(HARNESS_SRCS)

build/host/test_wad_read: harness/tests/test_wad_read.cpp harness/tools/wad_build.h harness/tools/wav_wrap.h plugins/games/doom/wad.h plugins/games/doom/geom.h plugins/games/doom/arena.h $(wildcard plugins/games/doom/wad_read.h) $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ harness/tests/test_wad_read.cpp $(HARNESS_SRCS)

build/host/wav_wrap: harness/tools/wav_wrap.cpp harness/tools/wav_wrap.h
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ harness/tools/wav_wrap.cpp

build/host/wad_build: harness/tools/wad_build.cpp harness/tools/wad_build.h
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ harness/tools/wad_build.cpp

host: build/host/test_wav_wrap build/host/test_wad build/host/test_doom_render \
      build/host/test_palette build/host/test_arena build/host/test_wad_read

test: host
	./build/host/test_wav_wrap
	./build/host/test_wad
	./build/host/test_doom_render
	./build/host/test_palette
	./build/host/test_arena
	./build/host/test_wad_read

# ---------------------------------------------------------------------------
# Hardware deploy over USB-MIDI sysex (NT firmware v1.13+).
SYSEX_ID ?= 0
SYSEX_PLUGIN ?= build/arm/doom_core_spike.o
deploy-sysex: $(SYSEX_PLUGIN)
	python3 harness/scripts/push_plugin_to_device.py $(SYSEX_ID) $(SYSEX_PLUGIN)

clean:
	rm -rf build
