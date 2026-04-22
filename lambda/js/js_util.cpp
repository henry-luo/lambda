/**
 * js_util.cpp — Node.js-style 'util' module for LambdaJS
 *
 * Provides utility functions: format, inspect, types, promisify (stub).
 * Registered as built-in module 'util' via js_module_get().
 */
#include "js_runtime.h"
#include "js_typed_array.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/strbuf.h"

#include <cstring>
#include <cmath>
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
                    double val = 0;
                    bool is_nan = false;
                    if (t == LMD_TYPE_INT) {
                        val = (double)it2i(num);
                    } else if (t == LMD_TYPE_FLOAT) {
                        val = it2d(num);
                        if (val != val) is_nan = true; // NaN check
                    } else {
                        is_nan = true;
                    }

                    // %i truncates to integer (like parseInt)
                    if (spec == 'i' && !is_nan && val == val) {
                        val = (val >= 0) ? floor(val) : ceil(val);
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
                    } else {
                        // after truncation for %i, val is always integer
                        pos += snprintf(buf + pos, sizeof(buf) - pos, "%lld", (long long)val);
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
                case 'j':
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
    if (get_type_id(value) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    Item cn = js_property_get(value, make_string_item("__class_name__"));
    if (get_type_id(cn) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
    String* s = it2s(cn);
    return (Item){.item = b2it(s->len == 4 && memcmp(s->chars, "Date", 4) == 0)};
}

extern "C" Item js_util_types_isRegExp(Item value) {
    if (get_type_id(value) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    Item cn = js_property_get(value, make_string_item("__class_name__"));
    if (get_type_id(cn) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
    String* s = it2s(cn);
    return (Item){.item = b2it(s->len == 6 && memcmp(s->chars, "RegExp", 6) == 0)};
}

extern "C" Item js_util_types_isArray(Item value) {
    return (Item){.item = b2it(get_type_id(value) == LMD_TYPE_ARRAY)};
}

extern "C" Item js_util_types_isMap(Item value) {
    if (get_type_id(value) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    Item cn = js_property_get(value, make_string_item("__class_name__"));
    if (get_type_id(cn) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
    String* s = it2s(cn);
    return (Item){.item = b2it(s->len == 3 && memcmp(s->chars, "Map", 3) == 0)};
}

extern "C" Item js_util_types_isSet(Item value) {
    if (get_type_id(value) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    Item cn = js_property_get(value, make_string_item("__class_name__"));
    if (get_type_id(cn) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
    String* s = it2s(cn);
    return (Item){.item = b2it(s->len == 3 && memcmp(s->chars, "Set", 3) == 0)};
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
    if (js_is_truthy(eq)) return (Item){.item = b2it(true)};

    TypeId ta = get_type_id(a);
    TypeId tb = get_type_id(b);
    if (ta != tb) return (Item){.item = b2it(false)};

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

// util.callbackify(fn) — convert async/promise fn to callback style
// simplified: wraps fn so the last arg is a callback called with (err, result)
extern "C" Item js_util_callbackify(Item fn) {
    // return fn unchanged (simplified stub — proper impl needs closure wrapping)
    if (get_type_id(fn) != LMD_TYPE_FUNC) return fn;
    return fn;
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
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    Item cls = js_property_get(obj, make_string_item("__class_name__"));
    if (get_type_id(cls) == LMD_TYPE_STRING) {
        String* s = it2s(cls);
        if (s && s->len == 7 && memcmp(s->chars, "Promise", 7) == 0)
            return (Item){.item = b2it(true)};
    }
    // also check for .then method
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
    Item cls = js_property_get(obj, make_string_item("__class_name__"));
    if (get_type_id(cls) == LMD_TYPE_STRING) {
        String* s = it2s(cls);
        if (s && s->len >= 5 && memcmp(s->chars + s->len - 5, "Error", 5) == 0)
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
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    Item cn = js_property_get(obj, make_string_item("__class_name__"));
    if (get_type_id(cn) == LMD_TYPE_STRING) {
        String* s = it2s(cn);
        if (s && s->len == 8 && memcmp(s->chars, "DataView", 8) == 0)
            return (Item){.item = b2it(true)};
    }
    return (Item){.item = b2it(false)};
}

extern "C" Item js_util_types_isWeakMap(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    Item cn = js_property_get(obj, make_string_item("__class_name__"));
    if (get_type_id(cn) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
    String* s = it2s(cn);
    return (Item){.item = b2it(s->len == 7 && memcmp(s->chars, "WeakMap", 7) == 0)};
}

extern "C" Item js_util_types_isWeakSet(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    Item cn = js_property_get(obj, make_string_item("__class_name__"));
    if (get_type_id(cn) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
    String* s = it2s(cn);
    return (Item){.item = b2it(s->len == 7 && memcmp(s->chars, "WeakSet", 7) == 0)};
}

extern "C" Item js_util_types_isWeakRef(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    Item cn = js_property_get(obj, make_string_item("__class_name__"));
    if (get_type_id(cn) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
    String* s = it2s(cn);
    return (Item){.item = b2it(s->len == 7 && memcmp(s->chars, "WeakRef", 7) == 0)};
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
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    Item cn = js_property_get(obj, make_string_item("__class_name__"));
    if (get_type_id(cn) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
    String* s = it2s(cn);
    return (Item){.item = b2it(s->len == 6 && memcmp(s->chars, "Number", 6) == 0)};
}

extern "C" Item js_util_types_isStringObject(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    Item cn = js_property_get(obj, make_string_item("__class_name__"));
    if (get_type_id(cn) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
    String* s = it2s(cn);
    return (Item){.item = b2it(s->len == 6 && memcmp(s->chars, "String", 6) == 0)};
}

extern "C" Item js_util_types_isBooleanObject(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    Item cn = js_property_get(obj, make_string_item("__class_name__"));
    if (get_type_id(cn) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
    String* s = it2s(cn);
    return (Item){.item = b2it(s->len == 7 && memcmp(s->chars, "Boolean", 7) == 0)};
}

extern "C" Item js_util_types_isSymbolObject(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    Item cn = js_property_get(obj, make_string_item("__class_name__"));
    if (get_type_id(cn) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
    String* s = it2s(cn);
    return (Item){.item = b2it(s->len == 6 && memcmp(s->chars, "Symbol", 6) == 0)};
}

extern "C" Item js_util_types_isNativeError(Item obj) {
    // same as isError — checks __class_name__ ending in "Error"
    return js_util_types_isError(obj);
}

extern "C" Item js_util_types_isBoxedPrimitive(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    Item cn = js_property_get(obj, make_string_item("__class_name__"));
    if (get_type_id(cn) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
    String* s = it2s(cn);
    if (s->len == 6 && memcmp(s->chars, "Number", 6) == 0) return (Item){.item = b2it(true)};
    if (s->len == 6 && memcmp(s->chars, "String", 6) == 0) return (Item){.item = b2it(true)};
    if (s->len == 7 && memcmp(s->chars, "Boolean", 7) == 0) return (Item){.item = b2it(true)};
    if (s->len == 6 && memcmp(s->chars, "Symbol", 6) == 0) return (Item){.item = b2it(true)};
    if (s->len == 6 && memcmp(s->chars, "BigInt", 6) == 0) return (Item){.item = b2it(true)};
    return (Item){.item = b2it(false)};
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
    Item cn = js_property_get(obj, make_string_item("__class_name__"));
    if (get_type_id(cn) == LMD_TYPE_STRING) {
        String* s = it2s(cn);
        if (s && s->len == 17 && memcmp(s->chars, "GeneratorFunction", 17) == 0)
            return (Item){.item = b2it(true)};
    }
    return (Item){.item = b2it(false)};
}

extern "C" Item js_util_types_isGeneratorObject(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    Item cn = js_property_get(obj, make_string_item("__class_name__"));
    if (get_type_id(cn) == LMD_TYPE_STRING) {
        String* s = it2s(cn);
        if (s && s->len == 9 && memcmp(s->chars, "Generator", 9) == 0)
            return (Item){.item = b2it(true)};
    }
    return (Item){.item = b2it(false)};
}

extern "C" Item js_util_types_isAsyncFunction(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_FUNC) return (Item){.item = b2it(false)};
    Item cn = js_property_get(obj, make_string_item("__class_name__"));
    if (get_type_id(cn) == LMD_TYPE_STRING) {
        String* s = it2s(cn);
        if (s && s->len == 13 && memcmp(s->chars, "AsyncFunction", 13) == 0)
            return (Item){.item = b2it(true)};
    }
    return (Item){.item = b2it(false)};
}

extern "C" Item js_util_types_isMapIterator(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    Item cn = js_property_get(obj, make_string_item("__class_name__"));
    if (get_type_id(cn) == LMD_TYPE_STRING) {
        String* s = it2s(cn);
        if (s && s->len == 11 && memcmp(s->chars, "MapIterator", 11) == 0)
            return (Item){.item = b2it(true)};
    }
    return (Item){.item = b2it(false)};
}

extern "C" Item js_util_types_isSetIterator(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    Item cn = js_property_get(obj, make_string_item("__class_name__"));
    if (get_type_id(cn) == LMD_TYPE_STRING) {
        String* s = it2s(cn);
        if (s && s->len == 11 && memcmp(s->chars, "SetIterator", 11) == 0)
            return (Item){.item = b2it(true)};
    }
    return (Item){.item = b2it(false)};
}

extern "C" Item js_util_types_isArgumentsObject(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP && get_type_id(obj) != LMD_TYPE_ARRAY) return (Item){.item = b2it(false)};
    Item cn = js_property_get(obj, make_string_item("__class_name__"));
    if (get_type_id(cn) == LMD_TYPE_STRING) {
        String* s = it2s(cn);
        if (s && s->len == 9 && memcmp(s->chars, "Arguments", 9) == 0)
            return (Item){.item = b2it(true)};
    }
    return (Item){.item = b2it(false)};
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
    js_util_set_method(util_namespace, "callbackify",         (void*)js_util_callbackify, 1);
    js_util_set_method(util_namespace, "deprecate",           (void*)js_util_deprecate, 2);
    js_util_set_method(util_namespace, "inherits",            (void*)js_util_inherits, 2);
    js_util_set_method(util_namespace, "isDeepStrictEqual",   (void*)js_util_isDeepStrictEqual, 2);
    js_util_set_method(util_namespace, "debuglog",            (void*)js_util_debuglog, 1);
    js_util_set_method(util_namespace, "styleText",           (void*)js_util_styleText, 2);
    js_util_set_method(util_namespace, "getSystemErrorName",  (void*)js_util_getSystemErrorName, 1);

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
