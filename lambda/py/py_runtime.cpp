/**
 * Python Runtime Functions for Lambda
 *
 * Implements Python semantics on top of Lambda's type system.
 * All functions are callable from MIR JIT compiled code.
 */
#include "py_runtime.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/hashmap.h"
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <cstdio>

// Global Input context for Python runtime (for map_put, pool allocations)
Input* py_input = NULL;

// Forward declarations from lambda-data-runtime.cpp
extern Item _map_read_field(ShapeEntry* field, void* map_data);
extern Item _map_get(TypeMap* map_type, void* map_data, char* key, bool* is_found);

extern TypeMap EmptyMap;

// Exception handling state
static bool py_exception_pending = false;
static Item py_exception_value = {0};

// Module variable table
#define PY_MODULE_VAR_MAX 1024
static Item py_module_vars[PY_MODULE_VAR_MAX];
static int py_module_var_count = 0;

// Stop iteration sentinel
static Item py_stop_iteration_sentinel = {0};
static bool py_stop_iteration_initialized = false;

// ============================================================================
// Runtime initialization
// ============================================================================

extern "C" void py_runtime_set_input(void* input) {
    py_input = (Input*)input;
}

// ============================================================================
// Type conversion
// ============================================================================

extern "C" Item py_to_int(Item value) {
    TypeId type = get_type_id(value);

    switch (type) {
    case LMD_TYPE_NULL:
        // Python: int(None) raises TypeError, but we return 0 for simplicity
        return (Item){.item = i2it(0)};
    case LMD_TYPE_BOOL:
        return (Item){.item = i2it(it2b(value) ? 1 : 0)};
    case LMD_TYPE_INT:
        return value;
    case LMD_TYPE_FLOAT: {
        double d = it2d(value);
        return (Item){.item = i2it((int64_t)d)};
    }
    case LMD_TYPE_STRING: {
        String* str = it2s(value);
        if (!str || str->len == 0) return (Item){.item = i2it(0)};
        char* endptr;
        long long v = strtoll(str->chars, &endptr, 10);
        if (endptr == str->chars) return (Item){.item = i2it(0)};
        return (Item){.item = i2it((int64_t)v)};
    }
    default:
        return (Item){.item = i2it(0)};
    }
}

extern "C" Item py_to_float(Item value) {
    TypeId type = get_type_id(value);

    switch (type) {
    case LMD_TYPE_NULL:
        return (Item){.item = i2it(0)};
    case LMD_TYPE_BOOL: {
        double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *ptr = it2b(value) ? 1.0 : 0.0;
        return (Item){.item = d2it(ptr)};
    }
    case LMD_TYPE_INT: {
        double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *ptr = (double)it2i(value);
        return (Item){.item = d2it(ptr)};
    }
    case LMD_TYPE_FLOAT:
        return value;
    case LMD_TYPE_STRING: {
        String* str = it2s(value);
        if (!str || str->len == 0) return (Item){.item = i2it(0)};
        char* endptr;
        double d = strtod(str->chars, &endptr);
        if (endptr == str->chars) return (Item){.item = i2it(0)};
        double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *ptr = d;
        return (Item){.item = d2it(ptr)};
    }
    default:
        return (Item){.item = i2it(0)};
    }
}

extern "C" Item py_to_str(Item value) {
    TypeId type = get_type_id(value);

    switch (type) {
    case LMD_TYPE_NULL:
        return (Item){.item = s2it(heap_create_name("None"))};
    case LMD_TYPE_BOOL:
        return (Item){.item = s2it(heap_create_name(it2b(value) ? "True" : "False"))};
    case LMD_TYPE_INT: {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%lld", (long long)it2i(value));
        return (Item){.item = s2it(heap_create_name(buffer))};
    }
    case LMD_TYPE_FLOAT: {
        double d = it2d(value);
        if (isnan(d)) return (Item){.item = s2it(heap_create_name("nan"))};
        if (isinf(d)) return (Item){.item = s2it(heap_create_name(d > 0 ? "inf" : "-inf"))};
        char buffer[64];
        // Python shows .0 for whole numbers
        if (d == (double)(int64_t)d && isfinite(d)) {
            snprintf(buffer, sizeof(buffer), "%.1f", d);
        } else {
            snprintf(buffer, sizeof(buffer), "%.15g", d);
        }
        return (Item){.item = s2it(heap_create_name(buffer))};
    }
    case LMD_TYPE_STRING:
        return value;
    case LMD_TYPE_ARRAY:
        return (Item){.item = s2it(heap_create_name("[...]"))};
    case LMD_TYPE_MAP:
        return (Item){.item = s2it(heap_create_name("{...}"))};
    case LMD_TYPE_FUNC:
        return (Item){.item = s2it(heap_create_name("<function>"))};
    default:
        return (Item){.item = s2it(heap_create_name("<object>"))};
    }
}

