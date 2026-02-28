/**
 * html-defs.h — shared HTML tag/attribute classification
 *
 * Sorted arrays with binary search for O(log n) lookups.
 * Used by format-html.cpp and html5_tree_builder.cpp.
 */
#ifndef HTML_DEFS_H
#define HTML_DEFS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Void elements (self-closing, no end tag). */
bool html_is_void_element(const char* tag, size_t len);

/** Raw text elements (content not parsed as HTML). */
bool html_is_raw_text_element(const char* tag, size_t len);

/** Boolean attributes (value may be omitted). */
bool html_is_boolean_attribute(const char* attr, size_t len);

/** Block-level elements. */
bool html_is_block_element(const char* tag, size_t len);

/** Heading elements (h1-h6). */
bool html_is_heading(const char* tag, size_t len);

/** Heading level (1-6), or 0 if not a heading. */
int  html_heading_level(const char* tag, size_t len);

#ifdef __cplusplus
}
#endif

#endif // HTML_DEFS_H
