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

// Sentinel value for deleted properties (used by delete operator).
// Encoded as a tagged INT (LMD_TYPE_INT=3) with a unique payload 0x00DEAD00DEAD00.
// This roundtrips correctly through map_field_store/map_read_field for INT fields.
#define JS_DELETED_SENTINEL_VAL ((3ULL << 56) | 0x00DEAD00DEAD00ULL)

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
int64_t js_is_nullish(Item value);

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
Item js_array_get_int(Item array, int64_t index);
Item js_array_set_int(Item array, int64_t index, Item value);
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
Item js_bind_function(Item func_item, Item bound_this, Item* bound_args, int bound_argc);
Item js_func_bind(Item func_item, Item bound_this, Item* bound_args, int bound_argc);
Item js_new_function_from_string(Item* args, int argc);
Item js_create_regex(const char* pattern, int pattern_len, const char* flags, int flags_len);
Item js_regexp_construct(Item pattern_item, Item flags_item);
Item js_regex_test(Item regex, Item str);
Item js_regex_exec(Item regex, Item str);
Item js_debug_check_callee(Item callee, int64_t site_id);
Item js_get_this();
void js_set_this(Item this_val);

// Get the native function pointer from a JsFunction Item (handles JsFunction layout)
void* js_function_get_ptr(Item fn_item);

// Get the parameter count from a JsFunction Item
int js_function_get_arity(Item fn_item);

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
Item js_math_apply(Item method_name, Item args_array);
Item js_math_property(Item prop_name);
Item js_math_set_property(Item key, Item value);

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
Item js_string_fromCharCode_array(Item arr_item);
Item js_string_fromCodePoint(Item code_item);
Item js_string_fromCodePoint_array(Item arr_item);

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
Item js_instanceof_classname(Item left, Item classname);
Item js_in(Item key, Item object);
Item js_nullish_coalesce(Item left, Item right);

// =============================================================================
// v5: Object utilities
// =============================================================================

Item js_object_keys(Item object);
Item js_object_get_own_property_symbols(Item object);
Item js_to_string_val(Item value);
Item js_number_property(Item prop_name);
Item js_make_getter_key(Item key);
Item js_make_setter_key(Item key);

// =============================================================================
// v8: Object & Global extensions
// =============================================================================

Item js_object_create(Item proto);
Item js_object_define_property(Item obj, Item name, Item descriptor);
Item js_array_is_array(Item value);
Item js_performance_now(void);
Item js_date_now(void);
Item js_date_new(void);
Item js_date_new_from(Item value);
Item js_date_utc(Item args_array);
Item js_date_method(Item date_obj, int method_id);
Item js_map_collection_new(void);
Item js_map_collection_new_from(Item iterable);
Item js_set_collection_new(void);
Item js_set_collection_new_from(Item iterable);
Item js_collection_method(Item obj, int method_id, Item arg1, Item arg2);
Item js_map_method(Item obj, Item method_name, Item* args, int argc);
Item js_method_call_apply(Item obj, Item method_name, Item args_array);
Item js_alert(Item msg);
void js_set_prototype(Item object, Item prototype);
void js_link_base_prototype(Item proto_marker, Item base_ctor);
Item js_get_prototype(Item object);
Item js_get_prototype_of(Item object);
Item js_reflect_construct(Item target, Item args_array);
Item js_prototype_lookup(Item object, Item property);
Item js_map_get_fast_ext(Map* m, const char* key_str, int key_len, bool* out_found);

// =============================================================================
// v9: Object extensions
// =============================================================================

Item js_object_values(Item object);
Item js_object_entries(Item object);
Item js_object_from_entries(Item iterable);
Item js_object_is(Item left, Item right);
Item js_object_assign(Item target, Item* sources, int count);
Item js_object_spread_into(Item target, Item source);
Item js_has_own_property(Item obj, Item key);
Item js_object_freeze(Item obj);
Item js_object_is_frozen(Item obj);

// =============================================================================
// v9: Number static methods
// =============================================================================

Item js_number_is_integer(Item value);
Item js_number_is_finite(Item value);
Item js_number_is_nan(Item value);
Item js_number_is_safe_integer(Item value);

// =============================================================================
// v9: Array.from, JSON.parse/stringify, delete
// =============================================================================

Item js_array_from(Item iterable);
Item js_array_from_with_mapper(Item iterable, Item mapFn);
Item js_json_parse(Item str_item);
Item js_json_stringify(Item value);
Item js_delete_property(Item obj, Item key);

// v15: fetch() API
Item js_fetch(Item url, Item options);
void js_fetch_reset(void);

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
 * Returns a Map with {name: "Error", message: msg, stack: trace}.
 */
Item js_new_error(Item message);
Item js_new_error_with_stack(Item message, Item stack_str);

/**
 * v11: Create a typed Error object (TypeError, RangeError, etc.).
 * Returns a Map with {name: error_name, message: msg, stack: trace}.
 */
Item js_new_error_with_name(Item error_name, Item message);
Item js_new_error_with_name_stack(Item error_name, Item message, Item stack_str);

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
Item js_new_from_class_object(Item callee, Item* args, int argc);

// A5: Constructor shape pre-allocation
// Creates a new object with pre-built shape: all property slots pre-allocated
// and initialized to null (8-byte slots). Subsequent js_property_set calls for
// these properties will find existing keys and do fast in-place updates.
Item js_new_object_with_shape(const char** prop_names, const int* prop_lens, int count);
// Same as above but also sets __proto__ from callee.prototype
Item js_constructor_create_object_shaped(Item callee, const char** prop_names, const int* prop_lens, int count);

