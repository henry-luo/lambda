#include <gtest/gtest.h>
#include "../../lib/arena.h"
#include "../../lib/mempool.h"
#include "../../lambda/lambda-data.hpp"  // For container types and functions
#include <string.h>

// Test suite for arena allocator using GTest

TEST(ArenaTest, CreateAndDestroy) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);

    Arena* arena = arena_create_default();
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

    Arena* arena = arena_create(8192, 32768);
    ASSERT_NE(arena, nullptr);

    EXPECT_EQ(arena_total_allocated(arena), 8192);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaTest, BasicAllocation) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

    char* str = arena_sprintf(arena, "Number: %d, String: %s", 42, "test");
    ASSERT_NE(str, nullptr);
    EXPECT_STREQ(str, "Number: 42, String: test");

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaTest, Reset) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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

TEST(ArenaTest, DetailedStatisticsTrackRewindAndBacking) {
    Pool* pool = pool_create();
    ASSERT_NE(pool, nullptr);
    Arena* arena = arena_create_default();
    ASSERT_NE(arena, nullptr);

    void* first = arena_alloc(arena, 64);
    ArenaMark mark = arena_mark(arena);
    void* second = arena_alloc(arena, 64);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    arena_rewind(arena, mark);
    void* reused = arena_alloc(arena, 64);
    EXPECT_EQ(reused, second);
    ASSERT_NE(arena_alloc(arena, 64), nullptr);  // guard keeps the retired block interior
    arena_retire(arena, first, 64);

    ArenaStats stats;
    arena_get_stats(arena, &stats);
    EXPECT_GT(stats.backing_bytes, stats.committed_bytes);
    EXPECT_EQ(stats.committed_bytes, ARENA_INITIAL_CHUNK_SIZE);
    EXPECT_EQ(stats.active_bytes, 128u);
    EXPECT_EQ(stats.retired_bytes, 64u);
    EXPECT_EQ(stats.allocation_count, 4u);
    EXPECT_EQ(stats.rewind_count, 1u);
    EXPECT_EQ(stats.retire_count, 1u);
    EXPECT_EQ(stats.fresh_chunk_count, 1u);
    EXPECT_GE(stats.high_water_active_bytes, stats.active_bytes);

    arena_reset(arena);
    arena_get_stats(arena, &stats);
    EXPECT_EQ(stats.active_bytes, 0u);
    EXPECT_EQ(stats.reset_count, 1u);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaTest, ReusePattern) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

    void* p = arena_alloc(arena, 0);
    EXPECT_EQ(p, nullptr);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaTest, StressTest) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default();

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

TEST(ArenaNegativeTest, CreateWithoutPool) {
    Arena* arena = arena_create_default();
    ASSERT_NE(arena, nullptr);
    arena_destroy(arena);
}

TEST(ArenaNegativeTest, CreateWithZeroSizes) {
    Pool* pool = pool_create();

    // Should use defaults when zero
    Arena* arena = arena_create(0, 0);
    ASSERT_NE(arena, nullptr);

    // Should have default initial chunk size
    EXPECT_EQ(arena_total_allocated(arena), ARENA_INITIAL_CHUNK_SIZE);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaNegativeTest, CreateWithInvalidSizes) {
    Pool* pool = pool_create();

    // Initial > max: should clamp initial to max
    Arena* arena = arena_create(64 * 1024, 16 * 1024);
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
    Arena* arena = arena_create_default();

    void* p = arena_alloc(arena, 0);
    EXPECT_EQ(p, nullptr);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaNegativeTest, AllocHugeSize) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default();

    // Try to allocate more than SIZE_LIMIT (1GB)
    size_t huge = (size_t)2 * 1024 * 1024 * 1024;  // 2GB
    void* p = arena_alloc(arena, huge);
    EXPECT_EQ(p, nullptr);

    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaNegativeTest, AllocAlignedInvalidAlignment) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

    arena_destroy(arena);
    // Second destroy should be safe (checks validity marker)
    arena_destroy(arena);

    pool_destroy(pool);
}

TEST(ArenaCornerTest, SingleByteAllocation) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create(64, 256);
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
    Arena* arena = arena_create(128, 512);

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
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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
    Arena* arena = arena_create_default();

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
        Arena* arena = arena_create_default();
        ASSERT_NE(arena, nullptr);

        // Use it a bit
        arena_alloc(arena, 100);

        arena_destroy(arena);
    }

    pool_destroy(pool);
}

