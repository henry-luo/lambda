# Math Parser Design and Progress

## Overview
This document tracks the design and implementation progress of the math parser for the Lambda system. The parser supports multiple math syntax flavors including LaTeX math, Typst math, and ASCII math.

## Design

### Core Structure
The math parser produces a syntax tree composed of nested Lambda elements using operator names:
- `<add>` for addition operations
- `<mul>` for multiplication operations  
- `<div>` for division operations
- `<sub>` for subtraction operations
- `<pow>` for exponentiation operations
- `<frac>` for fractions (LaTeX specific)
- `<sqrt>` for square roots
- `<sin>`, `<cos>`, etc. for mathematical functions
- Literals map directly to Lambda literals (numbers as strings, variables as symbols)
- Nested expressions as child nodes

### Supported Flavors
1. **LaTeX Math** (default) - Traditional LaTeX math syntax
2. **Typst Math** - Modern Typst math syntax with `^` power operator
3. **ASCII Math** - Simplified ASCII-based math notation with `^` and `**` power operators
4. **MathML** - W3C standard XML markup for mathematical expressions

### MathML Integration
**Mathematical Markup Language (MathML)** is the W3C standard for representing mathematical notation in web pages and applications. The Lambda math parser documentation includes MathML equivalents for all supported expressions to facilitate:

#### Benefits of MathML Support
- **Web Accessibility**: Screen readers can interpret mathematical expressions
- **Semantic Preservation**: Mathematical meaning is preserved in markup
- **Universal Compatibility**: Browser-native rendering without external libraries
- **Integration Ready**: Easy conversion between Lambda expressions and web formats

#### MathML Elements Used
- `<mi>` - Mathematical identifiers (variables, function names)
- `<mn>` - Numbers  
- `<mo>` - Operators (+, -, ×, ÷, etc.)
- `<mrow>` - Horizontal grouping of expressions
- `<mfrac>` - Fractions with numerator and denominator
- `<msup>` - Superscripts (exponents)
- `<msub>` - Subscripts
- `<msqrt>` - Square roots
- `<mover>` - Over-scripts (hats, arrows, etc.)
- `<munder>` - Under-scripts (limits)
- `<munderover>` - Combined under and over scripts (summations, integrals)
- `<mtable>`, `<mtr>`, `<mtd>` - Tables and matrices

#### Usage Example
```xml
<!-- LaTeX: \frac{x^2 + y^2}{2} -->
<!-- Lambda: <frac (<add (<pow 'x' 2>) (<pow 'y' 2>)>) 2> -->
<mfrac>
  <mrow>
    <msup><mi>x</mi><mn>2</mn></msup>
    <mo>+</mo>
    <msup><mi>y</mi><mn>2</mn></msup>
  </mrow>
  <mn>2</mn>
</mfrac>
```

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

## Implementation Details

### Phase 1: Basic Infrastructure ✅
- ✅ Create `input-math.c` file
- ✅ Implement basic parser structure
- ✅ Add flavor parameter support
- ✅ Implement simple arithmetic operators (+, -, *, /)
- ✅ Basic number parsing
- ✅ Create test framework

### Phase 2: LaTeX Math Support ✅ 
- ✅ Implement `\frac{}{}`
- ✅ Implement `^{}` (superscript)
- ✅ Implement `_{}` (subscript) 
- ✅ Implement `\sqrt{}`
- ✅ Correct operator precedence (*, / before +, -)
- ✅ Parentheses grouping with `(expression)`
- ✅ Mathematical functions: `\sin{}`, `\cos{}`, `\tan{}`, `\log{}`, `\ln{}`
- ✅ Greek letters (α, β, γ, etc.) - Enhanced with full Greek alphabet support
- ✅ Advanced mathematical functions: `\arcsin{}`, `\arccos{}`, `\sinh{}`, `\cosh{}`, etc.
- ✅ Mathematical operators: `\sum{}`, `\prod{}`, `\int{}`, `\lim{}`
- ✅ Sum and product with limits: `\sum_{i=1}^{n}`, `\prod_{k=0}^{\infty}`
- ✅ Integral with limits: `\int_{a}^{b}`
- ✅ Limit expressions: `\lim_{x \to 0}`
- ✅ Basic matrix support: `\matrix{}`, `\pmatrix{}`, `\bmatrix{}`
- ✅ Advanced matrix environments (`\begin{matrix}...\end{matrix}`)
- ✅ Integrals and sums with complex expressions

### Phase 3: Enhanced Input API ✅
- ✅ Support for map-based options in `fn_input`
- ✅ Backward compatibility with legacy symbol-based API
- ✅ Robust type detection using `get_type_id`
- ✅ Proper map extraction for `type` and `flavor` keys
- ✅ Integration with existing input system

### Phase 4: Multi-Flavor Support ✅
- ✅ Flavor detection and dispatch
- ✅ Typst basic support (power operator `^`, basic arithmetic)
- ✅ ASCII basic support (power operators `^` and `**`, basic arithmetic)
- ✅ Flavor-specific parsing in primary expressions
- ✅ Advanced Typst fraction syntax (`frac(a, b)`)
- ✅ ASCII function call notation (`sqrt(x)`, `sin(x)`)

### Phase 5: Document Parser Integration ✅
- ✅ Integration with Markdown parser (`input-md.c`)
  - ✅ Inline math (`$...$`)
  - ✅ Display math (`$$...$$`)  
  - ✅ Math code blocks (`math`)
- ✅ Integration with LaTeX parser (`input-latex.c`)
  - ✅ Inline math (`$...$`)
  - ✅ Display math (`$$...$$`)
  - ✅ Math environments (`\begin{equation}`, `\begin{align}`, etc.)
- ✅ Flavor-aware parsing in document contexts
- ✅ Comprehensive integration testing

### Phase 6: ASCII Math Enhancements ✅ 
- ✅ **Function Call Bug Fixes**: Fixed string buffer management causing `ERROR` returns
  - All ASCII function calls now parse correctly: `sqrt(16)`, `sin(0)`, `cos(1)`, `log(10)`
  - Enhanced identifier extraction with proper bounds checking
  - Fixed parentheses handling in function parameter parsing
- ✅ **Power Operation Enhancement**: Added comprehensive power operator support
  - Both `^` and `**` operators fully supported with correct precedence
  - Right-associative power operations: `2^3^4` → `2^(3^4)`
  - Dedicated POWER precedence level (higher than multiplication)
- ✅ **Number Type Enhancement**: Numbers now parse as proper Lambda types
  - Numbers like `"10"` are parsed as Lambda integers instead of strings
  - Floating-point numbers parsed as Lambda floats
  - Consistent type handling across all math expressions
- - ✅ **Comprehensive Testing**: All enhancements validated with extensive test suites

### Phase 9: Enhanced Mathematical Constructs  ✅
- ✅ **Binomial Coefficients**: LaTeX `\binom{n}{k}` and `\choose` syntax
  - Binomial notation: `\binom{n}{k}` → `<binom 'n';'k'>`
  - Choose notation: `n \choose k` → `<choose 'n';'k'>`
  - Works with variables and expressions: `\binom{x+1}{2}` → `<binom <add 'x';1>;2>`
  - Nested binomials supported: `\binom{\binom{n}{k}}{r}` → `<binom <binom 'n';'k'>;'r'>`
  - Complex expressions: `\binom{2n}{n+1}` → `<binom <mul 2;'n'>;<add 'n';1>>`

- ✅ **Vector Notation**: Mathematical vector representations
  - Vector arrows: `\vec{v}` → `<vec 'v'>`
  - Multi-character vectors: `\vec{AB}` → `<vec 'AB'>`
  - Vector expressions: `\vec{x + y}` → `<vec <add 'x';'y'>>`
  - Unit vectors: `\vec{i}`, `\vec{j}`, `\vec{k}` → `<vec 'i'>`, `<vec 'j'>`, `<vec 'k'>`
  - Vector operations: `\vec{a} \cdot \vec{b}` → `<mul <vec 'a'>;<cdot>;<vec 'b'>>`

- ✅ **Accent Marks**: Mathematical accent notation  
  - Hat accents: `\hat{x}` → `<hat 'x'>`
  - Tilde accents: `\tilde{y}` → `<tilde 'y'>`
  - Bar accents: `\bar{z}` → `<bar 'z'>`
  - Dot accents: `\dot{a}` → `<dot 'a'>`
  - Double dot: `\ddot{b}` → `<ddot 'b'>`
  - Check accents: `\check{c}` → `<check 'c'>`
  - Breve accents: `\breve{d}` → `<breve 'd'>`
  - Works with expressions: `\hat{x + y}` → `<hat <add 'x';'y'>>`

- ✅ **Derivative Notation**: Mathematical derivative symbols
  - Basic derivatives: `\frac{d}{dx}` → `<frac 'd';'dx'>`
  - Partial derivatives: `\partial` → `<partial symbol:"∂";>`
  - Function derivatives: `\frac{df}{dx}` → `<frac 'df';'dx'>`
  - Higher order: `\frac{d^2f}{dx^2}` → `<frac <pow 'd';2>'f';<pow 'dx';2>>`
  - Partial with function: `\frac{\partial f}{\partial x}` → `<frac <mul <partial symbol:"∂";>;'f'>;<mul <partial symbol:"∂";>;'x'>>`
  - Mixed partials: `\frac{\partial^2 f}{\partial x \partial y}` → Complex expression parsing

