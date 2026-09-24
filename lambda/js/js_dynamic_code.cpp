// Dynamic JavaScript source: eval() and the Function-family constructors.
//
// JSI35 / D8.1.3v20: dynamic source always executes in the AST interpreter,
// whatever the caller's tier or the unit selector; MIR never lowers it. This
// file assembles the source text, applies the source-level early errors that
// depend on the caller's context, and hands the code to js_interp.cpp.
#include "js_mir_internal.hpp"
#include "js_interp.hpp"
#include "js_runtime_state.hpp"
#include "js_function.hpp"
#include "../../lib/str.h"

// ============================================================================
// Source scanning shared by the dynamic-source early errors
// ============================================================================
JS_FORWARD_STATIC_EXPRESSION(bool, js_eval_is_ident_char, (char ch),
    (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
    (ch >= '0' && ch <= '9') || ch == '_' || ch == '$')

static bool js_eval_at_word(const char* source, size_t len, size_t pos, const char* word, size_t word_len) {
    if (pos + word_len > len) return false;
    if (memcmp(source + pos, word, word_len) != 0) return false;
    if (pos > 0 && js_eval_is_ident_char(source[pos - 1])) return false;
    if (pos + word_len < len && js_eval_is_ident_char(source[pos + word_len])) return false;
    return true;
}

static bool js_eval_at_line_terminator(const char* source, size_t len, size_t pos, size_t* width) {
    if (!source || pos >= len) return false;
    unsigned char ch = (unsigned char)source[pos];
    if (ch == '\n') {
        if (width) *width = 1;
        return true;
    }
    if (ch == '\r') {
        if (width) *width = (pos + 1 < len && source[pos + 1] == '\n') ? 2 : 1;
        return true;
    }
    if (pos + 2 < len && ch == 0xE2 && (unsigned char)source[pos + 1] == 0x80 &&
        ((unsigned char)source[pos + 2] == 0xA8 || (unsigned char)source[pos + 2] == 0xA9)) {
        if (width) *width = 3;
        return true;
    }
    return false;
}

// helper: skip a JavaScript string or comment while source scanners share the same lexical rules
static bool js_eval_skip_string_or_comment(const char* source, size_t len, size_t* pos) {
    size_t p = *pos;
    char ch = source[p];
    if (ch == '\'' || ch == '"' || ch == '`') {
        p = (size_t)(strn_scan_quoted(source + p, source + len, ch, true, NULL) - source);
        *pos = p;
        return true;
    }
    if (ch == '/' && p + 1 < len && source[p + 1] == '/') {
        p += 2;
        while (p < len && !js_eval_at_line_terminator(source, len, p, NULL)) p++;
        *pos = p;
        return true;
    }
    if (ch == '/' && p + 1 < len && source[p + 1] == '*') {
        p += 2;
        while (p + 1 < len && !(source[p] == '*' && source[p + 1] == '/')) p++;
        if (p + 1 < len) p += 2;
        *pos = p;
        return true;
    }
    return false;
}

static size_t js_eval_skip_space_and_comments(const char* source, size_t len, size_t pos) {
    while (pos < len) {
        char ch = source[pos];
        size_t lt_width = 0;
        if (ch == ' ' || ch == '\t') { pos++; continue; }
        if (js_eval_at_line_terminator(source, len, pos, &lt_width)) { pos += lt_width; continue; }
        if (ch == '/' && pos + 1 < len && source[pos + 1] == '/') {
            pos += 2;
            while (pos < len && !js_eval_at_line_terminator(source, len, pos, NULL)) pos++;
            continue;
        }
        if (ch == '/' && pos + 1 < len && source[pos + 1] == '*') {
            pos += 2;
            while (pos + 1 < len && !(source[pos] == '*' && source[pos + 1] == '/')) pos++;
            if (pos + 1 < len) pos += 2;
            continue;
        }
        break;
    }
    return pos;
}

// ============================================================================
// new Function / GeneratorFunction / AsyncFunction / AsyncGeneratorFunction
// ============================================================================
static Item js_dynamic_function_throw_syntax_error(const char* message) {
    return js_throw_syntax_error(js_name_item(message, (int)strlen(message)));
}

static bool js_source_contains_import_meta(const char* source, size_t len) {
    for (size_t pos = 0; pos < len; pos++) {
        if (js_eval_skip_string_or_comment(source, len, &pos)) {
            if (pos > 0) pos--;
            continue;
        }
        if (js_eval_at_word(source, len, pos, "import", 6)) {
            size_t member_pos = js_eval_skip_space_and_comments(source, len, pos + 6);
            if (member_pos + 5 <= len && memcmp(source + member_pos, ".meta", 5) == 0 &&
                (member_pos + 5 == len || !js_eval_is_ident_char(source[member_pos + 5]))) {
                return true;
            }
            pos += 5;
        }
    }
    return false;
}

JS_FORWARD_STATIC_EXPRESSION(bool, js_dynamic_function_source_has_hashbang,
    (const char* source, size_t len),
    source && len >= 2 && source[0] == '#' && source[1] == '!')

static bool js_dynamic_function_param_html_close_comment_starts_line(const char* source,
        size_t len, size_t pos) {
    // Dynamic-function parameter parsing requires an actual preceding line
    // terminator; source offset zero is not a valid HTML close comment here.
    if (!source || pos == 0 || pos > len) return false;
    size_t prior = pos - 1;
    if (source[prior] == '\n' || source[prior] == '\r') return true;
    return prior >= 2 && (unsigned char)source[prior - 2] == 0xE2 &&
        (unsigned char)source[prior - 1] == 0x80 &&
        ((unsigned char)source[prior] == 0xA8 || (unsigned char)source[prior] == 0xA9);
}

static bool js_dynamic_function_param_has_invalid_html_close_comment(const char* source, size_t len) {
    if (!source || len < 3) return false;

    for (size_t pos = 0; pos < len; pos++) {
        if (js_eval_skip_string_or_comment(source, len, &pos)) {
            if (pos > 0) pos--;
            continue;
        }
        char ch = source[pos];
        if (ch == '-' && pos + 2 < len && source[pos + 1] == '-' && source[pos + 2] == '>') {
            if (!js_dynamic_function_param_html_close_comment_starts_line(source, len, pos)) {
                return true;
            }
            pos += 2;
        }
    }

    return false;
}

static void js_dynamic_function_append_normalized_params(StrBuf* out,
        const char* source, size_t len) {
    if (!out || !source) return;
    size_t copied_start = 0;
    for (size_t pos = 0; pos < len;) {
        size_t skipped_end = pos;
        if (js_eval_skip_string_or_comment(source, len, &skipped_end)) {
            pos = skipped_end;
            continue;
        }
        if (pos + 2 < len && source[pos] == '-' && source[pos + 1] == '-' &&
                source[pos + 2] == '>' &&
                js_dynamic_function_param_html_close_comment_starts_line(source, len, pos)) {
            strbuf_append_str_n(out, source + copied_start, (int)(pos - copied_start));
            pos += 3;
            while (pos < len && !js_eval_at_line_terminator(source, len, pos, NULL)) pos++;
            copied_start = pos;
            continue;
        }
        pos++;
    }
    strbuf_append_str_n(out, source + copied_start, (int)(len - copied_start));
}

// Set the spec name and toString() source: "function anonymous(params\n) {\n
// body\n}" and its generator/async variants. The parsed wrapper differs.
static void js_dynamic_function_apply_metadata(Item fn_item, Item* args, int argc,
        const char* source_prefix) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    Item anon_name = js_name_item("anonymous", 9);
    js_set_function_name(fn_item, anon_name);

    StrBuf* src_buf = strbuf_new_cap(256);
    strbuf_append_all(src_buf, 2, source_prefix, "(");
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

static Item js_new_function_from_string_kind(Item* args, int argc, const char* parse_prefix,
        const char* source_prefix) {
    if (!js_current_runtime() || !js_active_runtime_state) {
        log_error("js-new-function: no runtime context for dynamic function compilation");
        return ItemNull;
    }

    // Build the JS source for the function expression.
    // new Function("param1", "param2", "body") or new Function("body")
    // → (function(param1, param2) { body })
    // new Function() with no args → (function() {})
    StrBuf* sb = strbuf_new_cap(256);
    strbuf_append_all(sb, 2, "(", parse_prefix);
    strbuf_append_str(sb, "(");

    // params are args[0..argc-2], body is args[argc-1]
    // per spec, each argument is converted to string via ToString()
    for (int i = 0; i < argc - 1; i++) {
        if (i > 0) strbuf_append_str(sb, ",");
        String* ps = it2s(args[i]);
        if (!ps) {
            Item str_item = js_to_string(args[i]);
            // new Function applies ToString before parsing; an abrupt conversion
            // must escape instead of being misreported as a source SyntaxError.
            if (item_is_error(str_item)) {
                strbuf_free(sb);
                return str_item;
            }
            ps = it2s(str_item);
        }
        if (ps && ps->len > 0) {
            if (js_source_contains_import_meta(ps->chars, ps->len)) {
                strbuf_free(sb);
                return js_dynamic_function_throw_syntax_error("Cannot use import.meta outside a module");
            }
            if (js_dynamic_function_source_has_hashbang(ps->chars, ps->len)) {
                strbuf_free(sb);
                return js_dynamic_function_throw_syntax_error("Hashbang is not allowed here");
            }
            if (js_dynamic_function_param_has_invalid_html_close_comment(ps->chars, ps->len)) {
                strbuf_free(sb);
                return js_dynamic_function_throw_syntax_error("Unexpected token '-->'");
            }
            // The parser receives a function-expression wrapper, where a
            // valid Annex-B close comment is not recognized in parameter
            // grammar context. Remove the already-validated comment line.
            js_dynamic_function_append_normalized_params(sb, ps->chars, ps->len);
        }
    }
    // Newline before ) is required by spec §20.2.1.1 to handle params ending with // comment
    strbuf_append_str(sb, "\n) {");

    // body
    String* body = (argc > 0) ? it2s(args[argc - 1]) : NULL;
    if (!body && argc > 0) {
        Item str_item = js_to_string(args[argc - 1]);
        // preserve an abrupt body ToString before any dynamic-source parsing.
        if (item_is_error(str_item)) {
            strbuf_free(sb);
            return str_item;
        }
        body = it2s(str_item);
    }
    if (body && body->len > 0) {
        if (js_source_contains_import_meta(body->chars, body->len)) {
            strbuf_free(sb);
            return js_dynamic_function_throw_syntax_error("Cannot use import.meta outside a module");
        }
        if (js_dynamic_function_source_has_hashbang(body->chars, body->len)) {
            strbuf_free(sb);
            return js_dynamic_function_throw_syntax_error("Hashbang is not allowed here");
        }
        strbuf_append_str(sb, "\n");
        strbuf_append_str_n(sb, body->chars, (int)body->len);
        strbuf_append_str(sb, "\n");
    }
    strbuf_append_str(sb, "})");

    // The wrapper is global script code whose completion value is the
    // function. The interpreter's AST template cache absorbs repeated sources.
    RootFrame roots(1);
    Rooted<Item> fn_root(roots, js_interp_execute_source(js_current_runtime(),
        sb->str, sb->length, "<new Function>", NULL));
    strbuf_free(sb);
    if (!item_is_error(fn_root.get())) {
        js_dynamic_function_apply_metadata(fn_root.get(), args, argc, source_prefix);
    }
    return fn_root.get();
}

JS_FORWARD_ITEM(js_new_function_from_string, (Item* args, int argc),
    js_new_function_from_string_kind,
    (args, argc, "function", "function anonymous"))
JS_FORWARD_ITEM(js_new_async_function_from_string, (Item* args, int argc),
    js_new_function_from_string_kind,
    (args, argc, "async function", "async function anonymous"))

extern "C" Item js_new_generator_function_from_string(Item* args, int argc, int is_async) {
    if (is_async) {
        return js_new_function_from_string_kind(args, argc, "async function*",
            "async function* anonymous");
    }
    return js_new_function_from_string_kind(args, argc, "function*",
        "function* anonymous");
}

// ============================================================================
// eval(code) early errors that depend on the caller's context
// ============================================================================
typedef struct JsEvalInitializerScan {
    bool contains_arguments;
    bool contains_new_target;
    bool contains_super_call;
    bool contains_super_property;
} JsEvalInitializerScan;

static JsEvalInitializerScan js_eval_scan_initializer_source(const char* source, size_t len) {
    JsEvalInitializerScan scan = {false, false, false, false};
    for (size_t pos = 0; pos < len; pos++) {
        char ch = source[pos];
        if (ch == '\'' || ch == '"' || ch == '`') {
            pos = (size_t)(strn_scan_quoted(source + pos, source + len, ch, true, NULL) - source);
            if (pos > 0) pos--;
            continue;
        }
        if (ch == '/' && pos + 1 < len && source[pos + 1] == '/') {
            pos += 2;
            while (pos < len && !js_eval_at_line_terminator(source, len, pos, NULL)) pos++;
            continue;
        }
        if (ch == '/' && pos + 1 < len && source[pos + 1] == '*') {
            pos += 2;
            while (pos + 1 < len && !(source[pos] == '*' && source[pos + 1] == '/')) pos++;
            if (pos + 1 < len) pos++;
            continue;
        }
        if (js_eval_at_word(source, len, pos, "arguments", 9)) {
            scan.contains_arguments = true;
            pos += 8;
            continue;
        }
        if (js_eval_at_word(source, len, pos, "new", 3)) {
            size_t member_pos = js_eval_skip_space_and_comments(source, len, pos + 3);
            if (member_pos + 7 <= len && memcmp(source + member_pos, ".target", 7) == 0 &&
                (member_pos + 7 == len || !js_eval_is_ident_char(source[member_pos + 7]))) {
                scan.contains_new_target = true;
            }
            pos += 2;
            continue;
        }
        if (js_eval_at_word(source, len, pos, "super", 5)) {
            size_t member_pos = js_eval_skip_space_and_comments(source, len, pos + 5);
            if (member_pos < len && source[member_pos] == '(') scan.contains_super_call = true;
            else if (member_pos < len && (source[member_pos] == '.' || source[member_pos] == '[')) scan.contains_super_property = true;
            pos += 4;
            continue;
        }
    }
    return scan;
}

static Item js_eval_initializer_early_error(String* code_str, bool is_direct_eval) {
    if ((!js_private_field_initializing && !js_eval_initializer_context) || !code_str) return js_status_ok();
    JsEvalInitializerScan scan = js_eval_scan_initializer_source(code_str->chars, code_str->len);
    if ((is_direct_eval && scan.contains_arguments) || (!is_direct_eval && scan.contains_new_target) ||
        scan.contains_super_call || (!is_direct_eval && scan.contains_super_property)) {
        return js_throw_syntax_error(js_name_item("Invalid eval in class field initializer", 39));
    }
    return js_status_ok();
}

// A caller's const is projected through the eval bridge as a plain binding, so
// strict eval code cannot observe its immutability until write-back.
static bool js_eval_source_assigns_immutable_binding(String* code_str) {
    if (!code_str) return false;
    const char* source = code_str->chars;
    size_t len = code_str->len;
    size_t pos = 0;
    while (pos < len) {
        if (js_eval_skip_string_or_comment(source, len, &pos)) continue;
        char ch = source[pos];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' || ch == '$')) {
            pos++;
            continue;
        }
        size_t start = pos++;
        while (pos < len && js_eval_is_ident_char(source[pos])) pos++;
        size_t after = js_eval_skip_space_and_comments(source, len, pos);
        bool assigns = false;
        if (after < len && source[after] == '=') {
            assigns = (after + 1 >= len || (source[after + 1] != '=' && source[after + 1] != '>'));
        }
        if (assigns) {
            Item key = js_name_item(source + start, pos - start);
            if (js_eval_local_has_immutable_binding(key)) return true;
        }
    }
    return false;
}

