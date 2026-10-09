#pragma once
#include "../../lambda-data.hpp"

struct MvpLmdExecution;
struct MvpLmdHost {
    int argc;
    const char* const* argv;
    FILE* output;
};

// the execution owns its AST, MIR, heap and result until destroy (D5.3).
// optional timing covers only the generated entry, excluding compile/setup and result publication.
MvpLmdExecution* mvp_lmd_execute(const char* source, size_t length, double* execution_ms = NULL,
    const MvpLmdHost* host = NULL);
Item mvp_lmd_result(const MvpLmdExecution* execution);
const char* mvp_lmd_diagnostic(const MvpLmdExecution* execution);
void mvp_lmd_destroy(MvpLmdExecution* execution);
void mvp_lmd_dump(const MvpLmdExecution* execution, FILE* output);