- ✅ **Arrow Notation**: Directional arrows and mathematical relations
  - Right arrows: `\to` → `<to direction:"right";>`
  - Left arrows: `\leftarrow` → `<leftarrow direction:"left";>`
  - Double arrows: `\Rightarrow` → `<Rightarrow direction:"right";>`
  - Left-right arrows: `\leftrightarrow` → `<leftrightarrow direction:"both";>`
  - Maps to: `\mapsto` → `<mapsto direction:"right";>`
  - Infinity symbol: `\infty` → `<infty symbol:"∞";>`
  - Used in limits: `x \to \infty` → `<mul <mul 'x';<to direction:"right";>>;<infty symbol:"∞";>>`

- ✅ **Over/Under Line Constructs**: Mathematical emphasis and grouping
  - Overlines: `\overline{x + y}` → `<overline position:"over";<add 'x';'y'>>`
  - Underlines: `\underline{a + b}` → `<underline position:"under";<add 'a';'b'>>`
  - Overbraces: `\overbrace{a + b + c}` → `<overbrace position:"over";<add <add 'a';'b'>;'c'>>`
  - Underbraces: `\underbrace{x y z}` → `<underbrace position:"under";<mul <mul 'x';'y'>;'z'>>`
  - Works with complex expressions: `\overline{\frac{a}{b}}` → `<overline position:"over";<frac 'a';'b'>>`

- ✅ **Implicit Multiplication Enhancement**: Enhanced multiplication parsing
  - Handles consecutive mathematical terms: `\partial f` → `<mul <partial symbol:"∂";>;'f'>`
  - Function composition: `f g(x)` → `<mul 'f';<g 'x'>>`
  - Symbol sequences: `abc` → `<mul <mul 'a';'b'>;'c'>`
  - Coefficient handling: `2x` → `<mul 2;'x'>`
  - Multiple symbol terms: `xy\sin z` → `<mul <mul 'x';'y'>;<sin 'z'>>`
  - Integrates with existing multiplication logic

### Phase 10: Latest Mathematical Constructs (July 2025) ✅

#### Special Mathematical Symbols ✅
- ✅ **Advanced Unicode Symbols**: Extended support for specialized mathematical symbols
  - Script lowercase l: `\ell` → `<ell 'ℓ'>` (symbol:"ℓ")
  - Planck constant: `\hbar` → `<hbar 'ℏ'>` (symbol:"ℏ")  
  - Dotless i: `\imath` → `<imath 'ı'>` (symbol:"ı")
  - Dotless j: `\jmath` → `<jmath 'ȷ'>` (symbol:"ȷ")
  - Hebrew aleph: `\aleph_0` → `<aleph_0 'ℵ₀'>` (cardinal numbers)
  - Hebrew beth: `\beth_1` → `<beth_1 'ב₁'>` (beth numbers)

#### Big Mathematical Operators ✅  
- ✅ **Set Theory Operations**: Advanced set-theoretic operators
  - Big union: `\bigcup A` → `<bigcup 'A'>` (symbol:"⋃")
  - Big intersection: `\bigcap B` → `<bigcap 'B'>` (symbol:"⋂")
- ✅ **Abstract Algebra Operations**: Algebraic structure operators
  - Big circled plus: `\bigoplus C` → `<bigoplus 'C'>` (symbol:"⊕")
  - Big circled times: `\bigotimes D` → `<bigotimes 'D'>` (symbol:"⊗")
- ✅ **Logic Operations**: Large logical operators
  - Big logical and: `\bigwedge E` → `<bigwedge 'E'>` (symbol:"⋀")
  - Big logical or: `\bigvee F` → `<bigvee 'F'>` (symbol:"⋁")

#### Advanced Fraction Constructs ✅
- ✅ **Fraction Styling**: Multiple LaTeX fraction display styles
  - Display fractions: `\dfrac{a}{b}` → `<frac style:"dfrac";'a';'b'>`
  - Text fractions: `\tfrac{c}{d}` → `<frac style:"tfrac";'c';'d'>`
  - Continued fractions: `\cfrac{e}{f+\cfrac{g}{h}}` → `<frac style:"cfrac";'e';<add 'f';<frac style:"cfrac";'g';'h'>>>`
- ✅ **Nested Fraction Support**: Complex fraction hierarchies
  - Arbitrary nesting depth supported
  - Proper syntax tree generation for complex continued fractions
  - Style preservation through nesting levels

#### Enhanced Root Functions ✅
- ✅ **Cube Roots**: Dedicated cube root parsing
  - Cube root: `\cbrt{8}` → `<root index:"3";8>`
  - Uses `parse_latex_root()` with index="3"
- ✅ **Advanced Root Indexing**: Variable index root support
  - Indexed roots: `\sqrt[4]{16}` → `<root index:"4";16>`
  - Variable index: `\sqrt[n]{x}` → `<root index:"n";'x'>`
  - Expression index: `\sqrt[k+1]{y}` → `<root index:<add 'k';1>;'y'>`

#### Format and Architecture Improvements ✅
- ✅ **Element Format Changes**: Updated syntax tree structure
  - **Before**: Elements used position attributes (`pos:"numerator"`, `pos:"denominator"`)
  - **After**: Elements use child structure for better nesting and parsing
  - Child-based parsing allows for more complex nested expressions
  - Improved memory safety with proper element allocation
- ✅ **Memory Safety Enhancements**: Fixed unsafe symbol creation patterns
  - All symbol creation now uses safe `create_math_symbol_safe()` functions
  - Proper string buffer management prevents memory errors
  - Enhanced error handling for malformed expressions

#### Comprehensive Testing Framework ✅
- ✅ **Consolidated Test Suite**: Streamlined validation approach
  - **Primary Test File**: `test/input/math_comprehensive.txt`
    - Contains all latest mathematical constructs in one file
    - Validates complex mixed expressions with multiple constructs
    - Tests: `\ell + \hbar + \imath + \jmath + \bigcup A + \bigcap B + \bigoplus C + \bigotimes D + \bigwedge E + \bigvee F + \dfrac{a}{b} + \tfrac{c}{d} + \cfrac{e}{f+\cfrac{g}{h}} + \cbrt{8} + \sqrt{16}`
  - **Primary Test Script**: `test/lambda/input/test_math_comprehensive.ls`
    - Unified testing approach for all latest enhancements
    - Comprehensive syntax tree validation
- ✅ **Individual Debug Tests**: Maintained for targeted debugging
  - `test/input/math_simple.txt`: Single expression testing
  - Individual construct test files for isolated validation
  - Facilitates debugging of specific parsing issues

#### Parser Enhancement Benefits ✅
- **Extended Mathematical Coverage**: Supports advanced mathematical notation used in:
  - Quantum mechanics (ℏ, ℓ quantum numbers)
  - Set theory (big unions, intersections)
  - Abstract algebra (big circled operations)
  - Mathematical logic (big logical operations)
  - Advanced calculus (continued fractions, indexed roots)
- **Improved Parsing Architecture**: 
  - Command dispatch system for extensible LaTeX command recognition
  - Modular parsing functions for different construct types
  - Enhanced memory management and error handling
- **Better Integration**: Seamless integration with existing mathematical constructs and document parsers

#### Working Complex Expression Example ✅
The parser successfully handles this comprehensive mathematical expression:
```latex
\ell + \hbar + \imath + \jmath + \bigcup A + \bigcap B + \bigoplus C + \bigotimes D + \bigwedge E + \bigvee F + \dfrac{a}{b} + \tfrac{c}{d} + \cfrac{e}{f+\cfrac{g}{h}} + \cbrt{8} + \sqrt{16}
```

**Generated Syntax Tree** (abbreviated):
```
<add <add <add ... <ell 'ℓ'>;<hbar 'ℏ'>>;<imath 'ı'>>;<jmath 'ȷ'>>;<bigcup 'A'>;<bigcap 'B'>;<bigoplus 'C'>;<bigotimes 'D'>;<bigwedge 'E'>;<bigvee 'F'>;<frac style:"dfrac";'a';'b'>;<frac style:"tfrac";'c';'d'>;<frac style:"cfrac";'e';<add 'f';<frac style:"cfrac";'g';'h'>>>;<root index:"3";8>;<sqrt 16>>
```

This demonstrates the parser's capability to handle:
- Multiple mathematical symbol types in one expression
- Complex nested structures (continued fractions)
- Mixed construct types (symbols, operators, functions, fractions, roots)
- Proper precedence and associativity

## Recent Enhancements

#### Major Mathematical Constructs Added ✅
The math parser received significant enhancements with the addition of six major categories of mathematical constructs:

1. **Binomial Coefficients** (`\binom{n}{k}`, `n \choose k`)
2. **Vector Notation** (`\vec{v}`, unit vectors, vector operations)
3. **Accent Marks** (`\hat{x}`, `\tilde{y}`, `\bar{z}`, `\dot{a}`, `\ddot{b}`, `\check{c}`, `\breve{d}`)
4. **Derivative Notation** (`\frac{d}{dx}`, `\partial`, partial derivatives)
5. **Arrow Notation** (`\to`, `\leftarrow`, `\Rightarrow`, `\leftrightarrow`, `\mapsto`, `\infty`)
6. **Over/Under Line Constructs** (`\overline{}`, `\underline{}`, `\overbrace{}`, `\underbrace{}`)

