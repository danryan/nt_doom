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
  `geom.h` (zero-copy map view over WAD lumps, including the typed `NodeRaw` NODES
  view, the P2 `seg_sidedef`/`seg_sector`/`seg_is_one_sided` resolvers, and the P3
  `Blockmap` view: `blockmap_load`/`blockmap_cell_of`/`blockmap_for_lines_in_cell`),
  `fb.h` (4-bit framebuffer packing), `render.h` (the P2 production BSP renderer:
  `point_on_side`, `bsp_visit_order`, the `SolidSegs` occlusion clip, `light_row`, and
  the 6-arg `render_view(m, cam, pal, cm, tex, fb)`), `texture.h` (P2 `TextureCache`:
  TEXTURE1/PNAMES/patch parse and lazy column-major composition into the DRAM arena,
  `texture_sample`/`wall_u`), the P3 player subsystem: `movement.h` (pure tank-scheme
  integrator), `input.h` (CV conditioning and intent mapping), `collision.h`
  (BLOCKMAP-broadphase per-axis line-slide), and the P1 data subsystem: `arena.h`
  (no-heap DRAM bump allocator), `wad_read.h` (host-testable WAD read-door seam over the
  WAV smuggle plus a documented `NT_readSampleFrames` device adapter), `palette.h`
  (PLAYPAL/COLORMAP to 16-level gray: `luma4`, `palette_load`, `colormap_load`,
  `shade_gray`).
- `plugins/games/doom_core_spike.cpp` is the player plug-in (GUID `DmSc`), built by the
  `BUILD_GAME` Makefile macro: P3 loads a real `DOOM1.WAD` via the read door, moves the
  camera with CV and front-panel controls, and slides against solid walls. It keeps the
  embedded synthetic WAD as the pre-load fallback.
- `plugins/probes/` holds the hardware probes: `wad_read_probe.cpp` (GUID `WdRd`, the
  SD file door, evolved in P1 to read a WAV-smuggled WAD into a DRAM arena and run the
  full parse stack on device as the P1 smoke test) and `dram_grant_probe.cpp` (GUID
  `DrAm`, the DRAM grant). Per project convention, evolve an existing plug-in for a new
  phase's on-device demo rather than adding a new probe.
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

## P2 BSP renderer (host-tested, hardware smoke PASS)

The P2 renderer replaces the single-subsector spike with a real BSP walk plus
solid-seg occlusion and perspective textured walls. Durable lessons:

- `point_on_side` uses Doom `R_PointOnSide`: `cross = dx*(cy-y) - dy*(cx-x)`, then
  `cross < 0 ? 0 : 1`. Side 0 is front/right, side 1 is back/left. The near child is
  `child[side]`, the far child `child[side ^ 1]`. Sanity check: a camera to the left of
  an upward partition (`dx=0, dy>0`) returns side 1.
- `bsp_visit_order` is a PURE enumerator (fills a subsector-index array front-to-back),
  separate from drawing, so traversal order is host-testable without a framebuffer. It
  has a depth guard (`depth > numNodes`) that cuts cyclic or malformed NODES so the
  renderer never hangs, and a `numNodes == 0` fallback that lists all subsectors.
- P2 render tests drive `build_bsp_test_wad` ONLY. `build_test_wad`'s NODES record is a
  parse-only CYCLE (`child[1]` points back at node 0); it is safe to enumerate (the
  depth guard bounds it) but must never be the geometry under a pixel test. Keep
  `build_test_wad` parse-only so the P1 `test_geom_nodes` asserts stay valid.
- `SolidSegs` is a fixed `kMaxClip = 64` array of inclusive occluded column ranges,
  sorted and coalescing adjacent ranges (gap of 1 merges). `solidsegs_clip_solid` emits
  the visible gaps via a `DrawSpan` callback then marks the span occluded; front-to-back
  order makes each column drawn once. Overflow past 64 ranges is silently dropped (fine
  for BSP-ordered input, which coalesces rather than fragments).
