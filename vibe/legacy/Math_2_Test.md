# Proposal: Reusing MathLive Test Fixtures for Math Layout Testing

## Overview

This document proposes how to leverage MathLive's comprehensive test fixtures to validate Lambda's new math layout engine (Phase 3 and 4 implementation).

## MathLive Test Resources Available

### 1. Unit Test Fixtures (`mathlive/test/`)

**LaTeX Parsing Tests** (`latex.test.ts`):
- 180+ test cases covering LaTeX parsing
- Categories: basic parsing, characters, expansion primitives, arguments, infix commands
- Format: Jest `test.each()` with snapshot comparison

**Markup Generation Tests** (`markup.test.ts`):
- 470+ test cases for HTML markup generation
- Categories: fractions, accents, delimiters, spacing, environments, surds, etc.
- Produces detailed HTML snapshots with layout measurements

**Snapshot Files** (`__snapshots__/`):
- `markup.test.ts.snap`: 1,468 lines of expected HTML output with precise measurements
- Contains height, depth, width values for validation

### 2. Visual Test Fixtures (`mathlive/test/static/`)

**Comprehensive Test Page** (`index.html`):
- 1,600+ lines of structured test cases
- Categories organized by feature:
  - Ordinary Symbols
  - Math Styles (scriptstyle, displaystyle, etc.)
  - Fractions and Generalized Fractions
  - Delimiters and Sizing
  - Accents and Over/Under
  - Spacing and Layout
  - Environments (matrices, arrays, cases)
  - Big Operators with limits
  - Radicals

**Reference Images** (`test/static/*.gif/*.png`):
- 35 reference images for visual comparison
- Covers: fractions, accents, operators, styling, spacing, delimiters

### 3. Key Test Categories Relevant to Our Layout Engine

| Category | MathLive Location | Relevance |
|----------|-------------------|-----------|
| Fractions | `markup.test.ts` FRACTIONS | `layout_fraction()` |
| Subscripts/Superscripts | `markup.test.ts` SUPERSCRIPT/SUBSCRIPT | `layout_subsup()` |
| Radicals | `markup.test.ts` SURDS | `layout_radical()` |
| Accents | `markup.test.ts` ACCENTS | `layout_accent()` |
| Delimiters | `markup.test.ts` LEFT/RIGHT | `layout_delimiter()` |
| Big Operators | `static/index.html` Big Operators | `layout_big_operator()` |
| Spacing | `markup.test.ts` SPACING AND KERN | `apply_inter_box_spacing()` |
| Math Styles | `static/index.html` Math Styles | `MathContext` style transitions |

## Proposed Integration Strategy

### Phase 1: Extract LaTeX Test Cases

Create a Python script to extract test cases from MathLive:

```python
# utils/extract_mathlive_tests.py

def extract_from_markup_test():
    """Extract LaTeX strings from markup.test.ts"""
    # Categories to extract:
    # - FRACTIONS, SURDS, ACCENTS, BINARY OPERATORS
    # - SUPERSCRIPT/SUBSCRIPT, LEFT/RIGHT, ENVIRONMENTS
    pass

def extract_from_static_html():
    """Extract test cases from static/index.html"""
    # Parse TESTING_SAMPLES JavaScript object
    pass
```

### Phase 2: Create Math Layout Test Fixtures

Create fixture files in Lambda's format:

```
test/math/
├── fixtures/
│   ├── fractions.json       # From MathLive FRACTIONS tests
│   ├── subscripts.json      # From MathLive SUPERSCRIPT/SUBSCRIPT
│   ├── radicals.json        # From MathLive SURDS
│   ├── accents.json         # From MathLive ACCENTS
│   ├── delimiters.json      # From MathLive LEFT/RIGHT
│   ├── operators.json       # From MathLive big operators
│   └── spacing.json         # From MathLive SPACING tests
├── fixture_loader.cpp       # Load JSON fixtures
└── test_math_layout.cpp     # GTest runner
```

**Fixture Format**:
```json
{
  "category": "fractions",
  "source": "mathlive",
  "tests": [
    {
      "id": 1,
      "latex": "\\frac{1}{2}",
      "description": "Simple fraction",
      "expected": {
        "height_range": [0.6, 0.8],
        "depth_range": [0.2, 0.4],
        "width_range": [0.4, 0.6]
      }
    }
  ]
}
```

### Phase 3: Test Implementation

**Option A: Box Dimension Validation**
```cpp
// test/math/test_math_layout.cpp

TEST_P(MathLayoutTest, BoxDimensions) {
    const auto& fixture = GetParam();

    // Parse LaTeX to MathNode tree
    Item math_node = parse_latex_math(fixture.latex);

    // Create ViewMath and run layout
    ViewMath* view = create_math_view(math_node, 16.0f);  // 16pt base size
    layout_math_view(view, &ctx);

    // Validate box dimensions
    MathBox* root = view->math_box;
    EXPECT_GE(root->height, fixture.expected.height_min);
    EXPECT_LE(root->height, fixture.expected.height_max);
    EXPECT_GE(root->depth, fixture.expected.depth_min);
    EXPECT_LE(root->depth, fixture.expected.depth_max);
}
```

