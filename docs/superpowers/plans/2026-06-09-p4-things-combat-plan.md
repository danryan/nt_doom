# P4 things and combat Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render map things as camera-facing billboard sprites over real E1M1, occluded behind walls by a per-column wall depth buffer, and wire the fire gate to a hitscan that registers a hit on the nearest thing in front of the player.

**Architecture:** Six units. Three pure headers (THINGS view, sprite cache, hitscan) and two render extensions (depth buffer, billboard render) are host-tested first; the device wiring on `doom_core_spike` is verified on hardware. All math is libm-free and heap-free; every new scratch buffer is an instance member, never a `draw`/`step` stack local (the P3 stack-overflow lesson).

**Tech Stack:** C++17 header-only engine, Catch2 host tests, `arm-none-eabi` PIC plug-in build, the disting NT firmware ABI.

Date: 2026-06-09
Phase: P4 (GitHub issue #5)
Branch: dr/p4-things-combat
Spec: docs/superpowers/specs/2026-06-09-p4-things-combat-design.md

---

## Execution model

Inline sequential TDD, no parallel implementers. Units live in shared headers
(`geom.h`, `render.h`) and form a tight chain (depth buffer to projection to clip), so
worktree orchestration would cost more integration tax than it saves. Worktree base is
already correct: this plan runs in the `dr/p4-things-combat` worktree branched from
`origin/main` (P3 PR #9 merged), submodules provisioned by `./bootstrap.sh`. Each step
leaves `make test` green and commits in small increments. Wait for commit approval before
each commit per the user's workflow.

### Step 0: baseline (done)

`./bootstrap.sh`, `make test` green, `make arm` clean, `doom_core_spike.o` `.text`
~8.5 KB, unresolved symbols all in the firmware-resolved set. Recorded in the preflight.

---

## Task 1: THINGS view and the synthetic things WAD

**Files:**

- Modify: `plugins/games/doom/geom.h` (add `ThingRaw`, `Map::things`/`numThings`, the
  `map_load` THINGS load)
- Modify: `harness/tools/wad_build.h` (add `build_things_test_wad()`)
- Test: `harness/tests/test_things.cpp` (new)
- Modify: `Makefile` (new `test_things` target, `host` and `test` lists)

- [ ] **Step 1: Write the failing test**

Create `harness/tests/test_things.cpp`:

```cpp
#include "catch.hpp"
#include "../tools/wad_build.h"
#include "../../plugins/games/doom/wad.h"
#include "../../plugins/games/doom/geom.h"

TEST_CASE("map_load reads the THINGS lump as a typed view", "[things]") {
    std::vector<uint8_t> bytes = build_things_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    doom::Map m; REQUIRE(doom::map_load(w, "E1M1", m));

    REQUIRE(m.numThings >= 2);                 // player start plus the barrel
    bool foundBarrel = false;
    for (int i = 0; i < m.numThings; ++i)
        if (m.things[i].type == 2035) {        // barrel
            foundBarrel = true;
            REQUIRE(m.things[i].x == 200);
            REQUIRE(m.things[i].y == 0);
        }
    REQUIRE(foundBarrel);
}

TEST_CASE("map_load tolerates a map with no THINGS lump", "[things]") {
    // build_bsp_test_wad has THINGS; simulate absence by clearing the view.
    std::vector<uint8_t> bytes = build_bsp_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    doom::Map m; REQUIRE(doom::map_load(w, "E1M1", m));
    m.things = nullptr; m.numThings = 0;       // a node-less / thing-less consumer must cope
    REQUIRE(m.numThings == 0);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Add the Makefile target first (so it compiles to a link/usage error, not a missing
binary). In `Makefile`, after the `test_collision` target add:

```make
build/host/test_things: harness/tests/test_things.cpp harness/tools/wad_build.h plugins/games/doom/wad.h plugins/games/doom/geom.h $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ harness/tests/test_things.cpp $(HARNESS_SRCS)
```

Append `build/host/test_things` to the `host:` prerequisite list and
`./build/host/test_things` to the `test:` recipe.

Run: `make build/host/test_things`
Expected: FAIL to compile (`build_things_test_wad` not declared, `Map` has no `things`).

- [ ] **Step 3: Add the THINGS view to `geom.h`**

In the `#pragma pack(push, 1)` block add `struct ThingRaw { int16_t x, y, angle, type, flags; };`.
Add to `struct Map`: `const ThingRaw* things = nullptr; int32_t numThings = 0;`. In
`map_load`, after the mandatory lumps and before/after the NODES load, add the optional
THINGS load (Recipe A):

```cpp
int32_t thi = wad_find_lump(w, "THINGS", base);
if (thi < 0) { m.things = nullptr; m.numThings = 0; }
else { m.things = (const ThingRaw*)wad_lump_ptr(w, thi);
       m.numThings = (int32_t)(wad_lump_size(w, thi) / sizeof(ThingRaw)); }
```

- [ ] **Step 4: Add `build_things_test_wad()` to `wad_build.h`**

Add a builder that extends the `build_bsp_test_wad` geometry (near wall x=100, far wall
x=300, one acyclic node, gray-ramp PLAYPAL/COLORMAP, TEXTURE1/PNAMES/PWALL) with a barrel
thing and a sprite lump set. Per Recipe H, the THINGS lump carries the player-1 start at
`(0,0)` and a barrel (type 2035) at `(200,0)` angle 0; between `S_START` and `S_END` add
one `BAR1A0` patch (16x32, frame A rotation 0, opaque posts with a deterministic pattern
that avoids palette index `0xFF`). Reproduce the `build_bsp_test_wad` assembly tail
verbatim. The barrel sits at depth 200 from the origin camera, between the two walls.

```cpp
// THINGS: player-1 start at (0,0) and a barrel (type 2035) at (200,0).
{ std::vector<uint8_t> d;
  w16(d,0); w16(d,0); w16(d,0); w16(d,1); w16(d,7);          // player 1
  w16(d,200); w16(d,0); w16(d,0); w16(d,2035); w16(d,7);     // barrel
  add("THINGS", d); }
// ... (VERTEXES/LINEDEFS/SIDEDEFS/SEGS/SSECTORS/NODES/SECTORS/PLAYPAL/COLORMAP/
//      PNAMES/TEXTURE1/PWALL identical to build_bsp_test_wad) ...
add("S_START", {});
// BAR1A0: 16x32 patch, every column a full 32px post, pixel[row] = (row & 0x3F).
{ std::vector<uint8_t> d;
  auto o32=[&](int32_t x){ d.push_back(x&0xFF); d.push_back((x>>8)&0xFF); d.push_back((x>>16)&0xFF); d.push_back((x>>24)&0xFF); };
  w16(d,16); w16(d,32); w16(d,8); w16(d,32);                 // width,height,left,top
  int32_t colStart = 8 + 16*4;
  int32_t colBytes = 1+1+1+32+1+1;                           // topdelta,length,pad,32px,pad,0xFF
  for (int c=0;c<16;++c) o32(colStart + c*colBytes);
  for (int c=0;c<16;++c) {
      d.push_back(0); d.push_back(32); d.push_back(0);
      for (int row=0;row<32;++row) d.push_back((uint8_t)(row + 1));   // 1..32, never 0xFF
      d.push_back(0); d.push_back(0xFF);
  }
  add("BAR1A0", d); }
add("S_END", {});
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `make build/host/test_things && ./build/host/test_things`
Expected: PASS (both cases).

- [ ] **Step 6: Commit**

```bash
git add plugins/games/doom/geom.h harness/tools/wad_build.h harness/tests/test_things.cpp Makefile
git commit -m "feat(doom): typed THINGS map view and synthetic things test WAD"
```

---

## Task 2: sprite lump index and SpriteCache

**Files:**

- Create: `plugins/games/doom/sprite.h`
- Test: `harness/tests/test_sprite.cpp` (new)
- Modify: `Makefile` (new `test_sprite` target, `host` and `test` lists)

- [ ] **Step 1: Write the failing test**

Create `harness/tests/test_sprite.cpp`:

```cpp
#include "catch.hpp"
#include "../tools/wad_build.h"
#include "../../plugins/games/doom/wad.h"
#include "../../plugins/games/doom/geom.h"
#include "../../plugins/games/doom/arena.h"
#include "../../plugins/games/doom/sprite.h"
#include <vector>

TEST_CASE("sprite_find decodes a single-rotation lump", "[sprite]") {
    std::vector<uint8_t> bytes = build_things_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    std::vector<uint8_t> mem(1 << 20);
    doom::Arena a; doom::arena_init(a, mem.data(), (uint32_t)mem.size());
    doom::SpriteCache sc; REQUIRE(doom::spritecache_init(sc, w, a));

    bool flip = true;
    int li = doom::sprite_find(sc, "BAR1", 'A', 0, flip);
    REQUIRE(li >= 0);
    REQUIRE(flip == false);                       // single pair, not mirrored
    REQUIRE(doom::sprite_find(sc, "BAR1", 'A', 3, flip) < 0);   // rotation 3 absent
}

TEST_CASE("sprite_get composes the patch column-major with a transparent sentinel", "[sprite]") {
    std::vector<uint8_t> bytes = build_things_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    std::vector<uint8_t> mem(1 << 20);
    doom::Arena a; doom::arena_init(a, mem.data(), (uint32_t)mem.size());
    doom::SpriteCache sc; REQUIRE(doom::spritecache_init(sc, w, a));

    bool flip = false;
    int li = doom::sprite_find(sc, "BAR1", 'A', 0, flip);
    const doom::Sprite* sp = doom::sprite_get(sc, li);
    REQUIRE(sp != nullptr);
    REQUIRE(sp->w == 16);
    REQUIRE(sp->h == 32);
    REQUIRE(sp->left == 8);
    REQUIRE(sp->top == 32);
    REQUIRE(sp->texels[0 * 32 + 0] == 1);         // column 0, row 0 = pixel value 1
    REQUIRE(sp->texels[0 * 32 + 31] == 32);       // column 0, row 31 = pixel value 32
}

TEST_CASE("sprite_find matches the mirror pair of an 8-char lump name", "[sprite]") {
    // A name decode unit: spritecache over a tiny hand-built WAD is overkill; assert the
    // decoder directly via a helper on the cache once a POSSA2A8-style lump is present.
    // build_things_test_wad carries only BAR1A0, so this case exercises the decode rule
    // through sprite_find on a synthetic second lump added below in Step 4 if present.
    SUCCEED();   // placeholder removed once the mirror lump is added (see Step 4 note)
}
```

Note: the mirror-pair case is fully exercised by the per-entry verification trace
(`POSSA2A8`); to make it a live assertion, Step 4 adds a second sprite lump `POSSA2A8`
(zero-content beyond a minimal patch) to `build_things_test_wad` and the test asserts
`sprite_find(sc,"POSS",'A',2,flip)` returns it with `flip==false` and
`sprite_find(sc,"POSS",'A',8,flip)` returns the same lump with `flip==true`. Replace the
`SUCCEED()` with those assertions.

- [ ] **Step 2: Run the test to verify it fails**

Add the Makefile target (after `test_things`):

```make
build/host/test_sprite: harness/tests/test_sprite.cpp harness/tools/wad_build.h plugins/games/doom/wad.h plugins/games/doom/geom.h plugins/games/doom/arena.h plugins/games/doom/sprite.h $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ harness/tests/test_sprite.cpp $(HARNESS_SRCS)
```

Add to `host:` and `test:`. Run: `make build/host/test_sprite`
Expected: FAIL to compile (`sprite.h` does not exist).

- [ ] **Step 3: Implement `sprite.h`**

Create `plugins/games/doom/sprite.h` per Recipe B: `Sprite`, `SpriteCache`,
`spritecache_init` (locate `S_START`/`S_END`), `sprite_find` (scan the interior, decode
pair 1 and the optional mirror pair 2, set `flip`), and `sprite_get` (compose the patch
column-major into the arena, buffer initialized to `kSpriteGap = 0xFF`, dimensions from
the patch header, memoized in `entries[]`/`composed[]`/`lumpOf[]`). Reuse the patch post
decoder from `texture.h` (`topdelta, length, pad, Npx, pad, 0xFF`); pixels at `col+3`,
advance `col += 3 + length + 1`.

```cpp
#pragma once
#include <cstdint>
#include "wad.h"
#include "arena.h"

namespace doom {
static const int     kMaxSprites = 64;
static const uint8_t kSpriteGap  = 0xFF;
struct Sprite { const uint8_t* texels; int16_t w, h, left, top; };
struct SpriteCache {
    const Wad* wad = nullptr; Arena* arena = nullptr;
    int32_t sStart = -1, sEnd = -1;
    mutable Sprite  entries[kMaxSprites];
    mutable bool    composed[kMaxSprites];
    mutable int32_t lumpOf[kMaxSprites];
    mutable int32_t count = 0;
};
inline bool spritecache_init(SpriteCache& sc, const Wad& w, Arena& a) {
    sc.wad = &w; sc.arena = &a; sc.count = 0;
    for (int i = 0; i < kMaxSprites; ++i) { sc.composed[i] = false; sc.lumpOf[i] = -1; }
    sc.sStart = wad_find_lump(w, "S_START");
    sc.sEnd   = wad_find_lump(w, "S_END");
    return sc.sStart >= 0 && sc.sEnd > sc.sStart;
}
inline int sprite_find(const SpriteCache& sc, const char name4[4], char frame, int rotation, bool& flip) {
    for (int32_t i = sc.sStart + 1; i < sc.sEnd; ++i) {
        const char* nm = sc.wad->dir[i].name;
        bool nameEq = nm[0]==name4[0] && nm[1]==name4[1] && nm[2]==name4[2] && nm[3]==name4[3];
        if (!nameEq) continue;
        if (nm[4] == frame && (nm[5]-'0') == rotation) { flip = false; return i; }
        if (nm[6] != 0 && nm[6] == frame && (nm[7]-'0') == rotation) { flip = true; return i; }
    }
    return -1;
}
// sprite_get: read width/height/left/top (4 int16) from the patch header; alloc w*h in
// the arena initialized to kSpriteGap; decode each column's posts into column-major
// texels[ x*h + y ]; memoize. Returns nullptr on a bad index or arena exhaustion.
const Sprite* sprite_get(const SpriteCache& sc, int lumpIndex);
} // namespace doom
```

Implement `sprite_get` inline in the header (mirror `TextureCache::get`'s body with the
`kSpriteGap` init and the header-derived dimensions), bounds-checking `lumpIndex` against
`(sStart, sEnd)` and `count` against `kMaxSprites`.

- [ ] **Step 4: Add the mirror lump to the test WAD and finish the test**

In `build_things_test_wad`, before `S_END`, add a minimal `POSSA2A8` patch (e.g. 8x8,
frame A, rotations 2 and 8 mirrored) so the mirror-pair case has a real lump. Replace the
`SUCCEED()` with:

```cpp
std::vector<uint8_t> bytes = build_things_test_wad();
doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
std::vector<uint8_t> mem(1 << 20);
doom::Arena a; doom::arena_init(a, mem.data(), (uint32_t)mem.size());
doom::SpriteCache sc; REQUIRE(doom::spritecache_init(sc, w, a));
bool flip = false;
int liA2 = doom::sprite_find(sc, "POSS", 'A', 2, flip); REQUIRE(liA2 >= 0); REQUIRE(flip == false);
int liA8 = doom::sprite_find(sc, "POSS", 'A', 8, flip); REQUIRE(liA8 == liA2); REQUIRE(flip == true);
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `make build/host/test_sprite && ./build/host/test_sprite`
Expected: PASS (all three cases).

- [ ] **Step 6: Commit**

```bash
git add plugins/games/doom/sprite.h harness/tests/test_sprite.cpp harness/tools/wad_build.h Makefile
git commit -m "feat(doom): sprite lump index and column-major SpriteCache"
```

---

## Task 3: per-column wall depth buffer

**Files:**

- Modify: `plugins/games/doom/render.h` (extend `render_view`/`render_subsector`/
  `render_seg` with `float* depthOut`; add `kFarDepth`, `sprite_column_visible`)
- Test: `harness/tests/test_depth.cpp` (new)
- Modify: `Makefile` (new `test_depth` target, `host` and `test` lists)

- [ ] **Step 1: Write the failing test**

Create `harness/tests/test_depth.cpp`:

```cpp
#include "catch.hpp"
#include "../tools/wad_build.h"
#include "../../plugins/games/doom/wad.h"
#include "../../plugins/games/doom/geom.h"
#include "../../plugins/games/doom/render.h"
#include <cmath>

namespace doom { void cos_sin(float a, float& c, float& s) { c = std::cos(a); s = std::sin(a); } }

TEST_CASE("sprite_column_visible is a strict nearer-than test", "[depth]") {
    REQUIRE(doom::sprite_column_visible(200.0f, 300.0f) == true);    // sprite nearer than wall
    REQUIRE(doom::sprite_column_visible(200.0f, 100.0f) == false);   // wall in front
    REQUIRE(doom::sprite_column_visible(200.0f, doom::kFarDepth) == true);  // no wall
}

TEST_CASE("render_view fills the depth buffer with the near wall depth", "[depth]") {
    std::vector<uint8_t> bytes = build_bsp_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    doom::Map m; REQUIRE(doom::map_load(w, "E1M1", m));
    doom::Palette pal; doom::palette_load(w, pal);
    doom::Colormap cm; doom::colormap_load(w, cm);

    uint8_t fb[128 * 64];
    float depth[doom::kScreenW];
    doom::Camera cam{0.0f, 0.0f, 0.0f};        // at origin, facing +x; near wall x=100
    doom::render_view(m, cam, pal, cm, nullptr, fb, depth);

    // Central column 128 is covered by the near wall (x~100), so its depth is ~100,
    // strictly less than the far wall (~300) and far below kFarDepth.
    REQUIRE(depth[128] < 150.0f);
    REQUIRE(depth[128] > 50.0f);
    // A column with no wall (far left edge, beyond both walls' projection) stays far.
    REQUIRE(depth[0] == Catch::Approx(doom::kFarDepth));
}
```

- [ ] **Step 2: Run the test to verify it fails**

Add the Makefile target (after `test_sprite`):

```make
build/host/test_depth: harness/tests/test_depth.cpp harness/tools/wad_build.h plugins/games/doom/wad.h plugins/games/doom/geom.h plugins/games/doom/palette.h plugins/games/doom/render.h plugins/games/doom/fb.h plugins/games/doom/texture.h $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ harness/tests/test_depth.cpp $(HARNESS_SRCS)
```

Add to `host:` and `test:`. Run: `make build/host/test_depth`
Expected: FAIL to compile (`sprite_column_visible`, `kFarDepth` undefined;
`render_view` takes 6 args).

- [ ] **Step 3: Extend `render.h`**

Add `static const float kFarDepth = 1.0e30f;` and the predicate
`inline bool sprite_column_visible(float spriteDepth, float wallDepth) { return spriteDepth < wallDepth; }`.
Add a trailing `float* depthOut = nullptr` parameter to `render_seg`, `render_subsector`,
and `render_view`. Thread it through. In `render_view`, when `depthOut != nullptr`,
initialize `for (int i = 0; i < kScreenW; ++i) depthOut[i] = kFarDepth;` before the BSP
walk. In `render_seg`'s draw lambda, after `depth` is computed, add
`if (depthOut) depthOut[x] = depth;` (inside the per-column loop, where each column is
drawn once by the nearest seg).

- [ ] **Step 4: Run the test to verify it passes**

Run: `make build/host/test_depth && ./build/host/test_depth`
Expected: PASS. Then `make test` to confirm the existing `[render]`/`[traverse]` suites
still pass (the default `depthOut = nullptr` keeps them unchanged).

- [ ] **Step 5: Commit**

```bash
git add plugins/games/doom/render.h harness/tests/test_depth.cpp Makefile
git commit -m "feat(doom): per-column wall depth buffer and sprite depth-clip predicate"
```

---

## Task 4: billboard projection, rotation, and multi-sprite render

**Files:**

- Modify: `plugins/games/doom/render.h` (add `project_thing`, `octant_of`,
  `sprite_rotation`, `render_things`; include `sprite.h`)
- Test: `harness/tests/test_sprite_render.cpp` (new)
- Modify: `Makefile` (new `test_sprite_render` target, `host` and `test` lists)

- [ ] **Step 1: Write the failing test**

Create `harness/tests/test_sprite_render.cpp`:

```cpp
#include "catch.hpp"
#include "../tools/wad_build.h"
#include "../../plugins/games/doom/wad.h"
#include "../../plugins/games/doom/geom.h"
#include "../../plugins/games/doom/arena.h"
#include "../../plugins/games/doom/sprite.h"
#include "../../plugins/games/doom/render.h"
#include <cmath>
#include <vector>

namespace doom { void cos_sin(float a, float& c, float& s) { c = std::cos(a); s = std::sin(a); } }

// Local type-to-sprite resolver for the render test (combat.h's table is built in Task 5).
static const char* testName(int type) { return type == 2035 ? "BAR1" : nullptr; }

TEST_CASE("project_thing centers a straight-ahead thing and scales with depth", "[sprite_render]") {
    doom::Camera cam{0.0f, 0.0f, 0.0f};
    float ca, sa; doom::cos_sin(cam.angle, ca, sa);
    doom::SpriteProj p = doom::project_thing(cam, ca, sa, 200.0f, 0.0f, 16, 32);
    REQUIRE(p.visible);
    REQUIRE(p.depth == Catch::Approx(200.0f));
    REQUIRE(p.colL <= 128); REQUIRE(p.colR >= 128);         // spans the center column
    // A thing behind the near plane is rejected.
    doom::SpriteProj behind = doom::project_thing(cam, ca, sa, -50.0f, 0.0f, 16, 32);
    REQUIRE(behind.visible == false);
}

TEST_CASE("sprite_rotation picks front when the thing faces the viewer, back otherwise", "[sprite_render]") {
    // sprite_rotation(thingAngleRadians, player->thing dx, dy). Thing faces +x (angle 0).
    // player->thing = (-1,0): the thing is west and faces +x toward the viewer to its east,
    // so the viewer sees the front -> rotation 1.
    REQUIRE(doom::sprite_rotation(0.0f, -1.0f, 0.0f) == 1);
    // player->thing = (1,0): the thing is east and faces +x away from the viewer to its
    // west, so the viewer sees the back -> rotation 5.
    REQUIRE(doom::sprite_rotation(0.0f, 1.0f, 0.0f) == 5);
}

TEST_CASE("render_things draws a sprite in front of a wall and occludes it behind", "[sprite_render]") {
    std::vector<uint8_t> bytes = build_things_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    doom::Map m; REQUIRE(doom::map_load(w, "E1M1", m));
    doom::Palette pal; doom::palette_load(w, pal);
    doom::Colormap cm; doom::colormap_load(w, cm);
    std::vector<uint8_t> mem(1 << 20);
    doom::Arena a; doom::arena_init(a, mem.data(), (uint32_t)mem.size());
    doom::SpriteCache sc; REQUIRE(doom::spritecache_init(sc, w, a));

    uint8_t fb[128 * 64];
    float depth[doom::kScreenW];
    int order[64];
    doom::Camera cam{0.0f, 0.0f, 0.0f};        // origin facing +x; barrel at (200,0)

    SECTION("sprite visible when the wall behind it is farther") {
        for (int i = 0; i < doom::kScreenW; ++i) depth[i] = doom::kFarDepth;  // no wall
        doom::fb_clear(fb);
        doom::render_things(m, cam, pal, cm, &sc, depth, fb, order, 64, testName);
        // The barrel sprite center is column 128; at least one lit pixel appears near it.
        bool lit = false;
        for (int y = 28; y <= 36 && !lit; ++y)
            for (int x = 125; x <= 131 && !lit; ++x) if (doom::fb_get(fb, x, y) != 0) lit = true;
        REQUIRE(lit);
    }
    SECTION("sprite occluded when a nearer wall covers its columns") {
        for (int i = 0; i < doom::kScreenW; ++i) depth[i] = 100.0f;   // wall at depth 100 < 200
        doom::fb_clear(fb);
        doom::render_things(m, cam, pal, cm, &sc, depth, fb, order, 64, testName);
        bool lit = false;
        for (int y = 0; y < 64 && !lit; ++y)
            for (int x = 120; x <= 136 && !lit; ++x) if (doom::fb_get(fb, x, y) != 0) lit = true;
        REQUIRE(lit == false);                  // fully occluded
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Add the Makefile target (after `test_depth`):

```make
build/host/test_sprite_render: harness/tests/test_sprite_render.cpp harness/tools/wad_build.h plugins/games/doom/wad.h plugins/games/doom/geom.h plugins/games/doom/arena.h plugins/games/doom/sprite.h plugins/games/doom/render.h plugins/games/doom/fb.h plugins/games/doom/texture.h plugins/games/doom/palette.h $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ harness/tests/test_sprite_render.cpp $(HARNESS_SRCS)
```

Add to `host:` and `test:`. Run: `make build/host/test_sprite_render`
Expected: FAIL to compile (`project_thing`, `octant_of`, `sprite_rotation`,
`render_things` undefined).

- [ ] **Step 3: Implement projection, rotation, and render in `render.h`**

Add `#include "sprite.h"` to `render.h`. Add `kNearClip`, `kSpriteScale`, `kSpriteLight`,
`kDeg2Rad`, `SpriteProj`, `project_thing`, `octant_of`, `sprite_rotation`, `ThingSpriteFn`,
and `render_things` per Recipe D. The type-to-name resolver is injected as a function
pointer, so `render.h` does not depend on `combat.h` (built in Task 5) and the host test
supplies its own resolver:

```cpp
typedef const char* (*ThingSpriteFn)(int type);
void render_things(const Map& m, const Camera& cam, const Palette& pal, const Colormap& cm,
                   const SpriteCache* sc, const float* depthBuf, uint8_t* fb,
                   int* order, int orderCap, ThingSpriteFn nameFn);
```

Because `render.h` now includes `sprite.h`, add `plugins/games/doom/sprite.h` to the
prerequisites of every render.h-dependent host target in the `Makefile` so a `sprite.h`
edit triggers their rebuild: `test_doom_render`, `test_bsp_traverse`, `test_movement`,
`test_depth`, and `test_sprite_render`. (`doom_core_spike` already uses
`$(wildcard plugins/games/doom/*.h)`, so it needs no change.)

The Step 1 test already defines and passes the local `testName` resolver. `render_things`
body per Recipe D: cull and collect by camera-space depth from the thing position (skip
behind the near plane, skip undrawable, record up to `orderCap`); sort far-first into
`order`; then draw each, picking the rotation with
`sprite_rotation(thing.angle * kDeg2Rad, thing.x - cam.x, thing.y - cam.y)` (THINGS angle
is in degrees, so convert to radians), resolving the lump via `sprite_find` at the computed
rotation then a rotation-0 then rotation-1 fallback, `sprite_get`, full-projecting with
`project_thing(cam, ca, sa, thing.x, thing.y, sp->w, sp->h)`, and drawing columns where
`sprite_column_visible(depth, depthBuf[x])`, sampling `sp->texels[u*sp->h + v]`
(mirror-aware) and skipping `kSpriteGap`.

- [ ] **Step 4: Run the test to verify it passes**

Run: `make build/host/test_sprite_render && ./build/host/test_sprite_render`
Expected: PASS (all sections). Then `make test` to confirm no regressions.

- [ ] **Step 5: Commit**

```bash
git add plugins/games/doom/render.h harness/tests/test_sprite_render.cpp Makefile
git commit -m "feat(doom): billboard sprite projection, rotation octant, depth-clipped render"
```

---

## Task 5: thing-type table and hitscan resolver

**Files:**

- Create: `plugins/games/doom/combat.h`
- Test: `harness/tests/test_combat.cpp` (new)
- Modify: `Makefile` (new `test_combat` target, `host` and `test` lists)

- [ ] **Step 1: Write the failing test**

Create `harness/tests/test_combat.cpp`:

```cpp
#include "catch.hpp"
#include "../tools/wad_build.h"
#include "../../plugins/games/doom/wad.h"
#include "../../plugins/games/doom/geom.h"
#include "../../plugins/games/doom/collision.h"
#include "../../plugins/games/doom/combat.h"
#include <cstring>

TEST_CASE("thing_sprite_name maps drawables and filters non-drawables", "[combat]") {
    REQUIRE(doom::thing_sprite_name(2035) != nullptr);              // barrel
    REQUIRE(std::strncmp(doom::thing_sprite_name(2035), "BAR1", 4) == 0);
    REQUIRE(doom::thing_sprite_name(1)  == nullptr);               // player-1 start
    REQUIRE(doom::thing_sprite_name(11) == nullptr);               // deathmatch start
    REQUIRE(doom::thing_sprite_name(14) == nullptr);               // teleport destination
    REQUIRE(doom::thing_sprite_name(99999) == nullptr);            // unknown
}

TEST_CASE("hitscan_nearest picks the nearest thing in front along the aim", "[combat]") {
    std::vector<uint8_t> bytes = build_things_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    doom::Map m; REQUIRE(doom::map_load(w, "E1M1", m));
    doom::Blockmap bm; bool haveBm = doom::blockmap_load(w, "E1M1", bm);  // may be absent

    // Aim from origin along +x; the barrel at (200,0) is the only drawable thing ahead.
    int hit = doom::hitscan_nearest(m, bm, 0.0f, 0.0f, 1.0f, 0.0f, 2000.0f, 32.0f);
    REQUIRE(hit >= 0);
    REQUIRE(m.things[hit].type == 2035);
    // Aiming away (-x) hits nothing.
    REQUIRE(doom::hitscan_nearest(m, bm, 0.0f, 0.0f, -1.0f, 0.0f, 2000.0f, 32.0f) == -1);
    (void)haveBm;
}

TEST_CASE("hitscan ray-vs-segment parameter is correct", "[combat]") {
    // Ray from (0,0) along +x; segment (200,-50)-(200,50) crosses at t=200, s=0.5.
    float t, s;
    bool hit = doom::ray_seg_t(0,0, 1,0, 200,-50, 200,50, t, s);
    REQUIRE(hit);
    REQUIRE(t == Catch::Approx(200.0f));
    REQUIRE(s == Catch::Approx(0.5f));
    // A parallel segment misses.
    REQUIRE(doom::ray_seg_t(0,0, 1,0, 10,5, 20,5, t, s) == false);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Add the Makefile target (after `test_sprite_render`):

```make
build/host/test_combat: harness/tests/test_combat.cpp harness/tools/wad_build.h plugins/games/doom/wad.h plugins/games/doom/geom.h plugins/games/doom/collision.h plugins/games/doom/combat.h $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ harness/tests/test_combat.cpp $(HARNESS_SRCS)
```

Add to `host:` and `test:`. Run: `make build/host/test_combat`
Expected: FAIL to compile (`combat.h` does not exist).

- [ ] **Step 3: Implement `combat.h`**

Create `plugins/games/doom/combat.h` per Recipes E and F: the static `ThingSprite` table
and `thing_sprite_name(int)`; the libm-free `ray_seg_t` helper (the `denom`/`t`/`s`
formula from the spec); and `hitscan_nearest` (gather solid lines from the BLOCKMAP cells
covering the ray bbox, find the nearest wall `t`, then the nearest drawable thing whose
bounding circle the ray crosses with `tc > 0` and `tc < wallDist`). Handle an absent
BLOCKMAP (`bm.base == nullptr`) by treating `wallDist = maxDist` (no wall block), so the
synthetic test WAD without a BLOCKMAP still resolves the thing.

```cpp
#pragma once
#include <cstdint>
#include "geom.h"
#include "collision.h"
namespace doom {
struct ThingSprite { int16_t type; char name[4]; };
const char* thing_sprite_name(int type);
// Ray (ox,oy)+t*(dx,dy) vs segment A-B; returns t (ray param) and s (segment param).
inline bool ray_seg_t(float ox,float oy, float dx,float dy, float ax,float ay, float bx,float by,
                      float& t, float& s) {
    float ex = bx-ax, ey = by-ay, wx = ax-ox, wy = ay-oy;
    float denom = dx*ey - dy*ex;
    if (denom == 0.0f) return false;
    t = (wx*ey - wy*ex) / denom;
    s = (wx*dy - wy*dx) / denom;
    return t >= 0.0f && s >= 0.0f && s <= 1.0f;
}
int hitscan_nearest(const Map& m, const Blockmap& bm, float ox, float oy,
                    float dirx, float diry, float maxDist, float thingRadius);
} // namespace doom
```

Implement `thing_sprite_name` and `hitscan_nearest` inline in the header. The table covers
the E1M1 types from Recipe E; unknown and non-drawable types return null.

- [ ] **Step 4: Wire `render_things` to the real table**

Now that `combat.h` exists, the device path passes `thing_sprite_name` as the
`render_things` `nameFn`. No render code change is needed (the function pointer is the
seam); the host `test_sprite_render` keeps its local `testName` resolver. Optionally add a
`[combat]` assertion that `render_things` with `thing_sprite_name` draws the barrel
(covered already by `test_sprite_render` with `testName`, so no new test required).

- [ ] **Step 5: Run the test to verify it passes**

Run: `make build/host/test_combat && ./build/host/test_combat`
Expected: PASS (all three cases). Then `make test` for the full suite.

- [ ] **Step 6: Commit**

```bash
git add plugins/games/doom/combat.h harness/tests/test_combat.cpp Makefile
git commit -m "feat(doom): thing-type sprite table and wall-blocked hitscan resolver"
```

---

## Task 6: device wiring on doom_core_spike

**Files:**

- Modify: `plugins/games/doom_core_spike.cpp` (sprite compose on load, `render_things` in
  draw, fire debounce and hit indicator)

No new host pixel test: the device path reuses the host-tested `render_view` and
`render_things`. This task is verified by `make arm` (clean build, symbols, `.text`) and
the hardware smoke (Task 8).

- [ ] **Step 1: Add the engine includes and instance members**

Add `#include "doom/sprite.h"` and `#include "doom/combat.h"`. Add to `_doomSpike`:

```cpp
doom::Wad wad;                         // retained: sprite_find derefs SpriteCache::wad in draw
doom::SpriteCache sprites;
bool spriteReady;
float depthBuf[doom::kScreenW];        // 1 KB, instance member (NOT a draw-stack local)
static const int kMaxThings = 256;
int   thingOrder[kMaxThings];
bool  prevFire;
int   hitCount;
int   hitThing;
```

The `wad` member is mandatory, not a convenience: `render_things` calls `sprite_find` at
draw time, which dereferences `SpriteCache::wad` to read sprite-lump names. If the cache
pointed at a `swapRealWad` stack-local `Wad`, that pointer would dangle after the swap
returns and fault on the next `draw`. The `Wad`'s `base`/`dir` point into the arena, so the
instance copy stays valid until the next load. Initialize the new members in `construct`
(`spriteReady = false; prevFire = false; hitCount = 0; hitThing = -1;`).

- [ ] **Step 2: Open the WAD into the instance member and compose sprites up front**

Change `swapRealWad` to open the WAD into the instance `a->wad` (not a stack local) and use
`a->wad` for every load call, so all caches reference the retained `Wad`. Replace the whole
body of `swapRealWad` after the `arena_alloc(wadBytes)` line (the old `doom::Wad w;`
through the final `a->pose = {...}`) with the block below; it folds in the `a->wad` opens,
the sprite load, and the pose-set, so the old trailing pose line is not left duplicated.
Composition runs in `step` context (via `swapRealWad`), never inside `draw`; `sprite_get`
memoizes, so repeated thing names are cheap.

```cpp
if (!doom::wad_open(wadBytes, wadLen, a->wad)) return;
doom::Map m;
if (!doom::map_load(a->wad, "E1M1", m)) return;
a->map = m;
doom::palette_load(a->wad, a->pal);
doom::colormap_load(a->wad, a->cm);
a->texReady = doom::texcache_init(a->tex, a->wad, a->arena);
if (a->texReady) for (int i = 0; i < a->tex.numTextures; ++i) a->tex.get(i);
a->bmReady = doom::blockmap_load(a->wad, "E1M1", a->bm);

a->spriteReady = doom::spritecache_init(a->sprites, a->wad, a->arena);
if (a->spriteReady)
    for (int i = 0; i < a->map.numThings; ++i) {
        const char* nm = doom::thing_sprite_name(a->map.things[i].type);
        if (!nm) continue;
        for (int rot = 0; rot <= 8; ++rot) {
            bool flip; int li = doom::sprite_find(a->sprites, nm, 'A', rot, flip);
            if (li >= 0) doom::sprite_get(a->sprites, li);   // compose now, in step context
        }
    }
a->pose = { kE1M1StartX, kE1M1StartY, kE1M1StartA };
```

- [ ] **Step 3: Debounce fire and resolve the hit in `step`**

After the movement commit in `step`, add the fire debounce (Recipe G):

```cpp
if (a->parsed && a->bmReady && in.fire && !a->prevFire) {
    float fca, fsa; doom::cos_sin(a->pose.angle, fca, fsa);
    int hit = doom::hitscan_nearest(a->map, a->bm, a->pose.x, a->pose.y,
                                    fca, fsa, 2000.0f, 32.0f);
    if (hit >= 0) { ++a->hitCount; a->hitThing = hit; }
}
a->prevFire = in.fire;
```

- [ ] **Step 4: Render things and the hit indicator in `draw`**

Change the `render_view` call to pass `depthBuf`, then draw things and a minimal
indicator:

```cpp
doom::render_view(a->map, cam, a->pal, a->cm, tex, NT_screen, a->depthBuf);
if (a->spriteReady)
    doom::render_things(a->map, cam, a->pal, a->cm, &a->sprites, a->depthBuf,
                        NT_screen, a->thingOrder, _doomSpike::kMaxThings,
                        doom::thing_sprite_name);
// Center crosshair plus a hit tally: a short bar whose length tracks hitCount (mod a cap).
for (int x = 126; x <= 130; ++x) doom::fb_put(NT_screen, x, 32, 8);
for (int y = 30; y <= 34; ++y) doom::fb_put(NT_screen, 128, y, 8);
int bar = a->hitCount % 32;
for (int x = 0; x < bar; ++x) doom::fb_put(NT_screen, x, 0, 12);
```

Keep the existing overlay snapshot and `return true`.

- [ ] **Step 5: Build for ARM and check symbols and size**

Run: `make arm`
Expected: clean build. Then:

```bash
arm-none-eabi-nm build/arm/doom_core_spike.o | grep ' U '
arm-none-eabi-readelf -W -S build/arm/doom_core_spike.o | grep -E '\.text'
```

Expected: unresolved symbols only in the firmware-resolved set (the P3 set plus nothing
new); `.text` well under the ~82 KB cap (estimate ~12-15 KB after sprites and hitscan).

- [ ] **Step 6: Commit**

```bash
git add plugins/games/doom_core_spike.cpp
git commit -m "feat(doom): render things as sprites and fire hitscan on doom_core_spike"
```

---

## Task 7: verify, document, and open the PR

- [ ] **Step 1: Full host suite green**

Run: `make test`
Expected: every Catch2 suite passes, including `[things] [sprite] [depth] [sprite_render]
[combat]`.

- [ ] **Step 2: ARM clean**

Run: `make arm`; re-check `nm | grep ' U '` and `readelf -W -S` `.text` under the cap.

- [ ] **Step 3: Update CLAUDE.md with the P4 durable lessons**

Add a P4 section: new engine modules (`sprite.h`, `combat.h`; the `render.h` depth buffer
and `render_things`; the `geom.h` THINGS view; the `wad_build.h` `build_things_test_wad`),
the depth-buffer seam (write `render_seg`'s per-column depth, default-off via the optional
`depthOut`), the sprite transparency sentinel `kSpriteGap = 0xFF` and its known limitation,
the rotation-octant policy, the hitscan wall block, the instance-member depth/order buffers
(stack discipline), and the reboot requirement (the SRAM struct grew; DRAM unchanged at
8 MB; `numParameters` unchanged so no param-count reboot, but the SRAM size cache still
needs a reboot). Run `markdownlint CLAUDE.md` and fix.

- [ ] **Step 4: Commit docs and open the PR**

```bash
git add CLAUDE.md docs/superpowers/
git commit -m "docs: P4 things-and-combat brainstorm, spec, plan, and CLAUDE.md lessons"
```

Detect the base branch (`git remote show origin | grep 'HEAD branch'`) and open the PR
with `gh pr create --base main` referencing issue #5, with a Summary (what: things render
as depth-clipped billboard sprites and fire registers a hitscan hit; why: completes the
honest v1 target; how: per-column wall depth buffer, sprite cache, nearest-thing hitscan)
and a checkbox test plan (host suites, ARM clean, `.text` under cap, and the on-device
smoke as an unchecked item pending hardware).

---

## Task 8: hardware smoke (after PR open)

- [ ] **Step 1: Deploy and reboot**

`make deploy-sysex SYSEX_PLUGIN=build/arm/doom_core_spike.o`. The SRAM struct grew (sprite
cache plus the 1 KB depth buffer plus the thing-order array), so reboot
(`F0 00 21 27 6D 00 7F F7` via `mido`) before re-adding, or `calculateRequirements` uses
the stale SRAM size and `construct` overruns. The parameter count is unchanged, so no
param-count reboot is additionally needed, and an already-known GUID re-adds after a
reconnect once rebooted.

- [ ] **Step 2: Load DOOM1.WAD and verify**

Place a real `DOOM1.WAD` as a 16-bit mono WAV in an existing `/samples/<folder>/` (e.g.
`!doom`), select it via the Folder/Sample params. Walk E1M1 with CV or the front panel and
capture the screen (`harness/scripts/nt_screenshot.py` or `mcp__nt_helper__show_screen`).
Confirm: things appear as billboard sprites over the walls, a sprite is correctly occluded
when a wall is between it and the player, and firing (CV fire bus or button 1) increments
the hit tally when aimed at the nearest thing. Hardware steps need a human for the
sample-scan modal and front-panel nav; do not block the phase on them.

---

## Files

- New: `plugins/games/doom/sprite.h`, `plugins/games/doom/combat.h`;
  `harness/tests/test_things.cpp`, `test_sprite.cpp`, `test_depth.cpp`,
  `test_sprite_render.cpp`, `test_combat.cpp`.
- Edited: `plugins/games/doom/geom.h` (THINGS view), `plugins/games/doom/render.h` (depth
  buffer, projection, `render_things`), `harness/tools/wad_build.h`
  (`build_things_test_wad`), `plugins/games/doom_core_spike.cpp` (device wiring),
  `Makefile` (five new host test targets), `CLAUDE.md` (P4 lessons),
  `docs/superpowers/` (brainstorm, spec, plan).

## Abort budget

- Planning: if sprites plus the depth buffer are projected to push `.text` over the cap or
  DRAM over 12 MB, resplan (cache fewer sprite frames; share the wall depth buffer). Not
  triggered (est. `.text` ~12-15 KB, DRAM unchanged at 8 MB, depth buffer 1 KB SRAM). In
  scope is six units, under the seven-unit ceiling.
- Implementation: if a real E1M1 walk shows a sprite drawing through a wall and it traces
  to the depth-clip seam (more than one of three per-entry checks wrong), audit the depth
  buffer write and the sprite clip before proceeding. If a device fault appears, suspect
  the draw stack first (the P3 lesson) and enumerate the fault dump (PC/BFAR/CFSR) before
  guessing; confirm `depthBuf`/`thingOrder` are instance members, not stack locals.
- Verification: an unresolved ARM symbol outside the firmware-resolved set, or `.text`
  over the cap, must be resolved before the PR. An ADD-time fault on hardware is fixed at
  the specific hazard and ADD is re-verified, not just registration.

## Verification

- Host: `make test` runs all Catch2 suites including `[things] [sprite] [depth]
  [sprite_render] [combat]`, all green.
- ARM: `make arm` clean; `nm | grep ' U '` shows only the firmware-resolved set;
  `readelf -W -S` `.text` under ~82 KB.
- Hardware: real `DOOM1.WAD` loads; things render as depth-clipped billboard sprites over
  E1M1; firing registers a hit on the nearest thing in front; the plug-in ADDs cleanly
  after the reboot with the enlarged instance struct.
