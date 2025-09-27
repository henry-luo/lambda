# Radiant HTML Table Support: Incremental Implementation Plan

## Scope and Goals

- Add baseline support for HTML tables, focusing on correct layout of `table`, `thead`, `tbody`, `tfoot`, `tr`, `th`, `td`, `colgroup`, `col`, and `caption`.
- Implement CSS table formatting model with progressive fidelity: separate borders first, collapsed borders later.
- Integrate with existing Radiant architecture for layout (`radiant/layout.cpp`), rendering (`radiant/render.cpp`), and events (`radiant/event.cpp`).
- Emphasize correctness of layout (width/height distribution, spanning, baseline) before advanced visual details.

---

## Architectural Integration Points

- **Entry point selection**: `layout_flow_node()` in `radiant/layout.cpp` currently routes nodes by display. We will extend display resolution to include table-related display types and dispatch to a new `layout_table_*` pipeline.
- **Style resolution**: `dom_node_resolve_style()` already visits element styles. Ensure CSS table-related properties are parsed and stored (e.g., `border-collapse`, `border-spacing`, `table-layout`, `vertical-align`, `caption-side`).
- **View tree**: Introduce table-specific view classes/types without polluting `ViewBlock`.
  - Define `ViewTable` that extends `ViewBlock` to represent the table formatting context root. All table-specific state lives on `ViewTable` (not on `ViewBlock`).
  - Rows and cells remain regular blocks for rendering efficiency but use distinct view types for clarity: `ViewTableRowGroup`, `ViewTableRow`, and `ViewTableCell`, each extending `ViewBlock` with minimal or no extra fields (metadata-only where needed).
  - Avoid adding table-specific fields to `ViewBlock`. Keep table metadata in the derived `ViewTable` (and minimal metadata in row/cell views if strictly necessary).
- **Rendering**: Reuse `render_block_view()` and `render_bound()` for backgrounds and borders in separate-border mode. Add specialized helpers later for collapsed borders.
- **Events**: Reuse existing hit-testing (`target_block_view()`/`target_children()`) since cells and rows are normal blocks. Add cell metadata to facilitate table-aware interactions when needed.

---

## Data Model Additions (New structures)

Introduce table-specific view classes that inherit from `ViewBlock`, plus property structs allocated via `alloc_prop`. Do not add table fields to `ViewBlock`.

- `ViewTable : ViewBlock`
  - Acts as the root container for the table grid.
  - Holds a pointer to `TableModel` (internal layout model, see below) and a `TableProp` for CSS-facing configuration.
  - View type: `RDT_VIEW_TABLE` (new enum). Rendering path can still leverage `render_block_view()` for background/border.

- `ViewTableRowGroup : ViewBlock`
  - Represents `thead`, `tbody`, `tfoot`. Minimal metadata only (e.g., group type enum) to assist layout ordering.
  - View type: `RDT_VIEW_TABLE_ROW_GROUP` (new).

- `ViewTableRow : ViewBlock`
  - Represents `tr`. May carry computed row baseline and final height cached by the algorithm, but avoid storing large arrays.
  - View type: `RDT_VIEW_TABLE_ROW` (new).

- `ViewTableCell : ViewBlock`
  - Represents `td`/`th`. Metadata only: `col_index`, `row_index` (computed), and span values when implemented.
  - View type: `RDT_VIEW_TABLE_CELL` (new).

- `TableProp` (attach to `ViewTable`)
  - `PropValue table_layout` (auto | fixed)
  - `PropValue border_collapse` (separate | collapse)
  - `int border_spacing_h`, `int border_spacing_v`
  - `PropValue caption_side` (top | bottom)

- `TableCellProp` (attach to `ViewTableCell`)
  - `int col_span` (default 1), `int row_span` (default 1)
  - `PropValue vertical_align` (baseline | top | middle | bottom)

- `TableColProp` (attach to `col`/`colgroup`)
  - `int given_width` (-1 for auto; percentages/absolute supported)
  - `int span` (for `colgroup`)

- `TableModel` (internal, not exposed as a view)
  - Holds transient structures computed during layout: column count, resolved column widths, row heights, cell grid with spanning occupancy.
  - Lives only on `ViewTable` and is rebuilt during layout to avoid state leakage.

Note: Dimensions still use inherited `ViewBlock` fields. Table-specific arrays/logic are confined to `TableModel` on `ViewTable`.

---

## Display Mapping and Anonymous Table Objects

