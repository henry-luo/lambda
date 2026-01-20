# Unified LaTeX/Math Pipeline Design

**Date:** January 12, 2026
**Status:** Proposal (Revised)
**Decision:** Consolidate on TeX Pipeline, Remove MathLive Pipeline

---

## 1. Executive Summary

This document proposes consolidating Lambda Script's two math rendering pipelines into a single unified TeX-based pipeline. The MathLive-derived pipeline will be deprecated and removed. The unified pipeline will:

1. Use **TexNode** as the canonical typeset representation AND view tree (no conversion)
2. Support **tree-sitter grammars** for parsing LaTeX, LaTeX-math, and ASCII-math
3. Support both **TFM** (authentic TeX metrics) and **FreeType** (system fonts)
4. Use **CSS pixels** as the layout unit (not TeX points)
5. Introduce **RDT_VIEW_TEXNODE** view type for direct rendering in Radiant
6. Render TexNode trees directly using **FreeType + ThorVG** to screen buffer
7. Support **caret, selection, and events** on TexNode trees
8. Output to **DVI/SVG/PDF/PNG** formats (with unit conversion for DVI)

---

## 2. Current State Analysis

### 2.1 TeX Pipeline (KEEP)

**Location:** `lambda/tex/`

**Architecture:**
```
┌─────────────────────────────────────────────────────────────────────────┐
│                         TeX Pipeline (Current)                           │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  LaTeX Source                                                            │
│       │                                                                  │
│       ▼                                                                  │
│  tree-sitter-latex ──────► Lambda Element tree                          │
│       │                          │                                       │
│       │                          ▼                                       │
│       │                   tex_lambda_bridge.cpp                          │
│       │                          │                                       │
│       │                          ▼                                       │
│       │    ┌─────────────────────────────────────────┐                  │
│       │    │     TexNode Tree (TeX points)           │                  │
│       │    │  (HList, VList, Char, Glue, Math...)   │                  │
│       │    └─────────────────┬───────────────────────┘                  │
│       │                      │                                           │
│       │         ┌────────────┼────────────┐                             │
│       │         ▼            ▼            ▼                             │
│       │    tex_dvi_out  tex_pdf_out  tex_to_view                        │
│       │         │            │            │                             │
│       │         ▼            ▼            ▼                             │
│       │       .dvi         .pdf       ViewTree (separate tree)          │
│       │                                                                  │
└─────────────────────────────────────────────────────────────────────────┘
```

**Issues with Current Design:**
- `tex_to_view.cpp` creates a **separate ViewTree**, duplicating data
- **Two coordinate systems**: TeX points for TexNode, CSS pixels for ViewTree
- Incomplete conversion: many node types not handled in tex_to_view.cpp
- No event system integration for TexNode

**Key Files:**

| File | Purpose | Lines |
|------|---------|-------|
| `tex_node.hpp` | Unified TexNode structure with 20+ node types | 583 |
| `tex_node.cpp` | Node operations and tree management | ~200 |
| `tex_tfm.cpp/hpp` | TFM font metric loading (cmr10, cmmi10, etc.) | ~800 |
| `tex_math_bridge.cpp` | LaTeX math string → TexNode conversion | 2286 |
| `tex_lambda_bridge.cpp` | Lambda Element → TexNode conversion | ~1500 |
| `tex_hlist.cpp` | Horizontal list building and processing | ~600 |
| `tex_vlist.cpp` | Vertical list building | ~400 |
| `tex_linebreak.cpp` | Knuth-Plass line breaking algorithm | ~800 |
| `tex_pagebreak.cpp` | Page breaking algorithm | ~500 |
| `tex_dvi_out.cpp` | DVI bytecode output | 886 |
| `tex_pdf_out.cpp` | PDF output via DVI | ~400 |
| `tex_to_view.cpp` | TexNode → ViewBlock/ViewSpan conversion | 679 |

**TexNode Structure (from `tex_node.hpp`):**

```cpp
enum class NodeClass : uint8_t {
    // Character nodes
    Char,           // Single character glyph
    Ligature,       // fi, fl, ff, ffi, ffl

    // List nodes (containers)
    HList,          // Horizontal list (paragraph content)
    VList,          // Vertical list (page content)

    // Box nodes
    HBox,           // \hbox{...}
    VBox,           // \vbox{...}
    VTop,           // \vtop{...}

    // Spacing nodes
    Glue,           // Stretchable space
    Kern,           // Fixed space
    Penalty,        // Break penalty

    // Rule nodes
    Rule,           // Filled rectangle (fraction bars, etc.)

    // Math nodes
    MathList,       // Math content container
    MathChar,       // Math character with atom type
    MathOp,         // Large operator (\sum, \int)
    Fraction,       // \frac, \over
    Radical,        // \sqrt
    Delimiter,      // \left, \right
    Accent,         // \hat, \bar
    Scripts,        // Subscript/superscript

    // Structure nodes
    Paragraph,      // Complete paragraph
    Page,           // Complete page

    // Special nodes
    Disc,           // Discretionary break
    Error,          // Error recovery
};
```

