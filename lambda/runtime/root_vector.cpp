// root_vector.cpp - growable, address-stable, precisely rooted Item vector.
// See root_vector.h for the contract (D5.1.1v2, D5.4.2; JSCU12).
#include "../lambda-data.hpp"   // full Item definition; lambda.h only forward-declares it in C++
#include "root_vector.h"
#include "heap_api.h"
#include "../../lib/memtrack.h"
#include "../../lib/log.h"
#include <string.h>

extern "C" Context* eval_context_tls_runtime(void);

struct RootVectorBlock {
    uint64_t slots[ROOT_VECTOR_BLOCK_SLOTS];  // exactly the registered range
};

static const char* root_vector_label(const RootVector* v) {
    return v && v->name ? v->name : "unnamed";
}

static Context* root_vector_owner(RootVector* v) {
    return v->owner ? v->owner : eval_context_tls_runtime();
}

static void root_vector_zero_range(RootVector* v, int64_t from, int64_t to) {
    for (int64_t i = from; i < to; i++) {
        v->blocks[i / ROOT_VECTOR_BLOCK_SLOTS]->slots[i % ROOT_VECTOR_BLOCK_SLOTS] = 0;
    }
}

// Registrations die with the heap. When the owner's generation moved, every
// retained Item belonged to the dead heap. Observing that drops the Items and
// forgets the registration; it never registers, so the non-publishing
// operations stay allocation-free (several are reached from NO_GC JIT
// imports such as js_with_restore_depth). Only push re-registers.
static void root_vector_observe_heap(RootVector* v) {
    uint64_t generation = heap_generation_for(root_vector_owner(v));
    if (generation == v->heap_generation) return;
    if (v->count > 0) root_vector_zero_range(v, 0, v->count);
    v->count = 0;
    v->heap_generation = 0;
}

// Publishing path: make the retained blocks scannable by the live heap before
// an Item lands in them. Returns false when the owner has no live heap.
static bool root_vector_sync_heap(RootVector* v) {
    root_vector_observe_heap(v);
    Context* owner = root_vector_owner(v);
    uint64_t generation = heap_generation_for(owner);
    if (generation == 0) return false;
    if (generation == v->heap_generation) return true;
    for (int i = 0; i < v->block_count; i++) {
        memset(v->blocks[i]->slots, 0, sizeof(v->blocks[i]->slots));
        heap_register_gc_root_range_for(owner, v->blocks[i]->slots,
                                        ROOT_VECTOR_BLOCK_SLOTS);
    }
    v->heap_generation = generation;
    return true;
}

static bool root_vector_grow(RootVector* v, Context* owner) {
    if (v->block_count >= v->block_capacity) {
        int capacity = v->block_capacity ? v->block_capacity * 2 : 4;
        RootVectorBlock** blocks = (RootVectorBlock**)mem_realloc(v->blocks,
            (size_t)capacity * sizeof(RootVectorBlock*), MEM_CAT_EVAL);
        if (!blocks) return false;
        v->blocks = blocks;
        v->block_capacity = capacity;
    }
    RootVectorBlock* block = (RootVectorBlock*)mem_calloc(1, sizeof(RootVectorBlock),
                                                          MEM_CAT_EVAL);
    if (!block) return false;
    // register before publishing: a later store may run inside a collection
    if (!heap_register_gc_root_range_for(owner, block->slots, ROOT_VECTOR_BLOCK_SLOTS)) {
        mem_free(block);
        return false;
    }
    v->blocks[v->block_count++] = block;
    return true;
}

extern "C" void root_vector_init(RootVector* v, Context* owner, const char* name) {
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->owner = owner;
    v->name = name;
}

extern "C" bool root_vector_push(RootVector* v, Item value) {
    if (!v) return false;
    if (!root_vector_sync_heap(v)) {
        log_error("root-vector: %s: push with no live heap", root_vector_label(v));
        return false;
    }
    int64_t capacity = (int64_t)v->block_count * ROOT_VECTOR_BLOCK_SLOTS;
    if (v->count >= capacity && !root_vector_grow(v, root_vector_owner(v))) {
        log_error("root-vector: %s: cannot grow", root_vector_label(v));
        return false;
    }
    v->blocks[v->count / ROOT_VECTOR_BLOCK_SLOTS]->slots[v->count % ROOT_VECTOR_BLOCK_SLOTS] =
        value.item;
    v->count++;
    if (v->count > v->high_water) v->high_water = v->count;
    return true;
}

extern "C" void root_vector_pop(RootVector* v) {
    if (!v) return;
    root_vector_observe_heap(v);
    if (v->count <= 0) return;
    v->count--;
    root_vector_zero_range(v, v->count, v->count + 1);
}

extern "C" Item* root_vector_at(RootVector* v, int64_t index) {
    if (!v) return NULL;
    root_vector_observe_heap(v);
    if (index < 0 || index >= v->count) return NULL;
    return (Item*)&v->blocks[index / ROOT_VECTOR_BLOCK_SLOTS]->slots[index % ROOT_VECTOR_BLOCK_SLOTS];
}

extern "C" int64_t root_vector_count(RootVector* v) {
    if (!v) return 0;
    root_vector_observe_heap(v);
    return v->count;
}

extern "C" void root_vector_clear(RootVector* v) {
    if (!v) return;
    root_vector_observe_heap(v);   // a replaced heap has already emptied it
    root_vector_zero_range(v, 0, v->count);
    v->count = 0;
}

extern "C" void root_vector_shrink(RootVector* v, int64_t count) {
    if (!v) return;
    if (count < 0) count = 0;
    root_vector_observe_heap(v);
    if (count >= v->count) return;
    root_vector_zero_range(v, count, v->count);
    v->count = count;
}

extern "C" void root_vector_destroy(RootVector* v) {
    if (!v) return;
    Context* owner = root_vector_owner(v);
    // unregister only from the heap the blocks are registered with; an older
    // heap's registry died with it
    bool live = heap_generation_for(owner) == v->heap_generation && v->heap_generation != 0;
    for (int i = 0; i < v->block_count; i++) {
        if (live) heap_unregister_gc_root_range_for(owner, v->blocks[i]->slots);
        mem_free(v->blocks[i]);
    }
    if (v->blocks) mem_free(v->blocks);
    Context* keep_owner = v->owner;
    const char* keep_name = v->name;
    memset(v, 0, sizeof(*v));
    v->owner = keep_owner;
    v->name = keep_name;
}

extern "C" int64_t root_vector_high_water(const RootVector* v) {
    return v ? v->high_water : 0;
}
