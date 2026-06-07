#include "catch.hpp"
#include "../tools/wad_build.h"
#include "../../plugins/games/doom/wad.h"
#include "../../plugins/games/doom/geom.h"

TEST_CASE("NodeRaw is the 28-byte Doom NODES record", "[nodes]") {
    REQUIRE(sizeof(doom::NodeRaw) == 28);
}

TEST_CASE("map_load exposes a typed NODES view", "[nodes]") {
    std::vector<uint8_t> bytes = build_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    doom::Map m; REQUIRE(doom::map_load(w, "E1M1", m));

    REQUIRE(m.numNodes == 1);
    REQUIRE(m.nodes != nullptr);

    const doom::NodeRaw& n = m.nodes[0];
    REQUIRE(n.x  == 10);
    REQUIRE(n.y  == 20);
    REQUIRE(n.dx == 30);
    REQUIRE(n.dy == 40);
    REQUIRE(n.bbox[0][0] == 100);   // right child: top
    REQUIRE(n.bbox[0][1] == -100);  // bottom
    REQUIRE(n.bbox[1][2] == -60);   // left child: left
    REQUIRE(n.bbox[1][3] == 60);    // right
}

TEST_CASE("node child high bit decodes subsector vs node", "[nodes]") {
    std::vector<uint8_t> bytes = build_test_wad();
    doom::Wad w; REQUIRE(doom::wad_open(bytes.data(), (uint32_t)bytes.size(), w));
    doom::Map m; REQUIRE(doom::map_load(w, "E1M1", m));
    const doom::NodeRaw& n = m.nodes[0];

    // child[0] = 0x8000: a subsector leaf, index 0.
    REQUIRE(doom::node_child_is_subsector(n.child[0]));
    REQUIRE(doom::node_child_index(n.child[0]) == 0);
    // child[1] = 0x0000: an interior node, index 0.
    REQUIRE_FALSE(doom::node_child_is_subsector(n.child[1]));
    REQUIRE(doom::node_child_index(n.child[1]) == 0);
}
