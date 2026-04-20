# Phase 6: Full Text Rendering on Linux via ThorVG

**Status:** Proposed  
**Author:** Lambda Team  
**Date:** April 2026  
**Parent Design:** `Radiant_Design_Font_Text.md` (Phases 1–5 complete)

---

## 1. Motivation

Phases 1–5 of the font migration established a 3-tier cascade (FreeType → CoreText
→ FontTables) and delivered production-quality text rendering on macOS via CoreText.
Linux still relies on FreeType for both glyph metrics and rasterization — the last
major platform without a native-quality backend.

**Why now:**

- **FreeType removal is blocked on Linux rasterization.** 79 FT call sites remain in
  `lib/font/`, nearly all exercised on Linux. Providing a ThorVG rasterizer is the
  critical path to eliminating the FreeType dependency entirely.
- **ThorVG is already linked.** It ships as a static library for SVG rendering
  (`rdt_vector_tvg.cpp`), Lottie animation, and vector operations. Adding glyph
  rasterization reuses an existing dependency with zero binary size increase.
- **FontTables already provides metrics.** `cmap`, `hmtx`, `kern`, `OS/2`, `hhea`,
  and `head` tables are parsed. Only the `glyf` outline table is missing.
- **Grayscale-only is acceptable.** Linux FreeType is already configured with
  `FT_LOAD_NO_HINTING` and grayscale rendering (no subpixel AA). ThorVG's software
  rasterizer produces equivalent grayscale output.

---

## 2. Scope

### 2.1 In Scope

- `glyf` table parser — extract TrueType quadratic Bézier contours (simple and
  compound glyphs) from raw font bytes
- `font_rasterize_tvg.cpp` — ThorVG-based glyph rasterizer producing 8-bit
  grayscale bitmaps, matching the `font_rasterize_ct.c` API shape
- Integrate into the existing 3-tier cascade as primary rasterizer on Linux,
  replacing FreeType's `FT_Load_Glyph(FT_LOAD_RENDER)`
- Glyph metrics from FontTables (`hmtx` advances, `glyf` bounding boxes) as
  primary on Linux, eliminating FreeType metric calls
- `CBDT` bitmap emoji support — decompress pre-rasterized PNG bitmaps from the
  `CBDT`/`CBLC` tables directly (no outline rendering needed)
- `COLR` v0 layered color glyphs — render each layer as a filled ThorVG shape
  with palette colors from the `CPAL` table
- Gamma-corrected rendering matching the Phase 5 CoreText pipeline
  (`γ² linearization`: `new = (old * old + 128) / 255`)

### 2.2 Out of Scope

- CFF/CFF2 outlines (PostScript cubic Bézier) — rare in system fonts, deferred
- `COLR` v1 (gradient-based color emoji) — high complexity, deferred
- Variable font axis interpolation (`gvar` table) — use default instance only;
  explicit weight files (e.g., separate `Bold.ttf`) required for now
- LCD subpixel rendering — Linux stays grayscale (matches current FreeType config)
- Hinting / grid-fitting — ThorVG renders unhinted outlines (matches
  `FT_LOAD_NO_HINTING` current behavior)
- WASM target — same architecture applies but integration deferred

---

## 3. Architecture

### 3.1 New Files

| File | Est. Lines | Purpose |
|------|-----------|---------|
| `lib/font/font_glyf.c` | ~500 | `glyf` + `loca` table parser — extracts contour points, flags, and component references |
| `lib/font/font_glyf.h` | ~60 | Public API: `GlyphOutline`, `glyf_get_outline()`, `glyf_get_bbox()` |
| `lib/font/font_rasterize_tvg.cpp` | ~400 | ThorVG rasterizer — outline → Shape → SwCanvas → grayscale bitmap |
| `lib/font/font_cbdt.c` | ~200 | CBDT/CBLC bitmap table parser — PNG extraction for color emoji |
| `lib/font/font_colr.c` | ~250 | COLR v0 + CPAL parser — layer stack rendering for color glyphs |

