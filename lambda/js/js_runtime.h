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
#include "../core/name_identity.h"
#include <string.h>

typedef Item (*JsNativeP0)(void);
typedef Item (*JsNativeP1)(Item);
typedef Item (*JsNativeP2)(Item, Item);
typedef Item (*JsNativeP3)(Item, Item, Item);
typedef Item (*JsNativeP4)(Item, Item, Item, Item);
typedef Item (*JsNativeP5)(Item, Item, Item, Item, Item);
typedef Item (*JsNativeP6)(Item, Item, Item, Item, Item, Item);
typedef Item (*JsNativeP7)(Item, Item, Item, Item, Item, Item, Item);
typedef Item (*JsNativeP8)(Item, Item, Item, Item, Item, Item, Item, Item);
typedef Item (*JsNativeSpan)(Item*, int);
typedef Item (*JsNativeThisSpan)(Item, Item*, int);
typedef Item (*JsNativeEnvSpan)(Item, Item*, int);
typedef Item (*JsNativeCallBody)(Item, Item, Item*, int, uint64_t*);
typedef Item (*JsNativeConstructBody)(Item, Item*, int, Item, uint64_t*);

void js_map_promote_descriptor_kind(Map* m);

Item js_undefined(void);
Item make_js_undefined(void);
Item js_make_string_len(const char* str, int len);
Item js_make_string(const char* str);

const char* js_item_to_cstr(Item value, char* buf, int buf_size);
bool js_item_to_integral_int64(Item value, int64_t* out, bool allow_int64);

static inline int js_utf8_next_codepoint(const char* s, int len, int* index) {
    unsigned char c = (unsigned char)s[*index];
    if (c < 0x80) {
        (*index)++;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && *index + 1 < len) {
        int cp = ((c & 0x1F) << 6) | ((unsigned char)s[*index + 1] & 0x3F);
        *index += 2;
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && *index + 2 < len) {
        int cp = ((c & 0x0F) << 12) |
                 (((unsigned char)s[*index + 1] & 0x3F) << 6) |
                 ((unsigned char)s[*index + 2] & 0x3F);
        *index += 3;
        return cp;
    }
    if ((c & 0xF8) == 0xF0 && *index + 3 < len) {
        int cp = ((c & 0x07) << 18) |
                 (((unsigned char)s[*index + 1] & 0x3F) << 12) |
                 (((unsigned char)s[*index + 2] & 0x3F) << 6) |
                 ((unsigned char)s[*index + 3] & 0x3F);
        *index += 4;
        return cp;
    }
    (*index)++;
    return c;
}
// Converts a well-known Symbol numeric ID to its generated realm-local ref.
// Internal runtime code uses this instead of diagnostic "__sym_N" spellings.
Item js_well_known_symbol_key(int64_t symbol_id);
bool js_is_callable(Item value);
bool is_callable(Item value);
bool js_has_call_capability(Item value);
bool js_has_construct_capability(Item value);

// Sentinel value for dense array holes. Uses the reserved non-type tag
// ITEM_SENTINEL_TAG, so it cannot collide with any valid JS value. Ordinary
// object deletes use JSPD_DELETED shape bits instead of storing this raw value
// in map slots.
#define JS_DELETED_SENTINEL_VAL ITEM_JS_DELETED_SENTINEL

// Sentinel value for iterator "done" (returned by js_iterator_step when exhausted).
// Shares ITEM_SENTINEL_TAG with a distinct payload, so it cannot collide with any
// valid JS value including null, undefined, false, 0, or empty string.
#define JS_ITER_DONE_SENTINEL ITEM_JS_ITER_DONE_SENTINEL

LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(JS_DELETED_SENTINEL_VAL >> 56)),
                     "JS deleted sentinel tag must stay out of double discriminator space");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(JS_ITER_DONE_SENTINEL >> 56)),
                     "JS iterator-done sentinel tag must stay out of double discriminator space");

// Maximum module-level live bindings tracked in the compact slot table.
// Generated Unicode identifier tests declare thousands of top-level vars; keep
// this above those rows so they stay on the indexed binding path.
#define JS_MAX_MODULE_VARS 16384

#define JS_LOAD_IC_POLY_MAX 4
#define JS_STORE_IC_POLY_MAX 4
#define JS_LOAD_IC_EMPTY 0
#define JS_LOAD_IC_MONO 1
#define JS_LOAD_IC_POLY 2
#define JS_LOAD_IC_MEGAMORPHIC 3
#define JS_STORE_IC_EMPTY 0
#define JS_STORE_IC_MONO 1
#define JS_STORE_IC_POLY 2
#define JS_STORE_IC_MEGAMORPHIC 3
#define JS_NAMED_IC_RECEIVER_MAP 0
#define JS_NAMED_IC_RECEIVER_ARRAY_PROPS 1

typedef struct JsLoadICEntry {
    void* shape;
    void* entry;
    int64_t byte_offset;
    uint32_t name_id;
    uint8_t receiver_kind;
} JsLoadICEntry;

typedef struct JsLoadIC {
    uint8_t state;
    uint8_t count;
    uint16_t miss_count;
    uint32_t name_id;
    JsLoadICEntry entries[JS_LOAD_IC_POLY_MAX];
} JsLoadIC;

typedef struct JsStoreIC {
    uint8_t state;
    uint8_t count;
    uint16_t miss_count;
    uint32_t name_id;
    JsLoadICEntry entries[JS_STORE_IC_POLY_MAX];
} JsStoreIC;

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
Item js_to_object(Item value);

/**
 * Check if a value is truthy according to JavaScript rules.
 */
bool js_is_truthy(Item value);
// true for both physical representations of an ordinary JavaScript Array.
bool js_is_js_array(Item value);
// Item-status helpers use the same merged lane as ordinary JS calls: a
// boolean true is success and an ERROR-tagged Item is the complete failure.
Item js_status_ok(void);
#define JS_RETURN_IF_ERROR(...) do { \
    Item js_status_value = (__VA_ARGS__); \
    if (item_is_error(js_status_value)) return js_status_value; \
} while (0)
// D8.4.3: use only as a standalone statement in an Item-returning function.
// The assigned value is the one merged JS success/error lane; no ambient
// exception state is consulted between the call and propagation.
#define JS_ASSIGN_OR_RETURN(name, ...) \
    Item name = (__VA_ARGS__); \
    if (item_is_error(name)) return name

// Reuse an existing destination while preserving the same single ERROR lane.
// This form is for staged conversions that intentionally keep the variable's
// identity (for example a rooted receiver or an accumulator).
#define JS_ASSIGN_OR_RETURN_INTO(name, ...) do { \
    (name) = (__VA_ARGS__); \
    if (item_is_error(name)) return (name); \
} while (0)
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
Item js_strict_equal(Item left, Item right);    // ===
// Tune8 §2.1: js_not_equal / js_strict_not_equal removed — transpiler emits
// the eq variant followed by inline MIR_XOR-with-1 on the boxed result.
Item js_less_than(Item left, Item right);       // < (C wrapper around js_compare)
Item js_greater_than(Item left, Item right);    // > (C wrapper around js_compare)
// Tune8 §2.1: js_less_equal / js_greater_equal removed — transpiler emits
// js_compare(op, l, r) where op encodes LT/LE/GT/GE.
Item js_compare(int64_t op, Item left, Item right);

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
// Allocate a JS object with its immutable semantic metadata selected before
// the object is returned to any caller. The class ID is a stable JsClass value.
Item js_new_object_with_class(int class_id);
struct TypeMap;
// Native carriers use the same pre-publication metadata-qualified empty shape.
struct TypeMap* js_object_type_for_class(int class_id);
Item js_new_class_function(void);
void js_set_class_constructor(Item class_function, Item constructor_body);
void js_set_class_instance_prototype(Item class_function, Item prototype);
void js_set_class_superclass(Item class_function, Item superclass);
Item js_get_class_superclass(Item class_function);
bool js_is_class_constructor_value(Item value);
// Shared numeric property-key materialization for JS element/descriptor code.
Item js_property_index_key(int64_t index);
String* js_property_index_name(int64_t index);
const char* js_property_index_chars(int64_t index, int* out_len);
Item js_get_key_default(Item object, Item key);
// Receiver-explicit property Get used by prototype, accessor, and Proxy paths.
Item js_get_key_core(Item object, Item key, Item receiver);
Item js_set_key_default(Item object, Item key, Item value);
// Receiver-explicit property Set used by prototype, accessor, and Proxy paths.
Item js_set_key_core(Item object, Item key, Item value,
                                   Item receiver);