#### Enhanced Implicit Multiplication ✅
- Improved parsing of consecutive mathematical terms
- Better handling of coefficient-variable combinations (e.g., `2x`, `3xy`)
- Enhanced symbol sequence parsing (e.g., `abc` → `<mul <mul 'a';'b'>;'c'>`)
- Integration with existing multiplication precedence logic

#### Test Suite Consolidation ✅
**Problem**: The math parser had accumulated 25+ individual test files and multiple redundant test scripts, making maintenance difficult and testing inefficient.

**Solution**: Implemented comprehensive test consolidation:
- **Consolidated Input**: Created `test/input/math_enhanced_constructs.txt` containing all new constructs
- **Consolidated Script**: Created `test/lambda/input/test_math_enhanced_consolidated.ls` for unified testing
- **Cleanup**: Removed 25+ obsolete test files while maintaining essential individual files for debugging
- **Documentation**: Enhanced README and summary documentation for clear test structure

**Benefits**:
- Faster test execution (single comprehensive test vs. multiple individual tests)
- Simplified maintenance (one consolidated file vs. 25+ files)
- Better coverage validation (all constructs tested together)
- Maintained debugging capability (key individual files retained)

#### Parser Architecture Improvements ✅
- **Enhanced Helper Functions**: Added robust helper functions for each new construct type
- **Improved Error Handling**: Better error messages and graceful parsing failures
- **String Buffer Management**: Fixed string buffer issues that caused parsing errors
- **Memory Safety**: Enhanced memory management for complex expressions

### Detailed Enhancement Documentation

#### 1. ASCII Function Call Bug Fixes ✅
**Problem**: ASCII function calls like `sqrt(16)`, `sin(0)`, `cos(1)` were returning `ERROR` instead of proper Lambda expressions.

**Root Cause Analysis**: 
- String buffer management issues in function name extraction
- Inadequate bounds checking during identifier parsing
- Incorrect parentheses handling in function parameter parsing

**Solution Implementation**: 
- Enhanced string buffer handling with proper bounds checking
- Fixed identifier extraction logic to handle function names correctly
- Improved parentheses matching for function parameters
- Added comprehensive error handling for malformed function calls

**Validation Results**: All ASCII function calls now parse correctly:
- `sqrt(16)` → `<sqrt 16>` 
- `sin(0)` → `<sin 0>`
- `cos(1)` → `<cos 1>`
- `log(10)` → `<log 10>`

#### 2. Power Operation Enhancement ✅
**Enhancement Goal**: Add comprehensive support for power operations with correct precedence and associativity.

**Technical Implementation**:
- Added dedicated POWER precedence level (higher than multiplication)
- Implemented right-associative parsing logic
- Support for both `^` and `**` operators
- Consistent behavior across all math flavors

**Parsing Examples**:
- `x^2` → `<pow 'x';2>`
- `x**2` → `<pow 'x';2>`
- `2^3^4` → `<pow 2;<pow 3;4>>` (right-associative)
- `x^2 + y^3` → `<add <pow 'x';2>;<pow 'y';3>>`

#### 3. Number Type Enhancement ✅
**Enhancement Goal**: Parse numbers as proper Lambda data types instead of strings.

**Before Enhancement**:
```
<pow 'x';"2">  // "2" parsed as string
<pow 'y';"3">  // "3" parsed as string
```

**After Enhancement**:
```
<pow 'x';2>    // 2 parsed as Lambda integer
<pow 'y';3>    // 3 parsed as Lambda integer
```

**Technical Implementation**:
- Enhanced number parsing using `strtol()` for integers and `strtod()` for floats
- Automatic detection of integer vs floating-point numbers
- Proper Lambda type construction using `i2it()` and `push_d()`
- Consistent type handling across all math expressions

### Testing and Validation Framework
All enhancements have been rigorously tested with:
- **Targeted Tests**: Individual test scripts (`test_power.ls`) for specific features
- **Comprehensive Suite**: Advanced test suite (`test_math_advanced.ls`) covering all flavors
- **Multi-Flavor Validation**: Consistency checks across LaTeX, Typst, and ASCII
- **Regression Testing**: Ensuring backward compatibility with existing functionality

## Test Coverage
### Phase 8: LaTeX Parser Bug Fixes ✅
- ✅ **LaTeX Absolute Value Parsing**: Fixed `\abs{x}` parsing bug 
  - **Issue**: LaTeX absolute value expressions like `\abs{x}` were incorrectly parsed as `<x 'x'>` instead of `<abs 'x'>`
  - **Root Cause**: String buffer corruption in `parse_math_identifier` function
  - **Solution**: Fixed string buffer management by copying strings before creating symbols
  - **Validation**: All LaTeX absolute value forms now parse correctly (`\abs{x}`, `\abs{xyz}`, `\abs{5}`)

- ✅ **LaTeX Ceiling/Floor Parsing**: Fixed `\lceil` and `\lfloor` parsing
  - **Issue**: LaTeX ceiling/floor expressions like `\lceil x \rceil` were showing empty results  
  - **Solution**: Ensured proper symbol creation for ceiling and floor functions
  - **Result**: Now correctly parses as `<ceil 'x'>` and `<floor 'x'>`

- ✅ **LaTeX Environment Parsing**: Fixed equation environment boundary issues
  - **Issue**: LaTeX environments like `\begin{equation}...\end{equation}` failed with "Expected \end{equation}" errors
  - **Root Cause**: The `parse_math_expression` function consumed entire input including environment end tags
  - **Solution**: Modified environment parsing to use loop-based content parsing with proper boundary detection
  - **Result**: Equation environments now parse correctly as `<equation env:"true",numbered:"true";...>`

### Phase 8: Enhanced Math Features  ✅
- ✅ **Advanced Mathematical Functions**: Enhanced support for specialized math functions
  - Absolute value: `\abs{x}` → `<abs 'x'>`
  - Ceiling function: `\lceil x \rceil` → `<ceil 'x'>`  
  - Floor function: `\lfloor x \rfloor` → `<floor 'x'>`
  - Prime notation: `f'(x)` → `<prime count:"1";'f'>`
  - Double prime: `f''(x)` → `<prime count:"2";'f'>`

- ✅ **Number Set Notation**: Mathematical number set symbols
  - Real numbers: `\mathbb{R}` → `<reals type:"R";>`
  - Complex numbers: `\mathbb{C}` → `<complex type:"C";>`
  - Natural numbers: `\mathbb{N}` → `<naturals type:"N";>`
  - Integers: `\mathbb{Z}` → `<integers type:"Z";>`
  - Rational numbers: `\mathbb{Q}` → `<rationals type:"Q";>`

- ✅ **Logic and Set Operators**: Logical quantifiers and set theory operators
  - Universal quantifier: `\forall` → `<forall >`
  - Existential quantifier: `\exists` → `<exists >`
  - Element of: `\in` → `<in >`
  - Subset: `\subset` → `<subset >`
  - Union: `\cup` → `<union >`
  - Intersection: `\cap` → `<intersection >`

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

### Typst Math ✅ (Enhanced)
- Power operations: ✅ (2^3)
- Basic arithmetic: ✅ (+, -, *, /)
- Parentheses: ✅
- Fraction function: ✅ (frac(a, b))
- Function calls: ✅ (sin(x), cos(x), sqrt(x))
- Mixed expressions: ✅ (complex combinations)

### ASCII Math ✅ (Enhanced)  
- Power operations: ✅ (x^2, x**2)
- Basic arithmetic: ✅ (+, -, *, /)
- Parentheses: ✅
- Function calls: ✅ (sqrt(x), sin(x), cos(x), log(x))
- Mixed expressions: ✅ (complex combinations)