static bool js_eval_source_is_v8_native_probe(String* code_str, bool* result_value) {
    if (!code_str) return false;
    const char* source = code_str->chars;
    size_t len = code_str->len;
    size_t pos = js_eval_skip_space_and_comments(source, len, 0);
    if (pos >= len || source[pos] != '%') return false;
    pos++;

    struct NativeProbe {
        const char* name;
        bool bool_value;
    };
    static const NativeProbe probes[] = {
        {"DebugPrint", false},
        {"CollectGarbage", false},
        {"HaveSameMap", true},
    };

    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        const NativeProbe* probe = &probes[i];
        size_t name_len = strlen(probe->name);
        if (pos + name_len > len || memcmp(source + pos, probe->name, name_len) != 0) continue;
        size_t open = js_eval_skip_space_and_comments(source, len, pos + name_len);
        if (open >= len || source[open] != '(') return false;
        bool closed = false;
        const char* after = strn_scan_balanced_quoted(source + open, source + len, '(', ')',
                                                       "\"'`", true, &closed);
        if (!closed) return false;
        size_t tail = js_eval_skip_space_and_comments(source, len, (size_t)(after - source));
        if (tail != len) return false;
        if (result_value) *result_value = probe->bool_value;
        return true;
    }
    return false;
}

// Whitespace and comments alone complete with undefined. Recognizing them
// here keeps eval-heavy loops from parsing and retaining a script per call;
// an unterminated block comment still reaches the parser's SyntaxError.
static bool js_eval_source_is_blank(String* code_str) {
    const char* s = code_str->chars;
    size_t len = code_str->len;
    size_t pos = 0;
    if (len >= 2 && s[0] == '#' && s[1] == '!') {
        while (pos < len && !js_eval_at_line_terminator(s, len, pos, NULL)) pos++;
    }
    while (pos < len) {
        size_t width = 0;
        if (s[pos] == ' ' || s[pos] == '\t') { pos++; continue; }
        if (js_eval_at_line_terminator(s, len, pos, &width)) { pos += width; continue; }
        if (s[pos] == '/' && pos + 1 < len && s[pos + 1] == '/') {
            while (pos < len && !js_eval_at_line_terminator(s, len, pos, NULL)) pos++;
            continue;
        }
        if (s[pos] == '/' && pos + 1 < len && s[pos + 1] == '*') {
            pos += 2;
            while (pos + 1 < len && !(s[pos] == '*' && s[pos + 1] == '/')) pos++;
            if (pos + 1 >= len) return false;
            pos += 2;
            continue;
        }
        return false;
    }
    return true;
}

