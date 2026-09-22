#include "js_transpiler.hpp"
#include "js_interp.hpp"
#include "js_function.hpp"
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/mem_factory.h"
#include "../../lib/strbuf.h"
#include "../../lib/mempool.h"
#include "../../lib/hashmap.h"
#include "../../lib/hashmap_typed.hpp"
#include "../input/input-script-cache.h"
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include "../../lib/mem.h"

static void js_script_destroy_extension(Script* base_script);

struct JsScopeBindingIndexEntry {
    JsScope* scope;
    String* name;
    NameEntry* binding;
};

typedef TypedHashMap<JsScopeBindingIndexEntry,
    HashMapIdentity2MemberKeyOps<JsScopeBindingIndexEntry,
        &JsScopeBindingIndexEntry::scope,
        &JsScopeBindingIndexEntry::name>> JsScopeBindingIndex;

// The shared indexer sees JavaScript's extension nodes through this immutable
// profile. Worker parsers may therefore share it without a first-use race.
LangProfile js_profile = { "js", js_ast_publish_extension_facts,
    js_ast_visit_extension_children };

static InputScriptRequest js_common_ast_cache_request(const char* source,
        size_t source_length, const char* reference, bool strict,
        bool typescript_profile, bool module_parse) {
    const char* identity = reference ? reference : "<inline-js>";
    InputScriptRequest request = {};
    request.identity = identity;
    request.source = source;
    request.source_length = source_length;
    request.source_kind = identity[0] == '<'
        ? INPUT_SCRIPT_SOURCE_INLINE : INPUT_SCRIPT_SOURCE_FILE;
    request.language = "javascript";
    request.profile = typescript_profile ? "typescript-ast" : "javascript-ast";
    request.parser_abi = "js-direct-parser-v1";
    request.parse_flags = strict ? "strict" : "sloppy";
    request.resolution_base = identity;
    request.backend = "ast";
    request.execution_mode = "ast-template";
    request.ast_abi = 1;
    // A module grammar creates a different global scope graph even when the
    // source text contains no import or export declaration.
    request.module_mode = module_parse;
    return request;
}

static bool js_common_ast_cache_eligible(const JsScript* script,
        bool typescript_profile) {
    (void)typescript_profile;
    // Cache only templates that can begin execution at T0. AUTO falls back to
    // MIR for the remaining shapes, so retaining them here would make a cache
    // hit change that execution policy.
    return script && js_interp_script_is_supported((JsScript*)script);
}

static void js_common_ast_cache_destroy(void* value) {
    runtime_destroy_cached_script_template((Script*)value);
}

static size_t js_common_ast_cache_artifact_bytes(const void* value) {
    return value ? sizeof(JsScript) : 0;
}

static JsScript* js_common_ast_cache_clone(Runtime* runtime,
        const JsScript* cached, InputCacheScope* scope) {
    if (!runtime || !cached || !scope) return NULL;
    JsScript* instance = (JsScript*)mem_calloc(1, sizeof(JsScript), MEM_CAT_SYSTEM);
    if (!instance) return NULL;
    memcpy(instance, cached, sizeof(JsScript));
    instance->reference = cached->reference
        ? mem_strdup(cached->reference, MEM_CAT_SYSTEM) : NULL;
    instance->directory = cached->directory
        ? mem_strdup(cached->directory, MEM_CAT_SYSTEM) : NULL;
    if ((cached->reference && !instance->reference) ||
            (cached->directory && !instance->directory)) {
        mem_free((void*)instance->reference);
        mem_free((void*)instance->directory);
        mem_free(instance);
        return NULL;
    }
    AstIndex cached_index = cached->ast_index;
    memset(&instance->ast_index, 0, sizeof(instance->ast_index));
    instance->ast_overlay_pool = mem_pool_create(NULL, MEM_ROLE_AST,
        "js.ast.overlay");
    if (!instance->ast_overlay_pool || !ast_index_clone(&instance->ast_index,
            &cached_index)) {
        if (instance->ast_overlay_pool) pool_destroy(instance->ast_overlay_pool);
        mem_free((void*)instance->reference);
        mem_free((void*)instance->directory);
        mem_free(instance);
        return NULL;
    }
    instance->cache_template = (const Script*)cached;
    instance->cache_scope = scope;
    instance->cache_owned_template = false;
    // The parser image names one process-wide logical unit; runtime_register
    // below maps it to this Runtime's dense module slab (D8.5.1v2).
    instance->cache_compilation_unit_id = cached->cache_compilation_unit_id;
    instance->is_loading = false;
    instance->is_retired = false;
    instance->ast_overlay_name_pool = NULL;
    instance->ast_index_overlay = true;
    instance->type_registry = NULL;
    // Module declaration instantiation belongs to this fresh execution even
    // though the parse-time module shape itself is shared.
    instance->es_module_scope_initialized = false;
    instance->ast_callables = NULL;
    instance->field_initializers = NULL;
    runtime_register_script(runtime, (Script*)instance);
    return instance;
}

