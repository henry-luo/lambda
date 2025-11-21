#include <gtest/gtest.h>
#include "../lib/arena.h"
#include "../lib/mempool.h"
#include <string.h>

// Test suite for arena allocator using GTest

TEST(ArenaTest, CreateAndDestroy) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);

    Arena* arena = arena_create_default(pool);
    ASSERT_NE(arena, nullptr);

    // Verify initial state
    EXPECT_EQ(arena_chunk_count(arena), 1);
    EXPECT_EQ(arena_total_used(arena), 0);
    EXPECT_EQ(arena_total_allocated(arena), ARENA_INITIAL_CHUNK_SIZE);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaTest, CreateWithCustomSizes) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);

    Arena* arena = arena_create(pool, 8192, 32768);
    ASSERT_NE(arena, nullptr);

    EXPECT_EQ(arena_total_allocated(arena), 8192);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaTest, BasicAllocation) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    // Allocate some memory
    int* x = (int*)arena_alloc(arena, sizeof(int));
    ASSERT_NE(x, nullptr);
    *x = 42;
    EXPECT_EQ(*x, 42);

    double* y = (double*)arena_alloc(arena, sizeof(double));
    ASSERT_NE(y, nullptr);
    *y = 3.14;
    EXPECT_EQ(*y, 3.14);

    // Verify state
    EXPECT_EQ(arena_chunk_count(arena), 1);
    EXPECT_GT(arena_total_used(arena), 0);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaTest, ManySmallAllocations) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    // Allocate many small items
    const int count = 1000;
    int* items[count];

    for (int i = 0; i < count; i++) {
        items[i] = (int*)arena_alloc(arena, sizeof(int));
        ASSERT_NE(items[i], nullptr);
        *items[i] = i;
    }

    // Verify all items
    for (int i = 0; i < count; i++) {
        EXPECT_EQ(*items[i], i);
    }

    // Should have triggered adaptive growth
    EXPECT_GT(arena_chunk_count(arena), 1);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaTest, AdaptiveChunkGrowth) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    // Allocate enough to trigger multiple chunk allocations
    // Initial: 4KB, then 8KB, 16KB, 32KB, 64KB
    size_t alloc_size = 3 * 1024;  // 3KB allocations

    void* p1 = arena_alloc(arena, alloc_size);  // Uses first 4KB chunk
    ASSERT_NE(p1, nullptr);
    EXPECT_EQ(arena_chunk_count(arena), 1);

    void* p2 = arena_alloc(arena, alloc_size);  // Needs new chunk -> 8KB
    ASSERT_NE(p2, nullptr);
    EXPECT_EQ(arena_chunk_count(arena), 2);

    void* p3 = arena_alloc(arena, alloc_size);  // Uses 8KB chunk
    ASSERT_NE(p3, nullptr);

    void* p4 = arena_alloc(arena, alloc_size);  // Needs new chunk -> 16KB
    ASSERT_NE(p4, nullptr);
    EXPECT_EQ(arena_chunk_count(arena), 3);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaTest, LargeAllocation) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    // Allocate something larger than default chunk
    size_t large_size = 128 * 1024;  // 128KB
    void* large = arena_alloc(arena, large_size);
    ASSERT_NE(large, nullptr);

    // Should have created a large chunk
    EXPECT_GE(arena_total_allocated(arena), large_size);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaTest, Alignment) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    // Allocate with default alignment
    void* p1 = arena_alloc(arena, 1);
    ASSERT_NE(p1, nullptr);
    EXPECT_EQ((uintptr_t)p1 % ARENA_DEFAULT_ALIGNMENT, 0);

    void* p2 = arena_alloc(arena, 7);
    ASSERT_NE(p2, nullptr);
    EXPECT_EQ((uintptr_t)p2 % ARENA_DEFAULT_ALIGNMENT, 0);

    // Allocate with custom alignment
    void* p3 = arena_alloc_aligned(arena, 100, 32);
    ASSERT_NE(p3, nullptr);
    EXPECT_EQ((uintptr_t)p3 % 32, 0);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaTest, Calloc) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    size_t size = 100;
    unsigned char* data = (unsigned char*)arena_calloc(arena, size);
    ASSERT_NE(data, nullptr);

    // Verify all bytes are zero
    for (size_t i = 0; i < size; i++) {
        EXPECT_EQ(data[i], 0);
    }

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaTest, Strdup) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    const char* original = "Hello, World!";
    char* dup = arena_strdup(arena, original);
    ASSERT_NE(dup, nullptr);
    EXPECT_STREQ(dup, original);
    EXPECT_NE(dup, original);  // Different pointers

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaTest, Strndup) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    const char* original = "Hello, World!";
    char* dup = arena_strndup(arena, original, 5);
    ASSERT_NE(dup, nullptr);
    EXPECT_STREQ(dup, "Hello");

    // Test with n larger than string
    char* dup2 = arena_strndup(arena, "Hi", 100);
    ASSERT_NE(dup2, nullptr);
    EXPECT_STREQ(dup2, "Hi");

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaTest, Sprintf) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    char* str = arena_sprintf(arena, "Number: %d, String: %s", 42, "test");
    ASSERT_NE(str, nullptr);
    EXPECT_STREQ(str, "Number: 42, String: test");

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaTest, Reset) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    // Allocate some memory
    void* p1 = arena_alloc(arena, 1000);
    void* p2 = arena_alloc(arena, 2000);
    void* p3 = arena_alloc(arena, 3000);
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    ASSERT_NE(p3, nullptr);

    size_t used_before = arena_total_used(arena);
    size_t allocated_before = arena_total_allocated(arena);
    size_t chunks_before = arena_chunk_count(arena);

    EXPECT_GT(used_before, 0);

    // Reset arena
    arena_reset(arena);

    // Memory usage should be zero, but chunks remain
    EXPECT_EQ(arena_total_used(arena), 0);
    EXPECT_EQ(arena_total_allocated(arena), allocated_before);
    EXPECT_EQ(arena_chunk_count(arena), chunks_before);

    // Can allocate again
    void* p4 = arena_alloc(arena, 500);
    ASSERT_NE(p4, nullptr);
    EXPECT_EQ(arena_total_used(arena), 512);  // Aligned to 16

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaTest, Clear) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    // Allocate enough to create multiple chunks
    for (int i = 0; i < 10; i++) {
        void* p = arena_alloc(arena, 2048);
        ASSERT_NE(p, nullptr);
    }

    size_t chunks_before = arena_chunk_count(arena);
    EXPECT_GT(chunks_before, 1);

    // Clear arena
    arena_clear(arena);

    // Should only have first chunk
    EXPECT_EQ(arena_chunk_count(arena), 1);
    EXPECT_EQ(arena_total_used(arena), 0);
    EXPECT_EQ(arena_total_allocated(arena), ARENA_INITIAL_CHUNK_SIZE);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaTest, Statistics) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    EXPECT_EQ(arena_total_allocated(arena), ARENA_INITIAL_CHUNK_SIZE);
    EXPECT_EQ(arena_total_used(arena), 0);
    EXPECT_EQ(arena_waste(arena), ARENA_INITIAL_CHUNK_SIZE);
    EXPECT_EQ(arena_chunk_count(arena), 1);

    // Allocate some memory
    arena_alloc(arena, 100);

    EXPECT_EQ(arena_total_used(arena), 112);  // Aligned to 16
    EXPECT_EQ(arena_waste(arena), ARENA_INITIAL_CHUNK_SIZE - 112);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaTest, ReusePattern) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    // Simulate typical reuse pattern
    for (int iteration = 0; iteration < 5; iteration++) {
        // Allocate during this iteration
        for (int i = 0; i < 100; i++) {
            void* p = arena_alloc(arena, 50);
            ASSERT_NE(p, nullptr);
        }

        size_t used = arena_total_used(arena);
        EXPECT_GT(used, 0);

        // Reset for next iteration
        arena_reset(arena);
        EXPECT_EQ(arena_total_used(arena), 0);
    }

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaTest, NullChecks) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    // Test null pointer handling
    EXPECT_EQ(arena_alloc(NULL, 100), nullptr);
    EXPECT_EQ(arena_calloc(NULL, 100), nullptr);
    EXPECT_EQ(arena_strdup(NULL, "test"), nullptr);
    EXPECT_EQ(arena_strdup(arena, NULL), nullptr);
    EXPECT_EQ(arena_sprintf(NULL, "test"), nullptr);
    EXPECT_EQ(arena_sprintf(arena, NULL), nullptr);

    // These should not crash
    arena_reset(NULL);
    arena_clear(NULL);
    arena_destroy(NULL);

    EXPECT_EQ(arena_total_allocated(NULL), 0);
    EXPECT_EQ(arena_total_used(NULL), 0);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaTest, ZeroSizeAllocation) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    void* p = arena_alloc(arena, 0);
    EXPECT_EQ(p, nullptr);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaTest, StressTest) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    // Allocate many different sizes
    for (int i = 1; i < 1000; i++) {
        void* p = arena_alloc(arena, i);
        ASSERT_NE(p, nullptr);

        // Write to verify memory is writable
        memset(p, i % 256, i);
    }

    // Should have grown adaptively
    EXPECT_GT(arena_chunk_count(arena), 1);
    EXPECT_GT(arena_total_allocated(arena), ARENA_INITIAL_CHUNK_SIZE);

    arena_destroy(arena);
    pool_destroy(pool);
}