Item js_set_completion_with_key(Item target, Item key, Item value,
                                Item receiver);
Item js_set_primitive_completion(Item target, Item key, Item value);
Item js_set_function_prototype_completion(Item target, Item value);
Item js_set_error_property_completion(Item target, Item key, Item value);
// Internal DefineOwn storage write; it never dispatches inherited accessors.
Item js_define_own_key_storage(Item object, Item key, Item value);
Item js_set_key_cstr(Item object, const char* key, Item value);
Item js_using_dispose(Item resource);
Item js_set_key_strict_policy(Item object, Item key, Item value);
// Tune8 §2.2: dispatcher for JIT-emitted dynamic-strict property sets.
Item js_set_key_policy(Item object, Item key, Item value, int64_t strict);
Item js_assignment_set_result(Item value, Item key, Item set_result,
                              int64_t strict, Item target);
Item js_delete_reference_result(Item key, Item delete_result, int64_t strict);
// Tune8 §2.2: js_private_property_set takes strict flag (4-arg);
// js_private_property_set_strict removed.
Item js_private_property_set(Item object, Item key, Item value, int64_t strict);
Item js_private_field_define(Item object, Item private_key, Item value);
Item js_create_data_property(Item object, Item key, Item value);
Item js_get_reference(Item object, Item key);
Item js_get_name_id(Item object, NameId name_id);
Item js_set_name_id(Item object, NameId name_id, Item value, int64_t strict);
Item js_get_name_id_ic(Item object, NameId name_id, JsLoadIC* ic);
Item js_set_name_id_ic(Item object, NameId name_id, Item value,
    int64_t strict, JsStoreIC* ic);
void* js_active_module_ic(uint32_t index);

// =============================================================================
// Array Functions
// =============================================================================

Item js_array_new(int length);
// Allocate an array with an explicit immutable JS class carrier for branded
// array-backed host objects such as FileList.
Item js_array_new_with_class(int length, int class_id);
Item js_array_new_numeric(int length);
Item js_elements_set_numeric_direct(Item array, int64_t index, Item value);
bool js_is_ordinary_numeric_array(Item value);
bool js_array_promote_numeric(Item array);
bool js_array_validate_elements_kind(Item value);
Item js_array_new_from_item(Item arg);
Item js_create_arguments(void);
Item js_elements_get(Item array, Item index);
Item js_elements_set(Item array, Item index, Item value);
Item js_elements_get_int(Item array, int64_t index);
Item js_elements_set_int(Item array, int64_t index, Item value);
// Returns a boolean Set completion for the narrow ordinary-array index fast
// path, or ItemNull when descriptor/prototype/exotic checks require fallback.
Item js_elements_set_int_completion(Item array, int64_t index, Item value);
Item js_elements_set_number(Item array, Item index, Item value);
Item js_elements_set_int_direct(Item array, int64_t index, Item value);
int64_t js_array_sparse_delete_index(Item array, int64_t index);
int64_t js_array_sparse_has_index(Item array, int64_t index);
Item js_array_sparse_get_index(Item array, int64_t index);
int64_t js_array_sparse_collect_indices(Item array, int64_t start, int64_t end, int64_t* indices, int64_t cap);
int64_t js_elements_set_append_or_dense_int_fast(Item array, int64_t index, Item value);
int64_t js_elements_set_append_or_dense_item_fast(Item array, Item index, Item value);
Item js_array_define_dense_element_direct(Item array, int64_t index, Item value);
int64_t js_array_length(Item array);
Item js_array_push(Item array, Item value);
void js_array_push_item_direct(Array* arr, Item value);
Item js_math_pow(Item base, Item exp);
double js_math_pow_d(double base, double exp);
int64_t js_get_length(Item object);

// =============================================================================
// Function Functions
// =============================================================================

Item js_new_function_mir(void* func_ptr, int param_count);
Item js_new_distinct_function_mir(void* func_ptr, int param_count);
Item js_new_method_function_mir(void* func_ptr, int param_count);
Item js_new_closure_mir(void* func_ptr, int param_count, Item* env, int env_size);
void js_set_formal_length(Item fn_item, int length);
void js_func_cache_suppress_push(void);
void js_func_cache_suppress_pop(void);
Item* js_alloc_env(int count);
void js_env_rehome_scalars(Item* env);
void js_set_function_name(Item fn_item, Item name_item);
void js_set_function_source(Item fn_item, Item source_item);
enum {
    JS_FUNC_INIT_GENERATOR = 1u << 0,
    JS_FUNC_INIT_ASYNC_GENERATOR = 1u << 1,
    JS_FUNC_INIT_ASYNC = 1u << 2,
    JS_FUNC_INIT_ARROW = 1u << 3,
    JS_FUNC_INIT_STRICT = 1u << 4,
    // Compiled wrappers take a trailing scalar-result home. Native builtins
    // retain their published signatures and do not set this marker.
    JS_FUNC_INIT_MIR_PUBLIC_ABI = 1u << 5,
    // The compiled body can leave a dynamic with scope on an early return.
    // Call dispatch must restore the caller's stack even when both endpoints
    // were empty on entry.
    JS_FUNC_INIT_USES_WITH = 1u << 6,
    // These facts are valid only when compiler analysis completed. Runtime and
    // dynamically-created wrappers deliberately omit ANALYSIS_KNOWN so the
    // dispatcher retains the conservative binding path.
    JS_FUNC_INIT_ANALYSIS_KNOWN = 1u << 7,
    JS_FUNC_INIT_READS_THIS = 1u << 8,
    JS_FUNC_INIT_READS_NEW_TARGET = 1u << 9,
    JS_FUNC_INIT_MIR_CONTEXT_ABI = 1u << 10,
    JS_FUNC_INIT_CLASS_FIELD_INITIALIZER = 1u << 11,
};
void js_finalize_function(Item fn_item, const char* name_chars,
                          const char* source_chars, uint64_t span_lengths,
                          int64_t formal_length, int64_t init_flags);
void js_set_function_home_class(Item fn_item, Item home_class);
void js_mark_generator_func(Item fn_item);
void js_mark_async_generator_func(Item fn_item);
void js_mark_async_func(Item fn_item);
void js_mark_derived_constructor_func(Item fn_item);
void js_mark_eval_initializer_func_if_active(Item fn_item);
Item js_get_constructor(Item name_item);
Item js_get_intrinsic_prototype_for_class(int class_id);
Item js_get_typed_array_per_type_proto(int element_type);
Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count);
Item js_call_accessor_getter(Item getter, Item receiver);
Item js_call_function_into(Item func_item, Item this_val, Item* args,
                           int arg_count, uint64_t* result_home);