Pool* js_script_execution_pool(JsScript* script) {
    return script && script->ast_overlay_pool ? script->ast_overlay_pool
        : script ? script->pool : NULL;
}

NamePool* js_script_execution_name_pool(JsScript* script) {
    if (!script) return NULL;
    if (!script->ast_overlay_pool) return script->name_pool;
    if (!script->ast_overlay_name_pool) {
        script->ast_overlay_name_pool = name_pool_create(script->ast_overlay_pool,
            NULL);
    }
    return script->ast_overlay_name_pool;
}

JsScript* js_common_ast_cache_lookup(Runtime* runtime, const char* source,
        size_t source_length, const char* reference, bool strict,
        bool typescript_profile, bool module_parse) {
    InputScriptCache* cache = input_manager_global_script_cache();
    if (!runtime || !source || !input_script_cache_ast_enabled(cache)) return NULL;
    InputCacheScope* scope = input_script_cache_open_scope(cache);
    InputScriptRequest request = js_common_ast_cache_request(source, source_length,
        reference, strict, typescript_profile, module_parse);
    InputScriptLease* lease = input_script_cache_acquire(scope, &request);
    void* value = NULL;
    if (!lease || !input_script_cache_get_ast(lease, &value)) {
        input_script_cache_close_scope(scope);
        return NULL;
    }
    JsScript* cached = (JsScript*)value;
    JsScript* instance = js_common_ast_cache_eligible(cached, typescript_profile)
        ? js_common_ast_cache_clone(runtime, cached, scope) : NULL;
    if (!instance) {
        input_script_cache_close_scope(scope);
        return NULL;
    }
    input_script_cache_mark_module_hit(cache);
    log_info("js common ast cache: hit script=%s", reference ? reference : "<inline-js>");
    return instance;
}

InputScriptBuildClaim js_common_ast_cache_begin_build(InputScriptBuildScope* build,
        const char* source, size_t source_length, const char* reference,
        bool strict, bool typescript_profile, bool module_parse) {
    InputScriptCache* cache = input_manager_global_script_cache();
    if (!source || !input_script_cache_ast_enabled(cache)) {
        input_script_build_scope_reset(build);
        return build ? build->state : INPUT_SCRIPT_BUILD_BYPASS;
    }
    InputScriptRequest request = js_common_ast_cache_request(source,
        source_length, reference, strict, typescript_profile, module_parse);
    return input_script_build_scope_begin(build, cache, &request,
        INPUT_SCRIPT_BUILD_AST);
}

void js_common_ast_cache_complete_build(InputScriptBuildScope* build,
        bool published, bool poison) {
    input_script_build_scope_complete(build, published, poison);
}

