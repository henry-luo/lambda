#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lambda-data.hpp"

/**
 * Lambda Element Tree Read-Only Interface
 * 
 * This interface provides efficient, const-correct access to Lambda element trees
 * without exposing the underlying mutable data structures. It's designed for:
 * - Document processing and transformation
 * - Template engines and formatters  
 * - Tree analysis and validation
 * - Safe traversal in multi-threaded contexts
 */

// Forward declarations
typedef struct ElementReader ElementReader;
typedef struct ElementIterator ElementIterator;
typedef struct AttributeReader AttributeReader;

// ==============================================================================
// Core Read-Only Element Interface
// ==============================================================================

/**
 * ElementReader - Read-only view of a Lambda Element
 * 
 * Provides safe, efficient access to element properties without mutation.
 * All pointers returned are const-qualified and should not be modified.
 */
struct ElementReader {
    const Element* element;          // Underlying element (read-only)
    const TypeElmt* element_type;   // Cached element type info
    const char* tag_name;           // Cached tag name (null-terminated)
    int64_t tag_name_len;          // Tag name length
    int64_t child_count;           // Number of child items
    int64_t attr_count;            // Number of attributes
};

/**
 * Create a read-only element reader from an Element pointer
 * Returns NULL if element is invalid
 */
ElementReader* element_reader_create(const Element* element, Pool* pool);

/**
 * Create element reader from an Item (validates type first)
 */
ElementReader* element_reader_from_item(Item item, Pool* pool);

/**
 * Free element reader resources
 */
void element_reader_free(ElementReader* reader, Pool* pool);

// ==============================================================================
// Element Property Access
// ==============================================================================

/**
 * Get the element's tag name (e.g., "div", "p", "span")
 * Returns null-terminated string, valid for lifetime of reader
 */
const char* element_reader_tag_name(const ElementReader* reader);

/**
 * Get tag name length (more efficient than strlen)
 */
int64_t element_reader_tag_name_len(const ElementReader* reader);

/**
 * Check if element has a specific tag name (case-sensitive comparison)
 */
bool element_reader_has_tag(const ElementReader* reader, const char* tag_name);

/**
 * Check if element has a specific tag name with length (more efficient)
 */
bool element_reader_has_tag_n(const ElementReader* reader, const char* tag_name, int64_t len);

/**
 * Get number of child elements/content items
 */
int64_t element_reader_child_count(const ElementReader* reader);

/**
 * Get number of attributes
 */
int64_t element_reader_attr_count(const ElementReader* reader);

/**
 * Check if element is empty (no children, no text content)
 */
bool element_reader_is_empty(const ElementReader* reader);

/**
 * Check if element contains only text content (no child elements)
 */
bool element_reader_is_text_only(const ElementReader* reader);

// ==============================================================================
// Child Access
// ==============================================================================

/**
 * Get child item at specific index
 * Returns ItemNull if index out of bounds
 */
Item element_reader_child_at(const ElementReader* reader, int64_t index);

/**
 * Get typed child item at specific index (more efficient)
 */
TypedItem element_reader_child_typed_at(const ElementReader* reader, int64_t index);

/**
 * Find first child element with specific tag name
 * Returns ItemNull if not found
 */
Item element_reader_find_child(const ElementReader* reader, const char* tag_name);

/**
 * Find all child elements with specific tag name
 * Returns array of Items, caller must free with arraylist_free()
 */
ArrayList* element_reader_find_children(const ElementReader* reader, const char* tag_name, Pool* pool);

/**
 * Get all text content as concatenated string
 * Traverses all child text nodes and concatenates them
 * Returns newly allocated String, caller must manage memory
 */
String* element_reader_text_content(const ElementReader* reader, Pool* pool);

/**
 * Get immediate text content only (no recursive traversal)
 * Returns newly allocated String, caller must manage memory
 */
String* element_reader_immediate_text(const ElementReader* reader, Pool* pool);

// ==============================================================================
// Attribute Access
// ==============================================================================

/**
 * AttributeReader - Read-only view of element attributes
 */
struct AttributeReader {
    const ElementReader* element_reader;
    const TypeMap* map_type;        // Cached map type info
    const void* attr_data;          // Packed attribute data
    const ShapeEntry* shape;        // Attribute shape definition
};

