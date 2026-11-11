#pragma once

#include "../transpiler.hpp"

#ifndef LAMBDA_NO_MIR
#include "../mir_module.hpp"
void lambda_mvp_compile_script(Transpiler* tp, Script* script, const char* script_path,
        double* out_jit_init_ms = nullptr,
        double* out_transpile_ms = nullptr,
        double* out_mir_gen_ms = nullptr,
        uint64_t* out_mir_module_count = nullptr,
        uint64_t* out_mir_function_count = nullptr,
        uint64_t* out_mir_instruction_count = nullptr);

// compile one T0-supported definition into a Script-owned MIR satellite.
// Its module bindings are read from the existing T0 slab; the resulting
// address has the normal generated boxed ABI.
bool lambda_mvp_compile_function(Runtime* runtime, Script* script,
        const AstFuncNode* fn, void** out_boxed_entry);

// builds a private image without mutating the running Script (D8.3.4).
bool lambda_mvp_compile_function_snapshot(Runtime* runtime, Script* script,
        const AstFuncNode* fn, uint32_t sequence,
        InterpSatelliteCancelProbe cancel_probe, void* cancel_context,
        InterpSatelliteImage** out_image);

struct LambdaMvpModule;
struct LambdaMvpStats {
    uint64_t functions;
    uint64_t instructions;
    uint64_t largest_function;
};

LambdaMvpModule* lambda_mvp_create(Transpiler* tp, MIR_context_t ctx);
bool lambda_mvp_plan(LambdaMvpModule* module);
bool lambda_mvp_lower(LambdaMvpModule* module);
bool lambda_mvp_finalize(LambdaMvpModule* module, LambdaMvpStats* stats, ArrayList** property_keys);
void lambda_mvp_destroy(LambdaMvpModule* module);
bool lambda_mvp_compile_satellite(Transpiler* tp, MIR_context_t ctx,
    const MirModuleNames* names, MirModuleArtifacts* artifacts, ArrayList** property_keys);
#endif

