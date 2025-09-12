/*
 * KLEE Symbolic Execution Test: Real Lambda Script Vulnerability Patterns
 * 
 * This test targets actual patterns found in the Lambda Script codebase
 * that could lead to memory safety vulnerabilities similar to those
 * identified by KLEE in our generic tests.
 * 
 * Key patterns tested:
 * 1. ViewNode parent-child relationship use-after-free
 * 2. Tree-sitter node parent pointer dangling references
 * 3. Memory pool cleanup with dangling pointers
 * 4. Reference counting edge cases
 */

#include <klee/klee.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

// Memory tracking for KLEE analysis
#define MAX_ALLOCS 100
typedef struct {
    void* ptr;
    size_t size;
    int is_freed;
    const char* source_location;
} MemoryTracker;

static MemoryTracker allocations[MAX_ALLOCS];
static int alloc_count = 0;

void* tracked_malloc(size_t size, const char* location) {
    void* ptr = malloc(size);
    if (ptr && alloc_count < MAX_ALLOCS) {
        allocations[alloc_count] = (MemoryTracker){ptr, size, 0, location};
        alloc_count++;
    }
    return ptr;
}

void tracked_free(void* ptr, const char* location) {
    for (int i = 0; i < alloc_count; i++) {
        if (allocations[i].ptr == ptr) {
            if (allocations[i].is_freed) {
                assert(0 && "Double free detected");
            }
            allocations[i].is_freed = 1;
            break;
        }
    }
    free(ptr);
}

// Simplified ViewNode structure based on typeset/view/view_tree.c
typedef enum {
    VIEW_NODE_TEXT_RUN,
    VIEW_NODE_GROUP,
    VIEW_NODE_IMAGE,
    VIEW_NODE_CONTAINER
} ViewNodeType;

typedef struct ViewNode {
    ViewNodeType type;
    int ref_count;
    struct ViewNode* parent;
    struct ViewNode* first_child;
    struct ViewNode* last_child;
    struct ViewNode* next_sibling;
    struct ViewNode* prev_sibling;
    int child_count;
    char* id;
    char* class_name;
    
    // Simplified content (real code has union)
    void* content_data;
} ViewNode;

// Test 1: ViewNode parent-child use-after-free vulnerability
// Based on patterns in typeset/view/view_tree.c lines 163-230
ViewNode* view_node_create(ViewNodeType type) {
    ViewNode* node = tracked_malloc(sizeof(ViewNode), "view_node_create");
    if (!node) return NULL;
    
    memset(node, 0, sizeof(ViewNode));
    node->type = type;
    node->ref_count = 1;
    return node;
}

void view_node_retain(ViewNode* node) {
    if (node) {
        node->ref_count++;
    }
}

void view_node_release(ViewNode* node) {
    if (!node) return;
    
    node->ref_count--;
    if (node->ref_count <= 0) {
        // Release children - potential vulnerability here
        ViewNode* child = node->first_child;
        while (child) {
            ViewNode* next = child->next_sibling;
            view_node_release(child);
            child = next;
        }
        
        // Free content
        if (node->content_data) {
            tracked_free(node->content_data, "view_node_release_content");
        }
        
        // Free metadata
        if (node->id) tracked_free(node->id, "view_node_release_id");
        if (node->class_name) tracked_free(node->class_name, "view_node_release_class");
        
        tracked_free(node, "view_node_release");
    }
}

void view_node_add_child(ViewNode* parent, ViewNode* child) {
    if (!parent || !child) return;
    
    // Remove child from current parent if any
    if (child->parent) {
        // This could create dangling pointers
        ViewNode* old_parent = child->parent;
        if (old_parent->first_child == child) {
            old_parent->first_child = child->next_sibling;
        }
        if (old_parent->last_child == child) {
            old_parent->last_child = child->prev_sibling;
        }
        old_parent->child_count--;
    }
    
    // Set parent relationship
    child->parent = parent;
    
    // Add to parent's child list
    if (!parent->first_child) {
        parent->first_child = child;
        parent->last_child = child;
    } else {
        parent->last_child->next_sibling = child;
        child->prev_sibling = parent->last_child;
        parent->last_child = child;
    }
    
    parent->child_count++;
    view_node_retain(child);
}

