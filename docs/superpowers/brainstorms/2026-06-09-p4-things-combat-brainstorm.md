# Brainstorm: P4 things and combat

Date: 2026-06-09
Status: accepted
Phase: P4 (GitHub issue #5)
Branch: dr/p4-things-combat

## Scope

Render map things as camera-facing billboard sprites over real E1M1, correctly
occluded behind walls, and add a basic hitscan fire that registers a hit on the
nearest thing in front of the player. Sprite projection, depth clipping, and hitscan
are pure math, host-tested first. The sprite render integration and the device fire
wiring are verified on hardware. This completes the honest v1 target.

In scope:

- THINGS parse and a typed zero-copy view in `geom.h` (absent THINGS non-fatal).
- Sprite lump index over the `S_START`/`S_END` markers; a thing-type to sprite-name
  table derived clean-room from the documented Doom thing table.
- A `SpriteCache` that decodes a sprite patch lump and composes it column-major into
  the DRAM arena, mirroring `TextureCache`.
- A per-column wall depth buffer written during `render_view`, plus the sprite
  depth-clip seam (a sprite column draws only where it is nearer than the stored wall
  depth).
- Billboard sprite projection (camera-facing), multi-sprite back-to-front draw.
- Hitscan fire on a debounced fire gate: nearest thing in front, blocked by solid
  walls along the ray. Hit registration as a counter/flag.
- Device wiring on `doom_core_spike`: render things over E1M1 walls, fire registers a
  hit, a minimal on-screen hit indicator.
- A synthetic test WAD extended with a drawable THING and a sprite lump.

Out of scope (P5, issue #6):

- Enemy AI / state machines (idle/chase/attack/die).
- Textured floors and ceilings (visplanes), sky.
- Audio / sound effects.
- Damage model, health, weapons beyond a single hitscan, projectile weapons.
- Thing death animations and removal (a hit may flag/count; no death sequence).

## Vendor pins

- `vendor/distingNT_API` at `cd12d87` (the P3 pin). Read-only.
- `vendor/llvm-project` sparse `compiler-rt/lib/builtins` at `llvmorg-19.1.0` (`a4bf6cd`).

## What P0-P3 already provide

- WAD container parse (`wad.h`) with `wad_find_lump(w, name, start)`, which a sprite
  index uses to scan lumps between `S_START` and `S_END`.
- Zero-copy map view (`geom.h`); the THINGS lump is already present in every test WAD
  (records of `x, y, angle, type, flags`, five int16) but has no typed view yet.
- BSP renderer (`render.h`): `render_seg` already computes the per-column wall `depth`
  (`dA + (dB - dA) * t`) inside its draw lambda, and `SolidSegs` guarantees each column
  is drawn once by the nearest seg. That is the natural depth-buffer write site.
- Patch column decoder (`texture.h`): `tex_blit_patch` decodes the Doom patch post
  format (`topdelta, length, pad, Npx, pad, 0xFF`); sprites are patches, so the sprite
  decoder reuses this.
- `Intent.fire` (`movement.h`/`input.h`) already exists from CV bus 4 and a `customUi`
  button (`panelFire`), but nothing consumes it. P4 wires it to hitscan.
- Collision primitives (`collision.h`): `segs_intersect`, `point_seg_dist2`,
  `line_is_solid`, and the BLOCKMAP cell walk, all reusable for the hitscan wall block.
- `cos_sin` per-TU trig seam (ARM rodata LUT, host `<cmath>`); all P4 math stays libm-free.
- The 8 MB DRAM arena holds the real WAD plus texture and sprite composition under 12 MB.

## P4 unit list with dependencies

1. THINGS view (`geom.h`) plus a synthetic drawable thing and sprite lump in
   `wad_build.h`. Independent. Host `[things]`.
2. Sprite lump index and `SpriteCache` (new `sprite.h`): scan `S_START`/`S_END`, decode
   a sprite patch lump, compose column-major into the arena. Depends on unit 1 only for
   the test fixture. Host `[sprite]`.
3. Per-column wall depth buffer (`render.h`): extend `render_view` to write a caller
   supplied `float depth[kScreenW]`. Independent. Host `[depth]`.
4. Billboard projection, depth clip, multi-sprite render (`render.h`): project a thing
   to a column span and row span, clip each column against the depth buffer, draw
   back-to-front. Depends on units 2 and 3. Host `[sprite_render]`.
5. Hitscan resolver and thing-type to sprite-name table (new `combat.h`): nearest thing
   in front, wall-blocked along the ray. Depends on unit 1. Host `[combat]`.
6. `doom_core_spike` device integration: render things over E1M1, debounce the fire
   gate, register and indicate a hit. Depends on all. Hardware smoke.

Units 1, 3, and 5 are independent; 2 needs 1's fixture; 4 needs 2 and 3; 6 needs all.
The render-integration chain (depth buffer to projection to clip) is tight and lives in
the shared `render.h`/`geom.h` surface, so this is inline sequential TDD, not parallel
implementers. Parallel worktree dispatch on shared headers would cost more integration
tax than it saves at this size.

## Decisions

### Depth buffer: a per-column float z-buffer written by render_view

Add a caller-supplied `float depth[kScreenW]` to `render_view` via an extended
signature `render_view(m, cam, pal, cm, tex, fb, float* depthOut = nullptr)`. The
default `nullptr` keeps every P2/P3 caller and pixel test unchanged. When non-null,
`render_seg` writes the interpolated wall depth it already computes into `depth[x]` for
each drawn column; columns with no wall keep a far sentinel (`kFarDepth`). A sprite
column then draws only where its projected depth is strictly nearer than `depth[x]`.

Alternatives considered. A full per-pixel z-buffer (`float[256*64]` = 64 KB) is rejected:
it is far too large for instance SRAM and gives no benefit here, because a billboard is a
single depth per column. A 1/z buffer instead of depth was considered; plain depth is used
because `render_seg` already has it and the comparison direction is the same. Tradeoff:
one `float[256]` (1 KB) lives as an instance member (NOT a draw-stack local, per the P3
stack-overflow lesson), and the `.text` cost is a per-column store. Host-testable as a
pure clip predicate over the depth array and a sprite column span plus depth, with no
framebuffer.

### Sprite rotation: view-octant selection with a rotation-0 fallback

Doom sprite lumps are named `NNNNFR`: four-char sprite name, a frame letter, and a
rotation digit. A single-view thing (most items) uses rotation `0` (one lump for all
view angles); a directional thing (monsters) ships rotations `1` through `8`. For P4,
select the rotation by the angle the player views the thing from, relative to the
thing's own facing, choosing the nearest of the eight available rotations; fall back to
rotation `0` when the sprite is single-view, then to the first available rotation. The
frame letter is fixed at `A` (the first, static frame); animation is a P5 concern with
enemy AI.

The octant is computed libm-free: transform the player-to-thing vector into the thing's
local frame using `cos_sin` of the thing angle, then pick the octant by comparing the
forward and right components (an eight-way compass via sign and magnitude tests), no
`atan2`. Alternatives. Full-view only (always rotation 0 or always rotation 1) is the
simpler kickoff-sanctioned option but shows monsters only from one side; the octant
selection is modest extra code, host-testable in isolation, and renders directional
things correctly, so it is chosen. The per `(name, frame)` rotation availability is
recorded by the sprite index so selection never references a missing lump.

### Hitscan: nearest thing in front, blocked by solid walls along the ray

On a rising fire gate, cast a ray from the player along the facing. For each candidate
thing, test the ray against the thing's bounding circle (radius) and keep the nearest hit
with a positive ray parameter (in front). Reject any thing farther along the ray than the
nearest solid wall, so the player cannot shoot through walls. The wall block reuses the
existing BLOCKMAP broadphase and a libm-free ray-versus-segment parameter test against
solid linedefs.

Alternatives. Nearest-thing-only (no wall block) is the kickoff-sanctioned simplification
and is strictly less code, but it lets a shot register on a thing in the next room, which
reads as a bug on real E1M1. The primitives for a wall block already exist (`segs_intersect`
generalizes to a ray parameter; the blockmap cell walk is in `collision.h`), the `.text`
budget is ample, and the result is correct, so the wall block is included. The whole
resolver is a pure function over a thing list, an origin, and an aim direction, returning
the hit thing index or none; the rising-edge debounce of the gate lives in the device
`step`, not in the pure resolver.

### Thing-type to sprite-name table: a small clean-room static table

Map the Doom thing `type` to a sprite name with a small static table derived from the
documented Doom thing table (clean-room: the mapping is reconstructed from published
type and sprite-name documentation, not copied from GPL source). The table covers the
thing types that appear in E1M1 (a handful of monsters, items, and decorations) and
filters out the non-drawable types (player starts 1 through 4, the deathmatch start 11,
and the teleport destination 14). An unknown type is skipped rather than drawn with a
placeholder. The synthetic test WAD adds one drawable thing whose type maps to a sprite
lump it also carries, so the host render test exercises the full chain.

### Sprite composition shares the texture arena pattern and stays out of draw()

`SpriteCache` composes a sprite patch lump into the DRAM arena column-major and memoizes
it, exactly like `TextureCache`. Per the P3 stack lesson, composition runs in `step`
context (when the real WAD loads, all referenced sprites are composed up front), never
lazily inside the `draw()` call chain, so `draw()` only reads cached column data.

## Explicit exclusions

- No enemy state, movement, or reaction to being hit. A hit increments a counter; the
  thing is not removed and does not animate.
- No floor/ceiling, visplane, or sky rendering; the depth buffer holds wall depth only,
  and a sprite is clipped against walls, not against a floor.
- No projectile or spread weapon; a single instantaneous hitscan ray.
- No new probe plug-in; `doom_core_spike` is evolved per project convention.
- The algorithms themselves (projection math, depth-clip predicate, rotation octant,
  hitscan resolver) are specified in the design doc, not here.
