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
    // ptr3 may be NULL or a minimal allocation - both are valid

    pool_free(pool, ptr);
    if (ptr3) {
        pool_free(pool, ptr3);
    }

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
};

static void run_all_tests(void) {
    printf("=== Comprehensive Memory Pool Test Suite ===\n");
    printf("Testing jemalloc-based memory pool implementation\n");
    printf("Features tested:\n");
    printf("  ✓ Basic allocation/deallocation (pool_alloc/pool_free)\n");
    printf("  ✓ Zero-initialized allocation (pool_calloc)\n");
    printf("  ✓ Memory reallocation (pool_realloc)\n");
    printf("  ✓ Memory pattern verification and coherency\n");
    printf("  ✓ Stress testing and fragmentation handling\n");
    printf("  ✓ Large allocation and memory pressure testing\n");
    printf("  ✓ Edge cases and boundary conditions\n");
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
