# Radiant Table Layout Enhancement Proposal

## Executive Summary

The current Radiant table layout implementation has **341 failing tests** (0% pass rate) in the `table` test suite. The failures span multiple categories:
- **Anonymous box/object tests** (majority): ~200+ tests with 0-85% element match rates
- **Height algorithm tests**: ~30 tests with 10-100% match rates
- **Border/spacing tests**: ~15 tests
- **Caption/column tests**: ~20 tests
- **Visual layout tests**: ~25 tests

This proposal outlines structural enhancements to better match browser behavior.

---

## 1. Anonymous Box Handling Strategy

### Current Design (Retained)

The current `generate_anonymous_table_boxes()` function (layout_table.cpp lines 1400-1740) modifies the DOM tree by creating and inserting anonymous elements. **This design will be retained** as it correctly implements CSS 2.1 Section 17.2.1 anonymous box generation semantics.

### Test Comparison Enhancement

The test failures for anonymous box tests occur because browsers don't expose anonymous boxes in their DOM APIs (like `getBoundingClientRect()`), while Radiant creates actual DOM elements. The solution is to **enhance the test comparison script** rather than change the layout architecture.

**Changes to `test/layout/compare-layout.js`:**

```javascript
// Add anonymous box filtering for element comparison
function isAnonymousTableElement(element) {
    // Check for Radiant's anonymous element markers
    const tagName = element.tagName || element.tag || '';
    return tagName.startsWith('::anon-') ||
           tagName === '::anon-tbody' ||
           tagName === '::anon-tr' ||
           tagName === '::anon-td';
}

function filterAnonymousBoxes(layoutTree) {
    // Recursively filter out anonymous boxes from comparison
    // But preserve their children (which are real DOM elements)
    if (!layoutTree) return layoutTree;

    if (layoutTree.children) {
        layoutTree.children = layoutTree.children
            .flatMap(child => {
                if (isAnonymousTableElement(child)) {
                    // Replace anonymous box with its children
                    return child.children || [];
                }
                return [filterAnonymousBoxes(child)];
            });
    }
    return layoutTree;
}

// In comparison function:
function compareLayoutTrees(radiantTree, browserTree, options = {}) {
    // Filter anonymous boxes before comparison
    const filteredRadiant = options.filterAnonymous
        ? filterAnonymousBoxes(radiantTree)
        : radiantTree;

    // Continue with existing comparison logic...
}
```

**Test Suite Configuration:**

```javascript
// For table tests, enable anonymous box filtering
const tableTestConfig = {
    filterAnonymous: true,
    tolerancePixels: 1,
    compareTextContent: true
};
```

This approach:
- Preserves correct CSS 2.1 layout semantics in Radiant
- Ensures test comparisons match what browsers expose via DOM APIs
- Maintains accurate position/size comparisons for actual DOM elements

---

## 2. Two-Phase Table Layout Architecture

### Current Issue

The table layout uses a mostly single-pass approach with retrofitting:
- Cell widths measured → columns sized → cells positioned → rowspan adjustments
- This causes issues where rowspan/colspan cells don't properly influence track sizing

### CSS 2.1 Section 17.5 Requirements

1. **Track Sizing Phase**: Calculate column widths considering ALL cells (including spanning)
2. **Cell Placement Phase**: Position cells within calculated tracks
3. **Content Layout Phase**: Layout cell content with final dimensions

### Proposed Enhancement

**New data structures (add to `layout_table.hpp`):**

```cpp
// Track sizing result for CSS 2.1 compliance
struct TrackSizingResult {
    float min_content;      // Minimum content width (MCW)
    float max_content;      // Maximum content width (PCW)
    float used_width;       // Final computed width
    float percent_basis;    // For percentage-based tracks
    bool has_explicit;      // Has explicit CSS width
    float explicit_width;   // The explicit width value
};

// Enhanced table layout state
struct TableLayoutState {
    // Phase 1: Structure analysis (existing)
    TableMetadata* meta;

    // Phase 2: Track sizing (new)
    TrackSizingResult* column_tracks;
    TrackSizingResult* row_tracks;

    // Phase 3: Cell allocation
    bool* cell_laid_out;    // Track which cells have been processed
};
```

**New multi-pass algorithm in `table_auto_layout()`:**

