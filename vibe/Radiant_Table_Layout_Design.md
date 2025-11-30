# Radiant Table Layout Design Document

## Overview

The Radiant layout engine implements CSS Table Layout according to the CSS 2.1 specification (Section 17). This document describes the architecture, data structures, and multi-pass algorithm used to compute table layouts.

---

## 1. High-Level Architecture

### 1.1 Unified DOM/View Tree

Radiant uses a **unified DOM/View tree** where DOM nodes and View objects share the same inheritance hierarchy. Table elements extend this architecture with specialized view types.

```
    DomNode (base)
        │
        └── DomElement (extends DomNode)
                │
                └── ViewGroup (extends DomElement)
                        │
                        └── ViewBlock (block elements)
                                │
                                ├── ViewTable (table element)
                                ├── ViewTableRowGroup (thead, tbody, tfoot)
                                ├── ViewTableRow (tr)
                                └── ViewTableCell (td, th)
```

**Key Benefits:**
- Table elements are their own View representations
- Navigation helpers provide uniform iteration regardless of anonymous boxes
- Reference counting and memory management work uniformly

### 1.2 View Coordinates and Box Model

Table views use the same coordinate system as other views:
```cpp
float x, y, width, height;
// (x, y) relative to the BORDER box of parent
// (width, height) forms the BORDER box
```

**Table-Specific Considerations:**
- Cells are positioned relative to their parent row
- Rows are positioned relative to their parent row group
- Row groups are positioned relative to the table
- Border-collapse mode affects how borders overlap between adjacent cells

### 1.3 CSS 2.1 Table Model

The implementation follows CSS 2.1 Section 17, supporting:

| Feature | Description |
|---------|-------------|
| `table-layout: auto` | Content-based column width calculation |
| `table-layout: fixed` | First-row-based column width calculation |
| `border-collapse` | Collapse (shared borders) or separate (spacing) |
| `border-spacing` | Horizontal and vertical spacing between cells |
| `colspan/rowspan` | Cell spanning across multiple columns/rows |
| `vertical-align` | Cell content vertical alignment |
| `caption-side` | Caption positioning (top/bottom) |

---

## 2. Key Data Structures

### 2.1 Table Properties (`TableProp`)

Located in `view.hpp`, stores CSS table-level properties:

```cpp
typedef struct TableProp {
    // Table layout algorithm mode
    enum {
        TABLE_LAYOUT_AUTO = 0,    // Content-based (default)
        TABLE_LAYOUT_FIXED = 1    // Fixed layout
    } table_layout;

    // Caption positioning
    enum {
        CAPTION_SIDE_TOP = 0,
        CAPTION_SIDE_BOTTOM = 1
    } caption_side;

    // Empty cells display
    enum {
        EMPTY_CELLS_SHOW = 0,
        EMPTY_CELLS_HIDE = 1
    } empty_cells;

    // Border model and spacing
    float border_spacing_h;     // Horizontal spacing (px)
    float border_spacing_v;     // Vertical spacing (px)
    int fixed_row_height;       // For fixed layout height distribution
    bool border_collapse;       // true = collapse, false = separate

    // CSS 2.1 Section 17.2.1: Anonymous box flags
    uint8_t is_annoy_tbody:1;   // Table acts as anonymous tbody
    uint8_t is_annoy_tr:1;      // Table/group acts as anonymous tr
    uint8_t is_annoy_td:1;      // Row has anonymous cell wrapper
    uint8_t is_annoy_colgroup:1; // Anonymous colgroup wrapper
} TableProp;
```

### 2.2 Table Cell Properties (`TableCellProp`)

Stores per-cell properties:

```cpp
struct TableCellProp {
    // Vertical alignment
    enum {
        CELL_VALIGN_TOP = 0,
        CELL_VALIGN_MIDDLE = 1,    // CSS 2.1 default for table cells
        CELL_VALIGN_BOTTOM = 2,
        CELL_VALIGN_BASELINE = 3
    } vertical_align;

    // Cell spanning metadata
    int col_span;    // Number of columns spanned (default: 1)
    int row_span;    // Number of rows spanned (default: 1)
    int col_index;   // Computed starting column index
    int row_index;   // Computed starting row index

    // Anonymous box flags (CSS 2.1 Section 17.2.1)
    uint8_t is_annoy_tr:1;
    uint8_t is_annoy_td:1;
    uint8_t is_annoy_colgroup:1;
};
```

