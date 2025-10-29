#ifndef CSS_FORMATTER_H
#define CSS_FORMATTER_H

#include "css_parser.h"
#include "css_style.h"
#include "../../../lib/mempool.h"
#include "../../../lib/stringbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * CSS Formatter
 *
 * Converts parsed CSS structures back to formatted CSS text.
 * Supports multiple formatting styles: compact, expanded, compressed.
 */

// Formatting options
typedef enum {
    CSS_FORMAT_COMPACT,      // Compact single-line format
    CSS_FORMAT_EXPANDED,     // Standard multi-line format with indentation
    CSS_FORMAT_COMPRESSED,   // Minified format
    CSS_FORMAT_PRETTY        // Pretty-printed with extra spacing
} CssFormatStyle;

typedef struct {
    CssFormatStyle style;
    int indent_size;          // Number of spaces per indent level (default: 2)
    bool use_tabs;            // Use tabs instead of spaces
    bool trailing_semicolon;  // Add semicolon after last declaration
    bool space_before_brace;  // Add space before opening brace
    bool newline_after_brace; // Add newline after opening brace
    bool lowercase_hex;       // Use lowercase for hex colors
    bool quote_urls;          // Quote URLs in url() functions
    bool sort_properties;     // Sort properties alphabetically
} CssFormatOptions;

// Formatter context
typedef struct {
    Pool* pool;
    StringBuf* output;
    CssFormatOptions options;
    int current_indent;
} CssFormatter;

// ============================================================================
// Public API
// ============================================================================

/**
 * Create a CSS formatter with default options
 */
CssFormatter* css_formatter_create(Pool* pool, CssFormatStyle style);

/**
 * Create a CSS formatter with custom options
 */
CssFormatter* css_formatter_create_with_options(Pool* pool, const CssFormatOptions* options);

/**
 * Destroy the formatter (frees output buffer)
 */
void css_formatter_destroy(CssFormatter* formatter);

/**
 * Format a complete stylesheet to string
 * Returns a null-terminated string (owned by formatter, valid until next format call)
 */
const char* css_format_stylesheet(CssFormatter* formatter, CssStylesheet* stylesheet);

/**
 * Format a single rule to string
 */
const char* css_format_rule(CssFormatter* formatter, CssRule* rule);

/**
 * Format a selector group to string
 * (Formats comma-separated selectors like "div, .class, #id")
 */
const char* css_format_selector_group(CssFormatter* formatter, CssSelectorGroup* selector_group);

/**
 * Format a value to string (appends to formatter's output buffer)
 */
void css_format_value(CssFormatter* formatter, CssValue* value);

/**
 * Format a declaration (property: value) to string
 */
const char* css_format_declaration(CssFormatter* formatter, CssPropertyId property_id, CssValue* value);

// ============================================================================
// Convenience functions
// ============================================================================

/**
 * Format stylesheet to string with default compact style
 */
const char* css_stylesheet_to_string(CssStylesheet* stylesheet, Pool* pool);

/**
 * Format stylesheet to string with specific style
 */
const char* css_stylesheet_to_string_styled(CssStylesheet* stylesheet, Pool* pool, CssFormatStyle style);

/**
 * Get default format options for a given style
 */
CssFormatOptions css_get_default_format_options(CssFormatStyle style);

#ifdef __cplusplus
}
#endif

#endif // CSS_FORMATTER_H
