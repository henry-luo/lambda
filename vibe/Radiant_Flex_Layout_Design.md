# Radiant Flex Layout Design Document

## Overview

The Radiant layout engine implements CSS Flexbox according to the W3C CSS Flexible Box Layout Module Level 1 specification. This document describes the architecture, data structures, and multi-pass algorithm used to compute flex layouts.

---

## 1. High-Level Architecture

### 1.1 Unified DOM/View Tree

Radiant uses a **unified DOM/View tree** where DOM nodes and View objects share the same inheritance hierarchy. This eliminates the need for separate parallel trees and reduces memory overhead.

```
	  DomNode (base)
		  │
		  ├── DomElement (extends DomNode)
		  │       │
		  │       └── ViewGroup (extends DomElement)
		  │               │
		  │               ├── ViewSpan (inline elements)
		  │               │
		  │               └── ViewBlock (block elements)
		  │
		  └── DomText (text nodes)
```

**Key Benefits:**
- Single tree traversal for both DOM operations and layout
- DOM nodes are their own View representations
- Children/siblings work for both DOM traversal and View iteration
- Reference to parent works uniformly

### 1.2 View Coordinates and Box Model

View bounds are defined by four floats:
```cpp
float x, y, width, height;
// (x, y) relative to the BORDER box of parent block
// (width, height) forms the BORDER box of current block
```

**Key Points:**
- Positions are **relative to parent's border box**, not content box
- Dimensions represent the **border box** (includes padding and border, excludes margin)
- Moving a parent automatically moves all children visually
- Enables efficient hit-testing by walking the parent chain

**Coordinate Conversion:** When absolute (viewport-relative) coordinates are needed (e.g., for rendering or JSON output), the engine traverses up the parent chain, accumulating offsets:

```cpp
float abs_x = view->x, abs_y = view->y;
ViewGroup* parent = view->parent_view();
while (parent) {
    abs_x += parent->x;
    abs_y += parent->y;
    parent = parent->parent_view();
}
```

### 1.3 Key Type Definitions

| Type | Purpose |
|------|---------|
| `DomNode` | Base for all DOM nodes, has `first_child`, `next_sibling`, `parent` |
| `DomElement` | Elements with attributes, styles, extends `DomNode` |
| `ViewGroup` | Visual element with display, positioning; extends `DomElement` |
| `ViewSpan` | Inline-level box; extends `ViewGroup` |
| `ViewBlock` | Block-level box; extends `ViewSpan` |

### 1.4 View Type Enumeration

```cpp
enum ViewType {
    RDT_VIEW_TEXT,          // Text run
    RDT_VIEW_SPAN,          // Inline span
    RDT_VIEW_BLOCK,         // Block container
    RDT_VIEW_INLINE_BLOCK,  // Inline-block
    RDT_VIEW_IMAGE,         // Image element
    RDT_VIEW_SVG            // SVG element
};
```

---

## 2. Key Data Structures

### 2.1 Flex Container Properties (`FlexProp`)

Located in `view.hpp`, stores CSS flex container properties:

```cpp
struct FlexProp {
    int direction;        // CSS_VALUE_ROW | ROW_REVERSE | COLUMN | COLUMN_REVERSE
    int wrap;             // CSS_VALUE_NOWRAP | WRAP | WRAP_REVERSE
    int justify;          // flex-start, flex-end, center, space-between, etc.
    int align_items;      // stretch, flex-start, flex-end, center, baseline
    int align_content;    // For multi-line containers
    int row_gap;          // Gap between rows (pixels)
    int column_gap;       // Gap between columns (pixels)
    WritingMode writing_mode;   // Horizontal-tb, vertical-rl, etc.
    TextDirection text_direction; // LTR or RTL
};
```

### 2.2 Flex Item Properties (`FlexItemProp`)

Stores per-item flex properties:

```cpp
struct FlexItemProp {
    float flex_grow;      // Flex grow factor (default: 0)
    float flex_shrink;    // Flex shrink factor (default: 1)
    float flex_basis;     // Initial main size (auto, content, or length)
    int order;            // Visual order within container
    int align_self;       // Item-specific alignment override
    float aspect_ratio;   // Intrinsic aspect ratio

    // Resolved constraint values
    int resolved_min_width, resolved_max_width;
    int resolved_min_height, resolved_max_height;

    // Intrinsic sizes for sizing algorithm
    IntrinsicSize intrinsic_width;
    IntrinsicSize intrinsic_height;
    bool has_intrinsic_width, has_intrinsic_height;
    bool has_explicit_width, has_explicit_height;

    int baseline_offset;  // For baseline alignment
};
```

### 2.3 Flex Container Layout State (`FlexContainerLayout`)

