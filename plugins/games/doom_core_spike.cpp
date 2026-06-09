#include <distingnt/api.h>
#include <distingnt/wav.h>
#include <distingnt/serialisation.h>
#include <new>
#include <cstring>
#include "doom_test_map.h"
#include "doom/arena.h"
#include "doom/wad.h"
#include "doom/geom.h"
#include "doom/palette.h"
#include "doom/texture.h"
#include "doom/render.h"
#include "doom/movement.h"
#include "doom/input.h"
#include "doom/collision.h"

// ARM cos_sin from a 256-entry rodata sine LUT (no libm sinf).
namespace doom {
static const float kSinLut[256] = {
    0.0000000f,0.0245412f,0.0490677f,0.0735646f,0.0980171f,0.1224107f,0.1467305f,0.1709619f,
    0.1950903f,0.2191012f,0.2429802f,0.2667128f,0.2902847f,0.3136817f,0.3368899f,0.3598950f,
    0.3826834f,0.4052413f,0.4275551f,0.4496113f,0.4713967f,0.4928982f,0.5141027f,0.5349976f,
    0.5555702f,0.5758082f,0.5956993f,0.6152316f,0.6343933f,0.6531728f,0.6715590f,0.6895405f,
    0.7071068f,0.7242471f,0.7409511f,0.7572088f,0.7730105f,0.7883464f,0.8032075f,0.8175848f,
    0.8314696f,0.8448536f,0.8577286f,0.8700870f,0.8819213f,0.8932243f,0.9039893f,0.9142098f,
    0.9238795f,0.9329928f,0.9415441f,0.9495282f,0.9569403f,0.9637761f,0.9700313f,0.9757021f,
    0.9807853f,0.9852776f,0.9891765f,0.9924795f,0.9951847f,0.9972905f,0.9987955f,0.9996988f,
    1.0000000f,0.9996988f,0.9987955f,0.9972905f,0.9951847f,0.9924795f,0.9891765f,0.9852776f,
    0.9807853f,0.9757021f,0.9700313f,0.9637761f,0.9569403f,0.9495282f,0.9415441f,0.9329928f,
    0.9238795f,0.9142098f,0.9039893f,0.8932243f,0.8819213f,0.8700870f,0.8577286f,0.8448536f,
    0.8314696f,0.8175848f,0.8032075f,0.7883464f,0.7730105f,0.7572088f,0.7409511f,0.7242471f,
    0.7071068f,0.6895405f,0.6715590f,0.6531728f,0.6343933f,0.6152316f,0.5956993f,0.5758082f,
    0.5555702f,0.5349976f,0.5141027f,0.4928982f,0.4713967f,0.4496113f,0.4275551f,0.4052413f,
    0.3826834f,0.3598950f,0.3368899f,0.3136817f,0.2902847f,0.2667128f,0.2429802f,0.2191012f,
    0.1950903f,0.1709619f,0.1467305f,0.1224107f,0.0980171f,0.0735646f,0.0490677f,0.0245412f,
    0.0000000f,-0.0245412f,-0.0490677f,-0.0735646f,-0.0980171f,-0.1224107f,-0.1467305f,-0.1709619f,
    -0.1950903f,-0.2191012f,-0.2429802f,-0.2667128f,-0.2902847f,-0.3136817f,-0.3368899f,-0.3598950f,
    -0.3826834f,-0.4052413f,-0.4275551f,-0.4496113f,-0.4713967f,-0.4928982f,-0.5141027f,-0.5349976f,
    -0.5555702f,-0.5758082f,-0.5956993f,-0.6152316f,-0.6343933f,-0.6531728f,-0.6715590f,-0.6895405f,
    -0.7071068f,-0.7242471f,-0.7409511f,-0.7572088f,-0.7730105f,-0.7883464f,-0.8032075f,-0.8175848f,
    -0.8314696f,-0.8448536f,-0.8577286f,-0.8700870f,-0.8819213f,-0.8932243f,-0.9039893f,-0.9142098f,
    -0.9238795f,-0.9329928f,-0.9415441f,-0.9495282f,-0.9569403f,-0.9637761f,-0.9700313f,-0.9757021f,
    -0.9807853f,-0.9852776f,-0.9891765f,-0.9924795f,-0.9951847f,-0.9972905f,-0.9987955f,-0.9996988f,
    -1.0000000f,-0.9996988f,-0.9987955f,-0.9972905f,-0.9951847f,-0.9924795f,-0.9891765f,-0.9852776f,
    -0.9807853f,-0.9757021f,-0.9700313f,-0.9637761f,-0.9569403f,-0.9495282f,-0.9415441f,-0.9329928f,
    -0.9238795f,-0.9142098f,-0.9039893f,-0.8932243f,-0.8819213f,-0.8700870f,-0.8577286f,-0.8448536f,
    -0.8314696f,-0.8175848f,-0.8032075f,-0.7883464f,-0.7730105f,-0.7572088f,-0.7409511f,-0.7242471f,
    -0.7071068f,-0.6895405f,-0.6715590f,-0.6531728f,-0.6343933f,-0.6152316f,-0.5956993f,-0.5758082f,
    -0.5555702f,-0.5349976f,-0.5141027f,-0.4928982f,-0.4713967f,-0.4496113f,-0.4275551f,-0.4052413f,
    -0.3826834f,-0.3598950f,-0.3368899f,-0.3136817f,-0.2902847f,-0.2667128f,-0.2429802f,-0.2191012f,
    -0.1950903f,-0.1709619f,-0.1467305f,-0.1224107f,-0.0980171f,-0.0735646f,-0.0490677f,-0.0245412f,
};
void cos_sin(float ang, float& c, float& s) {
    const float twoPi = 6.2831853f;
    float t = ang / twoPi;
    t = t - (float)(int)t; if (t < 0) t += 1.0f;
    int i = (int)(t * 256.0f) & 255;
    int j = (i + 64) & 255;   // cos = sin(ang + 90deg)
    s = kSinLut[i];
    c = kSinLut[j];
}
}

