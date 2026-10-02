#include <gtest/gtest.h>
#include "../../lib/scratch_arena.h"
#include "../../lib/arena.h"
#include "../../lib/mempool.h"
#include <string.h>

// Test fixture with pool + arena setup
class ScratchArenaTest : public ::testing::Test {
protected:
    Pool* pool;
    Arena* arena;

    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr);
        arena = arena_create(16 * 1024, 64 * 1024); // 16KB initial
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
    EXPECT_EQ(arena_active_scope_count(arena), 1u);
    EXPECT_EQ(scratch_live_count(&sa), 0);
    scratch_release(&sa);
    EXPECT_EQ(arena_active_scope_count(arena), 0u);
    EXPECT_EQ(scratch_live_count(&sa), 0);
}

TEST_F(ScratchArenaTest, RepeatedReleaseLeavesScopeBalanced) {
    ScratchArena sa;
    scratch_init(&sa, arena);
    scratch_release(&sa);
    scratch_release(&sa);
    EXPECT_EQ(arena_active_scope_count(arena), 0u);
}

TEST_F(ScratchArenaTest, PlainAllocIsAlignedAndZeroed) {
    ScratchArena sa;
    scratch_init(&sa, arena);

    void* p = scratch_alloc(&sa, 64);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ((uintptr_t)p % 16, 0);
    uint8_t* z = (uint8_t*)scratch_calloc(&sa, 256);
    ASSERT_NE(z, nullptr);
    for (int i = 0; i < 256; i++) {
        EXPECT_EQ(z[i], 0) << "byte " << i << " not zero";
    }
    EXPECT_EQ(scratch_live_count(&sa), 2);

    scratch_release(&sa);
}

// ============================================================================
// Mark / Restore
// ============================================================================

TEST_F(ScratchArenaTest, MarkRestore_Basic) {
    ScratchArena sa;
    scratch_init(&sa, arena);

    ASSERT_NE(scratch_alloc(&sa, 100), nullptr);  // a
    ScratchMark mark = scratch_mark(&sa);

    ASSERT_NE(scratch_alloc(&sa, 200), nullptr);  // b
    ASSERT_NE(scratch_alloc(&sa, 300), nullptr);  // c
    EXPECT_EQ(scratch_live_count(&sa), 3);

    // restore should free b and c
    scratch_restore(&sa, mark);
    EXPECT_EQ(scratch_live_count(&sa), 1); // only a remains

    scratch_release(&sa);
    EXPECT_EQ(scratch_live_count(&sa), 0);
}

TEST_F(ScratchArenaTest, MarkRestore_Nested) {
    ScratchArena sa;
    scratch_init(&sa, arena);

    ASSERT_NE(scratch_alloc(&sa, 64), nullptr);  // a
    ScratchMark mark1 = scratch_mark(&sa);

    ASSERT_NE(scratch_alloc(&sa, 64), nullptr);  // b
    ScratchMark mark2 = scratch_mark(&sa);

    ASSERT_NE(scratch_alloc(&sa, 64), nullptr);  // c
    ASSERT_NE(scratch_alloc(&sa, 64), nullptr);  // d

    // restore inner scope
    scratch_restore(&sa, mark2);
    EXPECT_EQ(scratch_live_count(&sa), 2); // a, b

    // restore outer scope
    scratch_restore(&sa, mark1);
    EXPECT_EQ(scratch_live_count(&sa), 1); // a

    scratch_release(&sa);
}

TEST_F(ScratchArenaTest, MarkRestore_EmptyScope) {
    ScratchArena sa;
    scratch_init(&sa, arena);

    ASSERT_NE(scratch_alloc(&sa, 64), nullptr);  // a
    ScratchMark mark = scratch_mark(&sa);

    // no allocations between mark and restore
    scratch_restore(&sa, mark);
    EXPECT_EQ(scratch_live_count(&sa), 1);

    scratch_release(&sa);
}

// ============================================================================
// Data integrity
// ============================================================================

