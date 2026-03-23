# Radiant Flex Layout — Unified Design Document

**Status:** Current as of December 2025  
**Replaces:** `Radiant_Flexbox0.md` through `Radiant_Flexbox10.md`, `Radiant_Flex_Layout_Design.md`, `Radiant_Flex_Layout_Nested.md`

---

## 1. Architecture Overview

### 1.1 Unified DOM/View Tree

Radiant uses a **unified DOM/View tree** where DOM nodes *are* view objects — there is no separate parallel tree. All layout state is carried inside the same nodes.

```
DomNode  (base: first_child, next_sibling, parent)
  └─ DomElement  (tag, attributes, specified_style, display, item_prop_type)
       └─ ViewElement  (x, y, width, height, bound, blk, embed, fi, position …)
            ├─ ViewSpan   (inline-level box; font, linebox data)
            └─ ViewBlock  (block-level box; extends ViewSpan)
```

Text nodes (`DomText`) share the same tree but are not `ViewElement` descendants.

**Key consequences:**
- A single traversal of `first_child → next_sibling` handles both DOM queries and view layout.
- Moving a parent's `(x, y)` automatically repositions all children visually (coordinates are relative to the parent's border-box origin).
- Flex item properties (`fi`), grid item properties (`gi`), table cell properties (`td`), and form control properties (`form`) are stored in a **union** on `ViewElement` identified by `item_prop_type`. **Never access the wrong union member.**

### 1.2 Coordinate System

```
(x, y)        — relative to parent's BORDER-BOX origin
(width, height) — the BORDER-BOX of this element (includes padding + border, excludes margin)
```

Absolute (viewport-relative) position is obtained by walking the parent chain:

```cpp
float abs_x = view->x;
ViewElement* p = view->parent ? view->parent->as_element() : nullptr;
while (p) { abs_x += p->x; p = p->parent ? p->parent->as_element() : nullptr; }
```

---

## 2. Key Data Structures

All types live in `radiant/view.hpp` or `radiant/layout.hpp`.

### 2.1 CSS Container Properties — `FlexProp`

Stored inside `EmbedProp.flex` pointer on the container element. Populated by `resolve_css_style.cpp`.

```cpp
typedef struct FlexProp {
    int direction;     // CSS_VALUE_ROW | ROW_REVERSE | COLUMN | COLUMN_REVERSE
    int wrap;          // CSS_VALUE_NOWRAP | WRAP | WRAP_REVERSE
    int justify;       // flex-start, flex-end, center, space-between, space-around, space-evenly
    int align_items;   // stretch (default), flex-start, flex-end, center, baseline
    int align_content; // stretch (default), flex-start, flex-end, center, space-between, space-around
    float row_gap;
    float column_gap;
    bool row_gap_is_percent;
    bool column_gap_is_percent;
    WritingMode writing_mode;
    TextDirection text_direction;
    int  first_baseline;       // computed first baseline of the container (post-layout)
    bool has_baseline_child;   // true if first line has baseline-aligned items
} FlexProp;
```

### 2.2 Flex Item Properties — `FlexItemProp`

Stored in `ViewElement.fi`. Only present when an element participates as a flex item.

```cpp
typedef struct FlexItemProp {
    float flex_basis;    // -1 = auto
    float flex_grow;
    float flex_shrink;
    CssEnum align_self;
    int   order;
    float aspect_ratio;
    float baseline_offset;

    // --- Intrinsic sizing cache (computed during measurement phase) ---
    IntrinsicSizes intrinsic_width;    // .min_content, .max_content
    IntrinsicSizes intrinsic_height;
    // --- Resolved min/max constraints (from BlockProp given_min_*/given_max_*) ---
    float resolved_min_width, resolved_max_width;    // resolved_max = FLT_MAX if none
    float resolved_min_height, resolved_max_height;
    // --- Hypothetical cross sizes (Phase 4.5) ---
    float hypothetical_cross_size;       // inner (content box)
    float hypothetical_outer_cross_size; // outer (with margins)

    // Bitfield flags
    int flex_basis_is_percent : 1;
    int is_margin_top_auto    : 1;
    int is_margin_right_auto  : 1;
    int is_margin_bottom_auto : 1;
    int is_margin_left_auto   : 1;
    int has_intrinsic_width   : 1;
    int has_intrinsic_height  : 1;
    int needs_measurement     : 1;
    int has_explicit_width    : 1;
    int has_explicit_height   : 1;
    int main_size_from_flex   : 1;  // true when parent flex grew/shrank this item's main axis
} FlexItemProp;
```