### 3.2 Modified Files

| File | Change |
|------|--------|
| `lib/font/font_tables.h` | Add `GlyfAccess*`, `CbdtAccess*`, `ColrAccess*` fields to `FontTables` |
| `lib/font/font_tables.c` | Add lazy-init for `glyf`/`loca`, `CBDT`/`CBLC`, `COLR`/`CPAL` table pointers |
| `lib/font/font_glyph.c` | Insert ThorVG rasterizer as Tier 2 on Linux (between FreeType and FontTables) |
| `lib/font/font_internal.h` | Add `void* tvg_canvas` field to `FontHandle` (reusable per-handle SwCanvas) |
| `lib/font/font_loader.c` | Parse `glyf`/`loca` tables when FreeType is unavailable or on Linux |
| `build_lambda_config.json` | Add new `.c`/`.cpp` files to the build |

### 3.3 Cascade After Phase 6

```
font_load_glyph(handle, codepoint, for_rendering=true)
  │
  ├─ macOS:  CoreText  → font_rasterize_ct_render()    [Phase 2, production]
  │
  ├─ Linux:  ThorVG    → font_rasterize_tvg_render()   [Phase 6, this proposal]
  │  │
  │  ├─ Simple/compound glyph  → glyf outline → ThorVG Shape → grayscale bitmap
  │  ├─ CBDT color emoji       → PNG decompress → BGRA bitmap
  │  └─ COLR v0 color glyph   → per-layer ThorVG fill → BGRA bitmap
  │
  ├─ Windows: DirectWrite → font_rasterize_dw_render()  [future]
  │
  └─ Fallback: FreeType  → FT_Load_Glyph(FT_LOAD_RENDER) [legacy, to be removed]
```

### 3.4 Metric Cascade After Phase 6 (Linux)

With ThorVG rasterization in place, glyph metrics on Linux shift to FontTables as
primary, removing the FreeType metric dependency:

```
font_get_glyph(handle, codepoint)
  │
  ├─ ASCII fast path: handle->ascii_advance[cp - 32]
  ├─ Cache hit: handle->advance_cache lookup
  │
  ├─ PRIMARY (Linux): FontTables
  │   cmap_lookup(codepoint) → glyph_id
  │   hmtx_get_advance(glyph_id) × (size_px / units_per_em)
  │
  ├─ SECONDARY (macOS): CoreText CTFontGetAdvancesForGlyphs()
  │
  └─ FALLBACK: FreeType FT_Load_Glyph() → metrics
```

---

## 4. `glyf` Table Parser Design

### 4.1 Data Structures

```c
// font_glyf.h

typedef struct {
    float x, y;
    bool  on_curve;    // true = on-curve point, false = off-curve control point
} GlyfPoint;

typedef struct {
    GlyfPoint* points;
    int         num_points;
} GlyfContour;

typedef struct {
    GlyfContour* contours;
    int           num_contours;
    int16_t       x_min, y_min, x_max, y_max;  // bounding box in design units
    bool          is_composite;
} GlyphOutline;

// Extract outline for a glyph. Handles both simple and compound glyphs.
// Returns 0 on success, -1 on error. Allocates from arena.
int glyf_get_outline(FontTables* tables, uint16_t glyph_id,
                     GlyphOutline* out, Arena* arena);
```

### 4.2 Simple Glyph Parsing

TrueType simple glyphs encode contours as:
1. `numberOfContours` (int16) — positive for simple glyphs
2. `endPtsOfContours[numberOfContours]` — last point index per contour
3. `instructionLength` + instructions (skipped — no hinting)
4. `flags[]` — per-point: on-curve, x-short, y-short, repeat, etc.
5. `xCoordinates[]`, `yCoordinates[]` — delta-encoded, variable width

