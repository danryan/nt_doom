#pragma once
#include <cstdint>
#include "wad.h"
#include "geom.h"
#include "arena.h"

namespace doom {

static const int kMaxTextures = 32;

struct Texture { const uint8_t* texels; int16_t w, h; };   // column-major palette idx

struct TextureCache {
    const Wad* wad = nullptr;
    Arena* arena = nullptr;
    const uint8_t* texture1 = nullptr;
    const uint8_t* pnames   = nullptr;
    int32_t numTextures = 0;
    int32_t numPnames   = 0;
    Texture entries[kMaxTextures];
    bool    composed[kMaxTextures];

    int find(const char name8[8]) const;
    const Texture* get(int textureIndex);
};

inline uint16_t tex_u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline int16_t  tex_s16(const uint8_t* p) { return (int16_t)tex_u16(p); }
inline uint32_t tex_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

inline bool texcache_init(TextureCache& tc, const Wad& w, Arena& arena) {
    tc.wad = &w; tc.arena = &arena;
    for (int i = 0; i < kMaxTextures; ++i) tc.composed[i] = false;
    int32_t ti = wad_find_lump(w, "TEXTURE1");
    int32_t pi = wad_find_lump(w, "PNAMES");
    if (ti < 0 || pi < 0) return false;
    tc.texture1 = wad_lump_ptr(w, ti);
    tc.pnames   = wad_lump_ptr(w, pi);
    tc.numTextures = (int32_t)tex_u32(tc.texture1);
    tc.numPnames   = (int32_t)tex_u32(tc.pnames);
    if (tc.numTextures > kMaxTextures) tc.numTextures = kMaxTextures;
    return true;
}

// maptexture_t pointer for texture index i (via the offset table after the count).
inline const uint8_t* tex_entry_ptr(const TextureCache& tc, int i) {
    uint32_t off = tex_u32(tc.texture1 + 4 + i * 4);
    return tc.texture1 + off;
}

inline int TextureCache::find(const char name8[8]) const {
    for (int i = 0; i < numTextures; ++i) {
        const uint8_t* e = tex_entry_ptr(*this, i);
        bool eq = true;
        for (int k = 0; k < 8; ++k) if ((char)e[k] != name8[k]) { eq = false; break; }
        if (eq) return i;
    }
    return -1;
}

// Compose a patch's posts into the column-major target buffer.
inline void tex_blit_patch(uint8_t* dst, int dstW, int dstH,
                           const uint8_t* patch, int originX, int originY) {
    int pw = tex_s16(patch + 0);
    for (int pc = 0; pc < pw; ++pc) {
        int tx = originX + pc;
        if (tx < 0 || tx >= dstW) continue;
        uint32_t colOff = tex_u32(patch + 8 + pc * 4);
        const uint8_t* col = patch + colOff;
        while (col[0] != 0xFF) {
            int topDelta = col[0];
            int length   = col[1];
            const uint8_t* px = col + 3;          // skip topdelta,length,pad
            for (int k = 0; k < length; ++k) {
                int ty = originY + topDelta + k;
                if (ty >= 0 && ty < dstH) dst[tx * dstH + ty] = px[k];
            }
            col += 3 + length + 1;                 // advance past pad
        }
    }
}

inline const Texture* TextureCache::get(int textureIndex) {
    if (textureIndex < 0 || textureIndex >= numTextures) return nullptr;
    if (composed[textureIndex]) return &entries[textureIndex];

    const uint8_t* e = tex_entry_ptr(*this, textureIndex);
    int w = tex_s16(e + 12);
    int h = tex_s16(e + 14);
    int patchCount = tex_s16(e + 20);
    uint8_t* buf = (uint8_t*)arena_alloc(*arena, (uint32_t)(w * h), 8);
    if (!buf) return nullptr;
    for (int i = 0; i < w * h; ++i) buf[i] = 0;   // gap sentinel

    const uint8_t* mp = e + 22;                    // first mappatch
    for (int p = 0; p < patchCount; ++p) {
        int originX = tex_s16(mp + 0);
        int originY = tex_s16(mp + 2);
        int pnameIdx = tex_s16(mp + 4);
        mp += 10;
        if (pnameIdx < 0 || pnameIdx >= numPnames) continue;
        char pn[8]; const uint8_t* pe = pnames + 4 + pnameIdx * 8;
        for (int k = 0; k < 8; ++k) pn[k] = (char)pe[k];
        int32_t li = wad_find_lump(*wad, pn);
        if (li < 0) continue;
        tex_blit_patch(buf, w, h, wad_lump_ptr(*wad, li), originX, originY);
    }
    entries[textureIndex] = Texture{ buf, (int16_t)w, (int16_t)h };
    composed[textureIndex] = true;
    return &entries[textureIndex];
}

// Affine horizontal texture coord for a column. Manhattan wall length keeps it
// libm-free and is exact for axis-aligned walls (the synthetic map's walls).
inline int wall_u(const Map& m, int32_t segIndex, float t, int texW) {
    const SegRaw& s = m.segs[segIndex];
    const VertexRaw& a = m.verts[s.v1];
    const VertexRaw& b = m.verts[s.v2];
    float dx = (float)(b.x - a.x), dy = (float)(b.y - a.y);
    float adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    float wallLen = adx + ady;
    const SidedefRaw* sd = seg_sidedef(m, segIndex);
    int xoff = sd ? sd->xoff : 0;
    int u = (int)(t * wallLen) + xoff + s.offset;
    if (texW <= 0) return 0;
    u %= texW; if (u < 0) u += texW;
    return u;
}

inline uint8_t texture_sample(const TextureCache* tex, const Map& m, int32_t segIndex,
                              int textureIndex, float t, int y, int top, int bot) {
    const Texture* T = const_cast<TextureCache*>(tex)->get(textureIndex);
    if (!T) return 0;
    int u = wall_u(m, segIndex, t, T->w);
    int v = (bot > top) ? ((y - top) * T->h) / (bot - top + 1) : 0;
    if (v < 0) v = 0; if (v >= T->h) v = T->h - 1;
    return T->texels[u * T->h + v];
}

} // namespace doom
