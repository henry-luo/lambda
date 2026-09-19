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

// A selected AUTO root keeps nested eval and dynamic functions in the AST
// executor. This is execution state, not an instruction to suppress AUTO's
// compatibility check for a new root module.
static inline bool js_ast_interpreter_requested(void) {
    return js_ast_interpreter_forced() ||
        (context && context->runtime && context->runtime->js_ast_backend);
}

// AUTO starts a supported unit in the retained AST executor. If the unit is
// outside that executor's coverage, the caller keeps the established MIR
// fallback rather than publishing a mixed-tier realm.
static inline bool js_execution_auto_requested(void) {
    const char* backend = getenv("JS_EXECUTION_BACKEND");
    return backend && strcmp(backend, "auto") == 0;
}
// Parse, bind, and retain a classic Script without evaluating it. Batch hosts
// use this to keep a harness AST across fresh per-test realms.
JsScript* js_interp_prepare_script(Runtime* runtime, const char* source,
                                   size_t source_length, const char* filename,
                                   bool strict = false);
Item js_interp_execute_script(Runtime* runtime, JsScript* script,
                              uint64_t* result_home);
Item js_interp_execute_source(Runtime* runtime, const char* source,
                              size_t source_length, const char* filename,
                              uint64_t* result_home);
// Execute indirect eval code in the AST tier with EvalDeclarationInstantiation
// global binding semantics.
Item js_interp_execute_indirect_eval_source(Runtime* runtime, const char* source,
                                             size_t source_length, const char* filename,
                                             uint64_t* result_home);
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
Item js_interp_call_function(JsFunction* function, Item* args, int arg_count,
                             uint64_t* result_home);
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
bool js_interp_script_is_supported(JsScript* script);
