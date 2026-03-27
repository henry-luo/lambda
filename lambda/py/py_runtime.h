#pragma once

// py_runtime.h — C API for Python runtime functions callable from JIT code
// All functions take and return Lambda Item values (64-bit tagged).

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

// ========================================================================
// Type conversion
// ========================================================================
Item py_to_int(Item value);
Item py_to_float(Item value);
Item py_to_str(Item value);
Item py_to_bool(Item value);
bool py_is_truthy(Item value);

// ========================================================================
// Arithmetic operators
// ========================================================================
Item py_add(Item left, Item right);
Item py_subtract(Item left, Item right);
Item py_multiply(Item left, Item right);
Item py_divide(Item left, Item right);       // true division (always float)
Item py_floor_divide(Item left, Item right); // //
Item py_modulo(Item left, Item right);
Item py_power(Item left, Item right);
Item py_negate(Item operand);
Item py_positive(Item operand);
Item py_bit_not(Item operand);

// ========================================================================
// Bitwise operators
// ========================================================================
Item py_bit_and(Item left, Item right);
Item py_bit_or(Item left, Item right);
Item py_bit_xor(Item left, Item right);
Item py_lshift(Item left, Item right);
Item py_rshift(Item left, Item right);

// ========================================================================
// Comparison operators
// ========================================================================
Item py_eq(Item left, Item right);
Item py_ne(Item left, Item right);
Item py_lt(Item left, Item right);
Item py_le(Item left, Item right);
Item py_gt(Item left, Item right);
Item py_ge(Item left, Item right);
Item py_is(Item left, Item right);
Item py_is_not(Item left, Item right);
Item py_contains(Item container, Item value);  // `in` operator
Item py_match_is_sequence(Item obj);
Item py_match_is_mapping(Item obj);
Item py_match_mapping_rest(Item obj, Item excluded_keys);

// ========================================================================
// Object/attribute operations
// ========================================================================
Item py_getattr(Item object, Item name);
Item py_setattr(Item object, Item name, Item value);
Item py_hasattr(Item object, Item name);
Item py_new_object(void);

// ========================================================================
// Collection operations
// ========================================================================
Item py_list_new(int length);
Item py_list_append(Item list, Item value);
Item py_list_get(Item list, Item index);
Item py_list_set(Item list, Item index, Item value);
int64_t py_list_length(Item list);

Item py_dict_new(void);
Item py_dict_get(Item dict, Item key);
Item py_dict_set(Item dict, Item key, Item value);

Item py_tuple_new(int length);
Item py_tuple_set(Item tuple, int index, Item value);

// ========================================================================
// Subscript and slice
// ========================================================================
Item py_subscript_get(Item object, Item key);
Item py_subscript_set(Item object, Item key, Item value);
Item py_slice_get(Item object, Item start, Item stop, Item step);
Item py_slice_set(Item object, Item start, Item stop, Item step, Item value);

// ========================================================================
// Format value with spec (used by f-strings and str.format)
// ========================================================================
Item py_format_value(Item value, Item spec);
Item py_exception_get_type(Item exception);

// ========================================================================
// File I/O
// ========================================================================
Item py_builtin_open(Item path, Item mode);

// ========================================================================
// Variadic args support
// ========================================================================
Item py_build_list_from_args(Item* args, int64_t count);

// ========================================================================
// Iterator protocol
// ========================================================================
Item py_get_iterator(Item iterable);
Item py_iterator_next(Item iterator);
Item py_range_new(Item start, Item stop, Item step);

// ========================================================================
// Function/closure
// ========================================================================
Item py_new_function(void* func_ptr, int param_count);
Item py_new_closure(void* func_ptr, int param_count, uint64_t* env, int env_size);
uint64_t* py_alloc_env(int size);
Item py_set_kwargs_flag(Item fn_item);
Item py_dict_merge(Item dst, Item src);
Item py_call_function(Item func, Item* args, int arg_count);
Item py_call_function_kw(Item func, Item* args, int arg_count, Item kwargs_map);

