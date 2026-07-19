/**
 * gc_heap.c - GC Heap Allocator Implementation
 *
 * Phase 5: Dual-zone non-moving mark-and-sweep garbage collector.
 *
 * Object structs are allocated from the Object Zone (size-class free lists).
 * Variable-size data buffers are allocated from the Data Zone (bump pointer).
 * The mark-sweep collector reclaims dead objects without moving them.
 * Data zone buffers of surviving objects are compacted to tenured data zone.
 */
#include "gc_heap.h"
#include "../log.h"
#include "../memtrack.h"
#include "../hashmap.h"
#include "../../lambda/lambda.h"
#include "../../lambda/js/js_exec_profile_weak.h"
#include <stdlib.h>
#include <string.h>

// Hosts may batch native-lifetime work released by weak callbacks. The weak
// default keeps the collector independent when no host integration is linked.
__attribute__((weak)) void gc_weak_slots_processed(void) {}

#ifdef LAMBDA_JS_EXEC_PROFILE
#define GC_PROFILE_ENTER(label) js_weak_profile_property_set_branch_enter(label)
#define GC_PROFILE_LEAVE(label, token) js_weak_profile_property_set_branch_leave(label, token)
#define GC_PROFILE_COUNT(label, count) js_weak_profile_property_set_branch_add_count(label, count)
#else
#define GC_PROFILE_ENTER(label) 0
#define GC_PROFILE_LEAVE(label, token) ((void)(token))
#define GC_PROFILE_COUNT(label, count) ((void)(count))
#endif

// Hook to release a memory-context node when a registered heap is destroyed.
// Installed by the allocator factory (lambda/mem_factory_rt.cpp); NULL when unused.
static void (*g_gc_heap_node_release)(void*) = NULL;
void gc_heap_set_node_release_hook(void (*fn)(void*)) { g_gc_heap_node_release = fn; }

// Initial mark stack capacity (grows if needed)
#define GC_MARK_STACK_INITIAL 4096

static int gc_bump_block_owns_exact(gc_heap_t* gc, void* ptr);

static void gc_assert_allocation_allowed(gc_heap_t* gc, const char* site) {
#ifndef NDEBUG
    if (gc && gc->no_gc_scope_depth > 0) {
        // A NO_GC helper that allocates invalidates generated safepoint
        // liveness, so fail at the first forbidden operation.
        log_error("gc-no-gc-scope: forbidden allocation/collection at %s depth=%d",
            site ? site : "unknown", gc->no_gc_scope_depth);
        abort();
    }
#else
    (void)gc;
    (void)site;
#endif
}

void gc_native_seen_init(gc_native_seen_t* seen) {
    if (!seen) return;
    seen->data = NULL;
    seen->length = 0;
    seen->capacity = 0;
}

void gc_native_seen_dispose(gc_native_seen_t* seen) {
    if (!seen) return;
    if (seen->data) free(seen->data);
    seen->data = NULL;
    seen->length = 0;
    seen->capacity = 0;
}

int gc_native_seen_seen_or_add(gc_native_seen_t* seen, void* ptr) {
    if (!seen || !ptr) return 1;
    for (int i = 0; i < seen->length; i++) {
        if (seen->data[i] == ptr) return 1;
    }
    if (seen->length >= seen->capacity) {
        int new_capacity = seen->capacity ? seen->capacity * 2 : 32;
        void** new_data = (void**)realloc(seen->data, (size_t)new_capacity * sizeof(void*));
        if (!new_data) return 1;
        seen->data = new_data;
        seen->capacity = new_capacity;
    }
    seen->data[seen->length++] = ptr;
    return 0;
}

static void* gc_header_user_ptr(gc_header_t* header) {
    return header ? (void*)(header + 1) : NULL;
}

static int gc_large_object_find(gc_heap_t* gc, void* ptr) {
    if (!gc || !ptr || gc->large_object_count <= 0) return -1;
    uintptr_t target = (uintptr_t)ptr;
    int lo = 0, hi = gc->large_object_count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        uintptr_t mid_ptr = (uintptr_t)gc_header_user_ptr(gc->large_objects[mid]);
        if (mid_ptr < target) lo = mid + 1;
        else hi = mid;
    }
    if (lo < gc->large_object_count && gc_header_user_ptr(gc->large_objects[lo]) == ptr) {
        return lo;
    }
    return -1;
}

static int gc_large_object_add(gc_heap_t* gc, gc_header_t* header) {
    if (!gc || !header) return 0;
    void* ptr = gc_header_user_ptr(header);
    if (gc_large_object_find(gc, ptr) >= 0) return 1;
    if (gc->large_object_count >= gc->large_object_capacity) {
        int new_cap = gc->large_object_capacity ? gc->large_object_capacity * 2 : 256;
        gc_header_t** new_objects = (gc_header_t**)realloc(gc->large_objects,
            (size_t)new_cap * sizeof(gc_header_t*));
        if (!new_objects) {
            log_error("gc_large_object_add: realloc failed for %d objects", new_cap);
            return 0;
        }
        gc->large_objects = new_objects;
        gc->large_object_capacity = new_cap;
    }

    uintptr_t target = (uintptr_t)ptr;
    int pos = 0;
    while (pos < gc->large_object_count &&
           (uintptr_t)gc_header_user_ptr(gc->large_objects[pos]) < target) {
        pos++;
    }
    if (pos < gc->large_object_count) {
        memmove(&gc->large_objects[pos + 1], &gc->large_objects[pos],
            (size_t)(gc->large_object_count - pos) * sizeof(gc_header_t*));
    }
    gc->large_objects[pos] = header;
    gc->large_object_count++;
    return 1;
}

static void gc_large_object_remove(gc_heap_t* gc, gc_header_t* header) {
    if (!gc || !header || gc->large_object_count <= 0) return;
    int idx = gc_large_object_find(gc, gc_header_user_ptr(header));
    if (idx < 0) return;
    if (idx < gc->large_object_count - 1) {
        memmove(&gc->large_objects[idx], &gc->large_objects[idx + 1],
            (size_t)(gc->large_object_count - idx - 1) * sizeof(gc_header_t*));
    }
    gc->large_object_count--;
}

// ============================================================================
// Bump-Pointer Block Management
// ============================================================================

// Allocate a new bump block from the pool and register its range with the
// object zone for ownership lookup (gc_object_zone_owns binary search).
static gc_bump_block_t* gc_alloc_bump_block(gc_heap_t* gc, size_t block_size) {
    uint8_t* memory = (uint8_t*)pool_alloc(gc->pool, block_size);
    if (!memory) {
        log_error("gc_alloc_bump_block: failed to allocate %zu bytes", block_size);
        return NULL;
    }
    memset(memory, 0, block_size);

    gc_bump_block_t* block = (gc_bump_block_t*)calloc(1, sizeof(gc_bump_block_t));
    if (!block) {
        log_error("gc_alloc_bump_block: failed to allocate block metadata");
        return NULL;
    }
    block->base = memory;
    block->size = block_size;
    block->next = NULL;

    // Register with object zone so gc_object_zone_owns() recognizes bump pointers.
    // This enables GC mark/sweep to find bump-allocated objects.
    // Use the internal register_slab_range function via the public class_size API
    // — actually we need direct access. We'll register via a helper.
    // For now, register the range directly in the object zone's sorted range array.
    if (gc->object_zone) {
        // Reuse the same ownership infrastructure: register the bump block as a "slab range"
        gc_object_zone_t* oz = gc->object_zone;
        uint8_t* end = memory + block_size;

        // update global min/max bounds
        if (!oz->min_addr || memory < oz->min_addr) oz->min_addr = memory;
        if (!oz->max_addr || end > oz->max_addr) oz->max_addr = end;

        // grow array if needed
        if (oz->range_count >= oz->range_capacity) {
            size_t new_cap = oz->range_capacity ? oz->range_capacity * 2 : GC_INITIAL_RANGE_CAPACITY;
            gc_slab_range_t* new_ranges = (gc_slab_range_t*)realloc(oz->slab_ranges,
                new_cap * sizeof(gc_slab_range_t));
            if (new_ranges) {
                oz->slab_ranges = new_ranges;
                oz->range_capacity = new_cap;
            }
        }

        // binary search for insertion point
        size_t lo = 0, hi = oz->range_count;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (oz->slab_ranges[mid].base < memory) lo = mid + 1;
            else hi = mid;
        }
        if (lo < oz->range_count) {
            memmove(&oz->slab_ranges[lo + 1], &oz->slab_ranges[lo],
                    (oz->range_count - lo) * sizeof(gc_slab_range_t));
        }
        oz->slab_ranges[lo].base = memory;
        oz->slab_ranges[lo].end = end;
        oz->range_count++;
    }

    log_debug("gc_alloc_bump_block: allocated %zu byte bump block at %p", block_size, memory);
    return block;
}

// ============================================================================
// Type identification helpers.
// Keep the local suffix used in this C file, but bind to lambda.h's authoritative
// enum values so TypeId reordering cannot desynchronize GC tracing.
// ============================================================================
#define LMD_TYPE_RAW_POINTER_ LMD_TYPE_RAW_POINTER
#define LMD_TYPE_NULL_        LMD_TYPE_NULL
#define LMD_TYPE_BOOL_        LMD_TYPE_BOOL
#define LMD_TYPE_NUM_SIZED_   LMD_TYPE_NUM_SIZED
#define LMD_TYPE_INT_         LMD_TYPE_INT
#define LMD_TYPE_INT64_       LMD_TYPE_INT64
#define LMD_TYPE_UINT64_      LMD_TYPE_UINT64
#define LMD_TYPE_FLOAT_       LMD_TYPE_FLOAT
#define LMD_TYPE_DECIMAL_     LMD_TYPE_DECIMAL
#define LMD_TYPE_DTIME_       LMD_TYPE_DTIME
#define LMD_TYPE_SYMBOL_      LMD_TYPE_SYMBOL
#define LMD_TYPE_STRING_      LMD_TYPE_STRING
#define LMD_TYPE_BINARY_      LMD_TYPE_BINARY
#define LMD_TYPE_PATH_        LMD_TYPE_PATH
#define LMD_TYPE_RANGE_       LMD_TYPE_RANGE
#define LMD_TYPE_ARRAY_NUM_   LMD_TYPE_ARRAY_NUM
#define LMD_TYPE_ARRAY_       LMD_TYPE_ARRAY
#define LMD_TYPE_MAP_         LMD_TYPE_MAP
#define LMD_TYPE_VMAP_        LMD_TYPE_VMAP
#define LMD_TYPE_ELEMENT_     LMD_TYPE_ELEMENT
#define LMD_TYPE_OBJECT_      LMD_TYPE_OBJECT
#define LMD_TYPE_TYPE_        LMD_TYPE_TYPE
#define LMD_TYPE_FUNC_        LMD_TYPE_FUNC
#define GC_TYPE_JS_ENV_       GC_TYPE_JS_ENV
#define LMD_TYPE_ANY_         LMD_TYPE_ANY
#define LMD_TYPE_ERROR_       LMD_TYPE_ERROR
#define LMD_TYPE_UNDEFINED_   LMD_TYPE_UNDEFINED

#define MAP_KIND_ITERATOR_ 6
#define MAP_KIND_PROXY_    9
#define MAP_KIND_ARRAY_SPARSE_ 14

typedef struct GcJsArraySparseHashEntry {
    int64_t index;
    uint64_t value;
} GcJsArraySparseHashEntry;

void gc_mark_item(gc_heap_t* gc, uint64_t item);

// SparseArrayMap extends Map; on 64-bit the base Map occupies 32 bytes.
#define GC_SPARSE_ARRAY_MAP_HASH_OFFSET 32

static struct hashmap** gc_sparse_array_map_table_slot(void* map_obj) {
    if (!map_obj) return NULL;
    return (struct hashmap**)((uint8_t*)map_obj + GC_SPARSE_ARRAY_MAP_HASH_OFFSET);
}

