#include "catch.hpp"
#include "../tools/wad_build.h"
#include "../../plugins/games/doom/wad.h"
#include "../../plugins/games/doom/geom.h"
#include "../../plugins/games/doom/palette.h"

TEST_CASE("build_bsp_test_wad is a valid IWAD with E1M1", "[bspmap]") {
    std::vector<uint8_t> bytes = build_bsp_test_wad();
    doom::Wad w;
    REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    doom::Map m;
    REQUIRE(doom::map_load(w, "E1M1", m));
}

TEST_CASE("BSP map has two subsectors split by one acyclic node", "[bspmap]") {
    std::vector<uint8_t> bytes = build_bsp_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    doom::Map m; REQUIRE(doom::map_load(w, "E1M1", m));

    REQUIRE(m.numSsecs == 2);
    REQUIRE(m.numNodes == 1);
    REQUIRE(m.numSegs == 2);

    const doom::NodeRaw& n = m.nodes[0];
    REQUIRE(n.x == 200); REQUIRE(n.y == 0);
    REQUIRE(n.dx == 0);  REQUIRE(n.dy == 100);
    // child[1] = near subsector (ssec0); child[0] = far subsector (ssec1).
    REQUIRE(doom::node_child_is_subsector(n.child[0]));
    REQUIRE(doom::node_child_is_subsector(n.child[1]));
    REQUIRE(doom::node_child_index(n.child[1]) == 0);   // near
    REQUIRE(doom::node_child_index(n.child[0]) == 1);   // far
}

TEST_CASE("BSP map carries PLAYPAL, COLORMAP, and texture lumps", "[bspmap]") {
    std::vector<uint8_t> bytes = build_bsp_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));

    doom::Palette pal; REQUIRE(doom::palette_load(w, pal));
    doom::Colormap cm; REQUIRE(doom::colormap_load(w, cm));
    REQUIRE(cm.numMaps == 34);

    REQUIRE(doom::wad_find_lump(w, "PNAMES")   >= 0);
    REQUIRE(doom::wad_find_lump(w, "TEXTURE1") >= 0);
    REQUIRE(doom::wad_find_lump(w, "PWALL")    >= 0);
}

TEST_CASE("seg_sector resolves seg -> linedef -> sidedef -> sector", "[segres]") {
    std::vector<uint8_t> bytes = build_bsp_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    doom::Map m; REQUIRE(doom::map_load(w, "E1M1", m));

    const doom::SectorRaw* near = doom::seg_sector(m, 0);   // seg0 -> sector0
    const doom::SectorRaw* far  = doom::seg_sector(m, 1);   // seg1 -> sector1
    REQUIRE(near != nullptr);
    REQUIRE(far  != nullptr);
    REQUIRE(near->light == 224);
    REQUIRE(far->light  == 160);
}

TEST_CASE("seg_is_one_sided is true for back == 0xFFFF", "[segres]") {
    std::vector<uint8_t> bytes = build_bsp_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    doom::Map m; REQUIRE(doom::map_load(w, "E1M1", m));
    REQUIRE(doom::seg_is_one_sided(m, 0));
    REQUIRE(doom::seg_is_one_sided(m, 1));
}
