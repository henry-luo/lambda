#include "js_mir_internal.hpp"

JsModuleConstEntry* g_eval_preamble_entries = NULL;
int g_eval_preamble_entry_count = 0;
int g_eval_preamble_var_count = 0;

// ============================================================================
// new Function(param1, param2, ..., body) — dynamic function compilation
// Called from JIT code when new Function(...) is encountered.
// Compiles a function body string into a new JsFunction object.
// ============================================================================
extern "C" Item js_new_function_from_string(Item* args, int argc) {
    if (!js_source_runtime) {
        log_error("js-new-function: no runtime context for dynamic function compilation");
        return ItemNull;
    }

    // Build the JS source for the function expression.
    // new Function("param1", "param2", "body") or new Function("body")
    // → (function(param1, param2) { body })
    // new Function() with no args → (function() {})
    StrBuf* sb = strbuf_new_cap(256);
    strbuf_append_str(sb, "(function(");

    // params are args[0..argc-2], body is args[argc-1]
    // per spec, each argument is converted to string via ToString()
    for (int i = 0; i < argc - 1; i++) {
        if (i > 0) strbuf_append_str(sb, ",");
        String* ps = it2s(args[i]);
        if (!ps) {
            Item str_item = js_to_string(args[i]);
            ps = it2s(str_item);
        }
        if (ps && ps->len > 0) {
            strbuf_append_str_n(sb, ps->chars, (int)ps->len);
        }
    }
    // Newline before ) is required by spec §20.2.1.1 to handle params ending with // comment
    strbuf_append_str(sb, "\n) {");

    // body
    String* body = (argc > 0) ? it2s(args[argc - 1]) : NULL;
    if (!body && argc > 0) {
        Item str_item = js_to_string(args[argc - 1]);
        body = it2s(str_item);
    }
    if (body && body->len > 0) {
        strbuf_append_str(sb, "\n");
        strbuf_append_str_n(sb, body->chars, (int)body->len);
        strbuf_append_str(sb, "\n");
    }

    strbuf_append_str(sb, "})");

    // null-terminate — use malloc; the transpiler will copy as needed
    char* source = (char*)mem_alloc(sb->length + 1, MEM_CAT_JS_RUNTIME);
    if (!source) {
        strbuf_free(sb);
        log_error("js-new-function: malloc failed for source buffer");
        return ItemNull;
    }
    memcpy(source, sb->str, sb->length);
    source[sb->length] = '\0';
    strbuf_free(sb);

    log_debug("js-new-function: compiling dynamic function body (len=%d)", (int)strlen(source));

    // Compile the function expression as a mini JS module.
    // Since the source is a top-level expression statement "(function(...) { ... })",
    // js_main() will evaluate it and return the function Item.
    JsTranspiler* tp = js_transpiler_create(js_source_runtime);
    if (!tp) {
        log_error("js-new-function: failed to create transpiler");
        return ItemNull;
    }

    if (!js_transpiler_parse(tp, source, strlen(source))) {
        log_error("js-new-function: parse failed for '%s'", source);
        mem_free(source);
        js_transpiler_destroy(tp);
        return ItemNull;
    }

    TSNode root = ts_tree_root_node(tp->tree);
    JsAstNode* js_ast = build_js_ast(tp, root);
    mem_free(source);  // source only needed for parsing and AST building
    if (!js_ast) {
        log_error("js-new-function: AST build failed");
        js_transpiler_destroy(tp);
        return ItemNull;
    }

    // Use optimize level 0 for dynamic code (eval/new Function) — small snippets
    // don't benefit from optimization but pay the full cost of each pass.
    MIR_context_t ctx = jit_init(0);
    if (!ctx) {
        log_error("js-new-function: MIR context init failed");
        js_transpiler_destroy(tp);
        return ItemNull;
    }

    // Install batch error handler if set (prevents exit(1) on MIR errors)
    if (g_batch_mir_error_handler) {
        MIR_set_error_func(ctx, g_batch_mir_error_handler);
    }

    JsMirTranspiler* mt = jm_create_mir_transpiler(tp, ctx, "<new Function>", false, 16, 8, 8, "js-new-function");
    if (!mt) {
        MIR_finish(ctx);
        js_transpiler_destroy(tp);
        return ItemNull;
    }

    // Inherit outer script's module_consts so eval()/new Function() can
    // resolve var declarations from the calling scope.
    if (g_eval_preamble_entries && g_eval_preamble_entry_count > 0) {
        mt->preamble_entries = g_eval_preamble_entries;
        mt->preamble_entry_count = g_eval_preamble_entry_count;
        mt->preamble_var_count = g_eval_preamble_var_count;
    }

    char module_name[48];
    snprintf(module_name, sizeof(module_name), "js_dynfunc_%d", js_dynamic_func_counter++);
    mt->module = MIR_new_module(ctx, module_name);

    transpile_js_mir_ast(mt, js_ast);

    if (!jm_validate_mir_labels(ctx)) {
        log_error("js-new-function: NULL labels detected");
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        js_transpiler_destroy(tp);
        return ItemNull;
    }

    MIR_link(ctx, g_mir_interp_mode ? MIR_set_interp_interface : MIR_set_gen_interface, import_resolver);

    typedef Item (*js_main_func_t)(Context*);
    js_main_func_t js_main_fn = (js_main_func_t)find_func(ctx, (char*)"js_main");

    if (!js_main_fn) {
        log_error("js-new-function: failed to find js_main");
        jm_destroy_mir_transpiler(mt);
        MIR_finish(ctx);
        js_transpiler_destroy(tp);
        return ItemNull;
    }

    // Execute js_main to get the compiled function Item
    Item fn_item = js_main_fn((Context*)context);

    // Cleanup transpiler but KEEP the MIR context alive (function code must persist).
    // Also keep name_pool and ast_pool alive: JIT code embeds raw String* pointers
    // interned in the name pool (via jm_box_string_literal). Freeing the pool would
    // leave dangling pointers in the generated code.
    jm_destroy_mir_transpiler(mt);
    jm_defer_mir_cleanup(ctx);
    // Attach name_pool and ast_pool to the deferred entry so they are freed
    // together with the MIR context (e.g., when eval() calls jm_finish_last_deferred_mir).
    if (module_mir_context_count > 0) {
        module_mir_name_pools[module_mir_context_count - 1] = tp->name_pool;
        module_mir_ast_pools[module_mir_context_count - 1] = tp->ast_pool;
    }
    // Detach from transpiler so js_transpiler_destroy doesn't free them.
    tp->name_pool = NULL;
    tp->ast_pool = NULL;
    js_transpiler_destroy(tp);

    log_debug("js-new-function: compiled dynamic function OK (type=%d)", get_type_id(fn_item));

    // Set spec-correct toString() source: "function anonymous(params\n) {\nbody\n}"
    // The internal source was (function(params) { body }) which doesn't match the spec.
    if (get_type_id(fn_item) == LMD_TYPE_FUNC) {
        StrBuf* src_buf = strbuf_new_cap(256);
        strbuf_append_str(src_buf, "function anonymous(");
        for (int i = 0; i < argc - 1; i++) {
            if (i > 0) strbuf_append_str(src_buf, ",");
            String* ps2 = it2s(args[i]);
            if (!ps2) {
                Item si = js_to_string(args[i]);
                ps2 = it2s(si);
            }
            if (ps2 && ps2->len > 0)
                strbuf_append_str_n(src_buf, ps2->chars, (int)ps2->len);
        }
        strbuf_append_str(src_buf, "\n) {\n");
        String* body2 = (argc > 0) ? it2s(args[argc - 1]) : NULL;
        if (!body2 && argc > 0) {
            Item si = js_to_string(args[argc - 1]);
            body2 = it2s(si);
        }
        if (body2 && body2->len > 0)
            strbuf_append_str_n(src_buf, body2->chars, (int)body2->len);
        strbuf_append_str(src_buf, "\n}");
        String* src_str = heap_create_name(src_buf->str, src_buf->length);
        strbuf_free(src_buf);
        Item src_item = (Item){.item = s2it(src_str)};
        js_set_function_source(fn_item, src_item);
    }

    return fn_item;
}

