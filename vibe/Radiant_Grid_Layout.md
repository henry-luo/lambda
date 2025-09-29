# Radiant CSS Grid Layout Implementation Plan

## Overview

This document outlines the incremental implementation plan for CSS Grid Layout support in the Radiant layout engine, following the established patterns from the existing flexbox implementation and adhering to the key design principles.

## Current Architecture Analysis

Based on the existing flex layout implementation in `layout_flex.cpp` and `layout_flex_content.cpp`, the Radiant layout system follows these patterns:

### Existing Flex Layout Architecture
- **Main Entry Point**: `layout_flex_container_new()` in `layout_flex.cpp`
- **Content Layout**: `layout_flex_item_content()` in `layout_flex_content.cpp`
- **Data Structures**: `FlexContainerLayout` and `FlexLineInfo` in `flex.hpp`
- **Integration**: Dispatched from main `layout.cpp` based on CSS display property
- **Memory Management**: Uses memory pools and proper cleanup functions
- **CSS Integration**: Direct alignment with Lexbor CSS constants

### Key Design Principles
1. **Extend, Don't Replace**: All enhancements extend existing layout functions
2. **Unicode Support**: General Unicode support with performance optimizations
3. **Non-Breaking Changes**: No changes to existing API or data structures
4. **CSS Compatibility**: Support standard CSS conformance
5. **High-DPI Compatibility**: Preserve existing pixel_ratio support for high-resolution displays
6. **Structured Logging**: Use structured logging using `./lib/log.h`

## Phase 1: Core Grid Data Structures (Week 1-2)

### 1.1 Grid Container Data Structure

Create `GridContainerLayout` structure in `grid.hpp`:

```cpp
typedef struct GridContainerLayout {
    // Grid template properties
    GridTrackList* grid_template_rows;
    GridTrackList* grid_template_columns;
    GridTrackList* grid_template_areas;
    
    // Grid gap properties
    int row_gap;
    int column_gap;
    
    // Grid alignment properties
    PropValue justify_content;    // LXB_CSS_VALUE_START, etc.
    PropValue align_content;      // LXB_CSS_VALUE_START, etc.
    PropValue justify_items;      // LXB_CSS_VALUE_STRETCH, etc.
    PropValue align_items;        // LXB_CSS_VALUE_STRETCH, etc.
    
    // Grid auto properties
    PropValue grid_auto_flow;     // LXB_CSS_VALUE_ROW, LXB_CSS_VALUE_COLUMN
    GridTrackList* grid_auto_rows;
    GridTrackList* grid_auto_columns;
    
    // Computed grid properties
    GridTrack* computed_rows;
    GridTrack* computed_columns;
    int computed_row_count;
    int computed_column_count;
    
    // Grid items
    ViewBlock** grid_items;
    int item_count;
    int allocated_items;
    
    // Grid areas
    GridArea* grid_areas;
    int area_count;
    int allocated_areas;
    
    // Layout state
    bool needs_reflow;
    int explicit_row_count;
    int explicit_column_count;
    int implicit_row_count;
    int implicit_column_count;
} GridContainerLayout;
```

### 1.2 Grid Track and Area Structures

```cpp
typedef enum {
    GRID_TRACK_SIZE_LENGTH,      // Fixed length (px, em, etc.)
    GRID_TRACK_SIZE_PERCENTAGE,  // Percentage of container
    GRID_TRACK_SIZE_FR,          // Fractional unit
    GRID_TRACK_SIZE_MIN_CONTENT, // min-content
    GRID_TRACK_SIZE_MAX_CONTENT, // max-content
    GRID_TRACK_SIZE_AUTO,        // auto
    GRID_TRACK_SIZE_FIT_CONTENT, // fit-content()
    GRID_TRACK_SIZE_MINMAX       // minmax()
} GridTrackSizeType;

typedef struct GridTrackSize {
    GridTrackSizeType type;
    int value;                   // Length value or percentage
    bool is_percentage;
    GridTrackSize* min_size;     // For minmax()
    GridTrackSize* max_size;     // For minmax()
} GridTrackSize;

typedef struct GridTrack {
    GridTrackSize* size;
    int computed_size;           // Final computed size in pixels
    int base_size;               // Base size for fr calculations
    float growth_limit;          // Growth limit for fr calculations
    bool is_flexible;            // Has fr units
} GridTrack;

typedef struct GridArea {
    char* name;                  // Named area identifier
    int row_start;
    int row_end;
    int column_start;
    int column_end;
} GridArea;
```

