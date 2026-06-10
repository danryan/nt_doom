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

// render_things resolves drawable types via combat.h's thing_sprite_name (2035 -> BAR1).

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
        doom::render_things(m, cam, pal, cm, &sc, depth, fb, order, 64);
        // The barrel sprite center is column 128; at least one lit pixel appears near it.
        bool lit = false;
        for (int y = 28; y <= 36 && !lit; ++y)
            for (int x = 125; x <= 131 && !lit; ++x) if (doom::fb_get(fb, x, y) != 0) lit = true;
        REQUIRE(lit);
    }
    SECTION("sprite occluded when a nearer wall covers its columns") {
        for (int i = 0; i < doom::kScreenW; ++i) depth[i] = 100.0f;   // wall at depth 100 < 200
        doom::fb_clear(fb);
        doom::render_things(m, cam, pal, cm, &sc, depth, fb, order, 64);
        bool lit = false;
        for (int y = 0; y < 64 && !lit; ++y)
            for (int x = 120; x <= 136 && !lit; ++x) if (doom::fb_get(fb, x, y) != 0) lit = true;
        REQUIRE(lit == false);                  // fully occluded
    }
}
