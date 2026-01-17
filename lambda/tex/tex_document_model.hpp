// tex_document_model.hpp - Intermediate Document Model for Unified Pipeline
//
// This layer sits between parsed LaTeX (Lambda Element AST) and output
// rendering (HTML, DVI, SVG, PDF). It captures document semantics while
// deferring output-specific formatting decisions.
//
// The goal is to unify the LaTeX->HTML and LaTeX->DVI pipelines into a single
// codebase, eventually phasing out format_latex_html_v2.cpp.
//
// Architecture:
//   LaTeX Source -> Tree-sitter -> Lambda Element AST
//                                        |
//                                        v
//                              TexDocumentModel (this layer)
//                                        |
//                    +-------------------+-------------------+
//                    |                   |                   |
//                    v                   v                   v
//                  HTML              TexNode             Other outputs
//                (inline SVG       (DVI/PDF/SVG)
//                 for math)
//
// Reference: Latex_Typeset_Design2.md, Latex_Typeset5.md

#ifndef TEX_DOCUMENT_MODEL_HPP
#define TEX_DOCUMENT_MODEL_HPP

#include "tex_node.hpp"
#ifndef DOC_MODEL_MINIMAL
#include "tex_latex_bridge.hpp"
#endif
#include "lib/arena.h"
#include "lib/arraylist.h"
#include "lib/strbuf.h"

// Lambda runtime is only needed for AST builder functions
#ifndef DOC_MODEL_MINIMAL
#include "../lambda-data.hpp"
#include "../mark_reader.hpp"
#endif

namespace tex {

#ifdef DOC_MODEL_MINIMAL
// Forward declarations for minimal builds
struct LaTeXContext;
struct TFMFontManager;
#endif

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
    ALIGNMENT,          // center/flushleft/flushright environment
    
    // Math elements (always typeset via TexNode)
    MATH_INLINE,        // $...$
    MATH_DISPLAY,       // $$...$$ or \[...\]
    MATH_EQUATION,      // equation environment (numbered)
    MATH_ALIGN,         // align/gather environment
    
    // Inline elements
    TEXT_SPAN,          // Styled text run
    TEXT_RUN,           // Plain text content
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
    RAW_LATEX,          // Unprocessed LaTeX content
    SPACE,              // Whitespace / line break
    ERROR,              // Error recovery node
};

// String name for debugging
const char* doc_elem_type_name(DocElemType type);

// ============================================================================
// Text Styling
// ============================================================================

enum class FontSizeName : uint8_t {
    INHERIT = 0,    // use parent's font size
    FONT_TINY,           // \tiny
    FONT_SCRIPTSIZE,     // \scriptsize  
    FONT_FOOTNOTESIZE,   // \footnotesize
    FONT_SMALL,          // \small
    FONT_NORMALSIZE,     // \normalsize (default)
    FONT_LARGE,          // \large
    FONT_LARGE2,         // \Large
    FONT_LARGE3,         // \LARGE
    FONT_HUGE,           // \huge
    FONT_HUGE2,          // \Huge
};

// Get CSS class name for font size
inline const char* font_size_name_class(FontSizeName sz) {
    switch (sz) {
    case FontSizeName::FONT_TINY: return "tiny";
    case FontSizeName::FONT_SCRIPTSIZE: return "scriptsize";
    case FontSizeName::FONT_FOOTNOTESIZE: return "footnotesize";
    case FontSizeName::FONT_SMALL: return "small";
    case FontSizeName::FONT_NORMALSIZE: return "normalsize";
    case FontSizeName::FONT_LARGE: return "large";
    case FontSizeName::FONT_LARGE2: return "Large";
    case FontSizeName::FONT_LARGE3: return "LARGE";
    case FontSizeName::FONT_HUGE: return "huge";
    case FontSizeName::FONT_HUGE2: return "Huge";
    default: return nullptr;
    }
}

struct DocTextStyle {
    enum Flags : uint16_t {
        NONE        = 0x0000,
        BOLD        = 0x0001,
        ITALIC      = 0x0002,
        MONOSPACE   = 0x0004,
        SMALLCAPS   = 0x0008,
        UNDERLINE   = 0x0010,
        STRIKEOUT   = 0x0020,
        SUPERSCRIPT = 0x0040,
        SUBSCRIPT   = 0x0080,
        SANS_SERIF  = 0x0100,
        ROMAN       = 0x0200,
    };
    
    uint16_t flags;
    FontSizeName font_size_name;  // Named size (\tiny, \small, etc.)
    const char* font_family;    // Override font (null = inherit)
    float font_size_pt;         // 0 = inherit
    uint32_t color;             // 0 = inherit (RGBA)
    uint32_t background;        // 0 = transparent
    