**Strengths:**
- Complete TeX-compatible typesetting
- Authentic font metrics via TFM files
- Proven DVI output (passes all tests)
- Handles full documents (text + math)
- Line breaking (Knuth-Plass algorithm)
- Page breaking

**Weaknesses:**
- TFM fonts only (no system font support)
- tex_to_view.cpp conversion incomplete
- Limited to cmr/cmmi/cmsy/cmex font families

---

### 2.2 MathLive Pipeline (REMOVE)

**Location:** `radiant/` + `lambda/math_node.hpp` + `lambda/input/input-math2.cpp`

**Architecture:**
```
┌─────────────────────────────────────────────────────────────────────────┐
│                       MathLive Pipeline                                  │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  LaTeX Math String                                                       │
│       │                                                                  │
│       ▼                                                                  │
│  tree-sitter-latex-math ──────► MathNode (Lambda Items)                 │
│       │                              │                                   │
│       │                              ▼                                   │
│       │                       layout_math.cpp                            │
│       │                              │                                   │
│       │                              ▼                                   │
│       │              ┌───────────────────────────────┐                  │
│       │              │         MathBox Tree          │                  │
│       │              │   (Glyph, HBox, VBox, Kern)   │                  │
│       │              └───────────────┬───────────────┘                  │
│       │                              │                                   │
│       │                              ▼                                   │
│       │                       render_math.cpp                            │
│       │                              │                                   │
│       │                              ▼                                   │
│       │                        Screen Pixels                             │
│       │                                                                  │
└─────────────────────────────────────────────────────────────────────────┘
```

**Key Files:**

| File | Purpose | Lines |
|------|---------|-------|
| `lambda/math_node.hpp` | MathNode types as Lambda Items | 297 |
| `lambda/input/input-math2.cpp` | tree-sitter-latex-math → MathNode | 749 |
| `radiant/math_box.hpp` | MathBox layout structure | 342 |
| `radiant/math_context.hpp` | Math typesetting context | 342 |
| `radiant/layout_math.cpp` | MathNode → MathBox layout | 900 |
| `radiant/render_math.cpp` | MathBox → pixels rendering | 297 |
| `radiant/math_integration.cpp` | Integration with Radiant | 302 |

**MathNode Types (from `math_node.hpp`):**

```cpp
enum class MathNodeType {
    Symbol,       // Single character
    Number,       // Numeric literal
    Command,      // \alpha, \times, etc.
    Group,        // {expr}
    Row,          // Horizontal sequence
    Subsup,       // x_1^2
    Fraction,     // \frac{a}{b}
    Binomial,     // \binom{n}{k}
    Radical,      // \sqrt{x}
    Delimiter,    // \left( ... \right)
    Accent,       // \hat{x}
    BigOperator,  // \sum_{i=1}^{n}
    Array,        // Matrix environments
    Text,         // \text{hello}
    Style,        // \mathbf{x}
    Space,        // \quad
    Error,        // Parse error
};
```

**MathBox Types (from `math_box.hpp`):**

```cpp
enum class MathBoxContentType {
    Empty,       // Empty box (spacing)
    Glyph,       // Single glyph
    HBox,        // Horizontal box
    VBox,        // Vertical box
    Kern,        // Horizontal spacing
    Rule,        // Filled rectangle
    VRule,       // Vertical rule
    Radical,     // Square root symbol
    Delimiter,   // Extensible delimiter
};
```

**Weaknesses (reasons for removal):**
- Duplicates TeX pipeline functionality
- Math-only (no document typesetting)
- No DVI/PDF output capability
- Two separate node systems to maintain
- Lambda Items overhead for typesetting
- Font metrics via FreeType only (less accurate)

---

### 2.3 Comparison Summary

| Aspect | TeX Pipeline | MathLive Pipeline |
|--------|--------------|-------------------|
| **Node Type** | TexNode (C struct) | MathNode (Lambda Items) + MathBox |
| **Scope** | Full documents | Math only |
| **Font Metrics** | TFM files (exact TeX) | FreeType (system fonts) |
| **Output Formats** | DVI, PDF, ViewTree | Screen pixels only |
| **Line Breaking** | Knuth-Plass ✓ | N/A |
| **Page Breaking** | ✓ | N/A |
| **Ligatures** | ✓ (fi, fl, ff...) | N/A |
| **Parser** | tree-sitter-latex + custom math | tree-sitter-latex-math |
| **Test Coverage** | 13 DVI tests pass | Limited |
| **Maturity** | Production-ready | Experimental |

---

## 3. Unified Pipeline Design (Revised)

### 3.1 New Architecture Overview

**Key Design Decisions:**