TEST_F(ScratchArenaTest, DataIntegrity_WriteAndRead) {
    ScratchArena sa;
    scratch_init(&sa, arena);

    // simulate a table layout: multiple arrays of different sizes
    ScratchScope scope(&sa);
    int* col_widths = scope.array_zero<int>(20);
    float* row_heights = scope.array_zero<float>(50);
    uint8_t* grid = scope.array_zero<uint8_t>(20 * 50);

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

    scope.end();
    EXPECT_EQ(scratch_live_count(&sa), 0);
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
// Reuse after scope end
// ============================================================================

TEST_F(ScratchArenaTest, ReuseAfterScopeEnd) {
    ScratchArena sa;
    scratch_init(&sa, arena);

    size_t used_start = arena_total_used(arena);

    // allocate and free several times — memory should be reclaimed each time
    for (int round = 0; round < 10; round++) {
        ScratchScope scope(&sa);
        void* a = scope.alloc(1024);
        void* b = scope.alloc(1024);
        ASSERT_NE(a, nullptr);
        ASSERT_NE(b, nullptr);
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
    ScratchMark null_scope = scratch_scope_begin(nullptr);
    EXPECT_EQ(scratch_scope_alloc(nullptr, &null_scope, 64), nullptr);
    scratch_scope_end(nullptr, &null_scope);
    scratch_release(nullptr);
    EXPECT_EQ(scratch_live_count(nullptr), 0);

    ScratchArena sa;
    scratch_init(&sa, arena);
    EXPECT_EQ(scratch_alloc(&sa, 0), nullptr); // zero-size
    scratch_release(&sa);
}

TEST_F(ScratchArenaTest, SingleAllocation_Release) {
    ScratchArena sa;
    scratch_init(&sa, arena);

    ASSERT_NE(scratch_alloc(&sa, 32), nullptr);
    EXPECT_EQ(scratch_live_count(&sa), 1);

    scratch_release(&sa);
    EXPECT_EQ(scratch_live_count(&sa), 0);
}

// ============================================================================
// Scopes (the only individual release path)
// ============================================================================

TEST_F(ScratchArenaTest, Scope_EndFreesOwnBlocks) {
    ScratchArena sa;
    scratch_init(&sa, arena);

    void* outer = scratch_alloc(&sa, 256);
    size_t used_before = arena_total_used(arena);
    {
        ScratchScope scope(&sa);
        void* a = scope.alloc(512);
        void* b = scope.alloc_zero(512);
        ASSERT_NE(a, nullptr);
        ASSERT_NE(b, nullptr);
        EXPECT_EQ(((uint8_t*)b)[100], 0);
        EXPECT_EQ(scratch_live_count(&sa), 3);
    }
    EXPECT_EQ(scratch_live_count(&sa), 1);  // outer
    EXPECT_LE(arena_total_used(arena), used_before + 32);
    EXPECT_NE(outer, nullptr);

    scratch_release(&sa);
}

TEST_F(ScratchArenaTest, Scope_NestedEndInOrder) {
    // render_block_view pattern: a clip scope, then a blend scope, inside a block scope
    ScratchArena sa;
    scratch_init(&sa, arena);

    ScratchScope block(&sa);
    void* outer = block.alloc(256);
    {
        ScratchScope clip(&sa);
        memset(clip.alloc(1024), 0xCC, 1024);
        EXPECT_EQ(sa.open_scope, clip.mark.scope);
    }
    EXPECT_EQ(sa.open_scope, block.mark.scope);
    {
        ScratchScope blend(&sa);
        memset(blend.alloc(2048), 0xBB, 2048);
    }
    EXPECT_EQ(scratch_live_count(&sa), 1);
    EXPECT_NE(outer, nullptr);
    block.end();
    EXPECT_EQ(sa.open_scope, 0u);
    EXPECT_EQ(scratch_live_count(&sa), 0);

    scratch_release(&sa);
}

TEST_F(ScratchArenaTest, Scope_EndIsIdempotent) {
    ScratchArena sa;
    scratch_init(&sa, arena);

    ScratchMark scope = scratch_scope_begin(&sa);
    ASSERT_NE(scratch_scope_alloc(&sa, &scope, 64), nullptr);
    scratch_scope_end(&sa, &scope);
    EXPECT_EQ(scope.scope, 0u);
    scratch_scope_end(&sa, &scope);  // ended scope: no-op
    EXPECT_EQ(scratch_scope_alloc(&sa, &scope, 64), nullptr);
    EXPECT_EQ(scratch_live_count(&sa), 0);

    scratch_release(&sa);
}

TEST_F(ScratchArenaTest, Scope_ArrayOverflowReturnsNull) {
    ScratchArena sa;
    scratch_init(&sa, arena);
    {
        ScratchScope scope(&sa);
        EXPECT_EQ(scope.array<uint64_t>(SIZE_MAX / 4), nullptr);
        EXPECT_EQ(scope.array_zero<uint64_t>(SIZE_MAX / 4), nullptr);
        EXPECT_EQ(scratch_live_count(&sa), 0);
    }
    scratch_release(&sa);
}

TEST_F(ScratchArenaTest, Scope_ReleaseClosesOpenScopes) {
    ScratchArena sa;
    scratch_init(&sa, arena);
    ScratchMark scope = scratch_scope_begin(&sa);
    ASSERT_NE(scratch_scope_alloc(&sa, &scope, 64), nullptr);
    scratch_release(&sa);
    EXPECT_EQ(sa.open_scope, 0u);
    EXPECT_EQ(scratch_live_count(&sa), 0);
}

#ifndef NDEBUG
// A block the scope did not allocate (a plain scratch_alloc made inside it)
// would be unwound under its owner, so ending the scope must fail loudly.
TEST_F(ScratchArenaTest, Scope_ForeignLiveBlockAsserts) {
    EXPECT_DEATH({
        ScratchArena sa;
        scratch_init(&sa, arena);
        ScratchMark scope = scratch_scope_begin(&sa);
        scratch_scope_alloc(&sa, &scope, 64);
        scratch_alloc(&sa, 64);  // foreign: not allocated through the scope
        scratch_scope_end(&sa, &scope);
    }, "");
}

TEST_F(ScratchArenaTest, Scope_OutOfOrderEndAsserts) {
    EXPECT_DEATH({
        ScratchArena sa;
        scratch_init(&sa, arena);
        ScratchMark outer = scratch_scope_begin(&sa);
        ScratchMark inner = scratch_scope_begin(&sa);
        (void)inner;
        scratch_scope_end(&sa, &outer);  // inner is still open
    }, "");
}

TEST_F(ScratchArenaTest, Scope_AllocThroughOuterScopeAsserts) {
    EXPECT_DEATH({
        ScratchArena sa;
        scratch_init(&sa, arena);
        ScratchMark outer = scratch_scope_begin(&sa);
        ScratchMark inner = scratch_scope_begin(&sa);
        (void)inner;
        scratch_scope_alloc(&sa, &outer, 64);  // would sit above inner's mark
    }, "");
}
#endif

// ============================================================================
// TableMetadata pattern (many parallel arrays)
// ============================================================================

TEST_F(ScratchArenaTest, TableMetadataPattern) {
    // simulate TableMetadata: 12 arrays allocated, all freed together at scope end
    ScratchArena sa;
    scratch_init(&sa, arena);

    const int rows = 50, cols = 10;
    ScratchScope scope(&sa);

    bool* grid_occupied = scope.array_zero<bool>(rows * cols);
    float* col_widths = scope.array_zero<float>(cols);
    float* col_min_widths = scope.array_zero<float>(cols);
    float* col_max_widths = scope.array_zero<float>(cols);
    float* row_heights = scope.array_zero<float>(rows);
    float* row_y_pos = scope.array_zero<float>(rows);
    bool* row_collapsed = scope.array_zero<bool>(rows);
    bool* col_collapsed = scope.array_zero<bool>(cols);
    float* col_orig_widths = scope.array_zero<float>(cols);
    bool* row_pct_height = scope.array_zero<bool>(rows);
    float* col_edge_border = scope.array_zero<float>((cols + 1));
    bool* col_explicit_w = scope.array_zero<bool>(cols);

    ASSERT_NE(grid_occupied, nullptr);
    ASSERT_NE(col_explicit_w, nullptr);
    EXPECT_EQ(scratch_live_count(&sa), 12);

    // write and verify
    for (int i = 0; i < cols; i++) col_widths[i] = 80.0f + i;
    for (int i = 0; i < rows; i++) row_heights[i] = 24.0f;
    EXPECT_FLOAT_EQ(col_widths[5], 85.0f);
    EXPECT_FLOAT_EQ(row_heights[0], 24.0f);

    // scope exit frees all 12 at once
    scope.end();
    EXPECT_EQ(scratch_live_count(&sa), 0);

    scratch_release(&sa);
}
