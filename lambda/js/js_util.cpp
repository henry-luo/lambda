/**
 * js_util.cpp — Node.js-style 'util' module for LambdaJS
 *
 * Provides utility functions: format, inspect, types, promisify.
 * Registered as built-in module 'util' via js_module_get().
 */
#include "js_runtime.h"
#include "js_typed_array.h"
#include "js_class.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/strbuf.h"

#include <cstring>
#include <cmath>
#include <cstdio>

// forward declarations
extern "C" Item js_util_inspect(Item obj_item, Item options_item);
extern "C" Item js_symbol_for(Item desc);
extern "C" Item js_process_emit(Item event_name, Item arg1);

struct JsUtilFunctionView {
    TypeId type_id;
    void* func_ptr;
    int param_count;
    Item* env;
    int env_size;
    Item prototype;
    Item bound_this;
    Item* bound_args;
    int bound_argc;
    String* name;
    int builtin_id;
    Item properties_map;
    uint16_t flags;
};

#define JS_UTIL_FUNC_FLAG_GENERATOR 1
#define JS_UTIL_FUNC_FLAG_ASYNC 128

// Helper: make JS undefined
static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

static Item make_string_item(const char* str, int len) {
    if (!str) return ItemNull;
    String* s = heap_create_name(str, (size_t)len);
    return (Item){.item = s2it(s)};
}

static Item make_string_item(const char* str) {
    if (!str) return ItemNull;
    return make_string_item(str, (int)strlen(str));
}

// =============================================================================
// util.format(...args) — simplified printf-style formatting
// =============================================================================

