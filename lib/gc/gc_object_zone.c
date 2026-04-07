/**
 * gc_object_zone.c - Size-Class Free-List Allocator Implementation
 *
 * Allocates fixed-size object structs from size-class segregated slabs.
 * Objects are never moved — Item tagged pointers remain stable.
 */
#include "gc_object_zone.h"
#include "gc_heap.h"   // for full gc_header_t definition and GC_FLAG_FREED
#include "../log.h"
#include <stdlib.h>
#include <string.h>

// User data size classes (not including gc_header_t)
static const size_t SIZE_CLASSES[GC_NUM_SIZE_CLASSES] = {16, 32, 48, 64, 96, 128, 256};

// Slab configuration: number of slots per slab for each size class
// Smaller objects get more slots per slab; larger objects fewer.
// Total slab memory = slot_size * slots_per_slab
static const size_t SLOTS_PER_SLAB[GC_NUM_SIZE_CLASSES] = {
    512,    // 16B class: 512 * 32 = 16 KB per slab
    256,    // 32B class: 256 * 48 = 12 KB per slab
    256,    // 48B class: 256 * 64 = 16 KB per slab
    128,    // 64B class: 128 * 80 = 10 KB per slab
    128,    // 96B class: 128 * 112 = 14 KB per slab
    64,     // 128B class: 64 * 144 = 9 KB per slab
    32,     // 256B class: 32 * 272 = 8.5 KB per slab
};

// find the smallest size class that fits the requested size
int gc_object_zone_class_index(size_t size) {
    for (int i = 0; i < GC_NUM_SIZE_CLASSES; i++) {
        if (size <= SIZE_CLASSES[i]) return i;
    }
    return -1; // too large for object zone
}

size_t gc_object_zone_class_size(int cls) {
    if (cls < 0 || cls >= GC_NUM_SIZE_CLASSES) return 0;
    return SIZE_CLASSES[cls];
}

// register a slab's address range for fast binary-search ownership lookup
static void register_slab_range(gc_object_zone_t* oz, uint8_t* base, size_t bytes) {
    uint8_t* end = base + bytes;

    // update global min/max bounds
    if (!oz->min_addr || base < oz->min_addr) oz->min_addr = base;
    if (!oz->max_addr || end > oz->max_addr) oz->max_addr = end;

    // grow array if needed
    if (oz->range_count >= oz->range_capacity) {
        size_t new_cap = oz->range_capacity ? oz->range_capacity * 2 : GC_INITIAL_RANGE_CAPACITY;
        gc_slab_range_t* new_ranges = (gc_slab_range_t*)realloc(oz->slab_ranges,
            new_cap * sizeof(gc_slab_range_t));
        if (!new_ranges) {
            log_error("gc_object_zone: failed to grow slab_ranges array");
            return;
        }
        oz->slab_ranges = new_ranges;
        oz->range_capacity = new_cap;
    }

    // binary search for insertion point to keep array sorted by base
    size_t lo = 0, hi = oz->range_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (oz->slab_ranges[mid].base < base) lo = mid + 1;
        else hi = mid;
    }

    // shift elements right to make room
    if (lo < oz->range_count) {
        memmove(&oz->slab_ranges[lo + 1], &oz->slab_ranges[lo],
                (oz->range_count - lo) * sizeof(gc_slab_range_t));
    }

    oz->slab_ranges[lo].base = base;
    oz->slab_ranges[lo].end = end;
    oz->range_count++;
}

// allocate a new slab for the given size class from the pool
static gc_object_slab_t* allocate_slab(gc_object_zone_t* oz, int cls) {
    size_t slot_size = sizeof(gc_header_t) + SIZE_CLASSES[cls];
    size_t slot_count = SLOTS_PER_SLAB[cls];
    size_t slab_bytes = slot_size * slot_count;

    // allocate slab memory from pool
    uint8_t* memory = (uint8_t*)pool_alloc(oz->pool, slab_bytes);
    if (!memory) {
        log_error("gc_object_zone: failed to allocate slab of %zu bytes", slab_bytes);
        return NULL;
    }
    memset(memory, 0, slab_bytes);

    // allocate slab metadata (from C heap — small, few slabs)
    gc_object_slab_t* slab = (gc_object_slab_t*)calloc(1, sizeof(gc_object_slab_t));
    if (!slab) {
        log_error("gc_object_zone: failed to allocate slab metadata");
        return NULL;
    }

    slab->base = memory;
    slab->slot_size = slot_size;
    slab->slot_count = slot_count;
    slab->next_fresh = 0;
    slab->next = NULL;

    oz->slab_count++;
    register_slab_range(oz, memory, slab_bytes);
    log_debug("gc_object_zone: allocated slab class=%d slot_size=%zu slots=%zu bytes=%zu",
              cls, slot_size, slot_count, slab_bytes);
    return slab;
}

gc_object_zone_t* gc_object_zone_create(Pool* pool) {
    gc_object_zone_t* oz = (gc_object_zone_t*)calloc(1, sizeof(gc_object_zone_t));
    if (!oz) {
        log_error("gc_object_zone_create: failed to allocate zone");
        return NULL;
    }
    oz->pool = pool;

    // pre-allocate one slab per size class
    for (int i = 0; i < GC_NUM_SIZE_CLASSES; i++) {
        oz->free_lists[i] = NULL;
        oz->slabs[i] = allocate_slab(oz, i);
        if (!oz->slabs[i]) {
            log_error("gc_object_zone_create: failed to allocate initial slab for class %d", i);
            gc_object_zone_destroy(oz);
            return NULL;
        }
    }

    log_debug("gc_object_zone_create: created with %d size classes", GC_NUM_SIZE_CLASSES);
    return oz;
}

