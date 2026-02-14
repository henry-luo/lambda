# Unified Font Module for Lambda/Radiant

## Proposal — February 2026

### Implementation Status

| Phase | Description | Status | Date |
|-------|-------------|--------|------|
| **Phase 1** | Foundation — all 11 source files in `lib/font/`, build config, compilation | ✅ Complete | 2026-02-14 |
| **Phase 2** | Radiant migration — wire `FontContext` into `UiContext`, rewrite `setup_font()` via `font_resolve()` | ✅ Complete | 2026-02-15 |
| **Phase 3** | Caching & fallback — disk cache, bitmap cache, advance eviction, @font-face bridge | ✅ Complete | 2026-02-14 |
| **Phase 4** | Cleanup — slim headers, remove ~335 lines of unused stubs from `font_face.cpp` | ✅ Complete | 2026-02-16 |

**Build:** 0 errors, 0 warnings · **Lambda baseline:** 224/224 ✅ · **Radiant baseline:** 1972/1972 ✅

#### Phase 2 Changes

**Approach:** Hybrid migration — both `FT_Face` and `FontHandle*` coexist in `FontProp`/`FontBox` for backward compatibility. `setup_font()` is the single migration point; all downstream callers continue reading `ft_face` unchanged.

**Files modified:**
- `radiant/view.hpp` — Forward declarations for `FontContext`/`FontHandle`; added `font_handle` field to `FontProp` and `FontBox`; added `font_ctx` to `UiContext`
- `radiant/ui_context.cpp` — Create `FontContext` with actual `pixel_ratio` (after window creation); destroy in cleanup
- `radiant/font.cpp` — Rewritten `setup_font()`: try @font-face first (old path), then `font_resolve()` for system fonts (new path), then legacy fallback chain; metrics populated from `font_get_metrics()`
- `lib/font/font.h` — Renamed `FontStyle` → `FontStyleDesc` and `FontFaceSrc` → `FontFaceSource` to avoid name collisions with existing `lib/font_config.h` types
- `lib/font/*.c` — Same rename applied across all implementation files

#### Phase 3 Changes

**Approach:** Gap-fill — most Phase 3 features (LRU face cache, advance cache, fallback chains, generic-family table, @font-face descriptor registry) were already implemented in Phase 1. Phase 3 filled the remaining gaps: disk persistence, bitmap caching, advance eviction, and bridging Radiant's `register_font_face()` to the unified module.

**Files modified:**
- `lib/font/font_database.c` — Implemented `font_database_save_cache_internal()` and `font_database_load_cache_internal()` with binary format (magic `0x4C464E54`, version 1, length-prefixed strings, file mtime/size validation for cache invalidation)
- `lib/font/font_internal.h` — Added declarations for `font_database_save_cache_internal` and `font_database_load_cache_internal`
- `lib/font/font_context.c` — Modified `font_context_scan()` to try disk cache first (via `font_database_load_cache_internal`), save after full scan; added `font_cache_save()` public API implementation
- `lib/font/font_glyph.c` — Added `ADVANCE_CACHE_MAX_ENTRIES` (4096) with clear-on-full eviction; added `bitmap_cache_hash()`, `bitmap_cache_compare()`, `ensure_bitmap_cache()` for lazy bitmap cache init; wired bitmap cache into `font_render_glyph()` (lookup before FreeType render, insert after)
- `radiant/font_face.cpp` — Added `#include "../lib/font/font.h"`; moved 7 removed types (`EnhancedFontMetrics`, `EnhancedFontBox`, `FontMatchCriteria`, `FontMatchResult`, `FontFallbackChain`, `CharacterMetrics`, `GlyphRenderInfo`) to internal scope; added bridge in `register_font_face()` that converts `CssEnum` weight/style → `FontWeight`/`FontSlant`, builds `FontFaceSource` array, and calls `font_face_register(uicon->font_ctx, &face_desc)`
- `radiant/font_face.h` — Removed 7 unused types and ~30 unused function declarations; kept only externally-used API: `FontFaceSrc`, `FontFaceDescriptor`, `process_document_font_faces`, `load_font_with_descriptors`, `load_local_font_file`, `register_font_face`, char width cache functions

**Name collision fix:** `font_database_save_cache` / `font_database_load_cache` conflicted with stubs in old `lib/font_config.c` — renamed to `_internal` suffix in `lib/font/`.

---

## 1. Motivation

Lambda's font handling has grown organically across several files with **overlapping responsibilities, duplicated logic, and inconsistent abstractions**. The code works, but it has become fragile and hard to extend. This proposal designs a unified font sub-module that consolidates all font-related functionality behind a single, consistent API.

### Current Pain Points

| Problem | Where |
|---------|-------|
| **Scattered ownership** — Font code lives in 7+ files across `lib/` and `radiant/` with no clear module boundary | `lib/font_config.c`, `radiant/font.cpp`, `radiant/font_face.cpp`, `radiant/font_lookup_platform.c`, `radiant/font.h`, `radiant/font_face.h`, `radiant/pdf/fonts.cpp` |
| **Duplicate FT_Library instances** — Layout engine and PDF engine each create their own `FT_Library`, with no sharing of loaded faces | `radiant/ui_context.cpp` vs `radiant/pdf/fonts.cpp` |
| **Duplicate fixed-size font logic** — The `FT_Select_Size` handling for color emoji is copy-pasted 4 times | `font.cpp` (multiple call sites), `font_face.cpp` |
| **Duplicate generic family tables** — `generic_families[]` in `font.cpp` and `resolve_generic_family()` define the same serif/sans-serif/monospace mappings independently | `radiant/font.cpp` |
| **WOFF1 gap** — Format preference lists WOFF but no decompressor exists; WOFF1 fonts silently fail | `radiant/font_face.cpp` |
| **`std::string` in WOFF2 path** — libwoff2 API requires `std::string`, violating project conventions | `radiant/font_face.cpp` |
| **Missing glyph advance cache** — Glyph `advance_x` is recalculated every call; the `// todo` has been open since initial implementation | `radiant/font.cpp` |
| **Disk font cache not implemented** — `FontDatabase` has full cache structs but `load_cache()`/`save_cache()` are stubs | `lib/font_config.c` |
| **`EnhancedFontBox` / `EnhancedFontMetrics` mostly unused** — Declared in `font_face.h` but the layout pipeline still uses the simpler `FontBox` | `radiant/font_face.h` vs `radiant/layout.hpp` |
| **FreeType types leak everywhere** — `FT_Face`, `FT_GlyphSlot`, `FT_Library` appear in `view.hpp`, `layout.hpp`, `font.h`, making them hard to replace or wrap | Project-wide |

### What We Are *Not* Doing

We are **not** rewriting FreeType or libwoff2. We inherit them as-is and wrap them behind our own API so that:
- Callers never touch `FT_*` types directly.
- Resource lifetimes (faces, glyphs, buffers) are managed in one place.
- Future substitution (e.g., replacing FreeType with stb_truetype for WASM) only changes the backend, not every call site.

---

## 2. Goals

1. **Unify** — One module (`lib/font/`) owns all font functionality: discovery, matching, loading, metrics, glyph rasterization, web font decompression, and caching.
2. **All common font formats** — First-class support for TrueType (`.ttf`), OpenType/CFF (`.otf`), TrueType Collections (`.ttc`), WOFF (`.woff`), and WOFF2 (`.woff2`). Every format loads through a single code path; callers never know or care which format the underlying file is.
3. **Wrap, don't rewrite** — Keep FreeType, libwoff2, and Brotli as backend libraries. Selectively include only the source files we need from those repos. Expose none of their types in the public API.
4. **Consistent API** — A single `FontContext` replaces `FT_Library` + `FontDatabase` + `UiContext` font fields. All callers go through `font_*()` functions.
5. **Pool/arena memory management everywhere** — All allocations inside the font module use Lambda's `Pool` and `Arena` allocators. Zero `malloc`/`free`/`new`/`delete`. Zero `std::string` or `std::vector`. Decompressed WOFF buffers, glyph bitmaps, cache entries, strings — everything goes through the pool or arena. This gives deterministic cleanup and memory accounting.
6. **Unified resource management** — One cache hierarchy: font file cache → face cache → glyph metrics cache → rasterized glyph cache, all pool/arena-backed with LRU eviction.
7. **Better Radiant integration** — Layout and rendering call the same API. Metrics are computed once and cached. Fallback chains are resolved lazily and cached per-codepoint.
8. **Cross-platform consistency** — Identical behavior on macOS, Linux, Windows. Platform-specific code is isolated to one file (`font_platform.c`).

