/**
 * @file input-html-scan.h
 * @brief Low-level HTML scanning and tokenization helpers
 */

#ifndef INPUT_HTML_SCAN_H
#define INPUT_HTML_SCAN_H

#include "../../lib/string.h"
#include "../../lib/stringbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Convert string to lowercase in-place
 * @param str String to convert
 */
void html_to_lowercase(char* str);

/**
 * Parse string content until end_char is found, handling HTML entities
 * All entities (including named ones) are decoded to UTF-8.
 * Used for attribute values where Symbol handling is not needed.
 * @param sb StringBuf to accumulate characters
 * @param html Pointer to HTML string pointer (will be advanced)
 * @param end_char Character that terminates the string
 * @return Lambda String with decoded entities
 */
String* html_parse_string_content(StringBuf* sb, const char **html, char end_char);

/**
 * Parse an HTML attribute value (quoted or unquoted)
 * @param sb StringBuf for temporary storage
 * @param html Pointer to HTML string pointer (will be advanced)
 * @param html_start Start of HTML document (for error reporting)
 * @return Lambda String with attribute value
 */
String* html_parse_attribute_value(StringBuf* sb, const char **html, const char *html_start);

/**
 * Parse an HTML tag name (converts to lowercase)
 * @param sb StringBuf for temporary storage
 * @param html Pointer to HTML string pointer (will be advanced)
 * @return Lambda String with tag name in lowercase
 */
String* html_parse_tag_name(StringBuf* sb, const char **html);

#ifdef __cplusplus
}
#endif

// C++ only: Mixed content parsing with Symbol support
#ifdef __cplusplus

#include "../lambda-data.hpp"

// Forward declaration
class MarkBuilder;

/**
 * Callback for emitting items during mixed content parsing
 * @param item The item to emit (String or Symbol)
 * @param user_data User context passed to html_parse_mixed_content
 */
typedef void (*HtmlMixedContentCallback)(Item item, void* user_data);

/**
 * Parse text content with Symbol support for named entities
 *
 * This function parses HTML text content and:
 * - Decodes ASCII escapes (&lt; &gt; &amp; &quot; &apos;) inline to characters
 * - Decodes numeric references (&#123; &#x1F;) inline to UTF-8
 * - Emits named entities (&copy; &mdash; etc.) as Symbol items
 * - Regular text is accumulated and emitted as String items
 *
 * @param builder MarkBuilder for creating String/Symbol items
 * @param sb StringBuf for temporary text accumulation
 * @param html Pointer to HTML string pointer (will be advanced)
 * @param end_char Character that terminates the content
 * @param callback Function to call for each String/Symbol item
 * @param user_data Context passed to callback
 */
void html_parse_mixed_content(
    MarkBuilder& builder,
    StringBuf* sb,
    const char **html,
    char end_char,
    HtmlMixedContentCallback callback,
    void* user_data
);

#endif // __cplusplus

#endif // INPUT_HTML_SCAN_H
