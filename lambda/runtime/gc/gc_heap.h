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
#include "../../../lib/mempool.h"
#include "gc_object_zone.h"
#include "gc_data_zone.h"

// GC header flags
#define GC_FLAG_FREED    0x01   // object has been freed / returned to free list
#define GC_FLAG_LARGE    0x08   // large object allocated via malloc (not pool)
#define GC_FLAG_BUMP     0x10   // object allocated from bump block, not object zone

// Debug/stress fill used after a dead object's external payloads are finalized.
#define GC_FREED_POISON_BYTE 0xDD

// GC generation tags (stored in gc_flags bits 1-2)
#define GC_GEN_NURSERY   0x00   // generation 0: nursery object
#define GC_GEN_TENURED   0x02   // generation 1: tenured (survived a collection)
#define GC_GEN_MASK      0x06   // bits 1-2 for generation

// Internal GC-only allocation tag. It is deliberately outside the public
// TypeId/Item tag space because a JS environment is addressed as a raw Item[].
#define GC_TYPE_JS_ENV   0x100

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
 * The gc_heap calls this when data zone exceeds threshold. Scanner-capable
 * compatibility builds also gather native-stack bounds before collection;
 * precise scanner-free builds publish roots through exact slots only.
 */
typedef void (*gc_collect_callback_t)(void);

/**
 * Callback type for tracing VMap entries during GC mark phase.
 * Called with: (vmap_data_ptr, gc_heap, gc_mark_item_fn)
 * The callback should iterate all Item keys/values in the VMap data
 * and call gc_mark_item(gc, item) for each.
 */
typedef struct gc_heap gc_heap_t;  // forward declaration for function pointer types
typedef void (*gc_vmap_trace_fn)(void* data, gc_heap_t* gc);

/**
 * Callback type for destroying VMap backing data during GC sweep.
 * Called with the VMap object and its opaque data pointer (HashMapData*).
 */
typedef void (*gc_vmap_destroy_fn)(void* obj, void* data);

/**
 * Callback types for tracing/finalizing heap-owned LambdaError payloads.
 */
typedef void (*gc_error_trace_fn)(void* data, gc_heap_t* gc);
typedef void (*gc_error_destroy_fn)(void* data);
typedef void (*gc_js_native_trace_fn)(void* data, gc_heap_t* gc);
typedef int (*gc_js_function_trace_fn)(void* data, gc_heap_t* gc);
// Releases refcounted/native payloads embedded in otherwise zone-owned objects.
// Implementations must clear the released field so repeated teardown is safe.
typedef void (*gc_external_destroy_fn)(void* data, uint16_t type_tag);
typedef void (*gc_weak_clear_fn)(uint64_t* slot, void* context);

/**
 * Small per-cleanup native-pointer set used by runtime finalizers that own
 * external memory through GC-managed wrapper objects.
 */
typedef struct gc_native_seen {
    void** data;
    int length;
    int capacity;
} gc_native_seen_t;

void gc_native_seen_init(gc_native_seen_t* seen);
void gc_native_seen_dispose(gc_native_seen_t* seen);
int  gc_native_seen_seen_or_add(gc_native_seen_t* seen, void* ptr);

// Default data zone usage threshold (75% of block size) to trigger GC
#define GC_DATA_ZONE_THRESHOLD (GC_DATA_ZONE_BLOCK_SIZE * 3 / 4)

// Initial registered root slot capacity; grows dynamically as needed.
#define GC_ROOT_SLOTS_INITIAL 256

// Root range: contiguous array of Items to scan as GC roots.
// Used for JS closure environment arrays (pool-allocated, invisible to GC).
typedef struct gc_root_range {
    uint64_t* base;
    int count;
} gc_root_range_t;

typedef struct gc_weak_slot {
    uint64_t* slot;
    gc_weak_clear_fn on_clear;
    void* context;
} gc_weak_slot_t;

/**
 * Root-discovery mode for one GC heap. Compatibility is the production
 * default while precise rooting is being migrated. Shadow verification keeps
 * the conservative scan but reports objects that precise roots did not reach.
 */