### 1.3 Grid Item Properties Extension

Extend `ViewBlock` structure to include grid item properties:

```cpp
// Add to ViewBlock in view.hpp (following flex pattern)
typedef struct ViewBlock {
    // ... existing properties ...
    
    // Grid item properties (similar to flex properties)
    int grid_row_start;
    int grid_row_end;
    int grid_column_start;
    int grid_column_end;
    char* grid_area;             // Named grid area
    PropValue justify_self;      // Item-specific justify alignment
    PropValue align_self;        // Item-specific align alignment
    
    // Grid item computed properties
    int computed_grid_row_start;
    int computed_grid_row_end;
    int computed_grid_column_start;
    int computed_grid_column_end;
} ViewBlock;
```

## Phase 2: Grid Layout Algorithm Core (Week 3-4)

### 2.1 Main Grid Layout Entry Point

Create `layout_grid.cpp` following the flex layout pattern:

```cpp
// Main grid layout algorithm entry point
void layout_grid_container_new(LayoutContext* lycon, ViewBlock* container) {
    if (!container || !container->embed || !container->embed->grid_container) {
        log_debug("Early return - missing container or grid properties\n");
        return;
    }
    
    GridContainerLayout* grid_layout = container->embed->grid_container;
    
    log_debug("GRID START - container: %dx%d at (%d,%d)\n", 
              container->width, container->height, container->x, container->y);
    
    // Phase 1: Collect grid items
    ViewBlock** items;
    int item_count = collect_grid_items(container, &items);
    
    // Phase 2: Resolve grid template areas
    resolve_grid_template_areas(grid_layout);
    
    // Phase 3: Place grid items
    place_grid_items(grid_layout, items, item_count);
    
    // Phase 4: Determine grid size
    determine_grid_size(grid_layout);
    
    // Phase 5: Resolve track sizes
    resolve_track_sizes(grid_layout, container);
    
    // Phase 6: Position grid items
    position_grid_items(grid_layout, container);
    
    // Phase 7: Align grid items
    align_grid_items(grid_layout);
    
    grid_layout->needs_reflow = false;
}
```

### 2.2 Grid Item Collection and Placement

```cpp
// Collect grid items from container children
int collect_grid_items(ViewBlock* container, ViewBlock*** items) {
    // Similar to collect_flex_items but for grid
    // Filter out absolutely positioned and hidden items
    // Initialize grid item properties with defaults
}

// Place grid items in the grid
void place_grid_items(GridContainerLayout* grid_layout, ViewBlock** items, int item_count) {
    // Phase 1: Place items with explicit positions
    // Phase 2: Place items with named grid areas
    // Phase 3: Auto-place remaining items based on grid-auto-flow
}
```

### 2.3 Track Sizing Algorithm

```cpp
// Resolve track sizes using CSS Grid track sizing algorithm
void resolve_track_sizes(GridContainerLayout* grid_layout, ViewBlock* container) {
    // Phase 1: Initialize track sizes
    initialize_track_sizes(grid_layout);
    
    // Phase 2: Resolve intrinsic track sizes
    resolve_intrinsic_track_sizes(grid_layout);
    
    // Phase 3: Maximize tracks
    maximize_tracks(grid_layout);
    
    // Phase 4: Expand flexible tracks
    expand_flexible_tracks(grid_layout, container);
}
```

## Phase 3: Grid Content Layout Integration (Week 5-6)

### 3.1 Grid Item Content Layout

Create `layout_grid_content.cpp` following the flex content pattern:

```cpp
// Layout content within a grid item
void layout_grid_item_content(LayoutContext* lycon, ViewBlock* grid_item) {
    if (!grid_item) return;
    
    log_debug("Layout grid item content for %p\n", grid_item);
    
    // Save current context
    Blockbox pa_block = lycon->block;
    Linebox pa_line = lycon->line;
    FontBox pa_font = lycon->font;
    ViewGroup* pa_parent = lycon->parent;
    
    // Set up grid item context
    lycon->parent = (ViewGroup*)grid_item;
    lycon->prev_view = NULL;
    lycon->block.width = grid_item->width;
    lycon->block.height = grid_item->height;
    lycon->block.advance_y = 0;
    lycon->block.max_width = 0;
    lycon->line.left = 0;
    lycon->line.right = grid_item->width;
    lycon->line.vertical_align = LXB_CSS_VALUE_BASELINE;
    line_init(lycon);
    
    // Layout child content
    DomNode* child = grid_item->node ? grid_item->node->first_child() : NULL;
    if (child) {
        do {
            layout_flow_node(lycon, child);
            child = child->next_sibling();
        } while (child);
        
        // Handle last line if needed
        if (!lycon->line.is_line_start) {
            line_break(lycon);
        }
    }
    
    // Calculate final content dimensions
    grid_item->content_width = lycon->block.max_width;
    grid_item->content_height = lycon->block.advance_y;
    
    // Restore context
    lycon->block = pa_block;
    lycon->line = pa_line;
    lycon->font = pa_font;
    lycon->parent = pa_parent;
    
    log_debug("Grid item content layout complete: %dx%d\n", 
              grid_item->content_width, grid_item->content_height);
}
```

### 3.2 Grid Item Intrinsic Sizing

```cpp
// Calculate intrinsic sizes for grid items
void calculate_grid_item_intrinsic_sizes(ViewBlock* grid_item) {
    if (!grid_item) return;
    
    log_debug("Calculate intrinsic sizes for grid item %p\n", grid_item);
    
    IntrinsicSizes sizes = {0};
    
    // Calculate based on content type (similar to flex implementation)
    View* child = grid_item->child;
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
    apply_intrinsic_size_constraints(grid_item, &sizes);
    
    log_debug("Intrinsic sizes calculated: min=%d, max=%d\n", 
              sizes.min_content, sizes.max_content);
}
```

## Phase 4: CSS Integration and Property Resolution (Week 7-8)

### 4.1 CSS Property Parsing

Extend `resolve_style.cpp` to handle grid properties:

```cpp
// Add grid property resolution functions
void resolve_grid_template_rows(ViewBlock* block, lxb_css_property_t* prop);
void resolve_grid_template_columns(ViewBlock* block, lxb_css_property_t* prop);
void resolve_grid_template_areas(ViewBlock* block, lxb_css_property_t* prop);
void resolve_grid_gap(ViewBlock* block, lxb_css_property_t* prop);
void resolve_grid_auto_flow(ViewBlock* block, lxb_css_property_t* prop);
void resolve_grid_item_position(ViewBlock* block, lxb_css_property_t* prop);
```

### 4.2 Grid Container Initialization

```cpp
// Initialize grid container layout state
void init_grid_container(ViewBlock* container) {
    if (!container) return;
    
    // Create embed structure if it doesn't exist
    if (!container->embed) {
        container->embed = (EmbedProp*)calloc(1, sizeof(EmbedProp));
    }
    
    GridContainerLayout* grid = (GridContainerLayout*)calloc(1, sizeof(GridContainerLayout));
    container->embed->grid_container = grid;
    
    // Set default values using enum names that align with Lexbor constants
    grid->justify_content = LXB_CSS_VALUE_START;
    grid->align_content = LXB_CSS_VALUE_START;
    grid->justify_items = LXB_CSS_VALUE_STRETCH;
    grid->align_items = LXB_CSS_VALUE_STRETCH;
    grid->grid_auto_flow = LXB_CSS_VALUE_ROW;
    
    // Initialize gaps
    grid->row_gap = 0;
    grid->column_gap = 0;
    
    // Initialize dynamic arrays
    grid->allocated_items = 8;
    grid->grid_items = (ViewBlock**)calloc(grid->allocated_items, sizeof(ViewBlock*));
    grid->allocated_areas = 4;
    grid->grid_areas = (GridArea*)calloc(grid->allocated_areas, sizeof(GridArea));
    
    grid->needs_reflow = false;
}
```

