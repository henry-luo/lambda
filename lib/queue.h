// queue.h - allocation-free FIFO mechanics for caller-owned embedded nodes.
#ifndef LIB_QUEUE_H
#define LIB_QUEUE_H

#include <stddef.h>

typedef struct QueueNode {
    struct QueueNode* next;
} QueueNode;

typedef struct Queue {
    QueueNode* first;
    QueueNode* last;
    size_t count;
} Queue;

#define QUEUE_CONTAINER_OF(node, type, member) \
    ((type*)((char*)(node) - offsetof(type, member)))

static inline void queue_init(Queue* queue) {
    if (queue) *queue = (Queue){0};
}

static inline void queue_push(Queue* queue, QueueNode* node) {
    if (!queue || !node) return;
    node->next = NULL;
    if (queue->last) queue->last->next = node;
    else queue->first = node;
    queue->last = node;
    queue->count++;
}

static inline QueueNode* queue_pop(Queue* queue) {
    if (!queue || !queue->first) return NULL;
    QueueNode* node = queue->first;
    queue->first = node->next;
    if (!queue->first) queue->last = NULL;
    node->next = NULL;
    queue->count--;
    return node;
}

static inline QueueNode* queue_last(const Queue* queue) {
    return queue ? queue->last : NULL;
}

#endif