static void gc_trace_sparse_array_map_entries(gc_heap_t* gc, void* map_obj) {
    struct hashmap** slot = gc_sparse_array_map_table_slot(map_obj);
    struct hashmap* table = slot ? *slot : NULL;
    if (!table) return;
    size_t iter = 0;
    void* item = NULL;
    while (hashmap_iter(table, &iter, &item)) {
        GcJsArraySparseHashEntry* entry = (GcJsArraySparseHashEntry*)item;
        gc_mark_item(gc, entry->value);
    }
}

static void gc_free_sparse_array_map_entries(void* map_obj) {
    struct hashmap** slot = gc_sparse_array_map_table_slot(map_obj);
    struct hashmap* table = slot ? *slot : NULL;
    if (!table) return;
    hashmap_free(table);
    *slot = NULL;
}

// ============================================================================
// Lifecycle
// ============================================================================

gc_heap_t* gc_heap_create(void) {
    // The GC keeps object slabs/bump blocks and data-zone blocks alive
    // independently until heap teardown or data-zone reset. rpmalloc first-class
    // heaps can hand a later large allocation an address range that overlaps a
    // still-retained data-zone block under heavy JS allocation churn. Use the
    // mmap-backed bump pool for GC arenas so each retained block owns a distinct
    // virtual-memory mapping.
    Pool* pool = pool_create_mmap();
    if (!pool) {
        log_error("gc_heap_create: failed to create pool");
        return NULL;
    }
    gc_heap_t* gc = gc_heap_create_with_pool(pool);
    if (!gc) {
        pool_destroy(pool);
    }
    return gc;
}

gc_heap_t* gc_heap_create_with_pool(Pool* pool) {
    if (!pool) return NULL;

    gc_heap_t* gc = (gc_heap_t*)calloc(1, sizeof(gc_heap_t));
    if (!gc) {
        log_error("gc_heap_create: failed to allocate gc_heap");
        return NULL;
    }
    gc->pool = pool;

    // create object zone (size-class free-list allocator)
    gc->object_zone = gc_object_zone_create(gc->pool);
    if (!gc->object_zone) {
        log_error("gc_heap_create: failed to create object zone");
        gc->pool = NULL;  // caller owns the pool
        free(gc);
        return NULL;
    }

    // create nursery data zone (bump allocator for variable-size buffers)
    gc->data_zone = gc_data_zone_create(gc->pool, GC_DATA_ZONE_BLOCK_SIZE);
    if (!gc->data_zone) {
        log_error("gc_heap_create: failed to create data zone");
        gc_object_zone_destroy(gc->object_zone);
        gc->pool = NULL;  // caller owns the pool
        free(gc);
        return NULL;
    }

    // create tenured data zone (survivors go here during GC compaction)
    gc->tenured_data = gc_data_zone_create(gc->pool, GC_DATA_ZONE_BLOCK_SIZE);
    if (!gc->tenured_data) {
        log_error("gc_heap_create: failed to create tenured data zone");
        gc_data_zone_destroy(gc->data_zone);
        gc_object_zone_destroy(gc->object_zone);
        gc->pool = NULL;  // caller owns the pool
        free(gc);
        return NULL;
    }

    // allocate mark stack
    gc->mark_stack = (gc_header_t**)malloc(GC_MARK_STACK_INITIAL * sizeof(gc_header_t*));
    gc->mark_top = 0;
    gc->mark_capacity = GC_MARK_STACK_INITIAL;

    gc->all_objects = NULL;
    gc->total_allocated = 0;
    gc->object_count = 0;
    gc->collections = 0;
    gc->bytes_collected = 0;

    // initialize root slot registry
    gc->root_slot_count = 0;
    gc->root_slot_capacity = GC_ROOT_SLOTS_INITIAL;
    gc->root_slots = (uint64_t**)calloc((size_t)gc->root_slot_capacity, sizeof(uint64_t*));
    if (!gc->root_slots) {
        log_error("gc_heap_create: failed to allocate root slots");
        gc->root_slot_capacity = 0;
    }

    gc->weak_slots = NULL;
    gc->weak_slot_count = 0;
    gc->weak_slot_capacity = 0;

    // initialize root range registry (for JS closure env arrays)
    gc->root_ranges = NULL;
    gc->root_range_count = 0;
    gc->root_range_capacity = 0;

    // Compatibility preserves the existing conservative scan until the
    // runtime explicitly opts a fully-audited heap into another mode.
    gc->root_mode = GC_ROOT_MODE_COMPATIBILITY;
    memset(&gc->last_root_stats, 0, sizeof(gc->last_root_stats));
    gc->root_type_stats = NULL;
    gc->root_type_stat_count = 0;
    gc->root_type_stat_capacity = 0;
    gc->force_collect_interval = 0;
    gc->force_allocation_count = 0;
    gc->forced_collection_count = 0;
    gc->force_random_seed = 0;
    gc->force_random_state = 0;
    gc->force_random_one_in = 0;
    gc->poison_freed = 0;

    // collection trigger
    gc->gc_threshold = GC_DATA_ZONE_THRESHOLD;
    gc->collecting = 0;
    gc->collect_callback = NULL;

    // VMap callbacks (set by runtime via lambda-mem.cpp)
    gc->vmap_trace = NULL;
    gc->vmap_destroy = NULL;
    gc->error_trace = NULL;
    gc->error_destroy = NULL;
    gc->js_native_trace = NULL;
    gc->js_function_trace = NULL;
    gc->external_destroy = NULL;
    // Initialize bump-pointer allocator
    gc->bump_blocks = NULL;
    gc->bump_cursor = NULL;
    gc->bump_end = NULL;
    gc_bump_block_t* first_block = gc_alloc_bump_block(gc, GC_BUMP_BLOCK_INITIAL_SIZE);
    if (first_block) {
        first_block->next = gc->bump_blocks;
        gc->bump_blocks = first_block;
        gc->bump_cursor = first_block->base;
        gc->bump_end = first_block->base + first_block->size;
        log_debug("gc_heap_create: initial bump block %zu bytes", first_block->size);
    }

    log_debug("gc_heap_create: dual-zone GC heap created (threshold=%zu)", gc->gc_threshold);
    return gc;
}

void gc_heap_destroy(gc_heap_t* gc) {
    if (!gc) return;

    if (gc->external_destroy) {
        gc_header_t* current = gc->all_objects;
        while (current) {
            if (!(current->gc_flags & GC_FLAG_FREED)) {
                gc->external_destroy((void*)(current + 1), current->type_tag);
            }
            current = current->next;
        }
    }

    // unlink from the memory context if registered (before freeing the struct)
    if (gc->mem_node && g_gc_heap_node_release) {
        g_gc_heap_node_release(gc->mem_node);
        gc->mem_node = NULL;
    }

    // free mark stack (C heap allocated)
    if (gc->mark_stack) {
        free(gc->mark_stack);
        gc->mark_stack = NULL;
    }

    // free root ranges (C heap allocated)
    if (gc->root_ranges) {
        free(gc->root_ranges);
        gc->root_ranges = NULL;
    }

    if (gc->root_type_stats) {
        free(gc->root_type_stats);
        gc->root_type_stats = NULL;
    }

    // free root slot table (C heap allocated)
    if (gc->root_slots) {
        free(gc->root_slots);
        gc->root_slots = NULL;
    }

    if (gc->weak_slots) {
        free(gc->weak_slots);
        gc->weak_slots = NULL;
    }

    if (gc->large_objects) {
        free(gc->large_objects);
        gc->large_objects = NULL;
    }

    // free bump block metadata (block memory is pool-allocated)
    gc_bump_block_t* block = gc->bump_blocks;
    while (block) {
        gc_bump_block_t* next = block->next;
        free(block);
        block = next;
    }
    gc->bump_blocks = NULL;

    // free zone metadata (zone memory is pool-allocated, freed by pool_destroy)
    if (gc->object_zone) gc_object_zone_destroy(gc->object_zone);
    if (gc->data_zone) gc_data_zone_destroy(gc->data_zone);
    if (gc->tenured_data) gc_data_zone_destroy(gc->tenured_data);

    // free remaining large objects (allocated with malloc, not pool)
    gc_header_t* obj = gc->all_objects;
    while (obj) {
        gc_header_t* next = obj->next;
        if (obj->gc_flags & GC_FLAG_LARGE) {
            free(obj);
        }
        obj = next;
    }
    gc->all_objects = NULL;

    // pool_destroy bulk-frees all pool-allocated memory
    if (gc->pool) pool_destroy(gc->pool);

    log_debug("gc_heap_destroy: %zu objects, %zu collections, %zu bytes collected",
              gc->object_count, gc->collections, gc->bytes_collected);
    free(gc);
}

// ============================================================================
// Allocation
// ============================================================================

static inline int gc_maybe_force_collect(gc_heap_t* gc, const char* site) {
    if (!gc || gc->collecting || !gc->collect_callback ||
            (gc->force_collect_interval == 0 &&
             gc->force_random_one_in == 0)) {
        return 0;
    }
    gc->force_allocation_count++;
    int force = gc->force_collect_interval > 0 &&
        gc->force_allocation_count % gc->force_collect_interval == 0;
    if (gc->force_random_one_in > 0) {
        // xorshift64* gives a deterministic, dependency-free stress schedule.
        // A zero seed is remapped by the setter because zero is a fixed point.
        uint64_t state = gc->force_random_state;
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        gc->force_random_state = state;
        uint64_t sample = state * UINT64_C(2685821657736338717);
        if (sample % gc->force_random_one_in == 0) force = 1;
    }
    if (!force) return 0;

    gc->forced_collection_count++;
    log_debug("gc-force-collect: allocation=%zu forced_collection=%zu seed=%llu one_in=%u site=%s",
        gc->force_allocation_count, gc->forced_collection_count,
        (unsigned long long)gc->force_random_seed,
        (unsigned)gc->force_random_one_in,
        site ? site : "unknown");
    gc->collect_callback();
    return 1;
}

void* gc_heap_alloc(gc_heap_t* gc, size_t size, uint16_t type_tag) {
    if (!gc || !gc->pool) {
        log_error("gc_heap_alloc: invalid gc_heap");
        return NULL;
    }
    gc_assert_allocation_allowed(gc, "gc_heap_alloc");
    gc_maybe_force_collect(gc, "gc_heap_alloc");

    // try object zone first (for objects up to GC_LARGE_OBJECT_THRESHOLD)
    void* ptr = gc_object_zone_alloc(gc->object_zone, size, type_tag, &gc->all_objects);
    if (ptr) {
        gc->total_allocated += sizeof(gc_header_t) + size;
        gc->object_count++;
        assert_raw_item_pointer(ptr);
        return ptr;
    }

    // large object: use malloc to avoid address conflicts with pool allocations
    // (the pool may be shared with other subsystems that allocate map data buffers)
    size_t total = sizeof(gc_header_t) + size;
    gc_header_t* header = (gc_header_t*)malloc(total);
    if (!header) {
        log_error("gc_heap_alloc: malloc failed for %zu bytes (large object)", total);
        return NULL;
    }
    memset(header, 0, total);
    // initialize header
    header->next = gc->all_objects;
    header->type_tag = type_tag;
    header->gc_flags = GC_FLAG_LARGE;
    header->marked = 0;
    header->alloc_size = (uint32_t)size;

    // link to all_objects list
    gc->all_objects = header;
    if (!gc_large_object_add(gc, header)) {
        gc->all_objects = header->next;
        free(header);
        return NULL;
    }
    gc->total_allocated += total;
    gc->object_count++;

    void* large_ptr = (void*)(header + 1);
    assert_raw_item_pointer(large_ptr);
    return large_ptr;
}

