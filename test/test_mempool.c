/*
 * Comprehensive Memory Pool Test Suite (Unified C Version)
 * ========================================================
 *
 * Combined testatstatic int test_basic_allocation(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, \"Pool creation should succeed\");

    void* ptr = pool_alloc(pool, 1024);
    EXPECT_NOT_NULL(ptr, \"Basic allocation should succeed\");

    // Don't free individual allocations - arena cleanup will handle it
    pool_destroy(pool);
    return 1;
}_basic_calloc(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, \"Pool creation should succeed\");

    size_t size = 1024;
    char* ptr = (char*)pool_calloc(pool, 1, size);
    EXPECT_NOT_NULL(ptr, \"Basic calloc should succeed\");

    // Check that memory is zeroed
    for (size_t i = 0; i < size; i++) {
        EXPECT_TRUE(ptr[i] == 0, \"Calloc should zero memory\");
    }

    // Don't free individual allocations - arena cleanup will handle it
    pool_destroy(pool);
    return 1;
}jemalloc-based memory pool implementation,
 * incorporating all functionality from:
 * - test_mempool_simple_gtest.cpp (basic GTest cases converted to C)
 * - test_mempool_standalone.c (standalone C tests)
 * - test_mempool_comprehensive_gtest.cpp (comprehensive GTest cases converted to C)
 *
 * Test Coverage:
 * - Basic functionality (pool_alloc, pool_calloc, pool_free)
 * - Memory alignment and patterns
 * - Error handling and edge cases
 * - Performance and stress testing
 * - Memory safety and boundary conditions
 * - Large allocation scenarios
 * - Real-world usage patterns
 */

#include "../lib/mempool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <time.h>

// Test tracking
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// Test result macros
#define TEST_START(name) \
    do { \
        printf("[ RUN      ] %s\n", name); \
        tests_run++; \
    } while(0)

#define TEST_PASS(name) \
    do { \
        printf("[       OK ] %s\n", name); \
        tests_passed++; \
    } while(0)

#define TEST_FAIL(name, msg) \
    do { \
        printf("[  FAILED  ] %s: %s\n", name, msg); \
        tests_failed++; \
    } while(0)

#define EXPECT_TRUE(condition, msg) \
    do { \
        if (!(condition)) { \
            printf("EXPECTATION FAILED: %s\n", msg); \
            return 0; \
        } \
    } while(0)

#define EXPECT_FALSE(condition, msg) \
    do { \
        if (condition) { \
            printf("EXPECTATION FAILED: %s\n", msg); \
            return 0; \
        } \
    } while(0)

#define EXPECT_NOT_NULL(ptr, msg) \
    do { \
        if ((ptr) == NULL) { \
            printf("EXPECTATION FAILED: %s\n", msg); \
            return 0; \
        } \
    } while(0)

#define EXPECT_NULL(ptr, msg) \
    do { \
        if ((ptr) != NULL) { \
            printf("EXPECTATION FAILED: %s\n", msg); \
            return 0; \
        } \
    } while(0)

// Helper functions
static void fill_pattern(void* ptr, size_t size, uint8_t pattern) {
    uint8_t* bytes = (uint8_t*)ptr;
    for (size_t i = 0; i < size; i++) {
        bytes[i] = (uint8_t)(pattern + (i % 256));
    }
}

static int verify_pattern(void* ptr, size_t size, uint8_t pattern) {
    uint8_t* bytes = (uint8_t*)ptr;
    for (size_t i = 0; i < size; i++) {
        if (bytes[i] != (uint8_t)(pattern + (i % 256))) {
            return 0;
        }
    }
    return 1;
}

static int is_memory_accessible(void* ptr, size_t size) {
    if (!ptr || size == 0) return 0;

    // Try to write and read back data
    char* data = (char*)ptr;
    char test_value = 0x42;

    // Write test pattern
    for (size_t i = 0; i < size; i += 64) {  // Test every 64 bytes
        data[i] = test_value;
    }

    // Verify test pattern
    for (size_t i = 0; i < size; i += 64) {
        if (data[i] != test_value) {
            return 0;
        }
    }

    return 1;
}

// ========================================================================
// Basic Functionality Tests (from simple and standalone tests)
// ========================================================================

static int test_basic_allocation(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    void* ptr = pool_alloc(pool, 1024);
    EXPECT_NOT_NULL(ptr, "Basic allocation should succeed");
    pool_free(pool, ptr);

    pool_destroy(pool);
    return 1;
}

