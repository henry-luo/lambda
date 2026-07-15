#include "mem_factory.h"

// Release hook installed into mempool.c / arena.c so that ANY pool_destroy /
// arena_destroy path (factory or raw) safely unregisters a tracked node — no
// dangling node can outlive its allocator. Installed lazily on first create.
static void release_node(void* node) {
    // subtree: destroying a pool/arena also unregisters child allocators whose
    // memory lives in it (arenas, name/shape pools) — prevents dangling nodes.
    mem_unregister_subtree((MemNode*)node);
}

static void ensure_release_hooks(void) {
    static bool installed = false;
    if (!installed) {
        pool_set_node_release_hook(release_node);
        arena_set_node_release_hook(release_node);
        scratch_set_node_release_hook(release_node);
        installed = true;
    }
}

// ============================================================================
// Pool factory
// ============================================================================

static bool pool_stat_fn(void* a, MemStatSample* s) {
    size_t reserved = 0, in_use = 0, count = 0;
    int is_mmap = 0;
    pool_get_mem_stats((Pool*)a, &reserved, &in_use, &count, &is_mmap);
    s->bytes_reserved = reserved;
    s->bytes_in_use = in_use;
    s->alloc_count = count;
    if (is_mmap) s->flags |= MEM_FLAG_MMAP;
    return true;
}

static void pool_destroy_fn(void* a) {
    pool_destroy((Pool*)a);
}

Pool* mem_pool_create(MemContext* ctx, MemRole role, const char* label) {
    ensure_release_hooks();
    Pool* p = pool_create();
    if (!p) return NULL;
    MemNode* n = mem_register(ctx ? ctx : mem_context_root(),
                              MEM_KIND_POOL, role, label, p, NULL,
                              pool_stat_fn, pool_destroy_fn);
    pool_set_mem_node(p, n);
    return p;
}

Pool* mem_pool_create_mmap(MemContext* ctx, MemRole role, const char* label) {
    ensure_release_hooks();
    Pool* p = pool_create_mmap();
    if (!p) return NULL;
    MemNode* n = mem_register(ctx ? ctx : mem_context_root(),
                              MEM_KIND_POOL, role, label, p, NULL,
                              pool_stat_fn, pool_destroy_fn);
    pool_set_mem_node(p, n);
    return p;
}

void mem_pool_destroy(Pool* pool) {
    // pool_destroy() auto-unregisters the mem_node (see lib/mempool.c), so any
    // destroy path — factory or raw — is safe. This wrapper exists for API
    // symmetry with mem_pool_create().
    pool_destroy(pool);
}

// ============================================================================
// Arena factory
// ============================================================================

static bool arena_stat_fn(void* a, MemStatSample* s) {
    Arena* ar = (Arena*)a;
    s->bytes_reserved = arena_total_allocated(ar);
    s->bytes_in_use = arena_total_used(ar);
    s->chunk_count = (uint32_t)arena_chunk_count(ar);
    return true;
}

static void arena_destroy_fn(void* a) {
    arena_destroy((Arena*)a);
}

static Arena* register_arena(MemContext* ctx, Pool* backing, Arena* ar,
                             MemRole role, const char* label) {
    if (!ar) return NULL;
    ensure_release_hooks();
    MemNode* parent = backing ? (MemNode*)pool_get_mem_node(backing) : NULL;
    MemNode* n = mem_register(ctx ? ctx : mem_context_root(),
                              MEM_KIND_ARENA, role, label, ar, parent,
                              arena_stat_fn, arena_destroy_fn);
    arena_set_mem_node(ar, n);
    return ar;
}

Arena* mem_arena_create(MemContext* ctx, Pool* backing, MemRole role, const char* label) {
    return register_arena(ctx, backing, arena_create_default(backing), role, label);
}

Arena* mem_arena_create_sized(MemContext* ctx, Pool* backing,
                              size_t initial_chunk_size, size_t max_chunk_size,
                              MemRole role, const char* label) {
    return register_arena(ctx, backing,
                          arena_create(backing, initial_chunk_size, max_chunk_size),
                          role, label);
}

void mem_arena_destroy(Arena* arena) {
    // arena_destroy() auto-unregisters the mem_node (see lib/arena.c).
    arena_destroy(arena);
}

// ============================================================================
// Scratch arena factory
// ============================================================================

static bool scratch_stat_fn(void* a, MemStatSample* s) {
    // Scratch memory is allocated from its backing arena (already counted under
    // the arena node), so report reserved/in_use as 0 to avoid double counting;
    // expose the live allocation count for diagnostics.
    s->bytes_reserved = 0;
    s->bytes_in_use = 0;
    s->alloc_count = (uint64_t)scratch_live_count((ScratchArena*)a);
    return true;
}

void mem_scratch_init(MemContext* ctx, ScratchArena* sa, Arena* backing,
                      MemRole role, const char* label) {
    if (!sa) return;
    ensure_release_hooks();
    scratch_init(sa, backing);
    MemNode* parent = backing ? (MemNode*)arena_get_mem_node(backing) : NULL;
    MemNode* n = mem_register(ctx ? ctx : mem_context_root(),
                              MEM_KIND_SCRATCH, role, label, sa, parent,
                              scratch_stat_fn, NULL);
    sa->mem_node = n;
}
