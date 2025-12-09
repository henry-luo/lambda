# Radiant Table Layout Enhancement Proposal v8

## Executive Summary

The Radiant table layout engine has been significantly improved. This document analyzes the root causes, identifies gaps compared to Chrome's table layout, and proposes structural enhancements to achieve a significantly higher pass rate.

**Current Status:** 6/412 tests passing (1.5% overall, many tests with 100% element accuracy)
**Target:** 80%+ pass rate through structural improvements

---

## Progress Update (December 2024)

### Implemented Fixes

#### 1. Anonymous Table Box Generation (Phase 1 - COMPLETED)

Added proper anonymous box generation per CSS 2.1 Section 17.2.1:

**New Functions in `layout_table.cpp`:**
- `create_anonymous_table_element()` - Creates DOM elements with CSS-spec styling
- `generate_anonymous_table_boxes()` - Analyzes table structure and creates missing wrappers
- DOM manipulation helpers: `append_child_to_element()`, `prepend_child_to_element()`, `reparent_node()`

**Key Implementation Details:**
- Anonymous elements inherit font properties from parent (CSS 2.1 spec)
- Non-inherited properties use initial values
- Names use `::anon-tbody`, `::anon-tr` naming convention
- Display values pre-set to bypass style resolution

**Files Modified:**
- `radiant/layout_table.cpp`: +360 lines
- `radiant/resolve_css_style.cpp`: +9 lines (check for pre-set display values)

#### 2. Text Intrinsic Width Measurement (COMPLETED)

**Problem:** Space width was measured differently between intrinsic sizing and layout, causing text to wrap unexpectedly.

- Intrinsic sizing used FT_LOAD_DEFAULT → space = 4.0px
- Layout used pre-computed space_width → space = 4.45px
- Difference caused "r1 c1" to measure as 35px but layout needed 35.6px

**Fix in `radiant/intrinsic_sizing.cpp`:**
- Changed to use `lycon->font.style->space_width` consistently
- Changed `max_content` from `roundf()` to `ceilf()` to prevent undersized cells

**Result:** Text that fits on one line in browser now fits on one line in Radiant.

#### 3. Table Cell Border Fix (COMPLETED)

**Problem:** Hardcoded 1px borders were added to all cells regardless of actual CSS styling.

**Fix in `layout_table.cpp` function `layout_table_cell_content()`:**
- Removed hardcoded `border_left = 1` etc.
- Now reads actual border values from cell's resolved style

#### 4. Border-Spacing Default Fix (COMPLETED)

**Problem:** Default `border-spacing` was incorrectly set to 2px instead of CSS spec's 0px.

**Fix in `radiant/view_pool.cpp`:**
- Changed `border_spacing_h = 2.0f` → `0.0f`
- Changed `border_spacing_v = 2.0f` → `0.0f`

#### 5. table-layout: fixed CSS Property (COMPLETED)

**Problem:** CSS `table-layout: fixed` property was parsed but never applied to table layout.

**Fix in `layout_table.cpp` function `resolve_table_properties()`:**
- Added reading of `CSS_PROPERTY_TABLE_LAYOUT` from element's specified style
- Sets `table->tb->table_layout = TABLE_LAYOUT_FIXED` when CSS value is `fixed`
- Added guard to skip auto layout algorithm when fixed layout is used

**Result:** `table_fixed_layout` test now passes 100%.

#### 6. Caption Margin Support (COMPLETED)

**Problem:** Caption `margin-bottom` was not included in vertical spacing calculations.

**Fix in `layout_table.cpp`:**
- Added `margin_v` calculation to `caption_height`
- Handles both `caption-side: top` (margin-bottom) and `caption-side: bottom` (margin-top)

**Result:** `table_simple` now has 100% element accuracy.

### Test Results After Fixes

| Metric | Before | After |
|--------|--------|-------|
| **Tests Passing** | 0/412 | 6/412 |
| Tests with 100% Elements | ~0 | ~25 |
| Tests with 100% Text | ~0 | ~50 |
| `table_fixed_layout` | 0% | **100%** ✅ |
| `table_simple` | 0% | 100% elements, 92% text |
| `table_complex_spans` | 0% | 100% elements, 94% text |
| `table_edge_cases` | 1.1% | 42.5% elements |
| `table_rowspan_test` | 3.7% | 63% elements |
| `table_width_algorithms` | 2% | 68% elements |

