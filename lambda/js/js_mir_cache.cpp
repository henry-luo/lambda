#include "js_transpiler.hpp"
#include "js_mir_context.hpp"

#include "../../lib/hashmap.h"
#include "../../lib/mem.h"
#include "../../lib/log.h"

#include <cstring>

struct JsMirCacheEntry {
    uint64_t source_hash;
    uint64_t preamble_abi_hash;
    JsMirCacheMode mode;
    char* source;
    size_t source_len;
    char* filename;
    size_t filename_len;
    JsPreambleState compiled;
};

struct JsMirCache {
    HashMap* entries;
    JsMirCacheStats stats;
};

static uint64_t js_mir_cache_mix(uint64_t hash, uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    return hash;
}

static uint64_t js_mir_cache_preamble_abi_hash(const JsPreambleState* preamble) {
    if (!preamble || !preamble->entries || preamble->entry_count <= 0) return 0;

    uint64_t hash = js_mir_cache_mix(0xcbf29ce484222325ULL,
                                     (uint64_t)preamble->module_var_count);
    for (int i = 0; i < preamble->entry_count; i++) {
        const JsModuleConstEntry* entry = &preamble->entries[i];
        size_t name_len = strnlen(entry->name, sizeof(entry->name));
        hash = js_mir_cache_mix(hash, hashmap_xxhash3(entry->name, name_len, 0, 0));
        hash = js_mir_cache_mix(hash, (uint64_t)entry->const_type);
        hash = js_mir_cache_mix(hash, (uint64_t)entry->int_val);
        hash = js_mir_cache_mix(hash, (uint64_t)entry->var_kind);
        hash = js_mir_cache_mix(hash, (uint64_t)entry->modvar_type);
        hash = js_mir_cache_mix(hash, entry->is_implicit_global ? 1 : 0);
        hash = js_mir_cache_mix(hash, entry->is_nested_func_hoist ? 1 : 0);
        hash = js_mir_cache_mix(hash, entry->is_iife_func_decl ? 1 : 0);
        hash = js_mir_cache_mix(hash, entry->annexb_suppressed ? 1 : 0);
        hash = js_mir_cache_mix(hash, entry->is_live_default_binding ? 1 : 0);
    }
    return hash;
}

static uint64_t js_mir_cache_entry_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const JsMirCacheEntry* entry = (const JsMirCacheEntry*)item;
    uint64_t hash = hashmap_xxhash3(entry->source, entry->source_len, seed0, seed1);
    hash = js_mir_cache_mix(hash, (uint64_t)entry->mode);
    hash = js_mir_cache_mix(hash, entry->preamble_abi_hash);
    hash = js_mir_cache_mix(hash,
        hashmap_xxhash3(entry->filename, entry->filename_len, seed0, seed1));
    return hash;
}

static int js_mir_cache_entry_compare(const void* left, const void* right, void* udata) {
    (void)udata;
    const JsMirCacheEntry* a = (const JsMirCacheEntry*)left;
    const JsMirCacheEntry* b = (const JsMirCacheEntry*)right;
    if (a->mode != b->mode) return a->mode < b->mode ? -1 : 1;
    if (a->preamble_abi_hash != b->preamble_abi_hash) {
        return a->preamble_abi_hash < b->preamble_abi_hash ? -1 : 1;
    }
    if (a->source_len != b->source_len) return a->source_len < b->source_len ? -1 : 1;
    if (a->filename_len != b->filename_len) {
        return a->filename_len < b->filename_len ? -1 : 1;
    }
    int filename_cmp = memcmp(a->filename, b->filename, a->filename_len);
    if (filename_cmp != 0) return filename_cmp;
    if (a->source_len == 0) return 0;
    return memcmp(a->source, b->source, a->source_len);
}

static void js_mir_cache_entry_free(void* item) {
    JsMirCacheEntry* entry = (JsMirCacheEntry*)item;
    preamble_state_destroy(&entry->compiled);
    mem_free(entry->source);
    mem_free(entry->filename);
    entry->source = nullptr;
    entry->filename = nullptr;
}

static char* js_mir_cache_copy_bytes(const char* value, size_t length) {
    char* copy = (char*)mem_alloc(length + 1, MEM_CAT_JS_RUNTIME);
    if (!copy) return nullptr;
    if (value && length > 0) memcpy(copy, value, length);
    copy[length] = '\0';
    return copy;
}

static JsMirCacheEntry js_mir_cache_make_probe(
    JsMirCacheMode mode, const char* source, size_t source_len,
    const char* filename, const JsPreambleState* preamble) {
    JsMirCacheEntry probe = {};
    probe.mode = mode;
    probe.source = (char*)(source ? source : "");
    probe.source_len = source ? source_len : 0;
    probe.filename = (char*)(filename ? filename : "<string>");
    probe.filename_len = strlen(probe.filename);
    probe.source_hash = hashmap_xxhash3(probe.source, probe.source_len, 0, 0);
    probe.preamble_abi_hash = js_mir_cache_preamble_abi_hash(preamble);
    return probe;
}

