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

// TLS identifies the context executing on this thread; it never owns runtime
// state.  Enter/leave is intentionally explicit so nested guests and callback
// dispatch can restore their caller without publishing mutable state globally.
EvalContext* eval_context_bind(EvalContext* next);
void eval_context_restore(EvalContext* previous);

class EvalContextScope {
public:
    explicit EvalContextScope(EvalContext* next)
        : previous(eval_context_bind(next)) {}
    ~EvalContextScope() { eval_context_restore(previous); }

    EvalContextScope(const EvalContextScope&) = delete;
    EvalContextScope& operator=(const EvalContextScope&) = delete;

private:
    EvalContext* previous;
};
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern bool g_dry_run;

// Context-module state is established before a sealed MIR module is entered.
// These lifecycle calls may allocate/register roots; generated code never
// calls them from a repeated variable or inline-cache path.
struct LambdaModuleState;
bool lambda_module_state_prepare(Context* runtime, uint32_t module_id,
                                 uint32_t var_count, uint32_t member_ic_count);
bool lambda_module_state_reserve(Context* runtime, uint32_t var_count,
                                 uint32_t member_ic_count, uint32_t* out_module_id);
bool lambda_active_js_module_state_ensure_vars(Context* runtime,
                                               uint32_t required_var_count);
bool lambda_module_state_bind_static(Context* runtime, uint32_t module_id,
                                     void* consts, void* type_list);
void* lambda_module_const_at(Context* runtime,
                             const struct LambdaModuleLayout* layout,
                             uint32_t index);
void lambda_module_var_store(void* module_state, uint32_t slot, Item item);
void lambda_module_state_reset(Context* runtime);
void lambda_module_state_destroy(Context* runtime);

#ifdef __cplusplus
}
#endif
