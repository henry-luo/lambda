# Radiant Table Layout Enhancement Proposal v5

**Date**: November 29, 2025
**Author**: Generated based on comprehensive code and test analysis
**Context**: Post Phases 1-4 implementation, analyzing 430 table tests with 5 passes / 425 failures
**Status**: Phase 5 implementation in progress

---

## Executive Summary

The current table layout implementation has undergone significant refactoring (Phases 1-4) but still shows a **98.8% failure rate** (425/430 tests failing, 5 passing). Analysis reveals the failures stem from several fundamental architectural issues rather than minor calculation bugs.

### Current Test Results
```
Total Tests: 430
✅ Successful: 5 (1.2%)
❌ Failed: 425 (98.8%)
💥 Errors (crashes/NaN): 18 → 0 (FIXED)
```

### Implementation Status

| Phase | Description | Status |
|-------|-------------|--------|
| 5.5 | NaN Bug Fixes | ✅ COMPLETED |
| 5.6 | Navigation Helpers | ✅ COMPLETED |
| 5.7 | Cell Processing Helpers | ✅ COMPLETED |
| 5.1 | Anonymous Box Generation | 🔄 NEW DESIGN (see below) |
| 5.2 | Width Algorithm Rewrite | 📋 Pending |
| 5.3 | Position Cleanup | 📋 Pending |
| 5.4 | Border Model | 📋 Pending |

### Root Cause Categories

| Category | Test Count | Root Cause |
|----------|-----------|------------|
| Anonymous Box Generation | ~200 | Missing CSS 2.1 anonymous table part creation |
| Width Algorithm | ~100 | Incorrect CSS 2.1 auto layout implementation |
| Position Calculation | ~50 | Relative vs absolute positioning confusion |
| Border Model | ~30 | Incomplete collapse/separate handling |
| Height Algorithm | ~30 | Row height distribution failures |
| ~~Crashes (NaN)~~ | ~~18~~ | ~~Unhandled edge cases causing NaN values~~ ✅ FIXED |

---

## Completed Work

### ✅ Phase 5.5: NaN Bug Fixes (COMPLETED)

**Root Cause**: Union type confusion in `dom_element.hpp` where `fi` (FlexItemProp), `gi` (GridItemProp), and `td` (TableCellProp) share the same memory. JSON serialization was reading flex properties from table cells.

**Fix Applied** (in `view_pool.cpp`):
```cpp
bool is_table_element = (block->view_type == RDT_VIEW_TABLE || ...);
if (block->fi && !is_table_element) {
    // ... output flex properties only for non-table elements
}
```

**Result**: 18 NaN errors eliminated, all 430 tests now produce valid JSON.

### ✅ Phase 5.6: Navigation Helper Functions (COMPLETED)

Created unified navigation methods that respect anonymous box flags (`is_annoy_tbody`, `is_annoy_tr`, etc.) without creating actual anonymous DOM elements.

**New Methods in `layout_table.cpp` (lines 27-165)**:

| Method | Description |
|--------|-------------|
| `ViewTable::first_row()` | Gets first logical row (respects `is_annoy_tbody`) |
| `ViewTable::next_row()` | Iterates all rows across all row groups |
| `ViewTable::first_row_group()` | Gets first row group (may be table itself) |
| `ViewTable::first_direct_cell()` | Gets first cell when table acts as row (`is_annoy_tr`) |
| `ViewTable::next_direct_cell()` | Gets next cell when table acts as row |
| `ViewTable::acts_as_tbody()` | Check if table has `is_annoy_tbody` flag |
| `ViewTable::acts_as_row()` | Check if table has `is_annoy_tr` flag |
| `ViewTableRowGroup::first_row()` | Gets first row in group |
| `ViewTableRowGroup::next_row()` | Gets next row in group |
| `ViewTableRow::first_cell()` | Gets first cell in row |
| `ViewTableRow::next_cell()` | Gets next cell in row |
| `ViewTableRow::parent_row_group()` | Gets parent row group |

**Layout Algorithms Updated**:
- `table_auto_layout()` uses `table->first_row()` and `table->next_row()`
- `calculate_rowspan_heights()` uses navigation helpers
- `table_fixed_layout()` uses `table->first_row()` for first row width distribution
- All cell iteration uses `row->first_cell()` and `row->next_cell()`

### ✅ Phase 5.7: Cell Processing Helpers (COMPLETED)

Extracted common cell processing logic into reusable helper functions, reducing code duplication by ~470 lines.

**New Helper Functions** (`layout_table.cpp` lines 175-395):

| Function | Purpose |
|----------|---------|
| `get_cell_css_width()` | Gets CSS width handling percentages and lengths |
| `get_explicit_css_height()` | Gets explicit CSS height from element |
| `measure_cell_content_height()` | Measures content height from children |
| `calculate_cell_height()` | Calculates final height with padding/border |
| `apply_cell_vertical_align()` | Applies vertical alignment to cell children |
| `position_cell_text_children()` | Positions text children within cell |
| `calculate_cell_width_from_columns()` | Calculates width from column widths (colspan) |
| `process_table_cell()` | Master function orchestrating all cell processing |
| `apply_fixed_row_height()` | Applies fixed row height to row and cells |