extern "C" Item py_to_bool(Item value) {
    return (Item){.item = b2it(py_is_truthy(value))};
}

extern "C" bool py_is_truthy(Item value) {
    TypeId type = get_type_id(value);

    switch (type) {
    case LMD_TYPE_NULL:
        return false;
    case LMD_TYPE_BOOL:
        return it2b(value);
    case LMD_TYPE_INT:
        return it2i(value) != 0;
    case LMD_TYPE_FLOAT: {
        double d = it2d(value);
        return d != 0.0 && !isnan(d);
    }
    case LMD_TYPE_STRING: {
        String* str = it2s(value);
        return str && str->len > 0;
    }
    case LMD_TYPE_ARRAY: {
        Array* arr = it2arr(value);
        return arr && arr->length > 0;
    }
    case LMD_TYPE_MAP: {
        // empty dict is falsy
        Map* m = it2map(value);
        return m && m->type && ((TypeMap*)m->type)->field_count > 0;
    }
    default:
        return value.item != 0;
    }
}

// ============================================================================
// Helper: make numeric Item (int or float)
// ============================================================================

static Item py_make_number(double d) {
    if (isfinite(d) && d == (double)(int64_t)d && d >= INT56_MIN && d <= INT56_MAX) {
        return (Item){.item = i2it((int64_t)d)};
    }
    double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *ptr = d;
    return (Item){.item = d2it(ptr)};
}

static double py_get_number(Item value) {
    TypeId type = get_type_id(value);
    switch (type) {
    case LMD_TYPE_INT: return (double)it2i(value);
    case LMD_TYPE_FLOAT: return it2d(value);
    case LMD_TYPE_BOOL: return it2b(value) ? 1.0 : 0.0;
    default: return 0.0;
    }
}

// ============================================================================
// Arithmetic operators — Python semantics
// ============================================================================

extern "C" Item py_add(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);

    // string + string → concatenation
    if (lt == LMD_TYPE_STRING && rt == LMD_TYPE_STRING) {
        return fn_join(left, right);
    }

    // int + int → int (unless overflow)
    if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) {
        int64_t a = it2i(left), b = it2i(right);
        int64_t r = a + b;
        if (r >= INT56_MIN && r <= INT56_MAX) {
            return (Item){.item = i2it(r)};
        }
        // overflow → float
        return py_make_number((double)a + (double)b);
    }

    // numeric addition
    return fn_add(left, right);
}

extern "C" Item py_subtract(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);

    if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) {
        int64_t a = it2i(left), b = it2i(right);
        int64_t r = a - b;
        if (r >= INT56_MIN && r <= INT56_MAX) {
            return (Item){.item = i2it(r)};
        }
        return py_make_number((double)a - (double)b);
    }

    return fn_sub(left, right);
}

