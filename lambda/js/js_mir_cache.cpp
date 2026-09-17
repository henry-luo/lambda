#include "js_transpiler.hpp"
#include "js_mir_internal.hpp"
#include "../jube/jube_interface.h"
#include "../input/input.hpp"

#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/hash.h"
#include "../../lib/hashmap_helpers.h"
#include "../../lib/shell.h"
#include "../../lib/arraylist.h"

#include <cstring>

struct JsMirLeaseSession {
    InputScriptCache* script_cache;
    ArrayList* held_scopes;
    JsMirLeaseSessionStats stats;
};

static uint64_t js_mir_lease_preamble_abi_hash(const JsPreambleState* preamble) {
    // DOM4 ordinal sites bake registry-local slots and declaration ordinals;
    // a changed interface must invalidate an otherwise identical source.
    uint64_t hash = hash_combine_u64(0xcbf29ce484222325ULL,
                                     jube_interface_registry_digest());
    if (!preamble) return hash;

    hash = hash_combine_u64(hash, (uint64_t)preamble->module_var_count);
    hash = hash_combine_u64(hash, 0x4e494431u);
    hash = hash_combine_u64(hash, (uint64_t)preamble->module_property_count);
    hash = hash_combine_u64(hash, (uint64_t)preamble->module_property_bytes_size);
    if (preamble->module_property_specs && preamble->module_property_bytes_size > 0) {
        hash = hash_combine_u64(hash, hashmap_hash_xxhash3_bytes(
            preamble->module_property_specs,
            preamble->module_property_bytes_size, 0, 0));
    }
    for (int i = 0; preamble->entries && i < preamble->entry_count; i++) {
        const JsModuleConstEntry* entry = &preamble->entries[i];
        size_t name_len = entry->name ? strlen(entry->name) : 0;
        hash = hash_combine_u64(hash, hashmap_hash_xxhash3_bytes(entry->name, name_len, 0, 0));
        hash = hash_combine_u64(hash, (uint64_t)entry->const_type);
        hash = hash_combine_u64(hash, (uint64_t)entry->int_val);
        hash = hash_combine_u64(hash, (uint64_t)entry->var_kind);
        hash = hash_combine_u64(hash, (uint64_t)entry->modvar_type);
        hash = hash_combine_u64(hash, entry->is_nested_func_hoist ? 1 : 0);
        hash = hash_combine_u64(hash, entry->is_iife_func_decl ? 1 : 0);
        hash = hash_combine_u64(hash, entry->is_live_default_binding ? 1 : 0);
    }
    return hash;
}

static InputScriptRequest js_mir_lease_request(bool preamble_mode,
        const char* source, size_t source_len, const char* filename,
        const JsPreambleState* preamble) {
    InputScriptRequest request = {};
    request.identity = filename ? filename : "<string>";
    request.source = source ? source : "";
    request.source_length = source ? source_len : 0;
    request.source_kind = preamble_mode ? INPUT_SCRIPT_SOURCE_HARNESS
        : INPUT_SCRIPT_SOURCE_FILE;
    request.language = "javascript";
    request.profile = "js-mir";
    request.parser_abi = "js-direct-parser-v1";
    request.parse_flags = preamble_mode ? "preamble" : "classic";
    request.resolution_base = request.identity;
    request.backend = "mir";
    request.execution_mode = preamble_mode ? "preamble" : "classic";
    request.ast_abi = 1;
    request.compiler_abi = 1;
    request.interface_abi = js_mir_lease_preamble_abi_hash(preamble);
    request.dependency_digest = jube_interface_registry_digest();
    request.optimize_level = g_js_mir_optimize_level;
    request.module_mode = false;
    return request;
}

static InputScriptLease* js_mir_lease_acquire(JsMirLeaseSession* session,
        bool preamble_mode, const char* source, size_t source_len,
        const char* filename, const JsPreambleState* preamble,
        InputCacheScope** out_scope) {
    if (out_scope) *out_scope = nullptr;
    if (!session || !session->script_cache) return nullptr;
    InputCacheScope* scope = input_script_cache_open_scope(session->script_cache);
    if (!scope) return nullptr;
    InputScriptRequest request = js_mir_lease_request(preamble_mode, source,
        source_len, filename, preamble);
    InputScriptLease* lease = input_script_cache_acquire(scope, &request);
    if (!lease) {
        input_script_cache_close_scope(scope);
        return nullptr;
    }
    if (out_scope) *out_scope = scope;
    return lease;
}

