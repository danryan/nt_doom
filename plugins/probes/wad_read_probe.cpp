#include <distingnt/api.h>
#include <distingnt/wav.h>
#include <new>
#include <cstring>

#include "doom/arena.h"
#include "doom/wad.h"
#include "doom/geom.h"
#include "doom/palette.h"

// WAD read door plus the P1 parse stack, on device. Reads the WAV-smuggled
// synthetic WAD into a DRAM arena via NT_readSampleFrames (the firmware strips
// the WAV header; P0 Spike A proved the PCM is byte-exact), then runs the real P1
// code on the DRAM bytes: wad_open, map_load, palette_load, colormap_load.
//
// Pass = on-screen values match the synthetic fixture:
//   IWAD ok, numVerts == 4, numNodes == 1, nodes[0].x == 10,
//   gray[255] == 15, gray[0] == 0, shade_gray(255, light 0) == 15.
//
// The synthetic WAD is 9968 bytes => 4984 16-bit frames. The file is auto-located
// by NAME (a frame-count match collides with real audio samples on a populated
// card). Place it with: harness/tools/wad_build <f>.wad; harness/tools/wav_wrap
// <f>.wad <f>.wav; push to any existing /samples folder as TESTMAP.wav via
// harness/scripts/push_file_to_device.py. nt_helper cannot set numeric params over
// MCP, so the name match avoids any front-panel dialing.
static const int kWadFrames = 4984;

// True if `name` contains the substring `sub` (no libc; strstr is not in the
// firmware-resolved set).
static bool name_has(const char* name, const char* sub) {
    if (!name) return false;
    for (int i = 0; name[i]; ++i) {
        int j = 0;
        while (sub[j] && name[i + j] == sub[j]) ++j;
        if (!sub[j]) return true;
    }
    return false;
}

struct _wadProbe : public _NT_algorithm {
    uint8_t* dram;            // DRAM grant base (arena backing + read target)
    uint32_t dramBytes;
    volatile bool readDone;
    bool readOk;
    bool scanned;             // one-shot locate + issue read
    bool parsed;              // one-shot parse once the read completes
    int  foundFolder, foundSample;
    // cached parse results for draw()
    bool wadOk, mapOk, palOk;
    int  numLumps, numVerts, numNodes, node0x;
    int  gray255, gray0, shade255;
    uint8_t head6[6];         // first 6 WAD bytes (read-door visibility)
};

static _NT_wavRequest g_req;   // must persist across the async read

static void readCb(void* data, bool success) {
    auto* a = (_wadProbe*)data; a->readOk = success; a->readDone = true;
}

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
    req.numParameters = 0;
    req.sram = sizeof(_wadProbe);
    req.dram = 64 * 1024;     // ample for the ~10 KB WAD plus arena alignment
}

_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,
                         const _NT_algorithmRequirements& req, const int32_t*) {
    auto* a = new (ptrs.sram) _wadProbe();
    a->parameters = nullptr; a->parameterPages = nullptr;
    a->dram = (uint8_t*)ptrs.dram; a->dramBytes = req.dram;
    a->readDone = false; a->readOk = false; a->scanned = false; a->parsed = false;
    a->foundFolder = -1; a->foundSample = -1;
    a->wadOk = a->mapOk = a->palOk = false;
    a->numLumps = a->numVerts = a->numNodes = a->node0x = -1;
    a->gray255 = a->gray0 = a->shade255 = -1;
    memset(a->head6, 0, 6);
    return a;
}

static void runParse(_wadProbe* a) {
    uint32_t wadLen = (uint32_t)kWadFrames * 2;   // 2 WAD bytes per 16-bit frame

    // The whole WAD was read to a->dram; model the arena owning that span so the
    // bump pointer reflects real on-device use of the P1 allocator.
    doom::Arena arena;
    doom::arena_init(arena, a->dram, a->dramBytes);
    uint8_t* wadBytes = (uint8_t*)doom::arena_alloc(arena, wadLen, 8);
    if (!wadBytes) return;
    for (int i = 0; i < 6; ++i) a->head6[i] = wadBytes[i];

    doom::Wad w;
    a->wadOk = doom::wad_open(wadBytes, wadLen, w);
    if (!a->wadOk) return;
    a->numLumps = w.numLumps;

    doom::Map m;
    a->mapOk = doom::map_load(w, "E1M1", m);
    if (a->mapOk) {
        a->numVerts = m.numVerts;
        a->numNodes = m.numNodes;
        a->node0x   = (m.numNodes > 0) ? m.nodes[0].x : -1;
    }

    doom::Palette pal;
    a->palOk = doom::palette_load(w, pal);
    if (a->palOk) {
        a->gray255 = pal.gray[255];
        a->gray0   = pal.gray[0];
        doom::Colormap cm;
        if (doom::colormap_load(w, cm))
            a->shade255 = doom::shade_gray(pal, cm, 255, 0);
    }
}