// Locate a WAV-smuggled WAD by name substring (no libc; strstr is not firmware-resolved).
static bool name_has(const char* name, const char* sub) {
    if (!name) return false;
    for (int i = 0; name[i]; ++i) {
        int j = 0;
        while (sub[j] && name[i + j] == sub[j]) ++j;
        if (!sub[j]) return true;
    }
    return false;
}

// Doom E1M1 player-1 start (open space), used when the real WAD parses. The synthetic
// fallback uses the camera origin instead.
static const float kE1M1StartX = 1056.0f, kE1M1StartY = -3616.0f, kE1M1StartA = 1.5707963f;

static const int kScrBottomRows = 8;   // overlay-suppression snapshot region (rows 56..63)

struct _doomSpike : public _NT_algorithm {
    doom::Arena arena;
    uint8_t*    dram;          // DRAM grant base (WAD read target + arena backing)
    uint32_t    dramBytes;

    doom::Map         map;
    doom::Palette     pal;
    doom::Colormap    cm;
    doom::TextureCache tex;
    doom::Blockmap    bm;
    bool texReady, bmReady;

    doom::Pose pose;
    float      lpState[4];     // CV lowpass state per axis
    float      panelFwd, panelTurn; bool panelFire;   // front-panel intent latch

    // Real-WAD async load (mirrors wad_read_probe, with a dynamic frame count).
    volatile bool readDone; bool readOk; bool scanned; bool parsed;
    int  foundFolder, foundSample; uint32_t wadFrames;

    uint8_t scrCache[kScrBottomRows * 128];   // overlay-suppression snapshot
    int     postDraw;
};

static const _NT_parameter parameters[] = {
    { .name = "Move spd",  .min = 0, .max = 1000, .def = 200, .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr },
    { .name = "Turn spd",  .min = 0, .max = 628,  .def = 200, .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr },
    { .name = "Strafe spd",.min = 0, .max = 1000, .def = 200, .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr },
    { .name = "Radius",    .min = 1, .max = 64,   .def = 16,  .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr },
    { .name = "Deadzone",  .min = 0, .max = 200,  .def = 10,  .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr },
    { .name = "Fwd bus",   .min = 1, .max = 28,   .def = 1,   .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr },
    { .name = "Turn bus",  .min = 1, .max = 28,   .def = 2,   .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr },
    { .name = "Strafe bus",.min = 1, .max = 28,   .def = 3,   .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr },
    { .name = "Fire bus",  .min = 1, .max = 28,   .def = 4,   .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr },
};
enum { kPMove, kPTurn, kPStrafe, kPRadius, kPDeadzone, kPFwdBus, kPTurnBus, kPStrafeBus, kPFireBus };
static const uint8_t page1[] = { kPMove, kPTurn, kPStrafe, kPRadius, kPDeadzone, kPFwdBus, kPTurnBus, kPStrafeBus, kPFireBus };
static const _NT_parameterPage pages[] = { { .name = "Doom", .numParams = ARRAY_SIZE(page1), .params = page1 } };
static const _NT_parameterPages parameterPages = { .numPages = 1, .pages = pages };

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_doomSpike);
    req.dram = 8 * 1024 * 1024;   // real DOOM1.WAD (~4.2 MB) plus texture-composition arena
}