static int test_basic_calloc(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    size_t size = 1024;
    char* ptr = (char*)pool_calloc(pool, 1, size);
    EXPECT_NOT_NULL(ptr, "Basic calloc should succeed");

    // Check that memory is zeroed
    for (size_t i = 0; i < size; i++) {
        EXPECT_TRUE(ptr[i] == 0, "Calloc should zero memory");
    }

    pool_free(pool, ptr);
    pool_destroy(pool);
    return 1;
}

static int test_multiple_allocations(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    void* ptrs[10];

    // Allocate multiple blocks
    for (int i = 0; i < 10; i++) {
        ptrs[i] = pool_alloc(pool, 128 * (i + 1));
        EXPECT_NOT_NULL(ptrs[i], "Multiple allocations should succeed");
    }

    // Free all blocks
    for (int i = 0; i < 10; i++) {
        pool_free(pool, ptrs[i]);
    }

    pool_destroy(pool);
    return 1;
}

static int test_zero_size_allocation(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    void* ptr = pool_alloc(pool, 0);
    // jemalloc may return NULL or a valid pointer for size 0
    // Both behaviors are acceptable
    if (ptr) {
        pool_free(pool, ptr);
    }

    pool_destroy(pool);
    return 1;
}

static int test_zero_size_calloc(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    void* ptr = pool_calloc(pool, 0, 100);
    // Should handle zero gracefully
    if (ptr) {
        pool_free(pool, ptr);
    }

    ptr = pool_calloc(pool, 100, 0);
    if (ptr) {
        pool_free(pool, ptr);
    }

    pool_destroy(pool);
    return 1;
}

static int test_free_null_pointer(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    // Should not crash
    pool_free(pool, NULL);

    pool_destroy(pool);
    return 1;
}

// ========================================================================
// Advanced Functionality Tests (from comprehensive test)
// ========================================================================

static int test_large_allocations(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    // Test various large allocation sizes
    size_t large_sizes[] = {
        1024 * 1024,      // 1MB
        5 * 1024 * 1024,  // 5MB
        10 * 1024 * 1024  // 10MB
    };

    for (size_t i = 0; i < sizeof(large_sizes) / sizeof(large_sizes[0]); i++) {
        void* ptr = pool_alloc(pool, large_sizes[i]);
        EXPECT_NOT_NULL(ptr, "Large allocation should succeed");

        // Verify memory is accessible
        EXPECT_TRUE(is_memory_accessible(ptr, large_sizes[i]),
                   "Large allocated memory should be accessible");

        pool_free(pool, ptr);
    }

    pool_destroy(pool);
    return 1;
}

static int test_very_small_allocations(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    void* ptrs[100];

    // Allocate many small blocks
    for (int i = 0; i < 100; i++) {
        ptrs[i] = pool_alloc(pool, 1 + (i % 16));  // 1-16 bytes
        EXPECT_NOT_NULL(ptrs[i], "Small allocation should succeed");
    }

    // Free all
    for (int i = 0; i < 100; i++) {
        pool_free(pool, ptrs[i]);
    }

    pool_destroy(pool);
    return 1;
}

static int test_memory_alignment(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    void* ptrs[10];

    for (int i = 0; i < 10; i++) {
        ptrs[i] = pool_alloc(pool, 64 + i * 8);
        EXPECT_NOT_NULL(ptrs[i], "Alignment test allocation should succeed");

        // Check alignment (should be at least pointer-aligned)
        uintptr_t addr = (uintptr_t)ptrs[i];
        EXPECT_TRUE(addr % sizeof(void*) == 0, "Memory should be properly aligned");
    }

    for (int i = 0; i < 10; i++) {
        pool_free(pool, ptrs[i]);
    }

    pool_destroy(pool);
    return 1;
}

static int test_memory_integrity(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    const size_t size = 1024;
    const uint8_t pattern = 0xAA;

    void* ptr = pool_alloc(pool, size);
    EXPECT_NOT_NULL(ptr, "Memory integrity test allocation should succeed");

    // Fill with pattern
    fill_pattern(ptr, size, pattern);

    // Verify pattern
    EXPECT_TRUE(verify_pattern(ptr, size, pattern),
               "Memory should maintain data integrity");

    pool_free(pool, ptr);
    pool_destroy(pool);
    return 1;
}

