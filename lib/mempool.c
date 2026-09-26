#include "mempool.h"
#include "mem_vm.h"
#define MEMTRACK_NO_LOCATION_MACROS
#include "memtrack.h"
#include "log.h"
#include "math_checked.hpp"
#include "str.h"
#include "math_utils.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SIZE_LIMIT ((size_t)1024 * 1024 * 1024)
#define POOL_DEFAULT_EXTENT_SIZE ((size_t)1024)
#define POOL_MIN_EXTENT_SIZE ((size_t)1024)
#define POOL_VM_THRESHOLD ((size_t)4096)
// A VM extent's commit step grows with what it has committed, up to this
// cap. One page per step cost an mprotect per 16 KiB -- 6% of a large HTML
// parse; committed pages stay non-resident until first touched.
#define POOL_COMMIT_STEP_MAX ((size_t)1024 * 1024)
#define POOL_MIN_PAYLOAD ((size_t)16)
#define POOL_VALID_MARKER 0xDEADBEEF
#define POOL_BLOCK_MAGIC 0x50424C4Bu
#define POOL_EXTENT_MAGIC 0x50455854u
#define POOL_ALIGNMENT 16u
#define POOL_BIN_COUNT 32u

#if defined(__GNUC__) || defined(__clang__)
#define MEMPOOL_WEAK __attribute__((weak))
#else
#define MEMPOOL_WEAK
#endif

typedef struct MemVmRegion MemVmRegion;

// Standalone lib tests intentionally link mempool without the tracker. These
// weak definitions preserve that boundary; engine builds resolve them to the
// hardened memtrack implementation.
MEMPOOL_WEAK MemtrackMode memtrack_get_mode(void) {
    return MEMTRACK_MODE_OFF;
}

MEMPOOL_WEAK void* mem_alloc_loc(size_t size, MemCategory category, int line) {
    (void)category;
    (void)line;
    return malloc(size);
}

MEMPOOL_WEAK void mem_free_loc(void* ptr, int line) {
    (void)line;
    free(ptr);
}

// Some focused library targets intentionally omit the VM provider. These weak
// fallbacks preserve that standalone boundary, but the engine library disables
// them so its real lib/mem_vm.c provider cannot be shadowed by archive order.
#ifndef MEMPOOL_RUNTIME_VM_PROVIDER
MEMPOOL_WEAK size_t mem_vm_page_size(void) {
    return 0;
}

MEMPOOL_WEAK MemVmRegion* mem_vm_region_reserve(MemContext* context,
                                                MemNode* owner,
                                                MemRole role,
                                                size_t size,
                                                size_t alignment) {
    (void)context;
    (void)owner;
    (void)role;
    (void)size;
    (void)alignment;
    return NULL;
}

MEMPOOL_WEAK bool mem_vm_region_commit(MemVmRegion* region,
                                       size_t offset, size_t size) {
    (void)region;
    (void)offset;
    (void)size;
    return false;
}

MEMPOOL_WEAK void mem_vm_region_release(MemVmRegion* region) {
    (void)region;
}

MEMPOOL_WEAK void* mem_vm_region_base(const MemVmRegion* region) {
    (void)region;
    return NULL;
}

MEMPOOL_WEAK size_t mem_vm_region_reserved_bytes(const MemVmRegion* region) {
    (void)region;
    return 0;
}
#endif

typedef struct PoolBlock PoolBlock;
typedef struct PoolExtent PoolExtent;

// A resource the pool owns without holding it as a block (D4.2.6). The record
// is itself a pool block, so it needs no release of its own.
typedef struct PoolCleanup {
    void (*fn)(void* arg);
    void* arg;
    struct PoolCleanup* next;
} PoolCleanup;

// PoolBlock is the physical boundary tag, 32 bytes ahead of every payload. The
// Pool owns the containing extent, so no global pointer index is required.
// A free block keeps its free-list links in its own payload (POOL_MIN_PAYLOAD
// holds them) and is marked free by requested == 0: a live block always has
// requested >= 1, because pool_alloc refuses size 0. With the links and a
// flags word in every header it was 64 bytes -- on a parsed HTML document,
// 64 MB of headers beside 175 MB of data.
struct PoolBlock {
    uint32_t magic;
    uint32_t requested;   // the caller's size, 0 while free (SIZE_LIMIT fits)
    size_t span;
    size_t prev_span;
    PoolExtent* extent;
};

typedef struct PoolFreeLinks {
    PoolBlock* prev;
    PoolBlock* next;
} PoolFreeLinks;

