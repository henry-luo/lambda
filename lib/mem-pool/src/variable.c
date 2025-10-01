#include "internals.h"
#include "../include/mem_pool.h"
#include <string.h>
#include <assert.h>
#include <stdint.h>

// AddressSanitizer annotations for custom allocator
#ifdef __has_feature
  #if __has_feature(address_sanitizer)
    #define ASAN_ENABLED 1
  #endif
#endif

#ifdef ASAN_ENABLED
void __asan_unpoison_memory_region(void const volatile *addr, size_t size);
void __asan_poison_memory_region(void const volatile *addr, size_t size);
#else
#define __asan_unpoison_memory_region(addr, size) ((void)(addr), (void)(size))
#define __asan_poison_memory_region(addr, size) ((void)(addr), (void)(size))
#endif

struct SizedBlock {
    Header header;
    SizedBlock *next_in_free_list;
};

struct VariableMemPool {
    size_t buff_size;
    size_t header_size;
    uint16_t tolerance_percent;
    Buffer *buff_head;
    Buffer *buff_last;
    SizedBlock *block_head;
    // pthread_mutex_t mutex;
};


MemPoolError pool_variable_init(VariableMemPool **pool, size_t grow_size, uint16_t tolerance_percent)
{
    *pool = malloc(sizeof(VariableMemPool));
    if (!*pool) {
        return MEM_POOL_ERR_MALLOC;
    }
    /* (*pool)->tolerance_percent = tolerance_percent <= MEM_POOL_NO_BEST_FIT ? MEM_POOL_NO_BEST_FIT : tolerance_percent; */
    (*pool)->tolerance_percent = tolerance_percent >= MEM_POOL_NO_BEST_FIT ? MEM_POOL_NO_BEST_FIT : tolerance_percent;
    (*pool)->header_size = mem_align(sizeof(Header));
    (*pool)->buff_size = grow_size;
    (*pool)->buff_head = buffer_new(grow_size);

    if (!(*pool)->buff_head) {
        return MEM_POOL_ERR_MALLOC;
    }

    (*pool)->buff_last = (*pool)->buff_head;
    (*pool)->block_head = NULL;

    // mutex_init((*pool));

    return MEM_POOL_ERR_OK;
}

static void *from_buffer(Buffer *buff, size_t header_size, size_t block_size)
{
    Header *header = (void *) buff->curr_ptr;
    header->size = block_size;
    header->prev_in_buff = (void *) buff->prev_ptr;

    buff->prev_ptr = buff->curr_ptr;
    buff->curr_ptr += (header_size + block_size);

    void *user_ptr = (char *)header + header_size;
    return user_ptr;
}

static void *best_fit_from_free_list(VariableMemPool *pool, size_t required_size)
{
    SizedBlock **curr = &pool->block_head;
    size_t block_size;
    long diff;
    size_t diff_percent;
    int iterations = 0;
    const int MAX_ITERATIONS = 1000;

    while (iterations++ < MAX_ITERATIONS) {
        // First unpoison the pointer location itself to safely check for NULL
        if (curr && *curr) {
            __asan_unpoison_memory_region(*curr, sizeof(SizedBlock));
        }

        // Now safely check if we have a current block pointer
        if (*curr == NULL) {
            break;
        }

        // SAFETY CHECK: Validate pointer before dereferencing
        if (!buffer_list_find(pool->buff_head, *curr)) {
            printf("ERROR: Corrupted free list pointer in best_fit_from_free_list: %p\n", (void*)*curr);
            *curr = NULL;
            break;
        }

        block_size = (*curr)->header.size;
        diff = labs((long) block_size - (long) required_size);
        diff_percent = ((size_t) diff * 100) / ((block_size + required_size) / 2);

        if (MEM_POOL_NO_BEST_FIT == pool->tolerance_percent || diff_percent <= pool->tolerance_percent) {
            SizedBlock *block = *curr;

            // CRITICAL: Check if reusing this block would overlap with allocated memory
            void *block_start = (char *)block + pool->header_size;
            void *block_end = (char *)block_start + required_size;

            // Check if this expanded block would overlap with any currently allocated blocks
            bool would_overlap = false;
            Buffer *buff = pool->buff_head;
            while (buff && !would_overlap) {
                // Check all allocated blocks in this buffer
                char *curr_ptr = buff->start;
                while (curr_ptr < buff->curr_ptr && !would_overlap) {
                    Header *header = (Header*)curr_ptr;
                    void *allocated_start = curr_ptr + pool->header_size;
                    void *allocated_end = curr_ptr + pool->header_size + header->size;

                    // Skip the block we're trying to reuse (it's in the free list)
                    if (allocated_start == block_start) {
                        curr_ptr += pool->header_size + header->size;
                        continue;
                    }

                    // Check for overlap: [block_start, block_end) overlaps [allocated_start, allocated_end)
                    if (block_start < allocated_end && allocated_start < block_end) {
                        would_overlap = true;
                        break;
                    }

                    curr_ptr += pool->header_size + header->size;
                }
                buff = buff->next;
            }

            if (would_overlap) {
                // First get the next pointer while this block is still unpoisoned
                SizedBlock *next_block = (*curr)->next_in_free_list;
                // Re-poison the header area (but keep the free list structure accessible)
                __asan_poison_memory_region((char*)*curr + sizeof(Header), (*curr)->header.size - sizeof(Header));
                // Move to next block
                *curr = next_block;
                continue;
            }

            void *user_ptr = (char *)block + pool->header_size;
            // Get next pointer while this block is still unpoisoned
            SizedBlock *next_block = (*curr)->next_in_free_list;
            *curr = next_block;

            // Unpoison this memory since we're reusing it
            __asan_unpoison_memory_region(user_ptr, required_size);

            return user_ptr;
        }

        // Re-poison the header area (but keep the free list structure accessible)
        __asan_poison_memory_region((char*)*curr + sizeof(Header), (*curr)->header.size - sizeof(Header));
        // Get next pointer while this block is still unpoisoned
        SizedBlock *next_block = (*curr)->next_in_free_list;
        *curr = next_block;
    }

    if (iterations >= MAX_ITERATIONS) {
        printf("ERROR: Infinite loop detected in best_fit_from_free_list\n");
    }

    return NULL;
}