static int test_rapid_allocation_deallocation(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    const int cycles = 50;  // Reduced for C version
    const int blocks_per_cycle = 10;

    for (int cycle = 0; cycle < cycles; cycle++) {
        void* ptrs[10];  // blocks_per_cycle

        // Rapid allocation
        for (int i = 0; i < blocks_per_cycle; i++) {
            ptrs[i] = pool_alloc(pool, 128);  // Fixed size for simplicity
            EXPECT_NOT_NULL(ptrs[i], "Rapid allocation should succeed");
        }

        // Write data to ensure memory is usable
        for (int i = 0; i < blocks_per_cycle; i++) {
            char* data = (char*)ptrs[i];
            snprintf(data, 128, "Cycle_%d_Block_%d", cycle, i);
        }

        // Verify data integrity
        for (int i = 0; i < blocks_per_cycle; i++) {
            char expected[50];
            snprintf(expected, sizeof(expected), "Cycle_%d_Block_%d", cycle, i);
            EXPECT_TRUE(strcmp((char*)ptrs[i], expected) == 0,
                       "Data integrity should be maintained");
        }

        // Rapid deallocation
        for (int i = 0; i < blocks_per_cycle; i++) {
            pool_free(pool, ptrs[i]);
        }
    }

    pool_destroy(pool);
    return 1;
}

static int test_fragmentation_stress(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    const int num_blocks = 50;  // Reduced for C version
    void* ptrs[50];

    // Allocate blocks of varying sizes
    for (int i = 0; i < num_blocks; i++) {
        size_t size = 32 + (i % 20) * 16;
        ptrs[i] = pool_alloc(pool, size);
        EXPECT_NOT_NULL(ptrs[i], "Fragmentation test allocation should succeed");

        // Fill with pattern
        fill_pattern(ptrs[i], size, 0xAA + (i % 4));
    }

    // Free every other block to create fragmentation
    for (int i = 1; i < num_blocks; i += 2) {
        pool_free(pool, ptrs[i]);
        ptrs[i] = NULL;
    }

    // Allocate new blocks in the gaps
    for (int i = 1; i < num_blocks; i += 2) {
        ptrs[i] = pool_alloc(pool, 64);
        EXPECT_NOT_NULL(ptrs[i], "Fragmentation gap allocation should succeed");
    }

    // Verify remaining data
    for (int i = 0; i < num_blocks; i += 2) {
        if (ptrs[i]) {
            size_t size = 32 + (i % 20) * 16;
            EXPECT_TRUE(verify_pattern(ptrs[i], size, 0xAA + (i % 4)),
                       "Original data should remain intact after fragmentation");
        }
    }

    // Free all remaining blocks
    for (int i = 0; i < num_blocks; i++) {
        if (ptrs[i]) {
            pool_free(pool, ptrs[i]);
        }
    }

    pool_destroy(pool);
    return 1;
}

static int test_power_of_two_sizes(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    void* ptrs[16];

    for (int i = 0; i < 16; i++) {
        size_t size = 1 << (i + 4);  // 16, 32, 64, ... up to 512KB
        ptrs[i] = pool_alloc(pool, size);
        EXPECT_NOT_NULL(ptrs[i], "Power of two allocation should succeed");

        // Write pattern
        fill_pattern(ptrs[i], size, 0x55);
    }

    // Verify patterns
    for (int i = 0; i < 16; i++) {
        size_t size = 1 << (i + 4);
        EXPECT_TRUE(verify_pattern(ptrs[i], size, 0x55),
                   "Power of two memory should maintain integrity");
    }

    // Free all
    for (int i = 0; i < 16; i++) {
        pool_free(pool, ptrs[i]);
    }

    pool_destroy(pool);
    return 1;
}

static int test_calloc_large_blocks(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    // Test calloc with large blocks
    size_t sizes[] = {1000, 10000, 100000};

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        char* ptr = (char*)pool_calloc(pool, 1, sizes[i]);
        EXPECT_NOT_NULL(ptr, "Large calloc should succeed");

        // Verify memory is zeroed
        for (size_t j = 0; j < sizes[i]; j += 64) {  // Check every 64th byte
            EXPECT_TRUE(ptr[j] == 0, "Large calloc should zero memory");
        }

        pool_free(pool, ptr);
    }

    pool_destroy(pool);
    return 1;
}

static int test_mixed_operations(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    void* ptrs[20];

    // Mixed allocation pattern
    for (int i = 0; i < 20; i++) {
        if (i % 3 == 0) {
            ptrs[i] = pool_alloc(pool, 128 + i * 8);
        } else if (i % 3 == 1) {
            ptrs[i] = pool_calloc(pool, 1, 64 + i * 4);
        } else {
            ptrs[i] = pool_alloc(pool, 256);
        }
        EXPECT_NOT_NULL(ptrs[i], "Mixed operation allocation should succeed");
    }

    // Free in random order (simplified pattern)
    int free_order[] = {3, 7, 1, 15, 9, 2, 18, 5, 12, 0, 8, 16, 4, 11, 19, 6, 13, 10, 17, 14};
    for (int i = 0; i < 20; i++) {
        pool_free(pool, ptrs[free_order[i]]);
    }

    pool_destroy(pool);
    return 1;
}

