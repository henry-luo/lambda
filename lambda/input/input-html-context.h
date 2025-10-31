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
 * HTML5 Insertion Modes
 * These define the parser's state machine for proper element placement
 */
typedef enum {
    HTML_MODE_INITIAL,           // Before any content
    HTML_MODE_BEFORE_HTML,       // Before <html> element
    HTML_MODE_BEFORE_HEAD,       // After <html>, before <head>
    HTML_MODE_IN_HEAD,           // Inside <head> element
    HTML_MODE_AFTER_HEAD,        // After </head>, before <body>
    HTML_MODE_IN_BODY,           // Inside <body> element (default for content)
    HTML_MODE_AFTER_BODY,        // After </body>
    HTML_MODE_AFTER_AFTER_BODY   // After final content (comments, whitespace only)
} HtmlInsertionMode;

/**
 * Phase 5: Open Element Stack
 * Tracks currently open elements for proper nesting and misnested tag handling
 */
typedef struct HtmlElementStack {
    Element** elements;     // Array of element pointers
    size_t length;          // Number of elements in stack
    size_t capacity;        // Allocated capacity
    Pool* pool;             // Memory pool for allocations
} HtmlElementStack;

/**
 * Phase 6: Active Formatting Elements List
 * Tracks formatting elements (b, i, strong, em, etc.) for reconstruction
 * Used for HTML5 adoption agency algorithm (simplified version)
 */
typedef struct HtmlFormattingElement {
    Element* element;       // The formatting element
    size_t stack_depth;     // Depth in open elements stack when added
} HtmlFormattingElement;

typedef struct HtmlFormattingList {
    HtmlFormattingElement* elements;  // Array of formatting elements
    size_t length;          // Number of elements
    size_t capacity;        // Allocated capacity
    Pool* pool;             // Memory pool for allocations
} HtmlFormattingList;

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

    // Phase 4.2: HTML5 Insertion Mode State Machine
    HtmlInsertionMode insertion_mode;  // Current parser state

    // Phase 5: Open Element Stack
    HtmlElementStack* open_elements;   // Stack of currently open elements

    // Phase 6: Active Formatting Elements List
    HtmlFormattingList* active_formatting;  // List of active formatting elements

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

/**
 * Phase 4.2: Get current insertion mode
 * @param ctx Parser context
 * @return Current insertion mode
 */
HtmlInsertionMode html_context_get_mode(HtmlParserContext* ctx);

/**
 * Phase 4.2: Set insertion mode
 * @param ctx Parser context
 * @param mode New insertion mode
 */
void html_context_set_mode(HtmlParserContext* ctx, HtmlInsertionMode mode);

/**
 * Phase 4.2: Transition insertion mode based on tag being inserted
 * Updates the insertion mode state machine based on element being processed
 * @param ctx Parser context
 * @param tag_name The tag being inserted
 * @param is_closing_tag True if this is a closing tag
 */
void html_context_transition_mode(HtmlParserContext* ctx, const char* tag_name, bool is_closing_tag);

// ============================================================================
// Phase 5: Open Element Stack Functions
// ============================================================================

/**
 * Create a new element stack
 * @param pool Memory pool for allocations
 * @return Newly allocated element stack
 */
HtmlElementStack* html_stack_create(Pool* pool);

/**
 * Destroy an element stack
 * @param stack The stack to destroy
 */
void html_stack_destroy(HtmlElementStack* stack);

/**
 * Push an element onto the stack
 * @param stack The element stack
 * @param element The element to push
 */
void html_stack_push(HtmlElementStack* stack, Element* element);

/**
 * Pop an element from the stack
 * @param stack The element stack
 * @return The popped element, or NULL if stack is empty
 */
Element* html_stack_pop(HtmlElementStack* stack);

/**
 * Peek at the top element without removing it
 * @param stack The element stack
 * @return The top element, or NULL if stack is empty
 */
Element* html_stack_peek(HtmlElementStack* stack);

/**
 * Get element at specific index (0 = bottom, length-1 = top)
 * @param stack The element stack
 * @param index Index from bottom of stack
 * @return Element at index, or NULL if out of bounds
 */
Element* html_stack_get(HtmlElementStack* stack, size_t index);

/**
 * Get the current number of elements in the stack
 * @param stack The element stack
 * @return Number of elements
 */
size_t html_stack_length(HtmlElementStack* stack);

/**
 * Check if the stack is empty
 * @param stack The element stack
 * @return True if empty
 */
bool html_stack_is_empty(HtmlElementStack* stack);

