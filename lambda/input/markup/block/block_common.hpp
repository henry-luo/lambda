/**
 * block_common.hpp - Shared block-level parsing interface
 *
 * This header defines the common interface and utilities for all block-level
 * parsers. Block parsers handle document structure elements like headers,
 * lists, code blocks, blockquotes, tables, etc.
 *
 * Each block parser file (block_header.cpp, block_list.cpp, etc.) implements
 * one or more block parsing functions that follow a common pattern:
 *   - Take a MarkupParser* and optionally the current line
 *   - Use the FormatAdapter for format-specific detection
 *   - Return an Item containing the parsed Element or error
 *   - Advance parser->current_line as appropriate
 */
#ifndef BLOCK_COMMON_HPP
#define BLOCK_COMMON_HPP

#include "../markup_common.hpp"
#include "../markup_parser.hpp"
#include "../format_adapter.hpp"
#include "../../input-context.hpp"
#include "../../../mark_builder.hpp"
#include "lib/strbuf.h"
#include "lib/log.h"
#include <cstring>
#include <cctype>

namespace lambda {
namespace markup {

// Forward declarations
class MarkupParser;
class FormatAdapter;

// ============================================================================
// Block Parser Function Signatures
// ============================================================================

/**
 * Parse a header element (h1-h6)
 * Handles ATX-style (#), Setext-style (underline), wiki (==), org (*), etc.
 */
Item parse_header(MarkupParser* parser, const char* line);

/**
 * Parse a list structure (ul/ol with nested li elements)
 * Handles ordered and unordered lists with proper nesting
 */
Item parse_list_item(MarkupParser* parser, const char* line);
Item parse_list_structure(MarkupParser* parser, int base_indent);
Item parse_nested_list_content(MarkupParser* parser, int base_indent);

/**
 * Parse a code block (fenced or indented)
 * Handles ```, ~~~, indented blocks, and format-specific variants
 */
Item parse_code_block(MarkupParser* parser, const char* line);

/**
 * Parse a blockquote element
 * Handles > prefix and nested blockquotes
 */
Item parse_blockquote(MarkupParser* parser, const char* line);

/**
 * Parse a table structure
 * Handles GFM tables, wiki tables, RST tables, etc.
 */
Item parse_table_row(MarkupParser* parser, const char* line);
Item parse_table_cell_content(MarkupParser* parser, const char* text);

/**
 * Parse a math block ($$...$$)
 * Handles display math with LaTeX/AsciiMath content
 */
Item parse_math_block(MarkupParser* parser, const char* line);

/**
 * Parse a horizontal rule/divider
 * Handles ---, ***, ___, etc.
 */
Item parse_divider(MarkupParser* parser);

/**
 * Parse a paragraph (fallback block type)
 * Collects text lines until a different block type is encountered
 */
Item parse_paragraph(MarkupParser* parser, const char* line);

/**
 * Parse inline content within a block
 * Called by block parsers to process text content
 */
Item parse_inline_spans(MarkupParser* parser, const char* text);

// ============================================================================
// Block Detection Utilities
// ============================================================================

/**
 * Detect the type of block that starts at the given line
 */
BlockType detect_block_type(MarkupParser* parser, const char* line);

/**
 * Check if a line is empty or contains only whitespace
 */
inline bool is_empty_line(const char* line) {
    if (!line) return true;
    while (*line) {
        if (*line != ' ' && *line != '\t' && *line != '\r' && *line != '\n') {
            return false;
        }
        line++;
    }
    return true;
}

/**
 * Check if a line is a code fence (``` or ~~~)
 */
bool is_code_fence(const char* line);

/**
 * Check if a line is a thematic break (---, ***, ___)
 */
bool is_thematic_break(const char* line);

/**
 * Check if a line is a list item
 */
bool is_list_item(const char* line);

/**
 * Get the list marker character from a list item line
 */
char get_list_marker(const char* line);

/**
 * Check if a marker indicates an ordered list
 */
bool is_ordered_marker(char marker);

/**
 * Get indentation level (number of leading spaces)
 */
int get_list_indentation(const char* line);

/**
 * Get header level from a line (0 if not a header)
 */
int get_header_level(MarkupParser* parser, const char* line);

/**
 * Check if a line might start a link reference definition
 */
bool is_link_definition_start(const char* line);

/**
 * Parse a link reference definition and add it to the parser
 * Returns true if successfully parsed, false otherwise
 */
bool parse_link_definition(MarkupParser* parser, const char* line);

/**
 * Check if a line starts an HTML block that can interrupt a paragraph
 * Only HTML block types 1-6 can interrupt paragraphs (type 7 cannot)
 */
bool html_block_can_interrupt_paragraph(const char* line);

// ============================================================================
// Element Creation Utilities
// ============================================================================

/**
 * Create a new element with the given tag name
 * Uses the parser's MarkBuilder
 */
inline Element* create_element(MarkupParser* parser, const char* tag) {
    return parser->builder.element(tag).final().element;
}

/**
 * Add an attribute to an element
 */
void add_attribute_to_element(MarkupParser* parser, Element* elem,
                              const char* name, const char* value);

/**
 * Create a string in the parser's memory pool
 */
inline String* create_string(MarkupParser* parser, const char* text) {
    return parser->builder.createString(text);
}

/**
 * Increment an element's content length counter
 */
inline void increment_element_content_length(Element* elem) {
    if (elem && elem->type) {
        TypeElmt* elmt_type = (TypeElmt*)elem->type;
        elmt_type->content_length++;
    }
}

// ============================================================================
// Text Processing Utilities
// ============================================================================

// Note: skip_whitespace is defined in markup_common.hpp

/**
 * Detect math flavor from content (latex, ascii, etc.)
 */
const char* detect_math_flavor(const char* content);

/**
 * Parse math content using the appropriate parser
 */
Item parse_math_content(InputContext& ctx, const char* content, const char* flavor);

} // namespace markup
} // namespace lambda

#endif // BLOCK_COMMON_HPP
