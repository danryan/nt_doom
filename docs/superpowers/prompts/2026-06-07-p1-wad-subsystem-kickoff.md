# Kickoff: P1 WAD subsystem (nt_doom Doom-WAD engine)

This prompt starts a fresh autonomous session to design and implement P1 of the
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
  (`fb.h`), a spike-grade BSP-ish wall renderer (`render.h`), the renderer-core spike
  plug-in (`plugins/games/doom_core_spike.cpp`, GUID `DmSc`), two hardware probes
  (`plugins/probes/wad_read_probe.cpp` GUID `WdRd`, `dram_grant_probe.cpp` GUID
  `DrAm`), host tools (`harness/tools/wav_wrap`, `wad_build`), Catch2 host tests, the
  `BUILD_GAME` ARM pipeline, and the sysex deploy/screenshot scripts.
- P0 proved on hardware (see `docs/superpowers/abort-reports/2026-06-04-doom-p0-spike-results.md`):
  - WAD-via-WAV read is byte-exact. The WAD ships as a 16-bit mono WAV in a samples
    folder; `NT_readSampleFrames` returns the PCM unconverted when the requested bit
    depth matches the file's native depth, at offset 0 and at deep offsets. Two WAD
    bytes pack per little-endian int16 sample; strip the 44-byte WAV header and any
    odd-byte pad on read. The NT sample browser REJECTS 8-bit WAV, so 16-bit is
    mandatory.
  - DRAM grant is at least 12 MB per plug-in instance (sentinel-verified); 16 MB
    fails to add. Budget the WAD plus working buffers under 12 MB.
  - Renderer-core `.text` is about 2.4 KB, far under the ~82 KB cap.

## This phase: P1 (GitHub issue #2)

Build the WAD data subsystem: get real WAD bytes into DRAM and expose typed,
zero-copy access to lumps and map structures. This is the foundation P2 (renderer,
issue #3) builds on. P1 is almost entirely pure logic and must be fully TDD'd on the
host before any device work.

The full DAG and issues:

- P1 WAD subsystem - issue #2 (THIS phase)
- P2 BSP wall renderer - issue #3 (depends on P1)
- P3 movement, collision, controls - issue #4 (depends on P1, P2)
- P4 things and combat - issue #5 (depends on P2, P3)
- P5 stretch (visplanes, enemy AI, audio) - issue #6 (depends on P4, budget-gated)

Do P1 only this session. Do not start P2.

## Two most important rules

1. Test-driven development is non-negotiable. Every line of P1 logic is written in
   response to a failing host test. P1 is pure logic; there is no excuse to skip TDD.
   Run `make test` continuously.
2. Host-green is NOT hardware-proof, but P1 is the one phase where it nearly is: P1
   has no `parameterChanged`, no `serialise`, no `step()` audio path. Still, the WAD
   read door touches `NT_readSampleFrames` (async) on device; design that seam so the
   logic is host-testable with a file-backed stub and the device path is a thin
   adapter. Verify the ARM build links clean (`make arm`, then
   `arm-none-eabi-nm <o> | grep ' U '` shows only the firmware-resolved set).

## Model selection

- Orchestrator and implementer: this session's default model is fine for P1 (single
  coherent subsystem, not a wide fan-out). Use subagents only if the brainstorm
  surfaces genuinely independent units (e.g. palette mapping vs arena allocator vs
  directory parse) worth parallelizing; otherwise implement inline with TDD.

## Autonomous execution contract

Run this sequence with a single preflight checkpoint:

1. Audit: read the primary references (below) in full. Reconcile the P1 scope in issue
   #2 against the existing code (`wad.h`, `geom.h` already exist from P0 and must be
   extended, not duplicated). Note what P0 already provides.
2. Preflight report (single message, structured headers): audit findings, what P0
   already covers, the proposed P1 unit breakdown, any scope concern, and the abort
   check. Proceed automatically unless an abort condition fired.
3. Brainstorm -> spec -> plan under `docs/superpowers/` (paths below). The brainstorm
   categorizes and selects; it does not design load-bearing infrastructure under
   deadline (the arena allocator and the read-door seam are designed in the spec).
