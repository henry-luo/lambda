#include <gtest/gtest.h>
#include "../lib/memtrack.h"
#include <cstring>
#include <cstdio>

// Test fixture for memory tracker tests
class MemtrackTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize memory tracker in debug mode for each test
        memtrack_init(MEMTRACK_MODE_DEBUG);
    }

    void TearDown() override {
        // Shutdown memory tracker after each test
        memtrack_shutdown();
    }
};

// ============================================================================
// Test 1: Memory Leak Detection
// ============================================================================

TEST_F(MemtrackTest, DetectsMemoryLeak) {
    // Allocate memory but don't free it - should be detected as leak
    void* leaked = mem_alloc(100, MEM_CAT_TEMP);
    ASSERT_NE(leaked, nullptr);

    // Get stats before shutdown
    MemtrackStats stats;
    memtrack_get_stats(&stats);

    EXPECT_EQ(stats.current_count, 1);
    EXPECT_EQ(stats.current_bytes, 100);

    // Note: Leak will be reported during TearDown (memtrack_shutdown)
    // We can't test the leak detection directly here, but we verify allocation tracking
}

TEST_F(MemtrackTest, NoLeakWhenFreed) {
    void* ptr = mem_alloc(200, MEM_CAT_TEMP);
    ASSERT_NE(ptr, nullptr);

    mem_free(ptr);

    MemtrackStats stats;
    memtrack_get_stats(&stats);

    EXPECT_EQ(stats.current_count, 0);
    EXPECT_EQ(stats.current_bytes, 0);
    EXPECT_EQ(stats.total_allocs, 1);
    EXPECT_EQ(stats.total_frees, 1);
}

// ============================================================================
// Test 2: Double Free Detection
// ============================================================================

TEST_F(MemtrackTest, DetectsDoubleFree) {
    void* ptr = mem_alloc(50, MEM_CAT_TEMP);
    ASSERT_NE(ptr, nullptr);

    // First free - should succeed
    mem_free(ptr);

    MemtrackStats stats;
    memtrack_get_stats(&stats);
    EXPECT_EQ(stats.current_count, 0);

    // Second free - should be detected as invalid free
    // This will log an error but won't crash
    mem_free(ptr);

    memtrack_get_stats(&stats);
    EXPECT_EQ(stats.invalid_frees, 1);
}

// ============================================================================
// Test 3: Invalid Free Detection (freeing untracked pointer)
// ============================================================================

TEST_F(MemtrackTest, DetectsInvalidFree) {
    int stack_var = 42;
    void* stack_ptr = &stack_var;

    // Try to free a pointer that wasn't allocated by mem_alloc
    mem_free(stack_ptr);

    MemtrackStats stats;
    memtrack_get_stats(&stats);
    EXPECT_EQ(stats.invalid_frees, 1);
}

// ============================================================================
// Test 4: Buffer Overflow Detection (via guard bytes)
// ============================================================================

TEST_F(MemtrackTest, DetectsBufferOverflowTail) {
    char* buffer = (char*)mem_alloc(10, MEM_CAT_TEMP);
    ASSERT_NE(buffer, nullptr);

    // Write valid data within bounds
    memcpy(buffer, "123456789", 9);
    buffer[9] = '\0';

    // Overflow: write past the allocated size
    // This should corrupt the tail guard bytes
    buffer[10] = 'X';  // Write 1 byte past allocated memory
    buffer[11] = 'Y';  // Write 2 bytes past

    // Free should detect the guard byte corruption
    mem_free(buffer);

    MemtrackStats stats;
    memtrack_get_stats(&stats);
    EXPECT_GE(stats.guard_violations, 1);
}

TEST_F(MemtrackTest, DetectsBufferOverflowHead) {
    char* buffer = (char*)mem_alloc(20, MEM_CAT_TEMP);
    ASSERT_NE(buffer, nullptr);

    // Corrupt the head guard bytes by writing before the buffer
    // This is dangerous in real code, but we're testing detection
    char* before_buffer = buffer - 4;
    *before_buffer = 0xFF;  // Corrupt head guard

    mem_free(buffer);

    MemtrackStats stats;
    memtrack_get_stats(&stats);
    EXPECT_GE(stats.guard_violations, 1);
}

// ============================================================================
// Test 5: Category Tracking
// ============================================================================

