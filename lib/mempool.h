#ifndef MEMPOOL_H
#define MEMPOOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

/**
 * Grouped memory ownership facade backed by the hardened memtrack substrate.
 * The legacy Pool API remains for bulk-owner call sites while the implementation
 * uses explicit memtrack-owned blocks.
 */

/**
 * Opaque grouped owner representing a set of memtrack allocations.
 */
typedef struct Pool Pool;

/**
 * Create a new memory pool (arena)
 * @return Pointer to new pool, or NULL on failure
 */
Pool* pool_create(void);

/**
 * Destroy a memory pool and free all associated memory
 * @param pool Pool to destroy
 */
void pool_destroy(Pool* pool);

/**
 * Drain a pool: release all allocated data but keep the Pool struct alive.
 * The pool is invalidated (further allocations return NULL).
 * Use this when the Pool struct must outlive its data (e.g., leaked gc_heaps
 * still hold a pointer to the Pool).
 * @param pool Pool to drain
 */
void pool_drain(Pool* pool);

/**
 * Reset a memory pool: free all allocated data but keep the pool alive.
 * The owner remains valid so future allocations can reuse the same group.
 * @param pool Pool to reset
 */
void pool_reset(Pool* pool);

/**
 * Allocate memory from a specific pool
 * @param pool Pool to allocate from
 * @param size Size in bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure
 */
void* pool_alloc(Pool* pool, size_t size);

/**
 * Allocate zeroed memory from a specific pool
 * @param pool Pool to allocate from
 * @param size Size in bytes to allocate and zero
 * @return Pointer to allocated zeroed memory, or NULL on failure
 */
void* pool_calloc(Pool* pool, size_t size);

/**
 * Free memory allocated by pool_alloc or pool_calloc
 * @param pool Pool the memory was allocated from
 * @param ptr Pointer to memory to free
 */
void pool_free(Pool* pool, void* ptr);

/**
 * Reallocate memory from a specific pool
 * @param pool Pool to reallocate from
 * @param ptr Pointer to existing memory (can be NULL)
 * @param size New size in bytes
 * @return Pointer to reallocated memory, or NULL on failure
 */
void* pool_realloc(Pool* pool, void* ptr, size_t size);

/**
 * Duplicate a string in a pool
 * @param pool Pool to allocate from
 * @param str String to duplicate
 * @return Pointer to duplicated string, or NULL on failure
 */
char* pool_strdup(Pool* pool, const char* str);

/**
 * Clean up the compatibility pool layer (currently a no-op).
 */
void mempool_cleanup(void);

/**
 * Get pool identifier (debug helper)
 */
unsigned int pool_get_id(Pool* pool);

/**
 * Get diagnostic stats: cumulative bytes/count ever allocated from this pool.
 * The counters include both live and cumulative allocation totals.
 */
void pool_get_stats(Pool* pool, size_t* alloc_bytes, size_t* alloc_count);

typedef struct PoolStats {
    size_t reserved_bytes;
    size_t live_bytes;
    size_t high_water_live_bytes;
    size_t cumulative_bytes;
    size_t allocation_count;
    size_t free_count;
} PoolStats;

/** Return the allocator-owned size of one live allocation. */
size_t pool_allocation_size(Pool* pool, void* ptr);

/** Copy detailed pool counters used by memory-domain attribution. */
void pool_get_detailed_stats(Pool* pool, PoolStats* out);

/**
 * Memory-context registration node accessors (opaque void* to avoid a hard
 * dependency on mem_context.h). Used by the allocator factory (mem_factory.c).
 */
void* pool_get_mem_node(Pool* pool);
void  pool_set_mem_node(Pool* pool, void* node);

/** Set the memtrack category used by subsequent allocations in this pool. */
void  pool_set_mem_category(Pool* pool, int category);

/**
 * Install a hook called by pool_destroy/pool_drain to release a registered
 * pool's mem_node. Set by the allocator factory; keeps mempool decoupled from
 * mem_context. NULL by default (no-op).
 */
void pool_set_node_release_hook(void (*fn)(void* node));

/**
 * Get memory-context stats for this pool.
 * @param reserved    bytes reserved by the owner (sum of live memtrack blocks)
 * @param in_use      best-effort live bytes (alloc_bytes)
 * @param alloc_count cumulative allocation calls
 * Any out-param may be NULL.
 */
void pool_get_mem_stats(Pool* pool, size_t* reserved, size_t* in_use,
                        size_t* alloc_count);

#ifdef __cplusplus
}
#endif

#endif // MEMPOOL_H
