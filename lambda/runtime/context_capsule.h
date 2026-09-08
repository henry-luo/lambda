#pragma once

// Context capsules: one directory and one lifecycle for every subsystem's
// context-owned state (D5.4.2, D5.4.3; JSCU15).
//
// `EvalContext` holds a small array of slots indexed by a compile-time id.
// Reading a capsule is one indexed load; construction is the only cold path.
// Each subsystem owns its own immutable `ContextCapsuleOps` next to its
// implementation and passes it to `context_capsule_ensure`, so the runtime
// never names a subsystem symbol -- the ops records stay frozen and
// process-global (D5.4.4) without a central table that would force every
// linked target to carry every subsystem.
//
// Lifecycle is a table walk, not a hand-maintained fan-out: `release_heap`
// runs for realm-lifetime capsules when the heap is replaced, `destroy` when
// the context goes away. Adding a subsystem never edits an unrelated list.

#include "../lambda.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EvalContext EvalContext;

typedef enum ContextCapsuleLifetime {
    // Survives heap replacement; released only at context destruction.
    CONTEXT_CAPSULE_LIFETIME_CONTEXT = 0,
    // Holds heap-derived state: released when the heap is replaced.
    CONTEXT_CAPSULE_LIFETIME_REALM = 1,
} ContextCapsuleLifetime;

typedef enum ContextCapsuleId {
    // Web/DOM state (JSCU17). These are peers of the JS capsule, not children
    // of it: the DOM core is realm-neutral and is driven by Lambda as well as
    // by JS, so its storage belongs to the context that owns the document.
    CONTEXT_CAPSULE_DOM_EVENT = 0,
    CONTEXT_CAPSULE_DOM_OBSERVER,
    CONTEXT_CAPSULE_DOM_XHR,
    CONTEXT_CAPSULE_DOM_HISTORY,
    CONTEXT_CAPSULE_DOM_COLLECTION,
    CONTEXT_CAPSULE_DOM_FOREIGN_DOCUMENT,
    CONTEXT_CAPSULE_DOM_FETCH,
    CONTEXT_CAPSULE_DOM_CANVAS,
    CONTEXT_CAPSULE_COUNT
} ContextCapsuleId;

typedef struct ContextCapsuleOps {
    const char* name;
    ContextCapsuleLifetime lifetime;
    // Default construction: a zeroed block of this size. Ignored when
    // `construct` is supplied.
    size_t size;
    void* (*construct)(EvalContext* owner);
    // Optional. Runs while the owning heap is still valid.
    void (*release_heap)(void* capsule);
    // Optional subsystem teardown; it owns freeing the capsule block. When
    // absent the directory frees the default-constructed block itself.
    void (*destroy)(void* capsule);
} ContextCapsuleOps;

// Hot path: the slot as it stands, without constructing. NULL when absent.
void* context_capsule(EvalContext* owner, ContextCapsuleId id);
// Cold path: construct on first use. Returns NULL only on allocation failure.
void* context_capsule_ensure(EvalContext* owner, ContextCapsuleId id,
                             const ContextCapsuleOps* ops);
// Drops one capsule (subsystem teardown, then the slot).
void context_capsule_drop(EvalContext* owner, ContextCapsuleId id);
// Walks realm-lifetime capsules before their heap goes away.
void context_capsule_release_heap(EvalContext* owner);
// Walks every capsule at context teardown, in reverse registration order.
void context_capsule_destroy_all(EvalContext* owner);

#ifdef __cplusplus
}
#endif
