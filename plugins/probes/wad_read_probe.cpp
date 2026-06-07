#include <distingnt/api.h>
#include <distingnt/wav.h>
#include <new>
#include <cstring>

struct _wadProbe : public _NT_algorithm {
    uint8_t buf[64];      // 32 frames at 16-bit = 64 raw bytes
    uint8_t bufOff[64];
    volatile bool done0, doneN;
    bool ok0, okN;
    int numFolders, foundFolder, foundSample;
    int natBits, natFrames;                    // native file format (diagnostic)
    bool scanned;                              // one-shot auto-locate done
};

// PATTERN.wav: 88894 payload bytes / 2 = 44447 frames, 16-bit mono. An uncommon
// frame count (not a power of two) so it does not collide with real loop samples.
static const int kPatternFrames = 44447;

static _NT_wavRequest g_req0, g_reqN;   // must persist across the async read

static void cb0(void* data, bool success) { auto* a=(_wadProbe*)data; a->ok0=success; a->done0=true; }
static void cbN(void* data, bool success) { auto* a=(_wadProbe*)data; a->okN=success; a->doneN=true; }

enum { kParamFolder, kParamSample, kParamOffset };
static const _NT_parameter parameters[] = {
    { .name="Folder", .min=0, .max=63, .def=0, .unit=kNT_unitNone, .scaling=0, .enumStrings=nullptr },
    { .name="Sample", .min=0, .max=63, .def=0, .unit=kNT_unitNone, .scaling=0, .enumStrings=nullptr },
    { .name="Offset", .min=0, .max=30000, .def=1000, .unit=kNT_unitNone, .scaling=0, .enumStrings=nullptr },
};
static const uint8_t page1[]={kParamFolder,kParamSample,kParamOffset};
static const _NT_parameterPage pages[]={{.name="WadRead",.numParams=3,.params=page1}};
static const _NT_parameterPages parameterPages={.numPages=1,.pages=pages};

void calculateRequirements(_NT_algorithmRequirements& req, const int32_t*) {
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(_wadProbe);
}
_NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements&, const int32_t*) {
    auto* a = new (ptrs.sram) _wadProbe();
    a->parameters=parameters; a->parameterPages=&parameterPages;
    a->done0=a->doneN=false; a->ok0=a->okN=false;
    a->numFolders=-1; a->foundFolder=-1; a->foundSample=-1;
    a->natBits=-1; a->natFrames=-1; a->scanned=false;
    memset(a->buf,0,64); memset(a->bufOff,0,64);
    return a;
}

void step(_NT_algorithm* self, float*, int) {
    auto* a = (_wadProbe*)self;
    if (!NT_isSdCardMounted()) return;
    if (a->scanned) return;
    a->scanned = true;

    // Auto-locate PATTERN.wav: the unique 16-bit, 32768-frame file. No numeric
    // param dialing needed (nt_helper cannot set numeric params over MCP).
    a->numFolders = (int)NT_getNumSampleFolders();
    int tf = -1, ts = -1;
    for (int f = 0; f < a->numFolders && tf < 0; ++f) {
        _NT_wavFolderInfo finfo;
        NT_getSampleFolderInfo((uint32_t)f, finfo);
        int nfiles = (int)finfo.numSampleFiles;
        for (int s = 0; s < nfiles; ++s) {
            _NT_wavInfo info;
            NT_getSampleFileInfo((uint32_t)f, (uint32_t)s, info);
            if (info.bits == kNT_WavBits16 && (int)info.numFrames == kPatternFrames) {
                tf = f; ts = s; a->natBits = 16; a->natFrames = (int)info.numFrames;
                break;
            }
        }
    }
    a->foundFolder = tf; a->foundSample = ts;
    if (tf < 0) return;   // not found; screen shows f/s = -1/-1

    a->done0=a->doneN=false; a->ok0=a->okN=false;

    // 32 frames at 16-bit fill the 64-byte buffer with raw little-endian PCM.
    g_req0.folder=(uint32_t)tf; g_req0.sample=(uint32_t)ts; g_req0.dst=a->buf;
    g_req0.numFrames=32; g_req0.startOffset=0;
    g_req0.channels=kNT_WavMono; g_req0.bits=kNT_WavBits16;
    g_req0.progress=kNT_WavNoProgress; g_req0.callback=cb0; g_req0.callbackData=a;
    NT_readSampleFrames(g_req0);

    // bN: read 1000 frames in (byte 2000) to confirm a deeper offset is exact too.
    g_reqN = g_req0; g_reqN.dst=a->bufOff; g_reqN.startOffset=1000;
    g_reqN.callback=cbN; g_reqN.callbackData=a;
    NT_readSampleFrames(g_reqN);
}

static void hex2(char* o, uint8_t b) {
    const char* h="0123456789ABCDEF"; o[0]=h[(b>>4)&0xF]; o[1]=h[b&0xF];
}
bool draw(_NT_algorithm* self) {
    auto* a=(_wadProbe*)self;
    char fs[12]; int lf=NT_intToString(fs, a->foundFolder); fs[lf++]='/'; int ls=NT_intToString(fs+lf, a->foundSample); fs[lf+ls]=0;
    char nb[8]; int lb=NT_intToString(nb, a->natBits); nb[lb]=0;
    char nfr[12]; int lr=NT_intToString(nfr, a->natFrames); nfr[lr]=0;
    NT_drawText(0,10,"f/s:"); NT_drawText(30,10,fs);
    NT_drawText(80,10,"bits:"); NT_drawText(115,10,nb);
    NT_drawText(150,10,"frm:"); NT_drawText(180,10,nfr);
    NT_drawText(0,22, a->done0 ? (a->ok0?"read0 OK":"read0 FAIL") : "read0 ...");
    NT_drawText(0,34, a->doneN ? (a->okN?"readN OK":"readN FAIL") : "readN ...");
    // first 6 bytes at offset 0
    char hx[20]; int p=0; for (int i=0;i<6;++i){ hex2(hx+p,a->buf[i]); p+=2; hx[p++]=' '; } hx[p]=0;
    NT_drawText(0,46,"b0:"); NT_drawText(24,46,hx);
    p=0; for (int i=0;i<6;++i){ hex2(hx+p,a->bufOff[i]); p+=2; hx[p++]=' '; } hx[p]=0;
    NT_drawText(0,58,"bN:"); NT_drawText(24,58,hx);
    return true;   // suppress firmware param overlay so the y=10 diag line shows
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('W','d','R','d'),
    .name = "WAD read probe",
    .description = "Reads a WAV-wrapped pattern via NT_readSampleFrames; shows bytes.",
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
