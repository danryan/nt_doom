#pragma once
#include <cstdint>
#include "geom.h"
#include "fb.h"
#include "palette.h"
#include "texture.h"
#include "sprite.h"

namespace doom {

// Provided per-TU: host (test) defines it with <cmath>; ARM defines it from a
// rodata sine LUT. Keeps this header libm-free.
void cos_sin(float ang, float& c, float& s);

struct Camera { float x, y, angle; };

static const int kScreenW = 256;
static const int kScreenH = 64;
static const int kScreenWmax = 255;   // last screen column

// Per-column wall depth sentinel: no wall drawn in this column. A sprite column draws
// only where it is strictly nearer than the stored wall depth.
static const float kFarDepth = 1.0e30f;
inline bool sprite_column_visible(float spriteDepth, float wallDepth) {
    return spriteDepth < wallDepth;
}

// Doom R_PointOnSide: cross = right - left; 0 = front/right side, 1 = back/left.
inline int point_on_side(const NodeRaw& n, float cx, float cy) {
    float cross = (float)n.dx * (cy - n.y) - (float)n.dy * (cx - n.x);
    return cross < 0.0f ? 0 : 1;
}

// Absolute recursion-depth ceiling. Real Doom BSP trees are balanced and rarely
// exceed a few dozen levels; this bounds the embedded draw-thread stack against a
// pathological or hostile near-linear tree even when numNodes is large.
static const int kMaxBspDepth = 256;

// Fills out[0..count) with subsector indices front-to-back; returns count.
// Depth guard cuts cyclic/malformed NODES (real WADs are acyclic); kMaxBspDepth
// additionally bounds the stack for large near-linear trees.
inline int bsp_visit_order(const Map& m, const Camera& cam, int* out, int cap) {
    int count = 0;
    auto recurse = [&](auto&& self, uint16_t ref, int depth) -> void {
        if (count >= cap || depth > m.numNodes || depth > kMaxBspDepth) return;
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
    if (x1 < 0) x1 = 0;
    if (x2 > kScreenWmax) x2 = kScreenWmax;
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
    if (sectorLight < 0) sectorLight = 0;
    if (sectorLight > 255) sectorLight = 255;
    int base = (255 - sectorLight) >> 3;            // 0 (bright) .. 31 (dark)
    int dadd = (int)(depth * kDepthLight);
    int row  = base + dadd;
    if (row < 0) row = 0;
    if (numMaps > 0 && row >= numMaps) row = numMaps - 1;
    return row;
}

// Draw one seg's wall columns into fb, clipped against the solidsegs list.
inline void render_seg(const Map& m, int32_t segIndex, float ca, float sa,
                       const Camera& cam, const Palette& pal, const Colormap& cm,
                       const TextureCache* tex, SolidSegs& solid, uint8_t* fb,
                       float* depthOut = nullptr) {
    if (segIndex < 0 || segIndex >= m.numSegs) return;
    const SegRaw& seg = m.segs[segIndex];
    // Bounds-check vertex indices: a corrupt or partially-read WAD can carry garbage seg
    // indices that would otherwise dereference unmapped memory (a device hard fault).
    if ((uint16_t)seg.v1 >= (uint16_t)m.numVerts || (uint16_t)seg.v2 >= (uint16_t)m.numVerts) return;
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

    // Texture id is invariant across the seg's columns; resolve it once.
    int texId = -1;
    if (tex) {
        const SidedefRaw* sd = seg_sidedef(m, segIndex);
        if (sd) texId = tex->find(sd->middle);
    }

    int spanA = sxA < 0 ? 0 : sxA;
    int spanB = sxB > kScreenWmax ? kScreenWmax : sxB;
    if (spanB < spanA) return;

    solidsegs_clip_solid(solid, spanA, spanB, [&](int vx1, int vx2) {
        for (int x = vx1; x <= vx2; ++x) {
            float t = (sxB == sxA) ? 0.0f : (float)(x - sxA) / (float)(sxB - sxA);
            float depth = dA + (dB - dA) * t;
            if (depth < 1.0f) depth = 1.0f;
            if (depthOut) depthOut[x] = depth;   // nearest wall depth (front-to-back, once per col)
            float wallH = (kScreenH * kWallScale) / depth;
            int top = (int)(kScreenH / 2 - wallH / 2);
            int bot = (int)(kScreenH / 2 + wallH / 2);
            if (top < 0) top = 0;
            if (bot > kScreenH - 1) bot = kScreenH - 1;
            int light = light_row(sectorLight, depth, cm.numMaps);
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
                             const TextureCache* tex, SolidSegs& solid, uint8_t* fb,
                             float* depthOut = nullptr) {
    if (ssecIndex < 0 || ssecIndex >= m.numSsecs) return;
    const SubsecRaw& ss = m.ssecs[ssecIndex];
    for (int s = 0; s < ss.numSegs; ++s)
        render_seg(m, ss.firstSeg + s, ca, sa, cam, pal, cm, tex, solid, fb, depthOut);
}

// Production renderer. tex == nullptr selects flat-shaded sectors (Unit D);
// Unit E passes a TextureCache and textures the columns instead.
inline void render_view(const Map& m, const Camera& cam,
                        const Palette& pal, const Colormap& cm,
                        const TextureCache* tex, uint8_t* fb, float* depthOut = nullptr) {
    fb_clear(fb);
    if (depthOut) for (int i = 0; i < kScreenW; ++i) depthOut[i] = kFarDepth;
    float ca, sa; cos_sin(cam.angle, ca, sa);
    SolidSegs solid; solidsegs_clear(solid);
    // Visit-order ceiling. E1M1 has ~470 subsectors, well under 1024; a larger PWAD
    // would truncate here (bsp_visit_order stops at cap, dropping the deepest leaves).
    // STATIC, not a stack array: 1024 ints is 4 KB, which overflows the NT's small draw
    // stack on a real map (the synthetic 2-subsector map never tripped it; E1M1's deep
    // BSP tree does, corrupting the return address into a hard fault). draw() is called
    // single-threaded by the firmware, so a static scratch buffer is safe.
    static const int kMaxVisit = 1024;
    static int order[kMaxVisit];
    int n = bsp_visit_order(m, cam, order, kMaxVisit);
    for (int i = 0; i < n; ++i)
        render_subsector(m, order[i], ca, sa, cam, pal, cm, tex, solid, fb, depthOut);
}

// ---------------------------------------------------------------------------
// Things: camera-facing billboard sprites, depth-clipped against the per-column wall
// depth buffer written by render_view.

static const float kNearClip    = 1.0f;
static const float kSpriteScale = kWallScale;   // tie sprite size to the wall projection
static const int   kSpriteLight = 192;          // nominal sector light for sprite shading
static const float kDeg2Rad     = 0.017453293f; // THINGS angle is degrees; cos_sin wants radians

struct SpriteProj {
    bool  visible;          // in front of the near plane
    float depth;            // camera-space forward distance
    int   colL, colR;       // inclusive screen column span
    int   rowTop, rowBot;   // inclusive screen row span
};

// Project a thing at world (tx,ty) to a screen column/row span, camera-facing. Same camera
// transform as render_seg; vertical placement centers the sprite on the horizon row (the
// floor-less P4 view; the patch top offset and feet placement come with visplanes in P5).
inline SpriteProj project_thing(const Camera& cam, float ca, float sa,
                                float tx, float ty, int spriteW, int spriteH) {
    float dx = tx - cam.x, dy = ty - cam.y;
    float a1 =  dx * ca + dy * sa;        // depth
    float a2 = -dx * sa + dy * ca;        // lateral
    if (a1 <= kNearClip) return SpriteProj{false, a1, 0, 0, 0, 0};
    int colC  = (int)(kScreenW / 2 + (a2 / a1) * kFovScale);
    float scrW = (float)spriteW * kSpriteScale / a1;
    float scrH = (float)spriteH * kSpriteScale / a1;
    int halfW = (int)(scrW / 2), halfH = (int)(scrH / 2);
    int mid = kScreenH / 2;
    return SpriteProj{true, a1, colC - halfW, colC + halfW, mid - halfH, mid + halfH};
}

// Eight-way compass from a 2D vector (libm-free), 0=E,1=NE,2=N,3=NW,4=W,5=SW,6=S,7=SE.
inline int octant_of(float x, float y) {
    const float T = 0.41421356f;          // tan(22.5 deg)
    float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    if (ay <= ax * T) return x >= 0 ? 0 : 4;   // near the x axis
    if (ax <= ay * T) return y >= 0 ? 2 : 6;   // near the y axis
    if (x >= 0) return y >= 0 ? 1 : 7;         // diagonal, +x
    return y >= 0 ? 3 : 5;                      // diagonal, -x
}

// Rotation digit 1..8 for viewing a thing of facing thingAngle (radians) from the
// player-to-thing vector. Front (thing faces the viewer) is rotation 1, back is 5.
inline int sprite_rotation(float thingAngle, float toThingDx, float toThingDy) {
    float vx = -toThingDx, vy = -toThingDy;    // thing -> viewer
    float ca, sa; cos_sin(thingAngle, ca, sa);
    float fwd  =  vx * ca + vy * sa;           // along the thing's facing
    float left = -vx * sa + vy * ca;
    return octant_of(fwd, left) + 1;
}

typedef const char* (*ThingSpriteFn)(int type);   // null when the type is undrawable

// Draw all drawable things back-to-front, depth-clipped per column. order is caller scratch
// (static/instance, never a draw-stack local). The type-to-sprite-name resolver is injected
// so render.h does not depend on combat.h.
inline void render_things(const Map& m, const Camera& cam, const Palette& pal, const Colormap& cm,
                          const SpriteCache* sc, const float* depthBuf, uint8_t* fb,
                          int* order, int orderCap, ThingSpriteFn nameFn) {
    if (!sc || !nameFn || !m.things) return;
    float ca, sa; cos_sin(cam.angle, ca, sa);
    auto depthOf = [&](int idx) -> float {
        return ((float)m.things[idx].x - cam.x) * ca + ((float)m.things[idx].y - cam.y) * sa;
    };

    int cnt = 0;
    for (int i = 0; i < m.numThings && cnt < orderCap; ++i) {
        if (!nameFn(m.things[i].type)) continue;
        if (depthOf(i) <= kNearClip) continue;
        order[cnt++] = i;
    }
    for (int i = 1; i < cnt; ++i) {            // insertion sort, far-first (descending depth)
        int key = order[i]; float kd = depthOf(key); int j = i - 1;
        while (j >= 0 && depthOf(order[j]) < kd) { order[j + 1] = order[j]; --j; }
        order[j + 1] = key;
    }

    for (int k = 0; k < cnt; ++k) {
        int i = order[k];
        const char* nm = nameFn(m.things[i].type);
        float tx = (float)m.things[i].x, ty = (float)m.things[i].y;
        int rot = sprite_rotation((float)m.things[i].angle * kDeg2Rad, tx - cam.x, ty - cam.y);
        bool flip = false;
        int li = sprite_find(*sc, nm, 'A', rot, flip);
        if (li < 0) li = sprite_find(*sc, nm, 'A', 0, flip);
        if (li < 0) li = sprite_find(*sc, nm, 'A', 1, flip);
        if (li < 0) continue;
        const Sprite* sp = sprite_get(*sc, li);
        if (!sp) continue;
        SpriteProj p = project_thing(cam, ca, sa, tx, ty, sp->w, sp->h);
        if (!p.visible) continue;
        int spanW = p.colR - p.colL + 1, spanH = p.rowBot - p.rowTop + 1;
        if (spanW <= 0 || spanH <= 0) continue;
        int light = light_row(kSpriteLight, p.depth, cm.numMaps);
        for (int x = p.colL; x <= p.colR; ++x) {
            if (x < 0 || x > kScreenWmax) continue;
            if (!sprite_column_visible(p.depth, depthBuf[x])) continue;
            int u = ((x - p.colL) * sp->w) / spanW;
            if (u < 0) u = 0;
            if (u >= sp->w) u = sp->w - 1;
            if (flip) u = sp->w - 1 - u;
            for (int y = p.rowTop; y <= p.rowBot; ++y) {
                if (y < 0 || y >= kScreenH) continue;
                int v = ((y - p.rowTop) * sp->h) / spanH;
                if (v < 0) v = 0;
                if (v >= sp->h) v = sp->h - 1;
                uint8_t texel = sp->texels[u * sp->h + v];
                if (texel == kSpriteGap) continue;
                fb_put(fb, x, y, shade_gray(pal, cm, texel, light));
            }
        }
    }
}

} // namespace doom
