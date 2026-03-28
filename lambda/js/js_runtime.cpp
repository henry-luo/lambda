/**
 * JavaScript Runtime Functions for Lambda v2
 *
 * Implements JavaScript semantics on top of Lambda's type system.
 * All functions are callable from MIR JIT compiled code.
 */
#include "js_runtime.h"
#include "js_dom.h"
#include "js_typed_array.h"
#include "js_event_loop.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/hashmap.h"
#include "../../lib/str.h"
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <re2/re2.h>

// Global Input context for JS runtime map_put operations.
// Initialized in transpile_js_to_mir() before JIT execution.
Input* js_input = NULL;

// Forward declaration for _map_read_field (defined in lambda-data-runtime.cpp)
Item _map_read_field(ShapeEntry* field, void* map_data);
// Forward declaration for _map_get (used as fallback for nested/spread maps)
Item _map_get(TypeMap* map_type, void* map_data, char *key, bool *is_found);

// Global 'this' binding for the current method call
static Item js_current_this = {0};

// Module-level variable table for top-level bindings accessible from any function.
// Populated during js_main execution, read by class method closures.
#define JS_MAX_MODULE_VARS 256
static Item js_module_vars[JS_MAX_MODULE_VARS];
static int js_module_var_count = 0;

// Helper to make JS undefined value
static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

// Symbol property key helpers: Symbols are encoded as negative ints (<= -1000000).
// Convert symbol items to unique string keys for property storage.
static inline bool js_key_is_symbol(Item key) {
    if (get_type_id(key) != LMD_TYPE_INT) return false;
    return it2i(key) <= -1000000;
}

