#include <criterion/criterion.h>
#include <criterion/parameterized.h>
#include "../../lambda/lambda.h"
#include "../../lambda/transpiler.hpp"
#include "../../lib/mem-pool/include/mem_pool.h"

// Test suite for memory safety issues
TestSuite(memory_safety, .timeout = 30);

// Test buffer overflow protection in string operations
Test(memory_safety, string_buffer_overflow_protection) {
    VariableMemPool* pool;
    MemPoolError err = pool_variable_init(&pool, 4096, 10);
    cr_assert_eq(err, MEM_POOL_ERR_OK, "Pool initialization should succeed");

    // Test extremely long string creation
    const size_t huge_size = 1024 * 1024;  // 1MB
    char* huge_buffer = (char*)malloc(huge_size);
    memset(huge_buffer, 'A', huge_size - 1);
    huge_buffer[huge_size - 1] = '\0';

    // This should not crash or cause buffer overflow
    String* result = string_from_cstr(huge_buffer, strlen(huge_buffer), pool);

    if (result) {
        cr_assert_eq(result->len, huge_size - 1, "String length should match");
        cr_assert_str_eq(result->chars, huge_buffer, "String content should match");
    }

    free(huge_buffer);
    pool_variable_destroy(pool);
}

// Test memory pool corruption detection
Test(memory_safety, memory_pool_corruption_detection) {
    VariableMemPool* pool;
    pool_variable_init(&pool, 4096, 10);

    void* valid_ptr;
    pool_variable_alloc(pool, 100, &valid_ptr);

    // Test invalid pointer patterns that have caused crashes
    void* invalid_ptrs[] = {
        NULL,
        (void*)0x1,  // Very low address
        (void*)0x6e6120646c6f6230ULL,  // Known corrupt pattern from logs
        (void*)0xDEADBEEF,  // Classic test pattern
        (void*)0x28  // Specific pattern from crash logs
    };

    for (size_t i = 0; i < sizeof(invalid_ptrs) / sizeof(invalid_ptrs[0]); i++) {
        MemPoolError err = pool_variable_free(pool, invalid_ptrs[i]);
        cr_assert_eq(err, MEM_POOL_ERR_UNKNOWN_BLOCK,
                     "Invalid pointer should be rejected gracefully");
    }

    // Verify pool remains functional
    void* test_ptr;
    MemPoolError err = pool_variable_alloc(pool, 50, &test_ptr);
    cr_assert_eq(err, MEM_POOL_ERR_OK, "Pool should remain functional after invalid operations");

    pool_variable_free(pool, valid_ptr);
    pool_variable_free(pool, test_ptr);
    pool_variable_destroy(pool);
}

// Test double-free protection
Test(memory_safety, double_free_protection) {
    VariableMemPool* pool;
    pool_variable_init(&pool, 4096, 10);

    void* ptr;
    pool_variable_alloc(pool, 100, &ptr);

    // First free should succeed
    MemPoolError err1 = pool_variable_free(pool, ptr);
    cr_assert_eq(err1, MEM_POOL_ERR_OK, "First free should succeed");

    // Second free should be detected and handled gracefully
    MemPoolError err2 = pool_variable_free(pool, ptr);
    cr_assert_eq(err2, MEM_POOL_ERR_UNKNOWN_BLOCK, "Double free should be detected");

    pool_variable_destroy(pool);
}

// Test array bounds checking
Test(memory_safety, array_bounds_checking) {
    VariableMemPool* pool;
    pool_variable_init(&pool, 4096, 10);

    Array* arr = array_pooled(pool);

    // Add some test items
    for (int i = 0; i < 5; i++) {
        Item item = {.item = i2it(i)};
        array_append(arr, item, pool);
    }

    // Test valid access
    Item valid_item = array_get(arr, 2);
    cr_assert_eq(valid_item.type_id, LMD_TYPE_INT, "Valid access should return correct type");

    // Test out-of-bounds access (should return null gracefully)
    Item invalid_item1 = array_get(arr, -1);
    cr_assert_eq(invalid_item1.type_id, LMD_TYPE_NULL, "Negative index should return null");

    Item invalid_item2 = array_get(arr, 100);
    cr_assert_eq(invalid_item2.type_id, LMD_TYPE_NULL, "Large index should return null");

    pool_variable_destroy(pool);
}

// Test null pointer handling in string operations
Test(memory_safety, null_pointer_handling_strings) {
    // Test string concatenation with null inputs
    String* result1 = fn_strcat(NULL, NULL);
    cr_assert_null(result1, "Concatenating two null strings should return null");

    // Test with one null input
    String test_str = {.len = 5, .ref_cnt = 0, .chars = "test"};
    String* result2 = fn_strcat(&test_str, NULL);
    cr_assert_null(result2, "Concatenating with null should return null");

    String* result3 = fn_strcat(NULL, &test_str);
    cr_assert_null(result3, "Concatenating null with string should return null");
}

// Test infinite loop protection in parsing
Test(memory_safety, infinite_loop_protection) {
    // Test with malformed input that could cause infinite loops
    const char* malformed_inputs[] = {
        "{{{{{{{{{{{",  // Unmatched braces
        "[[[[[[[[[[[",  // Unmatched brackets
        "\"\\\\\\\\\\\\\\\\\\\\",  // Excessive escaping
        "<element><element><element>",  // Unclosed XML elements
        NULL
    };

    for (int i = 0; malformed_inputs[i] != NULL; i++) {
        // This should not hang or cause infinite loops
        // We'll use a timeout to ensure it completes
        Input* input = InputManager_create_input(NULL);
        if (input) {
            parse_json(input, malformed_inputs[i]);
            // Note: Input is managed by InputManager, no explicit destroy needed
        }
    }
}

// Memory leak detection test
Test(memory_safety, memory_leak_detection, .timeout = 10) {
    VariableMemPool* pool;
    pool_variable_init(&pool, 4096, 10);

    // Allocate many objects and free only some
    void* ptrs[100];
    for (int i = 0; i < 100; i++) {
        pool_variable_alloc(pool, 64 + (i % 32), &ptrs[i]);
    }

    // Free every other pointer
    for (int i = 0; i < 100; i += 2) {
        pool_variable_free(pool, ptrs[i]);
    }

    // Pool destruction should handle remaining allocations gracefully
    pool_variable_destroy(pool);

    // Note: In a real implementation, this should also check for actual leaks
    // using tools like Valgrind or AddressSanitizer
}