Item js_call_function_prerooted_args_into(Item func_item, Item this_val,
                                          Item* args, int arg_count,
                                          uint64_t* result_home);
Item js_call_constructor_body_into(Item func_item, Item this_val, Item* args,
                                   int arg_count, Item new_target,
                                   uint64_t* result_home);
Item js_call_constructor_body_prerooted_args_into(Item func_item, Item this_val,
                                                  Item* args, int arg_count,
                                                  Item new_target,
                                                  uint64_t* result_home);
Item js_call_export_0_into(Function* function, uint64_t* result_home);
Item js_call_export_1_into(Function* function, Item a, uint64_t* result_home);
Item js_call_export_2_into(Function* function, Item a, Item b, uint64_t* result_home);
Item js_call_export_3_into(Function* function, Item a, Item b, Item c,
                           uint64_t* result_home);
Item js_call_export_4_into(Function* function, Item a, Item b, Item c, Item d,
                           uint64_t* result_home);
Item js_call_export_5_into(Function* function, Item a, Item b, Item c, Item d,
                           Item e, uint64_t* result_home);
Item js_call_export_6_into(Function* function, Item a, Item b, Item c, Item d,
                           Item e, Item f, uint64_t* result_home);
Item js_call_export_7_into(Function* function, Item a, Item b, Item c, Item d,
                           Item e, Item f, Item g, uint64_t* result_home);
Item js_call_export_8_into(Function* function, Item a, Item b, Item c, Item d,
                           Item e, Item f, Item g, Item h, uint64_t* result_home);
void js_array_stats_dump(void);
void js_set_call_stack_limit(int64_t limit);
Item js_apply_function(Item func_item, Item this_val, Item args_array);
Item js_apply_function_into(Item func_item, Item this_val, Item args_array,
                            uint64_t* result_home);
Item js_super_call_class_into(Item callee, Item this_val, Item* args, int argc,
                              uint64_t* result_home);
Item js_super_apply_class_into(Item callee, Item this_val, Item args_array,
                               uint64_t* result_home);
Item js_construct_array_like(Item constructor, Item args_array, Item new_target);
void js_set_internal_class_name(Item obj, Item class_name);
Item js_bind_function(Item func_item, Item bound_this, Item* bound_args, int bound_argc);
void js_function_root_item_if_needed(void* fn, Item* slot);
Item js_func_bind(Item func_item, Item bound_this, Item* bound_args, int bound_argc);
Item js_new_function_from_string(Item* args, int argc);
Item js_dynamic_function_call_body(Item callee, Item this_value, Item* args,
                                   int argc, uint64_t* result_home);
Item js_dynamic_function_construct_body(Item callee, Item* args, int argc,
                                        Item new_target, uint64_t* result_home);
Item js_dynamic_async_function_call_body(Item callee, Item this_value,
                                         Item* args, int argc,
                                         uint64_t* result_home);
Item js_dynamic_async_function_construct_body(Item callee, Item* args,
                                              int argc, Item new_target,
                                              uint64_t* result_home);
Item js_dynamic_generator_function_call_body(Item callee, Item this_value,
                                             Item* args, int argc,
                                             uint64_t* result_home);
Item js_dynamic_generator_function_construct_body(Item callee, Item* args,
                                                  int argc, Item new_target,
                                                  uint64_t* result_home);
Item js_dynamic_async_generator_function_call_body(Item callee,
        Item this_value, Item* args, int argc, uint64_t* result_home);
Item js_dynamic_async_generator_function_construct_body(Item callee,
        Item* args, int argc, Item new_target, uint64_t* result_home);
Item js_builtin_eval(Item code_item, int64_t is_global_scope);
void js_eval_private_push_frame(void);
void js_eval_private_pop_frame(void);
void js_eval_private_bind(Item unscoped_key, Item scoped_key);
Item js_eval_private_resolve(Item unscoped_key);
int64_t js_262_eval_script_is_active(void);
Item js_create_regex(const char* pattern, int pattern_len, const char* flags, int flags_len);
Item js_create_regex_literal(const char* pattern, int pattern_len, const char* flags, int flags_len);
Item js_create_regex_literal_items(Item pattern_item, Item flags_item);
Item js_regexp_construct(Item pattern_item, Item flags_item);
Item js_regex_test(Item regex, Item str);
Item js_regex_exec(Item regex, Item str);
Item js_debug_check_callee(Item callee, int64_t site_id);
Item js_get_this();
Item js_get_lexical_this_binding(void);
Item js_resolve_lexical_this(Item this_val);
void js_set_this(Item this_val);
Item js_get_new_target();
void js_set_direct_new_target(Item target);
void js_set_pending_call_source(const char* source, int64_t len);
Item js_super_bind_this(Item this_val, Item construct_result);
Item js_get_super_this_value(void);
Item js_get_super_constructor_from_receiver(Item receiver, Item fallback_ctor);
Item js_build_arguments_object(void);
void js_set_arguments_info(int64_t is_strict);

// Get the native function pointer from a JsFunction Item (handles JsFunction layout)
void* js_function_get_ptr(Item fn_item);

// Get the parameter count from a JsFunction Item
int js_function_get_arity(Item fn_item);

// =============================================================================
// Console Functions
// =============================================================================

Item js_console_log(Item value);

// =============================================================================
// Math Object Methods & Properties
// =============================================================================

Item js_get_math_object_value(void);
Item js_get_json_object_value(void);
Item js_get_console_object_value(void);
Item js_get_reflect_object_value(void);
Item js_get_atomics_object_value(void);
Item js_get_262_object_value(void);
Item js_get_css_object_value(void);

// =============================================================================
// v5: Process I/O
// =============================================================================

Item js_process_stdout_write(Item str_item);
void js_store_process_argv(int argc, const char** argv);
void js_set_process_argv(int argc, const char** argv);
void js_store_process_exec_argv(int argc, const char** argv);
void js_set_process_exec_argv(int argc, const char** argv);
Item js_get_process_argv(void);
Item js_get_process_exec_argv(void);
Item js_get_process_object_value(void);
int js_is_process_object_value(Item object);
void js_set_diagnose_enabled(int enabled);
int js_is_diagnose_enabled(void);

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

// =============================================================================
// v5: String Methods (charCodeAt, fromCharCode)
// =============================================================================

Item js_string_charCodeAt(Item str_item, Item index_item);
Item js_string_fromCharCode(Item code_item);
Item js_string_fromCharCode_int(int64_t code_value);
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
Item js_typed_array_enumerable_custom_keys(Item object);
Item js_for_in_keys(Item object);
Item js_object_get_own_property_names(Item object);
Item js_object_get_own_property_symbols(Item object);
Item js_to_string_val(Item value);
Item js_number_property(Item prop_name);
// Phase-5C: js_make_getter_key/js_make_setter_key removed (no callers).


// =============================================================================
// v8: Object & Global extensions
// =============================================================================