// P3/P4: Slot-indexed property access for shaped (constructor-created) objects.
// Avoids hash-table lookup by walking ShapeEntry chain to the N-th slot index.
// js_get_shaped_slot: reads and correctly boxes the typed field value.
// js_set_shaped_slot: writes with correct unboxing, updates ShapeEntry type.
Item js_get_shaped_slot(Item object, int64_t slot);
void js_set_shaped_slot(Item object, int64_t slot, Item value);

// =============================================================================
// v12: Language extensions
// =============================================================================

// Object rest destructuring: create object with all props except excluded keys
Item js_object_rest(Item src, Item* exclude_keys, int exclude_count);

// URI encoding/decoding
Item js_encodeURIComponent(Item str_item);
Item js_decodeURIComponent(Item str_item);
Item js_unescape(Item str_item);
Item js_escape(Item str_item);
Item js_atob(Item str_item);
Item js_btoa(Item str_item);

// globalThis
Item js_get_global_this(void);

// URL constructor
Item js_url_construct(Item input);
Item js_url_construct_with_base(Item input, Item base);
Item js_url_parse(Item input, Item base);
Item js_url_can_parse(Item input);
Item js_readable_stream_new(void);
Item js_writable_stream_new(void);

// Symbol API
// Symbol items are encoded as negative ints: -(id + JS_SYMBOL_BASE).
// Base must be beyond int32 range to avoid collision with bitwise op results.
#define JS_SYMBOL_BASE (1LL << 40)

Item js_symbol_create(Item description);
Item js_symbol_for(Item key);
Item js_symbol_key_for(Item sym);
Item js_symbol_to_string(Item sym);
Item js_symbol_well_known(Item name);

// =============================================================================
// v14: Generator Runtime
// =============================================================================

/**
 * Create a generator object from a state machine function pointer.
 * The func_ptr is the MIR-compiled generator body (state machine form).
 * env/env_size represent captured closure variables.
 */
Item js_generator_create(void* func_ptr, Item* env, int env_size);

/**
 * Advance the generator: execute next state, return {value, done} result.
 * input is the value passed to next() (ItemUndefined for first call).
 */
Item js_generator_next(Item generator, Item input);

/**
 * Force generator to return: set state to done, return {value, done:true}.
 */
Item js_generator_return(Item generator, Item value);

/**
 * Throw an error into the generator (at yield point).
 */
Item js_generator_throw(Item generator, Item error);

/**
 * v15: Create a 2-element array [value, next_state] for generator state machine returns.
 * Called from MIR-compiled generator state machine functions at each yield point.
 */
Item js_gen_yield_result(Item value, int64_t next_state);
Item js_gen_yield_delegate_result(Item iterable, int64_t resume_state);

/**
 * v15: Convert an iterable to an array. Drains generators, passes arrays through.
 */
Item js_iterable_to_array(Item iterable);

// =============================================================================
// v14: Promise Runtime
// =============================================================================

Item js_promise_create(Item executor);           // new Promise((resolve, reject) => ...)
Item js_promise_resolve(Item value);             // Promise.resolve(value)
Item js_promise_reject(Item reason);             // Promise.reject(reason)
Item js_promise_then(Item promise, Item on_fulfilled, Item on_rejected);
Item js_promise_catch(Item promise, Item on_rejected);
Item js_promise_finally(Item promise, Item on_finally);
Item js_promise_all(Item iterable);              // Promise.all([...])
Item js_promise_race(Item iterable);             // Promise.race([...])
Item js_promise_any(Item iterable);              // Promise.any([...])
Item js_promise_all_settled(Item iterable);      // Promise.allSettled([...])
Item js_promise_with_resolvers(void);            // Promise.withResolvers()
Item js_await_sync(Item value);                  // Phase 5: synchronous await unwrap

// Phase 6: Async state machine runtime
int64_t js_async_must_suspend(Item value);       // 1 if pending promise, 0 otherwise
Item js_async_get_resolved(void);                // get cached resolved value
Item js_async_context_create(void* fn_ptr, Item* env, int64_t env_size);
Item js_async_start(Item ctx_idx);               // begin async execution at state 0
Item js_async_get_promise(Item ctx_idx);          // get result promise for async ctx

// =============================================================================
// TextEncoder / TextDecoder (UTF-8 only)
// =============================================================================

Item js_text_encoder_new(void);
Item js_text_encoder_encode(Item encoder, Item str);
Item js_text_decoder_new(Item encoding);
Item js_text_decoder_decode(Item decoder, Item input);

// =============================================================================
// WeakMap / WeakSet stubs
// =============================================================================

Item js_weakmap_new(void);
Item js_weakset_new(void);

// Public collection type checks (for instanceof)
bool js_is_map_instance(Item obj);
bool js_is_set_instance(Item obj);

// =============================================================================
// v14: Event Loop & Timers
// =============================================================================

Item js_setTimeout(Item callback, Item delay);         // returns timer id
Item js_setInterval(Item callback, Item delay);        // returns timer id
void js_clearTimeout(Item timer_id);
void js_clearInterval(Item timer_id);

/**
 * Drain the event loop: process all microtasks, then fire due timers.
 * Returns 0 when nothing is pending, nonzero if work remains.
 */
int js_event_loop_drain(void);

/**
 * Initialize/reset the event loop state. Called before JS program execution.
 */
void js_event_loop_init(void);

/**
 * Schedule a microtask (used by Promise resolution).
 */
void js_microtask_enqueue(Item callback);

// =============================================================================
// v14: ES Module Runtime
// =============================================================================

/**
 * Register a module namespace object keyed by module specifier.
 */
void js_module_register(Item specifier, Item namespace_obj);

/**
 * Get a registered module namespace object.
 */
Item js_module_get(Item specifier);

/**
 * Create a module namespace object from an export map.
 */
Item js_module_namespace_create(Item exports_map);

#ifdef __cplusplus
}
#endif
