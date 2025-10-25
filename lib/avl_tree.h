#ifndef AVL_TREE_H
#define AVL_TREE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "mempool.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * AVL Tree Node Structure
 * 
 * Each node represents a CSS property with its value and cascade information.
 * The tree is keyed by property_id for O(log n) lookups.
 */
typedef struct AvlNode {
    uintptr_t property_id;       // CSS property ID as key (e.g., LXB_CSS_PROPERTY_COLOR)
    void* declaration;           // Pointer to CSS declaration/value
    short height;                // AVL tree balance height
    struct AvlNode* left;        // Left child
    struct AvlNode* right;       // Right child
    struct AvlNode* parent;      // Parent node (for easier traversal)
} AvlNode;

/**
 * AVL Tree Structure
 * 
 * Container for the AVL tree with memory management and statistics.
 */
typedef struct AvlTree {
    AvlNode* root;               // Root node of the tree
    Pool* pool;                  // Memory allocation pool
    int node_count;              // Number of nodes in the tree
    int max_depth;               // Maximum depth reached (for debugging)
    AvlNode* last_removed;       // Last node removed (for safe traversal)
} AvlTree;

/**
 * Callback function type for tree traversal operations
 */
typedef bool (*avl_callback_t)(AvlNode* node, void* context);

/**
 * Comparison function type for custom key comparison
 * Returns: < 0 if a < b, 0 if a == b, > 0 if a > b
 */
typedef int (*avl_compare_t)(uintptr_t a, uintptr_t b);

// ============================================================================
// Core AVL Tree Operations
// ============================================================================

/**
 * Create a new AVL tree
 * @param pool Memory pool for allocations
 * @return New AVL tree instance or NULL on failure
 */
AvlTree* avl_tree_create(Pool* pool);

/**
 * Initialize an existing AVL tree structure
 * @param tree Tree to initialize
 * @param pool Memory pool for allocations
 * @return true on success, false on failure
 */
bool avl_tree_init(AvlTree* tree, Pool* pool);

/**
 * Destroy an AVL tree and free all nodes
 * @param tree Tree to destroy
 */
void avl_tree_destroy(AvlTree* tree);

/**
 * Clear all nodes from the tree without destroying the tree structure
 * @param tree Tree to clear
 */
void avl_tree_clear(AvlTree* tree);

// ============================================================================
// Node Operations
// ============================================================================

/**
 * Insert a new node or update existing node
 * @param tree Target tree
 * @param property_id Key for the node
 * @param declaration Value to store
 * @return Pointer to the node (new or existing), NULL on failure
 */
AvlNode* avl_tree_insert(AvlTree* tree, uintptr_t property_id, void* declaration);

/**
 * Search for a node by property ID
 * @param tree Tree to search
 * @param property_id Key to find
 * @return Pointer to the node if found, NULL otherwise
 */
AvlNode* avl_tree_search(AvlTree* tree, uintptr_t property_id);

/**
 * Remove a node by property ID
 * @param tree Tree to modify
 * @param property_id Key of node to remove
 * @return Pointer to the removed declaration, NULL if not found
 */
void* avl_tree_remove(AvlTree* tree, uintptr_t property_id);

/**
 * Remove a specific node from the tree
 * @param tree Tree to modify
 * @param node Node to remove
 * @return Pointer to the removed declaration, NULL if not found
 */
void* avl_tree_remove_node(AvlTree* tree, AvlNode* node);

// ============================================================================
// Traversal Operations
// ============================================================================

/**
 * Traverse the tree in-order (sorted by property ID)
 * @param tree Tree to traverse
 * @param callback Function to call for each node
 * @param context User context passed to callback
 * @return Number of nodes processed
 */
int avl_tree_foreach_inorder(AvlTree* tree, avl_callback_t callback, void* context);

/**
 * Traverse the tree pre-order
 * @param tree Tree to traverse
 * @param callback Function to call for each node
 * @param context User context passed to callback
 * @return Number of nodes processed
 */
