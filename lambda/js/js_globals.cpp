/**
 * JavaScript Process I/O and Global Functions for Lambda v5
 *
 * Implements:
 * - process.stdout.write(str)
 * - process.hrtime.bigint() (as float64 nanoseconds)
 * - process.argv
 * - parseInt, parseFloat, isNaN, isFinite
 * - console.log with multiple arguments
 */
#include "js_runtime.h"
#include "js_typed_array.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../transpiler.hpp"
#include "../format/format.h"
#include "../../lib/log.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <time.h>

// forward declaration for JSON parser
extern Item parse_json_to_item(Input* input, const char* json_string);

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

extern void* heap_alloc(int size, TypeId type_id);
extern void fn_array_set(Array* arr, int index, Item value);
extern "C" void js_set_prototype(Item object, Item prototype);
extern "C" Item js_get_prototype(Item object);

// =============================================================================
// Process I/O
// =============================================================================

extern "C" Item js_process_stdout_write(Item str_item) {
    TypeId type = get_type_id(str_item);
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(str_item);
        if (s && s->len > 0) {
            fwrite(s->chars, 1, s->len, stdout);
            fflush(stdout);
        }
    } else {
        // Convert to string first
        Item str = js_to_string(str_item);
        String* s = it2s(str);
        if (s && s->len > 0) {
            fwrite(s->chars, 1, s->len, stdout);
            fflush(stdout);
        }
    }
    return (Item){.item = ITEM_TRUE};
}

extern "C" Item js_process_hrtime_bigint(void) {
    // Return nanosecond-precision monotonic time as a double
    // double has ~53 bits of mantissa, good for ~104 days of nanosecond precision
#ifdef __APPLE__
    static mach_timebase_info_data_t timebase = {0, 0};
    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }
    uint64_t ticks = mach_absolute_time();
    double ns = (double)ticks * (double)timebase.numer / (double)timebase.denom;
    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = ns;
    return (Item){.item = d2it(fp)};
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double ns = (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = ns;
    return (Item){.item = d2it(fp)};
#endif
}

// performance.now() — returns milliseconds (monotonic, high-resolution)
// Uses static buffer (not GC heap) because the returned float must survive GC cycles.
// MIR-generated code stores the Item value in registers/stack, where the conservative
// GC scanner may not find it (nursery floats get overwritten; heap floats get collected).
static double js_perf_now_buf[64];
static int js_perf_now_idx = 0;

extern "C" Item js_performance_now(void) {
#ifdef __APPLE__
    static mach_timebase_info_data_t timebase = {0, 0};
    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }
    uint64_t ticks = mach_absolute_time();
    double ns = (double)ticks * (double)timebase.numer / (double)timebase.denom;
    double ms = ns / 1e6;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double ms = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#endif
    double* fp = &js_perf_now_buf[js_perf_now_idx % 64];
    js_perf_now_idx++;
    *fp = ms;
    return (Item){.item = d2it(fp)};
}

// Date.now() — returns milliseconds since Unix epoch
static double js_date_now_buf[64];
static int js_date_now_idx = 0;

extern "C" Item js_date_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    double ms = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
    double* fp = &js_date_now_buf[js_date_now_idx % 64];
    js_date_now_idx++;
    *fp = ms;
    return (Item){.item = d2it(fp)};
}

// new Date() — returns a map that acts as a Date object.
// Stores the current timestamp so .getTime() can retrieve it at runtime.
// The transpiler handles new Date().getTime() as a special case (→ js_date_now()),
// but js_date_new() is needed if the Date object is stored in a variable first.
extern "C" Item js_date_new(void) {
    Item obj = js_new_object();
    Item time_val = js_date_now();
    Item key = (Item){.item = s2it(heap_create_name("_time"))};
    js_property_set(obj, key, time_val);
    return obj;
}

// Process argv storage
static Item js_process_argv_items = {.item = ITEM_NULL};

extern "C" void js_set_process_argv(int argc, const char** argv) {
    // Build a Lambda array from the argv
    Array* arr = array();
    for (int i = 0; i < argc; i++) {
        array_push(arr, (Item){.item = s2it(heap_create_name(argv[i]))});
    }
    js_process_argv_items = array_end(arr);
}

