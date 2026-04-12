# Radiant Markdown Rendering Issues

**Date:** April 12, 2026
**Last Updated:** April 12, 2026
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

All fixes verified against Radiant baseline test suite (4705 passed, 0 failed).

---

## Open Issues

### P1 — Medium Priority (Visual Correctness)

#### 3. Color emoji rendered as monochrome glyphs
- **Severity:** Medium
- **Pages:** `md_sequelize-readme`, `md_test-emoji`, and any page with emoji
- **Description:** Emoji characters that should display in color (using color font tables like COLR, CBLC, or sbix) render as black/monochrome text glyphs. The font fallback selects a text-presentation font instead of a color emoji font.
- **Examples:**
  - `❤️` (red heart) → renders as black heart ❤ (`md_sequelize-readme`)
  - `⬆️` (blue up arrow in box) → renders as plain black arrow ⬆ (`md_test-emoji`, "Dependency bump")
  - `⏭` (next track button) → renders as monochrome ⏭ (`md_test-emoji`, "Skipped" in table)
  - `⏳` (hourglass) → renders as monochrome (`md_test-emoji`, "Pending" in table)
  - `⏪` (rewind) → renders as monochrome (`md_test-emoji`, "Revert")
- **Note:** Some emoji do render with color (✅, ❌, 🐛, 📝, ⚡, 🔒, 🚀, 🎨, etc.) — these likely come from an Apple Color Emoji or similar font. The issue is specifically with emoji codepoints that have both text and emoji presentation forms and where the variation selector (VS16 `U+FE0F`) is not being honored to select emoji presentation.

---

### P1 — Medium Priority (continued)

#### 5. External images show as blank
- **Severity:** Medium
- **Pages:** `md_vue-readme` (sponsor logos), any page with remote `<img src="https://...">`
- **Description:** Images referencing external URLs (sponsor logos, some badges) render as empty/blank space. Radiant should load external images via HTTP and cache them.
- **Root cause:** TBD — external resource loading code exists but may not be wired up correctly.

---

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

| # | Issue | Priority | Category | Status |
|---|-------|----------|----------|--------|
| ~~1~~ | ~~`<ol>` renders as `<ul>` (no numbers)~~ | ~~P0~~ | List rendering | **FIXED** |
| ~~2~~ | ~~`<details>`/`<summary>` no triangle~~ | ~~P0~~ | Element support | **FIXED** |
| 3 | Color emoji rendered monochrome | P1 | Font/glyph | Open |
| ~~4~~ | ~~`<em>` italic missing in nested contexts~~ | ~~P1~~ | Font/style | **VERIFIED** |
| 5 | External images blank | P1 | Resource loading | Open |
| ~~6~~ | ~~Thai combining marks~~ | Future | Text shaping | Deferred |
| ~~7~~ | ~~Devanagari conjunct differences~~ | Future | Text shaping | Deferred |
| 8 | Table cell wrapping differences | P3 | Layout | Open |
| 9 | Line height accumulation drift | P3 | Layout | Open |
| 10 | Minor vertical offset at top | P3 | Layout | Open |
| 11 | Line wrapping position drift | P3 | Layout | Open |
