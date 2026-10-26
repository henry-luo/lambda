// mvp_lmd_internal.h — types shared by the MVP code generator
// (mvp_lmd_mir.cpp) and the MIR-free program/realm runtime
// (mvp_lmd_program.cpp). MIR names appear only as field types; a
// build without MIR (LAMBDA_NO_MIR) compiles the runtime half alone
// and never creates a program (vibe/Lambda_Design_Mobile.md MB22).
#pragma once

#include "mvp_lmd.h"
#include "mvp_lmd_runtime.h"
#include "mvp_lmd_async.h"
#include "mvp_lmd_dynamic.h"
#include "../js_transpiler.hpp"
#include "../js_source_module.hpp"
#include "../js_runtime.h"
#include "../js_host_hooks.h"
#include "../js_builtin_catalog.hpp"
#include "../js_well_known_names.h"
#include "../js_props.h"
#include "../js_runtime_state.hpp"
#include "../js_execution_internal.hpp"
#include "../js_event_loop.h"
#include "../js_interp.hpp"
#include "../js_interp_env.h"
#include "../js_typed_array.h"
#include "../../ts/ts_ast.hpp"
#include "../../ts/ts_runtime.h"
#include "../../dom/dom.h"
#include "../../jube/jube_registry.h"
#include "../../runtime/mir_emitter_shared.hpp"
#include "../../runtime/mir_policy.hpp"
#include "../../runtime/runtime-state.h"
#include "../../runtime/heap_api.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/lambda-error.h"
#include "../../runtime/context_capsule.h"
#include "../../runtime/activation.h"
#include "../../runtime/recovery_frame.h"
#include "../../input/input.hpp"
#include "../../core/lambda-decimal.hpp"
#include "../../mir/mir-gen.h"
#include "../../../lib/mem.h"
#include "../../../lib/utf.h"
#include "../../../lib/time_util.h"
#include "../../../lib/math_utils.h"
#include "../../../lib/strbuf.h"
#include "../../../lib/atomic.h"
#include "../../../lib/hashmap_typed.hpp"
#include <math.h>

enum LmdKind { K_UNDEFINED = 1, K_NULL = 2, K_BOOL = 4, K_NUMBER = 8,
    K_STRING = 16, K_ARRAY = 32, K_FUNCTION = 64, K_OBJECT = 128, K_MAP = 256,
    K_TYPED_ARRAY = 512, K_BIGINT = 1024, K_SYMBOL = 2048, K_ANY = 4095, K_BOXED = 4096 };
enum LmdIntrinsic { I_UNDEFINED = 1, I_NAN, I_INFINITY, I_MAP, I_OBJECT,
    I_MATH, I_INT32_ARRAY, I_UINT8_ARRAY, I_FLOAT64_ARRAY, I_ARRAY, I_STRING, I_ERROR,
    I_NUMBER, I_PARSE_INT, I_IS_NAN, I_PERFORMANCE, I_CONSOLE, I_PROCESS, I_REQUIRE, I_JSON, I_DATE, I_REGEXP, I_GLOBAL_THIS, I_SYMBOL, I_SET, I_REFLECT, I_PROXY,
    I_EVAL_ERROR, I_RANGE_ERROR, I_REFERENCE_ERROR, I_SYNTAX_ERROR, I_TYPE_ERROR, I_URI_ERROR, I_AGGREGATE_ERROR,
    I_BOOLEAN, I_BIGINT, I_FUNCTION, I_ARRAYBUFFER, I_DATAVIEW, I_TEST262, I_INT8_ARRAY, I_INT16_ARRAY, I_UINT16_ARRAY,
    I_UINT32_ARRAY, I_FLOAT32_ARRAY, I_UINT8_CLAMPED_ARRAY, I_BIGINT64_ARRAY, I_BIGUINT64_ARRAY, I_FLOAT16_ARRAY, I_PROMISE, I_EVAL, I_DECODE_URI, I_ENCODE_URI, I_DECODE_COMPONENT, I_ENCODE_COMPONENT, I_SET_TIMEOUT, I_CLEAR_TIMEOUT, I_SET_INTERVAL, I_CLEAR_INTERVAL, I_QUEUE_MICROTASK, I_WEAK_MAP, I_WEAK_SET, I_SHARED_ARRAYBUFFER, I_ESCAPE, I_UNESCAPE, I_WEAK_REF, I_FINALIZATION_REGISTRY, I_ATOMICS, I_COUNT };