extern "C" Item js_util_format(Item args_item) {
    if (get_type_id(args_item) != LMD_TYPE_ARRAY) return make_string_item("");

    int argc = (int)js_array_length(args_item);
    if (argc == 0) return make_string_item("");

    // first arg is the format string (or the only value)
    Item first = js_array_get_int(args_item, 0);
    if (get_type_id(first) != LMD_TYPE_STRING) {
        // first arg is not a string — inspect all args and join with space
        char buf[8192];
        int pos = 0;
        for (int i = 0; i < argc; i++) {
            if (i > 0 && pos < (int)sizeof(buf) - 1) buf[pos++] = ' ';
            Item val = js_array_get_int(args_item, i);
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
            if (get_type_id(str) == LMD_TYPE_STRING) {
                String* s = it2s(str);
                int clen = (int)s->len;
                if (pos + clen >= (int)sizeof(buf)) clen = (int)sizeof(buf) - 1 - pos;
                memcpy(buf + pos, s->chars, clen);
                pos += clen;
            }
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

            Item arg = js_array_get_int(args_item, arg_idx);
            i++; // skip past specifier

            switch (spec) {
                case 's': {
                    // Symbol needs special handling (js_to_string throws for Symbol)
                    TypeId at = get_type_id(arg);
                    if (at == LMD_TYPE_INT && it2i(arg) <= -(int64_t)JS_SYMBOL_BASE) {
                        Item sym_str = js_symbol_to_string(arg);
                        if (get_type_id(sym_str) == LMD_TYPE_STRING) {
                            String* s = it2s(sym_str);
                            int clen = (int)s->len;
                            if (pos + clen >= (int)sizeof(buf)) clen = (int)sizeof(buf) - 1 - pos;
                            memcpy(buf + pos, s->chars, clen);
                            pos += clen;
                        }
                    } else {
                        Item str = js_to_string(arg);
                        if (get_type_id(str) == LMD_TYPE_STRING) {
                            String* s = it2s(str);
                            int clen = (int)s->len;
                            if (pos + clen >= (int)sizeof(buf)) clen = (int)sizeof(buf) - 1 - pos;
                            memcpy(buf + pos, s->chars, clen);
                            pos += clen;
                        }
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
                        String* s = it2s(str);
                        int clen = (int)s->len;
                        if (pos + clen >= (int)sizeof(buf)) clen = (int)sizeof(buf) - 1 - pos;
                        memcpy(buf + pos, s->chars, clen);
                        pos += clen;
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
                    if (get_type_id(str) == LMD_TYPE_STRING) {
                        String* s = it2s(str);
                        int clen = (int)s->len;
                        if (pos + clen >= (int)sizeof(buf)) clen = (int)sizeof(buf) - 1 - pos;
                        memcpy(buf + pos, s->chars, clen);
                        pos += clen;
                    }
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
        Item extra = js_array_get_int(args_item, arg_idx++);
        TypeId et = get_type_id(extra);
        Item str;
        if (et == LMD_TYPE_INT && it2i(extra) <= -(int64_t)JS_SYMBOL_BASE) {
            str = js_symbol_to_string(extra);
        } else if (et == LMD_TYPE_STRING) {
            str = extra; // strings appended raw, not inspected
        } else {
            str = js_util_inspect(extra, make_js_undefined());
        }
        if (get_type_id(str) == LMD_TYPE_STRING) {
            String* s = it2s(str);
            int clen = (int)s->len;
            if (pos + clen >= (int)sizeof(buf)) clen = (int)sizeof(buf) - 1 - pos;
            memcpy(buf + pos, s->chars, clen);
            pos += clen;
        }
    }

    buf[pos] = '\0';
    return make_string_item(buf, pos);
}

// =============================================================================
// util.inspect(obj[, options]) — basic inspect for debugging
// =============================================================================

extern "C" Item js_util_inspect(Item obj_item, Item options_item) {
    (void)options_item;

    // handle values that JSON.stringify doesn't represent correctly
    TypeId tid = get_type_id(obj_item);
    if (tid == LMD_TYPE_FLOAT) {
        double d = it2d(obj_item);
        if (d != d) return make_string_item("NaN");
        if (d == 1.0/0.0) return make_string_item("Infinity");
        if (d == -1.0/0.0) return make_string_item("-Infinity");
    }
    if (tid == LMD_TYPE_UNDEFINED) return make_string_item("undefined");
    if (tid == LMD_TYPE_NULL) return make_string_item("null");

    // Strings: wrap in single quotes (Node.js style)
    if (tid == LMD_TYPE_STRING) {
        String* s = it2s(obj_item);
        if (!s) return make_string_item("''");
        // build 'value' with single quotes
        int len = (int)s->len;
        char* buf = (char*)mem_alloc(len + 3, MEM_CAT_JS_RUNTIME);
        buf[0] = '\'';
        memcpy(buf + 1, s->chars, len);
        buf[len + 1] = '\'';
        buf[len + 2] = '\0';
        Item result = make_string_item(buf, len + 2);
        mem_free(buf);
        return result;
    }

    // Objects: Node.js-style inspect { key: value, ... }
    if (tid == LMD_TYPE_MAP) {
        // Check for custom inspect function: Symbol.for('nodejs.util.inspect.custom')
        extern Item js_symbol_for(Item desc);
        extern int js_check_exception(void);
        extern Item js_clear_exception(void);
        Item custom_sym = js_symbol_for(make_string_item("nodejs.util.inspect.custom"));
        Item custom_fn = js_property_get(obj_item, custom_sym);
        if (get_type_id(custom_fn) == LMD_TYPE_FUNC) {
            Item result = js_call_function(custom_fn, obj_item, nullptr, 0);
            if (js_check_exception()) {
                js_clear_exception();
                return make_string_item("[object Object]");
            }
            if (get_type_id(result) == LMD_TYPE_STRING) return result;
            return js_to_string(result);
        }

        extern Item js_object_keys(Item obj);
        Item keys = js_object_keys(obj_item);
        int64_t klen = js_array_length(keys);
        if (klen == 0) return make_string_item("{}");
        // build "{ key1: val1, key2: val2 }"
        // estimate buffer size
        int cap = 256;
        char* buf = (char*)mem_alloc(cap, MEM_CAT_JS_RUNTIME);
        int pos = 0;
        buf[pos++] = '{';
        buf[pos++] = ' ';
        for (int64_t i = 0; i < klen; i++) {
            if (i > 0) {
                buf[pos++] = ',';
                buf[pos++] = ' ';
            }
            Item key = js_array_get_int(keys, i);
            String* ks = it2s(key);
            Item val = js_property_get(obj_item, key);
            Item val_str = js_util_inspect(val, make_js_undefined());
            String* vs = it2s(val_str);
            int need = pos + (ks ? (int)ks->len : 0) + 2 + (vs ? (int)vs->len : 0) + 4;
            if (need >= cap) {
                cap = need * 2;
                buf = (char*)mem_realloc(buf, cap, MEM_CAT_JS_RUNTIME);
            }
            if (ks) { memcpy(buf + pos, ks->chars, ks->len); pos += (int)ks->len; }
            buf[pos++] = ':';
            buf[pos++] = ' ';
            if (vs) { memcpy(buf + pos, vs->chars, vs->len); pos += (int)vs->len; }
        }
        buf[pos++] = ' ';
        buf[pos++] = '}';
        buf[pos] = '\0';
        Item r = make_string_item(buf, pos);
        mem_free(buf);
        return r;
    }

    // Arrays: Node.js-style inspect [ val1, val2, ... ]
    if (tid == LMD_TYPE_ARRAY) {
        int64_t alen = js_array_length(obj_item);
        if (alen == 0) return make_string_item("[]");
        int cap = 256;
        char* buf = (char*)mem_alloc(cap, MEM_CAT_JS_RUNTIME);
        int pos = 0;
        buf[pos++] = '[';
        buf[pos++] = ' ';
        for (int64_t i = 0; i < alen; i++) {
            if (i > 0) {
                buf[pos++] = ',';
                buf[pos++] = ' ';
            }
            Item val = js_array_get_int(obj_item, i);
            Item val_str = js_util_inspect(val, make_js_undefined());
            String* vs = it2s(val_str);
            int need = pos + (vs ? (int)vs->len : 0) + 6;
            if (need >= cap) {
                cap = need * 2;
                buf = (char*)mem_realloc(buf, cap, MEM_CAT_JS_RUNTIME);
            }
            if (vs) { memcpy(buf + pos, vs->chars, vs->len); pos += (int)vs->len; }
        }
        buf[pos++] = ' ';
        buf[pos++] = ']';
        buf[pos] = '\0';
        Item r = make_string_item(buf, pos);
        mem_free(buf);
        return r;
    }

    // use JSON.stringify as a basic inspect
    Item result = js_json_stringify(obj_item);
    if (get_type_id(result) == LMD_TYPE_STRING) return result;

    // fallback for non-JSON-serializable values
    Item str = js_to_string(obj_item);
    return str;
}

// =============================================================================
// util.types — type checking namespace
// =============================================================================

extern "C" Item js_util_types_isDate(Item value) {
    return (Item){.item = b2it(js_class_id(value) == JS_CLASS_DATE)};
}

extern "C" Item js_util_types_isRegExp(Item value) {
    return (Item){.item = b2it(js_class_id(value) == JS_CLASS_REGEXP)};
}

extern "C" Item js_util_types_isArray(Item value) {
    return (Item){.item = b2it(get_type_id(value) == LMD_TYPE_ARRAY)};
}

extern "C" Item js_util_types_isMap(Item value) {
    return (Item){.item = b2it(js_class_id(value) == JS_CLASS_MAP)};
}

extern "C" Item js_util_types_isSet(Item value) {
    return (Item){.item = b2it(js_class_id(value) == JS_CLASS_SET)};
}

// =============================================================================
// util.promisify(original) — callback-last API to Promise wrapper
// =============================================================================

static Item js_util_promisify_result_from_args(Item custom_args, Item rest_args);
static bool js_util_is_promise_like(Item value);
static void js_util_emit_promisify_promise_warning(void);

static Item js_util_promisify_callback(Item env_item, Item rest_args) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();

    Item resolve = env[0];
    Item reject = env[1];
    Item custom_args = env[2];
    int64_t argc = js_array_length(rest_args);
    Item err = (argc > 0) ? js_array_get_int(rest_args, 0) : make_js_undefined();

    if (js_is_truthy(err)) {
        Item reject_args[1] = {err};
        js_call_function(reject, make_js_undefined(), reject_args, 1);
        return make_js_undefined();
    }

    Item value = js_util_promisify_result_from_args(custom_args, rest_args);
    Item resolve_args[1] = {value};
    js_call_function(resolve, make_js_undefined(), resolve_args, 1);
    return make_js_undefined();
}

static Item js_util_promisify_executor(Item env_item, Item resolve, Item reject) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();

    Item original = env[0];
    Item this_arg = env[1];
    Item call_args_array = env[2];
    Item custom_args = env[3];
    int64_t argc64 = js_array_length(call_args_array);
    if (argc64 < 0) argc64 = 0;

    Item* cb_env = js_alloc_env(3);
    cb_env[0] = resolve;
    cb_env[1] = reject;
    cb_env[2] = custom_args;
    Item callback = js_new_closure((void*)js_util_promisify_callback, -1, cb_env, 3);

    int argc = (int)argc64 + 1;
    Item* call_args = (Item*)alloca((size_t)argc * sizeof(Item));
    for (int i = 0; i < (int)argc64; i++) {
        call_args[i] = js_array_get_int(call_args_array, i);
    }
    call_args[argc - 1] = callback;

    Item call_result = js_call_function(original, this_arg, call_args, argc);
    if (js_check_exception()) {
        Item error = js_clear_exception();
        Item reject_args[1] = {error};
        js_call_function(reject, make_js_undefined(), reject_args, 1);
        if (js_check_exception()) js_clear_exception();
    } else if (js_util_is_promise_like(call_result)) {
        js_util_emit_promisify_promise_warning();
    }
    return make_js_undefined();
}

static Item js_util_promisified_function(Item env_item, Item rest_args) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return js_promise_reject(make_string_item("promisified function missing target"));

    Item original = env[0];
    Item custom_args = env[1];
    Item this_arg = js_get_this();

    Item call_args_array = js_array_new(0);
    int64_t argc = js_array_length(rest_args);
    for (int64_t i = 0; i < argc; i++) {
        js_array_push(call_args_array, js_array_get_int(rest_args, i));
    }

    Item* exec_env = js_alloc_env(4);
    exec_env[0] = original;
    exec_env[1] = this_arg;
    exec_env[2] = call_args_array;
    exec_env[3] = custom_args;
    Item executor = js_new_closure((void*)js_util_promisify_executor, 2, exec_env, 4);
    return js_promise_create(executor);
}

static Item js_util_promisify_custom_symbol(void) {
    return js_symbol_for(make_string_item("nodejs.util.promisify.custom"));
}

extern "C" Item js_util_custom_promisify_args_symbol(void) {
    return js_symbol_for(make_string_item("nodejs.util.promisify.customArgs"));
}

static bool js_util_is_promise_like(Item value) {
    if (js_class_id(value) == JS_CLASS_PROMISE) return true;
    TypeId type = get_type_id(value);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_FUNC) return false;
    Item then_fn = js_property_get(value, make_string_item("then"));
    if (js_check_exception()) {
        js_clear_exception();
        return false;
    }
    return get_type_id(then_fn) == LMD_TYPE_FUNC;
}

