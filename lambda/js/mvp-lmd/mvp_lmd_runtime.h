#pragma once

enum { MVP_LMD_CAPSULE_OWNER = 0x4d56504c, MVP_LMD_DYNAMIC_OWNER = 0x4d564459,
    MVP_LMD_MODULE_OWNER = 0x4d564d4f, MVP_LMD_REGEXP_OWNER = 0x4d565245 };
#include "../../lambda-data.hpp"

struct MvpLmdProgram;
struct MvpLmdRegExpAccessor;
bool mvp_lmd_regexp_intrinsic(Item receiver, Item argument, int operation, Item* result,
    Item second = Item{.item = ITEM_JS_UNDEFINED});
struct JsRegexpLastMatch;
struct JsArrayOps;
struct JsStaticLiteralRecipe;
struct JsBufferViewOps;
extern const JsArrayOps mvp_lmd_array_ops;
extern const JsBufferViewOps mvp_lmd_buffer_ops;
struct JsCollectionOps;
struct JsCollectionStorageOps;
extern const JsCollectionOps mvp_lmd_collection_ops;
struct MvpLmdWeakFamily { const char* name; int prototype_owner; };
extern const MvpLmdWeakFamily mvp_lmd_weak_families[4];
extern const JsCollectionStorageOps mvp_lmd_collection_storage_ops;
struct AstClassNode;
struct JsTemplateLiteralNode;
struct JsTemplateObjectOps;
struct MvpLmdHost;
enum MvpLmdHostBinding { LMD_HOST_PERFORMANCE, LMD_HOST_CONSOLE, LMD_HOST_PROCESS,
    LMD_HOST_REQUIRE, LMD_HOST_PRINT, LMD_HOST_COUNT };
enum MvpLmdTimerBinding { LMD_TIMER_TIMEOUT, LMD_TIMER_CLEAR_TIMEOUT, LMD_TIMER_INTERVAL,
    LMD_TIMER_CLEAR_INTERVAL, LMD_TIMER_MICROTASK, LMD_TIMER_COUNT };
struct MvpLmdHostState {
    Item* values;                // execution-registered roots; borrowed by host callables
    Item* timers;                // optional timer functions in separately registered program slots
    Item* global;                // borrowed registered global receiver cell
    FILE* output;
    bool native_timers;
    bool process_requested;
    bool process_stdio_only;    // closed direct writes need no optional Node-core extension
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
    bool reusable_layout;
    bool dynamic;              // definition needs fresh per-evaluation nominal metadata
    Function* owner;             // ordinary callable; nonmoving GC object, traced from nominal instances
    uint8_t property_slot;       // first of three metadata Items in its closure environment
    uint8_t private_method_mask; // instance/static templates occupy optional slots after field keys
    bool has_instance_elements; // one admission fact for fields or private methods, in existing padding
    bool has_private_names;
    bool eval_environment;      // admission-only: retain lexical private records for direct eval
    bool indirect_values;       // large metadata uses one precisely traced Array closure slot
    uint32_t initializer_slot;
    uint32_t initializer_count;
    uint32_t field_count;
    Item* initializers;         // indexed field Functions, in registered program slots
    Item* field_keys;           // dynamic class keys, in the constructor's traced environment
    uint32_t* private_field_slots; // passive instance keys copied from precisely rooted program slots
};
static inline bool mvp_lmd_class_has_constructor(const MvpLmdClass* cls) {
    // interpreted constructors own a source definition, never a synthetic MIR id.
    return cls->constructor_id >= 0 || (cls->dynamic && cls->owner && cls->owner->def);
}
static inline Item* mvp_lmd_class_values(MvpLmdClass* cls) {
    return cls->owner && !cls->indirect_values
        ? (Item*)cls->owner->closure_env + cls->property_slot : cls->values;
}
// extended only for receiver-aware units; existing MVP function allocation stays unchanged.
struct MvpLmdCallable : Function {
    MvpLmdProgram* program;
    MvpLmdClass* home;
    bool constructor;
    bool static_method;
    bool native_constructor;
    bool non_constructible;
    bool custom_new_target;    // native constructor implements GetPrototypeFromConstructor
    uint8_t home_slot;          // one-based traced closure slot: literal home Map or enclosing method Function
    bool native_source;        // native extension retains its original name independently of own properties
    bool foreign_properties;  // native wrapper forwards its target's property capabilities
    MvpLmdClass* properties;     // lazy ordinary-function own/prototype storage
};
static_assert(sizeof(MvpLmdCallable) == sizeof(Function) + 4 * sizeof(void*),
    "the optional home slot must fit the existing callable padding");
