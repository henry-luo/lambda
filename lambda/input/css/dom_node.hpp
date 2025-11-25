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

typedef enum {
    RDT_VIEW_NONE = 0,
    RDT_VIEW_TEXT,
    RDT_VIEW_BR,
    // ViewSpan
    RDT_VIEW_INLINE,
    // ViewBlock
    RDT_VIEW_INLINE_BLOCK,
    RDT_VIEW_BLOCK,
    RDT_VIEW_LIST_ITEM,
    RDT_VIEW_TABLE,
    RDT_VIEW_TABLE_ROW_GROUP,
    RDT_VIEW_TABLE_ROW,
    RDT_VIEW_TABLE_CELL,
} ViewType;

typedef struct DomNode DomNode;
typedef DomNode View;
typedef struct ViewGroup ViewGroup;

/**
 * DomNode - Base struct/class for all DOM nodes
 * Provides common tree structure and node operations
 * Note: This is a plain C++ struct, not a polymorphic class (no virtual methods)
 */
struct DomNode {
    DomNodeType node_type;   // Node type discriminator
    DomNode* parent;         // Parent node (nullptr at root)
    DomNode* next_sibling;   // Next sibling node (nullptr if last)
    DomNode* prev_sibling;   // Previous sibling (nullptr if first)

    // view related fields
    ViewType view_type;
    // view always has x, y, wd, hg;  otherwise, it is a property group
    float x, y, width, height;  // (x, y) relative to the BORDER box of parent block, and (width, height) forms the BORDER box of current block

    // node_name() is for all nodes, including text and comment nodes
    // whereas tag_name is only for element nodes
    const char* node_name() const;

    // type checking helpers
    inline bool is_element() const { return node_type == DOM_NODE_ELEMENT; }
    inline bool is_text() const { return node_type == DOM_NODE_TEXT; }
    inline bool is_comment() const { return node_type == DOM_NODE_COMMENT || node_type == DOM_NODE_DOCTYPE; }

    // safe downcasting helpers (implementations in dom_element.hpp after derived types are defined)
    inline DomElement* as_element() {
        return is_element() ? ((DomElement*)this) : nullptr;
    }

    inline DomText* as_text() {
        return is_text() ? ((DomText*)this) : nullptr;
    }

    inline DomComment* as_comment() {
        return is_comment() ? ((DomComment*)this) : nullptr;
    }

    inline const DomElement* as_element() const {
        return is_element() ? ((const DomElement*)this) : nullptr;
    }

    inline const DomText* as_text() const {
        return is_text() ? ((const DomText*)this) : nullptr;
    }

    inline const DomComment* as_comment() const {
        return is_comment() ? ((const DomComment*)this) : nullptr;
    }    

    // static helper for tag name to ID conversion
    static uintptr_t tag_name_to_id(const char* tag_name);
    uintptr_t tag() const;
    unsigned char* text_data() const;
    // Get attribute for element nodes
    const char* get_attribute(const char* attr_name) const;

    // tree manipulation methods (implementations in dom_node.cpp)
    bool append_child(DomNode* child);
    bool remove_child(DomNode* child);
    bool insert_before(DomNode* new_node, DomNode* ref_node);

    // utility methods (implementations in dom_node.cpp)
    void print(StrBuf* buf = nullptr, int indent = 0) const;
    void free_tree();

    // view related methods =========================================
    inline View* next() { return (View*)next_sibling; }
    inline ViewGroup* parent_view() { return (ViewGroup*)this->parent; }

    inline bool is_group() { return view_type >= RDT_VIEW_INLINE; }

    inline bool is_inline() { 
        return view_type == RDT_VIEW_TEXT || view_type == RDT_VIEW_INLINE || view_type == RDT_VIEW_INLINE_BLOCK; 
    }

    inline bool is_block() {
        return view_type == RDT_VIEW_BLOCK || view_type == RDT_VIEW_INLINE_BLOCK || view_type == RDT_VIEW_LIST_ITEM ||
            view_type == RDT_VIEW_TABLE || view_type == RDT_VIEW_TABLE_ROW_GROUP || view_type == RDT_VIEW_TABLE_ROW || view_type == RDT_VIEW_TABLE_CELL;
    }

    const char* view_name();    

protected:
    // Constructor (only callable by derived classes)
    DomNode(DomNodeType type) : node_type(type), parent(nullptr),
        next_sibling(nullptr), prev_sibling(nullptr), view_type(RDT_VIEW_NONE),
        x(0), y(0), width(0), height(0) {}
};

// ============================================================================
// DOM Text Node API
// ============================================================================

/**
 * Create a new DomText node backed by Lambda String
 * @param native_string Pointer to Lambda String (will be referenced, not copied)
 * @param parent_element Parent DomElement (provides document context)
 * @return New DomText or NULL on failure
 */
DomText* dom_text_create(String* native_string, DomElement* parent_element);

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
 * Set text content (backed - updates Lambda String via MarkEditor)
 * @param text_node Text node
 * @param text New text content
 * @return true on success, false on failure
 */
bool dom_text_set_content(DomText* text_node, const char* text);

/**
 * Check if text node is backed by Lambda String (always true now)
 * @param text_node Text node
 * @return true if backed, false otherwise
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
 * Remove text node from parent (syncs with Lambda)
 * Removes from both DOM tree and Lambda Element's children array
 * @param text_node Text node to remove
 * @return true on success, false on failure
 */
bool dom_text_remove(DomText* text_node);

/**
 * Append a text node to a parent element (updates Lambda tree)
 * @param parent Parent DomElement
 * @param text_content Text content string
 * @return New DomText or NULL on failure
 */
DomText* dom_element_append_text(DomElement* parent, const char* text_content);

// ============================================================================
// DOM Comment/DOCTYPE Node API
// ============================================================================

/**
 * Create a new DomComment node (backed by Lambda Element)
 * @param native_element Lambda Element with tag "!--" or "!DOCTYPE" (required)
 * @param parent_element Parent DomElement (provides document context)
 * @return New backed DomComment or NULL on failure
 */
DomComment* dom_comment_create(Element* native_element, DomElement* parent_element);

/**
 * Destroy a DomComment node
 * @param comment_node Comment/DOCTYPE node to destroy
 */
void dom_comment_destroy(DomComment* comment_node);

/**
 * Get child index of a comment node in parent's items array
 * @param comment_node Comment node to check
 * @return Index in parent's items array, or -1 if not found
 */
int64_t dom_comment_get_child_index(DomComment* comment_node);

/**
 * Set comment content (updates Lambda Element)
 * @param comment_node Comment node to update
 * @param new_content New content string
 * @return true on success, false on failure
 */
bool dom_comment_set_content(DomComment* comment_node, const char* new_content);

/**
 * Append a comment to a parent element (updates Lambda tree)
 * @param parent Parent DomElement
 * @param comment_content Comment content string
 * @return New DomComment or NULL on failure
 */
DomComment* dom_element_append_comment(DomElement* parent, const char* comment_content);

/**
 * Remove a comment node (updates Lambda tree)
 * @param comment_node Comment node to remove
 * @return true on success, false on failure
 */
bool dom_comment_remove(DomComment* comment_node);

/**
 * Check if comment is backed by Lambda Element (always true now)
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