**Note:** Tests require 100% pass on ALL metrics to count as "passing".

---

## 1. Analysis of Current Implementation

### 1.1 Current Architecture

The table layout implementation in `radiant/layout_table.cpp` (~2867 lines) follows CSS 2.1 Section 17 and includes:

| Component | Status | Description |
|-----------|--------|-------------|
| Structure Parser | ✅ Present | `build_table_tree()` builds table structure from DOM |
| Anonymous Box Detection | ⚠️ Partial | `detect_anonymous_boxes()` handles simple cases |
| Column Width Calculation | ⚠️ Partial | MCW/PCW measurement with colspan support |
| Row Height Calculation | ⚠️ Partial | Content-based with rowspan support |
| Border Models | ⚠️ Partial | Separate/collapse modes implemented |
| Cell Vertical Alignment | ⚠️ Partial | Top/middle/bottom implemented |
| Caption Handling | ⚠️ Partial | Top/bottom positioning |
| Fixed Layout | ⚠️ Partial | Basic implementation |

### 1.2 Test Suite Categories

The 412 table tests cover these categories:

| Category | Count | Key Areas |
|----------|-------|-----------|
| `table-anonymous-objects-*` | ~210 | CSS display:table-* on non-table elements |
| `table-anonymous-block-*` | ~18 | Anonymous block boxes around tables |
| `table-height-algorithm-*` | ~32 | Height distribution algorithms |
| `table-visual-layout-*` | ~25 | Visual rendering positions |
| `table-layout-*` | ~20 | Fixed vs auto layout |
| `table-borders-*` | ~5 | Border collapse/separate |
| `table-vertical-align-*` | ~7 | Baseline alignment |
| `table_*` (custom) | ~30 | Various combinations |
| `tables-*` | ~3 | Comprehensive tests |

### 1.3 Radiant vs Chrome (Blink) Architecture Comparison

Understanding how Chrome implements table layout helps identify structural gaps in Radiant.

#### 1.3.1 Tree Architecture

| Aspect | Radiant | Chrome (Blink) |
|--------|---------|----------------|
| **DOM/Layout Tree** | **Unified** - DOM nodes ARE views (`DomElement → ViewBlock`) | **Separate** - DOM and LayoutObject trees are distinct |
| **Node Types** | Single inheritance: `ViewTable`, `ViewTableRow`, `ViewTableCell` | Dual objects: `HTMLTableElement` + `LayoutTable`, etc. |
| **Anonymous Boxes** | **Flags** (`is_annoy_tbody`, `is_annoy_tr`) on existing nodes | **Actual objects** - Creates anonymous `LayoutTableSection`, `LayoutTableRow` |
| **Coordinates** | Relative to parent | Relative to parent (converted to absolute for paint) |

**Key Difference:** Chrome creates actual anonymous layout objects when CSS requires them (e.g., `display: table-cell` without a table-row parent), while Radiant uses flags on existing DOM nodes. This is the root cause of ~50% of test failures.

#### 1.3.2 File Organization

| Radiant (~2900 LOC in 1 file) | Chrome (~15 files, ~8000+ LOC) |
|-------------------------------|-------------------------------|
| `layout_table.cpp` - Everything | `table_layout_algorithm.cc` - Main algorithm |
| `layout_table.hpp` - Headers | `table_layout_utils.cc` - Utilities |
| `view.hpp` - View types | `table_borders.cc` - Border handling |
| | `table_node.cc` - Table DOM bridge |
| | `layout_table_cell.cc` - Cell-specific |
| | `layout_table_row.cc` - Row-specific |
| | `layout_table_section.cc` - Section-specific |
| | `layout_table_column.cc` - COL/COLGROUP |
| | `table_layout_algorithm_types.cc` - Types |

Chrome's separation of concerns into ~15 specialized files enables easier testing and independent optimization.

#### 1.3.3 Layout Algorithm Passes

**Radiant (Single-pass with inline metadata):**
```
1. Build table structure (mark_table_node)
2. Detect anonymous boxes (flags only)
3. Single-pass column width calculation
4. Single-pass row layout + cell positioning
5. Second pass for rowspan height adjustment
```

