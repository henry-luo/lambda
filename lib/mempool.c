#include "mempool.h"
#define RPMALLOC_FIRST_CLASS_HEAPS 1
#include <rpmalloc/rpmalloc.h>
#include <stdio.h>
#include <stdlib.h>  // for standard malloc/free for pool structure
#include <string.h>  // for memcpy, strlen
#include <unistd.h>  // for _exit
#include <pthread.h> // for thread-safe initialization

#define SIZE_LIMIT (1024 * 1024 * 1024)  // 1GB limit for single allocation

/**
 * Pool structure - simple pool tracking without heap handles
 * Uses rpmalloc global functions for thread-safe allocation
 * The pool structure itself is allocated with standard malloc
 */
struct Pool {
    rpmalloc_heap_t* heap;  // rpmalloc heap handle for this pool
    unsigned pool_id;       // unique pool identifier
    unsigned valid;         // validity marker (POOL_VALID_MARKER when valid)
};

#define POOL_VALID_MARKER 0xDEADBEEF

static unsigned next_pool_id = 1;
static int rpmalloc_initialized = 0;
static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
static __thread int thread_initialized = 0;

// Initialize rpmalloc lazily when first needed
static void ensure_rpmalloc_initialized(void) {
    // Global initialization
    if (!rpmalloc_initialized) {
        pthread_mutex_lock(&init_mutex);
        if (!rpmalloc_initialized) {
            if (rpmalloc_initialize(NULL) != 0) {
                fprintf(stderr, "ERROR: rpmalloc_initialize() failed\n");
                pthread_mutex_unlock(&init_mutex);
                return;
            }
            rpmalloc_initialized = 1;
        }
        pthread_mutex_unlock(&init_mutex);
    }

    // Thread-local initialization
    if (!thread_initialized) {
        rpmalloc_thread_initialize();
        thread_initialized = 1;
    }
}

Pool* pool_create(void) {
    // Initialize rpmalloc lazily when first pool is created
    ensure_rpmalloc_initialized();

    // Allocate pool structure using standard malloc (not rpmalloc)
    // This keeps the pool management separate from rpmalloc
    Pool* pool = (Pool*)malloc(sizeof(Pool));
    if (!pool) {
        return NULL;
    }

    // Acquire a dedicated heap for this pool
    pool->heap = rpmalloc_heap_acquire();
    if (!pool->heap) {
        free(pool);
        return NULL;
    }

    pool->pool_id = next_pool_id++;
    pool->valid = POOL_VALID_MARKER;

    return pool;
}

void pool_destroy(Pool* pool) {
    if (!pool || pool->valid != POOL_VALID_MARKER) {
        return;
    }

    // Release the heap - this will free all allocations made from it
    // Note: rpmalloc_heap_release already frees all memory, so we don't need heap_free_all
    if (pool->heap) {
        rpmalloc_heap_release(pool->heap);
    }

    // Mark as invalid and free the pool structure using standard free
    pool->valid = 0;
    free(pool);  // Use standard free since we used standard malloc
}

void* pool_alloc(Pool* pool, size_t size) {
    if (!pool || pool->valid != POOL_VALID_MARKER) {
        return NULL;
    }
    if (size > SIZE_LIMIT) {
        return NULL;  // Overflow protection
    }
    // Use heap-specific allocation for better memory isolation
    return rpmalloc_heap_alloc(pool->heap, size);
}

void* pool_calloc(Pool* pool, size_t size) {
    if (!pool || pool->valid != POOL_VALID_MARKER) {
        return NULL;
    }
    if (size > SIZE_LIMIT) {
        return NULL;  // Overflow protection
    }
    // Use heap-specific zeroed allocation for better memory isolation
    return rpmalloc_heap_calloc(pool->heap, 1, size);
}

void pool_free(Pool* pool, void* ptr) {
    if (!pool || pool->valid != POOL_VALID_MARKER || !ptr) {
        return;
    }
    // Free using heap-specific free for better memory isolation
    rpmalloc_heap_free(pool->heap, ptr);
}

void* pool_realloc(Pool* pool, void* ptr, size_t size) {
    if (!pool || pool->valid != POOL_VALID_MARKER) {
        return NULL;
    }
    // Handle NULL pointer case (should behave like malloc)
    if (!ptr) {
        return rpmalloc_heap_alloc(pool->heap, size);
    }
    // Handle zero size case (should behave like free)
    if (size == 0) {
        rpmalloc_heap_free(pool->heap, ptr);
        return NULL;
    }
    if (size > SIZE_LIMIT) {
        return NULL;  // Overflow protection
    }
    // Use heap-specific reallocation for better memory isolation
    return rpmalloc_heap_realloc(pool->heap, ptr, size, 0);
}

void mempool_cleanup(void) {
    if (rpmalloc_initialized) {
        pthread_mutex_lock(&init_mutex);
        if (rpmalloc_initialized) {
            // rpmalloc_thread_finalize();
            rpmalloc_finalize();
            rpmalloc_initialized = 0;
        }
        pthread_mutex_unlock(&init_mutex);
    }
}

char* pool_strdup(Pool* pool, const char* str) {
    if (!pool || !str) return NULL;
    
    size_t len = strlen(str) + 1;
    char* dup = (char*)pool_alloc(pool, len);
    if (dup) {
        memcpy(dup, str, len);
    }
    return dup;
}
