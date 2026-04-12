// rb_runtime.cpp — Ruby runtime functions for LambdaRuby transpiler
// All functions operate on Lambda Item values (64-bit tagged pointers).
#include "rb_runtime.h"
#include "rb_transpiler.hpp"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"

#include <cstring>
#include <cstdio>

// forward declaration from rb_class.cpp
extern "C" Item rb_call_spaceship(Item left, Item right);
#include "../../lib/mem.h"
#include <cmath>
#include <re2/re2.h>

// push_d boxes a double into an Item via GC nursery allocation (C linkage)
extern "C" Item push_d(double dval);

// forward declarations
extern String* heap_create_name(const char* str, size_t len);

// global state
static void* rb_input = NULL;
static const char* rb_current_file = NULL;  // current executing file path
static void* rb_runtime_ptr = NULL;  // Runtime pointer for require_relative

#define RB_MODULE_VAR_MAX 1024
static Item rb_module_vars[RB_MODULE_VAR_MAX];

// exception state — simple flag-based model (same pattern as Python transpiler)
static bool rb_exception_pending = false;
static Item rb_exception_value = {0};

// ============================================================================
// Runtime initialization
// ============================================================================

extern "C" void rb_runtime_set_input(void* input_ptr) {
    rb_input = input_ptr;
    // register static Item array as GC root (BSS memory invisible to stack scanning)
    static bool statics_rooted = false;
    if (!statics_rooted) {
        heap_register_gc_root_range((uint64_t*)rb_module_vars, RB_MODULE_VAR_MAX);
        heap_register_gc_root(&rb_exception_value.item);
        statics_rooted = true;
    }
    // clear exception state on new script execution
    rb_exception_pending = false;
    rb_exception_value = (Item){.item = ITEM_NULL};
}

extern "C" void rb_runtime_set_current_file(const char* path) {
    rb_current_file = path;
}

extern "C" void rb_runtime_set_runtime(void* runtime) {
    rb_runtime_ptr = runtime;
}

// ============================================================================
// Module variables
// ============================================================================

extern "C" void rb_set_module_var(int index, Item value) {
    if (index >= 0 && index < RB_MODULE_VAR_MAX) {
        rb_module_vars[index] = value;
    }
}

extern "C" Item rb_get_module_var(int index) {
    if (index >= 0 && index < RB_MODULE_VAR_MAX) {
        return rb_module_vars[index];
    }
    return (Item){.item = ITEM_NULL};
}

extern "C" void rb_reset_module_vars(void) {
    memset(rb_module_vars, 0, sizeof(rb_module_vars));
}

// ============================================================================
// Type conversion
// ============================================================================

static double rb_get_number(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT) return (double)it2i(value);
    if (type == LMD_TYPE_INT64) return (double)it2i(value);
    if (type == LMD_TYPE_FLOAT) return it2d(value);
    return 0.0;
}

extern "C" Item rb_to_int(Item value) {
    TypeId type = get_type_id(value);
    switch (type) {
        case LMD_TYPE_NULL: return (Item){.item = ITEM_NULL};  // nil.to_i raises NoMethodError in Ruby, but return null
        case LMD_TYPE_BOOL: return (Item){.item = i2it(it2b(value) ? 1 : 0)};
        case LMD_TYPE_INT: return value;
        case LMD_TYPE_INT64: return value;
        case LMD_TYPE_FLOAT: return (Item){.item = i2it((int64_t)it2d(value))};
        case LMD_TYPE_STRING: {
            String* str = it2s(value);
            if (!str || !str->chars) return (Item){.item = i2it(0)};
            char* end;
            long long val = strtoll(str->chars, &end, 10);
            if (end == str->chars) return (Item){.item = i2it(0)};
            return (Item){.item = i2it(val)};
        }
        default:
            return (Item){.item = i2it(0)};
    }
}

extern "C" Item rb_to_float(Item value) {
    TypeId type = get_type_id(value);
    switch (type) {
        case LMD_TYPE_NULL: return (Item)push_d(0.0);
        case LMD_TYPE_BOOL: return (Item)push_d(it2b(value) ? 1.0 : 0.0);
        case LMD_TYPE_INT: return (Item)push_d((double)it2i(value));
        case LMD_TYPE_INT64: return (Item)push_d((double)it2i(value));
        case LMD_TYPE_FLOAT: return value;
        case LMD_TYPE_STRING: {
            String* str = it2s(value);
            if (!str || !str->chars) return (Item)push_d(0.0);
            char* end;
            double val = strtod(str->chars, &end);
            if (end == str->chars) return (Item)push_d(0.0);
            return (Item)push_d(val);
        }
        default:
            return (Item)push_d(0.0);
    }
}

extern "C" Item rb_to_str(Item value) {
    TypeId type = get_type_id(value);
    switch (type) {
        case LMD_TYPE_NULL:
            return (Item){.item = s2it(heap_create_name(""))};
        case LMD_TYPE_BOOL:
            return (Item){.item = s2it(heap_create_name(it2b(value) ? "true" : "false"))};
        case LMD_TYPE_INT: {
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", (long long)it2i(value));
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        case LMD_TYPE_INT64: {
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", (long long)it2i(value));
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        case LMD_TYPE_FLOAT: {
            char buf[64];
            double d = it2d(value);
            if (d == (double)(int64_t)d && !isinf(d) && !isnan(d)) {
                snprintf(buf, sizeof(buf), "%.1f", d);
            } else {
                snprintf(buf, sizeof(buf), "%g", d);
            }
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        case LMD_TYPE_STRING:
            return value;
        case LMD_TYPE_SYMBOL: {
            // Symbol has same layout as String in Lambda
            String* sym = it2s(value);
            if (sym && sym->chars) {
                return (Item){.item = s2it(heap_create_name(sym->chars))};
            }
            return (Item){.item = s2it(heap_create_name(""))};
        }
        default: {
            // fallback: format as inspection string
            StrBuf* buf = strbuf_new_cap(64);
            format_item(buf, value, 0, (char*)"  ");
            Item result = (Item){.item = s2it(heap_create_name(buf->str))};
            strbuf_free(buf);
            return result;
        }
    }
}

extern "C" Item rb_to_bool(Item value) {
    return (Item){.item = b2it(rb_is_truthy(value))};
}

// Ruby truthiness: only false and nil are falsy
extern "C" int rb_is_truthy(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_NULL) return 0;
    if (type == LMD_TYPE_BOOL) return it2b(value) ? 1 : 0;
    return 1;  // everything else is truthy (including 0, "", [])
}

// ============================================================================
// Arithmetic operators
// ============================================================================

extern "C" Item rb_add(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);

    // int + int
    if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) {
        return (Item){.item = i2it(it2i(left) + it2i(right))};
    }
    // float arithmetic
    if ((lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT || lt == LMD_TYPE_INT64) &&
        (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT || rt == LMD_TYPE_INT64)) {
        return (Item)push_d(rb_get_number(left) + rb_get_number(right));
    }
    // string concatenation
    if (lt == LMD_TYPE_STRING && rt == LMD_TYPE_STRING) {
        return rb_string_concat(left, right);
    }
    // array concatenation
    if (lt == LMD_TYPE_ARRAY && rt == LMD_TYPE_ARRAY) {
        Array* a = it2arr(left);
        Array* b = it2arr(right);
        Item result = rb_array_new();
        if (a) {
            for (int64_t i = 0; i < a->length; i++) {
                rb_array_push(result, a->items[i]);
            }
        }
        if (b) {
            for (int64_t i = 0; i < b->length; i++) {
                rb_array_push(result, b->items[i]);
            }
        }
        return result;
    }
    log_error("rb: TypeError: no implicit conversion in rb_add");
    return (Item){.item = ITEM_NULL};
}

