#include "mempool.h"
#define MEMTRACK_NO_LOCATION_MACROS
#include "memtrack.h"
#include "log.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SIZE_LIMIT ((size_t)1024 * 1024 * 1024)
#define POOL_VALID_MARKER 0xDEADBEEF
#define POOL_BLOCK_MAGIC 0x50424C4Bu
#define POOL_ALIGNMENT 16u

#if defined(__GNUC__) || defined(__clang__)
#define MEMPOOL_WEAK __attribute__((weak))
#else
#define MEMPOOL_WEAK
#endif

// Standalone lib tests intentionally link mempool without the tracker. The
// weak declarations keep that test boundary valid; engine builds resolve them
// to the hardened memtrack implementation.
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

typedef struct PoolBlock PoolBlock;

typedef struct PoolLookupEntry {
    void* user;
    PoolBlock* block;
} PoolLookupEntry;

struct PoolBlock {
    uint32_t magic;
    uint32_t _pad;
    struct Pool* owner;
    PoolBlock* prev;
    PoolBlock* next;
    void* base;
    size_t requested;
    size_t allocated;
};

struct Pool {
    unsigned pool_id;
    unsigned valid;
    bool struct_tracked;
    MemCategory category;
    PoolBlock* blocks;
    size_t alloc_bytes;
    size_t alloc_count;
    size_t live_bytes;
    size_t high_water_live_bytes;
    size_t free_count;
    void* mem_node;
    PoolLookupEntry* lookup;
    size_t lookup_capacity;
    size_t lookup_count;
};

static unsigned next_pool_id = 1;
static void (*g_pool_node_release)(void*) = NULL;

void pool_set_node_release_hook(void (*fn)(void*)) {
    g_pool_node_release = fn;
}

static bool checked_add_size(size_t left, size_t right, size_t* out) {
    if (right > SIZE_MAX - left) return false;
    *out = left + right;
    return true;
}

static size_t align_up_size(size_t value, size_t alignment) {
    size_t remainder = value & (alignment - 1);
    return remainder == 0 ? value : value + alignment - remainder;
}

static size_t block_header_size(void) {
    return align_up_size(sizeof(PoolBlock), POOL_ALIGNMENT);
}

#define POOL_LOOKUP_TOMBSTONE ((void*)(uintptr_t)1)
#define POOL_LOOKUP_INITIAL_CAPACITY 64u

static size_t pool_lookup_hash(void* ptr, size_t capacity) {
    uintptr_t value = (uintptr_t)ptr >> 4;
    value ^= value >> 23;
    value *= UINT64_C(0x9E3779B97F4A7C15);
    return (size_t)value & (capacity - 1);
}

static bool pool_lookup_resize(Pool* pool, size_t new_capacity) {
    PoolLookupEntry* entries = (PoolLookupEntry*)calloc(
        new_capacity, sizeof(PoolLookupEntry));
    if (!entries) return false;
    for (size_t i = 0; i < pool->lookup_capacity; i++) {
        PoolLookupEntry old = pool->lookup[i];
        if (!old.user || old.user == POOL_LOOKUP_TOMBSTONE) continue;
        size_t slot = pool_lookup_hash(old.user, new_capacity);
        while (entries[slot].user) slot = (slot + 1) & (new_capacity - 1);
        entries[slot] = old;
    }
    free(pool->lookup);
    pool->lookup = entries;
    pool->lookup_capacity = new_capacity;
    return true;
}

static bool pool_lookup_init(Pool* pool) {
    return pool_lookup_resize(pool, POOL_LOOKUP_INITIAL_CAPACITY);
}

static bool pool_lookup_insert(Pool* pool, void* user, PoolBlock* block) {
    if (!pool || !user || !block) return false;
    if (pool->lookup_count * 2 >= pool->lookup_capacity &&
        !pool_lookup_resize(pool, pool->lookup_capacity * 2)) {
        return false;
    }
    size_t slot = pool_lookup_hash(user, pool->lookup_capacity);
    size_t first_tombstone = SIZE_MAX;
    for (;;) {
        void* key = pool->lookup[slot].user;
        if (!key) {
            if (first_tombstone != SIZE_MAX) slot = first_tombstone;
            pool->lookup[slot].user = user;
            pool->lookup[slot].block = block;
            pool->lookup_count++;
            return true;
        }
        if (key == POOL_LOOKUP_TOMBSTONE) {
            if (first_tombstone == SIZE_MAX) first_tombstone = slot;
        } else if (key == user) {
            return false;
        }
        slot = (slot + 1) & (pool->lookup_capacity - 1);
    }
}

