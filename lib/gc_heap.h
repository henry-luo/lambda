/**
 * gc_heap.h - GC Heap Allocator with Object Tracking
 *
 * Replaces the entries ArrayList-based tracking with an intrusive linked list
 * of GCHeaders prepended to each managed allocation.
 *
 * All GC-managed objects (containers, strings, symbols, decimals, functions)
 * are allocated through gc_heap_alloc/gc_heap_calloc, which prepend a 16-byte
 * GCHeader. The header links objects into a singly-linked list (newest first)
 * for frame-based cleanup and future mark-sweep GC.
 *
 * Arena-allocated objects (from input parsers) are NOT tracked by GCHeap
 * and have no GCHeader.
 */
#ifndef GC_HEAP_H
#define GC_HEAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "mempool.h"

// GC header flags
#define GC_FLAG_FREED    0x01   // object has been freed (by recursive free_container)

// initial frame stack capacity
#define GC_FRAME_STACK_INITIAL 64

/**
 * GCHeader - 16 bytes prepended to every GC-managed allocation.
 *
 * Memory layout:
 *   [GCHeader 16 bytes][user data ...]
 *                       ^ pointer returned to caller
 *
 * The linked list is ordered newest-first (most recent allocation at head).
 */
typedef struct gc_header {
    struct gc_header* next;   // 8 bytes - next older object in all_objects list
    uint16_t type_tag;        // 2 bytes - TypeId of the allocation
    uint8_t  gc_flags;        // 1 byte  - GC_FLAG_FREED, etc.
    uint8_t  marked;          // 1 byte  - mark bit for future mark-sweep
    uint32_t alloc_size;      // 4 bytes - user allocation size (not including header)
} gc_header_t;                // 16 bytes total, user pointer is 16-byte aligned

/**
 * GCHeap - manages all GC-tracked allocations via intrusive linked list.
 *
 * Replaces the old Heap.entries ArrayList with a linked list of GCHeaders.
 * Frame management uses a frame_stack of saved list head pointers instead
 * of sentinel entries in the ArrayList.
 */
typedef struct gc_heap {
    Pool* pool;                 // underlying rpmalloc memory pool
    gc_header_t* all_objects;   // linked list head (most recent allocation first)
    gc_header_t** frame_stack;  // stack of saved list head pointers (frame markers)
    int frame_depth;            // current frame nesting depth
    int frame_capacity;         // capacity of frame_stack array
    size_t total_allocated;     // total bytes allocated (including headers)
    size_t object_count;        // number of live objects
} gc_heap_t;

/**
 * Create a new GC heap with its own memory pool.
 * @return new GCHeap, or NULL on failure
 */
gc_heap_t* gc_heap_create(void);

/**
 * Destroy the GC heap and all its allocations.
 * Calls pool_destroy to bulk-free all pool-allocated memory.
 * @param gc heap to destroy
 */
void gc_heap_destroy(gc_heap_t* gc);

/**
 * Allocate memory from the GC heap with a prepended GCHeader.
 * The returned pointer is AFTER the header (user data area).
 * @param gc     heap to allocate from
 * @param size   user data size in bytes
 * @param type_tag TypeId for tracking (e.g., LMD_TYPE_STRING, LMD_TYPE_ARRAY)
 * @return pointer to user data, or NULL on failure
 */
void* gc_heap_alloc(gc_heap_t* gc, size_t size, uint16_t type_tag);

/**
 * Allocate zeroed memory from the GC heap with a prepended GCHeader.
 * @param gc     heap to allocate from
 * @param size   user data size in bytes
 * @param type_tag TypeId for tracking
 * @return pointer to zeroed user data, or NULL on failure
 */
void* gc_heap_calloc(gc_heap_t* gc, size_t size, uint16_t type_tag);

/**
 * Free a GC-managed object. Accounts for the prepended GCHeader.
 * Also sets GC_FLAG_FREED on the header so the frame_end walk can skip it.
 * @param gc  heap the object was allocated from
 * @param ptr user data pointer (as returned by gc_heap_alloc/gc_heap_calloc)
 */
void gc_heap_pool_free(gc_heap_t* gc, void* ptr);

/**
 * Push a frame marker. Saves the current all_objects head.
 * The frame boundary determines which objects belong to the current scope.
 * @param gc heap
 */
void gc_heap_frame_push(gc_heap_t* gc);

/**
 * Pop the most recent frame marker.
 * @param gc heap
 * @return the saved all_objects head from the matching frame_push
 */
gc_header_t* gc_heap_frame_pop(gc_heap_t* gc);

/**
 * Get the GCHeader for a user data pointer.
 * @param ptr user data pointer (as returned by gc_heap_alloc)
 * @return pointer to the GCHeader, or NULL if ptr is NULL
 */
static inline gc_header_t* gc_get_header(void* ptr) {
    if (!ptr) return NULL;
    return ((gc_header_t*)ptr) - 1;
}

#ifdef __cplusplus
}
#endif

#endif // GC_HEAP_H
