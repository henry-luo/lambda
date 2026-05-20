// lib/atomic.h - portable atomic counter helpers (header-only).
//
// Replaces the pthread_mutex_lock + (*counter)++ + unlock pattern that recurs
// in network code, prefetchers, etc. Use these for simple shared counters;
// for richer synchronization (CAS, fences) reach for <stdatomic.h> directly.
//
// Backends:
//   - clang / gcc → __atomic_* builtins (C11 memory model, lock-free for word-sized types)
//   - MSVC (not currently supported by this repo) would map to Interlocked*
//
// All operations use sequentially-consistent ordering; counters are not the
// hot path so the simpler default is preferred over relaxed/acquire/release
// for correctness clarity.

#ifndef LIB_ATOMIC_H
#define LIB_ATOMIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 32-bit signed atomic counter. Initialize with `atomic_int32 c = {0};` or
// pass `&c.v = 0` zeroed via memset/calloc.
typedef struct {
    int32_t v;
} atomic_int32;

// 64-bit signed atomic counter.
typedef struct {
    int64_t v;
} atomic_int64;

// --- int32 ----------------------------------------------------------------

static inline int32_t atomic_load32(const atomic_int32* a) {
    return __atomic_load_n(&a->v, __ATOMIC_SEQ_CST);
}

static inline void atomic_store32(atomic_int32* a, int32_t v) {
    __atomic_store_n(&a->v, v, __ATOMIC_SEQ_CST);
}

// Returns the new value (post-increment, like ++a).
static inline int32_t atomic_inc32(atomic_int32* a) {
    return __atomic_add_fetch(&a->v, 1, __ATOMIC_SEQ_CST);
}

// Returns the new value (post-decrement).
static inline int32_t atomic_dec32(atomic_int32* a) {
    return __atomic_sub_fetch(&a->v, 1, __ATOMIC_SEQ_CST);
}

// Returns the new value.
static inline int32_t atomic_add32(atomic_int32* a, int32_t delta) {
    return __atomic_add_fetch(&a->v, delta, __ATOMIC_SEQ_CST);
}

// --- int64 ----------------------------------------------------------------

static inline int64_t atomic_load64(const atomic_int64* a) {
    return __atomic_load_n(&a->v, __ATOMIC_SEQ_CST);
}

static inline void atomic_store64(atomic_int64* a, int64_t v) {
    __atomic_store_n(&a->v, v, __ATOMIC_SEQ_CST);
}

static inline int64_t atomic_inc64(atomic_int64* a) {
    return __atomic_add_fetch(&a->v, 1, __ATOMIC_SEQ_CST);
}

static inline int64_t atomic_dec64(atomic_int64* a) {
    return __atomic_sub_fetch(&a->v, 1, __ATOMIC_SEQ_CST);
}

static inline int64_t atomic_add64(atomic_int64* a, int64_t delta) {
    return __atomic_add_fetch(&a->v, delta, __ATOMIC_SEQ_CST);
}

#ifdef __cplusplus
}
#endif

#endif