extern "C" Item py_multiply(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);

    // string * int → repeat (Python-specific)
    if (lt == LMD_TYPE_STRING && (rt == LMD_TYPE_INT || rt == LMD_TYPE_BOOL)) {
        String* str = it2s(left);
        int64_t count = (rt == LMD_TYPE_INT) ? it2i(right) : (it2b(right) ? 1 : 0);
        if (!str || count <= 0) return (Item){.item = s2it(heap_create_name(""))};
        if (count == 1) return left;

        size_t total_len = str->len * count;
        if (total_len > 1024 * 1024) total_len = 1024 * 1024; // limit

        char* buf = (char*)malloc(total_len + 1);
        if (!buf) return left;
        size_t pos = 0;
        for (int64_t i = 0; i < count && pos + str->len <= total_len; i++) {
            memcpy(buf + pos, str->chars, str->len);
            pos += str->len;
        }
        buf[pos] = '\0';
        Item result = (Item){.item = s2it(heap_strcpy(buf, pos))};
        free(buf);
        return result;
    }

    // list * int → repeat
    if (lt == LMD_TYPE_ARRAY && (rt == LMD_TYPE_INT || rt == LMD_TYPE_BOOL)) {
        Array* arr = it2arr(left);
        int64_t count = (rt == LMD_TYPE_INT) ? it2i(right) : (it2b(right) ? 1 : 0);
        if (!arr || count <= 0) {
            Array* empty = array();
            return (Item){.array = empty};
        }

        Array* result = array();
        for (int64_t c = 0; c < count; c++) {
            for (int i = 0; i < arr->length; i++) {
                array_push(result, arr->items[i]);
            }
        }
        return (Item){.array = result};
    }

    if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) {
        int64_t a = it2i(left), b = it2i(right);
        // check overflow
        if (b != 0 && (a > INT56_MAX / (b > 0 ? b : -b) || a < INT56_MIN / (b > 0 ? b : -b))) {
            return py_make_number((double)a * (double)b);
        }
        int64_t r = a * b;
        if (r >= INT56_MIN && r <= INT56_MAX) {
            return (Item){.item = i2it(r)};
        }
        return py_make_number((double)a * (double)b);
    }

    return fn_mul(left, right);
}

extern "C" Item py_divide(Item left, Item right) {
    // Python true division always returns float
    double l = py_get_number(left);
    double r = py_get_number(right);
    if (r == 0.0) {
        log_error("py: ZeroDivisionError: division by zero");
        return ItemNull;
    }
    double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *ptr = l / r;
    return (Item){.item = d2it(ptr)};
}

extern "C" Item py_floor_divide(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);

    if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) {
        int64_t a = it2i(left), b = it2i(right);
        if (b == 0) {
            log_error("py: ZeroDivisionError: integer floor division by zero");
            return ItemNull;
        }
        // Python floor division rounds toward negative infinity
        int64_t q = a / b;
        if ((a ^ b) < 0 && a % b != 0) q--;
        return (Item){.item = i2it(q)};
    }

    double l = py_get_number(left);
    double r = py_get_number(right);
    if (r == 0.0) {
        log_error("py: ZeroDivisionError: float floor division by zero");
        return ItemNull;
    }
    return py_make_number(floor(l / r));
}

extern "C" Item py_modulo(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);

    // string % args → format (simplified: just return string)
    if (lt == LMD_TYPE_STRING) {
        return left;
    }

    if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) {
        int64_t a = it2i(left), b = it2i(right);
        if (b == 0) {
            log_error("py: ZeroDivisionError: integer modulo by zero");
            return ItemNull;
        }
        // Python modulo: result has same sign as divisor
        int64_t r = a % b;
        if (r != 0 && (r ^ b) < 0) r += b;
        return (Item){.item = i2it(r)};
    }

    double l = py_get_number(left);
    double r = py_get_number(right);
    if (r == 0.0) {
        log_error("py: ZeroDivisionError: float modulo by zero");
        return ItemNull;
    }
    return py_make_number(fmod(l, r) + (fmod(l, r) != 0 && ((l < 0) != (r < 0)) ? r : 0));
}

extern "C" Item py_power(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);

    if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) {
        int64_t base = it2i(left), exp = it2i(right);
        if (exp < 0) {
            // negative exponent → float
            double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
            *ptr = pow((double)base, (double)exp);
            return (Item){.item = d2it(ptr)};
        }
        // positive integer exponent: compute by squaring
        int64_t result = 1;
        int64_t b = base;
        int64_t e = exp;
        while (e > 0) {
            if (e & 1) {
                // overflow check
                if (result > INT56_MAX / (b > 0 ? b : -b > 0 ? -b : 1)) {
                    return py_make_number(pow((double)base, (double)exp));
                }
                result *= b;
            }
            e >>= 1;
            if (e > 0) b *= b;
        }
        if (result >= INT56_MIN && result <= INT56_MAX) {
            return (Item){.item = i2it(result)};
        }
        return py_make_number(pow((double)base, (double)exp));
    }

    return fn_pow(left, right);
}