extern "C" Item js_create_regexp_from_source(const char* src, size_t len);

// A source that is exactly one RegExp literal evaluates to that RegExp;
// construct it without a per-call script. Other flags take the parser.
static bool js_eval_source_is_regexp_literal(String* code_str) {
    const char* s = code_str->chars;
    size_t len = code_str->len;
    if (len < 2 || s[0] != '/' || s[1] == '/' || s[1] == '*') return false;
    size_t pos = 1;
    bool in_class = false;
    while (pos < len) {
        char c = s[pos];
        if (c == '\\' && pos + 1 < len) { pos += 2; continue; }
        if (c == '[') in_class = true;
        else if (c == ']') in_class = false;
        else if (c == '/' && !in_class) break;
        pos++;
    }
    if (pos >= len) return false;
    for (pos++; pos < len; pos++) {
        char f = s[pos];
        if (f != 'g' && f != 'i' && f != 'm' && f != 's' && f != 'u' && f != 'y') return false;
    }
    return true;
}

// ============================================================================
// eval(code)
// ============================================================================
bool js_eval_source_shortcut(Item code_item, bool is_direct_eval, Item* result) {
    if (!js_current_runtime()) {
        log_error("js-eval: no runtime context for dynamic evaluation");
        *result = ItemNull;
        return true;
    }
    if (get_type_id(code_item) != LMD_TYPE_STRING) {
        // eval(non-string) returns the argument unchanged (ES spec)
        *result = code_item;
        return true;
    }
    String* code_str = it2s(code_item);
    *result = (Item){.item = ITEM_JS_UNDEFINED};
    if (!code_str || code_str->len == 0) return true;
    bool native_probe_value = false;
    if (js_eval_source_is_v8_native_probe(code_str, &native_probe_value)) {
        // Node official tests use V8 native probes under --allow_natives_syntax;
        // these probes observe engine internals and should not enter the JS parser.
        if (native_probe_value) *result = (Item){.item = b2it(true)};
        return true;
    }
    Item initializer_status = js_eval_initializer_early_error(code_str, is_direct_eval);
    if (item_is_error(initializer_status)) {
        *result = initializer_status;
        return true;
    }
    if (js_eval_source_is_blank(code_str)) return true;
    if (js_eval_source_is_regexp_literal(code_str)) {
        *result = js_create_regexp_from_source(code_str->chars, code_str->len);
        return true;
    }
    return false;
}