static void js_util_emit_promisify_promise_warning(void) {
    Item warning = js_new_object();
    js_property_set(warning, make_string_item("name"), make_string_item("DeprecationWarning"));
    js_property_set(warning, make_string_item("message"),
        make_string_item("Calling promisify on a function that returns a Promise is likely a mistake."));
    js_property_set(warning, make_string_item("code"), make_string_item("DEP0174"));
    js_process_emit(make_string_item("warning"), warning);
    if (js_check_exception()) js_clear_exception();
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
                Item name = js_array_get_int(custom_args, i);
                if (get_type_id(name) == LMD_TYPE_STRING) {
                    js_property_set(obj, name, js_array_get_int(rest_args, i + 1));
                }
            }
            return obj;
        }
    }

    return js_array_get_int(rest_args, 1);
}

extern "C" Item js_util_promisify(Item fn_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) {
        return js_throw_invalid_arg_type("original", "function", fn_item);
    }

    Item custom_key = js_util_promisify_custom_symbol();
    Item custom = js_property_get(fn_item, custom_key);
    if (js_check_exception()) return ItemNull;
    if (custom.item != ITEM_NULL && custom.item != ITEM_JS_UNDEFINED &&
        get_type_id(custom) != LMD_TYPE_UNDEFINED) {
        if (get_type_id(custom) != LMD_TYPE_FUNC) {
            return js_throw_invalid_arg_type("util.promisify.custom", "function", custom);
        }
        return custom;
    }

    Item custom_args = js_property_get(fn_item, js_util_custom_promisify_args_symbol());
    if (js_check_exception()) return ItemNull;

    Item* env = js_alloc_env(2);
    env[0] = fn_item;
    env[1] = custom_args;
    Item wrapper = js_new_closure((void*)js_util_promisified_function, -1, env, 2);
    js_property_set(wrapper, custom_key, wrapper);
    js_set_function_name(wrapper, make_string_item("promisified"));
    return wrapper;
}

// =============================================================================
// util.callbackify(original) — Promise API to callback-last wrapper
// =============================================================================

