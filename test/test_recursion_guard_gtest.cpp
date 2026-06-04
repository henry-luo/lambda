#include <gtest/gtest.h>

#include "../lib/recursion_guard.hpp"

namespace {

// A recursive walker that caps its own depth via RecursionGuard. Records the deepest
// level it reached while still within the limit.
int g_depth = 0;
int g_deepest_ok = 0;

void recur(int n, int limit) {
    lam::RecursionGuard g(&g_depth, limit);
    if (!g) return;                              // too deep — bail without recursing
    if (g_depth > g_deepest_ok) g_deepest_ok = g_depth;
    if (n > 0) recur(n - 1, limit);
}

} // namespace

TEST(RecursionGuard, IncrementsAndBalancesOnScopeExit) {
    int depth = 0;
    {
        lam::RecursionGuard g(&depth, 3);
        EXPECT_TRUE((bool)g);
        EXPECT_EQ(depth, 1);
        {
            lam::RecursionGuard g2(&depth, 3);
            EXPECT_TRUE((bool)g2);
            EXPECT_EQ(depth, 2);
        }
        EXPECT_EQ(depth, 1);   // inner guard restored depth
    }
    EXPECT_EQ(depth, 0);       // fully balanced
}

TEST(RecursionGuard, StopsAtLimitAndUnwindsCleanly) {
    g_depth = 0;
    g_deepest_ok = 0;

    recur(100, 3);             // would recurse 100 deep without the guard

    EXPECT_EQ(g_deepest_ok, 3);  // never proceeded past the limit
    EXPECT_EQ(g_depth, 0);       // counter balanced after full unwind
}

TEST(RecursionGuard, OverLimitGuardStillBalances) {
    int depth = 2;             // pretend we are already 2 deep
    {
        lam::RecursionGuard g(&depth, 2);   // entering would make it 3 > 2
        EXPECT_FALSE((bool)g);
        EXPECT_EQ(depth, 3);   // still incremented while in scope
    }
    EXPECT_EQ(depth, 2);       // and decremented on exit
}