bool js_common_ast_cache_admit(Runtime* runtime, JsScript* script, const char* source,
        size_t source_length, const char* reference, bool strict,
        bool typescript_profile, bool module_parse) {
    InputScriptCache* cache = input_manager_global_script_cache();
    if (!runtime || !source || !input_script_cache_ast_enabled(cache) ||
            !js_common_ast_cache_eligible(script, typescript_profile)) return false;
    InputCacheScope* scope = input_script_cache_open_scope(cache);
    InputScriptRequest request = js_common_ast_cache_request(source, source_length,
        reference, strict, typescript_profile, module_parse);
    InputScriptLease* lease = input_script_cache_acquire(scope, &request);
    void* existing = NULL;
    if (!lease || input_script_cache_get_ast(lease, &existing)) {
        input_script_cache_close_scope(scope);
        return false;
    }
    uint32_t unit_id = input_script_compilation_unit_id(
        input_script_lease_input(lease));
    if (!unit_id || !runtime_module_state_bind_unit(runtime, unit_id,
            script->module_state_id)) {
        input_script_cache_mark_rejected(cache);
        input_script_cache_close_scope(scope);
        return false;
    }
    InputScriptArtifactOps ops = {js_common_ast_cache_destroy,
        js_common_ast_cache_artifact_bytes};
    if (!input_script_cache_publish_ast(lease, script, &ops)) {
        runtime_module_state_unbind_unit(runtime, unit_id, script->module_state_id);
        input_script_cache_close_scope(scope);
        return false;
    }
    // The artifact is immutable, while every clone gets a fresh dense slab.
    // The temporary runtime binding above is removed at template teardown.
    script->cache_compilation_unit_id = unit_id;
    script->cache_owned_template = true;
    script->cache_scope = scope;
    log_info("js common ast cache: admitted script=%s", reference ? reference : "<inline-js>");
    return true;
}

int js_transpiler_parse_error_get(const JsTranspiler* tp, int64_t* out_row,
                                  int64_t* out_col, char* out_message,
                                  int64_t out_message_size) {
    if (!tp || !tp->parse_error_valid) return 0;
    if (out_row) *out_row = tp->parse_error_row;
    if (out_col) *out_col = tp->parse_error_col;
    if (out_message && out_message_size > 0) {
        snprintf(out_message, (size_t)out_message_size, "%s",
                 tp->parse_error_message);
    }
    return 1;
}

// Scope management functions

static ScopeKind js_scope_type_to_scope_kind(JsScopeType scope_type) {
    switch (scope_type) {
    case JS_SCOPE_GLOBAL: return SCOPE_KIND_GLOBAL;
    case JS_SCOPE_MODULE: return SCOPE_KIND_MODULE;
    case JS_SCOPE_FUNCTION: return SCOPE_KIND_FUNCTION;
    case JS_SCOPE_BLOCK:
    default:
        return SCOPE_KIND_BLOCK;
    }
}

JsScope* js_scope_create(JsTranspiler* tp, JsScopeType scope_type, JsScope* parent) {
    JsScope* scope = (JsScope*)pool_alloc(tp->pool, sizeof(JsScope));
    memset(scope, 0, sizeof(JsScope));

    scope->kind = js_scope_type_to_scope_kind(scope_type);
    scope->parent = parent;
    scope->strict = parent ? parent->strict : tp->strict_mode;
    scope->first = NULL;
    scope->last = NULL;

    return scope;
}

void js_scope_push(JsTranspiler* tp, JsScope* scope) {
    scope->parent = tp->current_scope;
    tp->current_scope = scope;
    log_debug("Pushed JavaScript scope type: %d", scope->kind);
}

void js_scope_pop(JsTranspiler* tp) {
    if (tp->current_scope) {
        JsScope* old_scope = tp->current_scope;
        tp->current_scope = old_scope->parent;
        log_debug("Popped JavaScript scope type: %d", old_scope->kind);
    }
}

static NameEntry* js_scope_find_entry_linear(JsScope* scope, String* name) {
    if (!scope || !name) return NULL;
    for (NameEntry* entry = scope->first; entry; entry = entry->next) {
        if (entry->name->len == name->len &&
            memcmp(entry->name->chars, name->chars, name->len) == 0) {
            return entry;
        }
    }
    return NULL;
}

