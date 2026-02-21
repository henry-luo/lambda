# Radiant Layout Engine — CSS 2.1 Conformance Test Report

**Date:** 2025-02-21  
**Test Suite:** W3C CSS 2.1 (`make layout suite=css2.1`)  
**Engine:** Lambda CSS / Radiant  
**Platform:** macOS (darwin)

---

## 1. Overall Results

| Metric | Value |
|--------|-------|
| **Total Tests** | 9,875 (9,928 total HTML files) |
| **Executed** | 9,875 |
| **Skipped** | 53 (36 no browser reference, 17 file too large) |
| **Compared** | 8,499 (with browser reference data) |
| **Passed** | **6,161** |
| **Failed** | **2,338** |
| **Overall Pass Rate** | **72.5%** |

---

## 2. Pass Rate by CSS Area

| CSS Area | Pass | Total | Pass Rate |
|----------|------|-------|-----------|
| **Visual** (color, background, opacity, outline, cursor) | 1,218 | 1,274 | **95.6%** |
| **Box Model** (margin, padding, border, width, height, min/max) | 2,358 | 3,095 | **76.2%** |
| **Positioning** (position, top/right/bottom/left, z-index, abspos) | 409 | 553 | **74.0%** |
| **Text & Font** (font, text-*, line-height, white-space, letter/word-spacing, direction, bidi) | 548 | 791 | **69.3%** |
| **Float & Clear** | 109 | 174 | **62.6%** |
| **Table** (table, border-collapse, border-spacing, empty-cells, caption-side) | 256 | 429 | **59.7%** |
| **Display & Flow** (display, overflow, inline-block, block-in-inline) | 46 | 81 | **56.8%** |
| **Generated Content** (first-letter, first-line, content, counter, list-style) | 476 | 926 | **51.4%** |
| **Other** (selectors, cascade, at-rules, run-in, etc.) | 840 | 1,318 | **63.7%** |

---

## 3. Detailed Category Breakdown

### 3.1 High-Performing Categories (≥90%)

| Category | Pass | Total | Rate |
|----------|------|-------|------|
| ident, uri, at-rule, at-import, cascade, class-selector, comments, specificity, attribute, character-encoding, descendent-selector, escaped-ident-spaces, ignored-rules, units, media-dependency | (all) | (all) | **100%** |
| color | 167 | 168 | 99.4% |
| background-color | 161 | 162 | 99.4% |
| outline-color | 159 | 161 | 98.8% |
| outline-width | 77 | 78 | 98.7% |
| margin-top | 63 | 64 | 98.4% |
| at-charset | 59 | 60 | 98.3% |
| outline | 41 | 42 | 97.6% |
| padding-bottom | 79 | 81 | 97.5% |
| clip | 66 | 68 | 97.1% |
| border-bottom-color | 156 | 161 | 96.9% |
| border-top-color | 156 | 161 | 96.9% |
| text-decoration | 104 | 108 | 96.3% |
| background-position | 103 | 107 | 96.3% |
| outline-style | 24 | 25 | 96.0% |
| background-image | 23 | 24 | 95.8% |
| margin-left | 62 | 65 | 95.4% |
| font-size | 77 | 81 | 95.1% |
| padding-top | 77 | 81 | 95.1% |
| quotes | 34 | 36 | 94.4% |
| blocks | 15 | 16 | 93.8% |
| background | 375 | 404 | 92.8% |
| cursor | 36 | 39 | 92.3% |
| min-height | 82 | 90 | 91.1% |
| height | 82 | 91 | 90.1% |
| width | 78 | 86 | 90.7% |
| margin-bottom | 58 | 64 | 90.6% |
| border-spacing | 74 | 82 | 90.2% |

### 3.2 Mid-Range Categories (50–89%)

