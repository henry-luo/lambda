#include "js_transpiler.hpp"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <math.h>
#include <string.h>

// JavaScript global object
static Item js_global_object = {.item = ITEM_NULL};

// Type conversion functions

Item js_to_primitive(Item value, const char* hint) {
    TypeId type = item_type(value);
    
    // Already primitive
    if (type == LMD_TYPE_NULL || type == LMD_TYPE_BOOL || 
        type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT || 
        type == LMD_TYPE_STRING || type == LMD_TYPE_SYMBOL) {
        return value;
    }
    
    // For objects, try valueOf() then toString()
    if (type == LMD_TYPE_MAP) {
        // TODO: Implement proper object to primitive conversion
        // For now, just return string representation
        return (Item){.item = s2it("[object Object]")};
    }
    
    return value;
}

Item js_to_number(Item value) {
    TypeId type = item_type(value);
    
    switch (type) {
        case LMD_TYPE_NULL:
            return (Item){.item = d2it(0.0)}; // null -> 0
        case LMD_TYPE_BOOL:
            return (Item){.item = d2it(it2b(value) ? 1.0 : 0.0)};
        case LMD_TYPE_INT:
            return (Item){.item = d2it((double)it2i(value))};
        case LMD_TYPE_FLOAT:
            return value; // Already a number
        case LMD_TYPE_STRING: {
            String* str = it2s(value);
            if (str->len == 0) {
                return (Item){.item = d2it(0.0)}; // Empty string -> 0
            }
            // TODO: Proper string to number conversion
            char* endptr;
            double num = strtod(str->chars, &endptr);
            if (endptr == str->chars + str->len) {
                return (Item){.item = d2it(num)};
            } else {
                return (Item){.item = d2it(NAN)}; // Invalid number
            }
        }
        default:
            return (Item){.item = d2it(NAN)};
    }
}

Item js_to_string(Item value) {
    TypeId type = item_type(value);
    
    switch (type) {
        case LMD_TYPE_NULL:
            return (Item){.item = s2it("null")};
        case LMD_TYPE_BOOL:
            return (Item){.item = s2it(it2b(value) ? "true" : "false")};
        case LMD_TYPE_INT: {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d", it2i(value));
            return (Item){.item = s2it(buffer)};
        }
        case LMD_TYPE_FLOAT: {
            double d = it2d(value);
            if (isnan(d)) {
                return (Item){.item = s2it("NaN")};
            } else if (isinf(d)) {
                return (Item){.item = s2it(d > 0 ? "Infinity" : "-Infinity")};
            } else {
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%.17g", d);
                return (Item){.item = s2it(buffer)};
            }
        }
        case LMD_TYPE_STRING:
            return value; // Already a string
        default:
            return (Item){.item = s2it("[object Object]")};
    }
}

Item js_to_boolean(Item value) {
    return (Item){.item = b2it(js_is_truthy(value))};
}

bool js_is_truthy(Item value) {
    TypeId type = item_type(value);
    
    switch (type) {
        case LMD_TYPE_NULL:
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
            return str->len > 0;
        }
        default:
            return true; // Objects are truthy
    }
}

// Arithmetic operators

Item js_add(Item left, Item right) {
    // JavaScript addition: string concatenation or numeric addition
    Item left_prim = js_to_primitive(left, "default");
    Item right_prim = js_to_primitive(right, "default");
    
    if (item_type(left_prim) == LMD_TYPE_STRING || item_type(right_prim) == LMD_TYPE_STRING) {
        // String concatenation
        Item left_str = js_to_string(left_prim);
        Item right_str = js_to_string(right_prim);
        return fn_join(left_str, right_str);
    } else {
        // Numeric addition
        Item left_num = js_to_number(left_prim);
        Item right_num = js_to_number(right_prim);
        return (Item){.item = d2it(it2d(left_num) + it2d(right_num))};
    }
}

Item js_subtract(Item left, Item right) {
    Item left_num = js_to_number(left);
    Item right_num = js_to_number(right);
    return (Item){.item = d2it(it2d(left_num) - it2d(right_num))};
}

