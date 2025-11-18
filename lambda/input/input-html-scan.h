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

#endif // INPUT_HTML_SCAN_H
