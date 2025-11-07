# CSS Positioning Implementation Plan for Radiant

## Overview

This document outlines the implementation plan for CSS float, absolute, and relative positioning support in Radiant's layout engine. The implementation extends existing layout infrastructure while maintaining compatibility with current flexbox, grid, and block layout systems.

## Current Architecture Analysis

### Existing Layout Flow
Based on analysis of `layout_text.cpp` and `layout_block.cpp`, Radiant uses:

1. **Block Layout Context (`LayoutContext`)**:
   - `Blockbox` manages block-level positioning and dimensions
   - `Linebox` handles inline flow and text positioning
   - `advance_y` tracks vertical progression through document flow

2. **View Tree Structure**:
   - Hierarchical view objects (`ViewBlock`, `ViewText`, `ViewSpan`)
   - Parent-child relationships maintain layout context
   - Memory pool allocation for efficient view management

3. **CSS Property Resolution**:
   - `resolve_style.cpp` handles CSS property parsing
   - Properties stored in view-specific structures (`BoundaryProp`, `BlockProp`)
   - Existing support for margins, padding, borders, display types

4. **Layout Dispatch System**:
   - `layout_flow_node()` routes elements to appropriate layout handlers
   - Support for block, inline, flex, grid, and table layouts
   - Extensible architecture for new layout modes

## Implementation Plan

### Phase 1: Core Positioning Infrastructure (2 weeks)

#### 1.1 Positioning Property Support
**File**: `radiant/resolve_style.cpp`
```cpp
// Add positioning property resolution
typedef struct PositionProp {
    PropValue position;        // static, relative, absolute, fixed, sticky
    int top, right, bottom, left;  // offset values
    int z_index;              // stacking order
    bool has_top, has_right, has_bottom, has_left;  // which offsets are set
} PositionProp;

// Add to ViewBlock structure in view.hpp
struct ViewBlock {
    // ... existing fields
    PositionProp* position;   // positioning properties
};
```

#### 1.2 Stacking Context Management
**File**: `radiant/stacking_context.hpp` (new)
```cpp
typedef struct StackingContext {
    ViewBlock* establishing_element;  // element that creates the context
    int z_index;                     // z-index of this context
    ArrayList* positioned_children;   // positioned descendants
    struct StackingContext* parent;   // parent stacking context
} StackingContext;

// Stacking context creation and management functions
StackingContext* create_stacking_context(ViewBlock* element);
void add_positioned_element(StackingContext* context, ViewBlock* element);
void sort_by_z_index(StackingContext* context);
```

#### 1.3 Containing Block Calculation
**File**: `radiant/containing_block.cpp` (new)
```cpp
// Calculate containing block for positioned elements
ViewBlock* find_containing_block(ViewBlock* element, PropValue position_type);
Rect get_containing_block_rect(ViewBlock* containing_block, ViewBlock* positioned_element);
```

### Phase 2: Relative Positioning (1 week)

#### 2.1 Relative Position Layout
**File**: `radiant/layout_positioned.cpp` (new)
```cpp
void layout_relative_positioned(LayoutContext* lycon, ViewBlock* block) {
    // 1. Layout element in normal flow first
    // 2. Apply offset without affecting other elements
    // 3. Update visual position only

    if (!block->position || block->position->position != CSS_VALUE_RELATIVE) {
        return;
    }

    // Calculate offset from top/right/bottom/left properties
    int offset_x = 0, offset_y = 0;
    calculate_relative_offset(block, &offset_x, &offset_y);

    // Apply offset to visual position
    block->x += offset_x;
    block->y += offset_y;

    log_debug("Applied relative positioning: offset (%d, %d)", offset_x, offset_y);
}
```

#### 2.2 Integration with Block Layout
**File**: `radiant/layout_block.cpp` (enhance existing)
```cpp
void layout_block(LayoutContext* lycon, DomNode *elmt, DisplayValue display) {
    // ... existing layout logic

    // After normal layout, apply relative positioning
    if (block->position && block->position->position == CSS_VALUE_RELATIVE) {
        layout_relative_positioned(lycon, block);
    }
}
```

