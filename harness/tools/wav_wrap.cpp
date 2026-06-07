#include "wav_wrap.h"
#include <cstdio>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3) { fprintf(stderr, "usage: wav_wrap <in> <out.wav>\n"); return 2; }
    FILE* in = fopen(argv[1], "rb");
    if (!in) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    std::vector<uint8_t> payload;
    uint8_t buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) payload.insert(payload.end(), buf, buf + n);
    fclose(in);
    // 16-bit: the NT sample browser rejects 8-bit WAV. NT_readSampleFrames
    // returns 16-bit data unconverted when the requested depth matches, so the
    // WAD bytes round-trip exactly (two bytes per little-endian sample).
    std::vector<uint8_t> wav = wrap_wav_16bit_mono(payload, 48000);
    FILE* out = fopen(argv[2], "wb");
    if (!out) { fprintf(stderr, "cannot write %s\n", argv[2]); return 1; }
    fwrite(wav.data(), 1, wav.size(), out);
    fclose(out);
    return 0;
}
