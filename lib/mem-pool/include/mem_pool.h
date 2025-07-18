#ifndef MEM_POOL_H
#define MEM_POOL_H


#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>


/**
 * Pass this as the second argument to pool_variable_init if you wan to skip
 * best fit checks
 */
/* #define MEM_POOL_NO_BEST_FIT 101 */
static const uint16_t MEM_POOL_NO_BEST_FIT = 101;


typedef struct FixedMemPool FixedMemPool;

// allocates memory in blocks
typedef struct VariableMemPool VariableMemPool;

/**
 * Return one of these from the callback function to either continue or stop the iteration
 */
typedef enum {
    MEM_POOL_FOREACH_STOP,
    MEM_POOL_FOREACH_CONTINUE
} MemPoolForeachStatus;

typedef MemPoolForeachStatus (*FixedPoolForeach)(void *block);

typedef enum {
    MEM_POOL_ERR_OK,
    MEM_POOL_ERR_MUTEX_INIT,
    MEM_POOL_ERR_MUTEX_DESTROY,
    MEM_POOL_ERR_LOCK,
    MEM_POOL_ERR_UNLOCK,
    MEM_POOL_ERR_MALLOC,
    MEM_POOL_ERR_UNKNOWN_BLOCK
} MemPoolError;

/**
 * initializes a new MemPool, with the given block size. If it runs out of space,
 * it'll create a new internal Buffer with increase_count * block_size size
 */
MemPoolError pool_fixed_init(FixedMemPool **pool, size_t block_size, size_t increase_count);

MemPoolError pool_fixed_alloc(FixedMemPool *pool, void **ptr);

/**
 * @return MEM_POOL_ERR_OK if associated MEM_POOL_ERR_UNKNOWN if not, or the error indicating the failure
 */
MemPoolError pool_fixed_is_associated(FixedMemPool *pool, void *ptr);

/**
 * Iterates through all the blocks allocated with the given pool
 */
MemPoolError pool_fixed_foreach(FixedMemPool *pool, FixedPoolForeach callback);

/**
 * The memory block is not actually freed, just given back to the pool to reuse it
 *
 * @return -1 if the pointer is not known by the pool, 0 otherwise
 */
MemPoolError pool_fixed_free(FixedMemPool *pool, void *ptr);

MemPoolError pool_fixed_destroy(FixedMemPool *pool);

/**
 * grow_size deremines the size of a new buffer required from malloc when no more free (fitting) space left
 * tolerance_percent is the maximum difference in percentage when looking for best fitting free blocks
 * if not reuqired, use MEM_POOL_NO_BEST_FIT
 */
MemPoolError pool_variable_init(VariableMemPool **pool, size_t grow_size, uint16_t tolerance_percent);

MemPoolError pool_variable_alloc(VariableMemPool *pool, size_t size, void **ptr);

void* pool_calloc(VariableMemPool* pool, size_t size);
void* pool_variable_realloc(VariableMemPool *pool, void *ptr,  size_t data_size, size_t new_size);

/**
 * @return MEM_POOL_ERR_OK if associated MEM_POOL_ERR_UNKNOWN if not, or the error indicating the failure
 */
MemPoolError pool_variable_is_associated(VariableMemPool *pool, void *ptr);

/*
 * Before appending to the free list, this function will attempt to merge neighbouring memory blocks 
 * (including the space used by their headers) in the given buffer.
 */
MemPoolError pool_variable_free(VariableMemPool *pool, void *ptr);

MemPoolError pool_variable_destroy(VariableMemPool *pool);

/*
 * @return same as pool_variable_is_associated
 * The *aligned* size of the block is written to the size_t pointer
 */
MemPoolError pool_variable_aligned_sizeof(VariableMemPool *pool, void *ptr, size_t *size);

/**
 * Rounds up the size to the correct alignment
 */
size_t mem_align(size_t size);


#endif
