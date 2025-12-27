#include "arena.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

#define ARENA_VALID_MARKER 0xABCD4321
#define SIZE_LIMIT (1024 * 1024 * 1024)  // 1GB limit for single allocation

// Free-list configuration
#define ARENA_FREE_LIST_BINS 8
#define ARENA_MIN_FREE_BLOCK_SIZE sizeof(ArenaFreeBlock)

// Align up to the next multiple of alignment (must be power of 2)
#define ALIGN_UP(n, alignment) (((n) + (alignment) - 1) & ~((alignment) - 1))

// Minimum of two values
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/**
 * Free block header for arena free-list
 */
typedef struct ArenaFreeBlock {
    size_t size;                    // Size of this free block
    struct ArenaFreeBlock* next;    // Next block in same bin
} ArenaFreeBlock;

/**
 * Arena chunk - linked list node containing allocation space
 * Use alignas to ensure data array starts on a 256-byte boundary (max alignment we support)
 */
typedef struct ArenaChunk {
    struct ArenaChunk* next;    // next chunk in list
    size_t capacity;            // total size of data array
    size_t used;                // bytes used in this chunk
    alignas(256) unsigned char data[];       // flexible array member for allocations (256-byte aligned)
} ArenaChunk;

/**
 * Arena structure - manages chunks and allocation state
 */
struct Arena {
    Pool* pool;                 // underlying memory pool for chunks
    ArenaChunk* current;        // current chunk being allocated from
    ArenaChunk* first;          // first chunk in list
    size_t chunk_size;          // current chunk size (grows adaptively)
    size_t max_chunk_size;      // maximum chunk size limit
    size_t initial_chunk_size;  // initial chunk size for reset
    size_t total_allocated;     // total bytes allocated across all chunks
    size_t total_used;          // total bytes actually used
    unsigned alignment;         // default alignment
    unsigned chunk_count;       // number of chunks allocated
    unsigned valid;             // validity marker

    // Free-list for memory reuse
    ArenaFreeBlock* free_lists[ARENA_FREE_LIST_BINS];  // Free-list bins
    size_t free_bytes;          // Total bytes in free-lists
};

// Helper: get bin index for size (log2-based bins: 16, 32, 64, 128, 256, 512, 1024, 2048+)
static inline int _arena_get_bin(size_t size) {
    if (size <= 16) return 0;
    if (size <= 32) return 1;
    if (size <= 64) return 2;
    if (size <= 128) return 3;
    if (size <= 256) return 4;
    if (size <= 512) return 5;
    if (size <= 1024) return 6;
    return 7;  // 2048+ goes to last bin
}

// Forward declarations
static void* _arena_alloc_from_freelist(Arena* arena, size_t size);

/**
 * Allocate a new chunk from the pool
 */
static ArenaChunk* _arena_alloc_chunk(Pool* pool, size_t capacity) {
    // Check if the requested capacity is too large
    // Account for chunk header overhead in the total size check
    size_t total_size = sizeof(ArenaChunk) + capacity;
    if (capacity > SIZE_LIMIT || total_size > SIZE_LIMIT) {
        return NULL;
    }

    // Allocate chunk header + data in one allocation
    ArenaChunk* chunk = (ArenaChunk*)pool_alloc(pool, total_size);
    if (!chunk) {
        return NULL;
    }

    chunk->next = NULL;
    chunk->capacity = capacity;
    chunk->used = 0;

    return chunk;
}

