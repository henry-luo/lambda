// tex_doc_model_internal.hpp - Internal declarations for tex_document_model split files
//
// This header contains shared declarations for the internal implementation
// of the document model, used across multiple .cpp files.

#ifndef TEX_DOC_MODEL_INTERNAL_HPP
#define TEX_DOC_MODEL_INTERNAL_HPP

#include "tex_document_model.hpp"
#include "lib/log.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace tex {

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

} // namespace tex

#endif // TEX_DOC_MODEL_INTERNAL_HPP
