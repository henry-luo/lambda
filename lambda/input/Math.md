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

## Test Coverage

The math parser has comprehensive test coverage including:

### Basic Test Suite (`test_math.ls`)
- Operators: `+`, `-`, `*`, `/`, `^`, `%`
- Parentheses and precedence
- Variable references and symbols
- Error handling for malformed expressions

### Advanced Test Suite (`test_math_advanced.ls`)

#### Typst Flavor Tests
- **Typst Fractions**: `frac(a, b)` syntax
  - Basic fractions: `frac(x, y)` → `<frac 'x' 'y'>`
  - Nested fractions: `frac(a, frac(b, c))` → `<frac 'a' (<frac 'b' 'c'>)>`
  - Complex expressions: `frac(x + 1, y - 2)` → `<frac (<+ 'x' '1'>) (<- 'y' '2'>)>`

- **Typst Function Calls**: `sqrt(x)`, `sin(y)`, etc.
  - Square root: `sqrt(x)` → `<sqrt 'x'>`
  - Trigonometric: `sin(theta)`, `cos(phi)`, `tan(alpha)`
  - Logarithmic: `log(n)`, `ln(x)`

#### ASCII Flavor Tests  
- **ASCII Function Calls**: Standard math notation
  - Square root: `sqrt(x)` → `<sqrt 'x'>`
  - Trigonometry: `sin(theta)`, `cos(phi)`, `tan(alpha)`
  - Logarithms: `log(n)`, `ln(x)`
  - Power notation: `pow(x, 2)` → `<pow 'x' '2'>`

#### Mixed Expression Tests
Complex expressions combining multiple features:
- `frac(sin(x), cos(y))` → `<frac (<sin 'x'>) (<cos 'y'>)>`
- `sqrt(x^2 + y^2)` → `<sqrt (<+ (<pow 'x' '2'>) (<pow 'y' '2'>))>`

#### Multi-Flavor Comparison
Tests ensuring consistent parsing across flavors where syntax overlaps.

### Current Test Status
- ✅ Typst fractions: `frac(a, b)` fully supported
- ✅ Typst function calls: all tested functions parse correctly
- ✅ ASCII function calls: all function calls parse correctly (**FIXED**)
- ✅ ASCII power operations: both `^` and `**` operators fully supported (**ENHANCED**)
- ✅ Number parsing: numbers are now parsed as Lambda integers/floats instead of strings (**ENHANCED**)
- ✅ Mixed expressions: complex combinations parse correctly
- ✅ Multi-flavor: consistent behavior across flavors

### Test Files
- Input files located in `test/input/`
- Test scripts in `test/lambda/input/`
- Run with: `./lambda.exe test/lambda/input/test_math_advanced.ls`

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

## Recent Enhancements

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
### Phase 7: Advanced LaTeX Math Features ✅
### Phase 7: Advanced LaTeX Math Features ✅
- ✅ Enhanced Greek letter support (full alphabet: α, β, γ, δ, ε, ..., Ω)
- ✅ Mathematical operators (∞, ∂, ∇, ⋅, ×, ÷, ±, ∓, ≤, ≥, ≠, ≈, etc.)
- ✅ Advanced trigonometric functions (arcsin, arccos, arctan, sinh, cosh, tanh)
- ✅ Hyperbolic and inverse functions (cot, sec, csc, etc.)
- ✅ Sum notation with limits: `\sum_{i=1}^{n} expression`
- ✅ Product notation with limits: `\prod_{k=0}^{\infty} expression` 
- ✅ Integral notation with limits: `\int_{a}^{b} f(x) dx`
- ✅ Limit expressions: `\lim_{x \to 0} f(x)`
- ✅ Basic matrix support: `\matrix{a & b \\ c & d}`
- ✅ Parenthesized matrices: `\pmatrix{1 & 2 \\ 3 & 4}`
- ✅ Bracketed matrices: `\bmatrix{x \\ y \\ z}`

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

## Test Files Structure