typedef enum gc_root_mode {
    GC_ROOT_MODE_COMPATIBILITY = 0,
    GC_ROOT_MODE_SHADOW_VERIFY,
    GC_ROOT_MODE_PRECISE_ONLY,
} gc_root_mode_t;

// Release builds that cannot admit C2MIR omit the native-stack scanner.
// Debug builds retain it solely for shadow verification; C2MIR builds retain
// it for their sticky compatibility contexts.
#if defined(LAMBDA_C2MIR) || !defined(NDEBUG)
#define GC_CONSERVATIVE_SCAN_AVAILABLE 1
#else
#define GC_CONSERVATIVE_SCAN_AVAILABLE 0
#endif

typedef struct gc_root_stats {
    size_t precise_root_count;
    size_t conservative_candidate_words;
    size_t conservative_new_objects;
    size_t conservative_scan_bytes;
    size_t conservative_skipped_poisoned_words;
} gc_root_stats_t;

typedef struct gc_root_type_stat {
    uint16_t type_tag;
    size_t count;
} gc_root_type_stat_t;

/**
 * GCHeap - manages all GC-tracked allocations.
 *
 * Uses dual-zone architecture:
 *   - object_zone: non-moving size-class allocator for object structs
 *   - data_zone:   bump-pointer allocator for variable-size data buffers
 *   - tenured_data: data zone for long-lived data that survived collection
 *   - pool: rpmalloc pool for large object fallback and bulk cleanup
 *
 * Hot-path allocation uses a bump-pointer region (bump_cursor/bump_end)
 * for fast sequential allocation. Objects are still linked into all_objects
 * for GC sweep. Dead objects go to the object_zone free lists for reuse.
 */

// Bump block metadata for tracking allocated bump regions
typedef struct gc_bump_block {
    uint8_t* base;                  // block memory start
    size_t size;                    // block size in bytes
    struct gc_bump_block* next;     // next (older) block in chain
} gc_bump_block_t;

// Initial and growth sizes for bump-pointer regions
#define GC_BUMP_BLOCK_INITIAL_SIZE  (4 * 1024 * 1024)   // 4 MB
#define GC_BUMP_BLOCK_MAX_SIZE      (64 * 1024 * 1024)   // 64 MB cap

typedef struct gc_heap {
    Pool* pool;                     // underlying rpmalloc memory pool
    gc_header_t* all_objects;       // linked list head (most recent allocation first)
    gc_header_t** large_objects;    // sorted malloc-backed objects for exact pointer lookup
    int large_object_count;
    int large_object_capacity;
    uint8_t* bump_cursor;           // bump-pointer allocation cursor (hot path)
    uint8_t* bump_end;              // end of current bump block
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
    uint64_t** root_slots;
    int root_slot_count;
    int root_slot_capacity;

    // Weak slots participate in identity caches without marking their Item.
    // Dead referents are cleared after tracing and before object sweep.
    gc_weak_slot_t* weak_slots;
    int weak_slot_count;
    int weak_slot_capacity;

    // Collection trigger
    size_t gc_threshold;            // data zone bytes that trigger auto-collection
    int collecting;                 // re-entrancy guard (1 = GC in progress)
    gc_collect_callback_t collect_callback;  // called when threshold exceeded

    // Collection statistics
    size_t collections;             // number of GC collections performed
    size_t bytes_collected;         // total bytes reclaimed across all collections

    // Root ranges: dynamically growing array of (base, count) pairs.
    // Each range is a pool-allocated Item[] array (e.g., JS closure envs).
    gc_root_range_t* root_ranges;
    int root_range_count;
    int root_range_capacity;

    // Root migration diagnostics. The mode is sticky for the heap unless an
    // explicit test/debug API changes it before executing guest code.
    gc_root_mode_t root_mode;
    gc_root_stats_t last_root_stats;
    gc_root_type_stat_t* root_type_stats;
    int root_type_stat_count;
    int root_type_stat_capacity;

    // Deterministic forced-GC schedule for safepoint stress. Zero disables it.
    size_t force_collect_interval;
    size_t force_allocation_count;
    size_t forced_collection_count;
    uint64_t force_random_seed;
    uint64_t force_random_state;
    uint32_t force_random_one_in;

    // Opt-in rooting stress: overwrite dead object payloads before their
    // storage is recycled so missed precise roots fail deterministically.
    int poison_freed;

#ifndef NDEBUG
    // Debug proof for helpers classified NO_GC. Public GC allocation and
    // collection entry points must reject activity while this scope is live.
    int no_gc_scope_depth;
#endif

    // VMap tracing/finalization callbacks (set by runtime, called from GC)
    gc_vmap_trace_fn vmap_trace;    // traces Item keys/values in VMap's HashMap
    gc_vmap_destroy_fn vmap_destroy; // frees VMap native payload/backing data
    gc_error_trace_fn error_trace;   // traces heap-owned LambdaError cause chain
    gc_error_destroy_fn error_destroy; // frees LambdaError external payload fields
    gc_js_native_trace_fn js_native_trace; // traces native payload edges on JS Map wrappers
    gc_js_function_trace_fn js_function_trace; // recognizes and traces GC-owned JsFunction objects
    gc_external_destroy_fn external_destroy; // frees generic external payloads at sweep/teardown

    // Bump-pointer block chain (for cleanup and ownership registration)
    gc_bump_block_t* bump_blocks;   // linked list of allocated bump regions

    void* mem_node;                 // MemContext registration node (NULL if untracked)
} gc_heap_t;