| Category | Pass | Total | Rate |
|----------|------|-------|------|
| background-repeat | 17 | 19 | 89.5% |
| font-style | 16 | 18 | 88.9% |
| border-left-color | 150 | 161 | 93.2% |
| border-right-color | 150 | 161 | 93.2% |
| counter | 142 | 164 | 86.6% |
| first-line | 24 | 28 | 85.7% |
| visibility | 18 | 21 | 85.7% |
| border-right-width | 65 | 76 | 85.5% |
| left | 59 | 69 | 85.5% |
| border-bottom-width | 60 | 70 | 85.7% |
| padding-left | 68 | 80 | 85.0% |
| right | 58 | 69 | 84.1% |
| bottom | 57 | 68 | 83.8% |
| border-left-width | 64 | 76 | 84.2% |
| border-top-width | 58 | 70 | 82.9% |
| max-width | 74 | 91 | 81.3% |
| display | 13 | 16 | 81.2% |
| border-bottom-style | 20 | 25 | 80.0% |
| border-top-style | 20 | 25 | 80.0% |
| caption-side | 16 | 20 | 80.0% |
| pseudo | 12 | 15 | 80.0% |
| top | 55 | 69 | 79.7% |
| block-formatting-contexts | 11 | 14 | 78.6% |
| text-indent | 63 | 81 | 77.8% |
| font-weight | 24 | 31 | 77.4% |
| position | 43 | 57 | 75.4% |
| page-break | 28 | 37 | 75.7% |
| z-index | 34 | 46 | 73.9% |
| vertical-align | 11 | 15 | 73.3% |
| word-spacing | 14 | 19 | 73.7% |
| max-height | 65 | 92 | 70.7% |
| background-attachment | 17 | 24 | 70.8% |
| content | 96 | 139 | 69.1% |
| html-attribute | 18 | 27 | 66.7% |
| containing-block | 12 | 18 | 66.7% |
| text-transform | 14 | 21 | 66.7% |
| overflow | 10 | 15 | 66.7% |
| white-space | 42 | 64 | 65.6% |
| letter-spacing | 28 | 43 | 65.1% |
| unicode-bidi | 11 | 17 | 64.7% |
| font | 42 | 65 | 64.6% |
| float | 91 | 144 | 63.2% |
| before-content-display | 11 | 18 | 61.1% |
| clear | 18 | 30 | 60.0% |
| inline-block | 15 | 27 | 55.6% |
| border | 281 | 518 | 54.2% |
| margin-right | 35 | 64 | 54.7% |
| padding-right | 43 | 80 | 53.8% |
| absolute-non-replaced | 23 | 44 | 52.3% |
| line-height | 45 | 88 | 51.1% |

### 3.3 Worst-Performing Categories (<50%)

| Category | Pass | Total | Rate | Primary Root Cause |
|----------|------|-------|------|--------------------|
| **bidi-list** | 0 | 7 | 0.0% | No BiDi support |
| **c414-flt-fit** | 0 | 7 | 0.0% | Float fit algorithm |
| **inlines** | 0 | 7 | 0.0% | Inline formatting gaps |
| **bidi-first-letter** | 0 | 6 | 0.0% | No BiDi + first-letter |
| **c548-ln-ht** | 0 | 5 | 0.0% | Line-height calc |
| **replaced-intrinsic** | 0 | 5 | 0.0% | No replaced intrinsic sizing |
| **collapsing-border-model** | 1 | 9 | 11.1% | Missing column border sources |
| **inline-replaced-width** | 1 | 8 | 12.5% | No replaced element sizing |
| **run-in-basic** | 3 | 18 | 16.7% | No run-in support |
| **bidi-box-model** | 3 | 15 | 20.0% | No BiDi support |
| **collapsing-table-borders** | 2 | 10 | 20.0% | Border-collapse incomplete |
| **abspos-width** | 1 | 5 | 20.0% | Abspos sizing equations |
| **absolute-replaced** | 14 | 63 | 22.2% | No §10.3.7/§10.6.4 equations |
| **direction** | 4 | 17 | 23.5% | No direction property |
| **border-color** | 8 | 27 | 29.6% | Border shorthand/color issues |
| **empty-cells** | 11 | 36 | 30.6% | Edge cases in hide logic |
| **font-variant** | 5 | 16 | 31.2% | Not stored in FontProp |
| **first-letter** | 147 | 448 | 32.8% | Float not applied to pseudo |
| **block-in-inline** | 8 | 23 | 34.8% | Splitting edge cases |
| **list-style** | 39 | 110 | 35.5% | Outside markers not in view tree |
| **text-align** | 12 | 32 | 37.5% | Inheritance + justify gaps |
| **margin** | 55 | 147 | 37.4% | Negative margin collapsing |
| **border-collapse** | 25 | 60 | 41.7% | Missing column/colgroup borders |
| **padding** | 14 | 31 | 45.2% | Shorthand / applies-to issues |
| **border-width** | 16 | 33 | 48.5% | Shorthand parsing edge cases |
| **font-family** | 36 | 75 | 48.0% | Missing fonts → elem=0 |
| **border-style** | 19 | 47 | 40.4% | Applies-to / shorthand |