static void js_mir_lease_destroy_artifact(void* value) {
    JsPreambleState* state = (JsPreambleState*)value;
    if (!state) return;
    preamble_state_destroy(state);
    mem_free(state);
}

static size_t js_mir_lease_artifact_bytes(const void* value) {
    const JsPreambleState* state = (const JsPreambleState*)value;
    if (!state) return 0;
    size_t bytes = sizeof(JsPreambleState) +
        (size_t)(state->entry_count > 0 ? state->entry_count : 0) *
        sizeof(JsModuleConstEntry) + state->module_property_bytes_size;
    if (state->source_buffer) bytes += strlen(state->source_buffer) + 1;
    return bytes;
}

static bool js_mir_lease_hold_scope(JsMirLeaseSession* session, InputCacheScope* scope) {
    return session && session->held_scopes && scope &&
        arraylist_append(session->held_scopes, scope);
}

static InputScriptBuildClaim js_common_mir_cache_begin_build(
        InputScriptCache* cache, const InputScriptRequest* request,
        InputScriptBuildScope* build) {
    return input_script_build_scope_begin(build, cache, request,
        INPUT_SCRIPT_BUILD_MIR);
}

static void js_common_mir_cache_complete_build(InputScriptBuildScope* build,
        bool published, bool poison) {
    input_script_build_scope_complete(build, published, poison);
}

JsMirLeaseSession* js_mir_lease_session_create(void) {
    InputScriptCache* script_cache = input_manager_global_script_cache();
    const char* js_alias = shell_getenv("LAMBDA_DISABLE_JS_MIR_CACHE");
    bool js_alias_disabled = js_alias &&
        (strcmp(js_alias, "1") == 0 || strcmp(js_alias, "true") == 0);
    if (!input_script_cache_mir_enabled(script_cache) || js_alias_disabled) {
        log_info("js-mir-lease: common MIR policy is disabled");
        return nullptr;
    }
    JsMirLeaseSession* session = (JsMirLeaseSession*)mem_calloc(1,
        sizeof(JsMirLeaseSession),
        MEM_CAT_JS_RUNTIME);
    if (!session) return nullptr;
    session->script_cache = script_cache;
    session->held_scopes = arraylist_new(8);
    if (!session->script_cache) {
        log_error("js-mir-lease: common script cache is unavailable");
        arraylist_free(session->held_scopes);
        mem_free(session);
        return nullptr;
    }
    if (!session->held_scopes) {
        log_error("js-mir-lease: failed to allocate lease list");
        mem_free(session);
        return nullptr;
    }
    log_info("js-mir-lease: using InputManager script cache");
    return session;
}

void js_mir_lease_session_close(JsMirLeaseSession* session) {
    if (!session) return;
    log_notice("js_mir_lease_summary: lookups=%llu hits=%llu misses=%llu compiles=%llu instantiations=%llu retained_entries=%zu retained_metadata_bytes=%zu",
        (unsigned long long)session->stats.lookups,
        (unsigned long long)session->stats.hits,
        (unsigned long long)session->stats.misses,
        (unsigned long long)session->stats.compiles,
        (unsigned long long)session->stats.instantiations,
        session->stats.retained_entries,
        session->stats.retained_metadata_bytes);
    if (session->held_scopes) {
        for (int i = 0; i < session->held_scopes->length; i++) {
            InputCacheScope* scope = (InputCacheScope*)session->held_scopes->data[i];
            input_script_cache_close_scope(scope);
        }
        arraylist_free(session->held_scopes);
        session->held_scopes = nullptr;
    }
    // inputmanager owns the persistent artifacts after the adapter releases
    // its execution leases.
    mem_free(session);
}

const JsPreambleState* js_mir_lease_session_lookup(
        JsMirLeaseSession* session, bool preamble_mode, const char* source,
        size_t source_len, const char* filename,
        const JsPreambleState* preamble) {
    if (!session || !session->script_cache) return nullptr;
    session->stats.lookups++;
    InputCacheScope* scope = nullptr;
    InputScriptLease* lease = js_mir_lease_acquire(session, preamble_mode,
        source, source_len, filename, preamble, &scope);
    if (!lease) {
        session->stats.misses++;
        return nullptr;
    }
    void* value = nullptr;
    bool hit = input_script_cache_get_mir(lease, &value);
    const JsPreambleState* result = hit ? (const JsPreambleState*)value : nullptr;
    if (hit) {
        if (!js_mir_lease_hold_scope(session, scope)) {
            session->stats.misses++;
            input_script_cache_close_scope(scope);
            return nullptr;
        }
        session->stats.hits++;
    }
    else {
        session->stats.misses++;
        input_script_cache_close_scope(scope);
    }
    return result;
}

