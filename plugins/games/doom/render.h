#pragma once
#include <cstdint>
#include "geom.h"
#include "fb.h"
#include "palette.h"

namespace doom {

struct TextureCache;   // forward decl; Unit E defines it. render_view takes a pointer only.

// Provided per-TU: host (test) defines it with <cmath>; ARM defines it from a
// rodata sine LUT. Keeps this header libm-free.
void cos_sin(float ang, float& c, float& s);

struct Camera { float x, y, angle; };

static const int kScreenW = 256;
static const int kScreenH = 64;
static const int kScreenWmax = 255;   // last screen column

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

// Doom R_PointOnSide: cross = right - left; 0 = front/right side, 1 = back/left.
inline int point_on_side(const NodeRaw& n, float cx, float cy) {
    float cross = (float)n.dx * (cy - n.y) - (float)n.dy * (cx - n.x);
    return cross < 0.0f ? 0 : 1;
}

// Fills out[0..count) with subsector indices front-to-back; returns count.
// Depth guard cuts cyclic/malformed NODES (real WADs are acyclic).
inline int bsp_visit_order(const Map& m, const Camera& cam, int* out, int cap) {
    int count = 0;
    auto recurse = [&](auto&& self, uint16_t ref, int depth) -> void {
        if (count >= cap || depth > m.numNodes) return;
        if (node_child_is_subsector(ref)) { out[count++] = node_child_index(ref); return; }
        if (ref >= (uint16_t)m.numNodes) return;
        const NodeRaw& n = m.nodes[ref];
        int side = point_on_side(n, cam.x, cam.y);
        self(self, n.child[side],     depth + 1);   // near first
        self(self, n.child[side ^ 1], depth + 1);   // far
    };
    if (m.numNodes == 0) {
        for (int32_t i = 0; i < m.numSsecs && count < cap; ++i) out[count++] = (int)i;
    } else {
        recurse(recurse, (uint16_t)(m.numNodes - 1), 0);
    }
    return count;
}

static const int kMaxClip = 64;          // disjoint occluded ranges; ample for 256 cols
struct SolidSegs {
    struct Range { int16_t first, last; };   // inclusive, occluded; sorted by first
    Range r[kMaxClip];
    int n;
};
inline void solidsegs_clear(SolidSegs& s) { s.n = 0; }

// Insert [a,b] into the sorted list, coalescing overlapping/adjacent ranges.
inline void solidsegs_insert(SolidSegs& s, int a, int b) {
    using Range = SolidSegs::Range;
    Range nr{(int16_t)a, (int16_t)b};
    Range merged[kMaxClip]; int mn = 0; bool placed = false;
    auto flush = [&](Range x){ if (mn < kMaxClip) merged[mn++] = x; };
    for (int i = 0; i < s.n; ++i) {
        Range cur = s.r[i];
        if (!placed && nr.last + 1 < cur.first) { flush(nr); placed = true; }
        if (placed || cur.last + 1 < nr.first) { flush(cur); }
        else { // overlap/adjacent with nr: absorb into nr
            if (cur.first < nr.first) nr.first = cur.first;
            if (cur.last  > nr.last)  nr.last  = cur.last;
        }
    }
    if (!placed) flush(nr);
    s.n = mn; for (int i = 0; i < mn; ++i) s.r[i] = merged[i];
}

// Clip solid wall span [x1,x2] (clamped, x1<=x2): emit visible gaps via drawSpan,
// then mark the span occluded. Front-to-back order makes each column drawn once.
template<class DrawSpan>
inline void solidsegs_clip_solid(SolidSegs& s, int x1, int x2, DrawSpan drawSpan) {
    if (x1 < 0) x1 = 0; if (x2 > kScreenWmax) x2 = kScreenWmax;
    if (x2 < x1) return;
    int cur = x1;
    for (int i = 0; i < s.n && cur <= x2; ++i) {
        const SolidSegs::Range& R = s.r[i];
        if (R.last < cur) continue;
        if (R.first > x2) break;
        if (R.first > cur) drawSpan(cur, R.first - 1);
        if (R.last + 1 > cur) cur = R.last + 1;
    }
    if (cur <= x2) drawSpan(cur, x2);
    solidsegs_insert(s, x1, x2);
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