void* gc_heap_calloc(gc_heap_t* gc, size_t size, uint16_t type_tag) {
    void* ptr = gc_heap_alloc(gc, size, type_tag);
    // gc_heap_alloc already zeroes large objects (malloc + memset)
    // and object zone allocates from zeroed slabs, so no additional zeroing needed
    return ptr;
}

void* gc_heap_calloc_class(gc_heap_t* gc, size_t size, uint16_t type_tag, int cls) {
    gc_assert_allocation_allowed(gc, "gc_heap_calloc_class");
    gc_maybe_force_collect(gc, "gc_heap_calloc_class");
    // Fast path: skip gc_heap_alloc → gc_object_zone_alloc class_index lookup.
    // The class index is pre-computed by the JIT at compile time.
    void* ptr = gc_object_zone_alloc_class(gc->object_zone, cls, size, type_tag,
                                            &gc->all_objects);
    if (ptr) {
        gc->total_allocated += sizeof(gc_header_t) + size;
        gc->object_count++;
        assert_raw_item_pointer(ptr);
    }
    // No large-object fallback needed — JIT only uses this for known small sizes.
    // No memset needed — object zone returns zeroed memory.
    return ptr;
}

void* gc_heap_bump_alloc(gc_heap_t* gc, size_t slot_size, size_t alloc_size,
                          uint16_t type_tag, int cls) {
    gc_assert_allocation_allowed(gc, "gc_heap_bump_alloc");
    gc_maybe_force_collect(gc, "gc_heap_bump_alloc");
    // ---- Fast path: try free list first (recycling after GC) ----
    gc_header_t* free_hdr = gc->object_zone->free_lists[cls];
    if (free_hdr) {
        gc->object_zone->free_lists[cls] = free_hdr->next;
        size_t class_user_size = gc_object_zone_class_size(cls);
        memset((void*)(free_hdr + 1), 0, class_user_size);
        // reinitialize header
        free_hdr->type_tag = type_tag;
        free_hdr->gc_flags = 0;
        free_hdr->marked = 0;
        free_hdr->alloc_size = (uint32_t)alloc_size;
        // link into all_objects
        free_hdr->next = gc->all_objects;
        gc->all_objects = free_hdr;
        gc->object_zone->total_slots_allocated++;
        gc->total_allocated += sizeof(gc_header_t) + alloc_size;
        gc->object_count++;
        void* ptr = (void*)(free_hdr + 1);
        assert_raw_item_pointer(ptr);
        return ptr;
    }

    // ---- Bump-pointer path ----
    uint8_t* cursor = gc->bump_cursor;
    if (cursor + slot_size > gc->bump_end) {
        // Current block exhausted — allocate a new one (double size, capped)
        size_t current_size = gc->bump_blocks ? gc->bump_blocks->size : GC_BUMP_BLOCK_INITIAL_SIZE;
        size_t new_size = current_size * 2;
        if (new_size > GC_BUMP_BLOCK_MAX_SIZE) new_size = GC_BUMP_BLOCK_MAX_SIZE;
        if (new_size < slot_size) new_size = slot_size * 256; // ensure room
        gc_bump_block_t* new_block = gc_alloc_bump_block(gc, new_size);
        if (!new_block) {
            // Fallback to slab allocator
            void* ptr = gc_object_zone_alloc_class(gc->object_zone, cls, alloc_size,
                                                   type_tag, &gc->all_objects);
            if (ptr) assert_raw_item_pointer(ptr);
            return ptr;
        }
        new_block->next = gc->bump_blocks;
        gc->bump_blocks = new_block;
        gc->bump_cursor = new_block->base;
        gc->bump_end = new_block->base + new_block->size;
        cursor = gc->bump_cursor;
    }

    gc->bump_cursor = cursor + slot_size;

    // Initialize header in-place (memory is pre-zeroed from block allocation)
    gc_header_t* header = (gc_header_t*)cursor;
    header->type_tag = type_tag;
    header->gc_flags = GC_FLAG_BUMP;
    header->marked = 0;
    header->alloc_size = (uint32_t)alloc_size;

    // Link into all_objects list
    header->next = gc->all_objects;
    gc->all_objects = header;

    gc->total_allocated += slot_size;
    gc->object_count++;

    void* ptr = (void*)(header + 1);
    assert_raw_item_pointer(ptr);
    return ptr;
}

void gc_heap_pool_free(gc_heap_t* gc, void* ptr) {
    if (!ptr || !gc) return;
    gc_header_t* header = gc_get_header(ptr);
    // guard against double-free
    if (header->gc_flags & GC_FLAG_FREED) return;
    header->gc_flags |= GC_FLAG_FREED;
    if (gc->poison_freed && header->alloc_size > 0) {
        // Explicitly freed objects are dead immediately; poison only their
        // payload so sweep can still classify and unlink the GC header.
        memset(ptr, GC_FREED_POISON_BYTE, header->alloc_size);
    }
    gc->total_allocated -= sizeof(gc_header_t) + header->alloc_size;
    gc->object_count--;
}

void* gc_data_alloc(gc_heap_t* gc, size_t size) {
    if (!gc || !gc->data_zone || size == 0) {
        log_error("gc_data_alloc: null check failed gc=%p dz=%p size=%zu",
                  (void*)gc, gc ? (void*)gc->data_zone : NULL, size);
        return NULL;
    }

    gc_assert_allocation_allowed(gc, "gc_data_alloc");

    // Forced stress and threshold collection share this legal pre-allocation
    // safepoint; never invoke the callback twice for one allocation.
    int forced = gc_maybe_force_collect(gc, "gc_data_alloc");
    if (!forced && !gc->collecting && gc->collect_callback) {
        size_t used = gc_data_zone_used(gc->data_zone);
        if (used >= gc->gc_threshold) {
            log_debug("gc_data_alloc: threshold exceeded (%zu >= %zu), triggering GC",
                      used, gc->gc_threshold);
            gc->collect_callback();
        }
    }

    void* result = gc_data_zone_alloc(gc->data_zone, size);
    if (!result) {
        log_error("gc_data_alloc: gc_data_zone_alloc returned NULL for %zu bytes, dz=%p dz->current=%p",
                  size, (void*)gc->data_zone, gc->data_zone ? (void*)gc->data_zone->current : NULL);
    }
    return result;
}

// ============================================================================
// Ownership queries
// ============================================================================

int gc_is_managed(gc_heap_t* gc, void* ptr) {
    if (!gc || !ptr) return 0;
    // check object zone
    if (gc_object_zone_owns(gc->object_zone, ptr)) return 1;
    // check bump-allocated object structs
    if (gc_bump_block_owns_exact(gc, ptr)) return 1;
    // check data zones
    if (gc_data_zone_owns(gc->data_zone, ptr)) return 1;
    if (gc_data_zone_owns(gc->tenured_data, ptr)) return 1;
    return 0;
}

int gc_is_nursery_data(gc_heap_t* gc, void* ptr) {
    if (!gc || !ptr) return 0;
    return gc_data_zone_owns(gc->data_zone, ptr);
}

// ============================================================================
// Root Slot Registration
// ============================================================================

int gc_try_register_root(gc_heap_t* gc, uint64_t* slot) {
    if (!gc || !slot) return 0;
    // check for duplicate
    for (int i = 0; i < gc->root_slot_count; i++) {
        if (gc->root_slots[i] == slot) return 1;
    }
    if (gc->root_slot_count >= gc->root_slot_capacity) {
        int new_cap = gc->root_slot_capacity ? gc->root_slot_capacity * 2 : GC_ROOT_SLOTS_INITIAL;
        uint64_t** new_slots = (uint64_t**)realloc(gc->root_slots,
            (size_t)new_cap * sizeof(uint64_t*));
        if (!new_slots) {
            log_error("gc_register_root: realloc failed for %d slots", new_cap);
            return 0;
        }
        memset(new_slots + gc->root_slot_capacity, 0,
            (size_t)(new_cap - gc->root_slot_capacity) * sizeof(uint64_t*));
        gc->root_slots = new_slots;
        gc->root_slot_capacity = new_cap;
    }
    gc->root_slots[gc->root_slot_count++] = slot;
    log_debug("gc_register_root: registered slot %p (total: %d)", (void*)slot, gc->root_slot_count);
    return 1;
}

void gc_register_root(gc_heap_t* gc, uint64_t* slot) {
    (void)gc_try_register_root(gc, slot);
}

void gc_unregister_root(gc_heap_t* gc, uint64_t* slot) {
    if (!gc || !slot) return;
    for (int i = 0; i < gc->root_slot_count; i++) {
        if (gc->root_slots[i] == slot) {
            // shift remaining entries down
            for (int j = i; j < gc->root_slot_count - 1; j++) {
                gc->root_slots[j] = gc->root_slots[j + 1];
            }
            gc->root_slot_count--;
            log_debug("gc_unregister_root: removed slot %p (total: %d)", (void*)slot, gc->root_slot_count);
            return;
        }
    }
}

void gc_no_gc_scope_begin(gc_heap_t* gc) {
#ifndef NDEBUG
    if (gc) gc->no_gc_scope_depth++;
#else
    (void)gc;
#endif
}

void gc_no_gc_scope_end(gc_heap_t* gc) {
#ifndef NDEBUG
    if (!gc || gc->no_gc_scope_depth <= 0) {
        log_error("gc-no-gc-scope: unbalanced scope end");
        abort();
    }
    gc->no_gc_scope_depth--;
#else
    (void)gc;
#endif
}

void gc_register_weak(gc_heap_t* gc, uint64_t* slot,
                      gc_weak_clear_fn on_clear, void* context) {
    if (!gc || !slot) return;
    for (int i = 0; i < gc->weak_slot_count; i++) {
        if (gc->weak_slots[i].slot == slot) {
            gc->weak_slots[i].on_clear = on_clear;
            gc->weak_slots[i].context = context;
            return;
        }
    }
    if (gc->weak_slot_count >= gc->weak_slot_capacity) {
        int new_capacity = gc->weak_slot_capacity
            ? gc->weak_slot_capacity * 2 : GC_ROOT_SLOTS_INITIAL;
        gc_weak_slot_t* slots = (gc_weak_slot_t*)realloc(
            gc->weak_slots, (size_t)new_capacity * sizeof(gc_weak_slot_t));
        if (!slots) {
            log_error("gc_register_weak: failed to grow weak slot registry to %d",
                      new_capacity);
            return;
        }
        gc->weak_slots = slots;
        gc->weak_slot_capacity = new_capacity;
    }
    gc_weak_slot_t* weak = &gc->weak_slots[gc->weak_slot_count++];
    weak->slot = slot;
    weak->on_clear = on_clear;
    weak->context = context;
}

void gc_unregister_weak(gc_heap_t* gc, uint64_t* slot) {
    if (!gc || !slot) return;
    for (int i = 0; i < gc->weak_slot_count; i++) {
        if (gc->weak_slots[i].slot == slot) {
            gc->weak_slots[i].slot = NULL;
            gc->weak_slots[i].on_clear = NULL;
            gc->weak_slots[i].context = NULL;
            return;
        }
    }
}

void gc_register_root_range(gc_heap_t* gc, uint64_t* base, int count) {
    if (!gc || !base || count <= 0) return;
    for (int i = 0; i < gc->root_range_count; i++) {
        if (gc->root_ranges[i].base == base) {
            gc->root_ranges[i].count = count;
            return;
        }
    }
    if (gc->root_range_count >= gc->root_range_capacity) {
        int new_cap = gc->root_range_capacity ? gc->root_range_capacity * 2 : 256;
        gc_root_range_t* new_arr = (gc_root_range_t*)realloc(gc->root_ranges,
            new_cap * sizeof(gc_root_range_t));
        if (!new_arr) {
            log_error("gc_register_root_range: realloc failed for %d ranges", new_cap);
            return;
        }
        gc->root_ranges = new_arr;
        gc->root_range_capacity = new_cap;
    }
    gc->root_ranges[gc->root_range_count].base = base;
    gc->root_ranges[gc->root_range_count].count = count;
    gc->root_range_count++;
}

