#pragma once

// RootVector: the one growable, address-stable, precisely scanned Item
// collection for context-owned non-LIFO state (D5.1.1v2, D5.4.2; JSCU12).
//
// Storage is a chain of fixed 64-slot blocks. A block is registered as a GC
// root range with the owning context's heap before its first Item is
// published, and it never moves, reallocates, or is freed while that heap can
// scan it. Vacated slots are cleared so a fully scanned block never retains a
// dead Item. When the owner's heap is replaced (its generation changes), every
// retained Item belonged to the dead heap: the vector empties and re-registers
// its blocks with the new heap on the next use. Block storage is retained
// until destroy. There is no workload-visible maximum.
//
// LIFO lifetimes keep using RootFrame/Rooted on the side stack; this is for
// stacks, journals and caches whose entries outlive one native activation.
// Never interleave POD metadata in a block (JSCU13): keep it in a parallel
// ordinary array.

#include "../lambda.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { ROOT_VECTOR_BLOCK_SLOTS = 64 };

typedef struct RootVectorBlock RootVectorBlock;

typedef struct RootVector {
    Context* owner;             // context whose heap scans the blocks (NULL = TLS)
    RootVectorBlock** blocks;   // POD index of blocks, never scanned
    int block_count;
    int block_capacity;
    int64_t count;              // published Items
    int64_t high_water;         // diagnostic: maximum count ever published
    uint64_t heap_generation;   // heap incarnation the blocks are registered with
    const char* name;           // diagnostic label
} RootVector;

void root_vector_init(RootVector* v, Context* owner, const char* name);
bool root_vector_push(RootVector* v, Item value);        // may allocate a block
void root_vector_pop(RootVector* v);                     // clears the vacated slot
Item* root_vector_at(RootVector* v, int64_t index);      // stable address; NULL if out of range
int64_t root_vector_count(RootVector* v);
void root_vector_clear(RootVector* v);                   // drops Items, keeps blocks
void root_vector_shrink(RootVector* v, int64_t count);   // drops Items above count
void root_vector_destroy(RootVector* v);                 // unregisters and frees blocks
int64_t root_vector_high_water(const RootVector* v);

#ifdef __cplusplus
}
#endif
