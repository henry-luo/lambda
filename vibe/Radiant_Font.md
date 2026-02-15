# Unified Font Module for Lambda/Radiant

> **Status:** All 9 phases complete — 0 build errors · Lambda 224/224 ✅ · Radiant 1972/1972 ✅

---

## 1. Motivation

Lambda's font handling grew organically across 7+ files in `lib/` and `radiant/` with overlapping responsibilities, duplicated logic, and inconsistent abstractions. The code works but is fragile and hard to extend.

### Pain Points

| Problem | Where |
|---------|-------|
| **Scattered ownership** — no clear module boundary | `lib/font_config.c`, `radiant/font.cpp`, `radiant/font_face.cpp`, `radiant/font_lookup_platform.c`, `radiant/font.h`, `radiant/font_face.h`, `radiant/pdf/fonts.cpp` |
| **Duplicate `FT_Library`** — layout and PDF each create their own, no face sharing | `radiant/ui_context.cpp` vs `radiant/pdf/fonts.cpp` |
| **Copy-pasted `FT_Select_Size`** — color emoji handling duplicated 4 times | `font.cpp`, `font_face.cpp` |
| **Duplicate generic-family tables** — serif/sans-serif/monospace defined independently in two places | `radiant/font.cpp` |
| **WOFF1 gap** — format detected but no decompressor; WOFF1 fonts silently fail | `radiant/font_face.cpp` |
| **`std::string` in WOFF2** — violates project conventions | `radiant/font_face.cpp` |
| **No glyph advance cache** — recalculated every call (open TODO) | `radiant/font.cpp` |
| **Disk cache stubs** — `FontDatabase` has cache structs but `load_cache()`/`save_cache()` unimplemented | `lib/font_config.c` |
| **FreeType types leak everywhere** — `FT_Face`, `FT_GlyphSlot`, `FT_Library` in public headers | `view.hpp`, `layout.hpp`, `font.h`, `font_face.h` |

### Non-Goals

We are **not** rewriting FreeType or the WOFF2 decoder. We bundle libwoff2's decode-only sources and wrap them (along with FreeType) behind our own API so that:
- Callers never touch `FT_*` types directly.
- Resource lifetimes (faces, glyphs, buffers) are managed in one place.
- Future substitution (e.g., replacing FreeType with stb_truetype for WASM) only changes the backend, not every call site.

---

## 2. Goals

1. **Unify** — One module (`lib/font/`) owns all font functionality: discovery, matching, loading, metrics, glyph rasterization, web font decompression, and caching.
2. **All common font formats** — First-class support for TrueType (`.ttf`), OpenType/CFF (`.otf`), TrueType Collections (`.ttc`), WOFF (`.woff`), and WOFF2 (`.woff2`). Every format loads through a single code path; callers never know or care which format the underlying file is.
3. **Wrap, don't rewrite** — Keep FreeType and Brotli as external backend libraries. Bundle only the minimal decode-only subset of libwoff2 sources directly under `lib/font/woff2/` (5 `.cpp` files, no external linking needed). Expose none of their types in the public API.
4. **Consistent API** — A single `FontContext` replaces `FT_Library` + `FontDatabase` + `UiContext` font fields. All callers go through `font_*()` functions.
5. **Pool/arena memory management everywhere** — All allocations inside the font module use Lambda's `Pool` and `Arena` allocators. Zero `malloc`/`free`/`new`/`delete`. Zero `std::string` or `std::vector`. Decompressed WOFF buffers, glyph bitmaps, cache entries, strings — everything goes through the pool or arena. This gives deterministic cleanup and memory accounting.
6. **Unified resource management** — One cache hierarchy: font file cache → face cache → glyph metrics cache → rasterized glyph cache, all pool/arena-backed with LRU eviction.
7. **Better Radiant integration** — Layout and rendering call the same API. Metrics are computed once and cached. Fallback chains are resolved lazily and cached per-codepoint.
8. **Cross-platform consistency** — Identical behavior on macOS, Linux, Windows. Platform-specific code is isolated to one file (`font_platform.c`).

---

## 3. Architecture

### 3.1 Module Structure

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
├── font_decompress.cpp     # WOFF1 (zlib) + WOFF2 (Brotli/libwoff2) decompression
└── woff2/                  # Bundled minimal libwoff2 decode-only sources (no external lib needed)
    ├── include/woff2/{decode,output}.h   # Public API headers
    ├── woff2_dec.cpp        # Main WOFF2→TTF/OTF decoder
    ├── woff2_out.cpp        # WOFF2MemoryOut output class
    ├── woff2_common.cpp     # Checksum & collection helpers
    ├── table_tags.cpp       # SFNT table tag constants
    └── variable_length.cpp  # Base128/255UShort variable-length encoding
```

> **Note:** `font_decompress` uses `.cpp` (not `.c`) because libwoff2's `WOFF2MemoryOut` API requires C++ construction. The `std::string` is eliminated by pre-computing the output size with `ComputeWOFF2FinalSize()` and arena-allocating the buffer directly.
>
> The `woff2/` subdirectory contains a minimal decode-only subset of Google's libwoff2, copied from `mac-deps/woff2/src/` and renamed `.cc` → `.cpp` for build-system compatibility. This eliminates the external `libwoff2dec.a` / `libwoff2common.a` static library dependencies. Only `libbrotlidec` and `libbrotlicommon` remain as external libraries (also required by FreeType). Covered by `test/test_woff2_gtest.cpp` (26 tests).

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
          │    zlib + woff2/ + Brotli      │              │
          │    (bundled sources)            │              │
          ▼                                ▼              │
   font_platform                    Lambda pool/arena    │
   (CoreText / dirs / Win32)        allocators           │
```

