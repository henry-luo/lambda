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

TEST_CASE("Variable Pool Null Pointer Handling", "[variable_pool][edge_cases]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 1024, 10);
    
    void *result = pool_variable_realloc(pool, nullptr, 0, 100);
    REQUIRE(result != nullptr);
    pool_variable_free(pool, result);
    
    pool_variable_destroy(pool);
}

TEST_CASE("Variable Pool Zero Size Operations", "[variable_pool][edge_cases]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 1024, 10);
    
    // Test allocation of zero size
    void *ptr;
    MemPoolError err = pool_variable_alloc(pool, 0, &ptr);
    REQUIRE(err == MEM_POOL_ERR_OK);
    REQUIRE(ptr != nullptr);
    pool_variable_free(pool, ptr);
    
    // Test calloc with zero size
    ptr = pool_calloc(pool, 0);
    REQUIRE(ptr != nullptr);
    pool_variable_free(pool, ptr);
    
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

// Individual realloc tests to match Criterion exactly
TEST_CASE("test_realloc_smaller", "[variable_pool][realloc]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 1024, 10);
    
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
    pool_variable_destroy(pool);
}

TEST_CASE("test_realloc_from_null", "[variable_pool][realloc]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 1024, 10);
    
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
    pool_variable_destroy(pool);
}

TEST_CASE("test_realloc_to_zero", "[variable_pool][realloc]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 1024, 10);
    
    void *ptr;
    pool_variable_alloc(pool, 100, &ptr);
    strcpy((char*)ptr, "Test");
    
    // Realloc to zero size
    void *new_ptr = pool_variable_realloc(pool, ptr, 100, 0);
    REQUIRE(new_ptr != nullptr);
    
    pool_variable_free(pool, new_ptr);
    pool_variable_destroy(pool);
}

// Corruption protection tests
TEST_CASE("test_free_list_corruption_detection", "[variable_pool][corruption]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 2048, 10);
    
    void *ptrs[10];
    MemPoolError err;
    
    // Allocate multiple blocks
    for (int i = 0; i < 10; i++) {
        err = pool_variable_alloc(pool, 50 + i * 10, &ptrs[i]);
        REQUIRE(err == MEM_POOL_ERR_OK);
        REQUIRE(ptrs[i] != nullptr);
    }
    
    // Free every other block to create fragmentation
    for (int i = 0; i < 10; i += 2) {
        err = pool_variable_free(pool, ptrs[i]);
        REQUIRE(err == MEM_POOL_ERR_OK);
    }
    
    // Try to allocate again - should use free list
    void *new_ptr;
    err = pool_variable_alloc(pool, 60, &new_ptr);
    REQUIRE(err == MEM_POOL_ERR_OK);
    REQUIRE(new_ptr != nullptr);
    
    // Free remaining blocks
    for (int i = 1; i < 10; i += 2) {
        err = pool_variable_free(pool, ptrs[i]);
        REQUIRE(err == MEM_POOL_ERR_OK);
    }
    
    err = pool_variable_free(pool, new_ptr);
    REQUIRE(err == MEM_POOL_ERR_OK);
    
    pool_variable_destroy(pool);
}

TEST_CASE("test_strbuf_realloc_pattern", "[variable_pool][corruption]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 1024, 20);
    
    void *ptr = nullptr;
    MemPoolError err;
    
    // Simulate StrBuf reallocation pattern
    err = pool_variable_alloc(pool, 16, &ptr);
    REQUIRE(err == MEM_POOL_ERR_OK);
    REQUIRE(ptr != nullptr);
    
    // Realloc to larger size (simulating string growth)
    ptr = pool_variable_realloc(pool, ptr, 16, 32);
    REQUIRE(ptr != nullptr);
    
    ptr = pool_variable_realloc(pool, ptr, 32, 64);
    REQUIRE(ptr != nullptr);
    
    ptr = pool_variable_realloc(pool, ptr, 64, 128);
    REQUIRE(ptr != nullptr);
    
    ptr = pool_variable_realloc(pool, ptr, 128, 256);
    REQUIRE(ptr != nullptr);
    
    // Realloc to smaller size
    ptr = pool_variable_realloc(pool, ptr, 256, 128);
    REQUIRE(ptr != nullptr);
    
    // Free final pointer
    err = pool_variable_free(pool, ptr);
    REQUIRE(err == MEM_POOL_ERR_OK);
    
    pool_variable_destroy(pool);
}

TEST_CASE("test_block_not_found_scenario", "[variable_pool][corruption]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 1024, 10);
    
    void *ptr1, *ptr2;
    MemPoolError err;
    
    // Allocate two blocks
    err = pool_variable_alloc(pool, 100, &ptr1);
    REQUIRE(err == MEM_POOL_ERR_OK);
    
    err = pool_variable_alloc(pool, 200, &ptr2);
    REQUIRE(err == MEM_POOL_ERR_OK);
    
    // Free first block
    err = pool_variable_free(pool, ptr1);
    REQUIRE(err == MEM_POOL_ERR_OK);
    
    // Try to realloc the freed block (should succeed with new allocation)
    void *result = pool_variable_realloc(pool, ptr1, 100, 150);
    REQUIRE(result != nullptr);
    
    // Free second block normally
    err = pool_variable_free(pool, ptr2);
    REQUIRE(err == MEM_POOL_ERR_OK);
    
    pool_variable_destroy(pool);
}

TEST_CASE("test_infinite_loop_prevention", "[variable_pool][corruption]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 2048, 10);
    
    void *ptrs[20];
    MemPoolError err;
    
    // Allocate many small blocks
    for (int i = 0; i < 20; i++) {
        err = pool_variable_alloc(pool, 50, &ptrs[i]);
        REQUIRE(err == MEM_POOL_ERR_OK);
    }
    
    // Free all blocks to populate free list
    for (int i = 0; i < 20; i++) {
        err = pool_variable_free(pool, ptrs[i]);
        REQUIRE(err == MEM_POOL_ERR_OK);
    }
    
    // Allocate again - should reuse from free list without infinite loop
    void *new_ptr;
    err = pool_variable_alloc(pool, 50, &new_ptr);
    REQUIRE(err == MEM_POOL_ERR_OK);
    REQUIRE(new_ptr != nullptr);
    
    err = pool_variable_free(pool, new_ptr);
    REQUIRE(err == MEM_POOL_ERR_OK);
    
    pool_variable_destroy(pool);
}

TEST_CASE("test_corrupted_pointer_handling", "[variable_pool][corruption]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 1024, 10);
    
    void *valid_ptr;
    MemPoolError err;
    
    // Allocate a valid pointer
    err = pool_variable_alloc(pool, 100, &valid_ptr);
    REQUIRE(err == MEM_POOL_ERR_OK);
    
    // Test with NULL pointer (should be handled gracefully)
    err = pool_variable_free(pool, nullptr);
    REQUIRE(err != MEM_POOL_ERR_OK);
    
    // Test realloc with NULL pointer (should act like malloc)
    void *realloc_result = pool_variable_realloc(pool, nullptr, 0, 200);
    REQUIRE(realloc_result != nullptr);
    
    // Clean up the realloc result
    err = pool_variable_free(pool, realloc_result);
    REQUIRE(err == MEM_POOL_ERR_OK);
    
    // Valid operations should still work
    err = pool_variable_free(pool, valid_ptr);
    REQUIRE(err == MEM_POOL_ERR_OK);
    
    pool_variable_destroy(pool);
}

TEST_CASE("test_double_free_protection", "[variable_pool][corruption]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 1024, 10);
    
    void *ptr;
    MemPoolError err;
    
    // Allocate a block
    err = pool_variable_alloc(pool, 100, &ptr);
    REQUIRE(err == MEM_POOL_ERR_OK);
    REQUIRE(ptr != nullptr);
    
    // Free it once (should succeed)
    err = pool_variable_free(pool, ptr);
    REQUIRE(err == MEM_POOL_ERR_OK);
    
    // Try to free it again (should be detected and handled)
    err = pool_variable_free(pool, ptr);
    REQUIRE(err != MEM_POOL_ERR_OK);
    
    pool_variable_destroy(pool);
}

TEST_CASE("test_format_md_stress_simulation", "[variable_pool][corruption]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 4096, 15);
    
    void *ptrs[50];
    MemPoolError err;
    
    // Simulate the allocation pattern that caused issues in format-md.c
    for (int iteration = 0; iteration < 5; iteration++) {
        // Allocate varying sizes (simulating StrBuf growth)
        for (int i = 0; i < 10; i++) {
            size_t size = 16 + (i * 8);
            err = pool_variable_alloc(pool, size, &ptrs[i]);
            REQUIRE(err == MEM_POOL_ERR_OK);
            REQUIRE(ptrs[i] != nullptr);
        }
        
        // Realloc some of them (simulating string appends)
        for (int i = 0; i < 5; i++) {
            size_t old_size = 16 + (i * 8);
            size_t new_size = 64 + (i * 16);
            ptrs[i] = pool_variable_realloc(pool, ptrs[i], old_size, new_size);
            REQUIRE(ptrs[i] != nullptr);
        }
        
        // Free in random order
        for (int i = 9; i >= 0; i--) {
            err = pool_variable_free(pool, ptrs[i]);
            REQUIRE(err == MEM_POOL_ERR_OK);
        }
    }
    
    pool_variable_destroy(pool);
}

TEST_CASE("test_safety_checks_validation", "[variable_pool][corruption]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 1024, 10);
    
    MemPoolError err;
    
    // Test NULL pointer handling
    err = pool_variable_free(pool, nullptr);
    REQUIRE(err != MEM_POOL_ERR_OK);
    
    void *null_realloc = pool_variable_realloc(pool, nullptr, 0, 100);
    REQUIRE(null_realloc != nullptr);
    
    // Test zero size allocation (behavior may vary - either succeed with minimal allocation or fail)
    void *zero_ptr;
    err = pool_variable_alloc(pool, 0, &zero_ptr);
    // Don't assert specific behavior for zero-size allocation as it's implementation-defined
    
    // Clean up
    if (null_realloc) {
        err = pool_variable_free(pool, null_realloc);
        REQUIRE(err == MEM_POOL_ERR_OK);
    }
    
    pool_variable_destroy(pool);
}

TEST_CASE("test_debug_output_validation", "[variable_pool][corruption]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 1024, 10);
    
    void *ptr1, *ptr2, *ptr3;
    MemPoolError err;
    
    // Allocate blocks to trigger debug output
    err = pool_variable_alloc(pool, 100, &ptr1);
    REQUIRE(err == MEM_POOL_ERR_OK);
    
    err = pool_variable_alloc(pool, 200, &ptr2);
    REQUIRE(err == MEM_POOL_ERR_OK);
    
    // Realloc to trigger debug messages
    ptr1 = pool_variable_realloc(pool, ptr1, 100, 150);
    REQUIRE(ptr1 != nullptr);
    
    // Free to trigger free list debug
    err = pool_variable_free(pool, ptr1);
    REQUIRE(err == MEM_POOL_ERR_OK);
    
    // Allocate again to reuse from free list
    err = pool_variable_alloc(pool, 120, &ptr3);
    REQUIRE(err == MEM_POOL_ERR_OK);
    
    // Clean up
    err = pool_variable_free(pool, ptr2);
    REQUIRE(err == MEM_POOL_ERR_OK);
    
    err = pool_variable_free(pool, ptr3);
    REQUIRE(err == MEM_POOL_ERR_OK);
    
    pool_variable_destroy(pool);
}

TEST_CASE("test_exact_crash_reproduction_attempt", "[variable_pool][corruption]") {
    VariableMemPool *pool;
    pool_variable_init(&pool, 2048, 10);
    
    void *ptrs[15];
    MemPoolError err;
    
    // Try to reproduce the exact sequence that caused crashes
    // Allocate initial blocks
    for (int i = 0; i < 10; i++) {
        err = pool_variable_alloc(pool, 32 + (i * 4), &ptrs[i]);
        REQUIRE(err == MEM_POOL_ERR_OK);
    }
    
    // Free some blocks in specific order
    err = pool_variable_free(pool, ptrs[2]);
    REQUIRE(err == MEM_POOL_ERR_OK);
    
    err = pool_variable_free(pool, ptrs[5]);
    REQUIRE(err == MEM_POOL_ERR_OK);
    
    err = pool_variable_free(pool, ptrs[8]);
    REQUIRE(err == MEM_POOL_ERR_OK);
    
    // Realloc remaining blocks
    for (int i = 0; i < 10; i++) {
        if (i != 2 && i != 5 && i != 8) {
            size_t old_size = 32 + (i * 4);
            size_t new_size = 64 + (i * 8);
            ptrs[i] = pool_variable_realloc(pool, ptrs[i], old_size, new_size);
            REQUIRE(ptrs[i] != nullptr);
        }
    }
    
    // Allocate new blocks
    for (int i = 10; i < 15; i++) {
        err = pool_variable_alloc(pool, 48, &ptrs[i]);
        REQUIRE(err == MEM_POOL_ERR_OK);
    }
    
    // Free everything
    for (int i = 0; i < 15; i++) {
        if (i != 2 && i != 5 && i != 8) {
            err = pool_variable_free(pool, ptrs[i]);
            REQUIRE(err == MEM_POOL_ERR_OK);
        }
    }
    
    pool_variable_destroy(pool);
}

TEST_CASE("test_buffer_boundary_overflow_prevention", "[variable_pool][boundary]") {
    VariableMemPool *pool;
    // Use a very small buffer size to force boundary conditions quickly
    pool_variable_init(&pool, 64, 10);
    
    void *ptr1, *ptr2;
    MemPoolError err;
    
    // Try to allocate more than the buffer can hold
    err = pool_variable_alloc(pool, 100, &ptr1);
    // Should either fail or trigger buffer expansion
    
    if (err == MEM_POOL_ERR_OK) {
        // If allocation succeeded, buffer must have expanded
        REQUIRE(ptr1 != nullptr);
        
        // Try another allocation
        err = pool_variable_alloc(pool, 50, &ptr2);
        
        // Clean up if successful
        if (ptr1) pool_variable_free(pool, ptr1);
        if (err == MEM_POOL_ERR_OK && ptr2) pool_variable_free(pool, ptr2);
    }
    
    pool_variable_destroy(pool);
}