### 2.3 Runtime Container State — `FlexContainerLayout`

Lives in `LayoutContext.flex_container` (stack-allocated per layout pass). Extends `FlexProp` with runtime state.

```cpp
typedef struct FlexContainerLayout : FlexProp {
    View** flex_items;   // collected flex items for this container
    int item_count;
    int allocated_items;

    FlexLineInfo* lines;
    int line_count;
    int allocated_lines;

    float main_axis_size;   // container content dimension on main axis
    float cross_axis_size;  // container content dimension on cross axis
    bool  needs_reflow;

    // CSS Flexbox §9.2 — sizing mode flags
    bool main_axis_is_indefinite;  // true for shrink-to-fit containers
    bool has_definite_cross_size;  // true when cross axis has an explicit CSS size

    LayoutContext* lycon;  // back-pointer for intrinsic sizing helpers
} FlexContainerLayout;
```

### 2.4 Flex Line — `FlexLineInfo`

```cpp
typedef struct FlexLineInfo {
    View** items;
    int   item_count;
    int   main_size;       // total main-axis space consumed
    int   cross_size;      // cross-axis height of this line (after Phase 5)
    int   cross_position;  // line's absolute cross-axis start (set by align_content)
    int   free_space;      // remaining space after flexible length resolution
    float total_flex_grow;
    float total_flex_shrink;
    int   baseline;        // first-line baseline offset
} FlexLineInfo;
```

### 2.5 Layout Context — `LayoutContext`

Passed through every layout call.

```cpp
typedef struct LayoutContext {
    View*        view;
    DomNode*     elmt;
    BlockContext block;
    Linebox      line;
    FontBox      font;
    float        root_font_size;
    FlexContainerLayout* flex_container; // active flex container (stack)
    GridContainerLayout* grid_container; // active grid container (stack)
    DomDocument* doc;
    UiContext*   ui_context;
    AvailableSpace available_space;
    radiant::RunMode run_mode; // ComputeSize | PerformLayout | PerformHiddenLayout
    // …
} LayoutContext;
```

---

## 3. File Organization

| File | Lines | Purpose |
|------|-------|---------|
| `layout_flex.cpp` | ~5850 | Core 10-phase flex algorithm, all axis helpers, constraint resolution |
| `layout_flex_multipass.cpp` | ~2340 | Top-level orchestration, auto-height/width, nested content, abs children |
| `layout_flex_measurement.cpp` | ~2080 | Content measurement, intrinsic sizing, measurement cache |
| `layout_flex.hpp` | — | Function declarations, enum types, `FlexLineInfo` |
| `layout_flex_multipass.hpp` | — | Multi-pass function declarations |
| `layout_flex_measurement.hpp` | — | Measurement function declarations, `MeasurementCacheEntry` |
| `view.hpp` | — | `FlexProp`, `FlexItemProp`, `EmbedProp` |
| `layout.hpp` | — | `FlexContainerLayout`, `LayoutContext` |
| `layout_block.cpp` | — | Flex dispatch point (`display.inner == CSS_VALUE_FLEX` branch) |
| `layout_alignment.hpp` | — | `fallback_alignment_for_overflow()` and related helpers |
| `intrinsic_sizing.hpp/.cpp` | — | `measure_element_intrinsic_widths()` used by flex |

---

## 4. Algorithm: Entry Point and Multi-Pass Structure

### 4.1 Top-Level Call Chain

