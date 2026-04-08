# Radiant Font, Text Measurement & Text Rendering

> How the Radiant layout engine handles fonts end-to-end: resolution, metrics,
> character-by-character measurement during layout, and glyph-by-glyph
> rendering to pixel surfaces.

---

## Architecture Overview

The font system is split into two layers:

| Layer | Location | Lines | Purpose |
|-------|----------|-------|---------|
| **Font Library** | `lib/font/` (10 .c files) | ~5,300 | Font resolution, loading, metrics, glyph access. Pure C, platform-abstracted. Wraps FreeType. |
| **Radiant Integration** | `radiant/font.cpp`, `layout_text.cpp`, `render.cpp` | ~5,000 | CSS→font mapping, text layout, pixel rendering. |

The public API (`lib/font/font.h`) exposes two opaque handles — `FontContext*` and
`FontHandle*` — and never leaks FreeType types.

### Third-Party Dependencies

| Library | Role | Wrapped By |
|---------|------|------------|
| **FreeType** | Font file parsing, glyph outline loading, bitmap rasterization | `lib/font/` (all FT calls confined here) |
| **CoreText** (macOS only) | Glyph advances for system fonts, GPOS kerning, codepoint coverage, line-height metrics | `lib/font/font_platform.c` |
| **ThorVG** | SVG/PNG pixel surface rendering (text decoration wavy lines, box blur) | `radiant/render.cpp` |

---

## 1. Font Resolution

Entry point: `setup_font()` in `radiant/font.cpp`, called from `layout.cpp` when
a new font context is needed (block elements, inline spans, HTML root).

### 1.1 CSS → FontStyleDesc Mapping

`setup_font()` converts CSS properties stored in `FontProp` (from `view.hpp`) into
a `FontStyleDesc` for the font library:

```
FontProp (CSS)                  FontStyleDesc (lib/font)
─────────────────               ─────────────────────────
family: "-apple-system"    →    family: "-apple-system"
font_size: 16.0            →    size_px: 16.0
font_weight_numeric: 400   →    weight: FONT_WEIGHT_NORMAL (400)
font_style: CSS_VALUE_ITALIC →  slant: FONT_SLANT_ITALIC
```

Weight mapping uses `font_weight_numeric` (100–900) if set, otherwise maps the
`font_weight` CSS enum keyword to numeric values. This avoids the `CSS_VALUE_NORMAL
= 307` enum collision — the numeric field is always correct.

### 1.2 The `font_resolve()` Pipeline (7 Steps)

`font_resolve()` in `font_cache.c` is the single entry point. Each step tries
progressively broader strategies:

```
┌─────────────────────────────────────────────────────────┐
│ 1. Cache Lookup  "family:weight:slant:size" → FontHandle│
│    Hit → return immediately                             │
├─────────────────────────────────────────────────────────┤
│ 2. @font-face Descriptors                               │
│    CSS @font-face rules registered via font_face_register│
│    Best-match by weight distance, load from source      │
├─────────────────────────────────────────────────────────┤
│ 3. Generic Family Resolution                            │
│    "sans-serif" → [Arial, Liberation Sans, Helvetica...]│
│    "-apple-system" → [System Font, SF Pro Display, ...]  │
│    Try each candidate in database                       │
├─────────────────────────────────────────────────────────┤
│ 4. Direct Database Lookup                               │
│    font_database_find_best_match_internal()             │
│    Weighted scoring: family +40, weight +20, style +15  │
├─────────────────────────────────────────────────────────┤
│ 5. Font Alias Substitution                              │
│    "Times New Roman" → Liberation Serif (metrically     │
│    compatible open-source equivalents)                  │
├─────────────────────────────────────────────────────────┤
│ 6. Platform Fallback                                    │
│    macOS: CoreText CTFontDescriptor path lookup         │
│    Windows: Registry scan                               │
├─────────────────────────────────────────────────────────┤
│ 7. Fallback Chain                                       │
│    Walks ctx->fallback_fonts[]: Noto Color Emoji,       │
│    Apple Color Emoji, Liberation Sans, DejaVu Sans, ... │
└─────────────────────────────────────────────────────────┘
```

Every successful resolution is cached under the `"family:weight:slant:size"` key.

