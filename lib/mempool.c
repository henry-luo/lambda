#include "mempool.h"
#include "log.h"
#define RPMALLOC_FIRST_CLASS_HEAPS 1
#include <rpmalloc/rpmalloc.h>
#include <stdio.h>
#include <stdlib.h>  // for standard malloc/free for pool structure
#include <string.h>  // for memcpy, strlen
#include <unistd.h>  // for _exit
#include <pthread.h> // for thread-safe initialization
#include <sys/mman.h> // for mmap/munmap

#define SIZE_LIMIT (1024 * 1024 * 1024)  // 1GB limit for single allocation

// mmap chunk for bump-allocator pools (no rpmalloc involvement)
typedef struct MmapChunk {
    struct MmapChunk* next;
    uint8_t* base;
    size_t size;
} MmapChunk;

#define MMAP_CHUNK_SIZE (4 * 1024 * 1024)  // 4 MB per chunk

struct Pool {
    rpmalloc_heap_t* heap;  // rpmalloc heap handle (NULL for mmap mode)
    unsigned pool_id;       // unique pool identifier
    unsigned valid;         // validity marker (POOL_VALID_MARKER when valid)
    // mmap mode fields (used when heap == NULL)
    MmapChunk* chunks;     // linked list of mmap'd regions
    uint8_t* cursor;       // bump pointer within current chunk
    uint8_t* limit;        // end of current chunk
};

#define POOL_VALID_MARKER 0xDEADBEEF

static unsigned next_pool_id = 1;
static int rpmalloc_initialized = 0;
static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
static __thread int thread_initialized = 0;

// ============================================================================
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
    Pool* pool = (Pool*)malloc(sizeof(Pool));
    if (!pool) {
        return NULL;
    }

    // Acquire a fresh first-class heap for this pool
    pool->heap = rpmalloc_heap_acquire();
    if (!pool->heap) {
        free(pool);
        return NULL;
    }

    pool->pool_id = next_pool_id++;
    pool->valid = POOL_VALID_MARKER;
    pool->chunks = NULL;
    pool->cursor = NULL;
    pool->limit = NULL;

    return pool;
}

// ============================================================================
// mmap-backed bump allocator pool (bypasses rpmalloc entirely)
// ============================================================================

static void mmap_pool_grow(Pool* pool, size_t min_size) {
    size_t chunk_size = min_size < MMAP_CHUNK_SIZE ? MMAP_CHUNK_SIZE : min_size;
    chunk_size = (chunk_size + 4095) & ~4095;  // page-align
    void* mem = mmap(NULL, chunk_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        log_error("mmap_pool_grow: mmap failed for %zu bytes", chunk_size);
        pool->cursor = NULL;
        pool->limit = NULL;
        return;
    }
    MmapChunk* chunk = (MmapChunk*)malloc(sizeof(MmapChunk));
    chunk->base = (uint8_t*)mem;
    chunk->size = chunk_size;
    chunk->next = pool->chunks;
    pool->chunks = chunk;
    pool->cursor = (uint8_t*)mem;
    pool->limit = (uint8_t*)mem + chunk_size;
}

Pool* pool_create_mmap(void) {
    Pool* pool = (Pool*)malloc(sizeof(Pool));
    if (!pool) return NULL;
    pool->heap = NULL;  // signals mmap mode
    pool->pool_id = next_pool_id++;
    pool->valid = POOL_VALID_MARKER;
    pool->chunks = NULL;
    pool->cursor = NULL;
    pool->limit = NULL;
    mmap_pool_grow(pool, MMAP_CHUNK_SIZE);
    return pool;
}

static void mmap_pool_free_chunks(Pool* pool) {
    MmapChunk* chunk = pool->chunks;
    while (chunk) {
        MmapChunk* next = chunk->next;
        munmap(chunk->base, chunk->size);
        free(chunk);
        chunk = next;
    }
    pool->chunks = NULL;
    pool->cursor = NULL;
    pool->limit = NULL;
}

void pool_destroy(Pool* pool) {
    if (!pool || pool->valid != POOL_VALID_MARKER) {
        log_debug("pool_destroy: skipping invalid pool=%p (valid=0x%x)", (void*)pool, pool ? pool->valid : 0);
        return;
    }

    log_debug("pool_destroy: destroying pool=%p (id=%u)", (void*)pool, pool->pool_id);

    if (pool->heap) {
        // rpmalloc mode
        rpmalloc_heap_free_all(pool->heap);
        rpmalloc_heap_release(pool->heap);
    } else {
        // mmap mode
        mmap_pool_free_chunks(pool);
    }

    pool->valid = 0;
    free(pool);
}

void pool_drain(Pool* pool) {
    if (!pool || pool->valid != POOL_VALID_MARKER) return;
    if (pool->heap) {
        rpmalloc_heap_free_all(pool->heap);
        rpmalloc_heap_release(pool->heap);
        pool->heap = NULL;
    } else {
        mmap_pool_free_chunks(pool);
    }
    pool->valid = 0;
}