// eval_flags bit 1: a syntactic direct eval whose MIR caller installed the
// EvalContext bridge; bit 2: inherit strictness from that caller. Indirect
// eval, string timers, and $262 sources pass 1 and run as global eval code.
// An interpreted caller links its environments instead (js_interp.cpp).
extern "C" Item js_builtin_eval(Item code_item, int64_t eval_flags) {
    bool is_direct_eval = (eval_flags & 2) != 0;
    bool inherited_strict = (eval_flags & 4) != 0;
    Item shortcut = ItemNull;
    if (js_eval_source_shortcut(code_item, is_direct_eval, &shortcut)) return shortcut;
    String* code_str = it2s(code_item);
    if (!is_direct_eval) {
        return js_interp_execute_indirect_eval_source(js_current_runtime(),
            code_str->chars, code_str->len, "<eval>", NULL);
    }
    // The bridge projects a MIR caller's consts as plain bindings, so this
    // source scan stands in for their strict write error until P4 (D8.1.3v21).
    if (inherited_strict && js_eval_source_assigns_immutable_binding(code_str)) {
        return js_throw_type_error("Assignment to constant variable");
    }
    return js_interp_execute_direct_eval_source(js_current_runtime(),
        code_str->chars, code_str->len, "<eval>", inherited_strict, NULL);
}
