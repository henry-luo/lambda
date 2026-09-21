// enhanced_file_cache.cpp
// Shared-LRU implementation with HTTP cache policy

#include "enhanced_file_cache.h"
#include "../../lib/file_utils.h"
#include "../../lib/file.h"
#include "../../lib/log.h"
#include "../../lib/str.h"
#include "../../lib/mem.h"
#include "../../lib/hex.h"
#include "../../lib/digest.h"
#include <string.h>
#include <errno.h>

// compute sha-256 hash through the shared digest facade
static void compute_sha256(const char* input, unsigned char* output) {
    if (!digest_sha256(input ? input : "", input ? strlen(input) : 0, output)) {
        memset(output, 0, 32);
        log_error("cache: sha256 digest failed");
    }
}

// convert hash to hex (delegates to lib/hex.h; caller frees the result)
static char* sha256_to_hex(const unsigned char* hash) {
    char* hex = (char*)mem_alloc(65, MEM_CAT_NETWORK);
    if (!hex) return NULL;
    hex_encode(hash, 32, hex);
    return hex;
}

static void cache_entry_evict(const char* url, void* value, size_t bytes, void* udata) {
    (void)bytes;
    CacheMetadata* meta = (CacheMetadata*)value;
    if (!meta) return;
    log_debug("cache: removing entry: %s", url);
    EnhancedFileCache* cache = (EnhancedFileCache*)udata;
    if ((!cache || cache->remove_files_on_evict) && meta->cache_path) {
        file_delete(meta->cache_path);
        if (meta->legacy_cache_entry) {
            char* key_path = file_cache_key_path(meta->cache_path);
            if (key_path) {
                file_delete(key_path);
                mem_free(key_path);
            }
        }
    }
    mem_free(meta->cache_path);
    mem_free(meta->etag);
    mem_free(meta);
}

static bool enhanced_cache_evict_lru_locked(EnhancedFileCache* cache) {
    if (!cache || lru_cache_count(cache->entries) == 0) return false;
    lru_cache_evict_one(cache->entries);
    return true;
}

static void enhanced_cache_make_space_locked(EnhancedFileCache* cache,
                                             CacheMetadata* replacement,
                                             size_t size) {
    size_t retained_bytes = lru_cache_bytes(cache->entries) -
        (replacement ? replacement->content_size : 0);
    while ((!replacement && cache->max_entries > 0 &&
            lru_cache_count(cache->entries) >= (size_t)cache->max_entries) ||
           (cache->max_size_bytes > 0 &&
            (retained_bytes > cache->max_size_bytes ||
             size > cache->max_size_bytes - retained_bytes) &&
            lru_cache_count(cache->entries) > (replacement ? 1u : 0u))) {
        if (!enhanced_cache_evict_lru_locked(cache)) break;
        retained_bytes = lru_cache_bytes(cache->entries) -
            (replacement ? replacement->content_size : 0);
    }
}

static CacheMetadata* enhanced_cache_adopt_legacy_entry_locked(EnhancedFileCache* cache,
                                                                 const char* url) {
    char* legacy_path = file_cache_path(url, cache->cache_dir, ".cache");
    if (!legacy_path) return NULL;

    // The old eight-hex-name cache can collide.  Only a sidecar written with
    // the response proves that this payload belongs to this URL.
    int64_t legacy_size = file_size(legacy_path);
    if (legacy_size < 0 || !file_cache_url_entry_matches(legacy_path, url)) {
        mem_free(legacy_path);
        return NULL;
    }

    enhanced_cache_make_space_locked(cache, NULL, (size_t)legacy_size);
    CacheMetadata* meta = (CacheMetadata*)mem_calloc(1, sizeof(CacheMetadata), MEM_CAT_NETWORK);
    if (!meta) {
        mem_free(legacy_path);
        return NULL;
    }
    meta->cache_path = legacy_path;
    meta->content_size = (size_t)legacy_size;
    meta->created_at = time(NULL);
    meta->last_accessed = meta->created_at;
    meta->legacy_cache_entry = true;
    if (!lru_cache_put(cache->entries, url, meta, meta->content_size)) {
        mem_free(meta->cache_path);
        mem_free(meta);
        return NULL;
    }

    // The enhanced index now owns eviction of this verified legacy payload.
    log_debug("cache: adopted legacy entry for %s -> %s", url, legacy_path);
    return meta;
}

