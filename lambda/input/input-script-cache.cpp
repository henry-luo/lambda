#include "input-script-cache.h"
#include "input.hpp"

#include "../../lib/file.h"
#include "../../lib/hash.h"
#include "../../lib/hashmap_helpers.h"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/mem_factory.h"
#include "../../lib/shell.h"
#include "../../lib/arraylist.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct InputScriptArtifact {
    void* value;
    InputScriptArtifactOps ops;
    uint64_t key;
    size_t retained_bytes;
};

struct InputScriptBuildState {
    InputScriptBuildKind kind;
    uint64_t key;
    bool building;
    bool poisoned;
    pthread_cond_t completed;
};

struct ScriptInput {
    char* identity;
    char* source;
    size_t source_length;
    InputScriptSourceKind source_kind;
    char* language;
    char* profile;
    char* parser_abi;
    char* parse_flags;
    char* resolution_base;
    char* backend;
    char* execution_mode;
    bool module_mode;
    uint64_t source_hash;
    uint64_t source_key;
    uint32_t compilation_unit_id;
    bool retired;
    Pool* cache_pool;
    Arena* cache_arena;
    MemContext* cache_context;
    ArrayList* ast_artifacts;
    ArrayList* mir_artifacts;
    ArrayList* build_states;
    // These edges identify source records, never Runtime or heap state. They
    // let a changed dependency retire cached importer artifacts as one cone.
    ArrayList* dependencies;
    ArrayList* dependents;
    int lease_count;
    uint64_t last_access_epoch;
};

struct InputScriptMapEntry {
    ScriptInput* input;
};

struct InputScriptLease {
    InputScriptCache* cache;
    InputCacheScope* scope;
    ScriptInput* input;
    bool released;
    bool persistent;
    bool ast_build_claimed;
    bool mir_build_claimed;
    uint64_t ast_key;
    uint64_t mir_key;
};

struct InputCacheScope {
    InputScriptCache* cache;
    ArrayList* leases;
    bool closed;
};

struct InputScriptCache {
    HashMap* entries;
    pthread_mutex_t mutex;
    InputScriptCachePolicy policy;
    bool mir_disabled_by_alias;
    size_t retention_limit_bytes;
    uint32_t next_compilation_unit_id;
    uint64_t next_access_epoch;
    InputScriptCacheStats stats;
    ArrayList* retired_inputs;
};

static const char* cache_text(const char* value, const char* fallback) {
    return value ? value : fallback;
}

static uint64_t cache_text_hash(const char* value) {
    const char* text = value ? value : "";
    return hashmap_hash_xxhash3_cstr(text, 0, 0);
}

static uint64_t cache_source_key(const ScriptInput* input) {
    uint64_t hash = input->source_hash;
    hash = hash_combine_u64(hash, input->source_length);
    hash = hash_combine_u64(hash, (uint64_t)input->source_kind);
    hash = hash_combine_u64(hash, cache_text_hash(input->identity));
    hash = hash_combine_u64(hash, cache_text_hash(input->language));
    hash = hash_combine_u64(hash, cache_text_hash(input->profile));
    hash = hash_combine_u64(hash, cache_text_hash(input->parser_abi));
    hash = hash_combine_u64(hash, cache_text_hash(input->parse_flags));
    hash = hash_combine_u64(hash, cache_text_hash(input->resolution_base));
    hash = hash_combine_u64(hash, input->module_mode ? 1 : 0);
    return hash;
}

static uint64_t cache_ast_key(const ScriptInput* input, uint64_t ast_abi) {
    return hash_combine_u64(input->source_key, ast_abi);
}

static uint64_t cache_mir_key(const ScriptInput* input,
        const InputScriptRequest* request) {
    uint64_t hash = cache_ast_key(input, request->ast_abi);
    hash = hash_combine_u64(hash, request->compiler_abi);
    hash = hash_combine_u64(hash, request->interface_abi);
    hash = hash_combine_u64(hash, request->dependency_digest);
    hash = hash_combine_u64(hash, request->optimize_level);
    hash = hash_combine_u64(hash, request->module_mode ? 1 : 0);
    hash = hash_combine_u64(hash, cache_text_hash(request->backend));
    hash = hash_combine_u64(hash, cache_text_hash(request->execution_mode));
    return hash;
}

static bool cache_text_equal(const char* left, const char* right) {
    return strcmp(cache_text(left, ""), cache_text(right, "")) == 0;
}

static bool cache_key_equal(const ScriptInput* left, const ScriptInput* right) {
    if (!left || !right) return false;
    if (left->source_length != right->source_length ||
            left->source_kind != right->source_kind ||
            left->source_hash != right->source_hash) return false;
    if (!cache_text_equal(left->identity, right->identity) ||
            !cache_text_equal(left->language, right->language) ||
            !cache_text_equal(left->profile, right->profile) ||
            !cache_text_equal(left->parser_abi, right->parser_abi) ||
            !cache_text_equal(left->parse_flags, right->parse_flags) ||
            !cache_text_equal(left->resolution_base, right->resolution_base) ||
            left->module_mode != right->module_mode) {
        return false;
    }
    if (left->source_length == 0) return true;
    return memcmp(left->source, right->source, left->source_length) == 0;
}

static uint64_t cache_entry_hash(const void* item, uint64_t seed0,
        uint64_t seed1) {
    const InputScriptMapEntry* entry = (const InputScriptMapEntry*)item;
    const ScriptInput* input = entry ? entry->input : NULL;
    return input ? hash_combine_u64(input->source_key, seed0 ^ seed1) : 0;
}

static int cache_entry_compare(const void* left, const void* right,
        void* udata) {
    (void)udata;
    const InputScriptMapEntry* a = (const InputScriptMapEntry*)left;
    const InputScriptMapEntry* b = (const InputScriptMapEntry*)right;
    return cache_key_equal(a ? a->input : NULL, b ? b->input : NULL) ? 0 : 1;
}

static void cache_destroy_artifact(InputScriptArtifact* artifact) {
    if (!artifact || !artifact->value) return;
    if (artifact->ops.destroy) artifact->ops.destroy(artifact->value);
    artifact->value = NULL;
    artifact->retained_bytes = 0;
}

static void cache_destroy_build_state(InputScriptBuildState* state) {
    if (!state) return;
    pthread_cond_destroy(&state->completed);
    mem_free(state);
}

static bool cache_input_list_contains(const ArrayList* list,
        const ScriptInput* input) {
    if (!list || !input) return false;
    for (int i = 0; i < list->length; i++) {
        if (list->data[i] == input) return true;
    }
    return false;
}

static void cache_input_list_remove(ArrayList* list, const ScriptInput* input) {
    if (!list || !input) return;
    for (int i = 0; i < list->length; i++) {
        if (list->data[i] != input) continue;
        arraylist_remove(list, i);
        return;
    }
}

static void cache_unlink_input_dependencies(ScriptInput* input) {
    if (!input) return;
    if (input->dependencies) {
        for (int i = 0; i < input->dependencies->length; i++) {
            ScriptInput* dependency =
                (ScriptInput*)input->dependencies->data[i];
            if (dependency) cache_input_list_remove(dependency->dependents, input);
        }
        arraylist_free(input->dependencies);
        input->dependencies = NULL;
    }
    if (input->dependents) {
        for (int i = 0; i < input->dependents->length; i++) {
            ScriptInput* dependent = (ScriptInput*)input->dependents->data[i];
            if (dependent) cache_input_list_remove(dependent->dependencies, input);
        }
        arraylist_free(input->dependents);
        input->dependents = NULL;
    }
}

