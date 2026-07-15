#ifndef MEM_FACTORY_H
#define MEM_FACTORY_H

/**
 * Allocator factory — the sanctioned path for creating Pools and Arenas that
 * are owned and tracked by a MemContext (see mem_context.h, vibe/Memory_Context.md).
 *
 * Each factory call creates the underlying allocator, registers a MemNode with
 * the given role/label and the correct backing (parent) edge, and stores the
 * node on the allocator. The matching mem_*_destroy unregisters the node and
 * destroys the allocator.
 *
 * Passing ctx = NULL uses the process-global root context.
 *
 * Phase 2 covers Pool + Arena. Scratch/Heap/Nursery factories follow in later
 * phases. Allocators created via the raw primitives (pool_create/arena_create)
 * remain valid and simply carry a NULL mem_node (untracked).
 */

#include "mem_context.h"
#include "mempool.h"
#include "arena.h"
#include "scratch_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- Pools ----
Pool* mem_pool_create(MemContext* ctx, MemRole role, const char* label);
Pool* mem_pool_create_mmap(MemContext* ctx, MemRole role, const char* label);
// Unregister + destroy. Safe on NULL and on untracked pools.
void  mem_pool_destroy(Pool* pool);

// ---- Arenas (backing pool's node becomes the parent edge) ----
Arena* mem_arena_create(MemContext* ctx, Pool* backing, MemRole role, const char* label);
Arena* mem_arena_create_sized(MemContext* ctx, Pool* backing,
                              size_t initial_chunk_size, size_t max_chunk_size,
                              MemRole role, const char* label);
void   mem_arena_destroy(Arena* arena);

// ---- Scratch arenas (caller-owned struct; node parented to the backing arena) ----
// Initializes `sa` (via scratch_init) and registers a MEM_KIND_SCRATCH node.
// The node is auto-unregistered by scratch_release(). Re-calling mem_scratch_init
// after a release re-registers.
void mem_scratch_init(MemContext* ctx, ScratchArena* sa, Arena* backing,
                      MemRole role, const char* label);

#ifdef __cplusplus
}
#endif

#endif // MEM_FACTORY_H