- `TextureCache::get` is `const` via `mutable entries[]`/`composed[]` so the renderer can
  take a `const TextureCache*`. Composition is lazy into the arena and memoized.
  `wall_u` uses Manhattan wall length (`adx + ady`) to stay libm-free; exact for the
  axis-aligned synthetic walls. Patch column format is
  `topdelta,length,pad,Npx,pad,0xFF`; advance `col += 3 + length + 1`, pixels at
  `col + 3`.
- Two-sided segs are drawn as solid full-height walls in P2 (portals, openings, sky are
  deferred to P5). The synthetic test map is one-sided only; two-sided-as-solid is an
  E1M1-only behavior verified on the device smoke test, not by host pixel tests.
- `.text` with textures is about 4.3 KB (4368 B), far under the cap; the texture
  scope-down lever was NOT needed. `doom_core_spike` now carries a 256 KB `arenaMem`
  member, so `sizeof(_doomSpike)` is ~263 KB. Per the SRAM-cache hazard below, the
  device needs a reboot after the first deploy of this build before
  `calculateRequirements` re-reads the enlarged struct.
- On-device smoke PASSED. The device capture matched the host occlusion test exactly:
  near wall column 128 first-lit shade 10 over 22 rows; far wall side gaps (columns 60
  and 195) shade 4 over 8 rows; column 20 unlit. Near brighter and taller than far, far
  visible only in the gaps the near wall does not occlude.
- Synthetic-texture-vs-palette gotcha: with textures enabled, walls render NEAR-BLACK on
  the synthetic WAD. `PWALL` pixel values are 0..15, and the test WAD's PLAYPAL is a gray
  ramp, so those low palette indices sit at the dark end (`shade_gray` of index 0..15 is
  ~gray 0). Flat mode (`kFlatWallIndex = 200` to gray 12) shows the geometry. Host render
  tests only exercise flat mode (`tex == nullptr`), so this first surfaces on device. To
  visually verify geometry on hardware with the synthetic WAD, pass `nullptr` (flat); a
  real `DOOM1.WAD` with real textures and palette renders visible textured walls. The
  engine is correct; this is a synthetic-data artifact.

## P3 player movement, collision, controls, real-WAD load (host-tested, hardware smoke PASS)

P3 makes the camera a player against real geometry. On-device smoke PASSED: a real
`DOOM1.WAD` loads via the read door (sample-picker selected), real E1M1 renders with real
textures and palette, CV and front-panel controls drive the player, and collision holds
against walls. Floors/ceilings (visplanes), proper two-sided/upper-lower texturing, and
sky stay deferred to P4/P5, so on E1M1 only one-sided walls show texture and there is no
floor/ceiling; that is the documented P3 scope, not a defect. Durable lessons:

- New engine modules: `movement.h` (pure tank-scheme integrator: `turn_angle`,
  `move_delta`, `integrate` over `Pose`/`Intent`/`MoveTuning`), `input.h` (`cv_lowpass`,
  `cv_to_norm`, `read_cv_intent`, `InputConfig`), `collision.h` (`move_blocked`,
  `collide_move`, `Vec2`, libm-free `segs_intersect`/`point_seg_dist2`), the `Blockmap`
  view in `geom.h` (`blockmap_load`/`blockmap_cell_of`/`blockmap_for_lines_in_cell`), and
  `build_move_test_wad()` in `wad_build.h`.
