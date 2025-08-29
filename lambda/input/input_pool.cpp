// input_pool.cpp
// Memory cache/pooling for Input objects
// Implements LRU cache for parsed Input*

#include "input.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// LRU cache entry for Input*
typedef struct InputCacheEntry {
    char* url_key;
    Input* input;
    time_t created_at;
    time_t last_accessed;
    size_t memory_size;
    struct InputCacheEntry* prev;
    struct InputCacheEntry* next;
} InputCacheEntry;

// Input cache manager
typedef struct InputCacheManager {
    InputCacheEntry* lru_head;
    InputCacheEntry* lru_tail;
    size_t max_memory_size;
    size_t current_memory_size;
    int max_entries;
    int entry_count;
    // ...hashmap for fast lookup...
} InputCacheManager;


// Create cache manager
InputCacheManager* cache_manager_create(size_t max_mem, int max_entries) {
    InputCacheManager* mgr = (InputCacheManager*)calloc(1, sizeof(InputCacheManager));
    mgr->max_memory_size = max_mem;
    mgr->max_entries = max_entries;
    mgr->entry_count = 0;
    mgr->current_memory_size = 0;
    mgr->lru_head = mgr->lru_tail = NULL;
    // TODO: initialize hashmap
    return mgr;
}

// Destroy cache manager
void cache_manager_destroy(InputCacheManager* mgr) {
    if (!mgr) return;
    // TODO: free all entries and hashmap
    free(mgr);
}

// Lookup/add/evict
Input* input_cache_lookup(InputCacheManager* mgr, const char* url_key);
void input_cache_add(InputCacheManager* mgr, const char* url_key, Input* input, size_t mem_size);
void input_cache_evict(InputCacheManager* mgr);