### Essential Tests
- `test/lambda/input/test_math_basic.ls` - Basic LaTeX math parser testing
- `test/lambda/input/test_math_complete.ls` - Comprehensive LaTeX feature testing
- `test/lambda/input/test_math_advanced.ls` - Enhanced Typst and ASCII feature testing
- `test/lambda/input/test_integration.ls` - Document parser integration testing

### Input Test Files
- `test/input/math_simple.txt` - Basic LaTeX math expressions
- `test/input/math_latex_basic.txt` - LaTeX mathematical functions
- `test/input/math_typst_frac.txt` - Typst frac() function syntax
- `test/input/math_typst_funcs.txt` - Typst function call examples
- `test/input/math_typst_mixed.txt` - Complex Typst expressions
- `test/input/math_ascii_funcs.txt` - ASCII function call notation (**Fixed**)
- `test/input/math_ascii_power.txt` - ASCII power operator examples (**Enhanced**)
- `test/input/math_ascii_mixed.txt` - Complex ASCII expressions
- `test/input/math_comparison.txt` - Multi-flavor comparison tests
- `test/input/test_markdown_math.md` - Markdown document with embedded math
- `test/input/test_latex_math.tex` - LaTeX document with math expressions

## Supported Math Expression Mappings

The following table lists all math expressions supported by Lambda's math parser and their mappings across different notation systems:

| Lambda Expression                    | LaTeX                                     | Typst                         | ASCII                  | Description          |
| ------------------------------------ | ----------------------------------------- | ----------------------------- | ---------------------- | -------------------- |
| `<add 'a' 'b'>`                      | `a + b`                                   | `a + b`                       | `a + b`                | Addition             |
| `<sub 'a' 'b'>`                      | `a - b`                                   | `a - b`                       | `a - b`                | Subtraction          |
| `<mul 'a' 'b'>`                      | `a * b`                                   | `a * b`                       | `a * b`                | Multiplication       |
| `<div 'a' 'b'>`                      | `a / b`                                   | `a / b`                       | `a / b`                | Division             |
| `<frac 'a' 'b'>`                     | `\frac{a}{b}`                             | `frac(a, b)` or `a/b`         | `a/b`                  | Fraction             |
| `<pow 'x' 'n'>`                      | `x^{n}`                                   | `x^n`                         | `x^n` or `x**n`        | Exponentiation       |
| `<sqrt 'x'>`                         | `\sqrt{x}`                                | `sqrt(x)`                     | `sqrt(x)`              | Square root          |
| `<sin 'x'>`                          | `\sin{x}`                                 | `sin(x)`                      | `sin(x)`               | Sine function        |
| `<cos 'x'>`                          | `\cos{x}`                                 | `cos(x)`                      | `cos(x)`               | Cosine function      |
| `<tan 'x'>`                          | `\tan{x}`                                 | `tan(x)`                      | `tan(x)`               | Tangent function     |
| `<arcsin 'x'>`                       | `\arcsin{x}`                              | `arcsin(x)`                   | `arcsin(x)`            | Arcsine function     |
| `<arccos 'x'>`                       | `\arccos{x}`                              | `arccos(x)`                   | `arccos(x)`            | Arccosine function   |
| `<arctan 'x'>`                       | `\arctan{x}`                              | `arctan(x)`                   | `arctan(x)`            | Arctangent function  |
| `<sinh 'x'>`                         | `\sinh{x}`                                | `sinh(x)`                     | `sinh(x)`              | Hyperbolic sine      |
| `<cosh 'x'>`                         | `\cosh{x}`                                | `cosh(x)`                     | `cosh(x)`              | Hyperbolic cosine    |
| `<tanh 'x'>`                         | `\tanh{x}`                                | `tanh(x)`                     | `tanh(x)`              | Hyperbolic tangent   |
| `<cot 'x'>`                          | `\cot{x}`                                 | `cot(x)`                      | `cot(x)`               | Cotangent function   |
| `<sec 'x'>`                          | `\sec{x}`                                 | `sec(x)`                      | `sec(x)`               | Secant function      |
| `<csc 'x'>`                          | `\csc{x}`                                 | `csc(x)`                      | `csc(x)`               | Cosecant function    |
| `<log 'x'>`                          | `\log{x}`                                 | `log(x)`                      | `log(x)`               | Logarithm (base 10)  |
| `<ln 'x'>`                           | `\ln{x}`                                  | `ln(x)`                       | `ln(x)`                | Natural logarithm    |
| `<sum 'start' 'end' 'expr'>`         | `\sum_{start}^{end} expr`                 | `sum_(start)^(end) expr`      | `sum(start,end,expr)`  | Summation            |
| `<prod 'start' 'end' 'expr'>`        | `\prod_{start}^{end} expr`                | `product_(start)^(end) expr`  | `prod(start,end,expr)` | Product              |
| `<int 'start' 'end' 'expr'>`         | `\int_{start}^{end} expr`                 | `integral_(start)^(end) expr` | `int(start,end,expr)`  | Definite integral    |
| `<lim 'approach' 'expr'>`            | `\lim_{approach} expr`                    | `lim_(approach) expr`         | `lim(approach,expr)`   | Limit                |
| `<matrix 'rows'>`                    | `\matrix{...}`                            | `mat(...)`                    | `matrix(...)`          | Matrix               |
| `<pmatrix 'rows'>`                   | `\pmatrix{...}`                           | `mat(...)`                    | `pmatrix(...)`         | Parenthesized matrix |
| `<bmatrix 'rows'>`                   | `\bmatrix{...}`                           | `mat(...)`                    | `bmatrix(...)`         | Bracketed matrix     |
| `<cases 'conditions'>`               | `\begin{cases}...\end{cases}`             | `cases(...)`                  | `cases(...)`           | Piecewise function   |
| `<align 'equations'>`                | `\begin{align}...\end{align}`             | `align(...)`                  | `align(...)`           | Aligned equations    |
| `<equation 'expr'>`                  | `\begin{equation}...\end{equation}`       | `equation(...)`               | `equation(...)`        | Numbered equation    |
| `<gather 'equations'>`               | `\begin{gather}...\end{gather}`           | `gather(...)`                 | `gather(...)`          | Gathered equations   |
| `<smallmatrix 'rows'>`               | `\begin{smallmatrix}...\end{smallmatrix}` | `smallmat(...)`               | `smallmatrix(...)`     | Small matrix         |