extern "C" Item py_negate(Item operand) {
    TypeId type = get_type_id(operand);
    if (type == LMD_TYPE_INT) {
        int64_t v = -it2i(operand);
        return (Item){.item = i2it(v)};
    }
    return fn_neg(operand);
}

extern "C" Item py_positive(Item operand) {
    // +x is identity for numbers
    return operand;
}

extern "C" Item py_bit_not(Item operand) {
    TypeId type = get_type_id(operand);
    if (type == LMD_TYPE_INT) {
        return (Item){.item = i2it(~it2i(operand))};
    }
    if (type == LMD_TYPE_BOOL) {
        return (Item){.item = i2it(~(it2b(operand) ? 1 : 0))};
    }
    return ItemNull;
}

// ============================================================================
// Bitwise operators
// ============================================================================

extern "C" Item py_bit_and(Item left, Item right) {
    return (Item){.item = i2it((int64_t)py_get_number(left) & (int64_t)py_get_number(right))};
}

extern "C" Item py_bit_or(Item left, Item right) {
    return (Item){.item = i2it((int64_t)py_get_number(left) | (int64_t)py_get_number(right))};
}

extern "C" Item py_bit_xor(Item left, Item right) {
    return (Item){.item = i2it((int64_t)py_get_number(left) ^ (int64_t)py_get_number(right))};
}

extern "C" Item py_lshift(Item left, Item right) {
    return (Item){.item = i2it((int64_t)py_get_number(left) << (int64_t)py_get_number(right))};
}

extern "C" Item py_rshift(Item left, Item right) {
    return (Item){.item = i2it((int64_t)py_get_number(left) >> (int64_t)py_get_number(right))};
}

// ============================================================================
// Comparison operators
// ============================================================================

extern "C" Item py_eq(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);

    if (lt == LMD_TYPE_NULL && rt == LMD_TYPE_NULL) return (Item){.item = ITEM_TRUE};
    if (lt == LMD_TYPE_NULL || rt == LMD_TYPE_NULL) return (Item){.item = ITEM_FALSE};

    if (lt == LMD_TYPE_INT && rt == LMD_TYPE_INT) {
        return (Item){.item = b2it(it2i(left) == it2i(right))};
    }
    if ((lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT) && (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT)) {
        return (Item){.item = b2it(py_get_number(left) == py_get_number(right))};
    }
    if (lt == LMD_TYPE_STRING && rt == LMD_TYPE_STRING) {
        String* a = it2s(left);
        String* b = it2s(right);
        if (a == b) return (Item){.item = ITEM_TRUE};
        if (a->len != b->len) return (Item){.item = ITEM_FALSE};
        return (Item){.item = b2it(memcmp(a->chars, b->chars, a->len) == 0)};
    }
    if (lt == LMD_TYPE_BOOL && rt == LMD_TYPE_BOOL) {
        return (Item){.item = b2it(it2b(left) == it2b(right))};
    }

    // mixed types: Python returns False for most
    return (Item){.item = ITEM_FALSE};
}

extern "C" Item py_ne(Item left, Item right) {
    Item eq = py_eq(left, right);
    return (Item){.item = b2it(!it2b(eq))};
}

extern "C" Item py_lt(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);

    if ((lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT) && (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT)) {
        return (Item){.item = b2it(py_get_number(left) < py_get_number(right))};
    }
    if (lt == LMD_TYPE_STRING && rt == LMD_TYPE_STRING) {
        String* a = it2s(left);
        String* b = it2s(right);
        int cmp = memcmp(a->chars, b->chars, a->len < b->len ? a->len : b->len);
        if (cmp == 0) return (Item){.item = b2it(a->len < b->len)};
        return (Item){.item = b2it(cmp < 0)};
    }

    return (Item){.item = ITEM_FALSE};
}

extern "C" Item py_le(Item left, Item right) {
    Item lt_r = py_lt(left, right);
    Item eq_r = py_eq(left, right);
    return (Item){.item = b2it(it2b(lt_r) || it2b(eq_r))};
}

extern "C" Item py_gt(Item left, Item right) {
    return py_lt(right, left);
}

extern "C" Item py_ge(Item left, Item right) {
    return py_le(right, left);
}