Item js_multiply(Item left, Item right) {
    Item left_num = js_to_number(left);
    Item right_num = js_to_number(right);
    return (Item){.item = d2it(it2d(left_num) * it2d(right_num))};
}

Item js_divide(Item left, Item right) {
    Item left_num = js_to_number(left);
    Item right_num = js_to_number(right);
    return (Item){.item = d2it(it2d(left_num) / it2d(right_num))};
}

Item js_modulo(Item left, Item right) {
    Item left_num = js_to_number(left);
    Item right_num = js_to_number(right);
    return (Item){.item = d2it(fmod(it2d(left_num), it2d(right_num)))};
}

Item js_power(Item left, Item right) {
    Item left_num = js_to_number(left);
    Item right_num = js_to_number(right);
    return (Item){.item = d2it(pow(it2d(left_num), it2d(right_num)))};
}

// Comparison operators

Item js_equal(Item left, Item right) {
    // JavaScript == operator with type coercion
    TypeId left_type = item_type(left);
    TypeId right_type = item_type(right);
    
    if (left_type == right_type) {
        return js_strict_equal(left, right);
    }
    
    // null == undefined
    if ((left_type == LMD_TYPE_NULL && right_type == LMD_TYPE_NULL)) {
        return (Item){.item = b2it(true)};
    }
    
    // Number and string comparison
    if ((left_type == LMD_TYPE_FLOAT && right_type == LMD_TYPE_STRING) ||
        (left_type == LMD_TYPE_STRING && right_type == LMD_TYPE_FLOAT)) {
        Item left_num = js_to_number(left);
        Item right_num = js_to_number(right);
        return (Item){.item = b2it(it2d(left_num) == it2d(right_num))};
    }
    
    // Boolean to number conversion
    if (left_type == LMD_TYPE_BOOL) {
        return js_equal(js_to_number(left), right);
    }
    if (right_type == LMD_TYPE_BOOL) {
        return js_equal(left, js_to_number(right));
    }
    
    return (Item){.item = b2it(false)};
}

Item js_not_equal(Item left, Item right) {
    return (Item){.item = b2it(!it2b(js_equal(left, right)))};
}

Item js_strict_equal(Item left, Item right) {
    TypeId left_type = item_type(left);
    TypeId right_type = item_type(right);
    
    if (left_type != right_type) {
        return (Item){.item = b2it(false)};
    }
    
    switch (left_type) {
        case LMD_TYPE_NULL:
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
            return (Item){.item = b2it(l_str->len == r_str->len && 
                       memcmp(l_str->chars, r_str->chars, l_str->len) == 0)};
        }
        default:
            // Object identity comparison
            return (Item){.item = b2it(left.item == right.item)};
    }
}

Item js_strict_not_equal(Item left, Item right) {
    return (Item){.item = b2it(!it2b(js_strict_equal(left, right)))};
}

Item js_less_than(Item left, Item right) {
    Item left_prim = js_to_primitive(left, "number");
    Item right_prim = js_to_primitive(right, "number");
    
    if (item_type(left_prim) == LMD_TYPE_STRING && item_type(right_prim) == LMD_TYPE_STRING) {
        // String comparison
        String* l_str = it2s(left_prim);
        String* r_str = it2s(right_prim);
        int cmp = memcmp(l_str->chars, r_str->chars, 
                        l_str->len < r_str->len ? l_str->len : r_str->len);
        if (cmp == 0) {
            return (Item){.item = b2it(l_str->len < r_str->len)};
        }
        return (Item){.item = b2it(cmp < 0)};
    } else {
        // Numeric comparison
        Item left_num = js_to_number(left_prim);
        Item right_num = js_to_number(right_prim);
        double l = it2d(left_num);
        double r = it2d(right_num);
        if (isnan(l) || isnan(r)) {
            return (Item){.item = b2it(false)};
        }
        return (Item){.item = b2it(l < r)};
    }
}

Item js_less_equal(Item left, Item right) {
    Item gt = js_greater_than(left, right);
    return (Item){.item = b2it(!it2b(gt))};
}

Item js_greater_than(Item left, Item right) {
    return js_less_than(right, left);
}

