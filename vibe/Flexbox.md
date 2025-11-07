# Radiant Flexbox Layout Redesign Plan

## Executive Summary

This document outlines a comprehensive plan to redesign the flexbox layout system in the Radiant rendering engine. The current implementation has architectural issues that make it difficult to maintain and extend. The proposed redesign will create a more robust, CSS-compliant, and extensible system that aligns with the existing view hierarchy while preparing for future CSS Grid support.

## Current Architecture Analysis

### Existing View System Structure

The Radiant layout engine uses a hierarchical view system:

```cpp
// Base view hierarchy
View -> ViewGroup -> ViewSpan/ViewBlock
```

**Key Components:**
- `View`: Base type with position, DOM node reference, and parent/sibling relationships
- `ViewGroup`: Container view with child management and display properties
- `ViewSpan`: Inline container with font, boundary, and flex item properties
- `ViewBlock`: Block container with positioning, dimensions, and embedded content

**Layout Flow:**
1. `layout_html_doc()` - Entry point for document layout
2. `layout_flow_node()` - Dispatches to appropriate layout handlers
3. `layout_block()` - Handles block-level elements
4. `layout_inline()` - Handles inline elements
5. `layout_text()` - Handles text content with line breaking

### Current Flex Implementation Issues

**Architectural Problems:**
1. **Separation of Concerns**: Flex logic is isolated in separate `FlexContainer`/`FlexItem` structs, disconnected from the view hierarchy
2. **Data Duplication**: Properties are copied between view objects and flex objects, leading to synchronization issues
3. **Memory Management**: Temporary allocations and complex cleanup in `layout_flex_nodes()`
4. **Limited Integration**: Flex layout doesn't properly integrate with the existing line-breaking and text layout systems
5. **CSS Compliance**: Missing support for many CSS flexbox features (writing modes, logical properties, etc.)

**Current Flow Issues:**
```cpp
// Current problematic flow:
ViewBlock -> FlexContainer -> FlexItem[] -> layout_flex_container() -> copy back to ViewBlock
```

## Proposed Architecture

### 1. Unified View-Based Flex System

**Core Principle**: Integrate flex layout directly into the view hierarchy rather than using separate data structures.

```cpp
// New integrated approach:
ViewBlock (flex container) -> ViewBlock[] (flex items) -> direct layout manipulation
```

### 2. Enhanced View Properties

**Extend existing view structures** rather than creating parallel flex structures:

```cpp
// Enhanced ViewBlock for flex containers
typedef struct ViewBlock : ViewSpan {
    // ... existing fields ...

    // Flex container properties (when display: flex)
    FlexContainerLayout* flex_layout;  // NULL for non-flex blocks
} ViewBlock;

// New flex container layout manager
typedef struct FlexContainerLayout {
    // CSS flexbox properties
    FlexDirection direction;
    FlexWrap wrap;
    JustifyContent justify_content;
    AlignItems align_items;
    AlignContent align_content;

    // Gap properties
    int row_gap;
    int column_gap;

    // Layout state
    FlexLine* lines;
    int line_count;

    // Computed dimensions
    int main_size;
    int cross_size;

    // Writing mode support
    WritingMode writing_mode;
    TextDirection text_direction;
} FlexContainerLayout;

// Enhanced flex item properties
typedef struct FlexItemLayout {
    // CSS flex item properties
    int flex_basis;          // -1 for auto
    float flex_grow;
    float flex_shrink;
    AlignSelf align_self;
    int order;

    // Computed values
    int main_size;
    int cross_size;
    int resolved_flex_basis;

    // Layout state
    bool is_frozen;
    float target_main_size;

    // Aspect ratio and constraints
    float aspect_ratio;
    int baseline_offset;
} FlexItemLayout;
```

### 3. Integrated Layout Algorithm

**Phase-based layout approach** that integrates with existing layout flow:

```cpp
// New layout flow for flex containers
void layout_flex_container(LayoutContext* lycon, ViewBlock* container) {
    // Phase 1: Initialize flex context
    FlexLayoutContext flex_ctx;
    init_flex_context(&flex_ctx, lycon, container);

    // Phase 2: Collect and prepare flex items
    collect_flex_items(&flex_ctx);

    // Phase 3: Determine main and cross sizes
    determine_flex_container_sizes(&flex_ctx);

    // Phase 4: Resolve flex item sizes
    resolve_flex_item_sizes(&flex_ctx);

    // Phase 5: Create flex lines
    create_flex_lines(&flex_ctx);

    // Phase 6: Resolve flexible lengths
    resolve_flexible_lengths(&flex_ctx);

    // Phase 7: Main axis alignment
    align_main_axis(&flex_ctx);

    // Phase 8: Cross axis alignment
    align_cross_axis(&flex_ctx);

    // Phase 9: Finalize layout
    finalize_flex_layout(&flex_ctx);
}
```

## Implementation Plan

### Phase 1: Core Infrastructure (Week 1-2)

**1.1 Enhanced Data Structures**
- Extend `ViewBlock` with integrated flex container properties
- Add `FlexContainerLayout` and `FlexItemLayout` structures
- Update memory allocation functions in `layout.cpp`

**1.2 Flex Context Management**
```cpp
typedef struct FlexLayoutContext {
    LayoutContext* base_context;
    ViewBlock* container;
    ViewBlock** items;
    int item_count;

    // Computed container properties
    int main_size;
    int cross_size;
    bool is_row_direction;
    bool is_reverse_direction;

    // Line management
    FlexLine* lines;
    int line_count;

    // Writing mode support
    WritingMode writing_mode;
    TextDirection text_direction;
} FlexLayoutContext;
```

**1.3 Integration Points**
- Modify `layout_block()` to detect flex containers
- Update `layout_block_content()` to handle flex layout
- Ensure proper integration with existing `LayoutContext`

### Phase 2: CSS-Compliant Flex Algorithm (Week 3-4)

**2.1 Flex Item Collection**
```cpp
void collect_flex_items(FlexLayoutContext* flex_ctx) {
    ViewBlock* container = flex_ctx->container;
    View* child = container->child;

    // Count and validate flex items
    int count = 0;
    while (child) {
        if (is_flex_item(child)) {
            count++;
        }
        child = child->next;
    }

    // Allocate and populate items array
    flex_ctx->items = allocate_flex_items(flex_ctx->base_context, count);
    flex_ctx->item_count = count;

    // Initialize flex item properties
    child = container->child;
    int index = 0;
    while (child && index < count) {
        if (is_flex_item(child)) {
            init_flex_item(flex_ctx, (ViewBlock*)child, index);
            index++;
        }
        child = child->next;
    }

    // Sort by order property
    sort_flex_items_by_order(flex_ctx);
}
```

**2.2 Flexible Length Resolution**
```cpp
void resolve_flexible_lengths(FlexLayoutContext* flex_ctx) {
    for (int line_idx = 0; line_idx < flex_ctx->line_count; line_idx++) {
        FlexLine* line = &flex_ctx->lines[line_idx];

        // Calculate free space
        int free_space = calculate_free_space(flex_ctx, line);

        if (free_space > 0) {
            // Distribute free space using flex-grow
            distribute_free_space(flex_ctx, line, free_space);
        } else if (free_space < 0) {
            // Shrink items using flex-shrink
            shrink_flex_items(flex_ctx, line, -free_space);
        }

        // Apply min/max constraints
        apply_flex_constraints(flex_ctx, line);
    }
}
```

**2.3 Alignment Implementation**
```cpp
void align_main_axis(FlexLayoutContext* flex_ctx) {
    for (int line_idx = 0; line_idx < flex_ctx->line_count; line_idx++) {
        FlexLine* line = &flex_ctx->lines[line_idx];

        // Handle auto margins first
        resolve_auto_margins_main_axis(flex_ctx, line);

        // Apply justify-content
        apply_justify_content(flex_ctx, line);
    }
}

void align_cross_axis(FlexLayoutContext* flex_ctx) {
    // Align items within each line
    for (int line_idx = 0; line_idx < flex_ctx->line_count; line_idx++) {
        FlexLine* line = &flex_ctx->lines[line_idx];
        align_items_in_line(flex_ctx, line);
    }

    // Align lines within container (align-content)
    if (flex_ctx->line_count > 1) {
        align_flex_lines(flex_ctx);
    }
}
```

