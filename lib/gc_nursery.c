#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "gc_nursery.h"
#include "log.h"

// allocate a new block with the given capacity
static gc_nursery_block_t* gc_nursery_alloc_block(size_t capacity) {
    gc_nursery_block_t* block = (gc_nursery_block_t*)malloc(sizeof(gc_nursery_block_t));
    if (!block) return NULL;

    block->data = (gc_num_value_t*)malloc(sizeof(gc_num_value_t) * capacity);
    if (!block->data) {
        free(block);
        return NULL;
    }

    block->capacity = capacity;
    block->used = 0;
    block->next = NULL;
    return block;
}

// create a new nursery with the given block size (number of elements per block)
gc_nursery_t* gc_nursery_create(size_t block_size) {
    if (block_size == 0) {
        block_size = GC_NURSERY_BLOCK_SIZE / sizeof(gc_num_value_t);  // default: 4096 values
    }

    gc_nursery_t* nursery = (gc_nursery_t*)malloc(sizeof(gc_nursery_t));
    if (!nursery) return NULL;

    gc_nursery_block_t* first_block = gc_nursery_alloc_block(block_size);
    if (!first_block) {
        free(nursery);
        return NULL;
    }

    nursery->head = first_block;
    nursery->current = first_block;
    nursery->block_size = block_size;
    nursery->total_allocated = 0;

    log_debug("gc nursery created: block_size=%zu, block_bytes=%zu",
              block_size, block_size * sizeof(gc_num_value_t));
    return nursery;
}

// destroy the nursery and all its blocks
void gc_nursery_destroy(gc_nursery_t* nursery) {
    if (!nursery) return;

    gc_nursery_block_t* block = nursery->head;
    while (block) {
        gc_nursery_block_t* next = block->next;
        free(block->data);
        free(block);
        block = next;
    }

    log_debug("gc nursery destroyed: total_allocated=%zu", nursery->total_allocated);
    free(nursery);
}

// allocate a slot in the nursery, growing if needed
static gc_num_value_t* gc_nursery_alloc_slot(gc_nursery_t* nursery) {
    if (!nursery) return NULL;

    // check if current block has space
    if (nursery->current->used >= nursery->current->capacity) {
        // allocate a new block (same size as the first — no geometric growth needed)
        gc_nursery_block_t* new_block = gc_nursery_alloc_block(nursery->block_size);
        if (!new_block) {
            log_error("gc nursery: failed to allocate new block");
            return NULL;
        }
        nursery->current->next = new_block;
        nursery->current = new_block;
        log_debug("gc nursery: allocated new block, total_allocated=%zu", nursery->total_allocated);
    }

    gc_num_value_t* slot = &nursery->current->data[nursery->current->used];
    nursery->current->used++;
    nursery->total_allocated++;
    return slot;
}

// allocate and store an int64 value
int64_t* gc_nursery_alloc_long(gc_nursery_t* nursery, int64_t value) {
    gc_num_value_t* slot = gc_nursery_alloc_slot(nursery);
    if (!slot) return NULL;
    slot->as_long = value;
    return &slot->as_long;
}

// allocate and store a double value
double* gc_nursery_alloc_double(gc_nursery_t* nursery, double value) {
    gc_num_value_t* slot = gc_nursery_alloc_slot(nursery);
    if (!slot) return NULL;
    slot->as_double = value;
    return &slot->as_double;
}

// allocate and store a DateTime value
DateTime* gc_nursery_alloc_datetime(gc_nursery_t* nursery, DateTime value) {
    gc_num_value_t* slot = gc_nursery_alloc_slot(nursery);
    if (!slot) return NULL;
    slot->as_datetime = value;
    return &slot->as_datetime;
}

// get total number of values allocated
size_t gc_nursery_total_allocated(gc_nursery_t* nursery) {
    return nursery ? nursery->total_allocated : 0;
}
