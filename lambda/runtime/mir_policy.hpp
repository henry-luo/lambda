#pragma once

// Shared MIR mode-selection policy for Lambda and LambdaJS. Keeping the
// thresholds and environment parsing here prevents the two frontends from
// drifting on the cold-module performance contract.

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MIR_LARGE_MODULE_INSN_THRESHOLD 100000ULL
#define MIR_RADIANT_INTERP_INSN_THRESHOLD 20000ULL
// MIR's interpreter allocates every virtual register of an activation on the
// native stack. Minified document bundles can lower to roughly seventeen MIR
// instructions per AST node, so keep them below the unsafe activation size.
#define MIR_RADIANT_AST_NODE_THRESHOLD 25000U
// A document can import many individually-safe modules whose combined native
// compiler state is still disproportionate to page rendering.  Keep a bounded
// first tranche in MIR and run later modules through the established AST tier.
#define MIR_RADIANT_DOCUMENT_JIT_AST_NODE_BUDGET 100000U
#define MIR_LARGE_SOURCE_INTERP_BYTES_DEFAULT 15000U

static inline bool mir_large_interp_enabled(void) {
    const char* flag = getenv("LAMBDA_JS_LARGE_INTERP");
    return !flag || (strcmp(flag, "0") != 0 && strcmp(flag, "false") != 0);
}

static inline bool mir_explicit_interpreter_requested(void) {
    const char* flag = getenv("JS_MIR_INTERP");
    return flag && (strcmp(flag, "1") == 0 || strcmp(flag, "true") == 0);
}

static inline size_t mir_large_source_interp_threshold(void) {
    const char* value = getenv("LAMBDA_JS_LARGE_INTERP_BYTES");
    if (!value || !value[0]) return MIR_LARGE_SOURCE_INTERP_BYTES_DEFAULT;
    char* end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || parsed <= 0) return MIR_LARGE_SOURCE_INTERP_BYTES_DEFAULT;
    return (size_t)parsed;
}

static inline size_t mir_radiant_document_jit_ast_node_budget(void) {
    const char* value = getenv("LAMBDA_JS_DOCUMENT_JIT_AST_NODES");
    if (!value || !value[0]) return MIR_RADIANT_DOCUMENT_JIT_AST_NODE_BUDGET;
    char* end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (end == value || parsed == 0 || parsed > UINT32_MAX) {
        return MIR_RADIANT_DOCUMENT_JIT_AST_NODE_BUDGET;
    }
    return (size_t)parsed;
}
