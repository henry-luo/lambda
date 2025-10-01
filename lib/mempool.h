#ifndef MEMPOOL_H
#define MEMPOOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

/**
 * Simple Memory Pool - Basic jemalloc wrapper
 */

/**
 * Allocate memory using jemalloc
 * @param size Size in bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure
 */
void* pool_alloc(size_t size);

/**
 * Allocate zeroed memory using jemalloc
 * @param n Number of elements
 * @param size Size of each element in bytes
 * @return Pointer to allocated zeroed memory, or NULL on failure
 */
void* pool_calloc(size_t n, size_t size);

/**
 * Free memory allocated by pool_alloc or pool_calloc
 * @param ptr Pointer to memory to free
 */
void pool_free(void* ptr);

#ifdef __cplusplus
}
#endif

#endif // MEMPOOL_H