// ============================================================================
// eval() helper: insert "return " before the last expression statement
// to capture the completion value. Returns malloc'd string or NULL.
// ============================================================================
char* eval_try_insert_return(const char* code, size_t len) {
    // Forward-scan to find the start of the last top-level statement,
    // handling strings, comments, and nesting so inner semicolons are skipped.
    size_t last_stmt_start = 0;
    int brace = 0, paren = 0, bracket = 0;
    bool in_sq = false, in_dq = false, in_tpl = false;
    bool in_line_cmt = false, in_block_cmt = false;

    for (size_t i = 0; i < len; i++) {
        char c = code[i];
        char n = (i + 1 < len) ? code[i + 1] : 0;

        if (in_line_cmt)  { if (c == '\n') in_line_cmt = false; continue; }
        if (in_block_cmt) { if (c == '*' && n == '/') { in_block_cmt = false; i++; } continue; }

        if (!in_sq && !in_dq && !in_tpl) {
            if (c == '/' && n == '/') { in_line_cmt  = true;  i++; continue; }
            if (c == '/' && n == '*') { in_block_cmt = true;  i++; continue; }
        }

        if (in_sq)  { if (c == '\\') { i++; continue; } if (c == '\'') in_sq  = false; continue; }
        if (in_dq)  { if (c == '\\') { i++; continue; } if (c == '"')  in_dq  = false; continue; }
        if (in_tpl) { if (c == '\\') { i++; continue; } if (c == '`')  in_tpl = false; continue; }

        if (c == '\'') { in_sq  = true; continue; }
        if (c == '"')  { in_dq  = true; continue; }
        if (c == '`')  { in_tpl = true; continue; }

        if (c == '(') paren++;   else if (c == ')') { if (paren   > 0) paren--;   }
        if (c == '[') bracket++; else if (c == ']') { if (bracket > 0) bracket--; }
        if (c == '{') brace++;   else if (c == '}') { if (brace   > 0) brace--;   }

        // Top-level semicolon — next non-whitespace char starts next statement
        if (c == ';' && brace == 0 && paren == 0 && bracket == 0) {
            size_t nxt = i + 1;
            while (nxt < len && (code[nxt] == ' ' || code[nxt] == '\t' ||
                                 code[nxt] == '\n' || code[nxt] == '\r'))
                nxt++;
            if (nxt < len) last_stmt_start = nxt;
        }
    }

    // Skip leading whitespace of last statement
    size_t ls = last_stmt_start;
    while (ls < len && (code[ls] == ' ' || code[ls] == '\t' ||
                        code[ls] == '\n' || code[ls] == '\r'))
        ls++;
    if (ls >= len) return NULL;

    // Extract the first identifier-word of the last statement
    size_t we = ls;
    while (we < len && ((code[we] >= 'a' && code[we] <= 'z') ||
                        (code[we] >= 'A' && code[we] <= 'Z') ||
                        (code[we] >= '0' && code[we] <= '9') ||
                        code[we] == '_' || code[we] == '$'))
        we++;
    size_t wlen = we - ls;

    // Keywords that start non-expression statements — don't prepend return
    static const char* kw[] = {
        "var", "let", "const", "function", "class",
        "if", "for", "while", "do", "switch", "try", "with",
        "throw", "return", "break", "continue", "debugger",
        "import", "export", NULL
    };
    for (int k = 0; kw[k]; k++) {
        size_t klen = strlen(kw[k]);
        if (wlen == klen && memcmp(code + ls, kw[k], klen) == 0)
            return NULL;
    }

    // Also skip if last statement starts with '{' (block)
    if (code[ls] == '{') return NULL;

    // Skip if last statement is empty (just ';') — completion is from the previous stmt
    if (code[ls] == ';') return NULL;

    // Build: code[0..ls] + "return " + code[ls..len]
    size_t total = len + 7 + 1;
    char* result = (char*)mem_alloc(total, MEM_CAT_JS_RUNTIME);
    if (!result) return NULL;
    memcpy(result, code, ls);
    memcpy(result + ls, "return ", 7);
    memcpy(result + ls + 7, code + ls, len - ls);
    result[total - 1] = '\0';
    return result;
}

