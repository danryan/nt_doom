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
