# Radiant Table Layout Enhancement Proposal

**Date:** January 14, 2026  
**Status:** In Progress  
**Test Suite:** `make layout suite=table`

---

## Executive Summary

The Radiant table layout engine initially failed 100% of the CSS table test suite (334/334 tests). After implementing Phase 1 fixes for font inheritance in anonymous table boxes, we now pass **3 tests** with the core infrastructure for proper inheritance in place.

---

## 0. Implemented Fixes (Phase 1 Complete)

### 0.1 Font Inheritance for Anonymous Table Elements

**Problem:** Anonymous table elements (e.g., `::anon-tbody`, `::anon-tr`) were created with `font` properties but no `specified_style`. When child elements (actual table cells) tried to inherit font properties, the inheritance code checked `parent->specified_style` and found NULL, skipping inheritance entirely.

**Root Cause:** In `resolve_css_styles()`, the inheritance loop condition was:
```cpp
if (parent_tree) {  // Only runs if parent has specified_style!
    // ... inheritance code ...
}
```

**Fix Applied (2 changes):**

1. **resolve_css_style.cpp** - Changed inheritance condition to also check for computed font:
```cpp
// Run inheritance check if parent has either specified_style or computed font
// This handles anonymous table elements that have font but no specified_style
if (parent_tree || (parent && parent->font)) {
```

2. **resolve_css_style.cpp** - Added direct font-size inheritance from computed parent font:
```cpp
// Special handling for font-size: also check ancestor's computed font->font_size
// This handles cases where ancestor is an anonymous box with no specified_style
if (prop_id == CSS_PROPERTY_FONT_SIZE && ancestor && ancestor->font && ancestor->font->font_size > 0) {
    ViewSpan* span = (ViewSpan*)lycon->view;
    if (!span->font) { span->font = alloc_font_prop(lycon); }
    span->font->font_size = ancestor->font->font_size;
    continue;
}
```

3. **layout_table.cpp** - Added `setup_font()` calls in `mark_table_node()` after style resolution for row groups, rows, and cells to propagate font context.

4. **layout_table.cpp** - Added `setup_font()` in `build_table_tree()` after table style resolution.

**Result:** Font-family and font-size now correctly inherit through anonymous table box wrappers.

---

## 1. Test Analysis

### 1.1 Test Results Overview

| Metric | Before Fix | After Phase 1 |
|--------|------------|---------------|
| Total Tests | 334 | 334 |
| Passed | 0 (0%) | 3 (0.9%) |
| Failed | 334 (100%) | 331 (99.1%) |

### 1.2 Failure Categories

| Category | Count | Match Rate | Root Cause |
|----------|-------|------------|------------|
| Anonymous Objects | 206 | 0-10% | CSS display table structure mishandling |
| Anonymous Blocks | 7 | 10-70% | Partial structure recognition |
| Borders | 4 | 36-50% | Border width/collapse calculation |
| Captions | 5 | 11-36% | Caption positioning/width |
| Columns | 10 | 15-47% | Column width distribution |
| Height Algorithm | 25 | 10-77% | Row height distribution |
| Vertical Align | 8 | 12-90% | Baseline/middle/bottom alignment |
| Fixed/Auto Layout | 20 | 9-90% | Column width algorithms |
| Margins | 3 | 0% | Table margin handling |
| Visual Layout | 15 | 11-94% | General positioning |

### 1.3 Key Observations

1. **Anonymous box tests dominate** - 62% of tests (206/334) involve CSS `display: table-*` properties
2. **Width calculation issues are pervasive** - Most failures show 6-150+ pixel width differences
3. **Border-collapse mode** - Consistent 6-9px errors suggest half-border calculation issues
4. **Caption handling** - 340px width differences indicate captions not constraining table width

---

## 2. Root Cause Analysis

### 2.1 Anonymous Box Generation (P0 - Critical)

**Current Implementation:** `generate_anonymous_table_boxes()` in `layout_table.cpp:1700-2200`

