# Design: P2 BSP wall renderer

Date: 2026-06-08
Status: Draft, pending user review
Issue: #3 (depends on #2, P1 complete)
Branch: `dr/p2-bsp-renderer`
Brainstorm: `docs/superpowers/brainstorms/2026-06-08-p2-bsp-renderer-brainstorm.md`

## Context

P2 replaces the single-subsector spike renderer with a real BSP front-to-back
traversal, solid-seg occlusion, and perspective wall columns shaded through the P1
palette and colormap. It consumes the P1 WAD subsystem (map views, arena, palette) and
the P0 framebuffer and projection math. The wall-type decision (one-sided solid walls;
two-sided drawn as solid; portals deferred to P5) is fixed in the brainstorm.

This spec designs the load-bearing infrastructure: the traversal order, the solidsegs
clip structure, the wall-column projection and affine sampling, the
`TEXTURE1`/`PNAMES`/patch composition layout, the flat-shaded fallback, the production
`render_view` signature, and the synthetic BSP test map. Per-unit entries and a
verification footer follow.

## Decisions

- One renderer header. `render.h` is rewritten in place as the production renderer.
  The spike `render_view(map, cam, fb)` is replaced; the spike's projection math
  (camera transform, near clip, screen-x, wall height, affine v) is retained and
  generalized.
- Flat-shaded first. Units land BSP traversal plus occlusion plus flat sector-shaded
  walls fully green before texture composition. Flat-shaded is the shippable milestone
  and the budget fallback.
- New synthetic builder. `build_bsp_test_wad()` is added alongside the existing
  `build_test_wad()`. The existing builder and its three P1 host tests (parse, geom
  nodes, square render) stay byte-for-byte and green. Host render tests for P2 call the
  new builder directly. The ARM `doom_test_map.h` gains `kDoomBspTestWad`; the spike
  loads it.
- Recursion for traversal. The BSP descent recurses; tree depth is `O(log subsectors)`
  and bounded, so no explicit stack is needed.
- Span-list occlusion. The solidsegs clip is the classic Doom sorted occluded-range
  list (frozen recipe), fixed-capacity, no heap.
- Optional textures via a nullable cache. `render_view` takes a `const TextureCache*`;
  `nullptr` selects flat-shaded sectors. Unit D passes `nullptr`; Unit E builds the
  cache and passes it. `render.h` forward-declares `struct TextureCache;` so Unit D
  compiles before `texture.h` exists (a pointer to an incomplete type plus `nullptr` is
  legal; only `texture_sample`, compiled in Unit E, dereferences it).
- Render tests use the acyclic BSP fixture only. The P2 `[render]` tests drive
  `build_bsp_test_wad()`. The existing square-room `[render]` test (which feeds
  `build_test_wad()`, whose NODES is a deliberate parse-only cycle) is replaced, not
  re-pointed at the BSP renderer: traversing the cyclic fake node would otherwise loop
  until the depth guard trips and render nothing useful. `build_test_wad()` stays the
  fixture for the parse and geom-node tests only, which never traverse.

## Architecture

```text
                    render_view(m, cam, pal, cm, tex?, fb)
                                  |
                          fb_clear; cos_sin
                                  |
              bsp_visit_order(m, cam) -> [ssec indices, front-to-back]
                                  |
              for each listed subsector: render_subsector(i)
                                  |
              for each seg: project_seg -> [sxA,sxB] + depths
                                  |
              solidsegs_clip_solid([sxA,sxB], drawSpan)
                                  |
              drawSpan(visFirst, visLast): per column ->
                 depth, wallH, top/bot
                 flat:    base index + light_row(sectorLight, depth)
                 textured: u (affine) -> tex column -> v step
                 fb_put(x, y, shade_gray(pal, cm, palIndex, light))
```

Modules:

- `plugins/games/doom/geom.h`: add `seg_sector(m, segIndex)` and `seg_wall_name(m,
  segIndex)` helpers (Unit A). No change to existing structs.