1. **TexNode IS the View Tree** - No separate ViewTree conversion. TexNode serves as both:
   - The typeset representation (semantic structure)
   - The view tree for rendering (layout coordinates)

2. **CSS Pixels as Layout Unit** - TexNode dimensions use CSS pixels directly:
   - Simplifies rendering (no coordinate conversion)
   - Direct integration with Radiant's CSS layout engine
   - DVI output converts CSS px → scaled points when needed

3. **New View Type: `RDT_VIEW_TEXNODE`** - Direct TexNode rendering in Radiant:
   - TexNode trees rendered via FreeType + ThorVG
   - No intermediate ViewTree creation
   - Preserves full TexNode structure for editing

4. **Event System Integration** - TexNode trees support:
   - Hit testing for mouse interaction
   - Caret positioning within math expressions
   - Selection ranges across TexNode subtrees

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    Unified TeX Pipeline (Revised)                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│                         ┌─────────────────────┐                             │
│                         │   Input Sources     │                             │
│                         └──────────┬──────────┘                             │
│                                    │                                         │
│         ┌──────────────────────────┼──────────────────────────┐             │
│         │                          │                          │             │
│         ▼                          ▼                          ▼             │
│  ┌──────────────┐         ┌──────────────┐         ┌──────────────┐        │
│  │ tree-sitter  │         │ tree-sitter  │         │   ASCII      │        │
│  │   -latex     │         │ -latex-math  │         │   Math       │        │
│  └──────┬───────┘         └──────┬───────┘         └──────┬───────┘        │
│         │                        │                        │                 │
│         └────────────────────────┼────────────────────────┘                 │
│                                  │                                          │
│                                  ▼                                          │
│                    ┌──────────────────────────┐                             │
│                    │  tex_unified_bridge.cpp  │                             │
│                    │  (Element/Math → TexNode)│                             │
│                    └────────────┬─────────────┘                             │
│                                 │                                           │
│                                 ▼                                           │
│                    ┌──────────────────────────┐                             │
│                    │      TexNode Tree        │                             │
│                    │  ══════════════════════  │                             │
│                    │  • Dimensions in CSS px  │                             │
│                    │  • IS the View Tree      │  ◄── No separate ViewTree! │
│                    │  • Supports hit testing  │                             │
│                    │  • Supports caret/select │                             │
│                    └────────────┬─────────────┘                             │
│                                 │                                           │
│                    ┌────────────┴─────────────┐                             │
│                    │     Font Metrics         │                             │
│                    │   ┌─────────┬─────────┐  │                             │
│                    │   │   TFM   │ FreeType│  │                             │
│                    │   │ (cmr10) │ (system)│  │                             │
│                    │   └─────────┴─────────┘  │                             │
│                    └────────────┬─────────────┘                             │
│                                 │                                           │
│         ┌───────────────────────┼───────────────────────────┐              │
│         │                       │                           │              │
│         ▼                       ▼                           ▼              │
│  ┌─────────────┐     ┌─────────────────────┐     ┌─────────────────┐      │
│  │   DVI Out   │     │   RDT_VIEW_TEXNODE  │     │   SVG/PDF/PNG   │      │
│  │ (converts   │     │   (direct render)   │     │ (vector/raster) │      │
│  │  px → sp)   │     │   FreeType+ThorVG   │     │                 │      │
│  └─────────────┘     └─────────────────────┘     └─────────────────┘      │
│                                │                                           │
│                                ▼                                           │
│                      ┌─────────────────────┐                               │
│                      │    Event System     │                               │
│                      │  ┌───────┬────────┐ │                               │
│                      │  │ Caret │Selection│ │                               │
│                      │  │ Pos   │ Range  │ │                               │
│                      │  └───────┴────────┘ │                               │
│                      └─────────────────────┘                               │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 CSS Pixel Coordinate System

**Unit Conversion:**

All TexNode dimensions are stored in **CSS pixels** (96 dpi reference):

| Unit | Value | Conversion |
|------|-------|------------|
| 1 CSS px | 1.0 | Base unit |
| 1 TeX pt | 1.333... px | 96/72 |
| 1 TeX sp | ~0.00002 px | 1/(65536 × 72.27) × 96 |

**TexNode Dimension Fields (in CSS px):**

```cpp
struct TexNode {
    // ... existing fields ...

    // Layout dimensions in CSS pixels
    float width;      // horizontal extent
    float height;     // above baseline
    float depth;      // below baseline
    float shift;      // vertical shift (for subscripts, etc.)

    // Position relative to parent (for rendering)
    float x;          // horizontal offset from parent
    float y;          // vertical offset (baseline-relative)
};
```

**DVI Output Conversion:**

When outputting DVI, convert CSS px → scaled points:

```cpp
// 1 CSS px = (72.27/96) × 65536 scaled points
constexpr float PX_TO_SP = (72.27f / 96.0f) * 65536.0f;

int32_t px_to_sp(float px) {
    return static_cast<int32_t>(px * PX_TO_SP);
}

// In tex_dvi_out.cpp
void emit_set_char(DviWriter& dvi, float x_px) {
    int32_t x_sp = px_to_sp(x_px);
    dvi.move_right(x_sp);
}
```

