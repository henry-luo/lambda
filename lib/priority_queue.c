// priority_queue.c
// Min-heap implementation for priority queue

#include "priority_queue.h"
#include "grow_capacity.h"
#include "math_checked.hpp"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 16
#define PARENT(i) (((i) - 1) / 2)
#define LEFT_CHILD(i) (2 * (i) + 1)
#define RIGHT_CHILD(i) (2 * (i) + 2)

// Create priority queue
PriorityQueue* priority_queue_create(size_t initial_capacity) {
    return priority_queue_create_custom(initial_capacity,NULL,NULL,NULL);
}
PriorityQueue* priority_queue_create_custom(size_t initial_capacity,PriorityQueueCompare compare,PriorityQueueRelocate relocate,void* owner) {
    PriorityQueue* pq = (PriorityQueue*)calloc(1,sizeof(PriorityQueue));
    if (!pq) return NULL;
    
    if (initial_capacity == 0) {
        initial_capacity = INITIAL_CAPACITY;
    }
    
    size_t allocation_size = 0;
    if (!math_checked_mul(initial_capacity, sizeof(PriorityQueueEntry), &allocation_size)) {
        free(pq);
        return NULL;
    }
    pq->entries = (PriorityQueueEntry*)malloc(allocation_size);
    if (!pq->entries) {
        free(pq);
        return NULL;
    }
    
    pq->size = 0;
    pq->capacity = initial_capacity;
    pq->compare=compare;pq->relocate=relocate;pq->owner=owner;
    
    return pq;
}

// Destroy priority queue
void priority_queue_destroy(PriorityQueue* pq) {
    if (!pq) return;
    free(pq->entries);
    free(pq);
}

// Swap two entries
static int compare_entries(const PriorityQueue* pq,size_t a,size_t b) {
    if(pq->compare)return pq->compare(pq->entries[a].data,pq->entries[b].data,pq->owner);
    return (pq->entries[a].priority>pq->entries[b].priority)-(pq->entries[a].priority<pq->entries[b].priority);
}
static void relocate_entry(PriorityQueue* pq,size_t index) {
    if(pq->relocate)pq->relocate(pq->entries[index].data,index,pq->owner);
}
static void swap_entries(PriorityQueue* pq,size_t a,size_t b) {
    PriorityQueueEntry temp = pq->entries[a];
    pq->entries[a] = pq->entries[b];
    pq->entries[b] = temp;
    relocate_entry(pq,a);relocate_entry(pq,b);
}

// Bubble up to maintain heap property
static size_t bubble_up(PriorityQueue* pq, size_t index) {
    while (index > 0) {
        size_t parent = PARENT(index);
        
        // Min-heap: parent should have lower or equal priority
        if (compare_entries(pq,parent,index)<=0) {
            break;
        }
        
        swap_entries(pq,parent,index);
        index = parent;
    }
    return index;
}

// Bubble down to maintain heap property
static void bubble_down(PriorityQueue* pq, size_t index) {
    while (true) {
        size_t smallest = index;
        size_t left = LEFT_CHILD(index);
        size_t right = RIGHT_CHILD(index);
        
        // Check left child
        if (left < pq->size && 
            compare_entries(pq,left,smallest)<0) {
            smallest = left;
        }
        
        // Check right child
        if (right < pq->size && 
            compare_entries(pq,right,smallest)<0) {
            smallest = right;
        }
        
        // If smallest is still index, we're done
        if (smallest == index) {
            break;
        }
        
        swap_entries(pq,index,smallest);
        index = smallest;
    }
}

// Push item into queue
bool priority_queue_push(PriorityQueue* pq, void* data, int priority) {
    if (!pq) return false;
    
    // Resize if needed
    if (pq->size >= pq->capacity) {
        size_t required_capacity = 0;
        size_t new_capacity = 0;
        if (!math_checked_add(pq->size, 1, &required_capacity) ||
            !lib_grow_capacity(pq->capacity, required_capacity, INITIAL_CAPACITY,
                               &new_capacity) ||
            new_capacity > SIZE_MAX / sizeof(PriorityQueueEntry)) return false;
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
    pq->size++;
    relocate_entry(pq,pq->size-1);
    bubble_up(pq,pq->size-1);
    
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
        relocate_entry(pq,0);
        bubble_down(pq, 0);
    }
    if(pq->relocate)pq->relocate(data,SIZE_MAX,pq->owner);
    
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
        if(pq->relocate)for(size_t i=0;i<pq->size;i++)pq->relocate(pq->entries[i].data,SIZE_MAX,pq->owner);
        pq->size = 0;
    }
}
bool priority_queue_update(PriorityQueue* pq,size_t index) {
    if(!pq||index>=pq->size)return false;
    bubble_down(pq,bubble_up(pq,index));return true;
}
