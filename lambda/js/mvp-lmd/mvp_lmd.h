#pragma once

#include "../../lambda-data.hpp"

struct MvpLmdExecution;
struct MvpLmdCode;
struct Runtime;
struct JsPreambleState;
void mvp_lmd_runtime_reset();
void mvp_lmd_code_retain(MvpLmdCode* code);
void mvp_lmd_code_release(MvpLmdCode* code);
size_t mvp_lmd_code_bytes(const MvpLmdCode* code);
Item mvp_lmd_compile_script_image(const char* source, size_t length,
    const char* filename, JsPreambleState* output);
Item mvp_lmd_execute_script_image(Runtime* runtime, const MvpLmdCode* code);
enum MvpLmdFailurePhase { MVP_LMD_SUCCESS, MVP_LMD_PARSE_FAILURE,
    MVP_LMD_ADMISSION_FAILURE, MVP_LMD_RESOLUTION_FAILURE, MVP_LMD_RUNTIME_FAILURE, MVP_LMD_INTERNAL_FAILURE };
struct MvpLmdHost {
    int argc;
    const char* const* argv;
    FILE* output;
    bool test262 = false;       // require the real $DONE completion flag for async jobs
};

struct MvpLmdOptions {
    const char* filename;
    bool module;
    const char* preamble = NULL;
    size_t preamble_length = 0;
    bool namespace_result = false; // public module loaders consume the live export namespace
    const MvpLmdCode* cached_code = NULL;
};
MvpLmdExecution* mvp_lmd_execute_with_options(const char* source, size_t length,
    double* execution_ms, const MvpLmdHost* host, const MvpLmdOptions* options);
// formal MIR entry: the Runtime retains code and the result's original heap.
Item mvp_lmd_execute_in_runtime(Runtime* runtime, const char* source, size_t length,
    const char* filename, uint64_t* result_home, bool module = false,
    const MvpLmdCode* cached_code = NULL);

// the execution owns its AST, MIR, heap and result until destroy (D5.3).
// optional timing covers only the generated entry, excluding compile/setup and result publication.
MvpLmdExecution* mvp_lmd_execute(const char* source, size_t length, double* execution_ms = NULL,
    const MvpLmdHost* host = NULL);
Item mvp_lmd_result(const MvpLmdExecution* execution);
const char* mvp_lmd_diagnostic(const MvpLmdExecution* execution,
    MvpLmdFailurePhase* phase = NULL, const char** error_type = NULL);
void mvp_lmd_destroy(MvpLmdExecution* execution);
void mvp_lmd_dump(const MvpLmdExecution* execution, FILE* output);
