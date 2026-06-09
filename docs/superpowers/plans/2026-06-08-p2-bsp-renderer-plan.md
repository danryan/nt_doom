# P2 BSP Wall Renderer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the single-subsector spike renderer with a real BSP front-to-back traversal, solid-seg occlusion, perspective walls (flat-shaded, then textured), shaded through the P1 palette and colormap, driven on device from a WAD loaded via the P1 read door.

**Architecture:** A pure `bsp_visit_order` enumerator walks the NODES tree front-to-back; `render_view` draws each subsector's segs, projecting seg endpoints with the spike camera math, clipping each wall span against a Doom-style `solidsegs` occluded-range list, and drawing columns shaded by sector light and depth. Wall textures are composed lazily from `TEXTURE1`/`PNAMES`/patch lumps into the DRAM arena; flat-shaded sectors are the budget fallback and the first shippable milestone. A new synthetic builder `build_bsp_test_wad()` provides a two-subsector occlusion map; the existing `build_test_wad()` and its parse tests are untouched.

**Tech Stack:** C++17 header-only engine under `plugins/games/doom/`, Catch2 host tests, `arm-none-eabi-c++` plug-in build (`BUILD_GAME` pipeline), no heap, no libm, `-fPIC`.

**Spec:** `docs/superpowers/specs/2026-06-08-p2-bsp-renderer-design.md`
**Brainstorm:** `docs/superpowers/brainstorms/2026-06-08-p2-bsp-renderer-brainstorm.md`

---

## File Structure

| File | Responsibility | Tasks |
| --- | --- | --- |
| `harness/tools/wad_build.h` | Add `build_bsp_test_wad()`: two-subsector occlusion map with acyclic NODES, PLAYPAL/COLORMAP, TEXTURE1/PNAMES/PWALL | 1 |
| `harness/tests/test_bsp_map.cpp` | Unit F parse asserts + Unit A `geom.h` seg helpers | 1, 2 |
| `plugins/games/doom/geom.h` | Add `seg_sidedef_index`, `seg_sidedef`, `seg_sector`, `seg_is_one_sided` | 2 |
| `plugins/games/doom/render.h` | Rewrite: `point_on_side`, `bsp_visit_order`, `SolidSegs` clip, `light_row`, `render_view` | 3, 4, 5 |
| `harness/tests/test_bsp_traverse.cpp` | Unit B traversal-order + Unit C solidsegs clip asserts | 3, 4 |
| `harness/tests/test_doom_render.cpp` | Rewrite: Unit D flat-shaded pixel asserts on the BSP map | 5 |
| `plugins/games/doom/texture.h` | New: `TextureCache`, TEXTURE1/PNAMES/patch parse + lazy composition | 6 |
| `harness/tests/test_texture.cpp` | Unit E parse + composition + sampling asserts | 6 |
| `harness/tools/wad_build.cpp` | Emit both `kDoomTestWad` and `kDoomBspTestWad` | 7 |
| `plugins/games/doom_core_spike.cpp` | Drive production `render_view` from `kDoomBspTestWad`; device read-door note | 7 |
| `Makefile` | Wire the three new host test binaries | 1, 3, 6 |

Order: Task 1 (F) and Task 2 (A) build the fixtures and lookups; Tasks 3-5 (B, C, D) are the tight traversal/clip/draw chain ending at the flat-shaded milestone; Task 6 (E) adds textures; Task 7 (G) wires the device plug-in and ARM verification.

---

## Task 1: Unit F - synthetic BSP test map

**Files:**

- Modify: `harness/tools/wad_build.h` (append `build_bsp_test_wad()` after `build_test_wad()`)
- Create: `harness/tests/test_bsp_map.cpp`
- Modify: `Makefile` (add `build/host/test_bsp_map` rule, add to `host:` and `test:`)

- [ ] **Step 1: Write the failing test**

Create `harness/tests/test_bsp_map.cpp`:

```cpp
#include "catch.hpp"
#include "../tools/wad_build.h"
#include "../../plugins/games/doom/wad.h"
#include "../../plugins/games/doom/geom.h"
#include "../../plugins/games/doom/palette.h"

TEST_CASE("build_bsp_test_wad is a valid IWAD with E1M1", "[bspmap]") {
    std::vector<uint8_t> bytes = build_bsp_test_wad();
    doom::Wad w;
    REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    doom::Map m;
    REQUIRE(doom::map_load(w, "E1M1", m));
}

TEST_CASE("BSP map has two subsectors split by one acyclic node", "[bspmap]") {
    std::vector<uint8_t> bytes = build_bsp_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    doom::Map m; REQUIRE(doom::map_load(w, "E1M1", m));

    REQUIRE(m.numSsecs == 2);
    REQUIRE(m.numNodes == 1);
    REQUIRE(m.numSegs == 2);

    const doom::NodeRaw& n = m.nodes[0];
    REQUIRE(n.x == 200); REQUIRE(n.y == 0);
    REQUIRE(n.dx == 0);  REQUIRE(n.dy == 100);
    // child[1] = near subsector (ssec0); child[0] = far subsector (ssec1).
    REQUIRE(doom::node_child_is_subsector(n.child[0]));
    REQUIRE(doom::node_child_is_subsector(n.child[1]));
    REQUIRE(doom::node_child_index(n.child[1]) == 0);   // near
    REQUIRE(doom::node_child_index(n.child[0]) == 1);   // far
}

TEST_CASE("BSP map carries PLAYPAL, COLORMAP, and texture lumps", "[bspmap]") {
    std::vector<uint8_t> bytes = build_bsp_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));

    doom::Palette pal; REQUIRE(doom::palette_load(w, pal));
    doom::Colormap cm; REQUIRE(doom::colormap_load(w, cm));
    REQUIRE(cm.numMaps == 34);

    REQUIRE(doom::wad_find_lump(w, "PNAMES")   >= 0);
    REQUIRE(doom::wad_find_lump(w, "TEXTURE1") >= 0);
    REQUIRE(doom::wad_find_lump(w, "PWALL")    >= 0);
}
```

- [ ] **Step 2: Add the Makefile rule, then run the test to verify it fails to build**

In `Makefile`, after the `build/host/test_geom_nodes` rule, add:

```makefile
build/host/test_bsp_map: harness/tests/test_bsp_map.cpp harness/tools/wad_build.h plugins/games/doom/wad.h plugins/games/doom/geom.h plugins/games/doom/palette.h $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ harness/tests/test_bsp_map.cpp $(HARNESS_SRCS)
```

Add `build/host/test_bsp_map` to the `host:` target list and `./build/host/test_bsp_map` to the `test:` recipe (after the `test_geom_nodes` lines).

Run: `make build/host/test_bsp_map`
Expected: FAIL with a compile or link error referencing `build_bsp_test_wad` (not declared).

- [ ] **Step 3: Implement `build_bsp_test_wad()`**

Append to `harness/tools/wad_build.h`, before the final `// (end of file)` (after `build_test_wad()` closes):

