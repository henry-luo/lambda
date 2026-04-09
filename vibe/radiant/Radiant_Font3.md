# Proposal: Complete FreeType Elimination from Lambda/Radiant

**Status:** Proposed  
**Author:** Lambda Team  
**Date:** April 2026  
**Prerequisite:** Radiant_Design_Font_Text.md (Phase 1–5 complete)

---

## 1. Current State

FreeType has been eliminated from all `radiant/` source files (Phase 1–5).
The **only remaining FreeType usage** is in `lib/font/` — 4 files, ~54 call sites:

| File | FT Calls | Role |
|------|:--------:|------|
| `font_internal.h` | — | FT headers, `FT_Face`/`FT_Library` in structs |
| `font_context.c` | 11 | Library lifecycle, custom memory, face cleanup |
| `font_loader.c` | ~25 | Face loading, size selection, variable font axes |
| `font_glyph.c` | ~18 | Glyph metrics, bitmap rasterization (FT = fallback on macOS) |

No FT types leak through the public API (`font.h`). All external accessors
(`font_context_get_ft_library`, `font_handle_get_ft_face`) return `void*`.

### What FreeType Does Today

| Operation | FT APIs | Replaceable? |
|-----------|---------|:---:|
| Parse font from memory | `FT_New_Memory_Face` (5 sites) | ✓ |
| Library lifecycle | `FT_New_Library`, `FT_Add_Default_Modules`, `FT_Done_Library` | ✓ |
| Custom allocator | `FT_MemoryRec_` callbacks → Pool | Remove entirely |
| Set size | `FT_Set_Pixel_Sizes`, `FT_Select_Size` | ✓ |
| Variable font axes | `FT_Get_MM_Var`, `FT_Set_Var_Design_Coordinates` | ✓ |
| Glyph metrics | `FT_Load_Glyph(LOAD_DEFAULT)` → advance, bearings | ✓ |
| Glyph rasterization | `FT_Load_Glyph(LOAD_RENDER)` → bitmap | ✓ |
| PostScript name | `FT_Get_Postscript_Name` | ✓ |
| Family name | `face->family_name` | ✓ |
| Face flags | `FT_FACE_FLAG_FIXED_SIZES`, `_COLOR`, `_MULTIPLE_MASTERS` | ✓ |
| Face count (TTC) | `face->num_faces` | ✓ |

### What Already Replaces FreeType

**FontTables** (custom TTF parser, `lib/font/font_tables.c`):
- `cmap` (format 4+12) — replaces `FT_Get_Char_Index`
- `hmtx` — glyph advance widths, replaces metrics-only `FT_Load_Glyph`
- `kern` (format 0) — pair kerning, replaces `FT_Get_Kerning`
- `head` — units_per_em, mac_style, replaces `FT_FACE_FLAG_*` queries
- `hhea` — ascender/descender/line_gap
- `OS/2` — weight, fsSelection, typo metrics, x-height, cap-height
- `name` — family, subfamily, PostScript name (replaces `face->family_name`, `FT_Get_Postscript_Name`)
- `fvar` — variable font axis metadata
- `glyf`/`loca` — glyph bounding boxes
- `post` — underline metrics

**CoreText** (macOS, `lib/font/font_rasterize_ct.c` + `font_platform.c`):
- `font_rasterize_ct_create()` — create CTFont from raw TTF/OTF bytes
- `font_rasterize_ct_metrics()` — per-glyph advance + bbox (no bitmap)
- `font_rasterize_ct_render()` — 8-bit grayscale / BGRA bitmap rasterization
- `font_platform_get_glyph_advance()` — CT advance override (already primary for layout)
- `font_platform_get_pair_kerning()` — GPOS kerning
- `font_platform_create_ct_font()` — system font by name + weight
- `get_font_metrics_platform()` — line-height metrics (Chrome-compatible)
- `font_platform_find_codepoint_font()` — fallback font discovery

**`font_metrics.c`** already uses **zero FT calls** — 100% FontTables.

---

## 2. Goal

Remove `libfreetype.a` (768KB), its transitive dependencies (HarfBuzz 1.2MB,
zlib, bzip2, brotli portions), and all `FT_*` API calls from the codebase.
Replace with platform-native rasterizers and the existing FontTables parser.

### Benefits