4. TDD implementation: red, green, refactor, in small commits. Each commit leaves
   `make test` green.
5. Verify: `make test` green, `make arm` clean, ARM symbols clean. Open a PR against
   `main` referencing issue #2.

There is exactly one preflight checkpoint (after audit, before brainstorm). No other
review gates. The user may halt within the window if preflight reveals a problem.

## Branch and worktree

- Create a feature branch off `main` before editing: `dr/p1-wad-subsystem` (or
  `claude/p1-wad-subsystem`). Never edit on `main`.
- Worktrees: if you dispatch parallel implementer subagents, branch each worktree from
  the feature branch head (not `main`), and run `git submodule update --init
  vendor/distingNT_API` plus `./bootstrap.sh` in the new worktree (worktrees do not
  inherit submodule state, and the llvm sparse-checkout needs bootstrap).

## Required skills and rules

- `superpowers:brainstorming`, `superpowers:writing-plans`,
  `superpowers:test-driven-development`, `superpowers:verification-before-completion`.
- Personal rules already in force this machine: TDD, worktrees, conventional commits,
  markdownlint after every `.md` edit, ASCII-only prose, no clause-joining hyphens.

## Frozen recipes (use, do not redesign)

- WAD read door (device): a persistent (not stack) `_NT_wavRequest`, request 16-bit
  mono to match the file, read in `pread`-style windows by `startOffset` (frames) into
  a DRAM arena, strip the 44-byte WAV header and odd-byte pad. The read is async
  (callback flips a done flag). See `plugins/probes/wad_read_probe.cpp` for the proven
  pattern and `harness/tools/wav_wrap.h` for the 16-bit wrap (2 WAD bytes per int16).
- WAD container parse and lump lookup: extend `plugins/games/doom/wad.h`
  (`doom::Wad`, `wad_open`, `wad_find_lump`, `wad_lump_ptr`, `wad_lump_size`). It uses
  inline byte compares, not `memcmp` (the firmware does not resolve `memcmp`).
- Map views: extend `plugins/games/doom/geom.h` (`doom::Map`, `map_load`, the packed
  `#pragma pack(push,1)` raw structs).
- Synthetic test WAD: extend `harness/tools/wad_build.h` (`build_test_wad`) and
  regenerate `plugins/games/doom_test_map.h` via `harness/tools/wad_build` when the
  fixture grows. Never commit a real WAD.
- Host test shape: include `catch.hpp`, declare `TEST_CASE` only (the shared
  `harness/src/catch_main.cpp` owns `main`). Build then parse the synthetic WAD; assert
  lump counts, names, sizes, and parsed field values.
- No heap: placement `new` plus a bump arena over the DRAM grant. No `malloc`/`new[]`
  (avoids pulling cxx runtime stubs).

## P1 operational boundary (in scope)

- The WAD read door: a host-testable seam (file-backed stub) plus the device adapter
  over `NT_readSampleFrames`. Optionally evaluate `NT_streamOpen` for larger reads;
  document the choice.
- WAD container parse and lump lookup (extend existing).
- Map lump views for a real map: `VERTEXES`, `LINEDEFS`, `SIDEDEFS`, `SECTORS`,
  `SEGS`, `SSECTORS`, `NODES` (extend existing).
- `PLAYPAL` and `COLORMAP` parse, mapped to the 16 gray levels of the NT screen.
- A no-heap DRAM arena allocator.
- Synthetic WAD builder extended as needed; WAD-builder-based unit tests.

## Out of scope (later phases)

- Any rendering change (P2). The spike renderer stays as-is until P2 replaces it.
- Movement, controls, collision (P3). Sprites, combat (P4). Visplanes, AI, audio (P5).
- Do not grow the spike plug-in into the game plug-in yet; P1 ships host logic plus,
  if useful, an updated `wad_read_probe` that demonstrates a real lump being located
  on device.

## Vendor and dependency pins

