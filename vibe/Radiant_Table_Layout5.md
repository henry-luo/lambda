# Radiant Table Layout Enhancement Proposal v5

**Date**: November 29, 2025
**Author**: Generated based on comprehensive code and test analysis
**Context**: Post Phases 1-4 implementation, analyzing 430 table tests with 5 passes / 425 failures
**Status**: Proposal for comprehensive structural redesign

---

## Executive Summary

The current table layout implementation has undergone significant refactoring (Phases 1-4) but still shows a **98.8% failure rate** (425/430 tests failing, 5 passing). Analysis reveals the failures stem from several fundamental architectural issues rather than minor calculation bugs.

### Current Test Results
```
Total Tests: 430
✅ Successful: 5 (1.2%)
❌ Failed: 425 (98.8%)
💥 Errors (crashes/NaN): 18
```

### Root Cause Categories

| Category | Test Count | Root Cause |
|----------|-----------|------------|
| Anonymous Box Generation | ~200 | Missing CSS 2.1 anonymous table part creation |
| Width Algorithm | ~100 | Incorrect CSS 2.1 auto layout implementation |
| Position Calculation | ~50 | Relative vs absolute positioning confusion |
| Border Model | ~30 | Incomplete collapse/separate handling |
| Height Algorithm | ~30 | Row height distribution failures |
| Crashes (NaN) | 18 | Unhandled edge cases causing NaN values |

---

## Problem Analysis

### Problem 1: Missing Anonymous Box Generation (Critical - ~200 tests)

**CSS 2.1 Section 17.2.1** requires automatic generation of anonymous table parts. The current implementation does NOT create these.

**Current Behavior**: If HTML has:
```html
<span style="display: table">
  <span style="display: table-cell">Cell 1</span>
  <span style="display: table-cell">Cell 2</span>
</span>
```

Radiant sees: TABLE → CELL, CELL (no row group or row!)

**Required Behavior**: CSS 2.1 mandates wrapping in:
- Anonymous `table-row` around adjacent cells not in a row
- Anonymous `table-row-group` around rows not in a group
- Anonymous `table-cell` around content not in a cell

**Evidence from failed tests**:
- `table-anonymous-objects-*` (200+ tests): All testing anonymous box generation
- `table-anonymous-block-*` (19 tests): Testing run-in and block context interactions

**Current Code Gap** (`layout_table.cpp:250-330` in `mark_table_node()`):
```cpp
// Only marks existing nodes, never creates anonymous wrappers
static void mark_table_node(LayoutContext* lycon, DomNode* node, ViewGroup* parent) {
    // ...
    if (tag == HTM_TAG_TR || display.inner == CSS_VALUE_TABLE_ROW) {
        ViewTableRow* row = (ViewTableRow*)set_view(lycon, RDT_VIEW_TABLE_ROW, node);
        // BUT: What if children are cells NOT wrapped in a row?
        // This case is not handled!
    }
}
```

### Problem 2: CSS 2.1 Width Algorithm Non-Compliance (~100 tests)

**Current Implementation Issues**:

1. **MCW/PCW Calculation Incomplete** (`measure_cell_minimum_width()` and `measure_cell_intrinsic_width()`):
   - Only measures text nodes character-by-character
   - Doesn't handle replaced elements (images) with intrinsic dimensions
   - Ignores `white-space: nowrap` effects
   - Missing proper handling of nested tables

2. **Column Width Distribution** (`table_auto_layout()` lines 1480-1610):
   - Uses `use_equal_distribution` heuristic that doesn't match browser behavior
   - Incorrect handling of percentage widths in auto-layout tables
   - Missing "guess" phase for columns without explicit widths

3. **Fixed Layout Algorithm** (lines 1250-1420):
   - Hardcodes 4px for table border instead of reading actual values
   - Doesn't properly handle `table-layout: fixed` with first-row cells
   - Missing handling for `<col width="...">` specifications

**Evidence**:
```
table-001: Elements 10.0%, Text 100.0%  (width algorithm issue)
table-layout-*: 0.0% (fixed layout broken)
table-width-algorithms: 0.0%
```

### Problem 3: Position Calculation Confusion (~50 tests)

**The Problem**: Table layout uses a hybrid positioning model:
- Table cells are positioned relative to their parent row
- Rows are positioned relative to the table/row-group
- Content within cells is positioned relative to the cell

