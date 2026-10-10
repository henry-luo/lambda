#pragma once
#include "../../lambda-data.hpp"

struct MvpLmdProgram;
struct AstClassNode;
struct MvpLmdHost;
enum MvpLmdHostBinding { LMD_HOST_PERFORMANCE, LMD_HOST_CONSOLE, LMD_HOST_PROCESS,
    LMD_HOST_REQUIRE, LMD_HOST_FS, LMD_HOST_COUNT };
struct MvpLmdHostState {
    Item* values;                // execution-registered roots; borrowed by host callables
    double origin;
    FILE* output;
};
Item mvp_lmd_host_initialize(MvpLmdProgram* program, MvpLmdHostState* state,
    TypeMap* shape, const MvpLmdHost* host);
struct MvpLmdClass {
    TypeNominal nominal;
    TypeNominal prototype_nominal;
    TypeMap shape;
    TypeMap prototype_shape;
    TypeMap static_shape;
    MvpLmdProgram* program;
    AstClassNode* ast;
    Item* values;                // constructor, prototype, statics; rooted program slots
    uint32_t slot;
    int64_t constructor_id;
    TypeMap* allocation_shape;
    String** nullable_initializers;
    int nullable_initializer_count;
    uint64_t allocation_epoch;
    bool reusable_layout;
    Function* owner;             // ordinary callable; nonmoving GC object, traced from nominal instances
    uint8_t property_slot;       // first of three metadata Items in its closure environment
    uint32_t initializer_slot;
    uint32_t initializer_count;
    Item* initializers;         // indexed field Functions, in registered program slots
};
static inline Item* mvp_lmd_class_values(MvpLmdClass* cls) {
    return cls->owner ? (Item*)cls->owner->closure_env + cls->property_slot : cls->values;
}
// extended only for receiver-aware units; existing MVP function allocation stays unchanged.
struct MvpLmdCallable : Function {
    MvpLmdProgram* program;
    MvpLmdClass* home;
    bool constructor;
    bool static_method;
    bool native_constructor;
    bool non_constructible;
    MvpLmdClass* properties;     // lazy ordinary-function own/prototype storage
};
typedef Item (*MvpLmdNativeEntry)(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item);
struct MvpLmdNativeCallable : MvpLmdCallable { void* state; };
Item mvp_lmd_native_function(MvpLmdProgram* program, void* state, MvpLmdNativeEntry entry, uint8_t arity);
Item mvp_lmd_named_set(Item owner, const char* key, Item value);
enum MvpLmdLibraryBinding { LMD_LIBRARY_JSON, LMD_LIBRARY_DATE, LMD_LIBRARY_DATE_PROTOTYPE,
    LMD_LIBRARY_DATE_STATICS, LMD_LIBRARY_REGEXP, LMD_LIBRARY_REGEXP_PROTOTYPE,
    LMD_LIBRARY_REGEXP_STATICS, LMD_LIBRARY_CALL, LMD_LIBRARY_APPLY, LMD_LIBRARY_BIND,
    LMD_LIBRARY_OBJECT, LMD_LIBRARY_OBJECT_PROTOTYPE, LMD_LIBRARY_OBJECT_STATICS,
    LMD_LIBRARY_ARRAY, LMD_LIBRARY_ARRAY_PROTOTYPE, LMD_LIBRARY_ARRAY_STATICS,
    LMD_LIBRARY_GENERATOR, LMD_LIBRARY_GENERATOR_PROTOTYPE, LMD_LIBRARY_GENERATOR_STATICS,
    LMD_LIBRARY_SYMBOL, LMD_LIBRARY_SYMBOL_PROTOTYPE, LMD_LIBRARY_SYMBOL_STATICS, LMD_LIBRARY_ITERATOR_SYMBOL,
    LMD_LIBRARY_ITERATOR, LMD_LIBRARY_ITERATOR_PROTOTYPE, LMD_LIBRARY_ITERATOR_STATICS,
    LMD_LIBRARY_SEQUENCE_ITERATOR,
    LMD_LIBRARY_MAP, LMD_LIBRARY_MAP_PROTOTYPE, LMD_LIBRARY_MAP_STATICS,
    LMD_LIBRARY_SET, LMD_LIBRARY_SET_PROTOTYPE, LMD_LIBRARY_SET_STATICS,
    LMD_LIBRARY_MATH, LMD_LIBRARY_COUNT };
struct MvpLmdLibraryState {
    Item* values;
    TypeMap* object_shape;
    TypeMap json_shape;
    MvpLmdClass date;
    MvpLmdClass regexp;
    MvpLmdClass object;
    MvpLmdClass array;
    MvpLmdClass generator;
    MvpLmdClass symbol;
    MvpLmdClass iterator;
    MvpLmdClass map;
    MvpLmdClass set;
};
Item mvp_lmd_library_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state);
Item mvp_lmd_library_class_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state,
    MvpLmdClass* cls, int index, const char* name, MvpLmdNativeEntry entry, uint8_t arity);
