# LaTeX Math Typesetting Pipeline - Master Design Document

**Date:** January 23, 2026
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
13. [References](#13-references)

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
    TEXT, ARRAY, SPACE, PHANTOM, NOT, ERROR
};

struct MathASTNode {
    MathNodeType type;
    uint8_t flags;  // FLAG_LIMITS, FLAG_LARGE, FLAG_CRAMPED, etc.

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
| `tex_math_ast.hpp` | ~290 | MathAST node definitions |
| `tex_math_ast_builder.cpp` | ~1500 | Phase A: Parse → MathAST |
| `tex_math_ast_typeset.cpp` | ~1500 | Phase B: MathAST → TexNode |
| `tex_math_bridge.hpp` | ~400 | MathContext, typesetting API |
| `tex_math_bridge.cpp` | ~2300 | Core math typesetting functions |

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
| `tex_tfm.cpp` | ~800 | TFM parsing, delimiter selection |
| `tex_font_adapter.hpp` | ~200 | Dual font provider interface |

### 7.4 Output Renderers

| File | Lines | Purpose |
|------|-------|---------|
| `tex_dvi_out.cpp` | ~900 | DVI bytecode generation |
| `tex_svg_out.cpp` | ~600 | SVG string generation |
| `tex_html_render.cpp` | ~400 | HTML+CSS generation |
| `tex_pdf_out.cpp` | ~400 | PDF output |

### 7.5 Document Model

| File | Lines | Purpose |
|------|-------|---------|
| `tex_document_model.hpp` | ~820 | DocElement, DocTextStyle |
| `tex_document_model.cpp` | ~7500 | Full document processing |
| `tex_doc_model_html.cpp` | ~1200 | DocElement → HTML |

---

## 8. Testing Infrastructure

### 8.1 DVI Comparison Tests

**Location:** `test/test_latex_dvi_compare_gtest.cpp`

**Strategy:**
- Compare Lambda DVI output against reference DVI from TeX
- Use **character frequency comparison** (order-independent)
- Reference files in `test/latex/expected/`
- Fixtures in `test/latex/fixtures/`

**Test Classes:**
```cpp
DVICompareBaselineTest  // Must pass 100% - core functionality
DVICompareExtendedTest  // Work in progress - advanced features
```

**Current Status (January 2026):**
- **Baseline Tests:** 18-20 passing
- **Extended Tests:** ~46 tests for complex constructs

### 8.2 Test Fixture Categories

| Category | Path | Description |
|----------|------|-------------|
| `basic/` | Simple text | Plain LaTeX text |
| `math/` | Math formulas | Fractions, roots, scripts |
| `primitives/` | TeX primitives | \hskip, \kern, \penalty |
| `structure/` | Document structure | Sections, headings |
| `spacing/` | Spacing | Glue, boxes |

### 8.3 Running Tests

```bash
# Build and run all tests
make build-test && make test

# Run only baseline tests
./test/test_latex_dvi_compare_gtest.exe --gtest_filter="DVICompareBaselineTest.*"

# Run specific test
./test/test_latex_dvi_compare_gtest.exe --gtest_filter="DVICompareBaselineTest.Fraction"

# Verbose output
./test/test_latex_dvi_compare_gtest.exe --gtest_filter="DVICompareBaselineTest.*" --verbose
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

| Feature | Status | Notes |
|---------|--------|-------|
| Simple math | ✅ | Variables, numbers |
| Greek letters | ✅ | \alpha, \beta, etc. |
| Fractions | ✅ | \frac, nested |
| Square roots | ✅ | \sqrt, \sqrt[n] |
| Sub/superscripts | ✅ | x_i^n |
| Basic delimiters | ✅ | \left( \right) |
| Binary operators | ✅ | +, -, \times, \cdot |
| Relations | ✅ | =, <, >, \leq |
| Big operators | ✅ | \sum, \int (basic) |
| Inter-atom spacing | ✅ | TeX spacing rules |

### 10.2 In Progress (Extended)

| Feature | Status | Issue |
|---------|--------|-------|
| Accents | ⚠️ | Font selection, positioning |
| Wide accents | ⚠️ | \widehat, \widetilde sizing |
| Extensible arrows | ⚠️ | \xrightarrow not implemented |
| Arrays/matrices | ⚠️ | Alignment incomplete |
| Phantom boxes | ⚠️ | \phantom, \vphantom |
| Negation | ⚠️ | \not overlay |
| `\over` primitive | ⚠️ | Grouping issues |
| Complex limits | ⚠️ | \limits, \nolimits |

### 10.3 Known Test Failures

See `vibe/Latex_Math_Design5.md` for detailed analysis of extended test failures.

---

## 11. Development Roadmap

### Phase 1: Quick Wins (1-2 days each)

1. **Phantom boxes** - `\phantom`, `\hphantom`, `\vphantom`, `\smash`
2. **`\not` overlay** - Negation slash positioning
3. **Font scaling** - Fix heading font sizes (cmbx12 vs cmbx10)

### Phase 2: Core Improvements (3-5 days each)

4. **Accent system** - Font selection, skew correction, wide accents
5. **`\over`/`\choose`** - Generalized fraction primitives
6. **Complex scripts** - `\mathop`, `\limits`, `\nolimits`

### Phase 3: Major Features (1-2 weeks each)

7. **Array layout** - Two-pass column width calculation
8. **Extensible arrows** - `\xrightarrow`, `\xleftarrow`
9. **Wide accents** - Extensible `\widehat`, `\widetilde`

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

### 12.5 Common Issues

| Symptom | Likely Cause | Debug Approach |
|---------|--------------|----------------|
| Missing character | Font lookup failed | Check log for TFM errors |
| Wrong positioning | Style not propagated | Check MathStyle in context |
| Wrong font | SYMBOL_TABLE mapping | Check SymFont enum |
| Spacing wrong | Inter-atom rules | Check AtomType classification |
| Delimiter too small | Chain not walked | Check TFM extensible chain |

---

## 13. References

### 13.1 TeX Documentation

- **TeXBook, Chapter 17** - More About Math (atoms, styles)
- **TeXBook, Chapter 18** - Fine Points (spacing, \over)
- **TeXBook, Appendix G** - 21 Rules of math typesetting

### 13.2 Internal Design Documents

| Document | Focus |
|----------|-------|
| `Latex_Math_Design.md` | Initial unified pipeline proposal |
| `Latex_Math_Design2.md` | DVI pipeline, two-phase design |
| `Latex_Math_Design3_Html_Output.md` | HTML output integration |
| `Latex_Math_Design4.md` | Delimiter sizing, test approach |
| `Latex_Math_Design5.md` | Extended test analysis, roadmap |

### 13.3 External References

- MathLive source: `./mathlive/src/core/` - Atom, Box, VBox
- LaTeXML source: Math grammar, operator precedence
- TFM format: TeX: The Program, Part 30
- DVI format: TeXBook Appendix A

*Last Updated: January 23, 2026*
