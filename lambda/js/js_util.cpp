/**
 * js_util.cpp — Node.js-style 'util' module for LambdaJS
 *
 * Provides utility functions: format, inspect, types, promisify.
 * Registered as built-in module 'util' via js_module_get().
 */
#include "js_runtime.h"
#include "js_runtime_state.hpp"
#include "js_host_hooks.h"
#include "js_typed_array.h"
#include "js_class.h"
#include "js_function.hpp"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../runtime/transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/file.h"
#include "../../lib/mem.h"
#include "../../lib/strbuf.h"

#include <cstring>
#include <cmath>
#include <cstdio>
#include <cerrno>
#include <time.h>
#include <limits.h>
#ifndef _WIN32
#include <unistd.h>
#endif

// forward declarations
extern "C" Item js_util_inspect(Item obj_item, Item options_item);
extern "C" Item js_process_emit(Item event_name, Item arg1);
extern "C" Item js_buffer_isBuffer(Item obj);
extern "C" bool js_props_obj_query_enumerable(Item obj, const char* name, int name_len);
extern __thread EvalContext* context;

#define JS_UTIL_FUNC_FLAG_GENERATOR 1
#define JS_UTIL_FUNC_FLAG_ASYNC 128

static bool js_util_set_real_filename(const char* path, Item* filename) {
    if (!path || !filename) return false;
    // POSIX realpath is unavailable on Windows; the shared file layer preserves
    // the same absolute-path behavior with the native platform API.
    char* resolved = file_realpath(path);
    if (!resolved) return false;
    *filename = make_string_item(resolved);
    mem_free(resolved);
    return true;
}

// helper: append a string Item without repeating the bounded-copy logic
static void js_util_append_string(char* buf, int* pos, int capacity, Item value) {
    if (get_type_id(value) != LMD_TYPE_STRING) return;
    String* str = it2s(value);
    if (!str) return;
    int count = (int)str->len;
    if (*pos + count >= capacity) count = capacity - 1 - *pos;
    if (count > 0) {
        memcpy(buf + *pos, str->chars, count);
        *pos += count;
    }
}

// Helper: make JS undefined
// =============================================================================
// util.format(...args) — simplified printf-style formatting
// =============================================================================

extern "C" Item js_util_format(Item args_item) {
    if (get_type_id(args_item) != LMD_TYPE_ARRAY) return make_string_item("");

    int argc = (int)js_array_length(args_item);
    if (argc == 0) return make_string_item("");

    // first arg is the format string (or the only value)
    Item first = js_elements_get_int(args_item, 0);
    if (get_type_id(first) != LMD_TYPE_STRING) {
        // first arg is not a string — inspect all args and join with space
        char buf[8192];
        int pos = 0;
        for (int i = 0; i < argc; i++) {
            if (i > 0 && pos < (int)sizeof(buf) - 1) buf[pos++] = ' ';
            Item val = js_elements_get_int(args_item, i);
            TypeId vt = get_type_id(val);
            // special case: Symbol (inspect would fail — use Symbol.toString)
            if (vt == LMD_TYPE_INT && it2i(val) <= -(int64_t)JS_SYMBOL_BASE) {
                Item sym_str = js_symbol_to_string(val);
                if (get_type_id(sym_str) == LMD_TYPE_STRING) {
                    String* s = it2s(sym_str);
                    int clen = (int)s->len;
                    if (pos + clen >= (int)sizeof(buf)) clen = (int)sizeof(buf) - 1 - pos;
                    memcpy(buf + pos, s->chars, clen);
                    pos += clen;
                }
                continue;
            }
            Item str = js_util_inspect(val, make_js_undefined());
            js_util_append_string(buf, &pos, sizeof(buf), str);
        }
        buf[pos] = '\0';
        return make_string_item(buf, pos);
    }

    String* fmt = it2s(first);
    char buf[8192];
    int pos = 0;
    int arg_idx = 1;

    for (int i = 0; i < (int)fmt->len && pos < (int)sizeof(buf) - 1; i++) {
        if (fmt->chars[i] == '%' && i + 1 < (int)fmt->len) {
            char spec = fmt->chars[i + 1];

            // %% is always handled regardless of remaining args
            if (spec == '%') {
                buf[pos++] = '%';
                i++; // skip second %
                continue;
            }

            // other specifiers require a remaining arg
            if (arg_idx >= argc) {
                buf[pos++] = fmt->chars[i];
                continue;
            }

            Item arg = js_elements_get_int(args_item, arg_idx);
            i++; // skip past specifier

            switch (spec) {
                case 's': {
                    // Symbol needs special handling (js_to_string throws for Symbol)
                    TypeId at = get_type_id(arg);
                    if (at == LMD_TYPE_INT && it2i(arg) <= -(int64_t)JS_SYMBOL_BASE) {
                        Item sym_str = js_symbol_to_string(arg);
                        js_util_append_string(buf, &pos, sizeof(buf), sym_str);
                    } else {
                        Item str = js_to_string(arg);
                        js_util_append_string(buf, &pos, sizeof(buf), str);
                    }
                    arg_idx++;
                    break;
                }
                case 'd':
                case 'i': {
                    // Symbol cannot convert to number
                    TypeId at = get_type_id(arg);
                    if (at == LMD_TYPE_INT && it2i(arg) <= -(int64_t)JS_SYMBOL_BASE) {
                        pos += snprintf(buf + pos, sizeof(buf) - pos, "NaN");
                        arg_idx++;
                        break;
                    }
                    Item num = js_to_number(arg);
                    TypeId t = get_type_id(num);
                    double val = 0;
                    bool is_nan = false;
                    bool is_int = false;
                    if (t == LMD_TYPE_INT) {
                        val = (double)it2i(num);
                        is_int = true;
                    } else if (t == LMD_TYPE_FLOAT) {
                        val = it2d(num);
                        if (val != val) is_nan = true; // NaN check
                    } else {
                        is_nan = true;
                    }

                    // %i truncates to integer (like parseInt)
                    if (spec == 'i') {
                        // parseInt semantics: NaN, Infinity, -Infinity → NaN
                        if (is_nan || val == 1.0/0.0 || val == -1.0/0.0) {
                            is_nan = true;
                        } else if (val == val) {
                            val = (val >= 0) ? floor(val) : ceil(val);
                            is_int = true;
                        }
                        // %i with string arg: parseInt('') = NaN, parseInt('  ') = NaN
                        if (!is_nan && get_type_id(arg) == LMD_TYPE_STRING) {
                            String* as = it2s(arg);
                            if (as) {
                                // Check if string is empty or whitespace-only
                                bool blank = true;
                                for (int k = 0; k < (int)as->len; k++) {
                                    if (as->chars[k] != ' ' && as->chars[k] != '\t' &&
                                        as->chars[k] != '\n' && as->chars[k] != '\r') {
                                        blank = false; break;
                                    }
                                }
                                if (blank) is_nan = true;
                            }
                        }
                    }

                    if (is_nan) {
                        pos += snprintf(buf + pos, sizeof(buf) - pos, "NaN");
                    } else if (val == 1.0/0.0) {
                        pos += snprintf(buf + pos, sizeof(buf) - pos, "Infinity");
                    } else if (val == -1.0/0.0) {
                        pos += snprintf(buf + pos, sizeof(buf) - pos, "-Infinity");
                    } else if (val == 0.0 && (1.0 / val) < 0) {
                        // negative zero
                        if (spec == 'i') {
                            pos += snprintf(buf + pos, sizeof(buf) - pos, "0"); // parseInt(-0) = 0
                        } else {
                            pos += snprintf(buf + pos, sizeof(buf) - pos, "-0");
                        }
                    } else if (is_int || val == floor(val)) {
                        pos += snprintf(buf + pos, sizeof(buf) - pos, "%lld", (long long)val);
                    } else {
                        // float with fractional part: use %g-style formatting
                        char tmp[64];
                        snprintf(tmp, sizeof(tmp), "%.17g", val);
                        // trim trailing zeros after decimal point but keep at least one digit
                        char* dot = strchr(tmp, '.');
                        if (dot) {
                            char* end = tmp + strlen(tmp) - 1;
                            while (end > dot + 1 && *end == '0') end--;
                            *(end + 1) = '\0';
                        }
                        int n = (int)strlen(tmp);
                        if (pos + n < (int)sizeof(buf)) { memcpy(buf + pos, tmp, n); pos += n; }
                    }
                    arg_idx++;
                    break;
                }
                case 'f': {
                    // Symbol cannot convert to number
                    TypeId fat = get_type_id(arg);
                    if (fat == LMD_TYPE_INT && it2i(arg) <= -(int64_t)JS_SYMBOL_BASE) {
                        pos += snprintf(buf + pos, sizeof(buf) - pos, "NaN");
                        arg_idx++;
                        break;
                    }
                    Item num = js_to_number(arg);
                    TypeId t = get_type_id(num);
                    double val = 0;
                    if (t == LMD_TYPE_INT) val = (double)it2i(num);
                    else if (t == LMD_TYPE_FLOAT) val = it2d(num);
                    pos += snprintf(buf + pos, sizeof(buf) - pos, "%f", val);
                    arg_idx++;
                    break;
                }
                case 'j': {
                    Item str = js_json_stringify(arg);
                    if (get_type_id(str) == LMD_TYPE_STRING) {
                        js_util_append_string(buf, &pos, sizeof(buf), str);
                    } else {
                        // JSON.stringify returns undefined for Symbol, functions, etc.
                        const char* undef = "undefined";
                        int ulen = 9;
                        if (pos + ulen < (int)sizeof(buf)) { memcpy(buf + pos, undef, ulen); pos += ulen; }
                    }
                    arg_idx++;
                    break;
                }
                case 'o':
                case 'O': {
                    Item str = js_json_stringify(arg);
                    js_util_append_string(buf, &pos, sizeof(buf), str);
                    arg_idx++;
                    break;
                }
                default: {
                    // unknown specifier — output literally
                    buf[pos++] = '%';
                    if (pos < (int)sizeof(buf) - 1) buf[pos++] = spec;
                    break;
                }
            }
        } else {
            buf[pos++] = fmt->chars[i];
        }
    }

    // append remaining args (Node.js: strings raw, others inspected)
    while (arg_idx < argc && pos < (int)sizeof(buf) - 2) {
        buf[pos++] = ' ';
        Item extra = js_elements_get_int(args_item, arg_idx++);
        TypeId et = get_type_id(extra);
        Item str;
        if (et == LMD_TYPE_INT && it2i(extra) <= -(int64_t)JS_SYMBOL_BASE) {
            str = js_symbol_to_string(extra);
        } else if (et == LMD_TYPE_STRING) {
            str = extra; // strings appended raw, not inspected
        } else {
            str = js_util_inspect(extra, make_js_undefined());
        }
        js_util_append_string(buf, &pos, sizeof(buf), str);
    }

    buf[pos] = '\0';
    return make_string_item(buf, pos);
}

// =============================================================================
// util.inspect(obj[, options]) — Node.js-style formatting for diagnostics
// =============================================================================


struct JsInspectContext {
    bool show_hidden;
    bool colors;
    int depth;
    Item seen;
};

static Item js_util_inspect_value(Item obj_item, JsInspectContext* ctx, int depth_left);
static void js_util_inspect_append_escaped_char(StrBuf* sb, char ch);

static Item js_util_inspect_make_string(StrBuf* sb) {
    Item result = make_string_item(sb->str ? sb->str : "", (int)sb->length);
    strbuf_free(sb);
    return result;
}

static bool js_util_inspect_option_bool(Item options, const char* name, bool fallback) {
    if (get_type_id(options) != LMD_TYPE_MAP) return fallback;
    Item value = js_get_key_default(options, make_string_item(name));
    if (get_type_id(value) == LMD_TYPE_BOOL) return it2b(value);
    return fallback;
}

static int js_util_inspect_option_depth(Item options) {
    if (get_type_id(options) != LMD_TYPE_MAP) return 2;
    Item value = js_get_key_cstr(options, "depth");
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_NULL) return 64;
    if (type == LMD_TYPE_INT) {
        int64_t n = it2i(value);
        if (n < 0) return -1;
        if (n > 64) return 64;
        return (int)n;
    }
    if (type == LMD_TYPE_FLOAT) {
        double d = it2d(value);
        if (d < 0) return -1;
        if (d > 64) return 64;
        return (int)d;
    }
    return 2;
}

static void js_util_inspect_append_styled(StrBuf* sb, JsInspectContext* ctx,
                                          const char* style, const char* text) {
    if (ctx && ctx->colors) {
        if (strcmp(style, "string") == 0) strbuf_append_str(sb, "\x1b[32m");
        else if (strcmp(style, "number") == 0 || strcmp(style, "boolean") == 0) strbuf_append_str(sb, "\x1b[33m");
        else if (strcmp(style, "special") == 0) strbuf_append_str(sb, "\x1b[90m");
    }
    strbuf_append_str(sb, text);
    if (ctx && ctx->colors) {
        if (strcmp(style, "special") == 0) strbuf_append_str(sb, "\x1b[39m");
        else strbuf_append_str(sb, "\x1b[39m");
    }
}

static bool js_util_inspect_is_object_like(Item value) {
    TypeId type = get_type_id(value);
    return type == LMD_TYPE_MAP || type == LMD_TYPE_ARRAY || type == LMD_TYPE_FUNC ||
           type == LMD_TYPE_OBJECT || type == LMD_TYPE_ELEMENT || type == LMD_TYPE_VMAP;
}

#define js_util_inspect_is_undefined(value) (get_type_id(value) == LMD_TYPE_UNDEFINED)

#define js_util_inspect_string_equals js_string_equals

static bool js_util_inspect_is_assertion_error(Item value) {
    if (!js_util_inspect_is_object_like(value)) return false;
    Item code = js_get_key_cstr(value, "code");
    return js_util_inspect_string_equals(code, "ERR_ASSERTION");
}

static bool js_util_inspect_seen_contains(Item seen, Item value) {
    int64_t len = js_array_length(seen);
    for (int64_t i = 0; i < len; i++) {
        if (js_elements_get_int(seen, i).item == value.item) return true;
    }
    return false;
}

static void js_util_inspect_seen_pop(Item seen) {
    int64_t len = js_array_length(seen);
    if (len <= 0) return;
    js_set_key_cstr(seen, "length", (Item){.item = i2it(len - 1)});
}

static Item js_util_inspect_string(Item obj_item, JsInspectContext* ctx) {
    String* s = it2s(obj_item);
    StrBuf* sb = strbuf_new();
    strbuf_append_char(sb, '\'');
    if (s && s->len > 0) strbuf_append_str_n(sb, s->chars, s->len);
    strbuf_append_char(sb, '\'');
    Item raw = js_util_inspect_make_string(sb);
    if (!ctx || !ctx->colors) return raw;

    StrBuf* styled = strbuf_new();
    String* rs = it2s(raw);
    strbuf_append_str(styled, "\x1b[32m");
    if (rs) strbuf_append_str_n(styled, rs->chars, rs->len);
    strbuf_append_str(styled, "\x1b[39m");
    return js_util_inspect_make_string(styled);
}

static Item js_util_inspect_number(Item obj_item, JsInspectContext* ctx) {
    TypeId type = get_type_id(obj_item);
    char buf[96];
    if (type == LMD_TYPE_INT) {
        snprintf(buf, sizeof(buf), "%lld", (long long)it2i(obj_item));
    } else {
        double d = it2d(obj_item);
        if (d != d) snprintf(buf, sizeof(buf), "NaN");
        else if (d == 1.0/0.0) snprintf(buf, sizeof(buf), "Infinity");
        else if (d == -1.0/0.0) snprintf(buf, sizeof(buf), "-Infinity");
        else snprintf(buf, sizeof(buf), "%.17g", d);
    }
    if (!ctx || !ctx->colors) return make_string_item(buf);
    StrBuf* sb = strbuf_new();
    js_util_inspect_append_styled(sb, ctx, "number", buf);
    return js_util_inspect_make_string(sb);
}