### 2.3 Table Metadata Cache (`TableMetadata`)

Runtime structure for table analysis (Phase 3 optimization):

```cpp
struct TableMetadata {
    int column_count;           // Total columns
    int row_count;              // Total rows
    bool* grid_occupied;        // colspan/rowspan tracking (rows × columns)
    int* col_widths;            // Final column widths
    int* col_min_widths;        // Minimum content widths (MCW)
    int* col_max_widths;        // Preferred content widths (PCW)
    int* row_heights;           // Row heights for rowspan
    int* row_y_positions;       // Row Y positions for rowspan

    // Grid accessor for colspan/rowspan tracking
    inline bool& grid(int row, int col);
};
```

### 2.4 Table View Hierarchy

**ViewTable** - Table element with navigation helpers:
```cpp
typedef struct ViewTable : ViewBlock {
    // Navigation helpers for anonymous box support
    ViewTableRow* first_row();
    ViewBlock* first_row_group();
    ViewTableRow* next_row(ViewTableRow* current);
    ViewTableCell* first_direct_cell();
    ViewTableCell* next_direct_cell(ViewTableCell* current);

    // Query anonymous box state
    bool acts_as_tbody();
    bool acts_as_row();
} ViewTable;
```

**ViewTableRowGroup** - Row group (thead, tbody, tfoot):
```cpp
typedef struct ViewTableRowGroup : ViewBlock {
    ViewTableRow* first_row();
    ViewTableRow* next_row(ViewTableRow* current);
} ViewTableRowGroup;
```

**ViewTableRow** - Table row:
```cpp
typedef struct ViewTableRow : ViewBlock {
    ViewTableCell* first_cell();
    ViewTableCell* next_cell(ViewTableCell* current);
    ViewBlock* parent_row_group();
} ViewTableRow;
```

**ViewTableCell** - Table cell (td, th):
```cpp
typedef struct ViewTableCell : ViewBlock {
    // Cell-specific properties stored in td (TableCellProp*)
} ViewTableCell;
```

### 2.5 Layout Context

The standard `LayoutContext` is used with table-specific considerations:

```cpp
struct LayoutContext {
    ViewGroup* parent;
    View* prev_view;
    View* view;              // Current view being processed

    Blockbox block;          // Block formatting context
    Linebox line;            // Inline formatting context
    FontBox font;            // Current font for text measurement

    bool is_measuring;       // True during content width measurement
    // ... other fields
};
```

---

## 3. Table Navigation Helpers

### 3.1 Anonymous Box Support (CSS 2.1 Section 17.2.1)

CSS 2.1 requires anonymous table objects when the document structure is incomplete. Instead of creating new DOM nodes, Radiant uses flags to mark existing elements as "doubled":

| Flag | Meaning |
|------|---------|
| `is_annoy_tbody` | Table acts as its own anonymous tbody |
| `is_annoy_tr` | Row group/table acts as anonymous tr |
| `is_annoy_td` | Row has text wrapped in anonymous cell |
| `is_annoy_colgroup` | Col elements wrapped in anonymous colgroup |

### 3.2 Unified Traversal

Navigation helpers provide consistent iteration regardless of table structure:

```cpp
// Iterate all rows (handles anonymous tbody)
for (ViewTableRow* row = table->first_row(); row; row = table->next_row(row)) {
    // Process row
}

// Iterate cells in a row
for (ViewTableCell* cell = row->first_cell(); cell; cell = row->next_cell(cell)) {
    // Process cell
}

// Handle tables with direct cell children (is_annoy_tr)
if (table->acts_as_row()) {
    for (ViewTableCell* cell = table->first_direct_cell();
         cell; cell = table->next_direct_cell(cell)) {
        // Process cell
    }
}
```

---

## 4. Multi-Pass Layout Algorithm

The table layout uses a **two-pass algorithm** with nested content layout.

### 4.1 Pass Overview

