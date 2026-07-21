#pragma once

// Active runtime process state. This declaration deliberately lives outside
// frozen lambda.h so consumers of the native/MIR-direct runtime have a
// provider-owned surface.
#ifdef __cplusplus
struct EvalContext;

// The current active evaluator belongs to the runtime process state, not to
// runner.cpp; narrow test fixtures may provide it without linking the runner.
extern __thread EvalContext* context;
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern bool g_dry_run;

#ifdef __cplusplus
}
#endif
