# Design Proposal: Migrate Font & Text Pipeline Away from FreeType

**Status:** In Progress (Phase 5 complete)  
**Author:** Lambda Team  
**Date:** April 2026  
**Last Updated:** October 8, 2026

---

## 1. Motivation

### 1.1 Current Pain Points

FreeType was adopted as the standard font library for glyph loading and
rasterization. Over time, Radiant's requirements have diverged from what FreeType
provides:

**Mismatch with browser rendering.** Chrome/Safari use CoreText (macOS) and
DirectWrite (Windows) for glyph advances and line-height. We already work around
FreeType in 5 places:
- CoreText glyph advance overrides in `font_get_glyph()` and `font_load_glyph()`
- CoreText line-height with 15% hack in `font_calc_normal_line_height()`
- CoreText GPOS kerning fallback in `font_get_kerning()`
- CoreText codepoint fallback in `font_find_codepoint_fallback()`
- CoreText space_width override in `setup_font()`

Each workaround is a dual-path that loads data from FreeType, then conditionally
replaces it with platform data. This adds bug surface and maintenance burden.

**Redundant font file parsing.** `font_database.c` already parses raw TTF bytes for
`name`, `OS/2`, and table directories — 1,421 lines of custom code that duplicates
what FreeType does internally. Adding `cmap` and `hmtx` parsing to this existing
infrastructure is straightforward.

**Weak caching.** FreeType's internal caching is opaque and not tuned for our
workload (many small documents, batch processing, REPL). We already built 5 cache
layers on top. A unified pipeline would merge these into a cleaner hierarchy.

**Binary size and dependency chain.** FreeType (768KB static) depends on HarfBuzz,
Brotli, zlib, bzip2. Removing it simplifies the build and reduces binary size by
~1MB after transitive dependency elimination.

**Build complexity.** `premake5.mac.lua` references FreeType include/lib paths ~20
times across targets. Platform-specific install (Homebrew, MSYS2, apt) is a
recurring CI/developer friction point.

### 1.2 What FreeType Actually Provides Today

FreeType is a 140,000-line library. Our actual usage reduces to 20 distinct API
functions across ~79 production call sites, performing 4 core operations:

| Operation | FT APIs | Call Sites | Can Replace With | Status |
|-----------|---------|------------|------------------|--------|
| Parse font from file/memory | `FT_New_Memory_Face`, `FT_Done_Face` | 5 | Custom TTF table parser | FontTables added as parallel path |
| Set size + variable axes | `FT_Set_Pixel_Sizes`, `FT_Select_Size`, `FT_Get/Set_Var_*` | 7 | `head.unitsPerEm` scaling | In lib/font/ only |
| Get glyph metrics | `FT_Get_Char_Index`, `FT_Load_Glyph`, `FT_Get_Kerning`, `FT_Get_Sfnt_Table` | ~53 | `cmap` + `hmtx` + `kern` parsing | 3-tier cascade (FT primary → CT → FontTables) |
| Rasterize glyph bitmap | `FT_Load_Glyph(RENDER)` | 6 | Platform API or ThorVG | CoreText added on macOS |
| Library lifecycle | `FT_New_Library`, `FT_Done_Library`, `FT_Init_FreeType` | 5 | Removed from radiant/ | Remains in lib/font/ only |
| PDF embedded fonts | `FT_New_Memory_Face` (PDF streams) | 3 | FontTables (name, OS/2, head) | Replaced by FontTables in pdf/fonts.cpp |

FreeType has been **fully eliminated from all `radiant/` files** including
`radiant/pdf/fonts.cpp` (now uses FontTables for embedded font metadata).
The `lib/font/` module retains FreeType as the primary backend with CoreText
and FontTables as fallbacks (Phase 3 cascade architecture).

We use none of: autohinter, Type 1/CID/BDF/PCF/PFR support (except PDF embedded),
caching subsystem, stroker, glyph management, bitmap conversion, LCD filtering,
or module system beyond default initialization.

---

## 2. Goals

1. **Remove FreeType as a dependency** — no `FT_*` calls, no `ft2build.h` includes,
   no `libfreetype.a` linkage.
2. **Own the full pipeline** — font file parsing, metrics, glyph access, and
   rasterization under `lib/font/`.
3. **Platform-native rendering** — CoreText on macOS, DirectWrite on Windows, ThorVG
   on Linux/WASM. Match browser output per platform.
4. **Unified caching** — multi-level cache from font files to glyph bitmaps, tuned
   for our workloads.
5. **No regression** — all 3,939 baseline layout tests pass; visual rendering output
   is identical or improved.
6. **Incremental migration** — each phase is independently shippable and testable.

### Non-Goals

- OpenType shaping (GSUB/GPOS ligatures, Arabic/Devanagari reordering). This
  requires HarfBuzz, not FreeType. Our character-by-character layout handles
  Latin/CJK/emoji which covers current use cases.
- GPU text rendering or glyph texture atlases. Can be added later independently.
- CFF2 variable outline support. TrueType `glyf` covers system fonts on all
  platforms.

---

## 3. Architecture

### 3.1 New Module: `font_tables.c` — TTF/OTF Table Parser

A single new file (~1,500 lines) that reads OpenType tables from raw bytes. Builds
on the existing table-directory parsing in `font_database.c`.

```
font_tables.c
├── font_tables_open(data, length)     → FontTables*
├── font_tables_close(tables)
├── font_tables_get_cmap(tables)       → CmapTable*
├── font_tables_get_hmtx(tables)       → HmtxTable*
├── font_tables_get_head(tables)       → HeadTable*
├── font_tables_get_hhea(tables)       → HheaTable*
├── font_tables_get_maxp(tables)       → MaxpTable*
├── font_tables_get_os2(tables)        → Os2Table*
├── font_tables_get_kern(tables)       → KernTable*
├── font_tables_get_post(tables)       → PostTable*
├── font_tables_get_name(tables)       → NameTable*
├── font_tables_get_fvar(tables)       → FvarTable*   (variable fonts)
└── font_tables_get_glyf(tables)       → GlyfAccess*  (outline access, Phase 3)
```

Each table is parsed lazily on first access and cached on the `FontTables` struct.
Data stays in the original memory-mapped buffer where possible (zero-copy for
`hmtx` advance arrays, `cmap` format-12 groups, etc.).

### 3.2 Replacement Mapping

| FreeType API | Replaced By | Data Source |
|---|---|---|
| `FT_New_Face` / `FT_New_Memory_Face` | `font_tables_open()` | Raw TTF bytes (file read or WOFF decompressed) |
| `FT_Done_Face` | `font_tables_close()` | — |
| `FT_Set_Pixel_Sizes` | `scale = size_px / head.unitsPerEm` | `head` table |
| `FT_Select_Size` | Direct bitmap size selection | `EBLC`/`EBDT` tables |
| `FT_Get_MM_Var` / `FT_Set_Var_Design_Coordinates` | `font_tables_get_fvar()` + axis mutation | `fvar` table |
| `FT_Get_Char_Index` | `cmap_lookup(cmap, codepoint)` | `cmap` format 4/12 |
| `FT_Load_Glyph` (metrics only) | `hmtx_get_advance(hmtx, glyph_id) × scale` | `hmtx` table |
| `FT_Load_Glyph` (RENDER) | Platform rasterizer or ThorVG | See §3.3 |
| `FT_Get_Kerning` | `kern_get_pair(kern, left, right) × scale` | `kern` table format 0 |
| `FT_Get_Sfnt_Table(OS2)` | `font_tables_get_os2()` | `OS/2` table |
| `FT_Get_Postscript_Name` | `name_get_postscript(name_table)` | `name` table |
| `FT_Load_Sfnt_Table` | `font_tables_find(tag)` → raw bytes | Table directory |
| `FT_HAS_KERNING` | `font_tables_has(tables, 'kern')` | Table directory |
| `FT_New_Library` / `FT_Done_Library` | Removed | — |
| `FT_Add_Default_Modules` | Removed | — |
| `FT_Library_SetLcdFilter` | Removed | — |

### 3.3 Glyph Rasterization Strategy

Three platform backends behind a single internal API:

```c
// font_rasterize.h — internal API
typedef struct {
    uint8_t* buffer;       // grayscale or BGRA bitmap
    int      width;
    int      height;
    int      pitch;        // bytes per row
    int      bearing_x;    // left bearing in pixels
    int      bearing_y;    // top bearing in pixels
    int      pixel_mode;   // GRAY, BGRA, LCD
} RasterizedGlyph;

// Returns a rasterized glyph bitmap. Caller does NOT free buffer (owned by cache).
RasterizedGlyph* font_rasterize_glyph(FontHandle* handle, uint32_t glyph_id,
                                       float size_px, float pixel_ratio);
```

| Platform | Backend | Implementation |
|----------|---------|----------------|
| **macOS** | CoreText | `CTFontCreatePathForGlyph()` → `CGContextFillPath()` on a `CGBitmapContext`. Already have `CTFontRef` on every FontHandle. Produces grayscale or subpixel bitmap matching Safari. |
| **Windows** | DirectWrite | `IDWriteFontFace::GetGlyphRunOutline()` → `ID2D1RenderTarget::DrawGlyphRun()`. Matches Edge rendering. |
| **Linux / WASM** | ThorVG | Parse `glyf` table → extract TrueType quadratic bezier contours → convert to `tvg::Shape` paths → `tvg::SwCanvas` rasterization. Already a dependency for SVG rendering. |