extern "C" Item rb_subtract(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);
    if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) {
        return (Item){.item = i2it(it2i(left) - it2i(right))};
    }
    if ((lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT || lt == LMD_TYPE_INT64) &&
        (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT || rt == LMD_TYPE_INT64)) {
        return (Item)push_d(rb_get_number(left) - rb_get_number(right));
    }
    log_error("rb: TypeError: no implicit conversion in rb_subtract");
    return (Item){.item = ITEM_NULL};
}

extern "C" Item rb_multiply(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);
    if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) {
        return (Item){.item = i2it(it2i(left) * it2i(right))};
    }
    if ((lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT || lt == LMD_TYPE_INT64) &&
        (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT || rt == LMD_TYPE_INT64)) {
        return (Item)push_d(rb_get_number(left) * rb_get_number(right));
    }
    // string * int → repetition
    if (lt == LMD_TYPE_STRING && rt == LMD_TYPE_INT) {
        return rb_string_repeat(left, right);
    }
    log_error("rb: TypeError: no implicit conversion in rb_multiply");
    return (Item){.item = ITEM_NULL};
}

extern "C" Item rb_divide(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);
    // Ruby integer division truncates toward negative infinity (same as Python)
    if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) {
        int64_t a = it2i(left);
        int64_t b = it2i(right);
        if (b == 0) {
            log_error("rb: ZeroDivisionError: divided by 0");
            return (Item){.item = ITEM_NULL};
        }
        // Ruby's integer division rounds toward negative infinity
        int64_t q = a / b;
        if ((a ^ b) < 0 && q * b != a) q--;
        return (Item){.item = i2it(q)};
    }
    if ((lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT || lt == LMD_TYPE_INT64) &&
        (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT || rt == LMD_TYPE_INT64)) {
        double d = rb_get_number(right);
        if (d == 0.0) {
            log_error("rb: ZeroDivisionError: divided by 0");
            return (Item){.item = ITEM_NULL};
        }
        return (Item)push_d(rb_get_number(left) / d);
    }
    log_error("rb: TypeError: no implicit conversion in rb_divide");
    return (Item){.item = ITEM_NULL};
}

extern "C" Item rb_modulo(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);
    if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) {
        int64_t a = it2i(left);
        int64_t b = it2i(right);
        if (b == 0) {
            log_error("rb: ZeroDivisionError: divided by 0");
            return (Item){.item = ITEM_NULL};
        }
        int64_t r = a % b;
        if ((r != 0) && ((r ^ b) < 0)) r += b;
        return (Item){.item = i2it(r)};
    }
    if ((lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT || lt == LMD_TYPE_INT64) &&
        (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT || rt == LMD_TYPE_INT64)) {
        double b = rb_get_number(right);
        if (b == 0.0) {
            log_error("rb: ZeroDivisionError: divided by 0");
            return (Item){.item = ITEM_NULL};
        }
        return (Item)push_d(fmod(rb_get_number(left), b));
    }
    log_error("rb: TypeError: no implicit conversion in rb_modulo");
    return (Item){.item = ITEM_NULL};
}

extern "C" Item rb_power(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);
    if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) {
        int64_t exp = it2i(right);
        if (exp < 0) {
            return (Item)push_d(pow((double)it2i(left), (double)exp));
        }
        int64_t base = it2i(left);
        int64_t result = 1;
        for (int64_t i = 0; i < exp; i++) {
            result *= base;
        }
        return (Item){.item = i2it(result)};
    }
    return (Item)push_d(pow(rb_get_number(left), rb_get_number(right)));
}

extern "C" Item rb_negate(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT) return (Item){.item = i2it(-it2i(value))};
    if (type == LMD_TYPE_INT64) return (Item){.item = i2it(-it2i(value))};
    if (type == LMD_TYPE_FLOAT) return (Item)push_d(-it2d(value));
    return (Item){.item = ITEM_NULL};
}

extern "C" Item rb_positive(Item value) {
    return value;
}

extern "C" Item rb_bit_not(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT) return (Item){.item = i2it(~it2i(value))};
    return (Item){.item = ITEM_NULL};
}

// ============================================================================
// Bitwise operators
// ============================================================================

extern "C" Item rb_bit_and(Item left, Item right) {
    return (Item){.item = i2it(it2i(left) & it2i(right))};
}

extern "C" Item rb_bit_or(Item left, Item right) {
    return (Item){.item = i2it(it2i(left) | it2i(right))};
}

extern "C" Item rb_bit_xor(Item left, Item right) {
    return (Item){.item = i2it(it2i(left) ^ it2i(right))};
}

extern "C" Item rb_lshift(Item left, Item right) {
    return (Item){.item = i2it(it2i(left) << it2i(right))};
}

extern "C" Item rb_rshift(Item left, Item right) {
    return (Item){.item = i2it(it2i(left) >> it2i(right))};
}

// ============================================================================
// Comparison operators
// ============================================================================