static bool cache_metadata_expired(const CacheMetadata* meta, time_t now) {
    return meta && meta->expires > 0 && meta->expires < now;
}

static bool cache_metadata_is_expired(const char* key, void* value,
                                      size_t bytes, void* user_data) {
    (void)key;
    (void)bytes;
    return cache_metadata_expired((const CacheMetadata*)value, *(const time_t*)user_data);
}

static void cache_metadata_apply_headers(CacheMetadata* meta,
                                         const HttpCacheHeaders* headers) {
    if (!meta || !headers) return;
    mem_free(meta->etag);
    meta->etag = headers->etag ? mem_strdup(headers->etag, MEM_CAT_NETWORK) : NULL;
    meta->expires = headers->expires > 0 ? headers->expires :
        (headers->max_age > 0 ? time(NULL) + headers->max_age : 0);
}

static bool cache_acquire_write_slot(EnhancedFileCache* cache, bool wait_for_slot) {
    if (!cache) return false;

    pthread_mutex_lock(&cache->write_mutex);
    int max_writes = cache->max_concurrent_writes > 0 ? cache->max_concurrent_writes : 1;
    while (cache->active_writes >= max_writes) {
        if (!wait_for_slot) {
            cache->skipped_write_count++;
            pthread_mutex_unlock(&cache->write_mutex);
            return false;
        }
        pthread_cond_wait(&cache->write_cond, &cache->write_mutex);
    }
    cache->active_writes++;
    pthread_mutex_unlock(&cache->write_mutex);
    return true;
}

static void cache_release_write_slot(EnhancedFileCache* cache) {
    if (!cache) return;

    pthread_mutex_lock(&cache->write_mutex);
    if (cache->active_writes > 0) cache->active_writes--;
    pthread_cond_signal(&cache->write_cond);
    pthread_mutex_unlock(&cache->write_mutex);
}

// create cache
EnhancedFileCache* enhanced_cache_create(const char* cache_dir, size_t max_size, int max_entries) {
    EnhancedFileCache* cache = (EnhancedFileCache*)mem_calloc(1, sizeof(EnhancedFileCache), MEM_CAT_NETWORK);
    if (!cache) return NULL;

    cache->cache_dir = mem_strdup(cache_dir ? cache_dir : "./temp/radiant_cache", MEM_CAT_NETWORK);
    cache->max_size_bytes = max_size;
    cache->max_entries = max_entries > 0 ? max_entries : 10000;
    cache->max_concurrent_writes = 2;
    cache->remove_files_on_evict = true;
    LruCacheConfig config = {};
    config.on_evict = cache_entry_evict;
    config.udata = cache;
    cache->entries = lru_cache_new(&config);
    if (!cache->cache_dir || !cache->entries) {
        lru_cache_free(cache->entries);
        mem_free(cache->cache_dir);
        mem_free(cache);
        return NULL;
    }

    create_dir_recursive(cache->cache_dir);
    pthread_rwlock_init(&cache->rwlock, NULL);
    pthread_mutex_init(&cache->write_mutex, NULL);
    pthread_cond_init(&cache->write_cond, NULL);

    log_debug("cache: created at %s (max_size=%zu, max_entries=%d)",
              cache->cache_dir, max_size, cache->max_entries);

    return cache;
}

void enhanced_cache_destroy(EnhancedFileCache* cache) {
    if (!cache) return;

    pthread_rwlock_wrlock(&cache->rwlock);
    cache->remove_files_on_evict = false;
    lru_cache_free(cache->entries);
    cache->entries = NULL;
    pthread_rwlock_unlock(&cache->rwlock);

    pthread_rwlock_destroy(&cache->rwlock);
    pthread_mutex_destroy(&cache->write_mutex);
    pthread_cond_destroy(&cache->write_cond);

    mem_free(cache->cache_dir);
    mem_free(cache);

    log_debug("cache: destroyed");
}

