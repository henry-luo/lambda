# Flexbox Integration 2.0: Advanced CSS Flex Layout Enhancement Plan

## Executive Summary

This document outlines the comprehensive enhancement plan for the second iteration of Radiant's flex layout implementation. Based on analysis of the existing codebase and test results, we need to focus on multi-pass layout algorithms, proper nested content handling, and full CSS compatibility to achieve browser-level flex layout support.

## Current State Analysis

### Existing Implementation Strengths ✅
- **Core Flex Algorithm**: Basic flex container and item collection working
- **Property Initialization**: Flex properties (grow, shrink, basis) properly initialized
- **Border Offset Support**: Container border offsets correctly applied
- **Basic Positioning**: Items positioned horizontally with correct spacing
- **Box Model Foundation**: Proper content vs border-box dimension handling established

### Critical Issues Identified ❌

#### 1. **Multi-Pass Layout Problems**
- **Single-Pass Limitation**: Current implementation uses single-pass layout
- **Content Size Calculation**: Intrinsic sizes not properly calculated before flex algorithm
- **Nested Content Issues**: Content layout happens after flex sizing, causing mismatches

#### 2. **Flex-Grow/Shrink Distribution Issues**
From test results:
- `flex_005_flex_grow`: Items sized incorrectly (156px vs 150.16px expected)
- **Root Cause**: Flex grow distribution not accounting for gaps and justify-content properly
- **Gap Handling**: Gaps calculated separately from flex distribution

#### 3. **Justify-Content Implementation Gaps**
From test results:
- `flex_006_justify_content`: Items positioned incorrectly (2px vs 50.66px expected)
- **Space-Evenly**: Not properly distributing space evenly
- **Gap Integration**: Gaps interfering with justify-content calculations

#### 4. **Auto Margins Not Working**
From test results:
- `flex_009_auto_margins`: Item at (2,2) instead of centered (150,100)
- **Auto Margin Detection**: Properties parsed but not applied in layout
- **Centering Logic**: Auto margins not consuming available space

#### 5. **Text Positioning Issues**
- **Text Layout**: Text positioned incorrectly within flex items
- **Content Flow**: Text not following proper inline layout within flex items
- **Baseline Alignment**: Text baseline not aligned properly

## Implementation Plan: Multi-Pass Flex Layout

### Integration Architecture Analysis

The current flex layout integrates with the existing block flow system through `layout_block_content()`, which serves as the central dispatcher:

```cpp
void layout_block_content(LayoutContext* lycon, ViewBlock* block, DisplayValue display) {
    if (display.inner == LXB_CSS_VALUE_FLEX) {
        // Current single-pass approach:
        // 1. Process DOM children into View objects via layout_flow_node()
        // 2. Run flex algorithm on already-sized children
        // 3. No content measurement before flex sizing decisions
        
        init_flex_container(lycon, block);
        do {
            layout_flow_node(lycon, child);  // ← Creates final View objects
            child = child->next_sibling();
        } while (child);
        layout_flex_container(lycon, block);  // ← Works with pre-sized children
        cleanup_flex_container(lycon);
    }
}
```

**Key Integration Insights:**
- **Shared Pipeline**: Both normal flow and flex use `layout_flow_node()` for child processing
- **Two-Phase Current**: DOM→View conversion, then flex algorithm
- **Limitation**: Children fully processed before flex sizing decisions made

### Phase 1: Multi-Pass Layout Architecture (Week 1-2)

#### 1.1 Enhanced Integration Flow
```cpp
// Enhanced multi-pass flex layout integration
else if (display.inner == LXB_CSS_VALUE_FLEX) {
    FlexContainerLayout* pa_flex = lycon->flex_container;
    init_flex_container(lycon, block);
    
    // PASS 1: Content measurement (new)
    DomNode* child = block->node->first_child();
    do {
        measure_flex_child_content(lycon, child);  // New function
        child = child->next_sibling();
    } while (child);
    
    // PASS 2: Create View objects with measured sizes
    child = block->node->first_child();
    do {
        layout_flow_node_for_flex(lycon, child);  // Enhanced function
        child = child->next_sibling();
    } while (child);
    
    // PASS 3: Apply flex algorithm with nested content support
    layout_flex_container_with_nested_content(lycon, block);
    
    cleanup_flex_container(lycon);
    lycon->flex_container = pa_flex;
}
```

