/**
 * gc_object_zone.c - Size-Class Free-List Allocator Implementation
 *
 * Allocates fixed-size object structs from size-class segregated slabs.
 * Objects are never moved — Item tagged pointers remain stable.
 */
#include "gc_object_zone.h"
#include "gc_heap.h"   // for full gc_header_t definition and GC_FLAG_FREED
#define MEMTRACK_NO_LOCATION_MACROS
#include "../../../lib/memtrack.h"
#include "../../../lib/log.h"
#include <stdlib.h>
#include <string.h>

// User data size classes (not including gc_header_t)
static const size_t SIZE_CLASSES[GC_NUM_SIZE_CLASSES] = {
    16, 32, 48, 64, 96, 128, 256, 384
};

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
    32,     // 384B class: 32 * 400 = 12.5 KB per slab
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

// ---- ownership page index ----------------------------------------------

static size_t page_index_hash(uintptr_t page, unsigned bits) {
    return (size_t)((page * UINT64_C(0x9E3779B97F4A7C15)) >> (64 - bits));
}

// the slot holding `page`, or the empty slot where it belongs
static gc_range_page_t* page_index_slot(gc_range_page_t* slots, unsigned bits, uintptr_t page) {
    size_t mask = ((size_t)1 << bits) - 1;
    for (size_t i = page_index_hash(page, bits);; i = (i + 1) & mask) {
        if (slots[i].page == page || slots[i].page == 0) return &slots[i];
    }
}

static bool page_index_grow(gc_object_zone_t* oz) {
    unsigned bits = oz->page_index_bits ? oz->page_index_bits + 1 : 10;
    if (bits >= 48) return false;
    gc_range_page_t* slots = (gc_range_page_t*)mem_calloc((size_t)1 << bits,
        sizeof(gc_range_page_t), MEM_CAT_CONTAINER);
    if (!slots) return false;
    if (oz->page_index) {
        size_t old_cap = (size_t)1 << oz->page_index_bits;
        for (size_t i = 0; i < old_cap; i++) {
            if (oz->page_index[i].page)
                *page_index_slot(slots, bits, oz->page_index[i].page) = oz->page_index[i];
        }
        mem_free(oz->page_index);
    }
    oz->page_index = slots;
    oz->page_index_bits = bits;
    return true;
}

// Index every page of a newly published range. An incomplete index would
// wrongly reject owned addresses, so a failed grow retires it for good.
static void page_index_add_range(gc_object_zone_t* oz, uint8_t* base, uint8_t* end,
                                 gc_range_owner_t owner) {
    if (oz->page_index_disabled) return;
    if (!oz->page_shift) {
        size_t page_size = mem_vm_page_size();
        unsigned shift = 12;
        while (((size_t)1 << (shift + 1)) <= page_size) shift++;
        oz->page_shift = shift;
    }
    uintptr_t first = (uintptr_t)base >> oz->page_shift;
    uintptr_t last = ((uintptr_t)end - 1) >> oz->page_shift;
    for (uintptr_t page = first; page <= last; page++) {
        if (!oz->page_index ||
                (oz->page_index_count + 1) * 2 > ((size_t)1 << oz->page_index_bits)) {
            if (!page_index_grow(oz)) {
                log_error("gc_object_zone: page index disabled after allocation failure");
                mem_free(oz->page_index);
                oz->page_index = NULL;
                oz->page_index_disabled = true;
                return;
            }
        }
        gc_range_page_t* slot = page_index_slot(oz->page_index, oz->page_index_bits, page);
        if (slot->page) { slot->shared = true; continue; }
        slot->page = page;
        slot->base = base;
        slot->end = end;
        slot->owner = owner;
        oz->page_index_count++;
    }
}

static const gc_range_page_t* page_index_find(const gc_object_zone_t* oz, uint8_t* p) {
    uintptr_t page = (uintptr_t)p >> oz->page_shift;
    gc_range_page_t* slot = page_index_slot(oz->page_index, oz->page_index_bits, page);
    return slot->page ? slot : NULL;
}

