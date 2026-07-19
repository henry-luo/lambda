#include "arena.h"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>

#define ARENA_VALID_MARKER 0xABCD4321
#define SIZE_LIMIT (1024 * 1024 * 1024)  // 1GB limit for single allocation

// Hook to release a memory-context node when a registered arena is destroyed.
// Installed by the allocator factory (mem_factory.c); NULL when unused.
static void (*g_arena_node_release)(void*) = NULL;
void arena_set_node_release_hook(void (*fn)(void*)) { g_arena_node_release = fn; }

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

    size_t high_water_active_bytes;
    uint64_t allocation_count;
    uint64_t free_count;
    uint64_t reuse_hits;
    uint64_t reuse_misses;
    uint64_t split_count;
    uint64_t coalesce_count;
    uint64_t bump_back_count;
    uint64_t fresh_chunk_count;
    uint64_t fresh_growth_bytes;
    uint64_t reset_count;
    uint64_t clear_count;
    uint32_t active_scope_count;

    void* mem_node;             // MemContext registration node (NULL if untracked)
};

static inline size_t _arena_active_bytes(const Arena* arena) {
    return arena->total_used >= arena->free_bytes
        ? arena->total_used - arena->free_bytes : 0;
}

static inline void _arena_update_high_water(Arena* arena) {
    size_t active = _arena_active_bytes(arena);
    if (active > arena->high_water_active_bytes) {
        arena->high_water_active_bytes = active;
    }
}

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

static inline size_t _arena_allocation_span(const Arena* arena, size_t size) {
    size_t span = ALIGN_UP(size, arena->alignment);
    return span < ARENA_MIN_FREE_BLOCK_SIZE ? ARENA_MIN_FREE_BLOCK_SIZE : span;
}

// Helper: find and remove a free block adjacent to [addr, addr+size)
// Scans all bins for a block whose end touches addr (left neighbor)
// or whose start is at addr+size (right neighbor).
// Returns the removed block, or NULL if no adjacent block found.
static ArenaFreeBlock* _arena_find_adjacent_block(Arena* arena, uintptr_t addr, size_t size) {
    uintptr_t block_end = addr + size;

    for (int i = 0; i < ARENA_FREE_LIST_BINS; i++) {
        ArenaFreeBlock** prev_ptr = &arena->free_lists[i];
        ArenaFreeBlock* block = arena->free_lists[i];

        while (block) {
            uintptr_t fb_start = (uintptr_t)block;
            uintptr_t fb_end = fb_start + block->size;

            if (fb_end == addr || fb_start == block_end) {
                // adjacent — remove from bin
                *prev_ptr = block->next;
                arena->free_bytes -= block->size;
                return block;
            }
            prev_ptr = &block->next;
            block = block->next;
        }
    }
    return NULL;
}

