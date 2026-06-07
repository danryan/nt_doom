#include "catch.hpp"
#include "../tools/wav_wrap.h"
#include <cstring>

TEST_CASE("8-bit mono WAV header wraps payload byte-exact", "[wavwrap]") {
    std::vector<uint8_t> payload = {0x00, 0x01, 0x02, 0xFF, 0x80};
    std::vector<uint8_t> wav = wrap_wav_8bit_mono(payload, 48000);

    REQUIRE(wav.size() == 44 + payload.size());
    REQUIRE(std::memcmp(wav.data(), "RIFF", 4) == 0);
    REQUIRE(std::memcmp(wav.data() + 8, "WAVE", 4) == 0);
    REQUIRE(std::memcmp(wav.data() + 12, "fmt ", 4) == 0);
    REQUIRE(std::memcmp(wav.data() + 36, "data", 4) == 0);

    uint16_t bits = wav[34] | (wav[35] << 8);
    uint16_t channels = wav[22] | (wav[23] << 8);
    REQUIRE(bits == 8);
    REQUIRE(channels == 1);

    for (size_t i = 0; i < payload.size(); ++i)
        REQUIRE(wav[44 + i] == payload[i]);
}

TEST_CASE("16-bit mono WAV header wraps payload byte-exact", "[wavwrap]") {
    std::vector<uint8_t> payload = {0x03, 0x0A, 0x11, 0x18, 0x1F, 0x26};
    std::vector<uint8_t> wav = wrap_wav_16bit_mono(payload, 48000);

    REQUIRE(wav.size() == 44 + payload.size());   // even payload: no pad
    REQUIRE(std::memcmp(wav.data(), "RIFF", 4) == 0);
    REQUIRE(std::memcmp(wav.data() + 36, "data", 4) == 0);

    uint16_t bits = wav[34] | (wav[35] << 8);
    uint16_t blockAlign = wav[32] | (wav[33] << 8);
    REQUIRE(bits == 16);
    REQUIRE(blockAlign == 2);

    // Payload bytes are the raw little-endian sample data, verbatim.
    for (size_t i = 0; i < payload.size(); ++i)
        REQUIRE(wav[44 + i] == payload[i]);
}

TEST_CASE("16-bit wrapper zero-pads an odd payload to a whole sample", "[wavwrap]") {
    std::vector<uint8_t> payload = {0xAB, 0xCD, 0xEF};   // 3 bytes
    std::vector<uint8_t> wav = wrap_wav_16bit_mono(payload, 48000);
    REQUIRE(wav.size() == 44 + 4);          // padded to 4 data bytes
    REQUIRE(wav[44 + 3] == 0x00);           // pad byte
}