Runtime state during flex layout computation:

```cpp
struct FlexContainerLayout : FlexProp {
    // Item collection
    View** flex_items;        // Array of flex item pointers
    int item_count;           // Number of flex items
    int allocated_items;      // Allocated array capacity

    // Line management
    FlexLineInfo* lines;      // Array of flex lines
    int line_count;           // Number of lines
    int allocated_lines;      // Allocated capacity

    // Axis dimensions
    float main_axis_size;     // Container main axis size
    float cross_axis_size;    // Container cross axis size

    // State flags
    bool needs_reflow;        // Layout needs recalculation
};
```

### 2.4 Flex Line Information (`FlexLineInfo`)

Represents a single line in a wrapping flex container:

```cpp
struct FlexLineInfo {
    View** items;          // Items in this line
    int item_count;        // Number of items in line

    float main_size;       // Total main axis size used
    float cross_size;      // Cross axis size of line
    float free_space;      // Remaining space after sizing

    float total_flex_grow;  // Sum of flex-grow factors
    float total_flex_shrink; // Sum of flex-shrink factors
};
```

### 2.5 Layout Context (`LayoutContext`)

Passed through the layout algorithm:

```cpp
struct LayoutContext {
    ViewGroup* parent;          // Current parent View
    View* prev_view;            // Previous sibling

    BlockContext block;             // Block formatting context
    Linebox line;               // Inline formatting context
    FontBox font;               // Current font information

    FlexContainerLayout* flex_container;  // Active flex container
    bool is_measuring;          // True during measurement pass
};
```

### 2.6 Measurement Cache Entry

Caches intrinsic sizes during measurement pass:

```cpp
struct MeasurementCacheEntry {
    DomNode* node;              // DOM node reference
    int measured_width;         // Computed width
    int measured_height;        // Computed height
    int content_width;          // Content area width
    int content_height;         // Content area height
};
```

---

## 3. Multi-Pass Layout Algorithm

The flex layout uses a **two-pass algorithm** with nested content layout.

### 3.1 Pass Overview

```
┌─────────────────────────────────────────────────────────┐
│  PASS 1: Measurement + View Initialization              │
│  - Initialize Views for flex items                      │
│  - Measure intrinsic content sizes                      │
│  - Cache measurements for later use                     │
└─────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────┐
│  PASS 2: Flex Algorithm                                 │
│  - Run 9-phase flex layout algorithm                    │
│  - Position and size all flex items                     │
│  - Handle wrapping, alignment, gaps                     │
│                                                         │
│  SUB-PASS 2a: Content Layout (per flex item)            │
│  - Layout nested content within each flex item          │
│  - Handle nested flex containers recursively            │
│  - Apply text layout, images, etc.                      │
│                                                         │
│  SUB-PASS 2b: Baseline Repositioning                    │
│  - Recalculate baselines with actual child dimensions   │
│  - Reposition baseline-aligned items                    │
└─────────────────────────────────────────────────────────┘
```

### 3.2 Entry Point

The flex layout is initiated from `layout_flex_content()` in `layout_flex_multipass.cpp`:

```cpp
void layout_flex_content(LayoutContext* lycon, ViewBlock* block) {
    // 1. Initialize flex container from CSS properties
    init_flex_container(lycon, block);

    // UNIFIED PASS: Collect, measure, and prepare all flex items
    // This replaces the old separate PASS 1 + Phase 1 (eliminated redundant traversal)
    int item_count = collect_and_prepare_flex_items(lycon, lycon->flex_container, block);

    // PASS 2: Run flex algorithm
    layout_flex_container_with_nested_content(lycon, block);

    // Cleanup
    cleanup_flex_container(lycon);
}
```

---

## 4. The 9-Phase Flex Algorithm

The core flex algorithm in `layout_flex_container()` implements the CSS Flexbox specification in 9 phases.

### Phase 1: Collect Flex Items

**Function:** `collect_and_prepare_flex_items()` (unified) or `collect_flex_items()` (legacy)

**Actions:**
- Single traversal of container's children
- Filter out non-element nodes (text nodes)
- Filter out absolutely positioned and hidden items
- Measure content and create Views (unified path)
- Build array of participating flex items
- Apply cached measurements

**Values Computed:**
- `flex_layout->flex_items[]` - Array of flex item Views
- `flex_layout->item_count` - Number of flex items
- Item `width`, `height` from cache
- Item `content_width`, `content_height` from cache

### Phase 2: Sort by Order

**Function:** `sort_flex_items_by_order()`

**Actions:**
- Stable sort flex items by CSS `order` property
- Maintain original order for items with same `order` value

**Values Computed:**
- Reordered `flex_layout->flex_items[]` array