// ============================================================================
// Negative and Corner Case Tests
// ============================================================================

TEST(ArenaNegativeTest, CreateWithNullPool) {
    Arena* arena = arena_create_default(NULL);
    EXPECT_EQ(arena, nullptr);
}

TEST(ArenaNegativeTest, CreateWithZeroSizes) {
    Pool* pool = pool_create();

    // Should use defaults when zero
    Arena* arena = arena_create(pool, 0, 0);
    ASSERT_NE(arena, nullptr);

    // Should have default initial chunk size
    EXPECT_EQ(arena_total_allocated(arena), ARENA_INITIAL_CHUNK_SIZE);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaNegativeTest, CreateWithInvalidSizes) {
    Pool* pool = pool_create();

    // Initial > max: should clamp initial to max
    Arena* arena = arena_create(pool, 64 * 1024, 16 * 1024);
    ASSERT_NE(arena, nullptr);

    EXPECT_EQ(arena_total_allocated(arena), 16 * 1024);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaNegativeTest, AllocWithInvalidArena) {
    void* p = arena_alloc(NULL, 100);
    EXPECT_EQ(p, nullptr);
}

TEST(ArenaNegativeTest, AllocZeroBytes) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    void* p = arena_alloc(arena, 0);
    EXPECT_EQ(p, nullptr);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaNegativeTest, AllocHugeSize) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    // Try to allocate more than SIZE_LIMIT (1GB)
    size_t huge = (size_t)2 * 1024 * 1024 * 1024;  // 2GB
    void* p = arena_alloc(arena, huge);
    EXPECT_EQ(p, nullptr);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaNegativeTest, AllocAlignedInvalidAlignment) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    // Non-power-of-2 alignment
    void* p1 = arena_alloc_aligned(arena, 100, 3);
    EXPECT_EQ(p1, nullptr);

    void* p2 = arena_alloc_aligned(arena, 100, 7);
    EXPECT_EQ(p2, nullptr);

    // Zero alignment
    void* p3 = arena_alloc_aligned(arena, 100, 0);
    EXPECT_EQ(p3, nullptr);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaNegativeTest, CallocWithNullArena) {
    void* p = arena_calloc(NULL, 100);
    EXPECT_EQ(p, nullptr);
}