static void cache_destroy_input(ScriptInput* input) {
    if (!input) return;
    cache_unlink_input_dependencies(input);
    if (input->ast_artifacts) {
        for (int i = 0; i < input->ast_artifacts->length; i++) {
            InputScriptArtifact* artifact =
                (InputScriptArtifact*)input->ast_artifacts->data[i];
            cache_destroy_artifact(artifact);
            mem_free(artifact);
        }
        arraylist_free(input->ast_artifacts);
    }
    if (input->mir_artifacts) {
        for (int i = 0; i < input->mir_artifacts->length; i++) {
            InputScriptArtifact* artifact =
                (InputScriptArtifact*)input->mir_artifacts->data[i];
            cache_destroy_artifact(artifact);
            mem_free(artifact);
        }
        arraylist_free(input->mir_artifacts);
    }
    if (input->build_states) {
        for (int i = 0; i < input->build_states->length; i++) {
            cache_destroy_build_state((InputScriptBuildState*)
                input->build_states->data[i]);
        }
        arraylist_free(input->build_states);
    }
    if (input->cache_context) {
        // the context owns the explicitly named cache pool and arena.
        mem_context_destroy(input->cache_context);
    }
    mem_free(input->identity);
    mem_free(input->source);
    mem_free(input->language);
    mem_free(input->profile);
    mem_free(input->parser_abi);
    mem_free(input->parse_flags);
    mem_free(input->resolution_base);
    mem_free(input->backend);
    mem_free(input->execution_mode);
    mem_free(input);
}

static size_t cache_input_ast_bytes(const ScriptInput* input) {
    if (!input || !input->ast_artifacts) return 0;
    size_t bytes = 0;
    for (int i = 0; i < input->ast_artifacts->length; i++) {
        InputScriptArtifact* artifact =
            (InputScriptArtifact*)input->ast_artifacts->data[i];
        if (artifact) bytes += artifact->retained_bytes;
    }
    return bytes;
}

static size_t cache_input_mir_bytes(const ScriptInput* input) {
    if (!input || !input->mir_artifacts) return 0;
    size_t bytes = 0;
    for (int i = 0; i < input->mir_artifacts->length; i++) {
        InputScriptArtifact* artifact =
            (InputScriptArtifact*)input->mir_artifacts->data[i];
        if (artifact) bytes += artifact->retained_bytes;
    }
    return bytes;
}

static void cache_subtract_bytes(uint64_t* total, size_t bytes) {
    if (!total) return;
    *total = *total >= bytes ? *total - bytes : 0;
}

static void cache_account_retired_input(InputScriptCache* cache,
        ScriptInput* input) {
    if (!cache || !input) return;
    if (cache->stats.retained_entries > 0) cache->stats.retained_entries--;
    cache_subtract_bytes(&cache->stats.retained_source_bytes,
        input->source_length);
    cache_subtract_bytes(&cache->stats.retained_ast_bytes,
        cache_input_ast_bytes(input));
    cache_subtract_bytes(&cache->stats.retained_mir_bytes,
        cache_input_mir_bytes(input));
}

static bool cache_input_matches_invalidation(const ScriptInput* input,
        const InputScriptRequest* request) {
    if (!input || !request || !request->identity || !request->identity[0]) {
        return false;
    }
    if (input->source_kind != request->source_kind ||
            !cache_text_equal(input->identity, request->identity) ||
            !cache_text_equal(input->language, request->language) ||
            !cache_text_equal(input->profile, request->profile) ||
            !cache_text_equal(input->parser_abi, request->parser_abi) ||
            !cache_text_equal(input->parse_flags, request->parse_flags) ||
            !cache_text_equal(input->resolution_base, request->resolution_base) ||
            input->module_mode != request->module_mode) {
        return false;
    }
    if (!request->source) return true;
    if (input->source_length != request->source_length) return true;
    return input->source_length == 0 || memcmp(input->source,
        request->source, input->source_length) != 0;
}

static void cache_reap_retired_input_locked(InputScriptCache* cache,
        ScriptInput* input) {
    if (!cache || !cache->retired_inputs || !input) return;
    for (int i = 0; i < cache->retired_inputs->length; i++) {
        if (cache->retired_inputs->data[i] == input) {
            arraylist_remove(cache->retired_inputs, i);
            cache_destroy_input(input);
            return;
        }
    }
}

static void cache_entry_free(void* item) {
    InputScriptMapEntry* entry = (InputScriptMapEntry*)item;
    if (!entry) return;
    cache_destroy_input(entry->input);
    entry->input = NULL;
}

static InputScriptCachePolicy cache_parse_policy(void) {
    const char* value = shell_getenv("LAMBDA_SCRIPT_CACHE");
    if (!value || !value[0] || strcmp(value, "all") == 0) {
        return INPUT_SCRIPT_CACHE_ALL;
    }
    if (strcmp(value, "off") == 0) return INPUT_SCRIPT_CACHE_OFF;
    if (strcmp(value, "ast") == 0) return INPUT_SCRIPT_CACHE_AST;
    if (strcmp(value, "mir") == 0) return INPUT_SCRIPT_CACHE_MIR;
    log_error("script-cache: invalid LAMBDA_SCRIPT_CACHE='%s'; using all", value);
    return INPUT_SCRIPT_CACHE_ALL;
}

static bool cache_alias_disables_mir(void) {
    const char* lambda_alias = shell_getenv("LAMBDA_DISABLE_MIR_CACHE");
    bool disabled = (lambda_alias &&
        (strcmp(lambda_alias, "1") == 0 || strcmp(lambda_alias, "true") == 0));
    if (disabled) {
        log_info("script-cache: MIR disabled by compatibility alias");
    }
    return disabled;
}

static size_t cache_parse_retention_limit_bytes(void) {
    const char* value = shell_getenv("LAMBDA_SCRIPT_CACHE_MAX_BYTES");
    if (!value || !value[0] || strcmp(value, "0") == 0) return 0;
    char* end = NULL;
    unsigned long long parsed = strtoull(value, &end, 10);
    if (end == value || !end || *end != '\0' || parsed == 0) {
        log_error("script-cache: invalid LAMBDA_SCRIPT_CACHE_MAX_BYTES='%s'; retaining without a byte limit", value);
        return 0;
    }
    unsigned long long max_size = (unsigned long long)(size_t)-1;
    if (parsed > max_size) {
        log_error("script-cache: LAMBDA_SCRIPT_CACHE_MAX_BYTES='%s' exceeds size_t; clamping", value);
        return (size_t)-1;
    }
    return (size_t)parsed;
}