### 3.3 RDT_VIEW_TEXNODE View Type

**ViewType Enum Extension:**

```cpp
// In dom_node.hpp
enum ViewType : uint8_t {
    RDT_VIEW_NONE = 0,
    RDT_VIEW_TEXT,
    RDT_VIEW_MATH,          // Legacy (to be removed)
    RDT_VIEW_BLOCK,
    RDT_VIEW_INLINE,
    RDT_VIEW_REPLACED,
    RDT_VIEW_FLEX,
    RDT_VIEW_GRID,
    RDT_VIEW_GRID_ITEM,
    RDT_VIEW_TABLE,
    RDT_VIEW_TABLE_ROW,
    RDT_VIEW_TABLE_CELL,
    RDT_VIEW_TEXNODE,       // ◄── NEW: Direct TexNode rendering
};
```

**DomElement Extension for TexNode:**

```cpp
// In dom_node.hpp or dom_element.hpp
struct DomElement : DomNode {
    // ... existing fields ...

    // For RDT_VIEW_TEXNODE elements
    TexNode* tex_root;      // Root of TexNode tree (when view_type == RDT_VIEW_TEXNODE)
};
```

**Rendering Integration:**

```cpp
// In render.cpp or render_texnode.cpp (NEW)
void render_texnode(RenderContext& ctx, DomElement* elem) {
    if (elem->view_type != RDT_VIEW_TEXNODE || !elem->tex_root) return;

    // Get element's position in page coordinates
    float base_x = elem->x + elem->border_left + elem->padding_left;
    float base_y = elem->y + elem->border_top + elem->padding_top;

    // Render TexNode tree
    render_texnode_tree(ctx, elem->tex_root, base_x, base_y);
}

void render_texnode_tree(RenderContext& ctx, TexNode* node, float x, float y) {
    if (!node) return;

    // Position includes node's relative offset
    float node_x = x + node->x;
    float node_y = y + node->y;

    switch (node->node_class) {
        case NodeClass::Char:
            render_texnode_char(ctx, node, node_x, node_y);
            break;
        case NodeClass::HList:
        case NodeClass::VList:
            render_texnode_list(ctx, node, node_x, node_y);
            break;
        case NodeClass::Rule:
            render_texnode_rule(ctx, node, node_x, node_y);
            break;
        // ... handle other node types ...
    }
}
```

### 3.4 Event System for TexNode

**Hit Testing:**

```cpp
// In tex_event.hpp (NEW)

// Hit test result
struct TexHitResult {
    TexNode* node;          // Deepest node containing point
    int char_index;         // Character index within node (for text)
    float local_x;          // Hit position relative to node
    float local_y;
    bool is_before;         // Caret should be before (true) or after (false) char
};

// Hit test a point in TexNode tree
TexHitResult tex_hit_test(TexNode* root, float x, float y);
```

**Caret Positioning:**

```cpp
// Caret position within TexNode tree
struct TexCaret {
    TexNode* node;          // Node containing caret
    int position;           // Position within node (0 = before first char)
    float x;                // Visual x position (for rendering cursor)
    float y;                // Visual y position (baseline)
    float height;           // Cursor height
};

// Get caret position from hit test
TexCaret tex_caret_from_hit(const TexHitResult& hit);

// Move caret (returns new caret position)
TexCaret tex_caret_move_left(TexNode* root, const TexCaret& current);
TexCaret tex_caret_move_right(TexNode* root, const TexCaret& current);
TexCaret tex_caret_move_up(TexNode* root, const TexCaret& current);
TexCaret tex_caret_move_down(TexNode* root, const TexCaret& current);
```

**Selection Range:**

```cpp
// Selection range within TexNode tree
struct TexSelection {
    TexCaret start;
    TexCaret end;
    bool is_collapsed() const { return start.node == end.node && start.position == end.position; }
};

// Selection operations
TexSelection tex_select_word(TexNode* root, const TexCaret& at);
TexSelection tex_select_all(TexNode* root);
void tex_render_selection(RenderContext& ctx, TexNode* root, const TexSelection& sel);
```

**Event Handler Integration:**

```cpp
// In DomElement or separate handler
class TexNodeEventHandler {
    TexNode* root;
    TexCaret caret;
    TexSelection selection;

public:
    bool on_mouse_down(float x, float y, int button);
    bool on_mouse_move(float x, float y);
    bool on_mouse_up(float x, float y, int button);
    bool on_key_down(int key, int mods);

    // For editing (future)
    void insert_char(int codepoint);
    void delete_char();
    void delete_selection();
};
```

### 3.5 Input Layer: Tree-Sitter Grammars

**Supported Grammars:**

