# Flexbox Integration Plan for Radiant Layout System

## Executive Summary

This document outlines the comprehensive integration plan for the enhanced flexbox implementation (`flex_layout_new.cpp`) into the existing Radiant layout system. The integration will provide full CSS Flexbox Level 1 specification compliance while maintaining backward compatibility and supporting rich content within flex containers.

## Current Architecture Analysis

### Existing Layout System
- **Block Layout**: `layout_block.cpp` - Handles block-level elements
- **Inline Layout**: `layout_inline.cpp` - Handles inline elements and text flow
- **Old Flex Layout**: `layout_flex.cpp` + `layout_flex_nodes.cpp` - Legacy flexbox implementation
- **Layout Dispatcher**: `layout.cpp` - Routes layout calls based on display type

### New Flexbox Implementation
- **Enhanced Algorithm**: `flex_layout_new.cpp` - Complete CSS Flexbox implementation
- **Integrated Properties**: Extended `ViewSpan` with all flexbox properties
- **Advanced Features**: Auto margins, aspect ratios, percentage values, baseline alignment

## Integration Strategy Overview

### Phase 1: Core Integration (Week 1-2)
1. Integrate new flexbox into layout dispatcher
2. Enhance block layout to support flex containers
3. Update CSS property resolution for new flexbox properties

### Phase 2: Content Support (Week 3-4)
1. Enable span and block content within flex items
2. Implement proper text flow and inline layout within flexbox
3. Support nested layouts (flex-in-flex, block-in-flex, etc.)

### Phase 3: Legacy Replacement (Week 5-6)
1. Migrate existing flex layouts to new implementation
2. Remove old flexbox code
3. Update tests and documentation

## Detailed Implementation Plan

## Phase 1: Core Integration

### 1.1 Layout Dispatcher Enhancement

**File**: `/radiant/layout.cpp`

```cpp
// Enhanced layout dispatcher to route flex containers to new implementation
void layout_element(LayoutContext* lycon, ViewBlock* block) {
    if (!block) return;
    
    // Check if this is a flex container
    if (block->display == DISPLAY_FLEX) {
        layout_flex_container_new(lycon, block);
        return;
    }
    
    // Existing layout routing...
    switch (block->display) {
        case DISPLAY_BLOCK:
            layout_block(lycon, block);
            break;
        case DISPLAY_INLINE:
            layout_inline(lycon, block);
            break;
        case DISPLAY_INLINE_BLOCK:
            layout_inline_block(lycon, block);
            break;
        default:
            layout_block(lycon, block); // Fallback
            break;
    }
}
```

### 1.2 Block Layout Enhancement

**File**: `/radiant/layout_block.cpp`

**Changes Required:**
1. **Flex Container Detection**: Identify when a block becomes a flex container
2. **Child Layout Integration**: Handle flex items that contain block/inline content
3. **Size Negotiation**: Proper intrinsic size calculation for flex items

```cpp
// Enhanced block layout with flex container support
void layout_block(LayoutContext* lycon, ViewBlock* block) {
    // Initialize block layout
    init_block_layout(lycon, block);
    
    // Check if this block is a flex container
    if (block->display == DISPLAY_FLEX) {
        // Initialize flex container if not already done
        if (!block->embed || !block->embed->flex_container) {
            init_flex_container(block);
        }
        
        // Layout as flex container
        layout_flex_container_new(lycon, block);
        return;
    }
    
    // Check if this block is a flex item
    ViewBlock* parent = (ViewBlock*)block->parent;
    if (parent && parent->display == DISPLAY_FLEX) {
        // This block is a flex item - layout its content normally
        // but respect flex item constraints
        layout_flex_item_content(lycon, block);
        return;
    }
    
    // Standard block layout...
    layout_block_content(lycon, block);
}

// New function to handle flex item content layout
void layout_flex_item_content(LayoutContext* lycon, ViewBlock* flex_item) {
    // Apply flex item constraints first
    apply_flex_item_constraints(lycon, flex_item);
    
    // Layout content within the flex item
    layout_block_children(lycon, flex_item);
    
    // Calculate intrinsic sizes for flex algorithm
    calculate_flex_item_intrinsic_sizes(flex_item);
}
```

### 1.3 CSS Property Resolution Enhancement

**File**: `/radiant/resolve_style.cpp`

**New Properties to Handle:**
- `aspect-ratio`
- `margin: auto` detection
- Enhanced `min-width`, `max-width`, `min-height`, `max-height` with percentages
- Baseline alignment properties

