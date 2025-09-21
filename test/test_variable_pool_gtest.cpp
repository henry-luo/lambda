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
    void *ptr = pool_calloc(pool, 100);
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