JsMirCache* js_mir_cache_create(void) {
    JsMirCache* cache = (JsMirCache*)mem_calloc(1, sizeof(JsMirCache), MEM_CAT_JS_RUNTIME);
    if (!cache) return nullptr;
    cache->entries = hashmap_new(sizeof(JsMirCacheEntry), 8, 0, 0,
        js_mir_cache_entry_hash, js_mir_cache_entry_compare,
        js_mir_cache_entry_free, nullptr);
    if (!cache->entries) {
        mem_free(cache);
        return nullptr;
    }
    return cache;
}

void js_mir_cache_destroy(JsMirCache* cache) {
    if (!cache) return;
    log_notice("js_mir_cache_summary: lookups=%llu hits=%llu misses=%llu compiles=%llu instantiations=%llu poisoned=%llu retained_entries=%zu retained_metadata_bytes=%zu",
        (unsigned long long)cache->stats.lookups,
        (unsigned long long)cache->stats.hits,
        (unsigned long long)cache->stats.misses,
        (unsigned long long)cache->stats.compiles,
        (unsigned long long)cache->stats.instantiations,
        (unsigned long long)cache->stats.poisoned,
        cache->stats.retained_entries,
        cache->stats.retained_metadata_bytes);
    hashmap_free(cache->entries);
    mem_free(cache);
}

const JsPreambleState* js_mir_cache_lookup(
    JsMirCache* cache, JsMirCacheMode mode,
    const char* source, size_t source_len, const char* filename,
    const JsPreambleState* preamble) {
    if (!cache || !cache->entries) return nullptr;
    JsMirCacheEntry probe = js_mir_cache_make_probe(
        mode, source, source_len, filename, preamble);
    cache->stats.lookups++;
    const JsMirCacheEntry* found = (const JsMirCacheEntry*)hashmap_get(cache->entries, &probe);
    if (!found || found->source_hash != probe.source_hash) {
        cache->stats.misses++;
        return nullptr;
    }
    cache->stats.hits++;
    return &found->compiled;
}

const JsPreambleState* js_mir_cache_adopt(
    JsMirCache* cache, JsMirCacheMode mode,
    const char* source, size_t source_len, const char* filename,
    const JsPreambleState* preamble, JsPreambleState* compiled_state) {
    if (!cache || !cache->entries || !compiled_state ||
        !compiled_state->owns_compiled_state || !compiled_state->mir_ctx) {
        return nullptr;
    }

    JsMirCacheEntry entry = js_mir_cache_make_probe(
        mode, source, source_len, filename, preamble);
    if (hashmap_get(cache->entries, &entry)) {
        // Cache mutation is single-threaded today. Refuse a duplicate adoption
        // so an artifact already referenced by an instance is never replaced.
        return nullptr;
    }
    entry.source = js_mir_cache_copy_bytes(source, source_len);
    entry.filename = js_mir_cache_copy_bytes(filename ? filename : "<string>",
                                              entry.filename_len);
    if (!entry.source || !entry.filename) {
        mem_free(entry.source);
        mem_free(entry.filename);
        return nullptr;
    }
    entry.compiled = *compiled_state;

    const JsMirCacheEntry* replaced =
        (const JsMirCacheEntry*)hashmap_set(cache->entries, &entry);
    if (replaced) {
        log_error("js_mir_cache_adopt: unexpected duplicate cache insertion");
        js_mir_cache_entry_free((void*)replaced);
        memset(compiled_state, 0, sizeof(*compiled_state));
        cache->stats.compiles++;
        const JsMirCacheEntry* stored =
            (const JsMirCacheEntry*)hashmap_get(cache->entries, &entry);
        return stored ? &stored->compiled : nullptr;
    }
    if (hashmap_oom(cache->entries)) {
        mem_free(entry.source);
        mem_free(entry.filename);
        return nullptr;
    }

    memset(compiled_state, 0, sizeof(*compiled_state));
    cache->stats.compiles++;
    cache->stats.retained_entries++;
    cache->stats.retained_metadata_bytes += sizeof(JsMirCacheEntry) + source_len + 1 +
        entry.filename_len + 1 +
        (size_t)entry.compiled.entry_count * sizeof(JsModuleConstEntry);

    const JsMirCacheEntry* stored =
        (const JsMirCacheEntry*)hashmap_get(cache->entries, &entry);
    return stored ? &stored->compiled : nullptr;
}

void js_mir_cache_record_instantiation(JsMirCache* cache) {
    if (cache) cache->stats.instantiations++;
}

void js_mir_cache_get_stats(const JsMirCache* cache, JsMirCacheStats* stats) {
    if (!stats) return;
    if (!cache) {
        memset(stats, 0, sizeof(*stats));
        return;
    }
    *stats = cache->stats;
}