```
┌─────────────────────────────────────────────────────────┐
│  PASS 1: Structure Building + Measurement               │
│  - Build table view tree from DOM                       │
│  - Detect anonymous box situations                      │
│  - Analyze table structure (count rows/columns)         │
│  - Assign cell column/row indices                       │
│  - Measure intrinsic content widths (MCW, PCW)          │
└─────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────┐
│  PASS 2: Layout Algorithm                               │
│  - Apply CSS 2.1 column width algorithm                 │
│  - Position columns (calculate X positions)             │
│  - Layout cell content within calculated widths         │
│  - Calculate row heights                                │
│  - Position rows (calculate Y positions)                │
│  - Handle border-collapse/border-spacing                │
│                                                         │
│  SUB-PASS 2a: Rowspan Height Distribution               │
│  - Recalculate heights for rowspan cells                │
│  - Update row heights if needed                         │
│                                                         │
│  SUB-PASS 2b: Final Dimensions                          │
│  - Apply vertical alignment to cell content             │
│  - Set final table dimensions                           │
└─────────────────────────────────────────────────────────┘
```

### 4.2 Entry Point

Table layout is initiated from `layout_table()` in `layout_table.cpp`:

```cpp
void layout_table(LayoutContext* lycon, DomNode* tableNode, DisplayValue display) {
    // Step 1: Build table structure from DOM
    ViewTable* table = build_table_tree(lycon, tableNode);

    // Step 1.5: Detect anonymous box situations
    detect_anonymous_boxes(table);

    // Step 2: Calculate layout (includes content layout)
    table_auto_layout(lycon, table);

    // Step 3: Update layout context for block integration
    lycon->block.advance_y = table->height;
    lycon->line.is_line_start = true;
}
```

---

## 5. The Table Layout Phases

### Phase 1: Build Table Tree

**Function:** `build_table_tree()`

**Actions:**
- Resolve table-level CSS properties (border-collapse, border-spacing, table-layout)
- Recursively mark all table children with correct view types
- Layout caption content immediately (captions appear above/below table)
- Resolve cell attributes (colspan, rowspan, vertical-align)

**Values Computed:**
- `table->tb->table_layout` - AUTO or FIXED
- `table->tb->border_collapse` - true or false
- `table->tb->border_spacing_h/v` - spacing in pixels
- Cell `td->col_span`, `td->row_span`, `td->vertical_align`

### Phase 2: Detect Anonymous Boxes

**Function:** `detect_anonymous_boxes()`

**Actions:**
- Scan immediate children to detect structure anomalies
- Set `is_annoy_tbody` if table has direct rows without row groups
- Set `is_annoy_tr` if table/group has direct cells without rows
- Mark cells that need anonymous wrapper treatment

**Values Computed:**
- `table->tb->is_annoy_tbody`, `is_annoy_tr`, etc.
- Per-cell `td->is_annoy_*` flags

### Phase 3: Analyze Table Structure

**Function:** `analyze_table_structure()`

**Actions:**
- Count total columns and rows using navigation helpers
- Create metadata structure with grid tracking
- Assign column/row indices to each cell
- Mark grid positions occupied by colspan/rowspan

**Values Computed:**
- `meta->column_count`, `meta->row_count`
- `meta->grid_occupied[]` - boolean grid for spanning
- `cell->td->col_index`, `cell->td->row_index`

### Phase 4: Measure Column Widths

**Functions:** `measure_cell_intrinsic_width()`, `measure_cell_minimum_width()`

**Actions:**
- For each cell, calculate MCW (Minimum Content Width) and PCW (Preferred Content Width)
- MCW = width of longest word (for text) or minimum element width
- PCW = natural width without line wrapping
- Handle explicit CSS widths (percentage and absolute)
- Distribute colspan cell widths across spanned columns

**Values Computed:**
- `meta->col_min_widths[]` - MCW per column
- `meta->col_max_widths[]` - PCW per column
- `meta->col_widths[]` - working column widths

### Phase 5: Apply CSS 2.1 Width Algorithm

**Location:** Inside `table_auto_layout()`

**Algorithm Selection:**

For `table-layout: fixed`:
1. Read explicit table width from CSS
2. Read first-row cell widths (percentage or absolute)
3. Distribute remaining width to unspecified columns equally
4. Scale if needed to fit container