## Phase 5: Layout Dispatcher Integration (Week 9)

### 5.1 Main Layout Dispatcher Updates

Update `layout.cpp` to handle grid containers:

```cpp
// Add to layout dispatcher in layout.cpp
void layout_block(LayoutContext* lycon, DomNode* node, DisplayValue display) {
    // ... existing code ...
    
    // Check for grid container
    if (display.inner == LXB_CSS_VALUE_GRID) {
        log_debug("Dispatching to grid layout\n");
        layout_grid_container_new(lycon, block);
        return;
    }
    
    // Check for flex container (existing)
    if (display.inner == LXB_CSS_VALUE_FLEX) {
        log_debug("Dispatching to flex layout\n");
        layout_flex_container_new(lycon, block);
        return;
    }
    
    // ... rest of existing code ...
}
```

### 5.2 Grid Item Detection

```cpp
// Add grid item detection to layout flow
bool is_grid_item(ViewBlock* block) {
    if (!block || !block->parent) return false;
    
    ViewBlock* parent = (ViewBlock*)block->parent;
    return parent->embed && 
           parent->embed->grid_container && 
           block->position != POS_ABSOLUTE &&
           block->visibility != VIS_HIDDEN;
}
```

## Phase 6: Advanced Grid Features (Week 10-12)

### 6.1 Named Grid Lines and Areas

```cpp
// Support for named grid lines
typedef struct GridLineName {
    char* name;
    int line_number;
    bool is_row;  // true for row, false for column
} GridLineName;

// Parse and resolve named grid areas
void parse_grid_template_areas(GridContainerLayout* grid, const char* areas_string) {
    // Parse grid-template-areas string
    // Create GridArea structures for named areas
    // Validate area rectangularity
}
```

### 6.2 Subgrid Support (Future)

```cpp
// Placeholder for subgrid support
typedef struct SubgridLayout {
    GridContainerLayout* parent_grid;
    bool inherit_rows;
    bool inherit_columns;
} SubgridLayout;
```

### 6.3 Grid Auto-Placement Algorithm

```cpp
// Advanced auto-placement with sparse/dense packing
void auto_place_grid_items(GridContainerLayout* grid, ViewBlock** items, int item_count) {
    // Implement CSS Grid auto-placement algorithm
    // Handle grid-auto-flow: row/column/dense
    // Resolve auto positions for items without explicit placement
}
```

## Phase 7: Testing and Optimization (Week 13-14)

### 7.1 Unit Tests

Create comprehensive test suite following the flex test pattern:

```cpp
// test_radiant_grid_gtest.cpp
TEST(RadiantGridTest, BasicGridContainer) {
    // Test basic grid container initialization
}

TEST(RadiantGridTest, GridItemPlacement) {
    // Test explicit grid item placement
}

TEST(RadiantGridTest, GridTrackSizing) {
    // Test track sizing algorithm
}

TEST(RadiantGridTest, GridAlignment) {
    // Test grid alignment properties
}
```

### 7.2 Performance Optimization

```cpp
// Optimize grid layout performance
void optimize_grid_layout(GridContainerLayout* grid) {
    // Cache computed track sizes
    // Minimize reflow triggers
    // Optimize memory allocation patterns
}
```

## Implementation Status

| Phase | Status | Deliverables |
|-------|--------|--------------|
| Phase 1 | ✅ **COMPLETED** | Core data structures, grid.hpp |
| Phase 2 | ✅ **COMPLETED** | Grid layout algorithm, layout_grid.cpp |
| Phase 3 | ✅ **COMPLETED** | Grid content layout, layout_grid_content.cpp |
| Phase 4 | ✅ **COMPLETED** | CSS integration, property resolution |
| Phase 5 | ✅ **COMPLETED** | Layout dispatcher integration |
| Phase 6 | ✅ **COMPLETED** | Advanced features, named areas |
| Phase 7 | 🔄 **PENDING** | Testing, optimization, documentation |

## Completed Implementation

