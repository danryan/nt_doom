# Brainstorm: P2 BSP wall renderer

Date: 2026-06-08
Status: Selected, pending spec
Issue: #3 (depends on #2, P1 complete)
Branch: `dr/p2-bsp-renderer`

## Purpose

Render true map geometry to the 256x64 4-bit screen with a real BSP front-to-back
traversal and solid-seg occlusion, replacing the single-subsector spike renderer.
This brainstorm categorizes the P2 work and selects the build order. The load-bearing
structures (traversal order, the solidsegs clip layout, texture composition) are
designed in the spec, not here.

## Scope (in)

- BSP NODES front-to-back traversal from the camera; descend subsectors in view order.
- Solid-seg occlusion clip (the classic Doom solidsegs span list over columns 0..255)
  so far walls do not overdraw near ones.
- Perspective wall columns: project seg endpoints, per-column wall height, affine
  texture-column sampling, distance shading to 16 gray levels via the P1 palette and
  colormap.
- `TEXTURE1`/`PNAMES`/patch parse and lazy composition into the DRAM arena.
- Flat-shaded sectors as the budget fallback and as the first shippable milestone.
- Replace the spike renderer: `render.h` becomes the production renderer;
  `doom_core_spike` drives it from a WAD loaded via the P1 read door.
- Render-to-buffer host pixel tests; synthetic WAD extended to a real multi-subsector
  map with a true NODE split, plus `TEXTURE1`/`PNAMES`/a patch lump.

## Wall-type scope decision (one-sided solid walls)

P2 renders one-sided linedefs as solid full-height walls. This is the dominant
correctness axis for a real map, so it is decided here, not in the spec.

- One-sided segs (back sidedef `0xFFFF`) are solid: they occlude the full screen
  column and are inserted into the solidsegs list. Their `middle` texture is the wall.
- Two-sided segs (a real back sector) are portals in real Doom: they do not fully
  occlude and need upper/lower texture handling plus per-column vertical clip arrays
  (`floorclip`/`ceilingclip`). Full portal rendering is OUT of P2 scope (deferred with
  visplanes). In P2 a two-sided seg is treated as solid (drawn as a full-height wall
  using its `middle` or its sidedef texture) so traversal and occlusion stay testable
  and a real E1M1 renders recognizable, if not portal-correct, geometry. Proper portal
  handling (windows, openings, sky) is a named P5 follow-on.

Consequence: the synthetic test map is built from one-sided walls only, so host pixel
tests exercise the solid path end to end. Two-sided-as-solid is an E1M1-only behavior
verified on the device smoke test, not asserted by host tests.

## Scope (out, deferred to later phases)

- Movement, controls, collision (P3). Camera stays a fixed or scripted pose in P2.
- Things and combat, sprites (P4).
- Textured floors and ceilings, visplanes, enemy AI, audio (P5). Floors are flat shade
  or blank in P2; visplanes explicitly deferred.
- Any change to the P1 subsystem beyond additive map and asset parsing it does not yet
  do.

## What P0 and P1 already provide (reuse)

| Asset | File | Status for P2 |
| --- | --- | --- |
| 4-bit framebuffer pack/get/clear | `plugins/games/doom/fb.h` | Reuse as-is |
| Typed map views (verts, segs, ssecs, sectors, lines, sides) | `plugins/games/doom/geom.h` | Reuse; add seg to sector helper |
| Typed NODES view + child helpers | `plugins/games/doom/geom.h` | Reuse for traversal |
| Palette and colormap to 16 gray, `shade_gray` | `plugins/games/doom/palette.h` | Wire wall shading through it |
| Spike projection math (camera transform, near clip, screen-x, wall height, affine v) | `plugins/games/doom/render.h` | Generalize; structure rebuilt |
| `cos_sin` per-TU seam (host cmath, ARM rodata LUT) | `render.h` decl, `doom_core_spike.cpp` def | Keep |
| No-heap DRAM bump arena | `plugins/games/doom/arena.h` | Texture cache backing store |
| WAV read door plus device adapter | `plugins/games/doom/wad_read.h` | Device WAD load path |
| Synthetic WAD builder | `harness/tools/wad_build.h` | Extend to multi-subsector + textures |

## P2 unit list

| Unit | What | Depends on | Independence |
| --- | --- | --- | --- |
| A | `geom.h` seg to sector resolution helper (seg -> linedef -> sidedef -> sector), for sector light and wall texture name | P1 geom | Tiny, sequential first |
| B | BSP NODES front-to-back traversal (partition-side test, near child first, subsector leaf dispatch) | A | Core; feeds C and D |
| C | solidsegs occlusion clip (sorted occluded column ranges, clip projected spans, insert solid walls) | B | Tight chain with B |
| D | Wall column draw, flat sector-shaded via `shade_gray` (generalize spike projection) | B, C | Tight chain |
| E | `TEXTURE1`/`PNAMES`/patch parse and lazy composition into the arena; textured column sampling | D | Most independent; the scope-down lever |
| F | Synthetic WAD extension (multi-subsector, real acyclic NODE split with an occlusion case, `TEXTURE1`/`PNAMES`/patch), regenerate `doom_test_map.h` | none | Independent; enables B..E pixel tests |
| G | `doom_core_spike` drives the production renderer from a WAD via the P1 read door on device | D (E for textures) | Plug-in integration |