// Forward declarations
static void* _arena_alloc_from_freelist(Arena* arena, size_t size, size_t alignment);

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
    arena->mem_node = NULL;
    arena->high_water_active_bytes = 0;
    arena->allocation_count = 0;
    arena->free_count = 0;
    arena->reuse_hits = 0;
    arena->reuse_misses = 0;
    arena->split_count = 0;
    arena->coalesce_count = 0;
    arena->bump_back_count = 0;
    arena->fresh_chunk_count = 1;
    arena->fresh_growth_bytes = initial_chunk_size;
    arena->reset_count = 0;
    arena->clear_count = 0;
    arena->active_scope_count = 0;

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

    // unlink from the memory context if registered (factory-created arenas)
    if (arena->mem_node && g_arena_node_release) {
        g_arena_node_release(arena->mem_node);
        arena->mem_node = NULL;
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
    // Ensure minimum size so all blocks can participate in free-list (A1 fix)
    size_t aligned_size = ALIGN_UP(size, alignment);
    if (aligned_size < ARENA_MIN_FREE_BLOCK_SIZE) aligned_size = ARENA_MIN_FREE_BLOCK_SIZE;

    // Try to allocate from free-list first (alignment-aware, A3 fix)
    void* free_ptr = _arena_alloc_from_freelist(arena, aligned_size, alignment);
    if (free_ptr) {
        arena->allocation_count++;
        arena->reuse_hits++;
        _arena_update_high_water(arena);
        return free_ptr;
    }
    arena->reuse_misses++;

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
        arena->allocation_count++;
        _arena_update_high_water(arena);
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
    arena->fresh_chunk_count++;
    arena->fresh_growth_bytes += chunk_capacity;

    // Allocate from new chunk - data array is already aligned to 256 bytes
    // so any alignment <= 256 will work from position 0
    void* ptr = &new_chunk->data[0];
    new_chunk->used = aligned_size;
    arena->total_used += aligned_size;
    arena->allocation_count++;
    _arena_update_high_water(arena);

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

    // Active scratch scopes retain pointers into these chunks; resetting here
    // would silently turn them into aliases of subsequent allocations.
    if (arena->active_scope_count != 0) {
        log_error("arena_reset: %u active scope(s) still reference arena %p",
                  arena->active_scope_count, (void*)arena);
        assert(arena->active_scope_count == 0);
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

    // Clear free-lists (pointers into reset chunks are now stale)
    for (int i = 0; i < ARENA_FREE_LIST_BINS; i++) {
        arena->free_lists[i] = NULL;
    }
    arena->free_bytes = 0;
    arena->reset_count++;

    // Note: chunk_size is NOT reset - keeps grown size for efficiency
}

void arena_clear(Arena* arena) {
    if (!arena || arena->valid != ARENA_VALID_MARKER) {
        return;
    }

    // Clearing an arena with active scratch scopes invalidates their headers.
    if (arena->active_scope_count != 0) {
        log_error("arena_clear: %u active scope(s) still reference arena %p",
                  arena->active_scope_count, (void*)arena);
        assert(arena->active_scope_count == 0);
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

    // Clear free-lists (pointers into freed chunks are now stale)
    for (int i = 0; i < ARENA_FREE_LIST_BINS; i++) {
        arena->free_lists[i] = NULL;
    }
    arena->free_bytes = 0;
    arena->clear_count++;

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

void arena_get_stats(Arena* arena, ArenaStats* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!arena || arena->valid != ARENA_VALID_MARKER) return;

    size_t backing = pool_allocation_size(arena->pool, arena);
    for (ArenaChunk* chunk = arena->first; chunk; chunk = chunk->next) {
        backing += pool_allocation_size(arena->pool, chunk);
    }
    out->backing_bytes = backing;
    out->committed_bytes = arena->total_allocated;
    out->bump_used_bytes = arena->total_used;
    out->active_bytes = _arena_active_bytes(arena);
    out->recyclable_bytes = arena->free_bytes;
    out->waste_bytes = arena->total_allocated - arena->total_used;
    out->overhead_bytes = backing > arena->total_allocated
        ? backing - arena->total_allocated : 0;
    out->high_water_active_bytes = arena->high_water_active_bytes;
    out->allocation_count = arena->allocation_count;
    out->free_count = arena->free_count;
    out->reuse_hits = arena->reuse_hits;
    out->reuse_misses = arena->reuse_misses;
    out->split_count = arena->split_count;
    out->coalesce_count = arena->coalesce_count;
    out->bump_back_count = arena->bump_back_count;
    out->fresh_chunk_count = arena->fresh_chunk_count;
    out->fresh_growth_bytes = arena->fresh_growth_bytes;
    out->reset_count = arena->reset_count;
    out->clear_count = arena->clear_count;
    out->active_scope_count = arena->active_scope_count;
}

void arena_scope_enter(Arena* arena) {
    if (!arena || arena->valid != ARENA_VALID_MARKER) return;
    arena->active_scope_count++;
}

void arena_scope_leave(Arena* arena) {
    if (!arena || arena->valid != ARENA_VALID_MARKER) return;
    if (arena->active_scope_count == 0) {
        log_error("arena_scope_leave: scope underflow for arena %p", (void*)arena);
        assert(arena->active_scope_count > 0);
        return;
    }
    arena->active_scope_count--;
}

uint32_t arena_active_scope_count(Arena* arena) {
    if (!arena || arena->valid != ARENA_VALID_MARKER) return 0;
    return arena->active_scope_count;
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

Pool* arena_pool(Arena* arena) {
    if (!arena || arena->valid != ARENA_VALID_MARKER) return NULL;
    return arena->pool;
}

void* arena_get_mem_node(Arena* arena) {
    return (arena && arena->valid == ARENA_VALID_MARKER) ? arena->mem_node : NULL;
}

void arena_set_mem_node(Arena* arena, void* node) {
    if (arena && arena->valid == ARENA_VALID_MARKER) arena->mem_node = node;
}

void arena_free(Arena* arena, void* ptr, size_t size) {
    if (!arena || arena->valid != ARENA_VALID_MARKER || !ptr) {
        return;
    }

    // validate that ptr belongs to this arena before adding to free-list
    if (!arena_owns(arena, ptr)) {
        log_error("arena_free: ptr %p (size %zu) not owned by arena %p", ptr, size, (void*)arena);
        return;
    }
    arena->free_count++;

    // Callers retain requested object sizes, while the bump cursor advances by
    // the aligned allocation span. Freeing the raw size stranded tail padding
    // and made variable-sized DOM text churn grow linearly.
    size = _arena_allocation_span(arena, size);

    // Bump-back coalescing: if block is at the end of current chunk,
    // reclaim space directly instead of adding to free-list (A4 fix)
    ArenaChunk* chunk = arena->current;
    uintptr_t data_start = (uintptr_t)&chunk->data[0];
    uintptr_t block_end = (uintptr_t)ptr + size;
    uintptr_t chunk_cursor = data_start + chunk->used;
    if (block_end == chunk_cursor) {
        chunk->used -= size;
        arena->total_used -= size;
        arena->bump_back_count++;
        return;
    }

    // Determine bin index based on size
    int bin = _arena_get_bin(size);

    // Coalesce with adjacent free blocks
    uintptr_t merged_addr = (uintptr_t)ptr;
    size_t merged_size = size;

    // Repeatedly scan for adjacent free blocks and merge them
    ArenaFreeBlock* adj;
    while ((adj = _arena_find_adjacent_block(arena, merged_addr, merged_size)) != NULL) {
        arena->coalesce_count++;
        uintptr_t adj_addr = (uintptr_t)adj;
        if (adj_addr + adj->size == merged_addr) {
            // left neighbor: adj is before our block
            merged_addr = adj_addr;
            merged_size += adj->size;
        } else {
            // right neighbor: adj is after our block
            merged_size += adj->size;
        }
    }

    // After coalescing, check if merged block reaches the bump cursor (bump-back)
    uintptr_t merged_end = merged_addr + merged_size;
    chunk_cursor = data_start + chunk->used;
    if (merged_end == chunk_cursor) {
        chunk->used -= merged_size;
        arena->total_used -= merged_size;
        arena->bump_back_count++;
        return;
    }

    // Add merged block to free-list in appropriate bin
    bin = _arena_get_bin(merged_size);
    ArenaFreeBlock* block = (ArenaFreeBlock*)merged_addr;
    block->size = merged_size;
    block->next = arena->free_lists[bin];
    arena->free_lists[bin] = block;
    arena->free_bytes += merged_size;
}

// Try to allocate from free-list (alignment-aware, A3 fix)
static void* _arena_alloc_from_freelist(Arena* arena, size_t size, size_t alignment) {
    int bin = _arena_get_bin(size);

    // Search current bin and larger bins for suitable block
    for (int i = bin; i < ARENA_FREE_LIST_BINS; i++) {
        ArenaFreeBlock** prev_ptr = &arena->free_lists[i];
        ArenaFreeBlock* block = arena->free_lists[i];

        while (block) {
            // Check both size and alignment before selecting (A3 fix)
            if (block->size >= size && ((uintptr_t)block & (alignment - 1)) == 0) {
                // Found suitable block - remove from free-list
                *prev_ptr = block->next;
                arena->free_bytes -= block->size;

                // If block is significantly larger, split it
                size_t excess = block->size - size;
                if (excess >= ARENA_MIN_FREE_BLOCK_SIZE) {
                    arena->split_count++;
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

    size_t old_span = _arena_allocation_span(arena, old_size);
    size_t new_span = _arena_allocation_span(arena, new_size);

    // Same allocator span -> no-op
    if (new_span == old_span) {
        return ptr;
    }

    // Shrinking -> add excess to free-list
    if (new_span < old_span) {
        size_t excess = old_span - new_span;
        if (excess >= ARENA_MIN_FREE_BLOCK_SIZE) {
            void* excess_ptr = (char*)ptr + new_span;
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
    if (ptr_addr + old_span == chunk_end) {
        size_t aligned_growth = new_span - old_span;

        if (chunk->used + aligned_growth <= chunk->capacity) {
            chunk->used += aligned_growth;
            arena->total_used += aligned_growth;
            return ptr;
        }
    }

    // Otherwise, allocate new, copy, free old
    void* new_ptr = arena_alloc(arena, new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, MIN(old_size, new_size));
        arena_free(arena, ptr, old_size);
    }
    return new_ptr;
}
