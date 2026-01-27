/**
 * JavaScript Runtime Functions for Lambda v2
 * 
 * Implements JavaScript semantics on top of Lambda's type system.
 * All functions are callable from MIR JIT compiled code.
 */
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <cstdio>

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
        snprintf(buffer, sizeof(buffer), "%d", it2i(value));
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
        
    case LMD_TYPE_ARRAY:
        // TODO: Implement array toString
        return (Item){.item = s2it(heap_create_name("[object Array]"))};
        
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
    if (d == (double)(int)d && d >= INT56_MIN && d <= INT56_MAX) {
        return (Item){.item = i2it((int)d)};
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
    
    // Numeric addition
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
    double l = js_get_number(left);
    double r = js_get_number(right);
    return js_make_number(l * r);
}

extern "C" Item js_divide(Item left, Item right) {
    double l = js_get_number(left);
    double r = js_get_number(right);
    return js_make_number(l / r);  // JS division always produces float
}

extern "C" Item js_modulo(Item left, Item right) {
    double l = js_get_number(left);
    double r = js_get_number(right);
    return js_make_number(fmod(l, r));
}

extern "C" Item js_power(Item left, Item right) {
    double l = js_get_number(left);
    double r = js_get_number(right);
    return js_make_number(pow(l, r));
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

extern "C" Item js_bitwise_and(Item left, Item right) {
    int32_t l = (int32_t)js_get_number(left);
    int32_t r = (int32_t)js_get_number(right);
    return (Item){.item = i2it(l & r)};
}

extern "C" Item js_bitwise_or(Item left, Item right) {
    int32_t l = (int32_t)js_get_number(left);
    int32_t r = (int32_t)js_get_number(right);
    return (Item){.item = i2it(l | r)};
}

extern "C" Item js_bitwise_xor(Item left, Item right) {
    int32_t l = (int32_t)js_get_number(left);
    int32_t r = (int32_t)js_get_number(right);
    return (Item){.item = i2it(l ^ r)};
}

extern "C" Item js_bitwise_not(Item operand) {
    int32_t val = (int32_t)js_get_number(operand);
    return (Item){.item = i2it(~val)};
}

extern "C" Item js_left_shift(Item left, Item right) {
    int32_t l = (int32_t)js_get_number(left);
    uint32_t r = (uint32_t)js_get_number(right) & 0x1F;
    return (Item){.item = i2it(l << r)};
}

extern "C" Item js_right_shift(Item left, Item right) {
    int32_t l = (int32_t)js_get_number(left);
    uint32_t r = (uint32_t)js_get_number(right) & 0x1F;
    return (Item){.item = i2it(l >> r)};
}

extern "C" Item js_unsigned_right_shift(Item left, Item right) {
    uint32_t l = (uint32_t)js_get_number(left);
    uint32_t r = (uint32_t)js_get_number(right) & 0x1F;
    return (Item){.item = i2it((int32_t)(l >> r))};
}

// =============================================================================
// Unary Operators
// =============================================================================

extern "C" Item js_unary_plus(Item operand) {
    return js_to_number(operand);
}

extern "C" Item js_unary_minus(Item operand) {
    double val = js_get_number(operand);
    return js_make_number(-val);
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
        result = "number";
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
// Array Functions
// =============================================================================

extern "C" Item js_array_new(int length) {
    Array* arr = array();
    // Pre-allocate capacity if needed
    Item null_item = {.item = ITEM_NULL};
    for (int i = 0; i < length; i++) {
        list_push(arr, null_item);
    }
    return (Item){.array = arr};
}

extern "C" Item js_array_get(Item array, Item index) {
    if (get_type_id(array) != LMD_TYPE_ARRAY) {
        return ItemNull;
    }
    
    int idx = (int)js_get_number(index);
    Array* arr = array.array;
    
    if (idx >= 0 && idx < arr->length) {
        return arr->items[idx];
    }
    
    return ItemNull;
}

extern "C" Item js_array_set(Item array, Item index, Item value) {
    if (get_type_id(array) != LMD_TYPE_ARRAY) {
        return value;
    }
    
    int idx = (int)js_get_number(index);
    Array* arr = array.array;
    
    if (idx >= 0 && idx < arr->length) {
        arr->items[idx] = value;
    }
    // TODO: Expand array if idx >= length
    
    return value;
}

extern "C" int js_array_length(Item array) {
    if (get_type_id(array) != LMD_TYPE_ARRAY) {
        return 0;
    }
    return array.array->length;
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