**Problems:**
1. CSS-styled elements (`display: table-cell`) don't receive proper anonymous wrappers
2. Mixed content (text nodes + table-internal elements) not handled correctly
3. Runs of consecutive cells don't share anonymous rows as required by CSS 2.1

**CSS 2.1 Section 17.2.1 Requirements:**
```
If a child of a 'table' element is not a table-row-group, table-row, 
table-column-group, table-column, or table-caption, user agents must 
generate an anonymous table-row-group box around it and any consecutive 
siblings that are also not one of those.
```

**Test Evidence:**
- `table-anonymous-objects-059.htm`: 152.5px width difference (entire structure wrong)
- `table-anonymous-objects-003.htm`: 85.7% elements match but spans fail (fontSize "16" vs "32")

### 2.2 Column Width Algorithm (P1)

**Current Implementation:** `layout_table.cpp:3400-3700`

**Problems:**
1. MCW (Minimum Content Width) not calculated correctly for wrapped text
2. PCW (Preferred Content Width) doesn't account for nowrap content
3. Colspan distribution uses simple proportional split instead of CSS 2.1 algorithm

**CSS 2.1 Section 17.5.2.2 Requirements:**
```
In the automatic table layout algorithm:
1. Calculate minimum content width (MCW) and preferred content width (PCW) for each cell
2. For cells spanning multiple columns, increase column widths to accommodate
3. Distribute available width minus column minimums proportionally
```

**Test Evidence:**
- `table_colspan_test.html`: 20.4px width difference on colspan cells
- `table_003_fixed_layout.html`: 10px width difference on table container

### 2.3 Border Model (P2)

**Current Implementation:** `calculate_cell_height()` in `layout_table.cpp:400-435`

**Problems:**
1. Border-collapse mode doesn't add half-border to table outer dimensions
2. Cell positioning doesn't account for collapsed border offsets
3. Row/column border contributions not accumulated correctly

**CSS 2.1 Section 17.6.2 Requirements:**
```
In the collapsing border model:
- Borders are centered on the grid lines between cells
- The table's outer edges include half of the outermost border width
- Adjacent cells share borders (winner determined by conflict resolution)
```

**Test Evidence:**
- `table-borders-001.htm`: Table 9px narrower than browser (133 vs 142)
- Row widths consistently 6px narrower

### 2.4 Caption Integration (P3)

**Current Implementation:** Caption laid out but not integrated with table width

**Problems:**
1. Caption width doesn't constrain minimum table width
2. Multiple captions not ordered correctly
3. `caption-side: bottom` not positioning caption after table

**Test Evidence:**
- `table-caption-002.htm`: 340px width difference, incorrect y-positions

### 2.5 Row Height Distribution (P4)

**Current Implementation:** `distribute_rowspan_heights()` in `layout_table.cpp:1050-1160`

**Problems:**
1. Simple proportional distribution doesn't match browser behavior
2. CSS height on rows not honored as minimum
3. Rowspan cells processed in arbitrary order (should be by increasing span)

---

## 3. Implementation Plan

### Phase 1: Anonymous Box Generation Fix (P0)

**Goal:** Fix 206 tests (62% of suite)

**Files to Modify:**
- `radiant/layout_table.cpp`
- `radiant/layout_table.hpp`

**Changes:**

#### 1.1 Refactor `generate_anonymous_table_boxes()`

```cpp
// NEW: Two-pass anonymous box generation
//
// Pass 1: Classify children into categories
//   - CAPTION: display:table-caption or <caption>
//   - ROW_GROUP: display:table-row-group/header-group/footer-group or thead/tbody/tfoot
//   - ROW: display:table-row or <tr>
//   - CELL: display:table-cell or <td>/<th>
//   - COLUMN: display:table-column(-group) or <col>/<colgroup>
//   - OTHER: text nodes, inline elements, blocks (need wrapping)
//
// Pass 2: Generate wrappers per CSS 2.1 Section 17.2.1
//   - Consecutive CELLs -> wrap in anonymous ROW -> wrap in anonymous ROW_GROUP
//   - Consecutive ROWs -> wrap in anonymous ROW_GROUP
//   - OTHER content in ROW -> wrap in anonymous CELL
//   - OTHER content in ROW_GROUP -> wrap in anonymous CELL -> anonymous ROW
```

