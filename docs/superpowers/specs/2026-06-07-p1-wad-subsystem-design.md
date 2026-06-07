# P1 WAD subsystem: design

- Date: 2026-06-07
- Status: proposed
- Branch: `dr/p1-wad-subsystem`
- Issue: danryan/nt_doom#2
- Vendor pins: `vendor/distingNT_API` `cd12d87`; `vendor/llvm-project` `a4bf6cd`
  (tag `llvmorg-19.1.0`, sparse `compiler-rt/lib/builtins`). Read-only.

## Context

P0 proved the engine is feasible: the WAD reads back byte-exact through
`NT_readSampleFrames`, a plug-in grants at least 12 MB of DRAM, and the renderer core
is a small fraction of the 82 KB `.text` cap. P1 builds the data subsystem on that
foundation: load real WAD bytes into DRAM and expose typed, zero-copy access to lumps
and map structures, so P2 can render a real map.

P1 is pure logic. Every line is written in response to a failing host test
(`make test`). The one device-facing seam (the WAD read door over `NT_readSampleFrames`)
is designed so the byte logic is host-testable with an in-memory WAV blob and the
device path is a thin, documented adapter.

## Decisions

- Read door is a byte-addressable window over a frame-addressable sample door. The
  firmware strips the WAV header on device; the host source strips the 44-byte header
  itself. Two WAD bytes occupy one little-endian int16 frame, so the WAD byte stream
  equals the PCM data byte stream verbatim (a trailing odd-pad byte is benign).
- No heap. A bump arena over a caller-supplied DRAM span backs all allocation;
  placement `new` only; no `malloc`/`new[]`.
- Palette mapping precomputes a 256-entry 4-bit gray table from PLAYPAL via a fixed
  integer luma; COLORMAP stays a zero-copy 34x256 index view; a combine helper yields
  the shaded gray for a palette index at a light level.
- NODES is exposed as a typed `NodeRaw` view mirroring the existing raw-struct pattern
  in `geom.h`.
- Each unit is a focused header under `plugins/games/doom/`; tests are new files under
  `harness/tests/`, never a 1:1 mirror of implementation files.

## Architecture

```text
WAV blob (16-bit mono) --WavWadSource.read(off,n)--> raw WAD bytes
                                   |
                          wad_load_all(source, arena)        Arena (bump, no heap)
                                   |                               ^
                                   v                               |
                            doom::Wad  --wad_open-->  directory + lumps
                              /         |            \
                       map_load     palette_load   colormap_load
                          |             |               |
                   Map (+NODES)     Palette.gray[256]  Colormap (34x256)
                                          \             /
                                        shade_gray(palIndex, light) -> 4-bit gray
```

## Units

### U1: synthetic builder, PLAYPAL + COLORMAP (extend `harness/tools/wad_build.h`)

Add two lumps to `build_test_wad()`:

- `PLAYPAL`: one 768-byte palette, 256 RGB triples. Entry `i` is the gray ramp
  `r = g = b = i`, so `gray[i] == i >> 4` and assertions are deterministic. (Real
  Doom ships 14 palettes; one is sufficient to exercise parse, and the renderer only
  uses palette 0.)
- `COLORMAP`: 34 maps of 256 bytes. Map `m`, index `i` holds
  `colormap[m][i] = (uint8_t)((i * (33 - m)) / 33)`, a uniform darkening: map 0 is
  identity, map 33 is all-zero. Deterministic and testable.

Regenerate `plugins/games/doom_test_map.h` by re-running `harness/tools/wad_build`.

### U2: DRAM bump arena (`plugins/games/doom/arena.h`, new)

```cpp
struct Arena { uint8_t* base; uint32_t cap; uint32_t used; };
void  arena_init(Arena&, void* base, uint32_t cap);
void* arena_alloc(Arena&, uint32_t bytes, uint32_t align = 8);  // nullptr on overflow
template<class T> T* arena_new(Arena&);                          // placement new, default ctor
void  arena_reset(Arena&);                                       // used = 0
```

- `arena_alloc` rounds `used` up to `align` (power of two), then advances by `bytes`.
  Returns `nullptr` and leaves `used` unchanged if the aligned request overruns `cap`.
- `arena_new<T>` allocates `sizeof(T)` at `alignof(T)` and placement-constructs;
  returns `nullptr` on overflow without constructing.
- No exceptions, no heap. `align` must be a power of two (caller contract).

### U3: WAD read door (`plugins/games/doom/wad_read.h`, new)

