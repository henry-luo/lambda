#include "mem_factory_core.h"

static void core_release_node(void* node) {
    mem_unregister_subtree((MemNode*)node);
}

static void ensure_core_release_hooks(void) {
    static bool installed = false;
    if (!installed) {
        name_pool_set_node_release_hook(core_release_node);
        shape_pool_set_node_release_hook(core_release_node);
        installed = true;
    }
}

static bool name_pool_stat_fn(void* a, MemStatSample* s) {
    // Names live in the backing Pool, so report only their logical count.
    s->bytes_reserved = 0;
    s->bytes_in_use = 0;
    s->alloc_count = (uint64_t)name_pool_count((NamePool*)a);
    return true;
}

NamePool* mem_name_pool_create(MemContext* ctx, Pool* backing, NamePool* parent,
                               MemRole role, const char* label) {
    ensure_core_release_hooks();
    NamePool* np = name_pool_create(backing, parent);
    if (!np) return NULL;
    MemNode* pool_node = backing ? (MemNode*)pool_get_mem_node(backing) : NULL;
    np->mem_node = mem_register(ctx ? ctx : mem_context_root(),
                                MEM_KIND_NAMEPOOL, role, label, np, pool_node,
                                name_pool_stat_fn, NULL);
    return np;
}

static bool shape_pool_stat_fn(void* a, MemStatSample* s) {
    // Shapes live in the backing arena/pool, so report only their logical count.
    s->bytes_reserved = 0;
    s->bytes_in_use = 0;
    s->alloc_count = (uint64_t)shape_pool_count((ShapePool*)a);
    return true;
}

ShapePool* mem_shape_pool_create(MemContext* ctx, Pool* backing, Arena* arena,
                                 ShapePool* parent, const char* label) {
    ensure_core_release_hooks();
    ShapePool* sp = shape_pool_create(backing, arena, parent);
    if (!sp) return NULL;
    MemNode* pool_node = backing ? (MemNode*)pool_get_mem_node(backing) : NULL;
    sp->mem_node = mem_register(ctx ? ctx : mem_context_root(),
                                MEM_KIND_SHAPEPOOL, MEM_ROLE_TYPE_SHAPE, label,
                                sp, pool_node, shape_pool_stat_fn, NULL);
    return sp;
}
