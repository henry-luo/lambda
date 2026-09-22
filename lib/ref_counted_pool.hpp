#pragma once

#include "hashmap.h"
#include <stdint.h>

template <typename PoolT>
static inline PoolT* ref_counted_pool_retain(PoolT* pool) {
    if (!pool) return nullptr;
    // immutable parent pools can be retained by multiple compiler workers.
    __atomic_add_fetch(&pool->ref_count, 1u, __ATOMIC_RELAXED);
    return pool;
}

template <typename PoolT>
static inline uint32_t ref_counted_pool_release_count(PoolT* pool) {
    return __atomic_sub_fetch(&pool->ref_count, 1u, __ATOMIC_ACQ_REL);
}

template <typename PoolT, typename ParentReleaseFn>
static inline void ref_counted_pool_finalize_zero(PoolT* pool,
                                                  void (*node_release)(void*),
                                                  ParentReleaseFn parent_release,
                                                  struct hashmap* entries) {
    if (!pool || __atomic_load_n(&pool->ref_count, __ATOMIC_ACQUIRE) != 0) return;
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