static NameEntry* js_scope_find_entry(JsTranspiler* tp, JsScope* scope,
        String* name) {
    if (!scope || !name) return NULL;
    if (tp && tp->scope_binding_index &&
            !JsScopeBindingIndex::oom(tp->scope_binding_index)) {
        JsScopeBindingIndexEntry key = {scope, name, NULL};
        const JsScopeBindingIndexEntry* indexed = JsScopeBindingIndex::get(
            tp->scope_binding_index, key);
        return indexed ? indexed->binding : NULL;
    }
    return js_scope_find_entry_linear(scope, name);
}

static void js_scope_index_binding(JsTranspiler* tp, JsScope* scope,
        NameEntry* binding) {
    if (!tp || !scope || !binding || !binding->name) return;
    if (!tp->scope_binding_index) {
        tp->scope_binding_index = JsScopeBindingIndex::create(32);
    }
    if (!tp->scope_binding_index ||
            JsScopeBindingIndex::oom(tp->scope_binding_index)) return;
    JsScopeBindingIndexEntry entry = {scope, binding->name, binding};
    JsScopeBindingIndex::set(tp->scope_binding_index, entry);
}

static bool js_scope_entry_matches_node(const NameEntry* entry,
        const JsAstNode* node) {
    if (!entry || !entry->node || !node) return false;
    if (entry->node->source_span.start_byte == node->source_span.start_byte &&
            entry->node->source_span.end_byte == node->source_span.end_byte) {
        return true;
    }
    // A predeclared destructuring name is represented by one placeholder for
    // its declarator; the real binding identifier lives inside that span.
    return entry->node->node_type == AST_NODE_IDENT &&
        entry->node->source_span.start_byte <= node->source_span.start_byte &&
        entry->node->source_span.end_byte >= node->source_span.end_byte;
}

NameEntry* js_scope_lookup(JsTranspiler* tp, String* name) {
    // builder-time lookup is the only spelling-based path; indexed lowering
    // consumes binding IDs, so no mutable cache can outlive scope mutation.
    for (JsScope* scope = tp ? tp->current_scope : NULL; scope;
            scope = scope->parent) {
        NameEntry* entry = js_scope_find_entry(tp, scope, name);
        if (entry) return entry;
    }
    return NULL;
}

NameEntry* js_scope_lookup_current(JsTranspiler* tp, String* name) {
    return tp ? js_scope_find_entry(tp, tp->current_scope, name) : NULL;
}

bool js_scope_plan_binding_slots(JsScope* scope) {
    if (!scope) return true;
    if (scope->binding_slots_planned) return true;
    uint32_t count = 0;
    for (NameEntry* entry = scope->first; entry; entry = entry->next) {
        if (count >= (uint32_t)INT32_MAX) return false;
        entry->slot = (int32_t)count++;
        entry->storage_assigned = true;
    }
    scope->binding_slot_count = count;
    scope->binding_slots_planned = true;
    return true;
}

