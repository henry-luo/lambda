/**
 * js_dom_events.cpp — DOM Event System for Lambda JS Runtime
 *
 * Implements the EventTarget interface (addEventListener, removeEventListener,
 * dispatchEvent) with full 3-phase propagation (capture → target → bubble).
 *
 * Listener storage: simple flat array of {key, listeners} entries keyed by
 * DomNode pointer. Avoids modifying the DomNode struct.
 */

#include "js_dom_events.h"
#include "js_dom.h"
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../input/css/dom_node.hpp"
#include "../input/css/dom_element.hpp"

#include <cstring>
#include <ctime>

// Forward decls used by Event helpers below (signatures from js_runtime.h /
// js_dom.h, declared here under extern "C" to avoid header coupling).
extern "C" Item js_get_this();
extern Item js_array_new(int length);
extern Item js_array_push(Item array, Item value);
extern Item js_get_document_object_value();
static inline Item event_make_double(double v) {
    double* p = (double*)heap_calloc(sizeof(double), LMD_TYPE_FLOAT);
    *p = v;
    return (Item){.item = d2it(p)};
}

// Monotonic ms since first call (acts as a per-process performance origin).
static double event_now_ms() {
    struct timespec ts;
#if defined(__APPLE__) || defined(__linux__)
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    timespec_get(&ts, TIME_UTC);
#endif
    static double origin_ms = 0.0;
    double now = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
    if (origin_ms == 0.0) origin_ms = now - 0.001;
    return now - origin_ms;
}

static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

// ============================================================================
// Event listener storage
// ============================================================================

struct EventListener {
    char*   type;       // event type string (owned copy)
    Item    callback;   // JS function item
    bool    capture;    // capture phase listener
    bool    once;       // remove after first invocation
    bool    passive;    // passive listener (cannot preventDefault)
};

// per-node listener list
struct NodeListeners {
    EventListener* items;
    int count;
    int capacity;
};

// flat array mapping void* keys → NodeListeners
struct NodeListenerEntry {
    void* key;
    NodeListeners listeners;
};

static NodeListenerEntry* _entries = nullptr;
static int _entry_count = 0;
static int _entry_capacity = 0;

// sentinel pointers for non-element targets
static int _window_sentinel = 0;
static int _document_sentinel = 0;

// get the key pointer for a target item
static void* get_event_target_key(Item target) {
    // check for document proxy
    if (js_is_document_proxy(target)) {
        return (void*)&_document_sentinel;
    }
    // check for DOM node
    void* node = js_dom_unwrap_element(target);
    if (node) return node;
    // fallback: treat as window
    return (void*)&_window_sentinel;
}

static NodeListeners* get_or_create_listeners(void* key) {
    // search existing entries
    for (int i = 0; i < _entry_count; i++) {
        if (_entries[i].key == key) {
            return &_entries[i].listeners;
        }
    }

    // grow if needed
    if (_entry_count >= _entry_capacity) {
        int new_cap = _entry_capacity == 0 ? 16 : _entry_capacity * 2;
        NodeListenerEntry* new_entries = (NodeListenerEntry*)mem_calloc(new_cap, sizeof(NodeListenerEntry), MEM_CAT_JS_RUNTIME);
        if (_entries && _entry_count > 0) {
            memcpy(new_entries, _entries, _entry_count * sizeof(NodeListenerEntry));
            mem_free(_entries);
        }
        _entries = new_entries;
        _entry_capacity = new_cap;
    }

    NodeListenerEntry* entry = &_entries[_entry_count++];
    entry->key = key;
    entry->listeners.items = nullptr;
    entry->listeners.count = 0;
    entry->listeners.capacity = 0;
    return &entry->listeners;
}

static NodeListeners* find_listeners(void* key) {
    for (int i = 0; i < _entry_count; i++) {
        if (_entries[i].key == key) {
            return &_entries[i].listeners;
        }
    }
    return nullptr;
}

