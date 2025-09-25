# Radiant Flex Layout Test Suite

This directory contains comprehensive unit tests for the Radiant flex layout system using Google Test (GTest) framework.

## Test Structure

### 1. Basic Flex Layout Tests (`test_radiant_flex_gtest.cpp`)
Tests fundamental flex layout functionality:
- Flex container initialization and properties
- Flex item creation and property management
- Basic row and column layouts
- Flex-grow, flex-shrink, and flex-basis behavior
- Justify-content and align-items alignment
- Flex-wrap behavior and gap properties
- Order property and visual reordering
- Writing modes and text direction support
- Nested flex containers
- Edge cases and error conditions

### 2. Flex Algorithm Tests (`test_radiant_flex_algorithm_gtest.cpp`)
Tests the core flexbox layout algorithms:
- Flex item collection and filtering
- Item ordering by CSS order property
- Flex line creation (single and multiple lines)
- Flexible length resolution (growing and shrinking)
- Main axis alignment algorithms (justify-content)
- Cross axis alignment algorithms (align-items, align-self)
- Baseline alignment calculations
- Gap handling in layout calculations
- Flex-basis calculations with different units
- Min/max width constraint handling
- Writing mode impact on main/cross axes
- Text direction impact on alignment
- Complex layout scenarios
- Algorithm performance with many items

### 3. Integration Tests (`test_radiant_flex_integration_gtest.cpp`)
Tests real-world layout scenarios with CSS-like properties:
- Basic flexbox layout matching CSS specifications
- Grid-like layouts using nested flexboxes
- Responsive flexbox layouts with flex-wrap
- Navigation menu layouts
- Card layout systems
- Form layout systems
- Sidebar layout systems
- Complex nested dashboard layouts
- Flexbox with CSS transforms
- Memory management in complex layouts

## Running the Tests

### Prerequisites
- Google Test (GTest) library installed
- Radiant layout engine built
- C++17 compatible compiler

### Build and Run All Tests
```bash
# From project root directory
./test/run_flex_tests.sh --build
```

### Run Specific Test Suite
```bash
# Run only basic tests
./test/run_flex_tests.sh --test basic

# Run only algorithm tests
./test/run_flex_tests.sh --test algorithm

# Run only integration tests
./test/run_flex_tests.sh --test integration
```

### Manual Build and Run
```bash
# Build tests using make (if Makefile exists)
make test-radiant

# Or build individual tests
clang++ -std=c++17 -I. -Iradiant -Ilib/mem-pool/include -Ilexbor/source \
    test/test_radiant_flex_gtest.cpp -lgtest -lgtest_main -o test_radiant_flex_gtest.exe

# Run individual test
./test_radiant_flex_gtest.exe
```

## Test Configuration

The tests are configured in `build_lambda_config.json` under the "radiant" test suite:

```json
{
    "suite": "radiant",
    "name": "🎨 Radiant Layout Engine Tests",
    "tests": [
        {
            "source": "test/test_radiant_flex_gtest.cpp",
            "name": "📦 Radiant Flex Layout Tests (GTest)",
            "dependencies": ["radiant"],
            "libraries": ["gtest", "gtest_main"]
        }
    ]
}
```

## Test Output

Tests generate XML reports in the `test_output/` directory:
- `basic_results.xml` - Basic flex layout test results
- `algorithm_results.xml` - Algorithm test results  
- `integration_results.xml` - Integration test results

## Test Coverage

The test suite covers:

### CSS Flexbox Properties
- `display: flex`
- `flex-direction: row | column | row-reverse | column-reverse`
- `flex-wrap: nowrap | wrap | wrap-reverse`
- `justify-content: flex-start | flex-end | center | space-between | space-around | space-evenly`
- `align-items: flex-start | flex-end | center | stretch | baseline`
- `align-content: flex-start | flex-end | center | stretch | space-between | space-around`
- `gap`, `row-gap`, `column-gap`
- `flex-grow`, `flex-shrink`, `flex-basis`
- `flex` shorthand property
- `align-self` override
- `order` property

### Layout Scenarios
- Single line layouts
- Multi-line wrapped layouts
- Nested flex containers
- Mixed flex and non-flex content
- Responsive designs
- Complex dashboard layouts
- Navigation systems
- Form layouts
- Card grids

### Edge Cases
- Empty containers
- Single item containers
- Zero-sized containers
- Negative flex values
- Large numbers of items
- Memory management
- Performance with complex layouts

## Integration with Radiant

These tests are designed to work with the new integrated flex layout system described in `/vibe/Flexbox.md`. They test:

1. **View Hierarchy Integration**: Flex properties embedded in `ViewBlock` structures
2. **Memory Pool Usage**: Efficient allocation using existing memory management
3. **Layout Context**: Integration with `LayoutContext` and font/image handling
4. **CSS Compliance**: Full CSS Flexbox specification compliance
5. **Performance**: Minimal data copying and temporary allocations

## Future Extensions

The test framework is designed to be extensible for:
- CSS Grid layout tests (when implemented)
- Additional CSS layout modes
- Performance benchmarking
- Visual regression testing
- Cross-platform compatibility testing

## Debugging Tests

For debugging failed tests:

```bash
# Run with verbose output
./test/run_flex_tests.sh --verbose

# Run specific test with GTest filters
./test_radiant_flex_gtest.exe --gtest_filter="FlexLayoutTest.BasicRowLayout"

# Run with debugging information
gdb ./test_radiant_flex_gtest.exe
```

## Contributing

When adding new flex layout features:

1. Add corresponding unit tests to the appropriate test file
2. Update this README with new test coverage
3. Ensure all existing tests continue to pass
4. Add integration tests for complex scenarios
5. Update the build configuration if needed

## Test Philosophy

These tests follow the principle of testing behavior, not implementation:
- Tests verify correct layout results, not internal algorithm steps
- Tests use CSS-like property names and values for clarity
- Tests cover real-world usage scenarios
- Tests are designed to catch regressions during refactoring
- Tests serve as documentation of expected behavior