### Unit F design constraints (load-bearing)

F is not just "exercise the NODES parser." Two constraints make it the prerequisite
for testing B, C, and D:

- Occlusion case required. Unit C (occlusion clip) has no failing test to drive it
  unless the map contains two walls that share screen columns at different depths from
  the camera pose. A single convex square (the current synthetic map) is one subsector
  with no wall behind another, so it cannot assert "far wall does not overwrite near
  one." F must build at least two subsectors split by a real partition, posed so one
  wall occludes another in some columns.
- Acyclic NODES required. The current `wad_build.h` NODES record is a cycle: child[1]
  is `0x0000`, an interior-node reference to node index 0, the node itself. Front-to-back
  traversal would infinite-loop on it. That record was valid only for the typed-view
  parse test, never for traversal. F must replace it with a real acyclic tree (root is
  the last node; each child is either a subsector leaf with the `0x8000` bit set or a
  lower node index that terminates).

### Unit B guard

Traversal must tolerate `numNodes == 0` (a node-less WAD): fall back to rendering the
subsectors directly rather than dereferencing a null root. `map_load` already tolerates
absent NODES; the traversal must too.

### Production renderer signature

`render_view` grows beyond the spike `(map, cam, fb)`. The production entry takes the
palette and colormap (for `shade_gray`) and, once Unit E lands, the arena (texture
cache). This ripples to `doom_core_spike.cpp` and `harness/tests/test_doom_render.cpp`;
the exact signature is fixed in the spec.

### Host tests bypass the read door

Host pixel tests feed the synthetic WAD bytes to `wad_open` directly. The WAV read door
is separately covered by P1 host tests and is only on the device load path (Unit G). Do
not wire the door into the render path; it is orthogonal to the pixel tests.

## Selected build order

Flat-shaded first, textures after. Land a correct geometry render (BSP traversal plus
occlusion plus flat depth-shaded walls) fully green and committed before adding texture
composition. This guarantees a shippable renderer even if textures are cut for budget,
and it isolates the code-size-sensitive texture unit (E) so its `.text` cost is measured
against a known-good baseline.

Sequence:

1. F (synthetic WAD with a real acyclic NODE split, an occlusion case, and texture
   lumps) and A (seg to sector helper) first, so the later units have real test fixtures
   and the sector lookup they need. F and A are independent of each other. F is the hard
   prerequisite for the C occlusion test.
2. B then C then D inline, TDD, each commit green. This is the tight dependency chain
   (traversal feeds clip feeds column draw); implement inline rather than parallel.
3. D ships flat-shaded walls: the first shippable milestone. Measure `.text`.
4. E adds `TEXTURE1`/`PNAMES`/patch composition and textured sampling on top, measured
   against the flat-shaded baseline. If E approaches the cap, stop at flat-shaded and
   defer textures with a named blocker.
5. G wires the device plug-in to the read door.

## Texture vs flat-shaded scope lever

Issue #3 sanctions dropping wall textures to flat-shaded sectors as the first
scope-down lever. The selected order makes this lever cheap: flat-shaded is the
committed milestone at step 3, so cutting textures means stopping after step 3 with the
renderer already green, not unwinding work. The trigger is a measured `.text` estimate
that approaches ~82 KB even with the texture unit minimized.

## Parallelization note

The renderer core (B -> C -> D) has a tight data dependency chain and is implemented
inline per the kickoff. Units E (texture composition) and F (synthetic WAD extension)
are the genuinely independent units. F is done first as a fixture prerequisite. E may be
dispatched to a worktree subagent if its size warrants, but inline is the default given
the single-subsystem coherence; revisit only if E proves large during the spec.

## Vendor and dependency pins

- `vendor/distingNT_API` at `cd12d87` (v1.15.0 family). Read-only.
- `vendor/llvm-project` sparse `compiler-rt/lib/builtins` at `llvmorg-19.1.0`
  (`a4bf6cd`). Read-only. Provisioned by `./bootstrap.sh`.

## Hard constraints

- Clean-room. No GPL Doom or PureDOOM source; WAD and rendering math reimplemented from
  documented formats.
- Never commit a real WAD. Tests use the synthetic `wad_build` WAD.
- No heap. `float` math is fine (FPU `fpv5-d16`). No libm `sinf` (rodata LUT via the
  `cos_sin` seam).
- Renderer plug-in `.text` under ~82 KB; WAD plus textures plus buffers DRAM under
  12 MB.
- TDD for all P2 logic, with render-to-buffer pixel assertions.

## Open questions for the spec

- The exact solidsegs structure (fixed-capacity sorted range array vs linked free list)
  and its capacity bound. Designed in the spec, not here.
- The production `render_view` signature (palette, colormap, arena params) and how
  `doom_core_spike` and `test_doom_render` adopt it.
- The synthetic two-subsector geometry and camera pose that produces the occlusion case
  Unit C asserts against.