### 3.3 Opaque Handle Design

```c
// Public — callers see these
typedef struct FontContext FontContext;        // opaque
typedef struct FontHandle  FontHandle;         // opaque, ref-counted
typedef uint32_t           GlyphId;            // FreeType glyph index, type-aliased

// Internal — only font_internal.h sees FT_Face, FT_Library, etc.
```

`view.hpp` and `layout.hpp` never `#include <ft2build.h>`. The `FontProp.ft_face` field (previously raw `FT_Face`) is replaced by `FontHandle*`.

### 3.4 Font Format Support

Every common font format works transparently. The caller passes a file path (or data URI, or in-memory buffer) to `font_loader.c`; the loader detects the format, decompresses if needed, and hands raw SFNT bytes to FreeType.

| Format | Extension | Magic Bytes | Decompression | Status |
|--------|-----------|-------------|---------------|--------|
| TrueType | `.ttf` | `0x00010000` / `true` | None | ✅ |
| OpenType/CFF | `.otf` | `OTTO` | None | ✅ |
| TrueType Collection | `.ttc` | `ttcf` | None | ✅ |
| WOFF1 | `.woff` | `wOFF` | zlib per-table inflate | ✅ |
| WOFF2 | `.woff2` | `wOF2` | Brotli via libwoff2 | ✅ |
| Data URI (base64) | N/A | `data:font/` | Base64 decode + format detect | ✅ |

**Loading pipeline:** detect format (magic bytes) → decompress if WOFF/WOFF2 → `FT_New_Memory_Face` → `FT_Set_Pixel_Sizes` → wrap in `FontHandle`.

Key insight: WOFF1 and WOFF2 are just compressed containers around SFNT (TTF/OTF) data. Once decompressed, they're identical. The decompressed buffer is arena-allocated and lives as long as the `FontHandle`.

### 3.5 Font Database: 3-Phase Scan & Matching

`font_database.c` implements a **3-phase lazy scan** to minimize startup cost.

**Phase 1 — Fast File Discovery (no parsing)**
Recursively walk platform font directories. For each font file, create a `FontEntry` placeholder with family name heuristically extracted from the filename (strip extension, strip style suffixes like "Bold"/"Italic", split CamelCase). Skip files < 1KB or > 50MB.

**Phase 2 — Priority Font Parsing (web-safe fonts only)**
Parse placeholders whose family names match priority fonts (Arial, Helvetica, Times, Courier, Verdana, Georgia, etc.). Read the name table for real family/subfamily/weight/style. Parse TTC collections.

**Phase 3 — Organize into Families**
Build hashmap: family_name → `FontFamily` (list of `FontEntry*`). Index by PostScript name and file path.

**Lazy on-demand loading:** When a family not in the hashmap is requested, matching placeholders are parsed in-place and the family index is rebuilt. An uncommon font (e.g. "Papyrus") is never parsed unless actually requested by CSS.

**Match scoring** (100 max, additive with penalties):

| Criterion | Points |
|-----------|--------|
| Family name (exact) | +40 |
| Family name (generic fallback) | +25 |
| Weight proximity (diff 0/≤100/≤200/≤300) | +20/+15/+10/+5 |
| Style match (normal/italic/oblique) | +15 |
| Unicode codepoint support | +15 |
| Monospace preference | +10 |
| Exact filename bonus ("Arial.ttf" for "Arial") | +10 |
| Unicode variant penalty ("Arial Unicode.ttf") | −8 |
| Oversized font penalty (> 5 MB) | −5 |

### 3.6 Pool & Arena Memory Architecture

Every allocation inside the font module uses Lambda's `Pool` or `Arena`.

| Allocator | What It's For | Lifetime |
|-----------|---------------|----------|
| **Pool** | Fixed-size recyclable objects: `FontHandle`, `GlyphInfo`, cache entries, hashmap nodes | Individual `pool_free` on eviction; bulk `pool_destroy` on shutdown |
| **Arena** | Append-only long-lived data: strings, decompressed WOFF buffers, font file data | Bulk `arena_destroy` — no individual frees |
| **Glyph Arena** | Glyph bitmap pixel data (separable for flush on font-size change) | `arena_reset` on full flush |

Internal struct layout:

```c
struct FontContext {
    Pool* pool;  Arena* arena;  Arena* glyph_arena;
    FT_Library ft_library;          // single FreeType instance
    FontDatabase* database;
    struct hashmap* face_cache;     // LRU: key → FontHandle*
    struct hashmap* glyph_cache;    // LRU: (handle,codepoint) → GlyphInfo
    struct hashmap* bitmap_cache;   // LRU: (handle,codepoint,mode) → GlyphBitmap
    FontFaceDescriptor** face_descriptors;
    FontContextConfig config;
};

struct FontHandle {
    FT_Face ft_face;  int ref_count;  bool borrowed_face;
    FontMetrics metrics;  bool metrics_ready;
    uint8_t* memory_buffer;  size_t memory_buffer_size;
    struct hashmap* advance_cache;   // codepoint → advance_x
    FontContext* ctx;  uint32_t lru_tick;
};
```

**FreeType custom allocator:** FreeType's internal allocations route through `Pool` via `FT_Memory`, giving accurate memory accounting and deterministic cleanup.