Item mvp_lmd_regexp_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state);
Item mvp_lmd_function_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state);
Item mvp_lmd_generator_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state);
Item mvp_lmd_generator_call(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item);
Item mvp_lmd_function_method(Item owner, String* name);
Item mvp_lmd_function_forward(Item function, Item receiver, Item prefix, Item* arguments,
    uint64_t count, int skip = 0, MvpLmdNativeEntry entry = nullptr,
    Item target = Item{.item = ITEM_JS_UNDEFINED});
MvpLmdLibraryState* mvp_lmd_program_library(MvpLmdProgram* program);
Item mvp_lmd_object_create(MvpLmdProgram* program, Item prototype);
extern const TypeNominalExtension mvp_lmd_class_extension;
MvpLmdClass* mvp_lmd_class_record(Item owner);
struct MvpLmdPropertyCacheEntry {
    TypeMap* shape;
    ShapeEntry* field;
    Item inherited;
    int64_t offset;
    uint64_t pointer_tag;
    TypeId storage;
    uint8_t pointer_lane;
    bool writable;
    bool nullable_initializer;
};
enum { MVP_LMD_PROPERTY_CACHE_SIZE = 4 };
struct MvpLmdPropertyCache {
    MvpLmdPropertyCacheEntry entries[MVP_LMD_PROPERTY_CACHE_SIZE];
    uint8_t next;
};
enum MvpLmdFailure { LMD_MVP_CAPABILITY, LMD_MVP_REFERENCE, LMD_MVP_TYPE,
    LMD_MVP_RANGE, LMD_MVP_MEMORY, LMD_MVP_SYNTAX };

// internal callee-only marker; never exposed as a JavaScript value.
static inline uint64_t mvp_lmd_method_token(int64_t method) {
    return ITEM_JS_UNDEFINED | ((uint64_t)method << 8);
}

// shared compile-time/runtime spelling classification; no JIT import is needed.
enum MvpLmdMethod { LMD_METHOD_FILL = 9, LMD_METHOD_PUSH, LMD_METHOD_POP,
    LMD_METHOD_JOIN, LMD_METHOD_CHAR_AT, LMD_METHOD_CODE_AT, LMD_METHOD_REPEAT,
    LMD_METHOD_SLICE, LMD_METHOD_FOREACH, LMD_METHOD_ARRAY_MAP,
    LMD_METHOD_SUBSTRING, LMD_METHOD_STRING_SLICE, LMD_METHOD_SPLIT,
    LMD_METHOD_INDEX_OF, LMD_METHOD_STARTS_WITH, LMD_METHOD_UPPER, LMD_METHOD_TO_FIXED,
    LMD_METHOD_REVERSE, LMD_METHOD_SORT, LMD_METHOD_BIGINT_STRING,
    LMD_METHOD_FLAT, LMD_METHOD_REDUCE, LMD_METHOD_SOME, LMD_METHOD_INCLUDES,
    LMD_METHOD_ARRAY_STRING, LMD_METHOD_MATCH, LMD_METHOD_REPLACE, LMD_METHOD_LOWER,
    LMD_METHOD_CONCAT, LMD_METHOD_SPLICE, LMD_METHOD_UNSHIFT,
    LMD_METHOD_FILTER, LMD_METHOD_ARRAY_INDEX_OF, LMD_METHOD_ENDS_WITH, LMD_METHOD_PAD_START,
    LMD_METHOD_NUMBER_STRING, LMD_METHOD_SHIFT, LMD_METHOD_TO_REVERSED, LMD_METHOD_AT, LMD_METHOD_COUNT };
enum MvpLmdStringRead { LMD_STRING_INDEX, LMD_STRING_CHAR, LMD_STRING_CODE, LMD_STRING_FROM_CODE, LMD_STRING_FROM_POINT };
enum MvpLmdProperty { LMD_PROP_GET, LMD_PROP_CALLEE, LMD_PROP_SET, LMD_PROP_DELETE,
    LMD_PROP_OWN, LMD_PROP_HAS, LMD_PROP_KEYS, LMD_PROP_VALUES, LMD_PROP_ENTRIES,
    LMD_PROP_INITIALIZE, LMD_PROP_STRICT = 256 };
int mvp_lmd_builtin_method(String* key, TypeId owner = LMD_TYPE_MAP);
TypeId mvp_lmd_method_owner(int method);

