// tex_latex_bridge.hpp - Bridge between LaTeX source and TeX Typesetter
//
// This file provides the integration layer connecting LaTeX source files
// (parsed via tree-sitter-latex) to the TeX typesetting pipeline.
//
// The bridge walks the tree-sitter LaTeX AST (stored as Lambda Element tree)
// and converts LaTeX constructs directly to TeX nodes for typesetting.
//
// This enables publication-quality typeset output from LaTeX documents
// without going through the intermediate HTML conversion step.
//
// Supported LaTeX constructs:
// - Document structure: \documentclass, \begin{document}, \end{document}
// - Sectioning: \section, \subsection, \subsubsection, \paragraph, \chapter
// - Text formatting: \textbf, \textit, \texttt, \emph, \underline
// - Font commands: \bf, \it, \tt, \rm, \sf, \sc
// - Lists: itemize, enumerate, description environments
// - Math: inline ($...$), display ($$...$$ or \[...\]), equation, align, etc.
// - Environments: quote, quotation, center, flushleft, flushright, verbatim
// - Spacing: \vspace, \hspace, \quad, \qquad, \,, \;, \:, \!
// - Special characters: \%, \&, \#, \$, \_, \{, \}
// - Cross-references: \label, \ref (basic support)
//
// Reference: tree-sitter-latex grammar, TeXBook

#ifndef TEX_LATEX_BRIDGE_HPP
#define TEX_LATEX_BRIDGE_HPP

#include "tex_node.hpp"
#include "tex_tfm.hpp"
#include "tex_hlist.hpp"
#include "tex_linebreak.hpp"
#include "tex_vlist.hpp"
#include "tex_pagebreak.hpp"
#include "tex_math_bridge.hpp"
#include "tex_hyphen.hpp"
#include "tex_lambda_bridge.hpp"  // Reuse DocumentContext and utilities
#include "../../lib/arena.h"

// Lambda includes for Element tree traversal
#include "../../lambda/lambda-data.hpp"
#include "../../lambda/mark_reader.hpp"

namespace tex {

// ============================================================================
// LaTeX Typesetting Context
// ============================================================================

// LaTeX-specific context extending DocumentContext with LaTeX state
struct LaTeXContext {
    DocumentContext doc_ctx;     // Base document context

    // Document class settings
    const char* document_class;  // article, report, book, etc.
    bool two_column;
    bool twosided;

    // Current state
    bool in_preamble;            // Before \begin{document}
    bool in_verbatim;            // Inside verbatim environment

    // Counters
    int chapter_num;
    int section_num;
    int subsection_num;
    int subsubsection_num;
    int paragraph_num;
    int figure_num;
    int table_num;
    int equation_num;
    int page_num;

    // Label storage for cross-references (simple map)
    struct LabelEntry {
        const char* label;
        const char* ref_text;    // Generated reference text
        int page;
    };
    LabelEntry* labels;
    int label_count;
    int label_capacity;

    // Create with default article class settings
    static LaTeXContext create(Arena* arena, TFMFontManager* fonts);

    // Create with specific document class
    static LaTeXContext create(Arena* arena, TFMFontManager* fonts,
                                const char* doc_class);

    // Reset counters for new chapter (in book class)
    void reset_chapter_counters();

    // Get formatted section number string
    const char* format_section_number(int level, Arena* arena);

