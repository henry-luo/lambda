# Math Layout Test Suite

This directory contains tests for Lambda's math layout engine, using test fixtures extracted from MathLive.

## Directory Structure

```
test/math/
├── README.md                  # This file
├── math_fixture_loader.h      # C++ fixture loader header
├── math_fixture_loader.cpp    # C++ fixture loader implementation
├── test_math_layout.cpp       # GTest test suite
└── fixtures/                  # JSON test fixtures (generated)
    ├── all_tests.json         # Combined fixture file
    ├── fractions.json         # Fraction tests
    ├── subscripts.json        # Subscript/superscript tests
    ├── radicals.json          # Radical (sqrt) tests
    ├── accents.json           # Accent tests
    ├── delimiters.json        # Delimiter tests
    ├── operators.json         # Big operator tests
    └── spacing.json           # Spacing tests
```

## Generating Fixtures

Run the extraction script to generate fixtures from MathLive:

```bash
python3 utils/extract_mathlive_tests.py
```

This extracts test cases from:
- `mathlive/test/markup.test.ts` - Jest unit tests
- `mathlive/test/static/index.html` - Visual test cases

## Running Tests

```bash
# Build the test executable
make build-test

# Run all math layout tests
./test/test_math_layout.exe

# Run specific category
./test/test_math_layout.exe --gtest_filter=Fractions*

# Run with verbose output
./test/test_math_layout.exe --gtest_filter=* --gtest_print_time=1
```

## Test Categories

| Category | Description | Layout Function |
|----------|-------------|-----------------|
| Fractions | `\frac`, `\binom`, etc. | `layout_fraction()` |
| Subscripts | `x^2`, `x_i`, `x^a_b` | `layout_subsup()` |
| Radicals | `\sqrt`, `\sqrt[n]` | `layout_radical()` |
| Accents | `\hat`, `\vec`, `\bar` | `layout_accent()` |
| Delimiters | `\left(`, `\right)` | `layout_delimiter()` |
| Operators | `\sum`, `\int`, `\lim` | `layout_big_operator()` |
| Spacing | `\quad`, `\,`, inter-atom | `apply_inter_box_spacing()` |

## Fixture Format

Each JSON fixture file has this structure:

```json
{
  "category": "fractions",
  "source": "mathlive",
  "tests": [
    {
      "id": 1,
      "latex": "\\frac{1}{2}",
      "description": "Simple fraction",
      "source": "mathlive/markup.test.ts"
    }
  ]
}
```

## Adding New Tests

1. Add LaTeX test cases to the appropriate JSON fixture file
2. Rebuild and run tests

Or modify `utils/extract_mathlive_tests.py` to extract additional test cases.

## Test Approaches

### 1. Smoke Tests (Current)
Verify that LaTeX parses without crashing and produces valid MathBox trees.

### 2. Structure Validation
Verify that specific LaTeX constructs produce expected box tree structures:
- Fractions → vbox with numerator, rule, denominator
- Subscripts → hbox with base and shifted script

### 3. Dimension Validation
Compare box dimensions (height, depth, width) against expected ranges based on MathLive measurements.

### 4. Visual Comparison (Future)
Render math to SVG and compare against reference images.

## Reference

- [MathLive](https://github.com/arnog/mathlive) - Source of test fixtures
- [TeXBook](https://www.amazon.com/TeXbook-Donald-Knuth/dp/0201134489) - Math typesetting algorithms
- [Math Layout Proposal](../../doc/Math_Test_Proposal.md) - Original proposal document