static bool cache_make_input(const InputScriptRequest* request,
        const char* source, size_t source_length, ScriptInput** out_input) {
    if (!request || !request->identity || !request->identity[0] ||
            (!source && source_length != 0) || !out_input) return false;

    ScriptInput* input = (ScriptInput*)mem_calloc(1, sizeof(ScriptInput),
        MEM_CAT_CACHE_OTHER);
    if (!input) return false;
    input->identity = mem_strdup(cache_text(request->identity, ""), MEM_CAT_CACHE_OTHER);
    input->source = mem_dup_n(source ? source : "", source_length, MEM_CAT_CACHE_OTHER);
    input->language = mem_strdup(cache_text(request->language, ""), MEM_CAT_CACHE_OTHER);
    input->profile = mem_strdup(cache_text(request->profile, ""), MEM_CAT_CACHE_OTHER);
    input->parser_abi = mem_strdup(cache_text(request->parser_abi, ""), MEM_CAT_CACHE_OTHER);
    input->parse_flags = mem_strdup(cache_text(request->parse_flags, ""), MEM_CAT_CACHE_OTHER);
    input->resolution_base = mem_strdup(cache_text(request->resolution_base, ""), MEM_CAT_CACHE_OTHER);
    input->backend = mem_strdup(cache_text(request->backend, ""), MEM_CAT_CACHE_OTHER);
    input->execution_mode = mem_strdup(cache_text(request->execution_mode, ""), MEM_CAT_CACHE_OTHER);
    input->source_length = source_length;
    input->source_kind = request->source_kind;
    input->module_mode = request->module_mode;
    if (!input->identity || !input->source || !input->language ||
            !input->profile || !input->parser_abi || !input->parse_flags ||
            !input->resolution_base || !input->backend ||
            !input->execution_mode) {
        cache_destroy_input(input);
        return false;
    }
    input->source_hash = hashmap_hash_xxhash3_bytes(input->source, source_length, 0, 0);
    input->source_key = cache_source_key(input);
    input->cache_context = mem_context_create(NULL, MEM_ROLE_CODE,
        "script.cache.input");
    if (!input->cache_context) {
        cache_destroy_input(input);
        return false;
    }
    input->cache_pool = mem_pool_create(input->cache_context, MEM_ROLE_CODE,
        "script.cache.pool");
    input->cache_arena = mem_arena_create(input->cache_context, MEM_ROLE_CODE,
        "script.cache.arena");
    input->ast_artifacts = arraylist_new(1);
    input->mir_artifacts = arraylist_new(2);
    input->build_states = arraylist_new(2);
    input->dependencies = arraylist_new(2);
    input->dependents = arraylist_new(2);
    if (!input->cache_pool || !input->cache_arena ||
            !input->ast_artifacts || !input->mir_artifacts ||
            !input->build_states ||
            !input->dependencies || !input->dependents) {
        cache_destroy_input(input);
        return false;
    }
    *out_input = input;
    return true;
}

static InputScriptMapEntry* cache_find_entry(InputScriptCache* cache,
        const InputScriptRequest* request, const char* source,
        size_t source_length) {
    (void)cache;
    ScriptInput probe_input = {};
    probe_input.identity = (char*)cache_text(request->identity, "");
    probe_input.source = (char*)(source ? source : "");
    probe_input.source_length = source_length;
    probe_input.source_kind = request->source_kind;
    probe_input.language = (char*)cache_text(request->language, "");
    probe_input.profile = (char*)cache_text(request->profile, "");
    probe_input.parser_abi = (char*)cache_text(request->parser_abi, "");
    probe_input.parse_flags = (char*)cache_text(request->parse_flags, "");
    probe_input.resolution_base = (char*)cache_text(request->resolution_base, "");
    probe_input.module_mode = request->module_mode;
    probe_input.source_hash = hashmap_hash_xxhash3_bytes(probe_input.source,
        source_length, 0, 0);
    probe_input.source_key = cache_source_key(&probe_input);
    InputScriptMapEntry probe = {&probe_input};
    return (InputScriptMapEntry*)hashmap_get(cache->entries, &probe);
}

static ScriptInput* cache_find_input_by_unit_locked(InputScriptCache* cache,
        uint32_t compilation_unit_id) {
    if (!cache || !compilation_unit_id) return NULL;
    size_t cursor = 0;
    void* item = NULL;
    while (hashmap_iter(cache->entries, &cursor, &item)) {
        InputScriptMapEntry* entry = (InputScriptMapEntry*)item;
        ScriptInput* input = entry ? entry->input : NULL;
        if (input && !input->retired &&
                input->compilation_unit_id == compilation_unit_id) {
            return input;
        }
    }
    return NULL;
}

static bool cache_collect_invalidation_cone(ScriptInput* input,
        ArrayList* cone) {
    if (!input || !cone) return false;
    if (cache_input_list_contains(cone, input)) return true;
    if (!arraylist_append(cone, input)) return false;
    for (int i = 0; input->dependents && i < input->dependents->length; i++) {
        ScriptInput* dependent = (ScriptInput*)input->dependents->data[i];
        if (dependent && !dependent->retired &&
                !cache_collect_invalidation_cone(dependent, cone)) {
            return false;
        }
    }
    return true;
}

static InputScriptLease* cache_make_lease(InputScriptCache* cache,
        InputCacheScope* scope, ScriptInput* input, bool persistent) {
    if (!cache || !scope || !input) return NULL;
    InputScriptLease* lease = (InputScriptLease*)mem_calloc(1,
        sizeof(InputScriptLease), MEM_CAT_CACHE_OTHER);
    if (!lease) return NULL;
    lease->cache = cache;
    lease->scope = scope;
    lease->input = input;
    lease->persistent = persistent;
    input->lease_count++;
    if (!arraylist_append(scope->leases, lease)) {
        input->lease_count--;
        mem_free(lease);
        return NULL;
    }
    cache->next_access_epoch++;
    if (cache->next_access_epoch == 0) cache->next_access_epoch = 1;
    input->last_access_epoch = cache->next_access_epoch;
    cache->stats.leases_acquired++;
    return lease;
}

static size_t cache_retained_bytes(const InputScriptCache* cache) {
    return (size_t)(cache->stats.retained_source_bytes +
        cache->stats.retained_ast_bytes + cache->stats.retained_mir_bytes);
}

static void cache_update_peak(InputScriptCache* cache) {
    size_t current = cache_retained_bytes(cache);
    if (current > cache->stats.peak_bytes) cache->stats.peak_bytes = current;
}

static bool cache_input_retention_evictable(const ScriptInput* input) {
    return input && !input->retired && input->lease_count == 0 &&
        (!input->dependencies || input->dependencies->length == 0) &&
        (!input->dependents || input->dependents->length == 0);
}

static bool cache_evict_inactive_input_locked(InputScriptCache* cache,
        ScriptInput* input) {
    if (!cache || !cache_input_retention_evictable(input)) {
        return false;
    }
    InputScriptMapEntry probe = {input};
    const InputScriptMapEntry* removed =
        (const InputScriptMapEntry*)hashmap_delete(cache->entries, &probe);
    if (!removed) return false;
    ((InputScriptMapEntry*)removed)->input = NULL;
    input->retired = true;
    cache_account_retired_input(cache, input);
    cache->stats.evictions++;
    cache_destroy_input(input);
    return true;
}

static void cache_enforce_retention_limit_locked(InputScriptCache* cache) {
    if (!cache || cache->retention_limit_bytes == 0) return;
    while (cache_retained_bytes(cache) > cache->retention_limit_bytes) {
        ScriptInput* oldest = NULL;
        size_t cursor = 0;
        void* item = NULL;
        while (hashmap_iter(cache->entries, &cursor, &item)) {
            InputScriptMapEntry* entry = (InputScriptMapEntry*)item;
            ScriptInput* candidate = entry ? entry->input : NULL;
            // Preserve live images and complete freshness cones in this
            // minimal policy; cone reclamation needs a dedicated rule.
            if (!cache_input_retention_evictable(candidate)) {
                continue;
            }
            if (!oldest || candidate->last_access_epoch < oldest->last_access_epoch) {
                oldest = candidate;
            }
        }
        if (!oldest || !cache_evict_inactive_input_locked(cache, oldest)) {
            cache->stats.retention_pressure++;
            return;
        }
    }
}

InputScriptCache* input_script_cache_create(void) {
    InputScriptCache* cache = (InputScriptCache*)mem_calloc(1,
        sizeof(InputScriptCache), MEM_CAT_CACHE_OTHER);
    if (!cache) return NULL;
    pthread_mutex_init(&cache->mutex, NULL);
    cache->policy = cache_parse_policy();
    cache->mir_disabled_by_alias = cache_alias_disables_mir();
    cache->retention_limit_bytes = cache_parse_retention_limit_bytes();
    cache->next_compilation_unit_id = 1;
    cache->next_access_epoch = 1;
    cache->stats.retention_limit_bytes = cache->retention_limit_bytes;
    cache->entries = hashmap_new(sizeof(InputScriptMapEntry), 32, 0, 0,
        cache_entry_hash, cache_entry_compare, cache_entry_free, NULL);
    cache->retired_inputs = arraylist_new(4);
    if (!cache->entries || !cache->retired_inputs) {
        if (cache->entries) hashmap_free(cache->entries);
        if (cache->retired_inputs) arraylist_free(cache->retired_inputs);
        pthread_mutex_destroy(&cache->mutex);
        mem_free(cache);
        return NULL;
    }
    log_info("script-cache: initialized policy=%d retention_limit=%zu",
        (int)cache->policy, cache->retention_limit_bytes);
    return cache;
}

