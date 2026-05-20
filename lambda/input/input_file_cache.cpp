// input_file_cache.cpp
// Filesystem cache for downloaded/network files, backed by lib/lru_cache.

#include "input.hpp"
#include "../../lib/mem.h"
#include "../../lib/file.h"
#include "../../lib/lru_cache.h"

#include <stdint.h>
#include <string.h>
#include <time.h>

typedef struct FileCacheEntry {
    char* cache_path;
    time_t created_at;
    time_t last_accessed;
    size_t file_size;
} FileCacheEntry;

typedef struct FileCacheManager {
    char* cache_directory;
    LruCache* cache;
} FileCacheManager;

static void file_cache_evict_fn(const char* key, void* value, size_t bytes, void* udata) {
    (void)key; (void)bytes; (void)udata;
    FileCacheEntry* entry = (FileCacheEntry*)value;
    if (!entry) return;
    if (entry->cache_path) mem_free(entry->cache_path);
    mem_free(entry);
}

FileCacheManager* file_cache_manager_create(const char* cache_dir, size_t max_size, int max_entries) {
    FileCacheManager* mgr = (FileCacheManager*)mem_calloc(1, sizeof(FileCacheManager), MEM_CAT_INPUT_OTHER);
    if (!mgr) return NULL;
    if (cache_dir) {
        mgr->cache_directory = mem_strdup(cache_dir, MEM_CAT_INPUT_OTHER);
    }
    LruCacheConfig cfg = {};
    cfg.max_entries = max_entries > 0 ? (size_t)max_entries : 0;
    cfg.max_bytes = max_size;
    cfg.on_evict = file_cache_evict_fn;
    mgr->cache = lru_cache_new(&cfg);
    if (!mgr->cache) {
        if (mgr->cache_directory) mem_free(mgr->cache_directory);
        mem_free(mgr);
        return NULL;
    }
    return mgr;
}

void file_cache_manager_destroy(FileCacheManager* mgr) {
    if (!mgr) return;
    if (mgr->cache) lru_cache_free(mgr->cache);
    if (mgr->cache_directory) mem_free(mgr->cache_directory);
    mem_free(mgr);
}

char* file_cache_lookup(FileCacheManager* mgr, const char* url_key) {
    if (!mgr || !url_key) return NULL;
    FileCacheEntry* entry = (FileCacheEntry*)lru_cache_get(mgr->cache, url_key);
    if (!entry) return NULL;
    entry->last_accessed = time(NULL);
    return entry->cache_path;
}

void file_cache_add(FileCacheManager* mgr, const char* url_key, const char* file_path, size_t file_size) {
    if (!mgr || !url_key || !file_path) return;
    FileCacheEntry* entry = (FileCacheEntry*)mem_calloc(1, sizeof(FileCacheEntry), MEM_CAT_INPUT_OTHER);
    if (!entry) return;
    entry->cache_path = mem_strdup(file_path, MEM_CAT_INPUT_OTHER);
    if (!entry->cache_path) { mem_free(entry); return; }
    entry->created_at = entry->last_accessed = time(NULL);
    entry->file_size = file_size;
    if (!lru_cache_put(mgr->cache, url_key, entry, file_size)) {
        mem_free(entry->cache_path);
        mem_free(entry);
    }
}

void file_cache_evict(FileCacheManager* mgr) {
    if (!mgr) return;
    lru_cache_evict_one(mgr->cache);
}
