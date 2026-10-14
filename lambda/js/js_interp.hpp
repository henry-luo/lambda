#pragma once

#include <cstdlib>
#include <cstring>

#include "js_transpiler.hpp"
#include "js_function.hpp"
#include "../runtime/runtime-state.h"

// Tree-walking execution tier. It intentionally shares the JS object/value
// helpers and the Runtime/EvalContext ownership model with MIR lowering.
static inline bool js_ast_interpreter_forced(void) {
    const char* backend = getenv("JS_EXEC_BACKEND");
    return backend && (strcmp(backend, "ast") == 0 ||
        strcmp(backend, "interpreter") == 0);
}

// A selected AUTO root keeps nested module loads in the AST executor. This is
// execution state, not an instruction to suppress AUTO's compatibility check
// for a new root module. Dynamic source (eval, Function constructors) always
// runs in the AST executor regardless of this state (JSI35).
static inline bool js_ast_interpreter_requested(void) {
    return js_ast_interpreter_forced() ||
        (context && context->runtime && context->runtime->js_ast_backend);
}

// AUTO starts a supported unit in the retained AST executor. If the unit is
// outside that executor's coverage, the caller keeps the established MIR
// fallback rather than publishing a mixed-tier realm.
static inline bool js_execution_auto_requested(void) {
    const char* backend = getenv("JS_EXEC_BACKEND");
    // D8.1.3v19: a supported unit starts in the retained AST tier. `mir`
    // remains the explicit opt-in, while unsupported AST surface falls back
    // to the whole-module MIR lane at the ordinary admission gate.
    return !backend || !backend[0] || strcmp(backend, "auto") == 0;
}
// Parse, bind, and retain a classic Script without evaluating it. Batch hosts
// use this to keep a harness AST across fresh per-test realms.
JsScript* js_interp_prepare_script(Runtime* runtime, const char* source,
                                   size_t source_length, const char* filename,
                                   bool strict = false);
// Retain an ES module template with its module-specific scope graph.
JsScript* js_interp_prepare_es_module_script(Runtime* runtime, const char* source,
                                             size_t source_length,
                                             const char* filename);
Item js_interp_execute_script(Runtime* runtime, JsScript* script,
                              uint64_t* result_home);
Item js_interp_execute_source(Runtime* runtime, const char* source,
                              size_t source_length, const char* filename,
                              uint64_t* result_home, bool dynamic_source = false);
// Execute a Test262 classic source with its realm-local native helper object.
Item js_interp_execute_test262_source(Runtime* runtime, const char* source,
                                      size_t source_length, const char* filename,
                                      bool native_harness, uint64_t* result_home);
// Execute indirect eval code in the AST tier with EvalDeclarationInstantiation
// global binding semantics.
Item js_interp_execute_indirect_eval_source(Runtime* runtime, const char* source,
                                             size_t source_length, const char* filename,
                                             uint64_t* result_home);
// Execute direct eval code for a MIR caller (JSI35), which has installed the
// EvalContext bridge; `strict` is the caller's strictness, which the eval code
// inherits. An interpreted caller links its own environments instead.
Item js_interp_execute_direct_eval_source(Runtime* runtime, const char* source,
                                           size_t source_length, const char* filename,
                                           bool strict, uint64_t* result_home);
// The eval steps before parsing (js_dynamic_code.cpp): a non-string argument is
// the result, and a blank source, a V8 native probe, or a lone RegExp literal
// completes without a script. Returns true when *result holds the completion
// or the early error that was thrown.
bool js_eval_source_shortcut(Item code_item, bool is_direct_eval, Item* result);
// Execute source in a module-private slab. `strict` distinguishes the
// CommonJS wrapper (sloppy) from an ES module (always strict).
Item js_interp_execute_module_source(Runtime* runtime, const char* source,
                                     size_t source_length, const char* filename,
                                     bool strict, uint64_t* result_home);
// Compile/link/evaluate a synchronous ES module in the shared registry. The
// returned value is its stable namespace object, not the body's completion.
Item js_interp_execute_es_module_source(Runtime* runtime, const char* source,
                                        size_t source_length, const char* filename,
                                        uint64_t* result_home);
Item js_interp_execute_es_module_script(Runtime* runtime, JsScript* script,
                                        uint64_t* result_home);
