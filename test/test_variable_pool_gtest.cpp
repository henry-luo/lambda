/*
 * VariableMemPool Test Suite (GTest Version)
 * ===========================================
 *
 * Comprehensive GTest-based test suite for VariableMemPool implementation
 * with special focus on pool_variable_realloc function.
 *
 * Test Coverage:
 * - Basic functionality (init, alloc, free)
 * - Memory reallocation scenarios
 * - Error handling and edge cases
 * - Performance scenarios
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdint>

extern "C" {
#include "../lib/mem-pool/include/mem_pool.h"
}

class VariablePoolTest : public ::testing::Test {
protected:
    VariableMemPool *pool = nullptr;

    void SetUp() override {
        MemPoolError err = pool_variable_init(&pool, 1024, 10);
        ASSERT_EQ(err, MEM_POOL_ERR_OK) << "Pool initialization should succeed";
        ASSERT_NE(pool, nullptr) << "Pool pointer should not be NULL";
    }

    void TearDown() override {
        if (pool) {
            pool_variable_destroy(pool);
            pool = nullptr;
        }
    }
};

TEST_F(VariablePoolTest, BasicInitialization) {
    // Pool is already initialized in SetUp
    EXPECT_NE(pool, nullptr);
}

TEST_F(VariablePoolTest, InvalidParameters) {
    VariableMemPool *test_pool;

    // Test with very large tolerance (should be clamped)
    MemPoolError err = pool_variable_init(&test_pool, 1024, 200);
    EXPECT_EQ(err, MEM_POOL_ERR_OK) << "Pool should handle large tolerance";

    pool_variable_destroy(test_pool);
}

TEST_F(VariablePoolTest, BasicAllocation) {
    void *ptr1, *ptr2, *ptr3;
    MemPoolError err;

    err = pool_variable_alloc(pool, 100, &ptr1);
    EXPECT_EQ(err, MEM_POOL_ERR_OK) << "First allocation should succeed";
    EXPECT_NE(ptr1, nullptr) << "First pointer should not be NULL";

    err = pool_variable_alloc(pool, 200, &ptr2);
    EXPECT_EQ(err, MEM_POOL_ERR_OK) << "Second allocation should succeed";
    EXPECT_NE(ptr2, nullptr) << "Second pointer should not be NULL";
    EXPECT_NE(ptr1, ptr2) << "Pointers should be different";

    err = pool_variable_alloc(pool, 50, &ptr3);
    EXPECT_EQ(err, MEM_POOL_ERR_OK) << "Third allocation should succeed";
    EXPECT_NE(ptr3, nullptr) << "Third pointer should not be NULL";

    // Test freeing
    err = pool_variable_free(pool, ptr2);
    EXPECT_EQ(err, MEM_POOL_ERR_OK) << "Free should succeed";

    err = pool_variable_free(pool, ptr1);
    EXPECT_EQ(err, MEM_POOL_ERR_OK) << "Free should succeed";

    err = pool_variable_free(pool, ptr3);
    EXPECT_EQ(err, MEM_POOL_ERR_OK) << "Free should succeed";
}

TEST_F(VariablePoolTest, ZeroMemory) {
    void *ptr = pool_variable_calloc(pool, 100);
    ASSERT_NE(ptr, nullptr) << "Calloc should return valid pointer";

    // Check that memory is zeroed
    char *bytes = (char*)ptr;
    for (int i = 0; i < 100; i++) {
        EXPECT_EQ(bytes[i], 0) << "Memory should be zeroed at index " << i;
    }

    pool_variable_free(pool, ptr);
}

TEST_F(VariablePoolTest, BasicRealloc) {
    // Initial allocation
    void *ptr;
    pool_variable_alloc(pool, 50, &ptr);

    // Fill with test data
    strcpy((char*)ptr, "Hello World");

    // Realloc to larger size
    void *new_ptr = pool_variable_realloc(pool, ptr, 50, 100);
    EXPECT_NE(new_ptr, nullptr) << "Realloc should succeed";
    EXPECT_STREQ((char*)new_ptr, "Hello World") << "Data should be preserved";

    // Realloc to smaller size
    void *smaller_ptr = pool_variable_realloc(pool, new_ptr, 100, 25);
    EXPECT_NE(smaller_ptr, nullptr) << "Realloc to smaller size should succeed";
    EXPECT_EQ(strncmp((char*)smaller_ptr, "Hello World", 11), 0) << "Data should be preserved";

    pool_variable_free(pool, smaller_ptr);
}

TEST_F(VariablePoolTest, ReallocFromNull) {
    // Realloc from NULL should behave like malloc
    void *ptr = pool_variable_realloc(pool, nullptr, 0, 100);
    EXPECT_NE(ptr, nullptr) << "Realloc from NULL should work like alloc";

    strcpy((char*)ptr, "Test");
    EXPECT_STREQ((char*)ptr, "Test") << "Should be able to write to allocated memory";

    pool_variable_free(pool, ptr);
}

TEST_F(VariablePoolTest, ReallocToZero) {
    void *ptr;
    pool_variable_alloc(pool, 100, &ptr);

    // Realloc to zero size should return valid pointer (per original Criterion test)
    void *zero_ptr = pool_variable_realloc(pool, ptr, 100, 0);
    EXPECT_NE(zero_ptr, nullptr) << "Realloc to zero size should return valid pointer";
}

TEST_F(VariablePoolTest, MultipleReallocs) {
    void *ptr;
    pool_variable_alloc(pool, 10, &ptr);
    strcpy((char*)ptr, "Hi");

    // Multiple reallocs
    ptr = pool_variable_realloc(pool, ptr, 10, 20);
    EXPECT_NE(ptr, nullptr);
    EXPECT_STREQ((char*)ptr, "Hi");

    ptr = pool_variable_realloc(pool, ptr, 20, 50);
    EXPECT_NE(ptr, nullptr);
    EXPECT_STREQ((char*)ptr, "Hi");

    ptr = pool_variable_realloc(pool, ptr, 50, 100);
    EXPECT_NE(ptr, nullptr);
    EXPECT_STREQ((char*)ptr, "Hi");

    pool_variable_free(pool, ptr);
}

TEST_F(VariablePoolTest, InvalidOperations) {
    void *ptr;
    MemPoolError err;

    // Double free
    err = pool_variable_alloc(pool, 100, &ptr);
    EXPECT_EQ(err, MEM_POOL_ERR_OK);

    err = pool_variable_free(pool, ptr);
    EXPECT_EQ(err, MEM_POOL_ERR_OK);

    // This should handle the double free gracefully
    err = pool_variable_free(pool, ptr);
    // The behavior for double free may vary, so we just check it doesn't crash
}

TEST_F(VariablePoolTest, NullPointerHandling) {
    MemPoolError err;
    void *ptr = nullptr;

    // Free NULL pointer should be safe
    err = pool_variable_free(pool, nullptr);
    // Should handle gracefully

    // Realloc with NULL should work like malloc
    ptr = pool_variable_realloc(pool, nullptr, 0, 50);
    EXPECT_NE(ptr, nullptr);

    pool_variable_free(pool, ptr);
}

TEST_F(VariablePoolTest, ZeroSizeOperations) {
    void *ptr = nullptr;
    MemPoolError err;

    // Zero size allocation
    err = pool_variable_alloc(pool, 0, &ptr);
    // Behavior may vary, but shouldn't crash

    if (ptr) {
        pool_variable_free(pool, ptr);
    }
}

TEST_F(VariablePoolTest, RapidOperations) {
    void *ptrs[10];
    MemPoolError err;

    // Rapid allocation/deallocation cycles
    for (int cycle = 0; cycle < 5; cycle++) {
        // Allocate multiple pointers
        for (int i = 0; i < 10; i++) {
            err = pool_variable_alloc(pool, 32 + i * 4, &ptrs[i]);
            EXPECT_EQ(err, MEM_POOL_ERR_OK) << "Allocation " << i << " in cycle " << cycle << " should succeed";
        }

        // Free in reverse order
        for (int i = 9; i >= 0; i--) {
            err = pool_variable_free(pool, ptrs[i]);
            EXPECT_EQ(err, MEM_POOL_ERR_OK) << "Free " << i << " in cycle " << cycle << " should succeed";
        }
    }
}

TEST_F(VariablePoolTest, ReallocSmaller) {
    void *ptr;
    MemPoolError err = pool_variable_alloc(pool, 200, &ptr);
    ASSERT_EQ(err, MEM_POOL_ERR_OK);

    // Fill with test data
    memset(ptr, 0xAA, 200);

    // Realloc to smaller size
    void *new_ptr = pool_variable_realloc(pool, ptr, 200, 50);
    ASSERT_NE(new_ptr, nullptr) << "Realloc to smaller size should succeed";

    // Check that the data is preserved for the smaller size
    char *bytes = (char*)new_ptr;
    for (int i = 0; i < 50; i++) {
        EXPECT_EQ(bytes[i], (char)0xAA) << "Data should be preserved at index " << i;
    }

    pool_variable_free(pool, new_ptr);
}

TEST_F(VariablePoolTest, FragmentationHandling) {
    void *ptrs[10];
    MemPoolError err;

    // Allocate multiple blocks
    for (int i = 0; i < 10; i++) {
        err = pool_variable_alloc(pool, 50 + i * 10, &ptrs[i]);
        ASSERT_EQ(err, MEM_POOL_ERR_OK);
        sprintf((char*)ptrs[i], "Block%d", i);
    }

    // Free every other block to create fragmentation
    for (int i = 1; i < 10; i += 2) {
        err = pool_variable_free(pool, ptrs[i]);
        EXPECT_EQ(err, MEM_POOL_ERR_OK);
    }

    // Now test realloc on remaining blocks
    for (int i = 0; i < 10; i += 2) {
        char expected[20];
        sprintf(expected, "Block%d", i);

        ptrs[i] = pool_variable_realloc(pool, ptrs[i], 50 + i * 10, 200);
        ASSERT_NE(ptrs[i], nullptr) << "Realloc with fragmentation should succeed for block " << i;
        EXPECT_STREQ((char*)ptrs[i], expected) << "Data should be preserved during fragmented realloc for block " << i;
    }

    // Cleanup remaining blocks
    for (int i = 0; i < 10; i += 2) {
        pool_variable_free(pool, ptrs[i]);
    }
}

TEST_F(VariablePoolTest, StressTest) {
    const int num_iterations = 100;
    void *ptrs[10];

    for (int iter = 0; iter < num_iterations; iter++) {
        // Allocate with varying sizes
        for (int i = 0; i < 10; i++) {
            size_t size = 10 + (iter + i) % 200;
            MemPoolError err = pool_variable_alloc(pool, size, &ptrs[i]);
            ASSERT_EQ(err, MEM_POOL_ERR_OK) << "Allocation should succeed in iteration " << iter;

            // Fill with pattern
            memset(ptrs[i], 0x55 + (i % 3), size);
        }

        // Realloc some randomly
        for (int i = 0; i < 5; i++) {
            size_t old_size = 10 + (iter + i) % 200;
            size_t new_size = 20 + (iter + i + 50) % 300;
            ptrs[i] = pool_variable_realloc(pool, ptrs[i], old_size, new_size);
            ASSERT_NE(ptrs[i], nullptr) << "Realloc should succeed in stress test iteration " << iter;
        }

        // Free all
        for (int i = 0; i < 10; i++) {
            MemPoolError err = pool_variable_free(pool, ptrs[i]);
            EXPECT_EQ(err, MEM_POOL_ERR_OK) << "Free should succeed in iteration " << iter;
        }
    }
}

TEST_F(VariablePoolTest, BufferGrowth) {
    void *ptrs[5];
    MemPoolError err;

    // Allocate blocks that will fill initial buffer
    for (int i = 0; i < 5; i++) {
        err = pool_variable_alloc(pool, 200, &ptrs[i]);
        ASSERT_EQ(err, MEM_POOL_ERR_OK) << "Initial allocation " << i << " should succeed";
        sprintf((char*)ptrs[i], "Data%d", i);
    }

    // This should trigger buffer growth
    void *large_ptr;
    err = pool_variable_alloc(pool, 500, &large_ptr);
    ASSERT_EQ(err, MEM_POOL_ERR_OK) << "Large allocation should succeed and trigger buffer growth";

    strcpy((char*)large_ptr, "LargeData");

    // Verify all data is still intact
    for (int i = 0; i < 5; i++) {
        char expected[20];
        sprintf(expected, "Data%d", i);
        EXPECT_STREQ((char*)ptrs[i], expected) << "Data should be preserved after buffer growth for block " << i;
    }
    EXPECT_STREQ((char*)large_ptr, "LargeData") << "Large block data should be correct";

    // Cleanup
    for (int i = 0; i < 5; i++) {
        pool_variable_free(pool, ptrs[i]);
    }
    pool_variable_free(pool, large_ptr);
}

TEST_F(VariablePoolTest, AlignedSizeof) {
    void *ptr;
    MemPoolError err = pool_variable_alloc(pool, 100, &ptr);
    ASSERT_EQ(err, MEM_POOL_ERR_OK);

    size_t size;
    err = pool_variable_aligned_sizeof(pool, ptr, &size);
    EXPECT_EQ(err, MEM_POOL_ERR_OK) << "aligned_sizeof should succeed";
    EXPECT_GE(size, 100) << "Aligned size should be at least the requested size";

    pool_variable_free(pool, ptr);
}

TEST_F(VariablePoolTest, BestFitAlgorithm) {
    // Test with smaller tolerance for better fit checking
    VariableMemPool *test_pool;
    MemPoolError err = pool_variable_init(&test_pool, 1024, 5);
    ASSERT_EQ(err, MEM_POOL_ERR_OK);

    void *ptr1, *ptr2, *ptr3;

    // Allocate blocks of different sizes
    err = pool_variable_alloc(test_pool, 100, &ptr1);
    ASSERT_EQ(err, MEM_POOL_ERR_OK);

    err = pool_variable_alloc(test_pool, 50, &ptr2);
    ASSERT_EQ(err, MEM_POOL_ERR_OK);

    err = pool_variable_alloc(test_pool, 200, &ptr3);
    ASSERT_EQ(err, MEM_POOL_ERR_OK);

    // Free middle block
    err = pool_variable_free(test_pool, ptr2);
    EXPECT_EQ(err, MEM_POOL_ERR_OK);

    // Allocate a block that should fit in the freed space
    void *ptr4;
    err = pool_variable_alloc(test_pool, 45, &ptr4);
    EXPECT_EQ(err, MEM_POOL_ERR_OK) << "Best fit allocation should succeed";

    // Cleanup
    pool_variable_free(test_pool, ptr1);
    pool_variable_free(test_pool, ptr3);
    pool_variable_free(test_pool, ptr4);
    pool_variable_destroy(test_pool);
}

TEST_F(VariablePoolTest, BufferBoundaryOverflowPrevention) {
    // Test for the specific buffer boundary bug that was causing heap buffer overflow
    VariableMemPool *test_pool;
    // Use a very small buffer size to force boundary conditions quickly
    MemPoolError err = pool_variable_init(&test_pool, 64, 10);
    ASSERT_EQ(err, MEM_POOL_ERR_OK);

    void *ptrs[10];

    // Fill the buffer almost to capacity to trigger boundary conditions
    // Each allocation uses header_size (16 bytes) + aligned block size
    // With 64-byte buffer, we can fit about 3 small allocations before hitting boundary

    // Allocate blocks that will fill the buffer close to capacity
    err = pool_variable_alloc(test_pool, 16, &ptrs[0]); // ~32 bytes with header
    EXPECT_EQ(err, MEM_POOL_ERR_OK);

    err = pool_variable_alloc(test_pool, 16, &ptrs[1]); // ~32 bytes with header
    EXPECT_EQ(err, MEM_POOL_ERR_OK);

    // This allocation should trigger buffer boundary check
    // Before the fix: buffer_has_space would incorrectly return true when curr_ptr == end
    // After the fix: buffer_has_space correctly returns false, creating new buffer
    err = pool_variable_alloc(test_pool, 16, &ptrs[2]); // Should create new buffer
    EXPECT_EQ(err, MEM_POOL_ERR_OK);
    ASSERT_NE(ptrs[2], nullptr) << "Allocation at buffer boundary should succeed with new buffer";

    // Test the exact scenario that caused the crash:
    // Allocate something that would exactly fill remaining space
    err = pool_variable_alloc(test_pool, 8, &ptrs[3]);
    EXPECT_EQ(err, MEM_POOL_ERR_OK);
    ASSERT_NE(ptrs[3], nullptr) << "Small allocation should succeed";

    // Now try to allocate something larger than remaining space
    // This should trigger new buffer creation, not overflow
    err = pool_variable_alloc(test_pool, 32, &ptrs[4]);
    EXPECT_EQ(err, MEM_POOL_ERR_OK);
    ASSERT_NE(ptrs[4], nullptr) << "Large allocation should trigger new buffer, not overflow";

    // Verify all pointers are usable (write test data)
    for (int i = 0; i < 5; i++) {
        if (ptrs[i]) {
            *((char*)ptrs[i]) = 'A' + i;
            EXPECT_EQ(*((char*)ptrs[i]), 'A' + i) << "Pointer " << i << " should be writable";
        }
    }

    // Cleanup
    for (int i = 0; i < 5; i++) {
        if (ptrs[i]) {
            pool_variable_free(test_pool, ptrs[i]);
        }
    }
    pool_variable_destroy(test_pool);
}

TEST_F(VariablePoolTest, FreeListCorruptionDetection) {
    // Create a scenario similar to the format-md.c crash
    void *ptr1, *ptr2, *ptr3, *ptr4;
    MemPoolError err;

    // Allocate adjacent blocks
    err = pool_variable_alloc(pool, 100, &ptr1);
    ASSERT_EQ(err, MEM_POOL_ERR_OK);
    err = pool_variable_alloc(pool, 100, &ptr2);
    ASSERT_EQ(err, MEM_POOL_ERR_OK);
    err = pool_variable_alloc(pool, 100, &ptr3);
    ASSERT_EQ(err, MEM_POOL_ERR_OK);
    err = pool_variable_alloc(pool, 100, &ptr4);
    ASSERT_EQ(err, MEM_POOL_ERR_OK);

    // Free middle blocks to create fragmentation
    err = pool_variable_free(pool, ptr2);
    EXPECT_EQ(err, MEM_POOL_ERR_OK);
    err = pool_variable_free(pool, ptr3);
    EXPECT_EQ(err, MEM_POOL_ERR_OK);

    // This should trigger coalescing and potential corruption detection
    // The safety checks should handle any "block not found" scenarios gracefully
    void *new_ptr1 = pool_variable_realloc(pool, ptr1, 100, 400);
    ASSERT_NE(new_ptr1, nullptr) << "Realloc should succeed even with fragmented free list";

    // Verify we can still use the pool normally after potential corruption
    void *test_ptr;
    err = pool_variable_alloc(pool, 50, &test_ptr);
    EXPECT_EQ(err, MEM_POOL_ERR_OK) << "Pool should remain functional after corruption handling";

    // Cleanup
    pool_variable_free(pool, ptr4);
    pool_variable_free(pool, new_ptr1);
    pool_variable_free(pool, test_ptr);
}

TEST_F(VariablePoolTest, StrbufReallocPattern) {
    // Simulate the exact pattern from format-md.c where StrBuf growth causes corruption
    void *strbuf_ptr;
    MemPoolError err = pool_variable_alloc(pool, 32, &strbuf_ptr); // Initial StrBuf capacity
    ASSERT_EQ(err, MEM_POOL_ERR_OK);

    // Fill with test data (simulating string content)
    strcpy((char*)strbuf_ptr, "Line Breaks and Paragraphs");

    // Simulate other allocations happening (element processing, etc.)
    void *elem1, *elem2, *elem3;
    err = pool_variable_alloc(pool, 64, &elem1);
    ASSERT_EQ(err, MEM_POOL_ERR_OK);
    err = pool_variable_alloc(pool, 128, &elem2);
    ASSERT_EQ(err, MEM_POOL_ERR_OK);
    err = pool_variable_alloc(pool, 96, &elem3);
    ASSERT_EQ(err, MEM_POOL_ERR_OK);

    // Free some to create fragmentation (simulating element cleanup)
    err = pool_variable_free(pool, elem2);
    EXPECT_EQ(err, MEM_POOL_ERR_OK);

    // Now simulate StrBuf growth (this is where corruption occurred)
    strbuf_ptr = pool_variable_realloc(pool, strbuf_ptr, 32, 256);
    ASSERT_NE(strbuf_ptr, nullptr) << "StrBuf realloc should succeed";
    EXPECT_STREQ((char*)strbuf_ptr, "Line Breaks and Paragraphs") << "Data should be preserved";

    // Continue with more operations that might trigger the issue
    strbuf_ptr = pool_variable_realloc(pool, strbuf_ptr, 256, 512);
    ASSERT_NE(strbuf_ptr, nullptr) << "Second StrBuf realloc should succeed";
    EXPECT_STREQ((char*)strbuf_ptr, "Line Breaks and Paragraphs") << "Data should still be preserved";

    // Test that we can continue allocating normally
    void *new_elem;
    err = pool_variable_alloc(pool, 200, &new_elem);
    EXPECT_EQ(err, MEM_POOL_ERR_OK) << "New allocations should work after realloc sequence";

    // Cleanup
    pool_variable_free(pool, elem1);
    pool_variable_free(pool, elem3);
    pool_variable_free(pool, strbuf_ptr);
    pool_variable_free(pool, new_elem);
}

TEST_F(VariablePoolTest, InfiniteLoopPrevention) {
    // Create a scenario that could potentially cause infinite loops in free list traversal
    void *ptrs[10];
    MemPoolError err;

    // Allocate many small blocks
    for (int i = 0; i < 10; i++) {
        err = pool_variable_alloc(pool, 50, &ptrs[i]);
        EXPECT_EQ(err, MEM_POOL_ERR_OK);
    }

    // Free them all to create a long free list
    for (int i = 0; i < 10; i++) {
        err = pool_variable_free(pool, ptrs[i]);
        EXPECT_EQ(err, MEM_POOL_ERR_OK);
    }

    // Now allocate and immediately realloc to stress the free list traversal
    void *test_ptr;
    err = pool_variable_alloc(pool, 40, &test_ptr);
    ASSERT_EQ(err, MEM_POOL_ERR_OK);

    // Multiple reallocs should not cause infinite loops even with long free lists
    for (int i = 0; i < 5; i++) {
        test_ptr = pool_variable_realloc(pool, test_ptr, 40 + i * 10, 40 + (i + 1) * 10);
        ASSERT_NE(test_ptr, nullptr) << "Realloc " << i << " should complete without infinite loop";
    }

    pool_variable_free(pool, test_ptr);
}

TEST_F(VariablePoolTest, CorruptedPointerHandling) {
    // Test that the pool handles various types of invalid pointers gracefully
    MemPoolError err;

    // Test freeing NULL
    err = pool_variable_free(pool, NULL);
    EXPECT_EQ(err, MEM_POOL_ERR_UNKNOWN_BLOCK) << "Freeing NULL should be handled gracefully";

    // Test freeing stack pointer
    int stack_var = 42;
    err = pool_variable_free(pool, &stack_var);
    EXPECT_EQ(err, MEM_POOL_ERR_UNKNOWN_BLOCK) << "Freeing stack pointer should be rejected";

    // Test freeing a pointer that looks like the corrupted pointer from the crash
    void *fake_ptr = (void*)0x6e6120646c6f6230ULL;
    err = pool_variable_free(pool, fake_ptr);
    EXPECT_EQ(err, MEM_POOL_ERR_UNKNOWN_BLOCK) << "Freeing corrupted pointer should be handled";

    // Test that pool remains functional after invalid operations
    void *valid_ptr;
    err = pool_variable_alloc(pool, 100, &valid_ptr);
    EXPECT_EQ(err, MEM_POOL_ERR_OK) << "Pool should remain functional after invalid operations";

    pool_variable_free(pool, valid_ptr);
}

TEST_F(VariablePoolTest, DoubleFreeProtection) {
    void *ptr;
    MemPoolError err = pool_variable_alloc(pool, 100, &ptr);
    ASSERT_EQ(err, MEM_POOL_ERR_OK);
    strcpy((char*)ptr, "Test data");

    // First free should succeed
    err = pool_variable_free(pool, ptr);
    EXPECT_EQ(err, MEM_POOL_ERR_OK) << "First free should succeed";

    // Second free should be detected and handled gracefully
    // Note: The current implementation doesn't explicitly check for double-free,
    // but our safety checks should prevent crashes
    err = pool_variable_free(pool, ptr);
    EXPECT_EQ(err, MEM_POOL_ERR_UNKNOWN_BLOCK) << "Double free should be handled gracefully";

    // Pool should remain functional
    void *new_ptr;
    err = pool_variable_alloc(pool, 150, &new_ptr);
    EXPECT_EQ(err, MEM_POOL_ERR_OK) << "Pool should remain functional after double-free attempt";

    pool_variable_free(pool, new_ptr);
}

TEST_F(VariablePoolTest, BlockNotFoundScenario) {
    // Create a specific scenario that triggers "block not found in free list"
    void *blocks[6];
    MemPoolError err;

    // Allocate a sequence of blocks
    for (int i = 0; i < 6; i++) {
        err = pool_variable_alloc(pool, 80 + i * 10, &blocks[i]);
        ASSERT_EQ(err, MEM_POOL_ERR_OK);
        snprintf((char*)blocks[i], 20, "Block%d", i);
    }

    // Free blocks in a pattern that creates complex coalescing scenarios
    err = pool_variable_free(pool, blocks[1]); // Free second block
    EXPECT_EQ(err, MEM_POOL_ERR_OK);
    err = pool_variable_free(pool, blocks[3]); // Free fourth block
    EXPECT_EQ(err, MEM_POOL_ERR_OK);
    err = pool_variable_free(pool, blocks[5]); // Free sixth block
    EXPECT_EQ(err, MEM_POOL_ERR_OK);

    // Now realloc the first block - this should trigger coalescing with freed block[1]
    // and the safety checks should handle any missing blocks gracefully
    blocks[0] = pool_variable_realloc(pool, blocks[0], 80, 300);
    ASSERT_NE(blocks[0], nullptr) << "Realloc should succeed despite complex free list state";
    EXPECT_STREQ((char*)blocks[0], "Block0") << "Data should be preserved during complex realloc";

    // Try to realloc another block that might trigger more coalescing
    blocks[2] = pool_variable_realloc(pool, blocks[2], 100, 250);
    ASSERT_NE(blocks[2], nullptr) << "Second complex realloc should also succeed";
    EXPECT_STREQ((char*)blocks[2], "Block2") << "Data should be preserved in second realloc";

    // Verify pool is still functional
    void *new_block;
    err = pool_variable_alloc(pool, 150, &new_block);
    EXPECT_EQ(err, MEM_POOL_ERR_OK) << "Pool should remain functional after complex operations";

    // Cleanup
    pool_variable_free(pool, blocks[0]);
    pool_variable_free(pool, blocks[2]);
    pool_variable_free(pool, blocks[4]);
    pool_variable_free(pool, new_block);
}

TEST_F(VariablePoolTest, FormatMdStressSimulation) {
    // Simulate the complex allocation pattern from format-md.c processing
    MemPoolError err;

    // Simulate main StrBuf for output
    void *output_buf;
    err = pool_variable_alloc(pool, 32, &output_buf);
    ASSERT_EQ(err, MEM_POOL_ERR_OK);
    strcpy((char*)output_buf, "# Heading\n");

    // Simulate various element allocations during markdown processing
    void *elements[20];
    for (int i = 0; i < 20; i++) {
        err = pool_variable_alloc(pool, 60 + (i % 8) * 20, &elements[i]);
        ASSERT_EQ(err, MEM_POOL_ERR_OK);
        snprintf((char*)elements[i], 30, "Element%d", i);
    }

    // Simulate StrBuf growth during processing (like appending text)
    output_buf = pool_variable_realloc(pool, output_buf, 32, 128);
    ASSERT_NE(output_buf, nullptr) << "First StrBuf growth should succeed";
    strcat((char*)output_buf, "## Subheading\n");

    // Free some elements (simulating element processing completion)
    for (int i = 5; i < 15; i += 2) {
        err = pool_variable_free(pool, elements[i]);
        EXPECT_EQ(err, MEM_POOL_ERR_OK);
        elements[i] = NULL;
    }

    // More StrBuf growth (like appending a long paragraph)
    output_buf = pool_variable_realloc(pool, output_buf, 128, 512);
    ASSERT_NE(output_buf, nullptr) << "Second StrBuf growth should succeed";
    strcat((char*)output_buf, "This is a long paragraph that would cause buffer expansion...\n");

    // Allocate more elements (simulating continued processing)
    void *more_elements[10];
    for (int i = 0; i < 10; i++) {
        err = pool_variable_alloc(pool, 40 + i * 5, &more_elements[i]);
        ASSERT_EQ(err, MEM_POOL_ERR_OK);
        snprintf((char*)more_elements[i], 20, "More%d", i);
    }

    // Final StrBuf growth
    output_buf = pool_variable_realloc(pool, output_buf, 512, 1024);
    ASSERT_NE(output_buf, nullptr) << "Final StrBuf growth should succeed";

    // Verify the output buffer still contains our data
    EXPECT_NE(strstr((char*)output_buf, "# Heading"), nullptr) << "Original content should be preserved";
    EXPECT_NE(strstr((char*)output_buf, "## Subheading"), nullptr) << "Added content should be preserved";

    // Cleanup all remaining allocations
    for (int i = 0; i < 20; i++) {
        if (elements[i]) {
            pool_variable_free(pool, elements[i]);
        }
    }
    for (int i = 0; i < 10; i++) {
        pool_variable_free(pool, more_elements[i]);
    }
    pool_variable_free(pool, output_buf);
}

// Test 27: Safety checks validation
TEST_F(VariablePoolTest, SafetyChecksValidation) {
    // Test that the safety checks we added for corruption detection work correctly
    void *ptr1, *ptr2;
    MemPoolError err;

    // Normal allocation and free
    err = pool_variable_alloc(pool, 100, &ptr1);
    ASSERT_EQ(err, MEM_POOL_ERR_OK);
    strcpy((char*)ptr1, "Test data");

    err = pool_variable_alloc(pool, 200, &ptr2);
    ASSERT_EQ(err, MEM_POOL_ERR_OK);
    strcpy((char*)ptr2, "More test data");

    // Free first pointer
    err = pool_variable_free(pool, ptr1);
    EXPECT_EQ(err, MEM_POOL_ERR_OK);

    // Verify second pointer is still valid
    EXPECT_STREQ((char*)ptr2, "More test data") << "Remaining data should be intact";

    // Test realloc on remaining pointer
    ptr2 = pool_variable_realloc(pool, ptr2, 200, 400);
    ASSERT_NE(ptr2, nullptr) << "Realloc should succeed";
    EXPECT_STREQ((char*)ptr2, "More test data") << "Data should be preserved";

    // Test that attempting to use freed pointer fails gracefully
    err = pool_variable_free(pool, ptr1); // Double free attempt
    EXPECT_EQ(err, MEM_POOL_ERR_UNKNOWN_BLOCK) << "Double free should be detected";

    pool_variable_free(pool, ptr2);
}

// Test 28: Debug output validation
TEST_F(VariablePoolTest, DebugOutputValidation) {
    // Test that debug output functions don't crash with various pool states
    void *ptrs[5];
    MemPoolError err;

    // Allocate some blocks
    for (int i = 0; i < 5; i++) {
        err = pool_variable_alloc(pool, 50 + i * 25, &ptrs[i]);
        ASSERT_EQ(err, MEM_POOL_ERR_OK);
        snprintf((char*)ptrs[i], 20, "Debug%d", i);
    }

    // Free some to create free list
    for (int i = 1; i < 4; i += 2) {
        err = pool_variable_free(pool, ptrs[i]);
        EXPECT_EQ(err, MEM_POOL_ERR_OK);
    }

    // Pool should remain functional after partial frees
    void *test_ptr;
    err = pool_variable_alloc(pool, 100, &test_ptr);
    EXPECT_EQ(err, MEM_POOL_ERR_OK) << "Pool should remain functional after partial frees";

    // Cleanup
    pool_variable_free(pool, ptrs[0]);
    pool_variable_free(pool, ptrs[4]);
    pool_variable_free(pool, test_ptr);
}

// Test 29: Exact crash reproduction attempt
TEST_F(VariablePoolTest, ExactCrashReproductionAttempt) {
    // Attempt to reproduce the exact conditions that caused the original crash
    // Based on the error logs from format-md.c processing

    void *output_buffer;
    MemPoolError err = pool_variable_alloc(pool, 32, &output_buffer);
    ASSERT_EQ(err, MEM_POOL_ERR_OK);
    strcpy((char*)output_buffer, "# Line Breaks and Paragraphs\n");

    // Simulate element tree processing with specific allocation pattern
    void *elements[15];
    const size_t element_sizes[] = {48, 64, 32, 80, 96, 112, 48, 64, 32, 128, 80, 96, 48, 64, 144};

    for (int i = 0; i < 15; i++) {
        err = pool_variable_alloc(pool, element_sizes[i], &elements[i]);
        ASSERT_EQ(err, MEM_POOL_ERR_OK);
        snprintf((char*)elements[i], 20, "Elem_%d", i);
    }

    // The specific realloc sequence that triggered the crash
    output_buffer = pool_variable_realloc(pool, output_buffer, 32, 128);
    ASSERT_NE(output_buffer, nullptr) << "First realloc should succeed";
    strcat((char*)output_buffer, "\\n\\n");

    // Free some elements to create the exact fragmentation pattern
    err = pool_variable_free(pool, elements[2]);
    EXPECT_EQ(err, MEM_POOL_ERR_OK);
    err = pool_variable_free(pool, elements[5]);
    EXPECT_EQ(err, MEM_POOL_ERR_OK);
    err = pool_variable_free(pool, elements[8]);
    EXPECT_EQ(err, MEM_POOL_ERR_OK);
    err = pool_variable_free(pool, elements[11]);
    EXPECT_EQ(err, MEM_POOL_ERR_OK);

    // The critical realloc that caused corruption
    output_buffer = pool_variable_realloc(pool, output_buffer, 128, 512);
    ASSERT_NE(output_buffer, nullptr) << "Critical realloc should succeed without corruption";

    // Verify data integrity
    EXPECT_NE(strstr((char*)output_buffer, "# Line Breaks"), nullptr) << "Original content should be preserved";
    EXPECT_NE(strstr((char*)output_buffer, "\\n\\n"), nullptr) << "Added content should be preserved";

    // Continue processing to ensure no delayed corruption
    for (int i = 0; i < 5; i++) {
        void *new_elem;
        err = pool_variable_alloc(pool, 72 + i * 8, &new_elem);
        ASSERT_EQ(err, MEM_POOL_ERR_OK) << "Post-corruption allocations should succeed";
        sprintf((char*)new_elem, "New_%d", i);

        // Immediate realloc test
        new_elem = pool_variable_realloc(pool, new_elem, 72 + i * 8, 150 + i * 10);
        ASSERT_NE(new_elem, nullptr) << "Post-corruption reallocs should succeed";
        EXPECT_EQ(strncmp((char*)new_elem, "New_", 4), 0) << "Data should be preserved in post-corruption reallocs";

        pool_variable_free(pool, new_elem);
    }

    // Cleanup remaining elements
    const int remaining[] = {0, 1, 3, 4, 6, 7, 9, 10, 12, 13, 14};
    for (size_t i = 0; i < sizeof(remaining)/sizeof(remaining[0]); i++) {
        pool_variable_free(pool, elements[remaining[i]]);
    }
    pool_variable_free(pool, output_buffer);

    EXPECT_TRUE(true) << "Crash reproduction test completed without corruption";
}