```cpp
// Builds a valid IWAD for the P2 BSP renderer: two one-sided walls in two
// subsectors split by one real acyclic node, posed so the near wall (x=100)
// occludes the far wall (x=300) only in the shared central columns. Includes
// PLAYPAL/COLORMAP (gray ramp + 34 darkening maps) and TEXTURE1/PNAMES/PWALL.
inline std::vector<uint8_t> build_bsp_test_wad() {
    using namespace wadbuild;
    std::vector<Lump> lumps;
    auto add = [&](const char* nm, std::vector<uint8_t> d) {
        Lump L; memset(L.name, 0, 8);
        for (int i = 0; i < 8 && nm[i]; ++i) L.name[i] = nm[i];
        L.data = std::move(d); lumps.push_back(std::move(L));
    };

    add("E1M1", {});

    // THINGS: player-1 start at (0,0) facing +x (angle 0).
    { std::vector<uint8_t> d; w16(d,0); w16(d,0); w16(d,0); w16(d,1); w16(d,7); add("THINGS", d); }

    // VERTEXES: near wall v0,v1 at x=100; far wall v2,v3 at x=300.
    { std::vector<uint8_t> d;
      int16_t xs[4]={100,100,300,300}, ys[4]={-40,40,-200,200};
      for (int i=0;i<4;++i){ w16(d,xs[i]); w16(d,ys[i]); } add("VERTEXES", d); }

    // LINEDEFS: v1,v2,flags,special,tag,front,back(0xFFFF). Two one-sided lines.
    { std::vector<uint8_t> d;
      // line0: v0->v1, front sidedef 0. line1: v2->v3, front sidedef 1.
      w16(d,0); w16(d,1); w16(d,1); w16(d,0); w16(d,0); w16(d,0); w16(d,(int16_t)0xFFFF);
      w16(d,2); w16(d,3); w16(d,1); w16(d,0); w16(d,0); w16(d,1); w16(d,(int16_t)0xFFFF);
      add("LINEDEFS", d); }

    // SIDEDEFS: xoff,yoff,upper,lower,middle,sector. side0->sector0, side1->sector1.
    { std::vector<uint8_t> d;
      w16(d,0); w16(d,0); name8(d,"-"); name8(d,"-"); name8(d,"WALL"); w16(d,0);
      w16(d,0); w16(d,0); name8(d,"-"); name8(d,"-"); name8(d,"WALL"); w16(d,1);
      add("SIDEDEFS", d); }

    // SEGS: v1,v2,angle,linedef,side,offset. seg0=near (line0), seg1=far (line1).
    { std::vector<uint8_t> d;
      w16(d,0); w16(d,1); w16(d,0); w16(d,0); w16(d,0); w16(d,0);
      w16(d,2); w16(d,3); w16(d,0); w16(d,1); w16(d,0); w16(d,0);
      add("SEGS", d); }

    // SSECTORS: ssec0=near (1 seg from 0), ssec1=far (1 seg from 1).
    { std::vector<uint8_t> d; w16(d,1); w16(d,0); w16(d,1); w16(d,1); add("SSECTORS", d); }

    // NODES: partition x=200, dir (0,100). child[0]=far ssec1, child[1]=near ssec0.
    { std::vector<uint8_t> d;
      w16(d,200); w16(d,0); w16(d,0); w16(d,100);
      int16_t bb[2][4] = {{200,-200,300,300},{40,-40,100,100}};
      for (int c=0;c<2;++c) for (int k=0;k<4;++k) w16(d,bb[c][k]);
      w16(d,(int16_t)(0x8000|1)); w16(d,(int16_t)(0x8000|0));
      add("NODES", d); }

    // SECTORS: floorh,ceilh,floortex,ceiltex,light,special,tag. Near brighter.
    { std::vector<uint8_t> d;
      w16(d,0); w16(d,128); name8(d,"FLAT"); name8(d,"FLAT"); w16(d,224); w16(d,0); w16(d,0);
      w16(d,0); w16(d,128); name8(d,"FLAT"); name8(d,"FLAT"); w16(d,160); w16(d,0); w16(d,0);
      add("SECTORS", d); }

    // PLAYPAL: gray ramp, entry i = (i,i,i).
    { std::vector<uint8_t> d;
      for (int i=0;i<256;++i){ d.push_back((uint8_t)i); d.push_back((uint8_t)i); d.push_back((uint8_t)i); }
      add("PLAYPAL", d); }

    // COLORMAP: 34 maps, map[m][i] = i*(33-m)/33.
    { std::vector<uint8_t> d;
      for (int m=0;m<34;++m) for (int i=0;i<256;++i) d.push_back((uint8_t)((i*(33-m))/33));
      add("COLORMAP", d); }

    // PNAMES: one patch name PWALL.
    { std::vector<uint8_t> d;
      auto o32=[&](int32_t x){ d.push_back(x&0xFF); d.push_back((x>>8)&0xFF); d.push_back((x>>16)&0xFF); d.push_back((x>>24)&0xFF); };
      o32(1); name8(d,"PWALL"); add("PNAMES", d); }

    // TEXTURE1: one 16x16 texture WALL from patch 0 (PWALL) at origin (0,0).
    { std::vector<uint8_t> d;
      auto o32=[&](int32_t x){ d.push_back(x&0xFF); d.push_back((x>>8)&0xFF); d.push_back((x>>16)&0xFF); d.push_back((x>>24)&0xFF); };
      o32(1);            // numTextures
      o32(4 + 4);        // offset[0]: after the 4-byte count + 4-byte offset table
      name8(d,"WALL"); o32(0); w16(d,16); w16(d,16); o32(0); w16(d,1);  // maptexture
      w16(d,0); w16(d,0); w16(d,0); w16(d,0); w16(d,0);                  // mappatch
      add("TEXTURE1", d); }

    // PWALL: 16x16 patch, every column a full post, pixel[row] = row (0..15).
    { std::vector<uint8_t> d;
      auto o32=[&](int32_t x){ d.push_back(x&0xFF); d.push_back((x>>8)&0xFF); d.push_back((x>>16)&0xFF); d.push_back((x>>24)&0xFF); };
      w16(d,16); w16(d,16); w16(d,0); w16(d,0);     // width,height,left,top
      int32_t colStart = 8 + 16*4;                   // header + offset table
      int32_t colBytes = 1+1+1+16+1+1;               // topdelta,length,pad,16px,pad,0xFF
      for (int c=0;c<16;++c) o32(colStart + c*colBytes);
      for (int c=0;c<16;++c) {
          d.push_back(0); d.push_back(16); d.push_back(0);   // topdelta=0,length=16,pad
          for (int row=0;row<16;++row) d.push_back((uint8_t)row);
          d.push_back(0); d.push_back(0xFF);                  // pad, terminator
      }
      add("PWALL", d); }

    // Assemble: 12-byte header, lump data, then directory.
    std::vector<uint8_t> out;
    auto o32 = [&](std::vector<uint8_t>& v, int32_t x){
        v.push_back(x & 0xFF); v.push_back((x>>8)&0xFF); v.push_back((x>>16)&0xFF); v.push_back((x>>24)&0xFF); };
    out.push_back('I'); out.push_back('W'); out.push_back('A'); out.push_back('D');
    o32(out, (int32_t)lumps.size());
    int32_t dirOffsetPos = (int32_t)out.size();
    o32(out, 0);
    std::vector<std::pair<int32_t,int32_t>> dir;
    for (auto& L : lumps) {
        int32_t pos = (int32_t)out.size();
        out.insert(out.end(), L.data.begin(), L.data.end());
        dir.push_back({pos, (int32_t)L.data.size()});
    }
    int32_t dirStart = (int32_t)out.size();
    for (size_t i = 0; i < lumps.size(); ++i) {
        o32(out, dir[i].first); o32(out, dir[i].second);
        out.insert(out.end(), lumps[i].name, lumps[i].name + 8);
    }
    out[dirOffsetPos+0] = dirStart & 0xFF; out[dirOffsetPos+1] = (dirStart>>8)&0xFF;
    out[dirOffsetPos+2] = (dirStart>>16)&0xFF; out[dirOffsetPos+3] = (dirStart>>24)&0xFF;
    return out;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `make build/host/test_bsp_map && ./build/host/test_bsp_map`
Expected: PASS (all `[bspmap]` assertions). Then `make test` to confirm no P1 regression.

- [ ] **Step 5: Commit**

```bash
git add harness/tools/wad_build.h harness/tests/test_bsp_map.cpp Makefile
git commit -m "test(doom): add build_bsp_test_wad synthetic occlusion map (P2 unit F)"
```

---

## Task 2: Unit A - seg to sector and wall-name helpers

**Files:**

- Modify: `plugins/games/doom/geom.h` (add helpers before the closing `} // namespace doom`)
- Modify: `harness/tests/test_bsp_map.cpp` (append seg-helper test cases)

