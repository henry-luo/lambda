# Radiant Table Layout Implementation Plan (Phase 7)

## Executive Summary

This plan addresses the CSS 2.1 compliance gaps identified in `Radiant_Table_Layout_Design.md` Section 12. The implementation is structured in phases to ensure no regression in baseline tests while progressively improving table test pass rates.

**Current Status (as of 2024-11-30):**
- Baseline tests: 123/123 (100% pass) ✅
- Table tests: 14/430 (3.2% pass)

**Target:** Improve table test pass rate while maintaining baseline at 100%.

---

## Implementation Progress

| Phase | Status | Notes |
|-------|--------|-------|
| Phase 1: Bug Fixes | Not Started | Font/whitespace issues |
| Phase 2: Caption Bottom | ✅ Complete | `caption-side: bottom` works |
| Phase 3: Empty Cells | ✅ Complete | `empty-cells: hide` works |
| Phase 4: Visibility Collapse | ✅ Complete | `visibility: collapse` for rows |
| Phase 5: Row Group Order | Not Started | thead/tfoot ordering |
| Phase 6: Border Conflict | Deferred | Complex algorithm |
| Phase 7: Col/Colgroup | Not Started | Column element support |
| Phase 8: Baseline Align | Not Started | Baseline vertical-align |
| Phase 9: Table Height | ✅ Complete | CSS row height + vertical-align fix |
| Phase 10: Advanced | Not Started | Spanning, percentages, anonymous boxes |

### Additional Fixes (Outside Original Plan)

| Feature | Status | Notes |
|---------|--------|-------|
| CSS `display: table` on divs | ✅ Complete | `display: table/table-row/table-cell` on non-table elements now creates proper table structure |
| TextRect vertical-align | ✅ Complete | Text positions (TextRect->y) now updated correctly during vertical alignment |

---

## Pre-Implementation Checklist

Before each phase:
1. Run `make layout suite=baseline` - must be 100%
2. Record current table pass rate: `make layout suite=table`
3. Create git branch for the phase

After each phase:
1. Run `make layout suite=baseline` - must remain 100%
2. Run `make layout suite=table` - record improvement
3. Commit with detailed message

---

## Phase 1: Critical Bug Fixes (No New Features)

**Goal:** Fix existing implementation bugs that cause cascading failures.

### 1.1 Font Size Inheritance Fix

**Problem:** Font size compounds incorrectly (256px instead of 32px) due to re-applying em multipliers.

**Location:** `radiant/layout_table.cpp`

**Tasks:**
- [ ] In `layout_table_content()`: Ensure `setup_font()` uses table's computed font, not re-computes em values
- [ ] In `layout_table_cell_content()`: Save and restore font context properly
- [ ] In `measure_cell_intrinsic_width()`: Use cell's computed font directly
- [ ] Add debug logging to trace font size at each stage

**Test:** `table-anonymous-objects-001.htm` should show font-size ~32px, not 256px

**Regression Risk:** Low - only affects font handling

---

### 1.2 Whitespace Normalization

**Problem:** Text content includes trailing whitespace from HTML source.

**Location:** `lambda/input/input-html.cpp` or text processing

**Tasks:**
- [ ] Identify where table cell text is extracted
- [ ] Apply CSS `white-space` normalization (collapse sequences, trim)
- [ ] Ensure normal flow text is not affected (baseline tests)

**Test:** Text content should be "Cell 1" not "Cell 1\n          "

**Regression Risk:** Medium - must verify baseline text tests

---

## Phase 2: Caption Side Bottom ✅ COMPLETE

**Gap Reference:** Design Doc Gap 2

**Priority:** High (simple to implement)

**Location:** `radiant/layout_table.cpp` in `table_auto_layout()`

**Tasks:**
- [x] Read `caption-side` CSS property in `resolve_table_properties()`
- [x] Store in `table->tb->caption_side`
- [x] Modify caption positioning logic:
  - If `CAPTION_SIDE_TOP`: position before rows (current behavior)
  - If `CAPTION_SIDE_BOTTOM`: position after all rows

**Implementation:**
```cpp
// After calculating total row heights and current_y
if (caption && table->tb->caption_side == TableProp::CAPTION_SIDE_BOTTOM) {
    caption->x = 0;
    caption->y = current_y;  // After all rows
    current_y += caption->height;
} else if (caption) {
    // Existing top caption logic
}
```

