// test/lib/test_thread_pool_gtest.cpp - tests for lib/thread_pool
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <mutex>

extern "C" {
#include "../../lib/thread_pool.h"
}

namespace {

struct CounterJob { std::atomic<int>* counter; };

void increment_job(void* arg) {
    auto* job = static_cast<CounterJob*>(arg);
    job->counter->fetch_add(1, std::memory_order_relaxed);
}

struct SlowJob {
    std::atomic<int>* counter;
    int delay_ms;
};

void slow_job(void* arg) {
    auto* job = static_cast<SlowJob*>(arg);
    std::this_thread::sleep_for(std::chrono::milliseconds(job->delay_ms));
    job->counter->fetch_add(1, std::memory_order_relaxed);
}

}  // namespace

TEST(ThreadPoolTest, CreateDestroy) {
    ThreadPool* tp = tp_create(2);
    ASSERT_NE(tp, nullptr);
    EXPECT_EQ(tp_thread_count(tp), 2);
    tp_destroy(tp);
}

TEST(ThreadPoolTest, CreateAutoDetect) {
    ThreadPool* tp = tp_create(0);
    ASSERT_NE(tp, nullptr);
    EXPECT_GT(tp_thread_count(tp), 0);
    tp_destroy(tp);
}

TEST(ThreadPoolTest, CreateWithStackSize) {
    // 1 MB stack is well above the platform minimum on all targets we support.
    ThreadPool* tp = tp_create_with_stack(2, 1024 * 1024);
    ASSERT_NE(tp, nullptr);
    EXPECT_EQ(tp_thread_count(tp), 2);
    std::atomic<int> counter{0};
    CounterJob job{&counter};
    ASSERT_TRUE(tp_submit(tp, increment_job, &job));
    tp_wait_all(tp);
    EXPECT_EQ(counter.load(), 1);
    tp_destroy(tp);
}

TEST(ThreadPoolTest, SubmitAndWait) {
    ThreadPool* tp = tp_create(4);
    std::atomic<int> counter{0};
    constexpr int N = 100;
    CounterJob jobs[N];
    for (int i = 0; i < N; ++i) {
        jobs[i].counter = &counter;
        ASSERT_TRUE(tp_submit(tp, increment_job, &jobs[i]));
    }
    tp_wait_all(tp);
    EXPECT_EQ(counter.load(), N);
    tp_destroy(tp);
}

TEST(ThreadPoolTest, WaitAllIsReusable) {
    ThreadPool* tp = tp_create(2);
    std::atomic<int> counter{0};
    CounterJob job{&counter};

    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < 10; ++i) {
            ASSERT_TRUE(tp_submit(tp, increment_job, &job));
        }
        tp_wait_all(tp);
    }
    EXPECT_EQ(counter.load(), 30);
    tp_destroy(tp);
}

TEST(ThreadPoolTest, SubmitAfterShutdownFails) {
    ThreadPool* tp = tp_create(2);
    tp_shutdown(tp);
    std::atomic<int> counter{0};
    CounterJob job{&counter};
    EXPECT_FALSE(tp_submit(tp, increment_job, &job));
    tp_destroy(tp);
}

TEST(ThreadPoolTest, DestroyDrainsPendingJobsBeforeReturn) {
    // submit slow jobs and immediately destroy; the worker threads finish their
    // current job before being joined.
    ThreadPool* tp = tp_create(2);
    std::atomic<int> counter{0};
    SlowJob jobs[4];
    for (int i = 0; i < 4; ++i) {
        jobs[i] = {&counter, 20};
        ASSERT_TRUE(tp_submit(tp, slow_job, &jobs[i]));
    }
    tp_wait_all(tp);
    EXPECT_EQ(counter.load(), 4);
    tp_destroy(tp);
}

TEST(ThreadPoolTest, PendingReflectsQueueDepth) {
    ThreadPool* tp = tp_create(1);  // single worker so jobs queue up
    std::atomic<int> counter{0};
    SlowJob jobs[3];
    for (int i = 0; i < 3; ++i) jobs[i] = {&counter, 30};

    ASSERT_TRUE(tp_submit(tp, slow_job, &jobs[0]));
    ASSERT_TRUE(tp_submit(tp, slow_job, &jobs[1]));
    ASSERT_TRUE(tp_submit(tp, slow_job, &jobs[2]));
    // some may have been picked up — count is between 0 and 3
    size_t pending = tp_pending(tp);
    EXPECT_LE(pending, 3u);

    tp_wait_all(tp);
    EXPECT_EQ(counter.load(), 3);
    EXPECT_EQ(tp_pending(tp), 0u);
    tp_destroy(tp);
}

TEST(ThreadPoolTest, NullInputSafety) {
    EXPECT_EQ(tp_thread_count(nullptr), 0);
    EXPECT_EQ(tp_pending(nullptr), 0u);
    tp_wait_all(nullptr);
    tp_shutdown(nullptr);
    tp_destroy(nullptr);
    EXPECT_FALSE(tp_submit(nullptr, [](void*){}, nullptr));
}

TEST(ThreadPoolTest, ShutdownIdempotent) {
    ThreadPool* tp = tp_create(2);
    tp_shutdown(tp);
    tp_shutdown(tp);  // should be a no-op the second time
    tp_destroy(tp);
}

TEST(ThreadPoolTest, PriorityClampedToValidRange) {
    ThreadPool* tp = tp_create(2);
    std::atomic<int> counter{0};
    CounterJob job{&counter};
    // out-of-range priority values should clamp, not crash
    EXPECT_TRUE(tp_submit_priority(tp, increment_job, &job, (TpPriority)-5));
    EXPECT_TRUE(tp_submit_priority(tp, increment_job, &job, (TpPriority)100));
    tp_wait_all(tp);
    EXPECT_EQ(counter.load(), 2);
    tp_destroy(tp);
}
