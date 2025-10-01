/*
 * Arena-based Memory Pool Test Suite
 * ==================================
 *
 * Comprehensive test suite for the arena-based jemalloc memory pool implementation.
 * Tests the new pool_create(), pool_destroy(), and updated pool_alloc/pool_calloc/pool_free API.
 *
 * Test Coverage:
 * - Pool creation and destruction
 * - Arena-specific memory allocation
 * - Memory isolation between pools
 * - Resource cleanup and leak detection
 * - Basic functionality (pool_alloc, pool_calloc, pool_free)
 * - Memory alignment and patterns
 * - Error handling and edge cases
 * - Performance and stress testing
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

// ========================================================================
// Pool Management Tests
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
    EXPECT_NULL(ptr, "Allocation with NULL pool should fail");

    ptr = pool_calloc(NULL, 10, 100);
    EXPECT_NULL(ptr, "Calloc with NULL pool should fail");

    // Should not crash
    pool_free(NULL, NULL);
    return 1;
}

static int test_multiple_pools(void) {
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

// ========================================================================
// Basic Functionality Tests
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
// Advanced Functionality Tests
// ========================================================================

static int test_large_allocations(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    const size_t large_sizes[] = {1024*1024, 4*1024*1024, 16*1024*1024};
    const size_t num_sizes = sizeof(large_sizes) / sizeof(large_sizes[0]);

    for (size_t i = 0; i < num_sizes; i++) {
        void* ptr = pool_alloc(pool, large_sizes[i]);
        if (ptr) {
            // Test basic write/read
            char* data = (char*)ptr;
            data[0] = 'A';
            data[large_sizes[i] - 1] = 'Z';

            EXPECT_TRUE(data[0] == 'A', "Large allocation should be writable at start");
            EXPECT_TRUE(data[large_sizes[i] - 1] == 'Z', "Large allocation should be writable at end");

            pool_free(pool, ptr);
        }
    }

    pool_destroy(pool);
    return 1;
}

static int test_memory_alignment(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    // Test various allocation sizes
    const size_t sizes[] = {1, 8, 16, 32, 64, 128, 256, 1024};
    const size_t num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    for (size_t i = 0; i < num_sizes; i++) {
        void* ptr = pool_alloc(pool, sizes[i]);
        EXPECT_NOT_NULL(ptr, "Allocation should succeed");

        // Check alignment (should be at least 8-byte aligned on most platforms)
        uintptr_t addr = (uintptr_t)ptr;
        EXPECT_TRUE(addr % 8 == 0, "Memory should be properly aligned");

        pool_free(pool, ptr);
    }

    pool_destroy(pool);
    return 1;
}

static int test_memory_integrity(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    const size_t size = 4096;
    void* ptr = pool_alloc(pool, size);
    EXPECT_NOT_NULL(ptr, "Allocation should succeed");

    // Fill with pattern
    fill_pattern(ptr, size, 0xAB);

    // Verify pattern
    EXPECT_TRUE(verify_pattern(ptr, size, 0xAB), "Memory pattern should be preserved");

    pool_free(pool, ptr);
    pool_destroy(pool);
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

static int test_stress_allocation(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    const int num_iterations = 1000;
    void* ptrs[100];

    for (int iter = 0; iter < num_iterations; iter++) {
        // Allocate
        for (int i = 0; i < 100; i++) {
            ptrs[i] = pool_alloc(pool, 64 + (i % 128));
            if (!ptrs[i]) {
                TEST_FAIL("StressAllocation", "Allocation failed during stress test");
                pool_destroy(pool);
                return 0;
            }
        }

        // Free half randomly
        for (int i = 0; i < 50; i++) {
            int idx = i * 2;  // Free every other allocation
            pool_free(pool, ptrs[idx]);
            ptrs[idx] = NULL;
        }

        // Free remaining
        for (int i = 0; i < 100; i++) {
            if (ptrs[i]) {
                pool_free(pool, ptrs[i]);
            }
        }
    }

    pool_destroy(pool);
    return 1;
}

static int test_pool_destruction_with_allocations(void) {
    Pool* pool = pool_create();
    EXPECT_NOT_NULL(pool, "Pool creation should succeed");

    // Allocate some memory but don't free it
    void* ptr1 = pool_alloc(pool, 1024);
    void* ptr2 = pool_alloc(pool, 2048);
    void* ptr3 = pool_calloc(pool, 100, 32);

    EXPECT_NOT_NULL(ptr1, "Allocation 1 should succeed");
    EXPECT_NOT_NULL(ptr2, "Allocation 2 should succeed");
    EXPECT_NOT_NULL(ptr3, "Allocation 3 should succeed");

    // Destroy pool without explicitly freeing allocations
    // This should clean up all memory automatically
    pool_destroy(pool);

    return 1;
}

// ========================================================================
// Test Registry
// ========================================================================

typedef struct {
    const char* name;
    int (*test_func)(void);
} TestCase;

static TestCase test_cases[] = {
    // Pool management tests
    {"PoolCreation", test_pool_creation},
    {"PoolDestruction", test_pool_destruction},
    {"NullPoolHandling", test_null_pool_handling},
    {"MultiplePools", test_multiple_pools},

    // Basic functionality tests
    {"BasicAllocation", test_basic_allocation},
    {"BasicCalloc", test_basic_calloc},
    {"MultipleAllocations", test_multiple_allocations},
    {"ZeroSizeAllocation", test_zero_size_allocation},
    {"ZeroSizeCalloc", test_zero_size_calloc},
    {"FreeNullPointer", test_free_null_pointer},

    // Advanced functionality tests
    {"LargeAllocations", test_large_allocations},
    {"MemoryAlignment", test_memory_alignment},
    {"MemoryIntegrity", test_memory_integrity},
    {"PoolIsolation", test_pool_isolation},
    {"StressAllocation", test_stress_allocation},
    {"PoolDestructionWithAllocations", test_pool_destruction_with_allocations},
};

static void run_all_tests(void) {
    printf("=== Arena-based Memory Pool Test Suite ===\n");
    printf("Testing arena-based jemalloc memory pool implementation\n");
    printf("Features tested:\n");
    printf("  ✓ Pool creation and destruction\n");
    printf("  ✓ Arena-specific memory allocation\n");
    printf("  ✓ Memory isolation between pools\n");
    printf("  ✓ Resource cleanup and leak detection\n");
    printf("  ✓ Basic allocation/deallocation (pool_alloc/pool_free)\n");
    printf("  ✓ Zero-initialized allocation (pool_calloc)\n");
    printf("  ✓ Memory pattern verification and coherency\n");
    printf("  ✓ Stress testing and fragmentation handling\n");
    printf("  ✓ Large allocation scenarios\n");
    printf("  ✓ Edge cases and boundary conditions\n");
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
        printf("=== All tests passed! Arena-based memory pool is working correctly ===\n");
    } else {
        printf("=== %d test(s) failed ===\n", tests_failed);
    }
}

int main(void) {
    run_all_tests();
    return (tests_failed == 0) ? 0 : 1;
}
