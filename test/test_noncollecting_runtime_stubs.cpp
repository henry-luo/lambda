// shared runtime hooks for focused tests that do not run a collector.

#include "../lambda/lambda-data.hpp"
#include "../lambda/runtime/side_stack.h"

__thread EvalContext* context = nullptr;

// render_map's retransform branch is linked into focused tests but is not
// exercised; keep its runtime-only dependencies unavailable in these fixtures.
void expand_list(List*, Arena*) {}

extern "C" Context* eval_context_tls_runtime(void) {
    return (Context*)context;
}

extern "C" bool lambda_root_frame_begin(LambdaRootFrame*, size_t) {
    return false;
}

extern "C" uint64_t* lambda_root_frame_take_slot(LambdaRootFrame*) {
    return nullptr;
}

extern "C" void lambda_root_frame_end(LambdaRootFrame*) {}

extern "C" void lambda_side_stack_restore(LambdaSideStackSnapshot) {
    // focused tests have no bound runtime or side-stack reservation.
}

extern "C" void lambda_root_frame_overflow_error(void) {}