#### 1.2 Multi-Pass Layout Function
```cpp
// New multi-pass flex layout function
void layout_flex_container_with_nested_content(LayoutContext* lycon, ViewBlock* flex_container) {
    // Pass 1: Content Measurement (already done in integration)
    // - Measured content sizes stored in flex items
    // - Min-content and max-content widths calculated
    // - Intrinsic aspect ratios determined
    
    // Pass 2: Flex Algorithm
    // - Run flex algorithm with measured content sizes
    // - Distribute available space using flex-grow/shrink
    // - Apply justify-content and align-items
    
    // Pass 3: Final Content Layout
    // - Layout flex item contents with final determined sizes
    // - Handle text flow and inline content properly
    // - Apply final positioning and clipping
}
```

#### 1.2 Content Measurement Pass
**File**: `/radiant/layout_flex_measurement.cpp` (New)
```cpp
// Measure flex item content sizes before flex algorithm
void measure_flex_item_content(LayoutContext* lycon, ViewBlock* flex_item) {
    // Create measurement context with unlimited space
    LayoutContext measure_ctx = *lycon;
    measure_ctx.block.width = INT_MAX;  // Allow natural width
    measure_ctx.block.height = INT_MAX; // Allow natural height
    
    // Layout content to measure natural sizes
    layout_flex_item_content(&measure_ctx, flex_item);
    
    // Store measured sizes for flex algorithm
    flex_item->measured_min_width = calculate_min_content_width(flex_item);
    flex_item->measured_max_width = calculate_max_content_width(flex_item);
    flex_item->measured_height = measure_ctx.block.advance_y;
}
```

#### 1.3 Flex Algorithm Enhancement
**File**: `/radiant/layout_flex.cpp` (Enhanced)
```cpp
// Enhanced flex algorithm with proper content size handling
void resolve_flexible_lengths_v2(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    // Step 1: Calculate hypothetical main sizes using measured content
    for (int i = 0; i < line->item_count; i++) {
        ViewBlock* item = line->items[i];
        int hypothetical_size = calculate_hypothetical_main_size(item, flex_layout);
        set_main_axis_size(item, hypothetical_size, flex_layout);
    }
    
    // Step 2: Collect flex items and calculate free space
    int total_hypothetical_size = 0;
    for (int i = 0; i < line->item_count; i++) {
        total_hypothetical_size += get_main_axis_size(line->items[i], flex_layout);
    }
    
    // Include gaps in free space calculation
    int gap_space = calculate_gap_space(flex_layout, line->item_count, true);
    int free_space = flex_layout->main_axis_size - total_hypothetical_size - gap_space;
    
    // Step 3: Distribute free space
    if (free_space != 0) {
        distribute_free_space_v2(flex_layout, line, free_space);
    }
}
```

### Phase 2: Advanced Flex Features (Week 3-4)

#### 2.1 Proper Auto Margins Implementation
**File**: `/radiant/layout_flex_margins.cpp` (New)
```cpp
// Handle auto margins in flex layout
void handle_auto_margins(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    // Detect auto margins on main and cross axes
    for (int i = 0; i < line->item_count; i++) {
        ViewBlock* item = line->items[i];
        
        // Main axis auto margins
        bool main_start_auto = is_main_axis_horizontal(flex_layout) ? 
            item->margin_left_auto : item->margin_top_auto;
        bool main_end_auto = is_main_axis_horizontal(flex_layout) ? 
            item->margin_right_auto : item->margin_bottom_auto;
            
        if (main_start_auto || main_end_auto) {
            apply_main_axis_auto_margins(flex_layout, line, item, i);
        }
        
        // Cross axis auto margins
        bool cross_start_auto = is_main_axis_horizontal(flex_layout) ? 
            item->margin_top_auto : item->margin_left_auto;
        bool cross_end_auto = is_main_axis_horizontal(flex_layout) ? 
            item->margin_bottom_auto : item->margin_right_auto;
            
        if (cross_start_auto || cross_end_auto) {
            apply_cross_axis_auto_margins(flex_layout, item);
        }
    }
}

void apply_main_axis_auto_margins(FlexContainerLayout* flex_layout, FlexLineInfo* line, 
                                 ViewBlock* item, int item_index) {
    // Calculate available space for auto margins
    int container_size = flex_layout->main_axis_size;
    int total_item_sizes = 0;
    int total_gaps = calculate_gap_space(flex_layout, line->item_count, true);
    
    for (int i = 0; i < line->item_count; i++) {
        total_item_sizes += get_main_axis_size(line->items[i], flex_layout);
    }
    
    int available_space = container_size - total_item_sizes - total_gaps;
    
    // Distribute available space among auto margins
    bool start_auto = is_main_axis_horizontal(flex_layout) ? 
        item->margin_left_auto : item->margin_top_auto;
    bool end_auto = is_main_axis_horizontal(flex_layout) ? 
        item->margin_right_auto : item->margin_bottom_auto;
    
    if (start_auto && end_auto) {
        // Center item: distribute space equally
        int margin_size = available_space / 2;
        set_main_axis_auto_margin(item, margin_size, margin_size, flex_layout);
    } else if (start_auto) {
        // Push to end
        set_main_axis_auto_margin(item, available_space, 0, flex_layout);
    } else if (end_auto) {
        // Push to start
        set_main_axis_auto_margin(item, 0, available_space, flex_layout);
    }
}
```