Item js_object_create(Item proto);
Item js_object_define_property(Item obj, Item name, Item descriptor);
Item js_object_define_properties(Item obj, Item props);
Item js_object_create_define_properties(Item obj, Item props);
Item js_object_get_own_property_descriptor(Item obj, Item name);
Item js_object_get_own_property_descriptors(Item obj);
Item js_array_is_array(Item value);
Item js_performance_now(void);
double js_performance_now_ms(void);
double js_performance_monotonic_now_ms(void);
double js_performance_monotonic_to_relative(double monotonic_ms);
void js_performance_virtual_clock_set(bool enabled, double monotonic_ms);
void js_performance_frame_clock_begin(double monotonic_ms);
void js_performance_frame_clock_end(void);
double js_performance_time_origin_ms(void);
Item js_date_now(void);
Item js_date_now_string(void);
Item js_date_new(void);
Item js_date_new_from(Item value);
Item js_date_utc(Item args_array);
Item js_date_method(Item date_obj, int method_id);
Item js_date_setter(Item date_obj, int method_id, Item arg0, Item arg1, Item arg2, Item arg3);
Item js_date_new_multi(Item args_array);
Item js_date_parse(Item str_item);
Item js_map_collection_new(void);
Item js_map_collection_new_from(Item iterable);
Item js_set_collection_new(void);
Item js_set_collection_new_from(Item iterable);
Item js_collection_method(Item obj, int method_id, Item arg1, Item arg2);
Item js_alert(Item msg);
void js_set_prototype(Item object, Item prototype);
void js_object_proto_setter(Item object, Item value);
void js_mark_non_enumerable(Item object, Item name);
void js_mark_non_writable(Item object, Item name);
void js_mark_non_configurable(Item object, Item name);
void js_func_init_property(Item fn, Item key, Item value);
void js_mark_all_non_enumerable(Item object);
Item js_new_number_wrapper(Item arg);
Item js_new_number_checked(Item arg);
Item js_new_boolean_wrapper(Item arg);
Item js_new_string_wrapper(Item arg);
void js_link_base_prototype(Item proto_marker, Item base_ctor);
Item js_get_prototype(Item object);
Item js_get_prototype_of(Item object);
Item js_get_prototype_from_constructor_default(Item new_target,
    int default_class, int typed_array_type);
Item js_reflect_construct(Item target, Item args_array, Item new_target);
Item js_reflect_apply(Item target, Item this_arg, Item args_array);
Item js_reflect_define_property(Item obj, Item key, Item desc);
Item js_reflect_delete_property(Item obj, Item key);
Item js_reflect_get_own_property_descriptor(Item target, Item key);
Item js_reflect_get_prototype_of(Item target);
Item js_reflect_is_extensible(Item target);
Item js_reflect_own_keys(Item obj);
Item js_reflect_prevent_extensions(Item obj);
Item js_reflect_set(Item obj, Item key, Item value, Item receiver);
Item js_reflect_set_prototype_of(Item obj, Item proto);
Item js_prototype_lookup(Item object, Item property);
Item js_map_shape_lookup_ext(Map* m, const char* key_str, int key_len, bool* out_found);

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
Item js_object_has_own(Item obj, Item key);
Item js_object_prototype_has_own_property(Item this_val, Item key);
Item js_object_freeze(Item obj);
Item js_object_is_frozen(Item obj);
Item js_object_seal(Item obj);
Item js_object_is_sealed(Item obj);

// Tagged template literals
Item js_build_template_object(Item* cooked, Item* raw, int count);
Item js_build_template_object_cached(Item* cooked, Item* raw, int count, int64_t site_id);
void js_reset_template_registry(void);
Item js_new_check_constructor_return(Item obj, Item result);
Item js_object_prevent_extensions(Item obj);
Item js_object_is_extensible(Item obj);

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
Item js_array_from_with_mapper_this(Item iterable, Item mapFn, Item this_arg);
Item js_json_parse(Item str_item);
Item js_json_parse_full(Item str_item, Item reviver);
Item js_json_stringify(Item value);
Item js_json_stringify_full(Item value, Item replacer, Item space);
Item js_delete_property(Item obj, Item key);
Item js_delete_property_strict(Item obj, Item key);

// v15: fetch() API
Item js_fetch(Item url, Item options);
void js_fetch_reset(void);
// Set base directory for resolving relative fetch() URLs to local files
// (used by `lambda.exe js --document <html>` so tests can fetch sibling
// resources from disk without an HTTP server).
void js_fetch_set_base_path(const char* dir_path);

// =============================================================================
// Exception Handling (try/catch/throw)
// =============================================================================

// D8.4.3: throw returns the merged ERROR Item; routing carries that Item.
Item js_throw_value(Item value);
// Convert a routed ERROR Item to the JavaScript value observable at a catch,
// rejection, or host boundary.  It never reads or clears ambient state.
Item js_error_lane_payload(Item lane);
// Format one routed lane for a host diagnostic without consulting ambient
// ambient state.  `out` is caller-owned and may be empty on failure.
void js_error_lane_format(Item lane, char* out, int out_size);

/** v20: Throw a RangeError with the given message. */
Item js_throw_range_error(const char* message);
Item js_throw_type_error(const char* message);
Item js_throw_syntax_error(Item message);
Item js_throw_reference_error(Item message);
// Tune8 §2.3: unified entry point for MIR-emitted throws (kind=0:SyntaxError,
// kind=1:ReferenceError). The named wrappers above are kept for C callers.
Item js_throw_named_error(int64_t kind, Item message);

/** Throw TypeError/RangeError with Node.js error code (e.g. ERR_INVALID_ARG_TYPE). */
Item js_throw_type_error_code(const char* code, const char* message);
Item js_throw_range_error_code(const char* code, const char* message);
Item js_throw_error_with_code(const char* code, const char* message);

/**
 * Node.js-style error helpers for common validation patterns.
 * Format: 'The "name" argument must be of type expected. Received type actual'
 */
Item js_throw_invalid_arg_type(const char* name, const char* expected, Item actual);
Item js_throw_invalid_arg_value(const char* name, const char* reason, Item actual);
Item js_throw_out_of_range(const char* name, const char* range, Item actual);

/** Throw a system error (like ENOENT, EACCES) with code, errno, syscall, path. */
Item js_throw_system_error(int uv_errno, const char* syscall, const char* path);

/** Throw TypeError if value is null or undefined (ES spec RequireObjectCoercible). */
Item js_require_object_coercible(Item value);

/**
 * Create a new Error object with a message.
 * Returns a Map with {name: "Error", message: msg, stack: trace}.
 */
Item js_new_error(Item message);
Item js_new_named_error(const char* type_name, const char* message);
Item js_throw_named_error_text(const char* type_name, const char* message);
Item js_new_error_with_stack(Item message, Item stack_str);

/**
 * v11: Create a typed Error object (TypeError, RangeError, etc.).
 * Returns a Map with {name: error_name, message: msg, stack: trace}.
 */
Item js_new_error_with_name(Item error_name, Item message);
Item js_new_error_with_name_stack(Item error_name, Item message, Item stack_str);
Item js_error_materialize_stack(Item error_obj);

/**
 * ES2021: Create AggregateError with errors array and message.
 */
Item js_new_aggregate_error(Item errors, Item message);

/**
 * ES2022: Extract cause from options object and set on error.
 */
Item js_error_set_cause(Item error, Item options);

// V8-specific: Error.captureStackTrace(targetObject[, constructorOpt])
Item js_error_captureStackTrace(Item target, Item ctor);

// TDZ (Temporal Dead Zone) check for let/const
Item js_check_tdz(Item value, NameId name_id, int name_len);

// Const assignment error
Item js_throw_const_assign(NameId name_id, int name_len);

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
void js_init_module_vars_undefined_bulk(const int* indices, const Item* keys,
    int count, int define_global_var_properties);