InputScriptBuildClaim js_mir_lease_session_begin_build(
        JsMirLeaseSession* session, bool preamble_mode, const char* source,
        size_t source_len, const char* filename,
        const JsPreambleState* preamble, InputScriptBuildScope* build) {
    InputScriptRequest request = js_mir_lease_request(preamble_mode, source,
        source_len, filename, preamble);
    return js_common_mir_cache_begin_build(session ? session->script_cache : NULL,
        &request, build);
}

void js_mir_lease_session_complete_build(InputScriptBuildScope* build,
        bool published, bool poison) {
    js_common_mir_cache_complete_build(build, published, poison);
}

const JsPreambleState* js_mir_lease_session_adopt(
        JsMirLeaseSession* session, bool preamble_mode, const char* source,
        size_t source_len, const char* filename,
        const JsPreambleState* preamble, JsPreambleState* compiled_state) {
    if (!session || !session->script_cache || !compiled_state ||
            !compiled_state->owns_compiled_state || !compiled_state->mir_ctx) {
        return nullptr;
    }

    InputCacheScope* scope = nullptr;
    InputScriptLease* lease = js_mir_lease_acquire(session, preamble_mode,
        source, source_len, filename, preamble, &scope);
    if (!lease) return nullptr;
    void* existing = nullptr;
    if (input_script_cache_get_mir(lease, &existing)) {
        input_script_cache_close_scope(scope);
        return nullptr;
    }

    JsPreambleState* retained = (JsPreambleState*)mem_alloc(
        sizeof(JsPreambleState), MEM_CAT_JS_RUNTIME);
    if (!retained) {
        input_script_cache_close_scope(scope);
        return nullptr;
    }
    *retained = *compiled_state;
    InputScriptArtifactOps ops = {
        js_mir_lease_destroy_artifact,
        js_mir_lease_artifact_bytes,
    };
    if (!js_mir_lease_hold_scope(session, scope)) {
        js_mir_lease_destroy_artifact(retained);
        input_script_cache_close_scope(scope);
        return nullptr;
    }
    bool published = input_script_cache_publish_mir(lease, retained, &ops);
    if (!published) {
        arraylist_remove(session->held_scopes, session->held_scopes->length - 1);
        js_mir_lease_destroy_artifact(retained);
        input_script_cache_close_scope(scope);
        return nullptr;
    }
    memset(compiled_state, 0, sizeof(*compiled_state));
    session->stats.compiles++;
    session->stats.retained_entries++;
    session->stats.retained_metadata_bytes += js_mir_lease_artifact_bytes(retained);
    return retained;
}

const JsPreambleState* js_mir_lease_session_adopt_build(
        JsMirLeaseSession* session, InputScriptBuildScope* build,
        JsPreambleState* compiled_state) {
    if (!session || !build || build->state != INPUT_SCRIPT_BUILD_OWNER ||
            !build->scope || !build->lease || !compiled_state ||
            !compiled_state->owns_compiled_state || !compiled_state->mir_ctx) {
        return nullptr;
    }
    void* existing = nullptr;
    if (input_script_cache_get_mir(build->lease, &existing)) return nullptr;
    JsPreambleState* retained = (JsPreambleState*)mem_alloc(
        sizeof(JsPreambleState), MEM_CAT_JS_RUNTIME);
    if (!retained) return nullptr;
    *retained = *compiled_state;
    InputScriptArtifactOps ops = {
        js_mir_lease_destroy_artifact,
        js_mir_lease_artifact_bytes,
    };
    InputCacheScope* scope = build->scope;
    if (!js_mir_lease_hold_scope(session, scope)) {
        js_mir_lease_destroy_artifact(retained);
        return nullptr;
    }
    build->scope = nullptr;
    if (!input_script_cache_publish_mir(build->lease, retained, &ops)) {
        arraylist_remove(session->held_scopes, session->held_scopes->length - 1);
        build->scope = scope;
        js_mir_lease_destroy_artifact(retained);
        return nullptr;
    }
    memset(compiled_state, 0, sizeof(*compiled_state));
    session->stats.compiles++;
    session->stats.retained_entries++;
    session->stats.retained_metadata_bytes += js_mir_lease_artifact_bytes(retained);
    return retained;
}