void input_script_cache_destroy(InputScriptCache* cache) {
    if (!cache) return;
    pthread_mutex_lock(&cache->mutex);
    InputScriptCacheStats stats = cache->stats;
    log_notice("script-cache: shutdown entries=%llu source_lookups=%llu source_hits=%llu source_misses=%llu ast_hits=%llu mir_hits=%llu invalidations=%llu waits=%llu poisoned=%llu evictions=%llu retention_limit=%llu retention_pressure=%llu retained=%llu peak=%llu",
        (unsigned long long)stats.retained_entries,
        (unsigned long long)stats.source_lookups,
        (unsigned long long)stats.source_hits,
        (unsigned long long)stats.source_misses,
        (unsigned long long)stats.ast_hits,
        (unsigned long long)stats.mir_hits,
        (unsigned long long)stats.invalidations,
        (unsigned long long)stats.single_flight_waits,
        (unsigned long long)stats.poisoned,
        (unsigned long long)stats.evictions,
        (unsigned long long)stats.retention_limit_bytes,
        (unsigned long long)stats.retention_pressure,
        (unsigned long long)(stats.retained_source_bytes +
            stats.retained_ast_bytes + stats.retained_mir_bytes),
        (unsigned long long)stats.peak_bytes);
    hashmap_free(cache->entries);
    cache->entries = NULL;
    if (cache->retired_inputs) {
        for (int i = 0; i < cache->retired_inputs->length; i++) {
            cache_destroy_input((ScriptInput*)cache->retired_inputs->data[i]);
        }
        arraylist_free(cache->retired_inputs);
        cache->retired_inputs = NULL;
    }
    pthread_mutex_unlock(&cache->mutex);
    pthread_mutex_destroy(&cache->mutex);
    mem_free(cache);
}

InputScriptCachePolicy input_script_cache_policy(const InputScriptCache* cache) {
    return cache ? cache->policy : INPUT_SCRIPT_CACHE_OFF;
}

bool input_script_cache_ast_enabled(const InputScriptCache* cache) {
    if (!cache) return false;
    return cache->policy == INPUT_SCRIPT_CACHE_AST ||
        cache->policy == INPUT_SCRIPT_CACHE_MIR ||
        cache->policy == INPUT_SCRIPT_CACHE_ALL;
}

bool input_script_cache_mir_enabled(const InputScriptCache* cache) {
    if (!cache || cache->mir_disabled_by_alias) return false;
    return cache->policy == INPUT_SCRIPT_CACHE_MIR ||
        cache->policy == INPUT_SCRIPT_CACHE_ALL;
}

InputScriptCache* input_manager_script_cache(InputManager* manager) {
    return manager ? manager->get_script_cache() : NULL;
}

InputScriptCache* input_manager_global_script_cache(void) {
    return InputManager::global_script_cache();
}

InputCacheScope* input_script_cache_open_scope(InputScriptCache* cache) {
    if (!cache) return NULL;
    InputCacheScope* scope = (InputCacheScope*)mem_calloc(1,
        sizeof(InputCacheScope), MEM_CAT_CACHE_OTHER);
    if (!scope) return NULL;
    scope->cache = cache;
    scope->leases = arraylist_new(4);
    if (!scope->leases) {
        mem_free(scope);
        return NULL;
    }
    pthread_mutex_lock(&cache->mutex);
    cache->stats.scopes_opened++;
    pthread_mutex_unlock(&cache->mutex);
    return scope;
}

InputCacheScope* input_manager_open_script_scope(InputManager* manager,
        const InputScriptRequest* request) {
    (void)request;
    return input_script_cache_open_scope(input_manager_script_cache(manager));
}

void input_script_cache_close_scope(InputCacheScope* scope) {
    if (!scope || scope->closed) return;
    while (scope->leases && scope->leases->length > 0) {
        InputScriptLease* lease = (InputScriptLease*)scope->leases->data[0];
        input_script_cache_release(lease);
    }
    scope->closed = true;
    arraylist_free(scope->leases);
    mem_free(scope);
}

void input_manager_close_script_scope(InputCacheScope* scope) {
    input_script_cache_close_scope(scope);
}

InputScriptLease* input_script_cache_acquire(InputCacheScope* scope,
        const InputScriptRequest* request) {
    if (!scope || scope->closed || !scope->cache || !request ||
            !request->identity || !request->identity[0] ||
            (!request->source && request->source_length != 0)) {
        if (scope && scope->cache) scope->cache->stats.rejected++;
        return NULL;
    }
    InputScriptCache* cache = scope->cache;
    pthread_mutex_lock(&cache->mutex);
    cache->stats.source_lookups++;
    if (cache->policy != INPUT_SCRIPT_CACHE_OFF) {
        InputScriptMapEntry* found = cache_find_entry(cache, request,
            request->source ? request->source : "", request->source_length);
        if (found && found->input && !found->input->retired) {
            cache->stats.source_hits++;
            InputScriptLease* lease = cache_make_lease(cache, scope,
                found->input, true);
            if (lease) {
                lease->ast_key = cache_ast_key(found->input, request->ast_abi);
                lease->mir_key = cache_mir_key(found->input, request);
            }
            pthread_mutex_unlock(&cache->mutex);
            return lease;
        }
    }
    cache->stats.source_misses++;
    ScriptInput* input = NULL;
    bool made = cache_make_input(request, request->source ? request->source : "",
        request->source_length, &input);
    if (!made) {
        cache->stats.rejected++;
        pthread_mutex_unlock(&cache->mutex);
        return NULL;
    }
    input->compilation_unit_id = cache->next_compilation_unit_id++;
    if (cache->next_compilation_unit_id == 0) cache->next_compilation_unit_id = 1;
    if (cache->policy == INPUT_SCRIPT_CACHE_OFF) {
        InputScriptLease* lease = cache_make_lease(cache, scope, input, false);
        if (lease) {
            lease->ast_key = cache_ast_key(input, request->ast_abi);
            lease->mir_key = cache_mir_key(input, request);
        }
        if (!lease) cache_destroy_input(input);
        pthread_mutex_unlock(&cache->mutex);
        return lease;
    }
    InputScriptMapEntry entry = {input};
    const void* replaced = hashmap_set(cache->entries, &entry);
    if (hashmap_oom(cache->entries)) {
        cache_destroy_input(input);
        cache->stats.rejected++;
        pthread_mutex_unlock(&cache->mutex);
        return NULL;
    }
    if (replaced) {
        // hashmap_set returns the displaced value in scratch storage; the new
        // entry is already installed, so retire only the old source owner.
        InputScriptMapEntry* displaced = (InputScriptMapEntry*)replaced;
        cache_destroy_input(displaced->input);
        log_error("script-cache: duplicate source replacement for %s",
            request->identity);
    }
    cache->stats.retained_entries++;
    cache->stats.retained_source_bytes += input->source_length;
    cache_update_peak(cache);
    InputScriptLease* lease = cache_make_lease(cache, scope, input, true);
    if (!lease) {
        cache->stats.rejected++;
        pthread_mutex_unlock(&cache->mutex);
        return NULL;
    }
    lease->ast_key = cache_ast_key(input, request->ast_abi);
    lease->mir_key = cache_mir_key(input, request);
    cache_enforce_retention_limit_locked(cache);
    pthread_mutex_unlock(&cache->mutex);
    return lease;
}

