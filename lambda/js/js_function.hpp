#pragma once

// Canonical layout for GC-owned JavaScript function objects.  Keep this
// lightweight so property and builtin modules do not need runtime internals.

#include "js_runtime.h"
#include "../lambda-data.hpp"

struct JsFunction;
struct AstFuncNode;
struct JsScript;
struct JsInterpEnv;
struct JsAstDefinition;
struct JsCallableCode;

JsAstDefinition* js_script_ast_definition_ensure(JsScript* script,
                                                 AstFuncNode* function);
JsCallableCode* js_script_ast_definition_code_ensure(
    JsAstDefinition* definition, int param_count, uint32_t module_state_id);

enum JsFunctionBodyKind : uint8_t {
    JS_FUNCTION_BODY_CODE = 0,
    JS_FUNCTION_BODY_AST = 1,
};

// Per-callee call entry. Choosing the calling protocol once, at function
// finalization, lets each entry contain only the steps its callee's shape
// actually needs instead of re-deciding inside one shared dispatcher body on
// every call. The generic dispatcher is itself an entry, so every function has
// one and there is no "miss" path.
typedef Item (*JsCallEntry)(Item fn_item, Item this_val, Item* args, int argc,
                            uint64_t* result_home, bool args_prerooted);

// Construction is a separate capability under D6.2.2v2. The caller supplies
// newTarget explicitly; an absent entry is the complete IsConstructor answer.
typedef Item (*JsConstructEntry)(Item fn_item, Item* args, int argc,
                                 Item new_target, uint64_t* result_home,
                                 bool args_prerooted);

union JsNativeTarget {
    JsNativeP0 p0;
    JsNativeP1 p1;
    JsNativeP2 p2;
    JsNativeP3 p3;
    JsNativeP4 p4;
    JsNativeP5 p5;
    JsNativeP6 p6;
    JsNativeP7 p7;
    JsNativeP8 p8;
    JsNativeSpan span;
    JsNativeThisSpan this_span;
    uint64_t bits;
};

enum JsNativeCallPolicy : uint8_t {
    JS_NATIVE_CALL_NONE = 0,
    JS_NATIVE_CALL_FIXED = 1,
    JS_NATIVE_CALL_REST = 2,
    JS_NATIVE_CALL_SPAN = 3,
    JS_NATIVE_CALL_THIS_SPAN = 4,
    JS_NATIVE_CALL_BODY = 6,
};

// JSCU20 optional payloads. Each is present only on the values that need it,
// so an ordinary closure carries three null words rather than eight fields.
// Reads go through the null-safe accessors below; writes allocate.
struct JsBoundData {
    Item target;
    Item* args;
    int argc;
    // JSCUO9: the bound receiver's owned scalar home. Only a bound function has
    // one, so it moved off the value with the rest of the bound state.
    Item this_store[2];
};

struct JsClassData {
    Item constructor;
    Item instance_prototype;
    Item superclass;
    // CustomElementRegistry owns this internal association; keeping it beside
    // the class capability avoids exposing host bookkeeping as a JS property.
    Item custom_element_name;
};

struct JsWithData {
    Item* env;
    int depth;
};

// JSCU19: the immutable native-call facts of a callable, split off the value
// record. Only a native wrapper has them, so a script closure carries one null
// word instead of 32 B of zeros. The wrapper cache already dedupes *values* by
// (target, arity, kind, policy, capabilities), so a record is created exactly
// once per native code identity and its lifetime is that of its wrapper.
struct JsNativeCode {
    JsNativeCallBody      call;
    JsNativeConstructBody construct;
    JsNativeTarget        target;
    uint8_t               arity;
    uint8_t               policy;
};

// Immutable AST definition facts are owned by JsScript and shared by every
// closure made for the same AstFuncNode. The per-value body retains only its
// closure environment and lexical Item homes (D6.2.1; JSCU33(A)).
struct JsAstDefinition {
    AstFuncNode* function;
    JsScript* script;
    JsCallableCode* code;
    bool has_direct_eval;
    bool uses_arguments;
    bool tail_reuse_safe;
};

