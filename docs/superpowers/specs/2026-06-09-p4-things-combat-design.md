# Design: P4 things and combat

Date: 2026-06-09
Status: accepted
Phase: P4 (GitHub issue #5)
Branch: dr/p4-things-combat

## Context

P3 left `doom_core_spike` walking real E1M1 with collision but rendering walls only.
P4 renders map things as camera-facing billboard sprites, occluded behind walls by a
new per-column wall depth buffer, and wires the existing fire gate to a hitscan that
registers a hit on the nearest thing in front of the player. Projection, depth clipping,
the rotation octant, and hitscan are pure math, host-tested first. The sprite render
integration and the device fire wiring are verified on hardware. All math is libm-free
and heap-free; `step` stays real-time safe and every new scratch buffer is an instance
member, never a `draw`/`step` stack local (the P3 stack-overflow lesson).

## Architecture

Six units. Units 1, 3, and 5 are independent pure headers; unit 2 needs unit 1's test
fixture; unit 4 needs units 2 and 3; unit 6 assembles all on the device. Tight,
shared-header chain, so inline sequential TDD (no parallel implementers).

```
geom.h (+ThingRaw view)        render.h (+per-column depth buffer)
     |        \                       |
     |         \                      |
     v          \                     v
 combat.h        sprite.h  ----->  render.h (render_things, depth-clipped)
 (type table,     (SpriteCache)           |
  hitscan)            \                    |
       \               \                   v
        +-------> doom_core_spike.cpp  (things render, fire debounce, hit indicator)
wad_build.h (build_things_test_wad) feeds the host tests for geom/sprite/render
```

## Canonical recipes

### Recipe A: THINGS view (`geom.h`)

The map `THINGS` lump is a record array, all little-endian int16 (cast directly, as the
other geom views do). Each record is `x, y, angle, type, flags` (10 bytes). THINGS is
optional: absent THINGS is non-fatal (mirror the existing NODES handling).

```cpp
#pragma pack(push, 1)
struct ThingRaw { int16_t x, y, angle, type, flags; };
#pragma pack(pop)
// added to Map: const ThingRaw* things = nullptr; int32_t numThings = 0;
```

In `map_load`, after the mandatory lumps, load THINGS optionally:

```cpp
int32_t thi = wad_find_lump(w, "THINGS", base);
if (thi < 0) { m.things = nullptr; m.numThings = 0; }
else { m.things = (const ThingRaw*)wad_lump_ptr(w, thi);
       m.numThings = (int32_t)(wad_lump_size(w, thi) / sizeof(ThingRaw)); }
```

THINGS precedes VERTEXES in the map directory, so the `start = base` forward scan finds
it. All thing access downstream is bounds-checked against `numThings`.

### Recipe B: sprite lump index and `SpriteCache` (`sprite.h`)

Sprite lumps sit between the zero-size marker lumps `S_START` and `S_END`. A sprite lump
name is `NNNNFR` (four-char sprite name, frame letter, rotation digit) or `NNNNFRF2R2`
(eight chars: a second frame/rotation that draws this same lump mirrored). Rotation `0`
is a single all-angles view; rotations `1` through `8` are the eight view octants.

```cpp
static const int     kMaxSprites = 128;       // distinct sprite lumps composed (E1M1 headroom)
static const uint8_t kSpriteGap  = 0xFF;      // transparent sentinel in the composed buffer

struct Sprite { const uint8_t* texels; int16_t w, h, left, top; };   // column-major

struct SpriteCache {
    const Wad* wad = nullptr;
    Arena*     arena = nullptr;
    int32_t    sStart = -1, sEnd = -1;        // marker lump indices (exclusive interior)
    mutable Sprite  entries[kMaxSprites];
    mutable bool    composed[kMaxSprites];
    mutable int32_t lumpOf[kMaxSprites];      // wad lump index each entry caches
    mutable int32_t count = 0;
};

bool spritecache_init(SpriteCache& sc, const Wad& w, Arena& a);   // locate S_START/S_END
// Find the lump for (name4, frame, rotation); sets flip when matched via the mirror pair.
int  sprite_find(const SpriteCache& sc, const char name4[4], char frame, int rotation, bool& flip);
const Sprite* sprite_get(const SpriteCache& sc, int lumpIndex);   // compose + memoize
```

`spritecache_init` sets `sStart = wad_find_lump(w, "S_START")` and
`sEnd = wad_find_lump(w, "S_END")`; returns false if either is missing.

`sprite_find` scans `i` in `(sStart, sEnd)`. For each lump it decodes pair 1
(`name[0..3]`, `name[4]`, `name[5]-'0'`) and, when `name[6] != 0`, pair 2 (`name[6]`,
`name[7]-'0'`, mirrored). It returns the lump index when a pair matches `(name4, frame,
rotation)`, with `flip = false` for pair 1 and `flip = true` for pair 2; otherwise `-1`.

`sprite_get` composes the patch lump column-major into the arena exactly like
`TextureCache::get`, with three differences: the `w`/`h`/`left`/`top` dimensions come
from the sprite patch header (the first four int16: width, height, left offset, top
offset) rather than a TEXTURE1 entry, the buffer is initialized to `kSpriteGap`
(transparent) rather than `0`, and the sprite stores `left`/`top`. A composed
texel equal to `kSpriteGap` is transparent at draw time. Known limitation: a sprite pixel
whose real palette index is `0xFF` is dropped; this is rare and accepted for P4. The
composer reuses the patch post format (`topdelta, length, pad, Npx, pad, 0xFF`).

### Recipe C: per-column wall depth buffer (`render.h`)

Extend `render_view` with an optional caller-supplied depth buffer. The default keeps
every P2/P3 caller and pixel test unchanged.

```cpp
static const float kFarDepth = 1.0e30f;   // no wall in this column

void render_view(const Map& m, const Camera& cam, const Palette& pal, const Colormap& cm,
                 const TextureCache* tex, uint8_t* fb, float* depthOut = nullptr);
```

When `depthOut != nullptr`: initialize `depthOut[0..kScreenW)` to `kFarDepth` before the
BSP walk, and thread the pointer through `render_subsector` into `render_seg`. Inside
`render_seg`'s existing per-column draw loop, after `depth` is computed, write
`depthOut[x] = depth`. Because the BSP walk is front-to-back and `SolidSegs` draws each
column exactly once (by the nearest seg), `depthOut[x]` holds the nearest wall depth.
The depth-clip predicate is pure and framebuffer-free:

```cpp
inline bool sprite_column_visible(float spriteDepth, float wallDepth) {
    return spriteDepth < wallDepth;   // sprite nearer than the wall in this column
}
```

### Recipe D: billboard projection and multi-sprite render (`render.h`)

Project a thing at world `(tx, ty)` into screen space with the same camera transform as
`render_seg` (`ca`, `sa` from `cos_sin(cam.angle)`).

```cpp
static const float kNearClip    = 1.0f;
static const float kSpriteScale = kWallScale;   // tie sprite size to the wall projection
static const int   kSpriteLight = 192;          // nominal sector light for sprite shading
static const float kDeg2Rad     = 0.017453293f; // THINGS angle is degrees; cos_sin wants radians

struct SpriteProj {
    bool  visible;          // in front of the near plane
    float depth;            // camera-space forward distance
    int   colL, colR;       // inclusive screen column span
    int   rowTop, rowBot;   // inclusive screen row span
};

inline SpriteProj project_thing(const Camera& cam, float ca, float sa,
                                float tx, float ty, int spriteW, int spriteH) {
    float dx = tx - cam.x, dy = ty - cam.y;
    float a1 =  dx * ca + dy * sa;        // depth
    float a2 = -dx * sa + dy * ca;        // lateral
    if (a1 <= kNearClip) return SpriteProj{false, a1, 0, 0, 0, 0};
    int colC  = (int)(kScreenW / 2 + (a2 / a1) * kFovScale);
    float scrW = (float)spriteW * kSpriteScale / a1;
    float scrH = (float)spriteH * kSpriteScale / a1;
    int halfW = (int)(scrW / 2), halfH = (int)(scrH / 2);
    int mid = kScreenH / 2;             // floor-less view: center the sprite on the horizon
    return SpriteProj{true, a1, colC - halfW, colC + halfW, mid - halfH, mid + halfH};
}
```

Vertical placement centers the sprite on the horizon row (`kScreenH/2`); the sprite
`top` offset and floor-relative feet placement are deferred with visplanes (P5). This is
the documented simplification for a floor-less view.

`render_things` draws all drawable things back-to-front, depth-clipped per column. The
type-to-sprite-name resolver is injected as a function pointer (`ThingSpriteFn`) rather
than called directly, so `render.h` does not depend on `combat.h`, the host render test
can supply a minimal resolver, and the render unit is testable before the thing table
exists.

```cpp
typedef const char* (*ThingSpriteFn)(int type);   // null when the type is undrawable
void render_things(const Map& m, const Camera& cam, const Palette& pal, const Colormap& cm,
                   const SpriteCache* sc, const float* depthBuf, uint8_t* fb,
                   int* order, int orderCap, ThingSpriteFn nameFn);   // order is caller scratch
```

Rotation selection needs only the thing's position and facing (not the sprite), and the
sort needs only depth (a billboard has one depth across all its columns). So the cull and
sort use the camera-space depth from the position alone, and the sprite is resolved during
the draw pass, after which the full projection runs:

1. `cos_sin(cam.angle, ca, sa)`.
2. Cull and collect. For each thing `i` in `[0, m.numThings)`: resolve
   `name = nameFn(thing.type)` (the device passes `thing_sprite_name`, Recipe E); skip when
   null. Compute the camera-space depth `a1 = (thing.x - cam.x)*ca + (thing.y - cam.y)*sa`;
   skip when `a1 <= kNearClip` (behind the near plane). Record `i` (keyed by `a1`), up to
   `orderCap` entries; drop the excess (defense-in-depth against a malformed `numThings`).
3. Sort the recorded indices far-first (descending `a1`) into `order` (insertion sort; the
   count is small and bounded by `orderCap`). `order` is caller scratch, never a stack array.
4. Draw each thing in `order`. Pick the rotation
   `rot = sprite_rotation(thing.angle * kDeg2Rad, thing.x - cam.x, thing.y - cam.y)`
   (THINGS angle is in degrees, so convert to radians). Resolve the lump
   `li = sprite_find(name, 'A', rot, flip)`, falling back to `sprite_find(name, 'A', 0, flip)`
   then `sprite_find(name, 'A', 1, flip)` when a request misses; skip the thing if all miss.
   `sp = sprite_get(li)` (cached; pre-composed on device). Project the full extent with
   `project_thing(cam, ca, sa, thing.x, thing.y, sp->w, sp->h)` for `colL`/`colR`/`rowTop`/
   `rowBot` and the constant billboard `depth`. For each column `x` in `[colL, colR]`
   clamped to `[0, kScreenWmax]`: if `sprite_column_visible(depth, depthBuf[x])`, sample
   column `u = ((x - colL) * sp->w) / (colR - colL + 1)` (mirror: `u = sp->w-1-u`); for each
   row `y` in `[rowTop, rowBot]` clamped to `[0, kScreenH-1]`:
   `v = ((y - rowTop) * sp->h) / (rowBot - rowTop + 1)`; `texel = sp->texels[u*sp->h + v]`;
   if `texel != kSpriteGap`, `fb_put(fb, x, y, shade_gray(pal, cm, texel,
   light_row(kSpriteLight, depth, cm.numMaps)))`. `kSpriteLight` is a fixed nominal sector
   light; per-thing sector light is a P5 refinement.

View rotation octant (libm-free; `toThingDx/Dy` is player-to-thing):

```cpp
inline int octant_of(float x, float y) {           // 0=E,1=NE,2=N,3=NW,4=W,5=SW,6=S,7=SE
    const float T = 0.41421356f;                   // tan(22.5 deg)
    float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    if (ay <= ax * T) return x >= 0 ? 0 : 4;        // near the x axis
    if (ax <= ay * T) return y >= 0 ? 2 : 6;        // near the y axis
    if (x >= 0) return y >= 0 ? 1 : 7;              // diagonal, +x
    else        return y >= 0 ? 3 : 5;              // diagonal, -x
}
inline int sprite_rotation(float thingAngle, float toThingDx, float toThingDy) {
    float vx = -toThingDx, vy = -toThingDy;         // thing -> viewer
    float ca, sa; cos_sin(thingAngle, ca, sa);
    float fwd =  vx * ca + vy * sa;                 // along the thing's facing
    float left = -vx * sa + vy * ca;
    return octant_of(fwd, left) + 1;                // rotation digit 1..8 (front = 1)
}
```

Single-rotation things (sprites that ship only rotation `0`) are handled by the
`sprite_find` fallback, so `sprite_rotation` never references a missing lump.

### Recipe E: thing-type to sprite-name table (`combat.h`)

A small static table maps the Doom thing `type` to a four-char sprite name, derived
clean-room from the documented Doom thing table. Non-drawable types (player starts 1-4,
deathmatch start 11, teleport destination 14) and unknown types return null (skipped, no
placeholder). The table covers the types that appear in E1M1; it is extensible.

```cpp
struct ThingSprite { int16_t type; char name[4]; };
const char* thing_sprite_name(int type);   // null when undrawable/unknown
```

Representative E1M1-focused entries (type to sprite name): 3004 POSS, 9 SPOS, 3001 TROO,
3002 SARG, 2035 BAR1, 2014 BON1, 2015 BON2, 2018 ARM1, 2019 ARM2, 2007 CLIP, 2048 AMMO,
2001 SHOT, 2005 CSAW, 2011 STIM, 2012 MEDI, 2013 SOUL, 8 BPAK, 5 BKEY, 6 YKEY, 13 RKEY,
2028 COLU (floor lamp), 34 CAND (candle), 35 CBRA (candelabra). The frame letter used for
rendering is `A` (the first, static frame). The audit during implementation reconciles
the table to the actual E1M1 thing types; any unlisted type renders nothing, which is
safe.

### Recipe F: hitscan resolver (`combat.h`)

Cast a unit-length ray from the player along the facing and pick the nearest drawable
thing whose bounding circle the ray crosses in front, not beyond the nearest solid wall.
All libm-free (`dir` is already unit length from `cos_sin`, so the ray parameter `t` is
in world units; circle test compares squared distances).

```cpp
int hitscan_nearest(const Map& m, const Blockmap& bm,
                    float ox, float oy, float dirx, float diry,
                    float maxDist, float thingRadius);   // thing index, or -1
```

Ray-versus-segment parameter `ray_seg_t` (for the wall block), `e = B - A`, `w = A - o`:

```cpp
inline bool ray_seg_t(float ox,float oy, float dx,float dy, float ax,float ay, float bx,float by,
                      float& t, float& s) {
    float ex = bx-ax, ey = by-ay, wx = ax-ox, wy = ay-oy;
    float denom = dx*ey - dy*ex;                 // cross(dir, e)
    if (denom == 0.0f) return false;
    t = (wx*ey - wy*ex) / denom;                 // distance along the ray (|dir| = 1)
    s = (wx*dy - wy*dx) / denom;                 // position along the segment
    return t >= 0.0f && s >= 0.0f && s <= 1.0f;  // a hit at distance t
}
```

Algorithm:

1. Wall distance: gather solid linedefs from the BLOCKMAP cells covering the ray segment
   `(ox,oy)-(ox + dirx*maxDist, oy + diry*maxDist)` bbox (reuse the `move_blocked` cell
   clamp). For each solid line (`line_is_solid`), compute the ray parameter `t` above and
   keep the minimum positive `t` as `wallDist` (default `maxDist`).
2. Thing distance: for each thing with a drawable type, project the center onto the ray:
   `tc = (cx-ox)*dirx + (cy-oy)*diry`. Skip when `tc <= 0` (behind). Closest-approach
   point `p = o + dir*tc`; `perp2 = (cx-px)^2 + (cy-py)^2`. When `perp2 <= thingRadius^2`,
   the ray crosses the circle; use `tc` as the hit distance. Keep the nearest `tc`.
3. Return the nearest thing index when its `tc < wallDist`; otherwise `-1`.

The rising-edge debounce of `Intent.fire` lives in the device `step`, not in this pure
resolver, which is a pure function of the thing list, origin, and aim direction.

### Recipe G: device wiring (`doom_core_spike.cpp`)

- New instance members: `doom::Wad wad; doom::SpriteCache sprites; bool spriteReady;
  float depthBuf[doom::kScreenW]; int thingOrder[kMaxThings]; bool prevFire; int hitCount;
  int hitThing;` (`kMaxThings` bounds the visible-thing scratch, e.g. 256). `depthBuf` and
  `thingOrder` are instance members (SRAM), never draw-stack locals.
- Lifetime: `wad` is an instance member, not a `swapRealWad` stack local, because
  `sprite_find` (called by `render_things` in `draw`) dereferences `SpriteCache::wad` to
  read sprite-lump names at draw time. A stack-local `Wad` would dangle after `swapRealWad`
  returns and fault on the next draw. (`TextureCache` happens to never deref its `wad`
  after the up-front compose, but pointing it at the instance `wad` too removes the latent
  fragility.) The `Wad` struct's `base`/`dir` point into the arena, so the copy stays valid
  until the next load resets the arena and rebuilds `wad`.
- `calculateRequirements`: unchanged `numParameters` (no new parameters, so no parameter
  count change). `req.sram = sizeof(_doomSpike)` grows (SpriteCache plus `depthBuf` 1 KB
  plus `thingOrder`); `req.dram` stays `8 MB`. The enlarged SRAM struct means the device
  needs a reboot (`0x7F`) before `calculateRequirements` re-reads the size (SRAM cache).
- `construct`: init the new members; leave sprites off until the real WAD loads.
- `swapRealWad`: open the WAD into the instance `wad` (`wad_open(wadBytes, wadLen, a->wad)`)
  and use `a->wad` for `map_load`/`palette_load`/`colormap_load`/`texcache_init`/
  `blockmap_load` so all caches reference the retained `Wad`. After `texcache_init` and
  texture composition, `spritecache_init(a->sprites, a->wad, a->arena)`; then compose every
  sprite a drawable thing references, up front (in step context, never in draw): iterate
  `a->map.things`, resolve `thing_sprite_name(type)`, and for each `(name, frame 'A')`
  `sprite_find`/`sprite_get` each present rotation lump (capped at `kMaxSprites`; the
  composer memoizes, so repeated names are cheap). Set `spriteReady`. The synthetic
  fallback keeps sprites off (no sprite lumps).
- `step`: after movement, debounce fire. Track `prevFire`; on a rising edge
  (`in.fire && !prevFire`) with `parsed && bmReady`, cast `dir = cos_sin(pose.angle)` and
  call `hitscan_nearest`; on a hit, `++hitCount` and store `hitThing`. Update `prevFire`.
  Real-time safe: bounded blockmap walk, no allocation.
- `draw`: `render_view(map, cam, pal, cm, tex, NT_screen, depthBuf)`; when `spriteReady`,
  `render_things(map, cam, pal, cm, &sprites, depthBuf, NT_screen, thingOrder, kMaxThings,
  thing_sprite_name)`.
  Draw a minimal center crosshair and a hit indicator (e.g. `hitCount` rendered as a short
  bar, or a brief flash when `hitThing` was just set). Keep the overlay-suppression
  snapshot. Return `true`.
- No serialise change: `hitCount`/`hitThing` are transient (do not survive a reload), so
  the parameter set and the serialise blob are unchanged from P3.

### Recipe H: synthetic things test WAD (`wad_build.h`)

`build_things_test_wad()`: extend the `build_bsp_test_wad` geometry (near wall x=100, far
wall x=300, one acyclic node) with a drawable thing and a sprite lump, so the host render
test exercises projection plus depth clip end-to-end against a known wall depth.

- THINGS: keep the player-1 start and add one barrel (type 2035, maps to `BAR1`) at a
  world position chosen so it projects to a known column and sits between the near and far
  walls in depth (e.g. `(200, 0)`), facing 0.
- Sprite lumps: `S_START`, then `BAR1A0` (a small patch, e.g. 16x32, frame A rotation 0,
  full opaque posts with a deterministic pixel pattern that avoids index `0xFF`), then
  `S_END`.
- Reuse the existing PLAYPAL/COLORMAP gray-ramp and TEXTURE1/PNAMES/PWALL from
  `build_bsp_test_wad` so walls still texture and the depth buffer fills.

## Per-unit entries

- Unit 1 (`geom.h` THINGS + `wad_build.h`): `ThingRaw`, `Map::things`/`numThings`,
  `map_load` THINGS load; `build_things_test_wad()`. Test `[things]`.
- Unit 2 (`sprite.h`): `Sprite`, `SpriteCache`, `spritecache_init`, `sprite_find`,
  `sprite_get`. Test `[sprite]` (name decode, mirror pair, compose against the test WAD).
- Unit 3 (`render.h` depth): extended `render_view` with `depthOut`, `kFarDepth`,
  `sprite_column_visible`. Test `[depth]` (depth buffer filled by a known wall; clip
  predicate).
- Unit 4 (`render.h` sprites): `project_thing`, `octant_of`, `sprite_rotation`,
  `ThingSpriteFn`, `render_things`. Test `[sprite_render]` (projection numbers, rotation
  octant, a pixel test, via a local resolver, that a sprite draws in front of and is
  occluded behind a wall).
- Unit 5 (`combat.h`): `thing_sprite_name`, `hitscan_nearest`, the ray-segment helper.
  Test `[combat]` (nearest pick, wall block, undrawable filter).
- Unit 6 (`doom_core_spike.cpp`): sprite compose on load, `render_things` in draw, fire
  debounce, hit indicator. Hardware smoke (ARM clean, ADD clean, things visible and
  occluded, firing registers a hit).

## Verification footer

### Recipe spot-check

`SpriteCache` (Recipe B) mirrors the shipped `TextureCache` (`texture.h`): the same
`tex_entry`/compose-into-arena/memoize shape and the same patch post decoder
(`topdelta, length, pad, Npx, pad, 0xFF`). The only deltas are the `S_START`/`S_END`
lump-range index instead of TEXTURE1, the `kSpriteGap = 0xFF` transparent init instead of
`0`, and the stored `left`/`top` offsets. The depth buffer (Recipe C) writes the `depth`
value `render_seg` already computes at `render.h:156`; it adds an init loop and one store
per drawn column, no new traversal.

### Per-entry verification (four derivations traced end-to-end)

1. Sprite projection plus depth clip. Camera `{0,0,0}` (facing +x), `ca=1, sa=0`. Barrel
   at `(200, 0)`, sprite `16x32`. `dx=200, dy=0`; `a1 = 200*1 + 0 = 200` (depth);
   `a2 = -200*0 + 0 = 0` (lateral). `a1 > kNearClip`, visible. `colC = 128 + (0/200)*128 =
   128`. `scrW = 16*32/200 = 2.56`, `halfW = 1`; `scrH = 32*32/200 = 5.12`, `halfH = 2`.
   Column span `[127,129]`, row span `[30,34]`. Now the depth buffer: the near wall at
   x=100 projects to a wall depth near 100 across the central columns, the far wall at
   x=300 to ~300. With `depthBuf[128] = 300` (only the far wall behind the sprite at that
   column), `sprite_column_visible(200, 300) = true` -> the sprite draws. With
   `depthBuf[128] = 100` (the near wall in front), `sprite_column_visible(200, 100) =
   false` -> occluded. Correct: the barrel at depth 200 hides behind the x=100 wall and
   shows in front of the x=300 wall.

2. Hitscan nearest pick with a wall block. Player `(0,0)`, facing +x: `dir = (1,0)`. Things
   at `(100,0)` (drawable) and `(300,0)` (drawable), `thingRadius = 20`, `maxDist = 1000`.
   No wall first: thing1 `tc = (100-0)*1 + (0-0)*0 = 100`, closest point `(100,0)`,
   `perp2 = 0 <= 400` -> hit at 100. thing2 `tc = 300`, `perp2 = 0` -> hit at 300. Nearest
   `tc = 100` -> thing1. Add a solid wall segment from `(200,-50)` to `(200,50)`: `e =
   (0,100)`, `w = A - o = (200,-50)`. `denom = dirx*ey - diry*ex = 1*100 - 0*0 = 100`;
   `t = (wx*ey - wy*ex)/denom = (200*100 - (-50)*0)/100 = 200`; `s = (wx*diry - wy*dirx)/
   denom = (200*0 - (-50)*1)/100 = 0.5` in `[0,1]` -> `wallDist = 200`. thing1 `tc=100 <
   200` kept; thing2 `tc=300 >= 200` rejected (behind the wall). Nearest in front of the
   wall = thing1. Correct.

3. Sprite lump name decode against the Doom format. Lump `POSSA2A8`: pair 1 = (`POSS`,
   `A`, `2`); `name[6]='A' != 0` so pair 2 = (`POSS`, `A`, `8`, mirrored).
   `sprite_find("POSS",'A',2)` matches pair 1 -> this lump, `flip=false`;
   `sprite_find("POSS",'A',8)` matches pair 2 -> this lump, `flip=true`. Lump `BAR1A0`:
   `name[6]=0`, single pair = (`BAR1`, `A`, `0`); `sprite_find("BAR1",'A',0)` -> this
   lump, `flip=false`; a request for rotation `1` misses, and the projection's
   rotation-0 fallback then resolves it. Correct against the documented `NNNNFR` /
   `NNNNFRF2R2` naming.

4. Rotation octant with the degrees conversion. A monster at facing angle 0 (degrees), so
   `thingAngle = 0 * kDeg2Rad = 0`, `cos_sin(0) = (1,0)`. Viewer at the origin, monster east
   at `(200,0)`: player-to-thing `(200,0)`, so thing-to-viewer `(vx,vy) = (-200,0)`.
   `fwd = vx*1 + vy*0 = -200` (the viewer is opposite the facing), `left = -vx*0 + vy*1 = 0`.
   `octant_of(-200, 0)`: `ay=0 <= ax*T` and `x<0` -> octant 4 -> rotation `4+1 = 5` (the
   back). The mirror case, monster west at `(-200,0)` facing 0: thing-to-viewer `(200,0)`,
   `fwd=200 > 0`, `octant 0` -> rotation `1` (the front, the monster faces the viewer).
   Correct: front is rotation 1, back is rotation 5. The conversion matters because a real
   E1M1 monster facing 90 (degrees) fed raw to `cos_sin` (radians) would pick a garbage
   octant; `kDeg2Rad` is mandatory and the synthetic angle-0 fixture would not have exposed
   its absence.

Result: 4 of 4 derivations agree with the source format and the synthetic geometry. No
audit-every-entry trigger.

### Prereq verification (P1/P2/P3 symbols and fields P4 depends on)

- `render.h`: `render_seg` computes per-column `depth = dA + (dB-dA)*t` (line 156), the
  depth-buffer write site; `render_view(m,cam,pal,cm,tex,fb)` (line 185) is extended with
  the optional `depthOut`; `kScreenW`/`kScreenH`/`kScreenWmax` (lines 16-18); `kFovScale`/
  `kWallScale` (lines 99-100); `light_row` (line 104); `cos_sin` decl (line 12). Present.
- `texture.h`: `tex_blit_patch` patch post decoder and the compose-into-arena/memoize
  pattern (lines 64-113); `tex_u16/tex_s16/tex_u32` readers. Present, reusable for sprites.
- `geom.h`: `Map` and `map_load` (optional-lump pattern at NODES, lines 53-58); `Blockmap`
  plus `blockmap_for_lines_in_cell` (lines 92-143). `wad.h`: `wad_find_lump(w,name,start)`
  (line 48), `wad_lump_ptr`/`wad_lump_size`. Present.
- `collision.h`: `segs_intersect`, `point_seg_dist2`, `line_is_solid`, and the
  `move_blocked` cell-clamp broadphase (lines 15-74), reusable for the hitscan wall block.
- `movement.h`/`input.h`: `Intent{forward,strafe,turn,fire}` (movement.h:10); the fire gate
  is produced by `read_cv_intent` and the `customUi` `panelFire` latch but currently
  unconsumed. `arena.h`: `arena_alloc`. Present.

All prerequisites exist. No abort.
