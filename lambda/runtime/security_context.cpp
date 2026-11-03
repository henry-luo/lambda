#include "../lambda-data.hpp"
#include "security_context.h"
#include "context_capsule.h"
#include "heap_api.h"
#include "lambda-error.h"
#include "../../lib/hashmap.h"
#include "../../lib/hashmap_helpers.h"
#include "../../lib/strbuf.h"
#include "../../lib/memtrack.h"
#include <string.h>

extern __thread EvalContext* context;

static const uint32_t security_capsule_owner = 0x53414645; // SAFE
static const ContextCapsuleOps security_capsule_ops = {
    "security-io", CONTEXT_CAPSULE_LIFETIME_CONTEXT, sizeof(SecurityIOClient), nullptr, nullptr, nullptr
};

const SecurityIOClient* eval_context_security_io(EvalContext* owner) {
    return (const SecurityIOClient*)context_capsule_extension(owner, security_capsule_owner, 1);
}

const SecurityIOClient* lambda_current_security_io(void) {
    return eval_context_security_io(context);
}

struct SecurityPathEntry {
    char* key;
    size_t length;
    uint64_t* root;
};
struct SecurityPathCache {
    EvalContext* owner;
    HashMap* entries;
    SecurityAuthorityStamp stamp;
};

static uint64_t security_path_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const SecurityPathEntry* entry = (const SecurityPathEntry*)item;
    return hashmap_hash_bytes(entry->key, entry->length, seed0, seed1);
}
static int security_path_compare(const void* a, const void* b, void*) {
    const SecurityPathEntry* left = (const SecurityPathEntry*)a;
    const SecurityPathEntry* right = (const SecurityPathEntry*)b;
    if (left->length != right->length) return left->length < right->length ? -1 : 1;
    return memcmp(left->key, right->key, left->length);
}
static void security_path_clear(void* data) {
    SecurityPathCache* cache = (SecurityPathCache*)data;
    size_t iter = 0;
    void* value;
    while (hashmap_iter(cache->entries, &iter, &value)) {
        SecurityPathEntry* entry = (SecurityPathEntry*)value;
        heap_unregister_gc_root_for(cache->owner, entry->root);
        mem_free(entry->root); mem_free(entry->key);
    }
    hashmap_clear(cache->entries, false);
    cache->stamp = {};
}
static void security_path_destroy(void* data) {
    SecurityPathCache* cache = (SecurityPathCache*)data;
    security_path_clear(cache);
    hashmap_free(cache->entries); mem_free(cache);
}
static bool security_stamp_equal(const SecurityAuthorityStamp* a, const SecurityAuthorityStamp* b) {
    return a->session == b->session && a->epoch == b->epoch;
}
static SecurityPathCache* security_path_cache(const SecurityAuthorityStamp* stamp) {
    if (!context || !stamp || !stamp->session || !stamp->epoch) return nullptr;
    SecurityPathCache* cache = (SecurityPathCache*)context_capsule_extension(context, security_capsule_owner, 2);
    if (!cache) {
        cache = (SecurityPathCache*)mem_calloc(1, sizeof(*cache), MEM_CAT_EVAL);
        if (!cache) return nullptr;
        cache->owner = context;
        cache->entries = hashmap_new(sizeof(SecurityPathEntry), 16, 0, 0,
            security_path_hash, security_path_compare, nullptr, nullptr);
        static const ContextCapsuleOps ops = {"security-path-snapshots", CONTEXT_CAPSULE_LIFETIME_REALM,
            0, nullptr, security_path_clear, security_path_destroy};
        if (!cache->entries || !context_capsule_extension_install(context,
                security_capsule_owner, 2, cache, &ops)) {
            if (cache->entries) hashmap_free(cache->entries);
            mem_free(cache); return nullptr;
        }
    }
    if (!security_stamp_equal(&cache->stamp, stamp)) {
        security_path_clear(cache);
        cache->stamp = *stamp;
    }
    return cache;
}

bool lambda_security_path_cache_get(Path* path, const SecurityAuthorityStamp* stamp, uint64_t* value) {
    SecurityPathCache* cache = security_path_cache(stamp);
    if (!cache || !path || !value) return false;
    StrBuf* key = strbuf_new();
    if (!key) return false;
    path_to_string(path, key);
    SecurityPathEntry query = {key->str, key->length, nullptr};
    const SecurityPathEntry* entry = (const SecurityPathEntry*)hashmap_get(cache->entries, &query);
    if (entry) *value = *entry->root;
    strbuf_free(key);
    return entry != nullptr;
}

bool lambda_security_path_cache_store(Path* path, const SecurityAuthorityStamp* stamp, uint64_t value) {
    if (!path || !stamp) { set_runtime_error(ERR_INVALID_STATE, "invalid path snapshot request"); return false; }
    SecurityAuthorityStamp current;
    if (!security_io_authority_stamp(lambda_current_security_io(), &current, nullptr) ||
            !security_stamp_equal(stamp, &current)) {
        set_runtime_error(ERR_FILE_ACCESS_DENIED, "path authority changed during acquisition");
        return false;
    }
    SecurityPathCache* cache = security_path_cache(stamp);
    if (!cache || !path) { set_runtime_error(ERR_OUT_OF_MEMORY, "path snapshot cache allocation failed"); return false; }
    StrBuf* key = strbuf_new();
    if (!key) { set_runtime_error(ERR_OUT_OF_MEMORY, "path snapshot key allocation failed"); return false; }
    path_to_string(path, key);
    SecurityPathEntry query = {key->str, key->length, nullptr};
    const SecurityPathEntry* existing = (const SecurityPathEntry*)hashmap_get(cache->entries, &query);
    if (existing) { *existing->root = value; strbuf_free(key); return true; }
    if (hashmap_count(cache->entries) >= SECURITY_IO_DIRECTORY_LIMIT) {
        strbuf_free(key); set_runtime_error(ERR_IO_ERROR, "path snapshot cache entry limit exceeded"); return false;
    }
    SecurityPathEntry entry = {mem_strdup(key->str, MEM_CAT_EVAL), key->length,
        (uint64_t*)mem_alloc(sizeof(uint64_t), MEM_CAT_EVAL)};
    strbuf_free(key);
    bool registered = false;
    if (entry.root && entry.key) {
        *entry.root = value;
        registered = heap_register_gc_root_for(cache->owner, entry.root);
        if (registered) {
            hashmap_set(cache->entries, &entry);
            if (!hashmap_oom(cache->entries)) return true;
        }
    }
    if (registered) heap_unregister_gc_root_for(cache->owner, entry.root);
    mem_free(entry.root); mem_free(entry.key);
    set_runtime_error(ERR_OUT_OF_MEMORY, "path snapshot root allocation failed");
    return false;
}

bool eval_context_bind_security_io(EvalContext* owner, const SecurityIOClient* client) {
    if (!owner || !client || client->version != 1 || !client->read || !client->write ||
        eval_context_security_io(owner)) return false;
    SecurityIOClient* binding = (SecurityIOClient*)mem_alloc(sizeof(*binding), MEM_CAT_EVAL);
    if (!binding) return false;
    *binding = *client;
    if (context_capsule_extension_install(owner, security_capsule_owner, 1, binding, &security_capsule_ops)) return true;
    mem_free(binding);
    return false;
}
