#ifndef DOM_NODE_HPP
#define DOM_NODE_HPP

#include <stdint.h>
#include <stdbool.h>
#include "../../../lib/mempool.h"
#include "../../../lib/strbuf.h"

/**
 * DOM Node Management Module
 * - DomNode provides common base fields and methods
 * - DomElement, DomText, DomComment inherit from DomNode
 */

// Forward declarations
struct DomElement;
struct DomText;
struct DomComment;
typedef struct String String;  // Lambda String type
typedef struct Element Element;  // Lambda Element type

enum DomNodeType {
    DOM_NODE_ELEMENT = 1,     // Element node
    DOM_NODE_TEXT = 3,        // Text node
    DOM_NODE_COMMENT = 8,     // Comment node
    DOM_NODE_DOCUMENT = 9,    // Document node
    DOM_NODE_DOCTYPE = 10     // DOCTYPE declaration
};

/**
 * DomNode - Base struct/class for all DOM nodes
 * Provides common tree structure and node operations
 * Note: This is a plain C++ struct, not a polymorphic class (no virtual methods)
 */
struct DomNode {
    DomNodeType node_type;   // Node type discriminator
    DomNode* parent;         // Parent node (nullptr at root)
    DomNode* first_child;    // First child node (nullptr if no children)
    DomNode* next_sibling;   // Next sibling node (nullptr if last)
    DomNode* prev_sibling;   // Previous sibling (nullptr if first)

    const char* name() const;
    inline DomNodeType type() const { return node_type; }

    // type checking helpers
    inline bool is_element() const { return node_type == DOM_NODE_ELEMENT; }
    inline bool is_text() const { return node_type == DOM_NODE_TEXT; }
    inline bool is_comment() const { return node_type == DOM_NODE_COMMENT || node_type == DOM_NODE_DOCTYPE; }

    // safe downcasting helpers (implementations in dom_element.hpp after derived types are defined)
    inline DomElement* as_element();
    inline DomText* as_text();
    inline DomComment* as_comment();
    inline const DomElement* as_element() const;
    inline const DomText* as_text() const;
    inline const DomComment* as_comment() const;

    // convenience methods for common operations (implementations in dom_element.hpp)
    inline uintptr_t tag() const;                                    // Get tag ID for element nodes
    inline unsigned char* text_data() const;                         // Get text content for text nodes
    inline const char* get_attribute(const char* attr_name) const;   // Get attribute for element nodes

    // tree manipulation methods (implementations in dom_node.cpp)
    bool append_child(DomNode* child);
    bool remove_child(DomNode* child);
    bool insert_before(DomNode* new_node, DomNode* ref_node);

    // utility methods (implementations in dom_node.cpp)
    void print(StrBuf* buf = nullptr, int indent = 0) const;
    void free_tree();

    // static helper for tag name to ID conversion
    static uintptr_t tag_name_to_id(const char* tag_name);

protected:
    // Constructor (only callable by derived classes)
    DomNode(DomNodeType type) : node_type(type), parent(nullptr), first_child(nullptr),
        next_sibling(nullptr), prev_sibling(nullptr) {}
};

// ============================================================================
// DOM Text Node API
// ============================================================================

/**
 * Create a new DomText node (unbacked - standalone copy)
 * @param pool Memory pool for allocations
 * @param text Text content (will be copied)
 * @return New DomText or NULL on failure
 * @deprecated Use dom_text_create_backed for Lambda-backed text nodes
 */
DomText* dom_text_create(Pool* pool, const char* text);

/**
 * Create a new DomText node backed by Lambda String (bidirectional sync)
 * @param pool Memory pool for allocations
 * @param native_string Pointer to Lambda String (will be referenced, not copied)
 * @param parent_element Parent DomElement (for MarkEditor operations)
 * @param child_index Index in parent's native_element->items array
 * @return New DomText or NULL on failure
 */
DomText* dom_text_create_backed(Pool* pool, String* native_string, 
                                 DomElement* parent_element, int64_t child_index);