#### 2.2 Enhanced Justify-Content Implementation
**File**: `/radiant/layout_flex_justify.cpp` (New)
```cpp
// Enhanced justify-content with proper gap handling
void align_items_main_axis_v2(FlexContainerLayout* flex_layout, FlexLineInfo* line) {
    // Check for auto margins first - they override justify-content
    if (has_main_axis_auto_margins(line)) {
        handle_auto_margins(flex_layout, line);
        return;
    }
    
    // Calculate positioning based on justify-content
    int container_size = flex_layout->main_axis_size;
    int total_item_size = calculate_total_item_size(line, flex_layout);
    int gap_space = calculate_gap_space(flex_layout, line->item_count, true);
    int available_space = container_size - total_item_size - gap_space;
    
    int current_pos = 0;
    int item_spacing = 0;
    
    switch (flex_layout->justify) {
        case LXB_CSS_VALUE_FLEX_START:
            current_pos = 0;
            break;
            
        case LXB_CSS_VALUE_FLEX_END:
            current_pos = available_space;
            break;
            
        case LXB_CSS_VALUE_CENTER:
            current_pos = available_space / 2;
            break;
            
        case LXB_CSS_VALUE_SPACE_BETWEEN:
            current_pos = 0;
            if (line->item_count > 1) {
                item_spacing = available_space / (line->item_count - 1);
            }
            break;
            
        case LXB_CSS_VALUE_SPACE_AROUND:
            if (line->item_count > 0) {
                item_spacing = available_space / line->item_count;
                current_pos = item_spacing / 2;
            }
            break;
            
        case LXB_CSS_VALUE_SPACE_EVENLY:
            if (line->item_count > 0) {
                item_spacing = available_space / (line->item_count + 1);
                current_pos = item_spacing;
            }
            break;
    }
    
    // Position items with proper spacing
    for (int i = 0; i < line->item_count; i++) {
        ViewBlock* item = line->items[i];
        set_main_axis_position(item, current_pos, flex_layout);
        
        // Advance position
        current_pos += get_main_axis_size(item, flex_layout);
        
        // Add justify-content spacing
        if (i < line->item_count - 1) {
            current_pos += item_spacing;
            
            // Add gap
            int gap = is_main_axis_horizontal(flex_layout) ? 
                flex_layout->column_gap : flex_layout->row_gap;
            current_pos += gap;
        }
    }
}
```

### Phase 3: Nested Content Layout (Week 5-6)

