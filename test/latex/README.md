# LaTeX Math Test Framework

Multi-layered semantic comparison framework for LaTeX math typesetting in Lambda.

## Overview

This framework compares Lambda's LaTeX math output against multiple references at three abstraction levels:

1. **AST Layer (50% weight)**: Structural/semantic correctness via MathLive
2. **HTML Layer (40% weight)**: Visual representation via MathLive + KaTeX cross-reference
3. **DVI Layer (10% weight)**: Precise typographic correctness via pdfTeX

## Quick Start

```bash
# Install dependencies (first time only)
make setup-math-tests

# Run all tests
make test-math

# Run baseline tests (DVI must pass 100%)
make test-math-baseline

# Run extended tests (semantic comparison)
make test-math-extended

# Run tests for specific feature group
make test-math-group group=fracs

# Run single test with verbose output
make test-math-single test=fracs_basic.json

# Generate reference files
make generate-math-references
```

## Directory Structure

```
test/latex/
├── test_math_comparison.js      # Main test runner
├── mathlive_reference.mjs       # MathLive AST/HTML generator
├── katex_reference.mjs          # KaTeX HTML generator
├── comparators/
│   ├── ast_comparator.js        # AST semantic comparison
│   ├── html_comparator.js       # HTML structure comparison
│   └── dvi_comparator.js        # DVI output comparison
├── baseline/                    # Baseline tests (.tex files)
│   ├── fracs_basic.tex
│   ├── sqrt_basic.tex
│   └── scripts_basic.tex
├── math/                        # Extended test files (.json)
│   ├── fracs_basic.json
│   ├── fracs_nested.json
│   ├── scripts_basic.json
│   └── complex_calculus.json
├── math-ast/                    # Additional AST test files
└── reference/                   # Reference output files
    ├── *.ast.json               # MathLive AST references
    ├── *.mathlive.html          # MathLive HTML references
    ├── *.katex.html             # KaTeX HTML references
    └── *.dvi                    # pdfTeX DVI references
```

## Test Categories

### Baseline Tests (`baseline/`)

- **Format**: `.tex` files containing simple LaTeX documents
- **Requirement**: DVI output must match pdfTeX at 100%
- **Purpose**: Core functionality and regression prevention
- **Command**: `make test-math-baseline`

Example:
```tex
\documentclass{article}
\begin{document}
\[
\frac{a}{b}
\]
\end{document}
```

### Extended Tests (`math/` and `math-ast/`)

- **Format**: `.json` files with multiple expressions
- **Scoring**: Weighted composite (50% AST, 40% HTML, 10% DVI)
- **Purpose**: Comprehensive coverage and quality tracking
- **Command**: `make test-math-extended`

Example JSON format:
```json
{
    "category": "fracs",
    "description": "Fraction tests",
    "expressions": [
        {
            "index": 0,
            "latex": "\\frac{a}{b}",
            "description": "Simple fraction"
        }
    ]
}
```

## Feature Groups

Tests are organized by mathematical feature for targeted testing:

| Prefix | Feature Group | Tests |
|--------|---------------|-------|
| `accents_` | Accents & decorations | `accents_basic.json` |
| `arrows_` | Arrow symbols | `arrows_basic.json` |
| `bigops_` | Big operators | `bigops_sum.json`, `bigops_integral.json` |
| `choose_` | Binomial coefficients | `choose_basic.json` |
| `delims_` | Delimiters | `delims_basic.json` |
| `fonts_` | Font styles | `fonts_basic.json` |
| `fracs_` | Fractions | `fracs_basic.json`, `fracs_nested.json` |
| `greek_` | Greek letters | `greek_lower.json` |
| `matrix_` | Matrices & arrays | `matrix_basic.json` |
| `not_` | Negations | `not_basic.json` |
| `operators_` | Binary/relation ops | `operators_basic.json` |
| `scripts_` | Sub/superscripts | `scripts_basic.json` |
| `spacing_` | Spacing & phantoms | `spacing_basic.json` |
| `sqrt_` | Square roots | `sqrt_basic.json` |
| `complex_` | Complex formulas | `complex_calculus.json` |

## CLI Usage

### Node.js Direct

```bash
# Run all tests
node test_math_comparison.js

# Run specific suite
node test_math_comparison.js --suite baseline
node test_math_comparison.js --suite extended

# Run single test
node test_math_comparison.js --test fracs_basic.json --verbose

# Run feature group
node test_math_comparison.js --group fracs

# Specific comparison layer
node test_math_comparison.js --compare ast
node test_math_comparison.js --compare html
node test_math_comparison.js --compare dvi

# JSON output for CI
node test_math_comparison.js --json

# Adjust thresholds
node test_math_comparison.js --threshold 85
node test_math_comparison.js --tolerance 1.0  # DVI position tolerance in points
```

### NPM Scripts

```bash
cd test/latex

npm test                    # Run all tests
npm run test:baseline       # Baseline tests only
npm run test:extended       # Extended tests only
npm run test:verbose        # Verbose output
npm run test:json           # JSON output
npm run generate:mathlive   # Generate MathLive references
npm run generate:katex      # Generate KaTeX references
npm run generate:ast        # Generate AST references
```

