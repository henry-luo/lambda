#ifndef LAMBDA_CORE_MEM_FACTORY_H
#define LAMBDA_CORE_MEM_FACTORY_H

// Core allocator factories register Input-owned value pools without creating a
// runtime heap. This keeps document construction independent of GC startup.
#include "../../lib/mem_context.h"
#include "../../lib/mempool.h"
#include "../../lib/arena.h"
#include "../name_pool.hpp"
#include "../shape_pool.hpp"

NamePool* mem_name_pool_create(MemContext* ctx, Pool* backing, NamePool* parent,
                               MemRole role, const char* label);
ShapePool* mem_shape_pool_create(MemContext* ctx, Pool* backing, Arena* arena,
                                 ShapePool* parent, const char* label);

#endif