The parser decodes flags and coordinates into absolute `GlyfPoint` arrays, one per
contour. Implicit on-curve midpoints (two consecutive off-curve points) are
synthesized per the TrueType spec.

### 4.3 Compound Glyph Parsing

Compound glyphs (e.g., accented characters `é` = `e` + `´`) reference other glyphs
by ID with affine transforms:

```c
typedef struct {
    uint16_t glyph_id;
    float    a, b, c, d;   // 2×2 matrix (scale, rotation)
    float    tx, ty;        // translation
} GlyfComponent;
```

The parser recursively resolves components (depth limit: 32) and flattens into a
single `GlyphOutline` with transformed point coordinates. Recursion depth limiting
prevents malicious font files from causing stack overflow.

### 4.4 `loca` Table

The `loca` (index-to-location) table maps `glyph_id → byte offset` in the `glyf`
table. Format is determined by `head.indexToLocFormat`:
- `0` = short format: `offset[i] = uint16 × 2`
- `1` = long format: `offset[i] = uint32`

Empty glyphs (e.g., space) have `offset[i] == offset[i+1]` — no outline data.

---

## 5. ThorVG Rasterizer Design

### 5.1 API

```c
// font_rasterize_tvg.h

// Create platform font reference for ThorVG rasterization.
// Called once per FontHandle on Linux. Returns opaque handle (SwCanvas*).
void* font_rasterize_tvg_create(void);

// Destroy ThorVG rasterization context.
void font_rasterize_tvg_destroy(void* tvg_ctx);

// Get glyph metrics without rasterizing (from glyf bounding box + hmtx advance).
bool font_rasterize_tvg_metrics(FontTables* tables, uint32_t codepoint,
                                float size_px, float pixel_ratio,
                                GlyphInfo* out);

// Rasterize a glyph outline to an 8-bit grayscale bitmap.
// Returns NULL if glyph has no outline (e.g., space, missing glyph).
GlyphBitmap* font_rasterize_tvg_render(void* tvg_ctx, FontTables* tables,
                                        uint32_t codepoint, float size_px,
                                        float pixel_ratio, Arena* arena);
```

### 5.2 Rendering Pipeline

```
font_rasterize_tvg_render(ctx, tables, codepoint, size_px, pixel_ratio, arena)
  │
  ├─ 1. Lookup glyph ID:  cmap_lookup(tables, codepoint) → glyph_id
  │
  ├─ 2. Check for color emoji:
  │     ├─ CBDT hit? → decompress PNG → return BGRA bitmap
  │     └─ COLR hit? → render layer stack (see §5.4)
  │
  ├─ 3. Extract outline:  glyf_get_outline(tables, glyph_id, &outline, arena)
  │
  ├─ 4. Compute pixel bounds (Skia-style roundOut + outset):
  │     scale = (size_px × pixel_ratio) / units_per_em
  │     px_left   = floor(x_min × scale) - 1
  │     px_bottom = floor(y_min × scale) - 1
  │     px_right  = ceil(x_max × scale) + 1
  │     px_top    = ceil(y_max × scale) + 1
  │     bmp_w = px_right - px_left
  │     bmp_h = px_top - px_bottom
  │
  ├─ 5. Build ThorVG shape from contours:
  │     tvg_shape = tvg_shape_new()
  │     for each contour:
  │       tvg_shape_move_to(shape, first.x × scale - px_left,
  │                                bmp_h - (first.y × scale - px_bottom))
  │       for each segment:
  │         if on-curve → on-curve:
  │           tvg_shape_line_to(shape, p.x, p.y)
  │         if off-curve (quadratic Bézier):
  │           convert to cubic: CP1 = P0 + 2/3(P1-P0)
  │                             CP2 = P2 + 2/3(P1-P2)
  │           tvg_shape_cubic_to(shape, cp1.x, cp1.y, cp2.x, cp2.y, end.x, end.y)
  │       tvg_shape_close(shape)
  │     tvg_shape_set_fill_color(shape, 255, 255, 255, 255)
  │     tvg_shape_set_fill_rule(shape, TVG_FILL_RULE_EVEN_ODD)   // nonzero for some fonts
  │
  ├─ 6. Rasterize to ABGR8888 buffer:
  │     tvg_swcanvas_set_target(canvas, pixels, bmp_w, bmp_w, bmp_h, ABGR8888)
  │     tvg_canvas_push(canvas, shape)
  │     tvg_canvas_draw(canvas)
  │     tvg_canvas_sync(canvas)
  │
  ├─ 7. Extract alpha channel → 8-bit grayscale:
  │     for each pixel: grayscale[i] = pixels[i] >> 24   // alpha byte of ABGR
  │
  ├─ 8. Apply gamma² linearization:
  │     for each pixel: g[i] = (g[i] * g[i] + 128) / 255
  │
  └─ 9. Fill GlyphBitmap and return
```