- [ ] **Step 1: Write the failing test**

Append to `harness/tests/test_bsp_map.cpp`:

```cpp
TEST_CASE("seg_sector resolves seg -> linedef -> sidedef -> sector", "[segres]") {
    std::vector<uint8_t> bytes = build_bsp_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    doom::Map m; REQUIRE(doom::map_load(w, "E1M1", m));

    const doom::SectorRaw* near = doom::seg_sector(m, 0);   // seg0 -> sector0
    const doom::SectorRaw* far  = doom::seg_sector(m, 1);   // seg1 -> sector1
    REQUIRE(near != nullptr);
    REQUIRE(far  != nullptr);
    REQUIRE(near->light == 224);
    REQUIRE(far->light  == 160);
}

TEST_CASE("seg_is_one_sided is true for back == 0xFFFF", "[segres]") {
    std::vector<uint8_t> bytes = build_bsp_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    doom::Map m; REQUIRE(doom::map_load(w, "E1M1", m));
    REQUIRE(doom::seg_is_one_sided(m, 0));
    REQUIRE(doom::seg_is_one_sided(m, 1));
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make build/host/test_bsp_map && ./build/host/test_bsp_map`
Expected: FAIL to compile with `seg_sector` / `seg_is_one_sided` not declared.

- [ ] **Step 3: Implement the helpers**

In `plugins/games/doom/geom.h`, add before `} // namespace doom`:

```cpp
inline int16_t seg_sidedef_index(const Map& m, int32_t segIndex) {
    const SegRaw& s = m.segs[segIndex];
    const LinedefRaw& ld = m.lines[s.linedef];
    return s.side == 0 ? ld.front : ld.back;   // 0xFFFF when absent
}
inline const SidedefRaw* seg_sidedef(const Map& m, int32_t segIndex) {
    int16_t si = seg_sidedef_index(m, segIndex);
    if ((uint16_t)si == 0xFFFFu || si < 0 || si >= m.numSides) return nullptr;
    return &m.sides[si];
}
inline const SectorRaw* seg_sector(const Map& m, int32_t segIndex) {
    const SidedefRaw* sd = seg_sidedef(m, segIndex);
    if (!sd || sd->sector < 0 || sd->sector >= m.numSectors) return nullptr;
    return &m.sectors[sd->sector];
}
inline bool seg_is_one_sided(const Map& m, int32_t segIndex) {
    return (uint16_t)m.lines[m.segs[segIndex].linedef].back == 0xFFFFu;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `make build/host/test_bsp_map && ./build/host/test_bsp_map`
Expected: PASS (`[segres]` included). Then `make test`.

- [ ] **Step 5: Commit**

```bash
git add plugins/games/doom/geom.h harness/tests/test_bsp_map.cpp
git commit -m "feat(doom): add seg->sector and one-sided helpers to geom (P2 unit A)"
```

---

## Task 3: Unit B - BSP front-to-back traversal

**Files:**

- Modify: `plugins/games/doom/render.h` (begin the rewrite: add `point_on_side`, `bsp_visit_order`; keep the existing spike `render_view` for now so the old render test still links until Task 5)
- Create: `harness/tests/test_bsp_traverse.cpp`
- Modify: `Makefile` (add `build/host/test_bsp_traverse`)

Note: `render.h` is rewritten incrementally. In this task add the new traversal functions alongside the existing spike code. Task 5 removes the spike `render_view` and replaces it with the production one. Add `#include "palette.h"` and a `struct TextureCache;` forward declaration at the top of `render.h` now so later tasks compile cleanly.

- [ ] **Step 1: Write the failing test**

Create `harness/tests/test_bsp_traverse.cpp`:

```cpp
#include "catch.hpp"
#include "../tools/wad_build.h"
#include "../../plugins/games/doom/wad.h"
#include "../../plugins/games/doom/geom.h"
#include "../../plugins/games/doom/render.h"
#include <cmath>

namespace doom { void cos_sin(float a, float& c, float& s) { c = std::cos(a); s = std::sin(a); } }

TEST_CASE("bsp_visit_order returns near subsector before far", "[traverse]") {
    std::vector<uint8_t> bytes = build_bsp_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    doom::Map m; REQUIRE(doom::map_load(w, "E1M1", m));

    doom::Camera cam{0.0f, 0.0f, 0.0f};   // player start, facing +x
    int order[8]; int n = doom::bsp_visit_order(m, cam, order, 8);
    REQUIRE(n == 2);
    REQUIRE(order[0] == 0);   // near (ssec0) first
    REQUIRE(order[1] == 1);   // far  (ssec1) second
}

TEST_CASE("bsp_visit_order falls back to all subsectors when node-less", "[traverse]") {
    // build_test_wad has one subsector and one (parse-only) node; force the
    // node-less path by zeroing numNodes on the loaded map view.
    std::vector<uint8_t> bytes = build_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    doom::Map m; REQUIRE(doom::map_load(w, "E1M1", m));
    m.numNodes = 0; m.nodes = nullptr;

    doom::Camera cam{128.0f, 128.0f, 0.0f};
    int order[8]; int n = doom::bsp_visit_order(m, cam, order, 8);
    REQUIRE(n == m.numSsecs);
    REQUIRE(order[0] == 0);
}

TEST_CASE("bsp_visit_order does not hang on a cyclic node", "[traverse]") {
    // build_test_wad's NODES child[1] = 0x0000 points back at node 0 (a cycle).
    // The depth guard must cut the recursion rather than loop forever.
    std::vector<uint8_t> bytes = build_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    doom::Map m; REQUIRE(doom::map_load(w, "E1M1", m));

    doom::Camera cam{128.0f, 128.0f, 0.0f};
    int order[64]; int n = doom::bsp_visit_order(m, cam, order, 64);
    REQUIRE(n >= 0);
    REQUIRE(n <= 64);   // bounded, no hang
}
```

- [ ] **Step 2: Add the Makefile rule, then run to verify it fails**

In `Makefile`, after the `build/host/test_bsp_map` rule add:

```makefile
build/host/test_bsp_traverse: harness/tests/test_bsp_traverse.cpp harness/tools/wad_build.h plugins/games/doom/wad.h plugins/games/doom/geom.h plugins/games/doom/render.h $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ harness/tests/test_bsp_traverse.cpp $(HARNESS_SRCS)
```

Add `build/host/test_bsp_traverse` to `host:` and `./build/host/test_bsp_traverse` to `test:`.

Run: `make build/host/test_bsp_traverse`
Expected: FAIL with `bsp_visit_order` / `point_on_side` not declared.

- [ ] **Step 3: Implement the traversal**

At the top of `plugins/games/doom/render.h`, ensure the includes and forward declaration are present (add if missing):

```cpp
#pragma once
#include <cstdint>
#include "geom.h"
#include "fb.h"
#include "palette.h"

namespace doom {

struct TextureCache;   // forward decl; Unit E defines it. render_view takes a pointer only.

void cos_sin(float ang, float& c, float& s);   // per-TU seam (host cmath / ARM LUT)

struct Camera { float x, y, angle; };
```

Then add the traversal functions inside `namespace doom` (keep the existing spike `render_view` for now):

