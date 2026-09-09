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
    CONTEXT_CAPSULE_DOM_PLATFORM,
    // Context-wide core services which formerly lived as direct EvalContext
    // pointers. They share the same directory/lifecycle as DOM capsules.
    CONTEXT_CAPSULE_RENDER_MAP,
    CONTEXT_CAPSULE_TEMPLATE_STATE,
    CONTEXT_CAPSULE_NODE_RUNTIME,
    CONTEXT_CAPSULE_JS_RUNTIME,
    CONTEXT_CAPSULE_COUNT
} ContextCapsuleId;

struct ContextCapsuleOps;

// Optional subsystems can register their own state keys without reserving a
// permanent EvalContext slot. The owner key keeps independently versioned
// module families from colliding in the shared directory.
typedef struct ContextCapsuleExtension {
    uint32_t owner_key;
    uint32_t slot_key;
    void* capsule;
    const struct ContextCapsuleOps* ops;
    struct ContextCapsuleExtension* next;
} ContextCapsuleExtension;

// A capsule slot and its lifecycle contract are one directory entry. Keeping
// the two facts in one record prevents a context owner from publishing state
// without its teardown authority (D5.4.2, D5.4.4).
typedef struct ContextCapsuleSlot {
    void* capsule;
    const struct ContextCapsuleOps* ops;
} ContextCapsuleSlot;

typedef struct ContextCapsuleDirectory {
    ContextCapsuleSlot slots[CONTEXT_CAPSULE_COUNT];
    ContextCapsuleExtension* extensions;
} ContextCapsuleDirectory;

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
// Publishes storage constructed by a subsystem that needs a non-default
// allocator. A slot may be installed exactly once before it is dropped.
bool context_capsule_install(EvalContext* owner, ContextCapsuleId id,
                             void* capsule, const ContextCapsuleOps* ops);
// Removes a slot without destroying it. Teardown code uses this when the
// capsule itself must remain available through a multi-subsystem shutdown.
void* context_capsule_take(EvalContext* owner, ContextCapsuleId id);
// Drops one capsule (subsystem teardown, then the slot).
void context_capsule_drop(EvalContext* owner, ContextCapsuleId id);
// Walks realm-lifetime capsules before their heap goes away.
void context_capsule_release_heap(EvalContext* owner);
// Walks every capsule at context teardown, in reverse registration order.
void context_capsule_destroy_all(EvalContext* owner);

// Dynamic directory entries for externally registered subsystem slots.
void* context_capsule_extension(EvalContext* owner, uint32_t owner_key,
                                uint32_t slot_key);
bool context_capsule_extension_install(EvalContext* owner, uint32_t owner_key,
                                       uint32_t slot_key, void* capsule,
                                       const ContextCapsuleOps* ops);
void context_capsule_extensions_drop_owner(EvalContext* owner, uint32_t owner_key);

#ifdef __cplusplus
}
#endif