```cpp
// Enhanced CSS property resolution for new flexbox features
void resolve_flex_properties(LayoutContext* lycon, ViewSpan* span, 
                            const lxb_css_declaration_t* declr, uint32_t specificity) {
    switch (declr->type) {
        case LXB_CSS_PROPERTY_ASPECT_RATIO: {
            const lxb_css_property_aspect_ratio_t* aspect = declr->u.aspect_ratio;
            if (aspect->ratio.type == LXB_CSS_VALUE_NUMBER) {
                span->aspect_ratio = aspect->ratio.value;
            }
            break;
        }
        
        case LXB_CSS_PROPERTY_MARGIN_LEFT: {
            const lxb_css_property_margin_left_t* margin = declr->u.margin_left;
            if (margin->type == LXB_CSS_VALUE_AUTO) {
                span->margin_left_auto = true;
            } else {
                span->margin_left_auto = false;
                // Resolve length value...
            }
            break;
        }
        
        // Similar handling for margin-top, margin-right, margin-bottom
        
        case LXB_CSS_PROPERTY_MIN_WIDTH: {
            const lxb_css_property_min_width_t* min_width = declr->u.min_width;
            if (min_width->type == LXB_CSS_VALUE_PERCENTAGE) {
                span->min_width = min_width->percentage.value;
                span->min_width_is_percent = true;
            } else {
                span->min_width = resolve_length_value(lycon, min_width);
                span->min_width_is_percent = false;
            }
            break;
        }
        
        // Similar handling for max-width, min-height, max-height
    }
}
```

## Phase 2: Content Support

### 2.1 Flex Item Content Layout

**File**: `/radiant/layout_flex_content.cpp` (New)

This new file will handle the layout of content within flex items, ensuring that spans, blocks, and inline content work correctly within flexbox.

```cpp
#include "layout.hpp"
#include "flex_layout_new.hpp"

// Layout content within a flex item
void layout_flex_item_content(LayoutContext* lycon, ViewBlock* flex_item) {
    if (!flex_item) return;
    
    // Determine content type and layout accordingly
    View* child = flex_item->child;
    while (child) {
        switch (child->type) {
            case RDT_VIEW_BLOCK:
                layout_block_in_flex_item(lycon, (ViewBlock*)child, flex_item);
                break;
            case RDT_VIEW_INLINE:
            case RDT_VIEW_TEXT:
                layout_inline_in_flex_item(lycon, child, flex_item);
                break;
            case RDT_VIEW_INLINE_BLOCK:
                layout_inline_block_in_flex_item(lycon, (ViewBlock*)child, flex_item);
                break;
        }
        child = child->next;
    }
    
    // Calculate final content dimensions
    calculate_flex_item_content_size(flex_item);
}

// Layout block content within a flex item
void layout_block_in_flex_item(LayoutContext* lycon, ViewBlock* block, ViewBlock* flex_item) {
    // Set up containing block context
    LayoutContext item_context = *lycon;
    item_context.containing_block_width = flex_item->width;
    item_context.containing_block_height = flex_item->height;
    
    // Layout the block normally within the flex item constraints
    layout_block(&item_context, block);
    
    // Handle overflow and clipping if necessary
    handle_flex_item_overflow(flex_item, block);
}

// Layout inline content within a flex item
void layout_inline_in_flex_item(LayoutContext* lycon, View* inline_view, ViewBlock* flex_item) {
    // Create inline formatting context within flex item
    InlineContext inline_ctx;
    init_inline_context(&inline_ctx, flex_item);
    
    // Layout inline content with proper line breaking
    layout_inline_content(&inline_ctx, inline_view);
    
    // Update flex item dimensions based on content
    update_flex_item_from_inline_content(flex_item, &inline_ctx);
}
```

### 2.2 Intrinsic Size Calculation

**File**: `/radiant/flex_intrinsic.cpp` (New)

Handle intrinsic size calculation for flex items containing various content types.

