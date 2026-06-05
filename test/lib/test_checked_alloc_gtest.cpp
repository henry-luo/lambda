#include <gtest/gtest.h>
#include <stdint.h>
#include <limits.h>

#include "../../lib/checked_math.hpp"
#include "../../lib/checked_alloc.hpp"
#include "../../lib/ownership.hpp"   // NonNull + arena helpers

// ---- checked_math --------------------------------------------------------

TEST(CheckedMath, MulNormalAndOverflow) {
    size_t out = 0;
    EXPECT_TRUE(lam::checked_mul(1000, 1000, &out));
    EXPECT_EQ(out, 1000000u);

    EXPECT_TRUE(lam::checked_mul(0, SIZE_MAX, &out));
    EXPECT_EQ(out, 0u);

    EXPECT_FALSE(lam::checked_mul(SIZE_MAX, 2, &out));
    EXPECT_FALSE(lam::checked_mul(SIZE_MAX / 2 + 1, 2, &out));
}

TEST(CheckedMath, AddOverflow) {
    size_t out = 0;
    EXPECT_TRUE(lam::checked_add(100, 28, &out));
    EXPECT_EQ(out, 128u);
    EXPECT_FALSE(lam::checked_add(SIZE_MAX, 1, &out));
}

TEST(CheckedMath, MulAddCombines) {
    size_t out = 0;
    EXPECT_TRUE(lam::checked_mul_add(sizeof(int), 10, 8, &out));
    EXPECT_EQ(out, sizeof(int) * 10 + 8);
    EXPECT_FALSE(lam::checked_mul_add(SIZE_MAX, 2, 0, &out));
}

TEST(CheckedMath, NarrowRoundTrips) {
    int small = 0;
    EXPECT_TRUE(lam::checked_narrow((int64_t)42, &small));
    EXPECT_EQ(small, 42);

    int truncated = 0;
    EXPECT_FALSE(lam::checked_narrow((int64_t)INT_MAX + 1, &truncated));

    uint8_t byte = 0;
    EXPECT_TRUE(lam::checked_narrow((int)200, &byte));
    EXPECT_EQ(byte, 200);
    EXPECT_FALSE(lam::checked_narrow((int)-1, &byte));   // negative doesn't fit unsigned
    EXPECT_FALSE(lam::checked_narrow((int)256, &byte));
}

// ---- checked_alloc (fallible / Type-1) -----------------------------------

TEST(CheckedAlloc, PoolArrayZeroesAndRejectsOverflow) {
    Pool* p = pool_create();
    ASSERT_NE(p, nullptr);

    int* a = lam::checked_pool_array<int>(p, 16);
    ASSERT_NE(a, nullptr);
    for (int i = 0; i < 16; i++) EXPECT_EQ(a[i], 0);   // pool_calloc zeroes
    a[15] = 7;
    EXPECT_EQ(a[15], 7);

    char* bad = lam::checked_pool_array<char>(p, SIZE_MAX, 2);   // overflow → NULL
    EXPECT_EQ(bad, nullptr);

    pool_destroy(p);
}

TEST(CheckedAlloc, ReallocGrowsAndPreservesOriginalOnFailure) {
    int* buf = nullptr;
    ASSERT_TRUE(lam::checked_realloc(&buf, 8));
    ASSERT_NE(buf, nullptr);
    buf[0] = 5;

    ASSERT_TRUE(lam::checked_realloc(&buf, 64));
    ASSERT_NE(buf, nullptr);
    EXPECT_EQ(buf[0], 5);                 // contents preserved across grow

    int* before = buf;
    EXPECT_FALSE(lam::checked_realloc(&buf, SIZE_MAX, 2));   // overflow → false
    EXPECT_EQ(buf, before);              // *slot unchanged — no leak, no dangling
    EXPECT_EQ(buf[0], 5);

    free(buf);
}

// ---- NonNull + infallible arena (Type-2) ---------------------------------

TEST(NonNullArena, ArenaNewIsNonNullAndZeroed) {
    Pool* p = pool_create();
    ASSERT_NE(p, nullptr);
    Arena* a = arena_create_default(p);
    ASSERT_NE(a, nullptr);

    lam::NonNull<int> n = lam::arena_new<int>(a);
    EXPECT_NE(n.get(), nullptr);
    EXPECT_EQ(*n, 0);
    *n = 42;
    EXPECT_EQ(*n, 42);

    // FAM-style header+payload allocation
    lam::NonNull<int32_t> hdr = lam::arena_new_sized<int32_t>(a, 64);
    EXPECT_NE(hdr.get(), nullptr);
    *hdr = 1;
    EXPECT_EQ(*hdr, 1);

    // reference sugar
    double& d = lam::arena_emplace<double>(a);
    d = 3.5;
    EXPECT_EQ(d, 3.5);

    // implicit decay to a pool-domain borrow
    lam::BorrowedPtr<int, lam::PoolDomain> b = n;
    EXPECT_EQ(b.get(), n.get());

    arena_destroy(a);
    pool_destroy(p);
}

TEST(NonNullArena, PoolTryNewCompanion) {
    Pool* p = pool_create();
    ASSERT_NE(p, nullptr);
    int* x = lam::pool_try_new<int>(p);
    ASSERT_NE(x, nullptr);
    EXPECT_EQ(*x, 0);
    pool_destroy(p);
}

#if !defined(NDEBUG) && defined(GTEST_HAS_DEATH_TEST) && GTEST_HAS_DEATH_TEST
TEST(NonNullDeathTest, ConstructingFromNullAsserts) {
    EXPECT_DEATH({ lam::NonNull<int> bad((int*)nullptr); (void)bad; }, "");
}
#endif