static inline Item js_symbol_to_key(Item sym) {
    int64_t id = -(it2i(sym) + 1000000);
    char buf[32];
    snprintf(buf, sizeof(buf), "__sym_%lld", (long long)id);
    return (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
}

extern "C" void js_set_module_var(int index, Item value) {
    if (index >= 0 && index < JS_MAX_MODULE_VARS) {
        js_module_vars[index] = value;
    }
}

extern "C" Item js_get_module_var(int index) {
    if (index >= 0 && index < JS_MAX_MODULE_VARS) {
        return js_module_vars[index];
    }
    return ItemNull;
}

extern "C" void js_reset_module_vars() {
    memset(js_module_vars, 0, sizeof(js_module_vars));
    js_module_var_count = 0;
}

// =============================================================================
// Exception Handling State
// =============================================================================

static bool js_exception_pending = false;
static Item js_exception_value = {0};

extern "C" void js_throw_value(Item value) {
    js_exception_pending = true;
    js_exception_value = value;
    log_debug("js: throw_value called, exception pending");
}

extern "C" int js_check_exception(void) {
    return js_exception_pending ? 1 : 0;
}

extern "C" Item js_clear_exception(void) {
    js_exception_pending = false;
    Item val = js_exception_value;
    js_exception_value = ItemNull;
    return val;
}

extern "C" Item js_new_error(Item message) {
    Item obj = js_new_object();
    Item name_key = (Item){.item = s2it(heap_create_name("name"))};
    Item name_val = (Item){.item = s2it(heap_create_name("Error"))};
    js_property_set(obj, name_key, name_val);
    Item msg_key = (Item){.item = s2it(heap_create_name("message"))};
    js_property_set(obj, msg_key, message);
    // Set __class_name__ for instanceof support
    Item cn_key = (Item){.item = s2it(heap_create_name("__class_name__", 14))};
    js_property_set(obj, cn_key, name_val);
    return obj;
}

// v11: Create a typed Error (TypeError, RangeError, SyntaxError, ReferenceError)
extern "C" Item js_new_error_with_name(Item error_name, Item message) {
    Item obj = js_new_object();
    Item name_key = (Item){.item = s2it(heap_create_name("name"))};
    js_property_set(obj, name_key, error_name);
    Item msg_key = (Item){.item = s2it(heap_create_name("message"))};
    js_property_set(obj, msg_key, message);
    // Set __class_name__ for instanceof support
    Item cn_key = (Item){.item = s2it(heap_create_name("__class_name__", 14))};
    js_property_set(obj, cn_key, error_name);
    return obj;
}

extern "C" void js_runtime_set_input(void* input) {
    js_input = (Input*)input;
}

extern "C" Item js_get_this() {
    return js_current_this;
}

extern "C" void js_set_this(Item this_val) {
    js_current_this = this_val;
}

extern TypeMap EmptyMap;

// =============================================================================
// Type Conversion Functions
// =============================================================================

extern "C" Item js_to_number(Item value) {
    TypeId type = get_type_id(value);

    switch (type) {
    case LMD_TYPE_NULL:
    case LMD_TYPE_UNDEFINED:
        // null -> 0, undefined -> NaN
        if (type == LMD_TYPE_UNDEFINED) {
            double* nan_ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
            *nan_ptr = NAN;
            return (Item){.item = d2it(nan_ptr)};
        }
        return (Item){.item = i2it(0)};

    case LMD_TYPE_BOOL: {
        int val = it2b(value) ? 1 : 0;
        return (Item){.item = i2it(val)};
    }

    case LMD_TYPE_INT:
        // Already a number (int), convert to float for consistency
        return value;

    case LMD_TYPE_FLOAT:
        return value;

    case LMD_TYPE_STRING: {
        String* str = it2s(value);
        if (!str || str->len == 0) {
            return (Item){.item = i2it(0)};  // Empty string -> 0
        }
        char* endptr;
        double num = strtod(str->chars, &endptr);
        if (endptr == str->chars) {
            // No valid conversion
            double* nan_ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
            *nan_ptr = NAN;
            return (Item){.item = d2it(nan_ptr)};
        }
        double* result = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *result = num;
        return (Item){.item = d2it(result)};
    }

    default:
        // Objects, arrays, etc. -> NaN
        double* nan_ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *nan_ptr = NAN;
        return (Item){.item = d2it(nan_ptr)};
    }
}

extern "C" Item js_to_string(Item value) {
    TypeId type = get_type_id(value);

    switch (type) {
    case LMD_TYPE_NULL:
        return (Item){.item = s2it(heap_create_name("null"))};

    case LMD_TYPE_UNDEFINED:
        return (Item){.item = s2it(heap_create_name("undefined"))};

    case LMD_TYPE_BOOL:
        return (Item){.item = s2it(heap_create_name(it2b(value) ? "true" : "false"))};

    case LMD_TYPE_INT: {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%lld", (long long)it2i(value));
        return (Item){.item = s2it(heap_create_name(buffer))};
    }

    case LMD_TYPE_FLOAT: {
        double d = it2d(value);
        if (isnan(d)) {
            return (Item){.item = s2it(heap_create_name("NaN"))};
        } else if (isinf(d)) {
            return (Item){.item = s2it(heap_create_name(d > 0 ? "Infinity" : "-Infinity"))};
        } else {
            char buffer[64];
            // Use %g for more natural number representation
            snprintf(buffer, sizeof(buffer), "%.15g", d);
            return (Item){.item = s2it(heap_create_name(buffer))};
        }
    }

    case LMD_TYPE_STRING:
        return value;

    case LMD_TYPE_ARRAY: {
        // JS: String([1,2,3]) => "1,2,3" (same as Array.prototype.join(","))
        Array* a = value.array;
        if (!a || a->length == 0) {
            return (Item){.item = s2it(heap_create_name(""))};
        }
        StrBuf* sb = strbuf_new();
        for (int i = 0; i < a->length; i++) {
            if (i > 0) strbuf_append_str_n(sb, ",", 1);
            TypeId etype = get_type_id(a->items[i]);
            if (etype != LMD_TYPE_NULL && etype != LMD_TYPE_UNDEFINED) {
                Item elem_str = js_to_string(a->items[i]);
                String* s = it2s(elem_str);
                if (s && s->len > 0) {
                    strbuf_append_str_n(sb, s->chars, (int)s->len);
                }
            }
        }
        String* result = heap_create_name(sb->str, sb->length);
        strbuf_free(sb);
        return (Item){.item = s2it(result)};
    }

    case LMD_TYPE_MAP:
        return (Item){.item = s2it(heap_create_name("[object Object]"))};

    case LMD_TYPE_FUNC:
        return (Item){.item = s2it(heap_create_name("[object Function]"))};

    default:
        return (Item){.item = s2it(heap_create_name("[object Object]"))};
    }
}

extern "C" Item js_to_boolean(Item value) {
    return (Item){.item = b2it(js_is_truthy(value))};
}

extern "C" bool js_is_truthy(Item value) {
    TypeId type = get_type_id(value);

    switch (type) {
    case LMD_TYPE_NULL:
    case LMD_TYPE_UNDEFINED:
        return false;

    case LMD_TYPE_BOOL:
        return it2b(value);

    case LMD_TYPE_INT:
        return it2i(value) != 0;

    case LMD_TYPE_FLOAT: {
        double d = it2d(value);
        return !isnan(d) && d != 0.0;
    }

    case LMD_TYPE_STRING: {
        String* str = it2s(value);
        return str && str->len > 0;
    }

    default:
        // Objects, arrays, functions are all truthy
        return value.item != 0;
    }
}

// js_is_nullish: returns true if value is null or undefined (for ?? operator)
extern "C" int64_t js_is_nullish(Item value) {
    TypeId type = get_type_id(value);
    return (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED) ? 1 : 0;
}

// =============================================================================
// Helper: Get numeric value as double
// =============================================================================

static double js_get_number(Item value) {
    TypeId type = get_type_id(value);

    switch (type) {
    case LMD_TYPE_INT:
        return (double)it2i(value);
    case LMD_TYPE_FLOAT:
        return it2d(value);
    case LMD_TYPE_BOOL:
        return it2b(value) ? 1.0 : 0.0;
    case LMD_TYPE_NULL:
        return 0.0;
    case LMD_TYPE_UNDEFINED:
        return NAN;
    case LMD_TYPE_STRING: {
        String* str = it2s(value);
        if (!str || str->len == 0) return 0.0;
        char* endptr;
        double num = strtod(str->chars, &endptr);
        if (endptr == str->chars) return NAN;
        return num;
    }
    default:
        return NAN;
    }
}

static Item js_make_number(double d) {
    // Check if it can be represented as an integer
    // Guard with isfinite to avoid UB from (int64_t)Infinity/NaN
    if (isfinite(d) && d == (double)(int64_t)d && d >= INT56_MIN && d <= INT56_MAX) {
        return (Item){.item = i2it((int64_t)d)};
    }
    double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *ptr = d;
    return (Item){.item = d2it(ptr)};
}

// =============================================================================
// Arithmetic Operators
// =============================================================================

extern "C" Item js_add(Item left, Item right) {
    TypeId left_type = get_type_id(left);
    TypeId right_type = get_type_id(right);

    // String concatenation if either operand is a string
    if (left_type == LMD_TYPE_STRING || right_type == LMD_TYPE_STRING) {
        Item left_str = js_to_string(left);
        Item right_str = js_to_string(right);
        return fn_join(left_str, right_str);
    }

    // Numeric addition — use double arithmetic for JS semantics
    double l = js_get_number(left);
    double r = js_get_number(right);
    return js_make_number(l + r);
}

extern "C" Item js_subtract(Item left, Item right) {
    double l = js_get_number(left);
    double r = js_get_number(right);
    return js_make_number(l - r);
}

extern "C" Item js_multiply(Item left, Item right) {
    // Use double arithmetic for JS semantics (no integer overflow errors)
    double l = js_get_number(left);
    double r = js_get_number(right);
    return js_make_number(l * r);
}

extern "C" Item js_divide(Item left, Item right) {
    // Use double arithmetic for correct JS semantics:
    // x/0 → Infinity, -x/0 → -Infinity, 0/0 → NaN (IEEE 754)
    double l = js_get_number(left);
    double r = js_get_number(right);
    return js_make_number(l / r);
}

extern "C" Item js_modulo(Item left, Item right) {
    // fn_mod does not support float types, so keep custom implementation
    // that handles JS numeric coercion correctly
    double l = js_get_number(left);
    double r = js_get_number(right);
    return js_make_number(fmod(l, r));
}

extern "C" Item js_power(Item left, Item right) {
    return fn_pow(js_to_number(left), js_to_number(right));
}

// =============================================================================
// Comparison Operators
// =============================================================================

extern "C" Item js_equal(Item left, Item right) {
    TypeId left_type = get_type_id(left);
    TypeId right_type = get_type_id(right);

    // Same type: use strict equality
    if (left_type == right_type) {
        return js_strict_equal(left, right);
    }

    // null == undefined
    if ((left_type == LMD_TYPE_NULL && right_type == LMD_TYPE_UNDEFINED) ||
        (left_type == LMD_TYPE_UNDEFINED && right_type == LMD_TYPE_NULL)) {
        return (Item){.item = b2it(true)};
    }

    // Number comparisons
    if ((left_type == LMD_TYPE_INT || left_type == LMD_TYPE_FLOAT) &&
        (right_type == LMD_TYPE_INT || right_type == LMD_TYPE_FLOAT)) {
        double l = js_get_number(left);
        double r = js_get_number(right);
        return (Item){.item = b2it(l == r)};
    }

    // String to number
    if ((left_type == LMD_TYPE_STRING && (right_type == LMD_TYPE_INT || right_type == LMD_TYPE_FLOAT)) ||
        ((left_type == LMD_TYPE_INT || left_type == LMD_TYPE_FLOAT) && right_type == LMD_TYPE_STRING)) {
        double l = js_get_number(left);
        double r = js_get_number(right);
        return (Item){.item = b2it(l == r)};
    }

    // Boolean to number
    if (left_type == LMD_TYPE_BOOL) {
        return js_equal(js_to_number(left), right);
    }
    if (right_type == LMD_TYPE_BOOL) {
        return js_equal(left, js_to_number(right));
    }

    return (Item){.item = b2it(false)};
}

extern "C" Item js_not_equal(Item left, Item right) {
    Item eq = js_equal(left, right);
    return (Item){.item = b2it(!it2b(eq))};
}

extern "C" Item js_strict_equal(Item left, Item right) {
    TypeId left_type = get_type_id(left);
    TypeId right_type = get_type_id(right);

    // In JS, all numeric types (int, int64, float) are the same "number" type
    // so int 0 === float 0.0 should be true (strict equality within number)
    bool left_is_num = (left_type == LMD_TYPE_INT || left_type == LMD_TYPE_INT64 || left_type == LMD_TYPE_FLOAT);
    bool right_is_num = (right_type == LMD_TYPE_INT || right_type == LMD_TYPE_INT64 || right_type == LMD_TYPE_FLOAT);
    if (left_is_num && right_is_num) {
        double l = js_get_number(left);
        double r = js_get_number(right);
        if (isnan(l) || isnan(r)) return (Item){.item = b2it(false)};
        return (Item){.item = b2it(l == r)};
    }

    // Different types are never strictly equal
    if (left_type != right_type) {
        return (Item){.item = b2it(false)};
    }

    switch (left_type) {
    case LMD_TYPE_NULL:
    case LMD_TYPE_UNDEFINED:
        return (Item){.item = b2it(true)};

    case LMD_TYPE_BOOL:
        return (Item){.item = b2it(it2b(left) == it2b(right))};

    case LMD_TYPE_INT:
        return (Item){.item = b2it(it2i(left) == it2i(right))};

    case LMD_TYPE_FLOAT: {
        double l = it2d(left);
        double r = it2d(right);
        // NaN !== NaN
        if (isnan(l) || isnan(r)) {
            return (Item){.item = b2it(false)};
        }
        return (Item){.item = b2it(l == r)};
    }

    case LMD_TYPE_STRING: {
        String* l_str = it2s(left);
        String* r_str = it2s(right);
        if (l_str->len != r_str->len) {
            return (Item){.item = b2it(false)};
        }
        return (Item){.item = b2it(memcmp(l_str->chars, r_str->chars, l_str->len) == 0)};
    }

    default:
        // Object identity comparison
        return (Item){.item = b2it(left.item == right.item)};
    }
}

extern "C" Item js_strict_not_equal(Item left, Item right) {
    Item eq = js_strict_equal(left, right);
    return (Item){.item = b2it(!it2b(eq))};
}

extern "C" Item js_less_than(Item left, Item right) {
    TypeId left_type = get_type_id(left);
    TypeId right_type = get_type_id(right);

    // String comparison
    if (left_type == LMD_TYPE_STRING && right_type == LMD_TYPE_STRING) {
        String* l_str = it2s(left);
        String* r_str = it2s(right);
        int cmp = memcmp(l_str->chars, r_str->chars,
                        l_str->len < r_str->len ? l_str->len : r_str->len);
        if (cmp == 0) {
            return (Item){.item = b2it(l_str->len < r_str->len)};
        }
        return (Item){.item = b2it(cmp < 0)};
    }

    // Numeric comparison
    double l = js_get_number(left);
    double r = js_get_number(right);
    if (isnan(l) || isnan(r)) {
        return (Item){.item = b2it(false)};
    }
    return (Item){.item = b2it(l < r)};
}

extern "C" Item js_less_equal(Item left, Item right) {
    Item gt = js_greater_than(left, right);
    return (Item){.item = b2it(!it2b(gt))};
}

extern "C" Item js_greater_than(Item left, Item right) {
    return js_less_than(right, left);
}

extern "C" Item js_greater_equal(Item left, Item right) {
    Item lt = js_less_than(left, right);
    return (Item){.item = b2it(!it2b(lt))};
}

// =============================================================================
// Logical Operators
// =============================================================================

extern "C" Item js_logical_and(Item left, Item right) {
    // Returns left if falsy, otherwise right
    if (!js_is_truthy(left)) {
        return left;
    }
    return right;
}

extern "C" Item js_logical_or(Item left, Item right) {
    // Returns left if truthy, otherwise right
    if (js_is_truthy(left)) {
        return left;
    }
    return right;
}

extern "C" Item js_logical_not(Item operand) {
    return (Item){.item = b2it(!js_is_truthy(operand))};
}

// =============================================================================
// Bitwise Operators
// =============================================================================

// JavaScript ToInt32: non-finite values → 0, large values wrap modulo 2^32
static inline int32_t js_to_int32(double d) {
    if (!isfinite(d) || d == 0.0) return 0;
    // Modulo 2^32, then interpret as signed
    double d2 = fmod(trunc(d), 4294967296.0);
    if (d2 < 0) d2 += 4294967296.0;
    return (d2 >= 2147483648.0) ? (int32_t)(d2 - 4294967296.0) : (int32_t)d2;
}

// JIT-callable version of ToInt32: takes double, returns int64 for MIR compatibility
extern "C" int64_t js_double_to_int32(double d) {
    return (int64_t)js_to_int32(d);
}

extern "C" Item js_bitwise_and(Item left, Item right) {
    int32_t l = js_to_int32(js_get_number(left));
    int32_t r = js_to_int32(js_get_number(right));
    return (Item){.item = i2it(l & r)};
}

extern "C" Item js_bitwise_or(Item left, Item right) {
    int32_t l = js_to_int32(js_get_number(left));
    int32_t r = js_to_int32(js_get_number(right));
    return (Item){.item = i2it(l | r)};
}

extern "C" Item js_bitwise_xor(Item left, Item right) {
    int32_t l = js_to_int32(js_get_number(left));
    int32_t r = js_to_int32(js_get_number(right));
    return (Item){.item = i2it(l ^ r)};
}

extern "C" Item js_bitwise_not(Item operand) {
    int32_t val = js_to_int32(js_get_number(operand));
    return (Item){.item = i2it(~val)};
}

extern "C" Item js_left_shift(Item left, Item right) {
    int32_t l = js_to_int32(js_get_number(left));
    uint32_t r = (uint32_t)js_to_int32(js_get_number(right)) & 0x1F;
    return (Item){.item = i2it(l << r)};
}

extern "C" Item js_right_shift(Item left, Item right) {
    int32_t l = js_to_int32(js_get_number(left));
    uint32_t r = (uint32_t)js_to_int32(js_get_number(right)) & 0x1F;
    return (Item){.item = i2it(l >> r)};
}

extern "C" Item js_unsigned_right_shift(Item left, Item right) {
    uint32_t l = (uint32_t)js_to_int32(js_get_number(left));
    uint32_t r = (uint32_t)js_to_int32(js_get_number(right)) & 0x1F;
    return (Item){.item = i2it((int64_t)(l >> r))};
}

// =============================================================================
// Unary Operators
// =============================================================================

extern "C" Item js_unary_plus(Item operand) {
    return js_to_number(operand);
}

extern "C" Item js_unary_minus(Item operand) {
    return fn_neg(js_to_number(operand));
}

extern "C" Item js_typeof(Item value) {
    TypeId type = get_type_id(value);

    const char* result;
    switch (type) {
    case LMD_TYPE_UNDEFINED:
        result = "undefined";
        break;
    case LMD_TYPE_NULL:
        result = "object";  // typeof null === "object" (JS quirk)
        break;
    case LMD_TYPE_BOOL:
        result = "boolean";
        break;
    case LMD_TYPE_INT:
    case LMD_TYPE_FLOAT:
        result = js_key_is_symbol(value) ? "symbol" : "number";
        break;
    case LMD_TYPE_STRING:
        result = "string";
        break;
    case LMD_TYPE_SYMBOL:
        result = "symbol";
        break;
    case LMD_TYPE_FUNC:
        result = "function";
        break;
    default:
        result = "object";
        break;
    }

    return (Item){.item = s2it(heap_create_name(result))};
}

// =============================================================================
// Object Functions
// =============================================================================

// JsFunction struct: defined here so js_property_get/set can access .prototype
struct JsFunction {
    TypeId type_id;  // Always LMD_TYPE_FUNC
    void* func_ptr;  // Pointer to the compiled function
    int param_count; // Number of parameters (user-visible, not including env)
    Item* env;       // Closure environment (NULL for non-closures)
    int env_size;    // Number of captured variables in env
    Item prototype;  // Constructor prototype (Foo.prototype = {...})
    Item bound_this; // v11: bound 'this' (0 if not a bound function)
    Item* bound_args; // v11: pre-applied arguments (NULL if none)
    int bound_argc;  // v11: number of bound arguments
};

extern "C" void* js_function_get_ptr(Item fn_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return NULL;
    // Try JsFunction layout first (func_ptr at offset 8)
    JsFunction* jsfn = (JsFunction*)fn_item.function;
    if (jsfn->func_ptr) return jsfn->func_ptr;
    // Fall back to Function layout (ptr at offset 16)
    Function* fn = fn_item.function;
    return (void*)fn->ptr;
}

extern "C" int js_function_get_arity(Item fn_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return 0;
    JsFunction* jsfn = (JsFunction*)fn_item.function;
    // If func_ptr (offset 8) is set, it's JsFunction layout
    if (jsfn->func_ptr) return jsfn->param_count;
    // Otherwise it's Function layout — arity at offset 1
    Function* fn = fn_item.function;
    return fn->arity;
}

// P2: Pre-computed size class for sizeof(Map) = 32 bytes → SIZE_CLASSES[1] = 32.
// Skips the class-index lookup in gc_heap_alloc and uses the bump-pointer fast path.
#define JS_MAP_SIZE_CLASS 1

// Create a new JS object as a Lambda Map (empty, using map_put for dynamic keys)
extern "C" Item js_new_object() {
    Map* m = (Map*)heap_calloc_class(sizeof(Map), LMD_TYPE_MAP, JS_MAP_SIZE_CLASS);
    m->type_id = LMD_TYPE_MAP;
    m->type = &EmptyMap;
    return (Item){.map = m};
}

// Create a new object for a constructor call: sets __proto__ from callee.prototype
extern "C" Item js_constructor_create_object(Item callee) {
    Item obj = js_new_object();
    if (get_type_id(callee) == LMD_TYPE_FUNC) {
        JsFunction* fn = (JsFunction*)callee.function;
        if (fn->prototype.item != ItemNull.item && get_type_id(fn->prototype) == LMD_TYPE_MAP) {
            js_set_prototype(obj, fn->prototype);
        }
    }
    return obj;
}

// A5: Create a new object with pre-built shape for constructor optimization.
// All property slots are pre-allocated as LMD_TYPE_NULL (8-byte pointer-sized)
// and initialized to null. When the constructor body sets this.prop = val,
// js_property_set will find the existing key via hash table and do a fast
// in-place update instead of extending the shape.
extern "C" Item js_new_object_with_shape(const char** prop_names, const int* prop_lens, int count) {
    if (!js_input || count <= 0) return js_new_object();

    Map* m = (Map*)heap_calloc_class(sizeof(Map), LMD_TYPE_MAP, JS_MAP_SIZE_CLASS);
    m->type_id = LMD_TYPE_MAP;

    // Allocate TypeMap
    TypeMap* tm = (TypeMap*)alloc_type(js_input->pool, LMD_TYPE_MAP, sizeof(TypeMap));
    if (!tm) { m->type = &EmptyMap; return (Item){.map = m}; }

    // Build ShapeEntry chain — each slot is LMD_TYPE_NULL (sizeof(void*) = 8 bytes).
    // This gives correct 8-byte spacing so INT, FLOAT, STRING, MAP, FUNC etc.
    // all fit in-place via the NULL→same-byte-size fast path in fn_map_set.
    ShapeEntry* first = NULL;
    ShapeEntry* prev = NULL;
    for (int i = 0; i < count; i++) {
        ShapeEntry* se = (ShapeEntry*)pool_calloc(js_input->pool, sizeof(ShapeEntry) + sizeof(StrView));
        StrView* nv = (StrView*)((char*)se + sizeof(ShapeEntry));
        String* key_str = heap_create_name(prop_names[i], (size_t)prop_lens[i]);
        nv->str = key_str->chars;
        nv->length = key_str->len;
        se->name = nv;
        se->type = type_info[LMD_TYPE_NULL].type;
        se->byte_offset = i * (int)sizeof(void*);  // 8-byte slots
        se->next = NULL;
        if (prev) prev->next = se;
        else first = se;
        prev = se;
    }
    tm->shape = first;
    tm->last = prev;
    tm->length = count;
    tm->byte_size = count * (int)sizeof(void*);

    // Populate hash table
    ShapeEntry* se = first;
    while (se) {
        typemap_hash_insert(tm, se);
        se = se->next;
    }

    // Allocate data buffer (pre-sized, zero-initialized = null pointers)
    int data_size = count * (int)sizeof(void*);
    int data_cap = data_size < 64 ? 64 : data_size;
    m->data = pool_calloc(js_input->pool, data_cap);
    m->data_cap = data_cap;
    m->type = tm;

    return (Item){.map = m};
}

// A5: Create pre-shaped object and set __proto__ from constructor's prototype
extern "C" Item js_constructor_create_object_shaped(Item callee,
    const char** prop_names, const int* prop_lens, int count) {
    Item obj = js_new_object_with_shape(prop_names, prop_lens, count);
    if (get_type_id(callee) == LMD_TYPE_FUNC) {
        JsFunction* fn = (JsFunction*)callee.function;
        if (fn->prototype.item != ItemNull.item && get_type_id(fn->prototype) == LMD_TYPE_MAP) {
            js_set_prototype(obj, fn->prototype);
        }
    }
    return obj;
}

// P3/P4: Slot-indexed property access for shaped (constructor-created) objects.
// These bypass the hash-table lookup in js_property_get/set by walking the
// ShapeEntry linked list to the N-th slot (O(slot) ≈ O(2-4) for typical classes).
//
// js_get_shaped_slot: read property at slot index → returns correctly boxed Item
// js_set_shaped_slot: write property at slot index, updates ShapeEntry type

extern "C" Item js_get_shaped_slot(Item object, int64_t slot) {
    if (get_type_id(object) != LMD_TYPE_MAP) return ItemNull;
    Map* m = (Map*)object.map;
    TypeMap* tm = (TypeMap*)m->type;
    if (!tm || !tm->shape) return ItemNull;
    ShapeEntry* entry = tm->shape;
    for (int i = 0; i < (int)slot && entry; i++) entry = entry->next;
    if (!entry) return ItemNull;
    return _map_read_field(entry, m->data);
}

extern "C" void js_set_shaped_slot(Item object, int64_t slot, Item value) {
    if (get_type_id(object) != LMD_TYPE_MAP) return;
    Map* m = (Map*)object.map;
    TypeMap* tm = (TypeMap*)m->type;
    if (!tm || !tm->shape) return;
    ShapeEntry* entry = tm->shape;
    for (int i = 0; i < (int)slot && entry; i++) entry = entry->next;
    if (!entry) return;
    void* field_ptr = (char*)m->data + entry->byte_offset;
    TypeId value_type = get_type_id(value);
    TypeId field_type = entry->type->type_id;
    // Store with correct type-aware unboxing (all shaped slots are 8 bytes).
    switch (value_type) {
    case LMD_TYPE_INT:
        *(int64_t*)field_ptr = value.get_int56();
        break;
    case LMD_TYPE_FLOAT:
        *(double*)field_ptr = value.get_double();
        break;
    case LMD_TYPE_MAP: case LMD_TYPE_ELEMENT: case LMD_TYPE_OBJECT:
    case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_INT: case LMD_TYPE_ARRAY_INT64:
    case LMD_TYPE_ARRAY_FLOAT: case LMD_TYPE_RANGE:
        *(Container**)field_ptr = value.container;
        break;
    case LMD_TYPE_STRING: case LMD_TYPE_SYMBOL: case LMD_TYPE_BINARY: {
        String* s = value.get_string();
        *(String**)field_ptr = s;
        break;
    }
    case LMD_TYPE_FUNC: case LMD_TYPE_DECIMAL: case LMD_TYPE_TYPE:
    case LMD_TYPE_PATH: case LMD_TYPE_VMAP:
        *(void**)field_ptr = (void*)(uintptr_t)(value.item & 0x00FFFFFFFFFFFFFFULL);
        break;
    case LMD_TYPE_BOOL:
        *(bool*)field_ptr = value.bool_val;
        break;
    case LMD_TYPE_NULL:
        *(void**)field_ptr = NULL;
        break;
    default:
        // Unexpected type: skip to prevent slot corruption
        log_debug("js_set_shaped_slot: unhandled type %d at slot %d", (int)value_type, (int)slot);
        return;
    }
    // Update ShapeEntry type in-place (NULL→type is the common constructor init path).
    // All shaped slots are 8 bytes so no reshape is needed.
    if (field_type != value_type && value_type != LMD_TYPE_NULL) {
        entry->type = type_info[value_type].type;
    }
}

// Forward declaration for prototype chain support
extern "C" Item js_prototype_lookup(Item object, Item property);

// P10f: Fast property lookup for JS objects.
// Like _map_get but avoids strncmp+strlen overhead by using pre-computed key_len.
// Still uses last-writer-wins since type changes can create duplicate shape entries.
// Also handles spread/nested map entries (field->name == NULL).
static Item js_map_get_fast(Map* m, const char* key_str, int key_len, bool* out_found = nullptr) {
    TypeMap* map_type = (TypeMap*)m->type;
    if (!map_type || !map_type->shape) { if (out_found) *out_found = false; return ItemNull; }

    // A1: Try hash table first for O(1) lookup (covers >99% of JS objects).
    // The hash table uses last-writer-wins via typemap_hash_insert, so the
    // entry in the table always points to the latest ShapeEntry for that name.
    if (map_type->field_count > 0) {
        ShapeEntry* entry = typemap_hash_lookup(map_type, key_str, key_len);
        if (entry) {
            if (out_found) *out_found = true;
            return _map_read_field(entry, m->data);
        }
        // Not found in hash table — may have overflowed (capacity=32).
        // Walk all named fields linearly to catch overflow entries, and also
        // check unnamed (nested/spread) entries.
        ShapeEntry* field = map_type->shape;
        Item overflow_result = ItemNull;
        bool found = false;
        while (field) {
            if (!field->name) {
                Map* nested_map = *(Map**)((char*)m->data + field->byte_offset);
                if (nested_map && nested_map->type_id == LMD_TYPE_MAP) {
                    bool nested_found;
                    Item nested_result = _map_get((TypeMap*)nested_map->type, nested_map->data, (char*)key_str, &nested_found);
                    if (nested_found) { if (out_found) *out_found = true; return nested_result; }
                }
            } else if (field->name->str == key_str ||  // A6: interned pointer match
                       (field->name->length == (size_t)key_len &&
                        memcmp(field->name->str, key_str, key_len) == 0)) {
                overflow_result = _map_read_field(field, m->data);
                found = true;
            }
            field = field->next;
        }
        if (out_found) *out_found = found;
        return overflow_result;
    }

    // Fallback: linear scan for objects without hash table
    ShapeEntry* field = map_type->shape;
    Item result = ItemNull;
    bool found = false;
    while (field) {
        if (!field->name) {
            Map* nested_map = *(Map**)((char*)m->data + field->byte_offset);
            if (nested_map && nested_map->type_id == LMD_TYPE_MAP) {
                bool nested_found;
                Item nested_result = _map_get((TypeMap*)nested_map->type, nested_map->data, (char*)key_str, &nested_found);
                if (nested_found) {
                    result = nested_result;
                    found = true;
                }
            }
        } else if (field->name->str == key_str ||  // A6: interned pointer match
                   (field->name->length == (size_t)key_len &&
                    memcmp(field->name->str, key_str, key_len) == 0)) {
            result = _map_read_field(field, m->data);
            found = true;
        }
        field = field->next;
    }
    if (out_found) *out_found = found;
    return result;
}

// P10d: Interned __proto__ key — avoid heap_create_name on every prototype lookup.
// Initialized lazily on first use.
static Item js_proto_key_item = {0};
static Item js_get_proto_key() {
    if (js_proto_key_item.item == 0) {
        js_proto_key_item.item = s2it(heap_create_name("__proto__", 9));
    }
    return js_proto_key_item;
}

extern "C" Item js_property_get(Item object, Item key) {
    // Convert Symbol keys to unique string keys for property lookup
    if (js_key_is_symbol(key)) key = js_symbol_to_key(key);
    TypeId type = get_type_id(object);

    if (type == LMD_TYPE_MAP) {
        Map* m = object.map;
        // Check if this is a typed array
        if (js_is_typed_array(object)) {
            if (get_type_id(key) == LMD_TYPE_STRING) {
                String* str_key = it2s(key);
                if (str_key->len == 6 && strncmp(str_key->chars, "length", 6) == 0) {
                    return (Item){.item = i2it(js_typed_array_length(object))};
                }
                if (str_key->len == 10 && strncmp(str_key->chars, "byteLength", 10) == 0) {
                    JsTypedArray* ta = (JsTypedArray*)object.map->data;
                    return (Item){.item = i2it(ta->byte_length)};
                }
                if (str_key->len == 10 && strncmp(str_key->chars, "byteOffset", 10) == 0) {
                    JsTypedArray* ta = (JsTypedArray*)object.map->data;
                    return (Item){.item = i2it(ta->byte_offset)};
                }
                if (str_key->len == 6 && strncmp(str_key->chars, "buffer", 6) == 0) {
                    JsTypedArray* ta = (JsTypedArray*)object.map->data;
                    if (!ta->buffer) {
                        // Create a backing ArrayBuffer lazily for standalone typed arrays
                        JsArrayBuffer* ab = (JsArrayBuffer*)malloc(sizeof(JsArrayBuffer));
                        ab->data = ta->data;  // share the same data pointer
                        ab->byte_length = ta->byte_length;
                        ta->buffer = ab;
                    }
                    return js_arraybuffer_wrap(ta->buffer);
                }
                if (str_key->len == 17 && strncmp(str_key->chars, "BYTES_PER_ELEMENT", 17) == 0) {
                    JsTypedArray* ta = (JsTypedArray*)object.map->data;
                    int bpe = 4;
                    switch (ta->element_type) {
                    case JS_TYPED_INT8: case JS_TYPED_UINT8: bpe = 1; break;
                    case JS_TYPED_INT16: case JS_TYPED_UINT16: bpe = 2; break;
                    case JS_TYPED_INT32: case JS_TYPED_UINT32: case JS_TYPED_FLOAT32: bpe = 4; break;
                    case JS_TYPED_FLOAT64: bpe = 8; break;
                    }
                    return (Item){.item = i2it(bpe)};
                }
            }
            return js_typed_array_get(object, key);
        }
        // Check if this is an ArrayBuffer
        if (js_is_arraybuffer(object)) {
            if (get_type_id(key) == LMD_TYPE_STRING) {
                String* str_key = it2s(key);
                if (str_key->len == 10 && strncmp(str_key->chars, "byteLength", 10) == 0) {
                    return (Item){.item = i2it(js_arraybuffer_byte_length(object))};
                }
            }
            return (Item){.item = ITEM_NULL};
        }
        // Check if this is a DataView
        if (js_is_dataview(object)) {
            if (get_type_id(key) == LMD_TYPE_STRING) {
                String* str_key = it2s(key);
                if (str_key->len == 10 && strncmp(str_key->chars, "byteLength", 10) == 0) {
                    JsDataView* dv = (JsDataView*)object.map->data;
                    return (Item){.item = i2it(dv->byte_length)};
                }
                if (str_key->len == 10 && strncmp(str_key->chars, "byteOffset", 10) == 0) {
                    JsDataView* dv = (JsDataView*)object.map->data;
                    return (Item){.item = i2it(dv->byte_offset)};
                }
                if (str_key->len == 6 && strncmp(str_key->chars, "buffer", 6) == 0) {
                    JsDataView* dv = (JsDataView*)object.map->data;
                    if (dv->buffer) {
                        return js_arraybuffer_wrap(dv->buffer);
                    }
                    return (Item){.item = ITEM_NULL};
                }
            }
            return (Item){.item = ITEM_NULL};
        }
        // Check if this is a DOM node wrapper (indicated by js_dom_type_marker)
        if (js_is_dom_node(object)) {
            return js_dom_get_property(object, key);
        }
        // Check if this is a computed style wrapper
        if (js_is_computed_style_item(object)) {
            return js_computed_style_get_property(object, key);
        }
        // Regular Lambda map (including JS objects)
        // P10f: Use fast lookup with pre-computed key length (memcmp instead of strncmp+strlen)
        Item result = ItemNull;
        bool own_found = false;
        if (key._type_id == LMD_TYPE_STRING || key._type_id == LMD_TYPE_SYMBOL) {
            const char* key_str = key.get_chars();
            int key_len = (int)key.get_len();
            result = js_map_get_fast(object.map, key_str, key_len, &own_found);
        } else {
            result = map_get(object.map, key);  // fallback for non-string keys
            own_found = (result.item != ItemNull.item);
        }
        if (own_found) return result;
        // Prototype chain fallback: if property not found on own object, walk __proto__
        result = js_prototype_lookup(object, key);
        if (result.item != ItemNull.item) return result;
        // Getter property fallback: check for __get_<propName> on object or prototype
        // Only check for getter if the key doesn't start with '_' (private properties
        // never have getters) and is short enough to be a getter name
        if (key._type_id == LMD_TYPE_STRING) {
            String* str_key = it2s(key);
            if (str_key->len < 64 && str_key->len > 0 && str_key->chars[0] != '_') {
                char getter_key[256];
                snprintf(getter_key, sizeof(getter_key), "__get_%.*s", (int)str_key->len, str_key->chars);
                // Use name pool lookup instead of heap allocation
                Item gk = (Item){.item = s2it(heap_create_name(getter_key, strlen(getter_key)))};
                Item getter = map_get(object.map, gk);
                if (getter.item == ItemNull.item) {
                    getter = js_prototype_lookup(object, gk);
                }
                if (getter.item != ItemNull.item && get_type_id(getter) == LMD_TYPE_FUNC) {
                    // Invoke getter with this = object (0 args)
                    return js_call_function(getter, object, NULL, 0);
                }
            }
        }
        // Property not found — return undefined (JS semantics)
        return make_js_undefined();
    } else if (type == LMD_TYPE_ELEMENT) {
        return elmt_get(object.element, key);
    } else if (type == LMD_TYPE_ARRAY) {
        // Array index access
        if (get_type_id(key) == LMD_TYPE_STRING) {
            String* str_key = it2s(key);
            // Check for "length" property
            if (str_key->len == 6 && strncmp(str_key->chars, "length", 6) == 0) {
                return (Item){.item = i2it(object.array->length)};
            }
            // Only allow numeric string keys for array index access
            if (str_key->len == 0 || (str_key->chars[0] < '0' || str_key->chars[0] > '9')) {
                return make_js_undefined();
            }
        }
        // Numeric index access
        int idx = (int)js_get_number(key);
        if (idx >= 0 && idx < object.array->length) {
            return object.array->items[idx];
        }
        return make_js_undefined();
    } else if (type == LMD_TYPE_STRING) {
        // String character access: str[index]
        String* str = it2s(object);
        if (get_type_id(key) == LMD_TYPE_STRING) {
            String* str_key = it2s(key);
            if (str_key->len == 6 && strncmp(str_key->chars, "length", 6) == 0) {
                return (Item){.item = i2it(str->len)};
            }
        }
        int idx = (int)js_get_number(key);
        if (idx >= 0 && idx < (int)str->len) {
            char ch[2] = { str->chars[idx], '\0' };
            return (Item){.item = s2it(heap_create_name(ch))};
        }
        return make_js_undefined();
    }

    // Function: reading .prototype property
    // Lazy initialization: create an empty prototype object on first access.
    // This is needed for patterns like `Foo.prototype.method = function(){}`
    // where prototype must be a real object, not null.
    if (type == LMD_TYPE_FUNC) {
        JsFunction* fn = (JsFunction*)object.function;
        if (get_type_id(key) == LMD_TYPE_STRING) {
            String* str_key = it2s(key);
            if (str_key->len == 9 && strncmp(str_key->chars, "prototype", 9) == 0) {
                if (fn->prototype.item == ItemNull.item) {
                    fn->prototype = js_new_object();
                    heap_register_gc_root(&fn->prototype.item);
                }
                return fn->prototype;
            }
        }
    }

    return make_js_undefined();
}

extern "C" Item js_property_set(Item object, Item key, Item value) {
    // Convert Symbol keys to unique string keys for property storage
    if (js_key_is_symbol(key)) key = js_symbol_to_key(key);
    TypeId type = get_type_id(object);

    // Array: result[i] = val or arr.length = n
    if (type == LMD_TYPE_ARRAY) {
        // Handle arr.length = newLength (resize array)
        if (get_type_id(key) == LMD_TYPE_STRING) {
            String* str_key = it2s(key);
            if (str_key->len == 6 && strncmp(str_key->chars, "length", 6) == 0) {
                int new_len = (int)js_get_number(value);
                Array* arr = object.array;
                if (new_len >= 0) {
                    if (new_len > arr->length) {
                        // Extend: ensure capacity and fill with undefined.
                        // Use direct realloc to avoid GC-triggering array_push loops.
                        if (new_len + 4 > arr->capacity) {
                            int new_cap = new_len + 4;
                            Item* new_items = (Item*)malloc(new_cap * sizeof(Item));
                            if (arr->items && arr->length > 0) {
                                memcpy(new_items, arr->items, arr->length * sizeof(Item));
                            }
                            // Note: old items may be malloc'd or data-zone allocated.
                            // If malloc'd, we should free. If data-zone, it's abandoned.
                            // For simplicity, we don't free (matches expand_list behavior).
                            arr->items = new_items;
                            arr->capacity = new_cap;
                        }
                        // Fill new slots with undefined
                        Item undef = make_js_undefined();
                        for (int i = arr->length; i < new_len; i++) {
                            arr->items[i] = undef;
                        }
                        arr->length = new_len;
                    } else if (new_len < arr->length) {
                        // Truncate
                        arr->length = new_len;
                    }
                }
                return value;
            }
        }
        return js_array_set(object, key, value);
    }

    // Typed array: ta[i] = val
    if (type == LMD_TYPE_MAP && js_is_typed_array(object)) {
        return js_typed_array_set(object, key, value);
    }

    if (type == LMD_TYPE_MAP) {
        Map* m = object.map;
        // Check if this is a DOM node wrapper (indicated by js_dom_type_marker)
        if (js_is_dom_node(object)) {
            return js_dom_set_property(object, key, value);
        }
        // Setter property dispatch: check for __set_<propName> on object or prototype
        if (get_type_id(key) == LMD_TYPE_STRING) {
            String* str_key = it2s(key);
            if (str_key && str_key->len < 64 && str_key->len > 0 && str_key->chars[0] != '_') {
                char setter_key[256];
                snprintf(setter_key, sizeof(setter_key), "__set_%.*s", (int)str_key->len, str_key->chars);
                Item sk = (Item){.item = s2it(heap_create_name(setter_key, strlen(setter_key)))};
                Item setter = map_get(m, sk);
                if (setter.item == ItemNull.item) {
                    setter = js_prototype_lookup(object, sk);
                }
                if (setter.item != ItemNull.item && get_type_id(setter) == LMD_TYPE_FUNC) {
                    Item args[1] = { value };
                    js_call_function(setter, object, args, 1);
                    return value;
                }
            }
        }
        // JS object / Lambda map: try fn_map_set first (update existing field),
        // fall back to map_put for new keys
        TypeMap* map_type = (TypeMap*)m->type;
        if (map_type && map_type != &EmptyMap && map_type->shape) {
            // search for existing key
            String* str_key = NULL;
            TypeId key_type = get_type_id(key);
            if (key_type == LMD_TYPE_STRING) str_key = it2s(key);
            else if (key_type == LMD_TYPE_SYMBOL) str_key = it2s(key);
            if (str_key) {
                // A1: Use hash table for O(1) existing-key check
                if (map_type->field_count > 0) {
                    ShapeEntry* found = typemap_hash_lookup(map_type, str_key->chars, (int)str_key->len);
                    if (found) {
                        fn_map_set(object, key, value);
                        return value;
                    }
                } else {
                    ShapeEntry* entry = map_type->shape;
                    while (entry) {
                        if (entry->name && (entry->name->str == str_key->chars ||  // A6: interned pointer
                            (entry->name->length == (size_t)str_key->len
                             && strncmp(entry->name->str, str_key->chars, str_key->len) == 0))) {
                            fn_map_set(object, key, value);
                            return value;
                        }
                        entry = entry->next;
                    }
                }
            }
        }
        // key not found or empty map — add new key via map_put
        if (js_input) {
            String* str_key = NULL;
            TypeId key_type = get_type_id(key);
            if (key_type == LMD_TYPE_STRING) str_key = it2s(key);
            else if (key_type == LMD_TYPE_SYMBOL) str_key = it2s(key);
            if (str_key) {
                map_put(m, str_key, value, js_input);
            }
        } else {
            log_error("js_property_set: no js_input context for map_put");
        }
        return value;
    }

    // Function: setting .prototype on a function (constructor pattern)
    if (type == LMD_TYPE_FUNC) {
        JsFunction* fn = (JsFunction*)object.function;
        String* str_key = NULL;
        TypeId key_type = get_type_id(key);
        if (key_type == LMD_TYPE_STRING) str_key = it2s(key);
        if (str_key && str_key->len == 9 && strncmp(str_key->chars, "prototype", 9) == 0) {
            fn->prototype = value;
            // Register fn->prototype as a GC root so the prototype map survives GC.
            // JsFunction is pool-allocated (invisible to GC), but fn->prototype points
            // to a GC-managed map. Without this, the prototype gets collected when no
            // live objects reference it via __proto__.
            heap_register_gc_root(&fn->prototype.item);
            return value;
        }
    }

    return value;
}

extern "C" Item js_property_access(Item object, Item key) {
    // Same as js_property_get but used for member expressions
    return js_property_get(object, key);
}

// Get the length of any JS value. Handles typed arrays specially (Map-based),
// and delegates to fn_len for all standard Lambda types (array, list, string, etc.)
extern "C" int64_t js_get_length(Item object) {
    if (get_type_id(object) == LMD_TYPE_MAP && js_is_typed_array(object)) {
        return (int64_t)js_typed_array_length(object);
    }
    return fn_len(object);
}

// =============================================================================
// Array Functions
// =============================================================================

extern "C" Item js_array_new(int length) {
    Array* arr = array();
    // Pre-allocate the array with space for all elements
    if (length > 0) {
        // Allocate items array directly
        arr->capacity = length + 4;
        arr->items = (Item*)malloc(arr->capacity * sizeof(Item));
        // Set length to the target size
        arr->length = length;
        // Initialize all slots to undefined
        Item undef = make_js_undefined();
        for (int i = 0; i < length; i++) {
            arr->items[i] = undef;
        }
    }
    return (Item){.array = arr};
}

// new Array(arg) — JS spec: if arg is a valid non-negative integer, create sparse
// array of that length. Otherwise, create a single-element array [arg].
// This matches the JS behavior where new Array(undefined) => [undefined] (length 1).
extern "C" Item js_array_new_from_item(Item arg) {
    TypeId type = get_type_id(arg);
    // Integer argument: create sparse array
    if (type == LMD_TYPE_INT) {
        int64_t len = it2i(arg);
        if (len >= 0 && len <= 0x7FFFFFFF) {
            return js_array_new((int)len);
        }
    }
    // Float argument: check if it's a non-negative integer value
    if (type == LMD_TYPE_FLOAT) {
        double d = it2d(arg);
        if (d >= 0.0 && d == (double)(int)d && d <= 2147483647.0) {
            return js_array_new((int)d);
        }
    }
    // Any other value (null, undefined, string, object, negative, non-integer float):
    // create a single-element array [arg]
    Item result = js_array_new(0);
    array_push(result.array, arg);
    return result;
}

extern "C" Item js_array_get(Item array, Item index) {
    if (get_type_id(array) != LMD_TYPE_ARRAY) {
        return make_js_undefined();
    }

    int idx = (int)js_get_number(index);
    Array* arr = array.array;

    if (idx >= 0 && idx < arr->length) {
        return arr->items[idx];
    }

    return make_js_undefined();
}

// P10e: Fast array access with native int index (no js_get_number overhead)
extern "C" Item js_array_get_int(Item array, int64_t index) {
    if (get_type_id(array) == LMD_TYPE_ARRAY) {
        Array* arr = array.array;
        if (index >= 0 && index < arr->length) {
            return arr->items[index];
        }
        return make_js_undefined();
    }
    // fall back to general property access for strings, maps, etc.
    return js_property_access(array, (Item){.item = i2it((int)index)});
}

// P10e: Fast array set with native int index
extern "C" Item js_array_set_int(Item array, int64_t index, Item value) {
    if (get_type_id(array) != LMD_TYPE_ARRAY) {
        return js_property_set(array, (Item){.item = i2it((int)index)}, value);
    }
    Array* arr = array.array;
    if (index >= 0 && index < arr->length) {
        arr->items[index] = value;
    } else if (index >= 0) {
        // Expand array: fill gaps with undefined, then set the value
        Item undef = make_js_undefined();
        while (arr->length < (int)index) {
            array_push(arr, undef);
        }
        if ((int)index == arr->length) {
            array_push(arr, value);
        } else {
            arr->items[index] = value;
        }
    }
    return value;
}

extern "C" Item js_array_set(Item array, Item index, Item value) {
    if (get_type_id(array) != LMD_TYPE_ARRAY) {
        return value;
    }

    int idx = (int)js_get_number(index);
    Array* arr = array.array;

    if (idx >= 0 && idx < arr->length) {
        arr->items[idx] = value;
    } else if (idx >= 0) {
        // Expand array: fill gaps with undefined, then set the value
        Item undef = make_js_undefined();
        while (arr->length < idx) {
            array_push(arr, undef);
        }
        if (idx == arr->length) {
            array_push(arr, value);
        } else {
            arr->items[idx] = value;
        }
    }

    return value;
}

extern "C" int64_t js_array_length(Item array) {
    if (get_type_id(array) != LMD_TYPE_ARRAY) {
        return 0;
    }
    return (int64_t)array.array->length;
}

extern "C" Item js_array_push(Item array, Item value) {
    if (get_type_id(array) != LMD_TYPE_ARRAY) {
        return (Item){.item = i2it(0)};
    }

    Array* arr = array.array;
    list_push(arr, value);
    return (Item){.item = i2it(arr->length)};
}

// =============================================================================
// Console Functions
// =============================================================================

extern "C" void js_console_log(Item value) {
    Item str = js_to_string(value);
    if (get_type_id(str) == LMD_TYPE_STRING) {
        String* s = it2s(str);
        printf("%.*s\n", (int)s->len, s->chars);
    }
}

// =============================================================================
// Function Functions
// =============================================================================

// (JsFunction struct defined above, before js_property_get/set)

// Cache: func_ptr → JsFunction*  (ensures same MIR function → same wrapper → same .prototype)
static const int JS_FUNC_CACHE_SIZE = 512;
static void* js_func_cache_keys[512];
static JsFunction* js_func_cache_vals[512];
static int js_func_cache_count = 0;

static JsFunction* js_func_cache_lookup(void* func_ptr) {
    for (int i = 0; i < js_func_cache_count; i++) {
        if (js_func_cache_keys[i] == func_ptr) return js_func_cache_vals[i];
    }
    return NULL;
}

static void js_func_cache_insert(void* func_ptr, JsFunction* fn) {
    if (js_func_cache_count < JS_FUNC_CACHE_SIZE) {
        js_func_cache_keys[js_func_cache_count] = func_ptr;
        js_func_cache_vals[js_func_cache_count] = fn;
        js_func_cache_count++;
    }
}

extern "C" Item js_new_function(void* func_ptr, int param_count) {
    if (!func_ptr) {
        log_error("js_new_function: null func_ptr! param_count=%d", param_count);
        return ItemNull;
    }
    // Return cached wrapper if the same MIR function was already wrapped.
    // This ensures Foo.prototype = {...} and (new Foo()) share the same JsFunction*.
    JsFunction* cached = js_func_cache_lookup(func_ptr);
    if (cached) return (Item){.function = (Function*)cached};

    // Pool-allocate: JS functions are module-lifetime objects that must not be
    // GC-collected (they live in pool-allocated env arrays unreachable from GC roots).
    JsFunction* fn = (JsFunction*)pool_calloc(js_input->pool, sizeof(JsFunction));
    fn->type_id = LMD_TYPE_FUNC;
    fn->func_ptr = func_ptr;
    fn->param_count = param_count;
    fn->env = NULL;
    fn->env_size = 0;
    fn->prototype = ItemNull;
    js_func_cache_insert(func_ptr, fn);
    return (Item){.function = (Function*)fn};
}

// Create a closure (function with captured environment)
extern "C" Item js_new_closure(void* func_ptr, int param_count, Item* env, int env_size) {
    // Pool-allocate: closures stored in env arrays are unreachable from GC roots
    // (env is pool-allocated, stack scan can't trace through pool to find them).
    JsFunction* fn = (JsFunction*)pool_calloc(js_input->pool, sizeof(JsFunction));
    fn->type_id = LMD_TYPE_FUNC;
    fn->func_ptr = func_ptr;
    fn->param_count = param_count;
    fn->env = env;
    fn->env_size = env_size;
    fn->prototype = ItemNull;
    return (Item){.function = (Function*)fn};
}

// Allocate closure environment (array of Item on the pool)
extern "C" Item* js_alloc_env(int count) {
    return (Item*)pool_calloc(js_input->pool, count * sizeof(Item));
}

// Invoke a JsFunction with args, handling env if it's a closure
static Item js_invoke_fn(JsFunction* fn, Item* args, int arg_count) {
    typedef Item (*P0)();
    typedef Item (*P1)(Item);
    typedef Item (*P2)(Item, Item);
    typedef Item (*P3)(Item, Item, Item);
    typedef Item (*P4)(Item, Item, Item, Item);
    typedef Item (*P5)(Item, Item, Item, Item, Item);
    typedef Item (*P6)(Item, Item, Item, Item, Item, Item);
    typedef Item (*P7)(Item, Item, Item, Item, Item, Item, Item);
    typedef Item (*P8)(Item, Item, Item, Item, Item, Item, Item, Item);

    // Pad missing arguments with undefined to match declared param count
    Item padded_args[8];
    Item undef = make_js_undefined();
    int effective_count = arg_count;
    Item* effective_args = args;

    if (arg_count < fn->param_count) {
        effective_count = fn->param_count;
        if (effective_count > 8) effective_count = 8;
        for (int i = 0; i < effective_count; i++) {
            padded_args[i] = (i < arg_count && args) ? args[i] : undef;
        }
        effective_args = padded_args;
    }

    if (fn->env) {
        // Closure: prepend env pointer as first argument
        Item env_item;
        env_item.item = (uint64_t)fn->env;
        switch (effective_count) {
            case 0: return ((P1)fn->func_ptr)(env_item);
            case 1: return ((P2)fn->func_ptr)(env_item, effective_args[0]);
            case 2: return ((P3)fn->func_ptr)(env_item, effective_args[0], effective_args[1]);
            case 3: return ((P4)fn->func_ptr)(env_item, effective_args[0], effective_args[1], effective_args[2]);
            case 4: return ((P5)fn->func_ptr)(env_item, effective_args[0], effective_args[1], effective_args[2], effective_args[3]);
            case 5: return ((P6)fn->func_ptr)(env_item, effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4]);
            case 6: return ((P7)fn->func_ptr)(env_item, effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5]);
            case 7: return ((P8)fn->func_ptr)(env_item, effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5], effective_args[6]);
            default:
                log_error("js_invoke_fn: too many args for closure (%d)", effective_count);
                return ItemNull;
        }
    } else {
        switch (effective_count) {
            case 0: return ((P0)fn->func_ptr)();
            case 1: return ((P1)fn->func_ptr)(effective_args[0]);
            case 2: return ((P2)fn->func_ptr)(effective_args[0], effective_args[1]);
            case 3: return ((P3)fn->func_ptr)(effective_args[0], effective_args[1], effective_args[2]);
            case 4: return ((P4)fn->func_ptr)(effective_args[0], effective_args[1], effective_args[2], effective_args[3]);
            case 5: return ((P5)fn->func_ptr)(effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4]);
            case 6: return ((P6)fn->func_ptr)(effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5]);
            case 7: return ((P7)fn->func_ptr)(effective_args[0], effective_args[1], effective_args[2], effective_args[3], effective_args[4], effective_args[5], effective_args[6]);
            default:
                log_error("js_invoke_fn: too many args (%d)", effective_count);
                return ItemNull;
        }
    }
}

