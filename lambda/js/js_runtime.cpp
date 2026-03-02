/**
 * JavaScript Runtime Functions for Lambda v2
 *
 * Implements JavaScript semantics on top of Lambda's type system.
 * All functions are callable from MIR JIT compiled code.
 */
#include "js_runtime.h"
#include "js_dom.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/hashmap.h"
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
    if (d == (double)(int64_t)d && d >= INT56_MIN && d <= INT56_MAX) {
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

    // Numeric addition — delegate to Lambda fn_add after coercion
    return fn_add(js_to_number(left), js_to_number(right));
}

extern "C" Item js_subtract(Item left, Item right) {
    return fn_sub(js_to_number(left), js_to_number(right));
}

extern "C" Item js_multiply(Item left, Item right) {
    return fn_mul(js_to_number(left), js_to_number(right));
}

extern "C" Item js_divide(Item left, Item right) {
    return fn_div(js_to_number(left), js_to_number(right));
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
// Object Functions
// =============================================================================

// Structure for JS object entries (key-value pairs)
struct JsObjectEntry {
    String* key;
    Item value;
};

// Hash function for JS object entries
static uint64_t js_object_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const JsObjectEntry* entry = (const JsObjectEntry*)item;
    return hashmap_sip(entry->key->chars, entry->key->len, seed0, seed1);
}

// Compare function for JS object entries
static int js_object_compare(const void *a, const void *b, void *udata) {
    const JsObjectEntry* ea = (const JsObjectEntry*)a;
    const JsObjectEntry* eb = (const JsObjectEntry*)b;
    if (ea->key->len != eb->key->len) return 1;
    return strncmp(ea->key->chars, eb->key->chars, ea->key->len);
}

// Create a new JS object using hashmap
extern "C" Item js_new_object() {
    // Create a hashmap to store object properties
    HashMap* obj = hashmap_new(sizeof(JsObjectEntry), 4, 0, 0,
        js_object_hash, js_object_compare, NULL, NULL);
    // Store as pointer with LMD_TYPE_MAP type (we'll use type_id to differentiate)
    // Actually we need a way to store this - let's use a wrapper struct
    // For now, we'll allocate a Map-like structure and store the hashmap pointer
    Map* wrapper = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    wrapper->type_id = LMD_TYPE_MAP;
    wrapper->data = (void*)obj;  // Store hashmap in data field
    wrapper->type = NULL;  // NULL type indicates JS object
    return (Item){.map = wrapper};
}

extern "C" Item js_property_get(Item object, Item key) {
    TypeId type = get_type_id(object);

    if (type == LMD_TYPE_MAP) {
        Map* m = object.map;
        // Check if this is a DOM node wrapper (indicated by js_dom_type_marker)
        if (js_is_dom_node(object)) {
            return js_dom_get_property(object, key);
        }
        // Check if this is a computed style wrapper
        if (js_is_computed_style_item(object)) {
            return js_computed_style_get_property(object, key);
        }
        // Check if this is a JS object (indicated by NULL type)
        if (m->type == NULL && m->data != NULL) {
            // This is a JS object using hashmap
            HashMap* hm = (HashMap*)m->data;
            String* str_key = NULL;

            TypeId key_type = get_type_id(key);
            if (key_type == LMD_TYPE_STRING) {
                str_key = it2s(key);
            } else if (key_type == LMD_TYPE_SYMBOL) {
                str_key = it2s(key);
            } else {
                // Convert to string for property access
                return ItemNull;
            }

            JsObjectEntry lookup = {.key = str_key, .value = ItemNull};
            const JsObjectEntry* found = (const JsObjectEntry*)hashmap_get(hm, &lookup);
            if (found) {
                return found->value;
            }
            return ItemNull;
        }
        // Regular Lambda map
        return map_get(object.map, key);
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
        }
        // Numeric index access
        int idx = (int)js_get_number(key);
        if (idx >= 0 && idx < object.array->length) {
            return object.array->items[idx];
        }
        return ItemNull;
    }

    return ItemNull;
}