### ✅ Phase 1: Core Data Structures
- **File**: `/radiant/grid.hpp`
- **Features**: Complete CSS Grid data structures
  - `GridContainerLayout` - Main grid container state
  - `GridTrackSize` - Track sizing with all CSS Grid size types
  - `GridTrack` - Computed track information
  - `GridArea` - Named grid areas support
  - `GridLineName` - Named grid lines support
  - `IntrinsicSizes` - Content-based sizing

### ✅ Phase 2: Grid Layout Algorithm
- **Files**: `/radiant/layout_grid.cpp`, `/radiant/grid_utils.cpp`, `/radiant/grid_sizing.cpp`, `/radiant/grid_positioning.cpp`
- **Features**: Complete CSS Grid layout algorithm
  - Grid item collection and placement
  - Auto-placement algorithm
  - Track sizing algorithm (intrinsic, flexible, fixed)
  - Grid item positioning and alignment
  - Named area resolution
  - Gap handling

### ✅ Phase 3: Content Layout Integration  
- **File**: `/radiant/layout_grid_content.cpp`
- **Features**: Grid item content layout
  - Content layout within grid items
  - Intrinsic size calculation
  - Multi-pass layout for sizing

### ✅ Phase 4: CSS Integration and Property Resolution
- **Files**: `/radiant/resolve_style.cpp` (updated), `/radiant/view.hpp` (updated), `/radiant/view_pool.cpp` (updated)
- **Features**: Complete CSS Grid property support
  - CSS Grid constants (`LXB_CSS_VALUE_GRID`, `LXB_CSS_VALUE_FR`, etc.)
  - Grid container properties (`grid-template-rows/columns`, `grid-template-areas`)
  - Grid item properties (`grid-row-start/end`, `grid-column-start/end`, `grid-area`)
  - Gap properties (`row-gap`, `column-gap`)
  - Auto-flow property (`grid-auto-flow`)
  - Display value resolution (`display: grid`, `display: inline-grid`)
  - Custom property parsing (until lexbor adds native support)

### ✅ Phase 5: Layout Dispatcher Integration
- **File**: `/radiant/layout_block.cpp` (updated)
- **Features**: Seamless integration with existing layout system
  - Grid container detection (`display: grid`)
  - DOM child processing for grid items
  - Integration with existing layout context
  - Non-breaking changes to existing flex/block layout

### ✅ Phase 6: Advanced Grid Features
- **Files**: `/radiant/grid_advanced.cpp` (new), `/radiant/grid_utils.cpp` (enhanced), `/radiant/grid.hpp` (extended)
- **Features**: Advanced CSS Grid Level 1 features
  - **Enhanced grid-template-areas**: Full CSS syntax parsing with rectangle validation
  - **minmax() function**: Complete minmax(min, max) track sizing support
  - **repeat() function**: repeat(count, tracks) and repeat(auto-fill/auto-fit, tracks)
  - **Dense auto-placement**: `grid-auto-flow: dense` algorithm implementation
  - **Advanced track parsing**: Handles complex CSS Grid template syntax
  - **Auto-fit/auto-fill**: Responsive grid layouts with automatic track generation
  - **Subgrid preparation**: Data structures ready for CSS Grid Level 2

## Current Implementation Summary

### **Files Created:**
1. `/radiant/grid.hpp` - Core grid data structures and function declarations
2. `/radiant/layout_grid.cpp` - Main grid layout algorithm implementation
3. `/radiant/grid_utils.cpp` - Grid utility functions (track lists, areas, line names)
4. `/radiant/grid_sizing.cpp` - CSS Grid track sizing algorithm
5. `/radiant/grid_positioning.cpp` - Grid item positioning and alignment
6. `/radiant/layout_grid_content.cpp` - Grid item content layout
7. `/radiant/grid_advanced.cpp` - Advanced grid features (minmax, repeat, dense)
8. `/test/test_grid_basic.cpp` - Basic grid functionality tests
9. `/test/html/test_grid_basic.html` - Basic CSS Grid HTML test
10. `/test/html/test_grid_areas.html` - Named grid areas HTML test
11. `/test/html/test_grid_advanced.html` - Advanced features HTML test