void js_reset_module_vars(void);
uint32_t js_alloc_module_state(uint32_t var_count);
bool js_activate_module_state(uint32_t var_count);
bool js_ensure_active_module_var_capacity(uint32_t required_var_count);
uint32_t js_get_active_module_state_id(void);
bool js_set_active_module_state_id(uint32_t module_state_id);
bool js_module_state_is_available(uint32_t module_state_id);
uint64_t js_active_module_name_id(uint32_t index);
Item js_active_module_name_item(uint32_t module_name_index, NameId direct_name_id);
uint32_t js_active_module_name_count(void);
uint32_t js_active_module_ic_count(void);
bool js_link_module_ic_table(uint32_t module_state_id, uint32_t count);
bool js_append_module_ic_table(uint32_t module_state_id, uint32_t count);
void* js_active_module_ic(uint32_t index);
uint32_t js_get_batch_preamble_var_count(void);
bool js_copy_module_state_var_prefix(uint32_t source_module_state_id,
                                     uint32_t destination_module_state_id,
                                     uint32_t count);
void js_eval_preamble_cache_reset(void);
void js_register_global_var_module_binding(Item key, int64_t index);
void js_register_global_var_module_bindings_bulk(const Item* keys, const int* indices, int count);

/**
 * Reset all JS runtime global state between batch test runs.
 * Clears module vars, event loop, DOM context, and Input context.
 */
void js_batch_reset(void);
void js_intrinsic_state_teardown(void);
int js_get_module_var_count(void);
void js_batch_reset_to(int checkpoint_var_count);
void js_prepare_compiled_preamble_vars(int declaration_count);
extern int js_batch_execution_mode;
void js_symbol_registry_batch_reset(void);
void js_dom_batch_reset(void);
void js_globals_batch_reset(void);
void js_reset_constructor_prototypes(void);
Item js_constructor_create_object(Item callee, Item new_target);
Item js_construct_value(Item callee, Item* args, int argc, Item new_target,
                        uint64_t* result_home, bool args_prerooted);
Item js_construct_value_defer_own_fields(Item callee, Item* args, int argc,
                                         Item new_target);

// =============================================================================
// v12: Language extensions
// =============================================================================

// Object rest destructuring: create object with all props except excluded keys
Item js_object_rest(Item src, Item* exclude_keys, int exclude_count);

// URI encoding/decoding
Item js_encodeURIComponent(Item str_item);
Item js_decodeURIComponent(Item str_item);
Item js_encodeURI(Item str_item);
Item js_decodeURI(Item str_item);
Item js_unescape(Item str_item);
Item js_escape(Item str_item);
Item js_atob(Item str_item);
Item js_btoa(Item str_item);

typedef struct JsMirPhaseTiming {
    long parse_us;
    long ast_us;
    long early_us;
    long imports_us;
    long mir_us;
    long link_us;
    long execute_us;
    long cleanup_us;
    long total_us;
    long preamble_us;
} JsMirPhaseTiming;

void js_mir_reset_last_phase_timing(void);
void js_mir_get_last_phase_timing(JsMirPhaseTiming* out);
void js_mir_begin_document_phase_timing(void);
void js_mir_accumulate_last_phase_timing(bool is_preamble);
void js_mir_end_document_phase_timing(JsMirPhaseTiming* out);

// Shared ECMAScript IdentifierName policy backed by the generated Unicode
// ID_Start / ID_Continue range tables used by RegExp property support.
bool js_unicode_id_is_start(uint32_t cp);
bool js_unicode_id_is_continue(uint32_t cp);

bool js_regexp_virtual_property_is_overridden(Item regex, const char* name, int name_len);
void js_regexp_mark_virtual_property_overridden(Item regex, const char* name, int name_len);

// Tune6 diagnostics: scope-lookup counters. Used by the JS transpile timing
// benchmark to test whether the linear-scan scope lookup is the AST-build
// bottleneck on large/minified libraries. Counting is gated by an enable flag so
// there is zero accumulation cost in normal runs.
typedef struct JsScopeCounters {
    long lookup_calls;     // calls to js_scope_lookup + js_scope_lookup_current
    long entries_scanned;  // total NameEntry compared across all lookups
    long scopes_walked;    // total parent scopes visited across all lookups
    long cache_hits;
    long cache_misses;
} JsScopeCounters;

void js_scope_counters_set_enabled(int enabled);
void js_scope_counters_reset(void);
void js_scope_counters_get(JsScopeCounters* out);

// Tune9 diagnostics: identifier-shape counters for Unicode identifier rows.
// Disabled by default; LAMBDA_JS_IDENTIFIER_STATS=1 also emits a per-process TSV
// under ./temp/js_identifier_stats.
typedef struct JsIdentifierCounters {
    long ast_identifiers;
    long ast_escaped_identifiers;
    long ast_non_ascii_identifiers;
    long ast_source_bytes;
    long ast_decoded_bytes;
    long early_identifier_checks;
    long early_escape_checks;
    long early_unicode_normalizations;
    long early_reserved_hits;
    long early_contextual_escape_hits;
} JsIdentifierCounters;

void js_identifier_counters_set_enabled(int enabled);
void js_identifier_counters_reset(void);
void js_identifier_counters_get(JsIdentifierCounters* out);
int js_identifier_counters_is_enabled(void);
void js_identifier_counters_record_ast(int source_len, int decoded_len,
    int has_escape, int has_non_ascii);
void js_identifier_counters_record_early_check(void);
void js_identifier_counters_record_early_escape(int normalized,
    int reserved_hit, int contextual_hit);

// Tune6 §3.2: MIR generated-code volume for the last transpile. Drives the
// MIR-lowering reduction work (§3.3) — identifies which fixtures emit the most MIR.
typedef struct JsMirVolumeCounters {
    long functions_discovered;  // JS functions collected (mt->func_count)
    long mir_insns_emitted;     // total MIR instructions across all func items
} JsMirVolumeCounters;

void js_mir_volume_counters_reset(void);
void js_mir_volume_counters_set(long functions_discovered, long mir_insns_emitted);
void js_mir_volume_counters_get(JsMirVolumeCounters* out);

// globalThis / global object
Item js_get_global_this(void);
Item js_get_global_object(void);
int js_is_global_this_object_value(Item object);
Item js_get_global_property(Item key);
Item js_get_global_property_strict(Item key);
Item js_get_global_property_reference(Item key, int64_t strict_reference);
int64_t js_global_binding_exists(Item key);
// Tune8 §2.2: js_set_global_property now takes a strict flag
// (0 = sloppy implicit global, 1 = strict throw-on-undeclared).
// js_set_global_property_strict has been removed.
Item js_set_global_property(Item key, Item value, int64_t strict);
Item js_set_global_var_property_fast(Item key, Item value);
Item js_set_global_property_strict_prechecked(Item key, Item value, int64_t binding_exists_at_lhs);
void js_define_global_var_property(Item key, Item value);
void js_define_global_eval_var_property(Item key, Item value);
void js_define_global_function_property(Item key, Item value);
void js_global_lexical_declare(Item key, Item value, int64_t immutable);
int64_t js_global_lexical_binding_exists(Item key);
Item js_global_lexical_get_or_fallback(Item key, Item fallback);
Item js_global_lexical_set_if_exists(Item key, Item value);
Item js_evalscript_check_global_lex_decl(Item key);
void js_mark_private_method_non_writable(Item object, Item name);
void js_set_method_home_from_target(Item target, Item fn_item);
void js_refresh_prototype_method_homes(Item prototype, Item class_item);
Item js_init_class_instance_fields(Item callee, Item object);
Item js_init_class_instance_fields_after_super(Item callee, Item object);
void js_init_class_instance_field_metadata(Item class_item, int count);
void js_set_class_instance_field_metadata_name_id_range(Item class_item,
    int index, uint32_t module_name_base, int count, uint64_t method_mask);
