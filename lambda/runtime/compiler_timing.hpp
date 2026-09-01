#pragma once

// Phase-0 compiler observability for the AST consolidation plan. The record is
// deliberately a plain value type so batch protocols can copy it without
// borrowing allocator-owned state.

#include <stdint.h>

typedef struct LambdaCompilerTiming {
    uint64_t parse_us;
    uint64_t ast_build_us;
    uint64_t bind_us;
    uint64_t validate_us;
    uint64_t index_us;
    uint64_t analysis_us;
    uint64_t plan_us;
    uint64_t mir_lower_us;
    uint64_t emitter_finalize_us;
    uint64_t module_finalize_us;
    uint64_t link_us;
    uint64_t build_transpile_us;
    uint64_t execute_us;
    uint64_t cleanup_us;
    uint64_t mir_module_count;
    uint64_t mir_function_count;
    uint64_t mir_insn_count;
    int valid;
} LambdaCompilerTiming;

typedef enum CompilerFactBits {
    COMPILER_FACT_NONE = 0,
    COMPILER_FACT_AST = 1u << 0,
    COMPILER_FACT_BOUND = 1u << 1,
    COMPILER_FACT_VALIDATED = 1u << 2,
    COMPILER_FACT_FRONTEND = COMPILER_FACT_AST | COMPILER_FACT_BOUND | COMPILER_FACT_VALIDATED,
    COMPILER_FACT_INDEXED = 1u << 3,
    COMPILER_FACT_ANALYZED = 1u << 4,
    COMPILER_FACT_PLANNED = 1u << 5,
    COMPILER_FACT_MIR_LOWERED = 1u << 6,
    COMPILER_FACT_FINALIZED = 1u << 7,
    COMPILER_FACT_PRELINKED = 1u << 8,
    COMPILER_FACT_LINKED = 1u << 9,
} CompilerFactBits;

typedef int (*CompilerPassRun)(void* context);
typedef struct CompilerPassSpec {
    const char* name;
    uint32_t required_facts;
    uint32_t produced_facts;
    CompilerPassRun run;
    void* context; // pass-owned state; NULL uses the manager caller context.
} CompilerPassSpec;

typedef struct CompilerPassManager {
    uint32_t facts;
    CompilerPassSpec passes[16];
    uint32_t pass_count;
    uint32_t next_pass; // first pass whose facts are not yet published
} CompilerPassManager;

#ifdef __cplusplus
extern "C" {
#endif
void lambda_compiler_timing_reset(void);
void lambda_compiler_timing_get(LambdaCompilerTiming* out);
int lambda_compiler_timing_enabled(void);
void compiler_pass_manager_init(CompilerPassManager* manager, uint32_t initial_facts);
int compiler_pass_manager_add(CompilerPassManager* manager, const CompilerPassSpec* pass);
int compiler_pass_manager_run(CompilerPassManager* manager, void* context);
#ifdef __cplusplus
}
#endif