extern "C" Item rb_eq(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);
    if (lt == LMD_TYPE_NULL && rt == LMD_TYPE_NULL) return (Item){.item = b2it(true)};
    if (lt == LMD_TYPE_NULL || rt == LMD_TYPE_NULL) return (Item){.item = b2it(false)};
    if (lt == LMD_TYPE_BOOL && rt == LMD_TYPE_BOOL) return (Item){.item = b2it(it2b(left) == it2b(right))};
    if ((lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT || lt == LMD_TYPE_INT64) &&
        (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT || rt == LMD_TYPE_INT64)) {
        return (Item){.item = b2it(rb_get_number(left) == rb_get_number(right))};
    }
    if (lt == LMD_TYPE_STRING && rt == LMD_TYPE_STRING) {
        String* a = it2s(left);
        String* b = it2s(right);
        if (!a || !b) return (Item){.item = b2it(a == b)};
        if (a->len != b->len) return (Item){.item = b2it(false)};
        return (Item){.item = b2it(memcmp(a->chars, b->chars, a->len) == 0)};
    }
    if (lt == LMD_TYPE_SYMBOL && rt == LMD_TYPE_SYMBOL) {
        return (Item){.item = b2it(left.item == right.item)};
    }
    return (Item){.item = b2it(left.item == right.item)};
}

extern "C" Item rb_ne(Item left, Item right) {
    return (Item){.item = b2it(!it2b(rb_eq(left, right)))};
}

extern "C" Item rb_lt(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);
    if ((lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT || lt == LMD_TYPE_INT64) &&
        (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT || rt == LMD_TYPE_INT64)) {
        return (Item){.item = b2it(rb_get_number(left) < rb_get_number(right))};
    }
    if (lt == LMD_TYPE_STRING && rt == LMD_TYPE_STRING) {
        String* a = it2s(left);
        String* b = it2s(right);
        int cmp = memcmp(a->chars, b->chars, a->len < b->len ? a->len : b->len);
        if (cmp == 0) return (Item){.item = b2it(a->len < b->len)};
        return (Item){.item = b2it(cmp < 0)};
    }
    // fallback: try custom <=> method on objects
    if (lt == LMD_TYPE_MAP) {
        Item cmp = rb_call_spaceship(left, right);
        if (cmp.item != ITEM_NULL && get_type_id(cmp) == LMD_TYPE_INT) {
            return (Item){.item = b2it(it2i(cmp) < 0)};
        }
    }
    return (Item){.item = ITEM_NULL};
}

extern "C" Item rb_le(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);
    if ((lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT || lt == LMD_TYPE_INT64) &&
        (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT || rt == LMD_TYPE_INT64)) {
        return (Item){.item = b2it(rb_get_number(left) <= rb_get_number(right))};
    }
    if (lt == LMD_TYPE_STRING && rt == LMD_TYPE_STRING) {
        String* a = it2s(left);
        String* b = it2s(right);
        int cmp = memcmp(a->chars, b->chars, a->len < b->len ? a->len : b->len);
        if (cmp == 0) return (Item){.item = b2it(a->len <= b->len)};
        return (Item){.item = b2it(cmp <= 0)};
    }
    // fallback: try custom <=> method on objects
    if (lt == LMD_TYPE_MAP) {
        Item cmp = rb_call_spaceship(left, right);
        if (cmp.item != ITEM_NULL && get_type_id(cmp) == LMD_TYPE_INT) {
            return (Item){.item = b2it(it2i(cmp) <= 0)};
        }
    }
    return (Item){.item = ITEM_NULL};
}

extern "C" Item rb_gt(Item left, Item right) {
    return rb_lt(right, left);
}

extern "C" Item rb_ge(Item left, Item right) {
    return rb_le(right, left);
}

extern "C" Item rb_cmp(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);
    if ((lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT || lt == LMD_TYPE_INT64) &&
        (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT || rt == LMD_TYPE_INT64)) {
        double a = rb_get_number(left);
        double b = rb_get_number(right);
        if (a < b) return (Item){.item = i2it(-1)};
        if (a > b) return (Item){.item = i2it(1)};
        return (Item){.item = i2it(0)};
    }
    if (lt == LMD_TYPE_STRING && rt == LMD_TYPE_STRING) {
        String* a = it2s(left);
        String* b = it2s(right);
        int cmp = memcmp(a->chars, b->chars, a->len < b->len ? a->len : b->len);
        if (cmp == 0) {
            if (a->len < b->len) return (Item){.item = i2it(-1)};
            if (a->len > b->len) return (Item){.item = i2it(1)};
            return (Item){.item = i2it(0)};
        }
        return (Item){.item = i2it(cmp < 0 ? -1 : 1)};
    }
    // fallback: try custom <=> method on objects
    if (lt == LMD_TYPE_MAP) {
        return rb_call_spaceship(left, right);
    }
    return (Item){.item = ITEM_NULL};
}

extern "C" Item rb_case_eq(Item left, Item right) {
    // default implementation: same as ==
    return rb_eq(left, right);
}

// ============================================================================
// Object/attribute operations
// ============================================================================

extern "C" Item rb_new_object(void) {
    Map* m = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    m->type_id = LMD_TYPE_MAP;
    extern TypeMap EmptyMap;
    m->type = &EmptyMap;
    return (Item){.map = m};
}

extern "C" Item rb_getattr(Item object, Item name) {
    TypeId type = get_type_id(object);
    if (type == LMD_TYPE_MAP) {
        return map_get(object.map, name);
    }
    return (Item){.item = ITEM_NULL};
}

extern "C" void rb_setattr(Item object, Item name, Item value) {
    TypeId type = get_type_id(object);
    if (type != LMD_TYPE_MAP) {
        log_error("rb_setattr: not a map (type=%d)", type);
        return;
    }
    Map* m = object.map;
    TypeMap* map_type = (TypeMap*)m->type;

    // try updating existing key via fn_map_set
    if (map_type && map_type->shape) {
        String* str_key = it2s(name);
        if (str_key) {
            ShapeEntry* found = (map_type->field_count > 0)
                ? typemap_hash_lookup(map_type, str_key->chars, (int)str_key->len)
                : nullptr;
            if (!found) {
                ShapeEntry* entry = map_type->shape;
                while (entry) {
                    if (entry->name && entry->name->length == (size_t)str_key->len
                        && strncmp(entry->name->str, str_key->chars, str_key->len) == 0) {
                        found = entry;
                        break;
                    }
                    entry = entry->next;
                }
            }
            if (found) {
                fn_map_set(object, name, value);
                return;
            }
        }
    }

    // add new key via map_put
    Input* input = (Input*)rb_input;
    if (!input) {
        log_error("rb_setattr: no rb_input context");
        return;
    }
    String* str_key = it2s(name);
    if (str_key) {
        map_put(m, str_key, value, input);
    }
}

