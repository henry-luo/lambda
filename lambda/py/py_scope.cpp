#include "py_transpiler.hpp"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/mempool.h"
#include "../../lib/arena.h"
#include "../tree-sitter-python/bindings/c/tree_sitter/tree-sitter-python.h"
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include "../../lib/mem.h"

// scope management functions

PyScope* py_scope_create(PyTranspiler* tp, PyScopeType scope_type, PyScope* parent) {
    PyScope* scope = (PyScope*)arena_calloc(tp->ast_arena, sizeof(PyScope));

    scope->scope_type = scope_type;
    scope->parent = parent;
    scope->function = NULL;
    scope->first = NULL;
    scope->last = NULL;

    return scope;
}

void py_scope_push(PyTranspiler* tp, PyScope* scope) {
    scope->parent = tp->current_scope;
    tp->current_scope = scope;
    log_debug("py: pushed scope type: %d", scope->scope_type);
}

void py_scope_pop(PyTranspiler* tp) {
    if (tp->current_scope) {
        PyScope* old_scope = tp->current_scope;
        tp->current_scope = old_scope->parent;
        log_debug("py: popped scope type: %d", old_scope->scope_type);
    }
}

NameEntry* py_scope_lookup(PyTranspiler* tp, String* name) {
    PyScope* scope = tp->current_scope;

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

NameEntry* py_scope_lookup_current(PyTranspiler* tp, String* name) {
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

void py_scope_define(PyTranspiler* tp, String* name, PyAstNode* node, PyVarKind kind) {
    PyScope* target_scope = tp->current_scope;

    // for global declarations, target the module scope
    if (kind == PY_VAR_GLOBAL) {
        target_scope = tp->module_scope;
    }
    // for nonlocal, walk up to nearest enclosing function scope
    else if (kind == PY_VAR_NONLOCAL) {
        target_scope = tp->current_scope->parent;
        while (target_scope && target_scope->scope_type != PY_SCOPE_FUNCTION) {
            target_scope = target_scope->parent;
        }
        if (!target_scope) {
            target_scope = tp->module_scope;
        }
    }

    if (!target_scope) {
        target_scope = tp->module_scope;
    }

    // check for existing definition in target scope
    NameEntry* existing = NULL;
    NameEntry* entry = target_scope->first;
    while (entry) {
        if (entry->name->len == name->len &&
            memcmp(entry->name->chars, name->chars, name->len) == 0) {
            existing = entry;
            break;
        }
        entry = entry->next;
    }

    if (existing) {
        // update existing entry
        existing->node = (AstNode*)node;
        return;
    }

    // create new name entry
    NameEntry* new_entry = (NameEntry*)arena_calloc(tp->ast_arena, sizeof(NameEntry));
    new_entry->name = name;
    new_entry->node = (AstNode*)node;
    new_entry->next = NULL;
    new_entry->import = NULL;

    // add to scope
    if (!target_scope->first) {
        target_scope->first = new_entry;
    } else {
        target_scope->last->next = new_entry;
    }
    target_scope->last = new_entry;

    log_debug("py: defined variable '%.*s' in scope type %d",
             (int)name->len, name->chars, target_scope->scope_type);
}

// error handling functions

void py_error(PyTranspiler* tp, TSNode node, const char* format, ...) {
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

    log_error("py: transpiler error: %s", buffer);
}

void py_warning(PyTranspiler* tp, TSNode node, const char* format, ...) {
    uint32_t start_row = ts_node_start_point(node).row;
    uint32_t start_col = ts_node_start_point(node).column;

    va_list args;
    va_start(args, format);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    log_warn("py: warning at line %u, column %u: %s",
             start_row + 1, start_col + 1, buffer);
}

// transpiler lifecycle functions

PyTranspiler* py_transpiler_create(Runtime* runtime) {
    PyTranspiler* tp = (PyTranspiler*)mem_alloc(sizeof(PyTranspiler), MEM_CAT_PY_RUNTIME);
    memset(tp, 0, sizeof(PyTranspiler));

    // initialize memory pools
    tp->ast_pool = pool_create();
    tp->ast_arena = arena_create_default(tp->ast_pool);
    tp->name_pool = name_pool_create(tp->ast_pool, NULL);
    tp->code_buf = strbuf_new();
    tp->error_buf = NULL;

    // initialize Tree-sitter parser
    tp->parser = ts_parser_new();
    ts_parser_set_language(tp->parser, tree_sitter_python());

    // initialize scopes
    tp->module_scope = py_scope_create(tp, PY_SCOPE_MODULE, NULL);
    tp->current_scope = tp->module_scope;
    tp->function_counter = 0;
    tp->temp_var_counter = 0;
    tp->label_counter = 0;
    tp->has_errors = false;
    tp->runtime = runtime;

    return tp;
}

void py_transpiler_destroy(PyTranspiler* tp) {
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

bool py_transpiler_parse(PyTranspiler* tp, const char* source, size_t length) {
    tp->source = source;
    tp->source_length = length;

    tp->tree = ts_parser_parse_string(tp->parser, NULL, source, length);

    if (!tp->tree) {
        log_error("py: failed to parse Python source");
        return false;
    }

    TSNode root = ts_tree_root_node(tp->tree);

    if (ts_node_has_error(root)) {
        log_error("py: source has syntax errors");
        return false;
    }

    return true;
}
