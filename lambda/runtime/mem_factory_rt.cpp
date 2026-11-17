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
        installed = true;
    }
}

// ============================================================================
// GC heap
// ============================================================================

static bool heap_stat_fn(void* a, MemStatSample* s) {
    gc_heap_t* gc = (gc_heap_t*)a;
    gc_memory_stats_t memory = gc_heap_get_memory_stats(gc);
    s->bytes_reserved = memory.object_reserved + memory.nursery_reserved + memory.tenured_reserved;
    s->bytes_in_use = memory.object_used + memory.nursery_used + memory.tenured_used;
    s->committed_bytes = memory.object_capacity + memory.nursery_capacity + memory.tenured_capacity;
    s->alloc_count = (uint64_t)gc->object_count;
    return true;
}

static gc_heap_t* register_heap(MemContext* ctx, gc_heap_t* gc,
                                MemRole role, const char* label) {
    if (!gc) return NULL;
    MemNode* n = mem_register(ctx ? ctx : mem_context_root(),
                              MEM_KIND_HEAP, role, label, gc, NULL,
                              heap_stat_fn, NULL);
    gc->mem_node = n;
    return gc;
}

gc_heap_t* mem_gc_heap_create(MemContext* ctx, MemRole role, const char* label) {
    ensure_rt_release_hooks();
    return register_heap(ctx, gc_heap_create(), role, label);
}
