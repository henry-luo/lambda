#pragma once

#include <stdint.h>

#define MIR_LARGE_FUNCTION_O1_THRESHOLD 20000ULL
#ifdef NDEBUG
#define MIR_EAGER_FUNCTION_O1_THRESHOLD 5000ULL
#else
#define MIR_EAGER_FUNCTION_O1_THRESHOLD 1000ULL
#endif

// bound cold import lowering growth before polymorphic checks become native
// code. Callers keep the specialized path for entry code and hot satellites.
static inline bool mir_compact_lowering(uint32_t ast_nodes) {
    return ast_nodes > 256;
}

// cold code needs to start quickly. Keep the level 1 instruction-selection
// passes and avoid global value numbering at the default level.
static inline unsigned int mir_cold_optimize_level(unsigned int optimize_level) {
    return optimize_level == 2 ? 1 : optimize_level;
}

static inline unsigned int mir_native_optimize_level(
        uint64_t instruction_count, unsigned int optimize_level,
        uint64_t budget = MIR_LARGE_FUNCTION_O1_THRESHOLD) {
#ifdef NDEBUG
    const unsigned int bounded_level = 1;
#else
    const unsigned int bounded_level = 0;
#endif
    return instruction_count > budget && optimize_level > bounded_level
        ? bounded_level : optimize_level;
}

// bound the global optimization cost of one body, independent of how many
// unrelated small exports share its eagerly generated module.
static inline unsigned int mir_eager_optimize_level(
        uint64_t largest_function_insns, unsigned int optimize_level) {
    return mir_native_optimize_level(largest_function_insns, optimize_level,
        MIR_EAGER_FUNCTION_O1_THRESHOLD);
}
