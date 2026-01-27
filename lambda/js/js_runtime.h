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
// Array Functions
// =============================================================================

Item js_array_new(int length);
Item js_array_get(Item array, Item index);
Item js_array_set(Item array, Item index, Item value);
int js_array_length(Item array);
Item js_array_push(Item array, Item value);

// =============================================================================
// Console Functions
// =============================================================================

void js_console_log(Item value);

#ifdef __cplusplus
}
#endif