void gc_unregister_root_range(gc_heap_t* gc, uint64_t* base) {
    if (!gc || !base) return;
    for (int i = 0; i < gc->root_range_count; i++) {
        if (gc->root_ranges[i].base == base) {
            for (int j = i; j < gc->root_range_count - 1; j++) {
                gc->root_ranges[j] = gc->root_ranges[j + 1];
            }
            gc->root_range_count--;
            return;
        }
    }
}

void gc_set_root_mode(gc_heap_t* gc, gc_root_mode_t mode) {
    if (!gc) return;
    if (mode < GC_ROOT_MODE_COMPATIBILITY || mode > GC_ROOT_MODE_PRECISE_ONLY) {
        log_error("gc-root-mode: rejected invalid mode %d", (int)mode);
        return;
    }
#if !GC_CONSERVATIVE_SCAN_AVAILABLE
    if (mode != GC_ROOT_MODE_PRECISE_ONLY) {
        // Scanner-free release code must never pretend compatibility is
        // active; retaining precise-only is fail-closed for direct API users.
        log_error("gc-root-mode: %s unavailable in scanner-free build",
            gc_root_mode_name(mode));
        return;
    }
#endif
    gc->root_mode = mode;
}

int gc_conservative_scan_available(void) {
    return GC_CONSERVATIVE_SCAN_AVAILABLE;
}

gc_root_mode_t gc_get_root_mode(const gc_heap_t* gc) {
    return gc ? gc->root_mode : GC_ROOT_MODE_COMPATIBILITY;
}

const char* gc_root_mode_name(gc_root_mode_t mode) {
    switch (mode) {
    case GC_ROOT_MODE_COMPATIBILITY: return "compatibility";
    case GC_ROOT_MODE_SHADOW_VERIFY: return "shadow-verify";
    case GC_ROOT_MODE_PRECISE_ONLY: return "precise-only";
    default: return "invalid";
    }
}

const gc_root_stats_t* gc_get_last_root_stats(const gc_heap_t* gc) {
    return gc ? &gc->last_root_stats : NULL;
}

size_t gc_get_last_conservative_type_count(const gc_heap_t* gc,
        uint16_t type_tag) {
    if (!gc) return 0;
    for (int i = 0; i < gc->root_type_stat_count; i++) {
        if (gc->root_type_stats[i].type_tag == type_tag) {
            return gc->root_type_stats[i].count;
        }
    }
    return 0;
}

void gc_set_force_collect_interval(gc_heap_t* gc, size_t interval) {
    if (!gc) return;
    gc->force_collect_interval = interval;
    gc->force_allocation_count = 0;
    gc->forced_collection_count = 0;
}

void gc_set_force_collect_random(gc_heap_t* gc, uint64_t seed,
        uint32_t one_in) {
    if (!gc) return;
    gc->force_random_seed = seed;
    gc->force_random_state = seed ? seed : UINT64_C(0x9e3779b97f4a7c15);
    gc->force_random_one_in = one_in;
    gc->force_allocation_count = 0;
    gc->forced_collection_count = 0;
}

size_t gc_get_forced_collection_count(const gc_heap_t* gc) {
    return gc ? gc->forced_collection_count : 0;
}

void gc_set_poison_freed(gc_heap_t* gc, int enabled) {
    if (!gc) return;
    gc->poison_freed = enabled ? 1 : 0;
}

int gc_get_poison_freed(const gc_heap_t* gc) {
    return gc ? gc->poison_freed : 0;
}

// ============================================================================
// Collection Trigger Query
// ============================================================================

int gc_should_collect(gc_heap_t* gc) {
    if (!gc || gc->collecting) return 0;
    size_t used = gc_data_zone_used(gc->data_zone);
    return used >= gc->gc_threshold;
}

void gc_set_collect_callback(gc_heap_t* gc, gc_collect_callback_t callback) {
    if (!gc) return;
    gc->collect_callback = callback;
    log_debug("gc_set_collect_callback: callback %s", callback ? "registered" : "cleared");
}

// ============================================================================
// Mark-and-Sweep Collector
// ============================================================================

// push a header onto the mark stack
static void mark_stack_push(gc_heap_t* gc, gc_header_t* header) {
    if (gc->mark_top >= gc->mark_capacity) {
        // grow mark stack
        gc->mark_capacity *= 2;
        gc->mark_stack = (gc_header_t**)realloc(gc->mark_stack,
                                                  gc->mark_capacity * sizeof(gc_header_t*));
        if (!gc->mark_stack) {
            log_error("gc mark stack: realloc failed");
            return;
        }
    }
    gc->mark_stack[gc->mark_top++] = header;
}

static gc_header_t* mark_stack_pop(gc_heap_t* gc) {
    if (gc->mark_top <= 0) return NULL;
    return gc->mark_stack[--gc->mark_top];
}

// extract raw pointer from a tagged Item value.
// Lambda Item encoding:
//   - Null: _type_id = LMD_TYPE_NULL (1), lower 56 bits = 0. item != 0.
//   - Bool/Int: _type_id = 2 or 3, value packed inline → no pointer.
//   - Tagged pointers (Int64, Float, Decimal, DateTime, String, Symbol, Binary):
//     _type_id = 4-11, lower 56 bits = pointer (mask off tag to get address).
//   - Containers (List, Array, Map, Element, Function, etc.):
//     Stored as raw pointer (item.item = (uint64_t)pointer).
//     On 64-bit platforms, heap pointer high byte is 0 → _type_id = 0.
//     Type is determined by reading Container.type_id at the struct's first byte.
//   - Error: _type_id = LMD_TYPE_ERROR, lower 56 bits may hold LambdaError*.
//   - Any: _type_id = LMD_TYPE_ANY, lower bits = 0 → no pointer.
static void* item_to_ptr(uint64_t item) {
    if (item == 0) return NULL;  // all-zero value

    // Inline doubles and invalid sentinel tags are scalar words; treating them
    // as fallback raw pointers can keep or rewrite JS holes as live objects.
    if (item & ITEM_DBL_MASK) return NULL;

    uint8_t tag = (uint8_t)(item >> 56);

    // tag 1 = NULL item, tag 2 = BOOL (inline), tag 3 = INT (inline)
    if (tag >= LMD_TYPE_NULL_ && tag <= LMD_TYPE_INT_) return NULL;

    // tag 0: container pointer (raw heap address, high byte = 0 on 64-bit systems)
    if (tag == 0) {
        return (void*)(uintptr_t)item;
    }

    // Compact INT64 values share the tag with boxed INT64 pointers; the
    // payload discriminator keeps scalar bits out of the conservative roots.
    if (tag == LMD_TYPE_INT64_ && (item & ITEM_INT64_INLINE_MARK)) return NULL;

    // tags 4-11: tagged pointer types (Int64, Float, Decimal, DateTime, String, etc.)
    // mask off the tag byte in the upper 8 bits to get the actual pointer
    if (tag >= LMD_TYPE_INT64_ && tag <= LMD_TYPE_BINARY_) {
        return (void*)(uintptr_t)(item & 0x00FFFFFFFFFFFFFF);
    }

    if (tag == LMD_TYPE_ERROR_) {
        return (void*)(uintptr_t)(item & 0x00FFFFFFFFFFFFFF);
    }

    // Raw container pointers have a zero high byte; unknown high tags are
    // sentinels or invalid Items, not recoverable pointer encodings.
    if (tag >= LMD_TYPE_RANGE_) return NULL;

    // anything else (ERROR=25 with null pointer, ANY=24, etc.)
    // extract pointer — if lower 56 bits are 0, returns NULL
    void* ptr = (void*)(uintptr_t)(item & 0x00FFFFFFFFFFFFFF);
    return ptr;
}

// check if a pointer is to a GC-managed OBJECT (has GCHeader)
static int gc_bump_block_owns_exact(gc_heap_t* gc, void* ptr) {
    if (!gc || !ptr) return 0;
    uint8_t* p = (uint8_t*)ptr;
    gc_bump_block_t* block = gc->bump_blocks;
    while (block) {
        uint8_t* start = block->base;
        uint8_t* end = block->base + block->size;
        if (p >= start + sizeof(gc_header_t) && p < end) {
            uint8_t* cursor = start;
            uint8_t* used_end = (block == gc->bump_blocks) ? gc->bump_cursor : end;
            if (used_end > end) used_end = end;

            while (cursor + sizeof(gc_header_t) <= used_end) {
                gc_header_t* header = (gc_header_t*)cursor;
                if (header->type_tag == 0 || header->alloc_size == 0) return 0;

                int cls = gc_object_zone_class_index(header->alloc_size);
                if (cls < 0) return 0;
                size_t class_size = gc_object_zone_class_size(cls);
                size_t slot_size = sizeof(gc_header_t) + class_size;
                uint8_t* user_ptr = (uint8_t*)(header + 1);
                uint8_t* next_cursor = cursor + slot_size;

                if (p == user_ptr) return 1;
                if (p < next_cursor) return 0;
                cursor = next_cursor;
            }
            return 0;
        }
        block = block->next;
    }
    return 0;
}

static int is_gc_object(gc_heap_t* gc, void* ptr) {
    if (!gc || !ptr) return 0;
    if (gc_object_zone_owns(gc->object_zone, ptr)) return 1;
    if (gc_large_object_find(gc, ptr) >= 0) return 1;
    if (gc_bump_block_owns_exact(gc, ptr)) return 1;
    return 0;
}

void gc_mark_item(gc_heap_t* gc, uint64_t item) {
    void* ptr = item_to_ptr(item);
    if (!ptr) return;

    // only mark objects that are in the GC object zone (have GCHeader)
    if (!is_gc_object(gc, ptr)) return;

    gc_header_t* header = gc_get_header(ptr);
    if (!header) return;
    if (header->marked) return;     // already visited
    if (header->gc_flags & GC_FLAG_FREED) return; // freed object

    header->marked = 1;
    mark_stack_push(gc, header);
}

void gc_mark_object_ptr(gc_heap_t* gc, void* ptr) {
    if (!ptr || !is_gc_object(gc, ptr)) return;

    gc_header_t* header = gc_get_header(ptr);
    if (!header) return;
    if (header->marked) return;
    if (header->gc_flags & GC_FLAG_FREED) return;

    header->marked = 1;
    mark_stack_push(gc, header);
}

static void gc_process_weak_slots(gc_heap_t* gc) {
    if (!gc || !gc->weak_slots) return;
    // Only slots present when weak processing starts participate in this
    // collection; callbacks may safely register work for the next cycle.
    int snapshot_count = gc->weak_slot_count;
    for (int i = 0; i < snapshot_count; i++) {
        uint64_t* slot = gc->weak_slots[i].slot;
        if (!slot || *slot == 0) continue;
        void* ptr = item_to_ptr(*slot);
        if (!ptr || !is_gc_object(gc, ptr)) continue;
        gc_header_t* header = gc_get_header(ptr);
        if (header && header->marked && !(header->gc_flags & GC_FLAG_FREED)) continue;

        gc_weak_clear_fn on_clear = gc->weak_slots[i].on_clear;
        void* context = gc->weak_slots[i].context;
        // Remove the registry edge before invoking user cleanup, so callback
        // unregistration is idempotent and cannot shift the active traversal.
        gc->weak_slots[i].slot = NULL;
        gc->weak_slots[i].on_clear = NULL;
        gc->weak_slots[i].context = NULL;
        *slot = 0;
        if (on_clear) on_clear(slot, context);
    }

    int write = 0;
    for (int read = 0; read < gc->weak_slot_count; read++) {
        if (!gc->weak_slots[read].slot) continue;
        if (write != read) gc->weak_slots[write] = gc->weak_slots[read];
        write++;
    }
    gc->weak_slot_count = write;
}

