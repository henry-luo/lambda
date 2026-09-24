#pragma once

// Internal, GC-owned lexical environment used by the AST interpreter. This
// stays C-compatible because the collector traces the raw record directly.

#include <stdint.h>
#include "../runtime/gc_environment.h"

struct NameScope;
struct JsScript;

typedef struct JsInterpEnv {
    struct JsInterpEnv* outer;
    struct NameScope* scope;
    // The function activation owns its materialized exotic arguments object.
    // Arrows leave this empty and resolve the nearest outer function record.
    uint64_t arguments_object;
    // Class evaluation publishes this lexical private-name owner for member
    // bodies, field initializers, static blocks, and their nested closures.
    uint64_t private_home_class;
    // Flat [source name, identity key] pairs exported to a direct-eval bridge.
    uint64_t private_bindings;
    // Flat [name, value] pairs declared by direct eval in this variable
    // environment. Closures retain this GC-owned record after the caller exits.
    uint64_t eval_bindings;
    // The nearest non-arrow function's live lexical `this` binding. Derived
    // constructors initialize this cell only after their `super()` call.
    uint64_t lexical_this;
    struct AstNode* function_node;
    // Set on the declarative record of a direct eval linked to an interpreted
    // caller (D8.1.3v21): its slots hold that eval Script's top-level
    // declarations, and `outer` continues into the caller frame's environments.
    struct JsScript* eval_script;
    uint32_t slot_count;
    uint8_t arguments_are_mapped;
    uint8_t has_lexical_this;
    uint8_t reserved[2];
    uint64_t slots[1];
} JsInterpEnv;

// JS adds lexical metadata to the common durable Item/scalar slot storage.
// The five Item words are deliberately contiguous so the generic visitor can
// trace them with the same contract it uses for ordinary capture slots.
static inline void js_interp_env_storage(JsInterpEnv* env,
        GcEnvironmentStorage* storage) {
    if (!storage) return;
    if (!env) {
        // keep the common access helpers safe for absent lexical parents.
        gc_environment_storage_init(storage, GC_ENVIRONMENT_LAYOUT_LEXICAL,
            NULL, NULL, 0, NULL, NULL, 0);
        return;
    }
    Item* slots = (Item*)(void*)env->slots;
    gc_environment_storage_init(storage, GC_ENVIRONMENT_LAYOUT_LEXICAL, slots,
        env->slots + env->slot_count, env->slot_count, env->outer,
        (Item*)(void*)&env->arguments_object, 5);
}

static inline Item js_interp_env_slot_read(JsInterpEnv* env, uint32_t slot,
        bool immortal) {
    GcEnvironmentStorage storage;
    js_interp_env_storage(env, &storage);
    return gc_environment_storage_read(&storage, slot, immortal);
}

static inline void js_interp_env_slot_store(JsInterpEnv* env, uint32_t slot,
        Item value) {
    GcEnvironmentStorage storage;
    js_interp_env_storage(env, &storage);
    gc_environment_storage_store(&storage, slot, value);
}
