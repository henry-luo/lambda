#ifndef CSS_STYLE_NODE_H
#define CSS_STYLE_NODE_H

#include "../../../lib/avl_tree.h"
#include "css_style.hpp"
#include <stdint.h>
#include <stdbool.h>

/**
 * CSS Style Node System
 *
 * This system extends the basic AVL tree with CSS cascade support.
 * Each style node represents a CSS property with its winning declaration
 * and maintains a list of losing declarations for proper cascade resolution.
 */

// ============================================================================
// Weak Declaration List (for CSS Cascade)
// ============================================================================

/**
 * CSS Style Node System
 *
 * This system extends the basic AVL tree with CSS cascade support.
 * Each style node represents a CSS property with its winning declaration
 * and maintains a list of losing declarations for proper cascade resolution.
 *
 * Types CssSpecificity, CssOrigin, and CssDeclaration are defined in css_style.h
 */

/**
 * Weak Declaration Node
 * Maintains losing declarations in specificity order for cascade fallback
 */
typedef struct WeakDeclaration {
    CssDeclaration* declaration;  // The losing declaration
    struct WeakDeclaration* next; // Next in specificity-sorted list
} WeakDeclaration;

// ============================================================================
// Style Node (extends AVL Node)
// ============================================================================

/**
 * Style Node
 * Extends AvlNode to support CSS cascade resolution
 */
typedef struct StyleNode {
    AvlNode base;                    // Base AVL tree node
    CssDeclaration* winning_decl;    // Current winning declaration
    WeakDeclaration* weak_list;      // Sorted list of losing declarations
    bool needs_recompute;            // Flag for computed value invalidation
    void* computed_value;            // Cached computed value
    uint32_t compute_version;        // Version for cache invalidation
} StyleNode;

// ============================================================================
// Style Tree (Document-level style management)
// ============================================================================

/**
 * Style Tree
 * Document-wide style management system
 */
typedef struct StyleTree {
    AvlTree* tree;                   // Underlying AVL tree
    Pool* pool;                      // Memory pool
    int declaration_count;           // Total number of declarations
    int next_source_order;           // Next source order counter
    uint32_t compute_version;        // Global compute version for cache invalidation
} StyleTree;

// ============================================================================
// Style Node Management API
// ============================================================================

/**
 * Create a new style tree
 * @param pool Memory pool for allocations
 * @return New style tree or NULL on failure
 */
StyleTree* style_tree_create(Pool* pool);

/**
 * Destroy a style tree
 * @param style_tree Tree to destroy
 */
void style_tree_destroy(StyleTree* style_tree);

/**
 * Clear all style nodes from the tree
 * @param style_tree Tree to clear
 */
void style_tree_clear(StyleTree* style_tree);

/**
 * Create a CSS declaration
 * @param property_id CSS property ID
 * @param value Parsed property value
 * @param specificity Selector specificity
 * @param origin Declaration origin
 * @param pool Memory pool for allocation
 * @return New declaration or NULL on failure
 */
CssDeclaration* css_declaration_create(CssPropertyId property_id,
                                      void* value,
                                      CssSpecificity specificity,
                                      CssOrigin origin,
                                      Pool* pool);

/**
 * Increment declaration reference count
 * @param declaration Declaration to reference
 */
void css_declaration_ref(CssDeclaration* declaration);

/**
 * Decrement declaration reference count and free if zero
 * @param declaration Declaration to unreference
 */
void css_declaration_unref(CssDeclaration* declaration);

/**
 * Apply a CSS declaration to a style tree
 * @param style_tree Target style tree
 * @param declaration Declaration to apply
 * @return Style node that holds the property, NULL on failure
 */
StyleNode* style_tree_apply_declaration(StyleTree* style_tree, CssDeclaration* declaration);

/**
 * Get the winning declaration for a property
 * @param style_tree Style tree to query
 * @param property_id Property to look up
 * @return Winning declaration or NULL if not set
 */
CssDeclaration* style_tree_get_declaration(StyleTree* style_tree, CssPropertyId property_id);

/**
 * Get the computed value for a property
 * @param style_tree Style tree to query
 * @param property_id Property to look up
 * @param parent_tree Parent element's style tree (for inheritance)
 * @return Computed value or NULL if not available
 */
void* style_tree_get_computed_value(StyleTree* style_tree,
                                   CssPropertyId property_id,
                                   StyleTree* parent_tree);

/**
 * Remove a property from the style tree
 * @param style_tree Style tree to modify
 * @param property_id Property to remove
 * @return true if property was removed, false if not found
 */
bool style_tree_remove_property(StyleTree* style_tree, CssPropertyId property_id);

/**
 * Remove a specific declaration from the style tree
 * @param style_tree Style tree to modify
 * @param declaration Declaration to remove
 * @return true if declaration was removed, false if not found
 */
bool style_tree_remove_declaration(StyleTree* style_tree, CssDeclaration* declaration);

// ============================================================================
// CSS Cascade Resolution
// ============================================================================

/**
 * Compare two CSS specificities
 * @param a First specificity
 * @param b Second specificity
 * @return -1 if a < b, 0 if a == b, 1 if a > b
 */
int css_specificity_compare(CssSpecificity a, CssSpecificity b);