- **~2MB smaller binary** (FreeType + HarfBuzz + transitive deps)
- **Zero third-party font code** — full control over the pipeline
- **Simpler build** — remove FreeType include/lib paths from all 4 platform configs
- **Eliminate dual-path bugs** — currently every glyph operation has FT primary +
  CT override; single path is simpler and matches browser output
- **Faster startup** — no FT_Library initialization or module registration

### Non-Goals

- OpenType shaping (GSUB/GPOS ligatures). Requires HarfBuzz, not FreeType.
- GPU text rendering or glyph atlases.
- CFF2 variable outline support (TrueType `glyf` covers system fonts).

---

## 3. Platform Strategy

### 3.1 macOS — CoreText (Ready Now)

CoreText already provides a **complete replacement pipeline**. On macOS today:

- **Rendering**: CT is primary rasterizer; FT is already just a fallback
- **Glyph metrics**: FT provides base metrics, but CT always overrides the advance
- **Font metrics**: 100% FontTables (zero FT)
- **Font loading**: `font_rasterize_ct_create()` creates CTFont from raw bytes

**Implementation**: Promote CT from secondary to sole backend. Remove all FT
fallback paths in `font_glyph.c`. No new code required — just deletion.

### 3.2 Linux — FontTables Metrics + ThorVG Rasterization

Linux has no system font rasterizer API. FreeType is currently the **only** glyph
loader and bitmap rasterizer.

**Replacement**: Two-layer approach using existing dependencies:

1. **Glyph metrics (layout)** — FontTables `cmap` + `hmtx` already provide
   advance widths. Add `glyf` outline parsing for bearing/bbox (partially exists
   via `font_tables_get_glyph_bbox`). This covers the layout path with no new
   dependency.

2. **Glyph rasterization (rendering)** — Use **ThorVG** (already linked, v1.0-pre34)
   as a **vector path rasterizer**. Extract TrueType glyph outlines from the `glyf`
   table via FontTables, convert quadratic beziers to cubic
   (`CP1 = P0 + 2/3(P1-P0)`, `CP2 = P2 + 2/3(P1-P2)`), feed into `tvg_shape_*`,
   render to a pixel buffer via `tvg_swcanvas`. This bypasses ThorVG's text API
   entirely — using it purely as a software rasterizer.

**ThorVG advantages:**
- Zero new dependencies — already linked for SVG rendering and wavy line decoration
- High-quality analytic anti-aliasing (same quality as SVG path rendering)
- COLR/CPAL color glyphs possible via layered shapes with palette colors
- No binary size increase (already in the build)

### 3.3 Windows — DirectWrite

Windows has **DirectWrite**, the native text rendering API (equivalent to CoreText).

**Replacement**: Implement a `font_rasterize_dw.c` module parallel to
`font_rasterize_ct.c`:

| CoreText API | DirectWrite Equivalent |
|-------------|----------------------|
| `CGFontCreateWithDataProvider` | `IDWriteFactory::CreateCustomFontFileReference` |
| `CTFontCreateWithGraphicsFont` | `IDWriteFontFace::CreateFontFace` |
| `CTFontGetAdvancesForGlyphs` | `IDWriteFontFace::GetDesignGlyphMetrics` |
| `CTFontGetBoundingRectsForGlyphs` | `IDWriteFontFace::GetDesignGlyphMetrics` |
| `CTFontDrawGlyphs` | `ID2D1RenderTarget::DrawGlyphRun` or `IDWriteBitmapRenderTarget::DrawGlyphRun` |
| `CTFontGetAscent/Descent/Leading` | `IDWriteFontFace::GetMetrics` |
| `kCTFontVariationAttribute` | `IDWriteFontFace5::GetFontAxisValues` |
| `CTFontCreateForString` (fallback) | `IDWriteFontFallback::MapCharacters` |

**Note**: DirectWrite can also be the Linux fallback if targeting minimal visual
quality requirements, but ThorVG with direct glyph outline extraction avoids
adding any new dependency.

---

## 4. Implementation Plan

### Phase 6: macOS — Promote CoreText to Sole Backend

**Files changed**: `font_glyph.c`, `font_loader.c`, `font_context.c`, `font_internal.h`

#### 6.1 `font_loader.c` — Replace FT_New_Memory_Face with CT

Current flow:
```
FT_New_Memory_Face(data, len) → FT_Face
FT_Set_Pixel_Sizes(face, size)
apply_variable_font_axes(face, opsz, wght)
font_rasterize_ct_create(data, len, size)        // ct_raster_ref
font_platform_create_ct_font(ps_name, family, size, weight)  // ct_font_ref
```

