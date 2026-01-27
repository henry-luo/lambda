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

// External reference to Lambda runtime context pointer (defined in mir.c)
extern "C" Context* _lambda_rt;

// External reference to the global evaluation context (defined in runner.cpp)
extern __thread EvalContext* context;

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
    JsAstNode* js_ast = build_js_ast(tp, root);
    if (!js_ast) {
        log_error("Failed to build JavaScript AST");
        return (Item){.item = ITEM_ERROR};
    }

    // Generate C code
    transpile_js_ast_root(tp, js_ast);

    if (tp->has_errors) {
        if (tp->error_buf) {
            log_error("Errors:\n%s", tp->error_buf->str);
        }
        return (Item){.item = ITEM_ERROR};
    }

    // Get generated C code
    char* c_code = tp->code_buf->str;
    size_t code_len = tp->code_buf->length;
    
    if (!c_code || code_len == 0) {
        log_error("Generated C code is empty!");
        return (Item){.item = ITEM_NULL};
    }
    
    log_debug("Generated JavaScript C code (length: %zu)", code_len);
    
    // Write generated C code to file for debugging
    FILE* debug_file = fopen("_transpiled_js.c", "w");
    if (debug_file) {
        fwrite(c_code, 1, code_len, debug_file);
        fclose(debug_file);
        log_debug("Wrote generated C code to _transpiled_js.c");
    }
    
    // Initialize MIR JIT context
    MIR_context_t jit_ctx = jit_init(2);  // Use optimization level 2
    if (!jit_ctx) {
        log_error("Failed to initialize MIR JIT context");
        return (Item){.item = ITEM_ERROR};
    }
    
    // For now, we don't set up _lambda_rt - JS runtime functions
    // don't need the full Lambda context (they use heap_alloc directly)
    
    // Compile the C code to MIR
    log_debug("Compiling JavaScript to MIR...");
    jit_compile_to_mir(jit_ctx, c_code, code_len, "javascript.js");
    
    // Generate native code for the js_main function
    log_notice("Generating native code for JavaScript...");
    typedef Item (*js_main_func_t)(Context*);
    js_main_func_t js_main = (js_main_func_t)jit_gen_func(jit_ctx, (char*)"js_main");
    
    if (!js_main) {
        log_error("Failed to generate native code for js_main");
        MIR_finish(jit_ctx);
        return (Item){.item = ITEM_ERROR};
    }
    
    // Set up a minimal evaluation context for JS runtime
    // The push_d/push_l functions use the global 'context' variable
    EvalContext js_context;
    memset(&js_context, 0, sizeof(EvalContext));
    js_context.num_stack = num_stack_create(16);  // Create num_stack for push_d/push_l
    
    // Save old context and set new one
    EvalContext* old_context = context;
    context = &js_context;
    
    // Initialize heap for JS execution
    heap_init();
    context->pool = context->heap->pool;
    
    // Initialize name_pool for string interning (required for heap_create_name)
    context->name_pool = name_pool_create(context->pool, nullptr);
    if (!context->name_pool) {
        log_error("Failed to create JS runtime name_pool");
    }
    
    // Execute the JIT compiled JavaScript code
    log_notice("Executing JIT compiled JavaScript code...");
    Item result = js_main(_lambda_rt);
    
    // Copy the result value before destroying the heap
    // For simple scalars (int, bool), the value is in the Item itself
    // For float, we need to copy the double value (it's on num_stack which we're about to destroy)
    Item copied_result;
    TypeId type_id = get_type_id(result);
    if (type_id == LMD_TYPE_FLOAT) {
        // Float values are stored on num_stack - need to copy the actual value
        double value = it2d(result);
        log_debug("JS result: float = %g", value);
        // For now, return an int representation if it's a whole number
        if (value == (int)value && value >= INT32_MIN && value <= INT32_MAX) {
            copied_result = (Item){.item = i2it((int)value)};
        } else {
            // For true floats, we can't easily preserve them without heap
            // Print and return null for now
            printf("%g", value);
            copied_result = (Item){.item = ITEM_NULL};
        }
    } else if (type_id == LMD_TYPE_INT) {
        // Int values are stored directly in the Item
        copied_result = result;
    } else if (type_id == LMD_TYPE_BOOL) {
        copied_result = result;
    } else if (type_id == LMD_TYPE_NULL) {
        copied_result = result;
    } else {
        // For complex types, we can't preserve across heap destruction
        log_debug("JS result has complex type %d, returning null", type_id);
        copied_result = (Item){.item = ITEM_NULL};
    }
    
    // Clean up JS context
    if (js_context.num_stack) {
        num_stack_destroy((num_stack_t*)js_context.num_stack);
    }
    heap_destroy();
    
    // Restore old context
    context = old_context;
    
    // Clean up MIR context
    MIR_finish(jit_ctx);
    
    return copied_result;
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
