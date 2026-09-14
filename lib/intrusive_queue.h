// intrusive_queue.h - allocation-free FIFO mechanics for caller-owned nodes.
#ifndef LIB_INTRUSIVE_QUEUE_H
#define LIB_INTRUSIVE_QUEUE_H

#include <stddef.h>

typedef struct IntrusiveQueueNode {
    struct IntrusiveQueueNode* next;
} IntrusiveQueueNode;

typedef struct IntrusiveQueue {
    IntrusiveQueueNode* first;
    IntrusiveQueueNode* last;
    size_t count;
} IntrusiveQueue;

#define INTRUSIVE_QUEUE_CONTAINER_OF(node, type, member) \
    ((type*)((char*)(node) - offsetof(type, member)))

static inline void intrusive_queue_init(IntrusiveQueue* queue) {
    if (queue) *queue = (IntrusiveQueue){0};
}

static inline void intrusive_queue_push(IntrusiveQueue* queue, IntrusiveQueueNode* node) {
    if (!queue || !node) return;
    node->next = NULL;
    if (queue->last) queue->last->next = node;
    else queue->first = node;
    queue->last = node;
    queue->count++;
}

static inline IntrusiveQueueNode* intrusive_queue_pop(IntrusiveQueue* queue) {
    if (!queue || !queue->first) return NULL;
    IntrusiveQueueNode* node = queue->first;
    queue->first = node->next;
    if (!queue->first) queue->last = NULL;
    node->next = NULL;
    queue->count--;
    return node;
}

static inline IntrusiveQueueNode* intrusive_queue_last(const IntrusiveQueue* queue) {
    return queue ? queue->last : NULL;
}

#endif
