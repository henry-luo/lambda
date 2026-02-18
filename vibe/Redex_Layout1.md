# Redex Layout — Design, Progress & Continuation Guide

> Formal CSS layout semantics in PLT Redex, serving as a verification oracle for the Radiant C++ engine.

---

## Table of Contents

1. [Key Designs & Program Flow](#1-key-designs--program-flow)
2. [What Has Been Implemented](#2-what-has-been-implemented)
3. [How to Test](#3-how-to-test)
4. [What's Outstanding](#4-whats-outstanding)
5. [How to Proceed in Future Sessions](#5-how-to-proceed-in-future-sessions)

---

## 1. Key Designs & Program Flow

### 1.1 Purpose

The Redex layout model is an **independent, formal specification** of CSS layout written in PLT Redex (a Racket DSL for operational semantics). It serves three goals:

1. **Verification oracle** — compare against Radiant's C++ layout engine to find bugs
2. **Specification** — a precise, executable, readable definition of CSS Block, Flex, Grid, and Positioned layout
3. **Regression testing** — 508 browser reference tests with tolerance-based comparison

### 1.2 Architecture Overview

```
┌──────────────────────────────────────────────────────────────────┐
│                     test/redex/                                  │
│                                                                  │
│  ┌─────────────────────┐    ┌───────────────────────────┐       │
│  │  css-layout-lang.rkt │    │  reference-import.rkt     │       │
│  │  (Redex grammar:     │    │  (HTML + Chrome JSON →    │       │
│  │   Box, Styles, View) │    │   Redex box tree +        │       │
│  └──────────┬──────────┘    │   expected positions)      │       │
│             │               └──────────────┬────────────┘       │
│             ▼                              │                     │
│  ┌─────────────────────┐                   │                     │
│  │  layout-dispatch.rkt │◀─────────────────┘                     │
│  │  (top-level router)  │                                        │
│  └──────┬───┬───┬──────┘                                        │
│         │   │   │                                                │
│    ┌────┘   │   └────┐                                          │
│    ▼        ▼        ▼                                          │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌───────────────┐ ┌─────────────┐  │
│  │block │ │ flex │ │ grid │ │  positioned   │ │  intrinsic  │  │
│  │.rkt  │ │.rkt  │ │.rkt  │ │  .rkt         │ │  .rkt       │  │
│  └──────┘ └──────┘ └──────┘ └───────────────┘ └─────────────┘  │
│    ▲        ▲        ▲        ▲                  ▲              │
│    └────────┴────────┴────────┴──────────────────┘              │
│                       │                                          │
│            ┌──────────┴───────────┐                              │
│            │  layout-common.rkt   │                              │
│            │  (box model, lengths,│                              │
│            │   margins, helpers)  │                              │
│            └──────────────────────┘                              │
│                                                                  │
│  Testing:                                                        │
│  ┌──────────────┐  ┌──────────────────┐  ┌──────────────────┐   │
│  │ test-all.rkt │  │test-differential │  │ compare-layouts  │   │
│  │ (40 unit)    │  │.rkt (508 browser)│  │ .rkt (tolerance) │   │
│  └──────────────┘  └──────────────────┘  └──────────────────┘   │
└──────────────────────────────────────────────────────────────────┘
```

### 1.3 Core Data Model (`css-layout-lang.rkt`, 182 lines)

The Redex `CSS-Layout` language defines three main term types:

#### Box Tree (Input)

```
Box ::= (block BoxId Styles Children)
      | (inline BoxId Styles InlineChildren)
      | (inline-block BoxId Styles Children)
      | (flex BoxId Styles Children)
      | (grid BoxId Styles GridDef Children)
      | (table BoxId Styles TableChildren)
      | (text BoxId Styles string number)        ; content + measured-width
      | (replaced BoxId Styles number number)    ; intrinsic w, h
      | (none BoxId)
```

#### Styles (Resolved CSS Properties)

```
Styles ::= (style StyleProp ...)

StyleProp ::= (width SizeValue) | (height SizeValue)
            | (min-width SizeValue) | (max-width SizeValue)
            | (margin (edges T R B L)) | (padding (edges T R B L))
            | (flex-direction FlexDir) | (flex-wrap FlexWrap)
            | (justify-content JustifyContent) | (align-items AlignItems)
            | ...

SizeValue ::= auto | none | (px N) | (% N) | (em N)
            | min-content | max-content | fit-content
```

#### View Tree (Output)

```
View ::= (view BoxId X Y Width Height (View ...))
       | (view-text BoxId X Y Width Height string)
```

Coordinates are **relative to parent's border-box origin** (same as Radiant).

### 1.4 Layout Dispatch (`layout-dispatch.rkt`, 282 lines)

The `layout` function is the single entry point. It:

1. Checks `position` property — routes `absolute`/`fixed` to `layout-positioned`
2. Pattern-matches on box type:
   - `block` → `layout-block`
   - `flex` → `layout-flex`
   - `grid` → `layout-grid`
   - `inline-block` → `layout-block` (treated as block internally)
   - `text` → measure and wrap
   - `replaced` → use intrinsic sizes
3. Applies `relative` positioning offset as a post-step

The `layout-document` wrapper creates the root available space:
```racket
(define (layout-document root-box viewport-w viewport-h)
  (define avail `(avail (definite ,viewport-w) (definite ,viewport-h)))
  (layout root-box avail))
```

### 1.5 Flex Layout — 9-Phase Algorithm (`layout-flex.rkt`, 943 lines)

The flexbox implementation is the largest and most complex module. It follows CSS Flexbox Level 1 §9 and mirrors Radiant's `layout_flex_multipass.cpp`.

#### Internal Data Structures

```racket
(struct flex-item
  (box styles bm order
   flex-grow flex-shrink flex-basis
   hypothetical-main min-main max-main
   main-size cross-size view)
  #:transparent #:mutable)

(struct flex-line
  (items main-size cross-size free-space)
  #:transparent #:mutable)
```

#### Phase-by-Phase Flow

```
Phase 1: Collect flex items
  ├── Skip display:none and absolute/fixed children
  ├── Resolve flex-grow, flex-shrink, flex-basis
  ├── Handle auto basis → measure content via max-content layout
  ├── Apply box-sizing: border-box → content-box conversion
  └── Compute hypothetical main size (basis clamped by min/max)

Phase 2: Sort by CSS `order` property
  └── Reverse item order for row-reverse / column-reverse

Phase 3: Hypothetical main sizes (computed in Phase 1)

Phase 4: Create flex lines
  ├── nowrap → single line with all items
  └── wrap/wrap-reverse → break lines when cumulative size > main-avail

Phase 5: Resolve flexible lengths (flex-grow / flex-shrink)
  ├── Compute free-space = available - basis_sum - margin/padding/border
  ├── If main-definite?: distribute free space by grow/shrink factors
  ├── If !main-definite: items stay at basis (no grow/shrink)
  └── Clamp each item by min-main / max-main

Phase 6: Determine cross sizes
  ├── Lay out each item with resolved main size
  ├── Set line cross-size = max of item cross sizes
  ├── Stretch items with align-self:stretch (no explicit cross size)
  ├── Expand single-line to cross-avail (for nowrap or stretch)
  └── Re-layout stretched items with new cross size

Phase 7+8: Position items (justify-content + align-items)
  ├── Apply align-content distribution across lines
  │   ├── stretch: distribute cross-free equally to each line
  │   ├── center/end: offset start position
  │   ├── space-between/around/evenly: compute line spacing
  │   └── Handle single-line wrapping vs nowrap distinction
  ├── wrap-reverse: reverse line order for cross-axis positioning
  ├── Per line, apply justify-content (main axis):
  │   ├── flex-start/end, center, space-between/around/evenly
  │   └── Auto-margin absorption on main axis
  ├── Per item, apply align-items/align-self (cross axis):
  │   ├── start, end, center, stretch, baseline (partial)
  │   └── Auto-margin override on cross axis
  └── Compute final (x, y) from main-pos/cross-pos + offsets

Phase 9: Compute final container size
  ├── Row auto-width: content-w = max(content-w, total-main)
  ├── Apply min-height / max-height constraints
  ├── Guard against +inf.0 in final sizes
  ├── Lay out absolute/fixed children against padding box
  └── Construct final View term
```

#### Absolute Children in Flex Containers

Absolute children are excluded from flex flow (Phase 1). They are laid out separately in Phase 9 against the **padding box** of the flex container. Static position fallback uses `justify-content` (main axis) and `align-items` (cross axis) to determine the starting offset.

### 1.6 Block Layout (`layout-block.rkt`, 164 lines)

Implements CSS 2.2 §10 block flow:

1. Resolve content-box width (CSS §10.3.3): explicit → use it; auto → fill available
2. Resolve height: explicit → use it; auto → determined by children
3. Lay out children top-to-bottom with margin collapsing (CSS §8.3.1)
4. Position children: stack vertically with padding/border offset

### 1.7 Common Utilities (`layout-common.rkt`, 303 lines)

Shared infrastructure used by all layout modes:

| Utility | Purpose |
|---------|---------|
| `get-style-prop` | Extract a CSS property from `Styles` term |
| `extract-box-model` | Parse margins/padding/border/box-sizing into struct |
| `resolve-size-value` | Convert `(px N)`, `(% N)`, `auto`, `none` to pixels |
| `resolve-block-width` | CSS §10.3.3 width resolution with min/max |
| `resolve-block-height` | Height resolution with explicit/auto logic |
| `resolve-min-height` | Resolve min-height (content-box), returns 0 if auto |
| `resolve-max-height` | Resolve max-height (content-box), returns +inf.0 if none |
| `collapse-margins` | CSS §8.3.1 margin collapsing |
| `compute-border-box-*` | Convert between content-box and border-box sizes |
| `make-view` | Construct View terms |

### 1.8 Positioned Layout (`layout-positioned.rkt`, 232 lines)

Implements CSS 2.2 §9.3:

- **Absolute**: resolve `top`/`right`/`bottom`/`left` insets, compute width from constraints or shrink-to-fit
- **Relative**: offset view by resolved `top`/`left` (or `bottom`/`right` fallback)
- Handles percentage insets against containing block
- Shrink-to-fit via max-content measurement

### 1.9 Grid Layout (`layout-grid.rkt`, 345 lines)

Implements CSS Grid Level 1:

- Track definition: `(px N)`, `(fr N)`, `(auto)`, `(minmax min max)`
- Explicit item placement via `grid-row`/`grid-column`
- Auto placement for items without explicit positions
- Fr unit distribution after fixed/auto tracks resolved
- Item alignment: justify-items + align-items (stretch, start, end, center)
- Gap support (row-gap, column-gap)

### 1.10 Reference Import Pipeline (`reference-import.rkt`, 772 lines)

Converts browser test data into Redex terms:

```
HTML file (inline styles) ──┐
                            ├──→ Redex Box tree (input)
Chrome reference JSON ──────┤
                            └──→ Expected layout tree (output)
```

Key processing steps:

1. **HTML parsing**: Extract inline `style=""` attributes from `<div>` elements
2. **Style merging**: Overlay inline styles on base defaults (`display:flex`, `box-sizing:border-box`, `position:relative`)
3. **CSS value parsing**: `parse-css-px` (px values), `parse-css-px-or-pct` (px or percentage), handle shorthand properties
4. **Gap handling**: Parse `gap` shorthand into `row-gap` + `column-gap`, support two-value format, filter `"normal"` values
5. **Box tree construction**: Map `display` values to Redex box types, handle `position:absolute`
6. **Reference extraction**: Parse Chrome JSON layout data into expected `(id x y w h children)` tree

### 1.11 Comparison Engine (`compare-layouts.rkt`, 243 lines)

Tree-walk comparator with configurable tolerance:

- **Base tolerance**: 5px absolute
- **Proportional tolerance**: 3% of reference dimension
- **Effective tolerance**: `max(base, proportional × expected)`
- Compares x, y, width, height per element
- Reports per-element pass/fail with path from root

---

## 2. What Has Been Implemented

### 2.1 File Inventory

| File | Lines | Purpose | Status |
|------|-------|---------|--------|
| `css-layout-lang.rkt` | 182 | Redex grammar definition | ✅ Complete |
| `layout-common.rkt` | 303 | Shared utilities, box model | ✅ Complete |
| `layout-dispatch.rkt` | 282 | Top-level layout router | ✅ Complete |
| `layout-block.rkt` | 164 | Block flow layout | ✅ Core complete |
| `layout-flex.rkt` | 943 | Flexbox 9-phase algorithm | 🟡 ~80% complete |
| `layout-grid.rkt` | 345 | Grid track sizing + placement | 🟡 ~60% complete |
| `layout-positioned.rkt` | 232 | Absolute/fixed/relative positioning | 🟡 ~70% complete |
| `layout-intrinsic.rkt` | 205 | Min/max content sizing | ✅ Core complete |
| `layout-inline.rkt` | 167 | Inline + line breaking | ✅ Basic |
| `reference-import.rkt` | 772 | HTML+JSON → Redex converter | ✅ Complete |
| `compare-layouts.rkt` | 243 | Tolerance-based comparison | ✅ Complete |
| `test-all.rkt` | 20 | Unit test runner | ✅ Complete |
| `test-differential.rkt` | 221 | Browser reference test runner | ✅ Complete |
| **Total** | **~3,840** | | |

### 2.2 Layout Features Implemented

#### Block Layout
- ✅ Width resolution (CSS §10.3.3): explicit, auto (fill available), min/max constraints
- ✅ Height resolution: explicit, auto (content-determined)
- ✅ Vertical stacking of children
- ✅ Margin collapsing (CSS §8.3.1)
- ✅ Box-sizing: border-box ↔ content-box conversion
- ✅ Padding and border offset

#### Flexbox Layout
- ✅ All four directions: `row`, `row-reverse`, `column`, `column-reverse`
- ✅ `flex-wrap`: `nowrap`, `wrap`, `wrap-reverse` (line reversal)
- ✅ `justify-content`: `flex-start`, `flex-end`, `center`, `space-between`, `space-around`, `space-evenly`, `start`, `end`
- ✅ `align-items`: `flex-start`/`start`, `flex-end`/`end`, `center`, `stretch` (with min/max clamping)
- ✅ `align-self`: all values including `auto` (inherits `align-items`)
- ✅ `align-content`: `start`, `end`, `center`, `stretch`, `space-between`, `space-around`, `space-evenly` (both single-line wrapping and multi-line)
- ✅ `flex-grow` / `flex-shrink` / `flex-basis` with proper free-space distribution
- ✅ `main-definite?` guard: flex-grow/shrink only applied when main-axis size is definite (prevents 0-width bugs in nested auto-width containers)
- ✅ `order` property sorting
- ✅ Flex line creation with line-break thresholds
- ✅ Min/max main-axis clamping on flex items
- ✅ Auto margins on main axis (absorb free space)
- ✅ Auto margins on cross axis (override align-self)
- ✅ Percentage gap resolution: row-gap against block-size, column-gap against inline-size, indefinite → 0
- ✅ Gap shorthand: two-value format (`"10% 10px"`)
- ✅ Auto-width flex containers: intrinsic width from `total-main`
- ✅ Min-height / max-height on flex container final size
- ✅ Absolute children: laid out against padding box with static position fallback
- ✅ Relative positioning offset after flex positioning

#### Grid Layout
- ✅ Track definitions: `px`, `fr`, `auto`, `minmax(min, max)`
- ✅ Explicit placement: `grid-row`, `grid-column`
- ✅ Auto placement for unpositioned items
- ✅ Fr unit distribution
- ✅ justify-items / align-items: stretch, start, end, center
- ✅ Row-gap / column-gap

#### Positioned Layout
- ✅ Absolute positioning with `top`/`right`/`bottom`/`left` insets
- ✅ Relative positioning offset
- ✅ Width from both-insets constraint (`left + right` set)
- ✅ Shrink-to-fit via max-content measurement
- ✅ Percentage insets
- ✅ Box-sizing conversion

#### Intrinsic Sizing
- ✅ `min-content-width`: block = max of children; inline = widest word; flex = axis-dependent
- ✅ `max-content-width`: content with no wrapping constraint

### 2.3 Test Results Summary

#### Unit Tests: 40/40 (100%)

```
test-block.rkt      — 10 tests (basic block, auto-width, margin collapse, etc.)
test-flex.rkt       — 18 tests (row/column, grow/shrink, wrap, justify, align)
test-grid.rkt       —  6 tests (track sizing, fr units, placement)
test-invariants.rkt —  6 tests (display:none, position identity, nested)
```

#### Browser Reference Tests: 291/508 (57.3%)

- **508 total** tests ported from Taffy/Yoga via Chrome Puppeteer extraction at 1200×800 viewport
- **291 passing** within tolerance (5px base + 3% proportional)
- **217 failing**: 108 grid (require fundamental grid improvements), 109 non-grid

### 2.4 Progress Trajectory

| Milestone | Pass Rate | Key Fixes |
|-----------|-----------|-----------|
| Phase 1 complete | 40/40 unit tests | All layout modes scaffolded |
| Phase 2 initial | 45/508 (8.9%) | Pipeline operational |
| Round 1 | 73/508 (14.4%) | Box-sizing, basic flex |
| Round 2 | 147/508 (28.9%) | Flex grow/shrink, block width |
| Round 3 | 154/508 (30.3%) | Gap support, order property |
| Round 4 | 168/508 (33.1%) | Stretch, positioned elements |
| Round 5 | 192/508 (37.8%) | Intrinsic sizing, reverse |
| Round 6 | 227/508 (44.7%) | Absolute children in flex |
| Round 7 | 251/508 (49.4%) | Auto margins, align-content |
| Round 8 | 257/508 (50.6%) | Percentage flex-basis |
| Round 9 | 272/508 (53.5%) | Multi-line align-content stretch |
| Round 10 | 279/508 (54.9%) | Percentage gap resolution |
| Round 11 | 286/508 (56.3%) | Single-line align-content, min/max |
| Round 12 | **291/508 (57.3%)** | **main-definite? guard, auto-width** |

---

## 3. How to Test

### 3.1 Prerequisites

```bash
# Racket 8.0+ with required packages
raco pkg install redex-lib rackunit-lib

# Ensure Racket is on PATH (macOS Homebrew)
export PATH="/opt/homebrew/bin:/usr/bin:/bin:$PATH"
```

### 3.2 Unit Tests (40 tests, ~5 seconds)

```bash
cd test/redex
racket test-all.rkt
```

Expected output:
```
Running block layout tests...
Block layout tests complete.
Running flexbox layout tests...
Flexbox layout tests complete.
Running grid layout tests...
Grid layout tests complete.
Running invariant tests...
Invariant tests complete.
All tests complete.
```

**Always run unit tests first** after any code change to catch regressions.

### 3.3 Browser Reference Tests (508 tests, ~3 minutes)

```bash
cd test/redex
racket test-differential.rkt 2>/dev/null | tee /tmp/redex_results.txt
```

Useful options:
```bash
# Filter by pattern
racket test-differential.rkt --filter flex

# Limit test count
racket test-differential.rkt --limit 20

# Verbose per-test details
racket test-differential.rkt --verbose

# Custom tolerance
racket test-differential.rkt --base-tolerance 10 --proportional-tolerance 0.05
```

### 3.4 Analyzing Failures

A Python analysis script categorizes failures by root cause:

```bash
python3 /tmp/analyze_failures.py /tmp/redex_results.txt
```

This script reads the `[FAIL]` blocks and produces:
- Failure category counts (width=0, height-off, x-off, y=0, etc.)
- Prefix-based grouping (grid, align, justify, wrap, etc.)
- Sample failure details

### 3.5 Single-Test Debugging

```bash
# Run one test with verbose output
racket test-differential.rkt --filter "flex_grow_height" --verbose

# To debug interactively, use the Racket REPL:
racket
> (require "layout-dispatch.rkt" "reference-import.rkt")
> (define tc (reference-file->test-case
               "../layout/data/baseline/flex_grow_height_maximized.html"
               "../layout/reference/flex_grow_height_maximized.json"))
> (define box (reference-test-case-box-tree tc))
> (define view (layout-document box 1200 800))
> view  ; inspect the output
```

### 3.6 Test Data Locations

| Path | Content |
|------|---------|
| `test/layout/data/baseline/*.html` | 508 HTML test files (Taffy/Yoga ported) |
| `test/layout/reference/*.json` | Chrome-extracted layout reference JSONs |
| `test/redex/tests/*.rkt` | Unit test suites |

---

## 4. What's Outstanding

### 4.1 Remaining Failures: 217 Tests (108 Grid + 109 Non-Grid)

#### Cluster A: `wrap-reverse` Line Ordering — 10 tests

**Symptom**: In `flex-wrap: wrap-reverse`, lines are stacked top-to-bottom instead of bottom-to-top. Items' cross-axis offsets are wrong.

**Tests**: `wrap_reverse_row.html`, `wrap_reverse_column.html`, `wrap_reverse_row_align_content_*.html`, etc.

**Root cause**: The line reversal logic (`ordered-lines = reverse lines`) is in place, but the cross-axis coordinate system isn't flipped. In `wrap-reverse`, the first line should be at the **far end** of the cross axis, not at 0. After reversing line order, `cross-pos` should start from `cross-avail - total-lines-cross` or the coordinate computation should mirror.

**Fix approach**: After computing `ac-start-offset` for the reversed lines, flip the cross positioning so that `cross-pos` starts from the end and moves backward. Or: compute positions normally with reversed lines, then mirror all cross offsets: `final-cross = cross-avail - cross-pos - line-cross`.

#### Cluster B: Baseline Alignment — 10 tests

**Symptom**: Items with `align-items: baseline` or `align-self: baseline` always get `y=0` (treated as `flex-start`).

**Tests**: `align_baseline.html`, `align_baseline_child.html`, `align_baseline_nested_child.html`, `align_self_baseline.html`, etc.

**Root cause**: The `align-baseline` case in `position-flex-items` falls through to `cross-margin-before` (same as `flex-start`). No baseline computation exists.

**Fix approach**: Implement baseline alignment:
1. For each flex line, compute each item's baseline (first child's baseline or item's bottom content edge)
2. Find `max-baseline = max(item_baseline for items with align:baseline)`
3. Offset each baseline-aligned item by `max-baseline - item_baseline`

This is medium-high complexity. Consider a `compute-item-baseline` helper that recurses into child views.

#### Cluster C: Reverse Direction Item Ordering — 9 tests

**Symptom**: In `row-reverse` / `column-reverse`, items appear at wrong positions. Some tests show swapped items, others show `y=+inf`.

**Tests**: `flex_direction_row_reverse.html`, `flex_direction_column_reverse.html`, `justify_content_column_start_reverse.html`, etc.

**Root cause**: Two sub-issues:
1. The reversed-direction `justify-content` swap (`flex-start ↔ flex-end`) may not cover all cases correctly
2. For `column-reverse` with auto height, `main-avail = +inf.0` causes positioning from infinity

**Fix approach**:
- For `column-reverse` with auto height: compute `total-main` first (sum of item sizes), then position from `total-main` backward
- Verify that `effective-justify` handles all justify variants for reversed directions
- Ensure `start`/`end` (CSS alignment) aren't swapped (they're writing-mode relative, not flex-relative)

#### Cluster D: `height=0` on Children — 13 tests

**Symptom**: Children get `height=0` when they should have height via stretch, flex-grow, aspect-ratio, or nested content.

**Tests**: `blitz_issue_88.html`, `align_flex_start_with_stretching_children.html`, `aspect_ratio_flex_*_fill_height.html`, `size_defined_by_child_with_border.html`, etc.

**Root cause**: Multiple sub-issues:
1. Stretch in column direction doesn't propagate definite cross-size to children
2. Aspect-ratio not implemented (width/height derived from ratio)
3. Nested flex containers may not receive definite available sizes from parent stretch

**Fix approach**:
- When a stretched item gets a new cross-size, pass it as a definite available size to child layout
- Implement `aspect-ratio` property: if one axis is definite and ratio is set, compute the other axis
- Ensure `size_defined_by_child_with_*` tests get border/padding correctly subtracted

#### Cluster E: `width=0` on Aspect-Ratio Items — 4 tests

**Symptom**: Items with `aspect-ratio` have `width=0` in row flex containers.

**Tests**: `aspect_ratio_flex_row_fill_width.html`, `chrome_issue_325928327.html`

**Root cause**: `aspect-ratio` is not implemented. Items' widths should be derived from their cross-size × aspect-ratio.

#### Cluster F: Min/Max Constraints Not Applied — 10 tests

**Symptom**: `max-width`, `max-height`, `min-width`, `min-height` are ignored. Items grow past max or stay below min.

**Tests**: `max_width.html`, `max_height.html`, `min_width.html`, `flex_grow_within_max_width.html`, `child_min_max_width_flexing.html`, etc.

**Root cause**: Min/max clamping is applied to the **container** and to **flex item basis**, but not to the **final resolved size** of block children or the **post-grow/shrink flex item size** when laid out as blocks.

**Fix approach**:
- In `layout-block`, apply `min-width`/`max-width` and `min-height`/`max-height` to the final content dimensions
- In flex Phase 6, after laying out items, clamp their view dimensions by min/max
- Ensure `resolve-block-width` and `resolve-block-height` both apply min/max even for auto sizes

#### Cluster G: Absolute Positioning Offsets — 6 tests

**Symptom**: Absolutely positioned elements don't respect `left`/`top`/`bottom`/`right` offsets, or margins on absolute elements are wrong.

**Tests**: `absolute_margin_bottom_left.html`, `percentage_position_left_top.html`, etc.

**Root cause**: Some inset combinations and margin interactions on absolute elements aren't handled correctly.

#### Cluster H: Flex-Grow Height ≈ Expected/2 — 6 tests

**Symptom**: Children get exactly half the expected height in column-direction flex with `flex-grow`.

**Tests**: `bevy_issue_8017.html`, `bevy_issue_8017_reduced.html`, `flex_grow_height_maximized.html`

**Root cause**: Likely a double-subtraction or double-counting of padding/border in the free-space calculation for column direction.

**Fix approach**: Trace the column-direction free-space computation. Check if `main-avail` already has padding subtracted but the grow distribution subtracts it again.

#### Cluster I: `align-items: center` with Min-Height — 6 tests

**Symptom**: Items in a container with `min-height` aren't centered correctly — they use content height instead of the resolved (min-height) height.

**Tests**: `align_items_center_with_min_height_*.html`

**Root cause**: Cross-axis centering uses the content-determined cross-avail instead of the min-height-expanded value.

#### Cluster J: Negative-Space Justify-Content — 4 tests

**Symptom**: When items overflow the container, `justify-content: center/end` should produce negative offsets but produce 0.

**Tests**: `justify_content_column_center_negative_space.html`, etc.

**Root cause**: `(max 0 free-space)` prevents negative offsets in justify-content positioning.

**Fix approach**: Remove `(max 0 ...)` clamping for center and end justify-content, allowing negative offsets when items overflow.

#### Cluster K: Margin Auto — 5 tests

**Symptom**: `margin: auto` doesn't absorb free space correctly.

**Tests**: `xmargin_auto_start_and_end.html`, `xmargin_end.html`, `xmargin_start.html`

#### Cluster L: Grid Layout Issues — 108 tests

All grid tests require improvements to the grid algorithm: align-content, absolute positioning in grid, subgrid, min/max track sizing, percentage sizing, and more. These represent the single largest block of failures.

### 4.2 Feature Matrix — What's Missing

| Feature | Tests Affected | Complexity | Priority |
|---------|---------------|------------|----------|
| `wrap-reverse` cross-axis flip | 10 | Medium | High |
| Baseline alignment | 10 | High | Medium |
| Min/max on block children | 10 | Low | High |
| Reverse direction positioning | 9 | Medium | High |
| `height=0` stretch/aspect-ratio | 13 | Medium-High | High |
| Flex-grow column half-height | 6 | Low | High |
| Negative justify-content offset | 4 | Low | High |
| Aspect-ratio property | 7 | Medium | Medium |
| Auto margin improvements | 5 | Medium | Medium |
| Align-items center + min-height | 6 | Medium | Medium |
| Grid improvements | 108 | Very High | Low (deferred) |

---

## 5. How to Proceed in Future Sessions

### 5.1 Quick-Start Checklist

```bash
# 1. Navigate to the Redex directory
cd test/redex

# 2. Verify unit tests still pass
export PATH="/opt/homebrew/bin:/usr/bin:/bin:$PATH"
racket test-all.rkt

# 3. Run differential tests to get baseline
racket test-differential.rkt 2>/dev/null | tee /tmp/redex_results.txt

# 4. Count pass/fail
head -12 /tmp/redex_results.txt

# 5. Analyze failures
python3 /tmp/analyze_failures.py /tmp/redex_results.txt
```

### 5.2 Recommended Fix Order (Highest Impact First)

**Batch 1 — Low-hanging fruit (est. +20–25 tests)**

1. **Negative justify-content offsets** (4 tests, ~10 min): Remove `(max 0 free-space)` from `center` and `end` cases in `position-flex-items` justify-content section.

2. **Min/max on block children** (10 tests, ~30 min): Ensure `layout-block` applies `min-width`/`max-width` and `min-height`/`max-height` to the final content dimensions, not just when explicit width/height is set.

3. **Flex-grow column half-height** (6 tests, ~20 min): Trace the column-direction free-space to find where padding/border is double-counted. Likely in `resolve-flex-lengths` or `collect-flex-items`.

**Batch 2 — Medium effort (est. +15–20 tests)**

4. **wrap-reverse cross-axis flip** (10 tests, ~45 min): After reversing lines, mirror the cross-axis coordinates. The simplest approach: compute positions normally with reversed lines, then `cross-offset = cross-avail - cross-pos - line-cross` for each line.

5. **Reverse direction fixes** (9 tests, ~30 min): Fix `column-reverse` with auto height (use `total-main` instead of `+inf.0`). Verify all `effective-justify` mappings.

6. **Align-items center + min-height** (6 tests, ~20 min): Use the min-height-expanded container cross-size for centering calculations, not the content-determined one.

**Batch 3 — Larger features (est. +15–20 tests)**

7. **Height=0 stretch propagation** (13 tests, ~60 min): Pass definite cross-size through the layout tree when stretching.

8. **Baseline alignment** (10 tests, ~60 min): Implement `compute-item-baseline` and modify the cross-offset logic.

9. **Aspect-ratio** (7 tests, ~45 min): Add ratio-based dimension derivation for flex items.

### 5.3 Development Workflow

The proven iterative cycle:

```
1. Analyze: python3 /tmp/analyze_failures.py /tmp/redex_results.txt
     └─ Identify the highest-impact cluster

2. Examine: racket test-differential.rkt --filter "test_name" --verbose
     └─ Understand the specific mismatch

3. Fix: edit the relevant .rkt file
     └─ layout-flex.rkt for flex issues
     └─ layout-common.rkt for shared utilities
     └─ layout-block.rkt for block issues
     └─ reference-import.rkt for parsing issues

4. Verify unit tests: racket test-all.rkt
     └─ Must pass 40/40 (no regressions)

5. Verify differential: racket test-differential.rkt 2>/dev/null | tee /tmp/redex_results_N.txt
     └─ Compare pass count to previous run

6. Check for regressions: diff <(grep "\[PASS\]" /tmp/redex_results_old.txt | sort) \
                               <(grep "\[PASS\]" /tmp/redex_results_new.txt | sort)
     └─ Ensure no previously-passing tests now fail

7. Repeat from step 1
```

### 5.4 Key Files to Modify

| Change Type | Primary File | Supporting Files |
|-------------|-------------|------------------|
| Flex algorithm fixes | `layout-flex.rkt` | `layout-common.rkt` |
| Block layout fixes | `layout-block.rkt` | `layout-common.rkt` |
| Positioned layout | `layout-positioned.rkt` | `layout-common.rkt` |
| CSS property parsing | `reference-import.rkt` | `css-layout-lang.rkt` |
| New CSS properties | `css-layout-lang.rkt` | `reference-import.rkt` |
| Grid improvements | `layout-grid.rkt` | `layout-common.rkt` |

### 5.5 Common Pitfalls

1. **Parenthesis balancing in Racket**: Use an editor with rainbow parentheses. One misplaced `)` can silently change semantics.

2. **`define` inside `when`**: Racket allows `define` inside `when` (implicit `begin`), but the scope is limited to that `when` block.

3. **`+inf.0` propagation**: When height is auto and no constraint exists, `+inf.0` can propagate into view sizes. Always guard with `(infinite? x)` checks before using values in final output.

4. **Coordinate system**: All positions are relative to parent's **border-box** origin. Padding/border offsets must be added explicitly when positioning children.

5. **Regression risk**: Fixes to one flex feature often break another. Always run both `test-all.rkt` AND `test-differential.rkt` after changes.

6. **Percentage resolution**: Percentages resolve against the **containing block**, not the element itself. Row-gap % resolves against height (block size); column-gap % resolves against width (inline size).

### 5.6 Analysis Script

The failure analysis script `/tmp/analyze_failures.py` may not persist between sessions. Recreate it as:

```python
#!/usr/bin/env python3
"""Analyze Redex differential test failures by category."""
import sys, re
from collections import defaultdict

def analyze(results_file):
    with open(results_file) as f:
        content = f.read()

    # Extract failure blocks
    fails = re.findall(r'\[FAIL\] (.+?):\n((?:FAIL:.+\n(?:  .+\n)*)*)', content)

    categories = defaultdict(list)
    prefixes = defaultdict(list)

    for name, detail in fails:
        prefix = name.split('_')[0] if '_' in name else name.replace('.html','')
        prefixes[prefix].append(name)

        for line in detail.strip().split('\n'):
            m = re.search(r'\.(\w+): expected=([\d.+inf-]+), actual=([\d.+inf-]+)', line)
            if m:
                prop, expected, actual = m.group(1), m.group(2), m.group(3)
                if actual == '0.0' or actual == '0':
                    categories[f'{prop}=0'].append(name)
                elif expected != actual:
                    categories[f'{prop}-off'].append(name)

    print(f"Total failing: {len(fails)}\n")
    print("=== Failure categories ===")
    for cat, tests in sorted(categories.items(), key=lambda x: -len(x[1])):
        unique = sorted(set(tests))
        print(f"{cat}: {len(unique)} tests")
        for t in unique[:3]:
            print(f"  - {t}")

    print(f"\n=== By prefix (top 15) ===")
    for prefix, tests in sorted(prefixes.items(), key=lambda x: -len(x[1]))[:15]:
        print(f"{prefix}: {len(tests)}")

    print(f"\n=== Sample non-grid failures ===")
    for name, detail in fails[:10]:
        if not name.startswith('grid_'):
            print(f"\n{name}:")
            for line in detail.strip().split('\n')[:4]:
                print(f"  {line.strip()}")

if __name__ == '__main__':
    analyze(sys.argv[1])
```

### 5.7 Target Pass Rates

| Target | Pass Count | Rate | What It Takes |
|--------|-----------|------|---------------|
| Current | 291/508 | 57.3% | — |
| Next milestone | 320/508 | 63% | Fix clusters A, C, F, J (~30 tests) |
| Stretch goal | 350/508 | 69% | + clusters B, D, H, I (~40 more tests) |
| Grid milestone | 400/508 | 79% | Major grid algorithm improvements |
| Complete | 508/508 | 100% | All features + edge cases |

---

*Last updated: February 14, 2026*
*Current pass rate: 291/508 (57.3%)*