For `table-layout: auto`:
1. Calculate min/pref table widths (sum of MCW/PCW + spacing)
2. Determine used table width based on explicit width or auto
3. Distribute width using CSS 2.1 Section 17.5.2.2 algorithm:
   - If perfect fit: use PCW directly
   - If wider than preferred: distribute extra proportionally
   - If narrower than preferred: scale between MCW and PCW

**Values Computed:**
- Final `col_widths[]` for each column
- `used_table_width` - total table width

### Phase 6: Calculate Column X Positions

**Location:** Inside `table_auto_layout()`

**Actions:**
- Calculate starting X position for each column
- Handle border-spacing (add gaps between columns)
- Handle border-collapse (subtract border overlap)
- Include table padding and border in positioning

**Values Computed:**
- `col_x_positions[]` - X coordinate for each column start
- Adjustments for border model

### Phase 7: Layout Cells and Calculate Row Heights

**Functions:** `process_table_cell()`, `layout_table_cell_content()`

**Actions per cell:**
1. Position cell relative to row using column X positions
2. Set cell width from column widths (sum for colspan)
3. Layout cell content within calculated width
4. Measure content height
5. Apply CSS height if specified
6. Add padding and border to get final cell height
7. Apply vertical alignment to content

**Actions per row:**
1. Position row relative to row group
2. Find maximum cell height (adjusted for rowspan)
3. Set row height
4. Apply border-spacing after row (for separate model)

**Values Computed:**
- `cell->x`, `cell->y`, `cell->width`, `cell->height`
- `row->height`
- `meta->row_heights[]`, `meta->row_y_positions[]`

### Phase 8: Fix Rowspan Heights (Second Pass)

**Location:** Inside `table_auto_layout()`

**Actions:**
- Iterate all cells with rowspan > 1
- Sum heights of spanned rows (including border-spacing)
- Update cell height to match total spanned height

**Values Computed:**
- Updated `cell->height` for rowspan cells

### Phase 9: Finalize Table Dimensions

**Location:** End of `table_auto_layout()`

**Actions:**
- Calculate final table height (rows + padding + border + spacing)
- Add table border to final dimensions
- Set `table->width`, `table->height`, `content_width`, `content_height`

**Values Computed:**
- Final table dimensions

---

## 6. Border Model Implementation

### 6.1 Border-Collapse Mode

When `border-collapse: collapse`:
- Adjacent cell borders share space (overlap)
- No border-spacing applied
- Column positions subtract half-border width for overlap
- Table width reduced by `(columns - 1) × border_width`

```cpp
if (table->tb->border_collapse && i > 1) {
    float overlap = cell_border_width / 2.0f;
    col_x_positions[i] -= (int)(overlap + 0.5f);
}
```

### 6.2 Border-Separate Mode

When `border-collapse: separate`:
- `border-spacing-h` added between columns
- `border-spacing-v` added between rows
- Spacing also added around table edges
- Each cell has independent borders

```cpp
if (!table->tb->border_collapse && table->tb->border_spacing_h > 0) {
    col_x_positions[i] += (int)(table->tb->border_spacing_h + 0.5f);
}
```

---

## 7. Content Width Measurement

### 7.1 Preferred Content Width (PCW)

**Function:** `measure_cell_intrinsic_width()`

Measures natural width without line wrapping:
1. Set measurement mode (`lycon->is_measuring = true`)
2. Apply cell's font properties
3. Set infinite width context (10000px)
4. Measure text using font metrics with kerning
5. Add padding and border
6. Return rounded result

### 7.2 Minimum Content Width (MCW)

**Function:** `measure_cell_minimum_width()`

Calculates narrowest width without overflow:
1. For text: find longest word width
2. For elements: use conservative minimum
3. Add padding and border
4. Round up to prevent overflow

---

## 8. Cell Content Layout

**Function:** `layout_table_cell_content()`

**Actions:**
1. Save current layout context
2. Calculate content area (subtract border and padding)
3. Set up layout context for cell content:
   - `block.content_width` = available content width
   - `line.left/right` = content boundaries
   - `line.advance_x` = starting X after padding