struct LmdRange { double lower = 0, upper = 0; unsigned state = 0; };
// lane bits follow typed_lanes; ITEMS admits only proven unchanged numeric literals.
enum { LMD_ARRAY_ITEMS = 8, LMD_ARRAY_UNKNOWN = 16 };
struct LmdArrayFacts { unsigned lanes = 0; LmdRange length; LmdRange elements; };
struct LmdFunction;
struct LmdObjectPlan;
struct LmdShapeSet { int count; LmdObjectPlan* plans[4]; };
struct LmdValue;
struct LmdScalarField;
struct LmdInlineFrame;
struct LmdInlineBody;
struct LmdNumericRegion;
struct LmdDenseArrayLoopPlan;
struct LmdWithLookup;
struct LmdRegionRead { LmdNumericRegion* region; int parent; String* name;
    unsigned kind; int index; LmdFunction* target; LmdShapeSet shapes; };
struct LmdNumericRegion {
    LmdFunction* function; int parameters; unsigned kinds[8];
    int count; LmdRegionRead reads[16]; int nodes; bool calls; bool allocates; bool computes; bool loops;
    int sites; AstNodeId site_ids[128]; int site_reads[128];
};
struct LmdBinding {
    NameEntry* entry;
    NameEntry* satellite_entry;
    LmdFunction* owner;
    LmdFunction* target;
    AstNode* initializer;
    uint32_t slot;
    unsigned kinds;
    unsigned seed_kinds;
    LmdRange range;
    AstNode* update;
    unsigned writes;
    unsigned range_changes;
    bool assigned;
    bool observed;
    bool private_name;
    bool external;
    bool native;
    bool integer;
    uint8_t intrinsic;
    bool dominated;
    MIR_reg_t reg;
    int scalar_home;
    LmdObjectPlan* shape_hint;
    LmdShapeSet shapes;
    LmdArrayFacts array;
    unsigned array_changes;
    bool array_observed;
    MIR_reg_t number_present;
    LmdRange present_range;
    unsigned present_range_changes;
    uint8_t integral_indices; // compiler proof: 0 unexamined, 1 unknown, 2 integral or nonfinite
    bool reflected_global;
    bool fixed_global;
    bool source_assigned;
    bool source_observed;
    bool closed_global;
    unsigned source_kinds;
    uint8_t reflected_intrinsic;
    bool class_name;
    bool captured;
    bool implicit_write;
    bool dense_array_storage;
    bool module_cell;
    bool ast_capture;
    const JsResolvedModuleBinding* module_import;
    LmdWithLookup* with_lookup;
    LmdBinding* fallback;
};
struct LmdWithLookup { LmdBinding* object; LmdWithLookup* outer; };
struct LmdPrivateBindingKey { AstNode* owner; String* name; LmdBinding* binding; };
using LmdPrivateBindings = TypedHashMap<LmdPrivateBindingKey,
    HashMapIdentity2MemberKeyOps<LmdPrivateBindingKey, &LmdPrivateBindingKey::owner, &LmdPrivateBindingKey::name>>;
struct LmdFunction {
    AstFuncNode* ast;
    MIR_item_t item;
    MIR_item_t forward;
    char name[40];
    uint32_t id;
    // pack analysis flags into alignment gaps to preserve ordinary function allocation size.
    bool native;
    bool integer;
    bool integer_region;
    bool scalar_factory;
    void* address;
    bool closed_calls;
    bool direct_args;
    bool reflected_entry;
    bool uses_process;
    LmdObjectPlan* shape_hint;
    LmdShapeSet shapes;
    unsigned returns;
    int arity;
    LmdRange range;
    unsigned range_changes;
    FnReturnAnalysis return_abi;
    MirImportEntry entry;
    MIR_var_t signature[LAMBDA_MAX_FUNCTION_ARGS];
    LmdArrayFacts array;
    unsigned array_changes;
    MvpLmdClass* home;
    LmdFunction* parent;
    ArrayList* captures;
    bool lexical_receiver;
    bool receiver_captured;
    bool observes_receiver;
    bool owns_captures;
    bool complex_params;
    bool parameter_expressions;
    bool duplicate_params;
    uint8_t formal_length;
    uint8_t home_slot;
    bool non_constructible;
    bool constructor;
    bool static_method;
    LmdBinding* arguments_binding;
    LmdBinding* body_arguments_binding;
    bool mapped_arguments;
    bool object_home;
    bool lexical_super;
    bool direct_eval;
    bool eval_environment;
    bool eval_lookup;
    bool eval_capture;
    LmdNumericRegion* numeric_region;
};
struct LmdObjectPlan { AstNode* literal; TypeMap* blueprint; TypeMap* shape;
    LmdBinding* scalar_binding; LmdScalarField* scalar_fields;
    LmdFunction* constructor; AstNode* initializers[4];
    LmdFunction* prototype_owner; };
