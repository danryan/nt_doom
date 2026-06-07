#include "catch.hpp"
#include "../tools/wad_build.h"
#include "../../plugins/games/doom/wad.h"

TEST_CASE("synthetic WAD carries PLAYPAL and COLORMAP lumps", "[palette]") {
    std::vector<uint8_t> bytes = build_test_wad();
    doom::Wad w;
    REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));

    int32_t pp = doom::wad_find_lump(w, "PLAYPAL");
    REQUIRE(pp >= 0);
    REQUIRE(doom::wad_lump_size(w, pp) == 768);   // one 256-entry RGB palette

    int32_t cm = doom::wad_find_lump(w, "COLORMAP");
    REQUIRE(cm >= 0);
    REQUIRE(doom::wad_lump_size(w, cm) == 34 * 256);
}