    static DocTextStyle plain() {
        DocTextStyle s = {};
        s.flags = NONE;
        s.font_size_name = FontSizeName::INHERIT;
        s.font_family = nullptr;
        s.font_size_pt = 0;
        s.color = 0;
        s.background = 0;
        return s;
    }
    
    bool has(Flags f) const { return (flags & f) != 0; }
    void set(Flags f) { flags |= f; }
    void clear(Flags f) { flags &= ~f; }
};

// ============================================================================
// List Type
// ============================================================================

enum class ListType : uint8_t {
    ITEMIZE,        // Unordered list (bullets)
    ENUMERATE,      // Ordered list (numbers)
    DESCRIPTION,    // Definition list (terms + descriptions)
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
    static constexpr uint8_t FLAG_FLUSH_LEFT  = 0x08;
    static constexpr uint8_t FLAG_FLUSH_RIGHT = 0x10;
    static constexpr uint8_t FLAG_CONTINUE = 0x20;  // paragraph continues after block element
    static constexpr uint8_t FLAG_NOINDENT = 0x40;  // \noindent - no paragraph indentation
    
    // Content (type-dependent union)
    union {
        // For TEXT_SPAN, TEXT_RUN, PARAGRAPH text content
        struct {
            const char* text;
            size_t text_len;
            DocTextStyle style;
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
            ListType list_type;
            int start_num;          // For enumerate
            int nesting_level;      // Nesting depth (0 = top-level)
        } list;
        
        // For LIST_ITEM
        struct {
            const char* label;      // For description items (plain text)
            const char* html_label; // Pre-rendered HTML label (for custom labels like \item[\itshape text])
            int item_number;        // For enumerate items
            bool has_custom_label;  // True if \item[...] was used (even if empty)
        } list_item;
        
        // For TABLE
        struct {
            const char* column_spec; // "lcr|p{3cm}"
            int num_columns;
            int num_rows;
        } table;
        
        // For TABLE_CELL
        struct {
            int colspan;
            int rowspan;
            char alignment;         // 'l', 'c', 'r', or 'p'
        } cell;
        
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
            const char* link_text;  // Renamed from 'text' to avoid conflict
        } link;
        
        // For ALIGNMENT (center, flushleft, flushright, quote, quotation, verse)
        struct {
            const char* env_name;   // "center", "quote", etc.
        } alignment;
        
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
            const char* cite_text;  // Rendered citation text
        } citation;
        
        // For CROSS_REF
        struct {
            const char* ref_label;
            const char* ref_text;   // Resolved reference text
        } ref;
        
        // For FOOTNOTE
        struct {
            int footnote_number;
            // Content is in children
        } footnote;
        
        // For SPACE
        struct {
            bool is_linebreak;      // \\ or \newline
            float vspace;           // \vspace amount (0 = normal)
            float hspace;           // \hspace amount (0 = normal)
        } space;
        
        // For RAW_HTML, RAW_LATEX
        struct {
            const char* raw_content;
            size_t raw_len;
        } raw;
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
    struct PackageFlags {
        bool amsmath : 1;
        bool amssymb : 1;
        bool graphicx : 1;
        bool hyperref : 1;
        bool xcolor : 1;
        bool geometry : 1;
        bool fontenc : 1;
        bool inputenc : 1;
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
    int label_capacity;
    
    // Bibliography
    struct BibEntry {
        const char* key;
        const char* formatted;
    };
    BibEntry* bib_entries;
    int bib_count;
    int bib_capacity;
    
    // User-defined macros (simple substitution only)
    struct MacroDef {
        const char* name;
        int num_args;
        const char* replacement;
    };
    MacroDef* macros;
    int macro_count;
    int macro_capacity;
    
    // Counters
    int chapter_num;
    int section_num;
    int subsection_num;
    int subsubsection_num;
    int paragraph_num;
    int equation_num;
    int figure_num;
    int table_num;
    int footnote_num;
    int page_num;
    int section_id_counter;  // Global counter for sequential section IDs
    
    // Methods
    void add_label(const char* label, const char* ref_text, int page);
    const char* resolve_ref(const char* label) const;
    void add_macro(const char* name, int num_args, const char* replacement);
    const MacroDef* find_macro(const char* name) const;
    void add_bib_entry(const char* key, const char* formatted);
    const char* resolve_cite(const char* key) const;
};

// ============================================================================
// Builder API - Construct Document Model from LaTeX AST
// ============================================================================

/**
 * Initialize a new document model.
 *
 * @param arena Arena for allocations
 * @return Initialized document model
 */
