#include <gtest/gtest.h>
#include "../lib/scratch_arena.h"
#include "../lib/arena.h"
#include "../lib/mempool.h"
#include <string.h>

// Test fixture with pool + arena setup
class ScratchArenaTest : public ::testing::Test {
protected:
    Pool* pool;
    Arena* arena;

    void SetUp() override {
        pool = pool_create_mmap();
        ASSERT_NE(pool, nullptr);
        arena = arena_create(pool, 16 * 1024, 64 * 1024); // 16KB initial
        ASSERT_NE(arena, nullptr);
    }

    void TearDown() override {
        arena_destroy(arena);
        pool_destroy(pool);
    }
};

// ============================================================================
// Basic lifecycle
// ============================================================================

TEST_F(ScratchArenaTest, InitAndRelease) {
    ScratchArena sa;
    scratch_init(&sa, arena);
    EXPECT_EQ(scratch_live_count(&sa), 0);
    scratch_release(&sa);
    EXPECT_EQ(scratch_live_count(&sa), 0);
}

TEST_F(ScratchArenaTest, SingleAllocFree) {
    ScratchArena sa;
    scratch_init(&sa, arena);

    void* p = scratch_alloc(&sa, 64);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(scratch_live_count(&sa), 1);

    // verify 16-byte alignment
    EXPECT_EQ((uintptr_t)p % 16, 0);

    scratch_free(&sa, p);
    EXPECT_EQ(scratch_live_count(&sa), 0);

    scratch_release(&sa);
}

TEST_F(ScratchArenaTest, CallocZeros) {
    ScratchArena sa;
    scratch_init(&sa, arena);

    uint8_t* p = (uint8_t*)scratch_calloc(&sa, 256);
    ASSERT_NE(p, nullptr);

    for (int i = 0; i < 256; i++) {
        EXPECT_EQ(p[i], 0) << "byte " << i << " not zero";
    }

    scratch_free(&sa, p);
    scratch_release(&sa);
}

// ============================================================================
// LIFO free (the common path)
// ============================================================================

TEST_F(ScratchArenaTest, LIFOFree_TwoAllocations) {
    ScratchArena sa;
    scratch_init(&sa, arena);

    void* a = scratch_alloc(&sa, 100);
    void* b = scratch_alloc(&sa, 200);
    EXPECT_EQ(scratch_live_count(&sa), 2);

    // free in LIFO order
    scratch_free(&sa, b);
    EXPECT_EQ(scratch_live_count(&sa), 1);

    scratch_free(&sa, a);
    EXPECT_EQ(scratch_live_count(&sa), 0);

    scratch_release(&sa);
}

TEST_F(ScratchArenaTest, LIFOFree_ManyAllocations) {
    ScratchArena sa;
    scratch_init(&sa, arena);

    const int N = 100;
    void* ptrs[100];
    for (int i = 0; i < N; i++) {
        ptrs[i] = scratch_alloc(&sa, 32 + i * 4);
        ASSERT_NE(ptrs[i], nullptr);
    }
    EXPECT_EQ(scratch_live_count(&sa), (size_t)N);

    // free all in reverse (LIFO)
    for (int i = N - 1; i >= 0; i--) {
        scratch_free(&sa, ptrs[i]);
    }
    EXPECT_EQ(scratch_live_count(&sa), 0);

    scratch_release(&sa);
}

TEST_F(ScratchArenaTest, LIFOFree_BumpBackReclaimsMemory) {
    ScratchArena sa;
    scratch_init(&sa, arena);

    size_t used_before = arena_total_used(arena);

    void* a = scratch_alloc(&sa, 512);
    void* b = scratch_alloc(&sa, 512);
    size_t used_with_both = arena_total_used(arena);
    EXPECT_GT(used_with_both, used_before);

    // LIFO free both — arena bump pointer should rewind
    scratch_free(&sa, b);
    scratch_free(&sa, a);

    size_t used_after = arena_total_used(arena);
    // should be back to (approximately) where we started
    // (exact match depends on arena alignment, but should be close)
    EXPECT_LE(used_after, used_before + 32); // allow small alignment rounding

    scratch_release(&sa);
}

// ============================================================================
// Non-LIFO free (hole creation + backward coalescing)
// ============================================================================

