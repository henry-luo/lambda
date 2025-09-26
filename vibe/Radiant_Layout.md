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

## Key Data Structures

### ViewBlock Structure

```cpp
typedef struct ViewBlock {
    // Positioning and dimensions
    int x, y;                    // Absolute position
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
    PropValue box_sizing;        // LXB_CSS_VALUE_CONTENT_BOX or LXB_CSS_VALUE_BORDER_BOX
    int given_width, given_height;  // CSS specified width/height values
} BlockProp;
```


## CSS Property Resolution

### Style Resolution Pipeline

1. **CSS Parsing**: Lexbor library parses CSS into property declarations
2. **Property Resolution**: `resolve_style.cpp` processes each CSS property
3. **Value Calculation**: Length values resolved to pixels
4. **Property Storage**: Values stored in appropriate data structures

## Box Model Implementation

### Box-Sizing Calculation

The box model is implemented in `layout_block.cpp` with proper `box-sizing` support:

```cpp
// Box-sizing calculation for width
int content_width = 0;
if (lycon->block.given_width >= 0) { 
    content_width = lycon->block.given_width;
    
    // Apply box-sizing calculation
    if (block->blk && block->blk->box_sizing == LXB_CSS_VALUE_BORDER_BOX) {
        // For border-box, subtract padding and borders from given width
        int padding_and_border = 0;
        if (block->bound) {
            padding_and_border += block->bound->padding.left + block->bound->padding.right;
            if (block->bound->border) {
                padding_and_border += block->bound->border->width.left + block->bound->border->width.right;
            }
        }
        content_width = max(content_width - padding_and_border, 0);
    }
}
```

### Critical Design Decision

**Box-sizing is calculated in the block layout phase, NOT in the flex algorithm.** This ensures:
- Consistent dimensions across layout phases
- No double-subtraction bugs
- Proper integration with CSS box model

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

**Process Flow**:
1. Parse HTML and CSS using Lexbor
2. Build DOM tree structure
3. Resolve CSS styles and properties
4. Execute layout algorithms (block, flex, etc.)
5. Generate view tree output
6. Write JSON layout data

### View Tree Output

The view tree provides detailed debugging information:

```
[view-block:div, x:2, y:2, wd:100, hg:100
  {line-hg:0.000000 txt-align:center txt-indent:0.000000 ls-sty-type:0
  min-wd:0.000000 max-wd:0.000000 min-hg:0.000000 max-hg:0.000000 
  box-sizing:border-box given-wd:100 given-hg:100 }
  {flex-container: row-gap:10 column-gap:10 main-axis:396 cross-axis:296}
  {bgcolor:#ffccccff margin:{left:0, right:0, top:0, bottom:0} 
   padding:{left:10, right:10, top:10, bottom:10}}
  text:'Item 1', start:0, len:6, x:28, y:10, wd:43, hg:18
]
```

**Key Information**:
- **Position**: `x:2, y:2` (includes border offset)
- **Dimensions**: `wd:100, hg:100` (border-box size)
- **Box-sizing**: `box-sizing:border-box given-wd:100 given-hg:100`
- **Flex Properties**: `row-gap:10 column-gap:10`
- **Box Model**: Complete margin, padding, border information

### Browser Reference Data Capture

The automated testing system uses **Puppeteer** to capture browser rendering results as reference data:

```javascript
// Puppeteer browser automation for reference data
const puppeteer = require('puppeteer');

async function captureBrowserLayout(htmlFile) {
    const browser = await puppeteer.launch();
    const page = await browser.newPage();
    
    // Load the test HTML file
    await page.goto(`file://${htmlFile}`);
    
    // Execute JavaScript to extract element positions and dimensions
    const layoutData = await page.evaluate(() => {
        const elements = [];
        document.querySelectorAll('*').forEach(el => {
            const rect = el.getBoundingClientRect();
            elements.push({
                tag: el.tagName.toLowerCase(),
                x: Math.round(rect.left),
                y: Math.round(rect.top),
                width: Math.round(rect.width),
                height: Math.round(rect.height),
                text: el.textContent?.trim() || ''
            });
        });
        return elements;
    });
    
    await browser.close();
    return layoutData;
}
```

**Browser Capture Process**:
1. **HTML Loading**: Puppeteer loads test HTML files in headless Chrome
2. **Style Application**: Browser applies CSS styles and performs layout
3. **Element Extraction**: JavaScript extracts `getBoundingClientRect()` data
4. **Reference Storage**: Layout data saved as JSON for comparison
5. **Automated Updates**: Reference data can be regenerated when browser behavior changes

### Test Comparison System

```javascript
// Element matching criteria
function elementsMatch(radiant, browser) {
    return (
        radiant.tag === browser.tag &&
        Math.abs(radiant.x - browser.x) <= tolerance &&
        Math.abs(radiant.y - browser.y) <= tolerance &&
        Math.abs(radiant.width - browser.width) <= tolerance &&
        Math.abs(radiant.height - browser.height) <= tolerance
    );
}
```

**Matching Process**:
1. Parse both Radiant and browser reference data
2. Extract element positions and dimensions
3. Apply tolerance-based matching (typically 1-2px)
4. Calculate match percentage
5. Generate detailed failure reports

### Test Results Interpretation

**Success Metrics**:
- **Element Match Rate**: Percentage of elements with correct position/size
- **Layout Accuracy**: Pixel-perfect positioning within tolerance
- **Property Compliance**: Correct CSS property application

**Common Failure Patterns**:
- **Position Offset**: Missing border/padding calculations
- **Size Mismatch**: Box-sizing calculation errors
- **Missing Elements**: Layout algorithm not creating expected elements
- **Spacing Issues**: Gap or margin/padding miscalculations

### Key Make Targets

The Radiant layout engine provides several make targets for development and testing:

#### `make build-radiant`
```bash
make build-radiant
```
**Purpose**: Build the Radiant layout engine executable and test suites
**Process**:
1. Generates platform-specific Premake configuration
2. Compiles `radiant.exe` with all layout algorithms
3. Builds GTest-based test executables
4. Compiles standalone test executables for additional coverage

**Output**: Ready-to-run Radiant executable and comprehensive test suite

#### `make test-radiant`
```bash
make test-radiant
```
**Purpose**: Execute all Radiant-specific unit and integration tests

#### `make test-layout`
```bash
make test-layout
```
**Purpose**: Run comprehensive layout integration tests against browser reference data
**Test Process**:
1. **HTML Test Execution**: Processes all test files in `test/layout/data/basic/`
2. **Layout Calculation**: Runs Radiant layout engine on each test case
3. **Reference Comparison**: Compares results against Puppeteer-captured browser data
4. **Element Matching**: Performs tolerance-based position/dimension matching
5. **Report Generation**: Creates detailed pass/fail reports with match percentages

**Test Categories**:
- **Block Layout Tests**: `block_001_margin_padding`, basic div positioning
- **Box Model Tests**: `box_001_basic_div`, `box_004_box_sizing`, border-box calculations
- **Flexbox Tests**: `flex_001_basic_layout`, `flex_002_wrap`, `flex_004_column_direction`
- **Advanced Features**: Gap properties, alignment, wrapping, direction changes

**Success Metrics**:
- **Element Match Rate**: Percentage of elements with correct position/size
- **Test Pass Rate**: Overall percentage of tests achieving acceptable match rates
- **Regression Detection**: Identifies layout changes that break existing functionality

## Performance Considerations

### Memory Pool Integration

Radiant uses Lambda's custom memory pool for efficient allocation:

```cpp
BlockProp* alloc_block_prop(LayoutContext* lycon) {
    BlockProp* prop = (BlockProp*)alloc_prop(lycon, sizeof(BlockProp));
    // Initialize with proper defaults
    prop->box_sizing = LXB_CSS_VALUE_CONTENT_BOX;
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
2. **Advanced Flexbox**: Baseline alignment, flex-wrap improvements
3. **Responsive Layout**: Media queries and container queries
4. **Performance Optimization**: SIMD vectorization, parallel layout
5. **Advanced Box Model**: CSS transforms, filters, animations

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