#### 1.2 Add Helper Functions

```cpp
// Classify a child node's table role
enum TableChildType {
    TABLE_CHILD_CAPTION,
    TABLE_CHILD_COLUMN,
    TABLE_CHILD_COLUMN_GROUP,
    TABLE_CHILD_ROW_GROUP,
    TABLE_CHILD_ROW,
    TABLE_CHILD_CELL,
    TABLE_CHILD_OTHER  // Needs wrapping
};

TableChildType classify_table_child(DomNode* child);

// Collect runs of same-type children for batch wrapping
struct ChildRun {
    TableChildType type;
    ArrayList* nodes;  // DomNode*
};

ArrayList* collect_child_runs(DomElement* parent);
```

#### 1.3 Fix CSS Display Detection

```cpp
// Current: Only checks HTML tags
// Fixed: Also check resolved display value for CSS-styled elements

bool is_table_row(DomNode* node) {
    uintptr_t tag = node->tag();
    if (tag == HTM_TAG_TR) return true;
    
    DisplayValue display = resolve_display_value(node);
    return display.inner == CSS_VALUE_TABLE_ROW;
}
```

### Phase 2: Column Width Algorithm (P1)

**Goal:** Fix colspan distribution and MCW/PCW calculation

**Changes:**

#### 2.1 Separate MCW and PCW Calculation

```cpp
struct ColumnConstraints {
    float min_width;   // MCW - minimum content width (longest unbreakable word)
    float pref_width;  // PCW - preferred content width (nowrap width)
    float css_width;   // Explicit CSS width (or -1 if auto)
    bool has_percent;  // True if CSS width is percentage
    float percent;     // Percentage value if has_percent
};

void calculate_column_constraints(LayoutContext* lycon, ViewTable* table,
                                  ColumnConstraints* constraints, int columns);
```

#### 2.2 CSS 2.1 Colspan Distribution

```cpp
// Process colspan cells in order of increasing span
// For each colspan cell:
//   1. Sum current widths of spanned columns
//   2. If cell needs more, distribute excess proportionally
//   3. Respect column minimum widths

void distribute_colspan_widths(ColumnConstraints* constraints, int columns,
                               ViewTable* table);
```

#### 2.3 Final Width Distribution

```cpp
// CSS 2.1 Section 17.5.2.2 Step 3:
// Distribute remaining space to columns with PCW > MCW
// If no such columns, distribute equally

void distribute_remaining_width(ColumnConstraints* constraints, int columns,
                                float available_width);
```

### Phase 3: Border Model Corrections (P2)

**Goal:** Fix 6-9px width/height discrepancies

**Changes:**

#### 3.1 Table Outer Dimensions

```cpp
// In border-collapse mode, table includes half of outer borders
float calculate_table_border_width(ViewTable* table) {
    if (!table->tb->border_collapse) {
        return table->bound->border->width.left + table->bound->border->width.right;
    }
    
    // Collapsed: Include half of first/last column borders
    float left_border = max(table_left_border, first_cell_left_border) / 2.0f;
    float right_border = max(table_right_border, last_cell_right_border) / 2.0f;
    return left_border + right_border;
}
```

#### 3.2 Cell Positioning with Collapsed Borders

```cpp
// Cells overlap by half border width in collapsed mode
float cell_x = col_x_positions[col];
if (table->tb->border_collapse) {
    cell_x -= resolved_left_border / 2.0f;
}
```

### Phase 4: Caption Integration (P3)

**Goal:** Proper caption width and positioning

**Changes:**

