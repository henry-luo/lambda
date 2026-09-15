#pragma once

#include <stddef.h>
#include <stdint.h>

// The MVP runtime has a private JS value ABI. It deliberately never aliases
// Item or a legacy execution-state carrier; conversion happens only at its host edge.
typedef struct MvpValue {
    uint64_t bits;
} MvpValue;

typedef struct MvpHeapObject MvpHeapObject;
typedef struct MvpHeap MvpHeap;
typedef struct MvpRootFrame MvpRootFrame;
typedef struct MvpExecutionFrame MvpExecutionFrame;
typedef struct MvpNumericProgram MvpNumericProgram;
typedef struct MvpAstUnit MvpAstUnit;

typedef struct MvpLiteralCacheEntry {
    uint64_t address;
    size_t byte_length;
    MvpValue value;
} MvpLiteralCacheEntry;

typedef struct MvpExecutionResult {
    MvpValue value;
    int ok;
    char error[160];
    struct MvpExecution* execution;
} MvpExecutionResult;

typedef enum MvpHeapKind {
    MVP_HEAP_VALUES = 1,
    MVP_HEAP_STRING = 2,
    MVP_HEAP_BIGINT = 3,
    MVP_HEAP_ARRAY = 4,
    MVP_HEAP_OBJECT = 5,
    MVP_HEAP_FUNCTION = 6,
    MVP_HEAP_MAP = 7,
    MVP_HEAP_REGEX = 8,
    MVP_HEAP_ACCESSOR = 9
} MvpHeapKind;

typedef enum MvpCompletionKind {
    MVP_COMPLETION_THROW = 1,
    MVP_COMPLETION_STOP = 2
} MvpCompletionKind;

struct MvpHeapObject {
    MvpHeapObject* next;
    MvpHeapObject* mark_next;
    size_t allocation_size;
    uint32_t kind;
    uint32_t marked;
};

// Each aggregate owns its backing values through a normal MvpValue reference.
// This keeps every traced edge in the private value ABI and lets capacities grow
// without moving the aggregate object itself.
typedef struct MvpString {
    MvpHeapObject header;
    size_t byte_length;
    uint64_t lookup_hash;
    // Execution-literal strings retain the parser-owned byte address so hot
    // named-field probes can prove identity without another byte comparison.
    const char* source_address;
    char bytes[1];
} MvpString;

// Limbs are base 1e9 and little-endian.  BigInts contain no tagged edges, so
// a flexible payload is both compact and straightforward for precise GC.
typedef struct MvpBigInt {
    MvpHeapObject header;
    size_t limb_count;
    int sign;
    uint32_t limbs[1];
} MvpBigInt;

typedef struct MvpArray {
    MvpHeapObject header;
    size_t length;
    size_t capacity;
    MvpValue storage;
    MvpValue properties;
} MvpArray;

typedef struct MvpObject {
    MvpHeapObject header;
    size_t property_count;
    size_t property_capacity;
    MvpValue storage;
    MvpValue prototype;
    size_t index_capacity;
    MvpValue index_storage;
} MvpObject;

// Map preserves insertion-order entries and owns a private open-addressed
// index. Both backing stores are traced through normal MvpValue edges.
typedef struct MvpMap {
    MvpHeapObject header;
    size_t entry_count;
    size_t entry_capacity;
    MvpValue storage;
    size_t index_capacity;
    MvpValue index_storage;
} MvpMap;

// The regex payload belongs only to the MVP heap.  The opaque compiled form
// is released with this object and never crosses into the legacy JS runtime.
typedef struct MvpRegex {
    MvpHeapObject header;
    MvpValue pattern;
    MvpValue flags;
    void* compiled;
    int global;
} MvpRegex;

typedef struct MvpAccessor {
    MvpHeapObject header;
    MvpValue getter;
    MvpValue setter;
} MvpAccessor;

// MIR-generated functions use a fixed receiver-plus-arguments convention. A
// descriptor is populated after MIR links and remains valid for the enclosing
// execution, so callable objects never depend on the legacy JS call ABI.
typedef struct MvpJitFunctionDescriptor {
    void* entry;
    size_t parameter_count;
    // A declaration with a nested callable remains representable so an
    // unrelated class method does not prevent its class from loading.
    int unavailable;
    int captures_receiver;
    int captures_environment;
} MvpJitFunctionDescriptor;

struct MvpFunction;

