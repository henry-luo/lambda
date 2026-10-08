#include "arena.h"
#define MEMTRACK_NO_LOCATION_MACROS
#include "memtrack.h"
#include "log.h"
#include "math_checked.hpp"
#include "binsearch.h"
#include "grow_capacity.h"
#include <limits.h>
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

// Retired-list configuration (arena_retire)
#define ARENA_RETIRED_BINS 8
#define ARENA_MIN_RETIRED_BLOCK_SIZE sizeof(ArenaRetiredBlock)

/**
 * Header written into a block retained on the retired list. The block stays
 * arena memory: it is only handed back out by a later allocation.
 */
typedef struct ArenaRetiredBlock {
    size_t size;                       // span of this retained block
    struct ArenaRetiredBlock* next;    // next block in the same bin
} ArenaRetiredBlock;

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
    ArenaChunk** chunk_index;   // chunks sorted by address, allocated after the first
    size_t chunk_index_capacity;
    size_t chunk_size;          // current chunk size (grows adaptively)
    size_t max_chunk_size;      // maximum chunk size limit
    size_t initial_chunk_size;  // initial chunk size for reset
    size_t total_allocated;     // total bytes allocated across all chunks
    size_t total_used;          // total bytes actually used
    unsigned alignment;         // default alignment
    unsigned chunk_count;       // number of chunks allocated
    unsigned valid;             // validity marker
    MemCategory category;       // memtrack category for owned blocks

    // Blocks retained for reuse (arena_retire), binned by span
    ArenaRetiredBlock* retired_lists[ARENA_RETIRED_BINS];
    size_t retired_bytes;

    size_t high_water_active_bytes;
    uint64_t allocation_count;
    uint64_t rewind_count;
    uint64_t retire_count;
    uint64_t reuse_hits;
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
    return arena->total_used >= arena->retired_bytes
        ? arena->total_used - arena->retired_bytes : 0;
}

static inline void _arena_update_high_water(Arena* arena) {
    size_t active = _arena_active_bytes(arena);
    if (active > arena->high_water_active_bytes) {
        arena->high_water_active_bytes = active;
    }
}

// log2-style bins: 16, 32, 64, 128, 256, 512, 1024, 2048+
static inline int _arena_retired_bin(size_t size) {
    if (size <= 16) return 0;
    if (size <= 32) return 1;
    if (size <= 64) return 2;
    if (size <= 128) return 3;
    if (size <= 256) return 4;
    if (size <= 512) return 5;
    if (size <= 1024) return 6;
    return 7;
}

// The span an allocation occupies; every span can hold a retired-block header.
static inline size_t _arena_allocation_span(const Arena* arena, size_t size, size_t alignment) {
    size_t span = 0;
    if (!math_size_align_up(size, alignment ? alignment : arena->alignment, &span)) return 0;
    return span < ARENA_MIN_RETIRED_BLOCK_SIZE ? ARENA_MIN_RETIRED_BLOCK_SIZE : span;
}

// Remove and return a retired block adjacent to [addr, addr+size), or NULL.
static ArenaRetiredBlock* _arena_take_adjacent_retired(Arena* arena, uintptr_t addr, size_t size) {
    uintptr_t block_end = addr + size;
    for (int i = 0; i < ARENA_RETIRED_BINS; i++) {
        ArenaRetiredBlock** prev_ptr = &arena->retired_lists[i];
        for (ArenaRetiredBlock* block = arena->retired_lists[i]; block; block = block->next) {
            uintptr_t start = (uintptr_t)block;
            if (start + block->size == addr || start == block_end) {
                *prev_ptr = block->next;
                arena->retired_bytes -= block->size;
                return block;
            }
            prev_ptr = &block->next;
        }
    }
    return NULL;
}

static void _arena_clear_retired(Arena* arena) {
    for (int i = 0; i < ARENA_RETIRED_BINS; i++) arena->retired_lists[i] = NULL;
    arena->retired_bytes = 0;
}

static void _arena_retain_span(Arena* arena, void* ptr, size_t span);

