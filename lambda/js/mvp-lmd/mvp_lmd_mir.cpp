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
static const char* intrinsic_names[] = {"undefined", "NaN", "Infinity", "Map", "Object",
    "Math", "Int32Array", "Uint8Array", "Float64Array", "Array", "String", "Error",
    "Number", "parseInt", "isNaN", "performance", "console", "process", "require", "JSON", "Date", "RegExp", "globalThis", "Symbol", "Set", "Reflect", "Proxy",
    "EvalError", "RangeError", "ReferenceError", "SyntaxError", "TypeError", "URIError", "AggregateError", "Boolean", "BigInt", "Function", "ArrayBuffer", "DataView", "$262", "Int8Array", "Int16Array", "Uint16Array",
    "Uint32Array", "Float32Array", "Uint8ClampedArray", "BigInt64Array", "BigUint64Array", "Float16Array", "Promise", "eval", "decodeURI", "encodeURI", "decodeURIComponent", "encodeURIComponent", "setTimeout", "clearTimeout", "setInterval", "clearInterval", "queueMicrotask", "WeakMap", "WeakSet", "SharedArrayBuffer", "escape", "unescape", "WeakRef", "FinalizationRegistry", "Atomics"};
// MIR needs ABI offsets for inherited callable and nominal class records, as in the Lambda emitter.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
static constexpr size_t LMD_CALLABLE_HOME_OFFSET = offsetof(MvpLmdCallable, home);
static constexpr size_t LMD_CLASS_VALUES_OFFSET = offsetof(MvpLmdClass, values);
static constexpr size_t LMD_CLASS_FIELD_KEYS_OFFSET = offsetof(MvpLmdClass, field_keys);
static constexpr size_t LMD_CLASS_INITIALIZERS_OFFSET = offsetof(MvpLmdClass, initializers);
#pragma clang diagnostic pop
static const ArrayNumElemType typed_lanes[] = {ELEM_INT32, ELEM_UINT8, ELEM_FLOAT64};
static const int typed_intrinsics[] = {I_INT8_ARRAY, I_UINT8_ARRAY, I_INT16_ARRAY, I_UINT16_ARRAY,
    I_INT32_ARRAY, I_UINT32_ARRAY, I_FLOAT32_ARRAY, I_FLOAT64_ARRAY, I_UINT8_CLAMPED_ARRAY,
    I_BIGINT64_ARRAY, I_BIGUINT64_ARRAY, I_FLOAT16_ARRAY};
static int typed_intrinsic(int id) {
    for (int type = 0; type < 12; type++) if (typed_intrinsics[type] == id) return type;
    return -1;
}
static const struct { const char* name; const char* native; int arity;
    double (*unary)(double); double (*binary)(double, double); bool binary_once = false; } math_operations[] = {
    {"sqrt", "sqrt", 1, sqrt}, {"sin", "sin", 1, sin}, {"floor", "floor", 1, floor}, {"trunc", "trunc", 1, trunc},
    {"abs", "fn_abs_f", 1, fn_abs_f}, {"min", "fn_min2_u", 2, NULL, fn_min2_u}, {"max", "fn_max2_u", 2, NULL, fn_max2_u},
    {"ceil", "ceil", 1, ceil}, {"cos", "cos", 1, cos}, {"round", "floor", 1, floor}, {"random", "math_splitmix64", 0},
    {"pow", "mvp_lmd_number_pow", 2, NULL, mvp_lmd_number_pow, true}};
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
    char* source;
    size_t source_length;
    char* reference;
};
void mvp_lmd_code_retain(MvpLmdCode* code) {
    if (code) atomic_inc32(&code->references);
}
void mvp_lmd_code_release(MvpLmdCode* code) {
    if (!code || atomic_dec32(&code->references)) return;
    if (code->mir) { MIR_gen_finish(code->mir); MIR_finish(code->mir); }
    strbuf_free(code->image); arraylist_free(code->addresses);
    mem_free(code->source); mem_free(code->reference); mem_free(code);
}
size_t mvp_lmd_code_bytes(const MvpLmdCode* code) {
    return code ? sizeof(*code) + code->image->length +
        code->addresses->length * sizeof(void*) + code->source_length +
        (code->reference ? strlen(code->reference) + 1 : 0) : 0;
}
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
static MvpLmdProgram* mvp_lmd_new_unit(MvpLmdExecution* execution, MvpLmdProgram* realm) {
    if (!execution->units) execution->units = arraylist_new(8);
    if (!execution->units) return NULL;
    auto* program = (MvpLmdProgram*)mem_calloc(1, sizeof(MvpLmdProgram), MEM_CAT_EVAL);
    if (!program) return NULL;
    if (!arraylist_append(execution->units, program)) { mem_free(program); return NULL; }
    program->execution_owner = execution;
    program->realm_program = realm;
    if (realm) {
        program->library = realm->library;
        program->host_enabled = realm->host_enabled;
        program->test262_host = realm->test262_host;
    }
    return program;
}
void mvp_lmd_runtime_reset() {
    if (!context || !js_active_runtime_state) return;
    Runtime* runtime = context->runtime;
    for (int i = 0; runtime && runtime->scripts && i < runtime->scripts->length; i++) {
        js_script_reset_heap(js_script_from_script((Script*)runtime->scripts->data[i]));
    }
    // queued work is already retired by js_batch_reset. Drop heap-derived
    // owners before replacement; Script extensions retain compiler metadata.
    js_runtime_state.formal_mir_owner = NULL;
    js_runtime_state.auto_mir_owner = NULL;
    js_runtime_state.promises.host_ops = NULL;
    js_runtime_state.promises.host_context = NULL;
    js_runtime_state.modules.source_ops = NULL;
    js_runtime_state.modules.source_owner = NULL;
    context_capsule_extensions_drop_owner(context, MVP_LMD_MODULE_OWNER);
    context_capsule_extensions_drop_owner(context, MVP_LMD_DYNAMIC_OWNER);
    context_capsule_extensions_drop_owner(context, MVP_LMD_REGEXP_OWNER);
    context_capsule_extensions_drop_owner(context, MVP_LMD_CAPSULE_OWNER);
}
MvpLmdProgram* mvp_lmd_program_realm(MvpLmdProgram* program) {
    return program && program->realm_program ? program->realm_program : program;
}
MvpLmdProgram* mvp_lmd_program_for_global(MvpLmdProgram* program, Item global) {
    MvpLmdExecution* execution = program ? program->execution_owner : NULL;
    if (!execution || get_type_id(global) != LMD_TYPE_MAP) return NULL;
    auto matches = [&](MvpLmdProgram* candidate) {
        return !candidate->realm_program && candidate->global_receiver && candidate->slots &&
            candidate->slots[candidate->global_slot].item == global.item;
    };
    if (matches(&execution->program)) return &execution->program;
    for (int i = 0; execution->units && i < execution->units->length; i++) {
        auto* candidate = (MvpLmdProgram*)execution->units->data[i];
        if (matches(candidate)) return candidate;
    }
    // prepared native globals may predate the formal realm. They retain the
    // caller's bridge policy when no MVP owner exists; no new realm is created.
    return NULL;
}
uint32_t mvp_lmd_program_realm_key(MvpLmdProgram* program) {
    return program ? mvp_lmd_program_realm(program)->realm_key : 0;
}
extern "C" const JsTemplateObjectOps* mvp_lmd_template_object_ops() {
    static const JsTemplateObjectOps ops = {
        [](int count) { return mvp_lmd_array_new(count); },
        [](Item array, int index, Item value) { return mvp_lmd_array_store(array, index, value); },
        [](Item object, Item raw) { return mvp_lmd_named_set(object, "raw", raw,
            JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE | JSPD_NON_CONFIGURABLE); },
        [](Item object) { return mvp_lmd_object_integrity(object, 1); }
    };
    return &ops;
}
extern "C" Item mvp_lmd_template_object(MvpLmdProgram* program, JsTemplateLiteralNode* literal, int64_t slot) {
    if (program->slots[slot].item) return program->slots[slot];
    int count = ast_linked_node_count(literal->quasis);
    RootSpan values(count * 2);
    if (!values.valid()) return ItemError;
    Item* cooked = values.items(); Item* raw = cooked + count;
    int index = 0;
    for (AstNode* node = literal->quasis; node; node = node->next, index++) {
        JsTemplateElementNode* element = (JsTemplateElementNode*)node;
        cooked[index].item = element->cooked ? s2it(element->cooked) : ITEM_JS_UNDEFINED;
        raw[index].item = s2it(element->raw);
    }
    Item object = js_build_template_object_with_ops(cooked, raw, count, mvp_lmd_template_object_ops());
    // publish only a complete frozen object; the execution owner traces this site cache.
    if (!item_is_error(object)) program->slots[slot] = object;
    return object;
}
static Item math_call(Context*, MvpLmdProgram* program, Item* args, uint64_t count, Item self, Item, Item) {
    const JsBuiltinMethodSpec* operation = (const JsBuiltinMethodSpec*)((MvpLmdNativeCallable*)self.function)->state;
    if (operation->builtin_id == JS_BUILTIN_MATH_RANDOM)
        return push_d(math_splitmix64(&program->random_state));
    return js_math_intrinsic_with_number((JsBuiltinId)operation->builtin_id, args, count,
        [](Item value) { return mvp_lmd_primitive_to_number(value); });
}
struct LmdControl {
    LmdControl* parent;
    MIR_label_t stop;
    MIR_label_t next;
    const char* label;
    int label_len;
    AstNode* loop_target;
    MIR_reg_t iteration_owner = 0;
    MIR_reg_t iteration_active = 0;
    MIR_reg_t iterator_record = 0;
    bool async_iterator = false;
    MIR_label_t error_label = NULL;
    MIR_reg_t error = 0;
};
struct LmdCompletion {
    LmdCompletion* parent;
    LmdControl* control;
    AstNode* finalizer;
    MIR_label_t handler;
    MIR_reg_t error;
    MIR_reg_t eval_env = 0;
    NameScope* eval_scope = NULL;
};
struct LmdNumberFacts {
    LmdNumberFacts* parent;
    LmdBinding* bindings[2];
    LmdRange ranges[2];
};
struct LmdCompiler {
    MvpLmdProgram* program;
    LmdFunction* function;
    MirEmitter em;
    MIR_reg_t unit;
    MIR_reg_t caller_file;
    size_t caller_file_offset;
    MIR_reg_t arguments;
    MIR_reg_t argc;
    MIR_reg_t self;
    MIR_reg_t argument_span;
    MIR_insn_t argument_fixup;
    int argument_count;
    unsigned allocating_inline_nodes;
    unsigned method_inline_nodes;
    MIR_label_t optional_exit;
    LmdControl* control;
    LmdCompletion* completion;
    LmdNumberFacts* number_facts;
    bool strict;
    LmdInlineFrame* inlining;
    MIR_label_t integer_bail;
    MIR_reg_t receiver;
    MIR_reg_t new_target;
    MIR_reg_t receiver_cell;
    MvpLmdClass* initializer_class;
    LmdBinding* dense_arrays[8];
    int dense_array_count;
    LmdDenseArrayLoopPlan* dense_loop;
    MIR_reg_t eval_env;
    NameScope* eval_scope;
    bool parameter_initializer;
};
struct LmdValue { MIR_reg_t reg; unsigned kind; bool literal = false; double number = 0; bool integer = false; LmdRange range;
    MIR_reg_t number_present = 0; };
struct LmdDenseArrayLoopPlan {
    LmdCompiler* compiler; AstLoopControlNode* node; LmdBinding* arrays[8];
    int count; int reads; int nodes; bool allowed; MIR_label_t bail;
    int element_writes; bool nested;
    int writes; LmdBinding* written[32]; LmdValue snapshots[32];
};
struct LmdScalarField { LmdValue value; unsigned kinds; bool written; bool integer;
    LmdRange range; int home; AstNode* initializer; };
struct LmdInlineFrame { LmdInlineFrame* parent; LmdFunction* function; int count;
    LmdBinding* bindings[32]; LmdScalarField slots[32];
    LmdObjectPlan* plans[32]; bool guarded[32]; MIR_reg_t receiver;
    MIR_label_t exit; MIR_reg_t result; LmdObjectPlan* scalar_receiver;
    MIR_reg_t self; MIR_reg_t receiver_cell;
    LmdNumericRegion* region; LmdValue region_values[16]; LmdObjectPlan* scalar_return;
    unsigned result_kind;
    LmdBinding* callback_binding; LmdFunction* callback;
    const LmdInlineBody* callback_body; MIR_reg_t callback_receiver_cell; };
struct LmdReference { LmdBinding* binding; LmdValue owner; MIR_reg_t key;
    bool key_known = false; int64_t index = 0; MIR_reg_t name = 0; LmdObjectPlan* plan = NULL; ShapeEntry* field = NULL; int method = 0; LmdShapeSet shapes = {}; String* spelling = NULL; bool guarded = false;
    unsigned lanes = 0; bool numeric_key = false; bool in_bounds = false; LmdRange elements;
    MIR_reg_t key_present = 0; String* string_literal = NULL; LmdValue deferred_name = {}; MIR_reg_t receiver = 0;
    bool dense_storage = false; bool uncoerced_name = false; MIR_reg_t with_object = 0;
    bool invalid_target = false; bool private_element = false;
    MIR_reg_t eval_reference = 0; MIR_reg_t global_presence = 0; };
enum { MVP_LMD_COMPACT_REGISTER_BUDGET = 4096 };
struct LmdPrivateNameInit { NameRef spelling; uint32_t slot; };

static LmdBinding* binding(MvpLmdProgram* p, NameEntry* entry) {
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        if (b->entry == entry) return b;
    }
    return NULL;
}
static LmdBinding* identifier_binding(MvpLmdProgram* p, AstIdentNode* id) {
    if (p->with_bindings) {
        AstNodeId index = ast_index_find(&p->frontend->ast_index, id);
        if (index < p->frontend->ast_index.count && p->with_bindings[index]) return p->with_bindings[index];
    }
    if (id->entry) return binding(p, id->entry);
    AstNodeId index = ast_index_find(&p->frontend->ast_index, id);
    return index < p->frontend->ast_index.count ? p->intrinsic_uses[index] : NULL;
}
static LmdRegionRead* numeric_region_read(MvpLmdProgram* p, LmdNumericRegion* region, AstNode* site) {
    if (!region || !site) return NULL;
    AstNodeId id = ast_index_find(&p->frontend->ast_index, site);
    for (int i = 0; i < region->sites; i++)
        if (region->site_ids[i] == id) return &region->reads[region->site_reads[i]];
    return NULL;
}
static LmdFunction* function(MvpLmdProgram* p, AstNode* ast) {
    for (int i = 1; i < p->functions->length; i++) {
        LmdFunction* f = (LmdFunction*)p->functions->data[i];
        if ((AstNode*)f->ast == ast) return f;
    }
    return NULL;
}
static void diagnostic(MvpLmdProgram* p, AstNode* n, const char* reason) {
    if (!p->diagnostic[0]) snprintf(p->diagnostic, sizeof(p->diagnostic),
        "MVP scope error at byte %u: %s", n ? n->source_span.start_byte : 0, reason);
}
static bool named(String* s, const char* name) {
    return s && s->len == strlen(name) && !memcmp(s->chars, name, s->len);
}
static String* property_spelling(AstNode* key, bool computed) {
    if (!computed && key->node_type == AST_NODE_IDENT) return ((AstIdentNode*)key)->name;
    return key->node_type == AST_NODE_LITERAL && ((AstLiteralNode*)key)->literal_type == AST_LITERAL_STRING
        ? ((AstLiteralNode*)key)->value.string_value : NULL;
}
static String* member_spelling(AstFieldNode* member) {
    return property_spelling(member->property, member->computed);
}
static int math_operation(MvpLmdProgram* p, AstCallNode* call) {
    if (p->math_observed) return -1;
    if (call->callee->node_type != AST_NODE_MEMBER_EXPR && call->callee->node_type != AST_NODE_INDEX_EXPR)
        return -1;
    AstFieldNode* member = (AstFieldNode*)call->callee;
    LmdBinding* owner = member->object->node_type == AST_NODE_IDENT
        ? identifier_binding(p, (AstIdentNode*)member->object) : NULL;
    if (!owner || owner->intrinsic != I_MATH) return -1;
    String* key = member_spelling(member);
    for (int i = 0; i < (int)(sizeof(math_operations) / sizeof(math_operations[0])); i++)
        if (named(key, math_operations[i].name)) return i;
    return -1;
}
static void mvp_lmd_nominal_initialize(MvpLmdClass* cls, String* name, MvpLmdProgram* p) {
    cls->program = p;
    cls->nominal.type_name = {name->chars, name->len};
    cls->nominal.struct_kind = LMD_TYPE_MAP;
    cls->nominal.extension = &mvp_lmd_class_extension; cls->nominal.extension_data = cls;
    cls->prototype_nominal = cls->nominal;
    TypeMap* shapes[] = {&cls->shape, &cls->prototype_shape, &cls->static_shape};
    for (int i = 0; i < 3; i++) {
        shapes[i]->type_id = LMD_TYPE_MAP; shapes[i]->type_index = -1;
        shapes[i]->is_nominal = true;
        shapes[i]->nominal = i ? &cls->prototype_nominal : &cls->nominal;
    }
}
Item mvp_lmd_object_create(MvpLmdProgram* p, Item prototype) {
    TypeId type = get_type_id(prototype);
    if (!mvp_lmd_is_object(prototype) && type != LMD_TYPE_NULL)
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> parent(roots, prototype), object(roots, mvp_lmd_object_new(p->roots[0], 0));
    if (item_is_error(object.get())) return object.get();
    Item result = mvp_lmd_set_prototype(object.get(), parent.get());
    return item_is_error(result) ? result : object.get();
}

static bool mvp_lmd_class_plan_elements(MvpLmdClass* cls) {
    AstClassNode* ast = cls->ast;
    for (AstNode* node = ((AstBlockNode*)ast->body)->statements; node; node = node->next) {
        if (node->node_type == AST_NODE_FIELD) {
            cls->field_count++; cls->dynamic = true;
            if (!((AstClassFieldNode*)node)->is_static) cls->initializer_count++;
        } else if (node->node_type == JS_AST_NODE_STATIC_BLOCK) cls->dynamic = true;
        else if (node->node_type == AST_NODE_METHOD && (((AstMethodNode*)node)->computed ||
                ((AstMethodNode*)node)->key->node_type != AST_NODE_IDENT ||
                (((AstMethodNode*)node)->static_method &&
                 (named(property_spelling(((AstMethodNode*)node)->key, false), "name") ||
                  named(property_spelling(((AstMethodNode*)node)->key, false), "length"))))) cls->dynamic = true;
    }
    for (AstNode* node = ((AstBlockNode*)ast->body)->statements; node; node = node->next) {
        bool computed = false, statics = false;
        AstNode* key = js_ast_class_element_key(node, &computed, &statics);
        if (!computed && key && key->node_type == AST_NODE_IDENT) {
            String* name = ((AstIdentNode*)key)->name;
            if (name && name->len > 1 && name->chars[0] == '#') {
                cls->has_private_names = cls->dynamic = true;
                if (node->node_type == AST_NODE_METHOD) cls->private_method_mask |= statics ? 2 : 1;
            }
        }
    }
    cls->has_instance_elements = cls->initializer_count || (cls->private_method_mask & 1);
    if (ast->superclass && ast->superclass->node_type != AST_NODE_IDENT) cls->dynamic = true;
    return (uint64_t)cls->field_count + cls->initializer_count + 3 +
        (cls->private_method_mask ? 2 : 0) <= INT32_MAX - UINT8_MAX;
}

static MvpLmdClass* class_plan(MvpLmdProgram* p, AstClassNode* ast, LmdBinding* name = NULL) {
    if (name) {
        if (!name->class_name || name->assigned || !name->entry->node ||
                name->entry->node->node_type != AST_NODE_CLASS) return NULL;
        ast = (AstClassNode*)name->entry->node;
    }
    if (!ast) return NULL;
    for (int i = 0; i < p->classes->length; i++) {
        MvpLmdClass* cls = (MvpLmdClass*)p->classes->data[i];
        if (cls->ast == ast) return cls;
    }
    MvpLmdClass* cls = (MvpLmdClass*)pool_calloc(p->frontend->pool, sizeof(MvpLmdClass));
    if (!cls) return NULL;
    cls->ast = ast; cls->program = p; cls->constructor_id = -1;
    AstNode* parent = ast_index_parent(&p->frontend->ast_index, (AstNode*)ast);
    cls->dynamic = ast->node_type == AST_NODE_CLASS_EXPR || !parent || parent->node_type != AST_SCRIPT;
    cls->slot = p->slot_count; p->slot_count += 3;
    cls->initializer_slot = p->slot_count;
    if (!mvp_lmd_class_plan_elements(cls)) {
        diagnostic(p, (AstNode*)ast, "class environment exceeds closure layout");
        return NULL;
    }
    p->slot_count += cls->initializer_count;
    mvp_lmd_nominal_initialize(cls, ast->name ? ast->name : name_pool_create_len(p->frontend->name_pool, "", 0), p);
    arraylist_append(p->classes, cls);
    return cls;
}
static NameEntry* satellite_capture(MvpLmdProgram* p, String* name) {
    if (!p->satellite_source) return NULL;
    AstIndex* index = &p->satellite_source->ast_index;
    uint32_t count = 0;
    const AstFunctionReference* references = ast_index_function_references(index, p->satellite_function_id, &count);
    for (uint32_t i = 0; i < count; i++) {
        NameEntry* entry = index->bindings[references[i].binding_id];
        if (entry->name->len == name->len && !memcmp(entry->name->chars, name->chars, name->len) &&
                js_interp_p2_is_body_capture(p->satellite_source, p->satellite_function_id, entry)) return entry;
    }
    return NULL;
}

static void collect_scope(MvpLmdProgram* p, LmdFunction* owner, NameScope* scope) {
    // a module satellite retains private bindings, including shadowed globals.
    bool script_globals = !p->source_module &&
        !(p->satellite_script && p->satellite_script->is_module) && !owner->id &&
        scope == ((AstScript*)p->frontend->ast_root)->global_vars;
    for (NameEntry* e = scope ? scope->first : NULL; e; e = e->next) {
        if (binding(p, e)) continue;
        LmdBinding* b = (LmdBinding*)mem_calloc(1, sizeof(LmdBinding), MEM_CAT_TEMP);
        b->entry = e;
        b->owner = owner;
        if (script_globals) {
            for (int i = 0; i < 3; i++) if (named(e->name, intrinsic_names[i])) {
                if (e->is_lexical || (e->node && e->node->node_type == AST_NODE_FUNC))
                    diagnostic(p, e->node, "SyntaxError: restricted global declaration");
                b->intrinsic = (uint8_t)i + 1;
                b->kinds = i ? K_NUMBER : K_UNDEFINED;
            }
        }
        if (script_globals && !e->is_lexical)
            for (int i = I_MAP - 1; i < I_COUNT - 1; i++) if (named(e->name, intrinsic_names[i]))
                p->global_receiver = p->global_reflection = p->receiver_abi = true;
        b->module_cell = p->source_module && !owner->id && scope == ((AstScript*)p->frontend->ast_root)->global_vars;
        if (!owner->id && !b->intrinsic) b->slot = b->module_cell ? p->module_slot_count++ : p->slot_count++;
        if (b->module_cell) {
            b->external = b->assigned = b->observed = b->array_observed = true;
            b->kinds = K_ANY;
        }
        if (p->satellite_script && !owner->id && scope == ((AstScript*)p->frontend->ast_root)->global_vars) {
            b->satellite_entry = satellite_capture(p, e->name);
            b->ast_capture = b->satellite_entry != NULL;
            if (b->ast_capture) {
                b->external = b->assigned = b->observed = b->array_observed = true;
                b->kinds = K_ANY;
                e->is_const = b->satellite_entry->is_const;
            }
            for (NameEntry* original = b->ast_capture ? NULL : p->satellite_script->global_scope->first;
                    original; original = original->next) {
                if (original->name->len != e->name->len || memcmp(original->name->chars, e->name->chars, e->name->len)) continue;
                // the AST caller's module slab remains the live binding owner after promotion.
                b->module_cell = true; b->slot = original->slot;
                b->satellite_entry = original;
                b->external = b->assigned = b->observed = b->array_observed = true;
                b->kinds = K_ANY; e->is_const = original->is_const;
                b->target = NULL;
                break;
            }
        }
        if (e->is_parameter) b->kinds = K_ANY;
        arraylist_append(p->bindings, b);
    }
}
struct LmdWalk { MvpLmdProgram* p; LmdFunction* owner; AstNode* parent; bool facts; };
static void capture_binding(MvpLmdProgram* p, LmdFunction* f, LmdBinding* b, AstNode* site) {
    b->captured = true; b->external = true; b->observed = true;
    b->owner->owns_captures = true;
    // each intermediate closure retains the same cell, including otherwise unused captures.
    for (; f && f != b->owner; f = f->parent) {
        if (!f->captures) f->captures = arraylist_new(4);
        bool present = false;
        for (int i = 0; i < f->captures->length; i++) present |= f->captures->data[i] == b;
        if (!present) arraylist_append(f->captures, b);
        if (f->captures->length >= UINT8_MAX) diagnostic(p, site, "closure capture limit");
    }
}
static LmdBinding* private_binding(MvpLmdProgram* p, AstNode* site, String* name,
        LmdFunction* declaring_function = NULL) {
    if (!name || name->len < 2 || name->chars[0] != '#') return NULL;
    AstNode* child = NULL;
    for (AstNode* node = site; node; child = node, node = ast_index_parent(&p->frontend->ast_index, node)) {
        if (node->node_type != AST_NODE_CLASS && node->node_type != AST_NODE_CLASS_EXPR) continue;
        if (child && child == ((AstClassNode*)node)->superclass) continue;
        // one frontend name pool interns spellings; retain the declaring class in the identity.
        LmdPrivateBindingKey key = {node, name, NULL};
        if (const auto* found = LmdPrivateBindings::get(p->private_bindings, key)) return found->binding;
        if (!declaring_function) continue;
        if (!p->private_bindings) p->private_bindings = LmdPrivateBindings::create(8);
        if (!p->private_bindings) { diagnostic(p, site, "private binding index allocation"); return NULL; }
        NameEntry* entry = (NameEntry*)pool_calloc(p->frontend->pool, sizeof(NameEntry));
        LmdBinding* binding = (LmdBinding*)mem_calloc(1, sizeof(LmdBinding), MEM_CAT_TEMP);
        if (!entry || !binding) { mem_free(binding); diagnostic(p, site, "private binding allocation"); return NULL; }
        entry->name = name; entry->node = node; entry->is_lexical = entry->is_const = true;
        binding->entry = entry; binding->owner = declaring_function; binding->kinds = K_STRING;
        binding->private_name = binding->observed = true;
        if (!declaring_function->id) binding->slot = p->slot_count++;
        if (!arraylist_append(p->bindings, binding)) {
            mem_free(binding); diagnostic(p, site, "private binding list allocation"); return NULL;
        }
        key.binding = binding;
        LmdPrivateBindings::set(p->private_bindings, key);
        if (hashmap_oom(p->private_bindings)) { diagnostic(p, site, "private binding index insertion"); return NULL; }
        return binding;
    }
    return NULL;
}
static void walk(AstNode* n, LmdWalk* state);
static void child(AstNode* n, void* context) { walk(n, (LmdWalk*)context); }
static __thread MvpLmdLibraryState** mvp_lmd_library_slot;
static __thread Context* mvp_lmd_library_context;
__thread MvpLmdLibraryState* mvp_lmd_foreign_library;
const char* mvp_lmd_program_reference(MvpLmdProgram* program) {
    return program && program->frontend ? program->frontend->reference : NULL;
}
MvpLmdLibraryState* mvp_lmd_program_library(MvpLmdProgram* program) {
    if (program) return program->library_runtime ? program->library : NULL;
    // derived TLS points into the owning context capsule; teardown clears it.
    // an activation may restore an empty cache saved before another turn's first promotion.
    if (context != mvp_lmd_library_context || !mvp_lmd_library_slot) {
        mvp_lmd_library_context = context;
        mvp_lmd_library_slot = (MvpLmdLibraryState**)context_capsule_extension(context, MVP_LMD_CAPSULE_OWNER, 0);
        mvp_lmd_foreign_library = mvp_lmd_library_slot && *mvp_lmd_library_slot &&
            (*mvp_lmd_library_slot)->default_realm ? *mvp_lmd_library_slot : NULL;
    }
    return mvp_lmd_library_slot ? *mvp_lmd_library_slot : NULL;
}
NameRef mvp_lmd_internal_name(MvpLmdInternalName name) {
    static const char* names[] = {"MVP [[Prototype]]", "MVP [[Extensible]]",
        "MVP [[PrimitiveValue]]", "MVP [[DateValue]]", "MVP [[ErrorData]]",
        "MVP [[ParameterMap]]", "MVP [[RegExpSource]]", "MVP [[RegExpFlags]]",
        "RegExp iterator matcher", "RegExp iterator string", "RegExp iterator global",
        "RegExp iterator unicode", "RegExp iterator done", "MVP [[IteratorNext]]",
        "MVP [[CustomElementName]]", "URL record", "PerformanceObserver record",
        "PerformanceObserverEntryList record"};
    static_assert(sizeof(names) / sizeof(names[0]) == LMD_INTERNAL_NAME_COUNT);
    static const ContextCapsuleOps ops = {"js-mvp-internal-names", CONTEXT_CAPSULE_LIFETIME_CONTEXT,
        sizeof(NameRef) * LMD_INTERNAL_NAME_COUNT, NULL, NULL, NULL};
    auto* keys = (NameRef*)context_capsule_extension(context, MVP_LMD_CAPSULE_OWNER, 1);
    if (!keys) {
        keys = (NameRef*)mem_calloc(LMD_INTERNAL_NAME_COUNT, sizeof(NameRef), MEM_CAT_EVAL);
        if (!keys) return NULL;
        if (!context_capsule_extension_install(context, MVP_LMD_CAPSULE_OWNER, 1, keys, &ops)) {
            mem_free(keys); return NULL;
        }
    }
    if (!keys[name]) keys[name] = name_pool_create_unique_private(context->name_pool,
        {names[name], strlen(names[name])});
    return keys[name];
}
MvpLmdRealmScope::MvpLmdRealmScope(MvpLmdProgram* program, bool native_realm) {
    previous = mvp_lmd_program_library();
    foreign = mvp_lmd_foreign_library;
    current = native_realm ? NULL : program ? mvp_lmd_program_library(program) : previous;
    if (current != previous && mvp_lmd_library_slot) {
        *mvp_lmd_library_slot = current;
        mvp_lmd_foreign_library = current && current->default_realm ? current : NULL;
        if (js_active_runtime_state) {
            previous_promise_context = js_runtime_state.promises.host_context;
            js_runtime_state.promises.host_context = native_realm ? NULL : mvp_lmd_program_realm(program);
        }
    }
}
MvpLmdRealmScope::~MvpLmdRealmScope() {
    if (current != previous) {
        if (mvp_lmd_library_slot) *mvp_lmd_library_slot = previous;
        mvp_lmd_foreign_library = foreign;
        if (js_active_runtime_state) js_runtime_state.promises.host_context = previous_promise_context;
    }
}
Item MvpLmdRealmScope::complete(Item result, bool force_error_payload) {
    if (!current || (current == previous && !force_error_payload)) return result;
    if (current->abort_kind && previous && !previous->abort_kind) {
        previous->abort_kind = current->abort_kind; previous->abort_site = current->abort_site;
    }
    if (item_is_error(result)) {
        RootFrame roots(1);
        if (!roots.valid()) return ItemError;
        Rooted<Item> error(roots, result);
        // materialize an implicit JS error before restoring the caller's intrinsics.
        Item caught = mvp_lmd_caught_value(current, error.get());
        return item_is_error(caught) ? caught : error.get();
    }
    return result;
}
struct MvpLmdRealmAmbient {
    Context* context;
    MvpLmdLibraryState** slot;
    MvpLmdLibraryState* current;
    MvpLmdLibraryState* foreign;
};
static void mvp_lmd_realm_save(void* opaque) {
    auto* state = (MvpLmdRealmAmbient*)opaque;
    state->current = mvp_lmd_program_library();
    state->context = mvp_lmd_library_context;
    state->slot = mvp_lmd_library_slot;
    state->foreign = mvp_lmd_foreign_library;
}
static void mvp_lmd_realm_load(const void* opaque) {
    const auto* state = (const MvpLmdRealmAmbient*)opaque;
    mvp_lmd_library_context = state->context;
    mvp_lmd_library_slot = state->slot;
    if (state->slot) *state->slot = state->current;
    mvp_lmd_foreign_library = state->foreign;
}
static const ActivationAmbientHook mvp_lmd_realm_ambient = {
    sizeof(MvpLmdRealmAmbient), mvp_lmd_realm_save, mvp_lmd_realm_save, mvp_lmd_realm_load, NULL};
[[maybe_unused]] static const bool mvp_lmd_realm_ambient_registered =
    activation_register_ambient_hook(&mvp_lmd_realm_ambient);
static void require_library(MvpLmdProgram* p, AstNode* site) {
    if (p->library_runtime) return;
    if (!p->library) p->library = (MvpLmdLibraryState*)pool_calloc(p->frontend->pool, sizeof(MvpLmdLibraryState));
    if (!p->library) { diagnostic(p, site, "builtin metadata allocation"); return; }
    p->library_runtime = true;
    p->library_slot = p->slot_count; p->slot_count += LMD_LIBRARY_COUNT;
}
static void activate_library(MvpLmdProgram* p, AstNode* site, bool observes_object_mutation = true) {
    if (observes_object_mutation) p->immutable_objects = false;
    p->library_active = p->receiver_abi = true;
    require_library(p, site);
}
static void admit_shared_realm(MvpLmdProgram* p, AstNode* site) {
    // external source and host callbacks can observe all Script bindings.
    p->dynamic_source = p->global_receiver = p->global_reflection =
        p->receiver_abi = p->property_callbacks = p->symbols = true;
    activate_library(p, site);
    if (p->execution_owner && !p->realm_program)
        p->execution_owner->shared_realm = true;
}
static bool program_has_existing_global(MvpLmdProgram* p) {
    if (!p->execution_owner) return true;
    JsRuntimeState* state = js_runtime_state_for(p->execution_owner->owner_context);
    Item* global = state && state->global_environment
        ? root_vector_at(&state->global_environment->roots, JS_GLOBAL_ENV_GLOBAL_THIS) : NULL;
    return global && get_type_id(*global) == LMD_TYPE_MAP;
}
static String* global_self_alias_store(MvpLmdProgram* p, AstNode* node) {
    if (!node || node->node_type != AST_NODE_ASSIGN) return NULL;
    auto* assignment = (AstAssignNode*)node;
    if (assignment->op != OPERATOR_ASSIGN || assignment->right->node_type != AST_NODE_IDENT ||
            (assignment->left->node_type != AST_NODE_MEMBER_EXPR &&
             assignment->left->node_type != AST_NODE_INDEX_EXPR)) return NULL;
    auto* value = (AstIdentNode*)assignment->right;
    auto* target = (AstFieldNode*)assignment->left;
    String* name = member_spelling(target);
    if (!name || named(name, "__proto__") || value->entry || !named(value->name, "globalThis") ||
            target->object->node_type != AST_NODE_IDENT) return NULL;
    auto* receiver = (AstIdentNode*)target->object;
    if (receiver->entry || !named(receiver->name, "globalThis")) return NULL;
    AstNode* statement = ast_index_parent(&p->frontend->ast_index, node);
    AstNode* owner = statement ? ast_index_parent(&p->frontend->ast_index, statement) : NULL;
    return statement && statement->node_type == AST_NODE_EXPR_STMT &&
        owner && owner->node_type == AST_SCRIPT ? name : NULL;
}
static void observe_global_receiver(MvpLmdProgram* p, AstNode* node, AstNode* parent) {
    String* key = parent && (parent->node_type == AST_NODE_MEMBER_EXPR ||
        parent->node_type == AST_NODE_INDEX_EXPR) && ((AstFieldNode*)parent)->object == node
        ? member_spelling((AstFieldNode*)parent) : NULL;
    if (!key && parent && parent->node_type == AST_NODE_ASSIGN &&
            ((AstAssignNode*)parent)->right == node && global_self_alias_store(p, parent)) {
        // defer this closed-store candidate until binding and key domains are solved.
        p->global_alias_store = true;
        return;
    }
    // an escaped global or unknown key can expose eval or a source loader.
    if (!key || named(key, "eval") || named(key, "Function") || named(key, "require"))
        admit_shared_realm(p, node);
}
static void observe_coercion_member(MvpLmdProgram* p, AstNode* node, String* name) {
    if (!named(name, "toString") && !named(name, "valueOf") && !named(name, "toJSON")) return;
    activate_library(p, node);
    p->coercion_callbacks = true;
    if (named(name, "toJSON")) p->property_callbacks = true;
}
static bool simple_array_pattern(AstNode* node, int count = -1) {
    if (!node || (node->node_type != AST_NODE_ARRAY_PATTERN && node->node_type != AST_NODE_ARRAY)) return false;
    int length = 0;
    for (AstNode* child = ((AstArrayNode*)node)->item; child; child = child->next, length++)
        if (child->node_type != AST_NODE_IDENT) return false;
    return count < 0 || length == count;
}
static bool pattern_iterators_are_intrinsic(MvpLmdProgram* p) {
    // iterator prototypes can be reached through values()/reflection without spelling Symbol.
    return !p->symbols && !p->property_callbacks && !p->global_reflection && !p->array_method_overrides;
}
static void collect_pattern_binding(AstNode* node, void* opaque) {
    MvpLmdProgram* p = (MvpLmdProgram*)opaque;
    if (node->node_type == AST_NODE_IDENT) {
        LmdBinding* b = identifier_binding(p, (AstIdentNode*)node);
        if (b) { b->kinds = K_ANY; b->assigned = b->implicit_write = true; b->dominated = false; }
        else p->global_receiver = p->receiver_abi = true;
    } else if (node->node_type == AST_NODE_MEMBER_EXPR || node->node_type == AST_NODE_INDEX_EXPR) {
        // pattern/iteration targets perform the same observable Set as explicit assignments.
        activate_library(p, node); p->property_callbacks = p->array_holes = p->array_named_properties = true;
    } else js_ast_visit_binding_pattern_children(node, collect_pattern_binding, p);
}
static void walk(AstNode* n, LmdWalk* state) {
    if (!n) return;
    MvpLmdProgram* p = state->p;
    LmdWalk inner = *state;
    inner.parent = n;
    // type annotation tags share the AST's numeric domain, but use a separate enum.
    switch ((int)n->node_type) {
    case AST_SCRIPT:
        collect_scope(p, state->owner, ((AstScript*)n)->global_vars); break;
    case AST_NODE_IMPORT: case AST_NODE_IMPORT_SPECIFIER: case AST_NODE_EXPORT_SPECIFIER:
        if (!p->source_module) diagnostic(p, n, "module declaration outside module goal");
        return;
    case AST_NODE_EXPORT:
        if (!p->source_module) { diagnostic(p, n, "module declaration outside module goal"); return; }
        walk(((AstExportDeclNode*)n)->declaration, &inner); return;
    case AST_NODE_BLOCK:
        collect_scope(p, state->owner, ((AstBlockNode*)n)->vars); break;
    case JS_AST_NODE_WITH_STATEMENT: {
        activate_library(p, n);
        p->global_receiver = p->global_reflection = p->receiver_abi = true;
        p->property_callbacks = p->primitives = true; p->immutable_objects = false;
        if (!state->facts) {
            if (!p->with_objects) p->with_objects = (LmdBinding**)pool_calloc(p->frontend->pool,
                p->frontend->ast_index.count * sizeof(LmdBinding*));
            NameEntry* entry = (NameEntry*)pool_calloc(p->frontend->pool, sizeof(NameEntry));
            LmdBinding* object = (LmdBinding*)mem_calloc(1, sizeof(LmdBinding), MEM_CAT_TEMP);
            if (!p->with_objects || !entry || !object) {
                mem_free(object); diagnostic(p, n, "object environment allocation"); return;
            }
            entry->name = name_pool_create_len(p->frontend->name_pool, "<with>", 6);
            entry->node = n; entry->is_lexical = true;
            object->entry = entry; object->owner = state->owner; object->kinds = K_OBJECT;
            arraylist_append(p->bindings, object);
            p->with_objects[ast_index_find(&p->frontend->ast_index, n)] = object;
            capture_binding(p, state->owner, object, n);
        }
        break;
    }
    case AST_NODE_TRY_STAM: activate_library(p, n); p->errors = true; break;
    case AST_NODE_CATCH_CLAUSE: {
        JsCatchNode* handler = (JsCatchNode*)n;
        collect_scope(p, state->owner, handler->vars);
        if (handler->param) collect_pattern_binding(handler->param, p);
        break;
    }
    case AST_NODE_LOOP:
        // host errors need intrinsic storage without enabling observable object coercion.
        require_library(p, n); p->errors = true;
        collect_scope(p, state->owner, ((AstLoopControlNode*)n)->vars); break;
    case AST_NODE_FOR_IN_STAM: case AST_NODE_FOR_OF_STAM: {
        require_library(p, n); p->errors = true;
        JsForOfNode* loop = (JsForOfNode*)n;
        collect_scope(p, state->owner, loop->vars);
        if (loop->init && n->node_type != AST_NODE_FOR_IN_STAM) diagnostic(p, n, "for-of initializer");
        if (loop->is_await) {
            if ((!state->owner->ast && !p->source_module) ||
                    (state->owner->ast && !state->owner->ast->is_async))
                diagnostic(p, n, "for-await outside async function or module");
            activate_library(p, n);
            p->promises = p->iterators = p->symbols = p->errors = p->property_callbacks = p->receiver_abi = true;
        }
        AstNode* head = loop->left;
        if (head->node_type == AST_NODE_ARRAY_PATTERN || head->node_type == AST_NODE_MAP_PATTERN) {
            if (head->node_type == AST_NODE_ARRAY_PATTERN && !simple_array_pattern(head, 2)) {
                activate_library(p, head); p->iterators = true;
            }
        } else if (head->node_type != AST_NODE_IDENT && (loop->declares_binding ||
                (head->node_type != AST_NODE_MEMBER_EXPR && head->node_type != AST_NODE_INDEX_EXPR &&
                 head->node_type != AST_NODE_CALL_EXPR)))
            diagnostic(p, head, "for-of binding");
        LmdBinding* key = loop->node_type == AST_NODE_FOR_IN_STAM && head->node_type == AST_NODE_IDENT
            ? identifier_binding(p, (AstIdentNode*)head) : NULL;
        unsigned previous = key ? key->kinds : 0;
        collect_pattern_binding(head, p);
        // enumeration adds only strings; preserve other incoming or assigned values in the same binding.
        if (key) key->kinds = previous | K_STRING;
        break;
    }
    case AST_NODE_ARRAY_PATTERN: {
        AstNode* parent = state->parent;
        bool pair = parent && parent->node_type == AST_NODE_FOR_OF_STAM &&
            simple_array_pattern(n, 2);
        if (pair && state->facts) {
            // only an unmodified intrinsic Map guarantees fresh entry pairs without an inner iterator.
            AstNode* source = ((JsForOfNode*)parent)->right;
            if (source->node_type == AST_NODE_IDENT) {
                LmdBinding* origin = identifier_binding(p, (AstIdentNode*)source);
                if (origin && !origin->assigned && !origin->external && origin->initializer) source = origin->initializer;
            }
            AstNode* callee = source->node_type == AST_NODE_NEW_EXPR ? ((AstCallNode*)source)->callee : NULL;
            pair = pattern_iterators_are_intrinsic(p) && callee && callee->node_type == AST_NODE_IDENT && !((AstIdentNode*)callee)->entry &&
                named(((AstIdentNode*)callee)->name, "Map");
        }
        bool literal = pattern_iterators_are_intrinsic(p) && parent && parent->node_type == AST_NODE_ASSIGN && simple_array_pattern(n) &&
            ((AstAssignNode*)parent)->right->node_type == AST_NODE_ARRAY;
        if (literal) for (AstNode* e = ((AstArrayNode*)((AstAssignNode*)parent)->right)->item; e; e = e->next)
            if (e->node_type == AST_NODE_SPREAD || e->node_type == AST_NODE_NULL) literal = false;
        if (!pair && !literal) {
            activate_library(p, n); p->iterators = p->primitives = true; collect_pattern_binding(n, p);
        }
        break;
    }
    case AST_NODE_MAP_PATTERN:
        activate_library(p, n); collect_pattern_binding(n, p); break;
    case AST_NODE_CLASS: case AST_NODE_CLASS_EXPR: {
        AstClassNode* cls = (AstClassNode*)n;
        p->receiver_abi = true; p->immutable_objects = false;
        MvpLmdClass* class_type = class_plan(p, cls);
        if (!class_type) { diagnostic(p, n, "class allocation"); return; }
        if (!state->facts) for (AstNode* member = ((AstBlockNode*)cls->body)->statements; member; member = member->next) {
            bool computed, statics;
            AstNode* key = js_ast_class_element_key(member, &computed, &statics);
            if (!computed && key && key->node_type == AST_NODE_IDENT &&
                    private_binding(p, n, ((AstIdentNode*)key)->name, state->owner)) {
                class_type->dynamic = class_type->has_private_names = true;
                p->property_callbacks = true;
                if (member->node_type == AST_NODE_METHOD) {
                    class_type->private_method_mask |= statics ? 2 : 1;
                    class_type->has_instance_elements |= !statics;
                }
            }
        }
        if (class_type->dynamic) activate_library(p, n);
        if (cls->superclass && cls->superclass->node_type != AST_NODE_IDENT) p->property_callbacks = true;
        collect_scope(p, state->owner, cls->entry ? cls->entry->scope : NULL);
        collect_scope(p, state->owner, ((JsClassNode*)cls)->expression_scope);
        NameEntry* names[] = {cls->entry, cls->outer_entry};
        for (NameEntry* entry : names) {
            LmdBinding* b = binding(p, entry);
            if (b) { b->kinds = K_FUNCTION; b->class_name = n->node_type == AST_NODE_CLASS && state->parent && state->parent->node_type == AST_SCRIPT; }
        }
        walk(cls->superclass, &inner); walk(cls->body, &inner); return;
    }
    case AST_NODE_FIELD: {
        AstClassFieldNode* field = (AstClassFieldNode*)n;
        if (!field->key) { diagnostic(p, n, "missing field key"); return; }
        String* name = !field->computed && field->key->node_type == AST_NODE_IDENT ? ((AstIdentNode*)field->key)->name : NULL;
        observe_coercion_member(p, n, name);
        if (field->computed) { p->property_callbacks = true; walk(field->key, &inner); }
        if (field->value) walk((AstNode*)js_script_field_initializer_ensure(p->frontend, field), &inner);
        return;
    }
    case JS_AST_NODE_STATIC_BLOCK:
        walk((AstNode*)js_script_field_initializer_ensure(p->frontend, NULL, (JsStaticBlockNode*)n), &inner); return;
    case AST_NODE_MAP: case AST_NODE_OBJECT_LITERAL:
        for (AstNode* e = ((AstMapNode*)n)->properties; e; e = e->next)
            if (e->node_type == AST_NODE_SPREAD) { activate_library(p, e); p->immutable_objects = false; }
            else if (e->node_type != AST_NODE_PROPERTY) diagnostic(p, e, "object literal member");
        break;
    case AST_NODE_PROPERTY: {
        AstPropertyNode* prop = (AstPropertyNode*)n;
        String* property_name = property_spelling(prop->key, prop->computed);
        if (state->parent && state->parent->node_type == AST_NODE_MAP_PATTERN &&
                (!property_name || named(property_name, "constructor")))
            p->suspended_constructor_observed = true;
        if (state->parent && state->parent->node_type == AST_NODE_MAP_PATTERN && named(property_name, "prototype")) {
            // Destructuring exposes the same mutable prototype as a member read.
            activate_library(p, n); p->property_callbacks = true;
        }
        observe_coercion_member(p, n, property_name);
        if (prop->computed && prop->method) { activate_library(p, n); p->property_callbacks = true; }
        if (prop->computed || prop->key->node_type != AST_NODE_IDENT) p->immutable_objects = false;
        if (prop->is_getter || prop->is_setter) {
            activate_library(p, n); p->property_callbacks = true; p->immutable_objects = false;
        }
        if (prop->method) p->receiver_abi = true;
        if (prop->computed) walk(prop->key, &inner);
        else if ((prop->key->node_type == AST_NODE_IDENT && named(((AstIdentNode*)prop->key)->name, "__proto__")) ||
                (prop->key->node_type == AST_NODE_LITERAL && ((AstLiteralNode*)prop->key)->literal_type == AST_LITERAL_STRING &&
                 named(((AstLiteralNode*)prop->key)->value.string_value, "__proto__"))) {
            activate_library(p, n); p->property_callbacks = true; p->immutable_objects = false;
        }
        walk(prop->value, &inner); return;
    }
    case AST_NODE_NEW_EXPR: {
        AstCallNode* call = (AstCallNode*)n;
        if (!call->interp_call_shape_planned) ast_plan_call_shape(call);
        if (call->interp_has_spread_args) { activate_library(p, n); p->iterators = true; }
        AstNode* target = call->callee;
        AstIdentNode* id = target->node_type == AST_NODE_IDENT ? (AstIdentNode*)target : NULL;
        if (state->facts && id) {
            LmdBinding* ctor = identifier_binding(p, id);
            // native-only units need no function-instance guards in every open read.
            if (ctor && ctor->target && !ctor->target->home) p->function_construction = true;
        }
        if (id && named(id->name, "Array")) p->array_holes = true;
        if (id && named(id->name, "Map") && ((AstCallNode*)n)->arguments) {
            activate_library(p, n); p->iterators = true;
        }
        if (!id || id->entry || (!named(id->name, "Array") && !named(id->name, "Map") &&
                !named(id->name, "Object") && !named(id->name, "Error") &&
                !named(id->name, "Int32Array") && !named(id->name, "Uint8Array") && !named(id->name, "Float64Array")))
            p->receiver_abi = true;
        break;
    }
    case AST_NODE_IF_EXPR:
        collect_scope(p, state->owner, ((JsIfNode*)n)->consequent_vars);
        collect_scope(p, state->owner, ((JsIfNode*)n)->alternate_vars); break;
    case AST_NODE_MATCH_EXPR:
        collect_scope(p, state->owner, ((JsSwitchNode*)n)->vars); break;
    case AST_NODE_METHOD:
    case AST_NODE_FUNC: case AST_NODE_FUNC_EXPR: case AST_NODE_ARROW_FUNC: {
        AstFuncNode* ast = (AstFuncNode*)n;
        LmdFunction* f = function(p, n);
        if (!f) {
            f = (LmdFunction*)mem_calloc(1, sizeof(LmdFunction), MEM_CAT_TEMP);
            f->ast = ast;
            f->id = (uint32_t)p->functions->length;
            f->parent = state->owner;
            snprintf(f->name, sizeof(f->name), "mvp_lmd_f%u", f->id);
            arraylist_append(p->functions, f);
        }
        if (n->node_type == AST_NODE_METHOD) {
            AstMethodNode* method = (AstMethodNode*)n;
            AstNode* parent = ast_index_parent(&p->frontend->ast_index, state->parent);
            if (!parent || (parent->node_type != AST_NODE_CLASS && parent->node_type != AST_NODE_CLASS_EXPR)) {
                diagnostic(p, n, "class member outside basic method scope"); return;
            }
            if (method->kind == AstMethodNode::JS_METHOD_GET || method->kind == AstMethodNode::JS_METHOD_SET) {
                activate_library(p, n); p->property_callbacks = true;
            }
            String* method_name = !method->computed && method->key->node_type == AST_NODE_IDENT ? ((AstIdentNode*)method->key)->name : NULL;
            observe_coercion_member(p, n, method_name);
            f->home = class_plan(p, (AstClassNode*)parent);
            if (method->kind == AstMethodNode::JS_METHOD_CONSTRUCTOR) f->home->constructor_id = f->id;
            if (method->computed) { p->property_callbacks = true; walk(method->key, &inner); }
        } else if (state->parent && (state->parent->node_type == AST_NODE_FIELD || state->parent->node_type == JS_AST_NODE_STATIC_BLOCK)) {
            f->object_home = true;
        }
        JsAstParameterFacts params = js_ast_collect_parameter_facts(ast->params);
        f->complex_params = params.has_non_simple_params;
        f->parameter_expressions = params.has_parameter_expressions;
        f->duplicate_params = params.has_duplicate_param_names;
        f->formal_length = (uint8_t)(params.formal_length < 0 ? params.parameter_count : params.formal_length);
        JsAstFunctionFacts facts = js_ast_collect_function_facts(ast->params, ast->body);
        bool arrow = n->node_type == AST_NODE_ARROW_FUNC;
        if (arrow && (facts.observations & (JS_AST_OBSERVES_THIS | JS_AST_OBSERVES_NEW_TARGET))) {
            LmdFunction* parent = f->parent;
            while (parent && parent->ast && parent->ast->node_type == AST_NODE_ARROW_FUNC) parent = parent->parent;
            if ((!parent || !parent->ast) && !p->source_module) p->global_receiver = p->receiver_abi = true;
            else if (parent && parent->ast) {
                parent->receiver_captured = parent->observes_receiver = true;
                p->receiver_abi = true;
                if (!parent->home && !parent->ast->has_use_strict_directive &&
                        !(parent->ast->vars && parent->ast->vars->strict)) p->global_receiver = true;
                for (LmdFunction* current = f; current != parent; current = current->parent)
                    current->lexical_receiver = true;
            }
        }
        if (!arrow && (facts.observations & (JS_AST_OBSERVES_THIS | JS_AST_OBSERVES_NEW_TARGET))) {
            f->observes_receiver = true; p->receiver_abi = true;
            if (!f->home && !ast->has_use_strict_directive && !(ast->vars && ast->vars->strict))
                p->global_receiver = true;
        }
        if (ast->is_generator) { activate_library(p, n); p->iterators = true;
            if (!ast->is_async) p->generator_functions = true; }
        if (ast->is_async) {
            activate_library(p, n);
            p->promises = p->property_callbacks = p->iterators = p->errors = p->receiver_abi = true;
            if (ast->is_generator) p->async_generators = true;
            else p->async_functions = true;
        }
        if (facts.has_direct_eval) f->direct_eval = true;
        inner.owner = f;
        collect_scope(p, f, ast->vars);
        if (!state->facts && !arrow && ((facts.observations & JS_AST_OBSERVES_ARGUMENTS) || facts.observes_direct_eval)) {
            String* name = name_pool_create_len(p->frontend->name_pool, "arguments", 9);
            NameEntry* entry = name_scope_lookup_name(ast->vars, name);
            bool shadowed = entry && (entry->is_parameter || (!f->complex_params &&
                (entry->is_lexical || (entry->node && entry->node->node_type == AST_NODE_FUNC))));
            if (!shadowed) {
                activate_library(p, n); p->property_callbacks = true; p->iterators = true;
                if (entry && f->complex_params && !entry->is_lexical &&
                        (!entry->node || entry->node->node_type != AST_NODE_FUNC)) {
                    f->body_arguments_binding = binding(p, entry);
                    f->body_arguments_binding->kinds |= K_OBJECT;
                    f->body_arguments_binding->external = true;
                }
                if (!entry || f->complex_params) {
                    // defaults close over the parameter binding, before body var/function declarations exist.
                    entry = (NameEntry*)pool_calloc(p->frontend->pool, sizeof(NameEntry));
                    if (!entry) { diagnostic(p, n, "arguments binding allocation"); return; }
                    entry->name = name; entry->scope = ast->vars; entry->is_mutable = true;
                    LmdBinding* local = (LmdBinding*)mem_calloc(1, sizeof(LmdBinding), MEM_CAT_TEMP);
                    if (!local) { diagnostic(p, n, "arguments binding allocation"); return; }
                    local->entry = entry; local->owner = f; arraylist_append(p->bindings, local);
                }
                f->arguments_binding = binding(p, entry);
                f->arguments_binding->kinds = K_OBJECT;
                f->arguments_binding->external = true;
                f->mapped_arguments = !f->complex_params && !ast->has_use_strict_directive && !(ast->vars && ast->vars->strict);
                if (f->mapped_arguments) for (AstNode* parameter = ast->params; parameter; parameter = parameter->next) {
                    LmdBinding* local = binding(p, js_ast_parameter_binding_identifier(parameter)->entry);
                    // indexed writes and escaped aliases use the same precise closure cell.
                    local->captured = local->external = local->implicit_write = true;
                    local->kinds = K_ANY; f->owns_captures = true;
                }
            }
        }
        for (AstNode* param = ast->params; param; param = param->next)
            if (!js_ast_parameter_binding_identifier(param)) {
                activate_library(p, param); collect_pattern_binding(param, p);
            }
        if (ast->vars && ast->vars->parent && ast->vars->parent->is_function_name_scope)
            collect_scope(p, f, ast->vars->parent);
        if (ast->entry) {
            LmdBinding* b = binding(p, ast->entry);
            if (b) {
                if (b->target && b->target != f) b->assigned = true;
                b->target = f;
                b->kinds |= K_FUNCTION;
                if (f->arguments_binding) b->observed = true;
                // the Annex B publication exposes this identity even without a local read.
                if (b->entry->annex_b_outer_binding) b->observed = true;
            }
        }
        if (f->home) {
            for (AstNode* a = ast->params; a; a = a->next) walk(a, &inner);
            walk(ast->body, &inner); return;
        }
        break;
    }
    case JS_AST_NODE_REGEX: activate_library(p, n); break;
    case AST_NODE_LITERAL:
        if (((AstLiteralNode*)n)->is_bigint) p->bigints = true; break;
    case AST_NODE_VAR_STAM:
        if (((AstVarDeclNode*)n)->is_using) diagnostic(p, n, "using declaration"); break;
    case AST_NODE_VARIABLE_DECLARATOR: {
        AstDeclaratorNode* d = (AstDeclaratorNode*)n;
        if (d->id && (d->id->node_type == AST_NODE_MAP_PATTERN || d->id->node_type == AST_NODE_ARRAY_PATTERN)) {
            activate_library(p, n); collect_pattern_binding(d->id, p);
        } else if (!d->id || d->id->node_type != AST_NODE_IDENT) diagnostic(p, n, "binding pattern");
        else {
            LmdBinding* b = binding(p, ((AstIdentNode*)d->id)->entry);
            if (b && d->init) {
                if (!state->facts) b->writes++;
                if (b->initializer && b->initializer != d->init) b->assigned = true;
                else if (!b->initializer) {
                    b->initializer = d->init;
                    AstNode* parent = state->parent;
                    AstNode* grand = ast_index_parent(&p->frontend->ast_index, parent);
                    // switch cases can enter after an initializer; lexical scope alone is no proof.
                    b->dominated = (grand && (grand->node_type == AST_SCRIPT ||
                            (grand->node_type == AST_NODE_BLOCK &&
                             (b->entry->is_lexical || (((AstBlockNode*)grand)->vars &&
                              ((AstBlockNode*)grand)->vars->is_function_body))))) ||
                        (grand && grand->node_type == AST_NODE_LOOP &&
                         ((AstLoopControlNode*)grand)->init == parent);
                }
            }
        }
        break;
    }
    case AST_NODE_IDENT: {
        AstIdentNode* id = (AstIdentNode*)n;
        if (LmdBinding* private_name = private_binding(p, n, id->name)) {
            id->entry = private_name->entry;
            capture_binding(p, state->owner, private_name, n);
            return;
        }
        if (!id->entry && named(id->name, "import.meta")) {
            if (!p->source_module) diagnostic(p, n, "import.meta outside module goal");
            return;
        }
        if (named(id->name, "arguments")) {
            LmdBinding* existing = binding(p, id->entry);
            for (LmdFunction* owner = state->owner; owner; owner = owner->parent) {
                if (existing && existing->owner == owner) {
                    if (!owner->arguments_binding) break;
                    bool in_body = false;
                    for (AstNode* parent = n; parent && parent != (AstNode*)owner->ast;
                            parent = ast_index_parent(&p->frontend->ast_index, parent))
                        if (parent == owner->ast->body) { in_body = true; break; }
                    if (!existing->entry->is_function_name_binding &&
                            (!owner->complex_params || in_body)) break;
                }
                // the implicit local shadows an outer explicit binding; arrows retain nearer locals.
                if (owner->arguments_binding) { id->entry = owner->arguments_binding->entry; break; }
                if (owner->ast && owner->ast->node_type != AST_NODE_ARROW_FUNC) break;
            }
        }
        if (named(id->name, "eval") && state->parent &&
                state->parent->node_type == AST_NODE_CALL_EXPR &&
                ((AstCallNode*)state->parent)->callee == n && !((AstCallNode*)state->parent)->optional) {
            state->owner->direct_eval = true;
            activate_library(p, n);
            p->dynamic_source = p->property_callbacks = p->global_receiver = p->global_reflection = p->receiver_abi = true;
        }
        if (named(id->name, "this") && !state->owner->ast) {
            p->global_receiver = p->global_reflection = true; p->receiver_abi = true;
            observe_global_receiver(p, n, state->parent);
        }
        if (!id->entry && (named(id->name, "parseFloat") || named(id->name, "isFinite") ||
                (p->host_enabled && (named(id->name, "print") || named(id->name, "gc")))))
            p->global_receiver = p->global_reflection = p->receiver_abi = true;
        if (named(id->name, "super")) {
            LmdFunction* method = state->owner;
            while (method && method->ast && method->ast->node_type == AST_NODE_ARROW_FUNC) method = method->parent;
            AstNode* property = method && method->ast ? ast_index_parent(&p->frontend->ast_index, (AstNode*)method->ast) : NULL;
            bool literal = property && property->node_type == AST_NODE_PROPERTY &&
                (((AstPropertyNode*)property)->method || ((AstPropertyNode*)property)->is_getter || ((AstPropertyNode*)property)->is_setter);
            if (!method || (!method->home && !literal && !method->object_home)) diagnostic(p, n, "super requires a home method");
            else {
                method->object_home |= literal;
                method->observes_receiver = true; p->receiver_abi = true;
                if (literal) { activate_library(p, n); p->property_callbacks = true; }
                if (method != state->owner) {
                    method->receiver_captured = true;
                    for (LmdFunction* current = state->owner; current != method; current = current->parent)
                        current->lexical_super = current->lexical_receiver = true;
                }
            }
        }
        if (!state->facts && !id->entry) {
            for (int i = 0; i < I_COUNT - 1; i++) if (named(id->name, intrinsic_names[i])) {
                AstNode* parent = state->parent;
                bool member_owner = parent && (parent->node_type == AST_NODE_MEMBER_EXPR || parent->node_type == AST_NODE_INDEX_EXPR) &&
                    ((AstFieldNode*)parent)->object == n;
                bool constructor = parent && (parent->node_type == AST_NODE_CALL_EXPR || parent->node_type == AST_NODE_NEW_EXPR) &&
                    ((AstCallNode*)parent)->callee == n;
                if (i + 1 == I_OBJECT || i + 1 == I_ARRAY || i + 1 == I_SYMBOL) {
                    // escaped constructors can expose reflective writers or mutable static methods.
                    if (!member_owner && !constructor) {
                        activate_library(p, n); p->property_callbacks = true;
                        p->array_method_overrides = p->map_method_overrides = p->primitive_method_overrides = true;
                    }
                    if (i + 1 == I_OBJECT && ((!member_owner && !constructor) ||
                            (constructor && ((AstCallNode*)parent)->arguments))) {
                        activate_library(p, n); p->primitives = true;
                    }
                }
                if (i + 1 == I_ERROR || (i + 1 >= I_EVAL_ERROR && i + 1 <= I_AGGREGATE_ERROR)) {
                    activate_library(p, n); p->errors = true;
                    if (i + 1 == I_AGGREGATE_ERROR) p->iterators = true;
                } else if (i + 1 == I_STRING || i + 1 == I_NUMBER || i + 1 == I_BOOLEAN || i + 1 == I_BIGINT) {
                    bool direct = state->parent && state->parent->node_type == AST_NODE_CALL_EXPR &&
                        ((AstCallNode*)state->parent)->callee == n;
                    if (!direct || i + 1 == I_BOOLEAN || i + 1 == I_BIGINT) {
                        activate_library(p, n); p->primitives = true;
                    }
                    // ordinary static calls do not expose the mutable instance prototype.
                    String* member = member_owner ? member_spelling((AstFieldNode*)parent) : NULL;
                    if (!direct && (!member_owner || !member || named(member, "prototype")))
                        p->primitive_method_overrides = true;
                    if (i + 1 == I_BIGINT) p->bigints = true;
                } else if (typed_intrinsic(i + 1) >= 0) {
                    p->typed_explicit = true;
                    bool simple = constructor && parent->node_type == AST_NODE_NEW_EXPR &&
                        i + 1 >= I_INT32_ARRAY && i + 1 <= I_FLOAT64_ARRAY;
                    AstNode* argument = simple ? ((AstCallNode*)parent)->arguments : NULL;
                    if (!simple || (argument && (argument->next || argument->node_type == AST_NODE_ARRAY ||
                            argument->node_type == AST_NODE_OBJECT_LITERAL || argument->node_type == AST_NODE_NEW_EXPR)))
                        p->typed_storage = true;
                } else if (i + 1 == I_TEST262) {
                    if (!p->test262_host) continue;
                    activate_library(p, n); p->buffers = p->primitives = p->property_callbacks = true;
                    p->dynamic_source = p->global_receiver = p->global_reflection = p->receiver_abi = true;
                } else if (i + 1 == I_ARRAYBUFFER || i + 1 == I_DATAVIEW || i + 1 == I_SHARED_ARRAYBUFFER) {
                    activate_library(p, n); p->buffers = p->primitives = p->property_callbacks = true;
                    if (i + 1 == I_SHARED_ARRAYBUFFER) p->shared_buffer = true;
                    p->intrinsics[i].kinds = K_FUNCTION;
                } else if (i + 1 == I_FUNCTION) {
                    activate_library(p, n); p->property_callbacks = true;
                    p->dynamic_source = p->global_receiver = p->global_reflection = p->receiver_abi = true;
                    p->intrinsics[i].kinds = K_FUNCTION;
                    p->intrinsics[i].slot = p->library_slot + LMD_LIBRARY_FUNCTION;
                } else if (i + 1 == I_EVAL) {
                    activate_library(p, n); p->property_callbacks = true;
                    p->dynamic_source = p->global_receiver = p->global_reflection = p->receiver_abi = true;
                    p->intrinsics[i].kinds = K_FUNCTION;
                } else if (i + 1 >= I_SET_TIMEOUT && i + 1 <= I_QUEUE_MICROTASK) {
                    if (!p->host_enabled) continue;
                    activate_library(p, n);
                    p->timers = p->promises = p->errors = p->property_callbacks = true;
                    p->global_receiver = p->global_reflection = p->receiver_abi = true;
                    p->intrinsics[i].kinds = K_FUNCTION;
                } else if (i + 1 >= I_DECODE_URI && i + 1 <= I_ENCODE_COMPONENT) {
                    activate_library(p, n); p->errors = true;
                    p->intrinsics[i].kinds = K_FUNCTION;
                    p->intrinsics[i].slot = p->library_slot + LMD_LIBRARY_DECODE_URI + i + 1 - I_DECODE_URI;
                } else if (i + 1 == I_ESCAPE || i + 1 == I_UNESCAPE) {
                    activate_library(p, n); p->errors = true;
                    p->intrinsics[i].kinds = K_FUNCTION;
                    p->intrinsics[i].slot = p->library_slot + LMD_LIBRARY_ESCAPE + i + 1 - I_ESCAPE;
                } else if (i + 1 == I_WEAK_MAP || i + 1 == I_WEAK_SET || i + 1 == I_WEAK_REF || i + 1 == I_FINALIZATION_REGISTRY) {
                    activate_library(p, n);
                    p->weak_collections = p->property_callbacks = p->iterators = p->errors = true;
                    if (i + 1 == I_WEAK_REF || i + 1 == I_FINALIZATION_REGISTRY) p->weak_lifetimes = p->promises = true;
                    p->intrinsics[i].kinds = K_FUNCTION;
                } else if (i + 1 == I_ATOMICS) {
                    activate_library(p, n);
                    p->atomics = p->buffers = p->errors = p->property_callbacks = p->promises = true;
                    p->intrinsics[i].kinds = K_OBJECT;
                } else if (i + 1 == I_PROMISE) {
                    activate_library(p, n);
                    p->promises = p->property_callbacks = p->iterators = p->errors = true;
                    p->intrinsics[i].kinds = K_FUNCTION;
                } else if (i + 1 == I_IS_NAN) activate_library(p, n);
                else if (i + 1 == I_MATH && (!state->parent ||
                        state->parent->node_type != AST_NODE_MEMBER_EXPR)) {
                    p->math_observed = true; activate_library(p, n);
                }
                else if (i + 1 == I_GLOBAL_THIS) {
                    p->global_receiver = p->receiver_abi = p->global_reflection = true;
                    p->immutable_objects = false;
                    observe_global_receiver(p, n, state->parent);
                } else if (i + 1 >= I_JSON) {
                    activate_library(p, n);
                    if (i + 1 == I_DATE) p->date_observed = true;
                    if (i + 1 == I_SYMBOL) p->symbols = p->iterators = true;
                    if (i + 1 == I_SET) p->iterators = true;
                    if (i + 1 == I_REFLECT || i + 1 == I_PROXY) p->property_callbacks = true;
                    if (i + 1 == I_PROXY) p->symbols = p->iterators = true;
                    int index = i + 1 == I_JSON ? LMD_LIBRARY_JSON : i + 1 == I_DATE ? LMD_LIBRARY_DATE :
                        i + 1 == I_SYMBOL ? LMD_LIBRARY_SYMBOL : i + 1 == I_SET ? LMD_LIBRARY_SET :
                        i + 1 == I_REFLECT ? LMD_LIBRARY_REFLECT : i + 1 == I_PROXY ? LMD_LIBRARY_PROXY : LMD_LIBRARY_REGEXP;
                    p->intrinsics[i].slot = p->library_slot + index;
                } else if (i + 1 >= I_PERFORMANCE) {
                    if (!p->host_enabled) continue;
                    if (i + 1 == I_PROCESS || i + 1 == I_REQUIRE) {
                        activate_library(p, n);
                        p->native_host_values = true;
                    }
                    if (i + 1 == I_PERFORMANCE) {
                        AstNode* consumer = member_owner ? ast_index_parent(&p->frontend->ast_index, parent) : NULL;
                        // host properties are mutable; only unexposed direct timer calls retain their return contract.
                        if (!member_owner || !named(member_spelling((AstFieldNode*)parent), "now") ||
                                !consumer || consumer->node_type != AST_NODE_CALL_EXPR ||
                                ((AstCallNode*)consumer)->callee != parent)
                            p->performance_observed = true;
                    }
                    if (!p->host_active) {
                        p->host_active = true; p->receiver_abi = true;
                        p->immutable_objects = false;
                        p->host_slot = p->slot_count; p->slot_count += LMD_HOST_COUNT;
                    }
                    p->intrinsics[i].slot = p->host_slot + i + 1 - I_PERFORMANCE;
                }
                bool assigned = state->parent && state->parent->node_type == AST_NODE_ASSIGN && ((AstAssignNode*)state->parent)->left == n;
                bool called = state->parent && state->parent->node_type == AST_NODE_CALL_EXPR && ((AstCallNode*)state->parent)->callee == n;
                if (assigned || ((i + 1 == I_PARSE_INT || i + 1 == I_IS_NAN) && !called))
                    p->global_receiver = p->global_reflection = p->receiver_abi = true;
                p->intrinsic_uses[ast_index_find(&p->frontend->ast_index, n)] = &p->intrinsics[i];
            }
        }
        LmdBinding* b = identifier_binding(p, id);
        if (!state->facts && p->host_enabled && !b &&
                !named(id->name, "this") && !named(id->name, "super") &&
                !named(id->name, "new.target") && !named(id->name, "arguments") &&
                js_host_namespace_has_name(id->name->chars, id->name->len)) {
            // native host dependencies use the installation catalog; implicit
            // globals retain the compiler's existing closed write inventory.
            admit_shared_realm(p, n);
        }
        if (!state->facts && b && b->intrinsic == I_REQUIRE) {
            AstNode* argument = state->parent && state->parent->node_type == AST_NODE_CALL_EXPR &&
                ((AstCallNode*)state->parent)->callee == n ? ((AstCallNode*)state->parent)->arguments : NULL;
            AstLiteralNode* literal = argument && argument->node_type == AST_NODE_LITERAL
                ? (AstLiteralNode*)argument : NULL;
            if (!literal || literal->literal_type != AST_LITERAL_STRING ||
                    !jube_specifier_source_closed(literal->value.string_value->chars))
                admit_shared_realm(p, n);
        }
        if (!b && state->parent && state->parent->node_type == AST_NODE_ASSIGN &&
                ((AstAssignNode*)state->parent)->left == n) {
            p->global_receiver = true; p->receiver_abi = true;
        }
        bool declaration_target = state->parent &&
            ((state->parent->node_type == AST_NODE_VARIABLE_DECLARATOR &&
              ((AstDeclaratorNode*)state->parent)->id == n) ||
             ((state->parent->node_type == AST_NODE_FUNC || state->parent->node_type == AST_NODE_FUNC_EXPR ||
               state->parent->node_type == AST_NODE_ARROW_FUNC || state->parent->node_type == AST_NODE_METHOD) &&
              ((AstFuncNode*)state->parent)->body != n));
        if (state->facts && b && !declaration_target) {
            AstNode* parent = state->parent;
            AstNode* grand = parent ? ast_index_parent(&p->frontend->ast_index, parent) : NULL;
            bool member = parent && (parent->node_type == AST_NODE_MEMBER_EXPR ||
                parent->node_type == AST_NODE_INDEX_EXPR) && ((AstFieldNode*)parent)->object == n;
            bool write = grand && ((grand->node_type == AST_NODE_ASSIGN && ((AstAssignNode*)grand)->left == parent) ||
                (grand->node_type == AST_NODE_UNARY && (((AstUnaryNode*)grand)->op == OPERATOR_JS_DELETE ||
                 ((AstUnaryNode*)grand)->op == OPERATOR_JS_INCREMENT || ((AstUnaryNode*)grand)->op == OPERATOR_JS_DECREMENT)) ||
                (grand->node_type == AST_NODE_CALL_EXPR && ((AstCallNode*)grand)->callee == parent));
            if (!member || write) b->array_observed = true;
            if (!b->class_name && b->owner != state->owner && (b->owner->id ||
                    (b->entry && b->entry->scope != ((AstScript*)p->frontend->ast_root)->global_vars)))
                capture_binding(p, state->owner, b, n);
            if (b->owner != state->owner) {
                // a function may run before a textually earlier program initializer.
                b->external = true; b->dominated = false;
            }
            if (!state->parent || state->parent->node_type != AST_NODE_CALL_EXPR ||
                    ((AstCallNode*)state->parent)->callee != n) b->observed = true;
            if (b->initializer && n->source_span.start_byte < b->initializer->source_span.end_byte)
                b->dominated = false;
        }
        break;
    }
    case AST_NODE_MEMBER_EXPR: case AST_NODE_INDEX_EXPR: {
        AstFieldNode* field = (AstFieldNode*)n;
        String* spelling = member_spelling(field);
        if (!spelling || named(spelling, "constructor") || named(spelling, "getPrototypeOf") ||
                named(spelling, "getOwnPropertyDescriptor") || named(spelling, "getOwnPropertyDescriptors"))
            p->suspended_constructor_observed = true;
        int collection_method = spelling ? mvp_lmd_builtin_method(spelling) : 0;
        // typed callbacks share Array spellings, not the Map collection table.
        int indexed_method = spelling ? mvp_lmd_builtin_method(spelling, LMD_TYPE_ARRAY) : 0;
        bool method_call = state->parent && state->parent->node_type == AST_NODE_CALL_EXPR &&
            ((AstCallNode*)state->parent)->callee == n;
        if (!method_call && indexed_method >= LMD_METHOD_COPY_WITHIN && indexed_method <= LMD_METHOD_WITH)
            activate_library(p, n);
        if (named(spelling, "groupBy") || named(spelling, "fromEntries")) { activate_library(p, n); p->iterators = true; }
        if (named(spelling, "matchAll")) {
            activate_library(p, n); p->iterators = true;
            if (!method_call) p->primitives = true;
        }
        if (named(spelling, "toLocaleString") || named(spelling, "toPrecision") ||
                named(spelling, "toExponential") || named(spelling, "replaceAll")) {
            activate_library(p, n); p->primitives = true; p->property_callbacks = true;
        }
        AstNode* consumer = method_call ? ast_index_parent(&p->frontend->ast_index, state->parent) : NULL;
        AstIdentNode* receiver = field->object->node_type == AST_NODE_IDENT ? (AstIdentNode*)field->object : NULL;
        if (receiver && !receiver->entry && ((named(receiver->name, "Object") && named(spelling, "is")) ||
                (named(receiver->name, "Array") && named(spelling, "of"))))
            activate_library(p, n);
        // borrowed methods and Object reflection can box receivers without spelling a primitive constructor.
        if (named(spelling, "call") || named(spelling, "apply") || named(spelling, "bind") ||
                (receiver && !receiver->entry && named(receiver->name, "Object") &&
                 (!spelling || named(spelling, "defineProperties") || named(spelling, "create") ||
                  named(spelling, "getOwnPropertyDescriptor") || named(spelling, "getOwnPropertyDescriptors") ||
                  named(spelling, "getOwnPropertyNames") || named(spelling, "getOwnPropertySymbols") ||
                  named(spelling, "assign")))) {
            activate_library(p, n); p->primitives = true;
        }
        if (receiver && !receiver->entry &&
                ((named(spelling, "prototype") && (named(receiver->name, "Object") || named(receiver->name, "Symbol"))) ||
                 (named(spelling, "getPrototypeOf") && named(receiver->name, "Object")))) {
            activate_library(p, n); p->primitives = true;
        }
        if (named(spelling, "create") || named(spelling, "getPrototypeOf") || named(spelling, "defineProperties") || named(spelling, "getOwnPropertyDescriptor") || named(spelling, "getOwnPropertyDescriptors") ||
                named(spelling, "propertyIsEnumerable") || named(spelling, "isPrototypeOf") ||
                named(spelling, "getOwnPropertyNames") || named(spelling, "getOwnPropertySymbols") || named(spelling, "preventExtensions") || named(spelling, "seal") ||
                named(spelling, "freeze") || named(spelling, "isExtensible") || named(spelling, "isSealed") || named(spelling, "isFrozen") ||
                named(spelling, "defineProperty") || named(spelling, "setPrototypeOf") || named(spelling, "__proto__") ||
                named(spelling, "__defineGetter__") || named(spelling, "__defineSetter__") ||
                named(spelling, "__lookupGetter__") || named(spelling, "__lookupSetter__")) {
            activate_library(p, n); p->property_callbacks = true; p->immutable_objects = false;
            p->array_method_overrides = p->map_method_overrides = p->primitive_method_overrides = true;
        }
        if (named(spelling, "defineProperty") || named(spelling, "assign") ||
                (receiver && !receiver->entry && named(receiver->name, "Object") && !spelling)) {
            p->array_method_overrides = p->map_method_overrides = p->primitive_method_overrides = true;
            if (named(spelling, "assign")) p->array_holes = p->array_named_properties = true;
            if (!spelling) { activate_library(p, n); p->property_callbacks = true; }
        }
        if (collection_method && !(receiver && !receiver->entry && named(receiver->name, "Object")) &&
                (!method_call || (collection_method >= 6 && (!consumer || consumer->node_type != AST_NODE_FOR_OF_STAM)))) {
            activate_library(p, n); p->iterators = true;
        }
        // a borrowed map method can create holes from an array-like receiver without sparse syntax.
        if (named(spelling, "map") && (!state->parent || state->parent->node_type != AST_NODE_CALL_EXPR ||
                ((AstCallNode*)state->parent)->callee != n)) p->array_holes = true;
        if (named(spelling, "prototype")) { activate_library(p, n); p->immutable_objects = false; }
        if (named(spelling, "call") || named(spelling, "apply") || named(spelling, "bind") || named(spelling, "hasOwnProperty") ||
                named(spelling, "toString") || named(spelling, "valueOf") || named(spelling, "constructor") ||
                named(spelling, "defineProperty") || named(spelling, "getPrototypeOf") || named(spelling, "create") || named(spelling, "assign") ||
                ((named(spelling, "push") || named(spelling, "concat") || named(spelling, "splice") || named(spelling, "unshift") || named(spelling, "shift") || named(spelling, "toReversed") || named(spelling, "at")) &&
                 (!state->parent || state->parent->node_type != AST_NODE_CALL_EXPR || ((AstCallNode*)state->parent)->callee != n)))
            activate_library(p, n);
        AstNode* parent = state->parent;
        bool write = parent && ((parent->node_type == AST_NODE_ASSIGN && ((AstAssignNode*)parent)->left == n) ||
                (parent->node_type == AST_NODE_UNARY && (((AstUnaryNode*)parent)->op == OPERATOR_JS_DELETE ||
                 ((AstUnaryNode*)parent)->op == OPERATOR_JS_INCREMENT || ((AstUnaryNode*)parent)->op == OPERATOR_JS_DECREMENT)));
        if (receiver && !receiver->entry && (named(receiver->name, "Object") || named(receiver->name, "Array")) && write) {
            activate_library(p, n); p->property_callbacks = true;
        }
        if (receiver && !receiver->entry && named(receiver->name, "Symbol") && !named(spelling, "iterator")) {
            activate_library(p, n); p->property_callbacks = true;
        }
        // implicit array conversion invokes these hooks through the native receiver-aware call ABI.
        if (write && (named(spelling, "toString") || named(spelling, "valueOf") || named(spelling, "toJSON") || named(spelling, "join"))) {
            activate_library(p, n); p->property_callbacks = true;
        }
        if (receiver && !receiver->entry && named(receiver->name, "Math") &&
                (write || (!method_call && !named(spelling, "PI")) || !spelling ||
                 (method_call && ((AstCallNode*)state->parent)->interp_has_spread_args))) {
            // an escaped namespace may be mutated through aliases before a later direct call.
            p->math_observed = true; activate_library(p, n);
        }
        if (receiver && !receiver->entry && named(receiver->name, "Math") && method_call) {
            bool direct = false;
            for (const auto& operation : math_operations) if (named(spelling, operation.name)) { direct = true; break; }
            // gathering has not visited the receiver yet; use the direct lane table, not its binding.
            if (!direct) activate_library(p, n);
        }
        if (write) p->immutable_objects = false;
        if (field->optional) activate_library(p, n);
        walk(field->object, &inner);
        if (field->computed || (spelling && spelling->len && spelling->chars[0] == '#')) walk(field->property, &inner);
        else if (!field->property || field->property->node_type != AST_NODE_IDENT)
            diagnostic(p, n, "invalid named property");
        return;
    }
    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)n;
        if (call->callee->node_type == AST_NODE_IDENT &&
                !((AstIdentNode*)call->callee)->entry && named(((AstIdentNode*)call->callee)->name, "import")) {
            activate_library(p, n);
            p->dynamic_source = p->global_receiver = p->global_reflection = p->receiver_abi = p->property_callbacks = true;
            if (!call->arguments || call->arguments->next || call->arguments->node_type == AST_NODE_SPREAD)
                diagnostic(p, n, "dynamic import attributes require a supported module request policy");
            walk(call->arguments, &inner);
            return;
        }
        if (js_ast_has_optional_chain(call->callee) && !js_ast_chain_continues(n, call->callee)) {
            activate_library(p, n); p->primitives = true;
        }
        if (!call->interp_call_shape_planned) ast_plan_call_shape(call);
        if (call->interp_has_spread_args) {
            activate_library(p, n); p->iterators = true;
            LmdBinding* target = call->callee->node_type == AST_NODE_IDENT ? identifier_binding(p, (AstIdentNode*)call->callee) : NULL;
            if (target) target->observed = true;
        }
        if (call->callee->node_type == AST_NODE_IDENT && named(((AstIdentNode*)call->callee)->name, "Array"))
            p->array_holes = true;
        if (call->optional) activate_library(p, n);
        if (call->callee->node_type == AST_NODE_MEMBER_EXPR || call->callee->node_type == AST_NODE_INDEX_EXPR) {
            String* method = member_spelling((AstFieldNode*)call->callee);
            if (named(method, "match") || named(method, "search") || named(method, "replace")) activate_library(p, n);
            if (named(method, "from") || named(method, "matchAll")) { activate_library(p, n); p->iterators = true; }
            if (named(method, "map")) p->array_holes = true;
            if (named(method, "sort") || named(method, "toSorted")) p->receiver_abi = true;
            if (method && mvp_lmd_builtin_method(method, LMD_TYPE_ARRAY) >= LMD_METHOD_EVERY)
                activate_library(p, n);
            // method mutation invalidates the same closed-unit literal facts as indexed writes.
            if (!method || named(method, "push") || named(method, "pop") || named(method, "fill") ||
                    named(method, "reverse") || named(method, "sort") || named(method, "splice") || named(method, "unshift") || named(method, "shift") || named(method, "copyWithin"))
                p->immutable_objects = false;
        }
        break;
    }
    case AST_NODE_ASSIGN:
        if (((AstAssignNode*)n)->left && (((AstAssignNode*)n)->left->node_type == AST_NODE_ARRAY ||
                ((AstAssignNode*)n)->left->node_type == AST_NODE_ARRAY_PATTERN)) {
            AstAssignNode* assignment = (AstAssignNode*)n;
            if (assignment->op != OPERATOR_ASSIGN) diagnostic(p, n, "compound destructuring");
            for (AstNode* e = ((AstArrayNode*)assignment->left)->item; e; e = e->next) {
                if (e->node_type != AST_NODE_IDENT) { collect_pattern_binding(e, p); continue; }
                LmdBinding* b = binding(p, ((AstIdentNode*)e)->entry);
                if (b) {
                    b->assigned = true;
                    // literal RHS elements have independent facts; an arbitrary array may contain anything.
                    if (assignment->right->node_type != AST_NODE_ARRAY) {
                        b->kinds = K_ANY; b->implicit_write = true; b->dominated = false;
                        b->array = {LMD_ARRAY_UNKNOWN, {0, 0, 2}};
                    }
                    if (!state->facts) b->writes++;
                } else collect_pattern_binding(e, p);
            }
        } else if (((AstAssignNode*)n)->left && ((AstAssignNode*)n)->left->node_type == AST_NODE_IDENT) {
            LmdBinding* b = binding(p, ((AstIdentNode*)((AstAssignNode*)n)->left)->entry);
            if (b) {
                b->assigned = true;
                if (!state->facts) { b->writes++; b->update = n; }
            }
        }
        break;
    case AST_NODE_UNARY:
        if (((AstUnaryNode*)n)->op == OPERATOR_JS_DELETE)
            p->array_holes = p->deletes_properties = true;
        if (!state->facts && (((AstUnaryNode*)n)->op == OPERATOR_JS_INCREMENT ||
                ((AstUnaryNode*)n)->op == OPERATOR_JS_DECREMENT) &&
                ((AstUnaryNode*)n)->operand->node_type == AST_NODE_IDENT) {
            LmdBinding* b = identifier_binding(p, (AstIdentNode*)((AstUnaryNode*)n)->operand);
            if (b) { b->writes++; b->update = n; }
        }
        if (((AstUnaryNode*)n)->op == OPERATOR_JS_DELETE && ((AstUnaryNode*)n)->operand->node_type == AST_NODE_IDENT)
            p->global_receiver = p->global_reflection = p->receiver_abi = true;
        break;
    case AST_NODE_SPREAD:
        if ((!state->parent || (state->parent->node_type != AST_NODE_ARRAY && state->parent->node_type != AST_NODE_CALL_EXPR &&
                state->parent->node_type != AST_NODE_NEW_EXPR &&
                state->parent->node_type != AST_NODE_MAP && state->parent->node_type != AST_NODE_OBJECT_LITERAL)) &&
                !(state->owner->complex_params && state->parent == (AstNode*)state->owner->ast))
            diagnostic(p, n, "spread requires an array literal");
        break;
    case AST_NODE_ASSIGN_PATTERN: case TS_AST_NODE_PARAMETER: {
        // type annotations retain a wrapper around the same executable parameter shape.
        AstNode* target = js_ast_parameter_pattern(n);
        AstNode* default_value = js_ast_parameter_default_value(n);
        if (target->node_type == AST_NODE_ASSIGN_PATTERN) target = ((AstAssignNode*)target)->left;
        // inferred function names need intrinsic metadata even when no property read activates it.
        if (target->node_type == AST_NODE_IDENT && default_value &&
                (default_value->node_type == AST_NODE_FUNC_EXPR || default_value->node_type == AST_NODE_ARROW_FUNC) &&
                !((AstFuncNode*)default_value)->name) activate_library(p, n);
        break;
    }
    case AST_NODE_REST_ELEMENT: case AST_NODE_REST_PROPERTY: break;
    case AST_NODE_BINARY:
        if (((AstBinaryNode*)n)->op == OPERATOR_JS_INSTANCEOF) {
            p->receiver_abi = true;
            AstNode* target = ((AstBinaryNode*)n)->right;
            if (target->node_type == AST_NODE_IDENT && !((AstIdentNode*)target)->entry &&
                    (named(((AstIdentNode*)target)->name, "Array") || named(((AstIdentNode*)target)->name, "Object")))
                activate_library(p, n);
        }
        break;
    case AST_NODE_YIELD:
        if (!state->owner->ast || !state->owner->ast->is_generator) diagnostic(p, n, "yield outside generator");
        break;
    case AST_NODE_AWAIT:
        if ((!state->owner->ast && !p->source_module) ||
                (state->owner->ast && !state->owner->ast->is_async))
            diagnostic(p, n, "await outside async function or module");
        break;
    case AST_NODE_ARRAY:
        for (AstNode* element = ((AstArrayNode*)n)->item; element; element = element->next)
            if (element->node_type == AST_NODE_NULL) p->array_holes = true;
        break;
    case JS_AST_NODE_TAGGED_TEMPLATE: {
        activate_library(p, n); p->property_callbacks = true;
        if (js_ast_has_optional_chain(((JsTaggedTemplateNode*)n)->tag)) p->primitives = true;
        if (!state->facts) {
            if (!p->template_slots) p->template_slots = (uint32_t*)pool_calloc(p->frontend->pool,
                p->frontend->ast_index.count * sizeof(uint32_t));
            if (!p->template_slots) { diagnostic(p, n, "template cache allocation"); return; }
            p->template_slots[ast_index_find(&p->frontend->ast_index, n)] = ++p->slot_count;
        }
        break;
    }
    case AST_NODE_NULL: case AST_NODE_EXPR_STMT: case AST_NODE_RETURN_STAM: case AST_NODE_RAISE_STAM:
    case AST_NODE_BREAK_STAM: case AST_NODE_CONTINUE_STAM: case AST_NODE_SEQ:
    case AST_NODE_CONDITIONAL_EXPR:
    case JS_AST_NODE_TEMPLATE_LITERAL: case JS_AST_NODE_TEMPLATE_ELEMENT:
    case AST_NODE_MATCH_ARM:
        break;
    default:
        if (n->node_type != JS_AST_NODE_LABELED_STATEMENT)
            diagnostic(p, n, "syntax outside MVP scope");
    }
    js_ast_visit_children(n, child, &inner);
}

static bool with_scope_boundary(AstNode* node, AstNode* child, NameScope* scope) {
    if (!scope) return false;
    switch (node->node_type) {
    case AST_SCRIPT: return ((AstScript*)node)->global_vars == scope;
    case AST_NODE_BLOCK: return ((AstBlockNode*)node)->vars == scope;
    case AST_NODE_LOOP: return ((AstLoopControlNode*)node)->vars == scope;
    case AST_NODE_FOR_IN_STAM: case AST_NODE_FOR_OF_STAM: return ((JsForOfNode*)node)->vars == scope;
    case AST_NODE_CATCH_CLAUSE: return ((JsCatchNode*)node)->vars == scope;
    case AST_NODE_MATCH_EXPR: return ((JsSwitchNode*)node)->vars == scope;
    case AST_NODE_CLASS: case AST_NODE_CLASS_EXPR: return ((JsClassNode*)node)->expression_scope == scope;
    case AST_NODE_FUNC: case AST_NODE_FUNC_EXPR: case AST_NODE_ARROW_FUNC: case AST_NODE_METHOD: {
        NameScope* vars = ((AstFuncNode*)node)->vars;
        return vars == scope || (vars && vars->parent == scope && scope->is_function_name_scope);
    }
    case AST_NODE_IF_EXPR: {
        JsIfNode* branch = (JsIfNode*)node;
        return (child == branch->then && branch->consequent_vars == scope) ||
            (child == branch->otherwise && branch->alternate_vars == scope);
    }
    default: return false;
    }
}
static bool eval_reference_site(MvpLmdProgram* p, AstNode* node) {
    AstNodeId id = p->eval_references ? ast_index_find(&p->frontend->ast_index, node) : AST_NODE_ID_INVALID;
    return id < p->frontend->ast_index.count && p->eval_references[id];
}
static void plan_eval_environments(MvpLmdProgram* p) {
    bool any = false;
    for (int i = 0; i < p->functions->length; i++) {
        LmdFunction* f = (LmdFunction*)p->functions->data[i];
        if (!f->direct_eval) continue;
        any = true; f->eval_lookup = true;
        for (LmdFunction* outer = f; outer; outer = outer->parent) outer->eval_environment = true;
    }
    if (!any) return;
    if (p->source_module) p->module_eval_slot = p->slot_count++;
    p->bigints = p->symbols = true;
    p->eval_references = (uint8_t*)pool_calloc(p->frontend->pool, p->frontend->ast_index.count);
    if (!p->eval_references) { diagnostic(p, NULL, "eval reference plan allocation"); return; }
    for (int i = 1; i < p->functions->length; i++) {
        LmdFunction* f = (LmdFunction*)p->functions->data[i];
        // A closure made before eval must later resolve vars introduced in its enclosing call.
        for (LmdFunction* outer = f->parent; outer && outer->id; outer = outer->parent)
            if (outer->direct_eval) f->eval_lookup = f->eval_environment = true;
        if (!f->eval_environment) continue;
        // suspended MIR bodies retain these same live cells in their precise activation roots.
        // eval can observe lexical super even when the source body does not spell it.
        LmdFunction* home = f;
        while (home->ast && home->ast->node_type == AST_NODE_ARROW_FUNC) home = home->parent;
        AstNode* site = home && home->ast ? ast_index_parent(&p->frontend->ast_index, (AstNode*)home->ast) : NULL;
        if (site && site->node_type == AST_NODE_PROPERTY &&
                (((AstPropertyNode*)site)->method || ((AstPropertyNode*)site)->is_getter || ((AstPropertyNode*)site)->is_setter))
            home->object_home = true;
        if (home && (home->home || home->object_home))
            for (LmdFunction* arrow = f; arrow != home; arrow = arrow->parent) arrow->lexical_super = true;
        for (AstNode* outer = (AstNode*)f->ast; outer;
                outer = ast_index_parent(&p->frontend->ast_index, outer)) {
            if (outer->node_type == AST_NODE_CLASS || outer->node_type == AST_NODE_CLASS_EXPR) {
                MvpLmdClass* cls = class_plan(p, (AstClassNode*)outer);
                cls->dynamic = cls->eval_environment = true;
            }
        }
        f->eval_capture = f->parent && f->parent->eval_environment;
        f->owns_captures = true;
        if (f->ast->node_type == AST_NODE_ARROW_FUNC) {
            LmdFunction* owner = f->parent;
            while (owner && owner->ast && owner->ast->node_type == AST_NODE_ARROW_FUNC) owner = owner->parent;
            if (owner) {
                owner->receiver_captured = true;
                if (owner->ast) owner->observes_receiver = true;
                for (LmdFunction* arrow = f; arrow != owner; arrow = arrow->parent) arrow->lexical_receiver = true;
            }
        } else f->receiver_captured = f->observes_receiver = true;
    }
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        if (!b->owner->eval_environment || b->intrinsic || b->with_lookup || b->module_cell || !b->entry->scope) continue;
        bool global_var = !b->owner->id && !b->entry->is_lexical &&
            b->entry->scope == ((AstScript*)p->frontend->ast_root)->global_vars;
        if (!global_var) capture_binding(p, b->owner, b, b->entry->node);
        b->external = b->assigned = b->observed = b->implicit_write = b->array_observed = true;
        b->kinds = K_ANY; b->dominated = false;
    }
    for (AstNodeId i = 0; i < p->frontend->ast_index.count; i++) {
        AstNode* node = p->frontend->ast_index.nodes[i];
        AstFunctionId owner = p->frontend->ast_index.owner_functions[i];
        LmdFunction* f = owner == AST_FUNCTION_ID_INVALID ? NULL :
            function(p, p->frontend->ast_index.functions[owner].node);
        if (!f) f = (LmdFunction*)p->functions->data[0];
        if (node->node_type == AST_NODE_CALL_EXPR && !((AstCallNode*)node)->optional &&
                ((AstCallNode*)node)->callee->node_type == AST_NODE_IDENT &&
                named(((AstIdentNode*)((AstCallNode*)node)->callee)->name, "eval")) {
            for (AstNode* parent = ast_index_parent(&p->frontend->ast_index, node); parent && parent != (AstNode*)f->ast;
                    parent = ast_index_parent(&p->frontend->ast_index, parent)) {
                if (parent->node_type == AST_NODE_CLASS || parent->node_type == AST_NODE_CLASS_EXPR) {
                    MvpLmdClass* cls = class_plan(p, (AstClassNode*)parent);
                    cls->dynamic = cls->eval_environment = true;
                }
            }
        }
        if (node->node_type == AST_NODE_IDENT) {
            LmdBinding* b = identifier_binding(p, (AstIdentNode*)node);
            if (b && b->class_name && b->captured && b->owner != f &&
                    b->entry->scope != ((AstScript*)p->frontend->ast_root)->global_vars) {
                // static class-name reads become cell captures when eval exposes their record.
                capture_binding(p, f, b, node);
                for (LmdFunction* owner = f; owner && owner != b->owner; owner = owner->parent)
                    if (owner->home) owner->home->dynamic = true;
            }
        }
        if (node->node_type == AST_NODE_IDENT && f->eval_lookup) {
            String* name = ((AstIdentNode*)node)->name;
            p->eval_references[i] = !(name->len && name->chars[0] == '#') &&
                !named(name, "this") && !named(name, "new.target") &&
                !named(name, "super") && !named(name, "import.meta");
        }
    }
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        if (b->private_name && class_plan(p, (AstClassNode*)b->entry->node)->eval_environment)
            capture_binding(p, b->owner, b, b->entry->node);
    }
}

static void plan_with_bindings(MvpLmdProgram* p) {
    if (!p->with_objects) return;
    AstIndex* index = &p->frontend->ast_index;
    p->with_bindings = (LmdBinding**)pool_calloc(p->frontend->pool, index->count * sizeof(LmdBinding*));
    if (!p->with_bindings) { diagnostic(p, NULL, "object environment references"); return; }
    for (AstNodeId i = 0; i < index->count; i++) {
        AstNode* node = index->nodes[i];
        if (node->node_type != AST_NODE_IDENT || js_ast_fact_walk_skips_child(index->parents[i], node)) continue;
        AstIdentNode* id = (AstIdentNode*)node;
        if (named(id->name, "this") || named(id->name, "super") || named(id->name, "new.target")) continue;
        LmdBinding* original = identifier_binding(p, id);
        // private names use their lexical class environment, never an object environment.
        if (original && original->private_name) continue;
        LmdFunction* owner = NULL;
        AstFunctionId function_id = index->owner_functions[i];
        if (function_id < index->function_count) owner = function(p, index->functions[function_id].node);
        if (!owner) owner = (LmdFunction*)p->functions->data[0];
        LmdWithLookup* first = NULL;
        LmdWithLookup** tail = &first;
        AstNode* child = node;
        for (AstNode* parent = index->parents[i]; parent;
                child = parent, parent = ast_index_parent(index, parent)) {
            if (with_scope_boundary(parent, child, id->entry ? id->entry->scope : NULL)) break;
            if (parent->node_type != JS_AST_NODE_WITH_STATEMENT || ((JsWithStatementNode*)parent)->body != child) continue;
            LmdWithLookup* lookup = (LmdWithLookup*)pool_calloc(p->frontend->pool, sizeof(LmdWithLookup));
            if (!lookup) { diagnostic(p, node, "object environment lookup"); return; }
            lookup->object = p->with_objects[ast_index_find(index, parent)];
            *tail = lookup; tail = &lookup->outer;
            capture_binding(p, owner, lookup->object, node);
        }
        if (!first) continue;
        LmdBinding* view = (LmdBinding*)pool_calloc(p->frontend->pool, sizeof(LmdBinding));
        if (!view) { diagnostic(p, node, "object environment binding"); return; }
        view->entry = id->entry;
        if (!view->entry) {
            view->entry = (NameEntry*)pool_calloc(p->frontend->pool, sizeof(NameEntry));
            if (!view->entry) { diagnostic(p, node, "object environment name"); return; }
            view->entry->name = id->name; view->entry->node = node;
        }
        view->owner = owner; view->fallback = original; view->with_lookup = first;
        view->kinds = K_ANY; view->external = view->observed = true;
        view->array = {LMD_ARRAY_UNKNOWN, {0, 0, 2}};
        p->with_bindings[i] = view;
        // a dynamic reference needs the real function value even if every source use is a call.
        if (original) {
            original->observed = true;
            // writes through a view can reach the lexical fallback after arbitrary property effects.
            if (!original->intrinsic) {
                original->external = true; original->kinds = K_ANY;
                original->array = {LMD_ARRAY_UNKNOWN, {0, 0, 2}};
            }
        }
    }
}

static Operator compound_operator(Operator op);
static LmdFunction* direct_target(MvpLmdProgram* p, AstCallNode* call) {
    if (call->interp_has_spread_args || eval_reference_site(p, call->callee)) return NULL;
    LmdBinding* b = call->callee->node_type == AST_NODE_IDENT
        ? identifier_binding(p, (AstIdentNode*)call->callee) : NULL;
    return b && b->target && !b->target->ast->is_generator && !b->target->ast->is_async && !b->assigned && !b->target->captures && !b->target->lexical_receiver
        ? b->target : NULL;
}
static unsigned binary_kind(Operator op, unsigned left, unsigned right, bool bigints = false) {
    if (op == OPERATOR_AND || op == OPERATOR_OR || op == OPERATOR_JS_NULLISH_COALESCE)
        return left | right;
    // observable conversion of an object can select either primitive arithmetic domain.
    unsigned* operands[] = {&left, &right};
    for (unsigned* operand : operands)
        if (*operand & (K_OBJECT | K_ARRAY | K_MAP | K_TYPED_ARRAY | K_FUNCTION))
            *operand |= K_NUMBER | K_STRING | (bigints ? K_BIGINT : 0);
    if (op == OPERATOR_ADD) return ((left | right) & K_STRING ? K_STRING : 0) |
        ((left & ~(K_STRING | K_BIGINT)) && (right & ~(K_STRING | K_BIGINT)) ? K_NUMBER : 0) |
        (left & right & K_BIGINT);
    if (op == OPERATOR_JS_STRICT_EQ || op == OPERATOR_JS_STRICT_NE || op == OPERATOR_IN || op == OPERATOR_JS_INSTANCEOF) return K_BOOL;
    MirNumericOpPlan plan;
    return em_numeric_op_plan(op, &plan) && plan.is_comparison ? K_BOOL :
        (left == K_BIGINT || right == K_BIGINT) ? K_BIGINT : K_NUMBER | (left & right & K_BIGINT);
}
static bool numeric_operands(Operator op, unsigned left, unsigned right, bool inert = false) {
    constexpr unsigned objects = K_OBJECT | K_ARRAY | K_MAP | K_TYPED_ARRAY | K_FUNCTION;
    if ((left | right) & K_BIGINT) return false;
    if (((left | right) & objects) && !inert) return false;
    // an ordinary object's primitive may be a string even in a callback-free unit.
    if (left & objects) left |= K_STRING;
    if (right & objects) right |= K_STRING;
    if (op == OPERATOR_EQ || op == OPERATOR_NE || op == OPERATOR_JS_STRICT_EQ ||
            op == OPERATOR_JS_STRICT_NE || op == OPERATOR_JS_INSTANCEOF) return false;
    if (op == OPERATOR_ADD) return !((left | right) & K_STRING);
    MirNumericOpPlan plan = {};
    if (em_numeric_op_plan(op, &plan)) return !plan.is_comparison || !((left & right) & K_STRING);
    return op == OPERATOR_JS_EXP || (op >= OPERATOR_JS_BIT_AND && op <= OPERATOR_JS_URSHIFT);
}
static LmdScalarField* scalar_field(MvpLmdProgram* p, AstNode* node);
static AstNode* scalar_field_initializer(MvpLmdProgram* p, AstNode* node);
static LmdArrayFacts array_facts(MvpLmdProgram* p, AstNode* n);
static LmdRange range(MvpLmdProgram* p, AstNode* n, bool scoped = false, bool present_only = false);
static bool primitive_property_key(MvpLmdProgram* p, AstNode* node, AstNode** path, int depth = 0,
    unsigned primitive = K_NUMBER | K_BOOL | K_NULL | K_UNDEFINED | K_BIGINT, uint8_t* visited = NULL);
static LmdRange finite_range(double lower, double upper);
static bool finite_integer(double value);
static AstNode* enclosing_loop(MvpLmdProgram* p, AstNode* node, LmdFunction* owner, bool include_for_of = false);
static unsigned immutable_member_kind(MvpLmdProgram* p, String* name) {
    if (!p->immutable_objects || !name) return K_ANY;
    unsigned result = 0;
    const AstIndex* index = &p->frontend->ast_index;
    for (uint32_t id = 0; id < index->count; id++) {
        AstNode* object = index->nodes[id];
        if (object->node_type != AST_NODE_MAP && object->node_type != AST_NODE_OBJECT_LITERAL) continue;
        unsigned field = K_UNDEFINED;
        for (AstNode* e = ((AstMapNode*)object)->properties; e; e = e->next) {
            AstPropertyNode* prop = (AstPropertyNode*)e;
            String* key = ((AstIdentNode*)prop->key)->name;
            if (key->len == name->len && !memcmp(key->chars, name->chars, name->len))
                field = p->literal_kinds[ast_index_find(index, e)];
        }
        result |= field;
    }
    return result;
}
static unsigned kind(MvpLmdProgram* p, AstNode* n, LmdInlineFrame* facts = NULL) {
    if (!n) return K_UNDEFINED;
    auto infer = [&]() -> unsigned {
    switch (n->node_type) {
    case JS_AST_NODE_REGEX: return K_OBJECT;
    case AST_NODE_LITERAL: {
        static const unsigned kinds[] = {K_NUMBER, K_STRING, K_BOOL, K_NULL, K_UNDEFINED};
        return ((AstLiteralNode*)n)->is_bigint ? K_BIGINT : kinds[((AstLiteralNode*)n)->literal_type];
    }
    case AST_NODE_IDENT: {
        AstIdentNode* id = (AstIdentNode*)n;
        if (named(id->name, "this") || named(id->name, "super") || named(id->name, "new.target") ||
                eval_reference_site(p, n)) return K_ANY;
        LmdBinding* b = identifier_binding(p, id);
        for (int i = 0; facts && i < facts->count; i++)
            if (facts->bindings[i] == b) return facts->slots[i].kinds;
        if (b && b->reflected_global && !b->fixed_global) return K_ANY;
        if (b && b->intrinsic) return b->kinds;
        if (b) return b->kinds | (!b->dominated && !b->entry->is_lexical &&
            !b->entry->is_parameter && !b->target ? K_UNDEFINED : 0);
        // unresolved names still read live host/global properties; a local
        // alias must not replace their callable or object value with undefined.
        return K_ANY;
    }
    case AST_NODE_MEMBER_EXPR: case AST_NODE_INDEX_EXPR: {
        LmdRegionRead* read = numeric_region_read(p, facts ? facts->region : NULL, n);
        if (read && facts && facts->region == read->region) return read->kind;
        AstFieldNode* member = (AstFieldNode*)n;
        unsigned owner = kind(p, member->object, facts);
        if (!owner) return 0;
        String* spelling = member_spelling(member);
        if ((owner == K_TYPED_ARRAY || owner == K_ARRAY || owner == K_STRING) && named(spelling, "length"))
            return K_NUMBER;
        // element facts do not describe named methods or inherited values at holes.
        unsigned key_kind = member->computed && !spelling ? kind(p, member->property, facts) : 0;
        if ((owner == K_TYPED_ARRAY || owner == K_ARRAY) && member->computed && !spelling && !key_kind) return 0;
        // optional typed keys join named writes; their values need not fit a numeric lane.
        // a closed dense array cannot acquire an "undefined" attribute; match the emitted optional-index path.
        bool optional_index = owner == K_TYPED_ARRAY ||
            (owner == K_ARRAY && !p->property_callbacks && !p->array_named_properties);
        bool indexed = spelling ? mvp_lmd_string_key(spelling) >= 0 :
            member->computed && (key_kind == K_NUMBER || (optional_index &&
                !(key_kind & ~(K_NUMBER | K_UNDEFINED))));
        if (indexed && (owner == K_TYPED_ARRAY ||
                (owner == K_ARRAY && array_facts(p, member->object).lanes == LMD_ARRAY_ITEMS))) {
            LmdArrayFacts array = array_facts(p, member->object);
            LmdRange index = member->computed ? range(p, member->property, true) : LmdRange{};
            bool present = (owner != K_TYPED_ARRAY || spelling || key_kind == K_NUMBER) &&
                index.state == 1 && array.length.state == 1 && index.lower >= 0 && index.upper < array.length.lower;
            if (present) return K_NUMBER;
            if (owner == K_TYPED_ARRAY || !p->property_callbacks) return K_NUMBER | K_UNDEFINED |
                (owner == K_TYPED_ARRAY && key_kind != K_NUMBER && !spelling ? p->typed_named_kinds : 0);
        }
        LmdBinding* b = !member->computed && member->object->node_type == AST_NODE_IDENT
            ? identifier_binding(p, (AstIdentNode*)member->object) : NULL;
        if (b && b->intrinsic == I_MATH && named(spelling, "PI")) return K_NUMBER;
        for (int i = 0; b && facts && i < facts->count; i++) {
            if (facts->bindings[i] != b || !facts->guarded[i]) continue;
            String* name = ((AstIdentNode*)member->property)->name;
            ShapeEntry* field = typemap_hash_lookup(facts->plans[i]->blueprint, name->chars, name->len);
            if (!field) break;
            TypeId tid = field->type->type_id;
            return tid == LMD_TYPE_INT || tid == LMD_TYPE_FLOAT ? K_NUMBER :
                tid == LMD_TYPE_BOOL ? K_BOOL : tid == LMD_TYPE_STRING ? K_STRING :
                tid == LMD_TYPE_NULL ? K_NULL : tid == LMD_TYPE_UNDEFINED ? K_UNDEFINED : K_ANY;
        }
        LmdScalarField* field = scalar_field(p, n);
        if (field) return field->kinds;
        // every successful plain-object read comes from a literal in this closed MVP unit.
        return (owner & K_OBJECT) && !(owner & ~(K_OBJECT | K_NULL | K_UNDEFINED))
            ? immutable_member_kind(p, spelling) : K_ANY;
    }
    case JS_AST_NODE_TEMPLATE_LITERAL: return K_STRING;
    case AST_NODE_ARRAY: return K_ARRAY;
    case AST_NODE_MAP: case AST_NODE_OBJECT_LITERAL: return K_OBJECT;
    case AST_NODE_NEW_EXPR: {
        AstCallNode* call = (AstCallNode*)n;
        LmdBinding* ctor = call->callee->node_type == AST_NODE_IDENT
            ? identifier_binding(p, (AstIdentNode*)call->callee) : NULL;
        return ctor && ctor->intrinsic == I_ARRAY ? K_ARRAY :
            ctor && typed_intrinsic(ctor->intrinsic) >= 0 ? (p->typed_storage ? K_OBJECT : K_TYPED_ARRAY) :
            ctor && ctor->intrinsic == I_MAP ? K_MAP : K_OBJECT | K_ARRAY | K_TYPED_ARRAY | K_FUNCTION;
    }
    case AST_NODE_CALL_EXPR: {
        LmdRegionRead* read = numeric_region_read(p, facts ? facts->region : NULL, n);
        if (read && read->target && facts && facts->region == read->region) return read->target->returns;
        if (math_operation(p, (AstCallNode*)n) >= 0) return K_NUMBER;
        AstNode* callee = ((AstCallNode*)n)->callee;
        LmdBinding* builtin = callee->node_type == AST_NODE_IDENT ? identifier_binding(p, (AstIdentNode*)callee) : NULL;
        if (builtin && builtin->intrinsic == I_ARRAY) return K_ARRAY;
        if (builtin && builtin->intrinsic == I_STRING) return K_STRING;
        if (builtin && builtin->intrinsic == I_SYMBOL) return K_SYMBOL;
        if (builtin && builtin->intrinsic == I_IS_NAN) return K_BOOL;
        if (builtin && (builtin->intrinsic == I_NUMBER || builtin->intrinsic == I_PARSE_INT)) return K_NUMBER;
        if (callee->node_type == AST_NODE_MEMBER_EXPR || callee->node_type == AST_NODE_INDEX_EXPR) {
            AstFieldNode* member = (AstFieldNode*)callee;
            unsigned receiver = kind(p, member->object, facts);
            String* spelling = member_spelling(member);
            if (receiver == K_ARRAY && p->array_method_overrides) return K_ANY;
            if ((receiver == K_STRING || receiver == K_NUMBER || receiver == K_BIGINT) &&
                    p->primitive_method_overrides) return K_ANY;
            if (named(spelling, "fill") && (!receiver || receiver == K_TYPED_ARRAY || receiver == K_ARRAY)) return receiver;
            if (receiver == K_ARRAY && named(spelling, "push")) return K_NUMBER;
            if (receiver == K_ARRAY && (named(spelling, "join") || named(spelling, "toString"))) return K_STRING;
            if (receiver == K_ARRAY && (named(spelling, "map") || named(spelling, "flat"))) return K_ARRAY;
            if (receiver == K_ARRAY && (named(spelling, "some") || named(spelling, "includes"))) return K_BOOL;
            if (receiver == K_STRING && named(spelling, "split")) return K_ARRAY;
            if (receiver == K_STRING && named(spelling, "indexOf")) return K_NUMBER;
            if (receiver == K_STRING && named(spelling, "startsWith")) return K_BOOL;
            if (receiver == K_NUMBER && named(spelling, "toFixed")) return K_STRING;
            if (receiver == K_BIGINT && named(spelling, "toString")) return K_STRING;
            if (receiver == K_STRING && (named(spelling, "substring") || named(spelling, "slice") ||
                    named(spelling, "toUpperCase"))) return K_STRING;
            if (receiver == K_STRING && named(spelling, "charCodeAt")) return K_NUMBER;
            if (receiver == K_STRING && (named(spelling, "charAt") || named(spelling, "repeat"))) return K_STRING;
            LmdBinding* object = member->object->node_type == AST_NODE_IDENT ? identifier_binding(p, (AstIdentNode*)member->object) : NULL;
            if (object && object->intrinsic == I_PERFORMANCE && named(spelling, "now") &&
                    !p->performance_observed && !p->property_callbacks && !p->global_reflection) return K_NUMBER;
            if (object && object->intrinsic == I_STRING && named(spelling, "fromCharCode")) return K_STRING;
            // bottom is not an unknown receiver: closed-call arguments may arrive on the next iteration.
            if (!receiver) return 0;
        }
        LmdFunction* f = direct_target(p, (AstCallNode*)n);
        return f ? f->returns : K_ANY;
    }
    case AST_NODE_FUNC: case AST_NODE_FUNC_EXPR: case AST_NODE_ARROW_FUNC: case AST_NODE_CLASS: case AST_NODE_CLASS_EXPR: return K_FUNCTION;
    case AST_NODE_UNARY: {
        Operator op = ((AstUnaryNode*)n)->op;
        unsigned operand = kind(p, ((AstUnaryNode*)n)->operand, facts);
        if (p->bigints && (operand & (K_OBJECT | K_ARRAY | K_MAP | K_TYPED_ARRAY | K_FUNCTION)))
            operand |= K_BIGINT;
        return op == OPERATOR_NOT || op == OPERATOR_JS_DELETE ? K_BOOL : op == OPERATOR_JS_TYPEOF ? K_STRING :
            op == OPERATOR_JS_VOID ? K_UNDEFINED : op == OPERATOR_POS ? K_NUMBER :
            operand == K_BIGINT ? K_BIGINT : K_NUMBER | (operand & K_BIGINT);
    }
    case AST_NODE_BINARY: {
        AstBinaryNode* b = (AstBinaryNode*)n;
        return binary_kind(b->op, kind(p, b->left, facts), kind(p, b->right, facts), p->bigints);
    }
    case AST_NODE_ASSIGN: {
        AstAssignNode* a = (AstAssignNode*)n;
        unsigned right = kind(p, a->right, facts);
        if (a->op == OPERATOR_ASSIGN) return right;
        if (a->op == OPERATOR_JS_AND_ASSIGN || a->op == OPERATOR_JS_OR_ASSIGN ||
                a->op == OPERATOR_JS_NULLISH_ASSIGN) return kind(p, a->left, facts) | right;
        return binary_kind(compound_operator(a->op), kind(p, a->left, facts), right, p->bigints);
    }
    case AST_NODE_IF_EXPR: case AST_NODE_CONDITIONAL_EXPR:
        return kind(p, ((AstIfNode*)n)->then, facts) | kind(p, ((AstIfNode*)n)->otherwise, facts);
    case AST_NODE_SEQ: {
        AstNode* item = ((AstArrayNode*)n)->item;
        while (item && item->next) item = item->next;
        return kind(p, item, facts);
    }
    default: return K_ANY;
    }
    };
    // unrelated units retain their original scalar kind facts and generated dispatch.
    return (infer() | (js_ast_has_optional_chain(n) ? K_UNDEFINED : 0)) &
        (p->bigints ? K_ANY : K_ANY & ~K_BIGINT) & (p->symbols ? K_ANY : K_ANY & ~K_SYMBOL);
}
static LmdArrayFacts array_facts(MvpLmdProgram* p, AstNode* n) {
    if (!n) return {LMD_ARRAY_UNKNOWN, {0, 0, 2}};
    switch (n->node_type) {
    case AST_NODE_IDENT: {
        LmdBinding* b = identifier_binding(p, (AstIdentNode*)n);
        if (!b || b->intrinsic) break;
        if ((!b->array_observed || p->immutable_objects) && b->writes == 1 && b->initializer &&
                b->initializer->node_type == AST_NODE_ARRAY) return array_facts(p, b->initializer);
        LmdArrayFacts result = b->array;
        // aliases are safe only when the entire closed unit excludes property mutation.
        if ((result.lanes & LMD_ARRAY_UNKNOWN) ||
                ((result.lanes & LMD_ARRAY_ITEMS) && !p->immutable_objects)) result = {LMD_ARRAY_UNKNOWN, {0, 0, 2}};
        return result;
    }
    case AST_NODE_ARRAY: {
        AstArrayNode* a = (AstArrayNode*)n;
        LmdRange elements = {};
        for (AstNode* e = a->item; e; e = e->next) {
            AstNode* literal = e->node_type == AST_NODE_UNARY && ((AstUnaryNode*)e)->op == OPERATOR_NEG
                ? ((AstUnaryNode*)e)->operand : e;
            if (literal->node_type != AST_NODE_LITERAL ||
                    ((AstLiteralNode*)literal)->literal_type != AST_LITERAL_NUMBER ||
                    ((AstLiteralNode*)literal)->is_bigint) return {LMD_ARRAY_UNKNOWN, {0, 0, 2}};
            LmdRange r = range(p, e);
            if (r.state != 1 || elements.state == 2) elements = {0, 0, 2};
            else elements = elements.state ? finite_range(fmin(elements.lower, r.lower), fmax(elements.upper, r.upper)) : r;
        }
        return {LMD_ARRAY_ITEMS, finite_range(a->length, a->length), elements};
    }
    case AST_NODE_NEW_EXPR: {
        AstCallNode* call = (AstCallNode*)n;
        LmdBinding* ctor = call->callee->node_type == AST_NODE_IDENT
            ? identifier_binding(p, (AstIdentNode*)call->callee) : NULL;
        if (p->typed_storage || !ctor || ctor->intrinsic < I_INT32_ARRAY || ctor->intrinsic > I_FLOAT64_ARRAY) break;
        LmdRange length = call->arguments ? range(p, call->arguments) : finite_range(0, 0);
        if (length.state == 2) length = finite_range(0, INT53_MAX);
        if (length.state == 1) length = finite_range(fmax(0, length.lower), fmax(0, length.upper));
        return {1u << (ctor->intrinsic - I_INT32_ARRAY), length};
    }
    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)n;
        if (call->callee->node_type == AST_NODE_MEMBER_EXPR) {
            AstFieldNode* member = (AstFieldNode*)call->callee;
            if (!member->computed && named(((AstIdentNode*)member->property)->name, "fill")) {
                LmdArrayFacts result = array_facts(p, member->object);
                if (!(result.lanes & (LMD_ARRAY_ITEMS | LMD_ARRAY_UNKNOWN))) return result;
            }
        }
        LmdFunction* f = direct_target(p, call);
        return f && !(f->array.lanes & LMD_ARRAY_UNKNOWN) ? f->array : LmdArrayFacts{LMD_ARRAY_UNKNOWN, {0, 0, 2}};
    }
    case AST_NODE_ASSIGN:
        if (((AstAssignNode*)n)->op == OPERATOR_ASSIGN) return array_facts(p, ((AstAssignNode*)n)->right);
        break;
    case AST_NODE_CONDITIONAL_EXPR: {
        LmdArrayFacts a = array_facts(p, ((AstIfNode*)n)->then), b = array_facts(p, ((AstIfNode*)n)->otherwise);
        if (!a.lanes) return b;
        if (!b.lanes) return a;
        LmdRange length = !a.length.state || !b.length.state ? LmdRange{} :
            a.length.state == 1 && b.length.state == 1
                ? finite_range(fmin(a.length.lower, b.length.lower), fmax(a.length.upper, b.length.upper)) : LmdRange{0, 0, 2};
        return {a.lanes | b.lanes, length};
    }
    default: break;
    }
    return {LMD_ARRAY_UNKNOWN, {0, 0, 2}};
}
// only proven abrupt paths exclude the implicit undefined return.
static bool falls_through(AstNode* n) {
    if (!n) return true;
    if (n->node_type == AST_NODE_RETURN_STAM) return false;
    if (n->node_type == AST_NODE_BLOCK) {
        for (AstNode* s = ((AstBlockNode*)n)->statements; s; s = s->next)
            if (!falls_through(s)) return false;
    } else if (n->node_type == AST_NODE_IF_EXPR) {
        JsIfNode* branch = (JsIfNode*)n;
        return falls_through(branch->then) || falls_through(branch->otherwise);
    }
    return true;
}
struct LmdTypes { MvpLmdProgram* p; bool changed; LmdFunction* owner = NULL; LmdInlineFrame* facts = NULL; };
static void visit_destructured_assignments(AstNode* node, void (*visit)(AstNode*, void*), void* state) {
    if (node->node_type != AST_NODE_ASSIGN) return;
    AstAssignNode* assignment = (AstAssignNode*)node;
    if (!simple_array_pattern(assignment->left) ||
            assignment->right->node_type != AST_NODE_ARRAY) return;
    AstNode* value = ((AstArrayNode*)assignment->right)->item;
    for (AstNode* target = ((AstArrayNode*)assignment->left)->item; target; target = target->next) {
        // reuse ordinary assignment transfer without changing the indexed source AST.
        AstAssignNode element = *assignment;
        element.left = target; element.right = value;
        visit((AstNode*)&element, state);
        if (value) value = value->next;
    }
}
static void union_kinds(LmdTypes* t, unsigned* target, unsigned incoming) {
    if ((*target | incoming) != *target) { *target |= incoming; t->changed = true; }
}
static void type_walk(AstNode* n, void* arg) {
    if (!n) return;
    LmdTypes* t = (LmdTypes*)arg;
    visit_destructured_assignments(n, type_walk, arg);
    if (n->node_type == AST_NODE_PROPERTY && t->p->immutable_objects && !t->facts)
        union_kinds(t, &t->p->literal_kinds[ast_index_find(&t->p->frontend->ast_index, n)],
            kind(t->p, ((AstPropertyNode*)n)->value, t->facts));
    if (n->node_type == AST_NODE_FUNC || n->node_type == AST_NODE_FUNC_EXPR ||
            n->node_type == AST_NODE_ARROW_FUNC || n->node_type == AST_NODE_METHOD) {
        LmdFunction* f = function(t->p, n);
        LmdTypes inner = {t->p, false, f};
        union_kinds(&inner, &f->returns, f->ast->body->node_type == AST_NODE_BLOCK
            ? (falls_through(f->ast->body) ? K_UNDEFINED : 0) : kind(t->p, f->ast->body, t->facts));
        union_kinds(&inner, &f->array.lanes, f->ast->body->node_type == AST_NODE_BLOCK
            ? (falls_through(f->ast->body) ? LMD_ARRAY_UNKNOWN : 0) : array_facts(t->p, f->ast->body).lanes);
        js_ast_visit_children(n, type_walk, &inner);
        t->changed |= inner.changed;
        return;
    }
    if (n->node_type == AST_NODE_RETURN_STAM && t->owner) {
        union_kinds(t, &t->owner->returns, kind(t->p, ((AstReturnNode*)n)->value, t->facts));
        union_kinds(t, &t->owner->array.lanes, array_facts(t->p, ((AstReturnNode*)n)->value).lanes);
    }
    if (n->node_type == AST_NODE_CALL_EXPR && !t->facts) {
        AstCallNode* call = (AstCallNode*)n;
        LmdFunction* f = direct_target(t->p, call);
        if (f && f->closed_calls) {
            AstNode* actual = call->arguments;
            for (AstNode* formal = f->ast->params; formal; formal = formal->next) {
                LmdBinding* b = binding(t->p, js_ast_parameter_binding_identifier(formal)->entry);
                unsigned incoming = actual ? kind(t->p, actual, t->facts) : K_UNDEFINED;
                union_kinds(t, &b->kinds, incoming);
                union_kinds(t, &b->array.lanes, array_facts(t->p, actual).lanes);
                if (actual) actual = actual->next;
            }
        }
    }
    AstNode* target = NULL;
    unsigned mask = 0;
    if (n->node_type == AST_NODE_VARIABLE_DECLARATOR) {
        AstDeclaratorNode* d = (AstDeclaratorNode*)n;
        target = d->id;
        if (d->init) mask = kind(t->p, d->init, t->facts);
    } else if (n->node_type == AST_NODE_ASSIGN) {
        AstAssignNode* a = (AstAssignNode*)n;
        target = a->left;
        mask = kind(t->p, n, t->facts);
    } else if (n->node_type == AST_NODE_UNARY &&
            (((AstUnaryNode*)n)->op == OPERATOR_JS_INCREMENT ||
             ((AstUnaryNode*)n)->op == OPERATOR_JS_DECREMENT)) {
        target = ((AstUnaryNode*)n)->operand;
        mask = K_NUMBER;
    }
    if (target && target->node_type == AST_NODE_IDENT) {
        LmdBinding* b = identifier_binding(t->p, (AstIdentNode*)target);
        // bare lexical declarations initialize to undefined, including on every block re-entry.
        // intrinsic constants have no lexical declaration record, even when used as write targets.
        if (b && !b->intrinsic && n->node_type == AST_NODE_VARIABLE_DECLARATOR && b->entry->is_lexical &&
                !((AstDeclaratorNode*)n)->init) mask |= K_UNDEFINED;
        unsigned* destination = b && !t->facts ? &b->kinds : NULL;
        for (int i = 0; t->facts && i < t->facts->count; i++)
            if (t->facts->bindings[i] == b) destination = &t->facts->slots[i].kinds;
        if (destination && b && !b->intrinsic) union_kinds(t, destination, mask);
        if (!t->facts && b && !b->intrinsic) union_kinds(t, &b->array.lanes, array_facts(t->p,
            n->node_type == AST_NODE_VARIABLE_DECLARATOR ? ((AstDeclaratorNode*)n)->init : n).lanes);
    }
    if (target && (target->node_type == AST_NODE_MEMBER_EXPR || target->node_type == AST_NODE_INDEX_EXPR)) {
        LmdScalarField* field = scalar_field(t->p, target);
        if (field) union_kinds(t, &field->kinds, mask);
        if (!t->facts && !t->p->typed_storage) {
            AstFieldNode* member = (AstFieldNode*)target;
            String* spelling = member_spelling(member);
            unsigned key = spelling ? K_STRING : kind(t->p, member->property);
            // bottom keys wait for their solved domain; numeric stores coerce into the existing lane.
            bool named_key = spelling ? mvp_lmd_string_key(spelling, true) == -2 : key && key != K_NUMBER;
            if (named_key && (kind(t->p, member->object) & K_TYPED_ARRAY))
                union_kinds(t, &t->p->typed_named_kinds,
                    n->node_type == AST_NODE_UNARY ? kind(t->p, n, t->facts) : mask);
        }
    }
    if (!t->facts && (n->node_type == AST_NODE_MEMBER_EXPR || n->node_type == AST_NODE_INDEX_EXPR)) {
        AstFieldNode* member = (AstFieldNode*)n;
        AstNode* parent = ast_index_parent(&t->p->frontend->ast_index, n);
        bool direct_call = parent && parent->node_type == AST_NODE_CALL_EXPR && ((AstCallNode*)parent)->callee == n;
        if (!direct_call) {
            // conservatively include observed methods and destructuring targets in the shared write proof.
            unsigned receiver = kind(t->p, member->object);
            String* spelling = member_spelling(member);
            struct { unsigned kind; TypeId type; bool* changed; } owners[] = {
                {K_ARRAY, LMD_TYPE_ARRAY, &t->p->array_method_overrides},
                {K_MAP, LMD_TYPE_MAP, &t->p->map_method_overrides}};
            // primitive values cannot retain own-method writes; prototype reachability is checked by admission.
            for (const auto& owner : owners) if (!*owner.changed && (receiver & owner.kind)) {
                bool overrides = spelling ? mvp_lmd_builtin_method(spelling, owner.type) != 0 :
                    (kind(t->p, member->property) & ~(K_NUMBER | K_BOOL | K_NULL | K_UNDEFINED)) != 0;
                if (overrides) {
                    log_debug("js mvp method proof: type %u invalidated at byte %u", owner.type, n->source_span.start_byte);
                    *owner.changed = t->changed = true;
                }
            }
        }
    }
    js_ast_visit_children(n, type_walk, t);
}

// ranges describe finite JS integer values only; unknown includes fractional and signed-zero values.
static LmdRange finite_range(double lower, double upper) {
    return isfinite(lower) && isfinite(upper) && lower >= INT53_MIN && upper <= INT53_MAX
        ? LmdRange{lower, upper, 1} : LmdRange{0, 0, 2};
}
static LmdRange range_binary(Operator operation, LmdRange left, LmdRange right) {
    if (!left.state || !right.state) return {};
    if (left.state != 1 || right.state != 1) return {0, 0, 2};
    if (operation == OPERATOR_ADD) return finite_range(left.lower + right.lower, left.upper + right.upper);
    if (operation == OPERATOR_SUB) return finite_range(left.lower - right.upper, left.upper - right.lower);
    if (operation == OPERATOR_MUL) {
        if ((left.lower <= 0 && left.upper >= 0 && right.lower < 0) ||
                (right.lower <= 0 && right.upper >= 0 && left.lower < 0)) return {0, 0, 2};
        double products[] = {left.lower * right.lower, left.lower * right.upper,
            left.upper * right.lower, left.upper * right.upper};
        double lo = products[0], hi = lo;
        for (double product : products) { lo = fmin(lo, product); hi = fmax(hi, product); }
        return finite_range(lo, hi);
    }
    if (operation == OPERATOR_MOD && left.lower >= 0 && right.lower > 0)
        return finite_range(0, fmin(left.upper, right.upper - 1));
    return {0, 0, 2};
}
static LmdBinding* linear_index(MvpLmdProgram* p, AstNode* node, double* offset) {
    *offset = 0;
    if (node && node->node_type == AST_NODE_BINARY) {
        AstBinaryNode* shifted = (AstBinaryNode*)node;
        if ((shifted->op != OPERATOR_ADD && shifted->op != OPERATOR_SUB) ||
                shifted->right->node_type != AST_NODE_LITERAL ||
                ((AstLiteralNode*)shifted->right)->literal_type != AST_LITERAL_NUMBER) return NULL;
        *offset = ((AstLiteralNode*)shifted->right)->value.number_value;
        if (!finite_integer(*offset)) return NULL;
        if (shifted->op == OPERATOR_SUB) *offset = -*offset;
        node = shifted->left;
    }
    return node && node->node_type == AST_NODE_IDENT ? identifier_binding(p, (AstIdentNode*)node) : NULL;
}
static LmdRange range(MvpLmdProgram* p, AstNode* n, bool scoped, bool present_only) {
    if (!n) return {0, 0, 2};
    // absence contributes no payload; ordinary arithmetic must still see its unknown/NaN range.
    if (present_only && kind(p, n) == K_UNDEFINED) return {};
    switch (n->node_type) {
    case AST_NODE_MEMBER_EXPR: case AST_NODE_INDEX_EXPR: {
        AstFieldNode* member = (AstFieldNode*)n;
        unsigned owner = kind(p, member->object);
        String* spelling = member_spelling(member);
        if ((owner == K_ARRAY || owner == K_TYPED_ARRAY || owner == K_STRING) && named(spelling, "length")) {
            LmdArrayFacts facts = array_facts(p, member->object);
            return facts.length.state == 1 ? facts.length : finite_range(0, owner == K_TYPED_ARRAY ? INT53_MAX : UINT32_MAX);
        }
        LmdArrayFacts facts = array_facts(p, member->object);
        if (member->computed && facts.lanes && !(facts.lanes & LMD_ARRAY_UNKNOWN)) {
            // kind admission uses this site's bounds; retain the same proof for its integer payload.
            LmdRange index = range(p, member->property, true);
            if (!index.state || !facts.length.state) return {};
            unsigned key = kind(p, member->property);
            if (present_only && owner == K_TYPED_ARRAY && key == (K_NUMBER | K_UNDEFINED) &&
                    !(p->typed_named_kinds & ~(K_NUMBER | K_UNDEFINED)) && !(facts.lanes & ~3u)) {
                // an absent key observes ordinary attributes; join their payload range with indexed storage.
                LmdRange named = p->typed_named_range;
                if (named.state == 2) return named;
                double low = facts.lanes & 1 ? INT32_MIN : 0;
                double high = facts.lanes & 1 ? INT32_MAX : UINT8_MAX;
                return named.state ? finite_range(fmin(low, named.lower), fmax(high, named.upper)) : finite_range(low, high);
            }
            bool numeric_key = owner == K_TYPED_ARRAY ? key == K_NUMBER : !(key & ~(K_NUMBER | K_UNDEFINED));
            if ((present_only && numeric_key) || ((owner != K_TYPED_ARRAY || numeric_key) &&
                    index.state == 1 && facts.length.state == 1 && index.lower >= 0 && index.upper < facts.length.lower)) {
                // a numeric-array parameter may have no integer-content proof; that is unknown, not bottom.
                if (facts.lanes == LMD_ARRAY_ITEMS) return facts.elements.state ? facts.elements : LmdRange{0, 0, 2};
                if (!(facts.lanes & ~3u)) return finite_range(facts.lanes & 1 ? INT32_MIN : 0,
                    facts.lanes & 1 ? INT32_MAX : UINT8_MAX);
            }
        }
        AstNode* value = scalar_field_initializer(p, n);
        return value ? range(p, value) : LmdRange{0, 0, 2};
    }
    case AST_NODE_LITERAL: {
        AstLiteralNode* literal = (AstLiteralNode*)n;
        if (literal->literal_type != AST_LITERAL_NUMBER || literal->is_bigint) break;
        double value = literal->value.number_value;
        if (value != trunc(value) || (value == 0 && signbit(value))) break;
        return finite_range(value, value);
    }
    case AST_NODE_IDENT: {
        LmdBinding* b = identifier_binding(p, (AstIdentNode*)n);
        // a successful const read has passed TDZ even when a closed function reads a module constant.
        bool immutable = b && b->entry && b->entry->is_const && b->writes == 1;
        if (!b || b->intrinsic || ((!immutable && b->external) ||
                !(immutable || b->dominated || b->entry->is_parameter))) return {0, 0, 2};
        LmdRange result = present_only && b->kinds == (K_NUMBER | K_UNDEFINED) ? b->present_range : b->range;
        if (!scoped || result.state != 1 || b->writes != 2) return result;
        AstNode* child = n;
        for (AstNode* parent = ast_index_parent(&p->frontend->ast_index, child); parent;
                child = parent, parent = ast_index_parent(&p->frontend->ast_index, parent)) {
            AstNode* condition = NULL;
            bool negate = false;
            if (parent->node_type == AST_NODE_LOOP) {
                AstLoopControlNode* loop = (AstLoopControlNode*)parent;
                if (loop->form == LOOP_FORM_DO_WHILE || loop->body != child) continue;
                // a nested loop can revisit the read after writing, before this condition is retested.
                if (loop->update != b->update && (!b->update ||
                        enclosing_loop(p, b->update, b->owner, true) != parent ||
                        ast_index_find(&p->frontend->ast_index, n) >=
                        ast_index_find(&p->frontend->ast_index, b->update))) continue;
                condition = loop->cond;
            } else if (parent->node_type == AST_NODE_IF_EXPR) {
                AstIfNode* branch = (AstIfNode*)parent;
                if (child != branch->then && child != branch->otherwise) continue;
                AstIndex* index = &p->frontend->ast_index;
                AstNodeId guard = ast_index_find(index, parent);
                // the counted binding's only two writes must both lie outside this guard.
                if (!b->initializer || !b->update ||
                        ast_index_node_descends(index, ast_index_find(index, b->initializer), guard) ||
                        ast_index_node_descends(index, ast_index_find(index, b->update), guard)) continue;
                condition = branch->cond;
                negate = child == branch->otherwise;
            } else continue;
            if (!condition || condition->node_type != AST_NODE_BINARY) continue;
            AstBinaryNode* cond = (AstBinaryNode*)condition;
            Operator comparison = cond->op;
            if (negate) {
                if (comparison == OPERATOR_LT) comparison = OPERATOR_GE;
                else if (comparison == OPERATOR_LE) comparison = OPERATOR_GT;
                else if (comparison == OPERATOR_GT) comparison = OPERATOR_LE;
                else if (comparison == OPERATOR_GE) comparison = OPERATOR_LT;
                else continue;
            }
            double offset;
            if (linear_index(p, cond->left, &offset) != b ||
                    finite_range(result.lower + offset, result.upper + offset).state != 1) continue;
            LmdRange limit = range(p, cond->right);
            if (limit.state != 1) continue;
            limit = finite_range(limit.lower - offset, limit.upper - offset);
            if (limit.state != 1) continue;
            double lo = result.lower, hi = result.upper;
            if (comparison == OPERATOR_LT || comparison == OPERATOR_LE)
                hi = fmin(hi, limit.upper - (comparison == OPERATOR_LT ? 1 : 0));
            if (comparison == OPERATOR_GT || comparison == OPERATOR_GE)
                lo = fmax(lo, limit.lower + (comparison == OPERATOR_GT ? 1 : 0));
            if (lo <= hi) result = finite_range(lo, hi);
        }
        return result;
    }
    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)n;
        int operation = math_operation(p, call);
        if (operation >= 0 && !strcmp(math_operations[operation].name, "floor") &&
                call->arguments && !call->arguments->next && !call->interp_has_spread_args) {
            AstNode* argument = call->arguments;
            LmdRange value = range(p, argument, scoped);
            if (value.state == 1) return value;
            if (argument->node_type == AST_NODE_BINARY && ((AstBinaryNode*)argument)->op == OPERATOR_DIV) {
                AstBinaryNode* quotient = (AstBinaryNode*)argument;
                LmdRange left = range(p, quotient->left, scoped), right = range(p, quotient->right, scoped);
                // positive division is monotone and cannot produce negative zero.
                if (left.state == 1 && right.state == 1 && left.lower >= 0 && right.lower > 0)
                    return finite_range(floor(left.lower / right.upper), floor(left.upper / right.lower));
            }
        }
        LmdFunction* f = direct_target(p, (AstCallNode*)n);
        // explicit numeric returns do not exclude an implicit undefined return.
        return f && f->returns == K_NUMBER ? f->range : LmdRange{0, 0, 2};
    }
    case AST_NODE_ASSIGN: {
        AstAssignNode* a = (AstAssignNode*)n;
        return a->op == OPERATOR_ASSIGN ? range(p, a->right, scoped, present_only) :
            range_binary(compound_operator(a->op), range(p, a->left, scoped), range(p, a->right, scoped));
    }
    case AST_NODE_BINARY: {
        AstBinaryNode* b = (AstBinaryNode*)n;
        if (b->op >= OPERATOR_JS_BIT_AND && b->op <= OPERATOR_JS_URSHIFT)
            return finite_range(b->op == OPERATOR_JS_URSHIFT ? 0 : INT32_MIN,
                b->op == OPERATOR_JS_URSHIFT ? UINT32_MAX : INT32_MAX);
        return range_binary(b->op, range(p, b->left, scoped), range(p, b->right, scoped));
    }
    case AST_NODE_UNARY: {
        AstUnaryNode* u = (AstUnaryNode*)n;
        LmdRange r = range(p, u->operand, scoped);
        if (u->op == OPERATOR_POS) return r;
        if (u->op == OPERATOR_JS_BIT_NOT) return finite_range(INT32_MIN, INT32_MAX);
        if (u->op == OPERATOR_NEG && r.state == 1 && (r.upper < 0 || r.lower > 0))
            return finite_range(-r.upper, -r.lower);
        if (u->op == OPERATOR_JS_INCREMENT || u->op == OPERATOR_JS_DECREMENT) {
            LmdRange after = range_binary(u->op == OPERATOR_JS_INCREMENT ? OPERATOR_ADD : OPERATOR_SUB, r, {1, 1, 1});
            // postfix consumers see the snapshot; include the write for the binding fixed point too.
            if (!u->prefix && r.state == 1 && after.state == 1)
                return finite_range(fmin(r.lower, after.lower), fmax(r.upper, after.upper));
            return after;
        }
        break;
    }
    default: break;
    }
    return {0, 0, 2};
}
static bool same_binding(MvpLmdProgram* p, AstNode* n, LmdBinding* b) {
    return n && n->node_type == AST_NODE_IDENT && identifier_binding(p, (AstIdentNode*)n) == b;
}
static LmdRange update_delta(MvpLmdProgram* p, LmdBinding* b) {
    AstNode* n = b->update;
    if (!n) return {0, 0, 2};
    if (n->node_type == AST_NODE_UNARY) {
        double step = ((AstUnaryNode*)n)->op == OPERATOR_JS_INCREMENT ? 1 : -1;
        return {step, step, 1};
    }
    AstAssignNode* a = (AstAssignNode*)n;
    Operator operation = a->op == OPERATOR_ASSIGN ? OPERATOR_ASSIGN : compound_operator(a->op);
    AstNode* rhs = a->right;
    if (operation == OPERATOR_ASSIGN && rhs->node_type == AST_NODE_BINARY) {
        AstBinaryNode* binary = (AstBinaryNode*)rhs;
        if (!same_binding(p, binary->left, b)) return {0, 0, 2};
        operation = binary->op; rhs = binary->right;
    }
    LmdRange delta = range(p, rhs);
    if (operation == OPERATOR_SUB && delta.state == 1) return {-delta.upper, -delta.lower, 1};
    return operation == OPERATOR_ADD ? delta : LmdRange{0, 0, 2};
}
static bool loop_linear(MvpLmdProgram* p, AstNode* n) {
    if (!n) return true;
    if (n->node_type == AST_NODE_LOOP || n->node_type == AST_NODE_BREAK_STAM ||
            n->node_type == AST_NODE_CONTINUE_STAM || n->node_type == AST_NODE_FUNC ||
            n->node_type == AST_NODE_FUNC_EXPR || n->node_type == AST_NODE_ARROW_FUNC) return false;
    struct Check { MvpLmdProgram* p; bool ok; } check = {p, true};
    js_ast_visit_children(n, [](AstNode* child, void* opaque) {
        Check* c = (Check*)opaque; c->ok &= loop_linear(c->p, child);
    }, &check);
    return check.ok;
}
static AstNode* enclosing_loop(MvpLmdProgram* p, AstNode* node, LmdFunction* owner, bool include_for_of) {
    for (AstNode* parent = ast_index_parent(&p->frontend->ast_index, node);
            parent && parent != (AstNode*)owner->ast;
            parent = ast_index_parent(&p->frontend->ast_index, parent))
        if (parent->node_type == AST_NODE_LOOP ||
                (include_for_of && parent->node_type == AST_NODE_FOR_OF_STAM)) return parent;
    return NULL;
}
static bool loop_reset_before(MvpLmdProgram* p, LmdBinding* binding, AstLoopControlNode* loop) {
    if (!binding->initializer) return false;
    AstIndex* index = &p->frontend->ast_index;
    AstNode* declaration = ast_index_parent(index, binding->initializer);
    if (!declaration || declaration->node_type != AST_NODE_VARIABLE_DECLARATOR) return false;
    AstNode* statement = ast_index_parent(index, declaration);
    if (statement == loop->init) return true;
    AstNode* block = ast_index_parent(index, (AstNode*)loop);
    // a direct preceding declaration executes on every entry to the containing block.
    return block && block->node_type == AST_NODE_BLOCK && statement &&
        ast_index_parent(index, statement) == block &&
        ast_index_find(index, statement) < ast_index_find(index, (AstNode*)loop);
}
// a unique additive write in a counted loop has a finite activation-wide bound.
static LmdRange counted_range(MvpLmdProgram* p, LmdBinding* b) {
    if (b->writes != 2 || !b->initializer || !b->update || b->external) return {0, 0, 2};
    AstNode* ancestor = enclosing_loop(p, b->update, b->owner);
    if (!ancestor) return {0, 0, 2};
    AstLoopControlNode* loop = (AstLoopControlNode*)ancestor;
    if (loop->form == LOOP_FORM_DO_WHILE) return {0, 0, 2};
    // condition writes also execute on the final failed test, outside the body trip count.
    AstNode* location = b->update;
    while (location && location != loop->body && location != ancestor)
        location = ast_index_parent(&p->frontend->ast_index, location);
    if (location != loop->body && loop->update != b->update) return {0, 0, 2};
    if (!loop->cond || loop->cond->node_type != AST_NODE_BINARY) return {0, 0, 2};
    AstBinaryNode* cond = (AstBinaryNode*)loop->cond;
    AstNode* index_node = cond->left;
    bool square = index_node && index_node->node_type == AST_NODE_BINARY &&
        ((AstBinaryNode*)index_node)->op == OPERATOR_MUL;
    if (square) {
        AstBinaryNode* product = (AstBinaryNode*)index_node;
        if (product->left->node_type != AST_NODE_IDENT || product->right->node_type != AST_NODE_IDENT ||
                ((AstIdentNode*)product->left)->entry != ((AstIdentNode*)product->right)->entry)
            return {0, 0, 2};
        index_node = product->left;
    }
    double offset;
    LmdBinding* index = linear_index(p, index_node, &offset);
    if (!index || index->writes != 2 || !index->initializer || !index->update || index->external) return {0, 0, 2};
    bool reset = loop_reset_before(p, index, loop) && (b == index || loop_reset_before(p, b, loop));
    if ((!reset && enclosing_loop(p, ancestor, b->owner)) ||
            ((!reset || b != index) && !loop_linear(p, loop->body))) return {0, 0, 2};
    AstNode* write = ast_index_parent(&p->frontend->ast_index, index->update);
    if (loop->update != index->update && (!write || write->node_type != AST_NODE_EXPR_STMT ||
            ast_index_parent(&p->frontend->ast_index, write) != loop->body)) return {0, 0, 2};
    LmdRange initial = range(p, index->initializer), limit = range(p, cond->right);
    LmdRange step = update_delta(p, index), start = range(p, b->initializer), delta = update_delta(p, b);
    if (!square && cond->right->node_type == AST_NODE_IDENT) {
        LmdBinding* bound = identifier_binding(p, (AstIdentNode*)cond->right);
        if (bound && bound != index && !bound->external && bound->writes == 2 && bound->update &&
                loop_reset_before(p, bound, loop) && enclosing_loop(p, bound->update, bound->owner) == ancestor) {
            LmdRange change = update_delta(p, bound);
            // an inward-moving opposite cursor cannot extend the induction variable's trip count.
            if (change.state == 1 &&
                    (((cond->op == OPERATOR_LT || cond->op == OPERATOR_LE) && change.upper <= 0) ||
                     ((cond->op == OPERATOR_GT || cond->op == OPERATOR_GE) && change.lower >= 0)))
                limit = range(p, bound->initializer);
        }
    }
    if (!initial.state || !limit.state || !step.state || !start.state || !delta.state) return {};
    if (initial.state != 1 || limit.state != 1 || step.state != 1 || start.state != 1 || delta.state != 1)
        return {0, 0, 2};
    if (offset) {
        // normalize affine tests only while both the shifted bound and arithmetic remain exact.
        limit = finite_range(limit.lower - offset, limit.upper - offset);
        if (limit.state != 1) return {0, 0, 2};
    }
    if (square) {
        if (step.lower <= 0 || limit.upper < 0 ||
                (cond->op != OPERATOR_LT && cond->op != OPERATOR_LE)) return {0, 0, 2};
        // a loose square-root bound includes JS multiplication rounding and the final update.
        limit = finite_range(0, floor(sqrt(limit.upper)) + 1);
    }
    int64_t distance, stride;
    if (step.lower > 0 && (cond->op == OPERATOR_LT || cond->op == OPERATOR_LE))
        { distance = (int64_t)limit.upper - (int64_t)initial.lower; stride = (int64_t)step.lower; }
    else if (step.upper < 0 && (cond->op == OPERATOR_GT || cond->op == OPERATOR_GE))
        { distance = (int64_t)initial.upper - (int64_t)limit.lower; stride = -(int64_t)step.upper; }
    else return {0, 0, 2};
    // differences fit i64; divide before multiplying so even int53 endpoint proofs stay exact.
    int64_t count = distance < 0 ? 0 : distance / stride + 1;
    int64_t lo = (int64_t)start.lower, hi = (int64_t)start.upper;
    int64_t down = delta.lower < 0 ? -(int64_t)delta.lower : 0;
    int64_t up = delta.upper > 0 ? (int64_t)delta.upper : 0;
    if ((down && count > (lo - INT53_MIN) / down) ||
            (up && count > (INT53_MAX - hi) / up)) return {0, 0, 2};
    LmdRange result = finite_range((double)(lo - count * down), (double)(hi + count * up));
    if (offset) {
        double index_lo = initial.lower + (step.lower < 0 ? count * step.lower : 0);
        double index_hi = initial.upper + (step.upper > 0 ? count * step.upper : 0);
        if (finite_range(index_lo + offset, index_hi + offset).state != 1) return {0, 0, 2};
    }
    return result;
}
// integral inputs and additive steps cannot create a finite fractional value.
static bool integer_binding_writes(MvpLmdProgram* p, LmdBinding* binding, bool positive_only, double* lower) {
    // unrelated dynamic source cannot write an unexposed local; direct eval
    // and captured cells retain their complete environment handling.
    if (!binding || binding->owner->eval_environment || !binding->native || binding->external || binding->captured ||
            binding->with_lookup || !binding->owner->id || !binding->initializer || binding->entry->is_parameter ||
            !binding->dominated || (binding->kinds & ~(K_NUMBER | K_UNDEFINED))) return false;
    *lower = INFINITY;
    unsigned writes = 0;
    const AstIndex* tree = &p->frontend->ast_index;
    for (AstNodeId i = 0; i < tree->count; i++) {
        AstNode* node = tree->nodes[i];
        AstNode* target = node->node_type == AST_NODE_VARIABLE_DECLARATOR ? ((AstDeclaratorNode*)node)->id :
            node->node_type == AST_NODE_ASSIGN ? ((AstAssignNode*)node)->left :
            node->node_type == AST_NODE_UNARY && (((AstUnaryNode*)node)->op == OPERATOR_JS_INCREMENT ||
                ((AstUnaryNode*)node)->op == OPERATOR_JS_DECREMENT) ? ((AstUnaryNode*)node)->operand : NULL;
        if (!target || target->node_type != AST_NODE_IDENT ||
                identifier_binding(p, (AstIdentNode*)target) != binding) continue;
        AstNode* value = node->node_type == AST_NODE_VARIABLE_DECLARATOR ? ((AstDeclaratorNode*)node)->init :
            node->node_type == AST_NODE_ASSIGN ? ((AstAssignNode*)node)->right : NULL;
        if (node->node_type == AST_NODE_VARIABLE_DECLARATOR && !value) continue;
        writes++;
        if (node->node_type == AST_NODE_UNARY) {
            if (positive_only && ((AstUnaryNode*)node)->op != OPERATOR_JS_INCREMENT) return false;
            continue;
        }
        LmdRange incoming = range(p, value, false, true);
        if (incoming.state != 1) return false;
        if (node->node_type == AST_NODE_ASSIGN && ((AstAssignNode*)node)->op != OPERATOR_ASSIGN) {
            Operator operation = compound_operator(((AstAssignNode*)node)->op);
            if (positive_only ? operation != OPERATOR_ADD || incoming.lower < 0 :
                    operation != OPERATOR_ADD && operation != OPERATOR_SUB) return false;
        } else *lower = fmin(*lower, incoming.lower);
    }
    // require the complete write inventory, including any forms this proof does not understand.
    return writes == binding->writes && isfinite(*lower);
}
static LmdRange ordered_integer_range(MvpLmdProgram* p, LmdBinding* binding, AstBinaryNode* condition) {
    if (condition->left->node_type != AST_NODE_IDENT ||
            identifier_binding(p, (AstIdentNode*)condition->left) != binding ||
            (condition->op != OPERATOR_LT && condition->op != OPERATOR_LE)) return {0, 0, 2};
    LmdRange limit = range(p, condition->right, false, true);
    double lower;
    if (limit.state != 1 || !integer_binding_writes(p, binding, true, &lower)) return {0, 0, 2};
    // the guard excludes undefined/NaN and positive overflow; positive steps preserve the seed's lower bound.
    double upper = limit.upper - (condition->op == OPERATOR_LT ? 1 : 0);
    if (lower > upper) return {0, 0, 2};
    return finite_range(lower, upper);
}
struct LmdRanges { MvpLmdProgram* p; LmdFunction* owner; bool changed; };
static void join_range(LmdRanges* t, LmdRange* into, unsigned* changes, LmdRange incoming) {
    if (!incoming.state || into->state == 2) return;
    if (into->state == 1 && incoming.state == 1)
        incoming = {fmin(into->lower, incoming.lower), fmax(into->upper, incoming.upper), 1};
    if (into->state == incoming.state && into->lower == incoming.lower && into->upper == incoming.upper) return;
    // cyclic growth without a counted-loop proof widens, rather than assuming a benchmark bound.
    if (++*changes > 16) incoming = {0, 0, 2};
    *into = incoming; t->changed = true;
}
static void range_walk(AstNode* n, void* opaque) {
    if (!n) return;
    LmdRanges* t = (LmdRanges*)opaque;
    MvpLmdProgram* p = t->p;
    visit_destructured_assignments(n, range_walk, opaque);
    if (n->node_type == AST_NODE_FUNC || n->node_type == AST_NODE_FUNC_EXPR || n->node_type == AST_NODE_ARROW_FUNC || n->node_type == AST_NODE_METHOD) {
        LmdFunction* f = function(p, n); LmdRanges inner = {p, f, false};
        if (f->ast->body->node_type != AST_NODE_BLOCK)
            join_range(&inner, &f->range, &f->range_changes, range(p, f->ast->body));
        if (f->ast->body->node_type != AST_NODE_BLOCK)
            join_range(&inner, &f->array.length, &f->array_changes, array_facts(p, f->ast->body).length);
        js_ast_visit_children(n, range_walk, &inner); t->changed |= inner.changed; return;
    }
    if (n->node_type == AST_NODE_RETURN_STAM && t->owner) {
        join_range(t, &t->owner->range, &t->owner->range_changes, range(p, ((AstReturnNode*)n)->value));
        join_range(t, &t->owner->array.length, &t->owner->array_changes, array_facts(p, ((AstReturnNode*)n)->value).length);
    }
    if (n->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)n; LmdFunction* f = direct_target(p, call);
        if (f && f->closed_calls) {
            AstNode* actual = call->arguments;
            for (AstNode* formal = f->ast->params; formal; formal = formal->next) {
                LmdBinding* b = binding(p, js_ast_parameter_binding_identifier(formal)->entry);
                join_range(t, &b->range, &b->range_changes, range(p, actual));
                join_range(t, &b->array.length, &b->array_changes, array_facts(p, actual).length);
                if (actual) actual = actual->next;
            }
        }
    }
    AstNode* target = n->node_type == AST_NODE_VARIABLE_DECLARATOR ? ((AstDeclaratorNode*)n)->id :
        n->node_type == AST_NODE_ASSIGN ? ((AstAssignNode*)n)->left :
        n->node_type == AST_NODE_UNARY && (((AstUnaryNode*)n)->op == OPERATOR_JS_INCREMENT ||
            ((AstUnaryNode*)n)->op == OPERATOR_JS_DECREMENT) ? ((AstUnaryNode*)n)->operand : NULL;
    if (target && !p->typed_storage && (target->node_type == AST_NODE_MEMBER_EXPR || target->node_type == AST_NODE_INDEX_EXPR)) {
        AstFieldNode* member = (AstFieldNode*)target;
        String* spelling = member_spelling(member);
        unsigned key = spelling ? K_STRING : kind(p, member->property);
        bool named_key = spelling ? mvp_lmd_string_key(spelling, true) == -2 : key && key != K_NUMBER;
        if (named_key && (kind(p, member->object) & K_TYPED_ARRAY)) {
            AstNode* value = n->node_type == AST_NODE_ASSIGN && ((AstAssignNode*)n)->op == OPERATOR_ASSIGN
                ? ((AstAssignNode*)n)->right : n;
            join_range(t, &p->typed_named_range, &p->typed_named_range_changes, range(p, value, false, true));
        }
    }
    if (target && target->node_type == AST_NODE_IDENT) {
        LmdBinding* b = identifier_binding(p, (AstIdentNode*)target);
        if (b && !b->intrinsic) {
            AstNode* value = n->node_type == AST_NODE_VARIABLE_DECLARATOR ? ((AstDeclaratorNode*)n)->init : n;
            LmdRange incoming = b->update == n ? counted_range(p, b) : range(p, value);
            if (b->update == n && incoming.state == 2) incoming = range(p, value);
            join_range(t, &b->range, &b->range_changes, incoming);
            if (b->kinds == (K_NUMBER | K_UNDEFINED))
                join_range(t, &b->present_range, &b->present_range_changes, range(p, value, false, true));
            join_range(t, &b->array.length, &b->array_changes, array_facts(p, value).length);
        }
    }
    js_ast_visit_children(n, range_walk, t);
}

static MIR_op_t reg(LmdCompiler* c, MIR_reg_t r) { return MIR_new_reg_op(c->em.ctx, r); }
static MIR_op_t integer(LmdCompiler* c, uint64_t v) { return MIR_new_uint_op(c->em.ctx, v); }
static MIR_label_t label(LmdCompiler* c) { return em_new_label(&c->em); }
static void put_label(LmdCompiler* c, MIR_label_t l) { em_emit_label(&c->em, l); }
static void jump(LmdCompiler* c, MIR_label_t l) {
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_JMP, MIR_new_label_op(c->em.ctx, l)));
}
static void move(LmdCompiler* c, MIR_reg_t dst, MIR_op_t value, bool number = false) {
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, number ? MIR_DMOV : MIR_MOV, reg(c, dst), value));
}
static MIR_reg_t constant(LmdCompiler* c, uint64_t value) {
    MIR_reg_t r = em_new_reg(&c->em, "constant", MIR_T_I64); move(c, r, integer(c, value)); return r;
}
static MIR_reg_t relocated_constant(LmdCompiler* c, uint64_t value) {
    if (!c->program->relocatable) return constant(c, value);
    ArrayList* sites = c->program->relocations;
    int index = sites->length;
    // one slot per emitted operand keeps the layout independent of pointer
    // identity, interning, and allocator reuse in another realm (D8.5.1v2).
    if (!arraylist_append(sites, (void*)value)) {
        diagnostic(c->program, NULL, "relocation allocation failed"); return constant(c, 0);
    }
    MIR_reg_t table = em_load_at(&c->em, c->unit, offsetof(MvpLmdProgram, relocations), MIR_T_I64, "relocations");
    MIR_reg_t data = em_load_at(&c->em, table, offsetof(ArrayList, data), MIR_T_I64, "relocation_data");
    return em_load_at(&c->em, data, index * sizeof(void*), MIR_T_I64, "relocated_constant");
}
static MIR_op_t relocated(LmdCompiler* c, uint64_t value) {
    return c->program->relocatable ? reg(c, relocated_constant(c, value)) : integer(c, value);
}
static bool finite_integer(double value) {
    return isfinite(value) && value >= INT53_MIN && value <= INT53_MAX &&
        value == trunc(value) && !(value == 0 && signbit(value));
}
static LmdValue number(LmdCompiler* c, double value) {
    bool integral = finite_integer(value);
    // inlined arithmetic retains its callee's numeric region, independently of the caller.
    LmdFunction* owner = c->inlining ? c->inlining->function : c->function;
    bool native_int = integral && (c->integer_bail || !owner->native || owner->integer_region);
    MIR_reg_t r = em_new_reg(&c->em, "number", native_int ? MIR_T_I64 : MIR_T_D);
    move(c, r, native_int ? MIR_new_int_op(c->em.ctx, (int64_t)value) :
        MIR_new_double_op(c->em.ctx, value), !native_int);
    return {r, K_NUMBER, true, value, native_int, integral ? finite_range(value, value) : LmdRange{0, 0, 2}};
}
static MIR_reg_t op(LmdCompiler* c, MIR_insn_code_t code, MIR_op_t a, MIR_op_t b,
        MIR_type_t type = MIR_T_I64) {
    MIR_reg_t r = em_new_reg(&c->em, "value", type);
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, code, reg(c, r), a, b)); return r;
}
static void branch(LmdCompiler* c, MIR_insn_code_t code, MIR_label_t l,
        MIR_op_t a, MIR_op_t b) {
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, code, MIR_new_label_op(c->em.ctx, l), a, b));
}
static void branch_truth(LmdCompiler* c, MIR_label_t l, MIR_reg_t r, bool yes = true) {
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, yes ? MIR_BT : MIR_BF,
        MIR_new_label_op(c->em.ctx, l), reg(c, r)));
}
static void root_value(void* owner, MIR_reg_t r, JitValueClass cls) {
    LmdCompiler* c = (LmdCompiler*)owner;
    MirFrameState* f = &c->em.frame;
    if (!em_root_note_candidate(&f->gc_candidates, &f->gc_candidate_count,
            &f->gc_candidate_capacity, &f->gc_candidate_by_reg,
            &f->gc_candidate_by_reg_capacity, r, cls, 0)) abort();
}
static LmdValue boxed(LmdCompiler* c, MIR_reg_t r, unsigned hint = K_ANY) {
    root_value(c, r, JIT_VALUE_BOXED_ITEM); return {r, hint | K_BOXED};
}
static bool is_boxed(LmdValue v) { return v.kind & K_BOXED; }
static unsigned semantic(LmdValue v) { return v.kind & K_ANY; }
static LmdValue expression(LmdCompiler* c, AstNode* n, bool borrow_scalar = false, bool numeric = false, bool chain_continuation = false);
static bool constructor_layout_reusable(MvpLmdProgram*, AstNode*, bool value = false,
    bool construction = false, int depth = 0);
static LmdValue box(LmdCompiler* c, LmdValue value);
static MIR_reg_t to_number(LmdCompiler* c, LmdValue value);
static MIR_reg_t truth(LmdCompiler* c, LmdValue value);
static MIR_reg_t tag(LmdCompiler* c, LmdValue value);
static MIR_reg_t guard_property_nominal(LmdCompiler* c, MIR_reg_t shape, MIR_label_t miss,
        bool allow_plain = false) {
    TypeMap marker = {};
    MIR_reg_t nominal = em_load_at(&c->em, shape,
        (char*)&marker.nominal - (char*)&marker, MIR_T_I64, "own_nominal");
    branch(c, MIR_BEQ, miss, reg(c, nominal), integer(c, 0));
    MIR_reg_t extension = em_load_at(&c->em, nominal,
        offsetof(TypeNominal, extension), MIR_T_I64, "own_extension");
    MIR_label_t owned = allow_plain ? label(c) : NULL;
    if (owned) branch(c, MIR_BEQ, owned, reg(c, extension),
        relocated(c, (uint64_t)&mvp_lmd_plain_extension));
    branch(c, MIR_BNE, miss, reg(c, extension), relocated(c, (uint64_t)&mvp_lmd_class_extension));
    if (owned) put_label(c, owned);
    return nominal;
}
// ordinary function instances share the same carrier and nominal proof in both read paths.
static MIR_reg_t guard_function_instance(LmdCompiler* c, MIR_reg_t owner, MIR_label_t miss,
        MIR_reg_t* constructor = NULL) {
    em_guard_container(&c->em, owner, LMD_TYPE_MAP, miss);
    branch(c, MIR_BNE, miss, reg(c, em_load_at(&c->em, owner,
        LAMBDA_GC_OFF_CONTAINER_MAP_KIND, MIR_T_U8, "own_map_kind")), integer(c, MAP_KIND_PLAIN));
    MIR_reg_t shape = em_load_at(&c->em, owner, LAMBDA_GC_OFF_MAP_TYPE, MIR_T_I64, "own_shape");
    branch(c, MIR_BEQ, miss, reg(c, shape), integer(c, 0));
    TypeMap marker = {};
    branch(c, MIR_BNE, miss, reg(c, em_load_at(&c->em, shape,
        (char*)&marker.js_meta - (char*)&marker, MIR_T_I64, "own_js_meta")), integer(c, 0));
    MIR_reg_t nominal = guard_property_nominal(c, shape, miss);
    MIR_reg_t cls = em_load_at(&c->em, nominal,
        offsetof(TypeNominal, extension_data), MIR_T_I64, "own_class");
    branch(c, MIR_BEQ, miss, reg(c, cls), integer(c, 0));
    MvpLmdClass class_marker = {};
    MIR_reg_t function = em_load_at(&c->em, cls,
        (char*)&class_marker.owner - (char*)&class_marker, MIR_T_I64, "own_constructor");
    branch(c, MIR_BEQ, miss, reg(c, function), integer(c, 0));
    if (constructor) *constructor = function;
    return shape;
}
static MIR_reg_t property_access(LmdCompiler* c, int operation, MIR_reg_t owner,
    MIR_reg_t name = 0, MIR_reg_t value = 0, String* spelling = NULL, bool numeric = false,
    const LmdValue* native_value = NULL, bool plain = false, unsigned owner_kind = K_ANY,
    bool computed_own = false);
static void branch_condition(LmdCompiler* c, AstNode* node, MIR_label_t target, bool yes = true);
static void statement(LmdCompiler* c, AstNode* n);
static LmdValue class_expression(LmdCompiler* c, AstClassNode* ast, MIR_reg_t inferred_name = 0);
static LmdObjectPlan* scalar_constructor_plan(MvpLmdProgram* p, AstNode* node,
    const LmdValue* native_values = NULL);
static MIR_reg_t scalar_reg(LmdCompiler* c, LmdValue value, unsigned kind, bool integral = false) {
    if (integral) {
        bool optional = kind == (K_NUMBER | K_UNDEFINED);
        if (value.integer && (optional || !value.number_present)) return value.reg;
        MIR_reg_t result = em_new_reg(&c->em, "proven_integer", MIR_T_I64);
        MIR_label_t absent = optional ? label(c) : NULL;
        if (optional) {
            move(c, result, integer(c, 0));
            if (value.number_present) branch_truth(c, absent, value.number_present, false);
            else branch(c, MIR_BEQ, absent, reg(c, tag(c, value)), integer(c, LMD_TYPE_UNDEFINED));
            value.number_present = 0; value.kind = K_NUMBER | (value.kind & K_BOXED);
        }
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_D2I, reg(c, result), reg(c, to_number(c, value))));
        if (absent) put_label(c, absent);
        return result;
    }
    return (kind & K_NUMBER) ? to_number(c, value) : truth(c, value);
}
static bool materialize_integer_operand(LmdCompiler* c, LmdValue* value) {
    // a finite integer proof belongs at the consumer, without changing the binding's arithmetic lane.
    if (!value->integer && !is_boxed(*value) && !value->number_present &&
            semantic(*value) == K_NUMBER && value->range.state == 1) {
        value->reg = scalar_reg(c, *value, K_NUMBER, true);
        value->integer = true;
    }
    return value->integer && !value->number_present;
}
static void check_error(LmdCompiler* c, MIR_reg_t result);
static MIR_reg_t emit_await(LmdCompiler* c, MIR_reg_t value) {
    MIR_reg_t resumed;
    if (c->function->ast && c->function->ast->is_generator)
        resumed = em_call_2(&c->em, "js_gen_park", MIR_T_I64, MIR_T_I64, reg(c, value),
            MIR_T_I64, integer(c, JS_GEN_PARK_AWAIT), true);
    else resumed = em_call_1(&c->em, "js_await_park", MIR_T_I64, MIR_T_I64, reg(c, value), true);
    // a shared Promise may have been fulfilled by a native Lambda procedure.
    return em_call_3(&c->em, "mvp_lmd_dynamic_import_value", MIR_T_I64,
        MIR_T_P, reg(c, c->unit), MIR_T_I64, reg(c, resumed), MIR_T_I64, integer(c, 0), true);
}
static void release_iterators(LmdCompiler* c, LmdControl* through = NULL, MIR_reg_t error = 0) {
    for (LmdControl* control = c->control; control && control != through; control = control->parent) {
        if (!control->iteration_active) continue;
        MIR_label_t done = label(c);
        branch_truth(c, done, control->iteration_active, false);
        move(c, control->iteration_active, integer(c, 0));
        if (control->iterator_record) {
            // the pending completion must survive user code invoked by IteratorClose.
            if (error) boxed(c, error);
            MIR_label_t native = label(c);
            branch(c, MIR_BEQ, native, reg(c, control->iterator_record), integer(c, ITEM_NULL));
            MIR_reg_t closed = control->async_iterator
                ? em_call_3(&c->em, "mvp_lmd_iterator_close_mode", MIR_T_I64,
                    MIR_T_I64, reg(c, control->iterator_record), MIR_T_I64,
                    error ? reg(c, error) : integer(c, ITEM_JS_UNDEFINED), MIR_T_I64, integer(c, 1), true)
                : em_call_2(&c->em, "mvp_lmd_iterator_close", MIR_T_I64,
                    MIR_T_I64, reg(c, control->iterator_record), MIR_T_I64,
                    error ? reg(c, error) : integer(c, ITEM_JS_UNDEFINED), true);
            if (!error) {
                // Close belongs outside the exited loop, including its body-local catches.
                LmdControl* saved_control = c->control; LmdCompletion* saved_completion = c->completion;
                c->control = control->parent;
                while (c->completion) {
                    bool inside = false;
                    for (LmdControl* owner = c->completion->control; owner; owner = owner->parent)
                        if (owner == control) { inside = true; break; }
                    if (!inside) break;
                    c->completion = c->completion->parent;
                }
                check_error(c, closed); c->control = saved_control; c->completion = saved_completion;
            }
            jump(c, done); put_label(c, native);
        }
        MIR_reg_t count = em_load_at(&c->em, control->iteration_owner,
            offsetof(LambdaGcOrderedMapLayout, cursors), MIR_T_I64, "active_cursors");
        em_store_at(&c->em, control->iteration_owner, offsetof(LambdaGcOrderedMapLayout, cursors), MIR_T_I64,
            op(c, MIR_SUB, reg(c, count), integer(c, 1)));
        move(c, control->iteration_active, integer(c, 0)); put_label(c, done);
    }
}
static void return_error(LmdCompiler* c, MIR_reg_t error) {
    // errors crossing the same lexical iterator share one cold close block.
    LmdControl* through = c->completion ? c->completion->control : NULL;
    for (LmdControl* control = c->control; control && control != through; control = control->parent) {
        if (!control->iteration_active) continue;
        if (!control->error_label) {
            control->error_label = label(c);
            control->error = em_new_reg(&c->em, "iterator_error", MIR_T_I64);
            boxed(c, control->error);
        }
        move(c, control->error, reg(c, error));
        jump(c, control->error_label); return;
    }
    if (c->completion) {
        release_iterators(c, c->completion->control, error);
        move(c, c->completion->error, reg(c, error));
        jump(c, c->completion->handler); return;
    }
    release_iterators(c, NULL, error);
    MIR_op_t placeholder = c->em.frame.return_type == MIR_T_D
        ? MIR_new_double_op(c->em.ctx, 0) : integer(c, 0);
    em_stage_function_return(&c->em, c->function->native ? placeholder : reg(c, error), error);
}
static void emit_iterator_error_pad(LmdCompiler* c, LmdControl* control) {
    if (!control->error_label) return;
    LmdControl* saved = c->control;
    put_label(c, control->error_label);
    c->control = control;
    release_iterators(c, control->parent, control->error);
    // outer handlers retain their lexical iterator boundary; close errors cannot replace this throw.
    c->control = control->parent;
    return_error(c, control->error);
    c->control = saved;
}
static void completion_cleanup(LmdCompiler* c, LmdControl* target) {
    LmdCompletion* saved = c->completion;
    LmdControl* saved_control = c->control;
    MIR_reg_t saved_eval = c->eval_env; NameScope* saved_scope = c->eval_scope;
    for (LmdCompletion* ctx = saved; ctx; ctx = ctx->parent) {
        if (target) {
            bool exits = false;
            for (LmdControl* outer = ctx->control; outer; outer = outer->parent)
                if (outer == target) { exits = true; break; }
            if (!exits) break;
        }
        release_iterators(c, ctx->control);
        c->completion = ctx->parent; c->control = ctx->control;
        c->eval_env = ctx->eval_env; c->eval_scope = ctx->eval_scope;
        statement(c, ctx->finalizer);
    }
    c->completion = saved; c->control = saved_control;
    c->eval_env = saved_eval; c->eval_scope = saved_scope;
}
static void return_value(LmdCompiler* c, LmdValue value) {
    if (c->completion) {
        // The return value is evaluated before finally, which may overwrite its source binding.
        value = box(c, value);
        completion_cleanup(c, NULL);
    }
    release_iterators(c);
    MIR_reg_t result = c->function->native
        ? scalar_reg(c, value, c->function->returns, c->function->integer)
        : box(c, value).reg;
    if (c->function->home && ((AstMethodNode*)c->function->ast)->kind == AstMethodNode::JS_METHOD_CONSTRUCTOR) {
        MIR_reg_t receiver = c->receiver_cell ? em_load_at(&c->em,
            em_array_element_address(&c->em, c->receiver_cell, constant(c, 0), 8), 0, MIR_T_I64, "constructor_receiver") : c->receiver;
        result = em_call_3(&c->em, "mvp_lmd_constructor_result", MIR_T_I64,
            MIR_T_I64, reg(c, result), MIR_T_I64, reg(c, receiver),
            MIR_T_I64, integer(c, c->function->home->ast->superclass != NULL), true);
    }
    em_stage_function_return(&c->em, reg(c, result));
}
static void fail(LmdCompiler* c, int kind, AstNode* n, const char* detail = NULL) {
    MIR_reg_t error = em_call_3(&c->em, "mvp_lmd_fail", MIR_T_I64, MIR_T_I64,
        integer(c, kind), MIR_T_I64, integer(c, n ? n->source_span.start_byte : 0),
        MIR_T_P, relocated(c, (uint64_t)detail), true);
    return_error(c, error);
}
static void check_error(LmdCompiler* c, MIR_reg_t result) {
    MIR_label_t ok = label(c);
    MIR_reg_t tag = op(c, MIR_URSH, reg(c, result), integer(c, 56));
    branch(c, MIR_BNE, ok, reg(c, tag), integer(c, LMD_TYPE_ERROR));
    return_error(c, result); put_label(c, ok);
}

struct LmdLoopInterrupt {
    LmdCompiler* compiler;
    MIR_reg_t remaining;
    explicit LmdLoopInterrupt(LmdCompiler* c)
        : compiler(c), remaining(constant(c, 1024)) {}
    void poll() {
        LmdCompiler* c = compiler;
        MIR_label_t ready = label(c);
        move(c, remaining, reg(c, op(c, MIR_SUB, reg(c, remaining), integer(c, 1))));
        branch(c, MIR_BNE, ready, reg(c, remaining), integer(c, 0));
        move(c, remaining, integer(c, 1024));
        // D8.4.3v2: pure compiled loops need a completion boundary after SIGPROF too.
        check_error(c, em_call_0(&c->em, "mvp_lmd_execution_interrupt_status", MIR_T_I64, true));
        put_label(c, ready);
    }
};
static MIR_reg_t payload(LmdCompiler* c, MIR_reg_t value) {
    return op(c, MIR_AND, reg(c, value), integer(c, ITEM_INT_PAYLOAD_MASK));
}
static MIR_reg_t tag(LmdCompiler* c, LmdValue value) {
    unsigned k = semantic(value);
    if (value.number_present) {
        MIR_reg_t result = constant(c, LMD_TYPE_UNDEFINED);
        MIR_label_t done = label(c);
        branch_truth(c, done, value.number_present, false);
        move(c, result, integer(c, LMD_TYPE_FLOAT)); put_label(c, done); return result;
    }
    if (k && !(k & (k - 1))) {
        TypeId tid = k == K_NUMBER ? LMD_TYPE_FLOAT : k == K_STRING ? LMD_TYPE_STRING :
            k == K_BOOL ? LMD_TYPE_BOOL : k == K_NULL ? LMD_TYPE_NULL :
            k == K_UNDEFINED ? LMD_TYPE_UNDEFINED : k == K_ARRAY ? LMD_TYPE_ARRAY :
            k == K_TYPED_ARRAY ? LMD_TYPE_ARRAY_NUM : k == K_BIGINT ? LMD_TYPE_DECIMAL : k == K_SYMBOL ? LMD_TYPE_SYMBOL :
            k == K_OBJECT || k == K_MAP ? LMD_TYPE_MAP : LMD_TYPE_FUNC;
        return constant(c, tid);
    }
    MIR_reg_t result = em_new_reg(&c->em, "type", MIR_T_I64);
    MIR_label_t numeric = label(c), done = label(c);
    MIR_reg_t mask = op(c, MIR_AND, reg(c, value.reg), integer(c, ITEM_DBL_MASK));
    branch_truth(c, numeric, mask);
    move(c, result, reg(c, op(c, MIR_URSH, reg(c, value.reg), integer(c, 56))));
    // INT is a runtime subtype; primitive dispatch still sees JS Number.
    branch(c, MIR_BEQ, numeric, reg(c, result), integer(c, LMD_TYPE_INT));
    branch_truth(c, done, result);
    branch(c, MIR_BEQ, done, reg(c, value.reg), integer(c, 0));
    move(c, result, reg(c, em_load_at(&c->em, value.reg, 0, MIR_T_U8, "container_type")));
    jump(c, done);
    put_label(c, numeric); move(c, result, integer(c, LMD_TYPE_FLOAT));
    put_label(c, done); return result;
}
static MIR_reg_t cold_box(void* owner, MIR_reg_t value) {
    LmdCompiler* c = (LmdCompiler*)owner;
    int id = em_scalar_home_new(&c->em);
    MIR_reg_t home = em_materialize_frame_ref(&c->em, em_scalar_home_ref(&c->em, id));
    MIR_reg_t r = em_store_f64_home(&c->em, value, home);
    em_scalar_home_bind(&c->em, id, r); return r;
}
static MIR_reg_t cold_unbox(void* owner, MIR_reg_t item) {
    LmdCompiler* c = (LmdCompiler*)owner;
    MIR_reg_t result = em_new_reg(&c->em, "numeric_item", MIR_T_D);
    MIR_label_t floating = label(c), done = label(c);
    MIR_reg_t type = op(c, MIR_URSH, reg(c, item), integer(c, 56));
    // packed integers need no Float sentinel or payload-pointer checks.
    branch(c, MIR_BNE, floating, reg(c, type), integer(c, LMD_TYPE_INT));
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, result),
        reg(c, em_unbox_finite_int_item(&c->em, item))));
    jump(c, done); put_label(c, floating);
    MIR_reg_t value = em_unbox_f64_noninline_item(&c->em, c,
        [](void* owner, MIR_reg_t item) -> MIR_reg_t {
            LmdCompiler* c = (LmdCompiler*)owner;
            return em_load_at(&c->em, payload(c, item), 0, MIR_T_D, "wide_number");
        }, item);
    move(c, result, reg(c, value), true); put_label(c, done); return result;
}
static LmdValue box(LmdCompiler* c, LmdValue value) {
    if (is_boxed(value)) return value;
    if (value.number_present) {
        MIR_reg_t result = constant(c, ITEM_JS_UNDEFINED);
        MIR_label_t done = label(c);
        branch_truth(c, done, value.number_present, false);
        LmdValue present = value; present.kind = K_NUMBER; present.number_present = 0;
        move(c, result, reg(c, box(c, present).reg));
        put_label(c, done); return boxed(c, result, K_NUMBER | K_UNDEFINED);
    }
    if (value.literal && value.range.state == 1)
        return boxed(c, constant(c, i2it((int64_t)value.number)), K_NUMBER);
    if (value.integer) return boxed(c, em_box_finite_int_item(&c->em, value.reg), K_NUMBER);
    if (value.kind == K_NUMBER && value.literal) {
        double* literal = (double*)pool_alloc(c->program->frontend->pool, sizeof(double));
        *literal = value.number;
        return boxed(c, relocated_constant(c, lambda_float_ptr_to_item(literal).item), K_NUMBER);
    }
    if (value.kind == K_NUMBER)
        return boxed(c, em_box_f64_to_item(&c->em, c, cold_box, value.reg), K_NUMBER);
    if (value.kind == K_BOOL)
        return boxed(c, op(c, MIR_OR, reg(c, value.reg), integer(c, (uint64_t)LMD_TYPE_BOOL << 56)), K_BOOL);
    return boxed(c, value.reg, value.kind);
}

static MIR_reg_t coerce_noninline_number(void* owner, MIR_reg_t item) {
    LmdCompiler* c = (LmdCompiler*)owner;
    MIR_reg_t tid = op(c, MIR_URSH, reg(c, item), integer(c, 56));
    MIR_reg_t result = em_new_reg(&c->em, "converted_number", MIR_T_D);
    MIR_label_t numeric = label(c), done = label(c);
    branch(c, MIR_BEQ, numeric, reg(c, tid), integer(c, LMD_TYPE_INT));
    branch(c, MIR_BEQ, numeric, reg(c, tid), integer(c, LMD_TYPE_FLOAT));
    TypeId types[] = {LMD_TYPE_STRING, LMD_TYPE_BOOL, LMD_TYPE_NULL, LMD_TYPE_UNDEFINED};
    MIR_label_t cases[4];
    // library coercion already handles primitives; retain one uncommon adapter after the numeric guard.
    int case_count = c->program->library_active ? 0 : 4;
    for (int i = 0; i < case_count; i++) {
        cases[i] = label(c); branch(c, MIR_BEQ, cases[i], reg(c, tid), integer(c, types[i]));
    }
    if (c->program->bigints && !c->program->library_active) {
        MIR_label_t other = label(c);
        branch(c, MIR_BNE, other, reg(c, tid), integer(c, LMD_TYPE_DECIMAL));
        fail(c, LMD_MVP_TYPE, NULL); put_label(c, other);
    }
    if (c->program->library_active) {
        MIR_reg_t converted = em_call_2(&c->em, c->program->coercion_callbacks ?
            "mvp_lmd_primitive_to_number" : "mvp_lmd_primitive_to_number_fixed", MIR_T_I64,
            MIR_T_I64, reg(c, item), MIR_T_I64, integer(c, 0), true);
        // keep the uncommon object-conversion decoder out of every numeric loop's MIR body.
        check_error(c, converted);
        move(c, result, reg(c, em_call_1(&c->em, "it2d", MIR_T_D,
            MIR_T_I64, reg(c, converted), true)), true); jump(c, done);
    } else fail(c, LMD_MVP_CAPABILITY, NULL);
    put_label(c, numeric); move(c, result, reg(c, cold_unbox(c, item)), true); jump(c, done);
    for (int i = 0; i < case_count; i++) {
        put_label(c, cases[i]); MIR_reg_t r;
        if (i == 0) r = em_call_1(&c->em, "mvp_lmd_string_to_number", MIR_T_D,
            MIR_T_P, reg(c, payload(c, item)), true);
        else if (i == 1) r = to_number(c, {payload(c, item), K_BOOL});
        else r = to_number(c, number(c, i == 2 ? 0 : NAN));
        move(c, result, reg(c, r), true); jump(c, done);
    }
    put_label(c, done); return result;
}
static MIR_reg_t to_number(LmdCompiler* c, LmdValue value) {
    // integer optional lanes keep absence separate until JS numeric coercion needs NaN.
    if (value.number_present) {
        if (!value.integer) return value.reg;
        MIR_reg_t result = to_number(c, number(c, NAN));
        MIR_label_t done = label(c);
        branch_truth(c, done, value.number_present, false);
        LmdValue present = value; present.kind = K_NUMBER; present.number_present = 0;
        move(c, result, reg(c, to_number(c, present)), true);
        put_label(c, done); return result;
    }
    if (!is_boxed(value) && value.kind == K_NUMBER && !value.integer) return value.reg;
    if (value.literal) {
        MIR_reg_t r = em_new_reg(&c->em, "number_literal", MIR_T_D);
        move(c, r, MIR_new_double_op(c->em.ctx, value.number), true); return r;
    }
    if (value.integer || (!is_boxed(value) && value.kind == K_BOOL)) {
        MIR_reg_t r = em_new_reg(&c->em, "boolean_number", MIR_T_D);
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, r), reg(c, value.reg))); return r;
    }
    value = box(c, value);
    unsigned k = semantic(value);
    if (k == K_NUMBER) return em_unbox_f64_item(&c->em, c, cold_unbox, value.reg);
    if (k == (K_NUMBER | K_UNDEFINED)) {
        MIR_reg_t result = to_number(c, number(c, NAN));
        MIR_label_t done = label(c);
        branch(c, MIR_BEQ, done, reg(c, value.reg), integer(c, ITEM_JS_UNDEFINED));
        move(c, result, reg(c, em_unbox_f64_item(&c->em, c, cold_unbox, value.reg)), true);
        put_label(c, done); return result;
    }
    if (k == K_STRING) return em_call_1(&c->em, "mvp_lmd_string_to_number", MIR_T_D,
        MIR_T_P, reg(c, payload(c, value.reg)), true);
    if (k == K_NULL || k == K_UNDEFINED) return to_number(c, number(c, k == K_NULL ? 0 : NAN));
    // consume the shared inline-number guard once; only other encodings need JS coercion.
    return em_unbox_f64_item(&c->em, c, coerce_noninline_number, value.reg);
}
static MIR_reg_t truth(LmdCompiler* c, LmdValue v) {
    unsigned k = semantic(v);
    if (v.number_present) {
        LmdValue present = v; present.kind = K_NUMBER; present.number_present = 0;
        return op(c, MIR_AND, reg(c, v.number_present), reg(c, truth(c, present)));
    }
    if (k == K_NUMBER) {
        if (v.integer) return op(c, MIR_NE, reg(c, v.reg), integer(c, 0));
        MIR_reg_t d = to_number(c, v);
        MIR_reg_t nonzero = op(c, MIR_DNE, reg(c, d), MIR_new_double_op(c->em.ctx, 0));
        return op(c, MIR_AND, reg(c, nonzero), reg(c, op(c, MIR_DEQ, reg(c, d), reg(c, d))));
    }
    if (k == K_BIGINT) return op(c, MIR_EQ,
        reg(c, em_call_1(&c->em, "bigint_is_zero", MIR_T_I64, MIR_T_I64, reg(c, v.reg), true)), integer(c, 0));
    if (k == K_BOOL) return is_boxed(v) ? payload(c, v.reg) : v.reg;
    if (k == K_NULL || k == K_UNDEFINED) return constant(c, 0);
    if (k == K_ARRAY || k == K_TYPED_ARRAY || k == K_FUNCTION || k == K_OBJECT || k == K_MAP) return constant(c, 1);
    if (k == K_STRING) return op(c, MIR_NE,
        reg(c, em_load_at(&c->em, payload(c, v.reg), offsetof(String, len), MIR_T_U32, "string_length")), integer(c, 0));
    constexpr unsigned falsy = K_NUMBER | K_BIGINT | K_STRING | K_BOOL | K_NULL | K_UNDEFINED;
    if (k && !(k & falsy)) return constant(c, 1);
    if (k && !(k & ~(K_NULL | K_UNDEFINED))) return constant(c, 0);
    if (k && !(k & ~(K_BOOL | K_NULL | K_UNDEFINED)))
        return op(c, MIR_EQ, reg(c, v.reg), integer(c, ITEM_TRUE));
    // every object is truthy; scalar tags suffice without decoding a container header.
    MIR_reg_t tid = op(c, MIR_URSH, reg(c, v.reg), integer(c, 56));
    MIR_reg_t result = em_new_reg(&c->em, "truth", MIR_T_I64);
    MIR_label_t done = label(c), numeric = label(c), string = label(c), boolean = label(c);
    move(c, result, integer(c, 0));
    if (k & K_BOOL) branch(c, MIR_BEQ, boolean, reg(c, tid), integer(c, LMD_TYPE_BOOL));
    if (k & K_NULL) branch(c, MIR_BEQ, done, reg(c, tid), integer(c, LMD_TYPE_NULL));
    if (k & K_UNDEFINED) branch(c, MIR_BEQ, done, reg(c, tid), integer(c, LMD_TYPE_UNDEFINED));
    if (k & K_NUMBER) {
        branch(c, MIR_BEQ, numeric, reg(c, tid), integer(c, LMD_TYPE_INT));
        branch(c, MIR_BEQ, numeric, reg(c, tid), integer(c, LMD_TYPE_FLOAT));
        branch_truth(c, numeric, op(c, MIR_AND, reg(c, v.reg), integer(c, ITEM_DBL_MASK)));
    }
    if (k & K_STRING) branch(c, MIR_BEQ, string, reg(c, tid), integer(c, LMD_TYPE_STRING));
    if (c->program->bigints && (k & K_BIGINT)) {
        MIR_label_t other = label(c);
        branch(c, MIR_BNE, other, reg(c, tid), integer(c, LMD_TYPE_DECIMAL));
        move(c, result, reg(c, truth(c, boxed(c, v.reg, K_BIGINT)))); jump(c, done); put_label(c, other);
    }
    move(c, result, integer(c, 1)); jump(c, done);
    const unsigned kinds[] = {K_NUMBER, K_STRING, K_BOOL};
    MIR_label_t labels[] = {numeric, string, boolean};
    for (int i = 0; i < 3; i++) {
        put_label(c, labels[i]);
        if (k & kinds[i]) { move(c, result, reg(c, truth(c, {v.reg, kinds[i] | K_BOXED}))); jump(c, done); }
    }
    put_label(c, done); return result;
}
static MIR_reg_t nullish(LmdCompiler* c, LmdValue v) {
    if (!(semantic(v) & (K_NULL | K_UNDEFINED))) return constant(c, 0);
    MIR_reg_t t = tag(c, v);
    return op(c, MIR_OR, reg(c, op(c, MIR_EQ, reg(c, t), integer(c, LMD_TYPE_NULL))),
        reg(c, op(c, MIR_EQ, reg(c, t), integer(c, LMD_TYPE_UNDEFINED))));
}
static void branch_condition(LmdCompiler* c, AstNode* node, MIR_label_t target, bool yes) {
    // a condition consumes truth directly; short-circuit operands need no boxed merge.
    if (node->node_type == AST_NODE_UNARY && ((AstUnaryNode*)node)->op == OPERATOR_NOT) {
        branch_condition(c, ((AstUnaryNode*)node)->operand, target, !yes); return;
    }
    if (node->node_type == AST_NODE_BINARY) {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        if (binary->op == OPERATOR_AND || binary->op == OPERATOR_OR) {
            bool skip_on = binary->op == OPERATOR_OR;
            MIR_label_t skip = skip_on == yes ? target : label(c);
            branch_condition(c, binary->left, skip, skip_on);
            branch_condition(c, binary->right, target, yes);
            if (skip != target) put_label(c, skip);
            return;
        }
    }
    branch_truth(c, target, truth(c, expression(c, node)), yes);
}
static LmdValue text(LmdCompiler* c, const char* bytes) {
    String* s = name_pool_create_len(c->program->frontend->name_pool, bytes, strlen(bytes));
    return boxed(c, relocated_constant(c, s2it(s)), K_STRING);
}
static LmdValue to_string(LmdCompiler* c, LmdValue v, bool explicit_string = false) {
    unsigned k = semantic(v);
    if (k == K_STRING) return box(c, v);
    if (k == K_ARRAY) {
        MIR_reg_t result = em_call_2(&c->em, c->program->coercion_callbacks ?
            "mvp_lmd_primitive_to_string" : "mvp_lmd_primitive_to_string_fixed", MIR_T_I64,
            MIR_T_I64, reg(c, box(c, v).reg), MIR_T_I64, integer(c, c->program->library_active), true);
        check_error(c, result); return boxed(c, result, K_STRING);
    }
    if (k == K_BIGINT) {
        MIR_reg_t result = em_call_2(&c->em, "mvp_lmd_bigint_to_string", MIR_T_I64,
            MIR_T_I64, reg(c, box(c, v).reg), MIR_T_D, MIR_new_double_op(c->em.ctx, 10), true);
        check_error(c, result); return boxed(c, result, K_STRING);
    }
    if (k == K_NUMBER) {
        MIR_reg_t r = em_call_1(&c->em, "mvp_lmd_number_to_string", MIR_T_I64, MIR_T_D,
            reg(c, to_number(c, v)), true); check_error(c, r); return boxed(c, r, K_STRING);
    }
    if (k == K_NULL || k == K_UNDEFINED) return text(c, k == K_NULL ? "null" : "undefined");
    if (k == K_BOOL) {
        MIR_reg_t result = em_new_reg(&c->em, "boolean_string", MIR_T_I64);
        MIR_label_t done = label(c);
        move(c, result, reg(c, text(c, "false").reg));
        branch_truth(c, done, truth(c, v), false);
        move(c, result, reg(c, text(c, "true").reg));
        put_label(c, done); return boxed(c, result, K_STRING);
    }
    v = box(c, v);
    MIR_reg_t result = em_new_reg(&c->em, "primitive_string", MIR_T_I64);
    MIR_label_t done = label(c);
    move(c, result, reg(c, v.reg));
    if (k & K_STRING) branch(c, MIR_BEQ, done, reg(c, tag(c, v)), integer(c, LMD_TYPE_STRING));
    // keep dynamic coercion out of numeric loops while preserving each unit's object boundary.
    MIR_reg_t converted = em_call_2(&c->em, c->program->coercion_callbacks ?
        "mvp_lmd_primitive_to_string" : "mvp_lmd_primitive_to_string_fixed", MIR_T_I64,
        MIR_T_I64, reg(c, v.reg), MIR_T_I64, integer(c, c->program->library_active | (explicit_string ? 2 : 0)), true);
    check_error(c, converted); move(c, result, reg(c, converted));
    put_label(c, done); return boxed(c, result, K_STRING);
}
static MIR_reg_t integer_lane(LmdCompiler* c, MIR_reg_t d, double lower, double upper,
        MIR_label_t miss, bool exact) {
    branch_truth(c, miss, op(c, MIR_DGE, reg(c, d), MIR_new_double_op(c->em.ctx, lower)), false);
    branch_truth(c, miss, op(c, MIR_DLE, reg(c, d), MIR_new_double_op(c->em.ctx, upper)), false);
    MIR_reg_t i = em_new_reg(&c->em, "integer", MIR_T_I64);
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_D2I, reg(c, i), reg(c, d)));
    if (exact) {
        MIR_reg_t round = em_new_reg(&c->em, "integer_roundtrip", MIR_T_D);
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, round), reg(c, i)));
        branch_truth(c, miss, op(c, MIR_DEQ, reg(c, d), reg(c, round)), false);
    }
    return i;
}
static MIR_reg_t int32(LmdCompiler* c, LmdValue v) {
    materialize_integer_operand(c, &v);
    if (v.integer) {
        MIR_reg_t result = em_new_reg(&c->em, "int32", MIR_T_I64);
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_EXT32, reg(c, result), reg(c, v.reg)));
        return result;
    }
    MIR_reg_t d = to_number(c, v);
    MIR_label_t zero = label(c), done = label(c), slow = label(c), narrow = label(c);
    MIR_reg_t result = em_new_reg(&c->em, "int32", MIR_T_I64);
    // truncating finite bounded Numbers before EXT32 is exactly ToInt32, including fractions.
    move(c, result, reg(c, integer_lane(c, d, -9007199254740992.0, 9007199254740992.0, slow, false)));
    jump(c, narrow); put_label(c, slow);
    MIR_reg_t bounded = em_call_2(&c->em, "fmod", MIR_T_D, MIR_T_D, reg(c, d),
        MIR_T_D, MIR_new_double_op(c->em.ctx, 4294967296.0), true);
    branch_truth(c, zero, op(c, MIR_DEQ, reg(c, bounded), reg(c, bounded)), false);
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_D2I, reg(c, result), reg(c, bounded)));
    put_label(c, narrow);
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_EXT32, reg(c, result), reg(c, result)));
    jump(c, done); put_label(c, zero); move(c, result, integer(c, 0)); put_label(c, done); return result;
}

static LmdValue binary_value(LmdCompiler* c, Operator operation, LmdValue left, LmdValue right);
static bool compact_property_dispatch(LmdCompiler* c);
static LmdValue stable_item(LmdCompiler* c, MIR_reg_t value);
static LmdValue equal(LmdCompiler* c, LmdValue left, LmdValue right, bool loose) {
    if (!loose && (semantic(left) == K_NULL || semantic(right) == K_NULL ||
            semantic(left) == K_UNDEFINED || semantic(right) == K_UNDEFINED))
        return {op(c, MIR_EQ, reg(c, box(c, left).reg), reg(c, box(c, right).reg)), K_BOOL};
    if (left.integer && right.integer) {
        MIR_reg_t result = op(c, MIR_EQ, reg(c, left.reg), reg(c, right.reg));
        if (left.number_present || right.number_present) {
            // absent payloads are zero; equality also requires matching presence.
            MIR_reg_t lp = left.number_present ? left.number_present : constant(c, 1);
            MIR_reg_t rp = right.number_present ? right.number_present : constant(c, 1);
            result = op(c, MIR_AND, reg(c, result), reg(c, op(c, MIR_EQ, reg(c, lp), reg(c, rp))));
        }
        return {result, K_BOOL};
    }
    if ((left.number_present || right.number_present) &&
            (left.number_present || semantic(left) == K_NUMBER) &&
            (right.number_present || semantic(right) == K_NUMBER)) {
        // absent numeric lanes coerce to NaN; equality additionally admits two absent values.
        MIR_reg_t result = op(c, MIR_DEQ, reg(c, to_number(c, left)), reg(c, to_number(c, right)));
        if (left.number_present && right.number_present) {
            MIR_reg_t present = op(c, MIR_OR, reg(c, left.number_present), reg(c, right.number_present));
            result = op(c, MIR_OR, reg(c, result), reg(c, op(c, MIR_EQ, reg(c, present), integer(c, 0))));
        }
        return {result, K_BOOL};
    }
    if (semantic(left) == K_NUMBER && semantic(right) == K_NUMBER)
        return {op(c, MIR_DEQ, reg(c, to_number(c, left)), reg(c, to_number(c, right))), K_BOOL};
    left = box(c, left); right = box(c, right);
    MIR_reg_t result = em_new_reg(&c->em, "equal", MIR_T_I64);
    MIR_label_t same = label(c), numeric = label(c), strings = label(c), done = label(c);
    MIR_label_t coerce = loose && c->program->library_active ? label(c) : NULL;
    if (semantic(left) & semantic(right) & K_NUMBER) {
        // canonical integer Items compare directly; mixed numeric encodings retain Number equality below.
        MIR_label_t general = label(c);
        for (LmdValue value : {left, right})
            branch(c, MIR_BNE, general, reg(c, op(c, MIR_URSH, reg(c, value.reg), integer(c, 56))), integer(c, LMD_TYPE_INT));
        move(c, result, reg(c, op(c, MIR_EQ, reg(c, left.reg), reg(c, right.reg)))); jump(c, done);
        put_label(c, general);
    }
    MIR_reg_t lt = tag(c, left), rt = tag(c, right);
    move(c, result, integer(c, 0));
    branch(c, MIR_BEQ, same, reg(c, lt), reg(c, rt));
    if (loose) {
        MIR_reg_t ln = nullish(c, left), rn = nullish(c, right);
        MIR_reg_t both_null = op(c, MIR_AND, reg(c, ln), reg(c, rn));
        move(c, result, reg(c, both_null)); branch_truth(c, done, result);
        branch_truth(c, done, op(c, MIR_OR, reg(c, ln), reg(c, rn)));
        // physical container kinds all participate in JS object identity (S1.11).
        MIR_reg_t objects[2] = {constant(c, 0), constant(c, 0)};
        MIR_reg_t types[2] = {lt, rt};
        const TypeId object_types[] = {LMD_TYPE_ARRAY, LMD_TYPE_ARRAY_NUM, LMD_TYPE_MAP, LMD_TYPE_FUNC};
        for (int i = 0; i < 2; i++) for (TypeId type : object_types)
            move(c, objects[i], reg(c, op(c, MIR_OR, reg(c, objects[i]),
                reg(c, op(c, MIR_EQ, reg(c, types[i]), integer(c, type))))));
        branch_truth(c, done, op(c, MIR_AND, reg(c, objects[0]), reg(c, objects[1])));
        // conversion covers the complete admitted Boolean/Number/String primitive domain.
        const TypeId primitive_types[] = {LMD_TYPE_BOOL, LMD_TYPE_FLOAT, LMD_TYPE_STRING};
        for (TypeId t : primitive_types) {
            MIR_label_t next = label(c);
            branch(c, MIR_BNE, next, reg(c, lt), integer(c, t));
            branch(c, MIR_BEQ, numeric, reg(c, rt), integer(c, LMD_TYPE_BOOL));
            branch(c, MIR_BEQ, numeric, reg(c, rt), integer(c, LMD_TYPE_FLOAT));
            branch(c, MIR_BEQ, numeric, reg(c, rt), integer(c, LMD_TYPE_STRING));
            if (coerce) jump(c, coerce); else fail(c, LMD_MVP_CAPABILITY, NULL);
            put_label(c, next);
        }
        if (coerce) jump(c, coerce); else fail(c, LMD_MVP_CAPABILITY, NULL);
    }
    jump(c, done);
    if (coerce) {
        put_label(c, coerce);
        // keep primitive/identity fast paths; reentry is needed only at object coercion.
        MIR_reg_t value = em_call_3(&c->em, "mvp_lmd_dynamic_binary", MIR_T_I64,
            MIR_T_I64, integer(c, OPERATOR_EQ), MIR_T_I64, reg(c, left.reg), MIR_T_I64, reg(c, right.reg), true);
        check_error(c, value);
        move(c, result, reg(c, op(c, MIR_AND, reg(c, value), integer(c, 1)))); jump(c, done);
    }
    put_label(c, same);
    branch(c, MIR_BEQ, numeric, reg(c, lt), integer(c, LMD_TYPE_FLOAT));
    branch(c, MIR_BEQ, strings, reg(c, lt), integer(c, LMD_TYPE_STRING));
    move(c, result, reg(c, op(c, MIR_EQ, reg(c, left.reg), reg(c, right.reg)))); jump(c, done);
    put_label(c, numeric);
    move(c, result, reg(c, op(c, MIR_DEQ, reg(c, to_number(c, left)), reg(c, to_number(c, right))))); jump(c, done);
    put_label(c, strings);
    // equality can reject ASCII length mismatches without computing lexical order.
    MIR_reg_t matches = em_call_2(&c->em, "js_string_equal", MIR_T_I64,
        MIR_T_I64, reg(c, left.reg), MIR_T_I64, reg(c, right.reg), true);
    move(c, result, reg(c, matches));
    put_label(c, done); return {result, K_BOOL};
}
// keep guarded integer results as Items so an overflowing edge can widen in the same body.
static bool integer_binary(LmdCompiler* c, Operator operation, LmdValue left,
        LmdValue right, LmdValue* out) {
    MirNumericOpPlan plan = {};
    if (left.number_present || right.number_present) return false;
    if (!em_numeric_op_plan(operation, &plan, true) ||
            (!plan.is_comparison && operation != OPERATOR_ADD && operation != OPERATOR_SUB &&
             operation != OPERATOR_MOD)) return false;
    if ((!left.integer && !is_boxed(left)) || (!right.integer && !is_boxed(right)) ||
            !(semantic(left) & semantic(right) & K_NUMBER)) return false;
    MIR_label_t miss = label(c), done = label(c);
    MIR_reg_t result = em_new_reg(&c->em, "integer_result", MIR_T_I64);
    LmdValue operands[] = {left, right};
    MIR_reg_t ints[2];
    for (int i = 0; i < 2; i++) {
        if (operands[i].integer) ints[i] = operands[i].reg;
        else {
            MIR_reg_t type = op(c, MIR_URSH, reg(c, operands[i].reg), integer(c, 56));
            branch(c, MIR_BNE, miss, reg(c, type), integer(c, LMD_TYPE_INT));
            ints[i] = em_unbox_finite_int_item(&c->em, operands[i].reg);
        }
    }
    if (operation == OPERATOR_MOD) branch(c, MIR_BEQ, miss, reg(c, ints[1]), integer(c, 0));
    MIR_reg_t value = op(c, plan.i64_opcode, reg(c, ints[0]), reg(c, ints[1]));
    if (!plan.is_comparison) {
        if (operation == OPERATOR_MOD) {
            // a negative exact multiple has JS -0, which has no finite integer carrier.
            MIR_label_t nonzero = label(c);
            branch(c, MIR_BNE, nonzero, reg(c, value), integer(c, 0));
            branch(c, MIR_BLT, miss, reg(c, ints[0]), integer(c, 0)); put_label(c, nonzero);
        } else em_branch_int53_outside(&c->em, value, miss);
        value = em_box_finite_int_item(&c->em, value);
    }
    move(c, result, reg(c, value)); jump(c, done); put_label(c, miss);
    MIR_reg_t l = to_number(c, left), r = to_number(c, right);
    LmdValue slow = plan.helper_name ? LmdValue{em_call_2(&c->em, plan.helper_name, MIR_T_D,
        MIR_T_D, reg(c, l), MIR_T_D, reg(c, r), true), K_NUMBER} :
        LmdValue{op(c, plan.f64_opcode, reg(c, l), reg(c, r), plan.is_comparison ? MIR_T_I64 : MIR_T_D),
            plan.is_comparison ? K_BOOL : K_NUMBER};
    move(c, result, reg(c, plan.is_comparison ? slow.reg : box(c, slow).reg)); put_label(c, done);
    *out = plan.is_comparison ? LmdValue{result, K_BOOL} : boxed(c, result, K_NUMBER);
    return true;
}
static LmdValue primitive_operand(LmdCompiler* c, LmdValue operand, int hint) {
    if (!c->program->library_active ||
            !(semantic(operand) & (K_OBJECT | K_ARRAY | K_FUNCTION | K_MAP | K_TYPED_ARRAY))) return operand;
    MIR_reg_t original = box(c, operand).reg;
    MIR_reg_t value = em_new_reg(&c->em, "primitive", MIR_T_I64);
    move(c, value, reg(c, original));
    MIR_label_t ready = label(c);
    // all admitted objects use Lambda's direct-pointer lane; tagged primitives have no hooks.
    em_guard_container(&c->em, original, LMD_TYPE_RAW_POINTER, ready);
    MIR_reg_t converted = em_call_3(&c->em, c->program->coercion_callbacks ?
        "mvp_lmd_to_primitive" : "mvp_lmd_to_primitive_fixed", MIR_T_I64,
        MIR_T_I64, reg(c, original), MIR_T_I64, integer(c, hint),
        MIR_T_I64, integer(c, 1), true);
    check_error(c, converted); move(c, value, reg(c, converted)); put_label(c, ready);
    return boxed(c, value, K_UNDEFINED | K_NULL | K_BOOL | K_NUMBER | K_STRING |
        (c->program->bigints ? K_BIGINT : 0) | (c->program->symbols ? K_SYMBOL : 0));
}
static LmdValue binary_value(LmdCompiler* c, Operator operation, LmdValue left, LmdValue right) {
    unsigned left_kind = semantic(left), right_kind = semantic(right);
    MirNumericOpPlan plan = {};
    bool planned = em_numeric_op_plan(operation, &plan, true);
    // with one Number operand, relational comparison cannot select string
    // ordering. ToNumber owns the other operand's complete conversion once.
    bool number_comparison = planned && plan.is_comparison && !c->program->bigints &&
        (left_kind == K_NUMBER || right_kind == K_NUMBER);
    bool coercing_bigint_equality = (operation == OPERATOR_EQ || operation == OPERATOR_NE) &&
        ((left_kind | right_kind) & K_BIGINT) &&
        ((left_kind | right_kind) & (K_OBJECT | K_ARRAY | K_MAP | K_TYPED_ARRAY | K_FUNCTION));
    if (coercing_bigint_equality || (compact_property_dispatch(c) &&
            ((left_kind & (left_kind - 1)) || (right_kind & (right_kind - 1))) &&
            ((left_kind | right_kind) & (K_STRING | K_BIGINT | K_SYMBOL | K_OBJECT |
                K_ARRAY | K_FUNCTION | K_MAP | K_TYPED_ARRAY)))) {
        // unknown coercions otherwise clone every primitive arm; the shared bridge owns their order.
        MIR_reg_t value = em_call_3(&c->em, "mvp_lmd_dynamic_binary", MIR_T_I64,
            MIR_T_I64, integer(c, operation), MIR_T_I64, reg(c, box(c, left).reg),
            MIR_T_I64, reg(c, box(c, right).reg), true);
        check_error(c, value);
        return boxed(c, stable_item(c, value).reg, binary_kind(operation, K_ANY, K_ANY));
    }
    if (!number_comparison && operation != OPERATOR_EQ && operation != OPERATOR_NE &&
            operation != OPERATOR_JS_STRICT_EQ && operation != OPERATOR_JS_STRICT_NE &&
            operation != OPERATOR_IN && operation != OPERATOR_JS_INSTANCEOF) {
        bool numeric = operation != OPERATOR_ADD && operation != OPERATOR_LT &&
            operation != OPERATOR_LE && operation != OPERATOR_GT && operation != OPERATOR_GE;
        left = primitive_operand(c, left, operation == OPERATOR_ADD ? 0 : 1);
        if (numeric && (semantic(left) & K_SYMBOL) &&
                (right_kind & (K_OBJECT | K_ARRAY | K_FUNCTION | K_MAP | K_TYPED_ARRAY))) {
            // Symbol's high-byte tag is outside inline-double space; no container load is needed.
            MIR_label_t ready = label(c);
            MIR_reg_t type = op(c, MIR_URSH, reg(c, box(c, left).reg), integer(c, 56));
            branch(c, MIR_BNE, ready, reg(c, type), integer(c, LMD_TYPE_SYMBOL));
            fail(c, LMD_MVP_TYPE, NULL); put_label(c, ready);
            left.kind &= ~K_SYMBOL;
        }
        right = primitive_operand(c, right, operation == OPERATOR_ADD ? 0 : 1);
    }
    if (c->program->bigints && ((semantic(left) | semantic(right)) & K_BIGINT) &&
            !(operation == OPERATOR_ADD && ((semantic(left) | semantic(right)) & K_STRING))) {
        MIR_reg_t result = em_new_reg(&c->em, "bigint_result", MIR_T_I64);
        MIR_label_t big = label(c), done = label(c);
        bool definite = semantic(left) == K_BIGINT || semantic(right) == K_BIGINT;
        if (!definite) {
            branch(c, MIR_BEQ, big, reg(c, tag(c, left)), integer(c, LMD_TYPE_DECIMAL));
            branch(c, MIR_BEQ, big, reg(c, tag(c, right)), integer(c, LMD_TYPE_DECIMAL));
            LmdValue a = left, b = right; a.kind &= ~K_BIGINT; b.kind &= ~K_BIGINT;
            move(c, result, reg(c, box(c, binary_value(c, operation, a, b)).reg)); jump(c, done);
        }
        put_label(c, big);
        MIR_reg_t value = em_call_3(&c->em, "mvp_lmd_bigint_binary", MIR_T_I64,
            MIR_T_I64, reg(c, box(c, left).reg), MIR_T_I64, reg(c, box(c, right).reg),
            MIR_T_I64, integer(c, operation), true);
        check_error(c, value); move(c, result, reg(c, value)); put_label(c, done);
        return boxed(c, result, binary_kind(operation, semantic(left), semantic(right)));
    }
    if (operation == OPERATOR_EQ || operation == OPERATOR_NE ||
            operation == OPERATOR_JS_STRICT_EQ || operation == OPERATOR_JS_STRICT_NE) {
        LmdValue result = equal(c, left, right, operation == OPERATOR_EQ || operation == OPERATOR_NE);
        if (operation == OPERATOR_NE || operation == OPERATOR_JS_STRICT_NE)
            result.reg = op(c, MIR_XOR, reg(c, result.reg), integer(c, 1));
        return result;
    }
    if (operation == OPERATOR_ADD && ((semantic(left) | semantic(right)) & K_STRING)) {
        bool definite = semantic(left) == K_STRING || semantic(right) == K_STRING;
        MIR_reg_t result = em_new_reg(&c->em, "addition", MIR_T_I64);
        MIR_label_t strings = label(c), done = label(c);
        if (!definite) {
            MIR_reg_t lt = tag(c, left), rt = tag(c, right);
            branch(c, MIR_BEQ, strings, reg(c, lt), integer(c, LMD_TYPE_STRING));
            branch(c, MIR_BEQ, strings, reg(c, rt), integer(c, LMD_TYPE_STRING));
            LmdValue numeric_left = left, numeric_right = right;
            numeric_left.kind &= ~K_STRING; numeric_right.kind &= ~K_STRING;
            move(c, result, reg(c, box(c, binary_value(c, OPERATOR_ADD,
                numeric_left, numeric_right)).reg)); jump(c, done);
        }
        put_label(c, strings);
        LmdValue l = to_string(c, left), r = to_string(c, right);
        MIR_reg_t joined = em_call_2(&c->em, "mvp_lmd_string_concat", MIR_T_I64,
            MIR_T_I64, reg(c, l.reg), MIR_T_I64, reg(c, r.reg), true);
        check_error(c, joined); move(c, result, reg(c, joined)); put_label(c, done);
        return boxed(c, result, binary_kind(operation, semantic(left), semantic(right)));
    }
    if (planned && plan.is_comparison && ((semantic(left) & semantic(right)) & K_STRING)) {
        MIR_reg_t result = em_new_reg(&c->em, "relational", MIR_T_I64);
        MIR_label_t numeric = label(c), done = label(c);
        branch(c, MIR_BNE, numeric, reg(c, tag(c, left)), integer(c, LMD_TYPE_STRING));
        branch(c, MIR_BNE, numeric, reg(c, tag(c, right)), integer(c, LMD_TYPE_STRING));
        left = box(c, left); right = box(c, right);
        MIR_reg_t order = em_call_2(&c->em, "mvp_lmd_string_compare", MIR_T_I64,
            MIR_T_I64, reg(c, left.reg), MIR_T_I64, reg(c, right.reg), true);
        move(c, result, reg(c, op(c, plan.i64_opcode, reg(c, order), integer(c, 0)))); jump(c, done);
        put_label(c, numeric);
        move(c, result, reg(c, op(c, plan.f64_opcode, reg(c, to_number(c, left)), reg(c, to_number(c, right)))));
        put_label(c, done); return {result, K_BOOL};
    }
    if (planned) {
        bool integral = left.integer && right.integer && !left.number_present && !right.number_present;
        if (left.integer && right.integer && plan.is_comparison) {
            MIR_reg_t result = op(c, plan.i64_opcode, reg(c, left.reg), reg(c, right.reg));
            // undefined is unordered; the zero payload is only a storage convention.
            if (left.number_present) result = op(c, MIR_AND, reg(c, result), reg(c, left.number_present));
            if (right.number_present) result = op(c, MIR_AND, reg(c, result), reg(c, right.number_present));
            return {result, K_BOOL};
        }
        if (c->integer_bail && integral && !plan.is_comparison) {
            MIR_reg_t l = left.reg, r = right.reg, value;
            if (operation == OPERATOR_MUL) {
                bool right_factor = right.literal && right.number > 0;
                LmdValue factor = right_factor ? right : left;
                MIR_reg_t input = right_factor ? l : r;
                // admission requires a positive literal factor; guard before multiplying.
                uint64_t maximum = INT53_MAX / (uint64_t)factor.number;
                branch(c, MIR_UBGT, c->integer_bail, reg(c, input), integer(c, maximum));
            }
            if (operation == OPERATOR_DIV) {
                uint64_t divisor = (uint64_t)right.number;
                branch_truth(c, c->integer_bail, op(c, MIR_AND, reg(c, l), integer(c, divisor - 1)));
                unsigned shift = 0;
                for (; divisor > 1; divisor >>= 1) shift++;
                value = op(c, MIR_URSH, reg(c, l), integer(c, shift));
            } else value = op(c, plan.i64_opcode, reg(c, l), reg(c, r));
            // operands are nonnegative int53 values; add/sub intermediates fit i64.
            if (operation == OPERATOR_ADD || operation == OPERATOR_SUB)
                branch(c, MIR_UBGT, c->integer_bail, reg(c, value), integer(c, INT53_MAX));
            bool positive = operation == OPERATOR_ADD ? left.range.lower > 0 || right.range.lower > 0 :
                operation == OPERATOR_MUL ? left.range.lower > 0 && right.range.lower > 0 :
                operation == OPERATOR_DIV && left.range.lower > 0;
            return {value, K_NUMBER, false, 0, true, finite_range(positive ? 1 : 0, INT53_MAX)};
        }
        if (left.literal && right.literal && left.integer && right.integer &&
                (operation == OPERATOR_ADD || operation == OPERATOR_SUB || operation == OPERATOR_MUL))
            return number(c, operation == OPERATOR_ADD ? left.number + right.number :
                operation == OPERATOR_SUB ? left.number - right.number : left.number * right.number);
        LmdRange bounds = range_binary(operation, left.range, right.range);
        if (integral && bounds.state == 1 &&
                (operation == OPERATOR_ADD || operation == OPERATOR_SUB || operation == OPERATOR_MUL || operation == OPERATOR_MOD)) {
            return {op(c, plan.i64_opcode, reg(c, left.reg), reg(c, right.reg)), K_NUMBER, false, 0, true, bounds};
        }
        LmdValue integer_result;
        if (integer_binary(c, operation, left, right, &integer_result)) return integer_result;
        MIR_reg_t l = to_number(c, left), r = to_number(c, right);
        if (operation == OPERATOR_MOD && right.literal && right.number >= 1 &&
                right.number <= 9007199254740992.0 && right.number == trunc(right.number)) {
            uint64_t divisor = (uint64_t)right.number;
            if (!(divisor & (divisor - 1))) {
                MIR_label_t slow = label(c), done = label(c);
                MIR_reg_t result = em_new_reg(&c->em, "remainder", MIR_T_D);
                // positive exact integers admit masking; zero/negative/fractional values retain fmod's sign rules.
                MIR_reg_t i = integer_lane(c, l, 1, 9007199254740991.0, slow, true);
                MIR_reg_t bits = op(c, MIR_AND, reg(c, i), integer(c, divisor - 1));
                em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, result), reg(c, bits)));
                jump(c, done); put_label(c, slow);
                move(c, result, reg(c, em_call_2(&c->em, plan.helper_name, MIR_T_D,
                    MIR_T_D, reg(c, l), MIR_T_D, reg(c, r), true)), true);
                put_label(c, done); return {result, K_NUMBER};
            }
        }
        if (operation == OPERATOR_DIV && right.literal && right.number >= 1 && isfinite(right.number)) {
            int exponent;
            if (frexp(right.number, &exponent) == 0.5)
                // division by a positive power of two has the same single rounding as exact scaling.
                return {op(c, MIR_DMUL, reg(c, l), MIR_new_double_op(c->em.ctx, 1.0 / right.number), MIR_T_D), K_NUMBER};
        }
        if (plan.helper_name) return {em_call_2(&c->em, plan.helper_name, MIR_T_D,
            MIR_T_D, reg(c, l), MIR_T_D, reg(c, r), true), K_NUMBER};
        return {op(c, plan.f64_opcode, reg(c, l), reg(c, r), plan.is_comparison ? MIR_T_I64 : MIR_T_D),
            plan.is_comparison ? K_BOOL : K_NUMBER};
    }
    if (operation == OPERATOR_JS_EXP) return {em_call_2(&c->em, "mvp_lmd_number_pow", MIR_T_D,
        MIR_T_D, reg(c, to_number(c, left)), MIR_T_D, reg(c, to_number(c, right)), true), K_NUMBER};
    MIR_reg_t l = int32(c, left), r = int32(c, right);
    MIR_insn_code_t code;
    switch (operation) {
    case OPERATOR_JS_BIT_AND: code = MIR_ANDS; break;
    case OPERATOR_JS_BIT_OR: code = MIR_ORS; break;
    case OPERATOR_JS_BIT_XOR: code = MIR_XORS; break;
    case OPERATOR_JS_LSHIFT: code = MIR_LSHS; break;
    case OPERATOR_JS_RSHIFT: code = MIR_RSHS; break;
    case OPERATOR_JS_URSHIFT: code = MIR_URSHS; break;
    default: diagnostic(c->program, NULL, "operator outside MVP scope"); return number(c, NAN);
    }
    if (operation == OPERATOR_JS_LSHIFT || operation == OPERATOR_JS_RSHIFT || operation == OPERATOR_JS_URSHIFT)
        r = op(c, MIR_AND, reg(c, r), integer(c, 31));
    MIR_reg_t bits = op(c, code, reg(c, l), reg(c, r));
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx,
        operation == OPERATOR_JS_URSHIFT ? MIR_UEXT32 : MIR_EXT32, reg(c, bits), reg(c, bits)));
    return {bits, K_NUMBER, false, 0, true, finite_range(
        operation == OPERATOR_JS_URSHIFT ? 0 : INT32_MIN,
        operation == OPERATOR_JS_URSHIFT ? UINT32_MAX : INT32_MAX)};
}
static MIR_reg_t module_slots(LmdCompiler* c) {
    return em_load_at(&c->em, c->unit, offsetof(MvpLmdProgram, slots), MIR_T_I64, "program_slots");
}
static LmdValue stable_item(LmdCompiler* c, MIR_reg_t value) {
    int home = em_scalar_home_new(&c->em);
    MIR_reg_t address = em_materialize_frame_ref(&c->em, em_scalar_home_ref(&c->em, home));
    MIR_reg_t result = em_adopt_scalar_item_value(&c->em, SCALAR_RETURN_F64, value, address);
    em_scalar_home_bind(&c->em, home, result); return boxed(c, result);
}
static MIR_reg_t binding_cell(LmdCompiler* c, LmdBinding* b) {
    if (!b->owner->id && b->entry->scope == ((AstScript*)c->program->frontend->ast_root)->global_vars)
        return b->owner == c->function ? b->reg : em_load_at(&c->em, module_slots(c),
            b->slot * sizeof(Item), MIR_T_I64, "global_lexical_cell");
    LmdInlineFrame activation = {}; activation.function = c->function; activation.self = c->self;
    for (LmdInlineFrame* frame = c->inlining ? c->inlining : &activation; frame;
            frame = frame->parent ? frame->parent : frame != &activation ? &activation : NULL) {
        if (frame == &activation && b->owner == c->function) return b->reg;
        if (!frame->self) continue;
        ArrayList* captures = frame->function->captures;
        for (int i = 0; captures && i < captures->length; i++) if (captures->data[i] == b) {
            MIR_reg_t env = em_load_at(&c->em, frame->self, offsetof(Function, closure_env), MIR_T_I64, "closure_env");
            return em_load_at(&c->em, env, (i + frame->function->lexical_receiver) * sizeof(Item), MIR_T_I64, "captured_cell");
        }
    }
    diagnostic(c->program, NULL, "missing closure cell"); return constant(c, ITEM_NULL);
}
static MIR_reg_t binding_item(LmdCompiler* c, LmdBinding* b) {
    if (b->ast_capture) {
        AstNode* definition = c->program->satellite_source->ast_index.functions[
            c->program->satellite_function_id].node;
        // self is the original callable on every entry, even when siblings share compiled code.
        MIR_reg_t value = em_call_3(&c->em, "js_interp_capture_read", MIR_T_I64,
            MIR_T_I64, reg(c, c->self), MIR_T_P, relocated(c, (uint64_t)definition),
            MIR_T_P, relocated(c, (uint64_t)b->satellite_entry), true);
        check_error(c, value); return value;
    }
    if (b->module_import) {
        MIR_reg_t value = em_call_1(&c->em, "js_module_binding_get", MIR_T_I64,
            MIR_T_P, relocated(c, (uint64_t)b->module_import), true);
        check_error(c, value); return value;
    }
    if (b->module_cell) {
        MIR_reg_t state = em_load_at(&c->em, c->unit, offsetof(MvpLmdProgram, module_state), MIR_T_I64, "module_state");
        return em_call_2(&c->em, "lambda_module_var_at", MIR_T_I64,
            MIR_T_P, reg(c, state), MIR_T_I64, integer(c, b->slot), false);
    }
    if (b->captured) {
        MIR_reg_t cell = binding_cell(c, b);
        return em_load_at(&c->em, em_array_element_address(&c->em, cell, constant(c, 0), 8),
            0, MIR_T_I64, "captured_value");
    }
    return !b->owner->id ? em_load_at(&c->em, module_slots(c),
        b->slot * sizeof(Item), MIR_T_I64, "program_value") : b->reg;
}
static MIR_reg_t eval_scratch(LmdCompiler* c, size_t size) {
    int slot = c->em.frame.plan.fixed_number_scratch_slots;
    c->em.frame.plan.fixed_number_scratch_slots += (int)((size + sizeof(uint64_t) - 1) / sizeof(uint64_t));
    return em_materialize_frame_ref(&c->em, em_fixed_number_scratch_ref(slot));
}
static MIR_reg_t eval_caller(LmdCompiler* c) {
    MIR_reg_t caller = eval_scratch(c, sizeof(JsInterpHostCaller));
    MIR_reg_t home = constant(c, ITEM_NULL), constructor = constant(c, ITEM_NULL);
    LmdFunction* owner = c->function;
    while (owner->ast && owner->ast->node_type == AST_NODE_ARROW_FUNC) owner = owner->parent;
    bool method = c->function->home || c->function->object_home || c->function->lexical_super;
    if (method) {
        home = em_call_2(&c->em, "mvp_lmd_function_home", MIR_T_I64,
            MIR_T_I64, reg(c, c->self), MIR_T_I64, integer(c, 0), true);
        check_error(c, home); boxed(c, home);
        if (owner->home && owner->id == owner->home->constructor_id) {
            constructor = em_call_2(&c->em, "mvp_lmd_function_home", MIR_T_I64,
                MIR_T_I64, reg(c, c->self), MIR_T_I64, integer(c, 1), true);
            check_error(c, constructor); boxed(c, constructor);
        }
    }
    AstNode* parent = owner->ast ? ast_index_parent(&c->program->frontend->ast_index, (AstNode*)owner->ast) : NULL;
    bool initializer = owner->object_home && parent &&
        (parent->node_type == AST_NODE_FIELD || parent->node_type == JS_AST_NODE_STATIC_BLOCK);
    // module eval inherits undefined this; Script eval inherits the realm global.
    MIR_reg_t receiver = c->receiver_cell ? em_load_at(&c->em,
        em_array_element_address(&c->em, c->receiver_cell, constant(c, 0), 8), 0, MIR_T_I64, "eval_this") :
        c->function->id ? c->receiver : c->program->source_module ? constant(c, ITEM_JS_UNDEFINED) :
        em_load_at(&c->em, module_slots(c),
            c->program->global_slot * sizeof(Item), MIR_T_I64, "eval_global_this");
    MIR_reg_t target = c->receiver_cell ? em_load_at(&c->em,
        em_array_element_address(&c->em, c->receiver_cell, constant(c, 1), 8), 0, MIR_T_I64, "eval_new_target") :
        c->function->id ? c->new_target : constant(c, ITEM_JS_UNDEFINED);
    em_store_at(&c->em, caller, offsetof(JsInterpHostCaller, script), MIR_T_I64, relocated_constant(c, (uint64_t)c->program->frontend));
    em_store_at(&c->em, caller, offsetof(JsInterpHostCaller, env), MIR_T_I64, c->eval_env);
    em_store_at(&c->em, caller, offsetof(JsInterpHostCaller, receiver), MIR_T_I64, receiver);
    em_store_at(&c->em, caller, offsetof(JsInterpHostCaller, new_target), MIR_T_I64, target);
    em_store_at(&c->em, caller, offsetof(JsInterpHostCaller, home_object), MIR_T_I64, home);
    em_store_at(&c->em, caller, offsetof(JsInterpHostCaller, class_constructor), MIR_T_I64, constructor);
    em_store_at(&c->em, caller, offsetof(JsInterpHostCaller, flags), MIR_T_U32,
        constant(c, (c->strict ? JS_INTERP_CALLER_STRICT : 0) |
            (c->parameter_initializer ? JS_INTERP_CALLER_PARAMETERS : 0) |
            (method ? JS_INTERP_CALLER_METHOD : 0) | (initializer ? JS_INTERP_CALLER_INITIALIZER : 0)));
    return caller;
}
static LmdValue eval_reference_operation(LmdCompiler* c, MIR_reg_t reference,
        int operation, MIR_reg_t key = 0, MIR_reg_t value = 0) {
    MIR_reg_t result = em_call_6(&c->em, "mvp_lmd_dynamic_reference", MIR_T_I64,
        MIR_T_P, reg(c, c->unit), MIR_T_P, reg(c, eval_caller(c)), MIR_T_P, reg(c, reference),
        MIR_T_I64, key ? reg(c, key) : integer(c, ITEM_NULL),
        MIR_T_I64, value ? reg(c, value) : integer(c, ITEM_JS_UNDEFINED),
        MIR_T_I64, integer(c, operation), true);
    check_error(c, result); return stable_item(c, result);
}
static LmdReference eval_reference(LmdCompiler* c, AstIdentNode* id) {
    LmdReference result = {};
    result.eval_reference = eval_scratch(c, sizeof(JsInterpHostReference));
    eval_reference_operation(c, result.eval_reference, JS_INTERP_REFERENCE_RESOLVE, relocated_constant(c, s2it(id->name)));
    return result;
}
static void check_tdz(LmdCompiler* c, LmdBinding* b, MIR_reg_t value, AstNode* site) {
    // a receiver cell has no lexical binding; its constructor state supplies the admission proof.
    if (!b || (b->entry->is_lexical && !b->entry->is_function_name_binding && !b->dominated) ||
            (b->entry->is_parameter && b->owner->complex_params)) {
        MIR_label_t ok = label(c);
        branch(c, MIR_BNE, ok, reg(c, value), integer(c, ITEM_JS_TDZ));
        const char* detail = NULL;
        if (b && b->entry->name) {
            String* name = b->entry->name;
            // retained compiler metadata outlives every escaped native entry.
            size_t size = (size_t)name->len + 48;
            char* text = (char*)pool_alloc(c->program->frontend->pool, size);
            if (text) {
                snprintf(text, size, "Cannot access '%.*s' before initialization", (int)name->len, name->chars);
                detail = text;
            }
        }
        fail(c, LMD_MVP_REFERENCE, site, detail); put_label(c, ok);
    }
}
static LmdValue read_local_value(LmdCompiler* c, LmdValue value, bool borrow_scalar = false);
static void scalar_field_write(LmdCompiler* c, LmdScalarField* field, LmdValue value);
static LmdScalarField* inline_slot(LmdCompiler* c, LmdBinding* binding) {
    for (LmdInlineFrame* frame = c->inlining; frame; frame = frame->parent)
        for (int i = 0; i < frame->count; i++) if (frame->bindings[i] == binding) return &frame->slots[i];
    return NULL;
}
static bool compact_property_dispatch(LmdCompiler* c) {
    if (!c->program->dynamic_source || c->inlining || c->em.reg_counter < MVP_LMD_COMPACT_REGISTER_BUDGET) return false;
    // large unrolled bodies need the same bound as entries; preserve function-loop specialization.
    if (c->function->id) for (LmdControl* control = c->control; control; control = control->parent)
        if (control->loop_target) return false;
    return true;
}
static bool repeated_execution(LmdCompiler* c) {
    if (c->function->id) return true;
    for (LmdControl* control = c->control; control; control = control->parent)
        if (control->loop_target) return true;
    return false;
}
static bool native_host_values(LmdCompiler* c, unsigned kinds = K_ANY) {
    // primitive receivers cannot carry native object storage, even beside a host import.
    constexpr unsigned primitives = K_UNDEFINED | K_NULL | K_BOOL | K_NUMBER | K_STRING | K_BIGINT | K_SYMBOL;
    if (kinds && !(kinds & ~primitives)) return false;
    LmdFunction* active = c->inlining ? c->inlining->function : c->function;
    return c->program->native_host_values && !c->program->direct_stdio &&
        (!c->program->closed_stdio || active->uses_process);
}
static bool cold_host_operation(LmdCompiler* c) {
    return !repeated_execution(c) && native_host_values(c);
}
static MIR_reg_t global_binding_access(LmdCompiler* c, AstNode* site, bool write,
        MIR_reg_t value = 0, bool allow_missing = false, String* spelling = NULL, bool deleting = false,
        bool presence_only = false) {
    AstNode* target = site->node_type == AST_NODE_ASSIGN ? ((AstAssignNode*)site)->left :
        site->node_type == AST_NODE_UNARY ? ((AstUnaryNode*)site)->operand : site;
    if ((!spelling && (!target || target->node_type != AST_NODE_IDENT)) || !c->program->global_receiver) {
        fail(c, LMD_MVP_REFERENCE, site); return constant(c, ITEM_JS_UNDEFINED);
    }
    MIR_reg_t owner = em_load_at(&c->em, module_slots(c),
        c->program->global_slot * sizeof(Item), MIR_T_I64, "global_binding_object");
    MIR_reg_t name = relocated_constant(c, s2it(spelling ? spelling : ((AstIdentNode*)target)->name));
    LmdBinding* bound = target && target->node_type == AST_NODE_IDENT
        ? identifier_binding(c->program, (AstIdentNode*)target) : NULL;
    // Script function declarations install non-configurable object bindings
    // before execution, so later Scripts cannot shadow them with lexicals.
    // Read the live property: reassignment still changes the invoked callable.
    bool declared_function_read = !write && !deleting && !presence_only && !c->program->satellite_script &&
        bound && bound->reflected_global && bound->entry->scope &&
        (!spelling || spelling == bound->entry->name) && bound->entry->node &&
        bound->entry->node->node_type == AST_NODE_FUNC && !bound->entry->is_annex_b_companion;
    if (c->program->test262_host && c->program->dynamic_source && !declared_function_read) {
        // host Script entries can add global lexicals after this MIR body was compiled.
        auto access = [&](int operation) {
            return em_call_5(&c->em, "mvp_lmd_dynamic_global", MIR_T_I64,
                MIR_T_P, reg(c, c->unit), MIR_T_I64, reg(c, name),
                MIR_T_I64, write ? reg(c, value) : integer(c, ITEM_NULL),
                MIR_T_I64, integer(c, operation), MIR_T_I64, integer(c, c->strict), true);
        };
        if (presence_only) {
            MIR_reg_t found = access(JS_INTERP_HOST_HAS);
            check_error(c, found); return found;
        }
        MIR_reg_t joined = em_new_reg(&c->em, "script_global_binding", MIR_T_I64);
        MIR_label_t done = label(c);
        if (allow_missing && !write && !deleting) {
            MIR_reg_t present = access(JS_INTERP_HOST_HAS);
            check_error(c, present);
            move(c, joined, integer(c, ITEM_JS_UNDEFINED));
            branch(c, MIR_BEQ, done, reg(c, present), integer(c, ITEM_FALSE));
        }
        MIR_reg_t result = access(deleting ? JS_INTERP_HOST_DELETE : write ? JS_INTERP_HOST_SET : JS_INTERP_HOST_GET);
        check_error(c, result); move(c, joined, reg(c, result)); put_label(c, done);
        return joined;
    }
    if (presence_only) {
        MIR_reg_t found = property_access(c, LMD_PROP_HAS, owner, name);
        check_error(c, found); return found;
    }
    // strict writes still resolve presence before the assignment; checked reads share their own lookup.
    if (write && c->strict && !allow_missing) {
        MIR_reg_t found = property_access(c, LMD_PROP_HAS, owner, name);
        check_error(c, found);
        MIR_label_t present = label(c);
        branch(c, MIR_BEQ, present, reg(c, found), integer(c, ITEM_TRUE));
        fail(c, LMD_MVP_REFERENCE, site); put_label(c, present);
    }
    MIR_reg_t result = em_call_4(&c->em, "mvp_lmd_class_property", MIR_T_I64,
        MIR_T_I64, reg(c, owner), MIR_T_I64, reg(c, name),
        MIR_T_I64, write ? reg(c, value) : integer(c, ITEM_NULL),
        MIR_T_I64, integer(c, write ? LMD_PROP_SET | (c->strict ? LMD_PROP_STRICT : 0) :
            allow_missing ? LMD_PROP_GET : LMD_PROP_GET_BINDING), true);
    check_error(c, result);
    return result;
}
static LmdReference with_reference(LmdCompiler* c, LmdBinding* binding, AstNode* site);
static LmdValue with_reference_operation(LmdCompiler* c, LmdReference ref, AstNode* site,
    int operation, LmdValue value = {}, bool allow_missing = false);
static LmdValue read_binding(LmdCompiler* c, LmdBinding* b, AstNode* site, bool borrow_scalar = false) {
    if (site && site->node_type == AST_NODE_IDENT && eval_reference_site(c->program, site))
        return eval_reference_operation(c, eval_reference(c, (AstIdentNode*)site).eval_reference, JS_INTERP_REFERENCE_GET);
    if (b && b->with_lookup) return with_reference_operation(c, with_reference(c, b, site), site, LMD_PROP_GET);
    if (LmdScalarField* slot = inline_slot(c, b)) {
        if (is_boxed(slot->value)) check_tdz(c, b, slot->value.reg, site);
        return read_local_value(c, slot->value, borrow_scalar);
    }
    if (!b) return boxed(c, global_binding_access(c, site, false));
    if (b->reflected_global) {
        MIR_reg_t loaded = global_binding_access(c, site, false, 0, false, b->entry->name);
        LmdValue value = b->fixed_global && !(b->kinds & K_NUMBER) ? boxed(c, loaded) : stable_item(c, loaded);
        if (b->fixed_global) value.kind = b->kinds | K_BOXED;
        return value;
    }
    if (b->intrinsic == I_GLOBAL_THIS)
        return boxed(c, em_load_at(&c->em, module_slots(c), c->program->global_slot * sizeof(Item), MIR_T_I64, "global_this"), K_OBJECT);
    if (b->intrinsic >= I_PERFORMANCE || (c->program->typed_storage && typed_intrinsic(b->intrinsic) >= 0) || ((b->intrinsic == I_ERROR ||
            ((b->intrinsic == I_STRING || b->intrinsic == I_NUMBER) && c->program->primitives) ||
            b->intrinsic == I_OBJECT || b->intrinsic == I_ARRAY || b->intrinsic == I_MAP || b->intrinsic == I_MATH) && c->program->library_active))
        return boxed(c, em_load_at(&c->em, module_slots(c), b->slot * sizeof(Item), MIR_T_I64,
            "host_binding"), b->kinds);
    if (b->intrinsic >= 4) { fail(c, LMD_MVP_CAPABILITY, site); return boxed(c, constant(c, ITEM_JS_UNDEFINED)); }
    if (b->intrinsic) return b->intrinsic == 1
        ? boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED)
        : number(c, b->intrinsic == 2 ? NAN : INFINITY);
    MIR_reg_t source = b->native ? b->reg : binding_item(c, b);
    if (!b->native) check_tdz(c, b, source, site);
    bool floating = b->native && (b->kinds & K_NUMBER) && !b->integer;
    MIR_reg_t copy = em_new_reg(&c->em, "read", floating ? MIR_T_D : MIR_T_I64);
    move(c, copy, reg(c, source), floating);
    if (b->native) {
        for (LmdNumberFacts* facts = c->number_facts; b->number_present && facts; facts = facts->parent)
            for (int i = 0; i < 2; i++) if (facts->bindings[i] == b) {
                LmdValue value = {copy, K_NUMBER, false, 0, b->integer, range(c->program, site, true, true)};
                // keep arithmetic in the binding's lane; index consumers can materialize an integer.
                if (facts->ranges[i].state == 1) value.range = facts->ranges[i];
                return value;
            }
        MIR_reg_t present = 0;
        if (b->number_present) {
            present = em_new_reg(&c->em, "present_snapshot", MIR_T_I64);
            move(c, present, reg(c, b->number_present));
        }
        return {copy, b->kinds, false, 0, b->integer, range(c->program, site, true, present != 0), present};
    }
    // parameter kinds cover every incoming call and assignment, including missing arguments.
    bool known = b->entry->is_parameter || b->dominated || (b->entry->is_const && b->writes == 1) ||
        (b->target && b->target->ast->entry == b->entry &&
        b->entry->node && b->entry->node->node_type == AST_NODE_FUNC && !b->entry->is_annex_b_companion);
    if (known && !(b->kinds & K_NUMBER)) return boxed(c, copy, b->kinds);
    LmdValue result = borrow_scalar || !(b->kinds & K_NUMBER) ? boxed(c, copy) : stable_item(c, copy);
    if (known) result.kind = b->kinds | K_BOXED;
    else if (!(b->kinds & ~(K_NUMBER | K_BOOL | K_STRING | K_NULL | K_UNDEFINED))) {
        // captured scalar writes retain their domain; hoisted var reads still include undefined.
        result.kind = b->kinds | K_BOXED | (b->entry->is_lexical ? 0 : K_UNDEFINED);
    }
    return result;
}
static void write_binding(LmdCompiler* c, LmdBinding* b, LmdValue v, AstNode* site, bool initialize = false) {
    // a later read must observe this write, including undefined and writes in only one branch.
    for (LmdNumberFacts* facts = c->number_facts; facts; facts = facts->parent)
        for (LmdBinding*& known : facts->bindings) if (known == b) known = NULL;
    if (b && b->with_lookup) {
        with_reference_operation(c, with_reference(c, b, site), site, LMD_PROP_SET, v); return;
    }
    if (!b) { global_binding_access(c, site, true, box(c, v).reg); return; }
    if (b->ast_capture) {
        // synthetic dependency declarations own no values; source writes were rejected by admission.
        if (!initialize) diagnostic(c->program, site, "AST capture is read only");
        return;
    }
    if (b->module_import) { fail(c, LMD_MVP_TYPE, site); return; }
    if (b->reflected_global) {
        global_binding_access(c, site, true, box(c, v).reg, false, b->entry->name); return;
    }
    if (b->intrinsic >= 4) { fail(c, LMD_MVP_CAPABILITY, site); return; }
    if (b->intrinsic) { if (c->strict) fail(c, LMD_MVP_TYPE, site); return; }
    LmdScalarField* local = inline_slot(c, b);
    if (!initialize && (local ? is_boxed(local->value) : !b->native))
        check_tdz(c, b, local ? local->value.reg : binding_item(c, b), site);
    if (!initialize && (b->entry->is_const || b->entry->is_function_name_binding)) {
        if (!b->entry->is_function_name_binding || c->strict) fail(c, LMD_MVP_TYPE, site);
        return;
    }
    if (local) {
        if (c->integer_bail && v.range.lower < 1)
            branch(c, MIR_BEQ, c->integer_bail, reg(c, v.reg), integer(c, 0));
        scalar_field_write(c, local, v); return;
    }
    if (b->native) {
        if (b->number_present) {
            MIR_reg_t present = v.number_present ? v.number_present :
                op(c, MIR_NE, reg(c, tag(c, v)), integer(c, LMD_TYPE_UNDEFINED));
            move(c, b->number_present, reg(c, present));
            move(c, b->reg, reg(c, scalar_reg(c, v, b->kinds, b->integer)), !b->integer);
        } else move(c, b->reg, reg(c, scalar_reg(c, v, b->kinds, b->integer)), b->kinds == K_NUMBER && !b->integer);
    } else {
        v = box(c, v);
        if (b->module_cell) {
            if (c->program->satellite_script &&
                    !c->program->satellite_script->is_module && !b->entry->is_lexical) {
                // the shared setter also synchronizes the caller's registered var/slab mirror.
                // ES-module declarations stay private even when non-lexical.
                MIR_reg_t stored = em_call_3(&c->em, "js_set_global_property_after_with_lookup", MIR_T_I64,
                    MIR_T_I64, relocated(c, s2it(b->entry->name)), MIR_T_I64, reg(c, v.reg),
                    MIR_T_I64, integer(c, c->strict), true);
                check_error(c, stored); return;
            }
            MIR_reg_t state = em_load_at(&c->em, c->unit, offsetof(MvpLmdProgram, module_state), MIR_T_I64, "module_state");
            em_call_void_3(&c->em, "lambda_module_var_store", MIR_T_P, reg(c, state),
                MIR_T_I64, integer(c, b->slot), MIR_T_I64, reg(c, v.reg), false);
            if (c->program->satellite_script && c->program->satellite_script->interp_exports) {
                // the legacy AST namespace mirrors exported aliases outside the slab.
                em_call_void_3(&c->em, "js_interp_publish_export_bindings",
                    MIR_T_P, relocated(c, (uint64_t)c->program->satellite_script),
                    MIR_T_P, relocated(c, (uint64_t)b->satellite_entry),
                    MIR_T_I64, reg(c, v.reg), true);
            }
        } else if (b->captured) {
            MIR_reg_t cell = binding_cell(c, b);
            MIR_reg_t stored = em_call_3(&c->em, "mvp_lmd_array_store", MIR_T_I64, MIR_T_I64,
                reg(c, cell), MIR_T_I64, integer(c, 0), MIR_T_I64, reg(c, v.reg), true);
            check_error(c, stored);
        } else if (!b->owner->id) em_call_void_4(&c->em, "owned_item_slot_store", MIR_T_P, reg(c, module_slots(c)),
            MIR_T_I64, integer(c, c->program->slot_count), MIR_T_I64, integer(c, b->slot),
            MIR_T_I64, reg(c, v.reg), true);
        else if (!(semantic(v) & K_NUMBER)) move(c, b->reg, reg(c, v.reg));
        else {
            if (!b->scalar_home) b->scalar_home = em_scalar_home_new(&c->em);
            MIR_reg_t address = em_materialize_frame_ref(&c->em, em_scalar_home_ref(&c->em, b->scalar_home));
            MIR_reg_t owned = em_adopt_scalar_item_value(&c->em, SCALAR_RETURN_F64, v.reg, address);
            move(c, b->reg, reg(c, owned)); em_scalar_home_bind(&c->em, b->scalar_home, b->reg);
        }
    }
}
static LmdReference with_reference(LmdCompiler* c, LmdBinding* binding, AstNode* site) {
    LmdReference ref = {binding->fallback};
    ref.name = relocated_constant(c, s2it(binding->entry->name));
    ref.with_object = em_new_reg(&c->em, "with_reference_base", MIR_T_I64);
    move(c, ref.with_object, integer(c, ITEM_JS_UNDEFINED)); boxed(c, ref.with_object);
    MIR_label_t done = label(c);
    for (LmdWithLookup* lookup = binding->with_lookup; lookup; lookup = lookup->outer) {
        // the saved object is internal; an eval-aware identifier site would resolve the guest name instead.
        LmdValue object = read_binding(c, lookup->object, lookup->object->entry->node);
        MIR_reg_t found = em_call_4(&c->em, "mvp_lmd_with_binding", MIR_T_I64,
            MIR_T_I64, reg(c, object.reg), MIR_T_I64, reg(c, ref.name),
            MIR_T_I64, integer(c, ITEM_NULL), MIR_T_I64, integer(c, LMD_PROP_HAS), true);
        check_error(c, found);
        MIR_label_t next = label(c);
        branch(c, MIR_BEQ, next, reg(c, found), integer(c, ITEM_FALSE));
        move(c, ref.with_object, reg(c, object.reg)); jump(c, done); put_label(c, next);
    }
    put_label(c, done); return ref;
}
static LmdValue with_reference_operation(LmdCompiler* c, LmdReference ref, AstNode* site,
        int operation, LmdValue value, bool allow_missing) {
    MIR_reg_t result = em_new_reg(&c->em, "with_reference_value", MIR_T_I64);
    MIR_label_t ordinary = label(c), done = label(c);
    branch(c, MIR_BEQ, ordinary, reg(c, ref.with_object), integer(c, ITEM_JS_UNDEFINED));
    MIR_reg_t resolved = em_call_4(&c->em, "mvp_lmd_with_binding", MIR_T_I64,
        MIR_T_I64, reg(c, ref.with_object), MIR_T_I64, reg(c, ref.name),
        MIR_T_I64, operation == LMD_PROP_SET ? reg(c, box(c, value).reg) : integer(c, ITEM_NULL),
        MIR_T_I64, integer(c, operation | (c->strict ? LMD_PROP_STRICT : 0)), true);
    check_error(c, resolved); move(c, result, reg(c, resolved)); jump(c, done);
    put_label(c, ordinary);
    if (operation == LMD_PROP_GET) {
        LmdValue fallback = !ref.binding && allow_missing ?
            boxed(c, global_binding_access(c, site, false, 0, true)) : read_binding(c, ref.binding, site);
        move(c, result, reg(c, box(c, fallback).reg));
    } else if (operation == LMD_PROP_SET) {
        write_binding(c, ref.binding, value, site);
        move(c, result, reg(c, box(c, value).reg));
    } else {
        if (ref.binding && !ref.binding->reflected_global) move(c, result, integer(c, ITEM_FALSE));
        else {
            MIR_reg_t global = em_load_at(&c->em, module_slots(c), c->program->global_slot * sizeof(Item), MIR_T_I64, "with_delete_global");
            MIR_reg_t removed = property_access(c, LMD_PROP_DELETE, global, ref.name);
            check_error(c, removed); move(c, result, reg(c, removed));
        }
    }
    put_label(c, done); return stable_item(c, result);
}
static MIR_reg_t property_key(LmdCompiler* c, LmdValue value, bool typed = false, bool integral_number = false) {
    double maximum = typed ? INT53_MAX : UINT32_MAX - 1.0;
    int64_t invalid = typed ? -3 : -2;
    if (value.literal) return constant(c, value.number >= 0 && value.number <= maximum &&
        value.number == trunc(value.number) ? (uint64_t)value.number : (uint64_t)invalid);
    materialize_integer_operand(c, &value);
    if (value.integer) {
        bool bounded = value.range.state == 1 && value.range.lower >= 0 && value.range.upper <= maximum;
        if (bounded && !value.number_present)
            return value.reg;
        MIR_reg_t index = constant(c, value.number_present ? (uint64_t)-2 : (uint64_t)invalid);
        MIR_label_t done = label(c);
        if (value.number_present) {
            branch_truth(c, done, value.number_present, false);
            move(c, index, MIR_new_int_op(c->em.ctx, invalid));
        }
        if (!bounded) branch(c, MIR_UBGT, done, reg(c, value.reg), integer(c, (uint64_t)maximum));
        move(c, index, reg(c, value.reg)); put_label(c, done); return index;
    }
    unsigned k = semantic(value);
    MIR_reg_t result = em_new_reg(&c->em, "index", MIR_T_I64);
    MIR_label_t numeric = label(c), done = label(c);
    if (c->program->symbols && (k & K_SYMBOL)) {
        move(c, result, integer(c, -2));
        branch(c, MIR_BEQ, done, reg(c, tag(c, value)), integer(c, LMD_TYPE_SYMBOL));
    }
    if (value.number_present) {
        move(c, result, MIR_new_int_op(c->em.ctx, -2));
        branch_truth(c, done, value.number_present, false);
    } else if (k != K_NUMBER) {
        if (k != K_STRING) branch(c, MIR_BEQ, numeric, reg(c, tag(c, value)), integer(c, LMD_TYPE_FLOAT));
        LmdValue string = to_string(c, value);
        move(c, result, reg(c, em_call_2(&c->em, "mvp_lmd_string_key", MIR_T_I64,
            MIR_T_P, reg(c, payload(c, string.reg)), MIR_T_I64, integer(c, typed), true))); jump(c, done);
    }
    put_label(c, numeric);
    MIR_reg_t d = to_number(c, value);
    move(c, result, MIR_new_int_op(c->em.ctx, invalid));
    MIR_reg_t i = integer_lane(c, d, 0, maximum, done, !integral_number);
    move(c, result, reg(c, i)); put_label(c, done); return result;
}
static bool shape_union(LmdShapeSet* target, LmdShapeSet incoming) {
    bool changed = false;
    for (int i = 0; i < incoming.count; i++) {
        bool present = false;
        for (int j = 0; j < target->count; j++) present |= target->plans[j] == incoming.plans[i];
        if (!present && target->count < 4) {
            target->plans[target->count++] = incoming.plans[i]; changed = true;
        }
    }
    return changed;
}
static LmdShapeSet object_shapes(MvpLmdProgram* p, AstNode* node, int depth = 0) {
    if (!node || depth >= 16) return {};
    // a constructor template describes its instances, never the function value itself.
    if (node->node_type == AST_NODE_FUNC || node->node_type == AST_NODE_FUNC_EXPR) return {};
    if (node->node_type == AST_NODE_IDENT) {
        LmdBinding* b = identifier_binding(p, (AstIdentNode*)node);
        return b && !b->reflected_global ? b->shapes : LmdShapeSet{};
    }
    if (node->node_type == AST_NODE_CALL_EXPR) {
        LmdFunction* f = direct_target(p, (AstCallNode*)node);
        return f ? f->shapes : LmdShapeSet{};
    }
    if (node->node_type == AST_NODE_MEMBER_EXPR) {
        AstFieldNode* field = (AstFieldNode*)node;
        if (field->computed) return {};
        String* key = ((AstIdentNode*)field->property)->name;
        LmdShapeSet owners = object_shapes(p, field->object, depth + 1), result = {};
        for (int i = 0; i < owners.count; i++) {
            if (owners.plans[i]->constructor) continue;
            for (AstNode* e = ((AstMapNode*)owners.plans[i]->literal)->properties; e; e = e->next) {
                AstPropertyNode* prop = (AstPropertyNode*)e;
                String* name = ((AstIdentNode*)prop->key)->name;
                if (key->len == name->len && !memcmp(key->chars, name->chars, key->len))
                    shape_union(&result, object_shapes(p, prop->value, depth + 1));
            }
        }
        return result;
    }
    if (node->node_type == AST_NODE_CONDITIONAL_EXPR) {
        LmdShapeSet result = object_shapes(p, ((AstIfNode*)node)->then, depth + 1);
        shape_union(&result, object_shapes(p, ((AstIfNode*)node)->otherwise, depth + 1));
        return result;
    }
    for (int i = 0; p->objects && i < p->objects->length; i++) {
        LmdObjectPlan* plan = (LmdObjectPlan*)p->objects->data[i];
        if (plan->literal == node) return {1, {plan}};
    }
    return {};
}
static LmdObjectPlan* object_plan(MvpLmdProgram* p, AstNode* node, bool constructor = false) {
    if (constructor) {
        for (int i = 0; p->objects && i < p->objects->length; i++) {
            LmdObjectPlan* plan = (LmdObjectPlan*)p->objects->data[i];
            if (plan->literal == node) return plan;
        }
        return NULL;
    }
    if (node && node->node_type == AST_NODE_CALL_EXPR) {
        LmdFunction* f = direct_target(p, (AstCallNode*)node);
        if (f && f->scalar_factory) for (int i = 0; i < p->objects->length; i++) {
            LmdObjectPlan* plan = (LmdObjectPlan*)p->objects->data[i];
            if (plan->literal == node) return plan;
        }
    }
    return object_shapes(p, node).plans[0];
}
static LmdScalarFactoryPlan* scalar_factory_plan(LmdObjectPlan* plan) {
    // only call-based constructor plans allocate this extension; ordinary object plans stay compact.
    return plan && plan->constructor && plan->literal->node_type == AST_NODE_CALL_EXPR
        ? (LmdScalarFactoryPlan*)plan : NULL;
}
static LmdScalarField* scalar_field(MvpLmdProgram* p, AstNode* node) {
    AstFieldNode* member = (AstFieldNode*)node;
    if (member->computed || member->object->node_type != AST_NODE_IDENT) return NULL;
    LmdBinding* b = identifier_binding(p, (AstIdentNode*)member->object);
    LmdObjectPlan* plan = b && !b->reflected_global ? b->shape_hint : NULL;
    if (!plan || plan->scalar_binding != b) return NULL;
    String* key = ((AstIdentNode*)member->property)->name;
    ShapeEntry* field = typemap_hash_lookup(plan->blueprint, key->chars, key->len);
    int i = 0;
    FOR_EACH_MAP_FIELD(plan->blueprint, entry) {
        if (entry == field) return &plan->scalar_fields[i];
        i++;
    }
    return NULL;
}
static AstNode* scalar_field_initializer(MvpLmdProgram* p, AstNode* node) {
    LmdScalarField* field = scalar_field(p, node);
    return field && !field->written ? field->initializer : NULL;
}
static LmdScalarField* scalar_reference(LmdReference ref) {
    if (!ref.plan || !ref.plan->scalar_binding) return NULL;
    int index = 0;
    FOR_EACH_MAP_FIELD(ref.plan->blueprint, field) {
        if (field == ref.field) return &ref.plan->scalar_fields[index];
        index++;
    }
    return NULL;
}
static LmdValue read_local_value(LmdCompiler* c, LmdValue value, bool borrow_scalar) {
    if (is_boxed(value)) {
        if (borrow_scalar || !(semantic(value) & K_NUMBER)) {
            MIR_reg_t copy = em_new_reg(&c->em, "local_borrow", MIR_T_I64);
            move(c, copy, reg(c, value.reg)); return boxed(c, copy, semantic(value));
        }
        LmdValue copy = stable_item(c, value.reg); copy.kind = value.kind; return copy;
    }
    bool floating = (semantic(value) & K_NUMBER) && !value.integer;
    MIR_reg_t copy = em_new_reg(&c->em, "local_read", floating ? MIR_T_D : MIR_T_I64);
    move(c, copy, reg(c, value.reg), floating); value.reg = copy; value.literal = false;
    if (value.number_present) {
        MIR_reg_t present = em_new_reg(&c->em, "local_present", MIR_T_I64);
        move(c, present, reg(c, value.number_present)); value.number_present = present;
    }
    return value;
}
static void scalar_field_write(LmdCompiler* c, LmdScalarField* field, LmdValue value) {
    if (!field->written) { field->value = is_boxed(value) ? read_local_value(c, value) : value; return; }
    bool native = field->kinds == K_NUMBER || field->kinds == K_BOOL;
    bool floating = field->kinds == K_NUMBER && !field->integer;
    if (!field->value.reg) {
        field->value = {em_new_reg(&c->em, "local_field", floating ? MIR_T_D : MIR_T_I64),
            field->kinds | (native ? 0u : (unsigned)K_BOXED), false, 0, field->integer, field->range};
        if (!native) {
            boxed(c, field->value.reg);
            field->home = em_scalar_home_new(&c->em);
        }
    }
    MIR_reg_t stored;
    if (native) stored = scalar_reg(c, value, field->kinds, field->integer);
    else if (!(semantic(value) & K_NUMBER)) stored = box(c, value).reg;
    else {
        MIR_reg_t address = em_materialize_frame_ref(&c->em, em_scalar_home_ref(&c->em, field->home));
        stored = em_adopt_scalar_item_value(&c->em, SCALAR_RETURN_F64, box(c, value).reg, address);
    }
    move(c, field->value.reg, reg(c, stored), floating);
    if (!native) em_scalar_home_bind(&c->em, field->home, field->value.reg);
}
static LmdObjectPlan* value_plan(LmdCompiler* c, AstNode* node) {
    if (node && node->node_type == AST_NODE_IDENT) {
        if (c->inlining && c->inlining->scalar_receiver && named(((AstIdentNode*)node)->name, "this"))
            return c->inlining->scalar_receiver;
        LmdBinding* b = identifier_binding(c->program, (AstIdentNode*)node);
        for (LmdInlineFrame* frame = c->inlining; frame; frame = frame->parent)
            for (int i = 0; i < frame->count; i++) if (frame->bindings[i] == b) return frame->plans[i];
    }
    return object_plan(c->program, node);
}
static MIR_reg_t planned_shape(LmdCompiler* c, LmdObjectPlan* plan) {
    return em_load_at(&c->em, relocated_constant(c, (uint64_t)plan), offsetof(LmdObjectPlan, shape), MIR_T_I64, "literal_shape");
}
static LmdValue field_read(LmdCompiler* c, LmdReference ref) {
    ShapeEntry* field = ref.field;
    TypeId type = field->type->type_id;
    MIR_reg_t data = em_load_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_MAP_DATA, MIR_T_I64, "field_data");
    if (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED)
        return boxed(c, constant(c, type == LMD_TYPE_NULL ? ITEM_NULL : ITEM_JS_UNDEFINED));
    // Packed bool predecessors can leave a word lane unaligned for MIR's target matcher.
    if (type != LMD_TYPE_BOOL && field->byte_offset % sizeof(uint64_t))
        return boxed(c, em_call_2(&c->em, "map_shape_field_to_item", MIR_T_I64,
            MIR_T_P, reg(c, data), MIR_T_P, relocated(c, (uint64_t)field), true));
    MIR_reg_t value = em_load_at(&c->em, data, field->byte_offset,
        type == LMD_TYPE_FLOAT ? MIR_T_D : type == LMD_TYPE_BOOL ? MIR_T_U8 : MIR_T_I64, "field_value");
    if (type == LMD_TYPE_INT) return {value, K_NUMBER, false, 0, true};
    if (type == LMD_TYPE_FLOAT) return {value, K_NUMBER};
    if (type == LMD_TYPE_BOOL) return {value, K_BOOL};
    if (type == LMD_TYPE_STRING)
        value = op(c, MIR_OR, reg(c, value), integer(c, (uint64_t)type << 56));
    return boxed(c, value);
}
static bool field_write(LmdCompiler* c, LmdReference ref, LmdValue value, MIR_label_t miss,
        bool fresh = false, bool* shape_preserved = NULL) {
    TypeId type = ref.field->type->type_id;
    if (type != LMD_TYPE_BOOL && type != LMD_TYPE_NULL && type != LMD_TYPE_UNDEFINED &&
            ref.field->byte_offset % sizeof(uint64_t)) return false;
    unsigned k = semantic(value);
    bool matches = type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT ? k == K_NUMBER :
        type == LMD_TYPE_BOOL ? k == K_BOOL : type == LMD_TYPE_NULL ? k == K_NULL :
        type == LMD_TYPE_UNDEFINED ? k == K_UNDEFINED : type == LMD_TYPE_STRING ? k == K_STRING :
        type == LMD_TYPE_FUNC ? k == K_FUNCTION : type == LMD_TYPE_ARRAY ? k == K_ARRAY :
        type == LMD_TYPE_MAP ? k == K_OBJECT || k == K_MAP : false;
    bool optional_object = type == LMD_TYPE_MAP && (k & K_OBJECT) && !(k & ~(K_OBJECT | K_NULL | K_UNDEFINED));
    // only an unconditional typed store preserves a fresh literal's shape on every path.
    if (shape_preserved) *shape_preserved = matches &&
        (type != LMD_TYPE_INT || value.integer) && type != LMD_TYPE_FLOAT;
    if (!matches && !optional_object) return false;
    if (!matches) branch(c, MIR_BNE, miss, reg(c, tag(c, value)), integer(c, LMD_TYPE_MAP));
    // this guard runs after the RHS: calls and computed keys may have reshaped the owner.
    if (!fresh) em_guard_map_shape(&c->em, ref.owner.reg, planned_shape(c, ref.plan), miss,
        semantic(ref.owner) == K_OBJECT || semantic(ref.owner) == K_MAP);
    MIR_reg_t data = em_load_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_MAP_DATA, MIR_T_I64, "field_data");
    // an unboxed double already proves the float lane; only boxed values need a physical-tag check.
    if (!value.integer && (type == LMD_TYPE_INT || (type == LMD_TYPE_FLOAT && is_boxed(value)))) {
        MIR_reg_t raw = op(c, MIR_URSH, reg(c, box(c, value).reg), integer(c, 56));
        branch(c, type == LMD_TYPE_INT ? MIR_BNE : MIR_BEQ, miss, reg(c, raw), integer(c, LMD_TYPE_INT));
    } else if (type == LMD_TYPE_FLOAT && value.integer) jump(c, miss);
    MIR_reg_t stored = type == LMD_TYPE_INT ? (value.integer ? value.reg : em_unbox_finite_int_item(&c->em, box(c, value).reg)) : type == LMD_TYPE_FLOAT ? to_number(c, value) :
        type == LMD_TYPE_BOOL ? truth(c, value) : type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED ? constant(c, 0) :
        type == LMD_TYPE_STRING || type == LMD_TYPE_FUNC ? payload(c, box(c, value).reg) : box(c, value).reg;
    em_store_at(&c->em, data, ref.field->byte_offset,
        type == LMD_TYPE_FLOAT ? MIR_T_D : type == LMD_TYPE_BOOL || type == LMD_TYPE_UNDEFINED ? MIR_T_U8 : MIR_T_I64, stored);
    return true;
}
// D8.4.1v2: shared shape lookup produces invocation-local metadata, never site feedback.
static MIR_reg_t own_data_field(LmdCompiler* c, MIR_reg_t owner, MIR_reg_t name,
        MIR_label_t miss, MIR_reg_t shape = 0, const LmdShapeSet* predicted = NULL, String* spelling = NULL) {
    if (!shape) {
        em_guard_container(&c->em, owner, LMD_TYPE_MAP, miss);
        branch(c, MIR_BNE, miss, reg(c, em_load_at(&c->em, owner,
            LAMBDA_GC_OFF_CONTAINER_MAP_KIND, MIR_T_U8, "own_map_kind")), integer(c, MAP_KIND_PLAIN));
        shape = em_load_at(&c->em, owner, LAMBDA_GC_OFF_MAP_TYPE, MIR_T_I64, "own_shape");
    }
    MIR_reg_t field = 0;
    MIR_label_t found = NULL;
    if (predicted && spelling) for (int i = 0; i < predicted->count; i++) {
        LmdObjectPlan* plan = predicted->plans[i];
        ShapeEntry* entry = typemap_hash_lookup(plan->blueprint, spelling->chars, spelling->len);
        if (!entry || entry->byte_offset < 0 || (entry->flags & JSPD_IS_ACCESSOR)) continue;
        if (!field) { field = em_new_reg(&c->em, "own_field", MIR_T_I64); found = label(c); }
        MIR_label_t next = label(c);
        // Lambda's inferred-shape route: compare a source recipe, never an execution-learned cell.
        branch(c, MIR_BNE, next, reg(c, shape), reg(c, planned_shape(c, plan)));
        move(c, field, relocated(c, (uint64_t)entry)); jump(c, found); put_label(c, next);
    }
    // foreign declared layouts retain the adapter's ownership and import rules.
    branch(c, MIR_BEQ, miss, reg(c, shape), integer(c, 0));
    // the carrier and shape are already proven; check the same owner directly without reclassification.
    guard_property_nominal(c, shape, miss, true);
    MIR_reg_t resolved = em_call_2(&c->em, "typemap_hash_lookup_key", MIR_T_P,
        MIR_T_P, reg(c, shape), MIR_T_P, reg(c, payload(c, name)), true);
    if (field) { move(c, field, reg(c, resolved)); put_label(c, found); }
    else field = resolved;
    branch_truth(c, miss, field, false);
    branch_truth(c, miss, op(c, MIR_AND, reg(c, em_load_at(&c->em, field,
        offsetof(ShapeEntry, flags), MIR_T_U8, "own_flags")), integer(c, JSPD_IS_ACCESSOR)));
    branch(c, MIR_BLT, miss, reg(c, em_load_at(&c->em, field,
        offsetof(ShapeEntry, byte_offset), MIR_T_I64, "own_offset")), integer(c, 0));
    return field;
}
static MIR_reg_t field_storage(LmdCompiler* c, MIR_reg_t field, MIR_label_t miss) {
    // D3.4.6: Lambda's canonical descriptor owns physical storage, including optional lanes.
    MIR_reg_t descriptor = em_call_1(&c->em, "shape_entry_storage", MIR_T_P,
        MIR_T_P, reg(c, field), true);
    branch(c, MIR_BNE, miss, reg(c, em_load_at(&c->em, descriptor,
        offsetof(LaneStorageDesc, nullable), MIR_T_U8, "field_nullable")), integer(c, 0));
    return descriptor;
}
static MIR_reg_t native_number_field(LmdCompiler* c, MIR_reg_t data, MIR_reg_t field,
        MIR_reg_t descriptor, MIR_label_t miss) {
    MIR_reg_t storage = em_load_at(&c->em, descriptor, offsetof(LaneStorageDesc, kind), MIR_T_U8, "number_storage");
    MIR_reg_t offset = em_load_at(&c->em, field, offsetof(ShapeEntry, byte_offset), MIR_T_I64, "number_offset");
    MIR_reg_t slot = op(c, MIR_ADD, reg(c, data), reg(c, offset));
    MIR_label_t integral = label(c), done = label(c);
    MIR_reg_t value = em_new_reg(&c->em, "field_number", MIR_T_D);
    branch(c, MIR_BEQ, integral, reg(c, storage), integer(c, LANE_STORAGE_INT));
    branch(c, MIR_BNE, miss, reg(c, storage), integer(c, LANE_STORAGE_FLOAT64));
    move(c, value, reg(c, em_load_at(&c->em, slot, 0, MIR_T_D, "field_float")), true);
    jump(c, done); put_label(c, integral);
    MIR_reg_t lane = em_load_at(&c->em, slot, 0, MIR_T_I64, "field_int");
    em_branch_int53_outside(&c->em, lane, miss);
    move(c, value, reg(c, to_number(c, {lane, K_NUMBER, false, 0, true})), true);
    put_label(c, done); return value;
}
static MIR_reg_t property_access(LmdCompiler* c, int operation, MIR_reg_t owner,
        MIR_reg_t name, MIR_reg_t value, String* spelling, bool numeric,
        const LmdValue* native_value, bool plain, unsigned owner_kind, bool computed_own) {
    if (plain && (operation == LMD_PROP_CREATE_DATA || (!c->program->receiver_abi &&
            (operation == LMD_PROP_SET || operation == LMD_PROP_DELETE)))) {
        // literal CreateDataProperty targets are fresh and unexposed, with default own descriptors.
        // Lambda owns their shape transitions and scalar homes, even in reflective units.
        // closed units without Symbol exposure produce only canonical ordinary string keys.
        bool text_key = !c->program->symbols;
        MIR_reg_t key = payload(c, name);
        MIR_reg_t stored = operation != LMD_PROP_DELETE
            ? em_call_3(&c->em, text_key ? "map_shape_set_text" : "map_shape_set", MIR_T_I64, MIR_T_P, reg(c, owner),
                MIR_T_P, reg(c, key), MIR_T_I64, reg(c, value), true)
            : em_call_2(&c->em, text_key ? "map_shape_delete_text" : "map_shape_delete", MIR_T_I64,
                MIR_T_P, reg(c, owner), MIR_T_P, reg(c, key), true);
        // C bool returns use only the low byte, as in the shared field-store path.
        MIR_label_t done = label(c);
        branch_truth(c, done, op(c, MIR_AND, reg(c, stored), integer(c, UINT8_MAX)));
        fail(c, LMD_MVP_MEMORY, NULL); put_label(c, done);
        return operation == LMD_PROP_DELETE ? constant(c, ITEM_TRUE) : value;
    }
    if (c->program->receiver_abi) {
        LmdFunction* active = c->inlining ? c->inlining->function : c->function;
        bool initializing = !c->program->property_callbacks && operation == LMD_PROP_SET && active->home &&
            active->id == active->home->constructor_id;
        // array/string length heads already cover the common path; keep their open miss compact.
        bool sequence_length = named(spelling, "length") && (owner_kind & (K_ARRAY | K_STRING));
        // computed keys on known object carriers share the guarded own-data lookup; misses retain full semantics.
        bool own_key = spelling ?
            !(spelling->len && spelling->chars[0] >= '0' && spelling->chars[0] <= '9') :
            computed_own && (owner_kind == K_OBJECT || owner_kind == K_MAP);
        bool own_read = operation == LMD_PROP_GET && (owner_kind & (K_OBJECT | K_MAP)) &&
            !sequence_length && repeated_execution(c) && !compact_property_dispatch(c) &&
            own_key;
        MIR_reg_t joined = 0;
        MIR_label_t done = NULL, semantic_read = own_read ? label(c) : NULL;
        auto load_field = [&](MIR_reg_t data, MIR_reg_t field) {
            // Lambda owns packed/optional decoding; lazy globals retain semantic lookup.
            MIR_reg_t loaded = em_call_2(&c->em, "map_shape_field_to_item", MIR_T_I64,
                MIR_T_P, reg(c, data), MIR_T_P, reg(c, field), true);
            if (own_read) branch(c, MIR_BEQ, semantic_read, reg(c, loaded), integer(c, ITEM_JS_LAZY_GLOBAL_SENTINEL));
            if (numeric) { check_error(c, loaded); loaded = to_number(c, boxed(c, loaded)); }
            return loaded;
        };
        if (own_read) {
            // this shared own lookup is independent of execution-populated cache state.
            if (!done) {
                done = label(c);
                joined = em_new_reg(&c->em, "own_property", numeric ? MIR_T_D : MIR_T_I64);
            }
            MIR_label_t missing = label(c);
            MIR_reg_t field = own_data_field(c, owner, name, missing);
            // the existing shared decoder also owns optional layouts and numeric consumption.
            MIR_reg_t data = em_load_at(&c->em, owner, LAMBDA_GC_OFF_MAP_DATA, MIR_T_I64, "own_data");
            if (numeric) {
                MIR_label_t decode = label(c);
                MIR_reg_t descriptor = field_storage(c, field, decode);
                move(c, joined, reg(c, native_number_field(c, data, field, descriptor, decode)), true);
                jump(c, done); put_label(c, decode);
            }
            move(c, joined, reg(c, load_field(data, field)), numeric); jump(c, done);
            put_label(c, missing); put_label(c, semantic_read);
        }
        if (native_value && (owner_kind & (K_OBJECT | K_MAP)) && spelling &&
                repeated_execution(c) && !compact_property_dispatch(c) &&
                !(spelling->len && spelling->chars[0] >= '0' && spelling->chars[0] <= '9')) {
            // the evaluated RHS can use an existing writable lane without boxing or a shape transition.
            done = label(c); joined = constant(c, ITEM_JS_UNDEFINED);
            MIR_label_t miss = label(c);
            MIR_reg_t field = own_data_field(c, owner, name, miss);
            branch_truth(c, miss, op(c, MIR_AND, reg(c, em_load_at(&c->em, field,
                offsetof(ShapeEntry, flags), MIR_T_U8, "own_writable")), integer(c, JSPD_NON_WRITABLE)));
            MIR_reg_t descriptor = field_storage(c, field, miss);
            LaneStorageKind lane = semantic(*native_value) == K_BOOL ? LANE_STORAGE_BOOL :
                native_value->integer ? LANE_STORAGE_INT : LANE_STORAGE_FLOAT64;
            branch(c, MIR_BNE, miss, reg(c, em_load_at(&c->em, descriptor,
                offsetof(LaneStorageDesc, kind), MIR_T_U8, "own_storage")), integer(c, lane));
            MIR_reg_t data = em_load_at(&c->em, owner, LAMBDA_GC_OFF_MAP_DATA, MIR_T_I64, "own_data");
            MIR_reg_t slot = op(c, MIR_ADD, reg(c, data), reg(c, em_load_at(&c->em, field,
                offsetof(ShapeEntry, byte_offset), MIR_T_I64, "own_offset")));
            em_store_at(&c->em, slot, 0, lane == LANE_STORAGE_FLOAT64 ? MIR_T_D :
                lane == LANE_STORAGE_BOOL ? MIR_T_U8 : MIR_T_I64, native_value->reg);
            jump(c, done); put_label(c, miss);
        }
        if (native_value) value = box(c, *native_value).reg;
        MIR_reg_t result = em_call_4(&c->em, c->program->property_callbacks ?
            "mvp_lmd_class_property" : native_host_values(c, owner_kind) ?
            "mvp_lmd_class_property_host" : "mvp_lmd_class_property_fixed", MIR_T_I64,
            MIR_T_I64, reg(c, owner), MIR_T_I64, name ? reg(c, name) : integer(c, ITEM_JS_UNDEFINED),
            MIR_T_I64, value ? reg(c, value) : integer(c, ITEM_JS_UNDEFINED),
            MIR_T_I64, integer(c, initializing ? LMD_PROP_INITIALIZE : operation |
                (c->strict && (operation == LMD_PROP_SET || operation == LMD_PROP_DELETE) ? LMD_PROP_STRICT : 0)), true);
        if (numeric) { check_error(c, result); result = to_number(c, boxed(c, result)); }
        if (!done) return result;
        move(c, joined, reg(c, result), numeric); put_label(c, done); return joined;
    }
    if (operation >= LMD_PROP_KEYS)
        return em_call_2(&c->em, c->program->property_callbacks ?
            "mvp_lmd_object_project" : "mvp_lmd_object_project_fixed", MIR_T_I64,
            MIR_T_I64, reg(c, owner), MIR_T_I64, integer(c, operation - LMD_PROP_KEYS), true);
    if (operation == LMD_PROP_DELETE)
        return em_call_2(&c->em, "mvp_lmd_property_delete", MIR_T_I64,
            MIR_T_I64, reg(c, owner), MIR_T_I64, reg(c, name), true);
    const char* helper = operation == LMD_PROP_SET ? "mvp_lmd_property_set" :
        operation >= LMD_PROP_OWN ? "mvp_lmd_property_has" : "mvp_lmd_property_get";
    if (!c->program->property_callbacks) {
        if (operation == LMD_PROP_SET) helper = "mvp_lmd_property_set_fixed";
        else if (operation < LMD_PROP_OWN) helper = "mvp_lmd_property_get_fixed";
    }
    return em_call_3(&c->em, helper, MIR_T_I64, MIR_T_I64, reg(c, owner), MIR_T_I64, reg(c, name),
        MIR_T_I64, operation == LMD_PROP_SET ? reg(c, value) :
            integer(c, operation == LMD_PROP_CALLEE || operation == LMD_PROP_HAS), true);
}
static LmdValue canonical_key(LmdCompiler* c, LmdValue value, bool preserve_number = false) {
    MIR_reg_t result = 0;
    MIR_label_t done = NULL;
    if (preserve_number && (semantic(value) & K_NUMBER)) {
        if (semantic(value) == K_NUMBER) return value;
        result = em_new_reg(&c->em, "index_key", MIR_T_I64);
        move(c, result, reg(c, box(c, value).reg));
        done = label(c);
        // numeric indices need no allocating spelling; other keys convert once before the RHS.
        branch(c, MIR_BEQ, done, reg(c, tag(c, value)), integer(c, LMD_TYPE_FLOAT));
    }
    // primitive keys cannot produce Symbols through reentry; keep native Numbers out of scalar homes.
    constexpr unsigned string_primitives = K_UNDEFINED | K_NULL | K_BOOL | K_NUMBER | K_STRING | K_BIGINT;
    unsigned kinds = semantic(value);
    bool primitive = kinds && !(kinds & ~string_primitives);
    LmdValue string = c->program->symbols && !primitive ? box(c, value) : to_string(c, value);
    MIR_reg_t key = em_call_1(&c->em, c->program->coercion_callbacks ?
        "mvp_lmd_property_key" : "mvp_lmd_property_key_fixed", MIR_T_I64, MIR_T_I64, reg(c, string.reg), true);
    check_error(c, key);
    if (!done) return boxed(c, key, K_STRING);
    move(c, result, reg(c, key)); put_label(c, done);
    return boxed(c, result, K_NUMBER | K_STRING);
}
static MIR_reg_t reference_name(LmdCompiler* c, LmdReference ref) {
    return ref.name ? ref.name : canonical_key(c, ref.deferred_name).reg;
}
static LmdReference reference(LmdCompiler* c, AstNode* n, bool capture_name = false, bool defer_key = false) {
    if (n->node_type == AST_NODE_CALL_EXPR) {
        // Annex B admits sloppy call targets but throws after the call, before RHS or numeric conversion.
        expression(c, n); fail(c, LMD_MVP_REFERENCE, n);
        LmdReference result = {}; result.invalid_target = true; return result;
    }
    if (n->node_type == AST_NODE_IDENT) {
        if (eval_reference_site(c->program, n)) return eval_reference(c, (AstIdentNode*)n);
        LmdBinding* binding = identifier_binding(c->program, (AstIdentNode*)n);
        LmdReference ref = binding && binding->with_lookup ? with_reference(c, binding, n) : LmdReference{binding};
        if (!binding && c->strict && c->program->global_receiver)
            ref.global_presence = global_binding_access(c, n, false, 0, false, NULL, false, true);
        return ref;
    }
    AstFieldNode* field = (AstFieldNode*)n;
    LmdObjectPlan* local = value_plan(c, field->object);
    if (local && local->scalar_binding) {
        String* name = ((AstIdentNode*)field->property)->name;
        ShapeEntry* entry = typemap_hash_lookup(local->blueprint, name->chars, name->len);
        return {NULL, {}, 1, true, 0, 0, local, entry};
    }
    bool continues = field->source_span.start_byte == field->object->source_span.start_byte;
    LmdValue owner = box(c, expression(c, field->object, false, false, continues));
    bool guarded_dense = false;
    if (field->object->node_type == AST_NODE_IDENT) {
        LmdBinding* binding = identifier_binding(c->program, (AstIdentNode*)field->object);
        for (int i = 0; i < c->dense_array_count; i++)
            if (c->dense_arrays[i] == binding) guarded_dense = true;
    }
    if (guarded_dense) owner.kind = K_BOXED | K_ARRAY;
    if (field->optional) {
        branch(c, MIR_BEQ, c->optional_exit, reg(c, owner.reg), integer(c, ITEM_NULL));
        branch(c, MIR_BEQ, c->optional_exit, reg(c, owner.reg), integer(c, ITEM_JS_UNDEFINED));
    }
    if (!field->computed && field->property->node_type == AST_NODE_IDENT &&
            ((AstIdentNode*)field->property)->name->chars[0] == '#') {
        LmdBinding* binding = identifier_binding(c->program, (AstIdentNode*)field->property);
        if (binding && binding->private_name) {
            LmdReference ref = {NULL, owner};
            ref.name = binding_item(c, binding); ref.private_element = true;
            return ref;
        }
    }
    // lexical property names are canonical pooled names; only computed keys need runtime conversion.
    LmdValue key = field->computed ? expression(c, field->property) : boxed(c, relocated_constant(c, s2it(
        name_pool_create_string(c->program->frontend->name_pool, ((AstIdentNode*)field->property)->name))), K_STRING);
    // primitive keys have no coercion effects; retain their existing index/field plans.
    bool defer_conversion = defer_key && field->computed &&
        (semantic(key) & (K_OBJECT | K_ARRAY | K_MAP | K_TYPED_ARRAY | K_FUNCTION));
    if (field->object->node_type == AST_NODE_IDENT && named(((AstIdentNode*)field->object)->name, "super")) {
        LmdReference ref = {NULL, owner};
        if (defer_conversion) { ref.deferred_name = box(c, key); ref.uncoerced_name = true; }
        else ref.name = field->computed ? canonical_key(c, key).reg : key.reg;
        ref.receiver = c->receiver_cell ? em_load_at(&c->em,
            em_array_element_address(&c->em, c->receiver_cell, constant(c, 0), 8), 0, MIR_T_I64, "super_receiver") : c->receiver;
        root_value(c, ref.receiver, JIT_VALUE_BOXED_ITEM);
        return ref;
    }
    if (defer_conversion) {
        LmdReference ref = {NULL, owner, constant(c, -2)};
        ref.deferred_name = box(c, key); ref.uncoerced_name = true; return ref;
    }
    if (field->computed && (semantic(owner) & (K_NULL | K_UNDEFINED)) &&
            (semantic(key) & (K_OBJECT | K_ARRAY | K_MAP | K_TYPED_ARRAY | K_FUNCTION))) {
        // GetValue rejects a nullish base before effectful ToPropertyKey;
        // simple assignment keeps its existing deferred PutValue path above.
        MIR_label_t valid = label(c);
        branch_truth(c, valid, nullish(c, owner), false);
        fail(c, LMD_MVP_TYPE, n); put_label(c, valid);
    }
    LmdValue name = {};
    bool typed = semantic(owner) == K_TYPED_ARRAY;
    bool known = !field->computed || field->property->node_type == AST_NODE_LITERAL;
    int64_t index = -2;
    if (!field->computed) index = mvp_lmd_string_key(((AstIdentNode*)field->property)->name, typed);
    else if (known) {
        AstLiteralNode* literal = (AstLiteralNode*)field->property;
        if (literal->literal_type == AST_LITERAL_STRING) index = mvp_lmd_string_key(literal->value.string_value, typed);
        else if (literal->literal_type == AST_LITERAL_NUMBER) {
            double d = literal->value.number_value;
            index = d >= 0 && d <= (typed ? INT53_MAX : UINT32_MAX - 1.0) && d == trunc(d)
                ? (int64_t)d : typed ? -3 : -2;
        }
    }
    bool integral_key = false;
    if (!known && !key.integer && !c->inlining && semantic(owner) != K_OBJECT && semantic(owner) != K_MAP &&
            field->property->node_type == AST_NODE_IDENT) {
        LmdBinding* binding = identifier_binding(c->program, (AstIdentNode*)field->property);
        if (binding) {
            if (!binding->integral_indices) {
                double lower;
                binding->integral_indices = integer_binding_writes(c->program, binding, false, &lower) ? 2 : 1;
            }
            integral_key = binding->integral_indices == 2;
        }
    }
    if (typed || semantic(owner) == K_ARRAY || semantic(owner) == K_STRING) {
        if (field->computed && semantic(owner) == K_ARRAY && c->program->property_callbacks) {
            // coerce effectful keys once; numeric indices need a spelling only on a named miss.
            key = canonical_key(c, key, true);
        }
        // optional numeric lanes already carry presence and range proofs; preserve their native index path.
        if (!known && !capture_name && !key.number_present && semantic(key) != K_NUMBER && (semantic(key) & K_NUMBER))
            key = canonical_key(c, key, true);
        LmdReference ref = {NULL, owner, known ? constant(c, (uint64_t)index) : property_key(c, key, typed, integral_key), known, index};
        AstNode* source = field->object;
        if (source->node_type == AST_NODE_IDENT) {
            LmdBinding* b = identifier_binding(c->program, (AstIdentNode*)source);
            source = b && b->entry->is_const && b->writes == 1 ? b->initializer : NULL;
        }
        // evaluating the owner above retains TDZ and source effects before using immutable literal facts.
        if (source && source->node_type == AST_NODE_LITERAL &&
                ((AstLiteralNode*)source)->literal_type == AST_LITERAL_STRING)
            ref.string_literal = ((AstLiteralNode*)source)->value.string_value;
        ref.spelling = member_spelling(field);
        LmdArrayFacts facts = array_facts(c->program, field->object);
        ref.lanes = facts.lanes;
        ref.elements = facts.elements;
        ref.numeric_key = semantic(key) == K_NUMBER || key.number_present;
        ref.key_present = key.number_present;
        LmdBinding* array_binding = field->object->node_type == AST_NODE_IDENT ?
            identifier_binding(c->program, (AstIdentNode*)field->object) : NULL;
        ref.dense_storage = guarded_dense || (array_binding && array_binding->dense_array_storage);
        // a present integer element retains its range even when the producer can be absent.
        LmdRange bounds = known ? finite_range(index, index) : range(c->program, field->property, true);
        if (!known && key.integer && key.range.state == 1) {
            // inline/payload facts and the read site's guard are independent proofs; retain both.
            if (bounds.state != 1) bounds = key.range;
            else {
                double lower = fmax(bounds.lower, key.range.lower), upper = fmin(bounds.upper, key.range.upper);
                if (lower <= upper) bounds = finite_range(lower, upper);
            }
        }
        ref.in_bounds = !key.number_present && !(facts.lanes & LMD_ARRAY_UNKNOWN) && bounds.state == 1 && facts.length.state == 1 && bounds.lower >= 0 && bounds.upper < facts.length.lower;
        if (guarded_dense && field->computed) {
            // invalid numeric indices join the bounds replay, before any named-property lookup.
            ref.numeric_key = true;
            return ref;
        }
        // numeric nonindices are absent when the unit cannot write named array properties.
        bool index_only = ref.numeric_key && ((!c->program->property_callbacks && !c->program->array_named_properties) ||
            (!key.number_present && bounds.state == 1 && bounds.lower >= 0 && bounds.upper < UINT32_MAX));
        bool fixed_method = ref.spelling && mvp_lmd_builtin_method(ref.spelling,
            semantic(owner) == K_STRING ? LMD_TYPE_STRING : LMD_TYPE_ARRAY);
        if (capture_name && !c->program->symbols && (typed || semantic(owner) == K_STRING ||
                (fixed_method && !c->program->array_method_overrides))) {
            // immutable builtins need a spelling, not an allocating property-key guard.
            ref.name = to_string(c, key).reg;
        } else if (capture_name || ((semantic(owner) == K_ARRAY || c->program->iterators ||
                (semantic(owner) == K_STRING && c->program->primitives)) && !index_only && (!known || index < -1))) {
            // named string misses need their key even when inlining proves a string receiver.
            if (!capture_name && (semantic(key) & K_NUMBER)) ref.deferred_name = key;
            else ref.name = field->computed ? canonical_key(c, key).reg : key.reg;
        }
        // a nonnumeric raw typed key needs its original value only on the named-property branch.
        if (typed && !ref.name && !ref.deferred_name.reg &&
                (ref.key_known ? ref.index == -2 : !ref.numeric_key || ref.key_present))
            ref.deferred_name = key;
        return ref;
    }
    // primitive numeric conversion has no user effects; defer it until an object needs a name.
    if (field->computed && (semantic(key) & K_NUMBER) && !capture_name &&
            semantic(owner) != K_OBJECT && semantic(owner) != K_MAP) {
        bool numeric = semantic(key) == K_NUMBER;
        if (!numeric) key = canonical_key(c, key, true);
        LmdReference ref = {NULL, owner, known ? constant(c, (uint64_t)index) : property_key(c, key, false, integral_key), known, index};
        ref.numeric_key = numeric; ref.deferred_name = key;
        return ref;
    }
    if (!field->computed) {
        name = key;
    } else name = canonical_key(c, key);
    MIR_reg_t slot = known ? constant(c, (uint64_t)index) : property_key(c, name);
    LmdObjectPlan* plan = known ? value_plan(c, field->object) : NULL;
    ShapeEntry* entry = NULL;
    String* spelling = member_spelling(field);
    if (plan && spelling) entry = typemap_hash_lookup(plan->blueprint, spelling->chars, spelling->len);
    bool guarded = false;
    if (field->object->node_type == AST_NODE_IDENT) {
        LmdBinding* b = identifier_binding(c->program, (AstIdentNode*)field->object);
        for (LmdInlineFrame* frame = c->inlining; frame; frame = frame->parent)
            for (int i = 0; i < frame->count; i++) if (frame->bindings[i] == b) guarded |= frame->guarded[i];
    }
    return {NULL, owner, slot, known, index, name.reg, plan, entry,
        spelling ? mvp_lmd_builtin_method(spelling) : 0, known ? object_shapes(c->program, field->object) : LmdShapeSet{}, spelling, guarded};
}
static void array_key_check(LmdCompiler* c, LmdReference ref, AstNode* site) {
    if (ref.key_known) { if (ref.index < -1) fail(c, LMD_MVP_CAPABILITY, site); }
    else {
        MIR_label_t valid = label(c);
        branch(c, MIR_BGE, valid, reg(c, ref.key), MIR_new_int_op(c->em.ctx, -1));
        fail(c, LMD_MVP_CAPABILITY, site); put_label(c, valid);
    }
}
static void property_failure(LmdCompiler* c, LmdValue owner, AstNode* site) {
    MIR_label_t type = label(c);
    branch_truth(c, type, nullish(c, owner));
    fail(c, LMD_MVP_CAPABILITY, site); put_label(c, type);
    fail(c, LMD_MVP_TYPE, site);
}
static void branch_sparse_array(LmdCompiler* c, MIR_label_t fallback, MIR_reg_t owner) {
    if (c->program->excludes_sparse_arrays) return;
    static_assert(JS_ELEMENTS_SPARSE_TAGGED == 0x80 && JS_ELEMENTS_HOLEY_TAGGED < 0x80 &&
        JS_ELEMENTS_PACKED_TAGGED < 0x80 && JS_ELEMENTS_PACKED_NUMERIC < 0x80 && JS_ELEMENTS_NONE < 0x80,
        "the sparse guard uses the existing element-kind sign bit");
    MIR_reg_t kind = em_load_at(&c->em, owner, offsetof(Container, array_flags), MIR_T_I8, "elements_kind");
    // dense states and layout bits are nonnegative; sparse or future high states use fallback.
    branch(c, MIR_BLT, fallback, reg(c, kind), integer(c, 0));
}
static LmdValue array_read(LmdCompiler* c, LmdReference ref, bool borrow_scalar = false,
        bool numeric = false, bool dense = false) {
    MIR_label_t replay = ref.dense_storage && c->dense_loop ? c->dense_loop->bail : NULL;
    bool callbacks = c->program->property_callbacks && !dense && !replay;
    MIR_reg_t size = em_load_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "array_length");
    if (ref.key_known && ref.index == -1) {
        return {size, K_NUMBER, false, 0, true, finite_range(0, UINT32_MAX)};
    }
    // callbacks can replace numeric contents or change length; retain only the current physical proof.
    if (callbacks || replay) { ref.lanes = LMD_ARRAY_UNKNOWN; ref.in_bounds = false; }
    // numeric consumers can merge absence as NaN at the load, without a second conversion branch.
    bool integral = ref.lanes == LMD_ARRAY_ITEMS && ref.elements.state == 1 && (!numeric || ref.in_bounds);
    bool optional = ref.lanes == LMD_ARRAY_ITEMS && (!numeric || integral) && !ref.in_bounds;
    bool known_numbers = ref.lanes == LMD_ARRAY_ITEMS;
    // a numeric consumer can decode an unknown Item here, before it needs a scalar home.
    numeric |= known_numbers;
    bool floating = numeric && !integral;
    MIR_reg_t present = optional ? constant(c, 0) : 0;
    MIR_label_t length = ref.key_known || ref.numeric_key ? NULL : label(c), done = label(c);
    MIR_reg_t result = em_new_reg(&c->em, "element", floating ? MIR_T_D : MIR_T_I64);
    if (length) branch(c, MIR_BEQ, length, reg(c, ref.key), MIR_new_int_op(c->em.ctx, -1));
    move(c, result, floating ? MIR_new_double_op(c->em.ctx, NAN) : integer(c, integral ? 0 : ITEM_JS_UNDEFINED), floating);
    // sparse storage requires holes; closed hole-free units and numeric literals exclude it.
    MIR_label_t sparse = callbacks ? label(c) : dense || ref.dense_storage || c->program->excludes_sparse_arrays ||
        !c->program->array_holes || ref.lanes == LMD_ARRAY_ITEMS ? NULL : label(c);
    if (sparse) branch_sparse_array(c, sparse, ref.owner.reg);
    if (!ref.in_bounds) branch(c, MIR_UBGE, replay ? replay : callbacks ? sparse : done, reg(c, ref.key), reg(c, size));
    MIR_reg_t element = em_load_at(&c->em,
        em_array_element_address(&c->em, ref.owner.reg, ref.key, 8), 0, MIR_T_I64, "element");
    // specialize proven arrays in hole-free units; retain the generic property fallback's CFG.
    if (!known_numbers && !dense && (replay || callbacks || c->program->array_holes || semantic(ref.owner) != K_ARRAY))
        branch(c, MIR_BEQ, replay ? replay : callbacks ? sparse : done, reg(c, element), integer(c, ITEM_JS_DELETED_SENTINEL));
    move(c, result, reg(c, integral ? em_unbox_finite_int_item(&c->em, element, 0, ref.elements.lower >= 0) :
        numeric ? to_number(c, boxed(c, element, known_numbers ? K_NUMBER : K_ANY)) :
        element), floating);
    if (present) move(c, present, integer(c, 1));
    if (length) {
        jump(c, done); put_label(c, length);
        LmdReference length_ref = ref; length_ref.key_known = true; length_ref.index = -1;
        LmdValue value = array_read(c, length_ref);
        move(c, result, reg(c, integral ? value.reg : numeric ? to_number(c, value) : box(c, value).reg), floating);
        if (present) move(c, present, integer(c, 1));
    }
    if (sparse) {
        jump(c, done); put_label(c, sparse);
        // indexed descriptors clear physical slots, so only misses need semantic lookup.
        MIR_reg_t observed = callbacks ? em_call_4(&c->em, "mvp_lmd_array_index_property", MIR_T_I64,
            MIR_T_I64, reg(c, ref.owner.reg), MIR_T_I64, reg(c, ref.key),
            MIR_T_I64, integer(c, ITEM_JS_UNDEFINED), MIR_T_I64, integer(c, LMD_PROP_GET), true) :
            em_call_2(&c->em, "mvp_lmd_array_own_item", MIR_T_I64,
                MIR_T_P, reg(c, ref.owner.reg), MIR_T_I64, reg(c, ref.key), true);
        if (callbacks) check_error(c, observed);
        branch(c, MIR_BEQ, done, reg(c, observed), integer(c, ITEM_JS_DELETED_SENTINEL));
        move(c, result, reg(c, numeric ? to_number(c, boxed(c, observed)) : observed), floating);
    }
    put_label(c, done); return numeric ? LmdValue{result, present ? K_NUMBER | K_UNDEFINED : K_NUMBER,
        false, 0, integral, length ? LmdRange{} : ref.elements, present} :
        // mutually exclusive storage paths share one destination-owned scalar snapshot.
        boxed(c, !borrow_scalar || callbacks ? stable_item(c, result).reg : result,
            ref.lanes == LMD_ARRAY_ITEMS ? K_NUMBER | K_UNDEFINED : K_ANY);
}
static Item* literal_characters(MvpLmdProgram* p) {
    if (p->character_items) return p->character_items;
    Item* items = (Item*)pool_alloc(p->frontend->pool, 128 * sizeof(Item));
    if (!items) { diagnostic(p, NULL, "character table allocation failed"); return NULL; }
    for (int i = 0; i < 128; i++) {
        char byte = (char)i;
        String* character = get_ascii_char_string((unsigned char)i);
        if (!character) character = name_pool_create_len(p->frontend->name_pool, &byte, 1);
        if (!character) { diagnostic(p, NULL, "character literal allocation failed"); return NULL; }
        items[i].item = s2it(character);
    }
    // entries are static ASCII strings or frontend-owned names, never borrowed GC values.
    p->character_items = items;
    return items;
}
static LmdValue string_read(LmdCompiler* c, LmdReference ref) {
    bool ascii_literal = ref.string_literal && ref.string_literal->is_ascii;
    if (ref.key_known && ref.index == -1) {
        if (ascii_literal) return number(c, ref.string_literal->len);
        MIR_reg_t string = payload(c, ref.owner.reg);
        MIR_reg_t size = em_load_at(&c->em, string, offsetof(String, len), MIR_T_U32, "byte_length");
        MIR_label_t done = label(c);
        MIR_reg_t flags = em_load_at(&c->em, string, offsetof(String, flags), MIR_T_U8, "string_flags");
        branch_truth(c, done, op(c, MIR_AND, reg(c, flags), integer(c, 1)));
        // share the rooted length/cursor cache with character and range reads.
        move(c, size, reg(c, em_call_1(&c->em, "js_string_utf16_length", MIR_T_I64,
            MIR_T_P, reg(c, string), true)));
        put_label(c, done);
        return {size, K_NUMBER, false, 0, true, finite_range(0, UINT32_MAX)};
    }
    Item* characters = literal_characters(c->program);
    if (!characters) return boxed(c, constant(c, ITEM_JS_UNDEFINED));
    // numeric keys cannot select length; retain the character result kind through consumers.
    MIR_label_t length = ref.key_known || ref.numeric_key ? NULL : label(c), done = label(c);
    MIR_label_t slow = ascii_literal ? NULL : label(c);
    MIR_reg_t result = em_new_reg(&c->em, "string_property", MIR_T_I64);
    if (length) branch(c, MIR_BEQ, length, reg(c, ref.key), MIR_new_int_op(c->em.ctx, -1));
    MIR_reg_t string = ascii_literal ? relocated_constant(c, (uint64_t)ref.string_literal) : payload(c, ref.owner.reg);
    if (slow) {
        MIR_reg_t flags = em_load_at(&c->em, string, offsetof(String, flags), MIR_T_U8, "string_flags");
        branch_truth(c, slow, op(c, MIR_AND, reg(c, flags), integer(c, 1)), false);
    }
    move(c, result, integer(c, ITEM_JS_UNDEFINED));
    MIR_reg_t size = ascii_literal ? constant(c, ref.string_literal->len) :
        em_load_at(&c->em, string, offsetof(String, len), MIR_T_U32, "string_bytes");
    branch(c, MIR_UBGE, done, reg(c, ref.key), reg(c, size));
    MIR_reg_t address = op(c, MIR_ADD, reg(c, string), reg(c, ref.key));
    MIR_reg_t unit = em_load_at(&c->em, address, offsetof(String, chars), MIR_T_U8, "ascii_unit");
    MIR_reg_t entry = op(c, MIR_ADD, relocated(c, (uint64_t)characters),
        reg(c, op(c, MIR_LSH, reg(c, unit), integer(c, 3))));
    move(c, result, reg(c, em_load_at(&c->em, entry, 0, MIR_T_I64, "ascii_character")));
    if (slow) {
        jump(c, done); put_label(c, slow);
        MIR_reg_t at = em_call_3(&c->em, "mvp_lmd_string_at", MIR_T_I64,
            MIR_T_I64, reg(c, ref.owner.reg), MIR_T_D, reg(c, to_number(c, {ref.key, K_NUMBER, false, 0, true})),
            MIR_T_I64, integer(c, LMD_STRING_INDEX), true);
        check_error(c, at); move(c, result, reg(c, at));
    }
    if (length) {
        jump(c, done); put_label(c, length);
        LmdReference length_ref = ref; length_ref.key_known = true; length_ref.index = -1;
        move(c, result, reg(c, box(c, string_read(c, length_ref)).reg));
    }
    put_label(c, done);
    return boxed(c, result, length ? K_ANY : K_STRING | K_UNDEFINED);
}
// JS admission/coercion precedes these shared physical lane operations (D2.4.3).
static LmdValue typed_array_lanes(LmdCompiler* c, LmdValue owner, MIR_reg_t index,
        LmdValue* value = NULL, MIR_reg_t end = 0, unsigned lanes = 0, bool native = false) {
    if (!lanes || (lanes & ~7u)) lanes = 7;
    bool single = !(lanes & (lanes - 1)), integral = !(lanes & 4);
    MIR_reg_t lane = single ? 0 : em_load_at(&c->em, owner.reg, LAMBDA_GC_OFF_CONTAINER_MAP_KIND, MIR_T_U8, "numeric_lane");
    MIR_label_t done = label(c);
    bool floating_result = native && !integral;
    MIR_reg_t result = value ? 0 : em_new_reg(&c->em, "typed_element", floating_result ? MIR_T_D : MIR_T_I64);
    MIR_reg_t converted = 0;
    if (value && lanes != 4) {
        MIR_label_t floating = label(c);
        if (lanes & 4) branch(c, MIR_BEQ, floating, reg(c, lane), integer(c, ELEM_FLOAT64));
        converted = int32(c, *value);
        put_label(c, floating);
    }
    for (int i = 0; i < 3; i++) {
        if (!(lanes & (1u << i))) continue;
        ArrayNumElemType element = typed_lanes[i];
        MIR_type_t storage = em_numeric_storage_type(element);
        MIR_label_t next = label(c);
        if (!single) branch(c, MIR_BNE, next, reg(c, lane), integer(c, element));
        MIR_label_t repeat = end ? label(c) : NULL;
        if (end) {
            put_label(c, repeat);
            branch(c, MIR_UBGE, done, reg(c, index), reg(c, end));
        }
        MIR_reg_t address = em_array_element_address(&c->em, owner.reg, index,
            ELEM_TYPE_SIZE[element >> 4]);
        if (value) em_store_at(&c->em, address, 0, storage,
            storage == MIR_T_D ? to_number(c, *value) : converted);
        else {
            MIR_reg_t loaded = em_load_at(&c->em, address, 0, storage, "numeric_element");
            LmdValue v = {loaded, K_NUMBER, false, 0, storage != MIR_T_D};
            move(c, result, reg(c, native ? (floating_result ? to_number(c, v) : loaded) : box(c, v).reg), floating_result);
        }
        if (end) { move(c, index, reg(c, op(c, MIR_ADD, reg(c, index), integer(c, 1)))); jump(c, repeat); }
        else jump(c, done);
        put_label(c, next);
    }
    if (!single) fail(c, LMD_MVP_CAPABILITY, NULL);
    put_label(c, done);
    return value ? owner : native ? LmdValue{result, K_NUMBER, false, 0, integral,
        integral ? finite_range(lanes & 1 ? INT32_MIN : 0, lanes & 1 ? INT32_MAX : UINT8_MAX) : LmdRange{0, 0, 2}}
        : boxed(c, result, K_NUMBER);
}
static LmdValue typed_array_reference(LmdCompiler* c, LmdReference ref, AstNode* site,
        LmdValue* value = NULL, bool callee = false, bool numeric = false) {
    if (ref.deferred_name.reg && !(semantic(ref.owner) == K_TYPED_ARRAY && !ref.uncoerced_name)) {
        ref.key = property_key(c, ref.deferred_name, true);
        ref.key_known = false;
    } else if (ref.spelling) {
        // lexical and literal names already have an immutable typed-index classification.
        ref.index = mvp_lmd_string_key(ref.spelling, true);
        ref.key = constant(c, (uint64_t)ref.index);
        ref.key_known = true;
    } else if (ref.name) {
        ref.key = property_key(c, boxed(c, ref.name, K_STRING), true);
        ref.key_known = false;
    }
    MIR_reg_t size = em_load_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "typed_length");
    if (ref.key_known) {
        if (ref.index == -1) {
            if (!value) return {size, K_NUMBER, false, 0, true, finite_range(0, INT53_MAX)};
            if (c->strict) fail(c, LMD_MVP_TYPE, site);
            return *value;
        }
        if (ref.index == -4 && callee && !value)
            return boxed(c, constant(c, mvp_lmd_method_token(9)));
        if (ref.index == -4) {
            fail(c, LMD_MVP_CAPABILITY, site); return boxed(c, constant(c, ITEM_JS_UNDEFINED));
        }
    }
    if (ref.key_present || (ref.key_known ? ref.index == -2 : !ref.numeric_key)) {
        unsigned result_kind = !callee && site && (site->node_type == AST_NODE_MEMBER_EXPR || site->node_type == AST_NODE_INDEX_EXPR)
            ? kind(c->program, site, c->inlining) : K_ANY;
        bool scalar = !value && !callee && result_kind && !(result_kind & ~(K_NUMBER | K_UNDEFINED));
        LmdRange bounds = scalar ? range(c->program, site, false, true) : LmdRange{};
        bool integral = scalar && !numeric && bounds.state == 1;
        bool floating = numeric || (scalar && !integral);
        MIR_reg_t present = scalar && !numeric ? constant(c, 0) : 0;
        MIR_reg_t result = value ? 0 : em_new_reg(&c->em, "typed_property", floating ? MIR_T_D : MIR_T_I64);
        auto save = [&](LmdValue output) {
            if (present) move(c, present, reg(c, output.number_present ? output.number_present :
                op(c, MIR_NE, reg(c, tag(c, output)), integer(c, LMD_TYPE_UNDEFINED))));
            move(c, result, reg(c, scalar ? scalar_reg(c, output, result_kind, integral) :
                numeric ? to_number(c, output) : box(c, output).reg), floating);
        };
        MIR_label_t named = label(c), done = label(c);
        if (!ref.key_known) {
            if (ref.key_present) branch_truth(c, named, ref.key_present, false);
            else branch(c, MIR_BEQ, named, reg(c, ref.key), integer(c, -2));
            LmdReference indexed = ref;
            indexed.numeric_key = true; indexed.key_present = 0;
            indexed.name = 0; indexed.deferred_name = {};
            LmdValue fast = typed_array_reference(c, indexed, site, value, callee, numeric);
            if (!value) save(fast);
            jump(c, done);
        }
        put_label(c, named);
        MIR_reg_t name = reference_name(c, ref);
        // named stores retain the original RHS; numeric conversion belongs only to indexed writes.
        MIR_reg_t stored = value ? box(c, *value).reg : 0;
        MIR_reg_t property = em_call_3(&c->em, value ? "mvp_lmd_property_set_fixed" : "mvp_lmd_property_get_fixed", MIR_T_I64,
            MIR_T_I64, reg(c, ref.owner.reg), MIR_T_I64, reg(c, name),
            MIR_T_I64, value ? reg(c, stored) : integer(c, callee), true);
        check_error(c, property);
        if (!value) {
            // numeric payloads are consumed before another safepoint; general Items retain an owned home.
            save(scalar ? boxed(c, property, result_kind) : stable_item(c, property));
        }
        put_label(c, done);
        return value ? *value : numeric ? LmdValue{result, K_NUMBER} : scalar ?
            LmdValue{result, result_kind, false, 0, integral, bounds, present} : boxed(c, result, result_kind);
    }
    bool native = !value && !callee;
    bool integral = native && ref.lanes && !(ref.lanes & ~3u) && (!numeric || ref.in_bounds);
    bool floating = native && !integral;
    MIR_reg_t present = native && (!numeric || integral) ? constant(c, 0) : 0;
    MIR_reg_t result = floating ? to_number(c, number(c, NAN)) : constant(c, integral ? 0 : ITEM_JS_UNDEFINED);
    MIR_label_t length = label(c), fill = label(c), unsupported = label(c), done = label(c);
    bool named_key = !ref.key_known && !ref.numeric_key;
    if (ref.key_present) {
        MIR_label_t present_key = label(c);
        branch_truth(c, present_key, ref.key_present);
        fail(c, LMD_MVP_CAPABILITY, site); put_label(c, present_key);
    }
    if (named_key) {
        branch(c, MIR_BEQ, length, reg(c, ref.key), MIR_new_int_op(c->em.ctx, -1));
        branch(c, MIR_BEQ, fill, reg(c, ref.key), MIR_new_int_op(c->em.ctx, -4));
        branch(c, MIR_BEQ, unsupported, reg(c, ref.key), MIR_new_int_op(c->em.ctx, -2));
    }
    // even an invalid canonical numeric index performs ToNumber on the RHS.
    LmdValue stored = value ? *value : LmdValue{};
    if (value && semantic(stored) != K_NUMBER && !(stored.integer && stored.number_present))
        stored = {to_number(c, stored), K_NUMBER};
    if (!ref.in_bounds) branch(c, MIR_UBGE, done, reg(c, ref.key), reg(c, size));
    LmdValue accessed = typed_array_lanes(c, ref.owner, ref.key, value ? &stored : NULL, 0, ref.lanes, native);
    if (ref.in_bounds) return value ? *value : accessed;
    if (!value) move(c, result, reg(c, floating ? to_number(c, accessed) : accessed.reg), floating);
    if (present) move(c, present, integer(c, 1));
    if (named_key) {
        jump(c, done);
        put_label(c, length);
        if (value) { if (c->strict) fail(c, LMD_MVP_TYPE, site); }
        else {
            LmdValue length_value = {size, K_NUMBER, false, 0, true};
            move(c, result, reg(c, integral ? size : native ? to_number(c, length_value) : box(c, length_value).reg), floating);
            if (present) move(c, present, integer(c, 1));
        }
        jump(c, done);
        put_label(c, fill);
        if (callee && !value) { move(c, result, integer(c, mvp_lmd_method_token(9))); jump(c, done); }
        put_label(c, unsupported); fail(c, LMD_MVP_CAPABILITY, site);
    }
    put_label(c, done); return value ? *value : native ?
        LmdValue{result, present ? K_NUMBER | K_UNDEFINED : K_NUMBER, false, 0, integral,
            named_key ? LmdRange{} : accessed.range, present} :
        boxed(c, result, callee ? K_ANY : K_NUMBER | K_UNDEFINED);
}
static LmdValue sequence_reference(LmdCompiler* c, LmdReference ref, unsigned owner_kind,
        AstNode* site, bool callee, bool borrow_scalar = false, bool numeric = false) {
    int method = ref.spelling ? mvp_lmd_builtin_method(ref.spelling,
        owner_kind == K_ARRAY ? LMD_TYPE_ARRAY : LMD_TYPE_STRING) : 0;
    if (callee && method && method < LMD_METHOD_STRING_CONCAT && owner_kind == K_STRING &&
            !c->program->primitive_method_overrides)
        return boxed(c, constant(c, mvp_lmd_method_token(method)));
    if (callee && method && owner_kind == K_ARRAY && !c->program->array_method_overrides)
        return boxed(c, constant(c, mvp_lmd_method_token(method)));
    if ((owner_kind == K_ARRAY || c->program->iterators || (owner_kind == K_STRING && c->program->primitives)) && (ref.name || ref.deferred_name.reg) &&
            !(ref.key_known && ref.index >= -1)) {
        MIR_label_t indexed = ref.key_known ? NULL : label(c), done = label(c);
        MIR_reg_t result = em_new_reg(&c->em, "array_property", MIR_T_I64);
        if (!ref.key_known) branch(c, MIR_BGE, indexed, reg(c, ref.key), integer(c, -1));
        if (callee && method && owner_kind == K_ARRAY && !c->program->property_callbacks) {
            MIR_label_t attributes = label(c);
            branch(c, MIR_BNE, attributes, reg(c, em_load_at(&c->em, ref.owner.reg,
                LAMBDA_GC_OFF_MAP_TYPE, MIR_T_I64, "array_attributes")), integer(c, 0));
            move(c, result, integer(c, mvp_lmd_method_token(method))); jump(c, done);
            put_label(c, attributes);
        }
        MIR_reg_t value = c->program->library_active ?
            em_call_4(&c->em, c->program->property_callbacks ?
                "mvp_lmd_array_method_value" : "mvp_lmd_array_method_value_fixed", MIR_T_I64, MIR_T_P, reg(c, c->unit),
                MIR_T_I64, reg(c, ref.owner.reg), MIR_T_I64, reg(c, reference_name(c, ref)), MIR_T_I64, integer(c, callee), true) :
            property_access(c, callee ? LMD_PROP_CALLEE : LMD_PROP_GET, ref.owner.reg, reference_name(c, ref));
        check_error(c, value);
        move(c, result, reg(c, value)); jump(c, done);
        // a statically named property has no indexed alternative or its scalar homes.
        if (indexed) {
            put_label(c, indexed);
            move(c, result, reg(c, box(c, owner_kind == K_ARRAY ? array_read(c, ref, true, numeric) : string_read(c, ref)).reg));
        }
        // snapshot after the branch joins, before a later operand can mutate the source owner.
        put_label(c, done); return boxed(c, borrow_scalar || callee ? result : stable_item(c, result).reg);
    }
    if (!callee || (ref.key_known && ref.index >= -1)) {
        // invalid numeric keys fail the unsigned bounds guard and preserve the optional lane.
        if (!(owner_kind == K_ARRAY && ((ref.dense_storage && c->dense_loop) ||
                (ref.numeric_key && !c->program->array_named_properties))))
            array_key_check(c, ref, site);
        return owner_kind == K_ARRAY ? array_read(c, ref, borrow_scalar, numeric) : string_read(c, ref);
    }
    if (method) return boxed(c, constant(c, mvp_lmd_method_token(method)));
    MIR_label_t indexed = label(c), done = label(c);
    MIR_reg_t result = em_new_reg(&c->em, "sequence_property", MIR_T_I64);
    branch(c, MIR_BGE, indexed, reg(c, ref.key), MIR_new_int_op(c->em.ctx, -1));
    MIR_reg_t member = property_access(c, LMD_PROP_CALLEE, ref.owner.reg, ref.name);
    check_error(c, member); move(c, result, reg(c, member)); jump(c, done);
    put_label(c, indexed);
    move(c, result, reg(c, box(c, owner_kind == K_ARRAY ? array_read(c, ref) : string_read(c, ref)).reg));
    put_label(c, done); return boxed(c, result);
}
static void branch_virtual_object(LmdCompiler* c, MIR_reg_t type, MIR_label_t target) {
    // public realms admit all host virtual carriers; closed units retain their proven surface.
    if (c->program->promises || c->program->module_realm)
        branch(c, MIR_BEQ, target, reg(c, type), integer(c, LMD_TYPE_VMAP));
    if (c->program->module_realm) {
        branch(c, MIR_BEQ, target, reg(c, type), integer(c, LMD_TYPE_VARRAY));
        branch(c, MIR_BEQ, target, reg(c, type), integer(c, LMD_TYPE_VELMT));
    }
}
static LmdValue read_reference(LmdCompiler* c, LmdReference ref, AstNode* site, bool callee = false,
        bool borrow_scalar = false, bool numeric = false) {
    if (ref.invalid_target) return boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
    if (ref.eval_reference) return eval_reference_operation(c, ref.eval_reference, JS_INTERP_REFERENCE_GET);
    if (ref.private_element) {
        MIR_reg_t result = em_call_4(&c->em, "mvp_lmd_private_element", MIR_T_I64,
            MIR_T_I64, reg(c, ref.owner.reg), MIR_T_I64, reg(c, ref.name), MIR_T_I64, integer(c, ITEM_JS_UNDEFINED),
            MIR_T_I64, integer(c, LMD_PRIVATE_GET), true);
        check_error(c, result); return stable_item(c, result);
    }
    if (ref.with_object) return with_reference_operation(c, ref, site, LMD_PROP_GET);
    if (ref.receiver) {
        MIR_reg_t result = em_call_5(&c->em, "mvp_lmd_property_with_receiver", MIR_T_I64,
            MIR_T_I64, reg(c, ref.owner.reg), MIR_T_I64, reg(c, ref.name), MIR_T_I64, integer(c, ITEM_JS_UNDEFINED),
            MIR_T_I64, integer(c, LMD_PROP_GET), MIR_T_I64, reg(c, ref.receiver), true);
        check_error(c, result); return stable_item(c, result);
    }

    LmdFunction* active = c->inlining ? c->inlining->function : c->function;
    LmdRegionRead* read = numeric_region_read(c->program, active->numeric_region, site);
    if (read && c->inlining && c->inlining->region == read->region)
        return c->inlining->region_values[read->index];
    if (!ref.key) return read_binding(c, ref.binding, site, borrow_scalar);
    if (LmdScalarField* field = scalar_reference(ref)) return read_local_value(c, field->value);
    if (ref.guarded && ref.field) return field_read(c, ref);
    unsigned k = semantic(ref.owner);
    if (k == K_TYPED_ARRAY) return typed_array_reference(c, ref, site, NULL, callee, numeric);
    if (k == K_ARRAY || k == K_STRING) {
        return sequence_reference(c, ref, k, site, callee, borrow_scalar, numeric);
    }
    if (callee && k == K_MAP && ref.spelling && !c->program->map_method_overrides) {
        // the closed-unit write proof excludes own method replacements; iterators still need Function values.
        int method = mvp_lmd_builtin_method(ref.spelling);
        if (method && (!c->program->iterators || method <= 5))
            return boxed(c, constant(c, mvp_lmd_method_token(method)));
    }
    if (callee && (k == K_NUMBER || k == K_BIGINT) && ref.spelling && !c->program->primitive_method_overrides) {
        int method = mvp_lmd_builtin_method(ref.spelling, k == K_BIGINT ? LMD_TYPE_DECIMAL : LMD_TYPE_FLOAT);
        if (method) return boxed(c, constant(c, mvp_lmd_method_token(method)));
    }
    bool consume = numeric && c->program->receiver_abi;
    // cold host reads use the same complete reader without emitting every carrier arm.
    if (c->program->library_active && (c->program->primitives || cold_host_operation(c)) && c->program->receiver_abi &&
            (!repeated_execution(c) || compact_property_dispatch(c)) &&
            (!k || (k & (k - 1))) && !ref.guarded && !ref.plan && !ref.shapes.count &&
            ref.name && !ref.deferred_name.reg && !ref.uncoerced_name) {
        // compact generic reads share runtime dispatch; specialized and deferred-key proofs stay intact.
        MIR_reg_t value = em_call_4(&c->em, "mvp_lmd_property_read", MIR_T_I64,
            MIR_T_P, reg(c, c->unit), MIR_T_I64, reg(c, ref.owner.reg),
            MIR_T_I64, reg(c, ref.name), MIR_T_I64, integer(c, callee), true);
        check_error(c, value);
        unsigned result_kind = !callee && site && (site->node_type == AST_NODE_MEMBER_EXPR || site->node_type == AST_NODE_INDEX_EXPR)
            ? kind(c->program, site, c->inlining) : K_ANY;
        // the single result owns its scalar before any later operand can mutate the property.
        LmdValue result = boxed(c, (result_kind & K_NUMBER) ? stable_item(c, value).reg : value, result_kind);
        return consume ? LmdValue{to_number(c, result), K_NUMBER} : result;
    }
    MIR_reg_t result = em_new_reg(&c->em, "property", consume ? MIR_T_D : MIR_T_I64);
    auto save = [&](LmdValue value) {
        move(c, result, reg(c, consume ? to_number(c, value) : box(c, value).reg), consume);
    };
    MIR_label_t array = label(c), string = label(c), typed = label(c), object = label(c), done = label(c);
    if (k != K_OBJECT && k != K_MAP) {
        MIR_reg_t tid = tag(c, ref.owner);
        branch(c, MIR_BEQ, array, reg(c, tid), integer(c, LMD_TYPE_ARRAY));
        branch(c, MIR_BEQ, string, reg(c, tid), integer(c, LMD_TYPE_STRING));
        branch(c, MIR_BEQ, typed, reg(c, tid), integer(c, LMD_TYPE_ARRAY_NUM));
        branch(c, MIR_BEQ, object, reg(c, tid), integer(c, LMD_TYPE_MAP));
        branch_virtual_object(c, tid, object);
        if (c->program->symbols) branch(c, MIR_BEQ, object, reg(c, tid), integer(c, LMD_TYPE_SYMBOL));
        if (c->program->receiver_abi) branch(c, MIR_BEQ, object, reg(c, tid), integer(c, LMD_TYPE_FUNC));
        // planned Number prototypes can supply inherited names beyond the fixed native method catalog.
        if (c->program->primitives || (callee && (!ref.spelling || mvp_lmd_builtin_method(ref.spelling, LMD_TYPE_FLOAT) ||
                named(ref.spelling, "toLocaleString")))) {
            branch(c, MIR_BEQ, object, reg(c, tid), integer(c, LMD_TYPE_FLOAT));
            branch(c, MIR_BEQ, object, reg(c, tid), integer(c, LMD_TYPE_INT));
        }
        // compact calls read an ordinary Function value; primitive receivers still use their realm prototypes.
        if (c->program->primitives)
            branch(c, MIR_BEQ, object, reg(c, tid), integer(c, LMD_TYPE_BOOL));
        if (c->program->bigints && (c->program->primitives || (callee && (!ref.spelling || named(ref.spelling, "toString") ||
                named(ref.spelling, "toLocaleString")))))
            branch(c, MIR_BEQ, object, reg(c, tid), integer(c, LMD_TYPE_DECIMAL));
        property_failure(c, ref.owner, site);
        put_label(c, array);
        // immediate conversion consumes the borrowed scalar before another operand can mutate it.
        save(sequence_reference(c, ref, K_ARRAY, site, callee, borrow_scalar || consume, consume)); jump(c, done);
        put_label(c, string);
        save(sequence_reference(c, ref, K_STRING, site, callee, borrow_scalar || consume, consume)); jump(c, done);
        put_label(c, typed);
        save(typed_array_reference(c, ref, site, NULL, callee)); jump(c, done);
    }
    put_label(c, object);
    if (callee && ref.method && (!c->program->iterators || ref.method <= 5)) {
        MIR_label_t miss = label(c);
        // capture the builtin before arguments run; the empty attribute shape excludes own overrides.
        TypeMap* shape = c->program->iterators ? &c->program->library->map.shape : c->program->roots[1];
        em_guard_map_shape(&c->em, ref.owner.reg, relocated_constant(c, (uint64_t)shape), miss,
            k == K_OBJECT || k == K_MAP);
        move(c, result, integer(c, mvp_lmd_method_token(ref.method))); jump(c, done);
        put_label(c, miss);
    }
    LmdShapeSet shapes = ref.shapes.count ? ref.shapes : LmdShapeSet{ref.plan ? 1 : 0, {ref.plan}};
    if (c->program->deletes_properties && shapes.count && !callee && site &&
            enclosing_loop(c->program, site, active, true)) {
        // predicted successors are fixed before execution; every field load still guards shape identity.
        int originals = shapes.count;
        for (int i = 0; i < c->program->objects->length && shapes.count < 4; i++) {
            LmdObjectPlan* plan = (LmdObjectPlan*)c->program->objects->data[i];
            if (plan->literal->node_type != AST_NODE_UNARY) continue;
            for (int j = 0; j < originals; j++)
                if (((LmdDeletedShapePlan*)plan)->source == shapes.plans[j])
                    shape_union(&shapes, {1, {plan}});
        }
    }
    for (int i = 0; i < shapes.count; i++) {
        LmdReference candidate = ref; candidate.plan = shapes.plans[i];
        if (ref.spelling) candidate.field = typemap_hash_lookup(candidate.plan->blueprint,
            ref.spelling->chars, ref.spelling->len);
        if (!candidate.field) continue;
        MIR_label_t miss = label(c);
        em_guard_map_shape(&c->em, ref.owner.reg, planned_shape(c, candidate.plan), miss,
            k == K_OBJECT || k == K_MAP);
        save(field_read(c, candidate)); jump(c, done);
        put_label(c, miss);
    }
    auto prototype_method = [&](MIR_reg_t prototype, MIR_reg_t expected,
            ShapeEntry* field, MIR_label_t miss) {
        em_guard_map_shape(&c->em, prototype, expected, miss, false);
        LmdReference predicted = {}; predicted.owner = boxed(c, prototype, K_OBJECT);
        predicted.field = field;
        LmdValue method = field_read(c, predicted);
        // a nullable packed function must take canonical decoding rather than publish a zero Item.
        branch_truth(c, miss, method.reg, false);
        save(method); jump(c, done);
    };
    if (callee && ref.spelling && repeated_execution(c) && !compact_property_dispatch(c)) {
        MvpLmdClass* classes[4]; ShapeEntry* fields[4]; int count = 0;
        for (int i = 0; c->program->classes && i < c->program->classes->length; i++) {
            MvpLmdClass* cls = (MvpLmdClass*)c->program->classes->data[i];
            ShapeEntry* field = cls->dynamic ? NULL : typemap_hash_lookup(&cls->prototype_shape,
                ref.spelling->chars, ref.spelling->len);
            if (!field || field->type->type_id != LMD_TYPE_FUNC || (field->flags & JSPD_IS_ACCESSOR)) continue;
            if (count == 4) { count = 0; break; }
            classes[count] = cls; fields[count++] = field;
        }
        if (count) {
            MIR_label_t miss = label(c), admitted = label(c);
            em_guard_container(&c->em, ref.owner.reg, LMD_TYPE_MAP, miss);
            MIR_reg_t shape = em_load_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_MAP_TYPE, MIR_T_I64, "method_shape");
            branch(c, MIR_BEQ, miss, reg(c, shape), integer(c, 0));
            TypeMap marker = {};
            MIR_reg_t nominal = em_load_at(&c->em, shape,
                (char*)&marker.nominal - (char*)&marker, MIR_T_I64, "method_nominal");
            for (int i = 0; i < count; i++)
                branch(c, i + 1 == count ? MIR_BNE : MIR_BEQ, i + 1 == count ? miss : admitted,
                    reg(c, nominal), relocated(c, (uint64_t)&classes[i]->nominal));
            put_label(c, admitted);
            // source nominal identity selects a candidate; a live own shadow still wins.
            MIR_reg_t own = em_call_2(&c->em, "typemap_hash_lookup_key", MIR_T_P,
                MIR_T_P, reg(c, shape), MIR_T_P, reg(c, payload(c, reference_name(c, ref))), true);
            branch_truth(c, miss, own);
            MIR_reg_t actual = 0;
            if (c->program->property_callbacks) {
                actual = em_call_1(&c->em, "mvp_lmd_get_prototype", MIR_T_I64,
                    MIR_T_I64, reg(c, ref.owner.reg), true);
                check_error(c, actual);
            }
            for (int i = 0; i < count; i++) {
                MIR_label_t next = i + 1 == count ? miss : label(c);
                if (count > 1) branch(c, MIR_BNE, next, reg(c, nominal),
                    relocated(c, (uint64_t)&classes[i]->nominal));
                MIR_reg_t prototype = actual;
                if (!prototype) {
                    // the existing closed-unit proof excludes prototype replacement and foreign newTarget.
                    MIR_reg_t values = em_load_at(&c->em, relocated_constant(c, (uint64_t)&classes[i]->values),
                        0, MIR_T_I64, "method_values");
                    prototype = em_load_at(&c->em, values, sizeof(Item), MIR_T_I64, "method_prototype");
                }
                prototype_method(prototype, relocated_constant(c, (uint64_t)&classes[i]->prototype_shape), fields[i], miss);
                if (next != miss) put_label(c, next);
            }
            put_label(c, miss);
        }
    }
    if (callee && ref.spelling && c->program->function_construction &&
            repeated_execution(c) && !compact_property_dispatch(c)) {
        LmdObjectPlan* plans[4]; ShapeEntry* fields[4]; int count = 0;
        for (int i = 0; i < c->program->objects->length; i++) {
            LmdObjectPlan* plan = (LmdObjectPlan*)c->program->objects->data[i];
            if (!plan->prototype_owner) continue;
            ShapeEntry* field = typemap_hash_lookup(plan->blueprint, ref.spelling->chars, ref.spelling->len);
            if (!field || field->type->type_id != LMD_TYPE_FUNC || (field->flags & JSPD_IS_ACCESSOR)) continue;
            if (count == 4) { count = 0; break; }
            plans[count] = plan; fields[count++] = field;
        }
        if (count) {
            MIR_label_t miss = label(c);
            MIR_reg_t constructor = 0;
            MIR_reg_t shape = guard_function_instance(c, ref.owner.reg, miss, &constructor);
            MIR_reg_t entry = em_load_at(&c->em, constructor, offsetof(Function, ptr), MIR_T_I64, "prototype_constructor");
            MIR_label_t admitted = label(c);
            for (int i = 0; i < count; i++)
                branch(c, i + 1 == count ? MIR_BNE : MIR_BEQ, i + 1 == count ? miss : admitted,
                    reg(c, entry), MIR_new_ref_op(c->em.ctx, plans[i]->prototype_owner->forward));
            put_label(c, admitted);
            // the live own descriptor wins even when it shadows a source prototype method.
            MIR_reg_t field = em_call_2(&c->em, "typemap_hash_lookup_key", MIR_T_P,
                MIR_T_P, reg(c, shape), MIR_T_P, reg(c, payload(c, reference_name(c, ref))), true);
            branch_truth(c, miss, field);
            MIR_reg_t prototype = em_call_1(&c->em, "mvp_lmd_get_prototype", MIR_T_I64,
                MIR_T_I64, reg(c, ref.owner.reg), true);
            check_error(c, prototype);
            for (int i = 0; i < count; i++) {
                MIR_label_t next = i + 1 == count ? miss : label(c);
                // load the live method before arguments; same-shaped replacements remain observable.
                prototype_method(prototype, planned_shape(c, plans[i]), fields[i], next);
                if (next != miss) put_label(c, next);
            }
            put_label(c, miss);
        }
    }
    MIR_reg_t name = reference_name(c, ref), own_value = 0;
    MIR_label_t own_done = NULL;
    if (!callee && k == K_OBJECT && !c->program->receiver_abi && !c->program->property_callbacks) {
        // a closed ordinary-map unit needs no repeated carrier admission for a present own field.
        MIR_label_t missing = label(c); own_done = label(c);
        own_value = em_new_reg(&c->em, "own_property", MIR_T_I64);
        MIR_reg_t shape = em_load_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_MAP_TYPE, MIR_T_I64, "own_shape");
        MIR_reg_t field = em_call_2(&c->em, "typemap_hash_lookup_key", MIR_T_P,
            MIR_T_P, reg(c, shape), MIR_T_P, reg(c, payload(c, name)), true);
        branch_truth(c, missing, field, false);
        MIR_reg_t data = em_load_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_MAP_DATA, MIR_T_I64, "own_data");
        MIR_reg_t item = em_call_2(&c->em, "map_shape_field_to_item", MIR_T_I64,
            MIR_T_P, reg(c, data), MIR_T_P, reg(c, field), true);
        move(c, own_value, reg(c, item)); jump(c, own_done); put_label(c, missing);
    }
    bool computed_own = false;
    if (!ref.key_known && site && (site->node_type == AST_NODE_MEMBER_EXPR || site->node_type == AST_NODE_INDEX_EXPR)) {
        auto* member = (AstFieldNode*)site;
        // numeric keys retain their indexed path without an unrelated own-field head.
        computed_own = member->computed && kind(c->program, member->property, c->inlining) != K_NUMBER;
    }
    MIR_reg_t value = property_access(c, callee ? LMD_PROP_CALLEE : LMD_PROP_GET,
        ref.owner.reg, name, 0, ref.spelling, consume, NULL, false, k, computed_own);
    if (own_done) { move(c, own_value, reg(c, value)); put_label(c, own_done); value = own_value; }
    if (consume) { move(c, result, reg(c, value), true); put_label(c, done); return {result, K_NUMBER}; }
    unsigned result_kind = !callee && site && (site->node_type == AST_NODE_MEMBER_EXPR || site->node_type == AST_NODE_INDEX_EXPR)
        ? kind(c->program, site, c->inlining) : K_ANY;
    check_error(c, value); move(c, result, reg(c, (result_kind & K_NUMBER) ? stable_item(c, value).reg : value));
    put_label(c, done); return boxed(c, result, result_kind);
}
static void array_index_write(LmdCompiler* c, LmdReference ref, LmdValue value,
        bool initialize = false, int64_t reserved_slots = 0) {
    bool guarded_write = !initialize && ref.dense_storage && c->dense_loop && c->dense_loop->element_writes;
    bool callbacks = c->program->property_callbacks && !initialize && !guarded_write;
    // a fresh literal reserves two slots per item, including a possible scalar home.
    bool reserved = initialize && ref.key_known && ref.index >= 0 && ref.index < reserved_slots;
    bool scalar_home = !value.integer && (semantic(value) & K_NUMBER);
    LmdValue item = box(c, value);
    if (reserved && !scalar_home) {
        // the reserved direct path needs neither fallback labels nor scalar-home checks.
        em_store_at(&c->em, em_array_element_address(&c->em, ref.owner.reg, ref.key, 8), 0, MIR_T_I64, item.reg);
        em_store_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, constant(c, ref.index + 1));
        return;
    }
    MIR_reg_t size = reserved ? 0 : em_load_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "length");
    MIR_label_t done = label(c), slow = label(c), store = label(c);
    // the entry proof excludes indexed hooks; bounds must fail before the iteration's sole store.
    if (guarded_write) branch(c, MIR_UBGE, c->dense_loop->bail, reg(c, ref.key), reg(c, size));
    if (!initialize && !ref.dense_storage && (callbacks || (c->program->array_holes && ref.lanes != LMD_ARRAY_ITEMS)))
        branch_sparse_array(c, slow, ref.owner.reg);
    if (!reserved && callbacks) branch(c, MIR_UBGE, slow, reg(c, ref.key), reg(c, size));
    if (callbacks) {
        MIR_reg_t current = em_load_at(&c->em, em_array_element_address(&c->em, ref.owner.reg, ref.key, 8), 0, MIR_T_I64, "own_element");
        branch(c, MIR_BEQ, slow, reg(c, current), integer(c, ITEM_JS_DELETED_SENTINEL));
    }
    // only out-of-band Numbers require an owned scalar home; zero sentinels store verbatim.
    if (scalar_home) {
        MIR_reg_t wide_tag = op(c, MIR_URSH, reg(c, item.reg), integer(c, 56));
        MIR_label_t immediate = label(c);
        branch(c, MIR_BNE, immediate, reg(c, wide_tag), integer(c, LMD_TYPE_FLOAT));
        branch(c, MIR_UBGT, slow, reg(c, item.reg), integer(c, ITEM_FLOAT_N0)); put_label(c, immediate);
    }
    if (!reserved) {
        branch(c, MIR_UBLT, store, reg(c, ref.key), reg(c, size));
        branch(c, MIR_BNE, slow, reg(c, ref.key), reg(c, size));
        MIR_reg_t capacity = em_load_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_LIST_CAPACITY, MIR_T_I64, "capacity");
        MIR_reg_t extra = em_load_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_LIST_EXTRA, MIR_T_I64, "tail_homes");
        branch(c, MIR_UBGE, slow, reg(c, size), reg(c, op(c, MIR_SUB, reg(c, capacity), reg(c, extra))));
        // only append changes length; no safepoint occurs before the owned slot is installed.
        em_store_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64,
            op(c, MIR_ADD, reg(c, size), integer(c, 1)));
    }
    put_label(c, store);
    em_store_at(&c->em, em_array_element_address(&c->em, ref.owner.reg, ref.key, 8), 0, MIR_T_I64, item.reg);
    if (reserved) em_store_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64,
        constant(c, ref.index + 1));
    jump(c, done);
    put_label(c, slow);
    MIR_reg_t stored = callbacks ? em_call_4(&c->em, "mvp_lmd_array_index_property", MIR_T_I64,
        MIR_T_I64, reg(c, ref.owner.reg), MIR_T_I64, reg(c, ref.key), MIR_T_I64, reg(c, item.reg),
        MIR_T_I64, integer(c, LMD_PROP_SET | (c->strict ? LMD_PROP_STRICT : 0)), true) :
        em_call_3(&c->em, "mvp_lmd_array_store", MIR_T_I64, MIR_T_I64,
            reg(c, ref.owner.reg), MIR_T_I64, reg(c, ref.key), MIR_T_I64, reg(c, item.reg), true);
    check_error(c, stored); put_label(c, done);
}
static void array_length_write(LmdCompiler* c, LmdReference ref, LmdValue value, AstNode* site) {
    if (c->program->property_callbacks) {
        MIR_reg_t result = em_call_4(&c->em, "mvp_lmd_array_index_property", MIR_T_I64,
            MIR_T_I64, reg(c, ref.owner.reg), MIR_T_I64, integer(c, -1),
            MIR_T_I64, reg(c, box(c, value).reg), MIR_T_I64, integer(c, LMD_PROP_SET | (c->strict ? LMD_PROP_STRICT : 0)), true);
        check_error(c, result); return;
    }
    MIR_reg_t d = to_number(c, value);
    MIR_label_t range_error = label(c), capability = label(c), done = label(c);
    MIR_reg_t i = integer_lane(c, d, 0, UINT32_MAX, range_error, true);
    MIR_reg_t size = em_load_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "old_length");
    branch_sparse_array(c, capability, ref.owner.reg);
    branch(c, MIR_UBGT, capability, reg(c, i), reg(c, size));
    em_store_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, i); jump(c, done);
    put_label(c, range_error); fail(c, LMD_MVP_RANGE, site);
    put_label(c, capability);
    MIR_reg_t resized = em_call_2(&c->em, "mvp_lmd_array_resize", MIR_T_I64,
        MIR_T_I64, reg(c, ref.owner.reg), MIR_T_I64, reg(c, i), true);
    check_error(c, resized); put_label(c, done);
}
static void write_reference(LmdCompiler* c, LmdReference ref, LmdValue value, AstNode* site) {
    if (ref.invalid_target) return;
    if (ref.global_presence) {
        // a RHS-created global does not resolve the previously captured strict reference.
        MIR_label_t present = label(c);
        branch(c, MIR_BNE, present, reg(c, ref.global_presence), integer(c, ITEM_FALSE));
        fail(c, LMD_MVP_REFERENCE, site); put_label(c, present);
    }
    if (ref.eval_reference) {
        eval_reference_operation(c, ref.eval_reference, JS_INTERP_REFERENCE_SET, 0, box(c, value).reg); return;
    }
    if (ref.private_element) {
        MIR_reg_t result = em_call_4(&c->em, "mvp_lmd_private_element", MIR_T_I64,
            MIR_T_I64, reg(c, ref.owner.reg), MIR_T_I64, reg(c, ref.name), MIR_T_I64, reg(c, box(c, value).reg),
            MIR_T_I64, integer(c, LMD_PRIVATE_SET), true);
        check_error(c, result); return;
    }
    if (ref.with_object) { with_reference_operation(c, ref, site, LMD_PROP_SET, value); return; }
    if (ref.uncoerced_name) {
        // pattern references retain the key value; PutValue converts it after the source/default effects.
        MIR_label_t valid = label(c);
        branch_truth(c, valid, nullish(c, ref.owner), false); fail(c, LMD_MVP_TYPE, site); put_label(c, valid);
        // numeric keys have no conversion effects; retain their index lane until a named lookup needs spelling.
        // repeated closed sites amortize the Number guard; reflective sites keep one canonical-key path.
        bool repeated_primitive = repeated_execution(c) && !c->program->coercion_callbacks;
        LmdValue key = canonical_key(c, ref.deferred_name,
            !ref.receiver && (semantic(ref.deferred_name) == K_NUMBER || repeated_primitive));
        // a canonical string/Symbol already has its spelling; only preserved Numbers need deferred naming.
        bool deferred_number = !ref.receiver && (semantic(key) & K_NUMBER);
        ref.name = deferred_number ? 0 : key.reg;
        ref.deferred_name = deferred_number ? key : LmdValue{}; ref.uncoerced_name = false;
        ref.numeric_key = semantic(key) == K_NUMBER;
        ref.key = property_key(c, key, semantic(ref.owner) == K_TYPED_ARRAY);
    }
    if (ref.receiver) {
        MIR_reg_t result = em_call_5(&c->em, "mvp_lmd_property_with_receiver", MIR_T_I64,
            MIR_T_I64, reg(c, ref.owner.reg), MIR_T_I64, reg(c, ref.name), MIR_T_I64, reg(c, box(c, value).reg),
            MIR_T_I64, integer(c, LMD_PROP_SET | (c->strict ? LMD_PROP_STRICT : 0)), MIR_T_I64, reg(c, ref.receiver), true);
        check_error(c, result); return;
    }

    if (!ref.key) { write_binding(c, ref.binding, value, site); return; }
    if (LmdScalarField* field = scalar_reference(ref)) { scalar_field_write(c, field, value); return; }
    MIR_label_t done = label(c), object = label(c);
    unsigned k = semantic(ref.owner);
    // satellites receive foreign carriers through the full property ABI;
    // closed units may instead pair fixed named storage with fixed reads.
    if (c->program->satellite_script && ref.numeric_key &&
            ref.deferred_name.reg && (k & (K_OBJECT | K_MAP))) {
        MIR_label_t ordinary = label(c);
        em_guard_container(&c->em, ref.owner.reg, LMD_TYPE_MAP, ordinary);
        branch(c, MIR_BNE, ordinary, reg(c, em_load_at(&c->em, ref.owner.reg,
            LAMBDA_GC_OFF_CONTAINER_MAP_KIND, MIR_T_U8, "indexed_map_kind")), integer(c, MAP_KIND_TYPED_ARRAY));
        // Number keys need no spelling for integer-indexed Set; +0 canonicalizes a Number -0 key.
        MIR_reg_t index = op(c, MIR_DADD, reg(c, to_number(c, ref.deferred_name)),
            MIR_new_double_op(c->em.ctx, 0), MIR_T_D);
        MIR_reg_t stored = em_call_3(&c->em, "mvp_lmd_typed_array_store", MIR_T_I64,
            MIR_T_I64, reg(c, ref.owner.reg), MIR_T_D, reg(c, index),
            MIR_T_I64, reg(c, box(c, value).reg), true);
        check_error(c, stored); jump(c, done); put_label(c, ordinary);
    }
    if (k == K_TYPED_ARRAY) { typed_array_reference(c, ref, site, &value); put_label(c, done); return; }
    if (k & K_TYPED_ARRAY) {
        MIR_label_t ordinary = label(c);
        branch(c, MIR_BNE, ordinary, reg(c, tag(c, ref.owner)), integer(c, LMD_TYPE_ARRAY_NUM));
        typed_array_reference(c, ref, site, &value); jump(c, done); put_label(c, ordinary);
    }
    if (k == K_OBJECT || k == K_MAP || (c->program->receiver_abi && k == K_FUNCTION)) jump(c, object);
    else if (k != K_ARRAY) {
        MIR_reg_t type = tag(c, ref.owner);
        branch(c, MIR_BEQ, object, reg(c, type), integer(c, LMD_TYPE_MAP));
        branch_virtual_object(c, type, object);
        if (c->program->receiver_abi && (k & K_FUNCTION))
            branch(c, MIR_BEQ, object, reg(c, type), integer(c, LMD_TYPE_FUNC));
    }
    if (k != K_ARRAY) {
        MIR_label_t array = label(c);
        branch(c, MIR_BEQ, array, reg(c, tag(c, ref.owner)), integer(c, LMD_TYPE_ARRAY));
        if (c->program->library_active && (k & (K_STRING | K_NUMBER | K_BOOL | K_BIGINT | K_SYMBOL))) {
            // primitive PutValue boxes the lookup base while preserving the original setter receiver.
            MIR_reg_t stored = em_call_5(&c->em, "mvp_lmd_property_with_receiver", MIR_T_I64,
                MIR_T_I64, reg(c, ref.owner.reg), MIR_T_I64, reg(c, reference_name(c, ref)),
                MIR_T_I64, reg(c, box(c, value).reg),
                MIR_T_I64, integer(c, LMD_PROP_SET | (c->strict ? LMD_PROP_STRICT : 0)),
                MIR_T_I64, reg(c, ref.owner.reg), true);
            check_error(c, stored); jump(c, done);
        } else property_failure(c, ref.owner, site);
        put_label(c, array);
    }
    if (ref.name || ref.deferred_name.reg) {
        if (ref.key_known && ref.index < -1) jump(c, object);
        else if (!ref.key_known) branch(c, MIR_BLT, object, reg(c, ref.key), integer(c, -1));
    } else if (!(ref.dense_storage && c->dense_loop && c->dense_loop->element_writes)) array_key_check(c, ref, site);
    // numeric keys cannot spell length; nonindices have already branched to named storage.
    if (ref.key_known || ref.numeric_key) {
        if (ref.key_known && ref.index == -1) array_length_write(c, ref, value, site);
        else array_index_write(c, ref, value);
    } else {
        MIR_label_t length = label(c), done = label(c);
        branch(c, MIR_BEQ, length, reg(c, ref.key), MIR_new_int_op(c->em.ctx, -1));
        array_index_write(c, ref, value); jump(c, done);
        put_label(c, length); array_length_write(c, ref, value, site); put_label(c, done);
    }
    if (!ref.name && !ref.deferred_name.reg) { put_label(c, done); return; }
    jump(c, done); put_label(c, object);
    if (ref.plan && ref.field) {
        MIR_label_t miss = label(c);
        if (field_write(c, ref, value, miss)) jump(c, done);
        put_label(c, miss);
    }
    bool native = c->program->receiver_abi && !is_boxed(value) && !value.number_present &&
        !(value.literal && value.range.state == 1 && !value.integer) &&
        (semantic(value) == K_NUMBER || semantic(value) == K_BOOL);
    // retain RHS nullability even when an arithmetic result remains boxed.
    MIR_reg_t stored = property_access(c, LMD_PROP_SET, ref.owner.reg,
        reference_name(c, ref), native ? 0 : box(c, value).reg, ref.spelling, false,
        native ? &value : NULL, k == K_OBJECT, k);
    check_error(c, stored); put_label(c, done);
}

static LmdValue iterator_value(LmdCompiler* c, MIR_reg_t record, MIR_reg_t active,
        MIR_label_t done, bool elision = false, bool asynchronous = false) {
    // an abrupt IteratorStep/IteratorValue marks this iterator done; enclosing iterators still close.
    move(c, active, integer(c, 0));
    MIR_reg_t result = asynchronous
        ? em_call_1(&c->em, "js_async_iterator_step_result", MIR_T_I64, MIR_T_I64, reg(c, record), true)
        : em_call_3(&c->em, "mvp_lmd_iterator_step", MIR_T_I64,
            MIR_T_I64, reg(c, record), MIR_T_I64, integer(c, ITEM_JS_UNDEFINED), MIR_T_I64, integer(c, 0), true);
    check_error(c, result); boxed(c, result);
    if (asynchronous) { result = emit_await(c, result); check_error(c, result); boxed(c, result); }
    MIR_reg_t finished = asynchronous
        ? em_call_1(&c->em, "js_iterator_result_done", MIR_T_I64, MIR_T_I64, reg(c, result), true)
        : property_access(c, LMD_PROP_GET, result, text(c, "done").reg);
    check_error(c, finished); branch_truth(c, done, truth(c, boxed(c, finished)));
    LmdValue value = boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
    if (!elision) {
        MIR_reg_t yielded = property_access(c, LMD_PROP_GET, result, text(c, "value").reg);
        check_error(c, yielded); value = stable_item(c, yielded);
    }
    move(c, active, integer(c, 1)); return value;
}
static bool pattern_reference(LmdCompiler* c, AstNode* pattern, bool initialize, LmdReference* result) {
    AstNode* target = js_ast_pattern_reference_target(pattern);
    if (!target) return false;
    if (initialize) {
        LmdBinding* binding = target->node_type == AST_NODE_IDENT ?
            identifier_binding(c->program, (AstIdentNode*)target) : NULL;
        if (!binding || !binding->with_lookup) return false;
    }
    *result = reference(c, target, true, true); return true;
}
static void bind_pattern(LmdCompiler* c, AstNode* pattern, LmdValue value, bool initialize,
        const LmdReference* resolved = NULL) {
    if (pattern->node_type == AST_NODE_IDENT) {
        if (resolved) write_reference(c, *resolved, value, pattern);
        else if (!initialize && eval_reference_site(c->program, pattern))
            write_reference(c, eval_reference(c, (AstIdentNode*)pattern), value, pattern);
        else write_binding(c, identifier_binding(c->program, (AstIdentNode*)pattern), value, pattern, initialize);
        return;
    }
    AstNode* target = js_ast_parameter_pattern(pattern);
    AstNode* default_value = js_ast_parameter_default_value(pattern);
    if (default_value) {
        if (target->node_type == AST_NODE_ASSIGN_PATTERN) target = ((AstAssignNode*)target)->left;
        MIR_reg_t selected = em_new_reg(&c->em, "binding_default", MIR_T_I64);
        move(c, selected, reg(c, box(c, value).reg)); boxed(c, selected);
        MIR_label_t supplied = label(c);
        branch(c, MIR_BNE, supplied, reg(c, selected), integer(c, ITEM_JS_UNDEFINED));
        move(c, selected, reg(c, box(c, expression(c, default_value)).reg));
        if (target->node_type == AST_NODE_IDENT &&
                (default_value->node_type == AST_NODE_FUNC_EXPR || default_value->node_type == AST_NODE_ARROW_FUNC) &&
                !((AstFuncNode*)default_value)->name) {
            // NamedEvaluation applies only to a directly anonymous definition, not a sequence/call result.
            MIR_reg_t named = em_call_4(&c->em, "mvp_lmd_named_set", MIR_T_I64,
                MIR_T_I64, reg(c, selected), MIR_T_P, relocated(c, (uint64_t)"name"),
                MIR_T_I64, relocated(c, s2it(((AstIdentNode*)target)->name)),
                MIR_T_I64, integer(c, JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE), true);
            check_error(c, named);
        }
        put_label(c, supplied);
        bind_pattern(c, target, stable_item(c, selected), initialize, resolved); return;
    }
    if (target != pattern) { bind_pattern(c, target, value, initialize, resolved); return; }
    if (pattern->node_type == AST_NODE_ARRAY_PATTERN || pattern->node_type == AST_NODE_ARRAY) {
        LmdValue owner = box(c, value);
        MIR_reg_t record = em_call_2(&c->em, "mvp_lmd_iterator_get", MIR_T_I64,
            MIR_T_P, reg(c, c->unit), MIR_T_I64, reg(c, owner.reg), true);
        check_error(c, record); boxed(c, record);
        MIR_reg_t active = constant(c, 1);
        LmdControl control = {c->control, NULL, NULL, NULL, 0, NULL, owner.reg, active, record};
        c->control = &control;
        for (AstNode* element = ((AstArrayNode*)pattern)->item; element; element = element->next) {
            bool rest = element->node_type == AST_NODE_REST_ELEMENT || element->node_type == AST_NODE_SPREAD;
            AstNode* target = rest ? ((AstSpreadNode*)element)->argument : element;
            LmdReference ref = {};
            bool prepared = pattern_reference(c, target, initialize, &ref);
            MIR_label_t done = label(c);
            if (rest) {
                MIR_reg_t array = em_call_1(&c->em, "mvp_lmd_array_new", MIR_T_I64, MIR_T_I64, integer(c, 0), true);
                check_error(c, array); LmdValue output = boxed(c, array, K_ARRAY);
                MIR_label_t next = label(c); put_label(c, next);
                branch_truth(c, done, active, false);
                LmdValue member = iterator_value(c, record, active, done);
                MIR_reg_t index = em_load_at(&c->em, array, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "rest_length");
                array_index_write(c, {NULL, output, index}, member, true);
                jump(c, next); put_label(c, done);
                bind_pattern(c, target, output, initialize, prepared ? &ref : NULL);
            } else {
                MIR_reg_t selected = constant(c, ITEM_JS_UNDEFINED); boxed(c, selected);
                branch_truth(c, done, active, false);
                LmdValue member = iterator_value(c, record, active, done, element->node_type == AST_NODE_NULL);
                move(c, selected, reg(c, member.reg)); put_label(c, done);
                if (element->node_type != AST_NODE_NULL)
                    bind_pattern(c, target, boxed(c, selected), initialize, prepared ? &ref : NULL);
            }
        }
        release_iterators(c, control.parent);
        if (control.error_label) {
            // successful destructuring must skip its exceptional cleanup landing pad.
            MIR_label_t done = label(c); jump(c, done);
            emit_iterator_error_pad(c, &control); put_label(c, done);
        }
        c->control = control.parent; return;
    }
    if (pattern->node_type == AST_NODE_MAP_PATTERN) {
        LmdValue owner = box(c, value);
        MIR_label_t valid = label(c);
        branch_truth(c, valid, nullish(c, owner), false); fail(c, LMD_MVP_TYPE, pattern); put_label(c, valid);
        AstNode* rest = NULL;
        for (AstNode* node = ((AstMapNode*)pattern)->properties; node; node = node->next)
            if (node->node_type == AST_NODE_REST_PROPERTY || node->node_type == AST_NODE_SPREAD) rest = node;
        LmdValue excluded = boxed(c, constant(c, ITEM_NULL), K_NULL);
        if (rest) {
            MIR_reg_t array = em_call_1(&c->em, "mvp_lmd_array_new", MIR_T_I64, MIR_T_I64, integer(c, 0), true);
            check_error(c, array); excluded = boxed(c, array, K_ARRAY);
        }
        int index = 0;
        for (AstNode* node = ((AstMapNode*)pattern)->properties; node; node = node->next) {
            if (node == rest) continue;
            if (node->node_type != AST_NODE_PROPERTY) { diagnostic(c->program, node, "object binding member"); continue; }
            AstPropertyNode* property = (AstPropertyNode*)node;
            String* spelling = !property->computed && property->key->node_type == AST_NODE_IDENT ? ((AstIdentNode*)property->key)->name : NULL;
            LmdValue key = spelling ? boxed(c, relocated_constant(c, s2it(spelling)), K_STRING) : canonical_key(c, expression(c, property->key));
            if (rest) array_index_write(c, {NULL, excluded, constant(c, index++)}, key, true);
            LmdReference target = {};
            bool prepared = pattern_reference(c, property->value, initialize, &target);
            LmdReference ref = {NULL, owner, property_key(c, key), false, -2, key.reg}; ref.spelling = spelling;
            LmdValue member = read_reference(c, ref, node);
            bind_pattern(c, property->value, member, initialize, prepared ? &target : NULL);
        }
        if (rest) {
            AstNode* argument = ((AstSpreadNode*)rest)->argument;
            LmdReference ref = {};
            bool prepared = pattern_reference(c, argument, initialize, &ref);
            MIR_reg_t object = em_call_2(&c->em, "mvp_lmd_object_new", MIR_T_I64,
                MIR_T_P, relocated(c, (uint64_t)c->program->library->object_shape), MIR_T_I64, integer(c, 0), true);
            check_error(c, object); LmdValue output = boxed(c, object, K_OBJECT);
            MIR_reg_t copied = em_call_4(&c->em, "mvp_lmd_object_copy", MIR_T_I64,
                MIR_T_I64, reg(c, object), MIR_T_I64, reg(c, owner.reg), MIR_T_I64, integer(c, 0),
                MIR_T_I64, reg(c, excluded.reg), true);
            check_error(c, copied); bind_pattern(c, argument, output, initialize, prepared ? &ref : NULL);
        }
        return;
    }
    if (!initialize && (pattern->node_type == AST_NODE_MEMBER_EXPR || pattern->node_type == AST_NODE_INDEX_EXPR ||
            pattern->node_type == AST_NODE_CALL_EXPR)) {
        write_reference(c, resolved ? *resolved : reference(c, pattern, true), value, pattern); return;
    }
    diagnostic(c->program, pattern, "binding pattern outside MVP scope");
}
static String* named_evaluation_spelling(MvpLmdProgram* program, AstNode* value, int64_t* prefix) {
    *prefix = 0;
    AstNode* parent = ast_index_parent(&program->frontend->ast_index, value);
    if (parent && parent->node_type == AST_NODE_EXPORT && ((AstExportDeclNode*)parent)->is_default)
        return name_pool_create_len(program->frontend->name_pool, "default", 7);
    if (parent && parent->node_type == AST_NODE_PROPERTY) {
        auto* property = (AstPropertyNode*)parent;
        // a prototype setter evaluates its value without NamedEvaluation.
        if (js_ast_property_is_proto_setter(property)) return NULL;
        *prefix = property->is_getter ? 1 : property->is_setter ? 2 : 0;
        return property_spelling(property->key, property->computed);
    }
    if (parent && parent->node_type == AST_NODE_ASSIGN) {
        auto* assignment = (AstAssignNode*)parent;
        if (assignment->lhs_is_parenthesized || (assignment->op != OPERATOR_ASSIGN &&
                assignment->op != OPERATOR_JS_AND_ASSIGN &&
                assignment->op != OPERATOR_JS_OR_ASSIGN &&
                assignment->op != OPERATOR_JS_NULLISH_ASSIGN)) return NULL;
    }
    AstNode* target = parent && parent->node_type == AST_NODE_VARIABLE_DECLARATOR ? ((AstDeclaratorNode*)parent)->id :
        parent && (parent->node_type == AST_NODE_ASSIGN || parent->node_type == AST_NODE_ASSIGN_PATTERN) ? ((AstAssignNode*)parent)->left : NULL;
    return target && target->node_type == AST_NODE_IDENT ? ((AstIdentNode*)target)->name : NULL;
}
static MIR_reg_t named_evaluation_key(LmdCompiler* c, AstNode* value, int64_t* name_prefix = NULL) {
    if (name_prefix) *name_prefix = 0;
    AstNode* initializer = c->function->object_home && c->function->ast ?
        ast_index_parent(&c->program->frontend->ast_index, (AstNode*)c->function->ast) : NULL;
    if (initializer && initializer->node_type == AST_NODE_FIELD && ((AstClassFieldNode*)initializer)->value == value)
        return em_load_at(&c->em, c->arguments, 0, MIR_T_I64, "field_name");
    int64_t prefix = 0;
    String* name = named_evaluation_spelling(c->program, value, &prefix);
    if (name_prefix) *name_prefix = prefix;
    return name ? relocated_constant(c, s2it(name)) : 0;
}
static int function_capture_count(LmdFunction* f, bool home = false) {
    return (f->captures ? f->captures->length : 0) + f->lexical_receiver + home;
}
static LmdValue new_function(LmdCompiler* c, LmdFunction* f, MIR_reg_t class_owner = 0) {
    MIR_reg_t value = em_call_2(&c->em, "mvp_lmd_function_new", MIR_T_I64,
        MIR_T_I64, integer(c, f->id), MIR_T_P, reg(c, c->unit), true);
    check_error(c, value); LmdValue result = boxed(c, value, K_FUNCTION);
    int captures = function_capture_count(f);
    int count = function_capture_count(f, f->object_home || f->lexical_super || class_owner) + f->eval_capture;
    if (count > UINT8_MAX) { diagnostic(c->program, (AstNode*)f->ast, "closure capture limit"); return result; }
    if (count) {
        MIR_reg_t env = em_call_1(&c->em, "heap_calloc_closure_env", MIR_T_P,
            MIR_T_I64, integer(c, count * sizeof(Item)), true);
        MIR_label_t allocated = label(c); branch_truth(c, allocated, env);
        fail(c, LMD_MVP_MEMORY, NULL); put_label(c, allocated);
        // publish the shared precisely traced environment before any later allocation.
        em_store_at(&c->em, result.reg, offsetof(Function, closure_env), MIR_T_I64, env);
        em_store_at(&c->em, result.reg, offsetof(Function, closure_field_count), MIR_T_U8, constant(c, count));
        for (int i = 0; i < captures; i++) {
            MIR_reg_t cell = f->lexical_receiver && i == 0 ? c->receiver_cell :
                binding_cell(c, (LmdBinding*)f->captures->data[i - f->lexical_receiver]);
            em_call_void_4(&c->em, "owned_item_slot_store", MIR_T_P, reg(c, env),
                MIR_T_I64, integer(c, count), MIR_T_I64, integer(c, i), MIR_T_I64, reg(c, cell), true);
        }
        if (f->eval_capture)
            em_store_at(&c->em, env, (count - 1) * sizeof(Item), MIR_T_I64, c->eval_env);
        if (f->lexical_super) {
            // the enclosing method retains its home object; Lambda traces the ordinary closure edge.
            em_store_at(&c->em, env, captures * sizeof(Item), MIR_T_I64, c->self);
        } else if (class_owner) {
            MIR_reg_t home = em_load_at(&c->em, class_owner, LMD_CALLABLE_HOME_OFFSET, MIR_T_I64, "evaluated_class");
            MIR_reg_t retained = class_owner;
            if (f->object_home) {
                AstNode* parent = ast_index_parent(&c->program->frontend->ast_index, (AstNode*)f->ast);
                bool statics = parent->node_type == JS_AST_NODE_STATIC_BLOCK || ((AstClassFieldNode*)parent)->is_static;
                MIR_reg_t values = em_load_at(&c->em, home, LMD_CLASS_VALUES_OFFSET, MIR_T_I64, "class_values");
                retained = em_load_at(&c->em, values, (statics ? 2 : 1) * sizeof(Item), MIR_T_I64, "initializer_home");
            } else em_store_at(&c->em, result.reg, LMD_CALLABLE_HOME_OFFSET, MIR_T_I64, home);
            // the same Lambda closure environment retains the per-evaluation class owner/home.
            em_store_at(&c->em, env, captures * sizeof(Item), MIR_T_I64, retained);
        }
    }
    if (!f->ast->name && (c->function->object_home || c->program->source_module)) {
        int64_t prefix = 0;
        MIR_reg_t name = named_evaluation_key(c, (AstNode*)f->ast, &prefix);
        if (name) check_error(c, em_call_3(&c->em, "mvp_lmd_function_set_name", MIR_T_I64,
            MIR_T_I64, reg(c, result.reg), MIR_T_I64, reg(c, name), MIR_T_I64, integer(c, prefix), true));
    }
    return result;
}
static LmdValue* call_arguments(LmdCompiler* c, AstCallNode* n, bool boxed_args) {
    int count = ast_linked_node_count(n->arguments), i = 0;
    LmdValue* values = (LmdValue*)mem_calloc((size_t)count, sizeof(LmdValue), MEM_CAT_TEMP);
    for (AstNode* a = n->arguments; a; a = a->next) {
        LmdValue value = expression(c, a);
        values[i++] = boxed_args ? box(c, value) : value;
    }
    return values;
}
static void sequence_bounds(LmdCompiler* c, MIR_reg_t size, LmdValue* values, int count,
        MIR_reg_t* bounds) {
    MIR_reg_t length = to_number(c, {size, K_NUMBER, false, 0, true});
    for (int i = 0; i < 2; i++) {
        bounds[i] = em_new_reg(&c->em, "fill_bound", MIR_T_I64);
        move(c, bounds[i], i ? reg(c, size) : integer(c, 0));
        if (count <= i) continue;
        MIR_label_t done = label(c), nonnegative = label(c);
        if (i) branch(c, MIR_BEQ, done, reg(c, tag(c, values[i])), integer(c, LMD_TYPE_UNDEFINED));
        MIR_reg_t d = em_call_1(&c->em, "trunc", MIR_T_D, MIR_T_D,
            reg(c, to_number(c, values[i])), true);
        move(c, bounds[i], integer(c, 0));
        branch_truth(c, done, op(c, MIR_DEQ, reg(c, d), reg(c, d)), false);
        branch_truth(c, nonnegative, op(c, MIR_DLT, reg(c, d), MIR_new_double_op(c->em.ctx, 0)), false);
        move(c, d, reg(c, op(c, MIR_DADD, reg(c, d), reg(c, length), MIR_T_D)), true);
        put_label(c, nonnegative);
        branch_truth(c, done, op(c, MIR_DGT, reg(c, d), MIR_new_double_op(c->em.ctx, 0)), false);
        move(c, bounds[i], reg(c, size));
        branch_truth(c, done, op(c, MIR_DLT, reg(c, d), reg(c, length)), false);
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_D2I, reg(c, bounds[i]), reg(c, d)));
        put_label(c, done);
    }
}
static LmdValue array_fill_call(LmdCompiler* c, LmdValue owner, LmdValue* values, int count,
        unsigned lanes = 0, bool ordinary = false) {
    LmdValue absent = boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
    LmdValue value = count ? values[0] : absent;
    if (!ordinary && (semantic(value) != K_NUMBER || is_boxed(value))) value = {to_number(c, value), K_NUMBER};
    MIR_reg_t size = em_load_at(&c->em, owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "fill_length");
    MIR_reg_t bounds[2]; sequence_bounds(c, size, count ? values + 1 : NULL, count - 1, bounds);
    if (ordinary) {
        MIR_label_t loop = label(c), done = label(c);
        put_label(c, loop);
        branch(c, MIR_UBGE, done, reg(c, bounds[0]), reg(c, bounds[1]));
        // the method guard survives primitive bounds conversion and callback-free stores.
        LmdReference ref = {NULL, owner, bounds[0]}; ref.dense_storage = !c->program->property_callbacks;
        array_index_write(c, ref, value);
        move(c, bounds[0], reg(c, op(c, MIR_ADD, reg(c, bounds[0]), integer(c, 1)))); jump(c, loop);
        put_label(c, done); return owner;
    }
    // conversion and lane selection happen once, outside the fixed-storage fill loop.
    return typed_array_lanes(c, owner, bounds[0], &value, bounds[1], lanes);
}
static LmdValue object_construct(LmdCompiler* c, AstCallNode* n) {
    int count = ast_linked_node_count(n->arguments);
    LmdValue* values = call_arguments(c, n, true);
    MIR_reg_t result = em_new_reg(&c->em, "object_construct", MIR_T_I64);
    MIR_label_t allocate = label(c), done = label(c);
    if (count) {
        MIR_reg_t type = tag(c, values[0]);
        branch(c, MIR_BEQ, allocate, reg(c, type), integer(c, LMD_TYPE_NULL));
        branch(c, MIR_BEQ, allocate, reg(c, type), integer(c, LMD_TYPE_UNDEFINED));
        move(c, result, reg(c, values[0].reg));
        const TypeId kinds[] = {LMD_TYPE_MAP, LMD_TYPE_ARRAY, LMD_TYPE_ARRAY_NUM, LMD_TYPE_FUNC};
        for (TypeId kind : kinds) branch(c, MIR_BEQ, done, reg(c, type), integer(c, kind));
        MIR_reg_t boxed_value = em_call_2(&c->em, "mvp_lmd_box_primitive", MIR_T_I64,
            MIR_T_I64, reg(c, values[0].reg), MIR_T_I64, integer(c, ITEM_JS_UNDEFINED), true);
        check_error(c, boxed_value); move(c, result, reg(c, boxed_value)); jump(c, done);
    }
    put_label(c, allocate);
    move(c, result, reg(c, em_call_2(&c->em, "mvp_lmd_object_new", MIR_T_I64,
        MIR_T_P, relocated(c, (uint64_t)c->program->roots[0]), MIR_T_I64, integer(c, 0), true)));
    check_error(c, result); put_label(c, done);
    mem_free(values); return boxed(c, result, count ? K_ANY : K_OBJECT);
}
static LmdValue array_construct(LmdCompiler* c, AstCallNode* n) {
    int count = ast_linked_node_count(n->arguments);
    LmdValue* values = call_arguments(c, n, true);
    MIR_reg_t result = em_new_reg(&c->em, "constructed_array", MIR_T_I64);
    MIR_label_t elements = label(c), done = label(c);
    if (count == 1) {
        branch(c, MIR_BNE, elements, reg(c, tag(c, values[0])), integer(c, LMD_TYPE_FLOAT));
        MIR_label_t invalid = label(c), valid = label(c);
        MIR_reg_t length = integer_lane(c, to_number(c, values[0]), 0, UINT32_MAX, invalid, true);
        MIR_reg_t array = em_call_1(&c->em, "mvp_lmd_array_new", MIR_T_I64, MIR_T_I64, reg(c, length), true);
        check_error(c, array); move(c, result, reg(c, array)); jump(c, valid);
        put_label(c, invalid); fail(c, LMD_MVP_RANGE, (AstNode*)n);
        put_label(c, valid); jump(c, done);
    }
    put_label(c, elements);
    MIR_reg_t empty = em_call_1(&c->em, "mvp_lmd_array_new", MIR_T_I64, MIR_T_I64, integer(c, 0), true);
    check_error(c, empty); LmdValue owner = boxed(c, empty, K_ARRAY);
    // Array construction creates own data properties even when indexed prototype setters exist.
    for (int i = 0; i < count; i++) array_index_write(c, {NULL, owner, constant(c, i), true, i}, values[i], true);
    move(c, result, reg(c, owner.reg)); put_label(c, done);
    mem_free(values); return boxed(c, result, K_ARRAY);
}
static LmdValue iterable_source(LmdCompiler*, AstNode*, int&, MIR_reg_t&, bool* = NULL);
static LmdValue array_foreach_call(LmdCompiler* c, LmdValue owner, LmdValue* values, int count, int method);
static MIR_reg_t call_argument_span(LmdCompiler* c, LmdValue* values, int count) {
    MIR_reg_t span = 0;
    if (count) {
        if (count > c->argument_count) c->argument_count = count;
        span = em_deferred_root_span_base(&c->em, &c->argument_span, &c->argument_fixup);
        for (int i = 0; i < count; i++) em_store_at(&c->em, span, i * sizeof(Item), MIR_T_I64, box(c, values[i]).reg);
    }
    return span;
}
static MIR_reg_t sequence_method_runtime(LmdCompiler* c, LmdValue owner, int method,
        LmdValue* values, int count) {
    MIR_reg_t span = call_argument_span(c, values, count);
    MIR_reg_t result = em_call_4(&c->em, mvp_lmd_array_visit_method(method) ? "mvp_lmd_array_visit" : "mvp_lmd_array_edit", MIR_T_I64,
        MIR_T_I64, reg(c, owner.reg), MIR_T_P, span ? reg(c, span) : integer(c, 0),
        MIR_T_I64, integer(c, count), MIR_T_I64, integer(c, method), true);
    check_error(c, result);
    for (int i = 0; i < count; i++) em_store_at(&c->em, span, i * sizeof(Item), MIR_T_I64, constant(c, ITEM_JS_UNDEFINED));
    return result;
}
static LmdValue sequence_method_dense(LmdCompiler* c, LmdValue owner, int method,
        LmdValue* values, int count) {
    LmdValue absent = boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
    if (method >= LMD_METHOD_COPY_WITHIN && method <= LMD_METHOD_WITH)
        return boxed(c, sequence_method_runtime(c, owner, method, values, count), K_ARRAY);
    if ((method >= LMD_METHOD_EVERY && method <= LMD_METHOD_LAST_INDEX_OF) ||
            method == LMD_METHOD_FLAT_MAP || method == LMD_METHOD_FIND_LAST || method == LMD_METHOD_FIND_LAST_INDEX)
        return stable_item(c, sequence_method_runtime(c, owner, method, values, count));
    if (method == LMD_METHOD_CONCAT || method == LMD_METHOD_SPLICE || method == LMD_METHOD_UNSHIFT ||
            method == LMD_METHOD_SHIFT || method == LMD_METHOD_TO_REVERSED || method == LMD_METHOD_AT) {
        MIR_reg_t result = sequence_method_runtime(c, owner, method, values, count);
        return method == LMD_METHOD_SHIFT || method == LMD_METHOD_AT ? stable_item(c, result) :
            boxed(c, result, method == LMD_METHOD_UNSHIFT ? K_NUMBER : K_ARRAY);
    }
    bool split_protocol = method == LMD_METHOD_SPLIT && c->program->library &&
        (!c->program->library->regexp_protocol_unobserved || (count &&
            (!semantic(values[0]) || (semantic(values[0]) & ~(K_STRING | K_NUMBER | K_BOOL | K_NULL | K_UNDEFINED)))));
    if (method == LMD_METHOD_MATCH || method == LMD_METHOD_SEARCH || method == LMD_METHOD_REPLACE || method == LMD_METHOD_MATCH_ALL || split_protocol) {
        MIR_reg_t result = em_call_5(&c->em, "mvp_lmd_regexp_string_method", MIR_T_I64,
            MIR_T_P, relocated(c, (uint64_t)c->program->library), MIR_T_I64, reg(c, owner.reg),
            MIR_T_I64, reg(c, count ? box(c, values[0]).reg : absent.reg),
            MIR_T_I64, reg(c, count > 1 ? box(c, values[1]).reg : absent.reg),
            MIR_T_I64, integer(c, method == LMD_METHOD_MATCH_ALL ? 4 : split_protocol ? 3 : method == LMD_METHOD_SEARCH ? 2 : method == LMD_METHOD_REPLACE), true);
        check_error(c, result); return method == LMD_METHOD_REPLACE && c->program->library->regexp_protocol_unobserved
            ? boxed(c, result, K_STRING) : stable_item(c, result);
    }
    if (method == LMD_METHOD_LOWER) {
        MIR_reg_t result = em_call_1(&c->em, "fn_lower", MIR_T_I64, MIR_T_I64, reg(c, owner.reg), true);
        check_error(c, result); return boxed(c, result, K_STRING);
    }
    if (method == LMD_METHOD_BIGINT_STRING || method == LMD_METHOD_NUMBER_STRING) {
        MIR_reg_t radix = to_number(c, count ? values[0] : number(c, 10));
        if (count && (semantic(values[0]) & K_UNDEFINED)) {
            MIR_label_t supplied = label(c);
            branch(c, MIR_BNE, supplied, reg(c, tag(c, values[0])), integer(c, LMD_TYPE_UNDEFINED));
            move(c, radix, MIR_new_double_op(c->em.ctx, 10), true); put_label(c, supplied);
        }
        MIR_reg_t result = method == LMD_METHOD_BIGINT_STRING ?
            em_call_2(&c->em, "mvp_lmd_bigint_to_string", MIR_T_I64,
                MIR_T_I64, reg(c, box(c, owner).reg), MIR_T_D, reg(c, radix), true) :
            em_call_2(&c->em, "mvp_lmd_number_to_radix_string", MIR_T_I64,
                MIR_T_D, reg(c, to_number(c, owner)), MIR_T_D, reg(c, radix), true);
        check_error(c, result); return boxed(c, result, K_STRING);
    }
    if (method == LMD_METHOD_FILL) return array_fill_call(c, owner, values, count, 0, true);
    if (method == LMD_METHOD_ARRAY_STRING) {
        MIR_reg_t result = em_call_3(&c->em, "mvp_lmd_array_to_string", MIR_T_I64,
            MIR_T_I64, reg(c, owner.reg), MIR_T_I64, integer(c, ITEM_JS_UNDEFINED),
            MIR_T_I64, integer(c, c->program->library_active), true);
        check_error(c, result); return boxed(c, result);
    }
    if (method == LMD_METHOD_FOREACH || method == LMD_METHOD_ARRAY_MAP ||
            method == LMD_METHOD_REDUCE || method == LMD_METHOD_SOME || method == LMD_METHOD_FILTER)
        return array_foreach_call(c, owner, values, count, method);
    if (method == LMD_METHOD_FLAT) {
        MIR_reg_t depth = to_number(c, number(c, 1));
        if (count) {
            MIR_label_t supplied = label(c);
            branch(c, MIR_BEQ, supplied, reg(c, tag(c, values[0])), integer(c, LMD_TYPE_UNDEFINED));
            move(c, depth, reg(c, to_number(c, values[0])), true); put_label(c, supplied);
        }
        MIR_reg_t result = em_call_4(&c->em, "mvp_lmd_array_flatten", MIR_T_I64,
            MIR_T_I64, reg(c, owner.reg), MIR_T_D, reg(c, depth), MIR_T_I64, integer(c, -1),
            MIR_T_P, integer(c, 0), true);
        check_error(c, result); return boxed(c, result, K_ARRAY);
    }
    if (method == LMD_METHOD_TO_FIXED) {
        MIR_reg_t result = em_call_2(&c->em, "mvp_lmd_number_to_fixed", MIR_T_I64,
            MIR_T_D, reg(c, to_number(c, owner)), MIR_T_D, reg(c, to_number(c, count ? values[0] : number(c, 0))), true);
        check_error(c, result); return boxed(c, result, K_STRING);
    }
    if (method == LMD_METHOD_REVERSE) {
        em_call_void_1(&c->em, "array_reverse_in_place", MIR_T_P, reg(c, owner.reg), true);
        return owner;
    }
    if (method == LMD_METHOD_SORT) {
        MIR_reg_t result = em_call_2(&c->em, "mvp_lmd_array_sort", MIR_T_I64,
            MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, count ? box(c, values[0]).reg : absent.reg), true);
        check_error(c, result); return boxed(c, result, K_ARRAY);
    }
    if (method == LMD_METHOD_PAD_START) {
        MIR_reg_t result = em_call_4(&c->em, "mvp_lmd_string_pad", MIR_T_I64,
            MIR_T_I64, reg(c, owner.reg), MIR_T_D, reg(c, to_number(c, count ? values[0] : number(c, 0))),
            MIR_T_I64, reg(c, count > 1 ? box(c, values[1]).reg : absent.reg), MIR_T_I64, integer(c, 0), true);
        check_error(c, result); return boxed(c, result, K_STRING);
    }
    if ((method >= LMD_METHOD_SUBSTRING && method <= LMD_METHOD_UPPER) || method == LMD_METHOD_ENDS_WITH) {
        MIR_reg_t result;
        unsigned kind = K_STRING;
        if (method == LMD_METHOD_UPPER) {
            result = em_call_1(&c->em, "fn_upper", MIR_T_I64, MIR_T_I64, reg(c, owner.reg), true);
        } else if (method == LMD_METHOD_SUBSTRING || method == LMD_METHOD_STRING_SLICE) {
            MIR_reg_t first = to_number(c, count ? values[0] : number(c, 0));
            MIR_reg_t end = to_number(c, number(c, INFINITY));
            if (count > 1) {
                MIR_label_t missing = label(c);
                branch(c, MIR_BEQ, missing, reg(c, tag(c, values[1])), integer(c, LMD_TYPE_UNDEFINED));
                move(c, end, reg(c, to_number(c, values[1])), true); put_label(c, missing);
            }
            result = em_call_4(&c->em, "mvp_lmd_string_range", MIR_T_I64,
                MIR_T_I64, reg(c, owner.reg), MIR_T_D, reg(c, first), MIR_T_D, reg(c, end),
                MIR_T_I64, integer(c, method == LMD_METHOD_STRING_SLICE), true);
        } else if (method == LMD_METHOD_SPLIT) {
            MIR_reg_t separator = count ? box(c, values[0]).reg : absent.reg;
            if (count) {
                MIR_label_t missing = label(c);
                MIR_reg_t converted = constant(c, ITEM_JS_UNDEFINED);
                branch(c, MIR_BEQ, missing, reg(c, tag(c, values[0])), integer(c, LMD_TYPE_UNDEFINED));
                move(c, converted, reg(c, to_string(c, values[0]).reg)); put_label(c, missing);
                separator = converted;
            }
            MIR_reg_t limit = constant(c, UINT32_MAX);
            if (count > 1) {
                MIR_label_t missing = label(c);
                branch(c, MIR_BEQ, missing, reg(c, tag(c, values[1])), integer(c, LMD_TYPE_UNDEFINED));
                move(c, limit, reg(c, op(c, MIR_AND, reg(c, int32(c, values[1])), integer(c, UINT32_MAX))));
                put_label(c, missing);
            }
            result = em_call_3(&c->em, "mvp_lmd_string_split", MIR_T_I64,
                MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, separator), MIR_T_I64, reg(c, limit), true);
            kind = K_ARRAY;
        } else {
            LmdValue needle = to_string(c, count ? values[0] : absent);
            LmdValue start = count > 1 ? values[1] : number(c, method == LMD_METHOD_ENDS_WITH ? INFINITY : 0);
            MIR_reg_t position = to_number(c, start);
            if (method == LMD_METHOD_ENDS_WITH && count > 1) {
                MIR_label_t supplied = label(c);
                branch(c, MIR_BNE, supplied, reg(c, tag(c, start)), integer(c, LMD_TYPE_UNDEFINED));
                move(c, position, MIR_new_double_op(c->em.ctx, INFINITY), true); put_label(c, supplied);
            }
            result = em_call_4(&c->em, "mvp_lmd_string_search", MIR_T_I64,
                MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, needle.reg),
                MIR_T_D, reg(c, position),
                MIR_T_I64, integer(c, method == LMD_METHOD_ENDS_WITH ? 2 : method == LMD_METHOD_STARTS_WITH), true);
            bool predicate = method == LMD_METHOD_STARTS_WITH || method == LMD_METHOD_ENDS_WITH;
            return {result, predicate ? K_BOOL : K_NUMBER, false, 0, !predicate};
        }
        check_error(c, result); return boxed(c, result, kind);
    }
    if (method == LMD_METHOD_SLICE) {
        MIR_reg_t size = em_load_at(&c->em, owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "slice_length");
        MIR_reg_t bounds[2]; sequence_bounds(c, size, values, count, bounds);
        MIR_reg_t result = em_call_3(&c->em, "fn_slice", MIR_T_I64,
            MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, box(c, {bounds[0], K_NUMBER, false, 0, true}).reg),
            MIR_T_I64, reg(c, box(c, {bounds[1], K_NUMBER, false, 0, true}).reg), true);
        check_error(c, result); return boxed(c, result, K_ARRAY);
    }
    if (method == LMD_METHOD_CHAR_AT || method == LMD_METHOD_CODE_AT) {
        LmdValue argument = count ? values[0] : number(c, 0);
        MIR_reg_t index = to_number(c, argument);
        MIR_reg_t code = 0;
        MIR_label_t slow = label(c), done = label(c);
        if (method == LMD_METHOD_CODE_AT) {
            code = to_number(c, number(c, NAN));
            MIR_reg_t string = payload(c, owner.reg);
            MIR_reg_t flags = em_load_at(&c->em, string, offsetof(String, flags), MIR_T_U8, "string_flags");
            branch_truth(c, slow, op(c, MIR_AND, reg(c, flags), integer(c, 1)), false);
            MIR_reg_t at = materialize_integer_operand(c, &argument) ? argument.reg :
                integer_lane(c, index, 0, UINT32_MAX, slow, false);
            MIR_reg_t bytes = em_load_at(&c->em, string, offsetof(String, len), MIR_T_U32, "string_bytes");
            branch(c, MIR_UBGE, done, reg(c, at), reg(c, bytes));
            MIR_reg_t address = op(c, MIR_ADD, reg(c, string), reg(c, at));
            MIR_reg_t unit = em_load_at(&c->em, address, offsetof(String, chars), MIR_T_U8, "code_unit");
            move(c, code, reg(c, to_number(c, {unit, K_NUMBER, false, 0, true})), true);
            jump(c, done); put_label(c, slow);
        }
        MIR_reg_t result = em_call_3(&c->em, "mvp_lmd_string_at", MIR_T_I64,
            MIR_T_I64, reg(c, owner.reg), MIR_T_D, reg(c, index),
            MIR_T_I64, integer(c, method == LMD_METHOD_CODE_AT ? LMD_STRING_CODE : LMD_STRING_CHAR), true);
        check_error(c, result);
        if (code) {
            move(c, code, reg(c, to_number(c, boxed(c, result, K_NUMBER))), true);
            put_label(c, done); return {code, K_NUMBER};
        }
        return boxed(c, result, K_STRING);
    }
    if (method == LMD_METHOD_REPEAT) {
        MIR_reg_t d = em_call_1(&c->em, "trunc", MIR_T_D, MIR_T_D,
            reg(c, to_number(c, count ? values[0] : number(c, 0))), true);
        MIR_label_t invalid = label(c), allocate = label(c), done = label(c);
        MIR_reg_t times = constant(c, 0);
        branch_truth(c, allocate, op(c, MIR_DEQ, reg(c, d), reg(c, d)), false);
        branch_truth(c, invalid, op(c, MIR_DGE, reg(c, d), MIR_new_double_op(c->em.ctx, 0)), false);
        branch_truth(c, invalid, op(c, MIR_DLT, reg(c, d), MIR_new_double_op(c->em.ctx, INFINITY)), false);
        MIR_reg_t source = payload(c, owner.reg);
        MIR_reg_t bytes = em_load_at(&c->em, source, offsetof(String, len), MIR_T_U32, "repeat_bytes");
        branch_truth(c, allocate, bytes, false);
        // enforce the shared allocator's representable size before converting an unbounded count.
        MIR_reg_t maximum = op(c, MIR_DDIV, MIR_new_double_op(c->em.ctx, INT_MAX - sizeof(String) - 1),
            reg(c, to_number(c, {bytes, K_NUMBER, false, 0, true})), MIR_T_D);
        branch_truth(c, invalid, op(c, MIR_DLE, reg(c, d), reg(c, maximum)), false);
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_D2I, reg(c, times), reg(c, d)));
        put_label(c, allocate);
        MIR_reg_t result = em_call_2(&c->em, "str_repeat", MIR_T_P,
            MIR_T_P, reg(c, payload(c, owner.reg)), MIR_T_I64, reg(c, times), true);
        MIR_label_t allocated = label(c); branch_truth(c, allocated, result);
        fail(c, LMD_MVP_MEMORY, NULL); put_label(c, allocated); jump(c, done);
        put_label(c, invalid); fail(c, LMD_MVP_RANGE, NULL); put_label(c, done);
        return boxed(c, op(c, MIR_OR, reg(c, result), integer(c, (uint64_t)LMD_TYPE_STRING << 56)), K_STRING);
    }
    MIR_reg_t size = em_load_at(&c->em, owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "sequence_length");
    if (method == LMD_METHOD_INCLUDES || method == LMD_METHOD_ARRAY_INDEX_OF) {
        bool includes = method == LMD_METHOD_INCLUDES;
        MIR_reg_t result = constant(c, includes ? 0 : -1), index = constant(c, 0);
        MIR_label_t start = label(c), found = label(c), next = label(c), done = label(c);
        // an empty search returns before even coercing its fromIndex argument.
        branch_truth(c, done, size, false);
        if (count > 1) {
            MIR_reg_t bounds[2]; sequence_bounds(c, size, values + 1, 1, bounds); index = bounds[0];
        }
        LmdValue sought = count ? values[0] : absent;
        put_label(c, start); branch(c, MIR_UBGE, done, reg(c, index), reg(c, size));
        if (!includes) branch(c, MIR_BEQ, next, reg(c, em_load_at(&c->em,
            em_array_element_address(&c->em, owner.reg, index, 8), 0, MIR_T_I64, "search_element")), integer(c, ITEM_JS_DELETED_SENTINEL));
        LmdReference ref = {NULL, owner, index}; ref.dense_storage = !c->program->property_callbacks;
        LmdValue current = array_read(c, ref);
        branch_truth(c, found, truth(c, binary_value(c, OPERATOR_JS_STRICT_EQ, current, sought)));
        // SameValueZero differs from strict equality only for NaN.
        if (includes) {
        MIR_reg_t nan = op(c, MIR_AND,
            reg(c, truth(c, binary_value(c, OPERATOR_JS_STRICT_NE, current, current))),
            reg(c, truth(c, binary_value(c, OPERATOR_JS_STRICT_NE, sought, sought))));
        branch_truth(c, found, nan);
        }
        put_label(c, next);
        move(c, index, reg(c, op(c, MIR_ADD, reg(c, index), integer(c, 1)))); jump(c, start);
        put_label(c, found); move(c, result, includes ? integer(c, 1) : reg(c, index)); put_label(c, done);
        return {result, includes ? K_BOOL : K_NUMBER, false, 0, !includes};
    }
    if (method == LMD_METHOD_PUSH) {
        MIR_label_t valid = label(c);
        branch(c, MIR_UBLE, valid, reg(c, size), integer(c, UINT32_MAX - (uint64_t)count));
        fail(c, LMD_MVP_RANGE, NULL); put_label(c, valid);
        for (int i = 0; i < count; i++) {
            LmdReference ref = {NULL, owner, op(c, MIR_ADD, reg(c, size), integer(c, i))};
            ref.dense_storage = !c->program->property_callbacks;
            array_index_write(c, ref, values[i]);
        }
        return {op(c, MIR_ADD, reg(c, size), integer(c, count)), K_NUMBER, false, 0, true};
    }
    if (method == LMD_METHOD_POP) {
        MIR_reg_t result = constant(c, ITEM_JS_UNDEFINED);
        MIR_label_t done = label(c); branch_truth(c, done, size, false);
        MIR_reg_t index = op(c, MIR_SUB, reg(c, size), integer(c, 1));
        // copy a tail-owned scalar before shrinking or clearing the source slot.
        LmdReference ref = {NULL, owner, index}; ref.dense_storage = !c->program->property_callbacks;
        move(c, result, reg(c, box(c, array_read(c, ref)).reg));
        em_store_at(&c->em, em_array_element_address(&c->em, owner.reg, index, 8), 0, MIR_T_I64, absent.reg);
        em_store_at(&c->em, owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, index);
        put_label(c, done); return boxed(c, result);
    }
    // primitive conversion stays in the existing MIR coercion path; fn_join2 receives strings only.
    MIR_reg_t separator = text(c, ",").reg;
    if (count) {
        MIR_label_t default_separator = label(c);
        branch(c, MIR_BEQ, default_separator, reg(c, tag(c, values[0])), integer(c, LMD_TYPE_UNDEFINED));
        move(c, separator, reg(c, to_string(c, values[0]).reg)); put_label(c, default_separator);
    }
    MIR_reg_t result = em_new_reg(&c->em, "joined", MIR_T_I64);
    MIR_reg_t scan = constant(c, 0);
    MIR_label_t inspect = label(c), convert = label(c), join = label(c), done = label(c);
    // a successful string-only scan needs no conversion allocation or user-code invocation.
    put_label(c, inspect); branch(c, MIR_UBGE, join, reg(c, scan), reg(c, size));
    MIR_reg_t part = em_load_at(&c->em,
        em_array_element_address(&c->em, owner.reg, scan, 8), 0, MIR_T_I64, "join_part");
    branch(c, MIR_BNE, convert, reg(c, op(c, MIR_URSH, reg(c, part), integer(c, 56))), integer(c, LMD_TYPE_STRING));
    move(c, scan, reg(c, op(c, MIR_ADD, reg(c, scan), integer(c, 1)))); jump(c, inspect);
    put_label(c, convert);
    // the shared converter snapshots length and owns the recursion guard before invoking elements.
    move(c, result, reg(c, em_call_3(&c->em, "mvp_lmd_array_to_string", MIR_T_I64,
        MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, separator),
        MIR_T_I64, integer(c, c->program->library_active), true)));
    jump(c, done); put_label(c, join);
    move(c, result, reg(c, em_call_2(&c->em, "fn_join2", MIR_T_I64,
        MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, separator), true)));
    put_label(c, done); check_error(c, result); return boxed(c, result, K_STRING);
}
static LmdValue sequence_method_call(LmdCompiler* c, LmdValue owner, int method,
        LmdValue* values, int count, bool dense_storage = false) {
    // these methods use the shared observable algorithm for both dense and sparse receivers.
    if (method >= LMD_METHOD_COPY_WITHIN && method <= LMD_METHOD_WITH)
        return sequence_method_dense(c, owner, method, values, count);
    int first = 0, end = 0;
    if (method == LMD_METHOD_JOIN || method == LMD_METHOD_FLAT) end = 1;
    else if (method == LMD_METHOD_FILL) { first = 1; end = 3; }
    else if (method == LMD_METHOD_SLICE) end = 2;
    else if (method == LMD_METHOD_INCLUDES || method == LMD_METHOD_ARRAY_INDEX_OF) { first = 1; end = 2; }
    for (int i = first; i < count && i < end; i++)
        if (semantic(values[i]) & (K_OBJECT | K_MAP | K_ARRAY | K_TYPED_ARRAY | K_FUNCTION)) {
            // argument coercion can resize or sparsify the receiver after a storage guard.
            return stable_item(c, sequence_method_runtime(c, owner, method, values, count));
        }
    if (dense_storage || c->program->excludes_sparse_arrays || mvp_lmd_method_owner(method) != LMD_TYPE_ARRAY || method == LMD_METHOD_CONCAT ||
            method == LMD_METHOD_SPLICE || method == LMD_METHOD_UNSHIFT || method == LMD_METHOD_SHIFT ||
            method == LMD_METHOD_TO_REVERSED || method == LMD_METHOD_AT)
        return sequence_method_dense(c, owner, method, values, count);
    MIR_label_t sparse = label(c), done = label(c);
    branch_sparse_array(c, sparse, owner.reg);
    LmdValue result = sequence_method_dense(c, owner, method, values, count);
    jump(c, done); put_label(c, sparse);
    MIR_reg_t observed = sequence_method_runtime(c, owner, method, values, count);
    bool floating = !is_boxed(result) && semantic(result) == K_NUMBER && !result.integer;
    move(c, result.reg, reg(c, is_boxed(result) ? stable_item(c, observed).reg :
        scalar_reg(c, boxed(c, observed), semantic(result), result.integer)), floating);
    put_label(c, done); return result;
}
// negative paths identify the receiver (-2) or a parameter (-3-index); -1 is failure.
static int numeric_region_path(MvpLmdProgram* p, LmdNumericRegion* region, AstNode* node, int depth = 0) {
    if (!node || depth > 16) return -1;
    if (node->node_type == AST_NODE_IDENT) {
        AstIdentNode* id = (AstIdentNode*)node;
        if (named(id->name, "this")) return -2;
        LmdBinding* b = identifier_binding(p, id);
        if (!b || b->owner != region->function) return -1;
        int index = 0;
        for (AstNode* param = region->function->ast->params; param; param = param->next, index++)
            if (js_ast_parameter_binding_identifier(param)->entry == id->entry) return -3 - index;
        return b->writes == 1 && b->dominated
            ? numeric_region_path(p, region, b->initializer, depth + 1) : -1;
    }
    if (node->node_type != AST_NODE_MEMBER_EXPR) return -1;
    AstFieldNode* member = (AstFieldNode*)node;
    if (member->computed) return -1;
    int parent = numeric_region_path(p, region, member->object, depth + 1);
    if (parent == -1) return -1;
    if (parent >= 0) region->reads[parent].kind = K_OBJECT;
    else if (parent <= -3) region->kinds[-3 - parent] = K_OBJECT;
    String* name = member_spelling(member);
    for (int i = 0; i < region->count; i++)
        if (region->reads[i].parent == parent && named(region->reads[i].name, name->chars)) return i;
    if (region->count == 16) return -1;
    int index = region->count++;
    region->reads[index] = {region, parent, name, K_NUMBER, index, NULL, object_shapes(p, member->object)};
    MvpLmdClass* home = region->function->home;
    if (parent == -2 && home && home->constructor_id >= 0) {
        LmdFunction* constructor = (LmdFunction*)p->functions->data[home->constructor_id];
        for (AstNode* n = ((AstBlockNode*)constructor->ast->body)->statements; n; n = n->next) {
            if (n->node_type != AST_NODE_EXPR_STMT) continue;
            AstNode* value = ((AstExprStmtNode*)n)->expression;
            if (value->node_type != AST_NODE_ASSIGN) continue;
            AstAssignNode* assign = (AstAssignNode*)value;
            if (assign->op != OPERATOR_ASSIGN || assign->left->node_type != AST_NODE_MEMBER_EXPR) continue;
            AstFieldNode* field = (AstFieldNode*)assign->left;
            if (field->computed || field->object->node_type != AST_NODE_IDENT ||
                    !named(((AstIdentNode*)field->object)->name, "this") || !named(member_spelling(field), name->chars)) continue;
            // initializer hints select a lane; the runtime storage guard still proves its type.
            if (kind(p, assign->right) == K_BOOL) region->reads[index].kind = K_BOOL;
        }
    }
    return index;
}
struct LmdInlineBudget { int nodes; bool allowed; MvpLmdProgram* program; LmdFunction* owner;
    bool statements; bool method; bool returns; LmdNumericRegion* region = NULL; bool recursive = false; bool calls = false; };
struct LmdInlineBody { AstNode* value; AstNode* prefix; AstNode* end; bool early_returns; unsigned nodes; bool recursive; };
static void inline_budget(AstNode* node, void* opaque) {
    LmdInlineBudget* budget = (LmdInlineBudget*)opaque;
    if (!budget->allowed || ++budget->nodes > (budget->region ? 384 : budget->statements ? 64 : 24)) {
        budget->allowed = false; return;
    }
    if (budget->region) {
        if (node->node_type == AST_NODE_BINARY || node->node_type == AST_NODE_UNARY ||
                node->node_type == AST_NODE_IF_EXPR || node->node_type == AST_NODE_CONDITIONAL_EXPR)
            budget->region->computes = true;
        switch (node->node_type) {
        case AST_NODE_IDENT: {
            AstIdentNode* id = (AstIdentNode*)node;
            if (named(id->name, "this")) break;
            LmdBinding* b = identifier_binding(budget->program, id);
            if (!b || b->captured || (b->owner != budget->owner &&
                    (b->writes != 1 || (b->kinds & ~(K_NUMBER | K_BOOL)))))
                budget->allowed = false;
            break;
        }
        case AST_NODE_MEMBER_EXPR: {
            int path = numeric_region_path(budget->program, budget->region, node);
            if (path < 0 || budget->region->sites == 128) { budget->allowed = false; return; }
            int site = budget->region->sites++;
            budget->region->site_ids[site] = ast_index_find(&budget->program->frontend->ast_index, node);
            budget->region->site_reads[site] = path;
            // property identifiers are names, not variable reads.
            inline_budget(((AstFieldNode*)node)->object, budget); return;
        }
        case AST_NODE_CALL_EXPR: {
            AstCallNode* call = (AstCallNode*)node;
            if (!budget->region->calls || call->callee->node_type != AST_NODE_MEMBER_EXPR) {
                budget->allowed = false; return;
            }
            AstFieldNode* member = (AstFieldNode*)call->callee;
            String* name = member_spelling(member);
            if (member->computed || !name) { budget->allowed = false; return; }
            LmdFunction* target = NULL;
            for (int i = 1; i < budget->program->functions->length; i++) {
                LmdFunction* f = (LmdFunction*)budget->program->functions->data[i];
                if (f->home && !f->home->dynamic && f->numeric_region && !f->numeric_region->count &&
                        !(f->returns & ~(K_NUMBER | K_BOOL)) &&
                        named(property_spelling(((AstMethodNode*)f->ast)->key, ((AstMethodNode*)f->ast)->computed), name->chars)) {
                    target = f; break;
                }
            }
            int path = target ? numeric_region_path(budget->program, budget->region, call->callee) : -1;
            if (path < 0 || budget->region->sites == 128 ||
                    ast_linked_node_count(call->arguments) < target->numeric_region->parameters) {
                budget->allowed = false; return;
            }
            LmdRegionRead* read = &budget->region->reads[path];
            budget->region->computes = true;
            read->kind = K_FUNCTION; read->target = target;
            int site = budget->region->sites++;
            budget->region->site_ids[site] = ast_index_find(&budget->program->frontend->ast_index, node);
            budget->region->site_reads[site] = path;
            inline_budget(member->object, budget);
            for (AstNode* arg = call->arguments; arg; arg = arg->next) inline_budget(arg, budget);
            return;
        }
        case AST_NODE_NEW_EXPR: {
            AstCallNode* call = (AstCallNode*)node;
            LmdBinding* b = call->callee->node_type == AST_NODE_IDENT
                ? identifier_binding(budget->program, (AstIdentNode*)call->callee) : NULL;
            MvpLmdClass* cls = class_plan(budget->program, NULL, b);
            if (!cls || !cls->reusable_layout) {
                budget->allowed = false; return;
            }
            // a fresh, receiver-unobserving initializer cannot mutate preloaded inputs.
            // allocation remains at its original point with the ordinary precise safepoint.
            budget->region->allocates = true;
            for (AstNode* arg = call->arguments; arg; arg = arg->next) inline_budget(arg, budget);
            return;
        }
        case AST_NODE_LOOP:
            budget->region->loops = true;
            break;
        case AST_NODE_INDEX_EXPR:
        case AST_NODE_MAP: case AST_NODE_OBJECT_LITERAL: case AST_NODE_PROPERTY:
            budget->allowed = false; return;
        case AST_NODE_LITERAL:
            if (((AstLiteralNode*)node)->literal_type == AST_LITERAL_STRING) budget->allowed = false;
            break;
        case AST_NODE_ASSIGN: {
            AstNode* target = ((AstAssignNode*)node)->left;
            LmdBinding* b = target->node_type == AST_NODE_IDENT
                ? identifier_binding(budget->program, (AstIdentNode*)target) : NULL;
            if (!b || b->owner != budget->owner || b->entry->is_parameter || b->captured)
                budget->allowed = false;
            break;
        }
        case AST_NODE_UNARY:
            if (((AstUnaryNode*)node)->op == OPERATOR_JS_TYPEOF) budget->allowed = false;
            break;
        default: break;
        }
        if (!budget->allowed) return;
    }
    switch (node->node_type) {
    case AST_NODE_IDENT:
        if (named(((AstIdentNode*)node)->name, "super")) budget->allowed = false;
        break;
    case AST_NODE_LITERAL: case AST_NODE_BINARY:
    case AST_NODE_MEMBER_EXPR: case AST_NODE_INDEX_EXPR: case AST_NODE_CONDITIONAL_EXPR:
        break;
    case AST_NODE_CALL_EXPR: {
        budget->calls = true;
        bool self = direct_target(budget->program, (AstCallNode*)node) == budget->owner;
        budget->recursive |= self;
        if (!budget->method && (!budget->owner->native || !self)) budget->allowed = false;
        break;
    }
    case AST_NODE_RETURN_STAM:
        if ((!budget->method && !budget->owner->native) || !budget->statements) budget->allowed = false;
        budget->returns = true;
        break;
    case AST_NODE_MAP: case AST_NODE_OBJECT_LITERAL: case AST_NODE_PROPERTY:
        if (budget->statements) budget->allowed = false;
        break;
    case AST_NODE_BLOCK: case AST_NODE_VAR_STAM: case AST_NODE_VARIABLE_DECLARATOR:
    case AST_NODE_EXPR_STMT: case AST_NODE_LOOP: case AST_NODE_IF_EXPR: case AST_NODE_NULL:
        if (!budget->statements) budget->allowed = false;
        break;
    case AST_NODE_ASSIGN: {
        AstNode* target = ((AstAssignNode*)node)->left;
        LmdBinding* b = target->node_type == AST_NODE_IDENT
            ? identifier_binding(budget->program, (AstIdentNode*)target) : NULL;
        bool property = budget->method && (target->node_type == AST_NODE_MEMBER_EXPR ||
            target->node_type == AST_NODE_INDEX_EXPR);
        bool capture = budget->method && b && b->captured;
        if (!budget->statements || (!property && !capture && (!b || b->owner != budget->owner))) budget->allowed = false;
        break;
    }
    case AST_NODE_UNARY: {
        Operator op = ((AstUnaryNode*)node)->op;
        if (op == OPERATOR_JS_DELETE) budget->allowed = false;
        if (op == OPERATOR_JS_INCREMENT || op == OPERATOR_JS_DECREMENT) {
            AstNode* target = ((AstUnaryNode*)node)->operand;
            LmdBinding* b = target->node_type == AST_NODE_IDENT
                ? identifier_binding(budget->program, (AstIdentNode*)target) : NULL;
            if (!budget->statements || !b || b->owner != budget->owner) budget->allowed = false;
        }
        break;
    }
    default: budget->allowed = false; break;
    }
    if (budget->allowed) js_ast_visit_children(node, inline_budget, budget);
}
static void plan_numeric_region(MvpLmdProgram* p, LmdFunction* f, bool calls = false) {
    // the region's own effect proof and descriptor guards exclude callbacks; unrelated dynamic code does not.
    if (!p->receiver_abi || !f->ast || f->eval_environment || f->arguments_binding || f->complex_params || f->owns_captures || f->lexical_receiver ||
            f->receiver_captured || (f->captures && f->captures->length) ||
            (f->home && f->id == f->home->constructor_id) ||
            f->duplicate_params) return;
    LmdNumericRegion region = {}; region.function = f; region.calls = calls;
    bool narrows_number = false, numbers_possible = true;
    for (AstNode* param = f->ast->params; param; param = param->next) {
        LmdBinding* b = binding(p, js_ast_parameter_binding_identifier(param)->entry);
        if (region.parameters == 8 || !b || b->writes || b->captured) return;
        narrows_number |= b->kinds != K_NUMBER;
        numbers_possible &= (b->kinds & K_NUMBER) != 0;
        region.kinds[region.parameters++] = K_NUMBER;
    }
    int locals = 0;
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        if (b->owner == f && (b->external || b->captured || ++locals > 32 ||
                (!b->entry->is_parameter && !b->dominated))) return;
    }
    LmdInlineBudget budget = {0, true, p, f, true, true, false, &region};
    inline_budget(f->ast->body, &budget);
    // scalar loops mutate only unescaped locals. Keep property snapshots,
    // allocation and calls outside this proof until their loop effects are modeled.
    // without a reachable new Number fact, a second loop body only duplicates code.
    if (region.loops && (!narrows_number || !numbers_possible ||
            region.count || region.allocates || budget.calls || f->observes_receiver)) return;
    // inherited method paths need a source prototype proof before their inputs can be preloaded.
    for (int i = 0; i < region.count; i++) if (region.reads[i].target) return;
    // passthrough getters already have a packed load; another guarded version only adds code.
    if (!region.computes && !region.allocates) return;
    // already-native scalar bodies need no second version; retain small scalar methods for inlining.
    if (!budget.allowed || (!region.count && (f->direct_args || budget.nodes > 64))) return;
    region.nodes = budget.nodes;
    f->numeric_region = (LmdNumericRegion*)pool_alloc(p->frontend->pool, sizeof(region));
    if (!f->numeric_region) { diagnostic(p, (AstNode*)f->ast, "numeric region allocation"); return; }
    *f->numeric_region = region;
    for (int i = 0; i < region.count; i++) f->numeric_region->reads[i].region = f->numeric_region;
}
static void guard_numeric_region(LmdCompiler* c, LmdInlineFrame& frame, LmdValue* values,
        int count, MIR_label_t miss) {
    LmdNumericRegion* region = frame.region;
    for (int i = 0; i < region->parameters; i++) {
        if (i >= count) { jump(c, miss); return; }
        if (region->kinds[i] != K_NUMBER) continue;
        branch(c, MIR_BNE, miss, reg(c, tag(c, values[i])), integer(c, LMD_TYPE_FLOAT));
        values[i] = {to_number(c, semantic(values[i]) == K_NUMBER ? values[i] :
            boxed(c, values[i].reg, K_NUMBER)), K_NUMBER};
    }
    MIR_reg_t shapes[16] = {}, data_slots[16] = {};
    for (int i = 0; i < region->count; i++) {
        LmdRegionRead* read = &region->reads[i];
        MIR_reg_t owner = read->parent >= 0 ? frame.region_values[read->parent].reg :
            read->parent == -2 ? frame.receiver : box(c, values[-3 - read->parent]).reg;
        if (!owner) { jump(c, miss); return; }
        // the region excludes mutation and callbacks; shared lookup has no GC safepoints.
        for (int j = 0; j < i; j++) if (region->reads[j].parent == read->parent) {
            shapes[i] = shapes[j]; data_slots[i] = data_slots[j]; break;
        }
        if (!shapes[i]) {
            em_guard_container(&c->em, owner, LMD_TYPE_MAP, miss);
            branch(c, MIR_BNE, miss, reg(c, em_load_at(&c->em, owner,
                LAMBDA_GC_OFF_CONTAINER_MAP_KIND, MIR_T_U8, "region_map_kind")), integer(c, MAP_KIND_PLAIN));
            shapes[i] = em_load_at(&c->em, owner, LAMBDA_GC_OFF_MAP_TYPE, MIR_T_I64, "region_shape");
            data_slots[i] = em_load_at(&c->em, owner, LAMBDA_GC_OFF_MAP_DATA, MIR_T_I64, "region_data");
        }
        MIR_reg_t field = own_data_field(c, owner, relocated_constant(c, s2it(read->name)), miss, shapes[i], &read->shapes, read->name);
        MIR_reg_t descriptor = field_storage(c, field, miss);
        MIR_reg_t storage = em_load_at(&c->em, descriptor, offsetof(LaneStorageDesc, kind), MIR_T_U8, "region_storage");
        MIR_reg_t offset = em_load_at(&c->em, field, offsetof(ShapeEntry, byte_offset), MIR_T_I64, "region_offset");
        MIR_reg_t slot = op(c, MIR_ADD, reg(c, data_slots[i]), reg(c, offset));
        if (read->kind == K_BOOL) {
            branch(c, MIR_BNE, miss, reg(c, storage), integer(c, LANE_STORAGE_BOOL));
            frame.region_values[i] = {em_load_at(&c->em, slot, 0, MIR_T_U8, "region_bool"), K_BOOL};
        } else if (read->kind == K_OBJECT) {
            branch(c, MIR_BNE, miss, reg(c, storage), integer(c, LANE_STORAGE_POINTER));
            branch(c, MIR_BNE, miss, reg(c, em_load_at(&c->em, descriptor,
                offsetof(LaneStorageDesc, value_domain), MIR_T_U8, "region_domain")), integer(c, LMD_TYPE_MAP));
            MIR_reg_t value = em_load_at(&c->em, slot, 0, MIR_T_I64, "region_object");
            em_guard_container(&c->em, value, LMD_TYPE_MAP, miss);
            frame.region_values[i] = boxed(c, value, K_OBJECT);
        } else {
            frame.region_values[i] = {native_number_field(c, data_slots[i], field, descriptor, miss), K_NUMBER};
        }
    }
}
static String* method_candidate_name(MvpLmdProgram* p, LmdFunction* function) {
    if (!function->ast) return NULL;
    if (function->home && !function->home->dynamic && function->ast->node_type == AST_NODE_METHOD) {
        AstMethodNode* method = (AstMethodNode*)function->ast;
        return property_spelling(method->key, method->computed);
    }
    if (function->home || function->ast->node_type != AST_NODE_FUNC_EXPR) return NULL;
    AstNode* parent = ast_index_parent(&p->frontend->ast_index, function->ast);
    if (!parent || parent->node_type != AST_NODE_PROPERTY) return NULL;
    AstPropertyNode* property = (AstPropertyNode*)parent;
    AstNode* literal = ast_index_parent(&p->frontend->ast_index, parent);
    if (!literal || (literal->node_type != AST_NODE_MAP && literal->node_type != AST_NODE_OBJECT_LITERAL)) return NULL;
    LmdObjectPlan* plan = object_plan(p, literal);
    return plan && plan->prototype_owner ? property_spelling(property->key, property->computed) : NULL;
}
static bool inline_body(LmdCompiler* c, LmdFunction* f, LmdInlineBody* body,
        bool guarded_method = false, bool constructor = false, bool numeric = false) {
    MvpLmdProgram* p = c->program;
    if (!f || f->eval_environment || f->arguments_binding || f->complex_params || (!f->closed_calls && !guarded_method) ||
            ast_linked_node_count(f->ast->params) > LAMBDA_MAX_FUNCTION_ARGS) return false;
    bool prototype_method = !f->home && method_candidate_name(p, f);
    if (f->observes_receiver && !f->home && !(guarded_method && prototype_method)) return false;
    if (guarded_method && (f->owns_captures || (!constructor && f->home && f->id == f->home->constructor_id))) return false;
    // bounded scalar/method inlining retains a real recursive edge and its stack guard (JC24).
    int depth = 0, recursive = f == c->function;
    for (LmdInlineFrame* frame = c->inlining; frame; frame = frame->parent) {
        recursive += frame->function == f;
        if (++depth >= 3) return false;
    }
    // generic callable guards do not prove a class method's layout or a scalar return ABI.
    if (recursive && ((!f->native && !(guarded_method && f->home)) || recursive > 1)) return false;
    // duplicate formals need the ordinary parameter installation order.
    if (f->duplicate_params) return false;
    int locals = 0;
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        if (b->owner == f && (b->external || ++locals > 32)) return false;
    }
    body->value = f->ast->body;
    numeric = numeric && f->numeric_region;
    LmdInlineBudget budget = {0, true, p, f, false, guarded_method, false};
    if (body->value->node_type == AST_NODE_BLOCK) {
        AstNode* first = ((AstBlockNode*)body->value)->statements;
        AstNode* last = first;
        while (last && last->next) last = last->next;
        bool returned = last && last->node_type == AST_NODE_RETURN_STAM;
        if (!returned && !guarded_method) return false;
        body->end = returned ? last : NULL;
        body->prefix = first == body->end ? NULL : first;
        budget.statements = body->prefix != NULL;
        if (!numeric) for (AstNode* node = body->prefix; node && node != body->end; node = node->next)
            inline_budget(node, &budget);
        body->early_returns = numeric ? body->prefix != NULL : budget.returns;
        body->value = returned ? ((AstReturnNode*)last)->value : NULL;
    }
    // the region proof already bounds pure arithmetic and fresh constructor allocations.
    if (numeric) { body->nodes = f->numeric_region->nodes; return true; }
    if (body->value) inline_budget(body->value, &budget);
    body->nodes = budget.nodes + 1;
    body->recursive = budget.recursive;
    // early-return native expansion is for bounded recursion; loop bodies retain their ordinary call boundary.
    if (!guarded_method && body->early_returns && !body->recursive) return false;
    // constructor expansion also duplicates allocation and fallback entries; reserve their code budget.
    // prototype functions keep a small leaf expansion; wide call graphs retain ordinary activations.
    if (prototype_method &&
            (!repeated_execution(c) || budget.calls || budget.nodes > 24)) return false;
    return budget.allowed && (!constructor || budget.nodes <= 48);
}
// both direct calls and guarded methods use the same parameter, local and ownership rules.
static LmdValue inline_call_body(LmdCompiler* c, LmdInlineFrame& frame, const LmdInlineBody& body,
        AstNode* arguments, LmdValue* values, int count, MIR_label_t fallback, bool* guarded) {
    LmdFunction* f = frame.function;
    if (f->lexical_receiver && !frame.receiver_cell) {
        MIR_reg_t env = em_load_at(&c->em, frame.self, offsetof(Function, closure_env), MIR_T_I64, "inline_env");
        frame.receiver_cell = em_load_at(&c->em, env, 0, MIR_T_I64, "inline_receiver_cell");
        root_value(c, frame.receiver_cell, JIT_VALUE_BOXED_ITEM);
    }
    AstNode* actual = arguments;
    for (AstNode* formal = f->ast->params; formal; formal = formal->next) {
        int i = frame.count++;
        frame.bindings[i] = binding(c->program, js_ast_parameter_binding_identifier(formal)->entry);
        frame.slots[i].written = body.prefix != NULL;
        frame.slots[i].kinds = frame.region ? frame.region->kinds[i] : f->closed_calls || frame.bindings[i]->writes
            ? frame.bindings[i]->kinds : i < count ? semantic(values[i]) : K_UNDEFINED;
        frame.slots[i].range = frame.bindings[i]->range;
        frame.slots[i].integer = frame.slots[i].kinds == K_NUMBER && frame.slots[i].range.state == 1;
        scalar_field_write(c, &frame.slots[i], i < count ? values[i] :
            boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED));
        frame.plans[i] = actual ? value_plan(c, actual) : NULL;
        if (body.prefix && f->direct_args && frame.plans[i] && !frame.bindings[i]->writes &&
                !frame.plans[i]->scalar_binding) {
            em_guard_map_shape(&c->em, box(c, values[i]).reg, planned_shape(c, frame.plans[i]), fallback,
                semantic(values[i]) == K_OBJECT || semantic(values[i]) == K_MAP);
            *guarded = frame.guarded[i] = true;
        }
        if (actual) actual = actual->next;
    }
    int parameters = frame.count;
    if (body.prefix) {
        for (int i = 0; i < c->program->bindings->length; i++) {
            LmdBinding* local = (LmdBinding*)c->program->bindings->data[i];
            if (local->owner != f || local->entry->is_parameter) continue;
            int slot = frame.count++;
            frame.bindings[slot] = local; frame.slots[slot].written = true;
            frame.slots[slot].kinds = frame.region ? 0 : local->dominated ? local->seed_kinds : K_ANY;
        }
        // reuse the type solver inside the guard; its facts stay local to this inlined region.
        LmdTypes types = {c->program, true, NULL, &frame};
        while (types.changed) { types.changed = false; type_walk(f->ast->body, &types); }
        for (int i = parameters; i < frame.count; i++) if (!frame.slots[i].kinds) {
            frame.slots[i].kinds = K_ANY; types.changed = true;
        }
        while (types.changed) { types.changed = false; type_walk(f->ast->body, &types); }
        for (int i = parameters; i < frame.count; i++) {
            LmdBinding* local = frame.bindings[i];
            bool native = frame.slots[i].kinds == K_NUMBER || frame.slots[i].kinds == K_BOOL;
            // inlining retains the range proof even when the original callee used a float region.
            frame.slots[i].range = local->range;
            frame.slots[i].integer = frame.slots[i].kinds == K_NUMBER && local->range.state == 1;
            scalar_field_write(c, &frame.slots[i], native ? number(c, 0) :
                boxed(c, constant(c, local->entry->is_lexical ? ITEM_JS_TDZ : ITEM_JS_UNDEFINED)));
        }
    }
    if (body.early_returns) {
        frame.result_kind = (frame.region || f->native) && (f->returns == K_NUMBER || f->returns == K_BOOL)
            ? f->returns : K_ANY;
        frame.exit = label(c); frame.result = em_new_reg(&c->em, "inline_return",
            frame.result_kind == K_NUMBER ? MIR_T_D : MIR_T_I64);
    }
    bool caller_strict = c->strict;
    c->strict = f->home || f->ast->has_use_strict_directive || (f->ast->vars && f->ast->vars->strict);
    c->inlining = &frame;
    if (body.prefix) for (AstNode* node = body.prefix; node && node != body.end; node = node->next) statement(c, node);
    LmdValue result = expression(c, body.value);
    if (frame.exit) {
        bool native = frame.result_kind != K_ANY;
        move(c, frame.result, reg(c, native ? scalar_reg(c, result, frame.result_kind) : box(c, result).reg),
            frame.result_kind == K_NUMBER);
        put_label(c, frame.exit);
        result = native ? LmdValue{frame.result, frame.result_kind} : boxed(c, frame.result);
    }
    // preserve the callee's proven integer return lane even if a local uses f64.
    if (f->native && f->integer && !result.integer)
        result = {scalar_reg(c, result, K_NUMBER, true), K_NUMBER, false, 0, true, f->range};
    c->inlining = frame.parent; c->strict = caller_strict;
    return result;
}
struct LmdCallCapture { LmdValue callee; LmdValue* values; LmdFunction* direct = NULL;
    bool borrows_arguments = false; LmdValue receiver; LmdValue new_target; };
static LmdValue capture_member_callee(LmdCompiler* c, AstNode* member, AstNode* site,
        LmdReference* ref, bool intrinsic) {
    bool grouped = js_ast_has_optional_chain(member) && !js_ast_chain_continues(site, member);
    if (!grouped) {
        *ref = reference(c, member, true);
        return box(c, read_reference(c, *ref, site, intrinsic));
    }
    // grouping finishes the chain before arguments/substitutions and preserves a successful method receiver.
    MIR_reg_t callee = constant(c, ITEM_JS_UNDEFINED), receiver = constant(c, ITEM_JS_UNDEFINED);
    MIR_label_t previous = c->optional_exit, done = label(c); c->optional_exit = done;
    LmdReference method = reference(c, member, true);
    LmdValue value = box(c, read_reference(c, method, site, false));
    move(c, callee, reg(c, value.reg));
    move(c, receiver, reg(c, method.receiver ? method.receiver : method.owner.reg));
    put_label(c, done); c->optional_exit = previous;
    *ref = {}; ref->owner = boxed(c, receiver);
    return boxed(c, callee);
}
static bool synchronous_callback_consumer(MvpLmdProgram* p, LmdFunction* f, LmdBinding** parameter) {
    AstNode* formal = f->ast->params;
    if (!formal || formal->next || f->complex_params || f->arguments_binding || f->eval_environment ||
            f->ast->is_generator || f->ast->is_async) return false;
    LmdBinding* b = binding(p, js_ast_parameter_binding_identifier(formal)->entry);
    if (!b || b->writes || b->captured || b->external) return false;
    AstIndex* index = &p->frontend->ast_index;
    uint32_t count = 0, calls = 0;
    const AstNodeId* uses = ast_index_binding_uses(index,
        ast_index_find_binding_entry(index, b->entry), &count);
    for (uint32_t i = 0; i < count; i++) {
        AstNode* use = index->nodes[uses[i]];
        if (use == formal || use == (AstNode*)js_ast_parameter_binding_identifier(formal)) continue;
        AstNode* parent = ast_index_parent(index, use);
        if (!parent || parent->node_type != AST_NODE_CALL_EXPR) return false;
        AstCallNode* call = (AstCallNode*)parent;
        if (call->callee != use || call->optional || call->interp_has_spread_args ||
                index->owner_functions[uses[i]] != index->owner_functions[ast_index_find(index, f->ast)])
            return false;
        calls++;
    }
    *parameter = b;
    return calls != 0;
}
static LmdValue call(LmdCompiler* c, AstCallNode* n, LmdCallCapture* captured);
static bool inline_literal_callback(LmdCompiler* c, AstCallNode* n, LmdValue callee,
        const LmdReference& method, LmdValue* result) {
    AstNode* argument = n->arguments;
    if (c->inlining || !c->function->id || !method.spelling || !argument || argument->next ||
            argument->node_type != AST_NODE_ARROW_FUNC || n->interp_has_spread_args) return false;
    LmdFunction* callback = function(c->program, argument);
    if (!callback || callback->parent != c->function || callback->lexical_super ||
            callback->ast->is_async || callback->ast->is_generator ||
            (callback->lexical_receiver && !c->receiver_cell)) return false;
    LmdInlineBody callback_body = {};
    if (!inline_body(c, callback, &callback_body, true)) return false;
    LmdFunction* consumer = NULL;
    LmdBinding* parameter = NULL;
    LmdInlineBody body = {};
    for (int i = 1; i < c->program->functions->length; i++) {
        LmdFunction* candidate = (LmdFunction*)c->program->functions->data[i];
        if (!named(method_candidate_name(c->program, candidate), method.spelling->chars) ||
                !synchronous_callback_consumer(c->program, candidate, &parameter) ||
                !inline_body(c, candidate, &body, true)) continue;
        // one proved consumer per site; homonymous wrappers keep the ordinary miss path.
        consumer = candidate; break;
    }
    if (!consumer ||
            c->method_inline_nodes + body.nodes + callback_body.nodes > 768) return false;
    c->method_inline_nodes += body.nodes + callback_body.nodes;
    MIR_label_t slow = label(c), done = label(c);
    MIR_reg_t joined = em_new_reg(&c->em, "callback_consumer_result", MIR_T_I64);
    // the method Get precedes the literal argument. No argument effects are replayed
    // on a miss; only the unobservable, nonescaping callback allocation is deferred.
    branch(c, MIR_BNE, slow, reg(c, tag(c, callee)), integer(c, LMD_TYPE_FUNC));
    branch(c, MIR_BNE, slow, reg(c, em_load_at(&c->em, callee.reg,
        offsetof(Function, entry_abi), MIR_T_U8, "callback_consumer_abi")), integer(c, FN_ENTRY_ABI_MVP_LMD));
    branch(c, MIR_BNE, slow, reg(c, em_load_at(&c->em, callee.reg,
        offsetof(Function, ptr), MIR_T_I64, "callback_consumer_entry")), MIR_new_ref_op(c->em.ctx, consumer->forward));
    MvpLmdCallable marker = {};
    marker.requires_runtime_context = true;
    branch(c, MIR_BEQ, slow, reg(c, op(c, MIR_AND, reg(c, em_load_at(&c->em, callee.reg,
        offsetof(Function, flags), MIR_T_U32, "callback_consumer_flags")), integer(c, marker.flags))), integer(c, 0));
    branch(c, MIR_BNE, slow, reg(c, em_load_at(&c->em, callee.reg,
        offsetof(Function, runtime_context), MIR_T_I64, "callback_consumer_context")), reg(c, c->em.frame.runtime));
    branch(c, MIR_BNE, slow, reg(c, em_load_at(&c->em, callee.reg,
        (char*)&marker.program - (char*)&marker, MIR_T_I64, "callback_consumer_program")), reg(c, c->unit));
    MIR_reg_t receiver = method.receiver ? method.receiver : method.owner.reg;
    em_guard_container(&c->em, receiver, LMD_TYPE_MAP, slow);
    LmdInlineFrame frame = {}; frame.function = consumer; frame.self = callee.reg; frame.receiver = receiver;
    frame.callback_binding = parameter; frame.callback = callback; frame.callback_body = &callback_body;
    frame.callback_receiver_cell = c->receiver_cell;
    LmdValue placeholder = boxed(c, constant(c, ITEM_JS_UNDEFINED), K_FUNCTION);
    bool guarded = false;
    LmdValue inlined = inline_call_body(c, frame, body, NULL, &placeholder, 1, slow, &guarded);
    move(c, joined, reg(c, box(c, inlined).reg)); jump(c, done);
    put_label(c, slow);
    LmdValue materialized = new_function(c, callback);
    LmdCallCapture capture = {callee, &materialized, NULL, true}; capture.receiver = method.owner;
    if (method.receiver) capture.receiver = boxed(c, method.receiver);
    move(c, joined, reg(c, box(c, call(c, n, &capture)).reg));
    put_label(c, done); *result = boxed(c, joined); return true;
}
static LmdValue call(LmdCompiler* c, AstCallNode* n, LmdCallCapture* captured = NULL) {
    if (!captured && c->program->direct_stdio &&
            (n->callee->node_type == AST_NODE_MEMBER_EXPR || n->callee->node_type == AST_NODE_INDEX_EXPR)) {
        auto* writer = (AstFieldNode*)n->callee;
        AstNode* receiver = writer->object;
        if (named(member_spelling(writer), "write") &&
                (receiver->node_type == AST_NODE_MEMBER_EXPR || receiver->node_type == AST_NODE_INDEX_EXPR)) {
            auto* stream = (AstFieldNode*)receiver;
            LmdBinding* owner = stream->object->node_type == AST_NODE_IDENT
                ? identifier_binding(c->program, (AstIdentNode*)stream->object) : NULL;
            String* name = member_spelling(stream);
            if (owner && owner->intrinsic == I_PROCESS && (named(name, "stdout") || named(name, "stderr"))) {
                // the closed-stream proof excludes observable callee changes and evaluates the argument once.
                LmdValue value = box(c, expression(c, n->arguments));
                MIR_reg_t result = em_call_1(&c->em, named(name, "stderr")
                    ? "js_process_stderr_write" : "js_process_stdout_write", MIR_T_I64,
                    MIR_T_I64, reg(c, value.reg), true);
                check_error(c, result);
                return stable_item(c, result);
            }
        }
    }
    if (!captured && c->inlining && n->callee->node_type == AST_NODE_IDENT) {
        LmdBinding* binding = identifier_binding(c->program, (AstIdentNode*)n->callee);
        for (LmdInlineFrame* consumer = c->inlining; consumer; consumer = consumer->parent) {
            if (consumer->callback_binding != binding || !consumer->callback) continue;
            LmdValue* values = call_arguments(c, n, false);
            LmdInlineFrame frame = {}; frame.parent = c->inlining; frame.function = consumer->callback;
            frame.receiver_cell = consumer->callback_receiver_cell;
            bool guarded = false; MIR_label_t done = label(c);
            LmdValue result = inline_call_body(c, frame, *consumer->callback_body, n->arguments,
                values, ast_linked_node_count(n->arguments), done, &guarded);
            put_label(c, done); mem_free(values); return result;
        }
    }
    // the TS language intrinsic shares its existing helper and respects source bindings.
    if (c->program->typescript && !captured && !n->optional &&
            n->callee->node_type == AST_NODE_IDENT) {
        AstIdentNode* name = (AstIdentNode*)n->callee;
        if (!name->entry && named(name->name, "type")) {
            LmdValue* arguments = call_arguments(c, n, false);
            LmdValue value = n->arguments ? box(c, arguments[0]) : boxed(c, constant(c, ITEM_NULL));
            MIR_reg_t result = em_call_1(&c->em, "ts_type_info", MIR_T_I64,
                MIR_T_I64, reg(c, value.reg), true);
            mem_free(arguments);
            check_error(c, result);
            return boxed(c, result, K_STRING);
        }
    }
    if (!captured && n->callee->node_type == AST_NODE_IDENT && !((AstIdentNode*)n->callee)->entry &&
            named(((AstIdentNode*)n->callee)->name, "import")) {
        LmdValue specifier = box(c, expression(c, n->arguments));
        MIR_reg_t result = em_call_3(&c->em, "mvp_lmd_dynamic_import", MIR_T_I64,
            MIR_T_P, reg(c, c->unit), MIR_T_I64, reg(c, specifier.reg), MIR_T_P,
            relocated(c, (uint64_t)c->program->frontend->reference), true);
        check_error(c, result);
        return stable_item(c, result);
    }
    if (!captured && n->callee->node_type == AST_NODE_IDENT && eval_reference_site(c->program, n->callee)) {
        LmdReference reference = eval_reference(c, (AstIdentNode*)n->callee);
        LmdValue callee = eval_reference_operation(c, reference.eval_reference, JS_INTERP_REFERENCE_GET);
        LmdValue receiver = eval_reference_operation(c, reference.eval_reference, JS_INTERP_REFERENCE_RECEIVER);
        if (n->optional) {
            branch(c, MIR_BEQ, c->optional_exit, reg(c, callee.reg), integer(c, ITEM_NULL));
            branch(c, MIR_BEQ, c->optional_exit, reg(c, callee.reg), integer(c, ITEM_JS_UNDEFINED));
        }
        AstArrayNode arguments = {}; arguments.node_type = AST_NODE_ARRAY;
        arguments.source_span = n->source_span; arguments.item = n->arguments;
        arguments.length = ast_linked_node_count(n->arguments);
        LmdValue values = expression(c, (AstNode*)&arguments);
        bool direct = !n->optional && named(((AstIdentNode*)n->callee)->name, "eval");
        MIR_label_t ordinary = direct ? label(c) : NULL, done = direct ? label(c) : NULL;
        MIR_reg_t result = em_new_reg(&c->em, "eval_call_result", MIR_T_I64);
        if (direct) {
            MIR_reg_t intrinsic = em_load_at(&c->em, module_slots(c), c->program->eval_slot * sizeof(Item), MIR_T_I64, "intrinsic_eval");
            branch(c, MIR_BNE, ordinary, reg(c, callee.reg), reg(c, intrinsic));
            LmdValue source = array_read(c, {NULL, values, constant(c, 0), true, 0});
            MIR_reg_t evaluated = em_call_3(&c->em, "mvp_lmd_dynamic_eval", MIR_T_I64,
                MIR_T_P, reg(c, c->unit), MIR_T_I64, reg(c, box(c, source).reg),
                MIR_T_P, reg(c, eval_caller(c)), true);
            check_error(c, evaluated); move(c, result, reg(c, evaluated)); jump(c, done);
            put_label(c, ordinary);
        }
        MIR_reg_t called = em_call_4(&c->em, "mvp_lmd_call_array", MIR_T_I64,
            MIR_T_I64, reg(c, callee.reg), MIR_T_I64, reg(c, receiver.reg),
            MIR_T_I64, reg(c, values.reg), MIR_T_I64, integer(c, ITEM_JS_UNDEFINED), true);
        check_error(c, called); move(c, result, reg(c, called));
        if (direct) put_label(c, done);
        return stable_item(c, result);
    }
    if (!captured && n->callee->node_type == AST_NODE_IDENT) {
        LmdBinding* binding = identifier_binding(c->program, (AstIdentNode*)n->callee);
        if (binding && binding->with_lookup) {
            LmdReference ref = with_reference(c, binding, n->callee);
            LmdValue callee = box(c, read_reference(c, ref, n->callee));
            if (n->optional) {
                branch(c, MIR_BEQ, c->optional_exit, reg(c, callee.reg), integer(c, ITEM_NULL));
                branch(c, MIR_BEQ, c->optional_exit, reg(c, callee.reg), integer(c, ITEM_JS_UNDEFINED));
            }
            LmdCallCapture resolved = {callee, n->interp_has_spread_args ? NULL : call_arguments(c, n, true)};
            resolved.receiver = boxed(c, ref.with_object);
            return call(c, n, &resolved);
        }
    }
    if (!captured && (c->function->home || c->function->lexical_super) && n->callee->node_type == AST_NODE_IDENT &&
            named(((AstIdentNode*)n->callee)->name, "super")) {
        LmdFunction* method = c->function;
        MIR_reg_t constructor = c->self;
        while (method->lexical_super) {
            int slot = function_capture_count(method);
            MIR_reg_t env = em_load_at(&c->em, constructor, offsetof(Function, closure_env), MIR_T_I64, "super_environment");
            constructor = em_load_at(&c->em, env, slot * sizeof(Item), MIR_T_I64, "super_constructor");
            root_value(c, constructor, JIT_VALUE_BOXED_ITEM);
            method = method->parent;
        }
        if (!method->home || method->id != method->home->constructor_id) {
            diagnostic(c->program, (AstNode*)n, "super call requires a derived constructor");
            return boxed(c, constant(c, ITEM_JS_UNDEFINED));
        }
        MIR_reg_t base = em_call_2(&c->em, "mvp_lmd_class_super", MIR_T_I64,
            MIR_T_I64, reg(c, constructor), MIR_T_I64, integer(c, 1), true);
        check_error(c, base);
        LmdCallCapture capture = {boxed(c, base, K_FUNCTION),
            n->interp_has_spread_args ? NULL : call_arguments(c, n, true)};
        capture.new_target = boxed(c, c->receiver_cell ? em_load_at(&c->em,
            em_array_element_address(&c->em, c->receiver_cell, constant(c, 1), 8), 0, MIR_T_I64, "super_new_target") : c->new_target);
        LmdValue result = call(c, n, &capture);
        MIR_reg_t receiver = c->receiver_cell ? em_load_at(&c->em,
            em_array_element_address(&c->em, c->receiver_cell, constant(c, 0), 8), 0, MIR_T_I64, "super_receiver") : c->receiver;
        MIR_label_t uninitialized = label(c);
        branch(c, MIR_BEQ, uninitialized, reg(c, receiver), integer(c, ITEM_JS_TDZ));
        fail(c, LMD_MVP_REFERENCE, (AstNode*)n); put_label(c, uninitialized);
        move(c, c->receiver, reg(c, result.reg));
        if (c->receiver_cell) array_index_write(c, {NULL, boxed(c, c->receiver_cell, K_ARRAY), constant(c, 0)}, result, true);
        if (method->home->has_instance_elements) {
            MIR_reg_t initialized = em_call_3(&c->em, "mvp_lmd_class_initialize_instance", MIR_T_I64,
                MIR_T_I64, reg(c, constructor), MIR_T_I64, reg(c, c->receiver), MIR_T_I64, integer(c, 0), true);
            check_error(c, initialized);
        }
        return result;
    }
    if (n->interp_has_spread_args && (!captured || !captured->values)) {
        bool member = !captured && (n->callee->node_type == AST_NODE_MEMBER_EXPR || n->callee->node_type == AST_NODE_INDEX_EXPR);
        LmdReference ref = {};
        LmdValue function = captured ? captured->callee : member ? capture_member_callee(c, n->callee, (AstNode*)n, &ref, false) : box(c, expression(c, n->callee));
        if (n->optional) {
            branch(c, MIR_BEQ, c->optional_exit, reg(c, function.reg), integer(c, ITEM_NULL));
            branch(c, MIR_BEQ, c->optional_exit, reg(c, function.reg), integer(c, ITEM_JS_UNDEFINED));
        }
        // Argument-list spread has the same ordered accumulation as an array literal.
        AstArrayNode arguments = {}; arguments.node_type = AST_NODE_ARRAY;
        arguments.source_span = n->source_span; arguments.item = n->arguments;
        arguments.length = ast_linked_node_count(n->arguments);
        LmdValue values = expression(c, (AstNode*)&arguments);
        MIR_reg_t result = em_call_4(&c->em, "mvp_lmd_call_array", MIR_T_I64,
            MIR_T_I64, reg(c, function.reg), MIR_T_I64, captured && captured->receiver.reg ? reg(c, captured->receiver.reg) :
                member ? reg(c, ref.receiver ? ref.receiver : ref.owner.reg) : integer(c, ITEM_JS_UNDEFINED),
            MIR_T_I64, reg(c, values.reg), MIR_T_I64, captured && captured->new_target.reg
                ? reg(c, captured->new_target.reg) : integer(c, ITEM_JS_UNDEFINED), true);
        check_error(c, result); return stable_item(c, result);
    }
    LmdBinding* intrinsic = !captured && n->callee->node_type == AST_NODE_IDENT
        ? identifier_binding(c->program, (AstIdentNode*)n->callee) : NULL;
    if (intrinsic && intrinsic->intrinsic == I_ARRAY) return array_construct(c, n);
    if (intrinsic && intrinsic->intrinsic == I_OBJECT) return object_construct(c, n);
    if (intrinsic && intrinsic->intrinsic == I_IS_NAN) {
        LmdValue* values = call_arguments(c, n, false);
        MIR_reg_t value = to_number(c, n->arguments ? values[0] : number(c, NAN));
        mem_free(values); return {op(c, MIR_DNE, reg(c, value), reg(c, value)), K_BOOL};
    }
    int parser = intrinsic && intrinsic->intrinsic == I_PARSE_INT ? 1 : 0;
    if (!captured && n->callee->node_type == AST_NODE_MEMBER_EXPR) {
        AstFieldNode* member = (AstFieldNode*)n->callee;
        LmdBinding* owner = member->object->node_type == AST_NODE_IDENT ? identifier_binding(c->program, (AstIdentNode*)member->object) : NULL;
        if (owner && owner->intrinsic == I_NUMBER)
            parser = named(member_spelling(member), "parseInt") ? 1 : named(member_spelling(member), "parseFloat") ? 2 : 0;
    }
    if (parser) {
        LmdValue* values = call_arguments(c, n, false);
        int count = ast_linked_node_count(n->arguments);
        LmdValue string = to_string(c, count ? values[0] : boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED));
        MIR_reg_t result = em_call_2(&c->em, parser == 1 ? "mvp_lmd_parse_integer" : "mvp_lmd_parse_number", MIR_T_D,
            MIR_T_P, reg(c, payload(c, string.reg)), MIR_T_I64,
            parser == 2 ? integer(c, 1) : count > 1 ? reg(c, int32(c, values[1])) : integer(c, 0), true);
        mem_free(values); return {result, K_NUMBER};
    }
    if (intrinsic && (intrinsic->intrinsic == I_STRING || intrinsic->intrinsic == I_NUMBER)) {
        LmdValue* values = call_arguments(c, n, false);
        bool string = intrinsic->intrinsic == I_STRING;
        if (!string && n->arguments && (semantic(values[0]) & (K_OBJECT | K_ARRAY | K_FUNCTION | K_TYPED_ARRAY))) {
            // Number permits a BigInt produced by ToPrimitive; implicit ToNumber does not.
            // preserve native numeric extraction before the observable conversion path.
            MIR_reg_t result = em_new_reg(&c->em, "explicit_number", MIR_T_D);
            LmdValue actual = box(c, values[0]);
            MIR_reg_t type = tag(c, actual);
            MIR_label_t numeric = label(c), done = label(c);
            branch(c, MIR_BEQ, numeric, reg(c, type), integer(c, LMD_TYPE_INT));
            branch(c, MIR_BEQ, numeric, reg(c, type), integer(c, LMD_TYPE_FLOAT));
            MIR_reg_t converted = em_call_2(&c->em, "mvp_lmd_primitive_to_number", MIR_T_I64,
                MIR_T_I64, reg(c, actual.reg), MIR_T_I64, integer(c, 1), true);
            check_error(c, converted);
            move(c, result, reg(c, to_number(c, boxed(c, converted, K_NUMBER))), true); jump(c, done);
            put_label(c, numeric); move(c, result, reg(c, cold_unbox(c, actual.reg)), true);
            put_label(c, done);
            mem_free(values); return {result, K_NUMBER};
        }
        if (!string && n->arguments && c->program->bigints && (semantic(values[0]) & K_BIGINT)) {
            // Number explicitly permits BigInt; implicit ToNumber and unary plus do not.
            MIR_reg_t result = em_new_reg(&c->em, "explicit_number", MIR_T_D);
            MIR_label_t ordinary = label(c), done = label(c);
            branch(c, MIR_BNE, ordinary, reg(c, tag(c, values[0])), integer(c, LMD_TYPE_DECIMAL));
            move(c, result, reg(c, em_call_1(&c->em, "bigint_to_double", MIR_T_D,
                MIR_T_I64, reg(c, box(c, values[0]).reg), true)), true); jump(c, done);
            put_label(c, ordinary); move(c, result, reg(c, to_number(c, values[0])), true);
            put_label(c, done); mem_free(values); return {result, K_NUMBER};
        }
        LmdValue result = string ? (n->arguments ? to_string(c, values[0], true) : text(c, ""))
            : n->arguments ? LmdValue{to_number(c, values[0]), K_NUMBER} : number(c, 0);
        mem_free(values); return result;
    }
    int math = captured ? -1 : math_operation(c->program, n);
    if (math >= 0) {
        LmdValue* values = call_arguments(c, n, false);
        int count = ast_linked_node_count(n->arguments);
        const auto& operation = math_operations[math];
        MIR_reg_t result;
        if (operation.arity == 0) {
            MIR_reg_t state = op(c, MIR_ADD, reg(c, c->unit), integer(c, offsetof(MvpLmdProgram, random_state)));
            result = em_call_1(&c->em, "math_splitmix64", MIR_T_D, MIR_T_P, reg(c, state), true);
        } else if (operation.arity == 1) {
            MIR_reg_t argument = to_number(c, count ? values[0] : number(c, NAN));
            result = em_call_1(&c->em, operation.native, MIR_T_D, MIR_T_D, reg(c, argument), true);
            if (!strcmp(operation.name, "round")) {
                MIR_label_t rounded = label(c);
                branch_truth(c, rounded, op(c, MIR_DGE, reg(c, op(c, MIR_DSUB, reg(c, argument), reg(c, result), MIR_T_D)),
                    MIR_new_double_op(c->em.ctx, 0.5)), false);
                move(c, result, reg(c, op(c, MIR_DADD, reg(c, result), MIR_new_double_op(c->em.ctx, 1), MIR_T_D)), true);
                put_label(c, rounded);
                result = em_call_2(&c->em, "copysign", MIR_T_D, MIR_T_D, reg(c, result), MIR_T_D, reg(c, argument), true);
            }
        }
        else if (operation.binary_once) {
            MIR_reg_t left = to_number(c, count ? values[0] : number(c, NAN));
            MIR_reg_t right = to_number(c, count > 1 ? values[1] : number(c, NAN));
            result = em_call_2(&c->em, operation.native, MIR_T_D,
                MIR_T_D, reg(c, left), MIR_T_D, reg(c, right), true);
        } else {
            result = to_number(c, count ? values[0] : number(c, !strcmp(operation.name, "min") ? INFINITY : -INFINITY));
            for (int i = 1; i < count; i++) result = em_call_2(&c->em, operation.native, MIR_T_D,
                MIR_T_D, reg(c, result), MIR_T_D, reg(c, to_number(c, values[i])), true);
        }
        mem_free(values); return {result, K_NUMBER};
    }
    bool member = !captured && ( n->callee->node_type == AST_NODE_MEMBER_EXPR || n->callee->node_type == AST_NODE_INDEX_EXPR);
    if (member) {
        AstFieldNode* field = (AstFieldNode*)n->callee;
        LmdBinding* builtin = field->object->node_type == AST_NODE_IDENT
            ? identifier_binding(c->program, (AstIdentNode*)field->object) : NULL;
        if (builtin && ((builtin->intrinsic == I_NUMBER && (named(member_spelling(field), "isNaN") || named(member_spelling(field), "isFinite"))) ||
                (!c->program->property_callbacks && builtin->intrinsic == I_ARRAY && named(member_spelling(field), "isArray")))) {
            LmdValue* values = call_arguments(c, n, false);
            LmdValue value = n->arguments ? values[0] : boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
            MIR_reg_t result = constant(c, 0);
            MIR_label_t done = label(c);
            if (builtin->intrinsic == I_ARRAY) move(c, result, reg(c, op(c, MIR_EQ,
                reg(c, tag(c, value)), integer(c, LMD_TYPE_ARRAY))));
            else {
                branch(c, MIR_BNE, done, reg(c, tag(c, value)), integer(c, LMD_TYPE_FLOAT));
                MIR_reg_t number = to_number(c, value);
                move(c, result, reg(c, named(member_spelling(field), "isFinite") ? op(c, MIR_AND,
                    reg(c, op(c, MIR_DLT, reg(c, number), MIR_new_double_op(c->em.ctx, INFINITY))),
                    reg(c, op(c, MIR_DGT, reg(c, number), MIR_new_double_op(c->em.ctx, -INFINITY)))) :
                    op(c, MIR_DNE, reg(c, number), reg(c, number))));
            }
            put_label(c, done); mem_free(values); return {result, K_BOOL};
        }
        if (builtin && builtin->intrinsic == I_STRING && (named(member_spelling(field), "fromCharCode") ||
                named(member_spelling(field), "fromCodePoint"))) {
            bool point = named(member_spelling(field), "fromCodePoint");
            LmdValue* values = call_arguments(c, n, false);
            int count = ast_linked_node_count(n->arguments);
            LmdValue result = text(c, "");
            for (int i = 0; i < count; i++) {
                MIR_reg_t unit = point ? to_number(c, values[i]) :
                    to_number(c, {op(c, MIR_AND, reg(c, int32(c, values[i])), integer(c, UINT16_MAX)), K_NUMBER, false, 0, true});
                MIR_reg_t character = em_call_3(&c->em, "mvp_lmd_string_at", MIR_T_I64,
                    MIR_T_I64, integer(c, ITEM_JS_UNDEFINED), MIR_T_D,
                    reg(c, unit), MIR_T_I64, integer(c, point ? LMD_STRING_FROM_POINT : LMD_STRING_FROM_CODE), true);
                check_error(c, character); LmdValue part = boxed(c, character, K_STRING);
                if (!i) result = part;
                else {
                    MIR_reg_t joined = em_call_2(&c->em, "mvp_lmd_string_concat", MIR_T_I64,
                        MIR_T_I64, reg(c, result.reg), MIR_T_I64, reg(c, part.reg), true);
                    check_error(c, joined); result = boxed(c, joined, K_STRING);
                }
            }
            mem_free(values); return result;
        }
        if (!c->program->property_callbacks && builtin && builtin->intrinsic == 5 && !field->computed &&
                (named(member_spelling(field), "keys") || named(member_spelling(field), "values") ||
                 named(member_spelling(field), "entries") || named(member_spelling(field), "hasOwn"))) {
            String* key = ((AstIdentNode*)field->property)->name;
            int op = named(key, "keys") ? 0 : named(key, "values") ? 1 : named(key, "entries") ? 2 :
                named(key, "hasOwn") ? 3 : -1;
            LmdValue* values = call_arguments(c, n, true);
            int count = ast_linked_node_count(n->arguments);
            LmdValue owner = count ? values[0] : boxed(c, constant(c, ITEM_JS_UNDEFINED));
            MIR_reg_t result;
            if (semantic(owner) & ~(K_OBJECT | K_MAP | K_FUNCTION | K_ARRAY | K_TYPED_ARRAY)) {
                MIR_reg_t object = em_call_2(&c->em, "mvp_lmd_box_primitive", MIR_T_I64, MIR_T_I64, reg(c, owner.reg), MIR_T_I64, integer(c, ITEM_JS_UNDEFINED), true);
                check_error(c, object); owner = boxed(c, object, K_OBJECT);
            }
            if (op == 3) {
                LmdValue key = count > 1 ? values[1] : boxed(c, constant(c, ITEM_JS_UNDEFINED));
                LmdValue name = canonical_key(c, key);
                result = property_access(c, LMD_PROP_OWN, owner.reg, name.reg);
            } else if (op >= 0) result = property_access(c, LMD_PROP_KEYS + op, owner.reg);
            else { fail(c, LMD_MVP_CAPABILITY, (AstNode*)n); result = constant(c, ITEM_JS_UNDEFINED); }
            mem_free(values); check_error(c, result); return boxed(c, result, op == 3 ? K_BOOL : K_ARRAY);
        }
    }
    LmdBinding* b = !captured && n->callee->node_type == AST_NODE_IDENT
        ? identifier_binding(c->program, (AstIdentNode*)n->callee) : NULL;
    LmdFunction* direct = captured ? captured->direct : direct_target(c->program, n);
    LmdObjectPlan* returned = !captured && direct && direct->scalar_factory
        ? object_plan(c->program, (AstNode*)n) : NULL;
    LmdScalarFactoryPlan* factory = scalar_factory_plan(returned);
    if (factory && returned->scalar_binding) {
        LmdValue* values = call_arguments(c, n, false);
        LmdInlineFrame frame = {}; frame.parent = c->inlining; frame.function = direct;
        frame.scalar_return = returned;
        LmdInlineBody body = {factory->value};
        bool guarded = false; MIR_label_t done = label(c);
        LmdValue result = inline_call_body(c, frame, body, n->arguments, values,
            ast_linked_node_count(n->arguments), done, &guarded);
        put_label(c, done); mem_free(values); return result;
    }
    LmdInlineBody body = {};
    bool numeric_region = c->function->id && direct && direct->numeric_region &&
        direct->numeric_region->count >= 2 && direct->numeric_region->nodes >= 64 &&
        !direct->numeric_region->allocates &&
        ast_linked_node_count(n->arguments) <= LAMBDA_MAX_FUNCTION_ARGS;
    bool expand = !captured && inline_body(c, direct, &body, false, false, numeric_region);
    // bounded recursive bodies retain their scalar ABI when expanded outside a loop.
    if (body.prefix && !numeric_region && !(direct && direct->native && body.recursive) &&
            !enclosing_loop(c->program, (AstNode*)n, c->function, true)) expand = false;
    if (expand) {
        // snapshot all arguments, including extras, before substituting immutable parameter reads.
        LmdValue* values = call_arguments(c, n, false);
        int count = ast_linked_node_count(n->arguments);
        LmdInlineFrame frame = {}; frame.parent = c->inlining; frame.function = direct;
        MIR_label_t fallback = label(c), done = label(c);
        bool guarded = false;
        LmdValue region_values[LAMBDA_MAX_FUNCTION_ARGS];
        if (numeric_region) {
            memcpy(region_values, values, count * sizeof(LmdValue));
            frame.region = direct->numeric_region;
            guard_numeric_region(c, frame, region_values, count, fallback);
            guarded = true;
        }
        LmdValue result = inline_call_body(c, frame, body, n->arguments,
            numeric_region ? region_values : values, count, fallback, &guarded);
        if (guarded) {
            MIR_reg_t joined = em_new_reg(&c->em, "inline_result", MIR_T_I64);
            move(c, joined, reg(c, box(c, result).reg)); jump(c, done);
            put_label(c, fallback);
            // both branches use the original argument snapshots; the outer call owns their array.
            LmdCallCapture capture = {boxed(c, constant(c, ITEM_JS_UNDEFINED)), values, direct, true};
            move(c, joined, reg(c, box(c, call(c, n, &capture)).reg));
            put_label(c, done); result = boxed(c, joined);
        } else { put_label(c, fallback); put_label(c, done); }
        mem_free(values); return result;
    }
    bool native = direct && direct->native;
    bool direct_args = direct && direct->direct_args;
    bool cold_host_call = !direct && !captured && cold_host_operation(c);
    if (cold_host_call && member) {
        String* name = member_spelling((AstFieldNode*)n->callee);
        // an unmaterialized builtin token still needs its existing receiver dispatch.
        cold_host_call = name != NULL;
        for (int i = 0; cold_host_call && i < mvp_lmd_builtin_method_count; i++)
            if (named(name, mvp_lmd_builtin_methods[i].name)) cold_host_call = false;
    }
    // cold host calls and oversized open entries reuse the complete invocation;
    // repeated operations retain their native, sequence and guarded call heads.
    bool compact_call = c->program->receiver_abi && (cold_host_call ||
        (!direct && !captured && compact_property_dispatch(c) && c->program->primitives &&
            c->program->iterators && c->program->typed_storage));
    LmdReference method = {};
    LmdValue callee = captured ? captured->callee : member ? capture_member_callee(c, n->callee, (AstNode*)n, &method, !compact_call) : direct && (!b->observed || b->closed_global) ? boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED)
        : box(c, expression(c, n->callee, false, false, n->source_span.start_byte == n->callee->source_span.start_byte));
    if (n->optional && !direct) {
        branch(c, MIR_BEQ, c->optional_exit, reg(c, callee.reg), integer(c, ITEM_NULL));
        branch(c, MIR_BEQ, c->optional_exit, reg(c, callee.reg), integer(c, ITEM_JS_UNDEFINED));
    }
    LmdValue callback_result = {};
    if (!captured && member && inline_literal_callback(c, n, callee, method, &callback_result))
        return callback_result;
    int count = ast_linked_node_count(n->arguments);
    bool typed_fill = !compact_call && member && semantic(method.owner) == K_TYPED_ARRAY && method.key_known && method.index == -4;
    unsigned receiver = semantic(method.owner);
    int sequence_method = !compact_call && member && method.spelling && (receiver == K_ARRAY || receiver == K_STRING || receiver == K_NUMBER || receiver == K_BIGINT)
        ? mvp_lmd_builtin_method(method.spelling, receiver == K_ARRAY ? LMD_TYPE_ARRAY :
            receiver == K_STRING ? LMD_TYPE_STRING : receiver == K_BIGINT ? LMD_TYPE_DECIMAL : LMD_TYPE_FLOAT) : 0;
    if (sequence_method >= LMD_METHOD_STRING_CONCAT) sequence_method = 0;
    LmdRegionRead* region_call = member && c->inlining
        ? numeric_region_read(c->program, c->inlining->region, (AstNode*)n) : NULL;
    LmdInlineBody region_body = {};
    bool region_inline = region_call && region_call->target && count <= LAMBDA_MAX_FUNCTION_ARGS &&
        (region_call->target->returns == K_NUMBER || region_call->target->returns == K_BOOL) &&
        inline_body(c, region_call->target, &region_body, true);
    LmdValue* values = captured ? captured->values : call_arguments(c, n,
        !direct_args && !typed_fill && !sequence_method && !region_inline);
    if (region_inline) {
        // the enclosing pure region already guarded this captured method's identity.
        LmdFunction* target = region_call->target;
        LmdInlineFrame frame = {}; frame.parent = c->inlining; frame.function = target;
        frame.region = target->numeric_region; frame.self = callee.reg; frame.receiver = method.owner.reg;
        LmdValue guarded_values[LAMBDA_MAX_FUNCTION_ARGS];
        memcpy(guarded_values, values, count * sizeof(LmdValue));
        MIR_label_t slow = label(c), done = label(c);
        guard_numeric_region(c, frame, guarded_values, count, slow);
        bool guarded = false;
        LmdValue result = inline_call_body(c, frame, region_body, n->arguments, guarded_values, count, slow, &guarded);
        MIR_reg_t joined = em_new_reg(&c->em, "region_call_result", target->returns == K_NUMBER ? MIR_T_D : MIR_T_I64);
        move(c, joined, reg(c, scalar_reg(c, result, target->returns)), target->returns == K_NUMBER);
        jump(c, done); put_label(c, slow);
        for (int i = 0; i < count; i++) values[i] = box(c, values[i]);
        LmdCallCapture capture = {callee, values, NULL, true}; capture.receiver = method.owner;
        result = call(c, n, &capture);
        move(c, joined, reg(c, scalar_reg(c, boxed(c, result.reg, target->returns), target->returns)),
            target->returns == K_NUMBER);
        put_label(c, done); mem_free(values);
        return {joined, target->returns};
    }
    if (typed_fill) {
        LmdValue result = array_fill_call(c, method.owner, values, count, method.lanes);
        mem_free(values); return result;
    }
    if (sequence_method) {
        if (receiver == K_ARRAY ? !c->program->array_method_overrides : !c->program->primitive_method_overrides) {
            LmdValue result = sequence_method_call(c, method.owner, sequence_method, values, count, method.dense_storage);
            mem_free(values); return result;
        }
        MIR_label_t overridden = label(c), done = label(c);
        MIR_reg_t result = em_new_reg(&c->em, "array_method_result", MIR_T_I64);
        branch(c, MIR_BNE, overridden, reg(c, callee.reg), integer(c, mvp_lmd_method_token(sequence_method)));
        move(c, result, reg(c, box(c, sequence_method_call(c, method.owner, sequence_method, values, count, method.dense_storage)).reg));
        jump(c, done); put_label(c, overridden);
        for (int i = 0; i < count; i++) values[i] = box(c, values[i]);
        LmdCallCapture capture = {callee, values, NULL, true}; capture.receiver = method.owner;
        move(c, result, reg(c, box(c, call(c, n, &capture)).reg));
        put_label(c, done); mem_free(values); return boxed(c, result);
    }
    LmdFunction* callback = !direct && b && b->target && !b->target->home ? b->target : NULL;
    bool guarded_call = !compact_call && (member || callback);
    MIR_label_t builtin_call = member && !compact_call ? label(c) : NULL, call_done = guarded_call ? label(c) : NULL;
    MIR_reg_t member_result = guarded_call ? em_new_reg(&c->em, "method_result", MIR_T_I64) : 0;
    if (builtin_call) {
        MIR_label_t ordinary = label(c);
        branch(c, MIR_BEQ, ordinary, reg(c, callee.reg), integer(c, ITEM_JS_UNDEFINED));
        branch(c, MIR_BEQ, builtin_call,
            reg(c, op(c, MIR_URSH, reg(c, callee.reg), integer(c, 56))), integer(c, LMD_TYPE_UNDEFINED));
        put_label(c, ordinary);
    }
    int i = 0;
    MIR_reg_t target = 0;
    MIR_label_t exotic = !direct && !compact_call && c->program->property_callbacks ? label(c) : NULL;
    // canonical host dependencies can introduce native callees in a closed Script;
    // receiver ABI alone does not require this cold branch.
    MIR_label_t native_call = !direct && !compact_call && !exotic &&
        native_host_values(c) ? label(c) : NULL;
    if (!direct && !compact_call) {
        if (exotic) branch(c, MIR_BEQ, exotic, reg(c, tag(c, callee)), integer(c, LMD_TYPE_MAP));
        MIR_label_t valid = label(c);
        branch(c, MIR_BEQ, valid, reg(c, tag(c, callee)), integer(c, LMD_TYPE_FUNC));
        fail(c, LMD_MVP_TYPE, (AstNode*)n); put_label(c, valid);
        MIR_reg_t abi = em_load_at(&c->em, callee.reg, offsetof(Function, entry_abi), MIR_T_U8, "callable_abi");
        if (exotic || native_call) {
            // the complete miss validates native and unsupported ABIs before reading a callable tail.
            branch(c, MIR_BNE, exotic ? exotic : native_call, reg(c, abi), integer(c, FN_ENTRY_ABI_MVP_LMD));
        } else {
            valid = label(c);
            branch(c, MIR_BEQ, valid, reg(c, abi), integer(c, FN_ENTRY_ABI_MVP_LMD));
            fail(c, LMD_MVP_CAPABILITY, (AstNode*)n); put_label(c, valid);
        }
        target = em_load_at(&c->em, callee.reg, offsetof(Function, ptr), MIR_T_I64, "entry");
        // source-node budgets miss generic dispatch growth; bound newly started entry inlines by emitted cost.
        if (!compact_property_dispatch(c) && ((member && method.spelling) || callback)) {
            int method_targets = 0;
            if (member) for (int fi = 0; fi < c->program->functions->length; fi++) {
                LmdFunction* candidate = (LmdFunction*)c->program->functions->data[fi];
                if (named(method_candidate_name(c->program, candidate), method.spelling->chars)) method_targets++;
            }
            bool allocating_inlined = false;
            // without a narrow target set, speculative guards cost more than the ordinary call.
            for (int fi = 0; method_targets <= 4 && fi < c->program->functions->length; fi++) {
                LmdFunction* candidate = (LmdFunction*)c->program->functions->data[fi];
                if (candidate != callback && (!member || !method.spelling ||
                        !named(method_candidate_name(c->program, candidate), method.spelling->chars))) continue;
                LmdInlineBody body = {};
                // one allocating target per site; other classes keep the guarded call fallback.
                // also bound total caller expansion to three ordinary statement-body budgets.
                bool native_allocation = c->function->id && !c->function->home && candidate->numeric_region &&
                    candidate->numeric_region->allocates && !candidate->numeric_region->calls &&
                    !allocating_inlined && candidate->numeric_region->nodes <= 48 &&
                    c->allocating_inline_nodes + candidate->numeric_region->nodes <= 192;
                if (!inline_body(c, candidate, &body, true, false, native_allocation)) continue;
                // statement expansion must amortize its guards and extra live values in a loop.
                // scalar loops select their guarded body at entry. Preserve
                // prior ordinary inlining without duplicating both loop bodies.
                bool numeric_region = candidate->numeric_region && !candidate->numeric_region->loops;
                bool ordinary_inline = !body.prefix || enclosing_loop(c->program, (AstNode*)n,
                    c->inlining ? c->inlining->function : c->function, true);
                if (!numeric_region && !ordinary_inline) continue;
                // depth alone does not bound wide polymorphic method graphs; reserve both versions
                // before descending so nested expansion shares the caller's finite code budget.
                unsigned expansion = body.nodes * (numeric_region && ordinary_inline &&
                    !candidate->numeric_region->count ? 2 : 1);
                if (c->method_inline_nodes + expansion > 768) continue;
                c->method_inline_nodes += expansion;
                if (native_allocation) {
                    c->allocating_inline_nodes += candidate->numeric_region->nodes;
                    allocating_inlined = true;
                }
                MIR_label_t other = label(c);
                // guard the captured callee, not its spelling: argument effects may replace the method.
                branch(c, MIR_BNE, other, reg(c, target), MIR_new_ref_op(c->em.ctx, candidate->forward));
                LmdInlineFrame frame = {}; frame.parent = c->inlining; frame.function = candidate;
                frame.self = callee.reg;
                frame.receiver = member ? (method.receiver ? method.receiver : method.owner.reg) : 0;
                if (!candidate->home && method_candidate_name(c->program, candidate)) {
                    // ordinary functions box primitive sloppy receivers and own their defining realm.
                    // restrict this expansion to an ordinary object in the current runtime/program.
                    if (!frame.receiver) { put_label(c, other); continue; }
                    em_guard_container(&c->em, frame.receiver, LMD_TYPE_MAP, other);
                    MvpLmdCallable marker = {};
                    branch(c, MIR_BNE, other, reg(c, em_load_at(&c->em, callee.reg,
                        offsetof(Function, runtime_context), MIR_T_I64, "inline_context")), reg(c, c->em.frame.runtime));
                    branch(c, MIR_BNE, other, reg(c, em_load_at(&c->em, callee.reg,
                        (char*)&marker.program - (char*)&marker, MIR_T_I64, "inline_program")), reg(c, c->unit));
                }
                LmdValue numeric_values[LAMBDA_MAX_FUNCTION_ARGS];
                LmdValue* inline_values = values;
                MIR_label_t generic_inline = numeric_region && ordinary_inline &&
                    !candidate->numeric_region->count ? label(c) : other;
                if (numeric_region && count <= LAMBDA_MAX_FUNCTION_ARGS) {
                    memcpy(numeric_values, values, count * sizeof(LmdValue));
                    frame.region = candidate->numeric_region;
                    guard_numeric_region(c, frame, numeric_values, count, generic_inline);
                    inline_values = numeric_values;
                }
                bool guarded = false;
                LmdValue inlined = inline_call_body(c, frame, body, n->arguments, inline_values, count, other, &guarded);
                move(c, member_result, reg(c, box(c, inlined).reg)); jump(c, call_done);
                if (generic_inline != other) {
                    put_label(c, generic_inline);
                    LmdInlineFrame fallback = {}; fallback.parent = frame.parent; fallback.function = candidate;
                    fallback.self = frame.self; fallback.receiver = frame.receiver;
                    // a failed lane guard must retain previously eligible generic inlining.
                    inlined = inline_call_body(c, fallback, body, n->arguments, values, count, other, &guarded);
                    move(c, member_result, reg(c, box(c, inlined).reg)); jump(c, call_done);
                }
                put_label(c, other);
            }
        }
    }
    if (exotic) put_label(c, exotic);
    MIR_reg_t span = direct_args ? 0 : call_argument_span(c, values, count);
    MIR_type_t types[LAMBDA_MAX_FUNCTION_ARGS] = {MIR_T_P, MIR_T_P, MIR_T_P, MIR_T_I64, MIR_T_I64};
    MIR_op_t args[LAMBDA_MAX_FUNCTION_ARGS] = {reg(c, c->em.frame.runtime), reg(c, c->unit), span ? reg(c, span) : integer(c, 0),
        integer(c, count), reg(c, callee.reg)};
    MirImportEntry* entry = direct_args ? &direct->entry : &c->program->entry;
    int nargs = direct_args ? direct->arity : c->program->receiver_abi ? 7 : 5;
    if (!direct_args && c->program->receiver_abi) {
        types[5] = types[6] = MIR_T_I64;
        args[5] = args[6] = integer(c, ITEM_JS_UNDEFINED);
    }
    AstNode* formal = direct_args ? direct->ast->params : NULL;
    if (direct_args) for (i = 2; i < nargs; i++, formal = formal->next) {
        types[i] = direct->signature[i].type;
        LmdBinding* parameter = binding(c->program,
            js_ast_parameter_binding_identifier(formal)->entry);
        LmdValue actual = i - 2 < count ? values[i - 2] : boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
        args[i] = reg(c, parameter->native ? scalar_reg(c, actual, parameter->kinds, parameter->integer)
            : box(c, actual).reg);
    }
    MIR_type_t result_type = native && direct->returns == K_NUMBER && !direct->integer ? MIR_T_D : MIR_T_I64;
    MIR_reg_t result = em_new_reg(&c->em, "call_result", result_type);
    bool pair = native && em_returns_result_pair(direct->return_abi.companion);
    MIR_reg_t companion = pair ? em_new_reg(&c->em, "call_error", MIR_T_I64) : 0;
    MIR_label_t invoked = NULL;
    if (c->program->receiver_abi && !direct) {
        MIR_op_t receiver = captured && captured->receiver.reg ? reg(c, captured->receiver.reg) :
            member ? reg(c, method.receiver ? method.receiver : method.owner.reg) : integer(c, ITEM_JS_UNDEFINED);
        MIR_label_t slow = label(c), fast = label(c);
        if (!compact_call) invoked = label(c);
        if (compact_call || (captured && captured->new_target.reg)) jump(c, slow);
        else {
            if (exotic) branch(c, MIR_BNE, slow, reg(c, tag(c, callee)), integer(c, LMD_TYPE_FUNC));
            // native functions can arrive through array slots or host callbacks;
            // only the MVP ABI permits reading the extended callable tail.
            if (exotic) branch(c, MIR_BNE, slow, reg(c, em_load_at(&c->em, callee.reg,
                offsetof(Function, entry_abi), MIR_T_U8, "call_abi")), integer(c, FN_ENTRY_ABI_MVP_LMD));
            // the shared ABI tag alone does not prove the extended callable tail exists.
            MvpLmdCallable marker = {}; marker.requires_runtime_context = true;
            MIR_reg_t flags = em_load_at(&c->em, callee.reg, offsetof(Function, flags), MIR_T_U32, "call_flags");
            branch(c, MIR_BEQ, slow, reg(c, op(c, MIR_AND, reg(c, flags), integer(c, marker.flags))), integer(c, 0));
            branch(c, MIR_BNE, slow, reg(c, em_load_at(&c->em, callee.reg,
                offsetof(Function, runtime_context), MIR_T_I64, "call_context")), reg(c, c->em.frame.runtime));
            branch(c, MIR_BNE, slow, reg(c, em_load_at(&c->em, callee.reg,
                (char*)&marker.constructor - (char*)&marker, MIR_T_U8, "constructor")), integer(c, 0));
            args[1] = reg(c, em_load_at(&c->em, callee.reg,
                (char*)&marker.program - (char*)&marker, MIR_T_I64, "call_program"));
            // another source unit can introduce a different MVP intrinsic owner.
            // native ABI callees already branch to the complete invocation above.
            if (c->program->dynamic_source)
                branch(c, MIR_BNE, slow, args[1], reg(c, c->unit));
            args[5] = receiver;
            jump(c, fast);
        }
        if (native_call) {
            put_label(c, native_call);
            // native ingress skipped the ordinary call's argument stores;
            // reuse the captured operands and the prologue-owned root span.
            call_argument_span(c, values, count);
        }
        put_label(c, slow);
        MIR_reg_t constructed = em_call_5(&c->em, "mvp_lmd_class_invoke", MIR_T_I64,
            MIR_T_I64, reg(c, callee.reg), MIR_T_I64,
            receiver, MIR_T_P, span ? reg(c, span) : integer(c, 0), MIR_T_I64, integer(c, count),
            MIR_T_I64, captured && captured->new_target.reg ? reg(c, captured->new_target.reg)
                : integer(c, ITEM_JS_UNDEFINED), true);
        move(c, result, reg(c, constructed));
        if (!compact_call) { jump(c, invoked); put_label(c, fast); }
    }
    if (!compact_call) {
        em_before_resolved_call(&c->em, "mvp_lmd_entry", &entry->call, nargs, types, args);
        MIR_insn_t insn = mir_new_call_with_target(c->em.ctx, entry->proto,
            direct ? MIR_new_ref_op(c->em.ctx, direct->forward) : reg(c, target), result, nargs, args, companion);
        mir_append_emit_insn(c->em.ctx, c->em.func_item, insn);
        em_after_resolved_call(&c->em, "mvp_lmd_entry", &entry->call, insn, 0, result_type);
    }
    if (invoked) put_label(c, invoked);
    // consume the context companion before any safepoint or rooted store (D5.2.1v3).
    bool pending = !direct || fn_return_shape_may_be_pending(direct->return_abi.shape);
    if (!pair && (native || pending)) companion = em_load_at(&c->em, c->em.frame.runtime,
        offsetof(Context, mir_companion_slot), MIR_T_I64, "companion");
    if (native) {
        MIR_label_t ok = label(c);
        branch(c, MIR_BEQ, ok, reg(c, companion), integer(c, pair ? ITEM_NULL : 0));
        return_error(c, companion); put_label(c, ok);
        if (!captured || !captured->borrows_arguments) mem_free(values);
        return {result, direct->returns, false, 0, direct->integer, direct->range};
    }
    MIR_reg_t resolved = result;
    if (pending) {
        int home = em_scalar_home_new(&c->em);
        MIR_reg_t address = em_materialize_frame_ref(&c->em, em_scalar_home_ref(&c->em, home));
        resolved = em_resolve_pending_pair(&c->em, result, companion, address);
        em_scalar_home_bind(&c->em, home, resolved);
    }
    if (count && !direct_args) {
        MIR_reg_t absent = constant(c, ITEM_JS_UNDEFINED);
        for (i = 0; i < count; i++) em_store_at(&c->em, span, i * sizeof(Item), MIR_T_I64, absent);
    }
    check_error(c, resolved);
    if (builtin_call) {
        move(c, member_result, reg(c, resolved)); jump(c, call_done); put_label(c, builtin_call);
        if ((semantic(method.owner) & K_TYPED_ARRAY) && (!method.spelling || named(method.spelling, "fill"))) {
            MIR_label_t map_method = label(c);
            branch(c, MIR_BNE, map_method, reg(c, callee.reg), integer(c, mvp_lmd_method_token(9)));
            branch(c, MIR_BNE, map_method, reg(c, tag(c, method.owner)), integer(c, LMD_TYPE_ARRAY_NUM));
            move(c, member_result, reg(c, array_fill_call(c, method.owner, values, count).reg)); jump(c, call_done);
            put_label(c, map_method);
        }
        for (int operation = LMD_METHOD_FILL; operation < LMD_METHOD_COUNT; operation++) {
            // planned text methods resolve live prototype functions, never the compiler's callee-only token.
            if (c->program->primitives && operation >= LMD_METHOD_STRING_NORMALIZE && operation <= LMD_METHOD_REPLACE_ALL) continue;
            TypeId owner_type = mvp_lmd_method_owner(operation);
            unsigned owner_kind = owner_type == LMD_TYPE_FLOAT ? K_NUMBER : owner_type == LMD_TYPE_STRING ? K_STRING :
                owner_type == LMD_TYPE_DECIMAL ? K_BIGINT : K_ARRAY;
            if (owner_kind == K_BIGINT && !c->program->bigints) continue;
            if (!(semantic(method.owner) & owner_kind)) continue;
            if (method.spelling && mvp_lmd_builtin_method(method.spelling,
                    owner_type) != operation) continue;
            MIR_label_t next = label(c);
            branch(c, MIR_BNE, next, reg(c, callee.reg), integer(c, mvp_lmd_method_token(operation)));
            LmdValue owner = boxed(c, method.owner.reg, owner_kind);
            // an unproven receiver otherwise duplicates the full generic equality loop at every call site.
            bool generic_search = semantic(method.owner) != K_ARRAY &&
                (operation == LMD_METHOD_INCLUDES || operation == LMD_METHOD_ARRAY_INDEX_OF);
            MIR_reg_t builtin_result = generic_search
                ? sequence_method_runtime(c, owner, operation, values, count)
                : box(c, sequence_method_call(c, owner, operation, values, count)).reg;
            move(c, member_result, reg(c, builtin_result));
            jump(c, call_done); put_label(c, next);
        }
        MIR_reg_t absent = constant(c, ITEM_JS_UNDEFINED);
        MIR_reg_t result = 0;
        MIR_label_t map_done = NULL;
        int lookup = named(method.spelling, "get") ? 1 : named(method.spelling, "set") ? 2 :
            named(method.spelling, "has") ? 3 : 0;
        if (lookup) {
            result = em_new_reg(&c->em, "map_result", MIR_T_I64);
            MIR_label_t miss = label(c); map_done = label(c);
            branch(c, MIR_BNE, miss, reg(c, callee.reg), integer(c, mvp_lmd_method_token(lookup)));
            MIR_reg_t map = em_guard_container(&c->em, method.owner.reg, LMD_TYPE_MAP, miss);
            branch(c, MIR_BNE, miss, reg(c, em_load_at(&c->em, map,
                LAMBDA_GC_OFF_CONTAINER_MAP_KIND, MIR_T_U8, "map_kind")), integer(c, MAP_KIND_ORDERED));
            MIR_reg_t entries = em_load_at(&c->em, map, offsetof(LambdaGcOrderedMapLayout, entries), MIR_T_I64, "ordered_entries");
            MIR_reg_t slot = op(c, MIR_SUB, reg(c, em_load_at(&c->em, map,
                offsetof(LambdaGcOrderedMapLayout, last_entry), MIR_T_I64, "last_entry")), integer(c, 1));
            branch(c, MIR_UBGE, miss, reg(c, slot), reg(c, em_load_at(&c->em, entries,
                LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "ordered_length")));
            MIR_reg_t row = em_array_element_address(&c->em, entries, slot, sizeof(Item));
            branch(c, MIR_BNE, miss, reg(c, em_load_at(&c->em, row, 3 * sizeof(Item), MIR_T_I64, "ordered_live")), integer(c, ITEM_TRUE));
            MIR_reg_t stored_key = em_load_at(&c->em, row, 0, MIR_T_I64, "ordered_key");
            MIR_reg_t key = count ? values[0].reg : absent;
            if (lookup == 2) {
                MIR_label_t matched = label(c);
                branch(c, MIR_BEQ, matched, reg(c, stored_key), reg(c, key));
                // byte-equal strings already match the canonical stored key; other spellings normalize on a miss.
                MIR_reg_t same = em_call_2(&c->em, "mvp_lmd_same_value_zero", MIR_T_I64,
                    MIR_T_I64, reg(c, stored_key), MIR_T_I64, reg(c, key), true);
                branch_truth(c, miss, same, false); put_label(c, matched);
            } else branch(c, MIR_BNE, miss, reg(c, stored_key), reg(c, key));
            if (lookup == 2) {
                // resolve the live ordinal after argument effects, then reuse owned Array stores for the private entry.
                LmdReference destination = {NULL, boxed(c, entries, K_ARRAY),
                    op(c, MIR_ADD, reg(c, slot), integer(c, 1))};
                array_index_write(c, destination, count > 1 ? values[1] : boxed(c, absent), true);
                move(c, result, reg(c, method.owner.reg));
            } else move(c, result, lookup == 3 ? integer(c, ITEM_TRUE) :
                reg(c, em_load_at(&c->em, row, sizeof(Item), MIR_T_I64, "ordered_value")));
            jump(c, map_done); put_label(c, miss);
        }
        MIR_reg_t fallback = em_call_4(&c->em, "mvp_lmd_map_call", MIR_T_I64,
            MIR_T_I64, reg(c, method.owner.reg), MIR_T_I64, reg(c, callee.reg),
            MIR_T_I64, reg(c, count ? values[0].reg : absent),
            MIR_T_I64, reg(c, count > 1 ? values[1].reg : absent), true);
        if (map_done) { move(c, result, reg(c, fallback)); put_label(c, map_done); }
        else result = fallback;
        check_error(c, result); move(c, member_result, reg(c, stable_item(c, result).reg));
        put_label(c, call_done); resolved = member_result;
    } else if (callback && !compact_call) {
        move(c, member_result, reg(c, resolved)); put_label(c, call_done); resolved = member_result;
    }
    if (!captured || !captured->borrows_arguments) mem_free(values);
    return boxed(c, resolved, direct ? direct->returns : K_ANY);
}
static LmdValue array_foreach_call(LmdCompiler* c, LmdValue owner, LmdValue* values, int count, int method) {
    bool map = method == LMD_METHOD_ARRAY_MAP, reduce = method == LMD_METHOD_REDUCE;
    bool some = method == LMD_METHOD_SOME, filter = method == LMD_METHOD_FILTER;
    LmdValue callback = count ? box(c, values[0]) : boxed(c, constant(c, ITEM_JS_UNDEFINED));
    MIR_label_t valid = label(c), start = label(c), next = label(c), done = label(c);
    branch(c, MIR_BEQ, valid, reg(c, tag(c, callback)), integer(c, LMD_TYPE_FUNC));
    fail(c, LMD_MVP_TYPE, NULL); put_label(c, valid);
    MIR_reg_t size = em_load_at(&c->em, owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "foreach_length");
    LmdValue output = {};
    if (reduce || some) output = boxed(c, constant(c, some ? ITEM_FALSE : ITEM_JS_UNDEFINED),
        some ? K_BOOL : K_ANY);
    MIR_reg_t initialized = reduce ? constant(c, count > 1) : 0;
    if (reduce && count > 1) move(c, output.reg, reg(c, box(c, values[1]).reg));
    if (map || filter) {
        MIR_reg_t created = em_call_1(&c->em, "mvp_lmd_array_new", MIR_T_I64, MIR_T_I64, map ? reg(c, size) : integer(c, 0), true);
        check_error(c, created); output = boxed(c, created, K_ARRAY);
    }
    MIR_reg_t index = constant(c, 0);
    put_label(c, start); branch(c, MIR_UBGE, done, reg(c, index), reg(c, size));
    MIR_reg_t live_size = em_load_at(&c->em, owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "foreach_live_length");
    branch(c, MIR_UBGE, next, reg(c, index), reg(c, live_size));
    MIR_reg_t element = em_load_at(&c->em, em_array_element_address(&c->em, owner.reg, index, 8),
        0, MIR_T_I64, "foreach_element");
    branch(c, MIR_BEQ, next, reg(c, element), integer(c, ITEM_JS_DELETED_SENTINEL));
    LmdValue value = stable_item(c, element);
    if (reduce) {
        MIR_label_t ready = label(c); branch_truth(c, ready, initialized);
        move(c, output.reg, reg(c, value.reg)); move(c, initialized, integer(c, 1)); jump(c, next);
        put_label(c, ready);
    }
    LmdValue args[4]; int argument_count = 0;
    if (reduce) args[argument_count++] = output;
    args[argument_count++] = value;
    args[argument_count++] = box(c, {index, K_NUMBER, false, 0, true});
    args[argument_count++] = owner;
    AstCallNode invocation = {}; AstNode formals[4] = {};
    for (int i = 1; i < argument_count; i++) formals[i - 1].next = &formals[i];
    invocation.arguments = formals;
    LmdCallCapture capture = {callback, args, NULL, true};
    capture.receiver = !reduce && count > 1 ? box(c, values[1]) : boxed(c, constant(c, ITEM_JS_UNDEFINED));
    LmdValue mapped = call(c, &invocation, &capture);
    if (map) array_index_write(c, {NULL, output, index}, mapped, true);
    if (filter) {
        branch_truth(c, next, truth(c, mapped), false);
        MIR_reg_t at = em_load_at(&c->em, output.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "filtered_length");
        array_index_write(c, {NULL, output, at}, value, true);
    }
    if (reduce) move(c, output.reg, reg(c, box(c, mapped).reg));
    if (some) {
        branch_truth(c, next, truth(c, mapped), false);
        move(c, output.reg, integer(c, ITEM_TRUE)); jump(c, done);
    }
    put_label(c, next);
    move(c, index, reg(c, op(c, MIR_ADD, reg(c, index), integer(c, 1)))); jump(c, start);
    put_label(c, done);
    if (reduce) {
        MIR_label_t valid = label(c); branch_truth(c, valid, initialized);
        fail(c, LMD_MVP_TYPE, NULL); put_label(c, valid);
    }
    // forEach has no loop-carried result; retain its constant undefined return and original lifetime.
    return output.reg ? output : boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
}
static Operator compound_operator(Operator op) {
    static const Operator operations[] = {OPERATOR_ADD, OPERATOR_SUB, OPERATOR_MUL,
        OPERATOR_DIV, OPERATOR_MOD, OPERATOR_JS_EXP, OPERATOR_JS_BIT_AND,
        OPERATOR_JS_BIT_OR, OPERATOR_JS_BIT_XOR, OPERATOR_JS_LSHIFT,
        OPERATOR_JS_RSHIFT, OPERATOR_JS_URSHIFT};
    // logical assignments have no eager arithmetic operation in the range planner.
    return op >= OPERATOR_JS_ADD_ASSIGN && op <= OPERATOR_JS_URSHIFT_ASSIGN
        ? operations[op - OPERATOR_JS_ADD_ASSIGN] : op;
}
static LmdValue logical(LmdCompiler* c, Operator operation, AstNode* left, AstNode* right,
        bool assignment = false) {
    LmdReference ref = {};
    LmdValue value;
    if (assignment) { ref = reference(c, left); value = read_reference(c, ref, left); }
    else value = expression(c, left);
    bool numeric = kind(c->program, left, c->inlining) == K_NUMBER && kind(c->program, right, c->inlining) == K_NUMBER;
    MIR_reg_t result = em_new_reg(&c->em, "short_circuit", numeric ? MIR_T_D : MIR_T_I64);
    MIR_label_t done = label(c);
    move(c, result, reg(c, numeric ? to_number(c, value) : box(c, value).reg), numeric);
    bool is_and = operation == OPERATOR_AND || operation == OPERATOR_JS_AND_ASSIGN;
    bool is_null = operation == OPERATOR_JS_NULLISH_COALESCE || operation == OPERATOR_JS_NULLISH_ASSIGN;
    branch_truth(c, done, is_null ? nullish(c, value) : truth(c, value), !is_and && !is_null);
    LmdValue rhs = expression(c, right);
    if (assignment) write_reference(c, ref, rhs, left);
    move(c, result, reg(c, numeric ? to_number(c, rhs) : box(c, rhs).reg), numeric);
    put_label(c, done); return numeric ? LmdValue{result, K_NUMBER} : boxed(c, result);
}
// these operands cannot overwrite a scalar source or allocate on a successful evaluation.
static bool immediate_scalar_operand(LmdCompiler* c, AstNode* n, bool inert = false, int depth = 0) {
    if (!n || depth == 16) return false;
    if (n->node_type == AST_NODE_IDENT || n->node_type == AST_NODE_LITERAL) return true;
    // the broader proof is used only after excluding callbacks and non-Number domains;
    // borrowed scalar reads retain the narrower array-only proof below.
    if (inert && n->node_type == AST_NODE_BINARY) {
        AstBinaryNode* binary = (AstBinaryNode*)n;
        return immediate_scalar_operand(c, binary->left, true, depth + 1) &&
            immediate_scalar_operand(c, binary->right, true, depth + 1);
    }
    if (inert && n->node_type == AST_NODE_UNARY) {
        AstUnaryNode* unary = (AstUnaryNode*)n;
        return (unary->op == OPERATOR_POS || unary->op == OPERATOR_NEG || unary->op == OPERATOR_NOT ||
            unary->op == OPERATOR_JS_BIT_NOT) && immediate_scalar_operand(c, unary->operand, true, depth + 1);
    }
    if (n->node_type != AST_NODE_MEMBER_EXPR && n->node_type != AST_NODE_INDEX_EXPR) return false;
    AstFieldNode* field = (AstFieldNode*)n;
    if (inert) return immediate_scalar_operand(c, field->object, true, depth + 1) &&
        (!field->computed || immediate_scalar_operand(c, field->property, true, depth + 1));
    if (field->object->node_type != AST_NODE_IDENT) return false;
    LmdBinding* owner = identifier_binding(c->program, (AstIdentNode*)field->object);
    // intrinsic receivers have no lexical NameEntry and cannot prove an array read.
    if (!owner || !owner->entry || (!owner->entry->is_parameter && !owner->dominated) ||
            kind(c->program, field->object, c->inlining) != K_ARRAY) return false;
    if (!field->computed || field->property->node_type == AST_NODE_LITERAL) return true;
    if (field->property->node_type != AST_NODE_IDENT) return false;
    unsigned key = kind(c->program, field->property, c->inlining);
    return key == K_NUMBER || key == K_STRING;
}
static bool remainder_zero(LmdCompiler* c, AstBinaryNode* test, LmdValue* result) {
    bool unequal = test->op == OPERATOR_NE || test->op == OPERATOR_JS_STRICT_NE;
    if (!unequal && test->op != OPERATOR_EQ && test->op != OPERATOR_JS_STRICT_EQ) return false;
    if (test->right->node_type != AST_NODE_LITERAL ||
            ((AstLiteralNode*)test->right)->literal_type != AST_LITERAL_NUMBER ||
            ((AstLiteralNode*)test->right)->value.number_value != 0 ||
            test->left->node_type != AST_NODE_BINARY) return false;
    AstBinaryNode* remainder = (AstBinaryNode*)test->left;
    if (remainder->op != OPERATOR_MOD || remainder->right->node_type != AST_NODE_LITERAL ||
            ((AstLiteralNode*)remainder->right)->literal_type != AST_LITERAL_NUMBER) return false;
    double divisor = ((AstLiteralNode*)remainder->right)->value.number_value;
    if (divisor < 1 || divisor > 9007199254740992.0 || divisor != trunc(divisor)) return false;
    uint64_t integer_divisor = (uint64_t)divisor;
    if (integer_divisor & (integer_divisor - 1)) return false;
    LmdValue value = expression(c, remainder->left, false, true);
    MIR_reg_t zero;
    if (value.integer && !value.number_present) {
        zero = op(c, MIR_EQ, reg(c, op(c, MIR_AND, reg(c, value.reg), integer(c, integer_divisor - 1))), integer(c, 0));
    } else {
        unsigned power = 0;
        for (uint64_t d = integer_divisor; d > 1; d >>= 1) power++;
        MIR_reg_t bits = em_emit_double_bits(&c->em, to_number(c, value));
        MIR_reg_t magnitude = op(c, MIR_AND, reg(c, bits), integer(c, INT64_MAX));
        MIR_reg_t exponent = op(c, MIR_URSH, reg(c, magnitude), integer(c, 52));
        MIR_label_t exceptional = label(c), done = label(c);
        zero = constant(c, 0);
        MIR_reg_t shift = op(c, MIR_SUB, reg(c, exponent), integer(c, 1023 + power));
        // ordinary exponents need one guard; keep zero, tiny and huge Numbers off this path.
        branch(c, MIR_UBGE, exceptional, reg(c, shift), integer(c, 52));
        MIR_reg_t fraction = op(c, MIR_AND,
            reg(c, op(c, MIR_LSH, reg(c, magnitude), reg(c, shift))), integer(c, (UINT64_C(1) << 52) - 1));
        move(c, zero, reg(c, op(c, MIR_EQ, reg(c, fraction), integer(c, 0))));
        jump(c, done); put_label(c, exceptional);
        MIR_reg_t large = op(c, MIR_ULT,
            reg(c, op(c, MIR_SUB, reg(c, shift), integer(c, 52))), integer(c, 2047 - 1023 - power - 52));
        move(c, zero, reg(c, op(c, MIR_OR, reg(c, large),
            reg(c, op(c, MIR_EQ, reg(c, magnitude), integer(c, 0)))))); put_label(c, done);
    }
    *result = {unequal ? op(c, MIR_XOR, reg(c, zero), integer(c, 1)) : zero, K_BOOL};
    return true;
}
static bool string_concat(LmdCompiler* c, AstBinaryNode* node, LmdValue* result) {
    AstNode* leaves[LAMBDA_STRING_CONCAT_MAX_PARTS];
    int count = 0;
    AstNode* left = (AstNode*)node;
    const unsigned primitives = K_STRING | K_NUMBER | K_BOOL | K_NULL | K_UNDEFINED;
    while (left->node_type == AST_NODE_BINARY && count < LAMBDA_STRING_CONCAT_MAX_PARTS - 1) {
        AstBinaryNode* part = (AstBinaryNode*)left;
        unsigned right_kind = kind(c->program, part->right, c->inlining);
        if (part->op != OPERATOR_ADD || kind(c->program, part->left, c->inlining) != K_STRING ||
                !right_kind || (right_kind & ~primitives)) break;
        leaves[count++] = part->right; left = part->left;
    }
    if (count < 2) return false;
    leaves[count++] = left;
    MIR_type_t types[LAMBDA_STRING_CONCAT_MAX_PARTS]; MIR_op_t args[LAMBDA_STRING_CONCAT_MAX_PARTS];
    for (int i = 0; i < count; i++) {
        // only primitive conversions can be combined; retain source evaluation and conversion order.
        LmdValue part = to_string(c, expression(c, leaves[count - i - 1]));
        types[i] = MIR_T_P; args[i] = reg(c, payload(c, part.reg));
    }
    const char* helpers[] = {"fn_strcat3", "fn_strcat4", "fn_strcat5", "fn_strcat6"};
    MIR_reg_t joined;
    if (count <= 6) joined = em_call_with_args(&c->em, helpers[count - 3], MIR_T_P, count, types, args, true);
    else {
        // reuse Lambda's variadic ABI and precise root handling for long primitive chains.
        MIR_var_t prefix[] = {{MIR_T_I64, "owned", 0}, {MIR_T_I64, "count", 0}};
        MIR_op_t values[] = {integer(c, 0), integer(c, count)};
        joined = em_emit_vararg_call(&c->em, "fn_strcat_many", MIR_T_P, 1, 2, prefix, values, count, args);
    }
    MIR_label_t valid = label(c), invalid = label(c);
    branch_truth(c, invalid, joined, false);
    branch(c, MIR_BNE, valid, reg(c, joined), relocated(c, (uint64_t)&STR_ERROR));
    put_label(c, invalid); fail(c, LMD_MVP_MEMORY, (AstNode*)node); put_label(c, valid);
    joined = em_call_1(&c->em, "fn_string_freeze", MIR_T_P, MIR_T_P, reg(c, joined), true);
    *result = boxed(c, op(c, MIR_OR, reg(c, joined), integer(c, (uint64_t)LMD_TYPE_STRING << 56)), K_STRING);
    return true;
}
static LmdValue expression(LmdCompiler* c, AstNode* n, bool borrow_scalar, bool numeric, bool chain_continuation) {
    bool optional_delete = n && n->node_type == AST_NODE_UNARY && ((AstUnaryNode*)n)->op == OPERATOR_JS_DELETE &&
        js_ast_has_optional_chain(((AstUnaryNode*)n)->operand);
    if (n && !chain_continuation && (js_ast_has_optional_chain(n) || optional_delete)) {
        MIR_reg_t output = constant(c, optional_delete ? ITEM_TRUE : ITEM_JS_UNDEFINED);
        MIR_label_t previous = c->optional_exit, done = label(c); c->optional_exit = done;
        LmdValue value = box(c, expression(c, n, false, false, true));
        move(c, output, reg(c, value.reg)); put_label(c, done); c->optional_exit = previous;
        return boxed(c, output, optional_delete ? K_BOOL : semantic(value) | K_UNDEFINED);
    }
    if (!n) return boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
    switch (n->node_type) {
    case AST_NODE_AWAIT: {
        LmdValue value = box(c, expression(c, ((JsAwaitNode*)n)->argument));
        MIR_reg_t resumed = emit_await(c, value.reg);
        check_error(c, resumed);
        return stable_item(c, resumed);
    }
    case AST_NODE_YIELD: {
        JsYieldNode* yield = (JsYieldNode*)n;
        LmdValue value = box(c, expression(c, yield->argument));
        bool async = c->function->ast->is_async;
        MIR_reg_t resumed = em_call_2(&c->em, async ? "js_gen_park" : "mvp_lmd_generator_park", MIR_T_I64,
            MIR_T_I64, reg(c, value.reg), MIR_T_I64,
            integer(c, async ? (yield->delegate ? JS_GEN_PARK_DELEGATE : JS_GEN_PARK_YIELD) : yield->delegate ? 2 : 0), true);
        if (async) {
            check_error(c, resumed); LmdValue result = stable_item(c, resumed);
            MIR_label_t normal = label(c), throwing = label(c);
            MIR_reg_t signal = em_call_1(&c->em, "js_gen_is_return_signal", MIR_T_I64, MIR_T_I64, reg(c, result.reg), true);
            branch_truth(c, throwing, signal, false);
            MIR_reg_t returned = em_call_1(&c->em, "js_gen_return_signal_value", MIR_T_I64, MIR_T_I64, reg(c, result.reg), true);
            return_value(c, stable_item(c, returned)); put_label(c, throwing);
            signal = em_call_1(&c->em, "js_gen_is_throw_signal", MIR_T_I64, MIR_T_I64, reg(c, result.reg), true);
            branch_truth(c, normal, signal, false);
            MIR_reg_t thrown = em_call_1(&c->em, "js_gen_throw_signal_value", MIR_T_I64, MIR_T_I64, reg(c, result.reg), true);
            MIR_reg_t error = em_call_2(&c->em, "mvp_lmd_throw", MIR_T_I64, MIR_T_I64, reg(c, thrown), MIR_T_I64, integer(c, 0), true);
            return_error(c, error); put_label(c, normal); return result;
        }
        check_error(c, resumed); LmdValue result = stable_item(c, resumed);
        MIR_reg_t operation = em_call_0(&c->em, "mvp_lmd_generator_resume_kind", MIR_T_I64, true);
        MIR_label_t throwing = label(c), normal = label(c);
        branch(c, MIR_BNE, throwing, reg(c, operation), integer(c, 1));
        return_value(c, result); put_label(c, throwing);
        branch(c, MIR_BNE, normal, reg(c, operation), integer(c, 2));
        MIR_reg_t error = em_call_2(&c->em, "mvp_lmd_throw", MIR_T_I64,
            MIR_T_I64, reg(c, result.reg), MIR_T_I64, integer(c, 0), true);
        return_error(c, error); put_label(c, normal); return result;
    }
    case JS_AST_NODE_REGEX: {
        JsRegexNode* literal = (JsRegexNode*)n;
        String* pattern = name_pool_create_len(c->program->frontend->name_pool, literal->pattern, literal->pattern_len);
        String* flags = name_pool_create_len(c->program->frontend->name_pool, literal->flags, literal->flags_len);
        MIR_reg_t result = em_call_3(&c->em, "mvp_lmd_regexp_new", MIR_T_I64,
            MIR_T_P, relocated(c, (uint64_t)c->program->library), MIR_T_I64, relocated(c, s2it(pattern)),
            MIR_T_I64, relocated(c, s2it(flags)), true);
        check_error(c, result); return boxed(c, result, K_OBJECT);
    }
    case AST_NODE_LITERAL: {
        AstLiteralNode* l = (AstLiteralNode*)n;
        if (l->is_bigint) {
            MIR_reg_t result = em_call_2(&c->em, "bigint_from_string", MIR_T_I64,
                MIR_T_P, relocated(c, (uint64_t)l->bigint_str->chars), MIR_T_I64, integer(c, l->bigint_str->len), true);
            check_error(c, result); return boxed(c, result, K_BIGINT);
        }
        if (l->literal_type == AST_LITERAL_NUMBER) return number(c, l->value.number_value);
        if (l->literal_type == AST_LITERAL_STRING)
            return boxed(c, relocated_constant(c, s2it(l->value.string_value)), K_STRING);
        if (l->literal_type == AST_LITERAL_BOOLEAN) return {constant(c, l->value.boolean_value), K_BOOL};
        return boxed(c, constant(c, l->literal_type == AST_LITERAL_NULL ? ITEM_NULL : ITEM_JS_UNDEFINED),
            l->literal_type == AST_LITERAL_NULL ? K_NULL : K_UNDEFINED);
    }
    case JS_AST_NODE_TEMPLATE_LITERAL: {
        JsTemplateLiteralNode* literal = (JsTemplateLiteralNode*)n;
        AstNode* next = literal->expressions;
        LmdValue result = text(c, "");
        for (AstNode* quasi = literal->quasis; quasi; quasi = quasi->next) {
            String* cooked = ((JsTemplateElementNode*)quasi)->cooked;
            LmdValue part = boxed(c, relocated_constant(c, s2it(cooked)), K_STRING);
            if (quasi == literal->quasis) result = part;
            else result = binary_value(c, OPERATOR_ADD, result, part);
            if (next) {
                part = to_string(c, expression(c, next));
                result = binary_value(c, OPERATOR_ADD, result, part); next = next->next;
            }
        }
        return result;
    }
    case JS_AST_NODE_TAGGED_TEMPLATE: {
        JsTaggedTemplateNode* tagged = (JsTaggedTemplateNode*)n;
        bool member = tagged->tag->node_type == AST_NODE_MEMBER_EXPR || tagged->tag->node_type == AST_NODE_INDEX_EXPR;
        LmdReference ref = {};
        LmdBinding* binding = tagged->tag->node_type == AST_NODE_IDENT ? identifier_binding(c->program, (AstIdentNode*)tagged->tag) : NULL;
        bool eval = tagged->tag->node_type == AST_NODE_IDENT && eval_reference_site(c->program, tagged->tag);
        bool dynamic = eval || (binding && binding->with_lookup);
        if (eval) ref = eval_reference(c, (AstIdentNode*)tagged->tag);
        else if (dynamic) ref = with_reference(c, binding, tagged->tag);
        if (eval) ref.with_object = eval_reference_operation(c, ref.eval_reference, JS_INTERP_REFERENCE_RECEIVER).reg;
        LmdValue callee = dynamic ? box(c, read_reference(c, ref, tagged->tag)) :
            member ? capture_member_callee(c, tagged->tag, n, &ref, false) : box(c, expression(c, tagged->tag));
        uint32_t slot = c->program->template_slots[ast_index_find(&c->program->frontend->ast_index, n)] - 1;
        MIR_reg_t object = em_call_3(&c->em, "mvp_lmd_template_object", MIR_T_I64,
            MIR_T_P, reg(c, c->unit), MIR_T_P, relocated(c, (uint64_t)tagged->quasi), MIR_T_I64, integer(c, slot), true);
        check_error(c, object);
        int count = ast_linked_node_count(tagged->quasi->expressions) + 1;
        LmdValue* values = (LmdValue*)mem_calloc(count, sizeof(LmdValue), MEM_CAT_TEMP);
        if (!values) { diagnostic(c->program, n, "template argument allocation"); return boxed(c, object); }
        values[0] = boxed(c, object, K_ARRAY);
        int i = 1;
        for (AstNode* node = tagged->quasi->expressions; node; node = node->next)
            values[i++] = box(c, expression(c, node));
        // reuse normal call capture so tag access precedes substitutions and member receivers survive.
        AstNode first = {}; first.next = tagged->quasi->expressions;
        AstCallNode invocation = {}; invocation.node_type = AST_NODE_CALL_EXPR;
        invocation.source_span = n->source_span; invocation.callee = tagged->tag; invocation.arguments = &first;
        LmdCallCapture captured = {callee, values};
        if (dynamic || member) captured.receiver = boxed(c, dynamic ? ref.with_object : ref.receiver ? ref.receiver : ref.owner.reg);
        return call(c, &invocation, &captured);
    }
    case AST_NODE_IDENT: {
        AstIdentNode* id = (AstIdentNode*)n;
        if (!id->entry && named(id->name, "import.meta")) {
            MIR_reg_t value = em_call_1(&c->em, "js_module_import_meta", MIR_T_I64,
                MIR_T_P, relocated(c, (uint64_t)c->program->source_module), true);
            check_error(c, value); return boxed(c, value, K_OBJECT);
        }
        // inlined bodies retain their own receiver even inside script/static initialization.
        if (!c->inlining && !c->initializer_class && c->program->source_module && named(id->name, "this")) {
            LmdFunction* function = c->function;
            while (function->ast && function->ast->node_type == AST_NODE_ARROW_FUNC) function = function->parent;
            if (!function->ast) return boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
        }
        if (!c->inlining && !c->initializer_class && named(id->name, "this")) {
            LmdFunction* owner = c->function;
            while (owner->ast && owner->ast->node_type == AST_NODE_ARROW_FUNC) owner = owner->parent;
            if (!owner->ast) {
                // Script arrows read their realm's lexical global this, independent of call receivers.
                MIR_reg_t slots = em_load_at(&c->em, c->unit, offsetof(MvpLmdProgram, slots), MIR_T_I64, "program_slots");
                return boxed(c, em_load_at(&c->em, slots, c->program->global_slot * sizeof(Item), MIR_T_I64, "global_receiver"), K_OBJECT);
            }
        }
        if (!c->inlining && c->initializer_class && named(id->name, "this")) return boxed(c, c->receiver, K_FUNCTION);
        if (c->inlining && c->inlining->receiver_cell &&
                (named(id->name, "this") || named(id->name, "new.target"))) {
            MIR_reg_t value = em_load_at(&c->em, em_array_element_address(&c->em, c->inlining->receiver_cell,
                constant(c, named(id->name, "this") ? 0 : 1), 8), 0, MIR_T_I64, "inline_receiver");
            if (named(id->name, "this")) check_tdz(c, NULL, value, n);
            return boxed(c, value);
        }
        if (c->inlining && c->inlining->receiver &&
                (named(id->name, "this") || named(id->name, "new.target")))
            return named(id->name, "this")
                ? boxed(c, op(c, MIR_OR, reg(c, c->inlining->receiver), integer(c, 0)))
                : boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
        if ((c->function->home || c->function->lexical_receiver || c->function->observes_receiver) &&
                (named(id->name, "this") || named(id->name, "new.target"))) {
            bool constructor = c->function->home && c->function->id == c->function->home->constructor_id;
            MIR_reg_t value = named(id->name, "this") ? c->receiver : c->new_target;
            if (c->function->lexical_receiver || (constructor && c->receiver_cell)) value = em_load_at(&c->em,
                em_array_element_address(&c->em, c->receiver_cell,
                    constant(c, named(id->name, "this") ? 0 : 1), 8), 0, MIR_T_I64, "lexical_receiver");
            // ordinary calls already bind this; only constructor/lexical receivers can retain TDZ.
            if (named(id->name, "this") && (constructor || c->function->lexical_receiver))
                check_tdz(c, NULL, value, n);
            // preserve the value of an earlier this read across a later super() call.
            unsigned hint = K_ANY;
            if (named(id->name, "this") && c->function->object_home) {
                AstNode* parent = ast_index_parent(&c->program->frontend->ast_index, (AstNode*)c->function->ast);
                // private static initializers receive their class constructor through the sole call edge.
                if (parent && (parent->node_type == JS_AST_NODE_STATIC_BLOCK ||
                        (parent->node_type == AST_NODE_FIELD && ((AstClassFieldNode*)parent)->is_static)))
                    hint = K_FUNCTION;
            }
            return boxed(c, op(c, MIR_OR, reg(c, value), integer(c, 0)), hint);
        }
        if ((c->function->home || c->function->object_home || c->function->lexical_super) && named(id->name, "super")) {
            MIR_reg_t receiver = c->receiver_cell ? em_load_at(&c->em,
                em_array_element_address(&c->em, c->receiver_cell, constant(c, 0), 8), 0, MIR_T_I64, "super_receiver") : c->receiver;
            if (c->function->lexical_receiver ||
                    (c->function->home && c->function->id == c->function->home->constructor_id))
                check_tdz(c, NULL, receiver, n);
            MIR_reg_t base = em_call_2(&c->em, "mvp_lmd_class_super", MIR_T_I64,
                MIR_T_I64, reg(c, c->self), MIR_T_I64, integer(c, 0), true);
            check_error(c, base); return boxed(c, base, K_OBJECT);
        }
        LmdBinding* b = identifier_binding(c->program, id);
        return read_binding(c, b, n, borrow_scalar);
    }
    case AST_NODE_FUNC: case AST_NODE_FUNC_EXPR: case AST_NODE_ARROW_FUNC:
        return new_function(c, function(c->program, n));
    case AST_NODE_CLASS_EXPR: return class_expression(c, (AstClassNode*)n);
    case AST_NODE_CALL_EXPR: return call(c, (AstCallNode*)n);
    case AST_NODE_MEMBER_EXPR: case AST_NODE_INDEX_EXPR: {
        AstFieldNode* field = (AstFieldNode*)n;
        LmdBinding* intrinsic = !field->computed && field->object->node_type == AST_NODE_IDENT
            ? identifier_binding(c->program, (AstIdentNode*)field->object) : NULL;
        if (intrinsic && intrinsic->intrinsic == I_MATH && !c->program->math_observed && named(member_spelling(field), "PI"))
            return number(c, M_PI);
        if (intrinsic && intrinsic->intrinsic == I_NUMBER) {
            String* name = member_spelling(field);
            if (named(name, "POSITIVE_INFINITY")) return number(c, INFINITY);
            if (named(name, "NEGATIVE_INFINITY")) return number(c, -INFINITY);
            if (named(name, "NaN")) return number(c, NAN);
        }
        LmdRegionRead* read = numeric_region_read(c->program, c->inlining ? c->inlining->region : NULL, n);
        if (read && c->inlining && c->inlining->region == read->region)
            return c->inlining->region_values[read->index];
        return read_reference(c, reference(c, n), n, false, borrow_scalar, numeric);
    }
    case AST_NODE_ASSIGN: {
        AstAssignNode* a = (AstAssignNode*)n;
        if (a->left->node_type == AST_NODE_MAP_PATTERN) {
            LmdValue value = box(c, expression(c, a->right));
            bind_pattern(c, a->left, value, false); return value;
        }
        if (a->left->node_type == AST_NODE_ARRAY || a->left->node_type == AST_NODE_ARRAY_PATTERN) {
            AstNode* parent = ast_index_parent(&c->program->frontend->ast_index, n);
            bool literal = !c->function->eval_lookup && a->right->node_type == AST_NODE_ARRAY && simple_array_pattern(a->left) &&
                pattern_iterators_are_intrinsic(c->program);
            if (literal) for (AstNode* e = ((AstArrayNode*)a->right)->item; e; e = e->next)
                if (e->node_type == AST_NODE_SPREAD || e->node_type == AST_NODE_NULL) literal = false;
            if (literal && parent && parent->node_type == AST_NODE_EXPR_STMT &&
                    (c->function->id || c->inlining)) {
                int count = ast_linked_node_count(((AstArrayNode*)a->right)->item), i = 0;
                LmdValue* values = (LmdValue*)mem_calloc(count, sizeof(LmdValue), MEM_CAT_TEMP);
                // snapshot every RHS before ordered writes; extra elements still run for their effects.
                for (AstNode* e = ((AstArrayNode*)a->right)->item; e; e = e->next)
                    values[i++] = expression(c, e);
                i = 0;
                for (AstNode* e = ((AstArrayNode*)a->left)->item; e; e = e->next, i++)
                    write_binding(c, identifier_binding(c->program, (AstIdentNode*)e),
                        i < count ? values[i] : boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED), e);
                mem_free(values);
                return boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
            }
            LmdValue owner = box(c, expression(c, a->right));
            if (literal) {
                int i = 0;
                for (AstNode* e = ((AstArrayNode*)a->left)->item; e; e = e->next, i++)
                    write_binding(c, identifier_binding(c->program, (AstIdentNode*)e),
                        array_read(c, {NULL, owner, constant(c, i), true, i}), e);
            } else bind_pattern(c, a->left, owner, false);
            return owner;
        }
        if (a->op == OPERATOR_JS_AND_ASSIGN || a->op == OPERATOR_JS_OR_ASSIGN || a->op == OPERATOR_JS_NULLISH_ASSIGN)
            return logical(c, a->op, a->left, a->right, true);
        LmdReference ref = reference(c, a->left, false, a->op == OPERATOR_ASSIGN);
        LmdValue old = {};
        bool consume = a->op != OPERATOR_ASSIGN && numeric_operands(compound_operator(a->op),
            kind(c->program, a->left, c->inlining), kind(c->program, a->right, c->inlining),
            !c->program->coercion_callbacks && !c->program->bigints && !c->program->symbols &&
                immediate_scalar_operand(c, a->right, true));
        if (a->op != OPERATOR_ASSIGN) old = read_reference(c, ref, a->left, false, false, consume);
        LmdValue rhs = expression(c, a->right, false, consume);
        if (a->op != OPERATOR_ASSIGN) rhs = binary_value(c, compound_operator(a->op), old, rhs);
        write_reference(c, ref, rhs, n); return rhs;
    }
    case AST_NODE_UNARY: {
        AstUnaryNode* u = (AstUnaryNode*)n;
        if (u->op == OPERATOR_JS_DELETE) {
            if (u->operand->node_type == AST_NODE_IDENT) {
                AstIdentNode* id = (AstIdentNode*)u->operand;
                LmdBinding* b = identifier_binding(c->program, id);
                if (eval_reference_site(c->program, u->operand)) return eval_reference_operation(c,
                    eval_reference(c, id).eval_reference, JS_INTERP_REFERENCE_DELETE);
                if (b && b->with_lookup)
                    return with_reference_operation(c, with_reference(c, b, u->operand), u->operand, LMD_PROP_DELETE);
                if (b && !b->reflected_global) return {constant(c, 0), K_BOOL};
                if (c->program->test262_host && c->program->dynamic_source)
                    return boxed(c, global_binding_access(c, u->operand, false, 0, false, id->name, true), K_BOOL);
                MIR_reg_t owner = em_load_at(&c->em, module_slots(c), c->program->global_slot * sizeof(Item), MIR_T_I64, "delete_global");
                return boxed(c, property_access(c, LMD_PROP_DELETE, owner, relocated_constant(c, s2it(id->name))), K_BOOL);
            }
            if (u->operand->node_type != AST_NODE_MEMBER_EXPR && u->operand->node_type != AST_NODE_INDEX_EXPR) {
                expression(c, u->operand); return {constant(c, 1), K_BOOL};
            }
            AstFieldNode* field = (AstFieldNode*)u->operand;
            bool super = field->object->node_type == AST_NODE_IDENT && named(((AstIdentNode*)field->object)->name, "super");
            LmdReference ref = reference(c, u->operand, true, super);
            if (super) {
                // a super reference rejects Delete before ToPropertyKey or null-base validation.
                fail(c, LMD_MVP_REFERENCE, n); return {constant(c, 1), K_BOOL};
            }
            if (semantic(ref.owner) & (K_NULL | K_UNDEFINED)) {
                MIR_label_t valid = label(c);
                branch_truth(c, valid, nullish(c, ref.owner), false);
                fail(c, LMD_MVP_TYPE, n); put_label(c, valid);
            }
            LmdValue owner = ref.owner;
            if (semantic(owner) & (K_STRING | K_NUMBER | K_BOOL | K_BIGINT | K_SYMBOL)) {
                MIR_reg_t lookup = em_new_reg(&c->em, "delete_lookup_base", MIR_T_I64);
                move(c, lookup, reg(c, owner.reg));
                MIR_label_t primitive = label(c), ready = label(c);
                em_guard_container(&c->em, owner.reg, LMD_TYPE_RAW_POINTER, primitive);
                jump(c, ready); put_label(c, primitive);
                MIR_reg_t object = em_call_2(&c->em, "mvp_lmd_box_primitive", MIR_T_I64,
                    MIR_T_I64, reg(c, owner.reg), MIR_T_I64, integer(c, ITEM_JS_UNDEFINED), true);
                check_error(c, object); move(c, lookup, reg(c, object)); put_label(c, ready);
                // boxing changes primitive alternatives only; arrays/functions retain their carriers.
                owner = boxed(c, lookup, K_OBJECT | (semantic(owner) &
                    (K_ARRAY | K_FUNCTION | K_MAP | K_TYPED_ARRAY)));
            }
            MIR_reg_t deleted = property_access(c, LMD_PROP_DELETE, owner.reg, ref.name,
                0, NULL, false, NULL, semantic(owner) == K_OBJECT);
            check_error(c, deleted); return boxed(c, deleted, K_BOOL);
        }
        if (u->op == OPERATOR_JS_INCREMENT || u->op == OPERATOR_JS_DECREMENT) {
            LmdReference ref = reference(c, u->operand);
            bool bigint = c->program->bigints && (kind(c->program, u->operand, c->inlining) & K_BIGINT);
            LmdValue before = read_reference(c, ref, n, false, false, !bigint);
            if (bigint) {
                MIR_reg_t previous = em_new_reg(&c->em, "update_previous", MIR_T_I64);
                MIR_reg_t updated = em_new_reg(&c->em, "update_value", MIR_T_I64);
                MIR_label_t numeric = label(c), done = label(c);
                branch(c, MIR_BNE, numeric, reg(c, tag(c, before)), integer(c, LMD_TYPE_DECIMAL));
                move(c, previous, reg(c, box(c, before).reg));
                MIR_reg_t result = em_call_3(&c->em, "mvp_lmd_bigint_binary", MIR_T_I64,
                    MIR_T_I64, reg(c, previous), MIR_T_I64, integer(c, ITEM_JS_UNDEFINED),
                    MIR_T_I64, integer(c, u->op), true);
                check_error(c, result); move(c, updated, reg(c, result)); jump(c, done);
                put_label(c, numeric);
                LmdValue number_before = {to_number(c, before), K_NUMBER};
                move(c, previous, reg(c, box(c, number_before).reg));
                move(c, updated, reg(c, box(c, binary_value(c,
                    u->op == OPERATOR_JS_INCREMENT ? OPERATOR_ADD : OPERATOR_SUB, number_before, number(c, 1))).reg));
                put_label(c, done);
                LmdValue after = boxed(c, updated, kind(c->program, n, c->inlining));
                write_reference(c, ref, after, n);
                return u->prefix ? after : boxed(c, previous, semantic(after));
            }
            if (semantic(before) != K_NUMBER) before = {to_number(c, before), K_NUMBER};
            LmdValue after = binary_value(c, u->op == OPERATOR_JS_INCREMENT ? OPERATOR_ADD : OPERATOR_SUB,
                before, number(c, 1));
            write_reference(c, ref, after, n); return u->prefix ? after : before;
        }
        LmdValue v = {};
        if (u->op == OPERATOR_JS_TYPEOF && u->operand->node_type == AST_NODE_IDENT) {
            AstIdentNode* id = (AstIdentNode*)u->operand;
            // receiver/meta reads still evaluate their value and preserve initialization errors.
            bool receiver_value = named(id->name, "this") || named(id->name, "new.target") ||
                named(id->name, "import.meta");
            LmdBinding* binding = identifier_binding(c->program, id);
            if (eval_reference_site(c->program, u->operand))
                v = eval_reference_operation(c, eval_reference(c, id).eval_reference, JS_INTERP_REFERENCE_TYPEOF);
            else if (binding && binding->with_lookup)
                v = with_reference_operation(c, with_reference(c, binding, u->operand), u->operand, LMD_PROP_GET, {}, true);
            // reflected intrinsic names can be deleted or absent in a host profile;
            // typeof must allow that miss while lexical bindings retain their TDZ check.
            if (!v.reg && !receiver_value && (!binding || binding->reflected_intrinsic)) {
                if (!c->program->global_receiver) return text(c, "undefined");
                v = boxed(c, global_binding_access(c, u->operand, false, 0, true));
            }
        }
        if (!v.reg) v = expression(c, u->operand, false,
            !(kind(c->program, u->operand, c->inlining) &
                (K_BIGINT | K_OBJECT | K_ARRAY | K_MAP | K_TYPED_ARRAY | K_FUNCTION)) &&
            (u->op == OPERATOR_POS || u->op == OPERATOR_NEG || u->op == OPERATOR_JS_BIT_NOT));
        if (u->op == OPERATOR_NOT) return {op(c, MIR_XOR, reg(c, truth(c, v)), integer(c, 1)), K_BOOL};
        if (u->op == OPERATOR_JS_VOID) return boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
        if (u->op == OPERATOR_JS_TYPEOF) {
            MIR_reg_t tid = tag(c, v), result = em_new_reg(&c->em, "typeof", MIR_T_I64);
            MIR_label_t done = label(c);
            TypeId types[] = {LMD_TYPE_UNDEFINED, LMD_TYPE_BOOL, LMD_TYPE_FLOAT, LMD_TYPE_STRING, LMD_TYPE_FUNC, LMD_TYPE_DECIMAL, LMD_TYPE_SYMBOL};
            const char* names[] = {"undefined", "boolean", "number", "string", "function", "bigint", "symbol"};
            move(c, result, reg(c, text(c, "object").reg));
            if (c->program->property_callbacks) {
                MIR_label_t ordinary = label(c);
                branch(c, MIR_BNE, ordinary, reg(c, tid), integer(c, LMD_TYPE_MAP));
                MIR_reg_t callable = em_call_1(&c->em, "mvp_lmd_is_callable", MIR_T_I64,
                    MIR_T_I64, reg(c, box(c, v).reg), true);
                branch(c, MIR_BEQ, done, reg(c, callable), integer(c, 0));
                move(c, result, reg(c, text(c, "function").reg)); jump(c, done); put_label(c, ordinary);
            }
            for (int i = 0; i < (c->program->symbols ? 7 : c->program->bigints ? 6 : 5); i++) {
                MIR_label_t next = label(c);
                branch(c, MIR_BNE, next, reg(c, tid), integer(c, types[i]));
                move(c, result, reg(c, text(c, names[i]).reg)); jump(c, done); put_label(c, next);
            }
            put_label(c, done); return boxed(c, result, K_STRING);
        }
        v = primitive_operand(c, v, 1);
        if (c->program->bigints && (semantic(v) & K_BIGINT)) {
            MIR_reg_t result = em_new_reg(&c->em, "unary_numeric", MIR_T_I64);
            MIR_label_t numeric = label(c), done = label(c);
            branch(c, MIR_BNE, numeric, reg(c, tag(c, v)), integer(c, LMD_TYPE_DECIMAL));
            MIR_reg_t big = em_call_3(&c->em, "mvp_lmd_bigint_binary", MIR_T_I64,
                MIR_T_I64, reg(c, box(c, v).reg), MIR_T_I64, integer(c, ITEM_JS_UNDEFINED),
                MIR_T_I64, integer(c, u->op), true);
            check_error(c, big); move(c, result, reg(c, big)); jump(c, done);
            put_label(c, numeric);
            LmdValue value = {to_number(c, v), K_NUMBER};
            if (u->op == OPERATOR_NEG) {
                MIR_reg_t negative = em_new_reg(&c->em, "unary_negative", MIR_T_D);
                em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_DNEG, reg(c, negative), reg(c, value.reg)));
                value.reg = negative;
            } else if (u->op == OPERATOR_JS_BIT_NOT)
                value = {op(c, MIR_XOR, reg(c, int32(c, value)), integer(c, UINT64_MAX)), K_NUMBER, false, 0, true};
            move(c, result, reg(c, box(c, value).reg)); put_label(c, done);
            return boxed(c, result, kind(c->program, n, c->inlining));
        }
        if (u->op == OPERATOR_JS_BIT_NOT) {
            MIR_reg_t bits = op(c, MIR_XOR, reg(c, int32(c, v)), integer(c, UINT64_MAX));
            return {bits, K_NUMBER, false, 0, true};
        }
        if (u->op == OPERATOR_POS && semantic(v) == K_NUMBER) return v;
        if (u->op == OPERATOR_NEG && v.literal) return number(c, -v.number);
        if (u->op == OPERATOR_NEG && v.integer && !v.number_present && v.range.state == 1 &&
                (v.range.lower > 0 || v.range.upper < 0)) {
            MIR_reg_t negated = em_new_reg(&c->em, "negative_integer", MIR_T_I64);
            em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_NEG, reg(c, negated), reg(c, v.reg)));
            return {negated, K_NUMBER, false, 0, true, finite_range(-v.range.upper, -v.range.lower)};
        }
        MIR_reg_t d = to_number(c, v);
        if (u->op == OPERATOR_POS) return {d, K_NUMBER};
        MIR_reg_t result = em_new_reg(&c->em, "negate", MIR_T_D);
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_DNEG, reg(c, result), reg(c, d))); return {result, K_NUMBER};
    }
    case AST_NODE_BINARY: {
        AstBinaryNode* b = (AstBinaryNode*)n;
        if (b->op == OPERATOR_IN && b->left->node_type == AST_NODE_IDENT) {
            LmdBinding* name = identifier_binding(c->program, (AstIdentNode*)b->left);
            if (name && name->private_name) {
                MIR_reg_t key = binding_item(c, name);
                LmdValue owner = box(c, expression(c, b->right));
                MIR_reg_t result = em_call_4(&c->em, "mvp_lmd_private_element", MIR_T_I64,
                    MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, key), MIR_T_I64, integer(c, ITEM_JS_UNDEFINED),
                    MIR_T_I64, integer(c, LMD_PRIVATE_HAS), true);
                check_error(c, result); return boxed(c, result, K_BOOL);
            }
        }
        LmdValue divisible;
        if (remainder_zero(c, b, &divisible)) return divisible;
        LmdValue concatenated;
        if (b->op == OPERATOR_ADD && string_concat(c, b, &concatenated)) return concatenated;
        if (b->op == OPERATOR_AND || b->op == OPERATOR_OR || b->op == OPERATOR_JS_NULLISH_COALESCE)
            return logical(c, b->op, b->left, b->right);
        bool consume = numeric_operands(b->op, kind(c->program, b->left, c->inlining),
            kind(c->program, b->right, c->inlining),
            !c->program->coercion_callbacks && !c->program->bigints && !c->program->symbols &&
                immediate_scalar_operand(c, b->right, true));
        MirNumericOpPlan plan = {};
        bool integer_consumer = (em_numeric_op_plan(b->op, &plan) && plan.is_comparison) ||
            (b->op >= OPERATOR_JS_BIT_AND && b->op <= OPERATOR_JS_URSHIFT);
        if (consume && integer_consumer && range(c->program, b->left, true, true).state == 1 &&
                range(c->program, b->right, true, true).state == 1) consume = false;
        // numeric consumers finish before a safepoint; a left borrow also needs an inert RHS.
        bool right_borrow = consume && immediate_scalar_operand(c, b->right);
        LmdValue left = expression(c, b->left, right_borrow && immediate_scalar_operand(c, b->left), consume);
        LmdValue right = expression(c, b->right, right_borrow, consume);
        if (b->op == OPERATOR_JS_INSTANCEOF) {
            MIR_reg_t result = em_call_3(&c->em, c->program->property_callbacks ? "mvp_lmd_instanceof_dynamic" : "mvp_lmd_instanceof", MIR_T_I64,
                MIR_T_I64, reg(c, box(c, left).reg), MIR_T_I64, reg(c, box(c, right).reg),
                MIR_T_I64, integer(c, c->program->property_callbacks ? 0 : 2), true);
            check_error(c, result); return boxed(c, result, K_BOOL);
        }
        if (b->op == OPERATOR_IN) {
            LmdValue name = canonical_key(c, left);
            MIR_reg_t has = property_access(c, LMD_PROP_HAS, box(c, right).reg, name.reg);
            check_error(c, has); return boxed(c, has, K_BOOL);
        }
        return binary_value(c, b->op, left, right);
    }
    case AST_NODE_CONDITIONAL_EXPR: {
        AstIfNode* b = (AstIfNode*)n;
        bool numeric = kind(c->program, n, c->inlining) == K_NUMBER;
        MIR_reg_t result = em_new_reg(&c->em, "conditional", numeric ? MIR_T_D : MIR_T_I64);
        MIR_label_t alternate = label(c), done = label(c);
        branch_condition(c, b->test, alternate, false);
        LmdValue yes = expression(c, b->then);
        move(c, result, reg(c, numeric ? to_number(c, yes) : box(c, yes).reg), numeric); jump(c, done);
        put_label(c, alternate); LmdValue no = expression(c, b->otherwise);
        move(c, result, reg(c, numeric ? to_number(c, no) : box(c, no).reg), numeric);
        put_label(c, done); return numeric ? LmdValue{result, K_NUMBER} : boxed(c, result);
    }
    case AST_NODE_SEQ: {
        LmdValue v = {};
        for (AstNode* a = ((AstArrayNode*)n)->item; a; a = a->next) v = expression(c, a);
        return v;
    }
    case AST_NODE_NEW_EXPR: {
        AstCallNode* call = (AstCallNode*)n;
        LmdBinding* b = call->callee->node_type == AST_NODE_IDENT
            ? identifier_binding(c->program, (AstIdentNode*)call->callee) : NULL;
        if (b && b->intrinsic == I_ARRAY) return array_construct(c, call);
        if (b && b->intrinsic == I_OBJECT) return object_construct(c, call);
        LmdObjectPlan* plan = object_plan(c->program, n);
        LmdScalarFactoryPlan* factory = c->inlining ? scalar_factory_plan(c->inlining->scalar_return) : NULL;
        if (factory && factory->value == n) plan = &factory->object;
        if (plan && plan->constructor && plan->scalar_binding) {
            // preserve constructor TDZ and all argument effects before installing the virtual fields.
            expression(c, call->callee);
            LmdValue* values = call_arguments(c, call, false);
            LmdInlineFrame frame = {}; frame.parent = c->inlining; frame.function = plan->constructor;
            frame.scalar_receiver = plan;
            LmdInlineBody body = {NULL, ((AstBlockNode*)plan->constructor->ast->body)->statements, NULL, false};
            bool guarded = false; MIR_label_t done = label(c);
            inline_call_body(c, frame, body, call->arguments, values,
                ast_linked_node_count(call->arguments), done, &guarded);
            put_label(c, done); mem_free(values);
            return boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
        }
        bool native_typed = b && !c->program->typed_storage &&
            b->intrinsic >= I_INT32_ARRAY && b->intrinsic <= I_FLOAT64_ARRAY;
        bool native_map = b && b->intrinsic == I_MAP && !c->program->iterators;
        // the public dispatcher also owns nonconstructible globals and their
        // argument effects; only the admitted native storage recipes bypass it.
        if (c->program->receiver_abi && !native_typed && !native_map) {
            LmdValue constructor = box(c, expression(c, call->callee));
            LmdFunction* ordinary = b ? b->target : NULL;
            LmdObjectPlan* layout = ordinary ? object_plan(c->program, (AstNode*)ordinary->ast, true) : NULL;
            if (layout && !call->interp_has_spread_args && !compact_property_dispatch(c)) {
                LmdValue* values = call_arguments(c, call, true);
                int count = ast_linked_node_count(call->arguments);
                MIR_label_t slow = label(c), done = label(c);
                MIR_reg_t joined = em_new_reg(&c->em, "source_constructed", MIR_T_I64);
                em_guard_container(&c->em, constructor.reg, LMD_TYPE_FUNC, slow);
                branch(c, MIR_BNE, slow, reg(c, em_load_at(&c->em, constructor.reg,
                    offsetof(Function, entry_abi), MIR_T_U8, "constructor_abi")), integer(c, FN_ENTRY_ABI_MVP_LMD));
                branch(c, MIR_BNE, slow, reg(c, em_load_at(&c->em, constructor.reg,
                    offsetof(Function, ptr), MIR_T_I64, "constructor_entry")), MIR_new_ref_op(c->em.ctx, ordinary->forward));
                branch(c, MIR_BNE, slow, reg(c, em_load_at(&c->em, constructor.reg,
                    offsetof(Function, runtime_context), MIR_T_I64, "constructor_context")), reg(c, c->em.frame.runtime));
                MvpLmdCallable marker = {};
                branch(c, MIR_BNE, slow, reg(c, em_load_at(&c->em, constructor.reg,
                    (char*)&marker.program - (char*)&marker, MIR_T_I64, "constructor_program")), reg(c, c->unit));
                MIR_reg_t instance = em_call_3(&c->em, "mvp_lmd_object_from_constructor", MIR_T_I64,
                    MIR_T_I64, reg(c, constructor.reg), MIR_T_P, integer(c, 0), MIR_T_I64, integer(c, 1), true);
                check_error(c, instance);
                branch(c, MIR_BEQ, slow, reg(c, instance), integer(c, ITEM_JS_UNDEFINED));
                boxed(c, instance, K_OBJECT);
                int index = 0;
                FOR_EACH_MAP_FIELD(layout->blueprint, field) {
                    AstNode* seed = layout->initializers[index++];
                    LmdValue value = boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
                    if (seed->node_type == AST_NODE_LITERAL) value = box(c, expression(c, seed));
                    else {
                        int ai = 0;
                        for (AstNode* formal = ordinary->ast->params; formal; formal = formal->next, ai++)
                            if (((AstIdentNode*)seed)->entry == js_ast_parameter_binding_identifier(formal)->entry && ai < count)
                                value = values[ai];
                    }
                    MIR_reg_t data = em_load_at(&c->em, instance, LAMBDA_GC_OFF_MAP_DATA, MIR_T_I64, "constructor_data");
                    // admitted initializers cannot observe the receiver or call guest code between stores.
                    em_call_3(&c->em, "map_field_store", MIR_T_I64, MIR_T_P,
                        reg(c, op(c, MIR_ADD, reg(c, data), integer(c, field->byte_offset))),
                        MIR_T_I64, reg(c, value.reg), MIR_T_I64, integer(c, LMD_TYPE_ANY), true);
                }
                move(c, joined, reg(c, instance)); jump(c, done); put_label(c, slow);
                LmdCallCapture capture = {constructor, values}; capture.new_target = constructor;
                move(c, joined, reg(c, ::call(c, call, &capture).reg)); put_label(c, done);
                return boxed(c, joined, K_OBJECT);
            }
            MvpLmdClass* cls = class_plan(c->program, NULL, b);
            LmdFunction* initializer = cls && cls->reusable_layout
                ? (LmdFunction*)c->program->functions->data[cls->constructor_id] : NULL;
            LmdInlineBody body = {};
            // expanded constructor proofs reuse class metadata without creating new per-site caches (D8.4.1v2).
            if (initializer && constructor_layout_reusable(c->program, initializer->ast->body) &&
                    inline_body(c, initializer, &body, true, true)) {
                LmdValue* values = call_arguments(c, call, false);
                int count = ast_linked_node_count(call->arguments);
                LmdObjectPlan* native_plan = c->inlining && c->inlining->region && count
                    ? scalar_constructor_plan(c->program, n, values) : NULL;
                if (native_plan) {
                    arraylist_append(c->program->objects, native_plan);
                    MIR_label_t slow = label(c), done = label(c);
                    MIR_reg_t joined = em_new_reg(&c->em, "native_constructed", MIR_T_I64);
                    MIR_reg_t class_values = em_load_at(&c->em, relocated_constant(c, (uint64_t)&cls->values),
                        0, MIR_T_I64, "class_values");
                    branch(c, MIR_BNE, slow, reg(c, constructor.reg), reg(c, em_load_at(&c->em,
                        class_values, 0, MIR_T_I64, "class_constructor")));
                    MIR_reg_t instance = em_call_2(&c->em, "mvp_lmd_object_new", MIR_T_I64,
                        MIR_T_P, reg(c, planned_shape(c, native_plan)), MIR_T_I64, integer(c, 0), true);
                    check_error(c, instance); LmdValue owner = boxed(c, instance, K_OBJECT);
                    int field_index = 0;
                    FOR_EACH_MAP_FIELD(native_plan->blueprint, field) {
                        AstNode* seed = native_plan->initializers[field_index++];
                        LmdValue value = {};
                        int ai = 0;
                        for (AstNode* argument = call->arguments; argument; argument = argument->next, ai++)
                            if (argument == seed) { value = values[ai]; break; }
                        if (!value.reg) value = expression(c, seed);
                        LmdReference ref = {}; ref.owner = owner; ref.plan = native_plan; ref.field = field;
                        // forwarding constructors cannot observe the fresh receiver before these stores.
                        if (!field_write(c, ref, value, slow, true))
                            diagnostic(c->program, n, "native constructor field mismatch");
                    }
                    move(c, joined, reg(c, instance)); jump(c, done); put_label(c, slow);
                    for (int i = 0; i < count; i++) values[i] = box(c, values[i]);
                    LmdCallCapture capture = {constructor, values}; capture.new_target = constructor;
                    move(c, joined, reg(c, ::call(c, call, &capture).reg)); put_label(c, done);
                    return boxed(c, joined, K_OBJECT);
                }
                MIR_label_t slow = label(c), done = label(c), unchanged = label(c);
                MIR_reg_t joined = em_new_reg(&c->em, "constructed", MIR_T_I64);
                MIR_reg_t shape_slot = relocated_constant(c, (uint64_t)&cls->allocation_shape);
                MIR_reg_t shape = em_load_at(&c->em, shape_slot, 0, MIR_T_I64, "allocation_shape");
                // the first ordinary construction supplies a completed, shared layout.
                branch(c, MIR_BEQ, slow, reg(c, shape), integer(c, 0));
                // program slots are installed at execution entry, after MIR generation.
                MIR_reg_t class_values = em_load_at(&c->em, relocated_constant(c, (uint64_t)&cls->values),
                    0, MIR_T_I64, "class_values");
                branch(c, MIR_BNE, slow, reg(c, constructor.reg), reg(c, em_load_at(&c->em,
                    class_values, 0, MIR_T_I64, "class_constructor")));
                MIR_reg_t instance = em_call_2(&c->em, "mvp_lmd_object_new", MIR_T_I64,
                    MIR_T_P, reg(c, shape), MIR_T_I64, integer(c, 0), true);
                check_error(c, instance); boxed(c, instance, K_OBJECT);
                LmdInlineFrame frame = {}; frame.parent = c->inlining; frame.function = initializer;
                frame.self = constructor.reg; frame.receiver = instance;
                bool guarded = false;
                inline_call_body(c, frame, body, call->arguments, values, count, slow, &guarded);
                MIR_reg_t completed = em_load_at(&c->em, instance, LAMBDA_GC_OFF_MAP_TYPE, MIR_T_I64, "completed_shape");
                MIR_reg_t shared = op(c, MIR_OR,
                    reg(c, em_load_at(&c->em, completed, (char*)&cls->shape.is_shared_constructor_shape - (char*)&cls->shape,
                        MIR_T_U8, "constructor_shape")),
                    reg(c, em_load_at(&c->em, completed, (char*)&cls->shape.is_transition_shared_shape - (char*)&cls->shape,
                        MIR_T_U8, "transition_shape")));
                branch_truth(c, unchanged, shared, false);
                em_store_at(&c->em, shape_slot, 0, MIR_T_I64, completed);
                put_label(c, unchanged); move(c, joined, reg(c, instance)); jump(c, done);
                put_label(c, slow);
                for (int i = 0; i < count; i++) values[i] = box(c, values[i]);
                LmdCallCapture capture = {constructor, values}; capture.new_target = constructor;
                move(c, joined, reg(c, ::call(c, call, &capture).reg)); put_label(c, done);
                return boxed(c, joined, K_OBJECT);
            }
            LmdCallCapture capture = {constructor, call->interp_has_spread_args ? NULL : call_arguments(c, call, true)};
            capture.new_target = constructor;
            return ::call(c, call, &capture);
        }
        LmdValue* args = call_arguments(c, call, false);
        if (b && b->intrinsic >= I_INT32_ARRAY && b->intrinsic <= I_FLOAT64_ARRAY) {
            // ToIndex truncates fractions and maps NaN/undefined to zero before checking range.
            MIR_reg_t requested = to_number(c, call->arguments ? args[0] : number(c, 0));
            mem_free(args);
            MIR_reg_t d = em_call_1(&c->em, "trunc", MIR_T_D, MIR_T_D, reg(c, requested), true);
            MIR_reg_t length = constant(c, 0);
            MIR_label_t allocate = label(c), invalid = label(c), memory = label(c), done = label(c);
            branch_truth(c, allocate, op(c, MIR_DEQ, reg(c, d), reg(c, d)), false);
            move(c, length, reg(c, integer_lane(c, d, 0, INT53_MAX, invalid, false)));
            put_label(c, allocate);
            MIR_reg_t array = em_call_2(&c->em, "array_num_new", MIR_T_P, MIR_T_I64,
                integer(c, typed_lanes[b->intrinsic - I_INT32_ARRAY]), MIR_T_I64, reg(c, length), true);
            LmdValue owner = boxed(c, array, K_TYPED_ARRAY);
            branch_truth(c, memory, array, false);
            // shared allocation keeps a zero length header on buffer allocation failure.
            branch(c, MIR_BNE, memory, reg(c, length), reg(c, em_load_at(&c->em, array,
                LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "allocated_length")));
            jump(c, done);
            put_label(c, invalid); fail(c, LMD_MVP_RANGE, n);
            put_label(c, memory); fail(c, LMD_MVP_MEMORY, n);
            put_label(c, done); return owner;
        }
        mem_free(args);
        if (!b || b->intrinsic != 4 || call->arguments) { fail(c, LMD_MVP_CAPABILITY, n); return boxed(c, constant(c, ITEM_JS_UNDEFINED)); }
        MIR_reg_t map = em_call_2(&c->em, "mvp_lmd_object_new", MIR_T_I64,
            MIR_T_P, relocated(c, (uint64_t)c->program->roots[1]), MIR_T_I64, integer(c, 1), true);
        check_error(c, map); return boxed(c, map, K_MAP);
    }
    case AST_NODE_MAP: case AST_NODE_OBJECT_LITERAL: {
        LmdObjectPlan* plan = object_plan(c->program, n);
        if (plan && plan->scalar_binding) {
            int index = 0;
            for (AstNode* e = ((AstMapNode*)n)->properties; e; e = e->next) {
                LmdValue value = expression(c, ((AstPropertyNode*)e)->value);
                // each initializer still runs once, in order; retain borrowed scalar storage precisely.
                scalar_field_write(c, &plan->scalar_fields[index++], value);
            }
            return boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
        }
        // a fixed-key fresh literal cannot escape through its initializers; root child snapshots first.
        int count = plan ? plan->blueprint->length : 0, index = 0;
        LmdValue* values = count ? (LmdValue*)mem_calloc(count, sizeof(LmdValue), MEM_CAT_TEMP) : NULL;
        if (values) for (AstNode* e = ((AstMapNode*)n)->properties; e; e = e->next)
            values[index++] = expression(c, ((AstPropertyNode*)e)->value);
        MIR_reg_t shape = plan ? planned_shape(c, plan) : relocated_constant(c, (uint64_t)c->program->roots[0]);
        MIR_reg_t object = em_call_2(&c->em, "mvp_lmd_object_new", MIR_T_I64,
            MIR_T_P, reg(c, shape), MIR_T_I64, integer(c, 0), true);
        check_error(c, object); LmdValue owner = boxed(c, object, K_OBJECT);
        index = 0;
        bool fresh = true;
        for (AstNode* e = ((AstMapNode*)n)->properties; e; e = e->next) {
            if (e->node_type == AST_NODE_SPREAD) {
                LmdValue source = box(c, expression(c, ((AstSpreadNode*)e)->argument));
                MIR_reg_t copied = em_call_4(&c->em, "mvp_lmd_object_copy", MIR_T_I64,
                    MIR_T_I64, reg(c, object), MIR_T_I64, reg(c, source.reg), MIR_T_I64, integer(c, 0),
                    MIR_T_I64, integer(c, ITEM_NULL), true);
                check_error(c, copied); fresh = false; continue;
            }
            AstPropertyNode* prop = (AstPropertyNode*)e;
            String* spelling = !prop->computed && prop->key->node_type == AST_NODE_IDENT
                ? name_pool_create_string(c->program->frontend->name_pool, ((AstIdentNode*)prop->key)->name) : NULL;
            LmdValue name = spelling ? boxed(c, relocated_constant(c, s2it(spelling)), K_STRING)
                : canonical_key(c, expression(c, prop->key));
            bool proto_literal = js_ast_property_is_proto_setter(prop);
            bool anonymous_class = prop->value && prop->value->node_type == AST_NODE_CLASS_EXPR &&
                !((AstClassNode*)prop->value)->name;
            LmdValue value = values ? values[index++] : anonymous_class
                ? class_expression(c, (AstClassNode*)prop->value, proto_literal ? 0 : name.reg) : expression(c, prop->value);
            LmdFunction* method = prop->method || prop->is_getter || prop->is_setter ? function(c->program, prop->value) : NULL;
            if (method && method->object_home) {
                int slot = function_capture_count(method);
                MIR_reg_t env = em_load_at(&c->em, box(c, value).reg, offsetof(Function, closure_env), MIR_T_I64, "method_environment");
                // bind after literal allocation; repeated evaluations retain distinct live home objects.
                em_store_at(&c->em, env, slot * sizeof(Item), MIR_T_I64, object);
            }
            if (proto_literal) {
                LmdValue parent = box(c, value);
                MIR_label_t ignored = label(c), accepted = label(c);
                MIR_reg_t kind = tag(c, parent);
                for (int type : {LMD_TYPE_MAP, LMD_TYPE_ARRAY, LMD_TYPE_FUNC, LMD_TYPE_NULL})
                    branch(c, MIR_BEQ, accepted, reg(c, kind), integer(c, type));
                jump(c, ignored); put_label(c, accepted);
                MIR_reg_t set = em_call_2(&c->em, "mvp_lmd_set_prototype", MIR_T_I64,
                    MIR_T_I64, reg(c, object), MIR_T_I64, reg(c, parent.reg), true);
                check_error(c, set); put_label(c, ignored); fresh = false; continue;
            }
            bool anonymous_function = prop->value &&
                (prop->value->node_type == AST_NODE_FUNC_EXPR || prop->value->node_type == AST_NODE_ARROW_FUNC) &&
                !((AstFuncNode*)prop->value)->name;
            if ((prop->method || prop->is_getter || prop->is_setter || anonymous_function) &&
                    (prop->computed || !property_spelling(prop->key, false)))
                check_error(c, em_call_3(&c->em, "mvp_lmd_function_set_name", MIR_T_I64,
                    MIR_T_I64, reg(c, box(c, value).reg), MIR_T_I64, reg(c, name.reg),
                    MIR_T_I64, integer(c, prop->is_getter ? 1 : prop->is_setter ? 2 : 0), true));
            if (prop->is_getter || prop->is_setter) {
                MIR_reg_t defined = em_call_4(&c->em, "mvp_lmd_define_accessor", MIR_T_I64,
                    MIR_T_I64, reg(c, object), MIR_T_I64, reg(c, name.reg),
                    MIR_T_I64, reg(c, box(c, value).reg), MIR_T_I64, integer(c, prop->is_setter ? 1 : 0), true);
                check_error(c, defined); fresh = false; continue;
            }
            MIR_label_t miss = label(c), done = label(c);
            ShapeEntry* field = plan && spelling ? typemap_hash_lookup(plan->blueprint, spelling->chars, spelling->len) : NULL;
            // all initializers ran before allocation; only a prior fallback can have reshaped this owner.
            bool preserved = false;
            bool written = field && field_write(c, {NULL, owner, 0, true, 0, name.reg, plan, field}, value, miss,
                fresh, &preserved);
            // a proven fresh store cannot reach the fallback; omitting it bounds literal SSA/root traffic.
            if (written && fresh && preserved) continue;
            if (written) jump(c, done);
            fresh &= preserved;
            put_label(c, miss);
            MIR_reg_t stored = property_access(c, LMD_PROP_CREATE_DATA, owner.reg, name.reg,
                box(c, value).reg, NULL, false, NULL, true);
            check_error(c, stored); put_label(c, done);
        }
        mem_free(values); return owner;
    }
    case AST_NODE_ARRAY: {
        AstArrayNode* a = (AstArrayNode*)n;
        MIR_reg_t result = em_call_0(&c->em, "array", MIR_T_P, false);
        root_value(c, result, JIT_VALUE_RAW_GC_POINTER);
        MIR_reg_t reserved = em_call_2(&c->em, "array_reserve_append_slots", MIR_T_I64,
            MIR_T_P, reg(c, result), MIR_T_I64, integer(c, (uint64_t)a->length * 2), true);
        MIR_label_t ok = label(c); branch_truth(c, ok, reserved); fail(c, LMD_MVP_MEMORY, n); put_label(c, ok);
        bool passive = a->length >= MVP_LMD_COMPACT_REGISTER_BUDGET;
        for (AstNode* e = a->item; passive && e; e = e->next) {
            if (e->node_type == AST_NODE_NULL) continue;
            if (e->node_type != AST_NODE_LITERAL) { passive = false; break; }
            AstLiteralNode* literal = (AstLiteralNode*)e;
            passive = !literal->is_bigint && literal->literal_type >= AST_LITERAL_NUMBER &&
                literal->literal_type <= AST_LITERAL_UNDEFINED;
        }
        if (passive) {
            // immutable frontend values have no evaluation effects; Lambda copies wide scalars into the fresh owner.
            Item* items = (Item*)pool_alloc(c->program->frontend->pool, (size_t)a->length * sizeof(Item));
            if (!items) { diagnostic(c->program, n, "literal table allocation failed"); return boxed(c, result, K_ARRAY); }
            uint32_t index = 0;
            for (AstNode* e = a->item; e; e = e->next, index++) {
                if (e->node_type == AST_NODE_NULL) { items[index].item = ITEM_JS_DELETED_SENTINEL; continue; }
                AstLiteralNode* literal = (AstLiteralNode*)e;
                switch (literal->literal_type) {
                case AST_LITERAL_NUMBER:
                    // retain the same finite-int encoding as ordinary boxed literal initialization.
                    items[index] = finite_integer(literal->value.number_value)
                        ? Item{.item = i2it((int64_t)literal->value.number_value)}
                        : lambda_float_ptr_to_item(&literal->value.number_value);
                    break;
                case AST_LITERAL_STRING: items[index].item = s2it(literal->value.string_value); break;
                case AST_LITERAL_BOOLEAN: items[index].item = b2it(literal->value.boolean_value); break;
                case AST_LITERAL_NULL: items[index].item = ITEM_NULL; break;
                default: items[index].item = ITEM_JS_UNDEFINED; break;
                }
            }
            em_call_void_4(&c->em, "array_copy_owned_items", MIR_T_P, reg(c, result),
                MIR_T_I64, integer(c, 0), MIR_T_P, relocated(c, (uint64_t)items),
                MIR_T_I64, integer(c, a->length), true);
            em_store_at(&c->em, result, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, constant(c, a->length));
            return boxed(c, result, K_ARRAY);
        }
        uint32_t i = 0; bool spread = false;
        for (AstNode* e = a->item; e; e = e->next, i++) {
            if (e->node_type == AST_NODE_SPREAD) {
                MIR_reg_t completed;
                if (c->program->iterators) {
                    LmdValue source = box(c, expression(c, ((AstSpreadNode*)e)->argument));
                    completed = em_call_5(&c->em, "mvp_lmd_iterator_collect", MIR_T_I64,
                        MIR_T_P, reg(c, c->unit), MIR_T_I64, reg(c, result), MIR_T_I64, reg(c, source.reg),
                        MIR_T_I64, integer(c, ITEM_JS_UNDEFINED), MIR_T_I64, integer(c, ITEM_JS_UNDEFINED), true);
                } else {
                    int projection; MIR_reg_t selected;
                    LmdValue source = iterable_source(c, ((AstSpreadNode*)e)->argument, projection, selected);
                    completed = em_call_3(&c->em, "mvp_lmd_array_spread", MIR_T_I64,
                        MIR_T_I64, reg(c, result), MIR_T_I64, reg(c, source.reg),
                        MIR_T_I64, selected ? reg(c, selected) : integer(c, projection), true);
                }
                check_error(c, completed); spread = true;
            } else {
                // Elisions occupy an absent slot, using the existing hole representation.
                LmdValue value = e->node_type == AST_NODE_NULL ? boxed(c, constant(c, ITEM_JS_DELETED_SENTINEL)) : expression(c, e);
                MIR_reg_t index = spread ? em_load_at(&c->em, result, LAMBDA_GC_OFF_LIST_LENGTH,
                    MIR_T_I64, "spread_length") : constant(c, i);
                // fresh literal slots use CreateDataProperty, so inherited setters cannot observe them.
                array_index_write(c, {NULL, boxed(c, result, K_ARRAY), index, !spread, i}, value, true, a->length);
            }
        }
        return boxed(c, result, K_ARRAY);
    }
    default: diagnostic(c->program, n, "expression outside MVP scope");
        return boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
    }
}

static void initialize_capture(LmdCompiler* c, LmdBinding* b, bool copy) {
    LmdValue previous = copy ? stable_item(c, binding_item(c, b)) : boxed(c, constant(c, ITEM_JS_TDZ));
    MIR_reg_t cell = em_call_1(&c->em, "mvp_lmd_array_new", MIR_T_I64, MIR_T_I64, integer(c, 1), true);
    check_error(c, cell); move(c, b->reg, reg(c, cell));
    if (!b->owner->id && b->entry->scope == ((AstScript*)c->program->frontend->ast_root)->global_vars)
        em_store_at(&c->em, module_slots(c), b->slot * sizeof(Item), MIR_T_I64, cell);
    write_binding(c, b, previous, b->entry->node, true);
}
static void eval_scope_enter(LmdCompiler* c, NameScope* scope, bool clone = false,
        int role = JS_INTERP_ENV_ORDINARY, bool private_record = false) {
    if (!c->function->eval_environment || (!scope && !private_record && role != JS_INTERP_ENV_WITH_OBJECT) ||
            (!clone && !private_record && role == JS_INTERP_ENV_ORDINARY && scope == c->eval_scope)) return;
    MIR_reg_t outer = clone ? em_load_at(&c->em, c->eval_env, offsetof(JsInterpEnv, outer), MIR_T_I64, "eval_outer")
        : c->eval_env ? c->eval_env : constant(c, 0);
    if (!c->function->id && scope == ((AstScript*)c->program->frontend->ast_root)->global_vars)
        role = c->program->source_module ? JS_INTERP_ENV_MODULE : JS_INTERP_ENV_GLOBAL_OBJECT;
    bool activation = c->function->ast && scope == c->function->ast->vars && role != JS_INTERP_ENV_BODY_VARS;
    int flags = JS_INTERP_CELL_ALL;
    if (activation && c->function->arguments_binding) flags |= JS_INTERP_CELL_ARGUMENTS;
    if (activation && c->function->ast->node_type != AST_NODE_ARROW_FUNC) flags |= JS_INTERP_CELL_THIS;
    MIR_reg_t env = em_call_4(&c->em, "mvp_lmd_dynamic_environment", MIR_T_P,
        MIR_T_P, reg(c, c->unit), MIR_T_P, relocated(c, (uint64_t)scope),
        MIR_T_P, reg(c, outer), MIR_T_I64, integer(c, flags | (role << 8)), true);
    root_value(c, env, JIT_VALUE_RAW_GC_POINTER);
    MIR_label_t allocated = label(c); branch_truth(c, allocated, env);
    fail(c, LMD_MVP_MEMORY, NULL); put_label(c, allocated);
    if (role == JS_INTERP_ENV_MODULE) {
        MIR_reg_t module = em_load_at(&c->em, c->unit, offsetof(MvpLmdProgram, source_module), MIR_T_I64, "eval_source_module");
        em_store_at(&c->em, env, offsetof(JsInterpEnv, source_module), MIR_T_I64, module);
    }
    for (NameEntry* entry = scope ? scope->first : NULL; entry; entry = entry->next) {
        if ((role == JS_INTERP_ENV_PARAMETERS && !entry->is_parameter) ||
                (role == JS_INTERP_ENV_BODY_VARS && !entry->is_js_body_var)) continue;
        LmdBinding* b = binding(c->program, entry);
        if (!b || !b->captured || b->reflected_global || b->intrinsic || b->module_cell) continue;
        em_store_at(&c->em, env, offsetof(JsInterpEnv, slots) + entry->slot * sizeof(Item),
            MIR_T_I64, binding_cell(c, b));
    }
    if (activation) {
        em_store_at(&c->em, env, offsetof(JsInterpEnv, function_node), MIR_T_I64, relocated_constant(c, (uint64_t)c->function->ast));
        if (flags & JS_INTERP_CELL_THIS) {
            em_store_at(&c->em, env, offsetof(JsInterpEnv, has_lexical_this), MIR_T_U8, constant(c, 1));
            em_store_at(&c->em, env, offsetof(JsInterpEnv, lexical_this), MIR_T_I64, c->receiver_cell);
        }
        if (flags & JS_INTERP_CELL_ARGUMENTS)
            em_store_at(&c->em, env, offsetof(JsInterpEnv, arguments_object), MIR_T_I64,
                binding_cell(c, c->function->arguments_binding));
        if (c->function->home && c->function->id == c->function->home->constructor_id)
            em_store_at(&c->em, env, offsetof(JsInterpEnv, private_home_class), MIR_T_I64, c->self);
    }
    if (clone) move(c, c->eval_env, reg(c, env));
    else { c->eval_env = env; c->eval_scope = scope; }
}
static void clone_loop_bindings(LmdCompiler* c, NameScope* scope) {
    for (NameEntry* e = scope ? scope->first : NULL; e; e = e->next) {
        LmdBinding* b = binding(c->program, e);
        if (b && b->captured && e->is_lexical && !e->is_const) initialize_capture(c, b, true);
    }
    if (c->function->eval_environment && scope && scope == c->eval_scope) eval_scope_enter(c, scope, true);
}
static void scope_capture_initialize(LmdCompiler* c, NameScope* scope, bool body_variables = false) {
    // install all cells before creating hoisted closures that can reference later declarations.
    for (NameEntry* e = scope ? scope->first : NULL; e; e = e->next) {
        LmdBinding* b = binding(c->program, e);
        // a body var gets its own cell; default closures keep the original parameter cell.
        bool copy_parameter = !c->function->eval_environment && c->function->parameter_expressions &&
            c->function->ast && scope == c->function->ast->vars && e->is_parameter && e->is_js_body_var;
        bool allocate = (body_variables ? e->is_js_body_var : !e->is_parameter) || copy_parameter;
        if (b && b->owner == c->function && b != c->function->arguments_binding && b->captured && allocate)
            initialize_capture(c, b, copy_parameter);
    }
}
static void scope_initialize(LmdCompiler* c, NameScope* scope, bool captures = true) {
    if (captures) { scope_capture_initialize(c, scope); eval_scope_enter(c, scope); }
    bool projected_body = c->function->eval_environment && c->function->parameter_expressions &&
        c->function->ast && scope == c->function->ast->vars;
    for (NameEntry* e = scope ? scope->first : NULL; e; e = e->next) {
        LmdBinding* b = binding(c->program, e);
        bool hoisted = b && b->target && e->node && e->node->node_type == AST_NODE_FUNC &&
            b->target->ast->entry == e && !e->is_annex_b_companion;
        if (!b || (b->owner != c->function && !inline_slot(c, b)) ||
                (projected_body ? !e->is_js_body_var : e->is_parameter && !hoisted) || b->native || b->intrinsic) continue;
        if (b == c->function->arguments_binding || b->module_import || b->ast_capture) continue;
        if (!b->reflected_global && b->shape_hint && b->shape_hint->scalar_binding == b) continue;
        LmdScalarField* local = inline_slot(c, b);
        if (local && !is_boxed(local->value)) continue;
        if (e->is_function_name_binding) {
            write_binding(c, b, boxed(c, c->self, K_FUNCTION), e->node, true);
        } else if (hoisted) {
            if (b->observed || b->assigned || b->target->captures || b->target->lexical_receiver || b->target->ast->is_generator || b->target->ast->is_async)
                write_binding(c, b, new_function(c, b->target), e->node, true);
        } else if (!projected_body && b == c->function->body_arguments_binding) {
            write_binding(c, b, boxed(c, binding_item(c, c->function->arguments_binding), K_OBJECT), e->node, true);
        } else if (!projected_body && !b->reflected_global)
            write_binding(c, b, boxed(c, constant(c, e->is_lexical ? ITEM_JS_TDZ : ITEM_JS_UNDEFINED)), e->node, true);
    }
}
extern "C" Item mvp_lmd_private_names_initialize(MvpLmdProgram* program,
        const LmdPrivateNameInit* names, int64_t count) {
    // the registered module slab owns every result; spellings belong to the retained frontend pool.
    for (int64_t i = 0; i < count; i++) {
        Item name = mvp_lmd_private_element(ItemNull, Item{.item = s2it(names[i].spelling)},
            Item{.item = ITEM_JS_UNDEFINED}, LMD_PRIVATE_NAME);
        if (item_is_error(name)) return name;
        owned_item_slot_store(program->slots, program->slot_count, names[i].slot, name);
    }
    return ItemNull;
}
static LmdValue class_expression(LmdCompiler* c, AstClassNode* ast, MIR_reg_t inferred_name) {
    MIR_reg_t saved_eval = c->eval_env; NameScope* saved_scope = c->eval_scope;
    MvpLmdClass* cls = class_plan(c->program, ast);
    bool saved_strict = c->strict;
    if (cls->eval_environment) c->strict = true;
    AstNode* site = (AstNode*)ast;
    LmdBinding* inner = binding(c->program, ast->entry);
    // eval exposure can capture the inner name even for a statically planned class.
    if (cls->dynamic || (inner && inner->captured))
        scope_initialize(c, ((JsClassNode*)ast)->expression_scope);
    else if (inner) write_binding(c, inner, boxed(c, constant(c, ITEM_JS_TDZ)), site, true);
    LmdValue base = box(c, expression(c, ast->superclass));
    auto passive_private = [site](LmdBinding* binding) {
        return binding->private_name && !binding->captured && !binding->owner->id && binding->entry->node == site;
    };
    int passive_count = 0;
    if (cls->has_private_names && !c->function->id && !c->inlining)
        for (int i = 0; i < c->program->bindings->length; i++) {
            LmdBinding* binding = (LmdBinding*)c->program->bindings->data[i];
            if (passive_private(binding))
                passive_count++;
        }
    bool bulk_private = passive_count >= MVP_LMD_COMPACT_REGISTER_BUDGET;
    if (bulk_private) {
        auto* names = (LmdPrivateNameInit*)pool_calloc(c->program->frontend->pool,
            passive_count * sizeof(LmdPrivateNameInit));
        if (!names) { diagnostic(c->program, site, "private name initialization plan"); return expression(c, NULL); }
        int index = 0;
        for (int i = 0; i < c->program->bindings->length; i++) {
            LmdBinding* binding = (LmdBinding*)c->program->bindings->data[i];
            if (passive_private(binding))
                names[index++] = {binding->entry->name, binding->slot};
        }
        check_error(c, em_call_3(&c->em, "mvp_lmd_private_names_initialize", MIR_T_I64,
            MIR_T_P, reg(c, c->unit), MIR_T_P, relocated(c, (uint64_t)names),
            MIR_T_I64, integer(c, passive_count), true));
        if (!cls->eval_environment && cls->field_count) {
            auto* slots = (uint32_t*)pool_calloc(c->program->frontend->pool, cls->field_count * sizeof(uint32_t));
            if (!slots) { diagnostic(c->program, site, "private field key plan"); return expression(c, NULL); }
            uint32_t fields = 0;
            bool passive = true;
            for (AstNode* member = ((AstBlockNode*)ast->body)->statements; member; member = member->next) {
                auto* field = member->node_type == AST_NODE_FIELD ? (AstClassFieldNode*)member : NULL;
                if (!field || !field->is_private || field->is_static || field->computed || field->value) { passive = false; break; }
                auto* key = (AstIdentNode*)field->key;
                LmdBinding* binding = private_binding(c->program, site, key->name);
                if (!binding || !passive_private(binding)) { passive = false; break; }
                slots[fields++] = binding->slot;
            }
            // no computed keys, methods or initializers can observe the intermediate metadata.
            if (passive && fields == cls->field_count) cls->private_field_slots = slots;
        }
    }
    if (cls->has_private_names) for (int i = 0; i < c->program->bindings->length; i++) {
        LmdBinding* binding = (LmdBinding*)c->program->bindings->data[i];
        if (!binding->private_name || binding->entry->node != site) continue;
        if (bulk_private && passive_private(binding)) continue;
        // only actual method/eval captures need cells; passive names use the existing owned binding slot.
        if (binding->captured) initialize_capture(c, binding, false);
        MIR_reg_t key = em_call_4(&c->em, "mvp_lmd_private_element", MIR_T_I64,
            MIR_T_I64, integer(c, ITEM_NULL), MIR_T_I64, relocated(c, s2it(binding->entry->name)),
            MIR_T_I64, integer(c, ITEM_JS_UNDEFINED), MIR_T_I64, integer(c, LMD_PRIVATE_NAME), true);
        check_error(c, key); write_binding(c, binding, boxed(c, key, K_STRING), site, true);
    }
    if (cls->eval_environment) {
        // private names start after heritage; the inner class name stays in its earlier TDZ record.
        eval_scope_enter(c, NULL, false, JS_INTERP_ENV_ORDINARY, true);
        MIR_reg_t pairs = em_call_1(&c->em, "mvp_lmd_array_new", MIR_T_I64, MIR_T_I64, integer(c, 0), true);
        check_error(c, pairs); LmdValue bindings = boxed(c, pairs, K_ARRAY);
        em_store_at(&c->em, c->eval_env, offsetof(JsInterpEnv, private_bindings), MIR_T_I64, pairs);
        int index = 0;
        for (int i = 0; i < c->program->bindings->length; i++) {
            LmdBinding* binding = (LmdBinding*)c->program->bindings->data[i];
            if (!binding->private_name || binding->entry->node != site) continue;
            array_index_write(c, {NULL, bindings, constant(c, index++)},
                boxed(c, relocated_constant(c, s2it(binding->entry->name)), K_STRING), true);
            array_index_write(c, {NULL, bindings, constant(c, index++)},
                boxed(c, binding_item(c, binding), K_STRING), true);
        }
    }
    LmdValue constructor = cls->dynamic && cls->constructor_id >= 0 ?
        new_function(c, (LmdFunction*)c->program->functions->data[cls->constructor_id]) : boxed(c, constant(c, ITEM_NULL));
    MIR_reg_t created = em_call_3(&c->em, "mvp_lmd_class_new", MIR_T_I64,
        MIR_T_P, relocated(c, (uint64_t)cls), MIR_T_I64, reg(c, base.reg), MIR_T_I64, reg(c, constructor.reg), true);
    check_error(c, created); LmdValue result = boxed(c, created, K_FUNCTION);
    if (cls->eval_environment)
        em_store_at(&c->em, c->eval_env, offsetof(JsInterpEnv, private_home_class), MIR_T_I64, created);
    MIR_reg_t metadata = cls->dynamic ? em_load_at(&c->em, created, LMD_CALLABLE_HOME_OFFSET, MIR_T_I64, "class_metadata") : 0;
    MIR_reg_t values = cls->dynamic ? em_load_at(&c->em, metadata, LMD_CLASS_VALUES_OFFSET, MIR_T_I64, "class_values") : 0;
    MIR_reg_t keys = cls->dynamic ? em_load_at(&c->em, metadata, LMD_CLASS_FIELD_KEYS_OFFSET, MIR_T_I64, "class_field_keys") : 0;
    MIR_reg_t initializers = cls->dynamic ? em_load_at(&c->em, metadata, LMD_CLASS_INITIALIZERS_OFFSET, MIR_T_I64, "class_initializers") : 0;
    if (!ast->name) {
        MIR_reg_t name = inferred_name ? inferred_name : named_evaluation_key(c, site);
        if (name) check_error(c, em_call_3(&c->em, "mvp_lmd_function_set_name", MIR_T_I64,
            MIR_T_I64, reg(c, created), MIR_T_I64, reg(c, name), MIR_T_I64, integer(c, 0), true));
    }
    uint32_t field_index = 0, initializer_index = 0;
    if (cls->dynamic && !cls->private_field_slots) for (AstNode* member = ((AstBlockNode*)ast->body)->statements; member; member = member->next) {
        if (member->node_type == JS_AST_NODE_STATIC_BLOCK) continue;
        bool field = member->node_type == AST_NODE_FIELD;
        AstMethodNode* method = field ? NULL : (AstMethodNode*)member;
        if (method && method->kind == AstMethodNode::JS_METHOD_CONSTRUCTOR) continue;
        AstClassFieldNode* definition = field ? (AstClassFieldNode*)member : NULL;
        bool computed, statics;
        AstNode* key_node = js_ast_class_element_key(member, &computed, &statics);
        LmdBinding* private_name = !computed && key_node->node_type == AST_NODE_IDENT ?
            private_binding(c->program, site, ((AstIdentNode*)key_node)->name) : NULL;
        LmdValue key = !computed && key_node->node_type == AST_NODE_IDENT ?
            boxed(c, relocated_constant(c, s2it(((AstIdentNode*)key_node)->name)), K_STRING) : canonical_key(c, expression(c, key_node));
        if (private_name) key = boxed(c, binding_item(c, private_name), K_STRING);
        if (field) {
            em_store_at(&c->em, keys, field_index++ * sizeof(Item), MIR_T_I64, key.reg);
            if (!statics) {
                AstNode* initializer = definition->value ? (AstNode*)js_script_field_initializer_ensure(c->program->frontend, definition) : NULL;
                LmdValue value = initializer ? new_function(c, function(c->program, initializer), created) : expression(c, NULL);
                em_store_at(&c->em, initializers, initializer_index++ * sizeof(Item), MIR_T_I64, box(c, value).reg);
            }
            continue;
        }
        LmdValue value = new_function(c, function(c->program, member), created);
        int prefix = method->kind == AstMethodNode::JS_METHOD_GET ? 1 : method->kind == AstMethodNode::JS_METHOD_SET ? 2 : 0;
        MIR_reg_t named = em_call_3(&c->em, "mvp_lmd_function_set_name", MIR_T_I64,
            MIR_T_I64, reg(c, value.reg), MIR_T_I64, reg(c, key.reg), MIR_T_I64, integer(c, prefix), true);
        check_error(c, named);
        MIR_reg_t owner = statics ? created : em_load_at(&c->em, values, sizeof(Item), MIR_T_I64, "class_prototype");
        if (private_name) {
            owner = em_load_at(&c->em, keys, (cls->field_count + (statics ? 1 : 0)) * sizeof(Item), MIR_T_I64, "private_methods");
        }
        MIR_reg_t defined = prefix ? em_call_4(&c->em, "mvp_lmd_define_accessor", MIR_T_I64,
            MIR_T_I64, reg(c, owner), MIR_T_I64, reg(c, key.reg), MIR_T_I64, reg(c, value.reg),
            MIR_T_I64, integer(c, (JSPD_NON_ENUMERABLE << 8) | (prefix == 2)), true) : private_name ?
            em_call_4(&c->em, "mvp_lmd_private_element", MIR_T_I64,
            MIR_T_I64, reg(c, owner), MIR_T_I64, reg(c, key.reg), MIR_T_I64, reg(c, value.reg),
            MIR_T_I64, integer(c, LMD_PRIVATE_METHOD), true) :
            em_call_4(&c->em, "mvp_lmd_class_property", MIR_T_I64,
            MIR_T_I64, reg(c, owner), MIR_T_I64, reg(c, key.reg), MIR_T_I64, reg(c, value.reg),
            MIR_T_I64, integer(c, LMD_PROP_CREATE_DATA | LMD_PROP_NON_ENUMERABLE), true);
        check_error(c, defined);
    }
    // the private class binding stays in TDZ through computed keys, then initializes before static execution.
    if (inner) write_binding(c, inner, result, site, true);
    if (cls->private_method_mask & 2) check_error(c, em_call_3(&c->em, "mvp_lmd_class_initialize_instance", MIR_T_I64,
        MIR_T_I64, reg(c, created), MIR_T_I64, reg(c, created), MIR_T_I64, integer(c, 1), true));
    field_index = 0;
    if (cls->dynamic) for (AstNode* member = ((AstBlockNode*)ast->body)->statements; member; member = member->next) {
        bool block = member->node_type == JS_AST_NODE_STATIC_BLOCK;
        if (!block && member->node_type != AST_NODE_FIELD) continue;
        AstClassFieldNode* field = block ? NULL : (AstClassFieldNode*)member;
        uint32_t key_slot = field_index;
        if (field) { field_index++; if (!field->is_static) continue; }
        LmdValue value = expression(c, NULL);
        if (block || field->value) {
            AstNode* initializer = (AstNode*)js_script_field_initializer_ensure(c->program->frontend, field,
                block ? (JsStaticBlockNode*)member : NULL);
            LmdValue function_value = new_function(c, function(c->program, initializer), created);
            MIR_reg_t called = em_call_5(&c->em, "mvp_lmd_class_invoke", MIR_T_I64,
                MIR_T_I64, reg(c, function_value.reg), MIR_T_I64, reg(c, created),
                MIR_T_P, block ? integer(c, 0) : reg(c, op(c, MIR_ADD, reg(c, keys), integer(c, key_slot * sizeof(Item)))),
                MIR_T_I64, integer(c, block ? 0 : 1), MIR_T_I64, integer(c, ITEM_JS_UNDEFINED), true);
            check_error(c, called); value = stable_item(c, called);
        }
        if (!block) {
            MIR_reg_t key = em_load_at(&c->em, keys, key_slot * sizeof(Item), MIR_T_I64, "static_field_key");
            MIR_reg_t defined = field->is_private ? em_call_4(&c->em, "mvp_lmd_private_element", MIR_T_I64,
                MIR_T_I64, reg(c, created), MIR_T_I64, reg(c, key), MIR_T_I64, reg(c, box(c, value).reg),
                MIR_T_I64, integer(c, LMD_PRIVATE_ADD), true) : em_call_4(&c->em, "mvp_lmd_class_property", MIR_T_I64,
                MIR_T_I64, reg(c, created), MIR_T_I64, reg(c, key), MIR_T_I64, reg(c, box(c, value).reg),
                MIR_T_I64, integer(c, LMD_PROP_CREATE_DATA), true);
            check_error(c, defined);
        }
    }
    LmdBinding* outer = binding(c->program, ast->outer_entry);
    if (outer && outer != inner) write_binding(c, outer, result, site, true);
    c->eval_env = saved_eval; c->eval_scope = saved_scope; c->strict = saved_strict;
    return result;
}
static void statements(LmdCompiler* c, AstNode* first) {
    for (AstNode* n = first; n; n = n->next) statement(c, n);
}
static void control_completion(LmdCompiler* c) {
    // if/iteration/switch initialize their own script completion to undefined.
    if (!c->function->id && !c->inlining) move(c, c->em.frame.return_reg, integer(c, ITEM_JS_UNDEFINED));
}
struct LmdIntegerLoopPlan { LmdCompiler* compiler; LmdInlineFrame frame; bool allowed; bool floating; int nodes; };
static void integer_loop_node(AstNode* node, void* opaque) {
    if (!node) return;
    LmdIntegerLoopPlan* plan = (LmdIntegerLoopPlan*)opaque;
    if (!plan->allowed || ++plan->nodes > 96) { plan->allowed = false; return; }
    Operator operation = OPERATOR_ASSIGN;
    AstNode* target = NULL;
    AstNode* left = NULL;
    AstNode* right = NULL;
    switch (node->node_type) {
    case AST_NODE_IDENT: {
        LmdBinding* b = identifier_binding(plan->compiler->program, (AstIdentNode*)node);
        if (!b || !b->native || b->external || b->kinds != K_NUMBER || b->owner != plan->frame.function)
            { plan->allowed = false; return; }
        for (int i = 0; i < plan->frame.count; i++) if (plan->frame.bindings[i] == b) return;
        if (plan->frame.count == 32) { plan->allowed = false; return; }
        plan->frame.bindings[plan->frame.count++] = b;
        plan->floating |= !b->integer; return;
    }
    case AST_NODE_LITERAL: {
        AstLiteralNode* literal = (AstLiteralNode*)node;
        if (literal->literal_type != AST_LITERAL_NUMBER || literal->is_bigint || !finite_integer(literal->value.number_value) ||
                literal->value.number_value < 0) plan->allowed = false;
        return;
    }
    case AST_NODE_ASSIGN: {
        AstAssignNode* a = (AstAssignNode*)node;
        target = left = a->left; right = a->right;
        operation = a->op == OPERATOR_ASSIGN ? OPERATOR_ASSIGN : compound_operator(a->op); break;
    }
    case AST_NODE_UNARY: {
        AstUnaryNode* u = (AstUnaryNode*)node;
        if (u->op != OPERATOR_JS_INCREMENT && u->op != OPERATOR_JS_DECREMENT)
            { plan->allowed = false; return; }
        target = u->operand; break;
    }
    case AST_NODE_BINARY: {
        AstBinaryNode* b = (AstBinaryNode*)node;
        operation = b->op; left = b->left; right = b->right; break;
    }
    case AST_NODE_BLOCK: case AST_NODE_EXPR_STMT: case AST_NODE_IF_EXPR: case AST_NODE_NULL: break;
    default: plan->allowed = false; return;
    }
    if (target) {
        LmdBinding* b = target->node_type == AST_NODE_IDENT
            ? identifier_binding(plan->compiler->program, (AstIdentNode*)target) : NULL;
        if (!b || b->intrinsic || b->entry->is_const || b->entry->is_function_name_binding)
            { plan->allowed = false; return; }
    }
    if (operation != OPERATOR_ASSIGN) {
        MirNumericOpPlan numeric = {};
        bool equality = operation == OPERATOR_EQ || operation == OPERATOR_NE ||
            operation == OPERATOR_JS_STRICT_EQ || operation == OPERATOR_JS_STRICT_NE;
        bool comparison = em_numeric_op_plan(operation, &numeric) && numeric.is_comparison;
        if (!equality && !comparison && operation != OPERATOR_ADD && operation != OPERATOR_SUB) {
            AstLiteralNode* factor = right && right->node_type == AST_NODE_LITERAL ? (AstLiteralNode*)right : NULL;
            if (operation == OPERATOR_MUL && (!factor || factor->literal_type != AST_LITERAL_NUMBER || factor->value.number_value <= 0))
                factor = left && left->node_type == AST_NODE_LITERAL ? (AstLiteralNode*)left : NULL;
            if ((operation != OPERATOR_MUL && operation != OPERATOR_DIV && operation != OPERATOR_MOD) ||
                    !factor || factor->literal_type != AST_LITERAL_NUMBER ||
                    !finite_integer(factor->value.number_value) || factor->value.number_value < 1)
                { plan->allowed = false; return; }
            uint64_t divisor = (uint64_t)factor->value.number_value;
            if (operation == OPERATOR_DIV && (divisor & (divisor - 1)))
                { plan->allowed = false; return; }
        }
    }
    js_ast_visit_children(node, integer_loop_node, opaque);
}
static void integer_loop(LmdCompiler* c, AstLoopControlNode* node, MIR_label_t done) {
    // a catch can observe induction variables when the interrupt completion exits the loop.
    if (c->completion || c->integer_bail || node->form != LOOP_FORM_WHILE || node->init || node->update || !node->cond) return;
    LmdIntegerLoopPlan plan = {}; plan.compiler = c; plan.allowed = true;
    plan.frame.parent = c->inlining; plan.frame.function = c->inlining ? c->inlining->function : c->function;
    if (!plan.frame.function->id) return;
    integer_loop_node(node->cond, &plan); integer_loop_node(node->body, &plan);
    if (!plan.allowed || !plan.floating || !plan.frame.count) return;
    MIR_label_t slow = label(c), head = label(c), finished = label(c), bail = label(c);
    MIR_reg_t snapshots[32];
    for (int i = 0; i < plan.frame.count; i++) {
        LmdValue value = read_binding(c, plan.frame.bindings[i], (AstNode*)node);
        MIR_reg_t integer = integer_lane(c, to_number(c, value), 1, INT53_MAX, slow, true);
        plan.frame.slots[i].value = {integer, K_NUMBER, false, 0, true, finite_range(1, INT53_MAX)};
        plan.frame.slots[i].integer = true; plan.frame.slots[i].kinds = K_NUMBER;
        plan.frame.slots[i].range = finite_range(1, INT53_MAX); plan.frame.slots[i].written = true;
        snapshots[i] = em_new_reg(&c->em, "iteration_snapshot", MIR_T_I64);
    }
    LmdLoopInterrupt interrupt(c);
    c->inlining = &plan.frame; c->integer_bail = bail;
    put_label(c, head);
    interrupt.poll();
    for (int i = 0; i < plan.frame.count; i++) move(c, snapshots[i], reg(c, plan.frame.slots[i].value.reg));
    branch_condition(c, node->cond, finished, false);
    statement(c, node->body); jump(c, head);
    c->inlining = plan.frame.parent; c->integer_bail = NULL;
    put_label(c, finished);
    for (int i = 0; i < plan.frame.count; i++) if (!plan.frame.bindings[i]->entry->is_const)
        write_binding(c, plan.frame.bindings[i], plan.frame.slots[i].value, (AstNode*)node);
    jump(c, done); put_label(c, bail);
    // only local numeric writes are admitted; undo a partial iteration before the generic replay.
    for (int i = 0; i < plan.frame.count; i++) if (!plan.frame.bindings[i]->entry->is_const)
        write_binding(c, plan.frame.bindings[i],
            {snapshots[i], K_NUMBER, false, 0, true, finite_range(1, INT53_MAX)}, (AstNode*)node);
    put_label(c, slow);
}
static void loop(LmdCompiler* c, AstLoopControlNode* n, const char* name = NULL, int length = 0,
    bool initialized = false);
static void dense_array_loop_node(AstNode* node, void* opaque) {
    if (!node) return;
    LmdDenseArrayLoopPlan* plan = (LmdDenseArrayLoopPlan*)opaque;
    if (!plan->allowed || ++plan->nodes > 160) { plan->allowed = false; return; }
    MvpLmdProgram* p = plan->compiler->program;
    constexpr unsigned objects = K_ARRAY | K_OBJECT | K_MAP | K_FUNCTION | K_TYPED_ARRAY;
    auto primitive = [&](AstNode* value) {
        if (!(kind(p, value, plan->compiler->inlining) & objects)) return true;
        // the visitor guards every array receiver, including an otherwise unknown parameter's length.
        return value && (value->node_type == AST_NODE_MEMBER_EXPR || value->node_type == AST_NODE_INDEX_EXPR) &&
            named(member_spelling((AstFieldNode*)value), "length");
    };
    AstNode* written = NULL;
    switch (node->node_type) {
    case AST_NODE_MEMBER_EXPR: case AST_NODE_INDEX_EXPR: {
        AstFieldNode* field = (AstFieldNode*)node;
        if (field->object->node_type != AST_NODE_IDENT || !(kind(p, field->object) & K_ARRAY) ||
                (!field->computed && !named(member_spelling(field), "length")) ||
                (field->computed && kind(p, field->property) != K_NUMBER)) { plan->allowed = false; return; }
        LmdBinding* binding = identifier_binding(p, (AstIdentNode*)field->object);
        if (!binding || binding->external || binding->captured || binding->reflected_global ||
                binding->owner != plan->compiler->function) { plan->allowed = false; return; }
        int i = 0; while (i < plan->count && plan->arrays[i] != binding) i++;
        if (i == plan->count) {
            if (i == 8) { plan->allowed = false; return; }
            plan->arrays[plan->count++] = binding;
        }
        if (field->computed) {
            plan->reads++;
            dense_array_loop_node(field->property, opaque);
        }
        return;
    }
    case AST_NODE_BINARY: {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        if (binary->op != OPERATOR_JS_STRICT_EQ && binary->op != OPERATOR_JS_STRICT_NE &&
                binary->op != OPERATOR_AND && binary->op != OPERATOR_OR &&
                (!primitive(binary->left) || !primitive(binary->right))) { plan->allowed = false; return; }
        break;
    }
    case AST_NODE_ASSIGN: {
        AstAssignNode* assignment = (AstAssignNode*)node;
        if (assignment->left->node_type == AST_NODE_MEMBER_EXPR || assignment->left->node_type == AST_NODE_INDEX_EXPR) {
            AstFieldNode* field = (AstFieldNode*)assignment->left;
            AstNode* body = plan->node->body;
            if (!field->computed || assignment->op != OPERATOR_ASSIGN || !primitive(assignment->right) || !body ||
                    node->source_span.start_byte < body->source_span.start_byte ||
                    node->source_span.end_byte > body->source_span.end_byte) { plan->allowed = false; return; }
            plan->element_writes++;
            break;
        }
        if (assignment->left->node_type != AST_NODE_IDENT || !primitive(assignment->left) ||
                (assignment->op != OPERATOR_ASSIGN && !primitive(assignment->right))) { plan->allowed = false; return; }
        written = assignment->left;
        break;
    }
    case AST_NODE_VARIABLE_DECLARATOR: {
        AstDeclaratorNode* declaration = (AstDeclaratorNode*)node;
        if (!declaration->id || declaration->id->node_type != AST_NODE_IDENT ||
                !declaration->init || kind(p, declaration->init) != K_NUMBER) { plan->allowed = false; return; }
        written = declaration->id;
        break;
    }
    case AST_NODE_UNARY: {
        AstUnaryNode* unary = (AstUnaryNode*)node;
        if (unary->op == OPERATOR_JS_DELETE ||
                (unary->op != OPERATOR_NOT && unary->op != OPERATOR_JS_TYPEOF && !primitive(unary->operand)) ||
                ((unary->op == OPERATOR_JS_INCREMENT || unary->op == OPERATOR_JS_DECREMENT) &&
                 unary->operand->node_type != AST_NODE_IDENT)) { plan->allowed = false; return; }
        if (unary->op == OPERATOR_JS_INCREMENT || unary->op == OPERATOR_JS_DECREMENT) written = unary->operand;
        break;
    }
    case AST_NODE_IDENT: {
        LmdBinding* binding = identifier_binding(p, (AstIdentNode*)node);
        if (!binding || binding->captured || binding->external || binding->reflected_global)
            { plan->allowed = false; return; }
        return;
    }
    case AST_NODE_LOOP:
        if (node != (AstNode*)plan->node) plan->nested = true;
        break;
    case AST_NODE_VAR_STAM: case AST_NODE_BLOCK: case AST_NODE_IF_EXPR: case AST_NODE_CONDITIONAL_EXPR:
    case AST_NODE_EXPR_STMT: case AST_NODE_RETURN_STAM: case AST_NODE_BREAK_STAM:
    case AST_NODE_CONTINUE_STAM:
    case AST_NODE_LITERAL: case AST_NODE_NULL: break;
    default: plan->allowed = false; return;
    }
    if (written) {
        LmdBinding* binding = identifier_binding(p, (AstIdentNode*)written);
        if (!binding || !binding->native || binding->kinds != K_NUMBER || binding->captured ||
                binding->external || binding->reflected_global || binding->owner != plan->compiler->function)
            { plan->allowed = false; return; }
        AstNode* init = binding->initializer;
        bool header = init && plan->node->init &&
            init->source_span.start_byte >= plan->node->init->source_span.start_byte &&
            init->source_span.end_byte <= plan->node->init->source_span.end_byte;
        bool iteration_local = init && binding->entry->is_lexical && plan->node->body &&
            init->source_span.start_byte >= plan->node->body->source_span.start_byte &&
            init->source_span.end_byte <= plan->node->body->source_span.end_byte;
        if (!binding->entry->is_parameter && (!binding->dominated || !init ||
                (!header && !iteration_local && init->source_span.end_byte > plan->node->source_span.start_byte)))
            { plan->allowed = false; return; }
        // body-scoped lexical locals are initialized again when this iteration replays.
        if (!iteration_local) {
            int i = 0; while (i < plan->writes && plan->written[i] != binding) i++;
            if (i == plan->writes) {
                if (i == 32) { plan->allowed = false; return; }
                plan->written[plan->writes++] = binding;
            }
        }
    }
    js_ast_visit_children(node, dense_array_loop_node, opaque);
}
static MIR_label_t dense_array_loop(LmdCompiler* c, AstLoopControlNode* node, const char* name, int length) {
    if (c->dense_array_count || c->inlining || !c->function->id || node->form == LOOP_FORM_DO_WHILE ||
            c->function->eval_environment ||
            c->program->excludes_sparse_arrays || !c->program->array_holes) return NULL;
    // reflection elsewhere cannot change uncaptured locals; the visitor rejects observable loop operations.
    // a bailout in the update would replay the preceding body; admit only local numeric updates.
    if (node->update) {
        AstNode* update = node->update;
        bool increment = update->node_type == AST_NODE_UNARY &&
            (((AstUnaryNode*)update)->op == OPERATOR_JS_INCREMENT || ((AstUnaryNode*)update)->op == OPERATOR_JS_DECREMENT) &&
            ((AstUnaryNode*)update)->operand->node_type == AST_NODE_IDENT;
        bool step = update->node_type == AST_NODE_ASSIGN &&
            ((AstAssignNode*)update)->left->node_type == AST_NODE_IDENT &&
            ((AstAssignNode*)update)->right->node_type == AST_NODE_LITERAL &&
            ((AstLiteralNode*)((AstAssignNode*)update)->right)->literal_type == AST_LITERAL_NUMBER;
        if (!increment && !step) return NULL;
    }
    LmdDenseArrayLoopPlan plan = {}; plan.compiler = c; plan.node = node; plan.allowed = true;
    dense_array_loop_node((AstNode*)node, &plan);
    // a write version has no later element access or nested iteration that could replay its store.
    if (!plan.allowed || !plan.count || (plan.element_writes ?
            plan.element_writes != 1 || plan.reads != 1 || plan.nested : plan.reads < 2)) return NULL;
    AstIndex* tree = &c->program->frontend->ast_index;
    for (int i = 0; i < plan.count; i++) {
        LmdBinding* binding = plan.arrays[i];
        // receiver guards still require bindings initialized before the loop, excluding body-local arrays.
        if (!binding->entry->is_parameter && (!binding->dominated || !binding->initializer ||
                ast_index_find(tree, binding->initializer) >= ast_index_find(tree, (AstNode*)node))) return NULL;
    }
    log_debug("js mvp dense loop: %s at byte %u, %d arrays", c->function->name, node->source_span.start_byte, plan.count);
    MIR_label_t slow = label(c), done = label(c);
    plan.bail = label(c);
    for (int i = 0; i < plan.count; i++) {
        LmdValue owner = read_binding(c, plan.arrays[i], (AstNode*)node);
        if (semantic(owner) != K_ARRAY)
            branch(c, MIR_BNE, slow, reg(c, tag(c, box(c, owner))), integer(c, LMD_TYPE_ARRAY));
        if (plan.element_writes) {
            MIR_reg_t storage = em_call_1(&c->em, "mvp_lmd_array_indices_use_own_storage", MIR_T_I64,
                MIR_T_I64, reg(c, owner.reg), true);
            branch(c, MIR_BEQ, slow, reg(c, storage), integer(c, 0));
        } else branch_sparse_array(c, slow, owner.reg);
        c->dense_arrays[i] = plan.arrays[i];
    }
    for (int i = 0; i < plan.writes; i++) {
        plan.snapshots[i] = read_binding(c, plan.written[i], (AstNode*)node);
        plan.snapshots[i].reg = em_new_reg(&c->em, "dense_iteration_snapshot", plan.written[i]->integer ? MIR_T_I64 : MIR_T_D);
    }
    // holes and bounds misses replay only the current iteration, before any observable operation.
    c->dense_array_count = plan.count; c->dense_loop = &plan;
    loop(c, node, name, length, true);
    c->dense_array_count = 0; c->dense_loop = NULL;
    jump(c, done); put_label(c, plan.bail);
    for (int i = 0; i < plan.writes; i++)
        write_binding(c, plan.written[i], plan.snapshots[i], (AstNode*)node);
    put_label(c, slow);
    return done;
}
static void loop(LmdCompiler* c, AstLoopControlNode* n, const char* name, int length, bool initialized) {
    control_completion(c);
    if (!initialized) {
        scope_initialize(c, n->vars);
        if (n->init) {
            if (n->init->node_type == AST_NODE_VAR_STAM) statement(c, n->init);
            else (void)expression(c, n->init);
        }
        clone_loop_bindings(c, n->vars);
    }
    // both storage versions share initialization; a mid-loop bailout must retain its current induction value.
    MIR_label_t dense_done = initialized ? NULL : dense_array_loop(c, n, name, length);
    MIR_label_t start = label(c), test = label(c), next = label(c), done = label(c);
    LmdControl control = {c->control, done, next, name, length, (AstNode*)n}; c->control = &control;
    for (LmdControl* outer = control.parent; outer; outer = outer->parent) {
        if (outer->loop_target == (AstNode*)n) { outer->next = next; outer->stop = done; }
    }
    integer_loop(c, n, done);
    LmdLoopInterrupt interrupt(c);
    if (n->form != LOOP_FORM_DO_WHILE) jump(c, test);
    put_label(c, start);
    interrupt.poll();
    LmdNumberFacts facts = {}; facts.parent = c->number_facts;
    // eval only invalidates these facts when it can observe this function's bindings.
    if (!c->inlining && !c->function->eval_environment && n->form != LOOP_FORM_DO_WHILE &&
            n->cond && n->cond->node_type == AST_NODE_BINARY) {
        AstBinaryNode* condition = (AstBinaryNode*)n->cond;
        bool ordered = condition->op == OPERATOR_LT || condition->op == OPERATOR_LE ||
            condition->op == OPERATOR_GT || condition->op == OPERATOR_GE;
        AstNode* operands[] = {condition->left, condition->right};
        bool passive = ordered;
        for (int i = 0; passive && i < 2; i++) {
            AstNode* operand = operands[i];
            if (operand->node_type == AST_NODE_LITERAL && ((AstLiteralNode*)operand)->literal_type == AST_LITERAL_NUMBER) continue;
            LmdBinding* binding = operand->node_type == AST_NODE_IDENT ? identifier_binding(c->program, (AstIdentNode*)operand) : NULL;
            if (!binding || !binding->native || binding->captured || binding->external || binding->with_lookup ||
                    binding->owner != c->function || (binding->kinds & ~(K_NUMBER | K_UNDEFINED))) passive = false;
            else if (binding->number_present) facts.bindings[i] = binding;
        }
        // true ordered comparisons exclude both undefined and NaN; passive operands cannot overwrite each other.
        if (passive) {
            for (int i = 0; i < 2; i++) if (facts.bindings[i])
                facts.ranges[i] = ordered_integer_range(c->program, facts.bindings[i], condition);
            c->number_facts = &facts;
        }
    }
    statement(c, n->body);
    c->number_facts = facts.parent;
    put_label(c, next); clone_loop_bindings(c, n->vars);
    if (n->update) (void)expression(c, n->update);
    put_label(c, test);
    if (c->dense_loop && c->dense_loop->node == n)
        for (int i = 0; i < c->dense_loop->writes; i++) {
            LmdBinding* binding = c->dense_loop->written[i];
            move(c, c->dense_loop->snapshots[i].reg, reg(c, binding->reg), !binding->integer);
        }
    if (n->cond) branch_condition(c, n->cond, start);
    else jump(c, start);
    put_label(c, done); c->control = control.parent;
    if (dense_done) put_label(c, dense_done);
}
static LmdValue iterable_source(LmdCompiler* c, AstNode* iterable, int& projection, MIR_reg_t& selected_projection, bool* object) {
    LmdValue owner;
    projection = 6;
    selected_projection = 0;
    bool object_projection = false;
    if (iterable->node_type == AST_NODE_CALL_EXPR && ((AstCallNode*)iterable)->callee->node_type == AST_NODE_MEMBER_EXPR) {
        AstNode* receiver = ((AstFieldNode*)((AstCallNode*)iterable)->callee)->object;
        LmdBinding* binding = receiver->node_type == AST_NODE_IDENT
            ? identifier_binding(c->program, (AstIdentNode*)receiver) : NULL;
        object_projection = binding && binding->intrinsic == 5;
    }
    AstFieldNode* projection_field = iterable->node_type == AST_NODE_CALL_EXPR &&
        (((AstCallNode*)iterable)->callee->node_type == AST_NODE_MEMBER_EXPR ||
         ((AstCallNode*)iterable)->callee->node_type == AST_NODE_INDEX_EXPR)
        ? (AstFieldNode*)((AstCallNode*)iterable)->callee : NULL;
    String* projection_name = projection_field ? member_spelling(projection_field) : NULL;
    bool projection_call = projection_name && (named(projection_name, "entries") ||
        named(projection_name, "keys") || named(projection_name, "values"));
    if (!object_projection && projection_call && iterable->node_type == AST_NODE_CALL_EXPR &&
            (((AstCallNode*)iterable)->callee->node_type == AST_NODE_MEMBER_EXPR ||
             ((AstCallNode*)iterable)->callee->node_type == AST_NODE_INDEX_EXPR)) {
        AstCallNode* call = (AstCallNode*)iterable;
        LmdReference ref = reference(c, call->callee, true);
        LmdValue method = box(c, read_reference(c, ref, iterable, true));
        LmdValue* args = call_arguments(c, call, true);
        projection = named(projection_name, "entries") ? 6 : named(projection_name, "keys") ? 7 : 8;
        MIR_reg_t selected = em_new_reg(&c->em, "iterable_owner", MIR_T_I64);
        selected_projection = constant(c, 6);
        MIR_label_t builtin = label(c), selected_done = label(c);
        branch(c, MIR_BEQ, builtin, reg(c, method.reg), integer(c, mvp_lmd_method_token(projection)));
        LmdCallCapture capture = {method, args};
        move(c, selected, reg(c, box(c, ::call(c, call, &capture)).reg)); jump(c, selected_done);
        put_label(c, builtin); move(c, selected, reg(c, ref.owner.reg));
        move(c, selected_projection, integer(c, projection));
        put_label(c, selected_done); owner = boxed(c, selected);
    } else owner = box(c, expression(c, iterable));
    if (object) *object = object_projection;
    return owner;
}
// direct collection traversal mirrors Lambda loops: owner + stable position, no iterator object.
static void for_of(LmdCompiler* c, JsForOfNode* loop, const char* name = NULL, int length = 0) {
    control_completion(c); scope_initialize(c, loop->vars);
    if (loop->init) {
        LmdValue initial = expression(c, loop->init);
        if (loop->node_type == AST_NODE_FOR_IN_STAM && loop->declares_binding)
            bind_pattern(c, loop->left, initial, false);
    }
    AstNode* iterable = loop->right;
    int projection = 6; MIR_reg_t selected_projection = 0; bool object_projection = false;
    bool enumerate = loop->node_type == AST_NODE_FOR_IN_STAM;
    LmdValue enumerated, owner;
    if (enumerate) {
        enumerated = box(c, expression(c, iterable));
        unsigned primitives = K_BOOL | K_NUMBER | K_STRING | K_BIGINT | K_SYMBOL;
        unsigned kinds = semantic(enumerated) ? semantic(enumerated) : K_ANY;
        if (c->program->property_callbacks && (kinds & primitives)) {
            // key collection and deletion checks must retain the same ToObject receiver.
            MIR_reg_t object = em_new_reg(&c->em, "enumerated_object", MIR_T_I64);
            move(c, object, reg(c, enumerated.reg));
            MIR_label_t ready = label(c);
            if (kinds & K_NULL)
                branch(c, MIR_BEQ, ready, reg(c, enumerated.reg), integer(c, ITEM_NULL));
            if (kinds & K_UNDEFINED)
                branch(c, MIR_BEQ, ready, reg(c, enumerated.reg), integer(c, ITEM_JS_UNDEFINED));
            MIR_reg_t converted = em_call_2(&c->em, "mvp_lmd_box_primitive", MIR_T_I64,
                MIR_T_I64, reg(c, enumerated.reg), MIR_T_I64, integer(c, ITEM_JS_UNDEFINED), true);
            check_error(c, converted); move(c, object, reg(c, converted));
            put_label(c, ready);
            enumerated = boxed(c, object, (kinds & ~primitives) | K_OBJECT);
        }
        MIR_reg_t keys = em_call_1(&c->em, "mvp_lmd_for_in_keys", MIR_T_I64, MIR_T_I64, reg(c, enumerated.reg), true);
        check_error(c, keys); owner = boxed(c, keys, K_ARRAY); object_projection = true;
    } else if (c->program->iterators) owner = box(c, expression(c, iterable));
    else owner = iterable_source(c, iterable, projection, selected_projection, &object_projection);
    bool direct_pair = !loop->is_await && simple_array_pattern(loop->left, 2) && projection == 6 &&
        !enumerate && pattern_iterators_are_intrinsic(c->program);
    bool borrow_pair = direct_pair && loop->declares_binding && !c->inlining;
    if (borrow_pair) for (AstNode* e = ((AstArrayNode*)loop->left)->item; e; e = e->next) {
        LmdBinding* b = e->node_type == AST_NODE_IDENT ? identifier_binding(c->program, (AstIdentNode*)e) : NULL;
        // both local bindings adopt their scalars before any body call or captured-cell allocation.
        if (!b || b->owner != c->function || !b->owner->id || b->captured || b->external ||
                b->reflected_global || b->native || b->intrinsic) borrow_pair = false;
    }
    MIR_reg_t components[2] = {0, 0};
    MIR_label_t bind_pair = direct_pair ? label(c) : NULL;
    if (direct_pair) for (int i = 0; i < 2; i++) components[i] = em_new_reg(&c->em, "pair_component", MIR_T_I64);
    MIR_reg_t cursor = constant(c, 0), item = em_new_reg(&c->em, "iteration_value", MIR_T_I64);
    MIR_label_t test = label(c), array = label(c), map = label(c), body = label(c), next = label(c), done = label(c);
    MIR_reg_t active = constant(c, 0);
    bool intrinsic_source = !loop->is_await && pattern_iterators_are_intrinsic(c->program) &&
        (semantic(owner) == K_ARRAY || semantic(owner) == K_MAP);
    bool protocol = c->program->iterators && !enumerate && !intrinsic_source;
    bool native_map = !protocol && semantic(owner) == K_MAP;
    bool native_array = !protocol && semantic(owner) == K_ARRAY;
    MIR_reg_t general_pair = direct_pair && !native_map ? constant(c, 0) : 0;
    MIR_reg_t record = protocol ? constant(c, ITEM_NULL) : 0;
    if (record) boxed(c, record);
    // iteration snapshots the owner; mutations cannot change its array/ordered-map representation.
    MIR_reg_t iterable_type = tag(c, owner);
    MIR_label_t registered = label(c), invalid = label(c), ordered = label(c);
    // without Symbol exposure this closed unit cannot replace an Array's iterator.
    if (!loop->is_await && (!protocol || !c->program->symbols))
        branch(c, MIR_BEQ, registered, reg(c, iterable_type), integer(c, LMD_TYPE_ARRAY));
    branch(c, MIR_BNE, invalid, reg(c, iterable_type), integer(c, LMD_TYPE_MAP));
    if (!protocol) branch(c, MIR_BEQ, ordered, reg(c, em_load_at(&c->em, owner.reg,
        LAMBDA_GC_OFF_CONTAINER_MAP_KIND, MIR_T_U8, "map_kind")), integer(c, MAP_KIND_ORDERED));
    put_label(c, invalid);
    if (protocol) {
        MIR_reg_t acquired = loop->is_await
            ? em_call_1(&c->em, "js_get_async_iterator", MIR_T_I64, MIR_T_I64, reg(c, owner.reg), true)
            : em_call_2(&c->em, "mvp_lmd_iterator_get", MIR_T_I64,
                MIR_T_P, reg(c, c->unit), MIR_T_I64, reg(c, owner.reg), true);
        check_error(c, acquired); move(c, record, reg(c, acquired)); move(c, active, integer(c, 1)); jump(c, registered);
    } else fail(c, LMD_MVP_CAPABILITY, iterable);
    put_label(c, ordered);
    MIR_reg_t count = em_load_at(&c->em, owner.reg, offsetof(LambdaGcOrderedMapLayout, cursors), MIR_T_I64, "active_cursors");
    em_store_at(&c->em, owner.reg, offsetof(LambdaGcOrderedMapLayout, cursors), MIR_T_I64,
        op(c, MIR_ADD, reg(c, count), integer(c, 1)));
    move(c, active, integer(c, 1)); put_label(c, registered);
    // the captured iterable's representation also fixes its stride across body mutations.
    MIR_reg_t step = constant(c, 1); MIR_label_t stride_ready = label(c);
    branch(c, MIR_BNE, stride_ready, reg(c, iterable_type), integer(c, LMD_TYPE_MAP));
    move(c, step, integer(c, 4)); put_label(c, stride_ready);
    LmdControl control = {c->control, done, next, name, length, (AstNode*)loop, owner.reg, active, record, loop->is_await}; c->control = &control;
    for (LmdControl* outer = control.parent; outer; outer = outer->parent)
        if (outer->loop_target == (AstNode*)loop) { outer->next = next; outer->stop = done; }
    LmdLoopInterrupt interrupt(c);
    put_label(c, test);
    interrupt.poll();
    if (protocol) {
        MIR_label_t native = label(c);
        branch(c, MIR_BEQ, native, reg(c, record), integer(c, ITEM_NULL));
        move(c, item, reg(c, iterator_value(c, record, active, done, false, loop->is_await).reg));
        jump(c, body); put_label(c, native);
    }
    if (!native_map) {
        // a proven native Map never enters array iteration or allocates its scalar snapshots.
        if (!native_array) branch(c, MIR_BNE, map, reg(c, iterable_type), integer(c, LMD_TYPE_ARRAY));
        put_label(c, array);
        branch(c, MIR_UBGE, done, reg(c, cursor), reg(c, em_load_at(&c->em, owner.reg,
            LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "iteration_length")));
        // Object projections produce a fresh dense snapshot even when the input has holes.
        LmdReference element = {NULL, owner, cursor};
        // the cursor is a numeric index already checked above, with no intervening callback.
        element.numeric_key = element.in_bounds = true;
        LmdBinding* iteration_binding = loop->left->node_type == AST_NODE_IDENT
            ? identifier_binding(c->program, (AstIdentNode*)loop->left) : NULL;
        // a local identifier adopts the scalar before the body; captured cells may allocate first.
        bool borrow_iteration = !enumerate && iteration_binding && iteration_binding->owner->id && !iteration_binding->captured &&
            !iteration_binding->external && !iteration_binding->reflected_global;
        move(c, item, reg(c, box(c, array_read(c, element, borrow_iteration || borrow_pair, false, object_projection)).reg)); jump(c, body);
    }
    // a proven native Array stays in owner/index traversal; its loop never dispatches through ordered Map storage.
    if (!native_array) {
        put_label(c, map);
        MIR_reg_t entries = em_load_at(&c->em, owner.reg, offsetof(LambdaGcOrderedMapLayout, entries), MIR_T_I64, "ordered_entries");
        branch(c, MIR_UBGE, done, reg(c, cursor), reg(c, em_load_at(&c->em, entries,
            LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "ordered_length")));
        MIR_label_t live = label(c);
        MIR_reg_t marker = em_load_at(&c->em, em_array_element_address(&c->em, entries,
            op(c, MIR_ADD, reg(c, cursor), integer(c, 3)), 8), 0, MIR_T_I64, "ordered_live");
        // dense entries need no native call; the existing readonly helper skips tombstones.
        branch(c, MIR_BEQ, live, reg(c, marker), integer(c, ITEM_TRUE));
        move(c, cursor, reg(c, em_call_2(&c->em, "mvp_lmd_map_next", MIR_T_I64,
            MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, cursor), true)));
        branch(c, MIR_BLT, done, reg(c, cursor), integer(c, 0));
        put_label(c, live);
        bool native_entry = direct_pair || projection == 7 || projection == 8;
        MIR_label_t ordinary_entries = selected_projection && projection != 6 ? label(c) : NULL;
        if (native_entry) {
            // an ordinary method's returned Map uses its default entry iteration.
            if (ordinary_entries) branch(c, MIR_BEQ, ordinary_entries,
                reg(c, selected_projection), integer(c, 6));
            int count = direct_pair ? 2 : 1;
            for (int i = 0; i < count; i++) {
                MIR_reg_t slot = op(c, MIR_ADD, reg(c, cursor), integer(c, direct_pair ? i : projection == 8));
                MIR_reg_t loaded = em_load_at(&c->em,
                    em_array_element_address(&c->em, entries, slot, 8), 0, MIR_T_I64, "ordered_value");
                LmdValue value = borrow_pair ? boxed(c, loaded) : stable_item(c, loaded);
                move(c, direct_pair ? components[i] : item, reg(c, value.reg));
                boxed(c, direct_pair ? components[i] : item);
            }
            if (general_pair) move(c, general_pair, integer(c, 0));
            jump(c, direct_pair ? bind_pair : body);
        }
        if (!native_entry || ordinary_entries) {
            if (ordinary_entries) put_label(c, ordinary_entries);
            MIR_reg_t entry = em_call_3(&c->em, "mvp_lmd_map_entry", MIR_T_I64,
                MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, cursor), MIR_T_I64,
                selected_projection ? reg(c, selected_projection) : integer(c, projection), true);
            check_error(c, entry); move(c, item, reg(c, stable_item(c, entry).reg));
        }
    }
    put_label(c, body); LmdValue value = boxed(c, item);
    if (enumerate && semantic(enumerated) != K_STRING) {
        MIR_label_t checked = label(c);
        branch(c, MIR_BEQ, checked, reg(c, tag(c, enumerated)), integer(c, LMD_TYPE_STRING));
        MIR_reg_t present = property_access(c, LMD_PROP_HAS, enumerated.reg, value.reg);
        check_error(c, present); branch(c, MIR_BEQ, next, reg(c, present), integer(c, ITEM_FALSE));
        put_label(c, checked);
    }
    AstNode* head = loop->left;
    if (direct_pair) {
        if (!native_map) {
            move(c, general_pair, integer(c, 0));
            MIR_label_t pair = label(c);
            branch(c, MIR_BEQ, pair, reg(c, tag(c, value)), integer(c, LMD_TYPE_ARRAY));
            // native entry arrays need no iterator allocation; other values retain IteratorBindingInitialization.
            move(c, general_pair, integer(c, 1)); jump(c, bind_pair); put_label(c, pair);
        }
        // local pairs adopt both values without reentry; array_read still snapshots accessor results.
        if (!native_map) for (int i = 0; i < 2; i++) move(c, components[i], reg(c, box(c,
            array_read(c, {NULL, boxed(c, value.reg, K_ARRAY), constant(c, i), true, i}, borrow_pair)).reg));
        put_label(c, bind_pair);
    }
    // both native Map pairs and ordinary iterable values create a fresh lexical binding.
    if (loop->declares_binding) for (NameEntry* e = loop->vars ? loop->vars->first : NULL; e; e = e->next) {
        LmdBinding* b = binding(c->program, e);
        if (b && b->captured && e->is_lexical) initialize_capture(c, b, false);
    }
    if (c->function->eval_environment && loop->vars && c->eval_scope == loop->vars)
        eval_scope_enter(c, loop->vars, true);
    if (direct_pair) {
        MIR_label_t general = general_pair ? label(c) : NULL, bound = general_pair ? label(c) : NULL;
        if (general_pair) branch_truth(c, general, general_pair);
        int i = 0;
        for (AstNode* e = ((AstArrayNode*)head)->item; e; e = e->next, i++) {
            LmdValue component = boxed(c, components[i]);
            write_binding(c, identifier_binding(c->program, (AstIdentNode*)e), component, e, loop->declares_binding);
        }
        if (general_pair) {
            jump(c, bound); put_label(c, general);
            bind_pattern(c, head, value, loop->declares_binding); put_label(c, bound);
        }
    } else bind_pattern(c, head, value, loop->declares_binding);
    statement(c, loop->body); put_label(c, next);
    move(c, cursor, reg(c, op(c, MIR_ADD, reg(c, cursor), reg(c, step)))); jump(c, test);
    // both normal loop exits already branch past this cold block.
    emit_iterator_error_pad(c, &control);
    put_label(c, done); release_iterators(c, control.parent); c->control = control.parent;
}
static void statement(LmdCompiler* c, AstNode* n) {
    if (!n) return;
    if (n->node_type == AST_NODE_LOOP || n->node_type == AST_NODE_FOR_IN_STAM ||
            n->node_type == AST_NODE_FOR_OF_STAM || n->node_type == JS_AST_NODE_LABELED_STATEMENT) {
        // nested backedges do not recheck the enclosing loop's numeric condition.
        for (LmdNumberFacts* facts = c->number_facts; facts; facts = facts->parent)
            for (LmdBinding*& known : facts->bindings) known = NULL;
    }
    MIR_reg_t saved_eval = c->eval_env; NameScope* saved_scope = c->eval_scope;
    switch (n->node_type) {
    case AST_SCRIPT:
        scope_initialize(c, ((AstScript*)n)->global_vars); statements(c, ((AstScript*)n)->body); break;
    case AST_NODE_IMPORT: break;
    case AST_NODE_EXPORT: {
        AstExportDeclNode* exported = (AstExportDeclNode*)n;
        if (!exported->declaration) break;
        if (!exported->is_default) { statement(c, exported->declaration); break; }
        LmdBinding* target = binding(c->program, &c->program->source_module->default_entry);
        if (!target) { statement(c, exported->declaration); break; }
        if (exported->declaration->node_type == AST_NODE_FUNC) break; // initialized with other hoisted functions
        LmdValue value = exported->declaration->node_type == AST_NODE_CLASS
            ? class_expression(c, (AstClassNode*)exported->declaration) : expression(c, exported->declaration);
        write_binding(c, target, value, n, true); break;
    }
    case AST_NODE_BLOCK:
        scope_initialize(c, ((AstBlockNode*)n)->vars); statements(c, ((AstBlockNode*)n)->statements); break;
    case JS_AST_NODE_WITH_STATEMENT: {
        JsWithStatementNode* statement_node = (JsWithStatementNode*)n;
        LmdValue input = box(c, expression(c, statement_node->object));
        MIR_reg_t object = em_call_2(&c->em, "mvp_lmd_box_primitive", MIR_T_I64,
            MIR_T_I64, reg(c, input.reg), MIR_T_I64, integer(c, ITEM_JS_UNDEFINED), true);
        check_error(c, object); boxed(c, object);
        LmdBinding* binding = c->program->with_objects[ast_index_find(&c->program->frontend->ast_index, n)];
        initialize_capture(c, binding, false);
        write_binding(c, binding, boxed(c, object), n, true);
        if (c->function->eval_environment) {
            eval_scope_enter(c, NULL, false, JS_INTERP_ENV_WITH_OBJECT);
            em_store_at(&c->em, c->eval_env, offsetof(JsInterpEnv, with_object), MIR_T_I64, object);
        }
        control_completion(c); statement(c, statement_node->body); break;
    }
    case AST_NODE_NULL: break;
    case AST_NODE_TRY_STAM: {
        AstTryNode* node = (AstTryNode*)n;
        MIR_label_t caught = label(c), thrown = label(c), normal = label(c), done = label(c);
        MIR_reg_t error = em_new_reg(&c->em, "caught_error", MIR_T_I64); boxed(c, error);
        LmdCompletion completion = {c->completion, c->control, node->finalizer,
            node->handler ? caught : thrown, error, c->eval_env, c->eval_scope};
        c->completion = &completion;
        statement(c, node->block); jump(c, normal);
        if (node->handler) {
            put_label(c, caught); completion.handler = thrown;
            JsCatchNode* handler = (JsCatchNode*)node->handler;
            scope_initialize(c, handler->vars);
            MIR_reg_t value = em_call_2(&c->em, "mvp_lmd_caught_value", MIR_T_I64,
                MIR_T_P, relocated(c, (uint64_t)c->program->library), MIR_T_I64, reg(c, error), true);
            check_error(c, value);
            if (handler->param) bind_pattern(c, handler->param, boxed(c, value), true);
            statement(c, handler->body); jump(c, normal);
        }
        c->completion = completion.parent;
        c->eval_env = saved_eval; c->eval_scope = saved_scope;
        put_label(c, thrown); statement(c, node->finalizer); return_error(c, error);
        put_label(c, normal); statement(c, node->finalizer); jump(c, done);
        put_label(c, done); break;
    }
    case AST_NODE_FUNC: {
        AstFuncNode* f = (AstFuncNode*)n;
        LmdBinding* b = binding(c->program, f->entry);
        if (b && b->entry->annex_b_outer_binding) {
            LmdBinding* outer = binding(c->program, b->entry->annex_b_outer_binding);
            write_binding(c, outer, read_binding(c, b, n), n, true);
        }
        break;
    }
    case AST_NODE_CLASS: class_expression(c, (AstClassNode*)n); break;
    case AST_NODE_RAISE_STAM: {
        AstNode* operand = ((AstRaiseNode*)n)->value;
        LmdValue value = expression(c, operand);
        MIR_reg_t error = em_call_2(&c->em, "mvp_lmd_throw", MIR_T_I64,
            MIR_T_I64, reg(c, box(c, value).reg), MIR_T_I64, integer(c, 0), true);
        return_error(c, error); break;
    }
    case AST_NODE_VAR_STAM:
        for (AstNode* d = ((AstVarDeclNode*)n)->declarations; d; d = d->next) {
            AstDeclaratorNode* declaration = (AstDeclaratorNode*)d;
            if (declaration->id->node_type != AST_NODE_IDENT) {
                bool initialize = !c->function->eval_lookup || ((AstVarDeclNode*)n)->kind != JS_VAR_VAR;
                bind_pattern(c, declaration->id, expression(c, declaration->init), initialize); continue;
            }
            LmdBinding* b = binding(c->program, ((AstIdentNode*)declaration->id)->entry);
            // an unobserved, initialized local function has only direct uses and no identity allocation.
            if (declaration->init && b->target && (AstNode*)b->target->ast == declaration->init &&
                    b->target->closed_calls && !b->observed && !b->assigned) continue;
            if (declaration->init || ((AstVarDeclNode*)n)->kind != JS_VAR_VAR) {
                LmdBinding* target = ((AstVarDeclNode*)n)->kind == JS_VAR_VAR ?
                    identifier_binding(c->program, (AstIdentNode*)declaration->id) : b;
                LmdReference resolved = {};
                if (target && target->with_lookup) resolved = with_reference(c, target, declaration->id);
                else if (((AstVarDeclNode*)n)->kind == JS_VAR_VAR && eval_reference_site(c->program, declaration->id))
                    resolved = eval_reference(c, (AstIdentNode*)declaration->id);
                // an uncaptured local adopts the scalar immediately, with no intervening call.
                bool borrow = !c->inlining && b->owner == c->function && b->owner->id &&
                    !b->captured && !b->external && !b->reflected_global && !b->native && !b->intrinsic && !resolved.with_object;
                LmdValue value = expression(c, declaration->init, borrow);
                if (!b->shape_hint || b->shape_hint->scalar_binding != b) {
                    if (resolved.with_object || resolved.eval_reference) write_reference(c, resolved, value, declaration->id);
                    else write_binding(c, b, value, d, true);
                }
            }
        }
        break;
    case AST_NODE_EXPR_STMT: {
        LmdValue evaluated = expression(c, ((AstExprStmtNode*)n)->expression);
        if (c->function->id || c->inlining) break;
        LmdValue v = box(c, evaluated);
        // retain the latest expression completion; declarations do not reset it.
        move(c, c->em.frame.return_reg, reg(c, v.reg)); break;
    }
    case AST_NODE_RETURN_STAM:
        if (c->inlining) {
            LmdInlineFrame* frame = c->inlining;
            while (frame && !frame->exit) frame = frame->parent;
            if (frame) {
                LmdValue result = expression(c, ((AstReturnNode*)n)->value);
                move(c, frame->result, reg(c, frame->result_kind != K_ANY
                    ? scalar_reg(c, result, frame->result_kind) : box(c, result).reg),
                    frame->result_kind == K_NUMBER);
                jump(c, frame->exit); break;
            }
        }
        if (c->function->ast && c->function->ast->is_async && c->function->ast->is_generator && ((AstReturnNode*)n)->value) {
            LmdValue value = box(c, expression(c, ((AstReturnNode*)n)->value));
            MIR_reg_t resumed = emit_await(c, value.reg);
            check_error(c, resumed); return_value(c, stable_item(c, resumed));
        } else {
            // JC24: self recursion is a call and must reach the stack guard.
            return_value(c, expression(c, ((AstReturnNode*)n)->value));
        }
        break;
    case AST_NODE_IF_EXPR: {
        control_completion(c);
        JsIfNode* f = (JsIfNode*)n;
        MIR_label_t alternate = label(c), done = label(c);
        branch_condition(c, f->test, alternate, false);
        LmdNumberFacts* facts = c->number_facts;
        LmdBinding* before[2] = {}, *after_then[2] = {};
        if (facts) for (int i = 0; i < 2; i++) before[i] = facts->bindings[i];
        scope_initialize(c, f->consequent_vars); statement(c, f->then); jump(c, done);
        put_label(c, alternate); c->eval_env = saved_eval; c->eval_scope = saved_scope;
        // alternatives start with the same facts; only facts valid on both exits survive the join.
        if (facts) for (int i = 0; i < 2; i++) {
            after_then[i] = facts->bindings[i]; facts->bindings[i] = before[i];
        }
        scope_initialize(c, f->alternate_vars); statement(c, f->otherwise);
        if (facts) for (int i = 0; i < 2; i++)
            if (facts->bindings[i] != after_then[i]) facts->bindings[i] = NULL;
        put_label(c, done); break;
    }
    case AST_NODE_LOOP: loop(c, (AstLoopControlNode*)n); break;
    case AST_NODE_FOR_IN_STAM: case AST_NODE_FOR_OF_STAM: for_of(c, (JsForOfNode*)n); break;
    case AST_NODE_MATCH_EXPR: {
        control_completion(c);
        JsSwitchNode* s = (JsSwitchNode*)n;
        LmdValue discriminant = box(c, expression(c, s->discriminant));
        scope_initialize(c, s->vars);
        int count = ast_linked_node_count(s->cases);
        MIR_label_t* cases = (MIR_label_t*)mem_calloc(count, sizeof(MIR_label_t), MEM_CAT_TEMP);
        MIR_label_t done = label(c), fallback = done;
        LmdControl control = {c->control, done, NULL, NULL, 0, NULL}; c->control = &control;
        int i = 0;
        for (AstNode* a = s->cases; a; a = a->next, i++) {
            cases[i] = label(c); AstMatchArm* arm = (AstMatchArm*)a;
            if (!arm->test) fallback = cases[i];
            else branch_truth(c, cases[i], equal(c, discriminant, expression(c, arm->test), false).reg);
        }
        jump(c, fallback); i = 0;
        for (AstNode* a = s->cases; a; a = a->next, i++) {
            put_label(c, cases[i]); statements(c, ((AstMatchArm*)a)->body);
        }
        put_label(c, done); c->control = control.parent; mem_free(cases); break;
    }
    case AST_NODE_BREAK_STAM: case AST_NODE_CONTINUE_STAM: {
        AstBreakContinueNode* b = (AstBreakContinueNode*)n;
        bool next = n->node_type == AST_NODE_CONTINUE_STAM;
        LmdControl* control = c->control;
        for (; control; control = control->parent) {
            if (b->label) {
                if (!control->label || control->label_len != b->label_len ||
                    memcmp(control->label, b->label, b->label_len)) continue;
            } else if (control->label && !control->next) continue;
            if (next && !control->next) continue;
            completion_cleanup(c, control);
            release_iterators(c, control);
            jump(c, next ? control->next : control->stop); break;
        }
        if (!control) diagnostic(c->program, n, "invalid control target"); break;
    }
    default:
        if (n->node_type == JS_AST_NODE_LABELED_STATEMENT) {
            JsLabeledStatementNode* l = (JsLabeledStatementNode*)n;
            // the parser represents an empty statement with a null body.
            if (!l->body) break;
            if (l->body->node_type == AST_NODE_LOOP) loop(c, (AstLoopControlNode*)l->body, l->label, l->label_len);
            else if (l->body->node_type == AST_NODE_FOR_OF_STAM || l->body->node_type == AST_NODE_FOR_IN_STAM) for_of(c, (JsForOfNode*)l->body, l->label, l->label_len);
            else {
                MIR_label_t done = label(c);
                AstNode* target = l->body;
                while (target && target->node_type == JS_AST_NODE_LABELED_STATEMENT)
                    target = ((JsLabeledStatementNode*)target)->body;
                LmdControl control = {c->control, done, NULL, l->label, l->label_len,
                    target && target->node_type == AST_NODE_LOOP ? target : NULL}; c->control = &control;
                statement(c, l->body); put_label(c, done); c->control = control.parent;
            }
        } else diagnostic(c->program, n, "statement outside MVP scope");
    }
    c->eval_env = saved_eval; c->eval_scope = saved_scope;
}

struct LmdImport { const char* name; void* address; JitImportMetadata audit; };
#define SCALAR JIT_VALUE_NON_GC_SCALAR
#define ITEM JIT_VALUE_BOXED_ITEM
#define GC_PTR JIT_VALUE_RAW_GC_POINTER
#define RAW_PTR JIT_VALUE_RAW_NON_GC_POINTER
#define ARG(i, cls) JIT_ARG_CLASS(i, cls)
#define AUDIT(gc, cls, args) {gc, JIT_REENTRY_NO, cls, args, \
    JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE) | JIT_IMPORT_NUMBER_STACK_PRESERVES | \
    JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_PRESERVES, 0}
#define REENTRY(gc, cls, args) {gc, JIT_REENTRY_YES, cls, args, \
    JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE) | JIT_IMPORT_NUMBER_STACK_PRESERVES | \
    JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_PRESERVES, 0}
#define COERCION_IMPORT(name, args) \
    {#name, (void*)name, REENTRY(JIT_EFFECT_MAY_GC, ITEM, args)}, \
    {#name "_fixed", (void*)name, AUDIT(JIT_EFFECT_MAY_GC, ITEM, args)}
static const LmdImport imports[] = {
    {"mvp_lmd_execution_interrupt_status", (void*)mvp_lmd_execution_interrupt_status,
        {JIT_EFFECT_MAY_GC, JIT_REENTRY_NO, ITEM, 0,
         JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE) | JIT_IMPORT_NUMBER_STACK_PRESERVES |
         JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_MAY_SET, 0}},
    // the TS-only type() call uses this shared helper in the active MIR engine.
    {"ts_type_info", (void*)ts_type_info, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM))},
    {"lambda_recovery_publish_fault_item", (void*)lambda_recovery_publish_fault_item,
        AUDIT(JIT_EFFECT_NO_GC, ITEM, ARG(0,RAW_PTR)|ARG(1,SCALAR)|ARG(2,SCALAR))},
    {"mvp_lmd_dynamic_environment", (void*)mvp_lmd_dynamic_environment, AUDIT(JIT_EFFECT_MAY_GC, GC_PTR,
        ARG(0,RAW_PTR)|ARG(1,RAW_PTR)|ARG(2,GC_PTR)|ARG(3,SCALAR))},
    {"mvp_lmd_dynamic_initialize_body", (void*)mvp_lmd_dynamic_initialize_body, {JIT_EFFECT_MAY_GC,
        JIT_REENTRY_NO, ITEM, ARG(0,RAW_PTR)|ARG(1,GC_PTR)|ARG(2,GC_PTR),
        JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE) | JIT_IMPORT_ARGS_BORROWED_AUDITED,
        JIT_EXCEPTION_PRESERVES, 0}},
    // interpreter scalar snapshots remain in the caller's number extent until its epilogue.
    {"mvp_lmd_dynamic_eval", (void*)mvp_lmd_dynamic_eval, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, ITEM,
        ARG(0,RAW_PTR)|ARG(1,ITEM)|ARG(2,RAW_PTR), JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE) |
        JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_PRESERVES, 0}},
    {"mvp_lmd_dynamic_reference", (void*)mvp_lmd_dynamic_reference, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, ITEM,
        ARG(0,RAW_PTR)|ARG(1,RAW_PTR)|ARG(2,RAW_PTR)|ARG(3,ITEM)|ARG(4,ITEM)|ARG(5,SCALAR),
        JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE) | JIT_IMPORT_ARGS_BORROWED_AUDITED,
        JIT_EXCEPTION_PRESERVES, 0}},
    {"mvp_lmd_property_with_receiver", (void*)mvp_lmd_property_with_receiver, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,ITEM)|ARG(3,SCALAR)|ARG(4,ITEM))},
    {"mvp_lmd_typed_array_store", (void*)mvp_lmd_typed_array_store, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,SCALAR)|ARG(2,ITEM))},
    {"mvp_lmd_set_prototype", (void*)mvp_lmd_set_prototype, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM))},
    {"mvp_lmd_get_prototype", (void*)mvp_lmd_get_prototype, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM))},
    {"mvp_lmd_define_accessor", (void*)mvp_lmd_define_accessor, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,ITEM)|ARG(3,SCALAR))},
    {"it2d", (void*)it2d, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,ITEM))},
    {"bigint_from_string", (void*)bigint_from_string, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,RAW_PTR)|ARG(1,SCALAR))},
    {"bigint_is_zero", (void*)bigint_is_zero, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,ITEM))},
    {"bigint_to_double", (void*)bigint_to_double, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,ITEM))},
    {"mvp_lmd_bigint_binary", (void*)mvp_lmd_bigint_binary, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,SCALAR))},
    {"mvp_lmd_dynamic_binary", (void*)mvp_lmd_dynamic_binary, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,SCALAR)|ARG(1,ITEM)|ARG(2,ITEM))},
    {"mvp_lmd_bigint_to_string", (void*)mvp_lmd_bigint_to_string, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,SCALAR))},
    {"mvp_lmd_array_flatten", (void*)mvp_lmd_array_flatten, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,SCALAR)|ARG(2,SCALAR)|ARG(3,RAW_PTR))},
    {"mvp_lmd_array_to_string", (void*)mvp_lmd_array_to_string, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,SCALAR), JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE)|JIT_IMPORT_NUMBER_STACK_PRESERVES|JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_PRESERVES, 0}},
    COERCION_IMPORT(mvp_lmd_primitive_to_string, ARG(0,ITEM)|ARG(1,SCALAR)),
    {"mvp_lmd_box_primitive", (void*)mvp_lmd_box_primitive, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM))},
    COERCION_IMPORT(mvp_lmd_primitive_to_number, ARG(0,ITEM)|ARG(1,SCALAR)),
    COERCION_IMPORT(mvp_lmd_to_primitive, ARG(0,ITEM)|ARG(1,SCALAR)|ARG(2,SCALAR)),
    {"mvp_lmd_fail", (void*)mvp_lmd_fail, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,SCALAR)|ARG(1,SCALAR)|ARG(2,RAW_PTR))},
    {"mvp_lmd_string_to_number", (void*)mvp_lmd_string_to_number, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,GC_PTR))},
    {"mvp_lmd_parse_number", (void*)mvp_lmd_parse_number, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,GC_PTR)|ARG(1,SCALAR))},
    {"mvp_lmd_number_to_string", (void*)mvp_lmd_number_to_string, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,SCALAR))},
    {"mvp_lmd_number_to_radix_string", (void*)mvp_lmd_number_to_radix_string, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,SCALAR)|ARG(1,SCALAR))},
    {"mvp_lmd_string_pad", (void*)mvp_lmd_string_pad, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,SCALAR)|ARG(2,ITEM)|ARG(3,SCALAR))},
    {"mvp_lmd_number_to_fixed", (void*)mvp_lmd_number_to_fixed, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,SCALAR)|ARG(1,SCALAR))},
    {"mvp_lmd_parse_integer", (void*)mvp_lmd_parse_integer, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,GC_PTR)|ARG(1,SCALAR))},
    {"mvp_lmd_string_concat", (void*)mvp_lmd_string_concat, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM))},
    {"fn_strcat3", (void*)fn_strcat3, AUDIT(JIT_EFFECT_MAY_GC, GC_PTR, ARG(0,GC_PTR)|ARG(1,GC_PTR)|ARG(2,GC_PTR))},
    {"fn_strcat4", (void*)fn_strcat4, AUDIT(JIT_EFFECT_MAY_GC, GC_PTR, ARG(0,GC_PTR)|ARG(1,GC_PTR)|ARG(2,GC_PTR)|ARG(3,GC_PTR))},
    {"fn_strcat5", (void*)fn_strcat5, AUDIT(JIT_EFFECT_MAY_GC, GC_PTR, ARG(0,GC_PTR)|ARG(1,GC_PTR)|ARG(2,GC_PTR)|ARG(3,GC_PTR)|ARG(4,GC_PTR))},
    {"fn_strcat6", (void*)fn_strcat6, AUDIT(JIT_EFFECT_MAY_GC, GC_PTR, ARG(0,GC_PTR)|ARG(1,GC_PTR)|ARG(2,GC_PTR)|ARG(3,GC_PTR)|ARG(4,GC_PTR)|ARG(5,GC_PTR))},
    {"fn_strcat_many", (void*)fn_strcat_many, AUDIT(JIT_EFFECT_MAY_GC, GC_PTR, ARG(0,SCALAR)|ARG(1,SCALAR))},
    {"fn_string_freeze", (void*)fn_string_freeze, AUDIT(JIT_EFFECT_NO_GC, GC_PTR, ARG(0,GC_PTR))},
    {"mvp_lmd_string_compare", (void*)mvp_lmd_string_compare, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,ITEM)|ARG(1,ITEM))},
    {"js_string_equal", (void*)js_string_equal, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,ITEM)|ARG(1,ITEM))},
    {"js_process_stdout_write", (void*)js_process_stdout_write, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM))},
    {"js_process_stderr_write", (void*)js_process_stderr_write, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM))},
    {"mvp_lmd_string_at", (void*)mvp_lmd_string_at, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,SCALAR)|ARG(2,SCALAR))},
    {"mvp_lmd_string_range", (void*)mvp_lmd_string_range, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,SCALAR)|ARG(2,SCALAR)|ARG(3,SCALAR))},
    {"mvp_lmd_string_search", (void*)mvp_lmd_string_search, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,SCALAR)|ARG(3,SCALAR))},
    {"mvp_lmd_string_split", (void*)mvp_lmd_string_split, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,SCALAR))},
    {"fn_lower", (void*)fn_lower, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM))},
    {"mvp_lmd_regexp_new", (void*)mvp_lmd_regexp_new, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,RAW_PTR)|ARG(1,ITEM)|ARG(2,ITEM))},
    {"mvp_lmd_regexp_string_method", (void*)mvp_lmd_regexp_string_method, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, ITEM,
        ARG(0,RAW_PTR)|ARG(1,ITEM)|ARG(2,ITEM)|ARG(3,ITEM)|ARG(4,SCALAR),
        JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE)|JIT_IMPORT_NUMBER_STACK_PRESERVES|JIT_IMPORT_ARGS_BORROWED_AUDITED,
        JIT_EXCEPTION_PRESERVES, 0}},
    {"fn_upper", (void*)fn_upper, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM))},
    {"mvp_lmd_number_pow", (void*)mvp_lmd_number_pow, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR)|ARG(1,SCALAR))},
    {"mvp_lmd_string_key", (void*)mvp_lmd_string_key, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,GC_PTR)|ARG(1,SCALAR))},
    {"mvp_lmd_array_store", (void*)mvp_lmd_array_store, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,SCALAR)|ARG(2,ITEM))},
    {"mvp_lmd_array_indices_use_own_storage", (void*)mvp_lmd_array_indices_use_own_storage, AUDIT(JIT_EFFECT_MAY_GC, SCALAR, ARG(0,ITEM))},
    {"array_reverse_in_place", (void*)array_reverse_in_place, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,GC_PTR))},
    {"mvp_lmd_array_method_value", (void*)mvp_lmd_array_method_value, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,RAW_PTR)|ARG(1,ITEM)|ARG(2,ITEM)|ARG(3,SCALAR))},
    {"mvp_lmd_property_read", (void*)mvp_lmd_property_read, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,RAW_PTR)|ARG(1,ITEM)|ARG(2,ITEM)|ARG(3,SCALAR))},
    {"mvp_lmd_array_method_value_fixed", (void*)mvp_lmd_array_method_value, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,RAW_PTR)|ARG(1,ITEM)|ARG(2,ITEM)|ARG(3,SCALAR))},
    {"mvp_lmd_array_visit", (void*)mvp_lmd_array_visit, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,RAW_PTR)|ARG(2,SCALAR)|ARG(3,SCALAR))},
    {"mvp_lmd_array_edit", (void*)mvp_lmd_array_edit, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,RAW_PTR)|ARG(2,SCALAR)|ARG(3,SCALAR))},
    {"mvp_lmd_for_in_keys", (void*)mvp_lmd_for_in_keys, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM))},
    {"mvp_lmd_array_sort", (void*)mvp_lmd_array_sort, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, ITEM, ARG(0,ITEM)|ARG(1,ITEM), JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE)|JIT_IMPORT_NUMBER_STACK_PRESERVES|JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_PRESERVES, 0}},
    {"mvp_lmd_array_spread", (void*)mvp_lmd_array_spread, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,SCALAR))},
    {"mvp_lmd_array_new", (void*)mvp_lmd_array_new, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,SCALAR))},
    {"mvp_lmd_array_own_item", (void*)mvp_lmd_array_own_item, AUDIT(JIT_EFFECT_NO_GC, ITEM, ARG(0,GC_PTR)|ARG(1,SCALAR))},
    {"mvp_lmd_is_callable", (void*)mvp_lmd_is_callable, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,ITEM))},
    {"mvp_lmd_array_index_property", (void*)mvp_lmd_array_index_property, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,SCALAR)|ARG(2,ITEM)|ARG(3,SCALAR))},
    {"mvp_lmd_array_resize", (void*)mvp_lmd_array_resize, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,SCALAR))},
    {"fn_slice", (void*)fn_slice, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,ITEM))},
    {"heap_calloc_closure_env", (void*)heap_calloc_closure_env, AUDIT(JIT_EFFECT_MAY_GC, GC_PTR, ARG(0,SCALAR))},
    {"str_repeat", (void*)str_repeat, AUDIT(JIT_EFFECT_MAY_GC, GC_PTR, ARG(0,GC_PTR)|ARG(1,SCALAR))},
    {"fn_join2", (void*)fn_join2, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM))},
    {"mvp_lmd_class_property", (void*)mvp_lmd_class_property, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,ITEM)|ARG(3,SCALAR))},
    {"mvp_lmd_dynamic_global", (void*)mvp_lmd_dynamic_global, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,RAW_PTR)|ARG(1,ITEM)|ARG(2,ITEM)|ARG(3,SCALAR)|ARG(4,SCALAR))},
    // the closed-unit admission proof excludes guest callbacks; storage and C ABI are shared.
    {"mvp_lmd_class_property_fixed", (void*)mvp_lmd_class_property_fixed, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,ITEM)|ARG(3,SCALAR))},
    {"mvp_lmd_class_property_host", (void*)mvp_lmd_class_property_host, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,ITEM)|ARG(3,SCALAR))},
    {"mvp_lmd_private_names_initialize", (void*)mvp_lmd_private_names_initialize, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,RAW_PTR)|ARG(1,RAW_PTR)|ARG(2,SCALAR))},
    {"map_shape_field_to_item", (void*)map_shape_field_to_item, AUDIT(JIT_EFFECT_NO_GC, ITEM, ARG(0,RAW_PTR)|ARG(1,RAW_PTR))},
    {"mvp_lmd_owns_properties", (void*)mvp_lmd_owns_properties, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,ITEM))},
    {"shape_entry_storage", (void*)shape_entry_storage, AUDIT(JIT_EFFECT_NO_GC, RAW_PTR, ARG(0,RAW_PTR))},
    {"typemap_hash_lookup_key", (void*)typemap_hash_lookup_key, AUDIT(JIT_EFFECT_NO_GC, RAW_PTR, ARG(0,RAW_PTR)|ARG(1,GC_PTR))},
    {"map_field_store", (void*)map_field_store, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,RAW_PTR)|ARG(1,ITEM)|ARG(2,SCALAR))},
    {"map_shape_set", (void*)map_shape_set, AUDIT(JIT_EFFECT_MAY_GC, SCALAR, ARG(0,GC_PTR)|ARG(1,GC_PTR)|ARG(2,ITEM))},
    {"map_shape_set_text", (void*)map_shape_set_text, AUDIT(JIT_EFFECT_MAY_GC, SCALAR, ARG(0,GC_PTR)|ARG(1,GC_PTR)|ARG(2,ITEM))},
    {"map_shape_delete", (void*)map_shape_delete, AUDIT(JIT_EFFECT_MAY_GC, SCALAR, ARG(0,GC_PTR)|ARG(1,GC_PTR))},
    {"map_shape_delete_text", (void*)map_shape_delete_text, AUDIT(JIT_EFFECT_MAY_GC, SCALAR, ARG(0,GC_PTR)|ARG(1,GC_PTR))},
    {"mvp_lmd_class_new", (void*)mvp_lmd_class_new, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,RAW_PTR)|ARG(1,ITEM)|ARG(2,ITEM))},
    {"mvp_lmd_object_from_constructor", (void*)mvp_lmd_object_from_constructor,
        REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,RAW_PTR)|ARG(2,SCALAR))},
    {"mvp_lmd_class_initialize_instance", (void*)mvp_lmd_class_initialize_instance, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,SCALAR))},
    {"mvp_lmd_private_element", (void*)mvp_lmd_private_element, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,ITEM)|ARG(3,SCALAR))},
    {"mvp_lmd_template_object", (void*)mvp_lmd_template_object, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,RAW_PTR)|ARG(1,RAW_PTR)|ARG(2,SCALAR))},
    {"mvp_lmd_with_binding", (void*)mvp_lmd_with_binding, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,ITEM)|ARG(3,SCALAR))},
    {"mvp_lmd_function_set_name", (void*)mvp_lmd_function_set_name, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,SCALAR))},
    {"mvp_lmd_class_invoke", (void*)mvp_lmd_class_invoke, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, SCALAR,
        ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,RAW_PTR)|ARG(3,SCALAR)|ARG(4,ITEM),
        JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE)|JIT_IMPORT_NUMBER_STACK_PRESERVES|JIT_IMPORT_ARGS_BORROWED_AUDITED,
        JIT_EXCEPTION_PRESERVES, 0}},
    {"mvp_lmd_function_home", (void*)mvp_lmd_function_home, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,SCALAR))},
    {"mvp_lmd_class_super", (void*)mvp_lmd_class_super, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,SCALAR))},
    {"mvp_lmd_constructor_result", (void*)mvp_lmd_constructor_result, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,SCALAR))},
    {"mvp_lmd_instanceof", (void*)mvp_lmd_instanceof, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,SCALAR))},
    {"mvp_lmd_instanceof_dynamic", (void*)mvp_lmd_instanceof, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,SCALAR))},
    {"mvp_lmd_throw", (void*)mvp_lmd_throw, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,SCALAR))},
    {"mvp_lmd_caught_value", (void*)mvp_lmd_caught_value, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,RAW_PTR)|ARG(1,ITEM))},
    {"js_get_async_iterator", (void*)js_get_async_iterator, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, ITEM, ARG(0,ITEM), JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE)|JIT_IMPORT_NUMBER_STACK_PRESERVES|JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_PRESERVES, 0}},
    {"js_async_iterator_step_result", (void*)js_async_iterator_step_result, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, ITEM, ARG(0,ITEM), JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE)|JIT_IMPORT_NUMBER_STACK_PRESERVES|JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_PRESERVES, 0}},
    {"js_iterator_result_done", (void*)js_iterator_result_done, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, ITEM, ARG(0,ITEM), JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE)|JIT_IMPORT_NUMBER_STACK_PRESERVES|JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_PRESERVES, 0}},
    {"js_gen_park", (void*)js_gen_park, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, ITEM, ARG(0,ITEM)|ARG(1,SCALAR), JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE)|JIT_IMPORT_NUMBER_STACK_PRESERVES|JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_PRESERVES, 0}},
    {"js_gen_is_return_signal", (void*)js_gen_is_return_signal, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, SCALAR, ARG(0,ITEM), JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE)|JIT_IMPORT_NUMBER_STACK_PRESERVES|JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_PRESERVES, 0}},
    {"js_gen_is_throw_signal", (void*)js_gen_is_throw_signal, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, SCALAR, ARG(0,ITEM), JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE)|JIT_IMPORT_NUMBER_STACK_PRESERVES|JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_PRESERVES, 0}},
    {"js_gen_return_signal_value", (void*)js_gen_return_signal_value, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, ITEM, ARG(0,ITEM), JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE)|JIT_IMPORT_NUMBER_STACK_PRESERVES|JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_PRESERVES, 0}},
    {"js_gen_throw_signal_value", (void*)js_gen_throw_signal_value, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, ITEM, ARG(0,ITEM), JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE)|JIT_IMPORT_NUMBER_STACK_PRESERVES|JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_PRESERVES, 0}},
    {"mvp_lmd_iterator_close_mode", (void*)mvp_lmd_iterator_close_mode, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,SCALAR), JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE)|JIT_IMPORT_NUMBER_STACK_PRESERVES|JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_PRESERVES, 0}},
    {"mvp_lmd_generator_park", (void*)mvp_lmd_generator_park, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, ITEM, ARG(0,ITEM)|ARG(1,SCALAR), JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE)|JIT_IMPORT_NUMBER_STACK_PRESERVES|JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_PRESERVES, 0}},
    {"js_await_park", (void*)js_await_park, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, ITEM, ARG(0,ITEM), JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE)|JIT_IMPORT_NUMBER_STACK_PRESERVES|JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_PRESERVES, 0}},
    {"mvp_lmd_dynamic_import_value", (void*)mvp_lmd_dynamic_import_value, AUDIT(JIT_EFFECT_MAY_GC, ITEM,
        ARG(0,RAW_PTR)|ARG(1,ITEM)|ARG(2,SCALAR))},
    {"mvp_lmd_generator_resume_kind", (void*)mvp_lmd_generator_resume_kind, AUDIT(JIT_EFFECT_NO_GC, SCALAR, 0)},
    {"mvp_lmd_iterator_get", (void*)mvp_lmd_iterator_get, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, ITEM, ARG(0,RAW_PTR)|ARG(1,ITEM), JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE)|JIT_IMPORT_NUMBER_STACK_PRESERVES|JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_PRESERVES, 0}},
    {"mvp_lmd_iterator_step", (void*)mvp_lmd_iterator_step, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,SCALAR), JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE)|JIT_IMPORT_NUMBER_STACK_PRESERVES|JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_PRESERVES, 0}},
    {"mvp_lmd_iterator_close", (void*)mvp_lmd_iterator_close, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, ITEM, ARG(0,ITEM)|ARG(1,ITEM), JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE)|JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_PRESERVES, 0}},
    {"mvp_lmd_iterator_collect", (void*)mvp_lmd_iterator_collect, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, ITEM, ARG(0,RAW_PTR)|ARG(1,ITEM)|ARG(2,ITEM)|ARG(3,ITEM)|ARG(4,ITEM), JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE)|JIT_IMPORT_NUMBER_STACK_PRESERVES|JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_PRESERVES, 0}},
    {"mvp_lmd_call_array", (void*)mvp_lmd_call_array, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,ITEM)|ARG(3,ITEM), JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_F64)|JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_PRESERVES, 0}},
    {"mvp_lmd_function_new", (void*)mvp_lmd_function_new, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,SCALAR)|ARG(1,RAW_PTR))},
    {"mvp_lmd_arguments_new", (void*)mvp_lmd_arguments_new, AUDIT(JIT_EFFECT_MAY_GC, ITEM,
        ARG(0,ITEM)|ARG(1,RAW_PTR)|ARG(2,SCALAR)|ARG(3,ITEM)|ARG(4,SCALAR))},
    COERCION_IMPORT(mvp_lmd_property_key, ARG(0,ITEM)),
    {"mvp_lmd_object_new", (void*)mvp_lmd_object_new, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,RAW_PTR)|ARG(1,SCALAR))},
    {"mvp_lmd_property_get", (void*)mvp_lmd_property_get, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,SCALAR))},
    {"mvp_lmd_property_get_fixed", (void*)mvp_lmd_property_get, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,SCALAR))},
    {"mvp_lmd_property_set", (void*)mvp_lmd_property_set, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,ITEM))},
    {"mvp_lmd_property_set_fixed", (void*)mvp_lmd_property_set, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,ITEM))},
    {"mvp_lmd_property_delete", (void*)mvp_lmd_property_delete, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM))},
    {"mvp_lmd_property_has", (void*)mvp_lmd_property_has, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,SCALAR))},
    {"mvp_lmd_object_project", (void*)mvp_lmd_object_project, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,SCALAR))},
    {"mvp_lmd_object_project_fixed", (void*)mvp_lmd_object_project, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,SCALAR))},
    {"mvp_lmd_object_copy", (void*)mvp_lmd_object_copy, REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,SCALAR)|ARG(3,ITEM))},
    {"mvp_lmd_named_set", (void*)static_cast<Item (*)(Item, const char*, Item, int)>(mvp_lmd_named_set), AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,RAW_PTR)|ARG(2,ITEM)|ARG(3,SCALAR))},
    {"mvp_lmd_map_call", (void*)mvp_lmd_map_call, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,ITEM)|ARG(3,ITEM))},
    {"mvp_lmd_same_value_zero", (void*)mvp_lmd_same_value_zero, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,ITEM)|ARG(1,ITEM))},
    {"mvp_lmd_map_next", (void*)mvp_lmd_map_next, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,ITEM)|ARG(1,SCALAR))},
    {"mvp_lmd_map_entry", (void*)mvp_lmd_map_entry, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,SCALAR)|ARG(2,SCALAR))},
    {"array", (void*)array, AUDIT(JIT_EFFECT_MAY_GC, GC_PTR, 0)},
    {"array_num_new", (void*)array_num_new, AUDIT(JIT_EFFECT_MAY_GC, GC_PTR, ARG(0,SCALAR)|ARG(1,SCALAR))},
    {"array_reserve_append_slots", (void*)array_reserve_append_slots, AUDIT(JIT_EFFECT_MAY_GC, SCALAR, ARG(0,GC_PTR)|ARG(1,SCALAR))},
    {"array_copy_owned_items", (void*)array_copy_owned_items, AUDIT(JIT_EFFECT_NO_GC, SCALAR,
        ARG(0,GC_PTR)|ARG(1,SCALAR)|ARG(2,RAW_PTR)|ARG(3,SCALAR))},
    {"js_module_binding_get", (void*)js_module_binding_get, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,RAW_PTR))},
    {"js_interp_capture_read", (void*)js_interp_capture_read,
        AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,RAW_PTR)|ARG(2,RAW_PTR))},
    {"js_interp_publish_export_bindings", (void*)js_interp_publish_export_bindings,
        AUDIT(JIT_EFFECT_MAY_GC, SCALAR, ARG(0,RAW_PTR)|ARG(1,RAW_PTR)|ARG(2,ITEM))},
    {"js_set_global_property_after_with_lookup", (void*)js_set_global_property_after_with_lookup,
        REENTRY(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,SCALAR))},
    {"js_module_import_meta", (void*)js_module_import_meta, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,RAW_PTR))},
    {"mvp_lmd_dynamic_import", (void*)mvp_lmd_dynamic_import, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, ITEM,
        ARG(0,RAW_PTR)|ARG(1,ITEM)|ARG(2,RAW_PTR), JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE)|JIT_IMPORT_NUMBER_STACK_PRESERVES|
        JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_PRESERVES, 0}},
    {"lambda_module_var_at", (void*)lambda_module_var_at, AUDIT(JIT_EFFECT_NO_GC, ITEM, ARG(0,RAW_PTR)|ARG(1,SCALAR))},
    {"lambda_module_var_store", (void*)lambda_module_var_store, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,RAW_PTR)|ARG(1,SCALAR)|ARG(2,ITEM))},
    {"owned_item_slot_store", (void*)owned_item_slot_store, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,RAW_PTR)|ARG(1,SCALAR)|ARG(2,SCALAR)|ARG(3,ITEM))},
    {"lambda_item_adopt_scalar_home", (void*)lambda_item_adopt_scalar_home, AUDIT(JIT_EFFECT_NO_GC, ITEM, ARG(0,ITEM)|ARG(1,RAW_PTR))},
    {"js_string_utf16_length", (void*)js_string_utf16_length, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,GC_PTR))},
    {"fmod", (void*)(double (*)(double, double))fmod, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR)|ARG(1,SCALAR))},
    {"sqrt", (void*)(double (*)(double))sqrt, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR))},
    {"sin", (void*)(double (*)(double))sin, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR))},
    {"cos", (void*)(double (*)(double))cos, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR))},
    {"math_splitmix64", (void*)math_splitmix64, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,RAW_PTR))},
    {"floor", (void*)(double (*)(double))floor, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR))},
    {"ceil", (void*)(double (*)(double))ceil, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR))},
    {"trunc", (void*)(double (*)(double))trunc, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR))},
    {"copysign", (void*)(double (*)(double, double))copysign, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR)|ARG(1,SCALAR))},
    {"fn_abs_f", (void*)fn_abs_f, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR))},
    {"fn_min2_u", (void*)fn_min2_u, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR)|ARG(1,SCALAR))},
    {"fn_max2_u", (void*)fn_max2_u, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR)|ARG(1,SCALAR))},

};
#undef COERCION_IMPORT
static bool import_metadata(const char* name, JitImportMetadata* out) {
    for (const LmdImport& entry : imports) if (!strcmp(name, entry.name)) { *out = entry.audit; return true; }
    if (!strcmp(name, "lambda_side_stack_ensure_for") || !strcmp(name, "lambda_stack_is_exhausted"))
        return jit_import_get_metadata(name, out);
    log_error("mvp-lmd-import: rejected import %s", name); abort();
}
static void* import_resolver(const char* name) {
    for (const LmdImport& entry : imports) if (!strcmp(name, entry.name)) return entry.address;
    for (int i = 0; i < jit_runtime_import_count; i++)
        if (!strcmp(name, jit_runtime_imports[i].name) &&
            (!strcmp(name, "lambda_side_stack_ensure_for") || !strcmp(name, "lambda_stack_is_exhausted")))
            return (void*)jit_runtime_imports[i].func;
    log_error("mvp-lmd-link: rejected import %s", name); return NULL;
}

static const MIR_var_t entry_arguments[] = {{MIR_T_P, "runtime", 0}, {MIR_T_P, "unit", 0},
    {MIR_T_P, "arguments", 0}, {MIR_T_I64, "argc", 0}, {MIR_T_I64, "self", 0},
    {MIR_T_I64, "receiver", 0}, {MIR_T_I64, "new_target", 0}};
static void emit_return(LmdCompiler* c, bool unentered = false) {
    MirFrameState* frame = &c->em.frame;
    MIR_reg_t result, companion;
    if (unentered) {
        // the reserved fault carrier requires no allocation or entered root frame.
        MIR_reg_t error = em_call_3(&c->em, "lambda_recovery_publish_fault_item", MIR_T_I64,
            MIR_T_P, reg(c, frame->runtime), MIR_T_I64, integer(c, LAMBDA_FAULT_STACK_OVERFLOW),
            MIR_T_I64, integer(c, ERR_OK), false);
        result = c->function->native ? (frame->return_type == MIR_T_D
            ? to_number(c, number(c, 0)) : constant(c, 0)) : error;
        companion = c->function->native ? error : constant(c, 0);
    } else if (c->function->native) {
        result = frame->return_reg; companion = frame->error_return_reg;
    } else if (fn_return_shape_may_be_pending(frame->plan.return_shape))
        em_build_pending_pair(&c->em, frame->return_reg, &result, &companion);
    else { result = frame->return_reg; companion = constant(c, 0); }
    bool pair = em_returns_result_pair(frame->plan.companion);
    if (em_returns_companion_slot(frame->plan.companion))
        em_store_frame_top(&c->em, frame->runtime, offsetof(Context, mir_companion_slot), companion);
    if (!unentered) {
        if (c->caller_file)
            em_store_at(&c->em, frame->runtime, c->caller_file_offset, MIR_T_I64, c->caller_file);
        em_store_frame_top(&c->em, frame->runtime, offsetof(Context, side_root_top), frame->root_base);
        // audited MVP imports preserve the watermark; only owned scalar storage needs an extent.
        if (frame->scalar_home_count || frame->fixed_number_slots || frame->number_frame_required)
            em_store_frame_top(&c->em, frame->runtime, offsetof(Context, side_number_top), frame->number_base);
    }
    em_emit_insn(&c->em, pair ? MIR_new_ret_insn(c->em.ctx, 2, reg(c, result), reg(c, companion))
        : MIR_new_ret_insn(c->em.ctx, 1, reg(c, result)));
}
static bool compile_function(MvpLmdProgram* p, LmdFunction* f, uint32_t* instruction_count) {
    LmdCompiler c = {}; c.program = p; c.function = f;
    c.em.ctx = p->mir; c.em.call_owner = &c; c.em.root_call_value = root_value;
    c.em.lookup_import_metadata = import_metadata; c.em.import_cache = p->import_cache;
    c.em.helper_results_skip_rehome = true;
    c.em.name_pool = p->frontend->name_pool;
    MIR_type_t result_types[] = {f->native && f->returns == K_NUMBER && !f->integer ? MIR_T_D : MIR_T_I64, MIR_T_I64};
    // the MIR API interns descriptor names; keep the reusable template immutable.
    MIR_var_t arguments[LAMBDA_MAX_FUNCTION_ARGS]; memcpy(arguments, f->signature, f->arity * sizeof(MIR_var_t));
    f->item = MIR_new_func_arr(p->mir, f->name, em_return_nres(f->return_abi.companion), result_types, f->arity, arguments);
    c.em.func_item = f->item; c.em.func = MIR_get_item_func(p->mir, f->item);
    MirFrameState* frame = &c.em.frame;
    frame->active = true; frame->number_active = true; frame->item_return = !f->native;
    frame->return_type = result_types[0]; frame->scalar_return_mode = f->return_abi.normal.scalar_class;
    frame->return_lane_kind = f->native ? RETURN_LANE_ERROR : RETURN_LANE_SCALAR;
    frame->runtime = MIR_reg(p->mir, "runtime", c.em.func);
    c.unit = MIR_reg(p->mir, "unit", c.em.func);
    if (!f->direct_args) {
        c.arguments = MIR_reg(p->mir, "arguments", c.em.func);
        c.argc = MIR_reg(p->mir, "argc", c.em.func);
        c.self = MIR_reg(p->mir, "self", c.em.func);
        if (p->receiver_abi) {
            c.receiver = MIR_reg(p->mir, "receiver", c.em.func);
            c.new_target = MIR_reg(p->mir, "new_target", c.em.func);
            root_value(&c, c.receiver, JIT_VALUE_BOXED_ITEM);
            root_value(&c, c.new_target, JIT_VALUE_BOXED_ITEM);
        }
    }
    for (int i = 0; i < f->arity; i++)
        em_function_argument_register(&c.em, MIR_reg(p->mir, f->signature[i].name, c.em.func));
    frame->root_base = em_new_reg(&c.em, "root_frame", MIR_T_I64);
    frame->root_end = em_new_reg(&c.em, "root_end", MIR_T_I64);
    frame->number_base = em_new_reg(&c.em, "number_frame", MIR_T_I64);
    frame->return_reg = em_new_reg(&c.em, "completion", frame->return_type);
    if (f->native) frame->error_return_reg = em_new_reg(&c.em, "error", MIR_T_I64);
    else root_value(&c, frame->return_reg, JIT_VALUE_BOXED_ITEM);
    frame->anchor = label(&c); frame->return_label = label(&c);
    frame->native_stack_overflow = label(&c);
    frame->plan.entry_kind = f->native ? FN_ENTRY_NATIVE_BODY : FN_ENTRY_BOXED_BODY;
    frame->plan.entry_mode = f->native ? MIR_ENTRY_BOUND_INTERNAL : MIR_ENTRY_CHECKED;
    frame->plan.debug_name = f->name;
    em_plan_bind_return(&frame->plan, &f->return_abi, !f->native);
    put_label(&c, frame->anchor);
    if (p->dynamic_source && p->frontend->reference) {
        // measure the inherited layout from the Context pointer passed to generated code.
        EvalContext marker;
        c.caller_file_offset = (size_t)((char*)&marker.current_file - (char*)(Context*)&marker);
        // escaped module callbacks create dynamic code in their defining source.
        c.caller_file = em_load_at(&c.em, frame->runtime, c.caller_file_offset, MIR_T_I64, "caller_file");
        em_store_at(&c.em, frame->runtime, c.caller_file_offset, MIR_T_I64,
            relocated_constant(&c, (uint64_t)p->frontend->reference));
    }
    if (!f->native) move(&c, frame->return_reg, integer(&c, ITEM_JS_UNDEFINED));
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        b->scalar_home = 0;
        if (b->owner == f && b->captured) {
            // a captured binding's existing register owns the cell, without a second unused value register.
            b->reg = em_new_reg(&c.em, "binding_cell", MIR_T_I64);
            root_value(&c, b->reg, JIT_VALUE_BOXED_ITEM);
            continue;
        }
        if (b->owner != f || (!f->id && !b->native)) continue;
        b->reg = em_new_reg(&c.em, "binding", b->native && (b->kinds & K_NUMBER) && !b->integer ? MIR_T_D : MIR_T_I64);
        b->number_present = b->native && b->kinds == (K_NUMBER | K_UNDEFINED)
            ? em_new_reg(&c.em, "binding_present", MIR_T_I64) : 0;
        if (!b->native) root_value(&c, b->reg, JIT_VALUE_BOXED_ITEM);
    }
    if (f->eval_capture) {
        MIR_reg_t capture = em_load_at(&c.em, c.self, offsetof(Function, closure_env), MIR_T_I64, "eval_capture");
        int slot = function_capture_count(f, f->object_home || f->lexical_super ||
            (f->home && f->id != f->home->constructor_id));
        c.eval_env = em_load_at(&c.em, capture, slot * sizeof(Item), MIR_T_I64, "retained_eval_environment");
        root_value(&c, c.eval_env, JIT_VALUE_RAW_GC_POINTER);
    }
    if (f->ast) {
        c.strict = f->ast->has_use_strict_directive || (f->ast->vars && f->ast->vars->strict);
        if (f->observes_receiver && !f->home && f->ast->node_type != AST_NODE_ARROW_FUNC &&
                !f->ast->has_use_strict_directive && !(f->ast->vars && f->ast->vars->strict)) {
            MIR_label_t ready = label(&c), global = label(&c), object = label(&c);
            branch(&c, MIR_BEQ, global, reg(&c, c.receiver), integer(&c, ITEM_JS_UNDEFINED));
            branch(&c, MIR_BNE, object, reg(&c, c.receiver), integer(&c, ITEM_NULL));
            put_label(&c, global);
            MIR_reg_t slots = em_load_at(&c.em, c.unit, offsetof(MvpLmdProgram, slots), MIR_T_I64, "program_slots");
            move(&c, c.receiver, reg(&c, em_load_at(&c.em, slots, p->global_slot * sizeof(Item), MIR_T_I64, "global_receiver")));
            jump(&c, ready); put_label(&c, object);
            // shared runtime callbacks enter here too; sloppy receivers need ToObject.
            MIR_reg_t receiver_type = tag(&c, boxed(&c, c.receiver));
            const TypeId object_types[] = {LMD_TYPE_MAP, LMD_TYPE_ARRAY, LMD_TYPE_ARRAY_NUM, LMD_TYPE_FUNC, LMD_TYPE_ERROR};
            for (TypeId type : object_types)
                branch(&c, MIR_BEQ, ready, reg(&c, receiver_type), integer(&c, type));
            branch_virtual_object(&c, receiver_type, ready);
            if (p->primitives) {
                MIR_reg_t wrapped = em_call_2(&c.em, "mvp_lmd_box_primitive", MIR_T_I64,
                    MIR_T_I64, reg(&c, c.receiver), MIR_T_I64, integer(&c, ITEM_JS_UNDEFINED), true);
                check_error(&c, wrapped); move(&c, c.receiver, reg(&c, wrapped));
            } else fail(&c, LMD_MVP_CAPABILITY, (AstNode*)f->ast);
            put_label(&c, ready);
        }
        if (f->lexical_receiver) {
            MIR_reg_t env = em_load_at(&c.em, c.self, offsetof(Function, closure_env), MIR_T_I64, "closure_env");
            c.receiver_cell = em_load_at(&c.em, env, 0, MIR_T_I64, "receiver_cell");
            root_value(&c, c.receiver_cell, JIT_VALUE_BOXED_ITEM);
        } else if (f->receiver_captured) {
            c.receiver_cell = em_call_1(&c.em, "mvp_lmd_array_new", MIR_T_I64, MIR_T_I64, integer(&c, 2), true);
            check_error(&c, c.receiver_cell); boxed(&c, c.receiver_cell, K_ARRAY);
            array_index_write(&c, {NULL, boxed(&c, c.receiver_cell, K_ARRAY), constant(&c, 0)}, boxed(&c, c.receiver), true);
            array_index_write(&c, {NULL, boxed(&c, c.receiver_cell, K_ARRAY), constant(&c, 1)}, boxed(&c, c.new_target), true);
        }
        NameScope* names = f->ast->vars && f->ast->vars->parent && f->ast->vars->parent->is_function_name_scope
            ? f->ast->vars->parent : NULL;
        // default-parameter closures must capture the initialized immutable self binding.
        scope_initialize(&c, names);
        for (int parameter = 0; parameter < p->bindings->length; parameter++) {
            LmdBinding* b = (LmdBinding*)p->bindings->data[parameter];
            if (b->owner != f || !b->entry->is_parameter) continue;
            if (b->captured) initialize_capture(&c, b, false);
            if (f->complex_params) write_binding(&c, b, boxed(&c, constant(&c, ITEM_JS_TDZ)), b->entry->node, true);
        }
        if (f->arguments_binding) {
            LmdValue cells = boxed(&c, constant(&c, ITEM_NULL), K_NULL);
            if (f->mapped_arguments) {
                MIR_reg_t table = em_call_1(&c.em, "mvp_lmd_array_new", MIR_T_I64, MIR_T_I64,
                    integer(&c, ast_linked_node_count(f->ast->params)), true);
                check_error(&c, table); cells = boxed(&c, table, K_ARRAY);
                int index = 0;
                for (AstNode* a = f->ast->params; a; a = a->next, index++) {
                    NameEntry* entry = js_ast_parameter_binding_identifier(a)->entry;
                    bool last = true;
                    for (AstNode* next = a->next; next; next = next->next)
                        if (js_ast_parameter_binding_identifier(next)->entry == entry) last = false;
                    if (last) array_index_write(&c, {NULL, cells, constant(&c, index)},
                        boxed(&c, binding_cell(&c, binding(p, entry)), K_ARRAY), true);
                }
            }
            LmdBinding* b = f->arguments_binding;
            if (b->captured) initialize_capture(&c, b, false);
            MIR_reg_t object = em_call_5(&c.em, "mvp_lmd_arguments_new", MIR_T_I64,
                MIR_T_I64, reg(&c, c.self), MIR_T_P, reg(&c, c.arguments), MIR_T_I64, reg(&c, c.argc),
                MIR_T_I64, reg(&c, cells.reg), MIR_T_I64, integer(&c, !f->mapped_arguments), true);
            check_error(&c, object);
            write_binding(&c, b, boxed(&c, object, K_OBJECT), (AstNode*)f->ast, true);
        }
        MIR_reg_t parameter_env = 0;
        if (f->eval_environment && f->parameter_expressions) {
            eval_scope_enter(&c, f->ast->vars, false, JS_INTERP_ENV_PARAMETERS);
            parameter_env = c.eval_env;
        }
        c.parameter_initializer = f->eval_environment;
        uint64_t i = 0;
        for (AstNode* a = f->ast->params; a; a = a->next, i++) {
            AstNode* pattern = js_ast_parameter_pattern(a);
            AstIdentNode* id = js_ast_parameter_binding_identifier(a);
            LmdBinding* b = id ? binding(p, id->entry) : NULL;
            if (pattern->node_type == AST_NODE_REST_ELEMENT || pattern->node_type == AST_NODE_SPREAD) {
                MIR_reg_t rest = em_call_1(&c.em, "mvp_lmd_array_new", MIR_T_I64, MIR_T_I64, integer(&c, 0), true);
                check_error(&c, rest); LmdValue array = boxed(&c, rest, K_ARRAY);
                MIR_reg_t index = constant(&c, i);
                MIR_label_t start = label(&c), done = label(&c);
                put_label(&c, start); branch(&c, MIR_UBGE, done, reg(&c, index), reg(&c, c.argc));
                MIR_reg_t address = op(&c, MIR_ADD, reg(&c, c.arguments),
                    reg(&c, op(&c, MIR_MUL, reg(&c, index), integer(&c, sizeof(Item)))));
                MIR_reg_t value = em_load_at(&c.em, address, 0, MIR_T_I64, "rest_argument");
                array_index_write(&c, {NULL, array, op(&c, MIR_SUB, reg(&c, index), integer(&c, i))}, boxed(&c, value), true);
                move(&c, index, reg(&c, op(&c, MIR_ADD, reg(&c, index), integer(&c, 1)))); jump(&c, start);
                put_label(&c, done);
                if (b) write_binding(&c, b, array, a, true);
                else bind_pattern(&c, ((JsSpreadElementNode*)pattern)->argument, array, true);
                continue;
            }
            if (f->direct_args) {
                MIR_reg_t value = MIR_reg(p->mir, f->signature[i + 2].name, c.em.func);
                write_binding(&c, b, b->native ? LmdValue{value, b->kinds, false, 0, b->integer, b->range}
                    : boxed(&c, value, b->kinds), a, true);
                continue;
            }
            MIR_reg_t value = em_new_reg(&c.em, "parameter", MIR_T_I64);
            if (f->closed_calls && !(b->kinds & K_UNDEFINED)) {
                move(&c, value, reg(&c, em_load_at(&c.em, c.arguments, i * sizeof(Item), MIR_T_I64, "argument")));
            } else {
                MIR_label_t absent = label(&c), done = label(&c);
                branch(&c, MIR_UBLE, absent, reg(&c, c.argc), integer(&c, i));
                move(&c, value, reg(&c, em_load_at(&c.em, c.arguments, i * sizeof(Item), MIR_T_I64, "argument")));
                jump(&c, done); put_label(&c, absent); move(&c, value, integer(&c, ITEM_JS_UNDEFINED));
                put_label(&c, done);
            }
            if (js_ast_parameter_default_value(a)) {
                bind_pattern(&c, a, boxed(&c, value), true); continue;
            }
            if (b) write_binding(&c, b, boxed(&c, value, b->kinds), a, true);
            else bind_pattern(&c, a, boxed(&c, value), true);
        }
        c.parameter_initializer = false;
        if (f->ast->is_generator) {
            MIR_reg_t parked = em_call_2(&c.em, f->ast->is_async ? "js_gen_park" : "mvp_lmd_generator_park", MIR_T_I64,
                MIR_T_I64, integer(&c, ITEM_JS_UNDEFINED), MIR_T_I64, integer(&c, f->ast->is_async ? 0 : 1), true);
            check_error(&c, parked);
        }
        AstBlockNode* body = f->ast->body->node_type == AST_NODE_BLOCK ? (AstBlockNode*)f->ast->body : NULL;
        // hoisted declarations can close over body lexicals before their initialization.
        scope_capture_initialize(&c, f->ast->vars, parameter_env != 0);
        if (body && body->vars != f->ast->vars) scope_capture_initialize(&c, body->vars);
        eval_scope_enter(&c, f->ast->vars, false,
            parameter_env ? JS_INTERP_ENV_BODY_VARS : JS_INTERP_ENV_ORDINARY);
        if (parameter_env) {
            MIR_reg_t initialized = em_call_3(&c.em, "mvp_lmd_dynamic_initialize_body", MIR_T_I64,
                MIR_T_P, reg(&c, c.unit), MIR_T_P, reg(&c, c.eval_env), MIR_T_P, reg(&c, parameter_env), true);
            check_error(&c, initialized);
        }
        if (body) eval_scope_enter(&c, body->vars);
        scope_initialize(&c, f->ast->vars, false);
        if (body) scope_initialize(&c, body->vars, false);
        if (f->numeric_region && (f->numeric_region->loops ||
                (f->numeric_region->count >= 2 &&
                 (f->numeric_region->nodes >= 64 || f->numeric_region->allocates)))) {
            MIR_label_t generic = label(&c);
            LmdInlineFrame region = {}; region.function = f; region.region = f->numeric_region;
            region.self = c.self; region.receiver = c.receiver;
            LmdValue values[8]; int count = 0;
            for (AstNode* param = f->ast->params; param; param = param->next)
                values[count++] = read_binding(&c, binding(p, js_ast_parameter_binding_identifier(param)->entry), param);
            guard_numeric_region(&c, region, values, count, generic);
            LmdInlineBody fast = {body ? NULL : f->ast->body, body ? body->statements : NULL, NULL, body != NULL};
            bool guarded = false;
            return_value(&c, inline_call_body(&c, region, fast, NULL, values, count, generic, &guarded));
            put_label(&c, generic);
        }
        if (f->ast->body->node_type == AST_NODE_BLOCK) {
            statements(&c, body->statements);
            if (falls_through(f->ast->body)) return_value(&c, expression(&c, NULL));
        } else return_value(&c, expression(&c, f->ast->body));
    } else {
        c.strict = p->source_module || ((AstScript*)p->frontend->ast_root)->has_use_strict_directive;
        if (f->eval_environment && f->receiver_captured) {
            c.receiver_cell = em_call_1(&c.em, "mvp_lmd_array_new", MIR_T_I64, MIR_T_I64, integer(&c, 2), true);
            check_error(&c, c.receiver_cell); boxed(&c, c.receiver_cell, K_ARRAY);
            MIR_reg_t receiver = p->source_module ? constant(&c, ITEM_JS_UNDEFINED) :
                em_load_at(&c.em, module_slots(&c), p->global_slot * sizeof(Item), MIR_T_I64, "global_lexical_this");
            array_index_write(&c, {NULL, boxed(&c, c.receiver_cell, K_ARRAY), constant(&c, 0)}, boxed(&c, receiver), true);
            array_index_write(&c, {NULL, boxed(&c, c.receiver_cell, K_ARRAY), constant(&c, 1)}, boxed(&c, constant(&c, ITEM_JS_UNDEFINED)), true);
        }
        if (p->source_module) {
            MIR_label_t body = label(&c);
            branch(&c, MIR_BEQ, body, reg(&c, c.argc), integer(&c, 0));
            scope_initialize(&c, ((AstScript*)p->frontend->ast_root)->global_vars);
            // instantiation and evaluation enter separately but retain the same module record.
            if (f->eval_environment)
                em_store_at(&c.em, module_slots(&c), p->module_eval_slot * sizeof(Item), MIR_T_I64, c.eval_env);
            LmdBinding* default_binding = binding(p, &p->source_module->default_entry);
            if (default_binding) {
                AstNode* declaration = p->source_module->default_declaration;
                LmdFunction* function = declaration && declaration->node_type == AST_NODE_FUNC ? ::function(p, declaration) : NULL;
                write_binding(&c, default_binding, function ? new_function(&c, function) :
                    boxed(&c, constant(&c, ITEM_JS_TDZ)), declaration, true);
            }
            jump(&c, frame->return_label); put_label(&c, body);
            if (f->eval_environment)
                move(&c, c.eval_env, reg(&c, em_load_at(&c.em, module_slots(&c),
                    p->module_eval_slot * sizeof(Item), MIR_T_I64, "module_eval_environment")));
            statements(&c, ((AstScript*)p->frontend->ast_root)->body);
            move(&c, frame->return_reg, integer(&c, ITEM_JS_UNDEFINED));
        } else statement(&c, p->frontend->ast_root);
        jump(&c, frame->return_label);
    }
    put_label(&c, frame->return_label);
    emit_return(&c);
    frame->fixed_root_slots = c.argument_count;
    MirRootWriteBackResult roots = {};
    if (!em_finalize_semantic_root_write_back(&c.em, frame->root_base, frame->anchor, false, 0,
        &frame->gc_candidates, &frame->gc_candidate_count, &frame->gc_candidate_capacity,
        &frame->gc_candidate_by_reg, &frame->gc_candidate_by_reg_capacity,
        frame->gc_call_sites, frame->gc_call_site_count, &roots, f->name)) {
        diagnostic(p, (AstNode*)f->ast, "root planning failed");
    }
    frame->root_slot_count = roots.stable_slots + roots.scratch_slots;
    frame->root_store_count = roots.inserted_stores;
    if (c.argument_fixup) c.argument_fixup->ops[2].u.i = (int64_t)frame->root_slot_count * sizeof(Item);
    frame->root_slot_count += c.argument_count;
    em_finalize_scalar_homes(&c.em);
    em_finalize_frame_prologue(&c.em, frame->plan.entry_mode,
        offsetof(Context, side_root_top), offsetof(Context, side_root_limit),
        offsetof(Context, side_number_top), offsetof(Context, side_number_limit),
        offsetof(Context, side_root_commit_limit), offsetof(Context, side_number_commit_limit));
    // reservation/stack failure has no frame to unwind and allocates nothing.
    emit_return(&c, true);
    em_finalize_function_metadata(&c.em);
    *instruction_count = c.em.last_function.mir_instruction_count;
    if (p->frontend->pass_timing.enabled) {
        // correlate the shared MIR telemetry/address map with immutable source spans.
        String* name = f->ast ? f->ast->name : NULL;
        log_notice("js-mvp-function: function=%s name=%.*s start=%u end=%u instructions=%u roots=%u homes=%u",
            f->name, name ? (int)name->len : 0, name ? name->chars : "",
            f->ast ? f->ast->source_span.start_byte : 0, f->ast ? f->ast->source_span.end_byte : 0,
            c.em.last_function.mir_instruction_count, c.em.last_function.root_slot_count,
            c.em.last_function.scalar_home_count);
    }
    MIR_finish_func(p->mir);
    em_frame_dispose(&c.em);
    return !p->diagnostic[0];
}
static bool plan_shape_field(MvpLmdProgram*, TypeMap*, String*, TypeId, uint8_t);
static bool plan_callable_shapes(MvpLmdProgram* p, MvpLmdClass* cls, int count,
        uint8_t metadata_flags, uint8_t prototype_flags, TypeId prototype_type = LMD_TYPE_MAP) {
    const char* names[] = {"length", "name", "prototype", "constructor"};
    const TypeId types[] = {LMD_TYPE_INT, LMD_TYPE_STRING, prototype_type, LMD_TYPE_FUNC};
    for (int i = 0; i < count; i++) {
        uint8_t flags = JSPD_NON_ENUMERABLE | (i < 2 ? metadata_flags : i == 2 ? prototype_flags : 0);
        if (!plan_shape_field(p, i == 3 ? &cls->prototype_shape : &cls->static_shape,
                name_pool_create_len(p->frontend->name_pool, names[i], strlen(names[i])), types[i], flags))
            return false;
    }
    return true;
}
Item mvp_lmd_function_source(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item receiver, Item) {
    if (!mvp_lmd_is_callable(receiver)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    Item dynamic = mvp_lmd_dynamic_function_source(receiver);
    if (dynamic.item != ITEM_JS_UNDEFINED) return dynamic;
    if (!mvp_lmd_is_proxy(receiver)) {
        MvpLmdCallable* function = (MvpLmdCallable*)receiver.function;
        AstNode* definition = function->constructor && function->home && function->home->ast ?
            (AstNode*)function->home->ast : (AstNode*)function->def;
        if (definition) {
            JsTranspiler* source = function->program->frontend;
            SourceSpan span = definition->source_span;
            if (span.end_byte > source->source_length || span.end_byte < span.start_byte)
                return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
            const char* text = source->source + span.start_byte;
            uint32_t length = span.end_byte - span.start_byte;
            // shared method spans omit the class-only static prefix and its trivia.
            // class constructors retain their complete ClassExpression/Declaration source.
            if (definition->node_type != AST_NODE_CLASS && definition->node_type != AST_NODE_CLASS_EXPR)
                js_function_source_span(source->source, source->source_length,
                    (const AstFuncNode*)definition, &text, &length);
            String* result = heap_strcpy(text, length);
            return result ? Item{.item = s2it(result)} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        }
        if (function->native_source && function->ptr != (fn_ptr)mvp_lmd_function_bound)
            return js_named_native_function_source_item(((MvpLmdNativeCallable*)function)->source_name);
    }
    // bound and proxy callables never expose their target's retained source.
    return js_named_native_function_source_item(NULL);
}
extern "C" Item mvp_lmd_function_set_name(Item function, Item key, int64_t prefix) {
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, function), name(roots, key);
    name.set(js_function_name_display(name.get().get_string(), prefix));
    if (item_is_error(name.get())) return name.get();
    // SetFunctionName defines a configurable property; assignment would ignore
    // the already-created, nonwritable empty name.
    JsPropertyDescriptor descriptor = {};
    descriptor.flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
    js_pd_set_configurable(&descriptor, true);
    descriptor.value = name.get();
    Item prepared = mvp_lmd_function_prepare(owner.get());
    if (item_is_error(prepared)) return prepared;
    MvpLmdProgram* program = ((MvpLmdCallable*)owner.get().function)->program;
    String* property = name_pool_create_len(program->frontend->name_pool, "name", 4);
    if (!property) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    // minimal realms use the owner's canonical pool without initializing native JS intrinsics.
    Item defined = mvp_lmd_define_own_property(owner.get(),
        Item{.item = s2it(property)}, &descriptor);
    return item_is_error(defined) || defined.item == ITEM_FALSE ?
        item_is_error(defined) ? defined : mvp_lmd_fail(LMD_MVP_TYPE, 0) : owner.get();
}
extern "C" Item mvp_lmd_function_prepare(Item owner) {
    Function* function = owner.function;
    // native own properties are materialized by the shared descriptor kernel.
    if (function_has_abi(function, FN_ENTRY_ABI_JS_FUNCTION)) return owner;
    if (function->entry_abi != FN_ENTRY_ABI_MVP_LMD || !function->requires_runtime_context)
        return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    MvpLmdCallable* callable = (MvpLmdCallable*)function;
    if (callable->properties || callable->constructor) return owner;
    const AstFuncNode* ast = (const AstFuncNode*)function->def;
    bool has_prototype = ast && (!callable->non_constructible || ast->is_generator);
    if (function->closure_field_count > UINT8_MAX - 3) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> held(roots, owner);
    MvpLmdProgram* p = callable->program;
    int64_t prefix = 0;
    String* inferred = ast && !ast->name ? named_evaluation_spelling(p, (AstNode*)ast, &prefix) : NULL;
    MvpLmdClass* cls = (MvpLmdClass*)pool_calloc(p->frontend->pool, sizeof(MvpLmdClass));
    if (!cls) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    mvp_lmd_nominal_initialize(cls, ast && ast->name ? ast->name : inferred ? inferred :
        name_pool_create_len(p->frontend->name_pool, "", 0), p);
    if (ast && ast->is_generator) cls->nominal.base = ast->is_async
        ? &p->library->async_generator->nominal : &p->library->generator.nominal;
    cls->owner = function; cls->property_slot = function->closure_field_count;
    LmdObjectPlan* layout = ast && p->function_construction ? object_plan(p, (AstNode*)ast, true) : NULL;
    if (layout) {
        cls->allocation_shape = (TypeMap*)pool_calloc(p->frontend->pool, sizeof(TypeMap));
        if (!cls->allocation_shape) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        *cls->allocation_shape = *layout->blueprint;
        cls->allocation_shape->nominal = &cls->nominal;
    }
    // function own keys are created in length/name/prototype order; constructor belongs to the prototype.
    if (!plan_callable_shapes(p, cls, has_prototype ? ast && ast->is_generator ? 3 : 4 : 2,
            JSPD_NON_WRITABLE, JSPD_NON_CONFIGURABLE))
        return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    typemap_hash_build(&cls->static_shape, p->frontend->pool);
    typemap_hash_build(&cls->prototype_shape, p->frontend->pool);
    int count = cls->property_slot;
    Item* env = (Item*)heap_calloc_closure_env((count + 3) * sizeof(Item));
    if (!env) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    function = held.get().function; callable = (MvpLmdCallable*)function;
    if (count) memcpy(env, function->closure_env, count * sizeof(Item));
    function->closure_env = env; function->closure_field_count = count + 3;
    callable->properties = cls;
    Item initialized = mvp_lmd_class_new(cls, held.get());
    if (item_is_error(initialized)) return initialized;
    if (inferred) {
        Item named = mvp_lmd_function_set_name(held.get(), Item{.item = s2it(inferred)}, prefix);
        if (item_is_error(named)) return named;
    }
    // closed synchronous units retain their nominal chain without reflective function intrinsics.
    if (ast && (ast->is_async || (ast->is_generator && p->library->generator_function_values))) {
        Item parent = ast->is_generator ? ast->is_async ? p->library->async_generator_values[4]
            : p->library->generator_function_values[1] : p->library->async_function_values[1];
        Item linked = mvp_lmd_set_prototype(held.get(), parent);
        if (item_is_error(linked)) return linked;
    }
    return initialized;
}
extern "C" Item mvp_lmd_function_new(uint64_t code_id, MvpLmdProgram* p) {
    LmdFunction* f = (LmdFunction*)p->functions->data[code_id];
    Function* value = (Function*)heap_calloc(f->ast->is_generator || f->ast->is_async ? sizeof(MvpLmdNativeCallable) :
        p->receiver_abi ? sizeof(MvpLmdCallable) : sizeof(Function), LMD_TYPE_FUNC);
    if (!value) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    function_init_abi(value, LMD_TYPE_FUNC, FN_ENTRY_ABI_MVP_LMD);
    value->arity = f->formal_length;
    value->ptr = (fn_ptr)f->address;
    if (f->ast->is_generator || f->ast->is_async) {
        ((MvpLmdNativeCallable*)value)->state = f->address;
        value->ptr = f->ast->is_generator ? (fn_ptr)mvp_lmd_generator_call : (fn_ptr)mvp_lmd_async_call;
    }
    value->runtime_context = context;
    value->def = f->ast;
    if (p->receiver_abi) {
        MvpLmdCallable* callable = (MvpLmdCallable*)value;
        callable->requires_runtime_context = true; callable->program = p; callable->home = f->home;
        callable->home_slot = f->home_slot;
        callable->non_constructible = f->non_constructible;
        callable->constructor = f->constructor;
        callable->static_method = f->static_method;
    }
    return Item{.item = (uint64_t)value};
}
extern "C" Item mvp_lmd_class_new(MvpLmdClass* cls, Item base, Item constructor_value) {
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> parent_value(roots, base), constructor_root(roots, constructor_value), parent_prototype(roots, ItemNull);
    Rooted<Item> metadata(roots, ItemNull);
    MvpLmdClass* definition = cls;
    bool explicit_parent_link = cls->dynamic;
    if (cls->dynamic) {
        MvpLmdClass* instance = (MvpLmdClass*)pool_calloc(cls->program->frontend->pool, sizeof(MvpLmdClass));
        if (!instance) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        *instance = *cls; cls = instance;
        cls->nominal.extension_data = cls; cls->prototype_nominal.extension_data = cls;
        cls->shape.nominal = &cls->nominal;
        cls->prototype_shape.nominal = cls->static_shape.nominal = &cls->prototype_nominal;
        if (constructor_root.get().item == ITEM_NULL) {
            MvpLmdCallable* fn = (MvpLmdCallable*)heap_calloc(sizeof(MvpLmdCallable), LMD_TYPE_FUNC);
            if (!fn) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            function_init_abi(fn, LMD_TYPE_FUNC, FN_ENTRY_ABI_MVP_LMD);
            fn->requires_runtime_context = true; fn->runtime_context = context; fn->program = cls->program;
            constructor_root.set(Item{.function = fn});
        }
        MvpLmdCallable* fn = (MvpLmdCallable*)constructor_root.get().function;
        bool retains_parent = cls->ast && cls->ast->superclass;
        int captures = fn->closure_field_count, count = captures + 3 + cls->initializer_count + cls->field_count +
            (cls->private_method_mask ? 2 : 0) + (retains_parent ? 1 : 0);
        // D5.3: large classes retain their metadata through one ordinary Array
        // slot, preserving the shared Function header and small-class layout.
        cls->indirect_values = count > UINT8_MAX;
        int closure_count = cls->indirect_values ? captures + 1 : count;
        if (closure_count > UINT8_MAX) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        if (cls->indirect_values) {
            // private metadata needs dense traced slots, independent of guest sparse-array policy.
            metadata.set(Item{.array = array()});
            int extent = count - captures;
            if (!metadata.get().array || !array_reserve_append_slots(metadata.get().array, extent))
                return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
            for (int i = 0; i < extent; i++) metadata.get().array->items[i].item = ITEM_JS_UNDEFINED;
            metadata.get().array->length = extent;
        }
        Item* env = (Item*)heap_calloc_closure_env(closure_count * sizeof(Item));
        if (!env) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        if (captures) memcpy(env, fn->closure_env, captures * sizeof(Item));
        // nominal ancestry outlives mutable prototype links; keep its parent
        // constructor in the existing precisely traced closure (D5.3).
        if (cls->indirect_values) env[captures] = metadata.get();
        fn->closure_env = env; fn->closure_field_count = closure_count; fn->home = cls; fn->constructor = true; fn->non_constructible = false;
        cls->owner = fn; cls->property_slot = captures;
        cls->values = cls->indirect_values ? metadata.get().array->items : env + captures;
        if (retains_parent) cls->values[count - captures - 1] = parent_value.get();
        cls->initializers = cls->values + 3; cls->field_keys = cls->initializers + cls->initializer_count;
        if (cls->private_field_slots) for (uint32_t i = 0; i < cls->field_count; i++)
            cls->field_keys[i] = cls->program->slots[cls->private_field_slots[i]];
        cls->values[0] = constructor_root.get();
        if (cls->private_method_mask) {
            for (int i = 0; i < 2; i++) if (cls->private_method_mask & (1 << i)) {
                Item& methods = cls->field_keys[cls->field_count + i];
                methods = mvp_lmd_object_new(mvp_lmd_program_library(cls->program)->object_shape, 0);
                if (item_is_error(methods)) return methods;
            }
        }
    }
    if (cls->ast && cls->ast->superclass) {
        if (parent_value.get().item != ITEM_NULL) {
            if (!mvp_lmd_is_constructor(parent_value.get())) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
            parent_value.set(mvp_lmd_dynamic_import_value(cls->program, parent_value.get()));
            if (item_is_error(parent_value.get())) return parent_value.get();
            if (get_type_id(parent_value.get()) == LMD_TYPE_FUNC &&
                    function_has_abi(parent_value.get().function, FN_ENTRY_ABI_JS_FUNCTION)) {
                // native constructors own allocation through the shared ABI; their public ancestry uses live links.
                explicit_parent_link = true;
            } else {
                if (!mvp_lmd_class_record(parent_value.get()) && get_type_id(parent_value.get()) == LMD_TYPE_FUNC) {
                    Item ready = mvp_lmd_function_prepare(parent_value.get());
                    if (item_is_error(ready)) return ready;
                }
                MvpLmdClass* parent = mvp_lmd_class_record(parent_value.get());
                // retained units in one execution share the heap and nominal type lifetime.
                if (!parent || parent->program->execution_owner != cls->program->execution_owner)
                    return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
                MvpLmdCallable* parent_callable = (MvpLmdCallable*)parent_value.get().function;
                // buffer/view constructors allocate their internal slots before applying the live newTarget prototype.
                MvpLmdLibraryState* library = mvp_lmd_program_library(parent->program);
                bool weak_constructor = false;
                for (int family = 0; library && family < library->weak_class_count; family++)
                    weak_constructor |= parent == &library->weak_collections[family];
                bool primitive_constructor = false;
                for (int family = 0; library && library->primitives && family < LMD_PRIMITIVE_COUNT; family++)
                    primitive_constructor |= parent == &library->primitives[family];
                bool dynamic_constructor = library && parent_value.get().item == library->values[LMD_LIBRARY_FUNCTION].item;
                // shared host wrappers already own allocation, native brand and newTarget handling.
                if (parent_callable->native_constructor && parent_callable->custom_new_target && !parent_callable->foreign_properties &&
                        parent != &library->object && parent != &library->array && parent != &library->symbol && parent != &library->date && parent != &library->map && parent != &library->set && parent != library->promise &&
                        parent != library->async_generator && parent != library->async_function && parent != &library->generator && parent != &library->regexp && parent != library->shared_buffer &&
                        !weak_constructor && !primitive_constructor && !dynamic_constructor &&
                        (!library->typed_arrays || !lambda_nominal_derives_from(&parent->nominal, &library->typed_arrays[12].nominal)) && (!library->buffers ||
                        (parent != &library->buffers[0] && parent != &library->buffers[1])) &&
                        mvp_lmd_error_kind(&parent->nominal, library) < 0)
                    return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
                cls->nominal.base = &parent->nominal;
                cls->prototype_nominal.base = &parent->prototype_nominal;
                // native constructors can publish a public prototype distinct from their carrier family.
                explicit_parent_link |= parent_callable->native_constructor;
            }
            if (explicit_parent_link) {
                parent_prototype.set(mvp_lmd_property_with_receiver(parent_value.get(),
                    Item{.item = s2it(heap_create_name("prototype", 9))}, ItemNull, LMD_PROP_GET, parent_value.get()));
                if (item_is_error(parent_prototype.get())) return parent_prototype.get();
                if (parent_prototype.get().item != ITEM_NULL && !mvp_lmd_is_object(parent_prototype.get()))
                    return mvp_lmd_fail(LMD_MVP_TYPE, 0);
            }
        } else if (!cls->dynamic) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    }
    // top-level declarations evaluate once; the registered slots own every published class value.
    mvp_lmd_class_values(cls)[1] = mvp_lmd_object_new(&cls->prototype_shape, 0);
    if (item_is_error(mvp_lmd_class_values(cls)[1])) return mvp_lmd_class_values(cls)[1];
    mvp_lmd_class_values(cls)[2] = mvp_lmd_object_new(&cls->static_shape, 0);
    if (item_is_error(mvp_lmd_class_values(cls)[2])) return mvp_lmd_class_values(cls)[2];
    if (cls->owner) mvp_lmd_class_values(cls)[0] = cls->dynamic ? constructor_root.get() : parent_value.get();
    else if (cls->constructor_id >= 0)
        mvp_lmd_class_values(cls)[0] = mvp_lmd_function_new((uint64_t)cls->constructor_id, cls->program);
    else {
        MvpLmdCallable* fn = (MvpLmdCallable*)heap_calloc(sizeof(MvpLmdCallable), LMD_TYPE_FUNC);
        if (!fn) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        function_init_abi(fn, LMD_TYPE_FUNC, FN_ENTRY_ABI_MVP_LMD);
        fn->requires_runtime_context = true; fn->runtime_context = context;
        fn->program = cls->program; fn->home = cls; fn->constructor = true;
        mvp_lmd_class_values(cls)[0] = Item{.function = fn};
    }
    if (item_is_error(mvp_lmd_class_values(cls)[0])) return mvp_lmd_class_values(cls)[0];
    Map* prototype = mvp_lmd_class_values(cls)[1].map;
    Map* statics = mvp_lmd_class_values(cls)[2].map;
    // every slot is predeclared and nonenumerable; initialization does not mutate shared shapes.
    ShapeEntry* constructor = typemap_hash_lookup(&cls->prototype_shape, "constructor", 11);
    ShapeEntry* proto = typemap_hash_lookup(&cls->static_shape, "prototype", 9);
    // arrows and ordinary methods own data properties without a constructible prototype.
    if (constructor) map_field_store((char*)prototype->data + constructor->byte_offset, mvp_lmd_class_values(cls)[0], LMD_TYPE_FUNC);
    if (proto) map_field_store((char*)statics->data + proto->byte_offset, mvp_lmd_class_values(cls)[1], LMD_TYPE_MAP);
    ShapeEntry* name = typemap_hash_lookup(&cls->static_shape, "name", 4);
    ShapeEntry* length = typemap_hash_lookup(&cls->static_shape, "length", 6);
    if (name->type->type_id == LMD_TYPE_STRING)
        // an anonymous class with an explicit constructor still has an empty class name.
        map_field_store((char*)statics->data + name->byte_offset, Item{.item = s2it(cls->ast ?
            (cls->ast->name ? cls->ast->name : name_pool_create_len(cls->program->frontend->name_pool, "", 0)) :
            cls->owner && cls->owner->def && ((AstFuncNode*)cls->owner->def)->name ?
            ((AstFuncNode*)cls->owner->def)->name : name_pool_create_len(cls->program->frontend->name_pool, "", 0))}, LMD_TYPE_STRING);
    if (length->type->type_id == LMD_TYPE_INT)
        map_field_store((char*)statics->data + length->byte_offset, Item{.item = i2it(mvp_lmd_class_values(cls)[0].function->arity)}, LMD_TYPE_INT);
    if (explicit_parent_link && cls->ast && cls->ast->superclass) {
        Item linked = mvp_lmd_set_prototype(mvp_lmd_class_values(cls)[1], parent_prototype.get());
        if (item_is_error(linked)) return linked;
        if (parent_value.get().item != ITEM_NULL) {
            linked = mvp_lmd_set_prototype(mvp_lmd_class_values(cls)[0], parent_value.get());
            if (item_is_error(linked)) return linked;
        }
    }
    if (cls->owner) {
        // Retained interpreter classes have no MIR declaration slots or AST index entry.
        AstNode* parent = cls->ast && definition->values
            ? ast_index_parent(&cls->program->frontend->ast_index, (AstNode*)cls->ast) : NULL;
        if (cls->dynamic && cls->ast->node_type == AST_NODE_CLASS && parent && parent->node_type == AST_SCRIPT)
            memcpy(mvp_lmd_class_values(definition), mvp_lmd_class_values(cls), 3 * sizeof(Item));
        return mvp_lmd_class_values(cls)[0];
    }
    for (int i = 1; i < cls->program->functions->length; i++) {
        LmdFunction* function = (LmdFunction*)cls->program->functions->data[i];
        if (function->home != cls || function->id == cls->constructor_id) continue;
        AstMethodNode* method = (AstMethodNode*)function->ast;
        String* key = name_pool_create_string(cls->program->frontend->name_pool, ((AstIdentNode*)method->key)->name);
        Item value = mvp_lmd_function_new(function->id, cls->program);
        if (item_is_error(value)) return value;
        Map* target = method->static_method ? statics : prototype;
        if (method->kind == AstMethodNode::JS_METHOD_GET || method->kind == AstMethodNode::JS_METHOD_SET) {
            Item defined = mvp_lmd_define_accessor(Item{.map = target}, Item{.item = s2it(key)}, value,
                (JSPD_NON_ENUMERABLE << 8) | (method->kind == AstMethodNode::JS_METHOD_SET ? 1 : 0));
            if (item_is_error(defined)) return defined;
        } else {
            ShapeEntry* field = typemap_hash_lookup((TypeMap*)target->type, key->chars, key->len);
            // an earlier accessor with the same name has no physical slot to overwrite.
            if (!map_shape_set_resolved(target, key, field, value, false)) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        }
    }
    return mvp_lmd_class_values(cls)[0];
}

Item mvp_lmd_dynamic_class(MvpLmdProgram* p, AstClassNode* ast, Item base, Item constructor) {
    if (!p || !ast) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    MvpLmdClass plan = {};
    plan.ast = ast; plan.dynamic = true; plan.constructor_id = -1;
    if (!mvp_lmd_class_plan_elements(&plan)) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    mvp_lmd_nominal_initialize(&plan, ast->name ? ast->name
        : name_pool_create_len(p->frontend->name_pool, "", 0), p);
    if (!plan_callable_shapes(p, &plan, 4, JSPD_NON_WRITABLE,
            JSPD_NON_WRITABLE | JSPD_NON_CONFIGURABLE))
        return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    typemap_hash_build(&plan.static_shape, p->frontend->pool);
    typemap_hash_build(&plan.prototype_shape, p->frontend->pool);
    return mvp_lmd_class_new(&plan, base, constructor);
}

static void plan_library_class(MvpLmdProgram* p, MvpLmdClass* cls, const char* name,
        TypeId prototype_type = LMD_TYPE_MAP) {
    mvp_lmd_nominal_initialize(cls, name_pool_create_len(p->frontend->name_pool, name, strlen(name)), p);
    if (!plan_callable_shapes(p, cls, 4, 0, 0, prototype_type))
        diagnostic(p, NULL, "builtin common shape allocation");
}
static void plan_library_shapes(MvpLmdProgram* p, bool metadata_only = false) {
    // key admission inspects the native field inventory without enabling runtime coercion.
    if (!p->library) p->library = (MvpLmdLibraryState*)pool_calloc(p->frontend->pool, sizeof(MvpLmdLibraryState));
    if (!p->library) { diagnostic(p, NULL, "builtin metadata allocation"); return; }
    if (!p->library->object.shape.type_id) {
        MvpLmdClass* classes[] = {&p->library->date, &p->library->regexp, &p->library->object, &p->library->array, &p->library->generator, &p->library->symbol, &p->library->iterator, &p->library->map, &p->library->set};
        const char* names[] = {"Date", "RegExp", "Object", "Array", "Generator", "Symbol", "Iterator", "Map", "Set"};
        for (int i = 0; i < 9; i++)
            plan_library_class(p, classes[i], names[i], i == 3 ? LMD_TYPE_ARRAY : LMD_TYPE_MAP);
        p->library->date.nominal.base = &p->library->object.nominal;
        p->library->regexp.nominal.base = &p->library->object.nominal;
        p->library->generator.nominal.base = &p->library->object.nominal;
        p->library->iterator.nominal.base = &p->library->object.nominal;
        p->library->map.nominal.base = &p->library->object.nominal;
        p->library->set.nominal.base = &p->library->object.nominal;
        p->library->json_shape = *p->roots[0];
        p->library->json_shape.nominal = &p->library->object.nominal;
        struct Field { TypeMap* shape; const char* name; TypeId type; };
        const Field fields[] = {
            {&p->library->json_shape, "parse", LMD_TYPE_FUNC}, {&p->library->json_shape, "stringify", LMD_TYPE_FUNC},
            {&p->library->date.prototype_shape, "getTime", LMD_TYPE_FUNC},
            {&p->library->date.prototype_shape, "valueOf", LMD_TYPE_FUNC}, {&p->library->date.static_shape, "now", LMD_TYPE_FUNC},
            {&p->library->object.prototype_shape, "toString", LMD_TYPE_FUNC},
            {&p->library->object.prototype_shape, "toLocaleString", LMD_TYPE_FUNC},
            {&p->library->array.prototype_shape, "toLocaleString", LMD_TYPE_FUNC},
            {&p->library->object.prototype_shape, "isPrototypeOf", LMD_TYPE_FUNC},
            {&p->library->object.prototype_shape, "propertyIsEnumerable", LMD_TYPE_FUNC},
            {&p->library->array.static_shape, "isArray", LMD_TYPE_FUNC},
            {&p->library->object.prototype_shape, "hasOwnProperty", LMD_TYPE_FUNC},
            {&p->library->object.prototype_shape, "valueOf", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "is", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "groupBy", LMD_TYPE_FUNC},
            {&p->library->map.static_shape, "groupBy", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "keys", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "values", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "entries", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "hasOwn", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "getOwnPropertyNames", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "getOwnPropertySymbols", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "getOwnPropertyDescriptors", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "defineProperty", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "defineProperties", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "setPrototypeOf", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "getOwnPropertyDescriptor", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "preventExtensions", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "seal", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "freeze", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "isExtensible", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "isSealed", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "isFrozen", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "create", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "assign", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "getPrototypeOf", LMD_TYPE_FUNC},
            {&p->library->array.static_shape, "from", LMD_TYPE_FUNC},
            {&p->library->array.static_shape, "of", LMD_TYPE_FUNC},
            {&p->library->generator.prototype_shape, "next", LMD_TYPE_FUNC},
            {&p->library->generator.prototype_shape, "return", LMD_TYPE_FUNC},
            {&p->library->generator.prototype_shape, "throw", LMD_TYPE_FUNC},
            {&p->library->symbol.static_shape, "iterator", LMD_TYPE_SYMBOL},
            {&p->library->iterator.prototype_shape, "next", LMD_TYPE_FUNC}};
        for (const Field& field : fields) if (!plan_shape_field(p, field.shape,
                name_pool_create_len(p->frontend->name_pool, field.name, strlen(field.name)), field.type, JSPD_NON_ENUMERABLE))
            diagnostic(p, NULL, "builtin shape allocation");
        for (int i = 0; ; i++) {
            const JsBuiltinMethodSpec* method = js_builtin_method_at(i);
            if (!method) break;
            if (method->owner != JS_BUILTIN_OWNER_SYMBOL_STATIC_METHOD) continue;
            if (!plan_shape_field(p, &p->library->symbol.static_shape,
                    name_pool_create_len(p->frontend->name_pool, method->name, method->len), LMD_TYPE_FUNC, JSPD_NON_ENUMERABLE))
                diagnostic(p, NULL, "Symbol registry shape allocation");
        }
        const JsBuiltinMethodSpec* symbol_description = js_builtin_catalog_find(JS_BUILTIN_OWNER_SYMBOL_INTERNAL, "description", 11);
        if (!plan_shape_field(p, &p->library->symbol.prototype_shape,
                name_pool_create_len(p->frontend->name_pool, symbol_description->name, symbol_description->len),
                LMD_TYPE_ANY, JSPD_NON_ENUMERABLE)) diagnostic(p, NULL, "Symbol description shape allocation");
        if (!plan_shape_field(p, &p->library->regexp.shape,
                name_pool_create_len(p->frontend->name_pool, "lastIndex", 9), LMD_TYPE_INT,
                JSPD_NON_ENUMERABLE | JSPD_NON_CONFIGURABLE))
            diagnostic(p, NULL, "regexp lastIndex shape allocation");
        for (int i = 0; ; i++) {
            const JsBuiltinMethodSpec* method = js_builtin_method_at(i);
            if (!method) break;
            if (method->owner != JS_BUILTIN_OWNER_REGEXP_PROTOTYPE_METHOD &&
                    method->owner != JS_BUILTIN_OWNER_REGEXP_ACCESSOR_INTERNAL) continue;
            if (!plan_shape_field(p, &p->library->regexp.prototype_shape,
                    name_pool_create_len(p->frontend->name_pool, method->name, method->len),
                    method->property_kind == JS_BUILTIN_PROPERTY_ACCESSOR ? LMD_TYPE_ANY : LMD_TYPE_FUNC,
                    JSPD_NON_ENUMERABLE))
                diagnostic(p, NULL, "regexp protocol shape allocation");
        }
        for (int i = 0; i < mvp_lmd_builtin_method_count; i++) {
            const MvpLmdBuiltinMethod& method = mvp_lmd_builtin_methods[i];
            if (method.owner == LMD_TYPE_ARRAY && !plan_shape_field(p, &p->library->array.prototype_shape,
                    name_pool_create_len(p->frontend->name_pool, method.name, strlen(method.name)), LMD_TYPE_FUNC, JSPD_NON_ENUMERABLE))
                diagnostic(p, NULL, "array builtin shape allocation");
        }
        for (int i = 0; ; i++) {
            const JsBuiltinMethodSpec* method = js_builtin_method_at(i);
            if (!method) break;
            bool set = method->owner == JS_BUILTIN_OWNER_SET_PROTOTYPE_METHOD;
            if (!set && method->owner != JS_BUILTIN_OWNER_MAP_PROTOTYPE_METHOD) continue;
            if (!plan_shape_field(p, set ? &p->library->set.prototype_shape : &p->library->map.prototype_shape,
                    name_pool_create_len(p->frontend->name_pool, method->name, method->len), LMD_TYPE_FUNC, JSPD_NON_ENUMERABLE))
                diagnostic(p, NULL, "collection shape allocation");
        }
        if (!plan_shape_field(p, &p->library->set.prototype_shape,
                name_pool_create_len(p->frontend->name_pool, "keys", 4), LMD_TYPE_FUNC, JSPD_NON_ENUMERABLE))
            diagnostic(p, NULL, "Set keys alias shape allocation");
        typemap_hash_build(&p->library->json_shape, p->frontend->pool);
        for (MvpLmdClass* cls : classes) {
            typemap_hash_build(&cls->shape, p->frontend->pool);
            typemap_hash_build(&cls->prototype_shape, p->frontend->pool);
            typemap_hash_build(&cls->static_shape, p->frontend->pool);
        }
    }
    if (metadata_only) return;
    p->library->date_observed = p->date_observed;
    bool collection_iterator_observed = !pattern_iterators_are_intrinsic(p);
    for (AstNodeId i = 0; !collection_iterator_observed && i < p->frontend->ast_index.count; i++) {
        AstNode* node = p->frontend->ast_index.nodes[i];
        if (node->node_type != AST_NODE_MEMBER_EXPR && node->node_type != AST_NODE_INDEX_EXPR) continue;
        AstFieldNode* member = (AstFieldNode*)node;
        String* name = member_spelling(member);
        if (name && !named(name, "next")) continue;
        unsigned key_kind = name ? 0 : kind(p, member->property);
        // zero means facts are not established yet; only a nonempty proven key domain excludes "next".
        if (!name && key_kind && !(key_kind & ~(K_NUMBER | K_BOOL | K_NULL | K_UNDEFINED))) continue;
        AstNode* parent = ast_index_parent(&p->frontend->ast_index, node);
        // extraction exposes the next function's family brand; direct next() cannot change its receiver.
        collection_iterator_observed = !parent || parent->node_type != AST_NODE_CALL_EXPR ||
            ((AstCallNode*)parent)->callee != node;
    }
    if (collection_iterator_observed && !p->library->iterator_families) {
        // Proven unobserved units keep their original iterator shape and roots.
        p->library->iterator_families = (MvpLmdClass*)pool_calloc(p->frontend->pool, LMD_ITERATOR_COUNT * sizeof(MvpLmdClass));
        if (!p->library->iterator_families) { diagnostic(p, NULL, "collection iterator metadata allocation"); return; }
        p->iterator_family_slot = p->slot_count; p->slot_count += LMD_ITERATOR_COUNT * 3;
        for (int family = 0; family < LMD_ITERATOR_COUNT; family++) {
            MvpLmdClass* cls = &p->library->iterator_families[family];
            const char* name = mvp_lmd_iterator_names[family];
            mvp_lmd_nominal_initialize(cls, name_pool_create_len(p->frontend->name_pool, name, strlen(name)), p);
            cls->nominal.base = &p->library->iterator.nominal;
            if (!plan_shape_field(p, &cls->prototype_shape,
                    name_pool_create_len(p->frontend->name_pool, "next", 4), LMD_TYPE_FUNC, JSPD_NON_ENUMERABLE))
                diagnostic(p, NULL, "collection iterator next shape allocation");
            typemap_hash_build(&cls->shape, p->frontend->pool);
            typemap_hash_build(&cls->prototype_shape, p->frontend->pool);
        }
    }
    bool regexp_iterator_observed = p->symbols || p->property_callbacks || p->global_reflection || p->dynamic_source;
    for (AstNodeId i = 0; !regexp_iterator_observed && i < p->frontend->ast_index.count; i++) {
        AstNode* node = p->frontend->ast_index.nodes[i];
        if (node->node_type != AST_NODE_MEMBER_EXPR && node->node_type != AST_NODE_INDEX_EXPR) continue;
        AstFieldNode* member = (AstFieldNode*)node;
        String* name = member_spelling(member);
        unsigned key_kind = name ? 0 : kind(p, member->property);
        // only a nonempty proven primitive key domain excludes a computed "matchAll" lookup.
        regexp_iterator_observed = name ? named(name, "matchAll") :
            !key_kind || (key_kind & ~(K_NUMBER | K_BOOL | K_NULL | K_UNDEFINED));
    }
    if (regexp_iterator_observed && !p->library->regexp_iterator) {
        p->library->regexp_iterator = (MvpLmdClass*)pool_calloc(p->frontend->pool, sizeof(MvpLmdClass));
        if (!p->library->regexp_iterator) { diagnostic(p, NULL, "RegExp iterator metadata allocation"); return; }
        p->regexp_iterator_slot = p->slot_count; p->slot_count += 3;
        MvpLmdClass* cls = p->library->regexp_iterator;
        mvp_lmd_nominal_initialize(cls,
            name_pool_create_len(p->frontend->name_pool, "RegExp String Iterator", 22), p);
        cls->nominal.base = &p->library->iterator.nominal;
        const JsBuiltinMethodSpec* next = js_builtin_catalog_find(JS_BUILTIN_OWNER_REGEXP_ITERATOR_INTERNAL, "next", 4);
        if (!plan_shape_field(p, &cls->prototype_shape,
                name_pool_create_len(p->frontend->name_pool, next->name, next->len), LMD_TYPE_FUNC, JSPD_NON_ENUMERABLE))
            diagnostic(p, NULL, "RegExp iterator next shape allocation");
        typemap_hash_build(&cls->shape, p->frontend->pool);
        typemap_hash_build(&cls->prototype_shape, p->frontend->pool);
    }
    if ((collection_iterator_observed || p->library->regexp_iterator) && !p->library->iterator_base) {
        // reuse the existing observation proof; closed loops keep their original metadata and roots.
        p->library->iterator_base = (MvpLmdClass*)pool_calloc(p->frontend->pool, sizeof(MvpLmdClass));
        if (!p->library->iterator_base) { diagnostic(p, NULL, "Iterator prototype metadata allocation"); return; }
        p->iterator_base_slot = p->slot_count; p->slot_count += 3;
        MvpLmdClass* cls = p->library->iterator_base;
        mvp_lmd_nominal_initialize(cls, name_pool_create_len(p->frontend->name_pool, "Iterator", 8), p);
        cls->nominal.base = &p->library->object.nominal;
        typemap_hash_build(&cls->shape, p->frontend->pool);
        typemap_hash_build(&cls->prototype_shape, p->frontend->pool);
    }
    if (p->library->iterator_base) {
        TypeNominal* base = &p->library->iterator_base->nominal;
        p->library->iterator.nominal.base = p->library->generator.nominal.base = base;
        if (p->library->iterator_families) for (int i = 0; i < LMD_ITERATOR_COUNT; i++)
            p->library->iterator_families[i].nominal.base = base;
        if (p->library->regexp_iterator) p->library->regexp_iterator->nominal.base = base;
    }
    if (p->weak_collections && !p->library->weak_collections) {
        p->library->weak_class_count = p->weak_lifetimes ? 4 : 2;
        p->library->weak_collections = (MvpLmdClass*)pool_calloc(p->frontend->pool, p->library->weak_class_count * sizeof(MvpLmdClass));
        if (!p->library->weak_collections) { diagnostic(p, NULL, "weak collection metadata allocation"); return; }
        p->weak_slot = p->slot_count; p->slot_count += 3 * p->library->weak_class_count;
        for (int family = 0; family < p->library->weak_class_count; family++) {
            MvpLmdClass* cls = &p->library->weak_collections[family];
            plan_library_class(p, cls, mvp_lmd_weak_families[family].name);
            cls->nominal.base = &p->library->object.nominal;
            for (int i = 0; ; i++) {
                const JsBuiltinMethodSpec* method = js_builtin_method_at(i);
                if (!method) break;
                if (method->owner != mvp_lmd_weak_families[family].prototype_owner) continue;
                if (!plan_shape_field(p, &cls->prototype_shape,
                        name_pool_create_len(p->frontend->name_pool, method->name, method->len), LMD_TYPE_FUNC, JSPD_NON_ENUMERABLE))
                    diagnostic(p, NULL, "weak collection prototype allocation");
            }
            typemap_hash_build(&cls->prototype_shape, p->frontend->pool);
            int intrinsic = family < 2 ? I_WEAK_MAP + family : I_WEAK_REF + family - 2;
            p->intrinsics[intrinsic - 1].kinds = K_FUNCTION;
            p->intrinsics[intrinsic - 1].slot = p->weak_slot + 3 * family;
        }
    }
    if (p->promises && !p->library->promise) {
        p->library->promise = (MvpLmdClass*)pool_calloc(p->frontend->pool, sizeof(MvpLmdClass));
        if (!p->library->promise) { diagnostic(p, NULL, "Promise metadata allocation"); return; }
        plan_library_class(p, p->library->promise, "Promise");
        p->library->promise->nominal.base = &p->library->object.nominal;
        p->promise_slot = p->slot_count; p->slot_count += 3;
        p->intrinsics[I_PROMISE - 1].slot = p->promise_slot;
    }
    if (p->async_generators && !p->library->async_generator) {
        p->library->async_generator = (MvpLmdClass*)pool_calloc(p->frontend->pool, sizeof(MvpLmdClass));
        if (!p->library->async_generator) { diagnostic(p, NULL, "async generator metadata allocation"); return; }
        plan_library_class(p, p->library->async_generator, "AsyncGenerator");
        p->library->async_generator->nominal.base = &p->library->object.nominal;
        p->async_generator_slot = p->slot_count; p->slot_count += 5;
    }
    if (p->async_functions && !p->library->async_function) {
        p->library->async_function = (MvpLmdClass*)pool_calloc(p->frontend->pool, sizeof(MvpLmdClass));
        if (!p->library->async_function) { diagnostic(p, NULL, "async function metadata allocation"); return; }
        plan_library_class(p, p->library->async_function, "AsyncFunction");
        p->library->async_function->nominal.base = &p->library->object.nominal;
        p->async_function_slot = p->slot_count; p->slot_count += 3;
    }
    if (p->generator_functions && p->property_callbacks && !p->generator_function_slot) {
        p->generator_function_slot = p->slot_count; p->slot_count += 2;
    }
    if (p->errors && !p->library->errors) {
        p->library->errors = (MvpLmdClass*)pool_calloc(p->frontend->pool, LMD_ERROR_COUNT * sizeof(MvpLmdClass));
        if (!p->library->errors) { diagnostic(p, NULL, "error class metadata allocation"); return; }
        p->error_slot = p->slot_count; p->slot_count += 3 * LMD_ERROR_COUNT;
        for (int i = 0; i < LMD_ERROR_COUNT; i++) {
            MvpLmdClass* cls = &p->library->errors[i];
            plan_library_class(p, cls, mvp_lmd_error_names[i]);
            cls->nominal.base = i ? &p->library->errors[0].nominal : &p->library->object.nominal;
            for (const char* name : {"name", "message"})
                if (!plan_shape_field(p, &cls->prototype_shape,
                        name_pool_create_len(p->frontend->name_pool, name, strlen(name)), LMD_TYPE_STRING, JSPD_NON_ENUMERABLE))
                    diagnostic(p, NULL, "error prototype shape allocation");
            int intrinsic = i ? I_EVAL_ERROR + i - 1 : I_ERROR;
            p->intrinsics[intrinsic - 1].kinds = K_FUNCTION;
            p->intrinsics[intrinsic - 1].slot = p->error_slot + i * 3;
        }
    }
    if (p->primitives && !p->library->primitives) {
        p->library->primitives = (MvpLmdClass*)pool_calloc(p->frontend->pool, LMD_PRIMITIVE_COUNT * sizeof(MvpLmdClass));
        if (!p->library->primitives) { diagnostic(p, NULL, "primitive class metadata allocation"); return; }
        p->primitive_slot = p->slot_count; p->slot_count += 3 * LMD_PRIMITIVE_COUNT;
        const int intrinsics[] = {I_STRING, I_NUMBER, I_BOOLEAN, I_BIGINT};
        for (int i = 0; i < LMD_PRIMITIVE_COUNT; i++) {
            MvpLmdClass* cls = &p->library->primitives[i];
            plan_library_class(p, cls, mvp_lmd_primitive_names[i]);
            cls->nominal.base = &p->library->object.nominal;
            p->intrinsics[intrinsics[i] - 1].kinds = K_FUNCTION;
            p->intrinsics[intrinsics[i] - 1].slot = p->primitive_slot + i * 3;
        }
    }
    if (p->buffers && !p->library->buffers) {
        p->library->buffers = (MvpLmdClass*)pool_calloc(p->frontend->pool, 2 * sizeof(MvpLmdClass));
        if (!p->library->buffers) { diagnostic(p, NULL, "buffer class metadata allocation"); return; }
        p->buffer_slot = p->slot_count; p->slot_count += p->test262_host ? 7 : 6;
        p->library->test262_host = p->test262_host;
        if (p->test262_host) p->intrinsics[I_TEST262 - 1].slot = p->buffer_slot + 6;
        for (int i = 0; i < 2; i++) {
            MvpLmdClass* cls = &p->library->buffers[i];
            plan_library_class(p, cls, i ? "DataView" : "ArrayBuffer");
            cls->nominal.base = &p->library->object.nominal;
            p->intrinsics[I_ARRAYBUFFER + i - 1].kinds = K_FUNCTION;
            p->intrinsics[I_ARRAYBUFFER + i - 1].slot = p->buffer_slot + i * 3;
        }
    }
    if (p->atomics && !p->atomics_slot) {
        p->atomics_slot = p->slot_count++;
        p->intrinsics[I_ATOMICS - 1].slot = p->atomics_slot;
    }
    if (p->shared_buffer && !p->library->shared_buffer) {
        MvpLmdClass* cls = (MvpLmdClass*)pool_calloc(p->frontend->pool, sizeof(MvpLmdClass));
        if (!cls) { diagnostic(p, NULL, "shared buffer metadata allocation"); return; }
        p->library->shared_buffer = cls;
        p->shared_buffer_slot = p->slot_count; p->slot_count += 3;
        plan_library_class(p, cls, "SharedArrayBuffer");
        cls->nominal.base = &p->library->object.nominal;
        p->intrinsics[I_SHARED_ARRAYBUFFER - 1].kinds = K_FUNCTION;
        p->intrinsics[I_SHARED_ARRAYBUFFER - 1].slot = p->shared_buffer_slot;
    }
    if (p->typed_storage && !p->library->typed_arrays) {
        p->library->typed_arrays = (MvpLmdClass*)pool_calloc(p->frontend->pool, 13 * sizeof(MvpLmdClass));
        if (!p->library->typed_arrays) { diagnostic(p, NULL, "typed array metadata allocation"); return; }
        p->typed_slot = p->slot_count; p->slot_count += 39;
        for (int step = 0; step < 13; step++) {
            int type = step ? step - 1 : 12;
            MvpLmdClass* cls = &p->library->typed_arrays[type];
            plan_library_class(p, cls, type == 12 ? "TypedArray" : js_typed_array_spec((JsTypedArrayType)type)->name);
            cls->nominal.base = type == 12 ? &p->library->object.nominal : &p->library->typed_arrays[12].nominal;
            if (type == 12) continue;
            p->intrinsics[typed_intrinsics[type] - 1].kinds = K_FUNCTION;
            p->intrinsics[typed_intrinsics[type] - 1].slot = p->typed_slot + 3 * type;
        }
    }
    p->roots[1]->nominal = &p->library->map.nominal;
    p->roots[0]->nominal = &p->library->object.nominal;
    if (!p->realm_program) p->library->object_shape = p->roots[0];
    p->intrinsics[I_OBJECT - 1].kinds = K_FUNCTION;
    p->intrinsics[I_OBJECT - 1].slot = p->library_slot + LMD_LIBRARY_OBJECT;
    p->intrinsics[I_ARRAY - 1].kinds = K_FUNCTION;
    p->intrinsics[I_ARRAY - 1].slot = p->library_slot + LMD_LIBRARY_ARRAY;
    p->intrinsics[I_MAP - 1].kinds = p->intrinsics[I_SET - 1].kinds = K_FUNCTION;
    p->intrinsics[I_MAP - 1].slot = p->library_slot + LMD_LIBRARY_MAP;
    p->intrinsics[I_MATH - 1].slot = p->library_slot + LMD_LIBRARY_MATH;
}
static void plan_typed_storage(MvpLmdProgram* p) {
    if (!p->typed_storage) return;
    activate_library(p, p->frontend->ast_root);
    p->buffers = p->primitives = p->iterators = p->symbols = true;
    // publishing unused global constructors does not expose ordinary array mutation.
    if (p->typed_explicit) {
        p->property_callbacks = true;
        p->array_method_overrides = p->map_method_overrides = p->primitive_method_overrides = true;
    }
}
static void plan_global_bindings(MvpLmdProgram* p) {
    NameScope* scope = ((AstScript*)p->frontend->ast_root)->global_vars;
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        if (p->dynamic_source && !b->owner->id && b->entry->scope == scope && !b->intrinsic) {
            // interpreted global code can observe/write a lexical cell between any two MIR calls.
            b->external = b->assigned = b->observed = b->array_observed = true;
            b->kinds = K_ANY; b->dominated = false;
        }
        if (!p->source_module && !b->module_cell && !b->owner->id && !b->entry->is_lexical && !b->intrinsic && b->entry->scope == scope) {
            b->source_assigned = b->assigned; b->source_observed = b->observed; b->source_kinds = b->kinds;
            b->reflected_global = b->external = b->assigned = b->observed = b->array_observed = true;
            b->kinds = K_ANY; b->dominated = false;
        }
    }
    LmdBinding* reflected[I_COUNT] = {};
    for (AstNodeId i = 0; i < p->frontend->ast_index.count; i++) {
        LmdBinding* original = p->intrinsic_uses[i];
        if (!original || original->intrinsic <= I_INFINITY ||
                (!p->typed_storage && typed_intrinsic(original->intrinsic) >= 0)) continue;
        int intrinsic = original->intrinsic;
        if ((intrinsic >= I_PERFORMANCE && intrinsic <= I_REQUIRE) && !p->host_enabled) continue;
        if ((intrinsic >= I_SET_TIMEOUT && intrinsic <= I_QUEUE_MICROTASK) &&
                !p->host_enabled && !p->dynamic_source) continue;
        if (!reflected[intrinsic]) {
            // only observable global names lose intrinsic specialization; lexical declarations remain separate.
            LmdBinding* b = (LmdBinding*)mem_calloc(1, sizeof(LmdBinding), MEM_CAT_TEMP);
            NameEntry* entry = (NameEntry*)pool_calloc(p->frontend->pool, sizeof(NameEntry));
            if (!b || !entry) { mem_free(b); diagnostic(p, NULL, "global binding allocation"); return; }
            entry->name = name_pool_create_len(p->frontend->name_pool, intrinsic_names[intrinsic - 1], strlen(intrinsic_names[intrinsic - 1]));
            entry->is_mutable = true;
            b->entry = entry; b->owner = original->owner; b->kinds = K_ANY;
            b->reflected_intrinsic = intrinsic;
            b->reflected_global = b->external = b->assigned = b->observed = b->array_observed = true;
            arraylist_append(p->bindings, b); reflected[intrinsic] = b;
        }
        p->intrinsic_uses[i] = reflected[intrinsic];
    }
}
static bool mvp_lmd_intrinsic_enabled(MvpLmdProgram* p, int i) {
    if ((i == I_STRING || i == I_NUMBER || i == I_BOOLEAN || i == I_BIGINT) && !p->primitives) return false;
    if ((i == I_ERROR || (i >= I_EVAL_ERROR && i <= I_AGGREGATE_ERROR)) && !p->errors) return false;
    if (typed_intrinsic(i) >= 0 && !p->typed_storage) return false;
    if (i >= I_ARRAYBUFFER && i <= I_FLOAT16_ARRAY && !p->buffers) return false;
    if (i == I_SHARED_ARRAYBUFFER && !p->shared_buffer) return false;
    if (i == I_ATOMICS && !p->atomics) return false;
    if (i == I_PROMISE && !p->promises) return false;
    if ((i == I_WEAK_MAP || i == I_WEAK_SET) && !p->weak_collections) return false;
    if ((i == I_WEAK_REF || i == I_FINALIZATION_REGISTRY) && !p->weak_lifetimes) return false;
    if (i == I_EVAL && !p->dynamic_source) return false;
    if (i >= I_SET_TIMEOUT && i <= I_QUEUE_MICROTASK && !p->timers) return false;
    if (i == I_TEST262 && !p->test262_host) return false;
    return !(i >= I_PERFORMANCE && i <= I_REQUIRE && !p->host_active);
}
static Item mvp_lmd_intrinsic_value(MvpLmdProgram* p, int i) {
    int library = i == I_JSON ? LMD_LIBRARY_JSON : i == I_DATE ? LMD_LIBRARY_DATE :
        i == I_REGEXP ? LMD_LIBRARY_REGEXP : i == I_SYMBOL ? LMD_LIBRARY_SYMBOL :
        i == I_SET ? LMD_LIBRARY_SET : i == I_REFLECT ? LMD_LIBRARY_REFLECT :
        i == I_PROXY ? LMD_LIBRARY_PROXY : i == I_FUNCTION ? LMD_LIBRARY_FUNCTION :
        i == I_IS_NAN ? LMD_LIBRARY_GLOBAL_NAN :
        i == I_ESCAPE || i == I_UNESCAPE ? LMD_LIBRARY_ESCAPE + i - I_ESCAPE :
        i >= I_DECODE_URI && i <= I_ENCODE_COMPONENT ? LMD_LIBRARY_DECODE_URI + i - I_DECODE_URI : -1;
    if (i == I_PARSE_INT) return p->library->values[LMD_LIBRARY_PARSE_INT];
    return i == I_UNDEFINED ? Item{.item = ITEM_JS_UNDEFINED} : i == I_NAN ? push_d(NAN) :
        i == I_INFINITY ? push_d(INFINITY) : i == I_GLOBAL_THIS ? p->slots[p->global_slot] :
        library >= 0 ? p->library->values[library] : p->slots[p->intrinsics[i - 1].slot];
}
Item mvp_lmd_realm_intrinsic(const char* name, size_t length) {
    MvpLmdLibraryState* library = mvp_lmd_program_library();
    if (!library || !library->intrinsics_ready || library->native_callable_values) return ItemNull;
    MvpLmdProgram* program = library->object.program;
    // immutable intrinsic slots remain authoritative after guest global replacement.
    if (length == 10 && !memcmp(name, "TypedArray", 10))
        return library->typed_values ? library->typed_arrays[
            sizeof(typed_intrinsics) / sizeof(typed_intrinsics[0])].values[0] : ItemNull;
    const struct { const char* name; int slot; } globals[] = {
        {"parseFloat", LMD_LIBRARY_PARSE_FLOAT}, {"isFinite", LMD_LIBRARY_GLOBAL_FINITE},
        {"gc", LMD_LIBRARY_GC}, {"structuredClone", LMD_LIBRARY_STRUCTURED_CLONE}
    };
    for (const auto& global : globals)
        if (strlen(global.name) == length && !memcmp(name, global.name, length))
            return library->values[global.slot];
    if (length == 5 && !memcmp(name, "print", 5))
        return program->host.values ? program->host.values[LMD_HOST_PRINT] : ItemNull;
    for (int i = 1; i < I_COUNT; i++) {
        const char* spelling = intrinsic_names[i - 1];
        if (strlen(spelling) == length && !memcmp(spelling, name, length))
            return mvp_lmd_intrinsic_enabled(program, i) ? mvp_lmd_intrinsic_value(program, i) : ItemNull;
    }
    return ItemNull;
}
static Item mvp_lmd_global_initialize(MvpLmdProgram* p) {
    Item global = p->slots[p->global_slot];
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> value(roots, ItemNull);
    for (int i = 1; i < I_COUNT; i++) {
        // public host services keep their shared namespace and loader policies.
        if (p->execution_owner->shared_realm && (i == I_PERFORMANCE || i == I_PROCESS || i == I_REQUIRE)) continue;
        // an unrequested process stays a lazy shared namespace, not an undefined shadow.
        if (i == I_PROCESS && p->host_active &&
                p->host.values[LMD_HOST_PROCESS].item == ITEM_JS_UNDEFINED) continue;
        if (!mvp_lmd_intrinsic_enabled(p, i)) continue;
        value.set(mvp_lmd_intrinsic_value(p, i));
        if (item_is_error(value.get())) return value.get();
        Item result = mvp_lmd_named_set(global, intrinsic_names[i - 1], value.get(), JSPD_NON_ENUMERABLE |
            (i <= I_INFINITY ? JSPD_NON_WRITABLE | JSPD_NON_CONFIGURABLE : 0));
        if (item_is_error(result)) return result;
    }
    value.set(p->library->values[LMD_LIBRARY_PARSE_FLOAT]);
    if (item_is_error(value.get())) return value.get();
    Item result = mvp_lmd_named_set(global, "parseFloat", value.get(), JSPD_NON_ENUMERABLE);
    if (item_is_error(result)) return result;
    result = mvp_lmd_named_set(global, "isFinite", p->library->values[LMD_LIBRARY_GLOBAL_FINITE], JSPD_NON_ENUMERABLE);
    if (item_is_error(result)) return result;
    if (p->host_active) {
        // shell output uses the same rooted conversion/write path as console.log.
        result = mvp_lmd_named_set(global, "print", p->host.values[LMD_HOST_PRINT], JSPD_NON_ENUMERABLE);
        if (item_is_error(result)) return result;
        value.set(mvp_lmd_native_function(p, NULL,
            [](Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item receiver, Item) {
                return js_intrinsic_global_gc_body(self, receiver, args, (int)count, NULL);
            }, 0, "gc"));
        p->library->values[LMD_LIBRARY_GC] = value.get();
        result = item_is_error(value.get()) ? value.get() :
            mvp_lmd_named_set(global, "gc", value.get(), JSPD_NON_ENUMERABLE);
        if (item_is_error(result)) return result;
        value.set(mvp_lmd_native_function(p, NULL, mvp_lmd_structured_clone, 1, "structuredClone"));
        p->library->values[LMD_LIBRARY_STRUCTURED_CLONE] = value.get();
        result = item_is_error(value.get()) ? value.get() :
            mvp_lmd_named_set(global, "structuredClone", value.get(), JSPD_NON_ENUMERABLE);
        if (item_is_error(result)) return result;
    }
    if (p->execution_owner->host_runtime || p->execution_owner->owned_runtime) {
        result = js_install_host_namespace_slots(global, [](Item object, Item key, Item value) -> Item {
            // core intrinsics and the closed-shell host paths already have owners.
            if (typemap_hash_lookup_key((TypeMap*)object.map->type, key.get_string())) return ItemNull;
            return mvp_lmd_named_set(object, key, value, JSPD_NON_ENUMERABLE);
        }, true);
        if (item_is_error(result)) return result;
    }
    for (int i = 0; !p->execution_owner->shared_realm && !p->script_bindings_pending &&
            i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        if (!b->reflected_global || !b->entry->scope) continue;
        bool function = b->entry->node && b->entry->node->node_type == AST_NODE_FUNC && !b->entry->is_annex_b_companion;
        Item key = {.item = s2it(b->entry->name)};
        Item own = mvp_lmd_property_has(global, key, 0);
        if (item_is_error(own)) return own;
        if (own.item == ITEM_TRUE && !function) continue;
        result = mvp_lmd_named_set(global, b->entry->name->chars, Item{.item = ITEM_JS_UNDEFINED}, JSPD_NON_CONFIGURABLE);
        if (item_is_error(result)) return result;
    }
    return global;
}
Item mvp_lmd_dynamic_global_this(MvpLmdProgram* p, bool required) {
    if (!p && js_active_runtime_state && js_runtime_state.formal_mir_owner) {
        auto* library = mvp_lmd_program_library();
        p = library ? library->object.program : &js_runtime_state.formal_mir_owner->program;
    }
    return p && p->global_receiver
        ? p->slots[p->global_slot] : required ? mvp_lmd_fail(LMD_MVP_CAPABILITY, 0) : ItemNull;
}
bool mvp_lmd_dynamic_retain_script(MvpLmdProgram* p, JsScript* script) {
    if (!p->dynamic_scripts) p->dynamic_scripts = arraylist_new(4);
    return p->dynamic_scripts && arraylist_append(p->dynamic_scripts, script);
}
Item mvp_lmd_dynamic_global(MvpLmdProgram* p, Item key, Item value, int64_t operation, bool strict) {
    bool host_entry = !p;
    if (!p && js_active_runtime_state && js_runtime_state.formal_mir_owner) {
        auto* library = mvp_lmd_program_library();
        p = library ? library->object.program : &js_runtime_state.formal_mir_owner->program;
    }
    p = mvp_lmd_program_realm(p);
    if (!p || !p->dynamic_source || get_type_id(key) != LMD_TYPE_STRING)
        return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    bool script = operation & JS_INTERP_HOST_SCRIPT;
    operation &= ~JS_INTERP_HOST_SCRIPT;
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    uint64_t home = 0;
    Rooted<Item> name(roots, key), input(roots, lambda_item_adopt_scalar_home(value, &home));
    if (host_entry && operation == JS_INTERP_HOST_SET) {
        // shared host services publish native callbacks through the existing
        // AST/native callable membrane, preserving its identity cache.
        input.set(mvp_lmd_dynamic_import_value(p, input.get()));
        if (item_is_error(input.get())) return input.get();
    }
    key = name.get(); value = input.get();
    bool found = false;
    Item lexical = mvp_lmd_dynamic_lexical(p, key, value, operation, &found);
    if (found || item_is_error(lexical)) return lexical;
    if (operation == JS_INTERP_HOST_HAS_LEXICAL) return Item{.item = ITEM_FALSE};
    Item global = p->slots[p->global_slot];
    // the checked read shares an own-descriptor lookup while retaining ordered
    // HasProperty/Get for inherited bindings and proxies.
    if (operation == JS_INTERP_HOST_GET)
        return mvp_lmd_class_property(global, name.get(), ItemNull, LMD_PROP_GET_BINDING);
    if (operation == JS_INTERP_HOST_CHECK_DECLARE_LEXICAL) {
        JsPropertyDescriptor descriptor = {};
        Item present = mvp_lmd_get_own_property(global, name.get(), &descriptor);
        if (item_is_error(present)) return present;
        return present.item == ITEM_TRUE && !js_pd_is_configurable(&descriptor)
            ? mvp_lmd_fail(LMD_MVP_SYNTAX, 0) : Item{.item = ITEM_TRUE};
    }
    if (operation == JS_INTERP_HOST_CHECK_DECLARE_FUNCTION || operation == JS_INTERP_HOST_DECLARE_FUNCTION) {
        JsPropertyDescriptor current = {};
        Item present = mvp_lmd_get_own_property(global, name.get(), &current);
        if (item_is_error(present)) return present;
        bool configurable = present.item != ITEM_TRUE || js_pd_is_configurable(&current);
        if (present.item != ITEM_TRUE) {
            Item extensible = mvp_lmd_object_extensible(global, 0);
            if (item_is_error(extensible)) return extensible;
            if (extensible.item != ITEM_TRUE) return mvp_lmd_fail(script ? LMD_MVP_SYNTAX : LMD_MVP_TYPE, 0);
        } else if (!configurable && (!js_pd_is_data(&current) ||
                !(current.flags & JS_PD_WRITABLE) || !(current.flags & JS_PD_ENUMERABLE))) {
            return mvp_lmd_fail(script ? LMD_MVP_SYNTAX : LMD_MVP_TYPE, 0);
        }
        if (operation == JS_INTERP_HOST_CHECK_DECLARE_FUNCTION) return Item{.item = ITEM_TRUE};
        // configurable globals become ordinary eval function bindings; otherwise
        // preserve their attributes and update only the validated data value.
        JsPropertyDescriptor descriptor = {};
        descriptor.flags = JS_PD_HAS_VALUE;
        descriptor.value = input.get();
        if (configurable) {
            descriptor.flags |= JS_PD_HAS_WRITABLE | JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE |
                JS_PD_WRITABLE | JS_PD_ENUMERABLE;
            js_pd_set_configurable(&descriptor, !script);
        }
        Item defined = mvp_lmd_define_own_property(global, name.get(), &descriptor);
        return !item_is_error(defined) && defined.item != ITEM_TRUE
            ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : defined;
    }
    bool declaration = operation == JS_INTERP_HOST_DECLARE || operation == JS_INTERP_HOST_CHECK_DECLARE;
    Item present = mvp_lmd_property_with_receiver(global, name.get(), ItemNull,
        declaration ? LMD_PROP_OWN : LMD_PROP_HAS, global);
    if (item_is_error(present)) return present;
    if (operation == JS_INTERP_HOST_HAS) return present;
    if (declaration) {
        if (present.item == ITEM_TRUE) return Item{.item = ITEM_TRUE};
        Item extensible = mvp_lmd_object_extensible(global, 0);
        if (item_is_error(extensible)) return extensible;
        if (extensible.item != ITEM_TRUE) return operation == JS_INTERP_HOST_CHECK_DECLARE && input.get().item == ITEM_TRUE
            ? Item{.item = ITEM_FALSE} : mvp_lmd_fail(LMD_MVP_TYPE, 0);
        if (operation == JS_INTERP_HOST_CHECK_DECLARE) return Item{.item = ITEM_TRUE};
        // Script var bindings are non-configurable; eval vars retain their separate contract.
        JsPropertyDescriptor descriptor = {};
        descriptor.flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_HAS_ENUMERABLE |
            JS_PD_HAS_CONFIGURABLE | JS_PD_WRITABLE | JS_PD_ENUMERABLE;
        js_pd_set_configurable(&descriptor, !script);
        descriptor.value = Item{.item = ITEM_JS_UNDEFINED};
        return mvp_lmd_define_own_property(global, name.get(), &descriptor);
    }
    if (present.item != ITEM_TRUE && (operation == JS_INTERP_HOST_GET ||
            (operation == JS_INTERP_HOST_SET && strict))) return mvp_lmd_fail(LMD_MVP_REFERENCE, 0);
    int mode = operation == JS_INTERP_HOST_GET ? LMD_PROP_GET :
        operation == JS_INTERP_HOST_SET ? LMD_PROP_SET : LMD_PROP_DELETE;
    return mvp_lmd_property_with_receiver(global, name.get(), input.get(),
        mode | (strict ? LMD_PROP_STRICT : 0), global);
}
static Item mvp_lmd_prepare_global_vars(MvpLmdProgram* program, NameScope* scope) {
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> global(roots, program->slots[program->global_slot]);
    if (get_type_id(global.get()) != LMD_TYPE_MAP || global.get().map->map_kind != MAP_KIND_PLAIN) return ItemNull;
    // mixed function/Annex B declarations retain their ordered descriptor operations.
    for (NameEntry* entry = scope ? scope->first : NULL; entry; entry = entry->next)
        if (entry->is_annex_b_companion || (entry->node && entry->node->node_type == AST_NODE_FUNC)) return ItemNull;
    int capacity = scope ? scope->entry_count : 0;
    if (!capacity) return ItemNull;
    auto** names = (String**)pool_alloc(program->frontend->pool, capacity * sizeof(String*));
    if (!names) return ItemError;
    int count = 0;
    for (NameEntry* entry = scope->first; entry; entry = entry->next) {
        LmdBinding* local = binding(program, entry);
        if (!local || local->intrinsic || entry->is_lexical) continue;
        if (!typemap_hash_lookup_key((TypeMap*)global.get().map->type, entry->name)) names[count++] = entry->name;
    }
    // the preceding declaration checks prove extensibility; scope entries are unique and own keys are absent.
    if (!count) return ItemNull;
    if (!map_put_undefined_unique_absent_bulk_heap(global.get().map, names, count, runtime_shape_tree(), 0)) return ItemError;
    // MVP descriptors live in shape flags on the ordinary packed carrier, without a legacy DESC map kind.
    for (int i = 0; i < count; i++)
        typemap_hash_lookup_key((TypeMap*)global.get().map->type, names[i])->flags |= JSPD_NON_CONFIGURABLE;
    return ItemNull;
}
static Item mvp_lmd_prepare_script_bindings(MvpLmdProgram* program) {
    NameScope* scope = ((AstScript*)program->frontend->ast_root)->global_vars;
    // check every declaration before publishing any cell or global property.
    for (int pass = 0; pass < 2; pass++) {
        if (pass) {
            Item status = mvp_lmd_prepare_global_vars(program, scope);
            if (item_is_error(status)) return status;
        }
        for (NameEntry* entry = scope ? scope->first : NULL;
                entry; entry = entry->next) {
            LmdBinding* local = binding(program, entry);
            // implicit writes are analysis facts; every source declaration still needs its binding.
            if (!local || local->intrinsic) continue;
            if (entry->is_annex_b_companion) {
                if (!pass) continue;
                Item available = mvp_lmd_dynamic_global(program, Item{.item = s2it(entry->name)},
                    Item{.item = ITEM_TRUE}, JS_INTERP_HOST_CHECK_DECLARE | JS_INTERP_HOST_SCRIPT, false);
                if (item_is_error(available)) return available;
                if (available.item != ITEM_TRUE) continue;
            }
            bool function = entry->node && entry->node->node_type == AST_NODE_FUNC && !entry->is_annex_b_companion;
            if (pass && entry->is_lexical) continue;
            int operation = entry->is_lexical ? JS_INTERP_HOST_CHECK_DECLARE_LEXICAL :
                function ? (pass ? JS_INTERP_HOST_DECLARE_FUNCTION : JS_INTERP_HOST_CHECK_DECLARE_FUNCTION) :
                pass ? JS_INTERP_HOST_DECLARE : JS_INTERP_HOST_CHECK_DECLARE;
            Item result = mvp_lmd_dynamic_global(program, Item{.item = s2it(entry->name)},
                Item{.item = ITEM_JS_UNDEFINED}, operation | JS_INTERP_HOST_SCRIPT, false);
            if (item_is_error(result)) return result;
        }
    }
    program->script_published = true;
    program->script_bindings_pending = false;
    return ItemNull;
}
static Item mvp_lmd_publish_script_lexicals(MvpLmdProgram* program) {
    JsGlobalEnvironment* environment = mvp_lmd_dynamic_global_environment(program);
    if (!environment) return ItemError;
    NameScope* scope = ((AstScript*)program->frontend->ast_root)->global_vars;
    for (NameEntry* entry = scope ? scope->first : NULL; entry; entry = entry->next) {
        LmdBinding* local = binding(program, entry);
        if (!local || local->intrinsic || !entry->is_lexical) continue;
        // generated code and dynamic lookup share the already-rooted unit slot or capture cell.
        if (!js_global_environment_link_storage(environment, Item{.item = s2it(entry->name)},
                entry->is_const, program->slots, program->slot_count, local->slot, local->captured))
            return ItemError;
    }
    return ItemNull;
}
static int admission_pass(void* opaque) {
    MvpLmdProgram* p = (MvpLmdProgram*)opaque;
    // append shared synthetic definitions before sizing node-indexed MVP analysis tables.
    AstNodeId original_count = p->frontend->ast_index.count;
    for (AstNodeId i = 0; i < original_count; i++) {
        AstNode* node = p->frontend->ast_index.nodes[i];
        if (node->node_type == JS_AST_NODE_STATIC_BLOCK) {
            if (!js_script_field_initializer_ensure(p->frontend, NULL, (JsStaticBlockNode*)node)) {
                diagnostic(p, node, "static initializer allocation"); return false;
            }
            continue;
        }
        if (node->node_type != AST_NODE_FIELD) continue;
        AstClassFieldNode* field = (AstClassFieldNode*)node;
        if (field->value && !js_script_field_initializer_ensure(p->frontend, field)) {
            diagnostic(p, node, "field initializer allocation"); return false;
        }
    }
    p->immutable_objects = true;
    p->literal_kinds = (unsigned*)pool_calloc(p->frontend->pool,
        p->frontend->ast_index.count * sizeof(unsigned));
    if (!p->literal_kinds) { diagnostic(p, NULL, "literal kind allocation"); return false; }
    p->functions = arraylist_new(8); p->bindings = arraylist_new(16); p->classes = arraylist_new(4);
    if (!p->functions || !p->bindings || !p->classes) {
        diagnostic(p, NULL, "admission allocation"); return false;
    }
    LmdFunction* main = (LmdFunction*)mem_calloc(1, sizeof(LmdFunction), MEM_CAT_TEMP);
    str_copy(main->name, sizeof(main->name), "mvp_lmd_program", 15);
    arraylist_append(p->functions, main);
    p->intrinsic_uses = (LmdBinding**)mem_calloc(p->frontend->ast_index.count,
        sizeof(LmdBinding*), MEM_CAT_TEMP);
    for (int i = 0; i < I_COUNT - 1; i++) {
        p->intrinsics[i].owner = main; p->intrinsics[i].intrinsic = (uint8_t)i + 1;
        p->intrinsics[i].kinds = i >= 3 ? K_OBJECT : i ? K_NUMBER : K_UNDEFINED;
        if (i + 1 == I_REQUIRE || i + 1 == I_DATE || i + 1 == I_REGEXP || i + 1 == I_SYMBOL || i + 1 == I_PROXY) p->intrinsics[i].kinds = K_FUNCTION;
    }
    for (int i = 0; i < 2; i++) {
        const char* name = i ? "MvpLmdMap" : "MvpLmdObject";
        p->families[i].type_name = {name, strlen(name)};
        p->families[i].struct_kind = LMD_TYPE_MAP;
        p->families[i].extension = &mvp_lmd_plain_extension;
        p->families[i].extension_data = p;
        p->roots[i] = (TypeMap*)alloc_type(p->frontend->pool, LMD_TYPE_MAP, sizeof(TypeMap));
        if (!p->roots[i]) return false;
        p->roots[i]->type_id = LMD_TYPE_MAP;
        p->roots[i]->nominal = &p->families[i];
        p->roots[i]->is_nominal = true;
        p->roots[i]->type_index = -1;
    }
    if (p->module_realm || p->realm_program) {
        // module graphs share one complete supported realm; later imports cannot replace intrinsic identities.
        admit_shared_realm(p, p->frontend->ast_root);
    }
    LmdWalk gather = {p, main, NULL, false}; walk(p->frontend->ast_root, &gather);
    gather.facts = true; walk(p->frontend->ast_root, &gather);
    if (p->execution_owner && p->execution_owner->host_runtime &&
            p->promises && !p->global_reflection) {
        // native promise continuations obtain their constructors from the shared realm.
        admit_shared_realm(p, p->frontend->ast_root);
    }
    if (p->source_module) {
        for (int index = 0; index < p->source_module->exports->length; index++) {
            auto* exported = (JsSourceModuleExport*)p->source_module->exports->data[index];
            if (exported->local != &p->source_module->default_entry) continue;
            LmdBinding* cell = (LmdBinding*)mem_calloc(1, sizeof(LmdBinding), MEM_CAT_TEMP);
            if (!cell) { diagnostic(p, NULL, "default export cell allocation"); return false; }
            cell->entry = exported->local; cell->owner = main; cell->slot = p->module_slot_count++;
            cell->module_cell = cell->external = cell->assigned = cell->observed = true;
            cell->kinds = K_ANY;
            arraylist_append(p->bindings, cell); break;
        }
    }
    plan_eval_environments(p);
    if (p->suspended_constructor_observed && (p->async_generators || p->async_functions || p->generator_functions)) {
        p->dynamic_source = p->global_receiver = p->global_reflection = p->receiver_abi = p->property_callbacks = true;
    }
    if (p->dynamic_source) {
        // later interpreted or imported source shares these bindings; scheduling still initializes lazily.
        p->timers = p->promises = p->iterators = p->errors = true;
        p->async_generators = p->async_functions = p->generator_functions = true;
    }
    plan_with_bindings(p);
    if (p->property_callbacks) p->array_method_overrides = p->map_method_overrides = p->primitive_method_overrides = true;
    if (p->global_receiver && !p->global_reflection) for (AstNodeId i = 0; i < p->frontend->ast_index.count; i++) {
        AstNode* node = p->frontend->ast_index.nodes[i];
        if (node->node_type != AST_NODE_CALL_EXPR) continue;
        AstCallNode* call = (AstCallNode*)node;
        LmdFunction* target = direct_target(p, call);
        if (!target && (call->callee->node_type == AST_NODE_FUNC_EXPR || call->callee->node_type == AST_NODE_ARROW_FUNC))
            target = function(p, call->callee);
        if (target && target->observes_receiver && !target->home && !target->ast->has_use_strict_directive &&
                !(target->ast->vars && target->ast->vars->strict)) p->global_reflection = true;
        if (call->callee->node_type == AST_NODE_MEMBER_EXPR) {
            String* name = member_spelling((AstFieldNode*)call->callee);
            // forwarding can bind an existing sloppy receiver to the global object through aliases.
            if (named(name, "call") || named(name, "apply") || named(name, "bind")) p->global_reflection = true;
        }
    }
    // host functions capture one canonical global even when source never reflects on it.
    p->global_receiver |= p->host_active;
    if (p->global_receiver) p->global_slot = p->slot_count++;
    if (p->dynamic_source) {
        p->eval_slot = p->slot_count++;
        p->intrinsics[I_EVAL - 1].slot = p->eval_slot;
    }
    if (p->global_reflection) {
        p->global_math_observed = p->math_observed;
        p->global_primitive_method_overrides = p->primitive_method_overrides;
        activate_library(p, p->frontend->ast_root);
        p->primitives = p->errors = p->math_observed = p->buffers = p->shared_buffer = p->date_observed = p->weak_collections = p->weak_lifetimes = p->promises = p->atomics = true;
        p->primitive_method_overrides = true;
        if (p->host_enabled) p->timers = p->promises = true;
        if (p->host_enabled) {
            p->native_host_values = true;
            if (!p->host_active) {
                p->host_active = true; p->host_slot = p->slot_count; p->slot_count += LMD_HOST_COUNT;
            }
            // an earlier single-host read allocated the span without indexing its unused siblings.
            for (int i = I_PERFORMANCE; i <= I_REQUIRE; i++) p->intrinsics[i - 1].slot = p->host_slot + i - I_PERFORMANCE;
        }
        if (p->property_callbacks) p->array_method_overrides = p->map_method_overrides = p->primitive_method_overrides = true;
    }
    if (p->timers) {
        p->timer_slot = p->slot_count; p->slot_count += LMD_TIMER_COUNT;
        for (int i = I_SET_TIMEOUT; i <= I_QUEUE_MICROTASK; i++) {
            p->intrinsics[i - 1].slot = p->timer_slot + i - I_SET_TIMEOUT;
            p->intrinsics[i - 1].kinds = K_FUNCTION;
        }
    }
    // parameter and receiver kinds are still seeds here; decide storage from their solved domains below.
    p->typed_storage |= p->buffers || p->global_reflection || (p->typed_explicit && p->property_callbacks);
    plan_typed_storage(p);
    if (p->library_runtime) plan_library_shapes(p);
    if (p->global_reflection) plan_global_bindings(p);
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i]; b->seed_kinds = b->kinds;
    }
    return !p->diagnostic[0];
}
static void reset_representation_facts(MvpLmdProgram* p) {
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        b->kinds = b->seed_kinds; b->range = {}; b->range_changes = 0;
        b->present_range = {}; b->present_range_changes = 0;
    }
    for (int i = 0; i < p->functions->length; i++) {
        LmdFunction* f = (LmdFunction*)p->functions->data[i];
        f->returns = 0; f->range = {}; f->range_changes = 0;
    }
}
static bool block_initializer_dominates(MvpLmdProgram* p, LmdBinding* b) {
    if (!b->initializer || b->external || b->captured || b->implicit_write ||
            b->entry->is_parameter || b->entry->is_lexical || !b->owner->ast ||
            b->owner->eval_environment || b->owner->ast->is_generator || b->owner->ast->is_async)
        return false;
    AstIndex* index = &p->frontend->ast_index;
    AstNode* declarator = ast_index_parent(index, b->initializer);
    AstNode* declaration = ast_index_parent(index, declarator);
    AstNode* block = ast_index_parent(index, declaration);
    if (!declarator || declarator->node_type != AST_NODE_VARIABLE_DECLARATOR ||
            !declaration || declaration->node_type != AST_NODE_VAR_STAM ||
            !block || block->node_type != AST_NODE_BLOCK) return false;
    AstNodeId block_id = ast_index_find(index, block);
    AstNodeId declaration_id = ast_index_find(index, declaration);
    AstNodeId after = ast_index_subtree_end(index, declaration_id);
    uint32_t count = 0;
    const AstNodeId* uses = ast_index_binding_uses(index,
        ast_index_find_binding_entry(index, b->entry), &count);
    if (!count) return false;
    // D8.2.4: reuse the shared binding-use index. Every observation must follow
    // this complete statement in the same block activation, including loop reentry.
    for (uint32_t i = 0; i < count; i++) {
        AstNodeId id = uses[i];
        if (index->nodes[id] == declarator || index->nodes[id] == ((AstDeclaratorNode*)declarator)->id) continue;
        if (id < after || !ast_index_node_descends(index, id, block_id) ||
                index->owner_functions[id] != index->owner_functions[declaration_id]) return false;
    }
    return true;
}
static int representation_pass(void* opaque) {
    MvpLmdProgram* p = (MvpLmdProgram*)opaque;
    p->typed_named_kinds = K_UNDEFINED;
    p->typed_named_range = {}; p->typed_named_range_changes = 0;
    for (int i = 0; i < p->functions->length; i++) {
        LmdFunction* f = (LmdFunction*)p->functions->data[i];
        f->array = {}; f->array_changes = 0;
    }
    // only stable declarations used exclusively as direct callees have a closed incoming domain.
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        bool initialized_function = !b->external && b->dominated && b->writes == 1 && b->initializer &&
            (b->initializer->node_type == AST_NODE_FUNC_EXPR || b->initializer->node_type == AST_NODE_ARROW_FUNC);
        if (initialized_function) {
            AstFuncNode* initialized = (AstFuncNode*)b->initializer;
            // named expressions require the boxed entry's self identity for their name scope.
            if (initialized->vars && initialized->vars->parent && initialized->vars->parent->is_function_name_scope)
                initialized_function = false;
        }
        if (initialized_function) b->target = function(p, b->initializer);
        if (b->target && !b->target->eval_environment && !b->target->arguments_binding && !b->target->ast->is_generator && !b->target->ast->is_async && !b->target->complex_params && !b->target->observes_receiver && !b->target->captures && !b->target->lexical_receiver &&
                !b->target->owns_captures && !b->assigned && (!b->observed || b->closed_global) && b->entry->node &&
                (b->entry->node->node_type == AST_NODE_FUNC || initialized_function) && !b->entry->is_annex_b_companion)
            b->target->closed_calls = true;
    }
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        // Closed calls constrain incoming arguments, not later iterator/pattern writes.
        if (b->entry->is_parameter && b->owner->closed_calls && !b->implicit_write) b->kinds = 0;
        b->array = {};
        b->array_changes = 0;
        if (b->implicit_write) { b->range.state = 2; b->array.length.state = 2; }
        if (b->implicit_write || (b->entry->is_parameter && !b->owner->closed_calls)) b->array.lanes = LMD_ARRAY_UNKNOWN;
    }
    // refine kinds once with solved extents; unknown bounds keep number-or-undefined.
    for (int pass = 0; pass < 2; pass++) {
        if (pass) {
            p->typed_named_kinds = K_UNDEFINED;
            memset(p->literal_kinds, 0, p->frontend->ast_index.count * sizeof(unsigned));
            for (int i = 0; i < p->bindings->length; i++) {
                LmdBinding* b = (LmdBinding*)p->bindings->data[i];
                b->kinds = b->entry->is_parameter && b->owner->closed_calls && !b->implicit_write ? 0 : b->seed_kinds;
            }
            for (int i = 0; i < p->functions->length; i++)
                ((LmdFunction*)p->functions->data[i])->returns = 0;
        }
        LmdTypes types = {p, true};
        while (types.changed) { types.changed = false; type_walk(p->frontend->ast_root, &types); }
        // unresolved recursive domains remain generic; propagate that widening before selecting ABIs.
        for (int i = 0; i < p->functions->length; i++) {
            LmdFunction* f = (LmdFunction*)p->functions->data[i];
            if (!f->returns) { f->returns = K_ANY; types.changed = true; }
            if (!f->array.lanes) { f->array.lanes = LMD_ARRAY_UNKNOWN; types.changed = true; }
        }
        for (int i = 0; i < p->bindings->length; i++) {
            LmdBinding* b = (LmdBinding*)p->bindings->data[i];
            if (!b->kinds) { b->kinds = K_ANY; types.changed = true; }
            if (!b->array.lanes) { b->array.lanes = LMD_ARRAY_UNKNOWN; types.changed = true; }
        }
        while (types.changed) { types.changed = false; type_walk(p->frontend->ast_root, &types); }
        if (pass) break;
        LmdRanges ranges = {p, NULL, true};
        while (ranges.changed) { ranges.changed = false; range_walk(p->frontend->ast_root, &ranges); }
        // unresolved cycles cannot establish integer admission; propagate widening before choosing carriers.
        for (int i = 0; i < p->bindings->length; i++) {
            LmdBinding* b = (LmdBinding*)p->bindings->data[i];
            if (!b->range.state) { b->range.state = 2; ranges.changed = true; }
            if (b->kinds == (K_NUMBER | K_UNDEFINED) && !b->present_range.state)
                { b->present_range.state = 2; ranges.changed = true; }
            if (!b->array.length.state) { b->array.length.state = 2; ranges.changed = true; }
        }
        for (int i = 0; i < p->functions->length; i++) {
            LmdFunction* f = (LmdFunction*)p->functions->data[i];
            if (!f->range.state) { f->range.state = 2; ranges.changed = true; }
            if (!f->array.length.state) { f->array.length.state = 2; ranges.changed = true; }
        }
        while (ranges.changed) { ranges.changed = false; range_walk(p->frontend->ast_root, &ranges); }
    }
    bool typed_aggregates = false;
    if (p->typed_explicit && !p->typed_storage) for (AstNodeId i = 0; i < p->frontend->ast_index.count; i++) {
        AstNode* node = p->frontend->ast_index.nodes[i];
        AstNode* value = NULL;
        if (node->node_type == AST_NODE_PROPERTY) value = ((AstPropertyNode*)node)->value;
        else if (node->node_type == AST_NODE_FIELD) value = ((AstClassFieldNode*)node)->value;
        else if (node->node_type == AST_NODE_ASSIGN) {
            AstNode* target = ((AstAssignNode*)node)->left;
            if ((target->node_type == AST_NODE_MEMBER_EXPR || target->node_type == AST_NODE_INDEX_EXPR) &&
                    kind(p, ((AstFieldNode*)target)->object) != K_TYPED_ARRAY)
                value = ((AstAssignNode*)node)->right;
        } else if (node->node_type == AST_NODE_ARRAY) {
            for (AstNode* item = ((AstArrayNode*)node)->item; item; item = item->next)
                if (kind(p, item) & K_TYPED_ARRAY) { typed_aggregates = true; break; }
        }
        if (value && (kind(p, value) & K_TYPED_ARRAY)) typed_aggregates = true;
    }
    auto observes_typed = [&](AstNode* value) {
        unsigned possible = kind(p, value);
        if (!(possible & (K_TYPED_ARRAY | (typed_aggregates ? K_ARRAY | K_OBJECT | K_MAP | K_FUNCTION : 0)))) return false;
        AstNode* path[48];
        // string-unit results are primitive too; only actual index admission needs the numeric-only proof.
        return !primitive_property_key(p, value, path);
    };
    if (p->typed_explicit && !p->typed_storage) for (AstNodeId i = 0; i < p->frontend->ast_index.count; i++) {
        AstNode* node = p->frontend->ast_index.nodes[i];
        if (node->node_type == AST_NODE_IDENT) {
            LmdBinding* binding = identifier_binding(p, (AstIdentNode*)node);
            if (!binding || binding->intrinsic != I_JSON) continue;
            AstNode* use = ast_index_parent(&p->frontend->ast_index, node);
            // stringify recursively observes stored views; an escaped namespace can expose it later.
            if (use && (use->node_type == AST_NODE_MEMBER_EXPR || use->node_type == AST_NODE_INDEX_EXPR) &&
                    ((AstFieldNode*)use)->object == node && named(member_spelling((AstFieldNode*)use), "parse")) continue;
        } else if (node->node_type == AST_NODE_NEW_EXPR || node->node_type == AST_NODE_CALL_EXPR) {
            AstCallNode* call = (AstCallNode*)node;
            LmdBinding* ctor = call->callee->node_type == AST_NODE_IDENT
                ? identifier_binding(p, (AstIdentNode*)call->callee) : NULL;
            if (node->node_type == AST_NODE_NEW_EXPR && ctor && typed_intrinsic(ctor->intrinsic) >= 0) {
                if (!p->property_callbacks && !p->array_method_overrides &&
                        (!call->arguments || kind(p, call->arguments) == K_NUMBER)) continue;
            } else {
                // source callees share the solver; native and escaped calls can observe the physical view.
                if (direct_target(p, call)) continue;
                bool typed_argument = false;
                for (AstNode* argument = call->arguments; argument; argument = argument->next)
                    typed_argument |= observes_typed(argument);
                if (typed_aggregates && (call->callee->node_type == AST_NODE_MEMBER_EXPR ||
                        call->callee->node_type == AST_NODE_INDEX_EXPR)) {
                    AstFieldNode* method = (AstFieldNode*)call->callee;
                    String* spelling = member_spelling(method);
                    bool native_sequence = spelling && (mvp_lmd_builtin_method(spelling, LMD_TYPE_ARRAY) ||
                        mvp_lmd_builtin_method(spelling, LMD_TYPE_STRING) || named(spelling, "toLocaleString"));
                    // the fill member check below validates the raw numeric lane separately.
                    bool raw_fill = kind(p, method->object) == K_TYPED_ARRAY && named(spelling, "fill");
                    if (native_sequence && !raw_fill) typed_argument |= observes_typed(method->object);
                }
                if (!typed_argument) continue;
            }
        } else if (node->node_type == AST_NODE_BINARY || node->node_type == AST_NODE_UNARY ||
                (node->node_type == AST_NODE_ASSIGN && ((AstAssignNode*)node)->op != OPERATOR_ASSIGN)) {
            AstNode* left = NULL; AstNode* right = NULL;
            Operator op;
            if (node->node_type == AST_NODE_BINARY) {
                AstBinaryNode* binary = (AstBinaryNode*)node;
                op = binary->op; left = binary->left; right = binary->right;
            } else if (node->node_type == AST_NODE_UNARY) {
                AstUnaryNode* unary = (AstUnaryNode*)node;
                op = unary->op; left = unary->operand;
                if (op == OPERATOR_NOT || op == OPERATOR_JS_TYPEOF || op == OPERATOR_JS_VOID || op == OPERATOR_JS_DELETE) continue;
            } else {
                AstAssignNode* assignment = (AstAssignNode*)node;
                if (assignment->op == OPERATOR_JS_AND_ASSIGN || assignment->op == OPERATOR_JS_OR_ASSIGN ||
                        assignment->op == OPERATOR_JS_NULLISH_ASSIGN) continue;
                op = compound_operator(assignment->op); left = assignment->left; right = assignment->right;
            }
            if (op == OPERATOR_JS_STRICT_EQ || op == OPERATOR_JS_STRICT_NE || op == OPERATOR_AND ||
                    op == OPERATOR_OR || op == OPERATOR_JS_NULLISH_COALESCE) continue;
            // coercion and reflective operators require the complete typed prototype and integer-index protocol.
            if (!observes_typed(left) && !observes_typed(right)) continue;
        } else if (node->node_type == JS_AST_NODE_TEMPLATE_LITERAL) {
            // interpolation performs ToString too; tagged substitutions can escape to native callees.
            bool typed_value = false;
            for (AstNode* value = ((JsTemplateLiteralNode*)node)->expressions; value; value = value->next)
                typed_value |= observes_typed(value);
            if (!typed_value) continue;
        } else if (node->node_type == AST_NODE_FOR_IN_STAM || node->node_type == AST_NODE_FOR_OF_STAM) {
            if (!(kind(p, ((JsForOfNode*)node)->right) & K_TYPED_ARRAY)) continue;
        } else if (node->node_type == AST_NODE_SPREAD) {
            if (!(kind(p, ((AstSpreadNode*)node)->argument) & K_TYPED_ARRAY)) continue;
        } else if (node->node_type == JS_AST_NODE_TEMPLATE_LITERAL) {
            bool observed = false;
            for (AstNode* value = ((JsTemplateLiteralNode*)node)->expressions; value; value = value->next)
                observed |= observes_typed(value);
            if (!observed) continue;
        } else if (node->node_type == AST_NODE_VARIABLE_DECLARATOR || node->node_type == AST_NODE_ASSIGN) {
            AstNode* pattern = node->node_type == AST_NODE_VARIABLE_DECLARATOR ?
                ((AstDeclaratorNode*)node)->id : ((AstAssignNode*)node)->left;
            AstNode* source = node->node_type == AST_NODE_VARIABLE_DECLARATOR ?
                ((AstDeclaratorNode*)node)->init : ((AstAssignNode*)node)->right;
            bool numeric_store = pattern && (pattern->node_type == AST_NODE_MEMBER_EXPR || pattern->node_type == AST_NODE_INDEX_EXPR) &&
                (kind(p, ((AstFieldNode*)pattern)->object) & K_TYPED_ARRAY) && observes_typed(source);
            if (!numeric_store && (!pattern || (pattern->node_type != AST_NODE_MAP_PATTERN &&
                    pattern->node_type != AST_NODE_ARRAY_PATTERN && pattern->node_type != AST_NODE_ARRAY) ||
                    !(kind(p, source) & K_TYPED_ARRAY))) continue;
        } else if (node->node_type == AST_NODE_MEMBER_EXPR || node->node_type == AST_NODE_INDEX_EXPR) {
            AstFieldNode* field = (AstFieldNode*)node;
            unsigned owner = kind(p, field->object);
            String* spelling = member_spelling(field);
            AstNode* parent = ast_index_parent(&p->frontend->ast_index, node);
            bool method_call = parent && parent->node_type == AST_NODE_CALL_EXPR &&
                ((AstCallNode*)parent)->callee == node;
            LmdBinding* receiver = field->object->node_type == AST_NODE_IDENT
                ? identifier_binding(p, (AstIdentNode*)field->object) : NULL;
            bool reflection = receiver && receiver->intrinsic == I_OBJECT &&
                (named(spelling, "keys") || named(spelling, "values") || named(spelling, "entries") || named(spelling, "hasOwn"));
            if (reflection) {
                // an escaped projection can later receive any typed view; direct calls use solved argument kinds.
                if (method_call && !(kind(p, ((AstCallNode*)parent)->arguments) & K_TYPED_ARRAY)) continue;
            } else if (owner && !(owner & K_TYPED_ARRAY)) {
                continue; // a proven ordinary Array/String member does not expose a typed view.
            } else if (parent && parent->node_type == AST_NODE_UNARY &&
                    ((AstUnaryNode*)parent)->op == OPERATOR_JS_DELETE) {
                // raw attribute fallback does not implement integer-indexed Delete.
            } else if (!spelling) {
                unsigned key = kind(p, field->property);
                if (field->computed && !method_call && key && !(key & ~(K_NUMBER | K_UNDEFINED))) continue;
                AstNode* path[48];
                if (field->computed && !method_call && !p->property_callbacks && !p->array_method_overrides &&
                        primitive_property_key(p, field->property, path, 0, K_NUMBER | K_UNDEFINED)) continue;
            } else if (named(spelling, "fill")) {
                AstCallNode* call = method_call ? (AstCallNode*)parent : NULL;
                bool raw_fill = owner == K_TYPED_ARRAY && call && !call->interp_has_spread_args;
                for (AstNode* argument = call ? call->arguments : NULL; raw_fill && argument; argument = argument->next) {
                    unsigned value = kind(p, argument);
                    raw_fill = value && !(value & ~(K_NUMBER | K_UNDEFINED));
                }
                // the existing fill lane preserves conversion order for primitive numeric arguments.
                if (raw_fill) continue;
            } else {
                // all other names may expose inherited methods or readonly intrinsic properties.
                int64_t key = mvp_lmd_string_key(spelling, true);
                if (key >= -1 || key == -3) continue;
                if (owner != K_TYPED_ARRAY && !named(spelling, "BYTES_PER_ELEMENT") && !named(spelling, "constructor") &&
                        !js_builtin_catalog_find(JS_BUILTIN_OWNER_TYPED_ARRAY_PROTOTYPE_METHOD, spelling->chars, spelling->len) &&
                        !js_builtin_catalog_find(JS_BUILTIN_OWNER_TYPED_ARRAY_STUB_METHOD, spelling->chars, spelling->len) &&
                        !js_builtin_catalog_find(JS_BUILTIN_OWNER_TYPED_ARRAY_ACCESSOR, spelling->chars, spelling->len) &&
                        !js_builtin_catalog_find(JS_BUILTIN_OWNER_OBJECT_PROTOTYPE_METHOD, spelling->chars, spelling->len))
                    continue; // unrelated host and user names use the raw carrier's ordinary attribute face.
            }
        } else continue;
        // observable views and unresolved domains retain full storage; numeric loops retain raw lanes.
        // promotion is monotone, so replay the existing solver once before choosing ABIs.
        p->typed_storage = true;
        plan_typed_storage(p);
        plan_library_shapes(p);
        if (p->diagnostic[0]) return false;
        reset_representation_facts(p);
        return representation_pass(p);
    }
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        if (!b->kinds) b->kinds = K_ANY;
        if (!b->dominated && (b->kinds == K_NUMBER || b->kinds == K_BOOL ||
                b->kinds == (K_NUMBER | K_UNDEFINED)))
            b->dominated = block_initializer_dominates(p, b);
        b->native = !b->external && (b->kinds == K_NUMBER || b->kinds == K_BOOL ||
            (!b->entry->is_parameter && b->kinds == (K_NUMBER | K_UNDEFINED))) &&
            (b->entry->is_parameter ? b->owner->closed_calls : b->initializer && b->dominated);
        b->integer = b->native && (b->kinds == K_NUMBER ? b->range.state == 1 :
            b->kinds == (K_NUMBER | K_UNDEFINED) && b->present_range.state == 1);
    }
    for (int i = 1; i < p->functions->length; i++) {
        LmdFunction* f = (LmdFunction*)p->functions->data[i];
        f->direct_args = f->closed_calls && !f->reflected_entry && ast_linked_node_count(f->ast->params) + 2 <= LAMBDA_MAX_FUNCTION_ARGS;
        f->native = f->direct_args && (f->returns == K_NUMBER || f->returns == K_BOOL);
        f->integer = f->native && f->returns == K_NUMBER && f->range.state == 1;
    }
    // keep closed native numeric regions coherent instead of converting invariant operands in a float loop.
    for (int i = 1; i < p->functions->length; i++) {
        LmdFunction* f = (LmdFunction*)p->functions->data[i];
        if (!f->native) continue;
        bool integral = f->returns != K_NUMBER || f->integer;
        bool array_region = false;
        for (int j = 0; j < p->bindings->length; j++) {
            LmdBinding* b = (LmdBinding*)p->bindings->data[j];
            if (b->owner == f && b->native && b->kinds == K_NUMBER) integral &= b->integer;
            if (b->owner == f && (b->kinds == K_ARRAY || b->kinds == K_TYPED_ARRAY)) array_region = true;
        }
        // array indices retain their proven integer carrier beside floating accumulators.
        f->integer_region = integral || array_region;
        if (!f->integer_region) for (int j = 0; j < p->bindings->length; j++) {
            LmdBinding* b = (LmdBinding*)p->bindings->data[j];
            if (b->owner == f) b->integer = false;
        }
    }
    return true;
}
static bool plan_shape_field(MvpLmdProgram* p, TypeMap* shape, String* key, TypeId tid, uint8_t flags) {
    if (!key) return false;
    key = name_pool_create_string(p->frontend->name_pool, key);
    if (!key) return false;
    if (typemap_shape_lookup_last(shape, key->chars, key->len)) return true;
    ShapeEntry* field = (ShapeEntry*)pool_calloc(p->frontend->pool, sizeof(ShapeEntry) + sizeof(StrView));
    if (!field) return false;
    field->name = (StrView*)(field + 1); *field->name = {key->chars, key->len};
    field->key_kind = NAME_KEY_STRING; field->flags = flags;
    field->name_hash = typemap_name_hash(key->chars, key->len);
    shape_entry_set_type(field, type_info[tid].type);
    field->byte_offset = shape->byte_size; shape->byte_size += shape_entry_storage_size(field);
    if (shape->last) shape->last->chain_next = field; else shape->shape = field;
    shape->last = field; shape->length++;
    return true;
}
static void plan_objects(AstNode* node, void* opaque) {
    MvpLmdProgram* p = (MvpLmdProgram*)opaque;
    // only ordinary function nodes can provide a constructor template.
    LmdFunction* constructor = p->function_construction &&
        (node->node_type == AST_NODE_FUNC || node->node_type == AST_NODE_FUNC_EXPR) ? function(p, node) : NULL;
    if (constructor && !constructor->home && !constructor->object_home &&
            !constructor->complex_params && !constructor->owns_captures && !constructor->eval_environment &&
            !constructor->ast->is_generator && !constructor->ast->is_async &&
            constructor->ast->body->node_type == AST_NODE_BLOCK &&
            constructor_layout_reusable(p, constructor->ast->body)) {
        AstNode* first = ((AstBlockNode*)constructor->ast->body)->statements;
        int count = ast_linked_node_count(first);
        bool exact = count > 0 && count <= 4 &&
            !js_ast_collect_parameter_facts(constructor->ast->params).has_duplicate_param_names;
        for (AstNode* n = first; exact && n; n = n->next) {
            AstAssignNode* assign = (AstAssignNode*)((AstExprStmtNode*)n)->expression;
            AstNode* seed = assign->right;
            LmdBinding* parameter = seed->node_type == AST_NODE_IDENT
                ? identifier_binding(p, (AstIdentNode*)seed) : NULL;
            exact = !named(member_spelling((AstFieldNode*)assign->left), "__proto__") &&
                ((seed->node_type == AST_NODE_LITERAL && (kind(p, seed) &
                    (K_NULL | K_UNDEFINED | K_BOOL | K_NUMBER | K_STRING))) ||
                    (parameter && parameter->owner == constructor && parameter->entry->is_parameter));
        }
        if (exact) {
            Pool* pool = p->frontend->pool;
            TypeMap* shape = (TypeMap*)alloc_type(pool, LMD_TYPE_MAP, sizeof(TypeMap));
            LmdObjectPlan* plan = (LmdObjectPlan*)pool_calloc(pool, sizeof(LmdObjectPlan));
            if (!shape || !plan) { diagnostic(p, node, "source constructor layout allocation"); return; }
            *plan = {node, shape, shape}; shape->type_index = -1;
            shape->is_nominal = true; shape->is_shared_constructor_shape = true;
            int index = 0;
            for (AstNode* n = first; exact && n; n = n->next) {
                AstAssignNode* assign = (AstAssignNode*)((AstExprStmtNode*)n)->expression;
                String* key = member_spelling((AstFieldNode*)assign->left);
                exact = key && !typemap_shape_lookup_last(shape, key->chars, key->len) &&
                    plan_shape_field(p, shape, key, LMD_TYPE_ANY, 0);
                plan->initializers[index++] = assign->right;
            }
            if (exact) {
                typemap_hash_build(shape, pool);
                plan->constructor = constructor;
                if (!arraylist_append(p->objects, plan))
                    diagnostic(p, node, "source constructor plan allocation");
            }
        }
    }
    if (node->node_type == AST_NODE_MAP || node->node_type == AST_NODE_OBJECT_LITERAL) {
        Pool* pool = p->frontend->pool;
        TypeMap* shape = (TypeMap*)alloc_type(pool, LMD_TYPE_MAP, sizeof(TypeMap));
        if (!shape) { diagnostic(p, node, "literal shape allocation"); return; }
        shape->nominal = &p->families[0]; shape->is_nominal = true; shape->type_index = -1;
        bool exact = true;
        for (AstNode* n = ((AstMapNode*)node)->properties; n; n = n->next) {
            if (n->node_type != AST_NODE_PROPERTY) { exact = false; break; }
            AstPropertyNode* prop = (AstPropertyNode*)n;
            if (prop->computed || prop->is_getter || prop->is_setter || prop->key->node_type != AST_NODE_IDENT) { exact = false; break; }
            String* key = name_pool_create_string(p->frontend->name_pool, ((AstIdentNode*)prop->key)->name);
            if (named(key, "__proto__")) { exact = false; break; }
            if (!key || typemap_shape_lookup_last(shape, key->chars, key->len)) { exact = false; break; }
            unsigned k = kind(p, prop->value);
            TypeId tid = k == K_UNDEFINED ? LMD_TYPE_UNDEFINED : k == K_NULL ? LMD_TYPE_NULL :
                k == K_BOOL ? LMD_TYPE_BOOL : k == K_STRING ? LMD_TYPE_STRING : k == K_ARRAY ? LMD_TYPE_ARRAY :
                k == K_OBJECT || k == K_MAP || ((k & K_OBJECT) && !(k & ~(K_OBJECT | K_NULL | K_UNDEFINED)))
                    ? LMD_TYPE_MAP : k == K_FUNCTION ? LMD_TYPE_FUNC : LMD_TYPE_ANY;
            if (k == K_NUMBER) tid = range(p, prop->value).state == 1 ? LMD_TYPE_INT : LMD_TYPE_FLOAT;
            if (tid == LMD_TYPE_ANY) { exact = false; break; }
            if (!plan_shape_field(p, shape, key, tid, 0)) { exact = false; break; }
        }
        if (exact) {
            typemap_hash_build(shape, pool);
            LmdObjectPlan* plan = (LmdObjectPlan*)pool_calloc(pool, sizeof(LmdObjectPlan));
            if (!plan) diagnostic(p, node, "literal plan allocation");
            else {
                *plan = {node, shape, shape}; arraylist_append(p->objects, plan);
                AstNode* parent = ast_index_parent(&p->frontend->ast_index, node);
                AstNode* target = parent && parent->node_type == AST_NODE_ASSIGN &&
                    ((AstAssignNode*)parent)->op == OPERATOR_ASSIGN && ((AstAssignNode*)parent)->right == node
                    ? ((AstAssignNode*)parent)->left : NULL;
                if (target && target->node_type == AST_NODE_MEMBER_EXPR &&
                        named(member_spelling((AstFieldNode*)target), "prototype")) {
                    AstNode* owner = ((AstFieldNode*)target)->object;
                    LmdBinding* binding = owner->node_type == AST_NODE_IDENT
                        ? identifier_binding(p, (AstIdentNode*)owner) : NULL;
                    if (binding && binding->target && !binding->target->home) {
                        plan->prototype_owner = binding->target;
                    }
                }
            }
        }
    }
    js_ast_visit_children(node, plan_objects, opaque);
}
struct LmdShapes { MvpLmdProgram* program; LmdFunction* owner; bool changed; };
static void shape_hint(LmdShapes* state, LmdShapeSet* target, LmdObjectPlan** first, AstNode* value) {
    state->changed |= shape_union(target, object_shapes(state->program, value));
    *first = target->plans[0];
}
static void shape_walk(AstNode* node, void* opaque) {
    LmdShapes* state = (LmdShapes*)opaque;
    MvpLmdProgram* p = state->program;
    LmdFunction* f = function(p, node);
    if (f) {
        LmdShapes inner = {p, f, false};
        if (f->ast->body->node_type != AST_NODE_BLOCK) shape_hint(&inner, &f->shapes, &f->shape_hint, f->ast->body);
        js_ast_visit_children(node, shape_walk, &inner);
        state->changed |= inner.changed; return;
    }
    if (node->node_type == AST_NODE_RETURN_STAM && state->owner)
        shape_hint(state, &state->owner->shapes, &state->owner->shape_hint, ((AstReturnNode*)node)->value);
    if (node->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)node;
        f = direct_target(p, call);
        AstNode* actual = call->arguments;
        for (AstNode* formal = f && f->closed_calls ? f->ast->params : NULL;
                formal && actual; formal = formal->next, actual = actual->next) {
            LmdBinding* b = binding(p, js_ast_parameter_binding_identifier(formal)->entry);
            shape_hint(state, &b->shapes, &b->shape_hint, actual);
        }
    }
    if (node->node_type == AST_NODE_ASSIGN) {
        AstAssignNode* assignment = (AstAssignNode*)node;
        if (assignment->op == OPERATOR_ASSIGN && assignment->left->node_type == AST_NODE_IDENT) {
            LmdBinding* b = identifier_binding(p, (AstIdentNode*)assignment->left);
            if (b) shape_hint(state, &b->shapes, &b->shape_hint, assignment->right);
        }
    }
    if (node->node_type == AST_NODE_VARIABLE_DECLARATOR) {
        AstDeclaratorNode* d = (AstDeclaratorNode*)node;
        if (d->id->node_type == AST_NODE_IDENT) {
            LmdBinding* b = identifier_binding(p, (AstIdentNode*)d->id);
            shape_hint(state, &b->shapes, &b->shape_hint, d->init);
        }
    }
    js_ast_visit_children(node, shape_walk, state);
}
static void plan_deleted_shapes(MvpLmdProgram* p) {
    if (!p->deletes_properties) return;
    Pool* pool = p->frontend->pool;
    for (AstNodeId id = 0; id < p->frontend->ast_index.count; id++) {
        AstNode* node = p->frontend->ast_index.nodes[id];
        if (node->node_type != AST_NODE_UNARY || ((AstUnaryNode*)node)->op != OPERATOR_JS_DELETE) continue;
        AstNode* operand = ((AstUnaryNode*)node)->operand;
        // identifier/value deletion has no receiver shape to predict.
        if (operand->node_type != AST_NODE_MEMBER_EXPR && operand->node_type != AST_NODE_INDEX_EXPR) continue;
        AstFieldNode* member = (AstFieldNode*)operand;
        String* key = member_spelling(member);
        if (!key) continue;
        LmdShapeSet sources = object_shapes(p, member->object);
        for (int i = 0; i < sources.count; i++) {
            LmdObjectPlan* source = sources.plans[i];
            if (source->constructor || source->literal->node_type == AST_NODE_UNARY) continue;
            ShapeEntry* removed = typemap_hash_lookup(source->blueprint, key->chars, key->len);
            if (!removed || source->blueprint->length <= 1) continue;
            int variants = 0;
            bool planned = false;
            for (int j = 0; j < p->objects->length; j++) {
                LmdObjectPlan* other = (LmdObjectPlan*)p->objects->data[j];
                if (other->literal->node_type != AST_NODE_UNARY) continue;
                LmdDeletedShapePlan* prior = (LmdDeletedShapePlan*)other;
                if (prior->source != source) continue;
                variants++; planned |= prior->removed == removed;
            }
            if (planned) continue;
            // predict removal and reinsertion with the original lane, within the existing four-shape budget.
            for (int reinsert = 0; reinsert < 2 && variants < 3; reinsert++) {
                if (reinsert && removed == source->blueprint->last) continue;
                TypeMap* shape = (TypeMap*)alloc_type(pool, LMD_TYPE_MAP, sizeof(TypeMap));
                LmdDeletedShapePlan* plan = (LmdDeletedShapePlan*)pool_calloc(pool, sizeof(LmdDeletedShapePlan));
                if (!shape || !plan) { diagnostic(p, node, "deleted shape allocation"); return; }
                shape->nominal = source->blueprint->nominal; shape->is_nominal = true; shape->type_index = -1;
                FOR_EACH_MAP_FIELD(source->blueprint, field) {
                    if (field == removed) continue;
                    String* name = name_pool_create_len(p->frontend->name_pool, field->name->str, field->name->length);
                    if (!plan_shape_field(p, shape, name, field->type->type_id, field->flags)) {
                        diagnostic(p, node, "deleted field allocation"); return;
                    }
                }
                if (reinsert && !plan_shape_field(p, shape, key, removed->type->type_id, removed->flags)) {
                    diagnostic(p, node, "reinserted field allocation"); return;
                }
                typemap_hash_build(shape, pool);
                plan->object = {node, shape, shape}; plan->source = source; plan->removed = removed;
                arraylist_append(p->objects, plan); variants++;
            }
        }
    }
}
// only a fixed, receiver-unobserving constructor can initialize existing scalar-field slots.
static LmdObjectPlan* scalar_constructor_plan(MvpLmdProgram* p, AstNode* node,
        const LmdValue* native_values) {
    if (!node) return NULL;
    AstNode* original = node;
    LmdFunction* factory = NULL;
    if (node->node_type == AST_NODE_CALL_EXPR) {
        if (native_values) return NULL;
        factory = direct_target(p, (AstCallNode*)node);
        if (!factory || !factory->closed_calls || factory->owns_captures ||
                ast_linked_node_count(factory->ast->params) > 32 ||
                js_ast_collect_parameter_facts(factory->ast->params).has_duplicate_param_names) return NULL;
        node = factory->ast->body;
        if (node->node_type == AST_NODE_BLOCK) {
            node = ((AstBlockNode*)node)->statements;
            if (!node || node->next || node->node_type != AST_NODE_RETURN_STAM) return NULL;
            node = ((AstReturnNode*)node)->value;
        }
        if (!node || node->node_type != AST_NODE_NEW_EXPR) return NULL;
        // the initial cross-return proof admits forwarding and literals, without extra effects.
        for (AstNode* arg = ((AstCallNode*)node)->arguments; arg; arg = arg->next) {
            LmdBinding* parameter = arg->node_type == AST_NODE_IDENT
                ? identifier_binding(p, (AstIdentNode*)arg) : NULL;
            if (arg->node_type != AST_NODE_LITERAL && (!parameter || parameter->owner != factory ||
                    !parameter->entry->is_parameter)) return NULL;
        }
    }
    if (node->node_type != AST_NODE_NEW_EXPR) return NULL;
    AstCallNode* call = (AstCallNode*)node;
    if (call->callee->node_type != AST_NODE_IDENT) return NULL;
    LmdBinding* name = identifier_binding(p, (AstIdentNode*)call->callee);
    MvpLmdClass* cls = class_plan(p, NULL, name);
    if (!cls || !cls->reusable_layout) return NULL;
    LmdFunction* constructor = (LmdFunction*)p->functions->data[cls->constructor_id];
    if (constructor->captures || constructor->owns_captures ||
            ast_linked_node_count(constructor->ast->params) > 32) return NULL;
    AstNode* statements = ((AstBlockNode*)constructor->ast->body)->statements;
    int count = ast_linked_node_count(statements);
    if (!count || count > 4) return NULL;
    Pool* pool = p->frontend->pool;
    LmdObjectPlan* plan = (LmdObjectPlan*)pool_calloc(pool,
        factory ? sizeof(LmdScalarFactoryPlan) : sizeof(LmdObjectPlan));
    TypeMap* shape = (TypeMap*)alloc_type(pool, LMD_TYPE_MAP, sizeof(TypeMap));
    if (!plan || !shape) { diagnostic(p, node, "scalar constructor plan allocation"); return NULL; }
    *plan = {original, shape, shape}; plan->constructor = constructor;
    if (native_values) {
        shape->nominal = &cls->nominal; shape->is_nominal = true;
        shape->is_shared_constructor_shape = true;
    }
    if (LmdScalarFactoryPlan* extra = scalar_factory_plan(plan)) {
        extra->function = factory; extra->value = node;
    }
    int index = 0;
    for (AstNode* statement = statements; statement; statement = statement->next, index++) {
        AstAssignNode* assign = (AstAssignNode*)(statement->node_type == AST_NODE_EXPR_STMT
            ? ((AstExprStmtNode*)statement)->expression : statement);
        String* key = member_spelling((AstFieldNode*)assign->left);
        if (!key || typemap_shape_lookup_last(shape, key->chars, key->len)) return NULL;
        AstNode* value = assign->right;
        const LmdValue* native_value = NULL;
        // only an actual expression or literal may seed caller-side inference; formal ASTs cannot.
        if (value->node_type == AST_NODE_LITERAL) plan->initializers[index] = value;
        else if (value->node_type == AST_NODE_IDENT) {
            LmdBinding* parameter = identifier_binding(p, (AstIdentNode*)value);
            AstNode* actual = call->arguments;
            int ai = 0;
            for (AstNode* formal = constructor->ast->params; formal; formal = formal->next) {
                if (parameter && parameter->entry == js_ast_parameter_binding_identifier(formal)->entry) {
                    plan->initializers[index] = actual;
                    if (native_values && actual) native_value = &native_values[ai];
                }
                if (actual) { actual = actual->next; ai++; }
            }
        }
        TypeId type = LMD_TYPE_ANY;
        if (native_values) {
            // only concrete scalar lanes are admitted; unknown values keep ordinary construction.
            if (native_value && !is_boxed(*native_value) && !native_value->number_present) {
                unsigned k = semantic(*native_value);
                type = k == K_NUMBER ? (native_value->integer ? LMD_TYPE_INT : LMD_TYPE_FLOAT) :
                    k == K_BOOL ? LMD_TYPE_BOOL : LMD_TYPE_ANY;
            } else if (value->node_type == AST_NODE_LITERAL) {
                unsigned k = kind(p, value);
                type = k == K_NUMBER ? (range(p, value).state == 1 ? LMD_TYPE_INT : LMD_TYPE_FLOAT) :
                    k == K_BOOL ? LMD_TYPE_BOOL : LMD_TYPE_ANY;
            }
            if (type == LMD_TYPE_ANY) return NULL;
        }
        if (!plan_shape_field(p, shape, key, type, 0)) return NULL;
        AstNode* seed = plan->initializers[index];
        if (factory && seed && seed->node_type == AST_NODE_IDENT) {
            AstNode* actual = ((AstCallNode*)original)->arguments;
            plan->initializers[index] = NULL;
            for (AstNode* formal = factory->ast->params; formal; formal = formal->next) {
                if (((AstIdentNode*)seed)->entry == js_ast_parameter_binding_identifier(formal)->entry)
                    plan->initializers[index] = actual;
                if (actual) actual = actual->next;
            }
        }
    }
    typemap_hash_build(shape, pool);
    return plan;
}
static void plan_scalar_objects(MvpLmdProgram* p) {
    const AstIndex* index = &p->frontend->ast_index;
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        LmdObjectPlan* plan = b->shape_hint;
        if (!plan && !b->external && b->dominated && b->writes == 1)
            plan = scalar_constructor_plan(p, b->initializer);
        if (!plan || plan->literal != b->initializer || b->external || !b->dominated ||
                b->writes != 1 || !plan->blueprint->length || plan->blueprint->length > 4) continue;
        bool local = true;
        for (uint32_t id = 0; local && id < index->count; id++) {
            AstNode* use = index->nodes[id];
            if (use->node_type != AST_NODE_IDENT || ((AstIdentNode*)use)->entry != b->entry) continue;
            AstNode* parent = ast_index_parent(index, use);
            if (parent && parent->node_type == AST_NODE_VARIABLE_DECLARATOR &&
                    ((AstDeclaratorNode*)parent)->id == use) continue;
            // identity, aliases and callee receivers require a real object.
            if (!parent || parent->node_type != AST_NODE_MEMBER_EXPR ||
                    ((AstFieldNode*)parent)->object != use || ((AstFieldNode*)parent)->computed ||
                    use->source_span.start_byte < b->initializer->source_span.end_byte) { local = false; break; }
            AstFieldNode* member = (AstFieldNode*)parent;
            String* key = ((AstIdentNode*)member->property)->name;
            AstNode* consumer = ast_index_parent(index, parent);
            if (!typemap_hash_lookup(plan->blueprint, key->chars, key->len) ||
                    (consumer && ((consumer->node_type == AST_NODE_UNARY &&
                        ((AstUnaryNode*)consumer)->op == OPERATOR_JS_DELETE) ||
                    (consumer->node_type == AST_NODE_CALL_EXPR && ((AstCallNode*)consumer)->callee == parent) ||
                    (consumer->node_type == JS_AST_NODE_TAGGED_TEMPLATE && ((JsTaggedTemplateNode*)consumer)->tag == parent)))) local = false;
        }
        if (local) {
            if (plan->constructor) {
                arraylist_append(p->objects, plan);
                b->shape_hint = plan; b->shapes = {1, {plan}};
                if (LmdScalarFactoryPlan* factory = scalar_factory_plan(plan)) factory->function->scalar_factory = true;
            }
            plan->scalar_binding = b;
            plan->scalar_fields = (LmdScalarField*)pool_calloc(p->frontend->pool, plan->blueprint->length * sizeof(LmdScalarField));
            if (!plan->scalar_fields) diagnostic(p, plan->literal, "scalar field allocation");
            else {
                int field_index = 0;
                if (plan->constructor) for (; field_index < plan->blueprint->length; field_index++) {
                    AstNode* value = plan->initializers[field_index];
                    plan->scalar_fields[field_index].initializer = value;
                    plan->scalar_fields[field_index].kinds = value ? kind(p, value) : K_ANY;
                }
                else for (AstNode* e = ((AstMapNode*)plan->literal)->properties; e; e = e->next) {
                    AstNode* value = ((AstPropertyNode*)e)->value;
                    plan->scalar_fields[field_index].initializer = value;
                    plan->scalar_fields[field_index++].kinds = kind(p, value);
                }
                for (uint32_t id = 0; id < index->count; id++) {
                    AstNode* use = index->nodes[id];
                    if (use->node_type != AST_NODE_MEMBER_EXPR) continue;
                    LmdScalarField* field = scalar_field(p, use);
                    if (!field) continue;
                    AstNode* consumer = ast_index_parent(index, use);
                    if (consumer && ((consumer->node_type == AST_NODE_ASSIGN && ((AstAssignNode*)consumer)->left == use) ||
                            (consumer->node_type == AST_NODE_UNARY &&
                                (((AstUnaryNode*)consumer)->op == OPERATOR_JS_INCREMENT ||
                                 ((AstUnaryNode*)consumer)->op == OPERATOR_JS_DECREMENT)))) field->written = true;
                }
            }
        }
    }
}
static bool dense_append_loop(MvpLmdProgram* p, AstFieldNode* field, AstNode* write) {
    if (write->node_type != AST_NODE_ASSIGN || ((AstAssignNode*)write)->op != OPERATOR_ASSIGN ||
            field->object->node_type != AST_NODE_IDENT || field->property->node_type != AST_NODE_IDENT) return false;
    LmdBinding* owner = identifier_binding(p, (AstIdentNode*)field->object);
    LmdBinding* index = identifier_binding(p, (AstIdentNode*)field->property);
    if (!owner || !index || owner->external || index->external || owner->writes != 1 ||
            index->writes != 2 || owner->owner != index->owner || !owner->initializer ||
            owner->initializer->node_type != AST_NODE_ARRAY) return false;
    LmdRange start = range(p, index->initializer), step = update_delta(p, index);
    if (start.state != 1 || start.lower != 0 || start.upper != 0 ||
            step.state != 1 || step.lower != 1 || step.upper != 1) return false;
    AstNode* parent = enclosing_loop(p, write, owner->owner, true);
    if (!parent || parent->node_type != AST_NODE_LOOP) return false;
    AstLoopControlNode* loop = (AstLoopControlNode*)parent;
    if (loop->update != index->update || !loop_reset_before(p, index, loop) ||
            !index->initializer || ast_index_parent(&p->frontend->ast_index,
                ast_index_parent(&p->frontend->ast_index, index->initializer)) != loop->init) return false;
    AstNode* body = loop->body;
    if (body->node_type == AST_NODE_BLOCK) body = ((AstBlockNode*)body)->statements;
    if (!body || body->next) return false;
    if (body->node_type == AST_NODE_EXPR_STMT) body = ((AstExprStmtNode*)body)->expression;
    if (body != write) return false;
    // the only body statement fills the next slot; reject escaping aliases that could shrink it.
    AstIndex* tree = &p->frontend->ast_index;
    for (AstNodeId i = 0; i < tree->count; i++) {
        AstNode* use = tree->nodes[i];
        if (!same_binding(p, use, owner)) continue;
        AstNode* consumer = ast_index_parent(tree, use);
        if (consumer && consumer->node_type == AST_NODE_VARIABLE_DECLARATOR &&
                ((AstDeclaratorNode*)consumer)->id == use) continue;
        if (!consumer || (consumer->node_type != AST_NODE_MEMBER_EXPR && consumer->node_type != AST_NODE_INDEX_EXPR) ||
                ((AstFieldNode*)consumer)->object != use) return false;
        AstNode* call = ast_index_parent(tree, consumer);
        if (call && call->node_type == AST_NODE_CALL_EXPR && ((AstCallNode*)call)->callee == consumer) return false;
    }
    return true;
}
static LmdRange guarded_array_index_extent(MvpLmdProgram* p, AstNode* node) {
    double offset;
    LmdBinding* binding = linear_index(p, node, &offset);
    if (!binding || !binding->owner || !binding->owner->id || binding->external || binding->captured ||
            binding->reflected_global || binding->kinds != K_NUMBER) return {0, 0, 2};
    AstIndex* tree = &p->frontend->ast_index;
    AstNode* child = node;
    for (AstNode* parent = ast_index_parent(tree, child); parent;
            child = parent, parent = ast_index_parent(tree, parent)) {
        if (parent->node_type != AST_NODE_LOOP) continue;
        AstLoopControlNode* loop = (AstLoopControlNode*)parent;
        if (loop->form == LOOP_FORM_DO_WHILE || loop->body != child || !loop->cond ||
                loop->cond->node_type != AST_NODE_BINARY) continue;
        AstBinaryNode* condition = (AstBinaryNode*)loop->cond;
        if ((condition->op != OPERATOR_LT && condition->op != OPERATOR_LE) ||
                !same_binding(p, condition->left, binding)) continue;
        LmdRange limit = range(p, condition->right);
        if (limit.state != 1) continue;
        AstNodeId body = ast_index_find(tree, loop->body), cond = ast_index_find(tree, loop->cond);
        bool stable = true;
        for (AstNodeId i = 0; i < tree->count && stable; i++) {
            AstNode* use = tree->nodes[i];
            if (!same_binding(p, use, binding) ||
                    (!ast_index_node_descends(tree, i, body) && !ast_index_node_descends(tree, i, cond))) continue;
            AstNode* consumer = ast_index_parent(tree, use);
            if (!consumer) continue;
            bool write = (consumer->node_type == AST_NODE_ASSIGN && ((AstAssignNode*)consumer)->left == use) ||
                (consumer->node_type == AST_NODE_VARIABLE_DECLARATOR && ((AstDeclaratorNode*)consumer)->id == use) ||
                (consumer->node_type == AST_NODE_UNARY &&
                    (((AstUnaryNode*)consumer)->op == OPERATOR_JS_INCREMENT || ((AstUnaryNode*)consumer)->op == OPERATOR_JS_DECREMENT)) ||
                ((consumer->node_type == AST_NODE_FOR_IN_STAM || consumer->node_type == AST_NODE_FOR_OF_STAM) &&
                    ((JsForOfNode*)consumer)->left == use);
            // a later write in this same iteration is followed by the loop condition before another read.
            stable = !write || (i > ast_index_find(tree, node) &&
                enclosing_loop(p, consumer, binding->owner, true) == parent &&
                enclosing_loop(p, node, binding->owner, true) == parent);
        }
        // only nonnegative integer keys index arrays; other Numbers cannot create sparse elements.
        if (stable) return finite_range(0, fmax(0, limit.upper + offset - (condition->op == OPERATOR_LT ? 1 : 0)));
    }
    return {0, 0, 2};
}
static bool unit_excludes_sparse_arrays(MvpLmdProgram* p, LmdBinding* local = NULL) {
    auto reject = [&](AstNode* node, const char* reason) {
        log_debug("js mvp sparse proof: %s at byte %u", reason, node ? node->source_span.start_byte : 0);
        return false;
    };
    if (p->property_callbacks || p->global_reflection || p->iterators || p->symbols) return reject(NULL, "observable unit");
    Array empty = {}; empty.type_id = LMD_TYPE_ARRAY;
    auto bounded = [&](LmdRange extent, bool length) {
        if (extent.state != 1 || !finite_integer(extent.upper) || extent.upper < 0 || extent.upper > UINT32_MAX) return false;
        // an empty array is the worst density/capacity case; reuse the runtime thresholds.
        return length ? !js_array_should_keep_length_sparse(&empty, (int64_t)extent.upper) :
            !js_array_should_store_sparse_for_index(&empty, (int64_t)extent.upper);
    };
    if (local) {
        if (!local->owner || !local->owner->id || local->writes != 1 || local->assigned ||
                local->captured || local->external || local->reflected_global ||
                !local->dominated || !local->initializer || local->kinds != K_ARRAY) return false;
        AstNode* source = local->initializer;
        if (source->node_type == AST_NODE_ARRAY) {
            for (AstNode* value = ((AstArrayNode*)source)->item; value; value = value->next)
                if (value->node_type == AST_NODE_SPREAD) return false;
            if (!bounded(finite_range(0, ((AstArrayNode*)source)->length), true)) return false;
        } else if (source->node_type == AST_NODE_CALL_EXPR || source->node_type == AST_NODE_NEW_EXPR) {
            AstCallNode* call = (AstCallNode*)source;
            LmdBinding* ctor = call->callee->node_type == AST_NODE_IDENT ?
                identifier_binding(p, (AstIdentNode*)call->callee) : NULL;
            if (!ctor || ctor->intrinsic != I_ARRAY || call->interp_has_spread_args) return false;
            if (call->arguments && !call->arguments->next && (kind(p, call->arguments) & K_NUMBER) &&
                    !bounded(range(p, call->arguments, true), true)) return false;
        } else return false;
    }
    AstIndex* tree = &p->frontend->ast_index;
    for (AstNodeId i = 0; i < tree->count; i++) {
        AstNode* node = tree->nodes[i];
        AstNode* parent = ast_index_parent(tree, node);
        if (!local && (node->node_type == AST_NODE_MAP_PATTERN || node->node_type == AST_NODE_ARRAY_PATTERN)) return reject(node, "pattern writes");
        if (node->node_type == AST_NODE_IDENT) {
            LmdBinding* value = identifier_binding(p, (AstIdentNode*)node);
            if (local && value == local) {
                if (parent && parent->node_type == AST_NODE_VARIABLE_DECLARATOR &&
                        ((AstDeclaratorNode*)parent)->id == node) continue;
                // aliases escape the inventory; these native methods preserve dense physical storage.
                if (!parent || (parent->node_type != AST_NODE_MEMBER_EXPR && parent->node_type != AST_NODE_INDEX_EXPR) ||
                        ((AstFieldNode*)parent)->object != node) return false;
                AstNode* consumer = ast_index_parent(tree, parent);
                if (consumer && consumer->node_type == AST_NODE_CALL_EXPR && ((AstCallNode*)consumer)->callee == parent) {
                    String* method = member_spelling((AstFieldNode*)parent);
                    if (p->array_method_overrides || (!named(method, "push") && !named(method, "pop") && !named(method, "join"))) return false;
                }
                if (consumer && (consumer->node_type == AST_NODE_ARRAY_PATTERN || consumer->node_type == AST_NODE_MAP_PATTERN ||
                        consumer->node_type == AST_NODE_PROPERTY || consumer->node_type == AST_NODE_FOR_OF_STAM ||
                        consumer->node_type == AST_NODE_FOR_IN_STAM)) return false;
            }
            if (!local && value && value->intrinsic == I_ARRAY) {
                if (!parent || (parent->node_type != AST_NODE_CALL_EXPR && parent->node_type != AST_NODE_NEW_EXPR) ||
                        ((AstCallNode*)parent)->callee != node) return reject(node, "Array exposure");
                AstCallNode* call = (AstCallNode*)parent;
                if (call->interp_has_spread_args) return reject(node, "constructor spread");
                if (call->arguments && !call->arguments->next && (kind(p, call->arguments) & K_NUMBER) &&
                        !bounded(range(p, call->arguments, true), true)) return reject(parent, "allocation extent");
            }
        }
        if (!local && (node->node_type == AST_NODE_FOR_OF_STAM || node->node_type == AST_NODE_FOR_IN_STAM)) {
            // member/pattern targets can create indexed writes without an assignment node.
            if (((JsForOfNode*)node)->left->node_type != AST_NODE_IDENT) return reject(node, "iteration target");
        }
        if (node->node_type != AST_NODE_MEMBER_EXPR && node->node_type != AST_NODE_INDEX_EXPR) continue;
        AstFieldNode* field = (AstFieldNode*)node;
        if (local && !same_binding(p, field->object, local)) continue;
        String* name = member_spelling(field);
        // these exposed operations can allocate a length-sized sparse result or escape Array.
        if (named(name, "map") || (!local && named(name, "join")) || named(name, "slice") || named(name, "concat") ||
                named(name, "splice") || named(name, "unshift") || named(name, "assign") ||
                named(name, "constructor") || named(name, "argv")) return reject(node, "bulk operation");
        if (!(kind(p, field->object) & K_ARRAY)) continue;
        bool assignment = parent && parent->node_type == AST_NODE_ASSIGN && ((AstAssignNode*)parent)->left == node;
        bool update = parent && parent->node_type == AST_NODE_UNARY &&
            (((AstUnaryNode*)parent)->op == OPERATOR_JS_INCREMENT || ((AstUnaryNode*)parent)->op == OPERATOR_JS_DECREMENT);
        if (!assignment && !update) continue;
        if (name) {
            if (!named(name, "length")) {
                int64_t index = mvp_lmd_string_key(name);
                if (index >= 0 && !bounded(finite_range(index, index), false)) return reject(node, "named index");
                continue;
            }
            if (!assignment || ((AstAssignNode*)parent)->op != OPERATOR_ASSIGN ||
                    !bounded(range(p, ((AstAssignNode*)parent)->right, true), true)) return reject(node, "length write");
        } else if (kind(p, field->property) != K_NUMBER ||
                (!bounded(range(p, field->property, true), false) &&
                 !bounded(guarded_array_index_extent(p, field->property), false))) return reject(node, "index write");
    }
    return true;
}
static bool constructor_layout_reusable(MvpLmdProgram* p, AstNode* node, bool value, bool construction, int depth) {
    if (!node || depth >= 16) return false;
    if (value) {
        if (node->node_type == AST_NODE_LITERAL) return true;
        if (node->node_type == AST_NODE_IDENT) {
            String* name = ((AstIdentNode*)node)->name;
            return !named(name, "this") && !named(name, "super") && !named(name, "new.target");
        }
        if (node->node_type == AST_NODE_BINARY) {
            AstBinaryNode* binary = (AstBinaryNode*)node;
            return constructor_layout_reusable(p, binary->left, true, construction, depth) &&
                constructor_layout_reusable(p, binary->right, true, construction, depth);
        }
        if (node->node_type == AST_NODE_UNARY) {
            AstUnaryNode* unary = (AstUnaryNode*)node;
            return (unary->op == OPERATOR_POS || unary->op == OPERATOR_NEG || unary->op == OPERATOR_NOT ||
                unary->op == OPERATOR_JS_BIT_NOT) && constructor_layout_reusable(p, unary->operand, true, construction, depth);
        }
        if (construction && node->node_type == AST_NODE_CONDITIONAL_EXPR) {
            AstIfNode* branch = (AstIfNode*)node;
            return constructor_layout_reusable(p, branch->test, true, construction, depth) &&
                constructor_layout_reusable(p, branch->then, true, construction, depth) &&
                constructor_layout_reusable(p, branch->otherwise, true, construction, depth);
        }
        if (construction && node->node_type == AST_NODE_NEW_EXPR) {
            AstCallNode* call = (AstCallNode*)node;
            if (call->callee->node_type != AST_NODE_IDENT || call->interp_has_spread_args) return false;
            LmdBinding* callee = identifier_binding(p, (AstIdentNode*)call->callee);
            for (AstNode* argument = call->arguments; argument; argument = argument->next)
                if (!constructor_layout_reusable(p, argument, true, construction, depth)) return false;
            if (callee && callee->intrinsic == I_ARRAY) return true;
            MvpLmdClass* cls = class_plan(p, NULL, callee);
            if (!cls || cls->constructor_id < 0 || cls->initializer_count || cls->ast->superclass) return false;
            LmdFunction* constructor = (LmdFunction*)p->functions->data[cls->constructor_id];
            // nested construction cannot expose this receiver; recursively exclude arbitrary calls and escapes.
            return !constructor->complex_params &&
                constructor_layout_reusable(p, constructor->ast->body, false, construction, depth + 1);
        }
        return false;
    }
    if (node->node_type == AST_NODE_BLOCK) {
        for (AstNode* child = ((AstBlockNode*)node)->statements; child; child = child->next)
            if (!constructor_layout_reusable(p, child, false, construction, depth)) return false;
        return true;
    }
    if (node->node_type != AST_NODE_EXPR_STMT) return false;
    AstNode* expression = ((AstExprStmtNode*)node)->expression;
    if (!expression || expression->node_type != AST_NODE_ASSIGN) return false;
    AstAssignNode* assignment = (AstAssignNode*)expression;
    if (assignment->op != OPERATOR_ASSIGN || assignment->left->node_type != AST_NODE_MEMBER_EXPR) return false;
    AstFieldNode* field = (AstFieldNode*)assignment->left;
    // receiver reads, escapes and unproven calls could reveal the preallocated field set.
    return !field->computed && field->object->node_type == AST_NODE_IDENT &&
        named(((AstIdentNode*)field->object)->name, "this") && constructor_layout_reusable(p, assignment->right, true, construction, depth);
}
// Admission proofs share the runtime shape inventory, including native result records.
template <typename Matches>
static bool native_property_matches(MvpLmdProgram* p, Matches matches) {
    if (!p->library) return true;
    MvpLmdClass* classes[] = {&p->library->date, &p->library->regexp, &p->library->object,
        &p->library->array, &p->library->generator, &p->library->symbol,
        &p->library->iterator, &p->library->map, &p->library->set};
    for (MvpLmdClass* cls : classes) {
        TypeMap* shapes[] = {&cls->shape, &cls->prototype_shape, &cls->static_shape};
        for (TypeMap* shape : shapes) FOR_EACH_MAP_FIELD(shape, field)
            if (field->name && matches(field->name->str, field->name->length)) return true;
    }
    TypeMap* shapes[] = {&p->library->json_shape, &p->library->proxy_shape};
    for (TypeMap* shape : shapes) FOR_EACH_MAP_FIELD(shape, field)
        if (field->name && matches(field->name->str, field->name->length)) return true;
    for (int i = 0; i < JS_INTRINSIC_BINDING_COUNT; i++) {
        const JsBuiltinMethodSpec* method = js_builtin_method_at(i);
        if (method->owner == JS_BUILTIN_OWNER_MATH_METHOD && matches(method->name, method->len)) return true;
    }
    for (int i = 0; i < js_math_constant_count; i++)
        if (matches(js_math_constants[i].name, strlen(js_math_constants[i].name))) return true;
    const char* names[] = {"message", "value", "done", "input", "index", "groups", "description",
        "stdout", "argv", "platform", "now", "log", "write", "readFileSync", "writeFileSync", "arguments", "caller", "callee",
        "__proto__", "toJSON", "call", "apply", "bind", "get", "set", "has", "deleteProperty", "ownKeys", "construct"};
    for (const char* name : names) if (matches(name, strlen(name))) return true;
    return false;
}
static bool global_bindings_are_exposed(MvpLmdProgram* p) {
    if (p->dynamic_source) return true;
    if (!p->global_reflection) return false;
    const AstIndex* tree = &p->frontend->ast_index;
    for (AstNodeId i = 0; i < tree->count; i++) {
        AstNode* node = tree->nodes[i];
        if (node->node_type != AST_NODE_IDENT) continue;
        AstNode* parent = ast_index_parent(tree, node);
        if (js_ast_fact_walk_skips_child(parent, node)) continue;
        String* name = ((AstIdentNode*)node)->name;
        if (named(name, "globalThis")) return true;
        if (!named(name, "this")) {
            // implicit global names are absent from the declared binding inventory.
            if (!identifier_binding(p, (AstIdentNode*)node)) return true;
            continue;
        }
        if (!parent || (parent->node_type != AST_NODE_MEMBER_EXPR && parent->node_type != AST_NODE_INDEX_EXPR) ||
                ((AstFieldNode*)parent)->object != node) return true;
        String* key = member_spelling((AstFieldNode*)parent);
        if (!key || native_property_matches(p, [&](const char* bytes, size_t length) {
                return key->len == length && !memcmp(key->chars, bytes, length);
            })) return true;
        for (const char* intrinsic : intrinsic_names) if (named(key, intrinsic)) return true;
        for (int j = 0; j < p->bindings->length; j++) {
            LmdBinding* binding = (LmdBinding*)p->bindings->data[j];
            if (binding->reflected_global && named(key, binding->entry->name->chars)) return true;
        }
    }
    // distinct named fields cannot alias global bindings; returned, computed and native receivers can.
    return false;
}
// Constructor provenance narrows same-spelled method calls without changing their runtime ABI.
static MvpLmdClass* property_receiver_class(MvpLmdProgram* p, AstNode* node, int depth = 0) {
    if (!node || depth == 16) return NULL;
    if (node->node_type == AST_NODE_ARRAY) return p->library ? &p->library->array : NULL;
    if (node->node_type == AST_NODE_NEW_EXPR)
        return property_receiver_class(p, ((AstCallNode*)node)->callee, depth + 1);
    if (node->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)node;
        LmdBinding* callee = call->callee->node_type == AST_NODE_IDENT
            ? identifier_binding(p, (AstIdentNode*)call->callee) : NULL;
        LmdFunction* target = callee && !callee->assigned ? callee->target : NULL;
        String* method = NULL;
        if (call->callee->node_type == AST_NODE_MEMBER_EXPR) {
            AstFieldNode* member = (AstFieldNode*)call->callee;
            LmdBinding* owner = member->object->node_type == AST_NODE_IDENT
                ? identifier_binding(p, (AstIdentNode*)member->object) : NULL;
            MvpLmdClass* cls = class_plan(p, NULL, owner);
            method = member_spelling(member);
            for (int i = 1; cls && method && i < p->functions->length; i++) {
                LmdFunction* function = (LmdFunction*)p->functions->data[i];
                if (function->home == cls && function->ast->node_type == AST_NODE_METHOD &&
                        ((AstMethodNode*)function->ast)->static_method &&
                        named(property_spelling(((AstMethodNode*)function->ast)->key, ((AstMethodNode*)function->ast)->computed), method->chars)) target = function;
            }
        }
        bool instance_returns = !target && method;
        if ((!target && !instance_returns) || (target && (target->ast->is_generator || target->ast->is_async))) return NULL;
        if (instance_returns && (p->global_reflection || mvp_lmd_builtin_method(method) ||
                native_property_matches(p, [&](const char* name, size_t length) {
                    return method->len == length && !memcmp(method->chars, name, length);
                }))) return NULL;
        const AstIndex* tree = &p->frontend->ast_index;
        MvpLmdClass* result = NULL;
        for (AstNodeId i = 0; i < tree->count; i++) {
            AstNode* use = tree->nodes[i];
            if (method && use->node_type == AST_NODE_PROPERTY && named(property_spelling(
                    ((AstPropertyNode*)use)->key, ((AstPropertyNode*)use)->computed), method->chars)) return NULL;
            if (method && use->node_type == AST_NODE_FIELD && named(property_spelling(
                    ((AstClassFieldNode*)use)->key, ((AstClassFieldNode*)use)->computed), method->chars)) return NULL;
            if (instance_returns && use->node_type == AST_NODE_MAP_PATTERN) return NULL;
            if (instance_returns && (use->node_type == AST_NODE_MEMBER_EXPR || use->node_type == AST_NODE_INDEX_EXPR) &&
                    named(member_spelling((AstFieldNode*)use), method->chars)) {
                AstNode* consumer = ast_index_parent(tree, use);
                if (!consumer || consumer->node_type != AST_NODE_CALL_EXPR || ((AstCallNode*)consumer)->callee != use) return NULL;
            }
            if (method && use->node_type == AST_NODE_CALL_EXPR) {
                AstNode* invoked = ((AstCallNode*)use)->callee;
                if ((invoked->node_type == AST_NODE_MEMBER_EXPR || invoked->node_type == AST_NODE_INDEX_EXPR) &&
                        named(member_spelling((AstFieldNode*)invoked), "assign")) return NULL;
            }
            AstNode* written = use->node_type == AST_NODE_ASSIGN ? ((AstAssignNode*)use)->left :
                use->node_type == AST_NODE_UNARY ? ((AstUnaryNode*)use)->operand : NULL;
            if (method && written && (written->node_type == AST_NODE_MEMBER_EXPR || written->node_type == AST_NODE_INDEX_EXPR) &&
                    named(member_spelling((AstFieldNode*)written), method->chars)) return NULL;
            if (use->node_type != AST_NODE_RETURN_STAM) continue;
            AstNode* owner = ast_index_parent(tree, use);
            while (owner && !function(p, owner)) owner = ast_index_parent(tree, owner);
            LmdFunction* candidate = owner ? function(p, owner) : NULL;
            if (instance_returns ? !candidate || !candidate->home || candidate->ast->node_type != AST_NODE_METHOD ||
                    !named(property_spelling(((AstMethodNode*)candidate->ast)->key, ((AstMethodNode*)candidate->ast)->computed), method->chars) :
                    owner != (AstNode*)target->ast) continue;
            if (candidate->ast->is_generator || candidate->ast->is_async) return NULL;
            AstNode* returned = ((AstReturnNode*)use)->value;
            // A method returning its receiver preserves the nominal family only
            // when all uses are direct calls and the method cannot be replaced.
            MvpLmdClass* next = instance_returns && !((AstMethodNode*)candidate->ast)->static_method &&
                returned && returned->node_type == AST_NODE_IDENT && named(((AstIdentNode*)returned)->name, "this") ?
                candidate->home : property_receiver_class(p, returned, depth + 1);
            if (!next || (result && result != next)) return NULL;
            result = next;
        }
        return result;
    }
    if (node->node_type == AST_NODE_IDENT) {
        LmdBinding* value = identifier_binding(p, (AstIdentNode*)node);
        if (named(((AstIdentNode*)node)->name, "this")) {
            AstNode* owner = ast_index_parent(&p->frontend->ast_index, node);
            while (owner && !function(p, owner)) owner = ast_index_parent(&p->frontend->ast_index, owner);
            LmdFunction* constructor = owner ? function(p, owner) : NULL;
            if (constructor && constructor->home && constructor->id == constructor->home->constructor_id &&
                    !constructor->home->ast->superclass) return constructor->home;
        }
        if (value && value->intrinsic == I_ARRAY) return p->library ? &p->library->array : NULL;
        if (value && value->class_name && !value->assigned) {
            MvpLmdClass* cls = class_plan(p, NULL, value);
            if (cls && cls->constructor_id >= 0) {
                LmdFunction* constructor = (LmdFunction*)p->functions->data[cls->constructor_id];
                const AstIndex* tree = &p->frontend->ast_index;
                for (AstNodeId i = 0; i < tree->count; i++) {
                    AstNode* ret = tree->nodes[i];
                    if (ret->node_type != AST_NODE_RETURN_STAM) continue;
                    AstNode* owner = ast_index_parent(tree, ret);
                    while (owner && !function(p, owner)) owner = ast_index_parent(tree, owner);
                    if (owner == (AstNode*)constructor->ast) return NULL;
                }
            }
            // an inherited constructor can also return a different object family.
            if (cls && cls->ast->superclass && !property_receiver_class(p, cls->ast->superclass, depth + 1)) return NULL;
            return cls;
        }
        if (value && value->writes == 1 && value->initializer)
            return property_receiver_class(p, value->initializer, depth + 1);
        return NULL;
    }
    if (node->node_type == AST_NODE_CONDITIONAL_EXPR) {
        MvpLmdClass* left = property_receiver_class(p, ((AstIfNode*)node)->then, depth + 1);
        return left == property_receiver_class(p, ((AstIfNode*)node)->otherwise, depth + 1) ? left : NULL;
    }
    if (node->node_type != AST_NODE_MEMBER_EXPR && node->node_type != AST_NODE_INDEX_EXPR) return NULL;
    AstFieldNode* member = (AstFieldNode*)node;
    String* name = member_spelling(member);
    if (!name || member->object->node_type != AST_NODE_IDENT) return NULL;
    AstIdentNode* owner = (AstIdentNode*)member->object;
    LmdBinding* binding = identifier_binding(p, owner);
    MvpLmdClass* result = NULL;
    MvpLmdClass* static_owner = class_plan(p, NULL, binding);
    if (static_owner) {
        for (AstNodeId i = 0; i < p->frontend->ast_index.count; i++) {
            AstNode* field = p->frontend->ast_index.nodes[i];
            if (field->node_type != AST_NODE_FIELD || !((AstClassFieldNode*)field)->is_static ||
                    !named(property_spelling(((AstClassFieldNode*)field)->key, ((AstClassFieldNode*)field)->computed), name->chars)) continue;
            AstNode* parent = ast_index_parent(&p->frontend->ast_index, field);
            while (parent && parent->node_type != AST_NODE_CLASS) parent = ast_index_parent(&p->frontend->ast_index, parent);
            if (parent == (AstNode*)static_owner->ast)
                result = property_receiver_class(p, ((AstClassFieldNode*)field)->value, depth + 1);
        }
    } else if (binding && binding->writes == 1 && binding->initializer &&
            (binding->initializer->node_type == AST_NODE_MAP || binding->initializer->node_type == AST_NODE_OBJECT_LITERAL)) {
        for (AstNode* n = ((AstMapNode*)binding->initializer)->properties; n; n = n->next) {
            if (n->node_type != AST_NODE_PROPERTY) return NULL;
            AstPropertyNode* property = (AstPropertyNode*)n;
            if (named(property_spelling(property->key, property->computed), name->chars))
                result = property_receiver_class(p, property->value, depth + 1);
        }
    } else if (!named(owner->name, "this")) return NULL;
    const AstIndex* tree = &p->frontend->ast_index;
    for (AstNodeId i = 0; i < tree->count; i++) {
        AstNode* n = tree->nodes[i];
        if (n->node_type == AST_NODE_CALL_EXPR) {
            AstNode* invoked = ((AstCallNode*)n)->callee;
            if ((invoked->node_type == AST_NODE_MEMBER_EXPR || invoked->node_type == AST_NODE_INDEX_EXPR) &&
                    named(member_spelling((AstFieldNode*)invoked), "assign")) return NULL;
        }
        if (!binding && n->node_type == AST_NODE_PROPERTY && named(property_spelling(
                ((AstPropertyNode*)n)->key, ((AstPropertyNode*)n)->computed), name->chars)) return NULL;
        if (!binding && n->node_type == AST_NODE_FIELD && named(property_spelling(
                ((AstClassFieldNode*)n)->key, ((AstClassFieldNode*)n)->computed), name->chars)) return NULL;
        AstNode* written = n->node_type == AST_NODE_ASSIGN ? ((AstAssignNode*)n)->left :
            n->node_type == AST_NODE_UNARY ? ((AstUnaryNode*)n)->operand : NULL;
        if (!written || (written->node_type != AST_NODE_MEMBER_EXPR && written->node_type != AST_NODE_INDEX_EXPR)) continue;
        if (!named(member_spelling((AstFieldNode*)written), name->chars)) continue;
        if (binding) {
            // a known unrelated instance cannot write this constructor's static property.
            MvpLmdClass* actual = static_owner ? property_receiver_class(p, ((AstFieldNode*)written)->object, depth + 1) : NULL;
            if (actual && actual != static_owner) continue;
            return NULL;
        }
        if (n->node_type != AST_NODE_ASSIGN) return NULL;
        AstAssignNode* assign = (AstAssignNode*)n;
        if (assign->op != OPERATOR_ASSIGN) return NULL;
        if (kind(p, assign->right) == K_NULL || kind(p, assign->right) == K_UNDEFINED) continue;
        MvpLmdClass* next = property_receiver_class(p, assign->right, depth + 1);
        if (!next || (result && result != next)) return NULL;
        result = next;
    }
    return result;
}
// This closed-unit proof is only for hook admission, never for a native value ABI.
// All computed accesses must pass this or the observation proof; named stores
// seed inductive field domains, and escaped methods retain unknown inputs.
static bool property_key_may_name(String* affix, bool prefix, const char* name, size_t length) {
    if (length <= affix->len) return false;
    size_t digits = length - affix->len;
    if (memcmp(name + (prefix ? 0 : digits), affix->chars, affix->len)) return false;
    const char* number = name + (prefix ? affix->len : 0);
    size_t i = number[0] == '-' ? 1 : 0;
    if (i == digits) return false;
    for (; i < digits; i++) if (number[i] < '0' || number[i] > '9') return false;
    return true;
}
static bool primitive_property_key_body(MvpLmdProgram* p, AstNode* node, AstNode** path,
        int depth, unsigned primitive, uint8_t* visited);
static bool primitive_property_key(MvpLmdProgram* p, AstNode* node, AstNode** path,
        int depth, unsigned primitive, uint8_t* visited) {
    if (!node) return true;
    if (!visited) {
        auto* states = (uint8_t*)mem_calloc(p->frontend->ast_index.count, sizeof(uint8_t), MEM_CAT_TEMP);
        if (!states) return false;
        bool result = primitive_property_key(p, node, path, depth, primitive, states);
        mem_free(states); return result;
    }
    AstNodeId id = ast_index_find(&p->frontend->ast_index, node);
    if (id == AST_NODE_ID_INVALID) return false;
    // all recursive obligations are conjunctive: a failed descendant rejects the
    // whole query. Reuse completed/active nodes only within this one induction,
    // never across roots whose assumptions or representation facts may differ.
    if (visited[id]) return visited[id] != 3;
    visited[id] = 1;
    bool result = primitive_property_key_body(p, node, path, depth, primitive, visited);
    visited[id] = result ? 2 : 3;
    return result;
}
static bool primitive_property_key_body(MvpLmdProgram* p, AstNode* node, AstNode** path,
        int depth, unsigned primitive, uint8_t* visited) {
    if (!node) return true;
    unsigned known = kind(p, node);
    if (known && !(known & ~primitive)) return true;
    bool numeric_only = primitive == (K_NUMBER | K_UNDEFINED);
    if (numeric_only && known && !(known & primitive)) return false;
    // canonical numeric strings have the same hook-admission proof as numeric keys.
    if (!numeric_only && node->node_type == AST_NODE_LITERAL && ((AstLiteralNode*)node)->literal_type == AST_LITERAL_STRING &&
            mvp_lmd_string_key(((AstLiteralNode*)node)->value.string_value) >= 0) return true;
    if (!numeric_only && (node->node_type == AST_NODE_INDEX_EXPR || node->node_type == AST_NODE_MEMBER_EXPR)) {
        AstFieldNode* member = (AstFieldNode*)node;
        unsigned key = kind(p, member->property);
        if (member->computed && kind(p, member->object) == K_STRING && key && !(key & ~primitive)) {
            // A numeric string lookup yields one UTF-16 unit or undefined, never
            // a prototype/hook name. Reflected one-unit globals still need a join.
            for (int i = 0; p->global_reflection && i < p->bindings->length; i++) {
                LmdBinding* binding = (LmdBinding*)p->bindings->data[i];
                String* name = binding->entry->name;
                if (binding->reflected_global && name->len <= 3) return false;
            }
            return true;
        }
    }
    if (depth == 48) return false;
    for (int i = 0; i < depth; i++) if (path[i] == node) return true;
    path[depth++] = node;
    auto safe = [&](AstNode* value) { return primitive_property_key(p, value, path, depth, primitive, visited); };
    if (node->node_type == AST_NODE_BINARY) {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        AstNode* literal = binary->left;
        AstNode* numeric = binary->right;
        bool prefix = true;
        if (literal->node_type != AST_NODE_LITERAL || ((AstLiteralNode*)literal)->literal_type != AST_LITERAL_STRING) {
            literal = binary->right; numeric = binary->left; prefix = false;
        }
        if (!numeric_only && binary->op == OPERATOR_ADD && p->library && literal->node_type == AST_NODE_LITERAL &&
                ((AstLiteralNode*)literal)->literal_type == AST_LITERAL_STRING && kind(p, numeric) == K_NUMBER &&
                range(p, numeric, true).state == 1) {
            String* affix = ((AstLiteralNode*)literal)->value.string_value;
            // finite int53 stringification is decimal; inspect the same native shape inventory as runtime lookup.
            auto matches = [&](const char* name, size_t length) { return property_key_may_name(affix, prefix, name, length); };
            if (native_property_matches(p, matches)) return false;
            for (int i = 0; i < p->bindings->length; i++) {
                LmdBinding* binding = (LmdBinding*)p->bindings->data[i];
                String* name = binding->entry->name;
                if (binding->reflected_global && matches(name->chars, name->len)) return false;
            }
            const AstIndex* tree = &p->frontend->ast_index;
            for (AstNodeId i = 0; i < tree->count; i++) {
                AstNode* use = tree->nodes[i];
                String* name = use->node_type == AST_NODE_METHOD ? property_spelling(((AstMethodNode*)use)->key, ((AstMethodNode*)use)->computed) :
                    use->node_type == AST_NODE_PROPERTY ? property_spelling(((AstPropertyNode*)use)->key, ((AstPropertyNode*)use)->computed) :
                    use->node_type == AST_NODE_FIELD ? property_spelling(((AstClassFieldNode*)use)->key, ((AstClassFieldNode*)use)->computed) :
                    use->node_type == AST_NODE_MEMBER_EXPR || use->node_type == AST_NODE_INDEX_EXPR ? member_spelling((AstFieldNode*)use) : NULL;
                if (name && matches(name->chars, name->len)) return false;
            }
            return true;
        }
        return safe(binary->left) && safe(binary->right);
    }
    if (node->node_type == AST_NODE_ASSIGN) {
        AstAssignNode* assign = (AstAssignNode*)node;
        return safe(assign->right) && (assign->op == OPERATOR_ASSIGN || safe(assign->left));
    }
    if (node->node_type == AST_NODE_CONDITIONAL_EXPR)
        return safe(((AstIfNode*)node)->then) && safe(((AstIfNode*)node)->otherwise);
    AstNode* backing = node;
    bool popped_array = false;
    if (node->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)node;
        if (call->callee->node_type == AST_NODE_MEMBER_EXPR && !call->interp_has_spread_args &&
                named(member_spelling((AstFieldNode*)call->callee), "pop")) {
            backing = ((AstFieldNode*)call->callee)->object;
            popped_array = true;
        }
    }
    LmdBinding* local = backing->node_type == AST_NODE_IDENT ? identifier_binding(p, (AstIdentNode*)backing) : NULL;
    String* field = backing->node_type == AST_NODE_MEMBER_EXPR || backing->node_type == AST_NODE_INDEX_EXPR
        ? member_spelling((AstFieldNode*)backing) : NULL;
    AstFieldNode* indexed_field = NULL;
    bool indexed_local = popped_array && local;
    int indexed_depth = popped_array ? 1 : 0;
    if (popped_array && field) indexed_field = (AstFieldNode*)backing;
    if (!field && (node->node_type == AST_NODE_MEMBER_EXPR || node->node_type == AST_NODE_INDEX_EXPR)) {
        AstNode* element = node;
        while (element->node_type == AST_NODE_MEMBER_EXPR || element->node_type == AST_NODE_INDEX_EXPR) {
            AstFieldNode* member = (AstFieldNode*)element;
            field = member_spelling(member);
            if (field) { indexed_field = member; break; }
            if (!member->computed || indexed_depth == 16 || !safe(member->property)) return false;
            indexed_depth++;
            element = member->object;
        }
        if (!field && element->node_type == AST_NODE_IDENT) {
            local = identifier_binding(p, (AstIdentNode*)element);
            indexed_local = local && !local->entry->is_parameter;
            if (!indexed_local) return false;
        }
    }
    String* implicit_name = !local && !field && backing == node && node->node_type == AST_NODE_IDENT
        ? ((AstIdentNode*)node)->name : NULL;
    if (implicit_name && (p->global_bindings_exposed || p->module_realm || p->property_callbacks ||
            named(implicit_name, "this") || named(implicit_name, "super") ||
            js_builtin_global_find(implicit_name->chars, implicit_name->len))) return false;
    auto matches_local = [&](AstNode* target) {
        if (!target || target->node_type != AST_NODE_IDENT) return false;
        LmdBinding* binding = identifier_binding(p, (AstIdentNode*)target);
        return local ? binding == local : implicit_name && !binding &&
            named(((AstIdentNode*)target)->name, implicit_name->chars);
    };
    LmdFunction* incoming = local && local->entry->is_parameter ? local->owner : NULL;
    // Global reflection cannot reach unaliased local arrays; calls and field inventories
    // still require a closed global binding environment.
    bool global_scalar = local && local->reflected_global && !indexed_local && local->initializer &&
        kind(p, local->initializer) && !(kind(p, local->initializer) & ~primitive);
    if (p->global_bindings_exposed && (!local || incoming || (local->reflected_global && !global_scalar))) return false;
    const AstIndex* tree = &p->frontend->ast_index;
    LmdFunction* receiver_method = incoming;
    if (field && !named(field, "length")) {
        AstNode* receiver = (indexed_field ? indexed_field : (AstFieldNode*)node)->object;
        if (receiver->node_type == AST_NODE_IDENT && named(((AstIdentNode*)receiver)->name, "this")) {
            AstNode* owner = ast_index_parent(tree, node);
            while (owner && !function(p, owner)) owner = ast_index_parent(tree, owner);
            receiver_method = owner ? function(p, owner) : NULL;
            if (!receiver_method || (receiver_method->home && ((AstMethodNode*)receiver_method->ast)->static_method)) return false;
            // Ordinary constructors and installed functions have no class home.
            // Only user fields can use their whole-unit write domain; native
            // records may supply the same spelling without a source assignment.
            if (!receiver_method->home && (mvp_lmd_builtin_method(field) ||
                    native_property_matches(p, [&](const char* name, size_t length) {
                        return field->len == length && !memcmp(field->chars, name, length);
                    }))) return false;
        } else {
            // Native records have writes outside the source inventory. Reject their
            // public fields before considering the closed set of user field stores.
            if (mvp_lmd_builtin_method(field) || native_property_matches(p, [&](const char* name, size_t length) {
                    return field->len == length && !memcmp(field->chars, name, length);
                })) return false;
        }
    }
    String* method = receiver_method && receiver_method->home && receiver_method->ast->node_type == AST_NODE_METHOD &&
        ((AstMethodNode*)receiver_method->ast)->kind != AstMethodNode::JS_METHOD_CONSTRUCTOR
        ? property_spelling(((AstMethodNode*)receiver_method->ast)->key, ((AstMethodNode*)receiver_method->ast)->computed) : NULL;
    bool instance_function = false;
    if (incoming && !method && !incoming->home) {
        AstNode* assignment = ast_index_parent(tree, (AstNode*)incoming->ast);
        AstNode* statement = assignment ? ast_index_parent(tree, assignment) : NULL;
        if (assignment && assignment->node_type == AST_NODE_ASSIGN && statement &&
                statement->node_type == AST_NODE_EXPR_STMT &&
                ((AstAssignNode*)assignment)->op == OPERATOR_ASSIGN &&
                ((AstAssignNode*)assignment)->right == (AstNode*)incoming->ast) {
            AstNode* target = ((AstAssignNode*)assignment)->left;
            if (target->node_type == AST_NODE_MEMBER_EXPR) {
                method = member_spelling((AstFieldNode*)target);
                instance_function = method != NULL;
            }
        }
    }
    bool class_constructor = incoming && incoming->home && incoming->id == incoming->home->constructor_id;
    LmdBinding* direct_binding = NULL;
    if (incoming && !method && !incoming->closed_calls) {
        for (int i = 0; i < p->bindings->length; i++) {
            LmdBinding* candidate = (LmdBinding*)p->bindings->data[i];
            if (!candidate->assigned && (candidate->target == incoming ||
                    (class_constructor && class_plan(p, NULL, candidate) == incoming->home))) direct_binding = candidate;
        }
        if (!direct_binding) return false;
        for (AstNodeId i = 0; i < tree->count; i++) {
            AstNode* use = tree->nodes[i];
            if (use->node_type != AST_NODE_IDENT) continue;
            LmdBinding* use_binding = identifier_binding(p, (AstIdentNode*)use);
            if (class_constructor ? class_plan(p, NULL, use_binding) != incoming->home : use_binding != direct_binding) continue;
            AstNode* parent = ast_index_parent(tree, use);
            if (!parent || (parent->node_type != AST_NODE_CALL_EXPR && parent->node_type != AST_NODE_NEW_EXPR) ||
                    ((AstCallNode*)parent)->callee != use) return false;
        }
    }
    if (incoming && incoming->complex_params) return false;
    if (local && (local->intrinsic || (local->reflected_global && !global_scalar) ||
            (!incoming && !global_scalar && local->seed_kinds == K_ANY))) return false;
    if (method) for (AstNodeId i = 0; i < tree->count; i++) {
        AstNode* other = tree->nodes[i];
        if (other->node_type == AST_NODE_MAP_PATTERN) return false;

        if (instance_function && (other->node_type == AST_NODE_MEMBER_EXPR || other->node_type == AST_NODE_INDEX_EXPR) &&
                (named(member_spelling((AstFieldNode*)other), "values") || named(member_spelling((AstFieldNode*)other), "entries")))
            return false; // enumerable instance functions can escape through value projections.
        if ((other->node_type == AST_NODE_MEMBER_EXPR || other->node_type == AST_NODE_INDEX_EXPR) &&
                named(member_spelling((AstFieldNode*)other), method->chars)) {
            AstNode* use = ast_index_parent(tree, other);
            if (instance_function && use && use->node_type == AST_NODE_ASSIGN &&
                    ((AstAssignNode*)use)->left == other) continue;
            if (!use || use->node_type != AST_NODE_CALL_EXPR || ((AstCallNode*)use)->callee != other) return false;
        }
    }
    if (node->node_type == AST_NODE_CALL_EXPR && !popped_array) {
        AstCallNode* call = (AstCallNode*)node;
        String* name = call->callee->node_type == AST_NODE_MEMBER_EXPR || call->callee->node_type == AST_NODE_INDEX_EXPR
            ? member_spelling((AstFieldNode*)call->callee) : NULL;
        LmdFunction* direct = direct_target(p, call);
        MvpLmdClass* receiver = name ? property_receiver_class(p, ((AstFieldNode*)call->callee)->object) : NULL;
        if (name && ((mvp_lmd_builtin_method(name) && (!receiver || !receiver->ast)) || named(name, "call") || named(name, "apply") ||
                named(name, "bind") || named(name, "readFileSync"))) return false;
        if (name && p->library) {
            // Native functions sharing a spelling are outside the source method inventory.
            TypeMap* shapes[] = {&p->library->json_shape, &p->library->object.static_shape,
                &p->library->object.prototype_shape, &p->library->array.static_shape,
                &p->library->date.static_shape, &p->library->date.prototype_shape,
                &p->library->regexp.prototype_shape};
            for (TypeMap* shape : shapes)
                if (typemap_shape_lookup_last(shape, name->chars, name->len)) return false;
        }
        bool found = false;
        for (int fi = 1; fi < p->functions->length; fi++) {
            LmdFunction* candidate = (LmdFunction*)p->functions->data[fi];
            if (candidate != direct && (!name || !candidate->home || candidate->ast->node_type != AST_NODE_METHOD ||
                    !named(property_spelling(((AstMethodNode*)candidate->ast)->key, ((AstMethodNode*)candidate->ast)->computed), name->chars))) continue;
            if (candidate != direct && receiver && receiver->ast && !receiver->ast->superclass && candidate->home != receiver) continue;
            if (candidate->ast->is_generator || candidate->ast->is_async) return false;
            found = true;
            if (candidate->ast->body->node_type != AST_NODE_BLOCK) {
                if (!safe(candidate->ast->body)) return false;
                continue;
            }
            for (AstNodeId i = 0; i < tree->count; i++) {
                AstNode* ret = tree->nodes[i];
                if (ret->node_type != AST_NODE_RETURN_STAM) continue;
                AstNode* owner = ast_index_parent(tree, ret);
                while (owner && !function(p, owner)) owner = ast_index_parent(tree, owner);
                if (owner == (AstNode*)candidate->ast && !safe(((AstReturnNode*)ret)->value)) return false;
            }
        }
        if (!found) return false;
        // A same-spelled user store can replace the callable without changing its receiver type.
        if (name) for (AstNodeId i = 0; i < tree->count; i++) {
            AstNode* other = tree->nodes[i];
            if (other->node_type == AST_NODE_PROPERTY && named(property_spelling(
                    ((AstPropertyNode*)other)->key, ((AstPropertyNode*)other)->computed), name->chars)) return false;
            if (other->node_type == AST_NODE_FIELD && named(property_spelling(
                    ((AstClassFieldNode*)other)->key, ((AstClassFieldNode*)other)->computed), name->chars)) return false;
            AstNode* target = other->node_type == AST_NODE_ASSIGN ? ((AstAssignNode*)other)->left : NULL;
            if (target && (target->node_type == AST_NODE_MEMBER_EXPR || target->node_type == AST_NODE_INDEX_EXPR) &&
                    named(member_spelling((AstFieldNode*)target), name->chars)) return false;
        }
        return true;
    }
    if (!local && !field && !implicit_name) return false;
    auto array_seed = [&](auto&& seed, AstNode* value, int levels) -> bool {
        if (!levels) return safe(value);
        if (!value) return false;
        if (value->node_type == AST_NODE_CALL_EXPR) {
            AstCallNode* call = (AstCallNode*)value;
            if (call->callee->node_type != AST_NODE_MEMBER_EXPR ||
                    !named(member_spelling((AstFieldNode*)call->callee), "fill") ||
                    call->interp_has_spread_args || !seed(seed, call->arguments, levels - 1)) return false;
            value = ((AstFieldNode*)call->callee)->object;
        }
        if (value->node_type == AST_NODE_ARRAY) {
            for (AstNode* item = ((AstArrayNode*)value)->item; item; item = item->next)
                if (item->node_type != AST_NODE_NULL && !seed(seed, item, levels - 1)) return false;
            return true;
        }
        if (value->node_type != AST_NODE_NEW_EXPR) return false;
        if (p->global_reflection) return false;
        AstCallNode* allocation = (AstCallNode*)value;
        if (allocation->callee->node_type != AST_NODE_IDENT || allocation->interp_has_spread_args) return false;
        LmdBinding* ctor = identifier_binding(p, (AstIdentNode*)allocation->callee);
        if (!ctor || ctor->intrinsic != I_ARRAY) return false;
        for (AstNode* item = allocation->arguments; item; item = item->next)
            if (allocation->arguments->next ? !seed(seed, item, levels - 1) : !safe(item)) return false;
        return true;
    };
    bool seeded = local != NULL || named(field, "length");
    int parameter = 0;
    if (incoming) for (AstNode* formal = incoming->ast->params; formal; formal = formal->next, parameter++)
        if (js_ast_parameter_binding_identifier(formal)->entry == local->entry) break;
    for (AstNodeId i = 0; i < tree->count; i++) {
        AstNode* other = tree->nodes[i];
        if (other->node_type == AST_NODE_MAP_PATTERN) return false;
        if (implicit_name && (other->node_type == AST_NODE_ARRAY_PATTERN ||
                (other->node_type == AST_NODE_ASSIGN &&
                 ((AstAssignNode*)other)->left->node_type == AST_NODE_ARRAY))) return false;
        if (local || implicit_name) {
            AstNode* implicit = other->node_type == AST_NODE_FOR_OF_STAM || other->node_type == AST_NODE_FOR_IN_STAM ?
                ((JsForOfNode*)other)->left : NULL;
            if (other->node_type == AST_NODE_ASSIGN) {
                AstNode* left = ((AstAssignNode*)other)->left;
                if (left->node_type == AST_NODE_ARRAY || left->node_type == AST_NODE_ARRAY_PATTERN) implicit = left;
            }
            if (implicit && (implicit->node_type == AST_NODE_ARRAY || implicit->node_type == AST_NODE_ARRAY_PATTERN))
                implicit = ((AstArrayNode*)implicit)->item;
            // Iterator and destructuring writes are not ordinary assignment nodes.
            for (AstNode* item = implicit; item; item = item->next)
                if (matches_local(item)) return false;
        }
        if (global_scalar) {
            // Reflective reads can expose the global through aliases or bulk
            // projections. Named writes to this binding must join its domain.
            if (other->node_type == AST_NODE_FOR_IN_STAM) return false;
            String* name = other->node_type == AST_NODE_PROPERTY ? property_spelling(
                ((AstPropertyNode*)other)->key, ((AstPropertyNode*)other)->computed) :
                other->node_type == AST_NODE_MEMBER_EXPR || other->node_type == AST_NODE_INDEX_EXPR ?
                    member_spelling((AstFieldNode*)other) : NULL;
            if (name && (named(name, local->entry->name->chars) || named(name, "assign") ||
                    named(name, "values") || named(name, "entries") || named(name, "getOwnPropertyDescriptors"))) return false;
        }
        // Named regexp groups, like JSON records, can introduce arbitrary field names.
        if (field && (other->node_type == JS_AST_NODE_REGEX || (other->node_type == AST_NODE_IDENT &&
                identifier_binding(p, (AstIdentNode*)other) == &p->intrinsics[I_REGEXP - 1]))) return false;
        if (field && other->node_type == AST_NODE_IDENT &&
                identifier_binding(p, (AstIdentNode*)other) == &p->intrinsics[I_JSON - 1]) {
            AstNode* use = ast_index_parent(tree, other);
            // stringify cannot introduce foreign fields; parse or an escaped namespace can.
            if (!use || use->node_type != AST_NODE_MEMBER_EXPR ||
                    ((AstFieldNode*)use)->object != other || !named(member_spelling((AstFieldNode*)use), "stringify")) return false;
        }
        if ((indexed_field && (other->node_type == AST_NODE_MEMBER_EXPR || other->node_type == AST_NODE_INDEX_EXPR) &&
                named(member_spelling((AstFieldNode*)other), field->chars)) ||
                (indexed_local && other->node_type == AST_NODE_IDENT && identifier_binding(p, (AstIdentNode*)other) == local)) {
            AstNode* declaration = ast_index_parent(tree, other);
            if (indexed_local && declaration && declaration->node_type == AST_NODE_VARIABLE_DECLARATOR &&
                    ((AstDeclaratorNode*)declaration)->id == other) continue;
            // The backing array must never escape through an alias or callback.
            AstNode* current = other;
            // Every intermediate array stays on the indexed path; aliases can mutate unseen leaves.
            for (int level = 0; level <= indexed_depth; level++) {
                AstNode* use = ast_index_parent(tree, current);
                if (use && use->node_type == AST_NODE_ASSIGN && ((AstAssignNode*)use)->left == current) {
                    if (level == indexed_depth ? !safe(use) : ((AstAssignNode*)use)->op != OPERATOR_ASSIGN ||
                            !array_seed(array_seed, ((AstAssignNode*)use)->right, indexed_depth - level)) return false;
                    if (!level) seeded = true;
                    break;
                }
                if (level == indexed_depth) break;
                if (use && use->node_type == AST_NODE_MEMBER_EXPR && ((AstFieldNode*)use)->object == current &&
                        (named(member_spelling((AstFieldNode*)use), "push") ||
                        (level + 1 == indexed_depth && named(member_spelling((AstFieldNode*)use), "pop")))) {
                    AstNode* consumer = ast_index_parent(tree, use);
                    if (!consumer || consumer->node_type != AST_NODE_CALL_EXPR || ((AstCallNode*)consumer)->callee != use ||
                            ((AstCallNode*)consumer)->interp_has_spread_args) return false;
                    if (named(member_spelling((AstFieldNode*)use), "push"))
                        for (AstNode* argument = ((AstCallNode*)consumer)->arguments; argument; argument = argument->next)
                            if (!array_seed(array_seed, argument, indexed_depth - level - 1)) return false;
                    break;
                }
                if (use && (use->node_type == AST_NODE_MEMBER_EXPR || use->node_type == AST_NODE_INDEX_EXPR) &&
                        ((AstFieldNode*)use)->object == current && named(member_spelling((AstFieldNode*)use), "length")) {
                    AstNode* consumer = ast_index_parent(tree, use);
                    if (consumer && ((consumer->node_type == AST_NODE_ASSIGN && ((AstAssignNode*)consumer)->left == use) ||
                            (consumer->node_type == AST_NODE_UNARY &&
                                (((AstUnaryNode*)consumer)->op == OPERATOR_JS_DELETE ||
                                 ((AstUnaryNode*)consumer)->op == OPERATOR_JS_INCREMENT ||
                                 ((AstUnaryNode*)consumer)->op == OPERATOR_JS_DECREMENT)))) return false;
                    break; // reading length exposes no array alias.
                }
                if (!use || (use->node_type != AST_NODE_MEMBER_EXPR && use->node_type != AST_NODE_INDEX_EXPR) ||
                        ((AstFieldNode*)use)->object != current || !((AstFieldNode*)use)->computed ||
                        !safe(((AstFieldNode*)use)->property)) return false;
                current = use;
            }
        }
        AstNode* target = NULL; AstNode* value = NULL;
        if (other->node_type == AST_NODE_ASSIGN) {
            target = ((AstAssignNode*)other)->left; value = other;
        } else if (other->node_type == AST_NODE_VARIABLE_DECLARATOR) {
            target = ((AstDeclaratorNode*)other)->id; value = ((AstDeclaratorNode*)other)->init;
        } else if (other->node_type == AST_NODE_UNARY &&
                (((AstUnaryNode*)other)->op == OPERATOR_JS_INCREMENT || ((AstUnaryNode*)other)->op == OPERATOR_JS_DECREMENT)) {
            target = ((AstUnaryNode*)other)->operand; value = other;
        }
        // implicit globals have no NameEntry; their closed write inventory still proves key kinds.
        bool matching = local || implicit_name ? matches_local(target) :
            target && (target->node_type == AST_NODE_MEMBER_EXPR || target->node_type == AST_NODE_INDEX_EXPR) &&
                named(member_spelling((AstFieldNode*)target), field->chars);
        if (matching && !indexed_field) {
            seeded = true;
            if (indexed_local ? other->node_type == AST_NODE_VARIABLE_DECLARATOR &&
                    !array_seed(array_seed, value, indexed_depth) : !safe(value)) return false;
        }
        if (field && other->node_type == AST_NODE_PROPERTY) {
            AstPropertyNode* property = (AstPropertyNode*)other;
            String* name = property_spelling(property->key, property->computed);
            if (name && named(name, field->chars)) {
                seeded = true;
                if (indexed_field ? !array_seed(array_seed, property->value, indexed_depth) : !safe(property->value)) return false;
            }
        }
        if (field && other->node_type == AST_NODE_METHOD &&
                named(property_spelling(((AstMethodNode*)other)->key, ((AstMethodNode*)other)->computed), field->chars)) return false;
        if (field && other->node_type == AST_NODE_FIELD) {
            AstClassFieldNode* member = (AstClassFieldNode*)other;
            if (!member->computed && member->key->node_type == AST_NODE_IDENT &&
                    named(((AstIdentNode*)member->key)->name, field->chars)) {
                seeded = true;
                if (indexed_field ? !array_seed(array_seed, member->value, indexed_depth) : !safe(member->value)) return false;
            }
        }
        if (!incoming) continue;
        if (other->node_type != AST_NODE_CALL_EXPR && other->node_type != AST_NODE_NEW_EXPR) continue;
        AstCallNode* call = (AstCallNode*)other;
        bool match = method ? (call->callee->node_type == AST_NODE_MEMBER_EXPR || call->callee->node_type == AST_NODE_INDEX_EXPR) &&
            named(member_spelling((AstFieldNode*)call->callee), method->chars) : class_constructor ?
            call->callee->node_type == AST_NODE_IDENT && class_plan(p, NULL,
                identifier_binding(p, (AstIdentNode*)call->callee)) == incoming->home : direct_target(p, call) == incoming;
        if (!match) continue;
        if (method && incoming->home) {
            MvpLmdClass* actual = property_receiver_class(p, ((AstFieldNode*)call->callee)->object);
            // A known unrelated nominal family cannot invoke this method through the same spelling.
            bool related = !actual;
            for (int level = 0; actual && level < 16; level++) {
                if (actual == incoming->home) { related = true; break; }
                if (!actual->ast || !actual->ast->superclass) break;
                MvpLmdClass* base = property_receiver_class(p, actual->ast->superclass);
                if (!base) { related = true; break; }
                actual = base;
            }
            if (!related) continue;
        }
        if (call->interp_has_spread_args) return false;
        AstNode* argument = call->arguments;
        for (int j = 0; argument && j < parameter; j++) argument = argument->next;
        if (!safe(argument)) return false;
    }
    return seeded;
}
// An Array prototype cannot supply a user-only class method unless a visible
// named store installed it. The consumer either calls an element's method or
// throws; it cannot retain a builtin prototype/constructor for a later mutation.
static bool computed_read_is_observation(MvpLmdProgram* p, AstFieldNode* member) {
    const AstIndex* tree = &p->frontend->ast_index;
    AstNode* declaration = ast_index_parent(tree, (AstNode*)member);
    if (declaration && declaration->node_type == AST_NODE_BINARY &&
            (((AstBinaryNode*)declaration)->op == OPERATOR_JS_STRICT_EQ ||
             ((AstBinaryNode*)declaration)->op == OPERATOR_JS_STRICT_NE)) return true;
    if (declaration && declaration->node_type == AST_NODE_UNARY &&
            (((AstUnaryNode*)declaration)->op == OPERATOR_JS_TYPEOF ||
             ((AstUnaryNode*)declaration)->op == OPERATOR_NOT || ((AstUnaryNode*)declaration)->op == OPERATOR_JS_VOID)) return true;
    if (kind(p, member->object) != K_ARRAY && (!p->library ||
            property_receiver_class(p, member->object) != &p->library->array)) return false;
    LmdBinding* value = NULL;
    if (declaration && declaration->node_type == AST_NODE_VARIABLE_DECLARATOR) {
        if (((AstDeclaratorNode*)declaration)->init != (AstNode*)member ||
                ((AstDeclaratorNode*)declaration)->id->node_type != AST_NODE_IDENT) return false;
        value = identifier_binding(p, (AstIdentNode*)((AstDeclaratorNode*)declaration)->id);
        if (!value || value->writes != 1 || value->captured || value->reflected_global) return false;
    }
    bool used = false;
    for (AstNodeId i = 0; i < (value ? tree->count : 1); i++) {
        AstNode* use = value ? tree->nodes[i] : (AstNode*)member;
        if (value && (use->node_type != AST_NODE_IDENT || identifier_binding(p, (AstIdentNode*)use) != value ||
                use == ((AstDeclaratorNode*)declaration)->id)) continue;
        AstNode* parent = ast_index_parent(tree, use);
        if (!parent || (parent->node_type != AST_NODE_MEMBER_EXPR && parent->node_type != AST_NODE_INDEX_EXPR) ||
                ((AstFieldNode*)parent)->object != use) return false;
        String* name = member_spelling((AstFieldNode*)parent);
        AstNode* call = ast_index_parent(tree, parent);
        if (!name || named(name, "constructor") || mvp_lmd_builtin_method(name) ||
                !call || call->node_type != AST_NODE_CALL_EXPR || ((AstCallNode*)call)->callee != parent) return false;
        bool declared = false;
        for (int fi = 1; fi < p->functions->length; fi++) {
            LmdFunction* method = (LmdFunction*)p->functions->data[fi];
            if (method->home && method->ast->node_type == AST_NODE_METHOD &&
                    named(property_spelling(((AstMethodNode*)method->ast)->key, false), name->chars)) declared = true;
        }
        if (!declared) return false;
        for (AstNodeId j = 0; j < tree->count; j++) {
            AstNode* n = tree->nodes[j];
            if (n->node_type != AST_NODE_ASSIGN) continue;
            AstNode* target = ((AstAssignNode*)n)->left;
            if ((target->node_type == AST_NODE_MEMBER_EXPR || target->node_type == AST_NODE_INDEX_EXPR) &&
                    named(member_spelling((AstFieldNode*)target), name->chars)) return false;
        }
        used = true;
    }
    return used;
}
// fixed prototype proofs require a passive, nonescaping setup.
static bool fixed_prototype_setup(MvpLmdProgram* p) {
    const AstIndex* tree = &p->frontend->ast_index;
    auto same = [&](auto&& equal, AstNode* a, AstNode* b, int depth) -> bool {
        if (!a || !b || depth == 16) return false;
        if (a->node_type == AST_NODE_IDENT && b->node_type == AST_NODE_IDENT)
            return identifier_binding(p, (AstIdentNode*)a) &&
                identifier_binding(p, (AstIdentNode*)a) == identifier_binding(p, (AstIdentNode*)b);
        if ((a->node_type != AST_NODE_MEMBER_EXPR && a->node_type != AST_NODE_INDEX_EXPR) ||
                (b->node_type != AST_NODE_MEMBER_EXPR && b->node_type != AST_NODE_INDEX_EXPR)) return false;
        String* x = member_spelling((AstFieldNode*)a), *y = member_spelling((AstFieldNode*)b);
        return x && y && x->len == y->len && !memcmp(x->chars, y->chars, x->len) &&
            equal(equal, ((AstFieldNode*)a)->object, ((AstFieldNode*)b)->object, depth + 1);
    };
    auto constructor = [&](auto&& fixed, AstNode* owner, int depth) -> bool {
        if (!owner || depth == 16) return false;
        if (owner->node_type == AST_NODE_IDENT) {
            LmdBinding* binding = identifier_binding(p, (AstIdentNode*)owner);
            return binding && !binding->assigned && (!binding->reflected_global || binding->fixed_global) && binding->target &&
                !binding->target->home && !binding->target->ast->is_generator && !binding->target->ast->is_async;
        }
        if (owner->node_type != AST_NODE_MEMBER_EXPR || !member_spelling((AstFieldNode*)owner) ||
                !fixed(fixed, ((AstFieldNode*)owner)->object, depth + 1)) return false;
        int definitions = 0;
        for (AstNodeId i = 0; i < tree->count; i++) {
            AstNode* node = tree->nodes[i];
            AstNode* target = node->node_type == AST_NODE_ASSIGN ? ((AstAssignNode*)node)->left :
                node->node_type == AST_NODE_UNARY ? ((AstUnaryNode*)node)->operand : NULL;
            if (!same(same, owner, target, 0)) continue;
            if (node->node_type != AST_NODE_ASSIGN || ((AstAssignNode*)node)->op != OPERATOR_ASSIGN ||
                    ((AstAssignNode*)node)->right->node_type != AST_NODE_FUNC_EXPR) return false;
            AstNode* statement = ast_index_parent(tree, node);
            if (!statement || statement->node_type != AST_NODE_EXPR_STMT ||
                    ast_index_parent(tree, statement) != p->frontend->ast_root ||
                    statement->source_span.start_byte >= owner->source_span.start_byte) return false;
            definitions++;
        }
        return definitions == 1;
    };
    uint32_t last = 0;
    bool any = false;
    for (AstNodeId i = 0; i < tree->count; i++) {
        AstNode* node = tree->nodes[i];
        if ((node->node_type != AST_NODE_MEMBER_EXPR && node->node_type != AST_NODE_INDEX_EXPR) ||
                !named(member_spelling((AstFieldNode*)node), "prototype")) continue;
        if (p->global_reflection && !p->global_mutations_closed) return false;
        AstNode* member = ast_index_parent(tree, node);
        if (!member || member->node_type != AST_NODE_MEMBER_EXPR || ((AstFieldNode*)member)->object != node ||
                !member_spelling((AstFieldNode*)member) || named(member_spelling((AstFieldNode*)member), "constructor")) return false;
        AstNode* owner = ((AstFieldNode*)node)->object;
        LmdBinding* builtin = owner->node_type == AST_NODE_IDENT ? identifier_binding(p, (AstIdentNode*)owner) : NULL;
        TypeMap* native = p->library && builtin ?
            builtin->intrinsic == I_OBJECT ? &p->library->object.prototype_shape :
            builtin->intrinsic == I_REGEXP ? &p->library->regexp.prototype_shape : NULL : NULL;
        if (native) {
            String* name = member_spelling((AstFieldNode*)member);
            ShapeEntry* field = typemap_hash_lookup(native, name->chars, name->len);
            AstNode* forwarding = ast_index_parent(tree, member);
            if (!field || field->type->type_id != LMD_TYPE_FUNC || (field->flags & JSPD_IS_ACCESSOR) ||
                    !forwarding || forwarding->node_type != AST_NODE_MEMBER_EXPR ||
                    ((AstFieldNode*)forwarding)->object != member) return false;
            String* operation = member_spelling((AstFieldNode*)forwarding);
            AstNode* call = ast_index_parent(tree, forwarding);
            // borrowing a native method does not expose its owning prototype;
            // every other prototype use and computed escape still needs its own proof.
            if ((!named(operation, "call") && !named(operation, "apply") && !named(operation, "bind")) ||
                    !call || call->node_type != AST_NODE_CALL_EXPR || ((AstCallNode*)call)->callee != forwarding) return false;
            continue;
        }
        any = true;
        if (!constructor(constructor, owner, 0)) return false;
        AstNode* assignment = ast_index_parent(tree, member);
        if (!assignment || assignment->node_type != AST_NODE_ASSIGN || ((AstAssignNode*)assignment)->left != member ||
                ((AstAssignNode*)assignment)->op != OPERATOR_ASSIGN) return false;
        AstNode* statement = ast_index_parent(tree, assignment);
        if (!statement || statement->node_type != AST_NODE_EXPR_STMT ||
                ast_index_parent(tree, statement) != p->frontend->ast_root) return false;
        if (statement->source_span.start_byte > last) last = statement->source_span.start_byte;
    }
    if (!any) return true;
    auto passive = [&](auto&& safe, AstNode* node, int depth) -> bool {
        if (!node) return true;
        if (depth == 32) return false;
        auto visit = [&](AstNode* value) { return safe(safe, value, depth + 1); };
        switch (node->node_type) {
        case AST_NODE_LITERAL: case AST_NODE_IDENT: case AST_NODE_NULL:
        case AST_NODE_FUNC: case AST_NODE_FUNC_EXPR: case AST_NODE_ARROW_FUNC: return true;
        case AST_NODE_EXPR_STMT: return visit(((AstExprStmtNode*)node)->expression);
        case AST_NODE_VARIABLE_DECLARATOR: return visit(((AstDeclaratorNode*)node)->init);
        case AST_NODE_RETURN_STAM: return visit(((AstReturnNode*)node)->value);
        case AST_NODE_VAR_STAM: case AST_NODE_BLOCK: case AST_NODE_ARRAY: {
            AstNode* first = node->node_type == AST_NODE_VAR_STAM ? ((AstVarDeclNode*)node)->declarations :
                node->node_type == AST_NODE_BLOCK ? ((AstBlockNode*)node)->statements : ((AstArrayNode*)node)->item;
            for (AstNode* child = first; child; child = child->next) if (!visit(child)) return false;
            return true;
        }
        case AST_NODE_MAP: case AST_NODE_OBJECT_LITERAL:
            for (AstNode* property = ((AstMapNode*)node)->properties; property; property = property->next) {
                if (property->node_type != AST_NODE_PROPERTY) return false;
                AstPropertyNode* field = (AstPropertyNode*)property;
                if (field->is_getter || field->is_setter || (field->computed && !visit(field->key)) || !visit(field->value)) return false;
            }
            return true;
        case AST_NODE_CALL_EXPR: {
            AstCallNode* call = (AstCallNode*)node;
            if (call->arguments || call->interp_has_spread_args || call->callee->node_type != AST_NODE_FUNC_EXPR) return false;
            AstFuncNode* function = (AstFuncNode*)call->callee;
            return !function->params && !function->is_generator && !function->is_async && visit(function->body);
        }
        case AST_NODE_NEW_EXPR: {
            AstCallNode* call = (AstCallNode*)node;
            LmdBinding* target = call->callee->node_type == AST_NODE_IDENT ?
                identifier_binding(p, (AstIdentNode*)call->callee) : NULL;
            // Reuse construction analysis for the intrinsic Array allocator;
            // user constructors can observe inherited members during setup.
            return target && target->intrinsic == I_ARRAY &&
                constructor_layout_reusable(p, node, true, true);
        }
        case AST_NODE_ASSIGN: {
            AstAssignNode* assignment = (AstAssignNode*)node;
            if (assignment->op != OPERATOR_ASSIGN || !visit(assignment->right)) return false;
            AstNode* target = assignment->left;
            if (target->node_type == AST_NODE_IDENT) return true;
            if (target->node_type != AST_NODE_MEMBER_EXPR) return false;
            AstNode* owner = ((AstFieldNode*)target)->object;
            if (owner->node_type == AST_NODE_MEMBER_EXPR && named(member_spelling((AstFieldNode*)owner), "prototype"))
                owner = ((AstFieldNode*)owner)->object;
            return constructor(constructor, owner, 0);
        }
        default: return false;
        }
    };
    for (AstNode* statement = ((AstScript*)p->frontend->ast_root)->body; statement &&
            statement->source_span.start_byte <= last; statement = statement->next)
        if (!passive(passive, statement, 0)) {
            log_debug("js mvp prototype setup: observable statement at byte %u", statement->source_span.start_byte);
            return false;
        }
    return true;
}
// Reflection publishes a binding; only writes or reflective escapes invalidate its function identity.
static void plan_fixed_global_bindings(MvpLmdProgram* p) {
    if (!p->global_reflection || p->dynamic_source || p->property_callbacks || p->global_mutations_closed) return;
    const AstIndex* tree = &p->frontend->ast_index;
    for (AstNodeId i = 0; i < tree->count; i++) {
        AstNode* node = tree->nodes[i];
        if (node->node_type == AST_NODE_MAP_PATTERN || node->node_type == AST_NODE_ARRAY_PATTERN ||
                node->node_type == AST_NODE_SPREAD) return;
        // iteration can write a global alias or an intrinsic method without an assignment node.
        if ((node->node_type == AST_NODE_FOR_IN_STAM || node->node_type == AST_NODE_FOR_OF_STAM) &&
                ((JsForOfNode*)node)->left->node_type != AST_NODE_IDENT) return;
        if (node->node_type == AST_NODE_IDENT) {
            AstNode* parent = ast_index_parent(tree, node);
            if (!js_ast_fact_walk_skips_child(parent, node) &&
                    named(((AstIdentNode*)node)->name, "require")) return;
            bool write = parent && ((parent->node_type == AST_NODE_ASSIGN && ((AstAssignNode*)parent)->left == node) ||
                parent->node_type == AST_NODE_UNARY || parent->node_type == AST_NODE_FOR_IN_STAM ||
                parent->node_type == AST_NODE_FOR_OF_STAM);
            LmdBinding* b = identifier_binding(p, (AstIdentNode*)node);
            if (write && b && b->reflected_intrinsic) return;
        }
        if (node->node_type != AST_NODE_MEMBER_EXPR && node->node_type != AST_NODE_INDEX_EXPR) continue;
        AstFieldNode* member = (AstFieldNode*)node;
        String* key = member_spelling(member);
        // These operations can write arbitrary names or recover a dynamic-code constructor through aliases.
        if (named(key, "assign") || named(key, "constructor") || named(key, "__defineGetter__") ||
                named(key, "__defineSetter__")) return;
        // A global alias must not recover a builtin that bypassed the ordinary intrinsic escape inventory.
        for (const char* intrinsic : intrinsic_names) if (named(key, intrinsic)) return;
        AstNode* parent = ast_index_parent(tree, node);
        bool write = parent && ((parent->node_type == AST_NODE_ASSIGN && ((AstAssignNode*)parent)->left == node) ||
            parent->node_type == AST_NODE_UNARY);
        if (write && key) {
            const JsBuiltinOwner owners[] = {JS_BUILTIN_OWNER_MATH_METHOD, JS_BUILTIN_OWNER_STRING_STATIC_METHOD,
                JS_BUILTIN_OWNER_NUMBER_STATIC_METHOD, JS_BUILTIN_OWNER_OBJECT_STATIC_METHOD, JS_BUILTIN_OWNER_ARRAY_STATIC_METHOD};
            for (JsBuiltinOwner owner : owners)
                if (js_builtin_catalog_find(owner, key->chars, key->len)) return;
        }
    }
    LmdBinding* reflected[I_COUNT] = {};
    for (AstNodeId i = 0; i < tree->count; i++) {
        LmdBinding* b = p->intrinsic_uses[i];
        if (b && b->reflected_intrinsic) {
            reflected[b->reflected_intrinsic] = b;
            p->intrinsic_uses[i] = &p->intrinsics[b->reflected_intrinsic - 1];
        }
    }
    bool math_observed = p->math_observed, primitive_overrides = p->primitive_method_overrides;
    p->math_observed = p->global_math_observed;
    p->primitive_method_overrides = p->global_primitive_method_overrides;
    // Prove the restored domains inductively: every possible computed lookup must exclude
    // builtin/global names. A failed proof restores all original bindings and facts.
    reset_representation_facts(p); representation_pass(p);
    bool safe = !p->diagnostic[0];
    for (AstNodeId i = 0; safe && i < tree->count; i++) {
        AstNode* node = tree->nodes[i];
        if (node->node_type != AST_NODE_MEMBER_EXPR && node->node_type != AST_NODE_INDEX_EXPR) continue;
        AstFieldNode* member = (AstFieldNode*)node;
        String* key = member_spelling(member);
        if (!key) {
            AstNode* path[48];
            safe = primitive_property_key(p, member->property, path);
        }
    }
    if (!safe) {
        for (AstNodeId i = 0; i < tree->count; i++) {
            LmdBinding* b = p->intrinsic_uses[i];
            if (b && b->intrinsic && reflected[b->intrinsic]) p->intrinsic_uses[i] = reflected[b->intrinsic];
        }
        p->math_observed = math_observed; p->primitive_method_overrides = primitive_overrides;
        reset_representation_facts(p); representation_pass(p);
        return;
    }
    p->global_mutations_closed = true;
    bool changed = false;
    bool projects_values = false;
    for (AstNodeId i = 0; i < tree->count; i++) {
        AstNode* node = tree->nodes[i];
        if (node->node_type != AST_NODE_MEMBER_EXPR && node->node_type != AST_NODE_INDEX_EXPR) continue;
        String* key = member_spelling((AstFieldNode*)node);
        projects_values |= named(key, "values") || named(key, "entries");
    }
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        if (!b->reflected_global || !b->entry->scope || b->implicit_write || b->entry->is_annex_b_companion) continue;
        String* name = b->entry->name;
        bool intrinsic = false;
        for (const char* builtin : intrinsic_names) intrinsic |= named(name, builtin);
        if (intrinsic) continue; // var declarations can initially observe the existing builtin value.
        // Native array-like operations may write their own field names without an explicit source store.
        if (native_property_matches(p, [&](const char* bytes, size_t length) {
                return name->len == length && !memcmp(name->chars, bytes, length);
            })) continue;
        bool written = false, observed = b->source_observed || projects_values;
        for (AstNodeId j = 0; j < tree->count; j++) {
            AstNode* node = tree->nodes[j];
            if ((node->node_type == AST_NODE_MEMBER_EXPR || node->node_type == AST_NODE_INDEX_EXPR) &&
                    named(member_spelling((AstFieldNode*)node), name->chars)) observed = true;
            AstNode* target = node->node_type == AST_NODE_ASSIGN ? ((AstAssignNode*)node)->left :
                node->node_type == AST_NODE_UNARY ? ((AstUnaryNode*)node)->operand : NULL;
            if (target && (target->node_type == AST_NODE_MEMBER_EXPR || target->node_type == AST_NODE_INDEX_EXPR) &&
                    named(member_spelling((AstFieldNode*)target), name->chars)) { written = true; break; }
        }
        if (!written) {
            b->fixed_global = true; b->assigned = b->source_assigned;
            // A reflected var exists before its initializer, even when a function reads it early.
            b->seed_kinds = b->source_kinds | (b->target ? 0 : K_UNDEFINED);
            b->closed_global = b->target && !observed && !b->assigned;
            if (b->closed_global) b->target->reflected_entry = true;
            changed = true;
        }
    }
    if (changed) { reset_representation_facts(p); representation_pass(p); }
}
static thread_local StrBuf* mvp_lmd_code_writer;
static thread_local bool mvp_lmd_code_write_failed;
static StrBuf* mvp_lmd_code_image(MIR_context_t mir) {
    StrBuf* image = strbuf_new();
    if (!image) return NULL;
    StrBuf* previous = mvp_lmd_code_writer;
    bool previous_failed = mvp_lmd_code_write_failed;
    mvp_lmd_code_writer = image;
    mvp_lmd_code_write_failed = false;
    MIR_write_with_func(mir, [](MIR_context_t, uint8_t byte) -> int {
        size_t before = mvp_lmd_code_writer->length;
        strbuf_append_char(mvp_lmd_code_writer, (char)byte);
        if (mvp_lmd_code_writer->length == before + 1) return byte;
        mvp_lmd_code_write_failed = true;
        return EOF;
    });
    mvp_lmd_code_writer = previous;
    if (mvp_lmd_code_write_failed) { strbuf_free(image); image = NULL; }
    mvp_lmd_code_write_failed = previous_failed;
    return image;
}
static void timed_codegen_interface(MIR_context_t ctx, MIR_item_t item) {
    uint64_t before = time_now_us();
    MIR_set_gen_interface(ctx, item);
    log_notice("js-mvp-codegen: function=%s us=%llu", item ? item->u.func->name : "<link-finish>",
        (unsigned long long)(time_now_us() - before));
}
static bool process_stdio_only(MvpLmdProgram* p, bool existing_global, const MvpLmdHost* host,
    bool require_closed_keys = true);
static void plan_native_host_uses(MvpLmdProgram* p) {
    if (!p->native_host_values || !p->execution_owner || p->source_module) return;
    // retaining the complete process surface for unknown keys does not make
    // its unescaped native carriers reachable in unrelated functions.
    p->closed_stdio = process_stdio_only(p, program_has_existing_global(p), p->execution_owner->host, false);
    if (!p->closed_stdio) return;
    // unobserved direct writes share the canonical kernel without allocating the process namespace.
    p->direct_stdio = process_stdio_only(p, program_has_existing_global(p), p->execution_owner->host);
    if (p->direct_stdio) {
        int argc = 0;
        const char* const* argv = js_process_bootstrap_arguments(&argc);
        const MvpLmdHost* host = p->execution_owner->host;
        // custom embedder arguments still require publication into the canonical process state.
        p->direct_stdio = host->argc == argc && host->argv == argv;
    }
    // the stream proof forbids escaping host values. Only functions containing
    // those source reads need native carrier/call guards, including when inlined.
    const AstIndex* tree = &p->frontend->ast_index;
    for (AstNodeId i = 0; i < tree->count; i++) {
        LmdBinding* binding = p->intrinsic_uses[i];
        if (!binding || binding->intrinsic != I_PROCESS) continue;
        AstNode* owner = tree->nodes[i];
        LmdFunction* active = NULL;
        while (owner && !(active = function(p, owner))) owner = ast_index_parent(tree, owner);
        if (!active) active = (LmdFunction*)p->functions->data[0];
        active->uses_process = true;
        // an escaping writer must observe later mutations of the canonical stream.
        if (active->ast && !active->closed_calls) p->direct_stdio = false;
    }
}
static int lower_pass(void* opaque) {
    MvpLmdProgram* p = (MvpLmdProgram*)opaque;
    bool timing = p->frontend->pass_timing.enabled;
    uint64_t started = timing ? time_now_us() : 0;
    // creation facts depend on the definition, not the fresh closure's identity or realm.
    for (int i = 1; i < p->functions->length; i++) {
        LmdFunction* f = (LmdFunction*)p->functions->data[i];
        AstNode* parent = ast_index_parent(&p->frontend->ast_index, (AstNode*)f->ast);
        f->home_slot = f->object_home || f->lexical_super ? function_capture_count(f) + 1 : 0;
        f->non_constructible = f->object_home || f->ast->node_type == AST_NODE_ARROW_FUNC ||
            f->ast->is_generator || f->ast->is_async ||
            (parent && parent->node_type == AST_NODE_PROPERTY && (((AstPropertyNode*)parent)->method ||
                ((AstPropertyNode*)parent)->is_getter || ((AstPropertyNode*)parent)->is_setter));
        if (f->home) {
            AstMethodNode* method = (AstMethodNode*)f->ast;
            f->constructor = method->kind == AstMethodNode::JS_METHOD_CONSTRUCTOR;
            f->static_method = method->static_method;
            f->non_constructible = !f->constructor;
        }
    }
    p->global_bindings_exposed = global_bindings_are_exposed(p);
    plan_fixed_global_bindings(p);
    plan_native_host_uses(p);
    for (AstNodeId i = 0; i < p->frontend->ast_index.count; i++) {
        AstNode* node = p->frontend->ast_index.nodes[i];
        bool primitives = false;
        if (node->node_type == AST_NODE_MEMBER_EXPR || node->node_type == AST_NODE_INDEX_EXPR) {
            AstFieldNode* method = (AstFieldNode*)node;
            String* name = member_spelling(method);
            if (!p->function_prototype_observed &&
                    (named(name, "caller") || named(name, "arguments") || named(name, "constructor")) &&
                    (kind(p, method->object) & K_FUNCTION)) {
                // constructor metadata needs the Function prototype even when
                // no dynamic source is invoked; ordinary objects keep their plan.
                bool had_library = p->library_active;
                activate_library(p, node, false); p->function_prototype_observed = true;
                if (!had_library) plan_library_shapes(p);
                if (p->diagnostic[0]) return false;
            }
            // shared spellings such as Vector.at/Array.concat must not disable unrelated direct string calls.
            primitives = name && mvp_lmd_builtin_method(name, LMD_TYPE_STRING) >= LMD_METHOD_STRING_CONCAT &&
                (kind(p, method->object) & K_STRING);
            AstNode* parent = ast_index_parent(&p->frontend->ast_index, node);
            unsigned receivers = kind(p, method->object);
            constexpr unsigned primitive_receivers = K_STRING | K_NUMBER | K_BOOL | K_BIGINT | K_SYMBOL;
            if (!p->primitives && name && (receivers & primitive_receivers) &&
                    !(receivers & ~(primitive_receivers | K_NULL | K_UNDEFINED))) {
                // a named primitive read outside the direct catalog needs its realm prototype,
                // including absent names; preserve native string indices and direct method calls.
                const struct { unsigned kind; TypeId type; } families[] = {
                    {K_STRING, LMD_TYPE_STRING}, {K_NUMBER, LMD_TYPE_FLOAT}, {K_BOOL, LMD_TYPE_BOOL},
                    {K_BIGINT, LMD_TYPE_DECIMAL}, {K_SYMBOL, LMD_TYPE_SYMBOL}
                };
                bool called = parent && parent->node_type == AST_NODE_CALL_EXPR &&
                    ((AstCallNode*)parent)->callee == node;
                for (const auto& family : families) {
                    if (!(receivers & family.kind)) continue;
                    bool indexed = family.kind == K_STRING && mvp_lmd_string_key(name) >= -1;
                    if (!indexed && !(called && mvp_lmd_builtin_method(name, family.type))) primitives = true;
                }
            }
            if (parent && parent->node_type == AST_NODE_UNARY && ((AstUnaryNode*)parent)->op == OPERATOR_JS_DELETE)
                primitives |= (kind(p, method->object) & (K_STRING | K_NUMBER | K_BOOL | K_BIGINT | K_SYMBOL)) != 0;
            if (!p->primitives && !name && method->computed && (kind(p, method->object) & K_STRING)) {
                AstNode* path[48];
                if (!p->library && (kind(p, method->property) & (K_STRING | K_OBJECT | K_ARRAY | K_FUNCTION | K_MAP))) {
                    // the existing numeric-affix proof needs the native property inventory, not runtime prototypes.
                    plan_library_shapes(p, true);
                    if (p->diagnostic[0]) return false;
                }
                primitives = !primitive_property_key(p, method->property, path);
            }
        } else if (node->node_type == AST_NODE_CALL_EXPR) {
            AstCallNode* call = (AstCallNode*)node;
            if (call->callee->node_type != AST_NODE_MEMBER_EXPR) continue;
            AstFieldNode* method = (AstFieldNode*)call->callee;
            LmdBinding* owner = method->object->node_type == AST_NODE_IDENT ? identifier_binding(p, (AstIdentNode*)method->object) : NULL;
            String* name = member_spelling(method);
            primitives = owner && owner->intrinsic == I_OBJECT && (named(name, "hasOwn") || named(name, "keys") ||
                named(name, "values") || named(name, "entries")) &&
                (kind(p, call->arguments) & ~(K_OBJECT | K_MAP | K_FUNCTION | K_ARRAY | K_TYPED_ARRAY));
        }
        if (primitives && !p->primitives) {
            // boxing is needed only for possible primitive receivers; plain projections retain their ABI.
            log_debug("js mvp primitive admission: builtin metadata at byte %u", node->source_span.start_byte);
            activate_library(p, node); p->primitives = true; plan_library_shapes(p);
        }
    }
    if (!p->property_callbacks && !fixed_prototype_setup(p)) {
        p->property_callbacks = p->array_method_overrides = p->map_method_overrides = p->primitive_method_overrides = true;
    }
    if (!p->property_callbacks) {
        for (AstNodeId i = 0; i < p->frontend->ast_index.count; i++) {
            AstNode* node = p->frontend->ast_index.nodes[i];
            bool callbacks = false;
            if (node->node_type == AST_NODE_PROPERTY) {
                AstPropertyNode* property = (AstPropertyNode*)node;
                AstNode* path[48];
                if (!property->computed || primitive_property_key(p, property->key, path)) continue;
                callbacks = true;
            } else {
                if (node->node_type != AST_NODE_MEMBER_EXPR && node->node_type != AST_NODE_INDEX_EXPR) continue;
                AstFieldNode* member = (AstFieldNode*)node;
                AstNode* path[48];
                if (member->computed && !member_spelling(member) && !p->library &&
                        (kind(p, member->property) & (K_STRING | K_OBJECT | K_ARRAY | K_FUNCTION | K_MAP))) {
                    // The key proof needs the same native property inventory as runtime lookup.
                    plan_library_shapes(p, true);
                    if (p->diagnostic[0]) return false;
                }
                if (!member->computed || member_spelling(member) || primitive_property_key(p, member->property, path)) continue;
                // A computed read can expose a prototype or method for a later aliased write/call.
                callbacks = !computed_read_is_observation(p, member);
            }
            // arbitrary string keys can name prototype or coercion hooks through aliases.
            if (callbacks) log_debug("js mvp property admission: semantic dispatch at byte %u", node->source_span.start_byte);
            bool had_library = p->library_active;
            activate_library(p, node); p->property_callbacks = callbacks;
            if (callbacks) p->array_method_overrides = p->map_method_overrides = p->primitive_method_overrides = true;
            if (!had_library) plan_library_shapes(p);
            if (p->diagnostic[0]) return false;
            if (callbacks) break;
        }
    }
    // late key/prototype proofs can expose the function family after initial slot planning.
    if (p->generator_functions && p->property_callbacks && !p->generator_function_slot) {
        plan_library_shapes(p);
        if (p->diagnostic[0]) return false;
    }
    for (int i = 0; i < p->classes->length; i++) {
        MvpLmdClass* cls = (MvpLmdClass*)p->classes->data[i];
        if (cls->constructor_id >= 0 && !cls->ast->superclass) {
            LmdFunction* constructor = (LmdFunction*)p->functions->data[cls->constructor_id];
            // parameter initializers can inspect or expose this before the proven body starts.
            cls->reusable_layout = !p->property_callbacks && !cls->initializer_count && !constructor->complex_params &&
                constructor_layout_reusable(p, constructor->ast->body, false, true);
            if (cls->reusable_layout) {
                AstNode* first = ((AstBlockNode*)constructor->ast->body)->statements;
                cls->nullable_initializers = (String**)pool_calloc(p->frontend->pool,
                    ast_linked_node_count(first) * sizeof(String*));
                if (first && !cls->nullable_initializers) {
                    diagnostic(p, (AstNode*)constructor->ast, "nullable initializer allocation"); return false;
                }
                for (AstNode* n = first; n; n = n->next) {
                    AstAssignNode* assign = (AstAssignNode*)((AstExprStmtNode*)n)->expression;
                    String* name = member_spelling((AstFieldNode*)assign->left);
                    // the last assignment determines whether this field has a literal-null initializer.
                    for (int j = 0; j < cls->nullable_initializer_count; j++)
                        if (named(cls->nullable_initializers[j], name->chars)) {
                            cls->nullable_initializers[j] = cls->nullable_initializers[--cls->nullable_initializer_count];
                            break;
                        }
                    if (assign->right->node_type == AST_NODE_LITERAL &&
                            ((AstLiteralNode*)assign->right)->literal_type == AST_LITERAL_NULL)
                        cls->nullable_initializers[cls->nullable_initializer_count++] = name;
                }
            }
        }
        bool ok = plan_callable_shapes(p, cls, 4, JSPD_NON_WRITABLE,
            JSPD_NON_WRITABLE | JSPD_NON_CONFIGURABLE);
        for (AstNode* n = ((AstBlockNode*)cls->ast->body)->statements; n; n = n->next) {
            if (cls->dynamic) break;
            if (n->node_type != AST_NODE_METHOD) continue;
            AstMethodNode* method = (AstMethodNode*)n;
            if (method->kind == AstMethodNode::JS_METHOD_CONSTRUCTOR) continue;
            uint8_t flags = JSPD_NON_ENUMERABLE;
            if (method->kind == AstMethodNode::JS_METHOD_GET || method->kind == AstMethodNode::JS_METHOD_SET)
                continue; // virtual cells are attached to the actual prototype at class evaluation.
            ok &= plan_shape_field(p, method->static_method ? &cls->static_shape : &cls->prototype_shape,
                ((AstIdentNode*)method->key)->name, LMD_TYPE_FUNC, flags);
        }
        if (!ok) { diagnostic(p, (AstNode*)cls->ast, "class shape allocation"); return false; }
        typemap_hash_build(&cls->prototype_shape, p->frontend->pool);
        typemap_hash_build(&cls->static_shape, p->frontend->pool);
    }
    p->objects = arraylist_new(8);
    plan_objects(p->frontend->ast_root, p);
    if (p->diagnostic[0]) return false;
    LmdShapes shapes = {p, NULL, true};
    while (shapes.changed) { shapes.changed = false; shape_walk(p->frontend->ast_root, &shapes); }
    plan_deleted_shapes(p);
    if (p->diagnostic[0]) return false;
    plan_scalar_objects(p);
    // proven unobservable fields feed the existing type/range solver, including accumulator lanes.
    reset_representation_facts(p);
    if (!representation_pass(p) || p->diagnostic[0]) return false;
    for (int i = 1; i < p->functions->length; i++)
        plan_numeric_region(p, (LmdFunction*)p->functions->data[i]);
    for (int i = 1; i < p->functions->length; i++) {
        LmdFunction* f = (LmdFunction*)p->functions->data[i];
        if (!f->numeric_region) plan_numeric_region(p, f, true);
    }
    if (p->diagnostic[0]) return false;
    // unknown array writes can now create holes; proven dense writes retain their old read path.
    for (AstNodeId i = 0; i < p->frontend->ast_index.count; i++) {
        AstNode* n = p->frontend->ast_index.nodes[i];
        AstNode* target = n->node_type == AST_NODE_ASSIGN ? ((AstAssignNode*)n)->left :
            n->node_type == AST_NODE_UNARY && (((AstUnaryNode*)n)->op == OPERATOR_JS_INCREMENT ||
                ((AstUnaryNode*)n)->op == OPERATOR_JS_DECREMENT) ? ((AstUnaryNode*)n)->operand : NULL;
        if (!target || (target->node_type != AST_NODE_MEMBER_EXPR && target->node_type != AST_NODE_INDEX_EXPR)) continue;
        AstFieldNode* field = (AstFieldNode*)target;
        if (!(kind(p, field->object) & K_ARRAY)) continue;
        if (!field->computed) {
            if (named(member_spelling(field), "length")) p->array_holes = true;
            else p->array_named_properties = true;
            continue;
        }
        LmdRange index = range(p, field->property, true);
        LmdArrayFacts array = array_facts(p, field->object);
        // the same conservative write scan proves whether "undefined" can be an own field.
        p->array_named_properties |= kind(p, field->property) != K_NUMBER ||
            index.state != 1 || index.lower < 0 || index.upper >= UINT32_MAX;
        p->array_holes |= !(index.state == 1 && array.length.state == 1 && index.lower >= 0 && index.upper <= array.length.lower) &&
            !dense_append_loop(p, field, n);
    }
    p->excludes_sparse_arrays = unit_excludes_sparse_arrays(p);
    if (!p->excludes_sparse_arrays && !p->property_callbacks && !p->global_reflection && !p->iterators && !p->symbols)
        for (int i = 0; i < p->bindings->length; i++) {
            LmdBinding* binding = (LmdBinding*)p->bindings->data[i];
            binding->dense_array_storage = unit_excludes_sparse_arrays(p, binding);
        }
    // fixed property lookup can still reach an ordinary own coercion method; audit those definitions separately.
    p->coercion_callbacks |= p->property_callbacks || p->global_reflection || p->iterators || p->symbols;
    for (AstNodeId i = 0; !p->coercion_callbacks && i < p->frontend->ast_index.count; i++) {
        AstNode* node = p->frontend->ast_index.nodes[i];
        String* name = node->node_type == AST_NODE_PROPERTY ? property_spelling(
            ((AstPropertyNode*)node)->key, ((AstPropertyNode*)node)->computed) :
            node->node_type == AST_NODE_METHOD ? property_spelling(((AstMethodNode*)node)->key, ((AstMethodNode*)node)->computed) :
            node->node_type == AST_NODE_FIELD ? property_spelling(((AstClassFieldNode*)node)->key, ((AstClassFieldNode*)node)->computed) :
            node->node_type == AST_NODE_MEMBER_EXPR || node->node_type == AST_NODE_INDEX_EXPR ? member_spelling((AstFieldNode*)node) : NULL;
        if (named(name, "toString") || named(name, "valueOf") || named(name, "join")) p->coercion_callbacks = true;
    }
    p->import_cache = em_import_cache_new(32);
    uint64_t planned = timing ? time_now_us() : 0;
    p->mir = MIR_init(); p->module = MIR_new_module(p->mir, "js_mvp_lmd");
    MIR_type_t result_type = MIR_T_I64;
    int entry_arity = p->receiver_abi ? 7 : 5;
    MIR_var_t arguments[7]; memcpy(arguments, entry_arguments, entry_arity * sizeof(MIR_var_t));
    p->entry.proto = MIR_new_proto_arr(p->mir, "mvp_lmd_signature", 1, &result_type, entry_arity, arguments);
    p->entry.audit = (JitImportMetadata)AUDIT(JIT_EFFECT_MAY_GC, SCALAR,
        ARG(0,RAW_PTR)|ARG(1,RAW_PTR)|ARG(2,RAW_PTR)|ARG(3,SCALAR)|ARG(4,ITEM));
    if (p->receiver_abi) p->entry.audit.arg_classes |= ARG(5,ITEM)|ARG(6,ITEM);
    em_normalize_import_call(&p->entry, MIR_T_I64, entry_arity, arguments, 1);
    for (int i = 0; i < p->functions->length; i++) {
        LmdFunction* f = (LmdFunction*)p->functions->data[i];
        f->forward = MIR_new_forward(p->mir, f->name);
        // closed nonnumeric results never need a pending scalar payload or a caller number home.
        ScalarReturnClass scalar = f->native || (f->closed_calls && !(f->returns & K_NUMBER))
            ? SCALAR_RETURN_NONE : SCALAR_RETURN_F64;
        f->return_abi.shape = em_return_shape(f->native, f->native, scalar);
        f->return_abi.companion = em_companion_transport(f->return_abi.shape, !f->native);
        f->return_abi.normal = {f->native ? (f->returns == K_NUMBER ? (f->integer ? LMD_TYPE_INT : LMD_TYPE_FLOAT) : LMD_TYPE_BOOL) : LMD_TYPE_ANY,
            f->native ? (f->returns == K_NUMBER ? (f->integer ? VALUE_REP_INT_LANE : VALUE_REP_F64) : VALUE_REP_I64) : VALUE_REP_ITEM,
            scalar};
        if (f->native) {
            f->return_abi.error_lane = FN_ERROR_LANE_CONTEXT_ITEM;
            f->return_abi.error = {LMD_TYPE_ERROR, VALUE_REP_ITEM, SCALAR_RETURN_NONE};
        }
        if (f->direct_args) {
            MIR_type_t types[LAMBDA_MAX_FUNCTION_ARGS] = {MIR_T_P, MIR_T_P};
            uint64_t classes = ARG(0,RAW_PTR) | ARG(1,RAW_PTR);
            f->arity = 2;
            for (AstNode* a = f->ast->params; a; a = a->next) {
                LmdBinding* b = binding(p, js_ast_parameter_binding_identifier(a)->entry);
                types[f->arity] = b->native && b->kinds == K_NUMBER && !b->integer ? MIR_T_D : MIR_T_I64;
                classes |= b->native ? ARG(f->arity, SCALAR) : ARG(f->arity, ITEM); f->arity++;
            }
            mir_prepare_call_args(f->signature, types, f->arity);
            f->signature[0].name = "runtime"; f->signature[1].name = "unit";
            MIR_type_t results[] = {f->native && f->returns == K_NUMBER && !f->integer ? MIR_T_D : MIR_T_I64, MIR_T_I64};
            char name[64]; snprintf(name, sizeof(name), "%s_signature", f->name);
            f->entry.proto = MIR_new_proto_arr(p->mir, name, em_return_nres(f->return_abi.companion), results, f->arity, f->signature);
            f->entry.audit = (JitImportMetadata)AUDIT(JIT_EFFECT_MAY_GC, SCALAR, classes);
            em_normalize_import_call(&f->entry, results[0], f->arity, f->signature, em_return_nres(f->return_abi.companion));
            f->entry.call.return_shape = f->return_abi.shape;
            f->entry.call.abi = &f->return_abi;
        } else {
            f->arity = entry_arity; memcpy(f->signature, entry_arguments, entry_arity * sizeof(MIR_var_t));
        }
    }
    bool ok = true;
    uint32_t largest_function_insns = 0;
    for (int i = 0; i < p->functions->length && ok; i++) {
        LmdFunction* f = (LmdFunction*)p->functions->data[i];
        uint64_t before = timing ? time_now_us() : 0;
        uint32_t instruction_count = 0;
        ok = compile_function(p, f, &instruction_count);
        // reuse emission telemetry so link policy never rescans the MIR instruction lists.
        if (instruction_count > largest_function_insns) largest_function_insns = instruction_count;
        if (timing) log_notice("js-mvp-emit: function=%s us=%llu", f->name,
            (unsigned long long)(time_now_us() - before));
    }
    uint64_t emitted = timing ? time_now_us() : 0;
    MIR_finish_module(p->mir); mir_dump_finalized(p->mir, NULL, false);
    if (!ok) return false;
    StrBuf* image = p->relocatable ? mvp_lmd_code_image(p->mir) : NULL;
    if (p->relocatable && !image) return false;
    const MvpLmdCode* cached = p->cached_code;
    if (cached && image && cached->relocation_count == (size_t)p->relocations->length &&
            cached->addresses->length == p->functions->length &&
            cached->image->length == image->length &&
            !memcmp(cached->image->str, image->str, image->length)) {
        // only machine code is shared. The unit's slots, literal storage,
        // shapes, classes, AST and relocation table remain realm-owned.
        strbuf_free(image);
        MIR_finish(p->mir);
        p->code = const_cast<MvpLmdCode*>(cached);
        mvp_lmd_code_retain(p->code);
        p->mir = p->code->mir;
        for (int i = 0; i < p->functions->length; i++)
            ((LmdFunction*)p->functions->data[i])->address = p->code->addresses->data[i];
        log_debug("js-mvp-code-cache: reused %d functions", p->functions->length);
        return true;
    }
    if (cached) log_debug("js-mvp-code-cache: realm metadata changed emitted MIR; compile fresh code");
    MIR_load_module(p->mir, p->module); MIR_gen_init(p->mir); p->linked = true;
    // D8.1.3v29: first hot-entry satellites favor compile latency; large units share Lambda's bound.
    unsigned int optimize_level = mir_native_optimize_level(largest_function_insns,
        p->satellite_script ? 1 : 2);
    MIR_gen_set_optimize_level(p->mir, optimize_level);
    if (timing) log_notice("js-mvp-link: optimize_level=%u largest_function_insns=%llu",
        optimize_level, (unsigned long long)largest_function_insns);
    // MIR's eager link interface generates code here; later MIR_gen calls only retrieve it.
    MIR_link(p->mir, timing ? timed_codegen_interface : MIR_set_gen_interface, import_resolver);
    uint64_t linked = timing ? time_now_us() : 0;
    for (int i = 0; i < p->functions->length; i++) {
        LmdFunction* f = (LmdFunction*)p->functions->data[i]; f->address = MIR_gen(p->mir, f->item);
    }
    if (timing) log_notice("js-mvp-phases: parse_us=%llu bind_us=%llu validate_us=%llu plan_us=%llu emit_us=%llu finalize_link_codegen_us=%llu address_us=%llu",
        (unsigned long long)p->frontend->pass_timing.parse_build_us,
        (unsigned long long)p->frontend->pass_timing.bind_us,
        (unsigned long long)p->frontend->pass_timing.validate_us,
        (unsigned long long)(planned - started), (unsigned long long)(emitted - planned),
        (unsigned long long)(linked - emitted), (unsigned long long)(time_now_us() - linked));
    jit_log_code_addresses(p->mir);
    if (image) {
        auto* code = (MvpLmdCode*)mem_calloc(1, sizeof(MvpLmdCode), MEM_CAT_EVAL);
        ArrayList* addresses = arraylist_new(p->functions->length);
        if (!code || !addresses) {
            mem_free(code); arraylist_free(addresses); strbuf_free(image); return false;
        }
        for (int i = 0; i < p->functions->length; i++) {
            if (!arraylist_append(addresses, ((LmdFunction*)p->functions->data[i])->address)) {
                mem_free(code); arraylist_free(addresses); strbuf_free(image); return false;
            }
        }
        *code = {{1}, p->mir, image, addresses, (size_t)p->relocations->length};
        p->code = code;
    }
    return true;
}
static bool mvp_lmd_compile_unit(MvpLmdProgram* p, const char* source, size_t length, int stage = 2,
        const char* reference = NULL) {
    if (!p->frontend) {
    p->failure_phase = MVP_LMD_INTERNAL_FAILURE;
    p->frontend = js_transpiler_create(NULL);
    if (!p->frontend) return false;
    if (length > UINT32_MAX) { diagnostic(p, NULL, "source is too large"); return false; }
    // retained AST definitions and diagnostics need source storage for the execution's lifetime.
    char* retained = (char*)pool_alloc(p->frontend->pool, length + 1);
    if (!retained) { diagnostic(p, NULL, "source allocation failed"); return false; }
    memcpy(retained, source, length); retained[length] = '\0';
    p->frontend->strict_js = !p->typescript;
    if (p->typescript) p->frontend->strict_mode = true;
    p->failure_phase = MVP_LMD_PARSE_FAILURE;
    JsParseMode mode = p->source_module ? JS_PARSE_MODULE : JS_PARSE_SCRIPT;
    if (p->typescript) mode = (JsParseMode)(mode | JS_PARSE_TYPESCRIPT);
    if (!js_transpiler_parse_c(p->frontend, retained, length, mode) ||
            p->frontend->has_errors || !p->frontend->ast_root) {
        diagnostic(p, NULL, "JS parsing or validation failed"); return false;
    }
    if (p->source_module) p->frontend->reference = p->source_module->descriptor->path;
    else if (reference) {
        size_t size = strlen(reference) + 1;
        char* retained_reference = (char*)pool_alloc(p->frontend->pool, size);
        if (!retained_reference) return false;
        memcpy(retained_reference, reference, size);
        p->frontend->reference = retained_reference;
    }
    }
    p->failure_phase = MVP_LMD_ADMISSION_FAILURE;
    // continue the retained frontend schedule; lowering consumes its bound, validated index.
    CompilerPassManager* manager = &p->frontend->pass_manager;
    const CompilerPassSpec passes[] = {
        {"mvp-admit", COMPILER_FACT_FRONTEND | COMPILER_FACT_INDEXED,
            COMPILER_FACT_COLLECTED | COMPILER_FACT_CAPTURES | COMPILER_FACT_ENV_LAYOUT,
            admission_pass, p},
        {"mvp-plan", COMPILER_FACT_COLLECTED | COMPILER_FACT_CAPTURES | COMPILER_FACT_ENV_LAYOUT,
            COMPILER_FACT_INFERRED | COMPILER_FACT_ANALYZED | COMPILER_FACT_PLANNED,
            representation_pass, p},
        {"mvp-lower", COMPILER_FACT_PLANNED,
            COMPILER_FACT_MIR_LOWERED | COMPILER_FACT_FINALIZED | COMPILER_FACT_LINKED,
            lower_pass, p},
    };
    uint32_t count = stage == 0 ? 0 : stage == 1 ? 2 : 3;
    while (p->compilation_stage < count) {
        if (!compiler_pass_manager_add(manager, &passes[p->compilation_stage])) return false;
        p->compilation_stage++;
    }
    return compiler_pass_manager_run(manager, NULL);
}
static bool process_stdio_only(MvpLmdProgram* p, bool existing_global, const MvpLmdHost* host,
        bool require_closed_keys) {
    // only a closed, unobserved stream write can omit the optional process extension.
    if (!host || (host->output && host->output != stdout) || existing_global ||
            p->execution_owner->shared_realm || p->global_reflection || p->dynamic_source ||
            p->coercion_callbacks || p->timers || p->promises) return false;
    bool used = false;
    for (AstNodeId i = 0; i < p->frontend->ast_index.count; i++) {
        AstNode* node = p->frontend->ast_index.nodes[i];
        if (require_closed_keys && (node->node_type == AST_NODE_MEMBER_EXPR ||
                node->node_type == AST_NODE_INDEX_EXPR)) {
            auto* member = (AstFieldNode*)node;
            String* name = member_spelling(member);
            // indirect source constructors and unknown keys can expose the full global.
            if (named(name, "constructor") || (!name && kind(p, member->property) != K_NUMBER)) return false;
        }
        LmdBinding* intrinsic = p->intrinsic_uses[i];
        if (!intrinsic) continue;
        if (intrinsic->intrinsic == I_REQUIRE) return false;
        if (intrinsic->intrinsic != I_PROCESS) continue;
        used = true;
        for (int step = 0; step < 2; step++) {
            AstNode* parent = ast_index_parent(&p->frontend->ast_index, node);
            if (!parent || (parent->node_type != AST_NODE_MEMBER_EXPR &&
                    parent->node_type != AST_NODE_INDEX_EXPR)) return false;
            auto* member = (AstFieldNode*)parent;
            String* name = member_spelling(member);
            if (member->object != node || (step ? !named(name, "write") :
                    !named(name, "stdout") && !named(name, "stderr"))) return false;
            node = parent;
        }
        AstNode* parent = ast_index_parent(&p->frontend->ast_index, node);
        if (!parent || parent->node_type != AST_NODE_CALL_EXPR) return false;
        auto* call = (AstCallNode*)parent;
        if (call->callee != node || !call->arguments || call->arguments->next ||
                call->arguments->node_type == AST_NODE_SPREAD) return false;
    }
    return used;
}
static Item mvp_lmd_instantiate_unit(MvpLmdExecution* e, MvpLmdProgram* p, const MvpLmdHost* host) {
    for (int i = 0; p->objects && i < p->objects->length; i++) {
        LmdObjectPlan* plan = (LmdObjectPlan*)p->objects->data[i];
        // ordinary source templates acquire their nominal identity per constructor closure.
        if (plan->constructor && !plan->constructor->home) continue;
        TypeMap* shape = plan->constructor ? &plan->constructor->home->shape : p->roots[0];
        Input* tree = runtime_shape_tree();
        FOR_EACH_MAP_FIELD(plan->blueprint, field) {
            shape = shape && tree ? type_tree_add_map_field_chars(tree, shape,
                field->name->str, field->name->length, field->type->type_id, NULL) : NULL;
        }
        plan->shape = shape ? shape : plan->blueprint;
    }
    p->random_state = time_now_ns() ^ (uint64_t)e;
    if (p->slot_count) {
        p->slots = (Item*)mem_calloc((size_t)p->slot_count * 2, sizeof(Item), MEM_CAT_EVAL);
        if (!p->slots || !heap_try_register_gc_root_range((uint64_t*)p->slots, (int)p->slot_count)) return ItemError;
        p->slots_registered = true;
    }
    for (int i = 0; i < p->classes->length; i++) {
        MvpLmdClass* cls = (MvpLmdClass*)p->classes->data[i];
        cls->values = p->slots + cls->slot;
        cls->initializers = p->slots + cls->initializer_slot;
    }
    if (p->realm_program) {
        // unit-owned slots retain literals/classes; all host and intrinsic values have one realm owner.
        p->slots[p->global_slot] = p->realm_program->slots[p->realm_program->global_slot];
        p->slots[p->eval_slot] = p->realm_program->slots[p->realm_program->eval_slot];
        return ItemNull;
    }
    bool existing_global = false;
    if (p->global_receiver) {
        JsGlobalEnvironment* environment = !p->realm_key && js_runtime_state_for(e->owner_context)
            ? js_runtime_state.global_environment : NULL;
        if (environment) {
            if (!js_global_environment_ensure(environment)) return ItemError;
            p->slots[p->global_slot] = js_global_environment_slot(environment, JS_GLOBAL_ENV_GLOBAL_THIS);
            existing_global = get_type_id(p->slots[p->global_slot]) == LMD_TYPE_MAP;
        }
        if (!existing_global) {
            p->slots[p->global_slot] = mvp_lmd_object_new(p->roots[0], 0);
            if (item_is_error(p->slots[p->global_slot])) return p->slots[p->global_slot];
            if (environment) {
                // publish once, before native factories can capture the realm global.
                js_global_environment_slot(environment, JS_GLOBAL_ENV_GLOBAL_THIS) = p->slots[p->global_slot];
                js_global_environment_slot(environment, JS_GLOBAL_ENV_WINDOW_EVENT) = make_js_undefined();
                environment->window_event_intercept_enabled = true;
            }
        }
    }
    if (p->library_runtime) {
        p->library->native_callable_values = existing_global;
        p->library->values = p->slots + p->library_slot;
        if (p->library->iterator_base)
            p->library->iterator_base_values = p->slots + p->iterator_base_slot;
        if (p->library->regexp_iterator)
            p->library->regexp_iterator_values = p->slots + p->regexp_iterator_slot;
        if (p->library->iterator_families)
            p->library->iterator_family_values = p->slots + p->iterator_family_slot;
        if (p->errors) p->library->error_values = p->slots + p->error_slot;
        if (p->primitives) p->library->primitive_values = p->slots + p->primitive_slot;
        if (p->buffers) p->library->buffer_values = p->slots + p->buffer_slot;
        if (p->shared_buffer) p->library->shared_buffer_values = p->slots + p->shared_buffer_slot;
        if (p->atomics) p->library->atomics_value = p->slots + p->atomics_slot;
        if (p->weak_collections) p->library->weak_values = p->slots + p->weak_slot;
        if (p->typed_storage) p->library->typed_values = p->slots + p->typed_slot;
        if (p->promises) p->library->promise_values = p->slots + p->promise_slot;
        if (p->async_generators) p->library->async_generator_values = p->slots + p->async_generator_slot;
        if (p->async_functions) p->library->async_function_values = p->slots + p->async_function_slot;
        if (p->generator_function_slot) p->library->generator_function_values = p->slots + p->generator_function_slot;
        p->library->mutable_prototypes = p->property_callbacks;
        p->library->function_prototype_observed = p->function_prototype_observed;
        // final AST/facts exclude every route to protocol mutation; observable programs keep raw descriptor checks.
        bool regexp_unobserved = !p->property_callbacks && !p->global_reflection && !p->symbols &&
            !p->dynamic_source && !p->primitive_method_overrides;
        for (AstNodeId i = 0; regexp_unobserved && i < p->frontend->ast_index.count; i++) {
            AstNode* node = p->frontend->ast_index.nodes[i];
            if (node->node_type != AST_NODE_MEMBER_EXPR && node->node_type != AST_NODE_INDEX_EXPR) continue;
            AstFieldNode* member = (AstFieldNode*)node;
            String* name = member_spelling(member);
            if (!name) regexp_unobserved = kind(p, member->property) == K_NUMBER;
            else if (named(name, "prototype") || named(name, "constructor") || named(name, "exec") ||
                    js_builtin_catalog_find(JS_BUILTIN_OWNER_REGEXP_ACCESSOR_INTERNAL, name->chars, name->len))
                regexp_unobserved = false;
            for (int legacy = 0; name && regexp_unobserved && js_regexp_legacy_name(legacy); legacy++)
                if (named(name, js_regexp_legacy_name(legacy))) regexp_unobserved = false;
        }
        p->library->regexp_protocol_unobserved = regexp_unobserved;
        static const ContextCapsuleOps ops = {"js-mvp-library", CONTEXT_CAPSULE_LIFETIME_CONTEXT,
            sizeof(MvpLmdLibraryState*), NULL, NULL, [](void* opaque) {
                // the optional fast selector must not outlive its context capsule.
                if (mvp_lmd_library_slot == opaque) {
                    mvp_lmd_library_slot = NULL; mvp_lmd_foreign_library = NULL;
                    mvp_lmd_library_context = NULL;
                }
                mem_free(opaque);
            }};
        auto** state = (MvpLmdLibraryState**)context_capsule_extension(e->owner_context, MVP_LMD_CAPSULE_OWNER, 0);
        if (!state) {
            state = (MvpLmdLibraryState**)mem_alloc(sizeof(*state), MEM_CAT_EVAL);
            if (!state) return ItemError;
            if (!context_capsule_extension_install(e->owner_context, MVP_LMD_CAPSULE_OWNER, 0, state, &ops)) {
                mem_free(state); return ItemError;
            }
        }
        *state = p->library;
        mvp_lmd_library_slot = state;
        mvp_lmd_library_context = e->owner_context;
        Item result = mvp_lmd_library_initialize(p, p->library);
        if (item_is_error(result)) return result;
        Item& math = p->library->values[LMD_LIBRARY_MATH];
        math = existing_global ? js_get_math_object_value() : mvp_lmd_object_new(p->roots[0], 0);
        if (item_is_error(math)) result = math;
        for (int i = 0; !existing_global && !item_is_error(result) && i < JS_INTRINSIC_BINDING_COUNT; i++) {
            const JsBuiltinMethodSpec* operation = js_builtin_method_at(i);
            if (operation->owner != JS_BUILTIN_OWNER_MATH_METHOD) continue;
            result = mvp_lmd_named_set(math, operation->name, mvp_lmd_native_function(p,
                (void*)operation, math_call, operation->param_count, operation->display_name ? operation->display_name : operation->name),
                JSPD_NON_ENUMERABLE);
        }
        for (int i = 0; !existing_global && !item_is_error(result) && i < js_math_constant_count; i++) {
            const JsMathConstantSpec& constant = js_math_constants[i];
            result = mvp_lmd_named_set(math, constant.name, push_d(constant.value),
                JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE | JSPD_NON_CONFIGURABLE);
        }
        if (!existing_global && !item_is_error(result)) {
            result = mvp_lmd_named_set(math,
                Item{.item = s2it(well_known_name_ref(JS_SYMBOL_TO_STRING_TAG))},
                Item{.item = s2it(heap_create_name("Math", 4))}, JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE);
        }
        if (item_is_error(result)) return result;
    }
    if (p->promises) {
        Item initialized = mvp_lmd_promise_initialize(p, p->library);
        if (item_is_error(initialized)) return initialized;
    }
    if (p->async_generators || p->async_functions || p->generator_function_slot) {
        Item initialized = mvp_lmd_suspended_functions_initialize(p, p->library);
        if (item_is_error(initialized)) return initialized;
    }
    if (p->dynamic_source) {
        Item initialized = mvp_lmd_dynamic_initialize(p);
        if (item_is_error(initialized)) return initialized;
        p->slots[p->eval_slot] = initialized;
    }
    if (existing_global && p->library_runtime) p->library->default_realm = p->library;
    if (p->library_runtime) p->library->intrinsics_ready = true;
    if (p->host_active || p->timers) {
        p->host.values = p->host_active ? p->slots + p->host_slot : NULL;
        p->host.timers = p->timers ? p->slots + p->timer_slot : NULL;
        p->host.global = p->global_receiver ? p->slots + p->global_slot : NULL;
        for (AstNodeId i = 0; i < p->frontend->ast_index.count; i++)
            if (!p->direct_stdio && p->intrinsic_uses[i] && p->intrinsic_uses[i]->intrinsic == I_PROCESS)
                p->host.process_requested = true;
        p->host.process_stdio_only = p->host.process_requested && process_stdio_only(p, existing_global, host);
        Item result = mvp_lmd_host_initialize(p, &p->host, p->roots[0], host);
        if (item_is_error(result)) return result;
    }
    if (p->global_receiver) {
        if (p->global_reflection && !existing_global && !item_is_error(p->slots[p->global_slot])) {
            Item initialized = mvp_lmd_global_initialize(p);
            if (item_is_error(initialized)) return initialized;
        }
        if (item_is_error(p->slots[p->global_slot])) {
            return p->slots[p->global_slot];
        }
        // a public receiver-only unit need not materialize any builtin library.
        if (p->test262_host && p->library && p->library->buffer_values) {
            Item result = mvp_lmd_named_set(p->library->buffer_values[6], "global", p->slots[p->global_slot]);
            if (item_is_error(result)) return result;
        }
    }
    return ItemNull;
}
Item mvp_lmd_create_realm(Context*, MvpLmdProgram* owner, Item*, uint64_t, Item, Item, Item) {
    MvpLmdExecution* execution = owner->execution_owner;
    if (!execution) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    if (execution->next_realm_key == UINT32_MAX) return ItemError;
    auto* program = mvp_lmd_new_unit(execution, NULL);
    if (!program) return ItemError;
    program->realm_key = ++execution->next_realm_key;
    program->module_realm = true; // complete intrinsic surface, with its own global and lexical owner
    program->host_enabled = owner->host_enabled;
    program->test262_host = owner->test262_host;
    // realm creation may run inside an async/generator activation's small native stack.
    bool faulted = false;
    void* compiled = activation_call_on_base([](void* opaque) -> void* {
        auto* unit = (MvpLmdProgram*)opaque;
        return mvp_lmd_compile_unit(unit, "", 0, 1) && mvp_lmd_compile_unit(unit, NULL, 0, 2)
            ? unit : NULL;
    }, program, &faulted);
    if (!compiled || faulted)
        return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    program->library->default_realm = execution->program.library;
    MvpLmdRealmScope realm(program);
    Item result = mvp_lmd_instantiate_unit(execution, program, execution->host);
    return realm.complete(item_is_error(result) ? result : program->library->buffer_values[6]);
}
static Item mvp_lmd_enter_unit(MvpLmdProgram* p, bool instantiate = false) {
    LmdFunction* main = (LmdFunction*)p->functions->data[0];
    Item undefined = {.item = ITEM_JS_UNDEFINED};
    typedef Item (*Entry)(Context*, MvpLmdProgram*, Item*, uint64_t, Item);
    typedef Item (*ReceiverEntry)(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item);
    auto enter = [&]() {
        return p->receiver_abi ? ((ReceiverEntry)main->address)(context, p, NULL, instantiate ? 1 : 0,
            undefined, undefined, undefined) : ((Entry)main->address)(context, p, NULL, instantiate ? 1 : 0, undefined);
    };
    if (!p->source_module && !p->dynamic_source) return enter();
    RuntimeCurrentFileScope file(context, p->frontend->reference);
    if (!p->source_module) return enter();
    RuntimeModuleStateScope module;
    if (!module.activate(p->module_state_id)) return ItemError;
    return enter();
}
static Item mvp_module_error(void* owner, JsSourceModuleError kind, const char*) {
    auto* root = (MvpLmdProgram*)owner;
    int failure = kind == JS_SOURCE_MODULE_SYNTAX ? LMD_MVP_SYNTAX :
        kind == JS_SOURCE_MODULE_REFERENCE ? LMD_MVP_REFERENCE :
        kind == JS_SOURCE_MODULE_TYPE ? LMD_MVP_TYPE :
        kind == JS_SOURCE_MODULE_RANGE ? LMD_MVP_RANGE : LMD_MVP_CAPABILITY;
    if (root->failure_phase == MVP_LMD_INTERNAL_FAILURE)
        root->failure_phase = MVP_LMD_RESOLUTION_FAILURE;
    return mvp_lmd_fail(failure, 0);
}
static Item mvp_module_compile_failure(MvpLmdRealm* realm, MvpLmdProgram* program) {
    MvpLmdProgram* root = realm->program;
    // a handled dynamic-import rejection must not poison the enclosing execution diagnostic.
    if (root->failure_phase != MVP_LMD_RUNTIME_FAILURE) {
        root->failure_phase = program->failure_phase;
        str_copy(root->diagnostic, sizeof(root->diagnostic), program->diagnostic, strlen(program->diagnostic));
    }
    return mvp_lmd_fail(program->failure_phase == MVP_LMD_PARSE_FAILURE ? LMD_MVP_SYNTAX : LMD_MVP_CAPABILITY, 0);
}
static Item mvp_module_prepare(void* owner, JsSourceModuleRecord* record, const char* source, size_t length) {
    auto* realm = mvp_lmd_program_realm((MvpLmdProgram*)owner)->module_controller;
    auto* program = mvp_lmd_new_unit(realm->execution, realm->program);
    if (!program) return ItemError;
    program->source_module = record;
    record->unit = program;
    if (!mvp_lmd_compile_unit(program, source, length, 0)) return mvp_module_compile_failure(realm, program);
    record->script = program->frontend;
    return ItemNull;
}
static Item mvp_module_plan(void* owner, JsSourceModuleRecord* record) {
    auto* realm = mvp_lmd_program_realm((MvpLmdProgram*)owner)->module_controller;
    auto* program = (MvpLmdProgram*)record->unit;
    if (!mvp_lmd_compile_unit(program, NULL, 0, 1)) return mvp_module_compile_failure(realm, program);
    if (!lambda_module_state_reserve(program->module_slot_count, &program->module_state_id)) return ItemError;
    RuntimeModuleStateScope scope;
    if (!scope.activate(program->module_state_id)) return ItemError;
    program->module_state = context->active_module_state;
    for (int index = 0; index < program->bindings->length; index++) {
        auto* binding = (LmdBinding*)program->bindings->data[index];
        if (binding->module_cell) lambda_module_var_store(program->module_state, binding->slot,
            Item{.item = binding->entry->is_lexical ? ITEM_JS_TDZ : ITEM_JS_UNDEFINED});
    }
    return ItemNull;
}
static bool mvp_module_local_binding(void*, JsSourceModuleRecord* record,
        NameEntry* entry, JsResolvedModuleBinding* output) {
    auto* program = (MvpLmdProgram*)record->unit;
    LmdBinding* local = binding(program, entry);
    if (!local || !local->module_cell || !program->module_state) return false;
    *output = {record, entry, program->module_state, local->slot, false};
    return true;
}
static Item mvp_module_link(void* owner, JsSourceModuleRecord* record) {
    auto* realm = mvp_lmd_program_realm((MvpLmdProgram*)owner)->module_controller;
    auto* program = (MvpLmdProgram*)record->unit;
    for (int index = 0; index < record->imports->length; index++) {
        auto* imported = (JsSourceModuleImport*)record->imports->data[index];
        LmdBinding* local = binding(program, imported->local);
        if (!local) return mvp_module_error(owner, JS_SOURCE_MODULE_SYNTAX, "Imported binding has no local cell");
        local->module_import = &imported->resolved;
    }
    const char* source = program->frontend->source;
    size_t length = program->frontend->source_length;
    const char* filename = program->frontend->reference;
    InputCacheScope* cache_scope = NULL;
    const JsModuleMirArtifact* artifact = js_module_mir_cache_lookup(source, length, filename, &cache_scope);
    InputScriptBuildScope build = {};
    if (!artifact && js_module_mir_cache_begin_build(source, length, filename, &build) == INPUT_SCRIPT_BUILD_READY)
        artifact = js_module_mir_cache_lookup(source, length, filename, &cache_scope);
    bool eligible = input_script_cache_mir_enabled(input_manager_global_script_cache());
    if (eligible) {
        program->relocations = arraylist_new(32);
        if (!program->relocations) {
            js_module_mir_cache_complete_build(&build, false, false);
            if (cache_scope) input_script_cache_close_scope(cache_scope);
            return ItemError;
        }
        program->relocatable = true;
        program->cached_code = artifact ? artifact->image.mvp_code : NULL;
    }
    bool compiled = mvp_lmd_compile_unit(program, NULL, 0, 2);
    bool reused = compiled && program->code && program->code == program->cached_code;
    program->cached_code = NULL;
    if (cache_scope) input_script_cache_close_scope(cache_scope);
    cache_scope = NULL;
    // linking the live graph is still mandatory on a code hit. Record exact
    // source generations through the common dependency invalidation service.
    bool dependencies = true;
    for (int i = 0; compiled && i < record->requests->length; i++) {
        auto* request = (JsSourceModuleRequest*)record->requests->data[i];
        JsScript* dependency = request->module->script;
        dependencies &= dependency && js_module_mir_cache_record_dependency(source, length, filename,
            dependency->source, dependency->source_length, request->module->descriptor->path);
    }
    bool published = false;
    if (compiled && program->code && !artifact && dependencies && build.state == INPUT_SCRIPT_BUILD_OWNER) {
        auto* retained = (JsModuleMirArtifact*)mem_calloc(1, sizeof(JsModuleMirArtifact), MEM_CAT_EVAL);
        if (retained) {
            retained->image.mvp_code = program->code;
            mvp_lmd_code_retain(program->code);
            retained->image.mir_ctx = program->code->mir;
            retained->image.entry_func = program->code->addresses->data[0];
            retained->image.owns_compiled_state = true;
            published = js_module_mir_cache_adopt_build(&build, retained, &cache_scope) != NULL;
            if (!published) js_module_mir_artifact_destroy(retained);
        }
    }
    js_module_mir_cache_complete_build(&build, published, false);
    if (cache_scope) input_script_cache_close_scope(cache_scope);
    if (reused) input_script_cache_mark_module_hit(input_manager_global_script_cache());
    if (!compiled) return mvp_module_compile_failure(realm, program);
    return mvp_lmd_instantiate_unit(realm->execution, program, realm->host);
}
static Item mvp_module_instantiate(void*, JsSourceModuleRecord* record) {
    return mvp_lmd_enter_unit((MvpLmdProgram*)record->unit, true);
}
static Item mvp_lmd_module_async_body(Activation* activation, Item) {
    JsOwnedCallActivation call_facts;
    auto* state = (JsAsyncContextStateRecord*)activation_user(activation);
    auto* program = (MvpLmdProgram*)state->state_fn;
    RuntimeCurrentFileScope file(context, state->module_path);
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> ns(roots, js_module_namespace_create(program->source_module));
    if (item_is_error(ns.get())) return ns.get();
    JsModuleNamespaceScope module(ns.get(), true);
    Item result = mvp_lmd_enter_unit(program);
    return item_is_error(result) ? result : Item{.item = ITEM_JS_UNDEFINED};
}
static Item mvp_module_evaluate(void*, JsSourceModuleRecord* record, bool async_frame) {
    auto* program = (MvpLmdProgram*)record->unit;
    MvpLmdRealmScope scope(program);
    return scope.complete(async_frame ? js_async_context_create_module_body(mvp_lmd_module_async_body,
        program, program->frontend, program->module_state_id, record->descriptor->specifier_item, record->descriptor)
        : mvp_lmd_enter_unit(program));
}
static Item mvp_module_import_meta(void* owner, JsSourceModuleRecord* record) {
    auto* realm = mvp_lmd_program_realm((MvpLmdProgram*)owner)->module_controller;
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, mvp_lmd_object_create(realm->program, ItemNull)), url(roots, ItemNull);
    if (item_is_error(object.get())) return object.get();
    const char* location = jm_document_script_base_url(record->runtime, record->descriptor->path);
    String* copied = heap_strcpy(location ? location : "", location ? strlen(location) : 0);
    if (!copied) return ItemError;
    url.set(Item{.item = s2it(copied)});
    Item written = mvp_lmd_named_set(object.get(), "url", url.get());
    return item_is_error(written) ? written : object.get();
}
Runtime* mvp_lmd_prepare_module_realm(void* owner) {
    auto* program = mvp_lmd_program_realm((MvpLmdProgram*)owner);
    auto* execution = program->execution_owner;
    if (program->module_controller) return program->module_controller->active_runtime;
    auto* realm = (MvpLmdRealm*)mem_calloc(1, sizeof(MvpLmdRealm), MEM_CAT_EVAL);
    if (!realm) return NULL;
    if (!execution->units) execution->units = arraylist_new(8);
    if (!execution->units) { mem_free(realm); return NULL; }
    realm->execution = execution;
    realm->program = program;
    realm->host = execution->host;
    realm->runtime.eval_context = execution->owner_context;
    realm->runtime.use_mir_direct = true;
    realm->runtime.optimize_level = 2;
    Runtime* parent = execution->host_runtime ? execution->host_runtime : execution->owned_runtime;
    if (parent) {
        realm->runtime.resource_policy = parent->resource_policy;
        realm->runtime.js_document_base_url = parent->js_document_base_url;
        realm->runtime.js_import_maps = parent->js_import_maps;
        realm->runtime.import_base_dir = parent->import_base_dir;
    }
    // child realms share the heap and scheduler, but own independent module registries.
    realm->active_runtime = parent && !program->realm_key ? parent : &realm->runtime;
    static const ContextCapsuleOps capsule = {"js-mvp-module-realm", CONTEXT_CAPSULE_LIFETIME_REALM,
        sizeof(MvpLmdRealm), NULL, NULL, [](void* opaque) {
            auto* realm = (MvpLmdRealm*)opaque;
            if (realm->active_runtime == &realm->runtime)
                module_registry_cleanup_for_runtime(&realm->runtime);
            // native agent AST bodies can outlive their queues until shared-heap
            // finalization; the existing program script list owns that tail.
            if (realm->runtime.scripts) {
                if (!realm->program->dynamic_scripts)
                    realm->program->dynamic_scripts = arraylist_new(8);
                for (int i = 0; i < realm->runtime.scripts->length; i++)
                    arraylist_append(realm->program->dynamic_scripts, realm->runtime.scripts->data[i]);
                realm->runtime.scripts->length = 0;
                runtime_free_all_scripts(&realm->runtime);
            }
            realm->program->module_controller = NULL;
            if (realm->execution->realm == realm) realm->execution->realm = NULL;
            mem_free(realm);
        }};
    if (!context_capsule_extension_install(execution->owner_context, MVP_LMD_MODULE_OWNER,
            program->realm_key, realm, &capsule)) { mem_free(realm); return NULL; }
    program->module_controller = realm;
    if (!program->realm_key) {
        execution->realm = realm;
        if (!parent) execution->owner_context->runtime = &realm->runtime;
    }
    return realm->active_runtime;
}
static const JsSourceModuleOps mvp_lmd_source_module_ops = {
    mvp_module_prepare, mvp_module_plan, mvp_module_local_binding, mvp_module_link,
    mvp_module_instantiate, mvp_module_evaluate, mvp_lmd_same_value, mvp_module_import_meta, mvp_module_error,
    mvp_lmd_prepare_module_realm,
    [](void*, Item value) { return mvp_lmd_primitive_to_string(value); },
    [](void* owner, Item error) { return mvp_lmd_caught_value(((MvpLmdProgram*)owner)->library, error); },
    [](void* owner, Item value) { return mvp_lmd_dynamic_import_value((MvpLmdProgram*)owner, value, true); }
};
Item mvp_lmd_dynamic_import(MvpLmdProgram* program, Item specifier, const char* reference) {
    program = mvp_lmd_program_realm(program);
    MvpLmdRealmScope realm(program);
    return realm.complete(js_source_dynamic_import(specifier, reference, &mvp_lmd_source_module_ops, program));
}
static bool global_alias_stores_are_closed(MvpLmdProgram* p) {
    if (program_has_existing_global(p)) return false;
    const AstIndex* tree = &p->frontend->ast_index;
    bool prefix = true, early_stores = true;
    for (AstNode* statement = ((AstScript*)p->frontend->ast_root)->body;
            statement; statement = statement->next) {
        AstNode* value = statement->node_type == AST_NODE_EXPR_STMT
            ? ((AstExprStmtNode*)statement)->expression : NULL;
        if (global_self_alias_store(p, value)) early_stores &= prefix;
        else if (statement->node_type != AST_NODE_FUNC &&
                (!value || value->node_type != AST_NODE_LITERAL)) prefix = false;
    }
    for (AstNodeId i = 0; i < tree->count; i++) {
        AstNode* node = tree->nodes[i];
        AstNode* parent = ast_index_parent(tree, node);
        if (node->node_type == AST_NODE_IDENT && !js_ast_fact_walk_skips_child(parent, node)) {
            auto* identifier = (AstIdentNode*)node;
            if (named(identifier->name, "this")) return false;
            // unknown host names can expose another reference to the global.
            if (!identifier_binding(p, identifier)) early_stores = false;
        }
        String* alias = global_self_alias_store(p, node);
        if (!alias) continue;
        for (AstNodeId j = 0; j < tree->count; j++) {
            AstNode* use = tree->nodes[j];
            AstNode* consumer = ast_index_parent(tree, use);
            String* name = use->node_type == AST_NODE_IDENT ? ((AstIdentNode*)use)->name :
                use->node_type == AST_NODE_MEMBER_EXPR || use->node_type == AST_NODE_INDEX_EXPR
                    ? member_spelling((AstFieldNode*)use) : NULL;
            if (!name || name->len != alias->len || memcmp(name->chars, alias->chars, name->len)) continue;
            if (use->node_type == AST_NODE_IDENT && consumer &&
                    js_ast_fact_walk_skips_child(consumer, use)) continue;
            if (!consumer || consumer->node_type != AST_NODE_ASSIGN ||
                    ((AstAssignNode*)consumer)->left != use || !global_self_alias_store(p, consumer)) return false;
        }
    }
    // before the first effect a fresh closed realm has no guest-installed
    // setter. Unread aliases cannot escape through later property callbacks;
    // direct/global-key escapes were already admitted by the source walk.
    if (early_stores && !p->dynamic_source && !p->execution_owner->shared_realm) return true;
    // reuse the existing whole-unit proof: computed keys cannot reach native
    // names or realm bindings, and reflection cannot mutate callable identities.
    plan_fixed_global_bindings(p);
    return p->global_mutations_closed;
}
static bool has_function_receiver_origin(MvpLmdProgram* p, AstNode* node, int depth = 0) {
    if (!node || depth == 16) return false;
    unsigned owner = kind(p, node);
    if (!(owner & K_FUNCTION)) return false;
    if (!(owner & ~(K_FUNCTION | K_NULL | K_UNDEFINED))) return true;
    if (node->node_type == AST_NODE_IDENT) {
        LmdBinding* binding = identifier_binding(p, (AstIdentNode*)node);
        return binding && has_function_receiver_origin(p, binding->initializer, depth + 1);
    }
    if (node->node_type == AST_NODE_CALL_EXPR) {
        auto* call = (AstCallNode*)node;
        if (call->callee->node_type != AST_NODE_MEMBER_EXPR &&
                call->callee->node_type != AST_NODE_INDEX_EXPR) return false;
        auto* method = (AstFieldNode*)call->callee;
        String* name = member_spelling(method);
        if (named(name, "bind")) return has_function_receiver_origin(p, method->object, depth + 1);
        LmdBinding* builtin = method->object->node_type == AST_NODE_IDENT
            ? identifier_binding(p, (AstIdentNode*)method->object) : NULL;
        int intrinsic = builtin ? builtin->intrinsic ? builtin->intrinsic : builtin->reflected_intrinsic : 0;
        // reflection and bind can return erased callable identities. An
        // ordinary call with an erased result supplies no such origin proof.
        if ((intrinsic == I_OBJECT || intrinsic == I_REFLECT) && named(name, "getPrototypeOf"))
            return has_function_receiver_origin(p, call->arguments, depth + 1);
        return intrinsic == I_REFLECT && named(name, "get");
    }
    if (node->node_type != AST_NODE_MEMBER_EXPR && node->node_type != AST_NODE_INDEX_EXPR) return false;
    String* name = member_spelling((AstFieldNode*)node);
    for (TypeId type : {LMD_TYPE_MAP, LMD_TYPE_ARRAY, LMD_TYPE_STRING})
        if (name && mvp_lmd_builtin_method(name, type)) return true;
    AstNode* object = ((AstFieldNode*)node)->object;
    for (int remaining = 16 - depth; remaining && object && object->node_type == AST_NODE_IDENT; remaining--) {
        LmdBinding* binding = identifier_binding(p, (AstIdentNode*)object);
        object = binding ? binding->initializer : NULL;
    }
    if (object && object->node_type == AST_NODE_ARRAY)
        for (AstNode* item = ((AstArrayNode*)object)->item; item; item = item->next)
            if (has_function_receiver_origin(p, item, depth + 1)) return true;
    // method values lose their callable kind in generic object projections.
    // Their source definitions still demonstrate a possible Function receiver.
    for (int i = 1; i < p->functions->length; i++) {
        AstNode* definition = (AstNode*)((LmdFunction*)p->functions->data[i])->ast;
        AstNode* parent = ast_index_parent(&p->frontend->ast_index, definition);
        String* key = NULL;
        if (definition->node_type == AST_NODE_METHOD) {
            auto* method = (AstMethodNode*)definition;
            key = property_spelling(method->key, method->computed);
        } else if (parent && parent->node_type == AST_NODE_PROPERTY &&
                ((AstPropertyNode*)parent)->value == definition) {
            auto* property = (AstPropertyNode*)parent;
            key = property_spelling(property->key, property->computed);
        } else if (parent && parent->node_type == AST_NODE_ASSIGN &&
                ((AstAssignNode*)parent)->right == definition) {
            AstNode* target = ((AstAssignNode*)parent)->left;
            if (target->node_type != AST_NODE_MEMBER_EXPR && target->node_type != AST_NODE_INDEX_EXPR) continue;
            key = member_spelling((AstFieldNode*)target);
        } else continue;
        if (!name || !key || (name->len == key->len && !memcmp(name->chars, key->chars, name->len))) return true;
    }
    return false;
}
static bool observes_function_constructor(MvpLmdProgram* p) {
    if (p->dynamic_source || !p->suspended_constructor_observed) return false;
    for (AstNodeId i = 0; i < p->frontend->ast_index.count; i++) {
        AstNode* node = p->frontend->ast_index.nodes[i];
        AstNode* parent = ast_index_parent(&p->frontend->ast_index, node);
        AstNode* receiver = NULL;
        AstNode* key = NULL;
        String* name = NULL;
        bool pattern = node->node_type == AST_NODE_PROPERTY && parent &&
            parent->node_type == AST_NODE_MAP_PATTERN;
        if (pattern) {
            auto* property = (AstPropertyNode*)node;
            key = property->key;
            name = property_spelling(key, property->computed);
            AstNode* source = ast_index_parent(&p->frontend->ast_index, parent);
            if (source && source->node_type == AST_NODE_VARIABLE_DECLARATOR)
                receiver = ((AstDeclaratorNode*)source)->init;
            else if (source && (source->node_type == AST_NODE_ASSIGN || source->node_type == AST_NODE_ASSIGN_PATTERN))
                receiver = ((AstAssignNode*)source)->right;
        } else if (node->node_type == AST_NODE_MEMBER_EXPR || node->node_type == AST_NODE_INDEX_EXPR) {
            auto* member = (AstFieldNode*)node;
            receiver = member->object;
            key = member->property;
            name = member_spelling(member);
        } else continue;
        if (parent && parent->node_type == AST_NODE_ASSIGN &&
                ((AstAssignNode*)parent)->left == node && ((AstAssignNode*)parent)->op == OPERATOR_ASSIGN) continue;
        unsigned owner = receiver ? kind(p, receiver) : K_ANY;
        if (!(owner & K_FUNCTION)) continue;
        if (name) {
            if (named(name, "constructor")) {
                // reading constructor metadata does not invoke or expose the
                // constructor; mutations and escaped uses still admit source.
                if (parent && (parent->node_type == AST_NODE_MEMBER_EXPR ||
                        parent->node_type == AST_NODE_INDEX_EXPR) &&
                        ((AstFieldNode*)parent)->object == node &&
                        named(member_spelling((AstFieldNode*)parent), "name")) {
                    AstNode* consumer = ast_index_parent(&p->frontend->ast_index, parent);
                    bool write = consumer && ((consumer->node_type == AST_NODE_ASSIGN &&
                        ((AstAssignNode*)consumer)->left == parent) ||
                        consumer->node_type == AST_NODE_UNARY);
                    if (!write) continue;
                }
                return true;
            }
            continue;
        }
        // numeric indices cannot name the constructor. Unknown pattern sources
        // retain full semantics; ordinary dictionary projections keep their plans.
        if ((kind(p, key) & (K_STRING | K_OBJECT | K_ARRAY | K_MAP | K_FUNCTION | K_TYPED_ARRAY)) &&
                (pattern || has_function_receiver_origin(p, receiver)))
            return true;
    }
    return false;
}
static void mvp_lmd_release_unit(MvpLmdProgram* p);
static MvpLmdExecution* mvp_lmd_execute_owned(const char* source, size_t length, double* execution_ms,
        const MvpLmdHost* host, const MvpLmdOptions* options, Runtime* runtime) {
    if (execution_ms) *execution_ms = 0;
    MvpLmdExecution* e = runtime ? js_runtime_state.formal_mir_owner : NULL;
    bool existing = e != NULL;
    if (!e) e = (MvpLmdExecution*)mem_calloc(1, sizeof(MvpLmdExecution), MEM_CAT_EVAL);
    if (!e) return NULL;
    e->owner_context = runtime ? runtime_get_eval_context(runtime) : NULL;
    e->host_runtime = runtime;
    if (!existing) e->shared_realm = runtime && !(options && options->terminal_script);
    e->result = Item{.item = ITEM_ERROR};
    MvpLmdProgram* p = existing ? mvp_lmd_new_unit(e, &e->program) : &e->program;
    if (!p) return e;
    e->last_program = p;
    if (!existing) {
        if (host) { e->retained_host = *host; host = &e->retained_host; }
        e->host = host;
    } else host = e->host;
    if (existing && p->library) p->library->abort_kind = 0;
    bool native_harness = options && options->native_harness;
    bool module_source = options && options->module;
    for (;;) {
        p->execution_owner = e;
        p->host_enabled = host != NULL;
        p->typescript = options && options->typescript;
        // the public JS host exposes $262 like the retained interpreter; async
        // harness completion remains separately controlled by host->test262.
        p->test262_host = runtime || (host && host->test262);
        // public Runtime results may escape to host callbacks; retain their
        // program and receiver entry ABI even when the source has no method calls.
        p->receiver_abi = e->shared_realm;
        // install harness globals before Script declarations can reserve their names.
        p->script_bindings_pending = native_harness;
        // host entries are open Scripts: later calls share lexicals and may mutate
        // intrinsics. Standalone execution retains closed-unit analysis.
        p->module_realm = e->shared_realm || module_source ||
            (options && (options->preamble || native_harness));
        if (!source || !mvp_lmd_compile_unit(p, module_source ? "" : source,
                module_source ? 0 : length, 1, options ? options->filename : NULL)) return e;
        if (existing || e->shared_realm) break;
        bool open_alias = p->global_alias_store && !global_alias_stores_are_closed(p);
        if (!open_alias && !observes_function_constructor(p)) break;
        // receiver kinds are solved only after admission. Replan the uninstantiated
        // unit with full realm bindings before lowering or any guest side effect.
        mvp_lmd_release_unit(p);
        memset(p, 0, sizeof(*p));
        e->shared_realm = true;
    }
    if (options && options->cached_code) {
        p->relocations = arraylist_new(32);
        if (!p->relocations) return e;
        p->relocatable = true;
        p->cached_code = options->cached_code;
    }
    if (!mvp_lmd_compile_unit(p, NULL, 0, 2)) return e;
    p->cached_code = NULL;
    p->failure_phase = MVP_LMD_INTERNAL_FAILURE;
    if (context && context != e->owner_context) {
        diagnostic(p, NULL, "execution belongs to another evaluator thread"); return e;
    }
    lambda_stack_init();
    if (!runtime) {
        e->owned_runtime = (Runtime*)mem_calloc(1, sizeof(Runtime), MEM_CAT_EVAL);
        if (!e->owned_runtime) return e;
        runtime_init(e->owned_runtime);
        bool reusing_context = false;
        if (!js_prepare_eval_context(e->owned_runtime, true,
                &e->owner_context, &reusing_context, false)) return e;
    }
    // use the shared recoverable budget and unwind headroom for native entry guards.
    e->owner_context->stack_limit = lambda_stack_recoverable_limit();
    if (!eval_context_init(e->owner_context)) return e;
    if (!e->owner_context->heap) heap_init();
    if (!e->owner_context->heap) return e;
    e->owner_context->pool = e->owner_context->heap->pool;
    if ((runtime || p->host_active || p->promises || p->dynamic_source || native_harness) && (!js_runtime_state_init(e->owner_context) ||
            !js_runtime_state_ensure_input(e->owner_context))) return e;
    if ((runtime || p->global_reflection) && js_runtime_state_for(e->owner_context))
        js_runtime_state.formal_mir_owner = e;
    Item initialized = mvp_lmd_instantiate_unit(e, p, host);
    if (item_is_error(initialized)) { e->result = runtime_publish_result(e->owner_context, initialized); return e; }
    if (runtime) js_runtime_state.formal_mir_owner = e;
    if (native_harness) {
        // native closures capture the canonical global, including async completion state.
        if (!mvp_lmd_prepare_module_realm(p)) return e;
        initialized = js_test262_native_harness_install();
        if (item_is_error(initialized)) { e->result = runtime_publish_result(e->owner_context, initialized); return e; }
    }
    if (e->shared_realm || native_harness) {
        if (!module_source) {
            initialized = mvp_lmd_prepare_script_bindings(p);
            if (item_is_error(initialized)) { e->result = runtime_publish_result(e->owner_context, initialized); return e; }
        }
    }
    if (p->dynamic_source && !module_source) {
        initialized = mvp_lmd_publish_script_lexicals(p);
        if (item_is_error(initialized)) { e->result = runtime_publish_result(e->owner_context, initialized); return e; }
    }
    JsSourceModuleRecord* module = NULL;
    if (p->dynamic_source && !js_module_set_source_ops(&mvp_lmd_source_module_ops, mvp_lmd_program_realm(p))) return e;
    if (options && options->preamble) {
        // the harness is a separate Script: its directives cannot alter the
        // test's strictness, and its declarations share the realm environment.
        RootFrame roots(1);
        if (!roots.valid()) return e;
        String* text = heap_strcpy(options->preamble, options->preamble_length);
        if (!text) return e;
        Rooted<Item> harness(roots, Item{.item = s2it(text)});
        Item argument = harness.get();
        Item status = mvp_lmd_eval_script(context, p, &argument, 1, ItemNull, ItemNull, ItemNull);
        if (item_is_error(status)) { e->result = runtime_publish_result(e->owner_context, status); return e; }
    }
    if (module_source) {
        Runtime* runtime = mvp_lmd_prepare_module_realm(p);
        if (!runtime) return e;
        Item loaded = js_module_load_link(runtime, &mvp_lmd_source_module_ops, mvp_lmd_program_realm(p),
            options->filename ? options->filename : "[module]", source, length, &module);
        if (item_is_error(loaded)) { e->result = runtime_publish_result(e->owner_context, loaded); return e; }
    }
    uint64_t started = execution_ms ? time_now_ns() : 0;
    p->failure_phase = MVP_LMD_RUNTIME_FAILURE;
    Item result = module ? js_module_evaluate(module) : mvp_lmd_enter_unit(p);
    if (module && !item_is_error(result)) result.item = ITEM_JS_UNDEFINED;
    if (execution_ms) *execution_ms = time_elapsed_ms_f(started, time_now_ns());
    result = lambda_item_resolve_pending_slot(result);
    if (p->promises && !item_is_error(result)) {
        // retain the source completion across jobs, including the shared flush budget's next batch.
        RootFrame roots(1);
        if (!roots.valid()) return e;
        uint64_t home = 0;
        Rooted<Item> completion(roots, lambda_item_adopt_scalar_home(result, &home));
        if (p->timers || p->atomics) {
            // waitAsync deadlines use the host loop even without observable timer globals.
            Item status = js_event_loop_drain_result(true);
            if (item_is_error(status)) completion.set(status);
        } else {
            do {
                Item status = js_microtask_flush_result();
                if (item_is_error(status)) { completion.set(status); break; }
            } while (js_microtask_pending_count() > 0);
        }
        result = runtime_publish_result(e->owner_context, completion.get());
    }
    if (module && !item_is_error(result)) {
        if (module->status == JS_SOURCE_MODULE_FAILED) result = module->descriptor->evaluation_error;
        else if (module->status != JS_SOURCE_MODULE_EVALUATED) {
            LambdaError* error = err_create_heap(ERR_USER_ERROR, "module evaluation did not settle", NULL);
            result = error ? err2it(error) : ItemError;
        }
        else if (options->namespace_result) result = js_module_namespace_create(module);
    }
    if (p->library_runtime && p->library->abort_kind)
        result = mvp_lmd_fail(p->library->abort_kind - 1, p->library->abort_site);
    if (host && host->test262 && p->global_receiver && !item_is_error(result)) {
        // a script-defined setTimeout must not bypass host async completion.
        RootFrame completion_roots(1);
        if (!completion_roots.valid()) return e;
        uint64_t completion_home = 0;
        Rooted<Item> completion(completion_roots, lambda_item_adopt_scalar_home(result, &completion_home));
        Item global = p->slots[p->global_slot];
        const char required_key[] = "__lambda_test262_async_required";
        const char done_key[] = "__lambda_test262_async_done";
        // minimal scripts may never initialize the runtime name pool; these
        // host keys can share the program-owned canonical pool like MIR literals.
        String* required_name = name_pool_create_len(p->frontend->name_pool, required_key, sizeof(required_key) - 1);
        if (!required_name) return e;
        Item required = mvp_lmd_property_with_receiver(global, Item{.item = s2it(required_name)}, ItemNull, LMD_PROP_GET, global);
        if (item_is_error(required)) completion.set(required);
        else if (required.item == ITEM_TRUE) {
            String* done_name = name_pool_create_len(p->frontend->name_pool, done_key, sizeof(done_key) - 1);
            if (!done_name) return e;
            Item done = mvp_lmd_property_with_receiver(global, Item{.item = s2it(done_name)}, ItemNull, LMD_PROP_GET, global);
            if (item_is_error(done)) completion.set(done);
            else if (done.item != ITEM_TRUE) {
                LambdaError* error = err_create_heap(ERR_USER_ERROR, "async test did not call $DONE", NULL);
                completion.set(error ? err2it(error) : ItemError);
            }
        }
        e->result = runtime_publish_result(e->owner_context, completion.get());
        return e;
    }
    e->result = runtime_publish_result(e->owner_context, result);
    return e;
}
MvpLmdExecution* mvp_lmd_execute_with_options(const char* source, size_t length, double* execution_ms,
        const MvpLmdHost* host, const MvpLmdOptions* options) {
    return mvp_lmd_execute_owned(source, length, execution_ms, host, options, NULL);
}

struct MvpLmdRetainedScript : Script {
    MvpLmdExecution* execution;
};
static MvpLmdRetainedScript* mvp_lmd_retain_execution(Runtime* runtime) {
    auto* retained = (MvpLmdRetainedScript*)mem_calloc(1, sizeof(MvpLmdRetainedScript), MEM_CAT_EVAL);
    if (!retained) return NULL;
    if (!runtime->scripts) runtime->scripts = arraylist_new(8);
    if (!runtime->scripts) { mem_free(retained); return NULL; }
    retained->destroy_extension = [](Script* script) {
        mvp_lmd_destroy(((MvpLmdRetainedScript*)script)->execution);
    };
    runtime_register_script(runtime, retained);
    return retained;
}

Item mvp_lmd_compile_function_satellite(Runtime* runtime, JsScript* script, uint32_t function_id,
        JsScript* binding_owner) {
    if (!runtime || !script || function_id >= script->ast_index.function_count ||
            !context || context->runtime != runtime) return ItemError;
    auto* definition = (AstFuncNode*)script->ast_index.functions[function_id].node;
    if (!binding_owner) binding_owner = script;
    const char* text = NULL; uint32_t length = 0;
    if (!js_function_source_span(script->source, script->source_length, definition, &text, &length)) return ItemError;
    StrBuf* source = strbuf_new();
    if (!source) return ItemError;
    if (script->strict_mode) strbuf_append_str(source, "'use strict';\n");
    // D8.2.4: clone only the checked body's resolved dependencies. Copying a
    // wide module scope made planning and the unused synthetic main quadratic.
    // separately parsed continuations resolve that same slice by spelling.
    AstIndex* index = &script->ast_index;
    bool* referenced = NULL;
    uint32_t selected_function_count = 0;
    if (index->binding_count) {
        if (!index->function_reference_offsets) { strbuf_free(source); return ItemError; }
        referenced = (bool*)mem_calloc(index->binding_count, sizeof(bool), MEM_CAT_TEMP);
        if (!referenced) { strbuf_free(source); return ItemError; }
    }
    // a contained helper can be the only reader of a live module binding.
    for (AstFunctionId id = 0; id < index->function_count; id++) {
        if (!ast_index_node_descends(index, index->functions[id].first_node,
                index->functions[function_id].first_node)) continue;
        selected_function_count++;
        if (!referenced) continue;
        uint32_t count = 0;
        const AstFunctionReference* references = ast_index_function_references(index, id, &count);
        for (uint32_t i = 0; i < count; i++) referenced[references[i].binding_id] = true;
    }
    for (NameEntry* entry = binding_owner->global_scope->first; entry; entry = entry->next) {
        if (definition->node_type == AST_NODE_FUNC && entry == definition->entry) continue;
        if (referenced) {
            NameEntry* dependency = binding_owner == script ? entry
                : name_scope_lookup_name_bytes(script->global_scope, entry->name);
            if (!dependency) continue;
            AstBindingId binding_id = ast_index_find_binding_entry(index, dependency);
            if (binding_id >= index->binding_count || !referenced[binding_id]) continue;
        }
        strbuf_append_str(source, entry->is_lexical ? "let " : "var ");
        strbuf_append_str_n(source, entry->name->chars, entry->name->len);
        strbuf_append_str(source, ";\n");
    }
    for (AstBindingId id = 0; referenced && id < index->binding_count; id++) {
        NameEntry* entry = index->bindings[id];
        if (!referenced[id] || !js_interp_p2_is_body_capture(script, function_id, entry)) continue;
        // declarations resolve clone identifiers; reads never use their synthetic storage.
        strbuf_append_str(source, "let ");
        strbuf_append_str_n(source, entry->name->chars, entry->name->len);
        strbuf_append_str(source, ";\n");
    }
    mem_free(referenced);
    // anonymous default declarations need expression syntax in an isolated unit.
    bool method = definition->node_type == AST_NODE_METHOD;
    bool as_expression = method || definition->node_type == AST_NODE_FUNC_EXPR ||
        definition->node_type == AST_NODE_ARROW_FUNC || !definition->name;
    if (as_expression) strbuf_append_char(source, '(');
    if (method) {
        // D8.1.3v38 preserves checked defaults verbatim and the original method metadata.
        strbuf_append_str(source, "function(");
        for (AstNode* parameter = definition->params; parameter; parameter = parameter->next) {
            SourceSpan span = parameter->source_span;
            if (!js_ast_parameter_binding_identifier(parameter) || span.end_byte <= span.start_byte ||
                    span.end_byte > script->source_length) { strbuf_free(source); return ItemError; }
            if (parameter != definition->params) strbuf_append_char(source, ',');
            strbuf_append_str_n(source, script->source + span.start_byte, span.end_byte - span.start_byte);
        }
        strbuf_append_char(source, ')');
        AstNode* body = definition->body;
        if (!body || body->node_type != AST_NODE_BLOCK ||
                body->source_span.end_byte <= body->source_span.start_byte ||
                body->source_span.end_byte > script->source_length) { strbuf_free(source); return ItemError; }
        strbuf_append_str_n(source, script->source + body->source_span.start_byte,
            body->source_span.end_byte - body->source_span.start_byte);
    } else strbuf_append_str_n(source, text, length);
    if (as_expression) strbuf_append_char(source, ')');
    strbuf_append_char(source, ';');
    MvpLmdExecution* execution = js_runtime_state.auto_mir_owner;
    bool existing = execution != NULL;
    if (!execution) {
        execution = (MvpLmdExecution*)mem_calloc(1, sizeof(MvpLmdExecution), MEM_CAT_EVAL);
        MvpLmdRetainedScript* retained = execution ? mvp_lmd_retain_execution(runtime) : NULL;
        if (!retained) { mem_free(execution); strbuf_free(source); return ItemError; }
        retained->execution = execution;
        execution->owner_context = context; execution->host_runtime = runtime;
        execution->shared_realm = true;
    }
    // all satellites in the native realm share intrinsic identities and capsule registrations.
    MvpLmdProgram* program = existing ? mvp_lmd_new_unit(execution, &execution->program) : &execution->program;
    if (!program) { strbuf_free(source); return ItemError; }
    program->execution_owner = execution; program->satellite_script = binding_owner;
    program->satellite_source = script; program->satellite_function_id = function_id;
    program->module_realm = true;
    // satellite parameters arrive from other units; local literals cannot exclude types.
    program->bigints = program->symbols = true;
    program->receiver_abi = program->global_receiver = program->global_reflection = program->property_callbacks = true;
    program->host_enabled = true;
    bool faulted = false;
    struct Compile { MvpLmdProgram* program; StrBuf* source; const char* filename; } compile = {program, source, script->reference};
    void* compiled = activation_call_on_base([](void* opaque) -> void* {
        auto* state = (Compile*)opaque;
        return mvp_lmd_compile_unit(state->program, state->source->str, state->source->length, 1, state->filename) &&
            mvp_lmd_compile_unit(state->program, NULL, 0, 2) ? state->program : NULL;
    }, &compile, &faulted);
    strbuf_free(source);
    if (!compiled || faulted || (uint32_t)program->functions->length != selected_function_count + 1) {
        log_error("js-p2-compile: %s functions=%d faulted=%d", program->diagnostic,
            program->functions ? program->functions->length : 0, faulted);
        return ItemError;
    }
    if (program->frontend->pass_timing.enabled) {
        // correlate isolated MIR timings with the original source, including anonymous arrows.
        log_notice("js-p2-source: file=%s name=%.*s start=%u end=%u",
            script->reference ? script->reference : "<script>",
            definition->name ? (int)definition->name->len : 0,
            definition->name ? definition->name->chars : "",
            definition->source_span.start_byte, definition->source_span.end_byte);
    }
    RuntimeModuleStateScope owner(context);
    if (!owner.activate(binding_owner->module_state_id)) {
        log_error("js-p2-compile: caller module state %u unavailable", binding_owner->module_state_id);
        return ItemError;
    }
    program->module_state = context->active_module_state; program->module_state_id = binding_owner->module_state_id;
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> result(roots, js_get_global_this());
    if (item_is_error(result.get())) return result.get();
    MvpLmdLibraryState* previous = mvp_lmd_program_library();
    MvpLmdHost host = {0, NULL, stdout};
    Item initialized = mvp_lmd_instantiate_unit(execution, program, &host);
    if (mvp_lmd_library_slot) *mvp_lmd_library_slot = previous;
    mvp_lmd_foreign_library = previous && previous->default_realm ? previous : NULL;
    if (item_is_error(initialized)) {
        LambdaError* error = it2err(initialized);
        log_error("js-p2-instantiate: %s", error && error->message ? error->message : "unknown failure");
        return initialized;
    }
    if (!existing) js_runtime_state.auto_mir_owner = execution;
    MvpLmdRealmScope realm(program);
    result.set(mvp_lmd_function_new(1, program));
    if (item_is_error(result.get())) log_error("js-p2-compile: callable allocation failed");
    return realm.complete(result.get());
}

Item mvp_lmd_compile_loop_satellite(Runtime* runtime, JsScript* script, const char* source,
        size_t length, const char* function_name) {
    // retain the source grammar goal; exported module source is not a Script.
    JsScript* parsed = script->is_es_module
        ? js_interp_prepare_es_module_script(runtime, source, length, "<loop-handoff>")
        : js_interp_prepare_script(runtime, source, length, "<loop-handoff>", script->strict_mode);
    if (!parsed) return ItemError;
    for (AstFunctionId id = 0; id < parsed->ast_index.function_count; id++) {
        auto* definition = (AstFuncNode*)parsed->ast_index.functions[id].node;
        if (!named(definition->name, function_name)) continue;
        return mvp_lmd_compile_function_satellite(runtime, parsed, id, script);
    }
    return ItemError;
}

Item mvp_lmd_call_function_satellite(Item compiled, Item source, Item receiver,
        Item* arguments, int count, uint64_t* result_home) {
    if (get_type_id(compiled) != LMD_TYPE_FUNC || !function_has_abi(compiled.function, FN_ENTRY_ABI_MVP_LMD)) return ItemError;
    auto* callable = (MvpLmdCallable*)compiled.function;
    if (callable->runtime_context != context) return ItemError;
    MvpLmdRealmScope realm(callable->program);
    // retain the original callable as `self`, including named-expression recursion and identity.
    Item result = ((MvpLmdNativeEntry)callable->ptr)(context, callable->program, arguments, count,
        source, receiver, make_js_undefined());
    result = lambda_item_resolve_pending_slot(result);
    result = realm.complete(result);
    return result_home ? lambda_item_adopt_scalar_home(result, result_home) : result;
}
Item mvp_lmd_execute_in_runtime(Runtime* runtime, const char* source, size_t length,
        const char* filename, uint64_t* result_home, bool module, const MvpLmdCode* cached_code,
        bool typescript, bool native_harness) {
    if (!runtime) return ItemError;
    EvalContext* owner = runtime_get_eval_context(runtime);
    if (!owner || (context && context != owner)) return ItemError;
    bool reusing_context = false;
    // publish the same heap/name-pool tuple used by Runtime cleanup and reentry.
    if (!js_prepare_eval_context(runtime, true, &owner, &reusing_context, true) ||
            !js_runtime_state_ensure_input(owner)) return ItemError;
    // D8.1.3v25: the formal entry owns the same document binding formerly
    // performed after legacy MIR compilation, including stack-worker rebinding.
    if (runtime->dom_ui_context) dom_set_ui_context(runtime->dom_ui_context);
    if (runtime->dom_doc) dom_set_document(runtime->dom_doc);
    RuntimeExecutionScope execution_scope(owner);
    if (execution_scope.is_outermost() && !js_runtime_state.event_loop->callback_running &&
            js_dynamic_import_suppress_module_drain <= 0) {
        // the public host turn installs Lambda's Promise bridge and libuv drain;
        // nested module imports retain the parent's queues and scheduler.
        js_event_loop_init();
    }
    // D5.3: Script retirement follows heap finalization; escaping closures and
    // nominal shapes keep their compiler-owned records throughout that lifetime.
    MvpLmdRetainedScript* retained = NULL;
    if (!js_runtime_state.formal_mir_owner) {
        retained = mvp_lmd_retain_execution(runtime);
        if (!retained) return ItemError;
    }
    MvpLmdHost host = {0, NULL, stdout};
    host.native_timers = true;
    host.argv = js_process_bootstrap_arguments(&host.argc);
    // public file entry has always accepted both Script and Module sources;
    // use the parser's existing lexical goal selection before creating a realm.
    MvpLmdOptions options = {filename, module || js_c_source_is_module(source, length)};
    options.namespace_result = module;
    options.cached_code = cached_code;
    options.typescript = typescript;
    options.native_harness = native_harness;
    options.terminal_script = runtime->js_terminal_script;
    runtime->js_terminal_script = false;
    MvpLmdExecution* execution = mvp_lmd_execute_owned(source, length,
        &runtime->js_execution_ms, &host, &options, runtime);
    if (retained) retained->execution = execution;
    MvpLmdFailurePhase phase;
    const char* type = NULL;
    const char* message = mvp_lmd_diagnostic(execution, &phase, &type);
    Item result = mvp_lmd_result(execution);
    if (message && result.item == ITEM_ERROR && context && context->heap) {
        LambdaError* error = err_create_heap(phase == MVP_LMD_PARSE_FAILURE ? ERR_SYNTAX_ERROR :
            phase == MVP_LMD_ADMISSION_FAILURE ? ERR_NOT_IMPLEMENTED : ERR_USER_ERROR, message, NULL);
        result = error ? err2it(error) : ItemError;
    }
    if (message && item_is_error(result) && result.item != ITEM_ERROR) {
        RootFrame roots(1);
        Rooted<Item> failure(roots, result);
        LambdaError* error = it2err(failure.get());
        if (type) error->js_name_item = s2it(heap_create_name(type, strlen(type)));
        if (error->thrown_value_item && get_type_id(Item{.item = error->thrown_value_item}) == LMD_TYPE_MAP) {
            JsPropertyDescriptor descriptor = {};
            Item key = {.item = s2it(heap_create_name("message", 7))};
            Item found = mvp_lmd_get_own_property(Item{.item = error->thrown_value_item}, key, &descriptor);
            // reporting an uncaught value must not execute accessor code.
            if (!item_is_error(found) && found.item == ITEM_TRUE &&
                    (descriptor.flags & JS_PD_HAS_VALUE) && get_type_id(descriptor.value) == LMD_TYPE_STRING)
                error->js_message_item = descriptor.value.item;
        }
        result = failure.get();
    }
    if (message) log_error("js-mir: %s: %s", type ? type : "Error", message);
    return result_home ? lambda_item_adopt_scalar_home(result, result_home) : result;
}
MvpLmdExecution* mvp_lmd_execute(const char* source, size_t length, double* execution_ms,
        const MvpLmdHost* host) {
    return mvp_lmd_execute_with_options(source, length, execution_ms, host, NULL);
}
Item mvp_lmd_result(const MvpLmdExecution* e) { return e ? e->result : Item{.item = ITEM_ERROR}; }
const char* mvp_lmd_diagnostic(const MvpLmdExecution* e, MvpLmdFailurePhase* phase,
        const char** error_type) {
    if (!error_type) {
        const char* type = NULL;
        const char* message = mvp_lmd_diagnostic(e, phase, &type);
        if (e && type && message && strncmp(message, type, strlen(type))) {
            snprintf(e->result_diagnostic, sizeof(e->result_diagnostic), "%s: %s", type, message);
            return e->result_diagnostic;
        }
        return message;
    }
    const MvpLmdProgram* p = e ? (e->last_program ? e->last_program : &e->program) : NULL;
    if (phase) *phase = p ? p->failure_phase : MVP_LMD_INTERNAL_FAILURE;
    if (error_type) *error_type = p && p->failure_phase == MVP_LMD_PARSE_FAILURE ? "SyntaxError" : NULL;
    if (!e) return "MVP allocation failed";
    if (p->diagnostic[0]) return p->diagnostic;
    if (get_type_id(e->result) == LMD_TYPE_ERROR) {
        LambdaError* error = it2err(e->result);
        if (phase) *phase = p->failure_phase == MVP_LMD_RESOLUTION_FAILURE ? MVP_LMD_RESOLUTION_FAILURE :
            !error || error->code == ERR_POOL_EXHAUSTED ? MVP_LMD_INTERNAL_FAILURE :
            error->code == ERR_NOT_IMPLEMENTED ? MVP_LMD_ADMISSION_FAILURE : MVP_LMD_RUNTIME_FAILURE;
        if (error_type && error && !error->thrown_value_item) {
            *error_type = error->code == ERR_TYPE_MISMATCH ? "TypeError" :
                error->code == ERR_UNDEFINED_VARIABLE ? "ReferenceError" :
                error->code == ERR_INDEX_OUT_OF_BOUNDS ? "RangeError" :
                error->code == ERR_SYNTAX_ERROR ? "SyntaxError" : NULL;
        }
        if (error_type && error && error->thrown_value_item && p->library && p->library->errors) {
            Item thrown = {.item = error->thrown_value_item};
            MvpLmdClass* cls = mvp_lmd_class_record(thrown);
            int kind = mvp_lmd_error_kind(cls ? &cls->nominal : NULL, p->library);
            if (kind >= 0) *error_type = mvp_lmd_error_names[kind];
        }
        return error && error->message ? error->message : "MVP execution failed";
    }
    if (phase) *phase = MVP_LMD_SUCCESS;
    return NULL;
}
void mvp_lmd_dump(const MvpLmdExecution* e, FILE* output) {
    if (e && e->program.mir && output) MIR_output(e->program.mir, output);
}
static void mvp_lmd_release_unit(MvpLmdProgram* p) {
    mem_free(p->slots); mem_free(p->intrinsic_uses);
    if (p->code) mvp_lmd_code_release(p->code);
    else if (p->mir) { if (p->linked) MIR_gen_finish(p->mir); MIR_finish(p->mir); }
    if (p->relocations) arraylist_free(p->relocations);
    if (p->functions) {
        for (int i = 0; i < p->functions->length; i++) {
            LmdFunction* f = (LmdFunction*)p->functions->data[i];
            if (f->captures) arraylist_free(f->captures);
            mem_free(f);
        }
        arraylist_free(p->functions);
    }
    if (p->bindings) {
        for (int i = 0; i < p->bindings->length; i++) mem_free(p->bindings->data[i]);
        arraylist_free(p->bindings);
    }
    if (p->import_cache) hashmap_free(p->import_cache);
    if (p->private_bindings) hashmap_free(p->private_bindings);
    if (p->objects) arraylist_free(p->objects);
    if (p->classes) arraylist_free(p->classes);
    if (p->dynamic_scripts) {
        for (int i = 0; i < p->dynamic_scripts->length; i++)
            runtime_free_script(NULL, (Script*)p->dynamic_scripts->data[i], false);
        arraylist_free(p->dynamic_scripts);
    }
    if (p->frontend) js_transpiler_destroy(p->frontend);
}
Item mvp_lmd_compile_script_image(const char* source, size_t length,
        const char* filename, JsPreambleState* output) {
    if (!source || !output) return ItemError;
    // compile against a complete, uninstantiated realm. No guest values or
    // execution-owned metadata become part of the shared code image.
    MvpLmdProgram realm = {}, unit = {};
    realm.module_realm = realm.host_enabled = realm.test262_host = true;
    bool compiled = mvp_lmd_compile_unit(&realm, "", 0);
    if (compiled) {
        unit.realm_program = &realm;
        unit.library = realm.library;
        unit.module_realm = unit.host_enabled = unit.test262_host = unit.receiver_abi = true;
        unit.relocations = arraylist_new(32);
        unit.relocatable = true;
        compiled = unit.relocations && mvp_lmd_compile_unit(&unit, source, length, 2, filename);
    }
    if (compiled) {
        unit.code->source = mem_dup_n(source, length, MEM_CAT_EVAL);
        unit.code->source_length = length;
        unit.code->reference = mem_strdup(filename ? filename : "<script>", MEM_CAT_EVAL);
        compiled = unit.code->source && unit.code->reference;
    }
    if (compiled) {
        memset(output, 0, sizeof(*output));
        output->mvp_code = unit.code;
        mvp_lmd_code_retain(unit.code);
        output->mvp_realm = output->owns_compiled_state = true;
        output->mir_ctx = unit.code->mir;
        output->entry_func = unit.code->addresses->data[0];
        output->module_state_id = UINT32_MAX;
    } else {
        log_error("js-mvp-code-cache: compilation failed: %s", unit.diagnostic[0] ? unit.diagnostic : realm.diagnostic);
    }
    mvp_lmd_release_unit(&unit);
    mvp_lmd_release_unit(&realm);
    return compiled ? ItemNull : ItemError;
}
Item mvp_lmd_execute_script_image(Runtime* runtime, const MvpLmdCode* code) {
    if (!runtime || !code || !code->source) return ItemError;
    EvalContext* owner = runtime_get_eval_context(runtime);
    JsRuntimeState* state = owner ? js_runtime_state_for(owner) : NULL;
    if (!state || !state->formal_mir_owner) {
        // cached Scripts are children of a complete realm, including the
        // first preamble. Later entries share that same global environment.
        Item initialized = mvp_lmd_execute_in_runtime(runtime, "", 0, "<realm>", NULL);
        if (item_is_error(initialized)) return initialized;
    }
    return mvp_lmd_execute_in_runtime(runtime, code->source, code->source_length,
        code->reference, NULL, false, code);
}
void mvp_lmd_destroy(MvpLmdExecution* e) {
    if (!e) return;
    MvpLmdProgram* p = &e->program;
    if (e->owned_runtime) {
        // D5.3/D5.4.1: one owner retires jobs and heap before compiler metadata.
        runtime_cleanup(e->owned_runtime);
        mem_free(e->owned_runtime);
    }
    if (e->units) {
        for (int index = 0; index < e->units->length; index++) {
            auto* unit = (MvpLmdProgram*)e->units->data[index];
            mvp_lmd_release_unit(unit); mem_free(unit);
        }
        arraylist_free(e->units);
    }
    if (e->realm) mem_free(e->realm);
    mvp_lmd_release_unit(p);
    mem_free(e);
}
