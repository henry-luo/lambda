#ifndef ARENA_H
#define ARENA_H
#include "lambda_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * Chunk-Based Arena Allocator - Fast sequential allocation with bulk deallocation
 *
 * Owns its chunks directly through the hardened memtrack substrate. Provides:
 * - O(1) bump-pointer allocation
 * - Adaptive chunk sizing (4KB -> 64KB)
 * - Zero per-allocation metadata overhead
 * - Bulk reset/clear operations
 *
 * @warning ARENA_NOT_THREAD_SAFE - Arena is NOT thread-safe.
 * Each Arena instance must be owned and accessed by a single thread only.
 * Concurrent access from multiple threads requires external synchronization
 * (e.g., a mutex). ScratchArena is designed for single-threaded scoped use.
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

typedef struct ArenaStats {
    size_t backing_bytes;       // always zero: Arena owns its blocks directly
    size_t committed_bytes;     // aggregate chunk data capacity
    size_t bump_used_bytes;     // bump extent including retained interior blocks
    size_t active_bytes;        // bump_used_bytes minus retired_bytes
    size_t retired_bytes;       // blocks retained for reuse (arena_retire)
    size_t waste_bytes;         // unused chunk tails
    size_t overhead_bytes;      // arena/chunk headers and allocator rounding
    size_t high_water_active_bytes;
    uint64_t allocation_count;
    uint64_t rewind_count;      // tail-region rewinds (arena_rewind)
    uint64_t retire_count;      // arena_retire calls
    uint64_t reuse_hits;        // allocations served from retained blocks
    uint64_t split_count;
    uint64_t coalesce_count;
    uint64_t bump_back_count;   // retained spans that rejoined the tail
    uint64_t fresh_chunk_count;
    uint64_t fresh_growth_bytes;
    uint64_t reset_count;
    uint64_t clear_count;
    uint32_t active_scope_count;
} ArenaStats;

/**
 * Create a new arena with custom chunk sizes
 * @param initial_chunk_size Starting chunk size in bytes
 * @param max_chunk_size Maximum chunk size limit in bytes
 * @return Pointer to new arena, or NULL on failure
 */
Arena* arena_create(size_t initial_chunk_size, size_t max_chunk_size);

/**
 * Create a new arena with default settings (4KB initial, 64KB max, adaptive)
 * @return Pointer to new arena, or NULL on failure
 */
Arena* arena_create_default();

/**
 * Destroy an arena and release all directly-owned chunks
 * @param arena Arena to destroy
 */
LAMBDA_LIB_API void arena_destroy(Arena* arena);

/**
 * Allocate memory from arena with default alignment
 * @param arena Arena to allocate from
 * @param size Size in bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure
 */
LAMBDA_LIB_API void* arena_alloc(Arena* arena, size_t size);

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
LAMBDA_LIB_API void* arena_calloc(Arena* arena, size_t size);

/**
 * Duplicate exactly len bytes in an arena and append a null terminator
 */
char* arena_dup_n(Arena* arena, const char* data, size_t len);

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
 * Resets the first chunk for reuse, frees all other directly-owned chunks.
 * Chunk size is reset to initial size.
 * Use when you want to reclaim memory between uses.
 * @param arena Arena to clear
 */
void arena_clear(Arena* arena);

/**
 * Get total bytes allocated across all owned chunks
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

/** Copy the allocator's detailed logical/backing counters. */
void arena_get_stats(Arena* arena, ArenaStats* out);

// An arena releases memory only in batch: whole-arena reset/clear/destroy, or
// rewinding its tail to a mark. A block is never individually freed or
// discarded; it may only be retained for reuse within the same arena
// (arena_retire), as DOM node retirement does (D4.1.4v5).

// A scratch arena registers as the sole owner of its backing arena's tail, so
// reset/clear cannot invalidate its pointers and no other allocation can land
// inside the region it rewinds. While registered, arena_alloc asserts; the
// owner allocates through arena_scope_alloc.
void arena_scope_enter(Arena* arena);
void arena_scope_leave(Arena* arena);
uint32_t arena_active_scope_count(Arena* arena);
void* arena_scope_alloc(Arena* arena, size_t size, size_t alignment);

// Tail position for the mark/rewind batch-free variant.
typedef struct ArenaMark {
    void* chunk;        // opaque chunk at the mark
    size_t used;        // bytes used in that chunk at the mark
    size_t total_used;  // arena-wide bytes used at the mark
} ArenaMark;

ArenaMark arena_mark(Arena* arena);
// Frees everything allocated after `mark`; later chunks are kept for reuse.
// Not valid on an arena holding retired blocks.
void arena_rewind(Arena* arena, ArenaMark mark);

/**
 * Retain a block on the arena's retired list for reuse by later allocations
 * from the same arena. The memory stays arena-owned until reset/destroy; a
 * retained span that reaches the tail rejoins it. Not allowed while a scratch
 * scope owns the arena.
 * @param size the size the block was allocated with
 */
void arena_retire(Arena* arena, void* ptr, size_t size);

/**
 * Check if a pointer belongs to this arena
 * Useful for determining if data needs to be copied during deep copy operations
 * @param arena Arena to check ownership
 * @param ptr Pointer to check
 * @return true if ptr was allocated from this arena, false otherwise
 */
bool arena_owns(Arena* arena, const void* ptr);

/**
 * Memory-context registration node accessors (opaque void* to avoid a hard
 * dependency on mem_context.h). Used by the allocator factory (mem_factory.c).
 */
void* arena_get_mem_node(Arena* arena);
void  arena_set_mem_node(Arena* arena, void* node);
void  arena_set_mem_category(Arena* arena, int category);

/**
 * Install a hook called by arena_destroy to release a registered arena's
 * mem_node. Set by the allocator factory; NULL by default (no-op).
 */
void arena_set_node_release_hook(void (*fn)(void* node));

#ifdef __cplusplus
}
#endif

#endif // ARENA_H
