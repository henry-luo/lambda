// lib/lru_cache.c - see lru_cache.h
//
// Implementation notes:
//   - The hashmap stores `LruNode*` indirection records (sizeof(void*)), so
//     reorganising the LRU list is O(1) and doesn't require moving entries
//     inside the hashmap.
//   - The hashmap is keyed by a NUL-terminated key string owned by the node.
//   - Expired (TTL) entries are evicted lazily on get/peek/touch and eagerly
//     during eviction sweeps in put().

#include "lru_cache.h"
#include "hashmap.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct LruNode {
    char* key;
    void* value;
    size_t bytes;
    uint64_t expires_at_ms;   // 0 = no TTL
    struct LruNode* prev;     // toward MRU
    struct LruNode* next;     // toward LRU
} LruNode;

typedef struct {
    LruNode* node;            // hashmap stores pointer-to-node; key is node->key
} LruRecord;

struct LruCache {
    struct hashmap* map;
    LruNode* head;            // MRU
    LruNode* tail;            // LRU
    size_t count;
    size_t bytes;
    LruCacheConfig cfg;
};

static uint64_t lru_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
}

static uint64_t lru_rec_hash(const void* item, uint64_t s0, uint64_t s1) {
    const LruRecord* r = (const LruRecord*)item;
    return hashmap_sip(r->node->key, strlen(r->node->key), s0, s1);
}

static int lru_rec_cmp(const void* a, const void* b, void* udata) {
    (void)udata;
    const LruRecord* ra = (const LruRecord*)a;
    const LruRecord* rb = (const LruRecord*)b;
    return strcmp(ra->node->key, rb->node->key);
}

LruCache* lru_cache_new(const LruCacheConfig* cfg) {
    LruCache* c = (LruCache*)calloc(1, sizeof(LruCache));
    if (!c) return NULL;
    if (cfg) c->cfg = *cfg;
    c->map = hashmap_new(sizeof(LruRecord), 0, 0, 0, lru_rec_hash, lru_rec_cmp, NULL, NULL);
    if (!c->map) { free(c); return NULL; }
    return c;
}

static void lru_unlink(LruCache* c, LruNode* n) {
    if (n->prev) n->prev->next = n->next; else c->head = n->next;
    if (n->next) n->next->prev = n->prev; else c->tail = n->prev;
    n->prev = n->next = NULL;
}

static void lru_push_front(LruCache* c, LruNode* n) {
    n->prev = NULL;
    n->next = c->head;
    if (c->head) c->head->prev = n;
    c->head = n;
    if (!c->tail) c->tail = n;
}

// remove from map + list. invokes on_evict. frees the node and its key.
static void lru_destroy_node(LruCache* c, LruNode* n) {
    lru_unlink(c, n);
    LruRecord probe = { .node = n };
    hashmap_delete(c->map, &probe);
    c->count--;
    c->bytes -= n->bytes;
    if (c->cfg.on_evict) c->cfg.on_evict(n->key, n->value, n->bytes, c->cfg.udata);
    free(n->key);
    free(n);
}

static bool lru_is_expired(const LruNode* n, uint64_t now_ms) {
    return n->expires_at_ms != 0 && n->expires_at_ms <= now_ms;
}

// returns the node if present and not expired; otherwise removes the stale
// entry and returns NULL.
static LruNode* lru_find_live(LruCache* c, const char* key) {
    LruNode probe_node = { .key = (char*)key };
    LruRecord probe = { .node = &probe_node };
    const LruRecord* r = (const LruRecord*)hashmap_get(c->map, &probe);
    if (!r) return NULL;
    if (c->cfg.default_ttl_ms || r->node->expires_at_ms) {
        if (lru_is_expired(r->node, lru_now_ms())) {
            lru_destroy_node(c, r->node);
            return NULL;
        }
    }
    return r->node;
}

static void lru_enforce_caps(LruCache* c) {
    while (c->tail &&
           ((c->cfg.max_entries && c->count > c->cfg.max_entries) ||
            (c->cfg.max_bytes && c->bytes > c->cfg.max_bytes))) {
        lru_destroy_node(c, c->tail);
    }
}