TEST_F(MemtrackTest, TracksCategoriesSeparately) {
    void* ptr1 = mem_alloc(100, MEM_CAT_INPUT_JSON);
    void* ptr2 = mem_alloc(200, MEM_CAT_INPUT_YAML);
    void* ptr3 = mem_alloc(150, MEM_CAT_INPUT_JSON);

    ASSERT_NE(ptr1, nullptr);
    ASSERT_NE(ptr2, nullptr);
    ASSERT_NE(ptr3, nullptr);

    MemtrackCategoryStats json_stats, yaml_stats;
    memtrack_get_category_stats(MEM_CAT_INPUT_JSON, &json_stats);
    memtrack_get_category_stats(MEM_CAT_INPUT_YAML, &yaml_stats);

    EXPECT_EQ(json_stats.current_count, 2);
    EXPECT_EQ(json_stats.current_bytes, 250);
    EXPECT_EQ(yaml_stats.current_count, 1);
    EXPECT_EQ(yaml_stats.current_bytes, 200);

    mem_free(ptr1);
    mem_free(ptr2);
    mem_free(ptr3);

    memtrack_get_category_stats(MEM_CAT_INPUT_JSON, &json_stats);
    memtrack_get_category_stats(MEM_CAT_INPUT_YAML, &yaml_stats);

    EXPECT_EQ(json_stats.current_count, 0);
    EXPECT_EQ(yaml_stats.current_count, 0);
}

// ============================================================================
// Test 6: mem_calloc (zeroed memory)
// ============================================================================

TEST_F(MemtrackTest, CallocZeroesMemory) {
    size_t count = 10;
    size_t size = sizeof(int);
    int* buffer = (int*)mem_calloc(count, size, MEM_CAT_TEMP);

    ASSERT_NE(buffer, nullptr);

    // Verify all bytes are zero
    for (size_t i = 0; i < count; i++) {
        EXPECT_EQ(buffer[i], 0);
    }

    mem_free(buffer);
}

// ============================================================================
// Test 7: mem_realloc
// ============================================================================

TEST_F(MemtrackTest, ReallocPreservesData) {
    char* buffer = (char*)mem_alloc(10, MEM_CAT_TEMP);
    ASSERT_NE(buffer, nullptr);

    strcpy(buffer, "Hello");

    // Grow buffer
    char* new_buffer = (char*)mem_realloc(buffer, 20, MEM_CAT_TEMP);
    ASSERT_NE(new_buffer, nullptr);

    // Data should be preserved
    EXPECT_STREQ(new_buffer, "Hello");

    mem_free(new_buffer);

    MemtrackStats stats;
    memtrack_get_stats(&stats);
    EXPECT_EQ(stats.current_count, 0);
}

TEST_F(MemtrackTest, ReallocShrink) {
    char* buffer = (char*)mem_alloc(100, MEM_CAT_TEMP);
    ASSERT_NE(buffer, nullptr);

    strcpy(buffer, "Test");

    // Shrink buffer
    char* new_buffer = (char*)mem_realloc(buffer, 10, MEM_CAT_TEMP);
    ASSERT_NE(new_buffer, nullptr);

    EXPECT_STREQ(new_buffer, "Test");

    mem_free(new_buffer);
}

// ============================================================================
// Test 8: mem_strdup
// ============================================================================

TEST_F(MemtrackTest, StrdupDuplicatesString) {
    const char* original = "Test String";
    char* duplicate = mem_strdup(original, MEM_CAT_TEMP);

    ASSERT_NE(duplicate, nullptr);
    EXPECT_STREQ(duplicate, original);
    EXPECT_NE(duplicate, original);  // Different pointer

    mem_free(duplicate);
}

TEST_F(MemtrackTest, StrdupNullReturnsNull) {
    char* duplicate = mem_strdup(nullptr, MEM_CAT_TEMP);
    EXPECT_EQ(duplicate, nullptr);
}

// ============================================================================
// Test 9: Peak Usage Tracking
// ============================================================================

TEST_F(MemtrackTest, TracksPeakUsage) {
    void* ptr1 = mem_alloc(100, MEM_CAT_TEMP);
    void* ptr2 = mem_alloc(200, MEM_CAT_TEMP);

    MemtrackStats stats;
    memtrack_get_stats(&stats);

    // Peak should be at least 300 bytes
    EXPECT_GE(stats.peak_bytes, 300);

    mem_free(ptr1);

    memtrack_get_stats(&stats);
    // Peak should still be 300+ even after freeing
    EXPECT_GE(stats.peak_bytes, 300);
    // Current should be ~200
    EXPECT_EQ(stats.current_bytes, 200);

    mem_free(ptr2);
}

// ============================================================================
// Test 10: Allocation Info Query
// ============================================================================

