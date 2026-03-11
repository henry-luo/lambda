/**
 * JavaScript Runtime Functions for Lambda
 * 
 * These functions implement JavaScript semantics and are callable from MIR JIT code.
 * All functions use Item (uint64_t) as the primary data type.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

// =============================================================================
// Type Conversion Functions
// =============================================================================

/**
 * Convert a JavaScript value to a primitive.
 * Follows ECMAScript ToPrimitive algorithm.
 */
Item js_to_number(Item value);
Item js_to_string(Item value);
Item js_to_boolean(Item value);

/**
 * Check if a value is truthy according to JavaScript rules.
 */
bool js_is_truthy(Item value);

// =============================================================================
// Arithmetic Operators
// =============================================================================

Item js_add(Item left, Item right);       // + (string concat or numeric)
Item js_subtract(Item left, Item right);  // -
Item js_multiply(Item left, Item right);  // *
Item js_divide(Item left, Item right);    // /
Item js_modulo(Item left, Item right);    // %
Item js_power(Item left, Item right);     // **

// =============================================================================
// Comparison Operators
// =============================================================================

Item js_equal(Item left, Item right);           // == (with coercion)
Item js_not_equal(Item left, Item right);       // !=
Item js_strict_equal(Item left, Item right);    // ===
Item js_strict_not_equal(Item left, Item right); // !==
Item js_less_than(Item left, Item right);       // <
Item js_less_equal(Item left, Item right);      // <=
Item js_greater_than(Item left, Item right);    // >
Item js_greater_equal(Item left, Item right);   // >=

// =============================================================================
// Logical Operators
// =============================================================================

Item js_logical_and(Item left, Item right);  // && (returns last evaluated operand)
Item js_logical_or(Item left, Item right);   // || (returns last evaluated operand)
Item js_logical_not(Item operand);           // !

// =============================================================================
// Bitwise Operators
// =============================================================================

Item js_bitwise_and(Item left, Item right);      // &
Item js_bitwise_or(Item left, Item right);       // |
Item js_bitwise_xor(Item left, Item right);      // ^
Item js_bitwise_not(Item operand);               // ~
int64_t js_double_to_int32(double d);            // ToInt32 (safe for Infinity/NaN)
Item js_left_shift(Item left, Item right);       // <<
Item js_right_shift(Item left, Item right);      // >>
Item js_unsigned_right_shift(Item left, Item right); // >>>

// =============================================================================
// Unary Operators
// =============================================================================

Item js_unary_plus(Item operand);   // +x (convert to number)
Item js_unary_minus(Item operand);  // -x (negate)
Item js_typeof(Item value);         // typeof x

// =============================================================================
// Object Functions
// =============================================================================

Item js_new_object(void);
Item js_property_get(Item object, Item key);
Item js_property_set(Item object, Item key, Item value);
Item js_property_access(Item object, Item key);

// =============================================================================
// Array Functions
// =============================================================================

Item js_array_new(int length);
Item js_array_new_from_item(Item arg);
Item js_array_get(Item array, Item index);
Item js_array_set(Item array, Item index, Item value);
int64_t js_array_length(Item array);
Item js_array_push(Item array, Item value);
int64_t js_get_length(Item object);

// =============================================================================
// Function Functions
// =============================================================================

Item js_new_function(void* func_ptr, int param_count);
Item js_new_closure(void* func_ptr, int param_count, Item* env, int env_size);
Item* js_alloc_env(int count);
Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count);
Item js_apply_function(Item func_item, Item this_val, Item args_array);
Item js_debug_check_callee(Item callee, int64_t site_id);
Item js_get_this();
void js_set_this(Item this_val);

// =============================================================================
// Console Functions
// =============================================================================

void js_console_log(Item value);

// =============================================================================
// String Method Dispatcher (delegates to Lambda fn_* functions)
// =============================================================================

Item js_string_method(Item str, Item method_name, Item* args, int argc);

// =============================================================================
// Array Method Dispatcher (map, filter, reduce, forEach, find, etc.)
// =============================================================================

Item js_array_method(Item arr, Item method_name, Item* args, int argc);

// =============================================================================
// Math Object Methods & Properties
// =============================================================================

Item js_math_method(Item method_name, Item* args, int argc);
Item js_math_property(Item prop_name);

// =============================================================================
// v5: Process I/O
// =============================================================================

Item js_process_stdout_write(Item str_item);
Item js_process_hrtime_bigint(void);
void js_set_process_argv(int argc, const char** argv);
Item js_get_process_argv(void);

// =============================================================================
// v5: Global Functions
// =============================================================================

Item js_parseInt(Item str_item, Item radix_item);
Item js_parseFloat(Item str_item);
Item js_isNaN(Item value);
Item js_isFinite(Item value);

// =============================================================================
// v5: Number Methods
// =============================================================================

Item js_toFixed(Item num_item, Item digits_item);
Item js_number_method(Item num, Item method_name, Item* args, int argc);

// =============================================================================
// v5: String Methods (charCodeAt, fromCharCode)
// =============================================================================

Item js_string_charCodeAt(Item str_item, Item index_item);
Item js_string_fromCharCode(Item code_item);

// =============================================================================
// v5: Array fill (regular + typed)
// =============================================================================

Item js_array_fill(Item arr_item, Item value);
Item js_array_slice_from(Item arr, Item start_item);

// =============================================================================
// v5: Console multi-argument log
// =============================================================================

void js_console_log_multi(Item* args, int argc);

// =============================================================================
// v5: Additional binary operators
// =============================================================================

Item js_instanceof(Item left, Item right);
Item js_in(Item key, Item object);
Item js_nullish_coalesce(Item left, Item right);

// =============================================================================
// v5: Object utilities
// =============================================================================

Item js_object_keys(Item object);
Item js_to_string_val(Item value);
Item js_number_property(Item prop_name);

// =============================================================================
// v8: Object & Global extensions
// =============================================================================

Item js_object_create(Item proto);
Item js_object_define_property(Item obj, Item name, Item descriptor);
Item js_array_is_array(Item value);
Item js_performance_now(void);
Item js_date_now(void);
Item js_date_new(void);
Item js_alert(Item msg);
void js_set_prototype(Item object, Item prototype);
Item js_get_prototype(Item object);

// =============================================================================
// Exception Handling (try/catch/throw)
// =============================================================================

/**
 * Set the exception flag and store the thrown value.
 * In the same try block, throw jumps directly to catch.
 * In a called function, throw sets the flag and returns; the caller checks.
 */
void js_throw_value(Item value);

/**
 * Check if an exception is currently pending.
 * Returns 1 if pending, 0 otherwise.
 */
int js_check_exception(void);

/**
 * Clear the pending exception and return the thrown value.
 * Called at the start of a catch block.
 */
Item js_clear_exception(void);

/**
 * Create a new Error object with a message.
 * Returns a Map with {name: "Error", message: msg}.
 */
Item js_new_error(Item message);

// =============================================================================
// Runtime Context
// =============================================================================

/**
 * Set the Input context for JS runtime map_put operations.
 * Called during JS execution setup. Takes void* for C compatibility (actually Input*).
 */
void js_runtime_set_input(void* input);

// =============================================================================
// Module Variable Table
// =============================================================================

void js_set_module_var(int index, Item value);
Item js_get_module_var(int index);
void js_reset_module_vars(void);
Item js_constructor_create_object(Item callee);

#ifdef __cplusplus
}
#endif
