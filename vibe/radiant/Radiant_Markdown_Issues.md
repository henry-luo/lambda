# Radiant Markdown Rendering Issues

**Date:** April 12, 2026
**Last Updated:** April 13, 2026
**Source:** Visual comparison of all 58 markdown test pages (`test/layout/data/markdown/*.html`) — Lambda render vs Chrome browser at 1200px viewport width.

---

## Fixed Issues

### ~~F1. Root Overflow Clip in PNG Auto-Sizing~~ (FIXED)
- **File:** `radiant/render_img.cpp`
- Root `<html>` element has `overflow-y:auto` with height=800px (viewport). `setup_scroller()` clips rendering at scroller clip bottom. When auto-sizing for PNG output, the surface was correctly resized to content bounds but the root scroller clip remained at 800px, making all content below 800px invisible.
- **Fix:** Extended root scroller clip to match content bounds after auto-sizing.

### ~~1. Ordered list (`<ol>`) renders as unordered list~~ (FIXED)
- **Fix:** The SVG/PDF shared render walker (`render_walk.cpp`) was missing a `case RDT_VIEW_MARKER`, causing marker nodes to be silently skipped. Added `render_marker` callback to `RenderBackend`, implemented SVG marker rendering for all marker types (disc, circle, square, decimal, roman, alpha, disclosure triangles).
- **Files:** `render_backend.h`, `render_walk.cpp`, `render_svg.cpp`

### ~~2. `<details>`/`<summary>` disclosure triangle missing~~ (FIXED)
- **Fix:** Same root cause as issue 1. Disclosure triangles (▸ closed, ▾ open) now render as SVG `<polygon>` elements.
- **Files:** Same as issue 1.

### ~~4. Italic text (`<em>`) not rendering in nested contexts~~ (VERIFIED WORKING)
- **Verification:** SVG output confirmed `font-style="italic"` is correctly applied in all contexts: inside `<li>`, inside `<td>`, and top-level. The layout engine and view tree correctly propagate italic style. The original report may have been a raster-renderer-specific issue that has since been resolved.

### ~~3. Color emoji rendered as monochrome glyphs~~ (FIXED)
- **Fix:** The raster renderer (`render.cpp`) now forces `font_load_glyph_emoji` for codepoints with `Emoji_Presentation=Yes` (Unicode 15.0, UTS #51), using a precise lookup table of ~95 BMP codepoints plus the SMP range (U+1F000–U+1FFFF). This routes them to Apple Color Emoji via `font_platform_find_emoji_font()` (CoreText `CTFontCreateForString` with VS16 appended). Layout metrics (`layout_text.cpp`) continue using the regular font path to match browser layout dimensions. SVG output (`render_svg.cpp`) also uses regular font advances since the SVG viewer handles emoji rendering.
- **Root cause:** The font database (`font_config.c`) only stores U+0020–U+007E in `unicode_ranges` for all fonts, causing `font_find_codepoint_fallback` to disqualify Apple Color Emoji for non-ASCII codepoints. Without VS16, the platform fallback picked text-presentation fonts (ZapfDingbats, Hiragino Mincho, STIXTwoMath) instead of Apple Color Emoji.
- **Files:** `render.cpp` (emoji rendering), `layout_text.cpp` (removed unused function), `render_svg.cpp` (VS16-only emoji)

### ~~5. External images show as blank~~ (VERIFIED WORKING)
- **Verification:** Tested with external badge URL (`https://img.shields.io/badge/test-passing-green`). HTTP download via `download_http_content()` (libcurl, 30s timeout, SSL) completed successfully (1091 bytes, HTTP 200). SVG output correctly included `<image>` element with base64-encoded PNG data. The external image pipeline (`surface.cpp` → `input_http.cpp` → libcurl) is fully functional.

All fixes verified against Radiant baseline test suite (4705 passed, 0 failed).

---

## Open Issues

### P3 — Cosmetic (Minor Visual Differences)

#### 8. Table cell text wrapping differences
- **Severity:** Very Low
- **Pages:** `md_uuid-readme`
- **Description:** Text in narrow table cells may wrap at slightly different word boundaries compared to the browser. For example, `[ options.random ]` — the bracket wraps to a new line in Lambda but stays on the same line in the browser.
- **Root cause:** Minor differences in font metrics or word-break calculation in constrained table column widths.

#### 9. Line height accumulation drift
- **Severity:** Very Low
- **Pages:** All 58 pages (most within 0-2%, max 4.5%)
- **Description:** Small per-line height differences (from font metrics, line-height calculation, or margin collapsing) accumulate over long pages, resulting in total page height differences of 48–168px on pages that are thousands of pixels tall.
- **Data:**
  - All 58 pages within 0–5% height difference
  - Most within 0–2%
  - Largest absolute diff: `md_uuid-readme` at +168px (1.5%)
  - Largest percentage: `md_gitmoji-readme` at 4.5% (72px on 1609px page)
  - Three pages slightly shorter: `md_index` (-17px), `md_ncu-readme` (-84px), `md_remark-readme` (-56px)

#### 10. Minor vertical offset at page top
- **Severity:** Very Low
- **Pages:** Several pages (e.g., `md_prettier-readme`)
- **Description:** Content starts at a slightly different vertical position at the top of the page compared to browser. The offset is small (a few pixels) and may be due to default body/html margin differences.

#### 11. Line wrapping position differences near page bottom
- **Severity:** Very Low
- **Pages:** `md_gitmoji-readme` and others with long flowing text
- **Description:** Near the bottom of long pages, accumulated font metric differences cause text to wrap at different positions, resulting in different number of lines. This accounts for most of the height differences observed in issue #9.

---

### Future — Deferred (Requires HarfBuzz Integration)

#### F2. Thai script combining marks incorrectly positioned
- **Pages:** `md_test-unicode`
- **Description:** Thai vowel marks (สระ) and tone marks that should be positioned above or below base consonants are instead rendered adjacent to them. Requires OpenType GPOS/GSUB shaping support (HarfBuzz).

#### F3. Hindi/Devanagari conjunct shaping minor differences
- **Pages:** `md_test-unicode`
- **Description:** Devanagari conjunct forms show minor visual differences from browser rendering. Requires OpenType GSUB tables for conjuncts (HarfBuzz).

---

## Summary Table

| #     | Issue                                        | Priority | Category         | Status       |
| ----- | -------------------------------------------- | -------- | ---------------- | ------------ |
| ~~1~~ | ~~`<ol>` renders as `<ul>` (no numbers)~~    | ~~P0~~   | List rendering   | **FIXED**    |
| ~~2~~ | ~~`<details>`/`<summary>` no triangle~~      | ~~P0~~   | Element support  | **FIXED**    |
| ~~3~~ | ~~Color emoji rendered monochrome~~           | ~~P1~~   | Font/glyph       | **FIXED**    |
| ~~4~~ | ~~`<em>` italic missing in nested contexts~~ | ~~P1~~   | Font/style       | **VERIFIED** |
| ~~5~~ | ~~External images blank~~                    | ~~P1~~   | Resource loading | **VERIFIED** |
| ~~6~~ | ~~Thai combining marks~~                     | Future   | Text shaping     | Deferred     |
| ~~7~~ | ~~Devanagari conjunct differences~~          | Future   | Text shaping     | Deferred     |
| 8     | Table cell wrapping differences              | P3       | Layout           | Open         |
| 9     | Line height accumulation drift               | P3       | Layout           | Open         |
| 10    | Minor vertical offset at top                 | P3       | Layout           | Open         |
| 11    | Line wrapping position drift                 | P3       | Layout           | Open         |
