# LaTeX Math Typesetting: DVI Pipeline Analysis

**Date:** January 20, 2026  
**Status:** Analysis & Proposal  
**Issue:** Math content not rendered to DVI via `lambda.exe render`
**Approach:** Option B - Eager Typesetting in `doc_model_from_string`

---

## 1. Executive Summary

The `test_latex_dvi_compare_gtest` test suite compares Lambda-generated DVI output against reference DVI files produced by TeX. Currently **28 of 32 tests fail** because math content is not being written to DVI output.

**Root Cause:** The `convert_math()` function in `tex_document_model.cpp` returns `nullptr` because `elem->math.node` is never populated. The math typesetting function `typeset_latex_math()` is never called during the DVI pipeline.

**Solution:** Implement Option B - Add a Math AST layer to the document model and typeset math eagerly during `doc_model_from_string()`.

---

## 2. Reference Architecture Analysis

### 2.1 MathLive Architecture (TypeScript)

MathLive uses a hierarchical **Atom** system with specialized atom types:

```
                    Atom (base class)
                       │
    ┌──────────────────┼──────────────────────────┐
    │                  │                          │
GenfracAtom      SubsupAtom              SurdAtom
(fractions)      (sub/superscript)       (radicals)
    │                  │                          │
    ├── above[]        ├── subscript[]            ├── body[]
    └── below[]        └── superscript[]          └── above[] (index)
```

**Key Design Points from MathLive:**
- **Atom Types**: `genfrac`, `subsup`, `surd`, `leftright`, `overunder`, `array`, `accent`, `operator`
- **Named Branches**: `body`, `above`, `below`, `superscript`, `subscript`
- **Serialization**: `toJson()` / `fromJson()` for persistence
- **Rendering**: `render(context)` → `Box` (layout primitive)

### 2.2 LaTeXML Architecture (Perl)

LaTeXML uses a grammar-based parser with recursive descent:

```
MathGrammar
    │
    ├── Formulae → Formula (comma-separated)
    │       │
    │       └── Expression → SignedTerm → Factor
    │               │
    │               └── moreTerms (AddOp/MulOp binding)
    │
    └── Factor
            ├── preScripted['FUNCTION'] addArgs
            ├── preScripted['bigop'] addOpArgs
            ├── OPEN Expression CLOSE
            └── absExpression (| ... |)
```

**Key Design Points from LaTeXML:**
- **Operator Precedence**: Formulae > Expression > Term > Factor
- **Script Handling**: `preScripted`, `addScripts`, `FLOATSUPERSCRIPT/POSTSUBSCRIPT`
- **Delimiter Matching**: `Fence()`, `balancedClose`, `isMatchingClose`
- **Semantic Annotation**: `Apply()`, `InvisibleTimes()`, `InvisibleComma()`

---

## 3. Proposed Math AST Design

### 3.1 MathAST Node Types

Add a new intermediate representation between LaTeX source and TexNode:

```cpp
// In tex_document_model.hpp (new section)

enum class MathNodeType : uint8_t {
    // ========================================
    // Atoms (terminals)
    // ========================================
    ORD,            // Ordinary: variable, constant
    OP,             // Large operator: \sum, \int
    BIN,            // Binary operator: +, -, \times
    REL,            // Relation: =, <, \leq
    OPEN,           // Opening delimiter: (, [, \{
    CLOSE,          // Closing delimiter: ), ], \}
    PUNCT,          // Punctuation: ,
    INNER,          // Inner atom (delimited group)
    
    // ========================================
    // Structures
    // ========================================
    ROW,            // Horizontal sequence of atoms
    FRAC,           // Fraction: \frac{num}{denom}
    SQRT,           // Square root: \sqrt{x} or \sqrt[n]{x}
    SCRIPTS,        // Sub/superscripts: x^2_i
    DELIMITED,      // Delimited: \left( ... \right)
    ACCENT,         // Accent: \hat{x}, \bar{y}
    OVERUNDER,      // Over/under: \sum_{i=0}^{n}
    
    // ========================================
    // Text in math
    // ========================================
    TEXT,           // \text{...} or \mathrm{...}
    
    // ========================================
    // Arrays
    // ========================================
    ARRAY,          // \begin{matrix}...\end{matrix}
    ARRAY_ROW,      // Row in array
    ARRAY_CELL,     // Cell in array row
    
    // ========================================
    // Special
    // ========================================
    SPACE,          // Math spacing: \, \; \quad
    ERROR,          // Parse error recovery
};
```

### 3.2 MathAST Node Structure