// ============================================================================
// Collection operations
// ============================================================================

extern "C" Item rb_array_new(void) {
    Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
    arr->type_id = LMD_TYPE_ARRAY;
    arr->items = NULL;
    arr->length = 0;
    arr->capacity = 0;
    return (Item){.array = arr};
}

extern "C" Item rb_array_push(Item array, Item value) {
    Array* arr = it2arr(array);
    if (!arr) return array;
    if (arr->length >= arr->capacity) {
        int64_t new_cap = arr->capacity < 8 ? 8 : arr->capacity * 2;
        arr->items = (Item*)mem_realloc(arr->items, new_cap * sizeof(Item), MEM_CAT_RB_RUNTIME);
        arr->capacity = new_cap;
    }
    arr->items[arr->length++] = value;
    return array;
}

extern "C" Item rb_array_get(Item array, Item index) {
    Array* arr = it2arr(array);
    if (!arr) return (Item){.item = ITEM_NULL};
    int64_t idx = it2i(index);
    if (idx < 0) idx += arr->length;
    if (idx < 0 || idx >= arr->length) return (Item){.item = ITEM_NULL};
    return arr->items[idx];
}

extern "C" Item rb_array_set(Item array, Item index, Item value) {
    Array* arr = it2arr(array);
    if (!arr) return (Item){.item = ITEM_NULL};
    int64_t idx = it2i(index);
    if (idx < 0) idx += arr->length;
    if (idx < 0 || idx >= arr->length) return (Item){.item = ITEM_NULL};
    arr->items[idx] = value;
    return value;
}

extern "C" Item rb_array_length(Item array) {
    Array* arr = it2arr(array);
    if (!arr) return (Item){.item = i2it(0)};
    return (Item){.item = i2it(arr->length)};
}

extern "C" Item rb_array_pop(Item array) {
    Array* arr = it2arr(array);
    if (!arr || arr->length == 0) return (Item){.item = ITEM_NULL};
    return arr->items[--arr->length];
}

extern "C" Item rb_hash_new(void) {
    return rb_new_object();
}

extern "C" Item rb_hash_get(Item hash, Item key) {
    return rb_getattr(hash, key);
}

extern "C" Item rb_hash_set(Item hash, Item key, Item value) {
    rb_setattr(hash, key, value);
    return value;
}

extern "C" Item rb_range_new(Item start, Item end, int exclusive) {
    Range* r = (Range*)heap_calloc(sizeof(Range), LMD_TYPE_RANGE);
    r->type_id = LMD_TYPE_RANGE;
    int64_t s = it2i(start);
    int64_t e = it2i(end);
    if (exclusive) e = e - 1;  // convert exclusive to inclusive end
    r->start = s;
    r->end = e;
    r->length = (e >= s) ? (e - s + 1) : 0;
    return (Item){.range = r};
}

// ============================================================================
// Subscript operations
// ============================================================================

extern "C" Item rb_subscript_get(Item object, Item key) {
    TypeId type = get_type_id(object);
    if (type == LMD_TYPE_ARRAY) {
        return rb_array_get(object, key);
    }
    if (type == LMD_TYPE_MAP) {
        return rb_getattr(object, key);
    }
    if (type == LMD_TYPE_STRING) {
        String* str = it2s(object);
        int64_t idx = it2i(key);
        if (idx < 0) idx += str->len;
        if (idx < 0 || idx >= (int64_t)str->len) return (Item){.item = ITEM_NULL};
        return (Item){.item = s2it(heap_create_name(str->chars + idx, 1))};
    }
    return (Item){.item = ITEM_NULL};
}

extern "C" Item rb_subscript_set(Item object, Item key, Item value) {
    TypeId type = get_type_id(object);
    if (type == LMD_TYPE_ARRAY) {
        return rb_array_set(object, key, value);
    }
    if (type == LMD_TYPE_MAP) {
        rb_setattr(object, key, value);
        return value;
    }
    return (Item){.item = ITEM_NULL};
}

// ============================================================================
// String operations
// ============================================================================

extern "C" Item rb_string_concat(Item left, Item right) {
    String* a = it2s(left);
    String* b = it2s(right);
    if (!a && !b) return (Item){.item = s2it(heap_create_name(""))};
    if (!a) return right;
    if (!b) return left;
    StrBuf* buf = strbuf_new_cap(a->len + b->len);
    strbuf_append_str_n(buf, a->chars, a->len);
    strbuf_append_str_n(buf, b->chars, b->len);
    Item result = (Item){.item = s2it(heap_create_name(buf->str))};
    strbuf_free(buf);
    return result;
}

extern "C" Item rb_string_repeat(Item str, Item count) {
    String* s = it2s(str);
    int64_t n = it2i(count);
    if (!s || n <= 0) return (Item){.item = s2it(heap_create_name(""))};
    StrBuf* buf = strbuf_new_cap(s->len * n);
    for (int64_t i = 0; i < n; i++) {
        strbuf_append_str_n(buf, s->chars, s->len);
    }
    Item result = (Item){.item = s2it(heap_create_name(buf->str))};
    strbuf_free(buf);
    return result;
}

extern "C" Item rb_format_value(Item value, Item format_spec) {
    (void)format_spec;
    return rb_to_str(value);
}

// ============================================================================
// Iterator protocol
// ============================================================================

// simple array iterator: stored as a 2-element array [collection, index]
extern "C" Item rb_get_iterator(Item collection) {
    Item iter = rb_array_new();
    rb_array_push(iter, collection);
    rb_array_push(iter, (Item){.item = i2it(0)});
    return iter;
}

extern "C" Item rb_iterator_next(Item iterator) {
    Array* iter = it2arr(iterator);
    if (!iter || iter->length < 2) return (Item){.item = ITEM_NULL};
    Item collection = iter->items[0];
    int64_t idx = it2i(iter->items[1]);
    TypeId coll_type = get_type_id(collection);

    if (coll_type == LMD_TYPE_ARRAY) {
        Array* arr = it2arr(collection);
        if (!arr || idx >= arr->length) {
            // signal stop
            return (Item){.item = ITEM_ERROR};
        }
        iter->items[1] = (Item){.item = i2it(idx + 1)};
        return arr->items[idx];
    }
    if (coll_type == LMD_TYPE_RANGE) {
        Range* range = it2range(collection);
        int64_t end_val = range->end;
        if (idx > end_val) return (Item){.item = ITEM_ERROR};
        iter->items[1] = (Item){.item = i2it(idx + 1)};
        return (Item){.item = i2it(idx)};
    }
    return (Item){.item = ITEM_ERROR};
}

