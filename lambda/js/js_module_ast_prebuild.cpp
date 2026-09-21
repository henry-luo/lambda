#include "js_interp.hpp"
#include "js_mir_internal.hpp"
#include "js_transpiler.hpp"
#include "../input/input-script-cache.h"
#include "../runtime/module_ast_prebuild.hpp"
#include "../runtime/transpiler.hpp"
#include "../../lib/file.h"
#include "../../lib/mem.h"

#include <limits.h>
#include <string.h>

static bool js_ast_prebuild_append_specifier(ArrayList* specifiers,
        const String* source) {
    if (!specifiers || !source) return false;
    char* specifier = mem_dup_n(source->chars, source->len, MEM_CAT_SYSTEM);
    if (!specifier || !arraylist_append(specifiers, specifier)) {
        mem_free(specifier);
        return false;
    }
    return true;
}

static ArrayList* js_ast_prebuild_discover_imports(void* opaque,
        const char* source, size_t source_length) {
    (void)opaque;
    if (!source || source_length > INT_MAX) return NULL;
    ArrayList* specifiers = arraylist_new(4);
    if (!specifiers) return NULL;
    JsTranspiler* transpiler = js_transpiler_create(NULL);
    if (!transpiler || !js_transpiler_parse_c(transpiler, source, source_length,
            JS_PARSE_AUTO) || !transpiler->ast_root) {
        js_transpiler_destroy(transpiler);
        arraylist_free(specifiers);
        return NULL;
    }
    JsProgramNode* program = transpiler->ast_root->node_type == AST_SCRIPT
        ? (JsProgramNode*)transpiler->ast_root : NULL;
    bool complete = true;
    for (JsAstNode* statement = program ? program->body : NULL;
            statement; statement = statement->next) {
        String* module_source = NULL;
        if (statement->node_type == AST_NODE_IMPORT) {
            module_source = ((JsImportNode*)statement)->source;
        } else if (statement->node_type == AST_NODE_EXPORT) {
            module_source = ((JsExportNode*)statement)->source;
        }
        if (module_source && !js_ast_prebuild_append_specifier(specifiers,
                module_source)) {
            complete = false;
            break;
        }
    }
    js_transpiler_destroy(transpiler);
    if (!complete) {
        for (int index = 0; index < specifiers->length; index++) {
            mem_free(specifiers->data[index]);
        }
        arraylist_free(specifiers);
        return NULL;
    }
    return specifiers;
}

static bool js_ast_prebuild_resolve_import(void* opaque,
        const char* importer_path, const char* specifier,
        ModuleAstResolvedImport* out) {
    (void)opaque;
    if (!out || !importer_path || !specifier || !specifier[0] ||
            strlen(specifier) > INT_MAX) return false;
    char resolved[2048];
    jm_resolve_module_path(importer_path, specifier, (int)strlen(specifier),
        resolved, sizeof(resolved));
    // The AST executor currently reaches Lambda exports through the native
    // bridge. Keep that adapter on its established execution path rather than
    // publishing an AST image it cannot yet call.
    if (jm_path_is_lambda_source(resolved) || !file_exists(resolved)) return false;
    out->path = mem_strdup(resolved, MEM_CAT_SYSTEM);
    out->language = MODULE_AST_LANGUAGE_JAVASCRIPT;
    return out->path != NULL;
}

static bool js_ast_prebuild_build_module(void* opaque, const char* path) {
    (void)opaque;
    Runtime worker = {};
    runtime_init(&worker);
    size_t source_length = 0;
    char* source = js_load_script_source_from_cache(path,
        "js-ast-prebuild", "ast-template", true, &source_length);
    JsScript* script = source ? js_interp_prepare_es_module_script(&worker, source,
        source_length, path) : NULL;
    // The cache admission predicate is the AST executor's compatibility
    // predicate. A prebuilt closure is usable by AUTO only when every worker
    // published (or reused) such an image.
    bool built = script && script->ast_root &&
        js_interp_script_is_supported(script) &&
        (script->cache_owned_template || script->cache_template);
    runtime_cleanup_ast_prebuild_worker(&worker);
    mem_free(source);
    return built;
}

bool js_module_ast_prebuild_imports(const char* filename, const char* source,
        size_t source_length) {
    if (!filename || !source || !input_script_cache_ast_enabled(
            input_manager_global_script_cache())) return false;
    ModuleAstPrebuildProfile profile = {
        "javascript", MODULE_AST_LANGUAGE_JAVASCRIPT,
        js_ast_prebuild_discover_imports, js_ast_prebuild_resolve_import,
        js_ast_prebuild_build_module, NULL,
    };
    ModuleAstPrebuildProfiles profiles = {};
    profiles.profiles[MODULE_AST_LANGUAGE_JAVASCRIPT] = &profile;
    ModuleAstPrebuildStats stats = {};
    bool scheduled = module_ast_prebuild_imports(&profiles,
        MODULE_AST_LANGUAGE_JAVASCRIPT, filename, source, source_length,
        &stats);
    return scheduled && stats.failed_modules == 0;
}