**Current Code Confusion** (lines 1900-2100):
```cpp
// Row group positioning (separate model)
if (table->tb->border_collapse) {
    child->x = 1.5f; // Hardcoded! Should use actual border
    child->y = 1.5f;
} else {
    child->x = (float)col_x_positions[0];
    child->y = (float)current_y;
}
```

**Issues**:
- Hardcoded border offset values (1.5px) instead of calculated
- Inconsistent use of float vs int coordinates
- Duplicate cell positioning code (lines 1920-2000 and 2180-2280)
- Text `x` and `y` set multiple times with different values

### Problem 4: Border Model Incompleteness (~30 tests)

**border-collapse: collapse** issues:
- Not properly implementing CSS 2.1 border conflict resolution
- Missing border precedence rules (wider > style > color)
- Not handling border width differences between cells

**border-collapse: separate** issues:
- Border spacing applied inconsistently
- Edge spacing (around table) vs inter-cell spacing confused

**Evidence**:
```
table_border_collapse: 0.0%
table-borders-*: mixed results
```

### Problem 5: Height Algorithm Failures (~30 tests)

**Current Issues**:
- `calculate_rowspan_heights()` only handles basic cases
- Fixed layout height distribution ignores row content
- Minimum row height (17px) hardcoded instead of calculated from font metrics

**Evidence**:
```
table-height-algorithm-*: Low pass rates
```

### Problem 6: NaN Crashes (18 tests)

**Symptom**: JSON output contains `nan` values causing test framework parsing failures

**Likely Causes**:
1. Division by zero in width distribution
2. Uninitialized float values in `ViewBlock`
3. Invalid font metrics feeding into layout calculations

**Evidence**:
```
table-anonymous-objects-003.htm: "Shrink": nan
table-anonymous-objects-004.htm: "Shrink": nan
... (18 total)
```

---

## Proposed Enhancement Plan

### Phase 5.1: Anonymous Box Generation (HIGH PRIORITY)

**Goal**: Implement CSS 2.1 Section 17.2.1 anonymous table parts

**Implementation Strategy**:

Create new file `layout_table_fixup.cpp` with:

```cpp
// CSS 2.1 17.2.1: Generate anonymous table components
void table_fixup_missing_parts(LayoutContext* lycon, ViewTable* table) {
    // Step 1: Wrap adjacent cells not in a row
    wrap_orphan_cells_in_row(lycon, table);

    // Step 2: Wrap adjacent rows not in a row-group
    wrap_orphan_rows_in_group(lycon, table);

    // Step 3: Wrap table-cell content not in a cell
    wrap_orphan_content_in_cells(lycon, table);

    // Step 4: Handle inline content before/after table structure
    wrap_inline_content(lycon, table);
}

static void wrap_orphan_cells_in_row(LayoutContext* lycon, ViewTable* table) {
    // Find sequences of adjacent table-cell children
    // Create anonymous table-row to wrap them

    ViewGroup* parent = table;
    View* child = table->first_child;
    View* cell_sequence_start = nullptr;

    while (child) {
        if (child->view_type == RDT_VIEW_TABLE_CELL) {
            if (!cell_sequence_start) {
                cell_sequence_start = child;
            }
        } else {
            if (cell_sequence_start) {
                // Found end of cell sequence - wrap in anonymous row
                create_anonymous_row(lycon, parent, cell_sequence_start, child);
                cell_sequence_start = nullptr;
            }
            // Recurse into row groups
            if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
                wrap_orphan_cells_in_row(lycon, (ViewTableRowGroup*)child);
            }
        }
        child = child->next_sibling;
    }
    // Handle trailing cells
    if (cell_sequence_start) {
        create_anonymous_row(lycon, parent, cell_sequence_start, nullptr);
    }
}
```

**Testing**:
```bash
# After implementation, these should improve dramatically:
make layout suite=table 2>&1 | grep "table-anonymous-objects" | head -20
```

**Expected Impact**: ~200 tests should move from 0-15% to 80-100%

### Phase 5.2: CSS 2.1 Width Algorithm Rewrite (HIGH PRIORITY)

**Goal**: Implement correct MCW/PCW/column distribution per CSS 2.1 Section 17.5.2

**Implementation Changes**:

1. **Replace `measure_cell_minimum_width()`** with proper MCW calculation:

```cpp
// CSS 2.1: Minimum Content Width (MCW)
// The narrowest the content can shrink without overflow
static int compute_cell_mcw(LayoutContext* lycon, ViewTableCell* cell) {
    if (!cell->is_element()) return CELL_MINIMUM_WIDTH;

    // For text: width of longest word (no breaking)
    // For replaced elements: intrinsic width
    // For nested tables: minimum table width
    // For blocks: maximum of children's MCW

    int mcw = 0;
    for (DomNode* child = cell->as_element()->first_child; child; child = child->next_sibling) {
        if (child->is_text()) {
            mcw = max(mcw, measure_longest_word_width(lycon, child));
        } else if (child->is_element()) {
            DisplayValue disp = resolve_display_value(child);
            if (disp.inner == CSS_VALUE_TABLE) {
                // Recursive table MCW
                mcw = max(mcw, compute_nested_table_mcw(lycon, child));
            } else if (is_replaced_element(child)) {
                mcw = max(mcw, get_replaced_element_intrinsic_width(child));
            } else {
                mcw = max(mcw, compute_block_mcw(lycon, child));
            }
        }
    }

    // Add cell padding and border
    return mcw + get_cell_horizontal_padding(cell) + get_cell_horizontal_border(cell);
}
```

2. **Implement proper column width distribution**:

```cpp
// CSS 2.1 Table Width Algorithm
void distribute_column_widths(TableMetadata* meta, int available_width, bool is_fixed_layout) {
    int columns = meta->column_count;

    if (is_fixed_layout) {
        // Fixed layout: use first row widths, then equal distribution
        distribute_fixed_layout(meta, available_width);
    } else {
        // Auto layout: CSS 2.1 Section 17.5.2.2

        // Step 1: All columns get at least MCW
        int total_mcw = 0;
        for (int i = 0; i < columns; i++) {
            meta->col_widths[i] = meta->col_min_widths[i];
            total_mcw += meta->col_min_widths[i];
        }

        if (available_width <= total_mcw) {
            // Table too narrow - use MCW and overflow
            return;
        }

        // Step 2: Distribute extra width proportionally to (PCW - MCW)
        int extra = available_width - total_mcw;
        int total_extra_wanted = 0;
        for (int i = 0; i < columns; i++) {
            total_extra_wanted += (meta->col_max_widths[i] - meta->col_min_widths[i]);
        }

        if (total_extra_wanted > 0) {
            for (int i = 0; i < columns; i++) {
                int wanted = meta->col_max_widths[i] - meta->col_min_widths[i];
                int share = (extra * wanted) / total_extra_wanted;
                meta->col_widths[i] += share;
            }
        }
    }
}
```

**Expected Impact**: ~100 tests should improve significantly

### Phase 5.3: Position Calculation Cleanup (MEDIUM PRIORITY)

**Goal**: Consistent, documented positioning model

**Changes**:

1. **Single positioning function for cells**:

```cpp
// All cell positioning goes through this function
void position_table_cell(ViewTableCell* cell, int col_start_x, int row_y,
                         int cell_width, int cell_height, bool border_collapse) {
    // Position is ALWAYS relative to parent row
    cell->x = col_start_x;  // Column X relative to row start
    cell->y = 0;            // Always 0 (top of row)
    cell->width = cell_width;
    cell->height = cell_height;

    // Border adjustment for collapse mode
    if (border_collapse && cell->td->col_index > 0) {
        // Adjacent cells share border - adjust overlap
        float border_overlap = get_collapsed_border_width(cell) / 2.0f;
        cell->x -= (int)border_overlap;
    }
}
```

2. **Remove duplicate positioning code** - consolidate lines 1920-2000 and 2180-2280

3. **Clear documentation of coordinate systems**:
```cpp
// COORDINATE SYSTEMS:
// - Table: relative to containing block
// - Row Group: relative to table content area
// - Row: relative to row group (or table if no group)
// - Cell: relative to row
// - Cell Content: relative to cell content area (inside padding+border)
```

### Phase 5.4: Border Model Completion (MEDIUM PRIORITY)

**Goal**: Full CSS 2.1 border-collapse and border-spacing support

**Key Changes**:

1. **Implement border conflict resolution for collapse mode**:

```cpp
// CSS 2.1 17.6.2: Border conflict resolution
CssBorder resolve_border_conflict(CssBorder border1, CssBorder border2) {
    // 1. Hidden wins over all
    if (border1.style == CSS_BORDER_HIDDEN || border2.style == CSS_BORDER_HIDDEN)
        return create_hidden_border();

    // 2. Wider border wins
    if (border1.width != border2.width)
        return border1.width > border2.width ? border1 : border2;

    // 3. Border style precedence: double > solid > dashed > dotted > ridge > outset > groove > inset
    if (border1.style != border2.style)
        return border_style_precedence(border1) > border_style_precedence(border2) ? border1 : border2;

    // 4. Origin precedence: cell > row > row-group > column > column-group > table
    return border_origin_precedence(border1) > border_origin_precedence(border2) ? border1 : border2;
}
```

2. **Fix border-spacing application**:

```cpp
void apply_border_spacing(ViewTable* table, int* col_x_positions, int columns) {
    float h_spacing = table->tb->border_spacing_h;
    float v_spacing = table->tb->border_spacing_v;

    // CSS 2.1: spacing around all edges AND between cells
    col_x_positions[0] = h_spacing;  // Left edge
    for (int i = 1; i <= columns; i++) {
        col_x_positions[i] = col_x_positions[i-1] + col_widths[i-1] + h_spacing;
    }
    // Note: Total table width includes 2*h_spacing for left+right edges
}
```

### Phase 5.5: NaN Bug Fixes (QUICK WIN) ✅ COMPLETED

**Goal**: Eliminate all NaN values in output

**Root Cause Identified**:
The NaN values were caused by a **union type confusion** in `dom_element.hpp`:

```cpp
// In DomElement struct:
union {
    FlexItemProp* fi;     // Flex item properties
    GridItemProp* gi;     // Grid item properties
    TableCellProp* td;    // Table cell properties
};
```

When a table cell is created, `td` is set. But the JSON serialization code in `view_pool.cpp` checked `if (block->fi)` which was true (because `fi` and `td` share the same memory), then read from `fi->flex_shrink` - which is garbage data from the `TableCellProp` struct, resulting in NaN values.

**Fix Applied** (in `view_pool.cpp` line ~1086):
```cpp
// Flex item properties (for elements inside flex containers)
// Note: fi, gi, td are in a union, so check view_type to avoid misinterpreting table cell data as flex data
bool is_table_element = (block->view_type == RDT_VIEW_TABLE ||
                         block->view_type == RDT_VIEW_TABLE_ROW_GROUP ||
                         block->view_type == RDT_VIEW_TABLE_ROW ||
                         block->view_type == RDT_VIEW_TABLE_CELL);
if (block->fi && !is_table_element) {
    // ... output flex properties
}
```

**Result**:
- ✅ 18 NaN errors eliminated
- ✅ 123/123 baseline tests still passing
- ✅ 430 table tests now run without parsing errors

**Previous recommendations below preserved for future-proofing**:

1. **Initialize all float fields**:
```cpp
// In set_view() for table types:
if (view_type == RDT_VIEW_TABLE) {
    ViewTable* table = (ViewTable*)view;
    table->tb->border_spacing_h = 0.0f;  // Explicit init
    table->tb->border_spacing_v = 0.0f;
    // ...
}
```

2. **Guard division operations**:
```cpp
// Before any division:
if (total_width > 0) {
    share = (extra * wanted) / total_width;
} else {
    share = 0;
}
```

3. **Validate font metrics**:
```cpp
// In measure_cell_intrinsic_width():
float char_width = 8.0f;  // Safe default
if (lycon->font.ft_face && lycon->font.ft_face->glyph) {
    char_width = lycon->font.ft_face->glyph->advance.x / 64.0f;
    if (isnan(char_width) || char_width <= 0) char_width = 8.0f;
}
```

**Expected Impact**: 18 error tests → 0 errors

---

## Implementation Priority

### ✅ Phase 5.5: NaN Bug Fixes (COMPLETED)
- ✅ Fixed union type confusion in JSON serialization
- ✅ 18 NaN errors eliminated (all 430 tests now produce valid JSON)
- ✅ Baseline regression: 123/123 tests still passing

