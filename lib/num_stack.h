#ifndef NUM_STACK_H
#define NUM_STACK_H

#include <stdlib.h>
#include <stdbool.h>
#include "datetime.h"

// union to store either long or double values
typedef union {
    int64_t as_long;
    double as_double;
    DateTime as_datetime;
} num_value_t;

typedef struct num_chunk num_chunk_t;

// main stack structure
typedef struct num_stack_t {
    num_chunk_t *head;             // first chunk
    num_chunk_t *tail;             // last chunk
    num_chunk_t *current_chunk;    // current chunk being written to
    size_t current_chunk_position; // position within current chunk
    size_t total_length;           // total number of elements across all chunks
    size_t initial_chunk_size;     // size of the first chunk
} num_stack_t;

// stack management functions
num_stack_t* num_stack_create(size_t initial_capacity);
void num_stack_destroy(num_stack_t *stack);

// push operations
int64_t* num_stack_push_long(num_stack_t *stack, int64_t value);
double* num_stack_push_double(num_stack_t *stack, double value);
DateTime* num_stack_push_datetime(num_stack_t *stack, DateTime value);

// access operations
num_value_t* num_stack_get(num_stack_t *stack, size_t index);
num_value_t* num_stack_peek(num_stack_t *stack);
bool num_stack_pop(num_stack_t *stack);

// stack state operations
bool num_stack_reset_to_index(num_stack_t *stack, size_t index);
size_t num_stack_length(num_stack_t *stack);
bool num_stack_is_empty(num_stack_t *stack);

#endif // NUM_STACK_H
