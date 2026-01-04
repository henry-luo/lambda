# Radiant Layout Engine - Taffy-Inspired Improvements Proposal

## Executive Summary

This proposal documents learnings from analyzing Taffy (a Rust CSS layout library used in Servo, Zed, Bevy, etc.) and recommends improvements for Radiant's flex and grid layout implementations. The focus is on:

1. **Performance** - Caching strategies that dramatically reduce redundant calculations
2. **Architecture** - Better separation of CSS properties from layout state
3. **Code Reuse** - Unified abstractions between flex and grid
4. **Correctness** - Improved CSS spec compliance

---

## Implementation Status Summary

| Phase | Description | Status | Notes |
|-------|-------------|--------|-------|
| Phase 1 | Foundation Types | ✅ **COMPLETE** | `available_space.hpp`, `layout_mode.hpp`, `layout_cache.hpp` created |
| Phase 1b | Grid Track Sizing Algorithm | ✅ **COMPLETE** | CSS Grid §11.4-11.8 fully implemented in `grid_sizing_algorithm.hpp` |
| Phase 1c | Grid Shrink-to-Fit | ✅ **COMPLETE** | Absolutely positioned grids shrink-to-fit content |
| Phase 1d | Grid-Specific Taffy Enhancements | ✅ **COMPLETE** | ItemBatcher, space distribution fix, 0fr handling, alignment/baseline helpers |
| Phase 2 | Unified Alignment | ✅ **COMPLETE** | `layout_alignment.hpp/cpp` - unified alignment for flex/grid |
| Phase 4.2 | Unified Intrinsic Sizing API | ✅ **COMPLETE** | `measure_intrinsic_sizes()` unified entry point |
| Phase 3 | Run Mode Integration | ⏳ Planned | |
| Phase 4 | Layout Cache Integration | ⏳ Planned | |
| Phase 5-6 | FlexGridItem/Context Unification | ⏳ Planned | |

**Current Test Status:** 1665/1665 baseline layout tests passing (100%)

---

## Part 1: Key Learnings from Taffy

### 1.1 Layout Caching System

Taffy implements a sophisticated 9-slot caching system per node:

```rust
// Taffy's cache slot computation
fn compute_cache_slot(known_dimensions: Size<Option<f32>>, available_space: Size<AvailableSpace>) -> usize {
    // Slot 0: Both dimensions known
    // Slots 1-4: One dimension known (2 slots each for width/height × MinContent/MaxContent)
    // Slots 5-8: Neither dimension known (4 combinations of MinContent/MaxContent)
}
```

**Why 9 slots?**
- Nodes are often measured multiple times during layout (by parent, by flex algorithm, by grid algorithm)
- Each measurement may have different constraints
- Caching avoids re-layout when same constraints appear

**Current Radiant State:**
- ✅ **IMPLEMENTED:** 9-slot `LayoutCache` struct in `radiant/layout_cache.hpp`
- ✅ **IMPLEMENTED:** `KnownDimensions` struct for cache key computation
- ✅ **IMPLEMENTED:** `layout_cache_compute_slot()`, `layout_cache_get()`, `layout_cache_store()` functions
- Cache integration with layout flow in progress

### 1.2 Run Modes

Taffy distinguishes between:

```rust
enum RunMode {
    ComputeSize,        // Only need dimensions, skip positioning
    PerformLayout,      // Full layout with final positions
    PerformHiddenLayout // For display:none elements
}

enum SizingMode {
    InherentSize,   // Use node's own size properties
    ContentSize     // Use min-content/max-content
}
```

This allows early bailout:
```rust
if run_mode == RunMode::ComputeSize {
    if let Some(width) = known_dimensions.width {
        if let Some(height) = known_dimensions.height {
            return LayoutOutput::from_outer_size(Size { width, height });
        }
    }
}
```

**Current Radiant State:**
- ✅ **IMPLEMENTED:** `RunMode` enum (`ComputeSize`, `PerformLayout`, `PerformHiddenLayout`) in `radiant/layout_mode.hpp`
- ✅ **IMPLEMENTED:** `SizingMode` enum (`InherentSize`, `ContentSize`) in `radiant/layout_mode.hpp`
- Integration with layout functions in progress

### 1.3 Available Space Type

Taffy uses a typed enum instead of magic numbers:

```rust
pub enum AvailableSpace {
    Definite(f32),   // Concrete pixel value
    MinContent,      // Size to minimum content
    MaxContent,      // Size to maximum content
}

impl AvailableSpace {
    fn is_definite(&self) -> bool;
    fn into_option(&self) -> Option<f32>;
    fn maybe_sub(&self, value: f32) -> AvailableSpace;
    fn is_roughly_equal(&self, other: AvailableSpace) -> bool;  // For cache comparison
}
```

**Current Radiant State:**
- ✅ **IMPLEMENTED:** `AvailableSpace` type with `DEFINITE`, `INDEFINITE`, `MIN_CONTENT`, `MAX_CONTENT` variants
- ✅ **IMPLEMENTED:** Full typed API in `radiant/available_space.hpp`
- Legacy `-1` pattern still supported for compatibility

### 1.4 Separated Layout State

Taffy separates CSS properties from intermediate layout calculations:

```rust
// FlexItem holds ALL intermediate values for ONE layout pass
struct FlexItem {
    node: NodeId,
    // Resolved CSS properties
    size: Size<Option<f32>>,
    min_size: Size<Option<f32>>,
    max_size: Size<Option<f32>>,
    margin: Rect<f32>,
    padding: Rect<f32>,
    border: Rect<f32>,

    // Intermediate calculations
    flex_basis: f32,
    inner_flex_basis: f32,
    resolved_minimum_main_size: f32,
    hypothetical_inner_size: Size<f32>,
    hypothetical_outer_size: Size<f32>,
    target_size: Size<f32>,
    outer_target_size: Size<f32>,
    violation: f32,
    frozen: bool,
    content_flex_fraction: f32,

    // Final positioning
    offset_main: f32,
    offset_cross: f32,
    baseline: f32,
}
```