extern "C" Item js_get_process_argv(void) {
    return js_process_argv_items;
}

// =============================================================================
// Global Functions
// =============================================================================

extern "C" Item js_parseInt(Item str_item, Item radix_item) {
    // Convert first arg to string
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }

    // Get radix (default 10)
    int radix = 10;
    TypeId rtype = get_type_id(radix_item);
    if (rtype == LMD_TYPE_INT) {
        radix = (int)it2i(radix_item);
    } else if (rtype == LMD_TYPE_FLOAT) {
        radix = (int)it2d(radix_item);
    }
    if (radix == 0) radix = 10;

    // Null-terminate
    char buf[256];
    int len = s->len < 255 ? s->len : 255;
    memcpy(buf, s->chars, len);
    buf[len] = '\0';

    // Skip whitespace
    char* start = buf;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') start++;

    char* endptr;
    long long val = strtoll(start, &endptr, radix);
    if (endptr == start) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }

    return (Item){.item = i2it((int64_t)val)};
}

extern "C" Item js_parseFloat(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }

    char buf[256];
    int len = s->len < 255 ? s->len : 255;
    memcpy(buf, s->chars, len);
    buf[len] = '\0';

    char* endptr;
    double val = strtod(buf, &endptr);
    if (endptr == buf) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }

    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = val;
    return (Item){.item = d2it(fp)};
}

extern "C" Item js_isNaN(Item value) {
    Item num = js_to_number(value);
    TypeId type = get_type_id(num);
    if (type == LMD_TYPE_FLOAT) {
        double d = it2d(num);
        return (Item){.item = isnan(d) ? ITEM_TRUE : ITEM_FALSE};
    }
    return (Item){.item = ITEM_FALSE};
}

extern "C" Item js_isFinite(Item value) {
    Item num = js_to_number(value);
    TypeId type = get_type_id(num);
    if (type == LMD_TYPE_FLOAT) {
        double d = it2d(num);
        return (Item){.item = isfinite(d) ? ITEM_TRUE : ITEM_FALSE};
    }
    if (type == LMD_TYPE_INT) {
        return (Item){.item = ITEM_TRUE};
    }
    return (Item){.item = ITEM_FALSE};
}

// =============================================================================
// Number Methods
// =============================================================================

extern "C" Item js_toFixed(Item num_item, Item digits_item) {
    double num;
    TypeId type = get_type_id(num_item);
    if (type == LMD_TYPE_FLOAT) {
        num = it2d(num_item);
    } else if (type == LMD_TYPE_INT) {
        num = (double)it2i(num_item);
    } else {
        return js_to_string(num_item);
    }

    int digits = 0;
    TypeId dtype = get_type_id(digits_item);
    if (dtype == LMD_TYPE_INT) {
        digits = (int)it2i(digits_item);
    } else if (dtype == LMD_TYPE_FLOAT) {
        digits = (int)it2d(digits_item);
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "%.*f", digits, num);
    return (Item){.item = s2it(heap_create_name(buf))};
}

extern "C" Item js_number_method(Item num, Item method_name, Item* args, int argc) {
    if (get_type_id(method_name) != LMD_TYPE_STRING) return ItemNull;
    String* method = it2s(method_name);
    if (!method) return ItemNull;

    if (method->len == 7 && strncmp(method->chars, "toFixed", 7) == 0) {
        Item digits = (argc > 0) ? args[0] : (Item){.item = i2it(0)};
        return js_toFixed(num, digits);
    }
    if (method->len == 8 && strncmp(method->chars, "toString", 8) == 0) {
        return js_to_string(num);
    }

    log_debug("js_number_method: unknown method '%.*s'", (int)method->len, method->chars);
    return ItemNull;
}

// =============================================================================
// String Methods (v5 additions)
// =============================================================================

extern "C" Item js_string_charCodeAt(Item str_item, Item index_item) {
    String* s = it2s(str_item);
    if (!s) return (Item){.item = i2it(0)};

    int idx = 0;
    TypeId itype = get_type_id(index_item);
    if (itype == LMD_TYPE_INT) {
        idx = (int)it2i(index_item);
    } else if (itype == LMD_TYPE_FLOAT) {
        idx = (int)it2d(index_item);
    }

    if (idx < 0 || idx >= (int)s->len) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }

    // Return the UTF-16 code unit (for ASCII, same as byte value)
    return (Item){.item = i2it((int64_t)(unsigned char)s->chars[idx])};
}

