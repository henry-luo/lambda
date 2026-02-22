# Radiant CSS 2.1 "applies-to" Test Failure Analysis

## Overview

563 out of ~1,339 "applies-to" tests fail. These tests verify that CSS properties correctly apply (or don't apply) to elements with each of the 15 CSS display types. Each test creates an element with a specific `display` value, applies the CSS property under test, and checks the resulting layout dimensions.

## Distribution by Display Type

Extracted from the `<meta name="assert">` tag of each failing test's HTML file:

| Display Type | Failures | % |
|---|---|---|
| table-caption | 73 | 13.0% |
| inline | 50 | 8.9% |
| inline-table | 48 | 8.5% |
| table-row-group | 45 | 8.0% |
| table-header-group | 45 | 8.0% |
| table-footer-group | 45 | 8.0% |
| table-row | 39 | 6.9% |
| table-cell | 38 | 6.7% |
| table | 37 | 6.6% |
| table-column-group | 35 | 6.2% |
| table-column | 35 | 6.2% |
| **Subtotal: table types** | **440** | **78%** |
| inline-block | 28 | 5.0% |
| list-item | 17 | 3.0% |
| block | 10 | 1.8% |
| run-in | 9 | 1.6% |
| **Subtotal: non-table** | **114** | **20%** |
| unknown | 9 | 1.6% |

## No Single Root Cause — 4 Distinct Categories

### Category 1: Table Layout Dimension Errors (440 tests, 78%)

The dominant category. Table-type elements produce incorrect dimensions when CSS properties affect sizing.

**Failure modes observed:**

1. **Table-caption height = 0** (73 tests). Nearly every CSS property fails for `display: table-caption` because the caption content area consistently computes to height=0. Example: `color-applies-to-015` — Radiant produces table height 0 vs browser 96px for the caption content.

2. **Table cell border not included in dimensions** (~150 tests). When a child element has borders, the table cell dimensions don't reflect them. Example: `padding-applies-to-001` — a `display: table-row-group` wrapping a cell with `border: 10px` inside a 200×200 div. Radiant: cell=200×200; Browser: cell=220×220 (200+10+10 borders).

3. **Inline-table sizing** (48 tests). `display: inline-table` elements produce incorrect dimensions.

4. **Table internal element sizing** (~170 tests across row-group, header-group, footer-group, row, column-group, column). Properties that affect the box model (padding, margin, border-width, min/max-width/height) produce wrong dimensions for these table-internal display types.

**Key code paths:**
- `radiant/layout_table.cpp` → `table_auto_layout()` (line 3968) — column/row sizing
- `radiant/layout_table.cpp` → `layout_table_cell_content()` (line 3355) — cell dimension propagation
- `radiant/layout_table.cpp` → caption handling in `table_auto_layout()` (line 3987)

### Category 2: Property Implementation Bugs (91 tests)

Six properties fail for ALL display types, indicating the property implementations themselves are incorrect:

| Property | Failures | Failure Mode |
|---|---|---|
| letter-spacing | 15 | Text width doesn't include spacing. Example: 16-char text is 176px instead of 192px (missing 16×1px). `layout_text.cpp` has the code but spacing isn't inherited through table context. |
| list-style | 15 | Block with list-style produces height=0. List marker layout doesn't propagate content height. |
| list-style-type | 15 | Same as list-style |
| list-style-position | 15 | Same as list-style |
| list-style-image | 15 | Same as list-style |
| empty-cells | 16 | Empty cells render 26×26 instead of 0×16. The `empty-cells` CSS property isn't suppressing borders/background correctly. |

**Key code paths:**
- `radiant/layout_text.cpp` — letter-spacing application (lines 574, 902, 944)
- `radiant/layout_block.cpp` — list-item marker height propagation
- `radiant/layout_table.cpp` — empty-cells implementation

### Category 3: Inline Element Positioning (50 inline + part of 28 inline-block)

Inline elements frequently fail due to a **ViewSpan y-position bug**:

The `layout_inline()` function in `radiant/layout_inline.cpp` sets:
```cpp
span->y = lycon->block.advance_y;  // line 254
```
This uses the block advance_y at the time the inline element starts, not the final line baseline position. Result: span positioned at y=11 when text is at y=50.

Additional issue: inline border box dimensions don't include borders of the inline element correctly — the bounding box calculation in `compute_span_bounding_box()` needs to account for border/padding of the span itself.

**Key code paths:**
- `radiant/layout_inline.cpp` → `layout_inline()` (line 213) — span y-position
- `radiant/layout_inline.cpp` → `compute_span_bounding_box()` call

### Category 4: Miscellaneous Property Bugs (~30 tests)

- **font-variant** (14 failures): small-caps text width differs (Radiant: 99.2px vs Browser: 74.94px)
- **direction** (12 failures): RTL layout not calculating dimensions correctly
- **text-indent, text-align** (~10 failures): text positioning
- **position/top/right/bottom/left** (~10 failures): positioned elements in table context

## What Makes Tests PASS vs FAIL?

Tests **pass** when:
- The CSS property only affects visual rendering (color, style) without changing dimensions
- AND the element structure dimensions happen to be correct

Example: `border-bottom-color-applies-to-001` (table-row-group) passes because border-color doesn't change dimensions, and the table structure is rendered with correct sizes for this particular test.

Tests **fail** when:
1. The property changes dimensions (padding, margin, width, height) and table layout computes them incorrectly
2. The property implementation is incomplete (letter-spacing, list-style)
3. The element positioning is fundamentally wrong (table-caption height=0, inline span y-position)

## Ahem Font: NOT the Root Cause

Only 153/1,339 (11.4%) applies-to tests use Ahem. Failures occur equally in Ahem and non-Ahem tests. The Ahem font is not a contributing factor to these failures.

## Fix Priority by Impact

| Priority | Fix | Tests Fixed | % of 563 |
|---|---|---|---|
| 1 | Fix table-caption content height (0→correct) | ~73 | 13% |
| 2 | Fix table cell dimension propagation (borders, padding) | ~200 | 35% |
| 3 | Fix list-style height propagation | ~60 | 11% |
| 4 | Fix inline ViewSpan y-position | ~30 | 5% |
| 5 | Fix letter-spacing inheritance/width | ~15 | 3% |
| 6 | Fix empty-cells suppression | ~16 | 3% |
| 7 | Fix inline-table dimensions | ~48 | 8.5% |
| | **Total addressable** | **~442** | **~78%** |

## Properties That Only Fail for Table Types (table-specific bugs)

These properties pass for block/inline-block/list-item/run-in but fail for table display types, confirming the issue is in table layout, not the property:

padding (13), max-width (11), border-right-width (11), border-right-style (11), border-right-color (11), border-right (11), border-left-style (11), border-left-color (11), border-left (11), overflow (10), counter-reset (10), counter-increment (10), border-width (10), max-height (9), line-height (9), width (8), quotes (8), padding-right (8), border-style (8), margin (7), height (6), border (6), border-color (5), padding-top (4), min-height (4), margin-bottom (4), visibility (3), vertical-align (3), font-size (3), border-spacing (3), and 19 more...

## Key Files to Modify

| File | Lines | What to Fix |
|---|---|---|
| `radiant/layout_table.cpp` | 6560 | Table dimension calculation, caption height, empty-cells |
| `radiant/layout_inline.cpp` | 396 | ViewSpan y-position, bounding box with borders |
| `radiant/layout_text.cpp` | ~1000 | Letter-spacing width calculation & inheritance |
| `radiant/layout_block.cpp` | 3500 | List-item marker height propagation |
| `radiant/resolve_css_style.cpp` | ~6000 | Letter-spacing inheritance through table elements |
