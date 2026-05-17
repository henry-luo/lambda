# Radiant Layout Enhancement Proposal

Date: 2026-05-16

## Executive Summary

Radiant has a strong architectural core: a unified DOM/View tree, relative coordinates, dedicated layout-mode modules, `AvailableSpace`, `LayoutCache`, scratch allocation, and shared alignment helpers. The codebase is not "badly structured"; it is a working engine that has grown feature-by-feature around real CSS edge cases.

The main risk is uneven structure. Some newer contracts are clean and reusable, but they are not consistently used across block, inline, flex, grid, table, positioned, form, and intrinsic sizing code. As a result, similar mechanics are reimplemented in several places: content-box/border-box math, min/max clamping, axis abstraction, percentage resolution, absolute child layout, layout pass orchestration, cache usage, context save/restore, and logging.

The highest-value improvement is not a rewrite. It is to extract common "layout primitives" and then make every flow use the same contracts.

## Current Structure Assessment

### What Is Working Well

- Layout files are organized by CSS formatting context, matching the design doc: `layout_block.cpp`, `layout_inline.cpp`, `layout_text.cpp`, `layout_flex.cpp`, `layout_flex_multipass.cpp`, `layout_grid.cpp`, `layout_grid_multipass.cpp`, `layout_table.cpp`, `layout_positioned.cpp`, and `intrinsic_sizing.cpp`.
- The design doc already names this intended file ownership in `doc/dev/Radiant_Layout_Design.md`.
- `layout.hpp` centralizes `LayoutContext`, `BlockContext`, `Linebox`, flex/grid context pointers, `AvailableSpace`, run mode, sizing mode, scratch arena, and depth guards.
- `layout_alignment.hpp` is a good example of the direction the layout code should continue moving: a small shared module extracted from duplicated flex/grid alignment logic.
- `available_space.hpp` and `layout_cache.hpp` are promising Taffy/Ladybird-style primitives for robust multi-pass layout.
- `block_context.cpp` is a good example of isolating one cross-cutting CSS concept, BFC/float handling, instead of burying it in `layout_block.cpp`.

### Where The Structure Is Uneven

- Large files still hold multiple responsibilities:
  - `layout_table.cpp`: about 9.4K lines.
  - `layout_block.cpp`: about 7.8K lines.
  - `layout_flex.cpp`: about 6.3K lines.
  - `intrinsic_sizing.cpp`: about 5.0K lines.
- Flex and grid have "core" plus "multipass/enhanced" layers, but the pass contracts are informal. `layout_grid_multipass.cpp` says it follows the same pattern as flex, but the shared pattern is not encoded as a common API.
- Many modules depend on forward declarations for functions owned by other `.cpp` files instead of stable headers. This makes ownership blurry and makes accidental coupling easier.
- `layout.hpp` is doing too much. It contains core layout context types, BFC types, line-break state, flex container state, helpers, allocation APIs, and many declarations. It is the common include, so growth here increases compile coupling.
- Some new grid headers still depend on `std::vector`, `std::sort`, and `std::min/std::max` despite the project convention requiring local `lib` equivalents. Several headers already contain TODO migration notes, so the issue is known but not finished.
- Debug comments such as `CRITICAL FIX`, `HACK`, `workaround`, and "legacy" appear in core layout paths. Some are helpful historical markers, but they also show where behavior has accreted without being converted into named invariants or shared helpers.

## Duplicated Or Repeated Mechanics To Extract

### 1. Box Model Geometry

Repeated patterns:

- Add/subtract padding and border to convert between content box and border box.
- Clamp content width/height to zero.
- Apply min/max constraints.
- Recompute content size for forms, grid items, table cells, captions, flex items, and absolute children.

Examples:

- `layout_block.cpp` has `adjust_min_max_width`, `adjust_min_max_height`, `adjust_border_padding_width`, and `adjust_border_padding_height`.
- `layout_flex.cpp` has `get_content_width`, `get_content_height`, border offset helpers, and axis-aware size helpers.
- `layout_form.cpp`, `layout_grid_multipass.cpp`, and `layout_table.cpp` manually subtract padding/border again.

Proposal:

Create `radiant/layout_box.hpp` and `radiant/layout_box.cpp`.

Core API:

```cpp
typedef struct BoxEdges {
    float left;
    float right;
    float top;
    float bottom;
} BoxEdges;

typedef struct BoxMetrics {
    BoxEdges margin;
    BoxEdges padding;
    BoxEdges border;
    float padding_h;
    float padding_v;
    float border_h;
    float border_v;
    float pad_border_h;
    float pad_border_v;
} BoxMetrics;

BoxMetrics layout_box_metrics(ViewBlock* block);
float layout_content_width_from_border_box(ViewBlock* block, float border_width);
float layout_content_height_from_border_box(ViewBlock* block, float border_height);
float layout_border_width_from_content_box(ViewBlock* block, float content_width);
float layout_border_height_from_content_box(ViewBlock* block, float content_height);
float layout_apply_min_max_width(ViewBlock* block, float width, bool width_is_border_box);
float layout_apply_min_max_height(ViewBlock* block, float height, bool height_is_border_box);
```

Benefits:

- One source of truth for content-box versus border-box behavior.
- Removes repeated negative-size clamps.
- Makes future fixes to `box-sizing`, min/max, and form/replaced elements less risky.

### 2. Containing Block And Percentage Resolution

Repeated patterns:

- Flex absolute children recompute percentage width/height against flex container content size.
- Grid item style resolution temporarily mutates `lycon->block.parent` to use grid container content width.
- Table and block paths repeatedly infer parent content width from parent view dimensions.

Proposal:

Create `radiant/layout_containing_block.hpp/cpp`.

Core API:

```cpp
typedef struct ContainingBlock {
    ViewBlock* view;
    float content_x;
    float content_y;
    float content_width;
    float content_height;
    float padding_x;
    float padding_y;
    float border_x;
    float border_y;
    bool has_definite_width;
    bool has_definite_height;
} ContainingBlock;

ContainingBlock layout_containing_block_for_child(LayoutContext* lycon, ViewBlock* parent);
ContainingBlock layout_grid_area_containing_block(GridContainerLayout* grid, ViewBlock* item);
void layout_resolve_percent_lengths_for_child(ViewBlock* child, ContainingBlock cb);
```

Benefits:

- Removes ad hoc percentage re-resolution.
- Makes flex/grid/table positioned children share the same coordinate and sizing assumptions.
- Helps root-cause bugs where percentages were resolved against the viewport or wrong parent.

### 3. Absolute And Fixed Child Layout

Repeated patterns:

- Flex has a local `layout_flex_absolute_children`.
- Grid has a separate `layout_grid_absolute_children`.
- Both save `BlockContext` and `Linebox`, set up containing block dimensions, call `layout_abs_block`, then apply container-specific static-position corrections.

Proposal:

Create `radiant/layout_abs_children.hpp/cpp`.

Core API:

```cpp
typedef enum AbsStaticContextKind {
    ABS_STATIC_BLOCK,
    ABS_STATIC_FLEX,
    ABS_STATIC_GRID,
} AbsStaticContextKind;

typedef struct AbsStaticContext {
    AbsStaticContextKind kind;
    ContainingBlock containing_block;
    FlexContainerLayout* flex;
    GridContainerLayout* grid;
} AbsStaticContext;

void layout_absolute_children_in_context(LayoutContext* lycon, ViewBlock* container, AbsStaticContext ctx);
```

Flex and grid would still provide their own static-position policy, but the common loop, context save/restore, percentage resolution, and `layout_abs_block` call would be shared.

Benefits:

- Reduces one of the most fragile duplicated paths.
- Makes absolute positioning tests easier to add across block/flex/grid.
- Reduces drift between flex/grid static-position behavior.

### 4. Axis-Abstraction Helpers

Flex already has local main/cross-axis helpers. Grid and alignment code have similar concepts but use their own representation.

Proposal:

Create `radiant/layout_axis.hpp`.

Core API:

```cpp
typedef enum LayoutAxis {
    LAYOUT_AXIS_X,
    LAYOUT_AXIS_Y,
} LayoutAxis;

float layout_axis_size(ViewElement* item, LayoutAxis axis);
void layout_axis_set_size(ViewElement* item, LayoutAxis axis, float size);
float layout_axis_pos(ViewElement* item, LayoutAxis axis);
void layout_axis_set_pos(ViewElement* item, LayoutAxis axis, float pos);
LayoutAxis flex_main_axis(FlexContainerLayout* flex);
LayoutAxis flex_cross_axis(FlexContainerLayout* flex);
```

Benefits:

- Shrinks flex helper code.
- Gives grid/flex/table alignment common language.
- Makes reverse/writing-mode extensions easier later.

### 5. Layout Pass Orchestration

Flex and grid both follow a multipass style:

- Resolve styles and initialize views.
- Measure intrinsic sizes.
- Run the mode algorithm.
- Lay out final content.
- Lay out absolute children.
- Store cache result.

