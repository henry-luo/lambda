/**
 * JavaScript Runtime Functions for Lambda
 * 
 * These functions implement JavaScript semantics and are callable from MIR JIT code.
 * All functions use Item (uint64_t) as the primary data type.
 */
#pragma once
#include "../../lib/lambda_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"
#include "../core/name_identity.h"
#include <string.h>

typedef struct Runtime Runtime;
typedef struct EvalContext EvalContext;
struct ModuleDescriptor;

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
typedef Item (*JsNativeCallBody)(Item, Item, Item*, int, uint64_t*);
typedef Item (*JsNativeConstructBody)(Item, Item*, int, Item, uint64_t*);
typedef void (*JsDocumentSourceLoadObserver)(size_t source_length);

// Radiant registers this only while a document watchdog is active. Module
// loading reports recursively acquired source so its budget covers the graph.
void js_set_document_source_load_observer(JsDocumentSourceLoadObserver observer);
Item js_dom_media_poll_pending(void);
// Host cancellation is checked at guest boundaries and returned in the Item lane.
typedef bool (*JsExecutionInterruptCheck)(void);
void js_set_execution_interrupt_check(JsExecutionInterruptCheck check);
bool js_execution_interrupt_pending(void);
LAMBDA_RT_API Item js_execution_interrupt_status(void);

void js_map_promote_descriptor_kind(Map* m);

Item js_undefined(void);
LAMBDA_RT_API Item make_js_undefined(void);
LAMBDA_RT_API Item js_make_string_len(const char* str, int len);
// Take a StrBuf's contents as a GC string Item and free the buffer.
Item js_strbuf_take_item(StrBuf* sb);
LAMBDA_RT_API Item js_make_string(const char* str);
LAMBDA_RT_API Item js_domexception_new(Item message, Item name_arg);
LAMBDA_RT_API bool js_string_equals(Item value, const char* expected);
// string-only, allocation-free equality; the scalar result is also the MIR ABI.
int64_t js_string_equal(Item left, Item right);
bool js_is_vm_context_error(Item value);
// Returns ItemNull on every non-ASCII or semantically effectful case so the
// MIR caller can preserve the ordinary capability call on that edge.

const char* js_item_to_cstr(Item value, char* buf, int buf_size);

// Maps a well-known Symbol catalog selector to its stable NameId compatibility
// record, or to the cached core Symbol value. Property storage converts the
// latter only at the NameId boundary; callers otherwise retain JS identity.
NameId js_well_known_symbol_name_id(int64_t symbol_id);
LAMBDA_RT_API Item js_well_known_symbol_key(int64_t symbol_id);
LAMBDA_RT_API bool js_is_callable(Item value);
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

// Async IteratorClose uses this only between the raw `return` call and its
// immediate consumer, so an absent method never becomes an awaitable value.
#define JS_ITER_CLOSE_ABSENT_SENTINEL ITEM_JS_ITER_CLOSE_ABSENT_SENTINEL

LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(JS_DELETED_SENTINEL_VAL >> 56)),
                     "JS deleted sentinel tag must stay out of double discriminator space");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(JS_ITER_DONE_SENTINEL >> 56)),
                     "JS iterator-done sentinel tag must stay out of double discriminator space");
LAMBDA_STATIC_ASSERT(ITEM_TAG_IS_NON_DOUBLE((uint8_t)(JS_ITER_CLOSE_ABSENT_SENTINEL >> 56)),
                     "JS iterator-close sentinel tag must stay out of double discriminator space");

// Maximum module-level live bindings tracked in the compact slot table.
// Generated Unicode identifier tests declare thousands of top-level vars; keep
// this above those rows so they stay on the indexed binding path.

// =============================================================================
// Type Conversion Functions
// =============================================================================

/**
 * Convert a JavaScript value to a primitive.
 * Follows ECMAScript ToPrimitive algorithm.
 */
LAMBDA_RT_API Item js_to_number(Item value);
// ToNumeric preserves BigInt while otherwise applying ToNumber.
Item js_to_numeric(Item value);
// ECMAScript ToInt32, used by bitwise operations and Web IDL integer adapters.
int32_t js_to_int32(double value);
LAMBDA_RT_API Item js_to_string(Item value);
Item js_to_boolean(Item value);
Item js_to_object(Item value);
Item js_to_property_key(Item value);

/**
 * Check if a value is truthy according to JavaScript rules.
 */
LAMBDA_RT_API bool js_is_truthy(Item value);
// true for both physical representations of an ordinary JavaScript Array.
LAMBDA_RT_API bool js_is_js_array(Item value);
LAMBDA_RT_API bool js_is_object_value(Item value);
// Item-status helpers use the same merged lane as ordinary JS calls: a
// boolean true is success and an ERROR-tagged Item is the complete failure.
LAMBDA_RT_API Item js_status_ok(void);
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

// Keep ABI adapters declarative when their only job is forwarding arguments.
#define JS_FORWARD_ITEM(name, args, target, call_args) \
    extern "C" Item name args { return target call_args; }
#define JS_FORWARD_STATIC_ITEM(name, args, target, call_args) \
    static Item name args { return target call_args; }
#define JS_FORWARD_RETURN(type, name, args, target, call_args) \
    extern "C" type name args { return target call_args; }
#define JS_FORWARD_STATIC_RETURN(type, name, args, target, call_args) \
    static type name args { return target call_args; }
#define JS_FORWARD_VOID(name, args, target, call_args) \
    extern "C" void name args { target call_args; }
#define JS_FORWARD_STATIC_VOID(name, args, target, call_args) \
    static void name args { target call_args; }
#define JS_FORWARD_EXPRESSION(type, name, args, expression) \
    extern "C" type name args { return expression; }
#define JS_FORWARD_STATIC_EXPRESSION(type, name, args, expression) \
    static type name args { return expression; }
#define JS_FORWARD_LOCAL_RETURN(type, name, args, target, call_args) \
    type name args { return target call_args; }
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

  // && (returns last evaluated operand)
   // || (returns last evaluated operand)
Item js_logical_not(Item operand);           // !

// =============================================================================
// Bitwise Operators
// =============================================================================

Item js_bitwise_and(Item left, Item right);      // &
Item js_bitwise_or(Item left, Item right);       // |
Item js_bitwise_xor(Item left, Item right);      // ^
Item js_bitwise_not(Item operand);               // ~
LAMBDA_RT_API int64_t js_double_to_int32(double d);            // ToInt32 (safe for Infinity/NaN)
Item js_left_shift(Item left, Item right);       // <<
Item js_right_shift(Item left, Item right);      // >>
Item js_unsigned_right_shift(Item left, Item right); // >>>

// =============================================================================
// Unary Operators
// =============================================================================

Item js_unary_plus(Item operand);   // +x (convert to number)
Item js_unary_minus(Item operand);  // -x (negate)
Item js_typeof(Item value);         // typeof x
// Internal MIR/interpreter entries: the caller has already applied ToNumeric.
Item js_increment_numeric(Item numeric);
Item js_decrement_numeric(Item numeric);

// =============================================================================
// Object Functions
// =============================================================================

struct TypeMap;
LAMBDA_RT_API Item js_new_object(void);
Item js_new_object_with_typemap(struct TypeMap* tm);
Item js_new_literal_object_with_typemap(struct TypeMap* tm);
// A compiler-owned primitive object-literal recipe.  The recipe outlives the
// MIR code that references it; every invocation still creates fresh strings
// and a fresh ordinary object.
typedef enum JsStaticObjectValueKind {
    JS_STATIC_OBJECT_VALUE_IMMEDIATE = 0,
    JS_STATIC_OBJECT_VALUE_STRING
} JsStaticObjectValueKind;

typedef struct JsStaticObjectProperty {
    const char* key_chars;
    const char* string_chars;
    uint64_t immediate;
    int key_len;
    int string_len;
    uint8_t value_kind;
} JsStaticObjectProperty;

// A recursive compiler-owned literal recipe. It represents only side-effect-free
// literal syntax, while every invocation materializes a fresh JS value graph.
typedef enum JsStaticLiteralKind {
    JS_STATIC_LITERAL_IMMEDIATE = 0,
    JS_STATIC_LITERAL_STRING,
    JS_STATIC_LITERAL_ARRAY,
    JS_STATIC_LITERAL_OBJECT,
    JS_STATIC_LITERAL_HOLE
} JsStaticLiteralKind;

typedef struct JsStaticLiteralRecipe {
    const char* key_chars;
    const char* string_chars;
    const struct JsStaticLiteralRecipe* children;
    uint64_t immediate;
    int key_len;
    int string_len;
    int length;
    uint8_t kind;
} JsStaticLiteralRecipe;

