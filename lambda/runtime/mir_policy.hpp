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
#define MIR_LARGE_SOURCE_INTERP_BYTES_DEFAULT 15000U
#define MIR_LARGE_FUNCTION_O1_THRESHOLD 20000ULL

static inline bool mir_large_interp_enabled(void);

typedef enum MirLinkInterface {
    MIR_LINK_NATIVE = 0,
    MIR_LINK_LAZY_NATIVE,
    MIR_LINK_INTERP,
} MirLinkInterface;

typedef struct MirLinkSelection {
    MirLinkInterface interface_kind;
    unsigned int optimize_level;
} MirLinkSelection;

// The code generator is vendored; policy selects its interface before it is
// invoked. A cold document and an oversized module use the interpreter ABI,
// while a single large native body gives up only the expensive O2 pass.
static inline MirLinkSelection mir_select_link_interface(
        uint64_t total_insns, uint64_t largest_function_insns,
        bool document_attached, unsigned int optimize_level,
        bool lazy_native_allowed) {
    MirLinkSelection selection = {lazy_native_allowed
        ? MIR_LINK_LAZY_NATIVE : MIR_LINK_NATIVE, optimize_level};
    if (mir_large_interp_enabled() &&
            (total_insns > MIR_LARGE_MODULE_INSN_THRESHOLD ||
             (document_attached &&
              total_insns > MIR_RADIANT_INTERP_INSN_THRESHOLD))) {
        selection.interface_kind = MIR_LINK_INTERP;
        return selection;
    }
    if (largest_function_insns > MIR_LARGE_FUNCTION_O1_THRESHOLD &&
            selection.optimize_level > 1) {
        selection.optimize_level = 1;
    }
    return selection;
}

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
