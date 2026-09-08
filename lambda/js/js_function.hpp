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
};

struct JsClassData {
    Item constructor;
    Item instance_prototype;
    Item superclass;
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

struct JsFunction {
    TypeId type_id;
    uint32_t layout_magic;
    void* func_ptr;
    int param_count;
    Item* env;
    int env_size;
    Item prototype;
    Item bound_this_store[2];
    JsBoundData* bound;
    String* name;
    int catalog_id;
    Item properties_map;
    uint16_t flags;
    uint8_t intrinsic_class;
    int16_t formal_length;
    // Concrete TypedArray constructors carry their element policy directly;
    // display names and catalog IDs are never executable selectors (D6.2.2v2).
    uint8_t typed_array_element_type_plus_one;
    uint8_t pool_pointer_roots_registered;
    JsCallEntry invoke;
    JsConstructEntry construct;
    // JSCU19: present only on a native wrapper; see JsNativeCode.
    JsNativeCode* native;
    uint32_t module_state_id;
    Item home_global;
    Item home_class;
    String* source_text;
    bool eval_initializer_context;
    JsWithData* with;
    // JSCU20: only a dynamically compiled function (eval / new Function / vm)
    // has an origin, so ordinary closures carry one null word instead of four
    // fields. Owned by this value and released by its destroy hook.
    JsEvalOrigin* eval_origin;
    // Source classes are functions with an explicit construct capability.
    // Keeping their constructor body and instance prototype here removes the
    // former callable-Map protocol and leaves ordinary properties observable.
    JsClassData* klass;
    Context* runtime_context;
    // JSCU20: present only on an AST-bodied closure. `body_kind` stays inline
    // because it is the body discriminator every call site tests.
    JsAstBody* ast;
    uint8_t body_kind;
};

// A missing payload reads as all-zero, so call sites keep the shape they had
// when these were inline fields and an unguarded read stays safe.
inline const JsNativeCode js_fn_native_absent{};
inline const JsAstBody js_fn_ast_absent{};
inline const JsBoundData js_fn_bound_absent{};
inline const JsClassData js_fn_class_absent{};
inline const JsWithData js_fn_with_absent{};

static inline const JsNativeCode* js_fn_native(const JsFunction* fn) {
    return fn && fn->native ? fn->native : &js_fn_native_absent;
}

static inline const JsAstBody* js_fn_ast(const JsFunction* fn) {
    return fn && fn->ast ? fn->ast : &js_fn_ast_absent;
}

static inline const JsBoundData* js_fn_bound(const JsFunction* fn) {
    return fn && fn->bound ? fn->bound : &js_fn_bound_absent;
}
static inline const JsClassData* js_fn_class(const JsFunction* fn) {
    return fn && fn->klass ? fn->klass : &js_fn_class_absent;
}
static inline const JsWithData* js_fn_with(const JsFunction* fn) {
    return fn && fn->with ? fn->with : &js_fn_with_absent;
}

JsNativeCode* js_fn_native_ensure(JsFunction* fn);
JsAstBody* js_fn_ast_ensure(JsFunction* fn);
JsBoundData* js_fn_bound_ensure(JsFunction* fn);
JsClassData* js_fn_class_ensure(JsFunction* fn);
JsWithData* js_fn_with_ensure(JsFunction* fn);

#define JS_FUNCTION_LAYOUT_MAGIC 0x4A53464Eu

// JSCUO6 -- inventory of what actually depends on this layout, so a field can
// be moved with the readers known rather than guessed at.
//
// (1) Cross-layout discrimination is the ONLY hard constraint. Three records
//     share the LMD_TYPE_FUNC tag and the `Item::function` (`Function*`) slot:
//     Lambda's `Function`, this `JsFunction`, and `JsAccessorPair`. Every
//     consumer that receives one of them as an Item reads `type_id` at 0 and
//     then `layout_magic` at 4 to decide which record it holds
//     (js_function_gc_trace / js_function_gc_compact in
//     js_runtime_function.cpp, js_function_get_target/get_arity there, and
//     the call kernel at js_runtime.cpp). Those two offsets must not move,
//     and must stay identical across JsFunction and JsAccessorPair.
// (2) No generated code reads a JsFunction field. JS MIR lowers every call to
//     a named C helper (js_call_function_into and friends) and dispatches
//     through `fn->invoke` in C, so unlike Lambda's `Function` -- whose
//     `flags` word transpile-mir.cpp loads at a baked `offsetof` -- this
//     record has no emitted-offset reader to preserve.
// (3) The collector reaches this record only through the js_function_trace
//     hook, which uses field names; the raw LAMBDA_GC_OFF_FUNCTION_* path in
//     gc_heap.c runs only after that hook declines, i.e. for `Function`.
//
// The two historical pins below (func_ptr at 8, bound_this_store at 48) were
// added with the scalar-GC-invariant work and back no reader found by this
// inventory. They are kept as tripwires, not as an ABI contract: moving those
// fields is safe once this block is updated in the same change.
static_assert(offsetof(JsFunction, type_id) == 0,
              "JsFunction type tag must sit where every Item consumer reads it");
static_assert(offsetof(JsFunction, layout_magic) == 4,
              "JsFunction layout discriminator is read before any field access");
static_assert(offsetof(JsAccessorPair, layout_magic) == offsetof(JsFunction, layout_magic),
              "accessor pairs share the FUNC tag and must discriminate at the same offset");
static_assert(offsetof(JsFunction, func_ptr) == 8,
              "JsFunction prefix must preserve the compiled-function ABI");
static_assert(offsetof(JsFunction, bound_this_store) == 48,
              "JsFunction bound-this slot must preserve the shared ABI");

static inline void js_function_init_native_module_scope(JsFunction* fn) {
    if (!fn) return;
    // Pool allocation zeroes this field, but zero is a valid module id. Native
    // wrappers have no compiled module scope and must not switch callers to it.
    fn->module_state_id = UINT32_MAX;
}

static inline void js_function_set_bound_this(JsFunction* fn, Item value) {
    owned_item_slot_store(fn->bound_this_store, 1, 0, value);
}

static inline Item js_function_get_bound_this(JsFunction* fn) {
    return owned_item_slot_read(fn->bound_this_store, 1, 0, false);
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