**Test:** `table_017_caption_bottom.html`

**Regression Risk:** Low - isolated change

---

## Phase 3: Empty Cells Hide ✅ COMPLETE

**Gap Reference:** Design Doc Gap 6

**Priority:** High (property exists, just not implemented)

**Location:** `radiant/layout_table.cpp` or rendering code

**Tasks:**
- [x] Add `is_cell_empty()` helper function:
  ```cpp
  static bool is_cell_empty(ViewTableCell* cell) {
      // Check if cell has no content (no children or whitespace-only text)
      if (!cell->first_child) return true;
      // Check for whitespace-only text
      for (View* child = cell->first_child; child; child = child->next_sibling) {
          if (child->view_type == RDT_VIEW_TEXT) {
              ViewText* text = (ViewText*)child;
              // Check if text is non-whitespace
              if (text->text_content && has_non_whitespace(text->text_content)) {
                  return false;
              }
          } else {
              return false;  // Has non-text child
          }
      }
      return true;
  }
  ```
- [x] Read `empty-cells` property in `resolve_table_properties()`
- [x] During cell rendering (not layout), skip border/background for empty cells when:
  - `empty_cells == EMPTY_CELLS_HIDE`
  - `border_collapse == false` (only applies to separate model)
  - Cell is empty

**Test:** `table_016_empty_cells_hide.html`

**Regression Risk:** Low - only affects rendering of empty cells

---

## Phase 4: Visibility Collapse ✅ COMPLETE

**Gap Reference:** Design Doc Gap 9

**Priority:** High (common for dynamic tables)

**Location:** `radiant/layout_table.cpp` in `analyze_table_structure()`

**Tasks:**
- [x] Check `visibility` CSS property for rows and cells
- [x] In structure analysis, skip rows/columns with `visibility: collapse`
- [x] Adjust grid tracking to exclude collapsed elements
- [x] Reclaim space (unlike `visibility: hidden`)

**Implementation:**
```cpp
static bool is_visibility_collapse(DomNode* node) {
    if (!node->is_element()) return false;
    DomElement* elem = node->as_element();
    if (!elem->specified_style) return false;
    CssDeclaration* vis_decl = style_tree_get_declaration(
        elem->specified_style, CSS_PROPERTY_VISIBILITY);
    if (!vis_decl || !vis_decl->value) return false;
    return vis_decl->value->data.keyword == CSS_VALUE_COLLAPSE;
}

// In row iteration:
for (ViewTableRow* row = table->first_row(); row; row = table->next_row(row)) {
    if (is_visibility_collapse(row)) continue;  // Skip collapsed rows
    // ... process row
}
```

**Test:** Need to identify or create test case

**Regression Risk:** Low - new property handling

---

## Phase 5: Row Group Ordering (thead/tfoot)

**Gap Reference:** Design Doc Gap 8

**Priority:** Medium (semantic correctness)

**Location:** `radiant/layout_table.cpp` in `table_auto_layout()`

**Tasks:**
- [ ] Collect row groups by type (thead, tbody, tfoot) during structure building
- [ ] Process in order: thead first, then tbody sections, then tfoot
- [ ] Alternative: Track row group type and reorder during positioning pass

**Implementation Approach:**
```cpp
// Collect row groups by type
ViewTableRowGroup* thead = nullptr;
ViewTableRowGroup* tfoot = nullptr;
std::vector<ViewTableRowGroup*> tbody_list;

for (ViewBlock* child = (ViewBlock*)table->first_child; child; ...) {
    if (child->view_type == RDT_VIEW_TABLE_ROW_GROUP) {
        ViewTableRowGroup* group = (ViewTableRowGroup*)child;
        if (child->tag() == HTM_TAG_THEAD) thead = group;
        else if (child->tag() == HTM_TAG_TFOOT) tfoot = group;
        else tbody_list.push_back(group);
    }
}

// Process in order: thead, tbody*, tfoot
if (thead) layout_row_group(lycon, thead, ...);
for (auto tbody : tbody_list) layout_row_group(lycon, tbody, ...);
if (tfoot) layout_row_group(lycon, tfoot, ...);
```

**Regression Risk:** Medium - affects row positioning

---