```cpp
// Doom R_PointOnSide: cross = right - left; 0 = front/right side, 1 = back/left.
inline int point_on_side(const NodeRaw& n, float cx, float cy) {
    float cross = (float)n.dx * (cy - n.y) - (float)n.dy * (cx - n.x);
    return cross < 0.0f ? 0 : 1;
}

// Fills out[0..count) with subsector indices front-to-back; returns count.
// Depth guard cuts cyclic/malformed NODES (real WADs are acyclic).
inline int bsp_visit_order(const Map& m, const Camera& cam, int* out, int cap) {
    int count = 0;
    auto recurse = [&](auto&& self, uint16_t ref, int depth) -> void {
        if (count >= cap || depth > m.numNodes) return;
        if (node_child_is_subsector(ref)) { out[count++] = node_child_index(ref); return; }
        if (ref >= (uint16_t)m.numNodes) return;
        const NodeRaw& n = m.nodes[ref];
        int side = point_on_side(n, cam.x, cam.y);
        self(self, n.child[side],     depth + 1);   // near first
        self(self, n.child[side ^ 1], depth + 1);   // far
    };
    if (m.numNodes == 0) {
        for (int32_t i = 0; i < m.numSsecs && count < cap; ++i) out[count++] = (int)i;
    } else {
        recurse(recurse, (uint16_t)(m.numNodes - 1), 0);
    }
    return count;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `make build/host/test_bsp_traverse && ./build/host/test_bsp_traverse`
Expected: PASS (`[traverse]`). Then `make test` (the old `test_doom_render` still links against the spike `render_view`, untouched).

- [ ] **Step 5: Commit**

```bash
git add plugins/games/doom/render.h harness/tests/test_bsp_traverse.cpp Makefile
git commit -m "feat(doom): BSP front-to-back visit-order enumerator (P2 unit B)"
```

---

## Task 4: Unit C - solidsegs occlusion clip

**Files:**

- Modify: `plugins/games/doom/render.h` (add `SolidSegs` and the clip)
- Modify: `harness/tests/test_bsp_traverse.cpp` (append clip test cases)

- [ ] **Step 1: Write the failing test**

Append to `harness/tests/test_bsp_traverse.cpp`:

```cpp
#include <vector>

namespace {
// Records the visible sub-spans a clip emits, for assertion.
struct SpanRec { std::vector<std::pair<int,int>> spans;
    void operator()(int a, int b) { spans.push_back({a, b}); } };
}

TEST_CASE("solidsegs: first wall draws whole span", "[solidsegs]") {
    doom::SolidSegs s; doom::solidsegs_clear(s);
    SpanRec rec;
    doom::solidsegs_clip_solid(s, 76, 179, [&](int a, int b){ rec(a, b); });
    REQUIRE(rec.spans.size() == 1);
    REQUIRE(rec.spans[0] == std::make_pair(76, 179));
}

TEST_CASE("solidsegs: far wall is clipped by a nearer one", "[solidsegs]") {
    doom::SolidSegs s; doom::solidsegs_clear(s);
    SpanRec near; doom::solidsegs_clip_solid(s, 76, 179, [&](int a, int b){ near(a, b); });
    SpanRec far;  doom::solidsegs_clip_solid(s, 42, 213, [&](int a, int b){ far(a, b); });
    // Far draws only the two side gaps, never the occluded centre [76,179].
    REQUIRE(far.spans.size() == 2);
    REQUIRE(far.spans[0] == std::make_pair(42, 75));
    REQUIRE(far.spans[1] == std::make_pair(180, 213));
}

TEST_CASE("solidsegs: fully occluded wall draws nothing", "[solidsegs]") {
    doom::SolidSegs s; doom::solidsegs_clear(s);
    SpanRec a; doom::solidsegs_clip_solid(s, 50, 200, [&](int x, int y){ a(x, y); });
    SpanRec b; doom::solidsegs_clip_solid(s, 80, 150, [&](int x, int y){ b(x, y); });
    REQUIRE(b.spans.empty());
}