InputScriptLease* input_script_cache_acquire_file(InputCacheScope* scope,
        const InputScriptRequest* request, const char* path) {
    if (!scope || !request || !path) return NULL;
    size_t source_length = 0;
    char* source = read_binary_file(path, &source_length);
    if (!source) {
        pthread_mutex_lock(&scope->cache->mutex);
        scope->cache->stats.source_lookups++;
        scope->cache->stats.source_misses++;
        pthread_mutex_unlock(&scope->cache->mutex);
        log_error("script-cache: failed to read source %s", path);
        return NULL;
    }
    InputScriptRequest file_request = *request;
    file_request.source = source;
    file_request.source_length = source_length;
    // A changed file is a new source generation for every adapter, not only
    // Lambda's runtime-local script index. Retire older generations before
    // admitting the exact snapshot; active leases remain valid until release.
    input_script_cache_invalidate(scope->cache, &file_request);
    InputScriptLease* lease = input_script_cache_acquire(scope, &file_request);
    mem_free(source);
    return lease;
}

static size_t cache_invalidate_input_cone_locked(InputScriptCache* cache,
        ScriptInput* seed) {
    if (!cache || !seed || seed->retired) return 0;
    ArrayList* cone = arraylist_new(4);
    if (!cone) {
        cache->stats.rejected++;
        return 0;
    }
    if (!cache_collect_invalidation_cone(seed, cone)) {
        cache->stats.rejected++;
        arraylist_free(cone);
        return 0;
    }

    int deferred_start = cache->retired_inputs->length;
    for (int i = 0; i < cone->length; i++) {
        ScriptInput* input = (ScriptInput*)cone->data[i];
        if (input && input->lease_count > 0 &&
                !arraylist_append(cache->retired_inputs, input)) {
            cache->stats.rejected++;
            arraylist_remove_range(cache->retired_inputs, deferred_start,
                cache->retired_inputs->length - deferred_start);
            arraylist_free(cone);
            return 0;
        }
    }

    size_t invalidated = 0;
    for (int i = 0; i < cone->length; i++) {
        ScriptInput* input = (ScriptInput*)cone->data[i];
        if (!input || input->retired) continue;
        bool direct = input == seed;
        InputScriptMapEntry probe = {input};
        const InputScriptMapEntry* removed =
            (const InputScriptMapEntry*)hashmap_delete(cache->entries, &probe);
        if (!removed) continue;
        ((InputScriptMapEntry*)removed)->input = NULL;
        input->retired = true;
        cache_account_retired_input(cache, input);
        cache->stats.invalidations++;
        if (!direct) cache->stats.dependency_invalidations++;
        invalidated++;
    }
    // Every map owner is detached before an unleased image is destroyed: a
    // dependency and an importer can otherwise observe each other's edge
    // during one cone teardown (D8.5.1v3).
    for (int i = 0; i < cone->length; i++) {
        ScriptInput* input = (ScriptInput*)cone->data[i];
        if (input && input->lease_count == 0) cache_destroy_input(input);
    }
    arraylist_free(cone);
    return invalidated;
}

size_t input_script_cache_invalidate(InputScriptCache* cache,
        const InputScriptRequest* request) {
    if (!cache || !request || !request->identity || !request->identity[0]) {
        return 0;
    }

    pthread_mutex_lock(&cache->mutex);
    ScriptInput* seed = NULL;
    size_t cursor = 0;
    void* item = NULL;
    while (hashmap_iter(cache->entries, &cursor, &item)) {
        InputScriptMapEntry* entry = (InputScriptMapEntry*)item;
        ScriptInput* input = entry ? entry->input : NULL;
        if (input && cache_input_matches_invalidation(input, request)) {
            seed = input;
            break;
        }
    }
    size_t invalidated = cache_invalidate_input_cone_locked(cache, seed);
    pthread_mutex_unlock(&cache->mutex);
    return invalidated;
}

size_t input_script_cache_invalidate_unit(InputScriptCache* cache,
        uint32_t compilation_unit_id) {
    if (!cache || !compilation_unit_id) return 0;

    pthread_mutex_lock(&cache->mutex);
    ScriptInput* seed = cache_find_input_by_unit_locked(cache,
        compilation_unit_id);
    size_t invalidated = cache_invalidate_input_cone_locked(cache, seed);
    pthread_mutex_unlock(&cache->mutex);
    return invalidated;
}

char* input_script_cache_copy_source(InputScriptCache* cache,
        const InputScriptRequest* request, const char* source,
        size_t source_length, size_t* out_length) {
    if (out_length) *out_length = 0;
    if (!cache || !request || (!source && source_length != 0)) return NULL;

    InputCacheScope* scope = input_script_cache_open_scope(cache);
    InputScriptRequest source_request = *request;
    source_request.source = source ? source : "";
    source_request.source_length = source ? source_length : 0;
    InputScriptLease* lease = scope
        ? input_script_cache_acquire(scope, &source_request) : NULL;
    char* copy = NULL;
    if (lease) {
        ScriptInput* input = input_script_lease_input(lease);
        size_t cached_length = input_script_source_length(input);
        const char* cached_source = input_script_source(input);
        copy = mem_dup_n(cached_source, cached_length, MEM_CAT_CACHE_OTHER);
        if (copy) {
            if (out_length) *out_length = cached_length;
        }
    }
    input_script_cache_close_scope(scope);
    return copy;
}

char* input_script_cache_copy_file_source(InputScriptCache* cache,
        const InputScriptRequest* request, const char* path,
        size_t* out_length) {
    if (out_length) *out_length = 0;
    if (!cache || !request || !path || !path[0]) return NULL;

    size_t source_length = 0;
    char* source = read_binary_file(path, &source_length);
    if (!source) {
        pthread_mutex_lock(&cache->mutex);
        cache->stats.source_lookups++;
        cache->stats.source_misses++;
        pthread_mutex_unlock(&cache->mutex);
        log_error("script-cache: failed to read source %s", path);
        return NULL;
    }
    InputScriptRequest file_request = *request;
    file_request.source = source;
    file_request.source_length = source_length;
    input_script_cache_invalidate(cache, &file_request);
    char* copy = input_script_cache_copy_source(cache, request, source,
        source_length, out_length);
    mem_free(source);
    return copy;
}

static void cache_abandon_build_claim_locked(InputScriptLease* lease,
        InputScriptBuildKind kind);

void input_script_cache_release(InputScriptLease* lease) {
    if (!lease || lease->released) return;
    InputScriptCache* cache = lease->cache;
    InputCacheScope* scope = lease->scope;
    ScriptInput* input = lease->input;
    if (cache) pthread_mutex_lock(&cache->mutex);
    if (cache && lease->ast_build_claimed) {
        // Never strand concurrent claimants if recovery closes an owner scope.
        cache_abandon_build_claim_locked(lease, INPUT_SCRIPT_BUILD_AST);
    }
    if (cache && lease->mir_build_claimed) {
        cache_abandon_build_claim_locked(lease, INPUT_SCRIPT_BUILD_MIR);
    }
    if (input && input->lease_count > 0) input->lease_count--;
    if (scope && scope->leases) {
        for (int i = 0; i < scope->leases->length; i++) {
            if (scope->leases->data[i] == lease) {
                arraylist_remove(scope->leases, i);
                break;
            }
        }
    }
    lease->released = true;
    if (cache) {
        cache->stats.leases_released++;
        if (lease->persistent && input && input->retired &&
                input->lease_count == 0) {
            cache_reap_retired_input_locked(cache, input);
        } else if (lease->persistent) {
            cache_enforce_retention_limit_locked(cache);
        }
        pthread_mutex_unlock(&cache->mutex);
    }
    if (!lease->persistent && input) cache_destroy_input(input);
    mem_free(lease);
}

