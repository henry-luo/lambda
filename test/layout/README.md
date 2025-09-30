# Radiant Layout Engine Testing Framework

A comprehensive automated testing system for validating the Radiant layout engine against browser reference implementations. This framework generates systematic HTML/CSS test cases, extracts precise layout data from browsers using Puppeteer, and validates Radiant's layout calculations through C++ test suites.

## 🎯 Overview

The Radiant Layout Testing Framework provides:

- **Automated Test Generation**: Systematic HTML/CSS test case creation covering flexbox, block layout, and complex scenarios
- **Browser Reference Extraction**: Puppeteer-based layout data extraction from Chromium for ground-truth references  
- **C++ Validation Framework**: Comprehensive validation system comparing Radiant output against browser references
- **Integrated Workflow**: Complete pipeline from test generation to validation reporting
- **Graduated Complexity**: Basic, intermediate, and advanced test categories with appropriate success rate expectations

## 🏗️ Architecture

```
test/layout/
├── run_tests.sh           # Main workflow orchestration script
├── package.json           # Node.js dependencies and npm scripts
├── data/                  # Generated HTML/CSS test cases
│   ├── basic/            # Simple layout tests (margins, padding, basic flexbox)
│   ├── intermediate/     # Complex nesting and combinations  
│   └── advanced/         # Real-world layout scenarios
├── reference/             # Browser-extracted reference data (JSON)
│   ├── basic/
│   ├── intermediate/
│   └── advanced/
├── tools/                 # Implementation tools
│   ├── generate_tests.js    # Test case generator
│   ├── extract_layout.js    # Puppeteer browser extraction
│   ├── layout_test_framework.hpp/.cpp  # C++ validation framework
│   ├── test_layout_auto.cpp # GTest integration
│   └── Makefile            # C++ compilation configuration
└── reports/               # Test results and analysis
    ├── test_results.xml      # GTest XML output
    ├── extraction_summary.json  # Browser extraction results
    └── latest_summary.txt    # Human-readable summary
```

## 🚀 Quick Start

### Prerequisites

- **Node.js** (≥16.0.0) and npm (≥8.0.0)
- **C++ Compiler** (g++ or clang++ with C++17 support)
- **System Libraries**: 
  - `json-c` for JSON parsing
  - `gtest` for C++ testing framework
  - `make` for build automation

#### Installation on macOS:
```bash
brew install node json-c googletest
```

#### Installation on Ubuntu:
```bash
sudo apt-get update
sudo apt-get install nodejs npm build-essential libjson-c-dev libgtest-dev
```

### Basic Usage

1. **Clone and Setup**:
   ```bash
   cd test/layout
   npm install
   ```

2. **Run Complete Testing Pipeline**:
   ```bash
   ./run_tests.sh
   ```

3. **View Results**:
   ```bash
   npm run report
   ```

## 📋 Detailed Usage

### Complete Workflow
```bash
# Run everything (generation → extraction → validation)
./run_tests.sh

# Run with verbose output
./run_tests.sh --verbose

# Clean previous results before running
./run_tests.sh --clean --verbose
```

### Category-Specific Testing
```bash
# Test specific categories
./run_tests.sh --category basic
./run_tests.sh --category intermediate  
./run_tests.sh --category advanced

# Using npm scripts
npm run test:basic
npm run test:intermediate
npm run test:advanced
```

### Step-by-Step Execution
```bash
# 1. Generate test cases only
./run_tests.sh --generate-only

# 2. Extract browser references only  
./run_tests.sh --extract-only

# 3. Run validation tests only
./run_tests.sh --validate-only
```

### Manual Tool Usage
```bash
# Generate specific test category
cd tools
node generate_tests.js category basic

# Extract references for specific category
node extract_layout.js category intermediate

# Compile and run C++ tests
make all
./test_layout_auto --gtest_output=xml:../reports/test_results.xml
```

## 🧪 Test Categories

### Basic Tests
- **Focus**: Fundamental layout properties and simple scenarios
- **Coverage**: Margins, padding, basic flexbox, block layout
- **Expected Success Rate**: ≥80%
- **Example Tests**: Single flex container, margin collapsing, padding calculations

### Intermediate Tests  
- **Focus**: Complex combinations and nested layouts
- **Coverage**: Nested flexbox, mixed layout types, responsive scenarios
- **Expected Success Rate**: ≥70%
- **Example Tests**: Flex containers within blocks, complex alignments

