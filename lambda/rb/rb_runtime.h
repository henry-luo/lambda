// rb_runtime.h — Ruby runtime function declarations for MIR JIT
// All functions use extern "C" linkage for MIR import resolution.
#pragma once

#include "../lambda.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Type conversion
// ============================================================================
Item rb_to_int(Item value);
Item rb_to_float(Item value);
Item rb_to_str(Item value);
Item rb_to_bool(Item value);
int rb_is_truthy(Item value);          // Ruby truthiness: only false and nil are falsy

// ============================================================================
// Arithmetic operators
// ============================================================================
Item rb_add(Item left, Item right);     // + (also string concat, array concat)
Item rb_subtract(Item left, Item right);
Item rb_multiply(Item left, Item right); // * (also string repetition)
Item rb_divide(Item left, Item right);   // / (integer division for int/int)
Item rb_modulo(Item left, Item right);   // %
Item rb_power(Item left, Item right);    // **
Item rb_negate(Item value);             // unary -
Item rb_positive(Item value);           // unary +
Item rb_bit_not(Item value);            // ~

// ============================================================================
// Bitwise operators
// ============================================================================
Item rb_bit_and(Item left, Item right);
Item rb_bit_or(Item left, Item right);
Item rb_bit_xor(Item left, Item right);
Item rb_lshift(Item left, Item right);
Item rb_rshift(Item left, Item right);

// ============================================================================
// Comparison operators
// ============================================================================
Item rb_eq(Item left, Item right);      // ==
Item rb_ne(Item left, Item right);      // !=
Item rb_lt(Item left, Item right);      // <
Item rb_le(Item left, Item right);      // <=
Item rb_gt(Item left, Item right);      // >
Item rb_ge(Item left, Item right);      // >=
Item rb_cmp(Item left, Item right);     // <=> spaceship
Item rb_case_eq(Item left, Item right); // === case equality

// ============================================================================
// Object/attribute operations
// ============================================================================
Item rb_getattr(Item object, Item name);
void rb_setattr(Item object, Item name, Item value);
Item rb_new_object(void);               // create empty hash-map object

// ============================================================================
// Collection operations
// ============================================================================
Item rb_array_new(void);
Item rb_array_push(Item array, Item value);
Item rb_array_get(Item array, Item index);
Item rb_array_set(Item array, Item index, Item value);
Item rb_array_length(Item array);
Item rb_array_pop(Item array);

Item rb_hash_new(void);
Item rb_hash_get(Item hash, Item key);
Item rb_hash_set(Item hash, Item key, Item value);

Item rb_range_new(Item start, Item end, int exclusive);

// ============================================================================
// Subscript operations
// ============================================================================
Item rb_subscript_get(Item object, Item key);
Item rb_subscript_set(Item object, Item key, Item value);

// ============================================================================
// String operations
// ============================================================================
Item rb_string_concat(Item left, Item right);
Item rb_string_repeat(Item str, Item count);
Item rb_format_value(Item value, Item format_spec);

// ============================================================================
// Iterator protocol
// ============================================================================
Item rb_get_iterator(Item collection);
Item rb_iterator_next(Item iterator);
int rb_is_stop_iteration(Item value);

// ============================================================================
// Module variables (top-level script variables)
// ============================================================================
void rb_set_module_var(int index, Item value);
Item rb_get_module_var(int index);
void rb_reset_module_vars(void);

// ============================================================================
// Output
// ============================================================================
Item rb_puts(Item* args, int argc);
Item rb_print(Item* args, int argc);
Item rb_p(Item* args, int argc);

// MIR-friendly single-arg wrappers
Item rb_puts_one(Item value);
Item rb_print_one(Item value);
Item rb_p_one(Item value);

// Aliases matching Ruby method convention
Item rb_to_s(Item value);
Item rb_to_i(Item value);
Item rb_to_f(Item value);

// ============================================================================
// Built-in functions (Kernel methods)
// ============================================================================
Item rb_builtin_len(Item obj);
Item rb_builtin_type(Item obj);
Item rb_builtin_rand(Item max);
Item rb_builtin_require_relative(Item path);

// ============================================================================
// Built-in method dispatchers (Phase 3)
// Returns ITEM_ERROR when method not found (sentinel for dispatch chain).
// ============================================================================
Item rb_string_method(Item self, Item method_name, Item* args, int argc);
Item rb_array_method(Item self, Item method_name, Item* args, int argc);
Item rb_hash_method(Item self, Item method_name, Item* args, int argc);
Item rb_int_method(Item self, Item method_name, Item* args, int argc);
Item rb_float_method(Item self, Item method_name, Item* args, int argc);

// ============================================================================
// Runtime initialization
// ============================================================================
void rb_runtime_set_input(void* input_ptr);

// ============================================================================
// Class system (Phase 2)
// ============================================================================
Item rb_class_create(Item name, Item superclass);
void rb_class_add_method(Item cls, Item method_name, Item func);
Item rb_class_new_instance(Item cls);
int rb_is_class(Item obj);
int rb_is_instance(Item obj);
Item rb_get_class(Item obj);
Item rb_instance_getattr(Item instance, Item name);
void rb_instance_setattr(Item instance, Item name, Item value);
Item rb_method_lookup(Item receiver, Item method_name);
Item rb_super_lookup(Item cls, Item method_name);
void rb_attr_reader(Item cls, Item attr_name);
void rb_attr_writer(Item cls, Item attr_name);
void rb_attr_accessor(Item cls, Item attr_name);

// ============================================================================
// Block / Proc / Lambda calling
// ============================================================================
Item rb_block_call(Item block, Item* args, int argc);
Item rb_block_call_1(Item block, Item arg);
Item rb_block_call_2(Item block, Item arg1, Item arg2);

// ============================================================================
// Iterator methods (block-based)
// ============================================================================
Item rb_array_each(Item array, Item block);
Item rb_array_map(Item array, Item block);
Item rb_array_select(Item array, Item block);
Item rb_array_reject(Item array, Item block);
Item rb_array_reduce(Item array, Item initial, Item block);
Item rb_array_each_with_index(Item array, Item block);
Item rb_array_any(Item array, Item block);
Item rb_array_all(Item array, Item block);
Item rb_array_find(Item array, Item block);
Item rb_int_times(Item n, Item block);
Item rb_int_upto(Item n, Item m, Item block);
Item rb_int_downto(Item n, Item m, Item block);

#ifdef __cplusplus
}
#endif
