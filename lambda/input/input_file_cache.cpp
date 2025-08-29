// input_file_cache.cpp
// Filesystem cache for downloaded/network files
// Implements content-addressable storage and cache aging

#include "input.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

// Compute SHA-256 hash for cache key
typedef struct FileCacheEntry {
    char* cache_path;
    char* url_key;
    time_t created_at;
    time_t last_accessed;
    size_t file_size;
    struct FileCacheEntry* prev;
    struct FileCacheEntry* next;
} FileCacheEntry;

// File cache manager
typedef struct FileCacheManager {
    char* cache_directory;
    size_t max_cache_size;
    size_t current_cache_size;
    int max_entries;
    int entry_count;
    // ...hashmap for fast lookup...
} FileCacheManager;


// Create file cache manager
FileCacheManager* file_cache_manager_create(const char* cache_dir, size_t max_size, int max_entries) {
    FileCacheManager* mgr = (FileCacheManager*)calloc(1, sizeof(FileCacheManager));
    if (cache_dir) {
        mgr->cache_directory = strdup(cache_dir);
    }
    mgr->max_cache_size = max_size;
    mgr->max_entries = max_entries;
    mgr->entry_count = 0;
    mgr->current_cache_size = 0;
    // TODO: initialize hashmap
    return mgr;
}

// Destroy file cache manager
void file_cache_manager_destroy(FileCacheManager* mgr) {
    if (!mgr) return;
    if (mgr->cache_directory) free(mgr->cache_directory);
    // TODO: free all entries and hashmap
    free(mgr);
}

// Lookup/add/evict
char* file_cache_lookup(FileCacheManager* mgr, const char* url_key);
void file_cache_add(FileCacheManager* mgr, const char* url_key, const char* file_path, size_t file_size);
void file_cache_evict(FileCacheManager* mgr);
