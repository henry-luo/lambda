# Math 2.0 Design Proposal

A new LaTeX math implementation for Radiant, inspired by MathLive's architecture.

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Phase 1: Parsing with Tree-sitter](#phase-1-parsing-with-tree-sitter)
4. [Phase 2: Math Node Tree](#phase-2-math-node-tree)
5. [Phase 3: Math Layout/Typesetting](#phase-3-math-layouttypesetting)
6. [Phase 4: Integration with Radiant](#phase-4-integration-with-radiant)
7. [Implementation Plan](#implementation-plan)

---

## Overview

### Goals

1. **High-quality math typesetting** following TeX/LaTeX conventions
2. **Integration with Radiant** reusing existing font, text, and rendering infrastructure
3. **Clean architecture** with separation between parsing, semantic representation, and layout
4. **Extensibility** for custom commands and macros

### Design Principles

1. **Hybrid Tree-sitter parsing** - Document grammar detects math spans; separate math grammar parses content
2. **Lambda-native data structures** - Math nodes are Lambda elements, enabling scripting and transformation
3. **MathLive-inspired layout** - Follow TeXBook algorithms with Atom→Box rendering pipeline
4. **Radiant view integration** - New `VIEW_BOX` type for math boxes within the existing view tree

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              Input: LaTeX String                            │
└─────────────────────────────────────────────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  Phase 1: PARSING                                                           │
│  ┌─────────────────────────┐    ┌─────────────────────────────────────────┐ │
│  │ tree-sitter-latex       │    │ tree-sitter-latex-math                  │ │
│  │ (Document Grammar)      │───▶│ (Math Grammar)                          │ │
│  │ - Detect math spans     │    │ - Parse math structure                  │ │
│  │ - $...$ and $$...$$     │    │ - Fractions, radicals, scripts          │ │
│  └─────────────────────────┘    └─────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  Phase 2: SEMANTIC TREE                                                     │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ MathNode Tree (Lambda Elements)                                         ││
│  │ - MathFrac, MathSurd, MathSubsup, MathSymbol, MathOp, ...              ││
│  │ - Atom types: mord, mbin, mrel, mop, mopen, mclose, mpunct, minner     ││
│  │ - Style properties: color, size, variant                                ││
│  └─────────────────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  Phase 3: LAYOUT                                                            │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ layout_math(MathNode, MathContext) → MathBox                            ││
│  │ - TeXBook algorithms (fractions, scripts, delimiters)                   ││
│  │ - Inter-box spacing                                                      ││
│  │ - Font metrics (σ, ξ constants)                                         ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ MathBox Tree (VIEW_BOX nodes)                                           ││
│  │ - Dimensions: height, depth, width                                       ││
│  │ - Position relative to baseline                                          ││
│  │ - Box types for spacing: ord, bin, rel, op, open, close, punct, inner  ││
│  └─────────────────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  Phase 4: RENDERING                                                         │
│  ┌──────────────────┐  ┌──────────────────┐  ┌────────────────────────────┐ │
│  │ FreeType         │  │ ThorVG           │  │ Radiant View Tree          │ │
│  │ - Font loading   │  │ - Vector paths   │  │ - Integration with DOM     │ │
│  │ - Glyph metrics  │  │ - SVG rendering  │  │ - Paint to canvas          │ │
│  └──────────────────┘  └──────────────────┘  └────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Tree Management: Separate vs Unified

A key architectural decision: how to manage the relationship between semantic (MathNode) and layout (MathBox) trees?

#### Option 1: Separate Trees (Recommended)

```cpp
// Math Node Tree (Lambda elements) - persists
struct MathNode {
    MathNodeType type;      // frac, sqrt, subsup, symbol...
    MathAtomType atom_type; // ord, bin, rel, op...
    Item children;          // Lambda list of child nodes
    int source_start, source_end;  // Source location
};

// View Box Tree - created during layout, can be discarded
struct MathBox {
    float height, depth, width;
    MathBoxType type;       // for inter-box spacing
    MathBox* children;
    Item source_node;       // back-pointer to semantic node
};
```

**Why Math is Different from HTML/CSS:**

Radiant uses a unified DOM/View tree for HTML because DOM structure ≈ visual structure (mostly 1:1). Math breaks this assumption - the mapping is **1:N**:

```
Math Node (semantic)          View Boxes (layout)
─────────────────────         ─────────────────────
MathFrac                  →   VBox
  ├─ numer: [x, +, 1]           ├─ HBox (numerator) ─ shifted up
  └─ denom: [2]                 ├─ Rule (fraction bar)
                                └─ HBox (denominator) ─ shifted down
                                    └─ + kern boxes for spacing
```

A single `MathFrac` node produces 3+ boxes. A `MathSubsup` produces base + positioned script boxes.

**Pros:**
1. **Re-layout without re-parse** - Change font size? Just re-run `layout_math()` on same node tree
2. **Multiple outputs** - Render same expression at different sizes (inline vs display)
3. **Memory efficiency** - Box tree is transient, discard after paint
4. **Clean algorithms** - Layout code doesn't pollute semantic structure
5. **Follows TeX/MathLive model** - Proven architecture

**Cons:**
1. Two allocations per expression
2. Need to maintain back-pointers for selection/editing

#### Option 2: Unified Tree

```cpp
// Single structure for both semantics and layout
struct MathNode {
    MathNodeType type;
    MathAtomType atom_type;
    Item children;
    // Embedded layout info
    float height, depth, width;  // computed during layout
    float x, y;                   // position
};
```

**Pros:**
1. Simpler structure, one allocation
2. Consistent with Radiant DOM/View pattern

**Cons:**
1. **1:N problem** - Where do the 3 boxes for a fraction go? Need auxiliary structures anyway
2. **Re-layout mutates data** - Can't keep multiple layouts
3. **Lifetime confusion** - When to recompute layout fields?
4. **Mixing concerns** - Semantic node contaminated with layout state

#### Decision: Option 1

| Aspect | Option 1 (Separate) | Option 2 (Unified) |
|--------|---------------------|-------------------|
| 1:N node→box mapping | ✅ Natural | ❌ Awkward |
| Re-layout same source | ✅ Easy | ❌ Mutates data |
| Memory | Arena for boxes | Mixed lifetimes |
| Complexity | Moderate | Hidden complexity |
| MathLive/TeX alignment | ✅ Yes | ❌ No |

#### Memory Management

Use arena allocation for box trees (like Radiant's view properties):

```cpp
// Per-frame or per-expression arena for boxes
Arena math_box_arena;

MathBox* alloc_math_box() {
    return (MathBox*)arena_alloc(&math_box_arena, sizeof(MathBox));
}

// After painting, reset entire arena
void reset_math_boxes() {
    arena_reset(&math_box_arena);
}
```

This gives the best of both worlds: separate trees conceptually, but efficient bulk allocation/deallocation.

---

## Phase 1: Parsing with Tree-sitter

### Hybrid Approach

Use two grammars with different responsibilities:

#### 1.1 Document Grammar (`tree-sitter-latex`)

Minimal math detection - captures math spans as raw content:

```javascript
// Existing grammar.js - keep math rules simple
inline_math: $ => choice(
  seq('$', field('content', $.math_content), '$'),
  seq('\\(', field('content', $.math_content), '\\)'),
),

display_math: $ => choice(
  seq('$$', field('content', $.math_content), '$$'),
  seq('\\[', field('content', $.math_content), '\\]'),
),

// Capture everything between delimiters as raw text
math_content: $ => /[^$]+/,  // Simple: just capture raw content
```

#### 1.2 Math Grammar (`tree-sitter-latex-math`)

New grammar for detailed math structure:

```javascript
// tree-sitter-latex-math/grammar.js
module.exports = grammar({
  name: 'latex_math',

  // Whitespace is ignored in math mode
  extras: $ => [/\s+/, $.comment],

  rules: {
    // Entry point
    math: $ => repeat($._expression),

    _expression: $ => choice(
      $._atom,
      $.group,
      $.subsup,
    ),

    _atom: $ => choice(
      $.symbol,
      $.number,
      $.operator,
      $.command,
      $.fraction,
      $.radical,
      $.delimiter_group,
      $.accent,
      $.text_command,
    ),

    // Basic elements
    symbol: $ => /[a-zA-Z]/,
    number: $ => /[0-9]+\.?[0-9]*/,
    operator: $ => /[+\-*\/=<>!,:;.?]/,

    // Groups
    group: $ => seq('{', repeat($._expression), '}'),

    // Sub/superscript
    subsup: $ => prec.right(seq(
      $._base,
      choice(
        seq('_', $._script, optional(seq('^', $._script))),
        seq('^', $._script, optional(seq('_', $._script))),
      ),
    )),
    _base: $ => $._atom,
    _script: $ => choice($.group, $.symbol, $.number),

    // Fractions: \frac{num}{denom}
    fraction: $ => seq(
      field('cmd', choice('\\frac', '\\dfrac', '\\tfrac', '\\cfrac')),
      field('numer', $.group),
      field('denom', $.group),
    ),

    // Radicals: \sqrt[index]{radicand}
    radical: $ => seq(
      '\\sqrt',
      optional(field('index', $.brack_group)),
      field('radicand', $.group),
    ),
    brack_group: $ => seq('[', repeat($._expression), ']'),

    // Delimiters: \left( ... \right)
    delimiter_group: $ => seq(
      '\\left', field('left_delim', $.delimiter),
      repeat($._expression),
      '\\right', field('right_delim', $.delimiter),
    ),
    delimiter: $ => choice(
      '(', ')', '[', ']', '\\{', '\\}', '|', '\\|',
      '\\langle', '\\rangle', '\\lfloor', '\\rfloor',
      '\\lceil', '\\rceil', '.',
    ),

    // Accents: \hat{x}, \vec{v}
    accent: $ => seq(
      field('cmd', choice(
        '\\hat', '\\check', '\\tilde', '\\acute', '\\grave',
        '\\dot', '\\ddot', '\\breve', '\\bar', '\\vec',
        '\\widehat', '\\widetilde', '\\overline', '\\underline',
      )),
      field('base', choice($.group, $.symbol)),
    ),

    // Generic command with arguments
    command: $ => prec.right(seq(
      field('name', $.command_name),
      repeat(field('arg', choice($.group, $.brack_group))),
    )),
    command_name: $ => /\\[a-zA-Z@]+\*?/,

    // Text mode inside math: \text{...}, \mathrm{...}
    text_command: $ => seq(
      choice('\\text', '\\textrm', '\\textit', '\\textbf',
             '\\mathrm', '\\mathit', '\\mathbf', '\\mathsf', '\\mathtt',
             '\\operatorname'),
      $.group,
    ),

    comment: $ => /%[^\n]*/,
  },
});
```

### 1.3 Parsing Flow in C++

```cpp
// lambda/input/input-math.cpp

#include "lambda-data.hpp"
#include "parse.h"  // Tree-sitter wrapper

// Step 1: Parse document to find math spans
Item parse_latex_document(const char* source) {
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_latex());
    TSTree* tree = ts_parser_parse_string(parser, NULL, source, strlen(source));
    // ... traverse tree, extract math content
}

// Step 2: Parse math content with math grammar
Item parse_math_content(const char* math_source) {
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_latex_math());
    TSTree* tree = ts_parser_parse_string(parser, NULL, math_source, strlen(math_source));
    
    // Convert CST to MathNode tree
    return build_math_tree(ts_tree_root_node(tree), math_source);
}
```

---

## Phase 2: Math Node Tree

### 2.1 MathNode Types

Math nodes are Lambda elements with specific shapes. Following MathLive's Atom classification:

```cpp
// lambda/math_node.hpp

// Atom types (TeXBook classification)
enum class MathAtomType {
    Ord,      // Ordinary: variables, constants
    Op,       // Large operators: \sum, \int
    Bin,      // Binary operators: +, -, \times
    Rel,      // Relations: =, <, \leq
    Open,     // Opening delimiters: (, [, \{
    Close,    // Closing delimiters: ), ], \}
    Punct,    // Punctuation: ,
    Inner,    // Fractions, delimited subformulas
};

// Node types (structural)
enum class MathNodeType {
    Symbol,       // Single character or command symbol
    Number,       // Numeric literal
    Operator,     // Binary/relational operator
    Fraction,     // \frac{num}{denom}
    Radical,      // \sqrt[index]{radicand}
    Subsup,       // Subscript/superscript
    Delimiter,    // \left...\right
    Accent,       // \hat, \vec, etc.
    Array,        // Matrix, align, cases
    Group,        // Grouping braces
    Text,         // \text{...}
    Space,        // Explicit spacing
    Error,        // Parse error
};
```

### 2.2 Lambda Element Representation

Using Lambda's element/map structure:

```cpp
// Build math nodes as Lambda elements
Item build_math_frac(MarkBuilder& builder, Item numer, Item denom, const char* cmd) {
    MapBuilder mb = builder.createMap();
    mb.put("type", builder.createSymbol("frac"));
    mb.put("command", builder.createString(cmd));
    mb.put("numer", numer);
    mb.put("denom", denom);
    mb.put("atom_type", builder.createSymbol("inner"));
    return mb.final();
}

Item build_math_symbol(MarkBuilder& builder, const char* value, MathAtomType atom_type) {
    MapBuilder mb = builder.createMap();
    mb.put("type", builder.createSymbol("symbol"));
    mb.put("value", builder.createString(value));
    mb.put("atom_type", builder.createSymbol(atom_type_name(atom_type)));
    return mb.final();
}

Item build_math_subsup(MarkBuilder& builder, Item base, Item sub, Item sup) {
    MapBuilder mb = builder.createMap();
    mb.put("type", builder.createSymbol("subsup"));
    mb.put("base", base);
    if (sub != ItemNull) mb.put("sub", sub);
    if (sup != ItemNull) mb.put("sup", sup);
    return mb.final();
}
```

### 2.3 Symbol/Command Tables

Runtime lookup tables for command semantics (similar to MathLive's definitions):

```cpp
// lambda/math_symbols.cpp

struct MathSymbolDef {
    const char* command;      // LaTeX command
    int codepoint;            // Unicode codepoint
    MathAtomType atom_type;   // Atom classification
    const char* variant;      // Font variant (optional)
};

// Greek letters
static const MathSymbolDef GREEK_SYMBOLS[] = {
    {"\\alpha",   0x03B1, MathAtomType::Ord, nullptr},
    {"\\beta",    0x03B2, MathAtomType::Ord, nullptr},
    {"\\gamma",   0x03B3, MathAtomType::Ord, nullptr},
    {"\\Gamma",   0x0393, MathAtomType::Ord, nullptr},
    // ...
};

// Operators
static const MathSymbolDef OPERATOR_SYMBOLS[] = {
    {"\\sum",     0x2211, MathAtomType::Op, nullptr},
    {"\\prod",    0x220F, MathAtomType::Op, nullptr},
    {"\\int",     0x222B, MathAtomType::Op, nullptr},
    // ...
};

// Relations
static const MathSymbolDef RELATION_SYMBOLS[] = {
    {"\\leq",     0x2264, MathAtomType::Rel, nullptr},
    {"\\geq",     0x2265, MathAtomType::Rel, nullptr},
    {"\\neq",     0x2260, MathAtomType::Rel, nullptr},
    // ...
};
```

---

## Phase 3: Math Layout/Typesetting

### 3.1 MathContext

Context for math layout, analogous to MathLive's Context:

```cpp
// radiant/math_context.hpp

enum class MathStyle {
    Display,          // D  - displaystyle
    DisplayCramped,   // D' - displaystyle cramped
    Text,             // T  - textstyle
    TextCramped,      // T' - textstyle cramped
    Script,           // S  - scriptstyle
    ScriptCramped,    // S' - scriptstyle cramped
    ScriptScript,     // SS - scriptscriptstyle
    ScriptScriptCramped, // SS' - scriptscriptstyle cramped
};

struct MathContext {
    MathContext* parent;
    
    // Current style
    MathStyle style;
    
    // Font settings
    float base_font_size;      // in px
    const char* font_family;   // e.g., "Latin Modern Math"
    
    // Colors
    uint32_t color;
    uint32_t background_color;
    
    // Computed properties
    float scaling_factor() const;
    bool is_display_style() const;
    bool is_cramped() const;
    bool is_tight() const;
    
    // Style transitions (TeXBook rules)
    MathStyle sup_style() const;
    MathStyle sub_style() const;
    MathStyle frac_num_style() const;
    MathStyle frac_den_style() const;
    MathStyle cramped_style() const;
    
    // Font metrics for current style
    const MathFontMetrics& metrics() const;
    
    // Create child context with new style
    MathContext derive(MathStyle new_style) const;
};
```

### 3.2 MathFontMetrics

Font metrics following TeX conventions:

```cpp
// radiant/math_font_metrics.hpp

struct MathFontMetrics {
    // Dimensions in em (relative to font size)
    float x_height;           // σ5 - height of lowercase 'x'
    float quad;               // σ6 - em width
    float axis_height;        // σ22 - math axis height
    
    // Fraction positioning
    float num1;               // σ8 - numerator shift (display)
    float num2;               // σ9 - numerator shift (text, with bar)
    float num3;               // σ10 - numerator shift (text, no bar)
    float denom1;             // σ11 - denominator shift (display)
    float denom2;             // σ12 - denominator shift (text)
    
    // Super/subscript positioning
    float sup1;               // σ13 - superscript shift (display)
    float sup2;               // σ14 - superscript shift (text)
    float sup3;               // σ15 - superscript shift (cramped)
    float sub1;               // σ16 - subscript shift
    float sub2;               // σ17 - subscript shift (with superscript)
    float sup_drop;           // σ18 - superscript baseline drop
    float sub_drop;           // σ19 - subscript baseline drop
    
    // Delimiter sizing
    float delim1;             // σ20 - delimiter size (display)
    float delim2;             // σ21 - delimiter size (text)
    
    // Rules and spacing
    float default_rule_thickness;  // ξ8
    float big_op_spacing1;    // ξ9
    float big_op_spacing2;    // ξ10
    float big_op_spacing3;    // ξ11
    float big_op_spacing4;    // ξ12
    float big_op_spacing5;    // ξ13
};

// Pre-computed metrics for each style size
extern const MathFontMetrics MATH_METRICS[3];  // [normal, script, scriptscript]
```

### 3.3 MathBox (VIEW_BOX)

New view type for math boxes:

```cpp
// radiant/math_box.hpp

enum class MathBoxType {
    Ord,      // For inter-box spacing
    Bin,
    Rel,
    Op,
    Open,
    Close,
    Punct,
    Inner,
    // Special types
    Ignore,   // No spacing contribution
    Lift,     // Lift children for spacing calculation
};

struct MathBox {
    // Dimensions (in em, relative to current font size)
    float height;      // Distance above baseline
    float depth;       // Distance below baseline
    float width;       // Horizontal width
    float italic;      // Italic correction
    float skew;        // Skew for accents
    
    // Box type for inter-box spacing
    MathBoxType type;
    
    // Scaling relative to parent
    float scale;
    
    // Content
    enum ContentType { GLYPH, HBOX, VBOX, KERN, RULE, SVG };
    ContentType content_type;
    
    union {
        struct { int codepoint; const char* font; } glyph;
        struct { MathBox* children; int count; } hbox;
        struct { MathBox* children; int count; float* shifts; } vbox;
        struct { float amount; } kern;
        struct { float thickness; } rule;
        struct { const char* path; } svg;
    } content;
    
    // Tree structure
    MathBox* parent;
    MathBox* next_sibling;
    MathBox* first_child;
    
    // Source mapping (for selection/editing)
    int source_start;
    int source_end;
};
```

### 3.4 Layout Functions

Core layout functions implementing TeXBook algorithms:

```cpp
// radiant/layout_math.cpp

// Main entry point
MathBox* layout_math(Item math_node, MathContext& ctx);

// Layout for specific node types
MathBox* layout_symbol(Item node, MathContext& ctx);
MathBox* layout_fraction(Item node, MathContext& ctx);
MathBox* layout_radical(Item node, MathContext& ctx);
MathBox* layout_subsup(Item node, MathContext& ctx);
MathBox* layout_delimiter(Item node, MathContext& ctx);
MathBox* layout_accent(Item node, MathContext& ctx);
MathBox* layout_array(Item node, MathContext& ctx);

// Helpers
MathBox* make_hbox(MathBox** children, int count);
MathBox* make_vbox_stacked(MathBox** children, int count, float* shifts);
MathBox* make_kern(float amount);
MathBox* make_rule(float width, float height, float depth);
MathBox* make_glyph(int codepoint, const char* font, MathContext& ctx);

// Inter-box spacing
void apply_inter_box_spacing(MathBox* root, MathContext& ctx);
```

### 3.5 Key Layout Algorithms

#### Fraction Layout (TeXBook Rule 15)

```cpp
MathBox* layout_fraction(Item node, MathContext& ctx) {
    Item numer = get_field(node, "numer");
    Item denom = get_field(node, "denom");
    bool has_bar = get_bool_field(node, "has_bar", true);
    
    // Render numerator and denominator in appropriate styles
    MathContext num_ctx = ctx.derive(ctx.frac_num_style());
    MathContext den_ctx = ctx.derive(ctx.frac_den_style());
    
    MathBox* numer_box = layout_math(numer, num_ctx);
    MathBox* denom_box = layout_math(denom, den_ctx);
    
    const MathFontMetrics& m = ctx.metrics();
    float rule_thickness = has_bar ? m.default_rule_thickness : 0;
    
    // Calculate shifts (Rule 15b)
    float numer_shift, denom_shift, clearance;
    if (ctx.is_display_style()) {
        numer_shift = m.num1;
        denom_shift = m.denom1;
        clearance = has_bar ? 3 * rule_thickness : 7 * rule_thickness;
    } else {
        numer_shift = has_bar ? m.num2 : m.num3;
        denom_shift = m.denom2;
        clearance = has_bar ? rule_thickness : 3 * m.default_rule_thickness;
    }
    
    // Adjust for minimum clearance (Rule 15c, 15d)
    // ... (adjustment logic)
    
    // Build vertical stack
    MathBox* children[3];
    float shifts[3];
    int count = 0;
    
    children[count] = numer_box;
    shifts[count++] = -numer_shift;
    
    if (has_bar) {
        children[count] = make_rule(fmax(numer_box->width, denom_box->width),
                                     rule_thickness, 0);
        shifts[count++] = -m.axis_height;
    }
    
    children[count] = denom_box;
    shifts[count++] = denom_shift;
    
    MathBox* frac = make_vbox_stacked(children, count, shifts);
    frac->type = MathBoxType::Inner;
    return frac;
}
```

#### Subscript/Superscript Layout (TeXBook Rules 18a-f)

```cpp
MathBox* layout_subsup(Item node, MathContext& ctx) {
    Item base_node = get_field(node, "base");
    Item sub_node = get_field(node, "sub");
    Item sup_node = get_field(node, "sup");
    
    MathBox* base = layout_math(base_node, ctx);
    MathBox* sub_box = nullptr;
    MathBox* sup_box = nullptr;
    
    const MathFontMetrics& m = ctx.metrics();
    
    float sup_shift = 0, sub_shift = 0;
    
    // Rule 18a: Render superscript
    if (sup_node != ItemNull) {
        MathContext sup_ctx = ctx.derive(ctx.sup_style());
        sup_box = layout_math(sup_node, sup_ctx);
        sup_shift = base->height - m.sup_drop * sup_ctx.scaling_factor();
    }
    
    // Render subscript
    if (sub_node != ItemNull) {
        MathContext sub_ctx = ctx.derive(ctx.sub_style());
        sub_box = layout_math(sub_node, sub_ctx);
        sub_shift = base->depth + m.sub_drop * sub_ctx.scaling_factor();
    }
    
    // Rule 18c: Minimum superscript shift
    float min_sup_shift;
    if (ctx.is_display_style()) min_sup_shift = m.sup1;
    else if (ctx.is_cramped()) min_sup_shift = m.sup3;
    else min_sup_shift = m.sup2;
    
    if (sup_box) {
        sup_shift = fmax(sup_shift, min_sup_shift);
        sup_shift = fmax(sup_shift, sup_box->depth + 0.25 * m.x_height);
    }
    
    // Rule 18b: Minimum subscript shift
    if (sub_box && !sup_box) {
        sub_shift = fmax(sub_shift, m.sub1);
        sub_shift = fmax(sub_shift, sub_box->height - 0.8 * m.x_height);
    }
    
    // Rule 18e: Both sub and sup - ensure minimum gap
    if (sub_box && sup_box) {
        float gap = (sup_shift - sup_box->depth) - (sub_box->height - sub_shift);
        if (gap < 4 * m.default_rule_thickness) {
            sub_shift += (4 * m.default_rule_thickness - gap);
            // Additional adjustment for cramped...
        }
    }
    
    // Build result
    MathBox* result = make_hbox_with_scripts(base, sub_box, sup_box, 
                                              sub_shift, sup_shift);
    return result;
}
```

### 3.6 Inter-Box Spacing

```cpp
// Spacing table (values in mu: 3=thin, 4=med, 5=thick)
static const int INTER_BOX_SPACING[8][8] = {
    //        Ord Op Bin Rel Open Close Punct Inner
    /* Ord */   {0, 3,  4,  5,  0,   0,    0,    3},
    /* Op  */   {3, 3,  0,  5,  0,   0,    0,    3},
    /* Bin */   {4, 4,  0,  0,  4,   0,    0,    4},
    /* Rel */   {5, 5,  0,  0,  5,   0,    0,    5},
    /* Open*/   {0, 0,  0,  0,  0,   0,    0,    0},
    /* Close*/  {0, 3,  4,  5,  0,   0,    0,    3},
    /* Punct*/  {3, 3,  0,  3,  3,   0,    3,    3},
    /* Inner*/  {3, 3,  4,  5,  3,   0,    3,    3},
};

// Tight spacing for script/scriptscript styles
static const int INTER_BOX_TIGHT_SPACING[8][8] = {
    // Most entries are 0; only Op-related spacing remains
    /* Ord */   {0, 3,  0,  0,  0,   0,    0,    0},
    /* Op  */   {3, 3,  0,  0,  0,   0,    0,    0},
    // ...
};

void apply_inter_box_spacing(MathBox* root, MathContext& ctx) {
    // 1. Adjust types (unary minus rule, etc.)
    adjust_box_types(root);
    
    // 2. Get muskip values
    float thin = 3.0 / 18.0;   // 3mu in em
    float med = 4.0 / 18.0;    // 4mu in em
    float thick = 5.0 / 18.0;  // 5mu in em
    
    // 3. Traverse and insert kerns
    MathBox* prev = nullptr;
    traverse_boxes(root, [&](MathBox* cur) {
        if (prev && prev->type != MathBoxType::Ignore) {
            const int (*table)[8] = ctx.is_tight() ? 
                INTER_BOX_TIGHT_SPACING : INTER_BOX_SPACING;
            int spacing = table[(int)prev->type][(int)cur->type];
            if (spacing > 0) {
                float amount = (spacing == 3) ? thin : 
                               (spacing == 4) ? med : thick;
                insert_kern_before(cur, amount * ctx.scaling_factor());
            }
        }
        prev = cur;
    });
}
```

---

## Phase 4: Integration with Radiant

### 4.1 View Tree Integration

Add `VIEW_BOX` to the existing view hierarchy:

```cpp
// radiant/view.hpp

enum ViewType {
    VIEW_BLOCK,
    VIEW_INLINE,
    VIEW_TEXT,
    VIEW_IMAGE,
    VIEW_BOX,     // NEW: Math box
    // ...
};

struct ViewBox : public ViewBase {
    MathBox* math_box;
    
    // Render math box to the view
    void paint(PaintContext& ctx) override;
};
```

### 4.2 CSS Integration

Support math-specific CSS properties:

```css
/* Example styling */
.math {
    font-family: "Latin Modern Math", "STIX Two Math", serif;
    font-size: 1.2em;
}

.math-display {
    display: block;
    text-align: center;
    margin: 1em 0;
}

.math-inline {
    display: inline;
    vertical-align: baseline;
}
```

### 4.3 Rendering Pipeline

```cpp
// radiant/render_math.cpp

void ViewBox::paint(PaintContext& ctx) {
    if (!math_box) return;
    
    // Set up transform for current position
    ctx.save();
    ctx.translate(x, y + baseline_offset);
    
    // Paint the math box tree
    paint_math_box(math_box, ctx);
    
    ctx.restore();
}

void paint_math_box(MathBox* box, PaintContext& ctx) {
    ctx.save();
    ctx.scale(box->scale, box->scale);
    
    switch (box->content_type) {
    case MathBox::GLYPH:
        // Use FreeType to get glyph outline
        // Use ThorVG to render
        paint_glyph(box->content.glyph.codepoint, 
                    box->content.glyph.font, ctx);
        break;
        
    case MathBox::HBOX:
        float x = 0;
        for (int i = 0; i < box->content.hbox.count; i++) {
            MathBox* child = &box->content.hbox.children[i];
            ctx.translate(x, 0);
            paint_math_box(child, ctx);
            x += child->width;
        }
        break;
        
    case MathBox::VBOX:
        for (int i = 0; i < box->content.vbox.count; i++) {
            MathBox* child = &box->content.vbox.children[i];
            float shift = box->content.vbox.shifts[i];
            ctx.save();
            ctx.translate(0, -shift);
            paint_math_box(child, ctx);
            ctx.restore();
        }
        break;
        
    case MathBox::KERN:
        // Just skip - it's horizontal space
        break;
        
    case MathBox::RULE:
        // Draw a filled rectangle
        ctx.fill_rect(0, -box->height, box->width, box->height + box->depth);
        break;
        
    case MathBox::SVG:
        // Use ThorVG to render SVG path
        paint_svg_path(box->content.svg.path, ctx);
        break;
    }
    
    ctx.restore();
}
```

### 4.4 Font Handling

Reuse Radiant's existing font infrastructure:

```cpp
// radiant/math_font.cpp

// Load math font using existing FreeType infrastructure
FT_Face load_math_font(const char* family) {
    // Use FontConfig to find the font file
    // Use FreeType to load the face
    // Return cached face
    return get_font_face(family, FONT_WEIGHT_NORMAL, FONT_STYLE_NORMAL);
}

// Get glyph metrics
void get_glyph_metrics(FT_Face face, int codepoint, 
                       float* width, float* height, float* depth) {
    FT_Load_Char(face, codepoint, FT_LOAD_NO_SCALE);
    FT_GlyphSlot glyph = face->glyph;
    
    float units_per_em = face->units_per_EM;
    *width = glyph->metrics.horiAdvance / units_per_em;
    *height = glyph->metrics.horiBearingY / units_per_em;
    *depth = (glyph->metrics.height - glyph->metrics.horiBearingY) / units_per_em;
}

// Render glyph using ThorVG
void render_glyph(FT_Face face, int codepoint, PaintContext& ctx) {
    FT_Load_Char(face, codepoint, FT_LOAD_NO_BITMAP);
    FT_Outline& outline = face->glyph->outline;
    
    // Convert FreeType outline to ThorVG path
    tvg::Shape* shape = tvg::Shape::gen().release();
    convert_outline_to_path(outline, shape);
    
    // Fill with current color
    shape->fill(ctx.current_color);
    ctx.canvas->push(unique_ptr<tvg::Shape>(shape));
}
```

---

## Implementation Plan

### Phase 1: Foundation (2-3 weeks)

1. **Create tree-sitter-latex-math grammar**
   - Basic structure: fractions, radicals, sub/superscript
   - Test with representative math expressions

2. **Define MathNode structures**
   - Lambda element shapes for each node type
   - Symbol/command lookup tables

3. **Build CST→MathNode converter**
   - Traverse Tree-sitter parse tree
   - Build Lambda element tree

### Phase 2: Layout Engine (3-4 weeks)

1. **Implement MathContext and MathFontMetrics**
   - Style transitions
   - Font metric loading

2. **Implement core layout functions**
   - `layout_symbol` - single characters
   - `layout_fraction` - fractions
   - `layout_subsup` - subscript/superscript
   - `layout_radical` - square roots

3. **Implement MathBox and VBox stacking**
   - Vertical box construction
   - Horizontal box construction

4. **Implement inter-box spacing**
   - Type adjustment
   - Spacing table lookup

### Phase 3: Rendering Integration (2 weeks)

1. **Integrate with Radiant view tree**
   - Add VIEW_BOX type
   - Implement ViewBox::paint()

2. **Font rendering**
   - FreeType glyph loading
   - ThorVG path rendering

3. **Test with SVG/PNG output**
   - Verify metrics
   - Compare with reference (LaTeX, MathLive)

### Phase 4: Polish (2 weeks)

1. **Add more constructs**
   - Delimiters (\left, \right)
   - Accents (\hat, \vec)
   - Arrays/matrices
   - Operators (\sum, \int)

2. **Error handling**
   - Parse errors
   - Missing glyphs

3. **Performance optimization**
   - Glyph caching
   - Layout caching

### Deliverables

| Milestone | Deliverable | Target |
|-----------|-------------|--------|
| M1 | tree-sitter-latex-math grammar | Week 2 |
| M2 | MathNode tree builder | Week 3 |
| M3 | Basic layout (frac, sqrt, subsup) | Week 6 |
| M4 | Inter-box spacing | Week 7 |
| M5 | Radiant integration | Week 9 |
| M6 | Full implementation | Week 11 |

---

## References

- [MathLive Architecture Analysis](./Mathlive.md) - Detailed analysis of MathLive's design
- [TeXBook](https://www.amazon.com/TeXbook-Donald-Knuth/dp/0201134489) - Appendix G for typesetting algorithms
- [OpenType Math Table](https://docs.microsoft.com/en-us/typography/opentype/spec/math) - Font metrics specification
- [Radiant Layout Design](../doc/Radiant_Layout_Design.md) - Existing Radiant architecture