// Test 2: Tree-sitter inspired node structure vulnerability
// Based on patterns in lambda/tree-sitter/lib/src/node.c and subtree.c
typedef struct TSNode {
    void* tree;
    void* subtree_ptr;
    struct TSNode* parent;
    struct TSNode** children;
    int child_count;
    int ref_count;
} TSNode;

TSNode* ts_node_create(void* tree_context) {
    TSNode* node = tracked_malloc(sizeof(TSNode), "ts_node_create");
    if (!node) return NULL;
    
    memset(node, 0, sizeof(TSNode));
    node->tree = tree_context;
    node->ref_count = 1;
    return node;
}

void ts_node_add_child(TSNode* parent, TSNode* child) {
    if (!parent || !child) return;
    
    parent->children = realloc(parent->children, 
                              sizeof(TSNode*) * (parent->child_count + 1));
    parent->children[parent->child_count] = child;
    child->parent = parent;
    parent->child_count++;
    child->ref_count++;
}

void ts_node_release(TSNode* node) {
    if (!node) return;
    
    node->ref_count--;
    if (node->ref_count <= 0) {
        // Release children
        for (int i = 0; i < node->child_count; i++) {
            if (node->children[i]) {
                // This could create dangling parent pointers
                node->children[i]->parent = NULL;
                ts_node_release(node->children[i]);
            }
        }
        
        if (node->children) {
            tracked_free(node->children, "ts_node_release_children");
        }
        tracked_free(node, "ts_node_release");
    }
}

// Test 3: Memory pool pattern vulnerability
// Based on lib/mem-pool patterns and lambda/lambda-mem.cpp
typedef struct PoolEntry {
    void* data;
    size_t size;
    int type_id;
    struct PoolEntry* next;
    int ref_count;
} PoolEntry;

typedef struct MemPool {
    PoolEntry* entries;
    int entry_count;
    int capacity;
} MemPool;

MemPool* pool_create(int capacity) {
    MemPool* pool = tracked_malloc(sizeof(MemPool), "pool_create");
    if (!pool) return NULL;
    
    pool->entries = tracked_malloc(sizeof(PoolEntry) * capacity, "pool_entries");
    pool->entry_count = 0;
    pool->capacity = capacity;
    return pool;
}

void* pool_alloc(MemPool* pool, size_t size, int type_id) {
    if (!pool || pool->entry_count >= pool->capacity) return NULL;
    
    void* data = tracked_malloc(size, "pool_alloc_data");
    if (!data) return NULL;
    
    PoolEntry* entry = &pool->entries[pool->entry_count];
    entry->data = data;
    entry->size = size;
    entry->type_id = type_id;
    entry->ref_count = 1;
    entry->next = NULL;
    
    pool->entry_count++;
    return data;
}

void pool_free_entry(MemPool* pool, void* ptr) {
    if (!pool || !ptr) return;
    
    for (int i = 0; i < pool->entry_count; i++) {
        if (pool->entries[i].data == ptr) {
            pool->entries[i].ref_count--;
            if (pool->entries[i].ref_count <= 0) {
                tracked_free(pool->entries[i].data, "pool_free_entry_data");
                pool->entries[i].data = NULL; // Important: clear pointer
            }
            return;
        }
    }
}

void pool_destroy(MemPool* pool) {
    if (!pool) return;
    
    // This pattern could leave dangling pointers if not handled carefully
    for (int i = 0; i < pool->entry_count; i++) {
        if (pool->entries[i].data && pool->entries[i].ref_count > 0) {
            tracked_free(pool->entries[i].data, "pool_destroy_data");
        }
    }
    
    tracked_free(pool->entries, "pool_destroy_entries");
    tracked_free(pool, "pool_destroy");
}

