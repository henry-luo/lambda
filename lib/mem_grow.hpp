#pragma once

#include "checked_math.hpp"
#include "arena.h"
#include "mempool.h"
#include "memtrack.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

namespace lam {

template <typename T>
inline bool mem_grow_array(T** data, int* capacity, int min_capacity,
                           int initial_capacity, MemCategory category) {
    if (!data || !capacity || initial_capacity <= 0) return false;
    if (*capacity >= min_capacity) return true;

    int new_capacity = *capacity > 0 ? *capacity : initial_capacity;
    while (new_capacity < min_capacity) {
        if (new_capacity > INT_MAX / 2) return false;
        new_capacity *= 2;
    }

    size_t bytes;
    if (!checked_mul((size_t)new_capacity, sizeof(T), &bytes)) return false;

    T* grown = (T*)mem_realloc(*data, bytes, category);
    if (!grown) return false;

    *data = grown;
    *capacity = new_capacity;
    return true;
}

template <typename T>
inline bool pool_grow_array(Pool* pool, T** data, int* capacity,
                            int min_capacity, int initial_capacity) {
    if (!pool || !data || !capacity || initial_capacity <= 0) return false;
    if (*capacity >= min_capacity) return true;

    int new_capacity = *capacity > 0 ? *capacity : initial_capacity;
    while (new_capacity < min_capacity) {
        if (new_capacity > INT_MAX / 2) return false;
        new_capacity *= 2;
    }

    size_t bytes;
    if (!checked_mul((size_t)new_capacity, sizeof(T), &bytes)) return false;

    T* grown = (T*)pool_realloc(pool, *data, bytes);
    if (!grown) return false;

    *data = grown;
    *capacity = new_capacity;
    return true;
}

// `size_t` owners avoid lossy capacity narrowing; arena storage is copied into
// a fresh extent because Arena deliberately has no realloc operation.
template <typename T>
inline bool mem_grow_array(T** data, size_t* capacity, size_t min_capacity,
                           size_t initial_capacity, MemCategory category) {
    if (!data || !capacity || initial_capacity == 0) return false;
    if (*capacity >= min_capacity) return true;
    size_t next = *capacity ? *capacity : initial_capacity;
    while (next < min_capacity) {
        if (next > (size_t)-1 / 2) { next = min_capacity; break; }
        next *= 2;
    }
    size_t bytes;
    if (!checked_mul(next, sizeof(T), &bytes)) return false;
    T* grown = (T*)mem_realloc(*data, bytes, category);
    if (!grown) return false;
    *data = grown;
    *capacity = next;
    return true;
}

template <typename T>
inline bool arena_grow_array(Arena* arena, T** data, size_t* capacity,
                             size_t count, size_t min_capacity,
                             size_t initial_capacity) {
    if (!arena || !data || !capacity || count > *capacity || initial_capacity == 0) return false;
    if (*capacity >= min_capacity) return true;
    size_t next = *capacity ? *capacity : initial_capacity;
    while (next < min_capacity) {
        if (next > (size_t)-1 / 2) { next = min_capacity; break; }
        next *= 2;
    }
    size_t bytes;
    if (!checked_mul(next, sizeof(T), &bytes)) return false;
    T* grown = (T*)arena_alloc(arena, bytes);
    if (!grown) return false;
    if (*data && count > 0) memcpy(grown, *data, count * sizeof(T));
    *data = grown;
    *capacity = next;
    return true;
}

} // namespace lam