typedef uint64_t (*MvpNativeFunction)(void* execution, struct MvpFunction* function,
                                      uint64_t receiver, uint64_t* arguments,
                                      size_t argument_count);

typedef struct MvpFunction {
    MvpHeapObject header;
    MvpNativeFunction entry;
    void* closure;
    size_t formal_count;
    MvpValue captures;
    MvpValue properties;
    MvpValue instance_prototype;
} MvpFunction;

struct MvpHeap {
    MvpHeapObject* objects;
    MvpRootFrame* roots;
    size_t allocated_bytes;
    size_t next_collection_bytes;
    size_t live_bytes;
    size_t object_count;
    size_t collection_count;
    int force_every_allocation;
};

struct MvpRootFrame {
    MvpRootFrame* previous;
    MvpValue* values;
    size_t count;
};

typedef struct MvpExecution {
    MvpHeap heap;
    MvpRootFrame root_frame;
    MvpRootFrame* active_frame;
    MvpExecutionFrame* free_frames;
    MvpValue* roots;
    size_t root_count;
    size_t root_capacity;
    size_t global_root_index;
    MvpLiteralCacheEntry* literal_cache;
    size_t literal_cache_count;
    size_t literal_cache_capacity;
    // class methods record their lexical home for super() resolution.
    MvpValue current_home_prototype;
    uint64_t random_state;
    char error[160];
} MvpExecution;

MvpValue mvp_value_from_number(double value);
double mvp_value_to_number(MvpValue value);
int mvp_value_is_number(MvpValue value);
int mvp_value_is_nan(MvpValue value);
int mvp_value_is_undefined(MvpValue value);
int mvp_value_is_null(MvpValue value);
int mvp_value_is_boolean(MvpValue value);
int mvp_value_is_reference(MvpValue value);
int mvp_value_is_completion(MvpValue value);
int mvp_value_boolean(MvpValue value);
MvpValue mvp_value_undefined(void);
MvpValue mvp_value_null(void);
MvpValue mvp_value_bool(int value);
MvpValue mvp_value_reference(MvpHeapObject* object);
MvpHeapObject* mvp_value_reference_object(MvpValue value);
MvpValue mvp_value_completion(MvpCompletionKind kind, MvpHeapObject* payload);
MvpCompletionKind mvp_value_completion_kind(MvpValue value);
MvpHeapObject* mvp_value_completion_payload(MvpValue value);
int mvp_value_is_string(MvpValue value);
int mvp_value_is_bigint(MvpValue value);
int mvp_value_is_array(MvpValue value);
int mvp_value_is_object(MvpValue value);
int mvp_value_is_function(MvpValue value);
int mvp_value_is_map(MvpValue value);
int mvp_value_is_regex(MvpValue value);
int mvp_value_truthy(MvpValue value);
int mvp_value_strict_equal(MvpValue left, MvpValue right);

void mvp_heap_init(MvpHeap* heap);
void mvp_heap_destroy(MvpHeap* heap);
void mvp_root_frame_push(MvpHeap* heap, MvpRootFrame* frame,
                         MvpValue* values, size_t count);
void mvp_root_frame_pop(MvpHeap* heap, MvpRootFrame* frame);
MvpHeapObject* mvp_heap_alloc_values(MvpHeap* heap, size_t value_count);
MvpValue* mvp_heap_object_values(MvpHeapObject* object);
size_t mvp_heap_object_value_count(const MvpHeapObject* object);
void mvp_heap_collect(MvpHeap* heap);

MvpValue mvp_string_new(MvpHeap* heap, const char* bytes, size_t byte_length);
const char* mvp_string_bytes(MvpValue value, size_t* out_length);
MvpValue mvp_string_concat(MvpHeap* heap, MvpValue left, MvpValue right);
int mvp_string_equal(MvpValue left, MvpValue right);
uint64_t mvp_string_hash_bytes(const char* bytes, size_t byte_length);

MvpValue mvp_bigint_from_decimal(MvpHeap* heap, MvpValue text);
MvpValue mvp_bigint_from_uint64(MvpHeap* heap, uint64_t value);
double mvp_bigint_to_number(MvpValue value);
MvpValue mvp_bigint_to_string(MvpHeap* heap, MvpValue value);
int mvp_bigint_compare(MvpValue left, MvpValue right);
MvpValue mvp_bigint_add(MvpHeap* heap, MvpValue left, MvpValue right);
MvpValue mvp_bigint_sub(MvpHeap* heap, MvpValue left, MvpValue right);
MvpValue mvp_bigint_mul(MvpHeap* heap, MvpValue left, MvpValue right);
MvpValue mvp_bigint_div(MvpHeap* heap, MvpValue left, MvpValue right);