static void nl_push(NodeListeners* nl, EventListener listener) {
    if (nl->count >= nl->capacity) {
        int new_cap = nl->capacity == 0 ? 8 : nl->capacity * 2;
        EventListener* new_items = (EventListener*)mem_calloc(new_cap, sizeof(EventListener), MEM_CAT_JS_RUNTIME);
        if (nl->items && nl->count > 0) {
            memcpy(new_items, nl->items, nl->count * sizeof(EventListener));
            mem_free(nl->items);
        }
        nl->items = new_items;
        nl->capacity = new_cap;
    }
    nl->items[nl->count++] = listener;
}

// ============================================================================
// Parse options argument
// ============================================================================

static void parse_listener_options(Item opts, bool* capture, bool* once, bool* passive) {
    *capture = false;
    *once = false;
    *passive = false;

    if (opts.item == 0 || get_type_id(opts) == LMD_TYPE_NULL ||
        get_type_id(opts) == LMD_TYPE_UNDEFINED) {
        return;
    }

    // boolean argument = useCapture
    TypeId tid = get_type_id(opts);
    if (tid == LMD_TYPE_BOOL) {
        *capture = js_is_truthy(opts);
        return;
    }

    // options object: {capture, once, passive}
    if (tid == LMD_TYPE_MAP) {
        Item cap_key = (Item){.item = s2it(heap_create_name("capture"))};
        Item once_key = (Item){.item = s2it(heap_create_name("once"))};
        Item passive_key = (Item){.item = s2it(heap_create_name("passive"))};

        Item cap_val = js_property_get(opts, cap_key);
        Item once_val = js_property_get(opts, once_key);
        Item passive_val = js_property_get(opts, passive_key);

        if (cap_val.item != 0 && get_type_id(cap_val) != LMD_TYPE_UNDEFINED)
            *capture = js_is_truthy(cap_val);
        if (once_val.item != 0 && get_type_id(once_val) != LMD_TYPE_UNDEFINED)
            *once = js_is_truthy(once_val);
        if (passive_val.item != 0 && get_type_id(passive_val) != LMD_TYPE_UNDEFINED)
            *passive = js_is_truthy(passive_val);
    }
}

// ============================================================================
// addEventListener / removeEventListener
// ============================================================================

void js_dom_add_event_listener(Item elem_item, Item type_item, Item cb_item, Item opts_item) {    const char* type = fn_to_cstr(type_item);
    if (!type) {
        log_debug("js_dom_add_event_listener: invalid type");
        return;
    }

    // callback must be a function (or at least truthy)
    if (!js_is_truthy(cb_item)) {
        log_debug("js_dom_add_event_listener: null callback for '%s'", type);
        return;
    }

    bool capture = false, once = false, passive = false;
    parse_listener_options(opts_item, &capture, &once, &passive);

    void* key = get_event_target_key(elem_item);
    NodeListeners* nl = get_or_create_listeners(key);

    // check for duplicate (same type + callback + capture)
    for (int i = 0; i < nl->count; i++) {
        EventListener* el = &nl->items[i];
        if (strcmp(el->type, type) == 0 && el->capture == capture &&
            el->callback.item == cb_item.item) {
            log_debug("js_dom_add_event_listener: duplicate listener for '%s', skipping", type);
            return;
        }
    }

    // allocate type string copy
    size_t type_len = strlen(type);
    char* type_copy = (char*)mem_calloc(1, type_len + 1, MEM_CAT_JS_RUNTIME);
    memcpy(type_copy, type, type_len);

    EventListener listener = {};
    listener.type = type_copy;
    listener.callback = cb_item;
    listener.capture = capture;
    listener.once = once;
    listener.passive = passive;

    nl_push(nl, listener);
    log_debug("js_dom_add_event_listener: added '%s' listener (capture=%d, once=%d) on %p",
              type, (int)capture, (int)once, key);
}

