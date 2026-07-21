/**
 * gc_data_zone.c - Bump-Pointer Allocator Implementation
 *
 * Variable-size data buffers are allocated via cursor increment.
 * No individual deallocation — bulk reset after GC compaction.
 */
#include "gc_data_zone.h"
#include "../log.h"
#include <stdlib.h>
#include <string.h>

// align size up to GC_DATA_ZONE_ALIGN boundary
static inline size_t align_up(size_t size) {
    return (size + GC_DATA_ZONE_ALIGN - 1) & ~(GC_DATA_ZONE_ALIGN - 1);
}

// allocate a new data block from the pool
static gc_data_block_t* allocate_block(gc_data_zone_t* dz, size_t min_size) {
    size_t capacity = dz->block_size;
    if (min_size > capacity) {
        // oversized allocation: create a block large enough
        capacity = align_up(min_size);
    }

    uint8_t* memory = (uint8_t*)pool_alloc(dz->pool, capacity);
    if (!memory) {
        log_error("gc_data_zone: failed to allocate block of %zu bytes", capacity);
        return NULL;
    }
    memset(memory, 0, capacity);

    gc_data_block_t* block = (gc_data_block_t*)calloc(1, sizeof(gc_data_block_t));
    if (!block) {
        log_error("gc_data_zone: failed to allocate block metadata");
        return NULL;
    }

    block->base = memory;
    block->cursor = memory;
    block->limit = memory + capacity;
    block->next = NULL;

    dz->total_blocks++;
    log_debug("gc_data_zone: allocated block %zu bytes (total blocks: %zu)",
              capacity, dz->total_blocks);
    return block;
}

gc_data_zone_t* gc_data_zone_create(Pool* pool, size_t block_size) {
    gc_data_zone_t* dz = (gc_data_zone_t*)calloc(1, sizeof(gc_data_zone_t));
    if (!dz) {
        log_error("gc_data_zone_create: failed to allocate zone");
        return NULL;
    }

    dz->pool = pool;
    dz->block_size = block_size > 0 ? block_size : GC_DATA_ZONE_BLOCK_SIZE;

    // allocate first block
    gc_data_block_t* first = allocate_block(dz, 0);
    if (!first) {
        free(dz);
        return NULL;
    }

    dz->head = first;
    dz->current = first;
    dz->total_allocated = 0;

    log_debug("gc_data_zone_create: block_size=%zu", dz->block_size);
    return dz;
}

void gc_data_zone_destroy(gc_data_zone_t* dz) {
    if (!dz) return;
    // free block metadata (block memory is pool-allocated, freed by pool_destroy)
    gc_data_block_t* block = dz->head;
    while (block) {
        gc_data_block_t* next = block->next;
        free(block);
        block = next;
    }
    log_debug("gc_data_zone_destroy: %zu blocks, %zu bytes allocated",
              dz->total_blocks, dz->total_allocated);
    free(dz);
}

void* gc_data_zone_alloc(gc_data_zone_t* dz, size_t size) {
    if (!dz || size == 0) return NULL;

    size = align_up(size);

    // try current block
    gc_data_block_t* block = dz->current;
    if (!block) {
        log_error("gc_data_zone_alloc: current block is NULL, dz=%p", (void*)dz);
        return NULL;
    }
    if (block->cursor + size <= block->limit) {
        void* ptr = block->cursor;
        block->cursor += size;
        dz->total_allocated += size;
        return ptr;  // already zeroed from block allocation or reset
    }

    // current block full — try subsequent existing blocks (after a reset, blocks still exist)
    while (block->next) {
        block = block->next;
        if (block->cursor + size <= block->limit) {
            dz->current = block;
            void* ptr = block->cursor;
            block->cursor += size;
            dz->total_allocated += size;
            return ptr;
        }
    }

    // all blocks full — allocate new block
    gc_data_block_t* new_block = allocate_block(dz, size);
    if (!new_block) {
        log_error("gc_data_zone_alloc: allocate_block failed for %zu bytes, total_blocks=%zu total_alloc=%zu",
                  size, dz->total_blocks, dz->total_allocated);
        return NULL;
    }

    // append to chain
    block->next = new_block;
    dz->current = new_block;

    void* ptr = new_block->cursor;
    new_block->cursor += size;
    dz->total_allocated += size;
    return ptr;
}

void gc_data_zone_reset(gc_data_zone_t* dz) {
    if (!dz) return;

    // reset all block cursors to base (retain blocks for reuse)
    gc_data_block_t* block = dz->head;
    while (block) {
        // zero the used portion for clean reuse
        size_t used = (size_t)(block->cursor - block->base);
        if (used > 0) {
            memset(block->base, 0, used);
        }
        block->cursor = block->base;
        block = block->next;
    }

    // start from the first block
    dz->current = dz->head;
    dz->total_allocated = 0;

    log_debug("gc_data_zone_reset: all blocks reset");
}

int gc_data_zone_owns(gc_data_zone_t* dz, void* ptr) {
    if (!dz || !ptr) return 0;
    uint8_t* p = (uint8_t*)ptr;

    gc_data_block_t* block = dz->head;
    while (block) {
        if (p >= block->base && p < block->limit) {
            return 1;
        }
        block = block->next;
    }
    return 0;
}

size_t gc_data_zone_used(gc_data_zone_t* dz) {
    if (!dz) return 0;
    size_t used = 0;
    gc_data_block_t* block = dz->head;
    while (block) {
        used += (size_t)(block->cursor - block->base);
        block = block->next;
    }
    return used;
}

void* gc_data_zone_copy(gc_data_zone_t* dz, const void* src, size_t size) {
    if (!src || size == 0) return NULL;
    void* dest = gc_data_zone_alloc(dz, size);
    if (dest) {
        memcpy(dest, src, size);
    }
    return dest;
}
