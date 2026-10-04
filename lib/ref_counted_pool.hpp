#pragma once

#include "hashmap.h"
#include "ref_count.h"
#include <stdint.h>

template <typename PoolT>
static inline PoolT* ref_counted_pool_retain(PoolT* pool) {
    if (!pool) return nullptr;
    // immutable parent pools can be retained by multiple compiler workers.
    return ref_count_retain(&pool->ref_count) ? pool : nullptr;
}

// true when this release dropped the last reference
template <typename PoolT>
static inline bool ref_counted_pool_release_last(PoolT* pool) {
    return ref_count_release(&pool->ref_count) == REF_COUNT_LAST;
}

template <typename PoolT, typename ParentReleaseFn>
static inline void ref_counted_pool_finalize_zero(PoolT* pool,
                                                  void (*node_release)(void*),
                                                  ParentReleaseFn parent_release,
                                                  struct hashmap* entries) {
    if (!pool || ref_count_get(&pool->ref_count) != 0) return;
    if (pool->mem_node && node_release) {
        node_release(pool->mem_node);
        pool->mem_node = nullptr;
    }
    if (pool->parent) {
        parent_release(pool->parent);
    }
    if (entries) {
        hashmap_free(entries);
    }
}
