#include "avl_tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// ============================================================================
#include "avl_tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper structures for callbacks
struct PrintContext {
    void (*print_value)(void* declaration);
    int index;
};

struct CloneContext {
    AvlTree* target;
    void* (*clone_value)(void* declaration, Pool* pool);
    Pool* pool;
};

struct MergeContext {
    AvlTree* target;
    void* (*merge_conflict)(void* existing, void* new_value, Pool* pool);
    int merged_count;
};

// Internal helper functions
// ============================================================================

/**
 * Get the height of a node (handles NULL)
 */
static inline int node_height(AvlNode* node) {
    return node ? node->height : 0;
}

/**
 * Calculate the balance factor of a node
 */
static inline int node_balance_factor(AvlNode* node) {
    return node ? node_height(node->right) - node_height(node->left) : 0;
}

/**
 * Update the height of a node based on its children
 */
static void node_update_height(AvlNode* node) {
    if (!node) return;
    
    int left_height = node_height(node->left);
    int right_height = node_height(node->right);
    node->height = 1 + (left_height > right_height ? left_height : right_height);
}

/**
 * Create a new AVL node
 */
static AvlNode* node_create(Pool* pool, uintptr_t property_id, void* declaration) {
    AvlNode* node = (AvlNode*)pool_calloc(pool, sizeof(AvlNode));
    if (!node) return NULL;
    
    node->property_id = property_id;
    node->declaration = declaration;
    node->height = 1;
    node->left = NULL;
    node->right = NULL;
    node->parent = NULL;
    
    return node;
}

/**
 * Right rotation
 *        y                x
 *       / \              / \
 *      x   C    -->     A   y
 *     / \                  / \
 *    A   B                B   C
 */
static AvlNode* rotate_right(AvlNode* y) {
    AvlNode* x = y->left;
    AvlNode* B = x->right;
    
    // Perform rotation
    x->right = y;
    y->left = B;
    
    // Update parents
    x->parent = y->parent;
    y->parent = x;
    if (B) B->parent = y;
    
    // Update heights
    node_update_height(y);
    node_update_height(x);
    
    return x;
}

/**
 * Left rotation
 *      x                  y
 *     / \                / \
 *    A   y      -->     x   C
 *       / \            / \
 *      B   C          A   B
 */
static AvlNode* rotate_left(AvlNode* x) {
    AvlNode* y = x->right;
    AvlNode* B = y->left;
    
    // Perform rotation
    y->left = x;
    x->right = B;
    
    // Update parents
    y->parent = x->parent;
    x->parent = y;
    if (B) B->parent = x;
    
    // Update heights
    node_update_height(x);
    node_update_height(y);
    
    return y;
}

/**
 * Rebalance a node and return the new subtree root
 */
static AvlNode* rebalance(AvlNode* node) {
    if (!node) return NULL;
    
    // Update height first
    node_update_height(node);
    
    int balance = node_balance_factor(node);
    
    // Left heavy
    if (balance < -1) {
        // Left-Right case
        if (node_balance_factor(node->left) > 0) {
            node->left = rotate_left(node->left);
        }
        // Left-Left case
        return rotate_right(node);
    }
    
    // Right heavy
    if (balance > 1) {
        // Right-Left case
        if (node_balance_factor(node->right) < 0) {
            node->right = rotate_right(node->right);
        }
        // Right-Right case
        return rotate_left(node);
    }
    
    return node;
}

/**
 * Find the minimum node in a subtree
 */
static AvlNode* subtree_min(AvlNode* node) {
    if (!node) return NULL;
    while (node->left) {
        node = node->left;
    }
    return node;
}

/**
 * Find the maximum node in a subtree
 */
static AvlNode* subtree_max(AvlNode* node) {
    if (!node) return NULL;
    while (node->right) {
        node = node->right;
    }
    return node;
}

/**
 * Insert a node into a subtree (recursive helper)
 */
static AvlNode* insert_recursive(AvlNode* root, AvlNode* new_node, AvlNode* parent) {
    // Base case: found insertion point
    if (!root) {
        new_node->parent = parent;
        return new_node;
    }
    
    // Recursive insertion
    if (new_node->property_id < root->property_id) {
        root->left = insert_recursive(root->left, new_node, root);
    } else if (new_node->property_id > root->property_id) {
        root->right = insert_recursive(root->right, new_node, root);
    } else {
        // Key already exists - update the declaration
        root->declaration = new_node->declaration;
        return root;
    }
    
    // Rebalance and return new subtree root
    return rebalance(root);
}

/**
 * Remove a node from a subtree (recursive helper)
 */