TEST_CASE("solidsegs: adjacent ranges coalesce", "[solidsegs]") {
    doom::SolidSegs s; doom::solidsegs_clear(s);
    SpanRec a; doom::solidsegs_clip_solid(s, 10, 20, [&](int x, int y){ a(x, y); });
    SpanRec b; doom::solidsegs_clip_solid(s, 21, 30, [&](int x, int y){ b(x, y); });
    // After two adjacent inserts the list is one merged range; a covering wall
    // over [10,30] is fully occluded.
    SpanRec c; doom::solidsegs_clip_solid(s, 10, 30, [&](int x, int y){ c(x, y); });
    REQUIRE(c.spans.empty());
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make build/host/test_bsp_traverse && ./build/host/test_bsp_traverse`
Expected: FAIL to compile with `SolidSegs` / `solidsegs_clip_solid` not declared.

- [ ] **Step 3: Implement solidsegs**

In `plugins/games/doom/render.h`, inside `namespace doom`, add after `bsp_visit_order`:

```cpp
static const int kMaxClip = 64;          // disjoint occluded ranges; ample for 256 cols
struct SolidSegs {
    struct Range { int16_t first, last; };   // inclusive, occluded; sorted by first
    Range r[kMaxClip];
    int n;
};
inline void solidsegs_clear(SolidSegs& s) { s.n = 0; }

// Insert [a,b] into the sorted list, coalescing overlapping/adjacent ranges.
inline void solidsegs_insert(SolidSegs& s, int a, int b) {
    Range nr{(int16_t)a, (int16_t)b};
    Range merged[kMaxClip]; int mn = 0; bool placed = false;
    auto flush = [&](Range x){ if (mn < kMaxClip) merged[mn++] = x; };
    for (int i = 0; i < s.n; ++i) {
        Range cur = s.r[i];
        if (!placed && nr.last + 1 < cur.first) { flush(nr); placed = true; }
        if (placed || cur.last + 1 < nr.first) { flush(cur); }
        else { // overlap/adjacent with nr: absorb into nr
            if (cur.first < nr.first) nr.first = cur.first;
            if (cur.last  > nr.last)  nr.last  = cur.last;
        }
    }
    if (!placed) flush(nr);
    s.n = mn; for (int i = 0; i < mn; ++i) s.r[i] = merged[i];
}

// Clip solid wall span [x1,x2] (clamped, x1<=x2): emit visible gaps via drawSpan,
// then mark the span occluded. Front-to-back order makes each column drawn once.
template<class DrawSpan>
inline void solidsegs_clip_solid(SolidSegs& s, int x1, int x2, DrawSpan drawSpan) {
    if (x1 < 0) x1 = 0; if (x2 > kScreenWmax) x2 = kScreenWmax;
    if (x2 < x1) return;
    int cur = x1;
    for (int i = 0; i < s.n && cur <= x2; ++i) {
        const SolidSegs::Range& R = s.r[i];
        if (R.last < cur) continue;
        if (R.first > x2) break;
        if (R.first > cur) drawSpan(cur, R.first - 1);
        if (R.last + 1 > cur) cur = R.last + 1;
    }
    if (cur <= x2) drawSpan(cur, x2);
    solidsegs_insert(s, x1, x2);
}
```

Add the screen-width clamp constant near the top of `namespace doom` (above `point_on_side`), if not already present from the spike code:

```cpp
static const int kScreenWmax = 255;   // last screen column
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `make build/host/test_bsp_traverse && ./build/host/test_bsp_traverse`
Expected: PASS (`[solidsegs]` and `[traverse]`). Then `make test`.

- [ ] **Step 5: Commit**

```bash
git add plugins/games/doom/render.h harness/tests/test_bsp_traverse.cpp
git commit -m "feat(doom): solidsegs occlusion clip (P2 unit C)"
```

---

## Task 5: Unit D - flat-shaded production render_view

**Files:**

- Modify: `plugins/games/doom/render.h` (replace the spike `render_view` with the production one; add `light_row`)
- Rewrite: `harness/tests/test_doom_render.cpp` (drive `build_bsp_test_wad`, flat-shaded pixel asserts)
- Modify: `Makefile` (update the `build/host/test_doom_render` prerequisites to add `palette.h`)

- [ ] **Step 1: Write the failing test (rewrite the render test)**

Replace the entire contents of `harness/tests/test_doom_render.cpp` with:

```cpp
#include "catch.hpp"
#include "../tools/wad_build.h"
#include "../../plugins/games/doom/wad.h"
#include "../../plugins/games/doom/geom.h"
#include "../../plugins/games/doom/palette.h"
#include "../../plugins/games/doom/render.h"
#include "../../plugins/games/doom/fb.h"
#include <cmath>

namespace doom { void cos_sin(float a, float& c, float& s) { c = std::cos(a); s = std::sin(a); } }

namespace {
struct Scene {
    std::vector<uint8_t> bytes; doom::Wad w; doom::Map m;
    doom::Palette pal; doom::Colormap cm;
    Scene() {
        bytes = build_bsp_test_wad();
        REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
        REQUIRE(doom::map_load(w, "E1M1", m));
        REQUIRE(doom::palette_load(w, pal));
        REQUIRE(doom::colormap_load(w, cm));
    }
};
int column_lit_count(const uint8_t* fb, int x) {
    int lit = 0; for (int y = 0; y < 64; ++y) if (doom::fb_get(fb, x, y) != 0) ++lit;
    return lit;
}
}

TEST_CASE("flat render draws walls in the projected columns", "[render]") {
    Scene sc; uint8_t fb[128 * 64];
    doom::Camera cam{0.0f, 0.0f, 0.0f};
    doom::render_view(sc.m, cam, sc.pal, sc.cm, nullptr, fb);

    REQUIRE(column_lit_count(fb, 128) > 0);   // centre: both walls project here
    REQUIRE(column_lit_count(fb, 60)  > 0);   // far-only left gap [42,75]
    REQUIRE(column_lit_count(fb, 20) == 0);   // outside both spans: unlit
}

TEST_CASE("near wall occludes far wall in the overlap", "[render]") {
    Scene sc; uint8_t fb[128 * 64];
    doom::Camera cam{0.0f, 0.0f, 0.0f};
    doom::render_view(sc.m, cam, sc.pal, sc.cm, nullptr, fb);

    // Find a lit row in the overlap column (near band) and in a far-only column.
    auto first_lit = [&](int x){ for (int y = 0; y < 64; ++y) if (doom::fb_get(fb, x, y)) return doom::fb_get(fb, x, y); return (uint8_t)0; };
    uint8_t overlapShade = first_lit(128);   // near owns this column
    uint8_t farShade     = first_lit(60);    // far-only column
    REQUIRE(overlapShade != 0);
    REQUIRE(farShade != 0);
    // Near sector is brighter and nearer, so its shade is brighter than the far wall's.
    REQUIRE(overlapShade > farShade);
}
```

- [ ] **Step 2: Update the Makefile prerequisites, then run to verify it fails**

In `Makefile`, change the `build/host/test_doom_render` prerequisite line to include `palette.h`:

```makefile
build/host/test_doom_render: harness/tests/test_doom_render.cpp harness/tools/wad_build.h plugins/games/doom/wad.h plugins/games/doom/geom.h plugins/games/doom/palette.h plugins/games/doom/render.h plugins/games/doom/fb.h $(HARNESS_SRCS)
```

Run: `make build/host/test_doom_render`
Expected: FAIL to compile - the spike `render_view(map, cam, fb)` signature does not match the new 6-arg call.

- [ ] **Step 3: Replace the spike render_view with the production renderer**

In `plugins/games/doom/render.h`, delete the spike `render_view`, the spike `shade`, and the spike `kWallTex` block. Add the projection constants, `light_row`, and the production `render_view`. Keep `cos_sin`, `point_on_side`, `bsp_visit_order`, `SolidSegs`, the clip, and the new constants.

```cpp
static const int kScreenW = 256, kScreenH = 64;
static const float kFovScale  = (float)kScreenW / 2.0f;   // ~90 deg horizontal
static const float kWallScale = 32.0f;                    // wall-height constant
static const float kDepthLight = 0.03f;                   // depth -> colormap rows
static const uint8_t kFlatWallIndex = 200;                // flat-mode base palette index

inline int light_row(int sectorLight, float depth, int numMaps) {
    if (sectorLight < 0) sectorLight = 0; if (sectorLight > 255) sectorLight = 255;
    int base = (255 - sectorLight) >> 3;            // 0 (bright) .. 31 (dark)
    int dadd = (int)(depth * kDepthLight);
    int row  = base + dadd;
    if (row < 0) row = 0; if (numMaps > 0 && row >= numMaps) row = numMaps - 1;
    return row;
}

// Draw one seg's wall columns into fb, clipped against the solidsegs list.
inline void render_seg(const Map& m, int32_t segIndex, float ca, float sa,
                       const Camera& cam, const Palette& pal, const Colormap& cm,
                       SolidSegs& solid, uint8_t* fb) {
    const SegRaw& seg = m.segs[segIndex];
    const VertexRaw& A = m.verts[seg.v1];
    const VertexRaw& B = m.verts[seg.v2];
    float ax = A.x - cam.x, ay = A.y - cam.y;
    float bx = B.x - cam.x, by = B.y - cam.y;
    float a1 =  ax * ca + ay * sa, a2 = -ax * sa + ay * ca;   // a1 depth, a2 lateral
    float b1 =  bx * ca + by * sa, b2 = -bx * sa + by * ca;
    if (a1 <= 1.0f && b1 <= 1.0f) return;                     // wholly behind
    if (a1 < 1.0f) a1 = 1.0f;
    if (b1 < 1.0f) b1 = 1.0f;
    int sxA = (int)(kScreenW / 2 + (a2 / a1) * kFovScale);
    int sxB = (int)(kScreenW / 2 + (b2 / b1) * kFovScale);
    float dA = a1, dB = b1;
    if (sxA > sxB) { int t = sxA; sxA = sxB; sxB = t; float td = dA; dA = dB; dB = td; }
    if (sxB < sxA) return;

    const SectorRaw* sec = seg_sector(m, segIndex);
    int sectorLight = sec ? sec->light : 128;

    int spanA = sxA < 0 ? 0 : sxA;
    int spanB = sxB > kScreenWmax ? kScreenWmax : sxB;
    if (spanB < spanA) return;

    solidsegs_clip_solid(solid, spanA, spanB, [&](int vx1, int vx2) {
        for (int x = vx1; x <= vx2; ++x) {
            float t = (sxB == sxA) ? 0.0f : (float)(x - sxA) / (float)(sxB - sxA);
            float depth = dA + (dB - dA) * t;
            if (depth < 1.0f) depth = 1.0f;
            float wallH = (kScreenH * kWallScale) / depth;
            int top = (int)(kScreenH / 2 - wallH / 2);
            int bot = (int)(kScreenH / 2 + wallH / 2);
            if (top < 0) top = 0;
            if (bot > kScreenH - 1) bot = kScreenH - 1;
            int light = light_row(sectorLight, depth, cm.numMaps);
            for (int y = top; y <= bot; ++y)
                fb_put(fb, x, y, shade_gray(pal, cm, kFlatWallIndex, light));
        }
    });
}

inline void render_subsector(const Map& m, int32_t ssecIndex, float ca, float sa,
                             const Camera& cam, const Palette& pal, const Colormap& cm,
                             SolidSegs& solid, uint8_t* fb) {
    const SubsecRaw& ss = m.ssecs[ssecIndex];
    for (int s = 0; s < ss.numSegs; ++s)
        render_seg(m, ss.firstSeg + s, ca, sa, cam, pal, cm, solid, fb);
}

// Production renderer. tex == nullptr selects flat-shaded sectors (Unit D);
// Unit E passes a TextureCache and textures the columns instead.
inline void render_view(const Map& m, const Camera& cam,
                        const Palette& pal, const Colormap& cm,
                        const TextureCache* tex, uint8_t* fb) {
    (void)tex;   // Unit E consumes this; flat-shaded ignores it.
    fb_clear(fb);
    float ca, sa; cos_sin(cam.angle, ca, sa);
    SolidSegs solid; solidsegs_clear(solid);
    int order[1024];
    int n = bsp_visit_order(m, cam, order, 1024);
    for (int i = 0; i < n; ++i)
        render_subsector(m, order[i], ca, sa, cam, pal, cm, solid, fb);
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `make build/host/test_doom_render && ./build/host/test_doom_render`
Expected: PASS (`[render]`). Then `make test` (full suite green).

- [ ] **Step 5: Measure ARM .text at the flat-shaded milestone**

Run:

```bash
make build/arm/doom_core_spike.o
arm-none-eabi-readelf -W -S build/arm/doom_core_spike.o | grep ' .text '
arm-none-eabi-nm build/arm/doom_core_spike.o | grep ' U '
```

Note: `doom_core_spike.cpp` still calls the spike-style 3-arg `render_view` and will FAIL to build until Task 7. That is expected. If you need the `.text` number now, defer it to Task 7 where the plug-in is updated; otherwise this milestone's measurement happens in Task 7 Step 5. Record the host-green flat-shaded milestone here.

- [ ] **Step 6: Commit**

```bash
git add plugins/games/doom/render.h harness/tests/test_doom_render.cpp Makefile
git commit -m "feat(doom): flat-shaded BSP render_view with occlusion (P2 unit D)"
```

---

## Task 6: Unit E - TEXTURE1/PNAMES/patch composition

**Files:**

- Create: `plugins/games/doom/texture.h`
- Create: `harness/tests/test_texture.cpp`
- Modify: `plugins/games/doom/render.h` (include `texture.h`; wire `texture_sample` into `render_seg` when `tex != nullptr`)
- Modify: `Makefile` (add `build/host/test_texture`; add `texture.h` to render/doom_render prerequisites)

- [ ] **Step 1: Write the failing test**

Create `harness/tests/test_texture.cpp`:

```cpp
#include "catch.hpp"
#include "../tools/wad_build.h"
#include "../../plugins/games/doom/wad.h"
#include "../../plugins/games/doom/arena.h"
#include "../../plugins/games/doom/texture.h"

namespace {
struct Fix {
    std::vector<uint8_t> bytes; doom::Wad w;
    std::vector<uint8_t> mem; doom::Arena arena;
    doom::TextureCache tc;
    Fix() {
        bytes = build_bsp_test_wad();
        REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
        mem.resize(1u << 20);
        doom::arena_init(arena, mem.data(), (uint32_t)mem.size());
        REQUIRE(doom::texcache_init(tc, w, arena));
    }
};
}

TEST_CASE("texcache finds WALL in the TEXTURE1 directory", "[texture]") {
    Fix f;
    char nm[8] = {'W','A','L','L',0,0,0,0};
    int id = f.tc.find(nm);
    REQUIRE(id >= 0);
}

TEST_CASE("texcache composes WALL as a 16x16 column-major buffer", "[texture]") {
    Fix f;
    char nm[8] = {'W','A','L','L',0,0,0,0};
    const doom::Texture* T = f.tc.get(f.tc.find(nm));
    REQUIRE(T != nullptr);
    REQUIRE(T->w == 16);
    REQUIRE(T->h == 16);
    // PWALL pixel[row] = row in every column, so texels[u*h + v] == v.
    REQUIRE(T->texels[5 * 16 + 3] == 3);
    REQUIRE(T->texels[0 * 16 + 0] == 0);
    REQUIRE(T->texels[15 * 16 + 15] == 15);
}

TEST_CASE("texture_sample returns the texel at the affine (u,v)", "[texture]") {
    Fix f;
    char nm[8] = {'W','A','L','L',0,0,0,0};
    int id = f.tc.find(nm);
    doom::Map m; REQUIRE(doom::map_load(f.w, "E1M1", m));
    // seg0, t=0 -> u=0; y=top -> v=0 -> texel 0.
    uint8_t s0 = doom::texture_sample(&f.tc, m, 0, id, 0.0f, 0, 0, 15);
    REQUIRE(s0 == 0);
    // y = bottom row of a 16-tall span -> v=15 -> texel 15.
    uint8_t s1 = doom::texture_sample(&f.tc, m, 0, id, 0.0f, 15, 0, 15);
    REQUIRE(s1 == 15);
}
```

- [ ] **Step 2: Add the Makefile rule, then run to verify it fails**

In `Makefile`, after the `build/host/test_bsp_traverse` rule add:

```makefile
build/host/test_texture: harness/tests/test_texture.cpp harness/tools/wad_build.h plugins/games/doom/wad.h plugins/games/doom/geom.h plugins/games/doom/arena.h plugins/games/doom/texture.h $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) -o $@ harness/tests/test_texture.cpp $(HARNESS_SRCS)
```

Add `build/host/test_texture` to `host:` and `./build/host/test_texture` to `test:`.

Run: `make build/host/test_texture`
Expected: FAIL - `texture.h` does not exist.

- [ ] **Step 3: Implement texture.h**

Create `plugins/games/doom/texture.h`:

```cpp
#pragma once
#include <cstdint>
#include "wad.h"
#include "geom.h"
#include "arena.h"