bool lru_cache_put_ttl(LruCache* c, const char* key, void* value, size_t value_bytes, uint64_t ttl_ms) {
    if (!c || !key) return false;

    LruNode probe_node = { .key = (char*)key };
    LruRecord probe = { .node = &probe_node };
    const LruRecord* existing = (const LruRecord*)hashmap_get(c->map, &probe);
    if (existing) {
        LruNode* n = existing->node;
        if (c->cfg.on_evict && n->value != value) {
            c->cfg.on_evict(n->key, n->value, n->bytes, c->cfg.udata);
        }
        c->bytes -= n->bytes;
        n->value = value;
        n->bytes = value_bytes;
        n->expires_at_ms = ttl_ms ? (lru_now_ms() + ttl_ms) : 0;
        c->bytes += value_bytes;
        lru_unlink(c, n);
        lru_push_front(c, n);
        lru_enforce_caps(c);
        return true;
    }

    LruNode* n = (LruNode*)calloc(1, sizeof(LruNode));
    if (!n) return false;
    n->key = strdup(key);
    if (!n->key) { free(n); return false; }
    n->value = value;
    n->bytes = value_bytes;
    uint64_t ttl = ttl_ms ? ttl_ms : c->cfg.default_ttl_ms;
    n->expires_at_ms = ttl ? (lru_now_ms() + ttl) : 0;

    LruRecord rec = { .node = n };
    if (!hashmap_set(c->map, &rec) && hashmap_oom(c->map)) {
        free(n->key); free(n);
        return false;
    }
    lru_push_front(c, n);
    c->count++;
    c->bytes += value_bytes;
    lru_enforce_caps(c);
    return true;
}

bool lru_cache_put(LruCache* c, const char* key, void* value, size_t value_bytes) {
    return lru_cache_put_ttl(c, key, value, value_bytes, 0);
}

void* lru_cache_get(LruCache* c, const char* key) {
    if (!c || !key) return NULL;
    LruNode* n = lru_find_live(c, key);
    if (!n) return NULL;
    lru_unlink(c, n);
    lru_push_front(c, n);
    return n->value;
}

void* lru_cache_peek(LruCache* c, const char* key) {
    if (!c || !key) return NULL;
    LruNode* n = lru_find_live(c, key);
    return n ? n->value : NULL;
}

bool lru_cache_touch(LruCache* c, const char* key) {
    if (!c || !key) return false;
    LruNode* n = lru_find_live(c, key);
    if (!n) return false;
    lru_unlink(c, n);
    lru_push_front(c, n);
    return true;
}

bool lru_cache_delete(LruCache* c, const char* key) {
    if (!c || !key) return false;
    LruNode probe_node = { .key = (char*)key };
    LruRecord probe = { .node = &probe_node };
    const LruRecord* r = (const LruRecord*)hashmap_get(c->map, &probe);
    if (!r) return false;
    lru_destroy_node(c, r->node);
    return true;
}

void lru_cache_clear(LruCache* c) {
    if (!c) return;
    while (c->head) lru_destroy_node(c, c->head);
}

size_t lru_cache_evict_one(LruCache* c) {
    if (!c || !c->tail) return 0;
    size_t freed = c->tail->bytes;
    lru_destroy_node(c, c->tail);
    return freed;
}

size_t lru_cache_count(const LruCache* c) { return c ? c->count : 0; }
size_t lru_cache_bytes(const LruCache* c) { return c ? c->bytes : 0; }

bool lru_cache_iter(LruCache* c, LruIterFn iter, void* udata) {
    if (!c || !iter) return true;
    for (LruNode* n = c->head; n; n = n->next) {
        if (!iter(n->key, n->value, n->bytes, udata)) return false;
    }
    return true;
}

void lru_cache_free(LruCache* c) {
    if (!c) return;
    lru_cache_clear(c);
    if (c->map) hashmap_free(c->map);
    free(c);
}