---

## 3. Architecture

### 3.1 Module Structure — ✅ Implemented

All 12 files created and compiling cleanly:

```
lib/font/
├── font.h                  # Public API — the ONLY header callers include
├── font_internal.h         # Internal types (FT_* appears ONLY here)
├── font_context.c          # FontContext lifecycle, init/destroy
├── font_database.c         # Font discovery, scanning, matching (from font_config.c)
├── font_loader.c           # Face loading: TTF/OTF/TTC/WOFF/WOFF2 (wraps FreeType + libwoff2)
├── font_metrics.c          # Metrics computation, OS/2/hhea/typo values
├── font_glyph.c            # Glyph loading, rasterization, advance caching
├── font_cache.c            # Multi-tier cache: faces, metrics, glyphs, disk + font_resolve()
├── font_fallback.c         # Fallback chain resolution, codepoint→face mapping
├── font_face.c             # Font face descriptor registry: register, store, query, match
├── font_platform.c         # Platform-specific discovery (macOS CoreText, Linux dirs, Win32)
└── font_decompress.cpp     # WOFF1 (zlib) + WOFF2 (Brotli/libwoff2) decompression (NOTE: .cpp, not .c)
```

> **Implementation note:** `font_decompress` uses `.cpp` extension (not `.c` as originally proposed) because libwoff2's `WOFF2MemoryOut` API requires C++ construction. The `std::string` is eliminated by pre-computing the output size with `ComputeWOFF2FinalSize()` and arena-allocating the buffer, then using `WOFF2MemoryOut` with the pre-sized buffer directly.

### 3.2 Dependency Graph

```
                    ┌─────────────────────────────┐
                    │       Callers (Radiant)      │
                    │  layout_inline, render_text, │
                    │  setup_font, PDF output      │
                    └────────────┬────────────────┘
                                 │ #include "font.h"
                                 ▼
                    ┌─────────────────────────────┐
                    │     font.h  (Public API)     │
                    │  FontContext, FontHandle,     │
                    │  GlyphInfo, FontMetrics       │
                    │  (NO FT_* types exposed)      │
                    └────────────┬────────────────┘
                                 │
          ┌──────────┬───────────┼──────────┬──────────────┐
          ▼          ▼           ▼          ▼              ▼
   font_database  font_loader  font_glyph  font_cache  font_fallback
          │          │           │          │              │
          │          ▼           ▼          │              │
          │    ┌────────────────┐ FreeType │              │
          │    │font_decompress │(internal)│              │
          │    └───────┬────────┘          │              │
          │            ▼                   │              │
          │    zlib + libwoff2 + Brotli    │              │
          │    (internal)                  │              │
          ▼                                ▼              │
   font_platform                    Lambda pool/arena    │
   (CoreText / dirs / Win32)        allocators           │
```

### 3.3 Key Design Principle: Opaque Handles

```c
// Public — callers see these
typedef struct FontContext FontContext;        // opaque
typedef struct FontHandle  FontHandle;         // opaque, ref-counted
typedef uint32_t           GlyphId;            // FreeType glyph index, but type-aliased

// Internal — only font_internal.h sees FT_Face, FT_Library, etc.
```

This means `view.hpp` and `layout.hpp` will never `#include <ft2build.h>` again. The `FontProp.ft_face` field (currently `void*`) becomes a `FontHandle*`.

### 3.4 Font Format Support Matrix

A core goal is that **every common font format works transparently**. The caller passes a file path (or data URI, or in-memory buffer) to `font_loader.c`; the loader detects the format, decompresses if needed, and hands raw SFNT bytes to FreeType.

| Format | Extension | Magic Bytes | Detection | Decompression | FreeType Load | Status Today | Status After |
|--------|-----------|-------------|-----------|---------------|---------------|-------------|-------------|
| **TrueType** | `.ttf` | `0x00010000` or `true` | `detect_font_format()` | None needed | `FT_New_Face` | ✅ Works | ✅ |
| **OpenType/CFF** | `.otf` | `OTTO` | `detect_font_format()` | None needed | `FT_New_Face` | ✅ Works | ✅ |
| **TrueType Collection** | `.ttc` | `ttcf` | `detect_font_format()` | None needed | `FT_New_Face(path, index)` | ✅ Works | ✅ |
| **WOFF1** | `.woff` | `wOFF` | `detect_font_format()` | zlib per-table inflate | `FT_New_Memory_Face` | ❌ Detected but **no decompressor** | ✅ New |
| **WOFF2** | `.woff2` | `wOF2` | `detect_font_format()` | Brotli via libwoff2 | `FT_New_Memory_Face` | ⚠️ Only via `@font-face` data URIs | ✅ Unified |
| **Data URI (base64)** | N/A | `data:font/` | Prefix check | Base64 decode + format detect | `FT_New_Memory_Face` | ✅ Works (font_face.cpp) | ✅ |

#### Format Loading Pipeline

```
  Input: file path / data URI / raw bytes
    │
    ▼
┌──────────────────────────────────┐
│  font_loader.c: font_load()     │
│  1. Detect format (magic bytes)  │
│  2. If WOFF1 → decompress_woff1()│  ← font_decompress.c (NEW)
│  3. If WOFF2 → decompress_woff2()│  ← font_decompress.c (existing libwoff2)
│  4. If data URI → base64_decode() + recurse │
│  5. Raw SFNT → FT_New_Memory_Face│  ← all formats converge here
│     or FT_New_Face (for files)  │
│  6. FT_Set_Pixel_Sizes          │
│  7. Wrap in FontHandle          │
└──────────────────────────────────┘
    │
    ▼
  Output: FontHandle* (opaque)
```

Key insight: **WOFF1 and WOFF2 are just compressed containers around SFNT (TTF/OTF) data.** Once decompressed, they're identical to TTF/OTF. The loader decompresses into an arena-allocated buffer, then passes it to `FT_New_Memory_Face`. The buffer stays alive as long as the `FontHandle` is alive (arena lifetime).

#### WOFF1 Decompression (`font_decompress.c`)

WOFF1 is far simpler than WOFF2. The format is:
```
┌──────────────────────┐
│ WOFF Header (44 bytes)│  signature, totalSfntSize, numTables, ...
├──────────────────────┤
│ Table Directory       │  tag, offset, compLength, origLength, origChecksum
│  (numTables entries)  │
├──────────────────────┤
│ Font Tables           │  each table: zlib-compressed if compLength < origLength
│  (concatenated)       │  otherwise stored raw
└──────────────────────┘
```

Decompression is ~80 lines of C using zlib `uncompress()` (already linked as a FreeType dependency). Output is a valid SFNT buffer allocated from the arena:

```c
// font_decompress.c

// Decompress WOFF1 to raw SFNT bytes.
// Output is arena-allocated and lives as long as the arena.
bool decompress_woff1(Arena* arena, const uint8_t* data, size_t len,
                      uint8_t** out, size_t* out_len);

// Decompress WOFF2 to raw SFNT bytes.
// Wraps libwoff2 API. Output is arena-allocated.
// Replaces the current std::string-based code in font_face.cpp.
bool decompress_woff2(Arena* arena, const uint8_t* data, size_t len,
                      uint8_t** out, size_t* out_len);

// Detect compressed format from magic bytes and decompress if needed.
// Returns the raw buffer (may be the input itself if no decompression needed).
// If decompression occurred, *out is arena-allocated.
bool font_decompress_if_needed(Arena* arena, const uint8_t* data, size_t len,
                               const uint8_t** out, size_t* out_len);
```

#### System Font Scanning: All Formats

The current `is_font_file()` in `font_config.c` **skips `.woff` and `.woff2`** with the comment *"less common and slower to parse"*. The unified module changes this:

```c
// BEFORE (font_config.c)
static bool is_font_file(const char* filename) {
    // checks .ttf, .otf, .ttc only
    return false; // Skip woff/woff2 for now
}

// AFTER (font_database.c)
static bool is_font_file(const char* filename) {
    // checks .ttf, .otf, .ttc, .woff, .woff2
    // WOFF/WOFF2 system fonts are uncommon but should be discovered.
    // Decompression is deferred to load time (lazy), so scanning cost is minimal.
}
```

For WOFF/WOFF2 files discovered during scanning, we parse metadata **without decompressing the full file**:
- WOFF1: The `name` table offset/length are in the WOFF directory. We decompress only that one table (~1-2 KB) to extract family name, weight, style.
- WOFF2: We defer to full loading since the WOFF2 format doesn't allow single-table extraction.

### 3.5 Pool & Arena Memory Architecture

Every allocation inside the font module goes through Lambda's `Pool` or `Arena`. This section defines exactly which allocator owns what.

#### Allocator Roles

| Allocator | What It's For | Lifetime | Deallocation |
|-----------|---------------|----------|-------------|
| **Pool** (`mempool.h`) | Fixed-size, recyclable objects: `FontHandle`, `GlyphInfo`, cache entries, hashmap nodes, `FontEntry`, `FontFamily` | Varies — objects are allocated and freed individually via `pool_alloc`/`pool_free` | Explicit `pool_free()` on eviction; bulk `pool_destroy()` on shutdown |
| **Arena** (`arena.h`) | Append-only, long-lived data: strings (family names, file paths, PostScript names), decompressed WOFF buffers, font file data for `FT_New_Memory_Face` | Lives until `FontContext` is destroyed | Bulk `arena_destroy()` — no individual frees |

#### Internal Struct Allocation Map

```c
// font_internal.h — internal layout (never exposed in font.h)

struct FontContext {
    Pool* pool;                     // pool_create() — owned
    Arena* arena;                   // arena_create_default(pool) — owned
    Arena* glyph_arena;             // separate arena for glyph bitmap data
    FT_Library ft_library;          // single FreeType instance
    FontDatabase* database;         // pool_calloc(pool, sizeof(FontDatabase))
    struct hashmap* face_cache;     // LRU cache: key → FontHandle*
    struct hashmap* glyph_cache;    // LRU cache: (handle,codepoint) → GlyphInfo
    struct hashmap* bitmap_cache;   // LRU cache: (handle,codepoint,mode) → GlyphBitmap
    FontFaceDescriptor** face_descriptors; // registered font face descriptors
    int face_descriptor_count;
    int face_descriptor_capacity;
    FontContextConfig config;
};

struct FontHandle {
    FT_Face ft_face;                // internal FreeType face
    int ref_count;                  // reference counting
    FontMetrics metrics;            // computed once, cached inline
    bool metrics_ready;
    uint8_t* memory_buffer;         // arena-allocated: decompressed WOFF data or mmap'd file
    size_t memory_buffer_size;      //   (FreeType requires buffer to outlive face)
    struct hashmap* advance_cache;  // codepoint → advance_x (pool-allocated entries)
    FontContext* ctx;               // back-pointer for pool access
    uint32_t lru_tick;              // for LRU eviction in face cache
};
```

#### Allocation Patterns by Operation

| Operation | Allocations | Allocator |
|-----------|------------|----------|
| **Font scanning** (discover files) | `FontEntry` structs, family/path strings | `pool_calloc` for entries, `arena_strdup` for strings |
| **Font loading** (open face) | `FontHandle` struct | `pool_calloc(ctx->pool, sizeof(FontHandle))` |
| **WOFF1/WOFF2 decompression** | Decompressed SFNT buffer (stays alive with handle) | `arena_alloc(ctx->arena, sfnt_size)` |
| **Data URI decoding** | Base64-decoded bytes, then decompressed SFNT | `arena_alloc` for both stages |
| **Glyph metrics query** | `GlyphInfo` cache entry | `pool_calloc` (recyclable on eviction) |
| **Glyph bitmap render** | `GlyphBitmap` + pixel buffer | `pool_calloc` for struct, `arena_alloc(ctx->glyph_arena, pitch*height)` for pixels |
| **Fallback chain** | `FontHandle*[]` array, codepoint→handle cache entries | `pool_alloc` for arrays, hashmap entries |
| **Face descriptor registration** | `FontFaceDescriptor` struct, `FontFaceSrc` array, family/path strings | `pool_calloc` for descriptor and src array, `arena_strdup` for strings |
| **Disk cache I/O** | Temporary read buffer | `arena_alloc` (reset after load) |
| **String operations** | Family name normalization, path construction | `arena_sprintf`, `arena_strdup` — never `malloc`/`strdup` |

#### Why Two Arenas

```
ctx->arena        — long-lived strings and font data buffers
                     Lives for the entire FontContext lifetime.
                     Grows monotonically; reset only on destroy.

ctx->glyph_arena  — glyph bitmap pixel data
                     Can be reset when the bitmap cache is fully evicted
                     (e.g., on font size change or document switch).
                     Separating it avoids fragmenting the main arena
                     with large, transient bitmap allocations.
```

#### WOFF2 libwoff2 Shim: Eliminating `std::string`

The current WOFF2 code uses `std::string` because the libwoff2 API requires it:
```cpp
// BEFORE (font_face.cpp) — violates project convention
std::string output;
woff2::ConvertWOFF2ToTTF(data, len, &output);
// output.data() passed to FT_New_Memory_Face — lifetime tied to std::string
```

The fix: a thin C++ wrapper in `font_decompress.c` that immediately copies into the arena:
```cpp
// AFTER (font_decompress.c) — the ONLY file with any C++ in the font module
bool decompress_woff2(Arena* arena, const uint8_t* data, size_t len,
                      uint8_t** out, size_t* out_len) {
    std::string output;  // temporary, scoped to this function only
    if (!woff2::ConvertWOFF2ToTTF(data, len, &output)) return false;
    *out_len = output.size();
    *out = (uint8_t*)arena_alloc(arena, *out_len);  // arena owns the memory now
    memcpy(*out, output.data(), *out_len);
    return true;
    // std::string destructor runs here — no leaked C++ memory
}
```

This confines `std::string` to a single 8-line function. Everything else is pure C with pool/arena.

#### FreeType Custom Memory Allocator

FreeType supports pluggable memory allocation via `FT_Memory`. We route it through our pool:

```c
// font_context.c — FreeType uses our pool for all its internal allocations
static void* ft_pool_alloc(FT_Memory memory, long size) {
    Pool* pool = (Pool*)memory->user;
    return pool_alloc(pool, (size_t)size);
}
static void ft_pool_free(FT_Memory memory, void* block) {
    Pool* pool = (Pool*)memory->user;
    pool_free(pool, block);
}
static void* ft_pool_realloc(FT_Memory memory, long cur_size, long new_size, void* block) {
    Pool* pool = (Pool*)memory->user;
    return pool_realloc(pool, block, (size_t)new_size);
}

static FT_MemoryRec_ ft_memory = {
    .user    = NULL,  // set to pool at init time
    .alloc   = ft_pool_alloc,
    .free    = ft_pool_free,
    .realloc = ft_pool_realloc,
};

FontContext* font_context_create(FontContextConfig* config) {
    Pool* pool = config->pool ? config->pool : pool_create();
    Arena* arena = config->arena ? config->arena : arena_create_default(pool);

    FontContext* ctx = (FontContext*)pool_calloc(pool, sizeof(FontContext));
    ctx->pool = pool;
    ctx->arena = arena;
    ctx->glyph_arena = arena_create(pool, 256 * 1024, 4 * 1024 * 1024); // 256KB–4MB chunks

    // Route FreeType's internal allocations through our pool
    ft_memory.user = pool;
    FT_New_Library(&ft_memory, &ctx->ft_library);
    FT_Add_Default_Modules(ctx->ft_library);
    FT_Library_SetLcdFilter(ctx->ft_library, FT_LCD_FILTER_DEFAULT);

    // ... database init, cache init ...
    return ctx;
}
```

This means **all memory** — FreeType internals, our structs, decompressed fonts, glyph bitmaps — flows through `Pool`, giving us:
- Accurate memory accounting via `pool` stats.
- Deterministic cleanup via `pool_destroy()` + `arena_destroy()`.
- No memory leaks from forgotten `free()` calls.