namespace doom {

static const int kMaxTextures = 32;

struct Texture { const uint8_t* texels; int16_t w, h; };   // column-major palette idx

struct TextureCache {
    const Wad* wad = nullptr;
    Arena* arena = nullptr;
    const uint8_t* texture1 = nullptr;
    const uint8_t* pnames   = nullptr;
    int32_t numTextures = 0;
    int32_t numPnames   = 0;
    Texture entries[kMaxTextures];
    bool    composed[kMaxTextures];

    int find(const char name8[8]) const;
    const Texture* get(int textureIndex);
};

inline uint16_t tex_u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline int16_t  tex_s16(const uint8_t* p) { return (int16_t)tex_u16(p); }
inline uint32_t tex_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

inline bool texcache_init(TextureCache& tc, const Wad& w, Arena& arena) {
    tc.wad = &w; tc.arena = &arena;
    for (int i = 0; i < kMaxTextures; ++i) tc.composed[i] = false;
    int32_t ti = wad_find_lump(w, "TEXTURE1");
    int32_t pi = wad_find_lump(w, "PNAMES");
    if (ti < 0 || pi < 0) return false;
    tc.texture1 = wad_lump_ptr(w, ti);
    tc.pnames   = wad_lump_ptr(w, pi);
    tc.numTextures = (int32_t)tex_u32(tc.texture1);
    tc.numPnames   = (int32_t)tex_u32(tc.pnames);
    if (tc.numTextures > kMaxTextures) tc.numTextures = kMaxTextures;
    return true;
}

// maptexture_t pointer for texture index i (via the offset table after the count).
inline const uint8_t* tex_entry_ptr(const TextureCache& tc, int i) {
    uint32_t off = tex_u32(tc.texture1 + 4 + i * 4);
    return tc.texture1 + off;
}

inline int TextureCache::find(const char name8[8]) const {
    for (int i = 0; i < numTextures; ++i) {
        const uint8_t* e = tex_entry_ptr(*this, i);
        bool eq = true;
        for (int k = 0; k < 8; ++k) if ((char)e[k] != name8[k]) { eq = false; break; }
        if (eq) return i;
    }
    return -1;
}

// Compose a patch's posts into the column-major target buffer.
inline void tex_blit_patch(uint8_t* dst, int dstW, int dstH,
                           const uint8_t* patch, int originX, int originY) {
    int pw = tex_s16(patch + 0);
    for (int pc = 0; pc < pw; ++pc) {
        int tx = originX + pc;
        if (tx < 0 || tx >= dstW) continue;
        uint32_t colOff = tex_u32(patch + 8 + pc * 4);
        const uint8_t* col = patch + colOff;
        while (col[0] != 0xFF) {
            int topDelta = col[0];
            int length   = col[1];
            const uint8_t* px = col + 3;          // skip topdelta,length,pad
            for (int k = 0; k < length; ++k) {
                int ty = originY + topDelta + k;
                if (ty >= 0 && ty < dstH) dst[tx * dstH + ty] = px[k];
            }
            col += 3 + length + 1;                 // advance past pad
        }
    }
}

inline const Texture* TextureCache::get(int textureIndex) {
    if (textureIndex < 0 || textureIndex >= numTextures) return nullptr;
    if (composed[textureIndex]) return &entries[textureIndex];

    const uint8_t* e = tex_entry_ptr(*this, textureIndex);
    int w = tex_s16(e + 12);
    int h = tex_s16(e + 14);
    int patchCount = tex_s16(e + 20);
    uint8_t* buf = (uint8_t*)arena_alloc(*arena, (uint32_t)(w * h), 8);
    if (!buf) return nullptr;
    for (int i = 0; i < w * h; ++i) buf[i] = 0;   // gap sentinel

    const uint8_t* mp = e + 22;                    // first mappatch
    for (int p = 0; p < patchCount; ++p) {
        int originX = tex_s16(mp + 0);
        int originY = tex_s16(mp + 2);
        int pnameIdx = tex_s16(mp + 4);
        mp += 10;
        if (pnameIdx < 0 || pnameIdx >= numPnames) continue;
        char pn[8]; const uint8_t* pe = pnames + 4 + pnameIdx * 8;
        for (int k = 0; k < 8; ++k) pn[k] = (char)pe[k];
        int32_t li = wad_find_lump(*wad, pn);
        if (li < 0) continue;
        tex_blit_patch(buf, w, h, wad_lump_ptr(*wad, li), originX, originY);
    }
    entries[textureIndex] = Texture{ buf, (int16_t)w, (int16_t)h };
    composed[textureIndex] = true;
    return &entries[textureIndex];
}

// Affine horizontal texture coord for a column. Manhattan wall length keeps it
// libm-free and is exact for axis-aligned walls (the synthetic map's walls).
inline int wall_u(const Map& m, int32_t segIndex, float t, int texW) {
    const SegRaw& s = m.segs[segIndex];
    const VertexRaw& a = m.verts[s.v1];
    const VertexRaw& b = m.verts[s.v2];
    float dx = (float)(b.x - a.x), dy = (float)(b.y - a.y);
    float adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    float wallLen = adx + ady;
    const SidedefRaw* sd = seg_sidedef(m, segIndex);
    int xoff = sd ? sd->xoff : 0;
    int u = (int)(t * wallLen) + xoff + s.offset;
    if (texW <= 0) return 0;
    u %= texW; if (u < 0) u += texW;
    return u;
}

inline uint8_t texture_sample(const TextureCache* tex, const Map& m, int32_t segIndex,
                              int textureIndex, float t, int y, int top, int bot) {
    const Texture* T = const_cast<TextureCache*>(tex)->get(textureIndex);
    if (!T) return 0;
    int u = wall_u(m, segIndex, t, T->w);
    int v = (bot > top) ? ((y - top) * T->h) / (bot - top + 1) : 0;
    if (v < 0) v = 0; if (v >= T->h) v = T->h - 1;
    return T->texels[u * T->h + v];
}

} // namespace doom
```

- [ ] **Step 4: Run the texture test to verify it passes**

Run: `make build/host/test_texture && ./build/host/test_texture`
Expected: PASS (`[texture]`).

- [ ] **Step 5: Wire textures into the renderer**

In `plugins/games/doom/render.h`, add `#include "texture.h"` after the `#include "palette.h"` line (this replaces the `struct TextureCache;` forward declaration; delete that forward-declaration line). In `render_seg`, pass `segIndex` and the texture cache through, and select the per-texel palette index. Change the `render_seg` signature and the texel line:

Replace the `render_seg` inner texel loop body

```cpp
            for (int y = top; y <= bot; ++y)
                fb_put(fb, x, y, shade_gray(pal, cm, kFlatWallIndex, light));
```

with

```cpp
            int texId = -1;
            if (tex) {
                const SidedefRaw* sd = seg_sidedef(m, segIndex);
                if (sd) texId = tex->find(sd->middle);
            }
            for (int y = top; y <= bot; ++y) {
                uint8_t palIndex = (tex && texId >= 0)
                    ? texture_sample(tex, m, segIndex, texId, t, y, top, bot)
                    : kFlatWallIndex;
                fb_put(fb, x, y, shade_gray(pal, cm, palIndex, light));
            }
```

Add `const TextureCache* tex,` to the `render_seg` and `render_subsector` parameter lists (before `uint8_t* fb`), thread `tex` through the calls, and pass `tex` from `render_view` (remove the `(void)tex;` line). The `render_view` signature is unchanged.

- [ ] **Step 6: Update Makefile prerequisites and verify the full suite**

In `Makefile`, add `plugins/games/doom/texture.h` to the prerequisite lists of `build/host/test_doom_render` and `build/host/test_bsp_traverse`.

Run: `make test`
Expected: PASS (all binaries, including the flat-shaded `[render]` tests which still pass `nullptr` and the new `[texture]` tests).

- [ ] **Step 7: Commit**

```bash
git add plugins/games/doom/texture.h plugins/games/doom/render.h harness/tests/test_texture.cpp Makefile
git commit -m "feat(doom): TEXTURE1/PNAMES/patch composition and textured sampling (P2 unit E)"
```

---

## Task 7: Unit G - device plug-in integration and ARM verification

**Files:**

- Modify: `harness/tools/wad_build.cpp` (emit both `kDoomTestWad` and `kDoomBspTestWad`)
- Modify: `plugins/games/doom_core_spike.cpp` (load `kDoomBspTestWad`, call production `render_view`, compose textures into a static arena)
- Verify: `make arm`, `.text` size, unresolved symbols

- [ ] **Step 1: Emit both WAD arrays into the generated header**

Replace the body of `harness/tools/wad_build.cpp`'s header-emit branch (the `else`/no-arg path) so it writes both arrays. Replace the no-arg block with:

```cpp
    auto emit = [](const char* name, const char* lenName, const std::vector<uint8_t>& w) {
        printf("static const uint8_t %s[] = {\n", name);
        for (size_t i = 0; i < w.size(); ++i) {
            printf("0x%02x,", w[i]);
            if ((i % 16) == 15) printf("\n");
        }
        printf("\n};\nstatic const uint32_t %s = %u;\n", lenName, (unsigned)w.size());
    };
    printf("#pragma once\n#include <cstdint>\n");
    printf("// Generated by harness/tools/wad_build. Do not edit by hand.\n");
    emit("kDoomTestWad",    "kDoomTestWadLen",    build_test_wad());
    emit("kDoomBspTestWad", "kDoomBspTestWadLen", build_bsp_test_wad());
    return 0;
```