### 5.3 Quadratic-to-Cubic Bézier Conversion

TrueType outlines use quadratic Bézier curves (one control point). ThorVG's
`tvg_shape_cubic_to()` requires cubic Béziers (two control points). The standard
lossless conversion:

Given quadratic: start `P0`, control `P1`, end `P2`:
- $CP_1 = P_0 + \frac{2}{3}(P_1 - P_0)$
- $CP_2 = P_2 + \frac{2}{3}(P_1 - P_2)$

This is exact — no approximation error.

TrueType also defines **implicit on-curve points** between consecutive off-curve
points: if points `Q1` and `Q2` are both off-curve, the midpoint
`M = (Q1 + Q2) / 2` is an implicit on-curve point, splitting into two quadratic
segments: `(prev, Q1, M)` and `(M, Q2, next)`.

### 5.4 Color Emoji: CBDT Bitmaps

The `CBDT`/`CBLC` tables store pre-rasterized color emoji as embedded PNG images
at fixed strike sizes (commonly 128×128 or 136×136 pixels).

```
font_rasterize_tvg_render(... codepoint ...)
  ├─ cblc_find_strike(tables, size_px × pixel_ratio) → best strike
  ├─ cbdt_get_bitmap(tables, glyph_id, strike) → PNG data
  ├─ Decompress PNG → BGRA pixel buffer
  ├─ Scale if strike size ≠ requested size (bilinear)
  └─ Return GlyphBitmap with pixel_mode = BGRA, bitmap_scale set
```

### 5.5 Color Glyphs: COLR v0

COLR v0 defines color glyphs as ordered layer stacks. Each layer is a glyph ID +
palette index from the `CPAL` table.

```
colr_render(tables, glyph_id, tvg_canvas, scale, arena)
  ├─ colr_get_layers(tables, glyph_id) → [(glyph_id, palette_index), ...]
  ├─ for each layer bottom-to-top:
  │   ├─ glyf_get_outline(tables, layer.glyph_id, &outline, arena)
  │   ├─ Build ThorVG shape from outline
  │   ├─ color = cpal_get_color(tables, layer.palette_index)
  │   ├─ tvg_shape_set_fill_color(shape, r, g, b, a)
  │   └─ tvg_canvas_push(canvas, shape)
  └─ tvg_canvas_draw() + tvg_canvas_sync() → composited BGRA bitmap
```

---

## 6. Integration Points

### 6.1 FontHandle Changes

```c
// font_internal.h — additions

struct FontHandle {
    // ... existing fields ...

#if !defined(__APPLE__)
    void* tvg_ctx;               // ThorVG SwCanvas (reused per handle, avoids alloc per glyph)
#endif
};
```

The `tvg_ctx` is lazily created on first `font_rasterize_tvg_render()` call and
destroyed in `font_handle_release()`. The SwCanvas is reused across glyphs by
re-targeting its pixel buffer for each glyph size.

