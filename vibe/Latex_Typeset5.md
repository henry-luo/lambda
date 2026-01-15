# LaTeX Pipeline Unification - Implementation Plan

This document provides a detailed implementation plan for unifying the LaTeX→HTML and LaTeX→DVI pipelines as outlined in [Latex_Typeset_Design2.md](Latex_Typeset_Design2.md).

**Goal**: Phase out `format_latex_html_v2.cpp` (~10K lines) by evolving `tex_latex_bridge.cpp` to support HTML output, achieving test parity with all existing HTML tests.

---

## Table of Contents

1. [Current Codebase Inventory](#current-codebase-inventory)
2. [Phase A: Document Model Layer](#phase-a-document-model-layer)
3. [Phase B: Math SVG Embedding](#phase-b-math-svg-embedding)
4. [Phase C: P0 Command Migration](#phase-c-p0-command-migration)
5. [Phase D: P1 Command Migration](#phase-d-p1-command-migration)
6. [Phase E: P2 Command Migration](#phase-e-p2-command-migration)
7. [Phase F: Test Parity Validation](#phase-f-test-parity-validation)
8. [Phase G: Deprecation and Removal](#phase-g-deprecation-and-removal)
9. [Testing Strategy](#testing-strategy)
10. [File Manifest](#file-manifest)

---

## Current Codebase Inventory

### DVI Pipeline Files (to extend)

| File | Lines | Purpose |
|------|-------|---------|
| `lambda/tex/tex_latex_bridge.hpp` | 322 | LaTeXContext, API declarations |
| `lambda/tex/tex_latex_bridge.cpp` | ~1200 | Document/environment processing |
| `lambda/tex/tex_math_ts.cpp` | ~2500 | Math typesetting from tree-sitter |
| `lambda/tex/tex_math_bridge.cpp` | ~800 | Math bridge utilities |
| `lambda/tex/tex_node.hpp` | 650 | TexNode unified node system |
| `lambda/tex/tex_svg_out.hpp/cpp` | ~400 | SVG rendering from TexNode |
| `lambda/tex/tex_dvi_out.cpp` | ~600 | DVI output generation |
| `lambda/tex/tex_pdf_out.cpp` | ~800 | PDF output generation |

### HTML Pipeline Files (to deprecate)

| File | Lines | Purpose |
|------|-------|---------|
| `lambda/format/format_latex_html_v2.cpp` | ~10,000 | Full HTML pipeline |

### Test Files (must pass)

| Test File | Coverage |
|-----------|----------|
| `test_latex_html_v2_baseline.cpp` | Core document structure |
| `test_latex_html_v2_macros.cpp` | `\newcommand`, macro expansion |
| `test_latex_html_v2_tables.cpp` | `tabular`, `table` environments |
| `test_latex_html_v2_graphics_color.cpp` | `\includegraphics`, `\textcolor` |
| `test_latex_html_v2_bibliography.cpp` | `\cite`, `thebibliography` |
| `test_latex_html_v2_floats.cpp` | `figure`, `table` floats |
| `test_latex_html_v2_special_chars.cpp` | Escape sequences, Unicode |
| `test_latex_html_v2_lists_envs.cpp` | `itemize`, `enumerate`, `description` |
| `test_latex_html_v2_new_commands.cpp` | Advanced macro patterns |

---

## Phase A: Document Model Layer

**Duration**: 1 week  
**Deliverable**: `tex_document_model.hpp`, `tex_document_model.cpp`  
**Status**: ✅ **COMPLETED** (2026-01-14)

### Implementation Notes
- Created `tex_document_model.hpp` with full DocElement structure, 20+ element types
- Created `tex_document_model.cpp` with HTML rendering, CSS generation, tree operations
- Added comprehensive unit tests (27 tests passing)
- Integrated into build system via `build_lambda_config.json`

### A.1 Create Document Model Header

Create `lambda/tex/tex_document_model.hpp`:

```cpp
// tex_document_model.hpp - Intermediate Document Model for Unified Pipeline
//
// This layer sits between parsed LaTeX (Lambda Element AST) and output
// rendering (HTML, DVI, SVG, PDF). It captures document semantics while
// deferring output-specific formatting decisions.

#ifndef TEX_DOCUMENT_MODEL_HPP
#define TEX_DOCUMENT_MODEL_HPP

#include "tex_node.hpp"
#include "tex_latex_bridge.hpp"
#include "lib/arena.h"
#include "lib/arraylist.h"
#include "lib/strbuf.h"
#include "../lambda-data.hpp"

namespace tex {

// ============================================================================
// Document Element Types
// ============================================================================

enum class DocElemType : uint8_t {
    // Block-level elements
    PARAGRAPH,          // Text paragraph (may contain inline math)
    HEADING,            // Section/chapter heading
    LIST,               // itemize/enumerate/description
    LIST_ITEM,          // Single list item
    TABLE,              // tabular environment
    TABLE_ROW,          // Table row
    TABLE_CELL,         // Table cell
    FIGURE,             // figure environment
    BLOCKQUOTE,         // quote/quotation environment
    CODE_BLOCK,         // verbatim environment
    
    // Math elements (always typeset via TexNode)
    MATH_INLINE,        // $...$
    MATH_DISPLAY,       // $$...$$ or \[...\]
    MATH_EQUATION,      // equation environment (numbered)
    MATH_ALIGN,         // align/gather environment
    
    // Inline elements
    TEXT_SPAN,          // Styled text run
    LINK,               // \href, \url
    IMAGE,              // \includegraphics
    FOOTNOTE,           // \footnote
    CITATION,           // \cite
    CROSS_REF,          // \ref, \pageref
    
    // Structure elements
    DOCUMENT,           // Root document
    SECTION,            // Logical section container
    ABSTRACT,           // abstract environment
    TITLE_BLOCK,        // \maketitle content
    
    // Special
    RAW_HTML,           // Pass-through HTML (for compatibility)
    ERROR,              // Error recovery node
};

// ============================================================================
// Text Styling
// ============================================================================

struct TextStyle {
    enum Flags : uint16_t {
        BOLD        = 0x0001,
        ITALIC      = 0x0002,
        MONOSPACE   = 0x0004,
        SMALLCAPS   = 0x0008,
        UNDERLINE   = 0x0010,
        STRIKEOUT   = 0x0020,
        SUPERSCRIPT = 0x0040,
        SUBSCRIPT   = 0x0080,
    };
    
    uint16_t flags;
    const char* font_family;    // Override font (null = inherit)
    float font_size_pt;         // 0 = inherit
    uint32_t color;             // 0 = inherit (RGBA)
    uint32_t background;        // 0 = transparent
    
    static TextStyle plain() {
        TextStyle s = {};
        s.flags = 0;
        s.font_family = nullptr;
        s.font_size_pt = 0;
        s.color = 0;
        s.background = 0;
        return s;
    }
};

// ============================================================================
// Document Element
// ============================================================================

struct DocElement {
    DocElemType type;
    uint8_t flags;
    
    // Flag bits
    static constexpr uint8_t FLAG_NUMBERED = 0x01;  // Has auto-number
    static constexpr uint8_t FLAG_STARRED  = 0x02;  // \section* (no number)
    static constexpr uint8_t FLAG_CENTERED = 0x04;  // center environment
    
    // Content (type-dependent)
    union {
        // For TEXT_SPAN, PARAGRAPH text content
        struct {
            const char* text;
            size_t text_len;
            TextStyle style;
        } text;
        
        // For HEADING
        struct {
            int level;              // 0=part, 1=chapter, 2=section, etc.
            const char* title;
            const char* number;     // Generated number string (or null)
            const char* label;      // \label if present
        } heading;
        
        // For LIST
        struct {
            enum { ITEMIZE, ENUMERATE, DESCRIPTION } list_type;
            int start_num;          // For enumerate
        } list;
        
        // For TABLE
        struct {
            const char* column_spec; // "lcr|p{3cm}"
            int num_columns;
        } table;
        
        // For IMAGE
        struct {
            const char* src;
            float width;            // 0 = auto
            float height;           // 0 = auto
            const char* alt;
        } image;
        
        // For LINK
        struct {
            const char* href;
            const char* text;
        } link;
        
        // For MATH_* types - pre-typeset TexNode
        struct {
            TexNode* node;          // Typeset math tree
            const char* latex_src;  // Original LaTeX (for fallback)
            const char* label;      // Equation label
            const char* number;     // Equation number
        } math;
        
        // For CITATION
        struct {
            const char* key;
            const char* text;       // Rendered citation text
        } citation;
        
        // For CROSS_REF
        struct {
            const char* label;
            const char* text;       // Resolved reference text
        } ref;
        
        // For FOOTNOTE
        struct {
            int number;
            DocElement* content;
        } footnote;
    };
    
    // Children (for container types)
    DocElement* first_child;
    DocElement* last_child;
    DocElement* next_sibling;
    DocElement* parent;
    
    // Source location (for error reporting)
    SourceLoc source;
};

// ============================================================================
// Document Model
// ============================================================================

struct TexDocumentModel {
    Arena* arena;
    
    // Document metadata
    const char* document_class;     // "article", "report", "book"
    const char* title;
    const char* author;
    const char* date;
    
    // Package flags
    struct {
        bool amsmath : 1;
        bool amssymb : 1;
        bool graphicx : 1;
        bool hyperref : 1;
        bool xcolor : 1;
        bool geometry : 1;
    } packages;
    
    // Document tree
    DocElement* root;
    
    // Cross-reference tables
    struct LabelEntry {
        const char* label;
        const char* ref_text;
        int page;
    };
    LabelEntry* labels;
    int label_count;
    
    // Bibliography
    struct BibEntry {
        const char* key;
        const char* formatted;
    };
    BibEntry* bib_entries;
    int bib_count;
    
    // User-defined macros (simple substitution only)
    struct MacroDef {
        const char* name;
        int num_args;
        const char* replacement;
    };
    MacroDef* macros;
    int macro_count;
    
    // Counters
    int chapter_num;
    int section_num;
    int subsection_num;
    int equation_num;
    int figure_num;
    int table_num;
    int footnote_num;
};

// ============================================================================
// Builder API - Construct Document Model from LaTeX AST
// ============================================================================

/**
 * Build document model from parsed LaTeX (Lambda Element tree).
 *
 * @param elem Root element from tree-sitter parse
 * @param arena Arena for allocations
 * @param ctx LaTeX context with fonts
 * @return Document model (arena-allocated)
 */
TexDocumentModel* doc_model_from_latex(
    Item elem,
    Arena* arena,
    LaTeXContext& ctx
);

/**
 * Build document model from LaTeX source string.
 *
 * @param latex LaTeX source code
 * @param len Source length
 * @param arena Arena for allocations
 * @param fonts Font manager
 * @return Document model (arena-allocated)
 */
TexDocumentModel* doc_model_from_string(
    const char* latex,
    size_t len,
    Arena* arena,
    TFMFontManager* fonts
);

// ============================================================================
// Element Allocation
// ============================================================================

DocElement* doc_alloc_element(Arena* arena, DocElemType type);
void doc_append_child(DocElement* parent, DocElement* child);

// ============================================================================
// Output Renderers
// ============================================================================

// HTML output options
struct HtmlOutputOptions {
    enum FontMode {
        FONT_SYSTEM,        // System fonts only
        FONT_WEBFONT,       // Link to CDN fonts
        FONT_EMBEDDED,      // Embed WOFF2 in output
    };
    
    FontMode font_mode;
    bool math_as_svg;           // true = SVG, false = MathML
    bool typeset_paragraphs;    // true = Knuth-Plass, false = browser CSS
    bool standalone;            // Include <!DOCTYPE>, <html> wrapper
    bool pretty_print;          // Indent output
    const char* css_class_prefix; // Prefix for CSS classes (default "latex-")
    
    static HtmlOutputOptions defaults() {
        HtmlOutputOptions o = {};
        o.font_mode = FONT_WEBFONT;
        o.math_as_svg = true;
        o.typeset_paragraphs = false;
        o.standalone = true;
        o.pretty_print = true;
        o.css_class_prefix = "latex-";
        return o;
    }
};

/**
 * Render document model to HTML.
 */
bool doc_model_to_html(
    TexDocumentModel* doc,
    StrBuf* output,
    const HtmlOutputOptions& opts
);

/**
 * Render document model to TexNode tree (for DVI/PDF/SVG output).
 */
TexNode* doc_model_to_texnode(
    TexDocumentModel* doc,
    Arena* arena,
    LaTeXContext& ctx
);

// ============================================================================
// Individual Element Rendering (for incremental/streaming output)
// ============================================================================

void doc_element_to_html(DocElement* elem, StrBuf* out, const HtmlOutputOptions& opts, int depth);
TexNode* doc_element_to_texnode(DocElement* elem, Arena* arena, LaTeXContext& ctx);

} // namespace tex

#endif // TEX_DOCUMENT_MODEL_HPP
```

### A.2 Implement Document Model Builder

Create `lambda/tex/tex_document_model.cpp` with these key functions:

```cpp
// Skeleton structure - implement incrementally

TexDocumentModel* doc_model_from_latex(Item elem, Arena* arena, LaTeXContext& ctx) {
    TexDocumentModel* doc = arena_alloc<TexDocumentModel>(arena);
    memset(doc, 0, sizeof(*doc));
    doc->arena = arena;
    
    // Walk the Lambda Element tree
    ItemReader reader(elem);
    
    // Extract document class
    if (auto docclass = reader.get("documentclass")) {
        doc->document_class = reader.read_string(docclass);
    }
    
    // Process preamble for packages, macros, metadata
    if (auto preamble = reader.get("preamble")) {
        process_preamble(doc, preamble, ctx);
    }
    
    // Build document body
    if (auto body = reader.get("body")) {
        doc->root = build_doc_element(body, arena, ctx, doc);
    }
    
    return doc;
}
```

### A.3 Tasks

| Task | File | Effort |
|------|------|--------|
| Create `tex_document_model.hpp` | New file | 2 days |
| Implement `doc_alloc_element()` | `tex_document_model.cpp` | 0.5 day |
| Implement `doc_model_from_latex()` skeleton | `tex_document_model.cpp` | 1 day |
| Add to build system | `build_lambda_config.json` | 0.5 day |
| Unit tests for DocElement allocation | `test_doc_model.cpp` | 1 day |

---

## Phase B: Math SVG Embedding

**Duration**: 2 weeks  
**Deliverable**: Math renders identically in HTML (as inline SVG) and DVI  
**Status**: ✅ **COMPLETED** (2026-01-15)

### Implementation Notes
- Added `svg_render_math_inline()` and `svg_compute_math_bounds()` to `tex_svg_out.hpp/cpp`
- Updated `render_math_html()` in document model to use SVG when TexNode available
- Added math-specific CSS styles (`.math-inline svg`, `.math-display svg`, `.eq-number`)
- Added conditional compilation (`DOC_MODEL_NO_SVG`) for lightweight test builds
- Added 6 new math tests (33 total tests passing)

### B.1 SVG String Rendering API

Add to `tex_svg_out.hpp`:

```cpp
/**
 * Render math TexNode to inline SVG string suitable for HTML embedding.
 *
 * @param math Math TexNode (from tex_math_ts.cpp)
 * @param arena Arena for allocations
 * @param opts SVG parameters
 * @return SVG string without XML declaration (for inline use)
 */
const char* svg_render_math_inline(
    TexNode* math,
    Arena* arena,
    const SVGParams* opts
);

/**
 * Compute tight bounding box for math node.
 * Used to set SVG viewBox for inline math.
 */
void svg_compute_math_bounds(
    TexNode* math,
    float* width,
    float* height,
    float* depth
);
```

### B.2 HTML Math Embedding

```cpp
// In tex_document_model.cpp

void render_math_to_html(DocElement* elem, StrBuf* out, const HtmlOutputOptions& opts) {
    TexNode* math = elem->math.node;
    
    // Compute dimensions
    float width, height, depth;
    svg_compute_math_bounds(math, &width, &height, &depth);
    
    // CSS class based on display mode
    const char* css_class = (elem->type == DocElemType::MATH_INLINE)
        ? "math math-inline"
        : "math math-display";
    
    // Generate inline SVG
    Arena temp_arena;
    arena_init(&temp_arena, 4096);
    
    SVGParams params = SVGParams::defaults();
    params.viewport_width = width;
    params.viewport_height = height + depth;
    params.indent = false;  // Compact for inline
    
    const char* svg = svg_render_math_inline(math, &temp_arena, &params);
    
    // Output with wrapper
    if (elem->type == DocElemType::MATH_DISPLAY) {
        strbuf_appendf(out, "<div class=\"%s%s\">\n", opts.css_class_prefix, css_class);
        strbuf_append(out, svg);
        if (elem->math.number) {
            strbuf_appendf(out, "<span class=\"%seq-number\">(%s)</span>",
                opts.css_class_prefix, elem->math.number);
        }
        strbuf_append(out, "\n</div>\n");
    } else {
        strbuf_appendf(out, "<span class=\"%s%s\" style=\"vertical-align: %.1fpx;\">",
            opts.css_class_prefix, css_class, -depth);
        strbuf_append(out, svg);
        strbuf_append(out, "</span>");
    }
    
    arena_free(&temp_arena);
}
```

### B.3 Tasks

| Task | File | Effort |
|------|------|--------|
| Add `svg_render_math_inline()` | `tex_svg_out.cpp` | 2 days |
| Add `svg_compute_math_bounds()` | `tex_svg_out.cpp` | 1 day |
| Implement `render_math_to_html()` | `tex_document_model.cpp` | 2 days |
| Test inline math SVG alignment | `test_doc_model_math.cpp` | 2 days |
| Test display math with equation numbers | `test_doc_model_math.cpp` | 1 day |
| Visual comparison tests | Manual | 2 days |

---

## Phase C: P0 Command Migration

**Duration**: 2 weeks  
**Deliverable**: Basic text formatting works in unified pipeline  
**Status**: ✅ **COMPLETED** (2026-01-15)

### Implementation Summary

All Phase C functionality has been implemented in `tex_document_model.cpp`:

1. **Text Formatting Commands** (`build_text_command()`):
   - `\textbf{...}` → TEXT_SPAN + BOLD → `<strong>...</strong>`
   - `\textit{...}` → TEXT_SPAN + ITALIC → `<em>...</em>`
   - `\texttt{...}` → TEXT_SPAN + MONOSPACE → `<code>...</code>`
   - `\emph{...}` → TEXT_SPAN + ITALIC → `<em>...</em>`
   - `\underline{...}` → TEXT_SPAN + UNDERLINE → `<u>...</u>`
   - `\textsc{...}` → TEXT_SPAN + SMALLCAPS → `<span class="smallcaps">...</span>`

2. **Section Commands** (`build_section_command()`):
   - `\part{...}` → HEADING level=0 → `<h1>...</h1>`
   - `\chapter{...}` → HEADING level=1 → `<h2>...</h2>`
   - `\section{...}` → HEADING level=2 → `<h3>...</h3>`
   - `\subsection{...}` → HEADING level=3 → `<h4>...</h4>`
   - `\subsubsection{...}` → HEADING level=4 → `<h5>...</h5>`
   - `\paragraph{...}` → HEADING level=5 → `<h6>...</h6>`
   - `\subparagraph{...}` → HEADING level=6 → `<h6>...</h6>`

3. **Builder Infrastructure**:
   - `build_doc_element()` - Main recursive AST traversal
   - `build_inline_content()` - Inline text and math handling
   - `build_paragraph()` - Paragraph construction from children
   - `extract_text_content()` - Helper to extract text from AST nodes
   - Conditional compilation with `DOC_MODEL_MINIMAL` for minimal test builds

4. **Tests**: 42 unit tests passing including:
   - Text style rendering (bold, italic, monospace, underline, smallcaps)
   - Section heading rendering at all levels
   - Starred sections (unnumbered)
   - Nested text styles
   - Style combinations (bold + italic)

### C.1 Text Formatting Commands

Migrate these commands from `format_latex_html_v2.cpp`:

| Command | HTML Output | DocElemType |
|---------|-------------|-------------|
| `\textbf{...}` | `<strong>...</strong>` | TEXT_SPAN + BOLD |
| `\textit{...}` | `<em>...</em>` | TEXT_SPAN + ITALIC |
| `\texttt{...}` | `<code>...</code>` | TEXT_SPAN + MONOSPACE |
| `\emph{...}` | `<em>...</em>` | TEXT_SPAN + ITALIC |
| `\underline{...}` | `<u>...</u>` | TEXT_SPAN + UNDERLINE |
| `\textsc{...}` | `<span class="smallcaps">` | TEXT_SPAN + SMALLCAPS |

### C.2 Section Commands

| Command | HTML Output | DocElemType |
|---------|-------------|-------------|
| `\section{...}` | `<h2>...</h2>` | HEADING level=2 |
| `\subsection{...}` | `<h3>...</h3>` | HEADING level=3 |
| `\subsubsection{...}` | `<h4>...</h4>` | HEADING level=4 |
| `\paragraph{...}` | `<h5>...</h5>` | HEADING level=5 |
| `\chapter{...}` | `<h1>...</h1>` | HEADING level=1 |

### C.3 Implementation Pattern

```cpp
// tex_document_model.cpp

DocElement* build_text_command(const ItemReader& cmd, Arena* arena, 
                                LaTeXContext& ctx, TexDocumentModel* doc) {
    const char* name = cmd.tag();
    
    DocElement* elem = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
    elem->text.style = TextStyle::plain();
    
    if (strcmp(name, "textbf") == 0) {
        elem->text.style.flags |= TextStyle::BOLD;
    } else if (strcmp(name, "textit") == 0 || strcmp(name, "emph") == 0) {
        elem->text.style.flags |= TextStyle::ITALIC;
    } else if (strcmp(name, "texttt") == 0) {
        elem->text.style.flags |= TextStyle::MONOSPACE;
    } else if (strcmp(name, "underline") == 0) {
        elem->text.style.flags |= TextStyle::UNDERLINE;
    } else if (strcmp(name, "textsc") == 0) {
        elem->text.style.flags |= TextStyle::SMALLCAPS;
    }
    
    // Process children
    for (auto child : cmd.children()) {
        DocElement* child_elem = build_doc_element(child, arena, ctx, doc);
        doc_append_child(elem, child_elem);
    }
    
    return elem;
}

DocElement* build_section_command(const ItemReader& cmd, Arena* arena,
                                   LaTeXContext& ctx, TexDocumentModel* doc) {
    const char* name = cmd.tag();
    
    DocElement* elem = doc_alloc_element(arena, DocElemType::HEADING);
    
    // Determine level
    if (strcmp(name, "part") == 0) elem->heading.level = 0;
    else if (strcmp(name, "chapter") == 0) elem->heading.level = 1;
    else if (strcmp(name, "section") == 0) elem->heading.level = 2;
    else if (strcmp(name, "subsection") == 0) elem->heading.level = 3;
    else if (strcmp(name, "subsubsection") == 0) elem->heading.level = 4;
    else if (strcmp(name, "paragraph") == 0) elem->heading.level = 5;
    
    // Check for starred version
    if (cmd.get("star")) {
        elem->flags |= DocElement::FLAG_STARRED;
    } else {
        elem->flags |= DocElement::FLAG_NUMBERED;
        elem->heading.number = ctx.format_section_number(elem->heading.level, arena);
    }
    
    // Extract title
    if (auto title_item = cmd.get("title")) {
        elem->heading.title = extract_text_content(title_item, arena);
    }
    
    return elem;
}
```

### C.4 HTML Output for P0

```cpp
void doc_element_to_html(DocElement* elem, StrBuf* out, 
                          const HtmlOutputOptions& opts, int depth) {
    switch (elem->type) {
    case DocElemType::TEXT_SPAN:
        render_text_span_html(elem, out, opts);
        break;
    case DocElemType::HEADING:
        render_heading_html(elem, out, opts);
        break;
    case DocElemType::PARAGRAPH:
        render_paragraph_html(elem, out, opts, depth);
        break;
    // ... more cases
    }
}

void render_text_span_html(DocElement* elem, StrBuf* out, const HtmlOutputOptions& opts) {
    // Opening tags
    if (elem->text.style.flags & TextStyle::BOLD)
        strbuf_append(out, "<strong>");
    if (elem->text.style.flags & TextStyle::ITALIC)
        strbuf_append(out, "<em>");
    if (elem->text.style.flags & TextStyle::MONOSPACE)
        strbuf_append(out, "<code>");
    if (elem->text.style.flags & TextStyle::UNDERLINE)
        strbuf_append(out, "<u>");
    if (elem->text.style.flags & TextStyle::SMALLCAPS)
        strbuf_appendf(out, "<span class=\"%ssmallcaps\">", opts.css_class_prefix);
    
    // Content
    if (elem->text.text) {
        html_escape_append(out, elem->text.text, elem->text.text_len);
    }
    
    // Recurse to children
    for (DocElement* child = elem->first_child; child; child = child->next_sibling) {
        doc_element_to_html(child, out, opts, 0);
    }
    
    // Closing tags (reverse order)
    if (elem->text.style.flags & TextStyle::SMALLCAPS)
        strbuf_append(out, "</span>");
    if (elem->text.style.flags & TextStyle::UNDERLINE)
        strbuf_append(out, "</u>");
    if (elem->text.style.flags & TextStyle::MONOSPACE)
        strbuf_append(out, "</code>");
    if (elem->text.style.flags & TextStyle::ITALIC)
        strbuf_append(out, "</em>");
    if (elem->text.style.flags & TextStyle::BOLD)
        strbuf_append(out, "</strong>");
}

void render_heading_html(DocElement* elem, StrBuf* out, const HtmlOutputOptions& opts) {
    // Map level to HTML heading
    int h_level = elem->heading.level + 1;  // level 0 (part) -> h1
    if (h_level > 6) h_level = 6;
    
    strbuf_appendf(out, "<h%d class=\"%sheading-%d\">", 
        h_level, opts.css_class_prefix, elem->heading.level);
    
    // Number if present
    if (elem->heading.number && !(elem->flags & DocElement::FLAG_STARRED)) {
        strbuf_appendf(out, "<span class=\"%ssection-number\">%s</span> ",
            opts.css_class_prefix, elem->heading.number);
    }
    
    // Title
    if (elem->heading.title) {
        html_escape_append(out, elem->heading.title, strlen(elem->heading.title));
    }
    
    strbuf_appendf(out, "</h%d>\n", h_level);
}
```

### C.5 Tasks

| Task | File | Effort | Status |
|------|------|--------|--------|
| Implement `build_text_command()` | `tex_document_model.cpp` | 1 day | ✅ Done |
| Implement `build_section_command()` | `tex_document_model.cpp` | 1 day | ✅ Done |
| Implement `render_text_span_html()` | `tex_document_model.cpp` | 1 day | ✅ Done |
| Implement `render_heading_html()` | `tex_document_model.cpp` | 1 day | ✅ Done |
| Implement `render_paragraph_html()` | `tex_document_model.cpp` | 2 days | ✅ Done |
| HTML escaping utilities | `tex_document_model.cpp` | 0.5 day | ✅ Done |
| Integration tests | `test_tex_document_model_gtest.cpp` | 3 days | ✅ Done (42 tests) |

---

## Phase D: P1 Command Migration

**Duration**: 3 weeks  
**Deliverable**: Lists, tables, and environments work  
**Status**: ✅ **COMPLETED** (2026-01-15)

### Implementation Summary

All Phase D functionality has been implemented in `tex_document_model.cpp`:

1. **List Environments** (`build_list_environment()`):
   - `itemize` → LIST + ITEMIZE → `<ul>...</ul>`
   - `enumerate` → LIST + ENUMERATE → `<ol>...</ol>`
   - `description` → LIST + DESCRIPTION → `<dl>...</dl>`

2. **List Item Builder** (`build_list_item()`):
   - Regular items with content
   - Description items with labels (`<dt>term</dt><dd>content</dd>`)
   - Auto-numbering for enumerate lists

3. **Table Environments** (`build_table_environment()`):
   - `tabular` environment parsing
   - Column specification parsing (l/c/r alignment)
   - Row and cell separation handling

4. **Quote Environments** (`build_blockquote_environment()`):
   - `quote` → BLOCKQUOTE → `<blockquote>...</blockquote>`
   - `quotation` → BLOCKQUOTE → `<blockquote>...</blockquote>`

5. **Code Environments** (`build_code_block_environment()`):
   - `verbatim` → CODE_BLOCK → `<pre><code>...</code></pre>`
   - `lstlisting` → CODE_BLOCK → `<pre><code>...</code></pre>`

6. **Tests**: 49 unit tests passing including:
   - Unordered list (itemize) rendering
   - Ordered list (enumerate) rendering
   - Description list rendering
   - Table rendering with cells and rows
   - Blockquote rendering
   - Code block rendering
   - Nested list rendering

### D.1 List Environments

| Environment | HTML Output | Notes |
|-------------|-------------|-------|
| `itemize` | `<ul>...</ul>` | Unordered list |
| `enumerate` | `<ol>...</ol>` | Ordered list |
| `description` | `<dl>...</dl>` | Definition list |

```cpp
DocElement* build_list_environment(const ItemReader& env, Arena* arena,
                                    LaTeXContext& ctx, TexDocumentModel* doc) {
    const char* name = env.tag();
    
    DocElement* elem = doc_alloc_element(arena, DocElemType::LIST);
    
    if (strcmp(name, "itemize") == 0) {
        elem->list.list_type = DocElement::ITEMIZE;
    } else if (strcmp(name, "enumerate") == 0) {
        elem->list.list_type = DocElement::ENUMERATE;
        elem->list.start_num = 1;
    } else if (strcmp(name, "description") == 0) {
        elem->list.list_type = DocElement::DESCRIPTION;
    }
    
    // Process \item entries
    for (auto item : env.children()) {
        if (item.tag_eq("item")) {
            DocElement* li = doc_alloc_element(arena, DocElemType::LIST_ITEM);
            
            // For description, extract label
            if (elem->list.list_type == DocElement::DESCRIPTION) {
                if (auto label = item.get("label")) {
                    li->text.text = extract_text_content(label, arena);
                }
            }
            
            // Build item content
            for (auto child : item.children()) {
                DocElement* child_elem = build_doc_element(child, arena, ctx, doc);
                doc_append_child(li, child_elem);
            }
            
            doc_append_child(elem, li);
        }
    }
    
    return elem;
}
```

### D.2 Table Environment

```cpp
DocElement* build_tabular_environment(const ItemReader& env, Arena* arena,
                                       LaTeXContext& ctx, TexDocumentModel* doc) {
    DocElement* table = doc_alloc_element(arena, DocElemType::TABLE);
    
    // Parse column spec
    if (auto spec = env.get("column_spec")) {
        table->table.column_spec = extract_text_content(spec, arena);
        table->table.num_columns = count_columns(table->table.column_spec);
    }
    
    // Parse rows (split by \\)
    DocElement* current_row = nullptr;
    DocElement* current_cell = nullptr;
    
    for (auto child : env.children()) {
        if (child.tag_eq("row_sep") || child.tag_eq("newline")) {
            // End current row, start new one
            if (current_row) doc_append_child(table, current_row);
            current_row = doc_alloc_element(arena, DocElemType::TABLE_ROW);
            current_cell = nullptr;
        } else if (child.tag_eq("cell_sep") || child.tag_eq("ampersand")) {
            // End current cell, start new one
            if (current_cell) doc_append_child(current_row, current_cell);
            current_cell = doc_alloc_element(arena, DocElemType::TABLE_CELL);
        } else {
            // Content for current cell
            if (!current_row) {
                current_row = doc_alloc_element(arena, DocElemType::TABLE_ROW);
            }
            if (!current_cell) {
                current_cell = doc_alloc_element(arena, DocElemType::TABLE_CELL);
            }
            DocElement* content = build_doc_element(child, arena, ctx, doc);
            doc_append_child(current_cell, content);
        }
    }
    
    // Append final row/cell
    if (current_cell) doc_append_child(current_row, current_cell);
    if (current_row) doc_append_child(table, current_row);
    
    return table;
}
```

### D.3 HTML Output for D1

```cpp
void render_list_html(DocElement* elem, StrBuf* out, 
                       const HtmlOutputOptions& opts, int depth) {
    const char* tag;
    switch (elem->list.list_type) {
    case DocElement::ITEMIZE: tag = "ul"; break;
    case DocElement::ENUMERATE: tag = "ol"; break;
    case DocElement::DESCRIPTION: tag = "dl"; break;
    }
    
    indent(out, depth);
    strbuf_appendf(out, "<%s class=\"%slist\">\n", tag, opts.css_class_prefix);
    
    for (DocElement* item = elem->first_child; item; item = item->next_sibling) {
        render_list_item_html(item, out, opts, depth + 1, elem->list.list_type);
    }
    
    indent(out, depth);
    strbuf_appendf(out, "</%s>\n", tag);
}

void render_table_html(DocElement* elem, StrBuf* out,
                        const HtmlOutputOptions& opts, int depth) {
    indent(out, depth);
    strbuf_appendf(out, "<table class=\"%stable\">\n", opts.css_class_prefix);
    
    for (DocElement* row = elem->first_child; row; row = row->next_sibling) {
        indent(out, depth + 1);
        strbuf_append(out, "<tr>\n");
        
        int col_idx = 0;
        for (DocElement* cell = row->first_child; cell; cell = cell->next_sibling) {
            char align = get_column_alignment(elem->table.column_spec, col_idx);
            const char* align_class = (align == 'c') ? "center" : 
                                       (align == 'r') ? "right" : "left";
            
            indent(out, depth + 2);
            strbuf_appendf(out, "<td class=\"%s\">\n", align_class);
            
            for (DocElement* content = cell->first_child; content; content = content->next_sibling) {
                doc_element_to_html(content, out, opts, depth + 3);
            }
            
            indent(out, depth + 2);
            strbuf_append(out, "</td>\n");
            col_idx++;
        }
        
        indent(out, depth + 1);
        strbuf_append(out, "</tr>\n");
    }
    
    indent(out, depth);
    strbuf_append(out, "</table>\n");
}
```

### D.4 Tasks

| Task | File | Effort | Status |
|------|------|--------|--------|
| Implement `build_list_environment()` | `tex_document_model.cpp` | 2 days | ✅ Done |
| Implement `build_tabular_environment()` | `tex_document_model.cpp` | 3 days | ✅ Done |
| Implement `render_list_html()` | `tex_document_model.cpp` | 1 day | ✅ Done |
| Implement `render_table_html()` | `tex_document_model.cpp` | 2 days | ✅ Done |
| Quote/verbatim environments | `tex_document_model.cpp` | 2 days | ✅ Done |
| Column spec parsing | `tex_document_model.cpp` | 1 day | ✅ Done |
| Integration tests (lists) | `test_tex_document_model_gtest.cpp` | 2 days | ✅ Done |
| Integration tests (tables) | `test_tex_document_model_gtest.cpp` | 2 days | ✅ Done |

---

## Phase E: P2 Command Migration

**Duration**: 2 weeks  
**Deliverable**: Images, links, figures, and cross-references work  
**Status**: ✅ **COMPLETED** (2026-01-15)

### Implementation Notes
- Implemented `build_image_command()` for `\includegraphics` with width/height parsing
- Implemented `build_href_command()` and `build_url_command()` for hyperlinks
- Implemented `build_figure_environment()` with caption and label support
- Implemented `process_label_command()` and `build_ref_command()` for cross-references
- Implemented `build_footnote_command()` and `build_cite_command()` for footnotes/citations
- Added graphics dimension parsing (pt, cm, mm, in, px, em, textwidth)
- Added 12 new unit tests for Phase E elements (61 total tests passing)
- Label/citation resolution uses existing `TexDocumentModel` methods

### E.1 Commands to Migrate

| Command | HTML Output | Notes |
|---------|-------------|-------|
| `\includegraphics` | `<img src="..." />` | width/height support |
| `\href{url}{text}` | `<a href="...">...</a>` | External links |
| `\url{...}` | `<a href="...">...</a>` | URL display |
| `\begin{figure}` | `<figure>...</figure>` | Float container |
| `\caption{...}` | `<figcaption>...</figcaption>` | Figure caption |
| `\label{...}` | `id="..."` attribute | Cross-ref anchor |
| `\ref{...}` | `<a href="#...">...</a>` | Internal link |

### E.2 Implementation

```cpp
DocElement* build_image_command(const ItemReader& cmd, Arena* arena,
                                 LaTeXContext& ctx, TexDocumentModel* doc) {
    DocElement* elem = doc_alloc_element(arena, DocElemType::IMAGE);
    
    // Extract source file
    if (auto path = cmd.get("path")) {
        elem->image.src = extract_text_content(path, arena);
    }
    
    // Parse options [width=..., height=...]
    if (auto opts = cmd.get("options")) {
        parse_graphics_options(opts, &elem->image.width, &elem->image.height);
    }
    
    return elem;
}

DocElement* build_figure_environment(const ItemReader& env, Arena* arena,
                                      LaTeXContext& ctx, TexDocumentModel* doc) {
    DocElement* fig = doc_alloc_element(arena, DocElemType::FIGURE);
    fig->flags |= DocElement::FLAG_NUMBERED;
    
    doc->figure_num++;
    
    for (auto child : env.children()) {
        if (child.tag_eq("caption")) {
            // Extract caption and number it
            DocElement* caption = doc_alloc_element(arena, DocElemType::TEXT_SPAN);
            caption->text.text = arena_sprintf(arena, "Figure %d: %s",
                doc->figure_num, extract_text_content(child.get("text"), arena));
            doc_append_child(fig, caption);
        } else if (child.tag_eq("label")) {
            // Store label for cross-reference
            const char* label = extract_text_content(child, arena);
            doc_add_label(doc, label, arena_sprintf(arena, "%d", doc->figure_num), -1);
        } else {
            DocElement* content = build_doc_element(child, arena, ctx, doc);
            doc_append_child(fig, content);
        }
    }
    
    return fig;
}
```

### E.3 Tasks

| Task | File | Effort |
|------|------|--------|
| Implement `build_image_command()` | `tex_document_model.cpp` | 1 day |
| Implement graphics options parser | `tex_document_model.cpp` | 1 day |
| Implement `build_href_command()` | `tex_document_model.cpp` | 0.5 day |
| Implement `build_url_command()` | `tex_document_model.cpp` | 0.5 day |
| Implement `build_figure_environment()` | `tex_document_model.cpp` | 2 days |
| Implement label/ref resolution | `tex_document_model.cpp` | 2 days |
| HTML renderers for all P2 | `tex_document_model.cpp` | 2 days |
| Integration tests | `test_doc_model_p2.cpp` | 3 days |

---

## Phase F: Test Parity Validation

**Duration**: 2 weeks  
**Deliverable**: 100% pass rate on existing HTML tests

### F.1 Test Infrastructure

Create parallel test runner:

```cpp
// test/test_html_parity.cpp

class HtmlParityTest : public ::testing::Test {
protected:
    void SetUp() override {
        arena_init(&arena, 64 * 1024);
    }
    
    void TearDown() override {
        arena_free(&arena);
    }
    
    // Render using legacy pipeline
    std::string render_legacy(const char* latex) {
        StrBuf out;
        strbuf_init(&out, 4096);
        format_latex_to_html_v2(parse_latex(latex), &out);
        std::string result(out.data, out.len);
        strbuf_free(&out);
        return result;
    }
    
    // Render using unified pipeline
    std::string render_unified(const char* latex) {
        TFMFontManager fonts;
        LaTeXContext ctx = LaTeXContext::create(&arena, &fonts, "article");
        
        Item elem = parse_latex(latex);
        TexDocumentModel* doc = doc_model_from_latex(elem, &arena, ctx);
        
        StrBuf out;
        strbuf_init(&out, 4096);
        doc_model_to_html(doc, &out, HtmlOutputOptions::defaults());
        
        std::string result(out.data, out.len);
        strbuf_free(&out);
        return result;
    }
    
    // Normalize HTML for comparison (ignore whitespace, class order, etc.)
    std::string normalize(const std::string& html) {
        // Remove extra whitespace
        // Sort attributes alphabetically
        // Normalize self-closing tags
        // ...
    }
    
    Arena arena;
};

TEST_F(HtmlParityTest, TextFormatting) {
    const char* latex = "\\textbf{bold} \\textit{italic}";
    EXPECT_EQ(normalize(render_legacy(latex)), normalize(render_unified(latex)));
}

TEST_F(HtmlParityTest, Sections) {
    const char* latex = "\\section{Intro} Text \\subsection{Details}";
    EXPECT_EQ(normalize(render_legacy(latex)), normalize(render_unified(latex)));
}

// ... more tests
```

### F.2 Comparison Script

```bash
#!/bin/bash
# test/compare_html_pipelines.sh

for tex_file in test/latex/*.tex; do
    echo "Testing: $tex_file"
    
    # Render with legacy
    ./lambda.exe convert "$tex_file" -t html --pipeline=legacy -o /tmp/legacy.html
    
    # Render with unified
    ./lambda.exe convert "$tex_file" -t html --pipeline=unified -o /tmp/unified.html
    
    # Compare (ignore whitespace)
    if diff -q -w /tmp/legacy.html /tmp/unified.html > /dev/null; then
        echo "  PASS"
    else
        echo "  FAIL"
        diff -u /tmp/legacy.html /tmp/unified.html | head -50
    fi
done
```

### F.3 Tasks

| Task | File | Effort |
|------|------|--------|
| Create `test_html_parity.cpp` | New file | 2 days |
| Create `compare_html_pipelines.sh` | New file | 0.5 day |
| Implement HTML normalizer | `test_html_parity.cpp` | 2 days |
| Add `--pipeline` flag to CLI | `lambda/main.cpp` | 1 day |
| Fix failures from baseline tests | Various | 3 days |
| Fix failures from macro tests | Various | 2 days |
| Fix failures from other test files | Various | 2 days |

---

## Phase G: Deprecation and Removal

**Duration**: 1 week  
**Deliverable**: `format_latex_html_v2.cpp` removed

### G.1 Deprecation Steps

1. **Add deprecation warning** (already in codebase):
```cpp
[[deprecated("Use unified pipeline via doc_model_to_html()")]]
void format_latex_to_html_v2(Item input, StrBuf* output);
```

2. **Update CLI** to use unified pipeline by default:
```cpp
// lambda/main.cpp
case OutputFormat::HTML:
    if (use_legacy_pipeline) {
        format_latex_to_html_v2(input, &output);  // deprecated path
    } else {
        TexDocumentModel* doc = doc_model_from_latex(input, &arena, ctx);
        doc_model_to_html(doc, &output, HtmlOutputOptions::defaults());
    }
    break;
```

3. **Remove legacy code** after 100% test pass:
```bash
git rm lambda/format/format_latex_html_v2.cpp
# Update build_lambda_config.json
# Update any includes
```

### G.2 Tasks

| Task | File | Effort |
|------|------|--------|
| Add `[[deprecated]]` attribute | `format_latex_html_v2.cpp` | 0.5 day |
| Update CLI to use unified by default | `lambda/main.cpp` | 0.5 day |
| Run full test suite | CI | 1 day |
| Remove legacy file | `format_latex_html_v2.cpp` | 0.5 day |
| Update build system | `build_lambda_config.json` | 0.5 day |
| Update documentation | `doc/Lambda_Reference.md` | 1 day |
| Final validation | All tests | 1 day |

---

## Testing Strategy

### Test Categories

| Category | Tests | Coverage |
|----------|-------|----------|
| Unit tests | `test_doc_model.cpp` | DocElement allocation, tree building |
| Math tests | `test_doc_model_math.cpp` | SVG embedding, alignment |
| Command tests | `test_doc_model_commands.cpp` | Individual command handlers |
| Parity tests | `test_html_parity.cpp` | Legacy vs unified comparison |
| Integration | `test_latex_html_v2_*.cpp` | Full document rendering |

### Continuous Validation

```makefile
# Makefile additions

test-latex-html-unified:
	./test/test_html_parity.exe
	./test/compare_html_pipelines.sh

test-all-latex: test-latex-dvi test-latex-html-unified
	@echo "All LaTeX tests passed"
```

---

## File Manifest

### New Files to Create

| File | Purpose | Phase |
|------|---------|-------|
| `lambda/tex/tex_document_model.hpp` | Document model header | A |
| `lambda/tex/tex_document_model.cpp` | Document model implementation | A-E |
| `test/test_doc_model.cpp` | Unit tests | A |
| `test/test_doc_model_math.cpp` | Math SVG tests | B |
| `test/test_doc_model_commands.cpp` | Command tests | C-E |
| `test/test_html_parity.cpp` | Parity tests | F |
| `test/compare_html_pipelines.sh` | Comparison script | F |

### Files to Modify

| File | Changes | Phase |
|------|---------|-------|
| `lambda/tex/tex_svg_out.hpp` | Add math inline rendering | B |
| `lambda/tex/tex_svg_out.cpp` | Implement math inline rendering | B |
| `lambda/main.cpp` | Add `--pipeline` flag | F |
| `build_lambda_config.json` | Add new source files | A |
| `Makefile` | Add test targets | F |

### Files to Remove

| File | Phase |
|------|-------|
| `lambda/format/format_latex_html_v2.cpp` | G |

---

## Timeline Summary

| Phase | Duration | Cumulative |
|-------|----------|------------|
| A: Document Model | 1 week | 1 week |
| B: Math SVG | 2 weeks | 3 weeks |
| C: P0 Commands | 2 weeks | 5 weeks |
| D: P1 Commands | 3 weeks | 8 weeks |
| E: P2 Commands | 2 weeks | 10 weeks |
| F: Test Parity | 2 weeks | 12 weeks |
| G: Deprecation | 1 week | **13 weeks** |

---

## Success Criteria

| Criterion | Target | Measurement |
|-----------|--------|-------------|
| HTML test pass rate | 100% | `make test-latex-html-unified` |
| Math rendering | Pixel-identical | Visual diff |
| Performance | ≤ 2x legacy | Benchmark |
| Code reduction | -8000 lines | `wc -l` |
| DVI test pass rate | ≥ 90% | `make test-latex-dvi` |