(Keep the `argc > 1` raw-WAD branch as-is; it still writes `build_test_wad()`. To push the BSP map to the device for the smoke test, regenerate via the new `kDoomBspTestWad` embedded array, or add a second raw-emit arg later if needed.)

Regenerate the header:

```bash
make build/host/wad_build && ./build/host/wad_build > plugins/games/doom_test_map.h
```

- [ ] **Step 2: Update the plug-in to drive the production renderer**

Replace `plugins/games/doom_core_spike.cpp`'s includes, instance struct, construct, and draw with the production path. Keep the `kSinLut` block and `cos_sin` definition unchanged. Change the includes at the top from the spike set to:

```cpp
#include <distingnt/api.h>
#include <new>
#include "doom_test_map.h"
#include "doom/wad.h"
#include "doom/geom.h"
#include "doom/palette.h"
#include "doom/texture.h"
#include "doom/render.h"
```

Replace the `_doomSpike` struct and `construct`/`draw` with:

```cpp
struct _doomSpike : public _NT_algorithm {
    doom::Wad wad;
    doom::Map map;
    doom::Palette pal;
    doom::Colormap cm;
    doom::Arena arena;
    doom::TextureCache tex;
    bool texReady = false;
    uint8_t arenaMem[256 * 1024];   // texture-composition scratch (DRAM-resident plug-in)
};
```

```cpp
_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements&, const int32_t*) {
    auto* a = new (ptrs.sram) _doomSpike();
    a->parameters = parameters;
    a->parameterPages = &parameterPages;
    doom::wad_open(kDoomBspTestWad, kDoomBspTestWadLen, a->wad);
    doom::map_load(a->wad, "E1M1", a->map);
    doom::palette_load(a->wad, a->pal);
    doom::colormap_load(a->wad, a->cm);
    doom::arena_init(a->arena, a->arenaMem, sizeof(a->arenaMem));
    a->texReady = doom::texcache_init(a->tex, a->wad, a->arena);
    return a;
}
```

```cpp
bool draw(_NT_algorithm* self) {
    auto* a = (_doomSpike*)self;
    doom::Camera cam{0.0f, 0.0f, 0.0f};   // player-start pose inside the BSP map
    const doom::TextureCache* tex = a->texReady ? &a->tex : nullptr;
    doom::render_view(a->map, cam, a->pal, a->cm, tex, NT_screen);
    return true;
}
```

Note: `_doomSpike` is large (256 KB arena member). `calculateRequirements` already sets `req.sram = sizeof(_doomSpike)`, so the firmware allocates it; per CLAUDE.md, enlarging the struct needs a device reboot before the new size takes effect. If the SRAM block is too large for the firmware's per-instance budget, move `arenaMem` to a requested DRAM region instead (a follow-up; the host path is unaffected).

- [ ] **Step 3: Build the host suite to confirm no regression**

Run: `make test`
Expected: PASS (all host binaries). The plug-in change is host-irrelevant; this confirms headers still compile together.

- [ ] **Step 4: Build ARM and verify symbols**

Run:

```bash
make arm
arm-none-eabi-nm build/arm/doom_core_spike.o | grep ' U '
```

Expected: the build succeeds; the only unresolved (` U `) symbols are firmware-resolved ones: `NT_screen`, `_GLOBAL_OFFSET_TABLE_`, and newlib `memcpy`/`memset`/`memmove`/`strlen`/`strcmp` (whichever the renderer pulls). If `memcmp`, `snprintf`, `strstr`, or a stray symbol appears, resolve it (inline byte compare, compiler-rt, or remove the call) before proceeding.

- [ ] **Step 5: Measure .text against the cap**

Run:

```bash
arm-none-eabi-readelf -W -S build/arm/doom_core_spike.o | grep ' .text '
```

Expected: the `.text` section size (column 6, hex) is well under `0x14000` (~82 KB). Record the byte value. If it approaches the cap, apply the scope-down lever: in `draw`, pass `nullptr` instead of `&a->tex` to drop to flat-shaded walls, drop the `texture.h` include path from the hot loop, rebuild, and re-measure. Document the deferral with the measured size as the blocker.

- [ ] **Step 6: Commit**

```bash
git add harness/tools/wad_build.cpp plugins/games/doom_test_map.h plugins/games/doom_core_spike.cpp
git commit -m "feat(doom): drive production BSP renderer from doom_core_spike (P2 unit G)"
```

- [ ] **Step 7: Open the PR**

Detect the default branch and open the PR referencing issue #3:

```bash
git push -u origin dr/p2-bsp-renderer
gh pr create --base main --title "P2: BSP wall renderer (traversal, occlusion, textured walls)" \
  --body "$(cat <<'EOF'
## Summary

- **What:** Replaces the single-subsector spike renderer with a real BSP front-to-back traversal, solid-seg occlusion, and perspective wall columns shaded through the P1 palette and colormap. Wall textures are composed lazily from TEXTURE1/PNAMES/patch lumps; flat-shaded sectors are the budget fallback. Implements issue #3.
- **Why:** P2 of the Doom-WAD engine DAG: render true map geometry, the foundation P3 movement and P4 things build on.
- **How:** A pure `bsp_visit_order` enumerator (depth-guarded against cyclic NODES) feeds `render_view`, which clips each projected wall span against a Doom-style solidsegs list and draws columns. A new `build_bsp_test_wad()` provides a two-subsector occlusion map; the P1 `build_test_wad()` and its parse tests are untouched.

## Test plan

- [ ] `make test` green (bsp map parse, seg helpers, traversal order, solidsegs clip, flat-shaded render pixels, texture composition).
- [ ] `make arm` clean; `doom_core_spike.o` unresolved symbols limited to the firmware-resolved set.
- [ ] `.text` under ~82 KB (record the measured size).
- [ ] On-device smoke: real E1M1 via the P1 read door renders recognizable wall geometry (post-merge, needs hardware).
EOF
)"
```

---

## Self-Review

Spec coverage:

- BSP traversal -> Task 3 (`bsp_visit_order`, `point_on_side`, depth guard, node-less fallback).
- solidsegs occlusion -> Task 4 (`SolidSegs`, `solidsegs_clip_solid`, coalesce, overflow guard).
- Perspective flat-shaded walls + depth shading -> Task 5 (`render_seg`, `light_row`, occlusion pixel test).
- TEXTURE1/PNAMES/patch composition + textured sampling -> Task 6 (`texture.h`, compose, `texture_sample`).
- seg -> sector/wall-name helpers -> Task 2.
- Synthetic two-subsector occlusion map with acyclic NODES + palette + texture lumps -> Task 1.
- Replace spike renderer; drive from BSP WAD; device read-door note -> Task 7.
- `.text` measurement and scope-down lever -> Task 5 Step 5 (deferred to Task 7) and Task 7 Step 5.

Placeholder scan: no "TBD"/"TODO"; every code step shows complete code; every command names expected output.

Type/name consistency: `render_view(m, cam, pal, cm, tex, fb)` is the single signature across Tasks 5-7. `texcache_init`, `TextureCache::find`, `TextureCache::get`, `texture_sample`, `wall_u`, `bsp_visit_order`, `point_on_side`, `solidsegs_clip_solid`, `solidsegs_clear`, `light_row`, `seg_sector`, `seg_sidedef`, `seg_is_one_sided` are used identically where referenced. `kFlatWallIndex`, `kDepthLight`, `kScreenWmax` defined once in `render.h`. Patch pixel convention (`pixel[row] = row`) is consistent between the Task 1 builder and the Task 6 composition asserts.

Known sequencing note: after Task 5 the plug-in (`doom_core_spike.cpp`) does not build until Task 7 updates its `render_view` call; `make arm` is therefore run only from Task 7. `make test` (host) stays green at every task boundary.
