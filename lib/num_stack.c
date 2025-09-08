#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "num_stack.h"
#include "log.h"

// chunk structure for the linked list
struct num_chunk {
    num_value_t *data;             // array of elements in this chunk
    size_t capacity;               // maximum number of elements this chunk can hold
    size_t used;                   // number of elements currently used in this chunk
    struct num_chunk *next;        // next chunk in the list
    struct num_chunk *prev;        // previous chunk in the list
    int index;                     // index of this chunk for debugging
};

// initialize a new number stack
num_stack_t* num_stack_create(size_t initial_capacity) {
    if (initial_capacity == 0) {
        initial_capacity = 16; // default initial capacity
    }
    
    num_stack_t *stack = malloc(sizeof(num_stack_t));
    if (!stack) return NULL;
    
    // allocate the first chunk
    num_chunk_t *first_chunk = malloc(sizeof(num_chunk_t));
    if (!first_chunk) {
        free(stack);
        return NULL;
    }
    
    first_chunk->data = malloc(sizeof(num_value_t) * initial_capacity);
    if (!first_chunk->data) {
        free(first_chunk);
        free(stack);
        return NULL;
    }
    
    first_chunk->capacity = initial_capacity;
    first_chunk->used = 0;
    first_chunk->next = NULL;
    first_chunk->prev = NULL;
    first_chunk->index = 0;
    
    stack->head = first_chunk;
    stack->tail = first_chunk;
    stack->current_chunk = first_chunk;
    stack->current_chunk_position = 0;
    stack->total_length = 0;
    stack->initial_chunk_size = initial_capacity;
    
    return stack;
}

// allocate a new chunk with double the capacity of the previous one
static num_chunk_t* allocate_new_chunk(num_chunk_t *prev_chunk) {
    num_chunk_t *new_chunk = malloc(sizeof(num_chunk_t));
    if (!new_chunk) return NULL;
    
    size_t new_capacity = prev_chunk->capacity * 2;
    new_chunk->data = malloc(sizeof(num_value_t) * new_capacity);
    if (!new_chunk->data) {
        free(new_chunk);
        return NULL;
    }
    
    new_chunk->capacity = new_capacity;
    new_chunk->used = 0;
    new_chunk->next = NULL;
    new_chunk->prev = prev_chunk;
    new_chunk->index = prev_chunk->index + 1;
    log_debug("allocated new chunk: %p with capacity: %zu, index: %d", new_chunk, new_capacity, new_chunk->index);
    
    // link the chunks
    prev_chunk->next = new_chunk;
    return new_chunk;
}

// push a num_value_t onto the stack
static num_value_t* num_stack_push_value(num_stack_t *stack, num_value_t value) {
    if (!stack) return NULL;
    
    // check if we need a new chunk
    if (stack->current_chunk_position >= stack->current_chunk->capacity) {
        num_chunk_t *new_chunk = allocate_new_chunk(stack->current_chunk);
        if (!new_chunk) return NULL;
        stack->tail = new_chunk;
        stack->current_chunk = new_chunk;
        stack->current_chunk_position = 0;
    }
    
    // store the value
    stack->current_chunk->data[stack->current_chunk_position] = value;
    stack->current_chunk_position++;
    stack->current_chunk->used = stack->current_chunk_position;
    stack->total_length++;

    return &stack->current_chunk->data[stack->current_chunk_position - 1];
}

// push a long value onto the stack
long* num_stack_push_long(num_stack_t *stack, long value) {
    num_value_t val;
    val.as_long = value;
    return (long*)num_stack_push_value(stack, val);
}

// push a double value onto the stack
double* num_stack_push_double(num_stack_t *stack, double value) {
    num_value_t val;
    val.as_double = value;
    return (double*)num_stack_push_value(stack, val);
}

DateTime* num_stack_push_datetime(num_stack_t *stack, DateTime value) {
    num_value_t val;
    val.as_datetime = value;
    return (DateTime*)num_stack_push_value(stack, val);
}

// get element at specific index (0-based)
num_value_t* num_stack_get(num_stack_t *stack, size_t index) {
    if (!stack || index >= stack->total_length) return NULL;
    
    size_t current_index = 0;
    num_chunk_t *chunk = stack->head;
    
    while (chunk) {
        if (current_index + chunk->used > index) {
            // the element is in this chunk
            size_t chunk_offset = index - current_index;
            return &chunk->data[chunk_offset];
        }
        current_index += chunk->used;
        chunk = chunk->next;
    }
    
    return NULL;
}

// reset stack to a specific position and free unused chunks
bool num_stack_reset_to_index(num_stack_t *stack, size_t index) {
    if (!stack) return false;
    // if index is beyond current length, just return false
    if (index > stack->total_length) return false;
    
    // if index equals current length, no change needed
    if (index == stack->total_length) return true;
    
    // start from the tail and work backwards to find the target chunk
    size_t elements_from_end = stack->total_length - index;
    size_t current_elements_counted = 0;
    num_chunk_t *chunk = stack->tail;
    
    // traverse from tail to find the chunk containing the target index
    while (chunk) {
        log_debug("checking num_stack chunk: %p, used: %zu, current_elements_counted: %zu, index: %d", 
            chunk, chunk->used, current_elements_counted, chunk->index);
        if (current_elements_counted + chunk->used >= elements_from_end) {
            // the target position is in this chunk
            size_t elements_to_keep_in_chunk = chunk->used - (elements_from_end - current_elements_counted);
            
            // update chunk usage
            chunk->used = elements_to_keep_in_chunk;
            
            // free all chunks after this one
            num_chunk_t *chunk_to_free = chunk->next;
            while (chunk_to_free) {
                log_debug("freeing num_stack chunk: %p, used: %zu, index: %d", chunk_to_free, chunk_to_free->used, chunk_to_free->index);
                num_chunk_t *next = chunk_to_free->next;
                free(chunk_to_free->data);
                free(chunk_to_free);
                chunk_to_free = next;
            }
            
            // update stack state
            chunk->next = NULL;
            stack->tail = chunk;
            stack->current_chunk = chunk;
            stack->current_chunk_position = elements_to_keep_in_chunk;
            stack->total_length = index;
            return true;
        }
        current_elements_counted += chunk->used;
        chunk = chunk->prev;
    }
    // if we get here, something went wrong
    log_error("num_stack_reset_to_index: failed to find chunk for index %zu", index);
    return false;
}

// get the current length of the stack
size_t num_stack_length(num_stack_t *stack) {
    return stack ? stack->total_length : 0;
}

// peek at the top element without removing it
num_value_t* num_stack_peek(num_stack_t *stack) {
    if (!stack || stack->total_length == 0) return NULL;
    return num_stack_get(stack, stack->total_length - 1);
}

// pop the top element from the stack
bool num_stack_pop(num_stack_t *stack) {
    if (!stack || stack->total_length == 0) return false;
    return num_stack_reset_to_index(stack, stack->total_length - 1);
}

// free the entire stack
void num_stack_destroy(num_stack_t *stack) {
    if (!stack) return;
    
    num_chunk_t *chunk = stack->head;
    while (chunk) {
        num_chunk_t *next = chunk->next;
        free(chunk->data);
        free(chunk);
        chunk = next;
    }
    free(stack);
}

// check if stack is empty
bool num_stack_is_empty(num_stack_t *stack) {
    return !stack || stack->total_length == 0;
}