typedef Item (*MvpLmdNativeEntry)(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item);
struct MvpLmdNativeCallable : MvpLmdCallable { void* state; NameRef source_name; };
extern const TypeNominalExtension mvp_lmd_plain_extension;
extern const TypeNominalExtension mvp_lmd_class_extension;
// foreign storage already declares its owner; ordinary MVP shapes need no extra flag.
static inline bool mvp_lmd_foreign_candidate(Item value, bool lambda_reads = false) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_MAP) {
        TypeMap* shape = (TypeMap*)value.map->type;
        if (shape && shape->js_meta && !shape->nominal && value.map->map_kind != MAP_KIND_PROXY) return true;
        if (!lambda_reads || !shape || value.map->map_kind == MAP_KIND_PROXY) return false;
        TypeNominal* nominal = shape->nominal;
        return !nominal || (nominal->extension != &mvp_lmd_plain_extension &&
            nominal->extension != &mvp_lmd_class_extension);
    }
    if (type == LMD_TYPE_ARRAY) {
        // host arrays carry their prototype/descriptor owner in the existing
        // companion map; MVP arrays keep their ordinary shape dispatch.
        Map* properties = js_array_props(value.array);
        TypeMap* shape = properties ? (TypeMap*)properties->type : NULL;
        return shape && shape->js_meta && !shape->nominal;
    }
    if (type == LMD_TYPE_FUNC) return value.function->entry_abi == FN_ENTRY_ABI_JS_FUNCTION ||
        (value.function->entry_abi == FN_ENTRY_ABI_MVP_LMD && value.function->requires_runtime_context &&
            ((MvpLmdCallable*)value.function)->foreign_properties);
    return is_virtual_container_type_id(type);
}
// native ingress must follow the owner's ABI/shape instead of casting its private tail.
static inline bool mvp_lmd_owns_properties(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_FUNC) return function_has_abi(value.function, FN_ENTRY_ABI_MVP_LMD);
    if (type == LMD_TYPE_MAP) {
        TypeNominal* nominal = ((TypeMap*)value.map->type)->nominal;
        return nominal && (nominal->extension == &mvp_lmd_plain_extension ||
            nominal->extension == &mvp_lmd_class_extension);
    }
    return type == LMD_TYPE_ARRAY && !mvp_lmd_foreign_candidate(value);
}
static inline MvpLmdProgram* mvp_lmd_property_program(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_FUNC) return function_has_abi(value.function, FN_ENTRY_ABI_MVP_LMD) &&
        value.function->requires_runtime_context ? ((MvpLmdCallable*)value.function)->program : NULL;
    Map* storage = type == LMD_TYPE_MAP ? value.map :
        type == LMD_TYPE_ARRAY && js_array_has_props(value.array) ? js_array_props(value.array) : NULL;
    TypeNominal* nominal = storage && storage->type ? ((TypeMap*)storage->type)->nominal : NULL;
    if (!nominal) return NULL;
    if (nominal->extension == &mvp_lmd_class_extension)
        return ((MvpLmdClass*)nominal->extension_data)->program;
    return nominal->extension == &mvp_lmd_plain_extension ? (MvpLmdProgram*)nominal->extension_data : NULL;
}
Item mvp_lmd_native_function(MvpLmdProgram* program, void* state, MvpLmdNativeEntry entry, uint8_t arity,
    const char* name = NULL);
Item mvp_lmd_named_set(Item owner, const char* key, Item value, int descriptor_flags = -1);
Item mvp_lmd_named_set(Item owner, Item key, Item value, int descriptor_flags);
Item mvp_lmd_object_value(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item receiver, Item);
Item mvp_lmd_object_string(Item receiver);
Item mvp_lmd_receiver_identity(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item receiver, Item);
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
    LMD_LIBRARY_MATH, LMD_LIBRARY_FUNCTION_PROTOTYPE, LMD_LIBRARY_SYMBOL_VALUES,
    LMD_LIBRARY_REFLECT, LMD_LIBRARY_PROXY, LMD_LIBRARY_FUNCTION,
    LMD_LIBRARY_THROW_TYPE, LMD_LIBRARY_FUNCTION_STRING, LMD_LIBRARY_GLOBAL_NAN,
    LMD_LIBRARY_GLOBAL_FINITE, LMD_LIBRARY_DECODE_URI, LMD_LIBRARY_ENCODE_URI,
    LMD_LIBRARY_DECODE_COMPONENT, LMD_LIBRARY_ENCODE_COMPONENT, LMD_LIBRARY_REGEXP_EXEC,
    LMD_LIBRARY_ESCAPE, LMD_LIBRARY_UNESCAPE, LMD_LIBRARY_STRING_ITERATOR,
    LMD_LIBRARY_PARSE_INT, LMD_LIBRARY_PARSE_FLOAT, LMD_LIBRARY_GC,
    LMD_LIBRARY_STRUCTURED_CLONE, LMD_LIBRARY_COUNT };