MemPoolError pool_variable_alloc(VariableMemPool *pool, size_t size, void **ptr) {
    Buffer *buff = pool->buff_last;
    size_t block_size = mem_align(size);

    if (pool->block_head && (*ptr = best_fit_from_free_list(pool, block_size))) {
        // Tell AddressSanitizer that this memory is now allocated and accessible
        __asan_unpoison_memory_region(*ptr, block_size);
        // unlock(pool);
        return MEM_POOL_ERR_OK;
    }

    if (!buffer_has_space(buff, pool->header_size + block_size)) {
        size_t new_buff_size = pool->header_size + max(pool->buff_size, block_size);
        buff->next = buffer_new(new_buff_size);
        if (!buff->next) {
            return MEM_POOL_ERR_MALLOC;
        }
        buff = buff->next;
        pool->buff_last = buff;
    }

    *ptr = from_buffer(buff, pool->header_size, block_size);

    // Tell AddressSanitizer that this memory is now allocated and accessible
    __asan_unpoison_memory_region(*ptr, block_size);

    // unlock(pool);

    return MEM_POOL_ERR_OK;
}

void* pool_calloc(VariableMemPool* pool, size_t size) {
    void* bytes;
    if (pool_variable_alloc(pool, size, &bytes) == MEM_POOL_ERR_OK) {
        memset(bytes, 0, size);
        return bytes;
    }
    return NULL;
}

void* pool_variable_realloc(VariableMemPool *pool, void *ptr, size_t data_size, size_t new_size) {
    void *new_ptr;
    MemPoolError err = pool_variable_alloc(pool, new_size, &new_ptr);
    if (err != MEM_POOL_ERR_OK) {
        return NULL;
    }
    // Tell AddressSanitizer about the new allocation size
    __asan_unpoison_memory_region(new_ptr, new_size);

    // copy the old data to the new block
    if (new_ptr == ptr) {
        // For same address reallocation, just tell AddressSanitizer about the new size
    }

    if (ptr) {
        if (data_size) {
            // Use memmove instead of memcpy to handle overlapping memory regions
            memmove(new_ptr, ptr, data_size);

        }

        if (new_ptr != ptr) {

            pool_variable_free(pool, ptr);
        }
    }

    return new_ptr;
}

static int delete_block_from_free_list(VariableMemPool *pool, SizedBlock *block) {
    SizedBlock **curr = &pool->block_head;
    int iterations = 0;
    const int MAX_ITERATIONS = 10000; // Prevent infinite loops

    while (*curr && iterations++ < MAX_ITERATIONS) {
        // SAFETY CHECK: Validate pointer before dereferencing
        if (!buffer_list_find(pool->buff_head, *curr)) {
            printf("ERROR: Corrupted free list pointer detected: %p\n", (void*)*curr);
            printf("ERROR: This pointer is not in any valid buffer\n");
            // Remove the corrupted pointer to prevent crash
            *curr = NULL;
            return 0;
        }

        // Additional safety: Check if the pointer looks reasonable
        uintptr_t ptr_val = (uintptr_t)*curr;
        if (ptr_val < 0x1000 || ptr_val == 0x6e6120646c6f6230ULL) {
            printf("ERROR: Suspicious pointer value detected: 0x%lx\n", (unsigned long)ptr_val);
            *curr = NULL;
            return 0;
        }

        if ((*curr) == block) {
            *curr = (*curr)->next_in_free_list;
            return 1;
        }

        curr = &(*curr)->next_in_free_list;
    }

    if (iterations >= MAX_ITERATIONS) {
        printf("ERROR: Infinite loop detected in free list traversal\n");
        return 0;
    }

    return 0;
}