**WOFF2 `std::string` elimination:** The `std::string` is confined to a single function in `font_decompress.cpp`. Output is pre-sized via `ComputeWOFF2FinalSize()` and written directly into an arena buffer via `WOFF2MemoryOut`.

### 3.7 Four-Tier Cache

```
Tier 1: Font Database Cache (disk)
  ~/.lambda/font_cache.bin — persists scan results across restarts.
  Invalidated by file mtime/size changes.

Tier 2: Face Cache (memory, LRU)
  (family, weight, slant, size) → FontHandle — max 64 open faces.

Tier 3: Glyph Metrics Cache (memory, per-face)
  codepoint → GlyphInfo — 4096 entries per FontHandle, clear-on-full.

Tier 4: Glyph Bitmap Cache (memory, LRU)
  (glyph_id, size, render_mode) → GlyphBitmap — max 4096 entries.

Fallback Resolution Cache:
  codepoint → FontHandle — per fallback chain, with negative caching.
```

### 3.8 `font_resolve()` Pipeline

The top-level resolution function implements a 7-step pipeline:

1. **Cache** — check face cache for exact match
2. **@font-face** — check registered font face descriptors
3. **Generic family** — resolve CSS generic families (serif → Times, sans-serif → Arial, etc.)
4. **Database** — query font database with weight/slant scoring
5. **Platform** — platform-specific discovery (CoreText on macOS)
6. **Fallback** — try known fallback fonts for the codepoint
7. **Default** — return system default font

---

## 4. Public API (`font.h`)

```c
// Context lifecycle
FontContext* font_context_create(FontContextConfig* config);
void         font_context_destroy(FontContext* ctx);
bool         font_context_scan(FontContext* ctx);

// Font resolution — the primary entry point
FontHandle* font_resolve(FontContext* ctx, const FontStyle* style);
FontHandle* font_resolve_for_codepoint(FontContext* ctx, const FontStyle* style, uint32_t cp);

// Handle management (ref-counted)
FontHandle* font_handle_retain(FontHandle* handle);
void        font_handle_release(FontHandle* handle);
FontHandle* font_handle_wrap(FontContext* ctx, void* ft_face, const FontStyle* style);

// Metrics (cached, zero-cost after first call)
const FontMetrics* font_get_metrics(FontHandle* handle);
float font_calc_normal_line_height(FontHandle* handle);
float font_get_cell_height(FontHandle* handle);
float font_get_x_height_ratio(FontHandle* handle);

// Glyph operations (cached)
GlyphInfo font_get_glyph(FontHandle* handle, uint32_t codepoint);
uint32_t  font_get_glyph_index(FontHandle* handle, uint32_t codepoint);
float     font_get_kerning(FontHandle* handle, uint32_t left, uint32_t right);
float     font_get_kerning_by_index(FontHandle* handle, uint32_t left_idx, uint32_t right_idx);
const GlyphBitmap* font_render_glyph(FontHandle* handle, uint32_t cp, GlyphRenderMode mode);

// Text measurement
TextExtents font_measure_text(FontHandle* handle, const char* text, int byte_len);
float       font_measure_char(FontHandle* handle, uint32_t codepoint);

// @font-face management
bool font_face_register(FontContext* ctx, const FontFaceDescriptor* desc);
const FontFaceDescriptor* font_face_find(FontContext* ctx, const FontStyle* style);
FontHandle* font_face_load(FontContext* ctx, const FontFaceDescriptor* desc, float size_px);
void font_face_clear(FontContext* ctx);

// Direct loading (PDF, CLI, tests)
FontHandle* font_load_from_file(FontContext* ctx, const char* path, const FontStyle* style);
FontHandle* font_load_from_data_uri(FontContext* ctx, const char* uri, const FontStyle* style);
FontHandle* font_load_from_memory(FontContext* ctx, const uint8_t* data, size_t len, const FontStyle* style);

// Database queries
bool font_family_exists(FontContext* ctx, const char* family);
const char* font_find_path(FontContext* ctx, const char* family);
FontMatchResult font_find_best_match(FontContext* ctx, const char* family, FontWeight w, FontSlant s);

// Cache control
bool font_cache_save(FontContext* ctx);
void font_cache_trim(FontContext* ctx);
FontCacheStats font_get_cache_stats(FontContext* ctx);
```

### Key Types

```c
typedef struct FontStyle {
    const char* family;  float size_px;  FontWeight weight;  FontSlant slant;
} FontStyle;

typedef struct FontMetrics {
    float ascender, descender, line_height, line_gap;
    float typo_ascender, typo_descender, typo_line_gap;
    float win_ascent, win_descent;
    float hhea_ascender, hhea_descender, hhea_line_gap, hhea_line_height;
    float x_height, cap_height, space_width, em_size;
    float underline_position, underline_thickness;
    bool has_kerning, use_typo_metrics;
} FontMetrics;

typedef struct GlyphInfo {
    GlyphId id;  float advance_x, advance_y, bearing_x, bearing_y;
    int width, height;  bool is_color;
} GlyphInfo;
```

---

## 5. Radiant Integration

### 5.1 UiContext Simplification

```c
// BEFORE — 9 font-related fields
typedef struct {
    FontDatabase *font_db;  FT_Library ft_library;  struct hashmap* fontface_map;
    FontProp default_font;  char** fallback_fonts;
    FontFaceDescriptor** font_faces;  struct hashmap* glyph_fallback_cache;
    float pixel_ratio;  ...
} UiContext;

// AFTER — single opaque pointer
typedef struct {
    FontContext *font_ctx;
    float pixel_ratio;  ...
} UiContext;
```

### 5.2 FontProp / FontBox

