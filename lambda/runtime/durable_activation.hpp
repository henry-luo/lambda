#pragma once

// The common durable portion of a JS generator or async activation that can
// outlive its native call. Lambda tasks suspend on stackful activations
// (activation.h) and need no durable frame of their own.

#include "../lambda.hpp"

enum DurableActivationKind : uint8_t {
    DURABLE_ACTIVATION_NONE = 0,
    DURABLE_ACTIVATION_JS_GENERATOR,
    DURABLE_ACTIVATION_JS_ASYNC,
};

struct DurableActivation {
    TypeId type_id;
    DurableActivationKind kind;
    uint8_t reserved[6];
    Item* env;
    int env_size;
    int64_t state;
};

static inline void durable_activation_init(DurableActivation* activation,
        TypeId type_id, DurableActivationKind kind) {
    if (!activation) return;
    activation->type_id = type_id;
    activation->kind = kind;
    memset(activation->reserved, 0, sizeof(activation->reserved));
    activation->env = NULL;
    activation->env_size = 0;
    activation->state = 0;
}

static inline bool durable_activation_uses_gc_environment(
        const DurableActivation* activation) {
    return activation && (activation->kind == DURABLE_ACTIVATION_JS_GENERATOR ||
        activation->kind == DURABLE_ACTIVATION_JS_ASYNC);
}

typedef void (*DurableActivationVisitEnvironmentFn)(void* context,
                                                     void* environment);

static inline void durable_activation_visit_environment(
        const DurableActivation* activation, void* context,
        DurableActivationVisitEnvironmentFn visit_environment) {
    if (!durable_activation_uses_gc_environment(activation) || !activation->env ||
            !visit_environment) return;
    visit_environment(context, activation->env);
}

static_assert(sizeof(DurableActivation) == 32,
    "DurableActivation must stay a compact common resume carrier");
