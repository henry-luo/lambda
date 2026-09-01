#pragma once

#include "../lambda.h"
#include "../input/css/dom_element.hpp"

#ifdef __cplusplus
extern "C" {
#endif

Item js_mutation_observer_new(Item callback);
Item js_resize_observer_new(Item callback);
Item js_intersection_observer_new(Item callback, Item options);

void dom_observers_mutation_notify(DomJsMutationKind kind,
                                      void* target, void* parent,
                                      const char* attribute_name,
                                      const char* old_value);
void dom_observers_child_replace_notify(void* parent, void* added,
                                           void* removed);
void dom_observers_post_layout(void);
void dom_observers_reset(void);

// ============================================================================
// Host-facing entry points (F23) — see the note in dom.h
//
// The notify entries are the engine's way into the mutation ring: Radiant calls
// them after it mutates the tree, and observer delivery reads the ring. They
// live with the observers rather than in dom.h because dom.h is included from
// C and must not pull in the C++ DomJsMutationKind definition.
// ============================================================================

void dom_notify_mutation(DomJsMutationKind kind, void* target, void* parent);
void dom_notify_mutation_detail(DomJsMutationKind kind,
                                   void* target, void* parent,
                                   const char* attribute_name,
                                   const char* old_value);

#ifdef __cplusplus
struct JsRuntimeState;
void dom_observers_destroy_context(JsRuntimeState* state);
#endif

#ifdef __cplusplus
}
#endif
