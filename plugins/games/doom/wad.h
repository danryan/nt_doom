#pragma once
#include <cstdint>

namespace doom {

struct WadHeader { char id[4]; int32_t numLumps; int32_t dirOffset; };
struct WadDirEntry { int32_t filePos; int32_t size; char name[8]; };

struct Wad {
    const uint8_t* base = nullptr;
    uint32_t size = 0;
    const WadDirEntry* dir = nullptr;
    int32_t numLumps = 0;
};

// Local byte compares avoid a libc memcmp dependency (the NT firmware does not
// resolve memcmp at load time; memcpy/memset/memmove/strlen/strcmp only).
inline bool wad_id_eq(const char* a, const char* b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}
inline bool wad_name_eq(const char* dir8, const char* want8) {
    for (int i = 0; i < 8; ++i) if (dir8[i] != want8[i]) return false;
    return true;
}

inline bool wad_open(const uint8_t* data, uint32_t size, Wad& w) {
    if (size < sizeof(WadHeader)) return false;
    const WadHeader* h = reinterpret_cast<const WadHeader*>(data);
    if (!wad_id_eq(h->id, "IWAD") && !wad_id_eq(h->id, "PWAD")) return false;
    if (h->numLumps < 0 || h->dirOffset < 0) return false;
    uint64_t dirEnd = (uint64_t)h->dirOffset + (uint64_t)h->numLumps * sizeof(WadDirEntry);
    if (dirEnd > size) return false;
    // Validate every directory entry's byte range. A truncated or corrupt read (e.g. a
    // device sample the firmware did not deliver intact) can leave the header valid but a
    // lump filePos garbage; without this check wad_lump_ptr would return a wild pointer
    // and the renderer would dereference unmapped memory (a hard fault on device).
    const WadDirEntry* dir = reinterpret_cast<const WadDirEntry*>(data + h->dirOffset);
    for (int32_t i = 0; i < h->numLumps; ++i) {
        if (dir[i].filePos < 0 || dir[i].size < 0) return false;
        if ((uint64_t)dir[i].filePos + (uint64_t)dir[i].size > size) return false;
    }
    w.base = data; w.size = size;
    w.dir = dir;
    w.numLumps = h->numLumps;
    return true;
}

inline int32_t wad_find_lump(const Wad& w, const char* name, int32_t start = 0) {
    char pad[8] = {0,0,0,0,0,0,0,0};
    for (int i = 0; i < 8 && name[i]; ++i) pad[i] = name[i];
    for (int32_t i = start; i < w.numLumps; ++i)
        if (wad_name_eq(w.dir[i].name, pad)) return i;
    return -1;
}

inline const uint8_t* wad_lump_ptr(const Wad& w, int32_t idx) {
    return w.base + w.dir[idx].filePos;
}
inline uint32_t wad_lump_size(const Wad& w, int32_t idx) {
    return (uint32_t)w.dir[idx].size;
}

} // namespace doom
