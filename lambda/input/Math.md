# Math Parser Design and Progress

## Overview
This document tracks the design and implementation progress of the math parser for the Lambda system. The parser supports multiple math syntax flavors including LaTeX math, Typst math, and ASCII math.

## Design

### Core Structure
The math parser produces a syntax tree composed of nested Lambda `<expr>` elements:
- `<expr op:add>` for addition operations
- `<expr op:mul>` for multiplication operations
- `<expr op:div>` for division operations
- `<expr op:sub>` for subtraction operations
- `<expr op:pow>` for exponentiation operations
- `<expr op:frac>` for fractions (LaTeX specific)
- `<expr op:sqrt>` for square roots
- `<expr op:sin>`, `<expr op:cos>`, etc. for mathematical functions
- Literals map directly to Lambda literals
- Nested expressions as operand child nodes

### Supported Flavors
1. **LaTeX Math** (default) - Traditional LaTeX math syntax
2. **Typst Math** - Modern Typst math syntax with `^` power operator
3. **ASCII Math** - Simplified ASCII-based math notation with `^` and `**` power operators

### Enhanced Input API
The `input()` function now supports two calling conventions:

#### Legacy (Backward Compatible)
```lambda
input('./path/to/file.txt', 'math')  // defaults to LaTeX flavor
```

#### New Map-Based Options  
```lambda
input('./path/to/file.txt', {'type': 'math', 'flavor': 'latex'})
input('./path/to/file.txt', {'type': 'math', 'flavor': 'typst'})
input('./path/to/file.txt', {'type': 'math', 'flavor': 'ascii'})
```

### Core API
```c
void parse_math(Input* input, const char* math_string, const char* flavor);
```

## Implementation Progress

### Phase 1: Basic Infrastructure ✅
- [x] Create `input-math.c` file
- [x] Implement basic parser structure
- [x] Add flavor parameter support
- [x] Implement simple arithmetic operators (+, -, *, /)
- [x] Basic number parsing
- [x] Create test framework

### Phase 2: LaTeX Math Support ✅ 
- [x] Implement `\frac{}{}`
- [x] Implement `^{}` (superscript)
- [x] Implement `_{}` (subscript) 
- [x] Implement `\sqrt{}`
- [x] Correct operator precedence (*, / before +, -)
- [x] Parentheses grouping with `(expression)`
- [x] Mathematical functions: `\sin{}`, `\cos{}`, `\tan{}`, `\log{}`, `\ln{}`
- [x] Greek letters (α, β, γ, etc.) - Enhanced with full Greek alphabet support
- [x] Advanced mathematical functions: `\arcsin{}`, `\arccos{}`, `\sinh{}`, `\cosh{}`, etc.
- [x] Mathematical operators: `\sum{}`, `\prod{}`, `\int{}`, `\lim{}`
- [x] Sum and product with limits: `\sum_{i=1}^{n}`, `\prod_{k=0}^{\infty}`
- [x] Integral with limits: `\int_{a}^{b}`
- [x] Limit expressions: `\lim_{x \to 0}`
- [x] Basic matrix support: `\matrix{}`, `\pmatrix{}`, `\bmatrix{}`
- [ ] Advanced matrix environments (`\begin{matrix}...\end{matrix}`)
- [ ] Integrals and sums with complex expressions

### Phase 3: Enhanced Input API ✅
- [x] Support for map-based options in `fn_input`
- [x] Backward compatibility with legacy symbol-based API
- [x] Robust type detection using `get_type_id`
- [x] Proper map extraction for `type` and `flavor` keys
- [x] Integration with existing input system

### Phase 4: Multi-Flavor Support ✅
- [x] Flavor detection and dispatch
- [x] Typst basic support (power operator `^`, basic arithmetic)
- [x] ASCII basic support (power operators `^` and `**`, basic arithmetic)
- [x] Flavor-specific parsing in primary expressions
- [ ] Advanced Typst fraction syntax
- [ ] ASCII function call notation (`sqrt(x)`, `sin(x)`)