```
layout_block.cpp:
  layout_block_content()
    → [if display.inner == CSS_VALUE_FLEX]
        layout_flex_content()          ← top-level entry (layout_flex_multipass.cpp)
          ├─ init_flex_container()     ← CSS → FlexContainerLayout
          ├─ [PASS 1] measure_all_flex_children_content()  ← fill measurement cache
          ├─ layout_flex_container_with_nested_content()   ← orchestrates everything
          │    ├─ init_flex_container() again (for *this* nested container)
          │    ├─ collect_and_prepare_flex_items()         ← unified pass
          │    ├─ [AUTO-HEIGHT/WIDTH calculation]
          │    ├─ run_enhanced_flex_algorithm()
          │    │    ├─ layout_flex_container()             ← 10-phase core algorithm
          │    │    └─ apply_auto_margin_centering()
          │    ├─ layout_final_flex_content()              ← lay out nested content
          │    ├─ reposition_baseline_items()              ← fix baselines after content
          │    └─ layout_flex_absolute_children()          ← abs/fixed children
          └─ cleanup_flex_container()
```

### 4.2 `init_flex_container()`

Allocates and populates `FlexContainerLayout` on the heap. Key responsibilities:

- Copies `FlexProp` from `container->embed->flex` (or applies defaults: `direction=ROW`, `align_items=STRETCH`).
- Resolves percentage gaps (`row_gap`, `column_gap`) against container content dimensions. For auto-size containers where dimensions are unknown, percentage gaps resolve to 0 and are re-resolved later once the size is known.
- Sets `main_axis_size` and `cross_axis_size` from the container's computed content-box dimensions.
- Sets `main_axis_is_indefinite` and `has_definite_cross_size` flags per CSS Flexbox §9.2.
- For absolutely-positioned elements with auto width (`shrink-to-fit` mode), `main_axis_size` starts at 0 and is computed from item widths later in Phase 4b.

### 4.3 Measurement Pass

`measure_all_flex_children_content()` iterates DOM children before creating views, filling the **measurement cache** (`MeasurementCacheEntry[]`). The cache maps `DomNode* → {measured_width, measured_height, content_width, content_height}`.

Key rules:
- Cache is **never cleared between passes** — it must survive from PASS 1 into the flex algorithm.
- Only cleared at the start of a fresh top-level layout.
- Text nodes are measured via `measure_text_content_accurate()` using `FontBox` metrics.
- Block elements use element-type heuristics (H1–H6, P, UL, DIV, etc.) or real font measurement.

### 4.4 `collect_and_prepare_flex_items()`

**Unified single pass** that combines:
1. DOM traversal (`first_child → next_sibling`)
2. View creation / style resolution for each child
3. Percentage `width`/`height` re-resolution relative to *this* container (not an ancestor)
4. Measurement cache lookup to seed `FlexItemProp.intrinsic_width/height`

Items are excluded from the flex algorithm if they are:
- `display: none`
- `position: absolute` or `position: fixed` (handled separately by `layout_flex_absolute_children`)
- `visibility: hidden` with no intrinsic dimensions

---

## 5. The 10-Phase Flex Algorithm (`layout_flex_container`)

### Phase 1 — Collect Flex Items
- Delegates to `collect_and_prepare_flex_items()` if items were not already pre-collected. Uses the View-tree path (`container->first_child`) only as a legacy fallback.
- Stores result in `flex_layout->flex_items[]` and `flex_layout->item_count`.

### Phase 2 — Sort by CSS `order`
`sort_flex_items_by_order()` — stable insertion sort; equal-`order` items preserve document order.

### Phase 2.5 — Resolve Per-Item Constraints
`apply_constraints_to_flex_items()` calls `resolve_flex_item_constraints()` for each item:
- `min-width: auto` → resolved to item's min-content width.
- `min-height: auto` → resolved to item's min-content height.
- Resolved values stored in `fi->resolved_min_width/height` and `fi->resolved_max_width/height`.

### Phase 3 — Create Flex Lines
`create_flex_lines()`:
- **`flex-wrap: nowrap`** → single line containing all items.
- **`flex-wrap: wrap` / `wrap-reverse`** → calculates `flex-basis` for each item; accumulates into current line until overflow; then starts a new line.
- Per line: computes `total_flex_grow`, `total_flex_shrink`, `main_size`.

**Flex-basis resolution priority:**

| Condition | Resolved basis |
|-----------|----------------|
| `flex-basis: <length>` | That length |
| `flex-basis: <percentage>` | Percentage × container `main_axis_size` |
| `flex-basis: auto` with explicit CSS `width`/`height` on main axis | That CSS dimension |
| `flex-basis: auto`, no explicit dimension | Item's max-content intrinsic width/height |
| `flex-basis: content` | Item's max-content intrinsic size |

