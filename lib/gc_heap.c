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
#include "log.h"
#include <stdlib.h>
#include <string.h>

// Initial mark stack capacity (grows if needed)
#define GC_MARK_STACK_INITIAL 4096

// ============================================================================
// Type identification helpers (match lambda.h TypeId values)
// These must match the EnumTypeId enum in lambda.h exactly.
// We declare the values we need rather than including lambda.h from C.
// ============================================================================
#define LMD_TYPE_RAW_POINTER_ 0
#define LMD_TYPE_NULL_     1
#define LMD_TYPE_BOOL_     2
#define LMD_TYPE_INT_      3
#define LMD_TYPE_INT64_    4
#define LMD_TYPE_FLOAT_    5
#define LMD_TYPE_DECIMAL_  6
#define LMD_TYPE_NUMBER_   7
#define LMD_TYPE_DTIME_    8
#define LMD_TYPE_SYMBOL_   9
#define LMD_TYPE_STRING_  10
#define LMD_TYPE_BINARY_  11
#define LMD_TYPE_LIST_    12
#define LMD_TYPE_RANGE_   13
#define LMD_TYPE_ARRAY_INT_ 14
#define LMD_TYPE_ARRAY_INT64_ 15
#define LMD_TYPE_ARRAY_FLOAT_ 16
#define LMD_TYPE_ARRAY_   17
#define LMD_TYPE_MAP_     18
#define LMD_TYPE_VMAP_    19
#define LMD_TYPE_ELEMENT_ 20
#define LMD_TYPE_OBJECT_  21
#define LMD_TYPE_TYPE_    22
#define LMD_TYPE_FUNC_    23
#define LMD_TYPE_ANY_     24
#define LMD_TYPE_ERROR_   25
#define LMD_TYPE_PATH_    27

// ============================================================================
// Lifecycle
// ============================================================================