/**
 * Create a new GC heap with its own memory pool.
 * @return new GCHeap, or NULL on failure
 */
gc_heap_t* gc_heap_create(void);

/**
 * Create a new GC heap reusing an existing memory pool.
 * The pool should have been reset (pool_reset) before reuse.
 * Ownership of the pool transfers to the gc_heap.
 * @param pool existing pool to reuse (must not be NULL)
 * @return new GCHeap, or NULL on failure
 */
gc_heap_t* gc_heap_create_with_pool(Pool* pool);

/**
 * Destroy the GC heap and all its allocations.
 * Calls pool_destroy to bulk-free all pool-allocated memory.
 * @param gc heap to destroy
 */
void gc_heap_destroy(gc_heap_t* gc);

/**
 * Install a hook called by gc_heap_destroy to release a registered heap's
 * mem_node. Set by the allocator factory; NULL by default (no-op).
 */
void gc_heap_set_node_release_hook(void (*fn)(void* node));

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
 * Allocate zeroed memory from a known size class (skips class_index lookup).
 * Used by the JIT when the allocation size and class are compile-time constants.
 * @param gc       heap to allocate from
 * @param size     user data size in bytes
 * @param type_tag TypeId for tracking
 * @param cls      pre-computed size class index (0-6)
 * @return pointer to zeroed user data, or NULL on failure
 */
void* gc_heap_calloc_class(gc_heap_t* gc, size_t size, uint16_t type_tag, int cls);

/**
 * Allocate from the bump-pointer region. Fast path for JIT-compiled code.
 * Uses the bump cursor for sequential allocation. Falls back to free list
 * and new block allocation when the current block is exhausted.
 * Objects are linked into all_objects for GC sweep compatibility.
 *
 * @param gc            heap to allocate from
 * @param slot_size     total slot size (sizeof(gc_header_t) + SIZE_CLASSES[cls])
 * @param alloc_size    original user data size (stored in header for class lookup)
 * @param type_tag      TypeId for tracking
 * @param cls           pre-computed size class index (0-6)
 * @return pointer to zeroed user data, or NULL on failure
 */
void* gc_heap_bump_alloc(gc_heap_t* gc, size_t slot_size, size_t alloc_size,
                          uint16_t type_tag, int cls);

// Migration instrumentation for the no-scalar-cell invariant.  Counts every
// scalar-tag allocation observed at the GC object-allocation choke points.
uint64_t gc_scalar_tag_allocation_count(uint16_t type_tag);

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
int gc_try_register_root(gc_heap_t* gc, uint64_t* slot);

