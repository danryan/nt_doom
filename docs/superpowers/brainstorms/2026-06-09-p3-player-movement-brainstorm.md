# Brainstorm: P3 player movement, collision, controls, real-WAD load

Date: 2026-06-09
Status: accepted
Phase: P3 (GitHub issue #4)
Branch: dr/p3-player-movement

## Scope

Make the camera a player against real geometry. Load a real `DOOM1.WAD` on device
via the P1 read door, then move the camera through the map with collision, driven by
CV buses and the front panel. Movement and collision are pure math, host-tested first.
The device assembly (real-WAD load, controls, serialise) is verified on hardware.

In scope:

- Real-WAD device load: `doom_core_spike` reads a real `DOOM1.WAD` (WAV-smuggled) via
  `NT_readSampleFrames` into a DRAM arena, replacing the embedded synthetic WAD on the
  device path. Real textures and palette render for free (P1/P2 already compose them).
- Player movement: forward/back, strafe, turn, time-stepped by `dt = numFrames / sampleRate`.
- BLOCKMAP-broadphase line-slide collision against solid linedefs.
- Inputs: CV buses (turn, move, strafe, fire gate) plus a front-panel `customUi` mirror.
  `int16`-safe parameters for speed/sensitivity. Pose serialise.
- Full-screen render from the live camera with overlay suppression.
- A synthetic enclosed-room WAD with a BLOCKMAP for host movement/collision tests.

Out of scope (later phases):

- Things, sprites, combat, hitscan (P4).
- Textured floors/ceilings, visplanes, enemy AI, audio, portal openings, sky (P5).
- Vertical look, jump, 6-DOF. Z stays Doom eye height; movement is planar.
- Any P1/P2 engine change beyond what movement, collision, real-WAD load, controls need.

## Vendor pins

- `vendor/distingNT_API` at `cd12d87` (v1.15.0 family). Read-only.
- `vendor/llvm-project` sparse `compiler-rt/lib/builtins` at `llvmorg-19.1.0` (`a4bf6cd`).

## What P0/P1/P2 already provide

- WAD container parse (`wad.h`), zero-copy map view (`geom.h`), 4-bit FB (`fb.h`).
- BSP renderer with occlusion and textured walls (`render.h`), `TextureCache` (`texture.h`).
- Palette/colormap to 16-level gray (`palette.h`). No-heap DRAM arena (`arena.h`).
- Read-door seam (`wad_read.h`) and the device adapter recipe (`wad_read_probe.cpp`).
- `Camera{x,y,angle}` is already movable; `render_view` consumes the live pose. The
  moving camera reuses `point_on_side` and `bsp_visit_order` unchanged.
- `cos_sin` is a per-TU seam: ARM uses a 256-entry rodata sine LUT
  (`doom_core_spike.cpp`), host TUs define it with `<cmath>`. Movement is libm-free.

## P3 unit list with dependencies

1. BLOCKMAP view (`geom.h`): parse the Doom BLOCKMAP lump (uniform 128-unit grid of
   per-cell linedef lists). Independent. Host `[blockmap]`.
2. Movement integrator (`movement.h`): pure `(pose, intent, dt) -> pose`. Independent.
   Host `[movement]`.
3. Collision resolver (`collision.h`): line-slide against solid linedefs, BLOCKMAP
   broadphase. Depends on units 1 and 4. Host `[collision]`.
4. Synthetic enclosed-room WAD + BLOCKMAP (`wad_build.h`): `build_move_test_wad()`.
   Independent. Host fixture for units 1 and 3.
5. Input seam (`input.h`): an `Intent` struct plus the CV-bus and `customUi` mapping,
   gated behind a sim-only `-D` flag so host tests inject intents directly. Independent.
   Host `[input]` for the mapping math.
6. `doom_core_spike` device integration: real-WAD async load (dynamic frame count, DRAM
   grant), live-camera `step`/`draw`, `int16` params, `customUi` mirror, `serialise`
   pose, `parameterChanged` sentinel, overlay suppression. Depends on all. Hardware smoke.

Units 1, 2, 4, 5 are independent; 3 needs 1+4; 6 needs all. The chain (movement ->
collision -> integration) is tight, so this is inline TDD, not parallel implementers.

## Decisions

### Control source: both CV buses and front-panel customUi

The issue requires both. CV buses give cable-free or patched control; the front-panel
`customUi` mirror lets a user walk the map without a CV source. Both write the same
`Intent` consumed by the integrator. The `int16` parameters (move speed, turn speed,
strafe speed, player radius) tune the mapping; encoders/buttons in `customUi` set an
intent latch that `step` reads. Rationale: deliverable requires both; sharing one
`Intent` keeps the integrator single-sourced.

### CV maps to velocity, not position; tank heading scheme

Map CV to velocity, never to an absolute position. Position mapping would lock the
player to a fixed coordinate range; velocity mapping traverses arbitrary maps and feels
like normal navigation. Use the tank scheme (Doom-authentic): CV1 is forward/back along
the heading, CV2 is turn rate, the heading is integrated from CV2. A raycaster needs an
explicit facing angle anyway, so the integrated heading is free; the absolute
(twin-stick, heading via `atan2`) alternative is rejected because it needs `atan2`
(libm) and does not match a raycaster's facing requirement. Strafe is a third CV axis
along the heading perpendicular; the fire gate is a fourth. Encoders mirror move/turn;
buttons cover fire/use and weapon switch (weapon switch is a P4 stub here).

### CV conditioning: deadzone, normalize, square taper, lowpass

Per axis, treat the input as bipolar volts with 0 V = stationary. Apply a deadzone so
noise and DC offset near 0 V do not drift the player, normalize to full scale, and
square the magnitude for fine low-speed control (sign-preserving). Lowpass the raw CV
with a single-pole IIR (`c += (v - c) * k`) before conditioning to kill jitter and
zipper. Read CV once per game frame (once per `step` block), not at audio rate; the
integrator advances by `dt = numFrames / sampleRate`. Bus scale is hardware-confirmed
1.0f = 1 V (CLAUDE.md), so full-scale `vmax = 5 V` and deadzone `0.1 V` are concrete;
the conditioning stays a tunable seam.

### Collision narrowphase: per-axis slide (not tangent projection)

Resolve the move per axis: attempt the X component against the current Y, commit it if
unblocked, then attempt the Y component against the (possibly updated) X. This yields
wall-sliding cheaply on a grid map and avoids the tangent-projection vector math. Each
axis test asks the BLOCKMAP broadphase for the solid linedefs near the player and
rejects the component if the swept point would cross one within the player radius. Full
vector slide with corner iteration is overkill for an axis-aligned grid map and is not
used.

### Collision broadphase: BLOCKMAP (not brute force)

E1M1 has hundreds of linedefs. A per-step brute-force scan of all of them is O(lines)
every audio block and risks the real-time budget. BLOCKMAP is the documented Doom
broadphase: a uniform 128-unit grid where each cell lists the linedefs crossing it, so
a step tests only the handful of lines in the player's cell neighborhood. Tradeoff: a
parser plus a small per-step cell walk (a few hundred bytes of `.text`) against bounded
O(lines-near-player) runtime. The issue names BLOCKMAP explicitly. Brute-force over a
bounded line set is the documented fallback if BLOCKMAP parse proves costly, but the
budget shows ample `.text` headroom, so BLOCKMAP is the choice.

### Embedded synthetic WAD stays as the pre-load device fallback

The device real-WAD read is async (locate file, issue read, parse on callback). Before
it completes, and when no `DOOM1.WAD` is on the card, the plug-in still renders the
existing embedded synthetic WAD (flat) so ADD always shows geometry. Once the real read
parses, the device swaps to real E1M1. Collision is exercised by host tests and the real
E1M1 smoke (E1M1 ships a BLOCKMAP); the embedded fallback only needs to render. This
avoids regenerating the embedded array and keeps the swap simple.

### Player model: point with a small radius

Treat the player as a point with a radius (Doom uses 16 units). The per-axis slide
narrowphase (above) handles corners implicitly: a blocked X keeps the unblocked Y and
vice versa, so the player slides along a wall and stops at a corner without an unbounded
loop. No heap; fixed scratch only.

### Real-WAD frame count is dynamic

The probe read a fixed `kWadFrames` for the tiny synthetic WAD. A real `DOOM1.WAD` is
~4.2 MB, so the device reads `info.frames` from `NT_getSampleFileInfo` and sets
`numFrames` from it. `wadLen = frames * 2`. The DRAM grant (~8 MB) holds the WAD plus the
texture-composition arena, under the 12 MB cap.

## Explicit exclusions

- No floor/ceiling height clipping for movement (planar move at fixed eye height; step-up
  and fall are P4/P5). Solid-wall blocking only.
- No two-sided portal traversal logic in collision beyond treating impassable two-sided
  lines as solid; openings are P5.
- No new probe plug-in; evolve `doom_core_spike` per project convention.
- The collision algorithm itself (slide math, blockmap cell walk) is designed in the
  spec, not here.
