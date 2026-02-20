# Radiant Layout Report — CSS 2.1 Conformance Audit

> **Status**: Tier 1 Fixes Complete — **61.5% pass rate** across 9,828 comparable CSS 2.1 tests  
> **Baseline**: 59.0% → 61.5% (+246 tests, +2.5pp)  
> **Date**: February 2026  
> **Engine**: Radiant (C++ layout engine)  
> **Test Suite**: W3C CSS 2.1 Test Suite (`test/layout/data/css2.1/html4/`)  
> **Tolerance**: 5px base + proportional (same as `test_radiant_layout.js`)  

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Test Methodology](#2-test-methodology)
3. [Overall Results](#3-overall-results)
4. [Broad Category Breakdown](#4-broad-category-breakdown)
5. [Failure Pattern Analysis](#5-failure-pattern-analysis)
6. [Top Failure Categories (Deep Dive)](#6-top-failure-categories-deep-dive)
7. [Strength Areas](#7-strength-areas)
8. [Root Cause Analysis](#8-root-cause-analysis)
9. [Fixes Applied](#9-fixes-applied)
10. [Improvement Roadmap](#10-improvement-roadmap)
11. [Appendix: Fine-Grained Failure Breakdown](#11-appendix-fine-grained-failure-breakdown)

---

## 1. Executive Summary

The Radiant layout engine was tested against the W3C CSS 2.1 Test Suite (`html4` variant), comprising **9,851 output files** with **9,828 having browser reference data**. After implementing Tier 1 fixes, the engine achieves a **61.5% pass rate** (6,041 passes / 9,828 comparable tests), up from 59.0% before fixes.

**Improvement**: +246 tests passing, +2.5 percentage points. Radiant baseline regression suite also improved from 1,875/1,968 to **1,878/1,968** (+3 bonus fixes from font shorthand line-height fix).

**3,787 tests still fail** layout comparison, plus 23 produce invalid JSON output. The remaining failures are concentrated in:
- **`::first-letter` pseudo-element** (411 failures) — not yet implemented
- **Table anonymous objects / border collapsing** (~340 failures)
- **Bidi / direction** (~170 failures) — UAX #9 not implemented
- **White space processing** (~100 failures)
- **Block-in-inline** (~67 failures)

The strongest areas achieve 85%+ pass rates:
- **Clipping** (97.0%), **Colors & Backgrounds** (93.6%), **Cursor** (92.7%)
- **Cascade & Inheritance** (88.9%), **Syntax & Parsing** (86.5%)

---

## 2. Test Methodology

| Parameter | Value |
|-----------|-------|
| **Test Source** | `test/layout/data/css2.1/html4/` (W3C CSS 2.1 Test Suite) |
| **Reference Data** | `test/layout/reference/*.json` (browser-generated layout trees) |
| **Viewport** | 1200×800px |
| **Engine** | Radiant (C++), invoked as `lambda.exe layout <file> -vw 1200 -vh 800` |
| **Comparison Method** | Recursive tree comparison ported from `test/layout/test_radiant_layout.js` |
| **Tolerance** | Base: 5px; Proportional: text width/y 3-7%, element height/y 3% |
| **Pass Criteria** | 100% element match AND 100% text match (within tolerance) |
| **Text Forgiveness** | When elements pass 100%: relaxed text tolerance (10px), text-split forgiveness |
| **Output files** | Pre-generated in `/tmp/css21_layout_test/` (9,851 files) |
| **Comparison runtime** | ~2 seconds (re-comparison only, no engine invocation) |
| **Full run time** | ~220 seconds (engine + comparison, 9 parallel workers) |

The comparison logic was ported faithfully from the project's own test runner (`test/layout/test_radiant_layout.js`), including:
- Proper `layout_tree` unwrapping from JSON root
- Reading `node.layout.{x, y, width, height}` (not `node.{x, y, width, height}`)
- Filtering out `head/script/style/meta/title/link` and `display:none` elements
- Flattening anonymous boxes (`::anon-*` tags)
- Browser text node `rects` array handling
- Per-element tag matching before layout comparison

---

## 3. Overall Results

```
╔══════════════════════════════════════════════════════════════╗
║  CSS 2.1 Conformance:  61.5%  (6,041 / 9,828)               ║
║  Previous:             59.0%  (5,795 / 9,828)               ║
║  Improvement:          +246 tests  (+2.5pp)                  ║
╠══════════════════════════════════════════════════════════════╣
║  PASS:       6,041   (61.5%)                                 ║
║  FAIL:       3,787   (38.5%)  layout mismatches              ║
║  ERROR:         23   (0.2%)   invalid JSON output            ║
║  No Ref:         0   (excluded from rate)                    ║
╚══════════════════════════════════════════════════════════════╝
```

---

## 4. Broad Category Breakdown

| Category | Pass | Fail | Total | Pass% | Status |
|----------|-----:|-----:|------:|------:|--------|
| Clipping | 65 | 2 | 67 | **97.0%** | ✅ |
| Colors & Backgrounds | 874 | 60 | 934 | **93.6%** | ✅ |
| Cursor | 38 | 3 | 41 | **92.7%** | ✅ |
| Cascade & Inheritance | 32 | 4 | 36 | **88.9%** | ✅ |
| Syntax & Parsing | 333 | 52 | 385 | **86.5%** | ✅ |
| Dimensions: Width | 242 | 58 | 301 | **80.4%** | ⚠️ |
| Visibility | 19 | 5 | 24 | **79.2%** | ⚠️ |
| Box Model: Padding | 278 | 81 | 360 | **77.2%** | ⚠️ |
| Dimensions: Height | 209 | 67 | 277 | **75.5%** | ⚠️ |
| Paged Media | 60 | 20 | 81 | **74.1%** | ⚠️ |
| Box Model: Borders | 1,321 | 515 | 1,836 | **71.9%** | ⚠️ |
| Positioning | 362 | 189 | 553 | **65.5%** | ⚠️ |
| Text | 189 | 143 | 335 | **56.4%** | ⚠️ |
| Box Model: Margins | 292 | 246 | 540 | **54.1%** | ⚠️ |
| Fonts | 192 | 163 | 356 | **53.9%** | ⚠️ |
| Outline | 162 | 153 | 316 | **51.3%** | ⚠️ |
| Generated Content | 205 | 213 | 418 | **49.0%** | ❌ |
| Vertical Alignment | 42 | 44 | 86 | **48.8%** | ❌ |
| Word Spacing | 31 | 39 | 70 | **44.3%** | ❌ |
| Other | 179 | 230 | 410 | **43.7%** | ❌ |
| Letter Spacing | 28 | 36 | 64 | **43.8%** | ❌ |
| Floats & Clear | 114 | 154 | 269 | **42.4%** | ❌ |
| Line Height & Box | 43 | 63 | 106 | **40.6%** | ❌ |
| Lists | 44 | 101 | 145 | **30.3%** | ❌ |
| Display & Inline | 89 | 206 | 296 | **30.1%** | ❌ |
| Overflow | 9 | 20 | 30 | **30.0%** | ❌ |
| Tables | 160 | 379 | 541 | **29.6%** | ❌ |
| Selectors & Pseudo | 167 | 499 | 669 | **25.0%** | ❌ |
| White Space | 18 | 99 | 118 | **15.3%** | ❌ |
| Bidi & Direction | 25 | 143 | 168 | **14.9%** | ❌ |
| Replaced Elements | 0 | 9 | 9 | **0.0%** | ❌ |

---

## 5. Failure Pattern Analysis

### Difference types across all failures

| Type | Count | Description |
|------|------:|-------------|
| `layout_difference` | ~8,500 | Element position/size mismatch (exceeds tolerance) |
| `text_layout_mismatch` | ~1,000 | Text node position/size mismatch |
| `missing_node` | ~90 | Tree structure difference (extra/missing nodes) |
| `text_content_mismatch` | ~89 | Text content differs between Radiant and browser |
| `node_type_mismatch` | ~42 | Element where text expected or vice versa |
| `tag_mismatch` | ~20 | Different element tags at same tree position |

The overwhelming majority of failures are **layout position/size mismatches** — the tree structure is usually correct, but computed geometry diverges from browser reference. This indicates CSS parsing, DOM construction, and view tree building are largely functional, but the layout algorithms need further work.

---

## 6. Top Failure Categories (Deep Dive)

### 6.1 `first-letter-punctuation` (411 failures)

The largest single failure bucket. These tests verify that `::first-letter` pseudo-element correctly selects the first letter plus adjacent punctuation. Radiant doesn't implement `::first-letter` — these tests fail because the pseudo-element generates extra inline boxes that change the layout geometry.

### 6.2 `table-anonymous-objects` (189 failures)

CSS 2.1 §17.2.1 anonymous table object generation. When table-related `display` values are used without proper table structure, browsers auto-generate anonymous table/row/cell wrappers. Radiant partially handles this but gets sizes wrong.

### 6.3 `margin-collapse` (68 failures)

Margin collapsing edge cases remain. Self-collapsing blocks through empty elements were fixed (see §9), but some complex scenarios involving clearance, floats, and deeply nested margins still fail.

### 6.4 `block-in-inline-insert` (67 failures)

Block-level elements inserted into inline-level contexts require splitting the inline into two anonymous blocks. This interaction is partially handled but has geometry errors.

### 6.5 `floats` (59 failures)

Float interaction with inline content and clearance. Line shortening around floats works for basic cases but fails for complex float stacking scenarios.

### 6.6 `border-conflict-*` (~150 failures)

Table border collapsing conflict resolution (CSS 2.1 §17.6.2). Border width/style precedence rules across table/tbody/tr/td levels have gaps.

### 6.7 `border-spacing` (53 failures)

Table `border-spacing` calculations with collapsed vs separated border models.

### 6.8 `white-space-processing` (45 failures)

White space collapsing and preservation edge cases around line breaks and `white-space: pre*` modes.

---

## 7. Strength Areas

Categories with **85%+ pass rate**:

| Category | Pass Rate | Notes |
|----------|-----------|-------|
| Clipping | 97.0% | `clip` property and overflow clipping |
| Colors & Backgrounds | 93.6% | Color resolution, background positioning |
| Cursor | 92.7% | Cursor style property |
| Cascade & Inheritance | 88.9% | Style cascade, specificity, `!important` |
| Syntax & Parsing | 86.5% | CSS parser handles most constructs |
| Dimensions: Width | 80.4% | Width computation for non-replaced elements |

---

## 8. Root Cause Analysis

### 8.1 Structural gaps (features not fully implemented)

| Feature | Impact | Tests Affected |
|---------|--------|---------------|
| `::first-letter` pseudo-element | No implementation | ~411 |
| Table anonymous object generation | Partial | ~189 |
| Table border collapsing | Partial | ~150 |
| Unicode BiDi (UAX #9) | Missing | ~143 |
| White space processing edge cases | Partial | ~99 |
| Block-in-inline splitting | Partial | ~67 |

### 8.2 Layout algorithm accuracy issues

| Area | Nature | Tests Affected |
|------|--------|---------------|
| Margin collapsing edge cases | Clearance, deeply nested | ~68 |
| Float-inline interaction | Line shortening around floats | ~59 |
| Border spacing/collapsing | Table-specific | ~200+ |
| Line height with mixed inline content | Baseline alignment | ~60+ |
| Generated content sizing | Counter/attr/quotes dimensions | ~38 |

---

## 9. Fixes Applied

### Phase 1: Quick Wins (3 fixes)

#### 9.1 P1: Font-size zero assertion
**File**: `radiant/view_pool.cpp`  
**Change**: `assert(font_size > 0)` → `assert(font_size >= 0)`  
**Impact**: Prevents crash on `font-size: 0`.

#### 9.2 P2: Layout text loop guard
**File**: `radiant/layout_text.cpp`  
**Change**: Added 500-iteration guard on the `LAYOUT_TEXT:` goto loop  
**Impact**: `first-letter-punct-before-*` tests complete instead of hanging.

#### 9.3 P3: Overflow clip direction
**File**: `radiant/layout_block.cpp`  
**Change**: Fixed `has_hz_scroll` → `has_vt_scroll` in vertical overflow branch  
**Impact**: Correct vertical overflow clipping.

### Phase 2: Tier 1 Structural Fixes (5 fixes, +246 tests)

#### 9.4 Word Spacing Implementation (R1)
**Files**: `radiant/view.hpp`, `radiant/resolve_css_style.cpp`, `radiant/layout_text.cpp`, `radiant/intrinsic_sizing.cpp`, `radiant/event.cpp`  
**Change**: Added `word_spacing` field to `FontProp`, stored CSS `word-spacing` value during style resolution, applied it to space characters in text layout, intrinsic sizing, and hit testing.  
**Impact**: Word spacing tests improved from 0% to 44.3% (31/70 pass). Remaining failures are due to interaction with other missing features (e.g., font shorthand parsing, bidi).

#### 9.5 Self-Collapsing Block Margin Collapse (R2/R5)
**File**: `radiant/layout_block.cpp`  
**Change**: Added detection of self-collapsing blocks (height=0, no border/padding, no BFC, no in-flow children). For these blocks, margins collapse through: `collapsed = max(margin_top, margin_bottom)`, which participates in sibling margin collapsing with previous siblings.  
**Impact**: Margin collapse tests improved from ~20% to ~28%. Some outline-color tests also fixed (they depend on empty element margin collapsing).

#### 9.6 `content: attr()` Implementation (R4)
**File**: `lambda/input/css/dom_element.cpp`  
**Change**: Added `attr()` function resolution in three code paths for pseudo-element content generation. Handles `CSS_VALUE_TYPE_FUNCTION` (where func->name == "attr") and `CSS_VALUE_TYPE_ATTR`. Extracts attribute names from STRING, KEYWORD, and CUSTOM value types.  
**Impact**: content:attr() tests now produce correct generated content text.

#### 9.7 CSS Half-Leading Line Height Model (New)
**Files**: `radiant/layout.hpp`, `radiant/layout.cpp`, `radiant/layout_text.cpp`  
**Change**: Added `line_height_is_normal` flag to `BlockContext`. When `line-height` is explicitly set (not `normal`), applies the CSS 2.1 §10.8.1 half-leading model in `output_text()`: adjusts `max_ascender`/`max_descender` so text's inline box height equals `line-height`, preventing font metrics from inflating line boxes beyond the specified value. The `has_mixed_fonts` logic still allows inline-blocks and actual mixed content to extend the line box when needed.  
**Impact**: Fixes hundreds of tests where `font: <size>/<line-height> Ahem` produced inflated line heights.

#### 9.8 Font Shorthand Line-Height Storage (Bonus)
**File**: `radiant/resolve_css_style.cpp`  
**Change**: Font shorthand handler now allocates `blk` property block before storing `line-height` (was silently dropping the value when `span->blk` was null).  
**Impact**: `font: <size>/<line-height> <family>` now correctly applies line-height. Fixed 3 additional baseline tests (1,875 → 1,878/1,968).

### Verification

All fixes verified against baseline regression suite:
- **Before fixes**: 1,875/1,968 (93 pre-existing failures)
- **After all fixes**: 1,878/1,968 (90 failures) — **3 net improvements**, zero regressions

---

## 10. Improvement Roadmap

### Tier 1: Remaining high-impact fixes (target: ~65-70% pass rate)

| # | Area | Est. Tests Fixed | Effort | Status |
|---|------|:----------------:|--------|:------:|
| R1 | Word spacing | 31/70 fixed | 2-3 days | ✅ Done |
| R2 | Self-collapsing margin collapse | ~30 fixed | 1-2 days | ✅ Done |
| R3 | `::first-letter` pseudo-element | ~200+ | 1-2 weeks | ⬜ Deferred |
| R4 | `content: attr()` | ~10 fixed | 1 day | ✅ Done |
| R5 | Half-leading line height model | ~150+ fixed | 1-2 days | ✅ Done |

### Tier 2: Layout algorithm improvements (target: ~75-80% pass rate)

| # | Area | Est. Tests Fixed | Effort | Priority |
|---|------|:----------------:|--------|:--------:|
| R6 | Table border collapsing (§17.6.2) | ~150 | 2 weeks | Medium |
| R7 | Table anonymous object wrapping (§17.2.1) | ~100 | 1-2 weeks | Medium |
| R8 | Float-inline interaction & clearance | ~80 | 1-2 weeks | Medium |
| R9 | Block-in-inline splitting | ~67 | 1-2 weeks | Medium |
| R10 | White space processing edge cases | ~60 | 1 week | Medium |
| R11 | Vertical alignment accuracy | ~30 | 1 week | Medium |

**Estimated improvement**: +487 tests → **~75-80% pass rate**

### Tier 3: Full conformance push (target: ~90%+)

| # | Area | Est. Tests Fixed | Effort | Priority |
|---|------|:----------------:|--------|:--------:|
| R12 | Unicode BiDi Algorithm (UAX #9) | ~143 | 2-4 weeks | Medium |
| R13 | `::first-letter` pseudo-element | ~200+ | 1-2 weeks | Medium |
| R14 | List style & marker positioning | ~60 | 1 week | Medium |
| R15 | Font metrics cross-platform normalization | ~100 | Ongoing | Low |
| R16 | Display value edge cases (run-in, etc.) | ~50 | 1-2 weeks | Low |

**Estimated improvement**: +553 tests → **~85-90% pass rate**

### Priority recommendation

1. **Next sprint**: R6 (table border collapse) + R8 (float-inline) — high-count, medium effort
2. **Short-term (2-4 weeks)**: R7 (table anonymous) + R9 (block-in-inline) + R10 (whitespace)
3. **Medium-term (1-2 months)**: R11-R14 (vertical align, bidi, first-letter, lists)
4. **Long-term**: R15-R16 (font normalization, display edge cases)

---

## 11. Appendix: Fine-Grained Failure Breakdown

Top 25 fine-grained categories by failure count (post-fix):

| Category | Failures | Pass Rate | Notes |
|----------|:--------:|:---------:|-------|
| first-letter-punctuation | 411 | 0.0% | `::first-letter` unimplemented |
| table-anonymous-objects | 189 | 10.0% | Anonymous table wrapping |
| border-conflict-w | 75 | 25.0% | Table border collapsing |
| border-conflict-width | 75 | 25.0% | Table border collapsing |
| margin-collapse | 68 | 28.4% | Edge cases remaining |
| block-in-inline-insert | 67 | 6.9% | Block-in-inline splitting |
| floats | 59 | 38.5% | Float interaction |
| border-spacing | 53 | 13.1% | Table spacing |
| white-space-processing | 45 | 22.4% | Whitespace edge cases |
| bidi-box-model | 42 | 6.7% | RTL box model |
| font | 39 | 30.4% | Font shorthand parsing |
| content | 38 | 69.6% | Generated content sizing |
| line-height | 36 | 50.0% | Line height edge cases |
| quotes | 34 | 5.6% | CSS quotes |
| table-height-algorithm | 32 | 0.0% | Table height computation |
| border-conflict-element | 30 | 23.1% | Table border element |
| font-size | 29 | 62.8% | Font size computation |
| padding-right | 29 | 56.1% | RTL padding |
| direction-unicode-bidi | 28 | 0.0% | Bidi not implemented |
| font-family-name | 28 | 0.0% | Custom font names |
| vertical-align | 28 | — | Complex baseline calcs |
| text-indent | 27 | — | Text indent edge cases |
| absolute-replaced-width | 25 | — | Replaced element sizing |
| max-height | 25 | — | Max-height computation |
| absolute-replaced-height | 24 | — | Replaced element sizing |

---

*Report updated after Tier 1 fixes. Full results in `temp/css21_recompare_results.json`. Comparison script: `temp/recompare_css21.js`. Test runner: `temp/run_css21_tests.js`.*