extern "C" Item js_string_fromCharCode(Item code_item) {
    int code = 0;
    TypeId type = get_type_id(code_item);
    if (type == LMD_TYPE_INT) {
        code = (int)it2i(code_item);
    } else if (type == LMD_TYPE_FLOAT) {
        code = (int)it2d(code_item);
    }

    char buf[5]; // max 4 bytes for UTF-8 + null
    if (code < 128) {
        buf[0] = (char)code;
        buf[1] = '\0';
    } else if (code < 0x800) {
        buf[0] = (char)(0xC0 | (code >> 6));
        buf[1] = (char)(0x80 | (code & 0x3F));
        buf[2] = '\0';
    } else {
        buf[0] = (char)(0xE0 | (code >> 12));
        buf[1] = (char)(0x80 | ((code >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (code & 0x3F));
        buf[3] = '\0';
    }

    return (Item){.item = s2it(heap_create_name(buf))};
}

// =============================================================================
// Console multi-argument log
// =============================================================================

extern "C" void js_console_log_multi(Item* args, int argc) {
    for (int i = 0; i < argc; i++) {
        if (i > 0) fputc(' ', stdout);
        Item str = js_to_string(args[i]);
        String* s = it2s(str);
        if (s) fwrite(s->chars, 1, s->len, stdout);
    }
    fputc('\n', stdout);
    fflush(stdout);
}

// =============================================================================
// Array fill (regular arrays)
// =============================================================================

extern "C" Item js_array_fill(Item arr_item, Item value) {
    TypeId type = get_type_id(arr_item);

    // Check if typed array first
    if (type == LMD_TYPE_MAP && js_is_typed_array(arr_item)) {
        return js_typed_array_fill(arr_item, value);
    }

    if (type != LMD_TYPE_ARRAY) return arr_item;

    int len = fn_len(arr_item);
    Array* arr = it2arr(arr_item);
    for (int i = 0; i < len; i++) {
        fn_array_set(arr, i, value);
    }

    return arr_item;
}

// =============================================================================
// instanceof operator — walks prototype chain
// =============================================================================

extern "C" Item js_instanceof(Item left, Item right) {
    // right should be a constructor (a class). We check if left's prototype chain
    // contains right's prototype. For our implementation, we check if right has
    // a __class_name__ marker that matches any __class_name__ in left's proto chain.
    if (get_type_id(left) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    if (get_type_id(right) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};

    // Get the class name from right (constructor's __class_name__)
    Item class_key = (Item){.item = s2it(heap_create_name("__class_name__", 14))};
    Item right_name = map_get(right.map, class_key);
    if (right_name.item == 0) return (Item){.item = b2it(false)};

    // Check if left has this class name in its prototype chain
    Item obj = left;
    int depth = 0;
    while (obj.item != 0 && get_type_id(obj) == LMD_TYPE_MAP && depth < 32) {
        Item obj_name = map_get(obj.map, class_key);
        if (obj_name.item != 0 && get_type_id(obj_name) == LMD_TYPE_STRING &&
            get_type_id(right_name) == LMD_TYPE_STRING) {
            String* on = it2s(obj_name);
            String* rn = it2s(right_name);
            if (on->len == rn->len && strncmp(on->chars, rn->chars, on->len) == 0) {
                return (Item){.item = b2it(true)};
            }
        }
        // walk up __proto__
        Item proto_key = (Item){.item = s2it(heap_create_name("__proto__", 9))};
        obj = map_get(obj.map, proto_key);
        depth++;
    }
    return (Item){.item = b2it(false)};
}

// =============================================================================
// in operator — check if key exists in object/array
// =============================================================================

extern "C" Item js_in(Item key, Item object) {
    TypeId type = get_type_id(object);
    if (type == LMD_TYPE_MAP) {
        // check if property exists on map
        Item result = js_property_access(object, key);
        return (Item){.item = b2it(get_type_id(result) != LMD_TYPE_NULL)};
    }
    if (type == LMD_TYPE_ARRAY) {
        // check if index is valid
        int idx = (int)it2i(key);
        int len = fn_len(object);
        return (Item){.item = b2it(idx >= 0 && idx < len)};
    }
    return (Item){.item = b2it(false)};
}

// =============================================================================
// nullish coalesce: a ?? b — returns b if a is null or undefined
// =============================================================================

extern "C" Item js_nullish_coalesce(Item left, Item right) {
    TypeId type = get_type_id(left);
    if (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED) return right;
    return left;
}

// =============================================================================
// Object.create — create new object with specified prototype
// =============================================================================

extern "C" Item js_object_create(Item proto) {
    Item obj = js_new_object();
    if (proto.item != 0 && get_type_id(proto) == LMD_TYPE_MAP) {
        js_set_prototype(obj, proto);
    }
    return obj;
}

// =============================================================================
// Object.defineProperty — define a property on an object
// =============================================================================

extern "C" Item js_object_define_property(Item obj, Item name, Item descriptor) {
    if (obj.item == 0) return obj;
    // extract descriptor.value
    if (get_type_id(descriptor) == LMD_TYPE_MAP) {
        Item value_key = (Item){.item = s2it(heap_create_name("value", 5))};
        Item value = js_property_get(descriptor, value_key);
        if (value.item != 0) {
            js_property_set(obj, name, value);
        } else {
            // check for getter/setter (accessor descriptor)
            Item get_key = (Item){.item = s2it(heap_create_name("get", 3))};
            Item getter = js_property_get(descriptor, get_key);
            if (getter.item != 0) {
                // store getter as the property value (simplified — no true accessor)
                js_property_set(obj, name, getter);
            }
        }
    }
    return obj;
}

// =============================================================================
// Array.isArray — check if value is an array
// =============================================================================

extern "C" Item js_array_is_array(Item value) {
    TypeId type = get_type_id(value);
    return (Item){.item = (type == LMD_TYPE_ARRAY) ? ITEM_TRUE : ITEM_FALSE};
}

// =============================================================================
// alert() — shim for benchmarks (outputs to console)
// =============================================================================

extern "C" Item js_alert(Item msg) {
    js_console_log(msg);
    return ItemNull;
}

// =============================================================================
// Object.keys — return array of property names
// =============================================================================

extern "C" Item js_object_keys(Item object) {
    TypeId type = get_type_id(object);
    if (type != LMD_TYPE_MAP) {
        return js_array_new(0);
    }

    Map* m = object.map;
    if (!m || !m->type) return js_array_new(0);

    TypeMap* tm = (TypeMap*)m->type;
    int count = 0;
    ShapeEntry* e = tm->shape;
    while (e) { count++; e = e->next; }

    Item result = js_array_new(count);
    e = tm->shape;
    int i = 0;
    while (e) {
        char nbuf[256];
        int nlen = (int)e->name->length < 255 ? (int)e->name->length : 255;
        memcpy(nbuf, e->name->str, nlen);
        nbuf[nlen] = '\0';
        Item key_str = (Item){.item = s2it(heap_create_name(nbuf))};
        js_array_set(result, (Item){.item = i2it(i)}, key_str);
        e = e->next;
        i++;
    }
    return result;
}

// =============================================================================
// js_to_string_val — convert any value to string (returns Item)
// =============================================================================

extern "C" Item js_to_string_val(Item value) {
    return js_to_string(value);
}

// =============================================================================
// Number property access — MAX_SAFE_INTEGER, MIN_SAFE_INTEGER, etc.
// =============================================================================

static Item make_double(double val) {
    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = val;
    return (Item){.item = d2it(fp)};
}

extern "C" Item js_number_property(Item prop_name) {
    TypeId type = get_type_id(prop_name);
    if (type != LMD_TYPE_STRING) return ItemNull;

    String* s = it2s(prop_name);
    if (!s) return ItemNull;

    if (s->len == 16 && strncmp(s->chars, "MAX_SAFE_INTEGER", 16) == 0) return make_double(9007199254740991.0);
    if (s->len == 16 && strncmp(s->chars, "MIN_SAFE_INTEGER", 16) == 0) return make_double(-9007199254740991.0);
    if (s->len == 9 && strncmp(s->chars, "MAX_VALUE", 9) == 0) return make_double(1.7976931348623157e+308);
    if (s->len == 9 && strncmp(s->chars, "MIN_VALUE", 9) == 0) return make_double(5e-324);
    if (s->len == 17 && strncmp(s->chars, "POSITIVE_INFINITY", 17) == 0) return make_double(1.0/0.0);
    if (s->len == 17 && strncmp(s->chars, "NEGATIVE_INFINITY", 17) == 0) return make_double(-1.0/0.0);
    if (s->len == 3 && strncmp(s->chars, "NaN", 3) == 0) return make_double(0.0/0.0);
    if (s->len == 7 && strncmp(s->chars, "EPSILON", 7) == 0) return make_double(2.220446049250313e-16);

    return ItemNull;
}

// =============================================================================
// Object.values — return array of property values
// =============================================================================

extern "C" Item js_object_values(Item object) {
    TypeId type = get_type_id(object);
    if (type != LMD_TYPE_MAP) return js_array_new(0);

    Map* m = object.map;
    if (!m || !m->type) return js_array_new(0);

    TypeMap* tm = (TypeMap*)m->type;
    Item result = js_array_new(0);
    ShapeEntry* e = tm->shape;
    while (e) {
        // skip internal properties
        if (e->name && e->name->length >= 2 &&
            e->name->str[0] == '_' && e->name->str[1] == '_') {
            e = e->next;
            continue;
        }
        Item key_str = (Item){.item = s2it(heap_create_name(e->name->str, (int)e->name->length))};
        Item val = map_get(m, key_str);
        js_array_push(result, val);
        e = e->next;
    }
    return result;
}

// =============================================================================
// Object.entries — return array of [key, value] pairs
// =============================================================================

extern "C" Item js_object_entries(Item object) {
    TypeId type = get_type_id(object);
    if (type != LMD_TYPE_MAP) return js_array_new(0);

    Map* m = object.map;
    if (!m || !m->type) return js_array_new(0);

    TypeMap* tm = (TypeMap*)m->type;
    Item result = js_array_new(0);
    ShapeEntry* e = tm->shape;
    while (e) {
        if (e->name && e->name->length >= 2 &&
            e->name->str[0] == '_' && e->name->str[1] == '_') {
            e = e->next;
            continue;
        }
        Item pair = js_array_new(2);
        char nbuf[256];
        int nlen = (int)e->name->length < 255 ? (int)e->name->length : 255;
        memcpy(nbuf, e->name->str, nlen);
        nbuf[nlen] = '\0';
        Item key_str = (Item){.item = s2it(heap_create_name(nbuf))};
        js_array_set(pair, (Item){.item = i2it(0)}, key_str);
        js_array_set(pair, (Item){.item = i2it(1)}, map_get(m, key_str));
        js_array_push(result, pair);
        e = e->next;
    }
    return result;
}

// =============================================================================
// Object.assign(target, ...sources)
// =============================================================================

extern "C" Item js_object_assign(Item target, Item* sources, int count) {
    if (get_type_id(target) != LMD_TYPE_MAP) return target;
    for (int i = 0; i < count; i++) {
        Item source = sources[i];
        if (get_type_id(source) != LMD_TYPE_MAP) continue;
        Map* m = source.map;
        if (!m || !m->type) continue;
        TypeMap* tm = (TypeMap*)m->type;
        ShapeEntry* e = tm->shape;
        while (e) {
            if (e->name) {
                Item key = (Item){.item = s2it(heap_create_name(e->name->str, (int)e->name->length))};
                Item val = map_get(m, key);
                js_property_set(target, key, val);
            }
            e = e->next;
        }
    }
    return target;
}

// =============================================================================
// obj.hasOwnProperty(key) / Object.hasOwn(obj, key)
// =============================================================================

extern "C" Item js_has_own_property(Item obj, Item key) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    Item k = js_to_string(key);
    // direct map_get — no prototype chain walk
    Item val = map_get(obj.map, k);
    return (Item){.item = b2it(val.item != ItemNull.item)};
}

// =============================================================================
// Object.freeze(obj) — set __frozen__ flag, Object.isFrozen(obj)
// =============================================================================

extern "C" Item js_object_freeze(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return obj;
    Item key = (Item){.item = s2it(heap_create_name("__frozen__", 10))};
    js_property_set(obj, key, (Item){.item = b2it(true)});
    return obj;
}

extern "C" Item js_object_is_frozen(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(true)};
    Item key = (Item){.item = s2it(heap_create_name("__frozen__", 10))};
    Item val = map_get(obj.map, key);
    return (Item){.item = b2it(js_is_truthy(val))};
}

// =============================================================================
// Number static methods — Number.isInteger, Number.isFinite, Number.isNaN, Number.isSafeInteger
// =============================================================================

extern "C" Item js_number_is_integer(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64) return (Item){.item = b2it(true)};
    if (type == LMD_TYPE_FLOAT) {
        double d = it2d(value);
        return (Item){.item = b2it(isfinite(d) && d == floor(d))};
    }
    return (Item){.item = b2it(false)};
}

