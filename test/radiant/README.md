# Browser-Based Layout Testing for Radiant

This system uses **Puppeteer** to extract layout data from real browser engines and generate reference test data for validating the Radiant layout engine. This approach ensures that Radiant's layout behavior matches web standards as implemented by modern browsers.

## Overview

The system consists of three main components:

1. **JavaScript Layout Extractor** (`tools/layout_extractor.js`) - Uses Puppeteer to load HTML/CSS in a headless browser and extract computed layout properties
2. **C++ Validation Framework** (`browser_layout_validator.hpp/.cpp`) - Loads extracted JSON data and validates Radiant's layout output
3. **Test Integration** (`test_browser_integration_gtest.cpp`) - Criterion-based tests that run the validation

## Quick Start

```bash
# 1. Navigate to the test directory
cd test/radiant/

# 2. Run the complete workflow
./run_browser_tests.sh

# This will:
# - Install Node.js dependencies (Puppeteer)
# - Generate sample HTML/CSS test cases
# - Extract layout data from Chrome/Chromium
# - Create reference JSON files
# - Set up C++ integration tests
```

## Architecture

### 1. Browser Layout Extraction

The Node.js extractor loads HTML/CSS content in a headless browser and uses the DOM API to extract:

- **Element Positions**: `getBoundingClientRect()` for precise positioning
- **Computed Styles**: `getComputedStyle()` for resolved CSS properties  
- **Layout Properties**: Flexbox properties, margins, padding, borders
- **Metadata**: Browser version, extraction timestamp, test parameters

**Example Usage:**
```bash
# Extract single test case
node tools/layout_extractor.js extract-single test.html test.css output.json

# Batch process directory
node tools/layout_extractor.js extract-batch ./input_dir ./output_dir

# Inline HTML/CSS
node tools/layout_extractor.js extract-inline '<div class="flex">...</div>' '.flex { display: flex; }'
```

### 2. Generated JSON Format

The extractor produces JSON test descriptors like this:

```json
{
  "test_id": "flexbox_basic_row_layout",
  "category": "flexbox", 
  "spec_reference": "https://www.w3.org/TR/css-flexbox-1/#flex-direction-property",
  "html": "<div class='container'><div class='item'>1</div><div class='item'>2</div></div>",
  "css": ".container { display: flex; width: 400px; } .item { width: 100px; height: 50px; }",
  "expected_layout": {
    ".container": {
      "x": 0, "y": 0, "width": 400, "height": 50,
      "computed_style": {
        "display": "flex",
        "justify_content": "flex-start",
        "align_items": "stretch"
      }
    },
    ".item[0]": { "x": 0, "y": 0, "width": 100, "height": 50 },
    ".item[1]": { "x": 100, "y": 0, "width": 100, "height": 50 }
  },
  "properties_to_test": ["position", "dimensions", "flex_properties"],
  "browser_engine": "chromium",
  "browser_version": "Chromium/119.0.0.0",
  "tolerance_px": 1.0
}
```

### 3. C++ Integration

The C++ validation framework provides:

```cpp
// Load browser-generated test data
auto descriptor = BrowserLayoutValidator::loadTestDescriptor("test.json");

// Run Radiant layout engine
Document* doc = parse_and_layout(descriptor->html, descriptor->css);

// Validate against browser reference
LayoutTestResult result = BrowserLayoutValidator::validateLayout(*descriptor, doc->view_tree);

// Check results
if (result.passed) {
    printf("✅ Test passed: %s\n", result.test_id.c_str());
} else {
    printf("❌ Test failed: %d differences found\n", result.differences.size());
}
```

## Test Categories

The system supports multiple test categories:

### Flexbox Tests
- Direction (row, column, reverse variants)
- Wrapping and line distribution  
- Justify content (start, end, center, space-between, space-around, space-evenly)
- Align items and align content
- Flex grow/shrink calculations
- Order property support

### Block Layout Tests  
- Margin collapsing behavior
- Padding and border calculations
- Width/height resolution
- Auto margins and centering