// Call a JavaScript function stored as an Item
static int js_call_count = 0;

// Debug: check callee before calling, print site info if null
extern "C" Item js_debug_check_callee(Item callee, int64_t site_id) {
    if (get_type_id(callee) != LMD_TYPE_FUNC) {
        log_debug("js_debug_check_callee: non-function callee at site_id=%lld (type=%d, call_count=%d)",
            (long long)site_id, get_type_id(callee), js_call_count);
    }
    return ItemNull;
}

extern "C" Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count) {
    js_call_count++;

    if (get_type_id(func_item) != LMD_TYPE_FUNC) {
        log_debug("js_call_function[%d]: not a function (type=%d, item=0x%llx, argc=%d, this_type=%d)",
            js_call_count, get_type_id(func_item), (unsigned long long)func_item.item, arg_count,
            get_type_id(this_val));
        return ItemNull;
    }

    JsFunction* fn = (JsFunction*)func_item.function;
    if (!fn || !fn->func_ptr) {
        log_error("js_call_function: null function pointer");
        return ItemNull;
    }

    // v11: handle bound functions — use bound this and prepend bound args
    if (fn->bound_args || fn->bound_this.item) {
        Item effective_this = fn->bound_this.item ? fn->bound_this : this_val;
        int total_argc = fn->bound_argc + arg_count;
        Item* merged_args = (Item*)alloca(total_argc * sizeof(Item));
        for (int i = 0; i < fn->bound_argc; i++) {
            merged_args[i] = fn->bound_args[i];
        }
        for (int i = 0; i < arg_count; i++) {
            merged_args[fn->bound_argc + i] = args ? args[i] : ItemNull;
        }
        Item prev_this = js_current_this;
        js_current_this = effective_this;
        Item result = js_invoke_fn(fn, merged_args, total_argc);
        js_current_this = prev_this;
        return result;
    }

    // Bind 'this' for the duration of this call
    Item prev_this = js_current_this;
    js_current_this = this_val;
    Item result = js_invoke_fn(fn, args, arg_count);
    js_current_this = prev_this;
    return result;
}

// Function.prototype.apply(thisArg, argsArray)
extern "C" Item js_apply_function(Item func_item, Item this_val, Item args_array) {
    if (get_type_id(func_item) != LMD_TYPE_FUNC) {
        log_error("js_apply_function: not a function (type=%d)", get_type_id(func_item));
        return ItemNull;
    }
    // Extract args from array
    int argc = 0;
    Item* args = NULL;
    if (get_type_id(args_array) == LMD_TYPE_ARRAY) {
        argc = (int)args_array.array->length;
        if (argc > 0) {
            args = (Item*)alloca(argc * sizeof(Item));
            for (int i = 0; i < argc; i++) {
                Item idx = {.item = i2it(i)};
                args[i] = js_array_get(args_array, idx);
            }
        }
    }
    return js_call_function(func_item, this_val, args, argc);
}

// v11: Function.prototype.bind(thisArg, ...args)
extern "C" Item js_bind_function(Item func_item, Item bound_this, Item* bound_args, int bound_argc) {
    if (get_type_id(func_item) != LMD_TYPE_FUNC) {
        log_error("js_bind_function: not a function (type=%d)", get_type_id(func_item));
        return ItemNull;
    }
    JsFunction* orig = (JsFunction*)func_item.function;
    JsFunction* bound = (JsFunction*)pool_calloc(js_input->pool, sizeof(JsFunction));
    bound->type_id = LMD_TYPE_FUNC;
    bound->func_ptr = orig->func_ptr;
    bound->param_count = orig->param_count;
    bound->env = orig->env;
    bound->env_size = orig->env_size;
    bound->prototype = ItemNull;
    bound->bound_this = bound_this;
    if (bound_argc > 0 && bound_args) {
        bound->bound_args = (Item*)pool_calloc(js_input->pool, bound_argc * sizeof(Item));
        for (int i = 0; i < bound_argc; i++) {
            bound->bound_args[i] = bound_args[i];
        }
        bound->bound_argc = bound_argc;
    }
    return (Item){.function = (Function*)bound};
}

// js_func_bind: safe version that checks type first.
// If func_item is a function → js_bind_function.
// If func_item is a map/object → call its own .bind() method with the args.
extern "C" Item js_func_bind(Item func_item, Item bound_this, Item* bound_args, int bound_argc) {
    if (get_type_id(func_item) == LMD_TYPE_FUNC) {
        return js_bind_function(func_item, bound_this, bound_args, bound_argc);
    }
    // Not a function: call the object's own .bind method
    // Reconstruct args as [bound_this, ...bound_args]
    int total = 1 + bound_argc;
    Item* args = (Item*)pool_calloc(js_input->pool, total * sizeof(Item));
    args[0] = bound_this;
    for (int i = 0; i < bound_argc; i++) args[i + 1] = bound_args ? bound_args[i] : ItemNull;
    Item method_name = (Item){.item = s2it(heap_create_name("bind", 4))};
    return js_map_method(func_item, method_name, args, total);
}

// =============================================================================
// v11: Regex support — /pattern/flags as Map objects with compiled RE2
// =============================================================================

// hidden property key for storing regex data pointer
static const char* JS_REGEX_DATA_KEY = "__rd";

struct JsRegexData {
    re2::RE2* re2;            // compiled regex
    bool global;              // 'g' flag
    bool ignore_case;         // 'i' flag
    bool multiline;           // 'm' flag
};

extern "C" Item js_create_regex(const char* pattern, int pattern_len, const char* flags, int flags_len) {
    // build RE2 options from flags
    re2::RE2::Options opts;
    opts.set_log_errors(false);
    bool global = false;
    for (int i = 0; i < flags_len; i++) {
        if (flags[i] == 'i') opts.set_case_sensitive(false);
        else if (flags[i] == 'm') opts.set_one_line(false);
        else if (flags[i] == 'g') global = true;
        else if (flags[i] == 's') opts.set_dot_nl(true);
    }
    // compile RE2 pattern
    re2::RE2* re2 = new re2::RE2(re2::StringPiece(pattern, pattern_len), opts);
    if (!re2->ok()) {
        log_error("js regex compile error: /%.*s/%.*s: %s",
            pattern_len, pattern, flags_len, flags, re2->error().c_str());
        delete re2;
        return ItemNull;
    }
    // store regex data in a pool-allocated struct
    JsRegexData* rd = (JsRegexData*)pool_calloc(js_input->pool, sizeof(JsRegexData));
    rd->re2 = re2;
    rd->global = global;
    rd->ignore_case = !opts.case_sensitive();
    rd->multiline = !opts.one_line();
    // create a Map object and set properties
    Item regex_obj = js_new_object();
    // store regex data pointer as int in hidden property
    Item rd_key = (Item){.item = s2it(heap_create_name(JS_REGEX_DATA_KEY))};
    Item rd_val = (Item){.item = i2it((int64_t)(uintptr_t)rd)};
    js_property_set(regex_obj, rd_key, rd_val);
    // create null-terminated copies for heap_create_name
    char* src_buf = (char*)pool_calloc(js_input->pool, pattern_len + 1);
    memcpy(src_buf, pattern, pattern_len);
    src_buf[pattern_len] = '\0';
    char* flg_buf = (char*)pool_calloc(js_input->pool, flags_len + 1);
    memcpy(flg_buf, flags, flags_len);
    flg_buf[flags_len] = '\0';
    // set visible properties
    Item source_key = (Item){.item = s2it(heap_create_name("source"))};
    Item source_val = (Item){.item = s2it(heap_create_name(src_buf))};
    js_property_set(regex_obj, source_key, source_val);
    Item flags_key = (Item){.item = s2it(heap_create_name("flags"))};
    Item flags_val = (Item){.item = s2it(heap_create_name(flg_buf))};
    js_property_set(regex_obj, flags_key, flags_val);
    Item global_key = (Item){.item = s2it(heap_create_name("global"))};
    Item global_val = (Item){.item = b2it(global ? BOOL_TRUE : BOOL_FALSE)};
    js_property_set(regex_obj, global_key, global_val);
    return regex_obj;
}

// new RegExp(pattern, flags) — construct regex from string arguments at runtime
extern "C" Item js_regexp_construct(Item pattern_item, Item flags_item) {
    const char* pattern = "";
    int pattern_len = 0;
    const char* flags = "";
    int flags_len = 0;
    if (get_type_id(pattern_item) == LMD_TYPE_STRING) {
        String* ps = it2s(pattern_item);
        if (ps) { pattern = ps->chars; pattern_len = (int)ps->len; }
    }
    if (get_type_id(flags_item) == LMD_TYPE_STRING) {
        String* fs = it2s(flags_item);
        if (fs) { flags = fs->chars; flags_len = (int)fs->len; }
    }
    return js_create_regex(pattern, pattern_len, flags, flags_len);
}

static JsRegexData* js_get_regex_data(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return NULL;
    Item rd_key = (Item){.item = s2it(heap_create_name(JS_REGEX_DATA_KEY))};
    Item rd_item = js_property_get(obj, rd_key);
    TypeId tid = get_type_id(rd_item);
    if (tid != LMD_TYPE_INT && tid != LMD_TYPE_INT64) return NULL;
    int64_t ptr_val = it2i(rd_item);
    if (ptr_val == 0) return NULL;
    return (JsRegexData*)(uintptr_t)ptr_val;
}

extern "C" Item js_regex_test(Item regex, Item str) {
    JsRegexData* rd = js_get_regex_data(regex);
    if (!rd) return (Item){.item = b2it(BOOL_FALSE)};
    TypeId tid = get_type_id(str);
    if (tid != LMD_TYPE_STRING) return (Item){.item = b2it(BOOL_FALSE)};
    const char* chars = str.get_chars();
    int len = str.get_len();
    bool matched = re2::RE2::PartialMatch(re2::StringPiece(chars, len), *rd->re2);
    return (Item){.item = b2it(matched ? BOOL_TRUE : BOOL_FALSE)};
}

extern "C" Item js_regex_exec(Item regex, Item str) {
    JsRegexData* rd = js_get_regex_data(regex);
    if (!rd) return ItemNull;
    TypeId tid = get_type_id(str);
    if (tid != LMD_TYPE_STRING) return ItemNull;
    const char* chars = str.get_chars();
    int len = str.get_len();
    // perform match with captures
    int num_groups = rd->re2->NumberOfCapturingGroups() + 1; // +1 for full match
    if (num_groups > 16) num_groups = 16;
    re2::StringPiece matches[16];
    bool matched = rd->re2->Match(re2::StringPiece(chars, len), 0, len,
        re2::RE2::UNANCHORED, matches, num_groups);
    if (!matched) return ItemNull;
    // build result array: [fullMatch, group1, group2, ..., matchIndex]
    // Note: matchIndex appended as last element since Lambda arrays don't support named properties
    Item result_arr = js_array_new(num_groups + 1);
    for (int i = 0; i < num_groups; i++) {
        if (matches[i].data()) {
            int mlen = (int)matches[i].size();
            char* mbuf = (char*)alloca(mlen + 1);
            memcpy(mbuf, matches[i].data(), mlen);
            mbuf[mlen] = '\0';
            Item s = (Item){.item = s2it(heap_create_name(mbuf))};
            Item idx = (Item){.item = i2it(i)};
            js_array_set(result_arr, idx, s);
        }
    }
    // store match index as last element
    int match_index = (int)(matches[0].data() - chars);
    Item idx_item = (Item){.item = i2it(num_groups)};
    Item idx_val = (Item){.item = i2it(match_index)};
    js_array_set(result_arr, idx_item, idx_val);
    return result_arr;
}

// =============================================================================
// v11: Map/Set collections — backed by HashMap from lib/hashmap.h
// =============================================================================

static const char* JS_COLLECTION_DATA_KEY = "__cd";

struct JsCollectionEntry {
    Item key;
    Item value;
};

// Insertion-order node for Map/Set (doubly-linked list)
struct JsCollectionOrderNode {
    Item key;
    Item value;
    JsCollectionOrderNode* next;
    JsCollectionOrderNode* prev;
};

// 0 = Map, 1 = Set
#define JS_COLLECTION_MAP 0
#define JS_COLLECTION_SET 1

struct JsCollectionData {
    HashMap* hmap;
    int type; // JS_COLLECTION_MAP or JS_COLLECTION_SET
    JsCollectionOrderNode* order_head; // insertion-order linked list (oldest first)
    JsCollectionOrderNode* order_tail; // newest entry
};

static uint64_t js_collection_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const JsCollectionEntry* e = (const JsCollectionEntry*)item;
    Item k = e->key;
    TypeId tid = get_type_id(k);
    if (tid == LMD_TYPE_STRING) {
        String* s = it2s(k);
        return hashmap_sip(s->chars, s->len, seed0, seed1);
    }
    if (tid == LMD_TYPE_INT || tid == LMD_TYPE_INT64) {
        int64_t v = it2i(k);
        return hashmap_sip(&v, sizeof(v), seed0, seed1);
    }
    // fallback: hash the raw item bits
    uint64_t bits = k.item;
    return hashmap_sip(&bits, sizeof(bits), seed0, seed1);
}

