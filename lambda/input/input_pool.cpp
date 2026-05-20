// input_pool.cpp
// Memory cache for parsed Input objects, backed by lib/lru_cache.

#include "input.hpp"
#include "../../lib/mem.h"
#include "../../lib/lru_cache.h"

#include <stdint.h>
#include <string.h>
#include <time.h>

// LRU cache entry for Input*. lib/lru_cache owns the key string; we own the
// Input* value (released via the on_evict callback).
typedef struct InputCacheEntry {
    Input* input;
    time_t created_at;
    time_t last_accessed;
    size_t memory_size;
} InputCacheEntry;

typedef struct InputCacheManager {
    LruCache* cache;
} InputCacheManager;

static void input_cache_evict_fn(const char* key, void* value, size_t bytes, void* udata) {
    (void)key; (void)bytes; (void)udata;
    InputCacheEntry* entry = (InputCacheEntry*)value;
    if (!entry) return;
    // Input* is owned upstream and freed by its own lifecycle; we just drop
    // our wrapper here.
    mem_free(entry);
}

InputCacheManager* cache_manager_create(size_t max_mem, int max_entries) {
    InputCacheManager* mgr = (InputCacheManager*)mem_calloc(1, sizeof(InputCacheManager), MEM_CAT_INPUT_OTHER);
    if (!mgr) return NULL;
    LruCacheConfig cfg = {};
    cfg.max_entries = max_entries > 0 ? (size_t)max_entries : 0;
    cfg.max_bytes = max_mem;
    cfg.on_evict = input_cache_evict_fn;
    mgr->cache = lru_cache_new(&cfg);
    if (!mgr->cache) { mem_free(mgr); return NULL; }
    return mgr;
}

void cache_manager_destroy(InputCacheManager* mgr) {
    if (!mgr) return;
    if (mgr->cache) lru_cache_free(mgr->cache);
    mem_free(mgr);
}

Input* input_cache_lookup(InputCacheManager* mgr, const char* url_key) {
    if (!mgr || !url_key) return NULL;
    InputCacheEntry* entry = (InputCacheEntry*)lru_cache_get(mgr->cache, url_key);
    if (!entry) return NULL;
    entry->last_accessed = time(NULL);
    return entry->input;
}

void input_cache_add(InputCacheManager* mgr, const char* url_key, Input* input, size_t mem_size) {
    if (!mgr || !url_key || !input) return;
    InputCacheEntry* entry = (InputCacheEntry*)mem_calloc(1, sizeof(InputCacheEntry), MEM_CAT_INPUT_OTHER);
    if (!entry) return;
    entry->input = input;
    entry->created_at = entry->last_accessed = time(NULL);
    entry->memory_size = mem_size;
    if (!lru_cache_put(mgr->cache, url_key, entry, mem_size)) {
        mem_free(entry);
    }
}

void input_cache_evict(InputCacheManager* mgr) {
    if (!mgr) return;
    lru_cache_evict_one(mgr->cache);
}