static Item js_util_callbackify_make_falsy_error(Item reason) {
    Item error = js_new_error(make_string_item("Promise was rejected with a falsy value"));
    js_property_set(error, make_string_item("code"), make_string_item("ERR_FALSY_VALUE_REJECTION"));
    js_property_set(error, make_string_item("reason"), reason);
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

    Item callback = js_array_get_int(rest_args, argc64 - 1);
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        return js_throw_invalid_arg_type("callback", "function", callback);
    }

    int argc = (int)argc64 - 1;
    Item* call_args = argc > 0 ? (Item*)alloca((size_t)argc * sizeof(Item)) : NULL;
    for (int i = 0; i < argc; i++) {
        call_args[i] = js_array_get_int(rest_args, i);
    }

    Item result = js_call_function(original, js_get_this(), call_args, argc);
    Item promise = ItemNull;
    if (js_check_exception()) {
        Item error = js_clear_exception();
        promise = js_promise_reject(error);
    } else {
        promise = js_promise_resolve(result);
        if (js_check_exception()) {
            Item error = js_clear_exception();
            promise = js_promise_reject(error);
        }
    }

    Item* cb_env = js_alloc_env(1);
    cb_env[0] = callback;
    Item on_fulfilled = js_new_closure((void*)js_util_callbackify_on_fulfilled, 1, cb_env, 1);
    Item on_rejected = js_new_closure((void*)js_util_callbackify_on_rejected, 1, cb_env, 1);
    js_promise_then(promise, on_fulfilled, on_rejected);
    return make_js_undefined();
}

extern "C" Item js_util_callbackify(Item fn) {
    if (get_type_id(fn) != LMD_TYPE_FUNC) {
        return js_throw_invalid_arg_type("original", "function", fn);
    }

    Item* env = js_alloc_env(1);
    env[0] = fn;
    Item wrapper = js_new_closure((void*)js_util_callbackified_function, -1, env, 1);
    js_set_function_name(wrapper, make_string_item("callbackified"));
    return wrapper;
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
        char msg[256];
        snprintf(msg, sizeof(msg), "The \"ctor\" argument must be of type function. Received %s", received);
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE", msg);
    }
    // validate superCtor — reject null, undefined, and non-object primitives
    if (super_item.item == ITEM_NULL || super_item.item == ITEM_JS_UNDEFINED) {
        const char* received = (super_item.item == ITEM_NULL) ? "null" : "undefined";
        char msg[256];
        snprintf(msg, sizeof(msg), "The \"superCtor\" argument must be of type function. Received %s", received);
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE", msg);
    }
    // validate superCtor.prototype
    Item super_proto = js_property_get(super_item, make_string_item("prototype"));
    TypeId proto_tid = get_type_id(super_proto);
    if (proto_tid != LMD_TYPE_MAP && proto_tid != LMD_TYPE_ARRAY) {
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "The \"superCtor.prototype\" property must be of type object. Received undefined");
    }
    // Set ctor.prototype.__proto__ = superCtor.prototype
    // This preserves existing properties on ctor.prototype (like D.prototype.d set before inherits)
    Item ctor_proto = js_property_get(ctor_item, make_string_item("prototype"));
    js_property_set(ctor_proto, make_string_item("__proto__"), super_proto);
    js_property_set(ctor_proto, make_string_item("constructor"), ctor_item);
    // set ctor.super_ = superConstructor (non-enumerable, per Node.js spec)
    Item super_key = make_string_item("super_");
    js_property_set(ctor_item, super_key, super_item);
    js_mark_non_enumerable(ctor_item, super_key);
    return make_js_undefined();
}

// =============================================================================
// util.isDeepStrictEqual(val1, val2) — basic deep equality
// =============================================================================