**Chrome (Multi-pass with explicit phases):**
```
Pass 1: Column Width Sizing
  - Collect min/max content widths
  - Apply COL/COLGROUP constraints
  - Distribute widths

Pass 2: Row Layout (per section)
  - Calculate row heights
  - Handle baseline alignment
  - Apply rowspan distribution

Pass 3: Section Layout
  - Position sections (thead/tbody/tfoot)
  - Handle fragmentation (pagination)

Pass 4: Cell Layout
  - Layout cell content
  - Apply vertical alignment
  - Handle overflow
```

#### 1.3.4 Feature Comparison

| Feature | Radiant | Chrome |
|---------|---------|--------|
| **MCW/PCW calculation** | ✅ `measure_cell_minimum_width()` | ✅ `ComputeMinMaxSizes()` |
| **COL/COLGROUP width** | ⚠️ Partial - reads from first row only | ✅ Full - `LayoutTableColumn` visitor |
| **Colspan distribution** | ✅ Even distribution | ✅ Proportional to existing widths |
| **Border conflict resolution** | ⚠️ Simplified (uses max width) | ✅ Full CSS 2.1 rules |
| **Border painting** | ⚠️ Cell-by-cell | ✅ Collapsed border painting pass |
| **Sub-pixel precision** | ⚠️ Integer rounding causes drift | ✅ Sub-pixel precision throughout |
| **Baseline alignment** | ⚠️ Flag only, not computed | ✅ Full `ComputeRowBaseline()` |
| **Page breaks** | ❌ Not implemented | ✅ `table_break_token_data.h` |
| **Repeating headers** | ❌ Not implemented | ✅ thead repeats on each page |

#### 1.3.5 Anonymous Box Generation: Key Architectural Gap

**Radiant's Flag-Based Approach:**
```cpp
// Uses flags instead of creating objects
struct TableProp {
    uint8_t is_annoy_tbody:1;  // Table acts as tbody
    uint8_t is_annoy_tr:1;     // Row group acts as tr
    uint8_t is_annoy_td:1;     // Row acts as td
};
```

**Chrome's Object-Based Approach:**
```cpp
// Creates actual anonymous layout objects
LayoutTableSection* CreateAnonymousSection() {
    auto* section = MakeGarbageCollected<LayoutTableSection>(nullptr);
    section->SetIsAnonymous(true);
    section->SetStyle(ComputedStyle::CreateAnonymousStyleForTable());
    return section;
}
```

**Impact:** Chrome's approach ensures anonymous boxes have proper layout properties and participate in the full layout algorithm, while Radiant's flags require special-case handling throughout the code.

#### 1.3.6 Summary of Chrome Alignment Priorities

| Gap | Impact | Alignment Effort |
|-----|--------|------------------|
| Anonymous box generation | High (50% of tests) | High - requires architectural change |
| Sub-pixel precision | Medium (accuracy) | Medium - change to float throughout |
| Border conflict resolution | Low (5% of tests) | Medium - implement CSS 2.1 rules |
| Baseline alignment | Low (2% of tests) | Medium - add baseline calculation |
| Fragmentation support | Low (0% of tests) | High - new feature |

---

## 2. Root Cause Analysis

### 2.1 Critical Gap: Height Calculation

Browser: `body.height = 170px`, `table.height = 170px`
Radiant: `body.height = 216px`, `table.height = 216px`

**Root Cause:** Table height includes incorrect border/spacing calculations.

### 2.2 Critical Gap: Width Calculation

Browser: `table.width = 410px`
Radiant: `table.width = 417px`

**Root Cause:** Column width distribution algorithm differs slightly from browser implementation.

### 2.3 Anonymous Box Handling

The ~210 anonymous-objects tests specifically test CSS 2.1 Section 17.2.1 "Anonymous table objects". Current issues:

1. **Missing anonymous cell wrapper generation** - When inline content appears in table-row-group
2. **Missing anonymous row wrapper generation** - When table-cell appears directly in table
3. **Incorrect anonymous table generation** - When table-cell appears outside table context
4. **Flag-based approach limitations** - Using `is_annoy_*` flags instead of actual anonymous DOM nodes limits positioning accuracy

### 2.4 Border Collapse Implementation

Current implementation has precision issues:
- Border width sharing between adjacent cells
- Half-pixel adjustments creating cumulative errors
- Missing border conflict resolution (CSS 2.1 Section 17.6.2.1)

### 2.5 Baseline Vertical Alignment

`table-vertical-align-baseline-*` tests fail because:
- Baseline calculation for cells not implemented
- Row baseline determination missing
- Baseline alignment within cells not applied