For border-box items the effective flex base is floored at `padding + border` on the main axis (content-box cannot be negative).

### Phase 4 — Resolve Flexible Lengths (CSS §9.7)
`resolve_flexible_lengths()` — the most critical phase. Implements the CSS spec iterative algorithm:

```
1. Compute free_space = main_axis_size - Σ(flex_basis) - gaps
2. If free_space > 0 and Σ(flex_grow) > 0:
     Determine GROWING case
3. If free_space < 0 and Σ(flex_shrink × flex_basis) > 0:
     Determine SHRINKING case
4. For each unfrozen item, compute raw target size:
     GROW:   target = flex_basis + (flex_grow / Σflex_grow) × free_space
     SHRINK: target = flex_basis - (flex_shrink × flex_basis / Σ(flex_shrink × flex_basis)) × |free_space|
5. Clamp target by resolved_min/max constraints
6. If clamped ≠ raw target → freeze item, accumulate violation
7. Distribute violation back to remaining unfrozen items
8. Repeat until all items are frozen
```

`apply_flex_constraint()` is the **single source of truth** for min/max clamping.

### Phase 4b — Shrink-to-Fit Container Width Update
After all items receive their resolved sizes, absolutely-positioned containers with `width: auto` recalculate `main_axis_size` as `Σ(item widths)` (no gap added — the container wraps content exactly).

### Phase 4.5 — Hypothetical Cross Sizes
`determine_hypothetical_cross_sizes()` — direct implementation of CSS Flexbox §9.4 step 5:
- For each item: measure its cross-axis size at the resolved main-axis size.
- Uses `measure_element_intrinsic_widths()` for items with auto cross-axis size.
- Stores result in `fi->hypothetical_cross_size` and `fi->hypothetical_outer_cross_size`.

### Phase 5 — Calculate Line Cross Sizes
`calculate_line_cross_sizes()`:
- Each line's `cross_size` = `max(fi->hypothetical_outer_cross_size)` across its items.
- Items that *will* be stretched by `align-items: stretch` are excluded from this max — their cross size is determined by the line, not the other way around (`item_will_stretch()` predicate).

### Phase 6 — Main Axis Alignment (justify-content)
`align_items_main_axis()`:

| `justify-content` | Behaviour |
|-------------------|-----------|
| `flex-start` / `start` | Items packed to main-start |
| `flex-end` / `end` | Items packed to main-end |
| `center` | Items centered |
| `space-between` | Equal gaps between items (first/last at edges) |
| `space-around` | Equal space around each item (half-space at edges) |
| `space-evenly` | Equal space including edges |

When `free_space < 0`, `fallback_alignment_for_overflow()` returns `FLEX_START` for space-based modes.

Gaps (`column_gap` for row flex, `row_gap` for column flex) are interleaved between items.  
`margin: auto` on the main axis is handled *after* this step by `apply_auto_margin_centering()`.

### Phase 7 — Finalize Container Cross Size
After line cross sizes are known, for **auto-height containers** (no explicit CSS height):
- For **row flex**: `cross_axis_size = Σ(line.cross_size) + row_gap × (lines - 1)`; container `height` updated.
- For **column flex**: `main_axis_size = Σ(line.cross_size) + column_gap × (lines - 1)`; container `height` updated.

This phase checks `has_explicit_height` carefully:
- `given_height > 0` (not `>= 0`) ← a `given_height` of 0 means `height: 0` explicitly, but is only set when CSS specifies it.
- Items with `main_size_from_flex = 1` AND `flex_grow > 0` are treated as having explicit height, preventing inner content from shrinking a container that was grown by its parent.

### Phase 8 — Align Content (multi-line)
`align_content()` — distributes space among flex lines on the cross axis. Supports all six `align-content` values. For `wrap-reverse`, line order is reversed. Each line's `cross_position` field is set here; items are moved in Phase 9.

For `align-content: stretch`: extra cross space is divided equally among lines; items in those lines are re-measured if they have auto cross size (mimicking Taffy / Yoga approach).

