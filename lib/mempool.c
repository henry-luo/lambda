#include "mempool.h"
#include <jemalloc/jemalloc.h>

// If you want to tie all allocations to a specific arena, you can.
// For now, we use the default jemalloc behavior, which is fine.

void* pool_alloc(size_t size) {
    return je_malloc(size);
}

void* pool_calloc(size_t n, size_t size) {
    return je_calloc(n, size);
}

void pool_free(void* ptr) {
    je_free(ptr);
}