// register a slab's address range for fast binary-search ownership lookup.
// `slab` may be NULL for non-slab ranges (bump blocks registered by gc_heap.c),
// which carry the heap's `foreign` owner instead.
static bool register_slab_range(gc_object_zone_t* oz, uint8_t* base, size_t bytes,
                                gc_object_slab_t* slab, void* foreign) {
    uint8_t* end = base + bytes;

    // Grow the paired arrays transactionally. A successful resize of only one
    // side would detach slab ownership from its binary-search range.
    if (oz->range_count >= oz->range_capacity) {
        if (oz->range_capacity > SIZE_MAX / 2) {
            log_error("gc_object_zone: range capacity overflow");
            return false;
        }
        size_t new_cap = oz->range_capacity ? oz->range_capacity * 2 : GC_INITIAL_RANGE_CAPACITY;
        if (new_cap > SIZE_MAX / sizeof(gc_slab_range_t) ||
            new_cap > SIZE_MAX / sizeof(gc_range_owner_t)) {
            log_error("gc_object_zone: range array size overflow");
            return false;
        }

        gc_slab_range_t* new_ranges = (gc_slab_range_t*)mem_alloc(
            new_cap * sizeof(gc_slab_range_t), MEM_CAT_CONTAINER);
        gc_range_owner_t* new_owners = (gc_range_owner_t*)mem_alloc(
            new_cap * sizeof(gc_range_owner_t), MEM_CAT_CONTAINER);
        if (!new_ranges || !new_owners) {
            mem_free(new_ranges);
            mem_free(new_owners);
            log_error("gc_object_zone: failed to grow paired slab range arrays");
            return false;
        }

        if (oz->range_count) {
            memcpy(new_ranges, oz->slab_ranges,
                   oz->range_count * sizeof(gc_slab_range_t));
            memcpy(new_owners, oz->range_owners,
                   oz->range_count * sizeof(gc_range_owner_t));
        }
        mem_free(oz->slab_ranges);
        mem_free(oz->range_owners);
        oz->slab_ranges = new_ranges;
        oz->range_owners = new_owners;
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
        memmove(&oz->range_owners[lo + 1], &oz->range_owners[lo],
                (oz->range_count - lo) * sizeof(gc_range_owner_t));
    }

    oz->slab_ranges[lo].base = base;
    oz->slab_ranges[lo].end = end;
    oz->range_owners[lo].slab = slab;
    oz->range_owners[lo].foreign = foreign;
    oz->range_count++;
    page_index_add_range(oz, base, end, oz->range_owners[lo]);
    oz->recent_range.base = NULL;

    // Publish bounds only after the paired entry is fully present.
    if (!oz->min_addr || base < oz->min_addr) oz->min_addr = base;
    if (!oz->max_addr || end > oz->max_addr) oz->max_addr = end;
    return true;
}

// Public entry for gc_heap.c to register a non-slab ownership range (bump block).
bool gc_object_zone_register_range(gc_object_zone_t* oz, uint8_t* base, size_t bytes,
                                   void* owner) {
    if (!oz || !base || bytes == 0) return false;
    return register_slab_range(oz, base, bytes, NULL, owner);
}

// allocate a new slab for the given size class from a dedicated VM extent
static gc_object_slab_t* allocate_slab(gc_object_zone_t* oz, int cls) {
    size_t slot_size = sizeof(gc_header_t) + SIZE_CLASSES[cls];
    size_t slot_count = SLOTS_PER_SLAB[cls];
    size_t slab_bytes = slot_size * slot_count;

    MemVmRegion* region = mem_vm_region_reserve(oz->context, oz->owner,
                                                MEM_ROLE_RUNTIME_HEAP,
                                                slab_bytes, mem_vm_page_size());
    if (!region || !mem_vm_region_commit(region, 0,
                                         mem_vm_region_reserved_bytes(region))) {
        if (region) mem_vm_region_release(region);
        log_error("gc_object_zone: failed to reserve/commit slab of %zu bytes", slab_bytes);
        return NULL;
    }
    uint8_t* memory = (uint8_t*)mem_vm_region_base(region);
    memset(memory, 0, slab_bytes);

    // allocate slab metadata (from C heap — small, few slabs)
    gc_object_slab_t* slab = (gc_object_slab_t*)mem_calloc(1, sizeof(gc_object_slab_t),
                                                            MEM_CAT_CONTAINER);
    if (!slab) {
        log_error("gc_object_zone: failed to allocate slab metadata");
        mem_vm_region_release(region);
        return NULL;
    }

    slab->base = memory;
    slab->region = region;
    slab->slot_size = slot_size;
    slab->slot_count = slot_count;
    slab->next_fresh = 0;
    slab->next = NULL;

    if (!register_slab_range(oz, memory, slab_bytes, slab, NULL)) {
        // The collector must never retain a slab it cannot identify as owned.
        log_error("gc_object_zone: failed to register slab class=%d", cls);
        mem_free(slab);
        mem_vm_region_release(region);
        return NULL;
    }
    oz->slab_count++;
    log_debug("gc_object_zone: allocated slab class=%d slot_size=%zu slots=%zu bytes=%zu",
              cls, slot_size, slot_count, slab_bytes);
    return slab;
}