static Item js_util_inspect_bigint(Item obj_item, JsInspectContext* ctx) {
    Item digits = js_to_string(obj_item);
    if (item_is_error(digits) || get_type_id(digits) != LMD_TYPE_STRING) return make_string_item("0n");
    String* ds = it2s(digits);
    StrBuf* raw = strbuf_new();
    if (ds) strbuf_append_str_n(raw, ds->chars, ds->len);
    strbuf_append_char(raw, 'n');
    Item text = js_util_inspect_make_string(raw);
    if (!ctx || !ctx->colors) return text;
    StrBuf* sb = strbuf_new();
    String* ts = it2s(text);
    strbuf_append_str(sb, "\x1b[33m");
    if (ts) strbuf_append_str_n(sb, ts->chars, ts->len);
    strbuf_append_str(sb, "\x1b[39m");
    return js_util_inspect_make_string(sb);
}

static void js_util_inspect_append_escaped_char(StrBuf* sb, char ch) {
    if (ch == '\\') strbuf_append_str(sb, "\\\\");
    else if (ch == '\'') strbuf_append_str(sb, "\\'");
    else if (ch == '\n') strbuf_append_str(sb, "\\n");
    else if (ch == '\r') strbuf_append_str(sb, "\\r");
    else if (ch == '\t') strbuf_append_str(sb, "\\t");
    else strbuf_append_char(sb, ch);
}

static void js_util_inspect_append_assertion_string(StrBuf* sb, Item value, size_t long_limit) {
    String* s = get_type_id(value) == LMD_TYPE_STRING ? it2s(value) : NULL;
    if (s && s->len > 0) {
        int newline_count = 0;
        for (size_t i = 0; i < s->len; i++) {
            if (s->chars[i] == '\n') {
                newline_count++;
                if (newline_count == 10) {
                    if (i + 1 <= 32) {
                        // Very short repeated lines stay readable as one escaped
                        // literal in Node's AssertionError inspect output.
                        strbuf_append_char(sb, '\'');
                        for (size_t k = 0; k <= i; k++) {
                            js_util_inspect_append_escaped_char(sb, s->chars[k]);
                        }
                        strbuf_append_str(sb, "...'");
                        return;
                    }
                    // AssertionError inspect prints long multiline strings as
                    // concatenated quoted chunks; one giant literal misses
                    // Node's public diagnostic shape.
                    size_t start = 0;
                    int line = 0;
                    for (size_t j = 0; j <= i; j++) {
                        if (s->chars[j] != '\n') continue;
                        if (line > 0) strbuf_append_str(sb, "    ");
                        strbuf_append_char(sb, '\'');
                        for (size_t k = start; k <= j; k++) {
                            js_util_inspect_append_escaped_char(sb, s->chars[k]);
                        }
                        strbuf_append_str(sb, "' +\n");
                        start = j + 1;
                        line++;
                    }
                    strbuf_append_str(sb, "    '...'");
                    return;
                }
            }
        }
    }

    strbuf_append_char(sb, '\'');
    if (s && s->len > 0) {
        size_t limit = s->len;
        bool append_ellipsis = false;
        if (!append_ellipsis && s->len > long_limit) {
            // AssertionError inspect intentionally shows a short excerpt of
            // actual/expected string slots; Assert instances with diff options
            // use Node's longer public excerpt while plain assert stays compact.
            limit = long_limit;
            append_ellipsis = true;
        }
        for (size_t i = 0; i < limit; i++) {
            js_util_inspect_append_escaped_char(sb, s->chars[i]);
        }
        if (append_ellipsis) strbuf_append_str(sb, "...");
    }
    strbuf_append_char(sb, '\'');
}

static bool js_util_inspect_key_is_array_index(String* key, int64_t len) {
    if (!key || key->len == 0) return false;
    int64_t index = 0;
    for (size_t i = 0; i < key->len; i++) {
        char c = key->chars[i];
        if (c < '0' || c > '9') return false;
        index = index * 10 + (c - '0');
        if (index >= len) return false;
    }
    return true;
}

static void js_util_inspect_append_key(StrBuf* sb, String* key, bool hidden) {
    if (hidden) strbuf_append_char(sb, '[');
    if (key && key->len > 0) strbuf_append_str_n(sb, key->chars, key->len);
    if (hidden) strbuf_append_char(sb, ']');
}

static void js_util_inspect_append_quoted_key(StrBuf* sb, String* key, bool hidden) {
    if (hidden) strbuf_append_char(sb, '[');
    if (!key) {
        if (hidden) strbuf_append_char(sb, ']');
        return;
    }
    bool identifier = key->len > 0 &&
        ((key->chars[0] >= 'A' && key->chars[0] <= 'Z') ||
         (key->chars[0] >= 'a' && key->chars[0] <= 'z') ||
         key->chars[0] == '_' || key->chars[0] == '$');
    for (size_t i = 1; identifier && i < key->len; i++) {
        char ch = key->chars[i];
        identifier = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '_' || ch == '$';
    }
    if (identifier) {
        strbuf_append_str_n(sb, key->chars, key->len);
    } else {
        strbuf_append_char(sb, '\'');
        for (size_t i = 0; i < key->len; i++) {
            js_util_inspect_append_escaped_char(sb, key->chars[i]);
        }
        strbuf_append_char(sb, '\'');
    }
    if (hidden) strbuf_append_char(sb, ']');
}

static void js_util_inspect_append_property(StrBuf* sb, Item owner, Item key,
                                            JsInspectContext* ctx, int depth_left,
                                            bool* first) {
    if (get_type_id(key) != LMD_TYPE_STRING) return;
    String* ks = it2s(key);
    if (ks && ks->len == 20 && memcmp(ks->chars, "__strict_arguments__", 20) == 0) {
        // Strict Arguments stores this engine marker in the companion map; Node
        // inspect/deep diagnostics must not expose it as a user property.
        return;
    }
    bool enumerable = true;
    if (ks) enumerable = js_props_obj_query_enumerable(owner, ks->chars, (int)ks->len);
    bool hidden = ctx && ctx->show_hidden && !enumerable;

    Item value = js_get_key_default(owner, key);
    Item value_str = js_util_inspect_value(value, ctx, depth_left - 1);
    String* vs = it2s(value_str);
    if (!*first) strbuf_append_str(sb, ", ");
    *first = false;
    js_util_inspect_append_key(sb, ks, hidden);
    strbuf_append_str(sb, ": ");
    if (vs) strbuf_append_str_n(sb, vs->chars, vs->len);
}

static void js_util_inspect_append_named_value(StrBuf* sb, const char* name, Item value,
                                               JsInspectContext* ctx, int depth_left,
                                               bool* first, bool assertion_string,
                                               size_t assertion_string_limit = 488) {
    if (js_util_inspect_is_undefined(value)) return;
    if (!*first) strbuf_append_str(sb, ", ");
    *first = false;
    strbuf_append_str(sb, name);
    strbuf_append_str(sb, ": ");
    if (assertion_string && get_type_id(value) == LMD_TYPE_STRING) {
        js_util_inspect_append_assertion_string(sb, value, assertion_string_limit);
        return;
    }
    if (assertion_string && get_type_id(value) == LMD_TYPE_ARRAY &&
            js_array_length(value) > 50) {
        // AssertionError's property suffix is a compact summary; expanding
        // large actual/expected arrays hides the useful constructor tag.
        strbuf_append_str(sb, "[Array]");
        return;
    }
    Item value_str = js_util_inspect_value(value, ctx, depth_left - 1);
    String* vs = it2s(value_str);
    if (vs) strbuf_append_str_n(sb, vs->chars, vs->len);
}

static Item js_util_inspect_assertion_error(Item obj_item, JsInspectContext* ctx, int depth_left) {
    Item message = js_get_key_cstr(obj_item, "message");
    Item code = js_get_key_cstr(obj_item, "code");
    Item diff = js_get_key_cstr(obj_item, "diff");
    Item instance_error = js_get_key_cstr(obj_item, "__assert_instance_error__");
    String* ms = get_type_id(message) == LMD_TYPE_STRING ? it2s(message) : NULL;
    String* cs = get_type_id(code) == LMD_TYPE_STRING ? it2s(code) : NULL;
    size_t assertion_string_limit =
        (get_type_id(instance_error) == LMD_TYPE_BOOL && it2b(instance_error)) ? 9488 : 488;

    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "AssertionError");
    if (cs && cs->len > 0) {
        strbuf_append_str(sb, " [");
        strbuf_append_str_n(sb, cs->chars, cs->len);
        strbuf_append_char(sb, ']');
    }
    if (ms && ms->len > 0) {
        strbuf_append_str(sb, ": ");
        strbuf_append_str_n(sb, ms->chars, ms->len);
    }
    strbuf_append_str(sb, " { ");

    bool first = true;
    js_util_inspect_append_named_value(sb, "generatedMessage",
        js_get_key_cstr(obj_item, "generatedMessage"),
        ctx, depth_left, &first, false);
    js_util_inspect_append_named_value(sb, "code", code, ctx, depth_left, &first, false);
    Item actual = js_get_key_cstr(obj_item, "actual");
    Item expected = js_get_key_cstr(obj_item, "expected");
    bool multiline_expected = get_type_id(actual) == LMD_TYPE_ARRAY &&
        js_array_length(actual) > 50 && !js_util_inspect_is_undefined(expected);
    js_util_inspect_append_named_value(sb, "actual", actual,
        ctx, depth_left, &first, true, assertion_string_limit);
    if (multiline_expected) {
        // Large AssertionError arrays keep the expected slot on the next line;
        // otherwise Node's compact `[Array]` summary is not discoverable.
        strbuf_append_str(sb, ",\n  expected: ");
        if (get_type_id(expected) == LMD_TYPE_STRING) {
            js_util_inspect_append_assertion_string(sb, expected, assertion_string_limit);
        } else {
            Item value_str = js_util_inspect_value(expected, ctx, depth_left - 1);
            String* vs = it2s(value_str);
            if (vs) strbuf_append_str_n(sb, vs->chars, vs->len);
        }
    } else {
        js_util_inspect_append_named_value(sb, "expected", expected,
            ctx, depth_left, &first, true, assertion_string_limit);
    }
    js_util_inspect_append_named_value(sb, "operator",
        js_get_key_cstr(obj_item, "operator"),
        ctx, depth_left, &first, false);
    js_util_inspect_append_named_value(sb, "diff", diff,
        ctx, depth_left, &first, false);

    strbuf_append_str(sb, " }");
    return js_util_inspect_make_string(sb);
}

static Item js_util_inspect_abort_signal(Item obj_item) {
    Item aborted = js_get_key_cstr(obj_item, "aborted");
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "AbortSignal { aborted: ");
    strbuf_append_str(sb, (get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted)) ? "true" : "false");
    strbuf_append_str(sb, " }");
    return js_util_inspect_make_string(sb);
}

static Item js_util_inspect_abort_controller(Item obj_item, JsInspectContext* ctx, int depth_left) {
    Item signal = js_get_key_cstr(obj_item, "signal");
    // util.inspect depth is consumed by the containing controller field, so a
    // depth-1 controller must render its signal as Node's typed placeholder.
    Item signal_text = depth_left <= 1 ? make_string_item("[AbortSignal]")
        : js_util_inspect_value(signal, ctx, depth_left - 1);
    String* st = get_type_id(signal_text) == LMD_TYPE_STRING ? it2s(signal_text) : NULL;
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "AbortController { signal: ");
    if (st) strbuf_append_str_n(sb, st->chars, st->len);
    else strbuf_append_str(sb, "[AbortSignal]");
    strbuf_append_str(sb, " }");
    return js_util_inspect_make_string(sb);
}

JS_FORWARD_STATIC_EXPRESSION(bool, js_util_is_arguments_exotic, (Item value), (js_is_arguments_exotic_array(value)))

static bool js_util_is_arguments_deep_value(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_MAP) return js_class_id(value) == JS_CLASS_ARGUMENTS;
    if (type == LMD_TYPE_ARRAY) return js_util_is_arguments_exotic(value);
    return false;
}

static Item js_util_inspect_date(Item obj_item, JsInspectContext* ctx, int depth_left) {
    StrBuf* sb = strbuf_new();
    char ctor_name[64];
    if (js_get_constructor_name(obj_item, ctor_name, sizeof(ctor_name)) &&
            strcmp(ctor_name, "Date") != 0) {
        strbuf_append_str(sb, ctor_name);
        strbuf_append_char(sb, ' ');
    }
    bool found_time = false;
    Item time_value = js_map_shape_lookup_ext(obj_item.map, "__time__", 8, &found_time);
    bool valid = found_time &&
        (get_type_id(time_value) == LMD_TYPE_INT || get_type_id(time_value) == LMD_TYPE_FLOAT);
    if (valid) {
        double millis = get_type_id(time_value) == LMD_TYPE_INT ? (double)it2i(time_value) : it2d(time_value);
        if (millis != millis) valid = false;
        if (valid) {
            time_t seconds = (time_t)(millis / 1000.0);
            int ms = (int)((int64_t)millis % 1000);
            if (ms < 0) ms = -ms;
            struct tm tm_value;
            if (gmtime_r(&seconds, &tm_value)) {
                char buf[40];
                snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                    tm_value.tm_year + 1900, tm_value.tm_mon + 1, tm_value.tm_mday,
                    tm_value.tm_hour, tm_value.tm_min, tm_value.tm_sec, ms);
                strbuf_append_str(sb, buf);
            } else {
                valid = false;
            }
        }
    }
    if (!valid) strbuf_append_str(sb, "Invalid Date");

    Item keys = (ctx && ctx->show_hidden) ? js_object_get_own_property_names(obj_item) : js_object_keys(obj_item);
    int64_t klen = js_array_length(keys);
    if (klen > 0) {
        int64_t emitted = 0;
        for (int64_t i = 0; i < klen; i++) {
            Item key = js_elements_get_int(keys, i);
            if (get_type_id(key) != LMD_TYPE_STRING) continue;
            String* ks = it2s(key);
            if (ks && ks->len == 8 && memcmp(ks->chars, "__time__", 8) == 0) continue;
            bool enumerable = ks ? js_props_obj_query_enumerable(obj_item, ks->chars, (int)ks->len) : true;
            bool hidden = ctx && ctx->show_hidden && !enumerable;
            Item value = js_get_key_default(obj_item, key);
            Item text = js_util_inspect_value(value, ctx, depth_left - 1);
            String* ts = get_type_id(text) == LMD_TYPE_STRING ? it2s(text) : NULL;
            if (emitted == 0) strbuf_append_str(sb, " { ");
            else strbuf_append_str(sb, ", ");
            js_util_inspect_append_quoted_key(sb, ks, hidden);
            strbuf_append_str(sb, ": ");
            if (ts) strbuf_append_str_n(sb, ts->chars, ts->len);
            emitted++;
        }
        if (emitted > 0) strbuf_append_str(sb, " }");
    }
    return js_util_inspect_make_string(sb);
}

