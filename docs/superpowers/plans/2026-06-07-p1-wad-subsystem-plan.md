# P1 WAD subsystem: implementation plan

- Date: 2026-06-07
- Status: ready
- Branch: `dr/p1-wad-subsystem`
- Issue: danryan/nt_doom#2
- Spec: `docs/superpowers/specs/2026-06-07-p1-wad-subsystem-design.md`

## Approach

TDD throughout: a failing host test precedes every line of engine logic. Each step
leaves `make test` green and is one small commit. P1 is a single coherent subsystem of
five small units in one header tree sharing one builder file, so it is implemented
inline (not a parallel fan-out). The only hard ordering is the shared builder file:
U1 and U5 both append lumps/records to `harness/tools/wad_build.h`, so they are
sequenced and `doom_test_map.h` is regenerated once after each builder change.

Units U2 (arena), U3 (read door), U4 (palette), U5 (NODES) are otherwise independent
and could be reordered freely. The chosen order front-loads the builder/fixture work
(U1) that U4 depends on, then proceeds smallest-blast-radius first.

## Worklist

### Step 1: U1 builder PLAYPAL + COLORMAP

- RED: add `test_palette.cpp` asserting `PLAYPAL` lump exists and is 768 bytes and
  `COLORMAP` is `34*256` bytes (fails: lumps absent). Wire into `Makefile`.
- GREEN: extend `build_test_wad()` with the gray-ramp `PLAYPAL` and the darkening
  `COLORMAP`; regenerate `doom_test_map.h` via `harness/tools/wad_build`.
- Commit: `test(wad): add PLAYPAL and COLORMAP to the synthetic WAD`.

### Step 2: U2 arena allocator

- RED: `test_arena.cpp` covering init, aligned sequential allocs, overflow returns
  `nullptr` and preserves `used`, `arena_new` constructs, `arena_reset` reuses.
- GREEN: `plugins/games/doom/arena.h`.
- REFACTOR: assess; keep only if it adds value.
- Commit: `feat(doom): add no-heap DRAM bump arena`.

### Step 3: U4 palette to gray

- RED: extend `test_palette.cpp` with `luma4` corners, `palette_load` against the ramp,
  `colormap_load` size and map-0 identity, `shade_gray` behavior.
- GREEN: `plugins/games/doom/palette.h`.
- Commit: `feat(doom): map PLAYPAL and COLORMAP to 16-level gray`.

### Step 4: U3 read door

- RED: `test_wad_read.cpp`: `WavWadSource` round-trip at offset 0, deep offset, across
  the odd-pad boundary, out-of-range rejection; `wad_load_all` into an arena then
  `wad_open` matches direct parse.
- GREEN: `plugins/games/doom/wad_read.h`.
- Commit: `feat(doom): add host-testable WAD read-door seam`.

### Step 5: U5 NODES view

- RED: `test_geom_nodes.cpp`: typed NODES view count and field values; child high-bit
  helpers. Add one NODES record to the builder; regenerate `doom_test_map.h`.
- GREEN: extend `geom.h` with `NodeRaw`, the `Map.nodes` pointer, `map_load` wiring,
  and the child helpers.
- Commit: `feat(doom): expose typed NODES map view`.

### Step 6: verify and PR

- `make test` green; `make arm` clean; `arm-none-eabi-nm build/arm/doom_core_spike.o |
  grep ' U '` shows only the firmware-resolved set (no new unresolved symbols; P1
  headers are host-consumed, but confirm the ARM build is unaffected).
- Open PR vs `main` referencing issue #2, with Summary and a checkbox test plan.

## ARM build note

P1 units are header-only and consumed by host tests. They are not yet wired into an
ARM plug-in (the spike plug-in is frozen until P2). The verify step confirms `make arm`
still builds clean and the existing plug-ins' symbol sets are unchanged. If an updated
`wad_read_probe` demonstrating a real lump on device is added, it is optional and
gated behind hardware access after PR open.

## Abort conditions (from the kickoff)

- Implementation: if a real shareware E1M1 lump set parses differently from the
  synthetic WAD in a way that contradicts the WAD format (more than one of three
  per-entry checks wrong), audit every map-view field before proceeding.
- Verification: if `make arm` shows an unresolved symbol outside the firmware-resolved
  set, resolve it (compiler-rt or inline) before opening the PR.

## Out of scope

Rendering (P2), movement (P3), combat (P4), stretch (P5). No real WAD committed.
