/**
 * gc_heap.h - GC Heap Allocator with Object Tracking
 *
 * All GC-managed objects are allocated through gc_heap_alloc/gc_heap_calloc,
 * which prepend a 16-byte GCHeader and link into an intrusive linked list.
 *
 * Phase 5 Architecture: Dual-Zone Non-Moving Mark-and-Sweep
 *
 *   Object Zone: Size-class free-list allocator for fixed-size object structs.
 *                Objects are NEVER moved — Item tagged pointers remain stable.
 *
 *   Data Zone:   Bump-pointer allocator for variable-size data buffers
 *                (items[], data, closure_env). Compactable on GC.
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
#include "gc_object_zone.h"
#include "gc_data_zone.h"

// GC header flags
#define GC_FLAG_FREED    0x01   // object has been freed / returned to free list

// GC generation tags (stored in gc_flags bits 1-2)
#define GC_GEN_NURSERY   0x00   // generation 0: nursery object
#define GC_GEN_TENURED   0x02   // generation 1: tenured (survived a collection)
#define GC_GEN_MASK      0x06   // bits 1-2 for generation

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
    uint8_t  gc_flags;        // 1 byte  - GC_FLAG_FREED, generation, etc.
    uint8_t  marked;          // 1 byte  - mark bit for mark-sweep
    uint32_t alloc_size;      // 4 bytes - user allocation size (not including header)
} gc_header_t;                // 16 bytes total, user pointer is 16-byte aligned

/**
 * Callback type for triggering GC collection from allocation paths.
 * The gc_heap calls this when data zone exceeds threshold.
 * The callback should gather roots (stack scan, context) and call gc_collect().
 */
typedef void (*gc_collect_callback_t)(void);

// Default data zone usage threshold (75% of block size) to trigger GC
#define GC_DATA_ZONE_THRESHOLD (GC_DATA_ZONE_BLOCK_SIZE * 3 / 4)

// Maximum number of registered root slots
#define GC_MAX_ROOT_SLOTS 256

/**
 * GCHeap - manages all GC-tracked allocations.
 *
 * Uses dual-zone architecture:
 *   - object_zone: non-moving size-class allocator for object structs
 *   - data_zone:   bump-pointer allocator for variable-size data buffers
 *   - tenured_data: data zone for long-lived data that survived collection
 *   - pool: rpmalloc pool for large object fallback and bulk cleanup
 */
typedef struct gc_heap {
    Pool* pool;                     // underlying rpmalloc memory pool
    gc_header_t* all_objects;       // linked list head (most recent allocation first)
    gc_object_zone_t* object_zone;  // non-moving object struct allocator
    gc_data_zone_t* data_zone;      // bump-pointer data buffer allocator (nursery)
    gc_data_zone_t* tenured_data;   // data zone for survivors (promoted on GC)
    size_t total_allocated;         // total bytes allocated (including headers)
    size_t object_count;            // number of live objects

    // Mark stack for GC traversal
    gc_header_t** mark_stack;       // stack of gray objects to trace
    int mark_top;                   // current mark stack position
    int mark_capacity;              // mark stack capacity

    // Registered root slots: external pointers to locations holding Items.
    // Each slot is a uint64_t* that points to a memory location containing
    // a boxed Item value (e.g., BSS globals, context->result).
    uint64_t* root_slots[GC_MAX_ROOT_SLOTS];
    int root_slot_count;

    // Collection trigger
    size_t gc_threshold;            // data zone bytes that trigger auto-collection
    int collecting;                 // re-entrancy guard (1 = GC in progress)
    gc_collect_callback_t collect_callback;  // called when threshold exceeded

    // Collection statistics
    size_t collections;             // number of GC collections performed
    size_t bytes_collected;         // total bytes reclaimed across all collections
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
 * Uses the object zone (size-class free list) for objects up to 256 bytes.
 * Falls back to pool_alloc for larger objects.
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
 * Free a GC-managed object. Sets GC_FLAG_FREED on the header.
 * If the object is in the object zone, returns it to the size-class free list.
 * @param gc  heap the object was allocated from
 * @param ptr user data pointer (as returned by gc_heap_alloc/gc_heap_calloc)
 */
void gc_heap_pool_free(gc_heap_t* gc, void* ptr);

/**
 * Allocate a variable-size data buffer from the data zone.
 * Used for items[], data, closure_env — NOT tracked by GCHeader.
 * @param gc    heap (provides data zone)
 * @param size  bytes to allocate
 * @return pointer to zeroed memory, or NULL on failure
 */
void* gc_data_alloc(gc_heap_t* gc, size_t size);

/**
 * Check if a pointer is managed by this GC heap (object zone, data zone, or large objects).
 * Returns 0 for arena-allocated, name-pool, and const-pool pointers.
 */
int gc_is_managed(gc_heap_t* gc, void* ptr);

/**
 * Check if a pointer is in the nursery data zone (for compaction decisions).
 */
int gc_is_nursery_data(gc_heap_t* gc, void* ptr);

// ============================================================================
// Mark-and-Sweep Collection API
// ============================================================================

/**
 * Register an external root slot with the GC.
 * A root slot is a pointer to a memory location that holds a boxed Item (uint64_t).
 * The GC will scan these during collection to mark reachable objects.
 * Used for BSS globals (module-level let bindings) and context->result.
 *
 * @param gc   heap to register with
 * @param slot pointer to uint64_t that holds a boxed Item
 */
void gc_register_root(gc_heap_t* gc, uint64_t* slot);

/**
 * Unregister an external root slot.
 * @param gc   heap to unregister from
 * @param slot pointer previously registered with gc_register_root
 */
void gc_unregister_root(gc_heap_t* gc, uint64_t* slot);

/**
 * Run a full garbage collection cycle:
 *   1. Mark: scan registered roots, stack, and explicit roots; trace reachable
 *   2. Compact: copy surviving data zone buffers to tenured data zone
 *   3. Sweep: free unmarked objects back to free lists
 *   4. Reset nursery data zone
 *
 * @param gc            heap to collect
 * @param extra_roots   array of additional root Items (may be NULL)
 * @param extra_count   number of additional root Items
 * @param stack_base    high address of the C stack (thread's stack base)
 * @param stack_current current stack pointer (low address)
 */
void gc_collect(gc_heap_t* gc, uint64_t* extra_roots, int extra_count,
                uintptr_t stack_base, uintptr_t stack_current);

/**
 * Mark a single Item as reachable (pushes to mark stack if GC-managed).
 * Public for use by external root scanners.
 */
void gc_mark_item(gc_heap_t* gc, uint64_t item);

/**
 * Check if the data zone has exceeded the GC trigger threshold.
 * Used by allocation paths to decide when to trigger collection.
 * @return 1 if data zone usage exceeds threshold, 0 otherwise
 */
int gc_should_collect(gc_heap_t* gc);

/**
 * Set the callback that gc_data_alloc invokes when data zone exceeds threshold.
 * The callback should call heap_gc_collect() or equivalent.
 * @param gc       heap
 * @param callback function to call for auto-collection (NULL to disable)
 */
void gc_set_collect_callback(gc_heap_t* gc, gc_collect_callback_t callback);

// ============================================================================
// Utility
// ============================================================================

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