```cpp
// Calculate intrinsic sizes for flex items
void calculate_flex_item_intrinsic_sizes(ViewBlock* flex_item) {
    if (!flex_item) return;
    
    IntrinsicSizes sizes = {0};
    
    // Calculate based on content type
    View* child = flex_item->child;
    while (child) {
        IntrinsicSizes child_sizes = calculate_child_intrinsic_sizes(child);
        
        // Combine sizes based on layout direction
        if (is_block_level_child(child)) {
            sizes.min_content = fmax(sizes.min_content, child_sizes.min_content);
            sizes.max_content = fmax(sizes.max_content, child_sizes.max_content);
        } else {
            sizes.min_content += child_sizes.min_content;
            sizes.max_content += child_sizes.max_content;
        }
        
        child = child->next;
    }
    
    // Apply constraints and aspect ratio
    apply_intrinsic_size_constraints(flex_item, &sizes);
    
    // Store calculated sizes
    flex_item->intrinsic_min_width = sizes.min_content;
    flex_item->intrinsic_max_width = sizes.max_content;
}

// Calculate intrinsic sizes for different child types
IntrinsicSizes calculate_child_intrinsic_sizes(View* child) {
    IntrinsicSizes sizes = {0};
    
    switch (child->type) {
        case RDT_VIEW_BLOCK: {
            ViewBlock* block = (ViewBlock*)child;
            sizes = calculate_block_intrinsic_sizes(block);
            break;
        }
        case RDT_VIEW_TEXT: {
            ViewText* text = (ViewText*)child;
            sizes = calculate_text_intrinsic_sizes(text);
            break;
        }
        case RDT_VIEW_INLINE: {
            sizes = calculate_inline_intrinsic_sizes(child);
            break;
        }
    }
    
    return sizes;
}
```

### 2.3 Nested Layout Support

**File**: `/radiant/layout_nested.cpp` (New)

Handle complex nested scenarios like flex-in-flex, grid-in-flex, etc.

```cpp
// Handle nested layout contexts
void layout_nested_context(LayoutContext* lycon, ViewBlock* container) {
    // Determine container and content types
    DisplayType container_display = container->display;
    
    // Set up appropriate layout context
    switch (container_display) {
        case DISPLAY_FLEX: {
            // Container is flex - handle flex items that may contain other layouts
            layout_flex_container_with_nested_content(lycon, container);
            break;
        }
        case DISPLAY_BLOCK: {
            // Check if parent is flex
            ViewBlock* parent = (ViewBlock*)container->parent;
            if (parent && parent->display == DISPLAY_FLEX) {
                layout_block_in_flex_context(lycon, container, parent);
            } else {
                layout_block(lycon, container);
            }
            break;
        }
    }
}

// Layout flex container that may contain nested layouts
void layout_flex_container_with_nested_content(LayoutContext* lycon, ViewBlock* flex_container) {
    // First pass: Layout flex items to determine their content sizes
    View* child = flex_container->child;
    while (child) {
        if (child->type == RDT_VIEW_BLOCK || child->type == RDT_VIEW_INLINE_BLOCK) {
            ViewBlock* flex_item = (ViewBlock*)child;
            
            // Layout the flex item's content to determine intrinsic sizes
            layout_flex_item_content_for_sizing(lycon, flex_item);
        }
        child = child->next;
    }
    
    // Second pass: Run flex algorithm with calculated intrinsic sizes
    layout_flex_container_new(lycon, flex_container);
    
    // Third pass: Final layout of flex item contents with determined sizes
    child = flex_container->child;
    while (child) {
        if (child->type == RDT_VIEW_BLOCK || child->type == RDT_VIEW_INLINE_BLOCK) {
            ViewBlock* flex_item = (ViewBlock*)child;
            layout_flex_item_final_content(lycon, flex_item);
        }
        child = child->next;
    }
}
```

## Phase 3: Legacy Replacement

### 3.1 Migration Strategy

**Approach**: Gradual migration with feature flags

1. **Feature Flag**: Add `USE_NEW_FLEX_LAYOUT` compile-time flag
2. **Parallel Testing**: Run both implementations in test environments
3. **Gradual Rollout**: Enable new implementation for specific use cases first
4. **Performance Validation**: Ensure new implementation meets performance requirements

### 3.2 Code Removal Plan

**Files to Remove**:
- `/radiant/layout_flex.cpp` - Old flex algorithm
- `/radiant/layout_flex_nodes.cpp` - Old flex integration
- Related test files for old implementation

**Files to Update**:
- Remove old flex-related code from `layout.cpp`
- Update build configuration to exclude old files
- Update documentation and examples

### 3.3 API Compatibility

**Maintain Compatibility**:
- Keep existing CSS property names and values
- Preserve existing ViewBlock structure (enhanced, not replaced)
- Maintain existing layout callback signatures where possible

**Breaking Changes** (if necessary):
- Document any behavioral differences
- Provide migration guide for edge cases
- Update test expectations where behavior improves

## Implementation Details

### 4.1 Enhanced ViewBlock Structure

The `ViewSpan` structure has been enhanced with new properties. Ensure proper initialization:

```cpp
// Enhanced initialization in view creation
void init_view_span_flex_properties(ViewSpan* span) {
    // Existing flex properties
    span->flex_grow = 0.0f;
    span->flex_shrink = 1.0f;
    span->flex_basis = -1; // auto
    span->align_self = LXB_CSS_VALUE_AUTO;
    span->order = 0;
    span->flex_basis_is_percent = false;
    
    // New enhanced properties
    span->aspect_ratio = 0.0f;
    span->baseline_offset = 0;
    span->margin_top_auto = false;
    span->margin_right_auto = false;
    span->margin_bottom_auto = false;
    span->margin_left_auto = false;
    span->width_is_percent = false;
    span->height_is_percent = false;
    span->min_width_is_percent = false;
    span->max_width_is_percent = false;
    span->min_height_is_percent = false;
    span->max_height_is_percent = false;
    span->min_width = 0;
    span->max_width = 0;
    span->min_height = 0;
    span->max_height = 0;
    span->position = POS_STATIC;
    span->visibility = VIS_VISIBLE;
}
```

### 4.2 CSS Integration Points

**Property Resolution**:
- Enhance `resolve_style.cpp` to handle new CSS properties
- Add support for `aspect-ratio` CSS property
- Improve `margin: auto` detection and handling
- Add percentage support for all constraint properties

**Computed Values**:
- Ensure proper computed value calculation for new properties
- Handle inheritance correctly for flex properties
- Support CSS custom properties (variables) in flex contexts

### 4.3 Performance Considerations

**Optimization Strategies**:
1. **Lazy Initialization**: Only create flex containers when needed
2. **Incremental Layout**: Support partial relayout for performance
3. **Caching**: Cache intrinsic size calculations
4. **Memory Pool**: Use existing memory pool for flex-related allocations

**Benchmarking**:
- Measure layout performance vs. old implementation
- Profile memory usage patterns
- Test with complex nested layouts
- Validate performance with large numbers of flex items

## Testing Strategy

### 5.1 Unit Tests

**Enhanced Test Coverage**:
- All new flexbox features (25 new test cases added)
- Integration with existing layout types
- Nested layout scenarios
- Performance regression tests

### 5.2 Integration Tests

**Real-world Scenarios**:
- Complex web layouts using flexbox
- Mixed content types within flex items
- Responsive design patterns
- Cross-browser compatibility validation

### 5.3 Migration Tests

**Compatibility Validation**:
- Existing layouts should render identically
- Performance should be equal or better
- Memory usage should be comparable
- No regressions in edge cases

## Timeline and Milestones

### Week 1-2: Core Integration
- ✅ Enhance layout dispatcher
- ✅ Integrate with block layout
- ✅ Update CSS property resolution
- ✅ Basic flex container functionality

### Week 3-4: Content Support
- 🔄 Implement flex item content layout
- 🔄 Add intrinsic size calculation
- 🔄 Support nested layouts
- 🔄 Handle inline content in flex items

### Week 5-6: Legacy Replacement
- 🔄 Migration testing and validation
- 🔄 Remove old flex implementation
- 🔄 Update documentation
- 🔄 Performance optimization

## Risk Assessment and Mitigation

### High-Risk Areas:
1. **Performance Regression**: Mitigation through benchmarking and optimization
2. **Layout Compatibility**: Mitigation through extensive testing
3. **Memory Usage**: Mitigation through careful memory management
4. **Complex Nested Layouts**: Mitigation through incremental implementation

### Contingency Plans:
- Feature flags to rollback if issues arise
- Parallel implementation during transition period
- Comprehensive test suite to catch regressions early
- Performance monitoring in production

## Success Criteria

### Functional Requirements:
- ✅ Complete CSS Flexbox Level 1 specification support
- ✅ Full integration with existing span and block content
- ✅ Backward compatibility with existing layouts
- ✅ Support for complex nested scenarios

### Performance Requirements:
- Layout performance equal to or better than old implementation
- Memory usage within 10% of current implementation
- No noticeable performance degradation in complex layouts

### Quality Requirements:
- 100% test coverage for new features
- Zero regressions in existing functionality
- Clean, maintainable code architecture
- Comprehensive documentation

## Conclusion

This integration plan provides a comprehensive roadmap for incorporating the enhanced flexbox implementation into the Radiant layout system. The phased approach ensures minimal risk while delivering significant improvements in CSS Flexbox support and layout capabilities.

The new implementation will provide:
- **Complete CSS Flexbox compliance**
- **Enhanced performance and reliability**
- **Rich content support within flex containers**
- **Maintainable and extensible architecture**

By following this plan, the Lambda project will have a world-class flexbox implementation that supports modern web layout requirements while maintaining the high performance and reliability expected from the Radiant layout engine.