### Phase 3: Writing Mode and Logical Properties (Week 5)

**3.1 Writing Mode Support**
```cpp
typedef struct LogicalDimensions {
    int inline_size;      // main axis size in current writing mode
    int block_size;       // cross axis size in current writing mode
    int inline_start;     // logical start position
    int block_start;      // logical start position
} LogicalDimensions;

void resolve_logical_dimensions(FlexLayoutContext* flex_ctx,
                               ViewBlock* item,
                               LogicalDimensions* logical) {
    WritingMode wm = flex_ctx->writing_mode;

    switch (wm) {
        case WM_HORIZONTAL_TB:
            logical->inline_size = item->width;
            logical->block_size = item->height;
            logical->inline_start = item->x;
            logical->block_start = item->y;
            break;

        case WM_VERTICAL_RL:
        case WM_VERTICAL_LR:
            logical->inline_size = item->height;
            logical->block_size = item->width;
            logical->inline_start = item->y;
            logical->block_start = item->x;
            break;
    }
}
```

**3.2 Bidirectional Text Support**
```cpp
void apply_text_direction(FlexLayoutContext* flex_ctx) {
    if (flex_ctx->text_direction == TD_RTL) {
        // Reverse item positions for RTL text direction
        for (int line_idx = 0; line_idx < flex_ctx->line_count; line_idx++) {
            FlexLine* line = &flex_ctx->lines[line_idx];
            reverse_line_items(flex_ctx, line);
        }
    }
}
```

### Phase 4: Performance Optimization (Week 6)

**4.1 Memory Pool Integration**
```cpp
// Use existing memory pool for flex layout allocations
FlexLayoutContext* create_flex_context(LayoutContext* lycon, ViewBlock* container) {
    FlexLayoutContext* flex_ctx = (FlexLayoutContext*)alloc_prop(lycon, sizeof(FlexLayoutContext));
    memset(flex_ctx, 0, sizeof(FlexLayoutContext));

    flex_ctx->base_context = lycon;
    flex_ctx->container = container;

    return flex_ctx;
}
```

**4.2 Incremental Layout**
```cpp
typedef struct FlexLayoutCache {
    int container_width;
    int container_height;
    int item_count;
    uint32_t properties_hash;

    // Cached results
    FlexLine* cached_lines;
    int cached_line_count;
} FlexLayoutCache;

bool can_use_cached_layout(FlexLayoutContext* flex_ctx, FlexLayoutCache* cache) {
    return cache->container_width == flex_ctx->main_size &&
           cache->container_height == flex_ctx->cross_size &&
           cache->item_count == flex_ctx->item_count &&
           cache->properties_hash == compute_properties_hash(flex_ctx);
}
```

**4.3 Batch Operations**
```cpp
void batch_update_item_positions(FlexLayoutContext* flex_ctx) {
    // Update all item positions in a single pass
    for (int line_idx = 0; line_idx < flex_ctx->line_count; line_idx++) {
        FlexLine* line = &flex_ctx->lines[line_idx];

        for (int item_idx = 0; item_idx < line->item_count; item_idx++) {
            ViewBlock* item = line->items[item_idx];

            // Apply computed position directly to view
            apply_flex_position(flex_ctx, item, line_idx, item_idx);
        }
    }
}
```

## CSS Grid Extensibility

### Shared Infrastructure

**Common layout primitives** that can be reused for CSS Grid:

```cpp
// Shared layout context base
typedef struct LayoutContextBase {
    LayoutContext* base_context;
    ViewBlock* container;
    ViewBlock** items;
    int item_count;

    WritingMode writing_mode;
    TextDirection text_direction;
} LayoutContextBase;

// Flex-specific extension
typedef struct FlexLayoutContext : LayoutContextBase {
    FlexLine* lines;
    int line_count;
    // ... flex-specific fields
} FlexLayoutContext;

// Future Grid-specific extension
typedef struct GridLayoutContext : LayoutContextBase {
    GridTrack* rows;
    GridTrack* columns;
    int row_count;
    int column_count;
    // ... grid-specific fields
} GridLayoutContext;
```

### Shared Algorithms

**Alignment and sizing algorithms** that work for both flex and grid:

```cpp
// Generic alignment functions
void align_items_generic(LayoutContextBase* ctx, AlignType align_type);
void justify_items_generic(LayoutContextBase* ctx, JustifyType justify_type);

// Generic sizing functions
void resolve_intrinsic_sizes(LayoutContextBase* ctx);
void apply_size_constraints(LayoutContextBase* ctx);
```

### Future Grid Integration Points

```cpp
// Grid container detection in layout_block()
if (display.inner == CSS_VALUE_GRID) {
    layout_grid_container(lycon, block, display);
}

// Grid-specific layout function
void layout_grid_container(LayoutContext* lycon, ViewBlock* container, DisplayValue display) {
    GridLayoutContext grid_ctx;
    init_grid_context(&grid_ctx, lycon, container);

    // Grid-specific algorithm phases
    parse_grid_template(&grid_ctx);
    place_grid_items(&grid_ctx);
    resolve_grid_sizes(&grid_ctx);
    align_grid_content(&grid_ctx);

    finalize_grid_layout(&grid_ctx);
}
```

## Testing Strategy

### Unit Tests

**Component-level testing** for each flex algorithm phase:

```cpp
// Test flex item collection
void test_collect_flex_items() {
    // Test with various DOM structures
    // Test order property sorting
    // Test visibility and position filtering
}

// Test flexible length resolution
void test_resolve_flexible_lengths() {
    // Test flex-grow distribution
    // Test flex-shrink behavior
    // Test min/max constraint handling
}

// Test alignment algorithms
void test_flex_alignment() {
    // Test justify-content values
    // Test align-items values
    // Test align-content values
    // Test auto margin behavior
}
```

### Integration Tests

**Full layout testing** with real HTML/CSS:

```html
<!-- Test cases for various flex scenarios -->
<div class="flex-container" style="display: flex; width: 300px;">
    <div class="flex-item" style="flex: 1;">Item 1</div>
    <div class="flex-item" style="flex: 2;">Item 2</div>
    <div class="flex-item" style="flex: 1;">Item 3</div>
</div>
```

### Performance Tests

**Benchmark critical paths**:
- Large flex containers (100+ items)
- Nested flex containers
- Dynamic content changes
- Memory allocation patterns

## Migration Strategy

### Phase 1: Parallel Implementation
- Implement new flex system alongside existing one
- Add feature flag to switch between implementations
- Ensure identical output for existing test cases

### Phase 2: Gradual Migration
- Migrate simple flex layouts first
- Add comprehensive test coverage
- Performance comparison and optimization

### Phase 3: Complete Replacement
- Remove old flex implementation
- Clean up temporary compatibility code
- Update documentation and examples

## Benefits of New Design

### 1. Better Integration
- **Unified Memory Management**: Uses existing memory pool system
- **Consistent API**: Follows existing layout patterns
- **Proper Hierarchy**: Maintains view parent/child relationships

### 2. CSS Compliance
- **Complete Flexbox Support**: All CSS flexbox properties
- **Writing Mode Support**: Proper handling of different writing modes
- **Logical Properties**: Support for logical sizing and positioning

### 3. Performance Improvements
- **Reduced Copying**: Direct manipulation of view objects
- **Memory Efficiency**: No temporary data structure allocations
- **Cache Friendly**: Better data locality and access patterns

### 4. Maintainability
- **Clear Separation**: Each phase has well-defined responsibilities
- **Extensible Design**: Easy to add new CSS features
- **Testable Components**: Each algorithm phase can be tested independently

### 5. Future-Proof Architecture
- **Grid Ready**: Shared infrastructure for CSS Grid implementation
- **Extensible**: Easy to add new layout modes
- **Standards Compliant**: Follows CSS specification closely

## Conclusion

This redesign addresses the fundamental architectural issues in the current flex implementation while providing a solid foundation for future CSS Grid support. The new system integrates seamlessly with the existing Radiant layout engine, provides complete CSS compliance, and offers significant performance improvements through better memory management and reduced data copying.

The phased implementation approach ensures a smooth migration path while maintaining backward compatibility. The shared infrastructure design prepares the codebase for future CSS Grid implementation with minimal additional architectural changes.
