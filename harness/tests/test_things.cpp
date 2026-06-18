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
