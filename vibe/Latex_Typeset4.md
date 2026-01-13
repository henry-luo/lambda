# LaTeX Typesetting Pipeline Phase 4: Tree-sitter Math Integration

## Overview

Replace the hand-written recursive descent math parser in `tex_math_bridge.cpp` with a tree-sitter based parser using the `tree-sitter-latex-math` grammar, while preserving the TeX typesetting infrastructure (TFM metrics, atom spacing, etc.).

## Current State Analysis

### Hand-written Parser (`tex_math_bridge.cpp`)

**Location:** `lambda/tex/tex_math_bridge.cpp` - `parse_latex_math_internal()` (~1500 lines)

**Strengths:**
- Directly produces `TexNode` trees (no intermediate representation)
- Integrated TFM font metrics for accurate glyph sizing
- Correct inter-atom spacing per TeXBook Chapter 18 rules
- Handles all major constructs: fractions, radicals, accents, delimiters, scripts
- `MathContext` provides style-aware font sizing

**Weaknesses:**
- 1500+ lines of manual character-by-character parsing
- Hard to maintain and extend
- Error recovery is ad-hoc (silent failures)
- Duplicates parsing logic that tree-sitter handles well
- Adding new constructs requires significant code changes

### Tree-sitter Parser (`input-math2.cpp`)

**Location:** `lambda/input/input-math2.cpp` - MathLive pipeline parser

**Strengths:**
- Clean grammar-based parsing via `tree-sitter-latex-math`
- Robust error recovery built into tree-sitter
- Well-structured CST with named fields
- Separate `MathNodeBuilder` for clean AST construction
- Easy to extend via grammar rules

**Weaknesses:**
- Produces Lambda `Item` trees (MathNode) rather than `TexNode`
- Missing TFM metrics integration
- Designed for MathLive rendering, not TeX typesetting
- Extra conversion step would be needed

---

## Proposed Design: Two-Phase Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                     LaTeX Math String Input                         │
│                    "$x^2 + \\frac{a}{b}$"                          │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│ Phase 1: PARSING (tree-sitter-latex-math)                          │
│ ┌─────────────────────────────────────────────────────────────────┐ │
│ │ TSParser → CST (Concrete Syntax Tree)                          │ │
│ │   - subsup { base: symbol, sup: group }                        │ │
│ │   - fraction { numer: group, denom: group }                    │ │
│ └─────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│ Phase 2: TYPESETTING (MathTypesetter)                              │
│ ┌─────────────────────────────────────────────────────────────────┐ │
│ │ CST Node → TexNode (with TFM metrics)                          │ │
│ │   - Look up TFM glyph metrics                                  │ │
│ │   - Apply atom spacing rules                                   │ │
│ │   - Position sub/superscripts                                  │ │
│ │   - Build extensible delimiters                                │ │
│ └─────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    TexNode Tree (Ready for DVI/PDF)                │
└─────────────────────────────────────────────────────────────────────┘
```

---

## New File: `tex_math_ts.cpp`

### Header (`tex_math_ts.hpp`)

```cpp
// tex_math_ts.hpp - Tree-sitter based math typesetter
//
// Parses LaTeX math using tree-sitter-latex-math grammar and
// directly produces TexNode trees with proper TFM metrics.

#ifndef TEX_MATH_TS_HPP
#define TEX_MATH_TS_HPP

#include "tex_math_bridge.hpp"
#include "tex_node.hpp"

namespace tex {

// Main entry point - parse LaTeX math and produce TexNode tree
// This replaces parse_latex_math_internal()
TexNode* typeset_latex_math_ts(const char* latex_str, size_t len, MathContext& ctx);

} // namespace tex

#endif // TEX_MATH_TS_HPP
```

### Core Structure

```cpp
// tex_math_ts.cpp

struct MathTypesetter {
    MathContext& ctx;
    const char* source;
    size_t source_len;

