#ifndef DOM_NODE_HPP
#define DOM_NODE_HPP

#include <stdint.h>
#include <stdbool.h>
#include "../../../lib/mempool.h"

/**
 * DOM Node Utilities for C++
 *
 * This module provides the base class for DOM nodes.
 * DomNodeBase is the abstract base class for DomElement, DomText, and DomComment.
 *
 * Design Pattern: C++ inheritance
 * - DomNodeBase provides common base fields and virtual methods
 * - DomElement, DomText, DomComment inherit from DomNodeBase
 * - Type discrimination via virtual methods and runtime type checking
 * - Safe operations via base class interface
 */

// Forward declarations of derived types (defined in dom_element.hpp)
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
// DOM Node Base Class
// ============================================================================

/**
 * DomNode - Base struct for all DOM nodes
 * Provides common tree structure and node operations
 * Note: This is a plain C++ struct, not a polymorphic class (no virtual methods)
 */
struct DomNode {
    DomNodeType node_type;       // Node type discriminator
    DomNode* parent;         // Parent node (nullptr at root)
    DomNode* first_child;    // First child node (nullptr if no children)
    DomNode* next_sibling;   // Next sibling node (nullptr if last)
    DomNode* prev_sibling;   // Previous sibling (nullptr if first)

    // Concrete methods (implementations in dom_node.cpp)
    const char* get_name() const;
    DomNodeType get_type() const { return node_type; }

    // Simplified API - inline wrappers for cleaner code
    inline const char* name() const { return get_name(); }
    inline DomNodeType type() const { return get_type(); }

    // Type checking helpers
    inline bool is_element() const { return node_type == DOM_NODE_ELEMENT; }
    inline bool is_text() const { return node_type == DOM_NODE_TEXT; }
    inline bool is_comment() const { return node_type == DOM_NODE_COMMENT || node_type == DOM_NODE_DOCTYPE; }

    // Safe downcasting helpers (implementations in dom_element.hpp after derived types are defined)
    inline DomElement* as_element();
    inline DomText* as_text();
    inline DomComment* as_comment();
    inline const DomElement* as_element() const;
    inline const DomText* as_text() const;
    inline const DomComment* as_comment() const;

    // Convenience methods for common operations (implementations in dom_element.hpp)
    inline uintptr_t tag() const;                                    // Get tag ID for element nodes
    inline unsigned char* text_data() const;                         // Get text content for text nodes
    inline const char* get_attribute(const char* attr_name) const;   // Get attribute for element nodes

    // Static helper for tag name to ID conversion
    static uintptr_t tag_name_to_id(const char* tag_name);

protected:
    // Constructor (only callable by derived classes)
    DomNode(DomNodeType type) : node_type(type), parent(nullptr), first_child(nullptr),
                                     next_sibling(nullptr), prev_sibling(nullptr) {}
};

// ============================================================================
// Type Checking and Casting
// ============================================================================

// Note: With C++ inheritance through DomNodeBase, most type checking and casting
// can now be done directly through the base class methods:
//   node->is_element(), node->is_text(), node->is_comment()
//   node->as_element(), node->as_text(), node->as_comment()
//   node->get_type(), node->get_name()
//
// The functions below provide C-style API compatibility for code that still
// uses the procedural style.

/**
 * Get node type from any DOM node pointer
 * @param node Node pointer (DomNodeBase*)
 * @return Node type or 0 if NULL
 */
static inline DomNodeType dom_node_get_type(const DomNode* node) {
    return node ? node->get_type() : (DomNodeType)0;
}

/**
 * Check if a node is an element
 * @param node Node pointer
 * @return true if node is DomElement
 */
static inline bool dom_node_is_element(const DomNode* node) {
    return node ? node->is_element() : false;
}

/**
 * Check if a node is a text node
 * @param node Node pointer
 * @return true if node is DomText
 */
static inline bool dom_node_is_text(const DomNode* node) {
    return node ? node->is_text() : false;
}

/**
 * Check if a node is a comment or DOCTYPE node
 * @param node Node pointer
 * @return true if node is DomComment or DOCTYPE
 */
static inline bool dom_node_is_comment(const DomNode* node) {
    return node ? node->is_comment() : false;
}

/**
 * Safe downcast to DomElement with runtime type check
 * @param node Base node pointer
 * @return DomElement pointer or NULL if wrong type
 */
static inline DomElement* dom_node_as_element(DomNode* node) {
    return node ? node->as_element() : nullptr;
}