extern "C" Item js_number_is_finite(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64) return (Item){.item = b2it(true)};
    if (type == LMD_TYPE_FLOAT) {
        return (Item){.item = b2it(isfinite(it2d(value)))};
    }
    return (Item){.item = b2it(false)};
}

extern "C" Item js_number_is_nan(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_FLOAT) {
        return (Item){.item = b2it(isnan(it2d(value)))};
    }
    return (Item){.item = b2it(false)};
}

extern "C" Item js_number_is_safe_integer(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64) return (Item){.item = b2it(true)};
    if (type == LMD_TYPE_FLOAT) {
        double d = it2d(value);
        return (Item){.item = b2it(isfinite(d) && d == floor(d) && fabs(d) <= 9007199254740991.0)};
    }
    return (Item){.item = b2it(false)};
}

// =============================================================================
// Array.from(iterable) — convert array-like to array
// =============================================================================

extern "C" Item js_array_from(Item iterable) {
    TypeId tid = get_type_id(iterable);
    if (tid == LMD_TYPE_ARRAY) {
        // shallow copy
        Array* src = iterable.array;
        Item result = js_array_new(src->length);
        Array* dst = result.array;
        memcpy(dst->items, src->items, src->length * sizeof(Item));
        dst->length = src->length;
        return result;
    }
    if (tid == LMD_TYPE_STRING) {
        // split string into array of single characters
        String* s = it2s(iterable);
        if (!s) return js_array_new(0);
        Item result = js_array_new(0);
        for (int i = 0; i < (int)s->len; i++) {
            String* ch = heap_strcpy(&s->chars[i], 1);
            js_array_push(result, (Item){.item = s2it(ch)});
        }
        return result;
    }
    return js_array_new(0);
}

