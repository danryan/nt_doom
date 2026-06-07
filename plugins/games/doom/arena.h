#pragma once
#include <cstdint>
#include <new>

namespace doom {

// A no-heap bump allocator over a caller-supplied span (the DRAM grant). No
// malloc/new[], so the plug-in does not pull cxx runtime stubs. Reset per level.
struct Arena {
    uint8_t* base = nullptr;
    uint32_t cap  = 0;
    uint32_t used = 0;
};

inline void arena_init(Arena& a, void* base, uint32_t cap) {
    a.base = (uint8_t*)base;
    a.cap  = cap;
    a.used = 0;
}

// Allocates `bytes` at the next `align`-aligned offset (align must be a power of
// two). Returns nullptr and leaves `used` unchanged if the aligned request would
// overrun the span. Alignment padding itself can trigger the overflow.
inline void* arena_alloc(Arena& a, uint32_t bytes, uint32_t align = 8) {
    uint32_t mask    = align - 1;
    uint32_t aligned = (a.used + mask) & ~mask;
    if (aligned > a.cap || bytes > a.cap - aligned) return nullptr;
    a.used = aligned + bytes;
    return a.base + aligned;
}

// Allocates sizeof(T) at alignof(T) and default-constructs in place. Returns
// nullptr without constructing on overflow.
template<class T>
inline T* arena_new(Arena& a) {
    void* p = arena_alloc(a, (uint32_t)sizeof(T), (uint32_t)alignof(T));
    if (!p) return nullptr;
    return new (p) T();
}

inline void arena_reset(Arena& a) { a.used = 0; }

} // namespace doom
