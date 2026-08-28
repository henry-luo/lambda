#pragma once

#include "js_transpiler.hpp"
#include "js_function.hpp"

// Tree-walking execution tier. It intentionally shares the JS object/value
// helpers and the Runtime/EvalContext ownership model with MIR lowering.
bool js_ast_interpreter_requested(void);
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
extern "C" Item js_interp_resume_async(JsAsyncContextStateRecord* state,
                                        Item input);
bool js_interp_script_is_supported(JsScript* script);