---

## 4. Failure Pattern Analysis

| Failure Type | Count | Meaning |
|--------------|-------|---------|
| Element match = 0% | 179 | Complete structural mismatch (missing elements, wrong tree) |
| Element match 1–49% | 992 | Major layout differences (wrong sizing, positioning) |
| Element match 50–89% | 857 | Moderate differences (partial property mismatches) |
| Element match 90–99% | 78 | Minor differences (rounding, tolerance issues) |
| Text-only failures (elem 100%) | 232 | Element geometry correct, text position wrong |
| **Total failures** | **2,338** | |

The **text-only failures** (232) are predominantly in `line-height` (40), `first-letter` (69), `direction/bidi` (40), and `white-space` (13) — suggesting font metrics and text positioning are the main remaining text-level issues.

---

## 5. Root Cause Analysis

### 5.1 Structural Gaps (Features Completely Missing)

These are architectural absences that account for ~400+ test failures:

#### 5.1.1 BiDi / Direction Support (0–25% pass rate, ~80 tests)

**Status:** `direction` and `unicode-bidi` properties are **completely unimplemented**. The `TextDirection` enum exists in `view.hpp` but is only used in `FlexProp.text_direction` for flex layout.

**Impact:** All `bidi-*`, `direction-*`, `unicode-bidi-*` tests fail. Also indirectly affects `margin-right`, `padding-right`, `border-right-*` tests (which test logical-to-physical mapping in RTL context).

#### 5.1.2 Replaced Element Intrinsic Sizing (0–22%, ~80 tests)

**Status:** `intrinsic_sizing.cpp` has no algorithm for replaced elements. Doesn't read `img.naturalWidth`/`img.naturalHeight` or apply the CSS 2.1 §10.3.2/§10.6.2 constraint equations for replaced elements.

**Impact:** `replaced-intrinsic` (0%), `inline-replaced-width` (12.5%), `absolute-replaced` (22.2%), `inline-replaced-height` (33.3%), `block-replaced-height` (33.3%).

#### 5.1.3 `display: run-in` (0–25%, ~60 tests)

**Status:** `CSS_VALUE_RUN_IN` is parsed during style resolution but **no layout code handles it**. Run-in elements are treated as blocks.

**Impact:** All `run-in-*` categories fail (16.7% aggregate).

#### 5.1.4 `font-variant: small-caps` (31.2%, 16 tests)

**Status:** The CSS value is parsed but **`FontProp` has no `font_variant` field** — the value is silently discarded during style resolution. Comment in code: `"font-variant: field not yet added to FontProp"`.

### 5.2 Implementation Gaps (Feature Exists but Incomplete)

These are partially implemented features accounting for ~800+ test failures:

#### 5.2.1 `::first-letter` Float Support (32.8%, 448 tests)

