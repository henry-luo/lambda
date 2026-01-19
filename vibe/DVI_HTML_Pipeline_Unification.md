# DVI and HTML Pipeline Unification Analysis

**Date:** January 19, 2026  
**Status:** Design Document

## Executive Summary

The Lambda TeX implementation is being unified to use a **single pipeline** for all LaTeX-to-output conversions:

```
LaTeX Source → Tree-sitter → DocElement → {HTML, TexNode→DVI/PDF/SVG/PNG}
```

This document specifies the unified architecture, eliminating the legacy tokenizer/expander/digester path.

---

## 1. Unified Architecture

### 1.1 Single Pipeline (Target State)

```
LaTeX Source
     ↓
Tree-sitter Parser (tex_latex_bridge.cpp)
     ↓
Lambda Element AST (Item tree)
     ↓
TexDocumentModel (tex_document_model.cpp)
     ├── DocElement tree (semantic structure)
     │   • PARAGRAPH, HEADING, LIST, TABLE, etc.
     │   • DocTextStyle for formatting
     │   • Cross-references, footnotes, citations
     │   • Macro definitions and expansion
     ↓
     ├── doc_model_to_html() → HTML output
     │
     └── doc_model_to_texnode() → TexNode tree
                                       │
                              ┌────────┼────────┐
                              ↓        ↓        ↓
                             DVI      SVG      PDF/PNG
```

### 1.2 Key Files

| File | Purpose |
|------|---------|
| `tex_latex_bridge.cpp` | Tree-sitter CST → Lambda Element AST |
| `tex_document_model.cpp` | DocElement construction + HTML/TexNode output |
| `tex_math_bridge.cpp` | Math mode → TexNode (inline in DocElement) |
| `tex_svg_out.cpp` | TexNode → SVG |
| `tex_dvi_out.cpp` | TexNode → DVI |
| `tex_pdf_out.cpp` | TexNode → PDF |

## 2. Macro Expansion (UNIFIED ✓)

### 2.1 Current State: Already Unified

Macro expansion is **already unified** in the Tree-sitter pipeline:

```cpp
// In tex_document_model.cpp (lines 178-202):

struct MacroDef {
    const char* name;         // e.g., "\\mycommand"
    int num_args;             // Number of arguments (0-9)
    const char* replacement;  // Replacement text with #1, #2, etc.
    const char* params;       // Parameter pattern: "[]{}[]" etc.
};

void TexDocumentModel::add_macro(const char* name, int num_args, 
                                  const char* replacement, const char* params);
const MacroDef* TexDocumentModel::find_macro(const char* name) const;
```

### 2.2 Macro Registration

When `\newcommand`, `\renewcommand`, or `\def` is encountered:
1. Tree-sitter parses the definition
2. `handle_newcommand()` extracts name, args, replacement
3. `doc->add_macro()` registers in TexDocumentModel

### 2.3 Macro Expansion

When a command is used:
1. `try_expand_macro()` looks up the command (line 2559)
2. Collects arguments from child elements
3. Substitutes `#1`, `#2`, etc. in replacement text
4. Recursively processes the expanded content

