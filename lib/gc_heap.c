/**
 * gc_heap.c - GC Heap Allocator Implementation
 *
 * All GC-managed allocations are prepended with a 16-byte GCHeader
 * and linked into an intrusive singly-linked list (newest first).
 * Frame management uses a stack of saved list head pointers.
 */
#include "gc_heap.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

gc_heap_t* gc_heap_create(void) {
    gc_heap_t* gc = (gc_heap_t*)calloc(1, sizeof(gc_heap_t));
    if (!gc) {
        log_error("gc_heap_create: failed to allocate gc_heap");
        return NULL;
    }
    gc->pool = pool_create();
    if (!gc->pool) {
        log_error("gc_heap_create: failed to create pool");
        free(gc);
        return NULL;
    }
    gc->all_objects = NULL;
    gc->total_allocated = 0;
    gc->object_count = 0;
    return gc;
}

void gc_heap_destroy(gc_heap_t* gc) {
    if (!gc) return;
    // pool_destroy bulk-frees all pool-allocated memory (including all GCHeaders + objects)
    if (gc->pool) pool_destroy(gc->pool);
    free(gc);
}

void* gc_heap_alloc(gc_heap_t* gc, size_t size, uint16_t type_tag) {
    if (!gc || !gc->pool) {
        log_error("gc_heap_alloc: invalid gc_heap");
        return NULL;
    }
    // allocate header + user data as one contiguous block
    size_t total = sizeof(gc_header_t) + size;
    gc_header_t* header = (gc_header_t*)pool_alloc(gc->pool, total);
    if (!header) {
        log_error("gc_heap_alloc: pool_alloc failed for %zu bytes", total);
        return NULL;
    }
    // initialize header
    header->next = gc->all_objects;
    header->type_tag = type_tag;
    header->gc_flags = 0;
    header->marked = 0;
    header->alloc_size = (uint32_t)size;

    // link to head of all_objects list
    gc->all_objects = header;
    gc->total_allocated += total;
    gc->object_count++;

    // return pointer to user data (after header)
    return (void*)(header + 1);
}

void* gc_heap_calloc(gc_heap_t* gc, size_t size, uint16_t type_tag) {
    void* ptr = gc_heap_alloc(gc, size, type_tag);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void gc_heap_pool_free(gc_heap_t* gc, void* ptr) {
    if (!ptr || !gc) return;
    gc_header_t* header = gc_get_header(ptr);
    // guard against double-free (e.g., frame_end freed it, then free_item tries again)
    if (header->gc_flags & GC_FLAG_FREED) return;
    // mark as freed so frame_end walk and future free_item calls can skip it.
    // DO NOT call pool_free here — the header must remain readable for
    // frame_end's linked list traversal. pool_free corrupts the header's
    // next pointer (used by allocator free list), breaking the walk.
    // Actual pool_free is deferred to frame_end when nodes are unlinked.
    header->gc_flags |= GC_FLAG_FREED;
    gc->total_allocated -= sizeof(gc_header_t) + header->alloc_size;
    gc->object_count--;
}