/**
 * Unregister an external root slot.
 * @param gc   heap to unregister from
 * @param slot pointer previously registered with gc_register_root
 */
void gc_unregister_root(gc_heap_t* gc, uint64_t* slot);

void gc_no_gc_scope_begin(gc_heap_t* gc);
void gc_no_gc_scope_end(gc_heap_t* gc);

/** Register/unregister a non-marking Item slot. The clear callback runs after
 * the slot is zeroed, while the dead object's header is still available to GC. */
void gc_register_weak(gc_heap_t* gc, uint64_t* slot,
                      gc_weak_clear_fn on_clear, void* context);
void gc_unregister_weak(gc_heap_t* gc, uint64_t* slot);

/**
 * Register a contiguous range of Items as GC roots.
 * Used for JS closure environment arrays that are pool-allocated.
 * @param gc    heap to register with
 * @param base  pointer to first Item in the range
 * @param count number of Items in the range
 */
void gc_register_root_range(gc_heap_t* gc, uint64_t* base, int count);

/**
 * Unregister a contiguous array of Items previously registered as GC roots.
 * @param gc    heap to unregister from
 * @param base  base pointer previously registered with gc_register_root_range
 */
void gc_unregister_root_range(gc_heap_t* gc, uint64_t* base);

/**
 * Configure and inspect collector root discovery. Precise-only mode is a
 * verification facility until every active execution tier has exact roots.
 */
void gc_set_root_mode(gc_heap_t* gc, gc_root_mode_t mode);
gc_root_mode_t gc_get_root_mode(const gc_heap_t* gc);
const char* gc_root_mode_name(gc_root_mode_t mode);
int gc_conservative_scan_available(void);
const gc_root_stats_t* gc_get_last_root_stats(const gc_heap_t* gc);
size_t gc_get_last_conservative_type_count(const gc_heap_t* gc,
                                           uint16_t type_tag);

/**
 * Force the existing collection callback before every Nth public GC
 * allocation. This is disabled by default and intended for deterministic
 * rooting stress at allocator boundaries that are already MAY_GC.
 */
void gc_set_force_collect_interval(gc_heap_t* gc, size_t interval);
void gc_set_force_collect_random(gc_heap_t* gc, uint64_t seed,
                                 uint32_t one_in);
size_t gc_get_forced_collection_count(const gc_heap_t* gc);

/**
 * Enable deterministic dead-object payload poisoning for rooting stress.
 * Disabled by default; recycled object-zone slots are zeroed on allocation.
 */
void gc_set_poison_freed(gc_heap_t* gc, int enabled);
int gc_get_poison_freed(const gc_heap_t* gc);

/**
 * Run a full garbage collection cycle:
 *   1. Mark: scan registered and explicit roots; compatibility builds also
 *      scan the native stack
 *   2. Compact: copy surviving data zone buffers to tenured data zone
 *   3. Sweep: free unmarked objects back to free lists
 *   4. Reset nursery data zone
 *
 * @param gc            heap to collect
 * @param extra_roots   array of additional root Items (may be NULL)
 * @param extra_count   number of additional root Items
 */
#if GC_CONSERVATIVE_SCAN_AVAILABLE
void gc_collect(gc_heap_t* gc, uint64_t* extra_roots, int extra_count,
                uintptr_t stack_base, uintptr_t stack_current);
void gc_collect_with_root_region(gc_heap_t* gc, uint64_t* extra_roots,
                int extra_count, uintptr_t stack_base, uintptr_t stack_current,
                uint64_t* root_base, int64_t root_count);
#else
void gc_collect(gc_heap_t* gc, uint64_t* extra_roots, int extra_count);
void gc_collect_with_root_region(gc_heap_t* gc, uint64_t* extra_roots,
                int extra_count, uint64_t* root_base, int64_t root_count);
#endif

/**
 * Mark a single Item as reachable (pushes to mark stack if GC-managed).
 * Public for use by external root scanners.
 */
void gc_mark_item(gc_heap_t* gc, uint64_t item);
void gc_mark_object_ptr(gc_heap_t* gc, void* ptr);

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
