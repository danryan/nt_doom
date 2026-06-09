#include "catch.hpp"
#include "../tools/wad_build.h"
#include "../../plugins/games/doom/wad.h"
#include "../../plugins/games/doom/geom.h"

namespace {
struct MoveScene {
    std::vector<uint8_t> bytes;
    doom::Wad w;
    doom::Blockmap bm;
    MoveScene() {
        bytes = build_move_test_wad();
        REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
        REQUIRE(doom::blockmap_load(w, "E1M1", bm));
    }
};
}

TEST_CASE("blockmap header matches the generated 5x5 grid", "[blockmap]") {
    MoveScene sc;
    REQUIRE(sc.bm.originX == 0);
    REQUIRE(sc.bm.originY == 0);
    REQUIRE(sc.bm.cols == 5);
    REQUIRE(sc.bm.rows == 5);
}

TEST_CASE("blockmap maps world points to cells and rejects out-of-range", "[blockmap]") {
    MoveScene sc;
    int col = -1, row = -1;
    REQUIRE(doom::blockmap_cell_of(sc.bm, 256.0f, 256.0f, col, row));
    REQUIRE(col == 2);
    REQUIRE(row == 2);

    REQUIRE(doom::blockmap_cell_of(sc.bm, 4.0f, 256.0f, col, row));
    REQUIRE(col == 0);
    REQUIRE(row == 2);

    REQUIRE_FALSE(doom::blockmap_cell_of(sc.bm, 5000.0f, 0.0f, col, row));
    REQUIRE_FALSE(doom::blockmap_cell_of(sc.bm, -10.0f, 0.0f, col, row));
    REQUIRE_FALSE(doom::blockmap_cell_of(sc.bm, 0.0f, -10.0f, col, row));
}

TEST_CASE("interior cell lists no walls", "[blockmap]") {
    MoveScene sc;
    int count = 0;
    doom::blockmap_for_lines_in_cell(sc.bm, 2, 2, [&](int) { ++count; });
    REQUIRE(count == 0);
}

TEST_CASE("edge cell lists the adjacent wall linedef", "[blockmap]") {
    MoveScene sc;
    // Left-edge cell (col 0, row 2) overlaps the x=0 wall, which is linedef 3 (v3->v0).
    int n = 0; bool sawLeftWall = false;
    doom::blockmap_for_lines_in_cell(sc.bm, 0, 2, [&](int ln) {
        ++n;
        if (ln == 3) sawLeftWall = true;
    });
    REQUIRE(n >= 1);
    REQUIRE(sawLeftWall);
}
