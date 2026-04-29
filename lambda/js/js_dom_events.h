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
 * Create a CustomEvent object with a detail property.
 */
Item js_create_custom_event(const char* type, bool bubbles, bool cancelable, Item detail);

/**
 * Create a CustomEvent with full EventInit (composed flag) plus detail.
 */
Item js_create_custom_event_init(const char* type, bool bubbles, bool cancelable,
                                 bool composed, Item detail);

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
