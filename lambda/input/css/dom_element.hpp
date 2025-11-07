#ifndef DOM_ELEMENT_H
#define DOM_ELEMENT_H

#include "../../../lib/avl_tree.h"
#include "../../../lib/mempool.h"
#include "../../../lib/strbuf.h"
#include "css_style.hpp"
#include "css_style_node.hpp"
#include "dom_node.hpp"  // Provides DomNodeType enum and utility functions
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DOM Element Extension for CSS Styling
 *
 * This module extends Lambda's Element structure to support AVL tree-based
 * CSS style management. It provides efficient style resolution, cascade
 * computation, and caching for high-performance rendering.
 *
 * Integration with Radiant:
 * - Uses C++ inheritance for polymorphic node operations
 * - Provides CSS style lookup through AVL trees (O(log n))
 * - Caches computed values for performance
 * - Supports dynamic style updates
 *
 * Note: DomNodeBase is the base class for DomElement, DomText, and DomComment,
 * providing polymorphic DOM tree operations.
 */

// Forward declarations
typedef struct Element Element;
typedef struct DocumentStyler DocumentStyler;

// Note: DomNodeBase is defined in dom_node.hpp and included above

// ============================================================================
// DOM Text Node
// ============================================================================

/**
 * DomText - Text node in DOM tree
 * Represents text content between elements
 */
struct DomText : public DomNodeBase {
    // Text-specific fields
    const char* text;            // Text content
    size_t length;               // Text length

    // Memory management
    Pool* pool;                  // Memory pool for allocations

    // Constructor
    DomText() : DomNodeBase(DOM_NODE_TEXT), text(nullptr), length(0), pool(nullptr) {}

    // Implement virtual methods
    const char* get_name() const override { return "#text"; }
};

// ============================================================================
// DOM Comment/DOCTYPE Node
// ============================================================================

/**
 * DomComment - Comment, DOCTYPE, or XML declaration node
 * Represents comments (<!-- -->), DOCTYPE declarations, and XML declarations
 */
struct DomComment : public DomNodeBase {
    // Comment-specific fields
    const char* tag_name;        // Node name: "!--" for comments, "!DOCTYPE" for DOCTYPE
    const char* content;         // Full content/text
    size_t length;               // Content length

    // Memory management
    Pool* pool;                  // Memory pool for allocations

    // Constructor
    DomComment(DomNodeType type = DOM_NODE_COMMENT) : DomNodeBase(type), tag_name(nullptr),
                                                       content(nullptr), length(0), pool(nullptr) {}

    // Implement virtual methods
    const char* get_name() const override { return tag_name ? tag_name : "#comment"; }
};

// ============================================================================
// Attribute Storage (Hybrid Array/Tree)
// ============================================================================

/**
 * Attribute pair for storage
 */
typedef struct AttributePair {
    const char* name;   // Attribute name
    const char* value;  // Attribute value
} AttributePair;

/**
 * Hybrid attribute storage
 * Uses array for small attribute counts (< 10), switches to HashMap for larger counts
 * This provides O(n) for common cases (n<10) and O(1) for SVG/data-heavy elements
 */
typedef struct AttributeStorage {
    int count;             // Number of attributes
    bool use_hashmap;      // Whether using hashmap (true) or array (false)
    Pool* pool;            // Memory pool

    union {
        AttributePair* array;     // Array storage for count < 10
        struct hashmap* hashmap;  // HashMap storage for count >= 10
    } storage;
} AttributeStorage;

// Threshold for switching from array to hashmap
#define ATTRIBUTE_HASHMAP_THRESHOLD 10

// ============================================================================
// DOM Element with Style Support
// ============================================================================

/**
 * DomElement - DOM element with integrated CSS styling
 *
 * This structure extends the basic DOM element concept with:
 * - Specified style tree (AVL tree of CSS declarations from rules)
 * - Computed style tree (AVL tree of resolved CSS values)
 * - Version tracking for cache invalidation
 * - Parent/child relationships for inheritance
 */
