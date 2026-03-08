/**
 * gc_object_zone.h - Size-Class Free-List Allocator for Fixed-Size Object Structs
 *
 * The Object Zone holds all fixed-size GC-managed object structs (List, Map, String,
 * Function, numerics, etc.). Objects in this zone are NEVER moved — Item tagged
 * pointers reference them directly and must remain stable.
 *
 * Allocation uses size-class segregated free lists for fast O(1) allocation with
 * minimal fragmentation. Each size class has a linked list of free slots and a
 * chain of slabs (contiguous arrays of same-size slots).
 *
 * Memory layout per slot:
 *   [gc_header_t 16 bytes][user data ...]
 *                          ^ pointer returned to caller
 *
 * Size classes: 16, 32, 48, 64, 96, 128, 256 bytes (user data size, not including header).
 * Objects larger than 256 bytes use direct pool allocation (large object path).
 */
#ifndef GC_OBJECT_ZONE_H
#define GC_OBJECT_ZONE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "mempool.h"

// Forward declaration — full definition in gc_heap.h
typedef struct gc_header gc_header_t;

// Size classes for object zone (user data sizes, header is additional)
// Designed to cover Lambda's struct sizes with minimal internal fragmentation:
//   16B: int64_t(8), double(8), DateTime(8), Decimal(~16)
//   32B: String(4+chars), Symbol(~16+chars), Range(~26), Map(~24), VMap(~18)
//   48B: List/Array(~34), ArrayInt/Int64/Float(~34), Function(~42)
//   64B: Element(~58), Object with padding
//   96B: larger strings, small closure envs
//  128B: medium strings
//  256B: larger variable-size objects
#define GC_NUM_SIZE_CLASSES 7
#define GC_LARGE_OBJECT_THRESHOLD 256

// Slab: a contiguous block of same-size slots for one size class.
// Slots are allocated sequentially (next_fresh), and freed slots are
// pushed to the per-class free list for reuse.
typedef struct gc_object_slab {
    uint8_t* base;              // slab memory start
    size_t slot_size;           // bytes per slot (header + user data, aligned)
    size_t slot_count;          // total slots in this slab
    size_t next_fresh;          // next un-used slot index (sequential allocation)
    struct gc_object_slab* next;// next slab in chain for this size class
} gc_object_slab_t;

// Slab range entry for fast ownership lookup via binary search
typedef struct gc_slab_range {
    uint8_t* base;          // slab memory start
    uint8_t* end;           // slab memory end (exclusive)
} gc_slab_range_t;

// Maximum number of slab ranges (grows dynamically if needed)
#define GC_INITIAL_RANGE_CAPACITY 64

// Object Zone: manages all non-moving object allocations
typedef struct gc_object_zone {
    // Per-size-class free lists: each entry is a gc_header_t* whose user data
    // area is available for reuse. The header's `next` field links free slots.
    gc_header_t* free_lists[GC_NUM_SIZE_CLASSES];

    // Per-size-class slab chains
    gc_object_slab_t* slabs[GC_NUM_SIZE_CLASSES];

    // Underlying memory pool for slab allocation
    Pool* pool;

    // Statistics
    size_t total_slots_allocated;   // total slots ever allocated
    size_t total_slots_freed;       // total slots returned to free list
    size_t slab_count;              // total number of slabs allocated

    // Fast ownership lookup: sorted array of slab ranges + min/max bounds
    gc_slab_range_t* slab_ranges;   // sorted by base address
    size_t range_count;             // number of entries in slab_ranges
    size_t range_capacity;          // allocated capacity of slab_ranges
    uint8_t* min_addr;              // minimum slab base address (fast rejection)
    uint8_t* max_addr;              // maximum slab end address (fast rejection)
} gc_object_zone_t;

/**
 * Create a new object zone backed by the given pool.
 */
gc_object_zone_t* gc_object_zone_create(Pool* pool);

/**
 * Destroy the object zone. Does NOT free pool memory (pool_destroy handles that).
 * Only frees the zone struct itself.
 */
void gc_object_zone_destroy(gc_object_zone_t* oz);

/**
 * Allocate a slot from the object zone. Returns pointer to user data
 * (after the gc_header_t). The header is initialized with type_tag and linked
 * into the GC's all_objects list by the caller.
 *
 * @param oz        object zone
 * @param size      user data size in bytes (will be rounded up to size class)
 * @param type_tag  TypeId for the allocation
 * @param all_objects pointer to gc_heap's all_objects list head (for linking)
 * @return pointer to zeroed user data, or NULL on failure
 */
void* gc_object_zone_alloc(gc_object_zone_t* oz, size_t size, uint16_t type_tag,
                           gc_header_t** all_objects);

/**
 * Allocate a slot from a known size class (skips class_index lookup).
 * The caller must pre-compute the class index at compile time.
 *
 * @param oz          object zone
 * @param cls         pre-computed size class index (0-6)
 * @param size        user data size in bytes (stored in header for sweep)
 * @param type_tag    TypeId for the allocation
 * @param all_objects pointer to gc_heap's all_objects list head (for linking)
 * @return pointer to zeroed user data, or NULL on failure
 */
void* gc_object_zone_alloc_class(gc_object_zone_t* oz, int cls, size_t size,
                                  uint16_t type_tag, gc_header_t** all_objects);

/**
 * Return a slot to the free list for its size class.
 * The slot's gc_header_t is reused as a free-list link node.
 *
 * @param oz     object zone
 * @param header pointer to the gc_header_t of the slot to free
 */
void gc_object_zone_free(gc_object_zone_t* oz, gc_header_t* header);

/**
 * Check if a pointer falls within any object zone slab.
 */
int gc_object_zone_owns(gc_object_zone_t* oz, void* ptr);

/**
 * Get the size class index for a given user data size.
 * Returns -1 if size exceeds the large object threshold.
 */
int gc_object_zone_class_index(size_t size);

/**
 * Get the actual allocation size (user data) for a size class index.
 */
size_t gc_object_zone_class_size(int cls);

#ifdef __cplusplus
}
#endif

#endif // GC_OBJECT_ZONE_H