void js_dom_remove_event_listener(Item elem_item, Item type_item, Item cb_item, Item opts_item) {
    const char* type = fn_to_cstr(type_item);
    if (!type) return;

    bool capture = false, once = false, passive = false;
    parse_listener_options(opts_item, &capture, &once, &passive);

    void* key = get_event_target_key(elem_item);
    NodeListeners* nl = find_listeners(key);
    if (!nl) return;

    for (int i = 0; i < nl->count; i++) {
        EventListener* el = &nl->items[i];
        if (strcmp(el->type, type) == 0 && el->capture == capture &&
            el->callback.item == cb_item.item) {
            // remove by shifting
            mem_free(el->type);
            for (int j = i; j < nl->count - 1; j++) {
                nl->items[j] = nl->items[j + 1];
            }
            nl->count--;
            log_debug("js_dom_remove_event_listener: removed '%s' listener from %p", type, key);
            return;
        }
    }
}

// ============================================================================
// Event Object Creation
// ============================================================================

// helper: set a string property on an event object
static void event_set_str(Item event, const char* key, const char* value) {
    Item k = (Item){.item = s2it(heap_create_name(key))};
    Item v = value ? (Item){.item = s2it(heap_create_name(value))} : ItemNull;
    js_property_set(event, k, v);
}

// helper: set a bool property on an event object
static void event_set_bool(Item event, const char* key, bool value) {
    Item k = (Item){.item = s2it(heap_create_name(key))};
    Item v = (Item){.item = b2it(value ? 1 : 0)};
    js_property_set(event, k, v);
}

// helper: set an int property on an event object
static void event_set_int(Item event, const char* key, int value) {
    Item k = (Item){.item = s2it(heap_create_name(key))};
    Item v = (Item){.item = i2it(value)};
    js_property_set(event, k, v);
}

// helper: set a double property
static void event_set_double(Item event, const char* key, double value) {
    Item k = (Item){.item = s2it(heap_create_name(key))};
    js_property_set(event, k, event_make_double(value));
}

// helper: set an arbitrary item
static void event_set_item(Item event, const char* key, Item value) {
    Item k = (Item){.item = s2it(heap_create_name(key))};
    js_property_set(event, k, value);
}

// Get the current event flag values from per-event slots stored on the event
// object itself (so nested dispatches don't trample each other).
static bool event_flag_get(Item event, const char* key) {
    Item v = js_property_get(event, (Item){.item = s2it(heap_create_name(key))});
    return js_is_truthy(v);
}

// Per-event state lives on the event object itself in __stop_prop /
// __stop_imm / __default_prevented / __dispatch_flag / __passive slots so
// that nested dispatch is safe. The thread-local mirrors below remain only
// as a transitional fallback for the dispatch loop checks until Phase 2
// fully wires per-event propagation through the loop.
static __thread bool _stop_propagation = false;
static __thread bool _stop_immediate = false;
static __thread bool _default_prevented = false;

extern "C" Item js_event_prevent_default() {
    Item ev = js_get_this();
    if (get_type_id(ev) == LMD_TYPE_MAP) {
        // Spec: silently no-op if the event is in a passive listener.
        if (event_flag_get(ev, "__in_passive")) return make_js_undefined();
        // Spec: silently no-op if event is not cancelable.
        Item cancelable = js_property_get(ev, (Item){.item = s2it(heap_create_name("cancelable"))});
        if (!js_is_truthy(cancelable)) return make_js_undefined();
        event_set_bool(ev, "__default_prevented", true);
        event_set_bool(ev, "defaultPrevented", true);
        event_set_bool(ev, "returnValue", false);
    }
    _default_prevented = true;
    return make_js_undefined();
}

extern "C" Item js_event_stop_propagation() {
    Item ev = js_get_this();
    if (get_type_id(ev) == LMD_TYPE_MAP) {
        event_set_bool(ev, "__stop_prop", true);
        event_set_bool(ev, "cancelBubble", true);
    }
    _stop_propagation = true;
    return make_js_undefined();
}