TEST(ArenaCornerTest, AllocAfterMultipleClearCycles) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default();

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

TEST(ArenaOwnershipTest, IndexedChunksTrackLiveBoundsAcrossRewindResetAndClear) {
    Arena* arena = arena_create(256, 256);
    Arena* other = arena_create(256, 256);
    ASSERT_NE(arena, nullptr);
    ASSERT_NE(other, nullptr);
    void* owned[256];
    void* foreign[256];
    for (size_t i = 0; i < 256; i++) {
        // Interleaved owners prevent address order from implying ownership.
        owned[i] = arena_alloc(arena, 256);
        foreign[i] = arena_alloc(other, 256);
        ASSERT_NE(owned[i], nullptr);
        ASSERT_NE(foreign[i], nullptr);
    }
    for (size_t i = 0; i < 256; i++) {
        size_t slot = (i * 73) % 256;
        EXPECT_TRUE(arena_owns(arena, owned[slot]));
        EXPECT_TRUE(arena_owns(arena, (char*)owned[slot] + 255));
        EXPECT_FALSE(arena_owns(arena, (char*)owned[slot] + 256));
        EXPECT_FALSE(arena_owns(arena, (char*)owned[slot] - 1));
        EXPECT_FALSE(arena_owns(arena, foreign[slot]));
    }
    ArenaMark mark = arena_mark(arena);
    void* tail = arena_alloc(arena, 256);
    ASSERT_NE(tail, nullptr);
    EXPECT_TRUE(arena_owns(arena, tail));
    arena_rewind(arena, mark);
    EXPECT_FALSE(arena_owns(arena, tail));
    EXPECT_TRUE(arena_owns(arena, owned[128]));
    EXPECT_EQ(arena_alloc(arena, 256), tail);
    EXPECT_TRUE(arena_owns(arena, tail));
    arena_reset(arena);
    for (void* pointer : owned) EXPECT_FALSE(arena_owns(arena, pointer));
    EXPECT_FALSE(arena_owns(arena, tail));
    EXPECT_EQ(arena_alloc(arena, 256), owned[0]);
    EXPECT_TRUE(arena_owns(arena, owned[0]));
    arena_clear(arena);
    EXPECT_FALSE(arena_owns(arena, owned[0]));
    for (size_t i = 0; i < 256; i++) {
        void* pointer = arena_alloc(arena, 256);
        ASSERT_NE(pointer, nullptr);
        EXPECT_TRUE(arena_owns(arena, pointer));
        EXPECT_FALSE(arena_owns(arena, foreign[i]));
    }
    arena_destroy(arena);
    arena_destroy(other);
}

TEST(ArenaOwnershipTest, OwnsPointerInFirstChunk) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default();
    
    void* ptr = arena_alloc(arena, 64);
    ASSERT_NE(ptr, nullptr);
    
    EXPECT_TRUE(arena_owns(arena, ptr));
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaOwnershipTest, OwnsPointerInMiddleOfChunk) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default();
    
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
    Arena* arena = arena_create_default();
    
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
    Arena* arena = arena_create_default();
    
    void* external = malloc(64);
    ASSERT_NE(external, nullptr);
    
    EXPECT_FALSE(arena_owns(arena, external));
    
    free(external);
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaOwnershipTest, DoesNotOwnNullPointer) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default();
    
    EXPECT_FALSE(arena_owns(arena, nullptr));
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaOwnershipTest, DoesNotOwnPointerBeforeArena) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default();
    
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
    Arena* arena = arena_create_default();
    
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
    Arena* arena = arena_create_default();
    
    void* ptr = arena_alloc(arena, 64);
    ASSERT_NE(ptr, nullptr);
    
    EXPECT_FALSE(arena_owns(nullptr, ptr));
    
    arena_destroy(arena);
    pool_destroy(pool);
}

// ============================================================================
// Tail rewind (the only partial free) and scratch ownership
// ============================================================================

