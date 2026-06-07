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
  via `merge_sections.lds` before stripping. The section count is not a firmware cap
  but the merged artifact is cleaner to inspect.

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