static void gc_mark_possible_item(gc_heap_t* gc, uint64_t value) {
    void* ptr = item_to_ptr(value);
    if (!ptr || !is_gc_object(gc, ptr)) return;

    gc_header_t* header = gc_get_header(ptr);
    if (!header) return;
    if (header->marked) return;
    if (header->gc_flags & GC_FLAG_FREED) return;

    header->marked = 1;
    mark_stack_push(gc, header);
}

static void gc_trace_data_words(gc_heap_t* gc, void* data_ptr, int64_t byte_size) {
    if (!data_ptr || byte_size < (int64_t)sizeof(uint64_t)) return;
    for (int64_t offset = 0; offset + (int64_t)sizeof(uint64_t) <= byte_size; offset += (int64_t)sizeof(void*)) {
        uint64_t value = *(uint64_t*)((uint8_t*)data_ptr + offset);
        gc_mark_possible_item(gc, value);
    }
}

// trace outgoing Item pointers from a type-aware object
// This is the core tracing logic that knows Lambda's struct layouts.
static void gc_trace_object(gc_heap_t* gc, gc_header_t* header) {
    void* obj = (void*)(header + 1);
    uint16_t tag = header->type_tag;

    switch (tag) {
    case GC_TYPE_JS_ENV_: {
        // JS environments are raw Item arrays. The GC header owns their exact
        // byte length, so parent links and captured values can be traced
        // without embedding a user-visible container header in slot zero.
        uint64_t* items = (uint64_t*)obj;
        // The second half is raw scalar-tail storage owned by the first-half
        // Item slots; tracing it as Items would retain arbitrary bit patterns.
        size_t count = header->alloc_size / (2 * sizeof(uint64_t));
        for (size_t i = 0; i < count; i++) gc_mark_item(gc, items[i]);
        break;
    }
    // types with no outgoing Item pointers — nothing to trace
    case LMD_TYPE_INT64_:
    case LMD_TYPE_FLOAT_:
    case LMD_TYPE_DTIME_:
    case LMD_TYPE_STRING_:
    case LMD_TYPE_SYMBOL_:
    case LMD_TYPE_BINARY_:
    case LMD_TYPE_DECIMAL_:
    case LMD_TYPE_RANGE_:
    case LMD_TYPE_PATH_:
        break;

    case LMD_TYPE_ERROR_: {
        if (gc->error_trace) {
            gc->error_trace(obj, gc);
        }
        break;
    }

    case LMD_TYPE_ARRAY_NUM_: {
        // Owned 1-D arrays have no outgoing pointers; views (is_view bit 5) hold
        // a base reference via the shape side-table in `extra` (offset 24).
        uint8_t* p = (uint8_t*)obj;
        uint8_t array_flags = p[2];
        if (array_flags & 0x02) {  // Container.is_view in array_flags byte
            void* shape_ptr = (void*)(uintptr_t)(*(int64_t*)(p + 24));
            if (shape_ptr) {
                // ArrayNumShape explicitly tags the backing source; semantic base
                // remains at offset 16 and is the only GC-traced descriptor pointer.
                uint64_t base = *(uint64_t*)((uint8_t*)shape_ptr + 16);
                gc_mark_possible_item(gc, base);
            }
        }
        break;
    }

    case LMD_TYPE_ARRAY_: {
        // Array: items is an Item* array
        // struct layout: { type_id(1), flags(1), Item* items(8), length(8), extra(8), capacity(8) }
        // We read items and length at known offsets.
        // Container is 2 bytes, then padding to pointer alignment.
        // Use byte offsets matching the actual struct layout:
        //   offset 0: type_id (1 byte)
        //   offset 1: flags (1 byte)
        //   offset 2: padding (6 bytes on 64-bit)
        //   offset 8: Item* items (8 bytes)
        //   offset 16: int64_t length (8 bytes)
        uint8_t* p = (uint8_t*)obj;
        void* items_ptr = *(void**)(p + 8);     // Item* items
        int64_t length = *(int64_t*)(p + 16);   // logical length
        uint8_t flags = *(uint8_t*)(p + 1);
        int64_t extra = *(int64_t*)(p + 24);    // reserved tail item count
        int64_t capacity = *(int64_t*)(p + 32); // allocated dense capacity
        int64_t dense_limit = capacity >= extra ? capacity - extra : 0;
        int64_t dense_count = length < dense_limit ? length : dense_limit;
        if (items_ptr && dense_count > 0) {
            uint64_t* items = (uint64_t*)items_ptr;
            for (int64_t i = 0; i < dense_count; i++) {
                gc_mark_item(gc, items[i]);
            }
        }
        if (items_ptr && capacity > 0 && (flags & CONTAINER_FLAG_JS_PROPS)) {
            // Flag-gated interpretation keeps the props edge precise; `extra`
            // is only a count and scalar payload words are never traced as Items.
            gc_mark_item(gc, ((uint64_t*)items_ptr)[capacity - 1]);
        }
        break;
    }

    case LMD_TYPE_MAP_:
    case LMD_TYPE_OBJECT_: {
        // Map/Object: { Container(8), type*(8@8), data*(8@16), data_cap(4@24) }
        uint8_t* p = (uint8_t*)obj;
        uint8_t map_kind = p[3];
        void* type_ptr = *(void**)(p + 8);    // TypeMap*
        void* data_ptr = *(void**)(p + 16);   // data buffer
        if (tag == LMD_TYPE_MAP_ && gc->js_native_trace) {
            gc->js_native_trace(obj, gc);
        }
        if (tag == LMD_TYPE_MAP_ && map_kind == MAP_KIND_ITERATOR_) {
            if (data_ptr) gc_mark_item(gc, *(uint64_t*)data_ptr);
            break;
        }
        if (tag == LMD_TYPE_MAP_ && map_kind == MAP_KIND_PROXY_) {
            if (data_ptr) {
                uint64_t* slots = (uint64_t*)data_ptr;
                gc_mark_item(gc, slots[0]);
                gc_mark_item(gc, slots[1]);
                gc_mark_item(gc, slots[2]);
            }
            break;
        }
        if (tag == LMD_TYPE_MAP_ && map_kind == MAP_KIND_ARRAY_SPARSE_) {
            gc_trace_sparse_array_map_entries(gc, obj);
        }
        if (!type_ptr || !data_ptr) break;

        // Walk shape entries to find Item fields
        // TypeMap layout: { Type(2+6pad=8), length(8@8), byte_size(8@16),
        //   type_index(4@24), pad(4), shape*(8@32), last*(8@40) }
        uint8_t* tp = (uint8_t*)type_ptr;
        int64_t byte_size = *(int64_t*)(tp + 16);     // TypeMap.byte_size
        void* shape_ptr = *(void**)(tp + 32);          // TypeMap.shape (ShapeEntry*)

        // Walk ShapeEntry linked list
        // ShapeEntry: { name*(8), type*(8), byte_offset(8), next*(8), ns*(8), default_value*(8) }
        uint8_t* shape = (uint8_t*)shape_ptr;
        while (shape) {
            void* field_type = *(void**)(shape + 8);   // ShapeEntry.type (Type*)
            int64_t byte_offset = *(int64_t*)(shape + 16);  // ShapeEntry.byte_offset
            void* next_shape = *(void**)(shape + 24);  // ShapeEntry.next

            if (field_type) {
                uint8_t field_type_id = *(uint8_t*)field_type;  // Type.type_id
                // only trace Item-typed fields (containers, strings, etc.)
                // Skip inline values (bool, int) which don't hold GC pointers
                if (field_type_id >= LMD_TYPE_INT64_ && field_type_id != LMD_TYPE_BOOL_
                    && field_type_id != LMD_TYPE_UNDEFINED_) {
                    if (byte_offset >= 0 && byte_offset < byte_size) {
                        void* field_ptr = (uint8_t*)data_ptr + byte_offset;

                        // LMD_TYPE_ANY fields use 9-byte TypedItem layout:
                        //   byte 0: runtime TypeId (1 byte)
                        //   bytes 1-8: value (8 bytes, packed)
                        // Must read the embedded type tag and value separately.
                        if (field_type_id == LMD_TYPE_ANY_) {
                            uint8_t stored_type = *(uint8_t*)field_ptr;
                            if (stored_type >= LMD_TYPE_INT64_ && stored_type != LMD_TYPE_BOOL_ &&
                                byte_offset + 1 + 8 <= byte_size) {
                                uint64_t val = *(uint64_t*)((uint8_t*)field_ptr + 1);
                                if (val != 0) {
                                    if (stored_type >= LMD_TYPE_RANGE_) {
                                        gc_mark_item(gc, val);
                                    } else {
                                        void* embedded_ptr = (void*)(uintptr_t)val;
                                        if (is_gc_object(gc, embedded_ptr)) {
                                            gc_header_t* h = gc_get_header(embedded_ptr);
                                            if (h && !h->marked && !(h->gc_flags & GC_FLAG_FREED)) {
                                                h->marked = 1;
                                                mark_stack_push(gc, h);
                                            }
                                        }
                                    }
                                }
                            }
                        } else {
                        // read as pointer-sized value (Item or pointer depending on type)
                        uint64_t val = *(uint64_t*)field_ptr;
                        if (val != 0) {
                            if (field_type_id >= LMD_TYPE_RANGE_) {
                                // container pointer stored directly
                                gc_mark_item(gc, val);
                            } else if (field_type_id == LMD_TYPE_STRING_ ||
                                       field_type_id == LMD_TYPE_SYMBOL_ ||
                                       field_type_id == LMD_TYPE_DECIMAL_ ||
                                       field_type_id == LMD_TYPE_INT64_ ||
                                       field_type_id == LMD_TYPE_FLOAT_ ||
                                       field_type_id == LMD_TYPE_DTIME_) {
                                // these are stored as raw pointers in the data buffer
                                // they need to be marked if they're GC-managed
                                void* embedded_ptr = (void*)(uintptr_t)val;
                                if (is_gc_object(gc, embedded_ptr)) {
                                    gc_header_t* h = gc_get_header(embedded_ptr);
                                    if (h && !h->marked && !(h->gc_flags & GC_FLAG_FREED)) {
                                        h->marked = 1;
                                        mark_stack_push(gc, h);
                                    }
                                }
                            }
                        }
                        } // end non-ANY
                    }
                }
            }
            shape = (uint8_t*)next_shape;
        }
        gc_trace_data_words(gc, data_ptr, byte_size);
        break;
    }

    case LMD_TYPE_ELEMENT_: {
        // Element extends List: { Container(2), Item*items(8@8), length(8@16),
        //   extra(8@24), capacity(8@32), type*(8@40), data*(8@48), data_cap(4@56) }
        uint8_t* p = (uint8_t*)obj;
        void* items_ptr = *(void**)(p + 8);
        int64_t length = *(int64_t*)(p + 16);
        int64_t capacity = *(int64_t*)(p + 32);
        void* type_ptr = *(void**)(p + 40);
        void* data_ptr = *(void**)(p + 48);
        int64_t dense_count = length < capacity ? length : capacity;

        // trace children items
        if (items_ptr && dense_count > 0) {
            uint64_t* items = (uint64_t*)items_ptr;
            for (int64_t i = 0; i < dense_count; i++) {
                gc_mark_item(gc, items[i]);
            }
        }

        // trace attributes (same shape-walk as Map)
        if (type_ptr && data_ptr) {
            uint8_t* tp = (uint8_t*)type_ptr;
            int64_t byte_size = *(int64_t*)(tp + 16);
            void* shape_ptr = *(void**)(tp + 32);
            uint8_t* shape = (uint8_t*)shape_ptr;
            while (shape) {
                void* field_type = *(void**)(shape + 8);
                int64_t byte_offset = *(int64_t*)(shape + 16);
                void* next_shape = *(void**)(shape + 24);
                if (field_type) {
                    uint8_t ftid = *(uint8_t*)field_type;
                    if (ftid >= LMD_TYPE_INT64_ && ftid != LMD_TYPE_BOOL_ &&
                        byte_offset >= 0 && byte_offset < byte_size) {
                        // Handle LMD_TYPE_ANY fields: 9-byte TypedItem layout
                        if (ftid == LMD_TYPE_ANY_) {
                            uint8_t* fptr = (uint8_t*)data_ptr + byte_offset;
                            uint8_t stored_type = *fptr;
                            if (stored_type >= LMD_TYPE_INT64_ && stored_type != LMD_TYPE_BOOL_ &&
                                byte_offset + 1 + 8 <= byte_size) {
                                uint64_t val = *(uint64_t*)(fptr + 1);
                                if (val != 0) {
                                    if (stored_type >= LMD_TYPE_RANGE_) {
                                        gc_mark_item(gc, val);
                                    } else {
                                        void* ep = (void*)(uintptr_t)val;
                                        if (is_gc_object(gc, ep)) {
                                            gc_header_t* h = gc_get_header(ep);
                                            if (h && !h->marked && !(h->gc_flags & GC_FLAG_FREED)) {
                                                h->marked = 1;
                                                mark_stack_push(gc, h);
                                            }
                                        }
                                    }
                                }
                            }
                        } else {
                        uint64_t val = *(uint64_t*)((uint8_t*)data_ptr + byte_offset);
                        if (val != 0) {
                            if (ftid >= LMD_TYPE_RANGE_) {
                                gc_mark_item(gc, val);
                            } else {
                                void* embedded_ptr = (void*)(uintptr_t)val;
                                if (is_gc_object(gc, embedded_ptr)) {
                                    gc_header_t* h = gc_get_header(embedded_ptr);
                                    if (h && !h->marked && !(h->gc_flags & GC_FLAG_FREED)) {
                                        h->marked = 1;
                                        mark_stack_push(gc, h);
                                    }
                                }
                            }
                        }
                        } // end non-ANY
                    }
                }
                shape = (uint8_t*)next_shape;
            }
            gc_trace_data_words(gc, data_ptr, byte_size);
        }
        break;
    }

    case LMD_TYPE_FUNC_: {
        if (gc->js_function_trace && gc->js_function_trace(obj, gc)) break;
        // Function: { type_id(1), arity(1), closure_field_count(1@2), padding(5),
        //             fn_type*(8@8), ptr*(8@16), closure_env*(8@24), name*(8@32) }
        uint8_t* p = (uint8_t*)obj;
        uint8_t field_count = *(uint8_t*)(p + 2);  // closure_field_count
        void* closure_env = *(void**)(p + 24);
        if (closure_env && field_count > 0) {
            // scan closure env as packed Item fields
            uint64_t* env_items = (uint64_t*)closure_env;
            for (int i = 0; i < (int)field_count; i++) {
                gc_mark_item(gc, env_items[i]);
            }
        }
        break;
    }

    case LMD_TYPE_VMAP_: {
        // VMap: { Container(2), data*(8@8), vtable*(8@16) }
        // VMap entries (Item keys/values) are stored in a malloc'd HashMap.
        // Delegate tracing to the runtime-provided callback.
        uint8_t* p = (uint8_t*)obj;
        void* data = *(void**)(p + 8);
        if (data && gc->vmap_trace) {
            gc->vmap_trace(data, gc);
        }
        break;
    }

    default:
        break;
    }
}