### 6.2 Glyph Loading Cascade Modification

In `font_glyph.c`, the rasterization branch for Linux changes from:

```c
// Current (Phase 5):
if (handle->ft_face) {
    FT_Load_Glyph(handle->ft_face, glyph_id, FT_LOAD_RENDER);
    // extract bitmap from FT_GlyphSlot
}
```

To:

```c
// Phase 6:
#if defined(__APPLE__)
    bitmap = font_rasterize_ct_render(handle->ct_raster_ref, codepoint, ...);
#elif defined(__linux__) || defined(__EMSCRIPTEN__)
    bitmap = font_rasterize_tvg_render(handle->tvg_ctx, handle->tables, codepoint, ...);
#else
    // Windows: DirectWrite (future), FreeType fallback
    FT_Load_Glyph(handle->ft_face, glyph_id, FT_LOAD_RENDER);
#endif
```

### 6.3 Build Integration

Add to `build_lambda_config.json` source directories — the new files live in
`lib/font/` which is already a scanned directory. No config changes needed for the
`.c` files. `font_rasterize_tvg.cpp` requires linking against the existing ThorVG
static library (already linked for `rdt_vector_tvg.cpp`).

---

## 7. Testing Strategy

### 7.1 Unit Tests

| Test | File | Coverage |
|------|------|----------|
| `glyf` simple glyph parsing | `test/test_font_glyf_gtest.cpp` | Parse known glyphs (A, g, @) from bundled test font, verify contour count and point coordinates |
| `glyf` compound glyph parsing | same | Parse accented characters (é, ñ), verify component flattening and transform application |
| `glyf` recursion depth limit | same | Malformed font with circular compound references — verify graceful failure |
| ThorVG rasterization | `test/test_font_rasterize_tvg_gtest.cpp` | Render glyphs at 12/16/24/48px, verify non-empty bitmap, correct dimensions, bearing values |
| Quadratic→cubic conversion | same | Known control points, verify cubic CPs match exact formula |
| CBDT emoji extraction | same | Extract known emoji from Noto Color Emoji, verify PNG decompression and BGRA output |
| COLR v0 layer rendering | same | Render layered glyph, verify correct color compositing |

### 7.2 Visual Regression Tests

Run the existing Radiant layout baseline suite (`make test-radiant-baseline`) on
Linux with ThorVG rasterization enabled. Expected: all 3,939 tests pass (pixel
differences from FreeType → ThorVG are acceptable as a new baseline).

Generate reference renders for a text-heavy test page at multiple sizes (10px,
14px, 16px, 24px, 48px, 72px) and compare against FreeType reference. Accept
±1 pixel differences (no hinting means outlines may snap differently).

### 7.3 Font Coverage Tests

Test against common Linux system fonts:
- **DejaVu Sans** — Latin, Cyrillic, Greek, many symbols
- **Liberation Sans/Serif/Mono** — metric-compatible with Arial/Times/Courier
- **Noto Sans** — CJK, Arabic, Hebrew, Indic scripts
- **Noto Color Emoji** — CBDT bitmap emoji
- **Twemoji** — COLR v0 layered emoji

---

## 8. Performance Considerations

### 8.1 SwCanvas Reuse

Creating and destroying a ThorVG `SwCanvas` per glyph is expensive. The design
reuses a single `SwCanvas` per `FontHandle`, re-targeting the pixel buffer for
each glyph. The canvas `clear()` + `push()` + `draw()` + `sync()` cycle is
~5–10μs per glyph on modern hardware.

### 8.2 Outline Caching

`glyf_get_outline()` parses raw font bytes on each call. For frequently used
glyphs, the parsed outline could be cached in the per-handle advance cache
(extending the cache value to include outline data). However, since the glyph
bitmap itself is cached after first render, outline parsing only occurs once per
unique `(font, glyph_id, size)` triple. Caching is deferred unless profiling shows
it as a bottleneck.