### Phase 5: Document Parser Integration ✅
- [x] Integration with Markdown parser (`input-md.c`)
  - [x] Inline math (`$...$`)
  - [x] Display math (`$$...$$`)  
  - [x] Math code blocks (````math`)
- [x] Integration with LaTeX parser (`input-latex.c`)
  - [x] Inline math (`$...$`)
  - [x] Display math (`$$...$$`)
  - [x] Math environments (`\begin{equation}`, `\begin{align}`, etc.)
- [x] Flavor-aware parsing in document contexts
- [x] Comprehensive integration testing

### Phase 6: Code Refactoring and Optimization ✅
- [x] Created shared `input-common.h` and `input-common.c` for code reuse
- [x] Refactored both `input-math.c` and `input-latex.c` to use common utilities
- [x] Consolidated Greek letters, math operators, and LaTeX command definitions
- [x] Shared whitespace handling and string parsing utilities
- [x] Improved maintainability through reduced code duplication

### Phase 7: Advanced LaTeX Math Features ✅
- [x] Enhanced Greek letter support (full alphabet: α, β, γ, δ, ε, ..., Ω)
- [x] Mathematical operators (∞, ∂, ∇, ⋅, ×, ÷, ±, ∓, ≤, ≥, ≠, ≈, etc.)
- [x] Advanced trigonometric functions (arcsin, arccos, arctan, sinh, cosh, tanh)
- [x] Hyperbolic and inverse functions (cot, sec, csc, etc.)
- [x] Sum notation with limits: `\sum_{i=1}^{n} expression`
- [x] Product notation with limits: `\prod_{k=0}^{\infty} expression` 
- [x] Integral notation with limits: `\int_{a}^{b} f(x) dx`
- [x] Limit expressions: `\lim_{x \to 0} f(x)`
- [x] Basic matrix support: `\matrix{a & b \\ c & d}`
- [x] Parenthesized matrices: `\pmatrix{1 & 2 \\ 3 & 4}`
- [x] Bracketed matrices: `\bmatrix{x \\ y \\ z}`

## Test Coverage

### Core Math Parser ✅
- Basic arithmetic with correct precedence: ✅ (1+2*3 → 1+(2*3))
- LaTeX fractions: ✅ (\frac{1}{2})
- LaTeX superscripts: ✅ (x^{2})  
- LaTeX square roots: ✅ (\sqrt{expression})
- LaTeX mathematical functions: ✅ (\sin{x}, \cos{y}, etc.)
- Parentheses grouping: ✅ ((2+3)*4 → (2+3)*4)

### Multi-Flavor API ✅
- Legacy symbol-based input: ✅ (input('file.txt', 'math'))
- Map-based input: ✅ (input('file.txt', {'type': 'math', 'flavor': 'latex'}))
- Flavor detection: ✅ (all three flavors: latex, typst, ascii)
- Type safety: ✅ (robust map vs symbol detection)

### Typst Math ✅ (Basic)
- Power operations: ✅ (2^3)
- Basic arithmetic: ✅ (+, -, *, /)
- Parentheses: ✅

### ASCII Math ✅ (Basic)  
- Power operations: ✅ (x^2, x**2)
- Basic arithmetic: ✅ (+, -, *, /)
- Parentheses: ✅

### Document Parser Integration ✅
- Markdown with math: ✅ (inline $...$, display $$...$$, code blocks ```math)
- LaTeX document with math: ✅ (inline $...$, display $$...$$, environments)
- Flavor-aware parsing: ✅ (defaults to LaTeX in document contexts)
- Mixed content handling: ✅ (text and math together)

## Test Files Structure

### Essential Tests
- `test/lambda/input/test_math_parser.ls` - Multi-flavor math parser testing
- `test/lambda/input/test_integration.ls` - Document parser integration testing

### Input Test Files
- `test/input/test_math_simple.txt` - Basic LaTeX math expressions
- `test/input/test_math_functions.txt` - LaTeX mathematical functions
- `test/input/test_math_typst.txt` - Typst syntax examples
- `test/input/test_math_ascii.txt` - ASCII math notation
- `test/input/test_markdown_math.md` - Markdown document with embedded math
- `test/input/test_latex_math.tex` - LaTeX document with math expressions

## Current Output Examples

