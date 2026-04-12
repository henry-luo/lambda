#include "scratch_arena.h"
#include "log.h"
#include <string.h>

// alignment for scratch allocations (matches ARENA_DEFAULT_ALIGNMENT)
#define SCRATCH_ALIGNMENT 16

// compile-time check: ScratchHeader must be exactly 16 bytes for alignment
_Static_assert(sizeof(ScratchHeader) == 16, "ScratchHeader must be 16 bytes");

// ============================================================================
// Internal helpers
// ============================================================================

// get ScratchHeader from user pointer
static inline ScratchHeader* _scratch_header(void* ptr) {
    return (ScratchHeader*)((char*)ptr - SCRATCH_HEADER_SIZE);
}

// free a single block back to the backing arena (bump-back or free-list)
static inline void _scratch_arena_free_block(ScratchArena* sa, ScratchHeader* hdr) {
    size_t total_size = SCRATCH_HEADER_SIZE + (size_t)hdr->size;
    arena_free(sa->arena, hdr, total_size);
}

// ============================================================================
// Public API
// ============================================================================

void scratch_init(ScratchArena* sa, Arena* arena) {
    if (!sa || !arena) return;
    sa->arena = arena;
    sa->head = NULL;
}

void* scratch_alloc(ScratchArena* sa, size_t size) {
    if (!sa || !sa->arena || size == 0) return NULL;

    // cap at uint32_t max (header stores size as uint32_t)
    if (size > UINT32_MAX) {
        log_error("scratch_alloc: size %zu exceeds uint32 max", size);
        return NULL;
    }

    // round user size up to alignment so the next header is aligned
    size_t aligned_size = (size + (SCRATCH_ALIGNMENT - 1)) & ~(SCRATCH_ALIGNMENT - 1);
    size_t total_size = SCRATCH_HEADER_SIZE + aligned_size;

    // allocate from backing arena (header + payload in one block)
    void* raw = arena_alloc_aligned(sa->arena, total_size, SCRATCH_ALIGNMENT);
    if (!raw) {
        log_error("scratch_alloc: backing arena failed for %zu bytes", total_size);
        return NULL;
    }

    // fill header
    ScratchHeader* hdr = (ScratchHeader*)raw;
    hdr->prev = sa->head;
    hdr->size = (uint32_t)aligned_size;
    hdr->flags = 0;

    // push onto stack
    sa->head = hdr;

    // return pointer past header
    return (char*)raw + SCRATCH_HEADER_SIZE;
}

void* scratch_calloc(ScratchArena* sa, size_t size) {
    void* ptr = scratch_alloc(sa, size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void scratch_free(ScratchArena* sa, void* ptr) {
    if (!sa || !ptr) return;

    ScratchHeader* hdr = _scratch_header(ptr);

    // LIFO fast path: freeing the most recent allocation
    if (hdr == sa->head) {
        // pop from stack
        sa->head = hdr->prev;

        // reclaim via backing arena (triggers bump-back if at chunk tail)
        _scratch_arena_free_block(sa, hdr);

        // backward walk: reclaim consecutive holes behind
        while (sa->head && (sa->head->flags & SCRATCH_FLAG_FREED)) {
            ScratchHeader* hole = sa->head;
            sa->head = hole->prev;
            _scratch_arena_free_block(sa, hole);
        }
        return;
    }

    // non-LIFO: mark as hole, reclaimed later by backward walk
    hdr->flags |= SCRATCH_FLAG_FREED;
}

ScratchMark scratch_mark(ScratchArena* sa) {
    ScratchMark mark = {0};
    if (sa) {
        mark.head = sa->head;
    }
    return mark;
}

void scratch_restore(ScratchArena* sa, ScratchMark mark) {
    if (!sa) return;

    // unwind all allocations until we reach the saved head
    while (sa->head != mark.head) {
        ScratchHeader* hdr = sa->head;
        if (!hdr) break;  // safety: ran past beginning
        sa->head = hdr->prev;
        _scratch_arena_free_block(sa, hdr);
    }
}

void scratch_release(ScratchArena* sa) {
    if (!sa) return;

    // unwind everything
    while (sa->head) {
        ScratchHeader* hdr = sa->head;
        sa->head = hdr->prev;
        _scratch_arena_free_block(sa, hdr);
    }
}

size_t scratch_live_count(ScratchArena* sa) {
    if (!sa) return 0;

    size_t count = 0;
    ScratchHeader* hdr = sa->head;
    while (hdr) {
        if (!(hdr->flags & SCRATCH_FLAG_FREED)) {
            count++;
        }
        hdr = hdr->prev;
    }
    return count;
}
