// One reference-count implementation for counted objects: the targets of
// lam::Counted<T> fields (pictures, font handles, byte storage, DOM ranges).
// Every operation is atomic; a count never overflows or goes below zero.
#pragma once

#include "atomic.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RefCount {
    atomic_int32 n;
} RefCount;

// a new object starts with the one reference its creator holds
static inline void ref_count_init(RefCount* count) {
    atomic_store32(&count->n, 1);
}

static inline int32_t ref_count_get(const RefCount* count) {
    return atomic_load32(&count->n);
}

// Adds a reference. Fails, leaving the count unchanged, when the object is
// already released (zero) or the count is saturated.
static inline bool ref_count_retain(RefCount* count) {
    int32_t refs = atomic_load32(&count->n);
    for (;;) {
        if (refs <= 0 || refs == INT32_MAX) return false;
        if (__atomic_compare_exchange_n(&count->n.v, &refs, refs + 1, false,
                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return true;
    }
}

typedef enum RefCountRelease {
    REF_COUNT_LIVE,       // other references remain
    REF_COUNT_LAST,       // this was the last reference: the caller frees the object
    REF_COUNT_UNDERFLOW,  // the count was already zero; nothing changed
} RefCountRelease;

static inline RefCountRelease ref_count_release(RefCount* count) {
    int32_t refs = atomic_load32(&count->n);
    for (;;) {
        if (refs <= 0) return REF_COUNT_UNDERFLOW;
        if (__atomic_compare_exchange_n(&count->n.v, &refs, refs - 1, false,
                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
            return refs == 1 ? REF_COUNT_LAST : REF_COUNT_LIVE;
        }
    }
}

#ifdef __cplusplus
}
#endif