### Advanced Tests
- **Focus**: Real-world layout scenarios and edge cases
- **Coverage**: Multi-level nesting, complex interactions, performance scenarios  
- **Expected Success Rate**: ≥60%
- **Example Tests**: Complete page layouts, complex grids, edge cases

## 📊 Test Framework Details

### Test Generation (`generate_tests.js`)

The test generator creates systematic HTML/CSS combinations using feature matrices:

```javascript
// Flexbox test matrix example
const flexboxTests = [
  { flexDirection: 'row', justifyContent: 'flex-start', alignItems: 'stretch' },
  { flexDirection: 'column', justifyContent: 'center', alignItems: 'center' },
  // ... comprehensive combinations
];
```

Key features:
- **Systematic Coverage**: Matrix-based test generation ensures comprehensive coverage
- **Metadata Embedding**: Each test includes JSON metadata for automated processing
- **Graduated Complexity**: Tests increase in complexity across categories
- **Real-world Scenarios**: Advanced tests mirror actual web layouts

### Browser Reference Extraction (`extract_layout.js`)

Puppeteer-based extraction provides precise layout data:

```javascript
// Extracted layout data structure
{
  selector: "#container",
  layout: {
    x: 10.0, y: 20.0, width: 300.0, height: 200.0,
    contentWidth: 280.0, contentHeight: 180.0
  },
  computed: {
    display: "flex", flexDirection: "row",
    marginTop: 10.0, paddingLeft: 5.0,
    // ... complete CSS property set
  }
}
```

Key features:
- **Comprehensive Property Extraction**: All relevant CSS properties and computed values
- **Consistent Environment**: Controlled browser environment for reproducible results  
- **Element Hierarchy**: Maintains parent-child relationships for context
- **Precision**: Sub-pixel accuracy with consistent rounding

### C++ Validation Framework (`layout_test_framework.hpp/.cpp`)

The validation framework provides robust comparison capabilities:

```cpp
// Validation example
ValidationResult result = validator.validateElement(
    "#container", 
    referenceElement,   // Browser-extracted data
    actualElement       // Radiant-computed data
);

// Configurable tolerances
validator.setTolerancePixels(1.0);      // 1px tolerance
validator.setTolerancePercent(0.01);    // 1% tolerance
```

Key features:
- **Tolerance-Based Comparison**: Configurable pixel and percentage tolerances
- **Property-Specific Validation**: Individual property validation with detailed reporting
- **Comprehensive Coverage**: Validates layout, computed styles, and element relationships
- **Detailed Reporting**: Per-property comparison results with failure analysis

### GTest Integration (`test_layout_auto.cpp`)

Comprehensive test suites with multiple validation approaches:

```cpp
// Category-based testing
TEST_F(BasicLayoutTests, RunAllBasicTests) {
    auto results = testRunner_->runCategory("basic");
    EXPECT_GE(results.successRate(), 0.8);  // 80% success expected
}

// Parametric testing for individual cases
INSTANTIATE_TEST_SUITE_P(AllLayoutTests, LayoutTestCase, 
                        ::testing::ValuesIn(generateTestParameters()));
```

Key features:
- **Multiple Test Strategies**: Category-based, parametric, and feature-specific tests
- **Success Rate Validation**: Category-appropriate success rate expectations
- **Detailed Failure Analysis**: Property-level failure reporting for debugging
- **XML Reporting**: Compatible with CI/CD systems and reporting tools

## 📈 Success Metrics and Expectations

### Success Rate Targets
- **Basic Tests**: ≥80% success rate (fundamental functionality)
- **Intermediate Tests**: ≥70% success rate (complex scenarios)  
- **Advanced Tests**: ≥60% success rate (edge cases and real-world complexity)

### Tolerance Configuration
- **Pixel Tolerance**: 1.0px (accounts for sub-pixel rendering differences)
- **Percentage Tolerance**: 1% (relative measurement variations)
- **Property-Specific**: Certain properties may have adjusted tolerances

### Quality Gates
1. **No Test Execution Errors**: All tests must execute without framework errors
2. **Reference Data Completeness**: All test cases must have corresponding browser references
3. **Category Success Rates**: Each category must meet minimum success rate thresholds
4. **Regression Prevention**: New changes should not significantly decrease success rates

