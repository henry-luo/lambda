/**
 * @file test_null_pointers.c
 * @brief KLEE test harness for null pointer vulnerability detection
 * @author Henry Luo
 * 
 * This test harness uses KLEE symbolic execution to discover null pointer
 * dereference vulnerabilities in typical Lambda Script coding patterns.
 */

#include <klee/klee.h>
#include <assert.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#define MAX_BUFFER_SIZE 64
#define MAX_ARRAY_SIZE 16

// Simulate Lambda Script data structures and patterns
typedef struct {
    char* data;
    size_t size;
    size_t capacity;
} Buffer;

typedef struct {
    int* items;
    size_t count;
    size_t capacity;
} IntArray;

typedef struct Node {
    int value;
    struct Node* next;
} Node;

// Test 1: Buffer operations with potential null pointers
int buffer_init(Buffer* buf, size_t initial_capacity) {
    if (!buf) return -1; // Check for null buffer pointer
    
    buf->data = malloc(initial_capacity);
    if (!buf->data) return -2; // Allocation failure
    
    buf->size = 0;
    buf->capacity = initial_capacity;
    return 0;
}

int buffer_append(Buffer* buf, const char* str) {
    // Missing null checks - KLEE should find these
    size_t len = strlen(str); // Potential null dereference of str
    
    if (buf->size + len >= buf->capacity) {
        // Reallocation needed
        char* new_data = realloc(buf->data, buf->capacity * 2);
        if (!new_data) return -1;
        buf->data = new_data;
        buf->capacity *= 2;
    }
    
    memcpy(buf->data + buf->size, str, len); // Potential null dereference
    buf->size += len;
    buf->data[buf->size] = '\0';
    return 0;
}

void buffer_free(Buffer* buf) {
    if (buf) { // Good: checking for null
        free(buf->data); // But what if buf->data is already null?
        buf->data = NULL;
        buf->size = 0;
        buf->capacity = 0;
    }
}

// Test 2: Array operations with null pointer risks
int array_create(IntArray* arr, size_t capacity) {
    if (!arr) return -1;
    
    arr->items = malloc(capacity * sizeof(int));
    if (!arr->items) return -2;
    
    arr->count = 0;
    arr->capacity = capacity;
    return 0;
}

int array_get(IntArray* arr, size_t index) {
    // Multiple null pointer risks
    if (index >= arr->count) return -1; // Missing arr null check
    return arr->items[index]; // Potential double null dereference
}

int array_push(IntArray* arr, int value) {
    if (!arr) return -1;
    
    if (arr->count >= arr->capacity) {
        // Resize array
        int* new_items = realloc(arr->items, arr->capacity * 2 * sizeof(int));
        if (!new_items) return -2;
        arr->items = new_items;
        arr->capacity *= 2;
    }
    
    arr->items[arr->count++] = value; // What if arr->items is null after failed realloc?
    return 0;
}

// Test 3: Linked list operations (common null pointer issues)
Node* list_create(int value) {
    Node* node = malloc(sizeof(Node));
    if (!node) return NULL;
    
    node->value = value;
    node->next = NULL;
    return node;
}

int list_append(Node** head, int value) {
    if (!head) return -1; // Check head pointer
    
    Node* new_node = list_create(value);
    if (!new_node) return -2;
    
    if (!*head) {
        *head = new_node;
        return 0;
    }
    
    Node* current = *head;
    while (current->next) { // What if current becomes null somehow?
        current = current->next;
    }
    
    current->next = new_node;
    return 0;
}

Node* list_find(Node* head, int value) {
    Node* current = head; // No null check for head
    
    while (current) {
        if (current->value == value) {
            return current;
        }
        current = current->next; // What if current->next is corrupted?
    }
    
    return NULL;
}

void list_free(Node* head) {
    while (head) {
        Node* temp = head;
        head = head->next;
        free(temp); // What if temp is already freed or corrupted?
    }
}

// Test 4: String processing with null risks
int string_concat(char* dest, size_t dest_size, const char* src) {
    if (!dest || !src) return -1; // Good null checks
    
    size_t dest_len = strlen(dest); // But what if dest is not null-terminated?
    size_t src_len = strlen(src);
    
    if (dest_len + src_len >= dest_size) return -2;
    
    strcat(dest, src); // Double risk: dest and src validity
    return 0;
}

char* string_duplicate(const char* str) {
    if (!str) return NULL;
    
    size_t len = strlen(str);
    char* copy = malloc(len + 1);
    // Missing null check for malloc result
    
    strcpy(copy, str); // Potential null dereference of copy
    return copy;
}

// Main test function with symbolic inputs
int main() {
    // Make various pointers symbolic to test null pointer scenarios
    Buffer* buf_ptr;
    IntArray* arr_ptr;
    Node* list_ptr;
    char* str_ptr;
    
    klee_make_symbolic(&buf_ptr, sizeof(buf_ptr), "buf_ptr");
    klee_make_symbolic(&arr_ptr, sizeof(arr_ptr), "arr_ptr");
    klee_make_symbolic(&list_ptr, sizeof(list_ptr), "list_ptr");
    klee_make_symbolic(&str_ptr, sizeof(str_ptr), "str_ptr");
    
    // Test buffer operations with potentially null pointers
    Buffer local_buf;
    
    // Test 1: Buffer initialization with null pointer
    int result1 = buffer_init(buf_ptr, 32);
    if (buf_ptr != NULL) {
        assert(result1 == 0 || result1 == -2); // Should succeed or fail allocation
    } else {
        assert(result1 == -1); // Should detect null pointer
    }
    
    // Test 2: Buffer append with null string
    if (buffer_init(&local_buf, 32) == 0) {
        int result2 = buffer_append(&local_buf, str_ptr);
        // KLEE should explore both null and non-null str_ptr cases
        buffer_free(&local_buf);
    }
    
    // Test 3: Array operations with null pointer
    IntArray local_arr;
    if (array_create(&local_arr, 8) == 0) {
        // Test array_get with potentially corrupted array
        if (arr_ptr != NULL) {
            int val = array_get(arr_ptr, 0); // Should check arr_ptr validity
        }
        
        // Test array access after potential corruption
        int push_result = array_push(&local_arr, 42);
        if (push_result == 0) {
            int get_result = array_get(&local_arr, 0);
            assert(get_result == 42);
        }
        
        free(local_arr.items);
    }
    
    // Test 4: Linked list with null pointers
    Node* head = NULL;
    
    if (list_append(&head, 10) == 0) {
        Node* found = list_find(head, 10);
        assert(found != NULL);
        assert(found->value == 10);
        
        // Test with potentially null head
        Node* found2 = list_find(list_ptr, 10);
        // KLEE should explore null list_ptr case
        
        list_free(head);
    }
    
    // Test 5: String operations with null pointers
    char buffer[64] = "Hello";
    
    if (str_ptr != NULL) {
        // Assume str_ptr points to valid memory for this test
        klee_assume((uintptr_t)str_ptr > 0x1000); // Not in null page
        int concat_result = string_concat(buffer, sizeof(buffer), str_ptr);
        // KLEE should explore various str_ptr validity scenarios
    }
    
    // Test string duplication with null
    char* dup = string_duplicate(str_ptr);
    if (str_ptr != NULL && (uintptr_t)str_ptr > 0x1000) {
        // If str_ptr was valid, dup should be valid too (if malloc succeeded)
        if (dup != NULL) {
            assert(strlen(dup) == strlen(str_ptr));
            free(dup);
        }
    }
    
    return 0;
}
