// priority_queue.c
// Min-heap implementation for priority queue

#include "priority_queue.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 16
#define PARENT(i) (((i) - 1) / 2)
#define LEFT_CHILD(i) (2 * (i) + 1)
#define RIGHT_CHILD(i) (2 * (i) + 2)

// Create priority queue
PriorityQueue* priority_queue_create(size_t initial_capacity) {
    PriorityQueue* pq = (PriorityQueue*)malloc(sizeof(PriorityQueue));
    if (!pq) return NULL;
    
    if (initial_capacity == 0) {
        initial_capacity = INITIAL_CAPACITY;
    }
    
    pq->entries = (PriorityQueueEntry*)malloc(initial_capacity * sizeof(PriorityQueueEntry));
    if (!pq->entries) {
        free(pq);
        return NULL;
    }
    
    pq->size = 0;
    pq->capacity = initial_capacity;
    
    return pq;
}

// Destroy priority queue
void priority_queue_destroy(PriorityQueue* pq) {
    if (!pq) return;
    free(pq->entries);
    free(pq);
}

// Swap two entries
static void swap_entries(PriorityQueueEntry* a, PriorityQueueEntry* b) {
    PriorityQueueEntry temp = *a;
    *a = *b;
    *b = temp;
}

// Bubble up to maintain heap property
static void bubble_up(PriorityQueue* pq, size_t index) {
    while (index > 0) {
        size_t parent = PARENT(index);
        
        // Min-heap: parent should have lower or equal priority
        if (pq->entries[parent].priority <= pq->entries[index].priority) {
            break;
        }
        
        swap_entries(&pq->entries[parent], &pq->entries[index]);
        index = parent;
    }
}

// Bubble down to maintain heap property
static void bubble_down(PriorityQueue* pq, size_t index) {
    while (true) {
        size_t smallest = index;
        size_t left = LEFT_CHILD(index);
        size_t right = RIGHT_CHILD(index);
        
        // Check left child
        if (left < pq->size && 
            pq->entries[left].priority < pq->entries[smallest].priority) {
            smallest = left;
        }
        
        // Check right child
        if (right < pq->size && 
            pq->entries[right].priority < pq->entries[smallest].priority) {
            smallest = right;
        }
        
        // If smallest is still index, we're done
        if (smallest == index) {
            break;
        }
        
        swap_entries(&pq->entries[index], &pq->entries[smallest]);
        index = smallest;
    }
}

// Push item into queue
bool priority_queue_push(PriorityQueue* pq, void* data, int priority) {
    if (!pq) return false;
    
    // Resize if needed
    if (pq->size >= pq->capacity) {
        size_t new_capacity = pq->capacity * 2;
        PriorityQueueEntry* new_entries = (PriorityQueueEntry*)realloc(
            pq->entries, new_capacity * sizeof(PriorityQueueEntry));
        
        if (!new_entries) return false;
        
        pq->entries = new_entries;
        pq->capacity = new_capacity;
    }
    
    // Add new entry at end
    pq->entries[pq->size].data = data;
    pq->entries[pq->size].priority = priority;
    
    // Restore heap property
    bubble_up(pq, pq->size);
    pq->size++;
    
    return true;
}

// Pop highest priority item (lowest priority number)
void* priority_queue_pop(PriorityQueue* pq) {
    if (!pq || pq->size == 0) return NULL;
    
    // Get root (highest priority)
    void* data = pq->entries[0].data;
    
    // Move last element to root
    pq->size--;
    if (pq->size > 0) {
        pq->entries[0] = pq->entries[pq->size];
        bubble_down(pq, 0);
    }
    
    return data;
}

// Peek at highest priority item without removing
void* priority_queue_peek(const PriorityQueue* pq) {
    if (!pq || pq->size == 0) return NULL;
    return pq->entries[0].data;
}

// Check if queue is empty
bool priority_queue_is_empty(const PriorityQueue* pq) {
    return !pq || pq->size == 0;
}

// Get queue size
size_t priority_queue_size(const PriorityQueue* pq) {
    return pq ? pq->size : 0;
}

// Clear all entries
void priority_queue_clear(PriorityQueue* pq) {
    if (pq) {
        pq->size = 0;
    }
}