- `vendor/distingNT_API` at the submodule pin (v1.15.0 family). Read-only.
- `vendor/llvm-project` sparse `compiler-rt/lib/builtins` at `llvmorg-19.1.0`.
  Read-only. Provisioned by `./bootstrap.sh`; a plain `git submodule update` does not
  carry the sparse config.

## Primary references (read in full during audit)

- `CLAUDE.md` (repo root): the firmware contract, memory limits, build mechanics,
  diagnostics, deploy loop. Authoritative.
- `docs/superpowers/specs/2026-06-04-doom-wad-engine-p0-spikes-design.md`: the P0 spec
  and the P1-P5 decomposition overview.
- `docs/superpowers/abort-reports/2026-06-04-doom-p0-spike-results.md`: the proven
  hardware results and carry-forward constraints.
- Existing code: `plugins/games/doom/{wad,geom,fb,render}.h`,
  `plugins/games/doom_core_spike.cpp`, `plugins/probes/wad_read_probe.cpp`,
  `harness/tools/{wav_wrap,wad_build}.{h,cpp}`, `harness/tests/*.cpp`, `Makefile`.

## Lessons inherited (firmware contract, condensed; full detail in CLAUDE.md)

- Code-size cap: ~82 KB `.text` per `.o`, scan-time. No code offload to custom
  sections. Run-time ITC pool shared across loaded slots (~100 KB total).
- SRAM/DRAM sizes are cached at scan time. Enlarging a struct or a DRAM request needs
  a reboot (`0x7F`), not just the rescan that `deploy-sysex` sends, or the plug-in
  allocates the old size and faults or returns garbage.
- Firmware resolves `NT_*`, `_GLOBAL_OFFSET_TABLE_`, newlib
  `memcpy`/`memset`/`memmove`/`strlen`/`strcmp`/`logf`/`powf`. It does NOT resolve
  `memcmp`, `snprintf`/`vsnprintf`, or 64-bit float/divide EABI builtins (compiler-rt
  covers those). Check with `arm-none-eabi-nm <o> | grep ' U '`.
- `_NT_parameter.name` must never point at plug-in `.bss` (firmware dereferences it on
  add and hard-faults); use `.rodata` literals or per-instance SRAM. `serialise` uses
  `addNumber` only. Construct-time `parameterChanged` fires before the algo is alive;
  gate any `NT_setParameterFromUi` behind a liveness sentinel and add
  `NT_parameterOffset()` to the index. `step()` may run before sample rate is set;
  guard `sampleRate == 0`. These bite in P3+, not P1, but know them.
- `_NT_parameter` values are `int16_t` (-32768..32767). `numParameters` must equal the
  populated entry count.
- Plug-ins are `-fPIC`; all linked objects must be PIC. compiler-rt is compiled with
  `arm-none-eabi-gcc` (not `c++`) and partial-linked. `BUILD_GAME` merges COMDAT
  sections via `merge_sections.lds`.
- Diagnose first on opaque errors: `objdump -r` for relocation types, `nm` for
  unresolved symbols, sysex `0x30`/`0x31` to confirm registration. Misc > Plug-ins >
  View Info gives no per-plugin failure reason.

## Hardware deploy loop (only if you do an on-device check)

- Build the `.o`, `make deploy-sysex SYSEX_PLUGIN=build/arm/<name>.o`, then REBOOT
  (`F0 00 21 27 6D 00 7F F7` via `mido`) to register a new/changed GUID; the rescan
  alone is not reliable.
- Each reboot pops a sample-scan modal needing a physical button press and staleness
  in nt_helper's catalog (a new GUID needs a full nt_helper restart, not a reconnect).
- nt_helper `edit_parameter` rejects numeric values (enum strings only); `add` may
  report "did not appear" even when it added (verify via screenshot). Capture the
  screen headlessly with `harness/scripts/nt_screenshot.py` (no nt_helper needed).
- `harness/scripts/push_file_to_device.py 0 <local> "/samples/<folder>/<file>"`
  uploads a test WAV to the card over sysex without USB disk mode. Hardware steps need
  a human for modals and front-panel nav; do not block the phase on them.

## Brainstorm requirements