Byte-window source plus a whole-WAD loader.

```cpp
struct WavWadSource { const uint8_t* wav; uint32_t wavLen; uint32_t wadLen; };
bool wav_wad_source_init(WavWadSource&, const uint8_t* wav, uint32_t wavLen);
bool wav_wad_read(const WavWadSource&, uint8_t* dst, uint32_t off, uint32_t n);
bool wad_load_all(const WavWadSource&, Arena&, const uint8_t** outBytes, uint32_t* outLen);
```

- `wav_wad_source_init` validates the `RIFF`, `WAVE`, `fmt` (4 bytes, trailing space),
  and `data` chunk ids, requires 16-bit mono
  PCM, and sets `wadLen` from the `data` chunk length. The WAD payload begins at the
  44-byte canonical header offset. (A trailing odd-pad byte is included in `wadLen`
  and is benign to `wad_open`, which addresses by offset.)
- `wav_wad_read` copies `[off, off+n)` of the WAD payload (header-relative) into `dst`,
  bounds-checked against `wadLen`; returns false on out-of-range.
- `wad_load_all` allocates `wadLen` bytes from the arena, reads the whole WAD into it,
  and returns the span; the caller then `wad_open`s it. This is the load path the
  device adapter mirrors.
- Device adapter (documented, not host-compiled): the engine issues
  `NT_readSampleFrames` with `bits = kNT_WavBits16`, `channels = kNT_WavMono`,
  `startOffset = off / 2` frames, `numFrames = (n + (off & 1) + 1) / 2`, into a scratch
  frame buffer, then copies `n` bytes from byte `off & 1` of that buffer. The request
  object persists (file-scope, as in `wad_read_probe.cpp`); the async callback flips a
  done flag the load loop waits on. P1 ships the host-synchronous seam; the async
  device path is exercised by the probe, not by host tests.

### U4: PLAYPAL + COLORMAP to 16 gray (`plugins/games/doom/palette.h`, new)

```cpp
uint8_t luma4(uint8_t r, uint8_t g, uint8_t b);   // 4-bit gray from RGB
struct Palette  { uint8_t gray[256]; };
bool palette_load(const Wad&, Palette&);          // finds PLAYPAL, fills gray[]
struct Colormap { const uint8_t* maps; int32_t numMaps; };  // zero-copy, maps[m*256+i]
bool colormap_load(const Wad&, Colormap&);
uint8_t shade_gray(const Palette&, const Colormap&, uint8_t palIndex, int light);
```

- `luma4` uses integer Rec.601 weights summing to 256:
  `luma = (77*r + 150*g + 29*b) >> 8`, then `gray = luma >> 4` (0..15).
- `palette_load` reads the first 768 bytes of `PLAYPAL` (palette 0) and fills
  `gray[i] = luma4(pal[i].r, pal[i].g, pal[i].b)`.
- `colormap_load` points `maps` at the `COLORMAP` lump and sets
  `numMaps = lumpSize / 256`.
- `shade_gray(pal, cm, idx, light)` clamps `light` to `[0, numMaps)` and returns
  `pal.gray[ cm.maps[light * 256 + idx] ]`.

### U5: NODES typed map view (extend `plugins/games/doom/geom.h`)

```cpp
#pragma pack(push, 1)
struct NodeRaw {
    int16_t  x, y, dx, dy;     // partition line origin and delta
    int16_t  bbox[2][4];       // [child][top, bottom, left, right]
    uint16_t child[2];         // right, left; high bit (0x8000) => subsector index
};
#pragma pack(pop)
// helpers
bool     node_child_is_subsector(uint16_t c);   // c & 0x8000
uint16_t node_child_index(uint16_t c);          // c & 0x7FFF
```

- `sizeof(NodeRaw) == 28`. Extend `Map` with `const NodeRaw* nodes = nullptr;`
  (`numNodes` already exists). `map_load` sets `m.nodes = (const NodeRaw*)wad_lump_ptr`
  and `m.numNodes = lumpSize / sizeof(NodeRaw)`.
- The synthetic builder (U1/U5 fixture touch) gains one real NODES record so the typed
  view has data to assert. The render test is unaffected (it traverses `ssecs[0]`, not
  nodes).

## Testing

New host test files (each includes `catch.hpp`, declares `TEST_CASE` only; the shared
`harness/src/catch_main.cpp` owns `main`):

- `test_arena.cpp`: init, aligned sequential allocs, overflow returns `nullptr` and
  preserves `used`, `arena_new` constructs, `arena_reset` reuses space.