void pool_reset(Pool* pool) {
    if (!pool || pool->valid != POOL_VALID_MARKER) return;
    if (pool->heap) {
        rpmalloc_heap_free_all(pool->heap);
    } else {
        // mmap mode: munmap all chunks, allocate fresh initial chunk
        mmap_pool_free_chunks(pool);
        mmap_pool_grow(pool, MMAP_CHUNK_SIZE);
    }
}

void* pool_alloc(Pool* pool, size_t size) {
    if (!pool) {
        log_error("pool_alloc: pool is NULL");
        return NULL;
    }
    if (pool->valid != POOL_VALID_MARKER) {
        log_error("pool_alloc: pool invalid marker (got 0x%x, expected 0x%x)", pool->valid, POOL_VALID_MARKER);
        return NULL;
    }
    if (size > SIZE_LIMIT) {
        log_error("pool_alloc: size %zu exceeds limit", size);
        return NULL;
    }
    if (pool->heap) {
        // rpmalloc mode
        if ((uintptr_t)pool->heap < 0x10000) {
            log_error("pool_alloc: corrupted heap pointer %p in pool %u", (void*)pool->heap, pool->pool_id);
            return NULL;
        }
        void* result = rpmalloc_heap_alloc(pool->heap, size);
        if (!result) {
            log_error("pool_alloc: rpmalloc_heap_alloc returned NULL (heap=%p, size=%zu)", pool->heap, size);
        }
        return result;
    }
    // mmap mode: bump allocate (16-byte aligned)
    size = (size + 15) & ~15;
    if (!pool->cursor || pool->cursor + size > pool->limit) {
        mmap_pool_grow(pool, size);
        if (!pool->cursor) return NULL;  // mmap failed
    }
    void* ptr = pool->cursor;
    pool->cursor += size;
    return ptr;
}

void* pool_calloc(Pool* pool, size_t size) {
    if (!pool || pool->valid != POOL_VALID_MARKER) {
        return NULL;
    }
    if (size > SIZE_LIMIT) {
        return NULL;
    }
    if (pool->heap) {
        // rpmalloc mode
        if ((uintptr_t)pool->heap < 0x10000) {
            log_error("pool_calloc: corrupted heap pointer %p in pool %u", (void*)pool->heap, pool->pool_id);
            return NULL;
        }
        return rpmalloc_heap_calloc(pool->heap, 1, size);
    }
    // mmap mode: bump allocate (pages are pre-zeroed by mmap)
    size = (size + 15) & ~15;
    if (!pool->cursor || pool->cursor + size > pool->limit) {
        mmap_pool_grow(pool, size);
        if (!pool->cursor) return NULL;  // mmap failed
    }
    void* ptr = pool->cursor;
    pool->cursor += size;
    return ptr;  // zeroed by MAP_ANONYMOUS
}

void pool_free(Pool* pool, void* ptr) {
    if (!pool || pool->valid != POOL_VALID_MARKER || !ptr) {
        return;
    }
    if (pool->heap) {
        rpmalloc_heap_free(pool->heap, ptr);
    }
    // mmap mode: no-op (bump allocator, freed in bulk on reset/destroy)
}

void* pool_realloc(Pool* pool, void* ptr, size_t size) {
    if (!pool || pool->valid != POOL_VALID_MARKER) {
        return NULL;
    }
    if (size > SIZE_LIMIT) {
        return NULL;
    }
    if (pool->heap) {
        // rpmalloc mode
        if ((uintptr_t)pool->heap < 0x10000) {
            log_error("pool_realloc: corrupted heap pointer %p in pool %u", (void*)pool->heap, pool->pool_id);
            return NULL;
        }
        if (!ptr) return rpmalloc_heap_alloc(pool->heap, size);
        if (size == 0) { rpmalloc_heap_free(pool->heap, ptr); return NULL; }
        return rpmalloc_heap_realloc(pool->heap, ptr, size, 0);
    }
    // mmap mode: allocate new + copy (can't free old in bump allocator)
    if (!ptr) return pool_alloc(pool, size);
    if (size == 0) return NULL;
    void* new_ptr = pool_alloc(pool, size);
    if (new_ptr && ptr) {
        // SAFETY: old_size is unknown in mmap mode. We cannot copy `size` bytes
        // from old ptr when growing — that would read past the old allocation.
        // Only safe approach: caller must re-populate the new buffer after growing.
        // We zero the new buffer (via pool_calloc-like behavior from MAP_ANONYMOUS)
        // but do NOT copy old data since we can't determine the safe copy length.
        log_debug("pool_realloc: mmap mode cannot safely copy data (old_size unknown)");
    }
    return new_ptr;
}

void mempool_cleanup(void) {
    if (rpmalloc_initialized) {
        pthread_mutex_lock(&init_mutex);
        if (rpmalloc_initialized) {
            rpmalloc_thread_finalize();
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
