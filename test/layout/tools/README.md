# Radiant Layout Test Framework Enhancement

## Overview

This enhancement implements the complete integration between the automated layout test framework and the Radiant layout engine, as outlined in `Flex_Auto.md`. The implementation follows the proven patterns from `test_radiant_flex_gtest.cpp` to ensure reliability and compatibility.

## Architecture

### Core Components

1. **HtmlCssParser** (`html_css_parser.hpp/cpp`)
   - Parses HTML content and extracts CSS styles
   - Supports inline styles and `<style>` tags
   - Simple but effective CSS rule matching

2. **CssRadiantMapper** (`css_radiant_mapper.hpp/cpp`)
   - Maps CSS flex properties to Radiant's internal representation
   - Uses Lexbor CSS constants for consistency
   - Handles enhanced flexbox properties (auto margins, aspect ratio, etc.)

3. **ViewBlockBuilder** (`viewblock_builder.hpp/cpp`)
   - Converts parsed HTML elements to Radiant's ViewBlock structure
   - Follows exact patterns from proven unit tests
   - Handles parent-child relationships and property application

4. **TestRunner Integration** (`layout_test_framework.cpp`)
   - Implements the core `runRadiantLayout()` function
   - Manages memory pools and layout context
   - Extracts results and converts back to test format

## Key Features

### HTML/CSS Parsing
- Supports basic HTML parsing with attributes and nested elements
- CSS parsing for flex properties, dimensions, and positioning
- Inline style and stylesheet support

### CSS Property Mapping
```cpp
// Example CSS to Radiant mapping
CSS: "display: flex; gap: 10px; justify-content: center;"
Maps to:
- direction = LXB_CSS_VALUE_ROW
- column_gap = 10, row_gap = 10  
- justify = LXB_CSS_VALUE_CENTER
```

### ViewBlock Tree Construction
- Creates flex containers using `init_flex_container()`
- Sets up parent-child relationships following proven patterns
- Applies dimensions and flex properties correctly

### Layout Execution
- Calls `layout_flex_container_new()` for flex containers
- Proper memory management with `init_view_pool()` and `cleanup_view_pool()`
- Error handling and graceful degradation

### Result Extraction
- Extracts layout positions, dimensions, and properties
- Maps Radiant constants back to CSS values
- Builds CSS selectors for element identification

## Usage

### Basic Test Case
```cpp
TestCase flexTest;
flexTest.name = "basic_flex";
flexTest.htmlContent = R"(
    <style>
        .container { display: flex; width: 400px; gap: 10px; }
        .item { width: 100px; flex-grow: 1; }
    </style>
    <div class="container">
        <div class="item"></div>
        <div class="item"></div>
    </div>
)";

ValidationResult result = testRunner->runSingleTest(flexTest);
```

### Integration Tests
The framework now includes integration tests in `test_layout_auto.cpp`:
- `RadiantIntegrationTests::BasicFlexContainerCreation`
- `RadiantIntegrationTests::FlexDirectionColumn`
- `RadiantIntegrationTests::JustifyContentCenter`

## Implementation Details

### Memory Management
- Uses Radiant's memory pool system (`VariableMemPool`)
- Proper initialization with `init_view_pool(lycon)`
- Cleanup with `cleanup_view_pool(lycon)` to prevent leaks

### Error Handling
- Graceful handling of parsing errors
- Memory cleanup on exceptions
- Verbose logging for debugging

### Performance
- Efficient HTML/CSS parsing
- Minimal memory allocations
- Fast ViewBlock tree construction

## Testing

### Unit Tests
Run the enhanced test suite:
```bash
cd test/layout/tools
./test_layout_auto.exe
```

### Integration Validation
The framework validates against browser reference data:
- Position accuracy (x, y coordinates)
- Dimension accuracy (width, height)
- Flex property preservation
- Layout algorithm correctness

## Proven Patterns

This implementation follows the exact patterns from `test_radiant_flex_gtest.cpp`:

1. **ViewBlock Creation**:
   ```cpp
   ViewBlock* container = alloc_view_block(lycon);
   container->content_width = width;
   container->content_height = height;
   init_flex_container(container);
   ```

2. **Parent-Child Setup**:
   ```cpp
   parent->first_child = children[0];
   parent->last_child = children.back();
   children[i]->parent = (ViewGroup*)parent;
   ```

3. **Memory Pool Management**:
   ```cpp
   init_view_pool(lycon);
   // ... layout operations ...
   cleanup_view_pool(lycon);
   ```

## Future Enhancements

1. **CSS Grid Support**: Extend parser and mapper for CSS Grid
2. **Advanced Selectors**: Support for pseudo-classes and complex selectors
3. **Animation Support**: Handle CSS transitions and animations
4. **Performance Optimization**: Caching and batch operations

## Dependencies

- Radiant layout engine (`layout.hpp`, `view.hpp`)
- Lexbor CSS constants
- JSON-C for reference data parsing
- Google Test for test framework

## Files Modified/Created

### New Files
- `html_css_parser.hpp/cpp` - HTML/CSS parsing
- `css_radiant_mapper.hpp/cpp` - CSS to Radiant mapping
- `viewblock_builder.hpp/cpp` - ViewBlock tree construction
- `example_test.html` - Example test case

### Enhanced Files
- `layout_test_framework.hpp` - Added integration methods
- `layout_test_framework.cpp` - Implemented `runRadiantLayout()`
- `test_layout_auto.cpp` - Added integration tests

## Success Metrics

✅ **Complete Integration**: Replaced TODO stub with full implementation
✅ **Memory Safety**: Proper pool management and cleanup
✅ **Proven Patterns**: Follows successful unit test patterns
✅ **Error Handling**: Graceful degradation and logging
✅ **Test Coverage**: Integration tests for core functionality

The enhancement successfully bridges the gap between automated browser validation and Radiant's layout engine, enabling comprehensive regression testing and quality assurance for the layout system.