// ========================================================================
// Exception handling
// ========================================================================
void py_raise(Item exception);
Item py_check_exception(void);
Item py_clear_exception(void);
Item py_new_exception(Item type_name, Item message);

// ========================================================================
// Context manager protocol (__enter__ / __exit__)
// ========================================================================
Item py_context_enter(Item mgr);
Item py_context_exit(Item mgr, Item exc_type, Item exc_val, Item exc_tb);
// Identifier fallback: resolves builtin class names (ValueError, RuntimeError, etc.)
Item py_resolve_name_item(Item name_item);

// ========================================================================
// Module variable table
// ========================================================================
void py_set_module_var(int index, Item value);
Item py_get_module_var(int index);
void py_reset_module_vars(void);

// ========================================================================
// Built-in functions
// ========================================================================
Item py_print(Item* args, int argc);
Item py_print_ex(Item* args, int argc, Item sep, Item end);
Item py_builtin_len(Item obj);
Item py_builtin_type(Item obj);
Item py_builtin_isinstance(Item obj, Item classinfo);
Item py_builtin_range(Item* args, int argc);
Item py_builtin_int(Item value);
Item py_builtin_float(Item value);
Item py_builtin_str(Item value);
Item py_builtin_bool(Item value);
Item py_builtin_abs(Item value);
Item py_builtin_min(Item* args, int argc);
Item py_builtin_max(Item* args, int argc);
Item py_builtin_sum(Item iterable);
Item py_builtin_enumerate(Item iterable);
Item py_builtin_zip(Item* args, int argc);
Item py_builtin_sorted(Item iterable);
Item py_builtin_reversed(Item iterable);
Item py_builtin_repr(Item obj);
Item py_builtin_hash(Item obj);
Item py_builtin_id(Item obj);
Item py_builtin_input(Item prompt);
Item py_builtin_ord(Item ch);
Item py_builtin_chr(Item code);
Item py_builtin_map(Item func, Item iterable);
Item py_builtin_filter(Item func, Item iterable);
Item py_builtin_list(Item iterable);
Item py_builtin_dict(Item* args, int argc);
Item py_builtin_set(Item iterable);
Item py_builtin_tuple(Item iterable);
Item py_builtin_round(Item x, Item ndigits);
Item py_builtin_all(Item iterable);
Item py_builtin_any(Item iterable);
Item py_builtin_bin(Item n);
Item py_builtin_oct(Item n);
Item py_builtin_hex(Item n);
Item py_builtin_divmod(Item a, Item b);
Item py_builtin_pow(Item base, Item exp, Item mod);
Item py_builtin_callable(Item obj);
Item py_builtin_property(Item fget);
Item py_property_setter(Item prop, Item fset);
Item py_property_deleter(Item prop, Item fdel);
Item py_builtin_sorted_ex(Item iterable, Item key_func, Item reverse_flag);
Item py_list_sort_ex(Item list, Item key_func, Item reverse_flag);

// ========================================================================
// String methods (dispatcher)
// ========================================================================
Item py_string_method(Item str, Item method_name, Item* args, int argc);

// ========================================================================
// List methods (dispatcher)
// ========================================================================
Item py_list_method(Item list, Item method_name, Item* args, int argc);

// ========================================================================
// Dict methods (dispatcher)
// ========================================================================
Item py_dict_method(Item dict, Item method_name, Item* args, int argc);

// ========================================================================
// Class system — see py_class.h for full declarations.
// py_class.h is included separately when needed.
// ========================================================================

// ========================================================================
// Runtime initialization
// ========================================================================
void py_runtime_set_input(void* input);

// ========================================================================
// Stop iteration sentinel
// ========================================================================
Item py_stop_iteration(void);
bool py_is_stop_iteration(Item value);

// ========================================================================
// Generator protocol (Phase A)
// ========================================================================
Item    py_gen_create(void* resume_fn_ptr, int frame_size);
int64_t py_gen_get_frame_c(Item gen);
Item    py_gen_next(Item gen);
Item    py_gen_send(Item gen, Item value);
bool    py_is_generator(Item x);

#ifdef __cplusplus
}
#endif