| Grammar | Location | Purpose |
|---------|----------|---------|
| `tree-sitter-latex` | `lambda/tree-sitter-latex/` | Full LaTeX documents |
| `tree-sitter-latex-math` | `lambda/tree-sitter-latex-math/` | Inline math expressions |
| ASCII Math (custom) | `lambda/input/input-math-ascii.cpp` | Simple notation |

**Parser Dispatch (`tex_input.cpp` - NEW):**

```cpp
// Unified input entry point
TexNode* tex_parse_and_typeset(
    const char* source,
    size_t len,
    InputType type,           // LATEX_DOC, LATEX_MATH, ASCII_MATH
    TexContext& ctx
);

enum class InputType {
    LATEX_DOC,      // Full LaTeX document (tree-sitter-latex)
    LATEX_MATH,     // Inline math only (tree-sitter-latex-math)
    ASCII_MATH,     // AsciiMath notation
    // Future: TYPST_MATH, MATHML
};
```

**Math Subset Generalization:**

To support multiple math notations, abstract the "math semantic" layer:

```cpp
// Abstract math operations that map to TexNode
enum class MathOp {
    // Binary
    ADD, SUB, MUL, DIV, POWER,
    // Comparison
    EQ, NEQ, LT, GT, LEQ, GEQ,
    // Structure
    FRAC, SQRT, ROOT,
    SUB, SUP, SUBSUP,
    // Delimiters
    PAREN, BRACKET, BRACE, ABS, NORM,
    // Big operators
    SUM, PROD, INT, OINT,
    UNION, INTERSECT,
    // Accents
    HAT, BAR, DOT, VEC, TILDE,
};

// Parser-agnostic math builder
class MathBuilder {
    TexContext& ctx;
public:
    TexNode* binary(MathOp op, TexNode* left, TexNode* right);
    TexNode* unary(MathOp op, TexNode* operand);
    TexNode* fraction(TexNode* num, TexNode* denom);
    TexNode* radical(TexNode* radicand, TexNode* index = nullptr);
    TexNode* scripts(TexNode* base, TexNode* sub, TexNode* sup);
    TexNode* delimiter(MathOp delim, TexNode* content);
    TexNode* bigop(MathOp op, TexNode* lower, TexNode* upper);
    TexNode* symbol(int codepoint, AtomType type);
    TexNode* text(const char* str);
};
```

### 3.6 TexNode Extensions for Generality

Add support for non-TeX math notations while preserving TeX semantics:

```cpp
// Extended NodeClass for math variants
enum class NodeClass : uint8_t {
    // ... existing classes ...

    // Extended math (for non-LaTeX sources)
    MathMatrix,     // Matrix/array with alignment
    MathCases,      // Cases environment
    MathAlign,      // Aligned equations
};

// Matrix support
struct MatrixContent {
    int rows;
    int cols;
    TexNode** cells;      // row-major order
    char* col_align;      // "lcr" alignment string
    bool has_delims;
    int32_t left_delim;
    int32_t right_delim;
};
```

### 3.7 Dual Font System: TFM + FreeType

**FontManager Interface:**

```cpp
// Unified font metric provider
class FontMetrics {
public:
    virtual ~FontMetrics() = default;

    // Glyph metrics
    virtual float get_width(int codepoint) = 0;
    virtual float get_height(int codepoint) = 0;
    virtual float get_depth(int codepoint) = 0;
    virtual float get_italic(int codepoint) = 0;

    // Font parameters
    virtual float get_x_height() = 0;
    virtual float get_quad() = 0;
    virtual float get_axis_height() = 0;

    // Kerning
    virtual float get_kern(int left, int right) = 0;

    // Ligatures
    virtual int get_ligature(int left, int right) = 0;
};

// TFM implementation (exact TeX)
class TFMFontMetrics : public FontMetrics {
    TFMFont* tfm;
    float size_pt;
public:
    TFMFontMetrics(TFMFont* font, float size);
    // ... implementations ...
};

// FreeType implementation (system fonts)
class FreeTypeFontMetrics : public FontMetrics {
    FT_Face face;
    float size_pt;
    float scale;
public:
    FreeTypeFontMetrics(FT_Face face, float size);
    // ... implementations ...
};
```

**Font Selection Strategy:**

```cpp
struct FontConfig {
    enum Mode {
        TFM_ONLY,       // Authentic TeX (DVI output)
        FREETYPE_ONLY,  // System fonts (screen display)
        TFM_PREFERRED,  // TFM if available, else FreeType
    };

    Mode mode;
    const char* tfm_path;      // Path to TFM font files
    const char* system_family; // Fallback system font family
};

FontMetrics* get_font_metrics(
    const char* font_name,     // e.g., "cmr10", "serif"
    float size_pt,
    const FontConfig& config
);
```

### 3.8 Output Backends

#### 3.8.1 DVI Output (Existing - Enhanced)

**File:** `lambda/tex/tex_dvi_out.cpp`

