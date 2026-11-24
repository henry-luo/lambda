#pragma once

#include "lambda/lambda.h"
#include "lambda/input/css/dom_element.hpp"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum JsObserverKind {
    JS_OBSERVER_MUTATION,
    JS_OBSERVER_RESIZE,
    JS_OBSERVER_INTERSECTION
} JsObserverKind;

const char* dom_observer_interface_name(JsObserverKind kind);
void dom_observer_install_interface(Item prototype, JsObserverKind kind);
void dom_install_observer_globals(Item global);
Item dom_mutation_observer_new(Item callback);
Item dom_resize_observer_new(Item callback);
Item dom_intersection_observer_new(Item callback, Item options);

void dom_observers_mutation_notify(DomJsMutationKind kind,
                                      void* target, void* parent,
                                      const char* attribute_name,
                                      const char* old_value, size_t old_value_length = SIZE_MAX);
void dom_observers_child_replace_notify(void* parent, void* added,
                                           void* removed);
void dom_observers_children_replace_notify(void* parent, Item added, Item removed);
// snapshot native nodes without invoking a script iterator (D9.5.6v2 / D6.3.3).
Item dom_observers_node_snapshot(void* first, bool siblings);
void dom_observers_signal_slot_change(DomElement* slot);
void dom_observers_post_layout(void);
void dom_observers_release_document(DomDocument* document);
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
void dom_notify_inline_style_mutation(DomElement* element,
                                     CssPropertyCode property, const char* old_value);
void dom_notify_mutation_detail(DomJsMutationKind kind,
                                   void* target, void* parent,
                                   const char* attribute_name,
                                   const char* old_value);
void dom_notify_mutation_detail_n(DomJsMutationKind kind, void* target, void* parent,
                                  const char* attribute_name, const char* old_value,
                                  size_t old_value_length);
// apply proven non-style data/aria writes with ordinary observer/attribute side effects.
bool dom_set_non_style_metadata(void* target, const char* name, const char* value);
void dom_notify_scroll_position_change(void* element, float old_x, float old_y);

#ifdef __cplusplus
struct JsRuntimeState;
#endif

#ifdef __cplusplus
}
#endif