---

## 4. Public API Design (`font.h`)

```c
#ifndef LAMBDA_FONT_H
#define LAMBDA_FONT_H

#include <stdint.h>
#include <stdbool.h>

// Forward declarations — all opaque
typedef struct FontContext FontContext;
typedef struct FontHandle  FontHandle;
typedef uint32_t GlyphId;

struct Pool;
struct Arena;

// ============================================================================
// Font Context — replaces FT_Library + FontDatabase + UiContext font fields
// ============================================================================

typedef struct FontContextConfig {
    struct Pool* pool;              // memory pool for allocations
    struct Arena* arena;            // arena for string storage
    float pixel_ratio;              // display pixel ratio (1.0, 2.0, etc.)
    const char* cache_dir;          // disk cache directory (NULL = default ~/.lambda/)
    int max_cached_faces;           // max open font faces (default 64)
    int max_cached_glyphs;          // max cached glyph bitmaps (default 4096)
    bool enable_lcd_rendering;      // subpixel rendering
    bool enable_disk_cache;         // persist font database to disk
} FontContextConfig;

FontContext* font_context_create(FontContextConfig* config);
void         font_context_destroy(FontContext* ctx);

// Trigger font directory scanning (auto-called on first lookup if needed)
bool font_context_scan(FontContext* ctx);

// ============================================================================
// Font Style — input for font resolution (CSS-like)
// ============================================================================

typedef enum FontWeight {
    FONT_WEIGHT_THIN       = 100,
    FONT_WEIGHT_EXTRA_LIGHT = 200,
    FONT_WEIGHT_LIGHT      = 300,
    FONT_WEIGHT_NORMAL     = 400,
    FONT_WEIGHT_MEDIUM     = 500,
    FONT_WEIGHT_SEMI_BOLD  = 600,
    FONT_WEIGHT_BOLD       = 700,
    FONT_WEIGHT_EXTRA_BOLD = 800,
    FONT_WEIGHT_BLACK      = 900,
} FontWeight;

typedef enum FontSlant {
    FONT_SLANT_NORMAL,
    FONT_SLANT_ITALIC,
    FONT_SLANT_OBLIQUE,
} FontSlant;

typedef struct FontStyle {
    const char* family;             // CSS font-family (comma-separated or single)
    float       size_px;            // desired size in CSS pixels
    FontWeight  weight;
    FontSlant   slant;
} FontStyle;

// ============================================================================
// Font Handle — an opened, sized font face (opaque, ref-counted)
// ============================================================================

// Resolve a FontStyle to a loaded, sized font handle.
// Returns NULL on failure. Caller must release with font_handle_release().
FontHandle* font_resolve(FontContext* ctx, const FontStyle* style);

// Increment / decrement reference count
FontHandle* font_handle_retain(FontHandle* handle);
void        font_handle_release(FontHandle* handle);

// ============================================================================
// Font Metrics — per-face, per-size metrics
// ============================================================================

typedef struct FontMetrics {
    float ascender;             // above baseline (positive)
    float descender;            // below baseline (negative)
    float line_height;          // ascender - descender + line_gap
    float line_gap;             // additional leading

    // OpenType table metrics (for browser-compat line height)
    float typo_ascender, typo_descender, typo_line_gap;
    float win_ascent, win_descent;
    float hhea_ascender, hhea_descender, hhea_line_gap;

    // Useful typographic measures
    float x_height;             // height of lowercase 'x'
    float cap_height;           // height of uppercase letters
    float space_width;          // advance width of U+0020 SPACE
    float em_size;              // units per em (typically 1000 or 2048)

    bool has_kerning;           // font contains kerning data
} FontMetrics;

// Get metrics for a resolved font handle (cached, zero-cost after first call)
const FontMetrics* font_get_metrics(FontHandle* handle);

// ============================================================================
// Glyph Info — per-glyph measurement (cached)
// ============================================================================

typedef struct GlyphInfo {
    GlyphId id;                 // glyph index in the font
    float   advance_x;         // horizontal advance
    float   advance_y;         // vertical advance (usually 0 for horizontal text)
    float   bearing_x;         // left side bearing
    float   bearing_y;         // top side bearing
    int     width, height;     // bitmap dimensions
    bool    is_color;          // color emoji / COLR glyph
} GlyphInfo;

// Get glyph info for a codepoint (advance, bearings — cached)
GlyphInfo font_get_glyph(FontHandle* handle, uint32_t codepoint);

// Get kerning between two codepoints (returns 0 if no kerning)
float font_get_kerning(FontHandle* handle, uint32_t left, uint32_t right);

// ============================================================================
// Glyph Rasterization — bitmap rendering for display
// ============================================================================

typedef enum GlyphRenderMode {
    GLYPH_RENDER_NORMAL,        // 8-bit anti-aliased
    GLYPH_RENDER_LCD,           // LCD subpixel (3x width)
    GLYPH_RENDER_MONO,          // 1-bit (for hit testing)
    GLYPH_RENDER_SDF,           // signed distance field
} GlyphRenderMode;

typedef struct GlyphBitmap {
    uint8_t* buffer;            // pixel data (owned by cache, valid until eviction)
    int      width, height;     // bitmap dimensions
    int      pitch;             // bytes per row
    int      bearing_x;        // left offset from pen position
    int      bearing_y;        // top offset from baseline
    GlyphRenderMode mode;
} GlyphBitmap;

// Render a glyph to bitmap (result is cached; pointer valid until cache eviction)
const GlyphBitmap* font_render_glyph(FontHandle* handle, uint32_t codepoint,
                                      GlyphRenderMode mode);

// ============================================================================
// Text Measurement — convenience for layout
// ============================================================================

typedef struct TextExtents {
    float width;                // total advance width
    float height;               // ascender - descender
    int   glyph_count;          // number of glyphs shaped
} TextExtents;

// Measure a UTF-8 text string (applies kerning, handles multi-byte)
TextExtents font_measure_text(FontHandle* handle, const char* text, int byte_len);

// Measure width of a single codepoint (convenience wrapper)
float font_measure_char(FontHandle* handle, uint32_t codepoint);

// ============================================================================
// Font Fallback — for codepoints not covered by the primary font
// ============================================================================

// Find a font that supports a specific codepoint, given a style hint.
// Searches registered font face descriptors first, then system fonts.
// Returns NULL if no font covers this codepoint.
FontHandle* font_resolve_for_codepoint(FontContext* ctx, const FontStyle* style,
                                        uint32_t codepoint);

// Check whether a handle supports a codepoint (without loading glyph)
bool font_supports_codepoint(FontHandle* handle, uint32_t codepoint);

// ============================================================================
// Font Face Management — register, query, and load font face descriptors
// ============================================================================
//
// Separation of concerns:
//   - CSS @font-face PARSING lives in Radiant (radiant/font_face.cpp).
//     Radiant walks the stylesheet, extracts family/weight/style/src, and
//     calls the registration API below.
//   - Font face MANAGEMENT lives here: storing descriptors, matching them
//     by criteria, loading the actual font data, and caching loaded faces.
//

// Descriptor for a single font face source (path or data URI + format hint)
typedef struct FontFaceSrc {
    const char* path;           // local file path or data URI
    const char* format;         // "truetype", "opentype", "woff", "woff2", or NULL
} FontFaceSrc;

// Descriptor for a registered font face (one @font-face rule = one descriptor)
typedef struct FontFaceDescriptor {
    const char*   family;       // font-family value
    FontWeight    weight;       // font-weight (100–900)
    FontSlant     slant;        // font-style (normal/italic/oblique)
    FontFaceSrc*  sources;      // ordered src list (tried in order)
    int           source_count;
} FontFaceDescriptor;

// Register a font face descriptor (called by Radiant after parsing @font-face).
// The descriptor is copied into the font module's pool/arena.
// Returns true on success.
bool font_face_register(FontContext* ctx, const FontFaceDescriptor* desc);

// Find the best-matching registered font face for a given style.
// Returns NULL if no registered face matches. Does NOT load the font;
// use font_face_load() on the result to get a FontHandle.
const FontFaceDescriptor* font_face_find(FontContext* ctx, const FontStyle* style);

// List all registered descriptors for a family name.
// Returns count; fills `out` up to `max_out` entries.
int font_face_list(FontContext* ctx, const char* family,
                   const FontFaceDescriptor** out, int max_out);

// Load a font from a registered descriptor (tries sources in order,
// handles all formats: TTF/OTF/TTC/WOFF/WOFF2/data URI).
// Returns a cached FontHandle if already loaded.
FontHandle* font_face_load(FontContext* ctx, const FontFaceDescriptor* desc,
                            float size_px);

// Remove all registered descriptors (e.g., on document unload)
void font_face_clear(FontContext* ctx);

// ============================================================================
// Direct Font Loading — for non-CSS use cases (PDF, CLI, tests)
// ============================================================================

// Load a font from a local file path (any format: TTF/OTF/TTC/WOFF/WOFF2).
FontHandle* font_load_from_file(FontContext* ctx, const char* path,
                                const FontStyle* style);

// Load a font from a data URI (base64-encoded, possibly WOFF2-compressed).
FontHandle* font_load_from_data_uri(FontContext* ctx, const char* data_uri,
                                     const FontStyle* style);

// Load a font from raw bytes in memory (any format, auto-detected).
// The buffer is copied into the arena; caller can free their copy.
FontHandle* font_load_from_memory(FontContext* ctx, const uint8_t* data,
                                   size_t len, const FontStyle* style);

// ============================================================================
// Cache Control
// ============================================================================

// Persist font database to disk (call on shutdown or periodically)
bool font_cache_save(FontContext* ctx);

// Evict least-recently-used entries from glyph cache
void font_cache_trim(FontContext* ctx);

// Get cache statistics for diagnostics
typedef struct FontCacheStats {
    int face_count;             // currently loaded faces
    int glyph_cache_count;      // cached glyph entries
    int glyph_cache_hit_rate;   // percentage (0-100)
    size_t memory_usage_bytes;  // approximate memory footprint
    int database_font_count;    // fonts in database
    int database_family_count;  // font families
} FontCacheStats;

FontCacheStats font_get_cache_stats(FontContext* ctx);

#endif // LAMBDA_FONT_H
```