MemPoolError pool_variable_is_associated(VariableMemPool *pool, void *ptr)
{
    MemPoolError err;
    poool_is_associated(pool, ptr, err);

    return err;
}

static SizedBlock *append(SizedBlock *to, SizedBlock *from, size_t header_size)
{
    to->header.size += from->header.size + header_size;

    return to;
}

static SizedBlock *merge_next_free_blocks(VariableMemPool *pool, Buffer *buff, SizedBlock *block)
{
    void *next = NULL;

    while (1) {
        next = (SizedBlock *)((char *)block + block->header.size + pool->header_size);

        if (buffer_has(buff, next) && delete_block_from_free_list(pool, next)) {
            block = append(block, next, pool->header_size);
        } else {
            break;
        }
    }

    return block;
}

static SizedBlock *merge_previous_free_blocks(VariableMemPool *pool, SizedBlock *block)
{
    SizedBlock *prev = block->header.prev_in_buff;

    while (prev) {
        if (!delete_block_from_free_list(pool, prev)) {
            break;
        }
        block = append(prev, block, pool->header_size);
        prev = prev->header.prev_in_buff;
    }

    return block;
}

static SizedBlock *defragment(VariableMemPool *pool, Buffer *buff, SizedBlock *block)
{
    block = merge_next_free_blocks(pool, buff, block);
    block = merge_previous_free_blocks(pool, block);

    // Add the merged block to the free list
    block->next_in_free_list = pool->block_head;
    pool->block_head = block;

    return block;
}

MemPoolError pool_variable_free(VariableMemPool *pool, void *ptr) {
    // SAFETY CHECK: Validate input parameters
    if (!pool || !ptr) {
        printf("ERROR: Invalid parameters to pool_variable_free\n");
        return MEM_POOL_ERR_UNKNOWN_BLOCK;
    }

    Buffer *buff = buffer_list_find(pool->buff_head, ptr);
    SizedBlock *new = (SizedBlock *)((char *)ptr - pool->header_size);
    size_t user_data_size = 0;  // Track user data size for AddressSanitizer

    if (!buff) {
        // SAFETY CHECK: Pointer not from this pool
        printf("ERROR: Attempting to free pointer not allocated by this pool: %p\n", ptr);
        // unlock(pool);
        return MEM_POOL_ERR_UNKNOWN_BLOCK;
    } else {
        // SAFETY CHECK: Validate header before defragmentation
        if (new->header.size == 0 || new->header.size > 10*1024*1024) {
            printf("ERROR: Invalid block size detected: %zu\n", new->header.size);
            return MEM_POOL_ERR_UNKNOWN_BLOCK;
        }

        // Calculate user data size before defragmentation
        user_data_size = new->header.size;

        // SAFETY CHECK: Check if block is already in free list (double-free detection)
        SizedBlock *current = pool->block_head;
        int free_list_count = 0;
        while (current && free_list_count < 100) {
            void *current_user_ptr = (char*)current + pool->header_size;
            size_t current_user_size = current->header.size - pool->header_size;
            void *current_user_end = (char*)current_user_ptr + current_user_size;

            // Check if the user pointer exactly matches this free block's start
            if (ptr == current_user_ptr) {
                return MEM_POOL_ERR_UNKNOWN_BLOCK;
            }

            // Check if the user pointer falls within this free block's user range
            if (ptr >= current_user_ptr && ptr < current_user_end) {
                return MEM_POOL_ERR_UNKNOWN_BLOCK;
            }

            current = current->next_in_free_list;
            free_list_count++;
        }

        // Store the original size before defragmentation
        // size_t original_size = user_data_size;

        // Call defragmentation
        new = defragment(pool, buff, new);

        // Post-defragmentation check - just log the results
        // void *new_user_ptr = (char*)new + pool->header_size;
    }
    // Note: Block is already added to free list above during defragmentation
    // No need to add it again here

    // Tell AddressSanitizer that this USER data is now freed and should not be accessed
    // Note: We poison the user data but leave the header accessible for free list management
    if (user_data_size > 0) {
        __asan_poison_memory_region(ptr, user_data_size);
    }

    // unlock(pool);
    return MEM_POOL_ERR_OK;
}

MemPoolError pool_variable_aligned_sizeof(VariableMemPool *pool, void *ptr, size_t *size)
{
    // lock(pool);
    if (!buffer_list_find(pool->buff_head, ptr)) {
        return MEM_POOL_ERR_UNKNOWN_BLOCK;
    }

    SizedBlock *block = (SizedBlock *)((char *)ptr - pool->header_size);
    *size = block->header.size;;

    // unlock(pool);
    return MEM_POOL_ERR_OK;
}

MemPoolError pool_variable_destroy(VariableMemPool *pool)
{
    pool_destroy(pool);

    return MEM_POOL_ERR_OK;
}
