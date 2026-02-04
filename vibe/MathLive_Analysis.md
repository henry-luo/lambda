# MathLive Architecture Analysis & Lessons for Lambda

## Overview

This document compares Lambda's math implementation with MathLive to identify improvements.

## Architecture Comparison

### MathLive (TypeScript)
```
LaTeX String → Parser → Atom Tree → Box Tree → HTML/MathML
```
- **Atom System**: Strongly typed atom classes for each construct
- **Box Model**: CSS-based rendering with explicit height/depth/width
- **Font Handling**: Unicode-first with font metrics lookup
- **Styling**: Cascade-based style inheritance through Context

### Lambda (C++)
```
LaTeX String → Tree-sitter → Lambda Element → MathASTNode → TexNode → HTML/DVI
```
- **Two-Phase Pipeline**: AST building → Typesetting (inspired by MathLive!)
- **TexNode**: TeX-traditional box model with TFM metrics
- **Font Handling**: TFM-based (Computer Modern fonts)
- **Styling**: Style parameters passed through MathContext

## Key Learnings

### 1. **Unicode-First Symbol Mapping** (Priority: HIGH)

MathLive uses Unicode codepoints as the primary symbol representation:
```typescript
// MathLive: symbols.ts
defineSymbols([
  ['\\forall', 0x2200],
  ['\\exists', 0x2203],
  ['\\rightarrow', 0x2192],
  ...
]);
```

Lambda currently uses font-specific codes (cmsy10, cmmi10):
```cpp
// Lambda: tex_math_ast_typeset.cpp  
{"leq", 20, AtomType::Rel, SymFont::CMSY},  // Font position
```

**Recommendation**: Add Unicode codepoint mapping alongside font codes for HTML output.
This would make HTML output more portable and less font-dependent.

### 2. **Extensible Delimiter System** (Priority: HIGH)

MathLive has sophisticated delimiter scaling in `delimiters.ts`:
- `makeSmallDelim()`: Normal font delimiters
- `makeLargeDelim()`: Size1-4 fonts
- `makeStackedDelim()`: Assembled from pieces (top/middle/repeat/bottom)

Lambda's delimiter handling is simpler. Consider:
- Adding delimiter stack assembly for very tall expressions
- Proper Size1-4 font selection based on content height

### 3. **Context-Based Style Inheritance** (Priority: MEDIUM)

MathLive's `Context` class provides clean style cascading:
```typescript
class Context {
  readonly parent?: Context;
  readonly mathstyle: Mathstyle;
  readonly color: string;
  readonly backgroundColor: string;
  readonly size: FontSize;
  // ... inherits from parent when not specified
}
```

Lambda's `MathContext` could adopt this pattern more fully for:
- Better color inheritance
- Cleaner font size propagation
- Style isolation in nested structures

### 4. **Atom Type System** (Priority: MEDIUM)

MathLive has dedicated atom classes per construct:
- `GenfracAtom` - fractions with all options
- `ExtensibleSymbolAtom` - big operators
- `OperatorAtom` - text operators (\sin, \log)
- `AccentAtom` - math accents
- `ArrayAtom` - matrices/arrays

Lambda's `MathASTNode` uses a union struct approach. Both are valid, but
MathLive's approach provides:
- Better type safety
- Cleaner serialization (toJson/fromJson)
- Easier extension for new constructs

### 5. **Font Variant System** (Priority: HIGH for styling tests)

MathLive has explicit font variant support:
```typescript
type Variant = 'normal' | 'bold' | 'italic' | 'bold-italic' | 
               'calligraphic' | 'fraktur' | 'double-struck' | 
               'script' | 'monospace' | 'sans-serif';
```

Lambda needs better mapping for:
- `\mathbf`, `\mathit`, `\mathsf`, etc.
- `\mathrm` (upright)
- `\mathcal`, `\mathfrak`, `\mathbb`

**Current Lambda weakness**: 58.9% on fonts tests suggests this needs work.

### 6. **VList Structure** (Already Adopted!)

Lambda already uses MathLive-compatible vlist structure:
```
ML__vlist-t [ML__vlist-t2]
├── ML__vlist-r
│   └── ML__vlist (height:Xem)
│       └── span (top:-Yem)
│           └── ML__pstrut
```

This is good! Continue maintaining compatibility.

### 7. **Array/Matrix Column Specification** (Priority: MEDIUM)

MathLive's `ColumnFormat` type:
```typescript
type ColumnFormat =
  | { align: 'l' | 'c' | 'r' | 'm' }
  | { gap: number | readonly Atom[] }
  | { separator: 'solid' | 'dashed' };
```