MvpValue mvp_array_new(MvpHeap* heap, size_t initial_capacity);
size_t mvp_array_length(MvpValue value);
MvpValue mvp_array_get(MvpValue value, size_t index);
int mvp_array_set(MvpHeap* heap, MvpValue value, size_t index, MvpValue element);
int mvp_array_push(MvpHeap* heap, MvpValue value, MvpValue element);
MvpValue mvp_array_pop(MvpValue value);
int mvp_array_set_length(MvpHeap* heap, MvpValue value, size_t length);
MvpValue mvp_array_get_property(MvpValue value, MvpValue key);
int mvp_array_set_property(MvpHeap* heap, MvpValue value, MvpValue key, MvpValue element);

MvpValue mvp_object_new(MvpHeap* heap, MvpValue prototype);
MvpValue mvp_object_get(MvpValue object, MvpValue key);
int mvp_object_get_named(MvpValue object, const char* key, size_t key_length,
                         MvpValue* out_value);
int mvp_object_get_named_hashed(MvpValue object, const char* key, size_t key_length,
                                uint64_t key_hash, MvpValue* out_value);
int mvp_object_get_own_named_hashed(MvpValue object, const char* key, size_t key_length,
                                    uint64_t key_hash, MvpValue* out_value);
int mvp_object_set_named_existing(MvpValue object, const char* key, size_t key_length,
                                  MvpValue value);
int mvp_object_set_named_existing_hashed(MvpValue object, const char* key,
                                         size_t key_length, uint64_t key_hash, MvpValue value);
int mvp_object_set(MvpHeap* heap, MvpValue object, MvpValue key, MvpValue value);
int mvp_object_has_own(MvpValue object, MvpValue key);

MvpValue mvp_map_new(MvpHeap* heap);
size_t mvp_map_size(MvpValue value);
MvpValue mvp_map_get(MvpValue value, MvpValue key);
int mvp_map_set(MvpHeap* heap, MvpValue value, MvpValue key, MvpValue element);
MvpValue mvp_map_entries(MvpHeap* heap, MvpValue value);

MvpValue mvp_regex_new(MvpHeap* heap, MvpValue pattern, MvpValue flags);
MvpValue mvp_regex_pattern(MvpValue value);
int mvp_regex_global(MvpValue value);
void* mvp_regex_compiled(MvpValue value);
MvpValue mvp_accessor_new(MvpHeap* heap, MvpValue getter, MvpValue setter);
MvpValue mvp_accessor_getter(MvpValue value);
MvpValue mvp_accessor_setter(MvpValue value);

MvpValue mvp_function_new(MvpHeap* heap, MvpNativeFunction entry, void* closure,
                          size_t formal_count, MvpValue captures);
MvpValue mvp_function_call(MvpValue function, void* execution, MvpValue receiver,
                           uint64_t* arguments, size_t argument_count);

void mvp_execution_init(MvpExecution* execution, size_t root_count);
void mvp_execution_destroy(MvpExecution* execution);
void mvp_execution_set_root(MvpExecution* execution, size_t index, uint64_t bits);
uint64_t mvp_execution_get_root(const MvpExecution* execution, size_t index);
uint64_t mvp_execution_global_get(MvpExecution* execution, uint64_t address,
                                  uint64_t byte_length);
uint64_t mvp_execution_global_set(MvpExecution* execution, uint64_t address,
                                  uint64_t byte_length, uint64_t value);
uint64_t mvp_execution_global_object(MvpExecution* execution);
uint64_t mvp_execution_literal_string(MvpExecution* execution, uint64_t address,
                                      uint64_t byte_length);
MvpValue* mvp_execution_enter_frame(MvpExecution* execution, size_t root_count);
void mvp_execution_leave_frame(MvpExecution* execution);
int mvp_execution_has_error(const MvpExecution* execution);
const char* mvp_execution_error(const MvpExecution* execution);
void mvp_execution_set_error(MvpExecution* execution, const char* message);
void mvp_execution_result_destroy(MvpExecutionResult* result);