/**
 * Check if stack contains an element with the given tag name
 * @param stack The element stack
 * @param tag_name Tag name to search for
 * @return True if found
 */
bool html_stack_contains(HtmlElementStack* stack, const char* tag_name);

/**
 * Find the index of the most recent element with the given tag name
 * @param stack The element stack
 * @param tag_name Tag name to search for
 * @return Index of element (from bottom), or -1 if not found
 */
int html_stack_find(HtmlElementStack* stack, const char* tag_name);

/**
 * Remove all elements from the stack up to and including the specified element
 * This is used for closing tags - pops elements until the matching opening tag is found
 * @param stack The element stack
 * @param element The element to pop up to
 * @return True if element was found and popped
 */
bool html_stack_pop_until(HtmlElementStack* stack, Element* element);

/**
 * Remove all elements from the stack up to and including the element with the given tag
 * @param stack The element stack
 * @param tag_name Tag name to pop up to
 * @return True if element was found and popped
 */
bool html_stack_pop_until_tag(HtmlElementStack* stack, const char* tag_name);

/**
 * Clear all elements from the stack
 * @param stack The element stack
 */
void html_stack_clear(HtmlElementStack* stack);

/**
 * Get the current insertion point from the stack
 * Returns the top element, or NULL if stack is empty
 * @param stack The element stack
 * @return Current insertion point element
 */
Element* html_stack_current_node(HtmlElementStack* stack);

// ============================================================================
// Phase 6: Active Formatting Elements Functions
// ============================================================================

/**
 * Create a new formatting elements list
 * @param pool Memory pool for allocations
 * @return Newly allocated formatting list
 */
HtmlFormattingList* html_formatting_create(Pool* pool);

/**
 * Destroy a formatting elements list
 * @param list The list to destroy
 */
void html_formatting_destroy(HtmlFormattingList* list);

/**
 * Add a formatting element to the list
 * @param list The formatting list
 * @param element The element to add
 * @param stack_depth Current depth in open elements stack
 */
void html_formatting_push(HtmlFormattingList* list, Element* element, size_t stack_depth);

/**
 * Remove the most recent occurrence of an element from the list
 * @param list The formatting list
 * @param element The element to remove
 * @return True if element was found and removed
 */
bool html_formatting_remove(HtmlFormattingList* list, Element* element);

/**
 * Remove the most recent element with the given tag name
 * @param list The formatting list
 * @param tag_name Tag name to remove
 * @return True if element was found and removed
 */
bool html_formatting_remove_tag(HtmlFormattingList* list, const char* tag_name);

/**
 * Check if the list contains an element with the given tag name
 * @param list The formatting list
 * @param tag_name Tag name to search for
 * @return True if found
 */
bool html_formatting_contains(HtmlFormattingList* list, const char* tag_name);

/**
 * Clear all elements from the formatting list
 * @param list The formatting list
 */
void html_formatting_clear(HtmlFormattingList* list);

/**
 * Get the number of elements in the formatting list
 * @param list The formatting list
 * @return Number of elements
 */
size_t html_formatting_length(HtmlFormattingList* list);

/**
 * Check if a tag is a formatting element
 * @param tag_name Tag name to check
 * @return True if it's a formatting element
 */
bool html_is_formatting_element(const char* tag_name);

// ============================================================================
// Phase 8: Simple Reconstruction for Misnested Formatting
// ============================================================================

/**
 * Reconstruct active formatting elements after a block element opens
 * This handles simple misnesting like <b><p>text</b></p>
 * @param ctx Parser context
 * @param parent Parent element to reconstruct into
 */
void html_reconstruct_formatting(HtmlParserContext* ctx, Element* parent);

// ============================================================================
// Phase 9: Foster Parenting for Table Misnesting
// ============================================================================

/**
 * Check if we're currently in a table context (parsing table structure)
 * @param ctx Parser context
 * @return True if inside a table element
 */
bool html_is_in_table_context(HtmlParserContext* ctx);

/**
 * Find the appropriate foster parent for content that appears in wrong table location
 * @param ctx Parser context
 * @return The element to use as foster parent (usually before the table)
 */
Element* html_find_foster_parent(HtmlParserContext* ctx);

/**
 * Check if an element is a table-related element
 * @param tag_name Tag name to check
 * @return True if it's a table element (table, tbody, thead, tfoot, tr, td, th, etc.)
 */
bool html_is_table_element(const char* tag_name);

#ifdef __cplusplus
}
#endif

#endif // INPUT_HTML_CONTEXT_H