For **color emoji** (`CBDT`/`COLR` tables):
- macOS/Windows: native APIs handle these automatically.
- Linux: parse `CBDT` (bitmap) directly — the bitmaps are pre-rasterized PNGs,
  just decompress and scale. `COLR` v0 is layered outlines, rendered via ThorVG with
  per-layer colors.

### 3.4 Updated FontHandle

```c
// After migration — no FT_Face
struct FontHandle {
    // Identity
    int ref_count;
    char* family_name;
    FontWeight weight;
    FontSlant slant;
    float size_px;
    float physical_size_px;       // size_px × pixel_ratio
    float scale;                  // size_px / unitsPerEm
    float bitmap_scale;           // for fixed-size bitmap fonts

    // Table access (lazy-parsed)
    FontTables* tables;           // owns the parsed table pointers
    uint8_t* file_buffer;         // mmap'd or arena-allocated font bytes
    size_t file_buffer_size;

    // Platform font handle (for advances + rasterization)
    void* platform_font;          // CTFontRef (macOS), IDWriteFontFace* (Win), NULL (Linux)

    // Cached metrics
    FontMetrics metrics;
    bool metrics_ready;

    // Caches (per-handle)
    struct hashmap* advance_cache;    // glyph_id → advance_x
    struct hashmap* kern_cache;       // (left, right) → kern delta

    // LRU tracking
    uint32_t lru_tick;
};
```

Key change: `FT_Face ft_face` is replaced by `FontTables* tables` (our own parsed
data) and `void* platform_font` (native handle for rasterization + advances).

### 3.5 Multi-Level Cache (Skia-Inspired)

```
┌──────────────────────────────────────────────────────┐
│ L0: Glyph Strike Cache                               │
│   Key: (font_id, glyph_id, size, flags)              │
│   Value: RasterizedGlyph (bitmap buffer + metrics)    │
│   Scope: FontContext-wide, shared across handles      │
│   Size: configurable (default 64MB)                   │
│   Eviction: LRU by access time                        │
│   Note: bitmap buffers in dedicated glyph_arena       │
├──────────────────────────────────────────────────────┤
│ L1: Font Instance Cache (existing face cache)         │
│   Key: "family:weight:slant:size"                     │
│   Value: FontHandle* (parsed tables + scaled metrics) │
│   Scope: FontContext-wide                             │
│   Size: 64 (LRU eviction)                            │
├──────────────────────────────────────────────────────┤
│ L2: Font File Cache                                   │
│   Key: file_path                                      │
│   Value: mmap'd buffer + FontTables* (shared tables)  │
│   Scope: FontContext-wide, shared across sizes         │
│   Size: 32 files                                      │
│   Note: Multiple FontHandles at different sizes share  │
│   the same file buffer and parsed table structures     │
├──────────────────────────────────────────────────────┤
│ L3: Font Database Disk Cache (existing)               │
│   Key: font_cache.bin                                 │
│   Value: family/weight/style metadata for all fonts   │
│   Scope: persistent across runs                       │
└──────────────────────────────────────────────────────┘
```

**Key improvement over current design:** L2 (font file cache) is new. Currently,
each FontHandle at a different size re-opens and re-parses the same font file. With
L2, a FontTables* for "System Font" is parsed once and shared by the 13px, 14px,
16px, and 24px handles.

### 3.6 Advance Width Strategy

| Platform | Source | Notes |
|----------|--------|-------|
| macOS | `CTFontGetAdvancesForGlyphs()` | Already used today; becomes the only path |
| Windows | `IDWriteFontFace::GetDesignGlyphMetrics()` | Design units → scale by `size_px / unitsPerEm` |
| Linux / WASM | `hmtx` table parsed by `font_tables.c` | `advance_width[glyph_id] × scale`. No hinting — matches current `FT_LOAD_NO_HINTING` behavior. |

On macOS and Windows, platform advances are used for **both layout and rendering**,
eliminating the current dual-path mismatch that caused the text overlap bug.

---

## 4. Migration Plan

### Phase 1: TTF Table Parser + Metrics Replacement — ✅ COMPLETE

**Commit:** 643ee4cf  
**Goal:** Replace all `FT_Get_Sfnt_Table`, `FT_Get_Char_Index`, metric-only
`FT_Load_Glyph`, and `FT_Get_Kerning` with custom table parsing. FreeType retained
only for rasterization.

**Deliverables:**
- New file: `lib/font/font_tables.c` (~1,500 lines)
  - Table directory parsing (refactored from `font_database.c`)
  - `cmap` format 4 (BMP) and format 12 (full Unicode) parser
  - `hmtx` advance width lookup
  - `head`, `hhea`, `maxp`, `OS/2` struct readers
  - `kern` format 0 pair lookup
  - `post` table reader (underline metrics)
  - `name` table reader (refactored from `font_database.c`)
  - `fvar` table reader (variable font axes)
- Modified: `font_loader.c` — read file into buffer, call `font_tables_open()`,
  create FontHandle. Changed `FT_New_Face` → `FT_New_Memory_Face` (shared buffer).
- Modified: `font_metrics.c` — read from `FontTables*` instead of `FT_Get_Sfnt_Table`
- Modified: `font_glyph.c` — use `cmap_lookup()` + `hmtx` instead of
  `FT_Get_Char_Index` + `FT_Load_Glyph` for measurement

**Actual outcome:** FontTables infrastructure created and wired into font_loader.
FreeType retained as primary metric source with FontTables as a tertiary fallback
(after CoreText). All 3,939 baseline layout tests pass.

**FT calls eliminated:** ~45 of 64 (all except rasterization)  
**Risk:** Low. Each replacement is testable per-function. Advances can be validated
against FreeType output before switching.  
**Validation:** All 3,939 baseline layout tests pass. ✅

### Phase 2: Platform Rasterization — ✅ COMPLETE (macOS)

**Goal:** Replace `FT_Load_Glyph(FT_LOAD_RENDER)` with platform-native or ThorVG
rasterization. Remove FreeType entirely.

**Deliverables (completed):**
- New file: `lib/font/font_rasterize_ct.c` — macOS CoreText rasterization backend.
  Uses `CTFontCreatePathForGlyph()` → `CGContextFillPath()` on `CGBitmapContext`.
  Handles grayscale and color emoji (BGRA) bitmaps.
- Modified: `font_glyph.c` — `font_load_glyph(for_rendering=true)` uses CoreText
  rasterizer on macOS when available, FreeType as fallback.
- Modified: `font_internal.h` — added `ct_raster_ref` (CoreText metrics) to FontHandle.
- Modified: `font_loader.c` — creates platform font references during face loading.
- Fixed: rpmalloc `PAGE_HUGE` crash from large arena allocations.

**Deliverables (deferred to future work):**
- `lib/font/font_rasterize_dw.c` — Windows DirectWrite backend (not yet needed)
- `lib/font/font_rasterize_tvg.cpp` — ThorVG backend for Linux/WASM (not yet needed)
- `glyf` table parser for TrueType outline extraction (not yet needed)
- Build system FreeType removal (deferred — FreeType still used as fallback)

**Actual outcome:** CoreText rasterization works on macOS. FreeType retained as
fallback for non-macOS platforms and for face loading (`FT_New_Memory_Face`).
All 3,939 baseline layout tests pass.

**FT calls eliminated:** ~6 rasterization calls replaced with CT on macOS  
**Risk:** Medium. Rasterization is visually sensitive.  
**Validation:** All 3,939 baseline layout tests pass. ✅

### Phase 3: Fast-Path FreeType Elimination — ✅ COMPLETE

**Goal (revised):** Eliminate FreeType from the per-glyph fast path in
`font_glyph.c` and `font_metrics.c`. Establish a 3-tier cascade:
FreeType (primary, when `ft_face` is available) → CoreText (secondary) →
FontTables (tertiary, pure table-based fallback).

**Deliverables:**
- Modified: `font_glyph.c` — 3-tier metric cascade for `font_get_glyph()`,
  `font_load_glyph()`, `font_get_kerning()`, `font_has_codepoint()`.
  FreeType remains primary when `ft_face` is non-NULL; CT and FontTables
  are fallbacks for when FreeType is eventually removed.
- Modified: `font_metrics.c` — same cascade for OS/2 table access, x-height,
  cap-height, space width, `font_calc_normal_line_height()`.

**Key lesson learned:** Do NOT change the primary metric computation path.
Replacing FreeType with CoreText as primary caused 30 regressions. The correct
approach: keep FreeType as primary when `ft_face` is available, add CT/FontTables
as fallbacks that activate when `ft_face` is NULL.

All 3,939 baseline layout tests pass. ✅

**Note:** The original Phase 3 goal (L2 font file cache, L0 glyph strike cache)
is deferred to a future optimization pass.

### Phase 4: Cleanup + PDF Module — ✅ COMPLETE