```c
// BEFORE                              // AFTER
struct FontProp {                      struct FontProp {
    ...                                    ...
    void* ft_face;  // leaked FT_Face      FontHandle* handle;  // opaque
};                                     };

struct FontBox {                       struct FontBox {
    FT_Face ft_face;                       FontHandle* font_handle;  // opaque
    FontProp *style;                       FontProp *style;
    int current_font_size;                 int current_font_size;
};                                     };
```

### 5.3 Layout: setup_font

```c
// BEFORE — ~80 lines, direct FreeType calls
void setup_font(UiContext* uicon, FontBox *fbox, FontProp *fprop) {
    // ... resolve generic family, try @font-face, try database, try platform
    // ... FT_Set_Pixel_Sizes, extract metrics from FT_Face
}

// AFTER — thin wrapper, ~15 lines
void setup_font(UiContext* uicon, FontBox *fbox, FontProp *fprop) {
    FontStyle style = { .family=fprop->family, .size_px=..., .weight=..., .slant=... };
    FontHandle* h = font_resolve(uicon->font_ctx, &style);
    const FontMetrics* m = font_get_metrics(h);
    fprop->space_width = m->space_width;
    fprop->ascender    = m->hhea_ascender;
    fprop->descender   = -(m->hhea_descender);
    fprop->font_height = m->hhea_line_height;
    fprop->has_kerning = m->has_kerning;
    fprop->handle      = h;
    fbox->handle = h;
}
```

### 5.4 Layout: Text Measurement

```c
// BEFORE — direct FreeType glyph loading + manual kerning
FT_UInt gi = FT_Get_Char_Index(ft_face, ch);
FT_Load_Glyph(ft_face, gi, FT_LOAD_DEFAULT);
float advance = ft_face->glyph->advance.x / 64.0f;
if (FT_HAS_KERNING(ft_face)) { FT_Get_Kerning(...); advance += kern.x / 64.0f; }

// AFTER — one call, cached
GlyphInfo g = font_get_glyph(fbox->handle, ch);
float advance = g.advance_x + font_get_kerning(fbox->handle, prev_cp, ch);
```

### 5.5 Separation of Concerns

CSS `@font-face` rule **parsing** (walking stylesheets, extracting `font-family`/`src`/`font-weight` from CSS AST) stays in `radiant/font_face.cpp`. After parsing, Radiant calls `font_face_register()` to hand the descriptor to the font module. All font face **management** (storing, matching, loading, caching) lives in `lib/font/font_face.c`.

---

## 6. Code Provenance

Every piece of the new module is sourced from existing code — **reorganization and consolidation**, not rewrite.

| New File | Primary Source | What Changed |
|----------|---------------|--------------|
| `font_context.c` | `radiant/ui_context.cpp` | Extracted `FT_Library` init, LCD filter, config into standalone context |
| `font_database.c` | `lib/font_config.c` (~2100 lines) | Removed global singleton; `FontContext` owns database; implemented disk cache |
| `font_loader.c` | `radiant/font.cpp` + `radiant/font_face.cpp` | Merged all face-loading paths; unified 4 duplicated `FT_Select_Size` blocks |
| `font_metrics.c` | `radiant/font.cpp` + `radiant/font_face.cpp` | Consolidated metric computation; always read OS/2, hhea, typo tables; cache per-face |
| `font_glyph.c` | `radiant/font.cpp` (`load_glyph`) | Same logic + added advance cache (was open TODO) |
| `font_cache.c` | Scattered across font.cpp, font_face.cpp | Centralized all caches with LRU eviction |
| `font_fallback.c` | `radiant/font.cpp` + `radiant/font_face.cpp` | Merged two divergent generic-family tables into one |
| `font_face.c` | `radiant/font_face.cpp` | Moved descriptor registry and matching; CSS parsing stays in Radiant |
| `font_platform.c` | `radiant/font_lookup_platform.c` + `lib/font_config.c` | Merged platform discovery |
| `font_decompress.cpp` | `radiant/font_face.cpp` | Extracted WOFF2 shim + added full WOFF1 decompressor |

---

## 7. Implementation Progress

### Phase Summary

| Phase | Description | Status | Date |
|-------|-------------|--------|------|
| **1** | Foundation — 12 source files in `lib/font/`, build config | ✅ Complete | 2026-02-14 |
| **2** | Radiant migration — wire `FontContext` into `UiContext`, rewrite `setup_font()` | ✅ Complete | 2026-02-15 |
| **3** | Caching & fallback — disk cache, bitmap cache, advance eviction, @font-face bridge | ✅ Complete | 2026-02-14 |
| **4** | Cleanup — slim headers, remove ~335 lines of unused stubs | ✅ Complete | 2026-02-16 |
| **5** | FT_Face migration — ~50 direct FreeType call sites → FontHandle API | ✅ Complete | 2026-02-14 |
| **6** | Radiant integration — decouple FreeType from `view.hpp`, migrate layout FT calls | ✅ Complete | 2026-02-15 |
| **7** | Phaseout `lib/font_config.c` — migrate all callers, exclude from build | ✅ Complete | 2026-02-15 |
| **8** | Render layer migration — FT_Face casts → FontHandle API, remove `FontBox.ft_face` | ✅ Complete | 2026-02-18 |
| **9** | Glyph abstraction — `LoadedGlyph`/`GlyphBitmap` replace `FT_GlyphSlot`/`FT_Bitmap` in all render + layout files | ✅ Complete | 2026-02-15 |

### Phase 1 — Foundation

