#pragma once
#include <cstdint>
#include "wad.h"

namespace doom {

// Integer Rec.601 luma to 4-bit gray. Weights sum to 256 (77+150+29), so the
// >>8 yields 0..255, then >>4 yields 0..15. Green-dominant, blue-weak.
inline uint8_t luma4(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t luma = (77u * r + 150u * g + 29u * b) >> 8;
    return (uint8_t)(luma >> 4);
}

// Palette 0 of PLAYPAL precomputed to 4-bit gray (256 entries).
struct Palette { uint8_t gray[256]; };

inline bool palette_load(const Wad& w, Palette& pal) {
    int32_t i = wad_find_lump(w, "PLAYPAL");
    if (i < 0) return false;
    if (wad_lump_size(w, i) < 768) return false;
    const uint8_t* p = wad_lump_ptr(w, i);
    for (int e = 0; e < 256; ++e)
        pal.gray[e] = luma4(p[e * 3 + 0], p[e * 3 + 1], p[e * 3 + 2]);
    return true;
}

// Zero-copy view over COLORMAP: numMaps light maps of 256 bytes each. Entry
// maps[m*256 + i] is the palette index that source index i maps to at light m.
struct Colormap { const uint8_t* maps = nullptr; int32_t numMaps = 0; };

inline bool colormap_load(const Wad& w, Colormap& cm) {
    int32_t i = wad_find_lump(w, "COLORMAP");
    if (i < 0) return false;
    cm.maps    = wad_lump_ptr(w, i);
    cm.numMaps = (int32_t)(wad_lump_size(w, i) / 256);
    return cm.numMaps > 0;
}

// Final 4-bit gray for a palette index at a given light level. `light` is
// clamped into [0, numMaps).
inline uint8_t shade_gray(const Palette& pal, const Colormap& cm, uint8_t palIndex, int light) {
    if (light < 0) light = 0;
    if (light >= cm.numMaps) light = cm.numMaps - 1;
    uint8_t mapped = cm.maps[light * 256 + palIndex];
    return pal.gray[mapped];
}

} // namespace doom