---

## 5. Migration Strategy: From Current Code to Unified Module

The key principle is **inherit, adapt, unify** — not rewrite. Every piece of the new module is sourced from existing code.

### 5.1 Code Provenance Map

| New File | Primary Source | What Changes |
|----------|---------------|--------------|
| `font_context.c` | `radiant/ui_context.cpp` (init code) | Extract `FT_Library` init, LCD filter setup, config into standalone context. One instance shared by layout + PDF. |
| `font_database.c` | `lib/font_config.c` (~2100 lines) | Move as-is. Remove `g_global_font_db` singleton; `FontContext` owns the database. Implement the disk cache stubs. |
| `font_loader.c` | `radiant/font.cpp` (`load_styled_font`, `try_load_font`) + `radiant/font_face.cpp` (`load_font_from_data_uri`, `load_local_font_file`) | Merge all face-loading paths into one. Unify the 4 duplicated `FT_Select_Size` code paths into a single `select_best_fixed_size()` helper. |
| `font_metrics.c` | `radiant/font.cpp` (`setup_font` metric extraction) + `radiant/font_face.cpp` (`compute_enhanced_font_metrics`) + `radiant/font_lookup_platform.c` (`get_font_metrics_platform`) | Consolidate metric computation. Always read OS/2, hhea, typo tables. Cache per-face-size pair. |
| `font_glyph.c` | `radiant/font.cpp` (`load_glyph`) | Same logic, plus add the missing advance cache. Unified fallback-glyph loading (currently in `load_glyph` with `glyph_fallback_cache`). |
| `font_cache.c` | New, but logic exists scattered: face cache (`fontface_map`), fallback cache (`glyph_fallback_cache`), char width cache (per-descriptor), data URI cache | Centralize all caches. Use LRU eviction for glyph bitmaps. Implement disk cache for `FontDatabase`. |
| `font_fallback.c` | `radiant/font.cpp` (`generic_families[]`, fallback logic in `setup_font`) + `radiant/font_face.cpp` (`build_fallback_chain`, `resolve_font_for_codepoint`) | Single fallback chain builder. Merge the two generic-family tables. Per-codepoint cache. |
| `font_face.c` | `radiant/font_face.cpp` (`register_font_face`, font face matching/scoring, `FontFaceDescriptor` storage, `load_font_with_descriptors`) | Move descriptor registry, matching logic, and loading orchestration. CSS parsing stays in Radiant. |
| `font_platform.c` | `radiant/font_lookup_platform.c` + macOS/Linux/Windows sections of `lib/font_config.c` | Merge platform discovery code. Keep CoreText metrics for macOS. Add Windows `DirectWrite` discovery (future). |

**Boundary with Radiant:** CSS `@font-face` rule **parsing** (walking stylesheets, extracting `font-family`/`src`/`font-weight` from CSS AST) stays in `radiant/font_face.cpp`. After parsing, Radiant calls `font_face_register()` to hand the descriptor to the font module. All font face **management** (storing descriptors, matching by criteria, loading sources in format-priority order, caching loaded faces) lives in `lib/font/font_face.c`.
| `font_decompress.c` | `radiant/font_face.cpp` (`decompress_woff2_to_ttf`) | Extract WOFF2 shim. **Add full WOFF1 decompressor** (~80 lines, uses zlib `uncompress()`). Replace `std::string` with arena-allocated buffer. All decompressed output is `arena_alloc`'d so it lives as long as the `FontHandle`. |
| `font_internal.h` | New | All `FT_*` types, internal structs, helper macros. NOT included by any file outside `lib/font/`. |

### 5.2 FreeType Source Inclusion

Instead of linking FreeType as a system library, **vendor the specific source files we need** directly into the build:

```
lib/font/freetype/          # vendored from FreeType repo (git subtree or copy)
├── include/                # FreeType public headers (only used by font_internal.h)
├── src/
│   ├── base/               # ft_init, ft_glyph, ft_bitmap, ft_sfnt, ft_system
│   ├── truetype/           # TrueType interpreter
│   ├── cff/                # CFF/OpenType support
│   ├── sfnt/               # SFNT table parsing
│   ├── smooth/             # Anti-aliased rasterizer
│   ├── autofit/            # Auto-hinting
│   ├── psnames/            # PostScript name mapping
│   └── pshinter/           # PostScript hinting
```

**What we skip from FreeType:** BDF, PCF, PFR, Type1, Type42, WinFNT drivers — formats we never encounter.

**Build integration:** Add as a static library target in `build_lambda_config.json`. Single compilation unit approach (FreeType supports `#include`-based amalgamation via `ftmodule.h`).

### 5.3 libwoff2 + zlib Source Inclusion

Already done for WOFF2 — sources live in `mac-deps/woff2/`. Continue building `libwoff2dec.a` + `libwoff2common.a` + Brotli from source. The only change is `font_decompress.c` wraps the C++ API with a pure-C interface that outputs into an arena buffer (see §3.5).

For WOFF1, no new library is needed — `zlib` is already linked (FreeType dependency). The WOFF1 decompressor in `font_decompress.c` calls `uncompress()` per-table, writing into `arena_alloc`'d output.

### 5.4 Phased Migration

**Phase 1 — Foundation ✅ COMPLETE (2026-02-14)**
- ✅ Create `lib/font/` directory structure.
- ✅ Write `font.h` (public API, ~322 lines) and `font_internal.h` (~357 lines).
- ✅ Implement `font_context.c`: single `FontContext` wrapping `FT_Library` + config (~353 lines).
- ✅ Implement `font_database.c`: font discovery, TTF/OTF metadata parsing, matching (~950 lines).
- ✅ Implement `font_platform.c`: macOS/Linux/Windows directory discovery (~180 lines).
- ✅ Implement `font_loader.c`: unified face loading with format detection (~309 lines).
- ✅ Implement `font_decompress.cpp`: WOFF1 (zlib) + WOFF2 (libwoff2) decompression (~280 lines).
- ✅ Implement `font_metrics.c`: full OpenType metric extraction with OS/2, hhea, typo tables (~195 lines).
- ✅ Implement `font_glyph.c`: glyph loading with per-handle advance cache (~257 lines).
- ✅ Implement `font_cache.c`: face cache with LRU eviction + `font_resolve()` pipeline (~260 lines).
- ✅ Implement `font_fallback.c`: unified generic-family table + codepoint fallback cache (~215 lines).
- ✅ Implement `font_face.c`: @font-face descriptor registry with weight/slant scoring (~278 lines).
- ✅ Update `build_lambda_config.json`: added `"lib/font"` to `source_dirs`.
- ✅ **Build:** 0 errors, 0 warnings.
- ✅ **Test:** Lambda baseline 224/224, Radiant baseline 1972/1972 — all pass.

