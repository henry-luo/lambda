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
- All matrix environment types (matrix_all_types.txt)
- Complex mathematical expressions with multiple operators
- Mixed syntax combining different LaTeX features
- Greek letters and mathematical symbols
- Advanced functions (limits, integrals, etc.)
- Multiple flavor support (LaTeX, basic, Typst, ASCII)
- Error handling with malformed input

**Data Files**:
- `matrix_all_types.txt` - All matrix environment types
- `math_complex.txt` - Complex expressions with fractions, sums, integrals
- `math_mixed_syntax.txt` - Mixed LaTeX command and environment syntax
- `math_greek_symbols.txt` - Greek letters and mathematical symbols
- `math_advanced_functions.txt` - Advanced mathematical functions
- `math_malformed_matrix.txt` - Malformed matrix for error testing

## Removed Redundant Files

### Test Scripts (Removed)
- `test_advanced_math_parser.ls` - Advanced features merged into comprehensive test
- `test_simple_math.ls` - Basic functionality merged into basic test  
- `test_comprehensive_math.ls` - Replaced by test_math_complete.ls
- `input_math.ls` - Basic input testing merged into basic test
- `input_math_enhanced.ls` - Enhanced features merged into comprehensive test
- `final_math_test.ls` - Multi-flavor testing merged into comprehensive test

### Data Files (Removed)
All necessary data files are preserved in the consolidated structure. Redundant files were removed during the consolidation process.

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
- Multiple flavor support demonstrated (LaTeX, basic, Typst, ASCII)
- Error handling demonstrated with malformed input
- Success message: "✅ Comprehensive math parser test completed!"

## Notes
- The math parser supports LaTeX flavor with matrix environments as the primary advanced feature
- Basic flavor provides fallback for simple mathematical expressions
- Error handling gracefully manages malformed input while still attempting to parse valid portions
- The consolidated tests provide complete coverage while being maintainable and focused
