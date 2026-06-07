#include "catch.hpp"
#include "../tools/wad_build.h"
#include "../../plugins/games/doom/wad.h"
#include "../../plugins/games/doom/palette.h"

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

TEST_CASE("luma4 quantizes RGB to 4-bit gray via Rec.601 weights", "[palette]") {
    REQUIRE(doom::luma4(0, 0, 0)       == 0);    // black
    REQUIRE(doom::luma4(255, 255, 255) == 15);   // white -> max gray
    REQUIRE(doom::luma4(255, 0, 0)     == 4);    // pure red:   77*255>>8 = 76, >>4 = 4
    REQUIRE(doom::luma4(0, 255, 0)     == 9);    // pure green: 150*255>>8 = 149, >>4 = 9
    REQUIRE(doom::luma4(0, 0, 255)     == 1);    // pure blue:  29*255>>8 = 28, >>4 = 1
}

TEST_CASE("palette_load fills the 4-bit gray table from PLAYPAL", "[palette]") {
    std::vector<uint8_t> bytes = build_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));

    doom::Palette pal;
    REQUIRE(doom::palette_load(w, pal));
    // Fixture palette is the gray ramp (i,i,i), so gray[i] == i >> 4.
    REQUIRE(pal.gray[0]   == 0);
    REQUIRE(pal.gray[15]  == 0);
    REQUIRE(pal.gray[16]  == 1);
    REQUIRE(pal.gray[255] == 15);
}

TEST_CASE("colormap_load views the 34x256 light maps", "[palette]") {
    std::vector<uint8_t> bytes = build_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));

    doom::Colormap cm;
    REQUIRE(doom::colormap_load(w, cm));
    REQUIRE(cm.numMaps == 34);
    // Map 0 is identity: colormap[0][i] == i.
    REQUIRE(cm.maps[0]   == 0);
    REQUIRE(cm.maps[200] == 200);
    // Map 33 is all-zero (darkest).
    REQUIRE(cm.maps[33 * 256 + 200] == 0);
}

TEST_CASE("shade_gray combines colormap light and palette gray", "[palette]") {
    std::vector<uint8_t> bytes = build_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    doom::Palette pal;  REQUIRE(doom::palette_load(w, pal));
    doom::Colormap cm;  REQUIRE(doom::colormap_load(w, cm));

    // Light 0 (identity map) at index 255: full gray.
    REQUIRE(doom::shade_gray(pal, cm, 255, 0) == 15);
    // A darker light remaps to a lower palette index, hence lower gray.
    REQUIRE(doom::shade_gray(pal, cm, 255, 16) < 15);
    // Darkest light maps everything to index 0 -> gray 0.
    REQUIRE(doom::shade_gray(pal, cm, 255, 33) == 0);
    // Out-of-range light clamps into [0, numMaps).
    REQUIRE(doom::shade_gray(pal, cm, 255, 999) == doom::shade_gray(pal, cm, 255, 33));
    REQUIRE(doom::shade_gray(pal, cm, 255, -5)  == doom::shade_gray(pal, cm, 255, 0));
}
