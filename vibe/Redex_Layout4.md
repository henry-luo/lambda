# Redex Layout Phase 4 — JSON Font Metrics & Chrome Line-Height Formula

> Replacing hardcoded font width/height tables with JSON-extracted per-glyph metrics and reverse-engineering Chrome's macOS line-height computation for pixel-perfect text layout.
> 
> **Status: Phase 4 Complete — 1790/1821 baseline (98.3%), up from 1788 (98.2%)**

---

## Table of Contents

1. [Current State & Summary](#1-current-state--summary)
2. [Phase 4A: JSON Font Metrics Infrastructure — ✅ COMPLETED](#2-phase-4a-json-font-metrics-infrastructure--completed)
3. [Phase 4B: Unquoted HTML Attribute Fix — ✅ COMPLETED](#3-phase-4b-unquoted-html-attribute-fix--completed)
4. [Phase 4C: Monospace Font Mapping — ✅ COMPLETED](#4-phase-4c-monospace-font-mapping--completed)
5. [Phase 4D: Eager Font Metrics Loading — ✅ COMPLETED](#5-phase-4d-eager-font-metrics-loading--completed)
6. [Phase 4E: Text Height = Line-Height — ✅ COMPLETED](#6-phase-4e-text-height--line-height--completed)
7. [Phase 4F: Chrome macOS Line-Height Formula — ✅ COMPLETED](#7-phase-4f-chrome-macos-line-height-formula--completed)
8. [Test Results](#8-test-results)
9. [Files Modified](#9-files-modified)
10. [Architecture Notes](#10-architecture-notes)
11. [Appendix A: All Remaining Failing Tests (31)](#11-appendix-a-all-remaining-failing-tests-31-baseline)
12. [Appendix B: Progress Log](#12-appendix-b-progress-log)

---

## 1. Current State & Summary

### Phase 3 Achievement (Starting Point)

| Suite | Total | Pass | Fail | Pass Rate |
|-------|-------|------|------|-----------|
| **baseline** | 1821 | 1788 | 33 | **98.2%** |

### Phase 4 Final Status

| Suite | Total | Pass | Fail | Pass Rate | Δ from Phase 3 |
|-------|-------|------|------|-----------|-----------------|
| **baseline** | 1821 | **1790** | **31** | **98.3%** | **+2 tests** |

### Newly Passing Tests

| Test | Root Cause Fixed |
|------|-----------------|
| `line-height-test.html` | Chrome macOS line-height formula (1978 element mismatches → 0) |
| `default-line-height.html` | Same formula fix — default line-height now computed correctly |

### What Changed

Phase 4 tackled the **font metrics accuracy problem** — the hardcoded character-width ratio tables (~80 characters) and approximate line-height ratios were producing systematic measurement errors. The solution was two-fold:

1. **Horizontal accuracy (text width)**: Extract per-glyph advance widths + kerning pairs from actual font files into JSON, load them at runtime for 2000+ glyph coverage.
2. **Vertical accuracy (line-height)**: Reverse-engineer Chrome's exact macOS line-height formula including CoreText's 15% ascent boost for Times and Helvetica.

The `line-height-test.html` test was the systematic validation target — it contains 1978 element comparisons across 28 font sizes (8px–100px) for three font families (serif, sans-serif, monospace). All 1978 now match.

---

## 2. Phase 4A: JSON Font Metrics Infrastructure — ✅ COMPLETED

**Impact: Foundation for all subsequent fixes. Eliminated ~0.025em uppercase kerning hack.**
**Status: Implemented and verified**

### Problem

The old `times-char-widths` and `arial-char-widths` hash tables in `reference-import.rkt` covered only ~80 characters each with manually-calibrated width ratios. This caused:
- Systematic text width drift on strings with uncovered characters
- Need for crude hacks like the `-0.025` uppercase kerning adjustment for Times
- No kerning pair support — inter-character spacing was ignored

### Solution

A three-stage pipeline:

#### Stage 1: Python Font Extraction (`utils/extract_font_metrics.py`)

Reads TrueType/OpenType font files using the `fonttools` library and outputs JSON:

```json
{
  "font_name": "Times New Roman",
  "font_style": "Regular",
  "units_per_em": 2048,
  "ascender_ratio": 0.891,
  "descender_ratio": 0.216,
  "line_height_ratio": 1.107,
  "char_width_ratios": { "32": 0.25, "65": 0.722, ... },
  "kern_pairs": { "65,86": -0.0537, ... }
}
```

Fields extracted:
- `units_per_em`: font design units (typically 2048 for TrueType)
- `ascender_ratio`, `descender_ratio`: hhea table values / unitsPerEm
- `line_height_ratio`: `max(hhea_asc + |hhea_desc| + lineGap, win_asc + win_desc) / unitsPerEm`
- `char_width_ratios`: per-codepoint advance width / unitsPerEm (2000+ entries)
- `kern_pairs`: GPOS/kern pair adjustments / unitsPerEm

#### Stage 2: JSON Font Metrics Files (`test/redex/font-metrics/`)

19 JSON files generated for all target fonts:

```
Arial-Bold.json              LiberationSans-Regular.json
Arial-Regular.json           LiberationSerif-Bold.json
Menlo-Regular.json           LiberationSerif-Regular.json
OpenSans-Bold.json           Roboto-Bold.json
OpenSans-Regular.json        Roboto-Regular.json
TimesNewRoman-Bold.json      ahem-metrics.json
TimesNewRoman-Regular.json   ...
```

System fonts (Times New Roman, Arial, Menlo) take priority over Liberation fallbacks.

#### Stage 3: Racket Loader (`test/redex/font-metrics.rkt`)

The `font-metrics.rkt` module loads JSON at startup and provides:

- **`measure-text-with-metrics`**: Full text width using per-glyph advances + kerning
- **`char-width-from-metrics`**: Single character width lookup
- **`make-char-width-fn`**: Returns a `(lambda (ch font-size) -> px)` for layout-dispatch word wrapping
- **`font-line-height-ratio`**: Chrome-compatible normal line-height / font-size
- **`font-ascender-ratio`**, **`font-descender-ratio`**: Vertical metrics

Font registry uses priority-based key mapping:
- `'times` → Times New Roman (or Liberation Serif fallback)
- `'arial` → Arial (or Liberation Sans fallback)
- `'mono` → Menlo (or Liberation Mono fallback)

The old `-0.025` uppercase kerning hack for Times was **removed** — real kerning pairs from the font now handle this naturally.

---

## 3. Phase 4B: Unquoted HTML Attribute Fix — ✅ COMPLETED

**Impact: 1978 → 276 mismatches on line-height-test.html (many elements were invisible due to missed style/class attributes)**
**Status: Implemented and verified**

### Problem

`reference-import.rkt` parsed HTML attributes using only double-quoted patterns:

```racket
(regexp-match #rx"id=\"([^\"]+)\"" attrs-str)
```

But `line-height-test.html` uses unquoted attributes:

```html
<span class=s style="font-size:8px">Text</span>
```

The regex `class="([^"]+)"` fails to match `class=s`, causing elements to lose their class (and thus their CSS rules), leading to wrong styles and massive layout mismatches.

### Solution

Added a unified `html-attr` helper function that handles three attribute value formats:

```racket
(define (html-attr attrs-str attr-name)
  (define quoted-rx (regexp (string-append attr-name "=\"([^\"]+)\"")))
  (define single-rx (regexp (string-append attr-name "='([^']+)'")))
  (define unquoted-rx (regexp (string-append attr-name "=([^ \t\n\r>\"']+)")))
  (define m (or (regexp-match quoted-rx attrs-str)
                (regexp-match single-rx attrs-str)
                (regexp-match unquoted-rx attrs-str)))
  (and m (cadr m)))
```

Updated **9 attribute extraction sites** across `reference-import.rkt`:
- `body-id`, `body-class` (2 sites in `html-file->inline-styles` and `reference->box-tree`)
- `elem-id`, `elem-class`, `style`, `width`, `height` in `parse-elements` (2 call sites for element parsing)

---

## 4. Phase 4C: Monospace Font Mapping — ✅ COMPLETED

**Impact: Correct metrics for monospace/courier font-family elements**
**Status: Implemented and verified**

### Problem

`font-metrics-sym` was a 2-way switch: `'times` (serif/normal) or `'arial` (sans-serif/bold). Elements with `font-family: monospace` or `font-family: courier` were incorrectly mapped to `'times`, getting wrong glyph widths and line-height ratios.

### Solution

Added `'mono` as a third font-metrics category with priority detection:

```racket
(define use-mono-metrics?
  (and font-family-val
       (or (regexp-match? #rx"(?i:monospace)" font-family-val)
           (regexp-match? #rx"(?i:courier)" font-family-val))))
(define use-arial-metrics?
  (and (not use-mono-metrics?)
       (or (eq? font-weight-val 'bold)
           (and font-family-val
                (or (regexp-match? #rx"(?i:sans-serif)" font-family-val)
                    (regexp-match? #rx"(?i:arial)" font-family-val)
                    (regexp-match? #rx"(?i:helvetica)" font-family-val))))))
(define font-metrics-sym (cond [use-mono-metrics? 'mono]
                                [use-arial-metrics? 'arial]
                                [else 'times]))
```

Applied in **both** text node construction blocks in `reference-import.rkt` (inline text and block text paths).

---

## 5. Phase 4D: Eager Font Metrics Loading — ✅ COMPLETED

**Impact: Fixed race condition where text measurement used fallback ratios instead of JSON metrics**
**Status: Implemented and verified**

### Problem

`font-metrics.rkt` loaded JSON lazily via `load-font-metrics!` (called by `resolve-font-key`). But `measure-text-proportional` in `reference-import.rkt` used `font-metrics-loaded?` to decide between JSON and legacy paths — and it was called **before** any layout code that would trigger loading. Result: first text measurements always used old hardcoded ratios.

### Solution

Added `(load-font-metrics!)` at the top of `measure-text-proportional`:

```racket
(define (measure-text-proportional text [font-size 16] [font-family #f] [font-metrics 'times])
  (load-font-metrics!)  ;; ensure JSON metrics are loaded
  (cond
    [(font-metrics-loaded?)
     ;; Use JSON-loaded metrics (with per-glyph widths + kerning)
     ...]
    [else
     ;; Legacy fallback
     ...]))
```

The `load-font-metrics!` call is idempotent (guarded by `metrics-loaded?` flag).

---

## 6. Phase 4E: Text Height = Line-Height — ✅ COMPLETED

**Impact: 227 → 99 mismatches on line-height-test.html**
**Status: Implemented and verified**

### Problem

Chrome's `Range.getClientRects()` returns the **line-height** as the text rectangle height, not the font-size (em-box). Our layout engine was storing `font-size` as text view height and `half-leading` as the y offset:

| | Chrome getClientRects | Our engine (before) |
|---|---|---|
| **text rect height** | line-height (e.g. 22px for 18px serif) | font-size (18px) |
| **text rect y** | 0 (leading included) | half-leading (2px) |

This caused systematic height mismatches: every text element was shorter by `2 × half-leading`.

### Solution

Changed all **5** `make-text-view` calls in `layout-dispatch.rkt`:

**Before:**
```racket
(make-text-view id 0 half-leading measured-w font-size content)
```

**After:**
```racket
(make-text-view id 0 0 measured-w line-height content)
```

Also updated multi-line text height formula:

**Before:** `(+ (* (sub1 num-lines) line-height) font-size)` — mixed line-height and font-size
**After:** `(* num-lines line-height)` — pure line-height stacking (matches Chrome bounding box)

And simplified `line-contribution` in `layout-inline.rkt`:

**Before:**
```racket
(define text-half-leading (view-y laid-out))
(define line-contribution (+ ch (* 2 text-half-leading)))
```

**After:**
```racket
(define line-contribution ch)  ;; ch IS line-height now
```

Removed the now-unused `half-leading` variable from `layout-text-inner`.

---

## 7. Phase 4F: Chrome macOS Line-Height Formula — ✅ COMPLETED

**Impact: 99 → 0 mismatches on line-height-test.html. Also fixed default-line-height.html.**
**Status: Implemented and verified**

### Problem

Even with correct line-height ratios from JSON, our `round(ratio × font-size)` didn't match Chrome's per-size integer results. Example at 18px serif:

| | `round(1.107 × 18)` | Chrome actual |
|---|---|---|
| **line-height** | 20 | 22 |

The 2px gap was consistent across all sizes. Something else was going on.

### Discovery: CoreText Ascent Boost (crbug.com/445830)

Chrome on macOS uses **CoreText font metrics** (not OpenType hhea tables directly) with a platform-specific quirk: a **15% ascent boost** for fonts whose natural metrics fit within the em square.

The exact formula, reverse-engineered from 28+ data points per font family:

```
ascent_px  = floor(asc_ratio × font_size + 0.5)
descent_px = floor(desc_ratio × font_size + 0.5)

if (boost applies):
    boost = floor((ascent_px + descent_px) × 0.15 + 0.5)
    ascent_px += boost

line_height = ascent_px + descent_px
```

### Font-Specific Parameters

| Font | asc_ratio | desc_ratio | Boost? | Notes |
|------|-----------|------------|--------|-------|
| **Times** (serif) | 3/4 | 1/4 | **Yes** | CoreText ratios (not hhea 0.891/0.216) |
| **Helvetica** (sans-serif) | 1577/2048 | 471/2048 | **Yes** | Standard TrueType ratios |
| **Menlo** (monospace) | 1901/2048 | 483/2048 | **No** | Ascent already exceeds em square |

Key insight: Times uses CoreText ratios (3/4, 1/4) that differ from hhea table values (0.891, 0.216). The boost only applies to fonts where ascent + descent ≤ 1.0 em — Menlo's ratio is 1901+483 = 2384 > 2048, so it skips the boost.

### Implementation

Added `chrome-mac-line-height` to `font-metrics.rkt`:

```racket
(define (chrome-mac-line-height font-metrics-sym font-size)
  (define-values (asc-ratio desc-ratio apply-boost?)
    (case font-metrics-sym
      [(times)  (values 3/4 1/4 #t)]
      [(arial)  (values (/ 1577 2048) (/ 471 2048) #t)]
      [(mono)   (values (/ 1901 2048) (/ 483 2048) #f)]
      [else     (values 3/4 1/4 #t)]))
  (define asc-px  (inexact->exact (floor (+ (* asc-ratio font-size) 1/2))))
  (define desc-px (inexact->exact (floor (+ (* desc-ratio font-size) 1/2))))
  (define boosted-asc
    (if apply-boost?
        (+ asc-px (inexact->exact (floor (+ (* (+ asc-px desc-px) 3/20) 1/2))))
        asc-px))
  (+ boosted-asc desc-px))
```

Uses Racket exact fractions (`3/4`, `1/2`, `3/20`) to avoid floating-point rounding errors.

### Deployment Sites (4 files)

Updated all line-height computation sites to use `chrome-mac-line-height`:

1. **`layout-dispatch.rkt`**: Proportional text line-height (2 sites — explicit lh-prop and default)
2. **`layout-inline.rkt`**: `text-height-from-styles` for inline children
3. **`layout-block.rkt`**: Strut calculations (2 sites — block-in-inline struts and IFC strut descent)

---

## 8. Test Results

### line-height-test.html Progression

| Phase | Mismatches | Fix Applied |
|-------|-----------|-------------|
| Starting point | 1978 / 1978 | (all elements wrong) |
| After Fix B (html-attr) | 276 | Unquoted attributes — elements now visible |
| After Fix C (mono mapping) | 227 | Monospace elements use correct metrics |
| After Fix D (eager loading) | 227 | (no change to this test — was already loading) |
| After Fix E (height = lh) | 118 | Text height now matches Chrome's reported height |
| After Fix E cont. (line-contribution) | 118 | (inline path aligned) |
| After integer rounding | 99 | `round()` on proportional line-heights |
| **After Fix F (chrome-mac-line-height)** | **0** | **Exact Chrome formula — all 1978 elements match** |

### Full Baseline

```
1790 / 1821 passing (98.3%)
31 failing
0 regressions from Phase 3
+2 newly passing (line-height-test.html, default-line-height.html)
```

---

## 9. Files Modified

### New Files

| File | Description |
|------|-------------|
| `utils/extract_font_metrics.py` | Python font metrics extraction script |
| `test/redex/font-metrics/` (19 files) | JSON font metrics for all target fonts |

### Modified Files

| File | Changes |
|------|---------|
| `test/redex/font-metrics.rkt` | Added `chrome-mac-line-height` function (32 lines), added to `provide` |
| `test/redex/layout-dispatch.rkt` | 5 `make-text-view` calls: height → line-height, y → 0; line-height computation → `chrome-mac-line-height`; multi-line height → `n × lh`; removed `half-leading` variable |
| `test/redex/layout-inline.rkt` | `text-height-from-styles` → `chrome-mac-line-height`; `line-contribution` simplified to `ch` (removed `text-half-leading` recovery) |
| `test/redex/layout-block.rkt` | 2 strut calculation sites → `chrome-mac-line-height` (replaced `lh-ratio` × `fs`) |
| `test/redex/reference-import.rkt` | Added `html-attr` helper (12 lines); updated 9 attribute sites; added `use-mono-metrics?` detection (2 code blocks); added `(load-font-metrics!)` call in `measure-text-proportional` |

---

## 10. Architecture Notes

### Chrome macOS Line-Height vs OpenType Metrics

The key discovery of Phase 4 is that Chrome's "normal" line-height on macOS does **not** simply use the OpenType hhea table values. Instead:

1. Chrome queries **CoreText** for font metrics (macOS platform API)
2. CoreText returns ascent/descent ratios that may differ from hhea (e.g., Times: CoreText gives 3/4 + 1/4 vs hhea 0.891 + 0.216)
3. Chrome applies a **15% ascent boost** for fonts whose CoreText metrics fit within the em square (crbug.com/445830)
4. Each component (ascent, descent, boost) is **individually rounded** to integer pixels

This means `round(line-height-ratio × font-size)` ≠ Chrome's result because Chrome rounds the components separately before summing. The difference is typically 1–3px per line, compounding to large mismatches on multi-line text.

### JSON Metrics vs chrome-mac-line-height

The JSON `line_height_ratio` from OpenType hhea tables is still used as a fallback but is NOT the primary line-height source for Chrome-compatible layout. The `chrome-mac-line-height` function is authoritative for all three font families.

### Text View Contract (Post-Phase 4)

```
view-text height = line-height (not font-size)
view-text y      = 0 (not half-leading)
multi-line height = num-lines × line-height (not (n-1)×lh + fs)
line-contribution = view height directly (no half-leading recovery)
```

This matches what Chrome's `Range.getClientRects()` reports for text spans.

### Exact Fraction Arithmetic

`chrome-mac-line-height` uses Racket exact rationals (`3/4`, `1577/2048`, `3/20`) to avoid floating-point precision loss. The `floor(x + 1/2)` pattern implements round-half-up, matching Chrome's C++ `round()` behavior for positive values.

---

## 11. Appendix A: All Remaining Failing Tests (31 baseline)

### Table Layout (8 tests)

- `table-anonymous-objects-040.htm`, `-046.htm`, `-048.htm`, `-050.htm` — Anonymous table box generation (font-metrics)
- `table-layout-applies-to-016.htm` — `display:none` comparison edge case
- `table-height-algorithm-010.htm` — Multi-column content-based width (rowspan distribution)
- `table_013_html_table.html` — HTML `<table>` colspan width distribution
- `table_019_vertical_alignment.html` — VA edge cases (font-metrics)

### Float/Position (7 tests)

- `floats-001.htm`, `floats-104.htm` — Complex float stacking / text wrapping
- `floats-wrap-bfc-005-ref.htm` — BFC + float wrap interaction (font-metrics)
- `position_001_float_left.html`, `position_002_float_right.html`, `position_003_float_both.html` — Float text wrapping (requires float exclusion zones in IFC)
- `position_013_float_relative_combo.html` — Float + relative positioning

### Flex (4 tests)

- `flex_011_nested_blocks.html` — Anonymous block wrapping in flex container
- `flex_012_nested_lists.html` — Nested lists in flex (font-metrics)
- `flex_014_nested_flex.html` — `<br>` handling + font-metrics in deeply nested flex
- `flex_020_table_content.html` — Table inside flex item (font-metrics)

### Grid (2 tests)

- `grid_aspect_ratio_fill_child_max_height.html` — Requires `writing-mode: vertical-lr`
- `grid_span_2_max_content_auto_indefinite_hidden.html` — Malformed HTML

### Inline/Block Misc (2 tests)

- `before-content-display-005.htm` — `::before` with display change (font-metrics)
- `blocks-017.htm` — 4px y offset (pre-existing border-box counting)

### Box Model (2 tests)

- `box_001_width_height.html` — Width/height (font-metrics)
- `box_008_centering.html` — Centering (font-metrics)
- `box_012_overflow.html` — Overflow panels

### Font/Text (2 tests)

- `issue-font-handling.html` — Font fallback / proportional font model
- `text-transform-003.htm` — Text transform width

### List-Style (2 tests)

- `list-style-position-001.htm`, `-002.htm` — `list-style-position: inside` vs `outside`

### Other (1 test)

- `white-space-bidirectionality-001.htm` — Bidi text handling
- `table_020_overflow_wrapping.html` — Overflow in cells (font-metrics)

---

## 12. Appendix B: Progress Log

### Phase 3 Final Status

**1788/1821 baseline (98.2%)**

### Phase 4 Progress

| Step | Baseline Failures | Fix | Key Changes |
|------|-------------------|-----|-------------|
| Start | 33 | — | Phase 3 ending point |
| 4A | 33 | JSON font metrics infra | Python extractor, 19 JSON files, Racket loader |
| 4B | 33* | Unquoted HTML attrs | `html-attr` helper, 9 sites updated |
| 4C | 33* | Monospace font mapping | 3-way `font-metrics-sym` (times/arial/mono) |
| 4D | 33* | Eager metrics loading | `load-font-metrics!` in `measure-text-proportional` |
| 4E | 33* | Text height = line-height | 5 `make-text-view` calls, multi-line formula, inline contribution |
| 4F | **31** | Chrome macOS line-height | `chrome-mac-line-height` function, 4 files updated |

*Steps 4B–4E were prerequisites that made `line-height-test.html` progressively closer but didn't flip it to PASS until 4F completed the exact formula.

### Phase 4 Final

**1790/1821 baseline (98.3%), +2 tests from Phase 3, 0 regressions**
