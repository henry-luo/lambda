# LaTeX Typesetting Redesign for Lambda

This document outlines the design and implementation plan for adding true TeX/LaTeX typesetting support to Lambda, moving beyond the current HTML-based transformation approach.

## Table of Contents

1. [Current State Analysis](#1-current-state-analysis)
2. [Design Goals](#2-design-goals)
3. [Architecture Overview](#3-architecture-overview)
4. [Phase 1: Parser and AST Redesign](#4-phase-1-parser-and-ast-redesign)
5. [Phase 2: Typesetting Engine](#5-phase-2-typesetting-engine)
6. [Phase 3: Box Model and Rendering](#6-phase-3-box-model-and-rendering)
7. [Phase 4: Testing Infrastructure](#7-phase-4-testing-infrastructure)
8. [Implementation Timeline](#8-implementation-timeline)

---

## 1. Current State Analysis

### 1.1 Existing Components

#### LaTeX Parser (`lambda/input/input-latex-ts.cpp`)
- **Technology**: Tree-sitter grammar (`lambda/tree-sitter-latex/grammar.js`)
- **Output**: Lambda Element tree (DOM-like structure)
- **Approach**: Structural parsing only, semantic interpretation deferred to formatter
- **Strengths**:
  - Proper handling of document structure, environments, commands
  - External scanner for verbatim content
  - Good error recovery via GLR parsing
- **Weaknesses**:
  - AST doesn't capture TeX semantic concepts (modes, glue, penalties)
  - Math content parsed minimally (delegated to math parser)
  - No distinction between horizontal/vertical mode

#### LaTeX Formatter (`lambda/format/format_latex_html_v2.cpp`)
- **Output**: HTML with CSS styling
- **Approach**: Transform Element tree to HTML DOM
- **Strengths**:
  - Handles many LaTeX commands, environments
  - Supports document classes, packages
  - Diacritics, ligatures, spacing rules
- **Weaknesses**:
  - No true typesetting - relies on browser for layout
  - CSS-based spacing approximations
  - Cannot produce precise TeX-like output

#### Math Parser (`lambda/input/input-math2.cpp`)
- **Technology**: Tree-sitter grammar (`lambda/tree-sitter-latex-math/grammar.js`)
- **Output**: MathNode tree (Lambda maps)
- **Strengths**:
  - Proper structural parsing of math
  - Atom type classification (ord, op, bin, rel, etc.)
  - Handles fractions, radicals, scripts, delimiters, accents

#### Math Layout (`radiant/layout_math.cpp`, `radiant/math_box.hpp`)
- **Output**: MathBox tree for rendering
- **Approach**: TeXBook-based algorithms
- **Strengths**:
  - Inter-box spacing tables (TeXBook Ch. 18)
  - Math styles (display, text, script, scriptscript)
  - Fraction, radical, script layout rules
- **Can be extended**: This is our foundation for full typesetting

### 1.2 Reference Implementations

#### latex.js
- **Approach**: PEG.js parser → Generator → HTML DOM
- **Key Files**:
  - `latex-parser.pegjs`: Grammar with semantic actions
  - `html-generator.ls`: HTML element creation
  - `generator.ls`: Core processing logic
- **Takeaways**:
  - Macro expansion at parse time
  - Dynamic argument parsing based on macro signatures
  - Document class and package handling

#### MathLive (`mathlive/src/core/`)
- **Approach**: Tokenizer → Parser → Atom tree → Box tree → HTML
- **Key Files**:
  - `atom-class.ts`: Atom hierarchy with branches
  - `box.ts`: Layout box with dimensions
  - `context.ts`: Rendering context with math styles
  - `mathstyle.ts`: 8 TeX math styles
  - `inter-box-spacing.ts`: Spacing tables
  - `v-box.ts`: Vertical box stacking
- **Takeaways**:
  - Clean separation: Atom (semantic) vs Box (layout)
  - Context inheritance for style propagation
  - Proper TeX spacing algorithm implementation

---

## 2. Design Goals

### 2.1 Primary Goals

1. **True Typesetting**: Implement TeX's algorithms for line/page breaking, not CSS approximations
2. **Semantic AST**: Parse LaTeX into a structured tree that captures TeX semantics
3. **Unified Pipeline**: Single architecture for both text and math typesetting
4. **DVI Compatibility**: Output that can be compared with reference TeX implementations

### 2.2 Non-Goals (Initial Version)

- Full LaTeX macro compatibility (subset of common packages)
- BibTeX/bibliography support
- Cross-references and TOC generation
- Multi-file document support

### 2.3 Success Criteria

- Math output pixel-identical to MathLive
- Text output measurably close to TeX/pdflatex DVI
- Performance: < 100ms for typical article-length documents

---

## 3. Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              INPUT                                          │
│                    LaTeX Source String                                      │
└─────────────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                         PHASE 1: PARSING                                    │
│  ┌─────────────┐    ┌──────────────────┐    ┌───────────────────────────┐  │
│  │ Tree-sitter │───▶│   AST Builder    │───▶│      TeX AST              │  │
│  │   Lexer     │    │ (expand macros)  │    │ (semantic nodes)          │  │
│  └─────────────┘    └──────────────────┘    └───────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                     PHASE 2: TYPESETTING                                    │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                    Typesetting Engine                                │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────────┐  │   │
│  │  │ Math Layout  │  │ Paragraph    │  │ Page Breaking            │  │   │
│  │  │ (TeXBook     │  │ Builder      │  │ (Knuth-Plass for pages)  │  │   │
│  │  │  Appendix G) │  │ (H-lists)    │  │                          │  │   │
│  │  └──────────────┘  └──────────────┘  └──────────────────────────┘  │   │
│  │                            │                                        │   │
│  │                            ▼                                        │   │
│  │  ┌──────────────────────────────────────────────────────────────┐  │   │
│  │  │            Line Breaking (Knuth-Plass Algorithm)             │  │   │
│  │  │            - Glue, penalties, discretionary breaks            │  │   │
│  │  └──────────────────────────────────────────────────────────────┘  │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                       PHASE 3: BOX MODEL                                    │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                        TeX Box Tree                                  │   │
│  │   ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐  │   │
│  │   │  HBox   │  │  VBox   │  │  Glue   │  │  Kern   │  │  Rule   │  │   │
│  │   │(h-list) │  │(v-list) │  │(stretch)│  │(fixed)  │  │(line)   │  │   │
│  │   └─────────┘  └─────────┘  └─────────┘  └─────────┘  └─────────┘  │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                         PHASE 4: OUTPUT                                     │
│  ┌───────────────┐  ┌───────────────┐  ┌───────────────┐                   │
│  │   Radiant     │  │     PDF       │  │     DVI       │                   │
│  │   Renderer    │  │    Export     │  │    Export     │                   │
│  │   (existing)  │  │  (new/extend) │  │    (new)      │                   │
│  └───────────────┘  └───────────────┘  └───────────────┘                   │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Phase 1: Parser and AST Redesign

### 4.1 Tree-sitter Grammar Enhancements

The current grammar is adequate for structural parsing but needs refinements:

#### 4.1.1 Current Grammar Issues

```javascript
// Current: Grammar treats all commands uniformly
command: $ => prec.right(seq(
  field('name', $.command_name),
  repeat(field('arg', choice(
    $.curly_group,
    $.brack_group,
    $.star,
  ))),
)),
```

**Problems**:
1. No argument arity information (how many args does `\frac` take?)
2. No mode awareness (text vs math)
3. Paragraph breaks not semantically significant

#### 4.1.2 Proposed Grammar Changes

```javascript
// Keep the grammar simple but add semantic markers

// 1. Explicit math environment boundaries
math_zone: $ => choice(
  $.inline_math,       // $...$, \(...\)
  $.display_math,      // $$...$$, \[...\]
  $.math_environment,  // \begin{equation}...\end{equation}
),

// 2. Paragraph structure
paragraph: $ => prec.right(seq(
  repeat1($._horizontal_content),
  optional($.paragraph_break),
)),

// 3. Keep command parsing generic, interpret at AST build time
// This maintains parser simplicity while allowing semantic enrichment
```

#### 4.1.3 Grammar vs AST Builder Split

**Principle**: Keep grammar simple, do semantic interpretation in AST builder.

| Responsibility | Grammar | AST Builder |
|---------------|---------|-------------|
| Token recognition | ✓ | |
| Structure nesting | ✓ | |
| Command arity | | ✓ |
| Mode tracking | | ✓ |
| Macro expansion | | ✓ |
| Glue/penalty insertion | | ✓ |

### 4.2 Semantic AST Design

#### 4.2.1 Node Types

Define a typed AST that captures TeX semantics:

```cpp
// tex_ast.hpp - TeX semantic AST nodes

namespace tex {

// ============================================================================
// Base AST Node
// ============================================================================

enum class NodeType {
    // Document structure
    Document,
    Preamble,
    Body,

    // Modes
    HMode,           // Horizontal mode (paragraphs)
    VMode,           // Vertical mode (page building)
    MathMode,        // Math mode (inline or display)

    // Horizontal content
    CharNode,        // Single character
    HBox,            // Horizontal box
    VBox,            // Vertical box
    Glue,            // Stretchable/shrinkable space
    Kern,            // Fixed space
    Penalty,         // Break penalty
    Discretionary,   // Discretionary hyphen
    Rule,            // Horizontal or vertical rule

    // Math content (reuse existing MathNodeType)
    MathAtom,
    MathFraction,
    MathRadical,
    MathScript,
    MathDelimiter,
    MathAccent,
    MathOp,
    MathArray,

    // Special
    Mark,            // For headers/footers
    Insert,          // Floating content
    Adjustment,      // \vadjust material
};

// ============================================================================
// Glue - The heart of TeX spacing
// ============================================================================

struct Glue {
    float space;      // Natural width/height
    float stretch;    // Maximum stretch
    float shrink;     // Maximum shrink
    int stretch_order; // 0=finite, 1=fil, 2=fill, 3=filll
    int shrink_order;

    static Glue fixed(float s) { return {s, 0, 0, 0, 0}; }
    static Glue flexible(float s, float st, float sh) {
        return {s, st, sh, 0, 0};
    }
    static Glue fil(float s, float st) {
        return {s, st, 0, 1, 0};
    }
};

// Standard LaTeX glues
constexpr Glue THIN_MUSKIP = {3.0f * MU, 0, 0, 0, 0};
constexpr Glue MED_MUSKIP  = {4.0f * MU, 2.0f * MU, 4.0f * MU, 0, 0};
constexpr Glue THICK_MUSKIP = {5.0f * MU, 5.0f * MU, 0, 0, 0};

// ============================================================================
// Penalty - Controls line/page breaking
// ============================================================================

struct Penalty {
    int value;  // -10000 = force break, +10000 = forbid break

    static constexpr int FORCE_BREAK = -10000;
    static constexpr int FORBID_BREAK = 10000;
};

// ============================================================================
// AST Node
// ============================================================================

struct TexNode {
    NodeType type;

    union {
        // CharNode
        struct {
            int codepoint;
            const char* font;
        } char_data;

        // Glue
        Glue glue_data;

        // Kern
        float kern_data;

        // Penalty
        Penalty penalty_data;

        // Box (HBox, VBox)
        struct {
            TexNode** children;
            int count;
            float width;      // Target width (for setting)
            float height;
            float depth;
        } box_data;

        // Rule
        struct {
            float width;
            float height;
            float depth;
        } rule_data;

        // Discretionary
        struct {
            TexNode* pre_break;   // Material before break
            TexNode* post_break;  // Material after break
            TexNode* no_break;    // Material if no break
        } disc_data;

        // Math (pointer to existing math node)
        Item math_data;
    };

    // Source location for error reporting
    int source_start;
    int source_end;
};

} // namespace tex
```

#### 4.2.2 AST Builder Algorithm

```cpp
// tex_ast_builder.cpp - Build TeX AST from Tree-sitter CST

class TexAstBuilder {
public:
    TexAstBuilder(Input* input);

    // Main entry point
    TexNode* build(TSNode root, const char* source);

private:
    // Mode tracking (crucial for TeX semantics)
    enum class Mode { Vertical, Horizontal, Math, RestrictedHorizontal };
    Mode current_mode = Mode::Vertical;

    // Macro expansion context
    MacroTable macros;
    int expansion_depth = 0;

    // Core processing methods
    TexNode* process_document(TSNode node);
    TexNode* process_paragraph(TSNode node);
    TexNode* process_command(TSNode node);
    TexNode* process_math(TSNode node);
    TexNode* process_environment(TSNode node);

    // Glue and penalty insertion
    void insert_interword_glue(HList& hlist);
    void insert_inter_sentence_glue(HList& hlist, bool after_period);
    void insert_math_spacing(HList& hlist, MathAtomType left, MathAtomType right);

    // Mode transitions
    void enter_horizontal_mode();
    void leave_horizontal_mode();  // Creates paragraph box
    void enter_math_mode(bool display);
    void leave_math_mode();

    // Hyphenation
    void hyphenate_word(const char* word, HList& hlist);
};
```

### 4.3 Macro System

Support a subset of TeX/LaTeX macro functionality:

```cpp
// tex_macros.hpp

struct MacroDef {
    const char* name;
    int num_args;           // 0-9
    const char* default_opt; // Optional argument default (nullptr if none)
    const char* replacement; // Replacement text with #1, #2, etc.
};

class MacroTable {
public:
    // Built-in LaTeX macros
    void load_latex_kernel();
    void load_package(const char* name);

    // Define new macro
    void define(const char* name, int num_args, const char* replacement);
    void define_with_opt(const char* name, int num_args,
                         const char* default_opt, const char* replacement);

    // Expand macro call
    TexNode* expand(const char* name, const std::vector<TexNode*>& args);

private:
    std::unordered_map<std::string, MacroDef> macros;
};
```

---

## 5. Phase 2: Typesetting Engine

### 5.1 Core Concepts from TeXBook

#### 5.1.1 Lists and Modes

TeX builds documents using **lists**:

| List Type | Contains | Built in Mode |
|-----------|----------|---------------|
| Horizontal (H-list) | Characters, boxes, glue, kerns | Horizontal mode |
| Vertical (V-list) | Boxes, glue, penalties, rules | Vertical mode |
| Math list | Atoms, fractions, scripts | Math mode |

#### 5.1.2 The Paragraph Builder

Convert H-list to lines using Knuth-Plass algorithm:

```cpp
// paragraph_builder.hpp

class ParagraphBuilder {
public:
    // Input: H-list (sequence of horizontal items)
    // Output: V-list of line boxes
    VList build(const HList& hlist, float line_width, const ParagraphParams& params);

private:
    // Knuth-Plass optimal line breaking
    struct Breakpoint {
        int position;        // Index in H-list
        int line;            // Line number
        float totalWidth;    // Total width up to this point
        float totalStretch;  // Total stretch
        float totalShrink;   // Total shrink
        float demerits;      // Accumulated demerits
        Breakpoint* previous; // Best path to here
    };

    // Find optimal breakpoints
    std::vector<int> find_breaks(const HList& hlist, float line_width);

    // Compute badness of a line
    float badness(float actual_width, float target_width,
                  float stretch, float shrink);

    // Compute demerits
    float demerits(float badness, int penalty, bool flagged);
};

struct ParagraphParams {
    float line_width;
    float parindent;
    float baselineskip;
    float lineskip;
    float lineskiplimit;
    int tolerance;          // Max acceptable badness
    int pretolerance;       // First pass tolerance
    int hyphenpenalty;
    int exhyphenpenalty;
    float emergencystretch; // Last resort stretch
};
```

#### 5.1.3 The Page Builder

```cpp
// page_builder.hpp

class PageBuilder {
public:
    // Input: V-list of paragraphs and other vertical material
    // Output: List of pages
    std::vector<Page> build(const VList& vlist, const PageParams& params);

private:
    // Page breaking with insertions (floats, footnotes)
    struct PageBreakpoint {
        int position;
        float page_goal;     // Target height
        float page_total;    // Current total
        float page_stretch;
        float page_shrink;
        float cost;
    };

    // Handle insertions
    void process_insert(const InsertNode* insert);
    void place_floats(Page& page);
};

struct PageParams {
    float page_height;
    float page_width;
    float top_skip;
    float max_depth;
    float text_height;
    float text_width;
};
```

### 5.2 Math Typesetting (Extend Existing)

The existing `radiant/layout_math.cpp` implements much of TeXBook Appendix G. Extend it:

#### 5.2.1 Missing Features

1. **Cramped styles**: Partially implemented, needs completion
2. **Delimiters**: Need extensible delimiter construction
3. **Arrays/matrices**: Basic support, need alignment
4. **Over/under scripts**: For `\sum`, `\lim` with limits

#### 5.2.2 Integration with Text

```cpp
// math_integration.hpp (extend existing)

class MathIntegration {
public:
    // Convert inline math to H-list item
    HBox* layout_inline_math(const TexNode* math, MathContext& ctx);

    // Convert display math to V-list item (centered, with spacing)
    VBox* layout_display_math(const TexNode* math, MathContext& ctx);

private:
    // Existing layout_math.cpp functions
    MathBox* layout_math_node(Item node, MathContext& ctx, Arena* arena);
};
```

### 5.3 Font Metrics

```cpp
// tex_font_metrics.hpp

struct TexFontMetrics {
    // Per-character metrics
    float char_width(int codepoint);
    float char_height(int codepoint);
    float char_depth(int codepoint);
    float char_italic(int codepoint);

    // Font-level parameters
    float slant;
    float space;           // Normal interword space
    float space_stretch;
    float space_shrink;
    float x_height;
    float quad;            // 1em
    float extra_space;     // Space after sentence

    // Math font parameters (Appendix G, Table G)
    float num1, num2, num3;           // Fraction numerator shifts
    float denom1, denom2;             // Fraction denominator shifts
    float sup1, sup2, sup3;           // Superscript shifts
    float sub1, sub2;                 // Subscript shifts
    float sup_drop, sub_drop;         // Script drops
    float delim1, delim2;             // Delimiter sizes
    float axis_height;                // Math axis
    float default_rule_thickness;
    float big_op_spacing[5];          // Limits spacing
};

// Load metrics from OpenType MATH table or TFM file
TexFontMetrics load_font_metrics(FT_Face face);
```

---

## 6. Phase 3: Box Model and Rendering

### 6.1 Unified Box Structure

Extend existing `MathBox` to handle all TeX boxes:

```cpp
// tex_box.hpp

namespace tex {

enum class BoxType {
    // From existing MathBox
    Empty, Glyph, HBox, VBox, Kern, Rule,
    // New for full TeX
    Glue,           // Stretchable space
    Penalty,        // Break point marker
    Discretionary,  // Hyphenation point
    Ligature,       // Merged characters
    Leaders,        // Repeated pattern (dots, rules)
    Special,        // DVI specials, PDF annotations
};

struct TexBox {
    BoxType type;

    // Dimensions (in CSS pixels, converted from TeX points)
    float width;
    float height;   // Above baseline
    float depth;    // Below baseline

    // For math: atom type for spacing
    MathBoxType math_type;

    // Position relative to parent
    float x, y;

    // Content
    union {
        // Glyph
        struct {
            int codepoint;
            FT_Face face;
            float scale;
        } glyph;

        // HBox/VBox
        struct {
            TexBox** children;
            int count;
            Glue glue_set;    // How glue was set
            float glue_ratio; // Stretch/shrink ratio
        } box;

        // Glue
        Glue glue;

        // Kern
        float kern;

        // Rule
        struct {
            float thickness; // 0 = use default
        } rule;

        // Discretionary
        struct {
            TexBox* pre_break;
            TexBox* post_break;
            TexBox* no_break;
        } disc;

        // Leaders
        struct {
            TexBox* pattern;
            Glue space;
        } leaders;
    };

    // Tree navigation
    TexBox* parent;
    TexBox* first_child;
    TexBox* next_sibling;

    // Source mapping
    int source_start;
    int source_end;
};

// Factory functions
TexBox* make_char_box(Arena* arena, int codepoint, FT_Face face, const TexFontMetrics& metrics);
TexBox* make_hbox(Arena* arena, TexBox** children, int count);
TexBox* make_vbox(Arena* arena, TexBox** children, int count);
TexBox* make_glue_box(Arena* arena, const Glue& glue);
TexBox* make_kern_box(Arena* arena, float kern);
TexBox* make_rule_box(Arena* arena, float width, float height, float depth);

} // namespace tex
```

### 6.2 Box Setting (Glue Distribution)

```cpp
// box_setting.hpp

// Set an H-box to a specific width
void set_hbox_width(TexBox* hbox, float target_width);

// Set a V-box to a specific height
void set_vbox_height(TexBox* vbox, float target_height);

// Implementation detail: glue distribution
void distribute_glue(TexBox* box, float excess, bool horizontal) {
    // Collect total stretch/shrink
    Glue totals = {0, 0, 0, 0, 0};
    for (TexBox* child = box->first_child; child; child = child->next_sibling) {
        if (child->type == BoxType::Glue) {
            // Add to totals at appropriate order
            if (child->glue.stretch_order == totals.stretch_order)
                totals.stretch += child->glue.stretch;
            else if (child->glue.stretch_order > totals.stretch_order)
                totals = child->glue; // Higher order dominates
            // Similar for shrink...
        }
    }

    // Compute glue ratio
    float ratio;
    if (excess >= 0) {
        ratio = (totals.stretch > 0) ? excess / totals.stretch : 0;
    } else {
        ratio = (totals.shrink > 0) ? -excess / totals.shrink : 0;
    }

    // Apply to each glue
    float pos = 0;
    for (TexBox* child = box->first_child; child; child = child->next_sibling) {
        if (horizontal) child->x = pos;
        else child->y = pos;

        float size = child->width; // or height for vertical
        if (child->type == BoxType::Glue) {
            if (excess >= 0)
                size += ratio * child->glue.stretch;
            else
                size -= ratio * child->glue.shrink;
        }

        pos += size;
    }
}
```

### 6.3 Rendering Integration

Extend Radiant renderer to handle TeX boxes:

```cpp
// render_tex.cpp (new file)

void render_tex_box(RenderContext& ctx, const TexBox* box, float x, float y) {
    switch (box->type) {
        case BoxType::Glyph:
            render_glyph(ctx, box->glyph.codepoint, box->glyph.face,
                        x + box->x, y + box->y, box->glyph.scale);
            break;

        case BoxType::HBox:
        case BoxType::VBox:
            for (TexBox* child = box->first_child; child; child = child->next_sibling) {
                render_tex_box(ctx, child, x + box->x, y + box->y);
            }
            break;

        case BoxType::Rule:
            render_rule(ctx, x + box->x, y + box->y - box->height,
                       box->width, box->height + box->depth);
            break;

        case BoxType::Glue:
        case BoxType::Kern:
            // Invisible, just positioning
            break;

        case BoxType::Leaders:
            render_leaders(ctx, box, x, y);
            break;
    }
}
```

### 6.4 Alignment with Existing MathBox

The existing `MathBox` in `radiant/math_box.hpp` should be merged or aliased:

```cpp
// Compatibility layer
namespace radiant {
    using MathBox = tex::TexBox;  // Unified type

    // Keep existing factory functions working
    inline MathBox* make_glyph_box(Arena* arena, ...) {
        return tex::make_char_box(arena, ...);
    }
}
```

---

## 7. Phase 4: Testing Infrastructure

### 7.1 DVI Reference Generation

Use standard TeX to generate reference DVI files:

```bash
#!/bin/bash
# generate_reference_dvi.sh

# Directory containing test .tex files
TEST_DIR="test/latex"
OUTPUT_DIR="test/latex/reference"

mkdir -p "$OUTPUT_DIR"

for tex_file in "$TEST_DIR"/*.tex; do
    base=$(basename "$tex_file" .tex)

    # Generate DVI using latex
    latex -output-directory="$OUTPUT_DIR" "$tex_file"

    # Convert DVI to text representation for comparison
    dvitype "$OUTPUT_DIR/$base.dvi" > "$OUTPUT_DIR/$base.dvi.txt"
done
```

### 7.2 DVI Parser for Lambda

```cpp
// dvi_parser.hpp - Parse DVI files for comparison

namespace dvi {

struct DVICommand {
    enum Type {
        SET_CHAR, SET_RULE, PUT_CHAR, PUT_RULE,
        NOP, BOP, EOP, PUSH, POP,
        RIGHT, DOWN, W, X, Y, Z,
        FNT_DEF, FNT_NUM, SPECIAL, PRE, POST
    };

    Type type;
    union {
        int char_code;
        struct { int width, height; } rule;
        struct { int a, b, c, d; } move;
        struct { int k; const char* name; } font_def;
        struct { const char* str; int len; } special;
    };
};

class DVIParser {
public:
    DVIParser(const char* filename);

    // Get page as list of positioned glyphs
    struct PositionedGlyph {
        int codepoint;
        float x, y;
        int font_num;
    };
    std::vector<PositionedGlyph> parse_page(int page_num);

    // Get page dimensions
    float page_width(int page_num);
    float page_height(int page_num);
};

} // namespace dvi
```

### 7.3 Comparison Script

```python
#!/usr/bin/env python3
# compare_dvi_output.py - Compare Lambda output with DVI reference

import sys
import subprocess
import json
from dataclasses import dataclass
from typing import List, Tuple

@dataclass
class Glyph:
    codepoint: int
    x: float
    y: float
    font: str

def parse_dvi_txt(filename: str) -> List[Glyph]:
    """Parse dvitype output to extract glyph positions."""
    glyphs = []
    current_h, current_v = 0.0, 0.0

    with open(filename) as f:
        for line in f:
            if line.startswith('setchar'):
                code = int(line.split()[1])
                glyphs.append(Glyph(code, current_h, current_v, ''))
            elif line.startswith('right'):
                current_h += float(line.split()[1])
            elif line.startswith('down'):
                current_v += float(line.split()[1])
            # ... handle other DVI commands

    return glyphs

def parse_lambda_output(json_file: str) -> List[Glyph]:
    """Parse Lambda TexBox tree JSON output."""
    with open(json_file) as f:
        data = json.load(f)

    glyphs = []
    def walk(node, x=0, y=0):
        if node['type'] == 'glyph':
            glyphs.append(Glyph(
                node['codepoint'],
                x + node.get('x', 0),
                y + node.get('y', 0),
                node.get('font', '')
            ))
        for child in node.get('children', []):
            walk(child, x + node.get('x', 0), y + node.get('y', 0))

    walk(data)
    return glyphs

def compare_outputs(dvi_glyphs: List[Glyph],
                    lambda_glyphs: List[Glyph],
                    tolerance: float = 1.0) -> Tuple[int, int, List[str]]:
    """Compare two glyph lists, return (matches, total, differences)."""
    matches = 0
    differences = []

    # Sort by position for stable comparison
    dvi_sorted = sorted(dvi_glyphs, key=lambda g: (g.y, g.x))
    lambda_sorted = sorted(lambda_glyphs, key=lambda g: (g.y, g.x))

    for i, (dvi_g, lam_g) in enumerate(zip(dvi_sorted, lambda_sorted)):
        if dvi_g.codepoint != lam_g.codepoint:
            differences.append(f"Glyph {i}: char mismatch {dvi_g.codepoint} vs {lam_g.codepoint}")
        elif abs(dvi_g.x - lam_g.x) > tolerance or abs(dvi_g.y - lam_g.y) > tolerance:
            differences.append(f"Glyph {i} ({chr(dvi_g.codepoint)}): pos ({dvi_g.x:.1f},{dvi_g.y:.1f}) vs ({lam_g.x:.1f},{lam_g.y:.1f})")
        else:
            matches += 1

    # Check for length differences
    if len(dvi_glyphs) != len(lambda_glyphs):
        differences.append(f"Glyph count: DVI has {len(dvi_glyphs)}, Lambda has {len(lambda_glyphs)}")

    return matches, max(len(dvi_glyphs), len(lambda_glyphs)), differences

def main():
    if len(sys.argv) < 3:
        print("Usage: compare_dvi_output.py <dvi.txt> <lambda.json>")
        sys.exit(1)

    dvi_glyphs = parse_dvi_txt(sys.argv[1])
    lambda_glyphs = parse_lambda_output(sys.argv[2])

    matches, total, diffs = compare_outputs(dvi_glyphs, lambda_glyphs)

    print(f"Match rate: {matches}/{total} ({100*matches/total:.1f}%)")
    if diffs:
        print("\nDifferences:")
        for d in diffs[:20]:  # Show first 20
            print(f"  {d}")

    sys.exit(0 if len(diffs) == 0 else 1)

if __name__ == '__main__':
    main()
```

### 7.4 Test Cases Structure

```
test/latex/
├── reference/              # Generated DVI reference files
│   ├── simple.dvi
│   ├── simple.dvi.txt
│   ├── math.dvi
│   └── ...
├── output/                 # Lambda output for comparison
│   ├── simple.json
│   ├── simple.pdf
│   └── ...
├── simple.tex              # Basic paragraph
├── math.tex                # Math formulas
├── fractions.tex           # Fraction layout
├── scripts.tex             # Subscripts/superscripts
├── spacing.tex             # Various spacing
├── hyphenation.tex         # Line breaking with hyphens
├── multiline.tex           # Multi-paragraph
├── lists.tex               # itemize, enumerate
├── tables.tex              # tabular environment
└── full_article.tex        # Complete document
```

### 7.5 Makefile Integration

```makefile
# Makefile additions

# Generate reference DVI files
test/latex/reference/%.dvi: test/latex/%.tex
	latex -output-directory=test/latex/reference $<

test/latex/reference/%.dvi.txt: test/latex/reference/%.dvi
	dvitype $< > $@

# Generate Lambda output
test/latex/output/%.json: test/latex/%.tex ./lambda.exe
	./lambda.exe typeset $< -o $@

# Compare single file
test-latex-%: test/latex/reference/%.dvi.txt test/latex/output/%.json
	python3 utils/compare_dvi_output.py $^

# Compare all
test-latex-all: $(patsubst test/latex/%.tex,test-latex-%,$(wildcard test/latex/*.tex))
	@echo "All LaTeX tests completed"

# Baseline tests (must pass)
test-latex-baseline: test-latex-simple test-latex-math test-latex-fractions
	@echo "Baseline LaTeX tests passed"
```

---

## 8. Implementation Timeline

### Phase 1: Foundation (2-3 weeks)

**Week 1-2: AST Redesign**
- [ ] Define `tex_ast.hpp` with node types
- [ ] Implement `TexAstBuilder` class
- [ ] Add mode tracking (H-mode, V-mode, Math)
- [ ] Integrate with existing tree-sitter parser

**Week 3: Macro System**
- [ ] Implement basic `MacroTable`
- [ ] Load LaTeX kernel macros
- [ ] Support user-defined macros via `\newcommand`

### Phase 2: Typesetting Core (3-4 weeks)

**Week 4-5: Paragraph Building**
- [ ] Implement `Glue` and `Penalty` structures
- [ ] Build H-list from text content
- [ ] Implement Knuth-Plass line breaking (simplified)
- [ ] Handle hyphenation (use existing hyphenation patterns)

**Week 6-7: Math Integration**
- [ ] Extend existing `layout_math.cpp` for text integration
- [ ] Implement cramped styles fully
- [ ] Add extensible delimiter construction
- [ ] Support display math environments

### Phase 3: Box Model (2 weeks)

**Week 8: Unified Box Structure**
- [ ] Merge `MathBox` with new `TexBox`
- [ ] Implement box setting (glue distribution)
- [ ] Add leaders support

**Week 9: Rendering**
- [ ] Extend Radiant renderer for TeX boxes
- [ ] Implement rule drawing
- [ ] Add DVI/PDF output option

### Phase 4: Testing (2 weeks)

**Week 10: Test Infrastructure**
- [ ] Set up DVI reference generation
- [ ] Implement DVI parser
- [ ] Write comparison script
- [ ] Create initial test cases

**Week 11: Validation**
- [ ] Run baseline tests
- [ ] Fix discrepancies
- [ ] Document known limitations

### Total: ~11 weeks

---

## Appendix A: Key TeXBook References

| Topic | TeXBook Section |
|-------|-----------------|
| Modes | Chapter 13 |
| Glue | Chapter 12 |
| Boxes | Chapter 12 |
| Line breaking | Chapter 14 |
| Math typesetting | Chapter 17-18 |
| Math algorithms | Appendix G |
| Penalties | Chapter 14 |
| Page breaking | Chapter 15 |
| Font metrics | Appendix F |

## Appendix B: Unit Conversions

```cpp
// TeX/LaTeX units to CSS pixels
constexpr float PT_PER_IN = 72.27f;       // TeX points per inch
constexpr float BP_PER_IN = 72.0f;        // Big points (PDF/PostScript)
constexpr float CSS_PX_PER_IN = 96.0f;    // CSS reference pixel

constexpr float PT_TO_PX = CSS_PX_PER_IN / PT_PER_IN;  // 1.3281...
constexpr float BP_TO_PX = CSS_PX_PER_IN / BP_PER_IN;  // 1.3333...

// Math unit (mu) = 1/18 em
constexpr float MU_TO_EM = 1.0f / 18.0f;

inline float pt_to_px(float pt) { return pt * PT_TO_PX; }
inline float px_to_pt(float px) { return px / PT_TO_PX; }
inline float mu_to_px(float mu, float em_size) { return mu * MU_TO_EM * em_size; }
```

## Appendix C: File Organization

```
lambda/
├── tex/                      # New: TeX typesetting engine
│   ├── tex_ast.hpp          # AST node definitions
│   ├── tex_ast_builder.cpp  # Build AST from CST
│   ├── tex_macros.hpp       # Macro system
│   ├── tex_macros.cpp
│   ├── tex_paragraph.hpp    # Paragraph builder
│   ├── tex_paragraph.cpp
│   ├── tex_page.hpp         # Page builder
│   ├── tex_page.cpp
│   ├── tex_font_metrics.hpp # Font metrics
│   ├── tex_font_metrics.cpp
│   └── tex_box.hpp          # Box definitions (extend math_box.hpp)
├── input/
│   └── input-latex-ts.cpp   # Updated: use TexAstBuilder
├── format/
│   ├── format_latex_html_v2.cpp  # Keep for HTML output
│   └── format_latex_tex.cpp      # New: TeX box output
└── tree-sitter-latex/
    └── grammar.js            # Minor updates

radiant/
├── layout_math.cpp          # Extended for text integration
├── math_box.hpp             # -> tex_box.hpp alias
├── render_tex.cpp           # New: TeX box renderer
└── render_tex.hpp

test/
├── latex/                   # New test directory
│   ├── reference/
│   ├── output/
│   └── *.tex
└── test_latex_gtest.cpp     # New: LaTeX typesetting tests

utils/
└── compare_dvi_output.py    # New: DVI comparison script
```
