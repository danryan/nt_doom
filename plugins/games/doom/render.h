#pragma once
#include <cstdint>
#include "geom.h"
#include "fb.h"

namespace doom {

// Provided per-TU: host (test) defines it with <cmath>; ARM defines it from a
// rodata sine LUT. Keeps this header libm-free.
void cos_sin(float ang, float& c, float& s);

struct Camera { float x, y, angle; };

static const int kScreenW = 256;
static const int kScreenH = 64;

// A 16x16 wall texture in rodata (gray 0..15). Exercises the texture-sample
// code path so the .text measurement is representative.
static const uint8_t kWallTex[16 * 16] = {
#define R0 12,12,11,11,12,12,11,11,12,12,11,11,12,12,11,11
    R0,R0,R0,R0,R0,R0,R0,R0,R0,R0,R0,R0,R0,R0,R0,R0
#undef R0
};

inline uint8_t shade(uint8_t base, float dist) {
    float atten = 64.0f / (dist + 1.0f);   // closer = brighter
    if (atten > 1.0f) atten = 1.0f;
    int g = (int)(base * atten);
    if (g < 0) g = 0;
    if (g > 15) g = 15;
    return (uint8_t)g;
}

// Renders the walls of subsector 0 into fb (128*64). Float math, FPU-friendly.
inline void render_view(const Map& m, const Camera& cam, uint8_t* fb) {
    fb_clear(fb);
    if (m.numSsecs == 0) return;
    const SubsecRaw& ss = m.ssecs[0];
    float ca, sa;
    cos_sin(cam.angle, ca, sa);

    const float fovScale = (float)kScreenW / 2.0f;   // ~90 deg horizontal
    for (int s = 0; s < ss.numSegs; ++s) {
        const SegRaw& seg = m.segs[ss.firstSeg + s];
        const VertexRaw& a = m.verts[seg.v1];
        const VertexRaw& b = m.verts[seg.v2];

        // World to camera space.
        float ax = a.x - cam.x, ay = a.y - cam.y;
        float bx = b.x - cam.x, by = b.y - cam.y;
        float a1 =  ax * ca + ay * sa, a2 = -ax * sa + ay * ca;
        float b1 =  bx * ca + by * sa, b2 = -bx * sa + by * ca;
        // a1/b1 are depth (forward), a2/b2 are lateral.

        if (a1 <= 1.0f && b1 <= 1.0f) continue;   // behind camera
        if (a1 < 1.0f) a1 = 1.0f;
        if (b1 < 1.0f) b1 = 1.0f;

        int sxA = (int)(kScreenW / 2 + (a2 / a1) * fovScale);
        int sxB = (int)(kScreenW / 2 + (b2 / b1) * fovScale);
        if (sxA > sxB) { int t = sxA; sxA = sxB; sxB = t; float td=a1; a1=b1; b1=td; }
        if (sxB <= sxA) continue;

        for (int x = sxA; x <= sxB; ++x) {
            if (x < 0 || x >= kScreenW) continue;
            float t = (sxB == sxA) ? 0.0f : (float)(x - sxA) / (float)(sxB - sxA);
            float depth = a1 + (b1 - a1) * t;
            float wallH = (kScreenH * 32.0f) / depth;
            int top = (int)(kScreenH / 2 - wallH / 2);
            int bot = (int)(kScreenH / 2 + wallH / 2);
            if (top < 0) top = 0;
            if (bot >= kScreenH) bot = kScreenH - 1;
            int u = ((int)(t * 16.0f)) & 15;
            for (int y = top; y <= bot; ++y) {
                int vtex = ((y - top) * 16) / (bot - top + 1);
                if (vtex < 0) vtex = 0;
                if (vtex > 15) vtex = 15;
                uint8_t texel = kWallTex[vtex * 16 + u];
                fb_put(fb, x, y, shade(texel, depth));
            }
        }
    }
}

} // namespace doom