static Item js_util_inspect_arguments(Item obj_item, JsInspectContext* ctx, int depth_left) {
    Item keys = (ctx && ctx->show_hidden) ? js_object_get_own_property_names(obj_item) : js_object_keys(obj_item);
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "[Arguments] {");
    int64_t klen = js_array_length(keys);
    bool first = true;
    for (int64_t i = 0; i < klen; i++) {
        Item key = js_elements_get_int(keys, i);
        if (get_type_id(key) != LMD_TYPE_STRING) continue;
        String* ks = it2s(key);
        if (ks && ks->len == 20 && memcmp(ks->chars, "__strict_arguments__", 20) == 0) {
            // Strict Arguments uses this internal bit for callee semantics; it
            // is not part of the public Arguments inspect shape.
            continue;
        }
        Item value = js_get_key_default(obj_item, key);
        Item text = js_util_inspect_value(value, ctx, depth_left - 1);
        String* ts = get_type_id(text) == LMD_TYPE_STRING ? it2s(text) : NULL;
        strbuf_append_str(sb, first ? " " : ", ");
        first = false;
        js_util_inspect_append_quoted_key(sb, ks, false);
        strbuf_append_str(sb, ": ");
        if (ts) strbuf_append_str_n(sb, ts->chars, ts->len);
    }
    if (!first) strbuf_append_char(sb, ' ');
    strbuf_append_char(sb, '}');
    return js_util_inspect_make_string(sb);
}

static Item js_util_inspect_regexp(Item obj_item, JsInspectContext* ctx, int depth_left) {
    Item text = js_to_string_val(obj_item);
    String* rs = get_type_id(text) == LMD_TYPE_STRING ? it2s(text) : NULL;
    StrBuf* sb = strbuf_new();
    char ctor_name[64];
    if (js_get_constructor_name(obj_item, ctor_name, sizeof(ctor_name)) &&
            strcmp(ctor_name, "RegExp") != 0) {
        strbuf_append_str(sb, ctor_name);
        strbuf_append_char(sb, ' ');
    }
    if (rs) strbuf_append_str_n(sb, rs->chars, rs->len);
    else strbuf_append_str(sb, "/(?:)/");

    Item keys = (ctx && ctx->show_hidden) ? js_object_get_own_property_names(obj_item) : js_object_keys(obj_item);
    int64_t klen = js_array_length(keys);
    if (klen > 0) {
        strbuf_append_str(sb, " {");
        for (int64_t i = 0; i < klen; i++) {
            Item key = js_elements_get_int(keys, i);
            if (get_type_id(key) != LMD_TYPE_STRING) continue;
            String* ks = it2s(key);
            bool enumerable = ks ? js_props_obj_query_enumerable(obj_item, ks->chars, (int)ks->len) : true;
            bool hidden = ctx && ctx->show_hidden && !enumerable;
            Item value = js_get_key_default(obj_item, key);
            Item value_text = js_util_inspect_value(value, ctx, depth_left - 1);
            String* vs = get_type_id(value_text) == LMD_TYPE_STRING ? it2s(value_text) : NULL;
            strbuf_append_str(sb, i == 0 ? " " : ", ");
            js_util_inspect_append_quoted_key(sb, ks, hidden);
            strbuf_append_str(sb, ": ");
            if (vs) strbuf_append_str_n(sb, vs->chars, vs->len);
        }
        strbuf_append_str(sb, " }");
    }
    return js_util_inspect_make_string(sb);
}

static Item js_util_inspect_typed_array(Item obj_item, JsInspectContext* ctx, int depth_left) {
    JsTypedArray* ta = js_get_typed_array_ptr(obj_item.map);
    const char* type_name = ta && ta->is_buffer ? "Buffer" : js_typed_array_type_name(obj_item);
    if (!type_name) type_name = "Uint8Array";
    int len = js_typed_array_length(obj_item);
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, type_name);
    strbuf_append_char(sb, '(');
    strbuf_append_int64(sb, len < 0 ? 0 : len);
    if (ta && ta->is_buffer) strbuf_append_str(sb, ") [Uint8Array] [");
    else strbuf_append_str(sb, ") [");
    if (len > 0) strbuf_append_char(sb, ' ');
    for (int i = 0; i < len; i++) {
        if (i > 0) strbuf_append_str(sb, ", ");
        Item elem = js_typed_array_get(obj_item, (Item){.item = i2it(i)});
        Item elem_text = js_util_inspect_value(elem, ctx, depth_left - 1);
        String* es = get_type_id(elem_text) == LMD_TYPE_STRING ? it2s(elem_text) : NULL;
        if (es) strbuf_append_str_n(sb, es->chars, es->len);
    }
    if (len > 0) strbuf_append_char(sb, ' ');
    strbuf_append_char(sb, ']');
    return js_util_inspect_make_string(sb);
}

static Item js_util_inspect_object(Item obj_item, JsInspectContext* ctx, int depth_left) {
    if (depth_left < 0) return make_string_item("[Object]");

    if (ctx && js_util_inspect_seen_contains(ctx->seen, obj_item)) {
        return make_string_item("[Circular]");
    }
    if (ctx) js_array_push(ctx->seen, obj_item);

    Item custom_sym = js_symbol_for(make_string_item("nodejs.util.inspect.custom"));
    Item custom_fn = js_get_key_default(obj_item, custom_sym);
    if (js_is_callable(custom_fn)) {
        Item result = js_call_function(custom_fn, obj_item, nullptr, 0);
        if (ctx) js_util_inspect_seen_pop(ctx->seen);
        if (item_is_error(result)) {
            return make_string_item("[object Object]");
        }
        if (get_type_id(result) == LMD_TYPE_STRING) return result;
        return js_to_string(result);
    }

    if (js_util_inspect_is_assertion_error(obj_item)) {
        Item result = js_util_inspect_assertion_error(obj_item, ctx, depth_left);
        if (ctx) js_util_inspect_seen_pop(ctx->seen);
        return result;
    }
    JsClass cls = js_class_id(obj_item);
    if (cls == JS_CLASS_ABORT_SIGNAL) {
        // Abort internals are hidden backing slots; generic own-key inspection
        // exposes them and loses Node's public AbortSignal rendering.
        Item result = depth_left < 0 ? make_string_item("[AbortSignal]")
            : js_util_inspect_abort_signal(obj_item);
        if (ctx) js_util_inspect_seen_pop(ctx->seen);
        return result;
    }
    if (cls == JS_CLASS_ABORT_CONTROLLER) {
        Item result = depth_left < 0 ? make_string_item("[AbortController]")
            : js_util_inspect_abort_controller(obj_item, ctx, depth_left);
        if (ctx) js_util_inspect_seen_pop(ctx->seen);
        return result;
    }
    if (cls == JS_CLASS_DATE) {
        // Date inspect is value-based; falling through to own properties prints
        // "{}" and breaks assert's public Date diagnostics.
        Item result = js_util_inspect_date(obj_item, ctx, depth_left);
        if (ctx) js_util_inspect_seen_pop(ctx->seen);
        return result;
    }
    if (cls == JS_CLASS_REGEXP) {
        // RegExp inspect must expose the pattern/flags, not the backing map.
        Item result = js_util_inspect_regexp(obj_item, ctx, depth_left);
        if (ctx) js_util_inspect_seen_pop(ctx->seen);
        return result;
    }
    if (cls == JS_CLASS_ARGUMENTS) {
        // Arguments objects have a distinct assert diff label even though their
        // enumerable shape looks object-like.
        Item result = js_util_inspect_arguments(obj_item, ctx, depth_left);
        if (ctx) js_util_inspect_seen_pop(ctx->seen);
        return result;
    }

    Item keys = (ctx && ctx->show_hidden) ? js_object_get_own_property_names(obj_item) : js_object_keys(obj_item);
    int64_t klen = js_array_length(keys);
    if (klen == 0) {
        if (ctx) js_util_inspect_seen_pop(ctx->seen);
        return make_string_item("{}");
    }

    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "{ ");
    bool first = true;
    for (int64_t i = 0; i < klen; i++) {
        js_util_inspect_append_property(sb, obj_item, js_elements_get_int(keys, i), ctx, depth_left, &first);
    }
    strbuf_append_str(sb, " }");
    if (ctx) js_util_inspect_seen_pop(ctx->seen);
    return js_util_inspect_make_string(sb);
}

static Item js_util_inspect_array(Item obj_item, JsInspectContext* ctx, int depth_left) {
    if (depth_left < 0) return make_string_item("[Array]");

    if (ctx && js_util_inspect_seen_contains(ctx->seen, obj_item)) {
        return make_string_item("[Circular]");
    }
    if (ctx) js_array_push(ctx->seen, obj_item);

    int64_t alen = js_array_length(obj_item);
    StrBuf* sb = strbuf_new();
    strbuf_append_char(sb, '[');
    if (alen > 0) strbuf_append_char(sb, ' ');
    bool first = true;
    for (int64_t i = 0; i < alen; i++) {
        Item val = js_elements_get_int(obj_item, i);
        Item val_str = js_util_inspect_value(val, ctx, depth_left - 1);
        String* vs = it2s(val_str);
        if (!first) strbuf_append_str(sb, ", ");
        first = false;
        if (vs) strbuf_append_str_n(sb, vs->chars, vs->len);
    }
    if (ctx && ctx->show_hidden) {
        Item keys = js_object_get_own_property_names(obj_item);
        int64_t klen = js_array_length(keys);
        for (int64_t i = 0; i < klen; i++) {
            Item key = js_elements_get_int(keys, i);
            if (get_type_id(key) != LMD_TYPE_STRING) continue;
            String* ks = it2s(key);
            if (ks && ks->len == 6 && memcmp(ks->chars, "length", 6) == 0) {
                if (!first) strbuf_append_str(sb, ", ");
                first = false;
                strbuf_append_str(sb, "[length]: ");
                strbuf_append_int64(sb, alen);
                continue;
            }
            if (js_util_inspect_key_is_array_index(ks, alen)) continue;
            js_util_inspect_append_property(sb, obj_item, key, ctx, depth_left, &first);
        }
    }
    if (alen > 0 || !first) strbuf_append_char(sb, ' ');
    strbuf_append_char(sb, ']');
    if (ctx) js_util_inspect_seen_pop(ctx->seen);
    return js_util_inspect_make_string(sb);
}

static Item js_util_inspect_value(Item obj_item, JsInspectContext* ctx, int depth_left) {
    TypeId tid = get_type_id(obj_item);
    if (tid == LMD_TYPE_UNDEFINED) {
        if (!ctx || !ctx->colors) return make_string_item("undefined");
        StrBuf* sb = strbuf_new();
        js_util_inspect_append_styled(sb, ctx, "special", "undefined");
        return js_util_inspect_make_string(sb);
    }
    if (tid == LMD_TYPE_NULL) {
        if (!ctx || !ctx->colors) return make_string_item("null");
        StrBuf* sb = strbuf_new();
        js_util_inspect_append_styled(sb, ctx, "special", "null");
        return js_util_inspect_make_string(sb);
    }
    if (tid == LMD_TYPE_BOOL) {
        const char* text = it2b(obj_item) ? "true" : "false";
        if (!ctx || !ctx->colors) return make_string_item(text);
        StrBuf* sb = strbuf_new();
        js_util_inspect_append_styled(sb, ctx, "boolean", text);
        return js_util_inspect_make_string(sb);
    }
    if (tid == LMD_TYPE_INT && it2i(obj_item) <= -(int64_t)JS_SYMBOL_BASE) {
        return js_symbol_to_string(obj_item);
    }

    if (tid == LMD_TYPE_INT || tid == LMD_TYPE_FLOAT) return js_util_inspect_number(obj_item, ctx);
    if (tid == LMD_TYPE_DECIMAL) {
        Decimal* dec = (Decimal*)(obj_item.item & 0x00FFFFFFFFFFFFFF);
        if (dec && dec->unlimited == DECIMAL_BIGINT) {
            // BigInt primitives must be printable diagnostics; falling through to
            // object/stringify paths turns inspect() into a fatal JSON BigInt throw.
            return js_util_inspect_bigint(obj_item, ctx);
        }
    }
    if (tid == LMD_TYPE_STRING) return js_util_inspect_string(obj_item, ctx);

    if (js_class_id(obj_item) == JS_CLASS_PROMISE) {
        const char* state = js_promise_state_name(obj_item);
        if (state && strcmp(state, "pending") == 0) return make_string_item("Promise { <pending> }");
        if (state && strcmp(state, "fulfilled") == 0) return make_string_item("Promise { <fulfilled> }");
        if (state && strcmp(state, "rejected") == 0) return make_string_item("Promise { <rejected> }");
    }

    if (tid == LMD_TYPE_FUNC) {
        Item name = js_get_key_cstr(obj_item, "name");
        String* ns = get_type_id(name) == LMD_TYPE_STRING ? it2s(name) : NULL;
        StrBuf* sb = strbuf_new();
        if (ns && ns->len > 0) {
            strbuf_append_str(sb, "[Function: ");
            strbuf_append_str_n(sb, ns->chars, ns->len);
            strbuf_append_char(sb, ']');
        } else {
            strbuf_append_str(sb, "[Function (anonymous)]");
        }
        return js_util_inspect_make_string(sb);
    }

    if (js_class_id(obj_item) == JS_CLASS_ARGUMENTS || js_util_is_arguments_exotic(obj_item)) {
        // Arguments may share array storage with a companion property map, but
        // Node diagnostics preserve the public [Arguments] label.
        return js_util_inspect_arguments(obj_item, ctx, depth_left);
    }
    if (tid == LMD_TYPE_ARRAY) return js_util_inspect_array(obj_item, ctx, depth_left);
    if (js_is_typed_array(obj_item)) return js_util_inspect_typed_array(obj_item, ctx, depth_left);
    if (js_util_inspect_is_object_like(obj_item)) return js_util_inspect_object(obj_item, ctx, depth_left);

    Item result = js_json_stringify(obj_item);
    if (get_type_id(result) == LMD_TYPE_STRING) return result;
    return js_to_string(obj_item);
}

extern "C" Item js_util_inspect(Item obj_item, Item options_item) {
    JsInspectContext ctx;
    ctx.show_hidden = get_type_id(options_item) == LMD_TYPE_BOOL ? it2b(options_item)
                                                                  : js_util_inspect_option_bool(options_item, "showHidden", false);
    ctx.colors = js_util_inspect_option_bool(options_item, "colors", false);
    ctx.depth = js_util_inspect_option_depth(options_item);
    ctx.seen = js_array_new(0);
    return js_util_inspect_value(obj_item, &ctx, ctx.depth);
}

// =============================================================================
// util.types — type checking namespace
// =============================================================================

#define JS_UTIL_TYPE_TEST(name, expression) \
    extern "C" Item name(Item value) { \
        return (Item){.item = b2it(expression)}; \
    }
JS_UTIL_TYPE_TEST(js_util_types_isDate, js_class_id(value) == JS_CLASS_DATE)
JS_UTIL_TYPE_TEST(js_util_types_isRegExp, js_class_id(value) == JS_CLASS_REGEXP)
JS_UTIL_TYPE_TEST(js_util_types_isArray, get_type_id(value) == LMD_TYPE_ARRAY)
JS_UTIL_TYPE_TEST(js_util_types_isMap, js_class_id(value) == JS_CLASS_MAP)
JS_UTIL_TYPE_TEST(js_util_types_isSet, js_class_id(value) == JS_CLASS_SET)
#undef JS_UTIL_TYPE_TEST

// =============================================================================
// util.promisify(original) — callback-last API to Promise wrapper
// =============================================================================

static Item js_util_promisify_result_from_args(Item custom_args, Item rest_args);
static bool js_util_is_promise_like(Item value);
static void js_util_emit_promisify_promise_warning(void);

