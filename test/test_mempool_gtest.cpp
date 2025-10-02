/*
 * Comprehensive Memory Pool Test Suite (GTest Version)
 * ===================================================
 *
 * GTest-based tests for jemalloc-based memory pool implementation,
 * incorporating all functionality from:
 * - test_mempool_simple_gtest.cpp (basic GTest cases)
 * - test_mempool_standalone.c (standalone C tests converted to GTest)
 * - test_mempool_comprehensive_gtest.cpp (comprehensive GTest cases)
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
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <ctime>

// ========================================================================
// Helper Functions
// ========================================================================

static void fill_pattern(void* ptr, size_t size, uint8_t pattern) {
    uint8_t* bytes = (uint8_t*)ptr;
    for (size_t i = 0; i < size; i++) {
        bytes[i] = (uint8_t)(pattern + (i % 256));
    }
}

static bool verify_pattern(void* ptr, size_t size, uint8_t pattern) {
    uint8_t* bytes = (uint8_t*)ptr;
    for (size_t i = 0; i < size; i++) {
        if (bytes[i] != (uint8_t)(pattern + (i % 256))) {
            return false;
        }
    }
    return true;
}

static bool is_memory_accessible(void* ptr, size_t size) {
    if (!ptr || size == 0) return false;

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
            return false;
        }
    }

    return true;
}

// ========================================================================
// Test Fixture
// ========================================================================

class MemoryPoolTest : public ::testing::Test {
protected:
    Pool* pool;

    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr) << "Pool creation should succeed";
    }

    void TearDown() override {
        if (pool) {
            pool_destroy(pool);
            pool = nullptr;
        }
    }
};

// ========================================================================
// Basic Functionality Tests
// ========================================================================

TEST_F(MemoryPoolTest, BasicAllocation) {
    void* ptr = pool_alloc(pool, 1024);
    EXPECT_NE(ptr, nullptr) << "Basic allocation should succeed";
    pool_free(pool, ptr);
}

TEST_F(MemoryPoolTest, BasicCalloc) {
    size_t size = 1024;
    char* ptr = (char*)pool_calloc(pool, size);
    EXPECT_NE(ptr, nullptr) << "Basic calloc should succeed";

    // Check that memory is zeroed
    for (size_t i = 0; i < size; i++) {
        EXPECT_EQ(ptr[i], 0) << "Calloc should zero memory at position " << i;
    }

    pool_free(pool, ptr);
}

TEST_F(MemoryPoolTest, MultipleAllocations) {
    void* ptrs[10];

    // Allocate multiple blocks
    for (int i = 0; i < 10; i++) {
        ptrs[i] = pool_alloc(pool, 128 * (i + 1));
        EXPECT_NE(ptrs[i], nullptr) << "Multiple allocations should succeed for block " << i;
    }

    // Free all blocks
    for (int i = 0; i < 10; i++) {
        pool_free(pool, ptrs[i]);
    }
}

TEST_F(MemoryPoolTest, ZeroSizeAllocation) {
    void* ptr = pool_alloc(pool, 0);
    // jemalloc may return NULL or a valid pointer for size 0
    // Both behaviors are acceptable
    if (ptr) {
        pool_free(pool, ptr);
    }
}

TEST_F(MemoryPoolTest, ZeroSizeCalloc) {
    void* ptr = pool_calloc(pool, 100);
    // Should handle zero gracefully
    if (ptr) {
        pool_free(pool, ptr);
    }

    ptr = pool_calloc(pool, 0);
    if (ptr) {
        pool_free(pool, ptr);
    }
}

TEST_F(MemoryPoolTest, FreeNullPointer) {
    // Should not crash
    pool_free(pool, nullptr);
}

// ========================================================================
// Advanced Functionality Tests
// ========================================================================

TEST_F(MemoryPoolTest, LargeAllocations) {
    // Test various large allocation sizes
    size_t large_sizes[] = {
        1024 * 1024,      // 1MB
        5 * 1024 * 1024,  // 5MB
        10 * 1024 * 1024  // 10MB
    };

    for (size_t i = 0; i < sizeof(large_sizes) / sizeof(large_sizes[0]); i++) {
        void* ptr = pool_alloc(pool, large_sizes[i]);
        EXPECT_NE(ptr, nullptr) << "Large allocation should succeed for size " << large_sizes[i];

        // Verify memory is accessible
        EXPECT_TRUE(is_memory_accessible(ptr, large_sizes[i]))
            << "Large allocated memory should be accessible for size " << large_sizes[i];

        pool_free(pool, ptr);
    }
}

TEST_F(MemoryPoolTest, VerySmallAllocations) {
    void* ptrs[100];

    // Allocate many small blocks
    for (int i = 0; i < 100; i++) {
        ptrs[i] = pool_alloc(pool, 1 + (i % 16));  // 1-16 bytes
        EXPECT_NE(ptrs[i], nullptr) << "Small allocation should succeed for block " << i;
    }

    // Free all
    for (int i = 0; i < 100; i++) {
        pool_free(pool, ptrs[i]);
    }
}

TEST_F(MemoryPoolTest, MemoryAlignment) {
    void* ptrs[10];

    for (int i = 0; i < 10; i++) {
        ptrs[i] = pool_alloc(pool, 64 + i * 8);
        EXPECT_NE(ptrs[i], nullptr) << "Alignment test allocation should succeed for block " << i;

        // Check alignment (should be at least pointer-aligned)
        uintptr_t addr = (uintptr_t)ptrs[i];
        EXPECT_EQ(addr % sizeof(void*), 0) << "Memory should be properly aligned for block " << i;
    }

    for (int i = 0; i < 10; i++) {
        pool_free(pool, ptrs[i]);
    }
}

TEST_F(MemoryPoolTest, MemoryIntegrity) {
    const size_t size = 1024;
    const uint8_t pattern = 0xAA;

    void* ptr = pool_alloc(pool, size);
    EXPECT_NE(ptr, nullptr) << "Memory integrity test allocation should succeed";

    // Fill with pattern
    fill_pattern(ptr, size, pattern);

    // Verify pattern
    EXPECT_TRUE(verify_pattern(ptr, size, pattern))
        << "Memory should maintain data integrity";

    pool_free(pool, ptr);
}

TEST_F(MemoryPoolTest, RapidAllocationDeallocation) {
    const int cycles = 50;
    const int blocks_per_cycle = 10;

    for (int cycle = 0; cycle < cycles; cycle++) {
        void* ptrs[10];  // blocks_per_cycle

        // Rapid allocation
        for (int i = 0; i < blocks_per_cycle; i++) {
            ptrs[i] = pool_alloc(pool, 128);  // Fixed size for simplicity
            EXPECT_NE(ptrs[i], nullptr) << "Rapid allocation should succeed for cycle " << cycle << ", block " << i;
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
            EXPECT_STREQ((char*)ptrs[i], expected)
                << "Data integrity should be maintained for cycle " << cycle << ", block " << i;
        }

        // Rapid deallocation
        for (int i = 0; i < blocks_per_cycle; i++) {
            pool_free(pool, ptrs[i]);
        }
    }
}

TEST_F(MemoryPoolTest, FragmentationStress) {
    const int num_blocks = 50;
    void* ptrs[50];

    // Allocate blocks of varying sizes
    for (int i = 0; i < num_blocks; i++) {
        size_t size = 32 + (i % 20) * 16;
        ptrs[i] = pool_alloc(pool, size);
        EXPECT_NE(ptrs[i], nullptr) << "Fragmentation test allocation should succeed for block " << i;

        // Fill with pattern
        fill_pattern(ptrs[i], size, 0xAA + (i % 4));
    }

    // Free every other block to create fragmentation
    for (int i = 1; i < num_blocks; i += 2) {
        pool_free(pool, ptrs[i]);
        ptrs[i] = nullptr;
    }

    // Allocate new blocks in the gaps
    for (int i = 1; i < num_blocks; i += 2) {
        ptrs[i] = pool_alloc(pool, 64);
        EXPECT_NE(ptrs[i], nullptr) << "Fragmentation gap allocation should succeed for block " << i;
    }

    // Verify remaining data
    for (int i = 0; i < num_blocks; i += 2) {
        if (ptrs[i]) {
            size_t size = 32 + (i % 20) * 16;
            EXPECT_TRUE(verify_pattern(ptrs[i], size, 0xAA + (i % 4)))
                << "Original data should remain intact after fragmentation for block " << i;
        }
    }

    // Free all remaining blocks
    for (int i = 0; i < num_blocks; i++) {
        if (ptrs[i]) {
            pool_free(pool, ptrs[i]);
        }
    }
}

TEST_F(MemoryPoolTest, PowerOfTwoSizes) {
    void* ptrs[16];

    for (int i = 0; i < 16; i++) {
        size_t size = 1 << (i + 4);  // 16, 32, 64, ... up to 512KB
        ptrs[i] = pool_alloc(pool, size);
        EXPECT_NE(ptrs[i], nullptr) << "Power of two allocation should succeed for size " << size;

        // Write pattern
        fill_pattern(ptrs[i], size, 0x55);
    }

    // Verify patterns
    for (int i = 0; i < 16; i++) {
        size_t size = 1 << (i + 4);
        EXPECT_TRUE(verify_pattern(ptrs[i], size, 0x55))
            << "Power of two memory should maintain integrity for size " << size;
    }

    // Free all
    for (int i = 0; i < 16; i++) {
        pool_free(pool, ptrs[i]);
    }
}

TEST_F(MemoryPoolTest, CallocLargeBlocks) {
    // Test calloc with large blocks
    size_t sizes[] = {1000, 10000, 100000};

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        char* ptr = (char*)pool_calloc(pool, sizes[i]);
        EXPECT_NE(ptr, nullptr) << "Large calloc should succeed for size " << sizes[i];

        // Verify memory is zeroed
        for (size_t j = 0; j < sizes[i]; j += 64) {  // Check every 64th byte
            EXPECT_EQ(ptr[j], 0) << "Large calloc should zero memory at position " << j << " for size " << sizes[i];
        }

        pool_free(pool, ptr);
    }
}

TEST_F(MemoryPoolTest, MixedOperations) {
    void* ptrs[20];

    // Mixed allocation pattern
    for (int i = 0; i < 20; i++) {
        if (i % 3 == 0) {
            ptrs[i] = pool_alloc(pool, 128 + i * 8);
        } else if (i % 3 == 1) {
            ptrs[i] = pool_calloc(pool, 64 + i * 4);
        } else {
            ptrs[i] = pool_alloc(pool, 256);
        }
        EXPECT_NE(ptrs[i], nullptr) << "Mixed operation allocation should succeed for block " << i;
    }

    // Free in random order (simplified pattern)
    int free_order[] = {3, 7, 1, 15, 9, 2, 18, 5, 12, 0, 8, 16, 4, 11, 19, 6, 13, 10, 17, 14};
    for (int i = 0; i < 20; i++) {
        pool_free(pool, ptrs[free_order[i]]);
    }
}

TEST_F(MemoryPoolTest, RealWorldSimulation) {
    // Simulate web server-like allocation pattern
    void* request_buffers[10];
    void* response_buffers[10];
    void* temp_storage[5];

    // Allocate request buffers
    for (int i = 0; i < 10; i++) {
        request_buffers[i] = pool_alloc(pool, 4096);  // 4KB request buffer
        EXPECT_NE(request_buffers[i], nullptr) << "Request buffer allocation should succeed for buffer " << i;
    }

    // Allocate response buffers
    for (int i = 0; i < 10; i++) {
        response_buffers[i] = pool_alloc(pool, 8192);  // 8KB response buffer
        EXPECT_NE(response_buffers[i], nullptr) << "Response buffer allocation should succeed for buffer " << i;
    }

    // Allocate temporary storage
    for (int i = 0; i < 5; i++) {
        temp_storage[i] = pool_alloc(pool, 1024 + i * 512);
        EXPECT_NE(temp_storage[i], nullptr) << "Temp storage allocation should succeed for storage " << i;
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
        EXPECT_TRUE(verify_pattern(request_buffers[i], 4096, 0x11 + i))
            << "Request buffer data should remain intact for buffer " << i;
        EXPECT_TRUE(verify_pattern(response_buffers[i], 8192, 0x22 + i))
            << "Response buffer data should remain intact for buffer " << i;
    }

    // Free remaining buffers
    for (int i = 0; i < 10; i++) {
        pool_free(pool, request_buffers[i]);
        pool_free(pool, response_buffers[i]);
    }
}

// ========================================================================
// Realloc Tests
// ========================================================================

TEST_F(MemoryPoolTest, PoolRealloc) {
    // Test basic realloc functionality
    void* ptr = pool_alloc(pool, 64);
    EXPECT_NE(ptr, nullptr) << "Initial allocation should succeed";

    // Fill with pattern
    fill_pattern(ptr, 64, 0xAA);

    // Reallocate to larger size
    ptr = pool_realloc(pool, ptr, 128);
    EXPECT_NE(ptr, nullptr) << "Realloc to larger size should succeed";

    // Verify original data is preserved
    EXPECT_TRUE(verify_pattern(ptr, 64, 0xAA)) << "Original data should be preserved after realloc";

    // Reallocate to smaller size
    ptr = pool_realloc(pool, ptr, 32);
    EXPECT_NE(ptr, nullptr) << "Realloc to smaller size should succeed";

    // Verify partial data is preserved
    EXPECT_TRUE(verify_pattern(ptr, 32, 0xAA)) << "Partial data should be preserved after shrinking";

    // Test realloc from NULL (should behave like malloc)
    void* ptr2 = pool_realloc(pool, nullptr, 256);
    EXPECT_NE(ptr2, nullptr) << "Realloc from NULL should behave like malloc";

    // Test realloc to size 0 (should behave like free)
    void* ptr3 = pool_realloc(pool, ptr2, 0);
    // After realloc to size 0, ptr3 should be NULL (memory is freed)
    EXPECT_EQ(ptr3, nullptr) << "Realloc to zero size should return NULL";

    pool_free(pool, ptr);
}

TEST_F(MemoryPoolTest, ReallocStress) {
    // Test multiple consecutive reallocs
    void* ptr = pool_alloc(pool, 10);
    EXPECT_NE(ptr, nullptr) << "Initial allocation should succeed";
    strcpy((char*)ptr, "Hi");

    // Multiple reallocs with increasing sizes
    for (int i = 0; i < 10; i++) {
        size_t new_size = 20 + i * 30;
        ptr = pool_realloc(pool, ptr, new_size);
        EXPECT_NE(ptr, nullptr) << "Realloc should succeed for iteration " << i;
        EXPECT_STREQ((char*)ptr, "Hi") << "Data should be preserved across reallocs for iteration " << i;
    }

    // Test realloc down to very small size
    ptr = pool_realloc(pool, ptr, 5);
    EXPECT_NE(ptr, nullptr) << "Realloc to small size should succeed";
    EXPECT_STREQ((char*)ptr, "Hi") << "Data should be preserved when shrinking";

    pool_free(pool, ptr);
}

TEST_F(MemoryPoolTest, ReallocNullHandling) {
    // Test realloc with NULL pointer (should behave like malloc)
    void* ptr1 = pool_realloc(pool, nullptr, 100);
    EXPECT_NE(ptr1, nullptr) << "Realloc from NULL should work like alloc";
    strcpy((char*)ptr1, "Test");
    EXPECT_STREQ((char*)ptr1, "Test") << "Should be able to write to allocated memory";

    // Test realloc to zero size (should behave like free)
    void* ptr2 = pool_realloc(pool, ptr1, 0);
    EXPECT_EQ(ptr2, nullptr) << "Realloc to zero size should return NULL";
}

TEST_F(MemoryPoolTest, ReallocDataPreservation) {
    // Test data preservation during expansion
    void* ptr = pool_alloc(pool, 20);
    EXPECT_NE(ptr, nullptr) << "Initial allocation should succeed";
    strcpy((char*)ptr, "Hello World!");

    ptr = pool_realloc(pool, ptr, 100);
    EXPECT_NE(ptr, nullptr) << "Realloc expansion should succeed";
    EXPECT_STREQ((char*)ptr, "Hello World!") << "Data should be preserved during expansion";

    // Test data preservation during shrinking
    ptr = pool_realloc(pool, ptr, 12);
    EXPECT_NE(ptr, nullptr) << "Realloc shrinking should succeed";
    EXPECT_EQ(strncmp((char*)ptr, "Hello World!", 12), 0) << "Data should be preserved during shrinking";

    pool_free(pool, ptr);
}

// ========================================================================
// Multi-Pool Tests
// ========================================================================

TEST(MemoryPoolMultiTest, MultiplePoolsIsolation) {
    // Test that multiple pools are properly isolated
    Pool* pool1 = pool_create();
    Pool* pool2 = pool_create();
    EXPECT_NE(pool1, nullptr) << "First pool creation should succeed";
    EXPECT_NE(pool2, nullptr) << "Second pool creation should succeed";

    // Allocate from both pools
    void* ptr1 = pool_alloc(pool1, 100);
    void* ptr2 = pool_alloc(pool2, 100);
    EXPECT_NE(ptr1, nullptr) << "Allocation from pool1 should succeed";
    EXPECT_NE(ptr2, nullptr) << "Allocation from pool2 should succeed";

    // Fill with different patterns
    fill_pattern(ptr1, 100, 0xAA);
    fill_pattern(ptr2, 100, 0xBB);

    // Verify isolation
    EXPECT_TRUE(verify_pattern(ptr1, 100, 0xAA)) << "Pool1 data should be preserved";
    EXPECT_TRUE(verify_pattern(ptr2, 100, 0xBB)) << "Pool2 data should be preserved";

    // Test realloc in both pools
    ptr1 = pool_realloc(pool1, ptr1, 200);
    ptr2 = pool_realloc(pool2, ptr2, 200);
    EXPECT_NE(ptr1, nullptr) << "Realloc in pool1 should succeed";
    EXPECT_NE(ptr2, nullptr) << "Realloc in pool2 should succeed";

    // Verify data is still preserved after realloc
    EXPECT_TRUE(verify_pattern(ptr1, 100, 0xAA)) << "Pool1 data should be preserved after realloc";
    EXPECT_TRUE(verify_pattern(ptr2, 100, 0xBB)) << "Pool2 data should be preserved after realloc";

    pool_free(pool1, ptr1);
    pool_free(pool2, ptr2);
    pool_destroy(pool1);
    pool_destroy(pool2);
}

TEST(MemoryPoolMultiTest, InvalidPoolOperations) {
    Pool* pool = pool_create();
    EXPECT_NE(pool, nullptr) << "Pool creation should succeed";

    void* ptr = pool_alloc(pool, 100);
    EXPECT_NE(ptr, nullptr) << "Valid allocation should succeed";

    // Test operations with NULL pool
    void* null_ptr = pool_alloc(nullptr, 100);
    EXPECT_EQ(null_ptr, nullptr) << "Allocation with NULL pool should fail";

    void* null_realloc = pool_realloc(nullptr, ptr, 200);
    EXPECT_EQ(null_realloc, nullptr) << "Realloc with NULL pool should fail";

    // Test free with NULL pool (should not crash)
    pool_free(nullptr, ptr);

    // Test operations with destroyed pool
    pool_destroy(pool);

    // After destruction, operations should fail gracefully
    void* destroyed_ptr = pool_alloc(pool, 100);
    EXPECT_EQ(destroyed_ptr, nullptr) << "Allocation from destroyed pool should fail";
}

// ========================================================================
// Additional Edge Case Tests
// ========================================================================

TEST_F(MemoryPoolTest, ReallocEdgeCases) {
    // Test realloc with same size
    void* ptr = pool_alloc(pool, 100);
    EXPECT_NE(ptr, nullptr) << "Initial allocation should succeed";
    strcpy((char*)ptr, "Same size test");

    void* same_ptr = pool_realloc(pool, ptr, 100);
    EXPECT_NE(same_ptr, nullptr) << "Realloc with same size should succeed";
    EXPECT_STREQ((char*)same_ptr, "Same size test") << "Data should be preserved with same size realloc";

    // Test very large realloc
    void* large_ptr = pool_realloc(pool, same_ptr, 10 * 1024 * 1024); // 10MB
    EXPECT_NE(large_ptr, nullptr) << "Large realloc should succeed";
    EXPECT_EQ(strncmp((char*)large_ptr, "Same size test", 14), 0) << "Data should be preserved in large realloc";

    // Test realloc back to small size
    void* small_ptr = pool_realloc(pool, large_ptr, 50);
    EXPECT_NE(small_ptr, nullptr) << "Realloc back to small size should succeed";
    EXPECT_EQ(strncmp((char*)small_ptr, "Same size test", 14), 0) << "Data should be preserved when shrinking from large";

    pool_free(pool, small_ptr);
}

TEST_F(MemoryPoolTest, ArenaMemoryEfficiency) {
    // Test that arena-based allocation is efficient
    void* ptrs[100];

    // Allocate many small blocks
    for (int i = 0; i < 100; i++) {
        ptrs[i] = pool_alloc(pool, 32 + (i % 16));
        EXPECT_NE(ptrs[i], nullptr) << "Arena allocation should succeed for block " << i;
        snprintf((char*)ptrs[i], 32 + (i % 16), "Block%d", i);
    }

    // Verify all allocations are valid
    for (int i = 0; i < 100; i++) {
        char expected[20];
        snprintf(expected, sizeof(expected), "Block%d", i);
        EXPECT_STREQ((char*)ptrs[i], expected) << "Arena memory should be properly allocated for block " << i;
    }

    // Test realloc on several blocks
    for (int i = 0; i < 10; i++) {
        char expected[20];
        snprintf(expected, sizeof(expected), "Block%d", i);
        ptrs[i] = pool_realloc(pool, ptrs[i], 100 + i * 10);
        EXPECT_NE(ptrs[i], nullptr) << "Arena realloc should succeed for block " << i;
        EXPECT_EQ(strncmp((char*)ptrs[i], expected, strlen(expected)), 0) << "Data should be preserved in arena realloc for block " << i;
    }

    // Free all blocks
    for (int i = 0; i < 100; i++) {
        pool_free(pool, ptrs[i]);
    }
}

TEST(MemoryPoolBasicTest, PoolCreation) {
    Pool* pool = pool_create();
    EXPECT_NE(pool, nullptr) << "Pool creation should succeed";
    pool_destroy(pool);
}

TEST(MemoryPoolBasicTest, PoolDestruction) {
    Pool* pool = pool_create();
    EXPECT_NE(pool, nullptr) << "Pool creation should succeed";

    // Should not crash
    pool_destroy(pool);

    // Double destroy should be safe
    pool_destroy(pool);
}

TEST(MemoryPoolBasicTest, NullPoolHandling) {
    // Should not crash with NULL pool
    pool_destroy(nullptr);

    void* ptr = pool_alloc(nullptr, 1024);
    EXPECT_EQ(ptr, nullptr) << "Allocation with NULL pool should fail";

    ptr = pool_calloc(nullptr, 100);
    EXPECT_EQ(ptr, nullptr) << "Calloc with NULL pool should fail";

    // Should not crash
    pool_free(nullptr, nullptr);
}

TEST(MemoryPoolBasicTest, MultiplePoolsCreation) {
    Pool* pool1 = pool_create();
    Pool* pool2 = pool_create();
    Pool* pool3 = pool_create();

    EXPECT_NE(pool1, nullptr) << "First pool creation should succeed";
    EXPECT_NE(pool2, nullptr) << "Second pool creation should succeed";
    EXPECT_NE(pool3, nullptr) << "Third pool creation should succeed";

    // Allocate from different pools
    void* ptr1 = pool_alloc(pool1, 1024);
    void* ptr2 = pool_alloc(pool2, 2048);
    void* ptr3 = pool_alloc(pool3, 512);

    EXPECT_NE(ptr1, nullptr) << "Allocation from pool1 should succeed";
    EXPECT_NE(ptr2, nullptr) << "Allocation from pool2 should succeed";
    EXPECT_NE(ptr3, nullptr) << "Allocation from pool3 should succeed";

    // Clean up
    pool_free(pool1, ptr1);
    pool_free(pool2, ptr2);
    pool_free(pool3, ptr3);

    pool_destroy(pool1);
    pool_destroy(pool2);
    pool_destroy(pool3);
}

TEST(MemoryPoolBasicTest, PoolIsolation) {
    Pool* pool1 = pool_create();
    Pool* pool2 = pool_create();

    EXPECT_NE(pool1, nullptr) << "Pool1 creation should succeed";
    EXPECT_NE(pool2, nullptr) << "Pool2 creation should succeed";

    // Allocate from both pools
    const size_t size = 1024;
    void* ptr1 = pool_alloc(pool1, size);
    void* ptr2 = pool_alloc(pool2, size);

    EXPECT_NE(ptr1, nullptr) << "Allocation from pool1 should succeed";
    EXPECT_NE(ptr2, nullptr) << "Allocation from pool2 should succeed";

    // Fill with different patterns
    fill_pattern(ptr1, size, 0x11);
    fill_pattern(ptr2, size, 0x22);

    // Verify patterns are preserved (pools are isolated)
    EXPECT_TRUE(verify_pattern(ptr1, size, 0x11)) << "Pool1 memory pattern should be preserved";
    EXPECT_TRUE(verify_pattern(ptr2, size, 0x22)) << "Pool2 memory pattern should be preserved";

    // Free from correct pools
    pool_free(pool1, ptr1);
    pool_free(pool2, ptr2);

    pool_destroy(pool1);
    pool_destroy(pool2);
}

TEST(MemoryPoolBasicTest, PoolDestructionWithAllocations) {
    Pool* pool = pool_create();
    EXPECT_NE(pool, nullptr) << "Pool creation should succeed";

    // Allocate some memory but don't free it explicitly
    void* ptr1 = pool_alloc(pool, 1024);
    void* ptr2 = pool_alloc(pool, 2048);
    void* ptr3 = pool_calloc(pool, 32);

    EXPECT_NE(ptr1, nullptr) << "Allocation 1 should succeed";
    EXPECT_NE(ptr2, nullptr) << "Allocation 2 should succeed";
    EXPECT_NE(ptr3, nullptr) << "Allocation 3 should succeed";

    // Fill with data to ensure allocations are valid
    strcpy((char*)ptr1, "Test data 1");
    strcpy((char*)ptr2, "Test data 2");
    strcpy((char*)ptr3, "Test data 3");

    // Destroy pool without explicitly freeing allocations
    // Arena-based implementation should clean up all memory automatically
    pool_destroy(pool);
}

// ========================================================================
// Main Entry Point
// ========================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