### Greek Letters
| Lambda Symbol | LaTeX | Typst | ASCII | Description |
|---------------|--------|-------|-------|-------------|
| `'alpha'` | `\alpha` | `alpha` | `alpha` | Greek letter α |
| `'beta'` | `\beta` | `beta` | `beta` | Greek letter β |
| `'gamma'` | `\gamma` | `gamma` | `gamma` | Greek letter γ |
| `'delta'` | `\delta` | `delta` | `delta` | Greek letter δ |
| `'epsilon'` | `\epsilon` | `epsilon` | `epsilon` | Greek letter ε |
| `'zeta'` | `\zeta` | `zeta` | `zeta` | Greek letter ζ |
| `'eta'` | `\eta` | `eta` | `eta` | Greek letter η |
| `'theta'` | `\theta` | `theta` | `theta` | Greek letter θ |
| `'iota'` | `\iota` | `iota` | `iota` | Greek letter ι |
| `'kappa'` | `\kappa` | `kappa` | `kappa` | Greek letter κ |
| `'lambda'` | `\lambda` | `lambda` | `lambda` | Greek letter λ |
| `'mu'` | `\mu` | `mu` | `mu` | Greek letter μ |
| `'nu'` | `\nu` | `nu` | `nu` | Greek letter ν |
| `'xi'` | `\xi` | `xi` | `xi` | Greek letter ξ |
| `'omicron'` | `\omicron` | `omicron` | `omicron` | Greek letter ο |
| `'pi'` | `\pi` | `pi` | `pi` | Greek letter π |
| `'rho'` | `\rho` | `rho` | `rho` | Greek letter ρ |
| `'sigma'` | `\sigma` | `sigma` | `sigma` | Greek letter σ |
| `'tau'` | `\tau` | `tau` | `tau` | Greek letter τ |
| `'upsilon'` | `\upsilon` | `upsilon` | `upsilon` | Greek letter υ |
| `'phi'` | `\phi` | `phi` | `phi` | Greek letter φ |
| `'chi'` | `\chi` | `chi` | `chi` | Greek letter χ |
| `'psi'` | `\psi` | `psi` | `psi` | Greek letter ψ |
| `'omega'` | `\omega` | `omega` | `omega` | Greek letter ω |
| `'Gamma'` | `\Gamma` | `Gamma` | `Gamma` | Greek letter Γ |
| `'Delta'` | `\Delta` | `Delta` | `Delta` | Greek letter Δ |
| `'Theta'` | `\Theta` | `Theta` | `Theta` | Greek letter Θ |
| `'Lambda'` | `\Lambda` | `Lambda` | `Lambda` | Greek letter Λ |
| `'Xi'` | `\Xi` | `Xi` | `Xi` | Greek letter Ξ |
| `'Pi'` | `\Pi` | `Pi` | `Pi` | Greek letter Π |
| `'Sigma'` | `\Sigma` | `Sigma` | `Sigma` | Greek letter Σ |
| `'Upsilon'` | `\Upsilon` | `Upsilon` | `Upsilon` | Greek letter Υ |
| `'Phi'` | `\Phi` | `Phi` | `Phi` | Greek letter Φ |
| `'Psi'` | `\Psi` | `Psi` | `Psi` | Greek letter Ψ |
| `'Omega'` | `\Omega` | `Omega` | `Omega` | Greek letter Ω |

