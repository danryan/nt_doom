# Kickoff: P4 things and combat (nt_doom Doom-WAD engine)

This prompt starts a fresh autonomous session to design and implement P4 of the nt_doom
Doom-WAD engine. It is self-contained: it carries the project state, the binding
constraints, the inherited hardware lessons, and the execution contract, so you do not need
prior conversation context. Read `CLAUDE.md` at the repo root first; it is the authoritative
firmware-and-build reference and this prompt does not repeat all of it.

## Repo and current state

- Repo: `nt_doom`, cloned at `/opt/code/github.com/danryan/nt_doom`. GitHub `danryan/nt_doom`.
  Default branch `main`.
- What this is: a from-scratch, clean-room compact Doom engine that loads and renders real
  Doom WAD content on the Expert Sleepers disting NT Eurorack module. NOT a port of the
  original Doom source (real Doom's `.text` is hundreds of KB; the NT caps a plug-in at
  roughly 82 KB of `.text` with no code offload).
- What shipped through P3 (all on `main`, hardware-validated):
  - P0 (PR #1): clean-room WAD container parse, zero-copy map view, 4-bit framebuffer, a
    spike wall renderer, the renderer-core plug-in (`doom_core_spike.cpp`, GUID `DmSc`), the
    read-door/DRAM probes, host tools, Catch2 tests, the `BUILD_GAME` ARM pipeline.
  - P1 (PR #7): the WAD data subsystem. No-heap DRAM bump arena (`arena.h`); the WAD read
    door (`wad_read.h`); PLAYPAL/COLORMAP to 16-level gray (`palette.h`); typed NODES view.
  - P2 (PR #8): the production BSP renderer. `point_on_side`, the pure `bsp_visit_order`
    enumerator, the `SolidSegs` occlusion clip, flat-shaded and textured
    `render_view(m, cam, pal, cm, tex, fb)`, `texture.h` (`TextureCache`: TEXTURE1/PNAMES/
    patch parse and lazy composition into the DRAM arena).
  - P3 (PR #9): the player. Real `DOOM1.WAD` loads via the read door, selected through a
    `Folder`/`Sample` sample-picker; real E1M1 renders with real textures and palette; CV
    and front-panel controls drive movement (tank scheme, cubic CV response, 1 V deadzone);
    BLOCKMAP-broadphase line-slide collision holds against walls. New pure host-tested
    headers: `movement.h`, `input.h`, `collision.h`, and the `Blockmap` view in `geom.h`.

Two carry-forward facts P4 must start from:

- The renderer draws WALLS only. There is no per-column wall DEPTH buffer retained after
  `render_view` returns; `SolidSegs` tracks occluded column RANGES, not depth. P4 sprite
  depth-clipping needs a per-column depth (a 1-D z-buffer) written during wall render, so P4
  adds that to `render.h`.
- The fire gate exists as an `Intent` field (`input.h`) and a CV/customUi source (P3), but
  nothing consumes it. P4 wires it to hitscan.

## This phase: P4 (GitHub issue #5)

Render map things as billboard sprites and add a basic fire action. Read issue #5 in full;
it is the authoritative deliverable list.

The full DAG and issues:

- P1 WAD subsystem - issue #2 (COMPLETE, PR #7)
- P2 BSP wall renderer - issue #3 (COMPLETE, PR #8)
- P3 movement, collision, controls, real-WAD load - issue #4 (COMPLETE, PR #9)
- P4 things and combat - issue #5 (THIS phase)
- P5 stretch (visplanes, enemy AI, audio) - issue #6 (depends on P4, budget-gated)

Do P4 only this session. Do not start P5. This completes the honest v1 target: load real
`DOOM1.WAD`, render true E1M1, walk with collision, see things as sprites, fire hitscan.

## Three most important rules

1. Test-driven development is non-negotiable. Sprite projection, depth clipping, and hitscan
   are pure math and must be host-tested before any device wiring: a sprite behind a wall is
   occluded (its columns clipped against the per-column wall depth), a sprite in front draws;
   the hitscan ray picks the NEAREST thing whose bounding circle the ray crosses, in front of
   the player. Render-from-a-live-camera reuses the P2/P3 pixel-test harness. Run `make test`
   continuously.
2. Stack discipline on the device draw path. The P3 root-cause bug was a 4 KB stack array in
   `render_view` overflowing the NT's small `draw()` stack on real E1M1 (it passed every host
   test; the host's large stack hid it). Sprite rendering ADDS to the draw path: any
   per-column or per-sprite scratch array must be `static`/instance-member (`.bss` or SRAM),
   NEVER a `draw`/`step` stack local. Compose/sort sprites in `step` context, not deep in
   `draw`. Budget the new buffers (a 256-column depth buffer is 256 floats = 1 KB; keep it
   static or in the instance struct).
3. `step()` stays real-time safe (no per-frame allocation, no blocking, `sampleRate == 0`
   guarded) and the device add-time firmware hazards still bite (see CLAUDE.md "Firmware ABI
   contract and add-time hazards"): `_NT_parameter.name` in rodata, `serialise` via
   `addNumber` only, `numParameters` change needs a REBOOT not just a rescan, CV-input bus
   params declared `kNT_unitCvInput`. Verify ADD on hardware, not just registration.

## Model selection

- Orchestrator and implementer: this session's default model is fine. P4 has a few
  genuinely independent pure units (THINGS view, sprite projection + depth clip, hitscan)
  that may be worth parallel subagents; the render-integration and device-assembly units are
  a tighter chain. Default to inline TDD; dispatch parallel implementers only if the
  brainstorm shows clean independence, and then follow the worktree-dispatch checklist
  (explicit base branch = the feature branch, submodule plus `./bootstrap.sh` in each
  worktree, allowed-surface bounds, a parent-installed pre-commit hook).

## Autonomous execution contract

Run this sequence with a single preflight checkpoint:

1. Audit: read the primary references (below) in full. Reconcile P4 scope in issue #5 against
   the existing code. Note what is reusable (`geom.h` map views, `render.h` BSP walk and
   `render_seg`, the `TextureCache` for sprite patches, `texture.h` patch decode, the P3
   `Intent` fire gate, `cos_sin`) and what must be built (a THINGS view, a sprite lump index
   over `S_START`/`S_END`, the per-column wall depth buffer in `render.h`, billboard sprite
   projection with depth clip, the hitscan resolver, the device wiring).
2. Preflight report (single message, structured headers): audit findings, what P0-P3 cover,
   the proposed P4 unit breakdown, the `.text` and DRAM budget delta for sprites plus the
   depth buffer, any scope concern, and the abort check. Proceed automatically unless an
   abort condition fired.
3. Brainstorm then spec then plan under `docs/superpowers/` (paths below). The brainstorm
   categorizes and selects; the load-bearing infrastructure (the per-column depth buffer and
   how sprites clip against it, the sprite lump indexing and rotation-frame selection, the
   hitscan broadphase) is designed in the spec.
4. TDD implementation: red, green, refactor, in small commits. Each commit leaves `make test`
   green.
5. Verify: `make test` green, `make arm` clean, ARM symbols clean (only the firmware-resolved
   set), `.text` under the cap. Open a PR against `main` referencing issue #5. Then the
   on-device smoke: load real `DOOM1.WAD`, walk E1M1, confirm things appear as sprites
   (occluded correctly behind walls), and firing registers a hit on the nearest thing.

There is exactly one preflight checkpoint (after audit, before brainstorm). No other review
gates. The user may halt within the window if preflight reveals a problem.

## Branch and worktree

- Branch from `main` AFTER P3 (PR #9) merges. First action: detect the default branch, check
  it out, pull, then `git worktree add` a P4 worktree on a new `dr/p4-things-combat` branch.
  Invoke `superpowers:using-git-worktrees` before any edit. Never edit on `main`.
- Run `./bootstrap.sh` in the new worktree first (worktrees do not inherit submodule state;
  the llvm sparse-checkout needs bootstrap). Confirm `make test` and `make arm` are green
  before the first edit.

## Required skills and rules

- `superpowers:brainstorming`, `superpowers:writing-plans`,
  `superpowers:test-driven-development`, `superpowers:verification-before-completion`,
  `superpowers:using-git-worktrees`, `superpowers:subagent-driven-development` (if you
  parallelize), `superpowers:requesting-code-review`.
- Personal rules in force: TDD, worktrees, conventional commits, markdownlint after every
  `.md` edit, ASCII-only prose, no clause-joining hyphens.

## Frozen recipes (use, do not redesign)

- THINGS parse: the map's `THINGS` lump is a record array `x, y, angle, type, flags` (5 int16
  per record). Add a typed zero-copy view in `geom.h` (`ThingRaw`, `m.things`/`m.numThings`,
  parsed in `map_load` like the other lumps; absent THINGS is non-fatal). Filter to the thing
  types worth drawing; map the Doom thing `type` to a sprite name via a small static table
  (clean-room: derive the type->sprite mapping from the documented Doom thing table, do not
  copy GPL source).
- Sprite lump index: between the `S_START` and `S_END` marker lumps, sprite lumps are named
  `NNNNFR` (4-char sprite name, frame letter, rotation digit; optionally a second mirrored
  frame+rotation in 8 chars). For P4, support the full-view rotation (rotation `0`, or pick
  the rotation frame nearest the view angle if present). Reuse the `texture.h` patch column
  decoder to draw a sprite patch (sprites are patches: width/height/left/top offset, then
  per-column posts). Compose or sample lazily into the DRAM arena like textures; keep
  composition OUT of the deep `draw` call chain (compose on demand in `step` or cache).
- Per-column wall depth buffer: during wall render in `render_view`, write the wall depth
  (the perspective `1/z` or the `a1`/`b1` interpolated depth already computed in `render_seg`)
  into a `static`/instance `float depth[kScreenW]` per drawn column, alongside the existing
  `SolidSegs` occlusion. A sprite column draws only where its projected depth is nearer than
  the stored wall depth at that column (and where the column is not fully occluded). This is
  the depth-clip seam; design it so it is host-testable without a framebuffer (a pure
  function over the depth array and a sprite's projected column span + depth).
- Billboard sprite projection: transform the thing's world `(x,y)` into camera space with the
  same `cos_sin` rotation as `render_seg`; reject things behind the near plane; project the
  sprite center to a screen column and the sprite half-width/half-height (from the patch
  dimensions scaled by `1/depth`) to a column span and row span; clip each column against the
  per-column wall depth. Draw back-to-front across multiple sprites (sort by depth, far
  first) so nearer sprites overdraw farther ones. Libm-free (reuse `cos_sin`, integer/`1/z`
  math; no `sqrt`/`atan2`).
- Hitscan fire: on a rising fire gate (debounce the `Intent.fire`), cast a ray from the
  player along the facing; for each candidate thing, test the ray against the thing's
  bounding circle (radius), pick the NEAREST hit in front of the player that is not behind a
  solid wall (optionally reuse the BLOCKMAP/`move_blocked` line test as a wall block along the
  ray, or for P4 a simpler nearest-thing-in-front is acceptable if documented). Register the
  hit (a counter / a flagged thing); actual death/removal animation is a P5 concern. Pure and
  host-testable: given a thing list and an aim ray, assert the nearest is picked.
- Inputs and serialise: reuse the P3 seam. The fire gate is already an `Intent` field from CV
  bus 4 and a `customUi` button. Persist any new player state (e.g. a kill/hit counter) only
  if it must survive a preset reload; otherwise keep it transient.
- No heap, no libm `sinf`/`sqrt`/`atan2`; `float` math on the FPU is fine. Stack discipline
  per rule 2.

## P4 operational boundary (in scope)

- THINGS parse and a typed view in `geom.h`.
- Sprite lump index over `S_START`/`S_END`; thing-type to sprite-name mapping.
- Billboard sprite rendering, camera-facing, depth-clipped against the new per-column wall
  depth buffer; multi-sprite back-to-front draw.
- Hitscan fire on the fire gate; nearest-thing-in-front hit registration.
- Device wiring on `doom_core_spike`: render things over the E1M1 walls; fire registers a hit;
  a minimal on-screen hit indicator is acceptable.
- Host tests: sprite projection + depth clip (occlusion behind a wall), hitscan nearest pick.
  A synthetic test WAD with a THINGS entry and a sprite lump (extend `wad_build.h`).

## Out of scope (P5, issue #6)

- Enemy AI / state machines (idle/chase/attack/die).
- Textured floors and ceilings (visplanes), sky.
- Audio / sound effects.
- Damage model, health, weapons beyond a single hitscan, projectile weapons.
- Thing death animations and removal (a hit may flag/count; no death sequence).

## Vendor and dependency pins

- `vendor/distingNT_API` at the submodule pin (the P3 pin; do not bump unless deliberate).
- `vendor/llvm-project` sparse `compiler-rt/lib/builtins` at `llvmorg-19.1.0`. Read-only.
  Provisioned by `./bootstrap.sh`.

## Primary references (read in full during audit)

- `CLAUDE.md` (repo root): the firmware contract, memory/code limits, build mechanics, the
  bus/step ABI, the add-time hazards, the overlay overdraw recipe, the P2 renderer section,
  and the P3 section INCLUDING the hardware bring-up lessons (stack overflow, CvInput
  routing, sample picker, wad_open validation, the `real_wad_probe` ASan tool). Authoritative.
- Issue #5 (the P4 deliverables and acceptance) and issue #6 (P5, for the deferral boundary).
- The P3 brainstorm/spec/plan and `docs/superpowers/prompts/2026-06-09-p3-player-movement-kickoff.md`.
- Existing code: `plugins/games/doom/{wad,geom,fb,render,texture,arena,wad_read,palette,
  movement,input,collision}.h`, `plugins/games/doom_core_spike.cpp`, `harness/tools/wad_build.h`,
  `harness/tools/real_wad_probe.cpp`, `harness/tests/*.cpp`, `Makefile`.

## Lessons inherited (condensed; full detail in CLAUDE.md)

- DEVICE STACK is small: large scratch arrays on the `draw`/`step` stack hard-fault on real
  maps (the P3 `render_view` 4 KB array). Use `static`/instance buffers. This is the single
  most important carry-forward for P4, which adds render work.
- "Works on host, faults on device" usually means a device-only resource limit (stack, DRAM
  grant) the host's large stack/heap hides. Reach for the `real_wad_probe` ASan host harness
  (extend it for sprites) and replicate the device memory layout; the engine math is almost
  always correct.
- `wad_open` validates directory entries; render/geom/collision bound-check indices. Keep
  sprite/THINGS access bounds-checked too (a bad thing/sprite index must not deref wild).
- CV inputs need `kNT_unitCvInput` params or the firmware routes nothing. `numParameters`
  changes need a reboot, not just a rescan; param attribute changes take effect on re-add.
  nt_helper `add` returns "did not appear" unreliably (verify by screenshot); it cannot set
  numeric params over MCP (dial on the panel); a structurally-changed plug-in needs a full
  nt_helper restart after a reboot.
- Real-WAD load is a sample-picker (Folder/Sample params) with construct-time auto-load; the
  read is async with a dynamic frame count into the 8 MB DRAM arena. Place a multi-MB WAD WAV
  via SD-card-direct, not the slow 4 MB sysex upload.
- `.text` after P3 is about 7.4 KB; ample headroom under the ~82 KB cap, but sprites plus the
  depth buffer add code and DRAM (sprite composition) - measure after meaningful additions
  (`arm-none-eabi-readelf -W -S <o>`), and keep real E1M1 plus sprites under the 12 MB grant.
- `cos_sin` is forward-declared per-TU (host defines it with `<cmath>`; ARM rodata LUT in
  `doom_core_spike.cpp`). Catch2 `Approx` needs the `Catch::` qualifier in this harness.

## Hardware deploy loop (for the on-device smoke)

- Build the `.o`, then `make deploy-sysex SYSEX_PLUGIN=build/arm/doom_core_spike.o`. The
  upload reloads the current preset and discards unsaved slots, so re-add after upload.
- A changed GUID, or an enlarged SRAM/DRAM request, or a parameter COUNT change, needs a
  REBOOT (`F0 00 21 27 6D 00 7F F7` via `mido`), not just the rescan. The reboot pops a
  sample-scan modal needing a physical button press, and makes nt_helper's catalog stale.
- Place the real `DOOM1.WAD` on the card as a 16-bit mono WAV in an existing `/samples/<folder>/`
  (e.g. `!doom`), selected via the Folder/Sample params. Capture the screen with
  `nt_screenshot.py` (headless) or `mcp__nt_helper__show_screen`. Hardware steps need a human
  for the modal and front-panel nav; do not block the phase on them.

## Brainstorm requirements

`docs/superpowers/brainstorms/<date>-p4-things-combat-brainstorm.md`: scope, vendor pin SHAs,
what P0-P3 provide, the P4 unit list with dependencies, the depth-buffer design choice (a
per-column z-buffer in `render_view` vs an alternative) with its `.text`/stack tradeoff, the
sprite rotation-frame policy (full-view only vs angle-based) made explicit, the hitscan
wall-block decision (reuse BLOCKMAP line test vs nearest-thing-only) with rationale, and
explicit exclusions. Categorize and select; do not design the algorithms here.

## Spec requirements

`docs/superpowers/specs/<date>-p4-things-combat-design.md`: the canonical recipes (the THINGS
view, the sprite lump index and rotation-frame selection, the per-column depth buffer and the
sprite depth-clip seam, the billboard projection, the hitscan resolver, the device wiring),
per-unit entries, and a spec footer with a recipe spot-check, a per-entry verification (trace
three derivations end-to-end: a sprite projection + depth-clip against a known wall depth, a
hitscan nearest-pick against a known thing list, and a THINGS/sprite-lump-name decode against
the Doom format), and a prereq verification (the P2/P3 symbols and map fields P4 depends on
exist: `render_seg` depth, `TextureCache` patch decode, `cos_sin`, the `Intent` fire gate).

## Plan requirements

`docs/superpowers/plans/<date>-p4-things-combat-plan.md`: a TDD worklist. THINGS view, sprite
projection, depth clip, and hitscan are pure and host-testable first; the sprite render
integration and the device fire wiring are verified on hardware. If you dispatch parallel
implementers, inline the worktree-dispatch checklist (explicit base branch = the feature
branch, submodule plus bootstrap in each worktree, allowed-surface bounds, a parent-installed
pre-commit hook on the shared surface).

## Hard constraints (non-negotiable)

- Clean-room. No GPL Doom or PureDOOM source. THINGS, sprite, projection, and hitscan logic
  are reimplemented from documented formats; the thing-type to sprite-name table is derived
  from the documented Doom thing table, not copied.
- Never commit a real WAD. Host tests use the synthetic `wad_build` WAD (extended with a
  THINGS entry and a sprite lump); the real `DOOM1.WAD` stays local and uncommitted.
- No heap. `float` math is fine. No libm `sinf`/`sqrt`/`atan2` (rodata LUT via `cos_sin`,
  `1/z` and squared-distance comparisons).
- `step()` is real-time safe; `sampleRate == 0` guarded. Stack discipline per rule 2.
- Keep the plug-in `.text` under ~82 KB and DRAM under 12 MB.
- TDD for all P4 logic: sprite projection, depth clip, and hitscan as pure-function host
  tests; render-with-sprites from a live camera as pixel tests.

## Output paths

- Brainstorm: `docs/superpowers/brainstorms/<date>-p4-things-combat-brainstorm.md`
- Spec: `docs/superpowers/specs/<date>-p4-things-combat-design.md`
- Plan: `docs/superpowers/plans/<date>-p4-things-combat-plan.md`
- Code under `plugins/games/doom/` and `harness/`.

## Abort budget (concrete thresholds)

- During audit: if P0-P3 code or the vendor pins are missing, or `make test`/`make arm` is
  not green on the clean worktree, halt and report (environment broken).
- During planning: if sprites plus the depth buffer are estimated to push `.text` over the
  cap or DRAM over 12 MB, halt and resplan (cache fewer sprite frames, share the wall depth
  buffer). If the in-scope unit count exceeds 7, halt and propose splitting into a successor
  phase.
- During implementation: if a real E1M1 walk shows a sprite drawing THROUGH a wall and it
  traces to the depth-clip seam (more than one of three per-entry checks wrong), audit the
  depth buffer and the sprite clip before proceeding. If a device fault appears, suspect the
  draw stack first (the P3 lesson) and enumerate the fault dump (PC/BFAR/CFSR) before guessing.
- During verification: if the ARM build shows an unresolved symbol outside the
  firmware-resolved set, or `.text` exceeds the cap, resolve it before opening the PR. If ADD
  on hardware faults, fix the specific hazard and re-verify ADD, not just registration.

## Reporting

End-of-run message: what shipped (units, tests, commits), `make test` and `make arm` results,
the ARM unresolved-symbol check, the measured `.text` against the cap, any deferred item with
the reason, the on-device smoke result (real `DOOM1.WAD`, things visible as sprites with
correct wall occlusion, firing registers a hit on the nearest thing), and the PR link
referencing issue #5.

## Success criteria

- Map things render as camera-facing billboard sprites over real E1M1, correctly occluded
  behind walls via the per-column depth buffer, drawn back-to-front.
- Firing (CV fire gate or front-panel button) raycasts and registers a hit on the nearest
  thing in front of the player, covered by host pure-function tests.
- `step()` is real-time safe; the plug-in ADDs cleanly on hardware (no add-time-hazard fault,
  no draw-stack fault) and the device smoke shows sprites and hit registration.
- `make arm` clean, ARM symbols clean, `.text` under the ~82 KB cap.
- Brainstorm, spec (with footer), and plan committed under `docs/superpowers/`.
- PR opened against `main` referencing issue #5, with a Summary and a checkbox test plan.

## First actions

1. Detect the default branch; check out `main`, pull (P3 PR #9 merged). `git worktree add` a
   P4 worktree on a new `dr/p4-things-combat` branch from `main` (invoke
   `superpowers:using-git-worktrees`).
2. `./bootstrap.sh` in the worktree, then `make test` and `make arm` to confirm a clean
   baseline.
3. Audit: read `CLAUDE.md` (especially the P2 renderer, the P3 section, and the P3 hardware
   bring-up lessons), issue #5, the P0-P3 specs, and the existing engine and harness files in
   full.
4. Emit the preflight report. Proceed unless an abort condition fired.
5. Brainstorm, then spec (with footer), then plan. Then TDD the units, committing small and
   green. Verify, open the PR, then run the on-device smoke with a real `DOOM1.WAD`.
