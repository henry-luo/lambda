#include "js_transpiler.hpp"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <setjmp.h>
#include <cstdlib>

// Scope management functions

JsScope* js_scope_create(JsTranspiler* tp, JsScopeType scope_type, JsScope* parent) {
    JsScope* scope = (JsScope*)pool_alloc(tp->ast_pool, sizeof(JsScope));
    memset(scope, 0, sizeof(JsScope));
    
    scope->scope_type = scope_type;
    scope->parent = parent;
    scope->strict_mode = parent ? parent->strict_mode : tp->strict_mode;
    scope->function = NULL;
    scope->first = NULL;
    scope->last = NULL;
    
    return scope;
}

void js_scope_push(JsTranspiler* tp, JsScope* scope) {
    scope->parent = tp->current_scope;
    tp->current_scope = scope;
    log_debug("Pushed JavaScript scope type: %d", scope->scope_type);
}

void js_scope_pop(JsTranspiler* tp) {
    if (tp->current_scope) {
        JsScope* old_scope = tp->current_scope;
        tp->current_scope = old_scope->parent;
        log_debug("Popped JavaScript scope type: %d", old_scope->scope_type);
    }
}

NameEntry* js_scope_lookup(JsTranspiler* tp, String* name) {
    JsScope* scope = tp->current_scope;
    
    while (scope) {
        NameEntry* entry = scope->first;
        while (entry) {
            if (entry->name->len == name->len && 
                memcmp(entry->name->chars, name->chars, name->len) == 0) {
                return entry;
            }
            entry = entry->next;
        }
        
        // For var declarations, skip block scopes and go to function scope
        scope = scope->parent;
    }
    
    return NULL; // Not found
}