extern "C" Item py_is(Item left, Item right) {
    // identity check: same tagged pointer
    return (Item){.item = b2it(left.item == right.item)};
}

extern "C" Item py_is_not(Item left, Item right) {
    return (Item){.item = b2it(left.item != right.item)};
}

extern "C" Item py_contains(Item container, Item value) {
    TypeId ct = get_type_id(container);

    if (ct == LMD_TYPE_STRING) {
        // "in" for strings: substring check
        String* haystack = it2s(container);
        String* needle = it2s(value);
        if (!haystack || !needle) return (Item){.item = ITEM_FALSE};
        if (needle->len == 0) return (Item){.item = ITEM_TRUE};
        if (needle->len > haystack->len) return (Item){.item = ITEM_FALSE};

        for (int64_t i = 0; i <= haystack->len - needle->len; i++) {
            if (memcmp(haystack->chars + i, needle->chars, needle->len) == 0) {
                return (Item){.item = ITEM_TRUE};
            }
        }
        return (Item){.item = ITEM_FALSE};
    }

    if (ct == LMD_TYPE_ARRAY) {
        Array* arr = it2arr(container);
        if (!arr) return (Item){.item = ITEM_FALSE};
        for (int i = 0; i < arr->length; i++) {
            Item eq = py_eq(arr->items[i], value);
            if (it2b(eq)) return (Item){.item = ITEM_TRUE};
        }
        return (Item){.item = ITEM_FALSE};
    }

    if (ct == LMD_TYPE_MAP) {
        // check if key exists in dict
        if (get_type_id(value) == LMD_TYPE_STRING) {
            String* key = it2s(value);
            Map* m = it2map(container);
            if (!m || !m->type) return (Item){.item = ITEM_FALSE};
            bool found = false;
            _map_get((TypeMap*)m->type, m->data, key->chars, &found);
            return (Item){.item = b2it(found)};
        }
        return (Item){.item = ITEM_FALSE};
    }

    return (Item){.item = ITEM_FALSE};
}

// ============================================================================
// Object/attribute operations
// ============================================================================

#define PY_MAP_SIZE_CLASS 1

extern "C" Item py_new_object(void) {
    Map* m = (Map*)heap_calloc_class(sizeof(Map), LMD_TYPE_MAP, PY_MAP_SIZE_CLASS);
    m->type_id = LMD_TYPE_MAP;
    m->type = &EmptyMap;
    return (Item){.map = m};
}

extern "C" Item py_getattr(Item object, Item name) {
    if (get_type_id(name) != LMD_TYPE_STRING) return ItemNull;

    TypeId ot = get_type_id(object);
    if (ot == LMD_TYPE_MAP) {
        Map* m = it2map(object);
        if (!m || !m->type) return ItemNull;
        String* key = it2s(name);
        bool found = false;
        Item result = _map_get((TypeMap*)m->type, m->data, key->chars, &found);
        return found ? result : ItemNull;
    }

    return ItemNull;
}

extern "C" Item py_setattr(Item object, Item name, Item value) {
    if (get_type_id(name) != LMD_TYPE_STRING) return object;
    if (get_type_id(object) != LMD_TYPE_MAP) return object;

    Map* m = it2map(object);
    if (!m) return object;

    String* key = it2s(name);
    if (py_input) {
        map_put(m, key, value, py_input);
    }
    return value;
}

// ============================================================================
// Collection operations
// ============================================================================

extern "C" Item py_list_new(int length) {
    Array* arr = array();
    if (length > 0) {
        arr->capacity = length + 4;
        arr->items = (Item*)malloc(arr->capacity * sizeof(Item));
        arr->length = length;
        for (int i = 0; i < length; i++) {
            arr->items[i] = ItemNull;
        }
    }
    return (Item){.array = arr};
}

extern "C" Item py_list_append(Item list, Item value) {
    if (get_type_id(list) != LMD_TYPE_ARRAY) return list;
    Array* arr = it2arr(list);
    array_push(arr, value);
    return list;
}

extern "C" Item py_list_get(Item list, Item index) {
    if (get_type_id(list) != LMD_TYPE_ARRAY) return ItemNull;
    Array* arr = it2arr(list);
    if (!arr) return ItemNull;

    int64_t idx = (get_type_id(index) == LMD_TYPE_INT) ? it2i(index) : 0;
    // Python supports negative indexing
    if (idx < 0) idx += arr->length;
    if (idx < 0 || idx >= arr->length) return ItemNull;
    return arr->items[idx];
}

