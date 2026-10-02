#ifndef SCRATCH_ARENA_H
#define SCRATCH_ARENA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include "arena.h"

/**
 * Scratch Arena - strictly LIFO temporary allocator (the Stack class)
 *
 * Used for pass-local temporaries in layout, render, and event handling.
 * Each allocation carries a small header (16 bytes) forming a
 * backward-linked list.
 *
 * Usage:
 *   ScratchArena sa;
 *   scratch_init(&sa, backing_arena);
 *   {
 *       ScratchScope scope(&sa);              // C++; C uses scratch_scope_*
 *       float* a = scope.array<float>(n);
 *       float* b = scope.array_zero<float>(m);
 *   }                                         // a and b freed here, LIFO
 *   scratch_release(&sa);                     // safety net: rewind everything
 *
 * There is no individual free: memory is returned only by ending the scope
 * that allocated it, by scratch_restore to a plain mark, or by release.
 */

// Allocation header - backward-linked list node
// Exactly 16 bytes for natural alignment of payload
typedef struct ScratchHeader {
    struct ScratchHeader* prev;  // previous allocation (backward link)
    uint32_t size;               // payload size (excluding header), max 4GB
    uint32_t scope;              // serial of the allocating scope (0 = plain)
} ScratchHeader;

#define SCRATCH_HEADER_SIZE sizeof(ScratchHeader)  // 16 bytes

// Scratch arena state - lightweight, stack-allocatable
typedef struct ScratchArena {
    Arena* arena;            // backing arena for actual memory
    ScratchHeader* head;     // most recent allocation (top of stack)
    void* mem_node;          // MemContext registration node (NULL if untracked)
    bool scope_active;       // paired arena scope registration
    uint32_t open_scope;     // serial of the innermost open scope (0 = none)
    uint32_t last_scope;     // last serial issued
} ScratchArena;

// Mark for save/restore pattern; a scope mark also carries its serial
typedef struct ScratchMark {
    ScratchHeader* head;     // saved head pointer
    uint32_t scope;          // scope serial (0 = plain mark or ended scope)
    uint32_t outer;          // enclosing open scope, restored at scope end
} ScratchMark;

// Install a hook called by scratch_release to release a registered scratch
// arena's mem_node. Set by the allocator factory; NULL by default (no-op).
void scratch_set_node_release_hook(void (*fn)(void* node));

/**
 * Initialize a scratch arena on an existing backing arena
 * @param sa Scratch arena to initialize (caller-owned, typically on stack)
 * @param arena Backing arena for memory allocation
 */
void scratch_init(ScratchArena* sa, Arena* arena);

/**
 * Allocate memory from scratch arena
 * Returns 16-byte aligned memory with a ScratchHeader preceding it.
 * @param sa Scratch arena
 * @param size Bytes to allocate (payload only)
 * @return Pointer to allocated memory, or NULL on failure
 */
void* scratch_alloc(ScratchArena* sa, size_t size);

/**
 * Allocate zero-initialized memory from scratch arena
 * @param sa Scratch arena
 * @param size Bytes to allocate and zero
 * @return Pointer to zeroed memory, or NULL on failure
 */
void* scratch_calloc(ScratchArena* sa, size_t size);

/**
 * Save current position for later restore
 * @param sa Scratch arena
 * @return Mark that can be passed to scratch_restore
 */
ScratchMark scratch_mark(ScratchArena* sa);

/**
 * Restore to a previously saved mark, freeing all allocations since
 * @param sa Scratch arena
 * @param mark Previously saved mark
 */
void scratch_restore(ScratchArena* sa, ScratchMark mark);

/**
 * Release all scratch allocations back to the backing arena
 * Rewinds all allocations made through this scratch arena.
 * The backing arena itself is NOT reset — only scratch-tracked allocations
 * are freed via arena_free().
 * @param sa Scratch arena
 */
void scratch_release(ScratchArena* sa);

/**
 * Get number of live (non-freed) allocations
 * @param sa Scratch arena
 * @return Count of active allocations
 */
size_t scratch_live_count(ScratchArena* sa);

/**
 * Scopes: the only way to release individual scratch memory. A scope owns
 * every block allocated through it and frees them all, LIFO, at its end.
 * Scopes nest strictly. Ending a scope that would unwind a live block it did
 * not allocate (a callee's block still live, or a plain scratch_alloc made
 * inside the scope) is reported as an error and asserted in debug builds.
 */
ScratchMark scratch_scope_begin(ScratchArena* sa);
void* scratch_scope_alloc(ScratchArena* sa, ScratchMark* scope, size_t size);
void* scratch_scope_calloc(ScratchArena* sa, ScratchMark* scope, size_t size);
// Ends the scope and clears *scope, so a second end is a no-op.
void scratch_scope_end(ScratchArena* sa, ScratchMark* scope);

#ifdef __cplusplus
}

#include "math_checked.hpp"

// Stack-resident scope: begins on construction, ends on every exit path.
struct ScratchScope {
    ScratchArena* sa;
    ScratchMark mark;

    explicit ScratchScope(ScratchArena* s) : sa(s), mark(scratch_scope_begin(s)) {}
    ~ScratchScope() { scratch_scope_end(sa, &mark); }
    // Ends the scope early; the destructor is then a no-op.
    void end() { scratch_scope_end(sa, &mark); }
    ScratchScope(const ScratchScope&) = delete;
    ScratchScope& operator=(const ScratchScope&) = delete;

    void* alloc(size_t size) { return scratch_scope_alloc(sa, &mark, size); }
    void* alloc_zero(size_t size) { return scratch_scope_calloc(sa, &mark, size); }

    // Element arrays with overflow-checked sizing; NULL on overflow or failure.
    template<class T> T* array(size_t count) {
        size_t bytes = 0;
        if (!math_checked_mul(count, sizeof(T), &bytes)) return nullptr;
        return (T*)scratch_scope_alloc(sa, &mark, bytes);
    }
    template<class T> T* array_zero(size_t count) {
        size_t bytes = 0;
        if (!math_checked_mul(count, sizeof(T), &bytes)) return nullptr;
        return (T*)scratch_scope_calloc(sa, &mark, bytes);
    }
};
#endif

#endif // SCRATCH_ARENA_H