// An array's companion property map is created on first use — index accessors,
// non-index keys and attribute bits all live there. Callers that are about to
// write must go through this; a bare js_array_props() read can be NULL.
Map* js_array_props_ensure(Array* arr);
// Allocate a JS object with its immutable semantic metadata selected before
// the object is returned to any caller. The class ID is a stable JsClass value.
Item js_new_object_with_class(int class_id);
// Native carriers use the same pre-publication metadata-qualified empty shape.
struct TypeMap* js_object_type_for_class(int class_id);
Item js_new_class_function(void);
void js_set_class_constructor(Item class_function, Item constructor_body);
void js_set_class_instance_prototype(Item class_function, Item prototype);
void js_set_class_superclass(Item class_function, Item superclass);
// ClassDefinitionEvaluation heritage links shared by both tiers: the prototype
// object's parent (null for `extends null`), then the constructor's parent.
Item js_class_heritage_prototype_parent(Item superclass);
void js_set_class_constructor_parent(Item class_function, Item superclass);
bool js_is_class_constructor_value(Item value);
// canonical typed-array index parsing is shared across frontend property authorities.
bool js_ta_key_canonical_numeric(Item key, double* numeric_index, bool* is_negative_zero);
bool js_ta_numeric_index_valid(Item object, double numeric_index, bool negative_zero, int* index);
// Shared numeric property-key materialization for JS element/descriptor code.
Item js_property_index_key(int64_t index);
String* js_property_index_name(int64_t index);
const char* js_property_index_chars(int64_t index, int* out_len);
LAMBDA_RT_API Item js_get_key_default(Item object, Item key);
LAMBDA_RT_API Item js_get_key_cstr(Item object, const char* key);
// Receiver-explicit property Get used by prototype, accessor, and Proxy paths.
Item js_get_key_core(Item object, Item key, Item receiver);
LAMBDA_RT_API Item js_set_key_default(Item object, Item key, Item value);
// Receiver-explicit property Set belongs to the completion kernel.
Item js_set_completion_with_key(Item target, Item key, Item value,
                                Item receiver);
Item js_dataset_owner(Item dataset);
bool js_dataset_property_via_api(Item dataset, Item key, Item value, bool write, Item* out);
Item js_set_primitive_completion(Item target, Item key, Item value);
Item js_set_function_prototype_completion(Item target, Item value);
Item js_set_error_property_completion(Item target, Item key, Item value);
// Internal DefineOwn storage write; it never dispatches inherited accessors.
Item js_define_own_key_storage(Item object, Item key, Item value);
LAMBDA_RT_API Item js_set_key_cstr(Item object, const char* key, Item value);
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
void js_dom_install_interface(Item global, const char* name);
LAMBDA_RT_API Item js_create_data_property(Item object, Item key, Item value);
Item js_get_reference(Item object, Item key);
Item js_get_name_id(Item object, NameId name_id);
bool js_try_get_own_named_data_no_gc(Item object, Item key, Item* out_value);

// =============================================================================
// Array Functions
// =============================================================================

LAMBDA_RT_API Item js_array_new(int length);
// Build a fresh dense array from compiler-owned inline Number Items. The
// description is immutable; the returned array and its elements stay ordinary.
// Preserve array-literal elisions as absent indexed properties.
Item js_array_hole(void);
// Allocate an array with an explicit immutable JS class carrier for branded
// array-backed host objects such as FileList.
Item js_array_new_with_class(int length, int class_id);
bool js_is_ordinary_numeric_array(Item value);
bool js_array_promote_numeric(Item array);
Item js_array_new_from_item(Item arg);
Item js_elements_get(Item array, Item index);
Item js_elements_set(Item array, Item index, Item value);
LAMBDA_RT_API Item js_elements_get_int(Item array, int64_t index);
LAMBDA_RT_API Item js_elements_set_int(Item array, int64_t index, Item value);
// T12-4: physical own-element heads used before the allocating property
// kernels. They accept only a present ordinary dense slot and cannot allocate.
bool js_array_try_get_existing_own_dense_no_gc(Item array, int64_t index,
                                               Item* out_value);
// Returns an existing direct Item through an ArrayProps companion, or the
// internal hole sentinel when scalar homes, descriptors, holes or another array
// kind require the complete element read.
bool js_array_try_set_existing_own_dense_no_gc(Item array, int64_t index,
                                               Item value);
// Stores an already-native Number only into an existing ordinary packed slot.
// False keeps the caller on the full Set continuation.
// Returns a boolean Set completion for the narrow ordinary-array index fast
// path, or ItemNull when descriptor/prototype/exotic checks require fallback.
Item js_elements_set_int_completion(Item array, int64_t index, Item value);
Item js_elements_set_int_direct(Item array, int64_t index, Item value);
// Neutral sparse storage shared by the full engine and the MVP semantic adapter.
struct SparseArrayMap* js_sparse_array_map_new(Map* existing, struct TypeMap* empty_shape);
bool js_sparse_array_map_set(struct SparseArrayMap* map, int64_t index, Item value);
bool js_array_sparse_get(Array* array, int64_t index, Item* value);
bool js_array_should_store_sparse_for_index(Array* array, int64_t index);
bool js_array_should_keep_length_sparse(Array* array, int64_t length);
void js_array_try_promote_sparse_dense(Item array, bool gc_items);
int64_t js_array_sparse_delete_index(Item array, int64_t index);
int64_t js_array_sparse_has_index(Item array, int64_t index);
Item js_array_sparse_get_index(Item array, int64_t index);
int64_t js_array_sparse_collect_indices(Item array, int64_t start, int64_t end, int64_t* indices, int64_t cap);
Item js_array_define_dense_element_direct(Item array, int64_t index, Item value);
LAMBDA_RT_API int64_t js_array_length(Item array);