**Option B: Visual Comparison (SVG/PNG Output)**
```cpp
TEST_P(MathVisualTest, RenderedOutput) {
    const auto& fixture = GetParam();

    // Parse and layout
    Item math_node = parse_latex_math(fixture.latex);
    ViewMath* view = create_math_view(math_node, 16.0f);
    layout_math_view(view, &ctx);

    // Render to SVG
    std::string svg = render_math_to_svg(view);

    // Compare with reference (or generate baseline)
    std::string reference_path = fixture.reference_svg;
    if (file_exists(reference_path)) {
        EXPECT_TRUE(compare_svg(svg, read_file(reference_path)));
    } else {
        write_file(reference_path, svg);  // Generate baseline
    }
}
```

**Option C: Structure Validation**
```cpp
TEST_P(MathStructureTest, BoxTree) {
    const auto& fixture = GetParam();

    Item math_node = parse_latex_math(fixture.latex);
    ViewMath* view = create_math_view(math_node, 16.0f);
    layout_math_view(view, &ctx);

    // Validate tree structure
    MathBox* root = view->math_box;

    // For fraction: expect vbox with 3 children (num, bar, den)
    if (fixture.category == "fraction") {
        EXPECT_EQ(root->content_type, MATH_BOX_VBOX);
        EXPECT_EQ(count_children(root), 3);
    }
}
```

### Phase 4: Specific Test Cases from MathLive

#### Fractions (from `markup.test.ts` FRACTIONS)
```
\\frac57
\\frac {5} {7}
\\frac {\\frac57} {\\frac37}
\\binom{n}{k}
\\dbinom{n}{k}
\\tbinom{n}{k}
```

#### Subscripts/Superscripts (from `markup.test.ts`)
```
-1-\\frac56-1-x^{2-\\frac34}
\\left(x+1\\right)^2
x^n+y^n
a^b_c
```

#### Radicals (from `markup.test.ts` SURDS)
```
\\sqrt5
\\sqrt{}
\\sqrt[3]{5}
\\sqrt[3]5
ax^2+bx+c = a \\left( x - \\frac{-b + \\sqrt {b^2-4ac}}{2a} \\right)
```

#### Accents (from `markup.test.ts` ACCENTS)
```
\\vec{x}
\\acute{x}
\\grave{x}
\\dot{x}
\\ddot{x}
\\tilde{x}
\\bar{x}
\\hat{x}
\\widehat{xyz}
```

#### Delimiters (from `markup.test.ts` LEFT/RIGHT)
```
\\left( x + 1 \\right)
\\left( x \\frac{\\frac34}{\\frac57} \\right)
\\left\\lfloor x \\right\\rfloor
\\left\\langle x \\right\\rangle
\\left| x \\right|
\\left\\| x \\right\\|
```

#### Big Operators (from `static/index.html`)
```
\\sum_{i=0}^{n} x_i
\\prod_{i=1}^{n}
\\int_0^\\infty
\\oint_C
\\lim_{x \\to 0}
```

#### Spacing Tests (from `markup.test.ts` SPACING)
```
a+{}b+c
a\\hskip 3em b
a\\kern 3em b
+-a+b=c+-d=x^{2-\\frac{1}{2}}
```

## Implementation Plan

### Step 1: Create Extraction Script
```bash
python3 utils/extract_mathlive_tests.py \
    --input mathlive/test/markup.test.ts \
    --input mathlive/test/static/index.html \
    --output test/math/fixtures/
```

### Step 2: Create Test Harness
```
test/math/
├── CMakeLists.txt           # Build configuration
├── fixture_loader.cpp/.h    # JSON fixture loader
├── math_test_utils.cpp/.h   # Helper functions
├── test_math_layout.cpp     # Main test file
└── fixtures/                # Extracted fixtures
```

### Step 3: Add to Build System
```lua
-- In build_lambda_config.json or premake5.mac.lua
project "test_math_layout"
    kind "ConsoleApp"
    files {
        "test/math/**.cpp",
        "test/math/**.h"
    }
```

### Step 4: CI Integration
```yaml
# In GitHub Actions
- name: Run Math Layout Tests
  run: |
    ./test/test_math_layout.exe --gtest_output=xml:test_results/
```

## Metrics for Success

1. **Coverage**: Test all layout functions
   - `layout_symbol` ✓
   - `layout_fraction` ✓
   - `layout_subsup` ✓
   - `layout_radical` ✓
   - `layout_delimiter` ✓
   - `layout_accent` ✓
   - `layout_big_operator` ✓

2. **Baseline Compatibility**: Match MathLive measurements within tolerance
   - Height: ±5%
   - Depth: ±5%
   - Width: ±10%

3. **Visual Quality**: Reference image comparison
   - Generate SVG for each test case
   - Compare with MathLive reference images

## Notes

- MathLive uses CSS-based layout with `em` units; our engine uses `pt` and FreeType metrics
- Conversion factor: 1em ≈ font_size in points at base size
- MathLive height values from snapshots can be used as proportional references
- Focus on relative proportions rather than absolute pixel values

## Appendix: Sample Extraction from MathLive Snapshot

From `markup.test.ts.snap`:
```
exports[`FRACTIONS 0/ \\frac57 renders correctly 1`] = `
[
  "<span class=\"ML__latex\"><span class=\"ML__strut\" style=\"height:1.19em\">...
  "no-error",
]
`;
```

Extract: LaTeX=`\frac57`, height=1.19em

This can be converted to our fixture format with expected height proportional to font size.
