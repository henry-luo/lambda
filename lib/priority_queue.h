// priority_queue.h
// Min-heap based priority queue for task scheduling
// Lower priority number = higher urgency (0 is highest priority)

#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Priority queue entry
typedef struct PriorityQueueEntry {
    void* data;           // User data
    int priority;         // Priority value (lower = higher priority)
} PriorityQueueEntry;
typedef int (*PriorityQueueCompare)(const void* left,const void* right,void* owner);
typedef void (*PriorityQueueRelocate)(void* data,size_t index,void* owner);

// Priority queue structure (min-heap)
typedef struct PriorityQueue {
    PriorityQueueEntry* entries;  // Dynamic array of entries
    size_t size;                  // Current number of entries
    size_t capacity;              // Allocated capacity
    PriorityQueueCompare compare;
    PriorityQueueRelocate relocate;
    void* owner;
} PriorityQueue;

// Create and destroy
PriorityQueue* priority_queue_create(size_t initial_capacity);
// custom ordering ignores integer priorities; indexed items must have unique data pointers.
// relocation reports SIZE_MAX on pop/clear. Callbacks must not mutate the queue.
PriorityQueue* priority_queue_create_custom(size_t capacity,PriorityQueueCompare compare,PriorityQueueRelocate relocate,void* owner);
void priority_queue_destroy(PriorityQueue* pq);

// Operations
bool priority_queue_push(PriorityQueue* pq, void* data, int priority);
void* priority_queue_pop(PriorityQueue* pq);  // Returns highest priority item
void* priority_queue_peek(const PriorityQueue* pq);  // View without removing
bool priority_queue_is_empty(const PriorityQueue* pq);
size_t priority_queue_size(const PriorityQueue* pq);
void priority_queue_clear(PriorityQueue* pq);
// restore order after changing the indexed item's comparison key, in either direction.
bool priority_queue_update(PriorityQueue* pq,size_t index);

#ifdef __cplusplus
}
#endif

#endif // PRIORITY_QUEUE_H
