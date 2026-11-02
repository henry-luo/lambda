#pragma once
#ifndef _WIN32
#include "../../lib/atomic.h"
#include "../../lib/time_util.h"
#include <errno.h>
#include <pthread.h>

// Native fixture worker with bounded observation and joined lifetime. The
// action stores its own result before done becomes visible to the test.
struct SafetyAsync {
    pthread_t thread = {};
    atomic_int32 started = {}, done = {};
    void (*action)(void*) = nullptr;
    void* context = nullptr;
    bool running = false;
    static void* run(void* context) {
        SafetyAsync* worker = (SafetyAsync*)context;
        atomic_store32(&worker->started, 1); worker->action(worker->context);
        atomic_store32(&worker->done, 1); return nullptr;
    }
    int start(void (*fn)(void*), void* ctx) {
        if (running) return EBUSY;
        action = fn; context = ctx; atomic_store32(&started, 0); atomic_store32(&done, 0);
        int result = pthread_create(&thread, nullptr, run, this); running = result == 0; return result;
    }
    int join() {
        if (!running) return 0;
        int result = pthread_join(thread, nullptr); if (!result) running = false; return result;
    }
    ~SafetyAsync() { (void)join(); }
    static bool wait(const atomic_int32* flag, uint32_t timeout_ms = 2000) {
        uint64_t deadline = time_now_ms() + timeout_ms;
        while (!atomic_load32(flag) && time_now_ms() < deadline) {
            struct timespec pause = {0, 1000000}; nanosleep(&pause, nullptr);
        }
        return atomic_load32(flag) != 0;
    }
};
#endif