extern "C" int rb_is_stop_iteration(Item value) {
    return value.item == ITEM_ERROR;
}

// ============================================================================
// Output
// ============================================================================

extern "C" Item rb_puts(Item* args, int argc) {
    if (argc == 0) {
        printf("\n");
        return (Item){.item = ITEM_NULL};
    }
    for (int i = 0; i < argc; i++) {
        Item str = rb_to_str(args[i]);
        TypeId type = get_type_id(str);
        if (type == LMD_TYPE_STRING) {
            String* s = it2s(str);
            if (s && s->chars && s->len > 0) {
                printf("%.*s", (int)s->len, s->chars);
                // Ruby: puts suppresses trailing newline if string already ends with \n
                if (s->chars[s->len - 1] != '\n') {
                    printf("\n");
                }
            } else {
                printf("\n");
            }
        } else {
            printf("\n");
        }
    }
    return (Item){.item = ITEM_NULL};
}

extern "C" Item rb_print(Item* args, int argc) {
    for (int i = 0; i < argc; i++) {
        Item str = rb_to_str(args[i]);
        TypeId type = get_type_id(str);
        if (type == LMD_TYPE_STRING) {
            String* s = it2s(str);
            if (s && s->chars) {
                printf("%.*s", (int)s->len, s->chars);
            }
        }
    }
    return (Item){.item = ITEM_NULL};
}

// p: prints inspect representation with newline
extern "C" Item rb_p(Item* args, int argc) {
    for (int i = 0; i < argc; i++) {
        TypeId type = get_type_id(args[i]);
        if (type == LMD_TYPE_STRING) {
            String* s = it2s(args[i]);
            printf("\"%.*s\"", (int)s->len, s->chars);
        } else if (type == LMD_TYPE_NULL) {
            printf("nil");
        } else if (type == LMD_TYPE_SYMBOL) {
            String* sym = it2s(args[i]);
            printf(":%.*s", (int)sym->len, sym->chars);
        } else {
            Item str = rb_to_str(args[i]);
            String* s = it2s(str);
            if (s && s->chars) {
                printf("%.*s", (int)s->len, s->chars);
            }
        }
        printf("\n");
    }
    if (argc == 1) return args[0];
    return (Item){.item = ITEM_NULL};
}

// ============================================================================
// Built-in functions
// ============================================================================

extern "C" Item rb_builtin_len(Item obj) {
    TypeId type = get_type_id(obj);
    switch (type) {
        case LMD_TYPE_STRING: {
            String* s = it2s(obj);
            return (Item){.item = i2it(s ? s->len : 0)};
        }
        case LMD_TYPE_ARRAY: {
            Array* arr = it2arr(obj);
            return (Item){.item = i2it(arr ? arr->length : 0)};
        }
        default:
            return (Item){.item = i2it(0)};
    }
}

extern "C" Item rb_builtin_type(Item obj) {
    TypeId type = get_type_id(obj);
    const char* name;
    switch (type) {
        case LMD_TYPE_NULL: name = "NilClass"; break;
        case LMD_TYPE_BOOL: name = it2b(obj) ? "TrueClass" : "FalseClass"; break;
        case LMD_TYPE_INT:
        case LMD_TYPE_INT64: name = "Integer"; break;
        case LMD_TYPE_FLOAT: name = "Float"; break;
        case LMD_TYPE_STRING: name = "String"; break;
        case LMD_TYPE_SYMBOL: name = "Symbol"; break;
        case LMD_TYPE_ARRAY: name = "Array"; break;
        case LMD_TYPE_MAP: name = "Hash"; break;
        case LMD_TYPE_RANGE: name = "Range"; break;
        case LMD_TYPE_FUNC: name = "Method"; break;
        default: name = "Object"; break;
    }
    return (Item){.item = s2it(heap_create_name(name))};
}

extern "C" Item rb_builtin_rand(Item max) {
    if (get_type_id(max) == LMD_TYPE_NULL) {
        return (Item)push_d((double)rand() / RAND_MAX);
    }
    int64_t n = it2i(max);
    if (n <= 0) return (Item){.item = i2it(0)};
    return (Item){.item = i2it(rand() % n)};
}