TEST_F(ScratchArenaTest, NonLIFO_HoleCoalescing) {
    // The critical scenario from §9.3:
    // [A] ← [B] ← [C] ← [D]    head = D
    // free(B) → hole
    // free(C) → hole
    // free(D) → LIFO, backward walk reclaims C, B holes
    // Result: [A] head = A

    ScratchArena sa;
    scratch_init(&sa, arena);

    void* a = scratch_alloc(&sa, 64);
    void* b = scratch_alloc(&sa, 64);
    void* c = scratch_alloc(&sa, 64);
    void* d = scratch_alloc(&sa, 64);
    EXPECT_EQ(scratch_live_count(&sa), 4);

    // non-LIFO: free B and C first (creates holes)
    scratch_free(&sa, b);
    EXPECT_EQ(scratch_live_count(&sa), 3); // A, C, D live; B is hole

    scratch_free(&sa, c);
    EXPECT_EQ(scratch_live_count(&sa), 2); // A, D live; B, C are holes

    // LIFO: free D → should trigger backward walk and reclaim B, C holes
    scratch_free(&sa, d);
    EXPECT_EQ(scratch_live_count(&sa), 1); // only A remains

    // verify A is still usable
    memset(a, 0xAA, 64);
    EXPECT_EQ(((uint8_t*)a)[0], 0xAA);

    scratch_free(&sa, a);
    EXPECT_EQ(scratch_live_count(&sa), 0);

    scratch_release(&sa);
}

TEST_F(ScratchArenaTest, NonLIFO_SingleHoleBehindTail) {
    // [A] ← [B] ← [C]   head = C
    // free(B) → hole
    // free(C) → LIFO, backward walk reclaims B
    // Result: [A]

    ScratchArena sa;
    scratch_init(&sa, arena);

    void* a = scratch_alloc(&sa, 128);
    void* b = scratch_alloc(&sa, 128);
    void* c = scratch_alloc(&sa, 128);

    scratch_free(&sa, b); // hole
    scratch_free(&sa, c); // LIFO + coalesce B
    EXPECT_EQ(scratch_live_count(&sa), 1);

    scratch_free(&sa, a);
    EXPECT_EQ(scratch_live_count(&sa), 0);

    scratch_release(&sa);
}

TEST_F(ScratchArenaTest, NonLIFO_HoleNotAtTail) {
    // [A] ← [B] ← [C]   head = C
    // free(A) → hole (not behind tail, just marked)
    // live_count should still be 2

    ScratchArena sa;
    scratch_init(&sa, arena);

    void* a = scratch_alloc(&sa, 64);
    void* b = scratch_alloc(&sa, 64);
    void* c = scratch_alloc(&sa, 64);

    scratch_free(&sa, a); // non-LIFO, not behind tail → just mark
    EXPECT_EQ(scratch_live_count(&sa), 2); // B, C live

    // free C (LIFO) → coalesces B? No — B is not a hole. Stops.
    scratch_free(&sa, c);
    EXPECT_EQ(scratch_live_count(&sa), 1); // B live, A is hole deep in stack

    scratch_free(&sa, b);
    // B is now LIFO → reclaim B, then backward walk hits A which is hole → reclaim A
    EXPECT_EQ(scratch_live_count(&sa), 0);

    scratch_release(&sa);
}

// ============================================================================
// Mark / Restore
// ============================================================================

TEST_F(ScratchArenaTest, MarkRestore_Basic) {
    ScratchArena sa;
    scratch_init(&sa, arena);

    void* a = scratch_alloc(&sa, 100);
    ScratchMark mark = scratch_mark(&sa);

    void* b = scratch_alloc(&sa, 200);
    void* c = scratch_alloc(&sa, 300);
    EXPECT_EQ(scratch_live_count(&sa), 3);

    // restore should free b and c
    scratch_restore(&sa, mark);
    EXPECT_EQ(scratch_live_count(&sa), 1); // only a remains

    scratch_free(&sa, a);
    EXPECT_EQ(scratch_live_count(&sa), 0);

    scratch_release(&sa);
}

TEST_F(ScratchArenaTest, MarkRestore_Nested) {
    ScratchArena sa;
    scratch_init(&sa, arena);

    void* a = scratch_alloc(&sa, 64);
    ScratchMark mark1 = scratch_mark(&sa);

    void* b = scratch_alloc(&sa, 64);
    ScratchMark mark2 = scratch_mark(&sa);

    void* c = scratch_alloc(&sa, 64);
    void* d = scratch_alloc(&sa, 64);

    // restore inner scope
    scratch_restore(&sa, mark2);
    EXPECT_EQ(scratch_live_count(&sa), 2); // a, b

    // restore outer scope
    scratch_restore(&sa, mark1);
    EXPECT_EQ(scratch_live_count(&sa), 1); // a

    scratch_free(&sa, a);
    EXPECT_EQ(scratch_live_count(&sa), 0);

    scratch_release(&sa);
}