## 🔧 Configuration and Customization

### Tolerance Adjustment
```cpp
// In C++ validation code
validator.setTolerancePixels(2.0);       // Increase pixel tolerance
validator.setTolerancePercent(0.02);     // Increase percentage tolerance
validator.addIgnoreProperty("fontFamily"); // Ignore specific properties
```

### Test Generation Customization
```javascript
// In generate_tests.js
const customTestMatrix = {
    flexDirection: ['row', 'column'],
    customProperty: ['value1', 'value2']
};
```

### Browser Configuration
```javascript
// In extract_layout.js
const browser = await puppeteer.launch({
    headless: 'new',
    args: ['--font-render-hinting=none'],  // Consistent font rendering
    // ... additional browser configuration
});
```

## 🐛 Troubleshooting

### Common Issues

**1. "No reference data found"**
- Ensure browser extraction step completed successfully
- Check `reference/` directory for JSON files
- Run extraction manually: `npm run extract`

**2. "Compilation failed"** 
- Verify C++ dependencies: `pkg-config --exists json-c gtest`
- Install missing libraries (see Prerequisites)
- Check compiler version supports C++17

**3. "Test execution errors"**
- Verify test directory structure exists
- Ensure executable permissions: `chmod +x run_tests.sh`
- Run from correct directory: `test/layout/`

**4. "Puppeteer browser launch failed"**
- Install Puppeteer dependencies: `cd tools && npm install`
- On Linux, may need additional packages: `sudo apt-get install -y gconf-service libasound2-dev`

### Debug Mode
```bash
# Run with maximum verbosity
./run_tests.sh --verbose

# Run individual steps for debugging
./run_tests.sh --generate-only --verbose
./run_tests.sh --extract-only --verbose  
./run_tests.sh --validate-only --verbose
```

### Log Analysis
```bash
# Check extraction results
cat reports/extraction_summary.json

# View detailed test results  
cat reports/latest_summary.txt

# Analyze GTest XML output
xmllint --format reports/test_results.xml
```

## 🚀 Integration with Development Workflow

### Continuous Integration
```yaml
# Example CI configuration (.github/workflows/layout-tests.yml)
name: Layout Tests
on: [push, pull_request]
jobs:
  layout-validation:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Setup dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y libjson-c-dev libgtest-dev
      - name: Run layout tests
        run: |
          cd test/layout
          npm install
          ./run_tests.sh --verbose
      - name: Upload test results
        uses: actions/upload-artifact@v3
        with:
          name: layout-test-results
          path: test/layout/reports/
```

### Pre-commit Hooks
```bash
# Add to .git/hooks/pre-commit
#!/bin/bash
cd test/layout
./run_tests.sh --category basic --verbose
if [ $? -ne 0 ]; then
    echo "Layout tests failed. Commit aborted."
    exit 1
fi
```

### Development Integration
```bash
# Quick validation during development
npm run test:basic           # Fast basic validation
npm run validate            # Validate against existing references
npm run report              # Check current status
```

## 📋 Roadmap and Future Enhancements

### Planned Features
- **Performance Benchmarking**: Timing comparisons between Radiant and browser layout
- **Visual Regression Testing**: Pixel-perfect visual comparison capabilities  
- **CSS Grid Support**: Comprehensive CSS Grid layout testing
- **Mobile Layout Testing**: Responsive design and mobile-specific layout validation
- **Memory Usage Analysis**: Layout algorithm memory consumption validation

### Extensibility Points
- **Custom Test Generators**: Plugin system for domain-specific test generation
- **Alternative Browsers**: Firefox and Safari reference extraction
- **Layout Algorithm Variants**: Testing different layout algorithm implementations
- **Real-world Test Cases**: Integration with actual website layout scenarios

## 🤝 Contributing

### Adding New Test Cases
1. Extend test matrices in `tools/generate_tests.js`
2. Add new categories or complexity levels
3. Update success rate expectations in `test_layout_auto.cpp`

### Extending Validation Framework
1. Add new property comparisons in `layout_test_framework.cpp`
2. Implement custom tolerance logic for specific properties
3. Add new validation metrics and reporting

### Browser Support
1. Extend `extract_layout.js` for alternative browsers
2. Add browser-specific tolerance configurations
3. Implement cross-browser reference comparison

---

## 📄 License

