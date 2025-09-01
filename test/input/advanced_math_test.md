# Advanced Math Expression Test

This file contains complex math expressions that require advanced parser/formatter fixes.
These expressions were moved from indexed_math_test.md after achieving 95% success rate.

## Remaining Issues to Address

### Missing Format Definitions
**Expr 136:** $\twoheadleftarrow$
- Issue: Formats as 'math' instead of proper LaTeX
- Fix needed: Add format definition for \twoheadleftarrow

### Spacing Command Conflicts  
**Expr 150:** $c\:d$
- Issue: \: spacing command parsed as type annotation
- Current: c : d
- Expected: c d (with medium space)

### Integral Formatting
**Expr 161:** $\iint_D f(x,y) dA$
- Issue: Spacing differences in subscripts and arguments
- Current: \iint_{D}f(x, y)dA
- Expected: \iint_D f(x,y) dA

### Matrix Environment Formatting
**Expr 169:** $\begin{matrix} a & b \\ c & d \end{matrix}$
- Issue: Matrix environments converted to function notation
- Current: matrix(row(a, b), row(c, d))
- Expected: \begin{matrix} a & b \\ c & d \end{matrix}

**Expr 170:** $\begin{pmatrix} 1 & 2 \\ 3 & 4 \end{pmatrix}$
- Issue: Parenthesized matrix environment formatting
- Current: pmatrix(row(1, 2), row(3, 4))

**Expr 171:** $\begin{bmatrix} x & y \\ z & w \end{bmatrix}$
- Issue: Bracketed matrix environment formatting
- Current: bmatrix(row(x, y), row(z, w))

**Expr 172:** $\begin{smallmatrix} p & q \\ r & s \end{smallmatrix}$
- Issue: Small matrix environment formatting
- Current: smallmatrix(row(p, q), row(r, s))

### Complex Cases Environment
**Expr 173:** $f(x) = \begin{cases} x^2 & \text{if } x \geq 0 \\ -x^2 & \text{if } x < 0 \end{cases}$
- Issue: Cases environment with complex conditions
- Current: f(x) = cases(case(x^2, \text{if}x \geq 0), case(-x^2, \text{if}x < 0), case(ases))

### Matrix Multiplication
**Expr 175:** $\begin{bmatrix} \cos\theta & -\sin\theta \\ \sin\theta & \cos\theta \end{bmatrix} \begin{bmatrix} x \\ y \end{bmatrix} = \begin{bmatrix} x' \\ y' \end{bmatrix}$
- Issue: Complex matrix multiplication with trigonometric functions

### Limit Expressions
**Expr 176:** $\lim_{h \to 0} \frac{f(x+h) - f(x)}{h} = f'(x)$
- Issue: Limit with complex fraction and derivative notation

## Priority Analysis

**High Priority (Structural Issues):**
- Matrix environment preservation (Expr 169-172, 175)
- Cases environment parsing (Expr 173)
- Missing format definitions (Expr 136)

**Medium Priority (Spacing/Formatting):**
- Spacing command conflicts (Expr 150)
- Integral formatting (Expr 161)
- Limit expression formatting (Expr 176)

## Implementation Strategy

1. **Matrix Environment Preservation**: Modify formatter to preserve LaTeX matrix syntax instead of converting to function notation
2. **Spacing Command Resolution**: Distinguish between \: as spacing vs : as type annotation based on context
3. **Format Definition Completion**: Add remaining missing arrow and symbol definitions
4. **Environment Parsing Enhancement**: Improve cases and matrix environment parsing logic

Total expressions to fix: 10
Current success rate: 95% (167/177)
Target success rate: 98%+ (174+/177)