**Current Radiant State:**
- `FlexItemProp` (CSS properties) is mixed with `FlexContainerLayout` (runtime state)
- Grid uses `GridItemProp` but has similar mixing issues

---

## Part 2: Flex and Grid Unification Opportunities

### 2.1 Current Code Duplication

| Feature | Flex Implementation | Grid Implementation |
|---------|---------------------|---------------------|
| Intrinsic sizing | `layout_flex_measurement.cpp` | `grid_sizing.cpp` + `intrinsic_sizing.cpp` |
| Item collection | `collect_and_prepare_flex_items()` | `collect_grid_items()` |
| Alignment | `align_items_cross_axis()` | `align_grid_items()` |
| Gap handling | `calculate_gap_space()` | Inline in track sizing |
| Baseline calculation | `calculate_baseline_recursive()` | Scattered |

### 2.2 Proposed Unified Types

#### 2.2.1 AvailableSpace Type

```cpp
// radiant/available_space.hpp

namespace radiant {

enum class AvailableSpaceType : uint8_t {
    Definite = 0,
    MinContent = 1,
    MaxContent = 2
};

struct AvailableSpace {
    float value;
    AvailableSpaceType type;
};

// C-style API (no methods in struct for better C compatibility)
inline AvailableSpace available_space_definite(float v) {
    return {v, AvailableSpaceType::Definite};
}
inline AvailableSpace available_space_min_content() {
    return {0, AvailableSpaceType::MinContent};
}
inline AvailableSpace available_space_max_content() {
    return {0, AvailableSpaceType::MaxContent};
}

inline bool available_space_is_definite(AvailableSpace as) {
    return as.type == AvailableSpaceType::Definite;
}
inline bool available_space_is_min_content(AvailableSpace as) {
    return as.type == AvailableSpaceType::MinContent;
}
inline bool available_space_is_max_content(AvailableSpace as) {
    return as.type == AvailableSpaceType::MaxContent;
}

// Get value or fallback
inline float available_space_unwrap_or(AvailableSpace as, float fallback) {
    return available_space_is_definite(as) ? as.value : fallback;
}

// Arithmetic that preserves type
inline AvailableSpace available_space_sub(AvailableSpace as, float v) {
    if (available_space_is_definite(as)) {
        return available_space_definite(as.value - v);
    }
    return as;
}

// Cache comparison with tolerance (uses abs() from view.hpp)
inline bool available_space_is_roughly_equal(AvailableSpace a, AvailableSpace b, float tolerance) {
    if (a.type != b.type) return false;
    if (!available_space_is_definite(a)) return true;  // Both same non-definite type
    return abs(a.value - b.value) < tolerance;  // Uses template abs() from view.hpp
}

struct AvailableSize {
    AvailableSpace width;
    AvailableSpace height;
};

inline AvailableSpace available_size_main(AvailableSize as, bool is_row) {
    return is_row ? as.width : as.height;
}
inline AvailableSpace available_size_cross(AvailableSize as, bool is_row) {
    return is_row ? as.height : as.width;
}

} // namespace radiant
```

#### 2.2.2 Run Mode Enum

```cpp
// radiant/layout_mode.hpp

namespace radiant {

enum class RunMode : uint8_t {
    ComputeSize = 0,      // Only compute dimensions
    PerformLayout = 1,    // Full layout with positioning
    PerformHidden = 2     // Layout for display:none (minimal work)
};

enum class SizingMode : uint8_t {
    InherentSize = 0,     // Use element's own size styles
    ContentSize = 1       // Use intrinsic content size
};

} // namespace radiant
```

#### 2.2.3 Layout Cache

```cpp
// radiant/layout_cache.hpp

namespace radiant {

#define LAYOUT_CACHE_SIZE 9

/**
 * Known dimensions - tracks which dimensions are explicitly set
 */
struct KnownDimensions {
    float width;
    float height;
    bool has_width;
    bool has_height;
};

struct CacheEntry {
    KnownDimensions known_dimensions;  // Input: known sizes
    AvailableSize available_space;     // Input: constraints
    SizeF computed_size;               // Output: computed dimensions (uses radiant's SizeF)
    bool valid;
};

struct LayoutCache {
    CacheEntry final_layout;                      // For PerformLayout mode
    CacheEntry measure_entries[LAYOUT_CACHE_SIZE]; // For ComputeSize mode
    bool is_empty;
};

// C-style API (no constructors/methods in struct)
void layout_cache_init(LayoutCache* cache);
void layout_cache_clear(LayoutCache* cache);

// Compute cache slot from inputs (returns 0-8)
int layout_cache_compute_slot(
    KnownDimensions known_dimensions,
    AvailableSize available_space
);

// Try to get cached result. Returns true if found, writes to out_size
bool layout_cache_get(
    LayoutCache* cache,
    KnownDimensions known_dimensions,
    AvailableSize available_space,
    RunMode mode,
    SizeF* out_size  // Output parameter
);

// Store computed result
void layout_cache_store(
    LayoutCache* cache,
    KnownDimensions known_dimensions,
    AvailableSize available_space,
    RunMode mode,
    SizeF result
);

} // namespace radiant
```

#### 2.2.4 Unified FlexGridItem