Already complete. Key enhancement for new architecture:
- Convert CSS pixels to scaled points for DVI output

```cpp
// Existing API (dimensions now in CSS px, converted internally)
bool dvi_open(DVIWriter& writer, const char* filename, const DVIParams& params);
bool dvi_write_page(DVIWriter& writer, TexNode* page_vlist, int page_num, TFMFontManager* fonts);
bool dvi_close(DVIWriter& writer);

// Internal conversion (CSS px → scaled points)
constexpr float PX_TO_SP = (72.27f / 96.0f) * 65536.0f;
```

#### 3.8.2 Direct Rendering via RDT_VIEW_TEXNODE

**File:** `radiant/render_texnode.cpp` (NEW)

**Replaces:** `lambda/tex/tex_to_view.cpp` (deprecated - TexNode IS the view tree)

The new approach renders TexNode trees directly without conversion to ViewTree:

```cpp
// Direct rendering of TexNode tree
void render_texnode_element(RenderContext& ctx, DomElement* elem) {
    if (elem->view_type != RDT_VIEW_TEXNODE || !elem->tex_root) return;

    // Element position in page coordinates
    float base_x = elem->content_box_x();
    float base_y = elem->content_box_y();

    // Render with FreeType + ThorVG
    render_texnode_recursive(ctx, elem->tex_root, base_x, base_y);
}
```

**Font Mapping for Screen:**

```cpp
const char* tex_font_to_system_font(const char* tex_font) {
    // Computer Modern → CMU (Computer Modern Unicode)
    if (strncmp(tex_font, "cmr", 3) == 0) return "CMU Serif";
    if (strncmp(tex_font, "cmmi", 4) == 0) return "CMU Serif";  // italic variant
    if (strncmp(tex_font, "cmsy", 4) == 0) return "CMU Serif";  // symbols
    if (strncmp(tex_font, "cmex", 4) == 0) return "CMU Serif";  // extensions
    if (strncmp(tex_font, "cmss", 4) == 0) return "CMU Sans Serif";
    if (strncmp(tex_font, "cmtt", 4) == 0) return "CMU Typewriter Text";

    return "serif";  // Ultimate fallback
}
```

#### 3.8.3 SVG Output (NEW)

**File:** `lambda/tex/tex_svg_out.cpp` (to create)

```cpp
struct SVGWriter {
    Arena* arena;
    StrBuf* output;
    float scale;              // TeX points to SVG units

    // Current state
    const char* current_font;
    float font_size;
    Color fill_color;
};

bool svg_write_document(
    SVGWriter& writer,
    TexNode* document,
    DocumentContext& ctx
);

// Output structure:
// <svg viewBox="0 0 width height">
//   <defs>
//     <style>/* font definitions */</style>
//   </defs>
//   <g class="page">
//     <text x="..." y="..." class="cmr10">characters</text>
//     <rect x="..." y="..." width="..." height="..."/>  <!-- rules -->
//   </g>
// </svg>
```

#### 3.8.4 PDF Output (Enhanced)

**File:** `lambda/tex/tex_pdf_out.cpp`

Current: Uses DVI as intermediate.

**Direct PDF generation option:**

```cpp
struct PDFWriter {
    Arena* arena;
    FILE* file;

    // PDF object management
    int next_object_id;
    ArrayList* objects;

    // Font embedding
    PDFFontEntry* fonts;
    int font_count;
};

bool pdf_write_document(
    PDFWriter& writer,
    TexNode* document,
    DocumentContext& ctx
);
```

#### 3.8.5 PNG Output (NEW)

**File:** `lambda/tex/tex_png_out.cpp` (to create)

Render via ViewTree + existing Radiant rendering:

```cpp
bool png_render_document(
    const char* output_path,
    TexNode* document,
    DocumentContext& ctx,
    int dpi = 300
);

// Implementation:
// 1. Convert TexNode → ViewTree
// 2. Create offscreen surface at target DPI
// 3. Render ViewTree to surface
// 4. Encode surface as PNG
```

---

## 4. Implementation Plan

### Phase 1: Deprecate MathLive Pipeline (Week 1)

**Goal:** Remove duplicate code, consolidate on TeX pipeline.

**Tasks:**

1. **Mark for removal:**
   - `radiant/layout_math.cpp` → deprecated
   - `radiant/layout_math.hpp` → deprecated
   - `radiant/math_box.hpp` → deprecated
   - `radiant/math_context.hpp` → deprecated
   - `radiant/render_math.cpp` → deprecated
   - `radiant/math_integration.cpp` → deprecated
   - `lambda/math_node.hpp` → deprecated
   - `lambda/input/input-math2.cpp` → keep parser, output to TexNode

2. **Update build system:**
   - Remove deprecated files from `build_lambda_config.json`
   - Update Premake configuration

3. **Fix any dependencies:**
   - Search for includes of deprecated headers
   - Redirect to TeX pipeline equivalents

