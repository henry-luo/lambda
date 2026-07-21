// transpile_js_mir.cpp - Direct MIR generation for JavaScript
//
// J41-1 mechanical split state anchor.
// Implementation lives in js_mir_*.cpp files with shared declarations in
// js_mir_internal.hpp.

#include "js_mir_internal.hpp"

// External reference to Lambda runtime context pointer (defined in mir.c)
extern "C" Context* _lambda_rt;
extern "C" {
    void *import_resolver(const char *name);
}

// External from runner.cpp
extern __thread EvalContext* context;

// External from js_runtime.cpp
extern "C" void js_reset_module_vars();
extern "C" Item* js_alloc_module_vars(void);
extern "C" Item* js_get_active_module_vars(void);
extern "C" void js_set_active_module_vars(Item* vars);
extern "C" void js_process_emit_exit(int code);

// Global MIR error handler override for batch mode.
// If non-NULL, installed after each jit_init() to prevent exit(1) on MIR errors.
MIR_error_func_t g_batch_mir_error_handler = NULL;

// Global MIR optimization level for JS compilation (default: 2).
// Set from CLI (e.g., --opt-level=0). Preamble always uses its own level.
unsigned int g_js_mir_optimize_level = 2;

// Tune6: when set (by document-rendering CLI commands layout/render/view), JS in a
// document context links via the MIR interpreter regardless of size — but with the
// JIT generator still initialized (g_mir_interp_mode stays 0), i.e. the link-
// interface interp path, not pure-interp. See Transpile_Js_Tune6_AST.md §0.2.
int g_js_force_document_interp = 0;

// keep the newer CLI --diagnose switch linkable after rolling the JS runtime
// back to d609; the reverted runtime ignores the flag unless diagnose probes
// are reintroduced later.
static int g_js_diagnose_enabled = 0;

extern "C" void js_set_diagnose_enabled(int enabled) {
    g_js_diagnose_enabled = enabled ? 1 : 0;
}

extern "C" int js_is_diagnose_enabled(void) {
    return g_js_diagnose_enabled;
}

// Adaptive gen interface: large functions (>N insns) compile at opt=1 to avoid
// O(n²) SSA/GVN cost, while small functions get full optimization.
// Threshold: 10K MIR insns → functions above this use opt=1.
#define JM_LARGE_FUNC_INSN_THRESHOLD 10000

// Threshold for total MIR instructions in a module. Modules above this
// (e.g., lodash with 272K insns) use opt=0 for the entire context because
// MIR's SSA/GVN passes have super-linear cost for very large functions.
#define JM_LARGE_MODULE_INSN_THRESHOLD 100000



// POC: MIR interpreter mode — set from mir.c
extern "C" int g_mir_interp_mode;

// External declarations for parallel module compilation
extern "C" {
    const TSLanguage* tree_sitter_typescript(void);
    const TSLanguage* tree_sitter_javascript(void);
    void ensure_jit_imports_initialized(void);
}