```cpp
struct MathASTNode {
    MathNodeType type;
    uint8_t flags;
    
    // Flag bits
    static constexpr uint8_t FLAG_LIMITS = 0x01;      // Display limits
    static constexpr uint8_t FLAG_LARGE = 0x02;       // Large variant
    static constexpr uint8_t FLAG_CRAMPED = 0x04;     // Cramped style
    
    // Content (type-dependent)
    union {
        // For ORD, OP, BIN, REL, OPEN, CLOSE, PUNCT
        struct {
            int32_t codepoint;      // Unicode codepoint
            const char* command;    // LaTeX command (e.g., "alpha")
            AtomType atom_type;     // TeX atom classification
        } atom;
        
        // For ROW
        struct {
            int child_count;
        } row;
        
        // For FRAC
        struct {
            float rule_thickness;   // 0 for \atop
            int32_t left_delim;     // For \binom: (
            int32_t right_delim;    // For \binom: )
        } frac;
        
        // For SQRT
        struct {
            bool has_index;         // Has optional [n]
        } sqrt;
        
        // For SCRIPTS
        struct {
            AtomType nucleus_type;
        } scripts;
        
        // For DELIMITED
        struct {
            int32_t left_delim;
            int32_t right_delim;
        } delimited;
        
        // For ACCENT
        struct {
            int32_t accent_char;    // Accent character
        } accent;
        
        // For TEXT
        struct {
            const char* text;
            size_t len;
            bool is_roman;          // \mathrm vs \text
        } text;
        
        // For SPACE
        struct {
            float width_mu;         // Width in mu (1/18 em)
        } space;
    };
    
    // Tree structure (named branches)
    MathASTNode* body;          // Main content (ROW, DELIMITED, SQRT, ACCENT)
    MathASTNode* above;         // Numerator (FRAC), index (SQRT), accent base
    MathASTNode* below;         // Denominator (FRAC)
    MathASTNode* superscript;   // Superscript (SCRIPTS, OVERUNDER)
    MathASTNode* subscript;     // Subscript (SCRIPTS, OVERUNDER)
    
    // Siblings (for sequences within branches)
    MathASTNode* next_sibling;
    MathASTNode* prev_sibling;
    
    // Source mapping
    SourceLoc source;
};
```

### 3.3 Named Branch Convention (from MathLive)

Following MathLive's branch naming:

| Node Type | body | above | below | superscript | subscript |
|-----------|------|-------|-------|-------------|-----------|
| ROW       | n/a  | n/a   | n/a   | n/a         | n/a       |
| FRAC      | n/a  | numerator | denominator | n/a | n/a |
| SQRT      | radicand | index | n/a | n/a | n/a |
| SCRIPTS   | nucleus | n/a | n/a | sup | sub |
| DELIMITED | content | n/a | n/a | n/a | n/a |
| ACCENT    | base | n/a | n/a | n/a | n/a |
| OVERUNDER | nucleus | over | under | n/a | n/a |

---

## 4. DocElement Integration

### 4.1 Enhanced Math Union

Update the `DocElement::math` union to include the AST:

```cpp
// For MATH_* types
struct {
    MathASTNode* ast;       // NEW: Parsed math AST (populated during parsing)
    TexNode* node;          // Typeset TeX node tree (populated during typesetting)
    const char* latex_src;  // Original LaTeX (for fallback/debugging)
    const char* label;      // Equation label
    const char* number;     // Equation number
} math;
```

### 4.2 Pipeline Flow (Two-Phase Design)

**Phase A: Parsing** (`doc_model_from_string`)
- Builds DocElement tree from LaTeX source
- For math elements: builds MathAST only (no typesetting yet)

**Phase B: Typesetting** (`doc_element_to_texnode` / `convert_math`)
- Converts DocElement tree to TexNode tree
- For math elements: calls `typeset_math_ast()` to convert MathAST → TexNode