#if defined(__cplusplus)
static_assert(POOL_MIN_PAYLOAD >= sizeof(PoolFreeLinks), "a free block's payload holds its links");
static_assert(SIZE_LIMIT <= UINT32_MAX, "requested is 32 bits");
#else
_Static_assert(POOL_MIN_PAYLOAD >= sizeof(PoolFreeLinks), "a free block's payload holds its links");
_Static_assert(SIZE_LIMIT <= UINT32_MAX, "requested is 32 bits");
#endif

struct PoolExtent {
    uint32_t magic;
    uint32_t flags;
    struct Pool* owner;
    MemVmRegion* vm_region;
    void* raw_base;
    uint8_t* base;
    size_t reserved;
    size_t committed;
    PoolBlock* first;
    PoolBlock* last;
    PoolExtent* prev;
    PoolExtent* next;
};

struct Pool {
    unsigned pool_id;
    unsigned valid;
    bool struct_tracked;
    MemCategory category;
    MemContext* context;
    PoolExtent* extents;
    PoolExtent* last_extent;
    PoolBlock* free_bins[POOL_BIN_COUNT];
    // bit i set iff free_bins[i] is non-empty: the search jumps to the next
    // non-empty bin instead of testing each empty head above the request's
    uint32_t free_bin_mask;
    size_t next_extent_size;
    size_t reserved_bytes;
    size_t committed_bytes;
    size_t alloc_bytes;
    size_t alloc_count;
    size_t live_bytes;
    size_t high_water_live_bytes;
    size_t free_count;
    void* mem_node;
    PoolCleanup* cleanups;   // newest first
};

static unsigned next_pool_id = 1;
static void (*g_pool_node_release)(void*) = NULL;

void pool_set_node_release_hook(void (*fn)(void*)) {
    g_pool_node_release = fn;
}

static size_t pool_align_up(size_t value) {
    size_t result = 0;
    return math_size_align_up(value, POOL_ALIGNMENT, &result) ? result : 0;
}

static size_t block_header_size(void) {
    return pool_align_up(sizeof(PoolBlock));
}

// only meaningful while the block is free
static PoolFreeLinks* pool_block_links(PoolBlock* block) {
    return (PoolFreeLinks*)((uint8_t*)block + block_header_size());
}

static size_t pool_page_size(void) {
#ifdef MEMPOOL_RUNTIME_VM_PROVIDER
    size_t page = mem_vm_page_size();
#else
    size_t page = mem_vm_page_size ? mem_vm_page_size() : 4096u;
#endif
    return page != 0 ? page : 4096u;
}

static bool pool_vm_api_available(void) {
#ifdef MEMPOOL_RUNTIME_VM_PROVIDER
    return mem_vm_page_size() != 0;
#else
    return mem_vm_page_size && mem_vm_page_size() != 0 &&
           mem_vm_region_reserve && mem_vm_region_commit &&
           mem_vm_region_release && mem_vm_region_base &&
           mem_vm_region_reserved_bytes;
#endif
}

static void* pool_meta_alloc(Pool* pool, size_t size) {
    if (mem_alloc_loc) return mem_alloc_loc(size, pool->category, 0);
    return malloc(size);
}

static void pool_meta_free(void* ptr) {
    if (!ptr) return;
    if (mem_free_loc) mem_free_loc(ptr, 0);
    else free(ptr);
}

static size_t pool_normalize_initial_size(size_t requested) {
    size_t size = POOL_MIN_EXTENT_SIZE;
    while (size < requested) {
        if (size > SIZE_MAX / 2) return 0;
        size *= 2;
    }
    return size;
}

static size_t pool_next_growth_size(size_t current) {
    return current > SIZE_MAX / 2 ? SIZE_MAX : current * 2;
}

static size_t pool_max_size(size_t left, size_t right) {
    return left > right ? left : right;
}

static size_t pool_block_required_size(size_t requested) {
    size_t total = 0;
    if (!math_checked_add(block_header_size(), requested, &total)) return 0;
    return pool_align_up(total);
}

static bool pool_block_can_hold(size_t span, size_t required) {
    return required != 0 && span >= required;
}

static bool pool_block_is_free(const PoolBlock* block) {
    return block && block->requested == 0;
}

// floor(log2(span)) capped at the last bin, 0 below 2. Counting leading zeros
// replaces a shift loop that ran three times per allocation (the request, the
// taken block and the split remainder, about 20 steps each for a large tail).
static unsigned pool_bin_index(size_t span) {
    if (span < 2) return 0;
    unsigned floor_log2 = (unsigned)(63 - math_clz64((uint64_t)span));
    return floor_log2 < POOL_BIN_COUNT - 1 ? floor_log2 : POOL_BIN_COUNT - 1;
}