**Code Reduction**: 2788 → 2341 lines (~16% reduction)

---

## New Design: Anonymous Box Handling Without Creating Elements

### Problem Statement

CSS 2.1 Section 17.2.1 requires automatic generation of anonymous table parts:
- Anonymous `table-row` around adjacent cells not in a row
- Anonymous `table-row-group` around rows not in a group
- Anonymous `table-cell` around content not in a cell

**Traditional Approach**: Create actual anonymous DOM elements (expensive, complex)

**Radiant Approach**: Use **flag-based virtual anonymous boxes** - mark existing elements with flags indicating they should behave as if wrapped in anonymous boxes, without actually creating those boxes.

### Flag-Based Anonymous Box System

**Data Structure** (in `view.hpp` TableProp):
```cpp
typedef struct TableProp {
    // ... other fields ...

    // Anonymous box flags - element behaves AS IF wrapped in these anonymous boxes
    uint8_t is_annoy_tbody:1;    // table acts as its own tbody (rows are direct children)
    uint8_t is_annoy_tr:1;       // table/tbody acts as its own tr (cells are direct children)
    uint8_t is_annoy_td:1;       // row acts as its own td (content is direct child)
    uint8_t is_annoy_colgroup:1; // table has implied colgroup
} TableProp;
```

### How It Works

#### Scenario 1: Cells Without Row
```html
<table>
  <td>Cell 1</td>
  <td>Cell 2</td>
</table>
```

**Traditional**: Create anonymous `<tr>` element

**Radiant**: Set `table->tb->is_annoy_tbody = true` and `table->tb->is_annoy_tr = true`

**Navigation**:
```cpp
// Table acts as both tbody AND row
if (table->acts_as_tbody() && table->acts_as_row()) {
    // Iterate cells directly from table
    for (auto cell = table->first_direct_cell(); cell; cell = table->next_direct_cell(cell)) {
        // Process cell as if it were in a row
    }
}
```

#### Scenario 2: Rows Without Row Group
```html
<table>
  <tr><td>Row 1</td></tr>
  <tr><td>Row 2</td></tr>
</table>
```

**Traditional**: Create anonymous `<tbody>` element

**Radiant**: Set `table->tb->is_annoy_tbody = true`

**Navigation**:
```cpp
// Table acts as tbody - rows are direct children
if (table->acts_as_tbody()) {
    for (auto row = table->first_row(); row; row = table->next_row(row)) {
        // Row found directly under table, treated as if in tbody
    }
}
```

#### Scenario 3: Mixed Structure
```html
<table>
  <thead><tr><th>Header</th></tr></thead>
  <tr><td>Body Row 1</td></tr>  <!-- orphan row -->
  <tr><td>Body Row 2</td></tr>  <!-- orphan row -->
</table>
```

**Radiant Approach**:
- `thead` is a real row group
- Orphan `<tr>` elements have their parent marked with `is_annoy_tbody`
- Navigation helpers find rows in both real groups and the "virtual" tbody

### Navigation Helper Implementation

The navigation helpers abstract away the anonymous box complexity:

```cpp
ViewTableRow* ViewTable::first_row() {
    // If table acts as its own tbody, rows are direct children
    if (acts_as_tbody()) {
        for (ViewBlock* child = first_child; child; child = child->next_sibling) {
            if (child->view_type == RDT_VIEW_TABLE_ROW) {
                return (ViewTableRow*)child;
            }
            // Check for cells if table also acts as row
            if (acts_as_row() && child->view_type == RDT_VIEW_TABLE_CELL) {
                return nullptr;  // Use first_direct_cell() instead
            }
        }
    }

    // Otherwise, look in row groups
    for (ViewBlock* child = first_child; child; child = child->next_sibling) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
            ViewTableRow* row = ((ViewTableRowGroup*)child)->first_row();
            if (row) return row;
        } else if (child->view_type == RDT_VIEW_TABLE_ROW) {
            return (ViewTableRow*)child;  // Direct row (table as tbody)
        }
    }
    return nullptr;
}
```

### Advantages of Flag-Based Approach

1. **No Memory Overhead**: No anonymous elements created
2. **DOM Integrity**: Original DOM structure preserved for debugging
3. **Simpler Serialization**: JSON output matches original HTML structure
4. **Unified Navigation**: Helper methods handle all cases transparently
5. **Incremental Adoption**: Flags can be set during view tree construction

### Flag Detection and Setting

During view tree construction (`mark_table_node()`):

```cpp
static void mark_table_node(LayoutContext* lycon, DomNode* node, ViewGroup* parent) {
    // After building table structure, detect missing anonymous boxes

    bool has_row_group = false;
    bool has_direct_row = false;
    bool has_direct_cell = false;

    for (View* child = table->first_child; child; child = child->next_sibling) {
        if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) has_row_group = true;
        if (child->view_type == RDT_VIEW_TABLE_ROW) has_direct_row = true;
        if (child->view_type == RDT_VIEW_TABLE_CELL) has_direct_cell = true;
    }

    // Set flags based on structure
    if (has_direct_row && !has_row_group) {
        table->tb->is_annoy_tbody = true;  // Table acts as tbody
    }
    if (has_direct_cell) {
        table->tb->is_annoy_tbody = true;  // Table acts as tbody
        table->tb->is_annoy_tr = true;     // Table also acts as row
    }
}
```