- `plugins/games/doom/render.h`: production renderer (Units B, C, D), optional texture
  sampling (Unit E hook). Holds the solidsegs structure and the traversal.
- `plugins/games/doom/texture.h` (new, Unit E): `TextureCache`, `TEXTURE1`/`PNAMES`
  parse, patch composition.
- `harness/tools/wad_build.h`: add `build_bsp_test_wad()` (Unit F).
- `plugins/games/doom_core_spike.cpp`: load `kDoomBspTestWad`, call the production
  `render_view`; device read-door wiring (Unit G).

## Unit A: seg to sector and wall-name resolution

`geom.h` gains pure helpers. A seg references a linedef and a side (`0` front/right,
`1` back/left). The sidedef gives the sector and the wall texture names.

```cpp
inline int16_t seg_sidedef_index(const Map& m, int32_t segIndex) {
    const SegRaw& s = m.segs[segIndex];
    const LinedefRaw& ld = m.lines[s.linedef];
    return s.side == 0 ? ld.front : ld.back;   // 0xFFFF when absent
}
inline const SidedefRaw* seg_sidedef(const Map& m, int32_t segIndex) {
    int16_t si = seg_sidedef_index(m, segIndex);
    if ((uint16_t)si == 0xFFFF || si < 0 || si >= m.numSides) return nullptr;
    return &m.sides[si];
}
inline const SectorRaw* seg_sector(const Map& m, int32_t segIndex) {
    const SidedefRaw* sd = seg_sidedef(m, segIndex);
    if (!sd || sd->sector < 0 || sd->sector >= m.numSectors) return nullptr;
    return &m.sectors[sd->sector];
}
inline bool seg_is_one_sided(const Map& m, int32_t segIndex) {
    return (uint16_t)m.lines[m.segs[segIndex].linedef].back == 0xFFFF;
}
```

The wall texture name for a one-sided seg is the sidedef `middle`. A null sector (bad
index) falls back to a default light level so the renderer never dereferences null.

## Unit B: BSP front-to-back traversal

Root is the last node (`numNodes-1`). The descent recurses, drawing the near child
before the far child so front-to-back order holds.

Partition-side test (frozen recipe): for partition origin `(x, y)` and delta
`(dx, dy)`, and camera `(cx, cy)`:

```cpp
inline int point_on_side(const NodeRaw& n, float cx, float cy) {
    float cross = (float)n.dx * (cy - n.y) - (float)n.dy * (cx - n.x);
    return cross < 0.0f ? 0 : 1;   // matches Doom R_PointOnSide: 0 = right/front, 1 = left/back
}
```

This is Doom's `R_PointOnSide` convention: `cross = right - left` where
`right = (cy-ny)*ndx` and `left = (cx-nx)*ndy`; Doom returns `0` (front/right) when
`cross < 0`, else `1` (back/left). The near child is `child[point_on_side(...)]`; the far
child is the other. Doom orders `child[0] = right`, `child[1] = left`, so
`point_on_side` returns the index of the side the camera is on, which is the near side.

Traversal is a pure enumerator, separate from drawing. `bsp_visit_order` fills an array
of subsector indices in front-to-back draw order; `render_view` then draws them in that
order. This makes Unit B testable directly (assert the order) without instrumenting the
draw path, and decouples traversal from the clip and column draw.