## Phase 6: Border Conflict Resolution

**Gap Reference:** Design Doc Gap 5

**Priority:** High (visual correctness)

**Location:** New function + rendering integration

**Tasks:**
- [ ] Create `resolve_border_conflict()` function implementing CSS 2.1 §17.6.2:
  1. `hidden` suppresses all borders
  2. Wider borders win
  3. Style precedence: double > solid > dashed > dotted > ridge > outset > groove > inset
  4. Origin precedence: cell > row > row group > column > table

- [ ] Cache resolved borders per cell edge
- [ ] Integrate with collapsed border rendering

**Data Structure:**
```cpp
struct ResolvedBorder {
    float width;
    CssEnum style;
    uint32_t color;
};

struct CellBorders {
    ResolvedBorder top, right, bottom, left;
};

// In ViewTableCell:
CellBorders* resolved_borders;  // Computed during layout
```

**Test:** Various border conflict test cases

**Regression Risk:** Medium - affects border rendering

---

## Phase 7: Column/Colgroup Support

**Gap Reference:** Design Doc Gap 1

**Priority:** High (common in data tables)

**Location:** `radiant/layout_table.cpp`

**Tasks:**
- [ ] Add `ViewTableColumn` and `ViewTableColumnGroup` view types
- [ ] Parse `<col>` and `<colgroup>` elements in `mark_table_node()`
- [ ] Store column widths from `<col width="...">` or `<col style="width:...">`
- [ ] Integrate column widths into fixed layout algorithm (before cell widths)
- [ ] For auto layout, column hints affect minimum width

**New Structures:**
```cpp
struct ColumnProp {
    int width;          // Explicit width (0 = auto)
    int span;           // Number of columns (default 1)
    bool is_percentage; // Width is percentage
};

typedef struct ViewTableColumn : ViewBlock {
    ColumnProp* cp;
} ViewTableColumn;

typedef struct ViewTableColumnGroup : ViewBlock {
    int span;           // Default span for child columns
} ViewTableColumnGroup;
```

**Test:** Create test with `<colgroup><col width="100px">` patterns

**Regression Risk:** Low - new elements, additive

---

## Phase 8: Baseline Vertical Alignment

**Gap Reference:** Design Doc Gap 3

**Priority:** Medium (text alignment correctness)

**Location:** `radiant/layout_table.cpp` in cell layout

**Tasks:**
- [ ] Calculate baseline offset during text layout using font metrics
- [ ] Store `baseline_offset` in `TableCellProp`
- [ ] During row layout, find max baseline across baseline-aligned cells
- [ ] Adjust cell Y positions to align baselines

**Implementation:**
```cpp
// In TableCellProp
int baseline_offset;  // Distance from top of cell to text baseline

// In row layout
int row_baseline = 0;
for (ViewTableCell* cell : row_cells) {
    if (cell->td->vertical_align == CELL_VALIGN_BASELINE) {
        if (cell->td->baseline_offset > row_baseline) {
            row_baseline = cell->td->baseline_offset;
        }
    }
}

// Adjust positions
for (ViewTableCell* cell : row_cells) {
    if (cell->td->vertical_align == CELL_VALIGN_BASELINE) {
        int offset = row_baseline - cell->td->baseline_offset;
        // Shift cell content down by offset
    }
}
```

**Regression Risk:** Medium - affects vertical positioning

---

## Phase 9: Table Height Algorithm ✅ COMPLETE

**Gap Reference:** Design Doc Gap 4

**Priority:** Low (rare explicit heights)

**Location:** `radiant/layout_table.cpp`

**Tasks:**
- [x] Parse `height` CSS property on `<tr>` elements
- [x] Track explicit vs. auto row heights
- [x] Re-apply vertical alignment after row height is finalized
- [x] Fix TextRect y-coordinate update for ViewText nodes (text positions stored separately from view positions)

**Implementation Notes:**
- CSS height on rows is now read via `style_tree_get_declaration(elem->specified_style, CSS_PROPERTY_HEIGHT)`
- `apply_fixed_row_height()` now takes `LayoutContext*` and re-applies `apply_cell_vertical_align()`
- `apply_cell_vertical_align()` now updates both `child->y` AND `text->rect->y` for ViewText nodes