extern "C" Item js_event_stop_immediate_propagation() {
    Item ev = js_get_this();
    if (get_type_id(ev) == LMD_TYPE_MAP) {
        event_set_bool(ev, "__stop_prop", true);
        event_set_bool(ev, "__stop_imm", true);
        event_set_bool(ev, "cancelBubble", true);
    }
    _stop_propagation = true;
    _stop_immediate = true;
    return make_js_undefined();
}

// Legacy aliases / setters

// composedPath() — returns array of targets in the dispatch path. With no
// shadow DOM, this is just [target, ...ancestors..., document, window].
// We compute lazily from the stored `target` slot at call time.
extern "C" Item js_event_composed_path() {
    Item ev = js_get_this();
    Item out = js_array_new(0);
    if (get_type_id(ev) != LMD_TYPE_MAP) return out;
    Item target = js_property_get(ev, (Item){.item = s2it(heap_create_name("target"))});
    if (target.item == 0 || get_type_id(target) == LMD_TYPE_NULL) return out;
    js_array_push(out, target);
    // Walk parent chain if the target is a DOM element wrapper.
    void* node_ptr = js_dom_unwrap_element(target);
    if (node_ptr) {
        DomNode* current = ((DomNode*)node_ptr)->parent;
        while (current) {
            Item w = js_dom_wrap_element((void*)current);
            if (w.item != 0 && get_type_id(w) != LMD_TYPE_NULL) js_array_push(out, w);
            current = current->parent;
        }
    }
    Item doc = js_get_document_object_value();
    if (doc.item != 0 && get_type_id(doc) != LMD_TYPE_NULL) js_array_push(out, doc);
    return out;
}

// initEvent(type, bubbles, cancelable) — legacy. No-op while dispatching.
extern "C" Item js_event_init_event(Item type_arg, Item b_arg, Item c_arg) {
    Item ev = js_get_this();
    if (get_type_id(ev) != LMD_TYPE_MAP) return make_js_undefined();
    if (event_flag_get(ev, "__dispatch_flag")) return make_js_undefined();
    const char* type = fn_to_cstr(type_arg);
    if (type) event_set_str(ev, "type", type);
    bool bub = (type_arg.item != 0) ? js_is_truthy(b_arg) : false;
    bool can = (type_arg.item != 0) ? js_is_truthy(c_arg) : false;
    event_set_bool(ev, "bubbles", bub);
    event_set_bool(ev, "cancelable", can);
    event_set_bool(ev, "defaultPrevented", false);
    event_set_bool(ev, "__default_prevented", false);
    event_set_bool(ev, "__stop_prop", false);
    event_set_bool(ev, "__stop_imm", false);
    event_set_bool(ev, "cancelBubble", false);
    event_set_bool(ev, "returnValue", true);
    event_set_item(ev, "target", ItemNull);
    event_set_item(ev, "srcElement", ItemNull);
    event_set_item(ev, "currentTarget", ItemNull);
    event_set_int(ev, "eventPhase", 0);
    return make_js_undefined();
}

// initCustomEvent(type, bubbles, cancelable, detail) — legacy.
extern "C" Item js_event_init_custom_event(Item type_arg, Item b_arg, Item c_arg, Item detail_arg) {
    js_event_init_event(type_arg, b_arg, c_arg);
    Item ev = js_get_this();
    if (get_type_id(ev) == LMD_TYPE_MAP) event_set_item(ev, "detail", detail_arg);
    return make_js_undefined();
}

// cancelBubble setter — assigning true is equivalent to stopPropagation.
// We expose a method-style helper; the legacy IDL accessor is approximated by
// a writable own property on the event (set by ctor / dispatch loop).

// returnValue setter — assigning false sets defaultPrevented.
// Same approach: mutable own property. Applied during dispatch.