static Item js_util_promisify_callback(Item env_item, Item rest_args) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();

    JS_ROOTS(roots,
        resolve_root, env[0],
        reject_root, env[1],
        custom_args_root, env[2],
        rest_args_root, rest_args,
        value_root, ItemNull);
    int64_t argc = js_array_length(rest_args_root.get());
    Item err = (argc > 0) ? js_elements_get_int(rest_args_root.get(), 0) : make_js_undefined();

    if (js_is_truthy(err)) {
        Item reject_args[1] = {err};
        js_call_function(reject_root.get(), make_js_undefined(), reject_args, 1);
        return make_js_undefined();
    }

    // Promise callbacks enter from an unrooted native argument frame; retain
    // the closure environment and result while custom result construction allocates.
    value_root.set(js_util_promisify_result_from_args(custom_args_root.get(), rest_args_root.get()));
    Item resolve_args[1] = {value_root.get()};
    js_call_function(resolve_root.get(), make_js_undefined(), resolve_args, 1);
    return make_js_undefined();
}

static Item js_util_promisify_executor(Item env_item, Item resolve, Item reject) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();

    RootFrame roots(9);
    Rooted<Item> original_root(roots, env[0]);
    Rooted<Item> this_root(roots, env[1]);
    Rooted<Item> call_args_root(roots, env[2]);
    Rooted<Item> custom_args_root(roots, env[3]);
    Rooted<Item> resolve_root(roots, resolve);
    Rooted<Item> reject_root(roots, reject);
    Rooted<Item> callback_root(roots, ItemNull);
    Rooted<Item> error_root(roots, ItemNull);
    int64_t argc64 = js_array_length(call_args_root.get());
    if (argc64 < 0) argc64 = 0;

    Item* cb_env = js_alloc_env(3);
    cb_env[0] = resolve_root.get();
    cb_env[1] = reject_root.get();
    cb_env[2] = custom_args_root.get();
    callback_root.set(js_new_native_closure(js_util_promisify_callback, -1, cb_env, 3));

    int argc = (int)argc64 + 1;
    Item* call_args = (Item*)alloca((size_t)argc * sizeof(Item));
    for (int i = 0; i < (int)argc64; i++) {
        call_args[i] = js_elements_get_int(call_args_root.get(), i);
    }
    call_args[argc - 1] = callback_root.get();

    Item call_result = js_call_function(original_root.get(), this_root.get(), call_args, argc);
    if (item_is_error(call_result)) {
        error_root.set(js_error_lane_payload(call_result));
        Item reject_args[1] = {error_root.get()};
        (void)js_call_function(reject_root.get(), make_js_undefined(), reject_args, 1);
    } else if (js_util_is_promise_like(call_result)) {
        js_util_emit_promisify_promise_warning();
    }
    return make_js_undefined();
}

static Item js_util_promisified_function(Item env_item, Item rest_args) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return js_promise_reject(make_string_item("promisified function missing target"));

    JS_ROOTS(roots,
        original_root, env[0],
        custom_args_root, env[1],
        this_root, js_get_this(),
        rest_args_root, rest_args,
        call_args_root, ItemNull,
        executor_root, ItemNull);
    call_args_root.set(js_array_new(0));
    int64_t argc = js_array_length(rest_args_root.get());
    for (int64_t i = 0; i < argc; i++) {
        js_array_push(call_args_root.get(), js_elements_get_int(rest_args_root.get(), i));
    }

    Item* exec_env = js_alloc_env(4);
    exec_env[0] = original_root.get();
    exec_env[1] = this_root.get();
    exec_env[2] = call_args_root.get();
    exec_env[3] = custom_args_root.get();
    executor_root.set(js_new_native_closure(js_util_promisify_executor, 2, exec_env, 4));
    return js_promise_create(executor_root.get());
}
JS_FORWARD_STATIC_ITEM(js_util_promisify_symbol, (const char* name), js_symbol_for, (make_string_item(name)))
JS_FORWARD_ITEM(js_util_promisify_custom_symbol, (void), js_util_promisify_symbol, ("nodejs.util.promisify.custom"))
JS_FORWARD_ITEM(js_util_custom_promisify_args_symbol, (void), js_util_promisify_symbol, ("nodejs.util.promisify.customArgs"))

static bool js_util_is_promise_like(Item value) {
    if (js_class_id(value) == JS_CLASS_PROMISE) return true;
    TypeId type = get_type_id(value);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_FUNC) return false;
    Item then_fn = js_get_key_cstr(value, "then");
    if (item_is_error(then_fn)) return false;
    return js_is_callable(then_fn);
}

static void js_util_emit_promisify_promise_warning(void) {
    Item warning = js_new_object();
    js_set_key_cstr(warning, "name", make_string_item("DeprecationWarning"));
    js_set_key_cstr(warning, "message", make_string_item("Calling promisify on a function that returns a Promise is likely a mistake."));
    js_set_key_cstr(warning, "code", make_string_item("DEP0174"));
    (void)js_process_emit(make_string_item("warning"), warning);
}

static Item js_util_promisify_result_from_args(Item custom_args, Item rest_args) {
    int64_t argc = js_array_length(rest_args);
    if (argc <= 1) return make_js_undefined();

    if (get_type_id(custom_args) == LMD_TYPE_ARRAY) {
        int64_t names_len = js_array_length(custom_args);
        if (names_len > 0) {
            Item obj = js_new_object();
            int64_t value_count = argc - 1;
            int64_t count = names_len < value_count ? names_len : value_count;
            for (int64_t i = 0; i < count; i++) {
                Item name = js_elements_get_int(custom_args, i);
                if (get_type_id(name) == LMD_TYPE_STRING) {
                    js_set_key_default(obj, name, js_elements_get_int(rest_args, i + 1));
                }
            }
            return obj;
        }
    }

    return js_elements_get_int(rest_args, 1);
}

extern "C" Item js_util_promisify(Item fn_item) {
    if (!js_is_callable(fn_item)) {
        return js_throw_invalid_arg_type("original", "function", fn_item);
    }

    Item custom_key = js_util_promisify_custom_symbol();
    JS_ASSIGN_OR_RETURN(custom, js_get_key_default(fn_item, custom_key));
    if (custom.item != ITEM_NULL && custom.item != ITEM_JS_UNDEFINED &&
        get_type_id(custom) != LMD_TYPE_UNDEFINED) {
        if (!js_is_callable(custom)) {
            return js_throw_invalid_arg_type("util.promisify.custom", "function", custom);
        }
        return custom;
    }

    JS_ASSIGN_OR_RETURN(custom_args, js_get_key_default(fn_item, js_util_custom_promisify_args_symbol()));

    Item* env = js_alloc_env(2);
    env[0] = fn_item;
    env[1] = custom_args;
    Item wrapper = js_new_native_closure(js_util_promisified_function, -1, env, 2);
    js_set_key_default(wrapper, custom_key, wrapper);
    js_set_function_name(wrapper, make_string_item("promisified"));
    return wrapper;
}

// =============================================================================
// util.callbackify(original) — Promise API to callback-last wrapper
// =============================================================================

static Item js_util_callbackify_make_falsy_error(Item reason) {
    Item error = js_new_error(make_string_item("Promise was rejected with a falsy value"));
    js_set_key_cstr(error, "code", make_string_item("ERR_FALSY_VALUE_REJECTION"));
    js_set_key_cstr(error, "reason", reason);
    return error;
}

static Item js_util_callbackify_on_fulfilled(Item env_item, Item value) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();

    Item callback = env[0];
    Item callback_args[2] = {ItemNull, value};
    js_call_function(callback, make_js_undefined(), callback_args, 2);
    return make_js_undefined();
}

static Item js_util_callbackify_on_rejected(Item env_item, Item reason) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();

    Item callback = env[0];
    Item error = js_is_truthy(reason) ? reason : js_util_callbackify_make_falsy_error(reason);
    Item callback_args[1] = {error};
    js_call_function(callback, make_js_undefined(), callback_args, 1);
    return make_js_undefined();
}

static Item js_util_callbackified_function(Item env_item, Item rest_args) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();

    Item original = env[0];
    int64_t argc64 = js_array_length(rest_args);
    if (argc64 <= 0) {
        return js_throw_invalid_arg_type("callback", "function", make_js_undefined());
    }

    Item callback = js_elements_get_int(rest_args, argc64 - 1);
    if (!js_is_callable(callback)) {
        return js_throw_invalid_arg_type("callback", "function", callback);
    }

    int argc = (int)argc64 - 1;
    Item* call_args = argc > 0 ? (Item*)alloca((size_t)argc * sizeof(Item)) : NULL;
    for (int i = 0; i < argc; i++) {
        call_args[i] = js_elements_get_int(rest_args, i);
    }

    Item result = js_call_function(original, js_get_this(), call_args, argc);
    Item promise = ItemNull;
    if (item_is_error(result)) {
        promise = js_promise_reject(js_error_lane_payload(result));
    } else {
        promise = js_promise_resolve(result);
        if (item_is_error(promise)) promise = js_promise_reject(js_error_lane_payload(promise));
    }

    Item* cb_env = js_alloc_env(1);
    cb_env[0] = callback;
    Item on_fulfilled = js_new_native_closure(js_util_callbackify_on_fulfilled, 1, cb_env, 1);
    Item on_rejected = js_new_native_closure(js_util_callbackify_on_rejected, 1, cb_env, 1);
    js_promise_then(promise, on_fulfilled, on_rejected);
    return make_js_undefined();
}

extern "C" Item js_util_callbackify(Item fn) {
    if (!js_is_callable(fn)) {
        return js_throw_invalid_arg_type("original", "function", fn);
    }

    Item* env = js_alloc_env(1);
    env[0] = fn;
    Item wrapper = js_new_native_closure(js_util_callbackified_function, -1, env, 1);
    js_set_function_name(wrapper, make_string_item("callbackified"));
    return wrapper;
}

// =============================================================================
// util.aborted(signal, resource)
// =============================================================================
JS_FORWARD_STATIC_EXPRESSION(bool, js_util_is_abort_signal, (Item signal), (js_class_id(signal) == JS_CLASS_ABORT_SIGNAL))

static bool js_util_is_resource_object(Item resource) {
    TypeId type = get_type_id(resource);
    return type == LMD_TYPE_MAP || type == LMD_TYPE_ARRAY || type == LMD_TYPE_OBJECT ||
           type == LMD_TYPE_FUNC || type == LMD_TYPE_VMAP || type == LMD_TYPE_ELEMENT;
}

static Item js_util_invalid_arg_rejection(const char* name, const char* expected) {
    char msg[256];
    snprintf(msg, sizeof(msg),
             "The \"%s\" argument must be of type %s.", name, expected);
    Item error = js_new_error_with_name(make_string_item("TypeError"), make_string_item(msg));
    js_set_key_cstr(error, "code", make_string_item("ERR_INVALID_ARG_TYPE"));
    return js_promise_reject(error);
}

static Item js_util_aborted_on_abort(Item env_item, Item event_item) {
    (void)event_item;
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();

    Item signal = env[0];
    Item resolve = env[1];
    Item handler = env[2];

    Item remove_fn = js_get_key_cstr(signal, "removeEventListener");
    if (js_is_callable(remove_fn)) {
        Item remove_args[2] = { make_string_item("abort"), handler };
        (void)js_call_function(remove_fn, signal, remove_args, 2);
    }

    if (js_is_callable(resolve)) {
        Item resolve_args[1] = { make_js_undefined() };
        (void)js_call_function(resolve, make_js_undefined(), resolve_args, 1);
    }
    return make_js_undefined();
}

static Item js_util_aborted_executor(Item env_item, Item resolve, Item reject) {
    (void)reject;
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();

    Item signal = env[0];
    Item aborted = js_get_key_cstr(signal, "aborted");
    if (get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted)) {
        Item resolve_args[1] = { make_js_undefined() };
        (void)js_call_function(resolve, make_js_undefined(), resolve_args, 1);
        return make_js_undefined();
    }

    Item* handler_env = js_alloc_env(3);
    handler_env[0] = signal;
    handler_env[1] = resolve;
    handler_env[2] = make_js_undefined();
    Item handler = js_new_native_closure(js_util_aborted_on_abort, 1, handler_env, 3);
    handler_env[2] = handler;

    Item add_fn = js_get_key_cstr(signal, "addEventListener");
    if (js_is_callable(add_fn)) {
        Item add_args[2] = { make_string_item("abort"), handler };
        (void)js_call_function(add_fn, signal, add_args, 2);
    }
    return make_js_undefined();
}

extern "C" Item js_util_aborted(Item signal, Item resource) {
    if (!js_util_is_abort_signal(signal)) {
        return js_util_invalid_arg_rejection("signal", "AbortSignal");
    }
    if (!js_util_is_resource_object(resource)) {
        return js_util_invalid_arg_rejection("resource", "object");
    }

    Item* env = js_alloc_env(1);
    env[0] = signal;
    Item executor = js_new_native_closure(js_util_aborted_executor, 2, env, 1);
    return js_promise_create(executor);
}

// =============================================================================
// util.deprecate(fn, msg) — returns fn (warning stub)
// =============================================================================

extern "C" Item js_util_deprecate(Item fn_item, Item msg_item) {
    (void)msg_item;
    // return the function as-is (no runtime warning implemented)
    return fn_item;
}

// =============================================================================
// util.inherits(constructor, superConstructor) — legacy inheritance
// =============================================================================

extern "C" Item js_util_inherits(Item ctor_item, Item super_item) {
    // validate ctor — must be typeof "function" (regular function or ES6 class)
    Item ctor_type = js_typeof(ctor_item);
    String* ctor_type_str = it2s(ctor_type);
    bool ctor_is_func = ctor_type_str && ctor_type_str->len == 8 &&
                        memcmp(ctor_type_str->chars, "function", 8) == 0;
    if (!ctor_is_func) {
        const char* received = (ctor_item.item == ITEM_NULL) ? "null" :
                               (ctor_item.item == ITEM_JS_UNDEFINED) ? "undefined" :
                               ctor_type_str ? ctor_type_str->chars : "unknown";
        return js_throw_type_error_codef("ERR_INVALID_ARG_TYPE", 
            "The \"ctor\" argument must be of type function. Received %s", received);
    }
    // validate superCtor — reject null, undefined, and non-object primitives
    if (super_item.item == ITEM_NULL || super_item.item == ITEM_JS_UNDEFINED) {
        const char* received = (super_item.item == ITEM_NULL) ? "null" : "undefined";
        return js_throw_type_error_codef("ERR_INVALID_ARG_TYPE", 
            "The \"superCtor\" argument must be of type function. Received %s", received);
    }
    // validate superCtor.prototype
    Item super_proto = js_get_key_cstr(super_item, "prototype");
    TypeId proto_tid = get_type_id(super_proto);
    if (proto_tid != LMD_TYPE_MAP && proto_tid != LMD_TYPE_ARRAY) {
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "The \"superCtor.prototype\" property must be of type object. Received undefined");
    }
    // Set ctor.prototype.__proto__ = superCtor.prototype
    // This preserves existing properties on ctor.prototype (like D.prototype.d set before inherits)
    Item ctor_proto = js_get_key_cstr(ctor_item, "prototype");
    js_set_key_cstr(ctor_proto, "__proto__", super_proto);
    js_set_key_cstr(ctor_proto, "constructor", ctor_item);
    // set ctor.super_ = superConstructor (non-enumerable, per Node.js spec)
    Item super_key = make_string_item("super_");
    js_set_key_default(ctor_item, super_key, super_item);
    js_mark_non_enumerable(ctor_item, super_key);
    return make_js_undefined();
}

// =============================================================================
// util.isDeepStrictEqual(val1, val2) — basic deep equality
// =============================================================================

static bool js_util_dataview_current_byte_length(JsDataView* view, int* out_length) {
    if (!view || !view->buffer || js_arraybuffer_detached(view->buffer)) return false;
    int buffer_length = js_arraybuffer_length(view->buffer);
    if (buffer_length < view->byte_offset) return false;
    if (view->length_tracking) {
        *out_length = buffer_length - view->byte_offset;
        return true;
    }
    if (view->byte_length < 0 ||
        buffer_length < (int64_t)view->byte_offset + (int64_t)view->byte_length) {
        return false;
    }
    *out_length = view->byte_length;
    return true;
}

