#pragma once

#include "../transpiler.hpp"

// the selector chooses a MIR implementation, independently of execution tier.
static inline bool lambda_mvp_selected() {
    const char* choice = getenv("LAMBDA_MIR_COMPILER");
    return choice && strcmp(choice, "mvp") == 0;
}

#ifndef LAMBDA_NO_MIR
struct LambdaMvpModule;
struct LambdaMvpStats {
    uint64_t functions;
    uint64_t instructions;
    uint64_t largest_function;
};

LambdaMvpModule* lambda_mvp_create(Transpiler* tp, MIR_context_t ctx);
bool lambda_mvp_plan(LambdaMvpModule* module);
bool lambda_mvp_lower(LambdaMvpModule* module);
bool lambda_mvp_finalize(LambdaMvpModule* module, LambdaMvpStats* stats);
void lambda_mvp_destroy(LambdaMvpModule* module);
#endif