// Return the module's recorded evaluation error in the JS throw lane after a
// host turn has drained its top-level-await carrier.
Item js_interp_es_module_evaluation_error(Runtime* runtime, JsScript* script);
Item js_interp_call_function(JsFunction* function, Item* args, int arg_count,
                             uint64_t* result_home);
// P2 promotion is attempted only at an ordinary call-entry boundary.
bool js_interp_promote_function_if_hot(JsFunction* function);
Item js_interp_start_async_function(JsFunction* function, Item* args,
                                    int arg_count);
Item js_interp_create_generator(JsFunction* function, Item* args, int arg_count);
// The bodies a generator or async carrier runs on its activation (RA1).
struct Activation;
extern "C" Item js_interp_generator_body(Activation* self, Item generator);
extern "C" Item js_interp_async_body(Activation* self, Item unused);
extern "C" Item js_interp_module_async_body(Activation* self, Item unused);
extern "C" Item js_mir_module_async_body(Activation* self, Item unused);
bool js_interp_script_is_supported(JsScript* script);

// A dynamic-source client supplies its own value semantics while sharing the
// retained AST walker and durable lexical cells (D8.1.3v24, D5.2-D5.3).
// This profile never creates a second realm or promotes a dynamic body to MIR.
enum JsInterpHostOperation {
    JS_INTERP_HOST_GET, JS_INTERP_HOST_HAS, JS_INTERP_HOST_SET,
    JS_INTERP_HOST_DELETE, JS_INTERP_HOST_DECLARE, JS_INTERP_HOST_CHECK_DECLARE,
    JS_INTERP_HOST_HAS_LEXICAL, JS_INTERP_HOST_CHECK_DECLARE_FUNCTION,
    JS_INTERP_HOST_DECLARE_FUNCTION
};
enum JsInterpHostError {
    JS_INTERP_HOST_CAPABILITY, JS_INTERP_HOST_REFERENCE, JS_INTERP_HOST_TYPE,
    JS_INTERP_HOST_RANGE, JS_INTERP_HOST_MEMORY, JS_INTERP_HOST_SYNTAX
};
enum JsInterpPrivateOperation {
    JS_INTERP_PRIVATE_NAME, JS_INTERP_PRIVATE_GET, JS_INTERP_PRIVATE_SET,
    JS_INTERP_PRIVATE_HAS, JS_INTERP_PRIVATE_ADD, JS_INTERP_PRIVATE_METHOD
};
struct JsInterpHostOps {
    void* state;
    Item (*global)(void*, Item key, Item value, int operation, bool strict);
    Item (*global_this)(void*);
    Item (*property)(void*, Item object, Item key, Item value, int operation, bool strict, Item receiver);
    Item (*property_key)(Item);
    Item (*call)(void*, Item callee, Item receiver, Item*, int, Item new_target);
    Item (*object)(void*);
    Item (*wrap_function)(void*, Item);
    JsFunction* (*unwrap_function)(Item);
    Item (*name_function)(Item, Item);
    Item (*binary)(int, Item, Item);
    Item (*unary)(int, Item);
    Item (*fail)(int);
    Item (*raise)(Item);
    Item (*caught)(Item);
    Item (*bind_this)(void*, Item, bool strict);
    bool (*retain_script)(void*, JsScript*);
    Item (*class_create)(void*, AstClassNode*, JsScript*, Item base, Item constructor);
    Item (*array_new)(int64_t length);
    Item (*array_append)(Item, Item value, bool hole);
    Item (*arguments_create)(void*, Item callee, Item*, int, AstFuncNode*, JsInterpEnv*, bool mapped);
    Item (*parameter_cell)(Item cell, Item value, bool write);
    Item (*to_string)(Item);
    Item (*iterator_get)(void*, Item);
    Item (*iterator_step)(Item, bool read_value);
    Item (*iterator_close)(Item, Item completion);
    Item (*iterator_collect_rest)(void*, Item);
    Item (*array_spread)(void*, Item target, Item source);
    Item (*object_copy)(void*, Item target, Item source, Item* excluded, int count);
    Item (*template_object)(void*, JsScript*, JsTemplateLiteralNode*, Item* cooked, Item* raw, int count);
    Item (*intrinsic_eval)(void*);
    Item (*generator_create)(void*, Item internal, Item wrapper, Item arguments, Item receiver, bool is_async);
    Item (*class_method)(void*, Item, AstMethodNode*, Item key, Item function);
    Item (*class_field)(void*, Item, AstClassFieldNode*, Item key, Item value,
        uint32_t field_index, uint32_t initializer_index, bool initialize);
    Item (*initialize_instance)(Item, Item receiver, int64_t statics);
    Item (*private_element)(Item, Item key, Item value, int64_t operation);
    Item (*prototype)(Item);
    Item (*constructor_result)(Item value, Item receiver, int64_t derived);
    bool (*is_derived_constructor)(Item);
    Item (*object_property)(void*, Item, JsPropertyNode*, Item key, Item value);
    Item (*for_in_keys)(Item);
    Item (*for_in_key_is_live)(Item, Item key);
};
// The caller owns these exact roots for the duration of a host operation.
// Escaped functions retain env, never this activation-local descriptor.
enum JsInterpHostCallerFlags : uint32_t {
    JS_INTERP_CALLER_STRICT = 1, JS_INTERP_CALLER_PARAMETERS = 2,
    JS_INTERP_CALLER_METHOD = 4, JS_INTERP_CALLER_INITIALIZER = 8
};
struct JsInterpHostCaller {
    JsScript* script;
    JsInterpEnv* env;
    Item receiver;
    Item new_target;
    Item home_object;
    Item class_constructor;
    uint32_t flags;
};
enum JsInterpLookupKind : uint8_t {
    JS_INTERP_LOOKUP_OUTER, JS_INTERP_LOOKUP_SLOT,
    JS_INTERP_LOOKUP_EVAL, JS_INTERP_LOOKUP_ARGUMENTS
};
struct JsInterpLookup {
    JsInterpLookupKind kind;
    NameEntry* entry;
    JsInterpEnv* env;
    JsScript* outer_script;
};
struct JsInterpHostReference {
    JsInterpLookup lookup;
    Item key;
    bool unresolvable;
};
enum JsInterpHostReferenceOperation {
    JS_INTERP_REFERENCE_RESOLVE, JS_INTERP_REFERENCE_GET,
    JS_INTERP_REFERENCE_SET, JS_INTERP_REFERENCE_DELETE,
    JS_INTERP_REFERENCE_TYPEOF
};
JsInterpEnv* js_interp_env_create(NameScope*, JsInterpEnv*);
Item js_interp_initialize_body_vars(JsInterpEnv* body, JsInterpEnv* parameters);
Item js_interp_host_reference(const JsInterpHostOps*, const JsInterpHostCaller*,
    JsInterpHostReference*, Item key, Item value, int operation);