struct DomElement : public DomNodeBase {
    // Basic element information
    Element* native_element;     // Pointer to native Lambda Element
    const char* tag_name;        // Element tag name (cached string)
    void* tag_name_ptr;          // Tag name pointer from name_pool (for fast comparison)
    uintptr_t tag_id;            // Tag ID for fast comparison (e.g., LXB_TAG_DIV)
    const char* id;              // Element ID attribute (cached)
    const char** class_names;    // Array of class names (cached)
    int class_count;             // Number of classes

    // Style trees
    StyleTree* specified_style;  // Specified values from CSS rules (AVL tree)
    StyleTree* computed_style;   // Computed values (AVL tree, cached)

    // Version tracking for cache invalidation
    uint32_t style_version;      // Incremented when specified styles change
    bool needs_style_recompute;  // Flag indicating computed values are stale

    // Attribute access (for selector matching) - hybrid array/tree storage
    AttributeStorage* attributes;  // Hybrid attribute storage (array < 10, tree >= 10)

    // Pseudo-class state (for :hover, :focus, etc.)
    uint32_t pseudo_state;       // Bitmask of pseudo-class states

    // Memory management
    Pool* pool;                  // Memory pool for this element's allocations

    // Constructor
    DomElement() : DomNodeBase(DOM_NODE_ELEMENT), native_element(nullptr), tag_name(nullptr),
                   tag_name_ptr(nullptr), tag_id(0), id(nullptr), class_names(nullptr), class_count(0),
                   specified_style(nullptr), computed_style(nullptr), style_version(0),
                   needs_style_recompute(false), attributes(nullptr), pseudo_state(0), pool(nullptr) {}

    // Implement virtual methods
    const char* get_name() const override { return tag_name ? tag_name : "#unnamed"; }

    // Document reference
    DocumentStyler* document;    // Parent document styler (should be set after construction)
};

// Pseudo-class state flags
#define PSEUDO_STATE_HOVER          (1 << 0)
#define PSEUDO_STATE_ACTIVE         (1 << 1)
#define PSEUDO_STATE_FOCUS          (1 << 2)
#define PSEUDO_STATE_VISITED        (1 << 3)
#define PSEUDO_STATE_LINK           (1 << 4)
#define PSEUDO_STATE_ENABLED        (1 << 5)
#define PSEUDO_STATE_DISABLED       (1 << 6)
#define PSEUDO_STATE_CHECKED        (1 << 7)
#define PSEUDO_STATE_INDETERMINATE  (1 << 8)
#define PSEUDO_STATE_VALID          (1 << 9)
#define PSEUDO_STATE_INVALID        (1 << 10)
#define PSEUDO_STATE_REQUIRED       (1 << 11)
#define PSEUDO_STATE_OPTIONAL       (1 << 12)
#define PSEUDO_STATE_READ_ONLY      (1 << 13)
#define PSEUDO_STATE_READ_WRITE     (1 << 14)
#define PSEUDO_STATE_FIRST_CHILD    (1 << 15)
#define PSEUDO_STATE_LAST_CHILD     (1 << 16)
#define PSEUDO_STATE_ONLY_CHILD     (1 << 17)

// ============================================================================
// Attribute Storage API
// ============================================================================

/**
 * Create a new AttributeStorage
 * @param pool Memory pool for allocations
 * @return New AttributeStorage or NULL on failure
 */
AttributeStorage* attribute_storage_create(Pool* pool);

/**
 * Destroy an AttributeStorage
 * @param storage Storage to destroy
 */
void attribute_storage_destroy(AttributeStorage* storage);

/**
 * Set an attribute (creates or updates)
 * @param storage Attribute storage
 * @param name Attribute name
 * @param value Attribute value
 * @return true on success, false on failure
 */
bool attribute_storage_set(AttributeStorage* storage, const char* name, const char* value);

/**
 * Get an attribute value
 * @param storage Attribute storage
 * @param name Attribute name
 * @return Attribute value or NULL if not found
 */
const char* attribute_storage_get(AttributeStorage* storage, const char* name);

/**
 * Check if an attribute exists
 * @param storage Attribute storage
 * @param name Attribute name
 * @return true if attribute exists, false otherwise
 */