char* enhanced_cache_lookup(EnhancedFileCache* cache, const char* url) {
    if (!cache || !url) return NULL;

    pthread_rwlock_wrlock(&cache->rwlock);
    CacheMetadata* meta = (CacheMetadata*)lru_cache_get(cache->entries, url);
    if (!meta) meta = enhanced_cache_adopt_legacy_entry_locked(cache, url);
    bool valid = meta && meta->cache_path && file_exists(meta->cache_path) &&
        !cache_metadata_expired(meta, time(NULL));
    char* result = valid ? mem_strdup(meta->cache_path, MEM_CAT_NETWORK) : NULL;
    if (valid && result) cache->hit_count++;
    else {
        if (meta && (!meta->cache_path || !file_exists(meta->cache_path) ||
                     cache_metadata_expired(meta, time(NULL)))) {
            lru_cache_delete(cache->entries, url);
        }
        cache->miss_count++;
    }
    pthread_rwlock_unlock(&cache->rwlock);
    if (result) log_debug("cache: hit for %s -> %s", url, result);
    else log_debug("cache: miss for %s", url);
    return result;
}

static char* enhanced_cache_store_impl(EnhancedFileCache* cache, const char* url,
                                       const char* content, size_t size,
                                       const HttpCacheHeaders* headers) {
    if (!cache || !url || !content) return NULL;

    pthread_rwlock_wrlock(&cache->rwlock);

    // Touch a replacement before eviction so its metadata stays live while
    // capacity pressure removes older entries.
    CacheMetadata* meta = (CacheMetadata*)lru_cache_get(cache->entries, url);
    enhanced_cache_make_space_locked(cache, meta, size);

    // compute hash for filename
    unsigned char hash[32];
    compute_sha256(url, hash);
    char* hex = sha256_to_hex(hash);
    if (!hex) {
        pthread_rwlock_unlock(&cache->rwlock);
        return NULL;
    }

    // create path: cache_dir/AB/ABCDEF...cache
    size_t path_cap = strlen(cache->cache_dir) + 80;
    char* path = (char*)mem_alloc(path_cap, MEM_CAT_NETWORK);
    if (!path) {
        mem_free(hex);
        pthread_rwlock_unlock(&cache->rwlock);
        return NULL;
    }
    str_fmt(path, path_cap, "%s/%c%c/%s.cache", cache->cache_dir, hex[0], hex[1], hex);

    // create subdirectory
    char dir_path[512];
    snprintf(dir_path, sizeof(dir_path), "%s/%c%c", cache->cache_dir, hex[0], hex[1]);
    create_dir_recursive(dir_path);

    // write file
    FILE* f = fopen(path, "wb");
    if (!f) {
        log_error("cache: failed to write %s: %s", path, strerror(errno));
        mem_free(hex);
        mem_free(path);
        pthread_rwlock_unlock(&cache->rwlock);
        return NULL;
    }

    fwrite(content, 1, size, f);
    fclose(f);

    if (meta) {
        char* replacement_path = mem_strdup(path, MEM_CAT_NETWORK);
        if (!replacement_path) {
            mem_free(hex);
            mem_free(path);
            pthread_rwlock_unlock(&cache->rwlock);
            return NULL;
        }
        mem_free(meta->cache_path);
        meta->cache_path = replacement_path;
        meta->content_size = size;
        meta->last_accessed = time(NULL);
        cache_metadata_apply_headers(meta, headers);
        if (!meta->cache_path || !lru_cache_put(cache->entries, url, meta, size)) {
            mem_free(hex);
            mem_free(path);
            pthread_rwlock_unlock(&cache->rwlock);
            return NULL;
        }
        log_debug("cache: updated %s (%zu bytes) -> %s", url, size, path);
    } else {
        meta = (CacheMetadata*)mem_calloc(1, sizeof(CacheMetadata), MEM_CAT_NETWORK);
        if (!meta) {
            mem_free(hex);
            mem_free(path);
            pthread_rwlock_unlock(&cache->rwlock);
            return NULL;
        }
        meta->cache_path = mem_strdup(path, MEM_CAT_NETWORK);
        meta->content_size = size;
        meta->created_at = time(NULL);
        meta->last_accessed = time(NULL);
        cache_metadata_apply_headers(meta, headers);
        if (!meta->cache_path || !lru_cache_put(cache->entries, url, meta, size)) {
            cache_entry_evict(url, meta, size, NULL);
            mem_free(hex);
            mem_free(path);
            pthread_rwlock_unlock(&cache->rwlock);
            return NULL;
        }
        log_debug("cache: stored %s (%zu bytes) -> %s", url, size, path);
    }

    mem_free(hex);
    pthread_rwlock_unlock(&cache->rwlock);

    return path;
}