void js_mir_lease_session_record_instantiation(JsMirLeaseSession* session) {
    if (session) session->stats.instantiations++;
}

static InputScriptRequest js_module_mir_request(const char* source,
        size_t source_len, const char* filename) {
    InputScriptRequest request = {};
    request.identity = filename ? filename : "<module>";
    request.source = source ? source : "";
    request.source_length = source ? source_len : 0;
    request.source_kind = js_path_is_http_url(filename)
        ? INPUT_SCRIPT_SOURCE_URL
        : (filename && filename[0] != '<'
            ? INPUT_SCRIPT_SOURCE_FILE : INPUT_SCRIPT_SOURCE_INLINE);
    request.language = "javascript";
    request.profile = "js-mir-module";
    request.parser_abi = "js-direct-parser-v1";
    request.parse_flags = "module";
    request.resolution_base = request.identity;
    request.backend = "mir";
    request.execution_mode = "module";
    request.ast_abi = 1;
    request.compiler_abi = 1;
    request.interface_abi = jube_interface_registry_digest();
    request.dependency_digest = jube_interface_registry_digest();
    request.optimize_level = g_js_mir_optimize_level;
    request.module_mode = true;
    return request;
}

void js_module_mir_artifact_destroy(JsModuleMirArtifact* artifact) {
    if (!artifact) return;
    preamble_state_destroy(&artifact->image);
    for (int i = 0; i < artifact->static_dependency_count; i++) {
        mem_free(artifact->static_dependency_paths
            ? artifact->static_dependency_paths[i] : NULL);
    }
    mem_free(artifact->static_dependency_paths);
    mem_free(artifact);
}

static void js_module_mir_cache_destroy_artifact(void* value) {
    js_module_mir_artifact_destroy((JsModuleMirArtifact*)value);
}

static size_t js_module_mir_cache_artifact_bytes(const void* value) {
    const JsModuleMirArtifact* artifact = (const JsModuleMirArtifact*)value;
    const JsPreambleState* image = artifact ? &artifact->image : nullptr;
    if (!image) return 0;
    size_t bytes = sizeof(JsModuleMirArtifact) +
        (size_t)(image->entry_count > 0 ? image->entry_count : 0) *
        sizeof(JsModuleConstEntry) + image->module_property_bytes_size;
    if (image->source_buffer) bytes += strlen(image->source_buffer) + 1;
    bytes += (size_t)(artifact->static_dependency_count > 0
        ? artifact->static_dependency_count : 0) * sizeof(char*);
    for (int i = 0; i < artifact->static_dependency_count; i++) {
        const char* path = artifact->static_dependency_paths
            ? artifact->static_dependency_paths[i] : NULL;
        if (path) bytes += strlen(path) + 1;
    }
    return bytes;
}

const JsModuleMirArtifact* js_module_mir_cache_lookup(const char* source,
        size_t source_len, const char* filename, InputCacheScope** out_scope) {
    if (out_scope) *out_scope = nullptr;
    InputScriptCache* cache = input_manager_global_script_cache();
    if (!cache || !input_script_cache_mir_enabled(cache)) return nullptr;
    InputCacheScope* scope = input_script_cache_open_scope(cache);
    if (!scope) return nullptr;
    InputScriptRequest request = js_module_mir_request(source, source_len, filename);
    InputScriptLease* lease = input_script_cache_acquire(scope, &request);
    if (!lease) {
        input_script_cache_close_scope(scope);
        return nullptr;
    }
    bool dependencies_changed = false;
    uint32_t unit_id = input_script_compilation_unit_id(
        input_script_lease_input(lease));
    if (!input_script_cache_refresh_file_dependencies(cache, unit_id,
            &dependencies_changed) || dependencies_changed) {
        input_script_cache_close_scope(scope);
        return nullptr;
    }
    void* value = nullptr;
    if (!input_script_cache_get_mir(lease, &value) || !value) {
        input_script_cache_close_scope(scope);
        return nullptr;
    }
    if (out_scope) *out_scope = scope;
    else input_script_cache_close_scope(scope);
    return (const JsModuleMirArtifact*)value;
}

