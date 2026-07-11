#pragma once

#include "hashmap.h"
#include <stdint.h>

template <typename PoolT>
static inline PoolT* ref_counted_pool_retain(PoolT* pool) {
    if (!pool) return nullptr;
    pool->ref_count++;
    return pool;
}

template <typename PoolT, typename ParentReleaseFn>
static inline void ref_counted_pool_finalize_zero(PoolT* pool,
                                                  void (*node_release)(void*),
                                                  ParentReleaseFn parent_release,
                                                  struct hashmap* entries) {
    if (!pool || pool->ref_count != 0) return;
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