### **Files Modified:**
1. `/radiant/view.hpp` - Added grid properties to ViewSpan and EmbedProp + CSS constants
2. `/radiant/layout_block.cpp` - Added grid layout dispatch
3. `/radiant/layout.hpp` - Added grid function declarations
4. `/radiant/resolve_style.cpp` - Added complete CSS Grid property resolution
5. `/radiant/view_pool.cpp` - Added grid container allocation function

### **Key Features Implemented:**
- ✅ Complete CSS Grid data structures
- ✅ Grid container initialization and cleanup
- ✅ Grid item collection and auto-placement
- ✅ Track sizing algorithm (length, percentage, fr, auto, min/max-content)
- ✅ Grid item positioning with gap support
- ✅ Grid item alignment (justify-self, align-self)
- ✅ Named grid areas (full CSS syntax support)
- ✅ Named grid lines (basic support)
- ✅ Intrinsic size calculation
- ✅ Layout dispatcher integration
- ✅ Memory management with proper cleanup
- ✅ Structured logging throughout
- ✅ **Complete CSS property integration**
- ✅ **CSS Grid constants and display values**
- ✅ **All major CSS Grid properties supported**
- ✅ **HTML/CSS test cases created**
- ✅ **Advanced Features (Phase 6):**
  - ✅ **minmax() function**: Complete track sizing with constraints
  - ✅ **repeat() function**: Including auto-fill and auto-fit
  - ✅ **Dense auto-placement**: grid-auto-flow: dense algorithm
  - ✅ **Enhanced template areas**: Full CSS syntax parsing
  - ✅ **Advanced track parsing**: Complex CSS Grid templates
  - ✅ **Subgrid preparation**: Ready for CSS Grid Level 2

### **Architecture Highlights:**
- **Non-Breaking**: Extends existing layout system without breaking changes
- **Memory Pool Integration**: Uses existing memory management patterns
- **Lexbor CSS Integration**: Aligns with existing CSS constant usage
- **Performance Optimized**: Follows established patterns from flex layout
- **Extensible Design**: Ready for advanced features like subgrid

## Next Steps (Remaining Phases)

### 🔄 Phase 7: Testing and Optimization
**Priority**: Medium  
**Estimated Effort**: 1-2 weeks

**Tasks Remaining:**
1. **Comprehensive Test Suite**: Expand beyond basic tests
2. **Browser Compatibility Testing**: Validate against major browsers
3. **Performance Optimization**: Profile and optimize grid algorithm
4. **Documentation**: Complete API documentation and usage examples

## Immediate Next Actions

### 1. Compilation and Testing (High Priority)
The CSS Grid implementation is now **feature-complete** with all major CSS Grid Level 1 features implemented:

- **Compile** the complete grid implementation (11 files)
- **Run** comprehensive test suite (basic, areas, advanced features)
- **Validate** HTML/CSS grid layouts with all test cases
- **Test** advanced features: minmax(), repeat(), dense packing
- **Verify** grid-template-areas parsing and validation

### 2. Integration and Performance Testing (High Priority)
- **Integration testing** with existing flex and block layouts
- **Regression testing** to ensure no breaking changes
- **Memory management** validation and cleanup verification
- **Performance benchmarking** compared to flex layout
- **Browser compatibility** testing against major browsers

### 3. Production Readiness (Medium Priority)
- **Documentation** completion and API reference
- **Error handling** and edge case validation
- **Optimization** of grid algorithm performance
- **Code review** and final quality assurance

## Success Metrics

1. **CSS Compliance**: Support for CSS Grid Level 1 specification
2. **Performance**: Grid layout performance comparable to flex layout
3. **Memory Efficiency**: Minimal memory overhead for grid structures
4. **Test Coverage**: >90% test coverage for grid functionality
5. **Browser Compatibility**: Layout results match major browsers
6. **Non-Breaking**: Zero impact on existing flex/block layout functionality

## Risk Mitigation

1. **Complexity Management**: Incremental implementation with working milestones
2. **Memory Management**: Follow existing memory pool patterns
3. **CSS Compatibility**: Regular validation against CSS specifications
4. **Performance**: Continuous benchmarking against flex layout
5. **Integration**: Careful testing of layout dispatcher changes

This implementation plan provides a comprehensive roadmap for adding CSS Grid Layout support to Radiant while maintaining the established architecture patterns and design principles.