```cpp
// radiant/flex_grid_item.hpp

namespace radiant {

/**
 * OptionalFloat - Explicit optional pattern without std::optional
 */
struct OptionalFloat {
    float value;
    bool has_value;
};

/**
 * OptionalSizeF - Optional size (width/height may or may not be specified)
 */
struct OptionalSizeF {
    float width;
    float height;
    bool has_width;
    bool has_height;
};

/**
 * IntrinsicSizes - Cached intrinsic measurements
 */
struct IntrinsicSizes {
    float min_content_width;
    float max_content_width;
    float min_content_height;
    float max_content_height;
    bool valid;  // Whether cache is populated
};

/**
 * FlexGridItem - Unified intermediate layout state for ONE flex/grid item
 *
 * This structure holds ALL intermediate calculations for a single item during
 * flex or grid layout. CSS properties remain in their original property structs
 * (FlexItemProp, GridItemProp); this is purely runtime state.
 *
 * LIFECYCLE:
 * 1. ALLOCATION: Array of FlexGridItem is allocated by FlexGridContext
 * 2. INITIALIZATION: collect_flex_items() or collect_grid_items() populates
 *    from CSS properties (FlexItemProp/GridItemProp)
 * 3. USAGE: Layout algorithm reads/writes intermediate fields
 * 4. CLEANUP: Freed when FlexGridContext is destroyed
 *
 * Memory is managed by FlexGridContext - do NOT manually allocate.
 */
struct FlexGridItem {
    // Node reference
    DomElement* node;
    uint32_t source_order;

    // Resolved CSS properties (computed once from FlexItemProp/GridItemProp)
    OptionalSizeF size;           // width/height if specified
    OptionalSizeF min_size;       // min-width/min-height
    OptionalSizeF max_size;       // max-width/max-height
    RectF margin;                 // Uses radiant's RectF (top/right/bottom/left)
    RectF padding;
    RectF border;
    OptionalFloat aspect_ratio;

    // Flex-specific resolved properties
    float flex_grow;
    float flex_shrink;
    float flex_basis;
    CssEnum align_self;

    // Grid-specific resolved properties
    CssEnum justify_self;
    int32_t row_start;           // Grid line numbers (or -1 for auto)
    int32_t row_end;
    int32_t col_start;
    int32_t col_end;

    // Intrinsic size cache (shared by flex and grid)
    IntrinsicSizes intrinsic_cache;

    // Intermediate calculations (flex algorithm)
    float inner_flex_basis;
    float resolved_minimum_main_size;
    SizeF hypothetical_inner_size;  // Uses radiant's SizeF
    SizeF hypothetical_outer_size;
    SizeF target_size;
    float violation;
    bool frozen;
    float content_flex_fraction;

    // Intermediate calculations (grid algorithm)
    int32_t placed_row;           // Resolved row after placement
    int32_t placed_col;
    int32_t row_span;
    int32_t col_span;

    // Final output (flex and grid)
    float offset_main;
    float offset_cross;
    float baseline;

    // Margin auto detection (for auto margin distribution)
    bool margin_top_is_auto;
    bool margin_right_is_auto;
    bool margin_bottom_is_auto;
    bool margin_left_is_auto;
};

// C-style helper functions (no methods in struct)
float flex_grid_item_padding_border_main(FlexGridItem* item, bool is_row);
float flex_grid_item_padding_border_cross(FlexGridItem* item, bool is_row);
float flex_grid_item_margin_main(FlexGridItem* item, bool is_row);
float flex_grid_item_margin_cross(FlexGridItem* item, bool is_row);

} // namespace radiant
```

#### 2.2.5 Unified FlexGridContext

```cpp
// radiant/flex_grid_context.hpp

#include "lib/arraylist.h"  // For ArrayList

namespace radiant {

/**
 * FlexGridContext - Container-level layout context for flex/grid
 *
 * This structure manages the container-level state during flex/grid layout.
 * It OWNS the FlexGridItem array for all child items.
 *
 * LIFECYCLE:
 * 1. ALLOCATION: Created via init_flex_context() or init_grid_context()
 * 2. ITEM COLLECTION: collect_flex_items() / collect_grid_items() fills items array
 * 3. LAYOUT PASSES: Algorithm uses items array + container state
 * 4. CLEANUP: cleanup_flex_grid_context() frees items array and any owned memory
 *
 * MEMORY OWNERSHIP:
 * - FlexGridContext owns the `items` array (allocated via pool_calloc)
 * - Individual FlexGridItem entries are within that array (not separately allocated)
 * - Grid-specific auxiliary structures (track arrays) are also owned here
 */
struct FlexGridContext {
    // Container reference
    DomElement* container;
    ViewBlock* container_view;

    // Layout mode
    bool is_row_direction;         // true = row/row-reverse, false = column/column-reverse
    bool is_reversed;              // flex-direction: *-reverse or grid row/col reversal
    bool is_wrap;                  // flex-wrap: wrap or wrap-reverse
    bool is_wrap_reverse;          // flex-wrap: wrap-reverse

    // Container sizes
    AvailableSpace available_main;
    AvailableSpace available_cross;
    float definite_main;           // Resolved main axis size (-1 if not definite)
    float definite_cross;          // Resolved cross axis size (-1 if not definite)

    // Gap values
    float main_gap;                // gap in main axis direction
    float cross_gap;               // gap in cross axis direction

    // Alignment
    CssEnum justify_content;
    CssEnum align_items;
    CssEnum align_content;

    // Items array (OWNED by this context)
    FlexGridItem* items;           // Heap-allocated array via pool_calloc
    int32_t item_count;            // Number of items
    int32_t item_capacity;         // Allocated capacity

    // === Flex-specific state ===
    // Flex lines (for wrap)
    struct FlexLine {
        int32_t start_index;       // First item index in this line
        int32_t end_index;         // One past last item index
        float main_size;           // Sum of item main sizes
        float cross_size;          // Max item cross size (or baseline-adjusted)
        float remaining_free_space;
    };
    FlexLine* flex_lines;          // Array of lines (null if single-line)
    int32_t flex_line_count;

    // === Grid-specific state ===
    // Track sizing arrays (uses lib/arraylist.h pattern)
    struct GridTrack {
        float base_size;
        float growth_limit;
        float min_track_sizing_function;  // Resolved min from grid-template
        float max_track_sizing_function;  // Resolved max from grid-template
        bool is_flexible;                 // Has fr unit
        float flex_factor;                // fr value if flexible
    };
    GridTrack* row_tracks;         // Array of row tracks
    GridTrack* col_tracks;         // Array of column tracks
    int32_t row_track_count;
    int32_t col_track_count;

    // Grid occupancy (pointer to separately allocated matrix)
    struct GridOccupancy* occupancy;
};

// === Lifecycle Functions ===

/**
 * Initialize context for flex layout.
 * Allocates items array with initial capacity.
 */
void init_flex_context(
    FlexGridContext* ctx,
    DomElement* container,
    ViewBlock* container_view,
    AvailableSpace available_main,
    AvailableSpace available_cross
);

/**
 * Initialize context for grid layout.
 * Allocates items array and track arrays.
 */
void init_grid_context(
    FlexGridContext* ctx,
    DomElement* container,
    ViewBlock* container_view,
    int32_t explicit_row_count,
    int32_t explicit_col_count
);

/**
 * Collect flex items from container children.
 * Populates ctx->items from FlexItemProp on each child.
 */
void collect_flex_items(FlexGridContext* ctx, LayoutContext* lycon);

/**
 * Collect grid items from container children.
 * Populates ctx->items from GridItemProp on each child.
 */
void collect_grid_items(FlexGridContext* ctx, LayoutContext* lycon);

/**
 * Free all memory owned by context.
 * Call this when layout is complete.
 */
void cleanup_flex_grid_context(FlexGridContext* ctx);

// === Item Array Management ===

/**
 * Ensure items array has capacity for at least `needed` items.
 * Reallocates if necessary.
 */
void flex_grid_context_ensure_capacity(FlexGridContext* ctx, int32_t needed);

/**
 * Add an item to the context. Returns pointer to the new item.
 */
FlexGridItem* flex_grid_context_add_item(FlexGridContext* ctx);

} // namespace radiant
```