Per CSS 2.1 table model, define display mapping and insertion of anonymous boxes if DOM does not contain the expected intermediate wrappers. Initial milestone can defer anonymous objects by assuming well-formed markup, then add anonymous generation.

- Supported display values:
  - `table`, `inline-table`
  - `table-row-group` (`tbody`), `table-header-group` (`thead`), `table-footer-group` (`tfoot`)
  - `table-row` (`tr`)
  - `table-cell` (`td`, `th`)
  - `table-column-group` (`colgroup`), `table-column` (`col`)
  - `table-caption` (`caption`)

- Phase 1: Assume canonical structure:
  `table` → [optional `caption`], [0..n `colgroup`], [thead?], [0..n tbody], [tfoot?]

- Phase 2: Add anonymous wrappers for non-canonical trees as per CSS spec.

---

## Layout Algorithm: Phased Plan (emphasis on layout)

### Phase 1: Minimum Viable Table (separate borders, no spans)

- Constraints:
  - No `rowspan`/`colspan` initially.
  - `table-layout: auto` only.
  - `border-collapse: separate` only.
  - Respect `border-spacing` and `caption-side`.

- Steps:
  1. **Dispatch**: Extend `layout_flow_node()` to detect table display and call `layout_table()`.
  2. **Table box sizing**: Determine table available width from parent like a block-level box (`Blockbox` mechanics, `box-sizing` honored). Content width is initially unknown.
  3. **Column discovery**: From first row (`tr`) or from explicit `colgroup`/`col` widths, infer the number of columns and initial preferred widths per column.
  4. **Intrinsic width collection (auto layout)**:
     - For each cell, perform a child layout pass constrained to an upper bound (infinite or parent constraint) to obtain the cell min-content and max-content widths (approximation acceptable in MVP: use measured child width after normal flow as both min/max to simplify).
     - The column preferred width is the max across its cells’ preferred widths.
  5. **Column width resolution**:
     - Sum preferred widths + inter-column horizontal spacing (`border-spacing_h`). If sum < table available content width, distribute extra space proportionally (simple even distribution in MVP). If sum > available, shrink proportionally but never below measured minimum (MVP: single preferred metric).
  6. **Row height calculation**:
     - Layout each cell’s content with the resolved column width as constraint. Measure resulting cell height.
     - Row height is the max of its cells’ heights.
  7. **Cell vertical alignment**:
     - Apply `vertical-align` for each cell. For MVP, implement `top` and `middle`; add `bottom` later. Baseline alignment can be deferred.
  8. **Positioning**:
     - Compute `x` positions per column with accumulated `border-spacing_h` and table padding/borders.
     - Compute `y` positions per row with `border-spacing_v` and previous row heights.
  9. **Caption**:
     - If present and `caption-side: top`, layout caption first, place above table grid. If `bottom`, place below grid.
  10. **Write results**:
      - Set `ViewBlock` widths/heights for table, rows, and cells. Update `content_width`/`content_height` of the table for scroller logic.

- Rendering (Phase 1):
  - Use `render_block_view()` and `render_bound()` per table/cell block to draw background and borders in separate mode. Respect `border-spacing` by positioning only.

- Events (Phase 1):
  - Existing hit-testing will target cells as blocks. No special handling needed. Add optional metadata to identify row/column indices for future features.

### Phase 2: Spanning and Better Intrinsic Sizing

- Add `rowspan` and `colspan` support:
  - Build a grid occupancy matrix for each row, placing cells across multiple columns/rows.
  - Column width resolution becomes a constraint problem:
    - For a spanning cell, its min/max contributes to the sum of spanned columns. Distribute contributions according to current column weights and adjust iteratively until convergence or a bounded iteration count.
  - Row height calculation accommodates `rowspan`: row heights in the span must meet or exceed the spanning cell height. Distribute extra height evenly or per spec approximation.

- Improve intrinsic widths:
  - Capture min-content vs max-content per cell by performing two measures (or reasonable approximations) to better match browsers.

- Vertical alignment and baseline:
  - Implement `baseline` alignment for cells within a row. Track first-in-flow baseline from cell content (text baseline or first child block baseline approximation).

### Phase 3: `table-layout: fixed`

- Column widths determined from `table` width, `col`/`colgroup` specified widths, and remaining width distribution.
- Avoid content-based measuring for width; height still content-driven.
- This improves performance on large tables.

### Phase 4: `border-collapse: collapse`

- Implement collapsed border conflict resolution:
  - Compute border precedence per edge (style priority, width, source).
  - Draw shared borders once per grid edge rather than per cell.
- Rendering requires a custom draw pass over the grid edges instead of per-cell borders.

