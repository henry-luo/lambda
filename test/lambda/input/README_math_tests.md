# Math Parser Test Documentation

## Overview
The math parser tests have been consolidated into two essential tests that cover all functionality comprehensively while eliminating redundancy.

## Test Structure

### 1. Basic Test (`test_math_basic.ls`)
**Purpose**: Tests essential math parser functionality with minimal complexity
**Coverage**:
- Basic matrix environments (matrix, pmatrix)
- Legacy API compatibility (simple string type parameter)
- Basic LaTeX commands (frac, sqrt)
- Flavor support verification

**Data Files**:
- `matrix_basic.txt` - Simple matrix environment
- `matrix_paren.txt` - Parentheses matrix environment  
- `math_simple.txt` - Basic math expression (x^2 + y_1)
- `math_latex_basic.txt` - Basic LaTeX commands (\frac{a}{b} + \sqrt{c})

### 2. Comprehensive Test (`test_math_complete.ls`)
**Purpose**: Tests advanced features, edge cases, and error handling
**Coverage**:
- All matrix environment types (matrix, pmatrix, bmatrix)
- Complex mathematical expressions with multiple operators
- Mixed syntax combining different LaTeX features
- Greek letters and mathematical symbols
- Advanced functions (limits, integrals, etc.)
- Flavor fallback testing (basic vs latex)
- Error handling with malformed input

**Data Files**:
- `matrix_all_types.txt` - All three matrix environment types
- `math_complex.txt` - Complex expressions with fractions, sums, integrals
- `math_mixed_syntax.txt` - Mixed LaTeX command and environment syntax
- `math_greek_symbols.txt` - Greek letters and mathematical symbols
- `math_advanced_functions.txt` - Advanced mathematical functions
- `math_malformed_matrix.txt` - Malformed matrix for error testing

## Removed Redundant Files

### Test Scripts (Removed)
- `test_matrix_files.ls` - Replaced by basic test
- `test_matrix_direct.ls` - Functionality merged into comprehensive test
- `test_matrix_environments.ls` - Replaced by comprehensive test
- `test_math_parser.ls` - Functionality distributed across both tests

### Data Files (Removed)
- `test_matrix_simple.txt`, `test_matrix_pmatrix.txt`, `test_matrix_bmatrix.txt` - Replaced by optimized versions
- `test_math_*.txt` (multiple files) - Consolidated into focused test data
- `test_direct_math.txt`, `test_latex_math.tex`, `test_markdown_math.md` - No longer needed

## Test Execution

Run the basic test:
```bash
./lambda.exe test/lambda/input/test_math_basic.ls
```

Run the comprehensive test:
```bash
./lambda.exe test/lambda/input/test_math_complete.ls
```

## Expected Results

### Basic Test Output
- Parsed matrix environments showing proper structure
- Legacy API compatibility confirmed
- Basic LaTeX commands working correctly
- Success message: "✅ Basic math parser functionality working!"

### Comprehensive Test Output  
- All matrix types parsed correctly
- Complex expressions handled appropriately (some may show ERROR for unsupported features)
- Greek symbols and advanced functions processed
- Flavor fallback working (basic flavor for simple expressions)
- Error handling demonstrated with malformed input
- Success message: "✅ Comprehensive math parser test completed!"

## Notes
- The math parser supports LaTeX flavor with matrix environments as the primary advanced feature
- Basic flavor provides fallback for simple mathematical expressions
- Error handling gracefully manages malformed input while still attempting to parse valid portions
- The consolidated tests provide complete coverage while being maintainable and focused