int avl_tree_foreach_preorder(AvlTree* tree, avl_callback_t callback, void* context);

/**
 * Traverse the tree post-order
 * @param tree Tree to traverse
 * @param callback Function to call for each node
 * @param context User context passed to callback
 * @return Number of nodes processed
 */
int avl_tree_foreach_postorder(AvlTree* tree, avl_callback_t callback, void* context);

// ============================================================================
// Query Operations
// ============================================================================

/**
 * Get the minimum node (leftmost)
 * @param tree Tree to query
 * @return Pointer to minimum node, NULL if empty
 */
AvlNode* avl_tree_min(AvlTree* tree);

/**
 * Get the maximum node (rightmost)
 * @param tree Tree to query
 * @return Pointer to maximum node, NULL if empty
 */
AvlNode* avl_tree_max(AvlTree* tree);

/**
 * Find the predecessor of a given node
 * @param node Node to find predecessor for
 * @return Pointer to predecessor node, NULL if none
 */
AvlNode* avl_tree_predecessor(AvlNode* node);

/**
 * Find the successor of a given node
 * @param node Node to find successor for
 * @return Pointer to successor node, NULL if none
 */
AvlNode* avl_tree_successor(AvlNode* node);

/**
 * Get the height of the tree
 * @param tree Tree to query
 * @return Height of the tree (0 for empty tree)
 */
int avl_tree_height(AvlTree* tree);

/**
 * Check if the tree is empty
 * @param tree Tree to check
 * @return true if empty, false otherwise
 */
bool avl_tree_is_empty(AvlTree* tree);

/**
 * Get the number of nodes in the tree
 * @param tree Tree to query
 * @return Number of nodes
 */
int avl_tree_size(AvlTree* tree);

// ============================================================================
// Validation and Debug Operations
// ============================================================================

/**
 * Validate the AVL tree invariants
 * @param tree Tree to validate
 * @return true if tree is valid, false otherwise
 */
bool avl_tree_validate(AvlTree* tree);

/**
 * Print tree structure for debugging
 * @param tree Tree to print
 * @param print_value Function to print node values (optional)
 */
void avl_tree_print(AvlTree* tree, void (*print_value)(void* declaration));

/**
 * Get tree statistics
 * @param tree Tree to analyze
 * @param stats Structure to fill with statistics
 */
typedef struct AvlTreeStats {
    int node_count;              // Total number of nodes
    int height;                  // Tree height
    int max_depth;               // Maximum depth from root
    int min_depth;               // Minimum depth to a leaf
    double average_depth;        // Average depth of all nodes
    int balance_violations;      // Number of balance violations (should be 0)
} AvlTreeStats;

void avl_tree_get_stats(AvlTree* tree, AvlTreeStats* stats);

// ============================================================================
// Advanced Operations
// ============================================================================

/**
 * Bulk insert multiple key-value pairs
 * @param tree Target tree
 * @param pairs Array of property_id, declaration pairs
 * @param count Number of pairs to insert
 * @return Number of successful insertions
 */
int avl_tree_bulk_insert(AvlTree* tree, uintptr_t* property_ids, 
                        void** declarations, int count);

/**
 * Clone an AVL tree
 * @param source Tree to clone
 * @param target_pool Pool for the new tree
 * @param clone_value Function to clone declaration values (optional)
 * @return New cloned tree, NULL on failure
 */
AvlTree* avl_tree_clone(AvlTree* source, Pool* target_pool, 
                       void* (*clone_value)(void* declaration, Pool* pool));

/**
 * Merge two AVL trees
 * @param target Tree to merge into
 * @param source Tree to merge from
 * @param merge_conflict Function to resolve conflicts (optional)
 * @return Number of nodes merged
 */
int avl_tree_merge(AvlTree* target, AvlTree* source,
                  void* (*merge_conflict)(void* existing, void* new_value, Pool* pool));

#ifdef __cplusplus
}
#endif

#endif // AVL_TREE_H