```cpp
// Fills out[0..count) with subsector indices in front-to-back order; returns count.
// depthGuard caps recursion against malformed or cyclic NODES (real WADs are acyclic;
// a malformed smuggled WAD must not stack-overflow the device).
inline int bsp_visit_order(const Map& m, const Camera& cam, int* out, int cap) {
    int count = 0;
    auto recurse = [&](auto&& self, uint16_t ref, int depth) -> void {
        if (count >= cap || depth > m.numNodes) return;        // overflow / cycle guard
        if (node_child_is_subsector(ref)) { out[count++] = node_child_index(ref); return; }
        if (ref >= m.numNodes) return;                          // bad index guard
        const NodeRaw& n = m.nodes[ref];
        int side = point_on_side(n, cam.x, cam.y);
        self(self, n.child[side],     depth + 1);               // near first
        self(self, n.child[side ^ 1], depth + 1);               // far
    };
    if (m.numNodes == 0) {                                       // node-less WAD fallback
        for (int32_t i = 0; i < m.numSsecs && count < cap; ++i) out[count++] = i;
    } else {
        recurse(recurse, (uint16_t)(m.numNodes - 1), 0);        // root index, high bit clear
    }
    return count;
}
```

Passing the root as a plain index works because `numNodes-1 < 0x8000` for any real WAD
(32767-node ceiling), so `node_child_is_subsector` reads false on it and
`node_child_index` masking is a no-op. The `depth > numNodes` guard bounds recursion: a
valid acyclic tree visits each node at most once, so depth never exceeds `numNodes`; a
cycle (such as the parse-only fake node in `build_test_wad`, whose `child[1]` points back
at node 0) is cut instead of looping. The cap on `out` bounds the subsector count.

`render_view` allocates `out` as a fixed array sized to the map's subsector count (from
the arena or a bounded instance buffer), calls `bsp_visit_order`, then draws each listed
subsector in order. Early-out on a fully-closed solidsegs list is optional and not
required for correctness.

## Unit C: solidsegs occlusion clip

A fixed-capacity sorted list of occluded, inclusive column ranges over screen columns
`[0, kScreenW-1]`. Front-to-back order plus this clip means each column is drawn once.

```cpp
static const int kMaxClip = 64;          // disjoint ranges; ample for 256 columns
struct SolidSegs {
    struct Range { int16_t first, last; };   // inclusive, occluded
    Range r[kMaxClip];
    int n;                                    // active ranges, sorted by first
};
inline void solidsegs_clear(SolidSegs& s) { s.n = 0; }
```

Clip a solid wall span `[x1, x2]` (already clamped to `[0, kScreenW-1]`, `x1 <= x2`):
walk the sorted ranges, emit each visible gap inside `[x1, x2]` via `drawSpan`, then
merge `[x1, x2]` into the list so later (farther) walls are clipped.

Algorithm (Doom `R_ClipSolidWallSegment`, adapted to inclusive ranges):

```text
cur = x1
for each range R in s.r (sorted ascending), while cur <= x2:
    if R.last < cur: continue                      # range entirely left of cursor
    if R.first > x2: break                          # ranges from here are right of span
    if R.first > cur: drawSpan(cur, R.first - 1)    # visible gap before this range
    cur = max(cur, R.last + 1)                       # jump past the occluded range
if cur <= x2: drawSpan(cur, x2)                      # trailing visible gap
insert_and_merge(s, x1, x2)                          # union [x1,x2] into the sorted list
```

`insert_and_merge` inserts `[x1, x2]` keeping the array sorted by `first` and coalescing
any overlapping or adjacent (`R.first <= prev.last + 1`) ranges, so the list stays
minimal. Overflow guard: if a non-coalescing insert would exceed `kMaxClip`, drop the
insert and continue (far walls in that rare case may overdraw; logged, never a crash).
64 ranges cannot be exceeded by 256 columns of disjoint walls in practice.

Two-sided segs (drawn as solid in P2) clip and insert identically to one-sided segs.

## Unit D: wall column projection and flat shading

Projection reuses the spike path. Per seg, transform both endpoints to camera space,
near-clip at depth `1.0`, compute screen-x from the lateral/depth ratio, order so
`sxA <= sxB`, and carry the endpoint depths for per-column interpolation.

```cpp
static const int kScreenW = 256, kScreenH = 64;
static const float kFovScale = (float)kScreenW / 2.0f;   // ~90 deg horizontal
static const float kWallScale = 32.0f;                    // wall-height constant
```

