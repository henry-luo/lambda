#pragma once

// Canonical layout for GC-owned JavaScript function objects.  Keep this
// lightweight so property and builtin modules do not need runtime internals.

#include "js_runtime.h"
#include "../lambda-data.hpp"

struct JsFunction;
struct AstFuncNode;
struct JsScript;
struct JsInterpEnv;

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

// An AST-bodied closure retains source-level semantics while using the ordinary
// JS call kernel. Only such a closure carries these facts, so a compiled-code
// function holds one null word instead of five fields and three flags. The two
// Items are precise GC edges reached through the owning value's tracer.
struct JsAstBody {
    AstFuncNode* function;
    JsScript* script;
    JsInterpEnv* env;
    Item lexical_this;
    Item lexical_new_target;
    // Derived once at closure creation; immutable because the source AST and
    // the lexical function form are immutable too.
    bool has_direct_eval;
    bool uses_arguments;
    bool tail_reuse_safe;
};

// Source origin of a dynamically compiled function.
struct JsEvalOrigin {
    String* filename;
    String* source;
    int64_t line_offset;
    int64_t column_offset;
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
    void* func_ptr;
    Item* env;
    Item prototype;
    String* name;
    Item properties_map;
    JsCallEntry invoke;
    JsConstructEntry construct;
    Item home_global;
    // `source_text` stays on the value: its setter js_set_function_source is a
    // NO_GC JIT import (sys_func_registry.c) and must not allocate, which a
    // lazily minted payload would force it to do.
    String* source_text;
    Context* runtime_context;
    // JSCUO8: every optional record hangs off this one word.
    JsFunctionPayload* payload;

    // 4-byte group
    int param_count;
    int env_size;
    int catalog_id;
    uint32_t module_state_id;

    // 2-byte group
    uint16_t flags;
    int16_t formal_length;

    // 1-byte group
    uint8_t intrinsic_class;
    // Concrete TypedArray constructors carry their element policy directly;
    // display names and catalog IDs are never executable selectors (D6.2.2v2).
    uint8_t typed_array_element_type_plus_one;
    uint8_t pool_pointer_roots_registered;
    bool eval_initializer_context;
    // `body_kind` stays inline because it is the discriminator every call site
    // tests before it looks at the payload at all.
    uint8_t body_kind;
};


// A missing payload reads as all-zero, so call sites keep the shape they had
// when these were inline fields and an unguarded read stays safe.
inline const JsNativeCode js_fn_native_absent{};
inline const JsAstBody js_fn_ast_absent{};
inline const JsBoundData js_fn_bound_absent{};
inline const JsClassData js_fn_class_absent{};
inline const JsWithData js_fn_with_absent{};

inline const JsEvalOrigin js_fn_eval_origin_absent{};

#define JS_FN_PAYLOAD_READ(fn, field) \
    ((fn) && (fn)->payload && (fn)->payload->field ? (fn)->payload->field \
                                                   : &js_fn_##field##_absent)

static inline const JsNativeCode* js_fn_native(const JsFunction* fn) {
    return JS_FN_PAYLOAD_READ(fn, native);
}
static inline const JsAstBody* js_fn_ast(const JsFunction* fn) {
    return JS_FN_PAYLOAD_READ(fn, ast);
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
static_assert(offsetof(JsFunction, type_id) < offsetof(JsFunction, func_ptr),
              "the discrimination prefix must precede every other field");

static inline void js_function_init_native_module_scope(JsFunction* fn) {
    if (!fn) return;
    // Pool allocation zeroes this field, but zero is a valid module id. Native
    // wrappers have no compiled module scope and must not switch callers to it.
    fn->module_state_id = UINT32_MAX;
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

static inline String* js_fn_source_text(const JsFunction* fn) {
    return fn ? fn->source_text : NULL;
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