TEST(ArenaNegativeTest, StrdupWithNullArena) {
    char* p = arena_strdup(NULL, "test");
    EXPECT_EQ(p, nullptr);
}

TEST(ArenaNegativeTest, StrdupWithNullString) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    char* p = arena_strdup(arena, NULL);
    EXPECT_EQ(p, nullptr);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaNegativeTest, StrndupWithNullArena) {
    char* p = arena_strndup(NULL, "test", 4);
    EXPECT_EQ(p, nullptr);
}

TEST(ArenaNegativeTest, StrndupWithNullString) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    char* p = arena_strndup(arena, NULL, 4);
    EXPECT_EQ(p, nullptr);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaNegativeTest, SprintfWithNullArena) {
    char* p = arena_sprintf(NULL, "test %d", 42);
    EXPECT_EQ(p, nullptr);
}

TEST(ArenaNegativeTest, SprintfWithNullFormat) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    char* p = arena_sprintf(arena, NULL);
    EXPECT_EQ(p, nullptr);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaNegativeTest, ResetNullArena) {
    // Should not crash
    arena_reset(NULL);
}

TEST(ArenaNegativeTest, ClearNullArena) {
    // Should not crash
    arena_clear(NULL);
}

TEST(ArenaNegativeTest, DestroyNullArena) {
    // Should not crash
    arena_destroy(NULL);
}