4. Layout children using `layout_flow_node()`
5. Apply CSS vertical-align positioning
6. Restore layout context

**Content Area Calculation:**
```cpp
int content_start_x = border_left + padding_left;
int content_start_y = border_top + padding_top;
int content_width = cell->width - border_left - border_right
                    - padding_left - padding_right;
```

---

## 9. Vertical Alignment

**Function:** `apply_cell_vertical_align()`

| Value | Algorithm |
|-------|-----------|
| `top` | No offset (content at top) |
| `middle` | `offset = (content_area - content_height) / 2` |
| `bottom` | `offset = content_area - content_height` |
| `baseline` | Align to text baseline (simplified to top) |

**Implementation Note:** For ViewText children, both `view->y` and `view->rect->y` (TextRect) must be updated. The JSON output uses TextRect coordinates for text positioning, so failing to update `rect->y` causes text to appear at wrong vertical positions.

---

## 10. File Organization

| File | Purpose |
|------|---------|
| `layout_table.cpp` | Main table layout implementation |
| `layout_table.hpp` | Function declarations |
| `view.hpp` | TableProp, TableCellProp, View classes |
| `layout.hpp` | LayoutContext, shared layout structures |

---

## 11. Helper Functions

### Cell Helpers

| Function | Purpose |
|----------|---------|
| `get_cell_css_width()` | Get explicit CSS width (percentage/length) |
| `get_explicit_css_height()` | Get explicit CSS height |
| `measure_cell_content_height()` | Measure height from children |
| `calculate_cell_height()` | Final height with padding/border |
| `apply_cell_vertical_align()` | Position content vertically (updates View and TextRect) |
| `position_cell_text_children()` | Set text positions in cell |
| `calculate_cell_width_from_columns()` | Sum column widths for colspan |
| `process_table_cell()` | Complete cell processing |
| `is_cell_empty()` | Check if cell has no visible content |

### Table Helpers

| Function | Purpose |
|----------|---------|
| `resolve_table_properties()` | Parse CSS table properties |
| `parse_cell_attributes()` | Parse colspan, rowspan, valign |
| `mark_table_node()` | Assign view types recursively |
| `calculate_rowspan_heights()` | Distribute height for rowspan |
| `is_visibility_collapse()` | Check if element has visibility:collapse |
| `apply_fixed_row_height()` | Apply fixed height and re-apply vertical alignment |

---

## 12. CSS 2.1 Compliance Analysis

### 12.1 Implemented Features

| Feature | Spec Reference | Status | Notes |
|---------|----------------|--------|-------|
| `table-layout: auto` | §17.5.2.2 | ✅ Complete | MCW/PCW calculation, proportional distribution |
| `table-layout: fixed` | §17.5.2.1 | ✅ Complete | First-row width, equal distribution |
| `border-collapse: collapse/separate` | §17.6 | ✅ Complete | Border overlap and spacing |
| `border-spacing` | §17.6.1 | ✅ Complete | Horizontal and vertical spacing |
| `colspan` / `rowspan` | §17.5 | ✅ Complete | Grid tracking, height distribution |
| `vertical-align` (top/middle/bottom) | §17.5.3 | ✅ Complete | Content positioning + TextRect update |
| `visibility: collapse` | §17.5.5 | ✅ Complete | Rows excluded from layout, space reclaimed |
| `empty-cells: hide` | §17.6.1.1 | ✅ Complete | Hides borders/backgrounds of empty cells |
| `caption-side: bottom` | §17.4.1 | ✅ Complete | Caption positioned after table rows |
| CSS `display: table` on divs | §17.2 | ✅ Complete | Non-table elements with table display values |
| Anonymous table boxes | §17.2.1 | ⚠️ Partial | Flags for tbody/tr detection |
| Caption layout | §17.4 | ✅ Complete | Top and bottom positioning |

### 12.2 Gaps and Missing Features

#### Gap 1: Column and Column Group Elements (CSS 2.1 §17.2)

**Status:** ❌ Not Implemented

CSS 2.1 allows `<col>` and `<colgroup>` elements to specify column widths:

```html
<table>
  <colgroup>
    <col width="100px">
    <col width="200px">
  </colgroup>
  ...
</table>
```

**Current behavior:** Column groups are ignored entirely.

