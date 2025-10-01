#include "mempool.h"
#include <jemalloc/jemalloc.h>
#include <stdio.h>

/**
 * Pool structure containing arena information
 */
struct Pool {
    unsigned arena_index;  // jemalloc arena index
    unsigned valid;       // validity marker
};

Pool* pool_create(void) {
    // Create new arena
    unsigned arena_index;
    size_t sz = sizeof(unsigned);

    // Create a new arena
    if (je_mallctl("arenas.create", &arena_index, &sz, NULL, 0) != 0) {
        return NULL;
    }

    // Allocate pool structure from default arena
    Pool* pool = (Pool*)je_malloc(sizeof(Pool));
    if (!pool) {
        return NULL;
    }

    pool->arena_index = arena_index;
    pool->valid = 0xDEADBEEF;  // Magic number for validation

    return pool;
}

void pool_destroy(Pool* pool) {
    if (!pool || pool->valid != 0xDEADBEEF) {
        return;
    }

    // Note: We don't destroy the arena here since individual allocations
    // may still be in use. The arena will be cleaned up when jemalloc exits.
    // In a production system, you would track all allocations and free them first.

    // Mark as invalid and free the pool structure
    pool->valid = 0;
    je_free(pool);
}

void* pool_alloc(Pool* pool, size_t size) {
    if (!pool || pool->valid != 0xDEADBEEF) {
        return NULL;
    }

    // Use arena-specific allocation
    return je_mallocx(size, MALLOCX_ARENA(pool->arena_index));
}

void* pool_calloc(Pool* pool, size_t n, size_t size) {
    if (!pool || pool->valid != 0xDEADBEEF) {
        return NULL;
    }

    // Calculate total size with overflow check
    if (n != 0 && size > SIZE_MAX / n) {
        return NULL;  // Overflow
    }

    size_t total_size = n * size;
    void* ptr = je_mallocx(total_size, MALLOCX_ARENA(pool->arena_index) | MALLOCX_ZERO);
    return ptr;
}

void pool_free(Pool* pool, void* ptr) {
    if (!pool || pool->valid != 0xDEADBEEF || !ptr) {
        return;
    }

    // Free back to the specific arena using dallocx
    je_dallocx(ptr, MALLOCX_ARENA(pool->arena_index));
}

void* pool_realloc(Pool* pool, void* ptr, size_t size) {
    if (!pool || pool->valid != 0xDEADBEEF) {
        return NULL;
    }

    // Handle NULL pointer case (should behave like malloc)
    if (!ptr) {
        return je_mallocx(size, MALLOCX_ARENA(pool->arena_index));
    }

    // Handle zero size case (should behave like free)
    if (size == 0) {
        je_dallocx(ptr, MALLOCX_ARENA(pool->arena_index));
        return NULL;
    }

    // Use arena-specific reallocation
    return je_rallocx(ptr, size, MALLOCX_ARENA(pool->arena_index));
}