NameEntry* js_scope_lookup_current(JsTranspiler* tp, String* name) {
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

void js_scope_define(JsTranspiler* tp, String* name, JsAstNode* node, JsVarKind kind) {
    JsScope* target_scope = tp->current_scope;
    
    // var declarations are function-scoped, let/const are block-scoped
    if (kind == JS_VAR_VAR) {
        // Find the nearest function scope or global scope
        while (target_scope && target_scope->scope_type == JS_SCOPE_BLOCK) {
            target_scope = target_scope->parent;
        }
    }
    
    if (!target_scope) {
        target_scope = tp->global_scope;
    }
    
    // Check for redeclaration in strict mode or with let/const
    if (target_scope->strict_mode || kind != JS_VAR_VAR) {
        NameEntry* existing = js_scope_lookup_current(tp, name);
        if (existing) {
            log_error("Identifier '%.*s' has already been declared", 
                     (int)name->len, name->chars);
            return;
        }
    }
    
    // Create new name entry
    NameEntry* entry = (NameEntry*)pool_alloc(tp->ast_pool, sizeof(NameEntry));
    entry->name = name;
    entry->node = node;
    entry->next = NULL;
    entry->import = NULL;
    
    // Add to scope
    if (!target_scope->first) {
        target_scope->first = entry;
    } else {
        target_scope->last->next = entry;
    }
    target_scope->last = entry;
    
    log_debug("Defined JavaScript variable '%.*s' in scope type %d", 
             (int)name->len, name->chars, target_scope->scope_type);
}

// Error handling functions

void js_error(JsTranspiler* tp, TSNode node, const char* format, ...) {
    tp->has_errors = true;
    
    if (!tp->error_buf) {
        tp->error_buf = strbuf_new();
    }
    
    // Add location information
    uint32_t start_row = ts_node_start_point(node).row;
    uint32_t start_col = ts_node_start_point(node).column;
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

void js_warning(JsTranspiler* tp, TSNode node, const char* format, ...) {
    // Add location information
    uint32_t start_row = ts_node_start_point(node).row;
    uint32_t start_col = ts_node_start_point(node).column;
    
    // Format warning message
    va_list args;
    va_start(args, format);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    log_warn("JavaScript transpiler warning at line %u, column %u: %s", 
             start_row + 1, start_col + 1, buffer);
}

// Transpiler lifecycle functions

JsTranspiler* js_transpiler_create(Runtime* runtime) {
    JsTranspiler* tp = (JsTranspiler*)malloc(sizeof(JsTranspiler));
    memset(tp, 0, sizeof(JsTranspiler));
    
    // Initialize memory pools
    tp->ast_pool = pool_create(1024 * 1024); // 1MB pool
    tp->name_pool = name_pool_create();
    tp->code_buf = strbuf_new();
    tp->error_buf = NULL;
    
    // Initialize Tree-sitter parser
    tp->parser = ts_parser_new();
    ts_parser_set_language(tp->parser, tree_sitter_javascript());
    
    // Initialize scopes
    tp->global_scope = js_scope_create(tp, JS_SCOPE_GLOBAL, NULL);
    tp->current_scope = tp->global_scope;
    
    // Initialize state
    tp->strict_mode = false;
    tp->function_counter = 0;
    tp->temp_var_counter = 0;
    tp->label_counter = 0;
    tp->has_errors = false;
    tp->runtime = runtime;
    
    return tp;
}

void js_transpiler_destroy(JsTranspiler* tp) {
    if (!tp) return;
    
    // Cleanup Tree-sitter
    if (tp->tree) {
        ts_tree_delete(tp->tree);
    }
    if (tp->parser) {
        ts_parser_delete(tp->parser);
    }
    
    // Cleanup memory pools
    if (tp->ast_pool) {
        pool_destroy(tp->ast_pool);
    }
    if (tp->name_pool) {
        name_pool_destroy(tp->name_pool);
    }
    if (tp->code_buf) {
        strbuf_free(tp->code_buf);
    }
    if (tp->error_buf) {
        strbuf_free(tp->error_buf);
    }
    
    free(tp);
}

bool js_transpiler_parse(JsTranspiler* tp, const char* source, size_t length) {
    tp->source = source;
    tp->source_length = length;
    
    // Parse with Tree-sitter
    tp->tree = ts_parser_parse_string(tp->parser, NULL, source, length);
    
    if (!tp->tree) {
        log_error("Failed to parse JavaScript source");
        return false;
    }
    
    TSNode root = ts_tree_root_node(tp->tree);
    
    // Check for syntax errors
    if (ts_node_has_error(root)) {
        log_error("JavaScript source has syntax errors");
        return false;
    }
    
    return true;
}

Item js_transpiler_compile(JsTranspiler* tp) {
    if (!tp->tree) {
        log_error("No parsed tree available for compilation");
        return ITEM_ERROR;
    }
    
    TSNode root = ts_tree_root_node(tp->tree);
    
    // Build JavaScript AST
    JsAstNode* js_ast = build_js_ast(tp, root);
    if (!js_ast) {
        log_error("Failed to build JavaScript AST");
        return ITEM_ERROR;
    }
    
    // Generate C code
    transpile_js_ast_root(tp, js_ast);
    
    if (tp->has_errors) {
        log_error("JavaScript transpilation failed with errors");
        if (tp->error_buf) {
            log_error("Errors:\n%s", strbuf_to_string(tp->error_buf));
        }
        return ITEM_ERROR;
    }
    
    // Get generated C code
    char* c_code = strbuf_to_string(tp->code_buf);
    log_debug("Generated JavaScript C code:\n%s", c_code);
    
    // TODO: Compile C code with MIR and execute
    // For now, just return the code as a string
    return s2it(c_code);
}

// Main entry point
Item transpile_js_to_c(Runtime* runtime, const char* js_source, const char* filename) {
    log_debug("Starting JavaScript transpilation for file: %s", filename ? filename : "<string>");
    
    // Create transpiler
    JsTranspiler* tp = js_transpiler_create(runtime);
    if (!tp) {
        log_error("Failed to create JavaScript transpiler");
        return ITEM_ERROR;
    }
    
    // Parse JavaScript source
    size_t source_length = strlen(js_source);
    if (!js_transpiler_parse(tp, js_source, source_length)) {
        js_transpiler_destroy(tp);
        return ITEM_ERROR;
    }
    
    // Compile to C code
    Item result = js_transpiler_compile(tp);
    
    // Cleanup
    js_transpiler_destroy(tp);
    
    log_debug("JavaScript transpilation completed");
    return result;
}
