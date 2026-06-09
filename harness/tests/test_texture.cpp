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