_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req, const int32_t*) {
    auto* a = new (ptrs.sram) _doomSpike();
    a->parameters = parameters;
    a->parameterPages = &parameterPages;
    a->dram = (uint8_t*)ptrs.dram; a->dramBytes = req.dram;
    a->texReady = a->bmReady = false;
    a->pose = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < 4; ++i) a->lpState[i] = 0.0f;
    a->panelFwd = a->panelTurn = 0.0f; a->panelFire = false;
    a->readDone = false; a->readOk = false; a->scanned = false; a->parsed = false;
    a->foundFolder = a->foundSample = -1; a->wadFrames = 0;
    a->postDraw = 0;

    // Fallback so ADD always renders: parse the embedded synthetic WAD (rodata bytes, no
    // read) for a FLAT render. We deliberately do NOT compose textures here: the synthetic
    // PWALL renders near-black anyway (low palette indices on a gray-ramp palette), and
    // composing at construct time would write into the DRAM arena before the grant is
    // proven live (DRAM size is cached at scan time), which hard-faults on add. Textures
    // turn on only after the real WAD loads into a confirmed arena. The synthetic WAD has
    // no BLOCKMAP, so collision is inert until then.
    doom::arena_init(a->arena, a->dram, a->dramBytes);
    doom::Wad w;
    if (doom::wad_open(kDoomBspTestWad, kDoomBspTestWadLen, w)) {
        doom::map_load(w, "E1M1", a->map);
        doom::palette_load(w, a->pal);
        doom::colormap_load(w, a->cm);
    }
    a->texReady = false;
    return a;
}

static _NT_wavRequest g_req;   // must persist across the async read

static void readCb(void* data, bool success) {
    auto* a = (_doomSpike*)data; a->readOk = success; a->readDone = true;
}

// Parse the real WAD once the read completes; swap the active map/palette/textures/blockmap
// and place the player at the E1M1 start.
static void swapRealWad(_doomSpike* a) {
    uint32_t wadLen = a->wadFrames * 2;   // 2 WAD bytes per 16-bit frame
    doom::arena_reset(a->arena);
    uint8_t* wadBytes = (uint8_t*)doom::arena_alloc(a->arena, wadLen, 8);
    if (!wadBytes) return;                // keep the synthetic fallback on overflow
    doom::Wad w;
    if (!doom::wad_open(wadBytes, wadLen, w)) return;
    doom::Map m;
    if (!doom::map_load(w, "E1M1", m)) return;
    a->map = m;
    doom::palette_load(w, a->pal);
    doom::colormap_load(w, a->cm);
    a->texReady = doom::texcache_init(a->tex, w, a->arena);
    a->bmReady  = doom::blockmap_load(w, "E1M1", a->bm);
    a->pose = { kE1M1StartX, kE1M1StartY, kE1M1StartA };
}

void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    auto* a = (_doomSpike*)self;
    uint32_t sr = NT_globals.sampleRate;
    if (sr == 0) return;                  // step may run before the sample rate is set
    int numFrames = numFramesBy4 * 4;

    // Restore the bottom rows the firmware overlay overdrew after the last draw().
    if (a->postDraw > 0) {
        memcpy(NT_screen + 56 * 128, a->scrCache, sizeof(a->scrCache));
        --a->postDraw;
    }

    // One-shot real-WAD locate and async read (dynamic frame count). Guarded on a live
    // DRAM grant: issuing the read to a null dst, or composing into a null arena, faults.
    if (!a->scanned && a->dram && a->dramBytes >= 64 * 1024 && NT_isSdCardMounted()) {
        a->scanned = true;
        int nf = (int)NT_getNumSampleFolders();
        for (int f = 0; f < nf && a->foundFolder < 0; ++f) {
            _NT_wavFolderInfo finfo; NT_getSampleFolderInfo((uint32_t)f, finfo);
            int nfiles = (int)finfo.numSampleFiles;
            for (int s = 0; s < nfiles; ++s) {
                _NT_wavInfo info; NT_getSampleFileInfo((uint32_t)f, (uint32_t)s, info);
                if (info.bits == kNT_WavBits16 &&
                    (name_has(info.name, "DOOM1") || name_has(info.name, "E1M1"))) {
                    a->foundFolder = f; a->foundSample = s;
                    a->wadFrames = info.numFrames;
                    if (a->wadFrames * 2 > a->dramBytes) a->wadFrames = a->dramBytes / 2;
                    break;
                }
            }
        }
        if (a->foundFolder >= 0) {
            g_req.folder = (uint32_t)a->foundFolder; g_req.sample = (uint32_t)a->foundSample;
            g_req.dst = a->dram; g_req.numFrames = a->wadFrames; g_req.startOffset = 0;
            g_req.channels = kNT_WavMono; g_req.bits = kNT_WavBits16;
            g_req.progress = kNT_WavNoProgress; g_req.callback = readCb; g_req.callbackData = a;
            NT_readSampleFrames(g_req);
        }
    }
    if (a->readDone && a->readOk && !a->parsed) { a->parsed = true; swapRealWad(a); }

    // Build the movement intent: CV buses plus the front-panel latch.
    doom::InputConfig cfg{
        a->v[kPFwdBus] - 1, a->v[kPTurnBus] - 1, a->v[kPStrafeBus] - 1, a->v[kPFireBus] - 1,
        a->v[kPDeadzone] / 100.0f, 5.0f, 0.05f, 1.0f };
    doom::Intent in = doom::read_cv_intent(busFrames, numFrames, cfg, a->lpState);
    in.forward += a->panelFwd; in.turn += a->panelTurn; in.fire = in.fire || a->panelFire;
    if (in.forward > 1.0f) in.forward = 1.0f; else if (in.forward < -1.0f) in.forward = -1.0f;
    if (in.turn > 1.0f) in.turn = 1.0f; else if (in.turn < -1.0f) in.turn = -1.0f;
    a->panelFwd *= 0.8f; a->panelTurn *= 0.8f; a->panelFire = false;

    doom::MoveTuning t{ (float)a->v[kPMove], (float)a->v[kPStrafe], a->v[kPTurn] / 100.0f };
    float dt = (float)numFrames / (float)sr;
    float ang = doom::turn_angle(a->pose.angle, in.turn, t.turnSpeed, dt);
    doom::MoveDelta d = doom::move_delta(ang, in, t, dt);
    float radius = (float)a->v[kPRadius];
    if (a->bmReady) {
        doom::Vec2 np = doom::collide_move(a->map, a->bm, { a->pose.x, a->pose.y }, d.dx, d.dy, radius);
        a->pose = { np.x, np.y, ang };
    } else {
        a->pose = { a->pose.x + d.dx, a->pose.y + d.dy, ang };
    }
}