    // TFM fonts (cached for performance)
    TFMFont* roman_tfm;
    TFMFont* italic_tfm;
    TFMFont* symbol_tfm;
    TFMFont* extension_tfm;

    MathTypesetter(MathContext& c, const char* src, size_t len);

    // Main entry point
    TexNode* typeset();

    // Node dispatch based on tree-sitter node type
    TexNode* build_node(TSNode node);

    // Specific node builders
    TexNode* build_math(TSNode node);           // Top-level math
    TexNode* build_symbol(TSNode node);         // Single letter: a-z, A-Z
    TexNode* build_number(TSNode node);         // Digits: 0-9
    TexNode* build_operator(TSNode node);       // +, -, *, /, etc.
    TexNode* build_relation(TSNode node);       // =, <, >, etc.
    TexNode* build_punctuation(TSNode node);    // ,, ;, etc.
    TexNode* build_command(TSNode node);        // \alpha, \sum, etc.
    TexNode* build_subsup(TSNode node);         // x^2, x_i, x_i^n
    TexNode* build_fraction(TSNode node);       // \frac{a}{b}
    TexNode* build_radical(TSNode node);        // \sqrt{x}, \sqrt[n]{x}
    TexNode* build_delimiter_group(TSNode node); // \left( ... \right)
    TexNode* build_accent(TSNode node);         // \hat{x}, \bar{x}
    TexNode* build_big_operator(TSNode node);   // \sum_{i=1}^{n}
    TexNode* build_environment(TSNode node);    // \begin{matrix}...\end{matrix}
    TexNode* build_group(TSNode node);          // {braced content}
    TexNode* build_text_command(TSNode node);   // \text{...}
    TexNode* build_style_command(TSNode node);  // \mathbf{...}
    TexNode* build_space_command(TSNode node);  // \quad, \,

    // Helpers
    const char* node_text(TSNode node, int* out_len);
    char* node_text_dup(TSNode node);  // Caller must free
    TexNode* make_char_node(int32_t cp, AtomType atom, FontSpec& font, TFMFont* tfm);
    AtomType get_node_atom_type(TexNode* node);
};
```

---

## Key Design Decisions

### 1. Direct CST → TexNode (No Intermediate MathNode)

Unlike `input-math2.cpp` which produces Lambda `Item` trees, the new design goes directly from tree-sitter CST to `TexNode`:

```cpp
// OLD (input-math2.cpp): CST → MathNode (Lambda Item) → would need conversion → TexNode
// NEW (tex_math_ts.cpp): CST → TexNode directly

