#include <gtest/gtest.h>
#include "../lib/mempool.h"

class SimpleMemPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        // No pool creation needed - using direct functions
    }

    void TearDown() override {
        // No pool cleanup needed
    }
};

TEST_F(SimpleMemPoolTest, BasicAllocation) {
    void* ptr = pool_alloc(1024);
    EXPECT_NE(ptr, nullptr);
    pool_free(ptr);
}

TEST_F(SimpleMemPoolTest, CallocZerosMemory) {
    size_t size = 1024;
    char* ptr = (char*)pool_calloc(1, size);
    EXPECT_NE(ptr, nullptr);

    // Check that memory is zeroed
    for (size_t i = 0; i < size; i++) {
        EXPECT_EQ(ptr[i], 0);
    }

    pool_free(ptr);
}

TEST_F(SimpleMemPoolTest, MultipleAllocations) {
    void* ptrs[10];

    // Allocate multiple blocks
    for (int i = 0; i < 10; i++) {
        ptrs[i] = pool_alloc(128 * (i + 1));
        EXPECT_NE(ptrs[i], nullptr);
    }

    // Free all blocks
    for (int i = 0; i < 10; i++) {
        pool_free(ptrs[i]);
    }
}

TEST_F(SimpleMemPoolTest, ZeroSizeAllocation) {
    void* ptr = pool_alloc(0);
    // jemalloc may return NULL or a valid pointer for size 0
    // Both behaviors are acceptable
    if (ptr) {
        pool_free(ptr);
    }
}

TEST_F(SimpleMemPoolTest, FreeNullPointer) {
    // Should not crash
    pool_free(nullptr);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