### Phase 9 — Cross Axis Alignment (align-items / align-self)
`align_items_cross_axis()` positions each item within its line. Respects:
- Per-line `cross_position` from Phase 8.
- `align-self` override per item; `auto` falls back to container `align_items`.
- `stretch`: item's cross size set to `line.cross_size` (clamped by `resolved_min/max`).
- `baseline`: items aligned via `calculate_item_baseline()` (see §6).

### Phase 10 — Relative Positioning Offsets
For items with `position: relative`, CSS `top`/`right`/`bottom`/`left` offsets are applied as translations on top of the flex-computed position.

---

## 6. Baseline Alignment

`calculate_item_baseline(item)`:
1. If item has a cached `fi->baseline_offset` from text layout → use it.
2. Otherwise recursively search first participating child: `baseline = child_baseline + child.y`.
3. Fallback (empty box): synthesize baseline at bottom of margin box (`margin_top + height`).

`find_max_baseline(line, container_align_items)`:
- Scans items where `align-self == ALIGN_BASELINE` or the container's `align_items == ALIGN_BASELINE`.
- Returns the maximum baseline offset across all such items.

`reposition_baseline_items(flex_container)`:
- Called **after** `layout_final_flex_content()` so nested content is fully laid out.
- For each line, recalculates `max_baseline` using actual child dimensions.
- Updates each baseline-aligned item's cross position: `pos = max_baseline - item_baseline`.

---

## 7. Nested Flex Containers

### The Chicken-and-Egg Problem

A nested flex container has a dual role:
- **As a flex item**: its size is determined by the parent flex algorithm.
- **As a flex container**: it must run its own flex algorithm for its children.

The ordering is:
1. Parent flex sizes the item (main axis and possibly cross axis via stretch).
2. Child flex container runs with those dimensions as its `main_axis_size` / `cross_axis_size`.

### Implementation

`layout_flex_container_with_nested_content()` handles this by:

1. **Pre-seeding width from parent cross axis** (for column parent + `stretch` child):  
   If `pa_flex` is a column container and this item should stretch, `flex_container->width = pa_flex->cross_axis_size` is set before `init_flex_container` runs.

2. **Auto-height after `collect_and_prepare_flex_items`**:  
   - Row flex with auto height: `max(item heights)` → update `cross_axis_size` and `flex_container->height`.
   - Column flex with auto height: `Σ(item heights + margins) + gaps` → update `main_axis_size` and `flex_container->height`.
   - Column flex with auto width: `max(item widths)` → update `cross_axis_size` and `flex_container->width`.

3. **Preserving parent-set heights**: Auto-height never shrinks a container below a height already set by a parent's `flex-grow` or `align-items: stretch`.

### Height Definiteness Propagation Rules

| Scenario | `has_explicit_height` |
|----------|-----------------------|
| CSS `height: <value>` where `given_height > 0` | true |
| Parent row flex `align-items: stretch` | true |
| Parent column flex, explicit `flex-basis >= 0` | true |
| Parent flex `flex-grow > 0` and item was grown | true (via `main_size_from_flex`) |
| Parent grid item with non-zero row assignment | true |
| Everything else | false (auto-height) |

---

## 8. Absolute Positioned Children in Flex Containers

Handled by `layout_flex_absolute_children()` after the main flex algorithm:

- Percentage `width`/`height` are **re-resolved** against the flex container as the containing block (not the viewport — which is the CSS-resolution-time ancestor).
- Static position rules (CSS Flexbox §4.1): the child is positioned "as if it were the sole flex item".
  - **justify-content** affects main axis static position.
  - **align-items** affects cross axis static position.
  - Each axis is handled independently: if `left`/`right` is explicitly set, don't touch `x`.
- For reverse directions (`row-reverse`, `column-reverse`): static position is adjusted to the end of the container after `layout_abs_block` runs (when item dimensions are known).
- `aspect-ratio` is applied to compute the missing dimension if only one of width/height is specified.

---

## 9. Shrink-to-Fit Containers

Absolutely-positioned flex containers with `width: auto` use shrink-to-fit sizing:

1. `main_axis_size` starts at 0 in `init_flex_container`.
2. Phase 4b: after flexible lengths are resolved, `main_axis_size = Σ(item main sizes)`.
3. `container->width` is updated to include padding and border.
4. **Exception for percentage gaps**: if `column_gap` is a percentage, re-resolution happens after `main_axis_size` is known.
5. **Exception for explicit flex-basis children**: items with `flex-basis >= 0` override `given_width` for sizing; the summing loop uses `flex_basis` directly in that case.

`min-width` and `max-width` constraints are respected: if either is present, shrink-to-fit is suppressed.

---

## 10. CSS Compatibility Layer

Lexbor (the HTML/CSS parser) does not support `justify-content: space-evenly` as a standard value. Workaround:

```css
/* In HTML, use custom property as fallback */
x-justify-content: space-evenly;
```

`resolve_css_style.cpp` intercepts `LXB_CSS_PROPERTY__CUSTOM` with the name `x-justify-content` and sets `embed->flex->justify = CSS_VALUE_SPACE_EVENLY`.

---

## 11. Overflow Fallback Alignment

Implemented in `layout_alignment.hpp` / `layout_alignment.cpp`:

```cpp
int fallback_alignment_for_overflow(int alignment, float remaining_space) {
    if (remaining_space >= 0) return alignment;
    switch (alignment) {
        case SPACE_BETWEEN: case SPACE_AROUND: case SPACE_EVENLY: case STRETCH:
            return FLEX_START;  // safe fallback per CSS Box Alignment §5.3
        default:
            return alignment;
    }
}
```

---

## 12. Percentage Values

| Property | Resolved against |
|----------|-----------------|
| `width: <pct>` | **`main_axis_size`** of containing flex container |
| `height: <pct>` | Containing block's `cross_axis_size` if definite; else 0 |
| `flex-basis: <pct>` | Container `main_axis_size` |
| `gap: <pct>` | Corresponding content-box dimension; 0 if auto-size at init time |
| `margin: auto` | Consumes remaining free space (main) or line cross space (cross) |

Percentage `width`/`height` are stored raw in `blk->given_width_percent` and `blk->given_height_percent` (NaN if not percentages), and are **re-resolved** in `collect_and_prepare_flex_items` against the actual container, overriding the ancestor-relative values from CSS resolution time.

---

## 13. Axis Helpers

```cpp
bool is_main_axis_horizontal(FlexProp* flex);
// Axis getters/setters (abstract over row vs column direction)
float get_main_axis_size(ViewElement* item, FlexContainerLayout* flex);
float get_main_axis_outer_size(ViewElement* item, FlexContainerLayout* flex);
void  set_main_axis_size(ViewElement* item, float size, FlexContainerLayout* flex);
void  set_main_axis_position(ViewElement* item, float pos, FlexContainerLayout* flex);
float get_cross_axis_size(ViewElement* item, FlexContainerLayout* flex);
void  set_cross_axis_size(ViewElement* item, float size, FlexContainerLayout* flex);
void  set_cross_axis_position(ViewElement* item, float pos, FlexContainerLayout* flex);
// Constraint application (single source of truth)
float apply_flex_constraint(ViewElement* item, float computed, bool is_main,
                            FlexContainerLayout* flex, bool* hit_min, bool* hit_max);
float apply_stretch_constraint(ViewElement* item, float container_cross,
                               FlexContainerLayout* flex);
```

---

## 14. Test Suite Structure

| Suite | Location | Description |
|-------|----------|-------------|
| `baseline` | `test/layout/data/baseline/` | 1330 core regression tests (100% passing) |
| `flex-nest` | `test/layout/data/flex-nest/` | 8 HTML tests for nested flex layouts |
| Taffy/Yoga flex | `test/layout/data/advanced/` | ~270 flex tests (uses `test_base_style.css`; all `<div>` are flex containers) |

Running tests:

```bash
make layout suite=flex-nest                    # nested flex suite
make layout suite=baseline                     # core regression
make layout test=flex_019_nested_flex.html     # single test
node test/layout/test_radiant_layout.js -c flex-nest -t flex_014_nested_flex.html -v
```

Approximate pass rates at time of writing (Taffy flex suite):