static int js_collection_compare(const void *a, const void *b, void *udata) {
    const JsCollectionEntry* ea = (const JsCollectionEntry*)a;
    const JsCollectionEntry* eb = (const JsCollectionEntry*)b;
    Item ka = ea->key, kb = eb->key;
    TypeId ta = get_type_id(ka), tb = get_type_id(kb);
    if (ta != tb) return 1;
    if (ta == LMD_TYPE_STRING) {
        String* sa = it2s(ka);
        String* sb = it2s(kb);
        if (sa->len != sb->len) return 1;
        return memcmp(sa->chars, sb->chars, sa->len);
    }
    if (ta == LMD_TYPE_INT || ta == LMD_TYPE_INT64) {
        int64_t va = it2i(ka), vb = it2i(kb);
        return (va == vb) ? 0 : 1;
    }
    if (ta == LMD_TYPE_FLOAT) {
        double da = ka.get_double(), db = kb.get_double();
        return (da == db) ? 0 : 1;
    }
    // reference equality for objects
    return (ka.item == kb.item) ? 0 : 1;
}

static JsCollectionData* js_get_collection_data(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return NULL;
    Item cd_key = (Item){.item = s2it(heap_create_name(JS_COLLECTION_DATA_KEY))};
    Item cd_item = js_property_get(obj, cd_key);
    TypeId tid = get_type_id(cd_item);
    // Property not found (undefined) or null → no collection data
    if (tid == LMD_TYPE_UNDEFINED || tid == LMD_TYPE_NULL) return NULL;
    if (tid != LMD_TYPE_INT && tid != LMD_TYPE_INT64) {
        log_error("js_get_collection_data: cd_item tid=%d (not INT/INT64), cd_item.item=0x%llx", (int)tid, (unsigned long long)cd_item.item);
        return NULL;
    }
    int64_t ptr_val = it2i(cd_item);
    if (ptr_val == 0) return NULL;
    // Guard against corrupt/small pointer values that would cause SIGSEGV
    if (ptr_val < 4096) {
        log_error("js_get_collection_data: suspicious pointer value %lld, rejecting", (long long)ptr_val);
        return NULL;
    }
    return (JsCollectionData*)(uintptr_t)ptr_val;
}

static Item js_collection_create(int type) {
    HashMap* hm = hashmap_new(sizeof(JsCollectionEntry), 16, 0, 0,
        js_collection_hash, js_collection_compare, NULL, NULL);
    JsCollectionData* cd = (JsCollectionData*)pool_calloc(js_input->pool, sizeof(JsCollectionData));
    cd->hmap = hm;
    cd->type = type;
    cd->order_head = NULL;
    cd->order_tail = NULL;
    Item obj = js_new_object();
    Item cd_key = (Item){.item = s2it(heap_create_name(JS_COLLECTION_DATA_KEY))};
    Item cd_val = (Item){.item = i2it((int64_t)(uintptr_t)cd)};
    js_property_set(obj, cd_key, cd_val);
    // set initial size property
    Item size_key = (Item){.item = s2it(heap_create_name("size"))};
    Item size_val = (Item){.item = i2it(0)};
    js_property_set(obj, size_key, size_val);
    return obj;
}

// Helper: add or update an entry in the insertion-order list
static void js_collection_order_upsert(JsCollectionData* cd, Item key, Item value) {
    // Check if key already exists in the order list
    JsCollectionOrderNode* node = cd->order_head;
    while (node) {
        JsCollectionEntry ea = {.key = node->key};
        JsCollectionEntry eb = {.key = key};
        if (js_collection_compare(&ea, &eb, NULL) == 0) {
            // Key exists — update value in-place (preserves insertion order)
            node->value = value;
            return;
        }
        node = node->next;
    }
    // New entry — append to tail
    JsCollectionOrderNode* n = (JsCollectionOrderNode*)pool_calloc(js_input->pool, sizeof(JsCollectionOrderNode));
    n->key = key;
    n->value = value;
    n->next = NULL;
    n->prev = cd->order_tail;
    if (cd->order_tail) cd->order_tail->next = n;
    else cd->order_head = n;
    cd->order_tail = n;
}

// Helper: remove an entry from the insertion-order list
static void js_collection_order_remove(JsCollectionData* cd, Item key) {
    JsCollectionOrderNode* node = cd->order_head;
    while (node) {
        JsCollectionEntry ea = {.key = node->key};
        JsCollectionEntry eb = {.key = key};
        if (js_collection_compare(&ea, &eb, NULL) == 0) {
            if (node->prev) node->prev->next = node->next;
            else cd->order_head = node->next;
            if (node->next) node->next->prev = node->prev;
            else cd->order_tail = node->prev;
            return;
        }
        node = node->next;
    }
}

static void js_collection_update_size(Item obj, JsCollectionData* cd) {
    Item size_key = (Item){.item = s2it(heap_create_name("size"))};
    Item size_val = (Item){.item = i2it((int64_t)hashmap_count(cd->hmap))};
    js_property_set(obj, size_key, size_val);
}

extern "C" Item js_map_collection_new(void) {
    return js_collection_create(JS_COLLECTION_MAP);
}

extern "C" Item js_set_collection_new(void) {
    return js_collection_create(JS_COLLECTION_SET);
}

extern "C" Item js_set_collection_new_from(Item iterable) {
    Item set = js_collection_create(JS_COLLECTION_SET);
    TypeId tid = get_type_id(iterable);
    if (tid == LMD_TYPE_ARRAY) {
        Array* a = iterable.array;
        for (int i = 0; i < a->length; i++) {
            js_collection_method(set, 0, a->items[i], ItemNull);
        }
    }
    return set;
}

extern "C" Item js_map_collection_new_from(Item iterable) {
    Item map = js_collection_create(JS_COLLECTION_MAP);
    TypeId tid = get_type_id(iterable);
    if (tid == LMD_TYPE_ARRAY) {
        Array* a = iterable.array;
        for (int i = 0; i < a->length; i++) {
            Item entry = a->items[i];
            if (get_type_id(entry) == LMD_TYPE_ARRAY && entry.array->length >= 2) {
                js_collection_method(map, 0, entry.array->items[0], entry.array->items[1]);
            }
        }
    }
    return map;
}

// Map/Set method dispatch
// method_id: 0=set/add, 1=get, 2=has, 3=delete, 4=clear,
//   5=forEach, 6=keys, 7=values, 8=entries, 9=size(getter)
extern "C" Item js_collection_method(Item obj, int method_id, Item arg1, Item arg2) {
    JsCollectionData* cd = js_get_collection_data(obj);
    if (!cd) return ItemNull;

    switch (method_id) {
        case 0: { // set(key, value) for Map, add(value) for Set
            JsCollectionEntry entry;
            if (cd->type == JS_COLLECTION_SET) {
                entry.key = arg1;
                entry.value = (Item){.item = b2it(BOOL_TRUE)};
            } else {
                entry.key = arg1;
                entry.value = arg2;
            }
            hashmap_set(cd->hmap, &entry);
            // Maintain insertion-order list
            js_collection_order_upsert(cd, entry.key, entry.value);
            js_collection_update_size(obj, cd);
            return obj; // return collection for chaining
        }
        case 1: { // get(key) — Map only
            JsCollectionEntry probe = {.key = arg1};
            const JsCollectionEntry* found = (const JsCollectionEntry*)hashmap_get(cd->hmap, &probe);
            if (found) return found->value;
            return make_js_undefined();
        }
        case 2: { // has(key)
            JsCollectionEntry probe = {.key = arg1};
            const JsCollectionEntry* found = (const JsCollectionEntry*)hashmap_get(cd->hmap, &probe);
            return (Item){.item = b2it(found ? BOOL_TRUE : BOOL_FALSE)};
        }
        case 3: { // delete(key)
            JsCollectionEntry probe = {.key = arg1};
            const JsCollectionEntry* found = (const JsCollectionEntry*)hashmap_delete(cd->hmap, &probe);
            js_collection_order_remove(cd, arg1);
            js_collection_update_size(obj, cd);
            return (Item){.item = b2it(found ? BOOL_TRUE : BOOL_FALSE)};
        }
        case 4: { // clear()
            hashmap_clear(cd->hmap, false);
            cd->order_head = NULL;
            cd->order_tail = NULL;
            js_collection_update_size(obj, cd);
            return make_js_undefined();
        }
        case 5: { // forEach(callback) — insertion order
            JsCollectionOrderNode* node = cd->order_head;
            while (node) {
                if (cd->type == JS_COLLECTION_SET) {
                    // callback(value, value, set)
                    js_call_function(arg1, make_js_undefined(), &node->key, 1);
                } else {
                    // callback(value, key, map)
                    Item args[2] = {node->value, node->key};
                    js_call_function(arg1, make_js_undefined(), args, 2);
                }
                node = node->next;
            }
            return make_js_undefined();
        }
        case 6: { // keys() — returns array in insertion order
            size_t count = hashmap_count(cd->hmap);
            Item arr = js_array_new((int)count);
            JsCollectionOrderNode* node = cd->order_head;
            int idx = 0;
            while (node) {
                js_array_set_int(arr, idx, node->key);
                idx++;
                node = node->next;
            }
            return arr;
        }
        case 7: { // values() — insertion order
            size_t count = hashmap_count(cd->hmap);
            Item arr = js_array_new((int)count);
            JsCollectionOrderNode* node = cd->order_head;
            int idx = 0;
            while (node) {
                if (cd->type == JS_COLLECTION_SET)
                    js_array_set_int(arr, idx, node->key);
                else
                    js_array_set_int(arr, idx, node->value);
                idx++;
                node = node->next;
            }
            return arr;
        }
        case 8: { // entries() — returns array of [key, value] pairs in insertion order
            size_t count = hashmap_count(cd->hmap);
            Item arr = js_array_new((int)count);
            JsCollectionOrderNode* node = cd->order_head;
            int idx = 0;
            while (node) {
                Item pair = js_array_new(2);
                js_array_set_int(pair, 0, node->key);
                js_array_set_int(pair, 1, node->value);
                js_array_set_int(arr, idx, pair);
                idx++;
                node = node->next;
            }
            return arr;
        }
        default: return ItemNull;
    }
}

// Forward declarations for v14 promise support
struct JsPromise;
static JsPromise* js_get_promise(Item promise_obj);
extern "C" Item js_promise_then(Item promise, Item on_fulfilled, Item on_rejected);
extern "C" Item js_promise_catch(Item promise, Item on_rejected);
extern "C" Item js_promise_finally(Item promise, Item on_finally);
extern "C" bool js_is_generator(Item obj);
extern "C" Item js_generator_next(Item generator, Item input);
extern "C" Item js_generator_return(Item generator, Item value);
extern "C" Item js_generator_throw(Item generator, Item error);

// Map method dispatcher: handles collection methods, falls back to property access
extern "C" Item js_map_method(Item obj, Item method_name, Item* args, int argc) {
    // DataView methods
    if (js_is_dataview(obj)) {
        return js_dataview_method(obj, method_name, args, argc);
    }
    // TypedArray methods
    if (js_is_typed_array(obj)) {
        String* method = it2s(method_name);
        if (method) {
            if (method->len == 4 && strncmp(method->chars, "fill", 4) == 0) {
                Item value = argc > 0 ? args[0] : ItemNull;
                return js_typed_array_fill(obj, value);
            }
            if (method->len == 3 && strncmp(method->chars, "set", 3) == 0) {
                Item source = argc > 0 ? args[0] : ItemNull;
                int offset = argc > 1 ? (int)it2i(args[1]) : 0;
                return js_typed_array_set_from(obj, source, offset);
            }
            if (method->len == 8 && strncmp(method->chars, "subarray", 8) == 0) {
                int start = argc > 0 ? (int)it2i(args[0]) : 0;
                JsTypedArray* ta = (JsTypedArray*)obj.map->data;
                int end = argc > 1 ? (int)it2i(args[1]) : ta->length;
                return js_typed_array_subarray(obj, start, end);
            }
            if (method->len == 5 && strncmp(method->chars, "slice", 5) == 0) {
                int start = argc > 0 ? (int)it2i(args[0]) : 0;
                JsTypedArray* ta = (JsTypedArray*)obj.map->data;
                int end = argc > 1 ? (int)it2i(args[1]) : ta->length;
                return js_typed_array_slice(obj, start, end);
            }
        }
    }
    // ArrayBuffer methods
    if (js_is_arraybuffer(obj)) {
        String* method = it2s(method_name);
        if (method) {
            if (method->len == 5 && strncmp(method->chars, "slice", 5) == 0) {
                int begin = argc > 0 ? (int)it2i(args[0]) : 0;
                int end = argc > 1 ? (int)it2i(args[1]) : js_arraybuffer_byte_length(obj);
                return js_arraybuffer_slice(obj, begin, end);
            }
        }
    }
    // Check if this is a Map/Set collection
    JsCollectionData* cd = js_get_collection_data(obj);
    if (cd) {
        String* method = it2s(method_name);
        if (method) {
            int method_id = -1;
            if (cd->type == JS_COLLECTION_MAP) {
                if (method->len == 3 && strncmp(method->chars, "set", 3) == 0) method_id = 0;
                else if (method->len == 3 && strncmp(method->chars, "get", 3) == 0) method_id = 1;
                else if (method->len == 3 && strncmp(method->chars, "has", 3) == 0) method_id = 2;
                else if (method->len == 6 && strncmp(method->chars, "delete", 6) == 0) method_id = 3;
                else if (method->len == 5 && strncmp(method->chars, "clear", 5) == 0) method_id = 4;
                else if (method->len == 7 && strncmp(method->chars, "forEach", 7) == 0) method_id = 5;
                else if (method->len == 4 && strncmp(method->chars, "keys", 4) == 0) method_id = 6;
                else if (method->len == 6 && strncmp(method->chars, "values", 6) == 0) method_id = 7;
                else if (method->len == 7 && strncmp(method->chars, "entries", 7) == 0) method_id = 8;
            } else {
                if (method->len == 3 && strncmp(method->chars, "add", 3) == 0) method_id = 0;
                else if (method->len == 3 && strncmp(method->chars, "has", 3) == 0) method_id = 2;
                else if (method->len == 6 && strncmp(method->chars, "delete", 6) == 0) method_id = 3;
                else if (method->len == 5 && strncmp(method->chars, "clear", 5) == 0) method_id = 4;
                else if (method->len == 7 && strncmp(method->chars, "forEach", 7) == 0) method_id = 5;
                else if (method->len == 6 && strncmp(method->chars, "values", 6) == 0) method_id = 7;
                else if (method->len == 7 && strncmp(method->chars, "entries", 7) == 0) method_id = 8;
            }
            if (method_id >= 0) {
                Item arg1 = argc > 0 ? args[0] : ItemNull;
                Item arg2 = argc > 1 ? args[1] : ItemNull;
                return js_collection_method(obj, method_id, arg1, arg2);
            }
        }
    }
    // v14: Promise instance methods (.then, .catch, .finally)
    JsPromise* promise = js_get_promise(obj);
    if (promise) {
        String* method = it2s(method_name);
        if (method) {
            if (method->len == 4 && strncmp(method->chars, "then", 4) == 0) {
                Item on_fulfilled = argc > 0 ? args[0] : ItemNull;
                Item on_rejected = argc > 1 ? args[1] : ItemNull;
                return js_promise_then(obj, on_fulfilled, on_rejected);
            }
            if (method->len == 5 && strncmp(method->chars, "catch", 5) == 0) {
                Item on_rejected = argc > 0 ? args[0] : ItemNull;
                return js_promise_catch(obj, on_rejected);
            }
            if (method->len == 7 && strncmp(method->chars, "finally", 7) == 0) {
                Item on_finally = argc > 0 ? args[0] : ItemNull;
                return js_promise_finally(obj, on_finally);
            }
        }
    }

    // v15: Generator instance methods (.next, .return, .throw)
    if (js_is_generator(obj)) {
        String* method = it2s(method_name);
        if (method) {
            if (method->len == 4 && strncmp(method->chars, "next", 4) == 0) {
                Item input = argc > 0 ? args[0] : make_js_undefined();
                return js_generator_next(obj, input);
            }
            if (method->len == 6 && strncmp(method->chars, "return", 6) == 0) {
                Item value = argc > 0 ? args[0] : make_js_undefined();
                return js_generator_return(obj, value);
            }
            if (method->len == 5 && strncmp(method->chars, "throw", 5) == 0) {
                Item error = argc > 0 ? args[0] : make_js_undefined();
                return js_generator_throw(obj, error);
            }
        }
    }

    // v15: Regex instance methods (.exec, .test)
    {
        JsRegexData* rd = js_get_regex_data(obj);
        if (rd) {
            String* method = it2s(method_name);
            if (method) {
                if (method->len == 4 && strncmp(method->chars, "exec", 4) == 0) {
                    Item str = argc > 0 ? args[0] : ItemNull;
                    return js_regex_exec(obj, str);
                }
                if (method->len == 4 && strncmp(method->chars, "test", 4) == 0) {
                    Item str = argc > 0 ? args[0] : ItemNull;
                    return js_regex_test(obj, str);
                }
            }
        }
    }

    // TextEncoder/TextDecoder instance methods (.encode, .decode)
    {
        String* method = it2s(method_name);
        if (method) {
            if (method->len == 6 && strncmp(method->chars, "encode", 6) == 0) {
                Item str_arg = argc > 0 ? args[0] : ItemNull;
                return js_text_encoder_encode(obj, str_arg);
            }
            if (method->len == 6 && strncmp(method->chars, "decode", 6) == 0) {
                Item input_arg = argc > 0 ? args[0] : ItemNull;
                return js_text_decoder_decode(obj, input_arg);
            }
        }
    }

    // Fallback: property access + call
    Item fn = js_property_access(obj, method_name);
    return js_call_function(fn, obj, args, argc);
}

// =============================================================================
// Helper: convert a JS number arg (typically float from push_d) to an int Item
// Lambda fn_substring requires LMD_TYPE_INT, but JS literals arrive as floats.
// =============================================================================
static Item js_arg_to_int(Item arg) {
    TypeId tid = get_type_id(arg);
    if (tid == LMD_TYPE_INT || tid == LMD_TYPE_INT64) return arg;
    if (tid == LMD_TYPE_FLOAT) {
        double d = arg.get_double();
        return (Item){.item = i2it((int64_t)d)};
    }
    // fallback: try js_get_number
    double d = js_get_number(arg);
    return (Item){.item = i2it((int64_t)d)};
}

// =============================================================================
// String replace/replaceAll implementation with regex + callback support
// =============================================================================

static Item js_string_replace_impl(Item str, Item* args, int argc, bool is_replace_all) {
    String* s = it2s(str);
    if (!s || s->len == 0) return str;
    bool replacement_is_func = (get_type_id(args[1]) == LMD_TYPE_FUNC);
    JsRegexData* rd = js_get_regex_data(args[0]);

    if (rd) {
        // regex-based replace
        re2::StringPiece input(s->chars, s->len);
        int ngroups = rd->re2->NumberOfCapturingGroups() + 1;
        if (ngroups > 16) ngroups = 16;
        re2::StringPiece matches[16];
        StrBuf* buf = strbuf_new();
        int pos = 0;
        bool found_match = false;
        while (pos <= (int)s->len) {
            bool matched = rd->re2->Match(input, pos, (int)s->len,
                re2::RE2::UNANCHORED, matches, ngroups);
            if (!matched) break;
            found_match = true;
            int match_start = (int)(matches[0].data() - s->chars);
            int match_len = (int)matches[0].size();
            // append text before match
            if (match_start > pos)
                strbuf_append_str_n(buf, s->chars + pos, match_start - pos);
            if (replacement_is_func) {
                // call function(match, g1, g2, ..., offset, originalString)
                int fn_argc = ngroups + 2;
                Item* fn_args = (Item*)alloca(fn_argc * sizeof(Item));
                for (int i = 0; i < ngroups; i++) {
                    if (matches[i].data()) {
                        fn_args[i] = (Item){.item = s2it(heap_strcpy(
                            (char*)matches[i].data(), (int)matches[i].size()))};
                    } else {
                        fn_args[i] = ItemNull;
                    }
                }
                fn_args[ngroups] = (Item){.item = i2it(match_start)};
                fn_args[ngroups + 1] = str;
                Item result = js_call_function(args[1], ItemNull, fn_args, fn_argc);
                Item result_str = js_to_string(result);
                String* rs = it2s(result_str);
                if (rs) strbuf_append_str_n(buf, rs->chars, rs->len);
            } else {
                // string replacement
                Item repl_str = js_to_string(args[1]);
                String* rs = it2s(repl_str);
                if (rs) strbuf_append_str_n(buf, rs->chars, rs->len);
            }
            pos = match_start + match_len;
            if (match_len == 0) pos++; // avoid infinite loop on zero-length match
            if (!is_replace_all && !(rd->global)) break;
        }
        if (found_match) {
            // append remaining text
            if (pos < (int)s->len)
                strbuf_append_str_n(buf, s->chars + pos, (int)s->len - pos);
            String* result = heap_strcpy(buf->str, buf->length);
            strbuf_free(buf);
            return (Item){.item = s2it(result)};
        }
        strbuf_free(buf);
        return str;
    }

    // string-based replace
    String* search = it2s(js_to_string(args[0]));
    if (!search || search->len == 0) {
        if (!replacement_is_func) return fn_replace(str, args[0], args[1]);
        return str;
    }
    if (!replacement_is_func) {
        if (is_replace_all) {
            // simple string replaceAll via loop
            Item result = str;
            int max_iter = 10000;
            while (max_iter-- > 0) {
                Item replaced = fn_replace(result, args[0], args[1]);
                String* r = it2s(replaced);
                String* prev_s = it2s(result);
                if (r && prev_s && r->len == prev_s->len && strncmp(r->chars, prev_s->chars, r->len) == 0)
                    break;
                result = replaced;
            }
            return result;
        }
        return fn_replace(str, args[0], args[1]);
    }
    // string search with function callback
    StrBuf* buf = strbuf_new();
    int pos = 0;
    bool found_any = false;
    while (pos <= (int)s->len - (int)search->len) {
        const char* found = (const char*)memmem(s->chars + pos, s->len - pos, search->chars, search->len);
        if (!found) break;
        found_any = true;
        int match_start = (int)(found - s->chars);
        if (match_start > pos)
            strbuf_append_str_n(buf, s->chars + pos, match_start - pos);
        // call function(match, offset, originalString)
        Item fn_args[3];
        fn_args[0] = args[0]; // the matched string == search string
        fn_args[1] = (Item){.item = i2it(match_start)};
        fn_args[2] = str;
        Item result = js_call_function(args[1], ItemNull, fn_args, 3);
        Item result_str = js_to_string(result);
        String* rs = it2s(result_str);
        if (rs) strbuf_append_str_n(buf, rs->chars, rs->len);
        pos = match_start + (int)search->len;
        if (!is_replace_all) break;
    }
    if (found_any) {
        if (pos < (int)s->len)
            strbuf_append_str_n(buf, s->chars + pos, (int)s->len - pos);
        String* result = heap_strcpy(buf->str, buf->length);
        strbuf_free(buf);
        return (Item){.item = s2it(result)};
    }
    strbuf_free(buf);
    return str;
}

// =============================================================================
// String Method Dispatcher
// =============================================================================

