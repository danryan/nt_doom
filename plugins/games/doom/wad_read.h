#pragma once
#include <cstdint>
#include "arena.h"

namespace doom {

// The WAD read door. On device, a WAD is smuggled as a 16-bit mono WAV in a
// sample folder; NT_readSampleFrames returns the PCM byte-exact (P0 Spike A). The
// firmware strips the WAV header on device. This host-side source models the same
// byte-addressable window over the WAV blob: it strips the canonical 44-byte
// header itself so the byte logic is fully host-testable. Two WAD bytes occupy one
// little-endian int16 frame, so the WAD byte stream equals the PCM data verbatim;
// a trailing odd-pad byte is included in wadLen and is benign to wad_open.
//
// Device adapter (documented, not host-compiled): issue NT_readSampleFrames with
// bits=kNT_WavBits16, channels=kNT_WavMono, startOffset = off/2 frames,
// numFrames = (n + (off & 1) + 1) / 2, into a scratch frame buffer, then copy n
// bytes from byte (off & 1) of that buffer. The request persists at file scope
// (see plugins/probes/wad_read_probe.cpp); the async callback flips a done flag
// the load loop waits on.

static const uint32_t kWavHeaderBytes = 44;

struct WavWadSource {
    const uint8_t* wav    = nullptr;
    uint32_t       wavLen = 0;
    uint32_t       wadLen = 0;   // WAD payload length (the WAV data chunk size)
};

inline uint16_t wad_rd_u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t wad_rd_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline bool wad_tag4(const uint8_t* p, char a, char b, char c, char d) {
    return p[0] == a && p[1] == b && p[2] == c && p[3] == d;
}

// Validates a canonical 16-bit mono PCM WAV and recovers the WAD payload length.
inline bool wav_wad_source_init(WavWadSource& s, const uint8_t* wav, uint32_t wavLen) {
    if (!wav || wavLen < kWavHeaderBytes) return false;
    if (!wad_tag4(wav + 0,  'R','I','F','F')) return false;
    if (!wad_tag4(wav + 8,  'W','A','V','E')) return false;
    if (!wad_tag4(wav + 12, 'f','m','t',' ')) return false;
    if (wad_rd_u16(wav + 22) != 1)  return false;   // mono
    if (wad_rd_u16(wav + 34) != 16) return false;   // 16-bit
    if (!wad_tag4(wav + 36, 'd','a','t','a')) return false;
    uint32_t dataLen = wad_rd_u32(wav + 40);
    if ((uint64_t)kWavHeaderBytes + dataLen > wavLen) return false;
    s.wav = wav; s.wavLen = wavLen; s.wadLen = dataLen;
    return true;
}

// Copies [off, off+n) of the WAD payload into dst, byte-exact. Bounds-checked.
inline bool wav_wad_read(const WavWadSource& s, uint8_t* dst, uint32_t off, uint32_t n) {
    if (off > s.wadLen || n > s.wadLen - off) return false;
    const uint8_t* src = s.wav + kWavHeaderBytes + off;
    for (uint32_t i = 0; i < n; ++i) dst[i] = src[i];
    return true;
}

// Reads the whole WAD into an arena allocation and returns the span. The caller
// then wad_open()s it. This is the load path the device adapter mirrors.
inline bool wad_load_all(const WavWadSource& s, Arena& arena,
                         const uint8_t** outBytes, uint32_t* outLen) {
    uint8_t* dst = (uint8_t*)arena_alloc(arena, s.wadLen, 8);
    if (!dst) return false;
    if (!wav_wad_read(s, dst, 0, s.wadLen)) return false;
    *outBytes = dst;
    *outLen   = s.wadLen;
    return true;
}

} // namespace doom
