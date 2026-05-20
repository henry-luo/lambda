// test/lib/test_time_util_gtest.cpp - tests for lib/time_util
#include <gtest/gtest.h>
#include <chrono>
#include <thread>

#include "../../lib/time_util.h"

TEST(TimeUtilTest, MonotonicAdvancesAcrossSleep) {
    uint64_t t0 = time_now_ms();
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    uint64_t t1 = time_now_ms();
    EXPECT_GT(t1, t0);
    EXPECT_GE(t1 - t0, 10u);   // slept ≥10ms (slack for scheduler)
    EXPECT_LT(t1 - t0, 5000u); // sanity bound
}

TEST(TimeUtilTest, UnitsAreConsistent) {
    uint64_t ns0 = time_now_ns();
    uint64_t us0 = time_now_us();
    uint64_t ms0 = time_now_ms();
    // All sampled within a few microseconds of each other.
    EXPECT_GE(us0, ns0 / 1000ULL);
    EXPECT_LE(us0, (ns0 / 1000ULL) + 1000ULL);
    EXPECT_GE(ms0, ns0 / 1000000ULL);
    EXPECT_LE(ms0, (ns0 / 1000000ULL) + 5ULL);
}

TEST(TimeUtilTest, SecondsAndMsAgree) {
    double s = time_now_seconds();
    uint64_t ms = time_now_ms();
    double s_from_ms = (double)ms / 1000.0;
    // Within 100ms tolerance (samples not taken atomically).
    EXPECT_NEAR(s, s_from_ms, 0.1);
}

TEST(TimeUtilTest, ElapsedSinceReportsAtLeastDelay) {
    uint64_t start = time_now_ms();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    uint64_t elapsed = time_elapsed_ms_since(start);
    EXPECT_GE(elapsed, 15u);
    EXPECT_LT(elapsed, 1000u);
}

TEST(TimeUtilTest, ElapsedSinceFutureClampsToZero) {
    uint64_t future = time_now_ms() + 1000000;  // hopefully far enough ahead
    EXPECT_EQ(time_elapsed_ms_since(future), 0u);
}
