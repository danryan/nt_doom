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
    std::vector<uint8_t> bytes = build_things_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    std::vector<uint8_t> mem(1 << 20);
    doom::Arena a; doom::arena_init(a, mem.data(), (uint32_t)mem.size());
    doom::SpriteCache sc; REQUIRE(doom::spritecache_init(sc, w, a));

    bool flip = false;
    int liA2 = doom::sprite_find(sc, "POSS", 'A', 2, flip); REQUIRE(liA2 >= 0); REQUIRE(flip == false);
    int liA8 = doom::sprite_find(sc, "POSS", 'A', 8, flip); REQUIRE(liA8 == liA2); REQUIRE(flip == true);
}