enum MvpLmdIteratorFamily { LMD_ITERATOR_MAP, LMD_ITERATOR_SET, LMD_ITERATOR_STRING, LMD_ITERATOR_COUNT };
extern const char* const mvp_lmd_iterator_names[LMD_ITERATOR_COUNT];
enum MvpLmdErrorKind { LMD_ERROR_BASE, LMD_ERROR_EVAL, LMD_ERROR_RANGE,
    LMD_ERROR_REFERENCE, LMD_ERROR_SYNTAX, LMD_ERROR_TYPE, LMD_ERROR_URI,
    LMD_ERROR_AGGREGATE, LMD_ERROR_COUNT };
extern const char* const mvp_lmd_error_names[LMD_ERROR_COUNT];
struct MvpLmdLibraryState;
int mvp_lmd_error_kind(const TypeNominal* nominal, const MvpLmdLibraryState* library);
enum MvpLmdPrimitiveKind { LMD_PRIMITIVE_STRING, LMD_PRIMITIVE_NUMBER,
    LMD_PRIMITIVE_BOOLEAN, LMD_PRIMITIVE_BIGINT, LMD_PRIMITIVE_COUNT };
extern const char* const mvp_lmd_primitive_names[LMD_PRIMITIVE_COUNT];
struct MvpLmdLibraryState {
    Item* values;
    TypeMap* object_shape;
    TypeMap json_shape;
    TypeMap proxy_shape;
    NameRef prototype_key;
    NameRef extensibility_key;
    bool mutable_prototypes;
    bool function_prototype_observed;
    bool native_callable_values; // native-first realms retain their existing callable identities
    bool intrinsics_ready; // native factories may borrow identities after initialization completes
    bool date_observed;
    MvpLmdClass date;
    MvpLmdClass regexp;
    MvpLmdClass* regexp_iterator; // optional observable RegExp String Iterator family
    Item* regexp_iterator_values; // three existing registered program slots
    NameRef regexp_iterator_keys[5]; // private regexp/string/global/unicode/done state
    NameRef regexp_source_key;
    NameRef regexp_flags_key;
    MvpLmdRegExpAccessor* regexp_accessors; // initial layout metadata; live descriptors are checked on every use
    TypeMap* regexp_accessor_shape; // pool-owned initial prototype shape, never a successful-guard cache
    int regexp_accessor_count;
    bool regexp_protocol_unobserved; // final whole-program proof; native numeric/data checks remain runtime checks
    JsRegexpLastMatch* regexp_last_match; // realm capsule owns precise legacy-state roots
    MvpLmdClass object;
    MvpLmdClass array;
    MvpLmdClass generator;
    MvpLmdClass symbol;
    MvpLmdClass iterator;
    MvpLmdClass* iterator_base; // optional shared IteratorPrototype, distinct from sequence prototypes
    Item* iterator_base_values; // three registered slots, only the prototype slot contains an object
    MvpLmdClass* iterator_families; // optional observable Map/Set/String iterator prototype families
    Item* iterator_family_values; // existing registered program slots, three per nominal family
    MvpLmdClass map;
    MvpLmdClass set;
    MvpLmdClass* weak_collections; // optional shared native ephemeron carriers
    int weak_class_count;
    Item* weak_values;
    MvpLmdClass* errors;        // optional nominal families; values live in registered program slots
    Item* error_values;
    NameRef error_data_key;     // native Error allocation brand, independent of nominal ancestry
    MvpLmdClass* primitives;
    Item* primitive_values;
    MvpLmdClass* buffers;       // optional ArrayBuffer/DataView carrier families
    Item* buffer_values;
    MvpLmdClass* shared_buffer; // separately optional shared storage nominal family
    Item* shared_buffer_values;
    MvpLmdClass* typed_arrays;  // twelve concrete families plus the abstract TypedArray constructor
    Item* typed_values;
    MvpLmdClass* promise;       // shared Promise carrier with optional MVP nominal metadata
    Item* promise_values;
    MvpLmdClass* async_generator; // optional async generator nominal/prototype family
    Item* async_generator_values;
    MvpLmdClass* async_function;
    Item* async_function_values;
    Item* generator_function_values; // shared IteratorPrototype alias and GeneratorFunction.prototype
    bool test262_host;
    NameRef primitive_key;      // boxed primitive payload, traced by the shared Map layout
    NameRef arguments_key;      // private Array of aliases to existing mutable parameter cells
    int abort_kind;             // one-based non-JS failure, retained across finally completions
    int64_t abort_site;
    Item* atomics_value;        // optional namespace in a registered program slot
    MvpLmdLibraryState* default_realm; // unstamped native arrays belong to this realm
};
enum MvpLmdInternalName {
    LMD_INTERNAL_PROTOTYPE, LMD_INTERNAL_EXTENSIBLE, LMD_INTERNAL_PRIMITIVE,
    LMD_INTERNAL_DATE, LMD_INTERNAL_ERROR, LMD_INTERNAL_ARGUMENTS,
    LMD_INTERNAL_REGEXP_SOURCE, LMD_INTERNAL_REGEXP_FLAGS,
    LMD_INTERNAL_REGEXP_ITERATOR, LMD_INTERNAL_REGEXP_ITERATOR_STRING,
    LMD_INTERNAL_REGEXP_ITERATOR_GLOBAL, LMD_INTERNAL_REGEXP_ITERATOR_UNICODE,
    LMD_INTERNAL_REGEXP_ITERATOR_DONE, LMD_INTERNAL_ITERATOR_NEXT,
    LMD_INTERNAL_CUSTOM_ELEMENT_NAME, LMD_INTERNAL_URL,
    LMD_INTERNAL_PERFORMANCE_OBSERVER, LMD_INTERNAL_PERFORMANCE_ENTRY_LIST,
    LMD_INTERNAL_NAME_COUNT
};
NameRef mvp_lmd_internal_name(MvpLmdInternalName name);
extern __thread MvpLmdLibraryState* mvp_lmd_foreign_library;
class MvpLmdRealmScope {
    MvpLmdLibraryState* previous;
    MvpLmdLibraryState* foreign;
    MvpLmdLibraryState* current;
    void* previous_promise_context = NULL;
public:
    explicit MvpLmdRealmScope(MvpLmdProgram* program, bool native_realm = false);
    ~MvpLmdRealmScope();
    Item complete(Item result, bool force_error_payload = false);
};
Item mvp_lmd_library_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state);
Item mvp_lmd_realm_intrinsic(const char* name, size_t length);
Runtime* mvp_lmd_prepare_module_realm(void* owner);
Item mvp_lmd_structured_clone(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item);
Item mvp_lmd_library_class_adopt(MvpLmdClass* cls, Item* slots, Item constructor);
Item mvp_lmd_library_class_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state,
    MvpLmdClass* cls, int index, const char* name, MvpLmdNativeEntry entry, uint8_t arity,
    bool immutable_metadata = true);
