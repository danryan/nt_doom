#include "catch.hpp"
#include "../../plugins/games/doom/arena.h"
#include <cstdint>

TEST_CASE("arena bump-allocates with alignment", "[arena]") {
    alignas(8) uint8_t backing[256];
    doom::Arena a;
    doom::arena_init(a, backing, sizeof(backing));
    REQUIRE(a.used == 0);

    uint8_t* p1 = (uint8_t*)doom::arena_alloc(a, 3, 1);   // 3 bytes, byte-aligned
    REQUIRE(p1 == backing);
    REQUIRE(a.used == 3);

    uint8_t* p2 = (uint8_t*)doom::arena_alloc(a, 4, 8);   // aligns 3 -> 8 first
    REQUIRE(p2 == backing + 8);
    REQUIRE(a.used == 12);
}

TEST_CASE("arena overflow returns nullptr and preserves used", "[arena]") {
    alignas(8) uint8_t backing[16];
    doom::Arena a;
    doom::arena_init(a, backing, sizeof(backing));
    REQUIRE(doom::arena_alloc(a, 16, 1) == backing);
    REQUIRE(a.used == 16);

    void* over = doom::arena_alloc(a, 1, 1);              // no room
    REQUIRE(over == nullptr);
    REQUIRE(a.used == 16);                                // unchanged
}

TEST_CASE("arena alignment padding can itself overflow", "[arena]") {
    alignas(8) uint8_t backing[16];
    doom::Arena a;
    doom::arena_init(a, backing, sizeof(backing));
    REQUIRE(doom::arena_alloc(a, 12, 1) != nullptr);      // used = 12
    void* over = doom::arena_alloc(a, 1, 8);              // aligns 12 -> 16, +1 overruns
    REQUIRE(over == nullptr);
    REQUIRE(a.used == 12);
}

namespace { struct Widget { int a; int b; }; }

TEST_CASE("arena_new placement-constructs and reset reuses space", "[arena]") {
    alignas(8) uint8_t backing[64];
    doom::Arena a;
    doom::arena_init(a, backing, sizeof(backing));

    Widget* w = doom::arena_new<Widget>(a);
    REQUIRE(w != nullptr);
    REQUIRE((uint8_t*)w == backing);                      // aligned at start
    REQUIRE(a.used == sizeof(Widget));
    w->a = 7; w->b = 9;
    REQUIRE(w->a == 7);

    doom::arena_reset(a);
    REQUIRE(a.used == 0);
    Widget* w2 = doom::arena_new<Widget>(a);
    REQUIRE((uint8_t*)w2 == backing);                     // space reused
}