extern "C" Item js_property_set(Item object, Item key, Item value) {
    TypeId type = get_type_id(object);

    if (type == LMD_TYPE_MAP) {
        Map* m = object.map;
        // Check if this is a DOM node wrapper (indicated by js_dom_type_marker)
        if (js_is_dom_node(object)) {
            return js_dom_set_property(object, key, value);
        }
        // Check if this is a JS object (indicated by NULL type)
        if (m->type == NULL && m->data != NULL) {
            // This is a JS object using hashmap
            HashMap* hm = (HashMap*)m->data;
            String* str_key = NULL;

            TypeId key_type = get_type_id(key);
            if (key_type == LMD_TYPE_STRING) {
                str_key = it2s(key);
            } else if (key_type == LMD_TYPE_SYMBOL) {
                str_key = it2s(key);
            } else {
                // Can't set non-string key
                return value;
            }

            JsObjectEntry entry = {.key = str_key, .value = value};
            hashmap_set(hm, &entry);
            return value;
        }
        // Regular Lambda map - not supported for set
        log_debug("js_property_set: Setting property on Lambda map (not supported)");
        return value;
    }

    return value;
}

extern "C" Item js_property_access(Item object, Item key) {
    // Same as js_property_get but used for member expressions
    return js_property_get(object, key);
}

// =============================================================================
// Array Functions
// =============================================================================

// Helper to make JS undefined value
static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

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

// =============================================================================
// Function Functions
// =============================================================================

// Structure to wrap JS function pointers
struct JsFunction {
    TypeId type_id;  // Always LMD_TYPE_FUNC
    void* func_ptr;  // Pointer to the compiled function
    int param_count; // Number of parameters
};

extern "C" Item js_new_function(void* func_ptr, int param_count) {
    JsFunction* fn = (JsFunction*)heap_alloc(sizeof(JsFunction), LMD_TYPE_FUNC);
    fn->type_id = LMD_TYPE_FUNC;
    fn->func_ptr = func_ptr;
    fn->param_count = param_count;
    return (Item){.function = (Function*)fn};
}

