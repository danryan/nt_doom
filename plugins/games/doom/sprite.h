#pragma once
#include <cstdint>
#include "wad.h"
#include "arena.h"
#include "texture.h"   // tex_s16, tex_blit_patch (the shared Doom patch post decoder)

namespace doom {

static const int     kMaxSprites = 128;       // distinct sprite lumps composed (E1M1 headroom)
static const uint8_t kSpriteGap  = 0xFF;      // transparent sentinel in the composed buffer

// Column-major palette indices; kSpriteGap marks transparent texels. left/top are the
// Doom patch offsets (unused for the floor-less P4 billboard, retained for P5).
struct Sprite { const uint8_t* texels; int16_t w, h, left, top; };

struct SpriteCache {
    const Wad* wad = nullptr;
    Arena*     arena = nullptr;
    int32_t    sStart = -1, sEnd = -1;        // marker lump indices (exclusive interior)
    mutable Sprite  entries[kMaxSprites];
    mutable bool    composed[kMaxSprites];
    mutable int32_t lumpOf[kMaxSprites];      // wad lump index each entry caches
    mutable int32_t count = 0;
};

inline bool spritecache_init(SpriteCache& sc, const Wad& w, Arena& a) {
    sc.wad = &w; sc.arena = &a; sc.count = 0;
    for (int i = 0; i < kMaxSprites; ++i) { sc.composed[i] = false; sc.lumpOf[i] = -1; }
    sc.sStart = wad_find_lump(w, "S_START");
    sc.sEnd   = wad_find_lump(w, "S_END");
    return sc.sStart >= 0 && sc.sEnd > sc.sStart;
}

// Find the lump for (name4, frame, rotation). Decodes pair 1 (name[0..3], name[4],
// name[5]) and the optional mirror pair 2 (name[6], name[7]); sets flip when matched via
// the mirror pair. Returns the lump index or -1.
inline int sprite_find(const SpriteCache& sc, const char name4[4], char frame, int rotation, bool& flip) {
    for (int32_t i = sc.sStart + 1; i < sc.sEnd; ++i) {
        const char* nm = sc.wad->dir[i].name;
        if (!(nm[0] == name4[0] && nm[1] == name4[1] && nm[2] == name4[2] && nm[3] == name4[3])) continue;
        if (nm[4] == frame && (nm[5] - '0') == rotation) { flip = false; return (int)i; }
        if (nm[6] != 0 && nm[6] == frame && (nm[7] - '0') == rotation) { flip = true; return (int)i; }
    }
    return -1;
}

// Compose the sprite patch lump column-major into the arena, initialized to kSpriteGap
// (transparent), with the dimensions read from the patch header. Memoized by lump index.
inline const Sprite* sprite_get(const SpriteCache& sc, int lumpIndex) {
    if (lumpIndex <= sc.sStart || lumpIndex >= sc.sEnd) return nullptr;
    for (int i = 0; i < sc.count; ++i)
        if (sc.lumpOf[i] == lumpIndex) return &sc.entries[i];
    if (sc.count >= kMaxSprites) return nullptr;

    const uint8_t* patch = wad_lump_ptr(*sc.wad, lumpIndex);
    int w = tex_s16(patch + 0), h = tex_s16(patch + 2);
    int left = tex_s16(patch + 4), top = tex_s16(patch + 6);
    if (w <= 0 || h <= 0) return nullptr;
    uint8_t* buf = (uint8_t*)arena_alloc(*sc.arena, (uint32_t)(w * h), 8);
    if (!buf) return nullptr;
    for (int i = 0; i < w * h; ++i) buf[i] = kSpriteGap;   // transparent
    tex_blit_patch(buf, w, h, patch, 0, 0);

    int slot = sc.count++;
    sc.entries[slot] = Sprite{ buf, (int16_t)w, (int16_t)h, (int16_t)left, (int16_t)top };
    sc.lumpOf[slot] = lumpIndex;
    sc.composed[slot] = true;
    return &sc.entries[slot];
}

} // namespace doom
