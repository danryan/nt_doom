# CLAUDE.md

Guidance for Claude Code working in nt_doom.

## What this repo is

A from-scratch, clean-room compact Doom-WAD engine that loads and renders real
Doom WAD content on the Expert Sleepers disting NT Eurorack module. It is NOT a
port of the original Doom source: real Doom's `.text` is hundreds of KB and the NT
caps a plug-in at roughly 82 KB of `.text`, so the engine is built compact from
scratch. The WAD data is not the wall; the code size is.

Clean-room rule: no GPL Doom / PureDOOM source is copied. PureDOOM informed the
file-IO callback shape only. Never commit a real `DOOM1.WAD` (shareware
redistribution is murky); tests use a synthetic WAD built by `harness/tools/wad_build`.

Vendor is two submodules, both effectively read-only: `vendor/distingNT_API` (the
NT plug-in ABI) and `vendor/llvm-project` (compiler-rt builtins, sparse-checkout of
`compiler-rt/lib/builtins` only, pinned at `llvmorg-19.1.0`).

## Bootstrap

```sh
./bootstrap.sh     # or: make vendor
```

Provisions toolchain checks, python deps (mido + python-rtmidi), and both
submodules. The llvm-project sparse-checkout is NOT carried by a plain
`git submodule update`; `bootstrap.sh` clones it sparsely and re-applies the sparse
config on every run. Required tools: `arm-none-eabi-c++`/`gcc`/`ld`/`objcopy`/`nm`,
`clang++` or `g++`, `python3`.

## Build and test

| Command | Purpose |
| --- | --- |
| `make test` | Build + run all host Catch2 tests (WAV wrapper, WAD parser, renderer) |
| `make arm` | Build all NT plug-ins under `build/arm/*.o` |
| `make deploy-sysex SYSEX_PLUGIN=build/arm/<name>.o` | Push a plug-in over USB-MIDI sysex |
| `make clean` | Remove `build/` |

Single Catch2 case: `./build/host/test_doom_render '[render]'` (build first via
`make build/host/test_doom_render`).

## Layout

- `plugins/games/doom/*.h` is the engine: `wad.h` (clean-room WAD container parse),
  `geom.h` (zero-copy map view over WAD lumps), `fb.h` (4-bit framebuffer packing),
  `render.h` (perspective wall projection + affine texture + distance shading).
- `plugins/games/doom_core_spike.cpp` is the non-interactive renderer-core plug-in
  (GUID `DmSc`), built by the `BUILD_GAME` Makefile macro.
- `plugins/probes/` holds the two hardware spikes: `wad_read_probe.cpp` (GUID
  `WdRd`, the SD file door) and `dram_grant_probe.cpp` (GUID `DrAm`, the DRAM grant).
- `harness/tools/` are host tools (`wav_wrap`, `wad_build`); `harness/tests/` are
  Catch2 binaries; `harness/scripts/` are the hardware-deploy and screenshot scripts.
- `docs/superpowers/` carries the P0 spec, plan, and the spike results report.

## Proven constraints (P0 spikes, validated on hardware)

- WAD smuggle is via a 16-bit mono WAV in a samples folder. The NT sample browser
  REJECTS 8-bit WAV. `NT_readSampleFrames` returns the PCM byte-exact when the
  requested bit depth matches the file's native depth (no dither/scale/convert), at
  offset 0 and at deep offsets. Two WAD bytes pack into one little-endian int16
  sample; strip the 44-byte WAV header and any odd-byte pad on read.
- DRAM: a single plug-in instance grants at least 12 MB (sentinel-verified); 16 MB
  fails to add. Budget the WAD plus working buffers under 12 MB. DRAM size is cached
  at scan time, so a size change needs a reboot before it takes effect.
- Code: the renderer core is about 2.4 KB `.text`, far under the ~82 KB per-`.o`
  cap. Keep each plug-in under that cap; there is no code offload to DRAM (the
  loader ignores non-canonical executable sections).

## NT plug-in build mechanics