InputScriptBuildClaim js_module_mir_cache_begin_build(const char* source,
        size_t source_len, const char* filename, InputScriptBuildScope* build) {
    InputScriptCache* cache = input_manager_global_script_cache();
    InputScriptRequest request = js_module_mir_request(source, source_len, filename);
    return js_common_mir_cache_begin_build(cache, &request, build);
}

void js_module_mir_cache_complete_build(InputScriptBuildScope* build,
        bool published, bool poison) {
    js_common_mir_cache_complete_build(build, published, poison);
}

bool js_module_mir_cache_record_dependency(const char* importer_source,
        size_t importer_source_len, const char* importer_filename,
        const char* dependency_source, size_t dependency_source_len,
        const char* dependency_filename) {
    InputScriptCache* cache = input_manager_global_script_cache();
    if (!cache || !input_script_cache_mir_enabled(cache)) return true;
    if (!importer_source || !importer_filename || !importer_filename[0] ||
            !dependency_source || !dependency_filename || !dependency_filename[0]) {
        return false;
    }
    InputCacheScope* scope = input_script_cache_open_scope(cache);
    if (!scope) return false;
    InputScriptRequest importer_request = js_module_mir_request(importer_source,
        importer_source_len, importer_filename);
    InputScriptRequest dependency_request = js_module_mir_request(dependency_source,
        dependency_source_len, dependency_filename);
    InputScriptLease* importer = input_script_cache_acquire(scope,
        &importer_request);
    InputScriptLease* dependency = input_script_cache_acquire(scope,
        &dependency_request);
    bool linked = false;
    if (importer && dependency) {
        linked = input_script_cache_record_dependency_by_unit(cache,
            input_script_compilation_unit_id(input_script_lease_input(importer)),
            input_script_compilation_unit_id(input_script_lease_input(dependency)));
    }
    input_script_cache_close_scope(scope);
    if (!linked) {
        log_error("js-mir-module-cache: failed to record dependency %s -> %s",
            importer_filename, dependency_filename);
    }
    return linked;
}

const JsModuleMirArtifact* js_module_mir_cache_adopt(const char* source,
        size_t source_len, const char* filename, JsModuleMirArtifact* compiled,
        InputCacheScope** out_scope) {
    if (out_scope) *out_scope = nullptr;
    if (!compiled || !compiled->image.owns_compiled_state ||
            !compiled->image.mir_ctx || !compiled->image.entry_func) {
        return nullptr;
    }
    InputScriptCache* cache = input_manager_global_script_cache();
    if (!cache || !input_script_cache_mir_enabled(cache)) return nullptr;
    InputCacheScope* scope = input_script_cache_open_scope(cache);
    if (!scope) return nullptr;
    InputScriptRequest request = js_module_mir_request(source, source_len, filename);
    InputScriptLease* lease = input_script_cache_acquire(scope, &request);
    if (!lease) {
        input_script_cache_close_scope(scope);
        return nullptr;
    }
    void* existing = nullptr;
    if (input_script_cache_get_mir(lease, &existing)) {
        input_script_cache_close_scope(scope);
        return nullptr;
    }
    InputScriptArtifactOps ops = {
        js_module_mir_cache_destroy_artifact,
        js_module_mir_cache_artifact_bytes,
    };
    if (!input_script_cache_publish_mir(lease, compiled, &ops)) {
        input_script_cache_close_scope(scope);
        return nullptr;
    }
    if (out_scope) *out_scope = scope;
    else input_script_cache_close_scope(scope);
    return compiled;
}

const JsModuleMirArtifact* js_module_mir_cache_adopt_build(
        InputScriptBuildScope* build, JsModuleMirArtifact* compiled,
        InputCacheScope** out_scope) {
    if (!out_scope) return nullptr;
    *out_scope = nullptr;
    if (!build || build->state != INPUT_SCRIPT_BUILD_OWNER || !build->scope ||
            !build->lease || !compiled || !compiled->image.owns_compiled_state ||
            !compiled->image.mir_ctx || !compiled->image.entry_func) {
        return nullptr;
    }
    void* existing = nullptr;
    if (input_script_cache_get_mir(build->lease, &existing)) return nullptr;
    InputScriptArtifactOps ops = {
        js_module_mir_cache_destroy_artifact,
        js_module_mir_cache_artifact_bytes,
    };
    if (!input_script_cache_publish_mir(build->lease, compiled, &ops)) {
        return nullptr;
    }
    InputCacheScope* scope = build->scope;
    build->scope = nullptr;
    *out_scope = scope;
    return compiled;
}