TEST(ArenaRewindTest, RewindReturnsTailAndKeepsChunks) {
    Arena* arena = arena_create(1024, 4096);
    ASSERT_NE(arena, nullptr);
    void* keep = arena_alloc(arena, 64);
    ASSERT_NE(keep, nullptr);
    memset(keep, 0x5A, 64);
    size_t used_at_mark = arena_total_used(arena);
    ArenaMark mark = arena_mark(arena);

    // spill into later chunks
    for (int i = 0; i < 40; i++) ASSERT_NE(arena_alloc(arena, 256), nullptr);
    size_t chunks = arena_chunk_count(arena);
    size_t allocated = arena_total_allocated(arena);
    EXPECT_GT(chunks, 1u);

    arena_rewind(arena, mark);
    EXPECT_EQ(arena_total_used(arena), used_at_mark);
    EXPECT_EQ(arena_chunk_count(arena), chunks);
    EXPECT_EQ(arena_total_allocated(arena), allocated);
    EXPECT_EQ(((unsigned char*)keep)[63], 0x5A);

    // the retained chunks serve the same pattern again without growth
    for (int i = 0; i < 40; i++) ASSERT_NE(arena_alloc(arena, 256), nullptr);
    EXPECT_EQ(arena_total_allocated(arena), allocated);
    arena_destroy(arena);
}

TEST(ArenaRewindTest, NestedMarksRewindInOrder) {
    Arena* arena = arena_create_default();
    ASSERT_NE(arena, nullptr);
    ArenaMark outer = arena_mark(arena);
    void* a = arena_alloc(arena, 32);
    ArenaMark inner = arena_mark(arena);
    void* b = arena_alloc(arena, 32);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    arena_rewind(arena, inner);
    EXPECT_EQ(arena_alloc(arena, 32), b);
    arena_rewind(arena, outer);
    EXPECT_EQ(arena_total_used(arena), 0u);
    EXPECT_EQ(arena_alloc(arena, 32), a);
    arena_destroy(arena);
}

TEST(ArenaRewindTest, NullAndEmptyMarksAreIgnored) {
    arena_rewind(nullptr, ArenaMark{});
    ArenaMark none = arena_mark(nullptr);
    EXPECT_EQ(none.chunk, nullptr);
    Arena* arena = arena_create_default();
    ASSERT_NE(arena, nullptr);
    ASSERT_NE(arena_alloc(arena, 16), nullptr);
    arena_rewind(arena, ArenaMark{});
    EXPECT_EQ(arena_total_used(arena), 16u);
    arena_destroy(arena);
}

// ============================================================================
// Retired list (arena_retire): retained for reuse, never discarded
// ============================================================================

TEST(ArenaRetireTest, RetiredBlockIsReusedBySameSizeAllocation) {
    Arena* arena = arena_create_default();
    ASSERT_NE(arena, nullptr);
    void* first = arena_alloc(arena, 64);
    void* guard = arena_alloc(arena, 64);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(guard, nullptr);
    arena_retire(arena, first, 64);

    ArenaStats stats;
    arena_get_stats(arena, &stats);
    EXPECT_EQ(stats.retired_bytes, 64u);
    EXPECT_EQ(stats.active_bytes, 64u);
    EXPECT_EQ(stats.bump_used_bytes, 128u);

    EXPECT_EQ(arena_alloc(arena, 64), first);
    arena_get_stats(arena, &stats);
    EXPECT_EQ(stats.retired_bytes, 0u);
    EXPECT_EQ(stats.retire_count, 1u);
    EXPECT_EQ(stats.reuse_hits, 1u);
    arena_destroy(arena);
}

TEST(ArenaRetireTest, AdjacentRetiredBlocksCoalesceAndSplit) {
    Arena* arena = arena_create_default();
    ASSERT_NE(arena, nullptr);
    void* a = arena_alloc(arena, 64);
    void* b = arena_alloc(arena, 64);
    void* guard = arena_alloc(arena, 64);
    ASSERT_NE(guard, nullptr);
    arena_retire(arena, a, 64);
    arena_retire(arena, b, 64);

    ArenaStats stats;
    arena_get_stats(arena, &stats);
    EXPECT_EQ(stats.coalesce_count, 1u);
    EXPECT_EQ(stats.retired_bytes, 128u);

    // a smaller request takes the front and keeps the rest retained
    EXPECT_EQ(arena_alloc(arena, 32), a);
    arena_get_stats(arena, &stats);
    EXPECT_EQ(stats.split_count, 1u);
    EXPECT_EQ(stats.retired_bytes, 96u);
    arena_destroy(arena);
}

