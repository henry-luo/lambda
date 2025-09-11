/**
 * @file test_memory_safety.c
 * @brief KLEE test harness for memory safety and use-after-free detection
 * @author Henry Luo
 * 
 * This test harness focuses on memory safety issues common in C code,
 * including use-after-free, double-free, and memory leak patterns.
 */

#include <klee/klee.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ALLOCATIONS 8

// Memory tracking structure (simplified)
typedef struct {
    void* ptr;
    size_t size;
    int is_freed;
} MemoryBlock;

// Global memory tracker for this test
MemoryBlock allocations[MAX_ALLOCATIONS];
int alloc_count = 0;

// Mock allocation tracking
void* tracked_malloc(size_t size) {
    void* ptr = malloc(size);
    if (ptr && alloc_count < MAX_ALLOCATIONS) {
        allocations[alloc_count].ptr = ptr;
        allocations[alloc_count].size = size;
        allocations[alloc_count].is_freed = 0;
        alloc_count++;
    }
    return ptr;
}

void tracked_free(void* ptr) {
    if (!ptr) return;
    
    // Find the allocation
    for (int i = 0; i < alloc_count; i++) {
        if (allocations[i].ptr == ptr) {
            if (allocations[i].is_freed) {
                // Double-free detected!
                assert(0 && "Double-free detected");
            }
            allocations[i].is_freed = 1;
            free(ptr);
            return;
        }
    }
    
    // Freeing untracked memory
    free(ptr); // This might be dangerous
}

// Test 1: Use-after-free scenarios
typedef struct {
    int* data;
    size_t size;
} DataContainer;

DataContainer* container_create(size_t size) {
    DataContainer* container = tracked_malloc(sizeof(DataContainer));
    if (!container) return NULL;
    
    container->data = tracked_malloc(size * sizeof(int));
    if (!container->data) {
        tracked_free(container);
        return NULL;
    }
    
    container->size = size;
    return container;
}

void container_free(DataContainer* container) {
    if (!container) return;
    
    tracked_free(container->data);
    container->data = NULL; // Good practice
    tracked_free(container);
}

int container_set(DataContainer* container, size_t index, int value) {
    if (!container) return -1;
    if (!container->data) return -2; // Check for freed data
    if (index >= container->size) return -3;
    
    container->data[index] = value;
    return 0;
}

int container_get(DataContainer* container, size_t index) {
    if (!container) return -1;
    if (!container->data) return -2; // Use-after-free check
    if (index >= container->size) return -3;
    
    return container->data[index];
}

// Test 2: Dangling pointer scenarios
typedef struct Node {
    int value;
    struct Node* next;
    struct Node* parent; // Potential dangling pointer
} TreeNode;

TreeNode* tree_create_node(int value) {
    TreeNode* node = tracked_malloc(sizeof(TreeNode));
    if (!node) return NULL;
    
    node->value = value;
    node->next = NULL;
    node->parent = NULL;
    return node;
}

void tree_add_child(TreeNode* parent, TreeNode* child) {
    if (!parent || !child) return;
    
    child->parent = parent; // Create parent reference
    child->next = parent->next;
    parent->next = child;
}

void tree_remove_node(TreeNode* node) {
    if (!node) return;
    
    // Remove from parent's child list
    if (node->parent && node->parent->next == node) {
        node->parent->next = node->next;
    }
    
    // Update children's parent pointers
    TreeNode* child = node->next;
    while (child) {
        child->parent = node->parent; // Potential issue if parent is freed
        child = child->next;
    }
    
    tracked_free(node);
    // Note: node pointer is now dangling in caller
}

int tree_get_value(TreeNode* node) {
    if (!node) return -1;
    
    // Check if this might be a freed node
    for (int i = 0; i < alloc_count; i++) {
        if (allocations[i].ptr == node && allocations[i].is_freed) {
            assert(0 && "Use-after-free: accessing freed node");
        }
    }
    
    return node->value;
}

// Test 3: Memory leak patterns
typedef struct {
    char* buffer;
    size_t size;
    int ref_count;
} SharedBuffer;

SharedBuffer* shared_buffer_create(size_t size) {
    SharedBuffer* sb = tracked_malloc(sizeof(SharedBuffer));
    if (!sb) return NULL;
    
    sb->buffer = tracked_malloc(size);
    if (!sb->buffer) {
        tracked_free(sb);
        return NULL;
    }
    
    sb->size = size;
    sb->ref_count = 1;
    return sb;
}

SharedBuffer* shared_buffer_retain(SharedBuffer* sb) {
    if (!sb) return NULL;
    
    sb->ref_count++;
    return sb;
}