static bool js_util_dataview_bytes_equal(Item a, Item b) {
    JsDataView* av = js_get_dataview_ptr(a);
    JsDataView* bv = js_get_dataview_ptr(b);
    int alen = 0, blen = 0;
    if (!js_util_dataview_current_byte_length(av, &alen) ||
        !js_util_dataview_current_byte_length(bv, &blen)) {
        return false;
    }
    if (alen != blen) return false;
    if (alen == 0) return true;
    const uint8_t* av_data = js_arraybuffer_data_const(av->buffer);
    const uint8_t* bv_data = js_arraybuffer_data_const(bv->buffer);
    if (!av_data || !bv_data) return false;
    const uint8_t* adata = av_data + av->byte_offset;
    const uint8_t* bdata = bv_data + bv->byte_offset;
    return memcmp(adata, bdata, alen) == 0;
}

static bool js_util_typed_array_bytes_equal(Item a, Item b) {
    JsTypedArray* av = js_get_typed_array_ptr(a.map);
    JsTypedArray* bv = js_get_typed_array_ptr(b.map);
    if (!av || !bv) return false;
    if (av->element_type != bv->element_type) return false;
    if (js_typed_array_is_out_of_bounds_item(a) || js_typed_array_is_out_of_bounds_item(b)) {
        return false;
    }
    int alen = js_typed_array_byte_length(a);
    int blen = js_typed_array_byte_length(b);
    if (alen != blen) return false;
    if (alen == 0) return true;
    void* adata = js_typed_array_current_data_ptr(a);
    void* bdata = js_typed_array_current_data_ptr(b);
    if (!adata || !bdata) return false;
    return memcmp(adata, bdata, (size_t)alen) == 0;
}

static bool js_util_typed_array_loose_compatible(JsTypedArray* a, JsTypedArray* b) {
    if (!a || !b) return false;
    if (a->element_type == b->element_type) return true;
    // Buffer is a Uint8Array subclass; loose deep equality may ignore that
    // subclass boundary, but signed/wider typed-array views are distinct types.
    return ((a->is_buffer && a->element_type == JS_TYPED_UINT8 && b->element_type == JS_TYPED_UINT8) ||
            (b->is_buffer && b->element_type == JS_TYPED_UINT8 && a->element_type == JS_TYPED_UINT8));
}

static bool js_util_arraybuffer_bytes_equal(Item a, Item b) {
    JsArrayBuffer* ab = js_get_arraybuffer_ptr_item(a);
    JsArrayBuffer* bb = js_get_arraybuffer_ptr_item(b);
    if (!ab || !bb) return false;
    if (js_arraybuffer_shared(ab) != js_arraybuffer_shared(bb)) return false;
    if (js_arraybuffer_detached(ab) || js_arraybuffer_detached(bb)) {
        return js_arraybuffer_detached(ab) && js_arraybuffer_detached(bb);
    }
    int byte_length = js_arraybuffer_length(ab);
    if (byte_length != js_arraybuffer_length(bb)) return false;
    if (byte_length == 0) return true;
    const uint8_t* adata = js_arraybuffer_data_const(ab);
    const uint8_t* bdata = js_arraybuffer_data_const(bb);
    if (!adata || !bdata) return false;
    return memcmp(adata, bdata, (size_t)byte_length) == 0;
}

static bool js_util_url_href_equal(Item a, Item b) {
    if (get_type_id(a) != LMD_TYPE_MAP || get_type_id(b) != LMD_TYPE_MAP) return false;
    if (js_class_id(a) != JS_CLASS_URL || js_class_id(b) != JS_CLASS_URL) return false;
    Item href_key = make_string_item("href", 4);
    Item ah = js_get_key_default(a, href_key);
    Item bh = js_get_key_default(b, href_key);
    return js_is_truthy(js_strict_equal(ah, bh));
}

static bool js_util_is_host_singleton_object(Item value) {
    if (get_type_id(value) != LMD_TYPE_MAP) return false;
    return js_is_global_this_object_value(value) || js_is_process_object_value(value);
}

typedef struct JsDeepEqualPair {
    Item a;
    Item b;
} JsDeepEqualPair;

typedef struct JsDeepEqualContext {
    JsDeepEqualPair stack[4096];
    int depth;
} JsDeepEqualContext;
JS_FORWARD_STATIC_EXPRESSION(bool, js_util_deep_equal_is_object_like_type, (TypeId type), (type == LMD_TYPE_MAP || type == LMD_TYPE_ARRAY || type == LMD_TYPE_OBJECT || type == LMD_TYPE_ELEMENT || type == LMD_TYPE_VMAP))

static int js_util_deep_equal_enter(JsDeepEqualContext* ctx, Item a, Item b) {
    if (!ctx) return 1;
    for (int i = 0; i < ctx->depth; i++) {
        if (ctx->stack[i].a.item == a.item && ctx->stack[i].b.item == b.item) return 0;
        if (ctx->stack[i].a.item == a.item || ctx->stack[i].b.item == b.item) return -1;
    }
    if (ctx->depth >= (int)(sizeof(ctx->stack) / sizeof(ctx->stack[0]))) return -1;
    ctx->stack[ctx->depth].a = a;
    ctx->stack[ctx->depth].b = b;
    ctx->depth++;
    return 1;
}

static void js_util_deep_equal_leave(JsDeepEqualContext* ctx) {
    if (ctx && ctx->depth > 0) ctx->depth--;
}

static bool js_util_is_nan_number(Item value) {
    TypeId type = get_type_id(value);
    return type == LMD_TYPE_FLOAT && isnan(it2d(value));
}

static bool js_util_strict_zero_sign_differs(Item a, Item b) {
    TypeId ta = get_type_id(a);
    TypeId tb = get_type_id(b);
    bool a_num = ta == LMD_TYPE_INT || ta == LMD_TYPE_FLOAT;
    bool b_num = tb == LMD_TYPE_INT || tb == LMD_TYPE_FLOAT;
    if (!a_num || !b_num) return false;
    double av = ta == LMD_TYPE_FLOAT ? it2d(a) : (double)it2i(a);
    double bv = tb == LMD_TYPE_FLOAT ? it2d(b) : (double)it2i(b);
    return av == 0.0 && bv == 0.0 && signbit(av) != signbit(bv);
}

static bool js_util_has_own_key(Item object, const char* key, int len) {
    Item result = js_has_own_property(object, js_name_item(key, len));
    return get_type_id(result) == LMD_TYPE_BOOL && it2b(result);
}
JS_FORWARD_STATIC_EXPRESSION(Item, js_util_deep_dispatch_value, (Item value), (js_is_proxy(value) ? js_proxy_get_target(value) : value))

static TypeId js_util_deep_dispatch_type(Item value) {
    return get_type_id(js_util_deep_dispatch_value(value));
}

static JsClass js_util_deep_dispatch_class(Item value) {
    Item target = js_util_deep_dispatch_value(value);
    return get_type_id(target) == LMD_TYPE_MAP ? js_class_id(target) : JS_CLASS_NONE;
}

static int64_t js_util_array_length_for_deep(Item value) {
    if (!js_is_proxy(value)) return js_array_length(value);
    Item len = js_get_key_default(value, make_string_item("length", 6));
    TypeId type = get_type_id(len);
    if (type == LMD_TYPE_INT) return it2i(len);
    if (type == LMD_TYPE_FLOAT) return (int64_t)it2d(len);
    return -1;
}

#define js_util_string_equals js_string_equals

static bool js_util_has_constructor_prototype(Item value, const char* ctor_name, int ctor_len) {
    if (get_type_id(value) != LMD_TYPE_MAP) return false;
    Item proto = js_get_prototype_of(value);
    if (get_type_id(proto) != LMD_TYPE_MAP) return false;
    Item ctor = js_get_constructor(make_string_item(ctor_name, ctor_len));
    if (get_type_id(ctor) == LMD_TYPE_FUNC) {
        Item ctor_proto = js_get_key_default(ctor, make_string_item("prototype", 9));
        if (proto.item == ctor_proto.item) return true;
    }
    Item tag = js_get_key_default(proto, js_well_known_symbol_key(4));
    return js_util_string_equals(tag, ctor_name);
}

static bool js_util_is_real_regexp(Item value) {
    if (get_type_id(value) != LMD_TYPE_MAP) return false;
    Item target = js_util_deep_dispatch_value(value);
    if (get_type_id(target) != LMD_TYPE_MAP) return false;
    return js_class_id(target) == JS_CLASS_REGEXP;
}

static bool js_util_is_regexp_like_value(Item value) {
    if (js_util_is_real_regexp(value)) return true;
    return js_util_has_constructor_prototype(value, "RegExp", 6) ||
           js_util_string_equals(js_get_key_default(value, js_well_known_symbol_key(4)), "RegExp");
}
JS_FORWARD_STATIC_RETURN(bool, js_util_is_url_like_value, (Item value), get_type_id, (value) == LMD_TYPE_MAP && (js_util_deep_dispatch_class(value) == JS_CLASS_URL || js_util_has_constructor_prototype(value, "URL", 3)))

static bool js_util_is_error_like_value(Item value) {
    if (get_type_id(value) != LMD_TYPE_MAP) return false;
    if (js_class_is_error_like(js_util_deep_dispatch_class(value))) return true;
    return js_util_has_constructor_prototype(value, "Error", 5);
}

static Item js_util_isDeepEqual_impl(Item a, Item b, JsDeepEqualContext* ctx, bool strict);

JS_FORWARD_STATIC_EXPRESSION(bool, js_util_key_is_symbol, (Item key), (get_type_id(key) == LMD_TYPE_INT && it2i(key) <= -(int64_t)JS_SYMBOL_BASE))

static Item js_util_enumerable_own_keys(Item object, bool include_symbols) {
    // Typed-array indexed storage is already compared by raw bytes; materializing
    // Object.keys for every numeric index makes large assert.deep* cases time out.
    Item result = js_is_typed_array(object)
        ? js_typed_array_enumerable_custom_keys(object)
        : js_object_keys(object);
    if (get_type_id(result) != LMD_TYPE_ARRAY) result = js_array_new(0);
    int64_t result_len = js_array_length(result);
    Item filtered = ItemNull;
    bool filtered_any = false;
    for (int64_t i = 0; i < result_len; i++) {
        Item key = js_elements_get_int(result, i);
        String* ks = get_type_id(key) == LMD_TYPE_STRING ? it2s(key) : NULL;
        bool skip_key = false;
        if (ks && ks->len == 20 && memcmp(ks->chars, "__strict_arguments__", 20) == 0) {
            // The strict Arguments marker is runtime bookkeeping, not an
            // enumerable user key for Node-compatible deep equality.
            skip_key = true;
        }
        if (skip_key) {
            if (!filtered_any) {
                filtered = js_array_new(0);
                for (int64_t j = 0; j < i; j++) js_array_push(filtered, js_elements_get_int(result, j));
            }
            filtered_any = true;
            continue;
        }
        if (filtered_any && get_type_id(filtered) != LMD_TYPE_ARRAY) {
            filtered = js_array_new(0);
            for (int64_t j = 0; j < i; j++) js_array_push(filtered, js_elements_get_int(result, j));
        }
        if (get_type_id(filtered) == LMD_TYPE_ARRAY) js_array_push(filtered, key);
    }
    if (filtered_any) {
        if (get_type_id(filtered) != LMD_TYPE_ARRAY) filtered = js_array_new(0);
        result = filtered;
    }
    if (!include_symbols) return result;

    Item symbols = js_object_get_own_property_symbols(object);
    if (get_type_id(symbols) != LMD_TYPE_ARRAY) return result;
    int64_t symbol_count = js_array_length(symbols);
    for (int64_t i = 0; i < symbol_count; i++) {
        Item key = js_elements_get_int(symbols, i);
        Item desc = js_object_get_own_property_descriptor(object, key);
        if (js_descriptor_is_enumerable(desc)) js_array_push(result, key);
    }
    return result;
}

static bool js_util_same_property_key(Item a, Item b) {
    if (a.item == b.item) return true;
    if (js_util_key_is_symbol(a) || js_util_key_is_symbol(b)) return false;
    return it2b(js_strict_equal(a, b));
}

static Item js_util_find_matching_key(Item keys, Item key) {
    int64_t len = js_array_length(keys);
    for (int64_t i = 0; i < len; i++) {
        Item candidate = js_elements_get_int(keys, i);
        if (js_util_same_property_key(candidate, key)) return candidate;
    }
    return ItemNull;
}

static bool js_util_compare_enumerable_properties(Item a, Item b, JsDeepEqualContext* ctx, bool strict) {
    Item keys_a = js_util_enumerable_own_keys(a, strict);
    Item keys_b = js_util_enumerable_own_keys(b, strict);
    int64_t la = js_array_length(keys_a);
    int64_t lb = js_array_length(keys_b);
    if (la != lb) return false;
    for (int64_t i = 0; i < la; i++) {
        Item key_a = js_elements_get_int(keys_a, i);
        Item key_b = js_util_find_matching_key(keys_b, key_a);
        if (key_b.item == ItemNull.item) return false;
        Item r = js_util_isDeepEqual_impl(
            js_get_key_default(a, key_a),
            js_get_key_default(b, key_b),
            ctx, strict);
        if (!js_is_truthy(r)) return false;
    }
    return true;
}

static bool js_util_compare_unordered_collection(Item a, Item b,
        JsDeepEqualContext* ctx, bool strict, bool is_map) {
    Item size_a = js_collection_method(a, 9, ItemNull, ItemNull);
    Item size_b = js_collection_method(b, 9, ItemNull, ItemNull);
    if (it2i(size_a) != it2i(size_b)) return false;
    Item items_a = js_iterable_to_array(a);
    Item items_b = js_iterable_to_array(b);
    int64_t len_a = js_array_length(items_a);
    int64_t len_b = js_array_length(items_b);
    bool matched[1024];
    for (int64_t i = 0; i < len_b && i < 1024; i++) matched[i] = false;
    for (int64_t i = 0; i < len_a; i++) {
        Item left = js_elements_get_int(items_a, i);
        Item left_key = is_map ? js_elements_get_int(left, 0) : left;
        Item left_value = is_map ? js_elements_get_int(left, 1) : ItemNull;
        bool found = false;
        for (int64_t j = 0; j < len_b; j++) {
            if (j < 1024 && matched[j]) continue;
            Item right = js_elements_get_int(items_b, j);
            Item right_key = is_map ? js_elements_get_int(right, 0) : right;
            Item key_result = js_util_isDeepEqual_impl(left_key, right_key, ctx, strict);
            if (!js_is_truthy(key_result)) continue;
            if (is_map) {
                Item value_result = js_util_isDeepEqual_impl(left_value,
                    js_elements_get_int(right, 1), ctx, strict);
                if (!js_is_truthy(value_result)) continue;
            }
            if (j < 1024) matched[j] = true;
            found = true;
            break;
        }
        if (!found) return false;
    }
    return true;
}

static bool js_util_date_time_equal(Item a, Item b) {
    if (js_class_id(a) != JS_CLASS_DATE || js_class_id(b) != JS_CLASS_DATE) return false;
    Item at = js_date_method(a, 0);
    Item bt = js_date_method(b, 0);
    TypeId ata = get_type_id(at);
    TypeId bta = get_type_id(bt);
    if ((ata != LMD_TYPE_INT && ata != LMD_TYPE_FLOAT) ||
            (bta != LMD_TYPE_INT && bta != LMD_TYPE_FLOAT)) {
        return false;
    }
    double av = ata == LMD_TYPE_FLOAT ? it2d(at) : (double)it2i(at);
    double bv = bta == LMD_TYPE_FLOAT ? it2d(bt) : (double)it2i(bt);
    if (isnan(av) || isnan(bv)) return isnan(av) && isnan(bv);
    return av == bv;
}