Lambda should ensure full support for:
- All alignment types including 'm' (multline)
- Column separators (vertical rules)
- Custom column gaps

**Current Lambda weakness**: 57.0% on arrays tests.

### 8. **Limits Placement Logic** (Priority: MEDIUM)

MathLive's `ExtensibleSymbolAtom.render()`:
```typescript
let limits = this.subsupPlacement ?? 'auto';
if (limits === 'auto' && context.isDisplayStyle) limits = 'over-under';
result = limits === 'over-under'
  ? this.attachLimits(context, { base, baseShift, slant })
  : this.attachSupsub(context, { base });
```

This clean logic for display/inline limits switching should be verified in Lambda.

## Specific Improvement Recommendations

### Short-term (Improve test scores quickly)

1. **Add Unicode HTML output mode**: Output symbols as Unicode characters
   in HTML instead of font-specific codes. This improves portability.

2. **Fix font command handling**: Properly handle `\mathbf`, `\mathrm`, etc.
   Map to appropriate CSS font-family/weight/style.

3. **Improve array environment support**: 
   - Handle `\hline` properly
   - Support `@{}` column specification
   - Handle `\\[Xpt]` row spacing

4. **Add \textcolor support**: Lambda tests show styles at 61.5%.
   Need proper color command handling.

### Medium-term (Architectural improvements)

5. **Add Unicode symbol table**: Parallel to TFM codes, maintain Unicode
   mappings for web output.

6. **Implement proper delimiter sizing algorithm**: 
   Based on content height, select appropriate size variant.

7. **Clean up Context/MathContext**: Make style inheritance more explicit.

### Long-term (Feature parity)

8. **Full AMS package support**: `\mathbb`, `\mathfrak`, etc.

9. **mhchem support**: Chemical equation typesetting.

10. **Better error recovery**: MathLive continues rendering with errors marked.

## Test Score Analysis

| Category | Lambda | Issue |
|----------|--------|-------|
| operators | 87.7% | Good |
| greek | 83.8% | Good |
| fracs | 81.7% | Good |
| bigops | 79.6% | Good |
| **styles** | **61.5%** | Need \textcolor, font styles |
| **fonts** | **58.9%** | Need \mathbf, \mathrm, etc. |
| **arrays** | **57.0%** | Need \hline, column specs |
| **misc** | **56.4%** | Mixed issues |

## Specific Bug: Font Variant Commands Not Implemented

**Critical Finding**: Lambda's AST builder creates STYLE nodes for font variants:
```cpp
// tex_math_ast_builder.cpp line 2632-2636
if (strncmp(cmd, "math", 4) == 0) {
    return make_math_style(arena, 4, cmd_str, body);  // 4=font variant
}
```

But the typesetter **only handles types 0-3**:
```cpp
// tex_math_ast_typeset.cpp line 581-586
switch (node->style.style_type) {
    case 0: ctx.style = MathStyle::Display; break;
    case 1: ctx.style = MathStyle::Text; break;
    case 2: ctx.style = MathStyle::Script; break;
    case 3: ctx.style = MathStyle::ScriptScript; break;
    // MISSING: case 4 (font variant) and case 5 (operatorname)!
}
```

**Fix Required**: Add handling for:
- `case 4`: Font variants (`\mathbf`, `\mathrm`, `\mathit`, `\mathfrak`, `\mathbb`, `\mathcal`, `\mathscr`, `\mathsf`, `\mathtt`)
- `case 5`: `\operatorname`

This should look at `node->style.command` to determine which font to use:
- `mathbf` → bold font (cmbx10)
- `mathrm` → roman font (cmr10)  
- `mathit` → italic font (cmmi10)
- `mathsf` → sans-serif (not in CM, need Unicode fallback)
- `mathtt` → typewriter (cmtt10)
- `mathcal` → calligraphic (cmsy10 for capitals)
- `mathbb` → blackboard bold (msbm10 or Unicode)
- `mathfrak` → fraktur (not in CM, need Unicode fallback)

## Conclusion

Lambda's math implementation has solid foundations (good AST design, MathLive-compatible HTML).
The main gaps are:
1. **Font variant commands** - **BUG**: AST creates them but typesetter ignores types 4 and 5
2. **Array environment features** - impacts 'arrays' tests  
3. **Color commands** - impacts 'styles' tests

**Immediate fix**: Add `case 4` and `case 5` handling in `tex_math_ast_typeset.cpp` around line 581.

Focusing on these three areas would likely raise the overall pass rate significantly.
