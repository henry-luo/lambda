// lib/lru_cache.h - Generic string-keyed LRU cache.
//
// Composes hashmap.h with a doubly-linked list. Supports:
//   - max entry count and/or max byte budget
//   - optional TTL per entry (millisecond resolution)
//   - eviction callback invoked when an entry is evicted (capacity, TTL, or
//     explicit delete/clear)
//   - O(1) get / put / touch / delete
//
// Keys are NUL-terminated C strings, copied into the cache. Values are stored
// by pointer; ownership transfers to the cache when `put` succeeds and is
// returned via the eviction callback when removed.

#ifndef LIB_LRU_CACHE_H
#define LIB_LRU_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LruCache LruCache;

typedef void (*LruEvictFn)(const char* key, void* value, size_t bytes, void* udata);

typedef struct {
    size_t max_entries;       // 0 = unlimited
    size_t max_bytes;         // 0 = unlimited
    uint64_t default_ttl_ms;  // 0 = no TTL
    LruEvictFn on_evict;      // optional, NULL = caller responsible for value lifetime via peek/delete
    void* udata;
} LruCacheConfig;

LruCache* lru_cache_new(const LruCacheConfig* cfg);
void lru_cache_free(LruCache* cache);

// inserts or replaces. on replace, the old value is reported via on_evict.
// returns true on success; false on OOM (value is *not* taken on failure).
bool lru_cache_put(LruCache* cache, const char* key, void* value, size_t value_bytes);
bool lru_cache_put_ttl(LruCache* cache, const char* key, void* value, size_t value_bytes, uint64_t ttl_ms);

// gets and marks the entry as most-recently-used. NULL if absent or expired.
void* lru_cache_get(LruCache* cache, const char* key);

// gets without touching the LRU position. NULL if absent or expired.
void* lru_cache_peek(LruCache* cache, const char* key);

// marks an entry MRU without reading the value. returns false if absent.
bool lru_cache_touch(LruCache* cache, const char* key);

// removes an entry. on_evict is called with the value. returns true if found.
bool lru_cache_delete(LruCache* cache, const char* key);

// removes all entries, calling on_evict for each.
void lru_cache_clear(LruCache* cache);

// evicts the single least-recently-used entry. returns bytes freed (0 if empty).
size_t lru_cache_evict_one(LruCache* cache);

size_t lru_cache_count(const LruCache* cache);
size_t lru_cache_bytes(const LruCache* cache);

// iterate from MRU to LRU. iter returns false to stop. returns true if fully iterated.
typedef bool (*LruIterFn)(const char* key, void* value, size_t bytes, void* udata);
bool lru_cache_iter(LruCache* cache, LruIterFn iter, void* udata);

#ifdef __cplusplus
}
#endif

#endif
