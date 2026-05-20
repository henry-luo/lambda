// lib/binsearch.h - Header-only binary search helpers for sorted lookup tables.
//
// Two main shapes:
//   1. binsearch_strtab() - look up a key inside an array of const char* sorted
//      lexicographically. Optional case-insensitive comparison.
//   2. binsearch_records() - generic void* variant for arbitrary record arrays,
//      using a caller-supplied comparator.
//
// Returns the index (>= 0) on hit, or -1 on miss. All inline to avoid pulling
// a .c TU; the bodies are small enough that callers benefit from inlining.

#ifndef LIB_BINSEARCH_H
#define LIB_BINSEARCH_H

#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>

#ifdef __cplusplus
extern "C" {
#endif

// returns -1 if not found; otherwise the index in `table` (0..count-1).
// `table` must be lexicographically sorted under the chosen comparator.
// `key` need not be NUL-terminated; `key_len` controls length.
static inline int binsearch_strtab_n(const char* const* table, int count,
                                     const char* key, size_t key_len,
                                     bool case_insensitive) {
    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        int mid = lo + ((hi - lo) >> 1);
        const char* s = table[mid];
        int cmp = case_insensitive
            ? strncasecmp(s, key, key_len)
            : strncmp(s, key, key_len);
        if (cmp == 0) {
            // strn(case)cmp ignores trailing chars in s; require equal length.
            char tail = s[key_len];
            if (tail == '\0') return mid;
            cmp = (unsigned char)tail;  // s longer than key -> s > key
        }
        if (cmp < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

// convenience: look up a NUL-terminated key.
static inline int binsearch_strtab(const char* const* table, int count,
                                   const char* key, bool case_insensitive) {
    return binsearch_strtab_n(table, count, key, strlen(key), case_insensitive);
}

// generic: returns index or -1. `cmp` is invoked as cmp(record_at_mid, key, udata).
typedef int (*BinsearchCmpFn)(const void* record, const void* key, void* udata);

static inline int binsearch_records(const void* base, int count, size_t stride,
                                    const void* key, BinsearchCmpFn cmp, void* udata) {
    const char* arr = (const char*)base;
    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        int mid = lo + ((hi - lo) >> 1);
        int c = cmp(arr + (size_t)mid * stride, key, udata);
        if (c == 0) return mid;
        if (c < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

// Range-containment binary search. Records form a sequence of disjoint sorted
// intervals; this finds the record whose interval contains `key`.
//
// Mechanically identical to binsearch_records — the difference is purely the
// contract of `range_cmp`, which is:
//    -1 if the record's interval lies entirely before `key`  (search higher)
//     0 if `key` lies within the record's interval           (hit; return index)
//    +1 if the record's interval lies entirely after `key`   (search lower)
//
// Returns the record index (0..count-1) on hit, -1 if `key` falls in a gap or
// beyond the table.
//
// Typical use cases:
//   - JIT debug-info: map native address → function (each func has [start,end))
//   - regex character classes: map codepoint → property (each entry has [first,last])
//   - LaTeX symbol tables, Unicode tables, etc.
static inline int binsearch_range(const void* base, int count, size_t stride,
                                  const void* key, BinsearchCmpFn range_cmp,
                                  void* udata) {
    return binsearch_records(base, count, stride, key, range_cmp, udata);
}

#ifdef __cplusplus
}
#endif

#endif