Created all 12 source files under `lib/font/`:

| File | Lines | Key Contents |
|------|------:|-------------|
| `font.h` | 364 | Public API: `FontContext`, `FontHandle`, `FontMetrics`, `GlyphInfo`, `TextExtents`, all `font_*()` functions |
| `font_internal.h` | 359 | Internal structs, FT_* includes, `_internal` function declarations |
| `font_context.c` | 467 | `FT_Memory` pool routing, create/destroy, `font_handle_retain/release/wrap`, accessors |
| `font_database.c` | 1200 | TTF table parsing, directory scanning, metadata extraction, TTC support, disk cache save/load |
| `font_platform.c` | 180 | macOS/Linux/Windows directory discovery |
| `font_loader.c` | 309 | Unified `font_load_from_file/data_uri/memory`, `select_best_fixed_size()`, format pipeline |
| `font_decompress.cpp` | 280 | WOFF1 zlib decompression, WOFF2 via libwoff2, `font_decompress_if_needed()` |
| `font_metrics.c` | 357 | OS/2, hhea, typo metrics; x_height/cap_height; `font_calc_normal_line_height()` |
| `font_glyph.c` | 331 | Advance cache (4096 max), bitmap cache, `font_get_glyph/kerning/glyph_index` |
| `font_cache.c` | 260 | Face cache with LRU, `font_resolve()` 7-step pipeline |
| `font_fallback.c` | 215 | Generic CSS families, codepoint fallback with negative caching |
| `font_face.c` | 278 | `font_face_register/find/list/load/clear`, weight/slant scoring |

**Implementation notes:**
- Old `lib/font_config.c` coexisted during Phases 1–6; phased out in Phase 7.
- New database functions use `_internal` suffix to avoid link-time conflicts.
- FreeType linked as system library (`/opt/homebrew/opt/freetype`); vendoring deferred.
- `font_decompress.cpp` — only C++ file; `std::string` eliminated via `ComputeWOFF2FinalSize()` + arena pre-allocation.

### Phase 2 — Radiant Migration

**Approach:** Hybrid migration — both `FT_Face` and `FontHandle*` coexist in `FontProp`/`FontBox` for backward compatibility. `setup_font()` is the single migration point.

**Files modified:**
- `radiant/view.hpp` — forward declarations for `FontContext`/`FontHandle`; added `font_handle` to `FontProp`/`FontBox`; added `font_ctx` to `UiContext`
- `radiant/ui_context.cpp` — create `FontContext` with actual `pixel_ratio`; destroy in cleanup
- `radiant/font.cpp` — rewritten `setup_font()`: try @font-face → `font_resolve()` → legacy fallback
- `lib/font/font.h` — renamed `FontStyle` → `FontStyleDesc`, `FontFaceSrc` → `FontFaceSource` to avoid collisions

### Phase 3 — Caching & Fallback

Most Phase 3 features (LRU face cache, advance cache, fallback chains, @font-face registry) were already implemented in Phase 1. Phase 3 filled gaps:

- **Disk cache** — `font_database_save_cache_internal()` / `load_cache_internal()` with binary format (magic `0x4C464E54`, version 1, mtime/size validation)
- **Bitmap cache** — lazy hashmap init, clear-on-full eviction, wired into `font_render_glyph()`
- **Advance cache eviction** — `ADVANCE_CACHE_MAX_ENTRIES` (4096) per `FontHandle`
- **@font-face bridge** — `register_font_face()` converts CssEnum → FontWeight/FontSlant, calls `font_face_register()`
- **Header cleanup** — removed 7 unused types and ~30 unused declarations from `radiant/font_face.h`

### Phase 4 — Cleanup

- Removed `FT_SFNT_NAMES_H` from `view.hpp` (unused)
- Removed redundant FT includes from `font_face.h`
- Slimmed `font.h`: removed `FontfaceEntry` and internal declarations
- Removed ~335 lines of unused stubs from `font_face.cpp` (12 stub functions, 5 types)
- Removed unused `cache_character_width`/`get_cached_char_width` declarations
- FreeType header removal from `view.hpp` deferred to Phase 6
- `lib/font_config.c` phaseout deferred to Phase 7

### Phase 5 — FT_Face → FontHandle Migration

**Scope:** ~50 direct FreeType API call sites, ~30 `size->metrics` accesses, ~10 `family_name` accesses across 15+ files.

**New APIs added to `font.h`:**
- `font_handle_wrap()` — wraps borrowed `FT_Face` without ownership transfer
- `font_calc_normal_line_height()` — Chrome-compatible (CoreText → OS/2 → HHEA fallback)
- `font_get_cell_height()` — text rect height with Apple 15% hack
- `font_get_glyph_index()` — replaces `FT_Get_Char_Index`
- `font_get_x_height_ratio()` — x-height / em-size for CSS `ex` unit
- `font_handle_get_family_name()`, `font_handle_get_size_px()`, `font_handle_get_physical_size_px()`
- `hhea_line_height`, `underline_position`, `underline_thickness` in `FontMetrics`

**Key signature changes:**
- `load_glyph(UiContext*, FT_Face, ...)` → `load_glyph(UiContext*, FontHandle*, ...)`
- `calc_normal_line_height(FT_Face, ...)` → `calc_normal_line_height(FontHandle*)`
- `FT_UInt prev_glyph_index` → `uint32_t prev_glyph_index` in `layout.hpp`

**Borrowed FontHandle:** The legacy path wraps `FT_Face` via `font_handle_wrap()` with a `borrowed_face` flag that prevents `FT_Done_Face` on release, so `font_handle` is **always** populated after `setup_font()`.

