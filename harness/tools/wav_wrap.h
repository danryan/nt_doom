#pragma once
#include <cstdint>
#include <vector>

inline void put_u32le(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF);
    v.push_back((x >> 16) & 0xFF); v.push_back((x >> 24) & 0xFF);
}
inline void put_u16le(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF);
}
inline void put_str(std::vector<uint8_t>& v, const char* s) {
    for (int i = 0; i < 4; ++i) v.push_back((uint8_t)s[i]);
}

// Canonical 8-bit unsigned mono PCM WAV. Payload bytes are the raw sample data
// and are emitted verbatim after the 44-byte header.
inline std::vector<uint8_t> wrap_wav_8bit_mono(const std::vector<uint8_t>& payload,
                                               uint32_t sampleRate) {
    std::vector<uint8_t> v;
    uint32_t dataLen = (uint32_t)payload.size();
    put_str(v, "RIFF"); put_u32le(v, 36 + dataLen); put_str(v, "WAVE");
    put_str(v, "fmt "); put_u32le(v, 16);
    put_u16le(v, 1);              // PCM
    put_u16le(v, 1);              // mono
    put_u32le(v, sampleRate);
    put_u32le(v, sampleRate);     // byte rate = sampleRate * 1 * 1
    put_u16le(v, 1);              // block align
    put_u16le(v, 8);              // bits per sample
    put_str(v, "data"); put_u32le(v, dataLen);
    v.insert(v.end(), payload.begin(), payload.end());
    return v;
}

// 16-bit mono PCM WAV. The payload (arbitrary WAD bytes) is emitted verbatim as
// the sample data: two payload bytes per 16-bit little-endian sample. An odd
// trailing byte is zero-padded to a whole sample. The NT only accepts 16/24-bit
// WAV in its sample browser, and NT_readSampleFrames returns the data unconverted
// when the requested bit depth matches the file, so the bytes round-trip exactly.
inline std::vector<uint8_t> wrap_wav_16bit_mono(const std::vector<uint8_t>& payload,
                                                uint32_t sampleRate) {
    std::vector<uint8_t> data = payload;
    if (data.size() & 1) data.push_back(0);   // pad to whole 16-bit samples
    std::vector<uint8_t> v;
    uint32_t dataLen = (uint32_t)data.size();
    put_str(v, "RIFF"); put_u32le(v, 36 + dataLen); put_str(v, "WAVE");
    put_str(v, "fmt "); put_u32le(v, 16);
    put_u16le(v, 1);              // PCM
    put_u16le(v, 1);              // mono
    put_u32le(v, sampleRate);
    put_u32le(v, sampleRate * 2); // byte rate = sampleRate * 1 ch * 2 bytes
    put_u16le(v, 2);              // block align
    put_u16le(v, 16);             // bits per sample
    put_str(v, "data"); put_u32le(v, dataLen);
    v.insert(v.end(), data.begin(), data.end());
    return v;
}