static AvlNode* remove_recursive(AvlNode* root, uintptr_t property_id, AvlNode** removed_node) {
    if (!root) {
        *removed_node = NULL;
        return NULL;
    }
    
    if (property_id < root->property_id) {
        root->left = remove_recursive(root->left, property_id, removed_node);
    } else if (property_id > root->property_id) {
        root->right = remove_recursive(root->right, property_id, removed_node);
    } else {
        // Found the node to remove
        *removed_node = root;
        
        if (!root->left && !root->right) {
            // Leaf node
            return NULL;
        } else if (!root->left) {
            // Only right child
            if (root->right) root->right->parent = root->parent;
            return root->right;
        } else if (!root->right) {
            // Only left child
            if (root->left) root->left->parent = root->parent;
            return root->left;
        } else {
            // Two children - find successor
            AvlNode* successor = subtree_min(root->right);
            
            // Replace root's data with successor's data
            root->property_id = successor->property_id;
            root->declaration = successor->declaration;
            
            // Remove the successor (which has at most one child)
            AvlNode* temp_removed;
            root->right = remove_recursive(root->right, successor->property_id, &temp_removed);
            
            // Update the removed node pointer to point to the original successor
            *removed_node = temp_removed;
        }
    }
    
    return rebalance(root);
}

/**
 * Search for a node in a subtree (recursive helper)
 */
static AvlNode* search_recursive(AvlNode* root, uintptr_t property_id) {
    if (!root || root->property_id == property_id) {
        return root;
    }
    
    if (property_id < root->property_id) {
        return search_recursive(root->left, property_id);
    } else {
        return search_recursive(root->right, property_id);
    }
}

/**
 * In-order traversal helper with early exit support
 */
static int foreach_inorder_recursive(AvlNode* node, avl_callback_t callback, void* context, bool* should_continue) {
    if (!node || !*should_continue) return 0;
    
    int count = 0;
    
    // Traverse left subtree
    count += foreach_inorder_recursive(node->left, callback, context, should_continue);
    
    // Process current node if we should continue
    if (*should_continue) {
        count++;
        *should_continue = callback(node, context);
    }
    
    // Traverse right subtree if we should continue
    if (*should_continue) {
        count += foreach_inorder_recursive(node->right, callback, context, should_continue);
    }
    
    return count;
}

/**
 * Pre-order traversal helper
 */
static int foreach_preorder_recursive(AvlNode* node, avl_callback_t callback, void* context, bool* should_continue) {
    if (!node || !*should_continue) return 0;
    
    int count = 0;
    
    // Process current node first
    count++;
    *should_continue = callback(node, context);
    
    // Traverse left subtree if we should continue
    if (*should_continue) {
        count += foreach_preorder_recursive(node->left, callback, context, should_continue);
    }
    
    // Traverse right subtree if we should continue
    if (*should_continue) {
        count += foreach_preorder_recursive(node->right, callback, context, should_continue);
    }
    
    return count;
}

/**
 * Post-order traversal helper
 */
static int foreach_postorder_recursive(AvlNode* node, avl_callback_t callback, void* context, bool* should_continue) {
    if (!node || !*should_continue) return 0;
    
    int count = 0;
    
    // Traverse left subtree
    count += foreach_postorder_recursive(node->left, callback, context, should_continue);
    
    // Traverse right subtree if we should continue
    if (*should_continue) {
        count += foreach_postorder_recursive(node->right, callback, context, should_continue);
    }
    
    // Process current node if we should continue
    if (*should_continue) {
        count++;
        *should_continue = callback(node, context);
    }
    
    return count;
}

/**
 * Calculate depth statistics helper
 */
static void calculate_depth_stats(AvlNode* node, int current_depth, int* min_depth, 
                                 int* max_depth, int* total_depth, int* node_count) {
    if (!node) return;
    
    (*node_count)++;
    *total_depth += current_depth;
    
    if (!node->left && !node->right) {
        // Leaf node
        if (*min_depth == -1 || current_depth < *min_depth) {
            *min_depth = current_depth;
        }
        if (current_depth > *max_depth) {
            *max_depth = current_depth;
        }
    }
    
    calculate_depth_stats(node->left, current_depth + 1, min_depth, max_depth, total_depth, node_count);
    calculate_depth_stats(node->right, current_depth + 1, min_depth, max_depth, total_depth, node_count);
}

/**
 * Validate AVL tree properties helper
 */
