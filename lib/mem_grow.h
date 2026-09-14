// mem_grow.h - C-compatible tracked-array growth with checked sizing.
#ifndef LIB_MEM_GROW_H
#define LIB_MEM_GROW_H

#include <stdbool.h>
#include <stddef.h>
#include <limits.h>

#include "grow_capacity.h"
#include "memtrack.h"

// Grow a memtrack-backed array in place. The caller owns element initialization:
// newly reserved slots retain mem_realloc's uninitialized-byte semantics.
static inline bool mem_grow_array_raw_limit(void** data, size_t element_size,
        size_t* capacity, size_t min_capacity, size_t initial_capacity,
        size_t maximum_capacity, MemCategory category) {
    if (!data || !capacity || element_size == 0 || initial_capacity == 0) return false;
    if (*capacity >= min_capacity) return true;

    size_t next_capacity = 0;
    if (!lib_grow_capacity(*capacity, min_capacity, initial_capacity, &next_capacity) ||
        next_capacity > maximum_capacity || next_capacity > SIZE_MAX / element_size) {
        return false;
    }

    void* grown = mem_realloc(*data, next_capacity * element_size, category);
    if (!grown) return false;
    *data = grown;
    *capacity = next_capacity;
    return true;
}

static inline bool mem_grow_array_raw(void** data, size_t element_size,
                                      size_t* capacity, size_t min_capacity,
                                      size_t initial_capacity, MemCategory category) {
    return mem_grow_array_raw_limit(data, element_size, capacity, min_capacity,
        initial_capacity, SIZE_MAX, category);
}

// Keep legacy int capacities checked at the C boundary without making callers
// narrow a successful allocation after it has already replaced their storage.
static inline bool mem_grow_array_raw_int(void** data, size_t element_size,
                                          int* capacity, int min_capacity,
                                          int initial_capacity, MemCategory category) {
    if (!data || !capacity || element_size == 0 || *capacity < 0 ||
        min_capacity < 0 || initial_capacity <= 0) return false;
    if (*capacity >= min_capacity) return true;

    size_t size_capacity = (size_t)*capacity;
    if (!mem_grow_array_raw_limit(data, element_size, &size_capacity,
            (size_t)min_capacity, (size_t)initial_capacity, INT_MAX,
            category)) return false;
    *capacity = (int)size_capacity;
    return true;
}

#endif