But only grid currently has explicit cache lookup at the entry point, while flex has a different orchestration flow.

Proposal:

Create a small pass contract rather than a generic framework:

```cpp
typedef struct LayoutPassState {
    LayoutContext* lycon;
    ViewBlock* container;
    BlockContext saved_block;
    Linebox saved_line;
    AvailableSpace saved_available_space;
    radiant::RunMode saved_run_mode;
    radiant::SizingMode saved_sizing_mode;
} LayoutPassState;

void layout_pass_enter(LayoutPassState* state, LayoutContext* lycon, ViewBlock* container);
void layout_pass_leave(LayoutPassState* state);
bool layout_try_cached_size(LayoutContext* lycon, ViewBlock* container, radiant::SizeF* out_size);
void layout_store_cached_size(LayoutContext* lycon, ViewBlock* container);
```

Benefits:

- Makes context save/restore explicit.
- Makes cache use consistent across block/flex/grid/table/intrinsic sizing.
- Prevents measurement mode from accidentally performing final positioning or mutating content twice.

### 6. Intrinsic Measurement API

Intrinsic sizing is a shared service, but flex, grid, table, and forms still contain special measurement code paths.

Proposal:

Promote `intrinsic_sizing.cpp` into a clearer public API:

```cpp
typedef struct IntrinsicSize {
    float min_width;
    float max_width;
    float min_height;
    float max_height;
    bool has_baseline;
    float first_baseline;
    float last_baseline;
} IntrinsicSize;

IntrinsicSize layout_measure_intrinsic(LayoutContext* lycon, DomNode* node, AvailableSpace space);
IntrinsicSize layout_measure_replaced(ViewBlock* block, AvailableSpace space);
IntrinsicSize layout_measure_form_control(ViewBlock* block, AvailableSpace space);
```

Benefits:

- Gives flex/grid/table one way to measure children.
- Makes baseline calculation cacheable.
- Reduces local "measure this child again" code.

## Proposed File Organization

Keep existing mode files, but move shared concepts out of them.

New or expanded modules:

| File | Responsibility |
|------|----------------|
| `layout_box.hpp/cpp` | Box edges, content/border conversion, min/max constraints |
| `layout_axis.hpp` | Axis-agnostic get/set helpers |
| `layout_containing_block.hpp/cpp` | Containing block geometry and percentage bases |
| `layout_abs_children.hpp/cpp` | Shared absolute/fixed child loop and context setup |
| `layout_pass.hpp/cpp` | Pass enter/leave, cache lookup/store, mode guards |
| `layout_measure.hpp/cpp` | Public intrinsic/replaced/form measurement facade |
| `layout_debug.hpp/cpp` | Structured layout logging helpers and optional snapshots |

Target ownership after extraction:

| Existing file | Desired role |
|---------------|--------------|
| `layout_block.cpp` | Block flow, margin collapsing, BFC interactions, dispatch |
| `layout_inline.cpp` | Inline element flow and inline-block interaction |
| `layout_text.cpp` | Text shaping, line breaking, whitespace, output text rects |
| `layout_flex.cpp` | Flex algorithm phases only |
| `layout_flex_multipass.cpp` | Flex pass orchestration only |
| `layout_grid.cpp` | Grid track sizing and placement only |
| `layout_grid_multipass.cpp` | Grid pass orchestration only |
| `layout_table.cpp` | Table model, row/column/cell algorithm only |
| `layout_positioned.cpp` | Absolute/fixed solving and relative/sticky offsets |
| `intrinsic_sizing.cpp` | Shared intrinsic algorithms, behind a smaller facade |

## Robustness Improvements

### 1. Encode Invariants As Helpers

Important invariants should be impossible to bypass accidentally:

- All layout dimensions are `float`.
- View `x/y` are relative to parent border box.
- View `width/height` are border-box dimensions.
- `content_width/content_height` never go below zero.
- Measurement mode must not permanently position children.
- Percentage lengths must be resolved against the correct containing block.

The more these are embedded in helpers, the less the code depends on comments and memory.

### 2. Add RAII-Style Context Guards Without Heavy C++

The code frequently saves and restores `lycon->block`, `lycon->line`, `lycon->run_mode`, `lycon->available_space`, and mode-specific pointers. A small C-compatible guard API would reduce missing-restore bugs:

```cpp
typedef struct LayoutContextSnapshot {
    BlockContext block;
    Linebox line;
    AvailableSpace available_space;
    radiant::RunMode run_mode;
    radiant::SizingMode sizing_mode;
    FlexContainerLayout* flex_container;
    GridContainerLayout* grid_container;
} LayoutContextSnapshot;

void layout_context_save(LayoutContext* lycon, LayoutContextSnapshot* out);
void layout_context_restore(LayoutContext* lycon, LayoutContextSnapshot* snap);
```

### 3. Replace "Critical Fix" Comments With Named Cases

Many comments describe bugs that are now implicit tests. Convert the most important ones into named regression fixtures:

- flex absolute static position with reverse direction
- grid absolute child inside named grid area
- percentage size resolution in nested flex/grid
- border-box min/max clamping
- float avoidance for BFC roots
- table caption width with border/padding
- inline-block baseline in flex/grid/table cells

### 4. Finish The `std::*` Migration In Radiant Layout Headers

Several grid headers still mention or use `std::vector`, `std::sort`, and `std::min/std::max`. This conflicts with the project convention and creates a different allocation style inside layout. Migrate these to fixed-capacity arrays, `ArrayList`, or pool/scratch arrays.

Priority:

1. `grid_baseline.hpp`
2. `grid_occupancy.hpp`
3. `grid_enhanced_adapter.hpp`
4. `grid_sizing_algorithm.hpp`
5. `layout_positioned.cpp` local `using std::min/max`

### 5. Make Logging Searchable But Quieter By Default

Radiant logs are very rich, but some hot paths log broad messages. Keep detailed logs behind explicit layout debug categories:

- `LAYOUT_BOX`
- `LAYOUT_PASS`
- `LAYOUT_ABS`
- `LAYOUT_FLEX`
- `LAYOUT_GRID`
- `LAYOUT_TABLE`
- `LAYOUT_TEXT`
- `LAYOUT_CACHE`

This keeps `log.txt` useful during crashes without making performance tests noisy.

## Performance Improvements

### 1. Apply Layout Cache Consistently

`LayoutCache` exists and grid uses it in its multipass entry. Expand use to:

- flex container measurement
- intrinsic size measurement
- table cell min/max measurement
- repeated text measurement at same width
- replaced/form element measurement

Cache keys should include:

- known dimensions
- available space
- run mode
- sizing mode
- relevant style generation or invalidation counter
- font generation for text nodes

### 2. Move Temporary Arrays To ScratchArena

The layout context already has `ScratchArena`. Use it for:

- grid item arrays
- table metadata arrays
- flex line temporary arrays
- intrinsic measurement lists
- absolute child collection

Avoid per-pass heap allocation in hot layout paths.

### 3. Avoid Full Final Layout During Measurement

Make `RunMode::ComputeSize` enforceable:

- no final child positioning
- no duplicate text rect output
- no image decode beyond dimensions
- no scrollbar/scroller updates
- no view tree snapshot logging

Then audit each layout mode for measurement leaks.

### 4. Add Targeted Timing Buckets

Existing global timers are useful but scattered. Wrap them in a single `LayoutProfiler` struct:

```cpp
typedef struct LayoutProfiler {
    double block_ms;
    double inline_ms;
    double text_ms;
    double flex_ms;
    double grid_ms;
    double table_ms;
    double intrinsic_ms;
    double style_ms;
    double image_ms;
    int64_t cache_hits;
    int64_t cache_misses;
} LayoutProfiler;
```

Report top offenders by node/source location in release layout profiling. Do not use debug builds for performance testing.

## Testing Improvements

The design doc describes browser-reference layout tests under `test/layout/data/*`; in this checkout `test/layout` is a symlink to the external `lambda-test/layout`. Keep that split, but make the local test workflow more discoverable.

Recommended test matrix:

| Area | Add focused fixtures for |
|------|--------------------------|
| Box model | content-box, border-box, min/max, negative clamp |
| Containing block | percentage width/height in block/flex/grid/table cells |
| Absolute layout | block/flex/grid static position, reverse flex, grid area containing block |
| Flex | grow/shrink, wrap, baseline, auto margins, nested flex |
| Grid | fr sizing, minmax, auto-fit/fill, item placement, absolute children |
| Table | fixed/auto layout, colspan/rowspan, captions, collapsed borders |
| Inline/text | whitespace, line-height, vertical-align, CJK, trailing spaces |
| Performance | large markdown table, image-heavy docs, deeply nested layout |

For each refactor phase:

1. Run existing baseline layout tests.
2. Add one regression file per extracted invariant.
3. Compare browser reference with 1-2px tolerance.
4. Run `make check-int-cast` after any Radiant layout code changes.

