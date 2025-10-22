#ifndef MEMPOOL_H
#define MEMPOOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

/**
 * Arena-based Memory Pool - rpmalloc wrapper with heap support
 */

/**
 * Opaque pool structure representing an rpmalloc heap
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
 * Clean up mempool system (optional - called automatically at process exit)
 * This can be called to explicitly shut down rpmalloc if needed
 */
void mempool_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // MEMPOOL_H