bool draw(_NT_algorithm* self) {
    auto* a = (_doomSpike*)self;
    doom::Camera cam{ a->pose.x, a->pose.y, a->pose.angle };
    const doom::TextureCache* tex = a->texReady ? &a->tex : nullptr;
    doom::render_view(a->map, cam, a->pal, a->cm, tex, NT_screen);
    // Snapshot the bottom rows so step() can restore them over the firmware overlay.
    memcpy(a->scrCache, NT_screen + 56 * 128, sizeof(a->scrCache));
    a->postDraw = 4;
    return true;
}

uint32_t hasCustomUi(_NT_algorithm*) {
    return kNT_encoderL | kNT_encoderR | kNT_button1;
}

void customUi(_NT_algorithm* self, const _NT_uiData& data) {
    auto* a = (_doomSpike*)self;
    // Left encoder turns, right encoder moves forward/back; each tick is a brief impulse
    // the step() loop consumes and decays. Button 1 fires.
    a->panelTurn += 0.5f * (float)data.encoders[0];
    a->panelFwd  += 0.5f * (float)data.encoders[1];
    if (a->panelTurn > 1.0f) a->panelTurn = 1.0f; else if (a->panelTurn < -1.0f) a->panelTurn = -1.0f;
    if (a->panelFwd  > 1.0f) a->panelFwd  = 1.0f; else if (a->panelFwd  < -1.0f) a->panelFwd  = -1.0f;
    if (data.controls & kNT_button1) a->panelFire = true;
}

void serialise(_NT_algorithm* self, _NT_jsonStream& stream) {
    auto* a = (_doomSpike*)self;
    stream.addMemberName("px"); stream.addNumber(a->pose.x);
    stream.addMemberName("py"); stream.addNumber(a->pose.y);
    stream.addMemberName("pa"); stream.addNumber(a->pose.angle);
}

bool deserialise(_NT_algorithm* self, _NT_jsonParse& parse) {
    auto* a = (_doomSpike*)self;
    float v;
    if (parse.matchName("px") && parse.number(v)) a->pose.x = v;
    if (parse.matchName("py") && parse.number(v)) a->pose.y = v;
    if (parse.matchName("pa") && parse.number(v)) a->pose.angle = v;
    return true;
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('D','m','S','c'),
    .name = "Doom core spike",
    .description = "Doom player: real-WAD load, CV+panel movement, BLOCKMAP collision.",
    .numSpecifications = 0,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .step = step,
    .draw = draw,
    .tags = kNT_tagInstrument,
    .hasCustomUi = hasCustomUi,
    .customUi = customUi,
    .serialise = serialise,
    .deserialise = deserialise,
};

extern "C" __attribute__((visibility("default"))) uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
    case kNT_selector_version:      return kNT_apiVersionCurrent;
    case kNT_selector_numFactories: return 1;
    case kNT_selector_factoryInfo:  return (uintptr_t)(data == 0 ? &factory : nullptr);
    }
    return 0;
}