static bool validate_recursive(AvlNode* node, int* balance_violations) {
    if (!node) return true;
    
    // Check balance factor
    int balance = node_balance_factor(node);
    if (balance < -1 || balance > 1) {
        (*balance_violations)++;
    }
    
    // Check height calculation
    int calculated_height = 1 + (node_height(node->left) > node_height(node->right) ? 
                                 node_height(node->left) : node_height(node->right));
    if (node->height != calculated_height) {
        return false;
    }
    
    // Check parent pointers
    if (node->left && node->left->parent != node) return false;
    if (node->right && node->right->parent != node) return false;
    
    // Check BST property
    if (node->left && node->left->property_id >= node->property_id) return false;
    if (node->right && node->right->property_id <= node->property_id) return false;
    
    return validate_recursive(node->left, balance_violations) && 
           validate_recursive(node->right, balance_violations);
}

// ============================================================================
// Public API Implementation
// ============================================================================

AvlTree* avl_tree_create(Pool* pool) {
    AvlTree* tree = (AvlTree*)pool_calloc(pool, sizeof(AvlTree));
    if (!tree) return NULL;
    
    tree->root = NULL;
    tree->pool = pool;
    tree->node_count = 0;
    tree->max_depth = 0;
    
    return tree;
}

bool avl_tree_init(AvlTree* tree, Pool* pool) {
    if (!tree) return false;
    
    tree->root = NULL;
    tree->pool = pool;
    tree->node_count = 0;
    tree->max_depth = 0;
    
    return true;
}

void avl_tree_destroy(AvlTree* tree) {
    if (!tree) return;
    
    // Memory is managed by the pool, so we just clear the structure
    avl_tree_clear(tree);
}

void avl_tree_clear(AvlTree* tree) {
    if (!tree) return;
    
    tree->root = NULL;
    tree->node_count = 0;
    tree->max_depth = 0;
}

AvlNode* avl_tree_insert(AvlTree* tree, uintptr_t property_id, void* declaration) {
    if (!tree) return NULL;
    
    // Check if node already exists
    AvlNode* existing = avl_tree_search(tree, property_id);
    if (existing) {
        existing->declaration = declaration;
        return existing;
    }
    
    // Create new node
    AvlNode* new_node = node_create(tree->pool, property_id, declaration);
    if (!new_node) return NULL;
    
    // Insert into tree
    tree->root = insert_recursive(tree->root, new_node, NULL);
    tree->node_count++;
    
    // Update max depth
    int height = avl_tree_height(tree);
    if (height > tree->max_depth) {
        tree->max_depth = height;
    }
    
    return new_node;
}

AvlNode* avl_tree_search(AvlTree* tree, uintptr_t property_id) {
    if (!tree) return NULL;
    return search_recursive(tree->root, property_id);
}

void* avl_tree_remove(AvlTree* tree, uintptr_t property_id) {
    if (!tree) return NULL;
    
    AvlNode* removed_node;
    tree->root = remove_recursive(tree->root, property_id, &removed_node);
    
    if (removed_node) {
        tree->node_count--;
        void* declaration = removed_node->declaration;
        return declaration;
    }
    
    return NULL;
}

void* avl_tree_remove_node(AvlTree* tree, AvlNode* node) {
    if (!tree || !node) return NULL;
    return avl_tree_remove(tree, node->property_id);
}

int avl_tree_foreach_inorder(AvlTree* tree, avl_callback_t callback, void* context) {
    if (!tree || !callback) return 0;
    bool should_continue = true;
    return foreach_inorder_recursive(tree->root, callback, context, &should_continue);
}

int avl_tree_foreach_preorder(AvlTree* tree, avl_callback_t callback, void* context) {
    if (!tree || !callback) return 0;
    bool should_continue = true;
    return foreach_preorder_recursive(tree->root, callback, context, &should_continue);
}

int avl_tree_foreach_postorder(AvlTree* tree, avl_callback_t callback, void* context) {
    if (!tree || !callback) return 0;
    bool should_continue = true;
    return foreach_postorder_recursive(tree->root, callback, context, &should_continue);
}

AvlNode* avl_tree_min(AvlTree* tree) {
    if (!tree) return NULL;
    return subtree_min(tree->root);
}

AvlNode* avl_tree_max(AvlTree* tree) {
    if (!tree) return NULL;
    return subtree_max(tree->root);
}

AvlNode* avl_tree_predecessor(AvlNode* node) {
    if (!node) return NULL;
    
    if (node->left) {
        return subtree_max(node->left);
    }
    
    AvlNode* parent = node->parent;
    while (parent && node == parent->left) {
        node = parent;
        parent = parent->parent;
    }
    
    return parent;
}

