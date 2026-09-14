// grow_capacity.h - overflow-safe geometric capacity selection.
#ifndef LIB_GROW_CAPACITY_H
#define LIB_GROW_CAPACITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline bool lib_grow_capacity(size_t current_capacity, size_t min_capacity,
                                     size_t initial_capacity, size_t* out_capacity) {
    if (!out_capacity || initial_capacity == 0) return false;
    if (current_capacity >= min_capacity) {
        *out_capacity = current_capacity;
        return true;
    }

    size_t capacity = current_capacity ? current_capacity : initial_capacity;
    while (capacity < min_capacity) {
        if (capacity > SIZE_MAX / 2u) {
            capacity = min_capacity;
            break;
        }
        capacity *= 2u;
    }
    *out_capacity = capacity;
    return true;
}

#endif
