#include "js_transpiler.hpp"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include "../../lib/mempool.h"
#include "../../lib/hashmap.h"
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include "../../lib/mem.h"

// TypeScript parser (unified: handles both JS and TS)
extern "C" {
    const TSLanguage* tree_sitter_typescript(void);
    const TSLanguage* tree_sitter_javascript(void);
}

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
    JsTranspiler* tp = (JsTranspiler*)mem_alloc(sizeof(JsTranspiler), MEM_CAT_JS_RUNTIME);
    memset(tp, 0, sizeof(JsTranspiler));

    // Initialize memory pools
    tp->ast_pool = pool_create(); // Memory pool
    tp->name_pool = name_pool_create(tp->ast_pool, NULL);
    tp->code_buf = strbuf_new();
    tp->func_buf = strbuf_new();  // Buffer for function expressions
    tp->error_buf = NULL;

    // Initialize Tree-sitter parser
    tp->parser = ts_parser_new();
    const TSLanguage* lang = tree_sitter_javascript();
    if (!lang) {
        lang = tree_sitter_typescript();
    }
    ts_parser_set_language(tp->parser, lang);

    // Initialize scopes
    tp->global_scope = js_scope_create(tp, JS_SCOPE_GLOBAL, NULL);
    tp->current_scope = tp->global_scope;
    tp->strict_mode = false;
    tp->function_counter = 0;
    tp->temp_var_counter = 0;
    tp->label_counter = 0;
    tp->in_expression = false;
    tp->has_errors = false;
    tp->strict_js = true;  // default: pure JS mode (reject TS syntax)
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

    // Release name_pool BEFORE destroying ast_pool: name_pool was allocated
    // from ast_pool, so pool_destroy would unmap its memory.
    if (tp->name_pool) {
        name_pool_release(tp->name_pool);
    }
    if (tp->ast_pool) {
        pool_destroy(tp->ast_pool);
    }
    if (tp->code_buf) {
        strbuf_free(tp->code_buf);
    }
    if (tp->func_buf) {
        strbuf_free(tp->func_buf);
    }
    if (tp->error_buf) {
        strbuf_free(tp->error_buf);
    }
    if (tp->type_registry) {
        hashmap_free(tp->type_registry);
    }

    mem_free(tp);
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
        // Recursively find the deepest error node
        TSNode current = root;
        for (int depth = 0; depth < 50; depth++) {
            bool found_error_child = false;
            uint32_t cc = ts_node_child_count(current);
            for (uint32_t i = 0; i < cc; i++) {
                TSNode child = ts_node_child(current, i);
                if (ts_node_is_missing(child)) {
                    TSPoint s = ts_node_start_point(child);
                    log_error("  [depth %d] MISSING node '%s' at line %u:%u",
                        depth, ts_node_type(child), s.row + 1, s.column);
                } else if (strcmp(ts_node_type(child), "ERROR") == 0) {
                    TSPoint s = ts_node_start_point(child);
                    TSPoint e = ts_node_end_point(child);
                    log_error("  [depth %d] ERROR node at line %u:%u - %u:%u",
                        depth, s.row + 1, s.column, e.row + 1, e.column);
                    // Print source around the error
                    uint32_t start_byte = ts_node_start_byte(child);
                    uint32_t end_byte = ts_node_end_byte(child);
                    uint32_t show_start = start_byte > 50 ? start_byte - 50 : 0;
                    uint32_t show_end = end_byte + 50 < length ? end_byte + 50 : (uint32_t)length;
                    char snippet[256];
                    uint32_t snip_len = show_end - show_start;
                    if (snip_len > 255) snip_len = 255;
                    memcpy(snippet, source + show_start, snip_len);
                    snippet[snip_len] = '\0';
                    // Replace newlines with spaces for readability
                    for (uint32_t k = 0; k < snip_len; k++) {
                        if (snippet[k] == '\n' || snippet[k] == '\r') snippet[k] = ' ';
                    }
                    log_error("  source: ...%s...", snippet);
                    current = child;
                    found_error_child = true;
                    break;
                } else if (ts_node_has_error(child)) {
                    TSPoint s = ts_node_start_point(child);
                    TSPoint e = ts_node_end_point(child);
                    log_error("  [depth %d] node '%s' has error, line %u:%u - %u:%u",
                        depth, ts_node_type(child), s.row + 1, s.column, e.row + 1, e.column);
                    current = child;
                    found_error_child = true;
                    break;
                }
            }
            if (!found_error_child) break;
        }
        log_error("JavaScript source has syntax errors");
        return false;
    }

    return true;
}