ScriptInput* input_script_lease_input(InputScriptLease* lease) {
    return lease && !lease->released ? lease->input : NULL;
}

const char* input_script_source(const ScriptInput* input) {
    return input ? input->source : NULL;
}

size_t input_script_source_length(const ScriptInput* input) {
    return input ? input->source_length : 0;
}

const char* input_script_identity(const ScriptInput* input) {
    return input ? input->identity : NULL;
}

uint32_t input_script_compilation_unit_id(const ScriptInput* input) {
    return input ? input->compilation_unit_id : 0;
}

bool input_script_cache_record_dependency_by_unit(InputScriptCache* cache,
        uint32_t importer_unit_id, uint32_t dependency_unit_id) {
    if (!cache || !importer_unit_id || !dependency_unit_id) return false;
    if (importer_unit_id == dependency_unit_id) return true;
    pthread_mutex_lock(&cache->mutex);
    ScriptInput* importer = cache_find_input_by_unit_locked(cache,
        importer_unit_id);
    ScriptInput* dependency = cache_find_input_by_unit_locked(cache,
        dependency_unit_id);
    if (!importer || !dependency) {
        pthread_mutex_unlock(&cache->mutex);
        return false;
    }
    if (cache_input_list_contains(importer->dependencies, dependency)) {
        pthread_mutex_unlock(&cache->mutex);
        return true;
    }
    if (!arraylist_append(importer->dependencies, dependency)) {
        pthread_mutex_unlock(&cache->mutex);
        return false;
    }
    if (!arraylist_append(dependency->dependents, importer)) {
        cache_input_list_remove(importer->dependencies, dependency);
        pthread_mutex_unlock(&cache->mutex);
        return false;
    }
    pthread_mutex_unlock(&cache->mutex);
    log_debug("script-cache: dependency importer_unit=%u dependency_unit=%u",
        importer_unit_id, dependency_unit_id);
    return true;
}

bool input_script_cache_refresh_file_unit(InputScriptCache* cache,
        uint32_t compilation_unit_id, const char* path, bool* out_changed) {
    if (out_changed) *out_changed = false;
    if (!cache || !compilation_unit_id || !path || !path[0]) return false;

    size_t source_length = 0;
    char* source = read_binary_file(path, &source_length);
    if (!source) {
        pthread_mutex_lock(&cache->mutex);
        ScriptInput* input = cache_find_input_by_unit_locked(cache,
            compilation_unit_id);
        size_t invalidated = cache_invalidate_input_cone_locked(cache, input);
        pthread_mutex_unlock(&cache->mutex);
        if (out_changed) *out_changed = invalidated > 0;
        log_error("script-cache: failed to refresh source %s", path);
        return invalidated > 0;
    }

    pthread_mutex_lock(&cache->mutex);
    cache->stats.source_lookups++;
    ScriptInput* input = cache_find_input_by_unit_locked(cache,
        compilation_unit_id);
    if (!input) {
        pthread_mutex_unlock(&cache->mutex);
        mem_free(source);
        return false;
    }
    bool changed = input->source_length != source_length ||
        (source_length > 0 && memcmp(input->source, source, source_length) != 0);
    if (!changed) {
        cache->stats.source_hits++;
        pthread_mutex_unlock(&cache->mutex);
        mem_free(source);
        return true;
    }

    cache->stats.source_misses++;
    size_t invalidated = cache_invalidate_input_cone_locked(cache, input);
    pthread_mutex_unlock(&cache->mutex);
    mem_free(source);
    if (out_changed) *out_changed = invalidated > 0;
    return invalidated > 0;
}

typedef struct InputScriptFileRefresh {
    uint32_t compilation_unit_id;
    char* path;
} InputScriptFileRefresh;

static void cache_destroy_file_refresh(InputScriptFileRefresh* refresh) {
    if (!refresh) return;
    mem_free(refresh->path);
    mem_free(refresh);
}

static void cache_destroy_file_refreshes(ArrayList* refreshes) {
    if (!refreshes) return;
    for (int i = 0; i < refreshes->length; i++) {
        cache_destroy_file_refresh((InputScriptFileRefresh*)refreshes->data[i]);
    }
    arraylist_free(refreshes);
}

static bool cache_collect_file_dependency_refreshes_locked(
        InputScriptCache* cache, uint32_t compilation_unit_id,
        ArrayList* refreshes) {
    if (!cache || !compilation_unit_id || !refreshes) return false;
    ScriptInput* root = cache_find_input_by_unit_locked(cache,
        compilation_unit_id);
    if (!root || root->retired) return false;
    ArrayList* pending = arraylist_new(4);
    ArrayList* visited = arraylist_new(4);
    if (!pending || !visited || !arraylist_append(pending, root) ||
            !arraylist_append(visited, root)) {
        arraylist_free(pending);
        arraylist_free(visited);
        return false;
    }
    while (pending->length > 0) {
        ScriptInput* current = (ScriptInput*)pending->data[0];
        arraylist_remove(pending, 0);
        for (int i = 0; current && current->dependencies &&
                i < current->dependencies->length; i++) {
            ScriptInput* dependency = (ScriptInput*)current->dependencies->data[i];
            if (!dependency || dependency->retired ||
                    cache_input_list_contains(visited, dependency)) {
                continue;
            }
            if (!arraylist_append(visited, dependency) ||
                    !arraylist_append(pending, dependency)) {
                arraylist_free(pending);
                arraylist_free(visited);
                return false;
            }
            if (dependency->source_kind != INPUT_SCRIPT_SOURCE_FILE ||
                    !dependency->identity || !dependency->identity[0]) {
                continue;
            }
            InputScriptFileRefresh* refresh = (InputScriptFileRefresh*)
                mem_calloc(1, sizeof(InputScriptFileRefresh), MEM_CAT_CACHE_OTHER);
            if (!refresh) {
                arraylist_free(pending);
                arraylist_free(visited);
                return false;
            }
            refresh->compilation_unit_id = dependency->compilation_unit_id;
            refresh->path = mem_strdup(dependency->identity, MEM_CAT_CACHE_OTHER);
            if (!refresh->path || !arraylist_append(refreshes, refresh)) {
                cache_destroy_file_refresh(refresh);
                arraylist_free(pending);
                arraylist_free(visited);
                return false;
            }
        }
    }
    arraylist_free(pending);
    arraylist_free(visited);
    return true;
}

bool input_script_cache_refresh_file_dependencies(InputScriptCache* cache,
        uint32_t compilation_unit_id, bool* out_changed) {
    if (out_changed) *out_changed = false;
    if (!cache || !compilation_unit_id) return false;
    ArrayList* refreshes = arraylist_new(4);
    if (!refreshes) {
        input_script_cache_invalidate_unit(cache, compilation_unit_id);
        if (out_changed) *out_changed = true;
        return false;
    }
    pthread_mutex_lock(&cache->mutex);
    bool collected = cache_collect_file_dependency_refreshes_locked(cache,
        compilation_unit_id, refreshes);
    pthread_mutex_unlock(&cache->mutex);
    if (!collected) {
        cache_destroy_file_refreshes(refreshes);
        size_t invalidated = input_script_cache_invalidate_unit(cache,
            compilation_unit_id);
        if (out_changed) *out_changed = invalidated > 0;
        return false;
    }
    for (int i = 0; i < refreshes->length; i++) {
        InputScriptFileRefresh* refresh =
            (InputScriptFileRefresh*)refreshes->data[i];
        bool changed = false;
        bool refreshed = input_script_cache_refresh_file_unit(cache,
            refresh->compilation_unit_id, refresh->path, &changed);
        if (changed) {
            cache_destroy_file_refreshes(refreshes);
            if (out_changed) *out_changed = true;
            return true;
        }
        if (!refreshed) {
            cache_destroy_file_refreshes(refreshes);
            // The dependency image disappeared while its source could not be
            // verified. Retire the importer rather than serving a stale MIR.
            size_t invalidated = input_script_cache_invalidate_unit(cache,
                compilation_unit_id);
            if (out_changed) *out_changed = invalidated > 0;
            return false;
        }
    }
    cache_destroy_file_refreshes(refreshes);
    return true;
}