### 1.3 Font Database — Custom TTF Parsing (No FreeType)

The font database (`font_database.c`, 1,421 lines) discovers and catalogs system
fonts using **custom C code that reads raw TTF/OTF bytes** — FreeType is NOT used
for scanning:

- **Phase 1 (fast)**: Directory walk, create `FontEntry` placeholders. Family name
  heuristically guessed from filename (strips `-Regular`, `-Bold`, splits CamelCase).
- **Phase 2 (priority)**: Parse priority fonts first (web-safe families). Reads TTF
  `name` table (platform 1=Mac, 3=Windows) for real family/subfamily, and `OS/2`
  table for `usWeightClass` and italic `fsSelection` bit.
- **Phase 3**: Organize placeholders into family hashmaps (case-insensitive).
- **Lazy**: Remaining fonts are parsed on first access.

Results are persisted to binary disk cache (`font_cache.bin`) for fast startup.

### 1.4 Font File Loading — FreeType

Once a font file is identified, `font_loader.c` loads it:

| Format | Method |
|--------|--------|
| TTF/OTF | `FT_New_Face()` directly from file path |
| TTC (collection) | `FT_New_Face()` with `face_index` |
| WOFF/WOFF2 | Decompress to memory → `FT_New_Memory_Face()` |
| Data URI | Base64 decode → decompress → `FT_New_Memory_Face()` |

After loading:
- **Variable font axes**: Reads `FT_Get_MM_Var()`, sets `opsz` = font_size and
  `wght` = CSS weight via `FT_Set_Var_Design_Coordinates()`
- **Sizing**: `FT_Set_Pixel_Sizes()` for scalable fonts, `FT_Select_Size()` for
  bitmap/emoji (with `bitmap_scale` computed for size mismatch)
- **CoreText companion**: If the font lacks an FreeType `kern` table, creates a
  `CTFontRef` via `font_platform_create_ct_font()` for GPOS kerning access

The result is wrapped in a `FontHandle` (ref-counted, LRU-evictable).

### 1.5 FreeType Custom Memory Allocator

All FreeType heap allocations route through Lambda's `Pool` allocator via a custom
`FT_MemoryRec_`:

```c
ft_pool_alloc()   → pool_alloc()
ft_pool_free()    → pool_free()
ft_pool_realloc() → pool_realloc()
```

This keeps FreeType memory under Lambda's control — no stray malloc/free calls.

---

## 2. Font Metrics

### 2.1 Metrics Computation (`font_metrics.c`)

`font_get_metrics()` is lazy-computed on first access and cached on the FontHandle.
It populates 20+ fields from two sources:

**From FreeType:**
- **HHEA metrics**: `face->size->metrics` (26.6 fixed-point → CSS px). Ascender,
  descender, line height.
- **OS/2 table**: `FT_Get_Sfnt_Table(face, FT_SFNT_OS2)` → `sTypoAscender`,
  `sTypoDescender`, `sTypoLineGap`, `usWinAscent`, `usWinDescent`. The
  `USE_TYPO_METRICS` flag (`fsSelection` bit 7) selects typo metrics over win
  metrics.
- **Character metrics**: `FT_Load_Glyph('x')` → x-height, `FT_Load_Glyph('H')` →
  cap-height. Falls back to OS/2 `sxHeight`/`sCapHeight` or ratios of ascender.
- **Space width**: `FT_Load_Char(' ', NO_HINTING)` → `advance.x / 64 / pixel_ratio`
- **Kerning flag**: `FT_HAS_KERNING(face)`, augmented with CoreText if ct_font_ref
  exists
- **Underline**: position and thickness from FreeType face fields

**From CoreText (macOS, populating `FontProp` in `setup_font()`):**
- Space width override: `font_get_glyph(handle, ' ')` returns CT advance when
  ct_font_ref exists — replaces FreeType's space_width for system fonts

### 2.2 Line Height — Chrome-Compatible Algorithm

`font_calc_normal_line_height()` computes `line-height: normal` using a three-tier
algorithm:

| Priority | Source | Method |
|----------|--------|--------|
| 1 (macOS) | CoreText | `CTFontGetAscent()` + `CTFontGetDescent()` + `CTFontGetLeading()`, each individually rounded. Plus **15% hack** for Times/Helvetica/Courier (Chrome crbug.com/445830). |
| 2 | OS/2 `USE_TYPO_METRICS` | typo_ascender + abs(typo_descender) + line_gap, per-component rounding |
| 3 | HHEA fallback | hhea ascender + abs(hhea descender) + line_gap in font units → CSS px |

The ascender/descender split for half-leading is handled by
`font_get_normal_lh_split()`, which uses the same priority chain but returns each
component separately rather than a sum.

---

## 3. Text Measurement (Layout)

Entry point: `layout_text()` in `radiant/layout_text.cpp` (~2,295 lines).

### 3.1 Character-by-Character Loop

The core is a `do { ... } while (*str)` loop that processes one character at a
time, accumulating width and deciding on line breaks:

```
for each character in text node:
  ├─ Space?
  │   width = space_width + word_spacing + letter_spacing
  │   Tab: width = space_width × tab_size
  │
  ├─ Unicode special space? (EN SPACE, EM SPACE, THIN SPACE, etc.)
  │   width = fraction_of_em × font_size + letter_spacing
  │
  └─ Normal glyph?
      glyph = font_load_glyph(handle, codepoint, for_rendering=false)
      width = glyph->advance_x / pixel_ratio + letter_spacing
      if has_kerning: kerning = font_get_kerning(prev, current)
```

### 3.2 Glyph Advance — What Gets Called

Two functions provide glyph metrics, used in different contexts:

**`font_get_glyph()` — Fast, cached (used in lookahead):**
1. Check per-FontHandle advance cache (hashmap, max 4,096 entries)
2. `FT_Get_Char_Index()` → `FT_Load_Glyph(FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING)`
3. `advance_x = slot->advance.x / 64.0 × bitmap_scale / pixel_ratio`
4. **macOS CT override**: if ct_font_ref, `font_platform_get_glyph_advance()` → CT
   advance replaces FT advance (already in CSS px)

**`font_load_glyph()` — Full, with fallback (used in main layout loop and render):**
1. Check loaded-glyph cache (keyed by handle + codepoint + for_rendering)
2. `FT_Load_Glyph()` with mode-specific flags:
   - Layout (`for_rendering=false`): `FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING`
   - Render (`for_rendering=true`): `FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL`
3. **macOS CT override**: if ct_font_ref, override advance_x with CT advance ×
   pixel_ratio (stored as physical pixels)
4. If glyph missing from primary font: `font_find_codepoint_fallback()` tries
   fallback chain, then CoreText `CTFontCreateForString()` for codepoint coverage

### 3.3 Kerning

Applied after glyph width, between consecutive non-space characters:

```c
if (has_kerning && prev_codepoint) {
    kerning = font_get_kerning(handle, prev_codepoint, codepoint);
    // added to current rect width (or shifts rect x if first char)
}
```

`font_get_kerning()` implementation:

| Condition | Method |
|-----------|--------|
| FreeType kern table exists | `FT_Get_Kerning(FT_KERNING_DEFAULT)` → `delta.x / 64 / pixel_ratio` |
| macOS + ct_font_ref (GPOS) | Return 0 — CT per-glyph advances already include GPOS positioning |
| macOS + no kern table | `font_platform_get_pair_kerning()` via CTLine (per-pair cached, max 4,096) |

### 3.4 Lookahead — Predicting Line Overflow

Before committing text to a line, `text_has_line_filled()` scans ahead using
`font_get_glyph()` (fast, no bitmap rasterization):

```
text_has_line_filled():
  scan upcoming text character by character
  accumulate width from font_get_glyph() advances
  if width exceeds remaining line space:
    if break_opportunity exists (hyphen, soft hyphen): return NOT_SURE
    else: return LINE_FILLED
  return LINE_NOT_FILLED
```

`view_has_line_filled()` extends this across inline span boundaries, accounting for
right margin/border/padding of ancestor spans.

### 3.5 TextRect Creation

When text is committed to a line, `output_text()` creates a `TextRect`:

```
TextRect {
  x, y:     float  — position in CSS pixels (relative to containing block)
  width:    float  — accumulated glyph advances + spacing
  height:   float  — font_get_cell_height(handle) (ascender + descender)
  start_index, length: int — byte range into the text node's string
  next:     TextRect* — linked list for multi-line text
}
```

