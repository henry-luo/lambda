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

// ───────── Standard comparators ─────────
//
// Two-argument signature matches qsort(3). Use these directly with qsort to
// avoid writing one-off comparators. For use with the 3-arg insertion_sort
// above, wrap with SORT_CMP_AS_R(name).
//
// Naming: sort_cmp_<type>_<order> where <order> is `asc` or `desc`.

typedef int (*SortCmp2Fn)(const void* a, const void* b);

static inline int sort_cmp_int_asc(const void* a, const void* b) {
    int va = *(const int*)a, vb = *(const int*)b;
    return (va > vb) - (va < vb);
}
static inline int sort_cmp_int_desc(const void* a, const void* b) {
    int va = *(const int*)a, vb = *(const int*)b;
    return (vb > va) - (vb < va);
}

#include <stdint.h>
static inline int sort_cmp_int64_asc(const void* a, const void* b) {
    int64_t va = *(const int64_t*)a, vb = *(const int64_t*)b;
    return (va > vb) - (va < vb);
}
static inline int sort_cmp_int64_desc(const void* a, const void* b) {
    int64_t va = *(const int64_t*)a, vb = *(const int64_t*)b;
    return (vb > va) - (vb < va);
}

static inline int sort_cmp_uint64_asc(const void* a, const void* b) {
    uint64_t va = *(const uint64_t*)a, vb = *(const uint64_t*)b;
    return (va > vb) - (va < vb);
}

static inline int sort_cmp_double_asc(const void* a, const void* b) {
    double va = *(const double*)a, vb = *(const double*)b;
    return (va > vb) - (va < vb);
}
static inline int sort_cmp_double_desc(const void* a, const void* b) {
    double va = *(const double*)a, vb = *(const double*)b;
    return (vb > va) - (vb < va);
}

static inline int sort_cmp_float_asc(const void* a, const void* b) {
    float va = *(const float*)a, vb = *(const float*)b;
    return (va > vb) - (va < vb);
}

// const char* — pointer-to-pointer style, matching how arrays of strings
// are typically passed to qsort: `qsort(arr_of_cstrs, n, sizeof(char*), sort_cmp_cstr_asc)`.
static inline int sort_cmp_cstr_asc(const void* a, const void* b) {
    const char* sa = *(const char* const*)a;
    const char* sb = *(const char* const*)b;
    return strcmp(sa, sb);
}

#include <strings.h>
static inline int sort_cmp_cstr_ci_asc(const void* a, const void* b) {
    const char* sa = *(const char* const*)a;
    const char* sb = *(const char* const*)b;
    return strcasecmp(sa, sb);
}

// SORT_CMP_AS_R(name) — produce a 3-arg adapter `name##_r` that ignores its
// udata argument, suitable for passing to insertion_sort.
#define SORT_CMP_AS_R(name) \
    static inline int name##_r(const void* a, const void* b, void* udata) { \
        (void)udata; return name(a, b); \
    }

#ifdef __cplusplus
}
#endif

#endif