bool attribute_storage_has(AttributeStorage* storage, const char* name);

/**
 * Remove an attribute
 * @param storage Attribute storage
 * @param name Attribute name
 * @return true if attribute was removed, false if not found
 */
bool attribute_storage_remove(AttributeStorage* storage, const char* name);

/**
 * Get all attribute names
 * @param storage Attribute storage
 * @param count Output parameter for number of attributes
 * @return Array of attribute names
 */
const char** attribute_storage_get_names(AttributeStorage* storage, int* count);

// ============================================================================
// DOM Element Creation and Destruction
// ============================================================================

/**
 * Create a new DomElement
 * @param pool Memory pool for allocations
 * @param tag_name Element tag name (e.g., "div", "span")
 * @param native_element Optional pointer to native element (lexbor/Lambda)
 * @return New DomElement or NULL on failure
 */
DomElement* dom_element_create(Pool* pool, const char* tag_name, Element* native_element);

/**
 * Destroy a DomElement
 * @param element Element to destroy
 */
void dom_element_destroy(DomElement* element);

/**
 * Initialize a DomElement structure (for stack-allocated elements)
 * @param element Element to initialize
 * @param pool Memory pool for allocations
 * @param tag_name Element tag name
 * @param native_element Optional pointer to native element
 * @return true on success, false on failure
 */
bool dom_element_init(DomElement* element, Pool* pool, const char* tag_name, Element* native_element);

/**
 * Clear all data from a DomElement (without freeing the structure)
 * @param element Element to clear
 */
void dom_element_clear(DomElement* element);

// ============================================================================
// Attribute Management
// ============================================================================

/**
 * Set an element attribute
 * @param element Target element
 * @param name Attribute name
 * @param value Attribute value
 * @return true on success, false on failure
 */
bool dom_element_set_attribute(DomElement* element, const char* name, const char* value);

/**
 * Get an element attribute
 * @param element Target element
 * @param name Attribute name
 * @return Attribute value or NULL if not found
 */
const char* dom_element_get_attribute(DomElement* element, const char* name);

/**
 * Remove an element attribute
 * @param element Target element
 * @param name Attribute name
 * @return true if attribute was removed, false if not found
 */
bool dom_element_remove_attribute(DomElement* element, const char* name);

/**
 * Check if element has an attribute
 * @param element Target element
 * @param name Attribute name
 * @return true if attribute exists, false otherwise
 */
bool dom_element_has_attribute(DomElement* element, const char* name);

// ============================================================================
// Class Management
// ============================================================================

/**
 * Add a CSS class to an element
 * @param element Target element
 * @param class_name Class name to add
 * @return true on success, false on failure
 */
bool dom_element_add_class(DomElement* element, const char* class_name);

/**
 * Remove a CSS class from an element
 * @param element Target element
 * @param class_name Class name to remove
 * @return true if class was removed, false if not found
 */
bool dom_element_remove_class(DomElement* element, const char* class_name);

/**
 * Check if element has a CSS class
 * @param element Target element
 * @param class_name Class name to check
 * @return true if class exists, false otherwise
 */
bool dom_element_has_class(DomElement* element, const char* class_name);

/**
 * Toggle a CSS class on an element
 * @param element Target element
 * @param class_name Class name to toggle
 * @return true if class is now present, false if removed
 */
bool dom_element_toggle_class(DomElement* element, const char* class_name);

// ============================================================================
// Inline Style Support
// ============================================================================

/**
 * Parse and apply inline style attribute to an element
 * @param element Target element
 * @param style_text Inline style text (e.g., "color: red; font-size: 14px")
 * @return Number of declarations applied
 */
int dom_element_apply_inline_style(DomElement* element, const char* style_text);

/**
 * Get inline style text from an element
 * @param element Source element
 * @return Inline style text or NULL if none
 */
const char* dom_element_get_inline_style(DomElement* element);

/**
 * Remove inline styles from an element
 * @param element Target element
 * @return true if inline styles were removed, false otherwise
 */
bool dom_element_remove_inline_styles(DomElement* element);

// ============================================================================
// Style Management
// ============================================================================