// An AST-bodied closure retains source-level semantic state while using the
// ordinary JS call kernel. The definition pointer is non-owning; the Script
// retains the definition rows until its AST function values are gone.
struct JsAstBody {
    JsAstDefinition* definition;
    JsInterpEnv* env;
    Item lexical_this;
    Item lexical_new_target;
};

// Source origin of a dynamically compiled function.
struct JsEvalOrigin {
    String* filename;
    String* source;
    int64_t line_offset;
    int64_t column_offset;
};

// JSCU33(A): immutable definition-site facts have one owner shared by the
// callable value and any wrappers materialized from that definition. Mutable
// value state (prototype, properties, bound/class payload and finalized
// capabilities) remains on JsFunction itself.
struct JsCallableCode {
    void* func_ptr;
    String* source_text;
    Context* runtime_context;
    int param_count;
    int catalog_id;
    uint32_t module_state_id;
    int16_t formal_length;
    uint8_t intrinsic_class;
    uint8_t typed_array_element_type_plus_one;
    bool eval_initializer_context;
    uint8_t body_kind;
    uint32_t intern_refcount;
    bool interned;
    // AST definition records live in their retained Script pool; their code
    // pointer is shared by every closure made from that definition.
    bool definition_owned;
    // a weak realm table is detached before its owner is destroyed.
    HashMap* intern_table;
    // module-scoped source recipe, shared by values from this definition.
    uint32_t construction_first;
    uint32_t construction_count;
};

// JSCUO8: one payload word on the value, not six. A value that needs none
// carries a single null pointer; a value that needs any carries one container
// and pays for only the records it actually has. Six separate pointers cost
// 48 B on every function value, including the overwhelming majority that use
// at most one of them.
struct JsFunctionPayload {
    Item          home_class;
    JsBoundData*  bound;
    JsClassData*  klass;
    JsWithData*   with;
    JsAstBody*    ast;
    JsNativeCode* native;
    JsEvalOrigin* eval_origin;
};

struct JsFunction {
    // JSCUO9: fields are grouped by alignment. The previous source order left
    // 30 B of interior padding — six 4- and 1-byte fields each sitting in an
    // 8-byte hole — which is what kept the record in the 256 B GC size class.
    // Only the discrimination prefix below is contractual (see JSCUO6).
    TypeId type_id;
    uint32_t layout_magic;

    // 8-byte group
    Item* env;
    Item prototype;
    String* name;
    Item properties_map;
    JsCallEntry invoke;
    JsConstructEntry construct;
    Item home_global;
    JsCallableCode* code;
    // JSCUO8: every optional value record hangs off this one word.
    JsFunctionPayload* payload;

    // 4-byte group
    int env_size;

    // 2-byte group
    uint16_t flags;
    uint8_t pool_pointer_roots_registered;
};


// A missing payload reads as all-zero, so call sites keep the shape they had
// when these were inline fields and an unguarded read stays safe.
inline const JsNativeCode js_fn_native_absent{};
inline const JsAstBody js_fn_ast_absent{};
inline const JsBoundData js_fn_bound_absent{};
inline const JsClassData js_fn_class_absent{};
inline const JsWithData js_fn_with_absent{};

inline const JsEvalOrigin js_fn_eval_origin_absent{};
inline const JsCallableCode js_fn_code_absent{
    NULL, NULL, NULL, 0, 0, UINT32_MAX, -1, 0, 0, false,
    JS_FUNCTION_BODY_CODE, 0, false, false, NULL, 0, 0};

