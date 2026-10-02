#include "scratch_arena.h"
#include "log.h"
#include "math_checked.hpp"
#include <string.h>
#include <assert.h>

// alignment for scratch allocations (matches ARENA_DEFAULT_ALIGNMENT)
#define SCRATCH_ALIGNMENT 16

// Hook to release a memory-context node when a registered scratch arena is
// released. Installed by the allocator factory (mem_factory.c); NULL when the
// context isn't linked.
static void (*g_scratch_node_release)(void*) = NULL;
void scratch_set_node_release_hook(void (*fn)(void*)) { g_scratch_node_release = fn; }

// compile-time check: ScratchHeader must be exactly 16 bytes for alignment
_Static_assert(sizeof(ScratchHeader) == 16, "ScratchHeader must be 16 bytes");

// ============================================================================
// Internal helpers
// ============================================================================

// The backing arena is rewound once per unwind; blocks are only poisoned here.
static inline void _scratch_retire_block(ScratchHeader* hdr) {
#ifndef NDEBUG
    // poison released payloads so a use after scope end reads garbage, not
    // the stale value, and shows up in layout/render output during testing
    memset((char*)hdr + SCRATCH_HEADER_SIZE, 0xDB, hdr->size);
#else
    (void)hdr;
#endif
}

// ============================================================================
// Public API
// ============================================================================

void scratch_init(ScratchArena* sa, Arena* arena) {
    if (!sa || !arena) return;
    sa->arena = arena;
    sa->head = NULL;
    sa->mem_node = NULL;
    sa->scope_active = true;
    sa->open_scope = 0;
    sa->last_scope = 0;
    arena_scope_enter(arena);
    sa->base = arena_mark(arena);
}

static void* _scratch_alloc_tagged(ScratchArena* sa, size_t size, uint32_t scope) {
    if (!sa || !sa->arena || size == 0) return NULL;

    // cap at uint32_t max (header stores size as uint32_t)
    if (size > UINT32_MAX) {
        log_error("scratch_alloc: size %zu exceeds uint32 max", size);
        return NULL;
    }

    // round user size up to alignment so the next header is aligned
    size_t aligned_size = 0;
    size_t total_size = 0;
    if (!math_size_align_up(size, SCRATCH_ALIGNMENT, &aligned_size) ||
        aligned_size > UINT32_MAX ||
        !math_checked_add(SCRATCH_HEADER_SIZE, aligned_size, &total_size)) return NULL;

    // allocate from backing arena (header + payload in one block)
    void* raw = arena_scope_alloc(sa->arena, total_size, SCRATCH_ALIGNMENT);
    if (!raw) {
        log_error("scratch_alloc: backing arena failed for %zu bytes", total_size);
        return NULL;
    }

    // fill header
    ScratchHeader* hdr = (ScratchHeader*)raw;
    hdr->prev = sa->head;
    hdr->size = (uint32_t)aligned_size;
    hdr->scope = scope;

    // push onto stack
    sa->head = hdr;

    // return pointer past header
    return (char*)raw + SCRATCH_HEADER_SIZE;
}

void* scratch_alloc(ScratchArena* sa, size_t size) {
    return _scratch_alloc_tagged(sa, size, 0);
}

void* scratch_calloc(ScratchArena* sa, size_t size) {
    void* ptr = scratch_alloc(sa, size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

ScratchMark scratch_mark(ScratchArena* sa) {
    ScratchMark mark = {0};
    if (sa) {
        mark.head = sa->head;
        mark.tail = arena_mark(sa->arena);
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
        _scratch_retire_block(hdr);
    }
    arena_rewind(sa->arena, mark.tail);
}

ScratchMark scratch_scope_begin(ScratchArena* sa) {
    ScratchMark mark = {0};
    if (!sa) return mark;
    uint32_t serial = sa->last_scope + 1;
    if (serial == 0) serial = 1;  // on wrap; 0 stays reserved for plain blocks
    sa->last_scope = serial;
    mark.head = sa->head;
    mark.tail = arena_mark(sa->arena);
    mark.scope = serial;
    mark.outer = sa->open_scope;
    sa->open_scope = serial;
    return mark;
}

void* scratch_scope_alloc(ScratchArena* sa, ScratchMark* scope, size_t size) {
    if (!sa || !scope || !scope->scope) return NULL;
    if (sa->open_scope != scope->scope) {
        // allocating through an outer scope while an inner one is open would
        // put the outer scope's block above the inner mark
        log_error("scratch_scope_alloc: scope %u is not the innermost (open %u)",
                  scope->scope, sa->open_scope);
        assert(sa->open_scope == scope->scope);
        return NULL;
    }
    return _scratch_alloc_tagged(sa, size, scope->scope);
}

void* scratch_scope_calloc(ScratchArena* sa, ScratchMark* scope, size_t size) {
    void* ptr = scratch_scope_alloc(sa, scope, size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void scratch_scope_end(ScratchArena* sa, ScratchMark* scope) {
    if (!sa || !scope || !scope->scope) return;
    if (sa->open_scope != scope->scope) {
        log_error("scratch_scope_end: scope %u ended out of order (open %u)",
                  scope->scope, sa->open_scope);
        assert(sa->open_scope == scope->scope);
    }

    size_t foreign = 0;
    size_t foreign_bytes = 0;
    while (sa->head != scope->head) {
        ScratchHeader* hdr = sa->head;
        if (!hdr) {
            log_error("scratch_scope_end: scope %u mark is no longer on the stack", scope->scope);
            assert(hdr);
            break;
        }
        if (hdr->scope != scope->scope) {
            foreign++;
            foreign_bytes += hdr->size;
        }
        sa->head = hdr->prev;
        _scratch_retire_block(hdr);
    }
    arena_rewind(sa->arena, scope->tail);
    if (foreign) {
        // a block the scope did not allocate is still live: whoever allocated
        // it may still hold it, so unwinding it here can leave a dangling pointer
        log_error("scratch_scope_end: scope %u unwound %zu foreign block(s), %zu bytes",
                  scope->scope, foreign, foreign_bytes);
        assert(foreign == 0);
    }
    sa->open_scope = scope->outer;
    *scope = (ScratchMark){0};
}

void scratch_release(ScratchArena* sa) {
    if (!sa) return;

    // unwind everything
    while (sa->head) {
        ScratchHeader* hdr = sa->head;
        sa->head = hdr->prev;
        _scratch_retire_block(hdr);
    }
    if (sa->scope_active) arena_rewind(sa->arena, sa->base);
    sa->open_scope = 0;

    // unlink from the memory context if registered (factory-created scratch)
    if (sa->mem_node && g_scratch_node_release) {
        g_scratch_node_release(sa->mem_node);
        sa->mem_node = NULL;
    }
    if (sa->scope_active) {
        arena_scope_leave(sa->arena);
        sa->scope_active = false;
    }
}

size_t scratch_live_count(ScratchArena* sa) {
    if (!sa) return 0;

    size_t count = 0;
    ScratchHeader* hdr = sa->head;
    while (hdr) {
        count++;
        hdr = hdr->prev;
    }
    return count;
}