// Hand out a retained block that fits, splitting off and re-retaining the rest.
static void* _arena_reuse_retired(Arena* arena, size_t span, size_t alignment) {
    for (int i = _arena_retired_bin(span); i < ARENA_RETIRED_BINS; i++) {
        ArenaRetiredBlock** prev_ptr = &arena->retired_lists[i];
        for (ArenaRetiredBlock* block = arena->retired_lists[i]; block; block = block->next) {
            if (block->size >= span && ((uintptr_t)block & (alignment - 1)) == 0) {
                *prev_ptr = block->next;
                arena->retired_bytes -= block->size;
                size_t excess = block->size - span;
                if (excess >= ARENA_MIN_RETIRED_BLOCK_SIZE) {
                    arena->split_count++;
                    _arena_retain_span(arena, (char*)block + span, excess);
                }
                return (void*)block;
            }
            prev_ptr = &block->next;
        }
    }
    return NULL;
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

    // DOM construction revisits a completed Input's chunks in arbitrary order;
    // searching their allocation-order list for every node was quadratic.
    if (arena->chunk_count) {
        size_t count = (size_t)arena->chunk_count + 1;
        size_t index_capacity = 0, index_bytes = 0;
        if (count > INT_MAX ||
            !lib_grow_capacity(arena->chunk_index_capacity, count, 4, &index_capacity) ||
            !math_checked_mul(index_capacity, sizeof(ArenaChunk*), &index_bytes)) {
            mem_free_loc(chunk, 0);
            return NULL;
        }
        if (index_capacity != arena->chunk_index_capacity) {
            ArenaChunk** index = (ArenaChunk**)mem_alloc_loc(
                index_bytes, arena->category, 0);
            if (!index) {
                mem_free_loc(chunk, 0);
                return NULL;
            }
            if (arena->chunk_index) {
                memcpy(index, arena->chunk_index, arena->chunk_count * sizeof(ArenaChunk*));
                mem_free_loc(arena->chunk_index, 0);
            } else {
                index[0] = arena->first;
            }
            arena->chunk_index = index;
            arena->chunk_index_capacity = index_capacity;
        }
        size_t lo = 0, hi = arena->chunk_count;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if ((uintptr_t)arena->chunk_index[mid] < (uintptr_t)chunk) lo = mid + 1;
            else hi = mid;
        }
        memmove(arena->chunk_index + lo + 1, arena->chunk_index + lo,
            (arena->chunk_count - lo) * sizeof(ArenaChunk*));
        arena->chunk_index[lo] = chunk;
    }
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
    arena->chunk_index = NULL;
    arena->chunk_index_capacity = 0;
    arena->valid = ARENA_VALID_MARKER;
    arena->mem_node = NULL;
    arena->high_water_active_bytes = 0;
    arena->allocation_count = 0;
    arena->rewind_count = 0;
    arena->retire_count = 0;
    arena->reuse_hits = 0;
    arena->split_count = 0;
    arena->coalesce_count = 0;
    arena->bump_back_count = 0;
    _arena_clear_retired(arena);
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
    mem_free_loc(arena->chunk_index, 0);
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

    // Spans are at least a retired-block header so any block can be retained
    size_t aligned_size = _arena_allocation_span(arena, size, alignment);
    if (aligned_size == 0) return NULL;

    // An arena that has never retired a block skips the bin walk entirely.
    if (arena->retired_bytes) {
        void* reused = _arena_reuse_retired(arena, aligned_size, alignment);
        if (reused) {
            arena->allocation_count++;
            arena->reuse_hits++;
            _arena_update_high_water(arena);
            return reused;
        }
    }

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
    _arena_clear_retired(arena);  // retained blocks lived in the reset chunks
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
    mem_free_loc(arena->chunk_index, 0);
    arena->chunk_index = NULL;
    arena->chunk_index_capacity = 0;
    arena->first->next = NULL;
    arena->first->used = 0;
    arena->current = arena->first;

    // Reset statistics
    arena->total_allocated = arena->first->capacity;
    arena->total_used = 0;
    arena->chunk_count = 1;
    _arena_clear_retired(arena);  // retained blocks lived in the released chunks
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

    size_t backing = sizeof(Arena) + arena->chunk_index_capacity * sizeof(ArenaChunk*);
    for (ArenaChunk* chunk = arena->first; chunk; chunk = chunk->next) {
        backing += sizeof(ArenaChunk) + chunk->capacity;
    }
    out->backing_bytes = backing;
    out->committed_bytes = arena->total_allocated;
    out->bump_used_bytes = arena->total_used;
    out->active_bytes = _arena_active_bytes(arena);
    out->retired_bytes = arena->retired_bytes;
    out->waste_bytes = arena->total_allocated - arena->total_used;
    out->overhead_bytes = backing > arena->total_allocated
        ? backing - arena->total_allocated : 0;
    out->high_water_active_bytes = arena->high_water_active_bytes;
    out->allocation_count = arena->allocation_count;
    out->rewind_count = arena->rewind_count;
    out->retire_count = arena->retire_count;
    out->reuse_hits = arena->reuse_hits;
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
    // retained blocks could lie in the rewound tail; tail-rewound arenas
    // (scratch backings) never retire blocks
    if (arena->retired_bytes) {
        log_error("arena_rewind: arena %p holds retired blocks", (void*)arena);
        assert(arena->retired_bytes == 0);
        return;
    }
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