static int gc_drain_mark_stack(gc_heap_t* gc) {
    int traced_count = 0;
    while (gc->mark_top > 0) {
        gc_header_t* obj = mark_stack_pop(gc);
        gc_trace_object(gc, obj);
        traced_count++;
    }
    return traced_count;
}

// ---- Data Zone Compaction ----

// Fix up embedded float/int64/datetime pointers within a compacted items[] buffer.
// array_set() stores float/int64/datetime values at the END of the items[] buffer
// (the "extra" area), with tagged pointers in items[0..length) pointing into
// that same buffer. After copying the buffer to a new address, those embedded
// pointers still reference the old location and must be updated.
static void gc_fixup_embedded_pointers(uint64_t* old_items, uint64_t* new_items,
                                        int64_t length, int64_t capacity) {
    // Compute the old buffer extent: [old_start, old_end)
    uint8_t* old_start = (uint8_t*)old_items;
    uint8_t* old_end   = old_start + capacity * sizeof(uint64_t);
    ptrdiff_t offset    = (uint8_t*)new_items - (uint8_t*)old_items;

    for (int64_t i = 0; i < length; i++) {
        uint64_t item = new_items[i];
        uint8_t item_tag = (uint8_t)(item >> 56);

        // Only fix tagged pointer types that array_set stores inline:
        // Float (5), Int64 (4), DateTime (8)
        if (item_tag == LMD_TYPE_FLOAT_ ||
            (item_tag == LMD_TYPE_INT64_ && !(item & ITEM_INT64_INLINE_MARK)) ||
            item_tag == LMD_TYPE_DTIME_) {
            // Extract the raw pointer (lower 56 bits)
            uint8_t* ptr = (uint8_t*)(uintptr_t)(item & 0x00FFFFFFFFFFFFFFULL);
            if (ptr >= old_start && ptr < old_end) {
                // Pointer is inside the old buffer — rebase to new location
                uint8_t* new_ptr = ptr + offset;
                new_items[i] = (item & 0xFF00000000000000ULL) |  // ITEM_TAG_LITERAL_OK: preserves existing boxed-pointer high-byte tag.
                               ((uint64_t)(uintptr_t)new_ptr & 0x00FFFFFFFFFFFFFFULL);
            }
        }
    }
}

static size_t gc_array_num_elem_bytes(uint8_t elem_type) {
    // ELEM_TYPE_SIZE is a C++ runtime table; keep this C collector table keyed
    // by the same high-nibble representation class.
    static const uint8_t elem_size_idx[16] = {
        8, 8, 1, 2, 4, 8, 1, 2, 4, 8, 2, 4, 8, 1, 1, 0
    };
    size_t elem_bytes = elem_size_idx[(elem_type >> 4) & 0xF];
    return elem_bytes ? elem_bytes : 8;
}

static int gc_compact_array_num_owned_data(gc_heap_t* gc, uint8_t* array) {
    if (!gc || !array || (array[2] & 0x02)) return 0;  // views borrow base storage
    void** data_slot = (void**)(array + 8);
    int64_t capacity = *(int64_t*)(array + 32);
    if (!*data_slot || capacity < 0 ||
            !gc_data_zone_owns(gc->data_zone, *data_slot)) {
        return 0;
    }

    size_t elem_bytes = gc_array_num_elem_bytes(array[3]);
    if ((uint64_t)capacity > SIZE_MAX / elem_bytes) return 0;
    size_t size = (size_t)capacity * elem_bytes;
    void* new_data = gc_data_zone_copy(gc->tenured_data, *data_slot, size);
    if (!new_data) return 0;
    *data_slot = new_data;
    return 1;
}

static int gc_rebind_array_num_gc_view(gc_heap_t* gc, uint8_t* view) {
    if (!gc || !view || !(view[2] & 0x02)) return 0;
    ArrayNumShape* shape = *(ArrayNumShape**)(view + 24);
    if (!shape || shape->backing_kind != ARRAY_NUM_BACKING_GC_VIEW ||
            !shape->base) {
        return 0;
    }

    uint8_t* base = (uint8_t*)shape->base;
    if (base[0] != LMD_TYPE_ARRAY_NUM_) return 0;
    int compacted = gc_compact_array_num_owned_data(gc, base);

    size_t elem_bytes = gc_array_num_elem_bytes(view[3]);
    if (shape->offset < 0 || (uint64_t)shape->offset > SIZE_MAX / elem_bytes) {
        log_error("gc-array-num-view: invalid element offset %lld",
            (long long)shape->offset);
        *(void**)(view + 8) = NULL;
        return compacted;
    }
    uint8_t* base_data = *(uint8_t**)(base + 8);
    *(void**)(view + 8) = base_data ?
        base_data + (size_t)shape->offset * elem_bytes : NULL;
    return compacted;
}

// Compact surviving data from nursery data zone to tenured data zone.
// For each marked object that has data pointers into the nursery data zone,
// copy the data to tenured and update the object's pointer.
static void gc_compact_data(gc_heap_t* gc) {
    gc_header_t* current = gc->all_objects;
#ifndef NDEBUG
    size_t compacted = 0;
#endif

    while (current) {
        if (!current->marked || (current->gc_flags & GC_FLAG_FREED)) {
            current = current->next;
            continue;
        }

        void* obj = (void*)(current + 1);
        uint16_t tag = current->type_tag;

        switch (tag) {
        case LMD_TYPE_ARRAY_: {
            uint8_t* p = (uint8_t*)obj;
            void** items_slot = (void**)(p + 8);
            int64_t length = *(int64_t*)(p + 16);
            int64_t extra = *(int64_t*)(p + 24);
            int64_t capacity = *(int64_t*)(p + 32);
            if (*items_slot && gc_data_zone_owns(gc->data_zone, *items_slot)) {
                uint64_t* old_items = (uint64_t*)*items_slot;
                size_t size = capacity * sizeof(uint64_t); // sizeof(Item)
                void* new_items = gc_data_zone_copy(gc->tenured_data, *items_slot, size);
                if (new_items) {
                    *items_slot = new_items;
                    // Fix embedded float/int64/datetime pointers that reference
                    // the old buffer's extra area (set by array_set)
                    if (extra > 0) {
                        int64_t dense_limit = capacity >= extra ? capacity - extra : 0;
                        int64_t dense_count = length < dense_limit ? length : dense_limit;
                        gc_fixup_embedded_pointers(old_items, (uint64_t*)new_items,
                                                   dense_count, capacity);
                    }
#ifndef NDEBUG
                    compacted++;
#endif
                }
            }
            break;
        }
        case LMD_TYPE_ARRAY_NUM_: {
            uint8_t* p = (uint8_t*)obj;
            uint8_t array_flags = p[2];
            // N-D shape descriptors live in the nursery data zone just like
            // element buffers. Promote them before resetting that zone or a
            // live array keeps a dangling `extra` pointer after collection.
            if (array_flags & 0x01) {  // Container.is_ndim
                void** shape_slot = (void**)(p + 24);
                if (*shape_slot && gc_data_zone_owns(gc->data_zone, *shape_slot)) {
                    ArrayNumShape* old_shape = (ArrayNumShape*)*shape_slot;
                    if (old_shape->ndim >= 1 && old_shape->ndim <= 32) {
                        size_t shape_size = sizeof(ArrayNumShape) +
                            2 * (size_t)old_shape->ndim * sizeof(int64_t);
                        void* new_shape = gc_data_zone_copy(
                            gc->tenured_data, old_shape, shape_size);
                        if (new_shape) {
                            *shape_slot = new_shape;
#ifndef NDEBUG
                            compacted++;
#endif
                        }
                    }
                }
            }
            if (array_flags & 0x02) {
                // A pinned nursery buffer would still be discarded by the reset.
                // Promote the ultimate owner and rebuild this cached alias instead.
                int moved = gc_rebind_array_num_gc_view(gc, p);
#ifndef NDEBUG
                compacted += (size_t)moved;
#endif
                (void)moved;
                break;
            }
            int moved = gc_compact_array_num_owned_data(gc, p);
#ifndef NDEBUG
            compacted += (size_t)moved;
#endif
            (void)moved;
            break;
        }
        case LMD_TYPE_MAP_:
        case LMD_TYPE_OBJECT_: {
            uint8_t* p = (uint8_t*)obj;
            void** data_slot = (void**)(p + 16);   // map->data
            void* type_ptr = *(void**)(p + 8);      // map->type
            if (*data_slot && gc_data_zone_owns(gc->data_zone, *data_slot) && type_ptr) {
                int64_t byte_size = *(int64_t*)((uint8_t*)type_ptr + 16);
                if (byte_size > 0) {
                    void* new_data = gc_data_zone_copy(gc->tenured_data, *data_slot, byte_size);
                    if (new_data) {
                        *data_slot = new_data;
#ifndef NDEBUG
                        compacted++;
#endif
                    }
                }
            }
            break;
        }
        case LMD_TYPE_ELEMENT_: {
            uint8_t* p = (uint8_t*)obj;
            // compact items[]
            void** items_slot = (void**)(p + 8);
            int64_t elmt_length = *(int64_t*)(p + 16);
            int64_t elmt_extra = *(int64_t*)(p + 24);
            int64_t elmt_capacity = *(int64_t*)(p + 32);
            if (*items_slot && gc_data_zone_owns(gc->data_zone, *items_slot)) {
                uint64_t* old_elmt_items = (uint64_t*)*items_slot;
                size_t size = elmt_capacity * sizeof(uint64_t);
                void* new_items = gc_data_zone_copy(gc->tenured_data, *items_slot, size);
                if (new_items) {
                    *items_slot = new_items;
                    // Fix embedded float/int64/datetime pointers
                    if (elmt_extra > 0) {
                        int64_t dense_count = elmt_length < elmt_capacity ? elmt_length : elmt_capacity;
                        gc_fixup_embedded_pointers(old_elmt_items, (uint64_t*)new_items,
                                                   dense_count, elmt_capacity);
                    }
#ifndef NDEBUG
                    compacted++;
#endif
                }
            }
            // compact data
            void** data_slot = (void**)(p + 48);
            void* type_ptr = *(void**)(p + 40);
            if (*data_slot && gc_data_zone_owns(gc->data_zone, *data_slot) && type_ptr) {
                int64_t byte_size = *(int64_t*)((uint8_t*)type_ptr + 16);
                if (byte_size > 0) {
                    void* new_data = gc_data_zone_copy(gc->tenured_data, *data_slot, byte_size);
                    if (new_data) {
                        *data_slot = new_data;
#ifndef NDEBUG
                        compacted++;
#endif
                    }
                }
            }
            break;
        }
        case LMD_TYPE_FUNC_: {
            uint8_t* p = (uint8_t*)obj;
            uint8_t field_count = *(uint8_t*)(p + 2);  // closure_field_count
            void** env_slot = (void**)(p + 24);
            if (*env_slot && field_count > 0 && gc_data_zone_owns(gc->data_zone, *env_slot)) {
                size_t size = (size_t)field_count * sizeof(uint64_t);
                void* new_env = gc_data_zone_copy(gc->tenured_data, *env_slot, size);
                if (new_env) {
                    *env_slot = new_env;
#ifndef NDEBUG
                    compacted++;
#endif
                }
            }
            break;
        }
        default:
            break;
        }

        current = current->next;
    }

    log_debug("gc_compact_data: compacted %zu data buffers to tenured", compacted);
}

