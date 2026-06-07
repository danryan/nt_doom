# Doom WAD engine: P0 feasibility spikes (design)

- Date: 2026-06-04
- Status: proposed
- Branch: `dr/doom-wad-engine`
- Vendor SHA pins (unchanged by this work): `vendor/distingNT_API` (NT plug-in ABI),
  `vendor/O_C-Phazerville` `7800d929`, `vendor/llvm-project` `llvmorg-19.1.0`.
  This project adds NO vendor source dependency. It only uses `vendor/distingNT_API`.

## Purpose

Decide whether a from-scratch, clean-room engine can load and render real Doom WAD
content on the disting NT before committing detailed specs for the engine itself.
Three hardware and budget unknowns gate the whole project. Each spike is cheap and
each can kill the project early. Do not write the P1 to P5 specs until P0 returns
real numbers.

This spec covers ONLY the P0 spikes plus a one-page decomposition overview of the
downstream sub-projects. It commits no engine detail.

## Background and the binding constraint

The disting NT runs code only as plug-ins. Two limits decide the project:

- Per-`.o` `.text` cap is approximately 82KB (empirical: 81566 B registers, 83448 B
  fails) and code cannot be offloaded to DRAM (the loader ignores custom code
  sections; `section_probe.cpp` hard-faulted on add). Real Doom
  (PureDOOM, Chocolate, doomgeneric) compiles to hundreds of KB of `.text` even at
  `-Os`, so it cannot ship as one NT plug-in. This is why the project is a
  from-scratch compact engine, not a port.
- There is NO generic file API in the SDK. Confirmed by enumerating every read entry
  point: only sample-folder access (`NT_getNumSampleFolders`, `NT_getSampleFileInfo`,
  `NT_readSampleFrames`, `NT_streamOpen`) and `NT_readScl` (Scala tuning files). All
  three streaming and read calls are bound to the sample-folder abstraction
  (folder index plus sample index plus `startOffset`). There is no `fopen`.

Research into tiny-hardware Doom ports (RP2040 Doom, nRF5340 Doom, stm32doom,
PureDOOM) shows the universal pattern: the WAD lives in external storage (SD or
flash), is streamed or executed in place, and is never fully resident in fast RAM.
RAM was the fight on those targets; all of them had 1.5MB or more of code flash. The
NT is harder on exactly the axis they all had slack: code size. Nobody fit Doom's
full renderer in under 82KB of code. Adapted conclusion: minimize CODE, treat the
WAD as on-demand data out of DRAM, and reimplement only a renderer subset.

## Hard constraints (carry into every downstream spec)

- Clean-room. Do not copy id Software Doom source or PureDOOM into this repo. Both are
  GPL; this repo is permissively licensed. The WAD binary format is documented and is
  not copyrighted; implementing a reader for it is allowed. PureDOOM informs the
  callback SHAPE (open, close, read, write, seek, tell, eof) only. No code is lifted.
- Never commit `DOOM1.WAD`. Shareware redistribution terms are unclear. Unit tests use
  a synthetic test WAD authored by a host-side builder. Real `DOOM1.WAD` is used only
  for local manual runs and a local-only integration test.
- No heap in the plug-in. Use static arenas (bump allocate, reset per level) so the
  build does not need `cxx_runtime_stubs` for `operator new`. Use `float`; no libm
  `sinf` (provide a sine lookup table in `rodata`).
- The ARM build mirrors the `BUILD_PER_APPLET` pipeline (compile, partial-link with
  `COMPILER_RT_OBJS`, section-merge) minus `SHIM_DEPS`, the manifest, and the
  per-applet runtime. New `BUILD_GAME` macro and `GAME_LIST`. New tree
  `plugins/games/`.

## The three P0 spikes

### Spike A: WAD-via-WAV read path (highest leverage)

What it proves: that a WAV-wrapped WAD placed in an NT sample folder can be read back
byte-exact from a plug-in, which is the only available door to WAD data. If this
fails, the "load real WADs" requirement is blocked at the hardware level and the
project stops here.

Recipe:

- Author a tiny host tool that prepends a 44-byte canonical 8-bit mono PCM WAV header
  to an arbitrary payload, producing `payload.wav`. The payload is a known byte
  pattern (for example a 64KB counter sequence), NOT a real WAD yet.
- Place `payload.wav` in a sample folder on the NT microSD.
- Build a probe plug-in `plugins/probes/wad_read_probe.cpp` that, once
  `NT_isSdCardMounted()` is true, locates the folder and sample by name via
  `NT_getSampleFolderInfo` and `NT_getSampleFileInfo`, then issues
  `NT_readSampleFrames` with `channels = 1`, `bits = 8`, a `startOffset`, and a small
  `numFrames`, into a DRAM buffer. The probe renders to the screen: number of folders,
  whether the file was found, the first 8 bytes read at offset 0, and the 8 bytes read
  at a nonzero `startOffset`.
- Compare on screen against the known byte pattern.

Pass criteria:

- The wrapped file appears as a sample in a folder (found = true).
- Bytes read at offset 0 equal the payload bytes that follow the 44-byte header
  (or a fixed, documented header skip if the firmware strips the header itself).
- Bytes read at a nonzero `startOffset` equal the payload at that offset, byte-exact,
  with no normalization, dithering, or rescaling.
- A second variant repeats the read via `NT_streamOpen` plus the stream buffer
  (`NT_globals.streamBufferSizeBytes`) to confirm the paged door also returns exact
  bytes.

Fail handling: if bytes are altered (normalized or dithered) or the file is not
recognized, record the exact transformation observed and HALT the project. There is no
other file door. Surface to the user as a hard blocker with the screen evidence.

### Spike B: DRAM grant size

What it proves: how much DRAM the firmware actually grants one plug-in, which decides
whether the ~4MB shareware WAD can be fully resident or must be paged on demand.

Recipe:

- A probe declares an increasing `dram` requirement in `calculateStaticRequirements`
  and `calculateRequirements` across rebuilds (for example 1MB, 2MB, 4MB, 6MB, 8MB),
  writes a sentinel pattern across the whole grant in `construct`, reads it back in
  `step`, and renders pass or fail plus the granted size to the screen.
- Bisect the largest grant that both registers and adds to a preset without fault.
  Remember the SRAM-size cache reboot discipline: enlarging the requirement needs a
  power cycle or reboot sysex before the new size takes effect.

Pass criteria: record the maximum reliable DRAM grant. There is no hard fail here; the
number selects the WAD access strategy.

Design consequence (already decided, independent of the number): WAD access is
lump-on-demand `pread` through the PureDOOM-style `seek` and `read` callbacks, backed
by a small LRU lump cache in DRAM. Full-resident WAD is an optimization enabled only
when the grant comfortably exceeds the WAD size. Designing for paged access first means
Spike B cannot block the project; it only tunes it.

### Spike C: renderer-core `.text` budget

What it proves: that the code-heavy core (WAD lump parse plus BSP traversal plus
textured wall-column rendering) fits inside the ~82KB `.text` cap with room left for
movement, sprites, and controls. This is the dominant viability risk.

Recipe:

- Build a NON-INTERACTIVE renderer-core spike `plugins/games/doom_core_spike.cpp`
  that: parses VERTEXES, LINEDEFS, SIDEDEFS, SECTORS, SEGS, SSECTORS, NODES from a
  fixed in-memory test map (the synthetic WAD compiled in as a `rodata` blob, so the
  spike does not depend on Spike A); traverses the BSP from a fixed camera; renders
  textured wall columns to `NT_screen` at 256x64 with palette-to-16-gray mapping and
  COLORMAP-style distance shading. No input, no movement, no sprites, no floors.
- Measure `.text` with `arm-none-eabi-nm` and `arm-none-eabi-readelf -W -S` on the
  built `.o`, after the section-merge step (the shipped artifact shape).

Pass criteria and budget reading:

- Record the renderer-core `.text` in KB. Interpret against the cap:
  - Under 50KB: comfortable. Sprites, movement, controls, and flat-shaded floors all
    fit. Enemy AI may fit.
  - 50KB to 70KB: tight. Movement, controls, and sprites fit; floors and AI become
    scope levers.
  - Over 70KB: the core alone nearly fills the cap. Renderer must be simplified
    (drop texture mapping for flat-shaded walls, reduce lump parsing) before the
    project can carry gameplay. Surface to the user as a scope decision.

## Decomposition overview (downstream, specified later)

Each sub-project gets its own spec and plan after P0. DAG, not a flat list.

- P1 WAD subsystem: the `pread` and stream door (gated on Spike A), lump directory
  parse and lookup, PLAYPAL and COLORMAP mapped to 16 gray levels, the arena
  allocator, and the host-side synthetic WAD builder plus WAD-builder-based unit tests.
  Pure logic, fully TDD'd on the host.
- P2 BSP wall renderer: nodes traversal, solid-seg occlusion clipping, perspective
  textured wall columns to 256x64. Render-to-buffer pixel assertions on the host.
- P3 movement, collision, controls: player move and BLOCKMAP line-slide collision; CV
  inputs (turn, move, strafe, fire gate) plus front-panel mirror; `dt` from
  `numFrames / NT_globals.sampleRate` with a `sampleRate == 0` guard; footer overdraw
  suppression for full-screen.
- P4 things and combat: billboard sprite rendering with depth clip; hitscan fire vs
  nearest thing.
- P5 stretch, each gated on the `.text` budget remaining after P4: flat-textured
  floors and ceilings (visplanes); enemy AI; audio sound effects to an audio bus.

Honest v1 target after P1 to P4: load real `DOOM1.WAD`, render true E1M1 geometry
(walls, at minimum flat-shaded sectors), walk it with collision, see things as
sprites, fire hitscan. Floors-textured and enemy AI are explicit scope-down levers.
The 256x64 (4:1) screen yields a short, squashed vertical field of view; this is
cosmetic and accepted.

## Spec footer

### Recipe spot-check

- Spike A reads back a known pattern through `NT_readSampleFrames` and `NT_streamOpen`;
  pass is byte-exact match at offset 0 and at a nonzero `startOffset`. Checked against
  `wav.h` lines 184 to 267 (the sample read and stream open signatures and the
  `_NT_streamOpenData` fields).
- Spike B bisects the `dram` field of `_NT_algorithmRequirements` and
  `_NT_staticRequirements`. Checked against `api.h` lines 142 to 165.
- Spike C measures merged `.text` with the same toolchain commands the repo already
  uses for the `.text` cap (`nm`, `readelf -W -S`).

### Per-entry verification (three checks against the SDK source)

1. File door: the only non-Scala read paths are `NT_readSampleFrames` (`wav.h:235`)
   and `NT_streamOpen` (`wav.h:267`), both sample-folder bound. Verified by enumerating
   every `NT_*read|load|file|open|sample|stream` symbol in `vendor/distingNT_API/`.
   There is no generic file open. Spike A is therefore load-bearing, not optional.
2. DRAM door: `_NT_algorithmRequirements.dram` and `_NT_staticRequirements.dram` are
   `uint32_t` with memory pointers returned in `_NT_algorithmMemoryPtrs.dram` and
   `_NT_staticMemoryPtrs.dram` (`api.h:144,152,162,173`). A plug-in can request DRAM;
   only the firmware pool cap is unknown, which is exactly what Spike B measures.
3. Code cap: the ~82KB `.text` cap and the no-code-offload result are recorded in
   project CLAUDE.md from the `section_probe` hard-fault and the 81566 vs 83448
   empirical loads. Spike C measures the renderer core against that known cap; it does
   not re-derive the cap.

### Shim prereq verification

This project uses no shim. It depends only on `vendor/distingNT_API`. The build mirrors
the `BUILD_PER_APPLET` partial-link plus section-merge pipeline (with
`COMPILER_RT_OBJS`) minus `SHIM_DEPS`, the manifest, and the per-applet runtime. No
shim symbol is required. `cxx_runtime_stubs` is avoided by the no-heap rule and is
linked only if a symbol shows unresolved in `arm-none-eabi-nm`.