static int _arena_chunk_range_compare(const void* record, const void* key, void* unused) {
    (void)unused;
    ArenaChunk* chunk = *(ArenaChunk* const*)record;
    uintptr_t start = (uintptr_t)chunk->data;
    uintptr_t address = (uintptr_t)key;
    if (address < start) return 1;
    // Read the current bump extent so reset, rewind and tail retirement cannot
    // leave stale ownership proofs in the address index (D4.1.4v5).
    return address - start < chunk->used ? 0 : -1;
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

    return arena->chunk_index && binsearch_range(arena->chunk_index,
        (int)arena->chunk_count, sizeof(ArenaChunk*), ptr,
        _arena_chunk_range_compare, NULL) >= 0;
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

// Coalesce a span with retained neighbours; return it to the tail when it
// reaches the bump cursor, otherwise keep it on the retired list.
static void _arena_retain_span(Arena* arena, void* ptr, size_t span) {
    uintptr_t merged_addr = (uintptr_t)ptr;
    size_t merged_size = span;
    ArenaRetiredBlock* adj;
    while ((adj = _arena_take_adjacent_retired(arena, merged_addr, merged_size)) != NULL) {
        arena->coalesce_count++;
        if ((uintptr_t)adj + adj->size == merged_addr) merged_addr = (uintptr_t)adj;
        merged_size += adj->size;
    }

    ArenaChunk* chunk = arena->current;
    uintptr_t cursor = (uintptr_t)&chunk->data[0] + chunk->used;
    if (merged_addr + merged_size == cursor) {
        chunk->used -= merged_size;
        arena->total_used -= merged_size;
        arena->bump_back_count++;
        return;
    }

    ArenaRetiredBlock* block = (ArenaRetiredBlock*)merged_addr;
    block->size = merged_size;
    int bin = _arena_retired_bin(merged_size);
    block->next = arena->retired_lists[bin];
    arena->retired_lists[bin] = block;
    arena->retired_bytes += merged_size;
}

void arena_retire(Arena* arena, void* ptr, size_t size) {
    if (!arena || arena->valid != ARENA_VALID_MARKER || !ptr) return;
    // a scratch owner rewinds its tail; a retained block there would dangle
    if (arena->active_scope_count != 0) {
        log_error("arena_retire: arena %p is owned by a scratch scope", (void*)arena);
        assert(arena->active_scope_count == 0);
        return;
    }
    if (!arena_owns(arena, ptr)) {
        log_error("arena_retire: ptr %p (size %zu) not owned by arena %p", ptr, size, (void*)arena);
        return;
    }
    // callers pass the object size; the allocation occupied the aligned span
    size_t span = _arena_allocation_span(arena, size, 0);
    if (span == 0) return;
    arena->retire_count++;
    _arena_retain_span(arena, ptr, span);
}