### Mathematical Operators
| Lambda Symbol | LaTeX | Typst | ASCII | Description |
|---------------|--------|-------|-------|-------------|
| `'infty'` | `\infty` | `infinity` | `inf` | Infinity symbol ∞ |
| `'partial'` | `\partial` | `diff` | `d` | Partial derivative ∂ |
| `'nabla'` | `\nabla` | `nabla` | `nabla` | Nabla operator ∇ |
| `'cdot'` | `\cdot` | `dot` | `*` | Center dot ⋅ |
| `'times'` | `\times` | `times` | `x` | Times symbol × |
| `'div'` | `\div` | `div` | `/` | Division symbol ÷ |
| `'pm'` | `\pm` | `+-` | `+-` | Plus-minus ± |
| `'mp'` | `\mp` | `-+` | `-+` | Minus-plus ∓ |
| `'leq'` | `\leq` | `<=` | `<=` | Less than or equal ≤ |
| `'geq'` | `\geq` | `>=` | `>=` | Greater than or equal ≥ |
| `'neq'` | `\neq` | `!=` | `!=` | Not equal ≠ |
| `'approx'` | `\approx` | `~~` | `~` | Approximately equal ≈ |

### Math Expressions Not Yet Supported in Lambda

The following table lists mathematical expressions that are available in other notation systems but are not yet implemented in Lambda's math parser:

| LaTeX | Typst | ASCII | Description | Priority |
|--------|-------|-------|-------------|----------|
| `\frac{d}{dx}` | `diff(f,x)` | `d/dx` | Derivative notation | High |
| `\int f(x) dx` | `integral f(x) dif x` | `integral(f,x)` | Indefinite integral | High |
| `\oint` | `integral.cont` | `contour_int` | Contour integral | Medium |
| `\sum_{i=1}^{\infty}` | `sum_(i=1)^infinity` | `sum(i=1,inf)` | Infinite series | High |
| `\lim_{x \to \infty}` | `lim_(x -> infinity)` | `lim(x->inf)` | Limit to infinity | High |
| `\binom{n}{k}` | `binom(n,k)` | `C(n,k)` | Binomial coefficient | Medium |
| `\choose` | `choose` | `choose` | Alternative binomial | Low |
| `\overline{x}` | `overline(x)` | `mean(x)` | Mean/average notation | Medium |
| `\hat{x}` | `hat(x)` | `x_hat` | Estimator notation | Medium |
| `\tilde{x}` | `tilde(x)` | `x~` | Approximation notation | Low |
| `\vec{v}` | `arrow(v)` | `vec(v)` | Vector notation | Medium |
| `\dot{x}` | `dot(x)` | `x_dot` | Time derivative | Medium |
| `\ddot{x}` | `dot.double(x)` | `x_ddot` | Second derivative | Medium |
| `\bar{x}` | `macron(x)` | `x_bar` | Complex conjugate | Low |
| `\mathbb{R}` | `RR` | `R` | Real numbers | High |
| `\mathbb{N}` | `NN` | `N` | Natural numbers | High |
| `\mathbb{Z}` | `ZZ` | `Z` | Integers | High |
| `\mathbb{Q}` | `QQ` | `Q` | Rational numbers | High |
| `\mathbb{C}` | `CC` | `C` | Complex numbers | High |
| `\in` | `in` | `in` | Element of | High |
| `\notin` | `in.not` | `not_in` | Not element of | Medium |
| `\subset` | `subset` | `subset` | Subset | Medium |
| `\supset` | `supset` | `superset` | Superset | Medium |
| `\cup` | `union` | `union` | Set union | Medium |
| `\cap` | `sect` | `intersect` | Set intersection | Medium |
| `\emptyset` | `nothing` | `empty_set` | Empty set | Medium |
| `\forall` | `forall` | `forall` | For all quantifier | Medium |
| `\exists` | `exists` | `exists` | Exists quantifier | Medium |
| `\land` | `and` | `and` | Logical AND | Medium |
| `\lor` | `or` | `or` | Logical OR | Medium |
| `\neg` | `not` | `not` | Logical NOT | Medium |
| `\Rightarrow` | `=>` | `=>` | Implies | Medium |
| `\Leftrightarrow` | `<=>` | `<=>` | If and only if | Medium |
| `\leftarrow` | `<-` | `<-` | Left arrow | Low |
| `\rightarrow` | `->` | `->` | Right arrow | Low |
| `\uparrow` | `arrow.t` | `up` | Up arrow | Low |
| `\downarrow` | `arrow.b` | `down` | Down arrow | Low |
| `\parallel` | `parallel` | `parallel` | Parallel | Low |
| `\perp` | `perp` | `perp` | Perpendicular | Low |
| `\angle` | `angle` | `angle` | Angle | Low |
| `\triangle` | `triangle` | `triangle` | Triangle | Low |
| `\square` | `square` | `square` | Square | Low |
| `\diamond` | `diamond` | `diamond` | Diamond | Low |
| `\circ` | `circle` | `compose` | Function composition | Medium |
| `\bullet` | `dot.op` | `bullet` | Bullet operator | Low |
| `\star` | `star` | `star` | Star operator | Low |
| `\ast` | `*` | `*` | Asterisk operator | Low |
| `\oplus` | `plus.circle` | `xor` | Exclusive or | Low |
| `\otimes` | `times.circle` | `tensor` | Tensor product | Low |
| `\odot` | `dot.circle` | `dot_prod` | Dot product | Low |
| `\oslash` | `slash.circle` | `oslash` | Circled slash | Low |
| `\boxplus` | `plus.square` | `boxplus` | Boxed plus | Low |
| `\boxtimes` | `times.square` | `boxtimes` | Boxed times | Low |
| `\equiv` | `equiv` | `equiv` | Equivalent | Medium |
| `\cong` | `tilde.equiv` | `congruent` | Congruent | Low |
| `\sim` | `~` | `similar` | Similar | Low |
| `\simeq` | `tilde.eq` | `simeq` | Similar equal | Low |
| `\propto` | `prop` | `proportional` | Proportional | Low |
| `\models` | `tack.r` | `models` | Models | Low |
| `\vdash` | `tack.r` | `proves` | Proves | Low |
| `\dashv` | `tack.l` | `dashv` | Dashv | Low |
| `\top` | `top` | `true` | True/top | Low |
| `\bot` | `bot` | `false` | False/bottom | Low |
| `\lceil x \rceil` | `ceil(x)` | `ceil(x)` | Ceiling function | Medium |
| `\lfloor x \rfloor` | `floor(x)` | `floor(x)` | Floor function | Medium |
| `\left\| x \right\|` | `abs(x)` | `abs(x)` | Absolute value | High |
| `\left\langle x,y \right\rangle` | `angle.l x,y angle.r` | `<x,y>` | Inner product | Medium |
| `f'(x)` | `f'(x)` | `f_prime(x)` | Prime notation | High |
| `f''(x)` | `f''(x)` | `f_double_prime(x)` | Double prime | High |
| `f^{(n)}(x)` | `f^((n))(x)` | `f_n(x)` | nth derivative | Medium |
| `\partial f/\partial x` | `diff(f,x)` | `df_dx` | Partial derivative | High |
| `\nabla f` | `grad f` | `grad(f)` | Gradient | Medium |
| `\nabla \cdot \vec{F}` | `div arrow(F)` | `div(F)` | Divergence | Medium |
| `\nabla \times \vec{F}` | `curl arrow(F)` | `curl(F)` | Curl | Medium |
| `\Delta` | `laplace` | `laplacian` | Laplacian | Low |

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