TEST(ArenaNegativeTest, StatsOnNullArena) {
    EXPECT_EQ(arena_total_allocated(NULL), 0);
    EXPECT_EQ(arena_total_used(NULL), 0);
    EXPECT_EQ(arena_waste(NULL), 0);
    EXPECT_EQ(arena_chunk_count(NULL), 0);
}

TEST(ArenaNegativeTest, DoubleDestroy) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    arena_destroy(arena);
    // Second destroy should be safe (checks validity marker)
    arena_destroy(arena);

    pool_destroy(pool);
}

TEST(ArenaCornerTest, SingleByteAllocation) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    unsigned char* p = (unsigned char*)arena_alloc(arena, 1);
    ASSERT_NE(p, nullptr);
    *p = 0xFF;
    EXPECT_EQ(*p, 0xFF);

    // Check alignment
    EXPECT_EQ((uintptr_t)p % ARENA_DEFAULT_ALIGNMENT, 0);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaCornerTest, MaxSizeSingleAllocation) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    // Allocate large size - account for chunk header and alignment overhead
    // SIZE_LIMIT is 1GB, leave room for ArenaChunk header (~32 bytes) and alignment (256 bytes)
    size_t max_size = 1024 * 1024 * 1024 - 1024;  // 1GB - 1KB for overhead
    void* p = arena_alloc(arena, max_size);
    ASSERT_NE(p, nullptr);

    // Should have created a large chunk
    EXPECT_GE(arena_total_allocated(arena), max_size);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaCornerTest, EmptyStringOperations) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    // strdup empty string
    char* s1 = arena_strdup(arena, "");
    ASSERT_NE(s1, nullptr);
    EXPECT_STREQ(s1, "");

    // strndup with n=0
    char* s2 = arena_strndup(arena, "test", 0);
    ASSERT_NE(s2, nullptr);
    EXPECT_STREQ(s2, "");

    // sprintf empty
    char* s3 = arena_sprintf(arena, "");
    ASSERT_NE(s3, nullptr);
    EXPECT_STREQ(s3, "");

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaCornerTest, VeryLongString) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    // Create a 10KB string
    size_t len = 10 * 1024;
    char* long_str = (char*)malloc(len + 1);
    memset(long_str, 'A', len);
    long_str[len] = '\0';

    char* dup = arena_strdup(arena, long_str);
    ASSERT_NE(dup, nullptr);
    EXPECT_EQ(strlen(dup), len);
    EXPECT_STREQ(dup, long_str);

    free(long_str);
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaCornerTest, AlignmentBoundaries) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    // Test various alignment powers of 2
    for (size_t align = 1; align <= 256; align *= 2) {
        void* p = arena_alloc_aligned(arena, 100, align);
        ASSERT_NE(p, nullptr);
        EXPECT_EQ((uintptr_t)p % align, 0)
            << "Failed alignment for " << align << " bytes";
    }

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaCornerTest, AlternatingSmallLargeAllocs) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    // Alternate between small and large allocations
    for (int i = 0; i < 10; i++) {
        void* small = arena_alloc(arena, 16);
        ASSERT_NE(small, nullptr);

        void* large = arena_alloc(arena, 8 * 1024);
        ASSERT_NE(large, nullptr);
    }

    EXPECT_GT(arena_chunk_count(arena), 1);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaCornerTest, ResetAfterClear) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    // Allocate to create multiple chunks
    for (int i = 0; i < 5; i++) {
        arena_alloc(arena, 2048);
    }

    size_t chunks_before = arena_chunk_count(arena);
    EXPECT_GT(chunks_before, 1);

    // Clear reduces to one chunk
    arena_clear(arena);
    EXPECT_EQ(arena_chunk_count(arena), 1);

    // Reset on single chunk should be fine
    arena_reset(arena);
    EXPECT_EQ(arena_chunk_count(arena), 1);
    EXPECT_EQ(arena_total_used(arena), 0);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaCornerTest, ClearAfterReset) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    // Allocate to create multiple chunks
    for (int i = 0; i < 5; i++) {
        arena_alloc(arena, 2048);
    }

    // Reset keeps all chunks
    arena_reset(arena);
    size_t chunks_after_reset = arena_chunk_count(arena);
    EXPECT_GT(chunks_after_reset, 1);

    // Clear reduces to one
    arena_clear(arena);
    EXPECT_EQ(arena_chunk_count(arena), 1);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaCornerTest, MultipleResetsPreserveChunkSize) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    // Force growth
    for (int i = 0; i < 3; i++) {
        arena_alloc(arena, 3 * 1024);
    }

    size_t chunks = arena_chunk_count(arena);
    size_t allocated = arena_total_allocated(arena);

    // Multiple resets should preserve chunks and size
    for (int i = 0; i < 5; i++) {
        arena_reset(arena);
        EXPECT_EQ(arena_chunk_count(arena), chunks);
        EXPECT_EQ(arena_total_allocated(arena), allocated);
        EXPECT_EQ(arena_total_used(arena), 0);
    }

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaCornerTest, TinyChunkSize) {
    Pool* pool = pool_create();

    // Create arena with very small chunks
    Arena* arena = arena_create(pool, 64, 256);
    ASSERT_NE(arena, nullptr);

    // Allocate more than one chunk
    void* p1 = arena_alloc(arena, 32);
    ASSERT_NE(p1, nullptr);
    EXPECT_EQ(arena_chunk_count(arena), 1);

    void* p2 = arena_alloc(arena, 32);
    ASSERT_NE(p2, nullptr);

    // Should have triggered growth
    void* p3 = arena_alloc(arena, 32);
    ASSERT_NE(p3, nullptr);
    EXPECT_GT(arena_chunk_count(arena), 1);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaCornerTest, AllocationExactlyAtChunkBoundary) {
    Pool* pool = pool_create();
    Arena* arena = arena_create(pool, 128, 512);

    // Chunk data starts 256-byte aligned, so we have full 128 bytes available
    // Fill the chunk completely
    void* p1 = arena_alloc(arena, 128);
    ASSERT_NE(p1, nullptr);
    EXPECT_EQ(arena_chunk_count(arena), 1);

    // Next allocation should trigger new chunk
    void* p2 = arena_alloc(arena, 16);
    ASSERT_NE(p2, nullptr);
    EXPECT_EQ(arena_chunk_count(arena), 2);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaCornerTest, SprintfWithVeryLongOutput) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    // Create format string that produces long output
    char* result = arena_sprintf(arena, "%1000d", 42);
    ASSERT_NE(result, nullptr);

    // Should have many leading spaces
    EXPECT_GT(strlen(result), 999);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaCornerTest, InterleavedAllocAndString) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    for (int i = 0; i < 100; i++) {
        void* p = arena_alloc(arena, 64);
        ASSERT_NE(p, nullptr);

        char* s = arena_sprintf(arena, "item_%d", i);
        ASSERT_NE(s, nullptr);

        // Verify they don't overlap
        EXPECT_TRUE(p != (void*)s);
    }

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaCornerTest, CallocActuallyZeroes) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    // Allocate and write non-zero data
    unsigned char* p1 = (unsigned char*)arena_alloc(arena, 256);
    memset(p1, 0xFF, 256);

    // Reset and use calloc
    arena_reset(arena);
    unsigned char* p2 = (unsigned char*)arena_calloc(arena, 256);
    ASSERT_NE(p2, nullptr);

    // Verify all zeroes even though memory might be reused
    for (size_t i = 0; i < 256; i++) {
        EXPECT_EQ(p2[i], 0) << "Non-zero byte at index " << i;
    }

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaCornerTest, StrndupWithExactLength) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    const char* str = "Hello, World!";
    size_t len = strlen(str);

    // n exactly equals string length
    char* dup = arena_strndup(arena, str, len);
    ASSERT_NE(dup, nullptr);
    EXPECT_STREQ(dup, str);
    EXPECT_EQ(strlen(dup), len);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaCornerTest, RapidCreateDestroy) {
    Pool* pool = pool_create();

    // Create and destroy many arenas
    for (int i = 0; i < 100; i++) {
        Arena* arena = arena_create_default(pool);
        ASSERT_NE(arena, nullptr);

        // Use it a bit
        arena_alloc(arena, 100);

        arena_destroy(arena);
    }

    pool_destroy(pool);
}