| Category | Pass rate | Notes |
|----------|-----------|-------|
| Baseline regression | 100% | Protected; must not regress |
| justify-content | ~71% | Mostly working |
| align-self | ~80% | Mostly working |
| align-content (stretch) | ~90% | Improved in Phase 9 |
| flex-wrap / wrap-reverse | ~83% | Improved; some edge cases remain |
| align-content wrapping | ~56% | Work in progress |
| flex grow/shrink/basis | ~20%–47% | Core sizing gaps |
| align-items (baseline) | ~47% | Simple cases pass; recursive cases fail |
| absolute pos in flex | ~70% | Phase 5 improvements |
| Nested flex | ~60%+ elements | Height, cross-axis gaps remain |

---

## 15. Debugging

Enable per-module debug logging in `log.conf`:

```
[layout_flex]
level = DEBUG
```

Key log prefixes for tracing:

| Prefix | Phase |
|--------|-------|
| `FLEX START` | Entry to `layout_flex_container` |
| `FLEX PROPERTIES` | Container direction/wrap/justify/align values |
| `Phase 4:` | Flexible length resolution |
| `ITERATIVE_FLEX` | Constraint resolution iterations |
| `Phase 4.5` | Hypothetical cross size determination |
| `AUTO-HEIGHT` | Auto-height calculation in multipass |
| `AUTO-WIDTH` | Auto-width calculation in multipass |
| `MAIN_AXIS_ALIGN` | Phase 6 justify-content positioning |
| `CROSS_ALIGN_ITEM` | Phase 9 align-items positioning |
| `COLLECT_FLEX_ITEMS` | Item collection and measurement |
| `NESTED_FLEX_WIDTH` | Nested container width pre-seeding |

Inspect view tree:

```bash
grep "view-block:div" view_tree.txt | head -20
grep "hg:0.0" view_tree.txt          # find zero-height containers
grep "AUTO-HEIGHT" log.txt
grep "Phase 7" log.txt
```

---

## 16. Known Gaps and Improvement Areas

### 16.1 Flex Distribution Precision

**Issue**: Two-pass constraint resolution (CSS §9.7) is implemented, but the iterative loop does not always produce exact browser-matching values. Grow/shrink amounts use `float` rounding that can accumulate 1–2px errors.

**Spec reference**: §9.7 step 4: "Sum the outside sizes of all the flex items to find their combined outer hypothetical main size."

**Suggested fix**: Use `double` accumulation for intermediate free-space sums; apply last-item correction to absorb rounding remainder (as Taffy does).

---

### 16.2 Hypothetical Cross Size (§9.4) Accuracy

**Issue**: `determine_hypothetical_cross_sizes()` uses a simplified intrinsic height measurement. For elements with complex inner layout (nested flex, tables, multi-column text), the returned height can differ significantly from browser values.

**Suggested fix**: Run a full `ComputeSize`-mode layout pass (using `run_mode = LayoutMode::ComputeSize`) similar to how Taffy calls `measure_child_size`. This would use the full block/flex/table layout engine with a `width = resolved_main_axis_size` constraint to get the real height.

---

### 16.3 Recursive Baseline Calculation

**Issue**: `calculate_item_baseline()` handles one level of nesting. Items whose baseline is inside a nested flex container (whose children have not yet been laid out at baseline-query time) fall back to the synthesized bottom-of-margin-box baseline.

**Suggested fix**: Two-pass approach:
1. Layout all content first.
2. Walk up the baseline chain recursively (`calculate_baseline_recursive()`), adding relative `y` offsets from each parent.

This requires restructuring `reposition_baseline_items` to run after all nested content is fully finalized, not after a single level.

---

### 16.4 Auto min-size for Flex Items (CSS §9.7 step 4)

**Issue**: The CSS spec says: *"the automatic minimum size of a flex item is its min-content size in the main axis, unless the item is a scroll container."* Radiant resolves `resolved_min_width` using intrinsic measurement, but this is not always triggered correctly when `min-width: auto` is the unset default (i.e., the property was never written in CSS).

**Suggested fix**: Ensure `resolve_flex_item_constraints()` is called unconditionally for every flex item, and that it treats a missing `given_min_width` as `auto` (not zero). Currently `given_min_width == 0` is treated as zero rather than auto for some code paths.

---

### 16.5 `align-content: stretch` with Re-layout

