// rb_scope.cpp — Ruby scope management and transpiler lifecycle
#include "rb_transpiler.hpp"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/mempool.h"
#include "../../lib/arena.h"
#include "../tree-sitter-ruby/bindings/c/tree_sitter/tree-sitter-ruby.h"
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include "../../lib/mem.h"

// ============================================================================
// Scope management
// ============================================================================

RbScope* rb_scope_create(RbTranspiler* tp, RbScopeType scope_type, RbScope* parent) {
    RbScope* scope = (RbScope*)arena_calloc(tp->ast_arena, sizeof(RbScope));
    scope->scope_type = scope_type;
    scope->parent = parent;
    scope->method = NULL;
    scope->first = NULL;
    scope->last = NULL;
    return scope;
}

void rb_scope_push(RbTranspiler* tp, RbScope* scope) {
    scope->parent = tp->current_scope;
    tp->current_scope = scope;
    log_debug("rb: pushed scope type: %d", scope->scope_type);
}

void rb_scope_pop(RbTranspiler* tp) {
    if (tp->current_scope) {
        RbScope* old_scope = tp->current_scope;
        tp->current_scope = old_scope->parent;
        log_debug("rb: popped scope type: %d", old_scope->scope_type);
    }
}

NameEntry* rb_scope_lookup(RbTranspiler* tp, String* name) {
    RbScope* scope = tp->current_scope;
    while (scope) {
        NameEntry* entry = scope->first;
        while (entry) {
            if (entry->name->len == name->len &&
                memcmp(entry->name->chars, name->chars, name->len) == 0) {
                return entry;
            }
            entry = entry->next;
        }
        scope = scope->parent;
    }
    return NULL;
}

NameEntry* rb_scope_lookup_current(RbTranspiler* tp, String* name) {
    if (!tp->current_scope) return NULL;
    NameEntry* entry = tp->current_scope->first;
    while (entry) {
        if (entry->name->len == name->len &&
            memcmp(entry->name->chars, name->chars, name->len) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

void rb_scope_define(RbTranspiler* tp, String* name, RbAstNode* node, RbVarKind kind) {
    RbScope* target_scope = tp->current_scope;

    // global variables always go to top scope
    if (kind == RB_VAR_GVAR || kind == RB_VAR_MODULE) {
        target_scope = tp->top_scope;
    }

    if (!target_scope) {
        target_scope = tp->top_scope;
    }

    // check for existing definition in target scope
    NameEntry* entry = target_scope->first;
    while (entry) {
        if (entry->name->len == name->len &&
            memcmp(entry->name->chars, name->chars, name->len) == 0) {
            // update existing entry
            entry->node = (AstNode*)node;
            return;
        }
        entry = entry->next;
    }

    // create new name entry
    NameEntry* new_entry = (NameEntry*)arena_calloc(tp->ast_arena, sizeof(NameEntry));
    new_entry->name = name;
    new_entry->node = (AstNode*)node;
    new_entry->next = NULL;

    // add to scope
    if (!target_scope->first) {
        target_scope->first = new_entry;
    } else {
        target_scope->last->next = new_entry;
    }
    target_scope->last = new_entry;

    log_debug("rb: defined variable '%.*s' in scope type %d",
             (int)name->len, name->chars, target_scope->scope_type);
}

// ============================================================================
// Error handling
// ============================================================================

void rb_error(RbTranspiler* tp, TSNode node, const char* format, ...) {
    tp->has_errors = true;

    if (!tp->error_buf) {
        tp->error_buf = strbuf_new();
    }

    uint32_t start_row = ts_node_start_point(node).row;
    uint32_t start_col = ts_node_start_point(node).column;
    strbuf_append_format(tp->error_buf, "Error at line %u, column %u: ",
                        start_row + 1, start_col + 1);

    va_list args;
    va_start(args, format);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    strbuf_append_str(tp->error_buf, buffer);
    strbuf_append_char(tp->error_buf, '\n');

    log_error("rb: transpiler error: %s", buffer);
}

void rb_warning(RbTranspiler* tp, TSNode node, const char* format, ...) {
    uint32_t start_row = ts_node_start_point(node).row;
    uint32_t start_col = ts_node_start_point(node).column;

    va_list args;
    va_start(args, format);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    log_warn("rb: warning at line %u, column %u: %s",
             start_row + 1, start_col + 1, buffer);
}

// ============================================================================
// Transpiler lifecycle
// ============================================================================

RbTranspiler* rb_transpiler_create(Runtime* runtime) {
    RbTranspiler* tp = (RbTranspiler*)mem_alloc(sizeof(RbTranspiler), MEM_CAT_RB_RUNTIME);
    memset(tp, 0, sizeof(RbTranspiler));

    // initialize memory pools
    tp->ast_pool = pool_create();
    tp->ast_arena = arena_create_default(tp->ast_pool);
    tp->name_pool = name_pool_create(tp->ast_pool, NULL);
    tp->code_buf = strbuf_new();
    tp->error_buf = NULL;

    // initialize tree-sitter parser
    tp->parser = ts_parser_new();
    ts_parser_set_language(tp->parser, tree_sitter_ruby());

    // initialize scopes
    tp->top_scope = rb_scope_create(tp, RB_SCOPE_TOP, NULL);
    tp->current_scope = tp->top_scope;
    tp->method_counter = 0;
    tp->temp_var_counter = 0;
    tp->label_counter = 0;
    tp->has_errors = false;
    tp->runtime = runtime;

    return tp;
}

void rb_transpiler_destroy(RbTranspiler* tp) {
    if (!tp) return;

    if (tp->tree) {
        ts_tree_delete(tp->tree);
    }
    if (tp->parser) {
        ts_parser_delete(tp->parser);
    }
    // Release name_pool BEFORE destroying ast_pool: name_pool was allocated
    // from ast_pool, so pool_destroy would unmap its memory.
    if (tp->name_pool) {
        name_pool_release(tp->name_pool);
    }
    if (tp->ast_arena) {
        arena_destroy(tp->ast_arena);
    }
    if (tp->ast_pool) {
        pool_destroy(tp->ast_pool);
    }
    if (tp->code_buf) {
        strbuf_free(tp->code_buf);
    }
    if (tp->error_buf) {
        strbuf_free(tp->error_buf);
    }

    mem_free(tp);
}

bool rb_transpiler_parse(RbTranspiler* tp, const char* source, size_t length) {
    tp->source = source;
    tp->source_length = length;

    tp->tree = ts_parser_parse_string(tp->parser, NULL, source, (uint32_t)length);

    if (!tp->tree) {
        log_error("rb: failed to parse Ruby source");
        return false;
    }

    TSNode root = ts_tree_root_node(tp->tree);

    if (ts_node_has_error(root)) {
        log_error("rb: source has syntax errors");
        // continue anyway — tree-sitter provides partial trees
    }

    return true;
}
