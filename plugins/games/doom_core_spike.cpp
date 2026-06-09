#include <distingnt/api.h>
#include <new>
#include "doom_test_map.h"
#include "doom/wad.h"
#include "doom/geom.h"
#include "doom/palette.h"
#include "doom/texture.h"
#include "doom/render.h"

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

struct _doomSpike : public _NT_algorithm {
    doom::Wad wad;
    doom::Map map;
    doom::Palette pal;
    doom::Colormap cm;
    doom::Arena arena;
    doom::TextureCache tex;
    bool texReady = false;
    uint8_t arenaMem[256 * 1024];   // 256 KB texture scratch; sizeof(_doomSpike) ~263 KB.
                                    // Device needs a reboot after first deploy for
                                    // calculateRequirements to re-read the enlarged
                                    // struct size (firmware SRAM-size cache).

};

static const _NT_parameter parameters[] = {
    { .name = "Frame", .min = 0, .max = 1, .def = 0, .unit = kNT_unitNone, .scaling = 0, .enumStrings = nullptr },
};
static const uint8_t page1[] = { 0 };
static const _NT_parameterPage pages[] = { { .name = "Doom", .numParams = 1, .params = page1 } };
static const _NT_parameterPages parameterPages = { .numPages = 1, .pages = pages };

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_doomSpike);
}

_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements&, const int32_t*) {
    auto* a = new (ptrs.sram) _doomSpike();
    a->parameters = parameters;
    a->parameterPages = &parameterPages;
    doom::wad_open(kDoomBspTestWad, kDoomBspTestWadLen, a->wad);
    doom::map_load(a->wad, "E1M1", a->map);
    doom::palette_load(a->wad, a->pal);
    doom::colormap_load(a->wad, a->cm);
    doom::arena_init(a->arena, a->arenaMem, sizeof(a->arenaMem));
    a->texReady = doom::texcache_init(a->tex, a->wad, a->arena);
    return a;
}

void step(_NT_algorithm*, float*, int) {}

bool draw(_NT_algorithm* self) {
    auto* a = (_doomSpike*)self;
    doom::Camera cam{0.0f, 0.0f, 0.0f};   // player-start pose inside the BSP map
    const doom::TextureCache* tex = a->texReady ? &a->tex : nullptr;
    doom::render_view(a->map, cam, a->pal, a->cm, tex, NT_screen);
    return true;
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('D','m','S','c'),
    .name = "Doom core spike",
    .description = "Non-interactive renderer-core .text measurement spike.",
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
    case kNT_selector_factoryInfo:  return (uintptr_t)(data == 0 ? &factory : nullptr);
    }
    return 0;
}