static PoolBlock* pool_lookup_find(Pool* pool, void* user) {
    if (!pool || !user || !pool->lookup_capacity) return NULL;
    size_t slot = pool_lookup_hash(user, pool->lookup_capacity);
    for (;;) {
        void* key = pool->lookup[slot].user;
        if (!key) return NULL;
        if (key != POOL_LOOKUP_TOMBSTONE && key == user) {
            return pool->lookup[slot].block;
        }
        slot = (slot + 1) & (pool->lookup_capacity - 1);
    }
}

static void pool_lookup_remove(Pool* pool, void* user) {
    if (!pool || !user || !pool->lookup_capacity) return;
    size_t slot = pool_lookup_hash(user, pool->lookup_capacity);
    for (;;) {
        void* key = pool->lookup[slot].user;
        if (!key) return;
        if (key != POOL_LOOKUP_TOMBSTONE && key == user) {
            pool->lookup[slot].user = POOL_LOOKUP_TOMBSTONE;
            pool->lookup[slot].block = NULL;
            if (pool->lookup_count > 0) pool->lookup_count--;
            return;
        }
        slot = (slot + 1) & (pool->lookup_capacity - 1);
    }
}

static void pool_lookup_clear(Pool* pool) {
    if (!pool || !pool->lookup) return;
    memset(pool->lookup, 0, pool->lookup_capacity * sizeof(PoolLookupEntry));
    pool->lookup_count = 0;
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
    if (pool->struct_tracked) {
        mem_free_loc(pool, 0);
    } else {
        free(pool);
    }
}

static void pool_init(Pool* pool) {
    pool->pool_id = next_pool_id++;
    pool->valid = POOL_VALID_MARKER;
    pool->category = MEM_CAT_SYSTEM;
    (void)pool_lookup_init(pool);
}

static void pool_link_block(Pool* pool, PoolBlock* block) {
    block->owner = pool;
    block->prev = NULL;
    block->next = pool->blocks;
    if (pool->blocks) pool->blocks->prev = block;
    pool->blocks = block;
}

static void pool_unlink_block(Pool* pool, PoolBlock* block) {
    if (block->prev) block->prev->next = block->next;
    else pool->blocks = block->next;
    if (block->next) block->next->prev = block->prev;
    block->prev = NULL;
    block->next = NULL;
}

static PoolBlock* pool_find_block(Pool* pool, void* ptr) {
    return pool_lookup_find(pool, ptr);
}

static void pool_release_block(Pool* pool, PoolBlock* block, bool count_free) {
    if (!pool || !block) return;
    void* user = (uint8_t*)block->base + block_header_size();
    pool_lookup_remove(pool, user);
    pool_unlink_block(pool, block);
    pool->live_bytes = pool->live_bytes >= block->allocated
        ? pool->live_bytes - block->allocated : 0;
    if (count_free) pool->free_count++;
    if (mem_free_loc) mem_free_loc(block->base, 0);
    else free(block->base);
}

static void pool_release_all_blocks(Pool* pool) {
    PoolBlock* block = pool->blocks;
    while (block) {
        PoolBlock* next = block->next;
        pool_release_block(pool, block, false);
        block = next;
    }
    pool->blocks = NULL;
    pool->live_bytes = 0;
}

static void pool_release_node(Pool* pool) {
    if (pool->mem_node && g_pool_node_release) {
        g_pool_node_release(pool->mem_node);
        pool->mem_node = NULL;
    }
}

Pool* pool_create(void) {
    Pool* pool = pool_alloc_struct();
    if (!pool) return NULL;
    pool_init(pool);
    if (!pool->lookup) {
        pool_free_struct(pool);
        return NULL;
    }
    return pool;
}

void pool_drain(Pool* pool) {
    if (!pool || pool->valid != POOL_VALID_MARKER) return;
    pool_release_node(pool);
    pool_release_all_blocks(pool);
    pool->valid = 0;
}