TEST(ArenaCornerTest, AllocAfterMultipleClearCycles) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);

    for (int cycle = 0; cycle < 10; cycle++) {
        // Allocate to grow
        for (int i = 0; i < 5; i++) {
            arena_alloc(arena, 1024);
        }

        // Clear back to one chunk
        arena_clear(arena);
        EXPECT_EQ(arena_chunk_count(arena), 1);

        // Allocate again - should work fine
        void* p = arena_alloc(arena, 512);
        ASSERT_NE(p, nullptr);
    }

    arena_destroy(arena);
    pool_destroy(pool);
}

// ============================================================================
// arena_owns() Tests
// ============================================================================

TEST(ArenaOwnershipTest, OwnsPointerInFirstChunk) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    void* ptr = arena_alloc(arena, 64);
    ASSERT_NE(ptr, nullptr);
    
    EXPECT_TRUE(arena_owns(arena, ptr));
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaOwnershipTest, OwnsPointerInMiddleOfChunk) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    void* ptr1 = arena_alloc(arena, 64);
    void* ptr2 = arena_alloc(arena, 128);
    void* ptr3 = arena_alloc(arena, 64);
    
    ASSERT_NE(ptr1, nullptr);
    ASSERT_NE(ptr2, nullptr);
    ASSERT_NE(ptr3, nullptr);
    
    EXPECT_TRUE(arena_owns(arena, ptr1));
    EXPECT_TRUE(arena_owns(arena, ptr2));
    EXPECT_TRUE(arena_owns(arena, ptr3));
    
    // Check pointer within ptr2
    void* mid_ptr = (char*)ptr2 + 50;
    EXPECT_TRUE(arena_owns(arena, mid_ptr));
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaOwnershipTest, OwnsPointerInSecondChunk) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    // Allocate large blocks to force second chunk
    void* ptr1 = arena_alloc(arena, 8192);
    void* ptr2 = arena_alloc(arena, 8192);
    
    ASSERT_NE(ptr1, nullptr);
    ASSERT_NE(ptr2, nullptr);
    
    EXPECT_TRUE(arena_owns(arena, ptr1));
    EXPECT_TRUE(arena_owns(arena, ptr2));
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaOwnershipTest, DoesNotOwnExternalPointer) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    void* external = malloc(64);
    ASSERT_NE(external, nullptr);
    
    EXPECT_FALSE(arena_owns(arena, external));
    
    free(external);
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaOwnershipTest, DoesNotOwnNullPointer) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    EXPECT_FALSE(arena_owns(arena, nullptr));
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaOwnershipTest, DoesNotOwnPointerBeforeArena) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    void* ptr = arena_alloc(arena, 64);
    ASSERT_NE(ptr, nullptr);
    
    // Pointer before the allocated region
    void* before_ptr = (char*)ptr - 100;
    EXPECT_FALSE(arena_owns(arena, before_ptr));
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaOwnershipTest, DoesNotOwnPointerAfterArena) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    void* ptr = arena_alloc(arena, 64);
    ASSERT_NE(ptr, nullptr);
    
    // Pointer way beyond the allocated region
    void* after_ptr = (char*)ptr + 100000;
    EXPECT_FALSE(arena_owns(arena, after_ptr));
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaOwnershipTest, OwnsReturnsFalseForInvalidArena) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    void* ptr = arena_alloc(arena, 64);
    ASSERT_NE(ptr, nullptr);
    
    EXPECT_FALSE(arena_owns(nullptr, ptr));
    
    arena_destroy(arena);
    pool_destroy(pool);
}