TEST_F(MemtrackTest, QueryAllocationInfo) {
    void* ptr = mem_alloc(42, MEM_CAT_INPUT_YAML);
    ASSERT_NE(ptr, nullptr);

    size_t size;
    MemCategory category;
    bool found = memtrack_get_alloc_info(ptr, &size, &category);

    EXPECT_TRUE(found);
    EXPECT_EQ(size, 42);
    EXPECT_EQ(category, MEM_CAT_INPUT_YAML);

    mem_free(ptr);

    // After free, should not be found
    found = memtrack_get_alloc_info(ptr, &size, &category);
    EXPECT_FALSE(found);
}

TEST_F(MemtrackTest, IsAllocatedCheck) {
    void* ptr = mem_alloc(10, MEM_CAT_TEMP);
    ASSERT_NE(ptr, nullptr);

    EXPECT_TRUE(memtrack_is_allocated(ptr));

    mem_free(ptr);

    EXPECT_FALSE(memtrack_is_allocated(ptr));
}

// ============================================================================
// Test 11: Multiple Allocations Stress Test
// ============================================================================

TEST_F(MemtrackTest, ManyAllocationsNoLeaks) {
    const int num_allocs = 1000;
    void* ptrs[num_allocs];

    // Allocate many blocks
    for (int i = 0; i < num_allocs; i++) {
        ptrs[i] = mem_alloc(10 + i, MEM_CAT_TEMP);
        ASSERT_NE(ptrs[i], nullptr);
    }

    MemtrackStats stats;
    memtrack_get_stats(&stats);
    EXPECT_EQ(stats.current_count, num_allocs);

    // Free all blocks
    for (int i = 0; i < num_allocs; i++) {
        mem_free(ptrs[i]);
    }

    memtrack_get_stats(&stats);
    EXPECT_EQ(stats.current_count, 0);
    EXPECT_EQ(stats.current_bytes, 0);
    EXPECT_EQ(stats.total_allocs, num_allocs);
    EXPECT_EQ(stats.total_frees, num_allocs);
}

// ============================================================================
// Test 12: Guard Verification
// ============================================================================

TEST_F(MemtrackTest, VerifyGuardsDetectsCorruption) {
    char* buffer1 = (char*)mem_alloc(50, MEM_CAT_TEMP);
    char* buffer2 = (char*)mem_alloc(50, MEM_CAT_TEMP);

    ASSERT_NE(buffer1, nullptr);
    ASSERT_NE(buffer2, nullptr);

    // Corrupt buffer2's guard bytes
    buffer2[-1] = 0xFF;  // Corrupt head guard

    // Verify guards across all allocations
    size_t violations = memtrack_verify_guards();
    EXPECT_GE(violations, 1);

    // Clean free of buffer1 (no corruption)
    mem_free(buffer1);

    // Free buffer2 will also detect corruption
    mem_free(buffer2);

    MemtrackStats stats;
    memtrack_get_stats(&stats);
    EXPECT_GE(stats.guard_violations, 1);
}

// ============================================================================
// Main function
// ============================================================================

// Test fixture for STATS mode tests
class MemtrackStatsTest : public ::testing::Test {
protected:
    void SetUp() override {
        memtrack_init(MEMTRACK_MODE_STATS);
    }
    void TearDown() override {
        memtrack_shutdown();
    }
};

// ============================================================================
// STATS mode: size tracking on free
// ============================================================================

TEST_F(MemtrackStatsTest, StatsModeTracksBytesOnFree) {
    void* p1 = mem_alloc(100, MEM_CAT_TEMP);
    void* p2 = mem_alloc(200, MEM_CAT_PARSER);
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);

    MemtrackStats stats;
    memtrack_get_stats(&stats);
    EXPECT_EQ(stats.current_bytes, 300);
    EXPECT_EQ(stats.current_count, 2);

    mem_free(p1);
    memtrack_get_stats(&stats);
    EXPECT_EQ(stats.current_bytes, 200);
    EXPECT_EQ(stats.current_count, 1);

    mem_free(p2);
    memtrack_get_stats(&stats);
    EXPECT_EQ(stats.current_bytes, 0);
    EXPECT_EQ(stats.current_count, 0);
}

TEST_F(MemtrackStatsTest, StatsModeTracksCategoryBytesOnFree) {
    void* p1 = mem_alloc(64, MEM_CAT_LAYOUT);
    void* p2 = mem_alloc(128, MEM_CAT_LAYOUT);
    void* p3 = mem_alloc(256, MEM_CAT_RENDER);

    mem_free(p1);

    MemtrackCategoryStats layout_stats, render_stats;
    memtrack_get_category_stats(MEM_CAT_LAYOUT, &layout_stats);
    memtrack_get_category_stats(MEM_CAT_RENDER, &render_stats);

    EXPECT_EQ(layout_stats.current_bytes, 128);
    EXPECT_EQ(layout_stats.current_count, 1);
    EXPECT_EQ(render_stats.current_bytes, 256);
    EXPECT_EQ(render_stats.current_count, 1);

    mem_free(p2);
    mem_free(p3);
}