static bool pool_block_range_valid(const PoolBlock* block,
                                   const PoolExtent* extent) {
    if (!block || !extent || extent->magic != POOL_EXTENT_MAGIC ||
        block->magic != POOL_BLOCK_MAGIC || block->extent != extent) {
        return false;
    }

    uintptr_t base = (uintptr_t)extent->base;
    uintptr_t address = (uintptr_t)block;
    if (address < base) return false;
    size_t offset = (size_t)(address - base);
    if (offset > extent->committed || block->span > extent->committed - offset) {
        return false;
    }
    if (block->span < block_header_size() ||
        block->span % POOL_ALIGNMENT != 0 ||
        block->requested > block->span - block_header_size()) {
        return false;
    }
    if (block->prev_span == 0) {
        if (offset != 0) return false;
    } else {
        if (block->prev_span > offset ||
            block->prev_span % POOL_ALIGNMENT != 0) {
            return false;
        }
        PoolBlock* previous = (PoolBlock*)((uint8_t*)block - block->prev_span);
        if (previous->magic != POOL_BLOCK_MAGIC ||
            previous->extent != extent || previous->span != block->prev_span) {
            return false;
        }
    }

    size_t next_offset = offset + block->span;
    if (next_offset < extent->committed) {
        PoolBlock* next = (PoolBlock*)(extent->base + next_offset);
        if (next->magic != POOL_BLOCK_MAGIC || next->extent != extent ||
            next->prev_span != block->span) {
            return false;
        }
    } else if (next_offset != extent->committed) {
        return false;
    }
    return true;
}

static PoolBlock* pool_block_next(const PoolBlock* block) {
    if (!block || !block->extent) return NULL;
    PoolExtent* extent = block->extent;
    uintptr_t offset = (uintptr_t)((const uint8_t*)block - extent->base);
    if (offset > extent->committed || block->span > extent->committed - offset) {
        return NULL;
    }
    size_t next_offset = (size_t)offset + block->span;
    return next_offset < extent->committed
        ? (PoolBlock*)(extent->base + next_offset) : NULL;
}

static PoolBlock* pool_block_prev(const PoolBlock* block) {
    if (!block || !block->extent || block->prev_span == 0) return NULL;
    return (PoolBlock*)((uint8_t*)block - block->prev_span);
}

static void pool_free_list_insert(Pool* pool, PoolBlock* block) {
    unsigned index = pool_bin_index(block->span);
    PoolFreeLinks* links = pool_block_links(block);
    links->prev = NULL;
    links->next = pool->free_bins[index];
    if (links->next) pool_block_links(links->next)->prev = block;
    pool->free_bins[index] = block;
    pool->free_bin_mask |= 1u << index;
}

static void pool_free_list_remove(Pool* pool, PoolBlock* block) {
    unsigned index = pool_bin_index(block->span);
    PoolFreeLinks* links = pool_block_links(block);
    if (links->prev) pool_block_links(links->prev)->next = links->next;
    else if (pool->free_bins[index] == block) pool->free_bins[index] = links->next;
    if (links->next) pool_block_links(links->next)->prev = links->prev;
    if (!pool->free_bins[index]) pool->free_bin_mask &= ~(1u << index);
}

static PoolBlock* pool_find_suitable(Pool* pool, size_t required) {
    unsigned first = pool_bin_index(required);
    // the same bins in the same order as testing each one, empty ones skipped
    uint32_t mask = pool->free_bin_mask & (UINT32_MAX << first);
    while (mask) {
        unsigned index = (unsigned)(63 - math_clz64((uint64_t)(mask & (0u - mask))));
        mask &= mask - 1;
        PoolBlock* block = pool->free_bins[index];
        while (block) {
            if (pool_block_is_free(block) &&
                pool_block_can_hold(block->span, required)) {
                return block;
            }
            block = pool_block_links(block)->next;
        }
    }
    return NULL;
}

static void pool_init_free_block(PoolBlock* block, PoolExtent* extent,
                                 size_t span, size_t prev_span) {
    memset(block, 0, sizeof(*block));
    block->magic = POOL_BLOCK_MAGIC;
    block->span = span;
    block->prev_span = prev_span;
    block->extent = extent;
}

static Pool* pool_alloc_struct(void) {
    bool tracked = memtrack_get_mode &&
                   memtrack_get_mode() != MEMTRACK_MODE_OFF &&
                   mem_alloc_loc && mem_free_loc;
    Pool* pool = tracked
        ? (Pool*)mem_alloc_loc(sizeof(Pool), MEM_CAT_SYSTEM, 0)
        : (Pool*)malloc(sizeof(Pool));
    if (!pool) return NULL;
    memset(pool, 0, sizeof(*pool));
    pool->struct_tracked = tracked;
    return pool;
}

