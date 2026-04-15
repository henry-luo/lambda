/**
 * js_util.cpp — Node.js-style 'util' module for LambdaJS
 *
 * Provides utility functions: format, inspect, types, promisify (stub).
 * Registered as built-in module 'util' via js_module_get().
 */
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/strbuf.h"

#include <cstring>
#include <cstdio>

// Helper: make JS undefined
static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

static const char* item_to_cstr(Item value, char* buf, int buf_size) {
    if (get_type_id(value) != LMD_TYPE_STRING) return NULL;
    String* s = it2s(value);
    int len = (int)s->len;
    if (len >= buf_size) len = buf_size - 1;
    memcpy(buf, s->chars, len);
    buf[len] = '\0';
    return buf;
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
    if (get_type_id(first) != LMD_TYPE_STRING || argc == 1) {
        // no format string — stringify and join with space
        char buf[8192];
        int pos = 0;
        for (int i = 0; i < argc; i++) {
            if (i > 0 && pos < (int)sizeof(buf) - 1) buf[pos++] = ' ';
            Item str = js_to_string(js_array_get_int(args_item, i));
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
        if (fmt->chars[i] == '%' && i + 1 < (int)fmt->len && arg_idx < argc) {
            char spec = fmt->chars[i + 1];
            Item arg = js_array_get_int(args_item, arg_idx);
            i++; // skip past specifier

            switch (spec) {
                case 's': {
                    Item str = js_to_string(arg);
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
                case 'd':
                case 'i': {
                    Item num = js_to_number(arg);
                    TypeId t = get_type_id(num);
                    if (t == LMD_TYPE_INT) {
                        pos += snprintf(buf + pos, sizeof(buf) - pos, "%lld", (long long)it2i(num));
                    } else if (t == LMD_TYPE_FLOAT) {
                        pos += snprintf(buf + pos, sizeof(buf) - pos, "%d", (int)it2d(num));
                    }
                    arg_idx++;
                    break;
                }
                case 'f': {
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
                    }
                    arg_idx++;
                    break;
                }
                case '%': {
                    buf[pos++] = '%';
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

    // append remaining args
    while (arg_idx < argc && pos < (int)sizeof(buf) - 2) {
        buf[pos++] = ' ';
        Item str = js_to_string(js_array_get_int(args_item, arg_idx++));
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
    if (get_type_id(value) != LMD_TYPE_MAP) return (Item){.item = i2it(0)};
    Item cn = js_property_get(value, make_string_item("__class_name__"));
    if (get_type_id(cn) != LMD_TYPE_STRING) return (Item){.item = i2it(0)};
    String* s = it2s(cn);
    return (Item){.item = i2it(s->len == 4 && memcmp(s->chars, "Date", 4) == 0 ? 1 : 0)};
}

extern "C" Item js_util_types_isRegExp(Item value) {
    if (get_type_id(value) != LMD_TYPE_MAP) return (Item){.item = i2it(0)};
    Item cn = js_property_get(value, make_string_item("__class_name__"));
    if (get_type_id(cn) != LMD_TYPE_STRING) return (Item){.item = i2it(0)};
    String* s = it2s(cn);
    return (Item){.item = i2it(s->len == 6 && memcmp(s->chars, "RegExp", 6) == 0 ? 1 : 0)};
}

extern "C" Item js_util_types_isArray(Item value) {
    return (Item){.item = i2it(get_type_id(value) == LMD_TYPE_ARRAY ? 1 : 0)};
}

extern "C" Item js_util_types_isMap(Item value) {
    if (get_type_id(value) != LMD_TYPE_MAP) return (Item){.item = i2it(0)};
    Item cn = js_property_get(value, make_string_item("__class_name__"));
    if (get_type_id(cn) != LMD_TYPE_STRING) return (Item){.item = i2it(0)};
    String* s = it2s(cn);
    return (Item){.item = i2it(s->len == 3 && memcmp(s->chars, "Map", 3) == 0 ? 1 : 0)};
}

extern "C" Item js_util_types_isSet(Item value) {
    if (get_type_id(value) != LMD_TYPE_MAP) return (Item){.item = i2it(0)};
    Item cn = js_property_get(value, make_string_item("__class_name__"));
    if (get_type_id(cn) != LMD_TYPE_STRING) return (Item){.item = i2it(0)};
    String* s = it2s(cn);
    return (Item){.item = i2it(s->len == 3 && memcmp(s->chars, "Set", 3) == 0 ? 1 : 0)};
}

// =============================================================================
// util.promisify(original) — stub (returns function wrapping callback-based API)
// =============================================================================

extern "C" Item js_util_promisify(Item fn_item) {
    // stub — returns the function as-is for now
    // proper promisify requires Promise support
    return fn_item;
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
    // set ctor.prototype = Object.create(super.prototype)
    Item super_proto = js_property_get(super_item, make_string_item("prototype"));
    if (get_type_id(super_proto) == LMD_TYPE_MAP || get_type_id(super_proto) == LMD_TYPE_ARRAY) {
        Item new_proto = js_object_create(super_proto);
        js_property_set(new_proto, make_string_item("constructor"), ctor_item);
        js_property_set(ctor_item, make_string_item("prototype"), new_proto);
    }
    // set ctor.super_ = superConstructor
    js_property_set(ctor_item, make_string_item("super_"), super_item);
    return make_js_undefined();
}

// =============================================================================
// util.isDeepStrictEqual(val1, val2) — basic deep equality
// =============================================================================

extern "C" Item js_util_isDeepStrictEqual(Item a, Item b) {
    // use strict equality for primitives
    Item eq = js_strict_equal(a, b);
    if (get_type_id(eq) == LMD_TYPE_INT && it2i(eq) == 1) return eq;

    TypeId ta = get_type_id(a);
    TypeId tb = get_type_id(b);
    if (ta != tb) return (Item){.item = i2it(0)};

    if (ta == LMD_TYPE_ARRAY) {
        int64_t la = js_array_length(a);
        int64_t lb = js_array_length(b);
        if (la != lb) return (Item){.item = i2it(0)};
        for (int64_t i = 0; i < la; i++) {
            Item ea = js_array_get_int(a, i);
            Item eb = js_array_get_int(b, i);
            Item r = js_util_isDeepStrictEqual(ea, eb);
            if (get_type_id(r) != LMD_TYPE_INT || it2i(r) != 1) return (Item){.item = i2it(0)};
        }
        return (Item){.item = i2it(1)};
    }

    if (ta == LMD_TYPE_MAP) {
        Item keys_a = js_object_keys(a);
        Item keys_b = js_object_keys(b);
        int64_t la = js_array_length(keys_a);
        int64_t lb = js_array_length(keys_b);
        if (la != lb) return (Item){.item = i2it(0)};
        for (int64_t i = 0; i < la; i++) {
            Item key = js_array_get_int(keys_a, i);
            Item va = js_property_get(a, key);
            Item vb = js_property_get(b, key);
            Item r = js_util_isDeepStrictEqual(va, vb);
            if (get_type_id(r) != LMD_TYPE_INT || it2i(r) != 1) return (Item){.item = i2it(0)};
        }
        return (Item){.item = i2it(1)};
    }

    return (Item){.item = i2it(0)};
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
    js_util_set_method(util_namespace, "promisify",           (void*)js_util_promisify, 1);
    js_util_set_method(util_namespace, "deprecate",           (void*)js_util_deprecate, 2);
    js_util_set_method(util_namespace, "inherits",            (void*)js_util_inherits, 2);
    js_util_set_method(util_namespace, "isDeepStrictEqual",   (void*)js_util_isDeepStrictEqual, 2);

    // util.types sub-namespace
    Item types = js_new_object();
    js_util_set_method(types, "isDate",    (void*)js_util_types_isDate, 1);
    js_util_set_method(types, "isRegExp",  (void*)js_util_types_isRegExp, 1);
    js_util_set_method(types, "isArray",   (void*)js_util_types_isArray, 1);
    js_util_set_method(types, "isMap",     (void*)js_util_types_isMap, 1);
    js_util_set_method(types, "isSet",     (void*)js_util_types_isSet, 1);
    js_property_set(util_namespace, make_string_item("types"), types);

    // default export
    Item default_key = make_string_item("default");
    js_property_set(util_namespace, default_key, util_namespace);

    return util_namespace;
}

extern "C" void js_util_reset(void) {
    util_namespace = (Item){0};
}
