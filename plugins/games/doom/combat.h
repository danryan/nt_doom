#pragma once
#include <cstdint>
#include "geom.h"
#include "collision.h"

namespace doom {

// Thing-type to sprite-name (clean-room from the documented Doom thing table). Covers the
// types that appear in E1M1: monsters, items, ammo, keys, and decorations. Non-drawable
// types (player starts 1-4, deathmatch start 11, teleport destination 14) and unknown types
// are simply absent and resolve to null (skipped, no placeholder).
struct ThingSprite { int16_t type; char name[4]; };

static const ThingSprite kThingSprites[] = {
    { 3004, {'P','O','S','S'} },   // zombieman
    {    9, {'S','P','O','S'} },   // shotgun guy
    { 3001, {'T','R','O','O'} },   // imp
    { 3002, {'S','A','R','G'} },   // demon
    { 2035, {'B','A','R','1'} },   // barrel
    { 2014, {'B','O','N','1'} },   // health bonus
    { 2015, {'B','O','N','2'} },   // armor bonus
    { 2018, {'A','R','M','1'} },   // green armor
    { 2019, {'A','R','M','2'} },   // blue armor
    { 2011, {'S','T','I','M'} },   // stimpack
    { 2012, {'M','E','D','I'} },   // medikit
    { 2013, {'S','O','U','L'} },   // soulsphere
    { 2007, {'C','L','I','P'} },   // ammo clip
    { 2048, {'A','M','M','O'} },   // box of bullets
    { 2046, {'B','R','O','K'} },   // box of rockets
    { 2001, {'S','H','O','T'} },   // shotgun
    { 2002, {'M','G','U','N'} },   // chaingun
    { 2005, {'C','S','A','W'} },   // chainsaw
    {    8, {'B','P','A','K'} },   // backpack
    {    5, {'B','K','E','Y'} },   // blue keycard
    {    6, {'Y','K','E','Y'} },   // yellow keycard
    {   13, {'R','K','E','Y'} },   // red keycard
    { 2028, {'C','O','L','U'} },   // floor lamp
    {   34, {'C','A','N','D'} },   // candle
    {   35, {'C','B','R','A'} },   // candelabra
    {   47, {'S','M','I','T'} },   // stalagmite
    {   48, {'E','L','E','C'} },   // tech pillar
};

inline const char* thing_sprite_name(int type) {
    const int n = (int)(sizeof(kThingSprites) / sizeof(kThingSprites[0]));
    for (int i = 0; i < n; ++i)
        if (kThingSprites[i].type == type) return kThingSprites[i].name;
    return nullptr;
}

// Ray (ox,oy)+t*(dx,dy) vs segment A-B; returns t (ray param, world units when |dir|=1) and
// s (segment param in [0,1]). Libm-free; e = B-A, w = A-o.
inline bool ray_seg_t(float ox, float oy, float dx, float dy,
                      float ax, float ay, float bx, float by, float& t, float& s) {
    float ex = bx - ax, ey = by - ay, wx = ax - ox, wy = ay - oy;
    float denom = dx * ey - dy * ex;
    if (denom == 0.0f) return false;
    t = (wx * ey - wy * ex) / denom;
    s = (wx * dy - wy * dx) / denom;
    return t >= 0.0f && s >= 0.0f && s <= 1.0f;
}

// Cast a unit-length ray from (ox,oy) along (dirx,diry); return the nearest drawable thing
// whose bounding circle the ray crosses in front, not beyond the nearest solid wall.
// Returns the thing index, or -1. An absent BLOCKMAP (bm.base == nullptr) means no wall
// block (wallDist = maxDist). Libm-free.
inline int hitscan_nearest(const Map& m, const Blockmap& bm, float ox, float oy,
                           float dirx, float diry, float maxDist, float thingRadius) {
    float wallDist = maxDist;
    if (bm.base) {
        float ex = ox + dirx * maxDist, ey = oy + diry * maxDist;
        float minx = ox < ex ? ox : ex, maxx = ox > ex ? ox : ex;
        float miny = oy < ey ? oy : ey, maxy = oy > ey ? oy : ey;
        auto cellClamp = [&](float v, float origin, int n) -> int {
            int c = (v < origin) ? 0 : (int)((v - origin) / (float)kBlockSize);
            if (c < 0) c = 0;
            if (c >= n) c = n - 1;
            return c;
        };
        int c0 = cellClamp(minx, bm.originX, bm.cols), c1 = cellClamp(maxx, bm.originX, bm.cols);
        int r0 = cellClamp(miny, bm.originY, bm.rows), r1 = cellClamp(maxy, bm.originY, bm.rows);
        for (int r = r0; r <= r1; ++r)
            for (int c = c0; c <= c1; ++c)
                blockmap_for_lines_in_cell(bm, c, r, [&](int ln) {
                    if (!line_is_solid(m, ln)) return;
                    const LinedefRaw& L = m.lines[ln];
                    if ((uint16_t)L.v1 >= (uint16_t)m.numVerts || (uint16_t)L.v2 >= (uint16_t)m.numVerts) return;
                    const VertexRaw& A = m.verts[L.v1];
                    const VertexRaw& B = m.verts[L.v2];
                    float t, s;
                    if (ray_seg_t(ox, oy, dirx, diry, A.x, A.y, B.x, B.y, t, s) && t < wallDist)
                        wallDist = t;
                });
    }

    int best = -1;
    float bestT = wallDist;                       // only accept hits closer than the wall
    float r2 = thingRadius * thingRadius;
    for (int i = 0; i < m.numThings; ++i) {
        if (!thing_sprite_name(m.things[i].type)) continue;
        float cx = (float)m.things[i].x, cy = (float)m.things[i].y;
        float tc = (cx - ox) * dirx + (cy - oy) * diry;
        if (tc <= 0.0f) continue;                 // behind the player
        float px = ox + dirx * tc, py = oy + diry * tc;
        float perp2 = (cx - px) * (cx - px) + (cy - py) * (cy - py);
        if (perp2 > r2) continue;                 // ray misses the bounding circle
        if (tc < bestT) { bestT = tc; best = i; }
    }
    return best;
}

} // namespace doom