static bool js_util_regexp_slots_equal(Item a, Item b, JsDeepEqualContext* ctx, bool strict) {
    if (!js_util_is_real_regexp(a) || !js_util_is_real_regexp(b)) return false;
    const char* names[] = {"source", "flags", "lastIndex", NULL};
    const int lens[] = {6, 5, 9, 0};
    for (int i = 0; names[i]; i++) {
        Item key = make_string_item(names[i], lens[i]);
        Item r = js_util_isDeepEqual_impl(js_get_key_default(a, key), js_get_key_default(b, key), ctx, strict);
        if (!js_is_truthy(r)) return false;
    }
    return true;
}

static bool js_util_is_weak_collection(Item value) {
    if (get_type_id(value) != LMD_TYPE_MAP) return false;
    JsClass cls = js_class_id(value);
    return cls == JS_CLASS_WEAK_MAP || cls == JS_CLASS_WEAK_SET;
}

static bool js_util_is_buffer_value(Item value) {
    Item result = js_buffer_isBuffer(value);
    if (get_type_id(result) == LMD_TYPE_BOOL) return it2b(result);
    return get_type_id(result) == LMD_TYPE_INT && it2i(result) != 0;
}

static bool js_util_is_boxed_primitive(Item value) {
    if (get_type_id(value) != LMD_TYPE_MAP) return false;
    JsClass cls = js_class_id(value);
    if (cls == JS_CLASS_BOOLEAN || cls == JS_CLASS_NUMBER ||
            cls == JS_CLASS_STRING || cls == JS_CLASS_SYMBOL ||
            cls == JS_CLASS_BIGINT) {
        return true;
    }
    bool found = false;
    (void)js_map_shape_lookup_ext(value.map, "__primitiveValue__", 18, &found);
    return found;
}

static bool js_util_boxed_primitive_equal(Item a, Item b, JsDeepEqualContext* ctx, bool strict) {
    if (!js_util_is_boxed_primitive(a) || !js_util_is_boxed_primitive(b)) return false;
    if (js_class_id(a) != js_class_id(b)) return false;
    bool af = false;
    bool bf = false;
    Item av = js_map_shape_lookup_ext(a.map, "__primitiveValue__", 18, &af);
    Item bv = js_map_shape_lookup_ext(b.map, "__primitiveValue__", 18, &bf);
    if (af != bf) return false;
    if (af) {
        Item r = js_util_isDeepEqual_impl(av, bv, ctx, strict);
        if (!js_is_truthy(r)) return false;
    }
    return true;
}

static Item js_util_isDeepEqual_impl(Item a, Item b, JsDeepEqualContext* ctx, bool strict) {
    // Deep equality uses SameValueZero for NaN; loose mode additionally starts
    // primitive comparison with == before recursing into containers.
    if (js_util_is_nan_number(a) && js_util_is_nan_number(b)) {
        return (Item){.item = b2it(true)};
    }
    if (strict && js_util_strict_zero_sign_differs(a, b)) {
        // Node deepStrictEqual uses SameValue for numeric primitives; `===`
        // collapses +0 and -0 too early for assert's signed-zero cases.
        return (Item){.item = b2it(false)};
    }
    TypeId ta = js_util_deep_dispatch_type(a);
    TypeId tb = js_util_deep_dispatch_type(b);
    bool a_object_like = js_util_deep_equal_is_object_like_type(ta);
    bool b_object_like = js_util_deep_equal_is_object_like_type(tb);
    if (!strict && a_object_like != b_object_like) {
        // Legacy assert.deepEqual is loose for primitive leaves, but it must
        // not let JS ToPrimitive make arrays/objects equal to primitives.
        return (Item){.item = b2it(false)};
    }
    if (!strict && ta == LMD_TYPE_MAP && tb == LMD_TYPE_MAP) {
        JsClass class_a = js_util_deep_dispatch_class(a);
        JsClass class_b = js_util_deep_dispatch_class(b);
        if (js_util_is_boxed_primitive(a) != js_util_is_boxed_primitive(b)) {
            // Boxed primitives are branded values in loose deep equality; their
            // enumerable index-like fields must not make them equal plain maps.
            return (Item){.item = b2it(false)};
        }
        if (class_a != class_b && (class_a != JS_CLASS_NONE || class_b != JS_CLASS_NONE)) {
            bool a_byte_view = js_is_typed_array(a) || js_util_is_buffer_value(a);
            bool b_byte_view = js_is_typed_array(b) || js_util_is_buffer_value(b);
            JsTypedArray* atyped = a_byte_view ? js_get_typed_array_ptr(a.map) : NULL;
            JsTypedArray* btyped = b_byte_view ? js_get_typed_array_ptr(b.map) : NULL;
            if (!a_byte_view || !b_byte_view ||
                    !js_util_typed_array_loose_compatible(atyped, btyped)) {
                // Loose deep equality ignores prototypes for ordinary objects,
                // but branded built-ins are not interchangeable except
                // Buffer/Uint8Array byte views, which Node compares by bytes.
                return (Item){.item = b2it(false)};
            }
        }
    }
    if (a.item == b.item) return (Item){.item = b2it(true)};
    bool a_arguments = js_util_is_arguments_deep_value(a);
    bool b_arguments = js_util_is_arguments_deep_value(b);
    if (a_arguments != b_arguments) {
        // Arguments is array-backed internally in LambdaJS, but Node deep
        // equality treats it as a distinct exotic object from arrays/maps.
        return (Item){.item = b2it(false)};
    }
    if (strict || (!a_object_like && !b_object_like)) {
        Item eq = strict ? js_strict_equal(a, b) : js_equal(a, b);
        if (js_is_truthy(eq)) return (Item){.item = b2it(true)};
    }

    if (ta != tb && (strict || !a_object_like || !b_object_like)) {
        return (Item){.item = b2it(false)};
    }

    if (ta == LMD_TYPE_MAP &&
        (js_util_is_host_singleton_object(a) || js_util_is_host_singleton_object(b))) {
        return (Item){.item = b2it(false)};
    }

    bool compare_object_like = a_object_like && b_object_like;
    if (compare_object_like) {
        int enter_status = js_util_deep_equal_enter(ctx, a, b);
        if (enter_status == 0) {
            // cyclic containers re-enter the same active pair; treating it as equal prevents unbounded recursion.
            return (Item){.item = b2it(true)};
        }
        if (enter_status < 0) {
            // a cycle that remaps one side to a different counterpart is not structurally consistent.
            return (Item){.item = b2it(false)};
        }
    }

    bool a_dataview = js_is_dataview(a);
    bool b_dataview = js_is_dataview(b);
    if (a_dataview || b_dataview) {
        bool equal = a_dataview && b_dataview &&
                     js_util_dataview_bytes_equal(a, b) &&
                     js_util_compare_enumerable_properties(a, b, ctx, strict);
        if (compare_object_like) js_util_deep_equal_leave(ctx);
        return (Item){.item = b2it(equal)};
    }

    bool a_typed_array = js_is_typed_array(a) || js_util_is_buffer_value(a);
    bool b_typed_array = js_is_typed_array(b) || js_util_is_buffer_value(b);
    if (a_typed_array || b_typed_array) {
        if (!a_typed_array || !b_typed_array) {
            // Buffer instances are byte views too; missing this branch lets a
            // fake object with copied index keys compare as a plain object.
            if (compare_object_like) js_util_deep_equal_leave(ctx);
            return (Item){.item = b2it(false)};
        }
        JsTypedArray* atyped = js_get_typed_array_ptr(a.map);
        JsTypedArray* btyped = js_get_typed_array_ptr(b.map);
        if (!atyped || !btyped) {
            // Borrowed TypedArray/Buffer prototypes do not carry native backing
            // storage; comparing them as byte views makes fake object types pass.
            if (compare_object_like) js_util_deep_equal_leave(ctx);
            return (Item){.item = b2it(false)};
        }
        if (strict && atyped && btyped && atyped->is_buffer != btyped->is_buffer) {
            // Buffer shares Uint8Array storage, but strict deep equality must keep the distinct Buffer prototype visible.
            if (compare_object_like) js_util_deep_equal_leave(ctx);
            return (Item){.item = b2it(false)};
        }
        bool equal = strict ? js_util_typed_array_bytes_equal(a, b) : false;
        if (!strict) {
            int alen = js_typed_array_byte_length(a);
            int blen = js_typed_array_byte_length(b);
            void* adata = js_typed_array_current_data_ptr(a);
            void* bdata = js_typed_array_current_data_ptr(b);
            equal = js_util_typed_array_loose_compatible(atyped, btyped) &&
                alen == blen && (alen == 0 || (adata && bdata && memcmp(adata, bdata, (size_t)alen) == 0));
        }
        // Typed-array bytes are only the indexed storage; enumerable custom and
        // symbol properties still participate in Node deep equality.
        if (equal) equal = js_util_compare_enumerable_properties(a, b, ctx, strict);
        if (compare_object_like) js_util_deep_equal_leave(ctx);
        return (Item){.item = b2it(equal)};
    }

    bool a_arraybuffer = js_is_arraybuffer(a);
    bool b_arraybuffer = js_is_arraybuffer(b);
    if (a_arraybuffer || b_arraybuffer) {
        bool equal = a_arraybuffer && b_arraybuffer &&
                     js_util_arraybuffer_bytes_equal(a, b) &&
                     js_util_compare_enumerable_properties(a, b, ctx, strict);
        if (compare_object_like) js_util_deep_equal_leave(ctx);
        return (Item){.item = b2it(equal)};
    }

    if (ta == LMD_TYPE_MAP && tb == LMD_TYPE_MAP &&
        (js_util_deep_dispatch_class(a) == JS_CLASS_DATE ||
         js_util_deep_dispatch_class(b) == JS_CLASS_DATE)) {
        bool equal = js_util_date_time_equal(a, b) &&
                     js_util_compare_enumerable_properties(a, b, ctx, strict);
        if (compare_object_like) js_util_deep_equal_leave(ctx);
        return (Item){.item = b2it(equal)};
    }

    if (ta == LMD_TYPE_MAP && tb == LMD_TYPE_MAP &&
        (js_util_is_regexp_like_value(a) || js_util_is_regexp_like_value(b))) {
        // Borrowed RegExp prototypes/tags do not provide RegExp internal slots;
        // treating those fakes as plain objects lets distinct object types pass.
        bool equal = js_util_regexp_slots_equal(a, b, ctx, strict) &&
                     js_util_compare_enumerable_properties(a, b, ctx, strict);
        if (compare_object_like) js_util_deep_equal_leave(ctx);
        return (Item){.item = b2it(equal)};
    }

    if (js_util_is_weak_collection(a) || js_util_is_weak_collection(b)) {
        // Weak collections have unobservable entries; only object identity can
        // prove equality, and identity returned before this branch.
        if (compare_object_like) js_util_deep_equal_leave(ctx);
        return (Item){.item = b2it(false)};
    }

    if (js_util_is_boxed_primitive(a) || js_util_is_boxed_primitive(b)) {
        bool equal = js_util_boxed_primitive_equal(a, b, ctx, strict) &&
                     js_util_compare_enumerable_properties(a, b, ctx, strict);
        if (compare_object_like) js_util_deep_equal_leave(ctx);
        return (Item){.item = b2it(equal)};
    }

    if (ta == LMD_TYPE_MAP && tb == LMD_TYPE_MAP &&
        (js_util_is_url_like_value(a) || js_util_is_url_like_value(b))) {
        // URL wrappers rebuild nested searchParams methods per instance; href is the canonical structural value.
        bool equal = js_class_id(a) == JS_CLASS_URL && js_class_id(b) == JS_CLASS_URL &&
                     js_util_url_href_equal(a, b) &&
                     js_util_compare_enumerable_properties(a, b, ctx, strict);
        if (compare_object_like) js_util_deep_equal_leave(ctx);
        return (Item){.item = b2it(equal)};
    }

    if (ta == LMD_TYPE_ARRAY) {
        if (tb != LMD_TYPE_ARRAY) {
            js_util_deep_equal_leave(ctx);
            return (Item){.item = b2it(false)};
        }
        // Proxies dispatch by target type, but key collection still goes
        // through proxy traps so invalid ownKeys remains observable.
        int64_t la = js_util_array_length_for_deep(a);
        int64_t lb = js_util_array_length_for_deep(b);
        if (la != lb) {
            js_util_deep_equal_leave(ctx);
                return (Item){.item = b2it(false)};
        }
        if (a_arguments && b_arguments) {
            for (int64_t i = 0; i < la; i++) {
                Item r = js_util_isDeepEqual_impl(
                    js_elements_get_int(a, i),
                    js_elements_get_int(b, i),
                    ctx, strict);
                if (!js_is_truthy(r)) {
                    js_util_deep_equal_leave(ctx);
                    return (Item){.item = b2it(false)};
                }
            }
            // Arguments indexed slots are stored in array backing; relying only
            // on Object.keys can miss changed parameter values after inspection.
        }
        // Array holes differ from present undefined elements; comparing the
        // enumerable own-key set preserves sparseness and custom properties.
        bool equal = js_util_compare_enumerable_properties(a, b, ctx, strict);
        js_util_deep_equal_leave(ctx);
        return (Item){.item = b2it(equal)};
    }

    if (ta == LMD_TYPE_MAP && tb == LMD_TYPE_MAP) {
        if (js_util_is_error_like_value(a) || js_util_is_error_like_value(b)) {
            if (!js_util_is_error_like_value(a) || !js_util_is_error_like_value(b)) {
                js_util_deep_equal_leave(ctx);
                return (Item){.item = b2it(false)};
            }
            JsClass class_a = js_util_deep_dispatch_class(a);
            JsClass class_b = js_util_deep_dispatch_class(b);
            if (strict && js_class_is_error_like(class_a) &&
                    js_class_is_error_like(class_b) && class_a != class_b) {
                js_util_deep_equal_leave(ctx);
                return (Item){.item = b2it(false)};
            }
            const char* keys[] = {"message", "cause", "errors", NULL};
            const int lens[] = {7, 5, 6, 0};
            for (int i = 0; keys[i]; i++) {
                bool ha = js_util_has_own_key(a, keys[i], lens[i]);
                bool hb = js_util_has_own_key(b, keys[i], lens[i]);
                if (ha != hb) {
                    js_util_deep_equal_leave(ctx);
                    return (Item){.item = b2it(false)};
                }
                if (ha) {
                    Item r = js_util_isDeepEqual_impl(
                        js_get_key_default(a, js_name_item(keys[i], lens[i])),
                        js_get_key_default(b, js_name_item(keys[i], lens[i])),
                        ctx, strict);
                    if (!js_is_truthy(r)) {
                        js_util_deep_equal_leave(ctx);
                        return (Item){.item = b2it(false)};
                    }
                }
            }
        }
        // Check if both are Set or Map collections
        bool a_set = js_is_set_instance(a), b_set = js_is_set_instance(b);
        bool a_map = js_is_map_instance(a), b_map = js_is_map_instance(b);
        if (a_set && b_set) {
            bool equal = js_util_compare_unordered_collection(a, b, ctx, strict, false) &&
                js_util_compare_enumerable_properties(a, b, ctx, strict);
            js_util_deep_equal_leave(ctx);
            return (Item){.item = b2it(equal)};
        }
        if (a_map && b_map) {
            bool equal = js_util_compare_unordered_collection(a, b, ctx, strict, true) &&
                js_util_compare_enumerable_properties(a, b, ctx, strict);
            js_util_deep_equal_leave(ctx);
            return (Item){.item = b2it(equal)};
        }
        if ((a_set && !b_set) || (!a_set && b_set) || (a_map && !b_map) || (!a_map && b_map)) {
            js_util_deep_equal_leave(ctx);
            return (Item){.item = b2it(false)};
        }
        // Plain object comparison must match enumerable own keys, not just
        // values read through possibly non-enumerable or inherited properties.
        bool equal = js_util_compare_enumerable_properties(a, b, ctx, strict);
        js_util_deep_equal_leave(ctx);
        return (Item){.item = b2it(equal)};
    }

    if (compare_object_like) js_util_deep_equal_leave(ctx);
    return (Item){.item = b2it(false)};
}