static uint64_t cache_build_key(const InputScriptLease* lease,
        InputScriptBuildKind kind) {
    return kind == INPUT_SCRIPT_BUILD_AST ? lease->ast_key : lease->mir_key;
}

static bool cache_build_kind_enabled(const InputScriptCache* cache,
        InputScriptBuildKind kind) {
    return kind == INPUT_SCRIPT_BUILD_AST
        ? input_script_cache_ast_enabled(cache)
        : kind == INPUT_SCRIPT_BUILD_MIR && input_script_cache_mir_enabled(cache);
}

static ArrayList* cache_build_artifacts(const ScriptInput* input,
        InputScriptBuildKind kind) {
    if (!input) return NULL;
    return kind == INPUT_SCRIPT_BUILD_AST ? input->ast_artifacts
        : kind == INPUT_SCRIPT_BUILD_MIR ? input->mir_artifacts : NULL;
}

static bool cache_build_artifact_ready(const InputScriptLease* lease,
        InputScriptBuildKind kind) {
    ArrayList* artifacts = cache_build_artifacts(lease ? lease->input : NULL,
        kind);
    uint64_t key = cache_build_key(lease, kind);
    for (int i = 0; artifacts && i < artifacts->length; i++) {
        InputScriptArtifact* artifact = (InputScriptArtifact*)artifacts->data[i];
        if (artifact && artifact->value && artifact->key == key) return true;
    }
    return false;
}

static InputScriptBuildState* cache_find_build_state(ScriptInput* input,
        InputScriptBuildKind kind, uint64_t key) {
    if (!input || !input->build_states) return NULL;
    for (int i = 0; i < input->build_states->length; i++) {
        InputScriptBuildState* state = (InputScriptBuildState*)
            input->build_states->data[i];
        if (state && state->kind == kind && state->key == key) return state;
    }
    return NULL;
}

static InputScriptBuildState* cache_get_build_state(ScriptInput* input,
        InputScriptBuildKind kind, uint64_t key) {
    InputScriptBuildState* state = cache_find_build_state(input, kind, key);
    if (state) return state;
    if (!input || !input->build_states) return NULL;
    state = (InputScriptBuildState*)mem_calloc(1, sizeof(InputScriptBuildState),
        MEM_CAT_CACHE_OTHER);
    if (!state) return NULL;
    state->kind = kind;
    state->key = key;
    if (pthread_cond_init(&state->completed, NULL) != 0) {
        mem_free(state);
        return NULL;
    }
    if (!arraylist_append(input->build_states, state)) {
        pthread_cond_destroy(&state->completed);
        mem_free(state);
        return NULL;
    }
    return state;
}

static void cache_abandon_build_claim_locked(InputScriptLease* lease,
        InputScriptBuildKind kind) {
    if (!lease || !lease->cache || !lease->input) return;
    bool* claimed = kind == INPUT_SCRIPT_BUILD_AST
        ? &lease->ast_build_claimed : &lease->mir_build_claimed;
    if (!*claimed) return;
    InputScriptBuildState* state = cache_find_build_state(lease->input, kind,
        cache_build_key(lease, kind));
    if (state && state->building) {
        state->building = false;
        if (!state->poisoned) {
            state->poisoned = true;
            lease->cache->stats.poisoned++;
        }
        pthread_cond_broadcast(&state->completed);
        log_error("script-cache: abandoned %s build for %s; poisoned until source invalidation",
            kind == INPUT_SCRIPT_BUILD_AST ? "AST" : "MIR",
            lease->input->identity ? lease->input->identity : "<unknown>");
    }
    *claimed = false;
}

InputScriptBuildClaim input_script_cache_claim_build(InputScriptLease* lease,
        InputScriptBuildKind kind) {
    if (!lease || lease->released || !lease->cache || !lease->input ||
            (kind != INPUT_SCRIPT_BUILD_AST && kind != INPUT_SCRIPT_BUILD_MIR)) {
        return INPUT_SCRIPT_BUILD_BYPASS;
    }
    InputScriptCache* cache = lease->cache;
    pthread_mutex_lock(&cache->mutex);
    if (!cache_build_kind_enabled(cache, kind)) {
        pthread_mutex_unlock(&cache->mutex);
        return INPUT_SCRIPT_BUILD_BYPASS;
    }
    if (cache_build_artifact_ready(lease, kind)) {
        pthread_mutex_unlock(&cache->mutex);
        return INPUT_SCRIPT_BUILD_READY;
    }
    InputScriptBuildState* state = cache_get_build_state(lease->input, kind,
        cache_build_key(lease, kind));
    if (!state) {
        cache->stats.rejected++;
        pthread_mutex_unlock(&cache->mutex);
        return INPUT_SCRIPT_BUILD_BYPASS;
    }
    while (state->building) {
        cache->stats.single_flight_waits++;
        pthread_cond_wait(&state->completed, &cache->mutex);
        if (cache_build_artifact_ready(lease, kind)) {
            pthread_mutex_unlock(&cache->mutex);
            return INPUT_SCRIPT_BUILD_READY;
        }
        if (state->poisoned) {
            pthread_mutex_unlock(&cache->mutex);
            return INPUT_SCRIPT_BUILD_POISONED;
        }
    }
    if (state->poisoned) {
        pthread_mutex_unlock(&cache->mutex);
        return INPUT_SCRIPT_BUILD_POISONED;
    }
    state->building = true;
    if (kind == INPUT_SCRIPT_BUILD_AST) lease->ast_build_claimed = true;
    else lease->mir_build_claimed = true;
    pthread_mutex_unlock(&cache->mutex);
    return INPUT_SCRIPT_BUILD_OWNER;
}

void input_script_cache_complete_build(InputScriptLease* lease,
        InputScriptBuildKind kind, bool published, bool poison) {
    if (!lease || lease->released || !lease->cache || !lease->input ||
            (kind != INPUT_SCRIPT_BUILD_AST && kind != INPUT_SCRIPT_BUILD_MIR)) {
        return;
    }
    InputScriptCache* cache = lease->cache;
    pthread_mutex_lock(&cache->mutex);
    InputScriptBuildState* state = cache_find_build_state(lease->input, kind,
        cache_build_key(lease, kind));
    if (state && state->building) {
        state->building = false;
        if (poison && !state->poisoned) {
            state->poisoned = true;
            cache->stats.poisoned++;
        }
        // A published artifact is verified by waiters while holding this same
        // mutex; an unpublished non-poisoned build permits one ordinary retry.
        (void)published;
        pthread_cond_broadcast(&state->completed);
    }
    if (kind == INPUT_SCRIPT_BUILD_AST) lease->ast_build_claimed = false;
    else lease->mir_build_claimed = false;
    pthread_mutex_unlock(&cache->mutex);
}

bool input_script_cache_get_ast(InputScriptLease* lease, void** out_ast) {
    if (out_ast) *out_ast = NULL;
    if (!lease || lease->released || !lease->cache || !lease->input) return false;
    InputScriptCache* cache = lease->cache;
    pthread_mutex_lock(&cache->mutex);
    cache->stats.ast_lookups++;
    InputScriptArtifact* found = NULL;
    if (input_script_cache_ast_enabled(cache) && lease->input->ast_artifacts) {
        for (int i = 0; i < lease->input->ast_artifacts->length; i++) {
            InputScriptArtifact* candidate =
                (InputScriptArtifact*)lease->input->ast_artifacts->data[i];
            if (candidate && candidate->value && candidate->key == lease->ast_key) {
                found = candidate;
                break;
            }
        }
    }
    bool hit = found != NULL;
    if (hit) {
        cache->stats.ast_hits++;
        if (out_ast) *out_ast = found->value;
    } else {
        cache->stats.ast_misses++;
    }
    pthread_mutex_unlock(&cache->mutex);
    return hit;
}