**Issue**: When `align-content: stretch` distributes extra space to lines, items with `height: auto` should be re-measured at the new line cross size. The current implementation sets item size directly but does not re-run inner layout, so inner content may overflow the new size.

**Suggested fix**: After Phase 8 assigns stretched line cross sizes, for each affected item: re-run `layout_block_content()` (or the equivalent flex layout for nested containers) with the new cross-size constraint.

---

### 16.6 Measurement Cache Invalidation

**Issue**: The measurement cache is populated in the first pass and shared across all nested containers. There is no mechanism to invalidate entries when a reflow occurs (e.g., the viewport resizes). Stale cache entries can cause items to be sized with old intrinsic measurements.

**Suggested fix**: Tie cache lifetime to a layout-generation counter (`uint32_t generation`). Increment the counter on each top-level `layout_html_doc()` call; skip cache entries with a different generation. This mirrors how Yoga handles `generation_count`.

---

### 16.7 `flex-wrap: wrap` + Multi-Line Auto-Height

**Issue**: Auto-height calculation in `layout_flex_container_with_nested_content()` assumes single-line layout when summing item heights. For a multi-line wrapping container, the height should be `Σ(line.cross_size) + row_gap × (line_count - 1)`.

**Suggested fix**: Use `flex_layout->lines[]` (computed by `create_flex_lines()`) as the source of truth for auto-height, not a direct child traversal. This is the same logic that Phase 7 uses; move or share it with the multipass auto-height step.

---

### 16.8 Percentage Heights in Nested Flex

**Issue**: A nested flex item with `height: 50%` should resolve relative to the parent flex container's cross-axis size. However, percentage heights are resolved once at CSS parse time against the initial containing block, and the re-resolution in `collect_and_prepare_flex_items` only handles direct percentage width.

**Suggested fix**: Extend the percentage re-resolution in `collect_and_prepare_flex_items` to also handle `height` percentages when the container's `has_definite_cross_size` is true.

---

### 16.9 `order` Property and DOM Traversal

**Issue**: Items are sorted by `order` in Phase 2, so `flex_layout->flex_items[]` may be reordered. However, `layout_final_flex_content()` re-traverses `container->first_child → next_sibling` (DOM order), applying content layout in DOM order rather than visual flex order. For `order`-reordered layouts this produces wrong baseline/scroll results.

**Suggested fix**: `layout_final_flex_content()` should iterate `flex_layout->flex_items[]` (already sorted order) rather than the DOM child list.

---

### 16.10 `gap` with `flex-wrap` Across Lines

**Issue**: `row_gap` (between flex lines in a row container) is not consistently applied during cross-axis positioning. `align_content()` uses it for multi-line spacing, but `align_items_cross_axis()` accounts for it only partially.

**Suggested fix**: `FlexLineInfo.cross_position` (set by `align_content()`) should be the single source of truth for each line's cross position. `align_items_cross_axis()` should use `line->cross_position` as the base rather than recomputing from `cross_axis_size`.

---

### 16.11 Writing Mode Support

**Issue**: `FlexProp` stores `writing_mode` and `text_direction`, but the axis helpers (`is_main_axis_horizontal`, `set_main_axis_position`, etc.) do not currently respect them. All flex layout assumes horizontal writing mode.

**Suggested fix**: Extend axis helpers to consider `writing_mode`. For `vertical-rl` and `vertical-lr`, the main axis should be vertical for `flex-direction: row`-equivalent layouts. This is a significant future work item but aligns with CSS Writing Modes Level 4.

---

## 17. References

- [CSS Flexible Box Layout Module Level 1](https://www.w3.org/TR/css-flexbox-1/)
- [CSS Box Alignment Module Level 3](https://www.w3.org/TR/css-align-3/)
- [CSS Sizing Level 3](https://www.w3.org/TR/css-sizing-3/) — min-content / max-content sizes
- [CSS Writing Modes Level 4](https://www.w3.org/TR/css-writing-modes-4/)
- [Facebook Yoga](https://github.com/facebook/yoga) — reference two-pass flex distribution, baseline recursion
- [Taffy](https://github.com/DioxusLabs/taffy) — `determine_hypothetical_cross_size`, `determine_container_cross_size` step structure