Item js_interp_execute_host_eval(const JsInterpHostOps*, const JsInterpHostCaller*, Item source);

class JsInterpHostScope {
    const JsInterpHostOps* previous_;
public:
    explicit JsInterpHostScope(const JsInterpHostOps* ops);
    ~JsInterpHostScope();
    JsInterpHostScope(const JsInterpHostScope&) = delete;
    JsInterpHostScope& operator=(const JsInterpHostScope&) = delete;
};
Item js_interp_execute_host_source(const JsInterpHostOps*, const char*, size_t,
    const char* filename, bool indirect_eval);
Item js_interp_call_host_function(const JsInterpHostOps*, Item wrapper,
    JsFunction*, Item receiver, Item*, int, Item new_target);

// The Function source builder owns parsing/early errors once; a host profile
// replaces only conversion, execution and metadata publication.
struct JsDynamicFunctionOps {
    void* state;
    Item (*to_string)(Item);
    Item (*syntax_error)(void*, const char*);
    Item (*execute)(void*, const char*, size_t);
    void (*metadata)(void*, Item, Item name, Item source);
};
Item js_new_function_from_string_with_ops(Item*, int, const char* parse_prefix,
    const char* source_prefix, const JsDynamicFunctionOps*);

Item js_new_interpreted_function_with_host(AstFuncNode*, JsScript*, JsInterpEnv*,
    uint32_t flags, Item home_global, Item lexical_this, Item lexical_new_target);