TEST_F(ScratchArenaTest, MarkRestore_EmptyScope) {
    ScratchArena sa;
    scratch_init(&sa, arena);

    void* a = scratch_alloc(&sa, 64);
    ScratchMark mark = scratch_mark(&sa);

    // no allocations between mark and restore
    scratch_restore(&sa, mark);
    EXPECT_EQ(scratch_live_count(&sa), 1);

    scratch_free(&sa, a);
    scratch_release(&sa);
}

// ============================================================================
// Data integrity
// ============================================================================

TEST_F(ScratchArenaTest, DataIntegrity_WriteAndRead) {
    ScratchArena sa;
    scratch_init(&sa, arena);

    // simulate a table layout: multiple arrays of different sizes
    int* col_widths = (int*)scratch_calloc(&sa, sizeof(int) * 20);
    float* row_heights = (float*)scratch_calloc(&sa, sizeof(float) * 50);
    uint8_t* grid = (uint8_t*)scratch_calloc(&sa, 20 * 50);

    ASSERT_NE(col_widths, nullptr);
    ASSERT_NE(row_heights, nullptr);
    ASSERT_NE(grid, nullptr);

    // write data
    for (int i = 0; i < 20; i++) col_widths[i] = 100 + i;
    for (int i = 0; i < 50; i++) row_heights[i] = 20.0f + i * 0.5f;
    for (int i = 0; i < 20 * 50; i++) grid[i] = (uint8_t)(i & 0xFF);

    // verify data
    for (int i = 0; i < 20; i++) EXPECT_EQ(col_widths[i], 100 + i);
    for (int i = 0; i < 50; i++) EXPECT_FLOAT_EQ(row_heights[i], 20.0f + i * 0.5f);
    for (int i = 0; i < 1000; i++) EXPECT_EQ(grid[i], (uint8_t)(i & 0xFF));

    // free in LIFO order
    scratch_free(&sa, grid);
    scratch_free(&sa, row_heights);
    scratch_free(&sa, col_widths);

    scratch_release(&sa);
}

TEST_F(ScratchArenaTest, DataIntegrity_LargeAllocation) {
    ScratchArena sa;
    scratch_init(&sa, arena);

    // simulate a pixel buffer (512x512 RGBA)
    size_t pixel_size = 512 * 512 * 4;
    uint32_t* pixels = (uint32_t*)scratch_alloc(&sa, pixel_size);
    ASSERT_NE(pixels, nullptr);

    // write pattern
    for (int i = 0; i < 512 * 512; i++) {
        pixels[i] = 0xFF000000 | (i & 0xFFFFFF);
    }

    // verify
    for (int i = 0; i < 512 * 512; i++) {
        EXPECT_EQ(pixels[i], (uint32_t)(0xFF000000 | (i & 0xFFFFFF)));
    }

    scratch_free(&sa, pixels);
    scratch_release(&sa);
}

// ============================================================================
// Alignment
// ============================================================================

TEST_F(ScratchArenaTest, Alignment16Byte) {
    ScratchArena sa;
    scratch_init(&sa, arena);

    // allocate various sizes and verify 16-byte alignment
    for (int i = 1; i <= 256; i++) {
        void* p = scratch_alloc(&sa, i);
        ASSERT_NE(p, nullptr) << "alloc failed at size " << i;
        EXPECT_EQ((uintptr_t)p % 16, 0) << "misaligned at size " << i;
    }

    scratch_release(&sa);
}

// ============================================================================
// Reuse after free
// ============================================================================

