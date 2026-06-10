#pragma once
#include <algorithm>
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

    // NODES: one 28-byte record exercising the typed view. Partition (10,20)+(30,40);
    // right bbox [100,-100,-50,50], left bbox [200,-200,-60,60]; child[0]=0x8000
    // (subsector leaf, index 0), child[1]=0x0000 (interior node, index 0).
    { std::vector<uint8_t> d;
      w16(d,10); w16(d,20); w16(d,30); w16(d,40);
      int16_t bb[2][4] = {{100,-100,-50,50},{200,-200,-60,60}};
      for (int c=0;c<2;++c) for (int k=0;k<4;++k) w16(d,bb[c][k]);
      w16(d,(int16_t)0x8000); w16(d,(int16_t)0x0000);
      add("NODES", d); }

    // SECTORS: floorh,ceilh,floortex,ceiltex,light,special,tag.
    { std::vector<uint8_t> d; w16(d,0); w16(d,128); name8(d,"FLAT"); name8(d,"FLAT"); w16(d,200); w16(d,0); w16(d,0); add("SECTORS", d); }

    // PLAYPAL: one 256-entry RGB palette (768 bytes). Gray ramp: entry i is
    // (i,i,i), so a Rec.601 luma quantizes to gray = i >> 4. Real Doom ships 14
    // palettes; one exercises the parser and the renderer only uses palette 0.
    { std::vector<uint8_t> d;
      for (int i=0;i<256;++i){ d.push_back((uint8_t)i); d.push_back((uint8_t)i); d.push_back((uint8_t)i); }
      add("PLAYPAL", d); }

    // COLORMAP: 34 maps of 256 bytes. Uniform darkening: map[m][i] = i*(33-m)/33.
    // Map 0 is identity (brightest), map 33 is all-zero (darkest). Deterministic.
    { std::vector<uint8_t> d;
      for (int m=0;m<34;++m) for (int i=0;i<256;++i) d.push_back((uint8_t)((i*(33-m))/33));
      add("COLORMAP", d); }

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

