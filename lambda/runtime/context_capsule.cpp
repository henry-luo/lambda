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

static ContextCapsuleExtension* context_capsule_extension_find(EvalContext* owner,
                                                                uint32_t owner_key,
                                                                uint32_t slot_key) {
    if (!owner) return NULL;
    for (ContextCapsuleExtension* entry = owner->capsule_directory.extensions;
            entry; entry = entry->next) {
        if (entry->owner_key == owner_key && entry->slot_key == slot_key) return entry;
    }
    return NULL;
}

static void context_capsule_extension_destroy(ContextCapsuleExtension* entry) {
    if (!entry) return;
    if (entry->ops && entry->ops->destroy) entry->ops->destroy(entry->capsule);
    else mem_free(entry->capsule);
    mem_free(entry);
}

extern "C" void* context_capsule(EvalContext* owner, ContextCapsuleId id) {
    if (!context_capsule_id_valid(owner, id)) return NULL;
    return owner->capsule_directory.slots[id].capsule;
}

extern "C" void* context_capsule_ensure(EvalContext* owner, ContextCapsuleId id,
                                        const ContextCapsuleOps* ops) {
    if (!context_capsule_id_valid(owner, id) || !ops) return NULL;
    if (owner->capsule_directory.slots[id].capsule) {
        return owner->capsule_directory.slots[id].capsule;
    }
    void* capsule = ops->construct ? ops->construct(owner)
        : (ops->size ? mem_calloc(1, ops->size, MEM_CAT_EVAL) : NULL);
    if (!capsule) {
        log_error("context-capsule: %s failed to construct", ops->name);
        return NULL;
    }
    // Publish the ops with the slot: the lifecycle walks reach a subsystem
    // only through what its own construction recorded.
    owner->capsule_directory.slots[id].ops = ops;
    owner->capsule_directory.slots[id].capsule = capsule;
    return capsule;
}

extern "C" bool context_capsule_install(EvalContext* owner, ContextCapsuleId id,
                                         void* capsule, const ContextCapsuleOps* ops) {
    if (!context_capsule_id_valid(owner, id) || !capsule || !ops ||
            owner->capsule_directory.slots[id].capsule) return false;
    owner->capsule_directory.slots[id].ops = ops;
    owner->capsule_directory.slots[id].capsule = capsule;
    return true;
}

extern "C" void* context_capsule_take(EvalContext* owner, ContextCapsuleId id) {
    if (!context_capsule_id_valid(owner, id)) return NULL;
    void* capsule = owner->capsule_directory.slots[id].capsule;
    owner->capsule_directory.slots[id].capsule = NULL;
    owner->capsule_directory.slots[id].ops = NULL;
    return capsule;
}

extern "C" void context_capsule_drop(EvalContext* owner, ContextCapsuleId id) {
    if (!context_capsule_id_valid(owner, id)) return;
    void* capsule = owner->capsule_directory.slots[id].capsule;
    if (!capsule) return;
    const ContextCapsuleOps* ops = owner->capsule_directory.slots[id].ops;
    // Clear the slot first: a subsystem teardown that re-enters its own
    // accessor must not observe a capsule that is being freed.
    owner->capsule_directory.slots[id].capsule = NULL;
    owner->capsule_directory.slots[id].ops = NULL;
    if (ops && ops->destroy) ops->destroy(capsule);
    else mem_free(capsule);
}

extern "C" void context_capsule_release_heap(EvalContext* owner) {
    if (!owner) return;
    for (int i = CONTEXT_CAPSULE_COUNT - 1; i >= 0; i--) {
        void* capsule = owner->capsule_directory.slots[i].capsule;
        const ContextCapsuleOps* ops = owner->capsule_directory.slots[i].ops;
        if (!capsule || !ops || ops->lifetime != CONTEXT_CAPSULE_LIFETIME_REALM) continue;
        if (ops->release_heap) ops->release_heap(capsule);
    }
}

extern "C" void context_capsule_destroy_all(EvalContext* owner) {
    if (!owner) return;
    while (owner->capsule_directory.extensions) {
        ContextCapsuleExtension* entry = owner->capsule_directory.extensions;
        owner->capsule_directory.extensions = entry->next;
        context_capsule_extension_destroy(entry);
    }
    for (int i = CONTEXT_CAPSULE_COUNT - 1; i >= 0; i--) {
        context_capsule_drop(owner, (ContextCapsuleId)i);
    }
}

extern "C" void* context_capsule_extension(EvalContext* owner, uint32_t owner_key,
                                             uint32_t slot_key) {
    ContextCapsuleExtension* entry = context_capsule_extension_find(owner, owner_key, slot_key);
    return entry ? entry->capsule : NULL;
}

extern "C" bool context_capsule_extension_install(EvalContext* owner, uint32_t owner_key,
                                                    uint32_t slot_key, void* capsule,
                                                    const ContextCapsuleOps* ops) {
    if (!owner || !capsule || !ops ||
            context_capsule_extension_find(owner, owner_key, slot_key)) return false;
    ContextCapsuleExtension* entry =
        (ContextCapsuleExtension*)mem_calloc(1, sizeof(ContextCapsuleExtension), MEM_CAT_EVAL);
    if (!entry) return false;
    entry->owner_key = owner_key;
    entry->slot_key = slot_key;
    entry->capsule = capsule;
    entry->ops = ops;
    entry->next = owner->capsule_directory.extensions;
    owner->capsule_directory.extensions = entry;
    return true;
}

extern "C" void context_capsule_extensions_drop_owner(EvalContext* owner,
                                                        uint32_t owner_key) {
    if (!owner) return;
    ContextCapsuleExtension** link = &owner->capsule_directory.extensions;
    while (*link) {
        ContextCapsuleExtension* entry = *link;
        if (entry->owner_key != owner_key) {
            link = &entry->next;
            continue;
        }
        *link = entry->next;
        context_capsule_extension_destroy(entry);
    }
}