// ============================================================================
// eval(code) — dynamic evaluation of JavaScript source code
// Wraps the code in an IIFE and compiles/executes via JIT.
// is_global_scope: 1 = called at global level, 0 = called inside a function
// ============================================================================
extern "C" Item js_builtin_eval(Item code_item, int64_t is_global_scope) {
    if (!js_source_runtime) {
        log_error("js-eval: no runtime context for dynamic evaluation");
        return ItemNull;
    }
    if (get_type_id(code_item) != LMD_TYPE_STRING) {
        // eval(non-string) returns the argument unchanged (ES spec)
        return code_item;
    }
    String* code_str = it2s(code_item);
    if (!code_str || code_str->len == 0) return ItemNull;

    // Check for whitespace/comment-only code — should return undefined
    {
        const char* s = code_str->chars;
        size_t slen = code_str->len;
        size_t i = 0;
        bool has_code = false;
        while (i < slen) {
            char c = s[i];
            // skip whitespace
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { i++; continue; }
            // skip line comments
            if (c == '/' && i + 1 < slen && s[i+1] == '/') {
                i += 2;
                while (i < slen && s[i] != '\n') i++;
                continue;
            }
            // skip block comments
            if (c == '/' && i + 1 < slen && s[i+1] == '*') {
                i += 2;
                while (i + 1 < slen && !(s[i] == '*' && s[i+1] == '/')) i++;
                if (i + 1 < slen) i += 2;
                continue;
            }
            has_code = true;
            break;
        }
        if (!has_code) return ItemNull;
    }

    // Fast path: if code is a single RegExp literal, construct directly
    // without going through the full parse → AST → JIT pipeline.
    // Pattern: /.../ optionally followed by [gimsuy]* flags, nothing else.
    if (code_str->len >= 2 && code_str->chars[0] == '/') {
        size_t i = 1;
        bool in_class = false;
        while (i < code_str->len) {
            char c = code_str->chars[i];
            if (c == '\\' && i + 1 < code_str->len) { i += 2; continue; }
            if (c == '[') { in_class = true; i++; continue; }
            if (c == ']' && in_class) { in_class = false; i++; continue; }
            if (c == '/' && !in_class) break;
            i++;
        }
        if (i < code_str->len) {
            size_t flags_start = i + 1;
            bool valid = true;
            for (size_t j = flags_start; j < code_str->len; j++) {
                char f = code_str->chars[j];
                if (!(f == 'g' || f == 'i' || f == 'm' || f == 's' || f == 'u' || f == 'y')) {
                    valid = false;
                    break;
                }
            }
            if (valid && flags_start <= code_str->len) {
                extern Item js_create_regexp_from_source(const char* src, size_t len);
                return js_create_regexp_from_source(code_str->chars, code_str->len);
            }
        }
    }

    extern Item js_call_function(Item func, Item this_val, Item* args, int argc);
    size_t code_len = code_str->len;
    Item fn_item = ItemNull;

    // v37: Phase A — try expression form for single-expression eval code.
    // Wraps as "return (code)\n" inside a function IIFE. This handles simple
    // cases like eval("1+2"), eval("new.target"), etc. and preserves function
    // context (new.target, arguments, super) that a top-level script wouldn't have.
    // Skip if code starts with a declaration keyword or contains semicolons
    // (multi-statement code should go to Phase C for correct scoping).
    {
        bool skip_expr_form = false;
        const char* s = code_str->chars;
        size_t slen = code_str->len;
        // Skip leading whitespace
        size_t i = 0;
        while (i < slen && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
        if (i < slen) {
            if (slen - i >= 8 && memcmp(s + i, "function", 8) == 0 &&
                (i + 8 >= slen || s[i+8] == ' ' || s[i+8] == '*' || s[i+8] == '('))
                skip_expr_form = true;
            else if (slen - i >= 5 && memcmp(s + i, "class", 5) == 0 &&
                (i + 5 >= slen || s[i+5] == ' ' || s[i+5] == '{'))
                skip_expr_form = true;
            else if (slen - i >= 5 && memcmp(s + i, "async", 5) == 0 &&
                (i + 5 >= slen || s[i+5] == ' '))
                skip_expr_form = true;
            // v37: '{' at start is a block statement, not an object literal
            else if (s[i] == '{')
                skip_expr_form = true;
            // v37: statement keywords that can't be expressions
            else if (slen - i >= 3 && memcmp(s + i, "var", 3) == 0 &&
                (i + 3 >= slen || s[i+3] == ' '))
                skip_expr_form = true;
            else if (slen - i >= 3 && memcmp(s + i, "let", 3) == 0 &&
                (i + 3 >= slen || s[i+3] == ' '))
                skip_expr_form = true;
            else if (slen - i >= 5 && memcmp(s + i, "const", 5) == 0 &&
                (i + 5 >= slen || s[i+5] == ' '))
                skip_expr_form = true;
            else if (slen - i >= 2 && memcmp(s + i, "if", 2) == 0 &&
                (i + 2 >= slen || s[i+2] == ' ' || s[i+2] == '('))
                skip_expr_form = true;
            else if (slen - i >= 3 && memcmp(s + i, "for", 3) == 0 &&
                (i + 3 >= slen || s[i+3] == ' ' || s[i+3] == '('))
                skip_expr_form = true;
            else if (slen - i >= 5 && memcmp(s + i, "while", 5) == 0 &&
                (i + 5 >= slen || s[i+5] == ' ' || s[i+5] == '('))
                skip_expr_form = true;
            else if (slen - i >= 6 && memcmp(s + i, "switch", 6) == 0 &&
                (i + 6 >= slen || s[i+6] == ' ' || s[i+6] == '('))
                skip_expr_form = true;
            else if (slen - i >= 3 && memcmp(s + i, "try", 3) == 0 &&
                (i + 3 >= slen || s[i+3] == ' ' || s[i+3] == '{'))
                skip_expr_form = true;
            else if (slen - i >= 2 && memcmp(s + i, "do", 2) == 0 &&
                (i + 2 >= slen || s[i+2] == ' ' || s[i+2] == '{'))
                skip_expr_form = true;
            else if (slen - i >= 5 && memcmp(s + i, "throw", 5) == 0 &&
                (i + 5 >= slen || s[i+5] == ' '))
                skip_expr_form = true;
            else if (slen - i >= 6 && memcmp(s + i, "return", 6) == 0 &&
                (i + 6 >= slen || s[i+6] == ' ' || s[i+6] == ';'))
                skip_expr_form = true;
            else if (slen - i >= 5 && memcmp(s + i, "break", 5) == 0 &&
                (i + 5 >= slen || s[i+5] == ' ' || s[i+5] == ';'))
                skip_expr_form = true;
            else if (slen - i >= 8 && memcmp(s + i, "continue", 8) == 0 &&
                (i + 8 >= slen || s[i+8] == ' ' || s[i+8] == ';'))
                skip_expr_form = true;
            else if (slen - i >= 6 && memcmp(s + i, "import", 6) == 0 &&
                (i + 6 >= slen || s[i+6] == ' '))
                skip_expr_form = true;
            else if (slen - i >= 6 && memcmp(s + i, "export", 6) == 0 &&
                (i + 6 >= slen || s[i+6] == ' '))
                skip_expr_form = true;
        }
        // v37: Also skip expression form if code contains semicolons (multi-statement)
        // or declarations that need to be compiled as a program for correct scoping.
        if (!skip_expr_form) {
            for (size_t j = i; j < slen; j++) {
                char c = s[j];
                if (c == ';') { skip_expr_form = true; break; }
                // Skip string literals to avoid false semicolon detection
                if (c == '\'' || c == '"' || c == '`') {
                    char q = c;
                    j++;
                    while (j < slen && s[j] != q) {
                        if (s[j] == '\\') j++; // skip escape
                        j++;
                    }
                }
            }
        }
        if (!skip_expr_form) {
            const char* prefix = "return (";
            const char* suffix = "\n)";
            size_t plen = strlen(prefix), slen2 = strlen(suffix);
            size_t total = plen + code_len + slen2 + 1;
            char* body = (char*)mem_alloc(total, MEM_CAT_JS_RUNTIME);
            if (!body) return ItemNull;
            memcpy(body, prefix, plen);
            memcpy(body + plen, code_str->chars, code_len);
            memcpy(body + plen + code_len, suffix, slen2);
            body[total - 1] = '\0';

            Item body_item = (Item){.item = s2it(heap_create_name(body, total - 1))};
            mem_free(body);
            fn_item = js_new_function_from_string(&body_item, 1);
        }
    }

    // If expression form succeeded, call and return
    if (fn_item.item != 0 && fn_item.item != ITEM_NULL && fn_item.item != ITEM_ERROR) {
        Item result = js_call_function(fn_item, ItemNull, NULL, 0);
        return result;
    }

    // v37: Phase C — compile code directly as a top-level script (not wrapped in a function).
    // Skip Phase B (return insertion) which still wrapped in function body (wrong scoping).
    // Phase C handles completion values via eval_completion_reg and var export
    // via is_eval_direct, making it spec-compliant for multi-statement code.
    {
        const char* source = code_str->chars;
        size_t source_len = code_str->len;

        JsTranspiler* tp = js_transpiler_create(js_source_runtime);
        if (!tp) {
            log_error("js-eval: failed to create transpiler for direct script");
            return ItemNull;
        }

        if (!js_transpiler_parse(tp, source, source_len)) {
            log_error("js-eval: parse failed for direct script");
            js_transpiler_destroy(tp);
            return ItemNull;
        }

        TSNode root = ts_tree_root_node(tp->tree);
        JsAstNode* js_ast = build_js_ast(tp, root);
        if (!js_ast) {
            log_error("js-eval: AST build failed for direct script");
            js_transpiler_destroy(tp);
            return ItemNull;
        }

        // Use optimize level 0 for eval code — small snippets don't benefit
        // from optimization but pay the full cost of each pass.
        MIR_context_t eval_ctx = jit_init(0);
        if (!eval_ctx) {
            log_error("js-eval: MIR context init failed");
            js_transpiler_destroy(tp);
            return ItemNull;
        }

        if (g_batch_mir_error_handler) {
            MIR_set_error_func(eval_ctx, g_batch_mir_error_handler);
        }

        JsMirTranspiler* mt = jm_create_mir_transpiler(tp, eval_ctx, "<eval>", false, 16, 8, 8, "js-eval");
        if (!mt) {
            MIR_finish(eval_ctx);
            js_transpiler_destroy(tp);
            return ItemNull;
        }
        mt->is_eval_direct = (is_global_scope != 0);  // sloppy-mode eval: export vars to globalThis

        // Inherit outer script's module_consts so eval() can resolve var declarations
        if (g_eval_preamble_entries && g_eval_preamble_entry_count > 0) {
            mt->preamble_entries = g_eval_preamble_entries;
            mt->preamble_entry_count = g_eval_preamble_entry_count;
            mt->preamble_var_count = g_eval_preamble_var_count;
        }

        char module_name[48];
        snprintf(module_name, sizeof(module_name), "js_eval_%d", js_dynamic_func_counter++);
        mt->module = MIR_new_module(eval_ctx, module_name);

        transpile_js_mir_ast(mt, js_ast);

        if (!jm_validate_mir_labels(eval_ctx)) {
            log_error("js-eval: NULL labels detected in direct script");
            jm_destroy_mir_transpiler(mt);
            MIR_finish(eval_ctx);
            js_transpiler_destroy(tp);
            return ItemNull;
        }

        MIR_link(eval_ctx, g_mir_interp_mode ? MIR_set_interp_interface : MIR_set_gen_interface, import_resolver);

        typedef Item (*js_main_func_t)(Context*);
        js_main_func_t js_main_fn = (js_main_func_t)find_func(eval_ctx, (char*)"js_main");

        if (!js_main_fn) {
            log_error("js-eval: failed to find js_main in direct script");
            jm_destroy_mir_transpiler(mt);
            MIR_finish(eval_ctx);
            js_transpiler_destroy(tp);
            return ItemNull;
        }

        // Execute js_main directly — returns the completion value
        // v37: Save/restore new.target, set to undefined for eval code.
        // Per ES spec, eval in class field initializers runs "outside a constructor",
        // so new.target should be undefined, not the enclosing constructor.
        extern Item js_get_new_target();
        extern void js_set_direct_new_target(Item);
        Item prev_nt = js_get_new_target();
        js_set_direct_new_target((Item){.item = ITEM_JS_UNDEFINED});

        Item result = js_main_fn((Context*)context);

        js_set_direct_new_target(prev_nt);

        // Cleanup
        jm_destroy_mir_transpiler(mt);
        // Defer MIR context cleanup — eval code may return closures/functions
        // whose JIT pointers must remain valid, and string literals from the
        // name_pool/ast_pool may be captured by variables or closures.
        jm_defer_mir_cleanup(eval_ctx);
        // Do NOT destroy the transpiler eagerly — its name_pool backs string
        // literals that may still be referenced.  Cleanup at program exit.
        // js_transpiler_destroy(tp);

        return result;
    }
}

// ============================================================================
// Public entry point: transpile a pre-built JS AST to MIR (used by TS transpiler)
// ============================================================================