Item js_create_event_init(const char* type, bool bubbles, bool cancelable, bool composed) {
    Item event = js_new_object();

    event_set_str(event, "type", type);
    event_set_bool(event, "bubbles", bubbles);
    event_set_bool(event, "cancelable", cancelable);
    event_set_bool(event, "composed", composed);
    event_set_bool(event, "defaultPrevented", false);
    event_set_int(event, "eventPhase", 0);  // NONE initially
    event_set_bool(event, "isTrusted", false);
    event_set_double(event, "timeStamp", event_now_ms());

    // Legacy aliases.
    event_set_bool(event, "cancelBubble", false);
    event_set_bool(event, "returnValue", true);

    // Per-event dispatch flags.
    event_set_bool(event, "__stop_prop", false);
    event_set_bool(event, "__stop_imm", false);
    event_set_bool(event, "__default_prevented", false);
    event_set_bool(event, "__dispatch_flag", false);
    event_set_bool(event, "__in_passive", false);

    // Static phase constants exposed on each instance for legacy code.
    event_set_int(event, "NONE", 0);
    event_set_int(event, "CAPTURING_PHASE", 1);
    event_set_int(event, "AT_TARGET", 2);
    event_set_int(event, "BUBBLING_PHASE", 3);

    // target / currentTarget / srcElement default to null.
    event_set_item(event, "target", ItemNull);
    event_set_item(event, "srcElement", ItemNull);
    event_set_item(event, "currentTarget", ItemNull);

    // methods — create proper JS function wrappers for native C callbacks.
    Item pd_key = (Item){.item = s2it(heap_create_name("preventDefault"))};
    Item sp_key = (Item){.item = s2it(heap_create_name("stopPropagation"))};
    Item si_key = (Item){.item = s2it(heap_create_name("stopImmediatePropagation"))};
    Item cp_key = (Item){.item = s2it(heap_create_name("composedPath"))};
    Item ie_key = (Item){.item = s2it(heap_create_name("initEvent"))};

    js_property_set(event, pd_key, js_new_function((void*)js_event_prevent_default, 0));
    js_property_set(event, sp_key, js_new_function((void*)js_event_stop_propagation, 0));
    js_property_set(event, si_key, js_new_function((void*)js_event_stop_immediate_propagation, 0));
    js_property_set(event, cp_key, js_new_function((void*)js_event_composed_path, 0));
    js_property_set(event, ie_key, js_new_function((void*)js_event_init_event, 3));

    return event;
}

Item js_create_event(const char* type, bool bubbles, bool cancelable) {
    return js_create_event_init(type, bubbles, cancelable, false);
}

Item js_create_custom_event_init(const char* type, bool bubbles, bool cancelable,
                                 bool composed, Item detail) {
    Item event = js_create_event_init(type, bubbles, cancelable, composed);
    event_set_item(event, "detail", detail);
    Item ice_key = (Item){.item = s2it(heap_create_name("initCustomEvent"))};
    js_property_set(event, ice_key, js_new_function((void*)js_event_init_custom_event, 4));
    return event;
}

Item js_create_custom_event(const char* type, bool bubbles, bool cancelable, Item detail) {
    return js_create_custom_event_init(type, bubbles, cancelable, false, detail);
}

// ============================================================================
// Event Dispatch (3-phase propagation)
// ============================================================================

// build propagation path from target to root
static int build_path(Item target, void** path, int max_path) {
    int count = 0;

    // Document proxy: target is the sentinel; walk to window only.
    if (js_is_document_proxy(target)) {
        if (count < max_path) path[count++] = (void*)&_document_sentinel;
        if (count < max_path) path[count++] = (void*)&_window_sentinel;
        return count;
    }

    // start from target's DOM node
    void* node_ptr = js_dom_unwrap_element(target);
    if (!node_ptr) {
        // non-DOM target (window or document)
        path[count++] = get_event_target_key(target);
        return count;
    }

    DomNode* node = (DomNode*)node_ptr;

    // walk from target up to root
    DomNode* current = node;
    while (current && count < max_path) {
        path[count++] = (void*)current;
        current = current->parent;
    }

    // add document and window sentinels at the end (root of propagation)
    if (count < max_path) path[count++] = (void*)&_document_sentinel;
    if (count < max_path) path[count++] = (void*)&_window_sentinel;

    return count;
}

