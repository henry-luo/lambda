#ifndef MEM_FACTORY_RT_H
#define MEM_FACTORY_RT_H

/**
 * Runtime allocator factory — factory wrappers for the higher-level,
 * lambda/runtime-level allocators that have heavier dependencies than the
 * lib-level pool/arena/scratch/nursery wrappers in lib/mem_factory.{h,c}:
 *
 *   - gc_heap   (runtime collector dependency chain via object/data zones)
 *
 * Keeping these here (compiled only into the main build, not the lib-only
 * unit tests) avoids pulling lambda/gc dependencies into the lib-level
 * mem_factory test.
 *
 * Each wrapper creates the allocator, registers a MemNode, and stores the node
 * on the allocator. The matching raw destroy/release auto-unregisters via a
 * release hook (installed lazily on first create).
 *
 * Passing ctx = NULL uses the process-global root context.
 */

#include "../../lib/mem_context.h"
#include "../../lib/mempool.h"
#include "../../lib/arena.h"
#include "gc/gc_heap.h"

// ---- GC heap ----
gc_heap_t* mem_gc_heap_create(MemContext* ctx, MemRole role, const char* label);
gc_heap_t* mem_gc_heap_create_with_pool(MemContext* ctx, Pool* pool,
                                        MemRole role, const char* label);

#endif // MEM_FACTORY_RT_H
