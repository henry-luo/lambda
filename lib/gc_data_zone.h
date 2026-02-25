/**
 * gc_data_zone.h - Bump-Pointer Allocator for Variable-Size Data Buffers
 *
 * The Data Zone holds variable-size data buffers associated with GC-managed objects:
 *   - List/Array items[] arrays
 *   - Map/Element/Object data buffers (packed field data)
 *   - Element items[] arrays (children)
 *   - Function closure_env structs
 *   - ArrayInt/ArrayInt64/ArrayFloat items[] arrays
 *
 * Data Zone buffers are referenced by exactly ONE pointer inside their owning
 * object struct (e.g., list->items, map->data, fn->closure_env). This means
 * moving a data buffer requires updating exactly one pointer — the struct field.
 *
 * Allocation is O(1) bump-pointer (cursor increment).
 * No individual deallocation — dead space is reclaimed during GC:
 *   1. Surviving data chunks are copied to the tenured data zone
 *   2. The owning struct's pointer is updated (single fixup per survivor)
 *   3. The nursery data zone is fully reset (cursor back to base)
 *
 * When a list/array grows, the old items[] buffer is abandoned (wasted until GC).
 * This is the standard trade-off for bump allocators.
 */
#ifndef GC_DATA_ZONE_H
#define GC_DATA_ZONE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "mempool.h"

// Default data zone block size: 256 KB
#define GC_DATA_ZONE_BLOCK_SIZE (256 * 1024)

// Alignment for all data zone allocations (cache-line friendly)
#define GC_DATA_ZONE_ALIGN 16

// A single contiguous block of bump-allocated memory
typedef struct gc_data_block {
    uint8_t* base;              // block start
    uint8_t* cursor;            // next free byte (bump pointer)
    uint8_t* limit;             // block end (base + capacity)
    struct gc_data_block* next; // next block in chain (overflow)
} gc_data_block_t;

// Data Zone: manages variable-size data buffer allocations
typedef struct gc_data_zone {
    gc_data_block_t* head;      // first block
    gc_data_block_t* current;   // current allocation block
    size_t block_size;          // default bytes per block
    Pool* pool;                 // underlying memory pool (for block allocation)

    // Statistics
    size_t total_allocated;     // total bytes allocated (user data)
    size_t total_blocks;        // number of blocks allocated
} gc_data_zone_t;

/**
 * Create a new data zone backed by the given pool.
 * @param pool       underlying memory pool for block allocation
 * @param block_size bytes per block (0 = use default GC_DATA_ZONE_BLOCK_SIZE)
 */
gc_data_zone_t* gc_data_zone_create(Pool* pool, size_t block_size);

/**
 * Destroy the data zone. Does NOT free pool memory (pool_destroy handles that).
 * Only frees the zone/block metadata structs.
 */
void gc_data_zone_destroy(gc_data_zone_t* dz);

/**
 * Allocate memory from the data zone. Returns a pointer to zeroed memory.
 * Allocation is O(1) bump-pointer. If the current block is full, a new block
 * is allocated from the pool.
 *
 * @param dz    data zone
 * @param size  bytes to allocate (will be aligned up to GC_DATA_ZONE_ALIGN)
 * @return pointer to zeroed memory, or NULL on failure
 */
void* gc_data_zone_alloc(gc_data_zone_t* dz, size_t size);

/**
 * Reset the data zone: set all block cursors back to base.
 * Called after GC compaction — all surviving data has been copied elsewhere.
 * Blocks are retained for reuse (not freed).
 */
void gc_data_zone_reset(gc_data_zone_t* dz);

/**
 * Check if a pointer falls within any block of this data zone.
 */
int gc_data_zone_owns(gc_data_zone_t* dz, void* ptr);

/**
 * Get the total bytes currently used (cursor - base across all blocks).
 */
size_t gc_data_zone_used(gc_data_zone_t* dz);

/**
 * Allocate a copy of existing data into the data zone.
 * Copies `size` bytes from `src` into a new data zone allocation.
 * Used during GC compaction to copy surviving data.
 *
 * @param dz    data zone to allocate into
 * @param src   source data to copy
 * @param size  bytes to copy
 * @return pointer to the copy in the data zone, or NULL on failure
 */
void* gc_data_zone_copy(gc_data_zone_t* dz, const void* src, size_t size);

#ifdef __cplusplus
}
#endif

#endif // GC_DATA_ZONE_H