void js_set_class_instance_field_metadata_key(Item class_item, int index, Item key);
void js_set_class_instance_field_metadata_value(Item class_item, int index, Item value);
void js_set_class_instance_field_metadata_initializer(Item class_item, int index,
    Item initializer);
Item js_private_key_for_class(Item class_item, Item source_name);
Item js_private_key_for_current_class(Item source_name);
Item js_private_in(Item object, Item private_key);
Item js_private_home_class_enter(Item class_item);
void js_private_home_class_leave(Item previous_class);
Item js_private_home_class_leave_result(Item previous_class, Item result);
Item js_private_brand_add(Item object, Item private_key, Item callee);
void js_set_function_name_from_property_key_if_anonymous(Item fn_item, Item key_item, int64_t prefix_kind);
void js_set_function_name_if_anonymous(Item fn_item, Item name_item);
Item js_get_global_builtin_fn_by_id(Item global_id);
void js_eval_env_push_frame(void);
void js_eval_global_lexical_push_frame(void);
int64_t js_eval_local_push_frame(void);
void js_eval_local_pop_frame(void);
void js_eval_private_push_frame(void);
void js_eval_private_pop_frame(void);
void js_eval_private_bind(Item unscoped_key, Item scoped_key);
Item js_eval_private_resolve(Item unscoped_key);
Item js_eval_local_get_binding_or_fallback(Item key, Item fallback);
void js_eval_local_export_var(Item key, Item value);
void js_eval_local_note_lexical_binding(Item key);
int64_t js_eval_local_has_lexical_binding(Item key);
void js_eval_local_note_immutable_binding(Item key);
int64_t js_eval_local_has_immutable_binding(Item key);
int64_t js_with_depth_active(void);
Item js_get_with_binding_or_fallback(Item key, Item fallback);
Item js_get_with_binding_or_fallback_strict(Item key, Item fallback);
Item js_get_last_with_binding_base_or_undefined(Item key);
Item js_probe_with_binding(Item key);
Item js_capture_with_binding(Item key);
Item js_set_last_with_binding_if_valid(Item key, Item value, int64_t strict);
Item js_set_with_binding_base(Item scope_obj, Item key, Item value, int64_t strict);
void js_eval_env_bind(Item key, Item value);
void js_eval_env_bridge_journal_vars(void);
void js_eval_global_lexical_bind(Item key, Item value);
int64_t js_eval_env_has_binding(Item key);
int64_t js_eval_env_is_active(void);
void js_eval_env_track_global_binding(Item key);
void js_eval_env_pop_frame(void);
void js_eval_global_lexical_pop_frame(void);
Item js_check_unresolved_capture(Item value, NameId name_id, int64_t len);
Item js_check_capture_binding(Item value, NameId name_id, int64_t len);
Item js_resolve_unresolved_binding(Item value, NameId name_id, int64_t len, int64_t in_typeof);

// URL constructor
Item js_url_construct(Item input);
Item js_url_construct_with_base(Item input, Item base);
Item js_url_parse(Item input, Item base);
Item js_url_can_parse(Item input);
Item js_readable_stream_new(Item underlying_source);
Item js_writable_stream_new(Item underlying_sink);

// Symbol API
// Symbol items are encoded as negative ints: -(id + JS_SYMBOL_BASE).
// Base must be beyond int32 range to avoid collision with bitwise op results.
#define JS_SYMBOL_BASE (1LL << 40)

Item js_symbol_create(Item description);
Item js_symbol_for(Item key);
Item js_symbol_key_for(Item sym);
Item js_symbol_to_string(Item sym);
Item js_symbol_get_description(Item sym);
Item js_symbol_well_known(Item name);

// =============================================================================
// v14: Generator Runtime
// =============================================================================

/**
 * Create a generator object from a state machine function pointer.
 * The func_ptr is the MIR-compiled generator body (state machine form).
 * env/env_size represent captured closure variables.
 */
Item js_generator_create(void* func_ptr, Item* env, int env_size, int is_async);
Item js_generator_create_mir(void* func_ptr, Item* env, int env_size, int is_async);

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
Item js_gen_await_result(Item value, int64_t next_state);
Item js_gen_yield_delegate_result(Item iterable, int64_t resume_state);
Item js_gen_return_signal(Item value);
int64_t js_gen_is_return_signal(Item value);
Item js_gen_return_signal_value(Item value);
Item js_gen_throw_signal(Item value);
int64_t js_gen_is_throw_signal(Item value);
Item js_gen_throw_signal_value(Item value);

/**
 * v15: Convert an iterable to an array. Drains generators, passes arrays through.
 */
Item js_iterable_to_array(Item iterable);

/**
 * Lazy iteration protocol for for-of loops.
 * js_get_iterator: Get an iterator object from an iterable.
 * js_iterator_step: Advance iterator, return next value or JS_ITER_DONE_SENTINEL when done.
 * js_iterator_close: Call iterator.return() for IteratorClose (on break/return).
 */
Item js_get_iterator(Item iterable);
Item js_get_async_iterator(Item iterable);
Item js_get_iterator_lazy(Item iterable);
bool js_is_fixed_layout_iterator(Item object);
Item js_iterator_step(Item iterator);
Item js_iterator_close(Item iterator);
Item js_iterator_collect_rest(Item iterator);

// =============================================================================
// v14: Promise Runtime
// =============================================================================

Item js_promise_create(Item executor);           // new Promise((resolve, reject) => ...)
Item js_promise_resolve(Item value);             // Promise.resolve(value)
Item js_promise_reject(Item reason);             // Promise.reject(reason)
Item js_promise_create_pending(void);
bool js_promise_is(Item promise);
void js_promise_fulfill_existing(Item promise, Item value);
void js_promise_reject_existing(Item promise, Item reason);
const char* js_promise_state_name(Item promise); // "pending", "fulfilled", "rejected", or NULL
int js_promise_pending_count(void);
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
Item js_async_must_suspend(Item value);          // true if pending promise, false otherwise
Item js_async_get_resolved(void);                // get cached resolved value
Item js_async_context_create(void* fn_ptr, Item* env, int64_t env_size, Item this_val);
Item js_async_context_create_mir(void* fn_ptr, Item* env, int64_t env_size,
                                 Item this_val);
Item js_async_start(Item ctx_idx);               // begin async execution at state 0
Item js_async_get_promise(Item ctx_idx);          // get result promise for async ctx

// =============================================================================
// TextEncoder / TextDecoder (UTF-8 only)
// =============================================================================

Item js_text_encoder_new(void);
Item js_text_encoder_encode(Item encoder, Item str);
Item js_text_encoder_encode_method(Item encoder, Item* args, int argc);
Item js_text_decoder_new(Item encoding, Item options);
Item js_text_decoder_decode(Item decoder, Item input);
Item js_text_decoder_decode_method(Item decoder, Item* args, int argc);

// =============================================================================
// WeakMap / WeakSet stubs
// =============================================================================

Item js_weakmap_new(void);
Item js_weakset_new(void);
Item js_weakref_new(Item target);
Item js_finalization_registry_new(Item cleanup_callback);
Item js_weakmap_new_with_iter(Item iterable);
Item js_weakset_new_with_iter(Item iterable);

// Public collection type checks (for instanceof)
bool js_is_map_instance(Item obj);
bool js_is_set_instance(Item obj);

// =============================================================================
// ES6 Proxy
// =============================================================================