**Goal:** Migrate the remaining FreeType usage in `radiant/pdf/` and
`radiant/ui_context.cpp`.

**Deliverables:**
- Modified: `radiant/ui_context.cpp` — removed duplicate `FT_Library` lifecycle
  (5 FT calls eliminated: `FT_Init_FreeType`, `FT_Library_SetLcdFilter` ×2,
  `configure_freetype_subpixel`, `FT_Done_FreeType`). Removed FT includes.
  FontContext already owns the FT_Library with proper LCD filter setup.
- Modified: `radiant/window.cpp` — removed unused `ft2build.h` / `FT_FREETYPE_H`
  includes.
- Modified: `radiant/view.hpp` — removed `void* ft_library` field from `UiContext`
  struct (no longer needed).
- Modified: `radiant/pdf/cmd_view_pdf.cpp` — replaced raw `FT_Load_Char` +
  `FT_GlyphSlot` bitmap access with `font_load_glyph()` API. Removed FT includes.
- Modified: `radiant/pdf/fonts.cpp` — eliminated `g_ft_library` global (was the
  3rd duplicate FT_Library). Each `PDFFontCache` now owns its own FT_Library.
  Replaced `FT_Set_Char_Size`/`FT_Get_Char_Index`/`FT_Load_Glyph` glyph width
  fallback with `font_tables` (cmap + hmtx lookup). Added `font_tables_open()`
  for embedded PDF fonts. Added `pdf_font_cache_destroy()` to fix resource leak.
- Modified: `radiant/pdf/pdf_fonts.h` — added `FontTables* tables` field to
  `PDFFontEntry`. Added `pdf_font_cache_destroy()` declaration.
- Modified: `radiant/pdf/pdf_to_view.cpp` — added `pdf_font_cache_destroy()` call.

**FT calls eliminated:** 13 from radiant/ (all radiant/ production FT calls removed
except `FT_New_Memory_Face` / `FT_Done_Face` / `FT_Init_FreeType` /
`FT_Done_FreeType` in `pdf/fonts.cpp` for embedded font loading)  
**Risk:** Low. PDF module has its own test suite.  
**Validation:** 3,939/3,939 layout baseline, 557/557 Lambda runtime tests. ✅

### Phase 5: CT Advance Override + Rasterization Quality — ✅ COMPLETE

**Goal:** Improve text rendering fidelity by (1) enabling CoreText advance width
overrides for all fonts, (2) fixing CoreGraphics font smoothing that caused
excessively bold glyph rasterization, and (3) fixing round-bottom glyph baseline
alignment to match Chrome/Skia.

**Change 1: CoreText advance override for all fonts**

- Modified: `lib/font/font_loader.c` — removed the `!FT_HAS_KERNING(face)` and
  `!has_any_kern_table` guards around `ct_font_ref` creation. Previously,
  `ct_font_ref` (which overrides FreeType glyph advances with CoreText advances to
  match browser rendering) was only created for fonts WITHOUT a kern table. Fonts
  like Arial that have a kern table used FreeType advances, which diverge slightly
  from Chrome/Skia.
- Now `ct_font_ref` is created unconditionally for all fonts. Kerning is unaffected:
  `font_get_kerning()` checks the FontTables kern table first, so fonts with kern
  tables still get proper kerning regardless of `ct_font_ref`.
- **Finding:** For Arial, CT and FT advances are nearly identical (e.g., 't': 7.992
  vs 8.000). The ~7px total width difference on "Direction" vs Chrome is inherent to
  CoreText's character-by-character advances vs Skia/HarfBuzz shaping — not fixable
  by switching between FT and CT on macOS.

**Impact:** 15 layout test pages improved (e.g., error-page +62%, npr +32.9%,
blog-homepage +22%), no individual regressions.

**Change 2: Gamma-corrected CoreGraphics font smoothing**

**Superseded 2026-10-08:** the implementation below applied an LCD-mask gamma
conversion to a DeviceGray bitmap. A later italic-only smoothing exclusion made
math variables especially faint. The grayscale backend now renders native
smoothed black text on white, then inverts luminance into mask coverage without
an extra gamma transform. This preserves CoreText's grayscale tonal response
for both upright and italic faces. Direct Chrome snapshots using the same
Times-Italic face at 96px and 16px validate the change; the native regression
`FontMetricTest.RasterPreservesNativeGrayscaleCoverage` compares against RGB
CoreText text for Times and bundled CMU faces at 8/16/96px and 1×/2× density.
The separate font-resource omission is fixed as
[RAD07-L2](Radiant_Issue_Ledger.md#rad07-l2): document packages now declare the
bundled CMU and KaTeX faces through a shared Lambda-home stylesheet helper
(D7.2.4), so default LaTeX math uses those faces rather than system fallback.
The historical measurements below cover the earlier implementation only.

- Modified: `lib/font/font_rasterize_ct.c` — kept font smoothing enabled
  (`CGContextSetShouldSmoothFonts(true)`) for stroke thickening, then added a
  gamma² linearization post-process on the resulting grayscale bitmap.
- **Root cause:** CoreGraphics font smoothing adds stroke thickening that matches
  browser rendering weight. However, CG also applies a gamma ~2.0 encoding to
  coverage values in offscreen grayscale contexts, making raw output appear
  excessively bold (+19.2% dark intensity vs Chrome).
- **Solution — Skia-style gamma² linearization:** After `CTFontDrawGlyphs`, apply
  `new_pixel = (pixel * pixel + 128) / 255` to each grayscale byte. This is the
  same formula Chrome/Skia uses in `gLinearCoverageFromCGLCDValue`
  (`SkScalerContext_mac_ct.cpp`, line ~377). It undoes CG's gamma encoding while
  preserving the thickened stroke geometry.
- **Quantitative results** (glyph_quality test, multi-size text vs Chrome reference):
  - Smoothing ON (raw):  dark intensity **+19.2%** vs Chrome — too heavy/bold
  - Smoothing OFF:       dark intensity **−9.3%** vs Chrome — too light
  - Smoothing ON + γ²:   dark intensity **+2.0%** vs Chrome — near-perfect match
  - h2_direction render mismatch: 1.77% (OFF) → **1.21%** (ON + γ²)
- The remaining ~2% gap is inherent to CoreText vs Skia/HarfBuzz differences in
  text shaping, not glyph weight.

**Impact:** Glyph rasterization quality closely matches Chrome/Safari. Stroke weight
within 2% of browser rendering across all tested font sizes (11px, 14px, 24px).

**Change 3: Integer pixel positioning for glyph bitmaps (roundOut + outset)**

- Modified: `lib/font/font_rasterize_ct.c` — replaced the bitmap dimension and
  glyph drawing position calculations with Skia-style `roundOut + outset(1,1)`
  integer pixel positioning.
- **Root cause:** Glyphs with round bottoms (e, c, o) have `bbox.origin.y` slightly
  negative (baseline overshoot, e.g., −0.2812 for 'e' in Arial Bold 24px). The old
  code computed the CG drawing position as `-(bbox.origin.y) + 1.0/scale`, producing
  fractional pixel coordinates (e.g., y=1.2812). This caused sub-pixel anti-aliasing
  to bleed into an extra bottom pixel row, making round-bottom glyphs appear 1px
  lower than flat-bottom glyphs compared to Chrome.
- **Solution — Skia-style roundOut + outset(1,1):** Compute the pixel-space bounding
  box with integer boundaries (`floor` for left/bottom, `ceil` for right/top), then
  extend each edge by 1 pixel for AA bleed margins. The glyph is drawn at integer CG
  pixel coordinates `(-px_left, -px_bottom)`, eliminating fractional baseline
  positioning. This matches Skia/Chrome's approach:
  `skBounds.roundOut(&mx.bounds); mx.bounds.outset(1, 1);`
- **Before/after per-glyph bottom pixel row** (Arial Bold 24px, "Direction"):
  - 'D' (flat): Chrome=31, Lambda before=31, after=31 — unchanged ✓
  - 'i' (flat): Chrome=31, Lambda before=31, after=31 — unchanged ✓
  - 'r' (round): Chrome=32, Lambda before=**33**, after=**32** — fixed ✓
  - 'e' (round): Chrome=32, Lambda before=**33**, after=**32** — fixed ✓
- The `bearing_x` and `bearing_y` values are numerically identical to the old
  formulas (`floor(origin.x * s) - 1` and `ceil((origin.y + height) * s) + 1`
  respectively). Only the internal CG drawing position and bitmap dimensions change.

**Impact:** h2_direction render test: 1.21% → **0.10%** (fixed, was failing).
list_markers_01: 13.01% → **11.75%** (fixed, was failing). glyph_quality improved
from 9.77% → 6.54%.

**Deliverables:**
- Modified: `lib/font/font_loader.c` — unconditional `ct_font_ref` creation
- Modified: `lib/font/font_rasterize_ct.c` — font smoothing enabled + gamma²
  linearization post-process (`(v*v+128)/255`) + integer pixel positioning via
  roundOut + outset(1,1) for glyph bitmap bounds and CG drawing position

**Risk:** Low. All three changes improve browser fidelity with no layout regressions.  
**Validation:** +2 render tests fixed (h2_direction, list_markers_01), 0 regressions.
562/567 Lambda runtime tests pass (5 pre-existing JS transpiler failures,
unrelated). ✅

---

### Remaining FreeType Usage After Phase 5

FreeType has been eliminated from all **radiant/** files except `radiant/pdf/fonts.cpp`
(embedded font loading — requires `FT_New_Memory_Face` for CFF/Type1/TrueType from
PDF streams). The **lib/font/** module retains FreeType as the primary backend with
CoreText and FontTables as fallbacks.

| Location | Production FT calls | Purpose |
|----------|--------------------|---------|
| `lib/font/font_context.c` | 8 | FT_Library lifecycle, custom FT_Memory, FT_Done_Face, x-height via FT |
| `lib/font/font_loader.c` | ~20 | FT_New_Memory_Face, FT_Set_Pixel_Sizes, FT_Select_Size, variable font axes (FT_Get_MM_Var etc.) |
| `lib/font/font_glyph.c` | ~25 | FT_Get_Char_Index, FT_Load_Glyph (measure + render), FT_Get_Kerning |
| `lib/font/font_metrics.c` | ~15 | FT_Get_Sfnt_Table(OS2), FT_Load_Char (space width), FT_HAS_KERNING |
| `lib/font/font_face.c` | 3 | FT_Get_Char_Index (Latin check) |
| `radiant/pdf/fonts.cpp` | ~8 | FT_Init_FreeType, FT_New_Memory_Face, FT_Done_Face, FT_Done_FreeType |
| **Total** | **~79** | |

**Next phase** to fully remove FreeType: eliminate the ~71 `lib/font/` calls by
making CoreText (macOS) / DirectWrite (Windows) the primary metric+rasterization
path, with FontTables as the cross-platform fallback. The Phase 3 cascade
architecture already supports this — set `ft_face = NULL` to activate fallbacks.

---

## 5. TTF Table Parsing — Technical Details

### 5.1 cmap (Character to Glyph Mapping)

Two formats cover >99% of fonts:

**Format 4 (BMP only, U+0000–U+FFFF):**
- Segment array with `startCode[]`, `endCode[]`, `idDelta[]`, `idRangeOffset[]`
- Binary search on segments, then: if `idRangeOffset == 0`, `glyph_id = cp + delta`;
  else index into `glyphIdArray`
- ~150 lines

**Format 12 (Full Unicode):**
- Sequential groups: `(startCharCode, endCharCode, startGlyphID)`
- Binary search on groups, then: `glyph_id = startGlyphID + (cp - startCharCode)`
- ~50 lines

Lookup function:
```c
uint16_t cmap_lookup(CmapTable* cmap, uint32_t codepoint);
// Returns 0 if glyph not found (missing glyph)
```

### 5.2 hmtx (Horizontal Metrics)

Linear array of `(advanceWidth, leftSideBearing)` pairs:
- First `hhea.numberOfHMetrics` entries have both fields (uint16 + int16)
- Remaining glyphs reuse the last advanceWidth

```c
float hmtx_get_advance(HmtxTable* hmtx, uint16_t glyph_id, float scale);
// Returns advance_width × scale (in CSS pixels)
```

~30 lines of code.

### 5.3 kern (Legacy Kerning)

Format 0 (pair list):
- Sorted array of `(left, right, value)` tuples
- Binary search for a specific pair

```c
float kern_get_pair(KernTable* kern, uint16_t left, uint16_t right, float scale);
// Returns kerning value × scale (negative = tighter)
```

~60 lines. Note: GPOS kerning (used by modern fonts) is already handled via
CoreText on macOS. On Linux, format 0 `kern` covers the common case.

### 5.4 head / hhea / maxp / OS/2

Fixed-size struct reads. Total ~100 lines for all four:

```c
typedef struct {
    uint16_t units_per_em;
    int16_t  x_min, y_min, x_max, y_max;
    uint16_t mac_style;        // bit 0 = bold, bit 1 = italic
    int16_t  index_to_loc_format;  // 0=short, 1=long (for glyf)
} HeadTable;

typedef struct {
    int16_t  ascender, descender, line_gap;
    uint16_t advance_width_max;
    uint16_t number_of_h_metrics;
} HheaTable;

typedef struct {
    // Full OS/2 table including sTypoAscender, usWinAscent, fsSelection, etc.
    // Same fields currently extracted via FT_Get_Sfnt_Table(FT_SFNT_OS2)
} Os2Table;
```

### 5.5 glyf (TrueType Outlines, Phase 2 Linux/WASM only)

Needed for ThorVG rasterization. Two glyph types:

**Simple glyphs:** contour endpoints + flag array + coordinate deltas (quadratic
bezier on-curve/off-curve points)

**Compound glyphs:** list of component references with transforms (translate, scale,
rotate)

Parser: ~500 lines. Produces a list of contours, each a sequence of on/off-curve
points. The ThorVG adapter converts quadratic→cubic beziers (standard formula:
`CP1 = P0 + 2/3(P1-P0)`, `CP2 = P2 + 2/3(P1-P2)`).

### 5.6 fvar (Variable Font Axes)

Already partially parsed (we call `FT_Get_MM_Var` then modify coordinates). The
`fvar` table is:
- Axis count + axis records: `(tag, minValue, defaultValue, maxValue, nameID)`
- Instance records (named presets)

```c
bool fvar_set_axes(FvarTable* fvar, float size_px, int css_weight,
                   float* coordinates /* out, design-space values */);
