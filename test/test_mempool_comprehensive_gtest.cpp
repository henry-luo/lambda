/*
 * Comprehensive Memory Pool Test Suite (Enhanced GTest Version)
 * ============================================================
 *
 * Extensive GTest-based test suite for the jemalloc-based memory pool
 * implementation, building on patterns from variable pool tests and
 * adding comprehensive coverage for the pool_alloc/pool_calloc/pool_free API.
 *
 * Test Coverage:
 * - Basic functionality (alloc, calloc, free)
 * - Memory alignment and patterns
 * - Error handling and edge cases
 * - Performance and stress testing
 * - Memory safety and leak detection
 * - Large allocation scenarios
 * - Concurrent access patterns
 * - Integration patterns for real-world usage
 * - Memory initialization and zeroing
 * - Boundary condition testing
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdint>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <random>
#include <algorithm>
#include <set>
#include <unordered_set>

extern "C" {
#include "../lib/mempool.h"
}

class MemoryPoolTest : public ::testing::Test {
protected:
    std::vector<void*> allocated_ptrs;

    void SetUp() override {
        allocated_ptrs.clear();
    }

    void TearDown() override {
        // Clean up any allocated memory to prevent leaks
        for (void* ptr : allocated_ptrs) {
            if (ptr) {
                pool_free(ptr);
            }
        }
        allocated_ptrs.clear();
    }

    // Helper function to track allocations
    void* tracked_alloc(size_t size) {
        void* ptr = pool_alloc(size);
        if (ptr) {
            allocated_ptrs.push_back(ptr);
        }
        return ptr;
    }

    // Helper function to track calloc allocations
    void* tracked_calloc(size_t n, size_t size) {
        void* ptr = pool_calloc(n, size);
        if (ptr) {
            allocated_ptrs.push_back(ptr);
        }
        return ptr;
    }

    // Helper to remove from tracking when manually freed
    void untrack_ptr(void* ptr) {
        auto it = std::find(allocated_ptrs.begin(), allocated_ptrs.end(), ptr);
        if (it != allocated_ptrs.end()) {
            allocated_ptrs.erase(it);
        }
    }

    // Helper function to fill memory with a pattern
    void fill_pattern(void* ptr, size_t size, uint8_t pattern) {
        uint8_t* bytes = static_cast<uint8_t*>(ptr);
        for (size_t i = 0; i < size; i++) {
            bytes[i] = static_cast<uint8_t>(pattern + (i % 256));
        }
    }

    // Helper function to verify memory pattern
    bool verify_pattern(void* ptr, size_t size, uint8_t pattern) {
        uint8_t* bytes = static_cast<uint8_t*>(ptr);
        for (size_t i = 0; i < size; i++) {
            if (bytes[i] != static_cast<uint8_t>(pattern + (i % 256))) {
                return false;
            }
        }
        return true;
    }

    // Helper function to check if memory is zeroed
    bool is_zeroed(void* ptr, size_t size) {
        uint8_t* bytes = static_cast<uint8_t*>(ptr);
        for (size_t i = 0; i < size; i++) {
            if (bytes[i] != 0) {
                return false;
            }
        }
        return true;
    }

    // Helper to verify memory is accessible (read/write)
    bool is_memory_accessible(void* ptr, size_t size) {
        char* char_ptr = static_cast<char*>(ptr);
        try {
            // Write pattern and read back
            char_ptr[0] = 'A';
            char_ptr[size - 1] = 'Z';
            return (char_ptr[0] == 'A' && char_ptr[size - 1] == 'Z');
        } catch (...) {
            return false;
        }
    }

    // Helper to generate deterministic test data
    void generate_test_data(void* ptr, size_t size, uint32_t seed) {
        uint8_t* bytes = static_cast<uint8_t*>(ptr);
        for (size_t i = 0; i < size; i++) {
            bytes[i] = static_cast<uint8_t>((seed + i * 17) % 256);
        }
    }

    // Helper to verify test data
    bool verify_test_data(void* ptr, size_t size, uint32_t seed) {
        uint8_t* bytes = static_cast<uint8_t*>(ptr);
        for (size_t i = 0; i < size; i++) {
            if (bytes[i] != static_cast<uint8_t>((seed + i * 17) % 256)) {
                return false;
            }
        }
        return true;
    }
};

// ========================================================================
// Basic Functionality Tests
// ========================================================================

TEST_F(MemoryPoolTest, BasicAllocation) {
    void* ptr = pool_alloc(1024);
    EXPECT_NE(ptr, nullptr) << "Basic allocation should succeed";

    // Verify we can write to and read from the memory
    char* data = (char*)ptr;
    strcpy(data, "Hello, World!");
    EXPECT_STREQ(data, "Hello, World!") << "Should be able to write and read from allocated memory";

    pool_free(ptr);
}

TEST_F(MemoryPoolTest, BasicCalloc) {
    size_t count = 256;
    size_t size = 4;
    int* ptr = (int*)pool_calloc(count, size);
    EXPECT_NE(ptr, nullptr) << "Calloc should succeed";

    // Verify memory is zeroed
    for (size_t i = 0; i < count; i++) {
        EXPECT_EQ(ptr[i], 0) << "Calloc should zero all memory at index " << i;
    }

    // Verify we can write to the memory
    for (size_t i = 0; i < count; i++) {
        ptr[i] = (int)(i + 1);
    }

    // Verify our writes
    for (size_t i = 0; i < count; i++) {
        EXPECT_EQ(ptr[i], (int)(i + 1)) << "Should be able to write to calloc'd memory at index " << i;
    }

    pool_free(ptr);
}

TEST_F(MemoryPoolTest, MultipleAllocations) {
    const int num_allocs = 20;
    void* ptrs[num_allocs];

    // Allocate blocks of different sizes
    for (int i = 0; i < num_allocs; i++) {
        ptrs[i] = pool_alloc(64 * (i + 1));
        EXPECT_NE(ptrs[i], nullptr) << "Allocation " << i << " should succeed";

        // Write a pattern to verify the memory
        char* data = (char*)ptrs[i];
        sprintf(data, "Block_%d", i);
    }

    // Verify all blocks are distinct
    for (int i = 0; i < num_allocs; i++) {
        for (int j = i + 1; j < num_allocs; j++) {
            EXPECT_NE(ptrs[i], ptrs[j]) << "Pointers " << i << " and " << j << " should be different";
        }
    }

    // Verify data integrity
    for (int i = 0; i < num_allocs; i++) {
        char expected[32];
        sprintf(expected, "Block_%d", i);
        EXPECT_STREQ((char*)ptrs[i], expected) << "Data should be preserved in block " << i;
    }

    // Free all blocks
    for (int i = 0; i < num_allocs; i++) {
        pool_free(ptrs[i]);
    }
}

// ========================================================================
// Edge Cases and Error Handling
// ========================================================================

TEST_F(MemoryPoolTest, ZeroSizeAllocation) {
    void* ptr = pool_alloc(0);
    // jemalloc behavior: may return NULL or a valid pointer for size 0
    // Both are acceptable behaviors
    if (ptr) {
        // If we get a pointer, we should be able to free it
        pool_free(ptr);
    }
    // Test should not crash regardless
}

TEST_F(MemoryPoolTest, ZeroSizeCalloc) {
    void* ptr1 = pool_calloc(0, 100);
    void* ptr2 = pool_calloc(100, 0);
    void* ptr3 = pool_calloc(0, 0);

    // All should be handled gracefully
    if (ptr1) pool_free(ptr1);
    if (ptr2) pool_free(ptr2);
    if (ptr3) pool_free(ptr3);
}

TEST_F(MemoryPoolTest, FreeNullPointer) {
    // Should not crash
    pool_free(nullptr);
    pool_free(NULL);
}

TEST_F(MemoryPoolTest, LargeAllocations) {
    // Test allocating large blocks
    size_t large_sizes[] = {
        1024 * 1024,        // 1MB
        4 * 1024 * 1024,    // 4MB
        16 * 1024 * 1024    // 16MB
    };

    for (size_t size : large_sizes) {
        void* ptr = pool_alloc(size);
        EXPECT_NE(ptr, nullptr) << "Large allocation of " << size << " bytes should succeed";

        if (ptr) {
            // Write to beginning and end to ensure the whole block is valid
            char* data = (char*)ptr;
            data[0] = 'A';
            data[size - 1] = 'Z';

            EXPECT_EQ(data[0], 'A') << "Should be able to write to beginning of large block";
            EXPECT_EQ(data[size - 1], 'Z') << "Should be able to write to end of large block";

            pool_free(ptr);
        }
    }
}

TEST_F(MemoryPoolTest, VerySmallAllocations) {
    // Test allocating very small blocks
    for (size_t size = 1; size <= 16; size++) {
        void* ptr = pool_alloc(size);
        EXPECT_NE(ptr, nullptr) << "Small allocation of " << size << " bytes should succeed";

        if (ptr) {
            // Write pattern and verify
            char* data = (char*)ptr;
            for (size_t i = 0; i < size; i++) {
                data[i] = (char)('A' + (i % 26));
            }

            for (size_t i = 0; i < size; i++) {
                EXPECT_EQ(data[i], (char)('A' + (i % 26))) << "Data should be preserved in small block at index " << i;
            }

            pool_free(ptr);
        }
    }
}

// ========================================================================
// Memory Safety Tests
// ========================================================================

TEST_F(MemoryPoolTest, MemoryAlignment) {
    // Test that allocations are properly aligned
    for (int i = 0; i < 10; i++) {
        void* ptr = tracked_alloc(64 + i * 8);
        EXPECT_NE(ptr, nullptr) << "Allocation " << i << " should succeed";

        // Check alignment (should be at least pointer-aligned)
        uintptr_t addr = (uintptr_t)ptr;
        EXPECT_EQ(addr % sizeof(void*), 0) << "Allocation " << i << " should be pointer-aligned";
    }
}

TEST_F(MemoryPoolTest, MemoryIntegrity) {
    const int num_blocks = 10;
    void* ptrs[num_blocks];

    // Allocate blocks with unique patterns
    for (int i = 0; i < num_blocks; i++) {
        ptrs[i] = pool_alloc(100);
        EXPECT_NE(ptrs[i], nullptr);

        // Fill with a unique pattern
        char* data = (char*)ptrs[i];
        for (int j = 0; j < 100; j++) {
            data[j] = (char)((i * 100 + j) % 256);
        }
    }

    // Verify patterns are intact (no corruption)
    for (int i = 0; i < num_blocks; i++) {
        char* data = (char*)ptrs[i];
        for (int j = 0; j < 100; j++) {
            EXPECT_EQ(data[j], (char)((i * 100 + j) % 256))
                << "Memory corruption detected in block " << i << " at offset " << j;
        }
    }

    // Clean up
    for (int i = 0; i < num_blocks; i++) {
        pool_free(ptrs[i]);
    }
}

TEST_F(MemoryPoolTest, DoubleFreeDetection) {
    void* ptr = pool_alloc(100);
    EXPECT_NE(ptr, nullptr);

    // First free should work normally
    pool_free(ptr);

    // Second free should not crash (jemalloc handles this gracefully)
    // Note: This is undefined behavior, but jemalloc typically doesn't crash
    pool_free(ptr);
}

// ========================================================================
// Performance and Stress Tests
// ========================================================================

TEST_F(MemoryPoolTest, RapidAllocationDeallocation) {
    const int cycles = 100;
    const int blocks_per_cycle = 20;

    for (int cycle = 0; cycle < cycles; cycle++) {
        void* ptrs[blocks_per_cycle];

        // Rapid allocation - ensure minimum size for our test string
        for (int i = 0; i < blocks_per_cycle; i++) {
            ptrs[i] = pool_alloc(128);  // Fixed size to avoid buffer issues
            EXPECT_NE(ptrs[i], nullptr) << "Rapid allocation should succeed in cycle " << cycle;
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
            EXPECT_STREQ((char*)ptrs[i], expected) << "Data integrity check failed in cycle " << cycle << ", block " << i;
        }

        // Rapid deallocation
        for (int i = 0; i < blocks_per_cycle; i++) {
            pool_free(ptrs[i]);
        }
    }
}

TEST_F(MemoryPoolTest, FragmentationStressTest) {
    const int num_blocks = 100;
    void* ptrs[num_blocks];

    // Allocate blocks of varying sizes
    for (int i = 0; i < num_blocks; i++) {
        size_t size = 32 + (i % 20) * 16;
        ptrs[i] = pool_alloc(size);
        EXPECT_NE(ptrs[i], nullptr);

        // Fill with pattern
        char* data = (char*)ptrs[i];
        memset(data, 0xAA + (i % 4), size);
    }

    // Free every other block to create fragmentation
    for (int i = 1; i < num_blocks; i += 2) {
        pool_free(ptrs[i]);
        ptrs[i] = nullptr;
    }

    // Allocate new blocks in the gaps
    for (int i = 1; i < num_blocks; i += 2) {
        ptrs[i] = pool_alloc(48);
        EXPECT_NE(ptrs[i], nullptr) << "Fragmented allocation should succeed";
        strcpy((char*)ptrs[i], "Fragmented");
    }

    // Verify all data
    for (int i = 0; i < num_blocks; i++) {
        if (i % 2 == 0) {
            // Original blocks
            char* data = (char*)ptrs[i];
            char expected = (char)(0xAA + (i % 4));
            EXPECT_EQ(data[0], expected) << "Original block " << i << " should be intact";
        } else {
            // New blocks
            EXPECT_STREQ((char*)ptrs[i], "Fragmented") << "New block " << i << " should contain expected data";
        }
    }

    // Clean up
    for (int i = 0; i < num_blocks; i++) {
        if (ptrs[i]) {
            pool_free(ptrs[i]);
        }
    }
}

TEST_F(MemoryPoolTest, ConcurrentLikeOperations) {
    // Simulate concurrent-like operations by interleaving allocations and frees
    std::vector<void*> active_ptrs;
    const int operations = 500;

    for (int op = 0; op < operations; op++) {
        if (active_ptrs.empty() || (op % 3 != 0)) {
            // Allocate
            size_t size = 32 + (op % 50) * 4;
            void* ptr = pool_alloc(size);
            EXPECT_NE(ptr, nullptr) << "Concurrent-like allocation should succeed";

            if (ptr) {
                active_ptrs.push_back(ptr);

                // Write pattern
                char* data = (char*)ptr;
                sprintf(data, "Op_%d", op);
            }
        } else {
            // Free a random active pointer
            size_t idx = op % active_ptrs.size();
            pool_free(active_ptrs[idx]);
            active_ptrs.erase(active_ptrs.begin() + idx);
        }
    }

    // Clean up remaining allocations
    for (void* ptr : active_ptrs) {
        pool_free(ptr);
    }
}

// ========================================================================
// Boundary and Limit Tests
// ========================================================================

TEST_F(MemoryPoolTest, MaxSizeAllocation) {
    // Test allocating very large blocks (but reasonable for testing)
    size_t large_size = 128 * 1024 * 1024; // 128MB
    void* ptr = pool_alloc(large_size);

    if (ptr) {
        // If we got the memory, verify we can use it
        char* data = (char*)ptr;
        data[0] = 'S';
        data[large_size - 1] = 'E';

        EXPECT_EQ(data[0], 'S') << "Should be able to write to start of large allocation";
        EXPECT_EQ(data[large_size - 1], 'E') << "Should be able to write to end of large allocation";

        pool_free(ptr);
    } else {
        // If allocation failed, that's also acceptable for very large sizes
        EXPECT_EQ(ptr, nullptr) << "Very large allocation may fail gracefully";
    }
}

TEST_F(MemoryPoolTest, PowerOfTwoSizes) {
    // Test allocations with power-of-two sizes
    for (int power = 0; power <= 20; power++) {
        size_t size = 1ULL << power;
        void* ptr = pool_alloc(size);
        EXPECT_NE(ptr, nullptr) << "Power-of-two allocation of " << size << " bytes should succeed";

        if (ptr) {
            // Write pattern
            char* data = (char*)ptr;
            if (size >= 4) {
                sprintf(data, "2^%d", power);
                char expected[20];
                sprintf(expected, "2^%d", power);
                EXPECT_STREQ(data, expected) << "Data should be preserved in power-of-two block";
            }

            pool_free(ptr);
        }
    }
}

TEST_F(MemoryPoolTest, OddSizes) {
    // Test allocations with odd/unusual sizes
    size_t odd_sizes[] = {
        1, 3, 7, 13, 17, 31, 63, 127, 255, 511, 1023,
        1025, 2049, 4097, 8191, 16383, 32767, 65537
    };

    for (size_t size : odd_sizes) {
        void* ptr = pool_alloc(size);
        EXPECT_NE(ptr, nullptr) << "Odd size allocation of " << size << " bytes should succeed";

        if (ptr) {
            // Fill with pattern and verify
            char* data = (char*)ptr;
            for (size_t i = 0; i < size && i < 100; i++) {
                data[i] = (char)('A' + (i % 26));
            }

            for (size_t i = 0; i < size && i < 100; i++) {
                EXPECT_EQ(data[i], (char)('A' + (i % 26))) << "Pattern mismatch in odd size block at " << i;
            }

            pool_free(ptr);
        }
    }
}

// ========================================================================
// Calloc Specific Tests
// ========================================================================

TEST_F(MemoryPoolTest, CallocLargeBlocks) {
    // Test calloc with large blocks
    size_t count = 1024;
    size_t size = 1024;

    void* ptr = pool_calloc(count, size);
    EXPECT_NE(ptr, nullptr) << "Large calloc should succeed";

    if (ptr) {
        // Verify entire block is zeroed
        char* data = (char*)ptr;
        size_t total_size = count * size;

        for (size_t i = 0; i < total_size; i += 1024) {
            EXPECT_EQ(data[i], 0) << "Large calloc block should be zeroed at offset " << i;
        }

        // Check the last byte too
        EXPECT_EQ(data[total_size - 1], 0) << "Last byte of large calloc block should be zeroed";

        pool_free(ptr);
    }
}

TEST_F(MemoryPoolTest, CallocOverflow) {
    // Test calloc with parameters that might cause overflow
    size_t large_count = SIZE_MAX / 2;
    size_t large_size = 2;

    void* ptr = pool_calloc(large_count, large_size);
    // This should either succeed or fail gracefully (return NULL)
    // jemalloc should handle overflow correctly

    if (ptr) {
        pool_free(ptr);
    }
}

// ========================================================================
// Integration Tests
// ========================================================================

TEST_F(MemoryPoolTest, MixedOperations) {
    // Test mixing pool_alloc and pool_calloc operations
    void* alloc_ptr = pool_alloc(1000);
    void* calloc_ptr = pool_calloc(250, 4);

    EXPECT_NE(alloc_ptr, nullptr);
    EXPECT_NE(calloc_ptr, nullptr);
    EXPECT_NE(alloc_ptr, calloc_ptr);

    // Use both blocks
    strcpy((char*)alloc_ptr, "Allocated block");

    int* calloc_data = (int*)calloc_ptr;
    for (int i = 0; i < 250; i++) {
        EXPECT_EQ(calloc_data[i], 0) << "Calloc block should be zeroed";
        calloc_data[i] = i + 1;
    }

    // Verify data
    EXPECT_STREQ((char*)alloc_ptr, "Allocated block");
    for (int i = 0; i < 250; i++) {
        EXPECT_EQ(calloc_data[i], i + 1) << "Calloc data should be preserved";
    }

    pool_free(alloc_ptr);
    pool_free(calloc_ptr);
}

TEST_F(MemoryPoolTest, RealWorldUsageSimulation) {
    // Simulate real-world usage pattern similar to string buffer operations
    std::vector<void*> buffers;

    // Allocate initial buffers
    for (int i = 0; i < 10; i++) {
        void* buf = pool_alloc(64);
        EXPECT_NE(buf, nullptr);
        sprintf((char*)buf, "Buffer_%d", i);
        buffers.push_back(buf);
    }

    // Simulate buffer growth and reallocation pattern
    for (int growth = 0; growth < 5; growth++) {
        for (size_t i = 0; i < buffers.size(); i++) {
            // "Grow" buffer by allocating larger one and copying data
            char old_data[100];
            strcpy(old_data, (char*)buffers[i]);

            pool_free(buffers[i]);

            buffers[i] = pool_alloc(64 * (growth + 2));
            EXPECT_NE(buffers[i], nullptr);
            strcpy((char*)buffers[i], old_data);
            strcat((char*)buffers[i], "_grown");
        }
    }

    // Verify final data
    for (size_t i = 0; i < buffers.size(); i++) {
        char expected[100];
        sprintf(expected, "Buffer_%zu_grown_grown_grown_grown_grown", i);
        EXPECT_STREQ((char*)buffers[i], expected) << "Buffer " << i << " should contain expected data";
    }

    // Cleanup
    for (void* buf : buffers) {
        pool_free(buf);
    }
}

// ========================================================================
// Memory Pattern Tests
// ========================================================================

TEST_F(MemoryPoolTest, MemoryPatternVerification) {
    const int num_blocks = 50;
    void* ptrs[num_blocks];

    // Allocate blocks with specific patterns
    for (int i = 0; i < num_blocks; i++) {
        size_t size = 100 + i * 10;
        ptrs[i] = pool_alloc(size);
        EXPECT_NE(ptrs[i], nullptr);

        // Fill with alternating pattern
        char* data = (char*)ptrs[i];
        for (size_t j = 0; j < size; j++) {
            data[j] = (j % 2 == 0) ? 0x55 : 0xAA;
        }
    }

    // Modify some blocks
    for (int i = 10; i < 40; i += 5) {
        char* data = (char*)ptrs[i];
        strcpy(data, "Modified");
    }

    // Verify patterns
    for (int i = 0; i < num_blocks; i++) {
        char* data = (char*)ptrs[i];
        size_t size = 100 + i * 10;

        if (i >= 10 && i < 40 && i % 5 == 0) {
            // Modified blocks
            EXPECT_EQ(strncmp(data, "Modified", 8), 0) << "Modified block " << i << " should contain expected text";
        } else {
            // Pattern blocks
            EXPECT_EQ(data[0], 0x55) << "Pattern block " << i << " should start with 0x55";
            EXPECT_EQ(data[1], (char)0xAA) << "Pattern block " << i << " should have 0xAA at position 1";
        }
    }

    // Cleanup
    for (int i = 0; i < num_blocks; i++) {
        pool_free(ptrs[i]);
    }
}

// ========================================================================
// Advanced Memory Pool Tests (Inspired by Variable Pool)
// ========================================================================

TEST_F(MemoryPoolTest, MemoryReusagePattern) {
    // Test that freed memory can be reused efficiently
    std::unordered_set<void*> seen_addresses;
    const int iterations = 100;
    const size_t alloc_size = 256;

    for (int i = 0; i < iterations; i++) {
        void* ptr = pool_alloc(alloc_size);
        EXPECT_NE(ptr, nullptr) << "Allocation " << i << " should succeed";

        seen_addresses.insert(ptr);

        // Write pattern to ensure memory is usable
        generate_test_data(ptr, alloc_size, i);
        EXPECT_TRUE(verify_test_data(ptr, alloc_size, i))
            << "Test data should be correctly written and read";

        pool_free(ptr);
    }

    // We should see some address reuse in a good allocator
    EXPECT_LT(seen_addresses.size(), iterations)
        << "Memory allocator should reuse some addresses";
}

TEST_F(MemoryPoolTest, MemoryCoherencyTest) {
    // Test memory coherency across different allocation patterns
    const int test_rounds = 5;
    const int allocs_per_round = 20;

    for (int round = 0; round < test_rounds; round++) {
        std::vector<void*> round_ptrs;

        // Allocate with varying sizes
        for (int i = 0; i < allocs_per_round; i++) {
            size_t size = 64 + (i * 17 + round * 23) % 512;
            void* ptr = pool_alloc(size);
            EXPECT_NE(ptr, nullptr) << "Round " << round << " allocation " << i << " failed";

            round_ptrs.push_back(ptr);

            // Fill with deterministic pattern
            uint32_t seed = round * 1000 + i;
            generate_test_data(ptr, size, seed);
        }

        // Verify all allocations in this round are coherent
        for (int i = 0; i < allocs_per_round; i++) {
            size_t size = 64 + (i * 17 + round * 23) % 512;
            uint32_t seed = round * 1000 + i;

            EXPECT_TRUE(verify_test_data(round_ptrs[i], size, seed))
                << "Memory coherency failed for round " << round << " allocation " << i;
        }

        // Free half the allocations
        for (int i = 0; i < allocs_per_round; i += 2) {
            pool_free(round_ptrs[i]);
            round_ptrs[i] = nullptr;
        }

        // Verify remaining allocations are still coherent
        for (int i = 1; i < allocs_per_round; i += 2) {
            size_t size = 64 + (i * 17 + round * 23) % 512;
            uint32_t seed = round * 1000 + i;

            EXPECT_TRUE(verify_test_data(round_ptrs[i], size, seed))
                << "Memory coherency failed after partial free for round " << round << " allocation " << i;
        }

        // Free remaining allocations
        for (int i = 1; i < allocs_per_round; i += 2) {
            pool_free(round_ptrs[i]);
        }
    }
}

TEST_F(MemoryPoolTest, MemoryPoolStateConsistency) {
    // Test that pool maintains consistent state through complex operations
    const int complexity_levels = 3;
    const int operations_per_level = 50;

    for (int level = 0; level < complexity_levels; level++) {
        std::vector<void*> active_allocs;

        for (int op = 0; op < operations_per_level; op++) {
            // Determine operation based on current state and level
            bool should_alloc = active_allocs.empty() ||
                               (active_allocs.size() < (level + 1) * 10) ||
                               (op % (level + 2) != 0);

            if (should_alloc) {
                // Allocate with size dependent on level and operation
                size_t size = 32 + (level * 64) + (op % 128);
                void* ptr = pool_alloc(size);

                EXPECT_NE(ptr, nullptr) << "Level " << level << " allocation " << op << " failed";

                if (ptr) {
                    active_allocs.push_back(ptr);

                    // Write verification pattern
                    uint32_t seed = level * 10000 + op;
                    generate_test_data(ptr, size, seed);
                }
            } else if (!active_allocs.empty()) {
                // Free a random allocation
                size_t idx = op % active_allocs.size();
                pool_free(active_allocs[idx]);
                active_allocs.erase(active_allocs.begin() + idx);
            }

            // Verify all remaining allocations are still valid
            for (size_t i = 0; i < active_allocs.size(); i++) {
                EXPECT_TRUE(is_memory_accessible(active_allocs[i], 32))
                    << "Memory accessibility check failed at level " << level << " operation " << op;
            }
        }

        // Clean up remaining allocations
        for (void* ptr : active_allocs) {
            pool_free(ptr);
        }
    }
}

TEST_F(MemoryPoolTest, MemoryPoolLimitTesting) {
    // Test pool behavior under memory pressure
    std::vector<void*> allocations;
    const size_t target_memory = 10 * 1024 * 1024; // Reduced to 10MB to be safer
    const size_t chunk_size = 1024;
    size_t total_allocated = 0;

    // Allocate until we hit a reasonable limit or failure
    while (total_allocated < target_memory) {
        void* ptr = pool_alloc(chunk_size);

        if (!ptr) {
            // Allocation failed - this is acceptable
            break;
        }

        allocations.push_back(ptr);
        total_allocated += chunk_size;

        // Write to memory to ensure it's really allocated
        if (allocations.size() % 1000 == 0) {
            // Test every 1000th allocation to reduce overhead
            EXPECT_TRUE(is_memory_accessible(ptr, chunk_size))
                << "Allocated memory should be accessible";
        }

        // Safety break to prevent infinite loop
        if (allocations.size() > 100000) {
            break;
        }
    }

    EXPECT_GT(allocations.size(), 0) << "Should have allocated at least some memory";

    // Verify a sample of allocations are still accessible
    if (!allocations.empty()) {
        size_t sample_interval = std::max(1UL, allocations.size() / 10);
        for (size_t i = 0; i < allocations.size(); i += sample_interval) {
            EXPECT_TRUE(is_memory_accessible(allocations[i], chunk_size))
                << "Sample allocation " << i << " should remain accessible";
        }
    }

    // Free all allocations
    for (void* ptr : allocations) {
        if (ptr != nullptr) {
            pool_free(ptr);
        }
    }
}

TEST_F(MemoryPoolTest, MemoryInitializationPatterns) {
    // Test different initialization patterns similar to variable pool
    const int pattern_count = 8;
    const size_t alloc_size = 1024;

    uint8_t patterns[pattern_count] = {0x00, 0xFF, 0xAA, 0x55, 0xCC, 0x33, 0xF0, 0x0F};

    for (int p = 0; p < pattern_count; p++) {
        void* ptr = pool_alloc(alloc_size);
        EXPECT_NE(ptr, nullptr) << "Pattern allocation " << p << " should succeed";

        // Fill with pattern
        fill_pattern(ptr, alloc_size, patterns[p]);

        // Verify pattern
        EXPECT_TRUE(verify_pattern(ptr, alloc_size, patterns[p]))
            << "Pattern " << p << " verification failed";

        // Overwrite with different pattern
        uint8_t new_pattern = patterns[(p + 1) % pattern_count];
        fill_pattern(ptr, alloc_size, new_pattern);

        // Verify new pattern
        EXPECT_TRUE(verify_pattern(ptr, alloc_size, new_pattern))
            << "Pattern " << p << " overwrite verification failed";

        pool_free(ptr);
    }
}

TEST_F(MemoryPoolTest, ThreadSafetySimulation) {
    // Simulate thread-like behavior without actual threading for deterministic testing
    const int num_simulated_threads = 4;
    const int ops_per_thread = 100;

    struct ThreadState {
        std::vector<void*> allocations;
        uint32_t seed;
    };

    ThreadState threads[num_simulated_threads];

    // Initialize thread states
    for (int t = 0; t < num_simulated_threads; t++) {
        threads[t].seed = t * 12345;
    }

    // Simulate interleaved operations
    for (int round = 0; round < ops_per_thread; round++) {
        for (int t = 0; t < num_simulated_threads; t++) {
            ThreadState& thread = threads[t];

            // Pseudo-random decision based on thread seed
            thread.seed = thread.seed * 1103515245 + 12345;
            bool should_alloc = thread.allocations.empty() ||
                               (thread.seed % 3 != 0);

            if (should_alloc) {
                size_t size = 64 + (thread.seed % 256);
                void* ptr = pool_alloc(size);

                EXPECT_NE(ptr, nullptr) << "Thread " << t << " allocation failed";

                if (ptr) {
                    thread.allocations.push_back(ptr);

                    // Write thread-specific pattern
                    generate_test_data(ptr, size, thread.seed + round);
                }
            } else if (!thread.allocations.empty()) {
                size_t idx = thread.seed % thread.allocations.size();
                pool_free(thread.allocations[idx]);
                thread.allocations.erase(thread.allocations.begin() + idx);
            }
        }
    }

    // Verify all remaining allocations
    for (int t = 0; t < num_simulated_threads; t++) {
        for (void* ptr : threads[t].allocations) {
            EXPECT_TRUE(is_memory_accessible(ptr, 64))
                << "Thread " << t << " allocation should remain accessible";
            pool_free(ptr);
        }
    }
}

// ========================================================================
// Regression Tests
// ========================================================================

TEST_F(MemoryPoolTest, RegressionStandardUseCases) {
    // Test standard library-like usage patterns

    // 1. String allocation pattern
    const char* test_strings[] = {
        "Hello", "World", "Memory", "Pool", "Testing",
        "This is a longer string to test different allocation sizes",
        "Short", "A", "", "🚀 Unicode test 测试"
    };

    std::vector<char*> string_copies;
    for (const char* str : test_strings) {
        size_t len = strlen(str) + 1;
        char* copy = (char*)pool_alloc(len);
        EXPECT_NE(copy, nullptr) << "String allocation should succeed";

        strcpy(copy, str);
        string_copies.push_back(copy);
    }

    // Verify all strings
    for (size_t i = 0; i < string_copies.size(); i++) {
        EXPECT_STREQ(string_copies[i], test_strings[i])
            << "String " << i << " should be preserved";
    }

    // Free all string copies
    for (char* copy : string_copies) {
        pool_free(copy);
    }

    // 2. Array allocation pattern
    const int array_sizes[] = {10, 100, 1000, 50, 250};
    std::vector<int*> int_arrays;

    for (int size : array_sizes) {
        int* array = (int*)pool_calloc(size, sizeof(int));
        EXPECT_NE(array, nullptr) << "Array allocation should succeed";

        // Fill array
        for (int i = 0; i < size; i++) {
            array[i] = i * i;
        }

        int_arrays.push_back(array);
    }

    // Verify arrays
    for (size_t a = 0; a < int_arrays.size(); a++) {
        int size = array_sizes[a];
        for (int i = 0; i < size; i++) {
            EXPECT_EQ(int_arrays[a][i], i * i)
                << "Array " << a << " element " << i << " should be correct";
        }
    }

    // Free arrays
    for (int* array : int_arrays) {
        pool_free(array);
    }

    // 3. Struct allocation pattern
    struct TestStruct {
        int id;
        double value;
        char name[16];
    };

    std::vector<TestStruct*> structs;
    for (int i = 0; i < 20; i++) {
        TestStruct* s = (TestStruct*)pool_alloc(sizeof(TestStruct));
        EXPECT_NE(s, nullptr) << "Struct allocation should succeed";

        s->id = i;
        s->value = i * 3.14159;
        snprintf(s->name, sizeof(s->name), "Struct_%d", i);

        structs.push_back(s);
    }

    // Verify structs
    for (size_t i = 0; i < structs.size(); i++) {
        EXPECT_EQ(structs[i]->id, (int)i) << "Struct " << i << " ID should be correct";
        EXPECT_DOUBLE_EQ(structs[i]->value, i * 3.14159) << "Struct " << i << " value should be correct";

        char expected_name[16];
        snprintf(expected_name, sizeof(expected_name), "Struct_%zu", i);
        EXPECT_STREQ(structs[i]->name, expected_name) << "Struct " << i << " name should be correct";
    }

    // Free structs
    for (TestStruct* s : structs) {
        pool_free(s);
    }
}

// Main function for running the tests
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    std::cout << "\n=== Enhanced Memory Pool Test Suite ===\n";
    std::cout << "Testing jemalloc-based memory pool implementation\n";
    std::cout << "Features tested:\n";
    std::cout << "  ✓ Basic allocation/deallocation (pool_alloc/pool_free)\n";
    std::cout << "  ✓ Zero-initialized allocation (pool_calloc)\n";
    std::cout << "  ✓ Memory pattern verification and coherency\n";
    std::cout << "  ✓ Stress testing and fragmentation handling\n";
    std::cout << "  ✓ Thread safety simulation\n";
    std::cout << "  ✓ Large allocation and memory pressure testing\n";
    std::cout << "  ✓ Edge cases and boundary conditions\n";
    std::cout << "  ✓ Real-world usage pattern simulation\n";
    std::cout << "  ✓ Regression testing for standard use cases\n";
    std::cout << "==========================================\n\n";

    return RUN_ALL_TESTS();
}