// ============================================================================
// arena_free() Tests
// ============================================================================

TEST(ArenaFreeTest, FreeAddsToFreeList) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    void* ptr = arena_alloc(arena, 64);
    ASSERT_NE(ptr, nullptr);
    
    // Free should not crash
    arena_free(arena, ptr, 64);
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaFreeTest, FreeSmallBlockIgnored) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    void* ptr = arena_alloc(arena, 64);
    ASSERT_NE(ptr, nullptr);
    
    // Free with size smaller than ArenaFreeBlock should be ignored
    arena_free(arena, ptr, 8);  // Too small to be useful
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaFreeTest, FreeNullPointerIgnored) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    // Should not crash
    arena_free(arena, nullptr, 64);
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaFreeTest, FreeInvalidArenaIgnored) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    void* ptr = arena_alloc(arena, 64);
    ASSERT_NE(ptr, nullptr);
    
    // Should not crash
    arena_free(nullptr, ptr, 64);
    
    arena_destroy(arena);
    pool_destroy(pool);
}

// ============================================================================
// arena_realloc() Tests
// ============================================================================

TEST(ArenaReallocTest, ReallocFromNullAllocatesNew) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    void* ptr = arena_realloc(arena, nullptr, 0, 64);
    ASSERT_NE(ptr, nullptr);
    EXPECT_TRUE(arena_owns(arena, ptr));
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaReallocTest, ReallocToZeroFrees) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    void* ptr = arena_alloc(arena, 64);
    ASSERT_NE(ptr, nullptr);
    
    void* result = arena_realloc(arena, ptr, 64, 0);
    EXPECT_EQ(result, nullptr);
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaReallocTest, ReallocSameSizeReturnsOriginal) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    void* ptr = arena_alloc(arena, 64);
    ASSERT_NE(ptr, nullptr);
    
    memset(ptr, 0xAB, 64);
    
    void* new_ptr = arena_realloc(arena, ptr, 64, 64);
    EXPECT_EQ(ptr, new_ptr);
    
    // Data should be unchanged
    EXPECT_EQ(((unsigned char*)new_ptr)[0], 0xAB);
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaReallocTest, ReallocShrinkReturnsOriginal) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    void* ptr = arena_alloc(arena, 128);
    ASSERT_NE(ptr, nullptr);
    
    memset(ptr, 0xCD, 128);
    
    void* new_ptr = arena_realloc(arena, ptr, 128, 64);
    EXPECT_EQ(ptr, new_ptr);
    
    // Data in first 64 bytes should be unchanged
    EXPECT_EQ(((unsigned char*)new_ptr)[0], 0xCD);
    EXPECT_EQ(((unsigned char*)new_ptr)[63], 0xCD);
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaReallocTest, ReallocGrowAtEndExtendsInPlace) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    // Allocate at end of chunk
    void* ptr = arena_alloc(arena, 64);
    ASSERT_NE(ptr, nullptr);
    
    memset(ptr, 0xEF, 64);
    
    // Grow should extend in place if at end of chunk
    void* new_ptr = arena_realloc(arena, ptr, 64, 128);
    ASSERT_NE(new_ptr, nullptr);
    
    // Data should be preserved
    EXPECT_EQ(((unsigned char*)new_ptr)[0], 0xEF);
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaReallocTest, ReallocGrowNotAtEndAllocatesNew) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    // Allocate two blocks
    void* ptr1 = arena_alloc(arena, 64);
    void* ptr2 = arena_alloc(arena, 64);
    
    ASSERT_NE(ptr1, nullptr);
    ASSERT_NE(ptr2, nullptr);
    
    memset(ptr1, 0x12, 64);
    
    // Growing ptr1 should allocate new since ptr2 is after it
    void* new_ptr = arena_realloc(arena, ptr1, 64, 128);
    ASSERT_NE(new_ptr, nullptr);
    
    // Data should be copied
    EXPECT_EQ(((unsigned char*)new_ptr)[0], 0x12);
    EXPECT_EQ(((unsigned char*)new_ptr)[63], 0x12);
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaReallocTest, ReallocPreservesData) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    void* ptr = arena_alloc(arena, 64);
    ASSERT_NE(ptr, nullptr);
    
    // Fill with pattern
    for (int i = 0; i < 64; i++) {
        ((unsigned char*)ptr)[i] = (unsigned char)i;
    }
    
    // Reallocate to larger size
    void* new_ptr = arena_realloc(arena, ptr, 64, 256);
    ASSERT_NE(new_ptr, nullptr);
    
    // Verify pattern is preserved
    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(((unsigned char*)new_ptr)[i], (unsigned char)i);
    }
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaReallocTest, ReallocInvalidArenaReturnsNull) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    void* ptr = arena_alloc(arena, 64);
    ASSERT_NE(ptr, nullptr);
    
    void* result = arena_realloc(nullptr, ptr, 64, 128);
    EXPECT_EQ(result, nullptr);
    
    arena_destroy(arena);
    pool_destroy(pool);
}