```

~100 lines. Note: variable font axis *application* (interpolating the actual glyph
data) is more complex — but on macOS/Windows the platform API handles this
when we pass the coordinates to the platform font constructor. On Linux with ThorVG,
we'd initially only support the default instance and require explicit weight files
(e.g., separate Bold.ttf). Full variable font interpolation can be added later.

---

## 6. Glyph Rasterization — Technical Details

### 6.1 macOS: CoreText Backend

```c
// font_rasterize_ct.c
RasterizedGlyph* rasterize_ct(CTFontRef ct_font, CGGlyph glyph_id,
                               float size_px, float pixel_ratio) {
    // 1. Get glyph bounding box
    CGRect bbox;
    CTFontGetBoundingRectsForGlyphs(ct_font, kCTFontOrientationHorizontal,
                                     &glyph_id, &bbox, 1);

    // 2. Create bitmap context at physical resolution
    int width  = ceil(bbox.size.width * pixel_ratio) + 2;  // +2 for antialiasing bleed
    int height = ceil(bbox.size.height * pixel_ratio) + 2;
    CGContextRef ctx = CGBitmapContextCreate(buffer, width, height, 8, pitch,
                                             colorSpace, kCGImageAlphaOnly);

    // 3. Position and draw
    CGContextSetFont(ctx, ...);
    CGContextSetFontSize(ctx, size_px * pixel_ratio);
    CGPoint position = { -bbox.origin.x * pixel_ratio, -bbox.origin.y * pixel_ratio };
    CTFontDrawGlyphs(ct_font, &glyph_id, &position, 1, ctx);

    // 4. Extract 8-bit grayscale bitmap from context
    return fill_rasterized_glyph(buffer, width, height, pitch, bearing_x, bearing_y);
}
```

~200 lines including error handling and color emoji support (use
`kCGImageAlphaPremultipliedFirst` for BGRA).

### 6.2 Windows: DirectWrite Backend

```c
// font_rasterize_dw.c (C++ with COM)
RasterizedGlyph* rasterize_dw(IDWriteFontFace* face, uint16_t glyph_id,
                                float size_px, float pixel_ratio) {
    DWRITE_GLYPH_RUN run = { face, size_px * pixel_ratio, 1, &glyph_id, &advance, NULL, FALSE, 0 };
    // Render to ID2D1BitmapRenderTarget or use
    // IDWriteGlyphRunAnalysis::CreateAlphaTexture for grayscale bitmap
}
```

~200 lines. DirectWrite handles variable fonts, color emoji, and subpixel rendering
natively.

### 6.3 Linux/WASM: ThorVG Backend

```c
// font_rasterize_tvg.cpp
RasterizedGlyph* rasterize_tvg(FontTables* tables, uint16_t glyph_id,
                                float size_px, float pixel_ratio) {
    // 1. Extract outline from glyf table
    GlyphOutline outline;
    glyf_get_outline(tables, glyph_id, &outline);

    // 2. Convert to ThorVG path
    auto shape = tvg::Shape::gen();
    for each contour in outline:
        shape->moveTo(first_point);
        for each segment:
            if on-curve: shape->lineTo(p);
            if off-curve: // quadratic → cubic conversion
                shape->cubicTo(cp1, cp2, end);
        shape->close();

    // 3. Apply scaling: design units → pixels
    float scale = (size_px * pixel_ratio) / tables->head.units_per_em;
    shape->scale(scale);
    shape->translate(-bbox.x, -bbox.y);

    // 4. Rasterize via ThorVG software canvas
    auto canvas = tvg::SwCanvas::gen();
    canvas->target(buffer, width, width, height, tvg::SwCanvas::ABGR8888);
    canvas->push(std::move(shape));
    canvas->draw();
    canvas->sync();

    // 5. Extract grayscale from alpha channel
    return fill_rasterized_glyph(buffer, width, height, ...);
}
```

~400 lines plus ~500 lines for `glyf` outline extraction.

---

## 7. Risk Analysis

### 7.1 Low Risk

| Area | Reason |
|------|--------|
| cmap parsing | Well-documented formats, only 2 formats needed, existing TTF parsing infrastructure |
| hmtx advances | Trivial linear array indexed by glyph ID |
| head/hhea/maxp/OS/2 | Fixed-size struct reads, same fields already extracted via FreeType |
| kern table | Simple sorted pair list, binary search |
| macOS rasterization | CTFont already created on every FontHandle; just need to draw glyphs |
| Windows rasterization | DirectWrite is well-documented, widely used by other engines |
| Cache redesign | Optimization only, no correctness impact |

### 7.2 Medium Risk

| Area | Risk | Mitigation |
|------|------|------------|
| glyf outline parsing | Compound glyphs have recursive component references with transforms | Limit compound recursion depth (32). Test with emoji fonts (heavily compound). |
| ThorVG rasterization quality | Quadratic→cubic conversion introduces small errors; no subpixel AA | Compare against FreeType reference renders. Accept grayscale-only on Linux. |
| Variable fonts on Linux | axis interpolation changes glyph outlines | Use default instance initially. Add `gvar` interpolation later if needed. |
| Color emoji on Linux | `CBDT` bitmaps need PNG decompression; `COLR` v1 has gradients | `CBDT` bitmaps are straightforward (PNG → pixel buffer). Skip `COLR` v1 initially. |
| CFF/CFF2 outlines | PostScript charstring interpreter is complex (~800 lines) | Skip initially. CFF fonts are rare for system fonts. Fall back to platform API where possible. |

### 7.3 High Risk

| Area | Risk | Mitigation |
|------|------|------------|
| **None identified** | The migration is incremental — each phase can be reverted independently. FreeType can be retained as a fallback during transition. | |

### 7.4 What We Explicitly Do NOT Need

| Capability | Why Not Needed |
|------------|----------------|
| Hinting / autohinting | Already using `FT_LOAD_NO_HINTING` for layout. Retina (2×) makes hinting irrelevant for rendering. |
| Type 1 / CID / BDF / PCF | Legacy formats not used by any web content or system font. |
| LCD subpixel rendering | macOS uses system compositor for subpixel; Windows uses ClearType via DirectWrite; Linux is grayscale (matches FreeType current config). |
| Stroker / emboldener | Not used anywhere in codebase. |
| OpenType shaping (GSUB/GPOS) | Requires HarfBuzz, not FreeType. Character-by-character layout covers Latin/CJK. |

---

## 8. Effort Estimate

| Phase | New Code | Modified Code | Effort | Status |
|-------|----------|---------------|--------|--------|
| **Phase 1:** Table parser + metrics | ~1,500 lines (`font_tables.c/.h`) | ~500 lines across `font_loader.c`, `font_metrics.c`, `font_glyph.c` | Medium | ✅ Done |
| **Phase 2:** Platform rasterization (macOS) | ~350 lines (`font_rasterize_ct.c`) | ~300 lines across `font_glyph.c`, `font_internal.h`, `font_loader.c` | Medium | ✅ Done |
| **Phase 3:** Fast-path FT elimination | 0 new files | ~400 lines across `font_glyph.c`, `font_metrics.c` (3-tier cascade) | Small | ✅ Done |
| **Phase 4:** PDF + radiant cleanup | 0 new files | ~150 lines across `radiant/pdf/`, `radiant/ui_context.cpp`, `radiant/view.hpp` | Small | ✅ Done |
| **Phase 5:** CT advance override + rasterization quality + baseline alignment | 0 new files | ~30 lines across `font_loader.c`, `font_rasterize_ct.c` | Small | ✅ Done |
| **Total completed** | ~1,850 new lines | ~1,360 modified lines | | |

**Deferred work:**
| Item | Est. Code | Notes |
|------|-----------|-------|
| Windows DirectWrite rasterizer | ~200 lines | `font_rasterize_dw.c` — not needed until Windows focus |
| Linux/WASM ThorVG rasterizer | ~900 lines | `font_rasterize_tvg.cpp` + `font_glyf.c` — not needed until Linux focus |
| Full FreeType removal from `lib/font/` | ~500 lines modified | Make CT/DW primary, FontTables fallback, remove FT_New_Memory_Face |
| L2 font file cache | ~300 lines | Original Phase 3 goal — optimization |
| Build system FreeType removal | ~100 lines | `build_lambda_config.json`, `premake5.mac.lua`, setup scripts |

---

## 9. Validation Strategy

### Per-Phase Testing

| Phase | Test Method | Result |
|-------|------------|--------|
| Phase 1 | For each replaced function, run both old (FreeType) and new (custom) paths, compare outputs. Advance widths must match within 0.01px. `cmap_lookup` output must exactly match `FT_Get_Char_Index`. All 3,939 baseline tests pass. | ✅ 3,939/3,939 |
| Phase 2 | CoreText rasterization on macOS. Visual comparison of rendered output. | ✅ 3,939/3,939 |
| Phase 3 | 3-tier cascade with FreeType as primary. All baselines pass with no regressions. | ✅ 3,939/3,939 |
| Phase 4 | PDF module uses font_load_glyph + font_tables. Lambda runtime tests pass. | ✅ 3,939/3,939 layout + 557/557 runtime |
| Phase 5 | CT advance override: 15 layout pages improved. Gamma² linearization: +2.0% vs Chrome. Integer pixel positioning: +2 render tests fixed (h2_direction 1.21%→0.10%, list_markers_01 13.01%→11.75%), 0 regressions. | ✅ 17 improvements, 0 regressions + 562/567 runtime |

### Regression Gate

Every commit during migration must pass:
- `make test-radiant-baseline` (3,939 tests)
- `make layout suite=markdown` (current 7+ passing)
- `make build-test && make test` (full test suite)

### Fallback Plan

During Phases 1–2, FreeType can be retained behind a compile flag
(`#ifdef USE_FREETYPE`) as a fallback. The flag is removed in Phase 4 once
all platforms are verified.

