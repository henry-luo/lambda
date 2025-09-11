/**
 * @file test_arrays.c
 * @brief KLEE test harness for array operations in Lambda Script
 * @author Henry Luo
 * 
 * This test harness uses KLEE to discover array bounds violations,
 * null pointer dereferences, and other array-related issues.
 */

#include <klee/klee.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

// Mock Lambda array structures for testing
typedef struct TestItem {
    uint64_t raw_value;
    uint8_t type_id;
} TestItem;

typedef struct TestArray {
    uint8_t type_id;
    uint8_t flags;
    uint16_t ref_cnt;
    TestItem* items;
    int64_t length;
    int64_t capacity;
} TestArray;

typedef struct TestList {
    uint8_t type_id;
    uint8_t flags; 
    uint16_t ref_cnt;
    TestItem* items;
    int64_t length;
    int64_t extra;
    int64_t capacity;
} TestList;

#define TEST_TYPE_ARRAY 16
#define TEST_TYPE_LIST 12
#define TEST_TYPE_INT 3
#define MAX_TEST_SIZE 100

// Mock allocation for testing
TestItem* test_alloc_items(int64_t count) {
    if (count <= 0 || count > MAX_TEST_SIZE) {
        return NULL;
    }
    return (TestItem*)malloc(count * sizeof(TestItem));
}

// Instrumented array access function
TestItem test_array_get(TestArray* arr, int64_t index) {
    TestItem error_item = {0, 0};
    
    // Null pointer check
    if (!arr) {
        klee_assert(0); // Null array pointer
        return error_item;
    }
    
    // Check array structure integrity
    if (arr->type_id != TEST_TYPE_ARRAY) {
        klee_assert(0); // Invalid array type
        return error_item;
    }
    
    if (!arr->items) {
        klee_assert(0); // Null items pointer
        return error_item;
    }
    
    // Validate length field
    if (arr->length < 0) {
        klee_assert(0); // Negative array length
        return error_item;
    }
    
    if (arr->length > MAX_TEST_SIZE) {
        klee_assert(0); // Array length too large
        return error_item;
    }
    
    // Bounds checking
    if (index < 0) {
        klee_assert(0); // Negative index
        return error_item;
    }
    
    if (index >= arr->length) {
        klee_assert(0); // Index out of bounds
        return error_item;
    }
    
    return arr->items[index];
}

// Instrumented array set function
int test_array_set(TestArray* arr, int64_t index, TestItem value) {
    if (!arr) {
        klee_assert(0); // Null array pointer
        return -1;
    }
    
    if (arr->type_id != TEST_TYPE_ARRAY) {
        klee_assert(0); // Invalid array type
        return -1;
    }
    
    if (!arr->items) {
        klee_assert(0); // Null items pointer
        return -1;
    }
    
    if (arr->length < 0 || arr->length > MAX_TEST_SIZE) {
        klee_assert(0); // Invalid array length
        return -1;
    }
    
    if (index < 0) {
        klee_assert(0); // Negative index
        return -1;
    }
    
    if (index >= arr->length) {
        klee_assert(0); // Index out of bounds
        return -1;
    }
    
    arr->items[index] = value;
    return 0;
}

// Test array resize with overflow protection
int test_array_resize(TestArray* arr, int64_t new_size) {
    if (!arr) {
        klee_assert(0); // Null array pointer
        return -1;
    }
    
    if (new_size < 0) {
        klee_assert(0); // Negative new size
        return -1;
    }
    
    if (new_size > MAX_TEST_SIZE) {
        klee_assert(0); // New size too large
        return -1;
    }
    
    // Check for overflow in allocation size calculation
    if (new_size > SIZE_MAX / sizeof(TestItem)) {
        klee_assert(0); // Allocation size overflow
        return -1;
    }
    
    // Simulate realloc
    TestItem* new_items = test_alloc_items(new_size);
    if (!new_items && new_size > 0) {
        klee_assert(0); // Allocation failure
        return -1;
    }
    
    // Copy existing items
    int64_t copy_count = (arr->length < new_size) ? arr->length : new_size;
    for (int64_t i = 0; i < copy_count; i++) {
        new_items[i] = arr->items[i];
    }
    
    free(arr->items);
    arr->items = new_items;
    arr->length = new_size;
    arr->capacity = new_size;
    
    return 0;
}