bool input_script_cache_publish_ast(InputScriptLease* lease, void* ast,
        const InputScriptArtifactOps* ops) {
    if (!lease || lease->released || !lease->cache || !lease->input || !ast) return false;
    InputScriptCache* cache = lease->cache;
    pthread_mutex_lock(&cache->mutex);
    if (!input_script_cache_ast_enabled(cache)) {
        pthread_mutex_unlock(&cache->mutex);
        return false;
    }
    if (!lease->input->ast_artifacts) {
        pthread_mutex_unlock(&cache->mutex);
        return false;
    }
    for (int i = 0; i < lease->input->ast_artifacts->length; i++) {
        InputScriptArtifact* existing =
            (InputScriptArtifact*)lease->input->ast_artifacts->data[i];
        if (existing && existing->key == lease->ast_key) {
            pthread_mutex_unlock(&cache->mutex);
            return false;
        }
    }
    InputScriptArtifact* artifact = (InputScriptArtifact*)mem_calloc(1,
        sizeof(InputScriptArtifact), MEM_CAT_CACHE_OTHER);
    if (!artifact) {
        pthread_mutex_unlock(&cache->mutex);
        return false;
    }
    artifact->value = ast;
    if (ops) artifact->ops = *ops;
    artifact->key = lease->ast_key;
    artifact->retained_bytes = ops && ops->retained_bytes
        ? ops->retained_bytes(ast) : 0;
    if (!arraylist_append(lease->input->ast_artifacts, artifact)) {
        mem_free(artifact);
        pthread_mutex_unlock(&cache->mutex);
        return false;
    }
    cache->stats.ast_builds++;
    cache->stats.retained_ast_bytes += artifact->retained_bytes;
    cache_update_peak(cache);
    cache_enforce_retention_limit_locked(cache);
    pthread_mutex_unlock(&cache->mutex);
    return true;
}

bool input_script_cache_get_mir(InputScriptLease* lease, void** out_mir) {
    if (out_mir) *out_mir = NULL;
    if (!lease || lease->released || !lease->cache || !lease->input) return false;
    InputScriptCache* cache = lease->cache;
    pthread_mutex_lock(&cache->mutex);
    cache->stats.mir_lookups++;
    InputScriptArtifact* found = NULL;
    if (input_script_cache_mir_enabled(cache) && lease->input->mir_artifacts) {
        for (int i = 0; i < lease->input->mir_artifacts->length; i++) {
            InputScriptArtifact* candidate =
                (InputScriptArtifact*)lease->input->mir_artifacts->data[i];
            if (candidate && candidate->value && candidate->key == lease->mir_key) {
                found = candidate;
                break;
            }
        }
    }
    if (found) {
        cache->stats.mir_hits++;
        if (out_mir) *out_mir = found->value;
    } else {
        cache->stats.mir_misses++;
    }
    pthread_mutex_unlock(&cache->mutex);
    return found != NULL;
}

bool input_script_cache_publish_mir(InputScriptLease* lease, void* mir,
        const InputScriptArtifactOps* ops) {
    if (!lease || lease->released || !lease->cache || !lease->input || !mir) return false;
    InputScriptCache* cache = lease->cache;
    pthread_mutex_lock(&cache->mutex);
    if (!input_script_cache_mir_enabled(cache) || !lease->input->mir_artifacts) {
        pthread_mutex_unlock(&cache->mutex);
        return false;
    }
    for (int i = 0; i < lease->input->mir_artifacts->length; i++) {
        InputScriptArtifact* existing =
            (InputScriptArtifact*)lease->input->mir_artifacts->data[i];
        if (existing && existing->key == lease->mir_key) {
            pthread_mutex_unlock(&cache->mutex);
            return false;
        }
    }
    InputScriptArtifact* artifact = (InputScriptArtifact*)mem_calloc(1,
        sizeof(InputScriptArtifact), MEM_CAT_CACHE_OTHER);
    if (!artifact) {
        pthread_mutex_unlock(&cache->mutex);
        return false;
    }
    artifact->value = mir;
    if (ops) artifact->ops = *ops;
    artifact->key = lease->mir_key;
    artifact->retained_bytes = ops && ops->retained_bytes
        ? ops->retained_bytes(mir) : 0;
    if (!arraylist_append(lease->input->mir_artifacts, artifact)) {
        mem_free(artifact);
        pthread_mutex_unlock(&cache->mutex);
        return false;
    }
    cache->stats.mir_builds++;
    cache->stats.retained_mir_bytes += artifact->retained_bytes;
    cache_update_peak(cache);
    cache_enforce_retention_limit_locked(cache);
    pthread_mutex_unlock(&cache->mutex);
    return true;
}

static void cache_mark_counter(InputScriptCache* cache, uint64_t* counter) {
    if (!cache || !counter) return;
    pthread_mutex_lock(&cache->mutex);
    (*counter)++;
    pthread_mutex_unlock(&cache->mutex);
}

void input_script_cache_mark_module_hit(InputScriptCache* cache) {
    cache_mark_counter(cache, &cache->stats.module_hits);
}

void input_script_cache_mark_dependency_invalidation(InputScriptCache* cache) {
    cache_mark_counter(cache, &cache->stats.dependency_invalidations);
}

void input_script_cache_mark_rejected(InputScriptCache* cache) {
    cache_mark_counter(cache, &cache->stats.rejected);
}

void input_script_cache_mark_poisoned(InputScriptCache* cache) {
    cache_mark_counter(cache, &cache->stats.poisoned);
}

void input_script_cache_get_stats(InputScriptCache* cache,
        InputScriptCacheStats* out_stats) {
    if (!out_stats) return;
    memset(out_stats, 0, sizeof(*out_stats));
    if (!cache) return;
    pthread_mutex_lock(&cache->mutex);
    *out_stats = cache->stats;
    pthread_mutex_unlock(&cache->mutex);
}

void input_script_cache_log_summary(InputScriptCache* cache) {
    if (!cache) return;
    InputScriptCacheStats stats;
    input_script_cache_get_stats(cache, &stats);
    log_notice("script-cache: summary entries=%llu source_lookups=%llu source_hits=%llu source_misses=%llu ast_hits=%llu ast_misses=%llu mir_hits=%llu mir_misses=%llu invalidations=%llu waits=%llu poisoned=%llu evictions=%llu retention_limit=%llu retention_pressure=%llu retained_source=%llu retained_ast=%llu retained_mir=%llu peak=%llu",
        (unsigned long long)stats.retained_entries,
        (unsigned long long)stats.source_lookups,
        (unsigned long long)stats.source_hits,
        (unsigned long long)stats.source_misses,
        (unsigned long long)stats.ast_hits,
        (unsigned long long)stats.ast_misses,
        (unsigned long long)stats.mir_hits,
        (unsigned long long)stats.mir_misses,
        (unsigned long long)stats.invalidations,
        (unsigned long long)stats.single_flight_waits,
        (unsigned long long)stats.poisoned,
        (unsigned long long)stats.evictions,
        (unsigned long long)stats.retention_limit_bytes,
        (unsigned long long)stats.retention_pressure,
        (unsigned long long)stats.retained_source_bytes,
        (unsigned long long)stats.retained_ast_bytes,
        (unsigned long long)stats.retained_mir_bytes,
        (unsigned long long)stats.peak_bytes);
}