### Phase 5: Anonymous Table Objects and Non-Canonical DOM

- Insert anonymous `table-row` and `table-row-group` where required by spec when encountering `table-cell` or inline content directly under `table`.
- Ensure robustness to real-world markup.

---

## Integration Details and Touch Points

- **layout.cpp (`layout_flow_node`)**
  - Extend display resolution to classify table display types.
  - Route to new functions creating specific view subclasses:
    - `layout_table_box()` constructs a `ViewTable` and owns the full table algorithm.
    - Internally, create `ViewTableRowGroup`, `ViewTableRow`, `ViewTableCell` instances as children and attach minimal metadata.
    - External callers should not layout table descendants directly; the table algorithm orchestrates child layout.

- **New source file**: `radiant/layout_table.cpp` (+ header)
  - Encapsulate the table layout algorithm and helpers:
    - Grid builder (rows, columns, spanning resolution)
    - Column width solver (auto/fixed)
    - Row height computation
    - Positioning and alignment
  - Produces `ViewTable`/`ViewTableRow(Group)`/`ViewTableCell` nodes; reuses `LayoutContext`.

- **Rendering (`radiant/render.cpp`)**
  - Phase 1/2/3: Keep using `render_block_view()` for `ViewTable*` subclasses; render backgrounds/borders per node in separate mode.
  - Phase 4: Add a specialized pass `render_table_collapsed_borders(ViewTable*)` and invoke it when the block is a `ViewTable` with `border-collapse: collapse`.

- **Events (`radiant/event.cpp`)**
  - Default targeting works. Optionally attach cell indices on `ViewBlock` via an extension struct to enable features (hover highlight, selection).

- **Style resolution**
  - Ensure CSS parser maps table properties to our new props (`TableProp`, `TableCellProp`, etc.).
  - Respect existing pixel ratio and box-sizing handling (see `vibe/Radiant_Layout.md`).

---

## Incremental Milestones and Tasks

- Milestone 1: MVP (separate borders, no spans, auto layout)
  - Parse and store table properties.
  - Implement `layout_table.cpp` with auto column sizing from first row, border-spacing handling, caption top/bottom, basic vertical-align top/middle.
  - Integrate dispatch in `layout_flow_node()`.
  - Reuse rendering and events.

- Milestone 2: Spanning and improved intrinsic sizing
  - Grid occupancy and span solver.
  - Approximate min/max content widths.
  - Row height distribution with rowspan.
  - Add cell baseline alignment.

- Milestone 3: `table-layout: fixed`
  - Implement fixed algorithm using `col` widths and remaining distribution.
  - Performance validation with large tables.

- Milestone 4: Border collapse
  - Conflict resolution and edge rendering.

- Milestone 5: Anonymous table objects
  - Robust handling of non-canonical trees.

---

## Validation and Testing Strategy

- Unit tests for layout helpers in `layout_table.cpp` (column width solver, spanning resolution, row height calc).
- Integration tests in `test/layout/data/basic/`:
  - `table_001_basic_auto.html`: simple 3x3, separate borders, spacing, caption top/bottom.
  - `table_002_col_widths.html`: explicit `colgroup/col` widths, percentages.
  - `table_003_vertical_align.html`: top/middle/bottom in cells.
  - `table_004_spans.html`: basic colspan/rowspan.
  - `table_005_fixed_layout.html`: `table-layout: fixed`.
  - `table_006_border_collapse.html`: collapsed borders precedence (later phase).
- Reference capture via Puppeteer (see `vibe/Radiant_Layout.md`) and tolerance-based comparison.

---

## Performance Considerations

- Avoid repeated child measurement in `auto` layout by caching intrinsic sizes per cell content (hash by node + constraints where possible).
- For very wide tables, prefer `table-layout: fixed` for O(rows + cols) behavior.
- Defer collapsed borders to a dedicated pass to minimize per-cell branching during render.

---

## Risks and Mitigations

- Intrinsic sizing divergence from browsers in MVP.
  - Mitigate with incremental refinements and browser comparison tests.
- Spanning solver complexity.
  - Start with iterative proportional distribution with caps; add heuristics as needed.
- Border-collapse rendering correctness.
  - Build a dedicated edge model and draw pass; compare against browser snapshots.

---

## Deliverables per Milestone

- Updated `layout_flow_node()` dispatch and new `layout_table.cpp/hpp`.
- Extended style resolution for table properties.
- New integration tests and reference baselines.
- Documentation updates in this file and `vibe/Radiant_Layout.md` cross-references.
