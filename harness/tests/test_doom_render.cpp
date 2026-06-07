#include "catch.hpp"
#include "../tools/wad_build.h"
#include "../../plugins/games/doom/wad.h"
#include "../../plugins/games/doom/geom.h"
#include "../../plugins/games/doom/render.h"
#include <cmath>

namespace doom { void cos_sin(float a, float& c, float& s) { c = std::cos(a); s = std::sin(a); } }

TEST_CASE("render_view draws walls from inside the square room", "[render]") {
    std::vector<uint8_t> bytes = build_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    doom::Map map; REQUIRE(doom::map_load(w, "E1M1", map));
    REQUIRE(map.numSegs == 4);

    uint8_t fb[128 * 64];
    doom::Camera cam{128.0f, 128.0f, 0.0f};   // center, facing +x
    doom::render_view(map, cam, fb);

    // The center column must contain at least one lit (non-zero) pixel: a wall.
    int lit = 0;
    for (int y = 0; y < 64; ++y)
        if (doom::fb_get(fb, 128, y) != 0) ++lit;
    REQUIRE(lit > 0);
}