#### 2.2.6 Lifecycle and Memory Management

The lifecycle of FlexGridItem and FlexGridContext follows a clear pattern that mirrors Taffy's approach while fitting Radiant's memory model:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         FLEX LAYOUT LIFECYCLE                                │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  1. INITIALIZATION                                                          │
│     ┌──────────────────────────────────────────────────────────┐            │
│     │ FlexGridContext ctx;                                     │            │
│     │ init_flex_context(&ctx, container, view, avail_w, avail_h);│           │
│     │   ├─ Allocates items array via pool_calloc()             │            │
│     │   ├─ Sets container references                           │            │
│     │   └─ Initializes direction/wrap/gap/alignment fields     │            │
│     └──────────────────────────────────────────────────────────┘            │
│                            │                                                │
│                            ▼                                                │
│  2. ITEM COLLECTION                                                         │
│     ┌──────────────────────────────────────────────────────────┐            │
│     │ collect_flex_items(&ctx, lycon);                         │            │
│     │   For each flex-participating child:                     │            │
│     │   ├─ flex_grid_context_add_item(&ctx) → FlexGridItem*   │            │
│     │   ├─ Copy from FlexItemProp → item resolved properties   │            │
│     │   ├─ Resolve margins/padding/border to pixels            │            │
│     │   └─ Initialize intermediate fields to defaults          │            │
│     └──────────────────────────────────────────────────────────┘            │
│                            │                                                │
│                            ▼                                                │
│  3. LAYOUT PASSES (uses ctx.items[0..item_count-1])                         │
│     ┌──────────────────────────────────────────────────────────┐            │
│     │ // Phase 1: Determine flex base sizes                    │            │
│     │ for (int i = 0; i < ctx.item_count; i++) {               │            │
│     │     FlexGridItem* item = &ctx.items[i];                  │            │
│     │     item->inner_flex_basis = compute_flex_basis(...);    │            │
│     │ }                                                        │            │
│     │ // Phase 2-9: Resolve flexible lengths, position items   │            │
│     │ ...                                                      │            │
│     └──────────────────────────────────────────────────────────┘            │
│                            │                                                │
│                            ▼                                                │
│  4. CLEANUP                                                                 │
│     ┌──────────────────────────────────────────────────────────┐            │
│     │ cleanup_flex_grid_context(&ctx);                         │            │
│     │   ├─ Frees items array                                   │            │
│     │   ├─ Frees flex_lines array (if allocated)               │            │
│     │   └─ Zeros all pointers                                  │            │
│     └──────────────────────────────────────────────────────────┘            │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Memory Allocation Strategy:**

| Component | Allocator | Lifetime | Notes |
|-----------|-----------|----------|-------|
| `FlexGridContext` | Stack | Layout function scope | Automatic cleanup |
| `items` array | `pool_calloc()` | Owned by context | Freed in cleanup |
| `flex_lines` array | `pool_calloc()` | Owned by context | Freed in cleanup |
| `row_tracks`/`col_tracks` | `pool_calloc()` | Owned by context | Grid only |
| `occupancy` matrix | `pool_calloc()` | Owned by context | Grid only |
| `IntrinsicSizes` cache | Inline in FlexGridItem | Same as item | No separate alloc |

**Key Design Decisions:**

1. **Stack-allocated context**: The `FlexGridContext` itself lives on the stack inside `layout_flex_content()` / `layout_grid_content()`. This avoids heap allocation for the container struct.

2. **Array, not linked list**: Items are stored in a contiguous array for cache-friendly iteration. Reallocated via geometric growth if needed.

3. **No separate intrinsic cache allocation**: The `IntrinsicSizes` struct is embedded directly in `FlexGridItem`, avoiding pointer chasing.

4. **CSS properties NOT copied**: The resolved CSS values in `FlexGridItem` are computed from `FlexItemProp`/`GridItemProp` during collection. The original property structs are not duplicated—only the computed pixel values are stored.

### 2.3 Unified Alignment Functions

Currently flex and grid have separate alignment implementations. These should be unified:

```cpp
// radiant/layout_alignment.hpp

namespace radiant {

/**
 * Compute offset for aligning an item within available space
 * Used by both flex and grid for:
 *   - justify-content / align-content (container level)
 *   - align-items / align-self (item level)
 *   - justify-items / justify-self (grid only)
 *
 * For simple alignment (no safe/first/last), use compute_alignment_offset_simple().
 */
float compute_alignment_offset(
    CssEnum alignment,           // CSS_VALUE_FLEX_START, CENTER, etc.
    float free_space,            // Available space for distribution
    float item_size,             // Size of item being aligned
    bool is_safe,                // Safe alignment (no overflow)
    bool is_first,               // First item in container
    bool is_last                 // Last item in container
);

// Simplified version without position flags
inline float compute_alignment_offset_simple(
    CssEnum alignment,
    float free_space,
    float item_size
) {
    return compute_alignment_offset(alignment, free_space, item_size, false, false, false);
}

/**
 * Apply safe alignment fallback when free_space is negative
 * space-between/around/evenly fall back to start alignment
 */
CssEnum fallback_alignment(CssEnum alignment, float free_space);

/**
 * Compute baseline for alignment
 * Works for both flex items and grid items
 */
float compute_item_baseline(
    LayoutContext* lycon,
    ViewBlock* item,
    bool is_row_direction
);

/**
 * Distribute space among items for space-between/around/evenly
 */
struct SpaceDistribution {
    float gap_before_first;
    float gap_between;
    float gap_after_last;
};

SpaceDistribution compute_space_distribution(
    CssEnum justify_content,
    float free_space,
    int32_t item_count  // Use int32_t instead of size_t for consistency
);

} // namespace radiant
```

### 2.4 Unified Intrinsic Sizing Interface

The intrinsic sizing code in `intrinsic_sizing.cpp` is already fairly unified but could be improved:

```cpp
// radiant/intrinsic_sizing.hpp

namespace radiant {

// IntrinsicSizes is defined in flex_grid_item.hpp (see 2.2.4)
// Helper functions for IntrinsicSizes access:
inline float intrinsic_sizes_min_content(IntrinsicSizes* sizes, bool is_row) {
    return is_row ? sizes->min_content_width : sizes->min_content_height;
}
inline float intrinsic_sizes_max_content(IntrinsicSizes* sizes, bool is_row) {
    return is_row ? sizes->max_content_width : sizes->max_content_height;
}

/**
 * Measure element's intrinsic sizes
 * This is the SINGLE ENTRY POINT for intrinsic sizing,
 * used by flex, grid, and table layouts.
 */
IntrinsicSizes measure_intrinsic_sizes(
    LayoutContext* lycon,
    ViewBlock* element,
    AvailableSize available_space  // Cross-axis constraint
);

/**
 * Compute min-content contribution for an item in a container
 * (item size + margins, clamped to min/max)
 */
float min_content_contribution(
    LayoutContext* lycon,
    FlexGridItem* item,
    bool is_main_axis,
    AvailableSize available_space
);

/**
 * Compute max-content contribution for an item
 */
float max_content_contribution(
    LayoutContext* lycon,
    FlexGridItem* item,
    bool is_main_axis,
    AvailableSize available_space
);

/**
 * Compute minimum contribution (CSS Flexbox §4.5 / Grid §6.6)
 * This is the automatic minimum size, which may be smaller
 * than min-content for items with overflow != visible
 */
float minimum_contribution(
    LayoutContext* lycon,
    FlexGridItem* item,
    bool is_main_axis,
    AvailableSize available_space
);

} // namespace radiant
```

---

## Part 3: Implementation Plan

### Phase 1: Foundation Types ✅ COMPLETE

**Goal:** Add core types without changing existing behavior

1. ✅ Created `radiant/available_space.hpp` with `AvailableSpace` type
2. ✅ Created `radiant/layout_mode.hpp` with `RunMode` and `SizingMode`
3. ✅ Created `radiant/layout_cache.hpp` with `LayoutCache` struct
4. ⏳ Add `LayoutCache* cache` to `DomElement` (in progress)

**Files created:**
- ✅ `radiant/available_space.hpp` - Type-safe available space representation
- ✅ `radiant/layout_mode.hpp` - RunMode and SizingMode enums
- ✅ `radiant/layout_cache.hpp` - 9-slot caching system

**Risk:** Low - new code, no changes to existing

### Phase 1b: Enhanced Grid Track Sizing ✅ COMPLETE

**Goal:** Implement CSS Grid §11.4-11.8 track sizing algorithm

1. ✅ Created `radiant/grid_sizing_algorithm.hpp` with full track sizing algorithm
2. ✅ Implemented §11.4 Initialize Track Sizes
3. ✅ Implemented §11.5 Resolve Intrinsic Track Sizes (min/max-content)
4. ✅ Implemented §11.6 Maximize Tracks
5. ✅ Implemented §11.7 Expand Flexible Tracks (fr units)
6. ✅ Implemented §11.8 Stretch auto Tracks
7. ✅ Added `GridItemContribution` struct for item sizing
8. ✅ Integrated with `grid_sizing.cpp` via `resolve_track_sizes_enhanced()`

**Key implementation details:**
- Items processed by span count (ascending) per spec
- Fixed-size tracks not grown when distributing intrinsic contributions
- Percentage tracks converted properly based on container definiteness
- Growth limits maintained ≥ base sizes throughout

### Phase 1c: Grid Shrink-to-Fit Width ✅ COMPLETE

**Goal:** Support absolutely positioned grid containers that shrink to content

1. ✅ Added `is_shrink_to_fit_width` field to `GridContainerLayout` in `grid.hpp`
2. ✅ Added shrink-to-fit detection in `layout_grid.cpp` for `position: absolute/fixed`
3. ✅ Modified `resolve_track_sizes_enhanced()` to use indefinite width (-1) for shrink-to-fit
4. ✅ Container width updated after track sizing based on resolved track sizes

**Files modified:**
- `radiant/grid.hpp` - Added `is_shrink_to_fit_width` field
- `radiant/layout_grid.cpp` - Shrink-to-fit detection and container width update
- `radiant/grid_sizing.cpp` - Pass indefinite width for shrink-to-fit containers

