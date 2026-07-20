#include "py_transpiler.hpp"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/mem_factory.h"
#include "../../lib/strbuf.h"
#include "../../lib/mempool.h"
#include "../../lib/arena.h"
#include "../tree-sitter-python/bindings/c/tree_sitter/tree-sitter-python.h"
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include "../../lib/mem.h"

// scope management functions

static bool py_scope_name_matches(NameEntry* entry, String* name) {
    return entry && entry->name->len == name->len &&
        memcmp(entry->name->chars, name->chars, name->len) == 0;
}

static NameEntry* py_scope_lookup_in(NameScope* scope, String* name) {
    if (!scope) return NULL;
    NameEntry* entry = scope->first;
    while (entry) {
        if (py_scope_name_matches(entry, name)) return entry;
        entry = entry->next;
    }
    return NULL;
}

static NameScope* py_scope_enclosing_function(NameScope* scope) {
    while (scope && scope->kind != PY_SCOPE_FUNCTION) {
        scope = scope->parent;
    }
    return scope;
}

static NameEntry* py_scope_append_entry(PyTranspiler* tp, NameScope* scope,
        String* name, PyAstNode* node) {
    // Python-only declaration flags must not enlarge every shared JS name entry.
    NameEntry* entry = (NameEntry*)arena_calloc(tp->ast_arena, sizeof(PyNameEntry));
    entry->name = name;
    entry->node = (AstNode*)node;
    entry->scope = scope;

    if (!scope->first) {
        scope->first = entry;
    } else {
        scope->last->next = entry;
    }
    scope->last = entry;
    return entry;
}

static PyNameEntry* py_name_entry(NameEntry* entry) {
    return (PyNameEntry*)entry;
}

PyScope* py_scope_create(PyTranspiler* tp, PyScopeType scope_type, PyScope* parent) {
    PyScope* scope = (PyScope*)arena_calloc(tp->ast_arena, sizeof(NameScope));

    scope->kind = scope_type;
    scope->parent = parent;
    scope->first = NULL;
    scope->last = NULL;

    return scope;
}

void py_scope_push(PyTranspiler* tp, PyScope* scope) {
    scope->parent = tp->current_scope;
    tp->current_scope = scope;
    log_debug("py: pushed scope type: %d", scope->kind);
}

void py_scope_pop(PyTranspiler* tp) {
    if (tp->current_scope) {
        PyScope* old_scope = tp->current_scope;
        tp->current_scope = old_scope->parent;
        log_debug("py: popped scope type: %d", old_scope->kind);
    }
}

NameEntry* py_scope_lookup(PyTranspiler* tp, String* name) {
    PyScope* scope = tp->current_scope;
    bool skip_class_scope = scope && (scope->kind == PY_SCOPE_FUNCTION ||
        scope->kind == PY_SCOPE_COMPREHENSION);

    while (scope) {
        if (!(skip_class_scope && scope->kind == PY_SCOPE_CLASS)) {
            NameEntry* entry = py_scope_lookup_in(scope, name);
            if (entry) {
                if (py_name_entry(entry)->is_global_decl) {
                    NameEntry* module_entry = py_scope_lookup_in(tp->module_scope, name);
                    return module_entry ? module_entry : entry;
                }
                if (py_name_entry(entry)->is_nonlocal_decl) {
                    NameScope* enclosing = py_scope_enclosing_function(scope->parent);
                    NameEntry* enclosing_entry = py_scope_lookup_in(enclosing, name);
                    return enclosing_entry ? enclosing_entry : entry;
                }
                return entry;
            }
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

    if (!target_scope) target_scope = tp->module_scope;

    if (kind == PY_VAR_GLOBAL || kind == PY_VAR_NONLOCAL) {
        NameEntry* current = py_scope_lookup_in(target_scope, name);
        if (current && current->node != (AstNode*)node) {
            // Python rejects a declaration after a local binding in the same scope.
            py_error(tp, node->node, "%s declaration follows a local binding for '%.*s'",
                kind == PY_VAR_GLOBAL ? "global" : "nonlocal", (int)name->len, name->chars);
            return;
        }
        if (!current) current = py_scope_append_entry(tp, target_scope, name, node);
        py_name_entry(current)->is_global_decl = kind == PY_VAR_GLOBAL;
        py_name_entry(current)->is_nonlocal_decl = kind == PY_VAR_NONLOCAL;
        return;
    }

    NameEntry* declaration = py_scope_lookup_in(target_scope, name);
    if (declaration && py_name_entry(declaration)->is_global_decl) {
        target_scope = tp->module_scope;
    } else if (declaration && py_name_entry(declaration)->is_nonlocal_decl) {
        target_scope = py_scope_enclosing_function(target_scope->parent);
        if (!target_scope) {
            // A nonlocal name must resolve to a function scope, never a module.
            py_error(tp, node->node, "nonlocal binding for '%.*s' not found",
                (int)name->len, name->chars);
            return;
        }
    }

    NameEntry* existing = py_scope_lookup_in(target_scope, name);

    if (existing) {
        // Keep the declaration entry in its source scope; later assignments
        // update the binding scope selected by that declaration.
        existing->node = (AstNode*)node;
        return;
    }

    py_scope_append_entry(tp, target_scope, name, node);

    log_debug("py: defined variable '%.*s' in scope type %d",
             (int)name->len, name->chars, target_scope->kind);
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

PyTranspiler* py_transpiler_create(void* host_execution) {
    PyTranspiler* tp = (PyTranspiler*)mem_alloc(sizeof(PyTranspiler), MEM_CAT_PY_RUNTIME);
    memset(tp, 0, sizeof(PyTranspiler));

    // initialize memory pools
    tp->ast_pool = mem_pool_create(NULL, MEM_ROLE_AST, "py.ast");
    tp->ast_arena = mem_arena_create(NULL, tp->ast_pool, MEM_ROLE_AST, "py.ast.arena");
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
    tp->host_execution = host_execution;

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