### Phase 2.5: Apply Constraints

**Function:** `apply_constraints_to_flex_items()`

**Actions:**
- For each flex item, call `resolve_flex_item_constraints()`
- Resolve `min-width: auto` to min-content size
- Resolve `min-height: auto` to min-content size
- Calculate intrinsic sizes if not cached
- Store resolved min/max values in `FlexItemProp`

**Values Computed:**
- `item->fi->resolved_min_width`
- `item->fi->resolved_max_width`
- `item->fi->resolved_min_height`
- `item->fi->resolved_max_height`
- `item->fi->intrinsic_width.min_content`
- `item->fi->intrinsic_height.min_content`

### Phase 3: Create Flex Lines

**Function:** `create_flex_lines()`

**Actions:**
- If `flex-wrap: nowrap`, create single line with all items
- If `flex-wrap: wrap/wrap-reverse`:
  - Calculate `flex-basis` for each item
  - Accumulate items until line overflows
  - Start new line when main axis size exceeded
- Calculate total flex-grow and flex-shrink per line

**Values Computed:**
- `flex_layout->lines[]` - Array of FlexLineInfo
- `flex_layout->line_count` - Number of lines
- Per line:
  - `line->items[]` - Items in this line
  - `line->item_count` - Item count
  - `line->total_flex_grow` - Sum of flex-grow
  - `line->total_flex_shrink` - Sum of flex-shrink
  - `line->main_size` - Total main axis size

### Phase 4: Resolve Flexible Lengths

**Function:** `resolve_flexible_lengths()`

**Algorithm:** Iterative constraint resolution per CSS spec

**Actions:**
1. Calculate free space = container size - total basis - gaps
2. If free space > 0 and total_flex_grow > 0: **GROW**
   - Distribute proportionally by `flex-grow`
3. If free space < 0 and total_flex_shrink > 0: **SHRINK**
   - Calculate scaled shrink: `flex_shrink × flex_basis`
   - Distribute proportionally by scaled shrink
4. Check min/max constraints
5. If item hits constraint: **freeze** it
6. Repeat with unfrozen items until converged

**Values Computed:**
- Final `item->width` or `item->height` (main axis)
- `line->free_space` - Remaining space after sizing

### Phase 5: Calculate Cross Sizes

**Function:** `calculate_line_cross_sizes()`

**Actions:**
- For each line, find largest cross-axis item
- Set line cross size to maximum
- Handle `align-items: stretch` (defer to Phase 7)

**Values Computed:**
- `line->cross_size` - Cross axis size of line

### Phase 6: Main Axis Alignment

**Function:** `align_items_main_axis()`

**Actions:**
Based on `justify-content`:
- `flex-start`: Items at start
- `flex-end`: Items at end
- `center`: Items centered
- `space-between`: Equal space between items
- `space-around`: Equal space around items
- `space-evenly`: Equal space including edges

Also handles:
- `margin: auto` on main axis
- Column/row gaps

**Values Computed:**
- `item->x` or `item->y` (main axis position)

### Phase 7: Cross Axis Alignment

**Function:** `align_items_cross_axis()`

**Actions:**
Based on `align-items` / `align-self`:
- `flex-start`: Item at cross start
- `flex-end`: Item at cross end
- `center`: Item centered
- `stretch`: Item stretched to line cross size (respects min/max constraints)
- `baseline`: Items aligned by baseline (initial positioning, refined in Pass 3)

Also handles:
- `margin: auto` on cross axis
- `wrap-reverse` alignment inversion

**Values Computed:**
- `item->y` or `item->x` (cross axis position)
- `item->height` or `item->width` (if stretched)

### Phase 8: Align Content (Multi-line)

**Function:** `align_content()`

**Condition:** Only runs if `line_count > 1`

**Actions:**
Based on `align-content`:
- Distribute lines within container cross axis
- Handle stretch, center, space-between, etc.

**Values Computed:**
- Adjusted `line->cross_size` (for stretch)
- Line start positions in cross axis

### Phase 9: Wrap-Reverse Handling

**Actions:**
- If `flex-wrap: wrap-reverse`:
  - Reverse line order in cross axis
  - Adjust item positions accordingly

**Values Computed:**
- Final adjusted positions for wrap-reverse

---

## 5. Sub-Pass 2: Content Layout

After the flex algorithm positions items, each item's content is laid out.

**Function:** `layout_flex_item_content()`

**Actions:**
1. Set up content area (subtract padding/border)
2. Check if item is a **nested flex container**
   - If `display.inner == CSS_VALUE_FLEX`:
     - Create lightweight Views for children
     - Recursively call `layout_flex_container_with_nested_content()`