- Plug-ins are `-fPIC`. The firmware applies relocations at on-device link time and
  expects all-PIC linkage. The toolchain ships no PIC `libgcc` for `v7e-m+dp/hard`,
  so 64-bit divide / float-to-int / popcount builtins come from compiler-rt,
  compiled with `arm-none-eabi-gcc` (NOT `c++`, which mangles EABI symbol names) and
  partial-linked via `ld -r`.
- The firmware resolves at load: the `NT_*` ABI, `_GLOBAL_OFFSET_TABLE_`, and newlib
  `memcpy`/`memset`/`memmove`/`strlen`/`strcmp`. It does NOT resolve `memcmp` (the
  WAD parser uses inline byte compares for that reason), `snprintf`/`vsnprintf`
  (format integers inline), or `__aeabi_d2lz` (compiler-rt covers it). Check with
  `arm-none-eabi-nm <plugin>.o | grep ' U '`.
- `BUILD_GAME` merges gcc's split `.text.<mangled>` COMDAT sections into ~6 sections
  via `merge_sections.lds` before stripping (`objcopy --remove-section='.group'` then
  `ld -r -T merge_sections.lds`). The section count is not a firmware cap but the
  merged artifact is cleaner to inspect. Use `arm-none-eabi-readelf -W -S` (not
  `objdump -h`, which truncates) to read full mangled section names.

The sections below are generic disting-NT plug-in knowledge, useful for any NT
plug-in, not just this engine.

## Memory and code-size limits (firmware)

- Scan-time `.text` cap: the firmware refuses to register a plug-in whose `.text`
  exceeds roughly 82 KB. Misc > Plug-ins > View Info shows "Not enough memory for
  .text : <name>" and marks the entry Failed. Empirically 81566 B loads, 83448 B
  fails. Section count is not the cap (a 12-section 81 KB build loaded; a 14-section
  83 KB build failed). Each `.o` is checked independently.
- Run-time ITC pool: each loaded slot copies its plug-in `.text` into ITC RAM. The
  total across all loaded slots must fit a shared hardware budget (empirically about
  100 KB). The error path is identical when adding an algorithm to a preset. There is
  no API to share `.text` across plug-ins (`calculateStaticRequirements` shares DATA
  only).
- No code offload: the loader copies only the canonical `.text` into the fixed ITC
  buffer. It does NOT recognize custom executable section names (`.code_dram`,
  `.text.cold`). Functions tagged `__attribute__((section(".code_dram")))` land at
  unmapped addresses; the plug-in may register but hard-faults when `step()`/`draw()`
  jumps into them. There is no way to dodge the `.text` cap by routing cold code
  elsewhere. Shrink code (Q15 LUTs, drop `printf`/`snprintf`, prune unused deps) or
  split the work across plug-ins.
- SRAM-size cache invalidation: the firmware caches `calculateRequirements` (SRAM and
  DRAM sizes) at scan time. Enlarging any `_NT_algorithm` subclass or a requested
  DRAM size needs a power cycle or reboot (`0x7F`) before the new build behaves. The
  plug-in rescan (`0x7A 08`, what `deploy-sysex` sends) re-registers `.text` but does
  NOT re-read `calculateRequirements`. Without the reboot the runtime allocates the
  old size, `construct()` overruns adjacent memory, and the plug-in faults on add or
  returns garbage that can masquerade as correct behavior.

## Firmware ABI contract and add-time hazards

A plug-in can pass the host build, register cleanly, and still fault when ADDED to a
preset. Always verify ADD on hardware, not just registration.

- `_NT_factory` field order matters with C++20 designated initializers. The struct
  orders `tags` before `hasCustomUi`/`customUi`; initializers must follow declaration
  order. Keep `.guid, .name, .description, .numSpecifications, .calculateRequirements,
  .construct, .step, .draw, .tags` in that order.
- `_NT_parameter` min/max/def and `_NT_algorithm::v[]` are `int16_t`
  (-32768..32767). A setting whose value exceeds that cannot be an NT parameter
  without truncation; expose only int16-safe settings as parameters and keep the rest
  internal (persist them through the serialise blob).