Per visible column `x` in a `drawSpan(visFirst, visLast)` callback:

```cpp
float t      = (sxB == sxA) ? 0.f : (float)(x - sxA) / (float)(sxB - sxA);
float depth  = depthA + (depthB - depthA) * t;
float wallH  = (kScreenH * kWallScale) / depth;
int   top    = (int)(kScreenH/2 - wallH/2);
int   bot    = (int)(kScreenH/2 + wallH/2);
clamp top to [0, kScreenH-1], bot to [0, kScreenH-1];
int   light  = light_row(sectorLight, depth, cm.numMaps);
for (int y = top; y <= bot; ++y) {
    uint8_t palIndex = flat ? kFlatWallIndex
                            : texture_sample(tex, texId, t, y, top, bot, seg);
    fb_put(fb, x, y, doom::shade_gray(pal, cm, palIndex, light));
}
```

Depth-based light row (closer is brighter; the synthetic colormap darkens with row):

```cpp
inline int light_row(int sectorLight, float depth, int numMaps) {
    if (sectorLight < 0) sectorLight = 0; if (sectorLight > 255) sectorLight = 255;
    int base = (255 - sectorLight) >> 3;            // 0 (bright) .. 31 (dark)
    int dadd = (int)(depth * kDepthLight);          // farther -> dimmer
    int row  = base + dadd;
    if (row < 0) row = 0; if (row >= numMaps) row = numMaps - 1;
    return row;
}
```

`kFlatWallIndex` is a fixed palette index giving a mid-gray base wall in flat mode.
`kDepthLight` is tuned so two columns at clearly different depths land on different
colormap rows (the occlusion and depth tests assert monotonic shading). Constraint:
`depth * kDepthLight` over the test's depth range must stay well under `cm.numMaps` (34)
so the near and far walls land on distinct, unclamped rows. With near depth `100` and far
depth `300`, a value near `kDepthLight = 0.03` keeps both rows in range and distinct
(near base `3 + 3 = 6`, far base `11 + 9 = 20`); the implementer fixes the exact value
against the synthetic colormap.

Flat-mode observable: a column at smaller depth has `shade_gray` output greater than or
equal to a column at larger depth (nearer is brighter or equal). The depth test asserts
this across a receding wall.

## Unit E: TEXTURE1 / PNAMES / patch composition

New header `texture.h`. Parses the Doom texture-assembly lumps and composes referenced
textures lazily into the arena as column-major palette-index buffers.

Lump formats (documented Doom format, reimplemented clean-room):

- `PNAMES`: `int32 count`, then `count` eight-byte uppercase patch names.
- `TEXTURE1`: `int32 numTextures`; `numTextures` `int32` offsets from the lump start;
  each `maptexture_t`: `name[8]`, `int32 masked`, `int16 width`, `int16 height`,
  `int32 columnDirectory` (obsolete), `int16 patchCount`, then `patchCount`
  `mappatch_t`: `int16 originX`, `int16 originY`, `int16 patch` (PNAMES index),
  `int16 stepDir` (unused), `int16 colormap` (unused).
- Patch lump (picture format): `int16 width`, `int16 height`, `int16 leftOffset`,
  `int16 topOffset`, then `width` `int32` column offsets from the patch start; each
  column is a series of posts: `byte topDelta`, `byte length`, `byte pad`, `length`
  pixel bytes, `byte pad`; a `topDelta` of `0xFF` ends the column.

Composition (`compose_texture`): allocate `width * height` bytes in the arena, fill with
`kTexGap` (a transparent/background sentinel, `0` here since P2 walls are opaque and
fully covered). For each patch in the texture, resolve its lump via PNAMES, and for each
patch column `pc` in `[0, patchWidth)`, write its posts at target column
`originX + pc` and target row `originY + post.topDelta + k` for each pixel `k`,
bounds-checked against `[0,width)` and `[0,height)`. Layout is column-major:
`texels[u * height + v]`.