#### 3.1 Enhanced Content Layout System
**File**: `/radiant/layout_flex_content.cpp` (Enhanced)
```cpp
// Enhanced flex item content layout with proper text flow
void layout_flex_item_content_v2(LayoutContext* lycon, ViewBlock* flex_item) {
    // Save parent context
    LayoutContext saved_context = *lycon;
    
    // Set up flex item context
    lycon->block.width = flex_item->width;
    lycon->block.height = flex_item->height;
    lycon->block.advance_y = 0;
    lycon->block.max_width = 0;
    
    // Account for padding in content area
    if (flex_item->bound) {
        lycon->block.width -= (flex_item->bound->padding.left + flex_item->bound->padding.right);
        lycon->block.height -= (flex_item->bound->padding.top + flex_item->bound->padding.bottom);
    }
    
    // Set up line context for text layout
    lycon->line.left = flex_item->bound ? flex_item->bound->padding.left : 0;
    lycon->line.right = lycon->block.width - (flex_item->bound ? flex_item->bound->padding.right : 0);
    lycon->line.vertical_align = LXB_CSS_VALUE_BASELINE;
    line_init(lycon);
    
    // Layout child content with proper flow
    if (flex_item->node && flex_item->node->first_child()) {
        DomNode* child = flex_item->node->first_child();
        do {
            layout_flow_node(lycon, child);
            child = child->next_sibling();
        } while (child);
        
        // Handle final line break
        if (!lycon->line.is_line_start) {
            line_break(lycon);
        }
    }
    
    // Update flex item content dimensions
    flex_item->content_width = lycon->block.max_width;
    flex_item->content_height = lycon->block.advance_y;
    
    // Restore context
    *lycon = saved_context;
}
```

#### 3.2 Text Positioning Fix
**File**: `/radiant/layout_flex_text.cpp` (New)
```cpp
// Fix text positioning within flex items
void position_text_in_flex_item(ViewBlock* flex_item) {
    // Traverse all text nodes within the flex item
    traverse_text_nodes(flex_item, fix_text_position);
}

void fix_text_position(ViewText* text_node, ViewBlock* flex_item) {
    // Calculate proper text position relative to flex item
    int padding_left = flex_item->bound ? flex_item->bound->padding.left : 0;
    int padding_top = flex_item->bound ? flex_item->bound->padding.top : 0;
    
    // Adjust text position to be relative to flex item content area
    text_node->x = flex_item->x + padding_left + text_node->relative_x;
    text_node->y = flex_item->y + padding_top + text_node->relative_y;
}
```

### Phase 4: CSS Property Integration (Week 7-8)

#### 4.1 Enhanced CSS Property Parsing
**File**: `/radiant/resolve_style.cpp` (Enhanced)
```cpp
// Enhanced flex property resolution
void resolve_flex_properties_v2(LayoutContext* lycon, ViewBlock* block, 
                               const lxb_css_declaration_t* declr, uint32_t specificity) {
    switch (declr->type) {
        case LXB_CSS_PROPERTY_FLEX_GROW: {
            const lxb_css_property_flex_grow_t* grow = declr->u.flex_grow;
            if (grow->type == LXB_CSS_VALUE_NUMBER) {
                block->flex_grow = grow->number.value;
            }
            break;
        }
        
        case LXB_CSS_PROPERTY_FLEX_SHRINK: {
            const lxb_css_property_flex_shrink_t* shrink = declr->u.flex_shrink;
            if (shrink->type == LXB_CSS_VALUE_NUMBER) {
                block->flex_shrink = shrink->number.value;
            }
            break;
        }
        
        case LXB_CSS_PROPERTY_FLEX_BASIS: {
            const lxb_css_property_flex_basis_t* basis = declr->u.flex_basis;
            if (basis->type == LXB_CSS_VALUE_AUTO) {
                block->flex_basis = -1; // auto
            } else if (basis->type == LXB_CSS_VALUE_LENGTH) {
                block->flex_basis = resolve_length_value(lycon, &basis->length);
                block->flex_basis_is_percent = false;
            } else if (basis->type == LXB_CSS_VALUE_PERCENTAGE) {
                block->flex_basis = basis->percentage.value;
                block->flex_basis_is_percent = true;
            }
            break;
        }
        
        case LXB_CSS_PROPERTY_MARGIN_LEFT:
        case LXB_CSS_PROPERTY_MARGIN_RIGHT:
        case LXB_CSS_PROPERTY_MARGIN_TOP:
        case LXB_CSS_PROPERTY_MARGIN_BOTTOM: {
            // Enhanced auto margin detection
            resolve_margin_property(block, declr);
            break;
        }
    }
}

void resolve_margin_property(ViewBlock* block, const lxb_css_declaration_t* declr) {
    bool is_auto = false;
    int margin_value = 0;
    
    // Check if margin is auto
    switch (declr->type) {
        case LXB_CSS_PROPERTY_MARGIN_LEFT: {
            const lxb_css_property_margin_left_t* margin = declr->u.margin_left;
            is_auto = (margin->type == LXB_CSS_VALUE_AUTO);
            if (!is_auto) {
                margin_value = resolve_length_value(lycon, &margin->length);
            }
            block->margin_left_auto = is_auto;
            if (!is_auto) block->margin_left = margin_value;
            break;
        }
        // Similar for other margin properties...
    }
}
```

