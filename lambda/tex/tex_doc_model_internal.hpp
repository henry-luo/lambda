// tex_doc_model_internal.hpp - Internal declarations for tex_document_model split files
//
// This header contains shared declarations for the internal implementation
// of the document model, used across multiple .cpp files.
//
// NOTE: This is a preparation header for future modularization.
// Currently, all implementations remain in tex_document_model.cpp.
// The declarations below document the planned module structure.

#ifndef TEX_DOC_MODEL_INTERNAL_HPP
#define TEX_DOC_MODEL_INTERNAL_HPP

#include "tex_document_model.hpp"
#include "../mark_reader.hpp"
#include "lib/log.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace tex {

// ============================================================================
// Internal Types
// ============================================================================

// Alignment state enumeration for paragraph building
enum class ParagraphAlignment : uint8_t {
    NONE = 0,       // No explicit alignment (default)
    CENTERING,      // \centering - center alignment
    RAGGEDRIGHT,    // \raggedright - left alignment (ragged right)
    RAGGEDLEFT      // \raggedleft - right alignment (ragged left)
};

// ============================================================================
// Text Transformation Utilities (from tex_doc_model_text.cpp)
// ============================================================================

// Normalize LaTeX whitespace: collapse consecutive whitespace to single space
const char* normalize_latex_whitespace(const char* text, Arena* arena);

// Transform LaTeX text to typographic text (ligatures, quotes, dashes)
char* transform_latex_text(const char* text, size_t len, bool in_monospace);

// HTML escape and append text with LaTeX transformations
void html_escape_append_transformed(StrBuf* out, const char* text, size_t len, bool in_monospace);

// Apply diacritic command to base character
const char* apply_diacritic(char diacritic_cmd, const char* base_char, Arena* arena);

// Check if a tag is a diacritic command
bool is_diacritic_tag(const char* tag);

// Get UTF-8 character length from first byte
int utf8_char_len(unsigned char first_byte);

// ============================================================================
// HTML Utilities (from tex_doc_model_html.cpp)
// ============================================================================

// Check if an element is inline content (used by both HTML rendering and model building)
bool is_inline_element(DocElement* elem);

// ============================================================================
// Documentation: Planned Module Structure
// ============================================================================
//
// tex_doc_model_builder.cpp - Core builder dispatch and markers
//   - Sentinel marker definitions (PARBREAK_MARKER, LINEBREAK_MARKER, etc.)
//   - Marker helper functions
//   - Tag classification utilities
//   - Paragraph whitespace utilities
//
// tex_doc_model_struct.cpp - Structural element builders
//   - build_section_command
//   - build_list_environment
//   - build_table_environment
//   - build_blockquote_environment
//   - build_alignment_environment
//   - build_code_block_environment
//   - build_figure_environment
//
// tex_doc_model_inline.cpp - Inline content builders
//   - build_text_command
//   - build_text_command_set_style
//   - Symbol command handling
//   - Diacritic command handling
//
// tex_doc_model_commands.cpp - Special command builders
//   - build_image_command
//   - build_href_command
//   - build_url_command
//   - build_ref_command

// ============================================================================
// Sentinel Markers (from tex_document_model.cpp)
// ============================================================================
// Special pointer values used during tree building to mark paragraph breaks, etc.
// These are not valid DocElement pointers - they are sentinel values.

extern DocElement* const PARBREAK_MARKER;   // paragraph break marker
extern DocElement* const LINEBREAK_MARKER;  // line break marker
extern DocElement* const NOINDENT_MARKER;   // \noindent command marker

// ============================================================================
// Common Helpers (from tex_document_model.cpp, used across modules)
// ============================================================================

// Helper to check tag name equality
bool tag_eq(const char* a, const char* b);

// Extract text content from an item (recursively collects all text)
const char* extract_text_content(const ItemReader& item, Arena* arena);

// Parse dimension value (e.g., "10cm", "100pt", "0.5\textwidth")
float parse_dimension(const char* value, Arena* arena);

// Parse graphics options like [width=10cm, height=5cm]
void parse_graphics_options(const char* opts, float* width, float* height, Arena* arena);

