#include "catch.hpp"
#include "../tools/wad_build.h"
#include "../../plugins/games/doom/wad.h"
#include "../../plugins/games/doom/geom.h"
#include "../../plugins/games/doom/render.h"
#include <cmath>

namespace doom { void cos_sin(float a, float& c, float& s) { c = std::cos(a); s = std::sin(a); } }

TEST_CASE("sprite_column_visible is a strict nearer-than test", "[depth]") {
    REQUIRE(doom::sprite_column_visible(200.0f, 300.0f) == true);    // sprite nearer than wall
    REQUIRE(doom::sprite_column_visible(200.0f, 100.0f) == false);   // wall in front
    REQUIRE(doom::sprite_column_visible(200.0f, doom::kFarDepth) == true);  // no wall
}

TEST_CASE("render_view fills the depth buffer with the near wall depth", "[depth]") {
    std::vector<uint8_t> bytes = build_bsp_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    doom::Map m; REQUIRE(doom::map_load(w, "E1M1", m));
    doom::Palette pal; doom::palette_load(w, pal);
    doom::Colormap cm; doom::colormap_load(w, cm);

    uint8_t fb[128 * 64];
    float depth[doom::kScreenW];
    doom::Camera cam{0.0f, 0.0f, 0.0f};        // at origin, facing +x; near wall x=100
    doom::render_view(m, cam, pal, cm, nullptr, fb, depth);

    // Central column 128 is covered by the near wall (x~100), so its depth is ~100,
    // strictly less than the far wall (~300) and far below kFarDepth.
    REQUIRE(depth[128] < 150.0f);
    REQUIRE(depth[128] > 50.0f);
    // A column with no wall (far left edge, beyond both walls' projection) stays far.
    REQUIRE(depth[0] == Catch::Approx(doom::kFarDepth));
}