```cpp
void table_auto_layout(LayoutContext* lycon, ViewTable* table) {
    // PHASE 1: Structure Analysis (existing - analyze_table_structure)
    TableMetadata* meta = analyze_table_structure(lycon, table);

    // PHASE 2: Collect Intrinsic Sizes (enhanced)
    // 2a. Measure all single-column cells first
    collect_single_column_contributions(lycon, table, meta);

    // 2b. Process spanning cells and distribute contributions
    distribute_colspan_contributions(lycon, table, meta);

    // PHASE 3: Resolve Column Widths (CSS 2.1 algorithm)
    resolve_column_widths_css21(lycon, table, meta);

    // PHASE 4: Row Height Calculation (enhanced)
    calculate_row_heights_multipass(lycon, table, meta);

    // PHASE 5: Final Cell Layout
    layout_all_cells_final(lycon, table, meta);

    // PHASE 6: Positioning
    position_table_elements(lycon, table, meta);
}
```

---

## 3. Intrinsic Size Calculation Enhancement

### Current Issue

`measure_cell_widths()` (lines 2830-3150) measures both min/max content widths but:
- Doesn't properly handle `table-layout: auto` for nested tables
- Missing proper contribution from percentage-based widths
- Colspan distribution doesn't follow CSS 2.1 algorithm precisely

### Proposed Enhancement

**New cell contribution structure:**

```cpp
// CSS 2.1 Section 17.5.2.2 precise implementation
struct CellIntrinsicContribution {
    float min_content;          // MCW - hard minimum
    float max_content;          // PCW - preferred
    float percentage_width;     // For percentage cells (0 if none)
    bool has_percentage;        // True if width is percentage-based
    bool has_explicit_width;    // True if explicit CSS width
    float explicit_width;       // The explicit width value
};

// Replace measure_cell_widths() with more precise version
static CellIntrinsicContribution compute_cell_contribution(
    LayoutContext* lycon,
    ViewTableCell* cell,
    float table_content_width)
{
    CellIntrinsicContribution result = {};

    // 1. Check for explicit CSS width
    float css_width = get_cell_css_width(lycon, cell, table_content_width);
    if (css_width > 0) {
        result.has_explicit_width = true;
        result.explicit_width = css_width;
    }

    // 2. Check for percentage width
    DomElement* elem = cell->as_element();
    if (elem && elem->specified_style) {
        CssDeclaration* width_decl = style_tree_get_declaration(
            elem->specified_style, CSS_PROPERTY_WIDTH);
        if (width_decl && width_decl->value &&
            width_decl->value->type == CSS_VALUE_TYPE_PERCENTAGE) {
            result.has_percentage = true;
            result.percentage_width = width_decl->value->data.percentage.value;
        }
    }

    // 3. Measure intrinsic content sizes
    CellWidths widths = measure_cell_widths(lycon, cell);
    result.min_content = widths.min_width;
    result.max_content = widths.max_width;

    return result;
}
```

**CSS 2.1 Colspan Distribution Algorithm:**

```cpp
// Distribute colspan cell's width contribution to columns
// CSS 2.1 Section 17.5.2.2
static void distribute_colspan_contribution(
    CellIntrinsicContribution* contrib,
    int start_col, int col_span,
    float* col_min, float* col_max, int columns)
{
    // Calculate current totals for spanned columns
    float current_min_total = 0, current_max_total = 0;
    for (int c = start_col; c < start_col + col_span && c < columns; c++) {
        current_min_total += col_min[c];
        current_max_total += col_max[c];
    }

    // Only distribute if cell needs more space
    if (contrib->min_content > current_min_total) {
        float extra = contrib->min_content - current_min_total;
        // CSS 2.1: Distribute proportionally to existing column sizes
        // If all zero, distribute equally
        if (current_min_total > 0) {
            for (int c = start_col; c < start_col + col_span && c < columns; c++) {
                col_min[c] += extra * (col_min[c] / current_min_total);
            }
        } else {
            float per_col = extra / col_span;
            for (int c = start_col; c < start_col + col_span && c < columns; c++) {
                col_min[c] += per_col;
            }
        }
    }

    // Same for max_content
    if (contrib->max_content > current_max_total) {
        float extra = contrib->max_content - current_max_total;
        if (current_max_total > 0) {
            for (int c = start_col; c < start_col + col_span && c < columns; c++) {
                col_max[c] += extra * (col_max[c] / current_max_total);
            }
        } else {
            float per_col = extra / col_span;
            for (int c = start_col; c < start_col + col_span && c < columns; c++) {
                col_max[c] += per_col;
            }
        }
    }
}
```

---