extern "C" Item py_list_set(Item list, Item index, Item value) {
    if (get_type_id(list) != LMD_TYPE_ARRAY) return ItemNull;
    Array* arr = it2arr(list);
    if (!arr) return ItemNull;

    int64_t idx = (get_type_id(index) == LMD_TYPE_INT) ? it2i(index) : 0;
    if (idx < 0) idx += arr->length;
    if (idx < 0 || idx >= arr->length) return ItemNull;
    arr->items[idx] = value;
    return value;
}

extern "C" int64_t py_list_length(Item list) {
    if (get_type_id(list) != LMD_TYPE_ARRAY) return 0;
    Array* arr = it2arr(list);
    return arr ? arr->length : 0;
}

extern "C" Item py_dict_new(void) {
    return py_new_object();
}

extern "C" Item py_dict_get(Item dict, Item key) {
    return py_getattr(dict, key);
}

extern "C" Item py_dict_set(Item dict, Item key, Item value) {
    return py_setattr(dict, key, value);
}

extern "C" Item py_tuple_new(int length) {
    return py_list_new(length); // tuples use arrays internally
}

extern "C" Item py_tuple_set(Item tuple, int index, Item value) {
    if (get_type_id(tuple) != LMD_TYPE_ARRAY) return ItemNull;
    Array* arr = it2arr(tuple);
    if (!arr || index < 0 || index >= arr->length) return ItemNull;
    arr->items[index] = value;
    return value;
}

// ============================================================================
// Subscript and slice
// ============================================================================

extern "C" Item py_subscript_get(Item object, Item key) {
    TypeId ot = get_type_id(object);

    if (ot == LMD_TYPE_ARRAY) {
        return py_list_get(object, key);
    }
    if (ot == LMD_TYPE_MAP) {
        return py_getattr(object, key);
    }
    if (ot == LMD_TYPE_STRING) {
        String* str = it2s(object);
        if (!str) return ItemNull;
        int64_t idx = (get_type_id(key) == LMD_TYPE_INT) ? it2i(key) : 0;
        if (idx < 0) idx += str->len;
        if (idx < 0 || idx >= (int64_t)str->len) return ItemNull;
        return (Item){.item = s2it(heap_strcpy(str->chars + idx, 1))};
    }

    return ItemNull;
}

extern "C" Item py_subscript_set(Item object, Item key, Item value) {
    TypeId ot = get_type_id(object);

    if (ot == LMD_TYPE_ARRAY) {
        return py_list_set(object, key, value);
    }
    if (ot == LMD_TYPE_MAP) {
        return py_setattr(object, key, value);
    }

    return ItemNull;
}

// ============================================================================
// Iterator protocol (simplified)
// ============================================================================

extern "C" Item py_get_iterator(Item iterable) {
    // for now, just return the iterable itself (we handle arrays and ranges)
    return iterable;
}

extern "C" Item py_iterator_next(Item iterator) {
    // this is handled by the transpiler based on type
    return py_stop_iteration();
}

extern "C" Item py_range_new(Item start, Item stop, Item step) {
    // create range as an array for simplicity
    int64_t s = (get_type_id(start) == LMD_TYPE_INT) ? it2i(start) : 0;
    int64_t e = (get_type_id(stop) == LMD_TYPE_INT) ? it2i(stop) : 0;
    int64_t st = (get_type_id(step) == LMD_TYPE_INT) ? it2i(step) : 1;

    if (st == 0) {
        log_error("py: ValueError: range() arg 3 must not be zero");
        return ItemNull;
    }

    Array* arr = array();
    if (st > 0) {
        for (int64_t i = s; i < e; i += st) {
            array_push(arr, (Item){.item = i2it(i)});
        }
    } else {
        for (int64_t i = s; i > e; i += st) {
            array_push(arr, (Item){.item = i2it(i)});
        }
    }
    return (Item){.array = arr};
}

// ============================================================================
// Function/closure
// ============================================================================