uint64_t mvp_op_add(MvpExecution* execution, uint64_t left, uint64_t right);
uint64_t mvp_op_sub(MvpExecution* execution, uint64_t left, uint64_t right);
uint64_t mvp_op_mul(MvpExecution* execution, uint64_t left, uint64_t right);
uint64_t mvp_op_div(MvpExecution* execution, uint64_t left, uint64_t right);
uint64_t mvp_op_mod(MvpExecution* execution, uint64_t left, uint64_t right);
uint64_t mvp_op_bit_and(MvpExecution* execution, uint64_t left, uint64_t right);
uint64_t mvp_op_bit_or(MvpExecution* execution, uint64_t left, uint64_t right);
uint64_t mvp_op_bit_xor(MvpExecution* execution, uint64_t left, uint64_t right);
uint64_t mvp_op_bit_lshift(MvpExecution* execution, uint64_t left, uint64_t right);
uint64_t mvp_op_bit_urshift(MvpExecution* execution, uint64_t left, uint64_t right);
uint64_t mvp_op_neg(MvpExecution* execution, uint64_t value);
uint64_t mvp_op_equal(MvpExecution* execution, uint64_t left, uint64_t right);
uint64_t mvp_op_not_equal(MvpExecution* execution, uint64_t left, uint64_t right);
uint64_t mvp_op_strict_equal(MvpExecution* execution, uint64_t left, uint64_t right);
uint64_t mvp_op_strict_not_equal(MvpExecution* execution, uint64_t left, uint64_t right);
uint64_t mvp_op_less_than(MvpExecution* execution, uint64_t left, uint64_t right);
uint64_t mvp_op_less_equal(MvpExecution* execution, uint64_t left, uint64_t right);
uint64_t mvp_op_greater_than(MvpExecution* execution, uint64_t left, uint64_t right);
uint64_t mvp_op_greater_equal(MvpExecution* execution, uint64_t left, uint64_t right);
uint64_t mvp_op_in(MvpExecution* execution, uint64_t key, uint64_t object);
uint64_t mvp_op_bit_rshift(MvpExecution* execution, uint64_t left, uint64_t right);
uint64_t mvp_op_bit_not(MvpExecution* execution, uint64_t value);
uint64_t mvp_op_not(MvpExecution* execution, uint64_t value);
uint64_t mvp_op_typeof(MvpExecution* execution, uint64_t value);
uint64_t mvp_op_truthy(MvpExecution* execution, uint64_t value);
uint64_t mvp_op_throw(MvpExecution* execution, uint64_t value);
uint64_t mvp_op_literal_string(MvpExecution* execution, uint64_t address,
                               uint64_t byte_length);
uint64_t mvp_host_hrtime_bigint(MvpExecution* execution);
uint64_t mvp_host_performance_now(MvpExecution* execution);
uint64_t mvp_host_math_random(MvpExecution* execution);
uint64_t mvp_host_stdout_write(MvpExecution* execution, uint64_t value);
uint64_t mvp_host_console_log(MvpExecution* execution, uint64_t value);
uint64_t mvp_host_number(MvpExecution* execution, uint64_t value);
uint64_t mvp_host_bigint(MvpExecution* execution, uint64_t value);
uint64_t mvp_host_string(MvpExecution* execution, uint64_t value);
uint64_t mvp_host_parse_int(MvpExecution* execution, uint64_t value);
uint64_t mvp_host_parse_float(MvpExecution* execution, uint64_t value);
uint64_t mvp_host_json_parse(MvpExecution* execution, uint64_t value);
uint64_t mvp_host_json_stringify(MvpExecution* execution, uint64_t value);
uint64_t mvp_host_require(MvpExecution* execution, uint64_t module);
uint64_t mvp_host_argv_get(MvpExecution* execution, uint64_t index);
uint64_t mvp_host_string_from_char_code(MvpExecution* execution, uint64_t value);
uint64_t mvp_host_array_is_array(MvpExecution* execution, uint64_t value);
uint64_t mvp_host_is_nan(MvpExecution* execution, uint64_t value);
uint64_t mvp_host_number_is_nan(MvpExecution* execution, uint64_t value);
uint64_t mvp_host_object_define_property(MvpExecution* execution, uint64_t object,
                                         uint64_t key, uint64_t descriptor);