## 4. Row Height Distribution Enhancement

### Current Issue

`distribute_rowspan_heights()` (lines 950-1020) uses proportional distribution, but:
- CSS 2.1 §17.5.3 specifies different behavior for constrained tables
- Explicit `height` on rows/cells isn't properly honored when conflicting with rowspan
- The algorithm runs AFTER initial layout, causing duplicate work

### Proposed Enhancement

**New row height algorithm phases:**

```cpp
enum RowHeightPhase {
    RH_PHASE_MIN_CONTENT,    // Calculate minimum content heights
    RH_PHASE_ROWSPAN_DIST,   // Distribute rowspan across rows
    RH_PHASE_EXPLICIT_MIN,   // Apply explicit CSS heights as minimums
    RH_PHASE_TABLE_HEIGHT,   // Distribute extra table height
};

// Multi-phase row height calculation
static void calculate_row_heights_multipass(
    LayoutContext* lycon,
    ViewTable* table,
    TableMetadata* meta)
{
    int rows = meta->row_count;

    // Phase 1: Calculate minimum content heights from single-row cells
    for (int r = 0; r < rows; r++) {
        meta->row_heights[r] = 0;
    }

    for (ViewTableRow* row = table->first_row(); row; row = table->next_row(row)) {
        for (ViewTableCell* cell = row->first_cell(); cell; cell = row->next_cell(cell)) {
            if (cell->td->row_span == 1) {
                float content_h = measure_cell_content_height(lycon, cell);
                float cell_h = calculate_cell_height(lycon, cell, table, content_h, 0);
                int r = cell->td->row_index;
                if (cell_h > meta->row_heights[r]) {
                    meta->row_heights[r] = cell_h;
                }
            }
        }
    }

    // Phase 2: Distribute rowspan cell heights
    distribute_rowspan_heights_enhanced(table, meta);

    // Phase 3: Apply explicit CSS row heights as minimums
    apply_explicit_row_heights(lycon, table, meta);

    // Phase 4: Distribute extra table height (if explicit table height)
    distribute_table_height_to_rows(lycon, table, meta);
}

// Enhanced rowspan distribution
static void distribute_rowspan_heights_enhanced(
    ViewTable* table,
    TableMetadata* meta)
{
    // Process rowspan cells in order of span size (smaller spans first)
    // This ensures proper cascading of height requirements
    for (int span = 2; span <= meta->row_count; span++) {
        for (ViewTableRow* row = table->first_row(); row; row = table->next_row(row)) {
            for (ViewTableCell* cell = row->first_cell(); cell; cell = row->next_cell(cell)) {
                if (cell->td->row_span == span) {
                    int start = cell->td->row_index;
                    int end = start + span;
                    if (end > meta->row_count) end = meta->row_count;

                    // Calculate current total height of spanned rows
                    float current_total = 0;
                    for (int r = start; r < end; r++) {
                        current_total += meta->row_heights[r];
                    }

                    // Calculate required height for this cell
                    float required = cell->height;

                    if (required > current_total) {
                        // Distribute extra proportionally
                        float extra = required - current_total;
                        if (current_total > 0) {
                            for (int r = start; r < end; r++) {
                                float prop = meta->row_heights[r] / current_total;
                                meta->row_heights[r] += extra * prop;
                            }
                        } else {
                            // Equal distribution if all rows are zero
                            float per_row = extra / span;
                            for (int r = start; r < end; r++) {
                                meta->row_heights[r] += per_row;
                            }
                        }
                    }
                }
            }
        }
    }
}
```

---

## 5. Border Model Enhancement

### Current Issue

Border-collapse handling in `resolve_collapsed_borders()` (lines 800-950) is comprehensive but:
- Column/column-group borders aren't considered
- Priority calculation doesn't fully implement CSS 2.1 §17.6.2.1

### Proposed Enhancement

**Complete border source hierarchy:**