### Phase 2: CSS Pixel Unit Conversion (Week 2)

**Goal:** Convert TexNode dimensions from TeX points to CSS pixels.

**Tasks:**

1. **Update TexNode structure:**
   - Change dimension fields to `float` (CSS px)
   - Add `x`, `y` position fields for rendering

2. **Update typesetting code:**
   - `tex_math_bridge.cpp`: Output dimensions in CSS px
   - Font metrics: Return values in CSS px
   - All layout calculations in CSS px

3. **DVI output conversion:**
   - Create `px_to_sp()` conversion function
   - Apply conversion in `tex_dvi_out.cpp`

### Phase 3: Add RDT_VIEW_TEXNODE View Type (Week 3)

**Goal:** Integrate TexNode rendering into Radiant.

**Tasks:**

1. **Add new view type:**
   - Update `ViewType` enum in `dom_node.hpp`
   - Add `tex_root` field to `DomElement`

2. **Create render_texnode.cpp:**
   - `render_texnode_element()` - entry point
   - `render_texnode_recursive()` - tree traversal
   - `render_texnode_char()` - glyph rendering via FreeType
   - `render_texnode_rule()` - horizontal/vertical rules

3. **Font mapping:**
   - Map CM fonts to system fonts (CMU family)
   - FreeType glyph rendering

4. **Deprecate tex_to_view.cpp:**
   - No longer needed (TexNode IS the view tree)
   - Remove from build

### Phase 4: Event System for TexNode (Week 4)

**Goal:** Support caret, selection, and interaction on TexNode trees.

**Tasks:**

1. **Create tex_event.hpp/cpp:**
   - `TexHitResult` struct
   - `TexCaret` struct
   - `TexSelection` struct
   - `tex_hit_test()` function

2. **Caret navigation:**
   - `tex_caret_move_left/right/up/down()`
   - Proper handling of sub/superscripts
   - Matrix cell navigation

3. **Selection rendering:**
   - `tex_render_selection()` - highlight selected region
   - Copy-to-clipboard support

4. **Integration with Radiant events:**
   - Mouse click → hit test → caret position
   - Keyboard navigation
   - Selection drag

### Phase 5: Dual Font System (Week 5)

**Goal:** Support both TFM and FreeType fonts.

**Tasks:**

1. **Create FontMetrics interface:**
   - Abstract font metric queries
   - Implement TFMFontMetrics
   - Implement FreeTypeFontMetrics

2. **Update tex_math_bridge:**
   - Use FontMetrics interface
   - Fallback logic for missing glyphs

3. **Glyph mapping:**
   - Create CM → Unicode mapping table
   - Handle math symbols consistently

### Phase 6: Additional Output Formats (Week 6)

**Goal:** SVG and PNG output.

**Tasks:**

1. **SVG output:**
   - Create tex_svg_out.cpp
   - Text and rule elements
   - Font embedding or system font references

2. **PNG output:**
   - Direct rasterization from TexNode tree
   - Configurable DPI
   - Transparent or white background

3. **Testing:**
   - Visual comparison tests
   - DVI ↔ SVG ↔ PNG consistency

---

## 5. Files to Modify

### 5.1 Files to Remove (MathLive Pipeline)

```
radiant/layout_math.cpp
radiant/layout_math.hpp
radiant/math_box.hpp
radiant/math_context.hpp
radiant/render_math.cpp
radiant/render_math.hpp
radiant/math_integration.cpp
radiant/math_integration.hpp
lambda/math_node.hpp
lambda/tex/tex_to_view.cpp          # Deprecated - TexNode IS the view tree
lambda/tex/tex_to_view.hpp
```

### 5.2 Files to Create

```
radiant/render_texnode.cpp          # Direct TexNode rendering (FreeType + ThorVG)
radiant/render_texnode.hpp
lambda/tex/tex_event.hpp            # Hit testing, caret, selection for TexNode
lambda/tex/tex_event.cpp
lambda/tex/tex_unified_input.cpp    # Unified input dispatcher
lambda/tex/tex_ascii_math.cpp       # ASCII Math → TexNode
lambda/tex/tex_font_metrics.hpp     # Font abstraction interface
lambda/tex/tex_font_freetype.cpp    # FreeType font metrics
lambda/tex/tex_svg_out.cpp          # SVG output
lambda/tex/tex_svg_out.hpp
lambda/tex/tex_png_out.cpp          # PNG output (direct from TexNode)
```

### 5.3 Files to Modify

```
lambda/input/css/dom_node.hpp       # Add RDT_VIEW_TEXNODE to ViewType enum
                                     # Add tex_root field to DomElement
lambda/tex/tex_node.hpp             # Change dimensions to CSS pixels (float)
                                     # Add x, y position fields
lambda/tex/tex_math_bridge.cpp      # Output dimensions in CSS px
lambda/tex/tex_dvi_out.cpp          # Convert CSS px → scaled points for DVI
lambda/tex/tex_tfm.cpp              # Return metrics in CSS px
lambda/input/input-math2.cpp        # Output TexNode instead of MathNode
radiant/render.cpp                  # Call render_texnode for RDT_VIEW_TEXNODE
```

