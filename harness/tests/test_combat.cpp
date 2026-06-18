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
    doom::Blockmap bm; doom::blockmap_load(w, "E1M1", bm);   // absent in this WAD; bm.base stays null

    // Aim from origin along +x; the barrel at (200,0) is the only drawable thing ahead.
    int hit = doom::hitscan_nearest(m, bm, 0.0f, 0.0f, 1.0f, 0.0f, 2000.0f, 32.0f);
    REQUIRE(hit >= 0);
    REQUIRE(m.things[hit].type == 2035);
    // Aiming away (-x) hits nothing.
    REQUIRE(doom::hitscan_nearest(m, bm, 0.0f, 0.0f, -1.0f, 0.0f, 2000.0f, 32.0f) == -1);
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
