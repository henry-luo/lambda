#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <cstdint>
#include <cstddef>

extern "C" {
#include "../lib/mem-pool/include/mem_pool.h"
}

// max_align_t is provided by the system headers

TEST_CASE("Variable Pool Basic Initialization", "[variable_pool][init]") {
    VariableMemPool *pool;
    MemPoolError err = pool_variable_init(&pool, 1024, 10);
    
    REQUIRE(err == MEM_POOL_ERR_OK);
    REQUIRE(pool != nullptr);
    
    pool_variable_destroy(pool);
}

TEST_CASE("Variable Pool Invalid Parameters", "[variable_pool][init][edge_cases]") {
    VariableMemPool *pool;
    
    // Test with very large tolerance (should be clamped)
    MemPoolError err = pool_variable_init(&pool, 1024, 200);
    REQUIRE(err == MEM_POOL_ERR_OK);
    
    pool_variable_destroy(pool);
}

TEST_CASE("Variable Pool Basic Allocation", "[variable_pool][alloc]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 1024, 10);
    
    void *ptr1, *ptr2, *ptr3;
    MemPoolError err;
    
    SECTION("Multiple allocations") {
        err = pool_variable_alloc(pool, 100, &ptr1);
        REQUIRE(err == MEM_POOL_ERR_OK);
        REQUIRE(ptr1 != nullptr);
        
        err = pool_variable_alloc(pool, 200, &ptr2);
        REQUIRE(err == MEM_POOL_ERR_OK);
        REQUIRE(ptr2 != nullptr);
        REQUIRE(ptr1 != ptr2);
        
        err = pool_variable_alloc(pool, 50, &ptr3);
        REQUIRE(err == MEM_POOL_ERR_OK);
        REQUIRE(ptr3 != nullptr);
    }
    
    SECTION("Free operations") {
        pool_variable_alloc(pool, 100, &ptr1);
        pool_variable_alloc(pool, 200, &ptr2);
        pool_variable_alloc(pool, 50, &ptr3);
        
        err = pool_variable_free(pool, ptr2);
        REQUIRE(err == MEM_POOL_ERR_OK);
        
        err = pool_variable_free(pool, ptr1);
        REQUIRE(err == MEM_POOL_ERR_OK);
        
        err = pool_variable_free(pool, ptr3);
        REQUIRE(err == MEM_POOL_ERR_OK);
    }
    
    pool_variable_destroy(pool);
}

TEST_CASE("Variable Pool Calloc Zero Memory", "[variable_pool][calloc]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 1024, 10);
    
    void *ptr = pool_calloc(pool, 100);
    REQUIRE(ptr != nullptr);
    
    // Check that memory is zeroed
    char *bytes = (char*)ptr;
    for (int i = 0; i < 100; i++) {
        REQUIRE(bytes[i] == 0);
    }
    
    pool_variable_free(pool, ptr);
    pool_variable_destroy(pool);
}

TEST_CASE("Variable Pool Realloc Basic", "[variable_pool][realloc]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 1024, 10);
    
    SECTION("Basic realloc to larger size") {
        void *ptr;
        pool_variable_alloc(pool, 50, &ptr);
        
        // Fill with test data
        strcpy((char*)ptr, "Hello World");
        
        // Realloc to larger size
        void *new_ptr = pool_variable_realloc(pool, ptr, 50, 100);
        REQUIRE(new_ptr != nullptr);
        REQUIRE(new_ptr != ptr);
        REQUIRE(strcmp((char*)new_ptr, "Hello World") == 0);
        
        pool_variable_free(pool, new_ptr);
    }
    
    SECTION("Realloc to smaller size") {
        void *ptr;
        pool_variable_alloc(pool, 200, &ptr);
        
        // Fill with test data
        memset(ptr, 0xAA, 200);
        
        // Realloc to smaller size
        void *new_ptr = pool_variable_realloc(pool, ptr, 200, 50);
        REQUIRE(new_ptr != nullptr);
        
        // Check that the data is preserved for the smaller size
        char *bytes = (char*)new_ptr;
        for (int i = 0; i < 50; i++) {
            REQUIRE(bytes[i] == (char)0xAA);
        }
        
        pool_variable_free(pool, new_ptr);
    }
    
    SECTION("Realloc from NULL") {
        // Realloc from NULL should act like malloc
        void *ptr = pool_variable_realloc(pool, nullptr, 0, 100);
        REQUIRE(ptr != nullptr);
        
        // Fill with data to ensure it's usable
        memset(ptr, 0xBB, 100);
        char *bytes = (char*)ptr;
        for (int i = 0; i < 100; i++) {
            REQUIRE(bytes[i] == (char)0xBB);
        }
        
        pool_variable_free(pool, ptr);
    }
    
    SECTION("Realloc to zero size") {
        void *ptr;
        pool_variable_alloc(pool, 100, &ptr);
        strcpy((char*)ptr, "Test");
        
        // Realloc to zero size
        void *new_ptr = pool_variable_realloc(pool, ptr, 100, 0);
        REQUIRE(new_ptr != nullptr);
        
        pool_variable_free(pool, new_ptr);
    }
    
    pool_variable_destroy(pool);
}