- Tank control scheme (CV maps to velocity, never position): CV1 forward along the
  heading, CV2 turn rate (heading is integrated, so a raycaster's facing is free), CV3
  strafe along the perpendicular, CV4 fire gate. Per axis: lowpass (single-pole IIR) then
  deadzone plus normalize plus a CUBIC taper, sign-preserving and libm-free. Cubic (not
  squared) plus a 1 V default deadzone feels right for manual CV control: low voltages
  crawl, high voltages ramp to full, and a resting source does not drift. Read CV once per
  `step` block, not at audio rate; advance by `dt = numFrames / sampleRate`.
- CV INPUT ROUTING: a bus-selector parameter that reads `busFrames` MUST be declared
  `kNT_unitCvInput` (the vendor `NT_PARAMETER_CV_INPUT` unit), not `kNT_unitNone`. The
  firmware only routes a physical input onto `busFrames` for buses an algorithm CLAIMS via
  a CV-input parameter; with `kNT_unitNone` the read returns all-zero and no input works.
  Read the bus with `busFrames[(v[busParam]-1) * numFrames + 0]` (param value is 1-based).
- BLOCKMAP format: header `originX, originY, cols, rows` (int16); then `cols*rows` uint16
  word offsets (int16 units from the lump start) to per-cell blocklists; each blocklist is
  a leading `0x0000` word, a run of uint16 linedef indices, and a `0xFFFF` terminator. Skip
  the leading `0x0000` UNCONDITIONALLY (a conditional "skip while zero" would also eat a
  genuine linedef index 0). Block size is 128 units; `col = floor((x-originX)/128)`.
- Collision is per-axis slide, not tangent projection: commit the X component if clear,
  then Y against the updated X, so a blocked axis keeps its coordinate and the player
  slides along walls and stops at corners. A move is blocked when the swept segment crosses
  a solid line (anti-tunnel, catches fast moves) OR the destination is within `radius` of a
  solid line AND closer to it than the source (so parallel slides, and a player already
  within radius, are still allowed). Solid linedef = one-sided (`back == 0xFFFF`) or
  ML_BLOCKING (`flags & 0x0001`). Broadphase: the swept bbox expanded by `radius`, cells
  clamped to the grid.
- Real-WAD device load uses a sample-PICKER, not a name scan: `Folder` and `Sample`
  parameters (`kNT_unitHasStrings`, `parameterString` renders the folder/file names like
  the built-in sample player) select the WAD WAV; `parameterChanged` sets a load request
  that `step` services. The firmware's construct-time `parameterChanged` fires auto-load
  the default selection, so no front-panel nudge is needed and no `alive` gate is required
  (nothing self-pushes a parameter). The read is async with a DYNAMIC frame count from
  `_NT_wavInfo::numFrames`, into an 8 MB DRAM grant; on the callback `arena_reset`,
  `arena_alloc(numFrames*2)` reserves the WAD span at the DRAM base, then `wad_open`/
  `map_load`/`palette_load`/`colormap_load`/`texcache_init`/`blockmap_load`. The embedded
  synthetic WAD is the pre-load fallback (rendered FLAT, never composing synthetic textures
  at construct: the synthetic PWALL is near-black on the gray ramp anyway, and composing at
  add time would write the arena before the grant is proven and hard-fault).
- `doom_core_spike` dropped the 256 KB SRAM `arenaMem` member and now runs the arena over
  the 8 MB DRAM grant. Both the SRAM struct size AND the DRAM request changed, so the device
  needs a reboot (`0x7F`) before `calculateRequirements` re-reads either; the rescan alone
  is not enough.
- `serialise`/`deserialise` are the first use in this repo. The firmware resolves the
  `_NT_jsonStream`/`_NT_jsonParse` mangled methods (`addMemberName`, `addNumber(float)`,
  `matchName`, `number(float&)`) at load, alongside the `NT_*` ABI. `addNumber(float)` is a
  real overload, so the pose serialises as three floats (`px`, `py`, `pa`) directly; never
  `addString`. Confirm these symbols resolve on the first hardware load.
- The construct-time `parameterChanged`/`NT_setParameterFromUi` hazard was avoided entirely
  by NOT self-pushing parameters from `customUi`: encoders drive a movement-intent latch
  (`panelFwd`/`panelTurn`, decayed each block) and button 1 sets a fire latch, none of which
  edit a parameter. No `alive` sentinel is needed when nothing self-pushes.
- `cos_sin` is forward-declared in `movement.h` so the header does not pull `render.h`; each
  host test TU defines it with `<cmath>`, and the ARM rodata LUT lives in
  `doom_core_spike.cpp`. Catch2 `Approx` needs the `Catch::` qualifier in this harness
  (`catch_main.cpp` adds no using-directive).
- `.text` with movement, collision, input, blockmap, `customUi`, serialise, and the sample
  picker is about 7.7 KB, far under the ~82 KB cap.

### P3 hardware bring-up (the device-only failures and how they were found)

The engine was correct on the host the entire time; every device fault was an environment
mismatch the host could not reproduce. Lessons, most load-bearing first:

- STACK OVERFLOW on real maps was the root cause of the whole fault saga. `render_view`
  held `int order[1024]` (4 KB) as a STACK local; on the NT's small `draw()` stack, the
  real E1M1 BSP walk (237 subsectors, 236-node tree) overflowed it. It surfaced two ways:
  a wild-pointer bus-fault (BFAR in the DRAM region past the grant) when the overflow
  clobbered a local pointer, and a garbage-PC usage fault (`PC=0x2A`, `CFSR=INVSTATE`,
  `LR=1`) when it clobbered the return address. The synthetic 2-subsector map never tripped
  it and the host's large stack never reproduced it, so it passed every host test. Fix:
  make the visit-order buffer `static` (draw is single-threaded). Large scratch arrays
  belong in `.bss` or instance SRAM, NEVER on the NT draw/step stack.
- Lazy texture composition deepens the same draw stack: `TextureCache::get()` runs
  `tex_blit_patch` from inside `render_view->subsector->seg->texture_sample->get`. Compose
  every texture ONCE up front (in `step`/`swapRealWad`, normal stack) so `draw()`'s `get()`
  only returns cached entries.
- The device read was byte-exact all along (no sample-size cap; the NT streams from the
  card, 4 GB FAT32 limit). The 4 MB `DOOM1.WAD`-as-WAV indexed and read fine. The earlier
  "corrupt read" theory was wrong; it was always the stack.
- Defense in depth added regardless: `wad_open` now validates every directory entry
  (`filePos`/`size` within the data) so a corrupt/truncated read is rejected gracefully,
  and `render.h`/`geom.h`/`collision.h` bound-check seg/vertex/linedef/subsector indices so
  bad map data renders partial garbage instead of dereferencing unmapped memory.
- `real_wad_probe` (host, ASan; `make build/host/real_wad_probe`, needs a local uncommitted
  WAD path) runs the full device load path: WAV-smuggle round-trip, `wad_open`, `map_load`,
  compose ALL textures, render, collide, plus a device-exact single-8 MB-arena layout and a
  truncated-WAD case. It proved the engine correct and isolated the bug to the device-only
  stack. When "works on host, faults on device", reach for an ASan host harness that
  replicates the device memory layout, and suspect the small device stack.
- `numParameters` is part of `calculateRequirements`, cached at scan time, so ADDING or
  removing a parameter (changing the count) needs a REBOOT (`0x7F`), not just the
  `deploy-sysex` rescan, or the new params never appear. Param ATTRIBUTE changes (unit,
  default, min/max, name) take effect on the next ADD with no reboot. The firmware prepends
  a `Bypass` parameter at UI index 0, but `self->v[]` is indexed by the plug-in's own
  parameter order (0-based, no Bypass offset).
- nt_helper over MCP: `add` frequently returns "did not appear" even when it added; verify
  with a screenshot, not the return. It CANNOT set numeric parameter values over MCP, so
  Folder/Sample and the speeds are dialed on the front panel. After a reboot the catalog is
  stale; a structurally-changed plug-in (new param count) needs a FULL nt_helper restart
  (quit and reopen the app), not just `/mcp reconnect`.
- NT sample browser realities: it enumerates leaf sample folders under `/samples` as
  flattened paths (`00 Kits/2600/BD`); a flat `Folder` index over a big library needs a
  high `max` or a first-sorting folder name (e.g. `!doom`, valid on exFAT/FAT32). A
  multi-MB WAD WAV is fine; place it via SD-card-direct (pull the card to a laptop), since a
  4 MB sysex upload is 512-byte ACK'd chunks and takes minutes.

## P4 things and combat (host-tested; hardware smoke pending)

P4 renders map things as camera-facing billboard sprites over real E1M1, occluded behind
walls by a per-column wall depth buffer, and wires the fire gate to a hitscan that registers
a hit on the nearest thing in front. New engine modules and durable lessons:

- New modules: `sprite.h` (the `SpriteCache`: `S_START`/`S_END` lump index, `sprite_find`,
  column-major `sprite_get` composition into the arena), `combat.h` (the clean-room
  thing-type to sprite-name table `thing_sprite_name`, the libm-free `ray_seg_t`, and the
  wall-blocked `hitscan_nearest`). Extended: `geom.h` (the `ThingRaw` THINGS view,
  `m.things`/`m.numThings`, parsed optionally in `map_load` like NODES; absent THINGS is
  non-fatal), `render.h` (the per-column depth buffer, `sprite_column_visible`, billboard
  `project_thing`, the rotation octant, and `render_things`), `wad_build.h`
  (`build_things_test_wad`).
- Depth buffer is a default-off seam: `render_view(m, cam, pal, cm, tex, fb, float* depthOut
  = nullptr)`. When non-null, `render_seg` writes the per-column wall depth it already
  computes; columns with no wall stay `kFarDepth`. The default `nullptr` keeps every P2/P3
  caller and pixel test byte-for-byte unchanged. A sprite column draws only where
  `sprite_column_visible(spriteDepth, depthBuf[x])` (strictly nearer than the wall). Because
  the BSP walk is front-to-back and `SolidSegs` draws each column once by the nearest seg,
  `depthBuf[x]` is the nearest wall depth for free.
- THINGS angle is in DEGREES (0..359, usually multiples of 45), but `cos_sin` wants RADIANS:
  `render_things` converts with `kDeg2Rad` before `sprite_rotation`. The synthetic fixture
  uses a thing at angle 0 (0 degrees == 0 radians), so the angle-0 case MASKS a missing
  conversion; only a non-zero-facing real E1M1 monster would expose it. This was caught by
  the spec's per-entry verification, not by a host test. Rotation 1 is the front (thing faces
  the viewer), rotation 5 the back; the octant is a libm-free 8-way compass over the
  player-to-thing vector transformed into the thing's local frame.