NameEntry* js_scope_define_in_scope(JsTranspiler* tp, JsScope* target_scope,
        String* name, JsAstNode* node, JsVarKind kind) {
    if (!target_scope) {
        target_scope = tp->global_scope;
    }

    NameEntry* existing = js_scope_find_entry(tp, target_scope, name);

    // Function-scoped var declarations are one hoisted binding even when the
    // source contains several declarations or a declaration is pre-scanned.
    if (existing && kind == JS_VAR_VAR && !existing->is_lexical) {
        if (js_scope_entry_matches_node(existing, node)) {
            existing->node = (AstNode*)node;
        }
        return existing;
    }

    // Check for redeclaration in strict mode or with let/const
    if (target_scope->strict || kind != JS_VAR_VAR) {
        if (existing) {
            bool annex_b_duplicate_block_function = !target_scope->strict &&
                target_scope->kind == SCOPE_KIND_BLOCK &&
                !target_scope->is_function_body && kind == JS_VAR_LET &&
                existing->is_lexical &&
                existing->node->node_type == AST_NODE_FUNC &&
                node->node_type == AST_NODE_FUNC;
            if (annex_b_duplicate_block_function) {
                // Annex B.3.3.4 permits sloppy block-only function duplicates.
                existing->node = (AstNode*)node;
                return existing;
            }
            if (js_scope_entry_matches_node(existing, node)) {
                existing->node = (AstNode*)node;
                return existing;
            }
            char message[320];
            snprintf(message, sizeof(message),
                "Identifier '%.*s' has already been declared in this scope",
                (int)name->len, name->chars);
            js_syntax_error(tp, node->source_span, message);
            tp->binding_error_count++;
            return existing;
        }
    }

    if (target_scope->binding_slots_planned) {
        log_error("js-scope: attempted to define a binding after slot planning");
        if (tp) tp->has_errors = true;
        return NULL;
    }

    // Create new name entry
    NameEntry* entry = (NameEntry*)pool_alloc(tp->pool, sizeof(NameEntry));
    memset(entry, 0, sizeof(NameEntry));
    entry->name = name;
    entry->node = (AstNode*)node;
    entry->scope = target_scope;
    entry->is_mutable = (kind != JS_VAR_CONST);
    entry->is_const = (kind == JS_VAR_CONST);
    entry->is_lexical = (kind != JS_VAR_VAR);
    entry->tdz_active = entry->is_lexical;

    // Add to scope
    if (!target_scope->first) {
        target_scope->first = entry;
    } else {
        target_scope->last->next = entry;
    }
    target_scope->last = entry;
    js_scope_index_binding(tp, target_scope, entry);
    log_debug("Defined JavaScript variable '%.*s' in scope type %d",
             (int)name->len, name->chars, target_scope->kind);
    return entry;
}

NameEntry* js_scope_define(JsTranspiler* tp, String* name, JsAstNode* node, JsVarKind kind) {
    JsScope* target_scope = tp->current_scope;

    // var declarations are function-scoped, let/const are block-scoped
    if (kind == JS_VAR_VAR) {
        // Annex B.3.5 keeps a var whose name matches a simple catch parameter
        // in the handler environment. Walk only the current var-declaration
        // region, so a nested function still starts a new var scope.
        for (JsScope* scope = target_scope; scope; scope = scope->parent) {
            if (scope->allows_legacy_var_redeclaration) {
                NameEntry* entry = js_scope_find_entry(tp, scope, name);
                if (entry && entry->is_lexical) return entry;
            }
            if (scope->kind != SCOPE_KIND_BLOCK) break;
        }

        // Find the nearest function scope or global scope
        while (target_scope && target_scope->kind == SCOPE_KIND_BLOCK) {
            target_scope = target_scope->parent;
        }
    }
    return js_scope_define_in_scope(tp, target_scope, name, node, kind);
}

void js_record_interp_import(JsTranspiler* tp, String* local,
        String* source, String* export_name, bool namespace_import) {
    if (!tp || !local || !source || (!namespace_import && !export_name)) return;
    JsInterpModuleBinding* binding = (JsInterpModuleBinding*)pool_calloc(tp->pool,
        sizeof(JsInterpModuleBinding));
    if (!binding) return;
    binding->local_name = local;
    binding->source = source;
    binding->export_name = export_name;
    binding->kind = JS_INTERP_MODULE_BINDING_IMPORT;
    binding->namespace_binding = namespace_import;
    binding->next = tp->interp_imports;
    tp->interp_imports = binding;
}

void js_record_interp_export(JsTranspiler* tp, String* local,
        String* export_name, String* source, bool namespace_export,
        bool star_export) {
    if (!tp || !local || !export_name) return;
    JsInterpModuleBinding* binding = (JsInterpModuleBinding*)pool_calloc(tp->pool,
        sizeof(JsInterpModuleBinding));
    if (!binding) return;
    binding->local_name = local;
    binding->export_name = export_name;
    binding->source = source;
    binding->kind = JS_INTERP_MODULE_BINDING_EXPORT;
    binding->namespace_binding = namespace_export;
    binding->star_export = star_export;
    binding->next = tp->interp_exports;
    tp->interp_exports = binding;
}

// Error handling functions

