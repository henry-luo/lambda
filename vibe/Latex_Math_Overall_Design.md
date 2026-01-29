# LaTeX Math Typesetting Pipeline - Master Design Document

**Date:** January 29, 2026
**Status:** Living Document
**Purpose:** Comprehensive reference for math typesetting implementation and development

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Architecture Overview](#2-architecture-overview)
3. [Core Data Structures](#3-core-data-structures)
4. [Pipeline Phases](#4-pipeline-phases)
5. [Font System](#5-font-system)
6. [Output Formats](#6-output-formats)
7. [Key Implementation Files](#7-key-implementation-files)
8. [Testing Infrastructure](#8-testing-infrastructure)
9. [CLI Commands](#9-cli-commands)
10. [Current Status & Known Issues](#10-current-status--known-issues)
11. [Development Roadmap](#11-development-roadmap)
12. [Debugging Guide](#12-debugging-guide)
13. [Supported Commands](#13-supported-commands)
14. [References](#14-references)

---

## 1. Executive Summary

Lambda Script implements a **TeX-compatible math typesetting pipeline** that converts LaTeX math notation to multiple output formats (DVI, PDF, SVG, HTML, PNG). The design follows TeX's proven algorithms while using CSS pixels as the internal coordinate system for web integration.

### Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| **Two-phase processing** | Separates parsing (font-independent) from typesetting (requires fonts) |
| **MathAST intermediate layer** | Enables AST inspection, multi-output support, and deferred typesetting |
| **TexNode as unified view tree** | No separate ViewTree conversion; TexNode IS the layout structure |
| **CSS pixels as layout unit** | Simplifies Radiant integration; DVI output converts to scaled points |
| **TFM-based font metrics** | Authentic TeX metrics for precise typesetting |

### Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    LaTeX Math Typesetting Pipeline                          │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│   LaTeX Source                                                               │
│   ───────────                                                                │
│        │                                                                     │
│        ▼                                                                     │
│   ┌────────────────────┐                                                    │
│   │  tree-sitter-latex │  (or tree-sitter-latex-math for math-only)         │
│   │      parsing       │                                                    │
│   └─────────┬──────────┘                                                    │
│             │                                                                │
│             ▼                                                                │
│   ┌────────────────────┐                                                    │
│   │  Lambda Element    │  Generic parsed AST (tag + attributes + children)  │
│   │      AST           │                                                    │
│   └─────────┬──────────┘                                                    │
│             │                                                                │
│   ══════════╪══════════════════════════════════════════════════════════════ │
│   PHASE A   │   PARSING (font-independent)                                  │
│   ══════════╪══════════════════════════════════════════════════════════════ │
│             │                                                                │
│             ▼                                                                │
│   ┌────────────────────┐    ┌────────────────────┐                         │
│   │  DocElement tree   │    │   MathASTNode      │                         │
│   │  (document model)  │───►│   (math AST)       │                         │
│   └─────────┬──────────┘    └─────────┬──────────┘                         │
│             │                         │                                     │
│   ══════════╪═════════════════════════╪════════════════════════════════════ │
│   PHASE B   │   TYPESETTING (with TFM fonts)                                │
│   ══════════╪═════════════════════════╪════════════════════════════════════ │
│             │                         │                                     │
│             ▼                         ▼                                     │
│   ┌────────────────────┐    ┌────────────────────┐                         │
│   │  TexNode tree      │◄───│  typeset_math_ast  │                         │
│   │  (layout with      │    │  (MathContext)     │                         │
│   │   dimensions)      │    └────────────────────┘                         │
│   └─────────┬──────────┘                                                    │
│             │                                                                │
│   ══════════╪══════════════════════════════════════════════════════════════ │
│   OUTPUT    │                                                                │
│   ══════════╪══════════════════════════════════════════════════════════════ │
│             │                                                                │
│    ┌────────┼────────┬────────────┬────────────┬────────────┐              │
│    │        │        │            │            │            │              │
│    ▼        ▼        ▼            ▼            ▼            ▼              │
│  ┌────┐  ┌─────┐  ┌─────┐    ┌──────┐    ┌──────┐    ┌──────┐             │
│  │DVI │  │ PDF │  │ SVG │    │ HTML │    │ PNG  │    │Screen│             │
│  └────┘  └─────┘  └─────┘    └──────┘    └──────┘    └──────┘             │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Architecture Overview

### 2.1 Design Philosophy

The math pipeline follows TeX's fundamental approach:

1. **Atom Classification** - Every math element has a class (Ord, Op, Bin, Rel, Open, Close, Punct, Inner)
2. **Style Propagation** - Math styles (Display, Text, Script, ScriptScript) affect sizing
3. **Box Model** - Everything becomes boxes with width, height, and depth
4. **Glue System** - Flexible spacing using stretch/shrink

### 2.2 Coordinate System

| System | Unit | Usage |
|--------|------|-------|
| **Internal** | CSS pixels (96 dpi) | TexNode dimensions, layout calculations |
| **DVI Output** | Scaled points (sp) | 1 sp = 1/65536 pt ≈ 0.015μm |
| **TFM Metrics** | Design units | Scaled by size_pt/design_size |
| **Math Units** | mu | 1 mu = 1/18 em (for inter-atom spacing) |

**Conversion Formulas:**
```
CSS px → TeX pt: px × (72.27/96) ≈ px × 0.7528
TeX pt → CSS px: pt × (96/72.27) ≈ pt × 1.3281
TeX pt → scaled pt: pt × 65536
CSS px → scaled pt: px × (72.27/96) × 65536 ≈ px × 49315
```

### 2.3 Memory Management

- **Arena allocation** - All TexNode and MathASTNode trees use arena allocation
- **No ownership semantics** - Trees share memory lifetime with arena
- **Pool allocation** - MarkBuilder uses pool_calloc() for Lambda Items

---

## 3. Core Data Structures

### 3.1 MathASTNode (Semantic Math AST)

**Location:** `lambda/tex/tex_math_ast.hpp`

The MathAST captures semantic math structure without font/size information:

```cpp
enum class MathNodeType : uint8_t {
    // Atom types (TeX's 8 classes)
    ORD, OP, BIN, REL, OPEN, CLOSE, PUNCT, INNER,

    // Structural types
    ROW,        // Sequence of nodes
    FRAC,       // Fraction: \frac{num}{denom}
    SQRT,       // Square root: \sqrt{x}, \sqrt[n]{x}
    SCRIPTS,    // Sub/superscript: x_i^n
    DELIMITED,  // \left( ... \right)
    ACCENT,     // \hat{x}, \bar{x}
    OVERUNDER,  // \sum_{i=0}^n

    // Special
    TEXT,           // \text{...}, \mathrm{...}
    ARRAY,          // array/matrix environments
    ARRAY_ROW,      // Row in array
    ARRAY_CELL,     // Cell in array
    SPACE,          // \quad, \;, \,
    PHANTOM,        // \phantom, \hphantom, \vphantom, \smash
    NOT,            // \not (negation overlay)
    BOX,            // \bbox, \fbox, \boxed, \colorbox, overlap boxes
    STYLE,          // \displaystyle, \textstyle, etc.
    SIZED_DELIM,    // \big, \Big, \bigg, \Bigg
    ERROR,          // Parse error recovery
};

struct MathASTNode {
    MathNodeType type;
    uint8_t flags;

    // Flag bits
    static constexpr uint8_t FLAG_LIMITS = 0x01;      // Display limits (above/below)
    static constexpr uint8_t FLAG_LARGE = 0x02;       // Large variant requested
    static constexpr uint8_t FLAG_CRAMPED = 0x04;     // Cramped style
    static constexpr uint8_t FLAG_NOLIMITS = 0x08;    // Force no-limits
    static constexpr uint8_t FLAG_LEFT = 0x10;        // Left delimiter in pair
    static constexpr uint8_t FLAG_RIGHT = 0x20;       // Right delimiter in pair
    static constexpr uint8_t FLAG_MIDDLE = 0x40;      // Middle delimiter

    // Content (type-dependent union)
    union { ... };

    // Named branches (MathLive-inspired)
    MathASTNode* body;          // Main content
    MathASTNode* above;         // Numerator, index, over-content
    MathASTNode* below;         // Denominator, under-content
    MathASTNode* superscript;
    MathASTNode* subscript;

    // Siblings (for ROW children)
    MathASTNode* next_sibling;
};
```

**Node Type Categories:**

| Category | Types | Count |
|----------|-------|-------|
| Atom types (TeX 8 classes) | ORD, OP, BIN, REL, OPEN, CLOSE, PUNCT, INNER | 8 |
| Structural types | ROW, FRAC, SQRT, SCRIPTS, DELIMITED, ACCENT, OVERUNDER | 7 |
| Array types | ARRAY, ARRAY_ROW, ARRAY_CELL | 3 |
| Special types | TEXT, SPACE, PHANTOM, NOT, BOX, STYLE, SIZED_DELIM, ERROR | 8 |

**Named Branch Convention:**

| Node Type | body | above | below | superscript | subscript |
|-----------|------|-------|-------|-------------|-----------|
| FRAC | - | numerator | denominator | - | - |
| SQRT | radicand | index | - | - | - |
| SCRIPTS | nucleus | - | - | sup | sub |
| DELIMITED | content | - | - | - | - |
| ACCENT | base | - | - | - | - |
| OVERUNDER | nucleus | over | under | - | - |

### 3.2 TexNode (Layout Tree)

**Location:** `lambda/tex/tex_node.hpp`

TexNode represents typeset boxes with computed dimensions:

```cpp
enum class NodeClass : uint8_t {
    // Character nodes
    Char, Ligature,

    // Container nodes
    HList, VList, HBox, VBox, VTop,

    // Spacing nodes
    Glue, Kern, Penalty,

    // Rule nodes
    Rule,

    // Math-specific
    MathList, MathChar, MathOp, Fraction, Radical,
    Delimiter, Accent, Scripts,

    // Structure
    Paragraph, Page,

    // Special
    Mark, Insert, Adjust, Whatsit, Disc, Error
};

struct TexNode {
    NodeClass node_class;
    uint8_t flags;

    // Dimensions (CSS pixels)
    float width, height, depth;
    float italic, shift;

    // Position relative to parent
    float x, y;

    // Tree structure
    TexNode* parent;
    TexNode* first_child, *last_child;
    TexNode* next_sibling, *prev_sibling;

    // Content union (type-dependent)
    union Content { ... } content;
};
```

### 3.3 MathContext (Typesetting Context)

**Location:** `lambda/tex/tex_math_bridge.hpp`

```cpp
struct MathContext {
    Arena* arena;
    TFMFontManager* fonts;
    FontProvider* font_provider;  // Optional dual font support

    MathStyle style;              // Display, Text, Script, ScriptScript
    float base_size_pt;           // Base font size (10pt default)

    // Font specs
    FontSpec roman_font;          // cmr10 for digits, text
    FontSpec italic_font;         // cmmi10 for variables
    FontSpec symbol_font;         // cmsy10 for operators
    FontSpec extension_font;      // cmex10 for large symbols

    // Computed parameters
    float x_height, quad, axis_height, rule_thickness;

    // Factory methods
    static MathContext create(Arena* arena, TFMFontManager* fonts, float size_pt);
};
```

### 3.4 DocElement (Document Model)

**Location:** `lambda/tex/tex_document_model.hpp`

The document model bridges parsing and typesetting:

```cpp
struct DocElement {
    DocElemType type;
    uint8_t flags;

    union {
        // For MATH_* types
        struct {
            MathASTNode* ast;       // Phase A: populated during parsing
            TexNode* node;          // Phase B: populated during typesetting
            const char* latex_src;  // Original source
            const char* label;
            const char* number;
        } math;

        // Other content types...
    };

    // Tree structure
    DocElement* parent;
    DocElement* first_child, *last_child;
    DocElement* next_sibling, *prev_sibling;
};
```

---

## 4. Pipeline Phases

### 4.1 Phase A: Parsing (Font-Independent)

**Entry Point:** `parse_math_string_to_ast()` or `parse_math_to_ast()`

**File:** `lambda/tex/tex_math_ast_builder.cpp`

**Process:**
1. Tree-sitter parses LaTeX math → CST
2. `build_math_ast_node()` recursively builds MathASTNode tree
3. Command lookup tables map `\alpha`, `\frac`, etc. to node types

**Key Lookup Tables:**
- `GREEK_TABLE[]` - Greek letters (α→cmmi10 position 11)
- `BIG_OP_TABLE[]` - Operators (∑→cmex10 position 80/88)
- `SYMBOL_TABLE[]` - Relations, binary ops (≤→cmsy10 position 20)

### 4.2 Phase B: Typesetting (With Fonts)

**Entry Point:** `typeset_math_ast()`

**File:** `lambda/tex/tex_math_ast_typeset.cpp`

**Process:**
1. Walk MathAST tree recursively
2. Create TexNode boxes with TFM-derived dimensions
3. Apply inter-atom spacing (Rules 20-21 in TeXBook Appendix G)
4. Handle style changes (cramped, script, etc.)

**Key Functions:**
```cpp
typeset_math_ast()        // Main entry point
typeset_node()            // Dispatcher by node type
typeset_frac()            // Fractions (Rule 15)
typeset_sqrt_node()       // Radicals (Rule 11)
typeset_scripts_node()    // Sub/superscripts (Rule 18)
typeset_delimited_node()  // Delimiters (Rule 19)
typeset_atom()            // Atom to character box
```

### 4.3 Full Document Flow

For complete LaTeX documents:

```
.tex file
    │
    ▼
tree-sitter-latex → Lambda Element AST
    │
    ▼
build_doc_element() → DocElement tree
    │                 (math.ast populated, math.node = nullptr)
    │
    ▼
doc_element_to_texnode() → TexNode tree
    │                       (convert_math() calls typeset_math_ast())
    │
    ▼
dvi_write_document() / svg_write_document() / etc.
```

---

## 5. Font System

### 5.1 TFM (TeX Font Metrics)

**Location:** `lambda/tex/tex_tfm.hpp`, `lambda/tex/tex_tfm.cpp`

TFM files provide character metrics:

| Font | Purpose | Key Characters |
|------|---------|----------------|
| `cmr10` | Roman text | Digits 0-9, punctuation |
| `cmmi10` | Math italic | Variables a-z, Greek lowercase |
| `cmsy10` | Math symbols | Relations (≤≥), operators (×÷), arrows |
| `cmex10` | Extensions | Large operators, delimiters, radicals |

**TFM Parameters (fontdimen):**
```cpp
TFM_PARAM_X_HEIGHT     // Height of 'x'
TFM_PARAM_QUAD         // Width of 1em
TFM_PARAM_AXIS_HEIGHT  // Math axis (fraction bar center)
TFM_PARAM_NUM1/NUM2    // Numerator positioning
TFM_PARAM_DENOM1/DENOM2// Denominator positioning
TFM_PARAM_SUP1/SUP2/SUP3// Superscript positioning
TFM_PARAM_SUB1/SUB2    // Subscript positioning
TFM_PARAM_DELIM1/DELIM2// Delimiter sizing
```

### 5.2 Delimiter Sizing (TeX-Spec)

**Location:** `lambda/tex/tex_tfm.cpp` (`select_delimiter()`)

TeX's delimiter algorithm (TeXBook p.152, Rule 19):

1. Compute `required_size = max(target × 0.901, target - 5pt)`
2. Try small form from text font (cmr10/cmsy10)
3. Walk cmex10 "next larger" chain
4. Fall back to extensible recipe (top/mid/bot/rep pieces)

**cmex10 Delimiter Chains:**
```
Left Paren:   0 → 16 → 18 → 32 (extensible)
Right Paren:  1 → 17 → 19 → 33 (extensible)
Left Bracket: 2 → 20 → 34 → 50 → 104 (extensible)
```

### 5.3 Font Provider Interface

**Location:** `lambda/tex/tex_font_adapter.hpp`

For dual TFM/FreeType support:

```cpp
struct FontProvider {
    virtual const FontMetrics* get_math_text_font(float size_pt, bool bold) = 0;
    virtual const FontMetrics* get_math_symbol_font(float size_pt) = 0;
    virtual const FontMetrics* get_math_extension_font(float size_pt) = 0;
};
```

---

## 6. Output Formats

### 6.1 DVI Output

**Location:** `lambda/tex/tex_dvi_out.hpp`, `lambda/tex/tex_dvi_out.cpp`

DVI (DeVice Independent) is TeX's standard output format.

**API:**
```cpp
bool dvi_open(DVIWriter& writer, const char* filename, const DVIParams& params);
bool dvi_write_document(DVIWriter& writer, PageContent* pages, int count, TFMFontManager* fonts);
bool dvi_close(DVIWriter& writer);
```

**Unit Conversion:**
- All TexNode dimensions (CSS px) → scaled points for DVI
- `px_to_sp(float px)` = `px × 49315.3`

### 6.2 SVG Output

**Location:** `lambda/tex/tex_svg_out.hpp`, `lambda/tex/tex_svg_out.cpp`

```cpp
bool svg_render_to_file(TexNode* root, const char* filename, const SVGParams* params, Arena* arena);
const char* svg_render_math_inline(TexNode* node, Arena* arena, const SVGParams* params);
```

### 6.3 HTML Output

**Location:** `lambda/tex/tex_html_render.hpp`, `lambda/tex/tex_html_render.cpp`

Uses MathLive-compatible CSS classes:

```cpp
char* render_texnode_to_html(TexNode* node, Arena* arena, const HtmlRenderOptions& opts);
```

**CSS Class Convention:**
- `ML__mfrac` - Fraction container
- `ML__vlist` - Vertical list
- `ML__sqrt` - Square root
- `ML__sup`, `ML__sub` - Scripts

### 6.4 PDF Output

**Location:** `lambda/tex/tex_pdf_out.hpp`, `lambda/tex/tex_pdf_out.cpp`

Generates PDF via DVI intermediate or direct PDF primitives.

---

## 7. Key Implementation Files

### 7.1 Core Math Pipeline

| File | Lines | Purpose |
|------|-------|---------|
| `tex_math_ast.hpp` | ~340 | MathAST node definitions |
| `tex_math_ast_builder.cpp` | ~2630 | Phase A: Parse → MathAST |
| `tex_math_ast_typeset.cpp` | ~1580 | Phase B: MathAST → TexNode |
| `tex_math_bridge.hpp` | ~385 | MathContext, typesetting API |
| `tex_math_bridge.cpp` | ~1460 | Core math typesetting functions |

### 7.2 TeX Node System

| File | Lines | Purpose |
|------|-------|---------|
| `tex_node.hpp` | ~650 | TexNode structure, NodeClass enum |
| `tex_node.cpp` | ~200 | Node operations, tree management |
| `tex_glue.hpp` | ~335 | Glue system, unit conversions |
| `tex_font_metrics.hpp` | ~420 | MathStyle, font parameters |

### 7.3 Font System

| File | Lines | Purpose |
|------|-------|---------|
| `tex_tfm.hpp` | ~280 | TFM structures, parameter indices |
| `tex_tfm.cpp` | ~1150 | TFM parsing, delimiter selection |
| `tex_font_adapter.hpp` | ~260 | Dual font provider interface |

### 7.4 Output Renderers

| File | Lines | Purpose |
|------|-------|---------|
| `tex_dvi_out.cpp` | ~1195 | DVI bytecode generation |
| `tex_svg_out.cpp` | ~715 | SVG string generation |
| `tex_html_render.cpp` | ~745 | HTML+CSS generation |
| `tex_pdf_out.cpp` | ~505 | PDF output |
| `tex_png_out.cpp` | ~200 | PNG rasterization |

### 7.5 Document Model

| File | Lines | Purpose |
|------|-------|---------|
| `tex_document_model.hpp` | ~825 | DocElement, DocTextStyle |
| `tex_document_model.cpp` | ~7685 | Full document processing |
| `tex_doc_model_html.cpp` | ~1350 | DocElement → HTML |
| `tex_doc_model_builder.cpp` | ~1000 | Document tree building |
| `tex_doc_model_text.cpp` | ~500 | Text processing |

### 7.6 Text Processing

| File | Lines | Purpose |
|------|-------|---------|
| `tex_linebreak.cpp` | ~875 | Knuth-Plass line breaking |
| `tex_hyphen.cpp` | ~975 | Liang hyphenation algorithm |
| `tex_pagebreak.cpp` | ~950 | Page breaking |
| `tex_align.cpp` | ~650 | Alignment environments |

### 7.7 Bridge Files

| File | Lines | Purpose |
|------|-------|---------|
| `tex_latex_bridge.cpp` | ~1995 | LaTeX → TexNode conversion |
| `tex_lambda_bridge.cpp` | ~1075 | Lambda → TeX integration |
| `tex_math_ts.cpp` | ~400 | Tree-sitter math parsing entry |

### 7.8 Tree-sitter Grammar

| File | Lines | Purpose |
|------|-------|---------|
| `tree-sitter-latex-math/grammar.js` | ~540 | Math mode grammar definition |
| `tree-sitter-latex-math/src/parser.c` | (generated) | Tree-sitter parser |

**Grammar Structure:**
```javascript
// Entry point
math: choice($.infix_frac, repeat($._expression))

// Expression types
_expression: choice($_atom, $.group, $.subsup)

// Atoms (basic building blocks)
_atom: choice(
    $.symbol, $.number, $.operator, $.relation, $.punctuation,
    $.fraction, $.binomial, $.radical, $.symbol_command,
    $.delimiter_group, $.sized_delimiter, $.overunder_command,
    $.extensible_arrow, $.accent, $.box_command, $.color_command,
    $.rule_command, $.phantom_command, $.big_operator,
    $.environment, $.text_command, $.style_command, $.space_command,
    $.command  // Generic fallback
)
```

### 7.9 Symbol Lookup Tables

**Location:** `lambda/tex/tex_math_ast_builder.cpp`

Three static lookup tables map LaTeX commands to Unicode and font positions:

| Table | Entries | Purpose |
|-------|---------|---------|
| `GREEK_TABLE[]` | ~50 | Greek letters → cmmi10 positions |
| `SYMBOL_TABLE[]` | ~150 | Symbols → font/codepoint/atom class |
| `BIG_OP_TABLE[]` | ~20 | Big operators → cmex10 positions |

**GREEK_TABLE structure:**
```cpp
struct GreekEntry {
    const char* name;    // "alpha", "beta", etc.
    int32_t codepoint;   // Unicode: 0x03B1, 0x03B2
    int tfm_pos;         // cmmi10 position: 11, 12, etc.
};
```

**SYMBOL_TABLE structure:**
```cpp
struct SymbolEntry {
    const char* name;    // "leq", "times", etc.
    int32_t codepoint;   // Unicode codepoint
    uint8_t atom_type;   // AtomType: BIN, REL, etc.
    uint8_t font;        // SymFont: ROMAN, SYMBOL, etc.
};
```

**BIG_OP_TABLE structure:**
```cpp
struct BigOpEntry {
    const char* name;    // "sum", "int", etc.
    int32_t codepoint;   // Unicode codepoint
    int text_pos;        // Small size position in cmex10
    int display_pos;     // Large size position in cmex10
    bool uses_limits;    // Default limits behavior
};
```

---

## 8. Testing Infrastructure

### 8.1 MathLive AST Comparison Tests

**Location:** `test/latex/test_math_comparison.js`

**Strategy:**
- Compare Lambda math AST output against MathLive reference
- Weighted scoring: AST 50%, HTML 49%, DVI 1%
- Reference data from MathLive's parseLatex() function
- Fixtures in `test/latex/fixtures/math/` and `test/latex/fixtures/math/mathlive/`

**Comparators** (`test/latex/comparators/`):
- `mathlive_ast_comparator.js` - AST structure comparison
- `html_comparator.js` - HTML output comparison
- `dvi_comparator.js` - DVI output comparison

**Current Status (January 2026):**
- **Total Tests:** 248 test cases
- **Weighted Score:** 67.1%
- **Category Scores:** radicals 74%, operators 74%, greek 73.3%, delims 72.6%, accents 70.6%

### 8.2 Test Fixture Categories

**Math-specific fixtures** (`test/latex/fixtures/math/`):

| Category | Files | Description |
|----------|-------|-------------|
| `accents_*.tex` | 2 | Accent commands (\hat, \bar, etc.) |
| `arrays_*.tex` | 2 | Arrays and matrices |
| `bigops_*.tex` | 2 | Big operators (\sum, \int) |
| `delims_*.tex` | 2 | Delimiters (\left, \right) |
| `fracs_*.tex` | 3 | Fractions (\frac, \binom) |
| `greek_*.tex` | 2 | Greek letters |
| `scripts_*.tex` | 2 | Sub/superscripts |
| `symbols_*.tex` | 2 | Relations, arrows, operators |
| `misc_*.tex` | 4 | Mixed/complex expressions |

**Document fixtures** (`test/latex/fixtures/`):

| Category | Path | Description |
|----------|------|-------------|
| `basic/` | 3 files | Plain LaTeX text |
| `fonts/` | 24 files | Font selection and sizing |
| `graphics/` | 24 files | TikZ, pictures |
| `math/` | 33 files | Math formulas |
| `expansion/` | 15 files | Macro expansion |

### 8.3 Running Tests

```bash
# Build and run all tests
make build-test && make test

# Run MathLive comparison tests (recommended)
node test/latex/test_math_comparison.js

# Run DVI comparison tests
./test/test_latex_dvi_compare_gtest.exe

# Run math AST unit tests
./test/test_math_gtest.exe

# Run extended math tests
./test/test_tex_math_extended_gtest.exe

# Run specific GTest filter
./test/test_math_gtest.exe --gtest_filter="MathASTTest.*"
```

### 8.4 Regenerating Reference Files

```bash
# Generate reference DVI files using system TeX
node utils/generate_latex_refs.js --output-format=dvi --force
```

---

## 9. CLI Commands

### 9.1 Math Formula Commands

```bash
# Render math formula to DVI
./lambda.exe math "\frac{a}{b}" -o formula.dvi

# Render to HTML
./lambda.exe math "\frac{a}{b}" --html -o formula.html

# Render with AST dump (debugging)
./lambda.exe math "\frac{a}{b}" --dump-ast

# Render with box dump (debugging)
./lambda.exe math "\frac{a}{b}" --dump-boxes
```

### 9.2 Document Conversion

```bash
# Convert LaTeX document to DVI
./lambda.exe convert document.tex -t dvi -o output.dvi

# Convert LaTeX document to HTML (math rendered as SVG)
./lambda.exe convert document.tex -t html -o output.html

# Render to PDF
./lambda.exe render document.tex -o output.pdf

# Render to PNG
./lambda.exe render document.tex -o output.png
```

### 9.3 Debugging Commands

```bash
# View HTML in browser
./lambda.exe view document.html

# Layout analysis
./lambda.exe layout document.html
```

---

## 10. Current Status & Known Issues

### 10.1 Working Features (Baseline)

| Feature | Status | Score | Notes |
|---------|--------|-------|-------|
| Radicals | ✅ | 74.0% | \sqrt, \sqrt[n] |
| Operators | ✅ | 74.0% | \times, \cdot, \pm |
| Greek letters | ✅ | 73.3% | \alpha, \beta, etc. |
| Nested expressions | ✅ | 73.0% | Deeply nested structures |
| Delimiters | ✅ | 72.6% | \left( \right), sizing |
| Accents | ✅ | 70.6% | \hat, \bar, \vec |
| Boxes | ✅ | 70.0% | \phantom, \smash, overlaps |
| Fonts | ✅ | 67.2% | \mathbf, \mathbb, etc. |
| Fractions | ✅ | 66.9% | \frac, \binom, nested |
| Symbols | ✅ | 66.2% | Relations, arrows |
| Big operators | ✅ | 65.7% | \sum, \int with limits |

### 10.2 In Progress (Extended)

| Feature | Status | Score | Issue |
|---------|--------|-------|-------|
| Spacing | ⚠️ | 64.7% | \hskip dimension parsing |
| Negation | ⚠️ | 64.3% | \not overlay positioning |
| Styles | ⚠️ | 61.1% | \displaystyle propagation |
| Scripts | ⚠️ | 56.6% | Complex \limits/\nolimits |
| Arrays/matrices | ⚠️ | 54.4% | Column alignment incomplete |
| Misc | ⚠️ | 48.7% | Edge cases, complex nesting |

### 10.3 Test Score Breakdown

**By Category (MathLive comparison):**
```
radicals      74.0%  ██████████████░░░░░░
operators     74.0%  ██████████████░░░░░░
greek         73.3%  ██████████████░░░░░░
nested        73.0%  ██████████████░░░░░░
delims        72.6%  ██████████████░░░░░░
accents       70.6%  ██████████████░░░░░░
boxes         70.0%  ██████████████░░░░░░
fonts         67.2%  █████████████░░░░░░░
fracs         66.9%  █████████████░░░░░░░
symbols       66.2%  █████████████░░░░░░░
bigops        65.7%  █████████████░░░░░░░
spacing       64.7%  ████████████░░░░░░░░
negation      64.3%  ████████████░░░░░░░░
subjects      61.5%  ████████████░░░░░░░░
styles        61.1%  ████████████░░░░░░░░
scripts       56.6%  ███████████░░░░░░░░░
arrays        54.4%  ██████████░░░░░░░░░░
misc          48.7%  █████████░░░░░░░░░░░
```

See `vibe/Latex_Math_Test.md` for detailed test analysis.

---

## 11. Development Roadmap

### Phase 1: Quick Wins (Target: 70%+ overall)

1. **Scripts handling** - Fix `\limits`/`\nolimits` explicit commands (56.6% → 65%+)
2. **Spacing dimensions** - Parse `\hskip 3em`, `\kern 2pt` (64.7% → 70%+)
3. **Negation overlay** - Fix `\not` positioning (64.3% → 70%+)

### Phase 2: Core Improvements (Target: 75%+ overall)

4. **Array layout** - Two-pass column width, alignment (54.4% → 65%+)
5. **Style propagation** - `\displaystyle`, `\textstyle` in nested contexts
6. **Edge cases** - misc category improvements (48.7% → 60%+)

### Phase 3: Advanced Features (Target: 80%+ overall)

7. **Extensible arrows** - `\xrightarrow{text}`, `\xleftarrow{text}` with labels
8. **Wide accents** - Extensible `\widehat`, `\widetilde` with proper sizing
9. **Complex environments** - `align`, `gather`, `cases` environments

### Phase 4: HTML Output Integration

10. **Native HTML math** - Use `render_texnode_to_html()` in document conversion
11. **MathLive compatibility** - CSS class naming, baseline handling

---

## 12. Debugging Guide

### 12.1 Logging

All logging goes to `./log.txt`:

```cpp
log_debug("[TYPESET] typeset_frac: numer_width=%.2f denom_width=%.2f", w1, w2);
log_info("[PARSE] parse_math_to_ast: command=%s", cmd);
log_error("[ERROR] Unknown symbol: %s", name);
```

**Search Patterns:**
- `[TYPESET]` - Phase B typesetting
- `[PARSE]` - Phase A parsing
- `[DVI]` - DVI output
- `[TFM]` - Font metric lookups

### 12.2 AST Dumping

```cpp
// Dump MathAST to string
StrBuf buf;
strbuf_init(&buf);
math_ast_dump(ast, &buf, 0);
log_debug("AST:\n%s", strbuf_cstr(&buf));
```

CLI: `./lambda.exe math "\frac{a}{b}" --dump-ast`

### 12.3 Box Dumping

CLI: `./lambda.exe math "\frac{a}{b}" --dump-boxes`

Shows TexNode tree with dimensions.

### 12.4 DVI Inspection

Use `dvitype` to inspect DVI files:

```bash
dvitype output.dvi > output.dvi.txt
```

Compare against reference:
```bash
dvitype test/latex/expected/math/test_fraction.dvi > ref.txt
diff output.dvi.txt ref.txt
```

### 12.5 JSON Serialization

The AST can be serialized to JSON for comparison testing:

```cpp
// Serialize MathAST to JSON
StrBuf json;
strbuf_init(&json);
math_ast_to_json(ast, &json);
// Output: {"type":"FRAC","above":{"type":"ORD",...},"below":{...}}
```

**JSON Output Format:**
- Each node is an object with `type` field
- Branches are nested objects: `body`, `above`, `below`, `superscript`, `subscript`
- Atoms include: `command`, `codepoint`, `value`
- Special fields per type: `phantom_type`, `box_type`, `style_type`, `size_level`

**MathLive Comparison:**
The JSON output is designed to be comparable with MathLive's AST structure, enabling automated testing via `test/latex/comparators/mathlive_ast_comparator.js`.

### 12.6 Common Issues

| Symptom | Likely Cause | Debug Approach |
|---------|--------------|----------------|
| Missing character | Font lookup failed | Check log for TFM errors |
| Wrong positioning | Style not propagated | Check MathStyle in context |
| Wrong font | SYMBOL_TABLE mapping | Check SymFont enum |
| Spacing wrong | Inter-atom rules | Check AtomType classification |
| Delimiter too small | Chain not walked | Check TFM extensible chain |

---

## 13. Supported Commands

The math grammar supports **275 unique LaTeX commands**. While most are math-specific (operators, symbols, Greek letters), a subset of general LaTeX typesetting commands are also available within math mode.

### 13.1 General LaTeX Commands in Math Mode

These commands are general-purpose LaTeX constructs that work within math environments:

| Command | Category | Description |
|---------|----------|-------------|
| `\begin`, `\end` | Environment | Start/end environments (array, matrix, etc.) |
| `\text` | Text | Insert text within math |
| `\textbf`, `\textit`, `\textrm`, `\textsf`, `\texttt` | Text Styling | Bold, italic, roman, sans-serif, typewriter text |
| `\mbox`, `\hbox`, `\fbox` | Boxes | Text boxes, framed boxes |
| `\boxed` | Boxes | Box around math expression |
| `\bbox` | Boxes | Background box with optional color/padding |
| `\color`, `\textcolor`, `\colorbox` | Color | Text and background coloring |
| `\hspace` | Spacing | Horizontal space with dimension |
| `\quad`, `\qquad` | Spacing | Em and 2em horizontal space |
| `\phantom`, `\hphantom`, `\vphantom` | Phantom | Invisible boxes for spacing |
| `\smash` | Phantom | Zero height/depth box |
| `\llap`, `\rlap`, `\clap` | Overlap | Left, right, center overlap boxes |
| `\mathllap`, `\mathrlap`, `\mathclap` | Overlap | Math-mode overlap boxes |
| `\rule` | Rules | Draw horizontal/vertical rules |
| `\displaystyle`, `\textstyle`, `\scriptstyle`, `\scriptscriptstyle` | Style | Force math style |
| `\left`, `\right`, `\middle` | Delimiters | Auto-sizing delimiter groups |
| `\big`, `\Big`, `\bigg`, `\Bigg` | Delimiters | Fixed-size delimiter scaling |
| `\bigl`, `\bigm`, `\bigr`, etc. | Delimiters | Sized delimiters with atom class |
| `\overline`, `\underline` | Decorations | Over/under lines |
| `\overbrace`, `\underbrace` | Decorations | Braces with optional labels |

### 13.2 Font-Switching Commands

These commands change the font family within math mode:

| Command | Font Family | Example |
|---------|-------------|---------|
| `\mathrm` | Roman (upright) | $\mathrm{sin}$ |
| `\mathit` | Italic | $\mathit{diff}$ |
| `\mathbf` | Bold | $\mathbf{x}$ |
| `\mathsf` | Sans-serif | $\mathsf{A}$ |
| `\mathtt` | Typewriter | $\mathtt{code}$ |
| `\mathbb` | Blackboard bold | $\mathbb{R}$ |
| `\mathcal` | Calligraphic | $\mathcal{L}$ |
| `\mathfrak` | Fraktur | $\mathfrak{g}$ |
| `\mathscr` | Script | $\mathscr{H}$ |
| `\operatorname` | Operator name | $\operatorname{argmax}$ |

### 13.3 Command Statistics

| Category | Count | Percentage |
|----------|-------|------------|
| Math-specific commands | ~240 | ~87% |
| General LaTeX commands | ~35 | ~13% |
| **Total** | **275** | **100%** |

**Math-specific categories include:**
- Greek letters (lowercase and uppercase)
- Binary operators (+, ×, ⊕, etc.)
- Relations (=, ≤, ≈, ⊂, etc.)
- Arrows (→, ⇒, ↔, etc.)
- Big operators (∑, ∏, ∫, etc.)
- Fractions and roots (`\frac`, `\sqrt`, `\binom`)
- Accents (`\hat`, `\bar`, `\vec`, `\dot`)
- Delimiters (parentheses, brackets, braces)
- Named functions (`\sin`, `\cos`, `\log`, `\lim`)

---

## 14. References

### 14.1 TeX Documentation

- **TeXBook, Chapter 17** - More About Math (atoms, styles)
- **TeXBook, Chapter 18** - Fine Points (spacing, \over)
- **TeXBook, Appendix G** - 21 Rules of math typesetting

### 14.2 Internal Design Documents

| Document | Focus |
|----------|-------|
| `Latex_Math_Overall_Design.md` | This document - master reference |
| `Latex_Math_Test.md` | MathLive comparison test analysis |
| `Mathlive.md` | MathLive integration notes |
| `Math_Spacing.md` | TeX spacing rules and implementation |
| `Math_Input.md` | Math input parsing |
| `Math_Ascii.md` | ASCII math notation |

### 14.3 External References

- MathLive source: `./mathlive/src/core/` - Atom, Box, VBox
- LaTeXML source: Math grammar, operator precedence
- TFM format: TeX: The Program, Part 30
- DVI format: TeXBook Appendix A

*Last Updated: January 29, 2026*