static Item js_util_is_deep_equal_mode(Item a, Item b, bool strict) {
    JsDeepEqualContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    return js_util_isDeepEqual_impl(a, b, &ctx, strict);
}
JS_FORWARD_ITEM(js_util_isDeepStrictEqual, (Item a, Item b), js_util_is_deep_equal_mode, (a, b, true))
JS_FORWARD_ITEM(js_util_isDeepEqual, (Item a, Item b), js_util_is_deep_equal_mode, (a, b, false))

// additional util.types.* functions
extern "C" Item js_util_types_isUint8Array(Item obj) {
    if (!js_is_typed_array(obj)) return (Item){.item = b2it(false)};
    Map* m = obj.map;
    JsTypedArray* ta = (JsTypedArray*)m->data;
    if (ta && ta->element_type == JS_TYPED_UINT8)
        return (Item){.item = b2it(true)};
    return (Item){.item = b2it(false)};
}

extern "C" Item js_util_types_isPromise(Item obj) {
    if (js_class_id(obj) == JS_CLASS_PROMISE) return (Item){.item = b2it(true)};
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    // also check for .then method (thenable duck-type)
    Item then = js_get_key_cstr(obj, "then");
    if (js_is_callable(then)) return (Item){.item = b2it(true)};
    return (Item){.item = b2it(false)};
}

extern "C" Item js_util_types_isNumber(Item obj) {
    TypeId t = get_type_id(obj);
    return (Item){.item = b2it(t == LMD_TYPE_INT || t == LMD_TYPE_FLOAT)};
}

#define JS_UTIL_TYPE_TEST_OBJ(name, expression) \
    extern "C" Item name(Item obj) { \
        return (Item){.item = b2it(expression)}; \
    }
JS_UTIL_TYPE_TEST_OBJ(js_util_types_isFunction, js_is_callable(obj))
JS_UTIL_TYPE_TEST_OBJ(js_util_types_isString, get_type_id(obj) == LMD_TYPE_STRING)
JS_UTIL_TYPE_TEST_OBJ(js_util_types_isBoolean, get_type_id(obj) == LMD_TYPE_BOOL)
JS_UTIL_TYPE_TEST_OBJ(js_util_types_isNull, get_type_id(obj) == LMD_TYPE_NULL)
#undef JS_UTIL_TYPE_TEST_OBJ


#define JS_UTIL_TYPE_TEST_OBJ(name, expression) \
    extern "C" Item name(Item obj) { \
        return (Item){.item = b2it(expression)}; \
    }
JS_UTIL_TYPE_TEST_OBJ(js_util_types_isUndefined, get_type_id(obj) == LMD_TYPE_UNDEFINED)
JS_UTIL_TYPE_TEST_OBJ(js_util_types_isNullOrUndefined,
    get_type_id(obj) == LMD_TYPE_NULL || get_type_id(obj) == LMD_TYPE_UNDEFINED)
JS_UTIL_TYPE_TEST_OBJ(js_util_types_isObject,
    get_type_id(obj) == LMD_TYPE_MAP || get_type_id(obj) == LMD_TYPE_ARRAY ||
    get_type_id(obj) == LMD_TYPE_FUNC)
JS_UTIL_TYPE_TEST_OBJ(js_util_types_isPrimitive,
    get_type_id(obj) == LMD_TYPE_NULL || get_type_id(obj) == LMD_TYPE_UNDEFINED ||
    get_type_id(obj) == LMD_TYPE_BOOL || get_type_id(obj) == LMD_TYPE_INT ||
    get_type_id(obj) == LMD_TYPE_FLOAT || get_type_id(obj) == LMD_TYPE_STRING)

#define JS_UTIL_TYPE_FORWARD(name, target) \
    extern "C" Item name(Item obj) { return target(obj); }
JS_UTIL_TYPE_FORWARD(js_util_types_isBuffer, js_buffer_isBuffer)

extern "C" Item js_util_types_isError(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    JsClass cls = js_class_id(obj);
    if (js_class_is_error_like(cls) || cls == JS_CLASS_DOM_EXCEPTION) {
        return (Item){.item = b2it(true)};
    }
    // check for .message + .stack
    Item msg = js_get_key_cstr(obj, "message");
    if (get_type_id(msg) == LMD_TYPE_STRING) return (Item){.item = b2it(true)};
    return (Item){.item = b2it(false)};
}

// ─── additional util.types.* functions ───────────────────────────────────────

#define JS_UTIL_TYPE_TEST(name, expression) \
    extern "C" Item name(Item obj) { \
        return (Item){.item = b2it(expression)}; \
    }
JS_UTIL_TYPE_TEST(js_util_types_isTypedArray, js_is_typed_array(obj))
JS_UTIL_TYPE_TEST(js_util_types_isArrayBuffer, js_is_arraybuffer(obj))
JS_UTIL_TYPE_TEST(js_util_types_isSharedArrayBuffer, js_is_sharedarraybuffer(obj))
JS_UTIL_TYPE_TEST(js_util_types_isAnyArrayBuffer,
    js_is_arraybuffer(obj) || js_is_sharedarraybuffer(obj))
JS_UTIL_TYPE_TEST(js_util_types_isDataView, js_class_id(obj) == JS_CLASS_DATA_VIEW)
JS_UTIL_TYPE_TEST(js_util_types_isWeakMap, js_class_id(obj) == JS_CLASS_WEAK_MAP)
JS_UTIL_TYPE_TEST(js_util_types_isWeakSet, js_class_id(obj) == JS_CLASS_WEAK_SET)
JS_UTIL_TYPE_TEST(js_util_types_isWeakRef, js_class_id(obj) == JS_CLASS_WEAK_REF)

static bool is_typed_array_type(Item obj, JsTypedArrayType target_type) {
    if (!js_is_typed_array(obj)) return false;
    Map* m = obj.map;
    JsTypedArray* ta = (JsTypedArray*)m->data;
    return ta && ta->element_type == target_type;
}

#define JS_UTIL_TYPED_ARRAY_TEST(name, type) \
    JS_UTIL_TYPE_TEST(name, is_typed_array_type(obj, type))
JS_UTIL_TYPED_ARRAY_TEST(js_util_types_isUint16Array, JS_TYPED_UINT16)
JS_UTIL_TYPED_ARRAY_TEST(js_util_types_isUint32Array, JS_TYPED_UINT32)
JS_UTIL_TYPED_ARRAY_TEST(js_util_types_isInt8Array, JS_TYPED_INT8)
JS_UTIL_TYPED_ARRAY_TEST(js_util_types_isInt16Array, JS_TYPED_INT16)
JS_UTIL_TYPED_ARRAY_TEST(js_util_types_isInt32Array, JS_TYPED_INT32)
JS_UTIL_TYPED_ARRAY_TEST(js_util_types_isFloat32Array, JS_TYPED_FLOAT32)
JS_UTIL_TYPED_ARRAY_TEST(js_util_types_isFloat16Array, JS_TYPED_FLOAT16)
JS_UTIL_TYPED_ARRAY_TEST(js_util_types_isFloat64Array, JS_TYPED_FLOAT64)
JS_UTIL_TYPED_ARRAY_TEST(js_util_types_isUint8ClampedArray, JS_TYPED_UINT8_CLAMPED)
#undef JS_UTIL_TYPED_ARRAY_TEST
JS_UTIL_TYPE_TEST(js_util_types_isNumberObject, js_class_id(obj) == JS_CLASS_NUMBER)
JS_UTIL_TYPE_TEST(js_util_types_isStringObject, js_class_id(obj) == JS_CLASS_STRING)
JS_UTIL_TYPE_TEST(js_util_types_isBooleanObject, js_class_id(obj) == JS_CLASS_BOOLEAN)
JS_UTIL_TYPE_TEST(js_util_types_isSymbolObject, js_class_id(obj) == JS_CLASS_SYMBOL)
JS_UTIL_TYPE_FORWARD(js_util_types_isNativeError, js_util_types_isError)

#define JS_UTIL_CLASS_TEST(name, expression) \
    JS_UTIL_TYPE_TEST(name, expression)
JS_UTIL_CLASS_TEST(js_util_types_isBoxedPrimitive,
    js_class_id(obj) == JS_CLASS_NUMBER || js_class_id(obj) == JS_CLASS_STRING ||
    js_class_id(obj) == JS_CLASS_BOOLEAN || js_class_id(obj) == JS_CLASS_SYMBOL ||
    js_class_id(obj) == JS_CLASS_BIGINT)
// these Node predicates have no native representation in this runtime
JS_UTIL_TYPE_TEST(js_util_types_isProxy, ((void)(obj), false))
JS_UTIL_TYPE_TEST(js_util_types_isExternal, ((void)(obj), false))

static Item js_util_types_function_flag(Item obj, uint32_t flag,
        bool exclude_generator) {
    if (get_type_id(obj) != LMD_TYPE_FUNC) return (Item){.item = b2it(false)};
    JsFunction* fn = (JsFunction*)obj.function;
    bool set = fn && (fn->flags & flag);
    if (exclude_generator) set = set && !(fn->flags & JS_UTIL_FUNC_FLAG_GENERATOR);
    return (Item){.item = b2it(set)};
}
JS_FORWARD_ITEM(js_util_types_isGeneratorFunction, (Item obj), js_util_types_function_flag, (obj, JS_UTIL_FUNC_FLAG_GENERATOR, false))

JS_UTIL_CLASS_TEST(js_util_types_isGeneratorObject, js_class_id(obj) == JS_CLASS_GENERATOR)
JS_FORWARD_ITEM(js_util_types_isAsyncFunction, (Item obj), js_util_types_function_flag, (obj, JS_UTIL_FUNC_FLAG_ASYNC, true))

JS_UTIL_CLASS_TEST(js_util_types_isMapIterator, js_class_id(obj) == JS_CLASS_MAP_ITERATOR)
JS_UTIL_CLASS_TEST(js_util_types_isSetIterator, js_class_id(obj) == JS_CLASS_SET_ITERATOR)
JS_UTIL_CLASS_TEST(js_util_types_isArgumentsObject, js_class_id(obj) == JS_CLASS_ARGUMENTS)

extern "C" Item js_util_types_isModuleNamespaceObject(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = ITEM_FALSE};
    Item marker = js_get_key_cstr(obj, "__vm_module_namespace__");
    return (Item){.item = b2it(get_type_id(marker) == LMD_TYPE_BOOL && it2b(marker))};
}

#undef JS_UTIL_CLASS_TEST
#undef JS_UTIL_TYPE_TEST_OBJ
#undef JS_UTIL_TYPE_TEST
#undef JS_UTIL_TYPE_FORWARD

extern "C" Item js_util_getCallSites(Item frame_count_item) {
    int64_t frame_count = 10;
    TypeId frame_count_type = get_type_id(frame_count_item);
    if (frame_count_type == LMD_TYPE_INT || frame_count_type == LMD_TYPE_INT64 ||
            frame_count_type == LMD_TYPE_FLOAT) {
        // JS numeric literals may arrive boxed as floats, so the explicit frame
        // count must be read before the default would fabricate extra frames.
        frame_count = (int64_t)it2d(frame_count_item);
    }
    if (frame_count < 1) frame_count = 1;
    if (frame_count > 200) frame_count = 200;

    Item global = js_get_global_this();
    Item filename = js_get_key_cstr(global, "__filename");
    Item argv = js_get_process_argv();
    if (get_type_id(argv) == LMD_TYPE_ARRAY && js_array_length(argv) > 1) {
        Item script = js_elements_get_int(argv, 1);
        if (get_type_id(script) == LMD_TYPE_STRING) {
            String* ss = it2s(script);
            if (ss && ss->len > 0) {
                // required helpers can overwrite global __filename; call-site
                // records for top-level tests must report the entry script.
                if (ss->chars[0] == '/') {
                    if (!js_util_set_real_filename(ss->chars, &filename)) filename = script;
                } else {
#ifndef _WIN32
                    char cwd[2048];
                    if (getcwd(cwd, sizeof(cwd))) {
                        char path[4096];
                        int len = snprintf(path, sizeof(path), "%s/%.*s",
                                           cwd, (int)ss->len, ss->chars);
                        if (len > 0 && len < (int)sizeof(path)) {
                            if (!js_util_set_real_filename(path, &filename)) {
                                filename = make_string_item(path, len);
                            }
                        }
                    }
#else
                    filename = script;
#endif
                }
            }
        }
    }
    if (get_type_id(filename) != LMD_TYPE_STRING) filename = make_string_item("<anonymous>");

    Item frames = js_array_new(0);
    for (int64_t i = 0; i < frame_count; i++) {
        Item frame = js_new_object();
        // util.getCallSites must not invoke Error.prepareStackTrace; it returns
        // frame records directly from the current script context.
        js_set_key_cstr(frame, "scriptName", filename);
        js_set_key_cstr(frame, "functionName", make_string_item(""));
        js_set_key_cstr(frame, "lineNumber", (Item){.item = i2it(1)});
        js_set_key_cstr(frame, "column", (Item){.item = i2it(1)});
        js_set_key_cstr(frame, "columnNumber", (Item){.item = i2it(1)});
        js_array_push(frames, frame);
    }
    return frames;
}

// ─── util.styleText(format, text) — ANSI color formatting ──────────────────
extern "C" Item js_util_styleText(Item format_item, Item text_item) {
    Item str = js_to_string(text_item);
    if (get_type_id(format_item) != LMD_TYPE_STRING) return str;
    String* fmt = it2s(format_item);
    if (!fmt || fmt->len == 0) return str;
    String* txt = it2s(str);
    if (!txt) return str;

    // map format names to ANSI codes
    const char* open = "";
    const char* close = "\033[0m";
    if (fmt->len == 4 && memcmp(fmt->chars, "bold", 4) == 0) { open = "\033[1m"; }
    else if (fmt->len == 9 && memcmp(fmt->chars, "underline", 9) == 0) { open = "\033[4m"; }
    else if (fmt->len == 6 && memcmp(fmt->chars, "italic", 6) == 0) { open = "\033[3m"; }
    else if (fmt->len == 3 && memcmp(fmt->chars, "red", 3) == 0) { open = "\033[31m"; }
    else if (fmt->len == 5 && memcmp(fmt->chars, "green", 5) == 0) { open = "\033[32m"; }
    else if (fmt->len == 6 && memcmp(fmt->chars, "yellow", 6) == 0) { open = "\033[33m"; }
    else if (fmt->len == 4 && memcmp(fmt->chars, "blue", 4) == 0) { open = "\033[34m"; }
    else if (fmt->len == 7 && memcmp(fmt->chars, "magenta", 7) == 0) { open = "\033[35m"; }
    else if (fmt->len == 4 && memcmp(fmt->chars, "cyan", 4) == 0) { open = "\033[36m"; }
    else if (fmt->len == 5 && memcmp(fmt->chars, "white", 5) == 0) { open = "\033[37m"; }
    else { return str; } // unknown format, return text as-is

    int open_len = (int)strlen(open);
    int close_len = (int)strlen(close);
    int total = open_len + (int)txt->len + close_len;
    char buf[4096];
    if (total >= (int)sizeof(buf)) total = (int)sizeof(buf) - 1;
    memcpy(buf, open, open_len);
    int txt_len = total - open_len - close_len;
    memcpy(buf + open_len, txt->chars, txt_len);
    memcpy(buf + open_len + txt_len, close, close_len);
    buf[total] = '\0';
    return js_name_item(buf, total);
}

