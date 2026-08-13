#pragma once

// Shared MIR mode-selection policy for Lambda and LambdaJS. Keeping the
// thresholds and environment parsing here prevents the two frontends from
// drifting on the cold-module performance contract.

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define MIR_LARGE_MODULE_INSN_THRESHOLD 100000ULL
#define MIR_RADIANT_INTERP_INSN_THRESHOLD 20000ULL
#define MIR_LARGE_SOURCE_INTERP_BYTES_DEFAULT 15000U

static inline bool mir_large_interp_enabled(void) {
    const char* flag = getenv("LAMBDA_JS_LARGE_INTERP");
    return !flag || (strcmp(flag, "0") != 0 && strcmp(flag, "false") != 0);
}

static inline size_t mir_large_source_interp_threshold(void) {
    const char* value = getenv("LAMBDA_JS_LARGE_INTERP_BYTES");
    if (!value || !value[0]) return MIR_LARGE_SOURCE_INTERP_BYTES_DEFAULT;
    char* end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || parsed <= 0) return MIR_LARGE_SOURCE_INTERP_BYTES_DEFAULT;
    return (size_t)parsed;
}