// ---- Sweep Phase ----

// Finalize a dead object's sub-allocations that are NOT in the data zone
// (e.g., mpd_t from libmpdec, VMap backing data from HashMap).
static void gc_finalize_dead_object(gc_heap_t* gc, gc_header_t* header) {
    void* obj = (void*)(header + 1);
    uint16_t tag = header->type_tag;

    if (gc->external_destroy) {
        // Refcounted bytes live outside the GC data zone, so dead owners must
        // release them during sweep instead of deferring cleanup to teardown.
        gc->external_destroy(obj, tag);
    }

    if (tag == LMD_TYPE_DECIMAL_) {
        // Decimal: mpd_t is allocated by libmpdec (outside GC)
        // We can't call mpd_del here from C code — it requires mpdecimal.h
        // This will be handled by gc_finalize_all_objects in lambda-mem.cpp
        // during context teardown. During mid-execution GC, decimals that
        // are truly dead will have their mpd_t leaked until context end.
        // TODO: Add a finalization callback mechanism.
    }
    else if (tag == LMD_TYPE_VMAP_) {
        // VMap host payload cleanup is independent of optional lazy backing data.
        uint8_t* p = (uint8_t*)obj;
        void* data = *(void**)(p + 8);
        if (gc->vmap_destroy) {
            gc->vmap_destroy(obj, data);
            *(void**)(p + 8) = NULL;  // prevent double-free at context teardown
        }
    }
    else if (tag == LMD_TYPE_ERROR_) {
        if (gc->error_destroy) {
            gc->error_destroy(obj);
        }
    }
    else if (tag == LMD_TYPE_MAP_) {
        uint8_t* p = (uint8_t*)obj;
        uint8_t map_kind = p[3];
        if (map_kind == MAP_KIND_ARRAY_SPARSE_) {
            gc_free_sparse_array_map_entries(obj);
        }
        if (map_kind == MAP_KIND_ITERATOR_ || map_kind == MAP_KIND_PROXY_) {
            void* data = *(void**)(p + 16);
            if (data) {
                mem_free(data);
                *(void**)(p + 16) = NULL;
            }
        }
    }
    // Other types: sub-allocations (items[], data, closure_env) are in data zone
    // and will be reclaimed by data zone reset. No explicit free needed.
}

static void gc_poison_dead_object(gc_heap_t* gc, gc_header_t* header) {
    if (!gc || !header || !gc->poison_freed || header->alloc_size == 0) return;
    // Finalization must observe the original fields. Poison afterward but
    // before storage reuse so a missed exact root cannot keep reading a
    // plausible stale object from retained bump blocks or zone slabs.
    log_debug("gc-poison-freed-object: collection=%zu object=%p type=%u size=%u flags=0x%02x",
        gc->collections + 1, (void*)(header + 1), (unsigned)header->type_tag,
        (unsigned)header->alloc_size, (unsigned)header->gc_flags);
    memset((void*)(header + 1), GC_FREED_POISON_BYTE, header->alloc_size);
}

static void gc_sweep(gc_heap_t* gc) {
    gc_header_t* current = gc->all_objects;
    gc_header_t* prev = NULL;
    size_t freed_count = 0;
    size_t freed_bytes = 0;
    size_t walked_count = 0;
    size_t alive_count = 0;
    size_t already_freed_count = 0;
    size_t bump_owned_count = 0;
    size_t object_zone_owned_count = 0;
    size_t large_owned_count = 0;
    while (current) {
        walked_count++;
        gc_header_t* next_obj = current->next;

        if (current->gc_flags & GC_FLAG_FREED) {
            already_freed_count++;
            // already freed — unlink from list and return to free list
            if (prev) prev->next = next_obj;
            else gc->all_objects = next_obj;
            // all_objects only contains headers produced by the GC allocation
            // paths, so header flags are enough to classify the owner.
            if (current->gc_flags & GC_FLAG_LARGE) {
                large_owned_count++;
                gc_large_object_remove(gc, current);
                free(current);
            } else if (current->gc_flags & GC_FLAG_BUMP) {
                bump_owned_count++;
                // bump-block objects are reclaimed only by unlinking; block memory is pool-owned
            } else {
                object_zone_owned_count++;
                gc_object_zone_free(gc->object_zone, current);
            }
            current = next_obj;
            continue;
        }

        if (current->marked) {
            // alive — reset mark for next cycle
            alive_count++;
            current->marked = 0;
            prev = current;
        } else {
            // dead — finalize external sub-allocations
            gc_finalize_dead_object(gc, current);
            gc_poison_dead_object(gc, current);

            // unlink from all_objects
            if (prev) prev->next = next_obj;
            else gc->all_objects = next_obj;

            freed_bytes += sizeof(gc_header_t) + current->alloc_size;
            freed_count++;

            // return to the owning allocation arena.
            if (current->gc_flags & GC_FLAG_LARGE) {
                large_owned_count++;
                // large objects allocated with malloc — free directly
                gc_large_object_remove(gc, current);
                free(current);
            } else if (current->gc_flags & GC_FLAG_BUMP) {
                bump_owned_count++;
                // Retained bump blocks remain address-discoverable after sweep.
                // Mark dead headers freed so a stale exact root cannot enqueue
                // and trace their poisoned payload in a later collection.
                current->gc_flags |= GC_FLAG_FREED;
            } else {
                object_zone_owned_count++;
                gc_object_zone_free(gc->object_zone, current);
            }
        }

        current = next_obj;
    }

    gc->object_count -= freed_count;
    gc->total_allocated -= freed_bytes;
    gc->bytes_collected += freed_bytes;

    GC_PROFILE_COUNT("gc_sweep_walked_objects", walked_count);
    GC_PROFILE_COUNT("gc_sweep_alive_objects", alive_count);
    GC_PROFILE_COUNT("gc_sweep_dead_objects", freed_count);
    GC_PROFILE_COUNT("gc_sweep_already_freed", already_freed_count);
    GC_PROFILE_COUNT("gc_sweep_bump_owned", bump_owned_count);
    GC_PROFILE_COUNT("gc_sweep_object_zone_owned", object_zone_owned_count);
    GC_PROFILE_COUNT("gc_sweep_large_owned", large_owned_count);
    GC_PROFILE_COUNT("gc_sweep_freed_bytes", freed_bytes);

    log_debug("gc_sweep: freed %zu objects (%zu bytes), %zu remain",
              freed_count, freed_bytes, gc->object_count);
}

// ============================================================================
// gc_collect: Full Collection Cycle
// ============================================================================

// Conservative stack scanning: scan C stack for potential Item values.
// Treats each aligned 8-byte value as a potential tagged pointer and checks
// if it points to a GC-managed object.
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define GC_NO_SANITIZE_ADDRESS __attribute__((no_sanitize("address")))
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) && !defined(GC_NO_SANITIZE_ADDRESS)
#define GC_NO_SANITIZE_ADDRESS __attribute__((no_sanitize_address))
#endif
#ifndef GC_NO_SANITIZE_ADDRESS
#define GC_NO_SANITIZE_ADDRESS
#endif

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define GC_ASAN_ENABLED 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define GC_ASAN_ENABLED 1
#endif
#ifndef GC_ASAN_ENABLED
#define GC_ASAN_ENABLED 0
#endif

#if GC_CONSERVATIVE_SCAN_AVAILABLE && GC_ASAN_ENABLED
extern int __asan_address_is_poisoned(const volatile void *addr);

static int gc_asan_word_is_poisoned(const uintptr_t* p) {
    const unsigned char* bytes = (const unsigned char*)p;
    for (size_t i = 0; i < sizeof(uintptr_t); i++) {
        if (__asan_address_is_poisoned(bytes + i)) return 1;
    }
    return 0;
}
#endif

static void gc_root_stats_reset(gc_heap_t* gc) {
    memset(&gc->last_root_stats, 0, sizeof(gc->last_root_stats));
    gc->root_type_stat_count = 0;
}

#if GC_CONSERVATIVE_SCAN_AVAILABLE
static void gc_root_stats_note_type(gc_heap_t* gc, uint16_t type_tag) {
    for (int i = 0; i < gc->root_type_stat_count; i++) {
        if (gc->root_type_stats[i].type_tag == type_tag) {
            gc->root_type_stats[i].count++;
            return;
        }
    }
    if (gc->root_type_stat_count >= gc->root_type_stat_capacity) {
        int next_capacity = gc->root_type_stat_capacity
            ? gc->root_type_stat_capacity * 2 : 16;
        gc_root_type_stat_t* next = (gc_root_type_stat_t*)realloc(
            gc->root_type_stats,
            (size_t)next_capacity * sizeof(gc_root_type_stat_t));
        if (!next) {
            log_error("gc-root-shadow: failed to grow per-type statistics");
            return;
        }
        gc->root_type_stats = next;
        gc->root_type_stat_capacity = next_capacity;
    }
    gc_root_type_stat_t* stat =
        &gc->root_type_stats[gc->root_type_stat_count++];
    stat->type_tag = type_tag;
    stat->count = 1;
}