void gc_object_zone_destroy(gc_object_zone_t* oz) {
    if (!oz) return;
    // free slab metadata (slab memory is pool-allocated, freed by pool_destroy)
    for (int i = 0; i < GC_NUM_SIZE_CLASSES; i++) {
        gc_object_slab_t* slab = oz->slabs[i];
        while (slab) {
            gc_object_slab_t* next = slab->next;
            free(slab);
            slab = next;
        }
    }
    // free sorted range array
    free(oz->slab_ranges);
    log_debug("gc_object_zone_destroy: freed %zu slabs, %zu allocs, %zu frees",
              oz->slab_count, oz->total_slots_allocated, oz->total_slots_freed);
    free(oz);
}

void* gc_object_zone_alloc(gc_object_zone_t* oz, size_t size, uint16_t type_tag,
                           gc_header_t** all_objects) {
    int cls = gc_object_zone_class_index(size);
    if (cls < 0) {
        // size exceeds object zone — fall through to caller for large object path
        return NULL;
    }
    size_t actual_user_size = SIZE_CLASSES[cls];

    gc_header_t* header = NULL;

    // 1. Try free list first (O(1) — pop from head)
    if (oz->free_lists[cls]) {
        header = oz->free_lists[cls];
        oz->free_lists[cls] = header->next;
        // zero the user data area (header will be re-initialized below)
        memset((void*)(header + 1), 0, actual_user_size);
    } else {
        // 2. Try fresh slot from current slab
        gc_object_slab_t* slab = oz->slabs[cls];

        // find a slab with free sequential slots
        while (slab && slab->next_fresh >= slab->slot_count) {
            slab = slab->next;
        }

        if (!slab) {
            // 3. All slabs full — allocate a new slab
            slab = allocate_slab(oz, cls);
            if (!slab) return NULL;
            // prepend new slab to chain
            slab->next = oz->slabs[cls];
            oz->slabs[cls] = slab;
        }

        // allocate from sequential slot
        header = (gc_header_t*)(slab->base + slab->next_fresh * slab->slot_size);
        slab->next_fresh++;
        // memory already zeroed from slab allocation
    }

    // initialize header
    header->type_tag = type_tag;
    header->gc_flags = 0;
    header->marked = 0;
    header->alloc_size = (uint32_t)size;  // original requested size

    // link into all_objects list
    header->next = *all_objects;
    *all_objects = header;

    oz->total_slots_allocated++;

    return (void*)(header + 1);
}

void* gc_object_zone_alloc_class(gc_object_zone_t* oz, int cls, size_t size,
                                  uint16_t type_tag, gc_header_t** all_objects) {
    size_t actual_user_size = SIZE_CLASSES[cls];

    gc_header_t* header = NULL;

    // 1. Try free list first (O(1) — pop from head)
    if (oz->free_lists[cls]) {
        header = oz->free_lists[cls];
        oz->free_lists[cls] = header->next;
        // zero the user data area (header will be re-initialized below)
        memset((void*)(header + 1), 0, actual_user_size);
    } else {
        // 2. Try fresh slot from current slab
        gc_object_slab_t* slab = oz->slabs[cls];

        // find a slab with free sequential slots
        while (slab && slab->next_fresh >= slab->slot_count) {
            slab = slab->next;
        }

        if (!slab) {
            // 3. All slabs full — allocate a new slab
            slab = allocate_slab(oz, cls);
            if (!slab) return NULL;
            // prepend new slab to chain
            slab->next = oz->slabs[cls];
            oz->slabs[cls] = slab;
        }

        // allocate from sequential slot
        header = (gc_header_t*)(slab->base + slab->next_fresh * slab->slot_size);
        slab->next_fresh++;
        // memory already zeroed from slab allocation
    }

    // initialize header
    header->type_tag = type_tag;
    header->gc_flags = 0;
    header->marked = 0;
    header->alloc_size = (uint32_t)size;  // original requested size

    // link into all_objects list
    header->next = *all_objects;
    *all_objects = header;

    oz->total_slots_allocated++;

    return (void*)(header + 1);
}

void gc_object_zone_free(gc_object_zone_t* oz, gc_header_t* header) {
    if (!oz || !header) return;

    int cls = gc_object_zone_class_index(header->alloc_size);
    if (cls < 0) {
        // large object — shouldn't be here, but be safe
        log_error("gc_object_zone_free: alloc_size %u exceeds size classes", header->alloc_size);
        return;
    }

    // push to free list (reuse header->next as free list link)
    header->gc_flags = GC_FLAG_FREED;
    header->next = oz->free_lists[cls];
    oz->free_lists[cls] = header;
    oz->total_slots_freed++;
}

int gc_object_zone_owns(gc_object_zone_t* oz, void* ptr) {
    if (!oz || !ptr) return 0;
    uint8_t* p = (uint8_t*)ptr;

    // Fast rejection: check against global min/max address bounds
    if (p < oz->min_addr || p >= oz->max_addr) return 0;

    // Binary search the sorted slab_ranges array for a range containing p
    size_t lo = 0, hi = oz->range_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (oz->slab_ranges[mid].end <= p) {
            lo = mid + 1;
        } else if (oz->slab_ranges[mid].base > p) {
            hi = mid;
        } else {
            // p >= base && p < end — found
            return 1;
        }
    }
    return 0;
}