- Sprite transparency uses a sentinel: `sprite_get` initializes the composed buffer to
  `kSpriteGap = 0xFF` and `tex_blit_patch` writes only covered post pixels, so gaps stay
  transparent and are skipped at draw. Limitation: a sprite pixel whose real palette index is
  `0xFF` is dropped (rare, accepted for P4). `kMaxSprites = 128` gives E1M1 headroom
  (~52 referenced rotation lumps).
- SYNTHETIC SPRITE BRIGHTNESS gotcha (same family as the PWALL gray-ramp artifact): the test
  sprite `BAR1A0` first used pixel values 1..32, which are LOW palette indices on the test
  WAD's gray-ramp PLAYPAL; `shade_gray` plus depth darkening renders them gray 0 (black), so
  the "sprite is lit" pixel assertion failed even though the sprite drew. Fix: give the
  synthetic sprite BRIGHT pixel values (200..231) so the shaded result is non-zero. Real
  `DOOM1.WAD` sprites have a real palette and render fine; this is a synthetic-data artifact,
  surfaced only by a render pixel test.
- WAD LIFETIME hazard (would fault on device, never on host): `SpriteCache::wad` is
  dereferenced by `sprite_find` at DRAW time (to read sprite-lump names for rotation
  selection). `swapRealWad` must open the WAD into an INSTANCE member (`a->wad`), not a stack
  local, or the cache's `wad` pointer dangles after the swap returns and faults on the next
  draw. (`TextureCache::wad` survives the same pattern only by accident, because its draw
  path never derefs `wad` after the up-front compose; P4 points it at `a->wad` too to remove
  the latent fragility.) The `Wad` struct's `base`/`dir` point into the arena, so the copy
  stays valid until the next load resets the arena.
