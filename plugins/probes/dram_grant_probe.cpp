#include <distingnt/api.h>
#include <new>

#ifndef DRAM_REQUEST_MB
#define DRAM_REQUEST_MB 4
#endif
static const uint32_t kReq = (uint32_t)DRAM_REQUEST_MB * 1024u * 1024u;

struct _dramProbe : public _NT_algorithm {
    uint8_t* mem;
    uint32_t got;
    bool verified;
};

static const _NT_parameter parameters[] = {
    { .name="Check", .min=0, .max=1, .def=0, .unit=kNT_unitNone, .scaling=0, .enumStrings=nullptr },
};
static const uint8_t page1[]={0};
static const _NT_parameterPage pages[]={{.name="Dram",.numParams=1,.params=page1}};
static const _NT_parameterPages parameterPages={.numPages=1,.pages=pages};

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_dramProbe);
    req.dram = kReq;
}
_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req, const int32_t*) {
    auto* a = new (ptrs.sram) _dramProbe();
    a->parameters=parameters; a->parameterPages=&parameterPages;
    a->mem = ptrs.dram; a->got = req.dram; a->verified=false;
    // Write a sentinel across the whole grant, then read it back.
    bool ok = (a->mem != nullptr);
    if (ok) {
        for (uint32_t i = 0; i < a->got; i += 4096) a->mem[i] = (uint8_t)(i * 2654435761u >> 24);
        for (uint32_t i = 0; i < a->got && ok; i += 4096)
            if (a->mem[i] != (uint8_t)(i * 2654435761u >> 24)) ok = false;
    }
    a->verified = ok;
    return a;
}
void step(_NT_algorithm*, float*, int) {}
bool draw(_NT_algorithm* self) {
    auto* a=(_dramProbe*)self;
    char buf[16]; int l=NT_intToString(buf, (int)(a->got/(1024*1024))); buf[l]=0;
    NT_drawText(0,20,"DRAM MB:"); NT_drawText(70,20,buf);
    NT_drawText(0,36, a->verified ? "sentinel OK" : "sentinel FAIL");
    return false;
}
static const _NT_factory factory = {
    .guid = NT_MULTICHAR('D','r','A','m'),
    .name = "DRAM grant probe",
    .description = "Requests DRAM_REQUEST_MB of DRAM and verifies a sentinel.",
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
