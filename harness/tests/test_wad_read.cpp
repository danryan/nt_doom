#include "catch.hpp"
#include "../tools/wad_build.h"
#include "../tools/wav_wrap.h"
#include "../../plugins/games/doom/wad.h"
#include "../../plugins/games/doom/geom.h"
#include "../../plugins/games/doom/arena.h"
#include "../../plugins/games/doom/wad_read.h"
#include <vector>

namespace {
// Build the synthetic WAD and its 16-bit-WAV smuggle once per test.
struct Fixture {
    std::vector<uint8_t> wad;
    std::vector<uint8_t> wav;
    Fixture() : wad(build_test_wad()), wav(wrap_wav_16bit_mono(wad, 44100)) {}
};
}

TEST_CASE("WavWadSource validates a 16-bit mono WAV and recovers the WAD length", "[wad_read]") {
    Fixture f;
    doom::WavWadSource src;
    REQUIRE(doom::wav_wad_source_init(src, f.wav.data(), (uint32_t)f.wav.size()));
    // Payload is the WAD, odd-padded to a whole 16-bit sample.
    REQUIRE(src.wadLen >= f.wad.size());
    REQUIRE(src.wadLen - f.wad.size() <= 1);

    // A buffer too small to hold a header is rejected.
    doom::WavWadSource bad;
    REQUIRE_FALSE(doom::wav_wad_source_init(bad, f.wav.data(), 10));
}

TEST_CASE("wav_wad_read returns WAD bytes byte-exact at any offset", "[wad_read]") {
    Fixture f;
    doom::WavWadSource src;
    REQUIRE(doom::wav_wad_source_init(src, f.wav.data(), (uint32_t)f.wav.size()));

    // Offset 0: the IWAD magic.
    uint8_t head[4];
    REQUIRE(doom::wav_wad_read(src, head, 0, 4));
    REQUIRE(head[0] == 'I'); REQUIRE(head[1] == 'W');
    REQUIRE(head[2] == 'A'); REQUIRE(head[3] == 'D');

    // A deep window, compared against the raw WAD.
    const uint32_t off = 100, n = 64;
    std::vector<uint8_t> got(n);
    REQUIRE(doom::wav_wad_read(src, got.data(), off, n));
    for (uint32_t i = 0; i < n; ++i) REQUIRE(got[i] == f.wad[off + i]);

    // A window spanning an odd offset and odd length (crosses frame boundaries).
    const uint32_t off2 = 101, n2 = 33;
    std::vector<uint8_t> got2(n2);
    REQUIRE(doom::wav_wad_read(src, got2.data(), off2, n2));
    for (uint32_t i = 0; i < n2; ++i) REQUIRE(got2[i] == f.wad[off2 + i]);

    // The final real WAD byte.
    uint8_t last;
    REQUIRE(doom::wav_wad_read(src, &last, (uint32_t)f.wad.size() - 1, 1));
    REQUIRE(last == f.wad.back());

    // Out of range is rejected.
    uint8_t over;
    REQUIRE_FALSE(doom::wav_wad_read(src, &over, src.wadLen, 1));
    REQUIRE_FALSE(doom::wav_wad_read(src, &over, 0, src.wadLen + 1));
}

TEST_CASE("wad_load_all reads the whole WAD into the arena and parses identically", "[wad_read]") {
    Fixture f;
    doom::WavWadSource src;
    REQUIRE(doom::wav_wad_source_init(src, f.wav.data(), (uint32_t)f.wav.size()));

    std::vector<uint8_t> backing(src.wadLen + 64);
    doom::Arena arena;
    doom::arena_init(arena, backing.data(), (uint32_t)backing.size());

    const uint8_t* loaded = nullptr;
    uint32_t loadedLen = 0;
    REQUIRE(doom::wad_load_all(src, arena, &loaded, &loadedLen));
    REQUIRE(loaded != nullptr);
    REQUIRE(loadedLen == src.wadLen);

    // The loaded copy opens and yields the same map as parsing the raw WAD.
    doom::Wad wLoaded;
    REQUIRE(doom::wad_open(loaded, loadedLen, wLoaded));
    doom::Wad wDirect;
    REQUIRE(doom::wad_open(f.wad.data(), (uint32_t)f.wad.size(), wDirect));
    REQUIRE(wLoaded.numLumps == wDirect.numLumps);

    doom::Map mLoaded, mDirect;
    REQUIRE(doom::map_load(wLoaded, "E1M1", mLoaded));
    REQUIRE(doom::map_load(wDirect, "E1M1", mDirect));
    REQUIRE(mLoaded.numVerts   == mDirect.numVerts);
    REQUIRE(mLoaded.numSegs    == mDirect.numSegs);
    REQUIRE(mLoaded.numLines   == mDirect.numLines);
    REQUIRE(mLoaded.numSectors == mDirect.numSectors);
    // Vertex bytes match.
    for (int32_t i = 0; i < mDirect.numVerts; ++i) {
        REQUIRE(mLoaded.verts[i].x == mDirect.verts[i].x);
        REQUIRE(mLoaded.verts[i].y == mDirect.verts[i].y);
    }
}