**Critical bugs found and fixed (36 regressions → 0):**

1. **`FontMetrics.line_height` vs HHEA height confusion** — `ascender`/`descender` get overwritten by OS/2 typo values when `USE_TYPO_METRICS` is set, producing different heights. **Fix:** Added `hhea_line_height` field computed from HHEA values only.

2. **`FontProp` ascender/descender using typo values** — must always use HHEA values for backward compatibility. **Fix:** Populate from `m->hhea_ascender` / `m->hhea_descender`.

3. **`font_get_x_height_ratio()` wrong formula** — computed pixel-based ratio instead of font-unit ratio (`sxHeight / units_per_EM`). For Ahem font: 0.56 instead of correct 0.8, causing `font-size: 7.5ex` → 90px instead of 96px. Majority of the 36 failures. **Fix:** Read `sxHeight / units_per_EM` directly from `FT_Face`.

### Phase 6 — Radiant Integration (FreeType Decoupling)

**Type changes in `view.hpp`:**
- Removed `#include <ft2build.h>` and `#include FT_FREETYPE_H`
- Removed dead `FontProp.ft_face` field
- `FontBox.ft_face`: `FT_Face` → `void*` (fully removed in Phase 8); `UiContext.ft_library`: `FT_Library` → `void*`
- `load_styled_font()` / `load_glyph()` returns → `void*`

**New APIs:** `font_get_kerning_by_index()`, `FontMetrics.use_typo_metrics`

**Layout files fully migrated (no FT includes):**
- `layout_text.cpp` — `FT_Load_Char` → `font_get_glyph()`, `FT_Get_Kerning` → `font_get_kerning_by_index()`
- `intrinsic_sizing.cpp` — 6 sites migrated
- `event.cpp` — 1 site
- `layout.cpp` — `FT_Get_Sfnt_Table` → `FontMetrics.use_typo_metrics` (fully FT-free)

**Files with local FT includes** (still need direct FT access for rendering):
`font.cpp`, `font_face.h/cpp`, `ui_context.cpp`, `render.cpp`, `render_form.cpp`, `render_svg.cpp`, `render_pdf.cpp`, `event.cpp`, `intrinsic_sizing.cpp`, `pdf/cmd_view_pdf.cpp`

### Phase 7 — Phaseout `lib/font_config.c`

Removed all dependencies on `lib/font_config.c/h`, migrated callers, excluded from build.

**New APIs:** `font_family_exists()`, `font_find_path()`, `font_find_best_match()`, `font_slant_to_string()`

**Callers migrated:**
- `radiant/font.cpp` — `load_font_path(FontDatabase*)` → `load_font_path(FontContext*)`; uses `font_find_best_match()`
- `radiant/font_face.cpp` — removed system font fallback that bypassed `font_resolve()`
- `radiant/ui_context.cpp` — removed `font_database_get_global()` init/cleanup
- `radiant/resolve_css_style.cpp` — `font_database_find_all_matches()` → `font_family_exists()`
- `radiant/view.hpp` — removed `FontDatabase` forward declaration and `UiContext.font_db`

**Bugs found and fixed (14 regressions → 0):**

1. **Generic family aliases** — concrete font names (Arial, Times) mapped to generic lists, causing `font_resolve()` to bypass weight/slant matching. Fix: removed concrete aliases.

2. **Placeholder suffix stripping** — `"Arial Bold.ttf"` kept "Arial Bold" because only dash-separated suffixes were stripped. Fix: added space-separated suffix stripping.

3. **CamelCase splitting** — `"HelveticaNeue.ttc"` → "HelveticaNeue" didn't match "Helvetica Neue". Fix: added CamelCase → space-separated splitting.

4. **`load_font_with_descriptors()` bypass** — system font fallback called `load_styled_font()` directly, bypassing `font_resolve()`. Fix: removed; let `setup_font` handle system fonts.

5. **CSS enum weight collision** — `CSS_VALUE_NORMAL=307` fell in the 100–900 numeric range, treated as weight 307 instead of 400. Fix: explicit enum comparisons.

6. **Priority font parsing limit** — `priority_parsed < 20` cap prevented Times.ttc from being parsed. Fix: removed arbitrary limit.

### Phase 8 — Render Layer Migration

Migrated all `(FT_Face)` cast sites in the render layer to `FontHandle`-based APIs. Removed the `FontBox.ft_face` field entirely.

**Sites migrated per file:**

| File | Sites | What Changed |
|------|------:|--------------|
| `render.cpp` | 7 | Debug logging (`family_name` → `font_handle_get_family_name()`); missing-glyph box (`y_ppem` → `font_handle_get_physical_size_px()`, `height/64` → `font_get_metrics()->hhea_line_height * rdcon->scale`); ascender access (2 sites → `hhea_ascender * rdcon->scale`); underline thickness → `font_get_metrics()->underline_thickness`; Monaco debug log → `font_handle` |
| `render_form.cpp` | 1 | Ascender: `((FT_Face)fbox.ft_face)->size->metrics.ascender / 64.0f` → `font_get_metrics(fbox.font_handle)->hhea_ascender * rdcon->scale` |
| `render_pdf.cpp` | 4 | Two text-measurement loops (`FT_Get_Char_Index` + `FT_Load_Glyph` + `glyph->advance.x / 64.0f` → `font_measure_char()` per character); **FT includes fully removed** |
| `render_svg.cpp` | 1 | Removed `ctx.font.ft_face = NULL` initialization |
| `font.cpp` | — | Rewrote `setup_font()`: local `FT_Face face` variable replaces `fbox->ft_face`; wraps in `FontHandle` via `font_handle_wrap()` |
| `view.hpp` | — | **Removed `FontBox.ft_face` field** entirely; `FontBox` now has 3 fields: `FontProp *style`, `FontHandle* font_handle`, `int current_font_size` |

