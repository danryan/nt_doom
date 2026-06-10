# Design: P3 player movement, collision, controls, real-WAD load

Date: 2026-06-09
Status: accepted
Phase: P3 (GitHub issue #4)
Branch: dr/p3-player-movement

## Context

P2 left `doom_core_spike` rendering an embedded synthetic WAD from a fixed camera at
`{0,0,0}`. P3 makes the camera a player against real geometry: load a real `DOOM1.WAD`
via the P1 read door, then move with CV and front-panel control and slide along solid
walls. Movement, collision, blockmap, and input conditioning are pure math, host-tested
first. The device assembly (real-WAD load, controls, serialise, overlay) is verified on
hardware. All math is libm-free and heap-free; `step` is real-time safe.

## Architecture

Six units. Units 1, 2, 4, 5 are pure headers tested directly (no plug-in-level sim flag
is needed because no production code path carries an injection seam; host tests call the
pure functions). Unit 3 depends on 1 and 4. Unit 6 assembles all on the device.

```
geom.h      (+Blockmap view)        movement.h      input.h
     \            |                      |              |
      \           v                      v              v
       +----> collision.h  <----  wad_build.h (build_move_test_wad)
                  |
                  v
        doom_core_spike.cpp  (real-WAD load, step, draw, params, customUi, serialise)
```

## Canonical recipes

### Recipe A: BLOCKMAP view (`geom.h`)

Doom BLOCKMAP lump, all little-endian int16/uint16 (matches the WAD byte order; cast
directly as the existing geom views do):

- Header (8 bytes): `originX` (int16), `originY` (int16), `cols` (uint16), `rows` (uint16).
- Offset table: `cols*rows` uint16 entries. Each is a word offset (units of int16, i.e.
  2 bytes) from the lump start to that cell's blocklist.
- Blocklist: a leading `0x0000` word, then a run of linedef indices (uint16), terminated
  by `0xFFFF`.

Block size is 128 map units. Cell of world point `(x,y)`:
`col = (int)floor((x - originX) / 128)`, `row = (int)floor((y - originY) / 128)`. Cell is
in range when `0 <= col < cols && 0 <= row < rows`.

```cpp
struct Blockmap {
    const uint8_t*  base    = nullptr;   // lump start (word-offset deref)
    int16_t         originX = 0, originY = 0;
    uint16_t        cols    = 0, rows = 0;
    const uint16_t* offsets = nullptr;   // cols*rows entries
    uint32_t        words   = 0;         // lumpSize/2, for bounds
};
bool blockmap_load(const Wad& w, const char* mapName, Blockmap& bm); // false if no BLOCKMAP
bool blockmap_cell_of(const Blockmap& bm, float x, float y, int& col, int& row); // false if OOB
// Iterate linedef indices in cell (col,row); no-op if OOB or offset past `words`.
template<class F> void blockmap_for_lines_in_cell(const Blockmap& bm, int col, int row, F fn);
```

`blockmap_for_lines_in_cell` reads `off = bm.offsets[row*cols + col]`, bounds-checks
`off < bm.words`, skips the leading word, then reads uint16 linedef indices until
`0xFFFF` or `words` is reached, calling `fn(int lineIndex)` for each.

### Recipe B: movement integrator (`movement.h`)

Pure functions over `Pose` and `Intent`. Heading is integrated (tank scheme). Uses the
per-TU `cos_sin` seam (libm-free on ARM). Forward is along `(cos a, sin a)`; strafe-right
is along the perpendicular `(sin a, -cos a)`.

```cpp
struct Pose      { float x, y, angle; };
struct Intent    { float forward, strafe, turn; bool fire; };   // forward/strafe/turn in [-1,1]
struct MoveTuning{ float moveSpeed, strafeSpeed, turnSpeed; };   // units/sec, units/sec, rad/sec
struct MoveDelta { float dx, dy; };

inline float     turn_angle(float angle, float turn, float turnSpeed, float dt);
inline MoveDelta move_delta(float angle, const Intent& in, const MoveTuning& t, float dt);
inline Pose      integrate (const Pose& p, const Intent& in, const MoveTuning& t, float dt);
```

`turn_angle = angle + turn*turnSpeed*dt`. `move_delta`: `cos_sin(angle, ca, sa);
f = in.forward*t.moveSpeed; s = in.strafe*t.strafeSpeed; dx = (f*ca + s*sa)*dt;
dy = (f*sa - s*ca)*dt`. `integrate`: `a = turn_angle(...); d = move_delta(a,...);
return {p.x+d.dx, p.y+d.dy, a}` (no collision; the device path composes
`turn_angle` + `move_delta` + `collide_move`).

### Recipe C: collision line-slide (`collision.h`)

Per-axis slide against solid linedefs from the BLOCKMAP broadphase. A linedef is solid
when one-sided (`back == 0xFFFF`) or flagged impassable (`flags & 0x0001`, ML_BLOCKING).

```cpp
struct Vec2 { float x, y; };
bool move_blocked(const Map& m, const Blockmap& bm,
                  float x0,float y0, float x1,float y1, float radius);
Vec2 collide_move(const Map& m, const Blockmap& bm, Vec2 from,
                  float dx, float dy, float radius);
```

`collide_move` (per-axis, slide implicit at corners):

```cpp
Vec2 p = from;
float nx = p.x + dx;
if (!move_blocked(m, bm, p.x, p.y, nx, p.y, radius)) p.x = nx;
float ny = p.y + dy;
if (!move_blocked(m, bm, p.x, p.y, p.x, ny, radius)) p.y = ny;
return p;
```

`move_blocked` broadphase: take the cells covering the swept bbox of `(x0,y0)-(x1,y1)`
expanded by `radius`, iterate their solid linedefs (re-testing a shared line is harmless).
Narrowphase per line, libm-free, blocked if either:

- the move segment `(x0,y0)-(x1,y1)` intersects the linedef segment (catches tunneling), or
- the squared distance from the destination `(x1,y1)` to the linedef segment is `< radius*radius`.

Segment intersection via sign-of-cross-product orientation tests; point-segment distance
via clamped projection. No `sqrt` (compare squared distances), no `atan2`.

### Recipe D: enclosed-room synthetic WAD + BLOCKMAP (`wad_build.h`)

`build_move_test_wad()`: an axis-aligned square room, walls on all four sides, one sector,
a player start in the center, palette/colormap (reuse the gray-ramp helpers), and a
generated BLOCKMAP. NODES omitted (`map_load` allows it; `numNodes==0` makes
`bsp_visit_order` enumerate the single subsector, so render works without an occlusion
tree).

- Vertices: `(0,0),(512,0),(512,512),(0,512)`. Four one-sided linedefs `0-1, 1-2, 2-3, 3-0`,
  `flags=1` (ML_BLOCKING), `back=0xFFFF`. Four sidedefs to sector 0, middle `WALL`.
- Four segs (one per line), one subsector (`numSegs=4, firstSeg=0`). One sector
  (floor 0, ceil 128, light 200).
- THINGS: player-1 start at `(256,256)` angle 0.
- BLOCKMAP: `origin (0,0)`, `cols=rows=5` (128-unit blocks cover 0..639, including the
  512 edge). Generator assigns each linedef to every cell whose `[c*128,c*128+128] x
  [r*128,r*128+128]` rectangle overlaps the linedef bounding box. This conservative
  overlap guarantees the broadphase never misses a wall near the player. Emit the header,
  the `cols*rows` offset table, and per-cell blocklists (leading `0x0000`, the cell's line
  indices, trailing `0xFFFF`), with offsets in int16 words from the lump start.

### Recipe E: input conditioning and mapping (`input.h`)

CV maps to velocity. Per axis: lowpass the raw volts (single-pole IIR), then deadzone +
normalize + square taper, sign-preserving. Bus scale is 1.0f = 1 V (hardware-confirmed);
`vmax = 5 V`, `dz = 0.1 V` are defaults, kept tunable.

```cpp
inline float cv_lowpass(float prev, float v, float k) { return prev + (v - prev) * k; }
inline float cv_to_norm(float v, float dz, float vmax) {       // -> [-1,1], libm-free
    float s = (v < 0.f) ? -1.f : 1.f;
    float m = (v < 0.f ? -v : v) - dz;
    if (m <= 0.f) return 0.f;
    float n = m / (vmax - dz);
    if (n > 1.f) n = 1.f;
    return s * n * n;                                          // squared taper
}
```

Bus read (device path, `input.h`): `read_cv_intent(busFrames, numFrames, cfg, lpState)`
samples the first frame of each configured bus (`busFrames[bus*numFrames + 0]`), lowpasses
into `lpState[axis]`, conditions via `cv_to_norm`, and fills an `Intent`. The fire gate is
a threshold on the fire bus. Read once per `step` block, never at audio rate.

### Recipe F: real-WAD device load (`doom_core_spike.cpp`)

Mirror the `wad_read_probe` recipe with a dynamic frame count:

- `calculateRequirements`: `req.numParameters = N`; `req.sram = sizeof(_doomSpike)`;
  `req.dram = 8*1024*1024` (WAD ~4.2 MB plus texture-composition arena, under 12 MB).
- `construct`: placement `new`; set `parameters`/`parameterPages`; init the DRAM arena over
  `ptrs.dram`; parse the embedded synthetic WAD (rodata bytes, no read needed) and
  `texcache_init` into the arena as the pre-load fallback; set a default pose; clear the
  async-load state (`scanned/readDone/readOk/parsed`) and the `alive` sentinel.
- `step` (guarded by `if (sampleRate == 0) return;`):
  1. One-shot: locate a 16-bit WAV whose name contains `DOOM1` (substring match, no libc),
     read its `numFrames` from `_NT_wavInfo`, and issue one `NT_readSampleFrames`
     (`dst = a->dram`, `numFrames = info.numFrames`, mono, 16-bit) via a persistent
     `_NT_wavRequest`. Set `scanned`.
  2. On `readDone && readOk && !parsed`: `arena_reset`; `arena_alloc(wadLen=numFrames*2)`
     to reserve the WAD span at the DRAM base; `wad_open`/`map_load("E1M1")`/`palette_load`/
     `colormap_load`/`texcache_init` into the remaining arena; `blockmap_load`; set the
     pose to the E1M1 player start; `parsed = true`.
  3. Movement every block: `dt = (numFramesBy4*4)/sampleRate`; build the `Intent` from
     `read_cv_intent` merged with the `customUi` latch; `a = turn_angle(...)`;
     `d = move_delta(a,...)`; `pos = collide_move(map, blockmap, {pose.x,pose.y}, d.dx,
     d.dy, radius)`; commit `pose`.
- `draw`: `render_view(map, {pose.x,pose.y,pose.angle}, pal, cm, tex, NT_screen)`. Overlay
  suppression: snapshot the bottom rows of `NT_screen` into a member cache and reset a
  post-draw counter; `step` restores them while the counter is active. Set `alive = true`.
  Return `true`.
- Parameters (`int16`-safe, names are rodata literals): `Move spd`, `Turn spd`,
  `Strafe spd`, `Radius`, `Deadzone`, `Fwd bus`, `Turn bus`, `Strafe bus`, `Fire bus`.
  `numParameters` equals the populated count.
- `parameterChanged(self, p)`: recompute `MoveTuning`/`InputConfig` from `self->v[]`. No
  `NT_setParameterFromUi` self-push here.
- `hasCustomUi`: return the OR of overridden controls (`kNT_encoderL|kNT_encoderR|
  kNT_button1|kNT_button2`). `customUi(self, data)`: encoders nudge a turn/move latch in
  the instance; buttons set the fire/use latch. Any self-push via `NT_setParameterFromUi`
  uses `NT_parameterOffset() + localIndex` and is gated behind the `alive` sentinel.
- `serialise(self, stream)`: `addMemberName("px"); addNumber(pose.x)` and likewise `py`,
  `pa` (floats; `addNumber(float)` exists). Never `addString`.
- `deserialise(self, parse)`: `matchName("px"); number(pose.x)` etc.; return success.
- `_NT_factory` field order (declaration order): `.guid, .name, .description,
  .numSpecifications, .calculateRequirements, .construct, .parameterChanged, .step, .draw,
  .tags, .hasCustomUi, .customUi, .serialise, .deserialise`.

## Per-unit entries

- Unit 1 (`geom.h` Blockmap): `Blockmap`, `blockmap_load`, `blockmap_cell_of`,
  `blockmap_for_lines_in_cell`. Test `[blockmap]` against `build_move_test_wad`.
- Unit 2 (`movement.h`): `Pose`, `Intent`, `MoveTuning`, `MoveDelta`, `turn_angle`,
  `move_delta`, `integrate`. Test `[movement]`.
- Unit 3 (`collision.h`): `Vec2`, `move_blocked`, `collide_move`, plus libm-free segment
  and point-segment geometry helpers. Test `[collision]` against `build_move_test_wad`.
- Unit 4 (`wad_build.h`): `build_move_test_wad()`. Exercised by units 1 and 3 tests.
- Unit 5 (`input.h`): `cv_lowpass`, `cv_to_norm`, `Intent` (shared from `movement.h` or a
  common header), `InputConfig`, `read_cv_intent`. Test `[input]` for the conditioning math.
- Unit 6 (`doom_core_spike.cpp`): real-WAD load, live-camera `step`/`draw`, params,
  `customUi`, `serialise`/`deserialise`, `parameterChanged`, overlay suppression. Hardware
  smoke (ARM build clean, ADD clean, walk E1M1).

## Verification footer

### Recipe spot-check

The real-WAD load (Recipe F) matches the shipped `wad_read_probe.cpp` recipe (name-substring
locate, persistent `_NT_wavRequest`, async `NT_readSampleFrames` into the DRAM grant,
arena parse on the callback). The only deltas are the dynamic frame count (`info.numFrames`
instead of the fixed `kWadFrames`), the `DOOM1` name match instead of `TESTMAP`, and the
post-read `texcache_init`/`blockmap_load`/pose-set, which are additive.

### Per-entry verification (three derivations traced end-to-end)

1. Movement integration step. `Pose{0,0,0}`, `Intent{forward=1, strafe=0, turn=0}`,
   `MoveTuning{moveSpeed=100, strafeSpeed=100, turnSpeed=1}`, `dt=0.1`. `turn_angle = 0 +
   0*1*0.1 = 0`. `cos_sin(0) = (1,0)`. `f = 1*100 = 100`, `s = 0`. `dx = (100*1 + 0)*0.1 =
   10`, `dy = (100*0 - 0)*0.1 = 0`. New pose `{10, 0, 0}`. Correct: forward at angle 0
   advances +x by `moveSpeed*dt`.

2. Line-slide against a known wall. `build_move_test_wad` left wall is linedef `3-0` from
   `(0,512)` to `(0,0)` (the `x=0` edge), one-sided/solid. Player at `(10,256)`, radius 16,
   desired delta `(-50,0)`. X axis: `nx = -40`; the destination `(-40,256)` has squared
   distance `40^2 = 1600` to the `x=0` segment, `< 16^2 = 256`? No, `1600 > 256`, but the
   move segment `(10,256)-(-40,256)` crosses `x=0` (intersects the wall), so `move_blocked`
   is true via the segment-intersection clause; X is rejected, `x` stays `10`. Y axis:
   `ny = 256` (dy=0), unblocked, `y` stays. Result `(10,256)`: blocked, never passes the
   wall. A second step with delta `(0,40)` slides: X rejected as before is not attempted
   (dx=0), Y `ny=296` is unblocked, player slides along the wall to `(10,296)`. Correct.

3. BLOCKMAP cell lookup against the Doom format. `build_move_test_wad` BLOCKMAP `origin
   (0,0)`, `cols=rows=5`. Point `(256,256)`: `col = floor((256-0)/128) = 2`, `row = 2`,
   in range. Offset index `row*cols + col = 2*5 + 2 = 12`; `off = offsets[12]` points at
   cell (2,2)'s blocklist; that interior cell overlaps no wall bbox, so the list is just
   `0x0000, 0xFFFF` and `blockmap_for_lines_in_cell` calls `fn` zero times (player free in
   the middle). Point `(4,256)`: `col=0,row=2`; cell (0,2) overlaps the `x=0` left wall's
   bbox, so its blocklist includes linedef index 3, and `fn(3)` fires. Correct against the
   documented header + word-offset + `0xFFFF`-terminated blocklist format.

Result: 3 of 3 derivations agree with the source format and the synthetic geometry. No
audit-every-entry trigger.

### Prereq verification (P1/P2 symbols and fields P3 depends on)

- `render.h`: `struct Camera{float x,y,angle}` (line 14); `render_view(m,cam,pal,cm,tex,fb)`
  (line 180); `point_on_side` (line 21); `cos_sin` per-TU decl (line 12). Present.
- `geom.h`: `LinedefRaw{v1,v2,flags,special,tag,front,back}` (line 12); `VertexRaw{x,y}`
  (line 8); `map_load` accepts a missing NODES lump (`numNodes=0`, lines 53-58). Present.
- `wad.h`: `wad_find_lump(w, name, start)` for map-relative lump find; `wad_lump_ptr`,
  `wad_lump_size`. Present (used by `map_load`).
- `arena.h`: `arena_init`, `arena_alloc`, `arena_reset`. Present.
- API: `_NT_wavInfo.numFrames` (uint32, `wav.h:79`); `_NT_wavRequest`, `NT_readSampleFrames`;
  `_NT_jsonStream::addNumber(float)` (`serialisation.h:59`); `_NT_jsonParse::number(float&)`
  (line 95); `_NT_uiData{pots,controls,lastButtons,encoders}` (api.h:362); factory fields
  `parameterChanged/hasCustomUi/customUi/serialise/deserialise` (api.h:439-498); `NT_setParameterFromUi`,
  `NT_parameterOffset`. Present.

All prerequisites exist. No abort.