### Phase 5: Testing and Validation (Week 9-10)

#### 5.1 Enhanced Test Suite
**File**: `/test/layout/tools/test_flex_advanced.py` (New)
```python
# Advanced flex layout testing
class FlexLayoutTester:
    def __init__(self):
        self.test_cases = [
            "flex_grow_distribution",
            "justify_content_space_evenly", 
            "auto_margins_centering",
            "nested_flex_containers",
            "text_positioning_in_flex",
            "multi_line_wrapping",
            "baseline_alignment",
            "aspect_ratio_constraints"
        ]
    
    def run_advanced_tests(self):
        results = {}
        for test_case in self.test_cases:
            result = self.run_single_test(test_case)
            results[test_case] = result
            
        return self.analyze_results(results)
    
    def validate_flex_grow(self, container, expected_sizes):
        # Validate that flex-grow distributes space correctly
        items = self.get_flex_items(container)
        actual_sizes = [item.width for item in items]
        
        tolerance = 2.0  # 2px tolerance
        for i, (actual, expected) in enumerate(zip(actual_sizes, expected_sizes)):
            diff = abs(actual - expected)
            if diff > tolerance:
                return False, f"Item {i}: expected {expected}px, got {actual}px (diff: {diff}px)"
        
        return True, "All flex-grow sizes within tolerance"
```

## Implementation Priority Matrix

### High Priority (Critical for Browser Compatibility)
1. **Multi-Pass Layout Architecture** - Foundation for all other fixes
2. **Auto Margins Implementation** - Required for centering and positioning
3. **Justify-Content Enhancement** - Core flex layout feature
4. **Text Positioning Fix** - Critical for visual correctness

### Medium Priority (Important for Full CSS Support)
1. **Flex-Grow/Shrink Distribution** - Advanced sizing features
2. **Nested Content Layout** - Complex layout scenarios
3. **CSS Property Integration** - Complete CSS compatibility
4. **Advanced Test Suite** - Comprehensive validation

## Success Metrics

### Functional Requirements
- **Test Pass Rate**: Achieve 95%+ pass rate on flex layout tests
- **Browser Compatibility**: Match Chrome/Firefox behavior within 2px tolerance
- **Feature Completeness**: Support all CSS Flexbox Level 1 features
- **Nested Layout**: Handle complex nested flex scenarios

### Performance Requirements
- **Layout Speed**: No more than 10% performance degradation
- **Memory Usage**: Stay within 15% of current memory footprint

### Quality Requirements
- **Code Maintainability**: Clean, well-documented code architecture
- **Test Coverage**: 100% test coverage for new features
- **Regression Protection**: No regressions in existing functionality

## Risk Assessment

### High Risk Areas
1. **Multi-Pass Complexity**: Risk of introducing layout loops or performance issues
2. **Memory Management**: Risk of memory leaks with complex nested structures
3. **CSS Integration**: Risk of breaking existing CSS property resolution

### Mitigation Strategies
1. **Incremental Implementation**: Implement features one at a time with thorough testing
2. **Feature Flags**: Use feature flags to enable/disable new functionality
3. **Comprehensive Testing**: Extensive test suite with regression protection
4. **Performance Monitoring**: Continuous performance benchmarking

## Conclusion

This enhanced flexbox implementation plan addresses the critical issues identified in the current implementation while providing a roadmap for full CSS Flexbox Level 1 compliance. The multi-pass layout architecture provides the foundation for proper content measurement and sizing, while the enhanced property handling ensures browser-compatible behavior.

The phased approach allows for incremental implementation and testing, reducing risk while delivering measurable improvements at each stage. The integration analysis shows how to leverage the existing `layout_block_content()` dispatcher and `layout_flow_node()` pipeline while adding the multi-pass capabilities needed for proper flex layout.

Upon completion, Radiant will have a world-class flexbox implementation that matches browser behavior and supports complex nested layouts.
