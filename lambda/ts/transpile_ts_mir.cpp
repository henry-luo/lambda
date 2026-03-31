// transpile_ts_mir.cpp — TypeScript MIR transpiler
//
// Entry point for TypeScript → MIR compilation. Follows the same pipeline as
// transpile_js_mir.cpp but adds type awareness from TS annotations.
// This initial implementation delegates most of the heavy lifting to the
// JS transpiler infrastructure while adding TS-specific handling.

#include "ts_transpiler.hpp"
#include "ts_runtime.h"
#include "../js/js_transpiler.hpp"
#include "../js/js_runtime.h"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/hashmap.h"
#include "../../lib/mempool.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

// tree-sitter (fall back to JS parser for now)
extern "C" {
    const TSLanguage* tree_sitter_javascript(void);
}

// ============================================================================
// TsTranspiler lifecycle
// ============================================================================

TsTranspiler* ts_transpiler_create(Runtime* runtime) {
    TsTranspiler* tp = (TsTranspiler*)calloc(1, sizeof(TsTranspiler));
    if (!tp) return NULL;

    tp->ast_pool = pool_create();
    tp->name_pool = name_pool_create(tp->ast_pool, NULL);
    tp->code_buf = strbuf_new_cap(4096);
    tp->func_buf = strbuf_new_cap(2048);
    tp->error_buf = strbuf_new_cap(256);
    tp->runtime = runtime;
    tp->strict_mode = true;  // TS always implies strict mode
    tp->emit_runtime_checks = false;

    // create global scope
    tp->global_scope = js_scope_create((JsTranspiler*)tp, JS_SCOPE_GLOBAL, NULL);
    tp->global_scope->strict_mode = true;
    tp->current_scope = tp->global_scope;

    // initialize type registry
    ts_type_registry_init(tp);

    return tp;
}

void ts_transpiler_destroy(TsTranspiler* tp) {
    if (!tp) return;
    if (tp->tree) ts_tree_delete(tp->tree);
    if (tp->parser) ts_parser_delete(tp->parser);
    if (tp->code_buf) strbuf_free(tp->code_buf);
    if (tp->func_buf) strbuf_free(tp->func_buf);
    if (tp->error_buf) strbuf_free(tp->error_buf);
    if (tp->type_registry) hashmap_free(tp->type_registry);
    if (tp->ast_pool) pool_destroy(tp->ast_pool);
    free(tp);
}

bool ts_transpiler_parse(TsTranspiler* tp, const char* source, size_t length) {
    tp->source = source;
    tp->source_length = length;

    tp->parser = ts_parser_new();
    if (!tp->parser) {
        log_error("ts-mir: failed to create parser");
        return false;
    }

    // use JS parser which handles most TS syntax via error recovery
    const TSLanguage* lang = tree_sitter_javascript();
    if (!lang) {
        log_error("ts-mir: no parser available");
        return false;
    }

    ts_parser_set_language(tp->parser, lang);
    tp->tree = ts_parser_parse_string(tp->parser, NULL, source, (uint32_t)length);
    if (!tp->tree) {
        log_error("ts-mir: parse failed");
        return false;
    }

    return true;
}

// ============================================================================
// Scope management (delegates to JS scope functions via cast)
// ============================================================================

JsScope* ts_scope_create(TsTranspiler* tp, JsScopeType scope_type, JsScope* parent) {
    return js_scope_create((JsTranspiler*)tp, scope_type, parent);
}

void ts_scope_push(TsTranspiler* tp, JsScope* scope) {
    js_scope_push((JsTranspiler*)tp, scope);
}

void ts_scope_pop(TsTranspiler* tp) {
    js_scope_pop((JsTranspiler*)tp);
}

NameEntry* ts_scope_lookup(TsTranspiler* tp, String* name) {
    return js_scope_lookup((JsTranspiler*)tp, name);
}

void ts_scope_define(TsTranspiler* tp, String* name, JsAstNode* node, JsVarKind kind) {
    js_scope_define((JsTranspiler*)tp, name, node, kind);
}

// ============================================================================
// Strip type-only nodes from AST before passing to JS transpiler
// ============================================================================

static JsAstNode* ts_strip_type_only_nodes(JsAstNode* body) {
    JsAstNode* first = NULL;
    JsAstNode* last = NULL;

    for (JsAstNode* node = body; node; ) {
        JsAstNode* next = node->next;
        int nt = node->node_type;

        // skip type-only declarations (already registered in type registry)
        if (nt == (int)TS_AST_NODE_INTERFACE ||
            nt == (int)TS_AST_NODE_TYPE_ALIAS ||
            nt == (int)TS_AST_NODE_AMBIENT_DECLARATION) {
            node = next;
            continue;
        }

        node->next = NULL;
        if (!first) {
            first = node;
            last = node;
        } else {
            last->next = node;
            last = node;
        }
        node = next;
    }
    return first;
}

// ============================================================================
// Main entry point
// ============================================================================

Item transpile_ts_to_mir(Runtime* runtime, const char* ts_source, const char* filename) {
    log_debug("ts-mir: starting TypeScript transpilation for '%s'", filename ? filename : "<string>");

    // Phase 1: Preprocess TS source — strip type annotations to produce valid JS
    size_t js_len = 0;
    char* js_source = ts_preprocess_source(ts_source, strlen(ts_source), &js_len);
    if (!js_source) {
        log_error("ts-mir: preprocessing failed");
        return (Item){.item = ITEM_ERROR};
    }
    log_debug("ts-mir: preprocessed %zu bytes JS", js_len);

    // create TS transpiler
    TsTranspiler* tp = ts_transpiler_create(runtime);
    if (!tp) {
        log_error("ts-mir: failed to create transpiler");
        free(js_source);
        return (Item){.item = ITEM_ERROR};
    }

    // Phase 2: Parse the preprocessed JS source with the JS parser
    if (!ts_transpiler_parse(tp, js_source, js_len)) {
        log_error("ts-mir: parse failed");
        free(js_source);
        ts_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }

    TSNode root = ts_tree_root_node(tp->tree);

    // Phase 3: Build AST from the clean JS CST
    log_debug("ts-mir: building AST...");
    JsAstNode* ts_ast = build_ts_ast(tp, root);
    if (!ts_ast) {
        log_error("ts-mir: AST build failed");
        free(js_source);
        ts_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }
    log_debug("ts-mir: AST built, node_type=%d", ts_ast->node_type);

    // Phase 4: Resolve type annotations (from TS-specific AST nodes, if any)
    log_debug("ts-mir: resolving types...");
    ts_resolve_all_types(tp, ts_ast);
    log_debug("ts-mir: types resolved");

    // Strip type-only nodes from program body before passing to JS transpiler
    if (ts_ast->node_type == JS_AST_NODE_PROGRAM) {
        JsProgramNode* prog = (JsProgramNode*)ts_ast;
        prog->body = ts_strip_type_only_nodes(prog->body);
        log_debug("ts-mir: type-only nodes stripped");
    }

    // Phase 5: Delegate to JS MIR transpiler with the pre-built AST
    log_debug("ts-mir: delegating to JS MIR transpiler...");
    Item result = transpile_js_ast_to_mir(runtime, (JsTranspiler*)tp, ts_ast, filename);
    log_debug("ts-mir: JS MIR transpiler returned");

    free(js_source);
    ts_transpiler_destroy(tp);
    log_debug("ts-mir: transpilation completed");
    return result;
}
