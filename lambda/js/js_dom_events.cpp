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

// internal state for stop propagation (per dispatch call)
static __thread bool _stop_propagation = false;
static __thread bool _stop_immediate = false;
static __thread bool _default_prevented = false;

// C callbacks for event methods — 0-param functions called via js_call_function.
// They only set thread-local flags; the dispatch loop updates the event object.
extern "C" Item js_event_prevent_default() {
    _default_prevented = true;
    return make_js_undefined();
}

extern "C" Item js_event_stop_propagation() {
    _stop_propagation = true;
    return make_js_undefined();
}

extern "C" Item js_event_stop_immediate_propagation() {
    _stop_propagation = true;
    _stop_immediate = true;
    return make_js_undefined();
}

Item js_create_event(const char* type, bool bubbles, bool cancelable) {
    Item event = js_new_object();

    event_set_str(event, "type", type);
    event_set_bool(event, "bubbles", bubbles);
    event_set_bool(event, "cancelable", cancelable);
    event_set_bool(event, "defaultPrevented", false);
    event_set_int(event, "eventPhase", 0);  // NONE initially
    event_set_bool(event, "isTrusted", false);
    event_set_int(event, "timeStamp", 0);

    // target and currentTarget set during dispatch
    Item null_item = ItemNull;
    Item target_key = (Item){.item = s2it(heap_create_name("target"))};
    Item ct_key = (Item){.item = s2it(heap_create_name("currentTarget"))};
    js_property_set(event, target_key, null_item);
    js_property_set(event, ct_key, null_item);

    // methods — create proper JS function wrappers for native C callbacks
    Item pd_key = (Item){.item = s2it(heap_create_name("preventDefault"))};
    Item sp_key = (Item){.item = s2it(heap_create_name("stopPropagation"))};
    Item si_key = (Item){.item = s2it(heap_create_name("stopImmediatePropagation"))};

    Item pd_fn = js_new_function((void*)js_event_prevent_default, 0);
    Item sp_fn = js_new_function((void*)js_event_stop_propagation, 0);
    Item si_fn = js_new_function((void*)js_event_stop_immediate_propagation, 0);
    js_property_set(event, pd_key, pd_fn);
    js_property_set(event, sp_key, sp_fn);
    js_property_set(event, si_key, si_fn);

    return event;
}

Item js_create_custom_event(const char* type, bool bubbles, bool cancelable, Item detail) {
    Item event = js_create_event(type, bubbles, cancelable);
    Item detail_key = (Item){.item = s2it(heap_create_name("detail"))};
    js_property_set(event, detail_key, detail);
    return event;
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
    if (!nl || nl->count == 0) return;

    // set eventPhase
    event_set_int(event, "eventPhase", phase);

    // set currentTarget
    Item ct_key = (Item){.item = s2it(heap_create_name("currentTarget"))};
    js_property_set(event, ct_key, wrap_path_key(key));

    // iterate (snapshot count, since once-listeners modify the list)
    int count = nl->count;
    for (int i = 0; i < count && i < nl->count; i++) {
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

    // set target
    Item target_key = (Item){.item = s2it(heap_create_name("target"))};
    js_property_set(event_item, target_key, elem_item);

    // reset propagation state
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

    // update defaultPrevented on the event object based on thread-local flag
    if (_default_prevented) {
        event_set_bool(event_item, "defaultPrevented", true);
    }

    log_debug("js_dom_dispatch_event: dispatched '%s' on %p (prevented=%d)",
              type, (void*)path[0], (int)_default_prevented);

    return (Item){.item = _default_prevented ? ITEM_FALSE : ITEM_TRUE};
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