TEST(ArenaRetireTest, RetiredTailRejoinsBumpCursor) {
    Arena* arena = arena_create_default();
    ASSERT_NE(arena, nullptr);
    ASSERT_NE(arena_alloc(arena, 64), nullptr);
    void* tail = arena_alloc(arena, 64);
    arena_retire(arena, tail, 64);

    ArenaStats stats;
    arena_get_stats(arena, &stats);
    EXPECT_EQ(stats.bump_back_count, 1u);
    EXPECT_EQ(stats.retired_bytes, 0u);
    EXPECT_EQ(arena_total_used(arena), 64u);
    arena_destroy(arena);
}

TEST(ArenaRetireTest, ResetDropsRetiredBlocks) {
    Arena* arena = arena_create_default();
    ASSERT_NE(arena, nullptr);
    void* a = arena_alloc(arena, 64);
    ASSERT_NE(arena_alloc(arena, 64), nullptr);
    arena_retire(arena, a, 64);
    arena_reset(arena);

    ArenaStats stats;
    arena_get_stats(arena, &stats);
    EXPECT_EQ(stats.retired_bytes, 0u);
    EXPECT_EQ(arena_alloc(arena, 64), a);  // fresh bump from the start
    arena_destroy(arena);
}

TEST(ArenaRetireTest, ForeignPointerIsIgnored) {
    Arena* arena = arena_create_default();
    ASSERT_NE(arena, nullptr);
    char outside[64];
    arena_retire(arena, outside, 64);
    arena_retire(arena, nullptr, 64);
    arena_retire(nullptr, outside, 64);
    ArenaStats stats;
    arena_get_stats(arena, &stats);
    EXPECT_EQ(stats.retire_count, 0u);
    arena_destroy(arena);
}

#ifndef NDEBUG
TEST(ArenaRetireTest, RetireOnScopeOwnedArenaAsserts) {
    EXPECT_DEATH({
        Arena* arena = arena_create_default();
        void* p = arena_alloc(arena, 64);
        arena_scope_enter(arena);
        arena_retire(arena, p, 64);  // the owner rewinds this tail
    }, "");
}

TEST(ArenaRetireTest, RewindWithRetiredBlocksAsserts) {
    EXPECT_DEATH({
        Arena* arena = arena_create_default();
        ArenaMark mark = arena_mark(arena);
        void* p = arena_alloc(arena, 64);
        arena_alloc(arena, 64);
        arena_retire(arena, p, 64);
        arena_rewind(arena, mark);
    }, "");
}

TEST(ArenaRewindTest, MarkAheadOfTailAsserts) {
    EXPECT_DEATH({
        Arena* arena = arena_create_default();
        ASSERT_NE(arena_alloc(arena, 64), nullptr);
        ArenaMark ahead = arena_mark(arena);
        arena_reset(arena);
        arena_rewind(arena, ahead);  // the mark lies beyond the current tail
    }, "");
}

TEST(ArenaScopeOwnerTest, DirectAllocationWhileScopeOwnedAsserts) {
    EXPECT_DEATH({
        Arena* arena = arena_create_default();
        arena_scope_enter(arena);
        arena_alloc(arena, 64);  // would land inside the owner's rewound tail
    }, "");
}

TEST(ArenaScopeOwnerTest, SecondScopeOwnerAsserts) {
    EXPECT_DEATH({
        Arena* arena = arena_create_default();
        arena_scope_enter(arena);
        arena_scope_enter(arena);
    }, "");
}
#endif

//==============================================================================
// Container Arena Allocation Tests (Phase 5a)
//==============================================================================

