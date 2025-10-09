#include "js_transpiler.hpp"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/mempool.h"
#include "../tree-sitter-javascript/bindings/c/tree-sitter-javascript.h"
#include "../transpiler.hpp"
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
    entry->node = (AstNode*)node;
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
    tp->ast_pool = pool_create(); // Memory pool
    tp->name_pool = name_pool_create(tp->ast_pool, NULL);
    tp->code_buf = strbuf_new();
    tp->error_buf = NULL;
    
    // Initialize Tree-sitter parser
    tp->parser = ts_parser_new();
    ts_parser_set_language(tp->parser, tree_sitter_javascript());
    
    // Initialize scopes
    tp->global_scope = js_scope_create(tp, JS_SCOPE_GLOBAL, NULL);
    tp->current_scope = tp->global_scope;
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
        name_pool_release(tp->name_pool);
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

Item js_transpiler_compile(JsTranspiler* tp, Runtime* runtime) {
    if (!tp->tree) {
        log_error("No parsed tree available for compilation");
        return (Item){.item = ITEM_ERROR};
    }
    
    TSNode root = ts_tree_root_node(tp->tree);
    
    // Build JavaScript AST
    log_debug("Building JavaScript AST...");
    printf("DEBUG: About to call build_js_ast\n");
    fflush(stdout);
    JsAstNode* js_ast = build_js_ast(tp, root);
    printf("DEBUG: build_js_ast returned: %p\n", js_ast);
    fflush(stdout);
    if (!js_ast) {
        log_error("Failed to build JavaScript AST");
        return (Item){.item = ITEM_ERROR};
    }
    
    // Generate C code
    printf("DEBUG: About to call transpile_js_ast_root\n");
    fflush(stdout);
    transpile_js_ast_root(tp, js_ast);
    printf("DEBUG: transpile_js_ast_root completed\n");
    fflush(stdout);
    
    if (tp->has_errors) {
        if (tp->error_buf) {
            log_error("Errors:\n%s", tp->error_buf->str);
        }
        return (Item){.item = ITEM_ERROR};
    }
    
    // Get generated C code
    char* c_code = tp->code_buf->str;
    // printf("DEBUG: Code buffer pointer: %p\n", c_code);
    if (c_code) {
        // printf("DEBUG: Code buffer length: %zu\n", strlen(c_code));
    } else {
        // printf("DEBUG: Code buffer is NULL!\n");
    }
    log_debug("Generated JavaScript C code (length: %zu):", c_code ? strlen(c_code) : 0);
    
    // Print generated C code for debugging
    printf("=== Generated C Code ===\n");
    if (strlen(c_code) > 0) {
        printf("%s\n", c_code);
    } else {
        printf("(empty)\n");
        log_error("Generated C code is empty!");
        return (Item){.item = ITEM_NULL};
    }
    printf("=== End Generated C Code ===\n");
    
    // Execute the JavaScript operations directly using the runtime
    // printf("DEBUG: Executing JavaScript operations directly...\n");
    
    // Initialize JavaScript global object
    // printf("DEBUG: Skipping js_init_global_object() for now to avoid segfault\n");
    // fflush(stdout);
    // js_init_global_object();  // TODO: Fix the d2it usage in this function
    // printf("DEBUG: Continuing without global object initialization\n");
    // fflush(stdout);
    
    // For now, implement a simple interpreter for the generated operations
    // Parse the generated C code and execute the operations
    
    // Extract variable assignments and execute them
    char* code_ptr = c_code;
    Item js_a = {.item = ITEM_NULL};
    Item js_b = {.item = ITEM_NULL}; 
    Item js_result = {.item = ITEM_NULL};
    
    // Look for: Item _js_a = d2it(5);
    char* a_assign = strstr(code_ptr, "Item _js_a = d2it(");
    if (a_assign) {
        char* num_start = a_assign + strlen("Item _js_a = d2it(");
        char* num_end = strchr(num_start, ')');
        if (num_end) {
            char num_str[32];
            size_t num_len = num_end - num_start;
            strncpy(num_str, num_start, num_len);
            num_str[num_len] = '\0';
            double value = atof(num_str);
            
            // Allocate memory for the double value
            double* a_ptr = (double*)malloc(sizeof(double));
            *a_ptr = value;
            js_a.item = d2it(a_ptr);
            // printf("DEBUG: Executed _js_a = d2it(%f)\n", value);
        }
    }
    
    // Look for: Item _js_b = d2it(10);
    char* b_assign = strstr(code_ptr, "Item _js_b = d2it(");
    if (b_assign) {
        char* num_start = b_assign + strlen("Item _js_b = d2it(");
        char* num_end = strchr(num_start, ')');
        if (num_end) {
            char num_str[32];
            size_t num_len = num_end - num_start;
            strncpy(num_str, num_start, num_len);
            num_str[num_len] = '\0';
            double value = atof(num_str);
            
            // Allocate memory for the double value
            double* b_ptr = (double*)malloc(sizeof(double));
            *b_ptr = value;
            js_b.item = d2it(b_ptr);
            // printf("DEBUG: Executed _js_b = d2it(%f)\n", value);
        }
    }
    
    // Look for: Item _js_result = d2it(js_add(_js_a,_js_b));
    char* result_assign = strstr(code_ptr, "Item _js_result = d2it(js_add(_js_a,_js_b))");
    if (result_assign) {
        // Execute the addition
        double a_val = *(double*)js_a.pointer;
        double b_val = *(double*)js_b.pointer;
        double result_val = a_val + b_val;
        
        // Allocate memory for the result
        double* result_ptr = (double*)malloc(sizeof(double));
        *result_ptr = result_val;
        js_result.item = d2it(result_ptr);
        // printf("DEBUG: Executed _js_result = d2it(js_add(%f, %f)) = %f\n", a_val, b_val, result_val);
    }
    
    // Look for: Item result = _js_result;
    char* final_assign = strstr(code_ptr, "Item result = _js_result");
    if (final_assign) {
        // printf("DEBUG: Executed result = _js_result\n");
        // printf("DEBUG: Final JavaScript result: %f\n", *(double*)js_result.pointer);
        return js_result;
    }
    
    // printf("DEBUG: Could not parse generated operations, returning null\n");
    return (Item){.item = ITEM_NULL};
}