typedef struct JsProxyData {
    uint64_t target;   // [[ProxyTarget]] — Item stored as uint64_t for C/C++ header compat
    uint64_t handler;  // [[ProxyHandler]] — Item stored as uint64_t for C/C++ header compat
    uint64_t private_slots; // engine-private slots attached to the Proxy object itself
    bool callable;     // immutable [[Call]] capability copied from the target
    bool constructable; // immutable [[Construct]] capability copied from the target
    bool revoked;      // true after Proxy.revocable().revoke() called
} JsProxyData;

Item js_proxy_new(Item target, Item handler);
Item js_proxy_revocable(Item target, Item handler);

// Check if an Item is a Proxy
bool js_is_proxy(Item obj);
// Get proxy data (returns NULL if not a proxy)
JsProxyData* js_get_proxy_data(Item obj);
// Get the ultimate non-proxy target (unwrap nested proxies)
Item js_proxy_get_target(Item obj);
Item js_set_private_proxy_property(Item proxy, Item key, Item value);

bool js_proxy_has_callable_target(Item obj);

// Proxy trap dispatch functions (called from js_globals.cpp)
Item js_proxy_trap_has(Item proxy, Item key);
Item js_proxy_trap_delete(Item proxy, Item key);
Item js_proxy_trap_own_keys(Item proxy);
Item js_proxy_trap_get_own_property_descriptor(Item proxy, Item key);
Item js_proxy_trap_define_property(Item proxy, Item key, Item desc);
Item js_proxy_trap_get_prototype_of(Item proxy);
Item js_proxy_trap_set_prototype_of(Item proxy, Item proto);
Item js_proxy_trap_is_extensible(Item proxy);
Item js_proxy_trap_prevent_extensions(Item proxy);
Item js_proxy_trap_apply(Item proxy, Item this_val, Item* args, int arg_count);
Item js_proxy_trap_construct(Item proxy, Item* args, int arg_count, Item new_target);

// =============================================================================
// v14: Event Loop & Timers
// =============================================================================

Item js_setTimeout(Item callback, Item delay);         // returns timer id
Item js_setInterval(Item callback, Item delay);        // returns timer id
void js_clearTimeout(Item timer_id);
void js_clearInterval(Item timer_id);
Item js_setImmediate(Item callback);                   // schedule for next tick
Item js_setImmediate_with_args(Item callback, Item args_array);
void js_clearImmediate(Item id);
Item js_requestAnimationFrame(Item callback);          // schedule for next frame
void js_cancelAnimationFrame(Item request_id);
Item js_structuredClone(Item value);                   // deep clone

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
void js_next_tick_enqueue(Item callback);

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
 * Js57 P3 (Track B2): read a live default-binding for the given module
 * specifier. Used by self-imports so reads of the imported name observe the
 * current state of namespace.default; throws ReferenceError on the TDZ sentinel.
 */
Item js_get_live_binding_default(Item specifier);

/**
 * Js57 P4 (Track B3): register a post-await chunk to be invoked after the
 * outermost module-load call unwinds. See js_runtime.cpp for details.
 */
void js_tla_register_continuation(Item func);
void js_tla_enter_module(void);
void js_tla_exit_module(void);
void js_tla_flush_for_dynamic_import(void);
int js_tla_module_depth_get(void);

/**
 * Js57 P5 (fulfillment/rejection-order): TLA awaited-target tracking on the
 * module registry. Used to make dynamic imports wait on the same Promise the
 * imported module's first top-level await is blocked on, and to propagate
 * that wait through static-import edges so siblings end up chained on the
 * same target.
 */
void js_module_set_awaited_target(Item specifier, Item target);
Item js_module_get_awaited_target(Item specifier);
void js_module_inherit_awaited_target(Item current_specifier, Item dep_specifier);
Item js_p5_module_await(Item specifier, Item value);
void js_module_record_evaluation_error(Item specifier, Item error);
Item js_module_get_evaluation_error(Item specifier);

/* Js57 P7d: per-module TLA evaluation tracking. */
void js_module_mark_has_tla(Item specifier);
int  js_module_get_has_tla(Item specifier);
int  js_module_needs_async_settle(Item specifier);
void js_tla_drain_pending_modules(void);
void js_module_register_async_parent(Item dep_specifier, Item parent_specifier);
void js_module_set_deferred_main_ptr(Item specifier, void* main_ptr);
int  js_module_pending_async_deps(Item specifier);
void js_module_mark_post_await_pending(Item specifier);
int  js_module_get_body_state(Item specifier);
void js_module_set_body_state(Item specifier, int state);
int  js_module_assign_async_eval_order(Item specifier);
void js_module_reset_aeo_counter(void);
void js_module_complete_tla_body(Item specifier);
void js_module_save_context(Item specifier, uint32_t module_state_id);
uint32_t js_module_get_saved_module_state_id(Item specifier);

/**
 * Create a module namespace object from an export map.
 */
Item js_module_namespace_create(Item exports_map);

/**
 * Current namespace object for the module being evaluated.
 */
Item js_get_active_module_namespace();
Item js_set_active_module_namespace(Item namespace_obj);
Item js_get_import_meta();

/**
 * CJS require() — load and execute a module, return its exports.
 * Defined in transpile_js_mir.cpp (needs access to transpiler internals).
 */
Item js_require(Item specifier);
Item js_dynamic_import(Item specifier);

// Native SHA hash functions (js_crypto.cpp)
Item js_native_sha256(Item data, Item offset, Item length);
Item js_native_sha384(Item data, Item offset, Item length);
Item js_native_sha512(Item data, Item offset, Item length);

// =============================================================================
// OffscreenCanvas / CanvasRenderingContext2D (js_canvas.cpp)
// =============================================================================

Item js_offscreen_canvas_new(Item width, Item height);
Item js_canvas_get_context(Item canvas);
void js_canvas_ctx_set_font(Item ctx_obj, Item font_val);
Item js_canvas_measure_text(Item ctx_obj, Item text);
bool js_canvas_property_set_intercept(Item obj, Item key, Item value);
void js_canvas_cleanup(void);
bool js_array_runtime_items_release(Item* items);
void js_array_runtime_items_cleanup_all(void);

#ifdef __cplusplus
}

Item make_string_item(const char* str, int len);
Item make_string_item(const char* str);
Item js_new_native_function(JsNativeP0 target);
Item js_new_native_function(JsNativeP1 target);
Item js_new_native_function(JsNativeP2 target);
Item js_new_native_function(JsNativeP3 target);
Item js_new_native_function(JsNativeP4 target);
Item js_new_native_function(JsNativeP5 target);
Item js_new_native_function(JsNativeP6 target);
Item js_new_native_function(JsNativeP7 target);
Item js_new_native_function(JsNativeP8 target);
Item js_new_native_function(JsNativeP0 target, int adapter_arity);
Item js_new_native_function(JsNativeP1 target, int adapter_arity);
Item js_new_native_function(JsNativeP2 target, int adapter_arity);
Item js_new_native_function(JsNativeP3 target, int adapter_arity);
Item js_new_native_function(JsNativeP4 target, int adapter_arity);
Item js_new_native_function(JsNativeP5 target, int adapter_arity);
Item js_new_native_function(JsNativeP6 target, int adapter_arity);
Item js_new_native_function(JsNativeP7 target, int adapter_arity);
Item js_new_native_function(JsNativeP8 target, int adapter_arity);
Item js_install_native_method(Item object, const char* name,
                              JsNativeP0 target, int adapter_arity);
Item js_install_native_method(Item object, const char* name,
                              JsNativeP1 target, int adapter_arity);
Item js_install_native_method(Item object, const char* name,
                              JsNativeP2 target, int adapter_arity);
Item js_install_native_method(Item object, const char* name,
                              JsNativeP3 target, int adapter_arity);