// Call a JavaScript function stored as an Item
extern "C" Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count) {
    if (get_type_id(func_item) != LMD_TYPE_FUNC) {
        log_error("js_call_function: not a function");
        return ItemNull;
    }

    JsFunction* fn = (JsFunction*)func_item.function;
    if (!fn || !fn->func_ptr) {
        log_error("js_call_function: null function pointer");
        return ItemNull;
    }

    // Cast function pointer and call based on argument count
    // Note: This is a simplified version that handles common cases
    typedef Item (*FnPtr0)();
    typedef Item (*FnPtr1)(Item);
    typedef Item (*FnPtr2)(Item, Item);
    typedef Item (*FnPtr3)(Item, Item, Item);
    typedef Item (*FnPtr4)(Item, Item, Item, Item);

    switch (arg_count) {
        case 0:
            return ((FnPtr0)fn->func_ptr)();
        case 1:
            return ((FnPtr1)fn->func_ptr)(args ? args[0] : ItemNull);
        case 2:
            return ((FnPtr2)fn->func_ptr)(
                args ? args[0] : ItemNull,
                args && arg_count > 1 ? args[1] : ItemNull
            );
        case 3:
            return ((FnPtr3)fn->func_ptr)(
                args ? args[0] : ItemNull,
                args && arg_count > 1 ? args[1] : ItemNull,
                args && arg_count > 2 ? args[2] : ItemNull
            );
        case 4:
            return ((FnPtr4)fn->func_ptr)(
                args ? args[0] : ItemNull,
                args && arg_count > 1 ? args[1] : ItemNull,
                args && arg_count > 2 ? args[2] : ItemNull,
                args && arg_count > 3 ? args[3] : ItemNull
            );
        default:
            log_error("js_call_function: too many arguments (%d)", arg_count);
            return ItemNull;
    }
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
        return (Item){.item = i2it(fn_index_of(str, args[0]))};
    }
    if (method->len == 11 && strncmp(method->chars, "lastIndexOf", 11) == 0) {
        if (argc < 1) return (Item){.item = i2it(-1)};
        return (Item){.item = i2it(fn_last_index_of(str, args[0]))};
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
        return fn_replace(str, args[0], args[1]);
    }
    if (method->len == 6 && strncmp(method->chars, "charAt", 6) == 0) {
        if (argc < 1) return (Item){.item = s2it(heap_create_name(""))};
        return fn_index(str, args[0]);
    }
    if (method->len == 10 && strncmp(method->chars, "charCodeAt", 10) == 0) {
        if (argc < 1) return ItemNull;
        String* s = it2s(str);
        if (!s || s->len == 0) return ItemNull;
        int idx = (int)js_get_number(args[0]);
        if (idx < 0 || idx >= (int)s->len) return ItemNull;
        // return the char code (byte value for ASCII/Latin1, first byte for UTF-8)
        unsigned char ch = (unsigned char)s->chars[idx];
        return (Item){.item = i2it((int64_t)ch)};
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
            list_push(arr.array, args[i]);
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
        for (int i = 0; i < a->length; i++) {
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
    // reverse - returns new reversed array (keeping as Array type)
    if (method->len == 7 && strncmp(method->chars, "reverse", 7) == 0) {
        if (arr_type != LMD_TYPE_ARRAY) return arr;
        Array* src = arr.array;
        Item result = js_array_new(src->length);
        Array* dst = result.array;
        for (int i = 0; i < src->length; i++) {
            dst->items[i] = src->items[src->length - 1 - i];
        }
        dst->length = src->length;
        return result;
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
        Item result = js_array_new(0);
        Array* dst = result.array;
        for (int i = start; i < end; i++) {
            array_push(dst, src->items[i]);
        }
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
        Item result = js_array_new(0);
        Array* dst = result.array;
        for (int i = 0; i < src->length; i++) {
            array_push(dst, src->items[i]);
        }
        for (int i = 0; i < argc; i++) {
            if (get_type_id(args[i]) == LMD_TYPE_ARRAY) {
                Array* other = args[i].array;
                for (int j = 0; j < other->length; j++) {
                    array_push(dst, other->items[j]);
                }
            } else {
                array_push(dst, args[i]);
            }
        }
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
        for (int i = 0; i < src->length; i++) {
            Item idx = (Item){.item = i2it(i)};
            Item mapped;
            JsFunction* fn = (JsFunction*)callback.function;
            if (fn->param_count >= 2) {
                mapped = ((Item (*)(Item, Item))fn->func_ptr)(src->items[i], idx);
            } else {
                mapped = ((Item (*)(Item))fn->func_ptr)(src->items[i]);
            }
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
        for (int i = 0; i < src->length; i++) {
            Item idx = (Item){.item = i2it(i)};
            Item pred;
            JsFunction* fn = (JsFunction*)callback.function;
            if (fn->param_count >= 2) {
                pred = ((Item (*)(Item, Item))fn->func_ptr)(src->items[i], idx);
            } else {
                pred = ((Item (*)(Item))fn->func_ptr)(src->items[i]);
            }
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
            Item idx = (Item){.item = i2it(i)};
            if (fn->param_count >= 3) {
                accumulator = ((Item (*)(Item, Item, Item))fn->func_ptr)(accumulator, src->items[i], idx);
            } else {
                accumulator = ((Item (*)(Item, Item))fn->func_ptr)(accumulator, src->items[i]);
            }
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
            Item idx = (Item){.item = i2it(i)};
            if (fn->param_count >= 2) {
                ((Item (*)(Item, Item))fn->func_ptr)(src->items[i], idx);
            } else {
                ((Item (*)(Item))fn->func_ptr)(src->items[i]);
            }
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
            Item idx = (Item){.item = i2it(i)};
            Item pred;
            if (fn->param_count >= 2) {
                pred = ((Item (*)(Item, Item))fn->func_ptr)(src->items[i], idx);
            } else {
                pred = ((Item (*)(Item))fn->func_ptr)(src->items[i]);
            }
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
            Item idx = (Item){.item = i2it(i)};
            Item pred;
            if (fn->param_count >= 2) {
                pred = ((Item (*)(Item, Item))fn->func_ptr)(src->items[i], idx);
            } else {
                pred = ((Item (*)(Item))fn->func_ptr)(src->items[i]);
            }
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
            Item pred;
            if (fn->param_count >= 2) {
                pred = ((Item (*)(Item, Item))fn->func_ptr)(src->items[i], (Item){.item = i2it(i)});
            } else {
                pred = ((Item (*)(Item))fn->func_ptr)(src->items[i]);
            }
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
            Item pred;
            if (fn->param_count >= 2) {
                pred = ((Item (*)(Item, Item))fn->func_ptr)(src->items[i], (Item){.item = i2it(i)});
            } else {
                pred = ((Item (*)(Item))fn->func_ptr)(src->items[i]);
            }
            if (!js_is_truthy(pred)) return (Item){.item = b2it(false)};
        }
        return (Item){.item = b2it(true)};
    }
    // sort
    if (method->len == 4 && strncmp(method->chars, "sort", 4) == 0) {
        return fn_sort1(arr);
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

    log_debug("js_array_method: unknown method '%.*s'", (int)method->len, method->chars);
    return ItemNull;
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
        return fn_sqrt(js_to_number(args[0]));
    }
    // Math.pow
    if (method->len == 3 && strncmp(method->chars, "pow", 3) == 0) {
        if (argc < 2) return ItemNull;
        return fn_pow(js_to_number(args[0]), js_to_number(args[1]));
    }
    // Math.min
    if (method->len == 3 && strncmp(method->chars, "min", 3) == 0) {
        if (argc < 2) return ItemNull;
        return fn_min2(js_to_number(args[0]), js_to_number(args[1]));
    }
    // Math.max
    if (method->len == 3 && strncmp(method->chars, "max", 3) == 0) {
        if (argc < 2) return ItemNull;
        return fn_max2(js_to_number(args[0]), js_to_number(args[1]));
    }
    // Math.log
    if (method->len == 3 && strncmp(method->chars, "log", 3) == 0) {
        if (argc < 1) return ItemNull;
        return fn_log(js_to_number(args[0]));
    }
    // Math.log10
    if (method->len == 5 && strncmp(method->chars, "log10", 5) == 0) {
        if (argc < 1) return ItemNull;
        return fn_log10(js_to_number(args[0]));
    }
    // Math.exp
    if (method->len == 3 && strncmp(method->chars, "exp", 3) == 0) {
        if (argc < 1) return ItemNull;
        return fn_exp(js_to_number(args[0]));
    }
    // Math.sin
    if (method->len == 3 && strncmp(method->chars, "sin", 3) == 0) {
        if (argc < 1) return ItemNull;
        return fn_sin(js_to_number(args[0]));
    }
    // Math.cos
    if (method->len == 3 && strncmp(method->chars, "cos", 3) == 0) {
        if (argc < 1) return ItemNull;
        return fn_cos(js_to_number(args[0]));
    }
    // Math.tan
    if (method->len == 3 && strncmp(method->chars, "tan", 3) == 0) {
        if (argc < 1) return ItemNull;
        return fn_tan(js_to_number(args[0]));
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

    log_debug("js_math_method: unknown method '%.*s'", (int)method->len, method->chars);
    return ItemNull;
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