// KLEE test scenarios
void test_viewnode_vulnerability() {
    // Create symbolic operation selector
    int operation;
    klee_make_symbolic(&operation, sizeof(operation), "viewnode_operation");
    klee_assume(operation >= 0 && operation <= 3);
    
    ViewNode* root = view_node_create(VIEW_NODE_CONTAINER);
    ViewNode* child1 = view_node_create(VIEW_NODE_TEXT_RUN);
    ViewNode* child2 = view_node_create(VIEW_NODE_GROUP);
    
    if (root && child1 && child2) {
        view_node_add_child(root, child1);
        view_node_add_child(root, child2);
        
        // Simulate different vulnerability scenarios
        switch (operation) {
            case 0:
                // Normal cleanup
                view_node_release(root);
                break;
                
            case 1:
                // Release parent while children still have references
                view_node_retain(child1); // Child has extra reference
                view_node_release(root);
                
                // Try to access parent through child (potential use-after-free)
                if (child1->parent) {
                    ViewNode* parent = child1->parent;
                    // This could be accessing freed memory
                    int type = parent->type;
                }
                view_node_release(child1);
                break;
                
            case 2:
                // Double release scenario
                view_node_release(child1);
                view_node_release(root); // This might try to release child1 again
                break;
                
            case 3:
                // Circular reference scenario
                child1->content_data = child2; // Create reference
                view_node_retain(child2);
                view_node_release(root);
                break;
        }
    }
}

void test_tsnode_vulnerability() {
    int operation;
    klee_make_symbolic(&operation, sizeof(operation), "tsnode_operation");
    klee_assume(operation >= 0 && operation <= 2);
    
    TSNode* root = ts_node_create("tree_context");
    TSNode* child1 = ts_node_create("tree_context");
    TSNode* child2 = ts_node_create("tree_context");
    
    if (root && child1 && child2) {
        ts_node_add_child(root, child1);
        ts_node_add_child(root, child2);
        
        switch (operation) {
            case 0:
                // Release parent first
                ts_node_release(root);
                
                // Try to access parent from child (dangling pointer)
                if (child1->parent) {
                    // This parent pointer is now dangling
                    int child_count = child1->parent->child_count;
                }
                break;
                
            case 1:
                // Release child while parent still references it
                ts_node_release(child1);
                
                // Parent still has pointer to freed child
                if (root->child_count > 0 && root->children[0]) {
                    // This could be accessing freed memory
                    int ref_count = root->children[0]->ref_count;
                }
                break;
                
            case 2:
                // Complex release order
                child1->ref_count++; // Extra reference
                ts_node_release(root);
                ts_node_release(child1);
                break;
        }
    }
}

void test_pool_vulnerability() {
    int operation;
    klee_make_symbolic(&operation, sizeof(operation), "pool_operation");
    klee_assume(operation >= 0 && operation <= 2);
    
    MemPool* pool = pool_create(10);
    if (!pool) return;
    
    void* ptr1 = pool_alloc(pool, 64, 1);
    void* ptr2 = pool_alloc(pool, 128, 2);
    
    switch (operation) {
        case 0:
            // Free entry but pool still has pointer
            pool_free_entry(pool, ptr1);
            
            // Try to access through pool (use-after-free)
            for (int i = 0; i < pool->entry_count; i++) {
                if (pool->entries[i].data == ptr1) {
                    // This data pointer should be NULL but might not be
                    if (pool->entries[i].data) {
                        // Potential use-after-free
                        size_t size = pool->entries[i].size;
                    }
                }
            }
            pool_destroy(pool);
            break;
            
        case 1:
            // Destroy pool while entries still referenced
            pool_destroy(pool);
            
            // Try to use freed pool memory
            if (ptr1) {
                // This could access freed memory if pool cleanup was incomplete
                memset(ptr1, 0, 32);
            }
            break;
            
        case 2:
            // Double free scenario
            pool_free_entry(pool, ptr1);
            pool_free_entry(pool, ptr1); // Double free
            pool_destroy(pool);
            break;
    }
}

int main() {
    // Initialize memory tracking
    memset(allocations, 0, sizeof(allocations));
    alloc_count = 0;
    
    // Test different vulnerability patterns
    int test_selector;
    klee_make_symbolic(&test_selector, sizeof(test_selector), "test_selector");
    klee_assume(test_selector >= 0 && test_selector <= 2);
    
    switch (test_selector) {
        case 0:
            test_viewnode_vulnerability();
            break;
        case 1:
            test_tsnode_vulnerability();
            break;
        case 2:
            test_pool_vulnerability();
            break;
    }
    
    // Check for memory leaks
    for (int i = 0; i < alloc_count; i++) {
        if (!allocations[i].is_freed) {
            // Memory leak detected
            assert(0 && "Memory leak: allocation not freed");
        }
    }
    
    return 0;
}