TEST(ArenaContainerTest, ArrayArenaAllocation) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default();
    
    // Create array from arena
    Array* arr = array_arena(arena);
    ASSERT_NE(arr, nullptr);
    
    // Verify array struct is arena-allocated
    EXPECT_TRUE(arena_owns(arena, arr));
    
    // Verify proper initialization (the bug we fixed!)
    EXPECT_EQ(arr->type_id, LMD_TYPE_ARRAY);
    EXPECT_EQ(arr->items, nullptr);  // Must be NULL, not garbage
    EXPECT_EQ(arr->length, 0);
    EXPECT_EQ(arr->capacity, 0);
    EXPECT_EQ(arr->extra, 0);
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaContainerTest, MapArenaAllocation) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default();
    
    // Create map from arena
    Map* map = map_arena(arena);
    ASSERT_NE(map, nullptr);
    
    // Verify map struct is arena-allocated
    EXPECT_TRUE(arena_owns(arena, map));
    
    // Verify proper initialization (the bug we fixed!)
    EXPECT_EQ(map->type_id, LMD_TYPE_MAP);
    EXPECT_EQ(map->data, nullptr);  // Must be NULL, not garbage
    EXPECT_NE(map->type, nullptr);  // Should point to EmptyMap
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaContainerTest, ElementArenaAllocation) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default();
    
    // Create element from arena
    Element* elmt = elmt_arena(arena);
    ASSERT_NE(elmt, nullptr);
    
    // Verify element struct is arena-allocated
    EXPECT_TRUE(arena_owns(arena, elmt));
    
    // Verify proper initialization (the bug we fixed!)
    EXPECT_EQ(elmt->type_id, LMD_TYPE_ELEMENT);
    EXPECT_EQ(elmt->items, nullptr);  // Must be NULL, not garbage
    EXPECT_EQ(elmt->length, 0);
    EXPECT_EQ(elmt->capacity, 0);
    EXPECT_EQ(elmt->extra, 0);
    EXPECT_NE(elmt->type, nullptr);  // Should point to EmptyElmt
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaContainerTest, ArrayArenaVsPoolAllocation) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default();
    
    // Create arrays from different allocators
    Array* arena_arr = array_arena(arena);
    Array* pool_arr = array_pooled(pool);
    
    ASSERT_NE(arena_arr, nullptr);
    ASSERT_NE(pool_arr, nullptr);
    
    // Arena array should be owned by arena
    EXPECT_TRUE(arena_owns(arena, arena_arr));
    
    // Pool array should NOT be owned by arena
    EXPECT_FALSE(arena_owns(arena, pool_arr));
    
    // Both should be properly initialized
    EXPECT_EQ(arena_arr->items, nullptr);
    EXPECT_EQ(pool_arr->items, nullptr);
    EXPECT_EQ(arena_arr->length, 0);
    EXPECT_EQ(pool_arr->length, 0);
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaContainerTest, MapArenaVsPoolAllocation) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default();
    
    // Create maps from different allocators
    Map* arena_map = map_arena(arena);
    Map* pool_map = map_pooled(pool);
    
    ASSERT_NE(arena_map, nullptr);
    ASSERT_NE(pool_map, nullptr);
    
    // Arena map should be owned by arena
    EXPECT_TRUE(arena_owns(arena, arena_map));
    
    // Pool map should NOT be owned by arena
    EXPECT_FALSE(arena_owns(arena, pool_map));
    
    // Both should be properly initialized
    EXPECT_EQ(arena_map->data, nullptr);
    EXPECT_EQ(pool_map->data, nullptr);
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaContainerTest, ElementArenaVsPoolAllocation) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default();
    
    // Create elements from different allocators
    Element* arena_elmt = elmt_arena(arena);
    Element* pool_elmt = elmt_pooled(pool);
    
    ASSERT_NE(arena_elmt, nullptr);
    ASSERT_NE(pool_elmt, nullptr);
    
    // Arena element should be owned by arena
    EXPECT_TRUE(arena_owns(arena, arena_elmt));
    
    // Pool element should NOT be owned by arena
    EXPECT_FALSE(arena_owns(arena, pool_elmt));
    
    // Both should be properly initialized
    EXPECT_EQ(arena_elmt->items, nullptr);
    EXPECT_EQ(pool_elmt->items, nullptr);
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaContainerTest, MultipleContainersInSameArena) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default();
    
    // Create multiple containers from same arena
    Array* arr1 = array_arena(arena);
    Array* arr2 = array_arena(arena);
    Map* map1 = map_arena(arena);
    Map* map2 = map_arena(arena);
    Element* elmt1 = elmt_arena(arena);
    Element* elmt2 = elmt_arena(arena);
    
    // All should be valid
    ASSERT_NE(arr1, nullptr);
    ASSERT_NE(arr2, nullptr);
    ASSERT_NE(map1, nullptr);
    ASSERT_NE(map2, nullptr);
    ASSERT_NE(elmt1, nullptr);
    ASSERT_NE(elmt2, nullptr);
    
    // All should be owned by the same arena
    EXPECT_TRUE(arena_owns(arena, arr1));
    EXPECT_TRUE(arena_owns(arena, arr2));
    EXPECT_TRUE(arena_owns(arena, map1));
    EXPECT_TRUE(arena_owns(arena, map2));
    EXPECT_TRUE(arena_owns(arena, elmt1));
    EXPECT_TRUE(arena_owns(arena, elmt2));
    
    // All should be properly initialized
    EXPECT_EQ(arr1->items, nullptr);
    EXPECT_EQ(arr2->items, nullptr);
    EXPECT_EQ(map1->data, nullptr);
    EXPECT_EQ(map2->data, nullptr);
    EXPECT_EQ(elmt1->items, nullptr);
    EXPECT_EQ(elmt2->items, nullptr);
    
    arena_destroy(arena);
    pool_destroy(pool);
}

