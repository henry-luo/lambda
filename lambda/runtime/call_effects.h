#ifndef LAMBDA_CALL_EFFECTS_H
#define LAMBDA_CALL_EFFECTS_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// S12.1.6: an absent procedure clause is unrestricted. An explicit bound
// currently contains one or more of these named effects.
typedef uint32_t LambdaEffectMask;
enum {
    LAMBDA_EFFECT_RANDOM = 1u << 0,
    LAMBDA_EFFECT_CLOCK = 1u << 1,
    LAMBDA_EFFECT_PRINT = 1u << 2,
    LAMBDA_EFFECT_URL_INPUT = 1u << 3,
    LAMBDA_EFFECT_GENERATIVE = LAMBDA_EFFECT_RANDOM | LAMBDA_EFFECT_CLOCK | LAMBDA_EFFECT_PRINT,
    LAMBDA_EFFECT_RENDER = LAMBDA_EFFECT_GENERATIVE | LAMBDA_EFFECT_URL_INPUT,
};
#define LAMBDA_EFFECT_UNRESTRICTED UINT32_MAX

static inline const char* lambda_effect_name(LambdaEffectMask effect) {
    switch (effect) {
    case LAMBDA_EFFECT_RANDOM: return "random";
    case LAMBDA_EFFECT_CLOCK: return "clock";
    case LAMBDA_EFFECT_PRINT: return "print";
    default: return NULL;
    }
}

static inline LambdaEffectMask lambda_effect_named(const char* name, size_t length) {
    for (LambdaEffectMask bit = 1; bit <= LAMBDA_EFFECT_GENERATIVE; bit <<= 1) {
        const char* spelling = lambda_effect_name(bit);
        if (spelling && strlen(spelling) == length && !memcmp(spelling, name, length))
            return bit;
    }
    return 0;
}

// The call guard has a 16-bit argument mask and a separate callee bit.
#define LAMBDA_EFFECT_GUARD_SHIFT 16
#define LAMBDA_EFFECT_GUARD_CAPACITY 0x1FFFu
#define LAMBDA_EFFECT_GUARD_RESOURCES (1u << 29)
#define LAMBDA_EFFECT_GUARD_DEFAULTS (1u << 30)

static inline uint32_t lambda_effect_guard_pack(LambdaEffectMask allowed) {
    return (allowed & LAMBDA_EFFECT_GUARD_CAPACITY) << LAMBDA_EFFECT_GUARD_SHIFT;
}

static inline LambdaEffectMask lambda_effect_guard_allowed(uint32_t guard) {
    return (guard >> LAMBDA_EFFECT_GUARD_SHIFT) & LAMBDA_EFFECT_GUARD_CAPACITY;
}

static inline bool lambda_procedure_effects_fit(LambdaEffectMask bound,
        LambdaEffectMask allowed) {
    return bound && !(bound & ~allowed);
}

#endif