- `test_wad_read.cpp`: wrap the synthetic WAD with `wrap_wav_16bit_mono`,
  `WavWadSource` round-trips bytes at offset 0, at a deep offset, across the odd-pad
  boundary, and rejects out-of-range; `wad_load_all` into an arena then `wad_open`
  yields a WAD whose `map_load` output is identical to parsing the raw bytes directly.
- `test_palette.cpp`: `luma4` on black/white/pure-R/pure-G/pure-B; `palette_load`
  against the ramp fixture (`gray[i] == i >> 4`); `colormap_load` size and identity of
  map 0; `shade_gray` at light 0 equals the base gray, darker light yields lower gray,
  out-of-range light clamps.
- `test_geom_nodes.cpp`: `map_load` exposes the typed NODES view with the expected
  count; `NodeRaw` field values match the fixture record; child high-bit helpers decode
  a subsector child and a node child.

Wire each into the `Makefile` `host`/`test` targets and prerequisites.

## Spec footer

### Recipe spot-check

- Read door: `WavWadSource` over `wrap_wav_16bit_mono` output; pass is byte-exact at
  offset 0, a deep offset, and across the odd-pad boundary. Mirrors the P0 hardware
  result (`docs/.../2026-06-04-doom-p0-spike-results.md`, Spike A table). Device frame
  math checked against `wav.h:88-99` (`_NT_wavRequest`) and `wav.h:235`
  (`NT_readSampleFrames`).
- Arena: bump over a caller span; overflow returns `nullptr`. No SDK dependency.
- Palette: integer Rec.601 luma to 4-bit gray; COLORMAP zero-copy 34x256.
- NODES: 28-byte record matching the documented Doom NODES layout; child high-bit is
  the subsector flag.

### Per-entry verification (three checks against source)

1. Luma at the corners. White `(255,255,255)`: `(77+150+29)=256`, `256*255>>8 = 255`,
   `255>>4 = 15` (max gray). Pure green `(0,255,0)`: `150*255>>8 = 149`, `149>>4 = 9`.
   Pure blue `(0,0,255)`: `29*255>>8 = 28`, `28>>4 = 1`. Black -> 0. These are the test
   anchors and confirm the weighting is green-dominant and blue-weak, as Rec.601
   requires.
2. NODES layout. Record size: `4` int16 (partition) `+ 2*4` int16 (two bboxes)
   `+ 2` uint16 (children) `= 4*2 + 8*2 + 2*2 = 8 + 16 + 4 = 28` bytes. The existing
   `geom.h` already divides the NODES lump by `28` for its count, so the typed struct
   must be 28 bytes or the count diverges. Child decode: `0x8000` set marks a leaf
   (subsector); index is the low 15 bits (`& 0x7FFF`). Matches the documented Doom map
   format.
3. WAV strip offset. `wrap_wav_16bit_mono` emits a 44-byte canonical header
   (`RIFF`+4, `WAVE`, the `fmt` id plus a 16-byte body, `data`+4) then the payload
   verbatim
   (`harness/tools/wav_wrap.h:40-57`). So WAD byte `off` lives at WAV byte `44 + off`,
   and `wav_wad_read` copies from there. `data` chunk length is `wadLen` (payload size,
   odd-padded), recovered for bounds. Confirmed against `wav_wrap.h` and the P0 table
   (`b0` at frame 0 equals payload byte 0).

### Prereq verification (symbols and API the units depend on exist)

- `wad_open`, `wad_find_lump(start)`, `wad_lump_ptr`, `wad_lump_size` exist in
  `plugins/games/doom/wad.h` (P0). Container parse is reused, not rebuilt.
- `wrap_wav_16bit_mono` exists in `harness/tools/wav_wrap.h` (P0). Used to build the
  read-door fixture.
- `_NT_wavRequest` fields (`folder`, `sample`, `dst`, `numFrames`, `startOffset`,
  `channels`, `bits`, `progress`, `callback`, `callbackData`) and `NT_readSampleFrames`
  exist in `vendor/distingNT_API/include/distingnt/wav.h:88-99,235`; `kNT_WavBits16`,
  `kNT_WavMono` enums at `wav.h:47-48` and the channels enum. The device adapter
  references these (documented only; not host-compiled).
- No `memcmp` introduced: lump lookup reuses the existing inline byte compares; new
  code uses field reads and explicit loops, not `memcmp`. ARM unresolved-symbol set is
  re-checked at verify (`arm-none-eabi-nm <o> | grep ' U '`).