The height is the **font cell height** (ascender + descender), NOT the line height.
Line height affects vertical spacing between lines via `line_break()`, not the rect
height itself.

### 3.6 Line Height in Layout

`line_break()` computes the vertical advance:

```
font_line_height = max_ascender + max_descender  (from all fonts on this line)
css_line_height  = block.line_height              (from CSS)
used_line_height = max(css_line_height, font_line_height)
if normal: used = max(used, max_normal_line_height)   (platform-aware)
advance_y += used_line_height
```

Half-leading model: `half_leading = (line_height - cell_height) / 2`, split equally
above and below the content area, applied in `output_text()`.

---

## 4. Text Rendering

Entry point: `render_text_view()` in `radiant/render.cpp` (~200 lines of glyph
rendering logic per TextRect).

### 4.1 Overview

Rendering traverses the view tree and for each `ViewText`, iterates its linked list
of `TextRect` nodes. Each TextRect's characters are rendered glyph-by-glyph at
physical-pixel coordinates.

```
render_text_view(ViewText):
  s = render_context.scale          // CSS px → physical px
  for each TextRect:
    x = block.x + rect.x × s
    y = block.y + rect.y × s
    ├─ Phase 1: Scan for natural width (justify calculation)
    ├─ Phase 2: Shadow blur pre-pass (if text-shadow has blur)
    ├─ Phase 3: Main glyph loop
    └─ Phase 4: Text decorations (underline, overline, line-through)
```

### 4.2 Justify Calculation (Phase 1)

Scans the entire TextRect to compute natural width and space count:

```c
while (scan < end) {
    if space: natural_width += scaled_space_width; space_count++;
    else:     glyph = font_load_glyph(handle, cp, false);  // measurement mode
              natural_width += glyph->advance_x;
}
if (text_align == JUSTIFY && space_count > 0) {
    extra = (rect.width × s) - natural_width;
    space_width += extra / space_count;
}
```

### 4.3 Main Glyph Loop (Phase 3)

```
for each character in TextRect:
  ├─ Space?
  │   draw selection background if selected
  │   x += space_width (adjusted for justify)
  │
  └─ Non-space?
      apply text-transform (uppercase/capitalize)
      glyph = font_load_glyph(handle, codepoint, for_rendering=true)
      ├─ glyph found:
      │   ascend = hhea_ascender × scale
      │   draw_glyph(bitmap, x + bearing_x, y + ascend - bearing_y)
      │   if text-shadow (no blur): draw shadow glyph at offset
      │   if selected: fill selection background rect
      │   x += glyph->advance_x    ← physical pixels
      │
      └─ glyph missing:
          draw red replacement rectangle
```

### 4.4 FreeType vs CoreText in Rendering

**FreeType provides:**
- Glyph bitmap rasterization (`FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL`)
- Bitmap buffer, dimensions, bearing offsets

**CoreText provides (macOS, via font_load_glyph):**
- `advance_x` override for the system font (SFNS) — ensures rendered glyph
  positions match the layout measurements

This is critical: both layout and rendering must use the **same advance_x** source.
Prior to a recent fix, layout used CT advances but rendering used FT advances,
causing rendered text to spread wider than its measured width — producing visible
overlap at inline element boundaries. Now both paths use CT advances when available.

### 4.5 Text Decoration (Phase 4)

Drawn after all glyphs in a TextRect via `fill_surface_rect()` or ThorVG shapes:

| Decoration | Y Position |
|------------|------------|
| underline | rect_y + ascender - underline_position + underline_offset |
| overline | rect_y |
| line-through | rect_y + height/2 |

Styles: solid (filled rect), dashed (3× thickness segments), dotted (1× segments),
double (two 1/3-thickness lines), wavy (ThorVG cubic bezier path).

### 4.6 Inline Element Backgrounds

For text inside inline elements (`<a>`, `<code>`, `<em>`, etc.), the background
color is rendered per-TextRect:

```c
if (bg_color) {
    Rect bg_rect = {x, y, text_rect->width × s, text_rect->height × s};
    fill_surface_rect(surface, &bg_rect, bg_color, &clip);
}
```

This happens before glyph rendering so text draws on top of the background.

---

## 5. Caching Architecture

Five layers of caching eliminate redundant work:

| Cache | Scope | Key | Value | Max Size |
|-------|-------|-----|-------|----------|
| **Face cache** | FontContext | `"family:weight:slant:size"` | FontHandle* | 64 (LRU eviction) |
| **Advance cache** | per FontHandle | codepoint | advance_x | 4,096 (clear-on-full) |
| **Kern pair cache** | per FontHandle | (left_cp, right_cp) | kerning delta | 4,096 (clear-on-full) |
| **Loaded glyph cache** | FontContext | (handle, cp, for_render) | LoadedGlyph | 16,384 (clear-on-full) |
| **Codepoint fallback** | FontContext | codepoint | FontHandle* or NULL | unbounded (negative caching) |
| **Font DB disk cache** | persistent | — | binary `font_cache.bin` | all scanned fonts |

Between documents (batch mode), `font_context_reset_document_fonts()` clears
@font-face descriptors, codepoint fallback cache, and face cache — but keeps the
system font database.

---

## 6. Platform-Specific Behavior

### macOS (CoreText Integration)

CoreText is used in five specific places, all contained in `font_platform.c`:

| Use | API | Purpose |
|-----|-----|---------|
| System font creation | `CTFontCreateUIFontForLanguage()` | Creates SFNS at correct optical size/weight |
| Glyph advance | `CTFontGetAdvancesForGlyphs()` | Matches Chrome for system font text width |
| Pair kerning | `CTLineCreateWithAttributedString()` | GPOS kern access for fonts without FreeType kern table |
| Line height | `CTFontGetAscent/Descent/Leading()` | Chrome-compatible `line-height: normal` with 15% hack |
| Codepoint coverage | `CTFontCreateForString()` | Finds best fallback font for missing glyphs |

### Linux / Windows

No CoreText. All paths fall back to FreeType-only:
- Glyph advances from FreeType
- Kerning from FreeType `kern` table only (no GPOS)
- Line height from HHEA/Typo metrics
- Codepoint fallback from database + fallback chain (no platform assist)

---

## 7. FreeType API Usage Summary

All FreeType calls are confined to `lib/font/`. No FreeType headers are included
in `radiant/`.

### Library Lifecycle
- `FT_New_Library()`, `FT_Add_Default_Modules()`, `FT_Done_Library()`
- `FT_Library_SetLcdFilter()`

### Font Loading
- `FT_New_Face()`, `FT_New_Memory_Face()`, `FT_Done_Face()`
- `FT_Set_Pixel_Sizes()`, `FT_Select_Size()`
- `FT_Get_MM_Var()`, `FT_Set_Var_Design_Coordinates()`, `FT_Done_MM_Var()`

### Metrics
- `FT_Get_Sfnt_Table(FT_SFNT_OS2)`
- `FT_Load_Sfnt_Table()` (kern table existence check)
- `FT_HAS_KERNING()`
- `FT_Get_Postscript_Name()`

### Glyph Access
- `FT_Get_Char_Index()` — codepoint → glyph index
- `FT_Load_Glyph()` — load glyph metrics (layout) or render bitmap (render)
- `FT_Load_Char()` — load + render in one call (space width)
- `FT_Get_Kerning()` — kern pair delta

### Load Flags by Context

| Context | Flags | Purpose |
|---------|-------|---------|
| Layout measurement | `FT_LOAD_DEFAULT \| FT_LOAD_NO_HINTING \| FT_LOAD_COLOR` | Unhinted metrics match browser |
| Lookahead | `FT_LOAD_DEFAULT \| FT_LOAD_NO_HINTING \| FT_LOAD_COLOR` | Same as layout |
| Rendering | `FT_LOAD_RENDER \| FT_LOAD_TARGET_NORMAL \| FT_LOAD_COLOR` | Produces bitmap |

---

## 8. Custom Code (Not FreeType, Not CoreText)

These components are entirely custom implementations:

| Component | File | What It Does |
|-----------|------|--------------|
| Font database scanner | `font_database.c` | Reads raw TTF `name`/`OS2` tables — no FreeType |
| CSS font matching | `font_database.c` | 100-point weighted scoring algorithm |
| Generic family tables | `font_fallback.c` | Maps CSS generics to concrete font lists |
| Alias substitution | `font_fallback.c` | Metric-compatible substitutions (17 entries) |
| @font-face registry | `font_face.c` | CSS font-face descriptor storage + matching |
| WOFF/WOFF2 decompression | `font_loader.c` | Decompresses before FreeType loading |
| Variable font axis selection | `font_loader.c` | Auto-sets `opsz` and `wght` axes |
| Advance/kern/glyph caches | `font_glyph.c` | Hashmap caches per-handle and per-context |
| Unicode space widths | `layout_text.cpp` | Hardcoded fractions for EN SPACE, EM SPACE, etc. |
| Half-leading model | `layout_text.cpp` | CSS 2.1 §10.8.1 line-height half-leading |
| Justify spacing | `render.cpp` | Distributes extra space across inter-word gaps |
| Text decoration | `render.cpp` | underline/overline/line-through/wavy/dashed/dotted |
| Bitmap surface drawing | `render.cpp` | `draw_glyph()` blits FreeType bitmaps to surface |
| FreeType memory routing | `font_context.c` | Routes FT allocations through Lambda's Pool |
| Disk cache persistence | `font_database.c` | Binary serialization of font metadata |

---

## 9. Data Flow Diagram

```
CSS Properties (FontProp)
        │
        ▼
   setup_font()                    ─── radiant/font.cpp
        │
        ▼
   font_resolve()                  ─── lib/font/font_cache.c
        │  (cache → @font-face → generic → database → alias → platform → fallback)
        ▼
   font_loader: FT_New_Face()      ─── lib/font/font_loader.c
        │  (+ variable axes, sizing, CoreText companion)
        ▼
   FontHandle* (cached)
        │
   ┌────┴────────────────────────────┐
   │                                 │
   ▼                                 ▼
LAYOUT                           RENDERING
layout_text()                    render_text_view()
   │                                 │
   ├─ font_load_glyph(cp, false)     ├─ font_load_glyph(cp, true)
   │  FT_Load_Glyph (no hinting)    │  FT_Load_Glyph (+ render bitmap)
   │  + CT advance override          │  + CT advance override
   │                                 │
   ├─ font_get_kerning()             ├─ draw_glyph() → pixel surface
   │  FT_Get_Kerning / CT GPOS      │
   │                                 ├─ x += glyph->advance_x
   ├─ output_text() → TextRect      │
   │                                 ├─ text decorations
   ├─ line_break() → advance_y      │
   │                                 └─ selection highlights
   └─ TextRect chain
```

---

## 10. Source File Reference

### lib/font/ — Font Library

| File | Lines | Responsibility |
|------|-------|----------------|
| `font.h` | 250 | Public API, opaque types, enums |
| `font_internal.h` | 421 | Internal structures (FontHandle, FontContext, FontDatabase) |
| `font_context.c` | 591 | FontContext lifecycle, FreeType init, memory allocator |
| `font_cache.c` | 314 | Face cache (LRU), `font_resolve()` pipeline |
| `font_loader.c` | 403 | Font file loading (FT_New_Face, WOFF, variable fonts) |
| `font_database.c` | 1,421 | System font scanning, TTF parsing, matching algorithm |
| `font_metrics.c` | 463 | Metrics computation (HHEA, OS/2, Chrome-compatible line height) |
| `font_glyph.c` | 708 | Glyph loading, advances, kerning, fallback, caches |
| `font_platform.c` | 761 | macOS CoreText / Windows registry integration |
| `font_fallback.c` | 304 | Generic families, aliases, fallback chain |
| `font_face.c` | 352 | @font-face descriptor registry |
| `font_config.c` | ~100 | Configuration defaults |

### radiant/ — Layout & Rendering

| File | Lines | Responsibility |
|------|-------|----------------|
| `font.cpp` | 85 | `setup_font()` — CSS→FontStyleDesc, calls font_resolve |
| `layout_text.cpp` | 2,295 | Character-by-character text measurement, line breaking |
| `layout_inline.cpp` | 1,393 | Inline span bounding boxes, line-height for inline elements |
| `layout.cpp` | 2,166 | Block layout, calls setup_font, setup_line_height |
| `render.cpp` | 2,646 | Glyph rendering, decorations, shadows, selection |
| `view.hpp` | — | FontProp, TextRect, ViewText structs |