static GC_NO_SANITIZE_ADDRESS void gc_scan_stack(gc_heap_t* gc,
        uintptr_t stack_base, uintptr_t stack_current) {
    if (stack_base == 0 || stack_current == 0) return;
    if (stack_current >= stack_base) return;  // stack grows down; current < base

    // scan from current SP up to stack base (stack grows downward)
    uintptr_t aligned_start = (stack_current + sizeof(uintptr_t) - 1) & ~(uintptr_t)(sizeof(uintptr_t) - 1);
    uintptr_t aligned_end = stack_base & ~(uintptr_t)(sizeof(uintptr_t) - 1);
    uintptr_t* scan_start = (uintptr_t*)aligned_start;
    uintptr_t* scan_end = (uintptr_t*)aligned_end;
    int scanned = 0;
    int marked = 0;
    int skipped_poisoned = 0;

    gc->last_root_stats.conservative_scan_bytes =
        (size_t)(aligned_end - aligned_start);

    for (uintptr_t* p = scan_start; p < scan_end; p++) {
#if GC_ASAN_ENABLED
        if (gc_asan_word_is_poisoned(p)) {
            skipped_poisoned++;
            continue;
        }
#endif
        uint64_t val = (uint64_t)*p;
        scanned++;
        // try to interpret as a tagged Item
        void* ptr = item_to_ptr(val);
        if (!ptr) continue;
        // check if it points to a GC-managed object
        if (is_gc_object(gc, ptr)) {
            gc_header_t* header = gc_get_header(ptr);
            gc->last_root_stats.conservative_candidate_words++;
            if (header && !header->marked && !(header->gc_flags & GC_FLAG_FREED)) {
                header->marked = 1;
                mark_stack_push(gc, header);
                marked++;
                gc->last_root_stats.conservative_new_objects++;
                if (gc->root_mode == GC_ROOT_MODE_SHADOW_VERIFY) {
                    gc_root_stats_note_type(gc, header->type_tag);
                    log_info("gc-root-shadow-candidate: collection=%zu stack_slot=%p object=%p type=%u size=%u",
                        gc->collections + 1, (void*)p, ptr,
                        (unsigned)header->type_tag, (unsigned)header->alloc_size);
                }
            }
        }
    }

    gc->last_root_stats.conservative_skipped_poisoned_words =
        (size_t)skipped_poisoned;

    log_debug("gc_scan_stack: scanned %d slots (%zu bytes), marked %d objects, skipped %d ASan-poisoned slots",
              scanned, (size_t)(aligned_end - aligned_start), marked, skipped_poisoned);
    (void)scanned; (void)marked; (void)skipped_poisoned;  // silence release-build unused warning
}
#endif

void gc_collect_with_root_region(gc_heap_t* gc, uint64_t* extra_roots,
                int extra_count,
#if GC_CONSERVATIVE_SCAN_AVAILABLE
                uintptr_t stack_base, uintptr_t stack_current,
#endif
                uint64_t* root_base, int64_t root_count) {
    if (!gc) return;
    gc_assert_allocation_allowed(gc, "gc_collect_with_root_region");
    if (gc->collecting) {
        log_debug("gc_collect: skipping re-entrant collection");
        return;
    }
    uint64_t gc_total_token = GC_PROFILE_ENTER("gc_collect_total");
    gc->collecting = 1;

    log_debug("gc_collect: starting collection #%zu (%zu objects, %zu bytes, data_zone=%zu)",
              gc->collections + 1, gc->object_count, gc->total_allocated,
              gc_data_zone_used(gc->data_zone));

    size_t bump_block_count = 0;
    for (gc_bump_block_t* block = gc->bump_blocks; block; block = block->next) {
        bump_block_count++;
    }
    GC_PROFILE_COUNT("gc_collect_start_objects", gc->object_count);
    GC_PROFILE_COUNT("gc_collect_bump_blocks", bump_block_count);
    if (gc->object_zone) {
        GC_PROFILE_COUNT("gc_collect_range_count", gc->object_zone->range_count);
        GC_PROFILE_COUNT("gc_collect_slab_count", gc->object_zone->slab_count);
    }

    // reset mark stack
    gc->mark_top = 0;
    gc_root_stats_reset(gc);

    // Phase 1a: Mark registered root slots (BSS globals, context->result, etc.)
    uint64_t gc_roots_token = GC_PROFILE_ENTER("gc_mark_roots");
    for (int i = 0; i < gc->root_slot_count; i++) {
        if (gc->root_slots[i]) {
            gc_mark_item(gc, *gc->root_slots[i]);
        }
    }

    // Phase 1a2: Mark registered root ranges (JS closure env arrays)
    for (int i = 0; i < gc->root_range_count; i++) {
        uint64_t* base = gc->root_ranges[i].base;
        int count = gc->root_ranges[i].count;
        if (base) {
            for (int j = 0; j < count; j++) {
                gc_mark_item(gc, base[j]);
            }
        }
    }
    GC_PROFILE_LEAVE("gc_mark_roots", gc_roots_token);

    // The side root stack is already a dense Item region. Its live watermark
    // makes this scan precise without registering or walking unused capacity.
    if (root_base && root_count > 0) {
        for (int64_t i = 0; i < root_count; i++) {
            gc_mark_item(gc, root_base[i]);
        }
    }

    // Phase 1b: Mark explicit extra roots (caller-provided Items)
    uint64_t gc_extra_roots_token = GC_PROFILE_ENTER("gc_mark_extra_roots");
    if (extra_roots && extra_count > 0) {
        for (int i = 0; i < extra_count; i++) {
            gc_mark_item(gc, extra_roots[i]);
        }
    }
    GC_PROFILE_LEAVE("gc_mark_extra_roots", gc_extra_roots_token);

    // Trace the complete graph reachable from precise roots before shadow
    // scanning. Otherwise a child of a precise root would be misreported as a
    // stack-exclusive candidate merely because its parent had not run yet.
    gc->last_root_stats.precise_root_count = (size_t)gc->mark_top;
    uint64_t gc_trace_token = GC_PROFILE_ENTER("gc_trace_objects");
    int traced_count = gc_drain_mark_stack(gc);
    GC_PROFILE_LEAVE("gc_trace_objects", gc_trace_token);

    // Phase 1c: Conservative stack scan. Precise-only is explicit and never
    // falls back to scanning when a root is absent.
    if (gc->root_mode != GC_ROOT_MODE_PRECISE_ONLY) {
#if GC_CONSERVATIVE_SCAN_AVAILABLE
        uint64_t gc_stack_token = GC_PROFILE_ENTER("gc_scan_stack");
        gc_scan_stack(gc, stack_base, stack_current);
        GC_PROFILE_LEAVE("gc_scan_stack", gc_stack_token);

        gc_trace_token = GC_PROFILE_ENTER("gc_trace_objects");
        traced_count += gc_drain_mark_stack(gc);
        GC_PROFILE_LEAVE("gc_trace_objects", gc_trace_token);
#else
        // This state can only arise from memory corruption or a caller that
        // bypassed gc_set_root_mode(); continuing would collect live objects.
        log_error("gc-root-mode: compatibility collection reached scanner-free build");
        abort();
#endif
    }

    if (gc->root_mode == GC_ROOT_MODE_SHADOW_VERIFY) {
        log_info("gc-root-shadow: collection=%zu precise_roots=%zu candidate_words=%zu new_objects=%zu scan_bytes=%zu",
            gc->collections + 1, gc->last_root_stats.precise_root_count,
            gc->last_root_stats.conservative_candidate_words,
            gc->last_root_stats.conservative_new_objects,
            gc->last_root_stats.conservative_scan_bytes);
    } else if (gc->root_mode == GC_ROOT_MODE_PRECISE_ONLY) {
        log_debug("gc-root-precise: collection=%zu precise_roots=%zu conservative_scan=skipped",
            gc->collections + 1, gc->last_root_stats.precise_root_count);
    }
    log_debug("gc_collect: traced %d objects total", traced_count);
    // Release builds compile debug logging away; retain the accounting above
    // without turning its diagnostic-only result into an unused-variable error.
    (void)traced_count;

    // Weak slots observe the completed reachability graph but must clear
    // before sweep reuses dead object storage.
    gc_process_weak_slots(gc);
    gc_weak_slots_processed();

    // Measure nursery and tenured usage before compaction for adaptive threshold
    size_t nursery_used_before = gc_data_zone_used(gc->data_zone);
    size_t tenured_before = gc_data_zone_used(gc->tenured_data);

    // Phase 2: Compact — copy surviving data zone buffers to tenured
    uint64_t gc_compact_token = GC_PROFILE_ENTER("gc_compact_data");
    gc_compact_data(gc);
    GC_PROFILE_LEAVE("gc_compact_data", gc_compact_token);

    // Phase 3: Sweep — free dead objects, return to free lists
    uint64_t gc_sweep_token = GC_PROFILE_ENTER("gc_sweep");
    gc_sweep(gc);
    GC_PROFILE_LEAVE("gc_sweep", gc_sweep_token);

    // Phase 4: Reset nursery data zone (all surviving data copied to tenured)
    uint64_t gc_reset_token = GC_PROFILE_ENTER("gc_data_zone_reset");
    gc_data_zone_reset(gc->data_zone);
    GC_PROFILE_LEAVE("gc_data_zone_reset", gc_reset_token);

    gc->collections++;
    gc->collecting = 0;

    // Adaptive threshold: measure how much nursery data survived compaction.
    // If collection freed very little nursery data, it was unproductive and
    // caused cache thrashing for no benefit — grow threshold to delay next GC.
    size_t tenured_after = gc_data_zone_used(gc->tenured_data);
    size_t survived_this_cycle = tenured_after - tenured_before;
    size_t freed_this_cycle = (nursery_used_before > survived_this_cycle)
                            ? (nursery_used_before - survived_this_cycle) : 0;
    if (nursery_used_before > 0) {
        size_t freed_pct = (freed_this_cycle * 100) / nursery_used_before;
        size_t new_threshold = gc->gc_threshold;
        if (freed_pct < 40) {
            // unproductive: < 40% freed — grow 4x
            new_threshold = gc->gc_threshold * 4;
        } else if (freed_pct < 75) {
            // somewhat unproductive: < 75% freed — grow 2x
            new_threshold = gc->gc_threshold * 2;
        }
        // cap at 256 MB to prevent runaway growth
        if (new_threshold > 256 * 1024 * 1024) new_threshold = 256 * 1024 * 1024;
        if (new_threshold > gc->gc_threshold) {
            log_debug("gc_collect: adaptive threshold %zu -> %zu (freed=%zu/%zu=%zu%%, survived=%zu)",
                      gc->gc_threshold, new_threshold, freed_this_cycle, nursery_used_before,
                      freed_pct, survived_this_cycle);
            gc->gc_threshold = new_threshold;
        }
    }

    log_debug("gc_collect: collection #%zu complete, %zu objects remain, %zu bytes collected total",
              gc->collections, gc->object_count, gc->bytes_collected);
    GC_PROFILE_LEAVE("gc_collect_total", gc_total_token);
}

#if GC_CONSERVATIVE_SCAN_AVAILABLE
void gc_collect(gc_heap_t* gc, uint64_t* extra_roots, int extra_count,
                uintptr_t stack_base, uintptr_t stack_current) {
    gc_collect_with_root_region(gc, extra_roots, extra_count,
                                stack_base, stack_current, NULL, 0);
#else
void gc_collect(gc_heap_t* gc, uint64_t* extra_roots, int extra_count) {
    gc_collect_with_root_region(gc, extra_roots, extra_count, NULL, 0);
#endif
}