// Test list append with capacity management
int test_list_append(TestList* list, TestItem item) {
    if (!list) {
        klee_assert(0); // Null list pointer
        return -1;
    }
    
    if (list->type_id != TEST_TYPE_LIST) {
        klee_assert(0); // Invalid list type
        return -1;
    }
    
    if (list->length < 0 || list->capacity < 0) {
        klee_assert(0); // Invalid length/capacity
        return -1;
    }
    
    if (list->length > MAX_TEST_SIZE || list->capacity > MAX_TEST_SIZE) {
        klee_assert(0); // Length/capacity too large
        return -1;
    }
    
    // Check if we need to grow the list
    if (list->length >= list->capacity) {
        // Calculate new capacity with overflow protection
        int64_t new_capacity = list->capacity * 2;
        if (new_capacity < list->capacity) { // Overflow check
            klee_assert(0); // Capacity overflow
            return -1;
        }
        
        if (new_capacity > MAX_TEST_SIZE) {
            new_capacity = MAX_TEST_SIZE;
        }
        
        if (list->length >= new_capacity) {
            klee_assert(0); // Cannot grow list further
            return -1;
        }
        
        // Resize the list
        TestItem* new_items = test_alloc_items(new_capacity);
        if (!new_items) {
            klee_assert(0); // Allocation failure
            return -1;
        }
        
        // Copy existing items
        for (int64_t i = 0; i < list->length; i++) {
            new_items[i] = list->items[i];
        }
        
        free(list->items);
        list->items = new_items;
        list->capacity = new_capacity;
    }
    
    // Add the new item
    if (!list->items) {
        klee_assert(0); // Null items after allocation
        return -1;
    }
    
    list->items[list->length] = item;
    list->length++;
    
    return 0;
}

// Test array slice operation
TestArray* test_array_slice(TestArray* arr, int64_t start, int64_t end) {
    if (!arr) {
        klee_assert(0); // Null array pointer
        return NULL;
    }
    
    if (arr->length < 0 || arr->length > MAX_TEST_SIZE) {
        klee_assert(0); // Invalid array length
        return NULL;
    }
    
    // Validate slice bounds
    if (start < 0) {
        klee_assert(0); // Negative start index
        return NULL;
    }
    
    if (end < start) {
        klee_assert(0); // End before start
        return NULL;
    }
    
    if (start > arr->length) {
        klee_assert(0); // Start index out of bounds
        return NULL;
    }
    
    if (end > arr->length) {
        klee_assert(0); // End index out of bounds
        return NULL;
    }
    
    int64_t slice_length = end - start;
    
    // Check for overflow in slice length
    if (slice_length > MAX_TEST_SIZE) {
        klee_assert(0); // Slice too large
        return NULL;
    }
    
    // Create new array for slice
    TestArray* slice = (TestArray*)malloc(sizeof(TestArray));
    if (!slice) {
        klee_assert(0); // Allocation failure
        return NULL;
    }
    
    slice->type_id = TEST_TYPE_ARRAY;
    slice->flags = 0;
    slice->ref_cnt = 1;
    slice->length = slice_length;
    slice->capacity = slice_length;
    
    if (slice_length > 0) {
        slice->items = test_alloc_items(slice_length);
        if (!slice->items) {
            klee_assert(0); // Items allocation failure
            free(slice);
            return NULL;
        }
        
        // Copy slice data
        for (int64_t i = 0; i < slice_length; i++) {
            slice->items[i] = arr->items[start + i];
        }
    } else {
        slice->items = NULL;
    }
    
    return slice;
}

int main() {
    TestArray arr;
    TestList list;
    TestItem items[MAX_TEST_SIZE];
    TestItem value;
    int64_t index, start, end, new_size;
    
    // Make array structure symbolic
    klee_make_symbolic(&arr, sizeof(arr), "array");
    klee_make_symbolic(&list, sizeof(list), "list");
    klee_make_symbolic(&value, sizeof(value), "item_value");
    klee_make_symbolic(&index, sizeof(index), "access_index");
    klee_make_symbolic(&start, sizeof(start), "slice_start");
    klee_make_symbolic(&end, sizeof(end), "slice_end");
    klee_make_symbolic(&new_size, sizeof(new_size), "new_size");
    
    // Add constraints
    klee_assume(arr.length >= 0 && arr.length <= MAX_TEST_SIZE);
    klee_assume(arr.capacity >= arr.length && arr.capacity <= MAX_TEST_SIZE);
    klee_assume(list.length >= 0 && list.length <= MAX_TEST_SIZE);
    klee_assume(list.capacity >= list.length && list.capacity <= MAX_TEST_SIZE);
    
    // Set up valid pointers
    arr.items = items;
    list.items = items;
    arr.type_id = TEST_TYPE_ARRAY;
    list.type_id = TEST_TYPE_LIST;
    
    // Test array access - KLEE will find out-of-bounds conditions
    TestItem result = test_array_get(&arr, index);
    
    // Test array modification - KLEE will find bounds violations
    int set_result = test_array_set(&arr, index, value);
    
    // Test array resize - KLEE will find overflow conditions
    int resize_result = test_array_resize(&arr, new_size);
    
    // Test list append - KLEE will find capacity issues
    int append_result = test_list_append(&list, value);
    
    // Test array slice - KLEE will find invalid slice bounds
    TestArray* slice = test_array_slice(&arr, start, end);
    if (slice) {
        free(slice->items);
        free(slice);
    }
    
    return 0;
}