**Required behavior:**
- Fixed layout should use `<col>` widths before cell widths
- `<colgroup span="N">` should apply properties to N columns
- Column widths should participate in width algorithm

**Implementation notes:**
- Add `ViewTableColumn` and `ViewTableColumnGroup` view types
- Parse `width`, `span` attributes during structure building
- Integrate column widths into Phase 4 (Measure Column Widths)

---

#### Gap 2: Caption Positioning (CSS 2.1 §17.4.1)

**Status:** ✅ Complete

`caption-side: bottom` positions caption after table rows. Property read in `resolve_table_properties()` and stored in `table->tb->caption_side`.

---

#### Gap 3: Baseline Vertical Alignment (CSS 2.1 §17.5.3)

**Status:** ❌ Simplified to Top

**Current behavior:** `vertical-align: baseline` is treated as top alignment.

**Required behavior per CSS 2.1:**
1. Find first text baseline in each cell
2. All baseline-aligned cells in a row share a common baseline
3. Row baseline = maximum of cell baselines
4. Cells are positioned so their baselines align

**Implementation notes:**
- Calculate baseline offset during text layout using font metrics
- Store `baseline_offset` in `TableCellProp`
- In row layout, find maximum baseline across baseline-aligned cells
- Adjust cell Y positions to align baselines

---

#### Gap 4: Table Height Algorithm (CSS 2.1 §17.5.3)

**Status:** ✅ Complete

Explicit CSS height on `<tr>` elements is now read via `style_tree_get_declaration()`. Row heights respect the larger of content height and explicit CSS height. Vertical alignment is re-applied after row height is finalized using `apply_fixed_row_height()`. TextRect y-coordinates are updated for ViewText nodes to ensure text positions match view positions.

---

#### Gap 5: Border Conflict Resolution (CSS 2.1 §17.6.2)

**Status:** ⚠️ Simplified

**Current behavior:** Uses maximum border width among adjacent borders.

**Required behavior per CSS 2.1 priority order:**
1. Borders with `border-style: hidden` suppress all borders at that edge
2. Wider borders win over narrower borders
3. Style precedence: `double` > `solid` > `dashed` > `dotted` > `ridge` > `outset` > `groove` > `inset`
4. Origin precedence: cell > row > row group > column > column group > table

**Implementation notes:**
- Create `resolve_border_conflict()` function
- Compare borders from all sources (cell, row, row group, column, table)
- Apply precedence rules in order
- Cache resolved borders per edge to avoid repeated calculation

---

#### Gap 6: Empty Cells (CSS 2.1 §17.6.1.1)

**Status:** ✅ Complete

`empty-cells: hide` implemented. `is_cell_empty()` helper checks for empty cells. Border/background painting skipped for empty cells in separate border model.

---

#### Gap 7: Table Width Constraints (CSS 2.1 §17.5.2)

**Status:** ⚠️ Partial

**Current behavior:**
- `min-width` / `max-width` on table not enforced
- No handling when table exceeds container width

**Required behavior:**
- Table width clamped by `min-width` and `max-width`
- Shrink-to-fit for `width: auto` in specific contexts (floats, abs pos)
- Overflow handling when content exceeds constraints

**Implementation notes:**
- Read `min-width`, `max-width` from table CSS
- Clamp `used_table_width` after algorithm
- Handle overflow via `overflow` property

---

#### Gap 8: Row Group Ordering (CSS 2.1 §17.2)

**Status:** ❌ Not Implemented

**Current behavior:** Row groups rendered in DOM order.

**Required behavior:**
- `<thead>` renders before all `<tbody>` sections
- `<tfoot>` renders after all `<tbody>` sections
- Multiple `<tbody>` sections render in DOM order between thead/tfoot

**Implementation notes:**
- During structure building, collect row groups by type
- Reorder: thead first, then tbody sections, then tfoot
- Alternative: multi-pass layout (thead, tbody*, tfoot)

---

#### Gap 9: Visibility: collapse (CSS 2.1 §17.5.5)

**Status:** ✅ Complete

`is_visibility_collapse()` helper checks the CSS visibility property. Rows with `visibility: collapse` are excluded from structure analysis and layout. Space is reclaimed (unlike `visibility: hidden`).

