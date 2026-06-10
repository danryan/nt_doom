#pragma once
#include <cstdint>
#include "wad.h"

namespace doom {

#pragma pack(push, 1)
struct VertexRaw  { int16_t x, y; };
struct SegRaw     { int16_t v1, v2, angle, linedef, side, offset; };
struct SubsecRaw  { int16_t numSegs, firstSeg; };
struct SectorRaw  { int16_t floorH, ceilH; char floorTex[8], ceilTex[8]; int16_t light, special, tag; };
struct LinedefRaw { int16_t v1, v2, flags, special, tag, front, back; };
struct SidedefRaw { int16_t xoff, yoff; char upper[8], lower[8], middle[8]; int16_t sector; };
struct NodeRaw {
    int16_t  x, y, dx, dy;     // partition line origin and delta
    int16_t  bbox[2][4];       // [child][top, bottom, left, right]
    uint16_t child[2];         // right, left; high bit (0x8000) => subsector index
};
#pragma pack(pop)

// A node child is a subsector leaf when the high bit is set; the low 15 bits are
// the subsector or node index.
inline bool     node_child_is_subsector(uint16_t c) { return (c & 0x8000u) != 0; }
inline uint16_t node_child_index(uint16_t c)        { return (uint16_t)(c & 0x7FFFu); }

struct Map {
    const VertexRaw*  verts   = nullptr; int32_t numVerts   = 0;
    const SegRaw*     segs    = nullptr; int32_t numSegs    = 0;
    const SubsecRaw*  ssecs   = nullptr; int32_t numSsecs   = 0;
    const SectorRaw*  sectors = nullptr; int32_t numSectors = 0;
    const LinedefRaw* lines   = nullptr; int32_t numLines   = 0;
    const SidedefRaw* sides   = nullptr; int32_t numSides   = 0;
    const NodeRaw*    nodes   = nullptr; int32_t numNodes   = 0;
};

inline bool map_load(const Wad& w, const char* mapName, Map& m) {
    int32_t base = wad_find_lump(w, mapName);
    if (base < 0) return false;
    auto lump = [&](const char* nm, const uint8_t** p, int32_t recSize, int32_t* count) -> bool {
        int32_t i = wad_find_lump(w, nm, base);
        if (i < 0) return false;
        *p = wad_lump_ptr(w, i);
        *count = recSize ? (int32_t)(wad_lump_size(w, i) / recSize) : 0;
        return true;
    };
    const uint8_t* p;
    if (lump("VERTEXES", &p, sizeof(VertexRaw),  &m.numVerts))   m.verts   = (const VertexRaw*)p;   else return false;
    if (lump("SEGS",     &p, sizeof(SegRaw),     &m.numSegs))    m.segs    = (const SegRaw*)p;      else return false;
    if (lump("SSECTORS", &p, sizeof(SubsecRaw),  &m.numSsecs))   m.ssecs   = (const SubsecRaw*)p;   else return false;
    if (lump("SECTORS",  &p, sizeof(SectorRaw),  &m.numSectors)) m.sectors = (const SectorRaw*)p;   else return false;
    if (lump("LINEDEFS", &p, sizeof(LinedefRaw), &m.numLines))   m.lines   = (const LinedefRaw*)p;  else return false;
    if (lump("SIDEDEFS", &p, sizeof(SidedefRaw), &m.numSides))   m.sides   = (const SidedefRaw*)p;  else return false;
    int32_t ni = wad_find_lump(w, "NODES", base);
    if (ni < 0) { m.nodes = nullptr; m.numNodes = 0; }
    else {
        m.nodes    = (const NodeRaw*)wad_lump_ptr(w, ni);
        m.numNodes = (int32_t)(wad_lump_size(w, ni) / sizeof(NodeRaw));
    }
    return true;
}

inline int16_t seg_sidedef_index(const Map& m, int32_t segIndex) {
    if (segIndex < 0 || segIndex >= m.numSegs) return (int16_t)0xFFFF;
    const SegRaw& s = m.segs[segIndex];
    if ((uint16_t)s.linedef >= (uint16_t)m.numLines) return (int16_t)0xFFFF;
    const LinedefRaw& ld = m.lines[s.linedef];
    return s.side == 0 ? ld.front : ld.back;   // 0xFFFF when absent
}
inline const SidedefRaw* seg_sidedef(const Map& m, int32_t segIndex) {
    int16_t si = seg_sidedef_index(m, segIndex);
    if ((uint16_t)si == 0xFFFFu || si < 0 || si >= m.numSides) return nullptr;
    return &m.sides[si];
}
inline const SectorRaw* seg_sector(const Map& m, int32_t segIndex) {
    const SidedefRaw* sd = seg_sidedef(m, segIndex);
    if (!sd || sd->sector < 0 || sd->sector >= m.numSectors) return nullptr;
    return &m.sectors[sd->sector];
}
inline bool seg_is_one_sided(const Map& m, int32_t segIndex) {
    if (segIndex < 0 || segIndex >= m.numSegs) return true;
    int32_t ld = m.segs[segIndex].linedef;
    if ((uint16_t)ld >= (uint16_t)m.numLines) return true;
    return (uint16_t)m.lines[ld].back == 0xFFFFu;
}

// Doom BLOCKMAP: a uniform 128-unit grid where each cell lists the linedefs that cross
// it, used as the collision broadphase. Header is originX, originY, cols, rows (int16);
// then cols*rows uint16 word offsets (units of int16 from the lump start) to per-cell
// blocklists; each blocklist is a leading 0x0000 word, a run of uint16 linedef indices,
// and a 0xFFFF terminator.
static const int kBlockSize = 128;
struct Blockmap {
    const uint8_t*  base    = nullptr;
    int16_t         originX = 0, originY = 0;
    uint16_t        cols    = 0, rows = 0;
    const uint16_t* offsets = nullptr;   // cols*rows entries
    uint32_t        words   = 0;         // lumpSize / 2, for bounds
};

inline bool blockmap_load(const Wad& w, const char* mapName, Blockmap& bm) {
    int32_t base = wad_find_lump(w, mapName);
    if (base < 0) return false;
    int32_t bi = wad_find_lump(w, "BLOCKMAP", base);
    if (bi < 0) return false;
    const uint8_t* p = wad_lump_ptr(w, bi);
    uint32_t sz = wad_lump_size(w, bi);
    if (sz < 8) return false;
    const int16_t* h = (const int16_t*)p;
    bm.base    = p;
    bm.originX = h[0]; bm.originY = h[1];
    bm.cols    = (uint16_t)h[2]; bm.rows = (uint16_t)h[3];
    bm.offsets = (const uint16_t*)(p + 8);
    bm.words   = sz / 2;
    if (4u + (uint32_t)bm.cols * bm.rows > bm.words) return false;   // table must fit
    return true;
}

// World point to cell coords. False when below the origin or past the grid. The origin is
// the grid minimum, so x >= originX implies the truncating cast equals floor.
inline bool blockmap_cell_of(const Blockmap& bm, float x, float y, int& col, int& row) {
    if (x < bm.originX || y < bm.originY) return false;
    int c = (int)((x - bm.originX) / (float)kBlockSize);
    int r = (int)((y - bm.originY) / (float)kBlockSize);
    if (c < 0 || c >= bm.cols || r < 0 || r >= bm.rows) return false;
    col = c; row = r; return true;
}

// Call fn(int lineIndex) for each linedef listed in cell (col,row). No-op when out of
// range or the offset points past the lump. Skips the mandatory leading 0x0000 marker
// unconditionally (so a genuine linedef index 0 is not mistaken for it).
template<class F>
inline void blockmap_for_lines_in_cell(const Blockmap& bm, int col, int row, F fn) {
    if (col < 0 || col >= bm.cols || row < 0 || row >= bm.rows) return;
    uint32_t idx = (uint32_t)row * bm.cols + (uint32_t)col;
    uint32_t off = bm.offsets[idx];
    if (off + 1 >= bm.words) return;
    const int16_t* wptr = (const int16_t*)bm.base;
    for (uint32_t i = off + 1; i < bm.words; ++i) {
        uint16_t v = (uint16_t)wptr[i];
        if (v == 0xFFFFu) break;
        fn((int)v);
    }
}

} // namespace doom
