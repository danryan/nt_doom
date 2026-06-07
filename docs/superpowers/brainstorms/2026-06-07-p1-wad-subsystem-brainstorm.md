# P1 WAD subsystem: brainstorm

- Date: 2026-06-07
- Status: accepted
- Branch: `dr/p1-wad-subsystem`
- Issue: danryan/nt_doom#2
- Gated on: P0 (complete, on `main` via PR #1)

## Purpose

Categorize and select the P1 work: get real WAD bytes into DRAM and expose typed,
zero-copy access to lumps and map structures. P1 is the data foundation that P2
(renderer) builds on. P1 is pure logic and is fully test-driven on the host before
any device work. This document selects scope and unit boundaries; the load-bearing
infrastructure (read-door seam, arena allocator, palette mapping) is designed in the
spec, not here.

## Vendor pins (read-only, unchanged by this work)

| Submodule | Pin | Notes |
| --- | --- | --- |
| `vendor/distingNT_API` | `cd12d87` | NT plug-in ABI (v1.15.0 family) |
| `vendor/llvm-project` | `a4bf6cd` (tag `llvmorg-19.1.0`) | sparse `compiler-rt/lib/builtins` |

## What P0 already provides

| File | Provides | P1 disposition |
| --- | --- | --- |
| `plugins/games/doom/wad.h` | `Wad`, `wad_open`, `wad_find_lump(start)`, `wad_lump_ptr`, `wad_lump_size`; inline byte compares (no `memcmp`) | Container parse is complete. Reuse as-is. |
| `plugins/games/doom/geom.h` | packed `VertexRaw`/`SegRaw`/`SubsecRaw`/`SectorRaw`/`LinedefRaw`/`SidedefRaw`, `Map`, `map_load` | Extend: add a typed `NODES` view (today NODES is count-only). |
| `plugins/games/doom/fb.h` | 4-bit framebuffer pack | Untouched in P1. |
| `plugins/games/doom/render.h` | spike renderer | Out of scope (P2 replaces it). |
| `harness/tools/wad_build.h` | synthetic IWAD builder (E1M1 + THINGS, VERTEXES, LINEDEFS, SIDEDEFS, SEGS, SSECTORS, NODES empty, SECTORS) | Extend: add `PLAYPAL` and `COLORMAP` lumps. |
| `harness/tools/wad_build.cpp` | regenerates `plugins/games/doom_test_map.h` | Re-run after the builder grows. |
| `plugins/probes/wad_read_probe.cpp` | proven async read-door pattern (persistent `_NT_wavRequest`, callback done-flag, 16-bit mono, `startOffset` windows) | Source pattern for the device adapter; not itself the reusable seam. |
| `harness/tests/test_wad.cpp`, `test_doom_render.cpp` | container + render host tests | Add new test files; do not duplicate. |

## P1 unit list

| Unit | New or extend | Independence | Status |
| --- | --- | --- | --- |
| U1 synthetic builder: PLAYPAL + COLORMAP lumps | extend `wad_build.h` | prerequisite for U4 fixture | planned |
| U2 DRAM bump arena allocator | new header | independent | planned |
| U3 WAD read-door seam (host stub + device adapter) | new header | independent | planned |
| U4 PLAYPAL + COLORMAP parse to 16 gray | new header | depends on U1 fixture | planned |
| U5 NODES typed map view | extend `geom.h` | independent | planned |

Five small units in one header tree sharing a single builder file. Per the kickoff,
this is a single coherent subsystem, not a wide fan-out; implement inline with TDD.
The only ordering constraint is U1 before U4 (U4 tests need the palette fixture).

## Selected approach

- Read door as a byte-addressable window reader over the frame-addressable sample
  door. The firmware strips the WAV header on device; the host stub strips the 44
  byte header and the odd-byte pad itself. Two WAD bytes per int16 frame. Injection
  through a sim-only compile flag so production code paths stay unchanged.
- Arena as a bump allocator over a caller-supplied DRAM span, placement `new`, no
  `malloc`/`new[]`, reset-per-level.
- Palette mapping computes a fixed luma from each PLAYPAL RGB triple and quantizes to
  4-bit gray; COLORMAP light maps index through the same palette.
- NODES exposed as a typed `NodeRaw` view, mirroring the existing raw-struct pattern
  in `geom.h`.

## Explicit exclusions (later phases)

- Any rendering change (P2). The spike renderer stays as-is.
- Movement, controls, collision (P3); sprites and combat (P4); visplanes, AI, audio (P5).
- Growing the spike plug-in into the game plug-in. P1 ships host logic; optionally an
  updated `wad_read_probe` demonstrating a real lump located on device.
- Committing any real WAD. Tests use the synthetic builder only.
