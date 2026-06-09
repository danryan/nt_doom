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

namespace {
// Read the WAD header's little-endian dirOffset (bytes 8..11).
uint32_t dir_offset(const std::vector<uint8_t>& b) {
    return (uint32_t)b[8] | ((uint32_t)b[9] << 8) | ((uint32_t)b[10] << 16) | ((uint32_t)b[11] << 24);
}
void put_le32(std::vector<uint8_t>& b, uint32_t pos, uint32_t v) {
    b[pos] = (uint8_t)v; b[pos + 1] = (uint8_t)(v >> 8);
    b[pos + 2] = (uint8_t)(v >> 16); b[pos + 3] = (uint8_t)(v >> 24);
}
}

// A corrupt or partially-read WAD whose header and directory location are valid but whose
// first lump entry has a garbage filePos must be rejected, not accepted (which would make
// wad_lump_ptr return a wild pointer and the renderer hard-fault on device).
TEST_CASE("wad_open rejects a directory entry pointing past the data", "[wad]") {
    std::vector<uint8_t> bytes = build_test_wad();
    doom::Wad ok;
    REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), ok));   // baseline valid

    // Corrupt the first directory entry's filePos to far beyond the data.
    uint32_t dirPos = dir_offset(bytes);
    put_le32(bytes, dirPos + 0, 0x00FFFFFFu);   // filePos way past end
    doom::Wad bad;
    REQUIRE_FALSE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), bad));
}

// A negative filePos or size must also be rejected.
TEST_CASE("wad_open rejects a negative lump offset", "[wad]") {
    std::vector<uint8_t> bytes = build_test_wad();
    uint32_t dirPos = dir_offset(bytes);
    put_le32(bytes, dirPos + 0, 0x80000000u);   // negative filePos
    doom::Wad bad;
    REQUIRE_FALSE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), bad));
}