```cpp
// CSS 2.1 Section 17.6.2.1 border sources (in precedence order)
enum BorderSource {
    BORDER_SOURCE_CELL = 1,         // Highest specificity
    BORDER_SOURCE_ROW = 2,
    BORDER_SOURCE_ROW_GROUP = 3,    // thead/tbody/tfoot
    BORDER_SOURCE_COLUMN = 4,       // col element
    BORDER_SOURCE_COLUMN_GROUP = 5, // colgroup element
    BORDER_SOURCE_TABLE = 6,        // Lowest specificity
};

// Enhanced border collection including columns
static void collect_border_candidates_for_edge(
    LayoutContext* lycon,
    ViewTable* table,
    TableMetadata* meta,
    int row, int col,
    int edge,  // 0=top, 1=right, 2=bottom, 3=left
    ArrayList* candidates)
{
    // 1. Cell border (highest priority)
    ViewTableCell* cell = find_cell_at(table, row, col);
    if (cell) {
        CollapsedBorder* border = (CollapsedBorder*)malloc(sizeof(CollapsedBorder));
        *border = get_cell_border(cell, edge);
        border->source = BORDER_SOURCE_CELL;
        arraylist_append(candidates, border);
    }

    // 2. Row border
    // ... existing row border collection

    // 3. Row group border
    // ... existing row group border collection

    // 4. Column border (NEW)
    ViewTableColumn* column = find_column_at(table, col);
    if (column && column->bound && column->bound->border) {
        CollapsedBorder* border = (CollapsedBorder*)malloc(sizeof(CollapsedBorder));
        *border = get_column_border(column, edge);
        border->source = BORDER_SOURCE_COLUMN;
        arraylist_append(candidates, border);
    }

    // 5. Column group border (NEW)
    ViewTableColumnGroup* colgroup = find_column_group_at(table, col);
    if (colgroup && colgroup->bound && colgroup->bound->border) {
        CollapsedBorder* border = (CollapsedBorder*)malloc(sizeof(CollapsedBorder));
        *border = get_column_group_border(colgroup, edge);
        border->source = BORDER_SOURCE_COLUMN_GROUP;
        arraylist_append(candidates, border);
    }

    // 6. Table border (lowest priority)
    CollapsedBorder* table_border = (CollapsedBorder*)malloc(sizeof(CollapsedBorder));
    *table_border = get_table_border(table, edge);
    table_border->source = BORDER_SOURCE_TABLE;
    arraylist_append(candidates, table_border);
}

// Enhanced winner selection with source priority
static CollapsedBorder select_winning_border_enhanced(
    const CollapsedBorder& a,
    const CollapsedBorder& b)
{
    // Rule 1: hidden wins
    if (a.style == CSS_VALUE_HIDDEN) return a;
    if (b.style == CSS_VALUE_HIDDEN) return b;

    // Rule 2: none loses
    if (a.style == CSS_VALUE_NONE && b.style == CSS_VALUE_NONE) return a;
    if (a.style == CSS_VALUE_NONE) return b;
    if (b.style == CSS_VALUE_NONE) return a;

    // Rule 3: wider wins
    if (a.width > b.width + 0.01f) return a;
    if (b.width > a.width + 0.01f) return b;

    // Rule 4: style priority
    uint8_t a_pri = get_border_style_priority(a.style);
    uint8_t b_pri = get_border_style_priority(b.style);
    if (a_pri > b_pri) return a;
    if (b_pri > a_pri) return b;

    // Rule 5: Source priority (cell > row > row-group > col > col-group > table)
    if (a.source < b.source) return a;
    if (b.source < a.source) return b;

    // Rule 6: top/left wins on final tie
    return a;
}
```

---

## 6. Caption Layout Enhancement

### Current Issue

Caption is laid out twice (initial + re-layout when table width changes). The current approach:
- Re-layouts caption after table width is known
- Doesn't properly handle `caption-side: bottom` margin calculations

### Proposed Enhancement

**Single-pass caption layout:**