extern "C" Item js_string_method(Item str, Item method_name, Item* args, int argc) {
    if (get_type_id(str) != LMD_TYPE_STRING || get_type_id(method_name) != LMD_TYPE_STRING) {
        return ItemNull;
    }
    String* method = it2s(method_name);
    if (!method) return ItemNull;

    // match method name and delegate to Lambda fn_* functions
    if (method->len == 7 && strncmp(method->chars, "indexOf", 7) == 0) {
        if (argc < 1) return (Item){.item = i2it(-1)};
        if (argc < 2) return (Item){.item = i2it(fn_index_of(str, args[0]))};
        // indexOf with start position
        int start_pos = (int)js_get_number(args[1]);
        String* s = it2s(str);
        String* sub = it2s(args[0]);
        if (!s || !sub) return (Item){.item = i2it(-1)};
        if (start_pos < 0) start_pos = 0;
        size_t byte_start;
        if (s->is_ascii) {
            byte_start = (size_t)start_pos;
        } else {
            byte_start = str_utf8_char_to_byte(s->chars, s->len, (size_t)start_pos);
        }
        if (byte_start >= s->len) return (Item){.item = i2it(-1)};
        if (sub->len == 0) return (Item){.item = i2it(start_pos)};
        if (s->len - byte_start < sub->len) return (Item){.item = i2it(-1)};
        for (size_t i = byte_start; i <= s->len - sub->len; i++) {
            if (memcmp(s->chars + i, sub->chars, sub->len) == 0) {
                int64_t char_index = s->is_ascii ? (int64_t)i : (int64_t)str_utf8_count(s->chars, i);
                return (Item){.item = i2it(char_index)};
            }
        }
        return (Item){.item = i2it(-1)};
    }
    if (method->len == 11 && strncmp(method->chars, "lastIndexOf", 11) == 0) {
        if (argc < 1) return (Item){.item = i2it(-1)};
        if (argc < 2) return (Item){.item = i2it(fn_last_index_of(str, args[0]))};
        // lastIndexOf with start position - search backwards from position
        int end_pos = (int)js_get_number(args[1]);
        String* s = it2s(str);
        String* sub = it2s(args[0]);
        if (!s || !sub) return (Item){.item = i2it(-1)};
        if (end_pos < 0) return (Item){.item = i2it(-1)};
        size_t byte_end;
        if (s->is_ascii) {
            byte_end = (size_t)end_pos;
        } else {
            byte_end = str_utf8_char_to_byte(s->chars, s->len, (size_t)end_pos);
        }
        if (byte_end >= s->len) byte_end = s->len - 1;
        if (sub->len == 0) {
            int64_t char_pos = s->is_ascii ? (int64_t)byte_end : (int64_t)str_utf8_count(s->chars, byte_end);
            return (Item){.item = i2it(char_pos < end_pos ? char_pos : end_pos)};
        }
        size_t max_start = byte_end + sub->len <= s->len ? byte_end : s->len - sub->len;
        for (size_t i = max_start + 1; i > 0; i--) {
            size_t pos = i - 1;
            if (memcmp(s->chars + pos, sub->chars, sub->len) == 0) {
                int64_t char_index = s->is_ascii ? (int64_t)pos : (int64_t)str_utf8_count(s->chars, pos);
                return (Item){.item = i2it(char_index)};
            }
        }
        return (Item){.item = i2it(-1)};
    }
    if (method->len == 8 && strncmp(method->chars, "includes", 8) == 0) {
        if (argc < 1) return (Item){.item = b2it(false)};
        return (Item){.item = b2it(fn_contains(str, args[0]))};
    }
    if (method->len == 10 && strncmp(method->chars, "startsWith", 10) == 0) {
        if (argc < 1) return (Item){.item = b2it(false)};
        return (Item){.item = b2it(fn_starts_with(str, args[0]))};
    }
    if (method->len == 8 && strncmp(method->chars, "endsWith", 8) == 0) {
        if (argc < 1) return (Item){.item = b2it(false)};
        return (Item){.item = b2it(fn_ends_with(str, args[0]))};
    }
    if (method->len == 4 && strncmp(method->chars, "trim", 4) == 0) {
        return fn_trim(str);
    }
    if (method->len == 9 && strncmp(method->chars, "trimStart", 9) == 0) {
        return fn_trim_start(str);
    }
    if (method->len == 7 && strncmp(method->chars, "trimEnd", 7) == 0) {
        return fn_trim_end(str);
    }
    if (method->len == 11 && strncmp(method->chars, "toLowerCase", 11) == 0) {
        return fn_lower(str);
    }
    if (method->len == 11 && strncmp(method->chars, "toUpperCase", 11) == 0) {
        return fn_upper(str);
    }
    if (method->len == 5 && strncmp(method->chars, "split", 5) == 0) {
        if (argc > 0) {
            Item sep = args[0];
            // check if separator is a regex (Map with __rd property)
            JsRegexData* rd = js_get_regex_data(sep);
            if (rd) {
                // regex split
                String* s = it2s(str);
                if (!s || s->len == 0) {
                    Item result = js_array_new(1);
                    js_array_push(result, (Item){.item = s2it(heap_create_name(""))});
                    return result;
                }
                Item result = js_array_new(0);
                re2::StringPiece input(s->chars, s->len);
                int num_groups = rd->re2->NumberOfCapturingGroups();
                int total_groups = num_groups + 1;
                if (total_groups > 16) total_groups = 16;
                re2::StringPiece match[16];
                int pos = 0;
                while (pos <= (int)s->len) {
                    bool found = rd->re2->Match(input, pos, (int)s->len,
                        re2::RE2::UNANCHORED, match, total_groups);
                    if (!found || match[0].size() == 0) break;
                    int match_start = (int)(match[0].data() - s->chars);
                    int match_end = match_start + (int)match[0].size();
                    // add substring before match
                    String* part = heap_strcpy(s->chars + pos, match_start - pos);
                    js_array_push(result, (Item){.item = s2it(part)});
                    // add capturing groups
                    for (int g = 1; g < total_groups; g++) {
                        if (match[g].data()) {
                            String* gstr = heap_strcpy((char*)match[g].data(), (int)match[g].size());
                            js_array_push(result, (Item){.item = s2it(gstr)});
                        } else {
                            js_array_push(result, make_js_undefined());
                        }
                    }
                    pos = match_end;
                }
                // add remainder
                String* tail = heap_strcpy(s->chars + pos, (int)s->len - pos);
                js_array_push(result, (Item){.item = s2it(tail)});
                return result;
            }
        }
        Item sep = argc > 0 ? args[0] : (Item){.item = s2it(heap_create_name(""))};
        return fn_split(str, sep);
    }
    if (method->len == 9 && strncmp(method->chars, "substring", 9) == 0) {
        if (argc < 1) return str;
        Item start = js_arg_to_int(args[0]);
        Item end_item;
        if (argc > 1) {
            end_item = js_arg_to_int(args[1]);
        } else {
            // JS substring(start) means from start to end of string
            int64_t len = fn_len(str);
            end_item = (Item){.item = i2it(len)};
        }
        return fn_substring(str, start, end_item);
    }
    if (method->len == 5 && strncmp(method->chars, "slice", 5) == 0) {
        if (argc < 1) return str;
        Item start = js_arg_to_int(args[0]);
        Item end_item;
        if (argc > 1) {
            end_item = js_arg_to_int(args[1]);
        } else {
            int64_t len = fn_len(str);
            end_item = (Item){.item = i2it(len)};
        }
        return fn_substring(str, start, end_item);
    }
    if (method->len == 7 && strncmp(method->chars, "replace", 7) == 0) {
        if (argc < 2) return str;
        return js_string_replace_impl(str, args, argc, false);
    }
    if (method->len == 6 && strncmp(method->chars, "charAt", 6) == 0) {
        if (argc < 1) return (Item){.item = s2it(heap_create_name(""))};
        return fn_index(str, args[0]);
    }
    if (method->len == 10 && strncmp(method->chars, "charCodeAt", 10) == 0) {
        if (argc < 1) return ItemNull;
        String* s = it2s(str);
        if (!s || s->len == 0) return ItemNull;
        int target_idx = (int)js_get_number(args[0]);
        if (target_idx < 0) return ItemNull;
        // walk UTF-8 bytes, counting UTF-16 code units
        int utf16_idx = 0;
        int byte_pos = 0;
        while (byte_pos < (int)s->len) {
            unsigned char b = (unsigned char)s->chars[byte_pos];
            uint32_t cp = 0;
            int bytes = 1;
            if (b < 0x80) { cp = b; bytes = 1; }
            else if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; bytes = 2; }
            else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; bytes = 3; }
            else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; bytes = 4; }
            for (int i = 1; i < bytes && byte_pos + i < (int)s->len; i++)
                cp = (cp << 6) | ((unsigned char)s->chars[byte_pos + i] & 0x3F);
            if (cp >= 0x10000) {
                // surrogate pair: 2 UTF-16 code units
                if (utf16_idx == target_idx) {
                    uint32_t hi = 0xD800 + ((cp - 0x10000) >> 10);
                    return (Item){.item = i2it((int64_t)hi)};
                }
                if (utf16_idx + 1 == target_idx) {
                    uint32_t lo = 0xDC00 + ((cp - 0x10000) & 0x3FF);
                    return (Item){.item = i2it((int64_t)lo)};
                }
                utf16_idx += 2;
            } else {
                if (utf16_idx == target_idx)
                    return (Item){.item = i2it((int64_t)cp)};
                utf16_idx++;
            }
            byte_pos += bytes;
        }
        return ItemNull;
    }
    if (method->len == 11 && strncmp(method->chars, "codePointAt", 11) == 0) {
        if (argc < 1) return ItemNull;
        String* s = it2s(str);
        if (!s || s->len == 0) return ItemNull;
        int target_idx = (int)js_get_number(args[0]);
        if (target_idx < 0) return ItemNull;
        // walk UTF-8, counting UTF-16 code units to find the right position
        int utf16_idx = 0;
        int byte_pos = 0;
        while (byte_pos < (int)s->len) {
            unsigned char b = (unsigned char)s->chars[byte_pos];
            uint32_t cp = 0;
            int bytes = 1;
            if (b < 0x80) { cp = b; bytes = 1; }
            else if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; bytes = 2; }
            else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; bytes = 3; }
            else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; bytes = 4; }
            for (int i = 1; i < bytes && byte_pos + i < (int)s->len; i++)
                cp = (cp << 6) | ((unsigned char)s->chars[byte_pos + i] & 0x3F);
            if (utf16_idx == target_idx)
                return (Item){.item = i2it((int64_t)cp)};
            utf16_idx += (cp >= 0x10000) ? 2 : 1;
            byte_pos += bytes;
        }
        return ItemNull;
    }
    if (method->len == 6 && strncmp(method->chars, "concat", 6) == 0) {
        Item result = str;
        for (int i = 0; i < argc; i++) {
            Item arg_str = js_to_string(args[i]);
            result = fn_join(result, arg_str);
        }
        return result;
    }
    if (method->len == 6 && strncmp(method->chars, "repeat", 6) == 0) {
        if (argc < 1) return (Item){.item = s2it(heap_create_name(""))};
        String* s = it2s(str);
        int count = (int)js_get_number(args[0]);
        if (count <= 0 || !s) return (Item){.item = s2it(heap_create_name(""))};
        // build repeated string
        StrBuf* buf = strbuf_new();
        for (int i = 0; i < count; i++) {
            strbuf_append_str_n(buf, s->chars, s->len);
        }
        String* result = heap_strcpy(buf->str, buf->length);
        strbuf_free(buf);
        return (Item){.item = s2it(result)};
    }
    // replaceAll — redirect to unified replace handler
    if (method->len == 10 && strncmp(method->chars, "replaceAll", 10) == 0) {
        if (argc < 2) return str;
        return js_string_replace_impl(str, args, argc, true);
    }
    // padStart(targetLength, padString?)
    if (method->len == 8 && strncmp(method->chars, "padStart", 8) == 0) {
        if (argc < 1) return str;
        String* s = it2s(str);
        if (!s) return str;
        int target = (int)js_get_number(args[0]);
        if ((int)s->len >= target) return str;
        String* pad = (argc > 1) ? it2s(js_to_string(args[1])) : NULL;
        const char* pad_chars = pad ? pad->chars : " ";
        int pad_len = pad ? (int)pad->len : 1;
        if (pad_len == 0) return str;
        int needed = target - (int)s->len;
        StrBuf* buf = strbuf_new();
        for (int i = 0; i < needed; i++) {
            strbuf_append_char(buf, pad_chars[i % pad_len]);
        }
        strbuf_append_str_n(buf, s->chars, s->len);
        String* result = heap_strcpy(buf->str, buf->length);
        strbuf_free(buf);
        return (Item){.item = s2it(result)};
    }
    // padEnd(targetLength, padString?)
    if (method->len == 6 && strncmp(method->chars, "padEnd", 6) == 0) {
        if (argc < 1) return str;
        String* s = it2s(str);
        if (!s) return str;
        int target = (int)js_get_number(args[0]);
        if ((int)s->len >= target) return str;
        String* pad = (argc > 1) ? it2s(js_to_string(args[1])) : NULL;
        const char* pad_chars = pad ? pad->chars : " ";
        int pad_len = pad ? (int)pad->len : 1;
        if (pad_len == 0) return str;
        int needed = target - (int)s->len;
        StrBuf* buf = strbuf_new();
        strbuf_append_str_n(buf, s->chars, s->len);
        for (int i = 0; i < needed; i++) {
            strbuf_append_char(buf, pad_chars[i % pad_len]);
        }
        String* result = heap_strcpy(buf->str, buf->length);
        strbuf_free(buf);
        return (Item){.item = s2it(result)};
    }
    // at(index) — supports negative indexing
    if (method->len == 2 && strncmp(method->chars, "at", 2) == 0) {
        if (argc < 1) return ItemNull;
        String* s = it2s(str);
        if (!s || s->len == 0) return ItemNull;
        int idx = (int)js_get_number(args[0]);
        if (idx < 0) idx = (int)s->len + idx;
        if (idx < 0 || idx >= (int)s->len) return ItemNull;
        String* ch = heap_strcpy(&s->chars[idx], 1);
        return (Item){.item = s2it(ch)};
    }
    // search(pattern) — return index of first match
    if (method->len == 6 && strncmp(method->chars, "search", 6) == 0) {
        if (argc < 1) return (Item){.item = i2it(-1)};
        // delegate to indexOf for string patterns
        return (Item){.item = i2it(fn_index_of(str, args[0]))};
    }
    // toString
    if (method->len == 8 && strncmp(method->chars, "toString", 8) == 0) {
        return str;
    }

    // match(regex) — delegates to regex exec
    if (method->len == 5 && strncmp(method->chars, "match", 5) == 0) {
        if (argc < 1) return ItemNull;
        // If arg is a regex object, use js_regex_exec
        if (get_type_id(args[0]) == LMD_TYPE_MAP) {
            JsRegexData* rd = js_get_regex_data(args[0]);
            if (rd) {
                if (!rd->global) {
                    return js_regex_exec(args[0], str);
                }
                // Global: collect all matches into array
                String* s = it2s(str);
                if (!s) return ItemNull;
                Item result = js_array_new(0);
                int offset = 0;
                int num_groups = rd->re2->NumberOfCapturingGroups() + 1;
                if (num_groups > 16) num_groups = 16;
                re2::StringPiece matches[16];
                while (offset < (int)s->len) {
                    bool matched = rd->re2->Match(
                        re2::StringPiece(s->chars, s->len), offset, (int)s->len,
                        re2::RE2::UNANCHORED, matches, num_groups);
                    if (!matched) break;
                    int mlen = (int)matches[0].size();
                    Item ms = (Item){.item = s2it(heap_strcpy((char*)matches[0].data(), mlen))};
                    array_push(result.array, ms);
                    int advance = (int)(matches[0].data() - s->chars) + mlen;
                    if (advance <= offset) advance = offset + 1;
                    offset = advance;
                }
                return result;
            }
        }
        // If arg is a string, treat as literal pattern
        return (Item){.item = i2it(fn_index_of(str, args[0]))};
    }
    // matchAll(regex) — returns array of all match results
    if (method->len == 8 && strncmp(method->chars, "matchAll", 8) == 0) {
        if (argc < 1) return js_array_new(0);
        if (get_type_id(args[0]) == LMD_TYPE_MAP) {
            JsRegexData* rd = js_get_regex_data(args[0]);
            if (rd) {
                String* s = it2s(str);
                if (!s) return js_array_new(0);
                Item result = js_array_new(0);
                int offset = 0;
                int num_groups = rd->re2->NumberOfCapturingGroups() + 1;
                if (num_groups > 16) num_groups = 16;
                re2::StringPiece matches[16];
                while (offset < (int)s->len) {
                    bool matched = rd->re2->Match(
                        re2::StringPiece(s->chars, s->len), offset, (int)s->len,
                        re2::RE2::UNANCHORED, matches, num_groups);
                    if (!matched) break;
                    // Build match object like regex exec
                    Item match_arr = js_array_new(num_groups + 1);
                    for (int i = 0; i < num_groups; i++) {
                        if (matches[i].data()) {
                            Item ms = (Item){.item = s2it(heap_strcpy((char*)matches[i].data(), (int)matches[i].size()))};
                            Item idx = (Item){.item = i2it(i)};
                            js_array_set(match_arr, idx, ms);
                        }
                    }
                    int match_index = (int)(matches[0].data() - s->chars);
                    Item idx_item = (Item){.item = i2it(num_groups)};
                    Item idx_val = (Item){.item = i2it(match_index)};
                    js_array_set(match_arr, idx_item, idx_val);
                    array_push(result.array, match_arr);
                    int advance = match_index + (int)matches[0].size();
                    if (advance <= offset) advance = offset + 1;
                    offset = advance;
                }
                return result;
            }
        }
        return js_array_new(0);
    }

    log_debug("js_string_method: unknown method '%.*s'", (int)method->len, method->chars);
    return ItemNull;
}

// =============================================================================
// Array Method Dispatcher
// =============================================================================