// =============================================================================
// JSON.parse(str) — parse JSON string to Lambda object
// =============================================================================

extern Input* js_input;

extern "C" Item js_json_parse(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) return ItemNull;

    // null-terminate for the parser
    char* buf = (char*)alloca(s->len + 1);
    memcpy(buf, s->chars, s->len);
    buf[s->len] = '\0';

    if (!js_input) {
        log_error("js_json_parse: no input context");
        return ItemNull;
    }

    Item result = parse_json_to_item(js_input, buf);
    return result;
}

// =============================================================================
// JSON.stringify(value, replacer?, space?) — convert Lambda object to JSON string
// =============================================================================

extern "C" Item js_json_stringify(Item value) {
    if (get_type_id(value) == LMD_TYPE_NULL) {
        return (Item){.item = s2it(heap_create_name("null", 4))};
    }

    Pool* pool = pool_create();
    String* json = format_json(pool, value);
    if (!json) {
        pool_destroy(pool);
        return ItemNull;
    }

    // copy string to GC heap before destroying pool
    String* result = heap_strcpy(json->chars, json->len);
    pool_destroy(pool);
    return (Item){.item = s2it(result)};
}

// =============================================================================
// delete operator — remove property from object
// =============================================================================

extern "C" Item js_delete_property(Item obj, Item key) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(true)};
    // set property to null (real deletion would require map_remove)
    js_property_set(obj, key, ItemNull);
    return (Item){.item = b2it(true)};
}