### Phase 2: Unified Alignment (Week 2) ✅ **COMPLETE**

**Goal:** Extract and unify alignment code

**Implementation Summary:**
- `layout_alignment.hpp/cpp` already existed with unified functions
- Refactored `layout_flex.cpp` to use unified alignment:
  - Removed duplicate `fallback_alignment()` and `fallback_justify()` functions
  - Updated justify-content/align-content to use `alignment_fallback_for_overflow()`
- Refactored `grid_positioning.cpp` to use unified alignment:
  - Replaced justify-content/align-content switch statements (~50 lines) with `compute_space_distribution()` and `compute_alignment_offset_simple()` calls
  - Replaced justify-self/align-self switch statements (~75 lines) with `resolve_justify_self()`, `resolve_align_self()`, `compute_alignment_offset_simple()` calls

**Unified Alignment Functions (in `layout_alignment.hpp`):**
```cpp
namespace radiant {
// Core alignment offset computation
float compute_alignment_offset(int alignment, float free_space, bool is_safe);
float compute_alignment_offset_simple(int alignment, float free_space);

// Space distribution for space-between/around/evenly
struct SpaceDistribution { float gap_before_first, gap_between, gap_after_last; };
SpaceDistribution compute_space_distribution(int alignment, float free_space, int item_count, float existing_gap);

// Alignment property resolution
int resolve_align_self(int align_self, int align_items);
int resolve_justify_self(int justify_self, int justify_items);

// Alignment classification
bool alignment_is_space_distribution(int alignment);
bool alignment_is_stretch(int alignment);
int alignment_fallback_for_overflow(int alignment, float free_space);
}
```

**Files modified:**
- `radiant/layout_flex.cpp` - ~20 lines removed, now uses unified functions
- `radiant/grid_positioning.cpp` - ~125 lines of switch statements replaced with ~40 lines using unified functions

**Test Status:** 1665/1665 baseline tests passing (100%)

**Risk Assessment:** Medium - Successfully tested, no regressions

### Phase 3: Run Mode Integration (Week 3)

**Goal:** Add early bailout optimization

1. Add `RunMode run_mode` to `LayoutContext`
2. Update `layout_block()` to check run_mode and short-circuit
3. Update `layout_flex_content()` with early bailout
4. Update `layout_grid_content()` with early bailout
5. Add measurement mode support (ComputeSize)

**Expected Performance Gain:** 10-30% reduction in layout time for complex nested layouts

**Files affected:**
- `radiant/layout.hpp` - add RunMode to LayoutContext
- `radiant/layout_block.cpp`
- `radiant/layout_flex_multipass.cpp`
- `radiant/layout_grid_multipass.cpp`

### Phase 4: Layout Cache Integration (Week 4)

**Goal:** Enable caching for repeated measurements

1. Initialize cache in `DomElement` during style resolution
2. Add cache lookup in `layout_block()` entry point
3. Add cache storage after layout completes
4. Invalidate cache on style changes
5. Add cache statistics for debugging

**Expected Performance Gain:** 20-50% for deeply nested flex/grid layouts

**Files affected:**
- `radiant/view.hpp` - add cache field
- `radiant/layout.cpp`
- `radiant/resolve_css_style.cpp` - invalidate on style change

### Phase 5: FlexGridItem/FlexGridContext Unification (Week 5-6)

**Goal:** Unified layout state structures

1. Create `radiant/flex_grid_item.hpp` with `FlexGridItem` struct
2. Create `radiant/flex_grid_context.hpp` with `FlexGridContext` struct
3. Update flex algorithm to use `FlexGridItem` and `FlexGridContext`
4. Update grid algorithm to use `FlexGridItem` and `FlexGridContext`
5. Deprecate scattered fields in `FlexContainerLayout`/`GridContainerLayout`
6. Update intrinsic sizing to use `FlexGridItem`

**Files affected:**
- `radiant/layout_flex.cpp` (significant changes)
- `radiant/layout_grid.cpp` (significant changes)
- `radiant/intrinsic_sizing.cpp`

**Risk:** High - significant refactoring

---

## Part 4: API Changes

### 4.1 LayoutContext Updates

```cpp
// Current
struct LayoutContext {
    DomDocument* doc;
    ViewGroup* view;
    DomElement* elmt;
    BlockContext block;
    Linebox line;
    FontBox font;
    FlexContainerLayout* flex_container;
    GridContainerLayout* grid_container;
    AvailableSpace available;  // CURRENT: uses Size<float> with -1 for auto
    bool is_measuring;
};

// Proposed additions
struct LayoutContext {
    // ... existing fields ...

    RunMode run_mode;          // NEW: ComputeSize vs PerformLayout
    SizingMode sizing_mode;    // NEW: InherentSize vs ContentSize
    AvailableSize available;   // CHANGED: typed AvailableSpace instead of float
};
```

### 4.2 Intrinsic Sizing API ✅ **COMPLETE**

**Implemented unified API in `intrinsic_sizing.hpp/cpp`:**

```cpp
// IntrinsicSizesBidirectional - Complete intrinsic sizes for both axes
struct IntrinsicSizesBidirectional {
    float min_content_width;   // Width of longest unbreakable segment
    float max_content_width;   // Width without any wrapping
    float min_content_height;  // Height at given width constraint
    float max_content_height;  // Height at given width constraint
};

// Unified entry point - measures both width and height in a single call
IntrinsicSizesBidirectional measure_intrinsic_sizes(
    LayoutContext* lycon,
    ViewBlock* element,
    AvailableSpace available_space  // 2D constraints
);

// Helper functions for axis extraction
IntrinsicSizes intrinsic_sizes_width(IntrinsicSizesBidirectional sizes);
IntrinsicSizes intrinsic_sizes_height(IntrinsicSizesBidirectional sizes);
IntrinsicSizes intrinsic_sizes_for_axis(IntrinsicSizesBidirectional sizes, bool is_row_axis);

// Table-specific convenience wrapper
struct CellIntrinsicWidths { float min_width; float max_width; };
CellIntrinsicWidths measure_table_cell_intrinsic_widths(LayoutContext* lycon, ViewBlock* cell);
```