### 8.3 Expected Performance

| Operation | Est. Time | Notes |
|-----------|----------|-------|
| `glyf` outline parse | ~1–2μs | Simple glyph; compounds ~3–5μs |
| Quadratic→cubic conversion | ~0.5μs | Per contour |
| ThorVG shape build | ~1–2μs | Path construction |
| ThorVG rasterize (16px) | ~5–10μs | SwCanvas software render |
| Gamma linearization | ~0.5μs | Tight pixel loop |
| **Total per glyph** | **~10–15μs** | Comparable to FreeType (~8–12μs) |

After L1 glyph bitmap caching (existing), repeated glyphs cost ~0.1μs (cache
lookup only). Latin text has ~60 unique glyphs; a full page caches within the
first paragraph.

---

## 9. Risk Analysis

| Risk | Impact | Mitigation |
|------|--------|------------|
| `glyf` compound glyph recursion | Stack overflow on malicious fonts | Depth limit of 32; fail gracefully with empty outline |
| Quadratic→cubic visual quality | Rounding artifacts at small sizes | Exact conversion (no approximation); compare against FreeType reference |
| ThorVG fill rule mismatch | Incorrect glyph rendering (holes in o, e, etc.) | Default to even-odd; detect and switch to nonzero per font if needed |
| Missing `glyf` table (CFF fonts) | No outline data for some fonts | Fall back to FreeType if `glyf` table absent; log warning |
| Variable fonts (no `gvar` support) | Wrong weight/width for variable fonts | Use default instance; require separate weight files on Linux |
| Large emoji bitmaps (CBDT) | Memory pressure for 128×128 BGRA bitmaps | Cache at rendered size, not strike size; evict LRU |
| Color palette (CPAL) not present | COLR v0 renders without color | Fall back to black fill per layer if no CPAL |

---

## 10. Phased Delivery

### Phase 6a: `glyf` Parser + ThorVG Grayscale Rasterization

**Deliverables:**
- `lib/font/font_glyf.c` — simple + compound glyph parsing
- `lib/font/font_rasterize_tvg.cpp` — outline → ThorVG → grayscale bitmap
- Integration into `font_glyph.c` cascade on Linux
- Unit tests for outline parsing and rasterization
- Radiant baseline tests pass with ThorVG backend

**Estimated size:** ~900 lines of new code

### Phase 6b: Color Emoji Support

**Deliverables:**
- `lib/font/font_cbdt.c` — CBDT/CBLC PNG bitmap extraction
- `lib/font/font_colr.c` — COLR v0 layer stack + CPAL palette
- Color emoji rendering in ThorVG pipeline
- Tests with Noto Color Emoji and Twemoji

**Estimated size:** ~450 lines of new code

### Phase 6c: FreeType Elimination on Linux

**Deliverables:**
- Remove all `FT_*` calls from Linux code path
- Remove FreeType from Linux link libraries
- Remove `ft_face` field from `FontHandle` on Linux (`#ifndef __APPLE__` →
  `#ifdef _WIN32` only)
- Binary size reduction: ~768KB (FreeType) + ~200KB (transitive deps)

**Prerequisites:** Phase 6a + 6b complete; all baseline tests pass without FreeType

---

## 11. File Size Estimates

| Component | New Lines | Modified Lines |
|-----------|----------|---------------|
| `font_glyf.h` | 60 | — |
| `font_glyf.c` | 500 | — |
| `font_rasterize_tvg.cpp` | 400 | — |
| `font_cbdt.c` | 200 | — |
| `font_colr.c` | 250 | — |
| `font_tables.h` / `.c` | — | +40 |
| `font_glyph.c` | — | +30 |
| `font_internal.h` | — | +5 |
| `font_loader.c` | — | +15 |
| Unit tests | 400 | — |
| **Total** | **~1,810** | **~90** |
