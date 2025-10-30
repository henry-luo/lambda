/**
 * @file input-html-tree.h
 * @brief HTML tree construction and manipulation functions
 *
 * This module provides functions for building and manipulating the HTML DOM tree
 * during parsing, separated from the low-level tokenization logic.
 */

#ifndef INPUT_HTML_TREE_H
#define INPUT_HTML_TREE_H

#include "../lambda-data.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct Input Input;
typedef struct Element Element;

/**
 * Append a child item to an HTML element
 * Handles both element and text node children
 * @param parent Parent element to append to
 * @param child Child item (element or text string)
 */
void html_append_child(Element* parent, Item child);

/**
 * Get the current depth of parsing (for recursion safety)
 * @return Current parse depth
 */
int html_get_parse_depth(void);

/**
 * Increment parse depth (call at start of element parsing)
 */
void html_enter_element(void);

/**
 * Decrement parse depth (call at end of element parsing)
 */
void html_exit_element(void);

/**
 * Reset parse depth to zero (for new parse operations)
 */
void html_reset_parse_depth(void);

/**
 * Set the content length of an element based on its children
 * @param element Element to update
 */
void html_set_content_length(Element* element);

#ifdef __cplusplus
}
#endif

#endif // INPUT_HTML_TREE_H