// ─── util.getSystemErrorName(errno) — errno to name ─────────────────────────
extern "C" Item js_util_getSystemErrorName(Item err_item) {
    // convert to number first
    Item num = js_to_number(err_item);
    int err_code = 0;
    TypeId t = get_type_id(num);
    if (t == LMD_TYPE_INT) err_code = (int)it2i(num);
    else if (t == LMD_TYPE_FLOAT) err_code = (int)it2d(num);
    int err = -err_code; // Node.js passes negative errno values
    if (err <= 0) err = (int)err_code; // handle positive values too
    const char* name = NULL;
    switch (err) {
        case 1: name = "EPERM"; break;
        case 2: name = "ENOENT"; break;
        case 3: name = "ESRCH"; break;
        case 4: name = "EINTR"; break;
        case 5: name = "EIO"; break;
        case 9: name = "EBADF"; break;
        case 11: name = "EAGAIN"; break;
        case 12: name = "ENOMEM"; break;
        case 13: name = "EACCES"; break;
        case 14: name = "EFAULT"; break;
        case 17: name = "EEXIST"; break;
        case 20: name = "ENOTDIR"; break;
        case 21: name = "EISDIR"; break;
        case 22: name = "EINVAL"; break;
        case 24: name = "EMFILE"; break;
        case 28: name = "ENOSPC"; break;
        case 32: name = "EPIPE"; break;
        case 36: name = "ENAMETOOLONG"; break;
        case 38: name = "ENOSYS"; break;
        case 39: name = "ENOTEMPTY"; break;
        case 40: name = "ELOOP"; break;
        case 61: name = "ENODATA"; break;
        case 95: name = "ENOTSUP"; break;
        case 98: name = "EADDRINUSE"; break;
        case 99: name = "EADDRNOTAVAIL"; break;
        case 104: name = "ECONNRESET"; break;
#if defined(ETIMEDOUT) && ETIMEDOUT != 110
        case ETIMEDOUT: name = "ETIMEDOUT"; break;
#endif
        case 110: name = "ETIMEDOUT"; break;
        case 111: name = "ECONNREFUSED"; break;
        default: name = "UNKNOWN"; break;
    }
    return js_name_item(name, strlen(name));
}

// ─── util.debuglog(section) ─────────────────────────────────────────────────
// Returns a logging function gated by NODE_DEBUG environment variable.
// If NODE_DEBUG contains the section name (case-insensitive), the returned
// function logs to stderr. Otherwise it's a no-op.
static Item js_debuglog_noop(Item args_rest) {
    (void)args_rest;
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}


static Item js_debuglog_active(Item args_rest) {
    // simple: log first argument to stderr
    if (args_rest.item != 0 && get_type_id(args_rest) != LMD_TYPE_UNDEFINED) {
        int64_t argc = js_array_length(args_rest);
        if (argc > 0) {
            Item arg0 = js_elements_get_int(args_rest, 0);
            Item str = js_json_stringify(arg0);
            if (get_type_id(str) == LMD_TYPE_STRING) {
                String* s = it2s(str);
                if (s) fprintf(stderr, "%.*s\n", (int)s->len, s->chars); // PRINTF_OK: implements Node.js util.debuglog stderr write.
            }
        }
    }
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

extern "C" Item js_util_debuglog(Item section) {
    if (get_type_id(section) != LMD_TYPE_STRING) {
        return js_new_native_rest_function(js_debuglog_noop);
    }
    String* sec = it2s(section);
    if (!sec || sec->len == 0) {
        return js_new_native_rest_function(js_debuglog_noop);
    }

    // check NODE_DEBUG env var
    const char* node_debug = getenv("NODE_DEBUG");
    if (!node_debug) {
        return js_new_native_rest_function(js_debuglog_noop);
    }

    // case-insensitive search for section name in NODE_DEBUG
    // NODE_DEBUG can be comma or space separated
    char sec_upper[128];
    int slen = (int)sec->len < 127 ? (int)sec->len : 127;
    for (int i = 0; i < slen; i++) {
        char c = sec->chars[i];
        sec_upper[i] = (c >= 'a' && c <= 'z') ? (c - 32) : c;
    }
    sec_upper[slen] = '\0';

    char debug_upper[1024];
    int dlen = (int)strlen(node_debug);
    if (dlen > 1023) dlen = 1023;
    for (int i = 0; i < dlen; i++) {
        char c = node_debug[i];
        debug_upper[i] = (c >= 'a' && c <= 'z') ? (c - 32) : c;
    }
    debug_upper[dlen] = '\0';

    if (strstr(debug_upper, sec_upper)) {
        return js_new_native_rest_function(js_debuglog_active);
    }

    return js_new_native_rest_function(js_debuglog_noop);
}

// util.stripVTControlCharacters(str)
// Strips ANSI escape codes from a string
static Item js_util_stripVTControlCharacters(Item str) {
    if (get_type_id(str) != LMD_TYPE_STRING) {
        return js_throw_invalid_arg_type("str", "string", str);
    }
    String* s = it2s(str);
    if (!s || s->len == 0) return str;

    // allocate worst case (same size as input)
    char* buf = (char*)alloca(s->len + 1);
    int out = 0;
    for (int i = 0; i < (int)s->len; i++) {
        unsigned char c = (unsigned char)s->chars[i];
        if (c == 0x1b) {
            if (i + 1 >= (int)s->len) continue;
            unsigned char next = (unsigned char)s->chars[i + 1];
            if (next == '[') {
                i += 2;
                while (i < (int)s->len) {
                    unsigned char cc = (unsigned char)s->chars[i];
                    if (cc >= 0x40 && cc <= 0x7e) break;
                    i++;
                }
                continue;
            }
            if (next == ']') {
                i += 2;
                while (i < (int)s->len) {
                    unsigned char cc = (unsigned char)s->chars[i];
                    if (cc == 0x07 || cc == 0x9c) break;
                    if (cc == 0x1b && i + 1 < (int)s->len && s->chars[i + 1] == '\\') {
                        i++;
                        break;
                    }
                    i++;
                }
                continue;
            }
            i++;
            continue;
        }
        if (c == 0x9b) {
            i++;
            while (i < (int)s->len) {
                unsigned char cc = (unsigned char)s->chars[i];
                if (cc >= 0x40 && cc <= 0x7e) break;
                i++;
            }
            continue;
        }
        if (c == 0x9d) {
            i++;
            while (i < (int)s->len) {
                unsigned char cc = (unsigned char)s->chars[i];
                if (cc == 0x07 || cc == 0x9c) break;
                if (cc == 0x1b && i + 1 < (int)s->len && s->chars[i + 1] == '\\') {
                    i++;
                    break;
                }
                i++;
            }
            continue;
        }
        if (c == 0x9c) continue;
        buf[out++] = s->chars[i];
    }
    return js_name_item(buf, out);
}

// util._extend(target, source) — deprecated Object.assign equivalent
static Item js_util_extend(Item target, Item source) {
    if (get_type_id(source) == LMD_TYPE_MAP) {
        Item keys = js_object_keys(source);
        int64_t len = js_array_length(keys);
        for (int64_t i = 0; i < len; i++) {
            Item key = js_elements_get_int(keys, i);
            js_set_key_default(target, key, js_get_key_default(source, key));
        }
    }
    return target;
}

// =============================================================================
// util Module Namespace Object
// =============================================================================

#define util_namespace (js_runtime_state.util.namespace_object)
JS_FORWARD_STATIC_EXPRESSION(bool, util_ensure_roots, (void), (js_active_runtime_state && js_root_range_ensure_registered(&js_runtime_state.util.roots)))

template <typename Target>
JS_FORWARD_STATIC_VOID( js_util_set_method, (Item ns, const char* name, Target target,         int adapter_arity), js_install_native_method, (ns, name, target, adapter_arity))

typedef Item (*JsUtilTypeMethod)(Item);
typedef struct JsUtilTypeMethodSpec {
    const char* name;
    JsUtilTypeMethod target;
} JsUtilTypeMethodSpec;

static const JsUtilTypeMethodSpec js_util_basic_type_methods[] = {
    {"isDate", js_util_types_isDate}, {"isRegExp", js_util_types_isRegExp},
    {"isArray", js_util_types_isArray}, {"isMap", js_util_types_isMap},
    {"isSet", js_util_types_isSet}, {"isUint8Array", js_util_types_isUint8Array},
    {"isPromise", js_util_types_isPromise}, {"isFunction", js_util_types_isFunction},
    {"isString", js_util_types_isString}, {"isNumber", js_util_types_isNumber},
    {"isBoolean", js_util_types_isBoolean}, {"isNull", js_util_types_isNull},
    {"isUndefined", js_util_types_isUndefined},
    {"isNullOrUndefined", js_util_types_isNullOrUndefined},
    {"isObject", js_util_types_isObject}, {"isPrimitive", js_util_types_isPrimitive},
    {"isBuffer", js_util_types_isBuffer}, {"isError", js_util_types_isError},
    {NULL, NULL}
};

static const JsUtilTypeMethodSpec js_util_extended_type_methods[] = {
    {"isTypedArray", js_util_types_isTypedArray}, {"isArrayBuffer", js_util_types_isArrayBuffer},
    {"isSharedArrayBuffer", js_util_types_isSharedArrayBuffer},
    {"isAnyArrayBuffer", js_util_types_isAnyArrayBuffer}, {"isDataView", js_util_types_isDataView},
    {"isWeakMap", js_util_types_isWeakMap}, {"isWeakSet", js_util_types_isWeakSet},
    {"isWeakRef", js_util_types_isWeakRef}, {"isUint16Array", js_util_types_isUint16Array},
    {"isUint32Array", js_util_types_isUint32Array}, {"isInt8Array", js_util_types_isInt8Array},
    {"isInt16Array", js_util_types_isInt16Array}, {"isInt32Array", js_util_types_isInt32Array},
    {"isFloat16Array", js_util_types_isFloat16Array}, {"isFloat32Array", js_util_types_isFloat32Array},
    {"isFloat64Array", js_util_types_isFloat64Array},
    {"isUint8ClampedArray", js_util_types_isUint8ClampedArray},
    {"isNumberObject", js_util_types_isNumberObject}, {"isStringObject", js_util_types_isStringObject},
    {"isBooleanObject", js_util_types_isBooleanObject}, {"isSymbolObject", js_util_types_isSymbolObject},
    {"isNativeError", js_util_types_isNativeError}, {"isBoxedPrimitive", js_util_types_isBoxedPrimitive},
    {"isProxy", js_util_types_isProxy}, {"isExternal", js_util_types_isExternal},
    {"isGeneratorFunction", js_util_types_isGeneratorFunction},
    {"isGeneratorObject", js_util_types_isGeneratorObject},
    {"isAsyncFunction", js_util_types_isAsyncFunction}, {"isMapIterator", js_util_types_isMapIterator},
    {"isSetIterator", js_util_types_isSetIterator}, {"isArgumentsObject", js_util_types_isArgumentsObject},
    {"isModuleNamespaceObject", js_util_types_isModuleNamespaceObject}, {NULL, NULL}
};

extern "C" Item js_get_util_namespace(void) {
    if (!util_ensure_roots()) return ItemError;
    // The console hook is valid for the process lifetime and has no Item
    // ownership; node-core activates this namespace at session attach so the
    // Node profile retains util.format before the first console call.
    js_host_hooks_set_console_format_hook(js_util_format);
    if (util_namespace.item != 0) return util_namespace;

    // The namespace escapes this helper and is not published in the module
    // cache until construction completes, so its stable cache slot owns it.
    util_namespace = js_new_object();

    JS_ROOTS(roots,
        inspect_root, ItemNull,
        custom_symbol_root, ItemNull,
        default_options_root, ItemNull,
        promisify_root, ItemNull,
        types_root, ItemNull,
        text_encoder_root, ItemNull,
        text_decoder_root, ItemNull,
        default_key_root, ItemNull,
        temporary_root, ItemNull);

    js_util_set_method(util_namespace, "format",              js_util_format, -1);
    js_util_set_method(util_namespace, "inspect",             js_util_inspect, 2);
    // Set util.inspect.custom = Symbol.for('nodejs.util.inspect.custom')
    {
        inspect_root.set(js_get_key_cstr(util_namespace, "inspect"));
        custom_symbol_root.set(js_symbol_for(make_string_item("nodejs.util.inspect.custom")));
        js_set_key_cstr(inspect_root.get(), "custom", custom_symbol_root.get());
        default_options_root.set(js_new_object());
        // util.inspect.defaultOptions is observable and must stay separate from
        // REPL-local writer options so enabling REPL colors does not leak global state.
        js_set_key_cstr(default_options_root.get(), "colors", (Item){.item = b2it(false)});
        js_set_key_cstr(inspect_root.get(), "defaultOptions", default_options_root.get());
    }
    js_util_set_method(util_namespace, "promisify",           js_util_promisify, 1);
    {
        promisify_root.set(js_get_key_cstr(util_namespace, "promisify"));
        temporary_root.set(js_util_promisify_custom_symbol());
        js_set_key_cstr(promisify_root.get(), "custom", temporary_root.get());
    }
    js_util_set_method(util_namespace, "callbackify",         js_util_callbackify, 1);
    js_util_set_method(util_namespace, "aborted",             js_util_aborted, 2);
    js_util_set_method(util_namespace, "deprecate",           js_util_deprecate, 2);
    js_util_set_method(util_namespace, "inherits",            js_util_inherits, 2);
    js_util_set_method(util_namespace, "isDeepStrictEqual",   js_util_isDeepStrictEqual, 2);
    js_util_set_method(util_namespace, "debuglog",            js_util_debuglog, 1);
    js_util_set_method(util_namespace, "styleText",           js_util_styleText, 2);
    js_util_set_method(util_namespace, "getSystemErrorName",  js_util_getSystemErrorName, 1);
    js_util_set_method(util_namespace, "stripVTControlCharacters", js_util_stripVTControlCharacters, 1);
    js_util_set_method(util_namespace, "_extend",             js_util_extend, 2);
    // util.debug is an alias for util.debuglog
    js_util_set_method(util_namespace, "debug",               js_util_debuglog, 1);

    // util.types sub-namespace
    types_root.set(js_new_object());
    for (const JsUtilTypeMethodSpec* spec = js_util_basic_type_methods;
            spec->name; spec++) {
        js_util_set_method(types_root.get(), spec->name, spec->target, 1);
    }
    // additional types checks
    Item types = types_root.get();
    for (const JsUtilTypeMethodSpec* spec = js_util_extended_type_methods;
            spec->name; spec++) {
        js_util_set_method(types, spec->name, spec->target, 1);
    }
    js_set_key_cstr(util_namespace, "types", types_root.get());
    js_util_set_method(util_namespace, "getCallSites", js_util_getCallSites, 1);

    // TextEncoder/TextDecoder — expose constructors on util namespace
    text_encoder_root.set(js_new_native_function(js_text_encoder_new));
    text_decoder_root.set(js_new_native_function(js_text_decoder_new));
    js_set_key_cstr(util_namespace, "TextEncoder", text_encoder_root.get());
    js_set_key_cstr(util_namespace, "TextDecoder", text_decoder_root.get());

    // default export
    default_key_root.set(make_string_item("default"));
    js_set_key_default(util_namespace, default_key_root.get(), util_namespace);

    return util_namespace;
}

extern "C" void js_util_reset(void) {
    if (!js_active_runtime_state) return;
    util_namespace = (Item){0};
    js_host_hooks_set_console_format_hook(NULL);
}
