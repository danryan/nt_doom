# Plan: P3 player movement, collision, controls, real-WAD load

Date: 2026-06-09
Status: ready
Phase: P3 (GitHub issue #4)
Branch: dr/p3-player-movement
Spec: docs/superpowers/specs/2026-06-09-p3-player-movement-design.md

## Context

Execute the P3 spec as a TDD worklist. Movement, collision, blockmap, and input are pure
headers, host-tested first (red then green, small commits, `make test` green each step).
The real-WAD load and the `customUi`/serialise integration are the device-facing unit,
verified on hardware after the PR opens. No parallel implementer dispatch: the chain
movement -> collision -> integration is tight, and the pure units are small enough that
inline TDD is faster than worktree orchestration.

## Execution order

### Step 0: baseline (done)

`./bootstrap.sh`, `make test` green, `make arm` clean, `.text` 4372 B. Recorded in the
preflight.

### Step 1: Unit 4, enclosed-room synthetic WAD + BLOCKMAP (`harness/tools/wad_build.h`)

Units 1 and 3 need this fixture, so build it first. Add `build_move_test_wad()` per
Recipe D: 512x512 room, four solid one-sided walls, one sector, one subsector, NODES
omitted, THINGS player start at center, gray-ramp PLAYPAL/COLORMAP, and a generated
BLOCKMAP (`origin (0,0)`, `cols=rows=5`, conservative bbox-overlap cell assignment).
No standalone test; it is exercised by Steps 2 and 5. Commit:
`feat(doom): synthetic enclosed-room WAD with BLOCKMAP for movement tests`.

### Step 2: Unit 1, BLOCKMAP view (`plugins/games/doom/geom.h`)

RED: `harness/tests/test_blockmap.cpp` (`[blockmap]`) over `build_move_test_wad`: header
fields (`origin (0,0)`, `cols=rows=5`), `blockmap_cell_of` for `(256,256)->(2,2)` and an
out-of-range point returns false, `blockmap_for_lines_in_cell` lists zero lines for the
interior cell `(2,2)` and the left wall's linedef for an edge cell. GREEN: add `Blockmap`,
`blockmap_load`, `blockmap_cell_of`, `blockmap_for_lines_in_cell` per Recipe A. Wire the
Makefile test target and the `host`/`test` lists. Commit:
`feat(doom): BLOCKMAP map view with cell line iteration`.

### Step 3: Unit 2, movement integrator (`plugins/games/doom/movement.h`)

RED: `harness/tests/test_movement.cpp` (`[movement]`, define `cos_sin` with `<cmath>` in
the TU): forward at angle 0 advances +x by `moveSpeed*dt`; turn rotates the angle by
`turnSpeed*dt`; strafe moves along the perpendicular; zero intent is identity; `dt`
scales linearly. GREEN: `Pose`, `Intent`, `MoveTuning`, `MoveDelta`, `turn_angle`,
`move_delta`, `integrate` per Recipe B. Commit: `feat(doom): pure tank-scheme movement integrator`.

### Step 4: Unit 5, input conditioning (`plugins/games/doom/input.h`)

RED: `harness/tests/test_input.cpp` (`[input]`): `cv_to_norm` returns 0 inside the
deadzone, preserves sign, clamps to 1 at `vmax`, applies the squared taper (0.5 of full
scale maps below linear); `cv_lowpass` converges monotonically toward the input. GREEN:
`cv_lowpass`, `cv_to_norm`, `InputConfig`, `read_cv_intent` per Recipe E (`Intent` shared
from `movement.h`). `read_cv_intent` is structured to be callable from host tests with a
synthetic `busFrames` array. Commit: `feat(doom): CV input conditioning and intent mapping`.

### Step 5: Unit 3, collision line-slide (`plugins/games/doom/collision.h`)

RED: `harness/tests/test_collision.cpp` (`[collision]`, define `cos_sin`): in the enclosed
room, a move into a wall is blocked (position unchanged on that axis); a move along a wall
slides (per-axis); a diagonal into a corner stops on both axes; an open-space move passes;
and the load-bearing assertion, a fast move that would tunnel through a wall in one step is
still blocked (segment-intersection clause). GREEN: `Vec2`, `move_blocked`, `collide_move`
and the libm-free segment/point-segment helpers per Recipe C. Commit:
`feat(doom): BLOCKMAP-broadphase line-slide collision`.

### Step 6: Unit 6, device integration (`plugins/games/doom_core_spike.cpp`)

No host pixel test changes required beyond reusing P2's `[render]` harness (render from a
live camera is the same `render_view` call). Implement Recipe F:

1. Drop the 256 KB SRAM `arenaMem`; add the DRAM grant, the async-load state, the player
   `Pose`, `MoveTuning`/`InputConfig`, the lowpass state, the `Blockmap`, the bottom-row
   overlay cache, and the `alive` sentinel.
2. `calculateRequirements`: `req.dram = 8 MB`, `req.sram = sizeof(_doomSpike)`,
   `req.numParameters`.
3. `construct`: arena over DRAM; parse embedded synthetic as the fallback; default pose.
4. `step`: `sampleRate==0` guard; one-shot `DOOM1` locate + async read (dynamic
   `numFrames`); parse-and-swap on completion (`blockmap_load`, E1M1 player start);
   per-block `turn_angle` + `move_delta` + `collide_move` + commit; overlay restore.
5. `draw`: `render_view` from the live pose; overlay snapshot; `alive=true`; return true.
6. Parameters (int16, rodata names); `parameterChanged` recomputes tuning; the
   `hasCustomUi`/`customUi` pair (encoders/buttons drive a latch, self-push gated behind
   `alive` with `NT_parameterOffset`); `serialise`/`deserialise` of `px/py/pa` via
   `addNumber(float)`.
7. Factory field order per the spec.

Run `make arm`; check `arm-none-eabi-nm build/arm/doom_core_spike.o | grep ' U '` against
the firmware-resolved set; measure `.text` via `arm-none-eabi-readelf -W -S`. Commit:
`feat(doom): player camera, real-WAD load, CV+panel controls on doom_core_spike`.

### Step 7: verify and PR

`make test` green, `make arm` clean, ARM symbols clean, `.text` under ~82 KB. Update
`CLAUDE.md` with the P3 durable lessons (new engine modules, the dynamic-frame-count
real-WAD load delta, any add-time hazard hit). Open the PR against `main` referencing
issue #4 with a Summary and a checkbox test plan. Commit the docs.

### Step 8: hardware smoke (after PR open)

Place a real `DOOM1.WAD` as 16-bit mono WAV in an existing `/samples/<folder>/` named to
contain `DOOM1`. `make deploy-sysex SYSEX_PLUGIN=build/arm/doom_core_spike.o`, reboot
(`0x7F`, DRAM + SRAM changed), re-add, screenshot. Walk E1M1 with CV and the front panel;
confirm real textures render and collision holds against walls. Hardware steps need a human
for the modal and front-panel nav; do not block the phase on them.

## Files

- New: `plugins/games/doom/movement.h`, `plugins/games/doom/collision.h`,
  `plugins/games/doom/input.h`; `harness/tests/test_blockmap.cpp`,
  `test_movement.cpp`, `test_input.cpp`, `test_collision.cpp`.
- Edited: `plugins/games/doom/geom.h` (Blockmap), `harness/tools/wad_build.h`
  (`build_move_test_wad`), `plugins/games/doom_core_spike.cpp` (device integration),
  `Makefile` (four new host test targets), `CLAUDE.md` (P3 lessons).

## Abort budget

- Planning: if `.text` is projected over the cap or DRAM over 12 MB, resplan (bounded
  brute-force, or reduce the loaded map). Not triggered (est. ~10 KB `.text`, 8 MB DRAM).
- Implementation: if a real E1M1 walk lets the player pass through a solid wall and it
  traces to the slide narrowphase (more than one of three per-entry checks wrong), audit
  `collide_move` before proceeding.
- Verification: an unresolved ARM symbol outside the firmware-resolved set, or `.text` over
  the cap, must be resolved before the PR. An ADD-time fault on hardware (the add-time
  hazards) is fixed at the specific hazard and ADD is re-verified, not just registration.

## Verification

- Host: `make test` runs all Catch2 suites including `[blockmap] [movement] [input]
  [collision]`, all green.
- ARM: `make arm` clean; `nm | grep ' U '` shows only the firmware-resolved set;
  `readelf -W -S` `.text` under ~82 KB.
- Hardware: real `DOOM1.WAD` loads via the read door; E1M1 renders with real textures;
  CV and front panel drive the player; collision holds; the plug-in ADDs cleanly after the
  reboot and survives with the enlarged player state.