**Root cause:** `create_first_letter_pseudo()` in `layout_block.cpp` hardcodes `display.outer = CSS_VALUE_INLINE` on the pseudo-element. The CSS-specified `float:left` (used in the vast majority of CSS 2.1 first-letter tests) is never applied.

**Fix needed:** After assigning `specified_style`, resolve the display/float properties and allow the first-letter pseudo to become a floated block when CSS says so. This is the **single highest-impact fix** — correctly floating first-letter would recover potentially 200+ tests.

#### 5.2.2 Margin Collapsing — Negative Margins (37.4%, 147 tests)

**Root cause:** The margin collapsing code in `layout_block.cpp` only handles positive margins. Guards check `> 0` before collapsing. Per CSS 2.1 §8.3.1:
- Two positive margins → use the larger
- Two negative margins → use the more negative (larger absolute value)
- Mixed → sum them (positive + negative)

The current code uses `min(margin_a, margin_b)` for the collapse amount, which only works for positive-positive.

#### 5.2.3 List-Style Outside Markers (35.5%, 110 tests)

**Root cause:** Outside-positioned list markers are **not created in the view tree**. The code explicitly says: `"Skipping 'outside' marker creation (should render in margin area)"`. Paint-time rendering (`render.cpp`) only draws `disc` bullets with hardcoded pixel offsets. No support for numbered markers, `circle`, `square`, or other marker types in outside position.

**Fix needed:** Create outside markers as positioned pseudo-elements in the view tree (like inside markers), positioned in the margin area. This makes them visible to layout testing and allows proper font-metric-based sizing.

#### 5.2.4 Border-Collapse — Column/Colgroup Borders (41.7%, 60 tests)

**Root cause:** The border-collapse conflict resolution in `layout_table.cpp` explicitly has a TODO: `"Add column and column group border support (CSS 2.1 §17.6.2)"`. The current code only considers cell, row, and table borders — column and colgroup borders are never candidates.

#### 5.2.5 Content Functions in Pseudo-Elements (content: 69.1%, counter integration)

**Root cause:** `layout_block.cpp` has TODOs for `counter()`, `counters()`, `attr()`, and `url()` content function types. The counter system itself (`layout_counters.cpp`) is fully implemented but its integration into `::before`/`::after` content resolution is incomplete.

#### 5.2.6 Text-Align Inheritance (37.5%, 32 tests)

**Root cause:** `text-align` resolution requires a `block` element — `if (!block) break;`. When a child inherits text-align but doesn't have `BlockProp` allocated, the value is lost. Also, HTML attribute `align="center"` isn't checked during CSS inheritance fallback.

#### 5.2.7 Font-Family Resolution (48%, 75 tests)

**Root cause:** 30 of 39 failures have element rate = 0%, suggesting the view tree is completely wrong when fonts are missing. Generic family matching is case-sensitive (`strcmp`), and the fallback chain may fail silently, producing zero-size elements.

### 5.3 Indirect / Cascading Failures

Several categories fail not due to their own feature being broken, but because of cross-cutting gaps:

| Category | Apparent Issue | Actual Root Cause |
|----------|---------------|-------------------|
| padding-right (53.8%) | RTL context tests | BiDi not implemented |
| margin-right (54.7%) | RTL context tests | BiDi not implemented |
| border-right-* (56%) | RTL context tests | BiDi not implemented |
| border-left-style (56%) | RTL tests + applies-to | BiDi + shorthand |
| border (54.2%) | Wide variety | Shorthand parsing + applies-to |

---

## 6. Enhancement Proposals

### Priority 1 — High Impact, Structural (estimated +800 tests)

