# Kickoff: P3 player movement, collision, controls (nt_doom Doom-WAD engine)

This prompt starts a fresh autonomous session to design and implement P3 of the
nt_doom Doom-WAD engine. It is self-contained: it carries the project state, the
binding constraints, the inherited hardware lessons, and the execution contract, so
you do not need prior conversation context. Read `CLAUDE.md` at the repo root first; it
is the authoritative firmware-and-build reference and this prompt does not repeat all
of it.

## Repo and current state

- Repo: `nt_doom`, cloned at `/opt/code/github.com/danryan/nt_doom`. GitHub
  `danryan/nt_doom`. Default branch `main`.
- What this is: a from-scratch, clean-room compact Doom engine that loads and renders
  real Doom WAD content on the Expert Sleepers disting NT Eurorack module. NOT a port of
  the original Doom source (real Doom's `.text` is hundreds of KB; the NT caps a plug-in
  at roughly 82 KB of `.text` with no code offload).
- What shipped (P0, complete, on `main` via PR #1): clean-room WAD container parse
  (`plugins/games/doom/wad.h`), a zero-copy map view (`geom.h`), a 4-bit framebuffer
  (`fb.h`), a spike wall renderer, the renderer-core spike plug-in
  (`plugins/games/doom_core_spike.cpp`, GUID `DmSc`), the read-door/DRAM probes, host
  tools, Catch2 host tests, and the `BUILD_GAME` ARM pipeline.
- What shipped (P1, complete, on `main` via PR #7, hardware-validated): the WAD data
  subsystem. No-heap DRAM bump arena (`arena.h`); the WAD read door (`wad_read.h`, a
  host-testable byte-window seam over the WAV smuggle plus a documented
  `NT_readSampleFrames` device adapter); PLAYPAL/COLORMAP to 16-level gray (`palette.h`:
  `luma4`, `palette_load`, `colormap_load`, `shade_gray`); the typed NODES map view in
  `geom.h`; `wad_read_probe` runs the full parse stack on device.
- What shipped (P2, complete, on `main` via PR #8, hardware smoke PASS): the production
  BSP renderer. `point_on_side` and the pure `bsp_visit_order` enumerator
  (depth-guarded, node-less fallback); the `SolidSegs` occlusion clip; flat-shaded and
  textured `render_view(m, cam, pal, cm, tex, fb)`; `texture.h` (`TextureCache`:
  TEXTURE1/PNAMES/patch parse and lazy column-major composition into the DRAM arena);
  seg-to-sector helpers in `geom.h`; a synthetic two-subsector occlusion test map
  (`build_bsp_test_wad`). `.text` about 4.4 KB, far under the cap. The on-device smoke
  matched the host occlusion model exactly.

Two carry-forward facts P3 must start from:

- The renderer is asset-agnostic. It already composes real `TEXTURE1`/`PNAMES`/patches
  and loads real `PLAYPAL`/`COLORMAP`. Real textures and palette appear the instant a
  real WAD is fed in; no new rendering code is needed for that.
- P2 left `doom_core_spike` driving the EMBEDDED synthetic `kDoomBspTestWad`, with a
  FIXED camera pose `{0, 0, 0}`. It does NOT yet load a real WAD via the read door, and
  the camera does not move. P3 fixes both.

## This phase: P3 (GitHub issue #4)

Make the camera a player against REAL geometry: load a real WAD via the P1 read door,
then move the camera through the map with collision, driven by CV and the front panel.
Read issue #4 in full; it is the authoritative deliverable list.

The full DAG and issues:

- P1 WAD subsystem - issue #2 (COMPLETE, PR #7)
- P2 BSP wall renderer - issue #3 (COMPLETE, PR #8)
- P3 movement, collision, controls, real-WAD load - issue #4 (THIS phase)
- P4 things and combat - issue #5 (depends on P2, P3)
- P5 stretch (visplanes, enemy AI, audio) - issue #6 (depends on P4, budget-gated)

Do P3 only this session. Do not start P4.

## Three most important rules

1. Test-driven development is non-negotiable. Movement and collision are pure math and
   must be host-tested before any device wiring: given an input intent plus `dt`, the
   integrator advances the pose correctly; given a move that crosses a solid linedef,
   the collision resolver blocks or slides along the wall and never lets the point pass
   through. Render-from-a-live-camera reuses the P2 pixel-test harness. Run `make test`
   continuously.
2. `step()` runs on the real-time audio path and may run before the sample rate is set.
   It must be real-time safe: no per-frame allocation (no heap ever, but also no arena
   churn per frame), no blocking reads, and a hard `if (sampleRate == 0) return;` guard
   before any `dt = numFrames / sampleRate` math, or the add hangs. The read-door WAD
   load happens ONCE in `construct`, never in `step`/`draw`.
3. Add-time firmware hazards now fully bite. P3 exposes parameters, pushes values back
   from a custom UI, and serialises player state, so every hazard in `CLAUDE.md`
   ("Firmware ABI contract and add-time hazards") is now live: `_NT_parameter.name`
   never points at plug-in `.bss`; `serialise` uses `addNumber` only; the construct-time
   `parameterChanged` fire needs a sentinel guard before any `NT_setParameterFromUi`
   self-push; `NT_setParameterFromUi` indexes the GLOBAL table (add `NT_parameterOffset`).
   Verify ADD on hardware, not just registration.

## Model selection

- Orchestrator and implementer: this session's default model is fine. P3 has a few
  genuinely independent units (the real-WAD read-door integration, the input/control
  seam, the collision math) that may be worth parallel subagents; the movement and
  render-integration units are a tighter chain. Default to inline TDD; dispatch parallel
  implementers only if the brainstorm shows clean independence, and then follow the
  worktree-dispatch checklist (explicit base branch = the feature branch, submodule plus
  `./bootstrap.sh` in each worktree, allowed-surface bounds).

## Autonomous execution contract

Run this sequence with a single preflight checkpoint:

1. Audit: read the primary references (below) in full. Reconcile P3 scope in issue #4
   against the existing code. Note what is reusable (`wad_read.h` read door,
   `render_view`, `geom.h` map views, `palette.h`, `texture.h`, the `cos_sin` seam) and
   what must be built (real-WAD device load wiring, input seam, movement integrator,
   collision resolver, a collision-testable synthetic map, the stateful player camera).
2. Preflight report (single message, structured headers): audit findings, what P0/P1/P2
   already cover, the proposed P3 unit breakdown, the `.text` and DRAM budget delta for
   real-E1M1 plus movement and collision, any scope concern, and the abort check.
   Proceed automatically unless an abort condition fired.
3. Brainstorm then spec then plan under `docs/superpowers/` (paths below). The brainstorm
   categorizes and selects; the load-bearing infrastructure (the collision algorithm and
   its broadphase, the input mapping, the player-state layout and its serialise) is
   designed in the spec.
4. TDD implementation: red, green, refactor, in small commits. Each commit leaves
   `make test` green.
5. Verify: `make test` green, `make arm` clean, ARM symbols clean, `.text` under the
   cap. Open a PR against `main` referencing issue #4. Then the on-device smoke test:
   load a real `DOOM1.WAD` via the read door and walk E1M1 with CV and the front panel.

There is exactly one preflight checkpoint (after audit, before brainstorm). No other
review gates. The user may halt within the window if preflight reveals a problem.

## Branch and worktree

- This kickoff and the P2 lessons already sit on the feature branch
  `dr/p3-player-movement` (worktree at
  `/opt/code/github.com/danryan/nt_doom/.claude/worktrees/p3-player-movement`, branched
  from `main` at the P2 merge). Continue on this branch; do not branch again from `main`
  unless you deliberately restart. Never edit on `main`.
- Run `./bootstrap.sh` in this worktree first (worktrees do not inherit submodule state;
  the llvm sparse-checkout needs bootstrap). Confirm `make test` and `make arm` are
  green before the first edit.
- If you dispatch parallel implementer subagents, branch each worktree from THIS feature
  branch head (not `main`) and run `git submodule update --init vendor/distingNT_API`
  plus `./bootstrap.sh` in the new worktree.

## Required skills and rules

- `superpowers:brainstorming`, `superpowers:writing-plans`,
  `superpowers:test-driven-development`, `superpowers:verification-before-completion`,
  `superpowers:using-git-worktrees`, `superpowers:subagent-driven-development` (if you
  parallelize), `superpowers:requesting-code-review`.
- Personal rules in force: TDD, worktrees, conventional commits, markdownlint after
  every `.md` edit, ASCII-only prose, no clause-joining hyphens.

## Frozen recipes (use, do not redesign)

- Real-WAD load via the read door: in `construct`, load a real `DOOM1.WAD` smuggled as a
  16-bit mono WAV through `wad_read.h` into the DRAM arena (two WAD bytes per
  little-endian int16 sample; strip the 44-byte WAV header and any odd-byte pad). Locate
  the file by NAME substring (`_NT_wavInfo::name`), NOT by frame count (a frame-count
  match collides with real audio samples on a populated card). Parse with `wad_open` plus
  `map_load`, then run the existing `palette_load`/`colormap_load`/`texcache_init`. The
  synthetic `build_bsp_test_wad` stays the host-test fixture; host tests feed WAD bytes
  to `wad_open` directly and never touch the door. Never commit a real WAD.
- Movement integration: per `step`, `dt = numFrames / NT_globals.sampleRate` with
  `numFrames = numFramesBy4 * 4`, guarded by `if (sampleRate == 0) return;`. Turn
  integrates the camera angle; forward/back and strafe advance position along the facing
  and its perpendicular. Build the facing vector with the `cos_sin` rodata-LUT seam
  (libm-free); `float` math is fine on the FPU.
- Collision (line-slide): treat the player as a point or small radius. Broadphase via the
  Doom `BLOCKMAP` lump (a uniform grid of per-cell linedef lists) parsed into a `geom.h`
  view, so each step tests only the lines in the player's blockmap cell, not all
  thousands in E1M1. Narrowphase: for each candidate solid linedef, if the intended move
  would cross it, project the move onto the wall tangent (slide) rather than passing
  through; iterate a bounded number of times for corners. No heap; fixed scratch only.
- Inputs (two sources, mirrored): CV from buses for turn, move, strafe, and a fire gate.
  The bus layout is `step(self, busFrames, numFramesBy4)`, 28 contiguous buses indexed
  `busFrames[bus * numFrames + frame]`, scale 1.0f = 1 V. A front-panel mirror via
  `customUi` (encoders and buttons) drives the same intents. Expose sensitivity and speed
  as `int16`-safe parameters. Any self-push uses `NT_setParameterFromUi` with
  `NT_parameterOffset` added, gated behind a sentinel that only flips true once the
  algorithm is genuinely alive (after `draw` has run at least once), because the firmware
  fires `parameterChanged` during construct before the algo is registered.
- Input seam for host tests: gate the device input read behind a sim-only `-D` flag so
  host tests inject control intents directly (the P1/P2 pattern: tests populate the seam,
  the device path reads it only under the flag). Movement and collision then host-test as
  pure functions of (intent, dt, map).
- Full-screen overlay suppression: `draw` returns `true` to suppress the parameter line;
  to also suppress the firmware helper-text overlay, snapshot the bottom rows of
  `NT_screen` in `draw` and restore them in `step` while a short post-draw counter is
  active (reset the counter in `draw`, increment in `step`).
- Player state and serialise: hold the camera pose (x, y, angle) and any tunables as
  members of the firmware-allocated instance struct. `serialise` writes through
  `addNumber` only (never `addString`); pack the pose as numbers. Enlarging the instance
  struct needs a device reboot before `calculateRequirements` re-reads the SRAM size.
- No heap: placement `new` plus the P1 bump arena over the DRAM grant. No
  `malloc`/`new[]`. No libm `sinf` (rodata LUT via `cos_sin`).

## P3 operational boundary (in scope)

- Real-WAD device load: `doom_core_spike` loads a real `DOOM1.WAD` via the P1 read door,
  replacing the embedded synthetic WAD on the device path. Real textures and palette
  render for free.
- Player movement: forward/back, strafe, turn, time-stepped by `dt` from the sample rate.
- BLOCKMAP-broadphase line-slide collision against solid linedefs.
- Inputs: CV buses plus a front-panel `customUi` mirror; `int16`-safe parameters for
  speed/sensitivity; pose serialise.
- Full-screen render from the live camera with overlay suppression.
- A synthetic test map with an enclosed, walkable room (walls on all sides) plus a
  BLOCKMAP, so movement and collision are exercised by host tests. The P2
  `build_bsp_test_wad` and its render tests stay intact.

## Out of scope (later phases)

- Things, sprites, combat, hitscan (P4).
- Textured floors and ceilings, visplanes, enemy AI, audio, portal openings and sky
  (P5). Floors stay flat-shaded or blank.
- Vertical look, jumping, or 6-DOF. Z stays the Doom eye height; movement is planar.
- Any change to the P1/P2 engine beyond what movement, collision, real-WAD load, and
  controls require.

## Vendor and dependency pins

- `vendor/distingNT_API` at the submodule pin (`cd12d87`, v1.15.0 family). Read-only.
- `vendor/llvm-project` sparse `compiler-rt/lib/builtins` at `llvmorg-19.1.0`
  (`a4bf6cd`). Read-only. Provisioned by `./bootstrap.sh`; a plain `git submodule update`
  does not carry the sparse config.

## Primary references (read in full during audit)

- `CLAUDE.md` (repo root): the firmware contract, memory limits, build mechanics, the
  bus/step ABI, the add-time hazards, the firmware overlay overdraw recipe, diagnostics,
  the deploy loop. Authoritative. Read the new P2 section and the synthetic-texture-black
  gotcha there.
- Issue #4 (the P3 deliverables and acceptance).
- `docs/superpowers/specs/2026-06-04-doom-wad-engine-p0-spikes-design.md`: the P0 spec
  and the P1-P5 decomposition.
- `docs/superpowers/abort-reports/2026-06-04-doom-p0-spike-results.md`: proven hardware
  results and carry-forward constraints (DRAM grant, WAV smuggle, bus ABI).
- The P1 and P2 brainstorm/spec/plan under `docs/superpowers/`.
- Existing code: `plugins/games/doom/{wad,geom,fb,render,arena,wad_read,palette,texture}.h`,
  `plugins/games/doom_core_spike.cpp`, `plugins/probes/wad_read_probe.cpp`,
  `harness/tools/{wav_wrap,wad_build}.{h,cpp}`, `harness/tests/*.cpp`, `Makefile`.

## Lessons inherited (condensed; full detail in CLAUDE.md)

- Code-size cap: ~82 KB `.text` per `.o`, scan-time, no code offload. P2 sits at ~4.4 KB,
  so P3 has ample headroom, but measure after meaningful additions
  (`arm-none-eabi-readelf -W -S <o>`).
- SRAM/DRAM sizes are cached at scan time. Enlarging the instance struct (the player
  state) or the DRAM request needs a reboot (`0x7F`), not just the rescan `deploy-sysex`
  sends, or the plug-in allocates the old size and faults or returns garbage. P2 already
  carries a 256 KB arena member; real E1M1 plus composited textures must stay under the
  12 MB DRAM grant.
- Firmware resolves `NT_*`, `_GLOBAL_OFFSET_TABLE_`, newlib
  `memcpy`/`memset`/`memmove`/`strlen`/`strcmp` (and `logf`/`powf`). It does NOT resolve
  `memcmp`, `snprintf`/`vsnprintf`, `strstr`, or 64-bit float/divide EABI builtins
  (compiler-rt covers those). Use inline byte compares. Check `arm-none-eabi-nm <o> |
  grep ' U '`.
- Bus and step ABI: `step(self, busFrames, numFramesBy4)`, `numFrames = numFramesBy4*4`,
  28 contiguous buses, `busFrames[bus*numFrames + frame]`, 1.0f = 1 V at ~1 mV
  resolution. Reading a bus the same frame an earlier slot wrote it is the basis for
  cable-free hardware-in-the-loop verification (slot order is load-bearing).
- `draw` writes `NT_screen` (256x64, 4-bit, 2 px/byte). Returning `true` suppresses the
  parameter line; the overlay overdraw recipe suppresses the helper text.
- P2 render gotcha: with textures, walls render NEAR-BLACK on the SYNTHETIC WAD (PWALL
  pixels 0..15 index the dark end of the gray-ramp palette). Real WAD textures and
  palette render normally. To eyeball geometry on the synthetic map, pass `nullptr`
  (flat). This is why the real-WAD load is a P3 deliverable, not just a nicety.
- `point_on_side` is `cross = dx*(cy-y) - dy*(cx-x)`, `cross < 0 ? 0 : 1`; the camera
  position feeds it, so the moving camera reuses the P2 traversal unchanged.
- Hardware loop: the NT sysex upload creates files but not directories ("Unable to open
  file" means the `/samples/<folder>/` is missing); target an existing folder. After a
  reboot, nt_helper's catalog is stale: a known GUID re-adds after `/mcp reconnect
  nt_helper`; a brand-new GUID needs a full nt_helper restart. nt_helper `add` can report
  "did not appear" even when it added; verify with a screenshot
  (`harness/scripts/nt_screenshot.py`, headless, or `mcp__nt_helper__show_screen`).
- Do NOT kill the nt_helper GUI app: the `nt_helper` MCP server is backed by it and dies
  with it, and only the user can `/mcp reconnect`. For a clean mido upload that needs the
  MIDI port free, ask the user to quit/reopen and reconnect, or stay fully headless over
  sysex (upload and screenshot need only mido).

## Hardware deploy loop (for the on-device smoke check)

- Build the `.o`, then `make deploy-sysex SYSEX_PLUGIN=build/arm/doom_core_spike.o` (or
  `python3 harness/scripts/push_plugin_to_device.py 0 build/arm/doom_core_spike.o`). The
  upload reloads the current preset and discards unsaved slots, so re-add the algorithm
  after upload.
- A changed GUID, or an enlarged SRAM/DRAM request, needs a REBOOT
  (`F0 00 21 27 6D 00 7F F7` via `mido`) for the firmware to re-read
  `calculateRequirements`; the rescan alone is not enough. The reboot pops a sample-scan
  modal that needs a physical button press.
- Place the real WAD on the card as a 16-bit mono WAV in an EXISTING `/samples/<folder>/`
  (`push_file_to_device.py` cannot mkdir). Locate it by name from the plug-in. Capture
  the screen headlessly with `nt_screenshot.py`. Hardware steps need a human for the
  modal and front-panel nav; do not block the phase on them.

## Brainstorm requirements

`docs/superpowers/brainstorms/2026-06-09-p3-player-movement-brainstorm.md`: scope, vendor
pin SHAs, what P0/P1/P2 already provide, the P3 unit list with dependencies, the
control-source decision (CV buses, front-panel `customUi`, or both) made explicitly, the
collision-broadphase choice (BLOCKMAP vs brute-force) with its `.text`/runtime tradeoff,
and explicit exclusions. Categorize and select; do not design the collision algorithm
here.

## Spec requirements

`docs/superpowers/specs/2026-06-09-p3-player-movement-design.md`: the canonical recipes
(the real-WAD read-door load path, the movement integrator and its constants, the
collision algorithm with its BLOCKMAP broadphase and slide narrowphase, the input
mapping and the host-injectable seam, the player-state layout and its `serialise`), per
unit entries, and a spec footer with a recipe spot-check, a per-entry verification (trace
three derivations end-to-end: a movement integration step, a line-slide resolution
against a known wall, and a BLOCKMAP cell lookup against the Doom format), and a prereq
verification (the P1/P2 symbols and map fields P3 depends on exist).

## Plan requirements

`docs/superpowers/plans/2026-06-09-p3-player-movement-plan.md`: a TDD worklist. Movement
and collision are pure and host-testable first; the real-WAD load and the
`customUi`/serialise integration are the device-facing units verified on hardware. If you
dispatch parallel implementers, inline the worktree-dispatch checklist (explicit base
branch = this feature branch, submodule plus bootstrap in each worktree, allowed-surface
bounds, a parent-installed pre-commit hook on the shared surface).

## Hard constraints (non-negotiable)

- Clean-room. No GPL Doom or PureDOOM source. The WAD, movement, and collision math are
  reimplemented from documented formats.
- Never commit a real WAD. Host tests use the synthetic `wad_build` WAD; the real
  `DOOM1.WAD` stays local and uncommitted and is loaded only on the device path.
- No heap. `float` math is fine. No libm `sinf` (rodata LUT via `cos_sin`).
- `step()` is real-time safe: no allocation, no blocking, `sampleRate == 0` guarded.
- Keep the plug-in `.text` under ~82 KB and DRAM under 12 MB.
- TDD for all P3 logic: movement and collision as pure-function host tests, render from a
  live camera as pixel tests.

## Output paths

- Brainstorm: `docs/superpowers/brainstorms/2026-06-09-p3-player-movement-brainstorm.md`
- Spec: `docs/superpowers/specs/2026-06-09-p3-player-movement-design.md`
- Plan: `docs/superpowers/plans/2026-06-09-p3-player-movement-plan.md`
- Code under `plugins/games/doom/` and `harness/`.

## Abort budget (concrete thresholds)

- During audit: if P0/P1/P2 code or the vendor pins are missing, or `make test`/`make
  arm` is not green on the clean worktree, halt and report (environment broken).
- During planning: if the collision broadphase plus real-E1M1 load is estimated to push
  `.text` over the cap or DRAM over 12 MB, halt and resplan the boundary (brute-force a
  bounded line set before parsing BLOCKMAP, or reduce the loaded map). If the in-scope
  unit count exceeds 7, halt and propose splitting into a successor phase.
- During implementation: if a real E1M1 walk-through exposes collision that lets the
  player pass through a solid wall and it traces to the slide narrowphase (more than one
  of three per-entry checks wrong), audit the collision resolver before proceeding.
- During verification: if the ARM build shows an unresolved symbol outside the
  firmware-resolved set, or `.text` exceeds the cap, resolve it before opening the PR. If
  ADD on hardware faults (the add-time hazards), fix the specific hazard and re-verify ADD,
  not just registration.

## Reporting

End-of-run message: what shipped (units, tests, commits), `make test` and `make arm`
results, the ARM unresolved-symbol check, the measured `.text` against the cap, any
deferred item with the reason, the on-device smoke result (real `DOOM1.WAD` loaded via the
read door, walking E1M1 with CV and front panel, collision holding), and the PR link
referencing issue #4.

## Success criteria

- `doom_core_spike` loads a real `DOOM1.WAD` via the P1 read door on device and renders
  real E1M1 geometry with real textures and palette.
- The camera is a player: CV and front-panel inputs drive forward/back, strafe, and turn,
  time-stepped from the sample rate, with line-slide collision that holds against solid
  walls, all covered by host pure-function tests.
- `step()` is real-time safe; the plug-in ADDs cleanly on hardware (no add-time-hazard
  fault) and survives a reboot with the enlarged player state.
- `make arm` clean, ARM symbols clean, `.text` under the ~82 KB cap.
- Brainstorm, spec (with footer), and plan committed under `docs/superpowers/`.
- PR opened against `main` referencing issue #4, with a Summary and a checkbox test plan;
  on-device smoke walks real E1M1 with collision.

## First actions

1. `cd /opt/code/github.com/danryan/nt_doom/.claude/worktrees/p3-player-movement`;
   confirm `git rev-parse --abbrev-ref HEAD` is `dr/p3-player-movement`.
2. `./bootstrap.sh` (provision submodules and the llvm sparse-checkout), then `make test`
   and `make arm` to confirm a clean baseline.
3. Audit: read `CLAUDE.md` (especially the bus/step ABI, add-time hazards, overlay
   overdraw, and the P2 section), issue #4, the P0/P1/P2 specs, and the existing engine
   and harness files in full.
4. Emit the preflight report. Proceed unless an abort condition fired.
5. Brainstorm, then spec (with footer), then plan. Then TDD the units, committing small
   and green. Verify, open the PR, then run the on-device smoke test with a real
   `DOOM1.WAD` loaded via the read door.