struct LmdScalarFactoryPlan { LmdObjectPlan object; LmdFunction* function; AstNode* value; };
struct LmdDeletedShapePlan { LmdObjectPlan object; LmdObjectPlan* source; ShapeEntry* removed; };
struct MvpLmdRealm;
struct MvpLmdCode {
    atomic_int32 references;
    MIR_context_t mir;
    StrBuf* image;
    ArrayList* addresses;
    size_t relocation_count;
    unsigned int optimize_level;
    char* source;
    size_t source_length;
    char* reference;
};
struct MvpLmdProgram {
    bool typescript;
    bool relocatable;
    ArrayList* relocations;
    MvpLmdCode* code;
    const MvpLmdCode* cached_code;
    MvpLmdRealm* module_controller;
    JsTranspiler* frontend;
    MvpLmdProgram* realm_program;
    JsSourceModuleRecord* source_module;
    void* module_state;
    uint32_t module_state_id;
    uint32_t module_slot_count;
    uint32_t module_eval_slot;
    uint32_t compilation_stage;
    bool module_realm;
    bool script_published;
    bool script_bindings_pending;
    bool slots_registered;
    MIR_context_t mir;
    MIR_module_t module;
    ArrayList* functions;
    ArrayList* bindings;
    HashMap* private_bindings;
    Item* slots;
    uint32_t slot_count;
    char diagnostic[192];
    MvpLmdFailurePhase failure_phase;
    bool linked;
    MirImportEntry entry;
    LmdBinding intrinsics[I_COUNT - 1];
    TypeNominal families[2];
    TypeMap* roots[2];
    ArrayList* objects;
    LmdBinding** intrinsic_uses;
    LmdBinding** with_objects;  // statement-owned cells; absent in units without object environments
    LmdBinding** with_bindings; // per-reference views, leaving lexical declaration identities intact
    HashMap* import_cache;
    unsigned* literal_kinds;
    Item* character_items;
    uint32_t* template_slots;    // AST site to one-based registered cache slot
    bool immutable_objects;
    bool property_callbacks;
    bool coercion_callbacks;
    bool bigints;
    bool symbols;
    bool iterators;
    bool array_holes;
    bool excludes_sparse_arrays;
    bool array_named_properties;
    bool array_method_overrides;
    bool map_method_overrides;
    bool primitive_method_overrides;
    bool math_observed;
    bool performance_observed;
    bool date_observed;
    ArrayList* classes;
    bool receiver_abi;
    bool function_construction; // source evidence gates optional own-read specialization
    bool function_prototype_observed;
    bool deletes_properties;
    bool host_enabled;
    bool native_host_values;
    bool closed_stdio;
    bool direct_stdio;
    bool host_active;
    bool timers;
    uint32_t timer_slot;
    uint32_t host_slot;
    uint32_t global_slot;
    uint64_t random_state;
    bool global_receiver;
    bool global_reflection;
    bool global_bindings_exposed;
    bool global_mutations_closed;
    bool global_math_observed;
    bool global_primitive_method_overrides;
    MvpLmdHostState host;
    MvpLmdLibraryState* library;
    bool library_active;
    bool library_runtime;
    uint32_t library_slot;
    uint32_t iterator_family_slot;
    uint32_t regexp_iterator_slot;
    uint32_t iterator_base_slot;
    bool errors;
    uint32_t error_slot;
    bool primitives;
    uint32_t primitive_slot;
    bool weak_collections;
    bool weak_lifetimes;
    uint32_t weak_slot;
    bool buffers;
    bool shared_buffer;
    uint32_t shared_buffer_slot;
    bool test262_host;
    uint32_t buffer_slot;
    bool typed_storage;
    bool typed_explicit;
    unsigned typed_named_kinds;
    LmdRange typed_named_range;
    unsigned typed_named_range_changes;
    uint32_t typed_slot;
    bool promises;
    uint32_t promise_slot;
    bool async_generators;
    uint32_t async_generator_slot;
    bool async_functions;
    uint32_t async_function_slot;
    bool generator_functions;
    uint32_t generator_function_slot;
    bool suspended_constructor_observed;
    bool dynamic_source;
    bool global_alias_store;
    uint8_t* eval_references; // absent outside units with syntactic direct eval
    uint32_t eval_slot;
    ArrayList* dynamic_scripts; // retained AST/code records outlive precise function finalizers
    bool atomics;
    uint32_t atomics_slot;
    MvpLmdExecution* execution_owner;
    JsScript* satellite_script; // caller-owned module bindings; no second Script evaluation
    JsScript* satellite_source;
    AstFunctionId satellite_function_id; // immutable identity, never a closure instance
    uint32_t realm_key;
};
struct MvpLmdRealm;
struct MvpLmdExecution {
    MvpLmdProgram program;
    MvpLmdProgram* last_program;
    EvalContext* owner_context;
    Runtime* host_runtime;
    Runtime* owned_runtime; // standalone clients use the same canonical owner and teardown
    bool shared_realm;
    MvpLmdHost retained_host;
    Item result;
    const MvpLmdHost* host;
    MvpLmdRealm* realm;
    ArrayList* units;
    uint32_t next_realm_key;
    mutable char result_diagnostic[512]; // reporting text is separate from the guest Error.message
};
struct MvpLmdRealm {
    Runtime runtime;                  // controller for the existing EvalContext; owns no heap
    Runtime* active_runtime;
    MvpLmdProgram* program;
    MvpLmdExecution* execution;
    const MvpLmdHost* host;
};