TEST_CASE("Variable Pool Multiple Reallocs", "[variable_pool][realloc][stress]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 1024, 10);
    
    void *ptr;
    pool_variable_alloc(pool, 10, &ptr);
    
    // Set initial data
    strcpy((char*)ptr, "Test");
    
    // Multiple reallocs with increasing sizes
    size_t sizes[] = {20, 50, 100, 200, 500};
    size_t current_size = 10;
    
    for (int i = 0; i < 5; i++) {
        ptr = pool_variable_realloc(pool, ptr, current_size, sizes[i]);
        REQUIRE(ptr != nullptr);
        REQUIRE(strcmp((char*)ptr, "Test") == 0);
        current_size = sizes[i];
    }
    
    pool_variable_free(pool, ptr);
    pool_variable_destroy(pool);
}

TEST_CASE("Variable Pool Fragmentation Handling", "[variable_pool][realloc][fragmentation]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 2048, 10);
    
    void *ptrs[10];
    
    // Allocate multiple blocks
    for (int i = 0; i < 10; i++) {
        pool_variable_alloc(pool, 50 + i * 10, &ptrs[i]);
        sprintf((char*)ptrs[i], "Block%d", i);
    }
    
    // Free every other block to create fragmentation
    for (int i = 1; i < 10; i += 2) {
        pool_variable_free(pool, ptrs[i]);
    }
    
    // Now test realloc on remaining blocks
    for (int i = 0; i < 10; i += 2) {
        char expected[20];
        sprintf(expected, "Block%d", i);
        
        ptrs[i] = pool_variable_realloc(pool, ptrs[i], 50 + i * 10, 200);
        REQUIRE(ptrs[i] != nullptr);
        REQUIRE(strcmp((char*)ptrs[i], expected) == 0);
    }
    
    // Cleanup
    for (int i = 0; i < 10; i += 2) {
        pool_variable_free(pool, ptrs[i]);
    }
    
    pool_variable_destroy(pool);
}

TEST_CASE("Variable Pool Realloc Stress Test", "[variable_pool][realloc][stress]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 4096, 10);
    
    void *ptr = nullptr;
    
    // Start with small allocation
    ptr = pool_variable_realloc(pool, ptr, 0, 16);
    REQUIRE(ptr != nullptr);
    
    // Stress test: grow and shrink with conservative sizes
    for (int i = 0; i < 50; i++) {
        size_t old_size = (i == 0) ? 16 : (16 + (i % 5) * 16);
        size_t new_size = 16 + ((i + 1) % 10) * 16;
        
        // Set test pattern
        size_t pattern_size = (old_size < new_size) ? old_size : new_size;
        if (pattern_size > 0) {
            memset(ptr, (i % 256), pattern_size);
        }
        
        void *old_ptr = ptr;
        ptr = pool_variable_realloc(pool, ptr, old_size, new_size);
        REQUIRE(ptr != nullptr);
        REQUIRE(ptr != old_ptr);
        
        // Verify pattern (check only the minimum size)
        if (pattern_size > 0) {
            char *bytes = (char*)ptr;
            for (size_t j = 0; j < pattern_size; j++) {
                REQUIRE(bytes[j] == (char)(i % 256));
            }
        }
    }
    
    pool_variable_free(pool, ptr);
    pool_variable_destroy(pool);
}

TEST_CASE("Variable Pool Buffer Growth", "[variable_pool][realloc][growth]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 256, 10); // Small initial buffer
    
    void *ptr;
    pool_variable_alloc(pool, 100, &ptr);
    strcpy((char*)ptr, "Initial data");
    
    // Force buffer growth by reallocating to a very large size
    ptr = pool_variable_realloc(pool, ptr, 100, 1024);
    REQUIRE(ptr != nullptr);
    REQUIRE(strcmp((char*)ptr, "Initial data") == 0);
    
    // Verify we can use the full new size
    memset((char*)ptr + 13, 0xCC, 1024 - 13);
    
    pool_variable_free(pool, ptr);
    pool_variable_destroy(pool);
}

TEST_CASE("Variable Pool Aligned Sizeof", "[variable_pool][sizeof]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 1024, 10);
    
    void *ptr;
    pool_variable_alloc(pool, 100, &ptr);
    
    size_t size;
    MemPoolError err = pool_variable_aligned_sizeof(pool, ptr, &size);
    REQUIRE(err == MEM_POOL_ERR_OK);
    
    // Size should be aligned
    size_t align = alignof(max_align_t);
    size_t expected_size = 100;
    if (expected_size % align) {
        expected_size = expected_size + (align - expected_size % align);
    }
    REQUIRE(size == expected_size);
    
    pool_variable_free(pool, ptr);
    pool_variable_destroy(pool);
}

