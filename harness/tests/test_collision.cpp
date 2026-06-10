#include "catch.hpp"
#include "../tools/wad_build.h"
#include "../../plugins/games/doom/wad.h"
#include "../../plugins/games/doom/geom.h"
#include "../../plugins/games/doom/collision.h"

namespace {
struct RoomScene {
    std::vector<uint8_t> bytes;
    doom::Wad w;
    doom::Map m;
    doom::Blockmap bm;
    RoomScene() {
        bytes = build_move_test_wad();
        REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
        REQUIRE(doom::map_load(w, "E1M1", m));
        REQUIRE(doom::blockmap_load(w, "E1M1", bm));
    }
};
const float kR = 16.0f;   // player radius
}

TEST_CASE("open-space move is unobstructed", "[collision]") {
    RoomScene sc;
    doom::Vec2 p = doom::collide_move(sc.m, sc.bm, {256.0f, 256.0f}, 20.0f, -20.0f, kR);
    REQUIRE(p.x == Catch::Approx(276.0f));
    REQUIRE(p.y == Catch::Approx(236.0f));
}

TEST_CASE("move that breaches the radius of a wall is blocked", "[collision]") {
    RoomScene sc;
    // From clearance 40 to the left wall, a step to clearance 10 (< radius 16) is rejected.
    doom::Vec2 p = doom::collide_move(sc.m, sc.bm, {40.0f, 256.0f}, -30.0f, 0.0f, kR);
    REQUIRE(p.x == Catch::Approx(40.0f));
    REQUIRE(p.x > 0.0f);   // never passes the wall
}

TEST_CASE("move that preserves wall clearance is allowed", "[collision]") {
    RoomScene sc;
    // From clearance 40, a step to clearance 30 (> radius) is fine.
    doom::Vec2 p = doom::collide_move(sc.m, sc.bm, {40.0f, 256.0f}, -10.0f, 0.0f, kR);
    REQUIRE(p.x == Catch::Approx(30.0f));
}

TEST_CASE("blocked axis still slides along the wall on the free axis", "[collision]") {
    RoomScene sc;
    // Toward the left wall (blocked) while moving up (free): slide along x=20.
    doom::Vec2 p = doom::collide_move(sc.m, sc.bm, {20.0f, 256.0f}, -10.0f, 50.0f, kR);
    REQUIRE(p.x == Catch::Approx(20.0f));
    REQUIRE(p.y == Catch::Approx(306.0f));
}

TEST_CASE("diagonal into a corner stops on both axes", "[collision]") {
    RoomScene sc;
    doom::Vec2 p = doom::collide_move(sc.m, sc.bm, {20.0f, 20.0f}, -10.0f, -10.0f, kR);
    REQUIRE(p.x == Catch::Approx(20.0f));
    REQUIRE(p.y == Catch::Approx(20.0f));
}

TEST_CASE("a fast move never tunnels through a solid wall", "[collision]") {
    RoomScene sc;
    // Single huge step that would cross the left wall is rejected by the segment test.
    doom::Vec2 p = doom::collide_move(sc.m, sc.bm, {40.0f, 256.0f}, -1000.0f, 0.0f, kR);
    REQUIRE(p.x == Catch::Approx(40.0f));
    REQUIRE(p.x > 0.0f);   // stayed inside the room
}