static void pool_free_struct(Pool* pool) {
    if (!pool) return;
    if (pool->struct_tracked) mem_free_loc(pool, 0);
    else free(pool);
}

static void pool_init(Pool* pool, size_t initial_size) {
    pool->pool_id = next_pool_id++;
    pool->valid = POOL_VALID_MARKER;
    pool->category = MEM_CAT_SYSTEM;
    pool->next_extent_size = initial_size;
}

static void pool_link_extent(Pool* pool, PoolExtent* extent) {
    extent->prev = pool->last_extent;
    extent->next = NULL;
    if (pool->last_extent) pool->last_extent->next = extent;
    else pool->extents = extent;
    pool->last_extent = extent;
}

static void pool_unlink_extent(Pool* pool, PoolExtent* extent) {
    if (extent->prev) extent->prev->next = extent->next;
    else pool->extents = extent->next;
    if (extent->next) extent->next->prev = extent->prev;
    else pool->last_extent = extent->prev;
    extent->prev = NULL;
    extent->next = NULL;
}

static void pool_release_extent(Pool* pool, PoolExtent* extent) {
    if (!pool || !extent) return;
#ifdef MEMPOOL_RUNTIME_VM_PROVIDER
    if (extent->vm_region) {
#else
    if (extent->vm_region && mem_vm_region_release) {
#endif
        mem_vm_region_release(extent->vm_region);
        extent->vm_region = NULL;
    } else if (extent->raw_base) {
        pool_meta_free(extent->raw_base);
        extent->raw_base = NULL;
    }
    pool->reserved_bytes = pool->reserved_bytes >= extent->reserved
        ? pool->reserved_bytes - extent->reserved : 0;
    pool->committed_bytes = pool->committed_bytes >= extent->committed
        ? pool->committed_bytes - extent->committed : 0;
    extent->magic = 0;
    pool_meta_free(extent);
}

static void pool_clear_free_bins(Pool* pool) {
    memset(pool->free_bins, 0, sizeof(pool->free_bins));
    pool->free_bin_mask = 0;
}

static bool pool_append_committed_range(Pool* pool, PoolExtent* extent,
                                        size_t added) {
    if (!pool || !extent || added == 0) return false;
    size_t old_committed = extent->committed;
    PoolBlock* last = extent->last;
    extent->committed += added;

    if (last && pool_block_is_free(last)) {
        pool_free_list_remove(pool, last);
        last->span += added;
        extent->last = last;
        pool_free_list_insert(pool, last);
    } else {
        PoolBlock* block = (PoolBlock*)(extent->base + old_committed);
        pool_init_free_block(block, extent, added, last ? last->span : 0);
        extent->last = block;
        if (!last) extent->first = block;
        pool_free_list_insert(pool, block);
    }
    pool->committed_bytes += added;
    return true;
}

static bool pool_commit_more(Pool* pool, PoolExtent* extent, size_t required) {
#ifdef MEMPOOL_RUNTIME_VM_PROVIDER
    if (!pool || !extent || !extent->vm_region) {
#else
    if (!pool || !extent || !extent->vm_region || !mem_vm_region_commit) {
#endif
        return false;
    }
    if (extent->reserved <= extent->committed) return false;

    size_t minimum = pool_max_size(POOL_VM_THRESHOLD, required);
    size_t step = extent->committed < POOL_COMMIT_STEP_MAX
        ? extent->committed : POOL_COMMIT_STEP_MAX;
    size_t commit_size = 0;
    if (!math_size_round_up(pool_max_size(minimum, step), pool_page_size(), &commit_size)) {
        return false;
    }
    size_t available = extent->reserved - extent->committed;
    if (commit_size > available) commit_size = available;
    if (commit_size == 0 || commit_size % pool_page_size() != 0) return false;
    if (!mem_vm_region_commit(extent->vm_region, extent->committed, commit_size)) {
        return false;
    }
    return pool_append_committed_range(pool, extent, commit_size);
}

static PoolExtent* pool_create_extent(Pool* pool, size_t required) {
    size_t target = pool_max_size(pool->next_extent_size, required);
    if (target < POOL_VM_THRESHOLD) {
        target = pool_normalize_initial_size(target);
        if (target == 0) return NULL;
    }

    PoolExtent* extent = (PoolExtent*)pool_meta_alloc(pool, sizeof(*extent));
    if (!extent) return NULL;
    memset(extent, 0, sizeof(*extent));
    extent->magic = POOL_EXTENT_MAGIC;
    extent->owner = pool;

    bool use_vm = target >= POOL_VM_THRESHOLD && pool_vm_api_available();
    if (use_vm) {
        size_t page = pool_page_size();
        size_t reserved = 0;
        size_t commit_size = 0;
        size_t minimum_commit = pool_max_size(POOL_VM_THRESHOLD, required);
        if (!math_size_round_up(target, page, &reserved) ||
            !math_size_round_up(minimum_commit, page, &commit_size)) {
            pool_meta_free(extent);
            return NULL;
        }
        if (commit_size > reserved) reserved = commit_size;

        extent->vm_region = mem_vm_region_reserve(
            pool->context, (MemNode*)pool->mem_node, MEM_ROLE_TEMP,
            reserved, page);
        if (!extent->vm_region ||
            !mem_vm_region_commit(extent->vm_region, 0, commit_size)) {
#ifdef MEMPOOL_RUNTIME_VM_PROVIDER
            if (extent->vm_region) {
#else
            if (extent->vm_region && mem_vm_region_release) {
#endif
                mem_vm_region_release(extent->vm_region);
            }
            pool_meta_free(extent);
            return NULL;
        }
        extent->base = (uint8_t*)mem_vm_region_base(extent->vm_region);
        if (!extent->base) {
            mem_vm_region_release(extent->vm_region);
            extent->vm_region = NULL;
            pool_meta_free(extent);
            return NULL;
        }
        extent->reserved = mem_vm_region_reserved_bytes(extent->vm_region);
        extent->committed = commit_size;
    } else {
        size_t reserved = pool_align_up(target);
        if (reserved == 0) {
            pool_meta_free(extent);
            return NULL;
        }
        extent->raw_base = pool_meta_alloc(pool, reserved);
        if (!extent->raw_base) {
            pool_meta_free(extent);
            return NULL;
        }
        extent->base = (uint8_t*)extent->raw_base;
        extent->reserved = reserved;
        extent->committed = reserved;
    }

    pool_init_free_block((PoolBlock*)extent->base, extent,
                         extent->committed, 0);
    extent->first = (PoolBlock*)extent->base;
    extent->last = extent->first;
    pool_link_extent(pool, extent);
    pool->reserved_bytes += extent->reserved;
    pool->committed_bytes += extent->committed;

    size_t growth_base = extent->reserved;
    pool->next_extent_size = pool_next_growth_size(growth_base);
    pool_free_list_insert(pool, extent->first);
    return extent;
}

static PoolExtent* pool_find_extent_for_ptr(Pool* pool, const void* ptr) {
    if (!pool || !ptr) return NULL;
    uintptr_t address = (uintptr_t)ptr;
    for (PoolExtent* extent = pool->extents; extent; extent = extent->next) {
        uintptr_t base = (uintptr_t)extent->base;
        if (address < base) continue;
        size_t offset = (size_t)(address - base);
        if (offset < extent->committed) return extent;
    }
    return NULL;
}

// Resolve the PoolBlock only after proving the user pointer lies in one of
// this Pool's committed extents; this keeps wrong-owner frees registry-free.
static PoolBlock* pool_find_block(Pool* pool, void* ptr) {
    if (!pool || !ptr) return NULL;
    PoolExtent* extent = pool_find_extent_for_ptr(pool, ptr);
    if (!extent) return NULL;

    uintptr_t address = (uintptr_t)ptr;
    uintptr_t base = (uintptr_t)extent->base;
    size_t offset = (size_t)(address - base);
    if (extent->committed < block_header_size() ||
        offset < block_header_size() ||
        offset >= extent->committed) {
        return NULL;
    }

    PoolBlock* block = (PoolBlock*)((uint8_t*)ptr - block_header_size());
    // the pointer is already proven inside committed storage; validating the
    // header's lower bound is sufficient because range validation checks the
    // complete block span, including a payload ending at the extent boundary.
    if (block->requested == 0 || !pool_block_range_valid(block, extent)) {
        return NULL;
    }
    return block;
}

static void pool_split_block(Pool* pool, PoolBlock* block, size_t required) {
    if (!pool || !block || block->span <= required) return;
    size_t remainder_span = block->span - required;
    size_t minimum_remainder = pool_align_up(block_header_size() + POOL_MIN_PAYLOAD);
    if (remainder_span < minimum_remainder) return;

    PoolExtent* extent = block->extent;
    PoolBlock* remainder = (PoolBlock*)((uint8_t*)block + required);
    pool_init_free_block(remainder, extent, remainder_span, required);
    block->span = required;
    if (extent->last == block) extent->last = remainder;

    PoolBlock* next = pool_block_next(remainder);
    if (next) next->prev_span = remainder->span;
    pool_free_list_insert(pool, remainder);
}

static PoolBlock* pool_take_block(Pool* pool, size_t required,
                                  size_t requested) {
    PoolBlock* block = pool_find_suitable(pool, required);
    if (!block) return NULL;
    pool_free_list_remove(pool, block);
    pool_split_block(pool, block, required);
    block->requested = (uint32_t)requested;  // >= 1 marks the block live
    return block;
}

static bool pool_grow_for_request(Pool* pool, size_t required) {
    PoolExtent* last = pool->last_extent;
    if (last && pool_commit_more(pool, last, required) &&
        pool_find_suitable(pool, required)) {
        return true;
    }
    return pool_create_extent(pool, required) != NULL;
}

static void pool_coalesce_free(Pool* pool, PoolBlock* block) {
    if (!pool || !block) return;
    PoolExtent* extent = block->extent;
    PoolBlock* previous = pool_block_prev(block);
    PoolBlock* next = pool_block_next(block);

    if (previous && pool_block_is_free(previous)) {
        pool_free_list_remove(pool, previous);
        previous->span += block->span;
        // the surviving predecessor must remain the extent tail after a
        // last-block merge; otherwise later growth follows stale metadata.
        if (extent->last == block) extent->last = previous;
        block = previous;
    }
    if (next && pool_block_is_free(next)) {
        pool_free_list_remove(pool, next);
        block->span += next->span;
        if (extent->last == next) extent->last = block;
    }

    PoolBlock* after = pool_block_next(block);
    if (after) after->prev_span = block->span;
    pool_free_list_insert(pool, block);
}

static void pool_release_all_extents(Pool* pool) {
    PoolExtent* extent = pool->extents;
    while (extent) {
        PoolExtent* next = extent->next;
        pool_unlink_extent(pool, extent);
        pool_release_extent(pool, extent);
        extent = next;
    }
    pool_clear_free_bins(pool);
    pool->reserved_bytes = 0;
    pool->committed_bytes = 0;
    pool->live_bytes = 0;
}

static void pool_release_node(Pool* pool) {
    if (pool->mem_node && g_pool_node_release) {
        g_pool_node_release(pool->mem_node);
        pool->mem_node = NULL;
    }
}

// Runs every cleanup once, newest first, while the pool's blocks are still
// valid. Each record is unlinked before its callback, so a cleanup that
// registers another has it run in the same pass.
static void pool_run_cleanups(Pool* pool) {
    PoolCleanup* cleanup;
    while ((cleanup = pool->cleanups) != NULL) {
        pool->cleanups = cleanup->next;
        cleanup->fn(cleanup->arg);
    }
}

bool pool_add_cleanup(Pool* pool, void (*fn)(void* arg), void* arg) {
    if (!pool || pool->valid != POOL_VALID_MARKER || !fn) return false;
    PoolCleanup* cleanup = (PoolCleanup*)pool_alloc(pool, sizeof(PoolCleanup));
    if (!cleanup) return false;
    cleanup->fn = fn;
    cleanup->arg = arg;
    cleanup->next = pool->cleanups;
    pool->cleanups = cleanup;
    return true;
}

Pool* pool_create_sized(size_t initial_size) {
    size_t normalized = pool_normalize_initial_size(initial_size);
    if (normalized == 0 || normalized > SIZE_LIMIT) return NULL;

    Pool* pool = pool_alloc_struct();
    if (!pool) return NULL;
    pool_init(pool, normalized);
    return pool;
}

Pool* pool_create(void) {
    return pool_create_sized(POOL_DEFAULT_EXTENT_SIZE);
}

void pool_drain(Pool* pool) {
    if (!pool || pool->valid != POOL_VALID_MARKER) return;
    pool_run_cleanups(pool);
    pool_release_node(pool);
    pool_release_all_extents(pool);
    pool->valid = 0;
}

void pool_destroy(Pool* pool) {
    if (!pool) return;
    if (pool->valid == POOL_VALID_MARKER) {
        log_debug("mempool: destroy pool=%p id=%u", (void*)pool, pool->pool_id);
        pool_drain(pool);
    }
    pool_free_struct(pool);
}

void pool_reset(Pool* pool) {
    if (!pool || pool->valid != POOL_VALID_MARKER) return;
    pool_run_cleanups(pool);
    pool_clear_free_bins(pool);
    pool->live_bytes = 0;
    for (PoolExtent* extent = pool->extents; extent; extent = extent->next) {
        if (extent->committed == 0) continue;
        PoolBlock* block = (PoolBlock*)extent->base;
        pool_init_free_block(block, extent, extent->committed, 0);
        extent->first = block;
        extent->last = block;
        pool_free_list_insert(pool, block);
    }
}

void pool_set_mem_category(Pool* pool, int category) {
    if (!pool) return;
    pool->category = category >= 0 && category < MEM_CAT_COUNT
        ? (MemCategory)category : MEM_CAT_UNKNOWN;
}

void pool_set_mem_context(Pool* pool, void* context) {
    if (pool) pool->context = (MemContext*)context;
}

void* pool_alloc(Pool* pool, size_t size) {
    if (!pool || pool->valid != POOL_VALID_MARKER || size == 0 ||
        size > SIZE_LIMIT) return NULL;

    size_t required = pool_block_required_size(size);
    if (required == 0) return NULL;

    PoolBlock* block = pool_take_block(pool, required, size);
    if (!block && pool_grow_for_request(pool, required)) {
        block = pool_take_block(pool, required, size);
    }
    if (!block) return NULL;

    pool->alloc_bytes += size;
    pool->alloc_count++;
    pool->live_bytes += size;
    if (pool->live_bytes > pool->high_water_live_bytes) {
        pool->high_water_live_bytes = pool->live_bytes;
    }
    return (uint8_t*)block + block_header_size();
}

void* pool_calloc(Pool* pool, size_t size) {
    void* ptr = pool_alloc(pool, size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

void pool_free(Pool* pool, void* ptr) {
    if (!pool || pool->valid != POOL_VALID_MARKER || !ptr) return;
    PoolBlock* block = pool_find_block(pool, ptr);
    if (!block) {
        log_error("mempool: free pointer %p is not owned by pool %u",
                  ptr, pool->pool_id);
        return;
    }

    size_t requested = block->requested;
    block->requested = 0;
    pool->live_bytes = pool->live_bytes >= requested
        ? pool->live_bytes - requested : 0;
    pool->free_count++;
    pool_coalesce_free(pool, block);
}

bool pool_owns(Pool* pool, const void* ptr) {
    return pool && pool->valid == POOL_VALID_MARKER && ptr &&
        pool_find_block(pool, (void*)ptr) != NULL;
}

static void pool_account_realloc(Pool* pool, size_t old_size, size_t new_size) {
    pool->alloc_bytes += new_size;
    pool->alloc_count++;
    pool->free_count++;
    pool->live_bytes = pool->live_bytes >= old_size
        ? pool->live_bytes - old_size + new_size : new_size;
    if (pool->live_bytes > pool->high_water_live_bytes) {
        pool->high_water_live_bytes = pool->live_bytes;
    }
}

static void pool_shrink_block(Pool* pool, PoolBlock* block, size_t required,
                              size_t new_size) {
    size_t old_span = block->span;
    size_t remainder_span = old_span - required;
    size_t minimum_remainder = pool_align_up(block_header_size() + POOL_MIN_PAYLOAD);
    if (remainder_span >= minimum_remainder) {
        PoolExtent* extent = block->extent;
        PoolBlock* remainder = (PoolBlock*)((uint8_t*)block + required);
        pool_init_free_block(remainder, extent, remainder_span, required);
        block->span = required;
        if (extent->last == block) extent->last = remainder;
        PoolBlock* next = pool_block_next(remainder);
        if (next) next->prev_span = remainder->span;
        pool_coalesce_free(pool, remainder);
    }
    block->requested = (uint32_t)new_size;
}

static bool pool_grow_block_in_place(Pool* pool, PoolBlock* block,
                                     size_t required, size_t new_size) {
    PoolBlock* next = pool_block_next(block);
    if (!next || !pool_block_is_free(next) ||
        block->span > SIZE_MAX - next->span ||
        block->span + next->span < required) {
        return false;
    }

    PoolExtent* extent = block->extent;
    size_t combined = block->span + next->span;
    pool_free_list_remove(pool, next);
    block->span = combined;
    if (extent->last == next) extent->last = block;

    size_t remainder_span = combined - required;
    size_t minimum_remainder = pool_align_up(block_header_size() + POOL_MIN_PAYLOAD);
    if (remainder_span >= minimum_remainder) {
        PoolBlock* remainder = (PoolBlock*)((uint8_t*)block + required);
        pool_init_free_block(remainder, extent, remainder_span, required);
        block->span = required;
        if (extent->last == block) extent->last = remainder;
        PoolBlock* after = pool_block_next(remainder);
        if (after) after->prev_span = remainder->span;
        pool_free_list_insert(pool, remainder);
    } else {
        PoolBlock* after = pool_block_next(block);
        if (after) after->prev_span = block->span;
    }
    block->requested = (uint32_t)new_size;
    return true;
}

void* pool_realloc(Pool* pool, void* ptr, size_t size) {
    if (!pool || pool->valid != POOL_VALID_MARKER || size > SIZE_LIMIT) {
        return NULL;
    }
    if (!ptr) return pool_alloc(pool, size);
    if (size == 0) {
        pool_free(pool, ptr);
        return NULL;
    }

    PoolBlock* block = pool_find_block(pool, ptr);
    if (!block) {
        log_error("mempool: realloc pointer %p is not owned by pool %u",
                  ptr, pool->pool_id);
        return NULL;
    }
    size_t required = pool_block_required_size(size);
    if (required == 0) return NULL;
    size_t old_size = block->requested;

    if (required <= block->span) {
        pool_shrink_block(pool, block, required, size);
        pool_account_realloc(pool, old_size, size);
        return ptr;
    }
    if (pool_grow_block_in_place(pool, block, required, size)) {
        pool_account_realloc(pool, old_size, size);
        return ptr;
    }

    void* replacement = pool_alloc(pool, size);
    if (!replacement) return NULL;
    memcpy(replacement, ptr, old_size < size ? old_size : size);
    pool_free(pool, ptr);
    return replacement;
}

char* pool_dup_n(Pool* pool, const char* data, size_t len) {
    if (!pool || !data || len == SIZE_MAX) return NULL;
    char* dup = (char*)pool_alloc(pool, len + 1);
    if (dup) {
        memcpy(dup, data, len);
        dup[len] = '\0';
    }
    return dup;
}

static void* pool_join_alloc(void* context, size_t size) {
    return pool_alloc((Pool*)context, size);
}

static char* pool_join_parts(Pool* pool, const char* const* parts,
                             const size_t* lengths, size_t count) {
    if (!pool) return NULL;
    return str_join_parts_alloc(parts, lengths, count, pool_join_alloc, pool);
}

char* pool_join2(Pool* pool, const char* first, size_t first_len,
                 const char* second, size_t second_len) {
    const char* parts[] = {first, second};
    const size_t lengths[] = {first_len, second_len};
    return pool_join_parts(pool, parts, lengths, 2);
}

char* pool_join3(Pool* pool, const char* first, size_t first_len,
                 const char* second, size_t second_len,
                 const char* third, size_t third_len) {
    const char* parts[] = {first, second, third};
    const size_t lengths[] = {first_len, second_len, third_len};
    return pool_join_parts(pool, parts, lengths, 3);
}

char* pool_strdup(Pool* pool, const char* str) {
    return str ? pool_dup_n(pool, str, strlen(str)) : NULL;
}

void mempool_cleanup(void) {
    // Pool ownership is explicit; no process-global allocator needs finalization.
}

unsigned int pool_get_id(Pool* pool) {
    return pool ? pool->pool_id : 0;
}

void pool_get_stats(Pool* pool, size_t* alloc_bytes, size_t* alloc_count) {
    if (alloc_bytes) *alloc_bytes = pool ? pool->alloc_bytes : 0;
    if (alloc_count) *alloc_count = pool ? pool->alloc_count : 0;
}

void* pool_get_mem_node(Pool* pool) {
    return pool ? pool->mem_node : NULL;
}

void pool_set_mem_node(Pool* pool, void* node) {
    if (pool) pool->mem_node = node;
}

void pool_get_mem_stats(Pool* pool, size_t* reserved, size_t* in_use,
                        size_t* alloc_count) {
    size_t reserved_value = 0;
    size_t live_value = 0;
    size_t count = 0;
    if (pool && pool->valid == POOL_VALID_MARKER) {
        reserved_value = pool->reserved_bytes;
        live_value = pool->live_bytes;
        count = pool->alloc_count;
    }
    if (reserved) *reserved = reserved_value;
    if (in_use) *in_use = live_value;
    if (alloc_count) *alloc_count = count;
}

size_t pool_allocation_size(Pool* pool, void* ptr) {
    PoolBlock* block = pool_find_block(pool, ptr);
    return block ? block->span : 0;
}

void pool_get_detailed_stats(Pool* pool, PoolStats* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!pool || pool->valid != POOL_VALID_MARKER) return;
    out->reserved_bytes = pool->reserved_bytes;
    out->committed_bytes = pool->committed_bytes;
    out->live_bytes = pool->live_bytes;
    out->high_water_live_bytes = pool->high_water_live_bytes;
    out->cumulative_bytes = pool->alloc_bytes;
    out->allocation_count = pool->alloc_count;
    out->free_count = pool->free_count;
}