AvlNode* avl_tree_successor(AvlNode* node) {
    if (!node) return NULL;
    
    if (node->right) {
        return subtree_min(node->right);
    }
    
    AvlNode* parent = node->parent;
    while (parent && node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    
    return parent;
}

int avl_tree_height(AvlTree* tree) {
    if (!tree) return 0;
    return node_height(tree->root);
}

bool avl_tree_is_empty(AvlTree* tree) {
    return !tree || tree->node_count == 0;
}

int avl_tree_size(AvlTree* tree) {
    return tree ? tree->node_count : 0;
}

bool avl_tree_validate(AvlTree* tree) {
    if (!tree) return false;
    
    int balance_violations = 0;
    bool valid = validate_recursive(tree->root, &balance_violations);
    
    return valid && balance_violations == 0;
}

// Helper callback for avl_tree_print
static bool print_node_callback(AvlNode* node, void* ctx) {
    struct PrintContext* pc = (struct PrintContext*)ctx;
    printf("  [%d] key=%lu", pc->index++, node->property_id);
    if (pc->print_value) {
        printf(" value=");
        pc->print_value(node->declaration);
    }
    printf("\n");
    return true;
}

// Helper callback for avl_tree_clone
static bool clone_node_callback(AvlNode* node, void* ctx) {
    struct CloneContext* cc = (struct CloneContext*)ctx;
    void* cloned_value = NULL;
    
    if (cc->clone_value && node->declaration) {
        cloned_value = cc->clone_value(node->declaration, cc->pool);
    } else {
        cloned_value = node->declaration;
    }
    
    avl_tree_insert(cc->target, node->property_id, cloned_value);
    return true;
}

void avl_tree_print(AvlTree* tree, void (*print_value)(void* declaration)) {
    if (!tree) {
        printf("AVL Tree: NULL\n");
        return;
    }
    
    printf("AVL Tree (size=%d, height=%d):\n", 
           tree->node_count, tree->root ? node_height(tree->root) : 0);
    
    if (tree->node_count == 0) {
        printf("  (empty)\n");
        return;
    }
    
    // Simple in-order traversal for printing
    struct PrintContext context = { print_value, 0 };
    
    avl_tree_foreach_inorder(tree, print_node_callback, &context);
}

void avl_tree_get_stats(AvlTree* tree, AvlTreeStats* stats) {
    if (!tree || !stats) return;
    
    memset(stats, 0, sizeof(AvlTreeStats));
    
    if (!tree->root) {
        return;
    }
    
    stats->node_count = tree->node_count;
    stats->height = avl_tree_height(tree);
    
    int min_depth = -1;
    int max_depth = 0;
    int total_depth = 0;
    int node_count = 0;
    
    calculate_depth_stats(tree->root, 0, &min_depth, &max_depth, &total_depth, &node_count);
    
    stats->max_depth = max_depth;
    stats->min_depth = min_depth == -1 ? 0 : min_depth;
    stats->average_depth = node_count > 0 ? (double)total_depth / node_count : 0.0;
    
    validate_recursive(tree->root, &stats->balance_violations);
}

int avl_tree_bulk_insert(AvlTree* tree, uintptr_t* property_ids, void** declarations, int count) {
    if (!tree || !property_ids || !declarations || count <= 0) return 0;
    
    int successful = 0;
    for (int i = 0; i < count; i++) {
        if (avl_tree_insert(tree, property_ids[i], declarations[i])) {
            successful++;
        }
    }
    
    return successful;
}

AvlTree* avl_tree_clone(AvlTree* source, Pool* target_pool, 
                       void* (*clone_value)(void* declaration, Pool* pool)) {
    if (!source || !target_pool) return NULL;
    
    AvlTree* cloned = avl_tree_create(target_pool);
    if (!cloned) return NULL;
    
    struct CloneContext context = { cloned, clone_value, target_pool };
    
    avl_tree_foreach_inorder(source, clone_node_callback, &context);
    
    return cloned;
}

// Helper callback for avl_tree_merge
static bool merge_node_callback(AvlNode* node, void* ctx) {
    struct MergeContext* mc = (struct MergeContext*)ctx;
    
    AvlNode* existing = avl_tree_search(mc->target, node->property_id);
    if (existing) {
        // Conflict resolution
        if (mc->merge_conflict) {
            existing->declaration = mc->merge_conflict(existing->declaration, 
                                                    node->declaration, 
                                                    mc->target->pool);
        } else {
            existing->declaration = node->declaration;
        }
    } else {
        // No conflict, insert new
        avl_tree_insert(mc->target, node->property_id, node->declaration);
    }
    
    mc->merged_count++;
    return true;
}

int avl_tree_merge(AvlTree* target, AvlTree* source,
                  void* (*merge_conflict)(void* existing, void* new_value, Pool* pool)) {
    if (!target || !source) return 0;
    
    struct MergeContext context = { target, merge_conflict, 0 };
    
    avl_tree_foreach_inorder(source, merge_node_callback, &context);
    
    return context.merged_count;
}