// Builds a valid IWAD for P3 movement/collision: a 512x512 enclosed room with four
// solid one-sided walls, one sector, one subsector, NODES omitted (map_load allows it;
// numNodes==0 makes bsp_visit_order enumerate the single subsector so render works),
// a player-1 start at the center (256,256), gray-ramp PLAYPAL/COLORMAP, and a generated
// BLOCKMAP (origin (0,0), 5x5 grid of 128-unit blocks, conservative bbox-overlap cell
// assignment so the collision broadphase never misses a wall near the player).
inline std::vector<uint8_t> build_move_test_wad() {
    using namespace wadbuild;
    std::vector<Lump> lumps;
    auto add = [&](const char* nm, std::vector<uint8_t> d) {
        Lump L; memset(L.name, 0, 8);
        for (int i = 0; i < 8 && nm[i]; ++i) L.name[i] = nm[i];
        L.data = std::move(d); lumps.push_back(std::move(L));
    };

    add("E1M1", {});

    // THINGS: player-1 start at the room center (256,256), facing +x (angle 0).
    { std::vector<uint8_t> d; w16(d,256); w16(d,256); w16(d,0); w16(d,1); w16(d,7); add("THINGS", d); }

    // VERTEXES: square room corners.
    const int16_t vx[4]={0,512,512,0}, vy[4]={0,0,512,512};
    { std::vector<uint8_t> d; for (int i=0;i<4;++i){ w16(d,vx[i]); w16(d,vy[i]); } add("VERTEXES", d); }

    // LINEDEFS: four one-sided blocking walls (flags=1=ML_BLOCKING, back=0xFFFF).
    const int lp[4][2]={{0,1},{1,2},{2,3},{3,0}};
    { std::vector<uint8_t> d;
      for (int i=0;i<4;++i){ w16(d,(int16_t)lp[i][0]); w16(d,(int16_t)lp[i][1]); w16(d,1); w16(d,0); w16(d,0); w16(d,(int16_t)i); w16(d,(int16_t)0xFFFF); }
      add("LINEDEFS", d); }

    // SIDEDEFS: all to sector 0, middle "WALL".
    { std::vector<uint8_t> d;
      for (int i=0;i<4;++i){ w16(d,0); w16(d,0); name8(d,"-"); name8(d,"-"); name8(d,"WALL"); w16(d,0); }
      add("SIDEDEFS", d); }

    // SEGS: one per line.
    { std::vector<uint8_t> d;
      for (int i=0;i<4;++i){ w16(d,(int16_t)lp[i][0]); w16(d,(int16_t)lp[i][1]); w16(d,0); w16(d,(int16_t)i); w16(d,0); w16(d,0); }
      add("SEGS", d); }

    // SSECTORS: one subsector, all four segs.
    { std::vector<uint8_t> d; w16(d,4); w16(d,0); add("SSECTORS", d); }

    // SECTORS: floor 0, ceiling 128, light 200. (No NODES lump: numNodes==0.)
    { std::vector<uint8_t> d; w16(d,0); w16(d,128); name8(d,"FLAT"); name8(d,"FLAT"); w16(d,200); w16(d,0); w16(d,0); add("SECTORS", d); }

    // PLAYPAL: gray ramp.
    { std::vector<uint8_t> d;
      for (int i=0;i<256;++i){ d.push_back((uint8_t)i); d.push_back((uint8_t)i); d.push_back((uint8_t)i); }
      add("PLAYPAL", d); }

    // COLORMAP: 34 darkening maps.
    { std::vector<uint8_t> d;
      for (int m=0;m<34;++m) for (int i=0;i<256;++i) d.push_back((uint8_t)((i*(33-m))/33));
      add("COLORMAP", d); }

    // BLOCKMAP: origin (0,0), cols=rows=5, block size 128. A cell lists a linedef when
    // the linedef's bounding box overlaps the cell rectangle [c*128,(c+1)*128] x
    // [r*128,(r+1)*128]. Words are int16; offsets are word offsets from the lump start.
    { const int ox=0, oy=0, cols=5, rows=5, BS=128;
      auto lbb=[&](int ln,int&x0,int&y0,int&x1,int&y1){
          int a=lp[ln][0], b=lp[ln][1];
          x0=std::min(vx[a],vx[b]); x1=std::max(vx[a],vx[b]);
          y0=std::min(vy[a],vy[b]); y1=std::max(vy[a],vy[b]); };
      std::vector<std::vector<int16_t>> cellLines(cols*rows);
      for (int r=0;r<rows;++r) for (int c=0;c<cols;++c) {
          int cx0=ox+c*BS, cx1=cx0+BS, cy0=oy+r*BS, cy1=cy0+BS;
          for (int ln=0; ln<4; ++ln) {
              int x0,y0,x1,y1; lbb(ln,x0,y0,x1,y1);
              if (x0<=cx1 && x1>=cx0 && y0<=cy1 && y1>=cy0) cellLines[r*cols+c].push_back((int16_t)ln);
          }
      }
      std::vector<int16_t> words;
      words.push_back((int16_t)ox); words.push_back((int16_t)oy);
      words.push_back((int16_t)cols); words.push_back((int16_t)rows);
      int offTable = (int)words.size();
      for (int i=0;i<cols*rows;++i) words.push_back(0);   // offset placeholders
      for (int i=0;i<cols*rows;++i) {
          words[offTable+i] = (int16_t)words.size();      // word offset to this blocklist
          words.push_back(0);                              // leading 0x0000
          for (int16_t ln : cellLines[i]) words.push_back(ln);
          words.push_back((int16_t)0xFFFF);                // terminator
      }
      std::vector<uint8_t> d; for (int16_t wd : words) w16(d, wd);
      add("BLOCKMAP", d); }

    // Assemble: 12-byte header, lump data, then directory.
    std::vector<uint8_t> out;
    auto o32 = [&](std::vector<uint8_t>& v, int32_t x){
        v.push_back(x & 0xFF); v.push_back((x>>8)&0xFF); v.push_back((x>>16)&0xFF); v.push_back((x>>24)&0xFF); };
    out.push_back('I'); out.push_back('W'); out.push_back('A'); out.push_back('D');
    o32(out, (int32_t)lumps.size());
    int32_t dirOffsetPos = (int32_t)out.size();
    o32(out, 0);
    std::vector<std::pair<int32_t,int32_t>> dir;
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

// Builds a valid IWAD for the P2 BSP renderer: two one-sided walls in two
// subsectors split by one real acyclic node, posed so the near wall (x=100)
// occludes the far wall (x=300) only in the shared central columns. Includes
// PLAYPAL/COLORMAP (gray ramp + 34 darkening maps) and TEXTURE1/PNAMES/PWALL.
inline std::vector<uint8_t> build_bsp_test_wad() {
    using namespace wadbuild;
    std::vector<Lump> lumps;
    auto add = [&](const char* nm, std::vector<uint8_t> d) {
        Lump L; memset(L.name, 0, 8);
        for (int i = 0; i < 8 && nm[i]; ++i) L.name[i] = nm[i];
        L.data = std::move(d); lumps.push_back(std::move(L));
    };

    add("E1M1", {});

    // THINGS: player-1 start at (0,0) facing +x (angle 0).
    { std::vector<uint8_t> d; w16(d,0); w16(d,0); w16(d,0); w16(d,1); w16(d,7); add("THINGS", d); }

    // VERTEXES: near wall v0,v1 at x=100; far wall v2,v3 at x=300.
    { std::vector<uint8_t> d;
      int16_t xs[4]={100,100,300,300}, ys[4]={-40,40,-200,200};
      for (int i=0;i<4;++i){ w16(d,xs[i]); w16(d,ys[i]); } add("VERTEXES", d); }

    // LINEDEFS: v1,v2,flags,special,tag,front,back(0xFFFF). Two one-sided lines.
    { std::vector<uint8_t> d;
      // line0: v0->v1, front sidedef 0. line1: v2->v3, front sidedef 1.
      w16(d,0); w16(d,1); w16(d,1); w16(d,0); w16(d,0); w16(d,0); w16(d,(int16_t)0xFFFF);
      w16(d,2); w16(d,3); w16(d,1); w16(d,0); w16(d,0); w16(d,1); w16(d,(int16_t)0xFFFF);
      add("LINEDEFS", d); }

    // SIDEDEFS: xoff,yoff,upper,lower,middle,sector. side0->sector0, side1->sector1.
    { std::vector<uint8_t> d;
      w16(d,0); w16(d,0); name8(d,"-"); name8(d,"-"); name8(d,"WALL"); w16(d,0);
      w16(d,0); w16(d,0); name8(d,"-"); name8(d,"-"); name8(d,"WALL"); w16(d,1);
      add("SIDEDEFS", d); }

    // SEGS: v1,v2,angle,linedef,side,offset. seg0=near (line0), seg1=far (line1).
    { std::vector<uint8_t> d;
      w16(d,0); w16(d,1); w16(d,0); w16(d,0); w16(d,0); w16(d,0);
      w16(d,2); w16(d,3); w16(d,0); w16(d,1); w16(d,0); w16(d,0);
      add("SEGS", d); }

    // SSECTORS: ssec0=near (1 seg from 0), ssec1=far (1 seg from 1).
    { std::vector<uint8_t> d; w16(d,1); w16(d,0); w16(d,1); w16(d,1); add("SSECTORS", d); }

    // NODES: partition x=200, dir (0,100). child[0]=far ssec1, child[1]=near ssec0.
    { std::vector<uint8_t> d;
      w16(d,200); w16(d,0); w16(d,0); w16(d,100);
      int16_t bb[2][4] = {{200,-200,300,300},{40,-40,100,100}};
      for (int c=0;c<2;++c) for (int k=0;k<4;++k) w16(d,bb[c][k]);
      w16(d,(int16_t)(0x8000|1)); w16(d,(int16_t)(0x8000|0));
      add("NODES", d); }

    // SECTORS: floorh,ceilh,floortex,ceiltex,light,special,tag. Near brighter.
    { std::vector<uint8_t> d;
      w16(d,0); w16(d,128); name8(d,"FLAT"); name8(d,"FLAT"); w16(d,224); w16(d,0); w16(d,0);
      w16(d,0); w16(d,128); name8(d,"FLAT"); name8(d,"FLAT"); w16(d,160); w16(d,0); w16(d,0);
      add("SECTORS", d); }

    // PLAYPAL: gray ramp, entry i = (i,i,i).
    { std::vector<uint8_t> d;
      for (int i=0;i<256;++i){ d.push_back((uint8_t)i); d.push_back((uint8_t)i); d.push_back((uint8_t)i); }
      add("PLAYPAL", d); }

    // COLORMAP: 34 maps, map[m][i] = i*(33-m)/33.
    { std::vector<uint8_t> d;
      for (int m=0;m<34;++m) for (int i=0;i<256;++i) d.push_back((uint8_t)((i*(33-m))/33));
      add("COLORMAP", d); }

    // PNAMES: one patch name PWALL.
    { std::vector<uint8_t> d;
      auto o32=[&](int32_t x){ d.push_back(x&0xFF); d.push_back((x>>8)&0xFF); d.push_back((x>>16)&0xFF); d.push_back((x>>24)&0xFF); };
      o32(1); name8(d,"PWALL"); add("PNAMES", d); }

    // TEXTURE1: one 16x16 texture WALL from patch 0 (PWALL) at origin (0,0).
    { std::vector<uint8_t> d;
      auto o32=[&](int32_t x){ d.push_back(x&0xFF); d.push_back((x>>8)&0xFF); d.push_back((x>>16)&0xFF); d.push_back((x>>24)&0xFF); };
      o32(1);            // numTextures
      o32(4 + 4);        // offset[0]: after the 4-byte count + 4-byte offset table
      name8(d,"WALL"); o32(0); w16(d,16); w16(d,16); o32(0); w16(d,1);  // maptexture
      w16(d,0); w16(d,0); w16(d,0); w16(d,0); w16(d,0);                  // mappatch
      add("TEXTURE1", d); }

    // PWALL: 16x16 patch, every column a full post, pixel[row] = row (0..15).
    { std::vector<uint8_t> d;
      auto o32=[&](int32_t x){ d.push_back(x&0xFF); d.push_back((x>>8)&0xFF); d.push_back((x>>16)&0xFF); d.push_back((x>>24)&0xFF); };
      w16(d,16); w16(d,16); w16(d,0); w16(d,0);     // width,height,left,top
      int32_t colStart = 8 + 16*4;                   // header + offset table
      int32_t colBytes = 1+1+1+16+1+1;               // topdelta,length,pad,16px,pad,0xFF
      for (int c=0;c<16;++c) o32(colStart + c*colBytes);
      for (int c=0;c<16;++c) {
          d.push_back(0); d.push_back(16); d.push_back(0);   // topdelta=0,length=16,pad
          for (int row=0;row<16;++row) d.push_back((uint8_t)row);
          d.push_back(0); d.push_back(0xFF);                  // pad, terminator
      }
      add("PWALL", d); }

    // Assemble: 12-byte header, lump data, then directory.
    std::vector<uint8_t> out;
    auto o32 = [&](std::vector<uint8_t>& v, int32_t x){
        v.push_back(x & 0xFF); v.push_back((x>>8)&0xFF); v.push_back((x>>16)&0xFF); v.push_back((x>>24)&0xFF); };
    out.push_back('I'); out.push_back('W'); out.push_back('A'); out.push_back('D');
    o32(out, (int32_t)lumps.size());
    int32_t dirOffsetPos = (int32_t)out.size();
    o32(out, 0);
    std::vector<std::pair<int32_t,int32_t>> dir;
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