static int test_real_world_simulation(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    // Simulate web server-like allocation pattern
    void* request_buffers[10];
    void* response_buffers[10];
    void* temp_storage[5];

    // Allocate request buffers
    for (int i = 0; i < 10; i++) {
        request_buffers[i] = pool_alloc(pool, 4096);  // 4KB request buffer
        EXPECT_NOT_NULL(request_buffers[i], "Request buffer allocation should succeed");
    }

    // Allocate response buffers
    for (int i = 0; i < 10; i++) {
        response_buffers[i] = pool_alloc(pool, 8192);  // 8KB response buffer
        EXPECT_NOT_NULL(response_buffers[i], "Response buffer allocation should succeed");
    }

    // Allocate temporary storage
    for (int i = 0; i < 5; i++) {
        temp_storage[i] = pool_alloc(pool, 1024 + i * 512);
        EXPECT_NOT_NULL(temp_storage[i], "Temp storage allocation should succeed");
    }

    // Use the memory (write patterns)
    for (int i = 0; i < 10; i++) {
        fill_pattern(request_buffers[i], 4096, 0x11 + i);
        fill_pattern(response_buffers[i], 8192, 0x22 + i);
    }

    // Free temporary storage first (typical pattern)
    for (int i = 0; i < 5; i++) {
        pool_free(pool, temp_storage[i]);
    }

    // Verify data integrity
    for (int i = 0; i < 10; i++) {
        EXPECT_TRUE(verify_pattern(request_buffers[i], 4096, 0x11 + i),
                   "Request buffer data should remain intact");
        EXPECT_TRUE(verify_pattern(response_buffers[i], 8192, 0x22 + i),
                   "Response buffer data should remain intact");
    }

    // Free remaining buffers
    for (int i = 0; i < 10; i++) {
        pool_free(pool, request_buffers[i]);
        pool_free(pool, response_buffers[i]);
    }

    pool_destroy(pool);
    return 1;
}

static int test_pool_realloc(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    // Test basic realloc functionality
    void* ptr = pool_alloc(pool, 64);
    EXPECT_NOT_NULL(ptr, "Initial allocation should succeed");

    // Fill with pattern
    fill_pattern(ptr, 64, 0xAA);

    // Reallocate to larger size
    ptr = pool_realloc(pool, ptr, 128);
    EXPECT_NOT_NULL(ptr, "Realloc to larger size should succeed");

    // Verify original data is preserved
    EXPECT_TRUE(verify_pattern(ptr, 64, 0xAA), "Original data should be preserved after realloc");

    // Reallocate to smaller size
    ptr = pool_realloc(pool, ptr, 32);
    EXPECT_NOT_NULL(ptr, "Realloc to smaller size should succeed");

    // Verify partial data is preserved
    EXPECT_TRUE(verify_pattern(ptr, 32, 0xAA), "Partial data should be preserved after shrinking");

    // Test realloc from NULL (should behave like malloc)
    void* ptr2 = pool_realloc(pool, NULL, 256);
    EXPECT_NOT_NULL(ptr2, "Realloc from NULL should behave like malloc");

    // Test realloc to size 0 (should behave like free)
    void* ptr3 = pool_realloc(pool, ptr2, 0);
    // After realloc to size 0, ptr3 should be NULL (memory is freed)
    EXPECT_TRUE(ptr3 == NULL, "Realloc to zero size should return NULL");

    pool_free(pool, ptr);

    pool_destroy(pool);
    return 1;
}

static int test_realloc_stress(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    // Test multiple consecutive reallocs
    void* ptr = pool_alloc(pool, 10);
    EXPECT_NOT_NULL(ptr, "Initial allocation should succeed");
    strcpy((char*)ptr, "Hi");

    // Multiple reallocs with increasing sizes
    for (int i = 0; i < 10; i++) {
        size_t new_size = 20 + i * 30;
        ptr = pool_realloc(pool, ptr, new_size);
        EXPECT_NOT_NULL(ptr, "Realloc should succeed");
        EXPECT_TRUE(strncmp((char*)ptr, "Hi", 2) == 0, "Data should be preserved across reallocs");
    }

    // Test realloc down to very small size
    ptr = pool_realloc(pool, ptr, 5);
    EXPECT_NOT_NULL(ptr, "Realloc to small size should succeed");
    EXPECT_TRUE(strncmp((char*)ptr, "Hi", 2) == 0, "Data should be preserved when shrinking");

    pool_free(pool, ptr);
    pool_destroy(pool);
    return 1;
}