void js_error(JsTranspiler* tp, SourceSpan span, const char* format, ...) {
    tp->has_errors = true;

    if (!tp->error_buf) {
        tp->error_buf = strbuf_new();
    }

    // Add location information
    LambdaSourcePoint point = lambda_source_span_start_point(tp->source, span);
    uint32_t start_row = point.row;
    uint32_t start_col = point.column;
    strbuf_append_format(tp->error_buf, "Error at line %u, column %u: ",
                        start_row + 1, start_col + 1);

    // Add error message
    va_list args;
    va_start(args, format);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    strbuf_append_str(tp->error_buf, buffer);
    strbuf_append_char(tp->error_buf, '\n');

    log_error("JavaScript transpiler error: %s", buffer);
}

void js_syntax_error(JsTranspiler* tp, SourceSpan span, const char* message) {
    if (!tp || !message) return;
    js_error(tp, span, "%s", message);
    LambdaSourcePoint point = lambda_source_span_start_point(tp->source, span);
    fprintf(stderr, "SyntaxError: %s (at line %u, column %u)\n", message,
        point.row + 1, point.column + 1); // PRINTF_OK: host syntax diagnostic.
}

// Transpiler lifecycle functions

JsTranspiler* js_transpiler_create(Runtime* runtime) {
    JsTranspiler* tp = (JsTranspiler*)mem_alloc(sizeof(JsTranspiler), MEM_CAT_JS_RUNTIME);
    memset(tp, 0, sizeof(JsTranspiler));

    // Initialize memory pools
    tp->pool = mem_pool_create(NULL, MEM_ROLE_AST, "js.ast"); // Memory pool
    tp->name_pool = name_pool_create(tp->pool, NULL);
    // RC-J2/RC-J7v2: a JS literal is materialized once per compilation unit
    // instead of being rebuilt from MIR-embedded bytes on every evaluation. The
    // pool is acquired lazily at the first literal and owned by the context,
    // because this builder dies while its compiled code is still callable.
    tp->const_unit_id = LAMBDA_CONST_UNIT_NONE;
    tp->const_pool = NULL;
    tp->error_buf = NULL;

    tp->strict_mode = false;
    tp->has_errors = false;
    tp->strict_js = true;  // default: pure JS mode (reject TS syntax)
    tp->profile = &js_profile;
    tp->destroy_extension = js_script_destroy_extension;
    tp->runtime = runtime;
    return tp;
}

static void js_transpiler_destroy_tail(JsTranspiler* tp) {
    if (!tp) return;
    if (tp->scope_binding_index) {
        hashmap_free(tp->scope_binding_index);
        tp->scope_binding_index = NULL;
    }
    if (tp->error_buf) {
        strbuf_free(tp->error_buf);
    }
}

static void js_script_destroy_extension(Script* base_script) {
    JsScript* script = js_script_from_script(base_script);
    if (!script) return;
    if (script->type_registry) {
        hashmap_free(script->type_registry);
        script->type_registry = NULL;
    }
    if (script->ast_callables) {
        hashmap_free(script->ast_callables);
        script->ast_callables = NULL;
    }
    if (script->field_initializers) {
        hashmap_free(script->field_initializers);
        script->field_initializers = NULL;
    }
    if (script->ast_overlay_name_pool) {
        name_pool_release(script->ast_overlay_name_pool);
        script->ast_overlay_name_pool = NULL;
    }
    if (script->ast_index_overlay) {
        ast_index_destroy(&script->ast_index);
        script->ast_index_overlay = false;
    }
    // NamePool owns hash tables outside the AST pool. Release it before base
    // Script cleanup destroys the backing pool.
    if (script->name_pool && !script->cache_template) {
        name_pool_release(script->name_pool);
        script->name_pool = NULL;
    }
    if (script->ast_overlay_pool) {
        pool_destroy(script->ast_overlay_pool);
        script->ast_overlay_pool = NULL;
    }
}

struct JsFieldInitializerEntry {
    AstNodeId field_id;
    JsFunctionNode* function;
};
typedef TypedHashMap<JsFieldInitializerEntry,
    HashMapIntegralMemberKeyOps<JsFieldInitializerEntry, &JsFieldInitializerEntry::field_id>>
    JsFieldInitializerMap;