/**
 * Apply a CSS declaration to an element
 * @param element Target element
 * @param declaration CSS declaration to apply
 * @return true on success, false on failure
 */
bool dom_element_apply_declaration(DomElement* element, CssDeclaration* declaration);

/**
 * Apply a CSS rule to an element
 * @param element Target element
 * @param rule CSS rule with declarations
 * @param specificity Selector specificity for cascade resolution
 * @return Number of declarations applied
 */
int dom_element_apply_rule(DomElement* element, CssRule* rule, CssSpecificity specificity);

/**
 * Get the specified value for a CSS property
 * @param element Target element
 * @param property_id Property to look up
 * @return Specified CSS declaration or NULL if not set
 */
CssDeclaration* dom_element_get_specified_value(DomElement* element, CssPropertyId property_id);

/**
 * Remove a CSS property from an element
 * @param element Target element
 * @param property_id Property to remove
 * @return true if property was removed, false if not found
 */
bool dom_element_remove_property(DomElement* element, CssPropertyId property_id);

// ============================================================================
// Pseudo-Class State Management
// ============================================================================

/**
 * Set a pseudo-class state flag
 * @param element Target element
 * @param pseudo_state Pseudo-state flag(s) to set
 */
void dom_element_set_pseudo_state(DomElement* element, uint32_t pseudo_state);

/**
 * Clear a pseudo-class state flag
 * @param element Target element
 * @param pseudo_state Pseudo-state flag(s) to clear
 */
void dom_element_clear_pseudo_state(DomElement* element, uint32_t pseudo_state);

/**
 * Check if a pseudo-class state is set
 * @param element Target element
 * @param pseudo_state Pseudo-state flag to check
 * @return true if state is set, false otherwise
 */
bool dom_element_has_pseudo_state(DomElement* element, uint32_t pseudo_state);

/**
 * Toggle a pseudo-class state flag
 * @param element Target element
 * @param pseudo_state Pseudo-state flag to toggle
 * @return true if state is now set, false if cleared
 */
bool dom_element_toggle_pseudo_state(DomElement* element, uint32_t pseudo_state);

// ============================================================================
// DOM Tree Navigation
// ============================================================================

/**
 * Get element parent
 * @param element Target element
 * @return Parent element or NULL if none
 */
DomElement* dom_element_get_parent(DomElement* element);

/**
 * Get first child element
 * @param element Target element
 * @return First child or NULL if none
 */
DomElement* dom_element_get_first_child(DomElement* element);

/**
 * Get next sibling element
 * @param element Target element
 * @return Next sibling or NULL if none
 */
DomElement* dom_element_get_next_sibling(DomElement* element);

/**
 * Get previous sibling element
 * @param element Target element
 * @return Previous sibling or NULL if none
 */
DomElement* dom_element_get_prev_sibling(DomElement* element);

/**
 * Append a child element
 * @param parent Parent element
 * @param child Child element to append
 * @return true on success, false on failure
 */
bool dom_element_append_child(DomElement* parent, DomElement* child);

/**
 * Remove a child element
 * @param parent Parent element
 * @param child Child element to remove
 * @return true on success, false if child not found
 */
bool dom_element_remove_child(DomElement* parent, DomElement* child);

/**
 * Insert a child element before another child
 * @param parent Parent element
 * @param new_child Child element to insert
 * @param reference_child Child before which to insert
 * @return true on success, false on failure
 */
bool dom_element_insert_before(DomElement* parent, DomElement* new_child, DomElement* reference_child);

// ============================================================================
// Structural Queries
// ============================================================================

/**
 * Check if element matches a structural position
 * @param element Target element
 * @return true if element is first child of its parent
 */
bool dom_element_is_first_child(DomElement* element);

/**
 * Check if element is last child
 * @param element Target element
 * @return true if element is last child of its parent
 */
bool dom_element_is_last_child(DomElement* element);

/**
 * Check if element is only child
 * @param element Target element
 * @return true if element is only child of its parent
 */
bool dom_element_is_only_child(DomElement* element);

/**
 * Get element's index among siblings (0-based)
 * @param element Target element
 * @return Index among siblings, or -1 if no parent
 */