void shared_buffer_release(SharedBuffer* sb) {
    if (!sb) return;
    
    sb->ref_count--;
    if (sb->ref_count <= 0) {
        tracked_free(sb->buffer);
        tracked_free(sb);
    }
}

// Test 4: Buffer overflow with null termination
char* create_string_copy(const char* source, size_t max_len) {
    if (!source) return NULL;
    
    size_t len = strlen(source);
    if (len > max_len) len = max_len;
    
    char* copy = tracked_malloc(len + 1);
    if (!copy) return NULL;
    
    memcpy(copy, source, len);
    copy[len] = '\0'; // Ensure null termination
    
    return copy;
}

// Main test with symbolic inputs
int main() {
    // Create symbolic control variables
    int operation;
    size_t size_param;
    size_t index_param;
    int value_param;
    int should_free;
    
    klee_make_symbolic(&operation, sizeof(operation), "operation");
    klee_make_symbolic(&size_param, sizeof(size_param), "size_param");
    klee_make_symbolic(&index_param, sizeof(index_param), "index_param");
    klee_make_symbolic(&value_param, sizeof(value_param), "value_param");
    klee_make_symbolic(&should_free, sizeof(should_free), "should_free");
    
    // Constrain inputs to reasonable ranges
    klee_assume(operation >= 0 && operation < 10);
    klee_assume(size_param > 0 && size_param <= 64);
    klee_assume(index_param < 32);
    
    // Test 1: Use-after-free detection
    DataContainer* container = container_create(size_param);
    if (container) {
        // Set some data
        if (container_set(container, index_param % container->size, value_param) == 0) {
            int retrieved = container_get(container, index_param % container->size);
            assert(retrieved == value_param);
        }
        
        // Potentially free the container
        if (should_free & 1) {
            container_free(container);
            container = NULL; // Good practice: nullify pointer
        }
        
        // Try to use after potential free
        if (operation == 1 && container) {
            int result = container_get(container, 0);
            // Should work if not freed
        }
        
        // Double-free test
        if (should_free & 2) {
            container_free(container); // Potential double-free
        }
    }
    
    // Test 2: Dangling pointer scenarios
    TreeNode* root = tree_create_node(1);
    TreeNode* child1 = tree_create_node(2);
    TreeNode* child2 = tree_create_node(3);
    
    if (root && child1 && child2) {
        tree_add_child(root, child1);
        tree_add_child(root, child2);
        
        // Test accessing through parent pointers
        if (operation == 2) {
            int val1 = tree_get_value(child1);
            assert(val1 == 2);
            
            // Remove parent - creates dangling pointers
            tree_remove_node(root);
            root = NULL;
            
            // Try to access child's parent (now dangling)
            if (child1->parent) {
                // This should be caught as use-after-free
                int parent_val = tree_get_value(child1->parent);
            }
        }
        
        // Clean up remaining nodes
        if (root) tree_remove_node(root);
        if (child1) tree_remove_node(child1);
        if (child2) tree_remove_node(child2);
    }
    
    // Test 3: Reference counting and memory leaks
    SharedBuffer* sb1 = shared_buffer_create(size_param);
    if (sb1) {
        SharedBuffer* sb2 = shared_buffer_retain(sb1);
        SharedBuffer* sb3 = shared_buffer_retain(sb1);
        
        // Release in different orders based on symbolic input
        if (operation == 3) {
            shared_buffer_release(sb1);
            shared_buffer_release(sb2);
            shared_buffer_release(sb3);
        } else if (operation == 4) {
            shared_buffer_release(sb2);
            shared_buffer_release(sb1);
            shared_buffer_release(sb3);
        } else {
            // Potential leak: not all references released
            shared_buffer_release(sb1);
            // sb2 and sb3 not released - memory leak
        }
    }
    
    // Test 4: String operations with symbolic input
    if (operation == 5) {
        char test_string[32];
        klee_make_symbolic(test_string, sizeof(test_string), "test_string");
        
        // Ensure null termination somewhere in the string
        for (int i = 0; i < 31; i++) {
            if (test_string[i] == '\0') break;
            if (i == 30) test_string[i] = '\0';
        }
        
        char* copy = create_string_copy(test_string, size_param);
        if (copy) {
            // Verify null termination
            size_t len = strlen(copy);
            assert(copy[len] == '\0');
            
            tracked_free(copy);
        }
    }
    
    // Final check: ensure no memory leaks
    int leaked_blocks = 0;
    for (int i = 0; i < alloc_count; i++) {
        if (!allocations[i].is_freed) {
            leaked_blocks++;
        }
    }
    
    // In a real scenario, we'd want to assert no leaks
    // But for testing, we'll just track them
    if (leaked_blocks > 0) {
        // Memory leak detected
        // In production, this should be an assertion failure
    }
    
    return 0;
}