### 🔴 Phase 5.1: Anonymous Box Generation (Week 1, Day 1-3)
- Critical for ~200 failing tests
- Requires new file and integration into table layout flow

### 🟡 Phase 5.2: Width Algorithm Rewrite (Week 1, Day 4 - Week 2)
- Complex but well-documented in CSS 2.1 spec
- Should fix ~100 tests

### 🟢 Phase 5.3: Position Cleanup (Week 3, Day 1-2)
- Mostly refactoring existing code
- Reduces maintenance burden

### 🟢 Phase 5.4: Border Model (Week 3, Day 3-5)
- Moderate complexity
- Needed for full CSS compliance

---

## Success Metrics

| Phase | Current Pass Rate | Target Pass Rate | Status |
|-------|------------------|------------------|--------|
| 5.5 NaN Fixes | 1.2% | 1.2% (18 fewer errors) | ✅ DONE |
| 5.1 Anonymous | 1.2% | 50% | 🔴 Next |
| 5.2 Width | 50% | 75% | 🟡 Pending |
| 5.3+5.4 | 75% | 85%+ | 🟢 Pending |

**Overall Target**: 85%+ pass rate (365+ tests passing)

---

## Testing Strategy

### Per-Phase Testing
```bash
# After each phase:
make layout suite=table 2>&1 | tail -20

# Focused testing:
make layout suite=table 2>&1 | grep -E "(PASS|FAIL)" | sort | uniq -c | sort -rn

# Specific category:
make layout suite=table 2>&1 | grep "table-anonymous-objects" | grep PASS | wc -l
```

### Regression Prevention
```bash
# CRITICAL: Baseline tests must ALL pass after each change
# Current baseline: 123/123 tests passing (100%)
make layout suite=baseline
# Expected output: "Total Tests: 123, ✅ Successful: 123"

# Run unit tests after each change:
make test-baseline

# Verify no new NaN errors:
make layout suite=table 2>&1 | grep "nan" | wc -l
```

**⚠️ REGRESSION GATE**: Any change that causes baseline tests to fail must be reverted or fixed before merging. The baseline suite includes:
- Core table functionality (border-collapse, border-spacing, fixed layout, rowspan, etc.)
- Typography and text layout
- Box model and positioning
- Width calculations

### Debug Logging
Enable detailed table logging in `log.conf`:
```
table_layout=DEBUG
```

---

## Appendix A: Passing Tests Analysis

The 5 passing tests provide insight into what currently works:

1. **table-anonymous-block-001**: Simple run-in interaction (works because no anonymous generation needed)
2. **tables-002**: Empty table with just display:table (minimal structure)
3. **table_simple_005_colspan**: 76.2% elements, 100% text (basic colspan)
4. **table_simple_009_empty_cells**: 85.7% elements, 100% text (empty cells)
5. (One more passing test)

**Pattern**: Simple tables without complex anonymous box requirements pass at reasonable rates.

---

## Appendix B: CSS 2.1 Table References

- **Section 17.2.1**: Anonymous table objects (CRITICAL)
- **Section 17.5.2**: Table width algorithms
- **Section 17.5.2.2**: Column width calculation
- **Section 17.5.3**: Table height algorithms
- **Section 17.6**: Borders (collapse/separate models)
- **Section 17.6.2**: Border conflict resolution

---

## Appendix C: Code Locations

| Component | File | Lines |
|-----------|------|-------|
| Entry Point | layout_block.cpp | 198-203 |
| Main Table Layout | layout_table.cpp | 2420-2461 |
| Structure Building | layout_table.cpp | 337-357 |
| Width Measurement | layout_table.cpp | 599-726 |
| Auto Layout Algorithm | layout_table.cpp | 950-1700 |
| Position Calculation | layout_table.cpp | 1700-2150 |
| Cell Content Layout | layout_table.cpp | 500-598 |

---

## Conclusion

The table layout system requires significant structural enhancement, primarily:

1. **Anonymous box generation** - The single biggest gap causing ~200 test failures
2. **CSS 2.1 compliant width algorithm** - Current implementation has multiple deviations
3. **Code cleanup** - Duplicate code and inconsistent positioning

With the phased approach outlined above, we expect to achieve 85%+ pass rate from the current 1.2%. The NaN fixes should be prioritized as quick wins, followed by the critical anonymous box generation work.
