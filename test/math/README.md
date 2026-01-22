# Math Layout Test Suite

This directory contains unit tests for Lambda's math typesetting API (`typeset_latex_math()`).

> **Note:** Math fixtures have been consolidated under `test/latex/fixtures/math/mathlive/`.
> The JSON fixtures here are the source, converted to .tex files by `utils/convert_math_fixtures.js`.

## Directory Structure

```
test/math/
├── README.md                  # This file
├── math_fixture_loader.h      # C++ fixture loader header
├── math_fixture_loader.cpp    # C++ fixture loader implementation
├── test_math_layout.cpp       # GTest unit tests (internal API)
├── fixtures/                  # JSON test fixtures (source)
│   ├── all_tests.json         # Combined fixture file
│   ├── fractions.json         # Fraction tests
│   └── ...                    # Other categories
└── reference/                 # Reference DVI files for unit tests
```

## Consolidated Test Structure

Math tests exist at two levels:

| Test File | Level | What It Tests |
|-----------|-------|---------------|
| `test/math/test_math_layout.cpp` | Unit | `typeset_latex_math()` API directly |
| `test/test_latex_dvi_compare_gtest.cpp` | Integration | Full CLI pipeline (`./lambda.exe render`) |

The JSON fixtures in this directory have been converted to .tex files:
- Source: `test/math/fixtures/*.json` (201 test cases)
- Target: `test/latex/fixtures/math/mathlive/*.tex` (201 files)
- DVI refs: `test/latex/expected/math/mathlive/*.dvi` (196 files)

## Converting Fixtures

To regenerate .tex files from JSON fixtures:

```bash
node utils/convert_math_fixtures.js --clean
```

To regenerate DVI references:

```bash
node utils/generate_latex_refs.js --output-format=dvi --test=mathlive --force
```

## Running Tests

```bash
# Unit tests (internal API)
./test/test_math_layout.exe

# Integration tests (CLI pipeline)
./test/test_latex_dvi_compare_gtest.exe --gtest_filter="*Mathlive*"

# Run specific category
./test/test_math_layout.exe --gtest_filter=Fractions*
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