uint64_t mvp_host_object_get_prototype(MvpExecution* execution, uint64_t object);
uint64_t mvp_host_object_to_string(MvpExecution* execution, uint64_t object);
uint64_t mvp_host_object_has_own(MvpExecution* execution, uint64_t object, uint64_t key);
uint64_t mvp_host_array_new(MvpExecution* execution, uint64_t length);
uint64_t mvp_host_array_empty(MvpExecution* execution);
uint64_t mvp_host_array_one(MvpExecution* execution, uint64_t value);
uint64_t mvp_host_typed_array_new(MvpExecution* execution, uint64_t length);
uint64_t mvp_host_object_new(MvpExecution* execution);
uint64_t mvp_host_accessor_new(MvpExecution* execution, uint64_t getter);
uint64_t mvp_host_environment_new(MvpExecution* execution, uint64_t parent);
uint64_t mvp_host_map_new(MvpExecution* execution);
uint64_t mvp_host_error_new(MvpExecution* execution, uint64_t message);
uint64_t mvp_host_date_new(MvpExecution* execution, uint64_t value);
uint64_t mvp_host_date_now(MvpExecution* execution);
uint64_t mvp_host_regexp_new(MvpExecution* execution, uint64_t pattern,
                             uint64_t flags);
uint64_t mvp_host_regexp_test_bind(MvpExecution* execution, uint64_t regex);
uint64_t mvp_host_numeric_call0(MvpExecution* execution, uint64_t entry);
uint64_t mvp_host_numeric_call1(MvpExecution* execution, uint64_t entry, uint64_t argument0);
uint64_t mvp_host_numeric_call2(MvpExecution* execution, uint64_t entry, uint64_t argument0,
                                uint64_t argument1);
uint64_t mvp_host_numeric_call3(MvpExecution* execution, uint64_t entry, uint64_t argument0,
                                uint64_t argument1, uint64_t argument2);
uint64_t mvp_host_numeric_call4(MvpExecution* execution, uint64_t entry, uint64_t argument0,
                                uint64_t argument1, uint64_t argument2, uint64_t argument3);
uint64_t mvp_host_numeric_call5(MvpExecution* execution, uint64_t entry, uint64_t argument0,
                                uint64_t argument1, uint64_t argument2, uint64_t argument3,
                                uint64_t argument4);
uint64_t mvp_host_numeric_call6(MvpExecution* execution, uint64_t entry, uint64_t argument0,
                                uint64_t argument1, uint64_t argument2, uint64_t argument3,
                                uint64_t argument4, uint64_t argument5);
uint64_t mvp_host_numeric_call7(MvpExecution* execution, uint64_t entry, uint64_t argument0,
                                uint64_t argument1, uint64_t argument2, uint64_t argument3,
                                uint64_t argument4, uint64_t argument5, uint64_t argument6);
uint64_t mvp_host_numeric_call8(MvpExecution* execution, uint64_t entry, uint64_t argument0,
                                uint64_t argument1, uint64_t argument2, uint64_t argument3,
                                uint64_t argument4, uint64_t argument5, uint64_t argument6,
                                uint64_t argument7);
uint64_t mvp_host_numeric_function(MvpExecution* execution, uint64_t entry,
                                   uint64_t parameter_count);

MvpNumericProgram* mvp_numeric_program_create(void* function_ast);
void mvp_numeric_program_destroy(MvpNumericProgram* program);
int mvp_numeric_program_matches(const MvpNumericProgram* program, const void* function_ast);
void* mvp_numeric_program_entry(const MvpNumericProgram* program);
size_t mvp_numeric_program_parameter_count(const MvpNumericProgram* program);
uint64_t mvp_host_math_sin(MvpExecution* execution, uint64_t value);
uint64_t mvp_host_math_cos(MvpExecution* execution, uint64_t value);
uint64_t mvp_host_math_sqrt(MvpExecution* execution, uint64_t value);
uint64_t mvp_host_math_abs(MvpExecution* execution, uint64_t value);
uint64_t mvp_host_math_max(MvpExecution* execution, uint64_t left, uint64_t right);
uint64_t mvp_host_math_max3(MvpExecution* execution, uint64_t first, uint64_t second,
                            uint64_t third);
uint64_t mvp_host_math_min(MvpExecution* execution, uint64_t left, uint64_t right);
uint64_t mvp_host_math_floor(MvpExecution* execution, uint64_t value);
uint64_t mvp_host_math_ceil(MvpExecution* execution, uint64_t value);
uint64_t mvp_host_math_trunc(MvpExecution* execution, uint64_t value);
uint64_t mvp_host_math_round(MvpExecution* execution, uint64_t value);
uint64_t mvp_host_jit_function(MvpExecution* execution, uint64_t descriptor_address);
uint64_t mvp_host_jit_closure(MvpExecution* execution, uint64_t descriptor_address,
                              uint64_t captures);