// Walk a JS array's elements in index order, binding `name` to each element.
// The index and the length are loop internals — a body that needs either one
// writes the loop out explicitly instead. `array` is evaluated once per step,
// so pass a plain variable, not an expression with side effects.
#define JS_ARRAY_FOREACH(name, array) \
    for (int64_t name##__index = 0, name##__count = js_array_length(array); \
         name##__index < name##__count; name##__index++) \
        if (Item name = js_elements_get_int(array, name##__index); true)

LAMBDA_RT_API Item js_array_push(Item array, Item value);
void js_array_push_item_direct(Array* arr, Item value);
double js_math_pow_d(double base, double exp);

// =============================================================================
// Function Functions
// =============================================================================

// Compiler-pending functions preserve ordinary name/length descriptors while
// avoiding their backing-map allocation until an observable operation needs it.
bool js_function_materialize_lazy_metadata_property(Item function, Item key);
void js_function_materialize_all_lazy_metadata_properties(Item function);
bool js_function_has_lazy_metadata_property(Item function, Item key);
// JC15: pack actuals [start, argc) into a rest array; `args` stays caller-rooted.
Item js_args_rest_array(Item* args, int64_t start, int64_t argc);
struct AstFuncNode;
struct JsScript;
struct JsInterpEnv;
struct JsAstLiteralCacheEntry;
// Retained AST literal values are context-owned precise roots. `ast_image` is
// the immutable template identity, not an execution Script shell.
struct JsAstLiteralCacheEntry* js_ast_literal_cache_acquire(const void* ast_image,
    uint32_t literal_count);
bool js_ast_literal_cache_read(const struct JsAstLiteralCacheEntry* entry,
    uint32_t slot, Item* out);
bool js_ast_literal_cache_write(struct JsAstLiteralCacheEntry* entry,
    uint32_t slot, Item value);
// Return the source text mandated by Function.prototype.toString for a parsed
// method/function node. Both MIR and AST execution use this canonical trimming.
bool js_function_source_span(const char* source, size_t source_length,
                             const struct AstFuncNode* function,
                             const char** text_out, uint32_t* length_out);
Item js_new_interpreted_function(struct AstFuncNode* function,
                                 struct JsScript* script,
                                 struct JsInterpEnv* environment,
                                 uint32_t flags);
void js_set_formal_length(Item fn_item, int length);
LAMBDA_RT_API Item* js_alloc_env(int count);
// Allocate a closure environment and populate it in one step. A native
// continuation's env is fixed at capture time, so the slot count and the slot
// values are one decision; splitting them leaves a half-built env visible to
// anything that walks the closure while the remaining stores are pending.
Item* js_alloc_env1(Item a);
LAMBDA_RT_API Item* js_alloc_env2(Item a, Item b);
LAMBDA_RT_API Item* js_alloc_env3(Item a, Item b, Item c);

// Unpack a native continuation's closure environment. A continuation that
// outlives its environment has nothing left to do, which is not an error.
#define JS_ENV_OR_UNDEFINED(name, env_item) \
    Item* name = (Item*)(uintptr_t)(env_item).item; \
    if (!name) return make_js_undefined()

void js_env_rehome_scalars(Item* env);
LAMBDA_RT_API void js_set_function_name(Item fn_item, Item name_item);
void js_set_function_source(Item fn_item, Item source_item);
void js_set_function_home_class(Item fn_item, Item home_class);
void js_mark_derived_constructor_func(Item fn_item);
Item js_get_constructor(Item name_item);
Item js_get_intrinsic_prototype_for_class(int class_id);
Item js_get_typed_array_per_type_proto(int element_type);
// O5: the one RangeError message for the call-depth guard, shared by the
// kernel and the JIT's inline source-invocation guard (same counter/limit).
#define JS_CALL_STACK_EXCEEDED_MESSAGE "Maximum call stack size exceeded"
// JC13: the sole dynamic-call entry; the named adapters below only fix its
// two ownership operands.
Item js_call(Item func_item, Item this_val, Item* args, int arg_count,
             uint64_t* result_home, bool args_prerooted);
// JC21: an AST call site's entry for a pre-rooted argument span. It takes the
// kernel's AST direct instance when the callee's facts allow, else js_call.
Item js_call_from_ast(Item callee, Item this_val, Item* args, int arg_count,
                      uint64_t* result_home);
LAMBDA_RT_API Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count);
Item js_call_accessor_getter(Item getter, Item receiver);
Item js_call_function_into(Item func_item, Item this_val, Item* args,
                           int arg_count, uint64_t* result_home);
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
void js_set_stack_size_kb(int64_t kb);
Item js_apply_function(Item func_item, Item this_val, Item args_array);
Item js_super_call_class_into(Item callee, Item this_val, Item* args, int argc,
                              uint64_t* result_home);
Item js_super_apply_class_into(Item callee, Item this_val, Item args_array,
                               uint64_t* result_home);
Item js_super_call_native(Item callee, Item this_val, Item* args, int argc);
Item js_super_apply_native(Item callee, Item this_val, Item args_array);
Item js_construct_array_like(Item constructor, Item args_array, Item new_target);
Item js_bind_function(Item func_item, Item bound_this, Item* bound_args, int bound_argc);
void js_function_root_item_if_needed(void* fn, Item* slot);
Item js_func_bind(Item func_item, Item bound_this, Item* bound_args, int bound_argc);
Item js_new_function_from_string(Item* args, int argc);
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
Item js_builtin_eval(Item code_item);
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
LAMBDA_RT_API Item js_get_this();
Item js_get_lexical_this_binding(void);
Item js_resolve_lexical_this(Item this_val);
Item js_get_new_target();
Item js_super_bind_this(Item this_val, Item construct_result);
Item js_get_super_this_value(void);
Item js_get_super_constructor_from_receiver(Item receiver, Item fallback_ctor);
Item js_super_get_base(Item receiver);
Item js_super_property_get_from_base(Item receiver, Item base, Item key);
Item js_super_property_set_from_base(Item receiver, Item base, Item key, Item value,
                                     int64_t strict);
Item js_build_arguments_object(void);
// Materialize an arguments exotic object for a deferred AST activation without
// borrowing the ambient callback's transient call span.
Item js_build_arguments_object_for_call(Item* args, int argc, int64_t is_strict,
                                        Item callee);

// Get the native function pointer from a JsFunction Item (handles JsFunction layout)
void* js_function_get_ptr(Item fn_item);

// Get the parameter count from a JsFunction Item
int js_function_get_arity(Item fn_item);

// =============================================================================
// Console Functions
// =============================================================================

Item js_console_log(Item value);
Item js_console_count_fn(Item label);
Item js_console_countReset_fn(Item label);
Item js_console_time_fn(Item label);
Item js_console_timeEnd_fn(Item label);
Item js_console_timeLog_fn(Item label);
Item js_console_clear_fn(void);
Item js_console_group_fn(Item label);
Item js_console_groupEnd_fn(void);
Item js_console_trace_fn(Item label);
Item js_console_dir_fn(Item object);
Item js_console_table_fn(Item data);
Item js_console_assert_fn(Item condition, Item message);

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
Item js_process_stderr_write(Item str_item);
void js_store_process_argv(int argc, const char** argv);
const char* const* js_process_bootstrap_arguments(int* argc);
void js_set_process_argv(int argc, const char** argv);
void js_store_process_exec_argv(int argc, const char** argv);
void js_set_process_exec_argv(int argc, const char** argv);
Item js_get_process_argv(void);
Item js_get_process_exec_argv(void);
Item js_get_process_object_value(void);
Item js_get_profiled_process_object(void);
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

Item js_string_fromCharCode(Item code_item);
Item js_string_fromCharCode_array(Item arr_item);
Item js_string_fromCodePoint(Item code_item);
Item js_string_fromCodePoint_array(Item arr_item);
struct JsStringCodeOps {
    Item (*read)(void* source, int64_t index);
    Item (*to_number)(Item value);
    Item (*range_error)();
};
Item js_string_from_codes_with_ops(void* source, int64_t count, bool code_points,
    const struct JsStringCodeOps* ops);

// RegExp protocols share algorithms; front ends retain property/coercion and native-slot policy.
// Callbacks may enter guest code. Inputs and all cross-callback results are precisely rooted.
typedef struct JsRegExpOps {
    Item (*get)(Item, const char*);
    Item (*set_last_index)(Item, Item);
    Item (*to_string)(Item);
    Item (*to_number)(Item);
    bool (*is_object)(Item);
    bool (*is_callable)(Item);
    bool (*truthy)(Item);
    bool (*same_value)(Item, Item);
    Item (*call)(Item, Item, Item*, int);
    Item (*builtin_exec)(Item, Item, bool);
    Item (*intrinsic_exec)(void);
    Item (*intrinsic_get)(Item, int);
    Item (*array_new)(int);
    Item (*append)(Item, Item);
    Item (*type_error)(const char*);
    Item (*match_bulk)(Item, Item, bool*);
    Item (*match_flags)(Item);
    int64_t (*advance)(Item, String*, int64_t, bool);
    Item (*get_key)(Item, Item);
    Item (*to_object)(Item);
    bool (*is_constructor)(Item);
    Item (*construct)(Item, Item*, int);
    Item (*default_constructor)(void);
    Item (*replace_bulk)(Item, Item, Item, bool*);
    bool (*split_utf8_native)(Item);
    bool (*native_is_regexp)(Item);
    Item (*create)(Item, Item);
} JsRegExpOps;
// native matchers provide UTF-16 offsets and pool-owned names; unmatched spans start at -1.
typedef struct JsRegExpIndexSpan { int64_t start, end; } JsRegExpIndexSpan;
typedef struct JsRegExpIndexName { const char* chars; int length, capture; } JsRegExpIndexName;
struct JsRegexpLastMatch;
const char* js_regexp_legacy_name(int index);
bool js_regexp_legacy_get(struct JsRegexpLastMatch* state, const char* name, int length, Item* out);
Item js_regexp_install_legacy_properties(Item constructor);
Item js_regexp_update_last_match_spans(struct JsRegexpLastMatch* state, Item input,
    const int* starts, const int* ends, int count, bool utf16);
// callers prove native exec, writable lastIndex, string replacement, no captures,
// and byte spans that do not split a UTF-16 surrogate pair (or an ASCII subject).
typedef struct JsRegExpReplaceSpanOps {
    int (*match)(void*, const char*, int, int, int*, int*); // found=1, absent=0, error<0
    Item (*record)(void*, Item, int, int);
    Item (*failure)(int);
} JsRegExpReplaceSpanOps;
Item js_regexp_replace_spans(Item regex, Item input, Item replacement, void* matcher,
    const JsRegExpOps* values, const JsRegExpReplaceSpanOps* spans);
typedef struct JsRegExpIndicesOps {
    const JsRegExpOps* values;
    Item (*null_object)(void);
    Item (*create_data)(Item object, Item key, Item value);
} JsRegExpIndicesOps;
Item js_regexp_attach_indices_with_ops(Item result, const JsRegExpIndexSpan* spans, int count,
    const JsRegExpIndexName* names, int named, const JsRegExpIndicesOps* ops);
// native slots never observe public source/flags; allocation consumes rooted strings.
typedef struct JsRegExpConstructorOps {
    const JsRegExpOps* values;
    Item (*native_slot)(Item regexp, bool flags);
    Item (*prototype)(Item new_target);
    Item (*initialize)(Item pattern, Item flags, Item prototype);
    Item (*replace)(Item receiver, Item replacement);
} JsRegExpConstructorOps;
Item js_regexp_compile_with_ops(Item receiver, Item pattern, Item flags,
    const JsRegExpConstructorOps* ops);
Item js_regexp_is_regexp_with_ops(Item value, bool* result, const JsRegExpOps* ops);
Item js_regexp_initialize_with_ops(Item pattern, Item flags, Item prototype,
    const JsRegExpConstructorOps* ops);
Item js_regexp_construct_with_ops(Item pattern, Item flags, Item constructor,
    Item new_target, const JsRegExpConstructorOps* ops);
// storage reads are nonallocating; the caller registers the two Item output slots before read.
typedef struct JsRegExpIteratorOps {
    Item (*create)(Item regexp, Item string, bool global, bool unicode);
    bool (*read)(Item iterator, Item* state, bool* flags); // state: regexp/string; flags: global/unicode/done
    Item (*set_done)(Item iterator);
    Item (*result)(Item value, bool done);
} JsRegExpIteratorOps;
Item js_regexp_match_all_with_ops(Item receiver, Item string,
    const JsRegExpOps* ops, const JsRegExpIteratorOps* iterators);
Item js_regexp_match_all_next_with_ops(Item receiver,
    const JsRegExpOps* ops, const JsRegExpIteratorOps* iterators);
Item js_regexp_string_match_all_with_ops(Item receiver, Item regexp, const JsRegExpOps* ops);
Item js_regexp_exec_with_ops(Item receiver, Item string, const JsRegExpOps* ops, bool test_only);
Item js_regexp_protocol_with_ops(int builtin_id, Item receiver, Item* args, int argc, const JsRegExpOps* ops);
int64_t js_regex_advance_string_index_units(String* string, int64_t index, bool full_unicode);
Item js_str_substring_utf16(Item string, int64_t start, int64_t end);
struct StrBuf;
// caller roots the borrowed string spans and captures; generic positions are UTF-16 units.
Item js_apply_replacement_from_items(struct StrBuf* output, const char* replacement, int replacement_length,
    const char* source, int source_length, int64_t position, const char* matched, int matched_length,
    Item* captures, int capture_count, Item groups, const JsRegExpOps* ops, Item source_item, bool utf16);

// =============================================================================
// v5: Console multi-argument log
// =============================================================================

void js_console_log_multi(Item* args, int argc);

// =============================================================================
// v5: Additional binary operators
// =============================================================================

Item js_instanceof(Item left, Item right);
Item js_in(Item key, Item object);

// =============================================================================
// v5: Object utilities
// =============================================================================

Item js_object_keys(Item object);
Item js_typed_array_enumerable_custom_keys(Item object);
Item js_for_in_keys(Item object);
// enumeration policies return undefined for an absent own key, bool for present, or an error.
typedef struct JsForInOps {
    Item (*to_object)(Item);
    Item (*prototype)(Item);
    Item (*own_enumerability)(Item, Item key);
} JsForInOps;
Item js_for_in_key_is_live_with_ops(Item object, Item key, const JsForInOps* ops);
bool js_for_in_key_is_live(Item object, Item key);
Item js_object_get_own_property_names(Item object);
Item js_object_get_own_property_symbols(Item object);
Item js_to_string_val(Item value);
// Phase-5C: js_make_getter_key/js_make_setter_key removed (no callers).


// =============================================================================
// v8: Object & Global extensions
// =============================================================================

Item js_object_create(Item proto);
LAMBDA_RT_API Item js_object_define_property(Item obj, Item name, Item descriptor);
Item js_object_define_properties(Item obj, Item props);
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
void js_performance_record_resource(const char* url, const char* initiator,
    double start_ms, double end_ms);
bool js_performance_is_target(Item value);
Item js_date_now(void);
double js_date_number_to_double(Item value);
Item js_date_now_string(void);
Item js_date_new(void);
Item js_date_new_from(Item value);
Item js_date_utc(Item args_array);
Item js_date_method(Item date_obj, int method_id);
Item js_date_setter(Item date_obj, int method_id, Item arg0, Item arg1, Item arg2, Item arg3);
Item js_date_new_multi(Item args_array);
Item js_date_parse(Item str_item);

// front ends share calendar kernels while retaining their own internal slots and completions.
typedef struct JsDateOps {
    Item (*number)(Item);
    Item (*read)(Item, bool*);
    Item (*store)(Item, double);
    Item (*error)(bool, const char*);
} JsDateOps;
Item js_date_get_time_value(Item receiver, bool* found);
Item js_date_store_time_value(Item receiver, double milliseconds);
double js_date_time_clip(double value);
double js_date_components_time(double* parts, bool local, bool relative_year);
Item js_date_parse_text(String* text);
Item js_date_method_with_ops(Item receiver, int method, const JsDateOps* ops);
Item js_date_setter_with_ops(Item receiver, int method, Item arg0, Item arg1, Item arg2, Item arg3,
    const JsDateOps* ops);
// generic Array operations share algorithms; the frontend supplies property and completion semantics.
typedef enum JsArrayIndexOp {
    JS_ARRAY_INDEX_GET, JS_ARRAY_INDEX_HAS, JS_ARRAY_INDEX_SET,
    JS_ARRAY_INDEX_DELETE, JS_ARRAY_INDEX_CREATE
} JsArrayIndexOp;
typedef struct JsArrayOps {
    const struct JsBufferViewOps* values;
    Item (*object)(Item);
    Item (*index)(Item, int64_t, Item, JsArrayIndexOp); // index -1 selects length
    Item (*create)(int64_t);
    bool (*callable)(Item);
    bool (*constructor)(Item);
    Item (*construct)(Item, Item*, int);
    Item (*sort)(Item, Item);
} JsArrayOps;
extern const JsArrayOps js_array_default_ops;
Item js_array_method_with_ops(int builtin_id, Item receiver, Item* args, int argc,
    const JsArrayOps* ops);
Item js_array_of_with_ops(Item constructor, Item* args, int argc, const JsArrayOps* ops);

Item js_map_collection_new(void);
// internal lexical tables use canonical collection storage without observable prototype lookup.
Item js_map_collection_storage_new(void);
Item js_map_collection_new_from(Item iterable);
Item js_set_collection_new(void);
Item js_set_collection_new_from(Item iterable);
Item js_collection_method(Item obj, int method_id, Item arg1, Item arg2);
// Shared collection storage preserves the existing native ephemeron carrier;
// front ends supply language operations without creating another JS realm.
typedef struct JsCollectionOps {
    bool (*is_object)(Item);
    bool (*is_callable)(Item);
    Item (*get)(Item, const char*);
    Item (*call)(Item, Item, Item*, int);
    Item (*iterator)(Item);
    Item (*step)(Item); // yielded value or JS_ITER_DONE_SENTINEL
    Item (*close)(Item, Item);
    Item (*error)(bool range, const char*);
    bool (*weak_key)(Item);
    Item (*number)(Item);
    bool (*truthy)(Item);
    Item (*iterator_from_method)(Item, Item);
} JsCollectionOps;
typedef struct JsObjectAccessorOps {
    const JsCollectionOps* values;
    Item (*to_object)(Item);
    Item (*key)(Item);
    Item (*define)(Item, Item, Item, bool getter);
    Item (*lookup)(Item, Item, bool getter, bool* found);
    Item (*prototype)(Item);
} JsObjectAccessorOps;
Item js_object_legacy_accessor_with_ops(Item receiver, Item property, Item callback,
    bool define, bool getter, const JsObjectAccessorOps* ops);
Item js_collection_create_with_storage(struct TypeMap* shape, Pool* pool, int kind, bool weak);
bool js_collection_has_brand(Item object, int kind, bool weak);
bool js_can_be_held_weakly(Item key);
Item js_weak_validate_input(bool registry, Item input, const JsCollectionOps* ops);
Item js_weak_create_with_storage(struct TypeMap* shape, bool registry, Item input, const JsCollectionOps* ops);
Item js_weak_method_with_ops(int operation, Item receiver, Item* args, int count, const JsCollectionOps* ops);
// host job boundary only: release kept targets and run pending native cleanup jobs.
Item js_weak_job_checkpoint(void);
Item js_collection_add_entries_with_ops(Item collection, Item iterable, bool is_map,
    const JsCollectionOps* ops);
Item js_collection_method_with_ops(Item object, int method, Item arg1, Item arg2,
    const JsCollectionOps* ops);
// Cursors hold only stable native positions; callers root the collection. A
// storage scope pins ordered ordinals until every normal or abrupt exit.
typedef struct JsCollectionCursor { uintptr_t position; bool started; } JsCollectionCursor;
typedef struct JsCollectionStorageOps {
    bool (*brand)(Item, int kind);
    int64_t (*size)(Item);
    Item (*create)(int kind);
    Item (*get)(Item, Item);
    Item (*has)(Item, Item);
    Item (*put)(Item, Item, Item);
    Item (*erase)(Item, Item);
    void (*begin)(Item);
    bool (*next)(Item, JsCollectionCursor*, Item* key, Item* value);
    void (*end)(Item);
} JsCollectionStorageOps;
extern const JsCollectionStorageOps js_collection_default_storage;
Item js_collection_for_each_with_ops(Item collection, Item callback, Item this_arg,
    bool is_set, const JsCollectionOps* ops, const JsCollectionStorageOps* storage);
Item js_set_operation_with_ops(Item receiver, Item other, int operation,
    const JsCollectionOps* ops, const JsCollectionStorageOps* storage);


LAMBDA_RT_API void js_set_prototype(Item object, Item prototype);
void js_object_proto_setter(Item object, Item value);
LAMBDA_RT_API void js_mark_non_enumerable(Item object, Item name);
void js_mark_non_writable(Item object, Item name);
void js_mark_non_configurable(Item object, Item name);
// Allocate a function's ordinary properties Map without materializing a data
// property. Virtual accessor descriptors use this before private-shape setup.
Map* js_function_props_ensure(Item fn);
void js_func_init_property(Item fn, Item key, Item value);
void js_mark_all_non_enumerable(Item object);
Item js_new_number_wrapper(Item arg);
Item js_new_number_checked(Item arg);
Item js_new_boolean_wrapper(Item arg);
Item js_new_string_wrapper(Item arg);
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
typedef struct JsTemplateObjectOps {
    Item (*array_new)(int);
    Item (*store)(Item, int, Item);
    Item (*define_raw)(Item, Item);
    Item (*freeze)(Item);
} JsTemplateObjectOps;
Item js_build_template_object_with_ops(Item* cooked, Item* raw, int count, const JsTemplateObjectOps* ops);
Item js_build_template_object_cached(Item* cooked, Item* raw, int count, int64_t site_id);
Item js_build_template_object_cached_with_ops(Item* cooked, Item* raw, int count,
    int64_t site_id, const JsTemplateObjectOps* ops, const void* site_owner);
typedef enum JsStringIntrinsicOp {
    JS_STRING_INTRINSIC_CHAR_AT,
    JS_STRING_INTRINSIC_CHAR_CODE_AT,
    JS_STRING_INTRINSIC_INDEX_OF,
    JS_STRING_INTRINSIC_INCLUDES,
    JS_STRING_INTRINSIC_SLICE,
    JS_STRING_INTRINSIC_SUBSTRING,
    JS_STRING_INTRINSIC_TO_LOWER_CASE,
    JS_STRING_INTRINSIC_TO_UPPER_CASE,
    JS_STRING_INTRINSIC_TRIM,
    JS_STRING_INTRINSIC_SPLIT,
    JS_STRING_INTRINSIC_REPLACE,
    JS_STRING_INTRINSIC_MATCH,
    JS_STRING_INTRINSIC_SEARCH,
    JS_STRING_INTRINSIC_STARTS_WITH,
    JS_STRING_INTRINSIC_ENDS_WITH,
    JS_STRING_INTRINSIC_REPEAT,
    JS_STRING_INTRINSIC_PAD_START,
    JS_STRING_INTRINSIC_PAD_END,
    JS_STRING_INTRINSIC_TO_STRING,
    JS_STRING_INTRINSIC_VALUE_OF,
    JS_STRING_INTRINSIC_TRIM_START,
    JS_STRING_INTRINSIC_TRIM_END,
    JS_STRING_INTRINSIC_CODE_POINT_AT,
    JS_STRING_INTRINSIC_NORMALIZE,
    JS_STRING_INTRINSIC_CONCAT,
    JS_STRING_INTRINSIC_AT,
    JS_STRING_INTRINSIC_LAST_INDEX_OF,
    JS_STRING_INTRINSIC_LOCALE_COMPARE,
    JS_STRING_INTRINSIC_REPLACE_ALL,
    JS_STRING_INTRINSIC_MATCH_ALL,
    JS_STRING_INTRINSIC_IS_WELL_FORMED,
    JS_STRING_INTRINSIC_TO_WELL_FORMED,
    JS_STRING_INTRINSIC_ANCHOR,
    JS_STRING_INTRINSIC_BIG,
    JS_STRING_INTRINSIC_BLINK,
    JS_STRING_INTRINSIC_BOLD,
    JS_STRING_INTRINSIC_FIXED,
    JS_STRING_INTRINSIC_FONTCOLOR,
    JS_STRING_INTRINSIC_FONTSIZE,
    JS_STRING_INTRINSIC_ITALICS,
    JS_STRING_INTRINSIC_LINK,
    JS_STRING_INTRINSIC_SMALL,
    JS_STRING_INTRINSIC_STRIKE,
    JS_STRING_INTRINSIC_SUB,
    JS_STRING_INTRINSIC_SUP,
    JS_STRING_INTRINSIC_SUBSTR,
    JS_STRING_INTRINSIC_TO_LOCALE_LOWER_CASE,
    JS_STRING_INTRINSIC_TO_LOCALE_UPPER_CASE,
} JsStringIntrinsicOp;
typedef struct JsStringTextOps {
    Item (*to_string)(Item);
    Item (*range_error)(const char*);
} JsStringTextOps;
// receiver is already a primitive string; object arguments use only the supplied guest policy.
Item js_string_text_with_ops(Item string, JsStringIntrinsicOp operation,
    Item argument, const JsStringTextOps* ops);
Item js_string_substr_with_ops(Item primitive_string, Item start, Item length,
    const struct JsBufferViewOps* ops);

typedef struct JsStringRawOps {
    Item (*to_object)(Item);
    Item (*get)(Item, Item);
    Item (*to_number)(Item);
    Item (*to_string)(Item);
} JsStringRawOps;
Item js_string_raw_with_ops(Item* args, int argc, const JsStringRawOps* ops);
void js_reset_template_registry(void);
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
Item js_array_from_with_mapper_this(Item iterable, Item mapFn, Item this_arg);
Item js_json_parse(Item str_item);
Item js_json_parse_full(Item str_item, Item reviver);
typedef struct JsJsonReviveOps {
    const JsCollectionOps* values;
    Item (*get)(Item, Item);
    Item (*is_array)(Item);
    Item (*keys)(Item);
    Item (*write)(Item, Item, Item);
    Item (*context)(void*, Item, Item, Item);
} JsJsonReviveOps;
Item js_json_revive_with_ops(Item holder, Item key, Item reviver, void* state,
    const JsJsonReviveOps* ops);
LAMBDA_RT_API Item js_json_stringify(Item value);
Item js_json_stringify_full(Item value, Item replacer, Item space);
typedef struct JsJsonStringifyOps {
    const JsCollectionOps* values;
    Item (*get)(Item, Item);
    Item (*is_array)(Item);
    Item (*keys)(Item);
    Item (*string)(Item);
    Item (*unbox)(Item); // nonallocating internal payload, or undefined for non-wrappers
    Item (*object)(void);
    Item (*create)(Item, Item, Item);
    Item (*raw)(Item); // optional raw-JSON extension; undefined means an ordinary value
} JsJsonStringifyOps;
Item js_json_stringify_with_ops(Item value, Item replacer, Item space,
    const JsJsonStringifyOps* ops);
Item js_delete_property(Item obj, Item key);

// v15: fetch() API
Item js_fetch(Item url, Item options);
Item js_fetch_with_initiator(Item url, Item options, const char* initiator);
void js_fetch_reset(void);
void js_fetch_dispatch_pending(void);
// Set base directory for resolving relative fetch() URLs to local files
// (used by `lambda.exe js --document <html>` so tests can fetch sibling
// resources from disk without an HTTP server).
void js_fetch_set_base_path(const char* dir_path);

// =============================================================================
// Exception Handling (try/catch/throw)
// =============================================================================

// D8.4.3: throw returns the merged ERROR Item; routing carries that Item.
LAMBDA_RT_API Item js_throw_value(Item value);
// Convert a routed ERROR Item to the JavaScript value observable at a catch,
// rejection, or host boundary.  It never reads or clears ambient state.
LAMBDA_RT_API Item js_error_lane_payload(Item lane);
// Format one routed lane for a host diagnostic without consulting ambient
// ambient state.  `out` is caller-owned and may be empty on failure.
void js_error_lane_format(Item lane, char* out, int out_size);

/** v20: Throw a RangeError with the given message. */
LAMBDA_RT_API Item js_throw_range_error(const char* message);
LAMBDA_RT_API Item js_throw_type_error(const char* message);
Item js_throw_syntax_error(Item message);
Item js_throw_reference_error(Item message);
// Tune8 §2.3: unified entry point for MIR-emitted throws (kind=0:SyntaxError,
// kind=1:ReferenceError). The named wrappers above are kept for C callers.
Item js_throw_named_error(int64_t kind, Item message);

/** Throw TypeError/RangeError with Node.js error code (e.g. ERR_INVALID_ARG_TYPE). */
LAMBDA_RT_API Item js_throw_type_error_code(const char* code, const char* message);
LAMBDA_RT_API Item js_throw_range_error_code(const char* code, const char* message);
// printf-style variants: format into a bounded buffer and throw. They replace
// the `char msg[N]; snprintf(...); throw(msg)` stanza at the call sites.
LAMBDA_RT_API Item js_throw_type_errorf(const char* format, ...);
LAMBDA_RT_API Item js_throw_type_error_codef(const char* code, const char* format, ...);
LAMBDA_RT_API Item js_throw_range_error_codef(const char* code, const char* format, ...);
LAMBDA_RT_API Item js_throw_error_with_code(const char* code, const char* message);

/**
 * Node.js-style error helpers for common validation patterns.
 * Format: 'The "name" argument must be of type expected. Received type actual'
 */
LAMBDA_RT_API Item js_throw_invalid_arg_type(const char* name, const char* expected, Item actual);
LAMBDA_RT_API Item js_throw_invalid_arg_value(const char* name, const char* reason, Item actual);
LAMBDA_RT_API Item js_throw_out_of_range(const char* name, const char* range, Item actual);

/** Throw a system error (like ENOENT, EACCES) with code, errno, syscall, path. */

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
LAMBDA_RT_API Item js_new_error_with_name(Item error_name, Item message);
LAMBDA_RT_API Item js_new_error_with_name_cstr(const char* error_name, const char* message);
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
// Binds JavaScript work to the Runtime's canonical EvalContext. MIR and the
// AST interpreter share this owner-selection boundary.
#ifdef __cplusplus
bool js_prepare_eval_context(Runtime* runtime, bool initialize_thread,
                             EvalContext** out_context,
                             bool* out_reusing_context, bool initialize_js = true);
#endif

// =============================================================================
// Module Variable Table
// =============================================================================

void js_register_global_var_module_binding(Item key, int64_t index);
void js_release_global_var_module_bindings_from(uint32_t first_module_state_id);

/**
 * Reset all JS runtime global state between batch test runs.
 * Clears module vars, event loop, DOM context, and Input context.
 */
void js_batch_reset(void);
void js_intrinsic_state_teardown(void);
void js_batch_reset_to(int checkpoint_var_count);
extern int js_batch_execution_mode;
void js_symbol_registry_batch_reset(void);
void dom_batch_reset(void);
void js_globals_batch_reset(void);
void js_reset_constructor_prototypes(void);
Item js_constructor_create_object(Item callee, Item new_target);
Item js_construct_value(Item callee, Item* args, int argc, Item new_target,
                        uint64_t* result_home, bool args_prerooted);

// =============================================================================
// v12: Language extensions
// =============================================================================

// Object rest destructuring: create object with all props except excluded keys
Item js_object_rest(Item src, Item* exclude_keys, int exclude_count);

// URI encoding/decoding. The shared kernel takes an already converted string;
// malformed may raise through another frontend, preserving its thrown identity.
Item js_uri_transform_string(Item text, bool component, bool decode,
    Item (*malformed)(const char*));
Item js_encodeURIComponent(Item str_item);
Item js_decodeURIComponent(Item str_item);
Item js_encodeURI(Item str_item);
Item js_decodeURI(Item str_item);
Item js_unescape(Item str_item);
Item js_escape(Item str_item);
LAMBDA_RT_API Item js_atob(Item str_item);
LAMBDA_RT_API Item js_btoa(Item str_item);

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
    long realm_us; // global graph construction, excluded from imports/execute
    long parse_build_us;
    long bind_us;
    long validate_us;
    long index_us;
    long collect_us;
    long captures_us;
    long env_layout_us;
    long infer_us;
    long forward_declare_us;
    long mir_lower_us;
    long finalize_us;
    long prelink_us;
} JsMirPhaseTiming;

void js_mir_reset_last_phase_timing(void);
void js_mir_get_last_phase_timing(JsMirPhaseTiming* out);
void js_mir_begin_document_phase_timing(void);
void js_mir_accumulate_last_phase_timing(bool is_preamble);
void js_mir_end_document_phase_timing(JsMirPhaseTiming* out);
// Emits a JS row through Lambda's common LAMBDA_PROFILE lifecycle.
void lambda_profile_record_js_compilation(const char* script_path,
    const JsMirPhaseTiming* timing);
uint64_t js_realm_init_time_us(void);

// Shared ECMAScript IdentifierName policy backed by the generated Unicode
// ID_Start / ID_Continue range tables used by RegExp property support.
bool js_unicode_id_is_start(uint32_t cp);
bool js_unicode_id_is_continue(uint32_t cp);

bool js_regexp_virtual_property_is_overridden(Item regex, const char* name, int name_len);
void js_regexp_mark_virtual_property_overridden(Item regex, const char* name, int name_len);

// Tune6 §3.2: MIR generated-code volume for the last transpile. Drives the
// MIR-lowering reduction work (§3.3) — identifies which fixtures emit the most MIR.
typedef struct JsMirVolumeCounters {
    long functions_discovered;  // JS functions collected (mt->func_count)
    long mir_insns_emitted;     // total MIR instructions across all func items
} JsMirVolumeCounters;

void js_mir_volume_counters_reset(void);
void js_mir_volume_counters_get(JsMirVolumeCounters* out);

// globalThis / global object
Item js_get_global_this(void);
// the writer installs a lazy own slot; each engine keeps its ordinary storage policy.
Item js_install_host_namespace_slots(Item global, Item (*writer)(Item, Item, Item), bool constructors);
bool js_host_namespace_has_name(const char* name, size_t length);
Item js_ensure_host_global_family(const char* name);
bool js_resolve_lazy_global(Item object, Item key, Item* out_value);
Item js_named_native_function_source_item(const String* name);
int js_is_global_this_object_value(Item object);
Item js_get_global_property(Item key);
// Resolve the global environment after an Object Environment Record already
// performed HasBinding for this identifier.
Item js_get_global_property_after_with_lookup(Item key);
// Set a global binding after an Object Environment Record already ran HasBinding.
Item js_set_global_property_after_with_lookup(Item key, Item value, int64_t strict);
int64_t js_global_binding_exists(Item key);
int64_t js_global_binding_exists_after_with_lookup(Item key);
// Tune8 §2.2: js_set_global_property now takes a strict flag
// (0 = sloppy implicit global, 1 = strict throw-on-undeclared).
// js_set_global_property_strict has been removed.
Item js_set_global_property(Item key, Item value, int64_t strict);
void js_define_global_var_property(Item key, Item value);
void js_define_global_eval_var_property(Item key, Item value);
void js_define_global_function_property(Item key, Item value);
void js_global_lexical_declare_module_slot(Item key, int64_t index, int64_t immutable);
int64_t js_global_lexical_binding_exists(Item key);
Item js_global_lexical_get_or_fallback(Item key, Item fallback);
Item js_global_lexical_set_if_exists(Item key, Item value);
Item js_evalscript_check_global_lex_decl(Item key);
void js_mark_private_method_non_writable(Item object, Item name);
Item js_init_class_instance_fields(Item callee, Item object);
Item js_init_class_instance_fields_after_super(Item callee, Item object);
void js_init_class_instance_field_metadata(Item class_item, int count);
void js_set_class_instance_field_metadata_key(Item class_item, int index, Item key);
void js_set_class_instance_field_metadata_private_method(Item class_item, int index);
void js_set_class_instance_field_metadata_value(Item class_item, int index, Item value);
void js_set_class_instance_field_metadata_initializer(Item class_item, int index,
    Item initializer);
LAMBDA_RT_API Item js_private_key_for_class(Item class_item, Item source_name);
Item js_private_in(Item object, Item private_key);
Item js_private_brand_add(Item object, Item private_key, Item callee);
void js_set_function_name_from_property_key_if_anonymous(Item fn_item, Item key_item, int64_t prefix_kind);
void js_set_function_name_from_property_key(Item fn_item, Item key_item, int64_t prefix_kind);
Item js_function_name_display(NameRef key, int64_t prefix_kind);
void js_set_function_name_if_anonymous(Item fn_item, Item name_item);
Item js_get_global_builtin_fn_by_id(Item global_id);
int64_t js_with_depth_active(void);
extern const char* const js_array_unscopable_names[];
typedef struct JsWithBindingOps {
    Item (*has)(Item object, Item key);
    Item (*get)(Item object, Item key);
    bool (*is_object)(Item value);
    int64_t (*truthy)(Item value);
} JsWithBindingOps;
Item js_with_binding_is_visible_with_ops(Item key, Item object, Item unscopables_symbol,
    const JsWithBindingOps* ops);
Item js_with_binding_is_visible(Item key, Item object);
Item js_with_get_binding_value(Item object, Item key, bool strict);
Item js_get_with_binding_or_fallback(Item key, Item fallback);
Item js_get_last_with_binding_base_or_undefined(Item key);
Item js_probe_with_binding(Item key);
Item js_probe_with_binding_from(Item key, int64_t minimum_depth);
Item js_capture_with_binding_from(Item key, int64_t minimum_depth);
Item js_set_last_with_binding_if_valid(Item key, Item value, int64_t strict);
Item js_set_with_binding_base(Item scope_obj, Item key, Item value, int64_t strict);
// URL constructor
Item js_url_construct(Item input);
Item js_url_get_component(Item receiver, const char* name);
Item js_url_construct_with_base(Item input, Item base);
Item js_readable_stream_new(Item underlying_source);
Item js_writable_stream_new(Item underlying_sink);
Item js_transform_stream_new(Item transformer);
Item js_text_encoder_stream_new(void);
Item js_text_decoder_stream_new(Item encoding, Item options);

// Symbol API
// JS Symbols use the core pointer-backed LMD_TYPE_SYMBOL representation.
// SYMBOL_JS_* kinds retain JS identity while SYMBOL_LAMBDA_NAME stays a
// separate Lambda textual-symbol contract.

Item js_symbol_create(Item description);
LAMBDA_RT_API Item js_symbol_for(Item key);
Item js_symbol_key_for(Item sym);
typedef struct JsSymbolOps {
    Item (*string)(Item value);
    Item (*error)(const char* message);
} JsSymbolOps;
Item js_symbol_for_with_ops(Item key, const JsSymbolOps* semantics);
Item js_symbol_key_for_with_ops(Item symbol, const JsSymbolOps* semantics);
bool js_symbol_from_name_id(NameId name_id, Item* out_symbol);

Item js_symbol_to_string(Item sym);
Item js_symbol_get_description(Item sym);
Item js_symbol_well_known(Item name);

// =============================================================================
// v14: Generator Runtime
// =============================================================================

struct JsGeneratorStateRecord;
struct Activation;
Item js_generator_create_body(Item (*body)(struct Activation*, Item), void* func_ptr,
    Item* env, int env_size, int is_async, Item function, Item arguments, Item receiver,
    struct TypeMap* shape);
Item js_async_generator_request(Item generator, int operation, Item value);

/**
 * Create a generator object from a state machine function pointer.
 * The func_ptr is the MIR-compiled generator body (state machine form).
 * env/env_size represent captured closure variables.
 */
Item js_generator_create_ast(Item function, Item arguments, Item this_value,
                             int is_async);
struct JsGeneratorStateRecord* js_generator_get_ast_state(Item generator);

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
Item js_make_iter_result(Item value, bool done);

/**
 * v15: Create a 2-element array [value, next_state] for generator state machine returns.
 * Called from MIR-compiled generator state machine functions at each yield point.
 */
// How a generator body is parked (its record's `state`): at a yield, at an
// async generator's await, or at a yield* whose delegate the driver steps.
enum { JS_GEN_PARK_YIELD = 1, JS_GEN_PARK_AWAIT = 2, JS_GEN_PARK_DELEGATE = 3 };
Item js_gen_park(Item value, int64_t kind);
Item js_gen_return_signal(Item value);
int64_t js_gen_is_return_signal(Item value);
Item js_gen_return_signal_value(Item value);
Item js_gen_throw_signal(Item value);
int64_t js_gen_is_throw_signal(Item value);
Item js_gen_throw_signal_value(Item value);

/**
 * v15: Convert an iterable to an array. Drains generators, passes arrays through.
 */
LAMBDA_RT_API Item js_iterable_to_array(Item iterable);

/**
 * Lazy iteration protocol for for-of loops.
 * js_get_iterator: Get an iterator object from an iterable.
 * js_iterator_step: Advance iterator, return next value or JS_ITER_DONE_SENTINEL when done.
 * js_iterator_close: Call iterator.return() for IteratorClose (on break/return).
 */
Item js_get_iterator(Item iterable);
// async iteration returns a private record retaining the iterator and its next method.
Item js_get_async_iterator(Item iterable);
Item js_get_iterator_lazy(Item iterable);
// IDL callers have already read @@iterator; validate its result and capture next once.
Item js_iterator_return_checked(Item iterator, bool cache_next, const char* error_message);
bool js_is_fixed_layout_iterator(Item object);
Item js_iterator_step(Item iterator);
// async iteration consumes the private record from js_get_async_iterator and keeps
// the protocol result intact until its promise settles.
Item js_async_iterator_step_result(Item record);
Item js_async_iterator_method_result(Item record, int operation, Item value, int count);
Item js_iterator_result_done(Item result);
Item js_iterator_result_value(Item result);
// Returns the raw `return()` result. An absent method produces the internal
// close-absent sentinel, while a callable that returns undefined stays an
// awaitable result.
Item js_async_iterator_close_result(Item iterator);
// record callers retain async-from-sync adaptation and use the same absent sentinel.
Item js_async_iterator_record_close_result(Item record);
bool js_async_iterator_close_needs_await(Item result);
Item js_iterator_close(Item iterator);
Item js_get_iterator_proto();
Item js_iterator_collect_rest(Item iterator);

// =============================================================================
// v14: Promise Runtime
// =============================================================================

// Frontend policy is fixed for one EvalContext. Its active realm owner follows
// calls and suspension; queued jobs retain their realm through wrap_job.
typedef struct JsPromiseHostOps {
    Item (*get)(void*, Item, Item, Item);
    bool (*callable)(void*, Item);
    bool (*constructor)(void*, Item);
    Item (*call)(void*, Item, Item, Item*, int);
    Item (*construct)(void*, Item, Item*, int, Item);
    Item (*intrinsic)(void*, bool);
    Item (*new_object)(void*);
    Item (*create_data)(void*, Item, Item, Item);
    Item (*iterator)(void*, Item);
    Item (*iterator_step)(void*, Item);
    Item (*iterator_close)(void*, Item, Item);
    Item (*error)(void*, const char*, Item, Item);
    Item (*error_payload)(void*, Item);
    Item (*expose_function)(void*, Item);
    Item (*wrap_job)(void*, Item job, Item handler);
    Item (*new_array)(void*, int64_t length);
} JsPromiseHostOps;
void js_promise_set_host_ops(const JsPromiseHostOps* ops, void* host_context);
Item js_promise_property_storage(Item promise, bool create);
typedef enum JsPromiseIntrinsicOp {
    JS_PROMISE_INTRINSIC_RESOLVE,
    JS_PROMISE_INTRINSIC_REJECT,
    JS_PROMISE_INTRINSIC_ALL,
    JS_PROMISE_INTRINSIC_ALL_SETTLED,
    JS_PROMISE_INTRINSIC_ANY,
    JS_PROMISE_INTRINSIC_RACE,
    JS_PROMISE_INTRINSIC_WITH_RESOLVERS,
    JS_PROMISE_INTRINSIC_THEN,
    JS_PROMISE_INTRINSIC_CATCH,
    JS_PROMISE_INTRINSIC_FINALLY,
} JsPromiseIntrinsicOp;
Item js_intrinsic_promise_operation(JsPromiseIntrinsicOp operation,
    Item this_value, Item* args, int argc);

LAMBDA_RT_API Item js_promise_create(Item executor);           // new Promise((resolve, reject) => ...)
LAMBDA_RT_API Item js_promise_from_completion(Item completion);
LAMBDA_RT_API Item js_promise_resolve(Item value);             // Promise.resolve(value)
LAMBDA_RT_API Item js_promise_reject(Item reason);             // Promise.reject(reason)
Item js_promise_create_pending(void);
bool js_promise_is(Item promise);
void js_promise_fulfill_existing(Item promise, Item value);
void js_promise_reject_existing(Item promise, Item reason);
const char* js_promise_state_name(Item promise); // "pending", "fulfilled", "rejected", or NULL
int js_promise_pending_count(void);
Item js_promise_then(Item promise, Item on_fulfilled, Item on_rejected);
// internal reactions use the supplied native result Promise, without species lookup.
bool js_promise_observe(Item promise, Item fulfilled, Item rejected, Item result_promise);
void js_promise_mark_handled(Item promise);
Item js_promise_finally(Item promise, Item on_finally);
Item js_promise_all(Item iterable);              // Promise.all([...])
Item js_promise_with_resolvers(void);            // Promise.withResolvers()
// An await outside any activation; runs jobs only until this promise settles.
Item js_await_sync(Item value);

// Re-home a borrowed wide scalar in the active runtime number extent.
Item js_retain_scalar(Item value);

// Phase 6: Async state machine runtime
           // fresh async-function result promise
struct Activation;
Item js_async_context_create_body(Item (*body)(struct Activation*, Item), void* fn_ptr,
    Item* env, int64_t env_size, Item this_val, Item function, Item arguments);
Item js_async_context_create_ast(Item function, Item arguments, Item this_val);
Item js_async_context_create_module(struct JsScript* script,
    uint32_t module_state_id, Item specifier);
Item js_async_context_create_module_body(Item (*body)(struct Activation*, Item),
    void* payload, struct JsScript* script, uint32_t module_state_id, Item specifier,
    struct ModuleDescriptor* descriptor);
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
// shared physical carrier allocation; guest adapters supply shape and capabilities.
Item js_proxy_carrier_new(Item target, Item handler, struct TypeMap* shape, bool callable, bool constructable);
Item js_proxy_revocable(Item target, Item handler);

// Check if an Item is a Proxy
bool js_is_proxy(Item obj);
// Get proxy data (returns NULL if not a proxy)
JsProxyData* js_get_proxy_data(Item obj);
#ifdef __cplusplus
Item js_proxy_private_slots(Item proxy, bool create, Item (*allocate)() = nullptr,
    bool allow_revoked = false);
#endif
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
struct JsTypedArrayOps;
typedef struct JsStructuredCloneOps {
    const JsArrayOps* arrays;
    const struct JsTypedArrayOps* typed_arrays;
    Item (*object)(void);
    Item (*keys)(Item);
    Item (*get)(Item, Item);
    Item (*create)(Item, Item, Item);
} JsStructuredCloneOps;
Item js_structured_clone_with_ops(Item value, const JsStructuredCloneOps* ops);

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

/**
 * Module graph depth: 1 while the entry module compiles, >= 2 for a nested
 * import. Exiting depth 0 starts the module bodies whose async dependencies
 * have completed. See js_runtime.cpp for details.
 */
void js_tla_flush_for_dynamic_import(void);
void js_tla_flush_for_runtime(struct Runtime* runtime);

/**
 * Js57 P5 (fulfillment/rejection-order): TLA awaited-target tracking on the
 * module registry. Used to make dynamic imports wait on the imported module's
 * carrier promise, which settles when its parked body finishes, and to
 * propagate that wait through static-import edges so siblings end up chained
 * on the same target.
 */
Item js_module_get_awaited_target(Item specifier);
void js_module_inherit_awaited_target(Item current_specifier, Item dep_specifier);
// An async function's await on its own activation: parks it until the promise
// settles (RA1).
Item js_await_park(Item value);
void js_module_record_evaluation_error(Item specifier, Item error);
// An async module must reject its own evaluation promise before notifying
// async importers, preserving leaf-to-root rejection reaction order.
void js_module_record_async_evaluation_error(Item specifier, Item error,
                                             Item completion_promise);

/* Js57 P7d: per-module TLA evaluation tracking. */
int  js_module_needs_async_settle(Item specifier);
void js_tla_drain_pending_modules(void);
void js_module_register_static_dependency(Item parent_specifier, Item dep_specifier);
void js_module_register_async_parent(Item dep_specifier, Item parent_specifier);
void js_module_set_deferred_async_frame(Item specifier, Item frame);
int  js_module_pending_async_deps(Item specifier);
void js_module_complete_tla_body(Item specifier);

/**
 * Current namespace object for the module being evaluated.
 */
Item js_get_active_module_namespace();
Item js_set_active_module_namespace(Item namespace_obj);
Item js_get_import_meta();

/**
 * CJS require() — load and execute a module, return its exports.
 * Shared host/module loading is implemented in js_execution.cpp.
 */
Item js_require(Item specifier);
Item js_require_at(Item specifier, const char* reference);
Item js_module_create_require(Item filename);
Item js_dynamic_import(Item specifier);
Item js_dynamic_import_at(Item specifier, const char* reference);
Item js_p5_chain_dynamic_import(Item awaited, Item namespace_obj, Item result_promise);

// Native SHA hash functions (Jube node-crypto host primitive provider)
Item js_native_sha256(Item data, Item offset, Item length);
Item js_native_sha384(Item data, Item offset, Item length);
Item js_native_sha512(Item data, Item offset, Item length);

// =============================================================================
// OffscreenCanvas / CanvasRenderingContext2D (js_canvas.cpp)
// =============================================================================

Item js_offscreen_canvas_new(Item width, Item height);
void js_canvas_ctx_set_font(Item ctx_obj, Item font_val);
bool js_canvas_property_set_intercept(Item obj, Item key, Item value);
void js_canvas_cleanup(void);
bool js_array_runtime_items_release(Array* owner);
void js_array_runtime_items_cleanup_all(void);
bool js_array_immortal_props_store(Array* owner, Map* props);
void js_array_immortal_props_cleanup_all(void);

// Runtime entry points shared by the JIT import registry and JS subsystems.
Item js_with_push_at(Item* slot, Item obj);
void js_with_pop(void);
Item js_delete_identifier_with_binding(Item key, int64_t declared_binding);
Item js_evalscript_check_global_var_decl(Item key);
Item js_evalscript_check_global_function_decl(Item key);
Item js_object_group_by(Item items, Item callback);
Item js_map_group_by(Item items, Item callback);
extern const JsCollectionOps js_collection_default_ops;
typedef struct JsGroupByOps {
    Item (*find)(Item result, Item key); // group Array or ItemNull
    Item (*put)(Item result, Item key, Item group);
    Item (*key_of)(Item raw_key);
    Item (*create)(void);
    Item (*new_array)(void);
    Item (*append)(Item array, Item value);
} JsGroupByOps;
Item js_group_by_with_ops(Item items, Item callback, const JsGroupByOps* groups,
    const JsCollectionOps* semantics);
Item js_object_from_entries_with_ops(Item iterable, const JsGroupByOps* properties,
    const JsCollectionOps* semantics);

Item js_object_set_prototype_of(Item obj, Item proto);
void js_set_class_name(Item cls_item, Item name_item);
void js_set_default_constructor_property(Item proto_item, Item cls_item);
Item js_arguments_mapped_get(Item arguments, int64_t index, Item current_value);
Item js_arguments_mapped_param_writeback(Item arguments, int64_t index, Item value);
Item js_assert_same_value(Item actual, Item expected, Item message);
Item js_assert_not_same_value(Item actual, Item unexpected, Item message);
Item js_assert_compare_array(Item actual, Item expected, Item message);
Item js_assert_deep_equal(Item actual, Item expected, Item message);
Item js_compare_array(Item a, Item b);
Item js_verify_property(Item obj, Item name, Item desc, Item options);
Item js_assert_throws(Item expected_ctor, Item func, Item message);
Item js_assert_base(Item must_be_true, Item message);
Item js_donotevaluate(void);
Item js_is_constructor(Item fn);
Item js_test262_native_harness_install(void);
Item js_262_get_agent_object(void);
Item js_test262_build_string(Item args);
uint64_t lambda_mir_double_bits(double dval);
double lambda_mir_bits_double(uint64_t bits);
// Runtime entry points shared across JS translation units.
uint64_t js_get_heap_epoch(void);
Item js_get_typed_array_base_proto(void);
Item js_get_typed_array_base(void);
LAMBDA_RT_API Item js_process_emit(Item event_name, Item arg1);
bool js_promise_vmap_is(Item value);
bool js_proto_snapshot_is_valid(void);
Item js_bigint_constructor(Item value);
bool js_is_generator(Item obj);
Item js_async_hooks_get_current_resource(void);
Item js_async_hooks_enter_resource(Item resource);
void js_async_hooks_restore_resource(Item previous);
void js_intrinsic_note_prototype_mutation(Item object);
LAMBDA_RT_API Item js_bigint_as_int_n(Item bits_item, Item bigint_item);
LAMBDA_RT_API Item js_bigint_as_uint_n(Item bits_item, Item bigint_item);

#ifdef __cplusplus
}