```cpp
// Defer caption layout until table width is determined
static void layout_table_caption(
    LayoutContext* lycon,
    ViewTable* table,
    ViewBlock* caption,
    float table_width)
{
    if (!caption) return;

    // Position based on caption-side
    caption->x = 0;
    caption->width = table_width;

    // Calculate content width
    float content_width = table_width;
    if (caption->bound) {
        content_width -= caption->bound->padding.left + caption->bound->padding.right;
        if (caption->bound->border) {
            content_width -= caption->bound->border->width.left +
                           caption->bound->border->width.right;
        }
    }
    content_width = max(content_width, 0.0f);

    // Save and setup layout context
    BlockContext saved_block = lycon->block;
    Linebox saved_line = lycon->line;
    View* saved_view = lycon->view;

    lycon->view = (View*)caption;
    dom_node_resolve_style((DomNode*)caption, lycon);

    lycon->block.content_width = content_width;
    lycon->block.content_height = 10000;
    lycon->block.advance_y = 0;
    lycon->line.left = 0;
    lycon->line.right = (int)content_width;
    lycon->line.advance_x = 0;
    lycon->line.is_line_start = true;

    // Layout caption content (single pass)
    DomElement* dom_elem = caption->as_element();
    if (dom_elem) {
        for (DomNode* child = dom_elem->first_child; child; child = child->next_sibling) {
            layout_flow_node(lycon, child);
        }
        if (!lycon->line.is_line_start) {
            line_break(lycon);
        }
    }

    // Set caption height including padding
    caption->height = lycon->block.advance_y;
    if (caption->bound) {
        caption->height += caption->bound->padding.top + caption->bound->padding.bottom;
    }

    // Restore context
    lycon->block = saved_block;
    lycon->line = saved_line;
    lycon->view = saved_view;
}

// In table_auto_layout(), remove initial caption layout and only call once:
void table_auto_layout(LayoutContext* lycon, ViewTable* table) {
    // ... column width calculation ...

    // Calculate table width first
    float table_width = calculate_table_width(col_widths, columns, table);

    // Now layout caption with known table width (single pass)
    ViewBlock* caption = find_caption(table);
    if (caption) {
        layout_table_caption(lycon, table, caption, table_width);
    }

    // ... rest of layout ...
}
```

---

## Implementation Priority

| Priority | Enhancement | Estimated Impact | Effort |
|----------|------------|------------------|--------|
| **P0** | Test comparison script enhancement (anonymous box filtering) | ~60% of failures | Low |
| **P1** | Proper colspan/rowspan contribution to track sizing | ~15% of failures | Medium |
| **P2** | Complete border-collapse with column borders | ~10% of failures | Medium |
| **P3** | Row height distribution algorithm | ~8% of failures | Low |
| **P4** | Caption layout single-pass | ~2% of failures | Low |

---

## New Table Layout Pipeline (Summary)

```
┌──────────────────────────────────────────────────────────────────┐
│                     TABLE LAYOUT PIPELINE                         │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│ Phase 1: Structure Analysis                                       │
│ • Existing: analyze_table_structure()                            │
│ • Creates TableMetadata with grid, indices                       │
│ • Anonymous boxes created in DOM (current design retained)       │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│ Phase 2: Intrinsic Size Collection (Enhanced)                    │
│ • NEW: collect_single_column_contributions()                     │
│ • NEW: distribute_colspan_contributions()                        │
│ • Proper percentage width tracking                               │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│ Phase 3: Column Width Resolution (CSS 2.1 §17.5.2)               │
│ • Enhanced: resolve_column_widths_css21()                        │
│ • table-layout: auto vs fixed paths                              │
│ • Proper constrained distribution                                │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│ Phase 4: Row Height Calculation (Enhanced)                        │
│ • NEW: calculate_row_heights_multipass()                         │
│ • Rowspan distribution by span size                              │
│ • Explicit height handling                                       │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│ Phase 5: Cell Content Layout                                      │
│ • Existing: layout_table_cell_content()                          │
│ • Cells laid out with final dimensions                           │
│ • Vertical alignment applied                                     │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│ Phase 6: Final Positioning                                        │
│ • Position rows, cells with computed coordinates                 │
│ • Enhanced: resolve_collapsed_borders() with column support      │
│ • NEW: Single-pass caption layout                                │
└──────────────────────────────────────────────────────────────────┘
```

---

## Testing Strategy

1. **P0 - Test Script Enhancement**
   - Modify `test/layout/compare-layout.js` to filter anonymous boxes
   - Run table test suite to verify improvement
   - Expected: ~60% of anonymous-object tests should pass

2. **P1-P4 - Incremental Implementation**
   - Implement each enhancement independently
   - Run test suite after each change
   - Track pass rate improvement

3. **Regression Protection**
   - Ensure baseline tests continue passing
   - Add unit tests for new functions

---

## Files to Modify

| File | Changes |
|------|---------|
| `test/layout/compare-layout.js` | Add anonymous box filtering |
| `radiant/layout_table.hpp` | New data structures |
| `radiant/layout_table.cpp` | Algorithm enhancements |
| `radiant/view.hpp` | Column/ColumnGroup view types (if needed) |

---

## Success Criteria

| Metric | Current | Target |
|--------|---------|--------|
| Table test pass rate | 0% | >70% |
| Anonymous object tests | 0/200+ | >120/200+ |
| Height algorithm tests | 0/30 | >25/30 |
| Border tests | 0/15 | >12/15 |
