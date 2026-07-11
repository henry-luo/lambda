#pragma once

#include "checked_math.hpp"
#include "mempool.h"
#include "memtrack.h"
#include <stddef.h>

namespace lam {

template <typename T>
inline bool mem_grow_array(T** data, int* capacity, int min_capacity,
                           int initial_capacity, MemCategory category) {
    if (!data || !capacity || initial_capacity <= 0) return false;
    if (*capacity >= min_capacity) return true;

    int new_capacity = *capacity > 0 ? *capacity : initial_capacity;
    while (new_capacity < min_capacity) {
        int doubled = new_capacity * 2;
        if (doubled <= new_capacity) return false;
        new_capacity = doubled;
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
        int doubled = new_capacity * 2;
        if (doubled <= new_capacity) return false;
        new_capacity = doubled;
    }

    size_t bytes;
    if (!checked_mul((size_t)new_capacity, sizeof(T), &bytes)) return false;

    T* grown = (T*)pool_realloc(pool, *data, bytes);
    if (!grown) return false;

    *data = grown;
    *capacity = new_capacity;
    return true;
}

} // namespace lam