TexNode* MathTypesetter::build_symbol(TSNode node) {
    char* text = node_text_dup(node);
    int32_t cp = text[0];  // Single ASCII letter

    // Use italic font for variables (cmmi10)
    FontSpec font = ctx.italic_font;
    font.size_pt = ctx.font_size();

    TexNode* result = make_char_node(cp, AtomType::Ord, font, italic_tfm);
    free(text);
    return result;
}
```

### 2. Reuse Existing MathContext and Metrics

All font/metrics infrastructure from `tex_math_bridge.cpp` is preserved:

```cpp
struct MathContext {
    Arena* arena;
    TFMFontManager* fonts;
    MathStyle style;              // Display, Text, Script, ScriptScript
    float base_size_pt;           // Base font size (10pt default)
    FontSpec roman_font;          // cmr10 for digits
    FontSpec italic_font;         // cmmi10 for variables
    FontSpec symbol_font;         // cmsy10 for operators
    FontSpec extension_font;      // cmex10 for large delimiters
    float x_height, quad, axis_height, rule_thickness;
};
```

### 3. Tree-sitter Field Access Pattern

Use tree-sitter's field API for clean CST navigation:

```cpp
TexNode* MathTypesetter::build_fraction(TSNode node) {
    TSNode numer_node = ts_node_child_by_field_name(node, "numer", 5);
    TSNode denom_node = ts_node_child_by_field_name(node, "denom", 5);

    TexNode* numer = build_group(numer_node);
    TexNode* denom = build_group(denom_node);

    // Reuse existing typeset_fraction() for layout
    return typeset_fraction(numer, denom, ctx.rule_thickness, ctx);
}
```

### 4. Row Building with Atom Spacing

The core spacing logic from `parse_latex_math_internal()` becomes `build_math()`:

```cpp
TexNode* MathTypesetter::build_math(TSNode node) {
    Arena* arena = ctx.arena;
    TexNode* first = nullptr;
    TexNode* last = nullptr;
    AtomType prev_type = AtomType::Ord;
    bool is_first = true;

    uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        TexNode* child_node = build_node(child);
        if (!child_node) continue;

        AtomType curr_type = get_node_atom_type(child_node);

        // Insert inter-atom spacing per TeXBook Table 18.1
        if (!is_first) {
            float spacing_mu = get_atom_spacing_mu(prev_type, curr_type, ctx.style);
            if (spacing_mu > 0) {
                float spacing_pt = mu_to_pt(spacing_mu, ctx);
                TexNode* kern = make_kern(arena, spacing_pt);
                last->next_sibling = kern;
                kern->prev_sibling = last;
                last = kern;
            }
        }

        // Link child_node
        if (!first) first = child_node;
        if (last) {
            last->next_sibling = child_node;
            child_node->prev_sibling = last;
        }
        last = child_node;

        prev_type = curr_type;
        is_first = false;
    }

    return wrap_in_hbox(first, last);
}
```

---

## Node Type Mapping

| Tree-sitter Node | TexNode Class | Font | AtomType | Notes |
|------------------|---------------|------|----------|-------|
| `symbol` | `MathChar` | cmmi10 | Ord | Variables (a-z, A-Z) |
| `number` | `Char` sequence | cmr10 | Ord | Digits, uses roman |
| `operator` | `MathChar` | cmr10/cmsy10 | Bin | +, -, ×, etc. |
| `relation` | `MathChar` | cmr10/cmsy10 | Rel | =, <, ≤, etc. |
| `punctuation` | `MathChar` | cmr10 | Punct | ,, ;, etc. |
| `command` | Various | Various | Various | Dispatch by command |
| `subsup` | `Scripts` | - | - | nucleus + sub + sup |
| `fraction` | `Fraction` | - | Inner | num/denom/rule |
| `radical` | `Radical` | cmex10 | Ord | radicand + degree |
| `delimiter_group` | `HBox` + `Delimiter` | cmex10 | Inner | \left...\right |
| `accent` | `Accent` | cmmi10 | Ord | \hat, \bar, etc. |
| `big_operator` | `MathOp` | cmex10 | Op | \sum, \int with limits |
| `environment` | `MathList` | - | Inner | matrix, cases, etc. |
| `group` | `HBox` | - | Inner | {braced content} |
| `text_command` | `HBox` | cmr10 | Ord | \text{...} |
| `style_command` | Content | Various | - | \mathbf{...} |
| `space_command` | `Kern`/`Glue` | - | - | \quad, \, |

---

## Command Dispatch Table

Commands (`\alpha`, `\sum`, etc.) require special handling:

```cpp
TexNode* MathTypesetter::build_command(TSNode node) {
    char* cmd = node_text_dup(ts_node_child_by_field_name(node, "name", 4));

    // 1. Greek letters → MathChar with cmmi10
    int greek_code = lookup_greek_letter(cmd);
    if (greek_code >= 0) {
        // ...
    }

    // 2. Symbols → MathChar with cmsy10 (∞, ∀, ∃, etc.)
    int sym_code = lookup_symbol(cmd);
    if (sym_code >= 0) {
        // ...
    }

    // 3. Function operators → roman text (\sin, \cos, \lim)
    if (is_function_operator(cmd)) {
        // Build HBox with roman characters
    }

    // 4. Unknown → treat as roman text
    // ...
}
```

---

## Preserving Best of Both Systems

### From `tex_math_bridge.cpp`:
- ✅ TFM metrics integration (`make_char_with_metrics`)
- ✅ Atom spacing table (`SPACING_MU_TABLE`, `TIGHT_SPACING_MU_TABLE`)
- ✅ Style-aware sizing (`style_size_factor`, `sub_style`, `sup_style`)
- ✅ Typesetting functions: `typeset_fraction()`, `typeset_sqrt()`, `typeset_root()`
- ✅ Extensible delimiter building (`build_extensible_delimiter`)
- ✅ Script positioning logic

### From `input-math2.cpp`:
- ✅ Grammar-based parsing (robust, maintainable)
- ✅ Field-based node access (clean code)
- ✅ Proper error handling via tree-sitter
- ✅ Support for all LaTeX constructs defined in grammar

---

## Files to Create/Modify

| File | Action | Description |
|------|--------|-------------|
| `lambda/tex/tex_math_ts.hpp` | CREATE | Header for tree-sitter math typesetter |
| `lambda/tex/tex_math_ts.cpp` | CREATE | Implementation (~600 lines) |
| `lambda/tex/tex_math_bridge.cpp` | MODIFY | Wire `typeset_latex_math()` to new impl |
| `build_lambda_config.json` | MODIFY | Add tex_math_ts.cpp to build |

---

## Implementation Plan

| Phase | Task | Effort |
|-------|------|--------|
| 1 | Create `tex_math_ts.hpp/cpp` skeleton with `MathTypesetter` | 0.5 day |
| 2 | Implement core: `typeset()`, `build_node()`, `build_math()` | 1 day |
| 3 | Implement atoms: `build_symbol`, `build_number`, `build_operator`, `build_relation` | 1 day |
| 4 | Implement `build_command()` with Greek, symbols, func operators | 1.5 days |
| 5 | Implement `build_subsup()` (subscript/superscript) | 0.5 day |
| 6 | Implement `build_fraction()`, `build_radical()` | 1 day |
| 7 | Implement `build_delimiter_group()`, `build_big_operator()` | 1 day |
| 8 | Implement `build_accent()`, `build_environment()` | 1 day |
| 9 | Wire up to `typeset_latex_math()`, integration testing | 1 day |

**Total: ~8-9 days**

---

## API Compatibility

The public API remains unchanged:

```cpp
// tex_math_bridge.hpp - signature unchanged
TexNode* typeset_latex_math(const char* latex_str, size_t len, MathContext& ctx);
```

Internal switch:

```cpp
// tex_math_bridge.cpp
TexNode* typeset_latex_math(const char* latex_str, size_t len, MathContext& ctx) {
#ifdef USE_TREE_SITTER_MATH
    return typeset_latex_math_ts(latex_str, len, ctx);  // NEW
#else
    return parse_latex_math_internal(latex_str, len, ctx);  // OLD
#endif
}
```

---

## Testing Strategy

1. **Unit Tests**: New test file `test/test_tex_math_ts_gtest.cpp`
   - Test each builder function with isolated input
   - Verify dimensions match old implementation

2. **Integration Tests**: Compare output with existing tests
   - Run existing `test_latex_dvi_compare_gtest.cpp` tests
   - Verify DVI output is identical

3. **Visual Regression**: Render to PNG and compare
   - Focus on spacing, positioning, delimiter sizing

---

## Success Criteria

1. **Correctness**: All existing math tests pass
2. **Maintainability**: Adding new constructs requires only grammar + builder
3. **Performance**: No measurable regression (tree-sitter is fast)
4. **Code Reduction**: Net reduction of ~800 lines (remove old parser)

---

## References

- TeXBook Chapters 17-18 (Math typesetting)
- `tree-sitter-latex-math` grammar: `lambda/tree-sitter-latex-math/grammar.js`
- Existing implementation: `lambda/tex/tex_math_bridge.cpp`
- MathLive parser: `lambda/input/input-math2.cpp`