Item mvp_lmd_regexp_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state);
Item mvp_lmd_function_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state);
Item mvp_lmd_function_intrinsic(int builtin_id);
Item mvp_lmd_generator_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state);
Item mvp_lmd_iterator_result(MvpLmdProgram* program, Item value, bool done);
Item mvp_lmd_iterator_prototype_initialize(MvpLmdProgram* program, MvpLmdClass* cls,
    Item* slots, const char* tag, Item existing = ItemNull);
Item mvp_lmd_buffer_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state);
Item mvp_lmd_date_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state);
Item mvp_lmd_typed_array_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state);
bool mvp_lmd_typed_array_index(Item owner, Item key, double* index);
Item mvp_lmd_typed_array_property(Item owner, Item key, Item value, int64_t operation, Item receiver);
Item mvp_lmd_typed_array_store(Item owner, double index, Item value);
Item mvp_lmd_generator_call(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item);
Item mvp_lmd_generator_resume(MvpLmdProgram*, Item receiver, Item value, int operation);
Item mvp_lmd_generator_create_ast(MvpLmdProgram*, Item internal, Item wrapper,
    Item arguments, Item receiver, bool is_async);
Item mvp_lmd_function_method(Item owner, String* name);
Item mvp_lmd_function_source(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item);
extern "C" Item mvp_lmd_arguments_new(Item function, Item* values, uint64_t count, Item cells, int64_t unmapped);
Item mvp_lmd_arguments_cell(Item owner, String* key, bool detach = false);
Item mvp_lmd_function_bound(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item);
Item mvp_lmd_function_forward(Item function, Item receiver, Item prefix, Item* arguments,
    uint64_t count, int skip = 0, MvpLmdNativeEntry entry = nullptr,
    Item target = Item{.item = ITEM_JS_UNDEFINED});