TexDocumentModel* doc_model_create(Arena* arena);

#ifndef DOC_MODEL_MINIMAL
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
#endif

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
// Element Allocation and Tree Building
// ============================================================================

/**
 * Allocate a new document element.
 */
DocElement* doc_alloc_element(Arena* arena, DocElemType type);

/**
 * Append a child element to a parent.
 */
void doc_append_child(DocElement* parent, DocElement* child);

/**
 * Insert a child element before another child.
 */
void doc_insert_before(DocElement* parent, DocElement* before, DocElement* child);

/**
 * Remove a child element from its parent.
 */
void doc_remove_child(DocElement* parent, DocElement* child);

/**
 * Create a text element with given content.
 */
DocElement* doc_create_text(Arena* arena, const char* text, size_t len, DocTextStyle style);

/**
 * Create a text element (null-terminated string).
 */
DocElement* doc_create_text_cstr(Arena* arena, const char* text, DocTextStyle style);

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
    bool include_css;           // Include default CSS styles
    bool legacy_mode;           // true = latex-js compatible output
    const char* css_class_prefix; // Prefix for CSS classes (default "latex-")
    const char* lang;           // Language code (default "en")
    
    static HtmlOutputOptions defaults() {
        HtmlOutputOptions o = {};
        o.font_mode = FONT_WEBFONT;
        o.math_as_svg = true;
        o.typeset_paragraphs = false;
        o.standalone = true;
        o.pretty_print = true;
        o.include_css = true;
        o.legacy_mode = false;
        o.css_class_prefix = "latex-";
        o.lang = "en";
        return o;
    }
    
    static HtmlOutputOptions legacy() {
        HtmlOutputOptions o = {};
        o.font_mode = FONT_WEBFONT;
        o.math_as_svg = false;
        o.typeset_paragraphs = false;
        o.standalone = false;
        o.pretty_print = true;
        o.include_css = false;
        o.legacy_mode = true;
        o.css_class_prefix = "";  // No prefix in legacy mode
        o.lang = "en";
        return o;
    }
};

/**
 * Render document model to HTML.
 *
 * @param doc Document model to render
 * @param output Output string buffer
 * @param opts HTML output options
 * @return true on success
 */
bool doc_model_to_html(
    TexDocumentModel* doc,
    StrBuf* output,
    const HtmlOutputOptions& opts
);

/**
 * Render document model to TexNode tree (for DVI/PDF/SVG output).
 *
 * @param doc Document model to render
 * @param arena Arena for TexNode allocation
 * @param ctx LaTeX context with fonts
 * @return Root TexNode (typically a VList)
 */
TexNode* doc_model_to_texnode(
    TexDocumentModel* doc,
    Arena* arena,
    LaTeXContext& ctx
);

// ============================================================================
// Individual Element Rendering
// ============================================================================

/**
 * Render a single element to HTML.
 *
 * @param elem Element to render
 * @param out Output buffer
 * @param opts HTML options
 * @param depth Indentation depth
 */
void doc_element_to_html(
    DocElement* elem,
    StrBuf* out,
    const HtmlOutputOptions& opts,
    int depth
);

/**
 * Render a single element to TexNode.
 *
 * @param elem Element to render
 * @param arena Arena for allocation
 * @param ctx LaTeX context
 * @return TexNode representation
 */
TexNode* doc_element_to_texnode(
    DocElement* elem,
    Arena* arena,
    LaTeXContext& ctx
);

// ============================================================================
// HTML Utilities
// ============================================================================

/**
 * Escape text for HTML output.
 *
 * @param out Output buffer
 * @param text Text to escape
 * @param len Text length
 */
void html_escape_append(StrBuf* out, const char* text, size_t len);

/**
 * Write indentation to output.
 *
 * @param out Output buffer
 * @param depth Indentation depth
 */
void html_indent(StrBuf* out, int depth);

/**
 * Generate default CSS for LaTeX documents.
 *
 * @param out Output buffer
 * @param prefix CSS class prefix
 */
void html_write_default_css(StrBuf* out, const char* prefix);

// ============================================================================
// Debug Utilities
// ============================================================================

/**
 * Print document tree for debugging.
 *
 * @param doc Document model
 * @param out Output buffer
 */
void doc_model_dump(TexDocumentModel* doc, StrBuf* out);

/**
 * Print element tree for debugging.
 *
 * @param elem Root element
 * @param out Output buffer
 * @param depth Indentation depth
 */
void doc_element_dump(DocElement* elem, StrBuf* out, int depth);

} // namespace tex

#endif // TEX_DOCUMENT_MODEL_HPP
