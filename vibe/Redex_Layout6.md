# Redex Layout 6 — Improvement Proposal: Line-Height, Margin/Padding, List, Table

> **Baseline:** Phase 1 results — 8,167 tests, 6,676 pass, **81.7%**  
> **Target:** ~86–88% after implementing all items in this proposal  
> **Estimated new passes:** +350–500 tests  
> **Key areas:** Line-height (44 failures), Margin & Padding (153 failures), List (61 failures), Table (109 failures)

---

## Table of Contents

1. [Current Failure Landscape](#1-current-failure-landscape)
2. [Area 1: Line-Height (44 failures → est. +37)](#2-area-1-line-height)
3. [Area 2: Margin & Padding (153 failures → est. +120)](#3-area-2-margin--padding)
4. [Area 3: Lists (61 failures → est. +50)](#4-area-3-lists)
5. [Area 4: Tables (109 failures → est. +60)](#5-area-4-tables)
6. [Implementation Order](#6-implementation-order)
7. [Projected Impact](#7-projected-impact)

---

## 1. Current Failure Landscape

### Summary of the 4 Focus Areas

| Area | Failing | Root Causes | Difficulty |
|------|--------:|-------------|------------|
| Line-Height | 44 | Ahem text wrapping, multi-line stacking, inline vertical-align | Medium |
| Margin & Padding | 153 | RTL positioning (88), margin-collapse edge cases (29), table margin (14), applies-to table-row (22) | Medium-Hard |
| Lists | 61 | Outside marker positioning, marker box generation, Ahem-based marker width | Medium |
| Tables | 109 | Anonymous table box wrapping (33), border-spacing RTL/defaults (24), empty-cells handling (16), table padding (12), caption (8) | Medium-Hard |
| **Total** | **367** | | |

These 367 failures account for **24.7% of all 1,489 remaining failures**. The other major failure group is `first-letter-punctuation` (411 tests, 27.6%) which requires `::first-letter` pseudo-element support and is out of scope here.

---

## 2. Area 1: Line-Height

**Current:** 35/72 pass (48.6%) + 0/5 c548-ln-ht (0%) = 35/77 total  
**Target:** ~72/77 (93.5%)

### 2A: Ahem Text Word-Wrapping on Spaces (est. +30 tests)

**Problem:** The dominant failure pattern (30+ tests) has the signature:

```
text.width: expected=16.0, actual=48.0
```

The test sets `font: 12pt ahem; width: 1em` and contains text `"X X"`. Chrome wraps "X X" into 3 single-character lines at `width: 1em = 16px`. The Redex engine treats "X X" as 48px wide (3 × 16px) and does not wrap it because **Ahem text splitting only happens on zero-width spaces** (`\u200B`), not on regular spaces.

**Root cause:** In `layout-dispatch.rkt`, lines 455–457:

```racket
;; Ahem font: split on zero-width spaces
(define words (string-split content "\u200B"))
```

Regular spaces are not used as break opportunities for Ahem text. For proportional fonts (lines 401–403), wrapping correctly splits on spaces.

**Fix:** In `layout-dispatch.rkt`, change the Ahem word-wrapping path to split on regular spaces (same as proportional), falling back to zero-width space splitting only when no regular spaces exist:

```racket
;; Ahem font: split on spaces (like proportional), or zero-width spaces
(define words
  (let ([space-split (string-split content " ")])
    (if (> (length space-split) 1)
        space-split  ;; normal space-separated words
        (string-split content "\u200B"))))  ;; fallback to ZWSP
```

Also add space-width accounting: for Ahem, a space character has width equal to `font-size` (same as any other glyph), so the space between words in the wrapping calculation should use `ahem-char-width #\space font-size`.

**File:** `test/redex/layout-dispatch.rkt`  
**Effort:** Easy  
**Complexity:** Low — straightforward modification to the word-splitting path

### 2B: Line-Height with Multi-Line Text Stacking (est. +5 tests)

**Problem:** The c548-ln-ht tests set `line-height: 0.5in` (48px) with `font: 24px Ahem` and multi-word text. The failure signature is:

```
div:anon.height: expected=126.0, actual=78.0
div:anon > text.width: expected=72.0, actual=168.0
div:anon > text.height: expected=72.0, actual=24.0
```

The text `" X X X X "` should wrap into multiple lines within a 4em-wide container. The height discrepancy (126 vs 78) and width discrepancy (72 vs 168) indicate wrong line count due to missing text wrapping (2B depends on 2A), combined with the text height not reflecting the explicit `line-height` value across multiple lines.

**Fix:** After fixing 2A (space-based wrapping), the multi-line text height formula `(n-1) × line-height + normal-lh` should produce the correct height. Verify that the existing formula at line 390 works correctly when `line-height > font-size` (half-leading case).

**File:** `test/redex/layout-dispatch.rkt`  
**Effort:** Minimal (likely resolves after 2A)

### 2C: Inline Line-Height Contribution for Non-Text Inline Boxes (est. +2 tests)

**Problem:** Some line-height tests involve nested inline elements where the child's `line-height` differs from the parent's. The current `text-height-from-styles` only applies to text nodes — inline-block and inline elements don't properly contribute their `line-height` to the line box height.

**Fix:** In `layout-inline.rkt`, when stacking inline-block children (lines 196–216), use the child's `line-height` (not just its content height) as the line contribution when `line-height` is explicitly set.

**File:** `test/redex/layout-inline.rkt`  
**Effort:** Easy

---

## 3. Area 2: Margin & Padding

**Current failures:** 153 total  
**Target:** ~33 remaining (est. +120 tests)

### Failure Breakdown

| Subcategory | Count | Root Cause |
|-------------|------:|------------|
| margin-right-* | 48 | RTL block x-positioning |
| padding-right-* | 40 | RTL block x-positioning |
| margin-collapse-* | 18 | Collapse-through empty blocks |
| margin-collapse-clear-* | 11 | Collapse + clearance interaction |
| margin-right-applies-to-* | 12 | RTL + table element margins |
| *-applies-to-{007,008,015} | 14 | Table row/cell margin/padding |
| margin-top/bottom-applies-to-* | 4 | Table row/cell margin |
| padding-*-applies-to-* | 6 | Table cell padding edge |

### 3A: RTL Block Positioning (est. +88 tests)

**Problem:** 48 `margin-right-*` and 40 `padding-right-*` failures all share the same signature:

```
div:anon.x: expected=-5.0, actual=0.0
```

In RTL mode, child blocks should be positioned from the **right edge** of the containing block. The current engine always positions at `x = 0 + margin-left`, ignoring `direction: rtl`.

**Root cause:** `layout-block.rkt` resolves block width correctly for RTL (via `resolve-block-width`), including the auto-margin RTL priority rule, but **does not flip the x-origin** when placing children. The child's `x` should be:

```
child-x = containing-width - margin-right - border-right - padding-right
          - content-width - padding-left - border-left - margin-left
```

Which for a standard LTR block is just `margin-left + border-left + padding-left`, but for RTL with over-constrained margins, the `margin-left` gets the slack (CSS 2.2 §10.3.3).

**Fix:** In `layout-block.rkt`, after `resolve-block-width`, when `direction: rtl`:

1. Detect RTL direction from the **parent's** computed style (RTL is inherited)
2. Compute `child-x` using the resolved right margin: `child-x = containing-width - child-margin-box-width`
3. This handles `margin-right: Npx` tests where the child should be inset from the right

**File:** `test/redex/layout-block.rkt`  
**Effort:** Medium — requires threading direction through child positioning, touching `layout-block-children` and the float/clear paths

### 3B: Margin Collapse Through Empty Blocks (est. +18 tests)

**Problem:** 18 `margin-collapse-*` failures involve margin collapsing through empty elements. CSS 2.2 §8.3.1 defines that an element's own top and bottom margins collapse if it has:

- No border-top/bottom
- No padding-top/bottom  
- No in-flow children that establish a height
- No `min-height` > 0 and no `height` (other than `auto`)
- No clearance

Example failure:

```
margin-collapse-005: div:div4.y: expected=60.0, actual=40.0
margin-collapse-009: div:div2.y: expected=40.0, actual=0.0
```

**Root cause:** The current `collapse-margins` logic in `layout-block.rkt` handles:
- Adjacent sibling top/bottom margin collapse ✓
- Parent first-child top margin collapse ✓  
- Parent last-child bottom margin collapse ✓

But it **does not handle "collapse through"** — when an empty block's top and bottom margins collapse with each other, creating a single margin that then participates in the parent's sibling collapsing chain.

**Fix:** Add a pre-pass in `layout-block-children` that detects empty collapsible blocks (zero height, no border/padding/inline-content) and merges their top+bottom margins into a single collapsed margin. This margin then participates in the normal sibling-to-sibling collapse algorithm.

```racket
(define (block-collapses-through? child)
  (and (zero? (box-model-border-top child-bm))
       (zero? (box-model-border-bottom child-bm))
       (zero? (box-model-padding-top child-bm))
       (zero? (box-model-padding-bottom child-bm))
       (null? (child-boxes child))
       (not (positive? (get-style-prop child-styles 'height 0)))
       (not (positive? (get-style-prop child-styles 'min-height 0)))))
```

**File:** `test/redex/layout-block.rkt`  
**Effort:** Medium — margin collapse is already complex; adding collapse-through requires careful integration with existing logic

### 3C: Margin Collapse + Clearance Interaction (est. +8 tests)

**Problem:** 11 `margin-collapse-clear-*` failures. When a clearing element is present, clearance prevents margin collapse between the cleared element and its predecessor. The typical signature:

```
margin-collapse-clear-000: div:anon.height: expected=152.0, actual=102.0
                           div:anon > div:anon.y: expected=101.0, actual=51.0
```

**Root cause:** The cleared element's top margin and the preceding element's bottom margin should **not** collapse when clearance is applied (CSS 2.2 §8.3.1), but the current implementation may still be collapsing them, or not accounting for clearance in the final `y` position.

**Fix:** In the clear/float handling path (around line 789 in `layout-block.rkt`), ensure that when clearance is computed (`cleared-y > current-y`), the collapsed margin between the cleared element and its predecessor is set to zero (clearance breaks the adjoining relationship).

**File:** `test/redex/layout-block.rkt`  
**Effort:** Medium — requires careful modification of the clearance+collapse interaction

### 3D: Table Element Margin/Padding (est. +6 tests)

**Problem:** The `*-applies-to-007`, `*-applies-to-008`, and `*-applies-to-015` tests verify that margins/padding are correctly applied (or not) to table-internal elements:

- **Table rows** (`display: table-row`): margins do NOT apply (CSS 2.2 §17.5.3)
- **Table cells** (`display: table-cell`): margins do NOT apply, padding DOES
- **Table elements** (`display: table`): margins and padding both apply

Failure signature (applies-to-007): cell width expected=220, actual=120, suggesting margins on the table element are not expanding the cell.

**Fix:** In `reference-import.rkt`, when processing table-internal elements, explicitly zero out margins for `table-row` and `table-cell` display types. Ensure table-level padding is computed at the `table` box level, not pushed down to cells.

**File:** `test/redex/reference-import.rkt`  
**Effort:** Easy

---

## 4. Area 3: Lists

**Current:** 55/116 pass (47.4%)  
**Target:** ~105/116 (90.5%)

### Failure Breakdown

| Subcategory | Count | Root Cause |
|-------------|------:|------------|
| list-style-position | 14 | Outside marker not generated |
| list-style-applies-to | 11 | No marker for table-cell display |
| list-style-type-applies-to | 11 | No marker type rendering |
| list-style-image-applies-to | 11 | No list-style-image |
| list-style-position-applies-to | 11 | No marker outside positioning |
| list-style-type-armenian | 1 | Non-disc marker type |
| list-style-image-available | 1 | Image marker |
| bidi-list | 1 | RTL list marker |

### 4A: Outside List Marker Generation (est. +14 tests)

**Problem:** The `list-style-position-*` tests verify that `list-style-position: outside` places the marker to the left of the content area. The current importer only handles `inside` markers (by injecting `__list-marker-inside-width` into styles). Outside markers are completely ignored — the list-item is treated as a plain block.

**Root cause:** In `reference-import.rkt` lines 3155–3170, the `list-item` display handler only processes `inside` markers. When `list-style-position` is `outside` (the CSS default), no marker box is generated.

**Fix:** For outside markers, generate a **negative-offset marker box** as a child of the list-item block. The marker should be positioned to the left of the content area with a negative `margin-left`:

```racket
;; For list-style-position: outside (or default)
;; Insert a marker child box before the content children:
(define marker-text "• ")  ;; disc marker
(define marker-w (* fs 1.375))
(define marker-box
  `(block "__marker" 
    (style (display block)
           (position absolute)
           (left ,(- marker-w))
           (top 0)
           (width ,marker-w)
           (font-size ,fs))
    ((text "__marker-text" (style (font-size ,fs)) ,marker-text ,marker-w))))
```

Alternatively, encode the marker as a style hint (`__list-marker-outside-width`) and handle positioning in `layout-block.rkt` by offsetting x to the left of the content edge.

**File:** `test/redex/reference-import.rkt`, potentially `test/redex/layout-block.rkt`  
**Effort:** Medium

### 4B: List Marker Applies-To for Non-List Elements (est. +33 tests)

**Problem:** The `list-style-*-applies-to-*` tests verify that `list-style-*` properties only apply to elements with `display: list-item`. For non-list elements (table-cell, block, inline-block), the property should be ignored. Current failures show height=0 for table elements that should have content height regardless of list-style properties.

**Root cause:** The failure signature `div:table.height: expected=18.0, actual=0.0` suggests that these tests' tables are getting zero height. This is likely a separate table height issue (empty table-cell with content not getting height), not directly about list-style.

**Fix:** These tests overlap with the table area — the root issue is likely that table cells with text content are computing `height: 0`. Fix the table height algorithm for cells with inline content (see Section 5). The list-style property is a red herring for most of these failures.

**File:** `test/redex/layout-table.rkt`  
**Effort:** Addressed by table fixes (Area 4)

### 4C: Marker Width by Type (est. +3 tests)

**Problem:** Different `list-style-type` values produce different marker widths:

| Type | Marker | Approx Width |
|------|--------|-------------|
| disc (default) | • | ~0.5em + space |
| circle | ◦ | ~0.5em + space |
| square | ▪ | ~0.5em + space |
| decimal | 1. / 2. / ... | varies |
| lower-alpha | a. / b. / ... | ~1em + space |
| armenian | Ա. / Բ. / ... | ~1.5em + space |

The current `marker-w = fs × 1.375` is hardcoded for disc. Other types need different widths.

**Fix:** Add a marker-width lookup table in the importer:

```racket
(define (marker-width-for-type type fs)
  (case type
    [(disc circle square) (* fs 1.375)]
    [(decimal) (* fs 1.5)]
    [(lower-alpha upper-alpha lower-roman upper-roman) (* fs 2.0)]
    [(armenian georgian) (* fs 2.5)]
    [else (* fs 1.375)]))
```

**File:** `test/redex/reference-import.rkt`  
**Effort:** Easy

---

## 5. Area 4: Tables

**Current:** 262/349 pass (75.1%)  
**Target:** ~322/349 (92.3%)

### Failure Breakdown

| Subcategory | Count | Root Cause |
|-------------|------:|------------|
| table-anonymous-objects | 27 | Missing anonymous row/table wrapping |
| border-spacing | 24 | RTL cell x-offset, default value, 2-value syntax |
| empty-cells-applies-to | 16 | Empty cell hiding not applied |
| table-anonymous-block | 6 | Anonymous block wrapping within table |
| caption-side-applies-to | 6 | Caption positioning below table |
| table-valign | 2 | Vertical alignment in cells |
| table-column-rendering | 2 | Column width distribution |
| run-in-contains-table | 2 | Run-in + table interaction |
| Other | 24 | Mixed edge cases |

### 5A: Anonymous Table Box Generation Enhancement (est. +20 tests)

**Problem:** 27 `table-anonymous-objects-*` failures and 6 `table-anonymous-block-*` failures. The importer already has `wrap-table-internal-children` (lines 3025–3045) and `make-anon-table-box` (lines 3046+), but the wrapping is incomplete.

**Missing cases per CSS 2.2 §17.2.1:**

1. **Row wrapping for orphan cells:** A `table-cell` directly inside a `table` (without `table-row`) needs an anonymous `table-row` wrapper. The current `make-anon-table-box` handles this, but may miss cells that are interspersed with non-table content.

2. **Table wrapping for orphan rows:** A `table-row` or `table-row-group` outside any `table` needs an anonymous `table` wrapper. This is handled by `wrap-table-internal-children`, but the signature suggests child-count mismatches (`expected=2, actual=3`), indicating extra anonymous wrappers where none are needed, or missing wrappers where they are.

3. **Consecutive run detection:** Non-table-internal content between table-internal children should break up the consecutive runs. The current implementation does this, but edge cases with whitespace-only text nodes between cells may cause spurious extra runs.

**Fix:**

1. Add whitespace-only text node filtering in `wrap-table-internal-children` — text nodes containing only whitespace between table-internal children should be ignored per CSS 2.2 §17.2.1 ("Anonymous boxes are generated as needed to fix up the document tree").

2. Ensure `table-row-group` elements (`<tbody>`, `<thead>`, `<tfoot>`) are recognized as table-internal and properly wrapped:

```racket
(define (is-table-internal-box? box)
  (match box
    [`(block ,_ ,s ,_)
     (let ([d (get-style-prop s 'display #f)])
       (member d '(table-row table-row-group table-header-group
                   table-footer-group table-cell table-caption
                   table-column table-column-group)))]
    [_ #f]))
```

3. Handle the case where a `table-caption` is inside a wrapped anonymous table — captions should be children of the table, not the row-group.

**File:** `test/redex/reference-import.rkt`  
**Effort:** Medium

### 5B: Border-Spacing RTL and Defaults (est. +18 tests)

**Problem:** 24 `border-spacing-*` failures. The typical failure signature shows `x: expected=97.0, actual=0.0` — cells are positioned at x=0 instead of being offset by border-spacing from the right edge (RTL tests), or `x: expected=192, actual=0` (large spacing values).

**Sub-issues:**

1. **RTL cell positioning** (12 tests): In `border-collapse: separate` mode with `direction: rtl`, cells should be positioned right-to-left. The first cell starts at `x = table-width - cell-width - border-spacing`, the next at `x = table-width - 2*cell-width - 2*border-spacing`, etc.

2. **Default border-spacing** (6 tests): When `border-spacing` is not explicitly set, the default is `2px` (CSS initial value). Some tests rely on this default, but the engine may be defaulting to `0`.

3. **Two-value syntax** (3 tests): `border-spacing: 5px 10px` sets horizontal and vertical spacing separately. The current parser may only handle single-value syntax.

4. **Negative border-spacing rejection** (3 tests): `border-spacing: -5px` is invalid per CSS 2.2 — should be ignored (treated as `0`).

**Fix:**

1. In `layout-table.rkt`, add RTL cell positioning: when `direction: rtl`, reverse the x-calculation for each cell in a row.

2. In `reference-import.rkt`, set `border-spacing: 2px 2px` as the default for table elements.

3. Parse two-value `border-spacing` syntax: `border-spacing: Hpx Vpx` → `(border-spacing-h H) (border-spacing-v V)`.

4. Reject negative values: `(when (negative? val) (set! val 0))`.

**Files:** `test/redex/layout-table.rkt`, `test/redex/reference-import.rkt`  
**Effort:** Medium

### 5C: Empty-Cells Handling (est. +12 tests)

**Problem:** 16 `empty-cells-applies-to-*` failures. CSS 2.2 §17.6.1.1: when `empty-cells: hide`, empty cells should have their borders and backgrounds hidden (but the cell still occupies space in the table layout). The failure signature:

```
div:table.width: expected=32.0, actual=48.0
div:table > div:anon.child-count: expected=2.0, actual=3.0
```

**Root cause:** `empty-cells` is parsed in the importer but not acted upon. Empty cells are still rendered with full borders, contributing extra width. Additionally, some test cells have `display: list-item` with `empty-cells: hide`, which should be ignored because `empty-cells` only applies to `table-cell` elements.

**Fix:**

1. In the importer, when a `table-cell` has `empty-cells: hide` and no child content, set its `border-width` to 0 on all sides and mark it invisible.

2. Ensure `empty-cells` is NOT applied to non-table-cell elements (list-item, block, etc.).

3. Define "empty" per CSS 2.2: no content, no `::before`/`::after` content, and no `&nbsp;` text.

**File:** `test/redex/reference-import.rkt`, `test/redex/layout-table.rkt`  
**Effort:** Easy-Medium

### 5D: Caption Positioning (est. +5 tests)

**Problem:** 6 `caption-side-applies-to-*` failures. Table captions should be positioned above (default) or below the table grid. The `caption-side: bottom` places the caption below all rows.

**Fix:** In `layout-table.rkt`, check `caption-side` property and position caption boxes either before (top) or after (bottom) the table row-group during table height calculation.

**File:** `test/redex/layout-table.rkt`  
**Effort:** Easy

### 5E: Table Cell Height for Inline Content (est. +5 tests)

**Problem:** Multiple `*-applies-to-*` tests show `div:table.height: expected=18.0, actual=0.0`. Table cells containing text content are computing zero height.

**Root cause:** The table height algorithm may not be running inline layout for cells whose children are text nodes. The cell height should be at least `line-height × number-of-lines`.

**Fix:** Ensure that table cells containing text nodes run inline layout to compute the cell's intrinsic height. If a cell has no block children but has text, the cell height should be at least the text's computed line-height.

**File:** `test/redex/layout-table.rkt`  
**Effort:** Easy

---

## 6. Implementation Order

Tasks are ordered by dependency chain and impact. Independent tasks can be parallelized.

### Phase A: Text Wrapping & RTL Foundation (est. +118 tests)

These two fixes are independent of each other and can be done in parallel. Together they address the most failures.

| Step | Task | Tests | Effort | Files |
|------|------|------:|--------|-------|
| A1 | Ahem text space-wrapping | +30 | Easy | layout-dispatch.rkt |
| A2 | RTL block x-positioning | +88 | Medium | layout-block.rkt |

### Phase B: Margin Collapse (est. +26 tests)

Depends on: nothing (independent of A)

| Step | Task | Tests | Effort | Files |
|------|------|------:|--------|-------|
| B1 | Collapse-through empty blocks | +18 | Medium | layout-block.rkt |
| B2 | Clearance + collapse interaction | +8 | Medium | layout-block.rkt |

### Phase C: Table Improvements (est. +60 tests)

Depends on: A2 (RTL positioning needed for border-spacing RTL)

| Step | Task | Tests | Effort | Files |
|------|------|------:|--------|-------|
| C1 | Anonymous table box wrapping fixes | +20 | Medium | reference-import.rkt |
| C2 | Border-spacing RTL + defaults | +18 | Medium | layout-table.rkt, reference-import.rkt |
| C3 | Empty-cells handling | +12 | Easy | reference-import.rkt, layout-table.rkt |
| C4 | Caption positioning | +5 | Easy | layout-table.rkt |
| C5 | Table cell inline height | +5 | Easy | layout-table.rkt |

### Phase D: List Markers (est. +50 tests)

Depends on: C5 (some list tests are actually table height issues)

| Step | Task | Tests | Effort | Files |
|------|------|------:|--------|-------|
| D1 | Outside marker generation | +14 | Medium | reference-import.rkt, layout-block.rkt |
| D2 | Marker type width lookup | +3 | Easy | reference-import.rkt |
| D3 | List-applies-to via table fix | +33 | Free | Addressed by C5 |

### Phase E: Line-Height Polish (est. +7 tests)

Depends on: A1 (multi-line text stacking depends on wrapping fix)

| Step | Task | Tests | Effort | Files |
|------|------|------:|--------|-------|
| E1 | Multi-line stacking height | +5 | Easy | layout-dispatch.rkt |
| E2 | Inline line-height contribution | +2 | Easy | layout-inline.rkt |

---

## 7. Projected Impact

### Per-Phase Projection

| After Phase | New Passes | Cumulative Pass | Rate |
|-------------|----------:|----------------:|-----:|
| **Current** | — | 6,676 | **81.7%** |
| **A** (text wrapping + RTL) | +118 | 6,794 | **83.2%** |
| **B** (margin collapse) | +26 | 6,820 | **83.5%** |
| **C** (tables) | +60 | 6,880 | **84.3%** |
| **D** (lists) | +50 | 6,930 | **84.9%** |
| **E** (line-height polish) | +7 | 6,937 | **84.9%** |
| **Total** | **+261** | **6,937** | **84.9%** |

> **Conservative estimate:** +261 new passes (assuming 10–15% of tests have additional edge cases beyond the primary fix). Optimistic estimate: +350.

### Remaining Failures After This Proposal

After implementing all items, the ~552 remaining failures will be dominated by:

| Category | Est. Remaining | Notes |
|----------|---------------:|-------|
| first-letter-punctuation | ~411 | Requires `::first-letter` support |
| Misc applies-to edge cases | ~40 | Long tail across many properties |
| Content/counter | ~40 | `content: counter()` not implemented |
| Float edge cases | ~25 | Multi-float stacking |
| Abspos containing block | ~10 | Padding-edge resolution |
| Other | ~26 | Various one-off failures |

### Path to 90%

To reach **90%** (7,350/8,167), the additional 413 tests would primarily come from:

1. **`::first-letter` pseudo-element** — unlocks 411 tests (Phase 3 from Layout5 roadmap)
2. This proposal's fixes — unlocks ~261 tests

Combined: 6,676 + 261 + 411 = **7,348 pass (90.0%)**. The `::first-letter` implementation remains the single highest-ROI feature for reaching 90%.
