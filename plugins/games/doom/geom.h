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
    const SegRaw& s = m.segs[segIndex];
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
    return (uint16_t)m.lines[m.segs[segIndex].linedef].back == 0xFFFFu;
}

} // namespace doom