// return the class cursor, creating a slab only when every existing fresh slot
// has been consumed. The cursor is the only slab that can have fresh capacity;
// older slabs are full because new slabs are prepended at exhaustion.
static gc_object_slab_t* get_fresh_slab(gc_object_zone_t* oz, int cls) {
    gc_object_slab_t* slab = oz->fresh_slabs[cls];
    if (slab && slab->next_fresh < slab->slot_count) return slab;

    slab = allocate_slab(oz, cls);
    if (!slab) return NULL;
    slab->next = oz->slabs[cls];
    oz->slabs[cls] = slab;
    oz->fresh_slabs[cls] = slab;
    return slab;
}

gc_object_zone_t* gc_object_zone_create(MemContext* context, MemNode* owner) {
    gc_object_zone_t* oz = (gc_object_zone_t*)mem_calloc(1, sizeof(gc_object_zone_t),
                                                          MEM_CAT_CONTAINER);
    if (!oz) {
        log_error("gc_object_zone_create: failed to allocate zone");
        return NULL;
    }
    oz->context = context;
    oz->owner = owner;

    // pre-allocate one slab per size class
    for (int i = 0; i < GC_NUM_SIZE_CLASSES; i++) {
        oz->free_lists[i] = NULL;
        oz->slabs[i] = allocate_slab(oz, i);
        if (!oz->slabs[i]) {
            log_error("gc_object_zone_create: failed to allocate initial slab for class %d", i);
            gc_object_zone_destroy(oz);
            return NULL;
        }
        oz->fresh_slabs[i] = oz->slabs[i];
    }

    log_debug("gc_object_zone_create: created with %d size classes", GC_NUM_SIZE_CLASSES);
    return oz;
}

void gc_object_zone_destroy(gc_object_zone_t* oz) {
    if (!oz) return;
    // release each extent explicitly; GC owns these regions rather than a Pool
    for (int i = 0; i < GC_NUM_SIZE_CLASSES; i++) {
        gc_object_slab_t* slab = oz->slabs[i];
        while (slab) {
            gc_object_slab_t* next = slab->next;
            mem_vm_region_release(slab->region);
            mem_free(slab);
            slab = next;
        }
    }
    // free sorted range arrays
    mem_free(oz->slab_ranges);
    mem_free(oz->range_owners);
    mem_free(oz->page_index);
    log_debug("gc_object_zone_destroy: freed %zu slabs, %zu allocs, %zu frees",
              oz->slab_count, oz->total_slots_allocated, oz->total_slots_freed);
    mem_free(oz);
}

void* gc_object_zone_reuse(gc_object_zone_t* oz, int cls, size_t size,
                          uint16_t type_tag, gc_header_t** all_objects) {
    gc_header_t* header = oz->free_lists[cls];
    if (!header) return NULL;
    oz->free_lists[cls] = header->next;
    memset((void*)(header + 1), 0, SIZE_CLASSES[cls]);
    // A shared size-class list can contain bump slots; clearing their owner
    // bit would make a later sweep classify their storage as a slab.
    gc_init_object_header(header, size, type_tag,
        header->gc_flags & GC_FLAG_BUMP, all_objects);
    oz->total_slots_allocated++;
    return (void*)(header + 1);
}

// keep both entry points on one inlined slot path; delegating the generic
// entry added a call to every small allocation.
static inline __attribute__((always_inline)) void* gc_object_zone_alloc_small(
        gc_object_zone_t* oz, int cls, size_t size, uint16_t type_tag,
        gc_header_t** all_objects) {
    void* reused = gc_object_zone_reuse(oz, cls, size, type_tag, all_objects);
    if (reused) return reused;

    gc_object_slab_t* slab = get_fresh_slab(oz, cls);
    if (!slab) return NULL;
    gc_header_t* header = (gc_header_t*)(slab->base + slab->next_fresh * slab->slot_size);
    slab->next_fresh++;
    gc_init_object_header(header, size, type_tag, 0, all_objects);

    oz->total_slots_allocated++;

    return (void*)(header + 1);
}