static int test_realloc_null_handling(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    // Test realloc with NULL pointer (should behave like malloc)
    void* ptr1 = pool_realloc(pool, NULL, 100);
    EXPECT_NOT_NULL(ptr1, "Realloc from NULL should work like alloc");
    strcpy((char*)ptr1, "Test");
    EXPECT_TRUE(strcmp((char*)ptr1, "Test") == 0, "Should be able to write to allocated memory");

    // Test realloc to zero size (should behave like free)
    void* ptr2 = pool_realloc(pool, ptr1, 0);
    EXPECT_TRUE(ptr2 == NULL, "Realloc to zero size should return NULL");

    pool_destroy(pool);
    return 1;
}

static int test_realloc_data_preservation(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    // Test data preservation during expansion
    void* ptr = pool_alloc(pool, 20);
    EXPECT_NOT_NULL(ptr, "Initial allocation should succeed");
    strcpy((char*)ptr, "Hello World!");

    ptr = pool_realloc(pool, ptr, 100);
    EXPECT_NOT_NULL(ptr, "Realloc expansion should succeed");
    EXPECT_TRUE(strcmp((char*)ptr, "Hello World!") == 0, "Data should be preserved during expansion");

    // Test data preservation during shrinking
    ptr = pool_realloc(pool, ptr, 12);
    EXPECT_NOT_NULL(ptr, "Realloc shrinking should succeed");
    EXPECT_TRUE(strncmp((char*)ptr, "Hello World!", 12) == 0, "Data should be preserved during shrinking");

    pool_free(pool, ptr);
    pool_destroy(pool);
    return 1;
}

static int test_multiple_pools_isolation(void) {
    // Test that multiple pools are properly isolated
    Pool* pool1 = pool_create();
    Pool* pool2 = pool_create();
    EXPECT_NOT_NULL(pool1, "First pool creation should succeed");
    EXPECT_NOT_NULL(pool2, "Second pool creation should succeed");

    // Allocate from both pools
    void* ptr1 = pool_alloc(pool1, 100);
    void* ptr2 = pool_alloc(pool2, 100);
    EXPECT_NOT_NULL(ptr1, "Allocation from pool1 should succeed");
    EXPECT_NOT_NULL(ptr2, "Allocation from pool2 should succeed");

    // Fill with different patterns
    fill_pattern(ptr1, 100, 0xAA);
    fill_pattern(ptr2, 100, 0xBB);

    // Verify isolation
    EXPECT_TRUE(verify_pattern(ptr1, 100, 0xAA), "Pool1 data should be preserved");
    EXPECT_TRUE(verify_pattern(ptr2, 100, 0xBB), "Pool2 data should be preserved");

    // Test realloc in both pools
    ptr1 = pool_realloc(pool1, ptr1, 200);
    ptr2 = pool_realloc(pool2, ptr2, 200);
    EXPECT_NOT_NULL(ptr1, "Realloc in pool1 should succeed");
    EXPECT_NOT_NULL(ptr2, "Realloc in pool2 should succeed");

    // Verify data is still preserved after realloc
    EXPECT_TRUE(verify_pattern(ptr1, 100, 0xAA), "Pool1 data should be preserved after realloc");
    EXPECT_TRUE(verify_pattern(ptr2, 100, 0xBB), "Pool2 data should be preserved after realloc");

    pool_free(pool1, ptr1);
    pool_free(pool2, ptr2);
    pool_destroy(pool1);
    pool_destroy(pool2);
    return 1;
}

static int test_invalid_pool_operations(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    void* ptr = pool_alloc(pool, 100);
    EXPECT_NOT_NULL(ptr, "Valid allocation should succeed");

    // Test operations with NULL pool
    void* null_ptr = pool_alloc(NULL, 100);
    EXPECT_TRUE(null_ptr == NULL, "Allocation with NULL pool should fail");

    void* null_realloc = pool_realloc(NULL, ptr, 200);
    EXPECT_TRUE(null_realloc == NULL, "Realloc with NULL pool should fail");

    // Test free with NULL pool (should not crash)
    pool_free(NULL, ptr);

    // Test operations with destroyed pool
    pool_destroy(pool);

    // After destruction, operations should fail gracefully
    void* destroyed_ptr = pool_alloc(pool, 100);
    EXPECT_TRUE(destroyed_ptr == NULL, "Allocation from destroyed pool should fail");

    return 1;
}