static bool js_util_dataview_current_byte_length(JsDataView* view, int* out_length) {
    if (!view || !view->buffer || view->buffer->detached) return false;
    if (view->buffer->byte_length < view->byte_offset) return false;
    if (view->length_tracking) {
        *out_length = view->buffer->byte_length - view->byte_offset;
        return true;
    }
    if (view->byte_length < 0 ||
        view->buffer->byte_length < (int64_t)view->byte_offset + (int64_t)view->byte_length) {
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
    if (!av->buffer->data || !bv->buffer->data) return false;
    uint8_t* adata = (uint8_t*)av->buffer->data + av->byte_offset;
    uint8_t* bdata = (uint8_t*)bv->buffer->data + bv->byte_offset;
    return memcmp(adata, bdata, alen) == 0;
}

extern "C" Item js_util_isDeepStrictEqual(Item a, Item b) {
    // use strict equality for primitives
    Item eq = js_strict_equal(a, b);
    if (js_is_truthy(eq)) return (Item){.item = b2it(true)};

    TypeId ta = get_type_id(a);
    TypeId tb = get_type_id(b);
    if (ta != tb) return (Item){.item = b2it(false)};

    bool a_dataview = js_is_dataview(a);
    bool b_dataview = js_is_dataview(b);
    if (a_dataview || b_dataview) {
        return (Item){.item = b2it(a_dataview && b_dataview &&
                                  js_util_dataview_bytes_equal(a, b))};
    }

    if (ta == LMD_TYPE_ARRAY) {
        int64_t la = js_array_length(a);
        int64_t lb = js_array_length(b);
        if (la != lb) return (Item){.item = b2it(false)};
        for (int64_t i = 0; i < la; i++) {
            Item ea = js_array_get_int(a, i);
            Item eb = js_array_get_int(b, i);
            Item r = js_util_isDeepStrictEqual(ea, eb);
            if (!js_is_truthy(r)) return (Item){.item = b2it(false)};
        }
        return (Item){.item = b2it(true)};
    }

    if (ta == LMD_TYPE_MAP) {
        // Check if both are Set or Map collections
        extern bool js_is_set_instance(Item obj);
        extern bool js_is_map_instance(Item obj);
        extern Item js_collection_method(Item obj, int method_id, Item arg1, Item arg2);
        extern Item js_iterable_to_array(Item iterable);
        bool a_set = js_is_set_instance(a), b_set = js_is_set_instance(b);
        bool a_map = js_is_map_instance(a), b_map = js_is_map_instance(b);
        if (a_set && b_set) {
            // Set: compare by size, then check each element
            Item size_a = js_collection_method(a, 9, ItemNull, ItemNull); // 9=size
            Item size_b = js_collection_method(b, 9, ItemNull, ItemNull);
            if (it2i(size_a) != it2i(size_b)) return (Item){.item = b2it(false)};
            // Convert both to arrays and compare elements
            Item arr_a = js_iterable_to_array(a);
            Item arr_b = js_iterable_to_array(b);
            int64_t la = js_array_length(arr_a);
            for (int64_t i = 0; i < la; i++) {
                Item elem = js_array_get_int(arr_a, i);
                // Check if elem exists in b
                Item has = js_collection_method(b, 2, elem, ItemNull); // 2=has
                if (!js_is_truthy(has)) {
                    // Try deep equality for each element in b
                    bool found = false;
                    int64_t lb = js_array_length(arr_b);
                    for (int64_t j = 0; j < lb; j++) {
                        Item eb = js_array_get_int(arr_b, j);
                        Item r = js_util_isDeepStrictEqual(elem, eb);
                        if (js_is_truthy(r)) { found = true; break; }
                    }
                    if (!found) return (Item){.item = b2it(false)};
                }
            }
            return (Item){.item = b2it(true)};
        }
        if (a_map && b_map) {
            // Map: compare by size, then compare key-value pairs
            Item size_a = js_collection_method(a, 9, ItemNull, ItemNull);
            Item size_b = js_collection_method(b, 9, ItemNull, ItemNull);
            if (it2i(size_a) != it2i(size_b)) return (Item){.item = b2it(false)};
            // Convert to array of [key, value] entries
            Item entries_a = js_iterable_to_array(a);
            int64_t la = js_array_length(entries_a);
            for (int64_t i = 0; i < la; i++) {
                Item pair = js_array_get_int(entries_a, i);
                Item ka = js_array_get_int(pair, 0);
                Item va = js_array_get_int(pair, 1);
                Item vb = js_collection_method(b, 1, ka, ItemNull); // 1=get
                Item r = js_util_isDeepStrictEqual(va, vb);
                if (!js_is_truthy(r)) return (Item){.item = b2it(false)};
            }
            return (Item){.item = b2it(true)};
        }
        if ((a_set && !b_set) || (!a_set && b_set) || (a_map && !b_map) || (!a_map && b_map)) {
            return (Item){.item = b2it(false)};
        }
        Item keys_a = js_object_keys(a);
        Item keys_b = js_object_keys(b);
        int64_t la = js_array_length(keys_a);
        int64_t lb = js_array_length(keys_b);
        if (la != lb) return (Item){.item = b2it(false)};
        for (int64_t i = 0; i < la; i++) {
            Item key = js_array_get_int(keys_a, i);
            Item va = js_property_get(a, key);
            Item vb = js_property_get(b, key);
            Item r = js_util_isDeepStrictEqual(va, vb);
            if (!js_is_truthy(r)) return (Item){.item = b2it(false)};
        }
        return (Item){.item = b2it(true)};
    }

    return (Item){.item = b2it(false)};
}

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
    Item then = js_property_get(obj, make_string_item("then"));
    if (get_type_id(then) == LMD_TYPE_FUNC) return (Item){.item = b2it(true)};
    return (Item){.item = b2it(false)};
}

extern "C" Item js_util_types_isFunction(Item obj) {
    return (Item){.item = b2it(get_type_id(obj) == LMD_TYPE_FUNC)};
}

extern "C" Item js_util_types_isString(Item obj) {
    return (Item){.item = b2it(get_type_id(obj) == LMD_TYPE_STRING)};
}

extern "C" Item js_util_types_isNumber(Item obj) {
    TypeId t = get_type_id(obj);
    return (Item){.item = b2it(t == LMD_TYPE_INT || t == LMD_TYPE_FLOAT)};
}

extern "C" Item js_util_types_isBoolean(Item obj) {
    return (Item){.item = b2it(get_type_id(obj) == LMD_TYPE_BOOL)};
}

extern "C" Item js_util_types_isNull(Item obj) {
    return (Item){.item = b2it(get_type_id(obj) == LMD_TYPE_NULL)};
}

extern "C" Item js_util_types_isUndefined(Item obj) {
    return (Item){.item = b2it(get_type_id(obj) == LMD_TYPE_UNDEFINED)};
}

extern "C" Item js_util_types_isNullOrUndefined(Item obj) {
    TypeId t = get_type_id(obj);
    return (Item){.item = b2it(t == LMD_TYPE_NULL || t == LMD_TYPE_UNDEFINED)};
}

extern "C" Item js_util_types_isObject(Item obj) {
    TypeId t = get_type_id(obj);
    return (Item){.item = b2it(t == LMD_TYPE_MAP || t == LMD_TYPE_ARRAY || t == LMD_TYPE_FUNC)};
}

extern "C" Item js_util_types_isPrimitive(Item obj) {
    TypeId t = get_type_id(obj);
    return (Item){.item = b2it(t == LMD_TYPE_NULL || t == LMD_TYPE_UNDEFINED ||
                                t == LMD_TYPE_BOOL || t == LMD_TYPE_INT ||
                                t == LMD_TYPE_FLOAT || t == LMD_TYPE_STRING)};
}