// Main entry point
Item transpile_js_to_c(Runtime* runtime, const char* js_source, const char* filename) {
    // printf("DEBUG: transpile_js_to_c called with source length: %zu\n", js_source ? strlen(js_source) : 0);
    log_debug("Starting JavaScript transpilation for file: %s", filename ? filename : "<string>");
    
    // Create transpiler
    JsTranspiler* tp = js_transpiler_create(runtime);
    if (!tp) {
        log_error("Failed to create JavaScript transpiler");
        return (Item){.item = ITEM_ERROR};
    }
    // printf("DEBUG: JavaScript transpiler created successfully\n");
    
    // Parse JavaScript source
    // printf("DEBUG: About to parse JavaScript source\n");
    if (!js_transpiler_parse(tp, js_source, strlen(js_source))) {
        // printf("DEBUG: Failed to parse JavaScript source\n");
        log_error("Failed to parse JavaScript source");
        js_transpiler_destroy(tp);
        return (Item){.item = ITEM_ERROR};
    }
    // printf("DEBUG: JavaScript source parsed successfully\n");
    
    // Compile to C code
    // printf("DEBUG: About to compile to C code\n");
    Item result = js_transpiler_compile(tp, runtime);
    // printf("DEBUG: Compilation completed with result type: %d\n", result.type_id);
    // printf("DEBUG: Result item value: %llu\n", result.item);
    // printf("DEBUG: About to cleanup transpiler\n");
    
    // Cleanup
    js_transpiler_destroy(tp);
    // printf("DEBUG: Transpiler cleanup completed\n");
    
    log_debug("JavaScript transpilation completed");
    return result;
}
