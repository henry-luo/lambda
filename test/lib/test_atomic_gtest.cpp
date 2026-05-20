// test/lib/test_atomic_gtest.cpp - tests for lib/atomic
#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>

#include "../../lib/atomic.h"

TEST(AtomicTest, Int32LoadStore) {
    atomic_int32 a = {0};
    EXPECT_EQ(atomic_load32(&a), 0);
    atomic_store32(&a, 42);
    EXPECT_EQ(atomic_load32(&a), 42);
    atomic_store32(&a, -7);
    EXPECT_EQ(atomic_load32(&a), -7);
}

TEST(AtomicTest, Int32IncDecAdd) {
    atomic_int32 a = {0};
    EXPECT_EQ(atomic_inc32(&a), 1);
    EXPECT_EQ(atomic_inc32(&a), 2);
    EXPECT_EQ(atomic_dec32(&a), 1);
    EXPECT_EQ(atomic_add32(&a, 5), 6);
    EXPECT_EQ(atomic_add32(&a, -10), -4);
    EXPECT_EQ(atomic_load32(&a), -4);
}

TEST(AtomicTest, Int64LoadStoreInc) {
    atomic_int64 a = {0};
    EXPECT_EQ(atomic_load64(&a), 0);
    atomic_store64(&a, (int64_t)INT64_MAX - 10);
    EXPECT_EQ(atomic_load64(&a), INT64_MAX - 10);
    EXPECT_EQ(atomic_inc64(&a), INT64_MAX - 9);
    EXPECT_EQ(atomic_add64(&a, 5), INT64_MAX - 4);
    EXPECT_EQ(atomic_dec64(&a), INT64_MAX - 5);
}

TEST(AtomicTest, Int32ContendedIncrement) {
    constexpr int kThreads = 8;
    constexpr int kPerThread = 10000;
    atomic_int32 counter = {0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&counter]() {
            for (int i = 0; i < kPerThread; ++i) atomic_inc32(&counter);
        });
    }
    for (auto& th : threads) th.join();
    EXPECT_EQ(atomic_load32(&counter), kThreads * kPerThread);
}

TEST(AtomicTest, Int64ContendedAdd) {
    constexpr int kThreads = 4;
    constexpr int64_t kDelta = 7;
    constexpr int kIterations = 5000;
    atomic_int64 counter = {0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&counter]() {
            for (int i = 0; i < kIterations; ++i) atomic_add64(&counter, kDelta);
        });
    }
    for (auto& th : threads) th.join();
    EXPECT_EQ(atomic_load64(&counter), (int64_t)kThreads * kIterations * kDelta);
}