MvpLmdProgram* mvp_lmd_program_realm(MvpLmdProgram* program);
MvpLmdProgram* mvp_lmd_program_for_global(MvpLmdProgram* program, Item global);
uint32_t mvp_lmd_program_realm_key(MvpLmdProgram* program);
Item mvp_lmd_dynamic_import(MvpLmdProgram* program, Item specifier, const char* reference);
Item mvp_lmd_create_realm(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item);
Item mvp_lmd_function_realm(Item function, MvpLmdProgram** program);
Item mvp_lmd_constructor_prototype(Item target, Item intrinsic);
Item mvp_lmd_host_task(Item callback, Item receiver, Item arguments);
MvpLmdLibraryState* mvp_lmd_program_library(MvpLmdProgram* program = NULL);
const char* mvp_lmd_program_reference(MvpLmdProgram* program);
struct MvpLmdBuiltinMethod { const char* name; TypeId owner; int id; uint8_t arity = 1; };
extern const MvpLmdBuiltinMethod mvp_lmd_builtin_methods[];
extern const int mvp_lmd_builtin_method_count;
// full-width Boolean results share one native/MIR ABI.
extern "C" int64_t mvp_lmd_same_value_zero(Item left, Item right);
// Ordered collection methods require the actual storage carrier, including through aliases.
static inline bool mvp_lmd_is_ordered_map(Item value) {
    if (get_type_id(value) != LMD_TYPE_MAP || value.map->map_kind != MAP_KIND_ORDERED) return false;
    TypeMap* shape = (TypeMap*)value.map->type;
    return shape && shape->nominal && !shape->js_meta;
}
bool mvp_lmd_same_value(Item left, Item right);
Item mvp_lmd_property_symbol(NameRef name);
struct JsPropertyDescriptor;
// returned numeric values borrow the rooted owner until the caller adopts them.
Item mvp_lmd_get_own_property(Item owner, Item key, JsPropertyDescriptor* descriptor);
Item mvp_lmd_define_own_property(Item owner, Item key, const JsPropertyDescriptor* descriptor);
Item mvp_lmd_descriptor_from_object(Item object, JsPropertyDescriptor* descriptor, uint64_t* home);
Item mvp_lmd_descriptor_object(const JsPropertyDescriptor* descriptor, bool null_prototype = false);
Item mvp_lmd_set_completion(Item owner, Item key, Item value, Item receiver);
Item mvp_lmd_reflect_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state);
Item mvp_lmd_arguments_list(Item source, bool property_keys = false);
Item mvp_lmd_own_keys(Item owner);
Item mvp_lmd_sequence_length(Item source);
Item mvp_lmd_get_prototype_with_library(Item object, MvpLmdLibraryState* library);
Item mvp_lmd_array_is_array(Item source);
Item mvp_lmd_array_species_create(Item source, int64_t length, bool* fresh = NULL);
bool mvp_lmd_is_callable(Item value);
bool mvp_lmd_is_constructor(Item value);
static inline bool mvp_lmd_is_proxy(Item value) {
    return get_type_id(value) == LMD_TYPE_MAP && value.map->map_kind == MAP_KIND_PROXY;
}
enum MvpLmdProxyOperation { LMD_PROXY_GET, LMD_PROXY_SET, LMD_PROXY_DEFINE, LMD_PROXY_DELETE,
    LMD_PROXY_HAS, LMD_PROXY_DESCRIPTOR, LMD_PROXY_KEYS, LMD_PROXY_PROTOTYPE,
    LMD_PROXY_SET_PROTOTYPE, LMD_PROXY_EXTENSIBLE, LMD_PROXY_PREVENT };
Item mvp_lmd_proxy_property(Item owner, Item key, Item value, Item receiver, int operation,
    JsPropertyDescriptor* descriptor = NULL);