### Phase 3: Absolute Positioning (2 weeks)

#### 3.1 Absolute Position Layout Algorithm
**File**: `radiant/layout_positioned.cpp` (extend)
```cpp
void layout_absolute_positioned(LayoutContext* lycon, ViewBlock* block) {
    // 1. Remove from normal flow
    // 2. Find containing block
    // 3. Calculate position based on offset properties
    // 4. Size element based on content or constraints

    ViewBlock* containing_block = find_containing_block(block, CSS_VALUE_ABSOLUTE);
    Rect cb_rect = get_containing_block_rect(containing_block, block);

    // Calculate position
    calculate_absolute_position(lycon, block, &cb_rect);

    // Layout content
    layout_absolute_content(lycon, block);

    log_debug("Positioned absolutely at (%d, %d)", block->x, block->y);
}
```

#### 3.2 Out-of-Flow Management
**File**: `radiant/out_of_flow.hpp` (new)
```cpp
typedef struct OutOfFlowManager {
    ArrayList* absolute_elements;    // absolutely positioned elements
    ArrayList* fixed_elements;       // fixed positioned elements
    StackingContext* root_context;   // root stacking context
} OutOfFlowManager;

// Manage elements removed from normal flow
void add_out_of_flow_element(OutOfFlowManager* manager, ViewBlock* element);
void layout_out_of_flow_elements(LayoutContext* lycon, OutOfFlowManager* manager);
```

### Phase 4: Float Implementation (3 weeks)

#### 4.1 Float Context Management
**File**: `radiant/float_context.hpp` (new)
```cpp
typedef struct FloatBox {
    ViewBlock* element;      // floating element
    Rect bounds;            // float box bounds
    PropValue float_side;   // left or right
    int clear_level;        // for clear property
} FloatBox;

typedef struct FloatContext {
    ArrayList* left_floats;   // left-floating elements
    ArrayList* right_floats;  // right-floating elements
    int current_y;           // current line position
    ViewBlock* container;    // containing block
} FloatContext;
```

#### 4.2 Float Layout Algorithm
**File**: `radiant/layout_float.cpp` (new)
```cpp
void layout_float_element(LayoutContext* lycon, ViewBlock* block, FloatContext* float_ctx) {
    // 1. Size the float based on content
    // 2. Find available position considering existing floats
    // 3. Place float and update float context
    // 4. Adjust line boxes around floats

    size_float_element(lycon, block);

    Rect float_position = find_float_position(float_ctx, block);
    block->x = float_position.x;
    block->y = float_position.y;

    add_float_to_context(float_ctx, block);

    log_debug("Floated element to (%d, %d)", block->x, block->y);
}
```

#### 4.3 Line Box Adjustment
**File**: `radiant/layout_text.cpp` (enhance existing)
```cpp
void adjust_line_for_floats(LayoutContext* lycon, FloatContext* float_ctx) {
    // Adjust line.left and line.right based on intersecting floats
    int line_top = lycon->block.advance_y;
    int line_bottom = line_top + lycon->block.line_height;

    // Check left floats
    for (int i = 0; i < float_ctx->left_floats->count; i++) {
        FloatBox* float_box = (FloatBox*)arraylist_get(float_ctx->left_floats, i);
        if (intersects_vertically(float_box->bounds, line_top, line_bottom)) {
            lycon->line.left = max(lycon->line.left, float_box->bounds.x + float_box->bounds.width);
        }
    }

    // Check right floats
    for (int i = 0; i < float_ctx->right_floats->count; i++) {
        FloatBox* float_box = (FloatBox*)arraylist_get(float_ctx->right_floats, i);
        if (intersects_vertically(float_box->bounds, line_top, line_bottom)) {
            lycon->line.right = min(lycon->line.right, float_box->bounds.x);
        }
    }
}
```

### Phase 5: Clear Property Support (1 week)

