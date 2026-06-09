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
