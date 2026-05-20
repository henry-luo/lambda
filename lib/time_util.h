// lib/time_util.h - monotonic time helpers (header-only).
//
// Wraps clock_gettime(CLOCK_MONOTONIC, ...) once so callers can stop carrying
// `#ifdef __APPLE__` blocks. Monotonic means the value never goes backward
// and isn't affected by wall-clock adjustments — use it for elapsed-time
// measurements, TTL/cache deadlines, deadlines passed to condvar timed-wait.
//
// For wall-clock time (Unix epoch, time-of-day, displayed timestamps) use
// `time(NULL)` or `lib/datetime.h` instead.

#ifndef LIB_TIME_UTIL_H
#define LIB_TIME_UTIL_H

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// nanoseconds since an arbitrary monotonic epoch
static inline uint64_t time_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// microseconds since an arbitrary monotonic epoch
static inline uint64_t time_now_us(void) {
    return time_now_ns() / 1000ULL;
}

// milliseconds since an arbitrary monotonic epoch
static inline uint64_t time_now_ms(void) {
    return time_now_ns() / 1000000ULL;
}

// seconds (with fractional part) since an arbitrary monotonic epoch
static inline double time_now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

// convenience: elapsed milliseconds since a previously captured time_now_ms()
static inline uint64_t time_elapsed_ms_since(uint64_t start_ms) {
    uint64_t now = time_now_ms();
    return now > start_ms ? now - start_ms : 0;
}

#ifdef __cplusplus
}
#endif

#endif
