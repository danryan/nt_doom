#include "catch.hpp"
#include "../tools/wad_build.h"
#include "../../plugins/games/doom/wad.h"
#include <cstring>

TEST_CASE("synthetic test WAD parses with expected lumps", "[wad]") {
    std::vector<uint8_t> bytes = build_test_wad();
    doom::Wad w;
    REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    REQUIRE(w.numLumps >= 9);

    int32_t m = doom::wad_find_lump(w, "E1M1");
    REQUIRE(m >= 0);
    REQUIRE(doom::wad_find_lump(w, "VERTEXES", m) > m);
    REQUIRE(doom::wad_find_lump(w, "LINEDEFS", m) > m);
    REQUIRE(doom::wad_find_lump(w, "SECTORS", m) > m);

    int32_t v = doom::wad_find_lump(w, "VERTEXES", m);
    REQUIRE(doom::wad_lump_size(w, v) == 4 * 4);   // 4 vertices, 4 bytes each
}