TEST(ArenaContainerTest, ContainerAllocationAcrossArenas) {
    Pool* pool = pool_create();
    Arena* arena1 = arena_create_default();
    Arena* arena2 = arena_create_default();
    
    // Create containers in different arenas
    Array* arr1 = array_arena(arena1);
    Array* arr2 = array_arena(arena2);
    
    ASSERT_NE(arr1, nullptr);
    ASSERT_NE(arr2, nullptr);
    
    // Each array should only be owned by its own arena
    EXPECT_TRUE(arena_owns(arena1, arr1));
    EXPECT_FALSE(arena_owns(arena1, arr2));
    EXPECT_FALSE(arena_owns(arena2, arr1));
    EXPECT_TRUE(arena_owns(arena2, arr2));
    
    arena_destroy(arena1);
    arena_destroy(arena2);
    pool_destroy(pool);
}

TEST(ArenaContainerTest, NullArenaHandling) {
    // Test that passing NULL arena doesn't crash
    Array* arr = array_arena(nullptr);
    Map* map = map_arena(nullptr);
    Element* elmt = elmt_arena(nullptr);
    
    // All should return NULL gracefully
    EXPECT_EQ(arr, nullptr);
    EXPECT_EQ(map, nullptr);
    EXPECT_EQ(elmt, nullptr);
}

// Test the regression bug: uninitialized memory causing crashes
TEST(ArenaContainerRegressionTest, UninitializedMemoryBug) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default();
    
    // Create element from arena
    Element* elmt = elmt_arena(arena);
    ASSERT_NE(elmt, nullptr);
    
    // This is the critical test - items MUST be NULL, not garbage
    // If items is garbage, realloc(garbage) will crash
    EXPECT_EQ(elmt->items, nullptr);
    EXPECT_EQ(elmt->capacity, 0);
    EXPECT_EQ(elmt->length, 0);
    
    // Simulate what list_push() does - should not crash
    // This would crash before the fix if items was garbage
    if (elmt->length + 1 > elmt->capacity) {
        elmt->capacity = 8;
        // This realloc MUST work with NULL items
        elmt->items = (Item*)realloc(elmt->items, elmt->capacity * sizeof(Item));
        EXPECT_NE(elmt->items, nullptr);
    }
    
    // Clean up the heap-allocated buffer
    free(elmt->items);
    
    arena_destroy(arena);
    pool_destroy(pool);
}

// Test the regression bug: uninitialized memory causing crashes
TEST(ArenaContainerRegressionTest, MapDataInitialization) {
    Pool* pool = pool_create();
    Arena* arena = arena_create_default();
    
    // Create map from arena
    Map* map = map_arena(arena);
    ASSERT_NE(map, nullptr);
    
    // Critical: data field MUST be NULL, not garbage
    EXPECT_EQ(map->data, nullptr);
    
    // Map should be properly initialized and usable
    // (Internal fields like capacity/length are managed internally)
    EXPECT_TRUE(arena_owns(arena, map));
    
    arena_destroy(arena);
    pool_destroy(pool);
}