    // Label management
    void add_label(const char* label, const char* ref_text, int page);
    const char* resolve_ref(const char* label);
};

// ============================================================================
// Main API - LaTeX Document Typesetting
// ============================================================================

// Typeset a LaTeX document from its parsed Element tree
// This is the main entry point for LaTeX→TeX conversion
//
// Parameters:
//   latex_root: Root element from input-latex-ts.cpp (typically "latex_document")
//   ctx: LaTeX typesetting context
//
// Returns: VList containing the typeset document content
TexNode* typeset_latex_document(
    Item latex_root,
    LaTeXContext& ctx
);

// Typeset with ElementReader interface
TexNode* typeset_latex_document(
    const ElementReader& latex_root,
    LaTeXContext& ctx
);

// ============================================================================
// Block-Level Element Conversion
// ============================================================================

// Convert a block-level LaTeX element to TeX nodes
TexNode* convert_latex_block(
    const ItemReader& item,
    LaTeXContext& ctx
);

// Convert a block-level LaTeX element (ElementReader version)
TexNode* convert_latex_block(
    const ElementReader& elem,
    LaTeXContext& ctx
);

// Section commands: \section, \subsection, etc.
TexNode* convert_latex_section(
    const ElementReader& elem,
    int level,
    LaTeXContext& ctx
);

// Paragraph content
TexNode* convert_latex_paragraph(
    const ElementReader& elem,
    LaTeXContext& ctx
);

// List environments: itemize, enumerate, description
TexNode* convert_latex_list(
    const ElementReader& elem,
    bool ordered,
    bool description,
    LaTeXContext& ctx
);

// Quote environments: quote, quotation
TexNode* convert_latex_quote(
    const ElementReader& elem,
    bool quotation,  // quotation has paragraph indentation
    LaTeXContext& ctx
);

// Verbatim environment
TexNode* convert_latex_verbatim(
    const ElementReader& elem,
    LaTeXContext& ctx
);

// Tabular environment
TexNode* convert_latex_tabular(
    const ElementReader& elem,
    LaTeXContext& ctx
);

// Center/flush environments
TexNode* convert_latex_alignment(
    const ElementReader& elem,
    int alignment,  // -1=left, 0=center, 1=right
    LaTeXContext& ctx
);

// Display math: $$...$$ or \[...\] or equation environment
TexNode* convert_latex_display_math(
    const ElementReader& elem,
    LaTeXContext& ctx
);

// ============================================================================
// Inline Content Conversion
// ============================================================================

// Convert inline LaTeX content to HList
TexNode* convert_latex_inline(
    const ItemReader& content,
    LaTeXContext& ctx
);

// Text formatting commands: \textbf{}, \textit{}, \emph{}, etc.
void append_latex_text_command(
    TexNode* hlist,
    const char* command,
    const ElementReader& elem,
    LaTeXContext& ctx
);

// Font declarations: \bf, \it, \tt, etc. (affect subsequent text)
void apply_latex_font_declaration(
    const char* command,
    LaTeXContext& ctx
);

// Inline math: $...$
void append_latex_inline_math(
    TexNode* hlist,
    const ElementReader& elem,
    LaTeXContext& ctx
);

// Spacing commands: \quad, \qquad, \,, \;, \:, \!, \hspace
TexNode* make_latex_hspace(
    const char* command,
    LaTeXContext& ctx
);

// Special characters: escaped %, &, #, $, _, {, }
void append_latex_special_char(
    TexNode* hlist,
    char ch,
    LaTeXContext& ctx
);

// ============================================================================
// Command Handlers
// ============================================================================

// Generic command handler - dispatches to specific handlers
TexNode* handle_latex_command(
    const ElementReader& elem,
    LaTeXContext& ctx
);

// Check if command is a sectioning command
bool is_section_command(const char* cmd);

// Get section level from command name
int get_section_level(const char* cmd);

// Check if command is a text formatting command
bool is_text_format_command(const char* cmd);

// Check if command is a font declaration
bool is_font_declaration(const char* cmd);

// ============================================================================
// Environment Handlers
// ============================================================================

// Generic environment handler - dispatches to specific handlers
TexNode* handle_latex_environment(
    const ElementReader& elem,
    LaTeXContext& ctx
);

// Get environment name from element
const char* get_environment_name(const ElementReader& elem);

// Check if environment is a list environment
bool is_list_environment(const char* env_name);

// Check if environment is a math environment
bool is_math_environment(const char* env_name);

// ============================================================================
// Utility Functions
// ============================================================================

// Extract text content from an element tree (for simple cases)
void extract_latex_text(
    const ItemReader& item,
    char* buffer,
    size_t buffer_size,
    size_t* out_len
);

// Check if element is a specific tag
bool latex_tag_is(const ElementReader& elem, const char* tag);

// Get attribute value as string
const char* latex_get_attr(const ElementReader& elem, const char* attr);

// Process paragraph content and build HList
TexNode* build_latex_paragraph_hlist(
    const ElementReader& elem,
    LaTeXContext& ctx
);

// Apply line breaking to HList
TexNode* break_latex_paragraph(
    TexNode* hlist,
    LaTeXContext& ctx
);

// ============================================================================
// Page Breaking and Output
// ============================================================================

// Break typeset content into pages
PageList break_latex_into_pages(
    TexNode* content,
    LaTeXContext& ctx
);

} // namespace tex

#endif // TEX_LATEX_BRIDGE_HPP