---

## 6. Testing Strategy

### 6.1 Existing Tests (Maintain)

```bash
make test-dvi-compare    # 13 tests comparing DVI output
```

### 6.2 New Tests

| Test Suite | Purpose |
|------------|---------|
| `test_tex_css_units` | CSS pixel coordinate conversion |
| `test_tex_render` | Direct TexNode rendering |
| `test_tex_event` | Hit testing, caret positioning |
| `test_tex_ascii_math` | ASCII Math parsing |
| `test_tex_freetype_metrics` | FreeType font metrics |
| `test_tex_svg_output` | SVG generation |

### 6.3 Visual Regression Tests

```bash
# Generate reference images
./lambda.exe render test.tex -o test_ref.png --dpi 300

# Compare after changes
./lambda.exe render test.tex -o test_new.png --dpi 300
compare test_ref.png test_new.png diff.png
```

---

## 7. Migration Guide

### 7.1 For Code Using MathLive Pipeline

**Before:**
```cpp
#include "radiant/layout_math.hpp"
#include "radiant/math_box.hpp"

MathBox* box = layout_math(math_node, ctx, arena);
render_math_box(rdcon, box, x, y);
```

**After:**
```cpp
#include "lambda/tex/tex_math_bridge.hpp"
#include "radiant/render_texnode.hpp"

TexNode* node = typeset_latex_math(latex_str, len, math_ctx);
// TexNode IS the view tree - render directly
render_texnode_tree(ctx, node, x, y);
```

### 7.2 For Code Using tex_to_view (Deprecated)

**Before:**
```cpp
#include "lambda/tex/tex_to_view.hpp"

TexNode* tex_tree = typeset_latex_math(...);
ViewBlock* view = tex_to_view_tree(tex_tree, ctx);
// Render view using Radiant ViewTree rendering
```

**After:**
```cpp
#include "radiant/render_texnode.hpp"

TexNode* tex_tree = typeset_latex_math(...);
// No conversion needed - TexNode IS the view tree
render_texnode_tree(ctx, tex_tree, x, y);
```

### 7.3 For Math Input

**Before:**
```cpp
Item math_tree = lambda::parse_math(latex_str, input);
```

**After:**
```cpp
TexNode* math_tree = tex_parse_math(latex_str, len, TEX_MATH_LATEX, ctx);
```

### 7.4 For Coordinate System Changes

**Before (TeX points):**
```cpp
// Dimensions in TeX points
float width_pt = node->width;  // e.g., 72.0 (1 inch)
```

**After (CSS pixels):**
```cpp
// Dimensions in CSS pixels
float width_px = node->width;  // e.g., 96.0 (1 inch at 96 dpi)

// For DVI output, convert:
int32_t width_sp = px_to_sp(width_px);  // Scaled points
```

---

## 8. Success Criteria

1. **All existing DVI tests pass** (13/13) - with CSS px → sp conversion
2. **RDT_VIEW_TEXNODE renders** correctly in Radiant
3. **Event system works** - caret positioning, selection
4. **ASCII Math input works** with TeX output
5. **FreeType fonts render** identically to TFM for common glyphs
6. **SVG output** validates and matches DVI visually
7. **PNG output** at 300 DPI matches reference images
8. **MathLive code removed** from build
9. **tex_to_view.cpp deprecated** and removed

---

## 9. Timeline

| Week | Milestone |
|------|-----------|
| 1 | MathLive pipeline deprecated, build clean |
| 2 | CSS pixel unit conversion in TexNode |
| 3 | RDT_VIEW_TEXNODE view type, direct rendering |
| 4 | Event system (hit test, caret, selection) |
| 5 | Dual font system (TFM + FreeType) |
| 6 | SVG/PNG output, final testing |

---

## 10. Appendix: TexNode Quick Reference

```cpp
// Character
TexNode* ch = make_char(arena, 'x', italic_font);

// Math character with atom type
TexNode* mc = make_math_char(arena, 0x03B1, AtomType::Ord, italic_font);

// Fraction
TexNode* frac = make_fraction(arena, num, denom, rule_thickness);

// Scripts
TexNode* scripts = make_scripts(arena, base, subscript, superscript);

// Horizontal list
TexNode* hlist = make_hlist(arena);
hlist->append_child(ch1);
hlist->append_child(make_kern(arena, 2.0f));
hlist->append_child(ch2);

// Glue (stretchable space)
TexNode* glue = make_glue(arena, Glue{6.0f, 3.0f, 2.0f}, "interword");

// Rule (horizontal line)
TexNode* rule = make_rule(arena, width, height, depth);
```
