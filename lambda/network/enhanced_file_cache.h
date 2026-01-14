// enhanced_file_cache.h
// Thread-safe file cache with LRU eviction and HTTP cache header support
// Content-addressable storage using SHA-256 keys

#ifndef ENHANCED_FILE_CACHE_H
#define ENHANCED_FILE_CACHE_H

#include <stddef.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// HTTP cache headers for cache control
typedef struct HttpCacheHeaders {
    char* etag;              // ETag header value
    time_t max_age;          // Cache-Control max-age (seconds from now)
    time_t expires;          // Expires header (absolute timestamp)
    char* last_modified;     // Last-Modified header value
    bool no_cache;           // Cache-Control: no-cache
    bool no_store;           // Cache-Control: no-store
} HttpCacheHeaders;

// Cache metadata for a single entry
typedef struct CacheMetadata {
    char* url;               // Original URL (key)
    char* cache_path;        // Path to cached file
    char* etag;              // HTTP ETag
    time_t expires;          // Expiration timestamp
    time_t last_modified;    // Last-Modified timestamp
    size_t content_size;     // Size in bytes
    time_t last_accessed;    // Last access time (for LRU)
    time_t created_at;       // Creation timestamp
    
    // LRU list pointers
    struct CacheMetadata* lru_prev;
    struct CacheMetadata* lru_next;
} CacheMetadata;

// Enhanced file cache manager
typedef struct EnhancedFileCache {
    char* cache_dir;              // Cache directory path
    size_t max_size_bytes;        // Maximum total cache size
    size_t current_size_bytes;    // Current total size
    int max_entries;              // Maximum number of entries
    int entry_count;              // Current entry count
    
    void* metadata_map;           // HashMap: URL → CacheMetadata*
    CacheMetadata* lru_head;      // LRU list head (most recent)
    CacheMetadata* lru_tail;      // LRU list tail (least recent)
    
    pthread_rwlock_t rwlock;      // Read-write lock for thread safety
    
    // Statistics
    int hit_count;
    int miss_count;
} EnhancedFileCache;

// Create and destroy cache
EnhancedFileCache* enhanced_cache_create(const char* cache_dir, 
                                         size_t max_size_bytes, 
                                         int max_entries);
void enhanced_cache_destroy(EnhancedFileCache* cache);

// Lookup and store operations (thread-safe)
char* enhanced_cache_lookup(EnhancedFileCache* cache, const char* url);
char* enhanced_cache_store(EnhancedFileCache* cache, 
                           const char* url, 
                           const char* content, 
                           size_t size,
                           const HttpCacheHeaders* headers);

// Eviction operations
void enhanced_cache_evict_lru(EnhancedFileCache* cache);
void enhanced_cache_evict_expired(EnhancedFileCache* cache);
void enhanced_cache_clear(EnhancedFileCache* cache);

// Statistics
size_t enhanced_cache_get_size(const EnhancedFileCache* cache);
int enhanced_cache_get_entry_count(const EnhancedFileCache* cache);
float enhanced_cache_get_hit_rate(const EnhancedFileCache* cache);

// Cache validation
bool enhanced_cache_is_valid(EnhancedFileCache* cache, const char* url);
bool enhanced_cache_is_expired(EnhancedFileCache* cache, const char* url);

#ifdef __cplusplus
}
#endif

#endif // ENHANCED_FILE_CACHE_H
