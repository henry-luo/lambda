/**
 * Bash Variable Scope Management
 *
 * Bash uses dynamic scoping for variables:
 * - Variables are global by default
 * - `local` creates function-local variables
 * - Scope lookup walks the call stack (dynamic, not lexical)
 * - Subshells get a copy of the current scope (snapshot + restore)
 */
#include "bash_transpiler.hpp"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/mempool.h"
#include "../../lib/hashmap.h"
#include <cstring>
#include "../../lib/mem.h"

// ============================================================================
// Scope management (compile-time, for the transpiler)
// ============================================================================

BashScope* bash_scope_create(BashTranspiler* tp, BashScopeType scope_type, BashScope* parent) {
    BashScope* scope = (BashScope*)pool_alloc(tp->ast_pool, sizeof(BashScope));
    memset(scope, 0, sizeof(BashScope));
    scope->scope_type = scope_type;
    scope->parent = parent;
    scope->first = NULL;
    scope->last = NULL;
    return scope;
}

void bash_ct_scope_push(BashTranspiler* tp, BashScope* scope) {
    scope->parent = tp->current_scope;
    tp->current_scope = scope;
    log_debug("bash: pushed scope type: %d", scope->scope_type);
}

void bash_ct_scope_pop(BashTranspiler* tp) {
    if (tp->current_scope) {
        BashScope* old_scope = tp->current_scope;
        tp->current_scope = old_scope->parent;
        log_debug("bash: popped scope type: %d", old_scope->scope_type);
    }
}

NameEntry* bash_scope_lookup(BashTranspiler* tp, String* name) {
    // dynamic scoping: walk up the scope chain
    BashScope* scope = tp->current_scope;
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

NameEntry* bash_scope_lookup_current(BashTranspiler* tp, String* name) {
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

void bash_scope_define(BashTranspiler* tp, String* name, BashAstNode* node, BashVarKind kind) {
    BashScope* target_scope = tp->current_scope;

    // local variables stay in current scope
    // global/export variables go to the global scope
    if (kind == BASH_VAR_GLOBAL || kind == BASH_VAR_EXPORT) {
        target_scope = tp->global_scope;
    }

    if (!target_scope) {
        target_scope = tp->global_scope;
    }

    // check for existing definition in target scope
    NameEntry* entry = target_scope->first;
    while (entry) {
        if (entry->name->len == name->len &&
            memcmp(entry->name->chars, name->chars, name->len) == 0) {
            entry->node = (AstNode*)node;
            return;
        }
        entry = entry->next;
    }

    // create new name entry
    NameEntry* new_entry = (NameEntry*)pool_alloc(tp->ast_pool, sizeof(NameEntry));
    new_entry->name = name;
    new_entry->node = (AstNode*)node;
    new_entry->next = NULL;
    new_entry->import = NULL;

    if (!target_scope->first) {
        target_scope->first = new_entry;
    } else {
        target_scope->last->next = new_entry;
    }
    target_scope->last = new_entry;

    log_debug("bash: defined variable '%.*s' in scope type %d",
              (int)name->len, name->chars, target_scope->scope_type);
}

// ============================================================================
// Error handling
// ============================================================================

void bash_error(BashTranspiler* tp, TSNode node, const char* format, ...) {
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

    log_error("bash: transpiler error: %s", buffer);
}

void bash_warning(BashTranspiler* tp, TSNode node, const char* format, ...) {
    uint32_t start_row = ts_node_start_point(node).row;
    uint32_t start_col = ts_node_start_point(node).column;

    va_list args;
    va_start(args, format);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    log_warn("bash: warning at line %u, column %u: %s",
             start_row + 1, start_col + 1, buffer);
}

// ============================================================================
// Transpiler lifecycle
// ============================================================================

BashTranspiler* bash_transpiler_create(Runtime* runtime) {
    BashTranspiler* tp = (BashTranspiler*)mem_alloc(sizeof(BashTranspiler), MEM_CAT_BASH_RUNTIME);
    memset(tp, 0, sizeof(BashTranspiler));

    // initialize memory pools
    tp->ast_pool = pool_create();
    tp->name_pool = name_pool_create(tp->ast_pool, NULL);
    tp->code_buf = strbuf_new();
    tp->error_buf = NULL;

    // initialize scopes (Bash uses dynamic scoping)
    tp->global_scope = bash_scope_create(tp, BASH_SCOPE_GLOBAL, NULL);
    tp->current_scope = tp->global_scope;
    tp->function_counter = 0;
    tp->temp_var_counter = 0;
    tp->label_counter = 0;
    tp->has_errors = false;
    tp->runtime = runtime;

    return tp;
}

void bash_transpiler_destroy(BashTranspiler* tp) {
    if (!tp) return;
    if (tp->code_buf) strbuf_free(tp->code_buf);
    if (tp->error_buf) strbuf_free(tp->error_buf);
    if (tp->ast_pool) pool_destroy(tp->ast_pool);
    mem_free(tp);
}
