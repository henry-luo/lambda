# Radiant Layout Engine Architecture

## Overview

Radiant is Lambda's HTML/CSS/SVG rendering engine that implements browser-compatible layout algorithms. It features a multi-phase layout system with support for block layout, flexbox, and CSS box model calculations.

## Core Architecture

### Layout Pipeline

The Radiant layout engine follows a structured pipeline:

1. **DOM Parsing** → **Style Resolution** → **Layout Calculation** → **Rendering**
2. **CSS Property Parsing** → **Box Model Application** → **Layout Algorithm Execution** → **View Tree Generation**

### Key Components

- **Layout Context (`LayoutContext`)**: Central coordinator for layout operations
- **View Tree**: Hierarchical structure representing rendered elements
- **Style Resolution**: CSS property parsing and application
- **Layout Algorithms**: Block, flex, and other layout implementations
- **Pixel Ratio**: hi-res display may have pixel ratio higher than 1. This value is stored in UiContext. And during resolve_style(), CSS pixel values are converted to physical pixel values, and dimensional fields in Radian view tree store physical pixel value.

## Key Data Structures

### ViewBlock Structure

```cpp
typedef struct ViewBlock {
    // Positioning and dimensions
    int x, y;                    // X, Y position relative to immediate containing block
    int width, height;           // Current dimensions
    int content_width, content_height;  // Content area dimensions

    // Layout properties
    BlockProp* blk;              // Block-specific properties
    BoundProp* bound;            // Margin, padding, border
    EmbedProp* embed;            // Flex container, etc.

    // Hierarchy
    ViewGroup* parent;
    ViewBlock* child;
    ViewBlock* next;

    // Flex properties
    int flex_basis;              // -1 for auto, >= 0 for fixed
    float flex_grow, flex_shrink;
    int order;
    bool flex_basis_is_percent;
} ViewBlock;
```

### BlockProp Structure

```cpp
typedef struct {
    PropValue text_align;
    int line_height;
    int text_indent;
    int min_width, max_width;
    int min_height, max_height;
    PropValue list_style_type;

    // Box-sizing support (CRITICAL)
    PropValue box_sizing;        // CSS_VALUE_CONTENT_BOX or CSS_VALUE_BORDER_BOX
    int given_width, given_height;  // CSS specified width/height values
} BlockProp;
```


## CSS Property Resolution

### Style Resolution Pipeline

1. **CSS Parsing**: Lexbor library parses CSS into property declarations
2. **Property Resolution**: `resolve_style.cpp` processes each CSS property
3. **Value Calculation**: Length values resolved to pixels
4. **Property Storage**: Values stored in appropriate data structures

## Automated Layout Integration Tests

### Test Architecture

The layout testing system consists of:

1. **HTML Test Files**: Located in `test/layout/data/basic/`
2. **Reference Data**: Expected layout results from browser rendering
3. **Comparison Engine**: Element-by-element matching system
4. **Test Runner**: Automated execution and reporting

### Layout Subcommand

```bash
./radiant.exe layout test/layout/data/basic/flex_001_basic_layout.html
```

### View Tree Output

The view tree provides detailed debugging information:

**Key Information**:
- **Position**: `x:2, y:2` (includes border offset)
- **Dimensions**: `wd:100, hg:100` (border-box size)
- **Box-sizing**: `box-sizing:border-box given-wd:100 given-hg:100`
- **Flex Properties**: `row-gap:10 column-gap:10`
- **Box Model**: Complete margin, padding, border information

### Browser Reference Data Capture

The automated testing system uses **Puppeteer** to capture browser rendering results as reference data:

**Browser Capture Process**:
1. **HTML Loading**: Puppeteer loads test HTML files in headless Chrome
2. **Style Application**: Browser applies CSS styles and performs layout
3. **Element Extraction**: JavaScript extracts `getBoundingClientRect()` data
4. **Reference Storage**: Layout data saved as JSON for comparison
5. **Automated Updates**: Reference data can be regenerated when browser behavior changes

### Test Comparison System

**Matching Process**:
1. Parse both Radiant and browser reference data
2. Extract element positions and dimensions
3. Apply tolerance-based matching (typically 1-2px)
4. Calculate match percentage
5. Generate detailed failure reports

### Key Make Targets

The Radiant layout engine provides several make targets for development and testing:

#### `make build-radiant`
```bash
make build-radiant      # Build the Radiant layout engine executable
make radiant            # Alias for build-radiant
make build-radiant-test # Build Radiant C/C++ unit tests
make test-radiant       # Run Radiant C/C++ unit tests
make test-layout        # Run Radiant automated integration test
```

## Performance Considerations

### Memory Pool Integration

Radiant uses Lambda's custom memory pool for efficient allocation:

```cpp
BlockProp* alloc_block_prop(LayoutContext* lycon) {
    BlockProp* prop = (BlockProp*)alloc_prop(lycon, sizeof(BlockProp));
    // Initialize with proper defaults
    prop->box_sizing = CSS_VALUE_CONTENT_BOX;
    prop->given_width = prop->given_height = -1;
    return prop;
}
```

### Layout Caching

- **Reflow Detection**: `needs_reflow` flags prevent unnecessary recalculations
- **Incremental Updates**: Only affected subtrees are re-laid out
- **Property Caching**: Resolved CSS values cached in data structures

## Future Enhancements

### Planned Features

1. **CSS Grid Layout**: Full grid container and item support
2. **Responsive Layout**: Media queries and container queries
3. **Performance Optimization**: SIMD vectorization, parallel layout
4. **Advanced Box Model**: CSS transforms, filters, animations

### Architecture Improvements

1. **Layout Phases**: More granular phase separation
2. **Constraint System**: Unified constraint solving for all layout types
3. **Incremental Layout**: Fine-grained invalidation and updates
4. **Memory Optimization**: Zero-copy operations, memory pooling
5. **Debug Tools**: Visual layout debugging, performance profiling

## Conclusion

The Radiant layout engine represents a significant achievement in browser-compatible layout implementation. The successful integration of CSS box-sizing, flexbox gap properties, and proper space distribution demonstrates the engine's capability to handle complex CSS layouts.

Key success factors:
- **Proper separation of concerns** between layout phases
- **Comprehensive box model implementation** with border-box support
- **Robust flex algorithm** following CSS specifications
- **Extensive debugging and testing infrastructure**
- **Memory-efficient design** using custom allocation strategies

The engine is now positioned for further enhancements toward full CSS3+ compatibility and production-ready performance.