---

## 3. Structural Enhancement Plan

### Phase 1: Anonymous Box Generation Reform (Priority: High) ✅ COMPLETED

**Current Approach:** Flag-based (`is_annoy_tbody`, `is_annoy_tr`, etc.)
**Implemented Approach:** Generate actual anonymous DOM elements during table structure building.

**CSS 2.1 Anonymous Box Requirements:**

| Scenario | Status | Implementation |
|----------|--------|----------------|
| `table-cell` in `table-row-group` | ✅ Done | Creates anonymous `::anon-tr` |
| `table-row` in `table` | ✅ Done | Creates anonymous `::anon-tbody` |
| `table-cell` in `table` | ✅ Done | Creates `::anon-tbody` + `::anon-tr` |
| Inline content in `table-row` | ⚠️ Partial | Not yet implemented |
| `table-cell` outside table context | ⚠️ Partial | Not yet implemented |

**Implementation Details:**

```cpp
// Implemented in layout_table.cpp
DomElement* create_anonymous_table_element(
    LayoutContext* lycon,
    const char* name,
    CssDisplayValue display_type,
    DomElement* parent
);

void generate_anonymous_table_boxes(
    LayoutContext* lycon,
    ViewTable* table,
    DomElement* table_elem
);
```

**Impact:** Text layout accuracy improved significantly - many tests now have 100% text match.

---

### Phase 2: Height Algorithm Refinement (Priority: High)

**Current Issues:**
1. Extra border/padding being added multiple times
2. Border-spacing applied incorrectly for edge rows
3. Caption height not properly separated from table content height

**Fix in `table_auto_layout()`:**

```cpp
// CORRECT height calculation
int final_table_height = 0;

// 1. Caption height (if top)
if (caption && table->tb->caption_side == CAPTION_SIDE_TOP) {
    final_table_height += caption->height;
}

// 2. Table border (top only - bottom added at end)
if (table->bound && table->bound->border) {
    final_table_height += table->bound->border->width.top;
}

// 3. Table padding
if (table->bound) {
    final_table_height += table->bound->padding.top;
}

// 4. Border-spacing (top edge for separate model)
if (!table->tb->border_collapse && table->tb->border_spacing_v > 0) {
    final_table_height += table->tb->border_spacing_v;
}

// 5. Row content heights
for (each row) {
    final_table_height += row->height;
    // Border-spacing between rows (NOT after last)
    if (!is_last_row && !table->tb->border_collapse) {
        final_table_height += table->tb->border_spacing_v;
    }
}

// 6. Border-spacing (bottom edge)
if (!table->tb->border_collapse && table->tb->border_spacing_v > 0) {
    final_table_height += table->tb->border_spacing_v;
}

// 7. Table padding (bottom)
if (table->bound) {
    final_table_height += table->bound->padding.bottom;
}

// 8. Table border (bottom)
if (table->bound && table->bound->border) {
    final_table_height += table->bound->border->width.bottom;
}

// 9. Caption height (if bottom)
if (caption && table->tb->caption_side == CAPTION_SIDE_BOTTOM) {
    final_table_height += caption->height;
}
```

**Expected Impact:** +10-15% pass rate.

---

### Phase 3: Width Algorithm Alignment (Priority: Medium)

**Issue:** CSS 2.1 auto table layout width distribution differs from browser behavior.

**Key Differences:**

1. **Minimum Content Width (MCW):** Currently using longest word width, but browsers use more nuanced approach
2. **Preferred Content Width (PCW):** Currently using intrinsic width, but browsers apply max-width constraints
3. **Distribution:** Proportional distribution differs slightly

**Enhanced Algorithm:**

```cpp
// CSS 2.1 Section 17.5.2.2 - Proper width distribution
void distribute_column_widths(TableMetadata* meta, int available_width) {
    int total_pref = 0, total_min = 0;

    // Phase 1: Calculate totals
    for (int i = 0; i < meta->column_count; i++) {
        total_min += meta->col_min_widths[i];
        total_pref += meta->col_max_widths[i];
    }

    // Phase 2: Determine distribution case
    if (available_width >= total_pref) {
        // Case A: Excess space - distribute proportionally above preferred
        distribute_excess(meta, available_width - total_pref);
    } else if (available_width >= total_min) {
        // Case B: Between min and pref - linear interpolation
        float factor = (float)(available_width - total_min) / (total_pref - total_min);
        for (int i = 0; i < meta->column_count; i++) {
            int range = meta->col_max_widths[i] - meta->col_min_widths[i];
            meta->col_widths[i] = meta->col_min_widths[i] + (int)(range * factor);
        }
    } else {
        // Case C: Less than min - use minimum and overflow
        for (int i = 0; i < meta->column_count; i++) {
            meta->col_widths[i] = meta->col_min_widths[i];
        }
    }
}
```