## Phased Implementation Plan

### Phase 1: Low-Risk Extraction

- Add `layout_box.hpp/cpp`.
- Move width/height conversion and min/max helpers from block/flex/form/grid/table call sites.
- Keep old helper names as wrappers temporarily if needed.
- Add box model regression tests.

Expected result: less duplicate box math, fewer content/border bugs.

### Phase 2: Containing Block And Absolute Children

- Add `layout_containing_block.hpp/cpp`.
- Refactor flex/grid percentage re-resolution to use it.
- Add `layout_abs_children.hpp/cpp`.
- Move common absolute child iteration out of flex/grid multipass files.
- Add regression tests for flex/grid absolute children.

Expected result: less drift between positioned behavior in layout modes.

### Phase 3: Pass And Measurement Contracts

- Add `layout_pass.hpp/cpp`.
- Standardize cache lookup/store.
- Add `layout_measure.hpp/cpp` facade over intrinsic sizing, replaced elements, and form controls.
- Audit `RunMode::ComputeSize` behavior for mutation leaks.

Expected result: fewer duplicate measurements and clearer multipass structure.

### Phase 4: Grid Header And `std::*` Cleanup

- Replace remaining grid `std::vector` and `std::sort` usage with local fixed arrays, `ArrayList`, pool arrays, or scratch arrays.
- Move large inline implementations out of headers where practical.
- Preserve behavior with grid regression tests.

Expected result: project convention compliance and reduced compile coupling.

Implementation status:

- Done: `grid_baseline.hpp` now uses fixed-capacity row/item scratch arrays instead of STL vectors.
- Done: `grid_occupancy.hpp` now uses tracked `MEM_CAT_LAYOUT` storage and explicit cleanup instead of an STL vector.
- Done: grid sizing and adapter headers use local `grid_min_value`/`grid_max_value` helpers and `qsort`/small-array sorting instead of STL algorithms.
- Done: grid-facing headers no longer introduce live `std::` references or STL container/algorithm includes.
- Deferred: moving the larger grid algorithms out of headers should be handled separately because it requires build-graph ownership decisions, not just mechanical extraction.
- Verification: `make check-int-cast`, `make build`, direct layout smokes for grid/table/flex/positioning, and `make layout test=grid_align_items_baseline` passed. `make layout test=grid_004_auto_placement` still reports a layout mismatch around auto-fit/auto-placement and should be tracked as a grid behavior issue rather than bundled into this structural cleanup.

### Phase 5: Table And Intrinsic Sizing Simplification

- Split table helpers only where they have clear ownership: captions, cell content layout, metadata/grid occupancy, width distribution.
- Do not split table code mechanically by line count.
- Move reusable table measurement behavior behind `layout_measure`.

Expected result: table remains understandable, but common measurement behavior stops being table-local.

Implementation status:

- Done: extracted table scratch metadata and grid occupancy ownership into `layout_table_metadata.hpp/cpp`.
- Done: extracted caption width adjustment and caption re-layout into `layout_table_caption.hpp/cpp`.
- Deferred: cell content layout and width distribution remain in `layout_table.cpp` because their call graph is still tightly coupled to row/column sizing, border collapse, and inline flow state.
- Verification: `make check-int-cast`, `make build`, direct table/caption/grid/flex layout smokes passed. `make layout test=table_004_auto_layout` still reports an 8.4px row-height mismatch while returning exit code 0; track that as a table behavior issue, not a structural extraction result.

## Priority Recommendations

1. Start with `layout_box` because it touches many duplicated areas and has a small, testable surface.
2. Then extract containing-block/absolute-child helpers because flex and grid are already duplicating high-risk positioning behavior.
3. Then standardize pass/cache/measurement contracts to improve performance without changing algorithms.
4. Then migrate grid headers away from `std::*`.
5. Finally, reduce `layout_table.cpp` and `intrinsic_sizing.cpp` only along natural boundaries.

## Success Criteria

- Layout mode files contain algorithms, not repeated geometry plumbing.
- Flex and grid multipass flows use the same pass vocabulary.
- Box sizing and percentage resolution have one implementation path.
- Absolute children in block/flex/grid are tested through the same helper layer.
- `RunMode::ComputeSize` is reliably side-effect-light.
- Layout cache hit rate is visible and improves on repeated measurements.
- Radiant layout headers no longer introduce `std::vector`, `std::sort`, or `std::min/std::max`.
- Existing layout regression tests keep passing, and new focused tests cover each extracted invariant.