extern "C" Item py_new_function(void* func_ptr, int param_count) {
    // allocate a Function-like object
    if (!py_input) return ItemNull;
    Function* fn = (Function*)pool_calloc(py_input->pool, sizeof(Function));
    fn->type_id = LMD_TYPE_FUNC;
    fn->ptr = (fn_ptr)func_ptr;
    fn->arity = param_count;
    return (Item){.function = fn};
}

extern "C" Item py_new_closure(void* func_ptr, int param_count, uint64_t* env, int env_size) {
    if (!py_input) return ItemNull;
    Function* fn = (Function*)pool_calloc(py_input->pool, sizeof(Function));
    fn->type_id = LMD_TYPE_FUNC;
    fn->ptr = (fn_ptr)func_ptr;
    fn->arity = param_count;
    fn->closure_env = env;
    fn->closure_field_count = env_size;
    return (Item){.function = fn};
}

extern "C" uint64_t* py_alloc_env(int size) {
    if (!py_input) return NULL;
    return (uint64_t*)pool_calloc(py_input->pool, size * sizeof(uint64_t));
}

extern "C" Item py_call_function(Item func, Item* args, int arg_count) {
    if (get_type_id(func) != LMD_TYPE_FUNC) {
        log_error("py: TypeError: object is not callable");
        return ItemNull;
    }
    Function* fn = func.function;
    if (!fn || !fn->ptr) return ItemNull;

    // call through function pointer — cast to appropriate type
    typedef Item (*PyFunc0)();
    typedef Item (*PyFunc1)(Item);
    typedef Item (*PyFunc2)(Item, Item);
    typedef Item (*PyFunc3)(Item, Item, Item);

    switch (fn->arity) {
    case 0: return ((PyFunc0)fn->ptr)();
    case 1: return arg_count >= 1 ? ((PyFunc1)fn->ptr)(args[0]) : ItemNull;
    case 2: return arg_count >= 2 ? ((PyFunc2)fn->ptr)(args[0], args[1]) : ItemNull;
    case 3: return arg_count >= 3 ? ((PyFunc3)fn->ptr)(args[0], args[1], args[2]) : ItemNull;
    default: return ItemNull;
    }
}

// ============================================================================
// Exception handling
// ============================================================================

extern "C" void py_raise(Item exception) {
    py_exception_pending = true;
    py_exception_value = exception;
    log_debug("py: exception raised");
}

extern "C" Item py_check_exception(void) {
    return (Item){.item = b2it(py_exception_pending)};
}

extern "C" Item py_clear_exception(void) {
    py_exception_pending = false;
    Item val = py_exception_value;
    py_exception_value = ItemNull;
    return val;
}

extern "C" Item py_new_exception(Item type_name, Item message) {
    Item obj = py_new_object();
    py_setattr(obj, (Item){.item = s2it(heap_create_name("type"))}, type_name);
    py_setattr(obj, (Item){.item = s2it(heap_create_name("message"))}, message);
    return obj;
}

// ============================================================================
// Module variable table
// ============================================================================

extern "C" void py_set_module_var(int index, Item value) {
    if (index >= 0 && index < PY_MODULE_VAR_MAX) {
        py_module_vars[index] = value;
        if (index >= py_module_var_count) {
            py_module_var_count = index + 1;
        }
    }
}

extern "C" Item py_get_module_var(int index) {
    if (index >= 0 && index < PY_MODULE_VAR_MAX) {
        return py_module_vars[index];
    }
    return ItemNull;
}

extern "C" void py_reset_module_vars(void) {
    memset(py_module_vars, 0, py_module_var_count * sizeof(Item));
    py_module_var_count = 0;
}

// ============================================================================
// Stop iteration
// ============================================================================

extern "C" Item py_stop_iteration(void) {
    if (!py_stop_iteration_initialized) {
        py_stop_iteration_sentinel = py_new_exception(
            (Item){.item = s2it(heap_create_name("StopIteration"))},
            (Item){.item = s2it(heap_create_name(""))}
        );
        py_stop_iteration_initialized = true;
    }
    return py_stop_iteration_sentinel;
}

extern "C" bool py_is_stop_iteration(Item value) {
    if (!py_stop_iteration_initialized) return false;
    return value.item == py_stop_iteration_sentinel.item;
}
