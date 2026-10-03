// lib/check.h - always-on invariant checks.
//
// assert() is compiled out under NDEBUG, so a violated invariant in a release
// build silently becomes undefined behaviour (a bad downcast, a stale index).
// LAM_CHECK keeps the test in every build: a failure logs one "CHECK-FAIL:"
// line and aborts, so corruption fails closed instead of spreading.
#ifndef LIB_CHECK_H
#define LIB_CHECK_H

#include <stdlib.h>
#include "log.h"

#if defined(__GNUC__) || defined(__clang__)
#define LAM_CHECK_LIKELY(x) __builtin_expect(!!(x), 1)
#define LAM_CHECK_NORETURN __attribute__((noreturn, cold, noinline))
#else
#define LAM_CHECK_LIKELY(x) (x)
#define LAM_CHECK_NORETURN
#endif

LAM_CHECK_NORETURN static inline void lam_check_fail(const char* expr, const char* file, int line) {
    log_error("CHECK-FAIL: %s at %s:%d", expr, file, line);
    abort();
}

#define LAM_CHECK(cond) \
    do { if (!LAM_CHECK_LIKELY(cond)) lam_check_fail(#cond, __FILE__, __LINE__); } while (0)

#endif // LIB_CHECK_H