Item js_greater_equal(Item left, Item right) {
    Item lt = js_less_than(left, right);
    return (Item){.item = b2it(!it2b(lt))};
}

// Logical operators

Item js_logical_and(Item left, Item right) {
    if (js_is_truthy(left)) {
        return right;
    } else {
        return left;
    }
}

Item js_logical_or(Item left, Item right) {
    if (js_is_truthy(left)) {
        return left;
    } else {
        return right;
    }
}

Item js_logical_not(Item operand) {
    return (Item){.item = b2it(!js_is_truthy(operand))};
}

// Bitwise operators

Item js_bitwise_and(Item left, Item right) {
    int32_t l = (int32_t)it2d(js_to_number(left));
    int32_t r = (int32_t)it2d(js_to_number(right));
    return (Item){.item = i2it(l & r)};
}

Item js_bitwise_or(Item left, Item right) {
    int32_t l = (int32_t)it2d(js_to_number(left));
    int32_t r = (int32_t)it2d(js_to_number(right));
    return (Item){.item = i2it(l | r)};
}

Item js_bitwise_xor(Item left, Item right) {
    int32_t l = (int32_t)it2d(js_to_number(left));
    int32_t r = (int32_t)it2d(js_to_number(right));
    return (Item){.item = i2it(l ^ r)};
}

Item js_bitwise_not(Item operand) {
    int32_t val = (int32_t)it2d(js_to_number(operand));
    return (Item){.item = i2it(~val)};
}

Item js_left_shift(Item left, Item right) {
    int32_t l = (int32_t)it2d(js_to_number(left));
    uint32_t r = (uint32_t)it2d(js_to_number(right)) & 0x1F;
    return (Item){.item = i2it(l << r)};
}

Item js_right_shift(Item left, Item right) {
    int32_t l = (int32_t)it2d(js_to_number(left));
    uint32_t r = (uint32_t)it2d(js_to_number(right)) & 0x1F;
    return (Item){.item = i2it(l >> r)};
}

Item js_unsigned_right_shift(Item left, Item right) {
    uint32_t l = (uint32_t)it2d(js_to_number(left));
    uint32_t r = (uint32_t)it2d(js_to_number(right)) & 0x1F;
    return (Item){.item = i2it((int32_t)(l >> r))};
}

// Unary operators

Item js_unary_plus(Item operand) {
    return js_to_number(operand);
}

Item js_unary_minus(Item operand) {
    Item num = js_to_number(operand);
    return (Item){.item = d2it(-it2d(num))};
}

Item js_increment(Item operand, bool prefix) {
    // TODO: Implement proper lvalue increment
    Item num = js_to_number(operand);
    return (Item){.item = d2it(it2d(num) + 1.0)};
}

Item js_decrement(Item operand, bool prefix) {
    // TODO: Implement proper lvalue decrement
    Item num = js_to_number(operand);
    return (Item){.item = d2it(it2d(num) - 1.0)};
}

Item js_typeof(Item value) {
    TypeId type = item_type(value);
    
    switch (type) {
        case LMD_TYPE_NULL:
            return (Item){.item = s2it("undefined")}; // Both null and undefined
        case LMD_TYPE_BOOL:
            return (Item){.item = s2it("boolean")};
        case LMD_TYPE_INT:
        case LMD_TYPE_FLOAT:
            return (Item){.item = s2it("number")};
        case LMD_TYPE_STRING:
            return (Item){.item = s2it("string")};
        case LMD_TYPE_SYMBOL:
            return (Item){.item = s2it("symbol")};
        case LMD_TYPE_FUNC:
            return (Item){.item = s2it("function")};
        default:
            return (Item){.item = s2it("object")};
    }
}

// Object and property functions

Item js_new_object() {
    Map* obj = map(0); // Create empty map
    return (Item){.map = obj};
}

Item js_new_array(int length) {
    Array* arr = array();
    // TODO: Initialize array with specified length
    return (Item){.array = arr};
}

Item js_property_access(Item object, Item key) {
    if (item_type(object) != LMD_TYPE_MAP) {
        // TODO: Handle property access on primitives
        return (Item){.item = ITEM_NULL};
    }
    
    Map* obj = object.map;
    Item key_str = js_to_string(key);
    
    return map_get(obj, key_str);
}