**Integration:**
- Grid: `calculate_grid_item_intrinsic_sizes()` now uses `measure_intrinsic_sizes()` internally
- Flex: Uses `measure_element_intrinsic_widths()` which remains compatible
- Table: Uses `measure_text_intrinsic_widths()`, added `measure_table_cell_intrinsic_widths()` wrapper

**Test Status:** 1665/1665 baseline tests passing (100%)

### 4.3 Cache API

```cpp
// New cache API (C-style functions with DomElement)

/**
 * Get or create the layout cache for an element.
 * Cache is lazy-allocated on first access.
 */
LayoutCache* dom_element_get_layout_cache(DomElement* element);

/**
 * Invalidate the layout cache. Called when styles change.
 */
void dom_element_invalidate_layout_cache(DomElement* element);

// The DomElement struct gains a private cache pointer:
// struct DomElement {
//     // ... existing fields ...
//     LayoutCache* layout_cache_;  // Lazy-allocated
// };
```

---

## Part 5: Grid-Specific Improvements from Taffy

### 5.1 ItemBatcher for Track Sizing ✅ COMPLETE

Taffy processes grid items in span-order using `ItemBatcher`:

```rust
struct ItemBatcher {
    axis: AbstractAxis,
    index_offset: usize,
    current_span: u16,
    current_is_flex: bool,
}
```

This is important because CSS Grid spec requires processing items:
1. First by whether they cross a flexible track (non-flex first)
2. Then by ascending span count

**Implementation in `grid_sizing_algorithm.hpp`:**

1. Added `crosses_flexible_track` field to `GridItemContribution` struct
2. Added `item_crosses_flexible_track()` function to detect if an item spans any truly flexible (non-zero fr) tracks
3. Added `sort_contributions_for_intrinsic_sizing()` to sort items correctly:
   - Primary sort: non-flex items before flex items
   - Secondary sort: ascending span count
4. Items crossing flexible tracks are skipped during intrinsic sizing (handled in §11.7)

```cpp
// Sort: non-flex items first, then by span count (ascending)
std::sort(contributions.begin(), contributions.end(),
    [](const GridItemContribution& a, const GridItemContribution& b) {
        if (a.crosses_flexible_track != b.crosses_flexible_track) {
            return !a.crosses_flexible_track;  // non-flex first
        }
        return a.track_span < b.track_span;
    });
```

### 5.2 Alignment Gutter Adjustment ✅ COMPLETE

When estimating track sizes during intrinsic sizing, Taffy accounts for `justify-content`/`align-content`:

```rust
fn compute_alignment_gutter_adjustment(
    alignment: AlignContent,
    axis_inner_node_size: Option<f32>,
    get_track_size_estimate: ...,
    tracks: &[GridTrack],
) -> f32
```

This improves accuracy when `space-between`, `space-around`, `space-evenly` are used.

**Implementation in `grid_sizing_algorithm.hpp`:**

Added helper functions for alignment gutter adjustment:
- `compute_alignment_start_offset()` - computes offset for `justify-content`/`align-content`
- `apply_alignment_to_tracks()` - distributes alignment space to track positions

### 5.3 Grid Item Baseline Support ✅ COMPLETE

Taffy's `resolve_item_baselines()` handles baseline alignment in grids:
- Groups items by row
- Computes baseline within each row
- Calculates `baseline_shim` for alignment

**Implementation:**

Full baseline support exists in `grid_baseline.hpp` with:
- `RowBaselineGroup` struct for grouping items by row
- `ItemBaselineInfo` struct for per-item baseline data
- `resolve_grid_item_baselines()` function

Additional helpers added to `grid_sizing_algorithm.hpp`:
- `item_participates_in_row_baseline()` - determines if item contributes to row baseline
- `compute_baseline_adjustment_for_track()` - calculates baseline shim for track alignment

### 5.4 Space Distribution Fix ✅ COMPLETE

Fixed `increase_sizes_for_spanning_item()` to distribute space correctly per CSS Grid spec:

**Problem:** Equal distribution was giving space to tracks that had already satisfied their needs.

**Solution:** Implemented "leveling" algorithm that prioritizes tracks with smaller base_size:
1. Find tracks at minimum base_size
2. Grow them toward the next-smallest track's size
3. Repeat until space is exhausted

This ensures tracks that need growth get it first, matching browser behavior.

### 5.5 0fr Track Handling ✅ COMPLETE

Fixed edge case where `0fr` tracks weren't being sized correctly:

**Problem:** `0fr` tracks have fr unit but don't actually flex (flex_factor = 0). They were being skipped during intrinsic sizing.

**Solution:**
- Updated `item_crosses_flexible_track()` to only consider tracks with **non-zero** fr as truly flexible
- Updated `increase_sizes_for_spanning_item()` to include `0fr` tracks in eligible tracks
- Items spanning only `0fr` tracks are now processed normally in §11.5

```cpp
// Only consider tracks with non-zero fr as truly flexible
bool is_truly_flexible = track.is_flexible() &&
                         track.max_track_sizing_function.flex_factor() > 0;
```

---

## Part 6: Testing Strategy

### 6.1 Regression Tests

Before each phase, ensure all existing tests pass:
```bash
make test-radiant-baseline
make layout suite=baseline
```

### 6.2 Performance Benchmarks

Create benchmarks for:
- Deep nested flex containers (10+ levels)
- Large grid with many items (100+ items)
- Mixed flex/grid nested layouts

Track metrics:
- Total layout time
- Number of layout calls per node
- Cache hit rate

### 6.3 New Test Cases

Add tests for:
- Cache invalidation correctness
- RunMode early bailout
- Unified alignment edge cases
- Grid baseline alignment

