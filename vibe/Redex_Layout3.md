# Redex Layout Phase 3 — Closing the Baseline Gap

> Proposal for advancing the PLT Redex CSS layout specification from 1541/1825 baseline tests (84.4%) to near-complete baseline coverage, plus improvements across all test suites.

---

## Table of Contents

1. [Current State & Summary](#1-current-state--summary)
2. [Cross-Suite Test Results](#2-cross-suite-test-results)
3. [Failure Root-Cause Taxonomy (Baseline)](#3-failure-root-cause-taxonomy-baseline)
4. [Phase 3A: Root Element Margin Positioning (~30 tests)](#4-phase-3a-root-element-margin-positioning)
5. [Phase 3B: Font-Size Zero & Font Measurement (~31 tests)](#5-phase-3b-font-size-zero--font-measurement)
6. [Phase 3C: Float Layout (~98 tests)](#6-phase-3c-float-layout)
7. [Phase 3D: Table Layout Improvements (~76 tests)](#7-phase-3d-table-layout-improvements)
8. [Phase 3E: List-Item Display (~14 tests)](#8-phase-3e-list-item-display)
9. [Phase 3F: Child-Count / Inline Text Nodes (~12 tests)](#9-phase-3f-child-count--inline-text-nodes)
10. [Phase 3G: Block Width/Height Edge Cases (~48 tests)](#10-phase-3g-block-widthheight-edge-cases)
11. [Phase 3H: Flex/Grid Remaining (~12 tests)](#11-phase-3h-flexgrid-remaining)
12. [Phase 3I: Performance — Depth Guard for Exponential Blowup](#12-phase-3i-performance--depth-guard)
13. [Implementation Priority & Milestones](#13-implementation-priority--milestones)
14. [Known Limitations / Deferred](#14-known-limitations--deferred)
15. [Appendix A: All Failing Tests by Category](#15-appendix-a-all-failing-tests-by-category)
16. [Appendix B: Progress Log](#16-appendix-b-progress-log)

---

## 1. Current State & Summary

### Phase 2 Achievement

| Suite | Total | Pass | Fail | Pass Rate |
|-------|-------|------|------|-----------|
| **baseline** | 1825 | 1541 | 284 | **84.4%** |
| **flex** | 156 | 153 | 3 | **98.1%** |
| **grid** | 123 | 117 | 6 | **95.1%** |
| **box** | 77 | 68 | 9 | **88.3%** |
| **basic** | 84 | 53 | 31 | **63.1%** |
| **position** | 53 | 19 | 34 | **35.8%** |
| **table** | 103 | 51 | 52 | **49.5%** |
| **text_flow** | 14 | 14 | 0 | **100%** |
| **flex-nest** | 2 | 0 | 2 | **0%** |
| **page** | ~42 | ~30 | ~10+hang | — |
| **Grand Total** | ~2479 | ~2046 | ~431 | **~82.5%** |

### Phase 3 Target: Baseline 100%

The primary goal of Phase 3 is to **pass all 1825 tests in the baseline suite** (currently 1541/1825 = 84.4%). Secondary goals are to improve the position (35.8%), basic (63.1%), and table (49.5%) suites, and to fix a critical performance issue causing hangs on complex pages.

### Critical Bug Found: Exponential Blowup on Nested Flex

Running `racket test-differential.rkt --suite page` hangs on `contact-form.html` — a deeply-nested flex layout page. The stack trace shows:

```
layout-flex → collect-flex-items → measure-flex-item-content → layout →
layout-flex → collect-flex-items → measure-flex-item-content → layout → ...
```

This isn't infinite recursion (it's bounded by tree depth), but it's **exponential**: each flex container measures each child by dispatching a full layout, and each child-flex-container does the same to its children. With depth $d$ and $k$ children per level, this is $O(k^d)$ layout calls. A page with 10 levels of nested flex and 3 children per level = $3^{10} = 59{,}049$ layout calls — for measurement passes alone.

---

## 2. Cross-Suite Test Results

### Baseline Failure Distribution (284 tests)

| Category | Count | % of Failures | Root Cause |
|----------|-------|---------------|------------|
| **Float layout** | 63 | 22.2% | Float positioning, clearance, BFC height not implemented |
| **Table layout** | 75 | 26.4% | Table cell/column width distribution incomplete |
| **Mixed (multi-property)** | 23 | 8.1% | Various combinations |
| **Width-only mismatches** | 24 | 8.5% | Shrink-to-fit, text width, border-box edge cases |
| **Font-size zero** | 23 | 8.1% | `font-size: 0` not handled (text rendered at 16px instead of 0) |
| **Height-only mismatches** | 19 | 6.7% | Auto height, min-height, collapsed margins |
| **Root margin-left (x=0)** | 16 | 5.6% | Root element margin not applied to position |
| **List-item display** | 11 | 3.9% | `display: list-item` marker not generated |
| **Position-only (x/y off)** | 10 | 3.5% | wrap-reverse, order, grid masonry |
| **Root margin + other** | 4 | 1.4% | Root margin + width/height cascade |
| **Child-count only** | 5 | 1.8% | Text node splitting/merging |
| **Child-count + size** | 6 | 2.1% | Text node + dimension issues |
| **Font measurement** | 5 | 1.8% | Non-Ahem font metric precision |

### Position Suite (34 failures — all floats)

Every single position suite failure involves float layout:
- `baseline_813_float_right.html` — float:right not positioned correctly
- `floats-*.htm` (30 tests) — comprehensive CSS 2.1 float conformance tests
- `floats-rule3-*`, `floats-rule7-*` — CSS 2.1 float placement rules
- `floats-wrap-bfc-outside-*` — BFC + float interaction

### Basic Suite (31 failures)

| Sub-category | Count |
|-------------|-------|
| Root margin + other | 9 |
| Mixed | 7 |
| Height-only | 3 |
| Font measurement | 3 |
| List-item | 3 |
| Width-only | 2 |
| Root margin only | 1 |
| Child-count + size | 1 |
| Table layout | 1 |
| Float layout | 1 |

### Table Suite (52 failures)

The table suite has the lowest pass rate (49.5%). Failures involve:
- Fixed table layout column width distribution
- Border-collapse model
- Table-caption placement (above/below)
- thead/tfoot ordering
- Cell intrinsic sizing
- Table percentage widths

---

## 3. Failure Root-Cause Taxonomy (Baseline)

### Category 1: Root Element Margin Positioning (30 tests across all suites)

**Bug:** `layout-document` returns the root view with `x=0, y=0`. The design assumes every box's position is set by its parent. The root element has no parent, so its margin-left/margin-top are never applied.

**Evidence:**
```
baseline_803_basic_margin.html:   x=0 (expected=20)
baseline_808_position_relative:   x=0 (expected=10)
baseline_811_basic_border:        x=0 (expected=30)
yoga-*-margin.html:               x=0 (expected=100)
abspos-001.html:                  x=0 (expected=16)   ← body padding
```

**Fix:** In `layout-document`, after calling `layout`, extract root box-model and apply margin offsets to the returned view.

### Category 2: Font-Size Zero (23 tests)

**Bug:** When `font-size: 0` or `font-size: 0px`, text should have zero width/height and the containing block should collapse to zero height. Currently, text always renders at the default 16px font-size.

**Evidence:**
```
font-size-089.htm: height expected=0, actual=16; text.width expected=0, actual=176
font-size-090.htm: same pattern
font-size-092.htm: same pattern
font-size-093.htm: same pattern
```

**Fix:** In `layout-text` and related text measurement functions, check `font-size` from styles and return 0×0 view when font-size ≤ 0.

### Category 3: Float Layout (98 tests across baseline + position suites)

**Status:** Float layout has basic support (left floats tracked, clearance partially implemented) but is fundamentally incomplete.

**Missing:**
1. **Float placement algorithm** — CSS 2.1 §9.5.1: floats are removed from normal flow and placed at the current line edge, then subsequent content flows around them
2. **Right floats** — only left floats are tracked
3. **Clear property** — `clear: left/right/both` should move the clear element below the relevant float
4. **BFC height inclusion** — block formatting contexts include floats in height calculation
5. **Float avoidance** — new BFCs should avoid overlapping with floats
6. **Float stacking rules** — CSS 2.1 §9.5.1 rules 1-7 for float placement

### Category 4: Table Layout (76 tests in baseline)

**Current state:** `layout-table-simple` does vertical stacking with equal-width cells — a minimal stub.

**Missing:**
1. **Column width algorithm** — CSS 2.1 §17.5.2: fixed table layout, auto table layout
2. **Border-collapse** — CSS 2.1 §17.6: collapsing borders between adjacent cells
3. **Caption placement** — `caption-side: top/bottom`
4. **thead/tfoot ordering** — thead always first, tfoot always last regardless of DOM order
5. **Cell spanning** — `colspan`, `rowspan`
6. **Percentage widths** — table and cell percentage width resolution

### Category 5: List-Item Display (14 tests)

**Missing:** `display: list-item` should generate a marker box (bullet/number). Currently treated as plain block.

**Fix:** Generate an inline marker pseudo-element before the content, offset by `text-indent` or marker padding.

### Category 6: Width/Height Edge Cases (48 tests)

Various issues:
- **`text-transform`** — `uppercase`/`lowercase` changes text width (5 tests)
- **`text-indent`** — negative text-indent causing text overflow (4 tests)
- **Auto height with floats** — BFC height should include floats (3 tests)
- **`min-content` / `max-content`** — intrinsic sizing edge cases (6 tests)
- **Collapsed margins** — margin collapsing through empty boxes (4 tests)
- **`writing-mode: vertical-lr`** — swaps inline/block axes (3 tests, deferred)

---

## 4. Phase 3A: Root Element Margin Positioning

**Impact: ~30 tests across all suites (16 baseline-only + 14 multi-suite)**
**Effort: Small (< 50 lines)**
**Risk: Very low**

### The Bug

In `layout-dispatch.rkt`:
```racket
(define (layout-document root-box viewport-w viewport-h)
  (define avail `(avail (definite ,viewport-w) (definite ,viewport-h)))
  (layout root-box avail))
```

The root view is returned with x=0, y=0. No parent exists to position it.

### The Fix

```racket
(define (layout-document root-box viewport-w viewport-h)
  (define avail `(avail (definite ,viewport-w) (definite ,viewport-h)))
  (define view (layout root-box avail))
  ;; Root element has no parent to apply its margin positioning.
  ;; Extract box model and apply margin offsets (mirrors layout-block-children).
  (define root-styles (get-box-styles root-box))
  (define root-bm (extract-box-model root-styles viewport-w))
  (set-view-pos view
                (box-model-margin-left root-bm)
                (box-model-margin-top root-bm)))
```

Additionally, the root element's available width should account for horizontal margins:
```racket
(define margin-h (+ (box-model-margin-left root-bm)
                     (box-model-margin-right root-bm)))
(define avail `(avail (definite ,(- viewport-w margin-h))
                      (definite ,viewport-h)))
```

Wait — `resolve-block-width` already subtracts margins from available width. But the avail passed to layout should be the full viewport width; `resolve-block-width` handles the subtraction internally. The key issue is just that x/y are not set. However, some tests like `abspos-001.html` show `width: expected=1168, actual=1200` alongside `x: expected=16, actual=0` — suggesting the width is *also* not accounting for the root margin. This means we need to verify that the avail width passed to the root layout correctly represents the **containing block** for the root element (which is the viewport minus body padding/margin).

**Verification:** Look at 2-3 tests with root_margin_plus (both x and width mismatch) to confirm.

---

## 5. Phase 3B: Font-Size Zero & Font Measurement

**Impact: ~31 tests (23 font-size-zero + 8 font-measurement)**
**Effort: Small-Medium**
**Risk: Low**

### 5.1 Font-Size Zero (23 tests)

All 23 tests set `font-size: 0` (or `font-size: 0px`, `font-size: 0%`, `font-size: 0em`). The text should render as invisible (0×0), and the containing block should have zero height.

**Current behavior:** Text measurement ignores font-size and always uses 16px.

**Fix in `layout-dispatch.rkt`:**

```racket
(define (layout-text text-box avail ...)
  (define font-size (get-style-prop styles 'font-size 16))
  (when (<= font-size 0)
    (return (make-text-view text-content 0 0)))
  ;; ... existing measurement code
  )
```

Also fix `layout-block.rkt` auto height to collapse when all children have zero height.

### 5.2 Font Measurement Precision (8 tests)

These tests use specific font-size values that reveal imprecision in our character-width tables:
- `font-size-080.htm`: `font-size: 7px` → height expected=7, actual=12.8
- `font-003.htm`: text.y expected=-7 (ascender overshoot)
- `font-size-091.htm`: `font-size: 1px` → text.width expected=0.97, actual=16

**Fix:** Ensure font-size is properly resolved from CSS computed values (px, em, %) and used as the scaling factor for character widths. Currently, the code reads `font-size` but may not propagate it correctly to all measurement paths.

---

## 6. Phase 3C: Float Layout

**Impact: ~98 tests (63 baseline + 34 position + 1 basic)**
**Effort: Large (estimated 500-800 lines)**
**Risk: Medium-High (complex CSS 2.1 feature)**

### Current State

`layout-block.rkt` has basic float tracking:
- Tracks left-float positions in a float context
- BFC float avoidance uses a hardcoded 50px estimate
- No right-float support
- No clear property implementation
- BFC height doesn't include floats

### Required: CSS 2.1 §9.5 Float Model

#### 6.1 Float Context Data Structure

```racket
;; Track all floats in the current BFC
(struct float-context
  (left-floats    ; list of (float-rect x y width height)
   right-floats   ; list of (float-rect x y width height)
   container-width) #:mutable)
```

#### 6.2 Float Placement Algorithm (§9.5.1)

1. A float is removed from normal flow
2. It is shifted to the left or right until its outer edge touches the containing block edge or the outer edge of another float
3. If the current line is too narrow for the float, it is shifted downward
4. The top of the float must not be higher than the top of any earlier float
5. The top of the float must not be higher than the top of any line box in the current flow

**Seven placement rules from CSS 2.1:**
- Rule 1: Left float's left outer edge must not be to the left of its containing block's left edge
- Rule 2: If current float is left, and there are earlier left floats, its left outer edge must not be to the left of any earlier float's right outer edge
- Rule 3: The right outer edge of a left float may not be to the right of the left outer edge of any right float
- Rule 4: A float's top may not be higher than its containing block
- Rule 5: A float's top may not be higher than the top of any earlier float
- Rule 6: A float's top may not be higher than any line box with content that precedes it
- Rule 7: A left float that has another left float to its left may not have its right outer edge to the right of its containing block's right edge

#### 6.3 Clear Property

`clear: left | right | both` — The clear element's top margin edge is pushed below the bottom of the relevant float(s).

```racket
(define (apply-clear clearance float-ctx current-y)
  (define max-float-bottom
    (match clearance
      ['left  (max-bottom (float-context-left-floats float-ctx))]
      ['right (max-bottom (float-context-right-floats float-ctx))]
      ['both  (max (max-bottom (float-context-left-floats float-ctx))
                   (max-bottom (float-context-right-floats float-ctx)))]))
  (max current-y max-float-bottom))
```

#### 6.4 BFC Height Includes Floats

For elements that establish a new BFC (e.g., `overflow: hidden/auto/scroll`, `display: flex/grid`, floats), the height must include any floats:

```racket
(define (bfc-auto-height content-height float-ctx)
  (max content-height
       (max-bottom (float-context-left-floats float-ctx))
       (max-bottom (float-context-right-floats float-ctx))))
```

#### 6.5 Content Flow Around Floats

Normal flow content must avoid overlapping with floats. Line boxes next to floats are shortened:

```racket
(define (available-line-width y float-ctx container-width)
  (define left-intrusion (sum-intrusion-at-y y (float-context-left-floats float-ctx)))
  (define right-intrusion (sum-intrusion-at-y y (float-context-right-floats float-ctx)))
  (- container-width left-intrusion right-intrusion))
```

### Implementation Plan

1. Define `float-context` struct with left/right float lists
2. Implement float placement algorithm following CSS 2.1 rules 1-7
3. Thread `float-context` through `layout-block-children`
4. Implement `clear` property in block child positioning
5. Implement BFC height calculation including floats
6. Implement shrink-to-fit for float boxes
7. Implement content flow around floats (shortened line boxes)

---

## 7. Phase 3D: Table Layout Improvements

**Impact: ~76 tests in baseline + 52 in table suite**
**Effort: Large (estimated 400-600 lines)**
**Risk: Medium**

### Current State

`layout-table-simple` in `layout-dispatch.rkt` (~30 lines) does:
- Vertical stacking of rows
- Equal-width cells within each row
- Basic border-box sizing

### Required: CSS 2.1 §17 Table Model

#### 7.1 Fixed Table Layout (§17.5.2.1)

The fixed table layout algorithm uses column widths from the first row:

```racket
(define (fixed-table-layout table-box table-width columns)
  ;; 1. Column widths from col elements or first-row cells
  ;; 2. Remaining space distributed to auto columns equally
  ;; 3. Table width = max(specified width, sum of column widths)
  )
```

#### 7.2 Auto Table Layout (§17.5.2.2)

The auto table layout uses intrinsic content sizes:

```racket
(define (auto-table-layout table-box container-width)
  ;; 1. Calculate each column's minimum and maximum width
  ;;    - min: widest unbreakable content in any cell
  ;;    - max: widest content without wrapping
  ;; 2. Spanning cells distribute across spanned columns
  ;; 3. Distribute available width proportionally
  )
```

#### 7.3 Caption Placement

```racket
;; caption-side: top → caption view placed before table grid
;; caption-side: bottom → caption view placed after table grid
```

#### 7.4 thead/tfoot Ordering

```racket
;; DOM order may be: <tfoot> before <tbody>
;; Visual order: thead first, tbody middle, tfoot last
;; Sort row groups by type, not DOM order
```

#### 7.5 Border Collapse

```racket
;; border-collapse: collapse
;; Adjacent cell borders merge, taking the widest/most-specific
;; Table border + cell border → single border line
;; Affects cell sizing (shared borders counted once, not twice)
```

### Implementation Priority

1. **Fixed table layout** — handles `table-layout: fixed` (simpler algorithm)
2. **Auto table layout** — intrinsic width distribution
3. **Caption placement** — `caption-side` property
4. **Row group ordering** — thead/tfoot visual ordering
5. **Border collapse** — CSS 2.1 border collapsing model
6. **Spanning cells** — colspan/rowspan distribution

---

## 8. Phase 3E: List-Item Display

**Impact: ~14 tests (11 baseline + 3 basic)**
**Effort: Small (< 100 lines)**
**Risk: Low**

### What's Needed

`display: list-item` creates a block box with a marker box. The marker is typically a bullet (•) for `ul` or a number for `ol`.

### Implementation

```racket
(define (layout-list-item box avail dispatch-fn)
  ;; 1. Extract list-style-type and list-style-position
  ;; 2. Generate marker text (•, ◦, ▪, 1., 2., etc.)
  ;; 3. If list-style-position: inside → prepend marker as inline content
  ;; 4. If list-style-position: outside → position marker in left margin area
  ;; 5. Lay out content as a normal block box
  )
```

Key properties:
- `list-style-type`: `disc | circle | square | decimal | none`
- `list-style-position`: `inside | outside`
- Marker width: typically ~40px left offset for `outside` markers
- For the reference JSON tests, the browser-computed marker is reflected in the `text-indent` or marker pseudo-element sizing

### List-Style Tests Pattern

```
list-style-type-001.htm: width expected=1142, actual=1100  (marker adds ~42px)
list-style-002.htm: child-count expected=3, actual=2        (marker is a child)
```

The 42px width difference is the list marker's contribution. The child-count difference means the marker pseudo-element needs to be emitted as a view child.

---

## 9. Phase 3F: Child-Count / Inline Text Nodes

**Impact: ~12 tests**
**Effort: Medium**
**Risk: Low-Medium**

### Root Cause

Child-count mismatches occur when:
1. **Whitespace text nodes** are stripped by the HTML parser but the browser preserves them
2. **`<br>` elements** create line breaks that split text into multiple nodes
3. **Inline elements** (`<span>`, `<strong>`) are not generated as separate child views
4. **`white-space: pre`** preserves whitespace nodes

### Fix Strategy

1. **Preserve whitespace between inline-block elements** — Modify `maybe-add-text-node!` to keep whitespace nodes when adjacent siblings are `display: inline-block`
2. **`<br>` handling** — Generate a line-break view that causes text splitting
3. **Inline element views** — Generate child views for `<span>` etc. (requires IFC)
4. **white-space: pre** — Two-pass: preserve all text nodes initially, prune based on CSS white-space in a second pass

### Tests Affected

| Test | Root Cause |
|------|-----------|
| `box_005_box_sizing.html` | Missing text node child |
| `flex_012_nested_lists.html` | Missing list item markers |
| `flex_014_nested_flex.html` | Missing whitespace text node |
| `inline-block-height-001.htm` | Missing whitespace between inline-blocks |
| `baseline_501_line_breaks.html` | `<br>` not splitting text |
| `box_004_borders.html` | Whitespace between inline-block elements |

---

## 10. Phase 3G: Block Width/Height Edge Cases

**Impact: ~48 tests (24 width-only + 19 height-only + 5 stretch)**
**Effort: Medium**

### 10.1 Width Mismatches (24 tests)

**Subcategories:**

| Pattern | Count | Fix |
|---------|-------|-----|
| List-item marker width | 12 | Add marker width to content width (Phase 3E) |
| `text-transform` width | 5 | Apply uppercase/lowercase to text before measurement |
| `border-box` edge case | 3 | Verify border-box width subtraction |
| Grid `max-size` with padding/border | 2 | `max-width` should include padding+border in `border-box` |
| `display: inline-block` shrink-to-fit | 2 | Intrinsic sizing in inline-block context |

**Text-transform fix:**
```racket
(define (apply-text-transform text transform)
  (case transform
    [(uppercase) (string-upcase text)]
    [(lowercase) (string-downcase text)]
    [(capitalize) (string-titlecase text)]
    [else text]))
```

### 10.2 Height Mismatches (19 tests)

| Pattern | Count | Fix |
|---------|-------|-----|
| `::before`/`::after` content height | 4 | Pseudo-element strut/block height |
| Collapsed margins through empty blocks | 4 | Margins collapse through zero-height blocks (§8.3.1) |
| `min-height` propagation | 3 | Verify min-height resolves percentages correctly |
| Auto height with `position: relative` | 3 | Relative children should contribute to parent height |
| `line-height` precision | 3 | Font-specific line-height ratios |
| `overflow: hidden` height | 2 | BFC height includes floats |

**Margin collapse through empty blocks (CSS 2.1 §8.3.1):**
Currently, margin collapsing only handles parent/child and adjacent sibling cases. The "through" case is missing:
> *If a block has no border, padding, height, min-height, or max-height, and all of its children's margins collapse, then its own margins collapse through it.*

```racket
(define (margins-collapse-through? box)
  (and (zero? border-top) (zero? border-bottom)
       (zero? padding-top) (zero? padding-bottom)
       (auto-height? box) (zero? min-height)
       (every child-margins-collapsed? children)))
```

---

## 11. Phase 3H: Flex/Grid Remaining

### Flex: 3 Remaining Failures (all `writing-mode: vertical-lr`)

| Test | Issue |
|------|-------|
| `intrinsic_sizing_main_size_column.html` | Width/height axes swapped |
| `intrinsic_sizing_main_size_column_nested.html` | Same |
| `intrinsic_sizing_main_size_column_wrap.html` | Same |

**Status: Deferred.** Writing-mode support requires swapping inline/block axes throughout the entire layout engine — a cross-cutting concern that affects every layout mode.

### Grid: 6 Remaining Failures

| Test | Issue | Status |
|------|-------|--------|
| `grid_015_content_sizing.html` | Non-Ahem font metrics (Arial) | Deferred — font engine limitation |
| `grid_119_negative_lines.html` | Negative line numbers + font | Deferred |
| `grid_relayout_vertical_text.html` | `writing-mode: vertical-lr` | Deferred |
| `grid_span_13_*.html` (2 tests) | 13 track types + spanning | Investigate — may be solvable |
| `xgrid_fr_span_2_*.html` | Browser spec deviation | Deferred — browser disagrees with spec |

### Baseline Flex/Grid Failures (10 position-only tests)

| Test | Issue | Fix |
|------|-------|-----|
| `flex_010_wrap_reverse.html` | `wrap-reverse` y offset | Cross-axis reversal needs baseline offset adjustment |
| `grid_017_masonry_like.html` | Grid auto-placement order | Dense auto-flow placement |
| `grid_117_order.html` | CSS `order` property in grid | Grid item order property |
| `yoga-flexwrap-wrap-reverse-column-fixed-size.html` | Column wrap-reverse x | Same as flex_010 but column direction |
| `position-relative-010.html` | Relative positioning with float | Float + relative interaction |
| `table-caption-003.htm` | Caption y position | Caption placement |
| `table-footer-group-*.htm` (3 tests) | tfoot ordering | thead/tfoot visual order |
| `text-indent-inherited-001.htm` | text-indent inheritance | text-indent on nested blocks |

---

## 12. Phase 3I: Performance — Depth Guard for Exponential Blowup

**Impact: Fixes hang on `contact-form.html` and other complex pages**
**Effort: Small-Medium**
**Risk: Low**

### The Problem

Nested flex/grid measurement creates exponential layout calls:

```
layout-flex (depth 0)
  → measure-flex-item-content (child 1)
    → layout-flex (depth 1)
      → measure-flex-item-content (child 1.1)
        → layout-flex (depth 2)
          → ... O(k^d) calls
```

### Solution: Depth-Limited Measurement with Caching

#### Option A: Depth Limit (Simple)

```racket
(define max-layout-depth 50)
(define current-layout-depth (make-parameter 0))

(define (layout box avail)
  (when (> (current-layout-depth) max-layout-depth)
    (error 'layout "depth limit exceeded: possible infinite recursion"))
  (parameterize ([current-layout-depth (add1 (current-layout-depth))])
    ;; ... existing dispatch
    ))
```

#### Option B: Measurement Memoization (Better)

Cache measurement results so nested flex containers don't re-measure the same subtree:

```racket
(define measurement-cache (make-hash))

(define (measure-flex-item-content item avail-cross)
  (define key (list (box-id item) avail-cross))
  (hash-ref! measurement-cache key
    (lambda () (dispatch-fn item ...))))
```

#### Recommended: Option A + B Combined

Add a depth guard as a safety net, plus memoization for performance. This prevents hangs and reduces exponential blowup to linear.

---

## 13. Implementation Priority & Milestones

### Phase 3A: Root Element Margin (Quick Win)
- **Estimated tests gained:** ~30
- **Effort:** 1 hour
- **Dependencies:** None
- **Files:** `layout-dispatch.rkt`

### Phase 3B: Font-Size Zero + Font Measurement
- **Estimated tests gained:** ~31
- **Effort:** 2-3 hours
- **Dependencies:** None
- **Files:** `layout-dispatch.rkt`, `layout-block.rkt`

### Phase 3C: Float Layout
- **Estimated tests gained:** ~60-80 (not all 98 — some need IFC too)
- **Effort:** 2-3 days
- **Dependencies:** None (but benefits from IFC for text-around-float)
- **Files:** `layout-block.rkt` (major), `layout-dispatch.rkt`, `layout-common.rkt`

### Phase 3D: Table Layout Improvements
- **Estimated tests gained:** ~40-60
- **Effort:** 2-3 days
- **Dependencies:** None
- **Files:** `layout-dispatch.rkt` (new `layout-table`), `layout-common.rkt`

### Phase 3E: List-Item Display
- **Estimated tests gained:** ~14
- **Effort:** 3-4 hours
- **Dependencies:** None
- **Files:** `layout-dispatch.rkt`, `reference-import.rkt`

### Phase 3F: Child-Count / Inline Text Nodes
- **Estimated tests gained:** ~12
- **Effort:** 4-6 hours
- **Dependencies:** Partially needs IFC
- **Files:** `reference-import.rkt`

### Phase 3G: Block Width/Height Edge Cases
- **Estimated tests gained:** ~30
- **Effort:** 1-2 days
- **Dependencies:** Some depend on 3C (float BFC height) and 3E (list-item width)
- **Files:** `layout-block.rkt`, `layout-common.rkt`, `layout-dispatch.rkt`

### Phase 3I: Performance (Depth Guard)
- **Estimated tests gained:** 0 (prevents hangs)
- **Effort:** 1-2 hours
- **Dependencies:** None
- **Files:** `layout-dispatch.rkt`

### Recommended Execution Order

```
3A (root margin)    ── 30 tests ── 1 hr    ── cumulative: ~1571/1825 (86.1%)
    │
3B (font-size)      ── 31 tests ── 3 hrs   ── cumulative: ~1602/1825 (87.8%)
    │
3I (depth guard)    ──  0 tests ── 2 hrs   ── fixes page suite hangs
    │
3E (list-item)      ── 14 tests ── 4 hrs   ── cumulative: ~1616/1825 (88.5%)
    │
3G (width/height)   ── 30 tests ── 1 day   ── cumulative: ~1646/1825 (90.2%)
    │
3C (float layout)   ── 70 tests ── 3 days  ── cumulative: ~1716/1825 (94.0%)
    │
3D (table layout)   ── 50 tests ── 3 days  ── cumulative: ~1766/1825 (96.8%)
    │
3F (child-count)    ── 12 tests ── 6 hrs   ── cumulative: ~1778/1825 (97.4%)
```

### Projected Final Baseline Status

| Category | Tests | Fixable | Deferred |
|----------|-------|---------|----------|
| Root margin | 30 | 30 | 0 |
| Font-size zero | 23 | 23 | 0 |
| Float layout | 63 | ~55 | ~8 (need IFC) |
| Table layout | 75 | ~50 | ~25 (complex edge cases) |
| List-item | 11 | 11 | 0 |
| Width edge cases | 24 | ~18 | ~6 (text-transform, writing-mode) |
| Height edge cases | 19 | ~15 | ~4 (line-height: 0, complex) |
| Position issues | 10 | ~6 | ~4 (writing-mode, dense auto-flow) |
| Mixed | 23 | ~15 | ~8 (IFC, block-in-inline) |
| Child-count | 5 | 5 | 0 |
| **Total** | **284** | **~228** | **~56** |

**Projected baseline pass rate: ~1769/1825 (96.9%)**

---

## 14. Known Limitations / Deferred

### Deferred to Phase 4

| Feature | Tests Blocked | Reason |
|---------|--------------|--------|
| `writing-mode: vertical-lr` | 6 (3 flex + 3 grid) | Cross-cutting: requires axis swap in all layout modes |
| Full Inline Formatting Context | ~15 | Complex: inline box splitting, baseline alignment, vertical-align |
| Non-Ahem font precision | ~5 | Would need a real font shaping engine |
| `white-space: pre` text nodes | 3 | Requires two-pass HTML parsing (CSS-aware text preservation) |
| `line-height: 0` negative overflow | 2 | Edge case: text rendered above container top |
| Browser spec deviations | 1 | Grid fr sub-1 sum — browser behavior differs from spec |

### Tests That Cannot Pass Without Major Architecture Changes

| Test | Requires |
|------|----------|
| `box_006_text_align.html` | Full IFC with inline elements sharing lines |
| `block-formatting-contexts-008.htm` | Float + BFC avoidance |
| `floats-129.htm` through `floats-131.htm` | Complex float stacking with inline content |
| `grid_span_13_*.html` | 13-track-type interaction (spec ambiguity) |

---

## 15. Appendix A: All Failing Tests by Category

### Float Layout (63 baseline + 34 position + 1 basic = 98 total)

<details>
<summary>Click to expand full list</summary>

**Baseline (63):**
- `baseline_806_float_left.html`
- `baseline_814_clear_float.html`
- `clear-001.html`
- `float-applies-to-001.htm` through `float-applies-to-015.htm` (15 tests)
- `float-non-replaced-width-001.htm` through `-015.htm` (15 tests)
- `floats-020.htm`, `floats-028.htm`, `floats-029.htm`, `floats-030.htm`
- `floats-rule3-outside-left-001.htm`, etc.
- And ~25 more float/clear-related tests

**Position (34):** All 34 failures in the position suite are float-related.

</details>

### Table Layout (75 baseline + 1 basic = 76 total)

<details>
<summary>Click to expand full list</summary>

- `fixed-table-layout-001.htm`
- `table-001.html` through `table-014.html`
- `table-anonymous-block-004.htm`
- `table-caption-003.htm`
- `table-footer-group-002.htm`, `-004.htm`
- Various `table-layout-applies-to-*.htm`
- And ~50 more table-related tests

</details>

### Root Margin (16 baseline + 1 basic + 13 cross-suite = 30 total)

<details>
<summary>Click to expand full list</summary>

- `baseline_803_basic_margin.html` (x=0, expected=20)
- `baseline_808_position_relative.html` (x=0, expected=10)
- `baseline_809_text_align.html` (x=0, expected=10)
- `baseline_810_overflow.html` (x=0, expected=10)
- `baseline_811_basic_border.html` (x=0, expected=30)
- `baseline_812_background_color.html` (x=0, expected=30)
- `baseline_816_line_height.html` (x=0, expected=40)
- `block_001_margin_padding.html` (x=0, expected=10)
- `box_002_margin_padding.html` (x=0, expected=20)
- `position-relative-007.html` (x=0, expected=96)
- `position-relative-008.html` (x=0, expected=96)
- `yoga-flexdirection-flex-direction-row-reverse-margin-left.html` (x=0, expected=100)
- `yoga-gap-row-gap-percent-wrapping-with-content-margin-and-padding.html` (x=0, expected=10)
- `yoga-gap-row-gap-percent-wrapping-with-content-margin.html` (x=0, expected=10)
- `yoga-justifycontent-justify-content-row-max-width-and-margin.html` (x=0, expected=100)
- `yoga-justifycontent-justify-content-row-min-width-and-margin.html` (x=0, expected=100)
- `abspos-001.html` (x=0, expected=16 — body padding)
- `baseline_815_font_size.html` (x=0, expected=30)
- `baseline_820_inline_block.html` (x=0, expected=20)
- `issue_flex_crosssize_001.html` (x=0, expected=20)
- `issue_grid_basic_001.html` (x=0, expected=20)

</details>

---

## 16. Appendix B: Progress Log

### Phase 2 Final Status (Feb 17, 2026)

**1541/1825 baseline (84.4%), 2046/~2479 cross-suite (~82.5%)**

Key achievements from Phase 2:
- JSON-computed-style import enabling `<style>` block tests
- 98.1% flex suite (153/156)
- 95.1% grid suite (117/123)
- 88.3% box suite (68/77)
- 100% text_flow suite (14/14)
- Dual font metric system (Times + Arial)
- Block-in-inline strut heights
- Inline `::before` content width
- Percentage margin/padding resolution
- Grid fr freeze algorithm
- Flex min/max main-axis re-resolve
- Aspect-ratio in positioned layout

### Identified Bugs

1. **Root element margin not applied** — `layout-document` doesn't position root view (~30 tests)
2. **Font-size: 0 ignored** — text always renders at 16px (~23 tests)
3. **Exponential blowup on nested flex** — page suite hangs on `contact-form.html`
4. **Float layout incomplete** — only basic left-float tracking (~98 tests)
5. **Table layout minimal** — equal-width cells only (~76 tests)