void* gc_object_zone_alloc(gc_object_zone_t* oz, size_t size, uint16_t type_tag,
                           gc_header_t** all_objects) {
    int cls = gc_object_zone_class_index(size);
    if (cls < 0) {
        // size exceeds object zone — fall through to caller for large object path
        return NULL;
    }
    return gc_object_zone_alloc_small(oz, cls, size, type_tag, all_objects);
}

void* gc_object_zone_alloc_class(gc_object_zone_t* oz, int cls, size_t size,
                                  uint16_t type_tag, gc_header_t** all_objects) {
    return gc_object_zone_alloc_small(oz, cls, size, type_tag, all_objects);
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
    header->gc_flags = (header->gc_flags & GC_FLAG_BUMP) | GC_FLAG_FREED;
    header->next = oz->free_lists[cls];
    oz->free_lists[cls] = header;
    oz->total_slots_freed++;
}

// Binary search the sorted slab_ranges array for the range containing p.
static bool range_search(const gc_object_zone_t* oz, uint8_t* p,
                         gc_range_owner_t* owner, gc_slab_range_t* range) {
    size_t lo = 0, hi = oz->range_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (oz->slab_ranges[mid].end <= p) {
            lo = mid + 1;
        } else if (oz->slab_ranges[mid].base > p) {
            hi = mid;
        } else {
            lo = mid;
            break;
        }
    }
    if (lo >= oz->range_count) return false;
    if (p < oz->slab_ranges[lo].base || p >= oz->slab_ranges[lo].end) return false;
    *owner = oz->range_owners[lo];
    *range = oz->slab_ranges[lo];
    return true;
}

int gc_object_zone_owns_or_foreign(gc_object_zone_t* oz, void* ptr, void** foreign_owner) {
    if (foreign_owner) *foreign_owner = NULL;
    if (!oz || !ptr) return 0;
    uint8_t* p = (uint8_t*)ptr;

    // Fast rejection: check against global min/max address bounds
    if (p < oz->min_addr || p >= oz->max_addr) return 0;

    gc_range_owner_t owner = oz->recent_owner;
    gc_slab_range_t range = oz->recent_range;
    if (!range.base || p < range.base || p >= range.end) {
        const gc_range_page_t* indexed = oz->page_index ? page_index_find(oz, p) : NULL;
        if (oz->page_index && !indexed) return 0;   // a complete index: no page, no range
        if (indexed && !indexed->shared) {
            if (p < indexed->base || p >= indexed->end) return 0;
            owner = indexed->owner;
            range = (gc_slab_range_t){indexed->base, indexed->end};
        } else if (!range_search(oz, p, &owner, &range)) {
            return 0;
        }
        // cache ownership, never liveness: exact-slot and allocation-watermark checks still run.
        oz->recent_range = range;
        oz->recent_owner = owner;
    }
    uint8_t* range_base = range.base;

    // Non-slab ranges (bump blocks) are not object-zone slots; gc_heap.c owns
    // their exact-slot check, starting from the owner found by this lookup.
    gc_object_slab_t* slab = owner.slab;
    if (!slab) {
        if (foreign_owner) *foreign_owner = owner.foreign;
        return 0;
    }
#ifndef NDEBUG
    // Slabs are freed only by gc_object_zone_destroy, which drops the whole
    // range array with them. If any path ever retires a slab mid-run it must
    // also remove its range, or mark would follow a dangling pointer here.
    if (slab->base != range_base) {
        log_error("gc_object_zone_owns: stale slab range base=%p slab=%p",
            (void*)range_base, (void*)slab);
        abort();
    }
#else
    (void)range_base;
#endif

    // Exact Item roots must name an allocated user slot, never an interior slab
    // address, before the collector accepts the pointer as a managed object.
    // The lookup already identified the owning slab, so this is O(1):
    // the slot must be aligned and below the slab's fresh-allocation watermark.
    size_t slot_size = slab->slot_size;
    uint8_t* first_user = slab->base + sizeof(gc_header_t);
    if (p < first_user) return 0;
    if (p >= slab->base + slab->next_fresh * slot_size) return 0;
    return ((uintptr_t)(p - first_user) % slot_size) == 0;
}

int gc_object_zone_owns(gc_object_zone_t* oz, void* ptr) {
    return gc_object_zone_owns_or_foreign(oz, ptr, NULL);
}