void pool_destroy(Pool* pool) {
    if (!pool || pool->valid != POOL_VALID_MARKER) return;
    log_debug("pool_destroy: destroying pool=%p (id=%u)", (void*)pool, pool->pool_id);
    pool_drain(pool);
    free(pool->lookup);
    pool->lookup = NULL;
    pool_free_struct(pool);
}

void pool_reset(Pool* pool) {
    if (!pool || pool->valid != POOL_VALID_MARKER) return;
    pool_release_all_blocks(pool);
    pool_lookup_clear(pool);
}

void pool_set_mem_category(Pool* pool, int category) {
    if (!pool) return;
    pool->category = category >= 0 && category < MEM_CAT_COUNT
        ? (MemCategory)category : MEM_CAT_UNKNOWN;
}

void* pool_alloc(Pool* pool, size_t size) {
    if (!pool || pool->valid != POOL_VALID_MARKER || size == 0 || size > SIZE_LIMIT) {
        return NULL;
    }

    size_t header = block_header_size();
    size_t total = 0;
    if (!checked_add_size(header, size, &total)) return NULL;

    void* base = mem_alloc_loc ? mem_alloc_loc(total, pool->category, 0)
                               : malloc(total);
    if (!base) return NULL;
    PoolBlock* block = (PoolBlock*)base;
    block->magic = POOL_BLOCK_MAGIC;
    block->base = base;
    block->requested = size;
    block->allocated = total;
    void* user = (uint8_t*)base + header;
    if (!pool_lookup_insert(pool, user, block)) {
        mem_free_loc(base, 0);
        return NULL;
    }
    pool_link_block(pool, block);

    pool->alloc_bytes += size;
    pool->alloc_count++;
    pool->live_bytes += total;
    if (pool->live_bytes > pool->high_water_live_bytes) {
        pool->high_water_live_bytes = pool->live_bytes;
    }
    return (uint8_t*)base + header;
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
        log_error("pool_free: pointer %p is not owned by pool %u", ptr, pool->pool_id);
        return;
    }
    pool_release_block(pool, block, true);
}

void* pool_realloc(Pool* pool, void* ptr, size_t size) {
    if (!pool || pool->valid != POOL_VALID_MARKER || size > SIZE_LIMIT) return NULL;
    if (!ptr) return pool_alloc(pool, size);
    if (size == 0) {
        pool_free(pool, ptr);
        return NULL;
    }

    PoolBlock* old_block = pool_find_block(pool, ptr);
    if (!old_block) {
        log_error("pool_realloc: pointer %p is not owned by pool %u", ptr, pool->pool_id);
        return NULL;
    }
    size_t old_size = old_block->requested;
    void* new_ptr = pool_alloc(pool, size);
    if (!new_ptr) return NULL;
    memcpy(new_ptr, ptr, old_size < size ? old_size : size);
    pool_release_block(pool, old_block, true);
    return new_ptr;
}

char* pool_strdup(Pool* pool, const char* str) {
    if (!pool || !str) return NULL;
    size_t len = strlen(str);
    if (len == SIZE_MAX) return NULL;
    char* dup = (char*)pool_alloc(pool, len + 1);
    if (dup) memcpy(dup, str, len + 1);
    return dup;
}

void mempool_cleanup(void) {
    // Retained as a source-compatible no-op. Pool ownership is explicit and
    // there is no process-global allocator that needs finalization anymore.
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
    size_t used = 0;
    size_t count = 0;
    if (pool && pool->valid == POOL_VALID_MARKER) {
        used = pool->live_bytes;
        count = pool->alloc_count;
    }
    if (reserved) *reserved = used;
    if (in_use) *in_use = used;
    if (alloc_count) *alloc_count = count;
}

size_t pool_allocation_size(Pool* pool, void* ptr) {
    PoolBlock* block = pool_find_block(pool, ptr);
    return block ? block->allocated : 0;
}

void pool_get_detailed_stats(Pool* pool, PoolStats* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!pool || pool->valid != POOL_VALID_MARKER) return;
    pool_get_mem_stats(pool, &out->reserved_bytes, &out->live_bytes,
                       &out->allocation_count);
    out->high_water_live_bytes = pool->high_water_live_bytes;
    out->cumulative_bytes = pool->alloc_bytes;
    out->free_count = pool->free_count;
}