New flow (macOS):
```
font_rasterize_ct_create(data, len, size)        // ct_raster_ref (primary)
font_platform_create_ct_font_var(family, size, weight, opsz)  // ct_font_ref with variation
font_tables_open(data, len)                       // tables (metrics)
```

**Changes:**
- Remove all `FT_New_Memory_Face` calls; `ct_raster_ref` and `tables` are sufficient
- New function: `font_platform_create_ct_font_var()` — creates CTFont with
  `kCTFontVariationAttribute` for opsz/wght axes (replaces `apply_variable_font_axes`)
- `FontHandle.ft_face` becomes NULL on macOS; all code paths must guard on
  `ct_raster_ref` / `tables` instead
- `FT_Get_Postscript_Name` → `font_tables_get_name(tables)->postscript_name`
- `face->family_name` → `font_tables_get_name(tables)->family_name`
- `face->face_flags & FT_FACE_FLAG_FIXED_SIZES` → check for `CBDT` or `sbix` table
  presence in FontTables (add `font_tables_has_table(tables, tag)` helper)
- `face->face_flags & FT_FACE_FLAG_COLOR` → same CBDT/COLR/sbix table check
- `face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS` → `font_tables_get_fvar(tables) != NULL`
- Bitmap scale for emoji: compute from `head.units_per_em` + requested size vs
  strike size in `CBDT`/`sbix` header

#### 6.2 `font_glyph.c` — Remove FT Fallback Paths

Current glyph loading (3 tiers):
```
1. FT_Load_Glyph          (primary)
2. ct_raster_ref metrics   (secondary)
3. FontTables hmtx         (tertiary)
→ CT advance override      (always, if ct_font_ref exists)
```

New glyph loading (macOS):
```
1. ct_raster_ref metrics   (primary — full advance + bbox)
2. FontTables hmtx/glyf    (fallback — advance only, if CT fails)
→ ct_font_ref advance      (override for system fonts, as before)
```

**Changes:**
- `font_get_glyph()`: Remove FT primary path (lines 186–205). CT `ct_raster_ref`
  becomes the first attempt. FontTables `hmtx` becomes fallback.
- `font_load_glyph()`: Remove `try_load_from_handle()` FT function. CT path
  (`try_load_from_handle_ct()`) becomes the only rendering path.