// wrap a path key back into an Item for currentTarget
static Item wrap_path_key(void* key) {
    if (key == (void*)&_window_sentinel) {
        // return undefined for window — jQuery checks typeof
        return ItemNull;
    }
    if (key == (void*)&_document_sentinel) {
        return js_get_document_object_value();
    }
    // it's a DomNode*
    return js_dom_wrap_element(key);
}

// fire listeners on a specific node for a given phase
static void fire_listeners(void* key, const char* type, Item event, int phase) {
    NodeListeners* nl = find_listeners(key);
    bool has_listeners = (nl && nl->count > 0);
    // Check for an `on<type>` IDL handler on the target. We fire it during
    // AT_TARGET / BUBBLING phases (not CAPTURING), matching the HTML spec.
    Item on_handler = ItemNull;
    Item target_item = ItemNull;
    if (phase != 1) {
        target_item = wrap_path_key(key);
        TypeId tt = get_type_id(target_item);
        if (tt == LMD_TYPE_MAP || tt == LMD_TYPE_ELEMENT) {
            char on_name[64];
            snprintf(on_name, sizeof(on_name), "on%s", type);
            Item on_key = (Item){.item = s2it(heap_create_name(on_name))};
            Item h = js_property_get(target_item, on_key);
            if (get_type_id(h) == LMD_TYPE_FUNC) on_handler = h;
        }
    }
    bool has_on = (get_type_id(on_handler) == LMD_TYPE_FUNC);
    if (!has_listeners && !has_on) return;

    // set eventPhase
    event_set_int(event, "eventPhase", phase);

    // set currentTarget
    Item ct_key = (Item){.item = s2it(heap_create_name("currentTarget"))};
    js_property_set(event, ct_key, wrap_path_key(key));

    // Fire on<type> IDL handler first (fires before addEventListener listeners
    // per HTML spec ordering when the IDL handler was set first).
    if (has_on && !_stop_immediate) {
        Item args[1] = { event };
        js_call_function(on_handler, target_item, args, 1);
    }

    // iterate (snapshot count, since once-listeners modify the list)
    int count = nl ? nl->count : 0;
    for (int i = 0; i < count && nl && i < nl->count; i++) {
        if (_stop_immediate) return;

        EventListener* el = &nl->items[i];
        if (strcmp(el->type, type) != 0) continue;

        // phase filtering:
        // AT_TARGET (phase 2): fire all listeners regardless of capture flag
        // CAPTURING (phase 1): only fire capture listeners
        // BUBBLING (phase 3): only fire non-capture listeners
        if (phase == 1 && !el->capture) continue;
        if (phase == 3 && el->capture) continue;

        // call the callback with event as argument
        Item args[1] = { event };
        js_call_function(el->callback, wrap_path_key(key), args, 1);

        // handle once
        if (el->once) {
            mem_free(el->type);
            for (int j = i; j < nl->count - 1; j++) {
                nl->items[j] = nl->items[j + 1];
            }
            nl->count--;
            count--;
            i--;  // re-examine this index
        }
    }
}

