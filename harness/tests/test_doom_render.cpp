#include "catch.hpp"
#include "../tools/wad_build.h"
#include "../../plugins/games/doom/wad.h"
#include "../../plugins/games/doom/geom.h"
#include "../../plugins/games/doom/palette.h"
#include "../../plugins/games/doom/render.h"
#include "../../plugins/games/doom/fb.h"
#include <cmath>

namespace doom { void cos_sin(float a, float& c, float& s) { c = std::cos(a); s = std::sin(a); } }

namespace {
struct Scene {
    std::vector<uint8_t> bytes; doom::Wad w; doom::Map m;
    doom::Palette pal; doom::Colormap cm;
    Scene() {
        bytes = build_bsp_test_wad();
        REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
        REQUIRE(doom::map_load(w, "E1M1", m));
        REQUIRE(doom::palette_load(w, pal));
        REQUIRE(doom::colormap_load(w, cm));
    }
};
int column_lit_count(const uint8_t* fb, int x) {
    int lit = 0; for (int y = 0; y < 64; ++y) if (doom::fb_get(fb, x, y) != 0) ++lit;
    return lit;
}
}

TEST_CASE("flat render draws walls in the projected columns", "[render]") {
    Scene sc; uint8_t fb[128 * 64];
    doom::Camera cam{0.0f, 0.0f, 0.0f};
    doom::render_view(sc.m, cam, sc.pal, sc.cm, nullptr, fb);

    REQUIRE(column_lit_count(fb, 128) > 0);   // centre: both walls project here
    REQUIRE(column_lit_count(fb, 60)  > 0);   // far-only left gap [42,75]
    REQUIRE(column_lit_count(fb, 20) == 0);   // outside both spans: unlit
}

TEST_CASE("near wall occludes far wall in the overlap", "[render]") {
    Scene sc; uint8_t fb[128 * 64];
    doom::Camera cam{0.0f, 0.0f, 0.0f};
    doom::render_view(sc.m, cam, sc.pal, sc.cm, nullptr, fb);

    auto first_lit = [&](int x){ for (int y = 0; y < 64; ++y) if (doom::fb_get(fb, x, y)) return doom::fb_get(fb, x, y); return (uint8_t)0; };
    uint8_t overlapShade = first_lit(128);   // near owns this column
    uint8_t farShade     = first_lit(60);    // far-only column
    REQUIRE(overlapShade != 0);
    REQUIRE(farShade != 0);
    // Near sector is brighter and nearer, so its shade is brighter than the far wall's.
    REQUIRE(overlapShade > farShade);
}
