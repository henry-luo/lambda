# ASCII Math Parser and Formatter Implementation Plan

## Project Overview

This document outlines the plan to implement a new standalone ASCII Math parser and formatter that is decoupled from the existing LaTeX Math implementation. The current approach of patching the LaTeX parser to support ASCII Math is error-prone and unstructured. The new implementation will be a clean, dedicated parser that references the official ASCII Math specification.

## References

1. **Official Documentation**: [ASCII Math Syntax](https://www1.chapman.edu/~jipsen/mathml/asciimathsyntax.html)
2. **Reference Implementation**: [ASCIIMathML.js](https://github.com/asciimath/asciimathml/blob/master/ASCIIMathML.js)
3. **Current LaTeX Implementation**: `./lambda/input/input-math.cpp` (lines ~4636 showing existing ASCII flavor support)

## Current State Analysis

### Existing Math System Architecture
- **Main Parser**: `./lambda/input/input-math.cpp` (5227 lines)
- **Formatter**: `./lambda/format/format-math.cpp` (2201 lines)
- **Current ASCII Support**: Patched into LaTeX parser with `MATH_FLAVOR_ASCII` enum
- **Test Coverage**: Comprehensive Lambda tests in `./test/lambda/input/test_math_*.ls`

### Problems with Current Approach
1. **Coupling**: ASCII math logic is interwoven with LaTeX parsing
2. **Complexity**: Single 5000+ line file handling multiple math flavors
3. **Maintenance**: Changes to one flavor can break others
4. **Clarity**: ASCII-specific logic scattered throughout LaTeX code
5. **Testing**: Hard to isolate ASCII-specific test cases

## Implementation Plan

### Phase 1: Foundation and Basic Parser (Week 1)

#### 1.1 Create Core Infrastructure
- **File**: `./lambda/input/input-math-ascii.cpp`
- **Purpose**: Standalone ASCII Math parser
- **Dependencies**: 
  - `input.h` and `input-common.h`
  - `../lambda.h` for data structures
  - `../../lib/log.h` for debugging

#### 1.2 Define ASCII Math Grammar Structure
```c
// ASCII Math specific token types
typedef enum {
    ASCII_TOKEN_IDENTIFIER,     // x, y, variable names
    ASCII_TOKEN_NUMBER,         // 123, 45.67, -8.9
    ASCII_TOKEN_OPERATOR,       // +, -, *, /
    ASCII_TOKEN_FUNCTION,       // sin, cos, log, sqrt
    ASCII_TOKEN_SYMBOL,         // alpha, beta, pi, infinity
    ASCII_TOKEN_RELATION,       // =, <, >, <=, >=, !=
    ASCII_TOKEN_GROUPING,       // (, ), [, ], {, }
    ASCII_TOKEN_SPECIAL,        // ^, _, /
    ASCII_TOKEN_TEXT,           // "quoted text"
    ASCII_TOKEN_EOF
} ASCIITokenType;

typedef struct {
    ASCIITokenType type;
    const char* start;
    size_t length;
    const char* unicode_output;  // Unicode equivalent for rendering
} ASCIIToken;
```

#### 1.3 Implement Core Tokenizer
- **Function**: `ascii_tokenize(const char* input, ASCIIToken** tokens)`
- **Logic**: Longest matching initial substring search (per spec)
- **Priority**: Constants table lookup before single character fallback

#### 1.4 Create ASCII Constants Table
Based on official spec, implement symbol mappings:
```c
typedef struct {
    const char* ascii_input;     // "alpha", "beta", "sum"
    const char* unicode_output;  // "α", "β", "∑"
    ASCIITokenType token_type;   // ASCII_TOKEN_SYMBOL
    bool is_function;           // true for sin, cos, etc.
} ASCIIConstant;
```

### Phase 2: Basic Expression Parsing (Week 1-2)

#### 2.1 Implement Grammar Parser
Following the BNF grammar from the spec:
```
c ::= [A-z] | numbers | greek letters | other constant symbols
u ::= 'sqrt' | 'text' | 'bb' | other unary symbols
b ::= 'frac' | 'root' | 'stackrel' binary symbols  
l ::= ( | [ | { | (: | {:          left brackets
r ::= ) | ] | } | :) | :}          right brackets
S ::= c | lEr | uS | bSS | "any"   simple expression
E ::= SE | S/S | S_S | S^S | S_S^S  expression
```

#### 2.2 Core Parsing Functions
- `parse_simple_expression()` - Handle constants, bracketed expressions, unary operations
- `parse_expression()` - Handle fractions, subscripts, superscripts
- `parse_binary_operation()` - Handle frac, root, stackrel
- `parse_matrix()` - Handle matrix notation with commas and brackets

#### 2.3 Lambda AST Generation
Convert parsed ASCII Math to Lambda's internal representation:
- **Follow Math.md Schema**: Produce identical Lambda AST structure as LaTeX Math parser
- Map to standardized elements: `<add>`, `<mul>`, `<div>`, `<frac>`, `<pow>`, `<sqrt>`, `<sin>`, etc.
- **Exact Schema Compliance**: Use the same operator names and element structure defined in Math.md
- Maintain full compatibility with existing format system and downstream processors
- Ensure ASCII and LaTeX parsers produce identical AST for equivalent expressions

### Phase 3: Testing Infrastructure (Week 2)

#### 3.1 Create Lambda Test Script
**File**: `./test/lambda/input/test_math_ascii.ls`
```lambda
// ASCII Math Parser Test Suite
"=== ASCII MATH PARSER TESTS ==="

// Basic expressions
let basic_test = input('x + y', {'type': 'math', 'flavor': 'ascii'})
"Basic addition:"
basic_test

// Functions
let function_test = input('sin(x) + cos(y)', {'type': 'math', 'flavor': 'ascii'})
"Trigonometric functions:"
function_test

// Fractions
let fraction_test = input('x/y + (a+b)/(c+d)', {'type': 'math', 'flavor': 'ascii'})
"Fractions:"
fraction_test

// Powers and subscripts
let power_test = input('x^2 + y_1^3', {'type': 'math', 'flavor': 'ascii'})
"Powers and subscripts:"
power_test

// Greek letters and symbols  
let symbol_test = input('alpha + beta = gamma', {'type': 'math', 'flavor': 'ascii'})
"Greek symbols:"
symbol_test

// Roots
let root_test = input('sqrt(x) + root(3)(y)', {'type': 'math', 'flavor': 'ascii'})
"Square and cube roots:"
root_test

// Matrices
let matrix_test = input('[[a,b],[c,d]]', {'type': 'math', 'flavor': 'ascii'})
"Matrix notation:"
matrix_test
```

#### 3.2 Create Test Input Files
- `./test/input/ascii_math_basic.txt` - Simple expressions
- `./test/input/ascii_math_functions.txt` - Function calls
- `./test/input/ascii_math_advanced.txt` - Complex expressions
- `./test/input/ascii_math_matrices.txt` - Matrix examples

### Phase 4: Comprehensive Unit Tests (Week 2-3)

#### 4.1 Create Criterion Test Suite
**File**: `./test/test_math_ascii.c`
```c
#include <criterion/criterion.h>
#include "../lambda/input/input.h"

TestSuite(ascii_math_parser);

Test(ascii_math_parser, test_basic_arithmetic) {
    MemPool* pool = mempool_create();
    
    Item result = input_ascii_math(pool, "x + y");
    cr_assert_not_null(result.data);
    cr_assert_eq(result.type_id, LMD_TYPE_ELEMENT);
    
    mempool_destroy(pool);
}

Test(ascii_math_parser, test_fractions) {
    MemPool* pool = mempool_create();
    
    Item result = input_ascii_math(pool, "x/y");
    cr_assert_not_null(result.data);
    // Verify fraction structure
    
    mempool_destroy(pool);
}

// More comprehensive tests...
```

#### 4.2 Test Categories
1. **Tokenization Tests**: Verify correct token identification
2. **Basic Arithmetic**: +, -, *, /, ^, _
3. **Functions**: sin, cos, log, sqrt, etc.
4. **Symbols**: Greek letters, special constants
5. **Complex Expressions**: Nested operations, precedence
6. **Matrices**: Various bracket styles and dimensions
7. **Error Handling**: Invalid syntax, edge cases
8. **Unicode Output**: Correct symbol mapping
9. **🆕 Schema Compliance**: Verify AST matches Math.md specification
10. **🆕 Cross-Parser Validation**: Ensure ASCII and LaTeX produce identical AST for equivalent expressions

### Phase 5: Formatter Implementation (Week 3-4)

#### 5.1 Create ASCII Math Formatter
**File**: `./lambda/format/format-math-ascii.cpp`
- Convert Lambda AST back to ASCII Math notation
- Support multiple output formats (ASCII, Unicode, MathML)
- Maintain reversibility where possible

#### 5.2 Integration with Main Formatter
- Add ASCII output option to `format-math.cpp`
- Implement `MATH_OUTPUT_ASCII` flavor
- Ensure compatibility with existing format system

### Phase 6: Integration and Documentation (Week 4)

#### 6.1 Integration Points
- Update `input.h` to include ASCII math functions
- Modify main input dispatcher to route ASCII math requests
- Update build system (`Makefile`, config files)
- Add to VS Code language support if applicable

#### 6.2 Documentation Updates
- Update `README.md` with ASCII Math examples
- Add ASCII Math syntax guide
- Document API changes and new functions
- Create migration guide from patched LaTeX approach

## Technical Specifications

### Lambda AST Schema Compliance

The ASCII Math parser **must** produce Lambda AST that follows the exact schema defined in `Math.md`. This ensures:

#### **Required AST Elements**
All ASCII Math expressions must map to the standardized Lambda elements:

| ASCII Math Input | Required Lambda AST | LaTeX Equivalent |
|------------------|---------------------|------------------|
| `x + y` | `<add 'x' 'y'>` | `x + y` |
| `x - y` | `<sub 'x' 'y'>` | `x - y` |
| `x * y` | `<mul 'x' 'y'>` | `x \cdot y` |
| `x / y` | `<div 'x' 'y'>` | `x / y` |
| `x^n` | `<pow 'x' 'n'>` | `x^{n}` |
| `sqrt(x)` | `<sqrt 'x'>` | `\sqrt{x}` |
| `sin(x)` | `<sin 'x'>` | `\sin{x}` |
| `alpha` | `'alpha'` | `\alpha` |
| `[[a,b],[c,d]]` | `<matrix <row 'a' 'b'> <row 'c' 'd'>>` | `\begin{matrix}a&b\\c&d\end{matrix}` |

#### **Schema Validation Requirements**
- **Identical Structure**: ASCII `x^2` and LaTeX `x^{2}` must both produce `<pow 'x' 2>`
- **Symbol Consistency**: ASCII `alpha` and LaTeX `\alpha` must both produce `'alpha'`
- **Function Mapping**: ASCII `sin(x)` and LaTeX `\sin{x}` must both produce `<sin 'x'>`
- **Matrix Compatibility**: ASCII and LaTeX matrix notation must produce identical AST structure

#### **Compliance Testing**
```lambda
// Test: Identical AST for equivalent expressions
let ascii_result = input('x^2 + sin(alpha)', {'type': 'math', 'flavor': 'ascii'})
let latex_result = input('x^{2} + \sin{\alpha}', {'type': 'math', 'flavor': 'latex'})

// Both must produce: <add <pow 'x' 2> <sin 'alpha'>>
assert_equal(ascii_result, latex_result, "ASCII and LaTeX must produce identical AST")
```

### API Design

#### Primary Functions
```c
// Main entry point - must produce Lambda AST compliant with Math.md schema
Item input_ascii_math(MemPool* pool, const char* ascii_math);

// Core parsing functions
ASCIIToken* ascii_tokenize(const char* input, size_t* token_count);
Item parse_ascii_expression(MemPool* pool, ASCIIToken* tokens, size_t token_count);

// AST validation functions
bool validate_lambda_ast_schema(Item ast);
bool compare_ast_with_latex(Item ascii_ast, Item latex_ast);

// Formatting functions
String* format_ascii_math(MemPool* pool, Item expr);
String* format_ascii_to_unicode(MemPool* pool, Item expr);
```

#### Error Handling
- Graceful degradation on syntax errors
- Detailed error messages with position information
- Fallback to text representation when parsing fails

### Performance Considerations

1. **Memory Management**: Use Lambda's variable memory pool system
2. **Token Caching**: Cache tokenization results for repeated expressions
3. **Symbol Lookup**: Use hash table for constant symbol mapping
4. **Minimal Allocations**: Reuse token structures where possible

### Unicode Support

- **Level 0**: ASCII-only fallback (no Unicode symbols)
- **Level 1**: Full Unicode symbol support with ICU
- **Configurable**: Runtime selection based on system capabilities

## Testing Strategy

### Incremental Testing Approach

1. **Unit Tests First**: Create failing tests before implementation
2. **Lambda Scripts**: Interactive testing during development
3. **Regression Testing**: Ensure no impact on existing LaTeX math
4. **Performance Testing**: Benchmark against current implementation
5. **Integration Testing**: Full pipeline from input to formatted output

### Test Coverage Goals

- **Tokenization**: 100% coverage of all ASCII Math constants
- **Parsing**: All grammar productions from official spec
- **Edge Cases**: Empty input, malformed expressions, very long formulas
- **Unicode**: Verify correct symbol mapping across all supported symbols
- **Compatibility**: Ensure existing tests continue to pass
- **🆕 Schema Compliance**: 100% coverage of Lambda AST elements defined in Math.md
- **🆕 Cross-Parser Consistency**: Verify identical AST output for equivalent ASCII/LaTeX expressions

## Migration Strategy

### Gradual Transition

1. **Phase 1**: Implement alongside existing system
2. **Phase 2**: Add feature flag to switch between implementations  
3. **Phase 3**: Deprecate old ASCII support in LaTeX parser
4. **Phase 4**: Remove ASCII code from LaTeX parser (cleanup)

### Backward Compatibility

- Maintain existing API for ASCII math flavor
- Ensure identical output for currently working expressions
- Provide migration warnings for deprecated features

## Success Criteria

1. **Functionality**: All examples from official spec parse correctly
2. **Performance**: No more than 10% slower than current implementation
3. **Memory**: No memory leaks, efficient pool usage
4. **Testing**: 95%+ code coverage, all tests passing
5. **Documentation**: Complete API documentation and examples
6. **Integration**: Seamless integration with existing Lambda workflow
7. **🆕 Schema Compliance**: 100% adherence to Lambda AST schema defined in Math.md
8. **🆕 Cross-Parser Consistency**: ASCII and LaTeX parsers produce identical AST for equivalent mathematical expressions

## Timeline

- **Week 1**: Foundation, tokenizer, basic parser
- **Week 2**: Lambda tests, expression parsing, simple Criterion tests
- **Week 3**: Advanced parsing, comprehensive unit tests
- **Week 4**: Formatter, integration, documentation

Total estimated effort: 4 weeks for full implementation and testing.

## Future Enhancements

1. **Extended Symbol Set**: Support for additional mathematical symbols
2. **Custom Extensions**: Allow user-defined symbol mappings
3. **Optimization**: JIT compilation of frequently used expressions
4. **Validation**: Schema validation for mathematical correctness
5. **Export Formats**: Additional output formats (LaTeX, MathJax, etc.)

This plan provides a structured approach to implementing a clean, maintainable ASCII Math parser that addresses the shortcomings of the current patched approach while maintaining full compatibility with the existing Lambda Script ecosystem.