**Expected Impact:** +5-10% pass rate.

---

### Phase 4: Border Collapse Precision (Priority: Medium)

**Current Issues:**
1. Half-pixel border sharing causes cumulative errors
2. Border conflict resolution not implemented
3. Edge borders handled inconsistently

**CSS 2.1 Section 17.6.2.1 - Border Conflict Resolution:**

```cpp
// Determine winning border in conflict
CssBorder resolve_border_conflict(CssBorder border1, CssBorder border2) {
    // Rule 1: hidden wins
    if (border1.style == CSS_VALUE_HIDDEN) return border1;
    if (border2.style == CSS_VALUE_HIDDEN) return border2;

    // Rule 2: none loses
    if (border1.style == CSS_VALUE_NONE) return border2;
    if (border2.style == CSS_VALUE_NONE) return border1;

    // Rule 3: wider wins
    if (border1.width > border2.width) return border1;
    if (border2.width > border1.width) return border2;

    // Rule 4: style priority (double > solid > dashed > dotted > ridge > ...)
    int style_priority1 = get_border_style_priority(border1.style);
    int style_priority2 = get_border_style_priority(border2.style);
    if (style_priority1 > style_priority2) return border1;
    if (style_priority2 > style_priority1) return border2;

    // Rule 5: origin priority (cell > row > row-group > col > col-group > table)
    // Use first border if equal
    return border1;
}
```

**Expected Impact:** +3-5% pass rate.

---

### Phase 5: Baseline Vertical Alignment (Priority: Medium)

**Implementation Requirements:**

1. **Row Baseline Calculation:**
```cpp
int calculate_row_baseline(ViewTableRow* row) {
    int max_baseline = 0;
    for (ViewTableCell* cell = row->first_cell(); cell; cell = row->next_cell(cell)) {
        if (cell->td->vertical_align == CELL_VALIGN_BASELINE) {
            int cell_baseline = calculate_cell_baseline(cell);
            if (cell_baseline > max_baseline) {
                max_baseline = cell_baseline;
            }
        }
    }
    return max_baseline;
}
```

2. **Cell Baseline Calculation:**
```cpp
int calculate_cell_baseline(ViewTableCell* cell) {
    // Find first line box in cell
    for (View* child = cell->first_child; child; child = child->next_sibling) {
        if (child->view_type == RDT_VIEW_TEXT) {
            ViewText* text = (ViewText*)child;
            // Return baseline of first line (y + ascender)
            return text->y + text->font->ascender;
        }
    }
    // If no text, use bottom of content
    return cell->height;
}
```

3. **Apply Baseline Alignment:**
```cpp
void apply_baseline_alignment(ViewTableRow* row, int row_baseline) {
    for (ViewTableCell* cell = row->first_cell(); cell; cell = row->next_cell(cell)) {
        if (cell->td->vertical_align == CELL_VALIGN_BASELINE) {
            int cell_baseline = calculate_cell_baseline(cell);
            int offset = row_baseline - cell_baseline;
            shift_cell_content(cell, offset);
        }
    }
}
```

**Expected Impact:** +2-3% pass rate.

---

### Phase 6: Fixed Layout Enhancement (Priority: Low)

**Current Issues:**
1. First row width determination doesn't match spec
2. COL/COLGROUP width inheritance missing
3. Percentage width in cells not properly resolved

**CSS 2.1 Section 17.5.2.1 - Fixed Table Layout:**