static int test_realloc_edge_cases(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    // Test realloc with same size
    void* ptr = pool_alloc(pool, 100);
    EXPECT_NOT_NULL(ptr, "Initial allocation should succeed");
    strcpy((char*)ptr, "Same size test");

    void* same_ptr = pool_realloc(pool, ptr, 100);
    EXPECT_NOT_NULL(same_ptr, "Realloc with same size should succeed");
    EXPECT_TRUE(strcmp((char*)same_ptr, "Same size test") == 0, "Data should be preserved with same size realloc");

    // Test very large realloc
    void* large_ptr = pool_realloc(pool, same_ptr, 10 * 1024 * 1024); // 10MB
    EXPECT_NOT_NULL(large_ptr, "Large realloc should succeed");
    EXPECT_TRUE(strncmp((char*)large_ptr, "Same size test", 14) == 0, "Data should be preserved in large realloc");

    // Test realloc back to small size
    void* small_ptr = pool_realloc(pool, large_ptr, 50);
    EXPECT_NOT_NULL(small_ptr, "Realloc back to small size should succeed");
    EXPECT_TRUE(strncmp((char*)small_ptr, "Same size test", 14) == 0, "Data should be preserved when shrinking from large");

    pool_free(pool, small_ptr);
    pool_destroy(pool);
    return 1;
}

static int test_arena_memory_efficiency(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    // Test that arena-based allocation is efficient
    void* ptrs[100];

    // Allocate many small blocks
    for (int i = 0; i < 100; i++) {
        ptrs[i] = pool_alloc(pool, 32 + (i % 16));
        EXPECT_NOT_NULL(ptrs[i], "Arena allocation should succeed");
        snprintf((char*)ptrs[i], 32 + (i % 16), "Block%d", i);
    }

    // Verify all allocations are valid
    for (int i = 0; i < 100; i++) {
        char expected[20];
        snprintf(expected, sizeof(expected), "Block%d", i);
        EXPECT_TRUE(strcmp((char*)ptrs[i], expected) == 0, "Arena memory should be properly allocated");
    }

    // Test realloc on several blocks
    for (int i = 0; i < 10; i++) {
        char expected[20];
        snprintf(expected, sizeof(expected), "Block%d", i);
        ptrs[i] = pool_realloc(pool, ptrs[i], 100 + i * 10);
        EXPECT_NOT_NULL(ptrs[i], "Arena realloc should succeed");
        EXPECT_TRUE(strncmp((char*)ptrs[i], expected, strlen(expected)) == 0, "Data should be preserved in arena realloc");
    }

    // Free all blocks
    for (int i = 0; i < 100; i++) {
        pool_free(pool, ptrs[i]);
    }

    pool_destroy(pool);
    return 1;
}

static int test_cross_pool_corruption_protection(void) {
    // Test that memory from one pool cannot corrupt another
    Pool* pool1 = pool_create();
    Pool* pool2 = pool_create();
    EXPECT_NOT_NULL(pool1, "First pool creation should succeed");
    EXPECT_NOT_NULL(pool2, "Second pool creation should succeed");

    void* ptr1 = pool_alloc(pool1, 100);
    void* ptr2 = pool_alloc(pool2, 100);
    EXPECT_NOT_NULL(ptr1, "Allocation from pool1 should succeed");
    EXPECT_NOT_NULL(ptr2, "Allocation from pool2 should succeed");

    strcpy((char*)ptr1, "Pool1 data");
    strcpy((char*)ptr2, "Pool2 data");

    // Attempt to free ptr1 using pool2 (should fail gracefully)
    pool_free(pool2, ptr1); // This should not corrupt anything

    // Verify both pointers are still valid and contain correct data
    EXPECT_TRUE(strcmp((char*)ptr1, "Pool1 data") == 0, "Pool1 data should remain intact");
    EXPECT_TRUE(strcmp((char*)ptr2, "Pool2 data") == 0, "Pool2 data should remain intact");

    // Proper cleanup
    pool_free(pool1, ptr1);
    pool_free(pool2, ptr2);
    pool_destroy(pool1);
    pool_destroy(pool2);
    return 1;
}

