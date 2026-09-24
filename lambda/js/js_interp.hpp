#pragma once

#include <cstdlib>
#include <cstring>

#include "js_transpiler.hpp"
#include "js_function.hpp"
#include "../runtime/runtime-state.h"

// Tree-walking execution tier. It intentionally shares the JS object/value
// helpers and the Runtime/EvalContext ownership model with MIR lowering.
static inline bool js_ast_interpreter_forced(void) {
    const char* backend = getenv("JS_EXECUTION_BACKEND");
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
    const char* backend = getenv("JS_EXECUTION_BACKEND");
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
                              uint64_t* result_home);
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
struct JsGeneratorStateRecord;
extern "C" Item js_interp_resume_generator(Item generator,
                                            JsGeneratorStateRecord* state,
                                            Item input);
struct gc_heap;
void js_interp_generator_trace_continuations(JsGeneratorStateRecord* state,
                                             struct gc_heap* gc);
void js_interp_generator_clear_continuations(JsGeneratorStateRecord* state);
struct JsAsyncContextStateRecord;
void js_interp_async_clear_continuations(JsAsyncContextStateRecord* state);
void js_interp_async_trace_continuations(JsAsyncContextStateRecord* state,
        struct gc_heap* gc);
extern "C" Item js_interp_resume_async(JsAsyncContextStateRecord* state,
                                        Item input);
extern "C" Item js_interp_resume_module_async(JsAsyncContextStateRecord* state,
                                               Item input);
bool js_interp_script_is_supported(JsScript* script);