- Stack discipline (the P3 lesson, reapplied): the new draw-path scratch is instance members,
  NOT draw/step stack locals. `depthBuf[256]` (1 KB) and `thingOrder[256]` live in the
  instance struct. All sprites are pre-composed in `swapRealWad` (step context), so
  `draw()`'s `sprite_get` only returns cached columns (lazy composition mid-draw would deepen
  the tight draw stack, like the texture lesson).
- Hitscan is a pure resolver: nearest drawable thing whose bounding circle the unit-length
  aim ray crosses with `tc > 0` (in front), rejected if beyond the nearest solid-wall hit
  (`ray_seg_t` over the BLOCKMAP-broadphase solid lines). An absent BLOCKMAP means no wall
  block. The rising-edge fire debounce (`prevFire`) lives in `step`, not the resolver, so one
  trigger fires one shot. The hit is a transient counter (`hitCount`/`hitThing`); death and
  removal are P5.
- Reboot rule for this build: `numParameters` is UNCHANGED (no new parameters), so no
  param-count reboot is needed for the params to appear. But the SRAM instance struct GREW
  (the `SpriteCache`, the retained `Wad`, `depthBuf` 1 KB, `thingOrder`), and
  `calculateRequirements` caches `req.sram` at scan time, so the device still needs a REBOOT
  (`0x7F`) before the enlarged struct is allocated; the `deploy-sysex` rescan alone is not
  enough. DRAM stays 8 MB (unchanged).