### Inline Layout Tests
- Text flow and line breaking
- Vertical alignment
- Baseline positioning
- Mixed content (text + inline-block)

### Complex Nested Tests
- Flex containers with block children
- Block containers with flex children
- Multiple nesting levels
- Real-world layout patterns

## Benefits

### 1. **Guaranteed Spec Compliance**
Tests are generated from actual browser engines (Chrome/Chromium), ensuring compatibility with web standards as implemented in practice.

### 2. **Comprehensive Coverage**
Easily generate tests for any HTML/CSS combination, including edge cases that might be missed in manual test creation.

### 3. **Regression Prevention**
Automated detection when Radiant's layout behavior diverges from browser behavior.

### 4. **Cross-Platform Consistency**
Reference data is generated once and can be used across different development environments.

### 5. **Documentation Value**
Test cases serve as living documentation of supported CSS features with concrete examples.

### 6. **Performance Baseline**
Can track performance regressions by measuring layout computation time against browser reference times.

## Integration with Existing Tests

The browser tests complement the existing Criterion test suite:

```cpp
// Existing unit tests (test_layout_flex.c)
Test(flexbox_tests, basic_layout) {
    // Test individual layout algorithms
}

// New browser reference tests  
Test(browser_layout_integration, validate_against_browser_data) {
    // Test end-to-end behavior against browser
}
```

## Workflow Integration

### Development Workflow
1. **Create Test Cases**: Write HTML/CSS for new features
2. **Extract Reference**: Run browser extractor to generate JSON
3. **Implement Feature**: Code the layout algorithm in Radiant
4. **Validate**: C++ tests compare Radiant output to browser reference
5. **Iterate**: Fix differences until tests pass

### CI/CD Integration
```bash
# Add to CI pipeline
make build-browser-tests
./test/radiant/run_browser_tests.sh --test-only
make run-browser-tests
```

## Advanced Features

### Visual Regression Testing
The system can be extended to generate reference images and perform pixel-perfect comparisons:

```javascript
// In layout_extractor.js
await page.screenshot({ 
    path: `${testId}_reference.png`,
    clip: containerRect 
});
```

### W3C Test Suite Integration
The extractor can process official W3C CSS test cases:

```bash
# Download W3C CSS Test Suite
git clone https://github.com/web-platform-tests/wpt.git
node tools/layout_extractor.js extract-batch wpt/css/css-flexbox/ ./w3c_tests/
```

### Performance Profiling
Track layout performance against browser benchmarks:

```cpp
auto start = std::chrono::high_resolution_clock::now();
layout_html_doc(ui_context, doc, false);
auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
// Compare against browser timing data
```

## Files Structure

```
test/radiant/
├── tools/
│   ├── layout_extractor.js     # Main extraction tool
│   ├── package.json            # Node.js dependencies  
│   ├── setup.sh               # Setup script
│   └── test_extractor.js      # Tool testing
├── browser_layout_validator.hpp/.cpp  # C++ integration
├── test_browser_integration_gtest.cpp  # Criterion tests
├── run_browser_tests.sh       # Complete workflow
├── test_cases/               # HTML/CSS test inputs
├── data/                     # Generated JSON reference data  
└── reports/                  # Test results and documentation
```

## Dependencies

- **Node.js 16+**: For running Puppeteer
- **Puppeteer**: Headless Chrome automation
- **JSON-C** or **nlohmann/json**: C++ JSON parsing
- **Criterion**: C++ testing framework (existing)

## Future Enhancements

1. **CSS Grid Support**: Extend extractor for CSS Grid layout
2. **Animation Testing**: Extract keyframe and transition data
3. **Responsive Design**: Test media queries and viewport changes  
4. **Accessibility**: Extract ARIA and semantic layout information
5. **Performance Metrics**: Layout timing and optimization opportunities

This browser-based testing system provides a robust foundation for ensuring Radiant's layout engine maintains compatibility with web standards while enabling rapid development and validation of new CSS features.