extern "C" Item js_util_types_isBuffer(Item obj) {
    extern Item js_buffer_isBuffer(Item);
    return js_buffer_isBuffer(obj);
}

extern "C" Item js_util_types_isError(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    JsClass cls = js_class_id(obj);
    if (js_class_is_error_like(cls) || cls == JS_CLASS_DOM_EXCEPTION) {
        return (Item){.item = b2it(true)};
    }
    // check for .message + .stack
    Item msg = js_property_get(obj, make_string_item("message"));
    if (get_type_id(msg) == LMD_TYPE_STRING) return (Item){.item = b2it(true)};
    return (Item){.item = b2it(false)};
}

// ─── additional util.types.* functions ───────────────────────────────────────

extern "C" Item js_util_types_isTypedArray(Item obj) {
    return (Item){.item = b2it(js_is_typed_array(obj))};
}

extern "C" Item js_util_types_isArrayBuffer(Item obj) {
    return (Item){.item = b2it(js_is_arraybuffer(obj))};
}

extern "C" Item js_util_types_isSharedArrayBuffer(Item obj) {
    return (Item){.item = b2it(js_is_sharedarraybuffer(obj))};
}

extern "C" Item js_util_types_isAnyArrayBuffer(Item obj) {
    return (Item){.item = b2it(js_is_arraybuffer(obj) || js_is_sharedarraybuffer(obj))};
}

extern "C" Item js_util_types_isDataView(Item obj) {
    return (Item){.item = b2it(js_class_id(obj) == JS_CLASS_DATA_VIEW)};
}

extern "C" Item js_util_types_isWeakMap(Item obj) {
    return (Item){.item = b2it(js_class_id(obj) == JS_CLASS_WEAK_MAP)};
}

extern "C" Item js_util_types_isWeakSet(Item obj) {
    return (Item){.item = b2it(js_class_id(obj) == JS_CLASS_WEAK_SET)};
}

extern "C" Item js_util_types_isWeakRef(Item obj) {
    return (Item){.item = b2it(js_class_id(obj) == JS_CLASS_WEAK_REF)};
}

static bool is_typed_array_type(Item obj, JsTypedArrayType target_type) {
    if (!js_is_typed_array(obj)) return false;
    Map* m = obj.map;
    JsTypedArray* ta = (JsTypedArray*)m->data;
    return ta && ta->element_type == target_type;
}

extern "C" Item js_util_types_isUint16Array(Item obj) {
    return (Item){.item = b2it(is_typed_array_type(obj, JS_TYPED_UINT16))};
}

extern "C" Item js_util_types_isUint32Array(Item obj) {
    return (Item){.item = b2it(is_typed_array_type(obj, JS_TYPED_UINT32))};
}

extern "C" Item js_util_types_isInt8Array(Item obj) {
    return (Item){.item = b2it(is_typed_array_type(obj, JS_TYPED_INT8))};
}

extern "C" Item js_util_types_isInt16Array(Item obj) {
    return (Item){.item = b2it(is_typed_array_type(obj, JS_TYPED_INT16))};
}

extern "C" Item js_util_types_isInt32Array(Item obj) {
    return (Item){.item = b2it(is_typed_array_type(obj, JS_TYPED_INT32))};
}

extern "C" Item js_util_types_isFloat32Array(Item obj) {
    return (Item){.item = b2it(is_typed_array_type(obj, JS_TYPED_FLOAT32))};
}

extern "C" Item js_util_types_isFloat64Array(Item obj) {
    return (Item){.item = b2it(is_typed_array_type(obj, JS_TYPED_FLOAT64))};
}

extern "C" Item js_util_types_isUint8ClampedArray(Item obj) {
    return (Item){.item = b2it(is_typed_array_type(obj, JS_TYPED_UINT8_CLAMPED))};
}

extern "C" Item js_util_types_isNumberObject(Item obj) {
    return (Item){.item = b2it(js_class_id(obj) == JS_CLASS_NUMBER)};
}

extern "C" Item js_util_types_isStringObject(Item obj) {
    return (Item){.item = b2it(js_class_id(obj) == JS_CLASS_STRING)};
}

extern "C" Item js_util_types_isBooleanObject(Item obj) {
    return (Item){.item = b2it(js_class_id(obj) == JS_CLASS_BOOLEAN)};
}

extern "C" Item js_util_types_isSymbolObject(Item obj) {
    return (Item){.item = b2it(js_class_id(obj) == JS_CLASS_SYMBOL)};
}

extern "C" Item js_util_types_isNativeError(Item obj) {
    return js_util_types_isError(obj);
}

extern "C" Item js_util_types_isBoxedPrimitive(Item obj) {
    JsClass cls = js_class_id(obj);
    bool boxed = (cls == JS_CLASS_NUMBER || cls == JS_CLASS_STRING ||
                  cls == JS_CLASS_BOOLEAN || cls == JS_CLASS_SYMBOL ||
                  cls == JS_CLASS_BIGINT);
    return (Item){.item = b2it(boxed)};
}

extern "C" Item js_util_types_isProxy(Item obj) {
    // can't detect Proxy from the outside — always return false
    (void)obj;
    return (Item){.item = b2it(false)};
}

extern "C" Item js_util_types_isExternal(Item obj) {
    // not supported — always return false
    (void)obj;
    return (Item){.item = b2it(false)};
}

extern "C" Item js_util_types_isGeneratorFunction(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_FUNC) return (Item){.item = b2it(false)};
    JsUtilFunctionView* fn = (JsUtilFunctionView*)obj.function;
    return (Item){.item = b2it(fn && (fn->flags & JS_UTIL_FUNC_FLAG_GENERATOR))};
}