---

#### Gap 10: Spanning Cell Width Distribution (CSS 2.1 §17.5.2.2)

**Status:** ⚠️ Simplified

**Current behavior:** Extra width from colspan cells distributed equally.

**Required behavior per CSS 2.1:**
- Distribute proportionally to non-spanning column widths
- Iterative constraint satisfaction for complex spanning patterns
- Consider percentage widths in distribution

**Implementation notes:**
- After initial column width pass, run spanning cell distribution
- Weight distribution by existing column widths
- May require multiple iterations for convergence

---

#### Gap 11: Percentage Width Edge Cases (CSS 2.1 §17.5.2.2)

**Status:** ⚠️ Partial

**Current behavior:**
- Percentage widths work when table has explicit width
- Not handled when table width is auto

**Required behavior:**
- Percentage widths should work with auto table width (resolve to auto)
- Conflicting percentages (sum > 100%) should be normalized
- Percentage on spanning cells distributed to columns

**Implementation notes:**
- Track which columns have percentage widths
- Normalize if sum exceeds 100%
- Resolve percentage columns after fixed-width columns

---

#### Gap 12: Full Anonymous Box Generation (CSS 2.1 §17.2.1)

**Status:** ⚠️ Partial

**Current behavior handles:**
- Direct rows under table → anonymous tbody
- Direct cells under table → anonymous tbody + tr

**Missing cases:**
- Text directly in table/row → should wrap in anonymous cell
- Non-table elements in table context → should generate wrappers
- `display: table-cell` without parent row → anonymous row
- `display: table-row` without parent tbody → anonymous tbody

**Implementation notes:**
- Enhance `detect_anonymous_boxes()` to handle all cases
- May need to insert wrapper views during structure building
- Check display type of all descendants, not just immediate children

---

### 12.3 Implementation Priority

#### Completed ✅

| Gap | Feature | Notes |
|-----|---------|-------|
| 2 | `caption-side: bottom` | Implemented |
| 4 | Row height / vertical-align | CSS row height + TextRect fix |
| 6 | `empty-cells: hide` | Implemented |
| 9 | `visibility: collapse` | Rows excluded from layout |

#### High Priority (Remaining)

| Gap | Feature | Rationale |
|-----|---------|-----------|
| 1 | `<col>` / `<colgroup>` width support | Common in data tables |
| 5 | Border conflict resolution | Visual correctness for styled tables |

#### Medium Priority

| Gap | Feature | Rationale |
|-----|---------|-----------|
| 3 | Baseline vertical alignment | Text alignment in mixed-content cells |
| 8 | Row group ordering (thead/tfoot) | Semantic table structure |
| 11 | Percentage width edge cases | Complex table layouts |

#### Low Priority (Edge Cases)

| Gap | Feature | Rationale |
|-----|---------|-----------|
| 12 | Full anonymous box generation | Rare malformed HTML |
| 7 | `min-width` / `max-width` constraints | Uncommon on tables |
| 10 | Complex colspan distribution | Edge case optimization |

---

## 13. Debug Logging

Enable debug output by setting log level in `log.conf`:

```
[layout_table]
level = DEBUG
```

Key log messages:
- `Table layout mode:` - auto vs fixed
- `Table border-spacing:` - spacing values
- `Column X width:` - final column widths
- `CSS 2.1 Case X:` - width distribution algorithm path
- `Rowspan cell fix:` - height distribution for spanning cells

---

## 14. References

- [CSS 2.1 Tables (Section 17)](https://www.w3.org/TR/CSS21/tables.html)
- [CSS 2.1 Table Width Algorithm (Section 17.5.2)](https://www.w3.org/TR/CSS21/tables.html#width-layout)
- [CSS 2.1 Table Height Algorithm (Section 17.5.3)](https://www.w3.org/TR/CSS21/tables.html#height-layout)
- [CSS 2.1 Border Conflict Resolution (Section 17.6.2)](https://www.w3.org/TR/CSS21/tables.html#border-conflict-resolution)
- [CSS 2.1 Anonymous Table Objects (Section 17.2.1)](https://www.w3.org/TR/CSS21/tables.html#anonymous-boxes)