JsFunctionNode* js_script_field_initializer_ensure(JsScript* script,
        JsFieldDefinitionNode* field) {
    if (!script || !script->pool || !field || !field->value) return NULL;
    AstNodeId field_id = ast_index_find(&script->ast_index, (AstNode*)field);
    if (field_id == AST_NODE_ID_INVALID) return NULL;
    if (!script->field_initializers)
        script->field_initializers = JsFieldInitializerMap::create(8);
    if (!script->field_initializers) return NULL;
    JsFieldInitializerEntry key = {field_id, NULL};
    const JsFieldInitializerEntry* found = JsFieldInitializerMap::get(
        script->field_initializers, key);
    if (found) return found->function;
    Pool* overlay_pool = js_script_execution_pool(script);
    JsFunctionNode* function = (JsFunctionNode*)pool_calloc(overlay_pool, sizeof(JsFunctionNode));
    JsBlockNode* body = (JsBlockNode*)pool_calloc(overlay_pool, sizeof(JsBlockNode));
    JsReturnNode* result = (JsReturnNode*)pool_calloc(overlay_pool, sizeof(JsReturnNode));
    NameScope* scope = (NameScope*)pool_calloc(overlay_pool, sizeof(NameScope));
    if (!function || !body || !result || !scope) return NULL;
    // one indexed definition per field; each class evaluation supplies its own environment.
    function->node_type = AST_NODE_FUNC_EXPR;
    function->source_span = field->source_span;
    function->body = (JsAstNode*)body;
    function->vars = scope;
    function->has_use_strict_directive = true;
    scope->kind = SCOPE_KIND_FUNCTION;
    scope->strict = true;
    // The execution-overlay function bypasses direct scope construction, but
    // still has an immutable zero-binding activation plan (D8.2.4).
    if (!js_scope_plan_binding_slots(scope)) return NULL;
    body->node_type = AST_NODE_BLOCK;
    body->source_span = field->source_span;
    body->statements = (JsAstNode*)result;
    result->node_type = AST_NODE_RETURN_STAM;
    result->source_span = field->source_span;
    result->argument = field->value;
    if (!ast_index_append_profile(&script->ast_index, (AstNode*)function,
            (AstNode*)field, script->profile)) return NULL;
    key.function = function;
    JsFieldInitializerMap::set(script->field_initializers, key);
    return JsFieldInitializerMap::oom(script->field_initializers) ? NULL : function;
}

struct JsAstCallableEntry {
    AstFunctionId function_id;
    JsCallableCode* code;
};
typedef TypedHashMap<JsAstCallableEntry,
    HashMapIntegralMemberKeyOps<JsAstCallableEntry, &JsAstCallableEntry::function_id>>
    JsAstCallableMap;

static bool js_callable_elides_function_environment(AstFuncNode* function,
        const JsAstFunctionFacts& facts) {
    JsBlockNode* body = function && function->body &&
            function->body->node_type == AST_NODE_BLOCK
        ? (JsBlockNode*)function->body : NULL;
    // A call activation can borrow its closure environment only when it owns
    // no cells and no lexical state that a nested arrow/eval can observe.
    return function && !function->is_async && !function->is_generator &&
        !facts.has_direct_eval && !facts.has_with &&
        !facts.has_direct_super_call && !facts.has_lexical_super_call &&
        facts.observations == 0 &&
        (!function->vars || !function->vars->first) &&
        (!body || !body->vars || !body->vars->first);
}