| # | Enhancement | Est. Tests Recovered | Effort | Description |
|---|-------------|---------------------|--------|-------------|
| **P1.1** | **`::first-letter` float support** | **~200** | Medium | After creating the first-letter pseudo-element, resolve its `float` property from the specified style. If `float` is set, change `display.outer` to `CSS_VALUE_BLOCK` and route through float placement. This is the single highest-ROI fix in the entire test suite. |
| **P1.2** | **Negative margin collapsing** | **~60** | Low | Implement CSS 2.1 §8.3.1 rules: positive+positive → max, negative+negative → min (most negative), mixed → sum. Replace the current `min()` logic with proper three-case handling. Also affects `border` and `padding` shorthand "applies-to" tests whose geometry depends on correct margin resolution. |
| **P1.3** | **Replaced element intrinsic sizing** | **~80** | Medium | Implement CSS 2.1 §10.3.2 (inline replaced width), §10.6.2 (inline replaced height), §10.3.7 (absolute replaced width), §10.6.4 (absolute replaced height). Store `intrinsic_width`, `intrinsic_height`, `intrinsic_ratio` on the element and use the constraint equations. |
| **P1.4** | **List-style outside markers** | **~50** | Medium | Create outside markers as `::marker` pseudo-elements positioned in the parent's margin area. Position using font metrics and the spec's marker box algorithm instead of hardcoded pixel offsets. Add support for all marker types (decimal, roman, alpha). |
| **P1.5** | **Content function integration** | **~40** | Low | Wire up the existing counter system to pseudo-element content resolution. Implement `counter()`, `counters()` content types by calling `counter_get_value()` / `counter_format_value()`. Also implement `attr()` by reading DOM attributes. |
| **P1.6** | **Border-collapse column borders** | **~30** | Low | Add column and colgroup border sources to the border-collapse conflict resolution in `layout_table.cpp`. The algorithm structure is already in place — just needs column iteration added to the candidate collection phase. |

### Priority 2 — Feature Completeness (estimated +350 tests)