**Location:** [tex_document_model.cpp#L2559-L2700](lambda/tex/tex_document_model.cpp#L2559)


## 3. Digestion (UNIFIED ✓)

### 3.1 Current State: Already Unified

"Digestion" (TeX's Stomach processing) is **already unified** in DocElement:

| TeX Concept | DocElement Equivalent |
|-------------|----------------------|
| Mode tracking (H/V/M) | DocElemType (PARAGRAPH, HEADING, MATH_*) |
| Font state | DocTextStyle (family, size, bold, italic) |
| Counter tracking | TexDocumentModel counter system |
| Label/ref resolution | DocElement ref_id + resolve_references() |
| List building | DocElement list_info |
| Box construction | DocElement types (TEXT_SPAN, BLOCK, etc.) |

### 3.2 DocElement as Semantic IR

DocElement serves the same role as DigestedNode but with **semantic** rather than typographic structure:

```cpp
// DocElement types cover all document constructs:
enum class DocElemType {
    PARAGRAPH,       // Text paragraph
    HEADING,         // Section, subsection, etc.
    LIST,            // itemize, enumerate
    LIST_ITEM,       // Individual item
    TABLE,           // tabular, table
    FIGURE,          // figure environment
    MATH_INLINE,     // $...$
    MATH_DISPLAY,    // \[...\] or equation
    TEXT_SPAN,       // Styled text run
    FOOTNOTE,        // Footnote reference
    CROSS_REF,       // \ref, \cite
    // ... etc.
};
```

---

## 4. Remaining Work: doc_model_to_texnode()

### 4.1 Current State

✅ **IMPLEMENTED** - The `doc_model_to_texnode()` and `doc_element_to_texnode()` functions are now implemented at [tex_document_model.cpp#L7850-L8385](lambda/tex/tex_document_model.cpp#L7850).

### 4.2 Implemented Conversions

| DocElemType | Status | Description |
|-------------|--------|-------------|
| `TEXT_RUN` | ✅ | Converts text to Char nodes with TFM metrics |
| `TEXT_SPAN` | ✅ | Styled text with font selection |
| `PARAGRAPH` | ✅ | HList with indentation and parfillskip |
| `HEADING` | ✅ | VList with section number and spacing |
| `LIST` | ✅ | VList with list items |
| `LIST_ITEM` | ✅ | HList with bullet/number and content |
| `MATH_*` | ✅ | Pass-through of pre-typeset TexNode |
| `SPACE` | ✅ | Glue nodes for spacing |
| `DOCUMENT` | ✅ | VList container with parskip |
| `SECTION` | ✅ | VList container |
| `BLOCKQUOTE` | ✅ | VList with margin |

### 4.3 Still TODO

| DocElemType | Status | Notes |
|-------------|--------|-------|
| `TABLE` | ❌ | Requires table layout algorithm |
| `FIGURE` | ❌ | Requires float placement |
| `IMAGE` | ❌ | Requires image loading |
| `CODE_BLOCK` | ❌ | Verbatim text handling |
| `FOOTNOTE` | ❌ | Requires footnote insertion |
| `CITATION` | ❌ | Bibliography formatting |
| `CROSS_REF` | ❌ | Reference text rendering |
| `LINK` | ❌ | Hyperlink handling |

### 4.4 Key Helper Functions

```cpp
// Font selection from DocTextStyle (lines 7866-7920)
static FontSpec doc_style_to_font(const DocTextStyle& style, float base_size_pt, LaTeXContext& ctx);

// Character with TFM metrics (lines 7922-7947)
static TexNode* make_text_char(Arena* arena, int32_t codepoint, const FontSpec& font, TFMFontManager* fonts);

// Interword glue from TFM (lines 7949-7966)
static TexNode* make_text_space(Arena* arena, const FontSpec& font, TFMFontManager* fonts);
```

---

## 5. Output Paths from TexNode

Once `doc_model_to_texnode()` is implemented, all fixed-layout outputs use the same TexNode tree:

```
               TexNode tree
                    │
      ┌─────────────┼─────────────┐
      ↓             ↓             ↓
dvi_output()   svg_output()  pdf_output()
      ↓             ↓             ↓
    .dvi          .svg          .pdf
```

**Already implemented:**
- `dvi_output_node()` in `tex_dvi_out.cpp` ✓
- `svg_output()` in `tex_svg_out.cpp` ✓
- `pdf_output()` in `tex_pdf_out.cpp` ✓
- `png_output()` in `tex_png_out.cpp` ✓

---

## 6. Migration Status

### 6.1 Completed ✓

| Component | Status |
|-----------|--------|
| Tree-sitter LaTeX grammar | ✓ Implemented |
| Lambda Element AST | ✓ Implemented |
| TexDocumentModel | ✓ Implemented |
| DocElement types | ✓ Comprehensive |
| Macro registration | ✓ Unified in TexDocumentModel |
| Macro expansion | ✓ Unified via try_expand_macro() |
| HTML output | ✓ doc_model_to_html() complete |
| Math to TexNode | ✓ Inline in DocElement |
| SVG/PDF/PNG from TexNode | ✓ Implemented |

### 6.2 In Progress

| Component | Status |
|-----------|--------|
| doc_model_to_texnode() | ✅ Implemented (basic elements) |
| DVI from DocElement path | Ready to test |
| Line breaking integration | TODO |
| Page breaking | TODO |

### 6.3 Deprecated Code and Analysis

| Component             | Replacement                                             |
| --------------------- | ------------------------------------------------------- |
| `tex_tokenizer.cpp`   | Replaced by Tree-sitter parsing                         |
| `tex_expander.cpp`    | Macro expansion now in TexDocumentModel                 |
| `tex_digester.cpp`    | Document building now in TexDocumentModel               |
| `tex_digested.hpp`    | DigestedNode replaced by DocElement                     |
| `tex_macro.cpp`       | TexDocumentModel.MacroDef (orphaned - never integrated) |
| `tex_conditional.cpp` | Not needed (orphaned - depends on tex_macro.cpp)        |

#### 6.3.1 tex_macro.cpp / tex_macro.hpp

**Status:** Orphaned - never integrated into main pipeline

The `MacroProcessor` class was designed for the legacy tokenizer path but:
- Was never connected to `tex_document_model.cpp`
- `tex_document_model.cpp` has its own simpler macro system (lines 178-202)

| Feature | MacroProcessor | TexDocumentModel |
|---------|----------------|------------------|
| Storage | Hashmap | Linear array |
| Parameter types | Undelimited, Delimited, Optional | Simple #1, #2 |
| \def delimiters | Full TeX-style `#1.#2` | Not supported |
| Recursive expansion | Yes, depth-limited | Yes, via DOM recursion |
| **Used in pipeline** | ❌ No | ✅ Yes |

#### 6.3.2 tex_conditional.cpp / tex_conditional.hpp

**Status:** Orphaned - depends on tex_macro.cpp

The `ConditionalProcessor` class implements `\if`, `\ifx`, `\ifnum`, etc. but:
- Depends on `MacroProcessor` from tex_macro.hpp
- Never included by any production code
- Only used by unit tests

---

## 8. Summary

### Architecture Decisions

1. **Single front-end:** Tree-sitter parser (no tokenizer)
2. **Single IR:** DocElement (no DigestedNode)
3. **Unified macros:** TexDocumentModel.MacroDef (no Expander)
4. **Shared typesetting:** TexNode tree for all fixed-layout output

### Remaining Work

1. **Line breaking:** Integrate Knuth-Plass algorithm (`tex_linebreak.cpp`) with paragraph HLists
2. **Page breaking:** Split VList into pages using `tex_pagebreak.cpp`
3. **Complex elements:** Tables, figures, footnotes
4. **Testing:** Create DVI output tests using DocElement path

### Key Benefits

1. **Single codebase** - One parser, one IR, multiple outputs
2. **No duplication** - Macro expansion, counter tracking, refs all unified
3. **Easier testing** - Test DocElement once, outputs follow
4. **Simpler maintenance** - Fix bugs in one place