**Coordinate system insight:** `FontMetrics` values are in CSS pixels (divided by `pixel_ratio`). The render layer works in physical pixels. All metric accesses multiply by `rdcon->scale` to convert.

**Deferred:** `render_texnode.cpp` — has its own independent font pipeline (`get_face_for_font()` → `load_font_face()` → `FT_Face`) for TeX math rendering. Requires a separate migration.

**FT includes still retained** in `render.cpp` and `render_form.cpp` for `FT_GlyphSlot` and `FT_Bitmap` types (used by `load_glyph()` return value and `draw_glyph()` parameter). Abstracting these behind opaque types is future work.

### Phase 9 — Glyph Abstraction Layer

Abstracted `FT_GlyphSlot` and `FT_Bitmap` behind opaque types defined in `font.h`. No render or layout file touches FreeType types anymore.

**New types in `font.h`:**

```c
typedef enum GlyphPixelMode {
    GLYPH_PIXEL_GRAY,   // 8-bit grayscale
    GLYPH_PIXEL_MONO,   // 1-bit monochrome
    GLYPH_PIXEL_BGRA,   // 32-bit BGRA (color emoji)
    GLYPH_PIXEL_LCD,    // LCD sub-pixel
} GlyphPixelMode;

typedef struct LoadedGlyph {
    GlyphBitmap bitmap;   // buffer, width, height, pitch, bearing_x, bearing_y, pixel_mode
    float advance_x;      // horizontal advance (physical pixels, 26.6 already decoded)
    float advance_y;
} LoadedGlyph;
```

`GlyphBitmap` extended with `pixel_mode` field (replaces `FT_PIXEL_MODE_*` constants).

**`load_glyph()` signature change:**
`void* load_glyph(...)` → `LoadedGlyph* load_glyph(...)` — returns static `LoadedGlyph` filled from `FT_GlyphSlot` via `fill_loaded_glyph()`. Valid until next call (same lifetime semantics as raw `FT_GlyphSlot`).

**`draw_glyph()` / `draw_color_glyph()` signature change:**
`draw_glyph(RenderContext*, FT_Bitmap*, int, int)` → `draw_glyph(RenderContext*, GlyphBitmap*, int, int)` — uses `GlyphPixelMode` instead of `FT_PIXEL_MODE_*`.

**Files migrated (FT includes removed):**

| File | What Changed |
|------|--------------|
| `render.cpp` | `FT_GlyphSlot` → `LoadedGlyph*`; `glyph->advance.x / 64.0` → `glyph->advance_x`; `glyph->bitmap_left` → `glyph->bitmap.bearing_x`; `draw_glyph(FT_Bitmap*)` → `draw_glyph(GlyphBitmap*)`; `FT_PIXEL_MODE_BGRA/MONO` → `GLYPH_PIXEL_BGRA/MONO`; **FT includes removed** |
| `render_form.cpp` | Same pattern; **FT includes removed** |
| `render_svg.cpp` | `FT_GlyphSlot` → `LoadedGlyph*`; `advance.x / 64.0` → `advance_x` |
| `layout_text.cpp` | `FT_GlyphSlot` → `LoadedGlyph*`; `advance.x / 64.0f` → `advance_x`; dead `get_font_cell_height(FT_Face)` removed; **FT includes removed** |
| `intrinsic_sizing.cpp` | `FT_GlyphSlot` → `LoadedGlyph*`; `FT_UInt` → `uint32_t`; **FT includes removed** |
| `event.cpp` | `FT_GlyphSlot` → `LoadedGlyph*`; `advance.x / 64.0` → `advance_x`; **FT includes removed** |
| `font_glyph.c` | `GlyphBitmap` now populated with `pixel_mode` via FT→`GlyphPixelMode` mapping |

**Remaining FT includes** (infrastructure only — not layout/render):
`font.cpp`, `font_face.h/cpp`, `ui_context.cpp`, `render_texnode.cpp`, `pdf/fonts.cpp`, `pdf/cmd_view_pdf.cpp`, `window.cpp`

---

## 8. Before / After Summary

| Aspect                        | Before                                            | After                                          |
| ----------------------------- | ------------------------------------------------- | ---------------------------------------------- |
| **Files**                     | 7+ across `lib/` and `radiant/`                   | 12 in `lib/font/` + `woff2/` (5 bundled)       |
| **Font formats**              | TTF/OTF/TTC (WOFF1 broken, WOFF2 partial)         | TTF, OTF, TTC, WOFF1, WOFF2 — all first-class  |
| **Format pipeline**           | Scattered across 3 files                          | Single `font_loader.c` detect→decompress→load  |
| **FT_* exposure**             | `view.hpp`, `layout.hpp`, `font.h`, `font_face.h` | Only `font_internal.h`; layout + render fully FT-free via `LoadedGlyph`/`GlyphBitmap` |
| **FT_Library instances**      | 2 (layout + PDF)                                  | 1 shared via FontContext                       |
| **Glyph advance cache**       | ❌ (open TODO)                                     | ✅ per-face hashmap, 4096 entries               |
| **Glyph bitmap cache**        | ❌ re-rasterized each frame                        | ✅ LRU, 4096 entries                            |
| **Disk font cache**           | ❌ stubs                                           | ✅ binary format with mtime validation          |
| **Generic family tables**     | 2 divergent copies                                | 1 authoritative table                          |
| **`FT_Select_Size` handling** | 4 copy-pasted blocks                              | 1 `select_best_fixed_size()`                   |
| **Memory management**         | Mixed malloc/free/new/std::string                 | 100% pool/arena                                |
| **`std::string`**             | Used in WOFF2 path                                | Eliminated from font module code; only inside bundled libwoff2 internals |
| **libwoff2**                  | External `libwoff2dec.a` + `libwoff2common.a`     | Bundled decode-only sources in `woff2/`        |
| **`FontBox.ft_face`**         | Raw `FT_Face` exposed in `view.hpp`               | Field removed; `FontHandle*` only              |
| **API surface**               | ~25 functions + raw FT types                      | ~20 opaque functions                           |