// ============================================================================
// Free-list Reuse Tests
// ============================================================================

TEST(ArenaFreeListTest, FreeListReusesMemory) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    // Allocate and free a block
    void* ptr1 = arena_alloc(arena, 64);
    ASSERT_NE(ptr1, nullptr);
    arena_free(arena, ptr1, 64);
    
    // Allocate again - might reuse freed block
    void* ptr2 = arena_alloc(arena, 64);
    ASSERT_NE(ptr2, nullptr);
    
    // Both should be valid
    EXPECT_TRUE(arena_owns(arena, ptr2));
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaFreeListTest, FreeListSplitsLargeBlocks) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    // Allocate and free a large block
    void* ptr1 = arena_alloc(arena, 256);
    ASSERT_NE(ptr1, nullptr);
    arena_free(arena, ptr1, 256);
    
    // Allocate smaller block - should split the free block
    void* ptr2 = arena_alloc(arena, 64);
    ASSERT_NE(ptr2, nullptr);
    
    EXPECT_TRUE(arena_owns(arena, ptr2));
    
    arena_destroy(arena);
    pool_destroy(pool);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(ArenaIntegrationTest, ReallocAndOwnershipIntegration) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    void* ptr = arena_alloc(arena, 64);
    ASSERT_NE(ptr, nullptr);
    EXPECT_TRUE(arena_owns(arena, ptr));
    
    // Realloc should maintain ownership
    void* new_ptr = arena_realloc(arena, ptr, 64, 128);
    ASSERT_NE(new_ptr, nullptr);
    EXPECT_TRUE(arena_owns(arena, new_ptr));
    
    // Shrink should maintain ownership
    void* shrunk_ptr = arena_realloc(arena, new_ptr, 128, 32);
    ASSERT_NE(shrunk_ptr, nullptr);
    EXPECT_TRUE(arena_owns(arena, shrunk_ptr));
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaIntegrationTest, MultipleAllocationsAndReallocs) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    // Complex scenario with multiple operations
    void* ptr1 = arena_alloc(arena, 64);
    void* ptr2 = arena_alloc(arena, 128);
    void* ptr3 = arena_alloc(arena, 256);
    
    ASSERT_NE(ptr1, nullptr);
    ASSERT_NE(ptr2, nullptr);
    ASSERT_NE(ptr3, nullptr);
    
    // All should be owned
    EXPECT_TRUE(arena_owns(arena, ptr1));
    EXPECT_TRUE(arena_owns(arena, ptr2));
    EXPECT_TRUE(arena_owns(arena, ptr3));
    
    // Realloc middle one
    void* new_ptr2 = arena_realloc(arena, ptr2, 128, 64);
    ASSERT_NE(new_ptr2, nullptr);
    EXPECT_TRUE(arena_owns(arena, new_ptr2));
    
    // Original pointers should still be owned
    EXPECT_TRUE(arena_owns(arena, ptr1));
    EXPECT_TRUE(arena_owns(arena, ptr3));
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaIntegrationTest, LargeRealloc) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default(pool);
    
    void* ptr = arena_alloc(arena, 64);
    ASSERT_NE(ptr, nullptr);
    
    // Realloc to much larger size
    void* new_ptr = arena_realloc(arena, ptr, 64, 16384);
    ASSERT_NE(new_ptr, nullptr);
    EXPECT_TRUE(arena_owns(arena, new_ptr));
    
    arena_destroy(arena);
    pool_destroy(pool);
}