| # | Enhancement | Est. Tests Recovered | Effort | Description |
|---|-------------|---------------------|--------|-------------|
| **P2.1** | **`direction` and `unicode-bidi` properties** | **~80** | High | Add `direction` and `unicode-bidi` to CSS property resolution. Store in a suitable prop struct (e.g., `BlockProp` or new `BidiProp`). Apply to inline layout: reverse inline direction, swap physical margins/paddings. Implement the Unicode Bidirectional Algorithm (UAX #9) for mixed LTR/RTL text. This is high-effort but would fix all `bidi-*`, `direction-*` tests and 30+ RTL-dependent tests in margin/padding/border. |
| **P2.2** | **`font-variant: small-caps`** | **~12** | Low | Add `font_variant` field to `FontProp`. During text rendering, when `small-caps` is set, uppercase the text and render lowercase characters at reduced font size (typically 80%). |
| **P2.3** | **`display: run-in`** | **~60** | Medium | After layout determines a run-in element's next sibling is a block, reclassify the run-in as an inline element and prepend it to the sibling's inline content. CSS 2.1 §9.2.3 specifies the algorithm. Note: run-in was removed in CSS Display Level 3, so this is purely for CSS 2.1 conformance. |
| **P2.4** | **`text-align` inheritance fix** | **~15** | Low | Ensure `text-align` value is properly inherited even when the child element doesn't have `BlockProp` allocated. Use the parent's computed `text-align` rather than searching `specified_style` only. Handle HTML `align` attribute as a presentational hint. |
| **P2.5** | **Font-family fallback robustness** | **~20** | Low | Make generic family matching case-insensitive. Improve the fallback chain so that unresolved fonts produce consistent default metrics rather than zero-size elements. |
| **P2.6** | **Empty-cells edge cases** | **~15** | Low | The `empty-cells: hide` implementation exists but has edge cases. Ensure whitespace-only cells are correctly treated as empty. Verify the visibility flag is checked during both sizing and painting. |

### Priority 3 — Architectural Quality (long-term conformance)

| # | Enhancement | Impact | Description |
|---|-------------|--------|-------------|
| **P3.1** | **Z-index stacking contexts** | Correctness | Implement `StackingBox` (currently commented out in `view.hpp`). Create stacking contexts for positioned elements with `z-index != auto`, and sort rendering order. Affects `z-index` (73.9%) and visual layering correctness. |
| **P3.2** | **Block-in-inline splitting refinement** | ~15 tests | The splitting algorithm in `layout_inline.cpp` exists but has edge cases with deeply nested inlines, margin/padding continuation, and interaction with floats. |
| **P3.3** | **Fixed positioning to viewport** | Minor | Currently falls back to root element. Should use viewport as containing block. |
| **P3.4** | **Float fit algorithm** | ~10 tests | The `c414-flt-fit` tests (0%) test float-next-to-float fitting behavior. The current float placement may not handle narrow-remaining-space scenarios correctly. |
| **P3.5** | **Inline formatting context** | ~10 tests | The `inlines` (0%) category suggests missing anonymous inline box generation for mixed inline/text content in certain edge cases. |

---

## 7. Recommended Implementation Order

Based on impact-to-effort ratio:

```
Phase 1 (Quick Wins — Low Effort, High Impact):
  1. P1.2 - Negative margin collapsing
  2. P1.5 - Content function integration (counter/attr)
  3. P1.6 - Border-collapse column borders
  4. P2.2 - font-variant: small-caps
  5. P2.4 - text-align inheritance fix
  6. P2.5 - Font-family fallback robustness

Phase 2 (Core Features — Medium Effort, Highest Impact):
  7. P1.1 - ::first-letter float support         ← biggest single gain
  8. P1.3 - Replaced element intrinsic sizing
  9. P1.4 - List-style outside markers

Phase 3 (Feature Completeness — Medium-High Effort):
  10. P2.1 - Direction + BiDi (unicode-bidi)
  11. P2.3 - display: run-in

Phase 4 (Architecture — Long-term):
  12. P3.1 - Z-index stacking contexts
  13. P3.2 - Block-in-inline refinement
  14. P3.5 - Inline formatting context edge cases
```

**Estimated pass rate after Phase 1+2:** ~82–85% (from 72.5%)  
**Estimated pass rate after Phase 3:** ~88–90%  
**Estimated pass rate after Phase 4:** ~92%+

---

## 8. Appendix: Full Category Data

<details>
<summary>All categories sorted by test count (click to expand)</summary>

| Category | Pass | Fail | Total | Rate |
|----------|------|------|-------|------|
| border | 281 | 237 | 518 | 54.2% |
| first-letter | 147 | 301 | 448 | 32.8% |
| background | 375 | 29 | 404 | 92.8% |
| table | 130 | 101 | 231 | 56.3% |
| color | 167 | 1 | 168 | 99.4% |
| counter | 142 | 22 | 164 | 86.6% |
| background-color | 161 | 1 | 162 | 99.4% |
| border-bottom-color | 156 | 5 | 161 | 96.9% |
| border-left-color | 150 | 11 | 161 | 93.2% |
| border-right-color | 150 | 11 | 161 | 93.2% |
| border-top-color | 156 | 5 | 161 | 96.9% |
| outline-color | 159 | 2 | 161 | 98.8% |
| margin | 55 | 92 | 147 | 37.4% |
| float | 91 | 53 | 144 | 63.2% |
| content | 96 | 43 | 139 | 69.1% |
| list-style | 39 | 71 | 110 | 35.5% |
| text-decoration | 104 | 4 | 108 | 96.3% |
| background-position | 103 | 4 | 107 | 96.3% |
| max-height | 65 | 27 | 92 | 70.7% |
| height | 82 | 9 | 91 | 90.1% |
| max-width | 74 | 17 | 91 | 81.3% |
| min-height | 82 | 8 | 90 | 91.1% |
| line-height | 45 | 43 | 88 | 51.1% |
| width | 78 | 8 | 86 | 90.7% |
| min-width | 73 | 12 | 85 | 85.9% |
| border-spacing | 74 | 8 | 82 | 90.2% |
| font-size | 77 | 4 | 81 | 95.1% |
| padding-bottom | 79 | 2 | 81 | 97.5% |
| padding-top | 77 | 4 | 81 | 95.1% |
| text-indent | 63 | 18 | 81 | 77.8% |
| padding-left | 68 | 12 | 80 | 85.0% |
| padding-right | 43 | 37 | 80 | 53.8% |
| outline-width | 77 | 1 | 78 | 98.7% |
| border-left-width | 64 | 12 | 76 | 84.2% |
| border-right-width | 65 | 11 | 76 | 85.5% |
| font-family | 36 | 39 | 75 | 48.0% |
| border-bottom-width | 60 | 10 | 70 | 85.7% |
| border-top-width | 58 | 12 | 70 | 82.9% |
| left | 59 | 10 | 69 | 85.5% |
| right | 58 | 11 | 69 | 84.1% |
| top | 55 | 14 | 69 | 79.7% |
| bottom | 57 | 11 | 68 | 83.8% |
| clip | 66 | 2 | 68 | 97.1% |
| font | 42 | 23 | 65 | 64.6% |
| margin-left | 62 | 3 | 65 | 95.4% |
| margin-bottom | 58 | 6 | 64 | 90.6% |
| margin-right | 35 | 29 | 64 | 54.7% |
| margin-top | 63 | 1 | 64 | 98.4% |
| white-space | 42 | 22 | 64 | 65.6% |
| absolute-replaced | 14 | 49 | 63 | 22.2% |
| at-charset | 59 | 1 | 60 | 98.3% |
| border-collapse | 25 | 35 | 60 | 41.7% |
| position | 43 | 14 | 57 | 75.4% |
| border-style | 19 | 28 | 47 | 40.4% |
| z-index | 34 | 12 | 46 | 73.9% |
| absolute-non-replaced | 23 | 21 | 44 | 52.3% |
| letter-spacing | 28 | 15 | 43 | 65.1% |
| outline | 41 | 1 | 42 | 97.6% |
| cursor | 36 | 3 | 39 | 92.3% |
| page-break | 28 | 9 | 37 | 75.7% |
| empty-cells | 11 | 25 | 36 | 30.6% |
| quotes | 34 | 2 | 36 | 94.4% |
| border-width | 16 | 17 | 33 | 48.5% |
| text-align | 12 | 20 | 32 | 37.5% |
| font-weight | 24 | 7 | 31 | 77.4% |
| padding | 14 | 17 | 31 | 45.2% |
| clear | 18 | 12 | 30 | 60.0% |
| first-line | 24 | 4 | 28 | 85.7% |
| border-color | 8 | 19 | 27 | 29.6% |
| html-attribute | 18 | 9 | 27 | 66.7% |
| inline-block | 15 | 12 | 27 | 55.6% |
| abspos | 9 | 17 | 26 | 34.6% |
| border-*-style | 68 | 32 | 100 | 68.0% |
| background-attachment | 17 | 7 | 24 | 70.8% |
| background-image | 23 | 1 | 24 | 95.8% |
| block-in-inline | 8 | 15 | 23 | 34.8% |
| text-transform | 14 | 7 | 21 | 66.7% |
| visibility | 18 | 3 | 21 | 85.7% |
| caption-side | 16 | 4 | 20 | 80.0% |
| background-repeat | 17 | 2 | 19 | 89.5% |
| word-spacing | 14 | 5 | 19 | 73.7% |
| run-in-basic | 3 | 15 | 18 | 16.7% |
| font-style | 16 | 2 | 18 | 88.9% |
| direction | 4 | 13 | 17 | 23.5% |
| unicode-bidi | 11 | 6 | 17 | 64.7% |
| display | 13 | 3 | 16 | 81.2% |
| font-variant | 5 | 11 | 16 | 31.2% |

</details>