extern "C" {
Item mvp_lmd_fail(int64_t kind, int64_t site);
double mvp_lmd_string_to_number(String* string);
double mvp_lmd_parse_number(String* string, int64_t prefix);
Item mvp_lmd_number_to_string(double value);
Item mvp_lmd_number_to_radix_string(double value, double radix);
Item mvp_lmd_primitive_to_string(Item value, int64_t library_objects = 1);
Item mvp_lmd_primitive_to_number(Item value);
Item mvp_lmd_to_primitive(Item value, int64_t hint, int64_t library_objects = 1);
Item mvp_lmd_caught_value(MvpLmdLibraryState* state, Item error);
Item mvp_lmd_generator_park(Item value, int64_t kind);
Item mvp_lmd_iterator_get(MvpLmdProgram* program, Item source);
Item mvp_lmd_iterator_step(Item record, Item value, int64_t count);
Item mvp_lmd_iterator_close(Item record, Item completion);
Item mvp_lmd_iterator_collect(MvpLmdProgram* program, Item target, Item source, Item mapper, Item receiver);
Item mvp_lmd_call_array(Item function, Item receiver, Item arguments, Item target);
int64_t mvp_lmd_truthy(Item value);
int64_t mvp_lmd_generator_resume_kind();
Item mvp_lmd_array_method_value(MvpLmdProgram* program, Item owner, Item name, int64_t callee = 0);
Item mvp_lmd_bigint_binary(Item left, Item right, int64_t operation);
Item mvp_lmd_bigint_to_string(Item value, double radix);
double mvp_lmd_parse_integer(String* value, int64_t radix);
Item mvp_lmd_number_to_fixed(double value, double digits);
Item mvp_lmd_string_concat(Item left, Item right);
int64_t mvp_lmd_string_compare(Item left, Item right);
Item mvp_lmd_string_at(Item string, double index, int64_t mode);
Item mvp_lmd_string_range(Item string, double start, double end, int64_t slice);
int64_t mvp_lmd_string_search(Item string, Item needle, double start, int64_t prefix);
Item mvp_lmd_string_pad(Item string, double length, Item filler);
Item mvp_lmd_string_split(Item string, Item separator, int64_t limit);
double mvp_lmd_number_pow(double base, double exponent);
int64_t mvp_lmd_string_key(String* string, int64_t typed = 0);
Item mvp_lmd_array_store(Item array, uint32_t index, Item value);
Item mvp_lmd_array_new(int64_t length);
Item mvp_lmd_array_flatten(Item source, double depth);
Item mvp_lmd_array_to_string(Item source, Item separator, int64_t library_objects = 1);
Item mvp_lmd_regexp_new(MvpLmdLibraryState* state, Item pattern, Item flags);
Item mvp_lmd_regexp_string_method(MvpLmdLibraryState* state, Item source, Item pattern,
    Item replacement, int64_t replace);
Item mvp_lmd_array_resize(Item owner, int64_t length);
Item mvp_lmd_array_edit(Item owner, Item* args, int64_t count, int64_t method);
Item mvp_lmd_array_visit(Item owner, Item* args, int64_t count, int64_t method);
Item mvp_lmd_for_in_keys(Item owner);
Item mvp_lmd_define_data_property(Item owner, Item name, Item descriptor);
Item mvp_lmd_array_sort(Item owner, Item comparator);
Item mvp_lmd_array_spread(Item target, Item source, int64_t projection);
Item mvp_lmd_property_key(Item string);
Item mvp_lmd_object_new(TypeMap* shape, int64_t collection);
Item mvp_lmd_property_get(Item owner, Item name, int64_t callee);
// mutation helpers borrow caller-rooted Items and stable scalar homes through the complete call.
Item mvp_lmd_property_set(Item owner, Item name, Item value);
Item mvp_lmd_property_delete(Item owner, Item name);
Item mvp_lmd_property_has(Item owner, Item name, int64_t inherited);
Item mvp_lmd_object_project(Item owner, int64_t projection);
Item mvp_lmd_object_copy(Item target, Item source, int64_t assign);
Item mvp_lmd_map_call(Item owner, Item method, Item key, Item value);
int64_t mvp_lmd_map_next(Item owner, int64_t cursor);
Item mvp_lmd_map_entry(Item owner, int64_t cursor, int64_t projection);
Item mvp_lmd_function_new(uint64_t code_id, MvpLmdProgram* program);
Item mvp_lmd_function_prepare(Item function);
Item mvp_lmd_class_new(MvpLmdClass* plan, Item base);
Item mvp_lmd_class_initialize_instance(Item constructor, Item receiver);
Item mvp_lmd_class_invoke(Item callee, Item receiver, Item* arguments, int64_t argc, Item new_target);
Item mvp_lmd_class_super(Item function, int64_t constructor);
Item mvp_lmd_constructor_result(Item value, Item receiver, int64_t derived);
Item mvp_lmd_instanceof(Item value, Item constructor);
Item mvp_lmd_throw(Item value, int64_t error_constructor);
Item mvp_lmd_class_property(Item owner, Item name, Item value, int64_t operation,
    MvpLmdPropertyCache* cache);
}
