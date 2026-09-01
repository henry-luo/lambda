#include "js_transpiler.hpp"
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/mem_factory.h"
#include "../../lib/strbuf.h"
#include "../../lib/mempool.h"
#include "../../lib/hashmap.h"
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include "../../lib/mem.h"

static void js_script_destroy_extension(Script* base_script);

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

static NameEntry* js_scope_find_entry(JsScope* scope, String* name) {
    if (!scope || !name) return NULL;
    for (NameEntry* entry = scope->first; entry; entry = entry->next) {
        if (entry->name->len == name->len &&
            memcmp(entry->name->chars, name->chars, name->len) == 0) {
            return entry;
        }
    }
    return NULL;
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
        NameEntry* entry = js_scope_find_entry(scope, name);
        if (entry) return entry;
    }
    return NULL;
}

NameEntry* js_scope_lookup_current(JsTranspiler* tp, String* name) {
    return tp ? js_scope_find_entry(tp->current_scope, name) : NULL;
}

NameEntry* js_scope_define_in_scope(JsTranspiler* tp, JsScope* target_scope,
        String* name, JsAstNode* node, JsVarKind kind) {
    if (!target_scope) {
        target_scope = tp->global_scope;
    }

    NameEntry* existing = js_scope_find_entry(target_scope, name);

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
                NameEntry* entry = js_scope_find_entry(scope, name);
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
    JsInterpImportBinding* binding = (JsInterpImportBinding*)pool_calloc(tp->pool,
        sizeof(JsInterpImportBinding));
    if (!binding) return;
    binding->local_name = local;
    binding->source = source;
    binding->export_name = export_name;
    binding->namespace_import = namespace_import;
    binding->next = tp->interp_imports;
    tp->interp_imports = binding;
}

void js_record_interp_export(JsTranspiler* tp, String* local,
        String* export_name, String* source, bool namespace_export,
        bool star_export) {
    if (!tp || !local || !export_name) return;
    JsInterpExportBinding* binding = (JsInterpExportBinding*)pool_calloc(tp->pool,
        sizeof(JsInterpExportBinding));
    if (!binding) return;
    binding->local_name = local;
    binding->export_name = export_name;
    binding->source = source;
    binding->namespace_export = namespace_export;
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
    tp->error_buf = NULL;

    tp->strict_mode = false;
    tp->has_errors = false;
    tp->strict_js = true;  // default: pure JS mode (reject TS syntax)
    // The shared indexer owns core edges. Install JavaScript's extension-only
    // adapter before any parse can publish an AstIndex for this profile.
    js_profile.visit_ext_children = js_ast_visit_extension_children;
    js_profile.publish_ext_facts = js_ast_publish_extension_facts;
    tp->profile = &js_profile;
    tp->destroy_extension = js_script_destroy_extension;
    tp->runtime = runtime;
    return tp;
}

static void js_transpiler_destroy_tail(JsTranspiler* tp) {
    if (!tp) return;
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
    // NamePool owns hash tables outside the AST pool. Release it before base
    // Script cleanup destroys the backing pool.
    if (script->name_pool) {
        name_pool_release(script->name_pool);
        script->name_pool = NULL;
    }
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
    char* source_copy = (char*)mem_alloc(tp->source_length + 1, MEM_CAT_SYSTEM);
    char* reference_copy = mem_strdup(script_reference, MEM_CAT_SYSTEM);
    JsScript* script = (JsScript*)mem_calloc(1, sizeof(JsScript), MEM_CAT_SYSTEM);
    if (!source_copy || !reference_copy || !script) {
        if (source_copy) mem_free(source_copy);
        if (reference_copy) mem_free(reference_copy);
        if (script) mem_free(script);
        return NULL;
    }

    memcpy(source_copy, tp->source, tp->source_length);
    source_copy[tp->source_length] = '\0';
    memcpy(script, tp, sizeof(JsScript));
    script->source = source_copy;
    script->reference = reference_copy;
    script->destroy_extension = js_script_destroy_extension;
    if (script->ast_root && script->ast_root->node_type == JS_AST_NODE_PROGRAM &&
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

    if (runtime) runtime_register_script(runtime, (Script*)script);
    return script;
}
