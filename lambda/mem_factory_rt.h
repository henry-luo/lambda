#ifndef MEM_FACTORY_RT_H
#define MEM_FACTORY_RT_H

/**
 * Runtime allocator factory — factory wrappers for the higher-level,
 * lambda/runtime-level allocators that have heavier dependencies than the
 * lib-level pool/arena/scratch/nursery wrappers in lib/mem_factory.{h,c}:
 *
 *   - gc_heap   (lib/gc — heavy dependency chain via object/data zones)
 *   - NamePool  (lambda — ref-counted release)
 *   - ShapePool (lambda — ref-counted release)
 *
 * Keeping these here (compiled only into the main build, not the lib-only
 * unit tests) avoids pulling lambda/gc dependencies into the lib-level
 * mem_factory test.
 *
 * Each wrapper creates the allocator, registers a MemNode, and stores the node
 * on the allocator. The matching raw destroy/release auto-unregisters via a
 * release hook (installed lazily on first create). For the ref-counted pools,
 * the hook fires only when the pool is actually freed (ref_count reaches 0).
 *
 * Passing ctx = NULL uses the process-global root context.
 */

#include "../lib/mem_context.h"
#include "../lib/mempool.h"
#include "../lib/arena.h"
#include "../lib/gc/gc_heap.h"
#include "name_pool.hpp"
#include "shape_pool.hpp"

// ---- GC heap ----
gc_heap_t* mem_gc_heap_create(MemContext* ctx, MemRole role, const char* label);
gc_heap_t* mem_gc_heap_create_with_pool(MemContext* ctx, Pool* pool,
                                        MemRole role, const char* label);

// ---- Name pool (ref-counted; node released at ref_count 0) ----
NamePool* mem_name_pool_create(MemContext* ctx, Pool* backing, NamePool* parent,
                               MemRole role, const char* label);

// ---- Shape pool (ref-counted; node released at ref_count 0) ----
ShapePool* mem_shape_pool_create(MemContext* ctx, Pool* backing, Arena* arena,
                                 ShapePool* parent, const char* label);

#endif // MEM_FACTORY_RT_H