- `.text` with sprites and hitscan is about 11.9 KB (0x2908 + 0x598), far under the ~82 KB
  cap. ARM symbols unchanged from P3 (only the firmware-resolved set).

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
  The upload (opcode `0x7A 04`) creates the FILE but NOT a missing parent directory,
  and there is no mkdir opcode. Uploading to a non-existent folder returns a sysex
  error (`0x7A 01` + ASCII "Unable to open file"); the script only prints "Error
  uploading file!" and hides the device reason. Target an existing `/samples/<folder>/`
  (e.g. the `00 PATTERN` folder) or create the folder first via USB disk mode. To
  diagnose an upload error, send the first chunk yourself and decode the device's
  sysex reply (bytes 9..-2 are an ASCII error string); do not assume MIDI concurrency.
- Locate a WAV-smuggled WAD on device by NAME (`_NT_wavInfo::name`), NOT by frame
  count. A frame-count match collides with real audio samples on a populated card and
  reads the wrong file (the parse then fails with a non-`IWAD` header). `wad_read_probe`
  matches the file name substring `TESTMAP`.
- After a reboot, nt_helper's catalog is stale. A known GUID re-adds after `/mcp
  reconnect nt_helper`; a brand-new GUID needs a full nt_helper restart (quit and
  reopen the app: `osascript -e 'quit app "nt_helper"'` then `open -a nt_helper`). The
  GUI app and the `mcp-server-nt_helper` MCP process are separate; the MIDI-port
  conflict that corrupts a multi-chunk upload comes from whichever is actively polling.
  Quit the app for an upload, then reconnect for `add`.

## Workflow

Non-trivial changes follow brainstorm -> spec -> plan -> TDD -> verify, with docs
under `docs/superpowers/`. TDD is the default: a failing host test before engine
code. Hardware smoke check happens after PR open since it needs physical access.

Always update this CLAUDE.md with the phase's durable lessons (firmware quirks,
hardware-loop gotchas, new engine modules) BEFORE merging the phase PR, so the next
session inherits them.

## Markdown

After editing any `.md`, run `markdownlint <file>` and fix errors.