---

## 10. File Inventory After Migration

### New Files

| File | Lines (est.) | Purpose |
|------|-------------|---------|
| `lib/font/font_tables.c` | 1,500 | TTF/OTF table parser (cmap, hmtx, head, hhea, maxp, OS/2, kern, post, name, fvar) |
| `lib/font/font_tables.h` | 200 | Public API for table access |
| `lib/font/font_rasterize.c` | 400 | Rasterization dispatcher + common utilities |
| `lib/font/font_rasterize_ct.c` | 200 | macOS CoreText rasterizer |
| `lib/font/font_rasterize_dw.c` | 200 | Windows DirectWrite rasterizer |
| `lib/font/font_rasterize_tvg.cpp` | 400 | Linux/WASM ThorVG rasterizer |
| `lib/font/font_glyf.c` | 500 | glyf outline parser (TrueType, for ThorVG) |

### Modified Files

| File | Change |
|------|--------|
| `lib/font/font_internal.h` | Remove FT includes, replace FT_Face with FontTables* |
| `lib/font/font_loader.c` | Replace FT_New_Face with font_tables_open() |
| `lib/font/font_metrics.c` | Read from FontTables* instead of FT_Get_Sfnt_Table |
| `lib/font/font_glyph.c` | Use cmap_lookup + hmtx instead of FT calls |
| `lib/font/font_context.c` | Remove FT_Library lifecycle |
| `lib/font/font_cache.c` | Add L2 font file cache |
| `lib/font/font_database.c` | Refactor TTF parsing to share with font_tables.c |
| `radiant/ui_context.cpp` | Remove legacy FT_Library |
| `radiant/pdf/fonts.cpp` | Use font_tables + font_rasterize |
| `build_lambda_config.json` | Remove FreeType dependency |

### Removed Dependencies

| Dependency | Size | Notes |
|------------|------|-------|
| FreeType | 768KB (static lib) | Primary target |
| HarfBuzz (FreeType dep) | ~500KB | Only needed if FreeType built with HB |
| Brotli (common+dec) | ~200KB | Retained — needed for WOFF2 decompression |
| zlib | ~100KB | Retained — used elsewhere |
| bzip2 | ~80KB | Can be removed if no other users |

**Net binary size reduction:** ~1.2MB (FreeType + HarfBuzz + bzip2).

## 11. Text overflow paint contract (2026-10-11)

This focused delivery extends the existing text pipeline; the migration estimates
above retain their historical scope. Ownership follows **D4.5.1v4 / D4.1.4v5**
and shared rendering boundaries follow **D7.3.3–D7.3.4**. No formal ruling changes.