Item js_property_set(Item object, Item key, Item value) {
    if (item_type(object) != LMD_TYPE_MAP) {
        // TODO: Handle property setting on primitives
        return value;
    }
    
    Map* obj = object.map;
    Item key_str = js_to_string(key);
    
    // TODO: Implement proper map setting
    // map_set(obj, key_str, value);
    return value;
}

Item js_property_delete(Item object, Item key) {
    if (item_type(object) != LMD_TYPE_MAP) {
        return (Item){.item = b2it(true)};
    }
    
    Map* obj = object.map;
    Item key_str = js_to_string(key);
    
    // TODO: Implement map_delete
    return (Item){.item = b2it(true)};
}

bool js_property_has(Item object, Item key) {
    if (item_type(object) != LMD_TYPE_MAP) {
        return false;
    }
    
    Map* obj = object.map;
    Item key_str = js_to_string(key);
    Item value = map_get(obj, key_str);
    
    return value.item != ITEM_NULL;
}

// JavaScript function object structure
typedef struct JsFunction {
    void* func_ptr;                 // C function pointer
    int param_count;                // Number of parameters
    Item* closure_vars;             // Closure variables (TODO)
    int closure_count;              // Number of closure variables
} JsFunction;

// Function call functions

Item js_call_function(Item func, Item this_binding, Item* args, int arg_count) {
    if (item_type(func) != LMD_TYPE_FUNC) {
        // TODO: Throw TypeError
        log_error("Attempted to call non-function value");
        return (Item){.item = ITEM_NULL};
    }
    
    JsFunction* js_func = (JsFunction*)func.raw_pointer;
    
    // For now, call the function with up to 5 parameters
    // TODO: Implement proper variadic function calling
    typedef Item (*JsFuncPtr0)();
    typedef Item (*JsFuncPtr1)(Item);
    typedef Item (*JsFuncPtr2)(Item, Item);
    typedef Item (*JsFuncPtr3)(Item, Item, Item);
    typedef Item (*JsFuncPtr4)(Item, Item, Item, Item);
    typedef Item (*JsFuncPtr5)(Item, Item, Item, Item, Item);
    
    switch (js_func->param_count) {
        case 0: {
            JsFuncPtr0 f = (JsFuncPtr0)js_func->func_ptr;
            return f();
        }
        case 1: {
            JsFuncPtr1 f = (JsFuncPtr1)js_func->func_ptr;
            Item arg0 = (arg_count > 0) ? args[0] : (Item){.item = ITEM_NULL};
            return f(arg0);
        }
        case 2: {
            JsFuncPtr2 f = (JsFuncPtr2)js_func->func_ptr;
            Item arg0 = (arg_count > 0) ? args[0] : (Item){.item = ITEM_NULL};
            Item arg1 = (arg_count > 1) ? args[1] : (Item){.item = ITEM_NULL};
            return f(arg0, arg1);
        }
        case 3: {
            JsFuncPtr3 f = (JsFuncPtr3)js_func->func_ptr;
            Item arg0 = (arg_count > 0) ? args[0] : (Item){.item = ITEM_NULL};
            Item arg1 = (arg_count > 1) ? args[1] : (Item){.item = ITEM_NULL};
            Item arg2 = (arg_count > 2) ? args[2] : (Item){.item = ITEM_NULL};
            return f(arg0, arg1, arg2);
        }
        case 4: {
            JsFuncPtr4 f = (JsFuncPtr4)js_func->func_ptr;
            Item arg0 = (arg_count > 0) ? args[0] : (Item){.item = ITEM_NULL};
            Item arg1 = (arg_count > 1) ? args[1] : (Item){.item = ITEM_NULL};
            Item arg2 = (arg_count > 2) ? args[2] : (Item){.item = ITEM_NULL};
            Item arg3 = (arg_count > 3) ? args[3] : (Item){.item = ITEM_NULL};
            return f(arg0, arg1, arg2, arg3);
        }
        case 5: {
            JsFuncPtr5 f = (JsFuncPtr5)js_func->func_ptr;
            Item arg0 = (arg_count > 0) ? args[0] : (Item){.item = ITEM_NULL};
            Item arg1 = (arg_count > 1) ? args[1] : (Item){.item = ITEM_NULL};
            Item arg2 = (arg_count > 2) ? args[2] : (Item){.item = ITEM_NULL};
            Item arg3 = (arg_count > 3) ? args[3] : (Item){.item = ITEM_NULL};
            Item arg4 = (arg_count > 4) ? args[4] : (Item){.item = ITEM_NULL};
            return f(arg0, arg1, arg2, arg3, arg4);
        }
        default:
            log_error("Function with %d parameters not supported yet", js_func->param_count);
            return (Item){.item = ITEM_NULL};
    }
}