- `numParameters` must equal the count of actually-populated `parameters[]` entries.
  The firmware reads `parameters[i]` for `i` in `[0, numParameters)` without
  validating each; entries past the populated range are zero-init and appear as blank,
  unsettable phantom rows.
- `_NT_parameter.name` must point at firmware-readable memory (`.rodata` string
  literals, or a buffer inside the firmware-allocated per-instance SRAM block), NEVER
  at the plug-in's global `.bss`. The firmware dereferences `name` during
  add-algorithm to build its UI; a pointer into plug-in `.bss` hard-faults the device
  (MemManage, PC in the firmware region). Plug-in-internal `.bss` access by the
  plug-in itself is fine; the fault is specifically the firmware reading a `.bss`
  pointer the plug-in handed it. For dynamic or prefixed names, store the name buffer
  as a member of the instance struct (which lives in firmware-allocated SRAM).
- `serialise()` must write through `_NT_jsonStream::addNumber` only, never
  `addString`. The firmware calls `serialise` during add-algorithm and `addString`
  faults there (surfaces as "Failed to add algorithm"). Pack any blob as numbers.
- Construct-time `parameterChanged`: the firmware fires `parameterChanged` for each
  parameter during construct, before the algorithm is fully registered. Calling
  `NT_setParameterFromUi` back into self from that spurious fire hard-crashes on add.
  Guards on `v != nullptr` or `NT_algorithmIndex(self) >= 0` are NOT sufficient (both
  look valid during construct). Gate forwarding behind a sentinel that flips true only
  once the algorithm is genuinely alive (e.g. after `draw()` has run at least once, or
  `customUi` has armed a flag). `NT_setParameterFromUi` does not re-enter
  `parameterChanged` synchronously, so no stack-reentry guard is needed.
- `NT_setParameterFromUi` indexes the GLOBAL parameter table: add
  `NT_parameterOffset()` to the plug-in-relative index when pushing a value back from
  custom UI, or the edit lands on the wrong row.
- `step()` may run before the sample rate is set (the firmware calls it during add).
  Any loop keyed on `NT_globals.sampleRate` must guard `if (sampleRate == 0) return;`
  or it spins forever and the add hangs.

## Bus and step ABI

- `step(self, busFrames, numFramesBy4)`: `numFrames = numFramesBy4 * 4`. There are 28
  buses laid out contiguously (all `numFrames` of bus 0, then bus 1, ...), so
  `busFrames[bus * numFrames + frame]`.
- Bus scale is 1.0f == 1 V, 0 V at zero, confirmed on hardware at about 1 mV
  resolution. Reading a bus the same frame an earlier slot wrote it (slot order is
  load-bearing: the writer must run before the reader) is the basis for cable-free
  hardware-in-the-loop verification.
- `draw()` writes `NT_screen` (256x64, 4-bit gray, 2 px/byte; even x in the high
  nibble, odd x in the low nibble of `NT_screen[y*128 + x/2]`). Returning `true` from
  `draw()` suppresses the firmware parameter line at the top of the screen.

## Diagnosing failed loads and unresolved symbols

- Misc > Plug-ins > View Info lists pass/fail per `.o` with ITC/DTC/DRAM stats but
  does NOT expand a per-plug-in failure reason on a Failed entry. Bisect a
  size-related failure by building at decreasing sizes and re-checking.
- To confirm a plug-in REGISTERED (vs silently rejected), enumerate over sysex rather
  than reading the screen: command `0x30` returns the algorithm count, `0x31 <index>`
  returns name, 4-byte GUID, and an `isPlugin` byte. Match your GUID. There is NO
  sysex opcode that returns a scan-failure reason; a rejected plug-in just does not
  appear.
- Enumerate unresolved symbols with `arm-none-eabi-nm <plugin>.o | grep ' U '`. Only
  the firmware-resolved set (see build mechanics above) should remain; anything else
  is a missing builtin or a typo'd `NT_*` name.