3. Otherwise, use standard flow layout:
   - Layout text nodes with line breaking
   - Layout block children vertically
   - Layout inline children horizontally

**Values Computed:**
- All nested Views positioned and sized
- `item->content_width`, `item->content_height`

---

## 5.1 Sub-Pass 2b: Baseline Repositioning

**Function:** `reposition_baseline_items()`

**Purpose:** After nested content is laid out, baselines can be correctly calculated for items with children (e.g., nested flex containers).

**Algorithm:**
1. For each line, recalculate `max_baseline` using `find_max_baseline()`
2. For baseline-aligned items, compute new cross position: `max_baseline - item_baseline`
3. Update item position via `set_cross_axis_position()`

**Baseline Calculation** (`calculate_item_baseline()`):
- If item has text: use `baseline_offset` from text layout
- If item has children: recursively find first child's baseline, add child's relative `y` position
- Fallback: synthesize baseline at bottom of margin box (`margin_top + height`)

**Note:** Since coordinates are relative, repositioning the parent doesn't require updating children.

---

## 6. Axis Helpers

### Main Axis

```cpp
bool is_main_axis_horizontal(FlexProp* flex);
int get_main_axis_size(ViewGroup* item, FlexContainerLayout* flex);
void set_main_axis_size(ViewGroup* item, int size, FlexContainerLayout* flex);
void set_main_axis_position(ViewGroup* item, int pos, FlexContainerLayout* flex);
```

### Cross Axis

```cpp
int get_cross_axis_size(ViewGroup* item, FlexContainerLayout* flex);
void set_cross_axis_size(ViewGroup* item, int size, FlexContainerLayout* flex);
void set_cross_axis_position(ViewGroup* item, int pos, FlexContainerLayout* flex);
```

---

## 7. Measurement System

### 7.1 Content Measurement

**Function:** `measure_flex_child_content()`

**Purpose:** Estimate intrinsic sizes before flex algorithm runs

**Approach:**
1. Check measurement cache first
2. For text nodes: measure with font metrics
3. For elements: traverse children and estimate heights
   - H1-H6: Estimated heights (32, 28, 24, 20, 18, 18 px)
   - P: ~36px (2-3 lines)
   - UL/OL: count × 18px per item
   - DIV: 56px default
4. Store in measurement cache

### 7.2 Intrinsic Size Calculation

**Function:** `calculate_item_intrinsic_sizes()`

**Purpose:** Calculate min-content and max-content sizes

**Algorithm:**
1. Check measurement cache first
2. Get text widths with font metrics
3. `min-content` = longest word width
4. `max-content` = total unwrapped width
5. Store in `FlexItemProp`

### 7.3 Measurement Cache

**Functions:**
- `store_in_measurement_cache()` - Store computed sizes
- `get_from_measurement_cache()` - Retrieve cached sizes
- `clear_measurement_cache()` - Reset cache (start of layout)

**Important:** Cache must NOT be cleared between PASS 1 and PASS 2.

---

## 8. File Organization

| File | Purpose |
|------|---------|
| `layout_flex.cpp` | Core 9-phase flex algorithm |
| `layout_flex.hpp` | Function declarations, FlexLineInfo, FlexDirection/FlexWrap/JustifyContent enums |
| `layout_flex_multipass.cpp` | Multi-pass orchestration, entry point |
| `layout_flex_measurement.cpp` | Content measurement, caching |
| `layout_flex_measurement.hpp` | Measurement function declarations |
| `view.hpp` | FlexProp, FlexItemProp, View classes |
| `layout.hpp` | FlexContainerLayout, LayoutContext |

---

## 9. Known Limitations

1. **Intrinsic Height Estimation**: Currently uses heuristics for element heights; actual font rendering would be more accurate.

2. **Nested Flex Performance**: Deep nesting triggers full recursive layout.

3. **Writing Mode Support**: Vertical writing modes have limited testing.

---

## 11. Debug Logging

Enable debug output by setting log level in `log.conf`:

```
[layout_flex]
level = DEBUG
```

Key log messages:
- `FLEX_HEIGHT` - Height calculation debugging
- `ITERATIVE_FLEX` - Flex length resolution iterations
- `MAIN_AXIS_ALIGN` - Main axis positioning
- `CROSS_ALIGN_ITEM` - Cross axis positioning
- `COLLECT_FLEX_ITEMS` - Item collection tracing

---

## 12. References

- [CSS Flexible Box Layout Module Level 1](https://www.w3.org/TR/css-flexbox-1/)
- [CSS Box Alignment Module Level 3](https://www.w3.org/TR/css-align-3/)
- [CSS Writing Modes Level 4](https://www.w3.org/TR/css-writing-modes-4/)
