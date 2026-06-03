#ifndef GC_NURSERY_H
#define GC_NURSERY_H

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include "../datetime.h"

#ifdef __cplusplus
extern "C" {
#endif

// GC Nursery: a bump-pointer allocator for numeric temporaries (int64, double, DateTime).
// Replaces num_stack with a simpler, non-frame-coupled allocator.
// Values allocated from the nursery persist until nursery_destroy() — no frame-based reset.

#define GC_NURSERY_BLOCK_SIZE (32 * 1024)  // 32 KB per block (4096 values of 8 bytes)

typedef union {
    int64_t as_long;
    double as_double;
    DateTime as_datetime;
} gc_num_value_t;

typedef struct gc_nursery_block {
    gc_num_value_t* data;           // array of values
    size_t capacity;                // max elements in this block
    size_t used;                    // currently used elements
    struct gc_nursery_block* next;  // next block in linked list
} gc_nursery_block_t;

typedef struct gc_nursery {
    gc_nursery_block_t* head;       // first block
    gc_nursery_block_t* current;    // current allocation block
    size_t block_size;              // elements per block
    size_t total_allocated;         // total elements allocated across all blocks
    void* mem_node;                 // MemContext registration node (NULL if untracked)
} gc_nursery_t;

// lifecycle
gc_nursery_t* gc_nursery_create(size_t block_size);
void gc_nursery_destroy(gc_nursery_t* nursery);

// Install a hook called by gc_nursery_destroy to release a registered
// nursery's mem_node. Set by the allocator factory; NULL by default (no-op).
void gc_nursery_set_node_release_hook(void (*fn)(void* node));

// allocation — returns stable pointer, never invalidated until destroy
int64_t* gc_nursery_alloc_long(gc_nursery_t* nursery, int64_t value);
double* gc_nursery_alloc_double(gc_nursery_t* nursery, double value);
DateTime* gc_nursery_alloc_datetime(gc_nursery_t* nursery, DateTime value);

// stats
size_t gc_nursery_total_allocated(gc_nursery_t* nursery);

#ifdef __cplusplus
}
#endif

#endif // GC_NURSERY_H