Item mvp_lmd_proxy_invoke(Item owner, Item receiver, Item* args, int64_t count, Item target);
Item mvp_lmd_proxy_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state);
Item mvp_lmd_proxy_project(Item owner, int64_t projection);
Item mvp_lmd_project_keys(Item owner, Item keys, int64_t projection);
bool mvp_lmd_descriptor_compatible(bool extensible, const JsPropertyDescriptor* update, const JsPropertyDescriptor* current);
extern "C" bool js_promise_is(Item promise);
struct JsSourceModuleRecord;
JsSourceModuleRecord* js_module_namespace_record(Item object);
static inline bool mvp_lmd_is_object(Item value) {
    TypeId type = get_type_id(value);
    return type == LMD_TYPE_MAP || type == LMD_TYPE_ARRAY || type == LMD_TYPE_FUNC || type == LMD_TYPE_ARRAY_NUM ||
        is_virtual_container_type_id(type);
}
Map* mvp_lmd_property_map(Item object, bool writing = false, bool numeric_attributes = false);
Item mvp_lmd_array_own_item(Array* array, int64_t index);
Item mvp_lmd_array_property(Item owner, Item key, Item value, int64_t operation, Item receiver);
extern "C" Item mvp_lmd_array_index_property(Item owner, int64_t index, Item value, int64_t operation);
Item mvp_lmd_object_create(MvpLmdProgram* program, Item prototype);
extern "C" Item mvp_lmd_object_from_constructor(Item new_target, MvpLmdClass* fallback,
    int64_t source_layout = 0);
Item mvp_lmd_box_primitive(Item value, Item prototype = Item{.item = ITEM_JS_UNDEFINED});
MvpLmdClass* mvp_lmd_class_record(Item owner);
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
    LMD_METHOD_NUMBER_STRING, LMD_METHOD_SHIFT, LMD_METHOD_TO_REVERSED, LMD_METHOD_AT,
    LMD_METHOD_EVERY, LMD_METHOD_FIND, LMD_METHOD_FIND_INDEX, LMD_METHOD_REDUCE_RIGHT,
    LMD_METHOD_LAST_INDEX_OF, LMD_METHOD_STRING_CONCAT, LMD_METHOD_TRIM, LMD_METHOD_TRIM_START,
    LMD_METHOD_TRIM_END, LMD_METHOD_CODE_POINT_AT, LMD_METHOD_STRING_AT, LMD_METHOD_STRING_INCLUDES,
    LMD_METHOD_PAD_END, LMD_METHOD_STRING_LAST_INDEX_OF,
    LMD_METHOD_FLAT_MAP, LMD_METHOD_FIND_LAST, LMD_METHOD_FIND_LAST_INDEX, LMD_METHOD_SEARCH, LMD_METHOD_MATCH_ALL,
    LMD_METHOD_COPY_WITHIN, LMD_METHOD_TO_SORTED, LMD_METHOD_TO_SPLICED, LMD_METHOD_WITH,
    LMD_METHOD_STRING_NORMALIZE, LMD_METHOD_STRING_LOCALE_COMPARE, LMD_METHOD_STRING_IS_WELL_FORMED, LMD_METHOD_STRING_TO_WELL_FORMED,
    LMD_METHOD_STRING_ANCHOR, LMD_METHOD_STRING_BIG, LMD_METHOD_STRING_BLINK, LMD_METHOD_STRING_BOLD,
    LMD_METHOD_STRING_FIXED, LMD_METHOD_STRING_FONTCOLOR, LMD_METHOD_STRING_FONTSIZE, LMD_METHOD_STRING_ITALICS,
    LMD_METHOD_STRING_LINK, LMD_METHOD_STRING_SMALL, LMD_METHOD_STRING_STRIKE, LMD_METHOD_STRING_SUB,
    LMD_METHOD_STRING_SUP, LMD_METHOD_STRING_SUBSTR, LMD_METHOD_REPLACE_ALL, LMD_METHOD_COUNT };