extern "C" Item rb_builtin_require_relative(Item path) {
    String* path_str = it2s(path);
    if (!path_str || !path_str->chars) {
        log_error("require_relative: invalid path argument");
        return (Item){.item = ITEM_NULL};
    }

    // resolve path relative to current file's directory
    char resolved[1024];
    if (rb_current_file) {
        // get directory of current file
        char dir[1024];
        strncpy(dir, rb_current_file, sizeof(dir) - 1);
        dir[sizeof(dir) - 1] = '\0';
        char* last_sep = strrchr(dir, '/');
        if (!last_sep) last_sep = strrchr(dir, '\\');
        if (last_sep) {
            *(last_sep + 1) = '\0';
        } else {
            dir[0] = '.';
            dir[1] = '/';
            dir[2] = '\0';
        }
        snprintf(resolved, sizeof(resolved), "%s%.*s", dir,
            (int)path_str->len, path_str->chars);
    } else {
        snprintf(resolved, sizeof(resolved), "%.*s",
            (int)path_str->len, path_str->chars);
    }

    // add .rb extension if no extension present
    const char* ext = strrchr(resolved, '.');
    if (!ext || (ext < strrchr(resolved, '/'))) {
        strncat(resolved, ".rb", sizeof(resolved) - strlen(resolved) - 1);
    }

    // read the file
    FILE* f = fopen(resolved, "r");
    if (!f) {
        log_error("require_relative: cannot open '%s'", resolved);
        return (Item){.item = ITEM_NULL};
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* source = (char*)mem_alloc(fsize + 1, MEM_CAT_RB_RUNTIME);
    if (!source) {
        fclose(f);
        log_error("require_relative: out of memory");
        return (Item){.item = ITEM_NULL};
    }
    size_t nread = fread(source, 1, fsize, f);
    source[nread] = '\0';
    fclose(f);

    // save and restore current file context
    const char* prev_file = rb_current_file;
    rb_current_file = resolved;

    // determine language by extension and compile+execute
    ext = strrchr(resolved, '.');
    Item result = (Item){.item = ITEM_NULL};

    if (ext && strcmp(ext, ".rb") == 0) {
        // Ruby file — use the transpile pipeline
        if (rb_runtime_ptr) {
            result = transpile_rb_to_mir((Runtime*)rb_runtime_ptr, source, resolved);
        } else {
            log_error("require_relative: no runtime available for .rb import");
        }
    } else {
        log_error("require_relative: unsupported file type '%s'", ext ? ext : "(none)");
    }

    mem_free(source);
    rb_current_file = prev_file;
    return result;
}

// defined?(var) runtime check — returns "local-variable" if non-nil, else nil
extern "C" Item rb_defined(Item value) {
    TypeId tid = get_type_id(value);
    if (tid == LMD_TYPE_NULL) return (Item){.item = ITEM_NULL};
    return (Item){.item = s2it(heap_create_name("local-variable", 14))};
}

// ============================================================================
// MIR-friendly single-arg wrappers for output functions
// ============================================================================

extern "C" Item rb_puts_one(Item value) {
    return rb_puts(&value, 1);
}

extern "C" Item rb_print_one(Item value) {
    return rb_print(&value, 1);
}

extern "C" Item rb_p_one(Item value) {
    return rb_p(&value, 1);
}

// Alias for rb_to_str matching Ruby convention
extern "C" Item rb_to_s(Item value) {
    return rb_to_str(value);
}

// Alias for rb_to_int matching Ruby convention
extern "C" Item rb_to_i(Item value) {
    return rb_to_int(value);
}

// Alias for rb_to_float matching Ruby convention
extern "C" Item rb_to_f(Item value) {
    return rb_to_float(value);
}

// ============================================================================
// Exception handling (Phase 4)
// ============================================================================

extern "C" void rb_raise(Item exception) {
    rb_exception_pending = true;
    rb_exception_value = exception;
}

extern "C" Item rb_check_exception(void) {
    return (Item){.item = b2it(rb_exception_pending)};
}

extern "C" Item rb_clear_exception(void) {
    rb_exception_pending = false;
    Item val = rb_exception_value;
    rb_exception_value = (Item){.item = ITEM_NULL};
    return val;
}

extern "C" Item rb_new_exception(Item type_name, Item message) {
    Item obj = rb_new_object();
    rb_setattr(obj, (Item){.item = s2it(heap_create_name("type", 4))}, type_name);
    rb_setattr(obj, (Item){.item = s2it(heap_create_name("message", 7))}, message);
    return obj;
}

extern "C" Item rb_exception_get_type(Item exception) {
    // if the exception is a plain string, treat it as RuntimeError message
    TypeId tid = get_type_id(exception);
    if (tid == LMD_TYPE_STRING) {
        return (Item){.item = s2it(heap_create_name("RuntimeError", 12))};
    }
    // for exception objects created by rb_new_exception, get the "type" attribute
    Item type_attr = rb_getattr(exception, (Item){.item = s2it(heap_create_name("type", 4))});
    if (get_type_id(type_attr) != LMD_TYPE_NULL) {
        return type_attr;
    }
    // for class instances (e.g. CustomError.new("msg")), use __rb_class__.__name__
    Item cls = rb_getattr(exception, (Item){.item = s2it(heap_create_name("__rb_class__", 12))});
    if (get_type_id(cls) != LMD_TYPE_NULL) {
        Item name = rb_getattr(cls, (Item){.item = s2it(heap_create_name("__name__", 8))});
        if (get_type_id(name) != LMD_TYPE_NULL) return name;
    }
    return (Item){.item = s2it(heap_create_name("RuntimeError", 12))};
}

extern "C" Item rb_exception_get_message(Item exception) {
    TypeId tid = get_type_id(exception);
    if (tid == LMD_TYPE_STRING) {
        return exception;
    }
    Item msg = rb_getattr(exception, (Item){.item = s2it(heap_create_name("message", 7))});
    if (get_type_id(msg) != LMD_TYPE_NULL) {
        return msg;
    }
    return (Item){.item = s2it(heap_create_name("", 0))};
}

extern "C" Item rb_respond_to(Item obj, Item method_name) {
    TypeId tid = get_type_id(obj);
    const char* mname = NULL;
    size_t mlen = 0;
    TypeId mtid = get_type_id(method_name);
    if (mtid == LMD_TYPE_STRING) {
        String* s = it2s(method_name);
        mname = s->chars;
        mlen = s->len;
    } else if (mtid == LMD_TYPE_SYMBOL) {
        String* s = it2s(method_name);
        mname = s->chars;
        mlen = s->len;
    }
    if (!mname) return (Item){.item = b2it(false)};

    // check built-in type methods
    // strings
    if (tid == LMD_TYPE_STRING) {
        Item test_args[1] = {method_name};
        Item result = rb_string_method(obj, method_name, test_args, 0);
        if (result.item != ((uint64_t)LMD_TYPE_ERROR << 56)) return (Item){.item = b2it(true)};
    }
    // arrays
    if (tid == LMD_TYPE_ARRAY) {
        Item test_args[1] = {method_name};
        Item result = rb_array_method(obj, method_name, test_args, 0);
        if (result.item != ((uint64_t)LMD_TYPE_ERROR << 56)) return (Item){.item = b2it(true)};
    }
    // integers
    if (tid == LMD_TYPE_INT || tid == LMD_TYPE_INT64) {
        Item test_args[1] = {method_name};
        Item result = rb_int_method(obj, method_name, test_args, 0);
        if (result.item != ((uint64_t)LMD_TYPE_ERROR << 56)) return (Item){.item = b2it(true)};
    }
    // floats
    if (tid == LMD_TYPE_FLOAT) {
        Item test_args[1] = {method_name};
        Item result = rb_float_method(obj, method_name, test_args, 0);
        if (result.item != ((uint64_t)LMD_TYPE_ERROR << 56)) return (Item){.item = b2it(true)};
    }
    // hashes
    if (tid == LMD_TYPE_MAP) {
        Item test_args[1] = {method_name};
        Item result = rb_hash_method(obj, method_name, test_args, 0);
        if (result.item != ((uint64_t)LMD_TYPE_ERROR << 56)) return (Item){.item = b2it(true)};
    }
    // check instance/class methods
    if (tid == LMD_TYPE_MAP) {
        Item lookup = rb_method_lookup(obj, method_name);
        if (get_type_id(lookup) != LMD_TYPE_NULL) return (Item){.item = b2it(true)};
    }
    // common methods all objects have
    if ((mlen == 4 && strncmp(mname, "to_s", 4) == 0) ||
        (mlen == 5 && strncmp(mname, "class", 5) == 0) ||
        (mlen == 4 && strncmp(mname, "nil?", 4) == 0) ||
        (mlen == 5 && strncmp(mname, "is_a?", 5) == 0) ||
        (mlen == 7 && strncmp(mname, "frozen?", 7) == 0)) {
        return (Item){.item = b2it(true)};
    }
    return (Item){.item = b2it(false)};
}

extern "C" Item rb_send(Item obj, Item method_name, Item* args, int argc) {
    // dynamic dispatch — try built-in dispatchers then class method lookup
    Item result;
    result = rb_string_method(obj, method_name, args, argc);
    if (result.item != ((uint64_t)LMD_TYPE_ERROR << 56)) return result;
    result = rb_array_method(obj, method_name, args, argc);
    if (result.item != ((uint64_t)LMD_TYPE_ERROR << 56)) return result;
    result = rb_hash_method(obj, method_name, args, argc);
    if (result.item != ((uint64_t)LMD_TYPE_ERROR << 56)) return result;
    result = rb_int_method(obj, method_name, args, argc);
    if (result.item != ((uint64_t)LMD_TYPE_ERROR << 56)) return result;
    result = rb_float_method(obj, method_name, args, argc);
    if (result.item != ((uint64_t)LMD_TYPE_ERROR << 56)) return result;
    // class instance method lookup
    Item func = rb_method_lookup(obj, method_name);
    if (get_type_id(func) == LMD_TYPE_FUNC) {
        return rb_block_call(func, args, argc);
    }
    log_error("rb: NoMethodError: undefined method '%s'",
        get_type_id(method_name) == LMD_TYPE_STRING ? it2s(method_name)->chars : "?");
    return (Item){.item = ITEM_NULL};
}

// ============================================================================
// File I/O
// ============================================================================
extern "C" char* read_text_file(const char* filename);
extern "C" void write_text_file(const char* filename, const char* content);
extern "C" bool file_exists(const char* path);

extern "C" Item rb_file_read(Item path) {
    if (get_type_id(path) != LMD_TYPE_STRING) return (Item){.item = ITEM_NULL};
    String* s = it2s(path);
    if (!s || !s->chars) return (Item){.item = ITEM_NULL};
    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%.*s", (int)s->len, s->chars);
    char* content = read_text_file(filepath);
    if (!content) return (Item){.item = ITEM_NULL};
    size_t len = strlen(content);
    Item result = (Item){.item = s2it(heap_create_name(content, len))};
    mem_free(content); // from read_text_file (lib)
    return result;
}

extern "C" Item rb_file_write(Item path, Item content) {
    if (get_type_id(path) != LMD_TYPE_STRING) return (Item){.item = ITEM_NULL};
    if (get_type_id(content) != LMD_TYPE_STRING) return (Item){.item = ITEM_NULL};
    String* p = it2s(path);
    String* c = it2s(content);
    if (!p || !c) return (Item){.item = ITEM_NULL};
    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%.*s", (int)p->len, p->chars);
    char* buf = (char*)mem_alloc(c->len + 1, MEM_CAT_RB_RUNTIME);
    memcpy(buf, c->chars, c->len);
    buf[c->len] = '\0';
    write_text_file(filepath, buf);
    mem_free(buf);
    return (Item){.item = i2it(c->len)};
}

extern "C" Item rb_file_exist(Item path) {
    if (get_type_id(path) != LMD_TYPE_STRING) return (Item){.item = b2it(BOOL_FALSE)};
    String* s = it2s(path);
    if (!s || !s->chars) return (Item){.item = b2it(BOOL_FALSE)};
    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%.*s", (int)s->len, s->chars);
    return (Item){.item = b2it(file_exists(filepath) ? BOOL_TRUE : BOOL_FALSE)};
}

// ============================================================================
// Regex (via RE2)
// ============================================================================

// rb_regex_new(pattern_str) — compile a regex pattern, return as Map with __regex__ marker
extern "C" Item rb_regex_new(Item pattern) {
    if (get_type_id(pattern) != LMD_TYPE_STRING) return (Item){.item = ITEM_NULL};
    String* s = it2s(pattern);
    if (!s) return (Item){.item = ITEM_NULL};

    // create RE2 regex
    std::string pat(s->chars, s->len);
    re2::RE2::Options opts;
    opts.set_log_errors(false);
    re2::RE2* re = new re2::RE2(pat, opts);
    if (!re->ok()) {
        log_error("rb: Regexp compile error: %s", re->error().c_str());
        delete re;
        return (Item){.item = ITEM_NULL};
    }

    // store as Map with __regex__ = pointer (as int64), __source__ = pattern string
    Item obj = rb_new_object();
    rb_setattr(obj, (Item){.item = s2it(heap_create_name("__regex__", 9))},
        (Item){.item = i2it((int64_t)(uintptr_t)re)});
    rb_setattr(obj, (Item){.item = s2it(heap_create_name("__source__", 10))}, pattern);
    rb_setattr(obj, (Item){.item = s2it(heap_create_name("__rb_regex__", 12))},
        (Item){.item = b2it(BOOL_TRUE)});
    return obj;
}

// helper: extract RE2 from regex map
static re2::RE2* rb_get_re2(Item regex) {
    Item ptr = rb_getattr(regex, (Item){.item = s2it(heap_create_name("__regex__", 9))});
    if (get_type_id(ptr) == LMD_TYPE_INT || get_type_id(ptr) == LMD_TYPE_INT64) {
        return (re2::RE2*)(uintptr_t)it2i(ptr);
    }
    return nullptr;
}

// check if item is a regex
extern "C" int rb_is_regex(Item item) {
    if (get_type_id(item) != LMD_TYPE_MAP) return 0;
    Item marker = rb_getattr(item, (Item){.item = s2it(heap_create_name("__rb_regex__", 12))});
    return (get_type_id(marker) == LMD_TYPE_BOOL && marker.item == b2it(BOOL_TRUE)) ? 1 : 0;
}

// rb_regex_match(regex_or_pattern, string) — returns matched string or nil
extern "C" Item rb_regex_match(Item regex, Item str) {
    if (get_type_id(str) != LMD_TYPE_STRING) return (Item){.item = ITEM_NULL};
    String* s = it2s(str);
    if (!s) return (Item){.item = ITEM_NULL};

    re2::RE2* re = nullptr;
    bool owns_re = false;

    if (rb_is_regex(regex)) {
        re = rb_get_re2(regex);
    } else if (get_type_id(regex) == LMD_TYPE_STRING) {
        // string pattern — compile inline
        String* pat = it2s(regex);
        std::string p(pat->chars, pat->len);
        re2::RE2::Options opts;
        opts.set_log_errors(false);
        re = new re2::RE2(p, opts);
        owns_re = true;
        if (!re->ok()) { delete re; return (Item){.item = ITEM_NULL}; }
    } else {
        return (Item){.item = ITEM_NULL};
    }

    re2::StringPiece input(s->chars, s->len);
    re2::StringPiece match;
    bool found = re->Match(input, 0, s->len, re2::RE2::UNANCHORED, &match, 1);
    if (owns_re) delete re;
    if (!found) return (Item){.item = ITEM_NULL};

    return (Item){.item = s2it(heap_create_name(match.data(), match.size()))};
}

// rb_regex_test(regex_or_pattern, string) — returns true/false
extern "C" Item rb_regex_test(Item regex, Item str) {
    if (get_type_id(str) != LMD_TYPE_STRING) return (Item){.item = b2it(BOOL_FALSE)};
    String* s = it2s(str);
    if (!s) return (Item){.item = b2it(BOOL_FALSE)};

    re2::RE2* re = nullptr;
    bool owns_re = false;

    if (rb_is_regex(regex)) {
        re = rb_get_re2(regex);
    } else if (get_type_id(regex) == LMD_TYPE_STRING) {
        String* pat = it2s(regex);
        std::string p(pat->chars, pat->len);
        re2::RE2::Options opts;
        opts.set_log_errors(false);
        re = new re2::RE2(p, opts);
        owns_re = true;
        if (!re->ok()) { delete re; return (Item){.item = b2it(BOOL_FALSE)}; }
    } else {
        return (Item){.item = b2it(BOOL_FALSE)};
    }

    bool found = re2::RE2::PartialMatch(re2::StringPiece(s->chars, s->len), *re);
    if (owns_re) delete re;
    return (Item){.item = b2it(found ? BOOL_TRUE : BOOL_FALSE)};
}

// rb_regex_scan(regex_or_pattern, string) — returns array of all matches
extern "C" Item rb_regex_scan(Item regex, Item str) {
    if (get_type_id(str) != LMD_TYPE_STRING) return rb_array_new();
    String* s = it2s(str);
    if (!s) return rb_array_new();

    re2::RE2* re = nullptr;
    bool owns_re = false;

    if (rb_is_regex(regex)) {
        re = rb_get_re2(regex);
    } else if (get_type_id(regex) == LMD_TYPE_STRING) {
        String* pat = it2s(regex);
        std::string p(pat->chars, pat->len);
        re2::RE2::Options opts;
        opts.set_log_errors(false);
        re = new re2::RE2(p, opts);
        owns_re = true;
        if (!re->ok()) { delete re; return rb_array_new(); }
    } else {
        return rb_array_new();
    }

    Item arr = rb_array_new();
    re2::StringPiece input(s->chars, s->len);
    re2::StringPiece match;
    size_t pos = 0;
    while (pos <= (size_t)s->len) {
        bool found = re->Match(input, (int)pos, s->len, re2::RE2::UNANCHORED, &match, 1);
        if (!found) break;
        Item m = (Item){.item = s2it(heap_create_name(match.data(), match.size()))};
        rb_array_push(arr, m);
        size_t new_pos = (size_t)(match.data() - s->chars) + match.size();
        if (new_pos == pos) new_pos++; // avoid infinite loop on zero-length match
        pos = new_pos;
    }
    if (owns_re) delete re;
    return arr;
}

// rb_regex_gsub(regex_or_pattern, string, replacement) — replace all matches
extern "C" Item rb_regex_gsub(Item regex, Item str, Item replacement) {
    if (get_type_id(str) != LMD_TYPE_STRING) return str;
    if (get_type_id(replacement) != LMD_TYPE_STRING) return str;
    String* s = it2s(str);
    String* r = it2s(replacement);
    if (!s || !r) return str;

    re2::RE2* re = nullptr;
    bool owns_re = false;

    if (rb_is_regex(regex)) {
        re = rb_get_re2(regex);
    } else if (get_type_id(regex) == LMD_TYPE_STRING) {
        String* pat = it2s(regex);
        std::string p(pat->chars, pat->len);
        re2::RE2::Options opts;
        opts.set_log_errors(false);
        re = new re2::RE2(p, opts);
        owns_re = true;
        if (!re->ok()) { delete re; return str; }
    } else {
        return str;
    }

    std::string result(s->chars, s->len);
    std::string repl(r->chars, r->len);
    re2::RE2::GlobalReplace(&result, *re, repl);
    if (owns_re) delete re;
    return (Item){.item = s2it(heap_create_name(result.c_str(), result.size()))};
}

// rb_regex_sub(regex_or_pattern, string, replacement) — replace first match
extern "C" Item rb_regex_sub(Item regex, Item str, Item replacement) {
    if (get_type_id(str) != LMD_TYPE_STRING) return str;
    if (get_type_id(replacement) != LMD_TYPE_STRING) return str;
    String* s = it2s(str);
    String* r = it2s(replacement);
    if (!s || !r) return str;

    re2::RE2* re = nullptr;
    bool owns_re = false;

    if (rb_is_regex(regex)) {
        re = rb_get_re2(regex);
    } else if (get_type_id(regex) == LMD_TYPE_STRING) {
        String* pat = it2s(regex);
        std::string p(pat->chars, pat->len);
        re2::RE2::Options opts;
        opts.set_log_errors(false);
        re = new re2::RE2(p, opts);
        owns_re = true;
        if (!re->ok()) { delete re; return str; }
    } else {
        return str;
    }

    std::string result(s->chars, s->len);
    std::string repl(r->chars, r->len);
    re2::RE2::Replace(&result, *re, repl);
    if (owns_re) delete re;
    return (Item){.item = s2it(heap_create_name(result.c_str(), result.size()))};
}