```
┌─────────────────────────────────────────────────────────────────────┐
│           PHASE A: PARSING (doc_model_from_string)                  │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  LaTeX Source (.tex)                                                 │
│       │                                                              │
│       ▼                                                              │
│  parse_latex_ts()  ──► Lambda Element tree                          │
│       │                                                              │
│       ▼                                                              │
│  build_doc_element()                                                 │
│       │                                                              │
│       ├─── Non-math ──► DocElement (as before)                      │
│       │                                                              │
│       └─── inline_math/display_math                                  │
│               │                                                      │
│               ▼                                                      │
│           parse_math_to_ast()  ──► MathASTNode tree  ✅ NEW         │
│               │                                                      │
│               ▼                                                      │
│           DocElement {                                               │
│             math.ast = MathASTNode*   ← POPULATED (AST only)        │
│             math.node = nullptr       ← NOT YET TYPESET             │
│             math.latex_src = "..."                                  │
│           }                                                          │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│        PHASE B: TYPESETTING (doc_element_to_texnode)                │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  DocElement tree                                                     │
│       │                                                              │
│       ▼                                                              │
│  doc_element_to_texnode()                                            │
│       │                                                              │
│       ├─── TEXT_RUN  ──► convert_text_run() ──► TexNode (Chars)     │
│       │                                                              │
│       ├─── PARAGRAPH ──► convert_paragraph() ──► TexNode (HList)    │
│       │                                                              │
│       └─── MATH_INLINE/MATH_DISPLAY ──► convert_math()              │
│               │                                                      │
│               ▼                                                      │
│           typeset_math_ast(elem->math.ast, ctx)  ✅ NEW             │
│               │                                                      │
│               ▼                                                      │
│           TexNode tree (with proper dimensions)                      │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

**Benefits of Two-Phase Design:**

| Benefit | Description |
|---------|-------------|
| **Separation of Concerns** | Parsing is font-independent; typesetting uses fonts |
| **Context Awareness** | Typesetting has access to LaTeXContext (font size, style) |
| **Consistent with Text** | Same pattern as text: build DocElement → convert to TexNode |
| **Deferred Typesetting** | Can typeset same AST at different sizes/styles |

---

## 5. Implementation Plan

### Phase 1: Add MathAST Data Structures

**File: `lambda/tex/tex_math_ast.hpp` (NEW)**

```cpp
// tex_math_ast.hpp - Math AST for intermediate representation
#ifndef TEX_MATH_AST_HPP
#define TEX_MATH_AST_HPP

#include "tex_node.hpp"
#include "lib/arena.h"

namespace tex {

enum class MathNodeType : uint8_t { ... };
struct MathASTNode { ... };

// Allocator
MathASTNode* alloc_math_node(Arena* arena, MathNodeType type);

// Named branch accessors
inline MathASTNode* math_node_body(MathASTNode* n) { return n->body; }
inline void set_math_node_body(MathASTNode* n, MathASTNode* b) { n->body = b; }
// ... etc for above, below, superscript, subscript

} // namespace tex
#endif
```

### Phase 2: Math AST Builder

**File: `lambda/tex/tex_math_ast_builder.cpp` (NEW)**

Build `MathASTNode` tree from Lambda Element tree (tree-sitter parsed math):

```cpp
// Parse inline_math or display_math element to MathASTNode
MathASTNode* parse_math_to_ast(const ItemReader& elem, Arena* arena);