// Forward declaration for build_doc_element
DocElement* build_doc_element(const ItemReader& item, Arena* arena, TexDocumentModel* doc);

// ============================================================================
// Command Builders (from tex_doc_model_commands.cpp)
// ============================================================================

// Build image command (\includegraphics)
DocElement* build_image_command(const ElementReader& elem, Arena* arena, TexDocumentModel* doc);

// Build href command (\href{url}{text})
DocElement* build_href_command(const ElementReader& elem, Arena* arena, TexDocumentModel* doc);

// Build url command (\url{...})
DocElement* build_url_command(const ElementReader& elem, Arena* arena, TexDocumentModel* doc);

// Build label command (\label{...})
void process_label_command(const ElementReader& elem, Arena* arena,
                           TexDocumentModel* doc, DocElement* parent);

// Build ref command (\ref{...})
DocElement* build_ref_command(const ElementReader& elem, Arena* arena, TexDocumentModel* doc);

// Build figure environment
DocElement* build_figure_environment(const ElementReader& elem, Arena* arena, TexDocumentModel* doc);

// Build footnote command (\footnote{...})
DocElement* build_footnote_command(const ElementReader& elem, Arena* arena, TexDocumentModel* doc);

// Build cite command (\cite{...})
DocElement* build_cite_command(const ElementReader& elem, Arena* arena, TexDocumentModel* doc);

// ============================================================================
// Structural Builders (from tex_doc_model_struct.cpp)
// ============================================================================

// Build section command (\section, \subsection, etc.)
DocElement* build_section_command(const char* cmd_name, const ElementReader& elem,
                                   Arena* arena, TexDocumentModel* doc);

// Build list environment (itemize, enumerate, description)
DocElement* build_list_environment(const char* env_name, const ElementReader& elem,
                                    Arena* arena, TexDocumentModel* doc);

// Build table environment (tabular)
DocElement* build_table_environment(const char* env_name, const ElementReader& elem,
                                     Arena* arena, TexDocumentModel* doc);

// Build blockquote environment (quote, quotation)
DocElement* build_blockquote_environment(const ElementReader& elem,
                                          Arena* arena, TexDocumentModel* doc);

// Build alignment environment (center, flushleft, flushright, verse)
DocElement* build_alignment_environment(const char* env_name, const ElementReader& elem,
                                         Arena* arena, TexDocumentModel* doc);

// Build code block environment (verbatim, lstlisting)
DocElement* build_code_block_environment(const char* env_name, const ElementReader& elem,
                                          Arena* arena, TexDocumentModel* doc);

// ============================================================================
// Shared Helper Functions (from tex_document_model.cpp)
// ============================================================================

// Check if a tag represents a block element (for inline/block decisions)
bool is_block_element_tag(const char* tag);

// Check if a tag is a document-level block element
bool is_document_block_tag(const char* tag);

// Check if an element is a special marker
bool is_special_marker(DocElement* elem);

// Check if an element is an alignment marker
bool is_alignment_marker(DocElement* elem);

// Convert alignment marker to alignment enum
ParagraphAlignment marker_to_alignment(DocElement* marker);

// Apply alignment to a paragraph
void apply_alignment_to_paragraph(DocElement* para, ParagraphAlignment align);

// Check if an item represents a paragraph break
bool is_parbreak_item(const ItemReader& item);

// Trim paragraph whitespace (leading/trailing text)
void trim_paragraph_whitespace(DocElement* para, Arena* arena);

// Trim paragraph whitespace with control over linebreak space handling
void trim_paragraph_whitespace_ex(DocElement* para, Arena* arena, bool preserve_linebreak_space);

// Build inline content from an item
DocElement* build_inline_content(const ItemReader& item, Arena* arena, TexDocumentModel* doc);

// Process labels within an element (recursive)
void process_labels_in_element(const ItemReader& item, Arena* arena,
                                TexDocumentModel* doc, DocElement* parent);

// Render brack_group content to HTML string
const char* render_brack_group_to_html(const ItemReader& item, Arena* arena, TexDocumentModel* doc);

} // namespace tex

#endif // TEX_DOC_MODEL_INTERNAL_HPP
