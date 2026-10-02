#include "arena.h"
#define MEMTRACK_NO_LOCATION_MACROS
#include "memtrack.h"
#include "log.h"
#include "math_checked.hpp"
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

// Minimum of two values
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

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
    MemCategory category;       // memtrack category for owned blocks

    size_t high_water_active_bytes;
    uint64_t allocation_count;
    uint64_t rewind_count;
    uint64_t fresh_chunk_count;
    uint64_t fresh_growth_bytes;
    uint64_t reset_count;
    uint64_t clear_count;
    uint32_t active_scope_count;

    void* mem_node;             // MemContext registration node (NULL if untracked)
};

static inline void _arena_update_high_water(Arena* arena) {
    if (arena->total_used > arena->high_water_active_bytes) {
        arena->high_water_active_bytes = arena->total_used;
    }
}

/**
 * Allocate a new directly-owned chunk
 */
static ArenaChunk* _arena_alloc_chunk(Arena* arena, size_t capacity) {
    // Check if the requested capacity is too large
    // Account for chunk header overhead in the total size check
    size_t total_size = sizeof(ArenaChunk) + capacity;
    if (capacity > SIZE_LIMIT || total_size > SIZE_LIMIT) {
        return NULL;
    }

    // Allocate chunk header + data in one allocation
    ArenaChunk* chunk = (ArenaChunk*)mem_alloc_loc(total_size, arena->category, 0);
    if (!chunk) {
        return NULL;
    }

    chunk->next = NULL;
    chunk->capacity = capacity;
    chunk->used = 0;

    return chunk;
}

Arena* arena_create(size_t initial_chunk_size, size_t max_chunk_size) {
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

    Arena* arena = (Arena*)mem_alloc_loc(sizeof(Arena), MEM_CAT_SYSTEM, 0);
    if (!arena) {
        return NULL;
    }

    // Initialize arena
    arena->category = MEM_CAT_SYSTEM;
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
    arena->rewind_count = 0;
    arena->fresh_chunk_count = 1;
    arena->fresh_growth_bytes = initial_chunk_size;
    arena->reset_count = 0;
    arena->clear_count = 0;
    arena->active_scope_count = 0;

    // Allocate first chunk
    ArenaChunk* first_chunk = _arena_alloc_chunk(arena, initial_chunk_size);
    if (!first_chunk) {
        mem_free_loc(arena, 0);
        return NULL;
    }

    arena->first = first_chunk;
    arena->current = first_chunk;
    arena->total_allocated = initial_chunk_size;
    arena->chunk_count = 1;

    return arena;
}

Arena* arena_create_default(void) {
    return arena_create(ARENA_INITIAL_CHUNK_SIZE, ARENA_MAX_CHUNK_SIZE);
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
        mem_free_loc(chunk, 0);
        chunk = next;
    }

    // Mark arena as invalid and free it
    arena->valid = 0;
    mem_free_loc(arena, 0);
}