static int test_realloc_chain_operations(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    // Test a complex chain of realloc operations
    void* ptr = pool_realloc(pool, NULL, 50); // Start with NULL
    EXPECT_NOT_NULL(ptr, "Initial realloc from NULL should succeed");
    strcpy((char*)ptr, "Chain test");

    // Chain of size changes
    size_t sizes[] = {100, 25, 200, 75, 300, 50, 400, 30};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        ptr = pool_realloc(pool, ptr, sizes[i]);
        EXPECT_NOT_NULL(ptr, "Chained realloc should succeed");
        EXPECT_TRUE(strncmp((char*)ptr, "Chain test", 10) == 0, "Data should be preserved through realloc chain");
    }

    // End with realloc to 0 (free)
    ptr = pool_realloc(pool, ptr, 0);
    EXPECT_TRUE(ptr == NULL, "Final realloc to 0 should return NULL");

    pool_destroy(pool);
    return 1;
}

// ========================================================================
// Arena-Specific Tests
// ========================================================================

static int test_pool_creation(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");
    pool_destroy(pool);
    return 1;
}

static int test_pool_destruction(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    // Should not crash
    pool_destroy(pool);

    // Double destroy should be safe
    pool_destroy(pool);
    return 1;
}

static int test_null_pool_handling(void) {
    // Should not crash with NULL pool
    pool_destroy(NULL);

    void* ptr = pool_alloc(NULL, 1024);
    EXPECT_TRUE(ptr == NULL, "Allocation with NULL pool should fail");

    ptr = pool_calloc(NULL, 10, 100);
    EXPECT_TRUE(ptr == NULL, "Calloc with NULL pool should fail");

    // Should not crash
    pool_free(NULL, NULL);
    return 1;
}

static int test_multiple_pools_creation(void) {
    Pool* pool1 = pool_create();
    Pool* pool2 = pool_create();
    Pool* pool3 = pool_create();

    EXPECT_NOT_NULL(pool1, "First pool creation should succeed");
    EXPECT_NOT_NULL(pool2, "Second pool creation should succeed");
    EXPECT_NOT_NULL(pool3, "Third pool creation should succeed");

    // Allocate from different pools
    void* ptr1 = pool_alloc(pool1, 1024);
    void* ptr2 = pool_alloc(pool2, 2048);
    void* ptr3 = pool_alloc(pool3, 512);

    EXPECT_NOT_NULL(ptr1, "Allocation from pool1 should succeed");
    EXPECT_NOT_NULL(ptr2, "Allocation from pool2 should succeed");
    EXPECT_NOT_NULL(ptr3, "Allocation from pool3 should succeed");

    // Clean up
    pool_free(pool1, ptr1);
    pool_free(pool2, ptr2);
    pool_free(pool3, ptr3);

    pool_destroy(pool1);
    pool_destroy(pool2);
    pool_destroy(pool3);
    return 1;
}

static int test_pool_isolation(void) {
    Pool* pool1 = pool_create();
    Pool* pool2 = pool_create();

    EXPECT_NOT_NULL(pool1, "Pool1 creation should succeed");
    EXPECT_NOT_NULL(pool2, "Pool2 creation should succeed");

    // Allocate from both pools
    const size_t size = 1024;
    void* ptr1 = pool_alloc(pool1, size);
    void* ptr2 = pool_alloc(pool2, size);

    EXPECT_NOT_NULL(ptr1, "Allocation from pool1 should succeed");
    EXPECT_NOT_NULL(ptr2, "Allocation from pool2 should succeed");

    // Fill with different patterns
    fill_pattern(ptr1, size, 0x11);
    fill_pattern(ptr2, size, 0x22);

    // Verify patterns are preserved (pools are isolated)
    EXPECT_TRUE(verify_pattern(ptr1, size, 0x11), "Pool1 memory pattern should be preserved");
    EXPECT_TRUE(verify_pattern(ptr2, size, 0x22), "Pool2 memory pattern should be preserved");

    // Free from correct pools
    pool_free(pool1, ptr1);
    pool_free(pool2, ptr2);

    pool_destroy(pool1);
    pool_destroy(pool2);
    return 1;
}

static int test_pool_destruction_with_allocations(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    // Allocate some memory but don't free it explicitly
    void* ptr1 = pool_alloc(pool, 1024);
    void* ptr2 = pool_alloc(pool, 2048);
    void* ptr3 = pool_calloc(pool, 100, 32);

    EXPECT_NOT_NULL(ptr1, "Allocation 1 should succeed");
    EXPECT_NOT_NULL(ptr2, "Allocation 2 should succeed");
    EXPECT_NOT_NULL(ptr3, "Allocation 3 should succeed");

    // Fill with data to ensure allocations are valid
    strcpy((char*)ptr1, "Test data 1");
    strcpy((char*)ptr2, "Test data 2");
    strcpy((char*)ptr3, "Test data 3");

    // Destroy pool without explicitly freeing allocations
    // Arena-based implementation should clean up all memory automatically
    pool_destroy(pool);

    return 1;
}