static inline bool mvp_lmd_array_visit_method(int method) {
    return method == LMD_METHOD_FOREACH || method == LMD_METHOD_ARRAY_MAP || method == LMD_METHOD_FILTER ||
        method == LMD_METHOD_REDUCE || method == LMD_METHOD_SOME || method == LMD_METHOD_EVERY ||
        method == LMD_METHOD_FIND || method == LMD_METHOD_FIND_INDEX || method == LMD_METHOD_REDUCE_RIGHT ||
        method == LMD_METHOD_FLAT_MAP || method == LMD_METHOD_FIND_LAST || method == LMD_METHOD_FIND_LAST_INDEX;
}
enum MvpLmdStringRead { LMD_STRING_INDEX, LMD_STRING_CHAR, LMD_STRING_CODE, LMD_STRING_FROM_CODE, LMD_STRING_FROM_POINT };
enum MvpLmdProperty { LMD_PROP_GET, LMD_PROP_CALLEE, LMD_PROP_SET, LMD_PROP_DELETE,
    LMD_PROP_OWN, LMD_PROP_HAS, LMD_PROP_KEYS, LMD_PROP_VALUES, LMD_PROP_ENTRIES,
    LMD_PROP_INITIALIZE, LMD_PROP_CREATE_DATA, LMD_PROP_PRESENT,
    LMD_PROP_GET_BINDING, // GetBindingValue: absent properties raise ReferenceError.
    LMD_PROP_STRICT = 256,
    // operation-local proof: no indexed descriptors or inherited indices; writable length/extensible for edits.
    LMD_PROP_OWN_STORAGE = 512, LMD_PROP_NON_ENUMERABLE = 1024 };
int mvp_lmd_builtin_method(String* key, TypeId owner = LMD_TYPE_MAP);
TypeId mvp_lmd_method_owner(int method);