gc_heap_t* gc_heap_create(void) {
    gc_heap_t* gc = (gc_heap_t*)calloc(1, sizeof(gc_heap_t));
    if (!gc) {
        log_error("gc_heap_create: failed to allocate gc_heap");
        return NULL;
    }
    gc->pool = pool_create();
    if (!gc->pool) {
        log_error("gc_heap_create: failed to create pool");
        free(gc);
        return NULL;
    }

    // create object zone (size-class free-list allocator)
    gc->object_zone = gc_object_zone_create(gc->pool);
    if (!gc->object_zone) {
        log_error("gc_heap_create: failed to create object zone");
        pool_destroy(gc->pool);
        free(gc);
        return NULL;
    }

    // create nursery data zone (bump allocator for variable-size buffers)
    gc->data_zone = gc_data_zone_create(gc->pool, GC_DATA_ZONE_BLOCK_SIZE);
    if (!gc->data_zone) {
        log_error("gc_heap_create: failed to create data zone");
        gc_object_zone_destroy(gc->object_zone);
        pool_destroy(gc->pool);
        free(gc);
        return NULL;
    }

    // create tenured data zone (survivors go here during GC compaction)
    gc->tenured_data = gc_data_zone_create(gc->pool, GC_DATA_ZONE_BLOCK_SIZE);
    if (!gc->tenured_data) {
        log_error("gc_heap_create: failed to create tenured data zone");
        gc_data_zone_destroy(gc->data_zone);
        gc_object_zone_destroy(gc->object_zone);
        pool_destroy(gc->pool);
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
    memset(gc->root_slots, 0, sizeof(gc->root_slots));

    // collection trigger
    gc->gc_threshold = GC_DATA_ZONE_THRESHOLD;
    gc->collecting = 0;
    gc->collect_callback = NULL;

    // VMap callbacks (set by runtime via lambda-mem.cpp)
    gc->vmap_trace = NULL;
    gc->vmap_destroy = NULL;

    log_debug("gc_heap_create: dual-zone GC heap created (threshold=%zu)", gc->gc_threshold);
    return gc;
}

void gc_heap_destroy(gc_heap_t* gc) {
    if (!gc) return;

    // free mark stack (C heap allocated)
    if (gc->mark_stack) {
        free(gc->mark_stack);
        gc->mark_stack = NULL;
    }

    // free zone metadata (zone memory is pool-allocated, freed by pool_destroy)
    if (gc->object_zone) gc_object_zone_destroy(gc->object_zone);
    if (gc->data_zone) gc_data_zone_destroy(gc->data_zone);
    if (gc->tenured_data) gc_data_zone_destroy(gc->tenured_data);

    // pool_destroy bulk-frees all pool-allocated memory
    if (gc->pool) pool_destroy(gc->pool);

    log_debug("gc_heap_destroy: %zu objects, %zu collections, %zu bytes collected",
              gc->object_count, gc->collections, gc->bytes_collected);
    free(gc);
}

// ============================================================================
// Allocation
// ============================================================================

void* gc_heap_alloc(gc_heap_t* gc, size_t size, uint16_t type_tag) {
    if (!gc || !gc->pool) {
        log_error("gc_heap_alloc: invalid gc_heap");
        return NULL;
    }

    // try object zone first (for objects up to GC_LARGE_OBJECT_THRESHOLD)
    void* ptr = gc_object_zone_alloc(gc->object_zone, size, type_tag, &gc->all_objects);
    if (ptr) {
        gc->total_allocated += sizeof(gc_header_t) + size;
        gc->object_count++;
        return ptr;
    }

    // large object: fall back to direct pool allocation with GCHeader
    size_t total = sizeof(gc_header_t) + size;
    gc_header_t* header = (gc_header_t*)pool_alloc(gc->pool, total);
    if (!header) {
        log_error("gc_heap_alloc: pool_alloc failed for %zu bytes (large object)", total);
        return NULL;
    }
    // initialize header
    header->next = gc->all_objects;
    header->type_tag = type_tag;
    header->gc_flags = 0;
    header->marked = 0;
    header->alloc_size = (uint32_t)size;

    // link to all_objects list
    gc->all_objects = header;
    gc->total_allocated += total;
    gc->object_count++;

    return (void*)(header + 1);
}

void* gc_heap_calloc(gc_heap_t* gc, size_t size, uint16_t type_tag) {
    void* ptr = gc_heap_alloc(gc, size, type_tag);
    if (ptr) {
        // Object zone allocations (size <= GC_LARGE_OBJECT_THRESHOLD = 256) already
        // return zeroed memory from either fresh slab slots (zeroed at slab creation)
        // or free-list pop (memset in gc_object_zone_alloc). Only large objects
        // allocated via pool_alloc need explicit zeroing.
        if (size > GC_LARGE_OBJECT_THRESHOLD) {
            memset(ptr, 0, size);
        }
    }
    return ptr;
}

void* gc_heap_calloc_class(gc_heap_t* gc, size_t size, uint16_t type_tag, int cls) {
    // Fast path: skip gc_heap_alloc → gc_object_zone_alloc class_index lookup.
    // The class index is pre-computed by the JIT at compile time.
    void* ptr = gc_object_zone_alloc_class(gc->object_zone, cls, size, type_tag,
                                            &gc->all_objects);
    if (ptr) {
        gc->total_allocated += sizeof(gc_header_t) + size;
        gc->object_count++;
    }
    // No large-object fallback needed — JIT only uses this for known small sizes.
    // No memset needed — object zone returns zeroed memory.
    return ptr;
}

void gc_heap_pool_free(gc_heap_t* gc, void* ptr) {
    if (!ptr || !gc) return;
    gc_header_t* header = gc_get_header(ptr);
    // guard against double-free
    if (header->gc_flags & GC_FLAG_FREED) return;
    header->gc_flags |= GC_FLAG_FREED;
    gc->total_allocated -= sizeof(gc_header_t) + header->alloc_size;
    gc->object_count--;
}

void* gc_data_alloc(gc_heap_t* gc, size_t size) {
    if (!gc || !gc->data_zone || size == 0) return NULL;

    // check if data zone usage exceeds threshold — trigger GC before allocating
    if (!gc->collecting && gc->collect_callback) {
        size_t used = gc_data_zone_used(gc->data_zone);
        if (used >= gc->gc_threshold) {
            log_debug("gc_data_alloc: threshold exceeded (%zu >= %zu), triggering GC",
                      used, gc->gc_threshold);
            gc->collect_callback();
        }
    }

    return gc_data_zone_alloc(gc->data_zone, size);
}

// ============================================================================
// Ownership queries
// ============================================================================

int gc_is_managed(gc_heap_t* gc, void* ptr) {
    if (!gc || !ptr) return 0;
    // check object zone
    if (gc_object_zone_owns(gc->object_zone, ptr)) return 1;
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

void gc_register_root(gc_heap_t* gc, uint64_t* slot) {
    if (!gc || !slot) return;
    if (gc->root_slot_count >= GC_MAX_ROOT_SLOTS) {
        log_error("gc_register_root: root slot table full (%d slots)", GC_MAX_ROOT_SLOTS);
        return;
    }
    // check for duplicate
    for (int i = 0; i < gc->root_slot_count; i++) {
        if (gc->root_slots[i] == slot) return;
    }
    gc->root_slots[gc->root_slot_count++] = slot;
    log_debug("gc_register_root: registered slot %p (total: %d)", (void*)slot, gc->root_slot_count);
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
//   - Error/Any: _type_id = 25 or 24, lower bits = 0 → no pointer.
static void* item_to_ptr(uint64_t item) {
    if (item == 0) return NULL;  // all-zero value

    uint8_t tag = (uint8_t)(item >> 56);

    // tag 1 = NULL item, tag 2 = BOOL (inline), tag 3 = INT (inline)
    if (tag >= LMD_TYPE_NULL_ && tag <= LMD_TYPE_INT_) return NULL;

    // tag 0: container pointer (raw heap address, high byte = 0 on 64-bit systems)
    if (tag == 0) {
        return (void*)(uintptr_t)item;
    }

    // tags 4-11: tagged pointer types (Int64, Float, Decimal, DateTime, String, etc.)
    // mask off the tag byte in the upper 8 bits to get the actual pointer
    if (tag >= LMD_TYPE_INT64_ && tag <= LMD_TYPE_BINARY_) {
        return (void*)(uintptr_t)(item & 0x00FFFFFFFFFFFFFF);
    }

    // tags >= 12 (containers with tag in high byte): shouldn't normally occur on
    // mainstream 64-bit platforms where heap pointers have 0 in the high byte.
    // But for safety, treat as raw pointer.
    if (tag >= LMD_TYPE_LIST_) {
        return (void*)(uintptr_t)item;
    }

    // anything else (ERROR=25 with null pointer, ANY=24, etc.)
    // extract pointer — if lower 56 bits are 0, returns NULL
    void* ptr = (void*)(uintptr_t)(item & 0x00FFFFFFFFFFFFFF);
    return ptr;
}

// check if a pointer is to a GC-managed OBJECT (has GCHeader)
static int is_gc_object(gc_heap_t* gc, void* ptr) {
    return gc_object_zone_owns(gc->object_zone, ptr);
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

// trace outgoing Item pointers from a type-aware object
// This is the core tracing logic that knows Lambda's struct layouts.
static void gc_trace_object(gc_heap_t* gc, gc_header_t* header) {
    void* obj = (void*)(header + 1);
    uint16_t tag = header->type_tag;

    switch (tag) {
    // types with no outgoing Item pointers — nothing to trace
    case LMD_TYPE_INT64_:
    case LMD_TYPE_FLOAT_:
    case LMD_TYPE_DTIME_:
    case LMD_TYPE_STRING_:
    case LMD_TYPE_SYMBOL_:
    case LMD_TYPE_BINARY_:
    case LMD_TYPE_DECIMAL_:
    case LMD_TYPE_RANGE_:
    case LMD_TYPE_ARRAY_INT_:
    case LMD_TYPE_ARRAY_INT64_:
    case LMD_TYPE_ARRAY_FLOAT_:
    case LMD_TYPE_PATH_:
        break;

    case LMD_TYPE_LIST_:
    case LMD_TYPE_ARRAY_: {
        // List/Array: items is an Item* array
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
        void* items_ptr = *(void**)(p + 8);   // Item* items
        int64_t length = *(int64_t*)(p + 16);  // length
        if (items_ptr && length > 0) {
            uint64_t* items = (uint64_t*)items_ptr;
            for (int64_t i = 0; i < length; i++) {
                gc_mark_item(gc, items[i]);
            }
        }
        break;
    }

    case LMD_TYPE_MAP_:
    case LMD_TYPE_OBJECT_: {
        // Map/Object: { Container(8), type*(8@8), data*(8@16), data_cap(4@24) }
        uint8_t* p = (uint8_t*)obj;
        void* type_ptr = *(void**)(p + 8);    // TypeMap*
        void* data_ptr = *(void**)(p + 16);   // data buffer
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
        int field_idx = 0;
        while (shape) {
            void* field_type = *(void**)(shape + 8);   // ShapeEntry.type (Type*)
            int64_t byte_offset = *(int64_t*)(shape + 16);  // ShapeEntry.byte_offset
            void* next_shape = *(void**)(shape + 24);  // ShapeEntry.next

            if (field_type) {
                uint8_t field_type_id = *(uint8_t*)field_type;  // Type.type_id
                // only trace Item-typed fields (containers, strings, etc.)
                // Skip inline values (bool, int) which don't hold GC pointers
                if (field_type_id >= LMD_TYPE_INT64_ && field_type_id != LMD_TYPE_BOOL_) {
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
                                    if (stored_type >= LMD_TYPE_LIST_) {
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
                            if (field_type_id >= LMD_TYPE_LIST_) {
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
            field_idx++;
            shape = (uint8_t*)next_shape;
        }
        break;
    }

    case LMD_TYPE_ELEMENT_: {
        // Element extends List: { Container(2), Item*items(8@8), length(8@16),
        //   extra(8@24), capacity(8@32), type*(8@40), data*(8@48), data_cap(4@56) }
        uint8_t* p = (uint8_t*)obj;
        void* items_ptr = *(void**)(p + 8);
        int64_t length = *(int64_t*)(p + 16);
        void* type_ptr = *(void**)(p + 40);
        void* data_ptr = *(void**)(p + 48);

        // trace children items
        if (items_ptr && length > 0) {
            uint64_t* items = (uint64_t*)items_ptr;
            for (int64_t i = 0; i < length; i++) {
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
                                    if (stored_type >= LMD_TYPE_LIST_) {
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
                            if (ftid >= LMD_TYPE_LIST_) {
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
        }
        break;
    }

    case LMD_TYPE_FUNC_: {
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
        if (item_tag == LMD_TYPE_FLOAT_ || item_tag == LMD_TYPE_INT64_ ||
            item_tag == LMD_TYPE_DTIME_) {
            // Extract the raw pointer (lower 56 bits)
            uint8_t* ptr = (uint8_t*)(uintptr_t)(item & 0x00FFFFFFFFFFFFFFULL);
            if (ptr >= old_start && ptr < old_end) {
                // Pointer is inside the old buffer — rebase to new location
                uint8_t* new_ptr = ptr + offset;
                new_items[i] = (item & 0xFF00000000000000ULL) |
                               ((uint64_t)(uintptr_t)new_ptr & 0x00FFFFFFFFFFFFFFULL);
            }
        }
    }
}

// Compact surviving data from nursery data zone to tenured data zone.
// For each marked object that has data pointers into the nursery data zone,
// copy the data to tenured and update the object's pointer.
static void gc_compact_data(gc_heap_t* gc) {
    gc_header_t* current = gc->all_objects;
    size_t compacted = 0;

    while (current) {
        if (!current->marked || (current->gc_flags & GC_FLAG_FREED)) {
            current = current->next;
            continue;
        }

        void* obj = (void*)(current + 1);
        uint16_t tag = current->type_tag;

        switch (tag) {
        case LMD_TYPE_LIST_:
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
                        gc_fixup_embedded_pointers(old_items, (uint64_t*)new_items,
                                                   length, capacity);
                    }
                    compacted++;
                }
            }
            break;
        }
        case LMD_TYPE_ARRAY_INT_:
        case LMD_TYPE_ARRAY_INT64_: {
            uint8_t* p = (uint8_t*)obj;
            void** items_slot = (void**)(p + 8);
            int64_t capacity = *(int64_t*)(p + 32);
            if (*items_slot && gc_data_zone_owns(gc->data_zone, *items_slot)) {
                size_t size = capacity * sizeof(int64_t);
                void* new_items = gc_data_zone_copy(gc->tenured_data, *items_slot, size);
                if (new_items) {
                    *items_slot = new_items;
                    compacted++;
                }
            }
            break;
        }
        case LMD_TYPE_ARRAY_FLOAT_: {
            uint8_t* p = (uint8_t*)obj;
            void** items_slot = (void**)(p + 8);
            int64_t capacity = *(int64_t*)(p + 32);
            if (*items_slot && gc_data_zone_owns(gc->data_zone, *items_slot)) {
                size_t size = capacity * sizeof(double);
                void* new_items = gc_data_zone_copy(gc->tenured_data, *items_slot, size);
                if (new_items) {
                    *items_slot = new_items;
                    compacted++;
                }
            }
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
                        compacted++;
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
                        gc_fixup_embedded_pointers(old_elmt_items, (uint64_t*)new_items,
                                                   elmt_length, elmt_capacity);
                    }
                    compacted++;
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
                        compacted++;
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
                    compacted++;
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

    if (tag == LMD_TYPE_DECIMAL_) {
        // Decimal: mpd_t is allocated by libmpdec (outside GC)
        // We can't call mpd_del here from C code — it requires mpdecimal.h
        // This will be handled by gc_finalize_all_objects in lambda-mem.cpp
        // during context teardown. During mid-execution GC, decimals that
        // are truly dead will have their mpd_t leaked until context end.
        // TODO: Add a finalization callback mechanism.
    }
    else if (tag == LMD_TYPE_VMAP_) {
        // VMap: backing data is malloc'd by HashMap implementation.
        // Free it immediately during sweep so dead VMaps don't leak.
        uint8_t* p = (uint8_t*)obj;
        void* data = *(void**)(p + 8);
        if (data && gc->vmap_destroy) {
            gc->vmap_destroy(data);
            *(void**)(p + 8) = NULL;  // prevent double-free at context teardown
        }
    }
    // Other types: sub-allocations (items[], data, closure_env) are in data zone
    // and will be reclaimed by data zone reset. No explicit free needed.
}

static void gc_sweep(gc_heap_t* gc) {
    gc_header_t* current = gc->all_objects;
    gc_header_t* prev = NULL;
    size_t freed_count = 0;
    size_t freed_bytes = 0;

    while (current) {
        gc_header_t* next_obj = current->next;

        if (current->gc_flags & GC_FLAG_FREED) {
            // already freed — unlink from list and return to free list
            if (prev) prev->next = next_obj;
            else gc->all_objects = next_obj;
            // return to object zone free list for reuse
            if (gc_object_zone_owns(gc->object_zone, (void*)(current + 1))) {
                gc_object_zone_free(gc->object_zone, current);
            }
            current = next_obj;
            continue;
        }

        if (current->marked) {
            // alive — reset mark for next cycle
            current->marked = 0;
            prev = current;
        } else {
            // dead — finalize external sub-allocations
            gc_finalize_dead_object(gc, current);

            // unlink from all_objects
            if (prev) prev->next = next_obj;
            else gc->all_objects = next_obj;

            freed_bytes += sizeof(gc_header_t) + current->alloc_size;
            freed_count++;

            // return to object zone free list
            if (gc_object_zone_owns(gc->object_zone, (void*)(current + 1))) {
                gc_object_zone_free(gc->object_zone, current);
            }
            // large objects: memory stays in pool until pool_destroy
        }

        current = next_obj;
    }

    gc->object_count -= freed_count;
    gc->total_allocated -= freed_bytes;
    gc->bytes_collected += freed_bytes;

    log_debug("gc_sweep: freed %zu objects (%zu bytes), %zu remain",
              freed_count, freed_bytes, gc->object_count);
}

// ============================================================================
// gc_collect: Full Collection Cycle
// ============================================================================

// Conservative stack scanning: scan C stack for potential Item values.
// Treats each aligned 8-byte value as a potential tagged pointer and checks
// if it points to a GC-managed object.
static void gc_scan_stack(gc_heap_t* gc, uintptr_t stack_base, uintptr_t stack_current) {
    if (stack_base == 0 || stack_current == 0) return;
    if (stack_current >= stack_base) return;  // stack grows down; current < base

    // scan from current SP up to stack base (stack grows downward)
    uintptr_t* scan_start = (uintptr_t*)stack_current;
    uintptr_t* scan_end = (uintptr_t*)stack_base;
    int scanned = 0;
    int marked = 0;

    for (uintptr_t* p = scan_start; p < scan_end; p++) {
        uint64_t val = (uint64_t)*p;
        scanned++;
        // try to interpret as a tagged Item
        void* ptr = item_to_ptr(val);
        if (!ptr) continue;
        // check if it points to a GC-managed object
        if (is_gc_object(gc, ptr)) {
            gc_header_t* header = gc_get_header(ptr);
            if (header && !header->marked && !(header->gc_flags & GC_FLAG_FREED)) {
                header->marked = 1;
                mark_stack_push(gc, header);
                marked++;
            }
        }
    }

    log_debug("gc_scan_stack: scanned %d slots (%zu bytes), marked %d objects",
              scanned, (size_t)(stack_base - stack_current), marked);
}

void gc_collect(gc_heap_t* gc, uint64_t* extra_roots, int extra_count,
                uintptr_t stack_base, uintptr_t stack_current) {
    if (!gc) return;
    if (gc->collecting) {
        log_debug("gc_collect: skipping re-entrant collection");
        return;
    }
    gc->collecting = 1;

    log_debug("gc_collect: starting collection #%zu (%zu objects, %zu bytes, data_zone=%zu)",
              gc->collections + 1, gc->object_count, gc->total_allocated,
              gc_data_zone_used(gc->data_zone));

    // reset mark stack
    gc->mark_top = 0;

    // Phase 1a: Mark registered root slots (BSS globals, context->result, etc.)
    for (int i = 0; i < gc->root_slot_count; i++) {
        if (gc->root_slots[i]) {
            gc_mark_item(gc, *gc->root_slots[i]);
        }
    }

    // Phase 1b: Mark explicit extra roots (caller-provided Items)
    if (extra_roots && extra_count > 0) {
        for (int i = 0; i < extra_count; i++) {
            gc_mark_item(gc, extra_roots[i]);
        }
    }

    // Phase 1c: Conservative stack scan
    gc_scan_stack(gc, stack_base, stack_current);

    // Phase 1d: Process mark stack (trace gray objects until empty)
    int traced_count = 0;
    while (gc->mark_top > 0) {
        gc_header_t* obj = mark_stack_pop(gc);
        gc_trace_object(gc, obj);
        traced_count++;
        // tracing may have pushed more objects; continue until empty
    }
    log_debug("gc_collect: traced %d objects total", traced_count);

    // Measure nursery and tenured usage before compaction for adaptive threshold
    size_t nursery_used_before = gc_data_zone_used(gc->data_zone);
    size_t tenured_before = gc_data_zone_used(gc->tenured_data);

    // Phase 2: Compact — copy surviving data zone buffers to tenured
    gc_compact_data(gc);

    // Phase 3: Sweep — free dead objects, return to free lists
    gc_sweep(gc);

    // Phase 4: Reset nursery data zone (all surviving data copied to tenured)
    gc_data_zone_reset(gc->data_zone);

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
}
