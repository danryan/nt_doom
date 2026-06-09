#pragma once
#include <cstdint>
#include "geom.h"
#include "fb.h"
#include "palette.h"
#include "texture.h"

namespace doom {

// Provided per-TU: host (test) defines it with <cmath>; ARM defines it from a
// rodata sine LUT. Keeps this header libm-free.
void cos_sin(float ang, float& c, float& s);

struct Camera { float x, y, angle; };

static const int kScreenW = 256;
static const int kScreenH = 64;
static const int kScreenWmax = 255;   // last screen column

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

static const float kFovScale  = (float)kScreenW / 2.0f;   // ~90 deg horizontal
static const float kWallScale = 32.0f;                    // wall-height constant
static const float kDepthLight = 0.03f;                   // depth -> colormap rows
static const uint8_t kFlatWallIndex = 200;                // flat-mode base palette index

inline int light_row(int sectorLight, float depth, int numMaps) {
    if (sectorLight < 0) sectorLight = 0; if (sectorLight > 255) sectorLight = 255;
    int base = (255 - sectorLight) >> 3;            // 0 (bright) .. 31 (dark)
    int dadd = (int)(depth * kDepthLight);
    int row  = base + dadd;
    if (row < 0) row = 0; if (numMaps > 0 && row >= numMaps) row = numMaps - 1;
    return row;
}

// Draw one seg's wall columns into fb, clipped against the solidsegs list.
inline void render_seg(const Map& m, int32_t segIndex, float ca, float sa,
                       const Camera& cam, const Palette& pal, const Colormap& cm,
                       const TextureCache* tex, SolidSegs& solid, uint8_t* fb) {
    const SegRaw& seg = m.segs[segIndex];
    const VertexRaw& A = m.verts[seg.v1];
    const VertexRaw& B = m.verts[seg.v2];
    float ax = A.x - cam.x, ay = A.y - cam.y;
    float bx = B.x - cam.x, by = B.y - cam.y;
    float a1 =  ax * ca + ay * sa, a2 = -ax * sa + ay * ca;   // a1 depth, a2 lateral
    float b1 =  bx * ca + by * sa, b2 = -bx * sa + by * ca;
    if (a1 <= 1.0f && b1 <= 1.0f) return;                     // wholly behind
    if (a1 < 1.0f) a1 = 1.0f;
    if (b1 < 1.0f) b1 = 1.0f;
    int sxA = (int)(kScreenW / 2 + (a2 / a1) * kFovScale);
    int sxB = (int)(kScreenW / 2 + (b2 / b1) * kFovScale);
    float dA = a1, dB = b1;
    if (sxA > sxB) { int t = sxA; sxA = sxB; sxB = t; float td = dA; dA = dB; dB = td; }
    if (sxB < sxA) return;

    const SectorRaw* sec = seg_sector(m, segIndex);
    int sectorLight = sec ? sec->light : 128;

    int spanA = sxA < 0 ? 0 : sxA;
    int spanB = sxB > kScreenWmax ? kScreenWmax : sxB;
    if (spanB < spanA) return;

    solidsegs_clip_solid(solid, spanA, spanB, [&](int vx1, int vx2) {
        for (int x = vx1; x <= vx2; ++x) {
            float t = (sxB == sxA) ? 0.0f : (float)(x - sxA) / (float)(sxB - sxA);
            float depth = dA + (dB - dA) * t;
            if (depth < 1.0f) depth = 1.0f;
            float wallH = (kScreenH * kWallScale) / depth;
            int top = (int)(kScreenH / 2 - wallH / 2);
            int bot = (int)(kScreenH / 2 + wallH / 2);
            if (top < 0) top = 0;
            if (bot > kScreenH - 1) bot = kScreenH - 1;
            int light = light_row(sectorLight, depth, cm.numMaps);
            int texId = -1;
            if (tex) {
                const SidedefRaw* sd = seg_sidedef(m, segIndex);
                if (sd) texId = tex->find(sd->middle);
            }
            for (int y = top; y <= bot; ++y) {
                uint8_t palIndex = (tex && texId >= 0)
                    ? texture_sample(tex, m, segIndex, texId, t, y, top, bot)
                    : kFlatWallIndex;
                fb_put(fb, x, y, shade_gray(pal, cm, palIndex, light));
            }
        }
    });
}

inline void render_subsector(const Map& m, int32_t ssecIndex, float ca, float sa,
                             const Camera& cam, const Palette& pal, const Colormap& cm,
                             const TextureCache* tex, SolidSegs& solid, uint8_t* fb) {
    const SubsecRaw& ss = m.ssecs[ssecIndex];
    for (int s = 0; s < ss.numSegs; ++s)
        render_seg(m, ss.firstSeg + s, ca, sa, cam, pal, cm, tex, solid, fb);
}

// Production renderer. tex == nullptr selects flat-shaded sectors (Unit D);
// Unit E passes a TextureCache and textures the columns instead.
inline void render_view(const Map& m, const Camera& cam,
                        const Palette& pal, const Colormap& cm,
                        const TextureCache* tex, uint8_t* fb) {
    fb_clear(fb);
    float ca, sa; cos_sin(cam.angle, ca, sa);
    SolidSegs solid; solidsegs_clear(solid);
    int order[1024];
    int n = bsp_visit_order(m, cam, order, 1024);
    for (int i = 0; i < n; ++i)
        render_subsector(m, order[i], ca, sa, cam, pal, cm, tex, solid, fb);
}

} // namespace doom