LAMBDA_RT_API Item js_make_number(double value);
extern int js_dynamic_import_suppress_module_drain;

LAMBDA_RT_API Item make_string_item(const char* str, int len);
struct JsFunction; struct String;
LAMBDA_RT_API Item make_string_item(const char* str);
// Interned property keys. These go through the name pool, so they keep name
// identity and need no RootFrame — unlike make_string_item / js_get_key_cstr,
// which heap-copy. Interning is semantics here, not style: do not "simplify"
// a converted site onto the copying forms.
LAMBDA_RT_API Item js_name_item(const char* name, int len);
LAMBDA_RT_API Item js_name_item(const char* name);
Item js_get_name_key(Item object, const char* name, int len);
Item js_get_name_key(Item object, const char* name);
Item js_set_name_key(Item object, const char* name, int len, Item value);
Item js_set_name_key(Item object, const char* name, Item value);
// Native adapters are one overload family; keep declarations in lockstep with
// the arity-generated implementations in js_runtime_function.cpp.
#define JS_NATIVE_FIXED_ARITIES(M) \
    M(0) M(1) M(2) M(3) M(4) M(5) M(6) M(7) M(8)
#define JS_NATIVE_REST_ARITIES(M) \
    M(1) M(2) M(3) M(4) M(5) M(6) M(7) M(8)