## Lambda Integration

The test runner calls Lambda's math typesetter:

```bash
./lambda.exe math "\\frac{a}{b}" \
    --output-ast output.ast.json \
    --output-html output.html \
    --output-dvi output.dvi
```

Lambda must implement this command to:
1. Parse the LaTeX math expression
2. Generate an AST (JSON format compatible with MathLive structure)
3. Render HTML output
4. Generate DVI output

## Scoring System

### Per-Test Score

```
Overall Score = (AST × 50%) + (HTML × 40%) + (DVI × 10%)
```

- **AST**: Node-level comparison with normalization
- **HTML**: Element structure comparison (best of MathLive/KaTeX)
- **DVI**: Glyph position comparison with tolerance

### Pass Criteria

- **Baseline tests**: 100% DVI match required
- **Extended tests**: ≥80% overall score (configurable with `--threshold`)

### Success Metrics

| Metric | Target |
|--------|--------|
| Baseline pass rate | 100% |
| Extended average | ≥85% |
| AST component | ≥90% |
| HTML component | ≥85% |
| DVI component | ≥75% |

## Output Format

```
================================================================================
📊 LaTeX Math Test Results
================================================================================

📂 Baseline Tests (DVI must pass 100%)
--------------------------------------------------------------------------------
  ✅ fracs_basic.tex            DVI: 100.0%
  ✅ sqrt_basic.tex             DVI: 100.0%
  ✅ scripts_basic.tex          DVI: 100.0%
--------------------------------------------------------------------------------
  Baseline: 3/3 passed (100%)

📂 Extended Tests by Feature Group
--------------------------------------------------------------------------------
  📁 fracs (3 tests)
     ✅ fracs_basic.json         AST:  100%  HTML:  100%  DVI:  100%  →  100.0%
     ✅ fracs_nested.json        AST:   95%  HTML:   92%  DVI:   88%  →   93.3%
     Group Average: 96.7%

  📁 scripts (2 tests)
     ✅ scripts_basic.json       AST:  100%  HTML:  100%  DVI:  100%  →  100.0%
     Group Average: 100.0%
--------------------------------------------------------------------------------

================================================================================
📈 OVERALL SUMMARY
================================================================================
  Total Tests: 5
  Passed: 5 (100%)
  Failed: 0 (0%)

  Component Averages:
    AST:  98.0%  (target: 90%)
    HTML: 96.0%  (target: 85%)
    DVI:  93.6%  (target: 75%)

  Weighted Average Score: 96.7%
================================================================================
```

## Development Workflow

### Adding a New Test

1. **For baseline tests** (`.tex` files):
   ```bash
   # Create test file
   cat > test/latex/baseline/new_test.tex << 'EOF'
   \documentclass{article}
   \begin{document}
   \[
   x^2 + y^2 = z^2
   \]
   \end{document}
   EOF

   # Generate reference DVI
   cd test/latex/baseline
   pdflatex -output-directory=../reference new_test.tex
   ```

2. **For extended tests** (`.json` files):
   ```bash
   # Create test file
   cat > test/latex/math/feature_test.json << 'EOF'
   {
       "category": "feature",
       "description": "Feature description",
       "expressions": [
           {
               "index": 0,
               "latex": "\\command{x}",
               "description": "Test description"
           }
       ]
   }
   EOF

   # Generate references
   cd test/latex
   npm run generate:mathlive
   npm run generate:katex
   ```

### Debugging Failed Tests

1. **Run with verbose output**:
   ```bash
   make test-math-single test=failing_test.json
   ```

2. **Check specific comparison layer**:
   ```bash
   node test_math_comparison.js --test failing_test.json --compare ast -v
   ```

3. **Examine differences**:
   - AST: Shows node-level differences with paths
   - HTML: Shows element structure mismatches
   - DVI: Shows glyph position differences

## CI Integration

```bash
# Run tests with JSON output
make test-math > results.txt
cd test/latex && npm test -- --json > results.json

# Check exit code
if [ $? -ne 0 ]; then
    echo "Tests failed"
    exit 1
fi
```

## Dependencies

- **Node.js** (v16+): Test runner and reference generators
- **npm packages**:
  - `jsdom`: HTML parsing
  - `puppeteer`: Browser automation (for MathLive)
- **Lambda**: Must implement `math` command with `--output-ast`, `--output-html`, `--output-dvi` options
- **pdfTeX**: For baseline reference DVIs (optional)

## Future Enhancements

- Visual regression testing (PNG comparison)
- Symbol coverage tracking
- Interactive debug mode
- Performance benchmarking
- Real-world document test extraction

## References

- [MathLive](https://cortexjs.io/mathlive/) - AST and HTML reference
- [KaTeX](https://katex.org/) - HTML cross-reference
- [pdfTeX](https://www.tug.org/applications/pdftex/) - DVI reference
- [LaTeX Math Commands](https://en.wikibooks.org/wiki/LaTeX/Mathematics)