- `font_render_glyph()`: Remove FT fallback after CT render. If CT fails, glyph
  is missing (matches browser behavior — browsers don't have an FT fallback).
- Delete `fill_loaded_glyph_from_slot()` — only exists to read FT_GlyphSlot fields.
- Remove `FT_LOAD_*` flag construction code.
- Remove `FT_PIXEL_MODE_*` constants.

#### 6.3 `font_context.c` — Remove FT Library Lifecycle

**Changes:**
- Remove `FT_New_Library`, `FT_Add_Default_Modules`, `FT_Library_SetLcdFilter` from
  `font_context_create()`
- Remove `FT_Done_Library` from `font_context_destroy()`
- Remove `FT_Done_Face` from `font_handle_release()` (nothing to release)
- Remove custom `FT_MemoryRec_` callbacks (`ft_pool_alloc`, `ft_pool_free`, `ft_pool_realloc`)
- Remove `font_context_get_ft_library()`, `font_handle_get_ft_face()`, `font_handle_wrap()`
  migration helpers
- Remove `FT_Library ft_library` from `FontContext` struct
- Remove `FT_MemoryRec_ ft_memory` from `FontContext` struct

#### 6.4 `font_internal.h` — Remove FT Types

**Changes:**
- Remove all `#include <ft2build.h>` and `FT_*_H` includes (11 lines)
- Remove `FT_Face ft_face` from `FontHandle` struct
- Remove `FT_Library ft_library` and `FT_MemoryRec_ ft_memory` from `FontContext`
- Replace `FT_Face` parameter types with `void*` or remove entirely

#### 6.5 `font_fallback.c` — Remove num_faces Reference

**Change**: Replace `handle->ft_face->num_faces` with FontTables-based TTC face
count. Add `font_tables_get_face_count()` that reads the TTC header `numFonts`
field (4 bytes at offset 8 in a TTC file).

#### 6.6 `font.h` — Remove Public FT Escape Hatches

**Changes:**
- Remove `font_context_get_ft_library()` declaration
- Remove `font_handle_get_ft_face()` declaration
- Remove `font_handle_wrap()` declaration
- Remove FT-related comments

#### 6.7 New: `font_platform_create_ct_font_var()` — Variable Font Support

```c
// Create CTFont with variation axes (opsz, wght) applied
void* font_platform_create_ct_font_var(const char* family_name,
                                        float size_px, int weight, float opsz);
```

Implementation uses CoreText's `kCTFontVariationAttribute`:
```c
// Build variation dictionary: { 'opsz': size_px, 'wght': weight }
CFNumberRef opsz_val = CFNumberCreate(NULL, kCFNumberFloatType, &opsz);
CFNumberRef wght_val = CFNumberCreate(NULL, kCFNumberIntType, &weight);
CFDictionaryRef variations = CFDictionaryCreate(NULL,
    (const void*[]){ kCTFontOpticalSizeAttribute_tag, kCTFontWeightTrait_tag },
    (const void*[]){ opsz_val, wght_val }, 2, ...);
// Apply via CTFontDescriptorCreateWithAttributes + CTFontCreateWithFontDescriptor
```

#### 6.8 New: `font_tables_has_table()` — Table Presence Check

```c
// Check if a specific table exists (e.g., 'CBDT', 'COLR', 'sbix', 'fvar')
bool font_tables_has_table(FontTables* ft, uint32_t tag);
```

Scans the table directory already parsed during `font_tables_open()`.

#### 6.9 New: `font_tables_get_face_count()` — TTC Face Count

```c
// Get number of faces in a TTC collection (1 for non-TTC fonts)
int font_tables_get_face_count(const uint8_t* data, size_t len);
```

Reads the TTC header `numFonts` field. Returns 1 for non-TTC files.

**Estimated changes**: ~400 lines deleted, ~150 lines added. Net reduction ~250 lines.

**Validation**: `make test-radiant-baseline`, `make test-lambda-baseline`, plus visual
comparison of rendered pages.

---

### Phase 7: Linux — FontTables + ThorVG Rasterizer

**New files**: `lib/font/font_glyf.c`, `lib/font/font_rasterize_tvg.cpp`

Linux has no system font rasterizer API. The replacement uses two components that
already exist in the project:

1. **FontTables** — glyph metrics (`cmap` + `hmtx`) and outline extraction (`glyf`/`loca`)
2. **ThorVG** (already linked, v1.0-pre34) — software path rasterizer via `SwCanvas`

ThorVG's public text API (`tvg_text_*`) operates on strings, not individual glyphs,
and exposes no per-glyph metrics or outline access. Instead, we **bypass the text
API entirely** and use ThorVG purely as a **vector path rasterizer**: extract glyph
outlines from the `glyf` table ourselves, convert to cubic bezier paths, feed them
into `tvg_shape_*`, and rasterize via `tvg_swcanvas`.

This approach is already planned in Radiant_Design_Font_Text.md §6.3.

#### 7.1 `font_glyf.c` — TrueType Outline Extraction (~500 lines)

New FontTables extension that parses the `glyf` table to extract glyph outlines:

```c
// Glyph outline representation
typedef struct {
    float x, y;
    bool  on_curve;   // true = on-curve point, false = off-curve control point
} GlyfPoint;

typedef struct {
    GlyfPoint* points;
    int*       contour_ends;   // index of last point in each contour
    int        num_points;
    int        num_contours;
    int16_t    x_min, y_min, x_max, y_max;  // bounding box in font units
} GlyfOutline;

// Extract outline for a glyph (handles simple + compound glyphs)
bool font_glyf_get_outline(FontTables* tables, uint16_t glyph_id,
                            GlyfOutline* out, Pool* pool);
```

Parsing steps:
1. Look up glyph offset via `loca` table (short or long format, from `head.indexToLocFormat`)
2. Read glyph header: `numberOfContours`, bounding box
3. **Simple glyph** (`numberOfContours >= 0`): read contour endpoint array,
   instruction length (skip), flag array (with repeat handling), x/y coordinate
   deltas (1-byte or 2-byte per flag bits)
4. **Compound glyph** (`numberOfContours == -1`): loop through components — each has
   a glyph index + transform (translate, scale, or 2×2 matrix). Recursively get
   component outlines and apply transforms.

#### 7.2 `font_rasterize_tvg.cpp` — ThorVG Path Rasterizer (~400 lines)

```cpp
// Same 3-function interface as font_rasterize_ct.c
void* font_rasterize_tvg_create(const uint8_t* data, size_t len,
                                 float size_px, int face_index);
bool  font_rasterize_tvg_metrics(void* tvg_ref, uint32_t codepoint,
                                  float bitmap_scale, GlyphInfo* out);
GlyphBitmap* font_rasterize_tvg_render(void* tvg_ref, uint32_t codepoint,
                                        float bitmap_scale, Arena* arena,
                                        GlyphRenderMode mode);
```

Internal state:
```cpp
typedef struct {
    FontTables* tables;
    float       size_px;
    float       scale;         // size_px / units_per_em
    Pool*       pool;
} TvgFontRef;
```

**Metrics** — use existing FontTables (no ThorVG needed):
```cpp
bool font_rasterize_tvg_metrics(void* tvg_ref, uint32_t codepoint,
                                 float bitmap_scale, GlyphInfo* out) {
    TvgFontRef* ref = (TvgFontRef*)tvg_ref;
    CmapTable* cmap = font_tables_get_cmap(ref->tables);
    HmtxTable* hmtx = font_tables_get_hmtx(ref->tables);
    uint16_t glyph_id = cmap_lookup(cmap, codepoint);
    if (!glyph_id) return false;

    // advance from hmtx
    uint16_t advance = hmtx_get_advance(hmtx, glyph_id);
    out->id = glyph_id;
    out->advance_x = (float)advance * ref->scale / bitmap_scale;

    // bbox from glyf table
    GlyfOutline outline;
    if (font_glyf_get_outline(ref->tables, glyph_id, &outline, ref->pool)) {
        out->bearing_x = (float)outline.x_min * ref->scale / bitmap_scale;
        out->bearing_y = (float)outline.y_max * ref->scale / bitmap_scale;
        out->width = (float)(outline.x_max - outline.x_min) * ref->scale / bitmap_scale;
        out->height = (float)(outline.y_max - outline.y_min) * ref->scale / bitmap_scale;
    }
    return true;
}
```

**Rasterization** — extract outline, convert to ThorVG shape, render to buffer:
```cpp
GlyphBitmap* font_rasterize_tvg_render(void* tvg_ref, uint32_t codepoint,
                                        float bitmap_scale, Arena* arena,
                                        GlyphRenderMode mode) {
    TvgFontRef* ref = (TvgFontRef*)tvg_ref;
    CmapTable* cmap = font_tables_get_cmap(ref->tables);
    uint16_t glyph_id = cmap_lookup(cmap, codepoint);
    if (!glyph_id) return NULL;

    // 1. Extract TrueType outline from glyf table
    GlyfOutline outline;
    if (!font_glyf_get_outline(ref->tables, glyph_id, &outline, ref->pool))
        return NULL;

    // 2. Compute pixel dimensions
    float scale = ref->size_px * bitmap_scale
                  / (float)font_tables_get_head(ref->tables)->units_per_em;
    int px_w = (int)ceilf((outline.x_max - outline.x_min) * scale) + 2;
    int px_h = (int)ceilf((outline.y_max - outline.y_min) * scale) + 2;
    if (px_w <= 0 || px_h <= 0) return NULL;

    // 3. Build ThorVG shape from outline contours
    Tvg_Paint shape = tvg_shape_new();
    int pt_idx = 0;
    for (int c = 0; c < outline.num_contours; c++) {
        int contour_end = outline.contour_ends[c];
        bool first = true;
        while (pt_idx <= contour_end) {
            GlyfPoint p = outline.points[pt_idx];
            float sx = (p.x - outline.x_min) * scale;
            float sy = (outline.y_max - p.y) * scale;  // flip Y axis

            if (first) {
                tvg_shape_move_to(shape, sx, sy);
                first = false;
            } else if (p.on_curve) {
                tvg_shape_line_to(shape, sx, sy);
            } else {
                // quadratic bezier → cubic: CP1 = P0 + 2/3(P1-P0)
                // get next on-curve point (or midpoint if also off-curve)
                // ... standard TrueType quadratic-to-cubic conversion
                tvg_shape_cubic_to(shape, cp1x, cp1y, cp2x, cp2y, ex, ey);
            }
            pt_idx++;
        }
        tvg_shape_close(shape);
    }
    tvg_shape_set_fill_color(shape, 255, 255, 255, 255);

    // 4. Rasterize via ThorVG software canvas
    uint32_t* buffer = (uint32_t*)pool_calloc(ref->pool, px_w * px_h * 4);
    Tvg_Canvas canvas = tvg_swcanvas_create(0);
    tvg_swcanvas_set_target(canvas, buffer, px_w, px_w, px_h, TVG_COLORSPACE_ABGR8888);
    tvg_canvas_push(canvas, shape);
    tvg_canvas_draw(canvas, true);
    tvg_canvas_sync(canvas);

    // 5. Extract grayscale from alpha channel → 8-bit bitmap
    GlyphBitmap* bmp = (GlyphBitmap*)arena_alloc(arena, sizeof(GlyphBitmap));
    bmp->data = (uint8_t*)arena_alloc(arena, px_w * px_h);
    for (int i = 0; i < px_w * px_h; i++) {
        bmp->data[i] = (buffer[i] >> 24) & 0xFF;  // alpha channel
    }
    bmp->width = px_w;
    bmp->height = px_h;
    bmp->pitch = px_w;
    bmp->bearing_x = (int)floorf(outline.x_min * scale);
    bmp->bearing_y = (int)ceilf(outline.y_max * scale);
    bmp->pixel_mode = GLYPH_PIXEL_GRAY;

    tvg_canvas_destroy(canvas);
    return bmp;
}
```

#### 7.3 Why ThorVG Over stb_truetype

| | ThorVG | stb_truetype |
|---|---|---|
| Already linked | **Yes** (used for SVG/wavy lines) | No (new dependency) |
| Anti-aliasing quality | High (analytic AA, same as SVG paths) | Medium (coverage-based) |
| Color emoji | COLR/CPAL possible via layered shapes | Not supported |
| Code needed | ~400 lines (outline→shape conversion) | ~300 lines (simpler API) |
| Glyph metrics | FontTables (same either way) | stb has its own parser |
| Binary size impact | 0 (already linked) | +30KB |

ThorVG is the better choice because it adds **zero new dependencies**, produces
**higher quality anti-aliased output** (the same rasterizer used for SVG rendering),
and can potentially handle **COLR color glyphs** via layered shapes.

#### 7.4 Color Emoji on Linux

Two emoji table formats to support:

1. **CBDT/sbix** — pre-rendered PNG bitmaps. Parse the strike header, find the
   glyph's PNG blob, decode with libpng (already linked). ~200 lines of table
   parsing code. No ThorVG needed.
2. **COLR/CPAL** — vector color glyphs (layered outlines with a color palette).
   Each color layer is a separate glyph outline rendered with a palette color.
   Can reuse the `font_glyf_get_outline()` + ThorVG shape pipeline, rendering each
   layer with `tvg_shape_set_fill_color(r, g, b, a)` from the CPAL palette.
   ~300 lines for COLR v0 (simple layers). COLR v1 (gradients, compositing) can
   be deferred.

#### 7.5 Variable Fonts on Linux

ThorVG does not provide variable font interpolation through its public API.
Options:

1. **Defer** — load variable fonts at their default instance. Most system fonts on
   Linux ship as separate static files (Regular.ttf, Bold.ttf). Variable web fonts
   are less common. This is the pragmatic starting point.
2. **FontTables `fvar` + `gvar` parsing** — read axis values and apply gvar deltas
   to `glyf` control points before outline extraction. Complex (~500 lines) but
   covers the common case (weight/opsz variations). Future enhancement.

Recommended: Option 1 for initial phase, option 2 as future enhancement.

#### 7.6 Font Discovery on Linux

Currently uses directory scanning (`font_database.c`). No FreeType involved —
already FT-free. No changes needed.

**Estimated changes**: ~900 lines new (`font_glyf.c` ~500 + `font_rasterize_tvg.cpp`
~400). No new dependencies. Conditional compilation via `#ifndef __APPLE__` and
`#ifndef _WIN32`.

**Validation**: Linux CI build + `make test-radiant-baseline` on Linux.

---

### Phase 8: Windows — DirectWrite Rasterizer

**New file**: `lib/font/font_rasterize_dw.c`

#### 8.1 DirectWrite Integration

Create `font_rasterize_dw.c` with the same 3-function interface:

```c
void* font_rasterize_dw_create(const uint8_t* data, size_t len,
                                float size_px, int face_index);
bool  font_rasterize_dw_metrics(void* dw_ref, uint32_t codepoint,
                                 float bitmap_scale, GlyphInfo* out);
GlyphBitmap* font_rasterize_dw_render(void* dw_ref, uint32_t codepoint,
                                       float bitmap_scale, Arena* arena,
                                       GlyphRenderMode mode);
```

#### 8.2 Key DirectWrite APIs

```c
// Font loading from memory
IDWriteFactory5* factory;
DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory5), &factory);

// Create font face from raw data
IDWriteFontFileStream* stream;  // custom implementation wrapping our buffer
IDWriteFontFile* file;
factory->CreateCustomFontFileReference(data, len, loader, &file);
IDWriteFontFace* face;
factory->CreateFontFace(DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, &file, 0,
                        DWRITE_FONT_SIMULATIONS_NONE, &face);

// Glyph metrics
UINT16 glyph_index;
face->GetGlyphIndices(&codepoint, 1, &glyph_index);
DWRITE_GLYPH_METRICS gm;
face->GetDesignGlyphMetrics(&glyph_index, 1, &gm, FALSE);

// Rasterization
IDWriteBitmapRenderTarget* target;
factory->CreateMonitorRenderingParams(monitor, &params);
gdi_interop->CreateBitmapRenderTarget(NULL, w, h, &target);
target->DrawGlyphRun(x, y, DWRITE_MEASURING_MODE_NATURAL, &run, params, RGB(0,0,0), NULL);

// Variable font axes
IDWriteFontFace5* face5;
face->QueryInterface(__uuidof(IDWriteFontFace5), &face5);
DWRITE_FONT_AXIS_VALUE axis_values[] = {
    { DWRITE_FONT_AXIS_TAG_OPTICAL_SIZE, opsz },
    { DWRITE_FONT_AXIS_TAG_WEIGHT, wght }
};
IDWriteFontResource* resource;
face5->GetFontResource(&resource);
resource->CreateFontFace(DWRITE_FONT_SIMULATIONS_NONE, axis_values, 2, &var_face);
```

#### 8.3 Color Emoji on Windows

DirectWrite natively supports COLR/CPAL and SVG color glyphs via
`ID2D1DeviceContext4::DrawColorBitmapGlyphRun`. This handles Windows emoji fonts
(Segoe UI Emoji) without custom parsing.

#### 8.4 Font Discovery on Windows

Currently uses Registry scanning in `font_database.c`. No FreeType involved.
No changes needed.

**Estimated changes**: ~400 lines new. Conditional compilation via `#ifdef _WIN32`.

---

### Phase 9: Build System Cleanup

#### 9.1 Remove FreeType from `build_lambda_config.json`

- Remove `freetype` library entry from top-level `libraries` array
- Remove `freetype` from `platforms.windows.libraries`
- Remove `freetype` from `platforms.linux.libraries`
- Remove FreeType include paths from `platforms.macos.includes` and `platforms.linux.includes`
- Update `brotlidec`/`brotlicommon` descriptions (no longer "FreeType dependency" — still
  needed for WOFF2)
- Update `harfbuzz` description (if retained for future shaping, clarify it's no
  longer an FreeType dependency)
- Remove `harfbuzz` + `graphite2` from Windows if only used as FreeType transitive deps
- Remove `zlib`/`bzip2` FreeType-related ordering comments

#### 9.2 Binary Size Reduction

| Library | Size (static) | Removable? |
|---------|:---:|:---:|
| libfreetype.a | 768KB | ✓ Remove |
| libharfbuzz.a | 1.2MB | ✓ Remove (no shaping needed) |
| libgraphite2.a | 120KB | ✓ Remove (HarfBuzz dep) |
| brotlidec + brotlicommon | 180KB | ✗ Keep (WOFF2 needs brotli) |
| zlib | — | ✗ Keep (used elsewhere) |
| bzip2 | — | ✗ Keep (used elsewhere) |
| **Total savings** | **~2MB** | |

#### 9.3 Dependency Graph After

```
lib/font/
├── font_context.c          — init FontTables, platform rasterizer
├── font_loader.c           — font_tables_open() + platform rasterizer create
├── font_glyph.c            — platform rasterizer metrics/render + FontTables fallback
├── font_metrics.c          — FontTables only (unchanged)
├── font_rasterize_ct.c     — macOS: CoreText (existing, promoted to primary)
├── font_rasterize_tvg.cpp  — Linux: ThorVG path rasterizer (new)
├── font_glyf.c             — TrueType glyf outline extraction (new, used by tvg)
├── font_rasterize_dw.c     — Windows: DirectWrite (new)
├── font_tables.c           — TTF/OTF binary parser (existing)
├── font_platform.c         — platform font discovery (existing)
├── font_fallback.c         — codepoint fallback chain (existing)
├── font_database.c         — system font catalog (existing, FT-free)
├── font_cache.c            — face cache (existing, FT-free)
└── font_face.c             — face descriptors (existing, FT-free)
```

No `ft2build.h`. No `libfreetype.a`. No `FT_*` calls.

---

## 5. Implementation Helpers Needed

### 5.1 FontTables Extensions

| Function | Purpose | Used By |
|----------|---------|---------|
| `font_tables_has_table(ft, tag)` | Check table presence (CBDT, COLR, sbix, fvar) | Replace `FT_FACE_FLAG_*` checks |
| `font_tables_get_face_count(data, len)` | TTC face count from header | Replace `face->num_faces` |
| `font_tables_open_face(data, len, face_index)` | Open specific TTC face by index | Replace `FT_New_Memory_Face(..., index)` |

### 5.2 Platform Rasterizer Interface

All three backends (`ct`, `stb`, `dw`) share the same 3-function interface stored
as function pointers on `FontHandle`:

```c
// In font_internal.h:
typedef void* (*RasterizerCreateFn)(const uint8_t* data, size_t len,
                                     float size_px, int face_index);
typedef bool  (*RasterizerMetricsFn)(void* ref, uint32_t codepoint,
                                     float bitmap_scale, GlyphInfo* out);
typedef GlyphBitmap* (*RasterizerRenderFn)(void* ref, uint32_t codepoint,
                                           float bitmap_scale, Arena* arena,
                                           GlyphRenderMode mode);
```

Or simply use `#ifdef __APPLE__` / `#ifdef __linux__` / `#ifdef _WIN32` guards
(simpler, matches current `font_rasterize_ct.c` pattern).

---

## 6. Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|:---:|:---:|------------|
| CT-only path misses FT edge cases | Low | Medium | FT is already fallback-only on macOS; CT has been primary for 5 phases |
| ThorVG rasterization quality gap on Linux | Low | Low | ThorVG uses same analytic AA as SVG paths; high quality output |
| glyf outline parsing edge cases | Medium | Medium | Compound glyphs, rare TrueType flags. Incremental testing against FT baseline |
| Variable fonts regress | Medium | Medium | Default to static instance initially; add fvar support incrementally |
| Color emoji broken on Linux | Low | Low | Parse CBDT PNG bitmaps directly (straightforward) |
| DirectWrite API complexity | Medium | Medium | Minimal surface needed (3 functions); well-documented API |
| Build system breakage | Low | High | Phased: macOS first (CI validates), then Linux, then Windows |

---

## 7. Phasing & Priority

| Phase | Platform | Effort | Priority |
|-------|----------|:---:|:---:|
| **6** | macOS — CT promotion | Small (mostly deletion) | **P0** — do first |
| **7** | Linux — ThorVG rasterizer | Medium (~900 lines new) | P1 |
| **8** | Windows — DirectWrite | Medium (new rasterizer) | P1 |
| **9** | Build cleanup | Small | P0 (after Phase 6–8) |

Phase 6 can ship independently. Phases 7 and 8 can be developed in parallel.
Phase 9 ships after all platforms are clean.

---

## 8. Validation Checklist

- [ ] `make build` — 0 errors, 0 FT-related warnings
- [ ] `make test-radiant-baseline` — no regressions
- [ ] `make test-lambda-baseline` — no regressions
- [ ] `grep -r "ft2build\|FT_Face\|FT_Library\|freetype" lib/ lambda/ radiant/` — zero hits
- [ ] Visual comparison: render 10 reference pages, diff against pre-change screenshots
- [ ] Binary size: measure reduction (expect ~2MB)
- [ ] `build_lambda_config.json` — no `freetype` entry
- [ ] Linux CI: build + test pass with stb_truetype
- [ ] Windows CI: build + test pass with DirectWrite