// --- shared between mvp_lmd_program.cpp and mvp_lmd_mir.cpp ---
inline const char* const intrinsic_names[] = {"undefined", "NaN", "Infinity", "Map", "Object",
    "Math", "Int32Array", "Uint8Array", "Float64Array", "Array", "String", "Error",
    "Number", "parseInt", "isNaN", "performance", "console", "process", "require", "JSON", "Date", "RegExp", "globalThis", "Symbol", "Set", "Reflect", "Proxy",
    "EvalError", "RangeError", "ReferenceError", "SyntaxError", "TypeError", "URIError", "AggregateError", "Boolean", "BigInt", "Function", "ArrayBuffer", "DataView", "$262", "Int8Array", "Int16Array", "Uint16Array",
    "Uint32Array", "Float32Array", "Uint8ClampedArray", "BigInt64Array", "BigUint64Array", "Float16Array", "Promise", "eval", "decodeURI", "encodeURI", "decodeURIComponent", "encodeURIComponent", "setTimeout", "clearTimeout", "setInterval", "clearInterval", "queueMicrotask", "WeakMap", "WeakSet", "SharedArrayBuffer", "escape", "unescape", "WeakRef", "FinalizationRegistry", "Atomics"};
inline const int typed_intrinsics[] = {I_INT8_ARRAY, I_UINT8_ARRAY, I_INT16_ARRAY, I_UINT16_ARRAY,
    I_INT32_ARRAY, I_UINT32_ARRAY, I_FLOAT32_ARRAY, I_FLOAT64_ARRAY, I_UINT8_CLAMPED_ARRAY,
    I_BIGINT64_ARRAY, I_BIGUINT64_ARRAY, I_FLOAT16_ARRAY};
inline int typed_intrinsic(int id) {
    for (int type = 0; type < 12; type++) if (typed_intrinsics[type] == id) return type;
    return -1;
}
// realm-library ambient selector; activations save and restore it
extern __thread MvpLmdLibraryState** mvp_lmd_library_slot;
extern __thread Context* mvp_lmd_library_context;
MvpLmdProgram* mvp_lmd_new_unit(MvpLmdExecution* execution, MvpLmdProgram* realm);
bool mvp_lmd_intrinsic_enabled(MvpLmdProgram* p, int i);
Item mvp_lmd_intrinsic_value(MvpLmdProgram* p, int i);
Item mvp_lmd_function_materialize(Item owner);
