#ifndef DOM_NODE_HPP
#define DOM_NODE_HPP

#include <stdint.h>
#include <stdbool.h>
#include "../../../lib/mempool.h"

/**
 * DOM Node Utilities for C++
 *
 * This module provides utility functions for working with DOM nodes.
 * DomElement, DomText, and DomComment are the actual node types, and they
 * share a common initial layout (node_type field first) which enables
 * polymorphic operations.
 *
 * Design Pattern: C++ struct inheritance via common initial sequence
 * - DomElement, DomText, DomComment all start with DomNodeType node_type
 * - Type discrimination via the first field
 * - Safe operations via helper functions with runtime checks
 */

// Forward declarations
struct DomElement;
struct DomText;
struct DomComment;

// ============================================================================
// DOM Node Types
// ============================================================================

enum DomNodeType {
    DOM_NODE_ELEMENT = 1,     // Element node
    DOM_NODE_TEXT = 3,        // Text node
    DOM_NODE_COMMENT = 8,     // Comment node
    DOM_NODE_DOCUMENT = 9,    // Document node
    DOM_NODE_DOCTYPE = 10     // DOCTYPE declaration
};

// ============================================================================
// Type Checking and Casting
// ============================================================================

/**
 * Get node type from any DOM node pointer
 * @param node Node pointer (can be DomElement*, DomText*, or DomComment*)
 * @return Node type or 0 if NULL
 */
static inline DomNodeType dom_node_get_type(const void* node) {
    if (!node) return (DomNodeType)0;
    // All node types have node_type as first field, safe to cast to any type
    return *((const DomNodeType*)node);
}

/**
 * Check if a node is an element
 * @param node Node pointer
 * @return true if node is DomElement
 */
static inline bool dom_node_is_element(const void* node) {
    return dom_node_get_type(node) == DOM_NODE_ELEMENT;
}

/**
 * Check if a node is a text node
 * @param node Node pointer
 * @return true if node is DomText
 */
static inline bool dom_node_is_text(const void* node) {
    return dom_node_get_type(node) == DOM_NODE_TEXT;
}

/**
 * Check if a node is a comment or DOCTYPE node
 * @param node Node pointer
 * @return true if node is DomComment or DOCTYPE
 */
static inline bool dom_node_is_comment(const void* node) {
    DomNodeType type = dom_node_get_type(node);
    return type == DOM_NODE_COMMENT || type == DOM_NODE_DOCTYPE;
}

/**
 * Safe downcast to DomElement with runtime type check
 * @param node Base node pointer
 * @return DomElement pointer or NULL if wrong type
 */
DomElement* dom_node_as_element(void* node);

/**
 * Safe downcast to DomText with runtime type check
 * @param node Base node pointer
 * @return DomText pointer or NULL if wrong type
 */
DomText* dom_node_as_text(void* node);

/**
 * Safe downcast to DomComment with runtime type check
 * @param node Base node pointer
 * @return DomComment pointer or NULL if wrong type
 */
DomComment* dom_node_as_comment(void* node);

// ============================================================================
// Tree Navigation
// ============================================================================

/**
 * Get node name (element tag, "#text", "#comment", etc.)
 * @param node Node pointer
 * @return Node name string or "#null" if NULL
 */
const char* dom_node_get_name(const void* node);

/**
 * Get parent node
 * @param node Node pointer
 * @return Parent node or NULL if none
 */
void* dom_node_get_parent(const void* node);

/**
 * Get first child node
 * @param node Node pointer
 * @return First child or NULL if none
 */
void* dom_node_first_child(void* node);

/**
 * Get next sibling node
 * @param node Node pointer
 * @return Next sibling or NULL if none
 */
void* dom_node_next_sibling(void* node);

/**
 * Get previous sibling node
 * @param node Node pointer
 * @return Previous sibling or NULL if none
 */
void* dom_node_prev_sibling(void* node);

/**
 * Get first child node (const version)
 * @param node Node pointer
 * @return First child or NULL if none
 */
const void* dom_node_first_child_const(const void* node);

/**
 * Get next sibling node (const version)
 * @param node Node pointer
 * @return Next sibling or NULL if none
 */
const void* dom_node_next_sibling_const(const void* node);

// ============================================================================
// Tree Manipulation
// ============================================================================

/**
 * Append a child node to a parent
 * @param parent Parent node (must be an element)
 * @param child Child node to append
 * @return true on success, false on failure
 */
bool dom_node_append_child(void* parent, void* child);

/**
 * Remove a child node from its parent
 * @param parent Parent node
 * @param child Child node to remove
 * @return true on success, false if child not found
 */
bool dom_node_remove_child(void* parent, void* child);

/**
 * Insert a node before a reference node
 * @param parent Parent node
 * @param new_node Node to insert
 * @param ref_node Reference node (insert before this)
 * @return true on success, false on failure
 */
bool dom_node_insert_before(void* parent, void* new_node, void* ref_node);

// ============================================================================
// Debugging and Utilities
// ============================================================================

/**
 * Print node information for debugging
 * @param node Node to print
 * @param indent Indentation level
 */
void dom_node_print(const void* node, int indent);

/**
 * Recursively free a DOM tree
 * Note: Nodes are pool-allocated, so this only updates structure, not memory
 * @param node Root node of tree to free
 */
void dom_node_free_tree(void* node);

#endif // DOM_NODE_HPP
