/**
 * inline_common.hpp - Shared interface for inline parsers
 *
 * This header provides common types and function declarations
 * used by inline-level parsers (emphasis, code, link, image, etc.).
 */
#ifndef INLINE_COMMON_HPP
#define INLINE_COMMON_HPP

#include "../markup_parser.hpp"

namespace lambda {
namespace markup {

// ============================================================================
// Inline Parser Interface
// ============================================================================

/**
 * Inline parsers follow this pattern:
 *
 * 1. Receive MarkupParser* and pointer to current position in text
 * 2. Use adapter to detect inline element at position
 * 3. If detected, create element using builder
 * 4. Parse nested content if applicable
 * 5. Advance *text pointer past the parsed element
 * 6. Return Item containing the created element
 *
 * Returns ITEM_NULL if no inline element was detected
 * Returns ITEM_ERROR on fatal errors
 */

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Create an inline element with given tag name
 */
inline Element* create_inline_element(MarkupParser* parser, const char* tag) {
    return parser->builder_.element(tag).final().element;
}

/**
 * Create a text string item
 */
inline Item create_text(MarkupParser* parser, const char* text, size_t len) {
    String* str = parser->builder_.createString(text, len);
    return Item{.item = s2it(str)};
}

/**
 * Check if character at position is escaped
 */
inline bool is_escaped(const char* start, const char* pos) {
    if (pos <= start) return false;
    int backslashes = 0;
    const char* p = pos - 1;
    while (p >= start && *p == '\\') {
        backslashes++;
        p--;
    }
    return (backslashes % 2) == 1;
}

/**
 * Find closing delimiter, respecting escapes
 */
const char* find_closing(const char* start, const char* delimiter);

/**
 * Find the end of an inline element, handling nesting
 */
const char* find_inline_end(const char* start, const char* open, const char* close);

/**
 * Extract text between positions, handling escapes
 */
std::string extract_text(const char* start, const char* end, bool unescape = true);

/**
 * URL-decode a string (for links)
 */
std::string url_decode(const char* start, const char* end);

} // namespace markup
} // namespace lambda

#endif // INLINE_COMMON_HPP