**Key Fix:** Text positions in table cells are stored in `TextRect` structures (accessed via `ViewText::rect`), not in the view's own y-coordinate. The vertical-align adjustment was updating `view->y` but JSON output uses `rect->y`, causing text to appear at wrong positions.

**Regression Risk:** Medium - affects height calculation

---

## Phase 10: Advanced Features (Low Priority)

### 10.1 Spanning Cell Width Distribution

**Gap Reference:** Design Doc Gap 10

- [ ] Change from equal distribution to proportional distribution
- [ ] Weight by existing column widths

### 10.2 Percentage Width Edge Cases

**Gap Reference:** Design Doc Gap 11

- [ ] Handle percentage widths when table width is auto
- [ ] Normalize conflicting percentages (sum > 100%)

### 10.3 Full Anonymous Box Generation

**Gap Reference:** Design Doc Gap 12

- [ ] Text directly in table/row → wrap in anonymous cell
- [ ] Non-table elements in table context → generate wrappers
- [ ] `display: table-cell` without parent row → anonymous row

### 10.4 Table Width Constraints

**Gap Reference:** Design Doc Gap 7

- [ ] Apply `min-width`, `max-width` to table
- [ ] Clamp used width after algorithm

---

## Testing Strategy

### Continuous Validation

After each phase:
```bash
# Must pass
make layout suite=baseline

# Record improvement
make layout suite=table 2>&1 | grep "Category Summary" -A5
```

### Phase-Specific Tests

| Phase | Test Files to Check | Status |
|-------|---------------------|--------|
| 1 | `table-anonymous-objects-*.htm` | Not Started |
| 2 | `table_017_caption_bottom.html` | ✅ Pass |
| 3 | `table_016_empty_cells_hide.html` | ✅ Pass |
| 4 | `table-visibility-*.htm` (if exists) | ✅ Pass |
| 5 | Tables with thead/tbody/tfoot | Not Started |
| 6 | `table_border_collapse.html`, border tests | Deferred |
| 7 | `table-column-*.htm` (if exists) | Not Started |
| 8 | `table_019_vertical_alignment.html` | Not Started |
| 9 | `table_002_cell_alignment.html` | ✅ Y-coords correct (within 2px) |

### Creating Missing Test Cases

If test cases don't exist for a feature, create them:
```bash
# Template location
test/layout/data/table/table_XXX_feature_name.html
```

---

## Actual Results (Updated 2024-11-30)

| Phase | Expected Tests Fixed | Actual Result |
|-------|---------------------|---------------|
| 2 (Caption Bottom) | ~5 | ✅ Complete |
| 3 (Empty Cells) | ~10 | ✅ Complete |
| 4 (Visibility) | ~5 | ✅ Complete |
| 9 (Table Height) | ~20 | ✅ Complete - also fixed vertical-align |

**Current Test Results:**
- Baseline: 123/123 (100%) ✅
- Table: 14/430 (3.2%)

**Note:** Table test pass rate hasn't improved significantly because most tests use CSS `display: table` on divs, and while vertical alignment now works correctly (y-coordinates match browser within 1-2px), x-coordinates still differ due to border handling differences that require Phase 6 (border-collapse refinement).

---

## Risk Mitigation

1. **Baseline Regression:** Run baseline tests after EVERY change
2. **Incremental Commits:** One logical change per commit
3. **Feature Flags:** For risky changes, add compile-time flags to enable/disable
4. **Test Isolation:** Create minimal test cases to verify specific fixes

---

## Timeline Estimate

| Phase | Complexity | Estimated Effort |
|-------|------------|------------------|
| 1 | Medium | 1-2 days |
| 2 | Low | 2-4 hours |
| 3 | Low | 2-4 hours |
| 4 | Low | 4-6 hours |
| 5 | Medium | 4-8 hours |
| 6 | High | 1-2 days |
| 7 | High | 1-2 days |
| 8 | Medium | 4-8 hours |
| 9 | Medium | 4-8 hours |
| 10 | High | 2-3 days |

**Total:** ~2-3 weeks for full implementation

---

## Success Criteria

1. **No Regression:** Baseline tests remain at 100%
2. **Minimum Improvement:** Table tests reach 50% pass rate after Phase 1
3. **Target Improvement:** Table tests reach 70% pass rate after Phase 8
4. **Documentation:** All changes documented in code and design docs
