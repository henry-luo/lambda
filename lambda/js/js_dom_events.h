/**
 * js_dom_events.h — DOM Event System for Lambda JS Runtime
 *
 * Implements addEventListener/removeEventListener/dispatchEvent on DOM nodes
 * with 3-phase event propagation (capture → target → bubble).
 *
 * Event listeners are stored in an external hash map keyed by DomNode pointer,
 * avoiding modifications to the DomNode struct.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

// ============================================================================
// Event Listener Management
// ============================================================================

/**
 * addEventListener(elem, type, callback, capture)
 * @param elem_item   Wrapped DOM element or document proxy
 * @param type_item   String: event type (e.g., "click", "load")
 * @param cb_item     Function: listener callback
 * @param opts_item   Boolean (useCapture) or options object {capture, once, passive}
 */
void js_dom_add_event_listener(Item elem_item, Item type_item, Item cb_item, Item opts_item);

/**
 * removeEventListener(elem, type, callback, capture)
 */
void js_dom_remove_event_listener(Item elem_item, Item type_item, Item cb_item, Item opts_item);

/**
 * dispatchEvent(elem, event) → bool
 * Dispatches event through capture → target → bubble phases.
 * @param elem_item   Target element
 * @param event_item  Event object (Map with type, bubbles, cancelable, etc.)
 * @return Boolean Item (true if not preventDefault'd)
 */
Item js_dom_dispatch_event(Item elem_item, Item event_item);

// ============================================================================
// Event Object Creation
// ============================================================================

/**
 * Create an Event object.
 * @param type     String: event type
 * @param bubbles  bool: whether the event bubbles (default: false)
 * @param cancelable bool: whether the event is cancelable (default: false)
 * @return Event object (Map with type, bubbles, cancelable, target, etc.)
 */
Item js_create_event(const char* type, bool bubbles, bool cancelable);

/**
 * Create an Event with full EventInit (composed flag set explicitly).
 */
Item js_create_event_init(const char* type, bool bubbles, bool cancelable, bool composed);

/**
 * Build a synthetic click MouseEvent for HTMLElement.prototype.click().
 * Composed=true, bubbles=true, cancelable=true, detail=1; all coordinate/
 * button/modifier fields default to 0/false per the HTML spec.
 */
Item js_create_click_mouse_event(void);

/**
 * Create a CustomEvent object with a detail property.
 */
Item js_create_custom_event(const char* type, bool bubbles, bool cancelable, Item detail);

/**
 * Create a CustomEvent with full EventInit (composed flag) plus detail.
 */
Item js_create_custom_event_init(const char* type, bool bubbles, bool cancelable,
                                 bool composed, Item detail);

// ============================================================================
// Generic EventTarget — a plain JS object that can be used as an event target
// ============================================================================

/**
 * Construct a fresh EventTarget. The returned object has addEventListener,
 * removeEventListener, and dispatchEvent methods bound such that this-receiver
 * is the storage key.
 */
Item js_create_event_target(void);

// ============================================================================
// Event subclass constructors (called by JsCtor wrappers in js_globals.cpp).
// Each takes (type, init) and returns an Event-shaped object stamped with the
// extra IDL members and `__class_name__` for instanceof.
// ============================================================================
Item js_ctor_ui_event_fn(Item type, Item init);
Item js_ctor_focus_event_fn(Item type, Item init);
Item js_ctor_mouse_event_fn(Item type, Item init);
Item js_ctor_wheel_event_fn(Item type, Item init);
Item js_ctor_keyboard_event_fn(Item type, Item init);
Item js_ctor_composition_event_fn(Item type, Item init);
Item js_ctor_input_event_fn(Item type, Item init);
Item js_ctor_pointer_event_fn(Item type, Item init);

// ============================================================================
// Native event factories (Radiant input bridge — §7 of Radiant_Design_Event.md).
// Each builds a spec-compliant Event object with isTrusted = true and
// bubbles = true so it propagates through the JS dispatcher just like a
// scripted user event. Caller passes pre-decoded modifier booleans to keep
// js_dom_events independent of radiant/event.hpp's RDT_MOD_* layout.
// ============================================================================
Item js_create_native_mouse_event(const char* type,
    int client_x, int client_y,
    int button, int buttons,
    bool ctrl, bool shift, bool alt, bool meta,
    int detail, Item related_target);

Item js_create_native_keyboard_event(const char* type,
    const char* key, const char* code,
    bool ctrl, bool shift, bool alt, bool meta,
    bool repeat);

Item js_create_native_focus_event(const char* type, Item related_target);

Item js_create_native_wheel_event(const char* type,
    int client_x, int client_y,
    double delta_x, double delta_y,
    int buttons,
    bool ctrl, bool shift, bool alt, bool meta);

/**
 * Returns true if event has been preventDefault()'d (or, for cancelable
 * legacy paths, returnValue=false). Mirrors the spec's "canceled flag".
 */
bool js_event_is_default_prevented(Item event);

// ============================================================================
// Lifecycle
// ============================================================================

/**
 * Reset event system state. Call between documents in batch mode.
 */
void js_dom_events_reset(void);

#ifdef __cplusplus
}
#endif
