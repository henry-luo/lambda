#include "js_transpiler.hpp"
#include "js_execution_internal.hpp"
#include "mvp-lmd/mvp_lmd.h"
#include "../jube/jube_interface.h"
#include "../input/input.hpp"

#include "../../lib/log.h"
#include "../../lib/mem.h"

#include <cstring>

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
    request.compiler_abi = 3;
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
    size_t bytes = sizeof(JsModuleMirArtifact);
    bytes += mvp_lmd_code_bytes(image->mvp_code);
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
    return input_script_build_scope_begin(build, cache, &request,
        INPUT_SCRIPT_BUILD_MIR);
}

void js_module_mir_cache_complete_build(InputScriptBuildScope* build,
        bool published, bool poison) {
    input_script_build_scope_complete(build, published, poison);
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