extern "C" Item js_array_method(Item arr, Item method_name, Item* args, int argc) {
    if (get_type_id(method_name) != LMD_TYPE_STRING) return ItemNull;
    String* method = it2s(method_name);
    if (!method) return ItemNull;
    TypeId arr_type = get_type_id(arr);

    // push - mutating
    if (method->len == 4 && strncmp(method->chars, "push", 4) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return (Item){.item = i2it(0)};
        for (int i = 0; i < argc; i++) {
            array_push(arr.array, args[i]);
        }
        return (Item){.item = i2it(arr.array->length)};
    }
    // pop - mutating
    if (method->len == 3 && strncmp(method->chars, "pop", 3) == 0) {
        if (arr_type != LMD_TYPE_ARRAY || arr.array->length == 0) return ItemNull;
        Item last = arr.array->items[arr.array->length - 1];
        arr.array->length--;
        return last;
    }
    // length property (handled as method for convenience)
    if (method->len == 6 && strncmp(method->chars, "length", 6) == 0) {
        return (Item){.item = i2it(fn_len(arr))};
    }
    // indexOf
    if (method->len == 7 && strncmp(method->chars, "indexOf", 7) == 0) {
        if (argc < 1 || arr_type != LMD_TYPE_ARRAY) return (Item){.item = i2it(-1)};
        Array* a = arr.array;
        int start = (argc >= 2) ? (int)js_get_number(args[1]) : 0;
        if (start < 0) start = 0;
        for (int i = start; i < a->length; i++) {
            if (it2b(js_strict_equal(a->items[i], args[0]))) return (Item){.item = i2it(i)};
        }
        return (Item){.item = i2it(-1)};
    }
    // item(index) - NodeList/HTMLCollection style index access
    if (method->len == 4 && strncmp(method->chars, "item", 4) == 0) {
        if (argc < 1 || arr_type != LMD_TYPE_ARRAY) return ItemNull;
        int idx = (int)js_get_number(args[0]);
        Array* a = arr.array;
        if (idx >= 0 && idx < a->length) return a->items[idx];
        return ItemNull;
    }
    // includes
    if (method->len == 8 && strncmp(method->chars, "includes", 8) == 0) {
        if (argc < 1 || arr_type != LMD_TYPE_ARRAY) return (Item){.item = b2it(false)};
        Array* a = arr.array;
        for (int i = 0; i < a->length; i++) {
            if (it2b(js_strict_equal(a->items[i], args[0]))) return (Item){.item = b2it(true)};
        }
        return (Item){.item = b2it(false)};
    }
    // join - converts all elements to strings and joins them
    if (method->len == 4 && strncmp(method->chars, "join", 4) == 0) {
        Item sep = argc > 0 ? args[0] : (Item){.item = s2it(heap_create_name(","))};
        String* sep_str = it2s(sep);
        const char* sep_chars = sep_str ? sep_str->chars : ",";
        size_t sep_len = sep_str ? sep_str->len : 1;

        Array* a = arr.array;
        StrBuf* buf = strbuf_new();
        for (int i = 0; i < a->length; i++) {
            if (i > 0 && sep_len > 0) {
                strbuf_append_str_n(buf, sep_chars, sep_len);
            }
            Item elem_str = js_to_string(a->items[i]);
            String* s = it2s(elem_str);
            if (s && s->len > 0) {
                strbuf_append_str_n(buf, s->chars, s->len);
            }
        }
        String* result = heap_strcpy(buf->str, buf->length);
        strbuf_free(buf);
        return (Item){.item = s2it(result)};
    }
    // reverse - in-place reversal (JS spec: mutates and returns same array)
    if (method->len == 7 && strncmp(method->chars, "reverse", 7) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return arr;
        Array* a = arr.array;
        for (int i = 0, j = a->length - 1; i < j; i++, j--) {
            Item tmp = a->items[i];
            a->items[i] = a->items[j];
            a->items[j] = tmp;
        }
        return arr;
    }
    // slice - returns new Array with elements from start to end
    if (method->len == 5 && strncmp(method->chars, "slice", 5) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return arr;
        Array* src = arr.array;
        int start = argc > 0 ? (int)js_get_number(args[0]) : 0;
        int end = argc > 1 ? (int)js_get_number(args[1]) : src->length;
        if (start < 0) start = src->length + start;
        if (end < 0) end = src->length + end;
        if (start < 0) start = 0;
        if (end > src->length) end = src->length;
        if (start >= end) return js_array_new(0);
        int count = end - start;
        Item result = js_array_new(count);
        Array* dst = result.array;
        for (int i = 0; i < count; i++) {
            dst->items[i] = src->items[start + i];
        }
        dst->length = count;
        return result;
    }
    // concat - returns new array that is the concatenation
    if (method->len == 6 && strncmp(method->chars, "concat", 6) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return arr;
        Array* src = arr.array;
        // calculate total length
        int total = src->length;
        for (int i = 0; i < argc; i++) {
            if (get_type_id(args[i]) == LMD_TYPE_ARRAY) {
                total += args[i].array->length;
            } else {
                total++;
            }
        }
        Item result = js_array_new(total);
        Array* dst = result.array;
        int pos = 0;
        for (int i = 0; i < src->length; i++) {
            dst->items[pos++] = src->items[i];
        }
        for (int i = 0; i < argc; i++) {
            if (get_type_id(args[i]) == LMD_TYPE_ARRAY) {
                Array* other = args[i].array;
                for (int j = 0; j < other->length; j++) {
                    dst->items[pos++] = other->items[j];
                }
            } else {
                dst->items[pos++] = args[i];
            }
        }
        dst->length = pos;
        return result;
    }
    // map - uses callback as first arg (must be a JsFunction)
    if (method->len == 3 && strncmp(method->chars, "map", 3) == 0) {
        if (argc < 1 || arr_type != LMD_TYPE_ARRAY) return arr;
        Item callback = args[0];
        if (get_type_id(callback) != LMD_TYPE_FUNC) return arr;
        Array* src = arr.array;
        Item result = js_array_new(0);
        Array* dst = result.array;
        JsFunction* fn = (JsFunction*)callback.function;
        for (int i = 0; i < src->length; i++) {
            Item cb_args[2] = { src->items[i], (Item){.item = i2it(i)} };
            Item mapped = js_invoke_fn(fn, cb_args, fn->param_count >= 2 ? 2 : 1);
            list_push(dst, mapped);
        }
        return result;
    }
    // filter
    if (method->len == 6 && strncmp(method->chars, "filter", 6) == 0) {
        if (argc < 1 || arr_type != LMD_TYPE_ARRAY) return arr;
        Item callback = args[0];
        if (get_type_id(callback) != LMD_TYPE_FUNC) return arr;
        Array* src = arr.array;
        Item result = js_array_new(0);
        Array* dst = result.array;
        JsFunction* fn = (JsFunction*)callback.function;
        for (int i = 0; i < src->length; i++) {
            Item cb_args[2] = { src->items[i], (Item){.item = i2it(i)} };
            Item pred = js_invoke_fn(fn, cb_args, fn->param_count >= 2 ? 2 : 1);
            if (js_is_truthy(pred)) {
                list_push(dst, src->items[i]);
            }
        }
        return result;
    }
    // reduce
    if (method->len == 6 && strncmp(method->chars, "reduce", 6) == 0) {
        if (argc < 1 || arr_type != LMD_TYPE_ARRAY) return ItemNull;
        Item callback = args[0];
        if (get_type_id(callback) != LMD_TYPE_FUNC) return ItemNull;
        Array* src = arr.array;
        JsFunction* fn = (JsFunction*)callback.function;
        Item accumulator;
        int start_idx;
        if (argc >= 2) {
            accumulator = args[1];
            start_idx = 0;
        } else {
            if (src->length == 0) return ItemNull;
            accumulator = src->items[0];
            start_idx = 1;
        }
        for (int i = start_idx; i < src->length; i++) {
            Item cb_args[3] = { accumulator, src->items[i], (Item){.item = i2it(i)} };
            accumulator = js_invoke_fn(fn, cb_args, fn->param_count >= 3 ? 3 : 2);
        }
        return accumulator;
    }
    // forEach
    if (method->len == 7 && strncmp(method->chars, "forEach", 7) == 0) {
        if (argc < 1 || arr_type != LMD_TYPE_ARRAY) return ItemNull;
        Item callback = args[0];
        if (get_type_id(callback) != LMD_TYPE_FUNC) return ItemNull;
        Array* src = arr.array;
        JsFunction* fn = (JsFunction*)callback.function;
        for (int i = 0; i < src->length; i++) {
            Item cb_args[2] = { src->items[i], (Item){.item = i2it(i)} };
            js_invoke_fn(fn, cb_args, fn->param_count >= 2 ? 2 : 1);
        }
        return ItemNull;
    }
    // find
    if (method->len == 4 && strncmp(method->chars, "find", 4) == 0) {
        if (argc < 1 || arr_type != LMD_TYPE_ARRAY) return ItemNull;
        Item callback = args[0];
        if (get_type_id(callback) != LMD_TYPE_FUNC) return ItemNull;
        Array* src = arr.array;
        JsFunction* fn = (JsFunction*)callback.function;
        for (int i = 0; i < src->length; i++) {
            Item cb_args[2] = { src->items[i], (Item){.item = i2it(i)} };
            Item pred = js_invoke_fn(fn, cb_args, fn->param_count >= 2 ? 2 : 1);
            if (js_is_truthy(pred)) return src->items[i];
        }
        return ItemNull;
    }
    // findIndex
    if (method->len == 9 && strncmp(method->chars, "findIndex", 9) == 0) {
        if (argc < 1 || arr_type != LMD_TYPE_ARRAY) return (Item){.item = i2it(-1)};
        Item callback = args[0];
        if (get_type_id(callback) != LMD_TYPE_FUNC) return (Item){.item = i2it(-1)};
        Array* src = arr.array;
        JsFunction* fn = (JsFunction*)callback.function;
        for (int i = 0; i < src->length; i++) {
            Item cb_args[2] = { src->items[i], (Item){.item = i2it(i)} };
            Item pred = js_invoke_fn(fn, cb_args, fn->param_count >= 2 ? 2 : 1);
            if (js_is_truthy(pred)) return (Item){.item = i2it(i)};
        }
        return (Item){.item = i2it(-1)};
    }
    // some
    if (method->len == 4 && strncmp(method->chars, "some", 4) == 0) {
        if (argc < 1 || arr_type != LMD_TYPE_ARRAY) return (Item){.item = b2it(false)};
        Item callback = args[0];
        if (get_type_id(callback) != LMD_TYPE_FUNC) return (Item){.item = b2it(false)};
        Array* src = arr.array;
        JsFunction* fn = (JsFunction*)callback.function;
        for (int i = 0; i < src->length; i++) {
            Item cb_args[2] = { src->items[i], (Item){.item = i2it(i)} };
            Item pred = js_invoke_fn(fn, cb_args, fn->param_count >= 2 ? 2 : 1);
            if (js_is_truthy(pred)) return (Item){.item = b2it(true)};
        }
        return (Item){.item = b2it(false)};
    }
    // every
    if (method->len == 5 && strncmp(method->chars, "every", 5) == 0) {
        if (argc < 1 || arr_type != LMD_TYPE_ARRAY) return (Item){.item = b2it(true)};
        Item callback = args[0];
        if (get_type_id(callback) != LMD_TYPE_FUNC) return (Item){.item = b2it(true)};
        Array* src = arr.array;
        JsFunction* fn = (JsFunction*)callback.function;
        for (int i = 0; i < src->length; i++) {
            Item cb_args[2] = { src->items[i], (Item){.item = i2it(i)} };
            Item pred = js_invoke_fn(fn, cb_args, fn->param_count >= 2 ? 2 : 1);
            if (!js_is_truthy(pred)) return (Item){.item = b2it(false)};
        }
        return (Item){.item = b2it(true)};
    }
    // sort
    if (method->len == 4 && strncmp(method->chars, "sort", 4) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return arr;
        Array* src = arr.array;
        if (argc >= 1 && get_type_id(args[0]) == LMD_TYPE_FUNC) {
            // sort with comparator callback
            JsFunction* cmp_fn = (JsFunction*)args[0].function;
            // insertion sort using comparator
            for (int i = 1; i < src->length; i++) {
                Item key_item = src->items[i];
                int j = i - 1;
                while (j >= 0) {
                    Item cmp_args[2] = { src->items[j], key_item };
                    Item cmp_result = js_invoke_fn(cmp_fn, cmp_args, 2);
                    double cval = js_get_number(cmp_result);
                    if (cval <= 0) break;
                    src->items[j + 1] = src->items[j];
                    j--;
                }
                src->items[j + 1] = key_item;
            }
        } else {
            // default sort: lexicographic string comparison (JS spec)
            for (int i = 1; i < src->length; i++) {
                Item key_item = src->items[i];
                Item key_str = js_to_string(key_item);
                String* ks = it2s(key_str);
                int j = i - 1;
                while (j >= 0) {
                    Item j_str = js_to_string(src->items[j]);
                    String* js_s = it2s(j_str);
                    // compare strings lexicographically
                    int cmp = 0;
                    if (js_s && ks) {
                        int min_len = js_s->len < ks->len ? js_s->len : ks->len;
                        cmp = strncmp(js_s->chars, ks->chars, min_len);
                        if (cmp == 0) cmp = (int)js_s->len - (int)ks->len;
                    }
                    if (cmp <= 0) break;
                    src->items[j + 1] = src->items[j];
                    j--;
                }
                src->items[j + 1] = key_item;
            }
        }
        return arr;
    }
    // flat
    if (method->len == 4 && strncmp(method->chars, "flat", 4) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return arr;
        Item result = js_array_new(0);
        Array* src = arr.array;
        Array* dst = result.array;
        for (int i = 0; i < src->length; i++) {
            if (get_type_id(src->items[i]) == LMD_TYPE_ARRAY) {
                Array* inner = src->items[i].array;
                for (int j = 0; j < inner->length; j++) {
                    list_push(dst, inner->items[j]);
                }
            } else {
                list_push(dst, src->items[i]);
            }
        }
        return result;
    }
    // fill
    if (method->len == 4 && strncmp(method->chars, "fill", 4) == 0) {
        if (argc < 1) return arr;
        return js_array_fill(arr, args[0]);
    }
    // splice(start, deleteCount, ...items) — mutating
    if (method->len == 6 && strncmp(method->chars, "splice", 6) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return js_array_new(0);
        Array* a = arr.array;
        int start = argc > 0 ? (int)js_get_number(args[0]) : 0;
        if (start < 0) start = a->length + start;
        if (start < 0) start = 0;
        if (start > a->length) start = a->length;
        int delete_count = argc > 1 ? (int)js_get_number(args[1]) : (a->length - start);
        if (delete_count < 0) delete_count = 0;
        if (start + delete_count > a->length) delete_count = a->length - start;
        int insert_count = argc > 2 ? argc - 2 : 0;

        // save deleted elements
        Item deleted = js_array_new(0);
        Array* del_arr = deleted.array;
        for (int i = 0; i < delete_count; i++) {
            array_push(del_arr, a->items[start + i]);
        }

        // shift elements
        int shift = insert_count - delete_count;
        int old_len = a->length;
        int new_len = old_len + shift;
        if (shift > 0) {
            // grow: ensure capacity, then memmove
            if (new_len + 4 > a->capacity) {
                int new_cap = new_len + 4;
                Item* new_items = (Item*)malloc(new_cap * sizeof(Item));
                if (a->items && a->length > 0) {
                    memcpy(new_items, a->items, a->length * sizeof(Item));
                }
                a->items = new_items;
                a->capacity = new_cap;
            }
            // move elements after delete region to their new positions
            int elements_to_move = old_len - start - delete_count;
            if (elements_to_move > 0) {
                memmove(&a->items[start + insert_count], &a->items[start + delete_count],
                        elements_to_move * sizeof(Item));
            }
            a->length = new_len;
        } else if (shift < 0) {
            // shrink: memmove left, then adjust length
            int elements_to_move = old_len - start - delete_count;
            if (elements_to_move > 0) {
                memmove(&a->items[start + insert_count], &a->items[start + delete_count],
                        elements_to_move * sizeof(Item));
            }
            a->length = new_len;
        }

        // insert new items
        for (int i = 0; i < insert_count; i++) {
            a->items[start + i] = args[2 + i];
        }
        return deleted;
    }
    // shift() — remove and return first element
    if (method->len == 5 && strncmp(method->chars, "shift", 5) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return ItemNull;
        Array* a = arr.array;
        if (a->length == 0) return ItemNull;
        Item first = a->items[0];
        memmove(&a->items[0], &a->items[1], (a->length - 1) * sizeof(Item));
        a->length--;
        return first;
    }
    // unshift(...items) — prepend items, return new length
    if (method->len == 7 && strncmp(method->chars, "unshift", 7) == 0) {
        if (arr_type != LMD_TYPE_ARRAY || argc < 1) return (Item){.item = i2it(arr_type == LMD_TYPE_ARRAY ? arr.array->length : 0)};
        Array* a = arr.array;
        int old_len = a->length;
        int new_len = old_len + argc;
        // ensure capacity
        if (new_len + 4 > a->capacity) {
            int new_cap = new_len + 4;
            Item* new_items = (Item*)malloc(new_cap * sizeof(Item));
            if (a->items && a->length > 0) {
                memcpy(new_items, a->items, a->length * sizeof(Item));
            }
            a->items = new_items;
            a->capacity = new_cap;
        }
        // shift existing elements right
        memmove(&a->items[argc], &a->items[0], old_len * sizeof(Item));
        // insert new items at front
        for (int i = 0; i < argc; i++) {
            a->items[i] = args[i];
        }
        a->length = new_len;
        return (Item){.item = i2it(a->length)};
    }
    // lastIndexOf
    if (method->len == 11 && strncmp(method->chars, "lastIndexOf", 11) == 0) {
        if (argc < 1 || arr_type != LMD_TYPE_ARRAY) return (Item){.item = i2it(-1)};
        Array* a = arr.array;
        int from = argc > 1 ? (int)js_get_number(args[1]) : a->length - 1;
        if (from < 0) from = a->length + from;
        if (from >= a->length) from = a->length - 1;
        for (int i = from; i >= 0; i--) {
            if (it2b(js_strict_equal(a->items[i], args[0]))) return (Item){.item = i2it(i)};
        }
        return (Item){.item = i2it(-1)};
    }
    // flatMap
    if (method->len == 7 && strncmp(method->chars, "flatMap", 7) == 0) {
        if (argc < 1 || arr_type != LMD_TYPE_ARRAY) return arr;
        Item callback = args[0];
        if (get_type_id(callback) != LMD_TYPE_FUNC) return arr;
        Array* src = arr.array;
        Item result = js_array_new(0);
        Array* dst = result.array;
        JsFunction* fn = (JsFunction*)callback.function;
        for (int i = 0; i < src->length; i++) {
            Item cb_args[2] = { src->items[i], (Item){.item = i2it(i)} };
            Item mapped = js_invoke_fn(fn, cb_args, fn->param_count >= 2 ? 2 : 1);
            // flatten one level
            if (get_type_id(mapped) == LMD_TYPE_ARRAY) {
                Array* inner = mapped.array;
                for (int j = 0; j < inner->length; j++) {
                    list_push(dst, inner->items[j]);
                }
            } else {
                list_push(dst, mapped);
            }
        }
        return result;
    }
    // reduceRight
    if (method->len == 11 && strncmp(method->chars, "reduceRight", 11) == 0) {
        if (argc < 1 || arr_type != LMD_TYPE_ARRAY) return ItemNull;
        Item callback = args[0];
        if (get_type_id(callback) != LMD_TYPE_FUNC) return ItemNull;
        Array* src = arr.array;
        JsFunction* fn = (JsFunction*)callback.function;
        Item accumulator;
        int start_idx;
        if (argc >= 2) {
            accumulator = args[1];
            start_idx = src->length - 1;
        } else {
            if (src->length == 0) return ItemNull;
            accumulator = src->items[src->length - 1];
            start_idx = src->length - 2;
        }
        for (int i = start_idx; i >= 0; i--) {
            Item cb_args[3] = { accumulator, src->items[i], (Item){.item = i2it(i)} };
            accumulator = js_invoke_fn(fn, cb_args, fn->param_count >= 3 ? 3 : 2);
        }
        return accumulator;
    }
    // at(index) — supports negative indexing
    if (method->len == 2 && strncmp(method->chars, "at", 2) == 0) {
        if (argc < 1 || arr_type != LMD_TYPE_ARRAY) return ItemNull;
        Array* a = arr.array;
        int idx = (int)js_get_number(args[0]);
        if (idx < 0) idx = a->length + idx;
        if (idx < 0 || idx >= a->length) return ItemNull;
        return a->items[idx];
    }
    // toString — join elements with comma
    if (method->len == 8 && strncmp(method->chars, "toString", 8) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return js_to_string(arr);
        Array* a = arr.array;
        StrBuf* buf = strbuf_new();
        for (int i = 0; i < a->length; i++) {
            if (i > 0) strbuf_append_str_n(buf, ",", 1);
            Item elem_str = js_to_string(a->items[i]);
            String* s = it2s(elem_str);
            if (s && s->len > 0) strbuf_append_str_n(buf, s->chars, s->len);
        }
        String* result = heap_strcpy(buf->str, buf->length);
        strbuf_free(buf);
        return (Item){.item = s2it(result)};
    }

    // keys() — returns array of indices [0, 1, 2, ...]
    if (method->len == 4 && strncmp(method->chars, "keys", 4) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return js_array_new(0);
        Array* a = arr.array;
        Item result = js_array_new(a->length);
        Array* r = result.array;
        for (int i = 0; i < a->length; i++) {
            r->items[i] = (Item){.item = i2it(i)};
        }
        r->length = a->length;
        return result;
    }
    // values() — returns shallow copy of array
    if (method->len == 6 && strncmp(method->chars, "values", 6) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return js_array_new(0);
        Array* a = arr.array;
        Item result = js_array_new(a->length);
        Array* r = result.array;
        memcpy(r->items, a->items, a->length * sizeof(Item));
        r->length = a->length;
        return result;
    }
    // entries() — returns array of [index, value] pairs
    if (method->len == 7 && strncmp(method->chars, "entries", 7) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return js_array_new(0);
        Array* a = arr.array;
        Item result = js_array_new(a->length);
        for (int i = 0; i < a->length; i++) {
            Item pair = js_array_new(2);
            Array* p = pair.array;
            p->items[0] = (Item){.item = i2it(i)};
            p->items[1] = a->items[i];
            p->length = 2;
            array_push(result.array, pair);
        }
        return result;
    }

    log_debug("js_array_method: unknown method '%.*s'", (int)method->len, method->chars);
    return ItemNull;
}

// Generic method call with expanded args array (used for obj.method(...spread) calls)
extern "C" Item js_method_call_apply(Item obj, Item method_name, Item args_array) {
    int argc = 0;
    Item* args = NULL;
    if (get_type_id(args_array) == LMD_TYPE_ARRAY && args_array.array->length > 0) {
        argc = (int)args_array.array->length;
        args = (Item*)alloca(argc * sizeof(Item));
        for (int i = 0; i < argc; i++) {
            Item idx = {.item = i2it(i)};
            args[i] = js_array_get(args_array, idx);
        }
    }
    TypeId obj_type = get_type_id(obj);
    if (obj_type == LMD_TYPE_STRING) {
        return js_string_method(obj, method_name, args, argc);
    }
    if (obj_type == LMD_TYPE_ARRAY) {
        return js_array_method(obj, method_name, args, argc);
    }
    if (obj_type == LMD_TYPE_INT || obj_type == LMD_TYPE_FLOAT) {
        return js_number_method(obj, method_name, args, argc);
    }
    return js_map_method(obj, method_name, args, argc);
}

// =============================================================================
// Math Object Methods
// =============================================================================

extern "C" Item js_math_method(Item method_name, Item* args, int argc) {
    if (get_type_id(method_name) != LMD_TYPE_STRING) return ItemNull;
    String* method = it2s(method_name);
    if (!method) return ItemNull;

    // Math.abs
    if (method->len == 3 && strncmp(method->chars, "abs", 3) == 0) {
        if (argc < 1) return ItemNull;
        return fn_abs(js_to_number(args[0]));
    }
    // Math.floor
    if (method->len == 5 && strncmp(method->chars, "floor", 5) == 0) {
        if (argc < 1) return ItemNull;
        return fn_floor(js_to_number(args[0]));
    }
    // Math.ceil
    if (method->len == 4 && strncmp(method->chars, "ceil", 4) == 0) {
        if (argc < 1) return ItemNull;
        return fn_ceil(js_to_number(args[0]));
    }
    // Math.round
    if (method->len == 5 && strncmp(method->chars, "round", 5) == 0) {
        if (argc < 1) return ItemNull;
        return fn_round(js_to_number(args[0]));
    }
    // Math.sqrt
    if (method->len == 4 && strncmp(method->chars, "sqrt", 4) == 0) {
        if (argc < 1) return ItemNull;
        return fn_math_sqrt(js_to_number(args[0]));
    }
    // Math.pow
    if (method->len == 3 && strncmp(method->chars, "pow", 3) == 0) {
        if (argc < 2) return ItemNull;
        return fn_pow(js_to_number(args[0]), js_to_number(args[1]));
    }
    // Math.min
    if (method->len == 3 && strncmp(method->chars, "min", 3) == 0) {
        if (argc < 1) return js_make_number(INFINITY);
        Item result = js_to_number(args[0]);
        for (int i = 1; i < argc; i++) {
            result = fn_min2(result, js_to_number(args[i]));
        }
        return result;
    }
    // Math.max
    if (method->len == 3 && strncmp(method->chars, "max", 3) == 0) {
        if (argc < 1) return js_make_number(-INFINITY);
        Item result = js_to_number(args[0]);
        for (int i = 1; i < argc; i++) {
            result = fn_max2(result, js_to_number(args[i]));
        }
        return result;
    }
    // Math.log
    if (method->len == 3 && strncmp(method->chars, "log", 3) == 0) {
        if (argc < 1) return ItemNull;
        return fn_math_log(js_to_number(args[0]));
    }
    // Math.log10
    if (method->len == 5 && strncmp(method->chars, "log10", 5) == 0) {
        if (argc < 1) return ItemNull;
        return fn_math_log10(js_to_number(args[0]));
    }
    // Math.exp
    if (method->len == 3 && strncmp(method->chars, "exp", 3) == 0) {
        if (argc < 1) return ItemNull;
        return fn_math_exp(js_to_number(args[0]));
    }
    // Math.sin
    if (method->len == 3 && strncmp(method->chars, "sin", 3) == 0) {
        if (argc < 1) return ItemNull;
        return fn_math_sin(js_to_number(args[0]));
    }
    // Math.cos
    if (method->len == 3 && strncmp(method->chars, "cos", 3) == 0) {
        if (argc < 1) return ItemNull;
        return fn_math_cos(js_to_number(args[0]));
    }
    // Math.tan
    if (method->len == 3 && strncmp(method->chars, "tan", 3) == 0) {
        if (argc < 1) return ItemNull;
        return fn_math_tan(js_to_number(args[0]));
    }
    // Math.sign
    if (method->len == 4 && strncmp(method->chars, "sign", 4) == 0) {
        if (argc < 1) return ItemNull;
        return fn_sign(js_to_number(args[0]));
    }
    // Math.trunc
    if (method->len == 5 && strncmp(method->chars, "trunc", 5) == 0) {
        if (argc < 1) return ItemNull;
        return fn_int(js_to_number(args[0]));
    }
    // Math.random
    if (method->len == 6 && strncmp(method->chars, "random", 6) == 0) {
        double r = (double)rand() / (double)RAND_MAX;
        return js_make_number(r);
    }
    // Math.asin
    if (method->len == 4 && strncmp(method->chars, "asin", 4) == 0) {
        if (argc < 1) return ItemNull;
        double d = js_get_number(js_to_number(args[0]));
        return js_make_number(asin(d));
    }
    // Math.acos
    if (method->len == 4 && strncmp(method->chars, "acos", 4) == 0) {
        if (argc < 1) return ItemNull;
        double d = js_get_number(js_to_number(args[0]));
        return js_make_number(acos(d));
    }
    // Math.atan
    if (method->len == 4 && strncmp(method->chars, "atan", 4) == 0) {
        if (argc < 1) return ItemNull;
        double d = js_get_number(js_to_number(args[0]));
        return js_make_number(atan(d));
    }
    // Math.atan2
    if (method->len == 5 && strncmp(method->chars, "atan2", 5) == 0) {
        if (argc < 2) return ItemNull;
        double y = js_get_number(js_to_number(args[0]));
        double x = js_get_number(js_to_number(args[1]));
        return js_make_number(atan2(y, x));
    }
    // Math.log2
    if (method->len == 4 && strncmp(method->chars, "log2", 4) == 0) {
        if (argc < 1) return ItemNull;
        double d = js_get_number(js_to_number(args[0]));
        return js_make_number(log2(d));
    }
    // Math.cbrt
    if (method->len == 4 && strncmp(method->chars, "cbrt", 4) == 0) {
        if (argc < 1) return ItemNull;
        double d = js_get_number(js_to_number(args[0]));
        return js_make_number(cbrt(d));
    }
    // Math.hypot
    if (method->len == 5 && strncmp(method->chars, "hypot", 5) == 0) {
        if (argc < 1) return js_make_number(0.0);
        double sum = 0.0;
        for (int i = 0; i < argc; i++) {
            double d = js_get_number(js_to_number(args[i]));
            sum += d * d;
        }
        return js_make_number(sqrt(sum));
    }
    // Math.clz32
    if (method->len == 5 && strncmp(method->chars, "clz32", 5) == 0) {
        if (argc < 1) return js_make_number(32.0);
        uint32_t n = (uint32_t)js_to_int32(js_get_number(args[0]));
        if (n == 0) return js_make_number(32.0);
        return js_make_number((double)__builtin_clz(n));
    }
    // Math.imul
    if (method->len == 4 && strncmp(method->chars, "imul", 4) == 0) {
        if (argc < 2) return js_make_number(0.0);
        int32_t a = js_to_int32(js_get_number(args[0]));
        int32_t b = js_to_int32(js_get_number(args[1]));
        return js_make_number((double)((int32_t)((uint32_t)a * (uint32_t)b)));
    }
    // Math.fround
    if (method->len == 6 && strncmp(method->chars, "fround", 6) == 0) {
        if (argc < 1) return ItemNull;
        float f = (float)js_get_number(js_to_number(args[0]));
        return js_make_number((double)f);
    }
    // Math.sinh
    if (method->len == 4 && strncmp(method->chars, "sinh", 4) == 0) {
        if (argc < 1) return ItemNull;
        return js_make_number(sinh(js_get_number(js_to_number(args[0]))));
    }
    // Math.cosh
    if (method->len == 4 && strncmp(method->chars, "cosh", 4) == 0) {
        if (argc < 1) return ItemNull;
        return js_make_number(cosh(js_get_number(js_to_number(args[0]))));
    }
    // Math.tanh
    if (method->len == 4 && strncmp(method->chars, "tanh", 4) == 0) {
        if (argc < 1) return ItemNull;
        return js_make_number(tanh(js_get_number(js_to_number(args[0]))));
    }
    // Math.asinh
    if (method->len == 5 && strncmp(method->chars, "asinh", 5) == 0) {
        if (argc < 1) return ItemNull;
        return js_make_number(asinh(js_get_number(js_to_number(args[0]))));
    }
    // Math.acosh
    if (method->len == 5 && strncmp(method->chars, "acosh", 5) == 0) {
        if (argc < 1) return ItemNull;
        return js_make_number(acosh(js_get_number(js_to_number(args[0]))));
    }
    // Math.atanh
    if (method->len == 5 && strncmp(method->chars, "atanh", 5) == 0) {
        if (argc < 1) return ItemNull;
        return js_make_number(atanh(js_get_number(js_to_number(args[0]))));
    }
    // Math.expm1
    if (method->len == 5 && strncmp(method->chars, "expm1", 5) == 0) {
        if (argc < 1) return ItemNull;
        return js_make_number(expm1(js_get_number(js_to_number(args[0]))));
    }
    // Math.log1p
    if (method->len == 5 && strncmp(method->chars, "log1p", 5) == 0) {
        if (argc < 1) return ItemNull;
        return js_make_number(log1p(js_get_number(js_to_number(args[0]))));
    }

    log_debug("js_math_method: unknown method '%.*s'", (int)method->len, method->chars);
    return ItemNull;
}

