#ifndef ARENA_H
#define ARENA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdbool.h>
#include "mempool.h"

/**
 * Chunk-Based Arena Allocator - Fast sequential allocation with bulk deallocation
 *
 * Built on top of the Pool system for memory management. Provides:
 * - O(1) bump-pointer allocation
 * - Adaptive chunk sizing (4KB -> 64KB)
 * - Zero per-allocation metadata overhead
 * - Bulk reset/clear operations
 */

// Default chunk size configurations
#define ARENA_INITIAL_CHUNK_SIZE  (4 * 1024)    // 4KB - start small
#define ARENA_MAX_CHUNK_SIZE      (64 * 1024)   // 64KB - efficient maximum
#define ARENA_DEFAULT_ALIGNMENT   16            // 16-byte SIMD alignment

// Suggested sizes for specific use cases
#define ARENA_SMALL_CHUNK_SIZE    (4 * 1024)    // 4KB  - for parsers/small work
#define ARENA_MEDIUM_CHUNK_SIZE   (16 * 1024)   // 16KB - general purpose
#define ARENA_LARGE_CHUNK_SIZE    (64 * 1024)   // 64KB - for formatters/large work

/**
 * Opaque arena structure - use accessor functions
 */
typedef struct Arena Arena;

/**
 * Create a new arena with custom chunk sizes
 * @param pool Underlying memory pool for chunk allocation
 * @param initial_chunk_size Starting chunk size in bytes
 * @param max_chunk_size Maximum chunk size limit in bytes
 * @return Pointer to new arena, or NULL on failure
 */
Arena* arena_create(Pool* pool, size_t initial_chunk_size, size_t max_chunk_size);

/**
 * Create a new arena with default settings (4KB initial, 64KB max, adaptive)
 * @param pool Underlying memory pool for chunk allocation
 * @return Pointer to new arena, or NULL on failure
 */
Arena* arena_create_default(Pool* pool);

/**
 * Destroy an arena and free all chunks back to the pool
 * @param arena Arena to destroy
 */
void arena_destroy(Arena* arena);

/**
 * Allocate memory from arena with default alignment
 * @param arena Arena to allocate from
 * @param size Size in bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure
 */
void* arena_alloc(Arena* arena, size_t size);

/**
 * Allocate memory from arena with custom alignment
 * @param arena Arena to allocate from
 * @param size Size in bytes to allocate
 * @param alignment Required alignment (must be power of 2)
 * @return Pointer to allocated memory, or NULL on failure
 */
void* arena_alloc_aligned(Arena* arena, size_t size, size_t alignment);

/**
 * Allocate zero-initialized memory from arena
 * @param arena Arena to allocate from
 * @param size Size in bytes to allocate and zero
 * @return Pointer to zeroed memory, or NULL on failure
 */
void* arena_calloc(Arena* arena, size_t size);

/**
 * Duplicate a string in arena
 * @param arena Arena to allocate from
 * @param str String to duplicate (null-terminated)
 * @return Pointer to duplicated string, or NULL on failure
 */
char* arena_strdup(Arena* arena, const char* str);

/**
 * Duplicate a string with length limit in arena
 * @param arena Arena to allocate from
 * @param str String to duplicate
 * @param n Maximum number of characters to copy
 * @return Pointer to duplicated string, or NULL on failure
 */
char* arena_strndup(Arena* arena, const char* str, size_t n);

/**
 * Create a formatted string in arena
 * @param arena Arena to allocate from
 * @param fmt Format string (printf-style)
 * @param ... Format arguments
 * @return Pointer to formatted string, or NULL on failure
 */
char* arena_sprintf(Arena* arena, const char* fmt, ...);

/**
 * Reset arena to beginning, keeping all chunks for reuse
 * All chunk 'used' counters are reset to 0, but chunks remain allocated.
 * Current chunk size is preserved (stays at grown size).
 * Fast operation - no memory allocation/deallocation.
 * @param arena Arena to reset
 */
void arena_reset(Arena* arena);

/**
 * Clear arena, freeing all chunks except the first
 * Resets the first chunk for reuse, frees all other chunks back to pool.
 * Chunk size is reset to initial size.
 * Use when you want to reclaim memory between uses.
 * @param arena Arena to clear
 */
void arena_clear(Arena* arena);

/**
 * Get total bytes allocated from pool (all chunks)
 * @param arena Arena to query
 * @return Total bytes allocated across all chunks
 */
size_t arena_total_allocated(Arena* arena);

/**
 * Get total bytes actually used by allocations
 * @param arena Arena to query
 * @return Total bytes used by user allocations
 */
size_t arena_total_used(Arena* arena);

/**
 * Get wasted bytes (fragmentation at end of chunks)
 * @param arena Arena to query
 * @return Bytes allocated but not used (waste)
 */
size_t arena_waste(Arena* arena);

/**
 * Get number of chunks currently allocated
 * @param arena Arena to query
 * @return Number of chunks
 */
size_t arena_chunk_count(Arena* arena);

/**
 * Check if a pointer belongs to this arena
 * Useful for determining if data needs to be copied during deep copy operations
 * @param arena Arena to check ownership
 * @param ptr Pointer to check
 * @return true if ptr was allocated from this arena, false otherwise
 */
bool arena_owns(Arena* arena, const void* ptr);

/**
 * Reallocate memory in arena with free-list support
 * Similar to realloc() but works within arena memory management
 * - If ptr is NULL, allocates new memory (like arena_alloc)
 * - If new_size is 0, frees memory to free-list (like arena_free)
 * - If shrinking, excess space added to free-list for reuse
 * - If growing at end of chunk, extends in-place if possible
 * - Otherwise allocates new, copies old data, and frees old block
 * @param arena Arena to reallocate from
 * @param ptr Pointer to existing allocation (or NULL for new allocation)
 * @param old_size Size of existing allocation in bytes (or 0 if ptr is NULL)
 * @param new_size Desired new size in bytes
 * @return Pointer to reallocated memory, or NULL on failure
 */
void* arena_realloc(Arena* arena, void* ptr, size_t old_size, size_t new_size);

/**
 * Free memory back to arena's free-list for reuse
 * Memory is not returned to pool, but added to internal free-list
 * for future arena_alloc() or arena_realloc() calls
 * @param arena Arena that owns the memory
 * @param ptr Pointer to free (must be from this arena)
 * @param size Size of allocation to free in bytes
 */
void arena_free(Arena* arena, void* ptr, size_t size);

#ifdef __cplusplus
}
#endif

#endif // ARENA_H