#### 5.1 Clear Implementation
**File**: `radiant/layout_clear.cpp` (new)
```cpp
void apply_clear_property(LayoutContext* lycon, ViewBlock* block, FloatContext* float_ctx) {
    if (!block->blk || block->blk->clear == CSS_VALUE_NONE) {
        return;
    }

    int clear_y = calculate_clear_position(float_ctx, block->blk->clear);
    if (clear_y > lycon->block.advance_y) {
        lycon->block.advance_y = clear_y;
        block->y = clear_y;
        log_debug("Applied clear, moved to y=%d", clear_y);
    }
}
```

### Phase 6: Integration and Optimization (2 weeks)

#### 6.1 Layout Dispatcher Enhancement
**File**: `radiant/layout.cpp` (enhance existing)
```cpp
void layout_flow_node(LayoutContext* lycon, DomNode *node) {
    // ... existing logic

    // Check for positioning properties
    if (element_has_positioning(node)) {
        handle_positioned_element(lycon, node);
        return;
    }

    // Check for float property
    if (element_has_float(node)) {
        handle_float_element(lycon, node);
        return;
    }

    // Continue with normal flow layout
}
```

#### 6.2 Performance Optimizations
- Spatial indexing for float collision detection
- Incremental layout for positioned elements
- Memory pool optimization for positioning structures

## Data Structure Extensions

### Enhanced ViewBlock Structure
```cpp
// Add to view.hpp
typedef struct ViewBlock {
    // ... existing fields
    PositionProp* position;          // positioning properties
    FloatContext* float_context;     // for block formatting contexts
    OutOfFlowManager* out_of_flow;   // out-of-flow descendants
    StackingContext* stacking_ctx;   // stacking context if established
    bool creates_bfc;                // creates block formatting context
} ViewBlock;
```

### CSS Property Extensions
```cpp
// Add to resolve_style.cpp
typedef struct BlockProp {
    // ... existing fields
    PropValue clear;                 // clear property
    PropValue float_prop;           // float property (renamed to avoid keyword)
    PropValue overflow_x, overflow_y; // overflow properties
} BlockProp;
```

## Testing Strategy

### Unit Tests
- Position calculation algorithms
- Float placement logic
- Stacking context creation
- Clear property behavior

### Integration Tests
- Complex layouts with mixed positioning
- Float interaction with text flow
- Nested positioning contexts
- Z-index ordering

### Browser Compatibility Tests
- Cross-reference with Chrome/Firefox behavior
- Edge cases and spec compliance
- Performance benchmarks

## Implementation Guidelines

### Design Principles Adherence

1. **Extend, Don't Replace**: All positioning features extend existing layout functions without breaking current functionality.

2. **Unicode Support**: Text flow around floats maintains existing Unicode and text wrapping capabilities.

3. **Non-Breaking Changes**: New positioning properties are optional additions to existing view structures.

4. **CSS Compatibility**: Full CSS 2.1 positioning specification compliance with CSS3 enhancements.

5. **High-DPI Compatibility**: All positioning calculations respect `pixel_ratio` for high-resolution displays.

6. **Structured Logging**: Use `log_debug()` throughout positioning code for debugging and performance analysis.

### Memory Management
- Use existing memory pool allocation patterns
- Efficient cleanup of positioning structures
- Minimize memory overhead for non-positioned elements

### Error Handling
- Graceful fallback for unsupported positioning combinations
- Validation of positioning property values
- Recovery from layout calculation errors

## Future Enhancements

### CSS3+ Features (Future Phases)
- `position: sticky` support
- CSS Grid integration with positioned elements
- CSS Transforms and positioning interaction
- Container queries and positioning

### Performance Optimizations
- GPU-accelerated positioning calculations
- Parallel layout for independent positioned elements
- Advanced spatial indexing for complex float layouts

## Conclusion

This implementation plan provides a comprehensive approach to adding CSS positioning support to Radiant while maintaining the existing architecture's strengths. The phased approach allows for incremental development and testing, ensuring stability and performance throughout the implementation process.

The design leverages Radiant's existing layout infrastructure, extends current data structures appropriately, and maintains compatibility with flexbox, grid, and table layout systems. The result will be a complete CSS positioning implementation that meets modern web layout requirements.