// Internal recursive builder
MathASTNode* build_math_ast_node(const ItemReader& node, Arena* arena);
```

### Phase 3: Math AST to TexNode

**File: `lambda/tex/tex_math_ast_typeset.cpp` (NEW)**

Convert `MathASTNode` tree to `TexNode` tree:

```cpp
// Typeset MathASTNode tree to TexNode tree
TexNode* typeset_math_ast(MathASTNode* ast, MathContext& ctx);
```

### Phase 4: Integrate into build_doc_element (Parsing Phase)

**File: `lambda/tex/tex_document_model.cpp`**

Modify `build_inline_element()` for `inline_math` - **AST only, no typesetting**:

```cpp
// Inline math
if (tag_eq(tag, "inline_math") || tag_eq(tag, "math")) {
    DocElement* math = doc_alloc_element(arena, DocElemType::MATH_INLINE);
    math->math.latex_src = extract_math_source(elem, arena);
    
    // NEW: Parse to AST (no typesetting yet - that happens in convert_math)
    math->math.ast = parse_math_to_ast(elem, arena);
    math->math.node = nullptr;  // Will be populated during typesetting phase
    
    return math;
}
```

Similarly for `display_math`:

```cpp
// Display math
if (tag_eq(tag, "display_math") || tag_eq(tag, "displaymath") ||
    tag_eq(tag, "equation") || tag_eq(tag, "equation*")) {
    DocElement* math = doc_alloc_element(arena, DocElemType::MATH_DISPLAY);
    math->math.latex_src = extract_math_source(elem, arena);
    
    // NEW: Parse to AST (no typesetting yet)
    math->math.ast = parse_math_to_ast(elem, arena);
    math->math.node = nullptr;
    
    return math;
}
```

### Phase 5: Update convert_math (Typesetting Phase)

**File: `lambda/tex/tex_document_model.cpp`**

Typeset math AST during DocElement → TexNode conversion:

```cpp
static TexNode* convert_math(DocElement* elem, Arena* arena, LaTeXContext& ctx) {
    if (!elem) return nullptr;
    
    // Check if already typeset (caching for re-renders)
    if (elem->math.node) {
        return elem->math.node;
    }
    
    // Typeset from AST
    if (elem->math.ast) {
        // Create MathContext from LaTeXContext
        MathContext math_ctx = MathContext::create(arena, ctx.fonts, ctx.doc_ctx.font_size);
        
        // Set math style based on element type
        if (elem->type == DocElemType::MATH_DISPLAY || 
            elem->type == DocElemType::MATH_EQUATION ||
            elem->type == DocElemType::MATH_ALIGN) {
            math_ctx.style = MathStyle::Display;
        } else {
            math_ctx.style = MathStyle::Text;
        }
        
        // Typeset AST to TexNode
        TexNode* result = typeset_math_ast(elem->math.ast, math_ctx);
        
        // Cache result for potential re-renders
        elem->math.node = result;
        
        if (result) {
            log_debug("convert_math: typeset success, width=%.2f height=%.2f",
                      result->width, result->height);
        }
        
        return result;
    }
    
    log_debug("doc_model: math element has no AST");
    return nullptr;
}
```

---

## 6. Benefits of This Approach

### 6.1 Why MathAST Layer?

| Benefit | Description |
|---------|-------------|
| **Reusability** | AST can be rendered to multiple outputs (DVI, SVG, HTML) |
| **Debugging** | AST can be inspected/dumped for debugging |
| **Optimization** | AST enables structure-aware optimizations |
| **Testing** | AST construction can be tested independently |
| **Caching** | Same AST can be typeset at different sizes |

### 6.2 Comparison with Option A

| Aspect | Option A (Lazy) | Option B (Eager + AST) |
|--------|-----------------|------------------------|
| Complexity | Low | Medium |
| Flexibility | Low | High |
| Debugging | Hard | Easy (AST dump) |
| Multi-output | No | Yes |
| Testing | Coupled | Decoupled |

---

## 7. File Changes Summary

| File | Action | Description |
|------|--------|-------------|
| `lambda/tex/tex_math_ast.hpp` | NEW | MathAST data structures |
| `lambda/tex/tex_math_ast_builder.cpp` | NEW | Parse → MathAST (Phase A) |
| `lambda/tex/tex_math_ast_typeset.cpp` | NEW | MathAST → TexNode (Phase B) |
| `lambda/tex/tex_document_model.hpp` | MODIFY | Add `ast` to math union |
| `lambda/tex/tex_document_model.cpp` | MODIFY | AST building in `build_doc_element`, typesetting in `convert_math` |
| `build_lambda_config.json` | MODIFY | Add new source files |

---

## 8. Testing Strategy

### 8.1 Unit Tests

| Test | Description |
|------|-------------|
| `test_math_ast_builder` | Parse LaTeX → MathAST (Phase A) |
| `test_math_ast_typeset` | MathAST → TexNode (Phase B) |
| `test_math_ast_roundtrip` | LaTeX → AST → LaTeX |

### 8.2 Integration Tests

Existing tests should pass after implementation:

| Test | Current | After |
|------|---------|-------|
| `test_tex_math_extended_gtest` | 34/34 ✅ | 34/34 ✅ |
| `test_latex_dvi_compare_gtest` | 4/32 ✅ | 32/32 ✅ |

---

## 9. Related Files

| File | Relevance |
|------|-----------|
| `lambda/tex/tex_document_model.cpp` | `build_doc_element()` builds AST, `convert_math()` typesets |
| `lambda/tex/tex_math_bridge.hpp` | `typeset_latex_math()` declaration |
| `lambda/tex/tex_math_bridge.cpp` | Math typesetting implementation |
| `lambda/tex/tex_math_ts.cpp` | Tree-sitter based math parser |
| `radiant/render_dvi.cpp` | DVI rendering pipeline |
| `test/test_latex_dvi_compare_gtest.cpp` | The failing test suite |
| `test/test_tex_math_extended_gtest.cpp` | Math typesetting tests (78 pass) |

---

## 10. Key Insight

**The math typesetting pipeline works** (Phase 4 tests: 41/41 pass, Phase 5: 37/37 pass).  
**The DVI output pipeline works** (SimpleText generates correct DVI).  
**They are not connected for math content.**

Option B adds a Math AST layer that:
1. Provides clean separation between parsing and typesetting
2. Enables eager typesetting during document model construction
3. Supports future features (MathML export, accessibility, editing)

---

## 11. Success Criteria

- [ ] `MathASTNode` structure defined
- [ ] `parse_math_to_ast()` implemented
- [ ] `typeset_math_ast()` implemented
- [ ] `doc_model_from_string()` eagerly typesets math
- [ ] `SimpleMath` test passes (basic math works)
- [ ] All 28 math tests pass
- [ ] No regressions in existing tests
- [ ] `test_latex_dvi_compare_gtest`: 32/32 pass