// ========================================================================
// Test Suite Execution
// ========================================================================

typedef struct {
    const char* name;
    int (*test_func)(void);
} test_case_t;

static test_case_t test_cases[] = {
    // Basic functionality tests
    {"BasicAllocation", test_basic_allocation},
    {"BasicCalloc", test_basic_calloc},
    {"MultipleAllocations", test_multiple_allocations},
    {"ZeroSizeAllocation", test_zero_size_allocation},
    {"ZeroSizeCalloc", test_zero_size_calloc},
    {"FreeNullPointer", test_free_null_pointer},

    // Advanced functionality tests
    {"LargeAllocations", test_large_allocations},
    {"VerySmallAllocations", test_very_small_allocations},
    {"MemoryAlignment", test_memory_alignment},
    {"MemoryIntegrity", test_memory_integrity},
    {"RapidAllocationDeallocation", test_rapid_allocation_deallocation},
    {"FragmentationStress", test_fragmentation_stress},
    {"PowerOfTwoSizes", test_power_of_two_sizes},
    {"CallocLargeBlocks", test_calloc_large_blocks},
    {"MixedOperations", test_mixed_operations},
    {"RealWorldSimulation", test_real_world_simulation},
    {"PoolRealloc", test_pool_realloc},
    {"ReallocStress", test_realloc_stress},
    {"ReallocNullHandling", test_realloc_null_handling},
    {"ReallocDataPreservation", test_realloc_data_preservation},
    {"MultiplePoolsIsolation", test_multiple_pools_isolation},
    {"InvalidPoolOperations", test_invalid_pool_operations},
    {"ReallocEdgeCases", test_realloc_edge_cases},
    {"ArenaMemoryEfficiency", test_arena_memory_efficiency},
    {"CrossPoolCorruptionProtection", test_cross_pool_corruption_protection},
    {"ReallocChainOperations", test_realloc_chain_operations},
    {"PoolCreation", test_pool_creation},
    {"PoolDestruction", test_pool_destruction},
    {"NullPoolHandling", test_null_pool_handling},
    {"MultiplePoolsCreation", test_multiple_pools_creation},
    {"PoolIsolation", test_pool_isolation},
    {"PoolDestructionWithAllocations", test_pool_destruction_with_allocations},
};

static void run_all_tests(void) {
    printf("=== Comprehensive Memory Pool Test Suite ===\n");
    printf("Testing jemalloc-based arena memory pool implementation\n");
    printf("Features tested:\n");
    printf("  ✓ Basic allocation/deallocation (pool_alloc/pool_free)\n");
    printf("  ✓ Zero-initialized allocation (pool_calloc)\n");
    printf("  ✓ Memory reallocation (pool_realloc) - comprehensive testing\n");
    printf("  ✓ Memory pattern verification and coherency\n");
    printf("  ✓ Arena-based memory isolation and efficiency\n");
    printf("  ✓ Pool lifecycle management (creation/destruction)\n");
    printf("  ✓ Multi-pool creation and isolation verification\n");
    printf("  ✓ Stress testing and fragmentation handling\n");
    printf("  ✓ Large allocation and memory pressure testing\n");
    printf("  ✓ Edge cases and boundary conditions\n");
    printf("  ✓ Multi-pool isolation and corruption protection\n");
    printf("  ✓ Null handling and invalid operation protection\n");
    printf("  ✓ Real-world usage pattern simulation\n");
    printf("==========================================\n\n");

    size_t num_tests = sizeof(test_cases) / sizeof(test_cases[0]);

    printf("[==========] Running %zu tests\n", num_tests);

    for (size_t i = 0; i < num_tests; i++) {
        TEST_START(test_cases[i].name);

        if (test_cases[i].test_func()) {
            TEST_PASS(test_cases[i].name);
        } else {
            TEST_FAIL(test_cases[i].name, "Test function returned failure");
        }
    }

    printf("\n[==========] %d tests ran\n", tests_run);
    printf("[  PASSED  ] %d tests\n", tests_passed);
    if (tests_failed > 0) {
        printf("[  FAILED  ] %d tests\n", tests_failed);
    }
    printf("\n");

    if (tests_failed == 0) {
        printf("=== All tests passed! Jemalloc memory pool is working correctly ===\n");
    } else {
        printf("=== %d test(s) failed ===\n", tests_failed);
    }
}

int main(void) {
    run_all_tests();
    return (tests_failed == 0) ? 0 : 1;
}