Item js_dom_dispatch_event(Item elem_item, Item event_item) {
    // get event type
    Item type_key = (Item){.item = s2it(heap_create_name("type"))};
    Item type_val = js_property_get(event_item, type_key);
    const char* type = fn_to_cstr(type_val);
    if (!type) {
        log_error("js_dom_dispatch_event: event has no type");
        return (Item){.item = ITEM_FALSE};
    }

    // get bubbles flag
    Item bubbles_key = (Item){.item = s2it(heap_create_name("bubbles"))};
    Item bubbles_val = js_property_get(event_item, bubbles_key);
    bool bubbles = js_is_truthy(bubbles_val);

    // Spec: throw InvalidStateError if event is already being dispatched.
    // (Implemented as a soft check + return false until error wiring lands.)
    if (event_flag_get(event_item, "__dispatch_flag")) {
        log_error("js_dom_dispatch_event: re-entrant dispatch on same event");
        return (Item){.item = ITEM_FALSE};
    }

    // set target / srcElement (only if not already set per spec on re-dispatch)
    Item target_key = (Item){.item = s2it(heap_create_name("target"))};
    Item src_key = (Item){.item = s2it(heap_create_name("srcElement"))};
    Item existing_target = js_property_get(event_item, target_key);
    if (existing_target.item == 0 || get_type_id(existing_target) == LMD_TYPE_NULL) {
        js_property_set(event_item, target_key, elem_item);
        js_property_set(event_item, src_key, elem_item);
    }

    // Mark event as dispatching.
    event_set_bool(event_item, "__dispatch_flag", true);

    // Reset per-event flags to fresh state for this dispatch.
    event_set_bool(event_item, "__stop_prop", false);
    event_set_bool(event_item, "__stop_imm", false);
    event_set_bool(event_item, "cancelBubble", false);

    // reset propagation state (legacy thread-locals)
    _stop_propagation = false;
    _stop_immediate = false;
    _default_prevented = false;

    // build propagation path (target → ... → document → window)
    void* path[128];
    int path_len = build_path(elem_item, path, 128);

    if (path_len == 0) {
        return (Item){.item = ITEM_TRUE};
    }

    // path[0] = target, path[1] = parent, ... path[n-1] = window
    // Phase 1: Capture — from root down to target (exclusive)
    for (int i = path_len - 1; i > 0; i--) {
        if (_stop_propagation) break;
        fire_listeners(path[i], type, event_item, 1);  // CAPTURING_PHASE
    }

    // Phase 2: Target
    if (!_stop_propagation) {
        fire_listeners(path[0], type, event_item, 2);  // AT_TARGET
    }

    // Phase 3: Bubble — from target parent up to root
    if (bubbles) {
        for (int i = 1; i < path_len; i++) {
            if (_stop_propagation) break;
            fire_listeners(path[i], type, event_item, 3);  // BUBBLING_PHASE
        }
    }

    // set eventPhase to NONE after dispatch
    event_set_int(event_item, "eventPhase", 0);

    // currentTarget is reset to null after dispatch (per spec).
    Item ct_key = (Item){.item = s2it(heap_create_name("currentTarget"))};
    js_property_set(event_item, ct_key, ItemNull);

    // Clear dispatching flag.
    event_set_bool(event_item, "__dispatch_flag", false);

    bool prevented = _default_prevented || event_flag_get(event_item, "__default_prevented");
    if (prevented) {
        event_set_bool(event_item, "defaultPrevented", true);
        event_set_bool(event_item, "returnValue", false);
    }

    log_debug("js_dom_dispatch_event: dispatched '%s' on %p (prevented=%d)",
              type, (void*)path[0], (int)prevented);

    // dispatchEvent returns false only when the event is cancelable AND
    // preventDefault was called.
    Item cancelable = js_property_get(event_item, (Item){.item = s2it(heap_create_name("cancelable"))});
    bool ret_false = prevented && js_is_truthy(cancelable);
    return (Item){.item = ret_false ? ITEM_FALSE : ITEM_TRUE};
}

// ============================================================================
// Lifecycle
// ============================================================================

void js_dom_events_reset(void) {
    for (int i = 0; i < _entry_count; i++) {
        NodeListeners* nl = &_entries[i].listeners;
        for (int j = 0; j < nl->count; j++) {
            if (nl->items[j].type) mem_free(nl->items[j].type);
        }
        if (nl->items) mem_free(nl->items);
    }
    if (_entries) {
        mem_free(_entries);
        _entries = nullptr;
    }
    _entry_count = 0;
    _entry_capacity = 0;
    _stop_propagation = false;
    _stop_immediate = false;
    _default_prevented = false;
}
