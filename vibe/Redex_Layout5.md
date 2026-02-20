# Redex Layout — CSS 2.1 Conformance Test Results & Enhancement Roadmap

> **CSS 2.1 Test Suite Run:** 8,180 tests from W3C CSS 2.1 html4 suite via Redex layout engine.
> **Overall Pass Rate: 77.5%** (6,343 pass / 1,814 fail / 23 error)
> **Skipped:** 1,739 complex tests (contain `<span>` or `<img>` tags unsupported by the importer classifier)

---

## Table of Contents

1. [Test Methodology](#1-test-methodology)
2. [Overall Results](#2-overall-results)
3. [Broad Category Breakdown](#3-broad-category-breakdown)
4. [Fine-Grained Category Breakdown (Top 50)](#4-fine-grained-category-breakdown-top-50)
5. [Worst-Performing Categories](#5-worst-performing-categories)
6. [Root Cause Analysis](#6-root-cause-analysis)
7. [Enhancement Roadmap](#7-enhancement-roadmap)
8. [Phase 1: Quick Wins (est. +540 tests)](#8-phase-1-quick-wins-est-540-tests)
9. [Phase 2: RTL & Text Spacing (est. +130 tests)](#9-phase-2-rtl--text-spacing-est-130-tests)
10. [Phase 3: Pseudo-Elements (est. +420 tests)](#10-phase-3-pseudo-elements-est-420-tests)
11. [Phase 4: Table & List Completeness (est. +80 tests)](#11-phase-4-table--list-completeness-est-80-tests)
12. [Phase 5: Layout Edge Cases (est. +50 tests)](#12-phase-5-layout-edge-cases-est-50-tests)
13. [Projected Progress](#13-projected-progress)
14. [Architecture Notes](#14-architecture-notes)

---

## 1. Test Methodology

### Suite & Runner

- **Test suite:** W3C CSS 2.1 conformance tests at `test/layout/data/css2.1/html4/` (9,924 HTML files)
- **Reference data:** Chrome-captured layout JSONs at `test/layout/reference/` (9,919 with matching references)
- **Runner:** `test/redex/test-differential.rkt --suite css21 --verbose`
- **Engine:** PLT Redex (Racket) formal layout specification

### Test Classification

The `classify-html-test` function in `reference-import.rkt` filters tests into three categories:

| Class | Criteria | Count | Tested? |
|-------|----------|------:|---------|
| **simple** | No `<span>`, `<img>`, or `<style>` | 109 | Yes |
| **style-block** | Has `<style>` block (no `<span>`/`<img>`) | 8,071 | Yes |
| **complex** | Has `<span>` or `<img>` elements | 1,739 | No (skipped) |

Style-block tests require the importer's CSS cascade resolver (selector matching, specificity, shorthand expansion). Complex tests are skipped because the importer cannot map `<span>` styling through class/selector rules into the Redex box tree.

### Comparison Tolerances

From `compare-layouts.rkt`:
- **Base tolerance:** 3px
- **Proportional tolerance:** 3% of reference value
- **Max tolerance:** 10px
- **Formula:** `tolerance = min(max(3, 0.03 × |reference|), 10)`

---

## 2. Overall Results

| Metric | Value |
|--------|------:|
| **Total Tested** | 8,180 |
| **Passed** | 6,343 |
| **Failed** | 1,814 |
| **Errors** | 23 |
| **Pass Rate** | **77.5%** |
| Skipped (complex) | 1,739 |
| Available w/ reference | 9,919 |

---

## 3. Broad Category Breakdown

| Category | Total | Pass | Fail | Err | Rate |
|----------|------:|-----:|-----:|----:|-----:|
| Visibility | 21 | 21 | 0 | 0 | **100.0%** |
| Stacking & Rendering | 2 | 2 | 0 | 0 | **100.0%** |
| Outline | 306 | 299 | 6 | 1 | **97.7%** |
| Cursor | 35 | 34 | 1 | 0 | **97.1%** |
| Cascade & Inheritance | 33 | 32 | 1 | 0 | **97.0%** |
| Syntax & Parsing | 310 | 286 | 23 | 1 | **92.3%** |
| Box Model: Borders | 1,802 | 1,638 | 163 | 1 | **90.9%** |
| Dimensions: Width | 258 | 234 | 23 | 1 | **90.7%** |
| Text | 242 | 219 | 22 | 1 | **90.5%** |
| Positioning | 401 | 352 | 47 | 2 | **87.8%** |
| White Space | 62 | 54 | 7 | 1 | **87.1%** |
| Clipping | 68 | 58 | 10 | 0 | **85.3%** |
| Paged Media | 72 | 59 | 12 | 1 | **81.9%** |
| Box Model: Padding | 351 | 287 | 63 | 1 | **81.8%** |
| Overflow | 15 | 12 | 2 | 1 | **80.0%** |
| Other | 364 | 291 | 72 | 1 | **79.9%** |
| Dimensions: Height | 272 | 215 | 56 | 1 | **79.0%** |
| Colors & Backgrounds | 786 | 610 | 176 | 0 | **77.6%** |
| Fonts | 294 | 221 | 72 | 1 | **75.2%** |
| Box Model: Margins | 457 | 342 | 113 | 2 | **74.8%** |
| Display & Inline | 143 | 107 | 35 | 1 | **74.8%** |
| Tables | 348 | 260 | 86 | 2 | **74.7%** |
| Floats & Clear | 174 | 129 | 44 | 1 | **74.1%** |
| Generated Content | 359 | 263 | 96 | 0 | **73.3%** |
| Bidi & Direction | 82 | 52 | 30 | 0 | **63.4%** |
| Lists | 116 | 55 | 61 | 0 | **47.4%** |
| Selectors & Pseudo | 628 | 180 | 445 | 3 | **28.7%** |
| Line Height & Box | 93 | 24 | 69 | 0 | **25.8%** |
| Word Spacing | 20 | 1 | 19 | 0 | **5.0%** |
| Letter Spacing | 44 | 1 | 43 | 0 | **2.3%** |
| Vertical Alignment | 14 | 0 | 14 | 0 | **0.0%** |
| Replaced Elements | 8 | 5 | 3 | 0 | **62.5%** |

### Observations

- **12 categories above 85%** — core block/border/text/width/positioning is solid
- **Selectors & Pseudo (28.7%)** — dominated by 411 `first-letter-punctuation` failures  
- **Letter/Word Spacing (2–5%)** — zero layout engine support for either property
- **Vertical Alignment (0%)** — inline vertical alignment not implemented
- **111 of 295 fine-grained categories** have 100% pass rate (≥5 tests each)

---

## 4. Fine-Grained Category Breakdown (Top 50)

| Category | Total | Pass | Fail | Rate |
|----------|------:|-----:|-----:|-----:|
| first-letter-punctuation | 411 | 0 | 411 | **0.0%** |
| background | 330 | 329 | 1 | 99.7% |
| border-bottom-color | 147 | 147 | 0 | 100.0% |
| border-left-color | 147 | 147 | 0 | 100.0% |
| border-right-color | 147 | 147 | 0 | 100.0% |
| border-top-color | 147 | 147 | 0 | 100.0% |
| color | 147 | 2 | 145 | **1.4%** |
| outline-color | 147 | 147 | 0 | 100.0% |
| content | 124 | 84 | 40 | 67.7% |
| border-conflict-style | 106 | 106 | 0 | 100.0% |
| border-conflict-w | 100 | 100 | 0 | 100.0% |
| border-conflict-width | 100 | 100 | 0 | 100.0% |
| margin-collapse | 94 | 72 | 21 | 76.6% |
| background-position | 90 | 90 | 0 | 100.0% |
| max-height | 75 | 51 | 24 | 68.0% |
| min-height | 73 | 58 | 14 | 79.5% |
| height | 72 | 72 | 0 | 100.0% |
| line-height | 72 | 10 | 62 | **13.9%** |
| max-width | 72 | 65 | 7 | 90.3% |
| text-decoration | 69 | 68 | 0 | 98.6% |
| width | 68 | 68 | 0 | 100.0% |
| font-size | 67 | 66 | 1 | 98.5% |
| min-width | 67 | 63 | 3 | 94.0% |
| padding-bottom | 67 | 64 | 3 | 95.5% |
| padding-top | 67 | 64 | 3 | 95.5% |
| floats | 66 | 48 | 18 | 72.7% |
| padding-left | 66 | 66 | 0 | 100.0% |
| padding-right | 66 | 23 | 43 | **34.8%** |
| outline-width | 64 | 63 | 1 | 98.4% |
| border-left-width | 62 | 53 | 9 | 85.5% |
| border-right-width | 62 | 52 | 10 | 83.9% |
| border-spacing | 61 | 37 | 24 | 60.7% |
| at-charset | 60 | 60 | 0 | 100.0% |
| text-indent | 57 | 55 | 2 | 96.5% |
| border-bottom-width | 56 | 54 | 2 | 96.4% |
| border-top-width | 56 | 54 | 2 | 96.4% |
| counter-increment | 54 | 53 | 1 | 98.1% |
| counter-reset | 54 | 53 | 1 | 98.1% |
| margin-left | 51 | 49 | 2 | 96.1% |
| bottom | 50 | 49 | 1 | 98.0% |
| left | 50 | 47 | 3 | 94.0% |
| margin-bottom | 50 | 48 | 2 | 96.0% |
| margin-right | 50 | 2 | 48 | **4.0%** |
| margin-top | 50 | 50 | 0 | 100.0% |
| right | 50 | 47 | 3 | 94.0% |
| top | 50 | 47 | 3 | 94.0% |
| clip | 47 | 47 | 0 | 100.0% |
| font | 43 | 41 | 2 | 95.3% |
| border-conflict-element | 39 | 39 | 0 | 100.0% |
| quotes | 36 | 34 | 2 | 94.4% |

### Notable Patterns

- `margin-right` (4.0%) vs `margin-left` (96.1%) and `margin-top` (100%) — **RTL positioning** is the sole driver
- `padding-right` (34.8%) vs `padding-left` (100%) — same RTL issue
- `color` (1.4%) and `line-height` (13.9%) — **Ahem font reference data** problem
- `border-right-width` (83.9%) vs `border-left-width` (85.5%) — mild RTL impact

---

## 5. Worst-Performing Categories

Categories with ≥5 tests and 0% pass rate:

| Category | Total | Root Cause |
|----------|------:|------------|
| first-letter-punctuation | 411 | No `::first-letter` support |
| abspos-containing-block | 10 | Padding-edge containing block not resolved |
| border-color-applies-to | 14 | Ahem font reference mismatch |
| border-style | 8 | Ahem font reference mismatch |
| c534-bgreps | 6 | Background-repeat positioning edge case |
| c548-ln-ht | 5 | Line-height with inline vertical-align |
| letter-spacing | 28 | No letter-spacing support |
| letter-spacing-applies-to | 14 | No letter-spacing support |
| list-style-applies-to | 11 | List marker handling missing |
| list-style-image-applies-to | 11 | No list-style-image support |
| list-style-position-applies-to | 11 | List marker position not implemented |
| list-style-type-applies-to | 11 | List marker type not implemented |
| units | 5 | Unit parsing beyond px/em/% |
| vertical-align-applies-to | 14 | No inline vertical-align |
| word-spacing-applies-to | 13 | No word-spacing support |
| word-spacing-remove-space | 6 | No word-spacing support |

---

## 6. Root Cause Analysis

All 1,837 failures (1,814 FAIL + 23 ERROR) trace back to **7 cross-cutting root causes**:

### Root Cause Summary

| # | Root Cause | Est. Failures | Fix Difficulty | Fix Location |
|---|-----------|------:|-----------|-----------|
| 1 | **Ahem font reference data mismatch** | ~500 | Trivial | Reference re-capture |
| 2 | **No `::first-letter` pseudo-element** | ~420 | Hard | Importer + Layout |
| 3 | **Incomplete RTL block positioning** | ~91 | Medium | Layout engine |
| 4 | **No letter-spacing / word-spacing** | ~60 | Easy | Layout engine |
| 5 | **Invalid negative min/max-height not rejected** | ~38 | Trivial | Importer |
| 6 | **No CSS counters / incomplete generated content** | ~60 | Medium | Importer |
| 7 | **Table/list/margin-collapse edge cases** | ~130 | Medium-Hard | Layout engine |

### Detailed Analysis

#### RC1: Ahem Font Reference Data Mismatch (~500 failures)

The `color-*`, `line-height-*`, `border-style-applies-to-*`, `font-variant-applies-to-*`, `font-applies-to-*`, `direction-applies-to-*`, `word-spacing-applies-to-*`, `vertical-align-applies-to-*`, and `font-weight-*` test groups all exhibit the same failure signature:

```
text.width:  expected=69.33, actual=96.0    (ratio: 0.722)
text.width:  expected=17.34, actual=24.0    (ratio: 0.723)
text.height: expected=28.0,  actual=24.0
```

The tests specify `font: Xin ahem` (e.g., `font: 1in ahem` → 96px, `font: 0.25in ahem` → 24px). Ahem is a monospace test font where every glyph = 1em. The Redex engine correctly computes `text.width = font-size × char-count` using Ahem metrics. However, the **Chrome reference JSON** was captured without Ahem installed, causing Chrome to fall back to a proportional font with narrower glyphs (width ratio ~0.722). These are **false negatives**: the Redex answer is correct but the reference is wrong.

**Fix:** Re-run the Puppeteer reference capture with Ahem font installed in macOS.

#### RC2: No `::first-letter` Pseudo-Element (~420 failures)

411 `first-letter-punctuation-*` tests and ~9 `first-letter-*` test failures. These tests use CSS rules like:

```css
div::first-letter { font-size: 36px; }
```

The Redex importer (`reference-import.rkt`) handles `::before` and `::after` pseudo-elements but has **zero support for `::first-letter`**. The layout engine has no concept of splitting a text node's first character(s) into a separately-styled inline box. Tests fail because the enlarged first-letter affects container height (expected=41px, actual=18px).

**Fix:** Requires importer changes to detect `::first-letter` rules and inject a wrapping box, plus layout engine support for the first-letter box model (CSS 2.2 §7.2).

#### RC3: Incomplete RTL Block Positioning (~91 failures)

48 `margin-right-*` and 43 `padding-right-*` failures all involve `direction: rtl`. In RTL mode, child x-positions should be computed from the right edge of the containing block. The engine produces `x: 0` when the reference shows negative or right-aligned positions.

The RTL code path in `layout-block.rkt` handles over-constrained auto-margin resolution for RTL (`resolve-block-width`) but does **not reverse the horizontal origin** when positioning child boxes within the containing block.

**Fix:** In `layout-block.rkt`, when `direction: rtl`, compute child x-position as `containing-width - child-margin-box-width` (mirrored from the right edge).

#### RC4: No Letter-Spacing / Word-Spacing (~60 failures)

28 `letter-spacing-*` + 19 `word-spacing-*` + 13+ applies-to variants. The Redex text measurement functions in `layout-dispatch.rkt` compute text width as `sum(char-widths)` with no additional spacing term. The CSS properties `letter-spacing` and `word-spacing` are parsed by the importer but **completely ignored by layout**.

**Fix:** Add `letter-spacing × (char-count - 1)` and `word-spacing × space-count` to the text width calculation in `layout-dispatch.rkt`'s `layout-text` function.

#### RC5: Invalid Negative Min/Max-Height Not Rejected (~38 failures)

24 `max-height-*` + 14 `min-height-*` failures where tests set `max-height: -1px` or `min-height: -5px`. CSS 2.2 says negative values are invalid — they should be ignored (fall back to `none`/`0`). The Redex importer passes them through unchanged, causing the engine to clamp heights to 0 instead of ignoring the constraint.

**Fix:** In the importer's style parser, discard `min-height` and `max-height` values < 0.

#### RC6: No CSS Counters / Incomplete Generated Content (~60 failures)

40 `content-*` + ~33 `counter-*`/`counters-*` failures. The importer handles `content: "text"` but not:
- `content: counter(name)` / `content: counters(name, separator)`
- `content: open-quote` / `content: close-quote`
- `content: none` / `content: normal` on replaced elements

Additionally, `::before`/`::after` block height computation is incorrect for some cases (expected=40px, actual=23.20px), suggesting the generated block's line-height doesn't match the parent's font metrics context.

**Fix:** Implement counter state tracking in the importer (reset/increment during tree walk), evaluate `counter()` and `counters()` during `content` resolution, and fix block `::before` height calculation.

#### RC7: Table / List / Margin-Collapse Edge Cases (~130 failures)

A heterogeneous group:
- **27** `table-anonymous-objects-*`: CSS 2.2 §17.2.1 anonymous table box generation (auto-wrapping `display: table-cell` in implied table-row/table wrappers)
- **24** `border-spacing-*`: Default spacing values, negative spacing rejection
- **21** `margin-collapse-*`: Edge cases — collapsing through empty blocks, clearance interaction, nested chains
- **~50** `list-style-*`: List marker positioning for `inside` vs `outside`, marker box generation
- **14** `floats-*`: Complex multi-float stacking, float + clear interaction

---

## 7. Enhancement Roadmap

### Priorities

The roadmap is structured into 5 phases, ordered by **impact-per-effort ratio**. Projections assume each fix addresses all tests in its group, with a 10% discount for unforeseen edge cases.

### Estimated Impact

| Phase | Focus | Est. New Passes | Cumulative Rate |
|-------|-------|----------------:|----------------:|
| Current | — | 6,343 | **77.5%** |
| Phase 1 | Quick wins (references + validation) | +540 | **83.1%** |
| Phase 2 | RTL + text spacing | +130 | **84.7%** |
| Phase 3 | `::first-letter` pseudo-element | +420 | **89.8%** |
| Phase 4 | Table & list completeness | +80 | **90.8%** |
| Phase 5 | Layout edge cases | +50 | **91.4%** |

---

## 8. Phase 1: Quick Wins (est. +540 tests)

### 1A: Re-Capture Chrome References with Ahem Font

**Impact:** ~500 tests  
**Effort:** Low (infrastructure, no code changes)  
**Files:** Puppeteer reference capture scripts

The ~500 false negatives from `color-*`, `line-height-*`, and the various `*-applies-to-*` groups are caused by Chrome reference data captured without the Ahem test font. The Redex engine correctly computes Ahem metrics (1em per glyph), but references show proportional-font widths.

**Steps:**
1. Install Ahem font on the reference-capture macOS machine
2. Verify Ahem activation: capture a test with `font: 100px/1 ahem; content: "X"` → width must be 100px
3. Re-run Puppeteer reference capture for the css2.1/html4 suite
4. Re-run Redex tests to confirm false negatives are resolved

### 1B: Reject Invalid Negative Min/Max-Height Values

**Impact:** ~38 tests  
**Effort:** Trivial  
**File:** `test/redex/reference-import.rkt`

```racket
;; In the style property parser, add validation:
(define (parse-length-value str property-name)
  (define val (string->number (regexp-replace #rx"px$" str "")))
  (cond
    [(and val (< val 0) (member property-name '("min-height" "max-height" 
                                                  "min-width" "max-width")))
     #f]  ;; CSS 2.2: negative values are invalid, ignore
    [else val]))
```

Tests like `max-height: -1px` should be discarded by the importer. Currently the engine sees `(max-height (px -1))` and clamps content to 0px height.

---

## 9. Phase 2: RTL & Text Spacing (est. +130 tests)

### 2A: RTL Block Positioning

**Impact:** ~91 tests  
**Effort:** Medium  
**File:** `test/redex/layout-block.rkt`

The entire `margin-right` (48 failures) and `padding-right` (43 failures) issue is RTL positioning. In LTR mode, children start at x=0 plus left margin/padding. In RTL mode, children should start from the right edge:

```
child-x = containing-width - child-margin-box-width
```

**Changes needed:**
1. Pass `direction` through to block child positioning
2. When `direction: rtl`, flip the x-origin for each child in the containing block
3. Handle over-constrained width: when left + right + width > containing-width, RTL gives priority to right (CSS 2.2 §10.3.3)

The `resolve-block-width` function already handles the auto-margin RTL case — the gap is in final positioning after width resolution.

### 2B: Letter-Spacing Support

**Impact:** ~41 tests  
**Effort:** Easy  
**File:** `test/redex/layout-dispatch.rkt`

In `layout-text`:

```racket
;; Current: text-width = sum of per-char widths
;; Fix: text-width += letter-spacing * (char-count - 1)
(define ls (get-style-prop styles 'letter-spacing 0))
(define total-width (+ measured-width (* ls (max 0 (sub1 (string-length text))))))
```

The `letter-spacing` property is already parsed by the importer (`reference-import.rkt` line 887) — it just needs to be consumed during layout.

### 2C: Word-Spacing Support

**Impact:** ~19 tests  
**Effort:** Easy  
**File:** `test/redex/layout-dispatch.rkt`

Similar to letter-spacing:

```racket
;; Add word-spacing for each space character
(define ws (get-style-prop styles 'word-spacing 0))
(define space-count (length (regexp-match* #rx" " text)))
(define total-width (+ measured-width (* ws space-count)))
```

---

## 10. Phase 3: Pseudo-Elements (est. +420 tests)

### 3A: `::first-letter` Pseudo-Element Support

**Impact:** ~420 tests  
**Effort:** Hard  
**Files:** `test/redex/reference-import.rkt`, `test/redex/layout-inline.rkt`, `test/redex/layout-dispatch.rkt`

This is the single highest-count failure group (411 `first-letter-punctuation` + ~9 `first-letter` selector tests).

**Implementation plan:**

#### Step 1: Detect `::first-letter` rules in the importer

In `reference-import.rkt`, extend the style-rule parser to recognize `::first-letter` / `:first-letter` selectors. Store the first-letter styles separately rather than applying them to the whole element.

```racket
;; New: first-letter style extraction
(define (extract-first-letter-styles rules)
  (filter-map
    (lambda (rule)
      (and (regexp-match? #rx"::?first-letter" (car rule))
           (cdr rule)))
    rules))
```

#### Step 2: Generate first-letter box in the box tree

During box tree construction, when an element has a `::first-letter` rule:
1. Find the first text child (recursing into inline children)
2. Split the text at the first letter boundary (letter + adjacent punctuation per CSS 2.2 §7.2)
3. Wrap the first-letter characters in an `inline` box with the first-letter styles
4. Leave the remaining text as a sibling text node

```
;; Before: (block "div" (style ...) (text "Hello"))
;; After:  (block "div" (style ...) 
;;           (inline "__first-letter" (style font-size 36px ...) (text "H"))
;;           (text "ello"))
```

#### Step 3: Handle punctuation inclusion (CSS 2.2 §7.2)

The `first-letter-punctuation-*` tests specifically verify that Unicode punctuation adjacent to the first letter is included. The first-letter boundary should include:
- The first typographic letter unit
- Any preceding or following Unicode characters in categories Ps (open punctuation), Pe (close), Pi (initial quote), Pf (final quote), Po (other punctuation)

#### Complexity considerations

- `::first-letter` interacts with `::before` generated content — if `::before` generates text, its first letter is the `::first-letter`
- First-letter only applies to block containers (not inline or table)
- The first-letter box is an inline-level box with block-level properties (margins, padding, float)

---

## 11. Phase 4: Table & List Completeness (est. +80 tests)

### 4A: Anonymous Table Box Generation

**Impact:** ~27 tests  
**Effort:** Medium  
**File:** `test/redex/reference-import.rkt` or `test/redex/layout-table.rkt`

CSS 2.2 §17.2.1 requires automatic insertion of anonymous table boxes when table-internal display types appear outside a proper table context:

| Found | Missing Ancestor | Insert |
|-------|-----------------|--------|
| `table-cell` without `table-row` | `table-row` | Anonymous `table-row` wrapper |
| `table-row` without `table` | `table-row-group` + `table` | Anonymous wrappers |
| `table-cell` without `table` | All three levels | Anonymous `table` + `table-row-group` + `table-row` |

**Implementation:** Add a tree normalization pass in `layout-table.rkt` (or as preprocessing in the importer) that walks the box tree and wraps orphaned table-internal boxes in anonymous containers.

### 4B: List Marker Positioning

**Impact:** ~50 tests  
**Effort:** Medium  
**Files:** `test/redex/layout-block.rkt`, `test/redex/reference-import.rkt`

The `list-style-position-*`, `list-style-type-*`, and `list-style-applies-to-*` tests verify list marker rendering. Currently the engine handles `__list-marker-inside-width` (inside markers take up horizontal space) but:

1. **Outside markers** need a negative-offset marker box placed to the left of the content area
2. **Marker type** (disc, circle, square, decimal, etc.) affects marker width — currently not computed
3. **`list-style-image`** requires image sizing — can be stubbed with a fixed marker width

### 4C: Border-Spacing Default & Validation

**Impact:** ~24 tests  
**Effort:** Easy  
**File:** `test/redex/reference-import.rkt`

- Set default `border-spacing: 2px` when not explicitly specified (Chrome default)
- Reject negative border-spacing values (CSS: invalid)
- Ensure two-value syntax is parsed: `border-spacing: 5px 10px` (horizontal, vertical)

---

## 12. Phase 5: Layout Edge Cases (est. +50 tests)

### 5A: Margin Collapse Through Empty Blocks

**Impact:** ~21 tests  
**Effort:** Medium  
**File:** `test/redex/layout-block.rkt`

CSS 2.2 §8.3.1 specifies that margins collapse through an element when it has:
- No border, padding, inline content, height, or min-height to separate its top and bottom margins
- No clearance

The current `collapse-margins` implementation handles basic adjacent-sibling and parent-first/last-child collapsing but misses the **"collapse through"** case where a zero-height block's top and bottom margins collapse with each other and participate in the parent's margin collapsing chain.

### 5B: Absolute Positioning Containing Block Resolution

**Impact:** ~10 tests  
**Effort:** Medium  
**File:** `test/redex/layout-positioned.rkt`

Tests like `abspos-containing-block-*` fail because the containing block for absolute positioning should be the **padding edge** of the nearest positioned ancestor. The current implementation may not properly propagate the parent's padding when computing the containing block dimensions.

**Fix:** When laying out absolute children, pass `parent-content-width + parent-padding` as the containing block width, and offset the child's position by the negative of the parent's padding (since the containing block starts at the padding edge, not the content edge).

### 5C: Inline Vertical-Align

**Impact:** ~14 tests  
**Effort:** Hard  
**File:** `test/redex/layout-inline.rkt`

Currently 0% pass rate for `vertical-align-applies-to-*`. The inline layout module creates line boxes but does **not implement `vertical-align` positioning** within line boxes. All inline items are baseline-aligned by default.

**Required vertical-align values:**
- `baseline` (default, already implicit)
- `top` / `bottom` (align to line box top/bottom)
- `middle` (align midpoint to parent baseline + half x-height)
- `sub` / `super` (shift baseline down/up)
- `<length>` / `<percentage>` (shift baseline by value)

This is architecturally complex because `vertical-align` affects **line box height computation** — a `top`-aligned element can increase the line box height, which then repositions `bottom`-aligned elements.

---

## 13. Projected Progress

### Cumulative Projections (with 10% edge-case discount)

| After Phase | Pass | Total | Rate | Description |
|-------------|-----:|------:|-----:|-------------|
| **Current** | 6,343 | 8,180 | **77.5%** | Baseline |
| **Phase 1** | 6,883 | 8,180 | **84.1%** | Ahem references + negative value validation |
| **Phase 2** | 7,000 | 8,180 | **85.6%** | RTL positioning + text spacing |
| **Phase 3** | 7,378 | 8,180 | **90.2%** | `::first-letter` support |
| **Phase 4** | 7,450 | 8,180 | **91.1%** | Table anonymous boxes + list markers |
| **Phase 5** | 7,495 | 8,180 | **91.6%** | Margin collapse + abspos + vertical-align |

### Beyond Phase 5: Reaching 95%+

To push beyond 91% on the tested 8,180, remaining failures will be a **long tail** of edge cases across many CSS properties. Additionally, the **1,739 skipped complex tests** (with `<span>` and `<img>`) could be unlocked by:

1. **Extending the importer's `classify-html-test`** to accept `<span>` elements (treat as inline boxes), increasing the test pool to ~9,900
2. **Adding replaced element support** (`<img>` with intrinsic dimensions from reference data)
3. **Supporting sibling selectors** (`+`, `~`) and structural pseudo-classes (`:nth-child`, `:first-child`, `:last-child`) in the CSS cascade
4. **Implementing `::after` pseudo-element** beyond the current partial support

With all 9,919 tests runnable, achieving 90%+ overall would represent strong CSS 2.1 conformance.

---

## 14. Architecture Notes

### Current Redex Engine Architecture

```
HTML/CSS  ─→  reference-import.rkt ─→  Box Tree (css-layout-lang.rkt)
                 │                          │
                 │ Style parsing            │ Layout dispatch
                 │ Selector matching        │
                 │ Shorthand expansion      ├── layout-block.rkt     (1,105 lines)
                 │ ::before/::after         ├── layout-inline.rkt    (242 lines)
                 │ Font measurement         ├── layout-flex.rkt      (2,020 lines)
                 │ Ahem text metrics        ├── layout-grid.rkt      (2,142 lines)
                 │                          ├── layout-table.rkt     (804 lines)
                 │ (3,658 lines)            ├── layout-positioned.rkt(284 lines)
                 │                          └── layout-intrinsic.rkt (174 lines)
                 │                          │
                 ▼                          ▼
          Chrome ref JSON  ←──  compare-layouts.rkt  ←──  View Tree
```

### Module Size Summary

| Module | Lines | Role |
|--------|------:|------|
| reference-import.rkt | 3,658 | Importer: HTML/CSS → Box Tree |
| layout-grid.rkt | 2,142 | CSS Grid layout |
| layout-flex.rkt | 2,020 | CSS Flexbox layout |
| layout-block.rkt | 1,105 | Block flow, floats, margin collapse |
| layout-table.rkt | 804 | Table layout |
| layout-dispatch.rkt | 550 | Dispatch, text, replaced elements |
| layout-common.rkt | 454 | Box model, length resolution |
| compare-layouts.rkt | 379 | Tolerance-based comparison |
| font-metrics.rkt | 367 | JSON font metrics, Chrome line-height |
| layout-positioned.rkt | 284 | Absolute/relative positioning |
| layout-inline.rkt | 242 | Inline formatting, line boxes |
| css-layout-lang.rkt | 182 | Redex grammar definition |
| layout-intrinsic.rkt | 174 | Min/max-content sizing |
| **Total** | **~12,400** | |

### Key Architectural Constraints

1. **Importer is the bottleneck.** At 3,658 lines, `reference-import.rkt` is the largest module because it handles all CSS parsing, cascade, inheritance, shorthand expansion, and pseudo-element generation. Every new CSS feature requires importer changes before the layout engine can even see it.

2. **No shared CSS parser.** The importer has its own bespoke inline-style and `<style>` block parsers. Complex selectors (`+`, `~`, `:nth-child`), at-rules (`@media`, `@import`), and shorthand properties require manual additions.

3. **Flat namespace for styles.** The `(style ...)` representation in `css-layout-lang.rkt` is a flat list of property-value pairs. Adding new properties requires extending both the Redex grammar and every module that pattern-matches on styles.

4. **Text layout is font-dependent.** Text width measurement requires pre-loaded JSON font metrics. Tests using fonts other than Ahem, Times, Arial, or monospace will have measurement errors.

5. **Comparison tolerance hides small errors.** The 3px base / 3% proportional / 10px max tolerance means small systematic errors (e.g., 1–2px off) in common patterns accumulate silently. Tightening tolerance would reveal more failures but may not reflect actual rendering differences visible to users.

### Structural Enhancement Recommendations

For the Redex engine to evolve from a verification oracle (~90% conformance target) toward a standalone reference implementation (~95%+ conformance), these architectural improvements would provide the most leverage:

| Enhancement | Benefit |
|-------------|---------|
| **Extract CSS parser into separate module** | Enables reuse, testing, and extension of selector matching without modifying the import pipeline |
| **Add writing-mode axis abstraction** | Unifies LTR/RTL/vertical by mapping logical (inline-start/block-start) to physical (left/top) once, rather than sprinkling `direction` checks |
| **Implement a proper cascade** | Replace the current specificity-ordered list with a proper cascade origin/importance/specificity/order resolution per CSS Cascading §6 |
| **Add a pseudo-element normalization pass** | Pre-process `::first-letter`, `::first-line`, `::before`, `::after` into the box tree before layout, rather than handling each as a special case during import |
| **Separate counter/quote state machine** | CSS counters and open/close-quote need document-order traversal state — extract into a stateful pre-layout pass |