char* enhanced_cache_store(EnhancedFileCache* cache, const char* url,
                           const char* content, size_t size,
                           const HttpCacheHeaders* headers) {
    if (!cache_acquire_write_slot(cache, true)) return NULL;
    char* path = enhanced_cache_store_impl(cache, url, content, size, headers);
    cache_release_write_slot(cache);
    return path;
}

char* enhanced_cache_try_store(EnhancedFileCache* cache, const char* url,
                               const char* content, size_t size,
                               const HttpCacheHeaders* headers) {
    if (!cache_acquire_write_slot(cache, false)) {
        log_debug("cache: skipped bounded cache write for %s", url ? url : "(null)");
        return NULL;
    }
    char* path = enhanced_cache_store_impl(cache, url, content, size, headers);
    cache_release_write_slot(cache);
    return path;
}

void enhanced_cache_set_max_concurrent_writes(EnhancedFileCache* cache, int max_writes) {
    if (!cache) return;

    pthread_mutex_lock(&cache->write_mutex);
    cache->max_concurrent_writes = max_writes > 0 ? max_writes : 1;
    pthread_cond_broadcast(&cache->write_cond);
    pthread_mutex_unlock(&cache->write_mutex);

    log_debug("cache: max concurrent writes set to %d", cache->max_concurrent_writes);
}

void enhanced_cache_evict_lru(EnhancedFileCache* cache) {
    if (!cache) return;

    pthread_rwlock_wrlock(&cache->rwlock);

    enhanced_cache_evict_lru_locked(cache);

    pthread_rwlock_unlock(&cache->rwlock);
}

void enhanced_cache_evict_expired(EnhancedFileCache* cache) {
    if (!cache) return;

    pthread_rwlock_wrlock(&cache->rwlock);

    time_t now = time(NULL);
    size_t evicted = lru_cache_remove_if(cache->entries,
        cache_metadata_is_expired, &now);

    pthread_rwlock_unlock(&cache->rwlock);

    if (evicted > 0) {
        log_debug("cache: removed %zu expired entries", evicted);
    }
}

void enhanced_cache_clear(EnhancedFileCache* cache) {
    if (!cache) return;

    pthread_rwlock_wrlock(&cache->rwlock);

    log_debug("cache: clearing all %zu entries", lru_cache_count(cache->entries));
    lru_cache_clear(cache->entries);

    pthread_rwlock_unlock(&cache->rwlock);
}

size_t enhanced_cache_get_size(const EnhancedFileCache* cache) {
    return cache ? lru_cache_bytes(cache->entries) : 0;
}

int enhanced_cache_get_entry_count(const EnhancedFileCache* cache) {
    return cache ? (int)lru_cache_count(cache->entries) : 0;
}

float enhanced_cache_get_hit_rate(const EnhancedFileCache* cache) {
    if (!cache) return 0.0f;
    int total = cache->hit_count + cache->miss_count;
    return total > 0 ? (float)cache->hit_count / total : 0.0f;
}

bool enhanced_cache_is_valid(EnhancedFileCache* cache, const char* url) {
    if (!cache || !url) return false;

    pthread_rwlock_rdlock(&cache->rwlock);

    CacheMetadata* meta = (CacheMetadata*)lru_cache_peek(cache->entries, url);
    bool valid = false;
    if (meta && meta->cache_path && file_exists(meta->cache_path) &&
        !cache_metadata_expired(meta, time(NULL))) {
        valid = true;
    }

    pthread_rwlock_unlock(&cache->rwlock);
    return valid;
}

bool enhanced_cache_is_expired(EnhancedFileCache* cache, const char* url) {
    if (!cache || !url) return true;

    pthread_rwlock_rdlock(&cache->rwlock);

    CacheMetadata* meta = (CacheMetadata*)lru_cache_peek(cache->entries, url);
    bool expired = true;
    if (meta) {
        if (meta->expires == 0) {
            // no expiration set, consider valid
            expired = false;
        } else if (meta->expires >= time(NULL)) {
            expired = false;
        }
    }

    pthread_rwlock_unlock(&cache->rwlock);
    return expired;
}