// The Script pool owns one canonical code artifact for each indexed AST
// function. Closures retain that artifact rather than a parallel definition row.
JsCallableCode* js_script_ast_callable_ensure(JsScript* script,
        AstFuncNode* function, uint32_t module_state_id) {
    if (!script || !script->pool || !function) return NULL;
    if (!script->ast_callables) {
        script->ast_callables = JsAstCallableMap::create(8);
        if (!script->ast_callables) return NULL;
    }
    AstNodeId node_id = ast_index_find(&script->ast_index, (AstNode*)function);
    AstFunctionId function_id = node_id != AST_NODE_ID_INVALID
        ? script->ast_index.owner_functions[node_id] : AST_FUNCTION_ID_INVALID;
    if (function_id == AST_FUNCTION_ID_INVALID ||
            script->ast_index.functions[function_id].node != (AstNode*)function) {
        log_error("js-definition: function is missing its indexed identity");
        return NULL;
    }
    JsAstCallableEntry key = {function_id, NULL};
    const JsAstCallableEntry* found = JsAstCallableMap::get(script->ast_callables, key);
    if (found) return found->code;
    JsCallableCode* code = (JsCallableCode*)pool_calloc(
        js_script_execution_pool(script), sizeof(JsCallableCode));
    if (!code) return NULL;
    JsAstParameterFacts parameter_facts = js_ast_collect_parameter_facts(
        (JsAstNode*)function->params);
    int param_count = parameter_facts.parameter_count;
    JsAstFunctionFacts facts = js_ast_collect_function_facts(
        (JsAstNode*)function->params, (JsAstNode*)function->body);
    js_callable_code_init_definition(code, function, (Script*)script,
        param_count);
    code->formal_length = (int16_t)(parameter_facts.formal_length >= 0
        ? parameter_facts.formal_length : param_count);
    code->module_state_id = module_state_id;
    code->body_kind = JS_FUNCTION_BODY_AST;
    code->has_direct_eval = facts.has_direct_eval;
    code->uses_arguments = facts.observations &
        JS_AST_OBSERVES_ARGUMENTS;
    code->has_non_simple_params = parameter_facts.has_non_simple_params;
    // The canonical definition owns this static proof, avoiding repeated
    // scope/fact walks for every closure activation (D8.2.4–D8.2.5v2).
    code->elides_function_environment = js_callable_elides_function_environment(
        function, facts);
    code->definition_owned = true;
    key.code = code;
    JsAstCallableMap::set(script->ast_callables, key);
    if (JsAstCallableMap::oom(script->ast_callables)) return NULL;
    return code;
}

void js_transpiler_destroy(JsTranspiler* tp) {
    if (!tp) return;

    js_transpiler_destroy_tail(tp);

    // The builder only borrows source bytes from its caller. The adopted
    // JsScript path copies them before reaching runtime_free_script().
    tp->source = NULL;
    tp->reference = NULL;
    tp->directory = NULL;
    runtime_free_script(NULL, (Script*)tp, false);
}

JsScript* js_script_adopt_transpiler(JsTranspiler* tp, Runtime* runtime,
                                     const char* reference) {
    if (!tp || !tp->source) return NULL;

    const char* script_reference = reference ? reference : "<inline-js>";
    char* source_copy = mem_dup_n(tp->source, tp->source_length, MEM_CAT_SYSTEM);
    char* reference_copy = mem_strdup(script_reference, MEM_CAT_SYSTEM);
    JsScript* script = (JsScript*)mem_calloc(1, sizeof(JsScript), MEM_CAT_SYSTEM);
    if (!source_copy || !reference_copy || !script) {
        if (source_copy) mem_free(source_copy);
        if (reference_copy) mem_free(reference_copy);
        if (script) mem_free(script);
        return NULL;
    }

    memcpy(script, tp, sizeof(JsScript));
    script->source = source_copy;
    script->reference = reference_copy;
    script->destroy_extension = js_script_destroy_extension;
    if (script->ast_root && script->ast_root->node_type == AST_SCRIPT &&
            ((JsProgramNode*)script->ast_root)->has_use_strict_directive) {
        // The AST tier reads strictness from its retained Script rather than
        // the ephemeral transpiler; retain a program directive across adoption.
        script->strict_mode = true;
        if (script->global_scope) script->global_scope->strict = true;
    }

    // Transfer the complete retained prefix. Builder-only state is released
    // after the prefix is zeroed so the adopted script owns the AST pool.
    memset((JsScript*)tp, 0, sizeof(JsScript));
    js_transpiler_destroy(tp);

    if (runtime) {
        runtime_register_script(runtime, (Script*)script);
    }
    return script;
}