// Math method dispatch where args are in a JS array (for spread calls: Math.max(...arr))
extern "C" Item js_math_apply(Item method_name, Item args_array) {
    if (get_type_id(args_array) != LMD_TYPE_ARRAY) {
        // Fall back to 0-arg call
        return js_math_method(method_name, NULL, 0);
    }
    int argc = (int)args_array.array->length;
    if (argc == 0) {
        return js_math_method(method_name, NULL, 0);
    }
    Item* args = (Item*)alloca(argc * sizeof(Item));
    for (int i = 0; i < argc; i++) {
        Item idx = {.item = i2it(i)};
        args[i] = js_array_get(args_array, idx);
    }
    return js_math_method(method_name, args, argc);
}

// Math constants as properties
extern "C" Item js_math_property(Item prop_name) {
    if (get_type_id(prop_name) != LMD_TYPE_STRING) return ItemNull;
    String* prop = it2s(prop_name);
    if (!prop) return ItemNull;

    if (prop->len == 2 && strncmp(prop->chars, "PI", 2) == 0) {
        return js_make_number(M_PI);
    }
    if (prop->len == 1 && prop->chars[0] == 'E') {
        return js_make_number(M_E);
    }
    if (prop->len == 4 && strncmp(prop->chars, "LN2", 3) == 0) {
        return js_make_number(M_LN2);
    }
    if (prop->len == 4 && strncmp(prop->chars, "LN10", 4) == 0) {
        return js_make_number(M_LN10);
    }
    if (prop->len == 5 && strncmp(prop->chars, "LOG2E", 5) == 0) {
        return js_make_number(M_LOG2E);
    }
    if (prop->len == 6 && strncmp(prop->chars, "LOG10E", 6) == 0) {
        return js_make_number(M_LOG10E);
    }
    if (prop->len == 5 && strncmp(prop->chars, "SQRT2", 5) == 0) {
        return js_make_number(M_SQRT2);
    }
    if (prop->len == 7 && strncmp(prop->chars, "SQRT1_2", 7) == 0) {
        return js_make_number(M_SQRT1_2);
    }

    return ItemNull;
}

// array slice from index to end — used for rest destructuring
extern "C" Item js_array_slice_from(Item arr, Item start_item) {
    TypeId tid = get_type_id(arr);
    if (tid != LMD_TYPE_ARRAY) return js_array_new(0);
    Array* src = arr.array;
    int start = (int)js_get_number(start_item);
    if (start < 0) start = src->length + start;
    if (start < 0) start = 0;
    if (start >= src->length) return js_array_new(0);
    Item result = js_array_new(0);
    Array* dst = result.array;
    for (int i = start; i < src->length; i++) {
        array_push(dst, src->items[i]);
    }
    return result;
}

// =============================================================================
// Prototype chain support
// =============================================================================

static const char PROTO_KEY[] = "__proto__";
static const int PROTO_KEY_LEN = 9;

// Set the prototype of an object (stores as __proto__ property on Map)
extern "C" void js_set_prototype(Item object, Item prototype) {
    if (get_type_id(object) != LMD_TYPE_MAP) return;
    if (get_type_id(prototype) != LMD_TYPE_MAP && prototype.item != ItemNull.item) return;
    // P10d: use interned __proto__ key
    Item key = js_get_proto_key();
    js_property_set(object, key, prototype);
}

// Get the prototype of an object (read __proto__ property)
// P10d: uses interned key + first-match lookup (no heap allocation per call)
extern "C" Item js_get_prototype(Item object) {
    if (get_type_id(object) != LMD_TYPE_MAP) return ItemNull;
    Map* m = object.map;
    // P10f+P10d: direct first-match lookup with interned key
    return js_map_get_fast(m, PROTO_KEY, PROTO_KEY_LEN);
}

// Walk the prototype chain to find a property
// P10f: uses first-match lookup on each prototype level
extern "C" Item js_prototype_lookup(Item object, Item property) {
    // first check own properties (skip — caller already checked)
    // walk up the chain via __proto__
    Item proto = js_get_prototype(object);
    int depth = 0;
    // P10f: extract key string once for first-match lookup
    const char* key_str = NULL;
    int key_len = 0;
    if (get_type_id(property) == LMD_TYPE_STRING) {
        String* s = it2s(property);
        key_str = s->chars;
        key_len = (int)s->len;
    }
    while (proto.item != ItemNull.item && get_type_id(proto) == LMD_TYPE_MAP && depth < 32) {
        Item result;
        if (key_str) {
            result = js_map_get_fast(proto.map, key_str, key_len);
        } else {
            result = map_get(proto.map, property);
        }
        if (result.item != ItemNull.item) return result;
        proto = js_get_prototype(proto);
        depth++;
    }
    return ItemNull;
}

// =============================================================================
// v12: Object rest destructuring
// =============================================================================

extern "C" Item js_object_rest(Item src, Item* exclude_keys, int exclude_count) {
    if (get_type_id(src) != LMD_TYPE_MAP) return js_new_object();
    Map* m = it2map(src);
    TypeMap* tm = (TypeMap*)m->type;
    if (!tm) return js_new_object();
    ShapeEntry* e = tm->shape;
    Item rest = js_new_object();
    while (e) {
        if (e->name) {
            bool excluded = false;
            for (int i = 0; i < exclude_count; i++) {
                String* ek = it2s(exclude_keys[i]);
                if (ek && e->name->length == ek->len && memcmp(e->name->str, ek->chars, ek->len) == 0) {
                    excluded = true;
                    break;
                }
            }
            if (!excluded) {
                Item val = _map_read_field(e, m->data);
                String* key_str = heap_create_name(e->name->str, e->name->length);
                js_property_set(rest, (Item){.item = s2it(key_str)}, val);
            }
        }
        e = e->next;
    }
    return rest;
}

// =============================================================================
// v15: Generator Runtime
// =============================================================================

// Generator state: the MIR-compiled state machine function takes
// (Item* env, Item input, int64_t state) and returns a 2-element array
// [value, next_state] where next_state == -1 means done.
struct JsGenerator {
    TypeId type_id;           // LMD_TYPE_MAP (treated as object)
    void*  state_fn;          // compiled state machine function pointer
    Item*  env;               // captured closure variables (params + locals)
    int    env_size;
    int64_t state;            // current state index (0=initial, -1=done)
    bool   done;
};

#define JS_MAX_GENERATORS 256
static JsGenerator js_generators[JS_MAX_GENERATORS];
static int js_generator_count = 0;

// Helper: create {value, done} iterator result object
static Item js_make_iter_result(Item value, bool done) {
    Item result = js_new_object();
    String* val_key = heap_create_name("value", 5);
    String* done_key = heap_create_name("done", 4);
    js_property_set(result, (Item){.item = s2it(val_key)}, value);
    js_property_set(result, (Item){.item = s2it(done_key)}, (Item){.item = b2it(done)});
    return result;
}

// v15: Create a 2-element array [value, next_state] for state machine returns
extern "C" Item js_gen_yield_result(Item value, int64_t next_state) {
    Item arr = js_array_new(2);
    arr.array->items[0] = value;
    arr.array->items[1] = (Item){.item = i2it(next_state)};
    return arr;
}

extern "C" Item js_generator_create(void* func_ptr, Item* env, int env_size) {
    if (js_generator_count >= JS_MAX_GENERATORS) {
        log_error("generator: exceeded max generators (%d)", JS_MAX_GENERATORS);
        return ItemNull;
    }

    int idx = js_generator_count++;
    JsGenerator* gen = &js_generators[idx];
    gen->type_id = LMD_TYPE_MAP;
    gen->state_fn = func_ptr;
    gen->env = env;
    gen->env_size = env_size;
    gen->state = 0;
    gen->done = false;

    // Create a map object that references this generator via a hidden index
    Item obj = js_new_object();
    String* gen_key = heap_create_name("__gen_idx", 9);
    js_property_set(obj, (Item){.item = s2it(gen_key)}, (Item){.item = i2it(idx)});

    return obj;
}

static JsGenerator* js_get_generator(Item gen_obj) {
    if (get_type_id(gen_obj) != LMD_TYPE_MAP) return NULL;
    String* gen_key = heap_create_name("__gen_idx", 9);
    Item idx_item = js_property_get(gen_obj, (Item){.item = s2it(gen_key)});
    if (get_type_id(idx_item) != LMD_TYPE_INT) return NULL;
    int64_t idx = it2i(idx_item);
    if (idx < 0 || idx >= js_generator_count) return NULL;
    return &js_generators[idx];
}

extern "C" Item js_generator_next(Item generator, Item input) {
    JsGenerator* gen = js_get_generator(generator);
    if (!gen) {
        log_error("generator_next: invalid generator object");
        return js_make_iter_result(ItemNull, true);
    }

    if (gen->done) {
        return js_make_iter_result(ItemNull, true);
    }

    // Call the state machine: fn(env, input, state) -> [value, next_state]
    // The state machine returns {value, next_state} as a 2-element array
    // If next_state == -1, the generator is done
    typedef Item (*GenFn)(Item*, Item, int64_t);

    Item result = ((GenFn)gen->state_fn)(gen->env, input, gen->state);

    if (get_type_id(result) == LMD_TYPE_ARRAY) {
        Array* arr = result.array;
        Item value = (arr->length > 0) ? arr->items[0] : ItemNull;
        int64_t next_state = -1;
        if (arr->length > 1 && get_type_id(arr->items[1]) == LMD_TYPE_INT) {
            next_state = it2i(arr->items[1]);
        }

        if (next_state < 0) {
            gen->done = true;
            gen->state = -1;
            return js_make_iter_result(value, true);
        } else {
            gen->state = next_state;
            return js_make_iter_result(value, false);
        }
    }

    // Fallback: function returned a plain value (final return)
    gen->done = true;
    gen->state = -1;
    return js_make_iter_result(result, true);
}

extern "C" Item js_generator_return(Item generator, Item value) {
    JsGenerator* gen = js_get_generator(generator);
    if (gen) {
        gen->done = true;
        gen->state = -1;
    }
    return js_make_iter_result(value, true);
}

extern "C" Item js_generator_throw(Item generator, Item error) {
    JsGenerator* gen = js_get_generator(generator);
    if (gen) {
        gen->done = true;
        gen->state = -1;
    }
    // Throw the error via the JS exception mechanism
    js_throw_value(error);
    return js_make_iter_result(ItemNull, true);
}

// v15: Check if an object is a generator (has __gen_idx property)
extern "C" bool js_is_generator(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return false;
    String* gen_key = heap_create_name("__gen_idx", 9);
    Item idx_item = js_property_get(obj, (Item){.item = s2it(gen_key)});
    return get_type_id(idx_item) == LMD_TYPE_INT;
}

// v15: Convert an iterable to an array. If it's a generator, drain it.
// If it's already an array, return it as-is.
extern "C" Item js_iterable_to_array(Item iterable) {
    if (get_type_id(iterable) == LMD_TYPE_ARRAY) return iterable;

    // Check if it's a generator object
    if (js_is_generator(iterable)) {
        Item arr = js_array_new(0);
        for (int safety = 0; safety < 100000; safety++) {
            Item result = js_generator_next(iterable, make_js_undefined());
            // Extract .done and .value from result
            String* done_key = heap_create_name("done", 4);
            Item done_item = js_property_get(result, (Item){.item = s2it(done_key)});
            if (get_type_id(done_item) == LMD_TYPE_BOOL && it2b(done_item)) break;
            String* val_key = heap_create_name("value", 5);
            Item value = js_property_get(result, (Item){.item = s2it(val_key)});
            array_push(arr.array, value);
        }
        return arr;
    }

    // Check if it's a Map/Set collection — convert to array of entries
    if (get_type_id(iterable) == LMD_TYPE_MAP) {
        JsCollectionData* cd = js_get_collection_data(iterable);
        if (cd) {
            if (cd->type == JS_COLLECTION_MAP) {
                // Map: return array of [key, value] pairs
                return js_collection_method(iterable, 8, ItemNull, ItemNull); // entries()
            } else {
                // Set: return array of values
                return js_collection_method(iterable, 7, ItemNull, ItemNull); // values()
            }
        }
    }

    // String: split into individual characters
    if (get_type_id(iterable) == LMD_TYPE_STRING) {
        String* str = it2s(iterable);
        if (str) {
            Item arr = js_array_new(0);
            for (int i = 0; i < (int)str->len; i++) {
                String* ch = heap_create_name(str->chars + i, 1);
                Item ch_item = (Item){.item = s2it(ch)};
                array_push(arr.array, ch_item);
            }
            return arr;
        }
    }

    // Check for iterator protocol: object with .next() method
    if (get_type_id(iterable) == LMD_TYPE_MAP) {
        // Already handled above - this is for other objects with next()
    }
    TypeId tid = get_type_id(iterable);
    if (tid == LMD_TYPE_MAP || tid == LMD_TYPE_ELEMENT) {
        // Check if object has a next() method (iterator protocol)
        String* next_key = heap_create_name("next", 4);
        Item next_fn = js_property_get(iterable, (Item){.item = s2it(next_key)});
        if (get_type_id(next_fn) == LMD_TYPE_FUNC) {
            Item arr = js_array_new(0);
            for (int safety = 0; safety < 100000; safety++) {
                Item result = js_call_function(next_fn, iterable, NULL, 0);
                String* done_key = heap_create_name("done", 4);
                Item done_item = js_property_get(result, (Item){.item = s2it(done_key)});
                if (get_type_id(done_item) == LMD_TYPE_BOOL && it2b(done_item)) break;
                String* val_key = heap_create_name("value", 5);
                Item value = js_property_get(result, (Item){.item = s2it(val_key)});
                array_push(arr.array, value);
            }
            return arr;
        }
    }

    // Fallback: return as-is (might be a map treated as iterable)
    return iterable;
}

// =============================================================================
// v14: Promise Runtime
// =============================================================================

enum JsPromiseState {
    JS_PROMISE_PENDING,
    JS_PROMISE_FULFILLED,
    JS_PROMISE_REJECTED,
};

struct JsPromise {
    TypeId type_id;                // LMD_TYPE_MAP
    JsPromiseState state;
    Item result;                   // fulfilled value or rejection reason
    Item on_fulfilled[8];          // then() callbacks (max chain depth)
    Item on_rejected[8];
    Item next_promise[8];          // chained promise for each then() handler
    bool is_finally[8];            // true if handler[i] is a finally handler
    int  then_count;
};

#define JS_MAX_PROMISES 1024
static JsPromise js_promises[JS_MAX_PROMISES];
static int js_promise_count = 0;

static JsPromise* js_alloc_promise() {
    if (js_promise_count >= JS_MAX_PROMISES) {
        log_error("promise: exceeded max promises (%d)", JS_MAX_PROMISES);
        return NULL;
    }
    if (js_promise_count >= JS_MAX_PROMISES - 16 && (js_promise_count % 16 == 0)) {
        log_info("promise: approaching capacity limit (%d/%d)", js_promise_count, JS_MAX_PROMISES);
    }
    JsPromise* p = &js_promises[js_promise_count++];
    p->type_id = LMD_TYPE_MAP;
    p->state = JS_PROMISE_PENDING;
    p->result = ItemNull;
    p->then_count = 0;
    memset(p->on_fulfilled, 0, sizeof(p->on_fulfilled));
    memset(p->on_rejected, 0, sizeof(p->on_rejected));
    memset(p->next_promise, 0, sizeof(p->next_promise));
    memset(p->is_finally, 0, sizeof(p->is_finally));
    return p;
}

static Item js_promise_to_item(JsPromise* p) {
    // Create a map object that holds a reference to the promise by index
    int idx = (int)(p - js_promises);
    Item obj = js_new_object();
    String* key = heap_create_name("__promise_idx", 13);
    js_property_set(obj, (Item){.item = s2it(key)}, (Item){.item = i2it(idx)});
    return obj;
}

static JsPromise* js_get_promise(Item promise_obj) {
    if (get_type_id(promise_obj) != LMD_TYPE_MAP) return NULL;
    String* key = heap_create_name("__promise_idx", 13);
    Item idx_item = js_property_get(promise_obj, (Item){.item = s2it(key)});
    if (get_type_id(idx_item) != LMD_TYPE_INT) return NULL;
    int64_t idx = it2i(idx_item);
    if (idx < 0 || idx >= js_promise_count) return NULL;
    return &js_promises[idx];
}

// Forward declaration — js_promise_settle is called recursively from microtask runner
static void js_promise_settle(JsPromise* p, JsPromiseState state, Item result);

// Forward declarations for promise microtask helpers
static Item js_promise_microtask_resolve(Item next_promise_item, Item value);
static Item js_promise_microtask_reject(Item next_promise_item, Item reason);

// Microtask runner for promise then() handlers.
// Called with 3 bound args: handler, result, next_promise_item.
// Calls handler(result), then settles next_promise with the return value.
static Item js_promise_microtask_run(Item handler, Item result, Item next_promise_item) {
    Item args[1] = {result};
    Item handler_result = js_call_function(handler, ItemNull, args, 1);

    JsPromise* next = js_get_promise(next_promise_item);
    if (next) {
        JsPromise* returned_p = js_get_promise(handler_result);
        if (returned_p) {
            // handler returned a promise — chain resolution
            if (returned_p->state != JS_PROMISE_PENDING) {
                js_promise_settle(next, returned_p->state, returned_p->result);
            } else {
                // pending: register then handler to settle next when returned resolves
                Item next_item = next_promise_item;
                if (returned_p->then_count < 8) {
                    Item resolve_fn = js_new_function((void*)js_promise_microtask_resolve, 2);
                    Item reject_fn = js_new_function((void*)js_promise_microtask_reject, 2);
                    returned_p->on_fulfilled[returned_p->then_count] = js_bind_function(resolve_fn, ItemNull, &next_item, 1);
                    returned_p->on_rejected[returned_p->then_count] = js_bind_function(reject_fn, ItemNull, &next_item, 1);
                    returned_p->next_promise[returned_p->then_count] = ItemNull; // these are direct settlers, no further chain
                    returned_p->then_count++;
                }
            }
        } else {
            js_promise_settle(next, JS_PROMISE_FULFILLED, handler_result);
        }
    }
    return ItemNull;
}

// Microtask runner for finally() handlers.
// Called with 4 bound args: handler, next_promise_item, state_item, original_result.
static Item js_promise_finally_microtask_run(Item handler, Item next_promise_item, Item state_item, Item original_result) {
    js_call_function(handler, ItemNull, NULL, 0);
    JsPromise* next = js_get_promise(next_promise_item);
    if (next) {
        JsPromiseState orig_state = (JsPromiseState)it2i(state_item);
        js_promise_settle(next, orig_state, original_result);
    }
    return ItemNull;
}

// Bound resolve/reject settle functions for promise chaining.
// When a then() handler returns a pending promise, these are registered as then
// handlers on that returned promise. When it settles, they settle the next promise.
static Item js_promise_microtask_resolve(Item next_promise_item, Item value) {
    JsPromise* next = js_get_promise(next_promise_item);
    if (next) js_promise_settle(next, JS_PROMISE_FULFILLED, value);
    return ItemNull;
}

static Item js_promise_microtask_reject(Item next_promise_item, Item reason) {
    JsPromise* next = js_get_promise(next_promise_item);
    if (next) js_promise_settle(next, JS_PROMISE_REJECTED, reason);
    return ItemNull;
}

// Enqueue a promise handler as a microtask with proper chaining.
// Creates a bound thunk: js_promise_microtask_run(handler, result, next_promise_item)
static void js_promise_enqueue_handler(Item handler, Item result, Item next_promise_item) {
    Item runner_fn = js_new_function((void*)js_promise_microtask_run, 3);
    Item bound_args[3] = {handler, result, next_promise_item};
    Item thunk = js_bind_function(runner_fn, ItemNull, bound_args, 3);
    js_microtask_enqueue(thunk);
}

// Enqueue a finally handler as a microtask.
static void js_promise_enqueue_finally(Item handler, Item next_promise_item, JsPromiseState state, Item result) {
    Item runner_fn = js_new_function((void*)js_promise_finally_microtask_run, 4);
    Item bound_args[4] = {handler, next_promise_item, (Item){.item = i2it((int64_t)state)}, result};
    Item thunk = js_bind_function(runner_fn, ItemNull, bound_args, 4);
    js_microtask_enqueue(thunk);
}

static void js_promise_settle(JsPromise* p, JsPromiseState state, Item result) {
    if (p->state != JS_PROMISE_PENDING) return; // already settled

    p->state = state;
    p->result = result;

    // Schedule handlers as microtasks (per ECMAScript spec)
    for (int i = 0; i < p->then_count; i++) {
        Item handler = (state == JS_PROMISE_FULFILLED) ? p->on_fulfilled[i] : p->on_rejected[i];
        Item next_item = p->next_promise[i];

        if (p->is_finally[i]) {
            // finally handler: called with 0 args, passes through original result
            if (get_type_id(handler) == LMD_TYPE_FUNC) {
                js_promise_enqueue_finally(handler, next_item, state, result);
            } else {
                JsPromise* next = js_get_promise(next_item);
                if (next) js_promise_settle(next, state, result);
            }
        } else if (get_type_id(handler) == LMD_TYPE_FUNC) {
            if (get_type_id(next_item) == LMD_TYPE_MAP) {
                // has a chained next promise — enqueue with chaining
                js_promise_enqueue_handler(handler, result, next_item);
            } else {
                // direct handler (e.g. from chaining resolution), no next promise
                Item runner_fn = js_new_function((void*)js_promise_microtask_run, 3);
                Item bound_args[3] = {handler, result, ItemNull};
                Item thunk = js_bind_function(runner_fn, ItemNull, bound_args, 3);
                js_microtask_enqueue(thunk);
            }
        } else {
            // no handler for this state — propagate to next promise directly
            JsPromise* next = js_get_promise(next_item);
            if (next) {
                js_promise_settle(next, state, result);
            }
        }
    }
}