**Implementation notes (Phase 1):**
- Old `lib/font_config.c` coexists with new `lib/font/` module during migration. Naming conflict on `font_add_scan_directory` resolved by renaming the new function to `font_context_add_scan_directory`.
- New database functions use `_internal` suffix to avoid symbol collisions.
- FreeType remains linked as system library (`/opt/homebrew/opt/freetype`) for now; vendoring deferred to Phase 4.
- `font_decompress.cpp` (not `.c`) — the only C++ file, because libwoff2's `WOFF2MemoryOut` requires C++ construction. The `std::string` elimination uses `ComputeWOFF2FinalSize()` + arena pre-allocation instead.
- `lib/` API discoveries during build: `arraylist_new(0)` / `arraylist_append()` (not `arraylist_create`/`arraylist_add`), `str_icmp` takes 4 args `(a, a_len, b, b_len)`, `base64_decode` returns `uint8_t*`, no `file_read()` utility exists (uses manual `fopen`/`fread`).

**Phase 2 — Loading & Metrics (Week 3-4)**
- Implement `font_loader.c` by extracting from `font.cpp` + `font_face.cpp`. Unified format pipeline: detect → decompress → FT_New_Memory_Face.
- Implement `font_decompress.c`: WOFF2 wrapper (from existing `decompress_woff2_to_ttf`) + **new WOFF1 decompressor** (~80 lines using zlib). Replace `std::string` with `arena_alloc`.
- Implement `font_metrics.c` with full OpenType metric reading.
- Refactor `FontProp.ft_face` → `FontHandle*` in `view.hpp`.
- Update `is_font_file()` in database to accept `.woff` and `.woff2` extensions.
- **Test:** Layout tests still pass. Glyph metrics match old values exactly. WOFF1 + WOFF2 files load correctly.

**Phase 3 — Caching & Fallback (Week 5-6) ✅ COMPLETE (2026-02-14)**
- ✅ `font_cache.c` with LRU face cache + `font_resolve()` 7-step pipeline — already implemented in Phase 1.
- ✅ `font_fallback.c` with unified generic-family table + codepoint fallback cache — already implemented in Phase 1.
- ✅ `font_face.c`: descriptor registry, matching, and load orchestration — already implemented in Phase 1.
- ✅ Disk cache for font database: binary format with magic/version header, length-prefixed strings, file mtime/size validation. `font_context_scan()` tries cache first, saves after full scan.
- ✅ Bitmap cache: `BitmapCacheEntry` wired into `font_render_glyph()` with lazy hashmap init and clear-on-full eviction.
- ✅ Advance cache eviction: `ADVANCE_CACHE_MAX_ENTRIES` (4096) per `FontHandle` with clear-on-full.
- ✅ Bridge `radiant/font_face.cpp` → unified module: `register_font_face()` now calls `font_face_register()` with CssEnum→FontWeight/FontSlant conversion.
- ✅ Slimmed `radiant/font_face.h`: removed 7 unused types and ~30 unused function declarations.
- **Test:** Lambda baseline 224/224, Radiant baseline 1972/1972 — all pass.