---

## 9. Phase 10 — Load Glyph Consolidation

### 9.1 Overview
Consolidated Radiant's `load_glyph()` (in `font.cpp`, with fallback cache via `UiContext`) into the font module as `font_load_glyph()` in `lib/font/font_glyph.c`. Moved the fallback cache into `FontContext` so all callers benefit from unified fallback resolution without depending on `UiContext`.

### 9.2 API Change

**Before (Radiant-specific):**
```cpp
extern LoadedGlyph* load_glyph(UiContext* uicon, FontHandle* handle, FontProp* font_style, uint32_t codepoint, bool for_rendering);
```

**After (font module, C API):**
```c
LoadedGlyph* font_load_glyph(FontHandle* handle, const FontStyleDesc* style, uint32_t codepoint, bool for_rendering);
```

Key differences:
- **No `UiContext` dependency** — fallback resolution goes through `FontContext` (accessed via `handle->ctx`)
- **`FontStyleDesc*` instead of `FontProp*`** — uses font module's own style description for fallback matching
- **Uses `font_find_codepoint_fallback()`** — the font module's codepoint fallback cache (in `FontContext.codepoint_fallback_cache`) with proper ref-counted `FontHandle*` entries, replacing `UiContext.glyph_fallback_cache` which stored raw `FT_Face` pointers

### 9.3 Helper: `font_style_desc_from_prop()`
Added inline conversion helper in `view.hpp`:
```cpp
inline FontStyleDesc font_style_desc_from_prop(const FontProp* fp);
```
Maps `FontProp` (CssEnum-based weight/style) → `FontStyleDesc` (FontWeight/FontSlant enums) for callers that still work with Radiant's CSS property types.

### 9.4 What Was Removed
| Item | Location | Status |
|------|----------|--------|
| `GlyphFallbackEntry` struct | `font.cpp` | Removed — replaced by `CodepointFallbackEntry` in `font_fallback.c` |
| `glyph_fallback_hash()` / `glyph_fallback_compare()` | `font.cpp` | Removed |
| `s_loaded_glyph` (static) | `font.cpp` | Moved to `font_glyph.c` |
| `fill_loaded_glyph()` | `font.cpp` | Moved to `font_glyph.c` as `fill_loaded_glyph_from_slot()` |
| `load_glyph()` | `font.cpp` | Replaced by `font_load_glyph()` in `font_glyph.c` |
| `UiContext.glyph_fallback_cache` | `view.hpp` | Removed — fallback cache lives in `FontContext` |
| `extern load_glyph` | `view.hpp`, `cmd_view_pdf.cpp` | Removed |
| `fontface_cleanup()` glyph cache free | `font.cpp` | Removed — `FontContext` owns the cache |

### 9.5 Updated Callers (7 sites)
All callers now construct a `FontStyleDesc` via `font_style_desc_from_prop()` and call `font_load_glyph()`:
1. `render.cpp` — 2 sites (measurement scan + rendering)
2. `render_form.cpp` — 1 site (form text rendering)
3. `render_svg.cpp` — 1 site (SVG text measurement)
4. `layout_text.cpp` — 1 site (inline text layout)
5. `intrinsic_sizing.cpp` — 1 site (intrinsic width calculation)
6. `event.cpp` — 1 site (click-to-character offset)

### 9.6 Test Results
- Lambda: 224/224 ✅
- Radiant: 1972/1972 (1970 pass, 2 pre-existing failures)
- 0 new regressions

---

## 10. Future Work

### 10.1 TeX Math Font Migration
`render_texnode.cpp` has its own font pipeline (`get_face_for_font()` → `load_font_face()` → `FT_Face`). Migrate to `FontHandle`-based API.

### 10.2 HarfBuzz Text Shaping
Current system does character-by-character glyph loading with manual kerning. For proper international text (Arabic, Devanagari, Thai), HarfBuzz shaping is needed. The opaque FontHandle makes this a clean addition.

### 10.3 Variable Font Support
OpenType variable fonts (`fvar` table) allow continuous weight/width/slant ranges. FreeType already supports this via `FT_Set_Var_Design_Coordinates()`.

### 10.4 Font Subsetting for PDF
Embed only glyphs actually used in the document instead of entire font files. Natural fit for `font_loader.c`.

### 10.5 FreeType Vendoring
Bundle selected FreeType source files directly (skip BDF, PCF, PFR, Type1 drivers). Currently linked as system library.

### 10.6 Thread Safety
FreeType's `FT_Library` is not thread-safe across faces. Options: per-thread library, or mutex around face loading. The opaque `FontContext` makes either approach possible without API changes.