### LaTeX Math
```
Input: "1 + 2 * 3"           → <expr op:"add";"1";<expr op:"mul";"2";"3">>
Input: "\frac{1}{2}"         → <expr op:"frac";"1";"2">
Input: "x^{2}"               → <expr op:"pow";"x";"2">
Input: "\sin{x} + \cos{y}"   → <expr op:"add";<expr op:"sin";"x">;<expr op:"cos";"y">>
Input: "(2 + 3) * 4"         → <expr op:"mul";<expr op:"add";"2";"3">;"4">
```

### Enhanced LaTeX Math
```
Input: "\alpha + \beta"      → <expr op:"add";"alpha";"beta">
Input: "\sum_{i=1}^{n} i"    → <expr op:"sum";"<expr op:"eq";"i";"1">";"n";"i">
Input: "\int_{0}^{1} x dx"   → <expr op:"int";"0";"1";"x">
Input: "\lim_{x \to 0} f(x)" → <expr op:"lim";"<expr op:"to";"x";"0">";"f(x)">
Input: "\matrix{a & b \\ c & d}" → <expr op:"matrix";<row>"a";"b"</row>;<row>"c";"d"</row>>
```

### Multi-Flavor Usage
```lambda
// Legacy API (defaults to LaTeX)
let result1 = input('./math.txt', 'math')

// New API with explicit flavors
let result2 = input('./math.txt', {'type': 'math', 'flavor': 'latex'})
let result3 = input('./math.txt', {'type': 'math', 'flavor': 'typst'})  
let result4 = input('./math.txt', {'type': 'math', 'flavor': 'ascii'})
```

### Typst Math (Basic)
```
Input: "2^3 + x/y"           → Power and division operations
```

### ASCII Math (Basic)
```
Input: "x**2 + sqrt(y)"      → Power and function operations
```

## Current Status
- ✅ **Core Infrastructure**: Complete recursive descent parser with flavor support
- ✅ **LaTeX Support**: Full LaTeX math parsing with functions, fractions, powers, subscripts
- ✅ **Advanced LaTeX**: Greek letters, mathematical operators, sums, integrals, limits, matrices
- ✅ **API Enhancement**: Map-based options with full backward compatibility
- ✅ **Multi-Flavor**: Basic Typst and ASCII support with power operations
- ✅ **Integration**: Fully integrated with Lambda input system and type system
- ✅ **Document Integration**: Math parsing integrated into Markdown and LaTeX document parsers
- ✅ **Code Refactoring**: Shared utilities between input parsers for better maintainability
- ✅ **Testing**: Comprehensive test coverage for core functionality and integration

## Current Limitations
- Typst and ASCII flavors have basic support (arithmetic + power operations only)
- Matrix parsing is simplified (doesn't support full `\begin{matrix}...\end{matrix}` environments)
- No error recovery for malformed expressions
- Limited function support in non-LaTeX flavors
- No support for very complex mathematical constructs (advanced integrals, differential equations)
- Some advanced LaTeX math environments not yet fully supported

## Next Steps
1. **Advanced Typst support**: Implement Typst-specific fraction and function syntax
2. **ASCII function parsing**: Add support for `sqrt(x)`, `sin(x)` function call notation
3. **Full matrix environments**: Support `\begin{matrix}...\end{matrix}` syntax
4. **Error handling**: Improve error reporting and recovery for malformed expressions
5. **Performance**: Optimize parsing for large mathematical expressions
6. **Extended syntax**: Add support for advanced mathematical constructs
7. **Enhanced LaTeX environments**: Complete support for complex math environments

## Code Organization
- `lambda/input/input-math.c` - Core math parser implementation
- `lambda/input/input-common.c` - Shared utilities between parsers
- `lambda/input/input-common.h` - Common definitions and function declarations
- `lambda/input/input.c` - Input dispatch and file handling  
- `lambda/input/input-md.c` - Markdown parser with math integration
- `lambda/input/input-latex.c` - LaTeX parser with math integration (refactored)
- `lambda/lambda-eval.c` - Enhanced `fn_input` with map support
- `test/lambda/input/test_math_parser.ls` - Core math parser tests
- `test/lambda/input/test_integration.ls` - Document integration tests
- `test/input/test_math_*.txt` - Math input test files
- `test/input/test_*_math.*` - Document integration test files