uint64_t mvp_host_static_literal(MvpExecution* execution, uint64_t node_address);
uint64_t mvp_host_class_new(MvpExecution* execution, uint64_t parent);
uint64_t mvp_host_class_define_method(MvpExecution* execution, uint64_t class_value,
                                      uint64_t key, uint64_t method);
uint64_t mvp_host_class_define_static(MvpExecution* execution, uint64_t class_value,
                                      uint64_t key, uint64_t method);
uint64_t mvp_op_member_get(MvpExecution* execution, uint64_t object, uint64_t key);
uint64_t mvp_op_named_member_get(MvpExecution* execution, uint64_t object,
                                 uint64_t key_address, uint64_t key_length);
uint64_t mvp_op_named_member_get_hashed(MvpExecution* execution, uint64_t object,
                                        uint64_t key_address, uint64_t key_length,
                                        uint64_t key_hash);
uint64_t mvp_op_object_named_own_get_hashed(MvpExecution* execution, uint64_t object,
                                            uint64_t key_address, uint64_t key_length,
                                            uint64_t key_hash);
uint64_t mvp_op_named_member_set(MvpExecution* execution, uint64_t object,
                                 uint64_t key_address, uint64_t key_length, uint64_t value);
uint64_t mvp_op_named_member_set_hashed(MvpExecution* execution, uint64_t object,
                                        uint64_t key_address, uint64_t key_length,
                                        uint64_t key_hash, uint64_t value);
uint64_t mvp_op_optional_member_get(MvpExecution* execution, uint64_t object,
                                    uint64_t key);
uint64_t mvp_op_optional_named_member_get(MvpExecution* execution, uint64_t object,
                                          uint64_t key_address, uint64_t key_length);
uint64_t mvp_op_optional_named_member_get_hashed(MvpExecution* execution, uint64_t object,
                                                 uint64_t key_address, uint64_t key_length,
                                                 uint64_t key_hash);
uint64_t mvp_op_member_set(MvpExecution* execution, uint64_t object, uint64_t key,
                           uint64_t value);
uint64_t mvp_op_enumerable_keys(MvpExecution* execution, uint64_t object);
uint64_t mvp_op_array_fill(MvpExecution* execution, uint64_t object, uint64_t value);
uint64_t mvp_op_array_push(MvpExecution* execution, uint64_t object, uint64_t value);
uint64_t mvp_op_array_spread(MvpExecution* execution, uint64_t object, uint64_t source);
uint64_t mvp_op_construct0(MvpExecution* execution, uint64_t function);
uint64_t mvp_op_construct1(MvpExecution* execution, uint64_t function, uint64_t argument0);
uint64_t mvp_op_construct2(MvpExecution* execution, uint64_t function, uint64_t argument0,
                           uint64_t argument1);
uint64_t mvp_op_construct3(MvpExecution* execution, uint64_t function, uint64_t argument0,
                           uint64_t argument1, uint64_t argument2);
uint64_t mvp_op_construct4(MvpExecution* execution, uint64_t function, uint64_t argument0,
                           uint64_t argument1, uint64_t argument2, uint64_t argument3);
uint64_t mvp_op_construct5(MvpExecution* execution, uint64_t function, uint64_t argument0,
                           uint64_t argument1, uint64_t argument2, uint64_t argument3,
                           uint64_t argument4);
uint64_t mvp_op_construct6(MvpExecution* execution, uint64_t function, uint64_t argument0,
                           uint64_t argument1, uint64_t argument2, uint64_t argument3,
                           uint64_t argument4, uint64_t argument5);
uint64_t mvp_op_construct7(MvpExecution* execution, uint64_t function, uint64_t argument0,
                           uint64_t argument1, uint64_t argument2, uint64_t argument3,
                           uint64_t argument4, uint64_t argument5, uint64_t argument6);
uint64_t mvp_op_construct8(MvpExecution* execution, uint64_t function, uint64_t argument0,
                           uint64_t argument1, uint64_t argument2, uint64_t argument3,
                           uint64_t argument4, uint64_t argument5, uint64_t argument6,
                           uint64_t argument7);