- Diagnose first on opaque errors. For "relocation of non-loaded section" warnings,
  run `arm-none-eabi-objdump -r <o> | awk '$2 ~ /^R_ARM/ {print $2}' | sort | uniq
  -c` to enumerate actual relocation types: `R_ARM_GOT32`/`R_ARM_GOTPC` reveal PIC
  code, `R_ARM_ABS32` reveals direct addressing, and mixing the two in one `.o` is the
  failure pattern (a non-PIC object linked into a PIC plug-in). One `objdump -r` beats
  five strip-and-redeploy cycles. Keep a minimal symbol-probe plug-in in tree to
  re-verify the resolved-symbol set after toolchain or firmware updates.

## Firmware overlay overdraw (custom full-screen UI)

The firmware paints its helper-text overlay onto `NT_screen` after `draw()` returns.
To suppress it without `hasCustomUi`: in `draw()`, snapshot the bottom rows of
`NT_screen` into a per-instance cache; in `step()` (which runs after the overlay pass
and before the next flush), restore them while a short post-draw counter is active (a
navigate-away guard). Reset the counter in `draw()`, increment it in `step()`.

## Host testing patterns

- One shared TU defines Catch2's `main` (`harness/src/catch_main.cpp`); test files
  include the Catch2 header and declare `TEST_CASE` only. A second
  `main`/`CATCH_CONFIG_MAIN` causes a linker collision.
- Gate any host-test injection seam behind a sim-only `-D` flag so production code
  paths stay unchanged: tests populate the seam, the plug-in's real path reads from it
  only under the flag.
- Makefile prerequisite expansion timing: `$(VAR)` in a prerequisite list expands at
  rule-parse time. A `:=` variable defined AFTER its first use in a prereq expands to
  empty. Define variables above the first rule that references them.
- Host-green is NOT hardware-proof. The host harness does not model the firmware
  add-time hazards above (`parameter.name` dereference, construct-time
  `parameterChanged`, `serialise`, `sampleRate == 0`, SRAM cache). A clean host run
  plus clean registration does not prove a plug-in will ADD; confirm on hardware.
- For a no-logic-change ARM refactor, `.text` is not byte-identical (the section-merge
  step relocates functions). Gate such refactors on an `objdump -d` mnemonic-histogram
  identity check, not a byte diff.

## Hardware deploy loop (gotchas)

- `make deploy-sysex` uploads a `.o` then the device must REBOOT to register a new
  or changed GUID (the `0x7A 08` rescan alone is not reliable for that). Reboot
  sysex: `F0 00 21 27 6D 00 7F F7` (send via `mido`).
- Every reboot pops a sample-scan modal that needs a physical button press, and
  makes nt_helper's algorithm catalog stale. A brand-new GUID needs a full nt_helper
  RESTART (disable/enable), not just a reconnect; an already-known GUID re-adds after
  a reconnect.
- `make deploy-sysex` rebuilds its plug-in prerequisite. For the sized
  `dram_grant_probe` (a FORCE rule), build the sized `.o` first, then push it
  directly with `python3 harness/scripts/push_plugin_to_device.py 0 <o>` so the
  deploy make does not silently rebuild it at the default `DRAM_MB`.
- The NT screen can be captured headlessly over sysex with
  `harness/scripts/nt_screenshot.py` (no nt_helper needed). nt_helper's `add`
  sometimes returns "did not appear" even when the slot DID add; verify with a
  screenshot, not the add return.
- `harness/scripts/push_file_to_device.py` uploads any local file to an arbitrary
  `/samples/...` path over sysex, so a test WAV can be placed without USB disk mode.

## Workflow

Non-trivial changes follow brainstorm -> spec -> plan -> TDD -> verify, with docs
under `docs/superpowers/`. TDD is the default: a failing host test before engine
code. Hardware smoke check happens after PR open since it needs physical access.

## Markdown

After editing any `.md`, run `markdownlint <file>` and fix errors.
