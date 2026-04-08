# Design Proposal: Migrate Font & Text Pipeline Away from FreeType

**Status:** Proposal  
**Author:** Lambda Team  
**Date:** April 2026

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

FreeType is a 140,000-line library. Our actual usage reduces to 22 distinct API
functions across 64 production call sites, performing 4 core operations:

| Operation | FT APIs | Call Sites | Can Replace With |
|-----------|---------|------------|------------------|
| Parse font from file/memory | `FT_New_Face`, `FT_New_Memory_Face`, `FT_Done_Face` | 5 | Custom TTF table parser |
| Set size + variable axes | `FT_Set_Pixel_Sizes`, `FT_Select_Size`, `FT_Get/Set_Var_*` | 7 | `head.unitsPerEm` scaling |
| Get glyph metrics | `FT_Get_Char_Index`, `FT_Load_Glyph`, `FT_Get_Kerning`, `FT_Get_Sfnt_Table` | 39 | `cmap` + `hmtx` + `kern` parsing |
| Rasterize glyph bitmap | `FT_Load_Glyph(RENDER)` | 6 | Platform API or ThorVG |

We use none of: autohinter, Type 1/CID/BDF/PCF/PFR support, caching subsystem,
stroker, glyph management, bitmap conversion, LCD filtering, or module system
beyond default initialization.

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

### Phase 1: TTF Table Parser + Metrics Replacement

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
  create FontHandle without `FT_New_Face`. Keep `FT_New_Face` only for rasterization.
- Modified: `font_metrics.c` — read from `FontTables*` instead of `FT_Get_Sfnt_Table`
- Modified: `font_glyph.c` — use `cmap_lookup()` + `hmtx` instead of
  `FT_Get_Char_Index` + `FT_Load_Glyph` for measurement

**FT calls eliminated:** ~45 of 64 (all except rasterization)  
**Risk:** Low. Each replacement is testable per-function. Advances can be validated
against FreeType output before switching.  
**Validation:** All 3,939 baseline layout tests must pass. Text widths compared
against FreeType output with <0.01px tolerance.

### Phase 2: Platform Rasterization

**Goal:** Replace `FT_Load_Glyph(FT_LOAD_RENDER)` with platform-native or ThorVG
rasterization. Remove FreeType entirely.

**Deliverables:**
- New file: `lib/font/font_rasterize.c` (~400 lines) — dispatches to platform backend
- New file: `lib/font/font_rasterize_ct.c` (~200 lines) — macOS CoreText backend
- New file: `lib/font/font_rasterize_dw.c` (~200 lines) — Windows DirectWrite backend
- New file: `lib/font/font_rasterize_tvg.cpp` (~400 lines) — ThorVG backend
  - Includes `glyf` table parser for TrueType outline extraction (~500 lines)
  - Quad bezier → ThorVG cubic bezier conversion
- Modified: `font_context.c` — remove `FT_New_Library`, `FT_Done_Library`, custom
  FT_Memory
- Modified: `font_loader.c` — remove `FT_New_Face`, `FT_New_Memory_Face`
- Modified: `font_glyph.c` — `font_load_glyph(for_rendering=true)` calls
  `font_rasterize_glyph()` instead of FreeType
- Removed: All `#include <ft2build.h>` and FreeType header includes
- Modified: `build_lambda_config.json` — remove FreeType from dependencies
- Modified: `premake5.mac.lua` — remove FreeType include/lib paths (~20 occurrences)
- Modified: `setup-mac-deps.sh`, `setup-linux-deps.sh`, `setup-windows-deps.sh` —
  remove FreeType install steps

**FT calls eliminated:** remaining ~19 (rasterization + library lifecycle)  
**Risk:** Medium. Rasterization is visually sensitive. Requires pixel-level
comparison of rendered output.  
**Validation:** Render all baseline HTML test files, compare PNG output against
FreeType-era reference images. Visual diff must be <1% pixel difference.

### Phase 3: Unified Cache + Font File Sharing

**Goal:** Implement L2 font file cache so multiple sizes share parsed tables. Add
L0 glyph strike cache with LRU eviction.

**Deliverables:**
- New struct: `FontFileEntry` in font_internal.h — holds mmap'd buffer + FontTables*
- Modified: `font_cache.c` — L2 cache layer keyed by file path
- Modified: `font_context.c` — L0 glyph strike cache with configurable size limit
- Modified: FontHandle — references shared `FontFileEntry` instead of owning buffer

**Risk:** Low. Caching is an optimization; correctness unchanged.  
**Validation:** Performance benchmarks on batch layout (100 markdown files).
Memory usage profiling.

### Phase 4: Cleanup + PDF Module

**Goal:** Migrate the remaining FreeType usage in `radiant/pdf/` and
`radiant/ui_context.cpp`.

**Deliverables:**
- Modified: `radiant/pdf/fonts.cpp` — use `font_tables.c` + `font_rasterize.c`
  instead of direct FreeType calls (7 call sites)
- Modified: `radiant/pdf/cmd_view_pdf.cpp` — use `font_load_glyph()` instead of raw
  `FT_Load_Char` (1 call site)
- Modified: `radiant/ui_context.cpp` — remove legacy FT_Library lifecycle (5 call
  sites)
- Removed: FreeType from all build targets, CI, dependency scripts

**FT calls eliminated:** 13 (all remaining)  
**Risk:** Low. PDF module has its own test suite.

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

| Phase | New Code | Modified Code | Effort |
|-------|----------|---------------|--------|
| **Phase 1:** Table parser + metrics | ~1,500 lines (`font_tables.c`) | ~500 lines across `font_loader.c`, `font_metrics.c`, `font_glyph.c` | Medium |
| **Phase 2:** Platform rasterization | ~1,300 lines (3 backends + glyf parser) | ~300 lines across `font_glyph.c`, `font_context.c`, build system | Medium |
| **Phase 3:** Unified cache | ~300 lines (L2 file cache, L0 strike cache enhancements) | ~200 lines in `font_cache.c`, `font_context.c` | Small |
| **Phase 4:** PDF + cleanup | 0 new files | ~200 lines across `radiant/pdf/`, `radiant/ui_context.cpp`, build scripts | Small |
| **Total** | ~3,100 new lines | ~1,200 modified lines | |

For reference, the current `lib/font/` module is ~5,300 lines. After migration it
would grow to ~7,500 lines — reasonable for a self-contained font subsystem.

---

## 9. Validation Strategy

### Per-Phase Testing

| Phase | Test Method |
|-------|------------|
| Phase 1 | For each replaced function, run both old (FreeType) and new (custom) paths, compare outputs. Advance widths must match within 0.01px. `cmap_lookup` output must exactly match `FT_Get_Char_Index`. All 3,939 baseline tests pass. |
| Phase 2 | Render all baseline HTML files to PNG. Pixel-diff against FreeType-era reference PNGs. Accept <1% pixel difference (subpixel rounding). Visual review of 10 representative markdown pages. |
| Phase 3 | Performance benchmark: layout 100 markdown files, compare wall time and peak memory before/after. Expect ≥10% speedup from font file sharing. |
| Phase 4 | PDF test suite passes. Build completes without FreeType on all platforms. |

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