Arena* arena_create(Pool* pool, size_t initial_chunk_size, size_t max_chunk_size) {
    if (!pool) {
        return NULL;
    }

    // Validate chunk sizes
    if (initial_chunk_size == 0) {
        initial_chunk_size = ARENA_INITIAL_CHUNK_SIZE;
    }
    if (max_chunk_size == 0) {
        max_chunk_size = ARENA_MAX_CHUNK_SIZE;
    }
    if (initial_chunk_size > max_chunk_size) {
        initial_chunk_size = max_chunk_size;
    }

    // Allocate arena structure
    Arena* arena = (Arena*)pool_alloc(pool, sizeof(Arena));
    if (!arena) {
        return NULL;
    }

    // Initialize arena
    arena->pool = pool;
    arena->chunk_size = initial_chunk_size;
    arena->max_chunk_size = max_chunk_size;
    arena->initial_chunk_size = initial_chunk_size;
    arena->alignment = ARENA_DEFAULT_ALIGNMENT;
    arena->total_allocated = 0;
    arena->total_used = 0;
    arena->chunk_count = 0;
    arena->valid = ARENA_VALID_MARKER;

    // Initialize free-lists
    for (int i = 0; i < ARENA_FREE_LIST_BINS; i++) {
        arena->free_lists[i] = NULL;
    }
    arena->free_bytes = 0;

    // Allocate first chunk
    ArenaChunk* first_chunk = _arena_alloc_chunk(pool, initial_chunk_size);
    if (!first_chunk) {
        pool_free(pool, arena);
        return NULL;
    }

    arena->first = first_chunk;
    arena->current = first_chunk;
    arena->total_allocated = initial_chunk_size;
    arena->chunk_count = 1;

    return arena;
}

Arena* arena_create_default(Pool* pool) {
    return arena_create(pool, ARENA_INITIAL_CHUNK_SIZE, ARENA_MAX_CHUNK_SIZE);
}

void arena_destroy(Arena* arena) {
    if (!arena || arena->valid != ARENA_VALID_MARKER) {
        return;
    }

    // Free all chunks
    ArenaChunk* chunk = arena->first;
    while (chunk) {
        ArenaChunk* next = chunk->next;
        pool_free(arena->pool, chunk);
        chunk = next;
    }

    // Mark arena as invalid and free it
    arena->valid = 0;
    pool_free(arena->pool, arena);
}

void* arena_alloc_aligned(Arena* arena, size_t size, size_t alignment) {
    if (!arena || arena->valid != ARENA_VALID_MARKER) {
        return NULL;
    }

    if (size == 0 || size > SIZE_LIMIT) {
        return NULL;
    }

    // Validate alignment (must be power of 2)
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return NULL;
    }

    // Calculate aligned size for proper accounting
    size_t aligned_size = ALIGN_UP(size, alignment);

    // Try to allocate from free-list first
    void* free_ptr = _arena_alloc_from_freelist(arena, aligned_size);
    if (free_ptr) {
        // Check if free block is properly aligned
        uintptr_t ptr_addr = (uintptr_t)free_ptr;
        if ((ptr_addr & (alignment - 1)) == 0) {
            // Already aligned, use it
            return free_ptr;
        }
        // Not aligned, put it back and fall through to chunk allocation
        arena_free(arena, free_ptr, aligned_size);
    }

    ArenaChunk* chunk = arena->current;

    // Calculate aligned position within the chunk
    // The chunk->data array is already aligned to 256 bytes (see ArenaChunk definition)
    uintptr_t data_start = (uintptr_t)&chunk->data[0];
    uintptr_t current_pos = data_start + chunk->used;
    uintptr_t aligned_pos = ALIGN_UP(current_pos, alignment);
    size_t aligned_offset = aligned_pos - data_start;

    // Check if current chunk has enough space
    if (aligned_offset + aligned_size <= chunk->capacity) {
        void* ptr = &chunk->data[aligned_offset];
        chunk->used = aligned_offset + aligned_size;
        arena->total_used += aligned_size;
        return ptr;
    }

    // Need a new chunk - grow adaptively
    size_t next_chunk_size = MIN(arena->chunk_size * 2, arena->max_chunk_size);
    arena->chunk_size = next_chunk_size;

    // Allocate chunk large enough for the request
    // Add extra space for alignment padding
    size_t chunk_capacity = MAX(next_chunk_size, aligned_size + alignment);
    ArenaChunk* new_chunk = _arena_alloc_chunk(arena->pool, chunk_capacity);
    if (!new_chunk) {
        return NULL;
    }

    // Link new chunk into list
    chunk->next = new_chunk;
    arena->current = new_chunk;
    arena->total_allocated += chunk_capacity;
    arena->chunk_count++;

    // Allocate from new chunk - data array is already aligned to 256 bytes
    // so any alignment <= 256 will work from position 0
    void* ptr = &new_chunk->data[0];
    new_chunk->used = aligned_size;
    arena->total_used += aligned_size;

    return ptr;
}

