#pragma once
// Fallible-allocation helpers — the Type-1 ("returns NULL on failure") allocation path.
// Every size computation is overflow-checked; every result is [[nodiscard]] so a caller
// cannot silently dereference a NULL. The complementary infallible (NonNull) arena path
// lives in ownership.hpp.
// See vibe/Memory_Safety_Template3.md §3.3 and §3.7.

#include <stddef.h>
#include <stdlib.h>   // realloc, free

#include "mempool.h"  // Pool, pool_calloc
#include "checked_math.hpp"

namespace lam {

// n elements of sz bytes, zero-initialized from a pool. Returns NULL on overflow OR OOM.
template<class T>
[[nodiscard]] inline T* checked_pool_array(Pool* p, size_t n, size_t sz = sizeof(T)) {
    size_t total;
    if (!checked_mul(n, sz, &total)) return NULL;
    return (T*)pool_calloc(p, total);
}

// header(sizeof T) + extra payload bytes, from a pool. Returns NULL on overflow OR OOM.
template<class T>
[[nodiscard]] inline T* checked_pool_sized(Pool* p, size_t extra) {
    size_t total;
    if (!checked_add(sizeof(T), extra, &total)) return NULL;
    return (T*)pool_calloc(p, total);
}

// realloc *slot to hold n elements of sz bytes. On overflow or OOM returns false and
// leaves *slot UNCHANGED (no leak of the original, no dangling). On success *slot is updated.
template<class T>
[[nodiscard]] inline bool checked_realloc(T** slot, size_t n, size_t sz = sizeof(T)) {
    size_t total;
    if (!checked_mul(n, sz, &total)) return false;
    T* fresh = (T*)realloc(*slot, total);
    if (!fresh) return false;
    *slot = fresh;
    return true;
}

// Plain malloc with an overflow-checked n*sz size. Returns NULL on overflow OR OOM.
[[nodiscard]] inline void* checked_malloc(size_t n, size_t sz) {
    size_t total;
    if (!checked_mul(n, sz, &total)) return NULL;
    return malloc(total);
}

} // namespace lam
