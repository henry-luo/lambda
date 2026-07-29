#pragma once

#include <stdint.h>

// Active runtime process state. This declaration deliberately lives outside
// frozen lambda.h so consumers of the native/MIR-direct runtime have a
// provider-owned surface.
#ifdef __cplusplus
struct EvalContext;
struct Context;

// The current active evaluator belongs to the runtime process state, not to
// runner.cpp; narrow test fixtures may provide it without linking the runner.
extern __thread EvalContext* context;

// An evaluator thread acquires one context identity before execution and keeps
// it until teardown. Nested callbacks and guest dispatch may validate the
// owner, but must never replace and later restore the TLS pointer.
bool eval_context_thread_initialize(EvalContext* owner);
bool eval_context_thread_matches(const EvalContext* owner);
bool eval_context_thread_shutdown(EvalContext* owner);
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern bool g_dry_run;

// Context-module state is established before a sealed MIR module is entered.
// These lifecycle calls may allocate/register roots; generated code never
// calls them from a repeated variable or inline-cache path.
struct LambdaModuleState;
bool lambda_module_state_prepare(uint32_t module_id, uint32_t var_count,
                                 uint32_t member_ic_count);
bool lambda_module_state_reserve(uint32_t var_count, uint32_t member_ic_count,
                                 uint32_t* out_module_id);
bool lambda_active_js_module_state_ensure_vars(uint32_t required_var_count);
bool lambda_module_state_bind_static(uint32_t module_id, void* consts,
                                     void* type_list);
// MIR-imported helpers obtain their owner from TLS.  `Context*` remains an
// ABI parameter only between generated MIR functions.
Context* eval_context_tls_runtime(void);
void* lambda_module_const_at(const struct LambdaModuleLayout* layout,
                             uint32_t index);
void lambda_module_var_store(void* module_state, uint32_t slot, Item item);
void lambda_module_state_reset(void);
void lambda_module_state_destroy(void);

#ifdef __cplusplus
}
#endif