```cpp
// Before column width calculation:
float caption_preferred_width = measure_caption_preferred_width(lycon, caption);

// Table width must be at least caption width
float min_table_width = max(content_min_width, caption_preferred_width);

// After table content layout:
if (table->tb->caption_side == CAPTION_SIDE_TOP) {
    caption->y = table->y - caption->height;
    table->y += caption->height;
} else {
    caption->y = table->y + table->height;
}
```

### Phase 5: Row Height Distribution (P4)

**Goal:** Correct rowspan height distribution

**Changes:**

```cpp
// Process rowspan cells in order of increasing span (2, 3, 4, ...)
// This ensures smaller spans are resolved before larger ones

void distribute_rowspan_heights(ViewTable* table, TableMetadata* meta) {
    int max_span = calculate_max_rowspan(table);
    
    for (int span = 2; span <= max_span; span++) {
        for (each cell with row_span == span) {
            float current_height = sum(row_heights[start..end]);
            float needed = cell->height;
            
            if (needed > current_height) {
                float extra = needed - current_height;
                // Distribute proportionally to row content heights
                distribute_extra_to_rows(extra, start, end, meta);
            }
        }
    }
}
```

---

## 4. Testing Strategy

### 4.1 Incremental Validation

After each phase:
```bash
make build && make layout suite=table 2>&1 | grep "Total Tests\|Successful\|Failed"
```

### 4.2 Category-Specific Tests

```bash
# Test anonymous object handling
make layout test=table-anonymous-objects-003.htm

# Test border calculations  
make layout test=table-borders-001.htm

# Test colspan distribution
make layout test=table_colspan_test.html
```

### 4.3 Expected Progress

| Phase | Expected Pass Rate | Tests Fixed |
|-------|-------------------|-------------|
| Phase 1 | ~60% | +200 |
| Phase 2 | ~75% | +50 |
| Phase 3 | ~82% | +25 |
| Phase 4 | ~87% | +15 |
| Phase 5 | ~92% | +15 |

---

## 5. Implementation Timeline

| Phase | Description | Est. Effort |
|-------|-------------|-------------|
| P0 | Anonymous Box Generation | 4-6 hours |
| P1 | Column Width Algorithm | 3-4 hours |
| P2 | Border Model | 2-3 hours |
| P3 | Caption Integration | 1-2 hours |
| P4 | Row Height Distribution | 2-3 hours |

**Total Estimated Effort:** 12-18 hours

---

## 6. References

- [CSS 2.1 Section 17 - Tables](https://www.w3.org/TR/CSS21/tables.html)
- [CSS 2.1 Section 17.2.1 - Anonymous Table Objects](https://www.w3.org/TR/CSS21/tables.html#anonymous-boxes)
- [CSS 2.1 Section 17.5 - Table Layout Algorithms](https://www.w3.org/TR/CSS21/tables.html#width-layout)
- [CSS 2.1 Section 17.6 - Borders](https://www.w3.org/TR/CSS21/tables.html#borders)

---

## 7. Appendix: Sample Failing Tests

### A.1 Anonymous Objects (0% match)
```html
<!-- table-anonymous-objects-059.htm -->
<span style="display:table">
  <span style="display: table-cell">Row 1, Col 1</span>
  <span style="display: table-row-group">
    <span style="display: table-row">
      <span style="display: table-cell">Row 2, Col 1</span>
    </span>
  </span>
</span>
```
**Issue:** First cell should get anonymous row+tbody wrapper. Currently gets wrong structure.

### A.2 Border Collapse (50% match)
```html
<!-- table-borders-001.htm -->
<table style="border: 3px solid">
  <tr><td style="border: 1px solid">...</td></tr>
</table>
```
**Issue:** Table width 133px vs browser 142px (missing 9px = 2×1.5px cell borders + 2×3px table borders / 2)

### A.3 Colspan (9% match)
```html
<!-- table_colspan_test.html -->
<table>
  <tr><td colspan="2">Spans 2</td></tr>
  <tr><td>A</td><td>B</td></tr>
</table>
```
**Issue:** Colspan cell width not distributed correctly to underlying columns.