Item js_install_native_method(Item object, const char* name,
                              JsNativeP4 target, int adapter_arity);
Item js_install_native_method(Item object, const char* name,
                              JsNativeP5 target, int adapter_arity);
Item js_install_native_method(Item object, const char* name,
                              JsNativeP6 target, int adapter_arity);
Item js_install_native_method(Item object, const char* name,
                              JsNativeP7 target, int adapter_arity);
Item js_install_native_method(Item object, const char* name,
                              JsNativeP8 target, int adapter_arity);
Item js_install_native_method(Item object, const char* name, JsNativeP0 target);
Item js_install_native_method(Item object, const char* name, JsNativeP1 target);
Item js_install_native_method(Item object, const char* name, JsNativeP2 target);
Item js_install_native_method(Item object, const char* name, JsNativeP3 target);
Item js_install_native_method(Item object, const char* name, JsNativeP4 target);
Item js_install_native_method(Item object, const char* name, JsNativeP5 target);
Item js_install_native_method(Item object, const char* name, JsNativeP6 target);
Item js_install_native_method(Item object, const char* name, JsNativeP7 target);
Item js_install_native_method(Item object, const char* name, JsNativeP8 target);
Item js_install_native_constructor(Item object, const char* name, JsNativeP0 target);
Item js_install_native_constructor(Item object, const char* name, JsNativeP1 target);
Item js_install_native_constructor(Item object, const char* name, JsNativeP2 target);
Item js_install_native_constructor(Item object, const char* name, JsNativeP3 target);
Item js_install_native_constructor(Item object, const char* name, JsNativeP4 target);
Item js_install_native_constructor(Item object, const char* name, JsNativeP5 target);
Item js_install_native_constructor(Item object, const char* name, JsNativeP6 target);
Item js_install_native_constructor(Item object, const char* name, JsNativeP7 target);
Item js_install_native_constructor(Item object, const char* name, JsNativeP8 target);
Item js_install_native_constructor(Item object, const char* name,
                                   JsNativeP0 target, int adapter_arity);
Item js_install_native_constructor(Item object, const char* name,
                                   JsNativeP1 target, int adapter_arity);
Item js_install_native_constructor(Item object, const char* name,
                                   JsNativeP2 target, int adapter_arity);
Item js_install_native_constructor(Item object, const char* name,
                                   JsNativeP3 target, int adapter_arity);
Item js_install_native_constructor(Item object, const char* name,
                                   JsNativeP4 target, int adapter_arity);
Item js_install_native_constructor(Item object, const char* name,
                                   JsNativeP5 target, int adapter_arity);
Item js_install_native_constructor(Item object, const char* name,
                                   JsNativeP6 target, int adapter_arity);
Item js_install_native_constructor(Item object, const char* name,
                                   JsNativeP7 target, int adapter_arity);
Item js_install_native_constructor(Item object, const char* name,
                                   JsNativeP8 target, int adapter_arity);
Item js_initialize_native_constructor_prototype(Item constructor,
                                                Item prototype);
Item js_new_native_rest_function(JsNativeP1 target);
Item js_new_native_rest_function(JsNativeP2 target);
Item js_new_native_rest_function(JsNativeP3 target);
Item js_new_native_rest_function(JsNativeP4 target);
Item js_new_native_rest_function(JsNativeP5 target);
Item js_new_native_rest_function(JsNativeP6 target);
Item js_new_native_rest_function(JsNativeP7 target);
Item js_new_native_rest_function(JsNativeP8 target);
Item js_new_native_span_function(JsNativeSpan target);
Item js_new_native_this_span_function(JsNativeThisSpan target);
Item js_new_native_span_constructor(JsNativeSpan target);
Item js_new_native_body_constructor(JsNativeCallBody call_body,
                                     JsNativeConstructBody construct_body,
                                     int formal_length);
Item js_new_native_payload_function(JsNativeCallBody call_body,
                                    uint64_t payload,
                                    int formal_length);
Item js_new_native_constructor(JsNativeP0 target);
Item js_new_distinct_native_constructor(JsNativeP0 target);
Item js_new_native_constructor(JsNativeP1 target);
Item js_new_native_constructor(JsNativeP2 target);
Item js_new_native_constructor(JsNativeP3 target);
Item js_new_native_constructor(JsNativeP4 target);
Item js_new_native_constructor(JsNativeP5 target);
Item js_new_native_constructor(JsNativeP6 target);
Item js_new_native_constructor(JsNativeP7 target);
Item js_new_native_constructor(JsNativeP8 target);
Item js_new_native_constructor(JsNativeP0 target, int adapter_arity);
Item js_new_native_constructor(JsNativeP1 target, int adapter_arity);
Item js_new_native_constructor(JsNativeP2 target, int adapter_arity);
Item js_new_native_constructor(JsNativeP3 target, int adapter_arity);
Item js_new_native_constructor(JsNativeP4 target, int adapter_arity);
Item js_new_native_constructor(JsNativeP5 target, int adapter_arity);
Item js_new_native_constructor(JsNativeP6 target, int adapter_arity);
Item js_new_native_constructor(JsNativeP7 target, int adapter_arity);
Item js_new_native_constructor(JsNativeP8 target, int adapter_arity);
Item js_new_native_rest_constructor(JsNativeP1 target);
Item js_new_native_rest_constructor(JsNativeP2 target);
Item js_new_native_rest_constructor(JsNativeP3 target);
Item js_new_native_rest_constructor(JsNativeP4 target);
Item js_new_native_rest_constructor(JsNativeP5 target);
Item js_new_native_rest_constructor(JsNativeP6 target);
Item js_new_native_rest_constructor(JsNativeP7 target);
Item js_new_native_rest_constructor(JsNativeP8 target);
Item js_new_distinct_native_function(JsNativeP0 target);
Item js_new_distinct_native_function(JsNativeP1 target);
Item js_new_distinct_native_function(JsNativeP2 target);
Item js_new_distinct_native_function(JsNativeP3 target);
Item js_new_distinct_native_function(JsNativeP4 target);
Item js_new_distinct_native_function(JsNativeP5 target);
Item js_new_distinct_native_function(JsNativeP6 target);
Item js_new_distinct_native_function(JsNativeP7 target);
Item js_new_distinct_native_function(JsNativeP8 target);
Item js_new_distinct_native_constructor(JsNativeP0 target);
Item js_new_distinct_native_constructor(JsNativeP1 target);
Item js_new_distinct_native_constructor(JsNativeP2 target);
Item js_new_distinct_native_constructor(JsNativeP3 target);
Item js_new_distinct_native_constructor(JsNativeP4 target);
Item js_new_distinct_native_constructor(JsNativeP5 target);
Item js_new_distinct_native_constructor(JsNativeP6 target);
Item js_new_distinct_native_constructor(JsNativeP7 target);
Item js_new_distinct_native_constructor(JsNativeP8 target);
Item js_new_native_closure(JsNativeP1 target, int adapter_arity,
                           Item* env, int env_size);
Item js_new_native_closure(JsNativeP2 target, int adapter_arity,
                           Item* env, int env_size);
Item js_new_native_closure(JsNativeP3 target, int adapter_arity,
                           Item* env, int env_size);
Item js_new_native_closure(JsNativeP4 target, int adapter_arity,
                           Item* env, int env_size);
Item js_new_native_closure(JsNativeP5 target, int adapter_arity,
                           Item* env, int env_size);
Item js_new_native_env_span_closure(JsNativeEnvSpan target, int formal_length,
                                    Item* env, int env_size);
#endif
