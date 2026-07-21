#include "mem_factory_rt.h"

// Release hook shared by all runtime allocator types: unregister the node when
// the allocator is destroyed / finally released. Installed lazily on first create.
static void rt_release_node(void* node) {
    mem_unregister_subtree((MemNode*)node);
}

static void ensure_rt_release_hooks(void) {
    static bool installed = false;
    if (!installed) {
        gc_heap_set_node_release_hook(rt_release_node);
        name_pool_set_node_release_hook(rt_release_node);
        shape_pool_set_node_release_hook(rt_release_node);
        installed = true;
    }
}

// ============================================================================
// GC heap
// ============================================================================

static bool heap_stat_fn(void* a, MemStatSample* s) {
    gc_heap_t* gc = (gc_heap_t*)a;
    s->bytes_reserved = (uint64_t)gc->total_allocated;
    s->bytes_in_use = (uint64_t)gc->total_allocated;
    s->alloc_count = (uint64_t)gc->object_count;
    return true;
}

static gc_heap_t* register_heap(MemContext* ctx, gc_heap_t* gc,
                                MemRole role, const char* label) {
    if (!gc) return NULL;
    MemNode* parent = gc->pool ? (MemNode*)pool_get_mem_node(gc->pool) : NULL;
    MemNode* n = mem_register(ctx ? ctx : mem_context_root(),
                              MEM_KIND_HEAP, role, label, gc, parent,
                              heap_stat_fn, NULL);
    gc->mem_node = n;
    return gc;
}

gc_heap_t* mem_gc_heap_create(MemContext* ctx, MemRole role, const char* label) {
    ensure_rt_release_hooks();
    return register_heap(ctx, gc_heap_create(), role, label);
}

gc_heap_t* mem_gc_heap_create_with_pool(MemContext* ctx, Pool* pool,
                                        MemRole role, const char* label) {
    ensure_rt_release_hooks();
    return register_heap(ctx, gc_heap_create_with_pool(pool), role, label);
}

// ============================================================================
// Name pool
// ============================================================================

static bool name_pool_stat_fn(void* a, MemStatSample* s) {
    // Interned names live in the backing Pool (counted under the pool node);
    // report 0 reserved/in-use to avoid double counting, expose the name count.
    s->bytes_reserved = 0;
    s->bytes_in_use = 0;
    s->alloc_count = (uint64_t)name_pool_count((NamePool*)a);
    return true;
}

NamePool* mem_name_pool_create(MemContext* ctx, Pool* backing, NamePool* parent,
                               MemRole role, const char* label) {
    ensure_rt_release_hooks();
    NamePool* np = name_pool_create(backing, parent);
    if (!np) return NULL;
    MemNode* p = backing ? (MemNode*)pool_get_mem_node(backing) : NULL;
    MemNode* n = mem_register(ctx ? ctx : mem_context_root(),
                              MEM_KIND_NAMEPOOL, role, label, np, p,
                              name_pool_stat_fn, NULL);
    np->mem_node = n;
    return np;
}

// ============================================================================
// Shape pool
// ============================================================================

static bool shape_pool_stat_fn(void* a, MemStatSample* s) {
    // Shapes live in the backing arena/pool; report 0 bytes, expose shape count.
    s->bytes_reserved = 0;
    s->bytes_in_use = 0;
    s->alloc_count = (uint64_t)shape_pool_count((ShapePool*)a);
    return true;
}

ShapePool* mem_shape_pool_create(MemContext* ctx, Pool* backing, Arena* arena,
                                 ShapePool* parent, const char* label) {
    ensure_rt_release_hooks();
    ShapePool* sp = shape_pool_create(backing, arena, parent);
    if (!sp) return NULL;
    MemNode* p = backing ? (MemNode*)pool_get_mem_node(backing) : NULL;
    MemNode* n = mem_register(ctx ? ctx : mem_context_root(),
                              MEM_KIND_SHAPEPOOL, MEM_ROLE_TYPE_SHAPE, label, sp, p,
                              shape_pool_stat_fn, NULL);
    sp->mem_node = n;
    return sp;
}
