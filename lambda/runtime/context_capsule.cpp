// context_capsule.cpp - the per-context capsule directory (JSCU15).
// See context_capsule.h for the contract.
#include "../lambda-data.hpp"
#include "context_capsule.h"
#include "../../lib/memtrack.h"
#include "../../lib/log.h"
#include <string.h>

static bool context_capsule_id_valid(EvalContext* owner, ContextCapsuleId id) {
    return owner && (int)id >= 0 && (int)id < CONTEXT_CAPSULE_COUNT;
}

extern "C" void* context_capsule(EvalContext* owner, ContextCapsuleId id) {
    if (!context_capsule_id_valid(owner, id)) return NULL;
    return owner->capsules[id];
}

extern "C" void* context_capsule_ensure(EvalContext* owner, ContextCapsuleId id,
                                        const ContextCapsuleOps* ops) {
    if (!context_capsule_id_valid(owner, id) || !ops) return NULL;
    if (owner->capsules[id]) return owner->capsules[id];
    void* capsule = ops->construct ? ops->construct(owner)
        : (ops->size ? mem_calloc(1, ops->size, MEM_CAT_EVAL) : NULL);
    if (!capsule) {
        log_error("context-capsule: %s failed to construct", ops->name);
        return NULL;
    }
    // Publish the ops with the slot: the lifecycle walks reach a subsystem
    // only through what its own construction recorded.
    owner->capsule_ops[id] = ops;
    owner->capsules[id] = capsule;
    return capsule;
}

extern "C" void context_capsule_drop(EvalContext* owner, ContextCapsuleId id) {
    if (!context_capsule_id_valid(owner, id)) return;
    void* capsule = owner->capsules[id];
    if (!capsule) return;
    const ContextCapsuleOps* ops = owner->capsule_ops[id];
    // Clear the slot first: a subsystem teardown that re-enters its own
    // accessor must not observe a capsule that is being freed.
    owner->capsules[id] = NULL;
    owner->capsule_ops[id] = NULL;
    if (ops && ops->destroy) ops->destroy(capsule);
    else mem_free(capsule);
}

extern "C" void context_capsule_release_heap(EvalContext* owner) {
    if (!owner) return;
    for (int i = CONTEXT_CAPSULE_COUNT - 1; i >= 0; i--) {
        void* capsule = owner->capsules[i];
        const ContextCapsuleOps* ops = owner->capsule_ops[i];
        if (!capsule || !ops || ops->lifetime != CONTEXT_CAPSULE_LIFETIME_REALM) continue;
        if (ops->release_heap) ops->release_heap(capsule);
    }
}

extern "C" void context_capsule_destroy_all(EvalContext* owner) {
    if (!owner) return;
    for (int i = CONTEXT_CAPSULE_COUNT - 1; i >= 0; i--) {
        context_capsule_drop(owner, (ContextCapsuleId)i);
    }
}
