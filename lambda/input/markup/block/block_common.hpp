/**
 * block_common.hpp - Shared interface for block parsers
 *
 * This header provides common types and function declarations
 * used by block-level parsers (header, list, code, quote, table, etc.).
 */
#ifndef BLOCK_COMMON_HPP
#define BLOCK_COMMON_HPP

#include "../markup_parser.hpp"

namespace lambda {
namespace markup {

// ============================================================================
// Block Parser Interface
// ============================================================================

/**
 * Block parsers follow this pattern:
 *
 * 1. Receive MarkupParser* and current line
 * 2. Use adapter to detect block type specifics
 * 3. Create element using builder
 * 4. Parse content (may recurse for nested blocks)
 * 5. Advance parser->current_line as needed
 * 6. Return Item containing the created element
 *
 * Returns ITEM_NULL if no block was parsed (e.g., blank line)
 * Returns ITEM_ERROR on fatal errors
 */

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Create a block element with given tag name
 */
inline Element* create_block_element(MarkupParser* parser, const char* tag) {
    return parser->builder_.element(tag).final().element;
}

/**
 * Add attribute to element
 */
void add_block_attribute(Element* elem, const char* name, const char* value);

/**
 * Add child item to element
 */
void add_block_child(Element* elem, Item child);

/**
 * Collect consecutive lines matching a predicate
 * Returns the collected text and advances parser->current_line
 */
std::string collect_lines_while(MarkupParser* parser,
                                bool (*predicate)(const char* line, void* ctx),
                                void* ctx = nullptr);

/**
 * Check if line continues a paragraph (not a block start)
 */
bool is_paragraph_continuation(MarkupParser* parser, const char* line);

} // namespace markup
} // namespace lambda

#endif // BLOCK_COMMON_HPP