### Document Parser Integration ✅
- Markdown with math: ✅ (inline $...$, display $$...$$, code blocks ```math)
- LaTeX document with math: ✅ (inline $...$, display $$...$$, environments)
- Flavor-aware parsing: ✅ (defaults to LaTeX in document contexts)
- Mixed content handling: ✅ (text and math together)

## Supported Math Expression Mappings

The following table lists all math expressions supported by Lambda's math parser and their mappings across different notation systems:

| Lambda Expression             | LaTeX                                     | Typst                         | ASCII                  | MathML                                                                                    | Description          |
| ----------------------------- | ----------------------------------------- | ----------------------------- | ---------------------- | ----------------------------------------------------------------------------------------- | -------------------- |
| `<add 'a' 'b'>`               | `a + b`                                   | `a + b`                       | `a + b`                | `<mrow><mi>a</mi><mo>+</mo><mi>b</mi></mrow>`                                             | Addition             |
| `<sub 'a' 'b'>`               | `a - b`                                   | `a - b`                       | `a - b`                | `<mrow><mi>a</mi><mo>-</mo><mi>b</mi></mrow>`                                             | Subtraction          |
| `<mul 'a' 'b'>`               | `a * b`                                   | `a * b`                       | `a * b`                | `<mrow><mi>a</mi><mo>⋅</mo><mi>b</mi></mrow>`                                             | Multiplication       |
| `<div 'a' 'b'>`               | `a / b`                                   | `a / b`                       | `a / b`                | `<mrow><mi>a</mi><mo>/</mo><mi>b</mi></mrow>`                                             | Division             |
| `<frac 'a' 'b'>`              | `\frac{a}{b}`                             | `frac(a, b)` or `a/b`         | `a/b`                  | `<mfrac><mi>a</mi><mi>b</mi></mfrac>`                                                     | Fraction             |
| `<pow 'x' 'n'>`               | `x^{n}`                                   | `x^n`                         | `x^n` or `x**n`        | `<msup><mi>x</mi><mi>n</mi></msup>`                                                       | Exponentiation       |
| `<sqrt 'x'>`                  | `\sqrt{x}`                                | `sqrt(x)`                     | `sqrt(x)`              | `<msqrt><mi>x</mi></msqrt>`                                                               | Square root          |
| `<sin 'x'>`                   | `\sin{x}`                                 | `sin(x)`                      | `sin(x)`               | `<mrow><mi>sin</mi><mo>(</mo><mi>x</mi><mo>)</mo></mrow>`                                 | Sine function        |
| `<cos 'x'>`                   | `\cos{x}`                                 | `cos(x)`                      | `cos(x)`               | `<mrow><mi>cos</mi><mo>(</mo><mi>x</mi><mo>)</mo></mrow>`                                 | Cosine function      |
| `<tan 'x'>`                   | `\tan{x}`                                 | `tan(x)`                      | `tan(x)`               | `<mrow><mi>tan</mi><mo>(</mo><mi>x</mi><mo>)</mo></mrow>`                                 | Tangent function     |
| `<arcsin 'x'>`                | `\arcsin{x}`                              | `arcsin(x)`                   | `arcsin(x)`            | `<mrow><mi>arcsin</mi><mo>(</mo><mi>x</mi><mo>)</mo></mrow>`                              | Arcsine function     |
| `<arccos 'x'>`                | `\arccos{x}`                              | `arccos(x)`                   | `arccos(x)`            | `<mrow><mi>arccos</mi><mo>(</mo><mi>x</mi><mo>)</mo></mrow>`                              | Arccosine function   |
| `<arctan 'x'>`                | `\arctan{x}`                              | `arctan(x)`                   | `arctan(x)`            | `<mrow><mi>arctan</mi><mo>(</mo><mi>x</mi><mo>)</mo></mrow>`                              | Arctangent function  |
| `<sinh 'x'>`                  | `\sinh{x}`                                | `sinh(x)`                     | `sinh(x)`              | `<mrow><mi>sinh</mi><mo>(</mo><mi>x</mi><mo>)</mo></mrow>`                                | Hyperbolic sine      |
| `<cosh 'x'>`                  | `\cosh{x}`                                | `cosh(x)`                     | `cosh(x)`              | `<mrow><mi>cosh</mi><mo>(</mo><mi>x</mi><mo>)</mo></mrow>`                                | Hyperbolic cosine    |
| `<tanh 'x'>`                  | `\tanh{x}`                                | `tanh(x)`                     | `tanh(x)`              | `<mrow><mi>tanh</mi><mo>(</mo><mi>x</mi><mo>)</mo></mrow>`                                | Hyperbolic tangent   |
| `<cot 'x'>`                   | `\cot{x}`                                 | `cot(x)`                      | `cot(x)`               | `<mrow><mi>cot</mi><mo>(</mo><mi>x</mi><mo>)</mo></mrow>`                                 | Cotangent function   |
| `<sec 'x'>`                   | `\sec{x}`                                 | `sec(x)`                      | `sec(x)`               | `<mrow><mi>sec</mi><mo>(</mo><mi>x</mi><mo>)</mo></mrow>`                                 | Secant function      |
| `<csc 'x'>`                   | `\csc{x}`                                 | `csc(x)`                      | `csc(x)`               | `<mrow><mi>csc</mi><mo>(</mo><mi>x</mi><mo>)</mo></mrow>`                                 | Cosecant function    |
| `<log 'x'>`                   | `\log{x}`                                 | `log(x)`                      | `log(x)`               | `<mrow><mi>log</mi><mo>(</mo><mi>x</mi><mo>)</mo></mrow>`                                 | Logarithm (base 10)  |
| `<ln 'x'>`                    | `\ln{x}`                                  | `ln(x)`                       | `ln(x)`                | `<mrow><mi>ln</mi><mo>(</mo><mi>x</mi><mo>)</mo></mrow>`                                  | Natural logarithm    |
| `<sum 'start' 'end' 'expr'>`  | `\sum_{start}^{end} expr`                 | `sum_(start)^(end) expr`      | `sum(start,end,expr)`  | `<mrow><munderover><mo>∑</mo><mi>start</mi><mi>end</mi></munderover><mi>expr</mi></mrow>` | Summation            |
| `<prod 'start' 'end' 'expr'>` | `\prod_{start}^{end} expr`                | `product_(start)^(end) expr`  | `prod(start,end,expr)` | `<mrow><munderover><mo>∏</mo><mi>start</mi><mi>end</mi></munderover><mi>expr</mi></mrow>` | Product              |
| `<int 'start' 'end' 'expr'>`  | `\int_{start}^{end} expr`                 | `integral_(start)^(end) expr` | `int(start,end,expr)`  | `<mrow><msubsup><mo>∫</mo><mi>start</mi><mi>end</mi></msubsup><mi>expr</mi></mrow>`       | Definite integral    |
| `<lim 'approach' 'expr'>`     | `\lim_{approach} expr`                    | `lim_(approach) expr`         | `lim(approach,expr)`   | `<mrow><munder><mi>lim</mi><mi>approach</mi></munder><mi>expr</mi></mrow>`                | Limit                |
| `<matrix 'rows'>`             | `\matrix{...}`                            | `mat(...)`                    | `matrix(...)`          | `<mtable><mtr>...</mtr></mtable>`                                                         | Matrix               |
| `<pmatrix 'rows'>`            | `\pmatrix{...}`                           | `mat(...)`                    | `pmatrix(...)`         | `<mrow><mo>(</mo><mtable><mtr>...</mtr></mtable><mo>)</mo></mrow>`                        | Parenthesized matrix |
| `<bmatrix 'rows'>`            | `\bmatrix{...}`                           | `mat(...)`                    | `bmatrix(...)`         | `<mrow><mo>[</mo><mtable><mtr>...</mtr></mtable><mo>]</mo></mrow>`                        | Bracketed matrix     |
| `<cases 'conditions'>`        | `\begin{cases}...\end{cases}`             | `cases(...)`                  | `cases(...)`           | `<mrow><mo>{</mo><mtable columnalign="left"><mtr>...</mtr></mtable></mrow>`               | Piecewise function   |
| `<align 'equations'>`         | `\begin{align}...\end{align}`             | `align(...)`                  | `align(...)`           | `<mtable columnalign="right left"><mtr>...</mtr></mtable>`                                | Aligned equations    |
| `<equation 'expr'>`           | `\begin{equation}...\end{equation}`       | `equation(...)`               | `equation(...)`        | `<mrow><mi>expr</mi></mrow>`                                                              | Numbered equation    |
| `<gather 'equations'>`        | `\begin{gather}...\end{gather}`           | `gather(...)`                 | `gather(...)`          | `<mtable><mtr>...</mtr></mtable>`                                                         | Gathered equations   |
| `<smallmatrix 'rows'>`        | `\begin{smallmatrix}...\end{smallmatrix}` | `smallmat(...)`               | `smallmatrix(...)`     | `<mtable displaystyle="false"><mtr>...</mtr></mtable>`                                    | Small matrix         |

### Enhanced Mathematical Constructs

| Lambda Expression                    | LaTeX                                     | Typst                         | ASCII                  | MathML                                          | Description          |
| ------------------------------------ | ----------------------------------------- | ----------------------------- | ---------------------- | ----------------------------------------------- | -------------------- |
| `<binom 'n' 'k'>`                    | `\binom{n}{k}`                            | `binom(n,k)`                  | `C(n,k)`               | `<mrow><mo>(</mo><mfrac linethickness="0"><mi>n</mi><mi>k</mi></mfrac><mo>)</mo></mrow>` | Binomial coefficient |
| `<choose 'n' 'k'>`                   | `n \choose k`                             | `choose(n,k)`                 | `choose(n,k)`          | `<mrow><mo>(</mo><mfrac linethickness="0"><mi>n</mi><mi>k</mi></mfrac><mo>)</mo></mrow>` | Alternative binomial |
| `<vec 'v'>`                          | `\vec{v}`                                 | `arrow(v)`                    | `vec(v)`               | `<mover><mi>v</mi><mo>→</mo></mover>`           | Vector notation      |
| `<hat 'x'>`                          | `\hat{x}`                                 | `hat(x)`                      | `x_hat`                | `<mover><mi>x</mi><mo>^</mo></mover>`           | Hat accent           |
| `<tilde 'y'>`                        | `\tilde{y}`                               | `tilde(y)`                    | `y~`                   | `<mover><mi>y</mi><mo>~</mo></mover>`           | Tilde accent         |
| `<bar 'z'>`                          | `\bar{z}`                                 | `macron(z)`                   | `z_bar`                | `<mover><mi>z</mi><mo>¯</mo></mover>`           | Bar accent           |
| `<dot 'a'>`                          | `\dot{a}`                                 | `dot(a)`                      | `a_dot`                | `<mover><mi>a</mi><mo>˙</mo></mover>`           | Dot accent           |
| `<ddot 'b'>`                         | `\ddot{b}`                                | `dot.double(b)`               | `b_ddot`               | `<mover><mi>b</mi><mo>¨</mo></mover>`           | Double dot accent    |
| `<check 'c'>`                        | `\check{c}`                               | `check(c)`                    | `c_check`              | `<mover><mi>c</mi><mo>ˇ</mo></mover>`           | Check accent         |
| `<breve 'd'>`                        | `\breve{d}`                               | `breve(d)`                    | `d_breve`              | `<mover><mi>d</mi><mo>˘</mo></mover>`           | Breve accent         |
| `<partial>`                          | `\partial`                                | `diff`                        | `d`                    | `<mo>∂</mo>`                                    | Partial derivative   |
| `<to>`                               | `\to`                                     | `->`                          | `->`                   | `<mo>→</mo>`                                    | Right arrow          |
| `<leftarrow>`                        | `\leftarrow`                              | `<-`                          | `<-`                   | `<mo>←</mo>`                                    | Left arrow           |
| `<Rightarrow>`                       | `\Rightarrow`                             | `=>`                          | `=>`                   | `<mo>⇒</mo>`                                    | Right double arrow   |
| `<leftrightarrow>`                   | `\leftrightarrow`                         | `<=>`                         | `<=>`                  | `<mo>↔</mo>`                                    | Left-right arrow     |
| `<mapsto>`                           | `\mapsto`                                 | `|->`                         | `|->`                  | `<mo>↦</mo>`                                    | Maps to              |
| `<infty>`                            | `\infty`                                  | `infinity`                    | `inf`                  | `<mi>∞</mi>`                                    | Infinity symbol      |
| `<overline 'expr'>`                  | `\overline{expr}`                         | `overline(expr)`              | `mean(expr)`           | `<mover><mi>expr</mi><mo>¯</mo></mover>`        | Overline             |
| `<underline 'expr'>`                 | `\underline{expr}`                        | `underline(expr)`             | `underline(expr)`      | `<munder><mi>expr</mi><mo>_</mo></munder>`      | Underline            |
| `<overbrace 'expr'>`                 | `\overbrace{expr}`                        | `overbrace(expr)`             | `overbrace(expr)`      | `<mover><mi>expr</mi><mo>⏞</mo></mover>`        | Overbrace            |
| `<underbrace 'expr'>`                | `\underbrace{expr}`                       | `underbrace(expr)`            | `underbrace(expr)`     | `<munder><mi>expr</mi><mo>⏟</mo></munder>`      | Underbrace           |

### Greek Letters
| Lambda Symbol | LaTeX | Typst | ASCII | MathML | Description |
|---------------|--------|-------|-------|--------|-------------|
| `'alpha'` | `\alpha` | `alpha` | `alpha` | `<mi>α</mi>` | Greek letter α |
| `'beta'` | `\beta` | `beta` | `beta` | `<mi>β</mi>` | Greek letter β |
| `'gamma'` | `\gamma` | `gamma` | `gamma` | `<mi>γ</mi>` | Greek letter γ |
| `'delta'` | `\delta` | `delta` | `delta` | `<mi>δ</mi>` | Greek letter δ |
| `'epsilon'` | `\epsilon` | `epsilon` | `epsilon` | `<mi>ε</mi>` | Greek letter ε |
| `'zeta'` | `\zeta` | `zeta` | `zeta` | `<mi>ζ</mi>` | Greek letter ζ |
| `'eta'` | `\eta` | `eta` | `eta` | `<mi>η</mi>` | Greek letter η |
| `'theta'` | `\theta` | `theta` | `theta` | `<mi>θ</mi>` | Greek letter θ |
| `'iota'` | `\iota` | `iota` | `iota` | `<mi>ι</mi>` | Greek letter ι |
| `'kappa'` | `\kappa` | `kappa` | `kappa` | `<mi>κ</mi>` | Greek letter κ |
| `'lambda'` | `\lambda` | `lambda` | `lambda` | `<mi>λ</mi>` | Greek letter λ |
| `'mu'` | `\mu` | `mu` | `mu` | `<mi>μ</mi>` | Greek letter μ |
| `'nu'` | `\nu` | `nu` | `nu` | `<mi>ν</mi>` | Greek letter ν |
| `'xi'` | `\xi` | `xi` | `xi` | `<mi>ξ</mi>` | Greek letter ξ |
| `'omicron'` | `\omicron` | `omicron` | `omicron` | `<mi>ο</mi>` | Greek letter ο |
| `'pi'` | `\pi` | `pi` | `pi` | `<mi>π</mi>` | Greek letter π |
| `'rho'` | `\rho` | `rho` | `rho` | `<mi>ρ</mi>` | Greek letter ρ |
| `'sigma'` | `\sigma` | `sigma` | `sigma` | `<mi>σ</mi>` | Greek letter σ |
| `'tau'` | `\tau` | `tau` | `tau` | `<mi>τ</mi>` | Greek letter τ |
| `'upsilon'` | `\upsilon` | `upsilon` | `upsilon` | `<mi>υ</mi>` | Greek letter υ |
| `'phi'` | `\phi` | `phi` | `phi` | `<mi>φ</mi>` | Greek letter φ |
| `'chi'` | `\chi` | `chi` | `chi` | `<mi>χ</mi>` | Greek letter χ |
| `'psi'` | `\psi` | `psi` | `psi` | `<mi>ψ</mi>` | Greek letter ψ |
| `'omega'` | `\omega` | `omega` | `omega` | `<mi>ω</mi>` | Greek letter ω |
| `'Gamma'` | `\Gamma` | `Gamma` | `Gamma` | `<mi>Γ</mi>` | Greek letter Γ |
| `'Delta'` | `\Delta` | `Delta` | `Delta` | `<mi>Δ</mi>` | Greek letter Δ |
| `'Theta'` | `\Theta` | `Theta` | `Theta` | `<mi>Θ</mi>` | Greek letter Θ |
| `'Lambda'` | `\Lambda` | `Lambda` | `Lambda` | `<mi>Λ</mi>` | Greek letter Λ |
| `'Xi'` | `\Xi` | `Xi` | `Xi` | `<mi>Ξ</mi>` | Greek letter Ξ |
| `'Pi'` | `\Pi` | `Pi` | `Pi` | `<mi>Π</mi>` | Greek letter Π |
| `'Sigma'` | `\Sigma` | `Sigma` | `Sigma` | `<mi>Σ</mi>` | Greek letter Σ |
| `'Upsilon'` | `\Upsilon` | `Upsilon` | `Upsilon` | `<mi>Υ</mi>` | Greek letter Υ |
| `'Phi'` | `\Phi` | `Phi` | `Phi` | `<mi>Φ</mi>` | Greek letter Φ |
| `'Psi'` | `\Psi` | `Psi` | `Psi` | `<mi>Ψ</mi>` | Greek letter Ψ |
| `'Omega'` | `\Omega` | `Omega` | `Omega` | `<mi>Ω</mi>` | Greek letter Ω |

### Mathematical Operators
| Lambda Symbol | LaTeX | Typst | ASCII | MathML | Description |
|---------------|--------|-------|-------|--------|-------------|
| `'infty'` | `\infty` | `infinity` | `inf` | `<mi>∞</mi>` | Infinity symbol ∞ |
| `'partial'` | `\partial` | `diff` | `d` | `<mo>∂</mo>` | Partial derivative ∂ |
| `'nabla'` | `\nabla` | `nabla` | `nabla` | `<mo>∇</mo>` | Nabla operator ∇ |
| `'cdot'` | `\cdot` | `dot` | `*` | `<mo>⋅</mo>` | Center dot ⋅ |
| `'times'` | `\times` | `times` | `x` | `<mo>×</mo>` | Times symbol × |
| `'div'` | `\div` | `div` | `/` | `<mo>÷</mo>` | Division symbol ÷ |
| `'pm'` | `\pm` | `+-` | `+-` | `<mo>±</mo>` | Plus-minus ± |
| `'mp'` | `\mp` | `-+` | `-+` | `<mo>∓</mo>` | Minus-plus ∓ |
| `'leq'` | `\leq` | `<=` | `<=` | `<mo>≤</mo>` | Less than or equal ≤ |
| `'geq'` | `\geq` | `>=` | `>=` | `<mo>≥</mo>` | Greater than or equal ≥ |
| `'neq'` | `\neq` | `!=` | `!=` | `<mo>≠</mo>` | Not equal ≠ |
| `'approx'` | `\approx` | `~~` | `~` | `<mo>≈</mo>` | Approximately equal ≈ |

### Latest Mathematical Constructs (Phase 10 - July 2025) ✅

The following constructs were added in the most recent enhancement phase:

| Lambda Expression | LaTeX | MathML | Unicode | Description |
|------------------|-------|--------|---------|-------------|
| `<ell 'ℓ'>` | `\ell` | `<mi>ℓ</mi>` | ℓ | Script lowercase l |
| `<hbar 'ℏ'>` | `\hbar` | `<mi>ℏ</mi>` | ℏ | Planck constant |
| `<imath 'ı'>` | `\imath` | `<mi>ı</mi>` | ı | Dotless i |
| `<jmath 'ȷ'>` | `\jmath` | `<mi>ȷ</mi>` | ȷ | Dotless j |
| `<aleph_0 'ℵ₀'>` | `\aleph_0` | `<mi>ℵ₀</mi>` | ℵ₀ | Aleph null (cardinal) |
| `<beth_1 'ב₁'>` | `\beth_1` | `<mi>ב₁</mi>` | ב₁ | Beth one (cardinal) |
| `<bigcup 'A'>` | `\bigcup A` | `<mo>⋃</mo><mi>A</mi>` | ⋃ | Big union |
| `<bigcap 'B'>` | `\bigcap B` | `<mo>⋂</mo><mi>B</mi>` | ⋂ | Big intersection |
| `<bigoplus 'C'>` | `\bigoplus C` | `<mo>⊕</mo><mi>C</mi>` | ⊕ | Big circled plus |
| `<bigotimes 'D'>` | `\bigotimes D` | `<mo>⊗</mo><mi>D</mi>` | ⊗ | Big circled times |
| `<bigwedge 'E'>` | `\bigwedge E` | `<mo>⋀</mo><mi>E</mi>` | ⋀ | Big logical and |
| `<bigvee 'F'>` | `\bigvee F` | `<mo>⋁</mo><mi>F</mi>` | ⋁ | Big logical or |
| `<frac style:"dfrac";'a';'b'>` | `\dfrac{a}{b}` | `<mfrac displaystyle="true"><mi>a</mi><mi>b</mi></mfrac>` | — | Display-style fraction |
| `<frac style:"tfrac";'c';'d'>` | `\tfrac{c}{d}` | `<mfrac displaystyle="false"><mi>c</mi><mi>d</mi></mfrac>` | — | Text-style fraction |
| `<frac style:"cfrac";'e';'f'>` | `\cfrac{e}{f}` | `<mfrac><mi>e</mi><mi>f</mi></mfrac>` | — | Continued fraction |
| `<root index:"3";8>` | `\cbrt{8}` | `<mroot><mn>8</mn><mn>3</mn></mroot>` | ∛ | Cube root |
| `<root index:"n";'x'>` | `\sqrt[n]{x}` | `<mroot><mi>x</mi><mi>n</mi></mroot>` | — | n-th root |
| `<sqrt 16>` | `\sqrt{16}` | `<msqrt><mn>16</mn></msqrt>` | √ | Square root |

**Complex Expression Example**:
```latex
\ell + \hbar + \imath + \jmath + \bigcup A + \dfrac{a}{b} + \cbrt{8}
```
**Parses to**:
```
<add <add <add <add <add <add <ell 'ℓ'>;<hbar 'ℏ'>>;<imath 'ı'>>;<jmath 'ȷ'>>;<bigcup 'A'>>;<frac style:"dfrac";'a';'b'>>;<root index:"3";8>>
```

### Math Expressions Not Yet Supported in Lambda

The following table lists mathematical expressions that are available in other notation systems but are not yet implemented in Lambda's math parser:

| LaTeX | Typst | ASCII | MathML | Description | Priority |
|--------|-------|-------|--------|-------------|----------|
| `\frac{d^2}{dx^2}` | `diff(f,x,2)` | `d2/dx2` | `<mfrac><mrow><msup><mi>d</mi><mn>2</mn></msup></mrow><mrow><mi>d</mi><msup><mi>x</mi><mn>2</mn></msup></mrow></mfrac>` | Higher order derivatives | High |
| `\int f(x) dx` | `integral f(x) dif x` | `integral(f,x)` | `<mrow><mo>∫</mo><mi>f</mi><mo>(</mo><mi>x</mi><mo>)</mo><mi>d</mi><mi>x</mi></mrow>` | Indefinite integral | High |
| `\oint` | `integral.cont` | `contour_int` | `<mo>∮</mo>` | Contour integral | Medium |
| `\sum_{i=1}^{\infty}` | `sum_(i=1)^infinity` | `sum(i=1,inf)` | `<mrow><munderover><mo>∑</mo><mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow><mi>∞</mi></munderover></mrow>` | Infinite series | High |
| `\lim_{x \to \infty}` | `lim_(x -> infinity)` | `lim(x->inf)` | `<mrow><munder><mi>lim</mi><mrow><mi>x</mi><mo>→</mo><mi>∞</mi></mrow></munder></mrow>` | Limit to infinity | High |
| `\mathbb{R}` | `RR` | `R` | `<mi mathvariant="double-struck">ℝ</mi>` | Real numbers | High |
| `\mathbb{N}` | `NN` | `N` | `<mi mathvariant="double-struck">ℕ</mi>` | Natural numbers | High |
| `\mathbb{Z}` | `ZZ` | `Z` | `<mi mathvariant="double-struck">ℤ</mi>` | Integers | High |
| `\mathbb{Q}` | `QQ` | `Q` | `<mi mathvariant="double-struck">ℚ</mi>` | Rational numbers | High |
| `\mathbb{C}` | `CC` | `C` | `<mi mathvariant="double-struck">ℂ</mi>` | Complex numbers | High |
| `\in` | `in` | `in` | `<mo>∈</mo>` | Element of | High |
| `\notin` | `in.not` | `not_in` | `<mo>∉</mo>` | Not element of | Medium |
| `\subset` | `subset` | `subset` | `<mo>⊂</mo>` | Subset | Medium |
| `\supset` | `supset` | `superset` | `<mo>⊃</mo>` | Superset | Medium |
| `\cup` | `union` | `union` | `<mo>∪</mo>` | Set union | Medium |
| `\cap` | `sect` | `intersect` | `<mo>∩</mo>` | Set intersection | Medium |
| `\emptyset` | `nothing` | `empty_set` | `<mo>∅</mo>` | Empty set | Medium |
| `\forall` | `forall` | `forall` | `<mo>∀</mo>` | For all quantifier | Medium |
| `\exists` | `exists` | `exists` | `<mo>∃</mo>` | Exists quantifier | Medium |
| `\land` | `and` | `and` | `<mo>∧</mo>` | Logical AND | Medium |
| `\lor` | `or` | `or` | `<mo>∨</mo>` | Logical OR | Medium |
| `\neg` | `not` | `not` | `<mo>¬</mo>` | Logical NOT | Medium |
| `\Rightarrow` | `=>` | `=>` | `<mo>⇒</mo>` | Implies | Medium |
| `\Leftrightarrow` | `<=>` | `<=>` | `<mo>⇔</mo>` | If and only if | Medium |
| `\leftarrow` | `<-` | `<-` | `<mo>←</mo>` | Left arrow | Low |
| `\rightarrow` | `->` | `->` | `<mo>→</mo>` | Right arrow | Low |
| `\uparrow` | `arrow.t` | `up` | `<mo>↑</mo>` | Up arrow | Low |
| `\downarrow` | `arrow.b` | `down` | `<mo>↓</mo>` | Down arrow | Low |
| `\parallel` | `parallel` | `parallel` | `<mo>∥</mo>` | Parallel | Low |
| `\perp` | `perp` | `perp` | `<mo>⊥</mo>` | Perpendicular | Low |
| `\angle` | `angle` | `angle` | `<mo>∠</mo>` | Angle | Low |
| `\triangle` | `triangle` | `triangle` | `<mo>△</mo>` | Triangle | Low |
| `\square` | `square` | `square` | `<mo>□</mo>` | Square | Low |
| `\diamond` | `diamond` | `diamond` | `<mo>◊</mo>` | Diamond | Low |
| `\circ` | `circle` | `compose` | `<mo>∘</mo>` | Function composition | Medium |
| `\bullet` | `dot.op` | `bullet` | `<mo>∙</mo>` | Bullet operator | Low |
| `\star` | `star` | `star` | `<mo>⋆</mo>` | Star operator | Low |
| `\ast` | `*` | `*` | `<mo>∗</mo>` | Asterisk operator | Low |
| `\oplus` | `plus.circle` | `xor` | `<mo>⊕</mo>` | Exclusive or | Low |
| `\otimes` | `times.circle` | `tensor` | `<mo>⊗</mo>` | Tensor product | Low |
| `\odot` | `dot.circle` | `dot_prod` | `<mo>⊙</mo>` | Dot product | Low |
| `\oslash` | `slash.circle` | `oslash` | `<mo>⊘</mo>` | Circled slash | Low |
| `\boxplus` | `plus.square` | `boxplus` | `<mo>⊞</mo>` | Boxed plus | Low |
| `\boxtimes` | `times.square` | `boxtimes` | `<mo>⊠</mo>` | Boxed times | Low |
| `\equiv` | `equiv` | `equiv` | `<mo>≡</mo>` | Equivalent | Medium |
| `\cong` | `tilde.equiv` | `congruent` | `<mo>≅</mo>` | Congruent | Low |
| `\sim` | `~` | `similar` | `<mo>∼</mo>` | Similar | Low |
| `\simeq` | `tilde.eq` | `simeq` | `<mo>≃</mo>` | Similar equal | Low |
| `\propto` | `prop` | `proportional` | `<mo>∝</mo>` | Proportional | Low |
| `\models` | `tack.r` | `models` | `<mo>⊨</mo>` | Models | Low |
| `\vdash` | `tack.r` | `proves` | `<mo>⊢</mo>` | Proves | Low |
| `\dashv` | `tack.l` | `dashv` | `<mo>⊣</mo>` | Dashv | Low |
| `\top` | `top` | `true` | `<mo>⊤</mo>` | True/top | Low |
| `\bot` | `bot` | `false` | `<mo>⊥</mo>` | False/bottom | Low |
| `\lceil x \rceil` | `ceil(x)` | `ceil(x)` | `<mrow><mo>⌈</mo><mi>x</mi><mo>⌉</mo></mrow>` | Ceiling function | Medium |
| `\lfloor x \rfloor` | `floor(x)` | `floor(x)` | `<mrow><mo>⌊</mo><mi>x</mi><mo>⌋</mo></mrow>` | Floor function | Medium |
| `\left\| x \right\|` | `abs(x)` | `abs(x)` | `<mrow><mo>|</mo><mi>x</mi><mo>|</mo></mrow>` | Absolute value | High |
| `\left\langle x,y \right\rangle` | `angle.l x,y angle.r` | `<x,y>` | `<mrow><mo>⟨</mo><mi>x</mi><mo>,</mo><mi>y</mi><mo>⟩</mo></mrow>` | Inner product | Medium |
| `f'(x)` | `f'(x)` | `f_prime(x)` | `<mrow><msup><mi>f</mi><mo>′</mo></msup><mo>(</mo><mi>x</mi><mo>)</mo></mrow>` | Prime notation | High |
| `f''(x)` | `f''(x)` | `f_double_prime(x)` | `<mrow><msup><mi>f</mi><mo>″</mo></msup><mo>(</mo><mi>x</mi><mo>)</mo></mrow>` | Double prime | High |
| `f^{(n)}(x)` | `f^((n))(x)` | `f_n(x)` | `<mrow><msup><mi>f</mi><mrow><mo>(</mo><mi>n</mi><mo>)</mo></mrow></msup><mo>(</mo><mi>x</mi><mo>)</mo></mrow>` | nth derivative | Medium |
| `\partial f/\partial x` | `diff(f,x)` | `df_dx` | `<mfrac><mrow><mo>∂</mo><mi>f</mi></mrow><mrow><mo>∂</mo><mi>x</mi></mrow></mfrac>` | Partial derivative | High |
| `\nabla f` | `grad f` | `grad(f)` | `<mrow><mo>∇</mo><mi>f</mi></mrow>` | Gradient | Medium |
| `\nabla \cdot \vec{F}` | `div arrow(F)` | `div(F)` | `<mrow><mo>∇</mo><mo>⋅</mo><mover><mi>F</mi><mo>→</mo></mover></mrow>` | Divergence | Medium |
| `\nabla \times \vec{F}` | `curl arrow(F)` | `curl(F)` | `<mrow><mo>∇</mo><mo>×</mo><mover><mi>F</mi><mo>→</mo></mover></mrow>` | Curl | Medium |
| `\Delta` | `laplace` | `laplacian` | `<mo>Δ</mo>` | Laplacian | Low |

**Priority Legend:**
- **High**: Commonly used in mathematical expressions, should be implemented soon
- **Medium**: Moderately useful, implement when extending specific domains
- **Low**: Specialized or rarely used, implement as needed

**Implementation Notes:**
- Many high-priority items involve extending existing function parsing
- Set theory and logic operators would require new parsing categories
- Calculus notation (derivatives, integrals) would need special syntax handling
- Number set symbols (ℝ, ℕ, ℤ, etc.) could be added to Greek letter parsing
- Arrow and geometric symbols are mostly display-oriented

## Current Output Examples

### Enhanced Lambda Math Format
```
Input: "1 + 2 * 3"           → <add 1 <mul 2 3>>
Input: "\frac{1}{2}"         → <frac 1 2>
Input: "x^{2}"               → <pow 'x' 2>
Input: "\sin{x} + \cos{y}"   → <add <sin 'x'> <cos 'y'>>
Input: "(2 + 3) * 4"         → <mul <add 2 3> 4>
```

### New Mathematical Constructs
```
Input: "\binom{n}{k}"           → <binom 'n';'k'>
Input: "n \choose k"            → <choose 'n';'k'>
Input: "\vec{v}"                → <vec 'v'>
Input: "\hat{x} + \tilde{y}"    → <add <hat 'x'>;<tilde 'y'>>
Input: "\bar{z} \cdot \dot{a}"  → <mul <bar 'z'>;<dot 'a'>>
Input: "\ddot{b}"               → <ddot 'b'>
Input: "\frac{d}{dx}"           → <frac 'd';'dx'>
Input: "\partial f"             → <mul <partial symbol:"∂";>;'f'>
Input: "x \to \infty"           → <mul <mul 'x';<to direction:"right";>>;<infty symbol:"∞";>>
Input: "\overline{x + y}"       → <overline position:"over";<add 'x';'y'>>
Input: "\underline{a - b}"      → <underline position:"under";<sub 'a';'b'>>
```

### Complex Combinations
```
Input: "\vec{\hat{x} + \tilde{y}}"              → <vec <add <hat 'x'>;<tilde 'y'>>>
Input: "\binom{n+1}{k-1}"                       → <binom <add 'n';1>;<sub 'k';1>>
Input: "\overline{\frac{a+b}{c}}"               → <overline position:"over";<frac <add 'a';'b'>;'c'>>
Input: "\frac{\partial f}{\partial x}"          → <frac <mul <partial symbol:"∂";>;'f'>;<mul <partial symbol:"∂";>;'x'>>
```

### Enhanced LaTeX Math with New Format
```
Input: "\alpha + \beta"      → <add 'alpha' 'beta'>
Input: "x * y"               → <mul 'x' 'y'>
Input: "\sum_{i=1}^{n} i"    → <sum <assign 'i' 1> 'n' 'i'>
Input: "\int_{0}^{1} x dx"   → <int 0 1 'x'>
Input: "\lim_{x \to 0} f(x)" → <lim <to 'x' 0> <f 'x'>>
Input: "\matrix{a & b \\ c & d}" → <matrix <row 'a' 'b'> <row 'c' 'd'>>
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

### Typst Math (Enhanced)
```
Input: "2^3 + x/y"           → <add <pow "2" "3"> <div 'x' 'y'>>
Input: "frac(1, 2)"          → <frac "1" "2">
Input: "sin(x) + cos(y)"     → <add <sin 'x'> <cos 'y'>>
Input: "sqrt(x^2 + y^2)"     → <sqrt <add <pow 'x' "2"> <pow 'y' "2">>>
```

### ASCII Math (Enhanced)
```
Input: "x**2 + sqrt(y)"      → <add <pow 'x' "2"> <sqrt 'y'>>
Input: "sin(0) + cos(1)"     → <add <sin "0"> <cos "1">>
Input: "log(10) * exp(2)"    → <mul <log "10"> <exp "2">>
Input: "x^2 + y**3"          → <add <pow 'x' "2"> <pow 'y' "3">>
```

### Current Status
- ✅ **Core Infrastructure**: Complete recursive descent parser with flavor support
- ✅ **LaTeX Support**: Full LaTeX math parsing with functions, fractions, powers, subscripts
- ✅ **Advanced LaTeX**: Greek letters, mathematical operators, sums, integrals, limits, matrices
- ✅ **Enhanced Mathematical Constructs**: Comprehensive support for binomial coefficients, vector notation, accent marks, derivative notation, arrow symbols, and over/under line constructs
- ✅ **Latest Mathematical Extensions (July 2025)**: Advanced symbol support, big operators, enhanced fractions, root functions
  - **Special Symbols**: `\ell`, `\hbar`, `\imath`, `\jmath`, `\aleph_0`, `\beth_1` with Unicode mapping
  - **Big Operators**: `\bigcup`, `\bigcap`, `\bigoplus`, `\bigotimes`, `\bigwedge`, `\bigvee` for set theory and algebra
  - **Advanced Fractions**: `\dfrac`, `\tfrac`, `\cfrac` with nested continued fraction support
  - **Enhanced Roots**: `\cbrt{8}` (cube roots), `\sqrt[n]{x}` (indexed roots) with proper syntax trees
- ✅ **Format Improvements**: Updated element structure using child elements instead of position attributes
- ✅ **Memory Safety**: Fixed unsafe symbol creation patterns throughout parser
- ✅ **API Enhancement**: Map-based options with full backward compatibility
- ✅ **Multi-Flavor**: Advanced Typst and ASCII support with function call notation
- ✅ **Integration**: Fully integrated with Lambda input system and type system
- ✅ **Document Integration**: Math parsing integrated into Markdown and LaTeX document parsers
- ✅ **Code Refactoring**: Shared utilities between input parsers for better maintainability
- ✅ **Enhanced Testing**: Consolidated comprehensive test suite with streamlined validation
- ✅ **Comprehensive Expression Parsing**: Successfully handles complex multi-construct expressions
- ✅ **Implicit Multiplication**: Enhanced parsing of consecutive mathematical terms
- ✅ **Documentation**: Comprehensive documentation with examples and implementation details
## Current Limitations and Known Issues

### Input Format Limitations
The math parser currently has several limitations with input format handling:

#### 1. Mixed Content Files with Comments ❌
**Issue**: Files containing both comments and math expressions are not fully supported.

**Example Problem File** (`math_enhanced_mixed.txt`):
```
// Mixed enhanced mathematical expressions
\abs{x^2 + y^2} + \lceil \frac{a}{b} \rceil + f'(x)
\mathbb{R} \in \mathbb{C} \quad \forall x \in \mathbb{N}
\lfloor \sqrt{n} \rfloor + \abs{\sin(\theta)} + g''(\phi)
```

**Current Behavior**: Returns `ERROR` because the parser encounters comment syntax (`//`) which is not valid math notation.

**Workaround**: Create separate files with pure mathematical expressions without comments.

**Priority**: Medium - would require preprocessing to strip comments before math parsing.

#### 2. Multi-Line Mathematical Expressions ❌  
**Issue**: The math parser is designed to parse single mathematical expressions, not multiple expressions across lines.

**Example Problem**:
```
x + y = z
a^2 + b^2 = c^2  
\sin(\theta) + \cos(\theta) = 1
```

**Current Behavior**: Parser processes only the first line and ignores subsequent expressions.

**Expected vs Actual**:
- Expected: Parse all three expressions separately
- Actual: Only parses `x + y = z`, ignores the rest

**Workaround**: 
- Split multi-line files into separate single-expression files
- Or use math environments that support multiple equations (like `align`)

**Priority**: Low - current design focuses on single expression parsing.

#### 3. Comparison Test Files with Mixed Syntax ❌
**Issue**: Test files designed for cross-flavor comparison contain mixed syntax that confuses individual flavor parsers.

**Example Problem File** (`math_abs_comparison.txt`):
```
// Absolute value comparison across flavors
// LaTeX: \abs{x}, \left| y \right|  
// ASCII: abs(x), |y|
// Typst: abs(x), |y|
\abs{5}
```

**Current Behavior**: 
- LaTeX flavor: Returns `ERROR` due to ASCII/Typst comment syntax
- ASCII flavor: Returns `ERROR` due to LaTeX commands in comments
- Typst flavor: Returns `ERROR` due to mixed syntax

**Root Cause**: These files were designed for documentation purposes, not actual parsing tests.

**Workaround**: Create flavor-specific test files:
```
// For LaTeX testing
math_abs_latex_only.txt: \abs{5}

// For ASCII testing  
math_abs_ascii_only.txt: abs(5)

// For Typst testing
math_abs_typst_only.txt: abs(5)
```

**Priority**: Low - these are test infrastructure issues, not core parser problems.

### Parser Architecture Limitations

#### 4. Single Expression Focus 🔍
**Design Choice**: The math parser is architected around parsing single mathematical expressions rather than mathematical documents.

**Implications**:
- Cannot parse mathematical papers or documents with mixed text and math
- Cannot handle mathematical conversations or explanations with embedded expressions
- Focused on expression-to-AST conversion, not document processing

**When This Is Appropriate**: 
- Evaluating mathematical expressions in computational contexts
- Converting single formulas between notation systems
- Building mathematical expression trees for computation

**When This Is Limiting**:
- Processing mathematical documents or papers
- Handling educational content with explanations
- Working with mathematical note-taking applications

#### 5. Comment Syntax Conflicts ⚠️
**Issue**: Mathematical notation systems don't have standardized comment syntax, leading to conflicts.

**Current Status**:
- LaTeX math: No native comment support in math mode
- ASCII math: Uses `//` or `#` for comments (not standardized)
- Typst math: Uses `//` for comments

**Problem**: When files mix comment styles or include explanatory text, parsers fail.

**Priority**: Medium - could be addressed by adding comment preprocessing.

### Error Handling Limitations

#### 6. Limited Error Recovery ❌
**Issue**: When the parser encounters an error, it returns `ERROR` without attempting to parse remaining content.

**Current Behavior**:
```
Input: "x + invalid_syntax + y"
Output: ERROR (entire expression fails)
```

**Desired Behavior**:
```
Input: "x + invalid_syntax + y" 
Output: <add 'x' ERROR 'y'> (partial parsing with error markers)
```

**Priority**: Medium - would improve user experience in interactive applications.

#### 7. Minimal Error Context 📍
**Issue**: Error messages provide minimal context about what went wrong and where.

**Current Messages**:
- "ERROR: Expected \end{equation} to close equation environment"
- Generic `ERROR` return value

**Desired Messages**:
- "ERROR at position 45: Expected \end{equation} to close equation environment opened at position 12"
- "ERROR: Unknown LaTeX command '\invalidcommand' at position 23"

**Priority**: Low - helpful for debugging but not critical for functionality.

### Scope and Feature Limitations

#### 8. Advanced LaTeX Environments ✅/⚠️
**Mixed Support**: LaTeX math environments have varying levels of implementation.

**Currently Fully Supported**:
- ✅ `\begin{equation}...\end{equation}`
- ✅ `\begin{align}...\end{align}` 
- ✅ `\begin{matrix}...\end{matrix}`
- ✅ `\begin{cases}...\end{cases}` (piecewise functions) 
- ✅ `\begin{gather}...\end{gather}` (gathered equations) 

**Partially Supported/Buggy**:
- ⚠️ `\begin{aligned}...\end{aligned}` (inline alignment) - Implemented but has parsing issues with equation right sides
  - **Issue**: "Failed to parse right side of aligned equation 1" error occurs even with simple cases
  - **Status**: Function exists and is called, but has bugs in equation parsing logic

**Limited or Not Supported**:
- ❌ Complex nested environments
- ❌ Advanced matrix variants: `pmatrix`, `bmatrix`, `vmatrix`, etc. beyond basic `matrix`

**Priority**: Medium - these are commonly used in mathematical documents.

#### 9. Advanced Mathematical Notation 📐
**Missing Features**: Many advanced mathematical constructs are not yet implemented.

**High Priority Missing**:
- ✅ Derivatives: `\frac{d}{dx}`, `\partial/\partial x` 
- Complex integrals: `\oint`, `\iint`, `\iiint`
- Advanced set notation: `\mathcal{P}(A)`, `\mathfrak{a}`
- ✅ Vector notation: `\vec{v}`, `\hat{n}`, `\dot{x}` 

**Medium Priority Missing**:
- ✅ Binomial coefficients: `\binom{n}{k}` 
- Continued fractions: `\cfrac{}{}`
- Chemical notation integration
- Advanced typography: `\mathbf{}`, `\mathit{}`

**Priority**: Varies - depends on specific use cases and user requirements.

### Performance Limitations

#### 10. Large Expression Handling 📊
**Issue**: Parser performance degrades with very large mathematical expressions.

**Current Status**: Not systematically tested with large expressions (>1000 tokens).

**Potential Issues**:
- Memory allocation for deep recursion
- String buffer management overhead  
- AST construction for complex trees

**Priority**: Low - most mathematical expressions are relatively small.

### Integration Limitations

#### 11. Document Parser Integration Gaps 🔗
**Issue**: While math parsing is integrated with Markdown and LaTeX document parsers, some edge cases remain.

**Known Issues**:
- Mixed math flavors within single documents
- Math expressions split across line breaks in documents
- Interaction between document structure and math environments

**Priority**: Medium - affects real-world document processing scenarios.

## Workarounds and Best Practices

### For Current Limitations

1. **Mixed Content Files**: 
   - Strip comments before processing: `sed 's|//.*||g' input.txt`
   - Use separate files for pure math expressions

2. **Multi-Line Expressions**:
   - Split into individual expression files
   - Use appropriate math environments (`align`, `gather`)
   - Process line-by-line in application code

3. **Cross-Flavor Testing**:
   - Create flavor-specific test files
   - Use programmatic testing instead of mixed-syntax files
   - Document expected outputs separately

4. **Error Handling**:
   - Validate expressions before parsing when possible
   - Implement application-level error recovery
   - Use fallback parsing strategies

### Recommended Usage Patterns

1. **Single Expression Processing**: ✅ Ideal use case
   ```lambda
   let result = input('./single_formula.txt', {'type': 'math', 'flavor': 'latex'})
   ```

2. **Batch Processing**: ✅ Supported with iteration
   ```lambda  
   let expressions = ["x^2", "\\sin(x)", "\\frac{1}{2}"]
   // Process each expression individually
   ```

3. **Document Integration**: ✅ Works well with proper document parsers
   ```lambda
   let doc = input('./paper.tex', 'latex')  // Uses integrated math parsing
   ```

4. **Interactive Applications**: ⚠️ May need additional error handling
   ```lambda
   // Implement validation layer for user input
   // Handle ERROR returns gracefully  
   // Provide user-friendly error messages
   ```

## Current Limitations
- ❌ Mixed content files with comments are not supported
- ❌ Multi-line mathematical expressions require individual processing  
- ❌ Files designed for comparison tests with mixed syntax cause parser conflicts
- ❌ Limited error recovery for malformed expressions
- ⚠️ Some advanced LaTeX math environments need additional implementation
- 📍 Minimal error context in failure messages

## Next Steps
1. ✅ **Extended mathematical constructs**: ~~Add support for advanced mathematical constructs~~ → **COMPLETED** (July 2025)
   - Added special symbols (ell, hbar, imath, jmath, aleph, beth)
   - Added big operators (bigcup, bigcap, bigoplus, bigotimes, bigwedge, bigvee)  
   - Added advanced fractions (dfrac, tfrac, cfrac with nesting)
   - Added enhanced root functions (cbrt, indexed roots)
2. **Mathematical environments**: Add support for advanced LaTeX math environments
   - `\begin{cases}...\end{cases}` for piecewise functions
   - `\begin{align}...\end{align}` for multi-line equations
   - `\begin{gather}...\end{gather}` for grouped equations
3. **Function notation**: Enhance function parsing and notation
   - Function composition operators
   - Function domain/range notation
   - Advanced function transformations
4. **Set theory**: Expand set notation support
   - Set builder notation: `\{x \mid P(x)\}`
   - Interval notation: `[a,b)`, `(a,b]`
   - Cardinality notation: `|S|`, `\#S`
5. **Error handling**: Improve error reporting and recovery for malformed expressions
   - Better error context in failure messages
   - Graceful handling of incomplete expressions
   - Suggestions for common syntax errors
6. **Performance**: Optimize parsing for large mathematical expressions
   - Reduce memory allocation overhead
   - Optimize string buffer management
   - Cache frequently used symbols

