#pragma once
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace wadbuild {

inline void w16(std::vector<uint8_t>& v, int16_t x) {
    v.push_back((uint8_t)(x & 0xFF)); v.push_back((uint8_t)((x >> 8) & 0xFF));
}
inline void name8(std::vector<uint8_t>& v, const char* s) {
    char b[8]; memset(b, 0, 8); for (int i = 0; i < 8 && s[i]; ++i) b[i] = s[i];
    for (int i = 0; i < 8; ++i) v.push_back((uint8_t)b[i]);
}

struct Lump { char name[8]; std::vector<uint8_t> data; };

} // namespace wadbuild

// Builds a valid IWAD: E1M1 marker + THINGS, VERTEXES, LINEDEFS, SIDEDEFS,
// SEGS, SSECTORS, NODES (empty), SECTORS. One 256x256 square sector.
inline std::vector<uint8_t> build_test_wad() {
    using namespace wadbuild;
    std::vector<Lump> lumps;
    auto add = [&](const char* nm, std::vector<uint8_t> d) {
        Lump L; memset(L.name, 0, 8);
        for (int i = 0; i < 8 && nm[i]; ++i) L.name[i] = nm[i];
        L.data = std::move(d); lumps.push_back(std::move(L));
    };

    add("E1M1", {});

    // THINGS: one player-1 start at (128,128) facing east. Record: x,y,angle,type,flags.
    { std::vector<uint8_t> d; w16(d,128); w16(d,128); w16(d,0); w16(d,1); w16(d,7); add("THINGS", d); }

    // VERTEXES: square corners.
    { std::vector<uint8_t> d;
      int16_t xs[4]={0,256,256,0}, ys[4]={0,0,256,256};
      for (int i=0;i<4;++i){ w16(d,xs[i]); w16(d,ys[i]); } add("VERTEXES", d); }

    // LINEDEFS: v1,v2,flags,special,tag,front,back(0xFFFF). 4 one-sided lines.
    { std::vector<uint8_t> d;
      int p[4][2]={{0,1},{1,2},{2,3},{3,0}};
      for (int i=0;i<4;++i){ w16(d,p[i][0]); w16(d,p[i][1]); w16(d,1); w16(d,0); w16(d,0); w16(d,i); w16(d,(int16_t)0xFFFF); }
      add("LINEDEFS", d); }

    // SIDEDEFS: xoff,yoff,upper,lower,middle,sector. middle="WALL".
    { std::vector<uint8_t> d;
      for (int i=0;i<4;++i){ w16(d,0); w16(d,0); name8(d,"-"); name8(d,"-"); name8(d,"WALL"); w16(d,0); }
      add("SIDEDEFS", d); }

    // SEGS: v1,v2,angle(BAM),linedef,side,offset. One seg per line.
    { std::vector<uint8_t> d;
      int p[4][2]={{0,1},{1,2},{2,3},{3,0}};
      int16_t ang[4]={0,(int16_t)16384,(int16_t)-32768,(int16_t)-16384};
      for (int i=0;i<4;++i){ w16(d,p[i][0]); w16(d,p[i][1]); w16(d,ang[i]); w16(d,i); w16(d,0); w16(d,0); }
      add("SEGS", d); }

    // SSECTORS: numsegs, firstseg.
    { std::vector<uint8_t> d; w16(d,4); w16(d,0); add("SSECTORS", d); }

    // NODES: empty (degenerate single-subsector map).
    add("NODES", {});

    // SECTORS: floorh,ceilh,floortex,ceiltex,light,special,tag.
    { std::vector<uint8_t> d; w16(d,0); w16(d,128); name8(d,"FLAT"); name8(d,"FLAT"); w16(d,200); w16(d,0); w16(d,0); add("SECTORS", d); }

    // Assemble: 12-byte header, lump data, then directory.
    std::vector<uint8_t> out;
    auto o32 = [&](std::vector<uint8_t>& v, int32_t x){
        v.push_back(x & 0xFF); v.push_back((x>>8)&0xFF); v.push_back((x>>16)&0xFF); v.push_back((x>>24)&0xFF); };
    out.push_back('I'); out.push_back('W'); out.push_back('A'); out.push_back('D');
    o32(out, (int32_t)lumps.size());
    int32_t dirOffsetPos = (int32_t)out.size();
    o32(out, 0); // patched later

    std::vector<std::pair<int32_t,int32_t>> dir; // (filePos, size)
    for (auto& L : lumps) {
        int32_t pos = (int32_t)out.size();
        out.insert(out.end(), L.data.begin(), L.data.end());
        dir.push_back({pos, (int32_t)L.data.size()});
    }
    int32_t dirStart = (int32_t)out.size();
    for (size_t i = 0; i < lumps.size(); ++i) {
        o32(out, dir[i].first); o32(out, dir[i].second);
        out.insert(out.end(), lumps[i].name, lumps[i].name + 8);
    }
    out[dirOffsetPos+0] = dirStart & 0xFF; out[dirOffsetPos+1] = (dirStart>>8)&0xFF;
    out[dirOffsetPos+2] = (dirStart>>16)&0xFF; out[dirOffsetPos+3] = (dirStart>>24)&0xFF;
    return out;
}