/**
 * Compare two CSS declarations for cascade ordering
 * @param a First declaration
 * @param b Second declaration
 * @return -1 if a loses to b, 0 if equal, 1 if a wins over b
 */
int css_declaration_cascade_compare(CssDeclaration* a, CssDeclaration* b);

/**
 * Resolve CSS cascade for a property
 * @param node Style node to resolve
 * @return Winning declaration after cascade resolution
 */
CssDeclaration* style_node_resolve_cascade(StyleNode* node);

// ============================================================================
// Style Inheritance
// ============================================================================

/**
 * Check if a property should inherit from parent
 * @param property_id Property to check
 * @param declaration Current declaration (may specify inherit keyword)
 * @return true if should inherit, false otherwise
 */
bool css_should_inherit_property(CssPropertyId property_id, CssDeclaration* declaration);

/**
 * Inherit a property value from parent
 * @param child_tree Child element's style tree
 * @param parent_tree Parent element's style tree
 * @param property_id Property to inherit
 * @return true if inheritance was applied, false otherwise
 */
bool style_tree_inherit_property(StyleTree* child_tree,
                                StyleTree* parent_tree,
                                CssPropertyId property_id);

/**
 * Apply inheritance for all inherited properties
 * @param child_tree Child element's style tree
 * @param parent_tree Parent element's style tree
 * @return Number of properties inherited
 */
int style_tree_apply_inheritance(StyleTree* child_tree, StyleTree* parent_tree);

// ============================================================================
// Computed Value Calculation
// ============================================================================

/**
 * Check if a property inherits by default
 * @param property_id Property ID
 * @return true if property inherits by default, false otherwise
 */
bool css_property_is_inherited(CssPropertyId property_id);

/**
 * Invalidate computed values in a style tree
 * @param style_tree Style tree to invalidate
 */
void style_tree_invalidate_computed_values(StyleTree* style_tree);

/**
 * Compute a property value
 * @param node Style node containing the property
 * @param parent_tree Parent element's style tree (for relative values)
 * @return Computed value
 */
void* style_node_compute_value(StyleNode* node, StyleTree* parent_tree);

/**
 * Get or compute a property value with caching
 * @param node Style node containing the property
 * @param parent_tree Parent element's style tree
 * @return Computed value (may be cached)
 */
void* style_node_get_computed_value(StyleNode* node, StyleTree* parent_tree);

// ============================================================================
// Style Tree Traversal and Debugging
// ============================================================================

/**
 * Callback function for style tree traversal
 */
typedef bool (*style_tree_callback_t)(StyleNode* node, void* context);

/**
 * Traverse all style nodes in a tree
 * @param style_tree Tree to traverse
 * @param callback Function to call for each node
 * @param context User context passed to callback
 * @return Number of nodes processed
 */
int style_tree_foreach(StyleTree* style_tree, style_tree_callback_t callback, void* context);

/**
 * Print style tree contents for debugging
 * @param style_tree Tree to print
 */
void style_tree_print(StyleTree* style_tree);

/**
 * Print a style node's cascade information
 * @param node Node to print
 */
void style_node_print_cascade(StyleNode* node);

/**
 * Get style tree statistics
 * @param style_tree Tree to analyze
 * @param total_nodes Output: total number of style nodes
 * @param total_declarations Output: total number of declarations
 * @param avg_weak_count Output: average number of weak declarations per node
 */
void style_tree_get_statistics(StyleTree* style_tree,
                              int* total_nodes,
                              int* total_declarations,
                              double* avg_weak_count);

// ============================================================================
// Advanced Style Operations
// ============================================================================

/**
 * Clone a style tree
 * @param source Tree to clone
 * @param target_pool Pool for the new tree
 * @return Cloned tree or NULL on failure
 */
StyleTree* style_tree_clone(StyleTree* source, Pool* target_pool);

/**
 * Merge two style trees (for style composition)
 * @param target Tree to merge into
 * @param source Tree to merge from
 * @return Number of declarations merged
 */
int style_tree_merge(StyleTree* target, StyleTree* source);

/**
 * Create a subset style tree with only specific properties
 * @param source Source tree
 * @param property_ids Array of property IDs to include
 * @param property_count Number of properties to include
 * @param target_pool Pool for the new tree
 * @return Subset tree or NULL on failure
 */
StyleTree* style_tree_create_subset(StyleTree* source,
                                   CssPropertyId* property_ids,
                                   int property_count,
                                   Pool* target_pool);

// ============================================================================
// CSS Specificity Utilities
// ============================================================================

/**
 * Create a CSS specificity structure
 * @param inline_style 1 if inline style, 0 otherwise
 * @param ids Number of ID selectors
 * @param classes Number of class/attribute/pseudo-class selectors
 * @param elements Number of element/pseudo-element selectors
 * @param important true if !important
 * @return Specificity structure
 */
CssSpecificity css_specificity_create(uint8_t inline_style,
                                     uint8_t ids,
                                     uint8_t classes,
                                     uint8_t elements,
                                     bool important);

/**
 * Convert specificity to a comparable integer value
 * @param specificity Specificity to convert
 * @return Integer value for comparison
 */
uint32_t css_specificity_to_value(CssSpecificity specificity);

/**
 * Print specificity for debugging
 * @param specificity Specificity to print
 */
void css_specificity_print(CssSpecificity specificity);


#endif // CSS_STYLE_NODE_H