#define JS_FN_PAYLOAD_READ(fn, field) \
    ((fn) && (fn)->payload && (fn)->payload->field ? (fn)->payload->field \
                                                   : &js_fn_##field##_absent)

static inline const JsCallableCode* js_fn_code(const JsFunction* fn) {
    return fn && fn->code ? fn->code : &js_fn_code_absent;
}
static inline void* js_fn_func_ptr(const JsFunction* fn) {
    return js_fn_code(fn)->func_ptr;
}
static inline String* js_fn_source_text(const JsFunction* fn) {
    return js_fn_code(fn)->source_text;
}
static inline Context* js_fn_runtime_context(const JsFunction* fn) {
    return js_fn_code(fn)->runtime_context;
}
static inline int js_fn_param_count(const JsFunction* fn) {
    return js_fn_code(fn)->param_count;
}
static inline int js_fn_catalog_id(const JsFunction* fn) {
    return js_fn_code(fn)->catalog_id;
}
static inline uint32_t js_fn_module_state_id(const JsFunction* fn) {
    return js_fn_code(fn)->module_state_id;
}
static inline int16_t js_fn_formal_length(const JsFunction* fn) {
    return js_fn_code(fn)->formal_length;
}
static inline uint8_t js_fn_intrinsic_class(const JsFunction* fn) {
    return js_fn_code(fn)->intrinsic_class;
}
static inline uint8_t js_fn_typed_array_element_type_plus_one(const JsFunction* fn) {
    return js_fn_code(fn)->typed_array_element_type_plus_one;
}
static inline bool js_fn_eval_initializer_context(const JsFunction* fn) {
    return js_fn_code(fn)->eval_initializer_context;
}
static inline uint8_t js_fn_body_kind(const JsFunction* fn) {
    return js_fn_code(fn)->body_kind;
}

static inline const JsNativeCode* js_fn_native(const JsFunction* fn) {
    return JS_FN_PAYLOAD_READ(fn, native);
}
static inline const JsAstBody* js_fn_ast(const JsFunction* fn) {
    return JS_FN_PAYLOAD_READ(fn, ast);
}
static inline const JsAstDefinition* js_fn_ast_definition(const JsFunction* fn) {
    const JsAstBody* ast = js_fn_ast(fn);
    return ast && ast->definition ? ast->definition : NULL;
}
static inline AstFuncNode* js_fn_ast_function(const JsFunction* fn) {
    const JsAstDefinition* definition = js_fn_ast_definition(fn);
    return definition ? definition->function : NULL;
}
static inline JsScript* js_fn_ast_script(const JsFunction* fn) {
    const JsAstDefinition* definition = js_fn_ast_definition(fn);
    return definition ? definition->script : NULL;
}
static inline bool js_fn_ast_has_direct_eval(const JsFunction* fn) {
    const JsAstDefinition* definition = js_fn_ast_definition(fn);
    return definition && definition->has_direct_eval;
}
static inline bool js_fn_ast_uses_arguments(const JsFunction* fn) {
    const JsAstDefinition* definition = js_fn_ast_definition(fn);
    return definition && definition->uses_arguments;
}
static inline bool js_fn_ast_tail_reuse_safe(const JsFunction* fn) {
    const JsAstDefinition* definition = js_fn_ast_definition(fn);
    return definition && definition->tail_reuse_safe;
}
static inline const JsBoundData* js_fn_bound(const JsFunction* fn) {
    return JS_FN_PAYLOAD_READ(fn, bound);
}
static inline const JsWithData* js_fn_with(const JsFunction* fn) {
    return JS_FN_PAYLOAD_READ(fn, with);
}
static inline const JsEvalOrigin* js_fn_eval_origin(const JsFunction* fn) {
    return JS_FN_PAYLOAD_READ(fn, eval_origin);
}
static inline const JsClassData* js_fn_class(const JsFunction* fn) {
    return fn && fn->payload && fn->payload->klass ? fn->payload->klass
                                                   : &js_fn_class_absent;
}

JsNativeCode* js_fn_native_ensure(JsFunction* fn);
JsAstBody* js_fn_ast_ensure(JsFunction* fn);
JsBoundData* js_fn_bound_ensure(JsFunction* fn);
JsClassData* js_fn_class_ensure(JsFunction* fn);
JsWithData* js_fn_with_ensure(JsFunction* fn);
JsEvalOrigin* js_fn_eval_origin_ensure(JsFunction* fn);
JsCallableCode* js_fn_code_ensure(JsFunction* fn);
JsCallableCode* js_callable_code_intern_mir(JsFunction* fn, void* func_ptr,
        Context* runtime_context, int param_count, uint32_t module_state_id);
void js_callable_code_release(JsCallableCode* code);
void js_callable_code_table_destroy(HashMap* table);

#define JS_FUNCTION_LAYOUT_MAGIC 0x4A53464Eu

// JSCUO6 -- inventory of what actually depends on this layout, so a field can
// be moved with the readers known rather than guessed at.
//
// (1) Cross-layout discrimination is the ONLY hard constraint. Lambda's
//     `Function` and this `JsFunction` share the LMD_TYPE_FUNC tag. Accessor
//     cells use GC_TYPE_JS_ACCESSOR and never enter this discriminator path.
//     Consumers that receive a callable Item read `type_id` at 0 and then
//     `layout_magic` at 4 to decide which callable record it holds.
// (2) No generated code reads a JsFunction field. JS MIR lowers every call to
//     a named C helper (js_call_function_into and friends) and dispatches
//     through `fn->invoke` in C, so unlike Lambda's `Function` -- whose
//     `flags` word transpile-mir.cpp loads at a baked `offsetof` -- this
//     record has no emitted-offset reader to preserve.
// (3) The collector reaches this record only through the js_function_trace
//     hook, which uses field names; the raw LAMBDA_GC_OFF_FUNCTION_* path in
//     gc_heap.c runs only after that hook declines, i.e. for `Function`.
//
// The two historical pins (func_ptr at 8, bound_this_store at 48) were added
// with the scalar-GC-invariant work and back no reader found by this
// inventory, so JSCUO6 kept them as tripwires rather than an ABI contract and
// said moving the fields is safe once this block is updated in the same
// change. JSCUO9 took that route: `bound_this_store` moved into JsBoundData
// (only a bound function has a bound receiver) and the remaining fields are
// ordered by alignment, so neither pin survives. Only the two discrimination
// offsets below are contractual.
static_assert(offsetof(JsFunction, type_id) == 0,
              "JsFunction type tag must sit where every Item consumer reads it");
static_assert(offsetof(JsFunction, layout_magic) == 4,
              "JsFunction layout discriminator is read before any field access");
static_assert(offsetof(JsFunction, type_id) < offsetof(JsFunction, code),
              "the discrimination prefix must precede every other field");

static inline void js_function_init_native_module_scope(JsFunction* fn) {
    if (!fn) return;
    // Pool allocation zeroes this field, but zero is a valid module id. Native
    // wrappers have no compiled module scope and must not switch callers to it.
    JsCallableCode* code = js_fn_code_ensure(fn);
    if (code) code->module_state_id = UINT32_MAX;
}

static inline void js_function_set_bound_this(JsFunction* fn, Item value) {
    JsBoundData* bound = js_fn_bound_ensure(fn);
    if (bound) owned_item_slot_store(bound->this_store, 1, 0, value);
}

static inline Item js_function_get_bound_this(JsFunction* fn) {
    // only a bound function has the home; anything else has no bound receiver
    if (!fn || !fn->payload || !fn->payload->bound) return ItemNull;
    return owned_item_slot_read(fn->payload->bound->this_store, 1, 0, false);
}

// JSCUO9: only a method carries a home class, and its setter
// js_set_function_home_class is MAY_GC, so it may mint the payload.
static inline Item js_fn_home_class(const JsFunction* fn) {
    return fn && fn->payload ? fn->payload->home_class : ItemNull;
}

#define JS_FUNC_FLAG_GENERATOR 1
#define JS_FUNC_FLAG_ARROW     2
#define JS_FUNC_FLAG_TYPED_ARRAY_METHOD 4
#define JS_FUNC_FLAG_STRICT    8
#define JS_FUNC_FLAG_HAS_BOUND_THIS 16
#define JS_FUNC_FLAG_METHOD    32
#define JS_FUNC_FLAG_ASYNC_GEN 64
#define JS_FUNC_FLAG_ASYNC     128
#define JS_FUNC_FLAG_DERIVED_CTOR 256
#define JS_FUNC_FLAG_MIR_PUBLIC_ABI 512
#define JS_FUNC_FLAG_USES_WITH 1024
#define JS_FUNC_FLAG_READS_THIS 2048
#define JS_FUNC_FLAG_READS_NEW_TARGET 4096
#define JS_FUNC_FLAG_ANALYSIS_KNOWN 8192
#define JS_FUNC_FLAG_MIR_CONTEXT_ABI 16384
#define JS_FUNC_FLAG_CLASS_CONSTRUCTOR 32768

#define JS_FUNC_POOL_POINTER_ROOTS_REGISTERED 1
#define JS_FUNC_FLAG_DATA_VIEW_ACCESSOR JS_FUNC_FLAG_METHOD