extern "C" Item js_util_types_isGeneratorObject(Item obj) {
    return (Item){.item = b2it(js_class_id(obj) == JS_CLASS_GENERATOR)};
}

extern "C" Item js_util_types_isAsyncFunction(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_FUNC) return (Item){.item = b2it(false)};
    JsUtilFunctionView* fn = (JsUtilFunctionView*)obj.function;
    return (Item){.item = b2it(fn && (fn->flags & JS_UTIL_FUNC_FLAG_ASYNC) &&
        !(fn->flags & JS_UTIL_FUNC_FLAG_GENERATOR))};
}

extern "C" Item js_util_types_isMapIterator(Item obj) {
    return (Item){.item = b2it(js_class_id(obj) == JS_CLASS_MAP_ITERATOR)};
}

extern "C" Item js_util_types_isSetIterator(Item obj) {
    return (Item){.item = b2it(js_class_id(obj) == JS_CLASS_SET_ITERATOR)};
}

extern "C" Item js_util_types_isArgumentsObject(Item obj) {
    return (Item){.item = b2it(js_class_id(obj) == JS_CLASS_ARGUMENTS)};
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
    return (Item){.item = s2it(heap_create_name(buf, total))};
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
        case 110: name = "ETIMEDOUT"; break;
        case 111: name = "ECONNREFUSED"; break;
        default: name = "UNKNOWN"; break;
    }
    return (Item){.item = s2it(heap_create_name(name, strlen(name)))};
}