Item js_new_function(void* func_ptr, int param_count) {
    JsFunction* js_func = (JsFunction*)heap_alloc(sizeof(JsFunction), LMD_TYPE_FUNC);
    js_func->func_ptr = func_ptr;
    js_func->param_count = param_count;
    js_func->closure_vars = NULL;
    js_func->closure_count = 0;
    
    return (Item){.raw_pointer = js_func};
}

// Array functions

Item js_array_get(Item array, Item index) {
    if (item_type(array) != LMD_TYPE_ARRAY) {
        return (Item){.item = ITEM_NULL};
    }
    
    Array* arr = array.array;
    int idx = it2i(js_to_number(index));
    
    if (idx >= 0 && idx < arr->length) {
        return arr->items[idx];
    }
    
    return (Item){.item = ITEM_NULL};
}

Item js_array_set(Item array, Item index, Item value) {
    if (item_type(array) != LMD_TYPE_ARRAY) {
        return value;
    }
    
    Array* arr = array.array;
    int idx = it2i(js_to_number(index));
    
    if (idx >= 0) {
        // TODO: Expand array if necessary
        if (idx < arr->length) {
            arr->items[idx] = value;
        }
    }
    
    return value;
}

int js_array_length(Item array) {
    if (item_type(array) != LMD_TYPE_ARRAY) {
        return 0;
    }
    
    Array* arr = array.array;
    return arr->length;
}

Item js_array_push(Item array, Item value) {
    if (item_type(array) != LMD_TYPE_ARRAY) {
        return (Item){.item = i2it(0)};
    }
    
    Array* arr = array.array;
    // TODO: Implement array push
    return (Item){.item = i2it(arr->length)};
}

Item js_array_pop(Item array) {
    if (item_type(array) != LMD_TYPE_ARRAY) {
        return (Item){.item = ITEM_NULL};
    }
    
    Array* arr = array.array;
    if (arr->length > 0) {
        Item value = arr->items[arr->length - 1];
        arr->length--;
        return value;
    }
    
    return (Item){.item = ITEM_NULL};
}

// Forward declarations for global functions
Item js_parse_int(Item value);
Item js_parse_float(Item value);
Item js_is_nan(Item value);
Item js_is_finite(Item value);

// Global object functions

Item js_get_global() {
    return js_global_object;
}

// Built-in array methods
Item js_array_map(Item array, Item callback) {
    if (item_type(array) != LMD_TYPE_ARRAY || item_type(callback) != LMD_TYPE_FUNC) {
        return (Item){.item = ITEM_NULL};
    }
    
    Array* arr = array.array;
    Item result = js_new_array(arr->length);
    Array* result_arr = result.array;
    
    for (int i = 0; i < arr->length; i++) {
        Item args[3] = { arr->items[i], (Item){.item = i2it(i)}, array };
        Item mapped_value = js_call_function(callback, (Item){.item = ITEM_NULL}, args, 3);
        result_arr->items[i] = mapped_value;
    }
    
    return result;
}

