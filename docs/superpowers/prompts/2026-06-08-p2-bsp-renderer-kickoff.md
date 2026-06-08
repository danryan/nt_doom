# Kickoff: P2 BSP wall renderer (nt_doom Doom-WAD engine)

This prompt starts a fresh autonomous session to design and implement P2 of the
nt_doom Doom-WAD engine. It is self-contained: it carries the project state, the
binding constraints, the inherited hardware lessons, and the execution contract, so
you do not need prior conversation context. Read `CLAUDE.md` at the repo root first;
it is the authoritative firmware-and-build reference and this prompt does not repeat
all of it.

## Repo and current state

- Repo: `nt_doom`, cloned at `/opt/code/github.com/danryan/nt_doom`. GitHub
  `danryan/nt_doom`. Default branch `main`.
- What this is: a from-scratch, clean-room compact Doom engine that loads and renders
  real Doom WAD content on the Expert Sleepers disting NT Eurorack module. NOT a port
  of the original Doom source (real Doom's `.text` is hundreds of KB; the NT caps a
  plug-in at roughly 82 KB of `.text` with no code offload).
- What shipped (P0, complete, on `main` via PR #1): the clean-room WAD container parse
  (`plugins/games/doom/wad.h`), a zero-copy map view (`geom.h`), a 4-bit framebuffer
  (`fb.h`), a spike-grade wall renderer (`render.h`), the renderer-core spike plug-in
  (`plugins/games/doom_core_spike.cpp`, GUID `DmSc`), the read-door/DRAM probes, host
  tools, Catch2 host tests, and the `BUILD_GAME` ARM pipeline.
- What shipped (P1, complete, on `main` via PR #7): the WAD data subsystem, all
  host-TDD'd and validated on hardware (`P1 SMOKE: PASS`):
  - No-heap DRAM bump arena (`plugins/games/doom/arena.h`).
  - WAD read door (`plugins/games/doom/wad_read.h`): a host-testable byte-window seam
    over the WAV smuggle plus a documented `NT_readSampleFrames` device adapter.
  - PLAYPAL and COLORMAP to 16-level gray (`plugins/games/doom/palette.h`):
    `luma4`, `palette_load`, `colormap_load`, `shade_gray`.
  - Typed NODES map view added to `geom.h` (`NodeRaw`, `Map.nodes`, child helpers).
  - PLAYPAL/COLORMAP/NODES added to the synthetic WAD builder
    (`harness/tools/wad_build.h`), a raw-WAD emit mode on `wad_build`.
  - `wad_read_probe` evolved to run the full P1 parse stack on device.

## This phase: P2 (GitHub issue #3)

Render true map geometry to the 256x64 4-bit screen with a real BSP front-to-back
traversal, replacing the spike renderer. P2 builds directly on P1: the WAD and map
views, the arena, and the palette-to-gray mapping all exist and are consumed here.

The full DAG and issues:

- P1 WAD subsystem - issue #2 (COMPLETE, PR #7)
- P2 BSP wall renderer - issue #3 (THIS phase, depends on P1)
- P3 movement, collision, controls - issue #4 (depends on P1, P2)
- P4 things and combat - issue #5 (depends on P2, P3)
- P5 stretch (visplanes, enemy AI, audio) - issue #6 (depends on P4, budget-gated)

Do P2 only this session. Do not start P3.

## Three most important rules

1. Test-driven development is non-negotiable. Every line of renderer logic is written
   in response to a failing host test. P2 is testable as render-to-buffer pixel
   assertions: walls appear in expected columns, occlusion holds (a far wall does not
   overwrite a near one), columns shade darker with depth. Run `make test` continuously.
2. The `.text` budget is the dominant P2 risk. Spike C measured the renderer core at
   about 2.4 KB, but that was one subsector with one in-rodata texture. Full BSP
   traversal plus solid-seg occlusion plus real texture composition
   (`TEXTURE1`/`PNAMES`/patches) is the code-heavy path the whole project was sized
   against. Measure `.text` after every meaningful addition
   (`arm-none-eabi-readelf -W -S build/arm/<plugin>.o`, the `.text` section size). If
   it approaches the cap, drop wall textures to flat-shaded sectors as the first
   scope-down lever (issue #3 sanctions this) and defer textures with a named blocker.
3. Iterate one plug-in. Grow `doom_core_spike` into the production renderer; do not add
   a new probe plug-in for P2. The hardware smoke test runs through the evolved game
   plug-in reading a real WAD via the P1 read door.

## Model selection

- Orchestrator and implementer: this session's default model is fine for P2. The
  renderer is one coherent subsystem with a tight data dependency chain (traversal
  feeds clipping feeds column drawing), so inline TDD is the default. Use subagents
  only if the brainstorm surfaces a genuinely independent unit (e.g. texture
  composition vs the traversal/clip core) worth parallelizing; otherwise implement
  inline.

## Autonomous execution contract

Run this sequence with a single preflight checkpoint:

1. Audit: read the primary references (below) in full. Reconcile the P2 scope in issue
   #3 against the existing code. `render.h` is the spike starting point (perspective
   projection, affine texture column, distance shading already exist in spike form);
   `geom.h` now has the typed NODES view; `palette.h` provides `shade_gray`. Note what
   is reusable and what must be built (BSP traversal, solidsegs occlusion, texture
   composition).
2. Preflight report (single message, structured headers): audit findings, what P0 and
   P1 already cover, the proposed P2 unit breakdown, the `.text` and DRAM budget
   estimate for an E1M1-scale render, any scope concern, and the abort check. Proceed
   automatically unless an abort condition fired.
3. Brainstorm -> spec -> plan under `docs/superpowers/` (paths below). The brainstorm
   categorizes and selects; the load-bearing infrastructure (the BSP traversal order,
   the solidsegs clip structure, the texture-composition layout) is designed in the
   spec.
4. TDD implementation: red, green, refactor, in small commits. Each commit leaves
   `make test` green.
5. Verify: `make test` green, `make arm` clean, ARM symbols clean, `.text` under the
   cap. Open a PR against `main` referencing issue #3. Then the on-device smoke test
   on real geometry.

There is exactly one preflight checkpoint (after audit, before brainstorm). No other
review gates. The user may halt within the window if preflight reveals a problem.

## Branch and worktree

- Create a feature branch off `main` before editing: `dr/p2-bsp-renderer` (or
  `claude/p2-bsp-renderer`). Never edit on `main`. Pull `main` first; P1 is merged.
- Worktrees: if you dispatch parallel implementer subagents, branch each worktree from
  the feature branch head (not `main`), and run `git submodule update --init
  vendor/distingNT_API` plus `./bootstrap.sh` in the new worktree (worktrees do not
  inherit submodule state, and the llvm sparse-checkout needs bootstrap).

## Required skills and rules

- `superpowers:brainstorming`, `superpowers:writing-plans`,
  `superpowers:test-driven-development`, `superpowers:verification-before-completion`,
  `superpowers:using-git-worktrees`.
- Personal rules in force: TDD, worktrees, conventional commits, markdownlint after
  every `.md` edit, ASCII-only prose, no clause-joining hyphens.

## Frozen recipes (use, do not redesign)

- BSP traversal: descend the NODES tree from the root (last node) front-to-back. At
  each node, compute which side of the partition line the camera is on
  (`cross = dx*(cy - y) - dy*(cx - x)`), draw the near child first, then the far
  child. A child with the `0x8000` high bit set is a subsector leaf
  (`doom::node_child_is_subsector` / `node_child_index` from `geom.h`); recurse
  otherwise. Render each subsector's segs.
- Solid-seg occlusion: the classic Doom `solidsegs` span list over screen columns
  `[0, 255]`. Maintain a sorted list of occluded column ranges; clip each projected
  wall span against it; draw only the visible sub-spans; insert closed (one-sided or
  solid) walls into the list. Front-to-back order plus this clip means each screen
  column is drawn once. Keep the list small (a fixed-capacity array in the instance or
  arena, no heap).
- Perspective wall column projection: reuse and generalize the spike `render.h` path
  (world-to-camera transform, near-plane clip at depth 1.0, screen-x from lateral/depth
  ratio, per-column wall height `screenH*scale/depth`). `cos_sin` stays the per-TU
  seam (host uses `<cmath>`; ARM uses a rodata sine LUT, libm-free).
- Affine texture column sampling: per visible column, sample a column-major texture by
  `u` from the seg/offset and `v` stepped over the wall height; shade each texel with
  `doom::shade_gray(pal, cm, palIndex, light)` using the sector light level and a
  depth-derived colormap row. Flat-shaded fallback: skip the texture sample and shade a
  single sector color.
- Texture composition: parse `TEXTURE1` (texture directory), `PNAMES` (patch-name
  table), and the patch lumps (Doom column-post format) into a composited column-major
  texture cached in the DRAM arena. This is the new code-heavy and code-size-sensitive
  unit. Compose lazily (only textures the visible walls reference) to bound DRAM.
- No heap: placement `new` plus the P1 bump arena over the DRAM grant. No
  `malloc`/`new[]`. `float` math is fine (FPU is `fpv5-d16`); no libm `sinf` (rodata
  LUT).
- Synthetic test WAD: extend `harness/tools/wad_build.h` to carry a small but real
  multi-subsector map with at least one NODE split, plus `TEXTURE1`/`PNAMES` and a
  patch lump, so traversal, occlusion, and texture composition are all exercised by
  host pixel tests. Regenerate `plugins/games/doom_test_map.h`. Never commit a real WAD.
- Host test shape: include `catch.hpp`, declare `TEST_CASE` only (the shared
  `harness/src/catch_main.cpp` owns `main`). Render into a `uint8_t fb[128*64]` and
  assert pixel/column properties via `doom::fb_get`.

## P2 operational boundary (in scope)

- BSP NODES traversal front-to-back from the camera.
- Solid-seg occlusion clipping (the solidsegs span list).
- Perspective textured wall columns: project seg endpoints, per-column wall height,
  affine texture-column sampling, distance shading to the 16 gray levels via the P1
  palette and colormap.
- `TEXTURE1`/`PNAMES`/patch parse and lazy texture composition into the DRAM arena.
- Flat-shaded sectors at minimum if the texture path is cut for budget.
- Replace the spike renderer: `render.h` becomes the production renderer and
  `doom_core_spike.cpp` drives it from a real WAD loaded via the P1 read door.
- Render-to-buffer host pixel tests; the synthetic WAD extended to a real
  multi-subsector map.

## Out of scope (later phases)

- Movement, controls, collision (P3). The camera stays a fixed or scripted pose in P2.
- Things and combat (P4). Sprites are not rendered.
- Textured floors and ceilings / visplanes, enemy AI, audio (P5). Floors may be a flat
  shade or left blank in P2; visplanes are explicitly deferred.
- Any change to the P1 subsystem beyond additive map/asset parsing it does not yet do.

## Vendor and dependency pins

- `vendor/distingNT_API` at the submodule pin (`cd12d87`, v1.15.0 family). Read-only.
- `vendor/llvm-project` sparse `compiler-rt/lib/builtins` at `llvmorg-19.1.0`
  (`a4bf6cd`). Read-only. Provisioned by `./bootstrap.sh`; a plain
  `git submodule update` does not carry the sparse config.

## Primary references (read in full during audit)

- `CLAUDE.md` (repo root): the firmware contract, memory limits, build mechanics,
  diagnostics, deploy loop. Authoritative.
- `docs/superpowers/specs/2026-06-04-doom-wad-engine-p0-spikes-design.md`: the P0 spec
  and the P1-P5 decomposition overview.
- `docs/superpowers/abort-reports/2026-06-04-doom-p0-spike-results.md`: the proven
  hardware results and carry-forward constraints.
- `docs/superpowers/specs/2026-06-07-p1-wad-subsystem-design.md` and the P1 brainstorm
  and plan: the data subsystem P2 consumes.
- Existing code: `plugins/games/doom/{wad,geom,fb,render,arena,wad_read,palette}.h`,
  `plugins/games/doom_core_spike.cpp`, `plugins/probes/wad_read_probe.cpp`,
  `harness/tools/{wav_wrap,wad_build}.{h,cpp}`, `harness/tests/*.cpp`, `Makefile`.

## Lessons inherited (firmware contract and hardware loop, condensed; full detail in CLAUDE.md)

- Code-size cap: ~82 KB `.text` per `.o`, scan-time. No code offload to custom
  sections. Run-time ITC pool shared across loaded slots (~100 KB total). This is the
  P2 budget to watch.
- SRAM/DRAM sizes are cached at scan time. Enlarging a struct or a DRAM request needs a
  reboot (`0x7F`), not just the rescan `deploy-sysex` sends, or the plug-in allocates
  the old size and faults or returns garbage. The renderer plug-in now requests DRAM
  (for the WAD plus composited textures plus the framebuffer); budget it under 12 MB.
- Firmware resolves `NT_*`, `_GLOBAL_OFFSET_TABLE_`, newlib
  `memcpy`/`memset`/`memmove`/`strlen`/`strcmp`/`logf`/`powf`. It does NOT resolve
  `memcmp`, `snprintf`/`vsnprintf`, `strstr`, or 64-bit float/divide EABI builtins
  (compiler-rt covers those). Use inline byte/substring compares. Check with
  `arm-none-eabi-nm <o> | grep ' U '`.
- Add-time hazards now bite (the renderer plug-in is added to a preset and draws):
  `_NT_parameter.name` must never point at plug-in `.bss`; `serialise` uses `addNumber`
  only; construct-time `parameterChanged` fires before the algo is alive; `step()` may
  run before the sample rate is set (guard `sampleRate == 0`). These matter most in P3+
  but verify ADD on hardware, not just registration.
- Plug-ins are `-fPIC`; all linked objects must be PIC. compiler-rt is compiled with
  `arm-none-eabi-gcc` (not `c++`) and partial-linked. `BUILD_GAME` merges COMDAT
  sections via `merge_sections.lds`.
- Hardware loop (from the P1 smoke test): the NT sysex upload creates files but not
  directories ("Unable to open file" means the `/samples/<folder>/` does not exist) -
  target an existing folder. Locate a smuggled WAV by NAME, not frame count (a
  frame-count match collides with real audio samples on a populated card). After a
  reboot, nt_helper's catalog is stale: a known GUID re-adds after `/mcp reconnect
  nt_helper`; a brand-new GUID needs a full nt_helper restart. nt_helper `add` can
  report "did not appear" even when it added - verify with a screenshot.

## Hardware deploy loop (for the on-device smoke check)

- Build the `.o`, `make deploy-sysex SYSEX_PLUGIN=build/arm/doom_core_spike.o`, then
  REBOOT (`F0 00 21 27 6D 00 7F F7` via `mido`) to register a changed GUID; the rescan
  alone is not reliable.
- Place the test WAV with `harness/scripts/push_file_to_device.py 0 <local>
  "/samples/<existing-folder>/<file>"`. Capture the screen headlessly with
  `harness/scripts/nt_screenshot.py` or `mcp__nt_helper__show_screen`. Hardware steps
  need a human for the sample-scan modal and front-panel nav; do not block the phase on
  them.

## Brainstorm requirements

`docs/superpowers/brainstorms/2026-06-08-p2-bsp-renderer-brainstorm.md`: scope, the
vendor pin SHAs, what P0 and P1 already provide, the P2 unit list with status, the
texture-vs-flat-shaded scope lever, and explicit exclusions. Categorize and select; do
not design infrastructure here.

## Spec requirements

`docs/superpowers/specs/2026-06-08-p2-bsp-renderer-design.md`: the canonical recipe
(BSP traversal order, the solidsegs clip structure, the wall-column projection and
affine sampling, the TEXTURE1/PNAMES/patch composition layout, the flat-shaded
fallback), per-unit entries, and a spec footer with a recipe spot-check, a per-entry
verification (trace three derivations end-to-end: a partition-side test, a solidsegs
clip case, and a patch-column composition offset against the Doom format), and a prereq
verification (the P1 symbols and map fields the renderer depends on exist).

## Plan requirements

`docs/superpowers/plans/2026-06-08-p2-bsp-renderer-plan.md`: a TDD worklist. The
renderer core has a tight dependency chain (traversal -> clip -> column draw), so it is
largely sequential; texture composition and the synthetic-WAD extension are the more
independent units. If you dispatch parallel implementers, inline the worktree-dispatch
checklist (explicit base branch = the feature branch, submodule plus bootstrap in each
worktree, allowed-surface bounds).

## Hard constraints (non-negotiable)

- Clean-room. No GPL Doom or PureDOOM source. PureDOOM informs callback shape only; the
  WAD and rendering math are reimplemented from documented formats.
- Never commit a real WAD. Tests use the synthetic `wad_build` WAD; a real shareware
  WAD stays local and uncommitted.
- No heap. `float` math is fine. No libm `sinf` (rodata LUT via the `cos_sin` seam).
- Keep the renderer plug-in `.text` under ~82 KB and the WAD-plus-textures-plus-buffers
  DRAM under 12 MB.
- TDD for all P2 logic, with render-to-buffer pixel assertions.

## Output paths

- Brainstorm: `docs/superpowers/brainstorms/2026-06-08-p2-bsp-renderer-brainstorm.md`
- Spec: `docs/superpowers/specs/2026-06-08-p2-bsp-renderer-design.md`
- Plan: `docs/superpowers/plans/2026-06-08-p2-bsp-renderer-plan.md`
- Code under `plugins/games/doom/` and `harness/`.

## Abort budget (concrete thresholds)

- During audit: if P0/P1 code or the vendor pins are missing or the build is not green
  on a clean checkout, halt and report (environment broken).
- During planning: if the renderer plus texture composition is estimated to exceed the
  82 KB `.text` cap even with flat-shaded walls, or the E1M1-scale WAD plus composited
  textures plus framebuffer exceeds the 12 MB DRAM budget, halt and resplan the
  boundary (cut textures first, then reduce the rendered subset).
- During implementation: if a real shareware E1M1 renders geometry that contradicts the
  synthetic map in a way that traces to a traversal or clip defect (more than one of
  three per-entry checks wrong), audit the traversal and clip before proceeding.
- During verification: if the ARM build shows an unresolved symbol outside the
  firmware-resolved set, or `.text` exceeds the cap, resolve it (compiler-rt, inline,
  or the flat-shaded scope-down) before opening the PR.

## Reporting

End-of-run message: what shipped (units, tests, commits), `make test` and `make arm`
results, the ARM unresolved-symbol check, the measured `.text` size against the cap,
any deferred item with the reason (e.g. textures dropped to flat-shaded), the on-device
smoke result, and the PR link referencing issue #3.

## Success criteria

- A real map (synthetic and, locally, a real E1M1) renders recognizable wall geometry
  to the 256x64 buffer through a real BSP front-to-back traversal with solid-seg
  occlusion, fully covered by host pixel tests (`make test` green).
- Walls are textured (or flat-shaded with a named deferral), depth-shaded via the P1
  palette and colormap.
- The spike renderer is replaced; `doom_core_spike` drives the production renderer from
  a WAD loaded via the P1 read door.
- `make arm` clean, ARM symbols clean, `.text` under the ~82 KB cap.
- Brainstorm, spec (with footer), and plan committed under `docs/superpowers/`.
- PR opened against `main`, referencing issue #3, with a Summary and a checkbox test
  plan; on-device smoke test renders recognizable geometry.

## First actions

1. `cd /opt/code/github.com/danryan/nt_doom`; `git checkout main && git pull`;
   `git checkout -b dr/p2-bsp-renderer`.
2. `./bootstrap.sh` (provision submodules), then `make test` and `make arm` to confirm
   a clean baseline.
3. Audit: read `CLAUDE.md`, the P0 spec and results, the P1 spec, and the existing
   engine and harness files in full. Read issue #3.
4. Emit the preflight report. Proceed unless an abort condition fired.
5. Brainstorm, then spec (with footer), then plan. Then TDD the units, committing
   small and green. Verify, open the PR, then run the on-device smoke test.