**Phase 4 — Cleanup (Week 7)**
- ✅ Removed `FT_SFNT_NAMES_H` from `view.hpp` (unused — no callers use `FT_Get_Sfnt_Name`).
- ✅ Removed redundant FT includes from `font_face.h` (already provided by `view.hpp`).
- ✅ Slimmed `font.h`: removed `FontfaceEntry` type and `fontface_compare`/`fontface_entry_free` declarations (internal to `font.cpp`).
- ✅ Removed ~335 lines of unused stubs from `font_face.cpp`: `EnhancedFontBox`, `FontMatchCriteria`, `FontMatchResult`, `FontFallbackChain` types and 12 stub functions (`calculate_font_match_score`, `find_best_font_match`, `build_fallback_chain`, `font_supports_codepoint`, `resolve_font_for_codepoint`, `cache_codepoint_font_mapping`, `compute_enhanced_font_metrics`, `calculate_line_height_from_css`, `apply_pixel_ratio_to_font_metrics`, `scale_font_size_for_display`, `ensure_pixel_ratio_compatibility`, `setup_font_enhanced`). All duplicated by `lib/font/` or never called.
- ✅ Removed unused `cache_character_width`/`get_cached_char_width` declarations from `font_face.h`.
- ⏸ Kept `FT_FREETYPE_H` in `view.hpp` — 15+ downstream files depend on `FT_Face`/`FT_Library` types. Full type elimination requires migrating all call sites to use `FontHandle`, deferred to future work.
- ⏸ Kept `radiant/font.cpp`, `radiant/font_lookup_platform.c`, `lib/font_config.c/h` — still actively used. `font_lookup_platform.c` provides CoreText metrics (CTFontGetAscent/Descent/Leading with Chrome's 15% hack) not yet replicated in `lib/font/`.
- ⏸ PDF `FT_Library` kept separate — PDF font loading runs at lambda/input layer (no `UiContext` available), correctly isolated.
- **Test:** Lambda baseline 224/224, Radiant baseline 1972/1972 — all pass.

---

## 6. Caching Architecture

### 6.1 Four-Tier Cache

```
┌─────────────────────────────────────────────────────┐
│ Tier 1: Font Database Cache (disk)                  │
│  ~/.lambda/font_cache.bin                           │
│  Persists scan results across process restarts.     │
│  Invalidated by file mtime/size changes.            │
│  ✅ Implemented (Phase 3)                            │
└─────────────────────┬───────────────────────────────┘
                      ▼
┌─────────────────────────────────────────────────────┐
│ Tier 2: Face Cache (memory, LRU)                    │
│  (family, weight, slant, size) → FontHandle         │
│  Max 64 open faces. LRU eviction closes FT_Face.    │
│  ✅ Implemented (Phase 1)                            │
└─────────────────────┬───────────────────────────────┘
                      ▼
┌─────────────────────────────────────────────────────┐
│ Tier 3: Glyph Metrics Cache (memory, per-face)      │
│  codepoint → GlyphInfo (advance, bearings)          │
│  4096 entries per FontHandle, clear-on-full.         │
│  ✅ Implemented (Phase 1 + Phase 3 eviction)         │
└─────────────────────┬───────────────────────────────┘
                      ▼
┌─────────────────────────────────────────────────────┐
│ Tier 4: Glyph Bitmap Cache (memory, LRU)            │
│  (glyph_id, size, render_mode) → GlyphBitmap        │
│  Max 4096 entries. Clear-on-full eviction.            │
│  ✅ Implemented (Phase 3)                            │
└─────────────────────────────────────────────────────┘
```

### 6.2 Fallback Resolution Cache

```
┌─────────────────────────────────────────────────────┐
│ Codepoint → FontHandle (per fallback chain)         │
│  When a codepoint isn't in the primary font, the    │
│  fallback chain is walked once. Result is cached.   │
│  Includes negative caching (no font has this glyph).│
│  ✅ Implemented (Phase 1)                            │
└─────────────────────────────────────────────────────┘
```

### 6.3 Memory Management

All cache memory uses Lambda's pool/arena allocators (see §3.5 for the full architecture):

| Cache Tier | Data Stored | Allocator | Deallocation |
|-----------|-------------|-----------|-------------|
| **Tier 1 (disk)** | Serialized `FontEntry` records | Temp `arena_alloc` for read buffer, `arena_reset` after parse | Bulk reset |
| **Tier 2 (faces)** | `FontHandle` structs (contain `FT_Face`) | `pool_calloc(ctx->pool, sizeof(FontHandle))` | `pool_free` on LRU eviction (after `FT_Done_Face`) |
| **Tier 2 (faces)** | Decompressed WOFF1/WOFF2 buffers (must outlive `FT_Face`) | `arena_alloc(ctx->arena, sfnt_size)` | Lives until `arena_destroy` on shutdown |
| **Tier 3 (metrics)** | `GlyphInfo` cache entries (~32 bytes each) | `pool_calloc` | Never evicted (tiny, per-face) |
| **Tier 4 (bitmaps)** | `GlyphBitmap` structs + pixel buffers | Struct: `pool_calloc`. Pixels: `arena_alloc(ctx->glyph_arena, pitch*rows)` | `pool_free` struct on LRU eviction; `arena_reset(glyph_arena)` on full flush |
| **Fallback cache** | Codepoint → `FontHandle*` mapping | Hashmap entries via `pool_calloc` | `pool_free` on invalidation |
| **Strings** | Family names, file paths, PostScript names | `arena_strdup(ctx->arena, str)` | Lives until `arena_destroy` |

**Invariant:** No `malloc`/`free` anywhere in `lib/font/`. No `std::string` or `std::vector` (the single `std::string` in `decompress_woff2` is scoped to one function and immediately copied into arena memory). FreeType's internal allocations also route through our pool via `FT_Memory` (see §3.5).

---

## 7. Radiant Integration Changes

### 7.1 UiContext Simplification

```c
// BEFORE (view.hpp)
typedef struct {
    FontDatabase *font_db;
    FT_Library ft_library;
    struct hashmap* fontface_map;
    FontProp default_font;
    FontProp legacy_default_font;
    char** fallback_fonts;
    FontFaceDescriptor** font_faces;
    int font_face_count, font_face_capacity;
    struct hashmap* glyph_fallback_cache;
    float pixel_ratio;
    // ... other fields
} UiContext;

// AFTER
typedef struct {
    FontContext *font_ctx;          // ← single opaque pointer replaces 9 fields
    float pixel_ratio;
    // ... other fields (window, surface, mouse, document)
} UiContext;
```

### 7.2 FontProp / FontBox Simplification

```c
// BEFORE
struct FontProp {
    char* family;
    float font_size;
    CssEnum font_style, font_weight;
    float space_width, ascender, descender, font_height;
    bool has_kerning;
    void* ft_face;                  // raw FT_Face leaked into view layer
};

struct FontBox {
    FontProp *style;
    FT_Face ft_face;                // FT_Face again
    int current_font_size;
};

// AFTER
struct FontProp {
    char* family;
    float font_size;
    CssEnum font_style, font_weight;
    CssEnum text_deco;
    float letter_spacing;
    // derived metrics (populated by font_resolve)
    float space_width, ascender, descender, font_height;
    bool has_kerning;
    FontHandle* handle;             // opaque, ref-counted
};

struct FontBox {
    FontProp *style;
    FontHandle* handle;             // opaque — no FT_Face
    int current_font_size;
};
```

### 7.3 Layout Integration

```c
// BEFORE (setup_font in font.cpp — ~80 lines, direct FreeType calls)
void setup_font(UiContext* uicon, FontBox *fbox, FontProp *fprop) {
    // ... resolve generic family
    // ... try @font-face descriptors (stays in radiant/font_face.cpp)
    // ... try database lookup
    // ... try platform fallback
    // ... FT_Set_Pixel_Sizes
    // ... extract metrics from FT_Face
}

// AFTER (thin wrapper — ~10 lines)
void setup_font(UiContext* uicon, FontBox *fbox, FontProp *fprop) {
    FontStyle style = {
        .family = fprop->family,
        .size_px = fprop->font_size / uicon->pixel_ratio,
        .weight = css_weight_to_font_weight(fprop->font_weight),
        .slant = css_style_to_font_slant(fprop->font_style),
    };
    FontHandle* h = font_resolve(uicon->font_ctx, &style);
    if (!h) return;

    const FontMetrics* m = font_get_metrics(h);
    fprop->space_width  = m->space_width;
    fprop->ascender     = m->ascender;
    fprop->descender    = m->descender;
    fprop->font_height  = m->line_height;
    fprop->has_kerning  = m->has_kerning;
    fprop->handle       = h;

    fbox->handle = h;
    fbox->style = fprop;
    fbox->current_font_size = (int)fprop->font_size;
}
```

### 7.4 Text Measurement in Layout

```c
// BEFORE (layout_inline.cpp — direct FreeType glyph loading + manual kerning)
FT_UInt gi = FT_Get_Char_Index(lycon->font.ft_face, ch);
FT_Load_Glyph(lycon->font.ft_face, gi, FT_LOAD_DEFAULT);
float advance = lycon->font.ft_face->glyph->advance.x / 64.0f;
if (FT_HAS_KERNING(lycon->font.ft_face)) {
    FT_Vector kern;
    FT_Get_Kerning(lycon->font.ft_face, prev, gi, FT_KERNING_DEFAULT, &kern);
    advance += kern.x / 64.0f;
}

// AFTER (one call, cached)
GlyphInfo g = font_get_glyph(fbox->handle, ch);
float advance = g.advance_x + font_get_kerning(fbox->handle, prev_cp, ch);
```

### 7.5 PDF Font Unification

The PDF subsystem (`radiant/pdf/fonts.cpp`) currently creates its own `FT_Library`. After migration:

```c
// BEFORE
static FT_Library pdf_ft_library;  // separate instance
FT_Init_FreeType(&pdf_ft_library);

// AFTER
// PDF renderer receives the shared FontContext from the document
FontHandle* h = font_resolve(doc->font_ctx, &style);
```

---

## 8. Additional Recommendations

### 8.1 Color Font Support (COLR/CPAL, sbix, CBDT/CBLC)

Beyond the core outline formats (TTF/OTF/WOFF/WOFF2), modern fonts include **color glyph data** for emoji and icons. FreeType already handles the three major color formats:
- **Apple `sbix`**: Bitmap-based (Apple Color Emoji). Already works via `FT_Select_Size` in our codebase.
- **Google `CBDT/CBLC`**: Bitmap-based (Noto Color Emoji). Same loading path as `sbix`.
- **Microsoft `COLR/CPAL`**: Vector-based layered glyphs. FreeType 2.13+ renders these.

The unified module's `GlyphInfo.is_color` flag already distinguishes color glyphs so renderers can handle them correctly (e.g., skip LCD subpixel filtering for emoji). No additional work needed — this comes free from FreeType.

### 8.2 HarfBuzz Text Shaping (Future)

The current system does character-by-character glyph loading with manual kerning. For proper international text (Arabic, Devanagari, Thai, advanced Latin ligatures), **HarfBuzz** shaping is needed. The unified font module makes this a clean addition:

```c
// Future API extension:
typedef struct ShapedGlyph {
    GlyphId id;
    float x_offset, y_offset;
    float x_advance, y_advance;
    int cluster;                // maps back to source text position
} ShapedGlyph;

int font_shape_text(FontHandle* handle, const char* text, int byte_len,
                    const char* language, ShapedGlyph* out, int max_glyphs);
```

HarfBuzz is already linked for FreeType on some platforms. Vendoring it alongside FreeType in `lib/font/harfbuzz/` would be consistent with the approach.

### 8.3 Variable Font Support (Future)

OpenType variable fonts (`.ttf` with `fvar` table) allow a single file to express a continuous range of weights, widths, and slants. FreeType already supports this via `FT_Set_Var_Design_Coordinates()`. The unified module can expose it cleanly:

```c
// Future API extension:
typedef struct FontAxis {
    uint32_t tag;               // e.g., 'wght', 'wdth', 'ital'
    float min, max, default_val;
} FontAxis;

int font_get_axes(FontHandle* handle, FontAxis* axes, int max_axes);
bool font_set_axis(FontHandle* handle, uint32_t tag, float value);
```

### 8.4 Font Subsetting for PDF Output

When embedding fonts in PDF output, we currently embed entire font files. A font subsetter (keep only glyphs actually used in the document) would dramatically reduce PDF size. FreeType provides the glyph data; we just need a SFNT table builder. This is a natural fit for `font_loader.c`.

### 8.5 Thread Safety Considerations

FreeType's `FT_Library` is **not thread-safe** across faces. If Lambda ever moves to multi-threaded layout:
- One `FT_Library` per thread, or
- Mutex around face loading (face *access* after loading is thread-safe for read-only ops in FreeType 2.13+).

The opaque `FontContext` makes either approach possible without API changes.

---

## 9. Build System Changes

### 9.1 `build_lambda_config.json` Updates

```jsonc
{
    // NEW: vendored FreeType (replaces system freetype dependency)
    "name": "lambda_font",
    "type": "static_lib",
    "sources": [
        "lib/font/*.c",
        "lib/font/freetype/src/base/ftsystem.c",
        "lib/font/freetype/src/base/ftinit.c",
        "lib/font/freetype/src/base/ftglyph.c",
        // ... selected FreeType source files
    ],
    "include": [
        "lib/font/freetype/include"
    ],
    "links": [
        "woff2dec", "woff2common",  // WOFF2 decompression
        "brotlidec", "brotlicommon", // Brotli (WOFF2 dependency)
        "z"                          // zlib (WOFF1 decompression + FreeType dependency)
    ]
}
```

### 9.2 Removed Dependencies

| Dependency | Status |
|-----------|--------|
| System `libfreetype.a` | **Replaced** by vendored sources |
| `libwoff2dec.a` + `libwoff2common.a` | **Kept** (already built from source, wraps WOFF2 decompression) |
| Brotli | **Kept** (WOFF2 dependency, already built from source) |
| zlib | **Kept** (system lib, used by FreeType, WOFF1 decompression, and PNG) |
| HarfBuzz (`libharfbuzz.a`) | **Kept** for now (FreeType may need it); evaluate vendoring later |

---

## 10. Testing Plan

| Test Category | Method |
|--------------|--------|
| **Unit tests** | New `test/test_font_gtest.cpp`: test `font_resolve`, `font_get_metrics`, `font_get_glyph`, `font_measure_text`, `font_face_register`, `font_face_find`, `font_face_load`, cache behavior |
| **Regression** | All existing layout baseline tests (`make test-radiant-baseline`) must pass with identical output |
| **Metrics compatibility** | Compare `font_get_metrics()` output against current `setup_font()` extracted metrics for the top 20 fonts. Differences must be zero. |
| **Performance** | Benchmark `font_measure_text()` vs current inline FreeType path. Expect ≥2x speedup from advance caching |
| **WOFF1** | Test loading `.woff` files from disk and data URI. Verify decompressed metrics match the equivalent `.ttf` |
| **WOFF2** | Test loading `.woff2` files from disk and data URI. Verify metrics match TTF equivalent |
| **All formats** | Round-trip test: for each of the top 5 fonts, load TTF, OTF, WOFF1, WOFF2 variants and verify identical `FontMetrics` |
| **Cache** | Test LRU eviction, disk cache save/load, cache invalidation on font file change |
| **Memory accounting** | Verify `font_get_cache_stats().memory_usage_bytes` tracks actual pool/arena usage. Verify zero leaked allocations after `font_context_destroy()` |
| **Cross-platform** | CI runs on macOS (ARM), Linux (x86_64), Windows (MSYS2/MinGW) |

---

## 11. Summary

| Aspect | Before | After |
|--------|--------|-------|
| **Files** | 7+ across `lib/` and `radiant/` | 11 in `lib/font/` (font face management included) + `radiant/font_face.cpp` (only CSS @font-face parsing stays in Radiant) |
| **Font formats** | TTF/OTF/TTC only (WOFF1 broken, WOFF2 partial) | TTF, OTF, TTC, WOFF1, WOFF2 — all first-class |
| **Format pipeline** | Scattered across `font.cpp`, `font_face.cpp`, `font_config.c` | Single `font_loader.c` detect→decompress→load path |
| **FT_* exposure** | `view.hpp`, `layout.hpp`, `font.h`, `font_face.h` | Only `font_internal.h` |
| **FT_Library instances** | 2 (layout + PDF) | 1 (shared via FontContext) |
| **FreeType memory** | Uses default `malloc`/`free` internally | Routed through `Pool` via `FT_Memory` |
| **WOFF1 support** | ❌ Detected but silent failure | ✅ zlib decompression → arena buffer |
| **WOFF2 support** | ⚠️ Only data URI path, uses `std::string` | ✅ File + data URI, arena-allocated output |
| **System font scanning** | Skips `.woff`/`.woff2` files | Scans all font formats |
| **Glyph advance cache** | ❌ none (TODO comment) | ✅ per-face hashmap (pool-allocated entries) |
| **Glyph bitmap cache** | ❌ re-rasterized each frame | ✅ LRU cache (4096 entries, glyph_arena) |
| **Disk font cache** | ❌ stubs | ✅ implemented |
| **Generic family tables** | 2 divergent copies | 1 authoritative table |
| **Fixed-size font handling** | 4 copy-pasted blocks | 1 `select_best_fixed_size()` |
| **API surface for callers** | ~25 functions + raw FT types | ~15 functions, all opaque |
| **Memory management** | Mixed `malloc`/`free`/`new`/`std::string` + some pool/arena | 100% pool/arena (`Pool` for objects, `Arena` for strings+buffers, `glyph_arena` for bitmaps) |
| **`std::string` / `std::vector`** | Used in WOFF2 path, leaks C++ semantics | Confined to single 8-line shim function |

The work is primarily **reorganization and consolidation**, not greenfield implementation. Every line of the new module traces back to existing, tested code. The result is a clean module boundary that isolates font complexity from the rest of the system and opens the door for future improvements (HarfBuzz shaping, variable fonts, font subsetting) without touching layout or rendering code.

### Phase 1 Completed Files

| File | Lines | Key Contents |
|------|------:|-------------|
| `font.h` | 322 | Public API: `FontContext`, `FontHandle`, `FontMetrics`, `GlyphInfo`, `TextExtents`, `FontFaceDesc`, all `font_*()` functions |
| `font_internal.h` | 359 | Internal structs (`FontContext`, `FontHandle` full layout), FT_* includes, helper macros, `_internal` function declarations |
| `font_context.c` | 390 | `FT_Memory` pool routing, `font_context_create/destroy`, `font_handle_retain/release`, disk cache integration in `font_context_scan()`, `font_cache_save()` API |
| `font_database.c` | 1200 | TTF table parsing (name, OS/2), directory scanning, metadata extraction, TTC support, format detection, **disk cache save/load** (binary format with mtime/size validation) |
| `font_platform.c` | 180 | macOS (`/System/Library/Fonts`, `~/Library/Fonts`), Linux (`/usr/share/fonts`, `~/.fonts`), Windows stubs |
| `font_loader.c` | 309 | Unified `font_load_from_file/data_uri/memory`, `select_best_fixed_size()`, format detect → decompress → `FT_New_Memory_Face` |
| `font_decompress.cpp` | 280 | WOFF1 per-table zlib decompression, WOFF2 via `ComputeWOFF2FinalSize` + `WOFF2MemoryOut`, `font_decompress_if_needed()` dispatcher |
| `font_metrics.c` | 195 | OS/2 table (typo metrics, `USE_TYPO_METRICS` bit 7), x_height/cap_height (table → glyph → estimate), space_width, kerning flag |
| `font_glyph.c` | 290 | Per-handle advance cache (hashmap, 4096 max with eviction), **bitmap cache** (lazy init, clear-on-full), `font_get_glyph/kerning`, `font_render_glyph`, `font_measure_text/char` |
| `font_cache.c` | 260 | Face cache ("family:weight:slant:size" key), LRU eviction, **`font_resolve()`** top-level pipeline: cache → @font-face → generic → database → platform → fallback |
| `font_fallback.c` | 215 | Generic CSS families (serif, sans-serif, monospace, cursive, fantasy, system-ui, ui-monospace + Apple/cross-platform aliases), codepoint fallback with negative caching |
| `font_face.c` | 278 | `font_face_register/find/list/load/clear`, weight/slant distance scoring, pool/arena-copied descriptors |