void* arena_alloc(Arena* arena, size_t size) {
    if (!arena || arena->valid != ARENA_VALID_MARKER) {
        return NULL;
    }

    return arena_alloc_aligned(arena, size, arena->alignment);
}

void* arena_calloc(Arena* arena, size_t size) {
    void* ptr = arena_alloc(arena, size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

char* arena_strdup(Arena* arena, const char* str) {
    if (!arena || !str) {
        return NULL;
    }

    size_t len = strlen(str) + 1;  // include null terminator
    char* dup = (char*)arena_alloc(arena, len);
    if (dup) {
        memcpy(dup, str, len);
    }
    return dup;
}

char* arena_strndup(Arena* arena, const char* str, size_t n) {
    if (!arena || !str) {
        return NULL;
    }

    // Find actual length (up to n)
    size_t len = 0;
    while (len < n && str[len] != '\0') {
        len++;
    }

    char* dup = (char*)arena_alloc(arena, len + 1);
    if (dup) {
        memcpy(dup, str, len);
        dup[len] = '\0';
    }
    return dup;
}

char* arena_sprintf(Arena* arena, const char* fmt, ...) {
    if (!arena || !fmt) {
        return NULL;
    }

    va_list args, args_copy;
    va_start(args, fmt);

    // First pass: determine required size
    va_copy(args_copy, args);
    int size = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);

    if (size < 0) {
        va_end(args);
        return NULL;
    }

    // Allocate space (size + 1 for null terminator)
    char* str = (char*)arena_alloc(arena, size + 1);
    if (!str) {
        va_end(args);
        return NULL;
    }

    // Second pass: format the string
    vsnprintf(str, size + 1, fmt, args);
    va_end(args);

    return str;
}

void arena_reset(Arena* arena) {
    if (!arena || arena->valid != ARENA_VALID_MARKER) {
        return;
    }

    // Reset all chunks to unused
    ArenaChunk* chunk = arena->first;
    while (chunk) {
        chunk->used = 0;
        chunk = chunk->next;
    }

    // Reset to first chunk
    arena->current = arena->first;
    arena->total_used = 0;

    // Note: chunk_size is NOT reset - keeps grown size for efficiency
}

void arena_clear(Arena* arena) {
    if (!arena || arena->valid != ARENA_VALID_MARKER) {
        return;
    }

    // Keep first chunk, free all others
    ArenaChunk* chunk = arena->first->next;
    while (chunk) {
        ArenaChunk* next = chunk->next;
        pool_free(arena->pool, chunk);
        chunk = next;
    }

    // Reset first chunk
    arena->first->next = NULL;
    arena->first->used = 0;
    arena->current = arena->first;

    // Reset statistics
    arena->total_allocated = arena->first->capacity;
    arena->total_used = 0;
    arena->chunk_count = 1;

    // Reset chunk size to initial
    arena->chunk_size = arena->initial_chunk_size;
}

size_t arena_total_allocated(Arena* arena) {
    if (!arena || arena->valid != ARENA_VALID_MARKER) {
        return 0;
    }
    return arena->total_allocated;
}

size_t arena_total_used(Arena* arena) {
    if (!arena || arena->valid != ARENA_VALID_MARKER) {
        return 0;
    }
    return arena->total_used;
}

size_t arena_waste(Arena* arena) {
    if (!arena || arena->valid != ARENA_VALID_MARKER) {
        return 0;
    }
    return arena->total_allocated - arena->total_used;
}

size_t arena_chunk_count(Arena* arena) {
    if (!arena || arena->valid != ARENA_VALID_MARKER) {
        return 0;
    }
    return arena->chunk_count;
}

bool arena_owns(Arena* arena, const void* ptr) {
    if (!arena || arena->valid != ARENA_VALID_MARKER || !ptr) {
        return false;
    }

    // Iterate through all chunks to find if ptr is within any chunk's data
    ArenaChunk* chunk = arena->first;
    while (chunk) {
        uintptr_t data_start = (uintptr_t)&chunk->data[0];
        uintptr_t data_end = data_start + chunk->used;
        uintptr_t ptr_addr = (uintptr_t)ptr;

        if (ptr_addr >= data_start && ptr_addr < data_end) {
            return true;
        }
        chunk = chunk->next;
    }

    return false;
}

void arena_free(Arena* arena, void* ptr, size_t size) {
    if (!arena || arena->valid != ARENA_VALID_MARKER || !ptr) {
        return;
    }

    // Must be large enough to hold a free block header
    if (size < ARENA_MIN_FREE_BLOCK_SIZE) {
        return;
    }

    // Determine bin index based on size
    int bin = _arena_get_bin(size);

    // Add to free-list
    ArenaFreeBlock* block = (ArenaFreeBlock*)ptr;
    block->size = size;
    block->next = arena->free_lists[bin];
    arena->free_lists[bin] = block;
    arena->free_bytes += size;
}

// Try to allocate from free-list
static void* _arena_alloc_from_freelist(Arena* arena, size_t size) {
    int bin = _arena_get_bin(size);

    // Search current bin and larger bins for suitable block
    for (int i = bin; i < ARENA_FREE_LIST_BINS; i++) {
        ArenaFreeBlock** prev_ptr = &arena->free_lists[i];
        ArenaFreeBlock* block = arena->free_lists[i];

        while (block) {
            if (block->size >= size) {
                // Found suitable block - remove from free-list
                *prev_ptr = block->next;
                arena->free_bytes -= block->size;

                // If block is significantly larger, split it
                size_t excess = block->size - size;
                if (excess >= ARENA_MIN_FREE_BLOCK_SIZE) {
                    void* excess_ptr = (char*)block + size;
                    arena_free(arena, excess_ptr, excess);
                }

                return (void*)block;
            }
            prev_ptr = &block->next;
            block = block->next;
        }
    }

    return NULL;  // No suitable block found
}

void* arena_realloc(Arena* arena, void* ptr, size_t old_size, size_t new_size) {
    if (!arena || arena->valid != ARENA_VALID_MARKER) {
        return NULL;
    }

    // NULL ptr -> allocate new
    if (!ptr) {
        return arena_alloc(arena, new_size);
    }

    // new_size == 0 -> free
    if (new_size == 0) {
        arena_free(arena, ptr, old_size);
        return NULL;
    }

    // Same size -> no-op
    if (new_size == old_size) {
        return ptr;
    }

    // Shrinking -> add excess to free-list
    if (new_size < old_size) {
        size_t excess = old_size - new_size;
        if (excess >= ARENA_MIN_FREE_BLOCK_SIZE) {
            void* excess_ptr = (char*)ptr + new_size;
            arena_free(arena, excess_ptr, excess);
        }
        return ptr;
    }

    // Growing -> check if at end of current chunk (can extend in-place)
    ArenaChunk* chunk = arena->current;
    uintptr_t ptr_addr = (uintptr_t)ptr;
    uintptr_t data_start = (uintptr_t)&chunk->data[0];
    uintptr_t chunk_end = data_start + chunk->used;

    // If at end of chunk and enough space remaining, extend in place
    if (ptr_addr + old_size == chunk_end) {
        size_t growth = new_size - old_size;
        size_t aligned_growth = ALIGN_UP(growth, arena->alignment);

        if (chunk->used + aligned_growth <= chunk->capacity) {
            chunk->used += aligned_growth;
            arena->total_used += aligned_growth;
            return ptr;
        }
    }

    // Otherwise, allocate new, copy, free old
    void* new_ptr = arena_alloc(arena, new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, old_size);
        arena_free(arena, ptr, old_size);
    }
    return new_ptr;
}