### Usage Pattern for Layout Code

Layout algorithms use navigation helpers, not direct child iteration:

```cpp
// ❌ OLD: Direct child iteration (breaks with anonymous boxes)
for (View* child = table->first_child; child; child = child->next_sibling) {
    if (child->view_type == RDT_VIEW_TABLE_ROW) { ... }
}

// ✅ NEW: Navigation helper (handles all anonymous box scenarios)
for (ViewTableRow* row = table->first_row(); row; row = table->next_row(row)) {
    for (ViewTableCell* cell = row->first_cell(); cell; cell = row->next_cell(cell)) {
        // Process cell
    }
}

// ✅ SPECIAL: When table acts as row (cells are direct children)
if (table->acts_as_row()) {
    for (ViewTableCell* cell = table->first_direct_cell(); cell;
         cell = table->next_direct_cell(cell)) {
        // Process cell directly
    }
}
```

---

## Remaining Work

### 🔴 Phase 5.1: Anonymous Box Flag Detection (HIGH PRIORITY)

The navigation helpers are in place. Now we need to ensure the anonymous box flags are properly detected and set during view tree construction.

**Current Status**: Flag detection exists in `mark_table_structure()` but may need enhancement for edge cases.

**Key Detection Points**:
```cpp
// In mark_table_structure() - detect and set flags:
// 1. Table with direct cells (no rows) → is_annoy_tbody + is_annoy_tr
// 2. Table with direct rows (no row groups) → is_annoy_tbody
// 3. Row with direct content (no cells) → is_annoy_td
```

### 🟡 Phase 5.2: CSS 2.1 Width Algorithm Rewrite (HIGH PRIORITY)

**Goal**: Implement correct MCW/PCW/column distribution per CSS 2.1 Section 17.5.2

**Key Changes Needed**:

1. **Replace `measure_cell_minimum_width()`** with proper MCW calculation
2. **Implement proper column width distribution** following CSS 2.1 spec
3. **Fix percentage width handling** in auto-layout tables

### 🟢 Phase 5.3: Position Calculation Cleanup (MEDIUM PRIORITY)

**Goal**: Consistent, documented positioning model

**Already Done**:
- Cell processing consolidated into `process_table_cell()` helper
- Duplicate code eliminated

**Remaining**:
- Document coordinate systems clearly
- Verify border adjustment calculations

### 🟢 Phase 5.4: Border Model Completion (MEDIUM PRIORITY)

**Goal**: Full CSS 2.1 border-collapse and border-spacing support

**Key Changes**:
1. Implement border conflict resolution for collapse mode
2. Fix border-spacing application consistency

---

## Implementation Priority

| Priority | Phase | Description | Status |
|----------|-------|-------------|--------|
| ✅ | 5.5 | NaN Bug Fixes | COMPLETED |
| ✅ | 5.6 | Navigation Helpers | COMPLETED |
| ✅ | 5.7 | Cell Processing Helpers | COMPLETED |
| 🔴 | 5.1 | Anonymous Box Flag Detection | Next |
| 🟡 | 5.2 | Width Algorithm Rewrite | Pending |
| 🟢 | 5.3 | Position Cleanup | Partially Done |
| 🟢 | 5.4 | Border Model | Pending |

---

## Success Metrics

| Phase | Current Pass Rate | Target Pass Rate | Status |
|-------|------------------|------------------|--------|
| 5.5 NaN Fixes | 1.2% | 1.2% (18 fewer errors) | ✅ DONE |
| 5.6 Navigation | 1.2% | 1.2% (code quality) | ✅ DONE |
| 5.7 Cell Helpers | 1.2% | 1.2% (code quality) | ✅ DONE |
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
| Navigation Helpers | layout_table.cpp | 27-165 |
| Cell Helper Functions | layout_table.cpp | 175-395 |
| Anonymous Box Flag Defs | view.hpp | 620-625 |
| ViewTable Navigation API | view.hpp | 635-660 |
| Entry Point | layout_block.cpp | 198-203 |
| Main Table Layout | layout_table.cpp | 2250-2341 |
| Structure Building | layout_table.cpp | 430-530 |
| Width Measurement | layout_table.cpp | 700-850 |
| Auto Layout Algorithm | layout_table.cpp | 1100-1500 |
| Position Calculation | layout_table.cpp | 1900-2150 |
| Cell Content Layout | layout_table.cpp | 600-700 |

---

## Conclusion

The table layout system requires significant structural enhancement, primarily:

1. **Anonymous box generation** - The single biggest gap causing ~200 test failures
2. **CSS 2.1 compliant width algorithm** - Current implementation has multiple deviations
3. **Code cleanup** - Duplicate code and inconsistent positioning

With the phased approach outlined above, we expect to achieve 85%+ pass rate from the current 1.2%. The NaN fixes should be prioritized as quick wins, followed by the critical anonymous box generation work.
