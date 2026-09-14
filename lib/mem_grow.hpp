#pragma once

#include "checked_math.hpp"
#include "grow_capacity.h"
#include "arena.h"
#include "mempool.h"
#include "memtrack.h"
#include "scratch_arena.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

namespace lam {

inline bool grow_capacity(int current_capacity, int min_capacity,
                          int initial_capacity, int* out_capacity) {
    if (current_capacity < 0 || min_capacity < 0 || initial_capacity <= 0) return false;
    size_t capacity = 0;
    if (!lib_grow_capacity((size_t)current_capacity, (size_t)min_capacity,
                           (size_t)initial_capacity, &capacity) || capacity > INT_MAX) {
        return false;
    }
    *out_capacity = (int)capacity;
    return true;
}

inline bool grow_capacity(size_t current_capacity, size_t min_capacity,
                          size_t initial_capacity, size_t* out_capacity) {
    return lib_grow_capacity(current_capacity, min_capacity, initial_capacity, out_capacity);
}

template <typename T>
inline bool mem_grow_array(T** data, int* capacity, int min_capacity,
                           int initial_capacity, MemCategory category) {
    if (!data || !capacity || initial_capacity <= 0) return false;
    if (*capacity >= min_capacity) return true;

    int new_capacity = 0;
    if (!grow_capacity(*capacity, min_capacity, initial_capacity, &new_capacity)) return false;

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

    int new_capacity = 0;
    if (!grow_capacity(*capacity, min_capacity, initial_capacity, &new_capacity)) return false;

    size_t bytes;
    if (!checked_mul((size_t)new_capacity, sizeof(T), &bytes)) return false;

    T* grown = (T*)pool_realloc(pool, *data, bytes);
    if (!grown) return false;

    *data = grown;
    *capacity = new_capacity;
    return true;
}

// Pool parsing structures may retain pointers into an old generation. Grow
// these by copying to a fresh pool allocation instead of pool_realloc().
template <typename T>
inline bool pool_copy_grow_array(Pool* pool, T** data, size_t* capacity,
                                 size_t count, size_t min_capacity,
                                 size_t initial_capacity, bool zeroed) {
    if (!pool || !data || !capacity || count > *capacity || initial_capacity == 0) return false;
    if (*capacity >= min_capacity) return true;
    size_t next = 0;
    if (!grow_capacity(*capacity, min_capacity, initial_capacity, &next)) return false;
    size_t bytes;
    if (!checked_mul(next, sizeof(T), &bytes)) return false;
    T* grown = (T*)(zeroed ? pool_calloc(pool, bytes) : pool_alloc(pool, bytes));
    if (!grown) return false;
    if (*data && count > 0) memcpy(grown, *data, count * sizeof(T));
    *data = grown;
    *capacity = next;
    return true;
}

template <typename T>
inline bool pool_copy_grow_array(Pool* pool, T** data, int* capacity,
                                 int count, int min_capacity,
                                 int initial_capacity, bool zeroed) {
    if (!pool || !data || !capacity || count < 0 || count > *capacity) return false;
    if (*capacity >= min_capacity) return true;
    int next = 0;
    if (!grow_capacity(*capacity, min_capacity, initial_capacity, &next)) return false;
    size_t bytes;
    if (!checked_mul((size_t)next, sizeof(T), &bytes)) return false;
    T* grown = (T*)(zeroed ? pool_calloc(pool, bytes) : pool_alloc(pool, bytes));
    if (!grown) return false;
    if (*data && count > 0) memcpy(grown, *data, (size_t)count * sizeof(T));
    *data = grown;
    *capacity = next;
    return true;
}

// `size_t` owners avoid lossy capacity narrowing; arena storage is copied into
// a fresh extent because Arena deliberately has no realloc operation.
template <typename T>
inline bool mem_grow_array(T** data, size_t* capacity, size_t min_capacity,
                           size_t initial_capacity, MemCategory category) {
    if (!data || !capacity || initial_capacity == 0) return false;
    if (*capacity >= min_capacity) return true;
    size_t next = 0;
    if (!grow_capacity(*capacity, min_capacity, initial_capacity, &next)) return false;
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
    size_t next = 0;
    if (!grow_capacity(*capacity, min_capacity, initial_capacity, &next)) return false;
    size_t bytes;
    if (!checked_mul(next, sizeof(T), &bytes)) return false;
    T* grown = (T*)arena_alloc(arena, bytes);
    if (!grown) return false;
    if (*data && count > 0) memcpy(grown, *data, count * sizeof(T));
    *data = grown;
    *capacity = next;
    return true;
}

template <typename T>
inline bool scratch_grow_array(ScratchArena* scratch, T** data, int* capacity,
                               int count, int min_capacity, int initial_capacity,
                               int maximum_capacity = INT_MAX) {
    if (!scratch || !data || !capacity || count < 0 || count > *capacity ||
        min_capacity < 0 || min_capacity > maximum_capacity) return false;
    if (*capacity >= min_capacity) return true;
    int next = 0;
    if (!grow_capacity(*capacity, min_capacity, initial_capacity, &next)) return false;
    if (next > maximum_capacity) next = maximum_capacity;
    if (next < min_capacity) return false;
    size_t bytes;
    if (!checked_mul((size_t)next, sizeof(T), &bytes)) return false;
    T* grown = (T*)scratch_calloc(scratch, bytes);
    if (!grown) return false;
    if (*data && count > 0) memcpy(grown, *data, (size_t)count * sizeof(T));
    *data = grown;
    *capacity = next;
    return true;
}

} // namespace lam
