/**
 * @file input-html-context.h
 * @brief HTML parser context for tracking document structure and insertion modes
 *
 * This module provides context tracking for HTML5-compliant parsing, including
 * automatic insertion of implicit elements (<html>, <head>, <body>) and state
 * management for proper element placement.
 */

#ifndef INPUT_HTML_CONTEXT_H
#define INPUT_HTML_CONTEXT_H

#include "../lambda-data.hpp"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct Input Input;
typedef struct Element Element;

/**
 * HTML parser context structure
 * Tracks document structure and parser state during HTML parsing
 */
typedef struct HtmlParserContext {
    // Implicit document structure elements (may be NULL if not yet created)
    Element* html_element;      // The <html> root element
    Element* head_element;      // The <head> element
    Element* body_element;      // The <body> element

    // Current insertion point for new elements
    Element* current_node;

    // Flags for tracking document state
    bool has_explicit_html;     // Did the source contain <html>?
    bool has_explicit_head;     // Did the source contain <head>?
    bool has_explicit_body;     // Did the source contain <body>?
    bool in_head;               // Currently parsing head content?
    bool head_closed;           // Has </head> been seen?
    bool in_body;               // Currently parsing body content?

    // Reference to the input for element creation
    Input* input;
} HtmlParserContext;

/**
 * Create a new HTML parser context
 * @param input The input structure for element creation
 * @return Newly allocated parser context
 */
HtmlParserContext* html_context_create(Input* input);

/**
 * Destroy an HTML parser context
 * @param ctx The context to destroy
 */
void html_context_destroy(HtmlParserContext* ctx);

/**
 * Ensure <html> element exists, creating it if necessary
 * @param ctx Parser context
 * @return The html element (existing or newly created)
 */
Element* html_context_ensure_html(HtmlParserContext* ctx);

/**
 * Ensure <head> element exists, creating it if necessary
 * Will also ensure <html> exists first
 * @param ctx Parser context
 * @return The head element (existing or newly created)
 */
Element* html_context_ensure_head(HtmlParserContext* ctx);

/**
 * Ensure <body> element exists, creating it if necessary
 * Will also ensure <html> exists first
 * @param ctx Parser context
 * @return The body element (existing or newly created)
 */
Element* html_context_ensure_body(HtmlParserContext* ctx);

/**
 * Get the appropriate parent element for inserting content
 * Automatically creates implicit elements as needed
 * @param ctx Parser context
 * @param tag_name The tag being inserted (used to determine placement)
 * @return The element where content should be inserted
 */
Element* html_context_get_insertion_point(HtmlParserContext* ctx, const char* tag_name);

/**
 * Mark that an explicit <html> tag was seen
 * @param ctx Parser context
 * @param element The html element from the source
 */
void html_context_set_html(HtmlParserContext* ctx, Element* element);

/**
 * Mark that an explicit <head> tag was seen
 * @param ctx Parser context
 * @param element The head element from the source
 */
void html_context_set_head(HtmlParserContext* ctx, Element* element);

/**
 * Mark that an explicit <body> tag was seen
 * @param ctx Parser context
 * @param element The body element from the source
 */
void html_context_set_body(HtmlParserContext* ctx, Element* element);

/**
 * Close the head section (transition to body)
 * @param ctx Parser context
 */
void html_context_close_head(HtmlParserContext* ctx);

#ifdef __cplusplus
}
#endif

#endif // INPUT_HTML_CONTEXT_H