`docs/superpowers/brainstorms/2026-06-07-p1-wad-subsystem-brainstorm.md`: scope, the
vendor pin SHAs, what P0 already provides, the P1 unit list with status, and explicit
exclusions. Categorize and select; do not design infrastructure here.

## Spec requirements

`docs/superpowers/specs/2026-06-07-p1-wad-subsystem-design.md`: the canonical recipe
(the read-door seam, the arena allocator, the palette-to-gray mapping, the lump and
map view extensions), per-unit entries, and a spec footer with a recipe spot-check, a
per-entry verification (trace three lump/struct field derivations against the WAD
format and `wav.h`), and a prereq verification (the symbols and API the units depend
on actually exist).

## Plan requirements

`docs/superpowers/plans/2026-06-07-p1-wad-subsystem-plan.md`: a TDD worklist, parallel
by default where units are independent (arena, palette, directory, map views are
largely independent), sequenced only where a unit depends on another. If you dispatch
parallel implementers, inline the worktree-dispatch checklist (explicit base branch =
the feature branch, submodule + bootstrap in each worktree, allowed-surface bounds).

## Hard constraints (non-negotiable)

- Clean-room. No GPL Doom or PureDOOM source. PureDOOM informs callback shape only.
- Never commit a real WAD. Tests use the synthetic `wad_build` WAD; a real shareware
  WAD stays local and uncommitted.
- No heap. `float` math is fine (the FPU is `fpv5-d16`). No libm `sinf` (rodata LUT if
  trig is needed; P1 likely needs none).
- Keep every plug-in `.text` under ~82 KB and the WAD-plus-buffers DRAM under 12 MB.
- TDD for all P1 logic.

## Output paths

- Brainstorm: `docs/superpowers/brainstorms/2026-06-07-p1-wad-subsystem-brainstorm.md`
- Spec: `docs/superpowers/specs/2026-06-07-p1-wad-subsystem-design.md`
- Plan: `docs/superpowers/plans/2026-06-07-p1-wad-subsystem-plan.md`
- Code under `plugins/games/doom/` and `harness/`.

## Abort budget (concrete thresholds)

- During audit: if P0 code or the vendor pins are missing or the build is not green on
  a clean checkout, halt and report (environment broken).
- During planning: if the P1 unit count or complexity implies more than one plug-in's
  worth of `.text` or exceeds the 12 MB DRAM budget for a real E1M1-scale WAD plus
  parse buffers, halt and resplan the boundary.
- During implementation: if a real shareware E1M1 lump set parses differently from the
  synthetic WAD in a way that contradicts the WAD format (more than one of three
  per-entry checks wrong), audit every map-view field before proceeding.
- During verification: if the ARM build shows an unresolved symbol outside the
  firmware-resolved set, resolve it (compiler-rt or inline) before opening the PR.

## Reporting

End-of-run message: what shipped (units, tests, commits), `make test` and `make arm`
results, ARM unresolved-symbol check, any deferred item with the reason, and the PR
link referencing issue #2.

## Success criteria

- A real WAD's E1M1 lump set and the synthetic WAD parse identically through the P1
  subsystem, fully covered by host tests (`make test` green).
- The DRAM arena, palette-to-gray, lump lookup, and map views are in place and
  consumed by a test (and optionally demonstrated on device by `wad_read_probe`).
- `make arm` clean, ARM symbols clean.
- Brainstorm, spec (with footer), and plan committed under `docs/superpowers/`.
- PR opened against `main`, referencing issue #2, with a Summary and a checkbox test
  plan.

## First actions

1. `cd /opt/code/github.com/danryan/nt_doom`; `git checkout main && git pull`;
   `git checkout -b dr/p1-wad-subsystem`.
2. `./bootstrap.sh` (provision submodules), then `make test` and `make arm` to confirm
   a clean baseline.
3. Audit: read `CLAUDE.md`, the P0 spec and results, and the existing engine and
   harness files in full. Read issue #2.
4. Emit the preflight report. Proceed unless an abort condition fired.
5. Brainstorm, then spec (with footer), then plan. Then TDD the units, committing
   small and green. Verify, then open the PR.