/**
 * Destroy a DomText node
 * @param text_node Text node to destroy
 */
void dom_text_destroy(DomText* text_node);

/**
 * Get text content
 * @param text_node Text node
 * @return Text content string
 */
const char* dom_text_get_content(DomText* text_node);

/**
 * Set text content (unbacked - updates local copy only)
 * @param text_node Text node
 * @param text New text content (will be copied)
 * @return true on success, false on failure
 * @deprecated Use dom_text_set_content_backed for Lambda-backed text nodes
 */
bool dom_text_set_content(DomText* text_node, const char* text);

/**
 * Set text content (backed - syncs with Lambda String)
 * Updates both DomText and underlying Lambda String via MarkEditor
 * @param text_node Text node (must be backed)
 * @param text New text content
 * @return true on success, false on failure
 */
bool dom_text_set_content_backed(DomText* text_node, const char* text);

/**
 * Check if text node is backed by Lambda String
 * @param text_node Text node
 * @return true if backed, false if standalone copy
 */
bool dom_text_is_backed(DomText* text_node);

/**
 * Get child index of text node in parent's Lambda Element
 * Validates cached index and rescans if necessary
 * @param text_node Text node (must be backed)
 * @return Child index or -1 on error
 */
int64_t dom_text_get_child_index(DomText* text_node);

/**
 * Remove text node from parent (backed - syncs with Lambda)
 * Removes from both DOM tree and Lambda Element's children array
 * @param text_node Text node to remove (must be backed)
 * @return true on success, false on failure
 */
bool dom_text_remove_backed(DomText* text_node);

// ============================================================================
// DOM Comment/DOCTYPE Node API
// ============================================================================

/**
 * Create a new DomComment node
 * @param pool Memory pool for allocations
 * @param node_type Type of node (DOM_NODE_COMMENT, DOM_NODE_DOCTYPE, etc.)
 * @param tag_name Node name (e.g., "!--", "!DOCTYPE")
 * @param content Content/text (will be copied)
 * @return New DomComment or NULL on failure
 */
DomComment* dom_comment_create(Pool* pool, DomNodeType node_type, const char* tag_name, const char* content);

/**
 * Destroy a DomComment node
 * @param comment_node Comment/DOCTYPE node to destroy
 */
void dom_comment_destroy(DomComment* comment_node);

/**
 * Create a backed DomComment node (connected to Lambda Element)
 * @param pool Memory pool for allocations
 * @param native_element Lambda Element with tag "!--" or "!DOCTYPE"
 * @param parent_element Parent DomElement
 * @param child_index Index in parent's native_element->items array
 * @return New backed DomComment or NULL on failure
 */
DomComment* dom_comment_create_backed(Pool* pool, Element* native_element, 
                                     DomElement* parent_element, int64_t child_index);

/**
 * Set comment content (backed version - updates Lambda Element)
 * @param comment_node Comment node to update
 * @param new_content New content string
 * @return true on success, false on failure
 */
bool dom_comment_set_content_backed(DomComment* comment_node, const char* new_content);

/**
 * Append a backed comment to a parent element (updates Lambda tree)
 * @param parent Parent DomElement
 * @param comment_content Comment content string
 * @return New DomComment or NULL on failure
 */
DomComment* dom_element_append_comment_backed(DomElement* parent, const char* comment_content);

/**
 * Remove a backed comment node (updates Lambda tree)
 * @param comment_node Comment node to remove
 * @return true on success, false on failure
 */
bool dom_comment_remove_backed(DomComment* comment_node);

/**
 * Check if comment is backed by Lambda Element
 * @param comment_node Comment node to check
 * @return true if backed, false otherwise
 */
bool dom_comment_is_backed(DomComment* comment_node);

/**
 * Get comment/DOCTYPE content
 * @param comment_node Comment node
 * @return Content string
 */
const char* dom_comment_get_content(DomComment* comment_node);

#endif // DOM_NODE_HPP