### New Lambda Math Format (Refactored)
```
Input: "1 + 2 * 3"           → <add "1" <mul "2" "3">>
Input: "\frac{1}{2}"         → <frac "1" "2">
Input: "x^{2}"               → <pow 'x' "2">
Input: "\sin{x} + \cos{y}"   → <add <sin 'x'> <cos 'y'>>
Input: "(2 + 3) * 4"         → <mul <add "2" "3"> "4">
```

### Enhanced LaTeX Math with New Format
```
Input: "\alpha + \beta"      → <add 'alpha' 'beta'>
Input: "x * y"               → <mul 'x' 'y'>
Input: "\sum_{i=1}^{n} i"    → <sum <assign 'i' "1"> 'n' 'i'>
Input: "\int_{0}^{1} x dx"   → <int "0" "1" 'x'>
Input: "\lim_{x \to 0} f(x)" → <lim <to 'x' "0"> <f 'x'>>
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
- ✅ **API Enhancement**: Map-based options with full backward compatibility
- ✅ **Multi-Flavor**: Advanced Typst and ASCII support with function call notation
- ✅ **Integration**: Fully integrated with Lambda input system and type system
- ✅ **Document Integration**: Math parsing integrated into Markdown and LaTeX document parsers
- ✅ **Code Refactoring**: Shared utilities between input parsers for better maintainability
- ✅ **Testing**: Comprehensive test coverage for core functionality and integration

## Current Limitations
- Some edge cases in ASCII function parsing need refinement
- Matrix parsing is simplified (doesn't support full `\begin{matrix}...\end{matrix}` environments) [Note: Actually implemented]
- No error recovery for malformed expressions
- Some advanced LaTeX math environments not yet fully supported

## Next Steps
1. **ASCII parsing refinement**: Fix edge cases in ASCII function call parsing
2. **Error handling**: Improve error reporting and recovery for malformed expressions
3. **Performance**: Optimize parsing for large mathematical expressions
4. **Extended syntax**: Add support for advanced mathematical constructs
5. **Enhanced LaTeX environments**: Complete support for complex math environments

## Test Coverage

### Enhanced Multi-Flavor Tests ✅
- **Typst Advanced Features**: ✅ (`frac(a, b)` syntax, function calls)
- **ASCII Function Calls**: ✅ (`sqrt(x)`, `sin(x)`, `cos(x)`, `log(x)`)
- **Multi-flavor Comparison**: ✅ (same expression in all three flavors)
- **Power Operations**: ✅ (both `^` and `**` in ASCII)
- **Mixed Expressions**: ✅ (complex expressions combining multiple features)

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