```cpp
static const int kMaxTextures = 32;          // composed-texture cache slots
struct Texture { const uint8_t* texels; int16_t w, h; };
struct TextureCache {
    const Wad* wad; Arena* arena;
    const uint8_t* texture1; const uint8_t* pnames;   // lump pointers
    Texture entries[kMaxTextures]; bool composed[kMaxTextures]; int count;
    int  find(const char name8[8]) const;   // TEXTURE1 directory lookup, -1 if absent
    const Texture* get(int textureIndex);     // composes on first reference, caches
};
```

`kMaxTextures` bounds the cache; a real E1M1's visible wall-texture set fits well within
32. `get` returns `nullptr` when `textureIndex` is out of range or composition overflows
the arena, and `texture_sample` falls back to `kFlatWallIndex` on `nullptr`.

`texture_sample(tex, texId, t, y, top, bot, seg)`:

```cpp
const Texture* T = tex->get(texId);
if (!T) return kFlatWallIndex;                  // missing texture -> flat
int u = wall_u(seg, t, T->w);                    // affine horizontal coord, mod w
int v = ((y - top) * T->h) / (bot - top + 1);    // affine vertical coord
if (v < 0) v = 0; if (v >= T->h) v = T->h - 1;
return T->texels[u * T->h + v];
```

`wall_u` derives the texture column from the affine span fraction `t` scaled by the
wall's texel length, plus the sidedef `xoff` and the seg `offset`, taken modulo the
texture width. Affine (not perspective-correct) sampling is accepted for the 256x64
screen per issue #3.

Scope-down lever: if Unit E pushes `.text` toward the cap, stop at Unit D (flat-shaded),
pass `nullptr` for `tex`, and defer textures to a P2 follow-on with the measured `.text`
as the named blocker.

## Unit F: synthetic BSP test map

`build_bsp_test_wad()` builds a valid IWAD with two subsectors split by one real acyclic
node, posed so a near wall occludes a far wall in shared screen columns, plus
`TEXTURE1`, `PNAMES`, and one patch lump. All walls are one-sided.

Geometry (Doom integer coordinates; camera at the player start `(0, 0)` facing `+x`,
angle `0`). The projection is the spike formula `sx = kScreenW/2 + (lateral/depth) *
kFovScale` with `kScreenW/2 = 128`, `kFovScale = 128`:

- Near wall: vertical seg at `x = 100`, from `y = -40` to `y = 40`. Depth `100`. Screen
  half-extent `(40/100)*128 = 51.2`, so it projects to columns `[128-51, 128+51] =
  [76, 179]` (truncating toward center as the spike's `(int)` cast does).
- Far wall: vertical seg at `x = 300`, from `y = -200` to `y = 200`. Depth `300`. Screen
  half-extent `(200/300)*128 = 85.3`, so it projects to `[128-85, 128+85] = [42, 213]`.
- The far wall subtends a wider screen angle than the near wall (ratio `0.667 > 0.4`), so
  the far wall peeks out on both sides of the near wall. Front-to-back, the near wall
  (drawn first) occludes the far wall only in the shared central span `[76, 179]`; the
  far wall remains visible in `[42, 75]` and `[180, 213]`. This is the partial-occlusion
  case Unit C asserts against. (Equal screen angles would make the near wall fully
  occlude the far wall, giving no visible far sub-spans and no testable clip; the far
  wall is deliberately wider.)

Partition: a vertical line at `x = 200`, delta `(0, 100)` (points `+y`). The near wall
(`x = 100`) is on one side, the far wall (`x = 300`) on the other. The two subsectors:
`ssec0` = near wall segs, `ssec1` = far wall segs.

NODES (one acyclic record): `x = 200, y = 0, dx = 0, dy = 100`. With
`point_on_side((200,0),(0,100), cam=(0,0))`:
`cross = dx*(cy - y) - dy*(cx - x) = 0*(0-0) - 100*(0-200) = +20000`, which is not
`< 0`, so `point_on_side` returns `1` (left/back). The camera at `x = 0` is geometrically
left of the `x = 200` partition, so the near side is the left child. Set `child[1]` (left)
to the near subsector leaf (`ssec0`, the `x = 100` wall) and `child[0]` (right) to the far
subsector leaf (`ssec1`, the `x = 300` wall), so the near subsector renders first. Both
children are subsector leaves with the `0x8000` bit set (`child[0] = 0x8000 | 1`,
`child[1] = 0x8000 | 0`); no interior child, so the tree is acyclic and terminates.

`TEXTURE1`/`PNAMES`/patch: one texture `WALL` (for example 16x16) assembled from one
patch `PWALL` (a 16x16 picture with a recognizable pattern, for example a gradient or a
border) at origin `(0, 0)`. The sidedef `middle` of every wall is `WALL`. This exercises
the directory lookup, the PNAMES resolve, and one patch composition.

Sectors carry distinct light levels (for example near sector light `224`, far sector
light `160`) so flat-shaded output differs between subsectors and the test can attribute
columns. Near is brighter (lower `light_row`), so an overlap column carrying the near
shade proves the far wall did not overwrite it.

PLAYPAL and COLORMAP are included, identical to `build_test_wad()`: one 256-entry gray
ramp palette (entry `i = (i,i,i)`, so `pal.gray[i] = i >> 4`) and 34 colormaps with
`map[m][i] = i*(33-m)/33` (map 0 brightest, map 33 darkest). Without these,
`palette_load`/`colormap_load` fail and `shade_gray` has no maps, so every shading and
occlusion assertion is dead. The renderer depends on them, so the builder must emit them.

`build_test_wad()` is unchanged; `build_bsp_test_wad()` is additive. `wad_build.cpp`
emits both `kDoomTestWad` and `kDoomBspTestWad` into `doom_test_map.h`.

## Unit G: device plug-in integration

`doom_core_spike.cpp` switches its embedded source to `kDoomBspTestWad` and calls the
production `render_view` with a loaded palette and colormap. The camera stays a fixed
pose inside the map. On the device, the smuggled-WAD read door (P1) supplies a real
E1M1: the load path is the documented `NT_readSampleFrames` adapter feeding the arena,
then `wad_open` and `map_load`, then `render_view`. Host pixel tests feed the synthetic
WAD bytes directly and do not exercise the door.

Two-sided segs on a real E1M1: P2 draws them as solid walls (the wall-type decision).
Because most two-sided lines carry an empty `middle` texture, the device render
flat-shades two-sided segs as solid surfaces (using the sector light, not a texture) so
the geometry reads as recognizable walls rather than rendering nothing. Proper portal
rendering (upper and lower textures, vertical clip, see-through openings) is the P5
follow-on. This affects only the device E1M1 smoke; host tests use one-sided walls only.

Verify ADD on hardware, not just registration: the renderer plug-in is added to a preset
and draws. The DRAM request (WAD plus composited textures plus framebuffer) must be sized
in `calculateRequirements`; enlarging it needs a device reboot, not just the rescan, or
the firmware allocates the old size and faults.

## Production render_view signature

```cpp
namespace doom {
struct Camera { float x, y, angle; };
void render_view(const Map& m, const Camera& cam,
                 const Palette& pal, const Colormap& cm,
                 const TextureCache* tex,   // nullptr => flat-shaded sectors
                 uint8_t* fb);
}
```

This replaces the spike `render_view(map, cam, fb)`. `doom_core_spike.cpp` and
`harness/tests/test_doom_render.cpp` adopt it. `render.h` forward-declares
`struct TextureCache;` ahead of the signature so Unit D builds before `texture.h` lands.
`test_doom_render.cpp` is rewritten to drive `build_bsp_test_wad()` (acyclic), loading
the palette and colormap and passing `nullptr` textures for the flat-shaded assertions;
the old square-room case is removed because `build_test_wad()`'s cyclic fake NODES cannot
be traversed by the BSP renderer.

## Build order and tests

1. Unit F: `build_bsp_test_wad()` plus a host test asserting the map parses, has two
   subsectors, one acyclic node, and the texture lumps. Regenerate `doom_test_map.h`.
2. Unit A: `geom.h` helpers plus host tests (seg to sector, one-sided detection).
3. Unit B: `bsp_visit_order` (pure enumerator) plus host tests asserting subsector visit
   order (`[0, 1]`, near before far) for the synthetic pose, the `numNodes==0` fallback,
   and the depth-guard cut on the cyclic `build_test_wad` node.
4. Unit C: solidsegs plus host tests (clip a gap, full occlusion, adjacency merge,
   overflow guard).
5. Unit D: flat-shaded `render_view` plus host pixel tests (walls in expected columns,
   occlusion holds, depth shading monotonic). Flat-shaded milestone. Measure `.text`.
6. Unit E: `texture.h` plus host tests (PNAMES/TEXTURE1 parse, patch composition offset,
   textured column sampling). Measure `.text`; apply the scope-down lever if needed.
7. Unit G: `doom_core_spike` integration; `make arm`, symbol check, `.text` check.

Each commit leaves `make test` green. `.text` measured after Units D, E, G via
`arm-none-eabi-readelf -W -S build/arm/doom_core_spike.o`.

## Test plan (host pixel assertions)

- Traversal order: call `bsp_visit_order` directly (pure function, no draw-path seam);
  assert it returns `[0, 1]` (near, far) for the synthetic camera, `[0..numSsecs)` for a
  `numNodes==0` map, and a guard-bounded result (no hang) on the cyclic
  `build_test_wad` node.
- Occlusion holds: render the synthetic map; in a column where near and far walls
  overlap, assert the drawn pixels match the near wall's depth/shade, and the far wall
  did not overwrite them. Concretely, the near subsector has a brighter sector light, so
  overlap columns must carry the brighter (near) shade.
- Walls in expected columns: assert lit columns fall within the projected screen-x span
  of each wall and unlit outside.
- Depth shading monotonic: across a single receding wall, a nearer column's gray is
  greater than or equal to a farther column's gray.
- Textured sampling (Unit E): a known patch pattern composes so that a sampled column
  reproduces the patch's pixel at the expected `(u, v)`.

## Verification footer

### Recipe spot-check

- BSP traversal (`bsp_visit_order`) descends from the root (`numNodes-1`), near child
  first via `point_on_side`, dispatching `0x8000`-tagged children as subsector leaves,
  with a depth guard against cyclic or malformed NODES. Matches the frozen recipe and
  `geom.h` helpers.
- solidsegs is a sorted fixed-capacity inclusive-range list; clip emits visible gaps and
  merges the solid span. Matches the frozen recipe.
- Projection reuses the spike camera transform, near clip at `1.0`, screen-x from
  lateral/depth, wall height `kScreenH*kWallScale/depth`. Matches `render.h`.
- Texture composition parses `PNAMES`/`TEXTURE1`/patch posts into a column-major arena
  buffer, composed lazily. Matches the Doom format and the no-heap constraint.

### Per-entry verification (three end-to-end traces)

Trace 1, partition-side test. Node `x=200, y=0, dx=0, dy=100`; camera `(0,0)`.
`cross = dx*(cy-y) - dy*(cx-x) = 0*(0-0) - 100*(0-200) = +20000`. `cross < 0` is false,
so `point_on_side` returns `1` (Doom's back/left side). The camera at `x=0` is
geometrically left of the `x=200` partition, which agrees. Near child is `child[1]`; set
`child[1] = 0x8000|0` (near subsector `ssec0`, the `x=100` wall) and `child[0] = 0x8000|1`
(far subsector `ssec1`, the `x=300` wall). Front-to-back order confirmed: the near wall
is drawn before the far wall and inserts its solid span first. (Self-review caught an
inverted return convention in an earlier draft here; the value above is the corrected
Doom `R_PointOnSide` result.) Correct.

Trace 2, solidsegs clip case (numbers from the Unit F geometry through the spike
projection). Near wall projects to columns `[76, 179]` at depth `100`; far wall projects
to `[42, 213]` at depth `300`; the spans overlap in `[76, 179]`. Order: near first.
List empty, near clips with no occluders: `drawSpan(76, 179)`, then merge so
`r = [{76,179}]`. Far second: `cur = 42`; range `{76,179}` has `last=179 >= cur` and
`first=76 > cur=42`, so `drawSpan(42, 75)`; `cur = max(42, 180) = 180`; no further range;
`180 <= 213`, so `drawSpan(180, 213)`. The far wall draws only `[42,75]` and `[180,213]`,
never `[76,179]`. The near wall owns the overlap; occlusion holds. Correct.

Trace 3, patch-column composition offset. Patch `PWALL` 16x16 at origin `(0,0)`; its
column `5` has one post with `topDelta = 3`, `length = 4`, pixels `p0..p3`. Composition
writes target column `originX + 5 = 5`, rows `originY + topDelta + k = 3 + k` for
`k in [0,3]`, so rows `3,4,5,6`. Column-major target index `u*height + v = 5*16 + v`.
Pixel `p0` lands at `texels[5*16 + 3] = texels[83]`, `p3` at `texels[5*16 + 6] =
texels[86]`. Sampling column `u=5`, `v=3` returns `p0`. Offsets match the Doom
column-post format. Correct.

If more than one of these three traces had contradicted the format or the helpers, every
entry would be audited before implementation. All three hold.

### Prerequisite verification (P1 symbols and fields the renderer depends on)

- `doom::shade_gray(const Palette&, const Colormap&, uint8_t, int)` exists in
  `palette.h:41`. Present.
- `doom::Palette`, `doom::Colormap`, `palette_load`, `colormap_load` in `palette.h`.
  Present.
- `doom::NodeRaw` (28-byte), `node_child_is_subsector`, `node_child_index` in
  `geom.h:14,23,24`. Present.
- `doom::Map` fields `verts, segs, ssecs, sectors, lines, sides, nodes` and their counts
  in `geom.h:26`. Present.
- `SegRaw{v1,v2,angle,linedef,side,offset}`, `LinedefRaw{...,front,back}`,
  `SidedefRaw{xoff,yoff,upper,lower,middle,sector}`, `SubsecRaw{numSegs,firstSeg}`,
  `SectorRaw{...,light,...}` in `geom.h:9-13`. Present.
- `doom::Arena`, `arena_alloc`, `arena_reset` in `arena.h`. Present.
- `wad_find_lump`, `wad_lump_ptr`, `wad_lump_size`, `wad_open` in `wad.h`. Present.
- `fb_put`, `fb_get`, `fb_clear` in `fb.h`. Present.
- `cos_sin` per-TU seam declared in `render.h:10`, host def in `test_doom_render.cpp`,
  ARM def in `doom_core_spike.cpp`. Present.

All prerequisites exist. No P1 change is required beyond the additive `geom.h` helpers
(Unit A) and the additive synthetic builder (Unit F).

## Hard constraints (carried)

- Clean-room; no GPL Doom or PureDOOM source. Formats reimplemented from documentation.
- Never commit a real WAD; tests use the synthetic builders.
- No heap; `float` math allowed; no libm `sinf` (rodata LUT via `cos_sin`).
- `.text` under ~82 KB; DRAM under 12 MB.
- TDD for all P2 logic, render-to-buffer pixel assertions.
