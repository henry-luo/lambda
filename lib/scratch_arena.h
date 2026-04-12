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
 * Scratch Arena - LIFO-optimized temporary allocator
 *
 * Designed to replace scoped malloc/free pairs in layout, render, and event
 * handling. Each allocation carries a small header (16 bytes) forming a
 * backward-linked list. Frees are O(1) in the common LIFO case, with
 * backward coalescing of holes for near-LIFO patterns.
 *
 * Usage:
 *   ScratchArena sa;
 *   scratch_init(&sa, backing_arena);
 *   void* a = scratch_alloc(&sa, 1024);
 *   void* b = scratch_alloc(&sa, 2048);
 *   scratch_free(&sa, b);  // LIFO: O(1) bump-back
 *   scratch_free(&sa, a);  // LIFO: O(1) bump-back
 *   scratch_release(&sa);  // safety net: rewind everything
 *
 * Non-LIFO free:
 *   scratch_free(&sa, a);  // marks as hole
 *   scratch_free(&sa, b);  // LIFO free + backward walk reclaims a's hole
 *
 * Mark/Restore (for pure-scoped patterns):
 *   ScratchMark mark = scratch_mark(&sa);
 *   void* x = scratch_alloc(&sa, 512);
 *   void* y = scratch_alloc(&sa, 256);
 *   scratch_restore(&sa, mark);  // frees both x and y
 */

// Allocation header - backward-linked list node
// Exactly 16 bytes for natural alignment of payload
typedef struct ScratchHeader {
    struct ScratchHeader* prev;  // previous allocation (backward link)
    uint32_t size;               // payload size (excluding header), max 4GB
    uint32_t flags;              // bit 0: freed (hole)
} ScratchHeader;

#define SCRATCH_FLAG_FREED  0x01
#define SCRATCH_HEADER_SIZE sizeof(ScratchHeader)  // 16 bytes

// Scratch arena state - lightweight, stack-allocatable
typedef struct ScratchArena {
    Arena* arena;            // backing arena for actual memory
    ScratchHeader* head;     // most recent allocation (top of stack)
} ScratchArena;

// Mark for save/restore pattern
typedef struct ScratchMark {
    ScratchHeader* head;     // saved head pointer
} ScratchMark;

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
 * Free a scratch allocation
 * - LIFO case (ptr is most recent): O(1) bump-back + backward coalescing
 * - Non-LIFO case: O(1) mark as hole, reclaimed later
 * @param sa Scratch arena
 * @param ptr Pointer returned by scratch_alloc/scratch_calloc
 */
void scratch_free(ScratchArena* sa, void* ptr);

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

#ifdef __cplusplus
}
#endif

#endif // SCRATCH_ARENA_H
