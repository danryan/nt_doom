// Host probe: run the full device WAD-load path on a REAL (uncommitted) DOOM1.WAD,
// under AddressSanitizer, to find on the host what hard-faulted on the device.
//
// Usage:  real_wad_probe <path-to-doom1.wad>   (or set NT_DOOM_WAD)
// Build with -fsanitize=address -g -O0 so a wild pointer reports file:line.
//
// Never commit a real WAD. This tool reads a local path the caller supplies.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <cstdint>

#include "../../plugins/games/doom/arena.h"
#include "../../plugins/games/doom/wad.h"
#include "../../plugins/games/doom/wad_read.h"
#include "../../plugins/games/doom/geom.h"
#include "../../plugins/games/doom/palette.h"
#include "../../plugins/games/doom/texture.h"
#include "../../plugins/games/doom/render.h"
#include "../../plugins/games/doom/collision.h"
#include "../../plugins/games/doom/fb.h"
#include "wav_wrap.h"

namespace doom { void cos_sin(float a, float& c, float& s) { c = std::cos(a); s = std::sin(a); } }

static std::vector<uint8_t> read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> b((size_t)n);
    if (fread(b.data(), 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "short read\n"); exit(2); }
    fclose(f);
    return b;
}

// Run map_load, compose every texture, render a frame, and a few collision moves.
static void exercise(const uint8_t* bytes, uint32_t len, const char* label) {
    printf("\n=== %s (len=%u) ===\n", label, len);
    doom::Wad w;
    if (!doom::wad_open(bytes, len, w)) { printf("  wad_open FAILED (graceful)\n"); return; }
    printf("  wad_open OK numLumps=%d\n", w.numLumps);

    doom::Map m;
    if (!doom::map_load(w, "E1M1", m)) { printf("  map_load FAILED (graceful)\n"); return; }
    printf("  map_load OK verts=%d segs=%d ssecs=%d sectors=%d lines=%d sides=%d nodes=%d\n",
           m.numVerts, m.numSegs, m.numSsecs, m.numSectors, m.numLines, m.numSides, m.numNodes);

    doom::Palette pal; bool pok = doom::palette_load(w, pal);
    doom::Colormap cm; bool cok = doom::colormap_load(w, cm);
    printf("  palette=%d colormap=%d (maps=%d)\n", pok, cok, cm.numMaps);

    static uint8_t arenaMem[16 * 1024 * 1024];
    doom::Arena arena; doom::arena_init(arena, arenaMem, sizeof(arenaMem));
    doom::TextureCache tex; bool tok = doom::texcache_init(tex, w, arena);
    printf("  texcache_init=%d numTextures=%d numPnames=%d\n", tok, tex.numTextures, tex.numPnames);

    int composed = 0, nullc = 0;
    for (int i = 0; i < tex.numTextures; ++i) {
        const doom::Texture* t = tex.get(i);   // composes patches: the suspect path
        if (t) ++composed; else ++nullc;
    }
    printf("  composed %d textures (%d null)\n", composed, nullc);

    doom::Blockmap bm; bool bmok = doom::blockmap_load(w, "E1M1", bm);
    printf("  blockmap=%d origin=(%d,%d) cols=%d rows=%d words=%u\n",
           bmok, bm.originX, bm.originY, bm.cols, bm.rows, bm.words);

    static uint8_t fb[128 * 64];
    doom::Camera cam{ 1056.0f, -3616.0f, 1.5707963f };
    doom::render_view(m, cam, pal, cm, tok ? &tex : nullptr, fb);
    int lit = 0; for (int x = 0; x < 256; ++x) for (int y = 0; y < 64; ++y) if (doom::fb_get(fb, x, y)) ++lit;
    printf("  render_view OK lit-pixels=%d\n", lit);

    if (bmok) {
        doom::Vec2 p{ cam.x, cam.y };
        for (int i = 0; i < 16; ++i)
            p = doom::collide_move(m, bm, p, 8.0f, 4.0f, 16.0f);
        printf("  collide_move OK final=(%.1f,%.1f)\n", p.x, p.y);
    }
    printf("  PASS\n");
}

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : getenv("NT_DOOM_WAD");
    if (!path) { fprintf(stderr, "usage: real_wad_probe <doom1.wad>  (or NT_DOOM_WAD)\n"); return 2; }

    std::vector<uint8_t> wad = read_file(path);
    printf("loaded %s (%zu bytes)\n", path, wad.size());

    // Test 1: full parse + compose + render + collide on the real WAD.
    exercise(wad.data(), (uint32_t)wad.size(), "FULL real WAD");

    // Test 2: WAV smuggle round-trip (wrap -> read-door extract -> compare).
    std::vector<uint8_t> wav = wrap_wav_16bit_mono(wad, 48000);
    printf("\n=== WAV round-trip (wav=%zu bytes) ===\n", wav.size());
    doom::WavWadSource s;
    bool sinit = doom::wav_wad_source_init(s, wav.data(), (uint32_t)wav.size());
    printf("  source_init=%d wadLen=%u (orig=%zu)\n", sinit, s.wadLen, wad.size());
    std::vector<uint8_t> back(s.wadLen);
    bool rok = doom::wav_wad_read(s, back.data(), 0, s.wadLen);
    bool exact = rok && s.wadLen == wad.size() && memcmp(back.data(), wad.data(), wad.size()) == 0;
    printf("  read=%d byte-exact=%d\n", rok, exact);
    if (exact) exercise(back.data(), (uint32_t)back.size(), "round-tripped WAD");

    // Test 4: replicate the DEVICE arena layout exactly: one 8 MB arena holding the WAD
    // bytes (reserved via arena_alloc) plus all composed textures after it.
    printf("\n=== DEVICE arena layout (8 MB: WAD + textures) ===\n");
    {
        static uint8_t dram[8 * 1024 * 1024];
        doom::Arena arena; doom::arena_init(arena, dram, sizeof(dram));
        uint8_t* wbytes = (uint8_t*)doom::arena_alloc(arena, (uint32_t)wad.size(), 8);
        if (!wbytes) { printf("  arena_alloc(WAD) failed\n"); }
        else {
            memcpy(wbytes, wad.data(), wad.size());
            doom::Wad w2;
            if (!doom::wad_open(wbytes, (uint32_t)wad.size(), w2)) { printf("  wad_open FAILED\n"); }
            else {
                doom::TextureCache tex; doom::texcache_init(tex, w2, arena);
                int composed = 0, nullc = 0;
                for (int i = 0; i < tex.numTextures; ++i) { if (tex.get(i)) ++composed; else ++nullc; }
                printf("  composed %d (%d null) arena.used=%u of %u (%.1f%%)\n",
                       composed, nullc, arena.used, arena.cap, 100.0 * arena.used / arena.cap);
            }
        }
    }

    // Test 3: simulate a device-truncated sample (directory unread) and confirm the
    // parser does NOT wild-pointer. Truncate to ~2 MB (below the ~4.18 MB directory).
    uint32_t trunc = (uint32_t)(wad.size() / 2);
    exercise(wad.data(), trunc, "TRUNCATED to half (device-cap simulation)");

    printf("\nALL TESTS COMPLETE\n");
    return 0;
}