#define JS_NATIVE_CLOSURE_ARITIES(M) M(1) M(2) M(3) M(4) M(5)
#define JS_DECLARE_NATIVE_FACTORY(arity) \
    LAMBDA_RT_API Item js_new_native_function(JsNativeP##arity target); \
    LAMBDA_RT_API Item js_new_native_constructor(JsNativeP##arity target); \
    Item js_new_distinct_native_function(JsNativeP##arity target); \
    Item js_new_distinct_native_constructor(JsNativeP##arity target);
JS_NATIVE_FIXED_ARITIES(JS_DECLARE_NATIVE_FACTORY)
#undef JS_DECLARE_NATIVE_FACTORY
#define JS_DECLARE_NATIVE_ADAPTED(arity) \
    LAMBDA_RT_API Item js_new_native_function(JsNativeP##arity target, int adapter_arity); \
    Item js_new_native_constructor(JsNativeP##arity target, int adapter_arity);
JS_NATIVE_FIXED_ARITIES(JS_DECLARE_NATIVE_ADAPTED)
#undef JS_DECLARE_NATIVE_ADAPTED
#define JS_DECLARE_NATIVE_PUBLISHER(arity) \
    void js_set_native_method(Item object, const char* name, JsNativeP##arity target); \
    LAMBDA_RT_API void js_set_native_key(Item object, Item key, JsNativeP##arity target);
JS_NATIVE_FIXED_ARITIES(JS_DECLARE_NATIVE_PUBLISHER)
#undef JS_DECLARE_NATIVE_PUBLISHER
#define JS_DECLARE_NATIVE_INSTALLER(arity) \
    LAMBDA_RT_API Item js_install_native_method(Item object, const char* name, JsNativeP##arity target); \
    LAMBDA_RT_API Item js_install_native_method(Item object, const char* name, JsNativeP##arity target, int adapter_arity); \
    LAMBDA_RT_API Item js_install_native_constructor(Item object, const char* name, JsNativeP##arity target); \
    LAMBDA_RT_API Item js_install_native_constructor(Item object, const char* name, JsNativeP##arity target, int adapter_arity);
JS_NATIVE_FIXED_ARITIES(JS_DECLARE_NATIVE_INSTALLER)
#undef JS_DECLARE_NATIVE_INSTALLER
#define JS_DECLARE_NATIVE_REST(arity) \
    Item js_new_native_rest_function(JsNativeP##arity target); \
    Item js_new_native_rest_constructor(JsNativeP##arity target);
JS_NATIVE_REST_ARITIES(JS_DECLARE_NATIVE_REST)
#undef JS_DECLARE_NATIVE_REST
Item js_initialize_native_constructor_prototype(Item constructor,
                                                Item prototype);
Item js_new_native_span_function(JsNativeSpan target);
Item js_new_native_this_span_function(JsNativeThisSpan target);
Item js_new_native_body_constructor(JsNativeCallBody call_body,
                                     JsNativeConstructBody construct_body,
                                     int formal_length);
Item js_new_native_body_constructor_closure(JsNativeCallBody call_body,
    JsNativeConstructBody construct_body, int formal_length, Item* env, int env_size);
Item js_new_native_payload_function(JsNativeCallBody call_body,
                                    uint64_t payload, int formal_length,
                                    Item* env = NULL, int env_size = 0);
#define JS_DECLARE_NATIVE_CLOSURE(arity) \
    LAMBDA_RT_API Item js_new_native_closure(JsNativeP##arity target, int adapter_arity, \
                               Item* env, int env_size);
JS_NATIVE_CLOSURE_ARITIES(JS_DECLARE_NATIVE_CLOSURE)
#undef JS_DECLARE_NATIVE_CLOSURE
#define JS_DECLARE_NATIVE_ENV_SCHEDULER(arity) \
    LAMBDA_RT_API void js_schedule_native_env(void (*schedule)(Item), JsNativeP##arity target, \
        int adapter_arity, const Item* values, int count); \
    Item js_schedule_native_env_timeout(JsNativeP##arity target, \
        int adapter_arity, Item delay, const Item* values, int count);
JS_NATIVE_CLOSURE_ARITIES(JS_DECLARE_NATIVE_ENV_SCHEDULER)
#undef JS_DECLARE_NATIVE_ENV_SCHEDULER
#undef JS_NATIVE_CLOSURE_ARITIES
#undef JS_NATIVE_REST_ARITIES
#undef JS_NATIVE_FIXED_ARITIES

// C3.4: native async callbacks all carry their captured values in one traced
// environment. Keep allocation, population, closure attachment, and enqueue
// in one path so a scheduler cannot observe a partially initialized closure.
#define JS_ENV_UNPACK(name, env_item) \
    Item* name = (Item*)(uintptr_t)(env_item).item

#define JS_TICK_N(target, adapter_arity, values, count) \
    js_schedule_native_env(js_next_tick_enqueue, target, adapter_arity, values, count)
#define JS_TIMEOUT_N(target, adapter_arity, delay, values, count) \
    js_schedule_native_env_timeout(target, adapter_arity, delay, values, count)
#endif
