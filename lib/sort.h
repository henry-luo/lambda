// lib/sort.h - small sort helpers.
//
// Two shapes:
//   1. insertion_sort - good for small N (< ~32) and nearly-sorted arrays.
//      Replaces the hand-written insertion / bubble sorts scattered in
//      lambda/py/py_builtins.cpp and lambda/lambda-vector.cpp.
//   2. SORT_BY_KEY - generic macro for sorting an array by a derived integer
//      or float key without writing a comparator.
//
// Existing qsort callers can keep using qsort; this header is additive.

#ifndef LIB_SORT_H
#define LIB_SORT_H

#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// In-place stable insertion sort. `cmp(a, b, udata)` follows the qsort_r
// convention: <0 if a<b, 0 if equal, >0 if a>b.
typedef int (*SortCmpFn)(const void* a, const void* b, void* udata);

static inline void insertion_sort(void* base, size_t count, size_t stride,
                                  SortCmpFn cmp, void* udata) {
    if (count < 2 || stride == 0 || !cmp) return;
    char* arr = (char*)base;
    // stack buffer: stride is typically small (8-64 bytes); fall back to heap
    // is not needed because callers using this on huge records should pick
    // qsort/introsort instead.
    char tmp[256];
    if (stride > sizeof(tmp)) return;  // caller error; assert in debug builds

    for (size_t i = 1; i < count; ++i) {
        memcpy(tmp, arr + i * stride, stride);
        size_t j = i;
        while (j > 0 && cmp(arr + (j - 1) * stride, tmp, udata) > 0) {
            memcpy(arr + j * stride, arr + (j - 1) * stride, stride);
            --j;
        }
        memcpy(arr + j * stride, tmp, stride);
    }
}

// Sort a pointer array ascending by an integer key.
typedef long (*SortKeyIntFn)(const void* item, void* udata);

static inline void sort_ptrs_by_int_key(void** arr, size_t n,
                                        SortKeyIntFn key_fn, void* udata) {
    if (!arr || n < 2 || !key_fn) return;
    for (size_t i = 1; i < n; ++i) {
        void* cur = arr[i];
        long k = key_fn(cur, udata);
        size_t j = i;
        while (j > 0 && key_fn(arr[j - 1], udata) > k) {
            arr[j] = arr[j - 1];
            --j;
        }
        arr[j] = cur;
    }
}

typedef double (*SortKeyDoubleFn)(const void* item, void* udata);

static inline void sort_ptrs_by_double_key(void** arr, size_t n,
                                           SortKeyDoubleFn key_fn, void* udata) {
    if (!arr || n < 2 || !key_fn) return;
    for (size_t i = 1; i < n; ++i) {
        void* cur = arr[i];
        double k = key_fn(cur, udata);
        size_t j = i;
        while (j > 0 && key_fn(arr[j - 1], udata) > k) {
            arr[j] = arr[j - 1];
            --j;
        }
        arr[j] = cur;
    }
}

#ifdef __cplusplus
}
#endif

#endif