[CSS Overflow 3 §5.1](https://www.w3.org/TR/css-overflow-3/#text-overflow)
requires ellipsis to affect painting while preserving layout and pointer targets.
The marker uses the block's style, fits after the remaining visible content, and
elides whole grapheme clusters. A first cluster that cannot fit is clipped.
Scrolling, inline-end direction, atomic inlines and transformed content remain
part of the acceptance surface. Legacy clamping must also fit its final painted
line and avoid generating a marker when no content was discarded.

The prechange source only marked a clamp's final rectangle and changed its
width; its source range still includes a glyph that occupies the marker's space.
Ordinary `text-overflow` has overflow measurement but no shared paint consumer.
The new explicit-reference fixture fails in PNG/SVG/PDF at both scales; the same
single-run, mixed-color and clamp references match independently in Chromium.

The implementation derives a temporary paint plan in each concrete block
children paint phase, using one shared scope/iterator implementation. The generic
block sequencer stays independent of text/font services. A block scope owns its plan and restores the parent scope on exit;
nested blocks establish their own applicability. Plan entries borrow the live
view/rectangle/font identity and copy their scalar projections. They never write
back to layout rectangles or survive paint. Retained paint commands must copy
any generated text through their existing payload ownership.

Existing `TextRect::line_number` identifies the block's lines. A single traversal
collects their text extents, using existing font and grapheme helpers. Atomic
inline extents remain part of the outstanding acceptance work. Each overflowing line selects its final visible cluster and one marker
position. The plan supplies a prefix or hidden projection for each affected
rectangle, plus a separate marker projection with the block's font/color. This
preserves inline color differences without inheriting an elided run's style.
Raster, SVG and PDF iterate the same projections through their existing text
painters; fitting and cluster selection are implemented once. No per-leaf rescan
of the entire block is permitted.

Acceptance includes independent pixels at 1×/2×, fitting and overflowing content,
mixed runs, cluster boundaries, direction and atomic inlines, mutation and scroll
recovery, unchanged geometry/hit targets, and repeated paint/retained-payload
lifetime controls. The initial three-case reproducer is not complete P1.3 or
complex-script/bidi qualification; remaining shaping work stays in P2.1.

The first bounded implementation now passes 14 independent Chromium pixel
references and the same explicit-text pairs in PNG/SVG/PDF at 1×/2×. These cover
clip/fitting text, mixed colors and fonts, first-cluster clipping, combining
marks, full uppercase expansion, absolute inline descendants, one/two-line
clamps and opacity/filter fallback. The live fixture has 20 assertions across
font/color changes, truncation disabling and override removal/restoration.
Native tests repeat 256 scopes without modifying source geometry, restore nested
scope state, and preserve recorded font selection while the command buffer grows.

A recorded glyph run captures its `FontBox` by value and borrows the live
owner's style under **D4.5.1v4 / D4.1.4v5**. Lowerers use the captured selection
rather than a mutable walker address. This preserves a marker's block font after
the iterator restores a differently styled text run. The source control failed
when an earlier 20px run observed the walker's later 30px selection.

This record does not close P1.3: RTL/bidi/vertical, atomic inlines, scrolling,
transformed/decorated inline contexts, full shaping/grapheme behavior and broader
clamp grammar/math/CSS-wide/alias cascade remain outstanding. The support manifest
therefore records the three truncation properties as bounded **partial** support.

## 12. Clamp integer computation contract (2026-10-11)

The existing numeric clamp subset must distinguish integer-token grammar from
computed integer math. [CSS Overflow 4 §5.1.1](https://drafts.csswg.org/css-overflow-4/#webkit-line-clamp)
defines the legacy count as `none | <integer [1,∞]>`. Literal zero, negative,
decimal/exponent spellings and dimensions are rejected before cascade; calculated
numbers round at computation and clamp to the positive range. This increment
retains Radiant's integer representation limit, using the shared typed-math and
integer serializer rather than a new calculator. Modern shorthand longhands,
legacy display/orientation applicability and interactions between the two names
remain separate outstanding work.

Layout computes relative-unit ratios after the owner's font is resolved. CSSOM
uses its existing owner-font adapter, including dirty declaration reads before
the next layout pass. Explicit inheritance copies the parent's computed count;
invalid substituted values and non-inherited CSS-wide resets compute to `none`.
The computed scalar survives declaration replacement, while temporary parser and
formatter trees are released after each read (**D4.5.1v4 / D4.1.4v5**). No formal
ruling changes. Acceptance includes invalid declarations preserving an earlier
winner, calculated rounding, causal font/custom-value changes and restoration,
visible clamp effects and repeated-read memory stability. Immediate dirty geometry
reads remain a distinct P0 dependency; computed strings alone do not qualify them.

Ancestor clamp counters stop at independent formatting contexts, using the same
BFC classifier as floats and margin collapse ([CSS Overflow 4 §5.2](https://drafts.csswg.org/css-overflow-4/#max-lines)).
The transient propagation state is distinct from the descendant's computed clamp
declaration; CSSOM reads must never serialize a borrowed ancestor counter. The bounded legacy
display/orientation contract is recorded in §13; broader legacy mappings remain open.

## 13. Legacy box clamp applicability contract (2026-10-11)

[CSS Overflow 4 §5.1.1](https://drafts.csswg.org/css-overflow-4/#webkit-line-clamp)
limits the legacy clamp mode to `display:-webkit-box`/`-webkit-inline-box` with
vertical `-webkit-box-orient`. Computed counts remain observable on other boxes
without affecting their layout. The vertical clamped block computes to
`flow-root`; its inline counterpart computes to `inline-block`. Other legacy
boxes retain their legacy computed spelling and use the existing flex consumer.
The [Compatibility Standard mappings](https://compat.spec.whatwg.org/#css-property-mappings)
acknowledge historical compatibility differences; the bounded implementation
uses the Overflow contract and independently captured Chromium behavior.

Register orientation through the ordinary property/name/IDL catalogs, with
non-inherited `horizontal` initial state, explicit inheritance/defaults, pending
substitution and strict four-keyword grammar. `inline-axis`/`block-axis` compute
to horizontal/vertical. Display computation, CSSOM and clamp setup must share
the same resolved state, including dirty reads and inherited computed display.
Keep computed legacy spelling and used flex routing distinct. Orientation affects
the existing flex direction consumer without overwriting the authored computed
`flex-direction` value.

The native cascade serializer and clamp-winner lookup are shared module helpers,
not copied resolvers. Temporary computation is scratch-owned; scalar display
flags carry no borrowed declaration pointers (**D4.5.1v4 / D4.1.4v5**). Acceptance
requires live display/orientation/count changes and restoration, variable/default
and inheritance cases, same-context/BFC descendants, real geometry/marker paint,
export references and repeated-read lifetime controls. Full legacy flex property
mappings, modern clamp shorthand/longhands and shaping contexts remain distinct
tasks; registering orientation alone does not qualify those features.

Auto-height intrinsic measurement now shares the active clamp count with final
layout. It limits text and `<br>` runs to the visible line contribution, including
the single-text flex path. Count computation uses shared element storage before a
block view tag exists, so grid rows do not reserve space for discarded lines.
`css_text_clamp_intrinsic` covers long and fitting labels, forced breaks, grid row
spacing, and live count changes/removal/restoration. This does not change the
bounded applicability contract above.

The added catalog entry preserves every existing NameId (**D4.6.1v4**). Intrinsic
min-content measurement keeps break-all letters separate while retaining
punctuation and grapheme restrictions; flex's text fast path passes inherited
wrap properties through the shared measurement helper. Flex-item flow bypasses
ordinary block setup, so it shares clamp counter initialization and result
storage. Its overflow viewport refresh follows final content-height refinement.
Otherwise a correctly computed two-line box can clip its second line using the
previous one-line viewport.

The bounded fixture passes 380 live assertions, including 371 Chromium-captured
attributes and nine marker pixels. The preceding qualified binary fails 196 of
those assertions. Twenty-two independent visible references match Chromium and
pass PNG/SVG/PDF at 1×/2×; repeated native computed reads release their scratch.
These results do not qualify modern clamp components, full legacy flex mappings,
other writing modes or the remaining P1.3/P2.1 text model.

Per [CSS Sizing 3 §2.1](https://drafts.csswg.org/css-sizing-3/#max-content-inline-size),
wrapping opportunities change min-content, not unwrapped max-content. The shared
measurement entry measures whole shaped runs separately when break-all or
overflow wrapping needs cluster boundaries. Native Arabic joining/lam-alef,
Latin ligature/kerning and combining-mark controls preserve the normal unwrapped
advance; this does not establish Arabic/Indic paint or full shaping support.

## 14. Visual-edge overflow projection contract (2026-10-11)

**FT-14 — Fit at the block's visual inline end.**
[CSS Overflow 3 §6.1](https://drafts.csswg.org/css-overflow-3/#text-overflow)
selects the ellipsis side from the block direction and removes visual end content.
Extend the existing scoped paint projection with a retained source byte offset
and x position. Its shared cluster scan can retain either a prefix or suffix;
marker placement follows the remaining visual content and keeps the block's font,
color and baseline. A marker preceding a retained suffix is an independent paint
segment, so the existing trailing-marker flag must not append it to the text.
Layout rectangles, geometry, hit testing and selection remain source-owned.

Retained slices initialize capitalization from the preceding source whitespace,
not from their new paint boundary. The current ASCII whitespace rule is shared by
fitting, raster, blurred shadows and export serialization; broader Unicode word
identification stays in P2.1. Computed `text-transform` reads use the shared
cascade serializer because the lazy layout enum cannot represent combined values.
The existing keyword-set validator enforces the grammar from
[CSS Text 3 §2.1](https://drafts.csswg.org/css-text-3/#text-transform-property),
including exclusive case transforms and invalid substitution defaulting. Accepted
combined/full-width/math-auto syntax does not qualify its paint consumer.

Use the existing grapheme, transform and font helpers. Paint scratch belongs to
the scope and contains no retained source pointers after teardown
(**D4.5.1v4 / D4.1.4v5**). Do not reverse UTF-8 bytes or create a second renderer.
The existing source-to-glyph/shaping limitations still belong to P2.1; fitting
physical edges alone does not establish correct mixed-bidi or complex-script
shaping. Later glyph-cluster transport must preserve this visual-edge contract.

Acceptance includes clipped/fitting text, first-cluster clipping, multiple styled
runs, inherited/changed direction and restoration, unchanged source geometry,
marker font/color and repeated nested scopes. Independent Chromium references
and PNG/SVG/PDF pixels must pass at both output scales. Scrolling, transformed
runs, atomic content, clamp/bidi interaction and vertical writing need their own
reproducers before their support status changes.

## 15. Selected-font SVG text contract (2026-10-11)

**FT-15 — Export the font geometry used by layout.** HTML text exported as an SVG
family name can be substituted by the receiving viewer. A local document font
then has different advances and ink bounds: the independent RTL fixture shows
the substituted ellipsis overlapping retained text, although raster and PDF use
the selected face correctly. Reordering paint to conceal that overlap would leave
the font defect in place.

Use the existing text-run outline helper, already shared with PDF, for resolved
HTML text in native SVG output. Preserve the existing glyph-run representation
for raster effect fallback and unbound paint-IR text. Native SVG uses filled paths
from the selected face; it sacrifices searchable text for portable font geometry,
as the existing inline-SVG text exporter does. Decorations, shadows, color glyphs,
complex-script shaping and missing-outline cases require separate qualification.
This changes no formal ruling: paths are command-owned until lowering or teardown
under **D4.5.1v4 / D4.1.4v5**. Each outlined run keeps its visible text as an escaped
accessible title through the existing semantic-group paint commands. Diagram
labels must survive outlining without adding hidden font-painted text.

Acceptance requires independent visible-text references through native SVG,
raster and PDF, including differing font/color runs, transforms, transparency and
filters, plus repeated command ownership checks. The shared outline helper must
remain the only source of text-to-path conversion; no exporter-local glyph loop.


## 16. Computed spacing in paint (2026-10-11)

**FT-16 — Fit and paint the same computed advances.** Layout already includes
computed word and letter spacing. Fitting, direct raster paint, recorded glyph
runs and vector paths must consume those same adjustments after whitespace
processing, with justification added separately. The eight-case current
reproducer loses positive/negative word spacing, collapsed/preserved separator
positions, effects and both ellipsis edges; Chromium matches all eight explicit
separator references. This is a consumer defect, not a new CSS grammar ruling.

Promote the existing letter-spacing helper into the shared layout header and
reuse one character-spacing/space-advance calculation in paint. Retain source
byte offsets while collapsed separators contribute only once; export text
normalizes the selected source slice using its actual white-space mode. A slice
starting within a collapsed sequence must not manufacture a new separator.
Recorded glyph-run word spacing remains the line's extra justification amount;
authored spacing comes from its captured font style. SVG attributes must include
negative as well as positive spacing when live text is required.

This follows [CSS Text 3 §4.1](https://www.w3.org/TR/css-text-3/#white-space-rules)
and [§7](https://www.w3.org/TR/css-text-3/#spacing). Scratch text and captured
font styles retain **D4.5.1v4 / D4.1.4v5** ownership. Qualification requires
native source-offset controls, independent visible-pixel references in all three
output formats, effect fallback and causal inherited-style mutation. Computed
word-spacing normal/initial serializes as `0px`, including declaration reads
before a font snapshot. Live tests read style during mutation and geometry
after its frame commit, preserving the [DOM geometry contract](Radiant_Design_DOM_API.md#34-cssom-layout-and-geometry). Tabs,
complete Unicode separator/word segmentation, cross-node whitespace/shaping,
vertical/bidi spacing and decorated text need their own controls.

## 17. Shared emphasis paint contract (2026-10-13)

**FT-17 — Emphasis belongs to the shared text paint destination.** The current
`emphasis46/marks.html` control paints 114/504 red mark pixels at 1×/2× in native
raster output and emits SVG marks, while independent PDF rasterization has zero
red pixels at both densities. PDF has no emphasis producer; SVG emits live
viewer-font text outside the shared paint list. These paths cannot establish
selected-font, effect or shadow parity.

Use shared mark selection and placement for native and semantic paint. Resolve
the mark from the element's font settings at half size, center it over each
eligible typographic character unit, and consume the same authored/justified
advances as the foreground run. Color resolves currentColor after inheritance;
the mark is independent of a transparent foreground glyph. The CSS requirements
come from [CSS Text Decoration 3 §3](https://www.w3.org/TR/css-text-decor-3/#emphasis-marks)
and [the Level 4 emphasis controls](https://drafts.csswg.org/css-text-decor-4/#emphasis-marks)
under **CI-18**, with selected outlines under **FT-15**. The current native
position approximations are evidence to audit against ruby/line metrics, not
the standard's definition of placement.

The independent Chromium 152 reference in
`emphasis46/browser_reference.json` places the under-dot ink at y=73–77 at 1×
and y=147–156 at 2× for the 40px Arial control. The pre-repair native ink is
y=67–71 and y=135–144, respectively. Both use the same horizontal centers;
the native `font-size * 1.35` baseline loses six CSS pixels of clearance.
Derive the mark baseline from the selected text/mark metrics and actual line
placement; do not use that approximation as the shared placement contract.

For the horizontal profile, use the selected text's content-area ascent/descent
and normalize the half-size mark's OS/2 typo ascent/descent to its em height,
falling back to the selected font's ascent/descent if typo metrics are unusable.
Place an under mark after the text descent plus mark ascent; place an over mark
before the text ascent plus mark descent. Round the outward offset in CSS pixels
before density conversion. This follows the ruby placement model in the CSS
specification; the independent reference is explained by Chromium's
[mark offset computation](https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/core/paint/text_painter.cc)
and [normalized typo metrics](https://chromium.googlesource.com/chromium/src/+/HEAD/third_party/blink/renderer/platform/fonts/simple_font_data.cc).
The selected Arial mark's normalized ascent is about 15.52px, rather than its
18.11px hhea ascent. This metric choice remains subject to independent controls
for other fonts, marks and line heights; ruby collisions need separate layout
integration.

Native bitmap paint and SVG/PDF paths must use the same mark positions and
selected face. SVG must retain accessible marker names through semantic paint
commands, with no receiving-viewer font dependency. Route marks through their
actual effect/transform destination and include them in text-shadow silhouettes
before per-layer alpha. Complete all source borrows synchronously; deferred
paths/images own their geometry under **D4.5.1v4 / D4.1.4v5**. Reuse the existing
outline and text-spacing helpers, and extract the common native/SVG shape before
adding a third exporter-local implementation.

Acceptance requires real pixels in all three outputs at both densities, matching
font/color/position and shorthand winners, transparent/current colors, spaces
and eligible punctuation, multiple runs, translated/filtered/group-opacity and
shadow cases, causal live changes, failure rollback and replay after font/source
retirement. Add independent placement references rather than defining correctness
by agreement between two renderer paths. Grapheme/ligature unit transport,
complex scripts, ruby interaction and vertical orientation stay explicit P2.1
requirements; this producer repair cannot close that wider text contract.

**Draft evidence (2026-10-13):** `emphasis46/full_draft_provenance.json` freezes
the shared producer and its causal CSSOM setter repair. The draft passes 149
vector cases and sixteen live pixel assertions. Native under-dot positions
match the independent browser at both densities; selected PDF marks now reach
actual pixels. Two newly added 1× SVG/PDF open-circle coverage checks remain
failed, with raw density-specific artifacts preserved. CSSOM shorthand writes
must replace their registered components even when the authored shorthand value
is unchanged; retaining an overriding `none` longhand suppresses the producer.
Getter projection and broader font/reader/text contexts remain unqualified.

The independent `emphasis46/circle_reference.json` confirms that Chromium paints
thirty strongly green pixels for the 15px open-circle mark at 1×. The native
selected-font bitmap also has thirty; the unhinted SVG/PDF contour has none,
although its antialiased ring remains visible. Selected face identity alone
does not preserve the small mark's hinted coverage. Record the same hinted glyph
coverage at the destination density and retain it in an owned paint resource
for export, using the existing glyph recorder and capture pipeline. Apply this
policy to the emphasis producer consistently, without circle-specific size or
stroke adjustments. Density-qualified embedded resources remain separate from
arbitrary-resolution vector text; qualification must preserve this limit.

The hinted-resource draft passes SVG and direct Poppler open-circle coverage at
both densities without changing the strong-color assertion. Its 1× macOS
`sips` rasterization still fails that assertion: the embedded PDF mask retains
coverage up to 213, while Poppler paints eleven strongly green samples and sips
paints none. Preserve this reader-specific result alongside the portable-reader
checks. CSSOM component projection now passes twelve independent browser cases,
including longhand edits, pending values, priorities, removals and CSS-wide
keywords. Omitted shorthand color serializes as `initial`; currentColor remains
the computed default. These are draft results pending complete matching-source
qualification.


**Main qualification (2026-10-13, FT-17):** protected `emphasis46mq_*` verifies
the shared selected-font producer after moving renderer-only capture into
`render_text.cpp`. The required core and Radiant gates pass, all 1,171 scoped
ASan cases pass, and 853 live assertions pass in each runtime. The full exporter
passes 267/272 groups with the original 38 assertions plus one 1× sips circle
comparison retained. SVG and direct Poppler pass that unchanged strong-color
control; the official SVG matrix passes 126 formats /2,466 assertions. These
results qualify the horizontal/output-density profile, not broader FT-17
shaping, fragmentation, ruby or vertical contexts. Deferred resources retain
owned payloads under **D4.5.1v4 / D4.1.4v5**.

## 18. Inside image-marker baseline contract (2026-10-15)

**FT-18 — Horizontal inside marker images use the inline replaced baseline.** The separate
`marker_baseline_probe/` compares natural 2px markers with equivalent inline
images across three font sizes, normal/explicit line heights and empty/nonempty
contents. Chromium aligns all twelve pairs; the protected affine checkpoint
aligns none. Native markers remain at the line top. Their layout creates an
image-sized fragment, but the final vertical-alignment dispatcher has no marker
case. Small image markers also bypass the replaced-content flag and contribute
only the font strut.

[CSS Lists 3 §3.2/§3.5](https://www.w3.org/TR/css-lists-3/#marker-content)
places an anonymous inline replaced image followed by a space inside the marker;
an inside marker participates at the start of the inline contents.
[CSS 2.2 §10.8.1](https://www.w3.org/TR/CSS22/visudet.html#propdef-vertical-align)
aligns a baselineless inline box through its bottom margin edge. Preserve the
marker font/space strut while contributing the image's bottom-edge ascent and
replaced-content state. Final placement uses the existing baseline calculator
and vertical-alignment helper, rather than a font-size-dependent paint offset.

Qualify natural/density-adjusted image sizes, larger images, marker font struts,
normal/explicit line heights, empty/text contents and causal font/source changes.
Compare real pixels with independent inline-image references and keep the
existing image-sampling/resource controls. No new declaration or image lifetime
owner is needed (**D4.5.1v4 / D4.1.4v5**). Outside placement, default-sized
gradients, wrapping, bidi/vertical and fragmented contexts need separate
acceptance evidence; the current outside-marker policy must not be silently
replaced by the inside-inline rule. This bounded P1.3 increment builds on the affine checkpoint, whose core gate
passes all 6,964 core/input cases. Its implementation is under matching-source
qualification. The expanded exploratory probe contains 64 rows:
all 52 URL/density-image references agree in Chromium, against 20/52 image
positions and 34/52 line boxes natively. Its twelve gradient references expose
a separate default-object-size difference in Chromium; retain those records
without treating them as acceptance controls for the intrinsic-image repair.


FT-18 qualification draft: marker generation can precede the list item's line
context. Refresh metrics from the current marker face on every reflow, resolve
non-normal leading from the owner's computed value, and share
`font_get_content_area_split` / `layout_apply_line_height_leading` with inline
layout. This replaces a raw half-leading calculation that could add a fractional
ascent/descent mismatch and an extra pixel. Image height also contributes to
the existing atomic-extent bound. Native URL image positions now agree in
52/52 exploratory pairs; 50/52 line pairs agree. In the two remaining normal
large-font controls, the marker's 37px box agrees with Chromium while the
nested-inline reference is 43px. Preserve that separate P1.3 strut defect.

The 95-check live fixture includes zero leading and causal font, pseudo-font,
image-density and suppression/restoration changes. Native passes all 95 against
53/95 on the affine predecessor. Chromium passes 87/95: eight assertions differ
because changing the resolution of a same-URL image-set retains the previous
2px marker size despite correct computed CSS, even after 500ms settling. Static
URL-density controls agree in Chromium. Keep all live assertions and the
original 81-check records; do not change native sizing to match that cache
behavior. The independent 48-case exporter supplies 96 pairs and 288 exact
PNG/SVG/PDF comparisons at both densities, all passing with positive color
coverage. The earlier sampling fixture's containing block moves from 24px to
128px so baseline-aligned quarter turns remain visible; its comparisons and
positive-color thresholds stay unchanged. Broader matching-source gates follow.


The refreshed browser exporter check retains all exact comparisons: 112/160
whole images match (64 sampling and 48 empty baseline pairs). In the 48
nonempty baseline pairs, Chromium's marker-to-text spacing differs from the
inline-image reference. Image bounds and line-box bounds agree in **96/96**
baseline pairs at both densities. Keep the full-image failures alongside those
geometry measurements; they do not establish matching marker text spacing.
The single-space native marker model remains as specified by CSS Lists 3 §3.2.

## 19. Nested inline font and block strut contract (2026-10-15)

**FT-19 — Keep the block strut's font metrics and leading paired.** A marker-free
24-row probe compares a 16px block containing 24/32/48px inline text with an
equivalent large-font block. It varies normal/unitless leading, a small replaced
image and text inside/outside the large span. Chromium agrees in all 24 line
boxes; both the protected affine and marker-baseline checkpoints agree in 12.
Normal lines with images are too tall; explicit-leading text-only lines lose
one pixel. Original images and measurements remain under
`temp/css_support_p0_p1_resume/nested_font_strut_probe/`.

[CSS 2.2 §10.8.1](https://www.w3.org/TR/CSS22/visudet.html#leading) gives the
block's zero-width strut its own font and line-height, while each inline box
contributes its own leading-adjusted metrics. Snapshot block leading alongside
the existing initial font metrics when establishing a formatting context.
Entering a styled inline may change active leading but must not change that
pair. Replaced-image descent, root-strut contribution, baseline placement and
line finalization must read the block pair when they mean the block strut.
Preserve valid zero leading. Reuse the existing block-metric initializer across
formatting contexts instead of adding another font-metric implementation.

Normalize each inline's metrics before taking the line union. Do not subtract
the block font's rounding excess from an already normalized descendant union.
Qualify the reproduced matrix, larger/smaller descendants, explicit lengths,
zero leading, wrapping and causal font/leading changes with browser geometry
and native pixels. Keep the marker regression and existing mixed-font controls.
This adds layout-local scalar state, with no new declaration, font or image
lifetime owner (**D4.5.1v4 / D4.1.4v5**). Vertical, bidi, shaping, ruby and
fragmented layout remain separately qualified profiles.

The first draft fixes all 24 line boxes and 126 live geometry/pixel checks, but
unchanged full-image comparisons expose a second metric split: macOS normal
leading divides an odd leading pixel equally, while unaligned single-font text
keeps its original origin. A large descendant is finalized through the baseline
and moves by half a pixel. Chromium matches all 24 text/image reference pairs.
Apply the same floor-to-ascent/remainder-to-descent policy already used for
explicit leading to platform normal leading. Initialize baseline text from its
own normalized metrics, so the plain and mixed-font paths agree before and
after final alignment. Preserve raw failed exports and browser controls.

The narrow wrapping probe separately shows an image-only first fragment losing
the enclosing large-font strut when its following word moves to the next line.
An inline contributes its font/leading to every occupied line fragment, even
when that fragment contains only replaced content. Publish that contribution
before a child can close the line; do not invent a taller image or change the
line-break opportunity. Empty/out-of-flow-only fragments retain their existing
line-generation rules and need regression coverage.

The broader draft exposed two applicability errors: initial letters and
non-baseline alignment already own their line contributions. Preserve those
paths. Publish the ordinary baseline strut ahead of replaced/forced-break
children; text publishes its normalized metrics when it actually occupies a
line, so collapsed trailing whitespace cannot create an extra contribution.
USE_TYPO_METRICS normal splitting also placed the font's descent above the
baseline (`ascent + descent`, zero descent for a zero line gap). Keep the
typographic ascent and descent on their own sides, splitting only the line gap.
The existing Hasubi Mono end-of-line controls retain this regression evidence.

The existing 24-check styled skip-ink fixture now declares 78px leading for its
70px Arial face. All assertions, pattern phases and density controls remain
unchanged; explicit leading isolates its ink-gap contract from the corrected
normal-leading placement. The original source, ten failed draft checks and the
passing explicit-leading diagnostic remain protected alongside the new
independent normal-leading browser comparisons. No layout baseline scores or
browser reference files are changed.

The required core gate later exposes six word-cloud SVG baselines that used the
old half-pixel split through `radiant.measure_html`. Arial weight 600 at
28/24.5623/22.0247/19.2178/17.682/16.4566px now reports height/baseline pairs
32/25, 28/22, 26/20, 22/17, 21/16 and 19/15. Current Chromium agrees in all six;
the previous native baselines are each 0.5px larger. The unchanged dashboard
golden therefore remains a raw core failure. A native retained-reflow control
protects the browser-confirmed cell rounding; no chart-specific compensation
or golden/reference change is applied.
