#pragma once
#include <cstdint>

namespace doom {

// 256x64, 4-bit, two pixels per byte. Mirrors shim/src/graphics.cpp set_pixel:
// even x occupies the high nibble (bits 4..7), odd x the low nibble (bits 0..3).
inline void fb_put(uint8_t* fb, int x, int y, uint8_t gray4) {
    if (x < 0 || x >= 256 || y < 0 || y >= 64) return;
    int byteIndex = y * 128 + (x >> 1);
    uint8_t g = gray4 & 0x0F;
    if (x & 1) fb[byteIndex] = (fb[byteIndex] & 0xF0) | g;
    else       fb[byteIndex] = (fb[byteIndex] & 0x0F) | (uint8_t)(g << 4);
}
inline uint8_t fb_get(const uint8_t* fb, int x, int y) {
    int byteIndex = y * 128 + (x >> 1);
    return (x & 1) ? (fb[byteIndex] & 0x0F) : (uint8_t)((fb[byteIndex] >> 4) & 0x0F);
}
inline void fb_clear(uint8_t* fb) { for (int i = 0; i < 128 * 64; ++i) fb[i] = 0; }

} // namespace doom