Item js_array_filter(Item array, Item predicate) {
    if (item_type(array) != LMD_TYPE_ARRAY || item_type(predicate) != LMD_TYPE_FUNC) {
        return (Item){.item = ITEM_NULL};
    }
    
    Array* arr = array.array;
    Item result = js_new_array(0);
    Array* result_arr = result.array;
    int result_index = 0;
    
    for (int i = 0; i < arr->length; i++) {
        Item args[3] = { arr->items[i], (Item){.item = i2it(i)}, array };
        Item test_result = js_call_function(predicate, (Item){.item = ITEM_NULL}, args, 3);
        
        if (js_is_truthy(test_result)) {
            // TODO: Expand array if necessary
            if (result_index < result_arr->length) {
                result_arr->items[result_index] = arr->items[i];
            }
            result_index++;
        }
    }
    
    return result;
}

Item js_array_reduce(Item array, Item reducer, Item initial_value) {
    if (item_type(array) != LMD_TYPE_ARRAY || item_type(reducer) != LMD_TYPE_FUNC) {
        return (Item){.item = ITEM_NULL};
    }
    
    Array* arr = array.array;
    Item accumulator = initial_value;
    int start_index = 0;
    
    // If no initial value provided, use first element
    if (initial_value.item == ITEM_NULL && arr->length > 0) {
        accumulator = arr->items[0];
        start_index = 1;
    }
    
    for (int i = start_index; i < arr->length; i++) {
        Item args[4] = { accumulator, arr->items[i], (Item){.item = i2it(i)}, array };
        accumulator = js_call_function(reducer, (Item){.item = ITEM_NULL}, args, 4);
    }
    
    return accumulator;
}

Item js_array_foreach(Item array, Item callback) {
    if (item_type(array) != LMD_TYPE_ARRAY || item_type(callback) != LMD_TYPE_FUNC) {
        return (Item){.item = ITEM_NULL};
    }
    
    Array* arr = array.array;
    
    for (int i = 0; i < arr->length; i++) {
        Item args[3] = { arr->items[i], (Item){.item = i2it(i)}, array };
        js_call_function(callback, (Item){.item = ITEM_NULL}, args, 3);
    }
    
    return (Item){.item = ITEM_NULL}; // forEach returns undefined
}

void js_init_global_object() {
    if (js_global_object.item == ITEM_NULL) {
        js_global_object = js_new_object();
        
        // Add global properties
        js_property_set(js_global_object, (Item){.item = s2it("undefined")}, (Item){.item = ITEM_NULL});
        js_property_set(js_global_object, (Item){.item = s2it("NaN")}, (Item){.item = d2it(NAN)});
        js_property_set(js_global_object, (Item){.item = s2it("Infinity")}, (Item){.item = d2it(INFINITY)});
        
        // Add global functions
        js_property_set(js_global_object, (Item){.item = s2it("parseInt")}, 
                       js_new_function((void*)js_parse_int, 1));
        js_property_set(js_global_object, (Item){.item = s2it("parseFloat")}, 
                       js_new_function((void*)js_parse_float, 1));
        js_property_set(js_global_object, (Item){.item = s2it("isNaN")}, 
                       js_new_function((void*)js_is_nan, 1));
        js_property_set(js_global_object, (Item){.item = s2it("isFinite")}, 
                       js_new_function((void*)js_is_finite, 1));
    }
}

// Global utility functions
Item js_parse_int(Item value) {
    Item str = js_to_string(value);
    String* s = it2s(str);
    
    // Simple integer parsing
    char* endptr;
    long result = strtol(s->chars, &endptr, 10);
    
    if (endptr == s->chars) {
        return (Item){.item = d2it(NAN)}; // No valid conversion
    }
    
    return (Item){.item = i2it((int)result)};
}

Item js_parse_float(Item value) {
    Item str = js_to_string(value);
    String* s = it2s(str);
    
    char* endptr;
    double result = strtod(s->chars, &endptr);
    
    if (endptr == s->chars) {
        return (Item){.item = d2it(NAN)}; // No valid conversion
    }
    
    return (Item){.item = d2it(result)};
}

Item js_is_nan(Item value) {
    Item num = js_to_number(value);
    return (Item){.item = b2it(isnan(it2d(num)))};
}

Item js_is_finite(Item value) {
    Item num = js_to_number(value);
    double d = it2d(num);
    return (Item){.item = b2it(!isnan(d) && !isinf(d))};
}
