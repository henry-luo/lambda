// tex_lambda_bridge.hpp - Bridge between Lambda Documents and TeX Typesetter
//
// This file provides the integration layer connecting Lambda's document
// representation (Mark/Element tree) to the TeX typesetting pipeline.
//
// The bridge converts Lambda document elements to TeX nodes, enabling
// publication-quality typeset output from Lambda documents.
//
// Supported elements:
// - Paragraphs (p) - body text with inline formatting
// - Headings (h1-h6) - section titles with numbering
// - Lists (ul, ol, li) - bulleted and numbered lists
// - Math (math) - inline and display math via tex_math_bridge
// - Emphasis (em, strong, b, i) - inline formatting
// - Code (code, pre) - monospace text
// - Blockquotes (blockquote) - indented quotations
// - Links (a) - hyperlinks (currently rendered as text)
// - Images (img) - placeholders (future: actual images)
// - Tables (table) - basic table layout
// - Horizontal rules (hr) - section breaks
//
// Reference: Lambda Mark data model, TeXBook

#ifndef TEX_LAMBDA_BRIDGE_HPP
#define TEX_LAMBDA_BRIDGE_HPP

#include "tex_node.hpp"
#include "tex_tfm.hpp"
#include "tex_hlist.hpp"
#include "tex_linebreak.hpp"
#include "tex_vlist.hpp"
#include "tex_pagebreak.hpp"
#include "tex_math_bridge.hpp"
#include "tex_hyphen.hpp"
#include "../../lib/arena.h"

// Lambda includes - always available since this is the bridge
#include "../../lambda/lambda-data.hpp"
#include "../../lambda/mark_reader.hpp"

namespace tex {

// ============================================================================
// Document Context
// ============================================================================

// Font styles for inline formatting
enum class TextStyle : uint8_t {
    Roman,          // Normal roman text
    Italic,         // Emphasized text (\it)
    Bold,           // Bold text (\bf)
    BoldItalic,     // Bold italic
    Monospace,      // Code text (\tt)
    SmallCaps,      // Small capitals (\sc)
};

// Current formatting state
struct FormatState {
    TextStyle style;
    float size_pt;
    int list_depth;         // Current list nesting level
    int list_counter[8];    // Counters for nested numbered lists
    bool in_math;           // Whether we're inside math content

    FormatState()
        : style(TextStyle::Roman)
        , size_pt(10.0f)
        , list_depth(0)
        , in_math(false) {
        for (int i = 0; i < 8; i++) list_counter[i] = 0;
    }
};

// Section numbering state
struct SectionState {
    int chapter;
    int section;
    int subsection;
    int subsubsection;

    SectionState() : chapter(0), section(0), subsection(0), subsubsection(0) {}

    void increment(int level) {
        switch (level) {
            case 1: chapter++; section = subsection = subsubsection = 0; break;
            case 2: section++; subsection = subsubsection = 0; break;
            case 3: subsection++; subsubsection = 0; break;
            case 4: subsubsection++; break;
        }
    }
};

// Document typesetting context
struct DocumentContext {
    Arena* arena;
    TFMFontManager* fonts;

    // Page layout parameters
    float page_width;       // Total page width
    float page_height;      // Total page height
    float margin_left;
    float margin_right;
    float margin_top;
    float margin_bottom;

    // Computed text area
    float text_width;       // page_width - margin_left - margin_right
    float text_height;      // page_height - margin_top - margin_bottom

    // Typography settings
    float base_size_pt;     // Base font size (10pt default)
    float leading;          // Line spacing factor (1.2 default = 12pt for 10pt text)
    float parindent;        // Paragraph indentation
    float parskip;          // Space between paragraphs

    // Font specifications
    FontSpec roman_font;
    FontSpec italic_font;
    FontSpec bold_font;
    FontSpec mono_font;

    // TFM fonts
    TFMFont* roman_tfm;
    TFMFont* italic_tfm;
    TFMFont* bold_tfm;
    TFMFont* mono_tfm;

    // State
    FormatState format;
    SectionState sections;

    // Hyphenation engine
    HyphenEngine* hyphenator;

    // Create with default settings (8.5x11 inch page, 1 inch margins)
    static DocumentContext create(Arena* arena, TFMFontManager* fonts);

