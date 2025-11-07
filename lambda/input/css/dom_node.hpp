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

    const char* name() const;
    inline DomNodeType type() const { return node_type; }

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

    // Tree manipulation methods (implementations in dom_node.cpp)
    bool append_child(DomNode* child);
    bool remove_child(DomNode* child);
    bool insert_before(DomNode* new_node, DomNode* ref_node);

    // Utility methods (implementations in dom_node.cpp)
    void print(int indent = 0) const;
    void free_tree();

    // Static helper for tag name to ID conversion
    static uintptr_t tag_name_to_id(const char* tag_name);

protected:
    // Constructor (only callable by derived classes)
    DomNode(DomNodeType type) : node_type(type), parent(nullptr), first_child(nullptr),
                                     next_sibling(nullptr), prev_sibling(nullptr) {}
};

#endif // DOM_NODE_HPP