---

## Part 7: Migration Path

### 7.1 Backward Compatibility

- Keep existing `-1` checks working during transition
- Add `AvailableSpace::from_legacy(float)` for conversion
- Deprecate but don't remove old APIs initially

### 7.2 Gradual Rollout

1. **Phase 1-2:** Add new types alongside old
2. **Phase 3-4:** New code uses new types, old code unchanged
3. **Phase 5-6:** Migrate old code to new types
4. **Final:** Remove deprecated code

---

## Appendix A: File Organization

### Files Created ✅

```
radiant/
├── available_space.hpp          # ✅ AvailableSpace type with DEFINITE/INDEFINITE/MIN_CONTENT/MAX_CONTENT
├── layout_mode.hpp              # ✅ RunMode, SizingMode enums
├── layout_cache.hpp             # ✅ 9-slot LayoutCache struct and API
└── grid_sizing_algorithm.hpp    # ✅ CSS Grid §11.4-11.8 track sizing algorithm
```

### Files Modified ✅

```
radiant/
├── grid.hpp                     # ✅ Added is_shrink_to_fit_width field
├── layout_grid.cpp              # ✅ Shrink-to-fit detection, container width update
├── grid_sizing.cpp              # ✅ resolve_track_sizes_enhanced() integration
└── layout.hpp                   # ✅ Added AvailableSpace to LayoutContext
```

### Files to Create (Remaining)

```
radiant/
├── layout_cache.cpp             # Cache implementation (if needed beyond header)
├── flex_grid_item.hpp           # FlexGridItem - per-item layout state
├── flex_grid_context.hpp        # FlexGridContext - container context
├── flex_grid_context.cpp        # Context lifecycle implementation
├── layout_alignment.hpp         # Unified alignment functions
├── layout_alignment.cpp         # Alignment implementation
└── item_batcher.hpp             # Grid item batching (flex vs non-flex separation)
```

### Files to Modify (Remaining)

```
radiant/
├── layout.cpp                   # Cache integration
├── layout_flex.cpp              # Use unified alignment
├── layout_flex_multipass.cpp    # Run mode support
├── layout_grid_multipass.cpp    # Run mode support
├── intrinsic_sizing.cpp         # Unified API
├── intrinsic_sizing.hpp         # IntrinsicSizes struct
└── view.hpp                     # Cache pointer in DomElement
```

---

## Appendix B: References

- [Taffy GitHub Repository](https://github.com/DioxusLabs/taffy)
- [CSS Flexbox Level 1 Spec](https://www.w3.org/TR/css-flexbox-1/)
- [CSS Grid Level 1 Spec](https://www.w3.org/TR/css-grid-1/)
- [Radiant Layout Design Doc](../doc/Radiant_Layout_Design.md)

---

## Appendix C: Code Examples

### C.1 Cache Slot Computation

```cpp
int layout_cache_compute_slot(
    KnownDimensions known_dimensions,
    AvailableSize available_space
) {
    bool has_width = known_dimensions.has_width;
    bool has_height = known_dimensions.has_height;

    // Slot 0: Both dimensions known
    if (has_width && has_height) return 0;

    // Slots 1-2: Width known, height unknown
    if (has_width) {
        return available_space_is_min_content(available_space.height) ? 2 : 1;
    }

    // Slots 3-4: Height known, width unknown
    if (has_height) {
        return available_space_is_min_content(available_space.width) ? 4 : 3;
    }

    // Slots 5-8: Neither known
    bool width_is_min = available_space_is_min_content(available_space.width);
    bool height_is_min = available_space_is_min_content(available_space.height);

    if (!width_is_min && !height_is_min) return 5;  // Both MaxContent/Definite
    if (!width_is_min && height_is_min) return 6;   // Width MaxContent, Height MinContent
    if (width_is_min && !height_is_min) return 7;   // Width MinContent, Height MaxContent
    return 8;                                        // Both MinContent
}
```

### C.2 Early Bailout Example

```cpp
LayoutOutput layout_flex_content(LayoutContext* lycon, ViewBlock* container) {
    // Early bailout for ComputeSize mode if dimensions are known
    if (lycon->run_mode == RunMode::ComputeSize) {
        KnownDimensions known = layout_get_known_dimensions(lycon, container);
        if (known.has_width && known.has_height) {
            return layout_output_from_size(known.width, known.height);
        }

        // Check cache
        LayoutCache* cache = dom_element_get_layout_cache(container->element);
        SizeF cached_size;
        if (layout_cache_get(cache, known, lycon->available, RunMode::ComputeSize, &cached_size)) {
            return layout_output_from_size(cached_size.width, cached_size.height);
        }
    }

    // ... full flex algorithm ...
}
```

### C.3 Unified Alignment

```cpp
float compute_alignment_offset(
    CssEnum alignment,
    float free_space,
    float item_size,
    bool is_safe,
    bool is_first,
    bool is_last
) {
    // Safe alignment: don't allow overflow
    if (is_safe && free_space < 0) {
        alignment = CSS_VALUE_FLEX_START;
    }

    switch (alignment) {
        case CSS_VALUE_FLEX_START:
        case CSS_VALUE_START:
            return 0;

        case CSS_VALUE_FLEX_END:
        case CSS_VALUE_END:
            return free_space;

        case CSS_VALUE_CENTER:
            return free_space / 2;

        case CSS_VALUE_STRETCH:
            return 0;  // Item will be stretched to fill

        case CSS_VALUE_SPACE_BETWEEN:
            if (is_first) return 0;
            if (is_last) return free_space;
            // Caller handles gap distribution
            return 0;

        case CSS_VALUE_SPACE_AROUND:
        case CSS_VALUE_SPACE_EVENLY:
            // Caller handles gap distribution
            return 0;

        case CSS_VALUE_BASELINE:
            // Caller handles baseline calculation
            return 0;

        default:
            return 0;
    }
}
```