/**
 * Safe downcast to DomText with runtime type check
 * @param node Base node pointer
 * @return DomText pointer or NULL if wrong type
 */
static inline DomText* dom_node_as_text(DomNode* node) {
    return node ? node->as_text() : nullptr;
}

/**
 * Safe downcast to DomComment with runtime type check
 * @param node Base node pointer
 * @return DomComment pointer or NULL if wrong type
 */
static inline DomComment* dom_node_as_comment(DomNode* node) {
    return node ? node->as_comment() : nullptr;
}

// ============================================================================
// Tree Navigation
// ============================================================================

/**
 * Get node name (element tag, "#text", "#comment", etc.)
 * @param node Node pointer
 * @return Node name string or "#null" if NULL
 */
const char* dom_node_get_name(const DomNode* node);

/**
 * Get tag name from element nodes
 * @param node Node pointer (must be DOM_NODE_ELEMENT)
 * @return Tag name string or NULL if not an element
 */
const char* dom_node_get_tag_name(DomNode* node);

/**
 * Get text content from text nodes
 * @param node Node pointer (must be DOM_NODE_TEXT)
 * @return Text content string or NULL if not a text node
 */
const char* dom_node_get_text(DomNode* node);

/**
 * Get comment content from comment nodes
 * @param node Node pointer (must be DOM_NODE_COMMENT)
 * @return Comment content string or NULL if not a comment
 */
const char* dom_node_get_comment_content(DomNode* node);

/**
 * Get parent node
 * @param node Node pointer
 * @return Parent node or NULL if none
 */
static inline DomNode* dom_node_get_parent(const DomNode* node) {
    return node ? node->parent : nullptr;
}

/**
 * Get first child node
 * @param node Node pointer
 * @return First child or NULL if none
 */
static inline DomNode* dom_node_first_child(DomNode* node) {
    return node ? node->first_child : nullptr;
}

/**
 * Get next sibling node
 * @param node Node pointer
 * @return Next sibling or NULL if none
 */
static inline DomNode* dom_node_next_sibling(DomNode* node) {
    return node ? node->next_sibling : nullptr;
}

/**
 * Get previous sibling node
 * @param node Node pointer
 * @return Previous sibling or NULL if none
 */
static inline DomNode* dom_node_prev_sibling(DomNode* node) {
    return node ? node->prev_sibling : nullptr;
}

/**
 * Get first child node (const version)
 * @param node Node pointer
 * @return First child or NULL if none
 */
static inline const DomNode* dom_node_first_child_const(const DomNode* node) {
    return node ? node->first_child : nullptr;
}

/**
 * Get next sibling node (const version)
 * @param node Node pointer
 * @return Next sibling or NULL if none
 */
static inline const DomNode* dom_node_next_sibling_const(const DomNode* node) {
    return node ? node->next_sibling : nullptr;
}

// ============================================================================
// Tree Manipulation
// ============================================================================

/**
 * Append a child node to a parent
 * @param parent Parent node (must be an element)
 * @param child Child node to append
 * @return true on success, false on failure
 */
bool dom_node_append_child(DomNode* parent, DomNode* child);

/**
 * Remove a child node from its parent
 * @param parent Parent node
 * @param child Child node to remove
 * @return true on success, false if child not found
 */
bool dom_node_remove_child(DomNode* parent, DomNode* child);

/**
 * Insert a node before a reference node
 * @param parent Parent node
 * @param new_node Node to insert
 * @param ref_node Reference node (insert before this)
 * @return true on success, false on failure
 */
bool dom_node_insert_before(DomNode* parent, DomNode* new_node, DomNode* ref_node);

// ============================================================================
// Debugging and Utilities
// ============================================================================

/**
 * Print node information for debugging
 * @param node Node to print
 * @param indent Indentation level
 */
void dom_node_print(const DomNode* node, int indent);

/**
 * Recursively free a DOM tree
 * Note: Nodes are pool-allocated, so this only updates structure, not memory
 * @param node Root node of tree to free
 */
void dom_node_free_tree(DomNode* node);

// ============================================================================
// Attribute Access
// ============================================================================

/**
 * Get attribute value from element node
 * @param node Node pointer (must be DOM_NODE_ELEMENT)
 * @param attr_name Attribute name
 * @return Attribute value or NULL if attribute not found or node is not an element
 */
const char* dom_node_get_attribute(DomNode* node, const char* attr_name);

#endif // DOM_NODE_HPP