TEST_CASE("Variable Pool Error Handling", "[variable_pool][error]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 1024, 10);
    
    SECTION("Free NULL pointer") {
        MemPoolError err = pool_variable_free(pool, nullptr);
        REQUIRE(err == MEM_POOL_ERR_UNKNOWN_BLOCK);
    }
    
    SECTION("Free invalid pointer") {
        char stack_var;
        MemPoolError err = pool_variable_free(pool, &stack_var);
        REQUIRE(err == MEM_POOL_ERR_UNKNOWN_BLOCK);
    }
    
    SECTION("Sizeof on invalid pointer") {
        char stack_var;
        size_t size;
        MemPoolError err = pool_variable_aligned_sizeof(pool, &stack_var, &size);
        REQUIRE(err == MEM_POOL_ERR_UNKNOWN_BLOCK);
    }
    
    pool_variable_destroy(pool);
}

TEST_CASE("Variable Pool Edge Cases", "[variable_pool][edge_cases]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 1024, 10);
    
    SECTION("NULL pointer realloc") {
        void *result = pool_variable_realloc(pool, nullptr, 0, 100);
        REQUIRE(result != nullptr);
        pool_variable_free(pool, result);
    }
    
    SECTION("Zero size operations") {
        void *ptr;
        MemPoolError err = pool_variable_alloc(pool, 0, &ptr);
        REQUIRE(err == MEM_POOL_ERR_OK);
        REQUIRE(ptr != nullptr);
        pool_variable_free(pool, ptr);
        
        ptr = pool_calloc(pool, 0);
        REQUIRE(ptr != nullptr);
        pool_variable_free(pool, ptr);
    }
    
    pool_variable_destroy(pool);
}

TEST_CASE("Variable Pool Best Fit Algorithm", "[variable_pool][tolerance]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 2048, 20); // 20% tolerance
    
    void *ptrs[5];
    
    // Allocate and free blocks of different sizes
    for (int i = 0; i < 5; i++) {
        pool_variable_alloc(pool, 50 + i * 20, &ptrs[i]);
    }
    
    // Free some blocks to create free list
    pool_variable_free(pool, ptrs[1]); // 70 bytes
    pool_variable_free(pool, ptrs[3]); // 110 bytes
    
    // Try to allocate something that should fit in the 70-byte block (within tolerance)
    void *new_ptr;
    MemPoolError err = pool_variable_alloc(pool, 65, &new_ptr);
    REQUIRE(err == MEM_POOL_ERR_OK);
    REQUIRE(new_ptr != nullptr);
    
    // Cleanup
    pool_variable_free(pool, new_ptr);
    pool_variable_free(pool, ptrs[0]);
    pool_variable_free(pool, ptrs[2]);
    pool_variable_free(pool, ptrs[4]);
    
    pool_variable_destroy(pool);
}

TEST_CASE("Variable Pool Rapid Operations", "[variable_pool][performance]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 4096, 10);
    
    // Rapid allocation and freeing
    for (int i = 0; i < 100; i++) {
        void *ptr;
        pool_variable_alloc(pool, 32 + (i % 10) * 8, &ptr);
        REQUIRE(ptr != nullptr);
        
        // Touch the memory
        memset(ptr, i % 256, 32);
        
        pool_variable_free(pool, ptr);
    }
    
    pool_variable_destroy(pool);
}

TEST_CASE("Variable Pool Corruption Protection", "[variable_pool][corruption]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 2048, 10);
    
    SECTION("Free list corruption detection") {
        void *ptr1, *ptr2, *ptr3, *ptr4;
        
        // Allocate adjacent blocks
        pool_variable_alloc(pool, 100, &ptr1);
        pool_variable_alloc(pool, 100, &ptr2);
        pool_variable_alloc(pool, 100, &ptr3);
        pool_variable_alloc(pool, 100, &ptr4);
        
        // Free middle blocks to create fragmentation
        pool_variable_free(pool, ptr2);
        pool_variable_free(pool, ptr3);
        
        // This should trigger coalescing and potential corruption detection
        void *new_ptr1 = pool_variable_realloc(pool, ptr1, 100, 400);
        REQUIRE(new_ptr1 != nullptr);
        
        // Verify we can still use the pool normally
        void *test_ptr;
        MemPoolError err = pool_variable_alloc(pool, 50, &test_ptr);
        REQUIRE(err == MEM_POOL_ERR_OK);
        
        // Cleanup
        pool_variable_free(pool, ptr4);
        pool_variable_free(pool, new_ptr1);
        pool_variable_free(pool, test_ptr);
    }
    
    SECTION("Double free protection") {
        void *ptr;
        pool_variable_alloc(pool, 100, &ptr);
        strcpy((char*)ptr, "Test data");
        
        // First free should succeed
        MemPoolError err = pool_variable_free(pool, ptr);
        REQUIRE(err == MEM_POOL_ERR_OK);
        
        // Second free should be detected and handled gracefully
        err = pool_variable_free(pool, ptr);
        REQUIRE(err == MEM_POOL_ERR_UNKNOWN_BLOCK);
        
        // Pool should remain functional
        void *new_ptr;
        err = pool_variable_alloc(pool, 150, &new_ptr);
        REQUIRE(err == MEM_POOL_ERR_OK);
        
        pool_variable_free(pool, new_ptr);
    }
    
    pool_variable_destroy(pool);
}