uint64_t mvp_op_call0(MvpExecution* execution, uint64_t function, uint64_t receiver);
uint64_t mvp_op_call1(MvpExecution* execution, uint64_t function, uint64_t receiver,
                      uint64_t argument0);
uint64_t mvp_op_call2(MvpExecution* execution, uint64_t function, uint64_t receiver,
                      uint64_t argument0, uint64_t argument1);
uint64_t mvp_op_call3(MvpExecution* execution, uint64_t function, uint64_t receiver,
                      uint64_t argument0, uint64_t argument1, uint64_t argument2);
uint64_t mvp_op_call4(MvpExecution* execution, uint64_t function, uint64_t receiver,
                      uint64_t argument0, uint64_t argument1, uint64_t argument2,
                      uint64_t argument3);
uint64_t mvp_op_call5(MvpExecution* execution, uint64_t function, uint64_t receiver,
                      uint64_t argument0, uint64_t argument1, uint64_t argument2,
                      uint64_t argument3, uint64_t argument4);
uint64_t mvp_op_call6(MvpExecution* execution, uint64_t function, uint64_t receiver,
                      uint64_t argument0, uint64_t argument1, uint64_t argument2,
                      uint64_t argument3, uint64_t argument4, uint64_t argument5);
uint64_t mvp_op_call7(MvpExecution* execution, uint64_t function, uint64_t receiver,
                      uint64_t argument0, uint64_t argument1, uint64_t argument2,
                      uint64_t argument3, uint64_t argument4, uint64_t argument5,
                      uint64_t argument6);
uint64_t mvp_op_call8(MvpExecution* execution, uint64_t function, uint64_t receiver,
                      uint64_t argument0, uint64_t argument1, uint64_t argument2,
                      uint64_t argument3, uint64_t argument4, uint64_t argument5,
                      uint64_t argument6, uint64_t argument7);
uint64_t mvp_op_call9(MvpExecution* execution, uint64_t function, uint64_t receiver,
                      uint64_t argument0, uint64_t argument1, uint64_t argument2,
                      uint64_t argument3, uint64_t argument4, uint64_t argument5,
                      uint64_t argument6, uint64_t argument7, uint64_t argument8);
uint64_t mvp_op_super0(MvpExecution* execution, uint64_t receiver);
uint64_t mvp_op_super1(MvpExecution* execution, uint64_t receiver, uint64_t argument0);
uint64_t mvp_op_super2(MvpExecution* execution, uint64_t receiver, uint64_t argument0,
                       uint64_t argument1);
uint64_t mvp_op_super3(MvpExecution* execution, uint64_t receiver, uint64_t argument0,
                       uint64_t argument1, uint64_t argument2);
uint64_t mvp_op_super4(MvpExecution* execution, uint64_t receiver, uint64_t argument0,
                       uint64_t argument1, uint64_t argument2, uint64_t argument3);
uint64_t mvp_op_super5(MvpExecution* execution, uint64_t receiver, uint64_t argument0,
                       uint64_t argument1, uint64_t argument2, uint64_t argument3,
                       uint64_t argument4);
uint64_t mvp_op_super6(MvpExecution* execution, uint64_t receiver, uint64_t argument0,
                       uint64_t argument1, uint64_t argument2, uint64_t argument3,
                       uint64_t argument4, uint64_t argument5);
uint64_t mvp_op_super7(MvpExecution* execution, uint64_t receiver, uint64_t argument0,
                       uint64_t argument1, uint64_t argument2, uint64_t argument3,
                       uint64_t argument4, uint64_t argument5, uint64_t argument6);
uint64_t mvp_op_super8(MvpExecution* execution, uint64_t receiver, uint64_t argument0,
                       uint64_t argument1, uint64_t argument2, uint64_t argument3,
                       uint64_t argument4, uint64_t argument5, uint64_t argument6,
                       uint64_t argument7);

// Parsing is deliberately isolated from execution: this adapter borrows only
// the existing JS parser/AST builder and never creates legacy execution state.
MvpAstUnit* mvp_ast_parse(const char* source, size_t source_length);
void* mvp_ast_root(const MvpAstUnit* unit);
void mvp_ast_destroy(MvpAstUnit* unit);

// This is the new direct-MIR entry. Its admitted surface grows with the MVP;
// unsupported executable AST receives a diagnostic rather than old-engine fallthrough.
MvpExecutionResult mvp_execute_source(const char* source, size_t source_length);