int dom_element_get_child_index(DomElement* element);

/**
 * Count all children (elements, text nodes, comments)
 * @param element Target element
 * @return Total number of child nodes
 */
int dom_element_child_count(DomElement* element);

/**
 * Count only element children (excludes text nodes and comments)
 * @param element Target element
 * @return Number of child elements
 */
int dom_element_count_child_elements(DomElement* element);

/**
 * Check if element matches nth-child formula
 * @param element Target element
 * @param a Coefficient in an+b formula
 * @param b Constant in an+b formula
 * @return true if element matches nth-child(an+b)
 */
bool dom_element_matches_nth_child(DomElement* element, int a, int b);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Print element information for debugging
 * @param element Element to print
 */
void dom_element_print_info(DomElement* element);

/**
 * Print element's style tree for debugging
 * @param element Element to print
 */
void dom_element_print_styles(DomElement* element);

/**
 * Get element statistics
 * @param element Element to analyze
 * @param specified_count Output: number of specified properties
 * @param computed_count Output: number of computed properties
 * @param total_declarations Output: total number of declarations in cascade
 */
void dom_element_get_style_stats(DomElement* element,
                                 int* specified_count,
                                 int* computed_count,
                                 int* total_declarations);

/**
 * Clone a DomElement (deep copy)
 * @param source Element to clone
 * @param pool Memory pool for new element
 * @return Cloned element or NULL on failure
 */
DomElement* dom_element_clone(DomElement* source, Pool* pool);

/**
 * Print a DOM element and its children to a string buffer
 * @param element Element to print
 * @param buf String buffer to append to
 * @param indent Current indentation level (number of spaces)
 */
void dom_element_print(DomElement* element, StrBuf* buf, int indent);

// ============================================================================
// DOM Text Node API
// ============================================================================

/**
 * Create a new DomText node
 * @param pool Memory pool for allocations
 * @param text Text content (will be copied)
 * @return New DomText or NULL on failure
 */
DomText* dom_text_create(Pool* pool, const char* text);

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
 * Set text content
 * @param text_node Text node
 * @param text New text content (will be copied)
 * @return true on success, false on failure
 */
bool dom_text_set_content(DomText* text_node, const char* text);

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
 * Get comment/DOCTYPE content
 * @param comment_node Comment node
 * @return Content string
 */
const char* dom_comment_get_content(DomComment* comment_node);

// Note: DOM node type utilities (dom_node_get_type, dom_node_is_element, etc.)
// are now provided by dom_node.h

#ifdef __cplusplus
}

// ============================================================================
// DomNodeBase Inline Method Implementations
// ============================================================================
// These must come after the derived class definitions are complete

inline DomElement* DomNodeBase::as_element() {
    return is_element() ? static_cast<DomElement*>(this) : nullptr;
}

inline DomText* DomNodeBase::as_text() {
    return is_text() ? static_cast<DomText*>(this) : nullptr;
}

inline DomComment* DomNodeBase::as_comment() {
    return is_comment() ? static_cast<DomComment*>(this) : nullptr;
}

inline const DomElement* DomNodeBase::as_element() const {
    return is_element() ? static_cast<const DomElement*>(this) : nullptr;
}

inline const DomText* DomNodeBase::as_text() const {
    return is_text() ? static_cast<const DomText*>(this) : nullptr;
}

inline const DomComment* DomNodeBase::as_comment() const {
    return is_comment() ? static_cast<const DomComment*>(this) : nullptr;
}

// Convenience method implementations
inline uintptr_t DomNodeBase::tag() const {
    const DomElement* elem = as_element();
    return elem ? elem->tag_id : 0;
}

inline unsigned char* DomNodeBase::text_data() const {
    const DomText* text = as_text();
    return text ? (unsigned char*)text->text : nullptr;
}

inline const char* DomNodeBase::get_attribute(const char* attr_name) const {
    const DomElement* elem = as_element();
    return elem ? dom_element_get_attribute(const_cast<DomElement*>(elem), attr_name) : nullptr;
}
#endif

#endif // DOM_ELEMENT_H