// ─── util.debuglog(section) ─────────────────────────────────────────────────
// Returns a logging function gated by NODE_DEBUG environment variable.
// If NODE_DEBUG contains the section name (case-insensitive), the returned
// function logs to stderr. Otherwise it's a no-op.
static Item js_debuglog_noop(Item args_rest) {
    (void)args_rest;
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

extern "C" Item js_json_stringify(Item val);

static Item js_debuglog_active(Item args_rest) {
    // simple: log first argument to stderr
    if (args_rest.item != 0 && get_type_id(args_rest) != LMD_TYPE_UNDEFINED) {
        int64_t argc = js_array_length(args_rest);
        if (argc > 0) {
            Item arg0 = js_array_get_int(args_rest, 0);
            Item str = js_json_stringify(arg0);
            if (get_type_id(str) == LMD_TYPE_STRING) {
                String* s = it2s(str);
                if (s) fprintf(stderr, "%.*s\n", (int)s->len, s->chars);
            }
        }
    }
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

extern "C" Item js_util_debuglog(Item section) {
    if (get_type_id(section) != LMD_TYPE_STRING) {
        return js_new_function((void*)js_debuglog_noop, -1);
    }
    String* sec = it2s(section);
    if (!sec || sec->len == 0) {
        return js_new_function((void*)js_debuglog_noop, -1);
    }

    // check NODE_DEBUG env var
    const char* node_debug = getenv("NODE_DEBUG");
    if (!node_debug) {
        return js_new_function((void*)js_debuglog_noop, -1);
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
        return js_new_function((void*)js_debuglog_active, -1);
    }

    return js_new_function((void*)js_debuglog_noop, -1);
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
    return (Item){.item = s2it(heap_create_name(buf, out))};
}

// util._extend(target, source) — deprecated Object.assign equivalent
static Item js_util_extend(Item target, Item source) {
    if (get_type_id(source) == LMD_TYPE_MAP) {
        Item keys = js_object_keys(source);
        int64_t len = js_array_length(keys);
        for (int64_t i = 0; i < len; i++) {
            Item key = js_array_get_int(keys, i);
            js_property_set(target, key, js_property_get(source, key));
        }
    }
    return target;
}

// =============================================================================
// util Module Namespace Object
// =============================================================================

static Item util_namespace = {0};

static void js_util_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
}

extern "C" Item js_get_util_namespace(void) {
    if (util_namespace.item != 0) return util_namespace;

    util_namespace = js_new_object();

    js_util_set_method(util_namespace, "format",              (void*)js_util_format, -1);
    js_util_set_method(util_namespace, "inspect",             (void*)js_util_inspect, 2);
    // Set util.inspect.custom = Symbol.for('nodejs.util.inspect.custom')
    {
        extern Item js_symbol_for(Item desc);
        Item inspect_fn = js_property_get(util_namespace, make_string_item("inspect"));
        Item custom_sym = js_symbol_for(make_string_item("nodejs.util.inspect.custom"));
        js_property_set(inspect_fn, make_string_item("custom"), custom_sym);
    }
    js_util_set_method(util_namespace, "promisify",           (void*)js_util_promisify, 1);
    {
        Item promisify_fn = js_property_get(util_namespace, make_string_item("promisify"));
        js_property_set(promisify_fn, make_string_item("custom"), js_util_promisify_custom_symbol());
    }
    js_util_set_method(util_namespace, "callbackify",         (void*)js_util_callbackify, 1);
    js_util_set_method(util_namespace, "deprecate",           (void*)js_util_deprecate, 2);
    js_util_set_method(util_namespace, "inherits",            (void*)js_util_inherits, 2);
    js_util_set_method(util_namespace, "isDeepStrictEqual",   (void*)js_util_isDeepStrictEqual, 2);
    js_util_set_method(util_namespace, "debuglog",            (void*)js_util_debuglog, 1);
    js_util_set_method(util_namespace, "styleText",           (void*)js_util_styleText, 2);
    js_util_set_method(util_namespace, "getSystemErrorName",  (void*)js_util_getSystemErrorName, 1);
    js_util_set_method(util_namespace, "stripVTControlCharacters", (void*)js_util_stripVTControlCharacters, 1);
    js_util_set_method(util_namespace, "_extend",             (void*)js_util_extend, 2);
    // util.debug is an alias for util.debuglog
    js_util_set_method(util_namespace, "debug",               (void*)js_util_debuglog, 1);

    // util.types sub-namespace
    Item types = js_new_object();
    js_util_set_method(types, "isDate",           (void*)js_util_types_isDate, 1);
    js_util_set_method(types, "isRegExp",         (void*)js_util_types_isRegExp, 1);
    js_util_set_method(types, "isArray",          (void*)js_util_types_isArray, 1);
    js_util_set_method(types, "isMap",            (void*)js_util_types_isMap, 1);
    js_util_set_method(types, "isSet",            (void*)js_util_types_isSet, 1);
    js_util_set_method(types, "isUint8Array",     (void*)js_util_types_isUint8Array, 1);
    js_util_set_method(types, "isPromise",        (void*)js_util_types_isPromise, 1);
    js_util_set_method(types, "isFunction",       (void*)js_util_types_isFunction, 1);
    js_util_set_method(types, "isString",         (void*)js_util_types_isString, 1);
    js_util_set_method(types, "isNumber",         (void*)js_util_types_isNumber, 1);
    js_util_set_method(types, "isBoolean",        (void*)js_util_types_isBoolean, 1);
    js_util_set_method(types, "isNull",           (void*)js_util_types_isNull, 1);
    js_util_set_method(types, "isUndefined",      (void*)js_util_types_isUndefined, 1);
    js_util_set_method(types, "isNullOrUndefined",(void*)js_util_types_isNullOrUndefined, 1);
    js_util_set_method(types, "isObject",         (void*)js_util_types_isObject, 1);
    js_util_set_method(types, "isPrimitive",      (void*)js_util_types_isPrimitive, 1);
    js_util_set_method(types, "isBuffer",         (void*)js_util_types_isBuffer, 1);
    js_util_set_method(types, "isError",          (void*)js_util_types_isError, 1);
    // additional types checks
    js_util_set_method(types, "isTypedArray",     (void*)js_util_types_isTypedArray, 1);
    js_util_set_method(types, "isArrayBuffer",    (void*)js_util_types_isArrayBuffer, 1);
    js_util_set_method(types, "isSharedArrayBuffer", (void*)js_util_types_isSharedArrayBuffer, 1);
    js_util_set_method(types, "isAnyArrayBuffer",    (void*)js_util_types_isAnyArrayBuffer, 1);
    js_util_set_method(types, "isDataView",       (void*)js_util_types_isDataView, 1);
    js_util_set_method(types, "isWeakMap",        (void*)js_util_types_isWeakMap, 1);
    js_util_set_method(types, "isWeakSet",        (void*)js_util_types_isWeakSet, 1);
    js_util_set_method(types, "isWeakRef",        (void*)js_util_types_isWeakRef, 1);
    js_util_set_method(types, "isUint16Array",    (void*)js_util_types_isUint16Array, 1);
    js_util_set_method(types, "isUint32Array",    (void*)js_util_types_isUint32Array, 1);
    js_util_set_method(types, "isInt8Array",      (void*)js_util_types_isInt8Array, 1);
    js_util_set_method(types, "isInt16Array",     (void*)js_util_types_isInt16Array, 1);
    js_util_set_method(types, "isInt32Array",     (void*)js_util_types_isInt32Array, 1);
    js_util_set_method(types, "isFloat32Array",   (void*)js_util_types_isFloat32Array, 1);
    js_util_set_method(types, "isFloat64Array",   (void*)js_util_types_isFloat64Array, 1);
    js_util_set_method(types, "isUint8ClampedArray", (void*)js_util_types_isUint8ClampedArray, 1);
    js_util_set_method(types, "isNumberObject",   (void*)js_util_types_isNumberObject, 1);
    js_util_set_method(types, "isStringObject",   (void*)js_util_types_isStringObject, 1);
    js_util_set_method(types, "isBooleanObject",  (void*)js_util_types_isBooleanObject, 1);
    js_util_set_method(types, "isSymbolObject",   (void*)js_util_types_isSymbolObject, 1);
    js_util_set_method(types, "isNativeError",    (void*)js_util_types_isNativeError, 1);
    js_util_set_method(types, "isBoxedPrimitive", (void*)js_util_types_isBoxedPrimitive, 1);
    js_util_set_method(types, "isProxy",          (void*)js_util_types_isProxy, 1);
    js_util_set_method(types, "isExternal",       (void*)js_util_types_isExternal, 1);
    js_util_set_method(types, "isGeneratorFunction", (void*)js_util_types_isGeneratorFunction, 1);
    js_util_set_method(types, "isGeneratorObject",   (void*)js_util_types_isGeneratorObject, 1);
    js_util_set_method(types, "isAsyncFunction",     (void*)js_util_types_isAsyncFunction, 1);
    js_util_set_method(types, "isMapIterator",       (void*)js_util_types_isMapIterator, 1);
    js_util_set_method(types, "isSetIterator",       (void*)js_util_types_isSetIterator, 1);
    js_util_set_method(types, "isArgumentsObject",   (void*)js_util_types_isArgumentsObject, 1);
    js_property_set(util_namespace, make_string_item("types"), types);

    // TextEncoder/TextDecoder — expose constructors on util namespace
    extern Item js_text_encoder_new(void);
    extern Item js_text_decoder_new(Item encoding_item);
    Item te_ctor = js_new_function((void*)js_text_encoder_new, 0);
    Item td_ctor = js_new_function((void*)js_text_decoder_new, 1);
    js_property_set(util_namespace, make_string_item("TextEncoder"), te_ctor);
    js_property_set(util_namespace, make_string_item("TextDecoder"), td_ctor);

    // default export
    Item default_key = make_string_item("default");
    js_property_set(util_namespace, default_key, util_namespace);

    return util_namespace;
}

extern "C" void js_util_reset(void) {
    util_namespace = (Item){0};
}