extern "C" {
Item mvp_lmd_with_binding(Item owner, Item key, Item value, int64_t operation);
Item mvp_lmd_template_object(MvpLmdProgram* program, JsTemplateLiteralNode* literal, int64_t slot);
const JsTemplateObjectOps* mvp_lmd_template_object_ops();
Item mvp_lmd_function_set_name(Item function, Item key, int64_t prefix);
Item mvp_lmd_fail(int64_t kind, int64_t site, const char* detail = NULL);
Item mvp_lmd_execution_interrupt_status(void);
double mvp_lmd_string_to_number(String* string);
double mvp_lmd_parse_number(String* string, int64_t prefix);
Item mvp_lmd_number_to_string(double value);
Item mvp_lmd_number_to_radix_string(double value, double radix);
Item mvp_lmd_primitive_to_string(Item value, int64_t library_objects = 1);
Item mvp_lmd_primitive_to_number(Item value, int64_t explicit_number = 0);
void mvp_lmd_numeric_string_span(String* string, const char** bytes, size_t* length);
Item mvp_lmd_to_primitive(Item value, int64_t hint, int64_t library_objects = 1);
Item mvp_lmd_caught_value(MvpLmdLibraryState* state, Item error);
Item mvp_lmd_generator_park(Item value, int64_t kind);
Item mvp_lmd_iterator_get(MvpLmdProgram* program, Item source);
Item mvp_lmd_iterator_method(MvpLmdProgram* program, Item source);
Item mvp_lmd_sequence_iterator_create(MvpLmdProgram* program, Item source,
    int projection, bool ordered, bool set, bool string = false);
Item mvp_lmd_iterator_get_with_method(MvpLmdProgram* program, Item source, Item method);
Item mvp_lmd_iterator_step(Item record, Item value, int64_t count);
Item mvp_lmd_iterator_step_value(Item record);
Item mvp_lmd_iterator_close(Item record, Item completion);
Item mvp_lmd_iterator_close_mode(Item record, Item completion, int64_t asynchronous);
Item mvp_lmd_iterator_collect(MvpLmdProgram* program, Item target, Item source, Item mapper, Item receiver);
Item mvp_lmd_iterator_collect_with_method(MvpLmdProgram* program, Item target, Item source,
    Item mapper, Item receiver, Item method, int64_t array_length = -1, bool create_properties = false,
    Item acquired_record = ItemNull);
Item mvp_lmd_call_array(Item function, Item receiver, Item arguments, Item target);
int64_t mvp_lmd_truthy(Item value);
int64_t mvp_lmd_generator_resume_kind();
Item mvp_lmd_array_method_value(MvpLmdProgram* program, Item owner, Item name, int64_t callee = 0);
Item mvp_lmd_property_read(MvpLmdProgram* program, Item owner, Item name, int64_t callee);
Item mvp_lmd_bigint_binary(Item left, Item right, int64_t operation);
Item mvp_lmd_bigint_to_string(Item value, double radix);
double mvp_lmd_parse_integer(String* value, int64_t radix);
Item mvp_lmd_number_to_fixed(double value, double digits);
Item mvp_lmd_string_concat(Item left, Item right);
int64_t mvp_lmd_string_compare(Item left, Item right);
Item mvp_lmd_string_at(Item string, double index, int64_t mode);
Item mvp_lmd_string_range(Item string, double start, double end, int64_t slice);
int64_t mvp_lmd_string_search(Item string, Item needle, double start, int64_t prefix);
Item mvp_lmd_string_pad(Item string, double length, Item filler, int64_t end = 0);
Item mvp_lmd_string_split(Item string, Item separator, int64_t limit);
double mvp_lmd_number_pow(double base, double exponent);
int64_t mvp_lmd_string_key(String* string, int64_t typed = 0);
Item mvp_lmd_array_store(Item array, uint32_t index, Item value);
int64_t mvp_lmd_array_indices_use_own_storage(Item array);
bool mvp_lmd_shape_has_indexed_keys(TypeMap* shape);
Item mvp_lmd_array_new(int64_t length);
Item mvp_lmd_array_from_recipe(const JsStaticLiteralRecipe* recipe);
Item mvp_lmd_array_adopt_realm(Item value);
Item mvp_lmd_array_flatten(Item source, double depth, int64_t length = -1, Item* mapping = nullptr);
Item mvp_lmd_array_to_string(Item source, Item separator, int64_t library_objects = 1);
Item mvp_lmd_regexp_new(MvpLmdLibraryState* state, Item pattern, Item flags);
struct MvpLmdRegExp;
MvpLmdRegExp* mvp_lmd_regexp_record(Item value, MvpLmdLibraryState* state);
Item mvp_lmd_regexp_string_method(MvpLmdLibraryState* state, Item source, Item pattern,
    Item replacement, int64_t replace);
Item mvp_lmd_array_resize(Item owner, int64_t length);
Item mvp_lmd_array_edit(Item owner, Item* args, int64_t count, int64_t method);
Item mvp_lmd_array_visit(Item owner, Item* args, int64_t count, int64_t method);
Item mvp_lmd_for_in_keys(Item owner);
Item mvp_lmd_define_property(Item owner, Item name, Item descriptor);
Item mvp_lmd_define_properties(Item owner, Item descriptors);
Item mvp_lmd_get_own_descriptor(Item owner, Item name);
Item mvp_lmd_object_integrity(Item owner, int64_t operation);
Item mvp_lmd_define_accessor(Item owner, Item name, Item function, int64_t attributes);
enum MvpLmdPrivateOperation { LMD_PRIVATE_NAME, LMD_PRIVATE_GET, LMD_PRIVATE_SET,
    LMD_PRIVATE_HAS, LMD_PRIVATE_ADD, LMD_PRIVATE_METHOD };
extern "C" Item mvp_lmd_private_element(Item owner, Item key, Item value, int64_t operation);
Item mvp_lmd_accessor_call(Item receiver, ShapeEntry* field, Item value, int64_t operation);
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
Item mvp_lmd_object_copy(Item target, Item source, int64_t assign, Item excluded = ItemNull);
bool mvp_lmd_property_key_equal(Item left, Item right);
Item mvp_lmd_map_call(Item owner, Item method, Item key, Item value);
int64_t mvp_lmd_map_next(Item owner, int64_t cursor);
Item mvp_lmd_map_entry(Item owner, int64_t cursor, int64_t projection);
Item mvp_lmd_function_new(uint64_t code_id, MvpLmdProgram* program);
Item mvp_lmd_function_prepare(Item function);
Item mvp_lmd_class_new(MvpLmdClass* plan, Item base, Item constructor = ItemNull);
Item mvp_lmd_class_initialize_instance(Item constructor, Item receiver, int64_t static_methods = 0);
Item mvp_lmd_class_invoke(Item callee, Item receiver, Item* arguments, int64_t argc, Item new_target);
Item mvp_lmd_function_home(Item function, int64_t constructor);
Item mvp_lmd_class_super(Item function, int64_t constructor);
Item mvp_lmd_get_prototype(Item object);
Item mvp_lmd_set_prototype(Item object, Item prototype);
Item mvp_lmd_object_extensible(Item object, int64_t prevent);
Item mvp_lmd_constructor_result(Item value, Item receiver, int64_t derived);
Item mvp_lmd_instanceof(Item value, Item constructor, int64_t ordinary = 0);
Item mvp_lmd_throw(Item value, int64_t error_constructor);
Item mvp_lmd_property_with_receiver(Item owner, Item name, Item value, int64_t operation, Item receiver);
Item mvp_lmd_class_property(Item owner, Item name, Item value, int64_t operation);
Item mvp_lmd_class_property_fixed(Item owner, Item name, Item value, int64_t operation);
Item mvp_lmd_class_property_host(Item owner, Item name, Item value, int64_t operation);
}