void step(_NT_algorithm* self, float*, int) {
    auto* a = (_wadProbe*)self;
    if (!NT_isSdCardMounted()) return;

    if (!a->scanned) {
        a->scanned = true;
        int nf = (int)NT_getNumSampleFolders();
        for (int f = 0; f < nf && a->foundFolder < 0; ++f) {
            _NT_wavFolderInfo finfo; NT_getSampleFolderInfo((uint32_t)f, finfo);
            int nfiles = (int)finfo.numSampleFiles;
            for (int s = 0; s < nfiles; ++s) {
                _NT_wavInfo info; NT_getSampleFileInfo((uint32_t)f, (uint32_t)s, info);
                if (info.bits == kNT_WavBits16 && name_has(info.name, "TESTMAP")) {
                    a->foundFolder = f; a->foundSample = s; break;
                }
            }
        }
        if (a->foundFolder < 0) return;

        // Read the whole WAD into DRAM in one request (device strips the header).
        g_req.folder = (uint32_t)a->foundFolder; g_req.sample = (uint32_t)a->foundSample;
        g_req.dst = a->dram; g_req.numFrames = (uint32_t)kWadFrames; g_req.startOffset = 0;
        g_req.channels = kNT_WavMono; g_req.bits = kNT_WavBits16;
        g_req.progress = kNT_WavNoProgress; g_req.callback = readCb; g_req.callbackData = a;
        NT_readSampleFrames(g_req);
        return;
    }

    if (a->readDone && a->readOk && !a->parsed) {
        a->parsed = true;
        runParse(a);
    }
}

static void hex2(char* o, uint8_t b) {
    const char* h = "0123456789ABCDEF"; o[0] = h[(b >> 4) & 0xF]; o[1] = h[b & 0xF];
}
static void label_num(int x, int y, const char* lbl, int val, int lx) {
    char b[12]; int l = NT_intToString(b, val); b[l] = 0;
    NT_drawText(x, y, lbl); NT_drawText(x + lx, y, b);
}

bool draw(_NT_algorithm* self) {
    auto* a = (_wadProbe*)self;
    label_num(0,   8, "f/s:", a->foundFolder, 28);
    label_num(40,  8, "/",    a->foundSample, 8);
    NT_drawText(80, 8, a->readDone ? (a->readOk ? "read OK" : "read FAIL") : "read ...");
    NT_drawText(180,8, a->wadOk ? "IWAD OK" : "IWAD --");

    char hx[20]; int p = 0;
    for (int i = 0; i < 6; ++i) { hex2(hx + p, a->head6[i]); p += 2; hx[p++] = ' '; }
    hx[p] = 0;
    NT_drawText(0, 18, "b0:"); NT_drawText(24, 18, hx);

    label_num(0,  30, "vtx:",   a->numVerts, 28);
    label_num(70, 30, "nodes:", a->numNodes, 44);
    label_num(160,30, "nx:",    a->node0x,   20);
    label_num(0,  42, "g255:",  a->gray255,  36);
    label_num(70, 42, "g0:",    a->gray0,    20);
    label_num(130,42, "shd:",   a->shade255, 30);

    bool pass = a->wadOk && a->mapOk && a->palOk &&
                a->numVerts == 4 && a->numNodes == 1 && a->node0x == 10 &&
                a->gray255 == 15 && a->gray0 == 0 && a->shade255 == 15;
    NT_drawText(0, 56, pass ? "P1 SMOKE: PASS" : "P1 SMOKE: ...");
    return true;   // suppress firmware param overlay
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('W','d','R','d'),
    .name = "WAD read probe",
    .description = "Reads a WAV-smuggled WAD into DRAM and runs the P1 parse stack.",
    .numSpecifications = 0,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .step = step,
    .draw = draw,
    .tags = kNT_tagUtility,
};
extern "C" __attribute__((visibility("default"))) uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
    case kNT_selector_version:      return kNT_apiVersionCurrent;
    case kNT_selector_numFactories: return 1;
    case kNT_selector_factoryInfo:  return (uintptr_t)(data==0?&factory:nullptr);
    }
    return 0;
}
