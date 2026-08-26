#pragma once

#include "js_transpiler.hpp"
#include "js_function.hpp"

// Tree-walking execution tier. It intentionally shares the JS object/value
// helpers and the Runtime/EvalContext ownership model with MIR lowering.
Item js_interp_execute_script(Runtime* runtime, JsScript* script,
                              uint64_t* result_home);
Item js_interp_execute_source(Runtime* runtime, const char* source,
                              size_t source_length, const char* filename,
                              uint64_t* result_home);
Item js_interp_call_function(JsFunction* function, Item* args, int arg_count,
                             uint64_t* result_home);
bool js_interp_script_is_supported(JsScript* script);