    // Create with custom page size
    static DocumentContext create(Arena* arena, TFMFontManager* fonts,
                                   float page_w, float page_h,
                                   float margin_lr, float margin_tb);

    // Get current font based on format state
    FontSpec current_font() const;
    TFMFont* current_tfm() const;

    // Get line break parameters for current context
    LineBreakParams line_break_params() const;

    // Get baseline skip (line height)
    float baseline_skip() const;

    // Create MathContext for math typesetting
    MathContext math_context() const;
};

// ============================================================================
// Document Conversion API
// ============================================================================

// Convert a Lambda document tree to a TeX VList (vertical list of content)
// This is the main entry point for document typesetting
//
// The Lambda document is expected to have a root element (like <doc> or <html>)
// containing block-level elements (paragraphs, headings, lists, etc.)
//
// Returns: A VList containing the typeset document content
TexNode* convert_document(
    Item document,
    DocumentContext& ctx
);

// Convert with explicit root element reader
TexNode* convert_document(
    const ElementReader& root,
    DocumentContext& ctx
);

// ============================================================================
// Block Element Conversion
// ============================================================================

// Convert a block-level element to TeX nodes
// Block elements are stacked vertically in the VList
TexNode* convert_block_element(
    const ElementReader& elem,
    DocumentContext& ctx
);

// Individual block element converters
TexNode* convert_paragraph(const ElementReader& elem, DocumentContext& ctx);
TexNode* convert_heading(const ElementReader& elem, int level, DocumentContext& ctx);
TexNode* convert_list(const ElementReader& elem, bool ordered, DocumentContext& ctx);
TexNode* convert_list_item(const ElementReader& elem, DocumentContext& ctx);
TexNode* convert_blockquote(const ElementReader& elem, DocumentContext& ctx);
TexNode* convert_code_block(const ElementReader& elem, DocumentContext& ctx);
TexNode* convert_math_block(const ElementReader& elem, DocumentContext& ctx);
TexNode* convert_table(const ElementReader& elem, DocumentContext& ctx);
TexNode* convert_horizontal_rule(DocumentContext& ctx);

// ============================================================================
// Inline Element Conversion
// ============================================================================

// Convert inline content to an HList (horizontal list of characters/nodes)
// Inline content includes text, emphasis, links, inline math, etc.
TexNode* convert_inline_content(
    const ItemReader& content,
    DocumentContext& ctx
);

// Build HList from element's children (text and inline elements)
TexNode* build_inline_hlist(
    const ElementReader& elem,
    DocumentContext& ctx
);

// Individual inline element converters
void append_text(TexNode* hlist, const char* text, size_t len, DocumentContext& ctx);
void append_emphasis(TexNode* hlist, const ElementReader& elem, DocumentContext& ctx);
void append_strong(TexNode* hlist, const ElementReader& elem, DocumentContext& ctx);
void append_code(TexNode* hlist, const ElementReader& elem, DocumentContext& ctx);
void append_link(TexNode* hlist, const ElementReader& elem, DocumentContext& ctx);
void append_inline_math(TexNode* hlist, const ElementReader& elem, DocumentContext& ctx);

// ============================================================================
// Text Processing Utilities
// ============================================================================

// Convert plain text to HList with proper character nodes and inter-word glue
// Applies ligatures and kerning via HListBuilder
TexNode* text_to_hlist(
    const char* text,
    size_t len,
    DocumentContext& ctx
);

// Process text looking for inline math ($...$) and convert appropriately
TexNode* process_text_with_inline_math(
    const char* text,
    size_t len,
    DocumentContext& ctx
);

// ============================================================================
// Page Breaking and Output
// ============================================================================

// Break document VList into pages
// Returns a list of Page nodes
struct PageList {
    TexNode** pages;
    int page_count;
    int total_badness;
};

PageList break_into_pages(
    TexNode* content,
    DocumentContext& ctx
);

// ============================================================================
// High-Level API
// ============================================================================

// Typeset to single VList (no page breaking)
TexNode* typeset_document(
    Item document,
    DocumentContext& ctx
);

// Alias for typeset_document
TexNode* typeset_document_vlist(
    Item document,
    DocumentContext& ctx
);

} // namespace tex

#endif // TEX_LAMBDA_BRIDGE_HPP
