#pragma once
#include <cstdint>
#include "geom.h"

namespace doom {

struct Vec2 { float x, y; };

inline float cross2(float ox, float oy, float ax, float ay, float bx, float by) {
    return (ax - ox) * (by - oy) - (ay - oy) * (bx - ox);
}

// Proper (non-collinear) intersection of segments ab and cd. Libm-free orientation test.
// Collinear grazing is left to the radius clause in move_blocked.
inline bool segs_intersect(float ax, float ay, float bx, float by,
                           float cx, float cy, float dx, float dy) {
    float d1 = cross2(cx, cy, dx, dy, ax, ay);
    float d2 = cross2(cx, cy, dx, dy, bx, by);
    float d3 = cross2(ax, ay, bx, by, cx, cy);
    float d4 = cross2(ax, ay, bx, by, dx, dy);
    return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
           ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}

// Squared distance from point p to segment ab (clamped projection). No sqrt.
inline float point_seg_dist2(float px, float py, float ax, float ay, float bx, float by) {
    float vx = bx - ax, vy = by - ay;
    float wx = px - ax, wy = py - ay;
    float vv = vx * vx + vy * vy;
    float t = vv > 0.0f ? (wx * vx + wy * vy) / vv : 0.0f;
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    float ex = px - (ax + t * vx), ey = py - (ay + t * vy);
    return ex * ex + ey * ey;
}

// A linedef blocks the player when one-sided (back == 0xFFFF) or flagged ML_BLOCKING (0x1).
inline bool line_is_solid(const Map& m, int ln) {
    if (ln < 0 || ln >= m.numLines) return false;
    const LinedefRaw& L = m.lines[ln];
    return ((uint16_t)L.back == 0xFFFFu) || (L.flags & 0x0001);
}

// Would the move (x0,y0)->(x1,y1) breach a solid linedef for a player of `radius`?
// Blocked when the swept segment crosses a solid line (anti-tunnel) or the destination
// moves within radius of one while getting closer to it (so parallel slides are allowed).
inline bool move_blocked(const Map& m, const Blockmap& bm,
                         float x0, float y0, float x1, float y1, float radius) {
    float r2 = radius * radius;
    float minx = (x0 < x1 ? x0 : x1) - radius, maxx = (x0 > x1 ? x0 : x1) + radius;
    float miny = (y0 < y1 ? y0 : y1) - radius, maxy = (y0 > y1 ? y0 : y1) + radius;
    auto cellClamp = [&](float v, float origin, int n) -> int {
        int c = (v < origin) ? 0 : (int)((v - origin) / (float)kBlockSize);
        if (c < 0) c = 0;
        if (c >= n) c = n - 1;
        return c;
    };
    int c0 = cellClamp(minx, bm.originX, bm.cols), c1 = cellClamp(maxx, bm.originX, bm.cols);
    int r0 = cellClamp(miny, bm.originY, bm.rows), r1 = cellClamp(maxy, bm.originY, bm.rows);
    bool blocked = false;
    for (int r = r0; r <= r1 && !blocked; ++r)
        for (int c = c0; c <= c1 && !blocked; ++c)
            blockmap_for_lines_in_cell(bm, c, r, [&](int ln) {
                if (blocked || !line_is_solid(m, ln)) return;
                const LinedefRaw& L = m.lines[ln];
                if ((uint16_t)L.v1 >= (uint16_t)m.numVerts || (uint16_t)L.v2 >= (uint16_t)m.numVerts) return;
                const VertexRaw& A = m.verts[L.v1];
                const VertexRaw& B = m.verts[L.v2];
                if (segs_intersect(x0, y0, x1, y1, A.x, A.y, B.x, B.y)) { blocked = true; return; }
                float dTo   = point_seg_dist2(x1, y1, A.x, A.y, B.x, B.y);
                float dFrom = point_seg_dist2(x0, y0, A.x, A.y, B.x, B.y);
                if (dTo < r2 && dTo < dFrom) blocked = true;
            });
    return blocked;
}

// Per-axis slide: commit X if its component is clear, then Y against the updated X. A
// blocked axis keeps its coordinate, so the player slides along walls and stops at corners.
inline Vec2 collide_move(const Map& m, const Blockmap& bm, Vec2 from,
                         float dx, float dy, float radius) {
    Vec2 p = from;
    float nx = p.x + dx;
    if (!move_blocked(m, bm, p.x, p.y, nx, p.y, radius)) p.x = nx;
    float ny = p.y + dy;
    if (!move_blocked(m, bm, p.x, p.y, p.x, ny, radius)) p.y = ny;
    return p;
}

} // namespace doom