TEST_F(ScratchArenaTest, ReuseAfterLIFOFree) {
    ScratchArena sa;
    scratch_init(&sa, arena);

    size_t used_start = arena_total_used(arena);

    // allocate and free several times — memory should be reclaimed each time
    for (int round = 0; round < 10; round++) {
        void* a = scratch_alloc(&sa, 1024);
        void* b = scratch_alloc(&sa, 1024);
        ASSERT_NE(a, nullptr);
        ASSERT_NE(b, nullptr);
        scratch_free(&sa, b);
        scratch_free(&sa, a);
    }

    size_t used_end = arena_total_used(arena);
    // after 10 rounds of alloc/free, arena usage should not have grown much
    // (bump-back should rewind each time)
    EXPECT_LE(used_end, used_start + 256); // small tolerance for alignment

    scratch_release(&sa);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_F(ScratchArenaTest, NullSafety) {
    // all APIs should handle NULL gracefully
    scratch_init(nullptr, arena);
    EXPECT_EQ(scratch_alloc(nullptr, 64), nullptr);
    EXPECT_EQ(scratch_calloc(nullptr, 64), nullptr);
    scratch_free(nullptr, (void*)0x1234);
    scratch_release(nullptr);
    EXPECT_EQ(scratch_live_count(nullptr), 0);

    ScratchArena sa;
    scratch_init(&sa, arena);
    scratch_free(&sa, nullptr); // should not crash
    EXPECT_EQ(scratch_alloc(&sa, 0), nullptr); // zero-size
    scratch_release(&sa);
}

TEST_F(ScratchArenaTest, ReleaseWithHoles) {
    // release should clean up even if there are unreleased holes
    ScratchArena sa;
    scratch_init(&sa, arena);

    void* a = scratch_alloc(&sa, 64);
    void* b = scratch_alloc(&sa, 64);
    void* c = scratch_alloc(&sa, 64);

    scratch_free(&sa, b); // create hole
    // a and c still live, b is hole

    scratch_release(&sa); // should clean up everything
    EXPECT_EQ(scratch_live_count(&sa), 0);
}

TEST_F(ScratchArenaTest, SingleAllocation_Release) {
    ScratchArena sa;
    scratch_init(&sa, arena);

    void* a = scratch_alloc(&sa, 32);
    EXPECT_EQ(scratch_live_count(&sa), 1);

    scratch_release(&sa);
    EXPECT_EQ(scratch_live_count(&sa), 0);
}

// ============================================================================
// Interleaved alloc/free (render_block_view pattern)
// ============================================================================

TEST_F(ScratchArenaTest, InterleavedAllocFree) {
    // simulates render_block_view: alloc clip, render children, free clip,
    // alloc blend, render, free blend
    ScratchArena sa;
    scratch_init(&sa, arena);

    // outer block
    void* outer = scratch_alloc(&sa, 256);

    // clip scope
    void* clip = scratch_alloc(&sa, 1024);
    memset(clip, 0xCC, 1024);
    // ... render children ...
    scratch_free(&sa, clip); // LIFO

    EXPECT_EQ(scratch_live_count(&sa), 1); // outer

    // blend scope
    void* blend = scratch_alloc(&sa, 2048);
    memset(blend, 0xBB, 2048);
    // ... render ...
    scratch_free(&sa, blend); // LIFO

    EXPECT_EQ(scratch_live_count(&sa), 1); // outer

    scratch_free(&sa, outer);
    EXPECT_EQ(scratch_live_count(&sa), 0);

    scratch_release(&sa);
}

// ============================================================================
// TableMetadata pattern (many parallel arrays)
// ============================================================================

TEST_F(ScratchArenaTest, TableMetadataPattern) {
    // simulate TableMetadata: 12 arrays allocated, all freed together at scope end
    ScratchArena sa;
    scratch_init(&sa, arena);

    const int rows = 50, cols = 10;
    ScratchMark mark = scratch_mark(&sa);

    bool* grid_occupied = (bool*)scratch_calloc(&sa, rows * cols * sizeof(bool));
    float* col_widths = (float*)scratch_calloc(&sa, cols * sizeof(float));
    float* col_min_widths = (float*)scratch_calloc(&sa, cols * sizeof(float));
    float* col_max_widths = (float*)scratch_calloc(&sa, cols * sizeof(float));
    float* row_heights = (float*)scratch_calloc(&sa, rows * sizeof(float));
    float* row_y_pos = (float*)scratch_calloc(&sa, rows * sizeof(float));
    bool* row_collapsed = (bool*)scratch_calloc(&sa, rows * sizeof(bool));
    bool* col_collapsed = (bool*)scratch_calloc(&sa, cols * sizeof(bool));
    float* col_orig_widths = (float*)scratch_calloc(&sa, cols * sizeof(float));
    bool* row_pct_height = (bool*)scratch_calloc(&sa, rows * sizeof(bool));
    float* col_edge_border = (float*)scratch_calloc(&sa, (cols + 1) * sizeof(float));
    bool* col_explicit_w = (bool*)scratch_calloc(&sa, cols * sizeof(bool));

    ASSERT_NE(grid_occupied, nullptr);
    ASSERT_NE(col_explicit_w, nullptr);
    EXPECT_EQ(scratch_live_count(&sa), 12);

    // write and verify
    for (int i = 0; i < cols; i++) col_widths[i] = 80.0f + i;
    for (int i = 0; i < rows; i++) row_heights[i] = 24.0f;
    EXPECT_FLOAT_EQ(col_widths[5], 85.0f);
    EXPECT_FLOAT_EQ(row_heights[0], 24.0f);

    // scope exit: restore frees all 12 at once
    scratch_restore(&sa, mark);
    EXPECT_EQ(scratch_live_count(&sa), 0);

    scratch_release(&sa);
}