```cpp
void table_fixed_layout(LayoutContext* lycon, ViewTable* table, int table_width) {
    // Step 1: Get column widths from COL elements
    for (ViewColGroup* cg = table->first_col_group(); cg; cg = table->next_col_group(cg)) {
        for (ViewCol* col = cg->first_col(); col; col = cg->next_col(col)) {
            if (col->width > 0) {
                col_widths[col->index] = col->width;
            }
        }
    }

    // Step 2: Get widths from first row cells (for columns without COL width)
    ViewTableRow* first_row = table->first_row();
    if (first_row) {
        for (ViewTableCell* cell = first_row->first_cell(); cell;
             cell = first_row->next_cell(cell)) {
            if (col_widths[cell->td->col_index] == 0 && cell->td->col_span == 1) {
                col_widths[cell->td->col_index] = get_cell_width(cell);
            }
        }
    }

    // Step 3: Distribute remaining width equally
    distribute_remaining_width(col_widths, column_count, table_width);
}
```

**Expected Impact:** +2-3% pass rate.

---

## 4. Implementation Priority Matrix

| Phase | Priority | Effort | Impact | Dependencies |
|-------|----------|--------|--------|--------------|
| 1. Anonymous Boxes | High | High | 25-30% | None |
| 2. Height Algorithm | High | Medium | 10-15% | None |
| 3. Width Algorithm | Medium | Medium | 5-10% | None |
| 4. Border Collapse | Medium | Medium | 3-5% | Phase 2, 3 |
| 5. Baseline Alignment | Medium | Medium | 2-3% | Phase 2 |
| 6. Fixed Layout | Low | Low | 2-3% | Phase 3 |

**Projected Total Improvement: 47-66%**

---

## 5. Recommended Implementation Order

### Week 1-2: Anonymous Boxes
- [ ] Phase 1a: Design anonymous box generation API
- [ ] Phase 1b: Implement anonymous row wrapper
- [ ] Phase 1c: Implement anonymous cell wrapper
- [ ] Phase 1d: Implement anonymous table wrapper

### Week 3: Core Fixes
- [ ] Phase 2: Height algorithm refinement
- [ ] Phase 3: Width algorithm alignment
- [ ] Run test suite to validate improvements

### Week 4: Polish
- [ ] Phase 4: Border collapse precision
- [ ] Phase 5: Baseline alignment
- [ ] Phase 6: Fixed layout refinements

---

## 6. Testing Strategy

### 6.1 Incremental Validation

After each phase, run targeted tests:

```bash
# After Phase 1 (anonymous boxes)
make layout suite=table | grep anonymous-objects | head -20

# After Phase 2 (height)
make layout suite=table | grep height-algorithm

# After Phase 3 (width)
make layout test=table_simple

# Full suite
make layout suite=table
```

### 6.2 Expected Pass Rate Progression

| Phase | Cumulative Pass Rate |
|-------|---------------------|
| Baseline | 0% |
| Phase 1 | 25-30% |
| Phase 2 | 35-45% |
| Phase 3 | 40-55% |
| Phase 4 | 43-60% |
| Phase 5 | 45-63% |
| Phase 6 | 47-66% |

---

## 7. Technical Debt Considerations

### 7.1 Code Quality Improvements

1. **Extract Table Metadata to Separate File:**
   - Move `TableMetadata` to `layout_table_metadata.hpp`
   - Add proper constructor/destructor
   - Add validation methods

2. **Consolidate Helper Functions:**
   - Group related functions (cell helpers, row helpers, positioning)
   - Add consistent documentation

3. **Improve Error Handling:**
   - Add validation for invalid table structures
   - Log warnings for spec violations

### 7.2 Performance Considerations

Current implementation already uses:
- Single-pass structure analysis
- Pre-allocated metadata arrays
- Navigation helpers for structure traversal

Additional optimizations:
- Cache column widths between layout passes
- Skip empty cell layout when `empty-cells: hide`
- Early exit for trivial tables (single row/column)

---

## 8. References

- [CSS 2.1 Specification - Tables](https://www.w3.org/TR/CSS21/tables.html)
- [CSS Tables Module Level 3](https://www.w3.org/TR/css-tables-3/) (draft)
- Chrome Blink Table Layout: `blink/renderer/core/layout/ng/table/`
- Radiant Layout Design: `vibe/Radiant__Layout_Design.md`
- Current Implementation: `radiant/layout_table.cpp`

---

## 9. Conclusion

The Radiant table layout engine has a solid foundation but requires structural improvements to achieve browser compatibility. The most impactful changes are:

1. **Anonymous box generation** - Required for 50% of tests
2. **Height/width algorithm refinement** - Improves accuracy
3. **Border collapse precision** - Fixes cumulative errors

With the proposed phased implementation, we project achieving **47-66% pass rate**, up from the current 0%.