/**
 * Get attribute reader for element
 */
AttributeReader* element_reader_attributes(const ElementReader* reader, Pool* pool);

/**
 * Free attribute reader
 */
void attribute_reader_free(AttributeReader* attr_reader, Pool* pool);

/**
 * Check if attribute exists
 */
bool attribute_reader_has(const AttributeReader* attr_reader, const char* attr_name);

/**
 * Get attribute value as string
 * Returns NULL if attribute doesn't exist or isn't a string
 */
const String* attribute_reader_get_string(const AttributeReader* attr_reader, const char* attr_name);

/**
 * Get attribute value as C string (null-terminated)
 * Returns NULL if attribute doesn't exist or isn't a string
 */
const char* attribute_reader_get_cstring(const AttributeReader* attr_reader, const char* attr_name);

/**
 * Get attribute value as typed item
 */
TypedItem attribute_reader_get_typed(const AttributeReader* attr_reader, const char* attr_name);

/**
 * Get all attribute names
 * Returns array of strings, caller must free
 */
ArrayList* attribute_reader_names(const AttributeReader* attr_reader, Pool* pool);

// ==============================================================================
// Element Tree Iterator
// ==============================================================================

typedef enum {
    ITER_CHILDREN_ONLY,     // Iterate only direct children
    ITER_DEPTH_FIRST,       // Depth-first traversal of entire subtree
    ITER_BREADTH_FIRST,     // Breadth-first traversal of entire subtree
    ITER_ELEMENTS_ONLY,     // Only visit Element nodes (skip text/other)
    ITER_TEXT_ONLY          // Only visit text/string nodes
} IteratorMode;

/**
 * ElementIterator - Efficient tree traversal
 */
struct ElementIterator {
    const ElementReader* root;      // Root element being iterated
    IteratorMode mode;              // Iteration mode
    int64_t current_index;          // Current position
    int64_t max_depth;              // Maximum depth (-1 for unlimited)
    void* state;                    // Internal state for complex traversals
    Pool* pool;                     // Memory pool for allocations
};

/**
 * Create iterator for element tree
 */
ElementIterator* element_iterator_create(const ElementReader* root, IteratorMode mode, Pool* pool);

/**
 * Set maximum depth for traversal (-1 for unlimited)
 */
void element_iterator_set_max_depth(ElementIterator* iter, int64_t max_depth);

/**
 * Get next item in iteration
 * Returns ItemNull when iteration complete
 */
Item element_iterator_next(ElementIterator* iter);

/**
 * Get next item as ElementReader (for elements only)
 * Returns NULL for non-elements or when iteration complete
 */
ElementReader* element_iterator_next_element(ElementIterator* iter);

/**
 * Reset iterator to beginning
 */
void element_iterator_reset(ElementIterator* iter);

/**
 * Check if iteration has more items
 */
bool element_iterator_has_next(const ElementIterator* iter);

/**
 * Get current depth in tree (0 = root level)
 */
int64_t element_iterator_depth(const ElementIterator* iter);

/**
 * Free iterator resources
 */
void element_iterator_free(ElementIterator* iter);

// ==============================================================================
// Utility Functions
// ==============================================================================

/**
 * Create element reader from Input root
 * Handles the case where root might be a List containing elements
 */
ElementReader* element_reader_from_input_root(const Input* input, Pool* pool);

/**
 * Find element by ID attribute
 */
ElementReader* element_reader_find_by_id(const ElementReader* root, const char* id, Pool* pool);

/**
 * Find elements by class attribute (space-separated classes)
 */
ArrayList* element_reader_find_by_class(const ElementReader* root, const char* class_name, Pool* pool);

/**
 * Find elements by attribute value
 */
ArrayList* element_reader_find_by_attribute(const ElementReader* root, const char* attr_name, const char* attr_value, Pool* pool);

/**
 * Count total elements in subtree
 */
int64_t element_reader_count_elements(const ElementReader* root);

/**
 * Get tree depth (maximum nesting level)
 */
int64_t element_reader_tree_depth(const ElementReader* root);

/**
 * Serialize element tree to debug string
 */
String* element_reader_debug_string(const ElementReader* root, Pool* pool);

#ifdef __cplusplus
}
#endif