TEST_F(MemtrackStatsTest, StatsModeReallocUpdatesBytesCorrectly) {
    char* buf = (char*)mem_alloc(50, MEM_CAT_TEMP);
    ASSERT_NE(buf, nullptr);
    memcpy(buf, "hello", 5);

    MemtrackStats stats;
    memtrack_get_stats(&stats);
    EXPECT_EQ(stats.current_bytes, 50);

    // grow
    buf = (char*)mem_realloc(buf, 200, MEM_CAT_TEMP);
    ASSERT_NE(buf, nullptr);
    EXPECT_EQ(memcmp(buf, "hello", 5), 0);

    memtrack_get_stats(&stats);
    EXPECT_EQ(stats.current_bytes, 200);
    EXPECT_EQ(stats.current_count, 1);

    mem_free(buf);
    memtrack_get_stats(&stats);
    EXPECT_EQ(stats.current_bytes, 0);
    EXPECT_EQ(stats.current_count, 0);
}

TEST_F(MemtrackStatsTest, StatsModeShutdownReturnsLeakCount) {
    mem_alloc(100, MEM_CAT_TEMP);
    mem_alloc(200, MEM_CAT_PARSER);
    // intentionally not freed

    // Shutdown and check return value
    size_t leaks = memtrack_shutdown();
    EXPECT_EQ(leaks, 2);

    // Re-init for TearDown
    memtrack_init(MEMTRACK_MODE_STATS);
}

// ============================================================================
// Pool/Arena lifecycle counters
// ============================================================================

TEST_F(MemtrackStatsTest, PoolLifecycleTracking) {
    EXPECT_EQ(memtrack_get_pool_count(), 0);

    Pool* pool = memtrack_pool_create(MEM_CAT_TEMP);
    ASSERT_NE(pool, nullptr);
    EXPECT_EQ(memtrack_get_pool_count(), 1);

    Pool* pool2 = memtrack_pool_create(MEM_CAT_LAYOUT);
    ASSERT_NE(pool2, nullptr);
    EXPECT_EQ(memtrack_get_pool_count(), 2);

    memtrack_pool_destroy(pool);
    EXPECT_EQ(memtrack_get_pool_count(), 1);

    memtrack_pool_destroy(pool2);
    EXPECT_EQ(memtrack_get_pool_count(), 0);
}

TEST_F(MemtrackStatsTest, ArenaLifecycleTracking) {
    Pool* pool = memtrack_pool_create(MEM_CAT_TEMP);
    ASSERT_NE(pool, nullptr);

    EXPECT_EQ(memtrack_get_arena_count(), 0);

    Arena* arena = memtrack_arena_create(pool, MEM_CAT_LAYOUT);
    ASSERT_NE(arena, nullptr);
    EXPECT_EQ(memtrack_get_arena_count(), 1);

    // Allocations via tracked arena work
    void* ptr = memtrack_arena_alloc(arena, 128);
    EXPECT_NE(ptr, nullptr);

    memtrack_arena_destroy(arena);
    EXPECT_EQ(memtrack_get_arena_count(), 0);

    memtrack_pool_destroy(pool);
    EXPECT_EQ(memtrack_get_pool_count(), 0);
}

// ============================================================================
// New categories exist
// ============================================================================

TEST_F(MemtrackStatsTest, NewCategoriesWork) {
    void* p1 = mem_alloc(32, MEM_CAT_JS_RUNTIME);
    void* p2 = mem_alloc(64, MEM_CAT_PY_RUNTIME);
    void* p3 = mem_alloc(16, MEM_CAT_NETWORK);
    void* p4 = mem_alloc(48, MEM_CAT_SERVE);
    void* p5 = mem_alloc(24, MEM_CAT_SYSTEM);

    MemtrackCategoryStats js_stats;
    memtrack_get_category_stats(MEM_CAT_JS_RUNTIME, &js_stats);
    EXPECT_EQ(js_stats.current_bytes, 32);

    mem_free(p1);
    mem_free(p2);
    mem_free(p3);
    mem_free(p4);
    mem_free(p5);

    MemtrackStats stats;
    memtrack_get_stats(&stats);
    EXPECT_EQ(stats.current_count, 0);
    EXPECT_EQ(stats.current_bytes, 0);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