// Resolve/reject callbacks for promise executors.
// Each callback is bound (via js_bind_function) to its promise index,
// so these work even when called asynchronously (e.g. in setTimeout).
static Item js_resolve_callback(Item promise_idx_item, Item value) {
    int64_t idx = it2i(promise_idx_item);
    if (idx >= 0 && idx < js_promise_count) {
        JsPromise* p = &js_promises[idx];
        // if value is a promise, chain resolution
        JsPromise* thenable = js_get_promise(value);
        if (thenable) {
            if (thenable->state != JS_PROMISE_PENDING) {
                js_promise_settle(p, thenable->state, thenable->result);
                js_microtask_flush();
            } else {
                // register then on thenable to forward settlement
                Item p_item = js_promise_to_item(p);
                if (thenable->then_count < 8) {
                    Item resolve_fn = js_new_function((void*)js_promise_microtask_resolve, 2);
                    Item reject_fn = js_new_function((void*)js_promise_microtask_reject, 2);
                    thenable->on_fulfilled[thenable->then_count] = js_bind_function(resolve_fn, ItemNull, &p_item, 1);
                    thenable->on_rejected[thenable->then_count] = js_bind_function(reject_fn, ItemNull, &p_item, 1);
                    thenable->next_promise[thenable->then_count] = ItemNull;
                    thenable->then_count++;
                }
            }
        } else {
            js_promise_settle(p, JS_PROMISE_FULFILLED, value);
            js_microtask_flush();
        }
    }
    return ItemNull;
}

static Item js_reject_callback(Item promise_idx_item, Item reason) {
    int64_t idx = it2i(promise_idx_item);
    if (idx >= 0 && idx < js_promise_count) {
        js_promise_settle(&js_promises[idx], JS_PROMISE_REJECTED, reason);
        js_microtask_flush();
    }
    return ItemNull;
}

extern "C" Item js_promise_create(Item executor) {
    JsPromise* p = js_alloc_promise();
    if (!p) return ItemNull;

    if (get_type_id(executor) == LMD_TYPE_FUNC) {
        // Create resolve/reject functions bound to this promise's index
        int idx = (int)(p - js_promises);
        Item idx_item = (Item){.item = i2it(idx)};
        Item resolve_base = js_new_function((void*)js_resolve_callback, 2);
        Item reject_base = js_new_function((void*)js_reject_callback, 2);
        Item resolve_fn = js_bind_function(resolve_base, ItemNull, &idx_item, 1);
        Item reject_fn = js_bind_function(reject_base, ItemNull, &idx_item, 1);

        Item args[2] = {resolve_fn, reject_fn};
        js_call_function(executor, ItemNull, args, 2);
    }

    return js_promise_to_item(p);
}

extern "C" Item js_promise_resolve(Item value) {
    // If value is already a promise, return it
    JsPromise* existing = js_get_promise(value);
    if (existing) return value;

    JsPromise* p = js_alloc_promise();
    if (!p) return ItemNull;
    js_promise_settle(p, JS_PROMISE_FULFILLED, value);
    return js_promise_to_item(p);
}

extern "C" Item js_promise_reject(Item reason) {
    JsPromise* p = js_alloc_promise();
    if (!p) return ItemNull;
    js_promise_settle(p, JS_PROMISE_REJECTED, reason);
    return js_promise_to_item(p);
}

extern "C" Item js_promise_with_resolvers(void) {
    JsPromise* p = js_alloc_promise();
    if (!p) return ItemNull;
    int idx = (int)(p - js_promises);
    Item idx_item = (Item){.item = i2it(idx)};
    Item resolve_base = js_new_function((void*)js_resolve_callback, 2);
    Item reject_base = js_new_function((void*)js_reject_callback, 2);
    Item resolve_fn = js_bind_function(resolve_base, ItemNull, &idx_item, 1);
    Item reject_fn = js_bind_function(reject_base, ItemNull, &idx_item, 1);
    Item promise = js_promise_to_item(p);
    // Build { promise, resolve, reject } object
    Item result = js_new_object();
    Item k_promise = (Item){.item = s2it(heap_create_name("promise"))};
    Item k_resolve = (Item){.item = s2it(heap_create_name("resolve"))};
    Item k_reject = (Item){.item = s2it(heap_create_name("reject"))};
    js_property_set(result, k_promise, promise);
    js_property_set(result, k_resolve, resolve_fn);
    js_property_set(result, k_reject, reject_fn);
    return result;
}

// Phase 5: Synchronous await — unwraps resolved promises, throws on rejected
extern "C" Item js_await_sync(Item value) {
    // If not a promise, return value as-is (like awaiting a non-thenable)
    JsPromise* p = js_get_promise(value);
    if (!p) return value;

    if (p->state == JS_PROMISE_FULFILLED) {
        return p->result;
    }
    if (p->state == JS_PROMISE_REJECTED) {
        // Rejected promise: throw the rejection reason
        js_throw_value(p->result);
        return ItemNull;
    }
    // Pending promise — synchronous fast-path cannot handle this
    log_debug("js: await_sync: promise still pending (no async state machine yet)");
    return make_js_undefined();
}

// ============================================================
// Phase 6: Async/Await Full State Machine Runtime
// ============================================================

// Async context: tracks a running async function's state machine
struct JsAsyncContext {
    void* state_fn;      // pointer to async_sm_<name> state machine function
    Item* env;           // env array (captures + params + locals)
    int env_size;
    int state;           // current resume state
    int promise_idx;     // index into js_promises[] for the async function's result promise
};

#define JS_MAX_ASYNC_CONTEXTS 256
static JsAsyncContext js_async_contexts[JS_MAX_ASYNC_CONTEXTS];
static int js_async_context_count = 0;

// Cached resolved value from js_async_must_suspend (fast-path)
static Item js_async_resolved_value;

// Check if an awaited value requires suspension (pending promise)
// Returns: 1 = pending (must suspend), 0 = resolved/rejected/non-promise
// For resolved/non-promise: caches result in js_async_resolved_value
// For rejected: calls js_throw_value (exception mechanism handles it)
extern "C" int64_t js_async_must_suspend(Item value) {
    JsPromise* p = js_get_promise(value);
    if (!p) {
        js_async_resolved_value = value;
        return 0;
    }
    if (p->state == JS_PROMISE_FULFILLED) {
        js_async_resolved_value = p->result;
        return 0;
    }
    if (p->state == JS_PROMISE_REJECTED) {
        js_throw_value(p->result);
        js_async_resolved_value = ItemNull;
        return 0;
    }
    // Pending — must suspend
    return 1;
}

// Get the cached resolved value after js_async_must_suspend returned 0
extern "C" Item js_async_get_resolved(void) {
    return js_async_resolved_value;
}

// Forward declarations for async callbacks
static Item js_async_resume_handler(Item ctx_idx_item, Item resolved_value);
static Item js_async_reject_handler(Item ctx_idx_item, Item reason);

// Core async state machine driver: calls the state machine and handles results
static void js_async_drive(int ctx_idx, Item input, int64_t state) {
    JsAsyncContext* ctx = &js_async_contexts[ctx_idx];
    typedef Item (*AsyncSmFn)(Item*, Item, int64_t);
    Item result = ((AsyncSmFn)ctx->state_fn)(ctx->env, input, state);

    // Parse result: [value, next_state]
    if (get_type_id(result) != LMD_TYPE_ARRAY) {
        js_promise_settle(&js_promises[ctx->promise_idx], JS_PROMISE_REJECTED, result);
        return;
    }
    Array* arr = result.array;
    if (arr->length < 2) {
        js_promise_settle(&js_promises[ctx->promise_idx], JS_PROMISE_REJECTED, ItemNull);
        return;
    }
    Item value = arr->items[0];
    int64_t next_state = it2i(arr->items[1]);

    if (next_state == -1) {
        // Done — fulfill the async function's promise
        js_promise_settle(&js_promises[ctx->promise_idx], JS_PROMISE_FULFILLED, value);
        js_microtask_flush();
    } else if (next_state == -2) {
        // Rejected — reject the async function's promise
        js_promise_settle(&js_promises[ctx->promise_idx], JS_PROMISE_REJECTED, value);
        js_microtask_flush();
    } else {
        // Suspended on pending promise — register resume/reject callbacks
        ctx->state = next_state;

        // Create bound resume callback: js_async_resume_handler(ctx_idx, resolved_val)
        Item resume_fn = js_new_function((void*)js_async_resume_handler, 2);
        Item idx_item = (Item){.item = i2it(ctx_idx)};
        Item bound_resume = js_bind_function(resume_fn, ItemNull, &idx_item, 1);

        // Create bound reject callback: js_async_reject_handler(ctx_idx, reason)
        Item reject_fn = js_new_function((void*)js_async_reject_handler, 2);
        Item bound_reject = js_bind_function(reject_fn, ItemNull, &idx_item, 1);

        // Register on the pending promise
        js_promise_then(value, bound_resume, bound_reject);
        js_microtask_flush();
    }
}

// Callback when an awaited promise resolves — resumes the async state machine
static Item js_async_resume_handler(Item ctx_idx_item, Item resolved_value) {
    int ctx_idx = (int)it2i(ctx_idx_item);
    if (ctx_idx < 0 || ctx_idx >= js_async_context_count) return ItemNull;
    JsAsyncContext* ctx = &js_async_contexts[ctx_idx];
    js_async_drive(ctx_idx, resolved_value, ctx->state);
    return ItemNull;
}

// Callback when an awaited promise rejects — re-enter state machine with exception set
static Item js_async_reject_handler(Item ctx_idx_item, Item reason) {
    int ctx_idx = (int)it2i(ctx_idx_item);
    if (ctx_idx < 0 || ctx_idx >= js_async_context_count) return ItemNull;
    JsAsyncContext* ctx = &js_async_contexts[ctx_idx];
    // Set exception so the state machine's try/catch can handle it
    js_throw_value(reason);
    // Re-enter the state machine — it will check the exception and jump to catch
    js_async_drive(ctx_idx, ItemNull, ctx->state);
    return ItemNull;
}

// Create an async context: allocates promise, returns context index
extern "C" Item js_async_context_create(void* fn_ptr, Item* env, int64_t env_size) {
    if (js_async_context_count >= JS_MAX_ASYNC_CONTEXTS) {
        log_error("js: async context limit reached (%d)", JS_MAX_ASYNC_CONTEXTS);
        return (Item){.item = i2it(-1)};
    }
    int idx = js_async_context_count++;
    JsAsyncContext* ctx = &js_async_contexts[idx];
    ctx->state_fn = fn_ptr;
    ctx->env = env;
    ctx->env_size = (int)env_size;
    ctx->state = 0;

    // Create a pending promise for this async function's result
    JsPromise* p = js_alloc_promise();
    ctx->promise_idx = (int)(p - js_promises);

    return (Item){.item = i2it(idx)};
}

// Start execution of an async state machine (initial call at state 0)
extern "C" Item js_async_start(Item ctx_idx_item) {
    int ctx_idx = (int)it2i(ctx_idx_item);
    if (ctx_idx < 0 || ctx_idx >= js_async_context_count) return ItemNull;
    js_async_drive(ctx_idx, make_js_undefined(), 0);
    return ItemNull;
}

// Get the result promise for an async context
extern "C" Item js_async_get_promise(Item ctx_idx_item) {
    int ctx_idx = (int)it2i(ctx_idx_item);
    if (ctx_idx < 0 || ctx_idx >= js_async_context_count) return ItemNull;
    JsAsyncContext* ctx = &js_async_contexts[ctx_idx];
    return js_promise_to_item(&js_promises[ctx->promise_idx]);
}

extern "C" Item js_promise_then(Item promise, Item on_fulfilled, Item on_rejected) {
    JsPromise* p = js_get_promise(promise);
    if (!p) return ItemNull;

    // Create a new promise for the then chain
    JsPromise* next = js_alloc_promise();
    if (!next) return ItemNull;
    Item next_item = js_promise_to_item(next);

    if (p->state == JS_PROMISE_PENDING) {
        // Register callbacks with chained next promise
        if (p->then_count < 8) {
            p->on_fulfilled[p->then_count] = on_fulfilled;
            p->on_rejected[p->then_count] = on_rejected;
            p->next_promise[p->then_count] = next_item;
            p->then_count++;
        }
    } else if (p->state == JS_PROMISE_FULFILLED) {
        if (get_type_id(on_fulfilled) == LMD_TYPE_FUNC) {
            // per spec: schedule as microtask even if already resolved
            js_promise_enqueue_handler(on_fulfilled, p->result, next_item);
        } else {
            // no handler — pass through value
            js_promise_settle(next, JS_PROMISE_FULFILLED, p->result);
        }
    } else {
        if (get_type_id(on_rejected) == LMD_TYPE_FUNC) {
            js_promise_enqueue_handler(on_rejected, p->result, next_item);
        } else {
            js_promise_settle(next, JS_PROMISE_REJECTED, p->result);
        }
    }

    return next_item;
}

extern "C" Item js_promise_catch(Item promise, Item on_rejected) {
    Item undef = (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
    return js_promise_then(promise, undef, on_rejected);
}

extern "C" Item js_promise_finally(Item promise, Item on_finally) {
    JsPromise* p = js_get_promise(promise);
    if (!p) return ItemNull;

    JsPromise* next = js_alloc_promise();
    if (!next) return ItemNull;
    Item next_item = js_promise_to_item(next);

    if (p->state != JS_PROMISE_PENDING) {
        if (get_type_id(on_finally) == LMD_TYPE_FUNC) {
            // per spec: schedule as microtask even if already settled
            js_promise_enqueue_finally(on_finally, next_item, p->state, p->result);
        } else {
            js_promise_settle(next, p->state, p->result);
        }
    } else {
        // pending: store both fulfilled/rejected handlers as the same finally callback
        if (p->then_count < 8) {
            p->on_fulfilled[p->then_count] = on_finally;
            p->on_rejected[p->then_count] = on_finally;
            p->next_promise[p->then_count] = next_item;
            p->is_finally[p->then_count] = true;
            p->then_count++;
        }
    }

    return next_item;
}

extern "C" Item js_promise_all(Item iterable) {
    if (get_type_id(iterable) != LMD_TYPE_ARRAY) return js_promise_resolve(iterable);

    Array* arr = iterable.array;
    int count = arr->length;

    if (count == 0) {
        Item empty_arr = js_array_new(0);
        return js_promise_resolve(empty_arr);
    }

    // Simplified: resolve all synchronously settled promises
    Item results_arr = js_array_new(count);
    Array* results = results_arr.array;
    results->length = count;
    int resolved = 0;

    for (int i = 0; i < count; i++) {
        JsPromise* p = js_get_promise(arr->items[i]);
        if (p) {
            if (p->state == JS_PROMISE_REJECTED) {
                return js_promise_reject(p->result);
            }
            results->items[i] = (p->state == JS_PROMISE_FULFILLED) ? p->result : ItemNull;
            if (p->state == JS_PROMISE_FULFILLED) resolved++;
        } else {
            results->items[i] = arr->items[i]; // non-promise value
            resolved++;
        }
    }

    return js_promise_resolve(results_arr);
}

extern "C" Item js_promise_race(Item iterable) {
    if (get_type_id(iterable) != LMD_TYPE_ARRAY) return js_promise_resolve(iterable);

    Array* arr = iterable.array;
    for (int i = 0; i < arr->length; i++) {
        JsPromise* p = js_get_promise(arr->items[i]);
        if (p && p->state == JS_PROMISE_FULFILLED) {
            return js_promise_resolve(p->result);
        }
        if (p && p->state == JS_PROMISE_REJECTED) {
            return js_promise_reject(p->result);
        }
        if (!p) {
            return js_promise_resolve(arr->items[i]);
        }
    }

    // All pending — return a pending promise
    JsPromise* result = js_alloc_promise();
    if (!result) return ItemNull;
    return js_promise_to_item(result);
}

extern "C" Item js_promise_any(Item iterable) {
    if (get_type_id(iterable) != LMD_TYPE_ARRAY) return js_promise_resolve(iterable);

    Array* arr = iterable.array;
    for (int i = 0; i < arr->length; i++) {
        JsPromise* p = js_get_promise(arr->items[i]);
        if (p && p->state == JS_PROMISE_FULFILLED) {
            return js_promise_resolve(p->result);
        }
        if (!p) {
            return js_promise_resolve(arr->items[i]);
        }
    }

    // All rejected
    Item err = js_new_error((Item){.item = s2it(heap_create_name("All promises were rejected", 26))});
    return js_promise_reject(err);
}

extern "C" Item js_promise_all_settled(Item iterable) {
    if (get_type_id(iterable) != LMD_TYPE_ARRAY) return js_promise_resolve(iterable);

    Array* arr = iterable.array;
    Item results_arr = js_array_new(arr->length);
    Array* results = results_arr.array;

    for (int i = 0; i < arr->length; i++) {
        JsPromise* p = js_get_promise(arr->items[i]);
        Item entry = js_new_object();
        String* status_key = heap_create_name("status", 6);

        if (p) {
            if (p->state == JS_PROMISE_FULFILLED) {
                js_property_set(entry, (Item){.item = s2it(status_key)},
                    (Item){.item = s2it(heap_create_name("fulfilled", 9))});
                String* val_key = heap_create_name("value", 5);
                js_property_set(entry, (Item){.item = s2it(val_key)}, p->result);
            } else if (p->state == JS_PROMISE_REJECTED) {
                js_property_set(entry, (Item){.item = s2it(status_key)},
                    (Item){.item = s2it(heap_create_name("rejected", 8))});
                String* reason_key = heap_create_name("reason", 6);
                js_property_set(entry, (Item){.item = s2it(reason_key)}, p->result);
            } else {
                js_property_set(entry, (Item){.item = s2it(status_key)},
                    (Item){.item = s2it(heap_create_name("pending", 7))});
            }
        } else {
            js_property_set(entry, (Item){.item = s2it(status_key)},
                (Item){.item = s2it(heap_create_name("fulfilled", 9))});
            String* val_key = heap_create_name("value", 5);
            js_property_set(entry, (Item){.item = s2it(val_key)}, arr->items[i]);
        }
        results->items[i] = entry;
    }

    return js_promise_resolve(results_arr);
}

// =============================================================================
// v14: ES Module Runtime
// =============================================================================

#define JS_MAX_MODULES 64

struct JsModule {
    String* specifier;
    Item    namespace_obj;
};

static JsModule js_modules[JS_MAX_MODULES];
static int js_module_count_v14 = 0;

extern "C" void js_module_register(Item specifier, Item namespace_obj) {
    if (js_module_count_v14 >= JS_MAX_MODULES) {
        log_error("module: exceeded max modules (%d)", JS_MAX_MODULES);
        return;
    }
    if (js_module_count_v14 >= JS_MAX_MODULES - 4) {
        log_info("module: approaching capacity limit (%d/%d)", js_module_count_v14, JS_MAX_MODULES);
    }
    if (get_type_id(specifier) != LMD_TYPE_STRING) return;

    String* spec = it2s(specifier);
    // Check for existing registration
    for (int i = 0; i < js_module_count_v14; i++) {
        if (js_modules[i].specifier->len == spec->len &&
            memcmp(js_modules[i].specifier->chars, spec->chars, spec->len) == 0) {
            js_modules[i].namespace_obj = namespace_obj;
            return;
        }
    }

    JsModule* m = &js_modules[js_module_count_v14++];
    m->specifier = spec;
    m->namespace_obj = namespace_obj;
}

extern "C" Item js_module_get(Item specifier) {
    if (get_type_id(specifier) != LMD_TYPE_STRING) return ItemNull;
    String* spec = it2s(specifier);

    // check for built-in modules (bare specifier or with .js suffix from resolver)
    if ((spec->len == 2 && memcmp(spec->chars, "fs", 2) == 0) ||
        (spec->len == 5 && memcmp(spec->chars, "fs.js", 5) == 0) ||
        (spec->len == 7 && memcmp(spec->chars, "node:fs", 7) == 0)) {
        extern Item js_get_fs_namespace(void);
        return js_get_fs_namespace();
    }
    if ((spec->len == 13 && memcmp(spec->chars, "child_process", 13) == 0) ||
        (spec->len == 16 && memcmp(spec->chars, "child_process.js", 16) == 0) ||
        (spec->len == 18 && memcmp(spec->chars, "node:child_process", 18) == 0)) {
        extern Item js_get_child_process_namespace(void);
        return js_get_child_process_namespace();
    }

    for (int i = 0; i < js_module_count_v14; i++) {
        if (js_modules[i].specifier->len == spec->len &&
            memcmp(js_modules[i].specifier->chars, spec->chars, spec->len) == 0) {
            return js_modules[i].namespace_obj;
        }
    }
    return ItemNull;
}

extern "C" Item js_module_namespace_create(Item exports_map) {
    // Module namespace is just a frozen map object
    return exports_map;
}

// =============================================================================
// TextEncoder / TextDecoder (UTF-8 only)
// =============================================================================

extern "C" Item js_text_encoder_new(void) {
    Item obj = js_new_object();
    Item k = (Item){.item = s2it(heap_create_name("encoding"))};
    Item v = (Item){.item = s2it(heap_create_name("utf-8"))};
    js_property_set(obj, k, v);
    // Mark as TextEncoder for method dispatch
    Item type_key = (Item){.item = s2it(heap_create_name("__class_name__"))};
    Item type_val = (Item){.item = s2it(heap_create_name("TextEncoder"))};
    js_property_set(obj, type_key, type_val);
    return obj;
}

extern "C" Item js_text_encoder_encode(Item encoder, Item str) {
    (void)encoder;
    if (get_type_id(str) != LMD_TYPE_STRING) return js_array_new(0);
    String* s = it2s(str);
    if (!s || s->len == 0) return js_array_new(0);
    // Create array of byte values
    Item result = js_array_new(0);
    for (int i = 0; i < (int)s->len; i++) {
        array_push(result.array, (Item){.item = i2it((unsigned char)s->chars[i])});
    }
    return result;
}

extern "C" Item js_text_decoder_new(void) {
    Item obj = js_new_object();
    Item k = (Item){.item = s2it(heap_create_name("encoding"))};
    Item v = (Item){.item = s2it(heap_create_name("utf-8"))};
    js_property_set(obj, k, v);
    // Mark as TextDecoder for method dispatch
    Item type_key = (Item){.item = s2it(heap_create_name("__class_name__"))};
    Item type_val = (Item){.item = s2it(heap_create_name("TextDecoder"))};
    js_property_set(obj, type_key, type_val);
    return obj;
}

extern "C" Item js_text_decoder_decode(Item decoder, Item input) {
    (void)decoder;
    TypeId tid = get_type_id(input);
    if (tid == LMD_TYPE_ARRAY) {
        // Array of byte values → string
        Array* arr = input.array;
        if (!arr || arr->length == 0)
            return (Item){.item = s2it(heap_create_name(""))};
        char* buf = (char*)alloca(arr->length + 1);
        for (int i = 0; i < arr->length; i++) {
            buf[i] = (char)(unsigned char)js_get_number(arr->items[i]);
        }
        buf[arr->length] = '\0';
        return (Item){.item = s2it(heap_strcpy(buf, arr->length))};
    }
    // Handle TypedArray (Uint8Array etc.)
    if (tid == LMD_TYPE_MAP && js_is_typed_array(input)) {
        Map* m = input.map;
        JsTypedArray* ta = (JsTypedArray*)m->data;
        if (!ta || ta->length == 0)
            return (Item){.item = s2it(heap_create_name(""))};
        char* buf = (char*)alloca(ta->length + 1);
        uint8_t* bytes = (uint8_t*)ta->data;
        for (int i = 0; i < ta->length; i++) {
            buf[i] = (char)bytes[i];
        }
        buf[ta->length] = '\0';
        return (Item){.item = s2it(heap_strcpy(buf, ta->length))};
    }
    if (tid == LMD_TYPE_STRING) return input;
    return (Item){.item = s2it(heap_create_name(""))};
}

// =============================================================================
// WeakMap / WeakSet stubs (aliased to Map/Set for PDF.js compat)
// =============================================================================

extern "C" Item js_weakmap_new(void) {
    return js_map_collection_new();
}

extern "C" Item js_weakset_new(void) {
    return js_set_collection_new();
}