// Bump allocation at the arena tail; arenas never reuse interior blocks.
static void* _arena_alloc_tail(Arena* arena, size_t size, size_t alignment) {
    if (size == 0 || size > SIZE_LIMIT) {
        return NULL;
    }

    // Chunks are aligned to their flexible data member's 256-byte boundary.
    if (alignment > alignof(ArenaChunk)) return NULL;

    // Calculate aligned size for proper accounting
    size_t aligned_size = 0;
    if (!math_size_align_up(size, alignment, &aligned_size)) return NULL;

    ArenaChunk* chunk = arena->current;

    // Resettable arenas retain their grown chunk chain. Walk that chain before
    // appending: restarting at the first chunk must not orphan its successors.
    for (;;) {
        // Calculate aligned position within the chunk.
        size_t aligned_offset = 0;
        if (!math_size_align_up(chunk->used, alignment, &aligned_offset)) return NULL;

        if (aligned_offset <= chunk->capacity &&
            aligned_size <= chunk->capacity - aligned_offset) {
            void* ptr = &chunk->data[aligned_offset];
            chunk->used = aligned_offset + aligned_size;
            arena->current = chunk;
            arena->total_used += aligned_size;
            arena->allocation_count++;
            _arena_update_high_water(arena);
            return ptr;
        }

        if (!chunk->next) break;
        chunk = chunk->next;
    }

    // Need a new chunk - grow adaptively
    size_t next_chunk_size = MIN(arena->chunk_size * 2, arena->max_chunk_size);
    arena->chunk_size = next_chunk_size;

    // Allocate chunk large enough for the request
    // Add extra space for alignment padding
    size_t chunk_capacity = MAX(next_chunk_size, aligned_size + alignment);
    ArenaChunk* new_chunk = _arena_alloc_chunk(arena, chunk_capacity);
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

void* arena_alloc_aligned(Arena* arena, size_t size, size_t alignment) {
    if (!arena || arena->valid != ARENA_VALID_MARKER) {
        return NULL;
    }
    // A scratch scope owns the tail of its arena and rewinds it; a direct
    // allocation here would sit inside that tail and be rewound with it.
    if (arena->active_scope_count != 0) {
        log_error("arena_alloc: arena %p is owned by a scratch scope", (void*)arena);
        assert(arena->active_scope_count == 0);
        return NULL;
    }
    return _arena_alloc_tail(arena, size, alignment);
}

void* arena_scope_alloc(Arena* arena, size_t size, size_t alignment) {
    if (!arena || arena->valid != ARENA_VALID_MARKER) {
        return NULL;
    }
    return _arena_alloc_tail(arena, size, alignment);
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

char* arena_dup_n(Arena* arena, const char* data, size_t len) {
    if (!arena || !data || len == SIZE_MAX) return NULL;
    char* dup = (char*)arena_alloc(arena, len + 1);
    if (dup) {
        memcpy(dup, data, len);
        dup[len] = '\0';
    }
    return dup;
}

char* arena_strdup(Arena* arena, const char* str) {
    return str ? arena_dup_n(arena, str, strlen(str)) : NULL;
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

    return arena_dup_n(arena, str, len);
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
        mem_free_loc(chunk, 0);
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

    size_t backing = sizeof(Arena);
    for (ArenaChunk* chunk = arena->first; chunk; chunk = chunk->next) {
        backing += sizeof(ArenaChunk) + chunk->capacity;
    }
    out->backing_bytes = backing;
    out->committed_bytes = arena->total_allocated;
    out->bump_used_bytes = arena->total_used;
    out->active_bytes = arena->total_used;
    out->waste_bytes = arena->total_allocated - arena->total_used;
    out->overhead_bytes = backing > arena->total_allocated
        ? backing - arena->total_allocated : 0;
    out->high_water_active_bytes = arena->high_water_active_bytes;
    out->allocation_count = arena->allocation_count;
    out->rewind_count = arena->rewind_count;
    out->fresh_chunk_count = arena->fresh_chunk_count;
    out->fresh_growth_bytes = arena->fresh_growth_bytes;
    out->reset_count = arena->reset_count;
    out->clear_count = arena->clear_count;
    out->active_scope_count = arena->active_scope_count;
}

void arena_scope_enter(Arena* arena) {
    if (!arena || arena->valid != ARENA_VALID_MARKER) return;
    // one scratch owner per arena: two would interleave blocks, and neither
    // could rewind its own tail without cutting into the other's
    if (arena->active_scope_count != 0) {
        log_error("arena_scope_enter: arena %p already has a scratch owner", (void*)arena);
        assert(arena->active_scope_count == 0);
    }
    arena->active_scope_count++;
}

ArenaMark arena_mark(Arena* arena) {
    ArenaMark mark = {0};
    if (!arena || arena->valid != ARENA_VALID_MARKER) return mark;
    mark.chunk = arena->current;
    mark.used = arena->current ? arena->current->used : 0;
    mark.total_used = arena->total_used;
    return mark;
}

void arena_rewind(Arena* arena, ArenaMark mark) {
    if (!arena || arena->valid != ARENA_VALID_MARKER || !mark.chunk) return;
    ArenaChunk* target = (ArenaChunk*)mark.chunk;
    // the mark must lie at or behind the tail, on this arena's chain
    ArenaChunk* chunk = target;
    while (chunk && chunk != arena->current) chunk = chunk->next;
    if (!chunk || (target == arena->current && mark.used > target->used)) {
        log_error("arena_rewind: mark is not behind the tail of arena %p", (void*)arena);
        assert(false);
        return;
    }
    // chunks past the mark keep their capacity for reuse
    for (chunk = target->next; chunk; chunk = chunk->next) {
        chunk->used = 0;
        if (chunk == arena->current) break;
    }
    target->used = mark.used;
    arena->current = target;
    arena->total_used = mark.total_used;
    arena->rewind_count++;
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

    // The chunk being allocated from owns the most recent buffers -- a list
    // that is still growing lives there -- so test it before walking the list
    // from the oldest chunk. list growth asks on every step, and the walk made
    // large documents quadratic (58% of Markdown parse time).
    ArenaChunk* current = arena->current;
    if (current) {
        uintptr_t current_start = (uintptr_t)&current->data[0];
        uintptr_t ptr_addr = (uintptr_t)ptr;
        if (ptr_addr >= current_start && ptr_addr < current_start + current->used) {
            return true;
        }
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

void* arena_get_mem_node(Arena* arena) {
    return (arena && arena->valid == ARENA_VALID_MARKER) ? arena->mem_node : NULL;
}

void arena_set_mem_node(Arena* arena, void* node) {
    if (arena && arena->valid == ARENA_VALID_MARKER) arena->mem_node = node;
}

void arena_set_mem_category(Arena* arena, int category) {
    if (!arena || arena->valid != ARENA_VALID_MARKER) return;
    arena->category = category >= 0 && category < MEM_CAT_COUNT
        ? (MemCategory)category : MEM_CAT_UNKNOWN;
}
