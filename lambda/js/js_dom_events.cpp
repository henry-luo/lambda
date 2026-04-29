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
#include <cmath>
#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

// Forward decls used by Event helpers below (signatures from js_runtime.h /
// js_dom.h, declared here under extern "C" to avoid header coupling).
extern "C" Item js_get_this();
extern Item js_array_new(int length);
extern Item js_array_push(Item array, Item value);
extern Item js_get_document_object_value();
extern "C" Item js_get_global_this();
extern "C" int js_check_exception(void);

// Form-control IDL helpers from js_dom.cpp — used by HTMLElement click
// activation behavior (HTML §6.4.4).
extern "C" bool js_dom_is_checkbox_or_radio(void* dom_elem);
extern "C" bool js_dom_get_checkedness(void* dom_elem);
extern "C" void js_dom_set_checkedness(void* dom_elem, bool v);
extern "C" const char* js_dom_input_type_lower(void* dom_elem);
extern "C" const char* js_dom_tag_name_raw(void* dom_elem);
extern "C" bool js_dom_is_disabled(void* dom_elem);
extern "C" bool js_dom_is_connected(void* dom_elem);
static inline Item event_make_double(double v) {
    double* p = (double*)heap_calloc(sizeof(double), LMD_TYPE_FLOAT);
    *p = v;
    return (Item){.item = d2it(p)};
}

// High-resolution monotonic ms with the SAME time origin as
// performance.now() (no origin subtraction; counts since boot on macOS,
// since CLOCK_MONOTONIC origin on Linux). Spec requires that an event's
// `timeStamp` is comparable with values returned by performance.now().
// The value is clamped to 5 microsecond resolution per HR-Time / WPT
// `Event-timestamp-safe-resolution` (timing-attack hardening).
static double event_now_ms() {
    double ms;
#ifdef __APPLE__
    static mach_timebase_info_data_t timebase = {0, 0};
    if (timebase.denom == 0) mach_timebase_info(&timebase);
    uint64_t ticks = mach_absolute_time();
    double ns = (double)ticks * (double)timebase.numer / (double)timebase.denom;
    ms = ns / 1e6;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ms = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#endif
    // clamp to 5us = 0.005ms resolution
    return floor(ms / 0.005) * 0.005;
}

// Report an exception thrown by an event listener / handler to
// `window.onerror` (HTML spec: report exception). Best-effort: if
// onerror is not a function, just swallow.
static void report_exception_to_window_onerror(Item err, const char* type) {
    extern Item js_get_global_this(void);
    extern Item js_to_string(Item value);
    Item global = js_get_global_this();
    if (global.item == 0) return;
    Item onerr_key = (Item){.item = s2it(heap_create_name("onerror"))};
    Item onerr = js_property_get(global, onerr_key);
    if (get_type_id(onerr) != LMD_TYPE_FUNC) return;
    // build message string from error value
    Item msg = err;
    if (get_type_id(err) == LMD_TYPE_MAP || get_type_id(err) == LMD_TYPE_OBJECT) {
        Item m_key = (Item){.item = s2it(heap_create_name("message"))};
        Item m = js_property_get(err, m_key);
        if (get_type_id(m) == LMD_TYPE_STRING) msg = m;
        else msg = js_to_string(err);
    } else if (get_type_id(err) != LMD_TYPE_STRING) {
        msg = js_to_string(err);
    }
    Item args[5] = { msg, ItemNull, (Item){.item = b2it(false)}, (Item){.item = b2it(false)}, err };
    js_call_function(onerr, global, args, 5);
    if (js_check_exception()) (void)js_clear_exception();
    (void)type;
}

static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

// ============================================================================
// Event listener storage
// ============================================================================

struct EventListener {
    char*   type;       // event type string (owned copy)
    Item    callback;   // JS function OR object with handleEvent method
    Item    signal;     // AbortSignal (or ItemNull); if signal aborts, listener is removed
    bool    capture;    // capture phase listener
    bool    once;       // remove after first invocation
    bool    passive;    // passive listener (cannot preventDefault)
    bool    has_passive;// passive flag was explicitly set
    bool    removed;    // tombstone — set when removed during dispatch
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
    // If target IS the global (window) object, key on the window sentinel so
    // that addEventListener on window and dispatch through the path agree.
    {
        Item global = js_get_global_this();
        if (target.item != 0 && target.item == global.item) {
            return (void*)&_window_sentinel;
        }
    }
    // Plain JS object EventTarget — key on the object pointer itself so
    // each `new EventTarget()` instance has its own listener list.
    TypeId tid = get_type_id(target);
    if (tid == LMD_TYPE_MAP || tid == LMD_TYPE_OBJECT || tid == LMD_TYPE_VMAP) {
        return (void*)target.container;
    }
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

static void parse_listener_options(Item opts, bool* capture, bool* once,
                                    bool* passive, bool* has_passive,
                                    Item* signal_out) {
    *capture = false;
    *once = false;
    *passive = false;
    *has_passive = false;
    *signal_out = ItemNull;

    if (opts.item == 0 || get_type_id(opts) == LMD_TYPE_NULL ||
        get_type_id(opts) == LMD_TYPE_UNDEFINED) {
        return;
    }

    // boolean argument = useCapture. Per WebIDL, anything that is not a
    // dictionary (object/map) is converted via ToBoolean for the boolean union.
    TypeId tid = get_type_id(opts);
    if (tid != LMD_TYPE_MAP && tid != LMD_TYPE_OBJECT) {
        *capture = js_is_truthy(opts);
        return;
    }

    // options object: {capture, once, passive, signal}
    if (tid == LMD_TYPE_MAP || tid == LMD_TYPE_OBJECT) {
        Item cap_key = (Item){.item = s2it(heap_create_name("capture"))};
        Item once_key = (Item){.item = s2it(heap_create_name("once"))};
        Item passive_key = (Item){.item = s2it(heap_create_name("passive"))};
        Item signal_key = (Item){.item = s2it(heap_create_name("signal"))};

        Item cap_val = js_property_get(opts, cap_key);
        Item once_val = js_property_get(opts, once_key);
        Item passive_val = js_property_get(opts, passive_key);
        Item signal_val = js_property_get(opts, signal_key);

        if (cap_val.item != 0 && get_type_id(cap_val) != LMD_TYPE_UNDEFINED)
            *capture = js_is_truthy(cap_val);
        if (once_val.item != 0 && get_type_id(once_val) != LMD_TYPE_UNDEFINED)
            *once = js_is_truthy(once_val);
        if (passive_val.item != 0 && get_type_id(passive_val) != LMD_TYPE_UNDEFINED) {
            *passive = js_is_truthy(passive_val);
            *has_passive = true;
        }
        if (signal_val.item != 0) {
            TypeId st = get_type_id(signal_val);
            if (st == LMD_TYPE_NULL) {
                // Per spec, signal must be an AbortSignal — null is a TypeError.
                Item n = (Item){.item = s2it(heap_create_name("TypeError"))};
                Item m = (Item){.item = s2it(heap_create_name(
                    "Failed to execute 'addEventListener' on 'EventTarget': "
                    "member signal is not of type 'AbortSignal'."))};
                js_throw_value(js_new_error_with_name(n, m));
                return;
            }
            if (st == LMD_TYPE_MAP || st == LMD_TYPE_OBJECT) {
                *signal_out = signal_val;
            }
        }
    }
}

// returns true if signal_item is an already-aborted AbortSignal
static bool signal_is_aborted(Item signal_item) {
    if (signal_item.item == 0) return false;
    TypeId t = get_type_id(signal_item);
    if (t != LMD_TYPE_MAP && t != LMD_TYPE_OBJECT) return false;
    Item ab = js_property_get(signal_item,
        (Item){.item = s2it(heap_create_name("aborted"))});
    return js_is_truthy(ab);
}

// ============================================================================
// addEventListener / removeEventListener
// ============================================================================

void js_dom_add_event_listener(Item elem_item, Item type_item, Item cb_item, Item opts_item) {    const char* type = fn_to_cstr(type_item);
    if (!type) {
        log_debug("js_dom_add_event_listener: invalid type");
        return;
    }

    // Per spec the options flattening happens before any further checks; in
    // particular it must run even when the callback is null so getter side
    // effects (used by feature-detection code) fire.
    bool capture = false, once = false, passive = false, has_passive = false;
    Item signal = ItemNull;
    parse_listener_options(opts_item, &capture, &once, &passive, &has_passive, &signal);
    if (js_check_exception()) return;

    // Per spec: addEventListener with null/undefined callback is a no-op.
    TypeId cb_tid = get_type_id(cb_item);
    if (cb_item.item == 0 || cb_tid == LMD_TYPE_NULL || cb_tid == LMD_TYPE_UNDEFINED) {
        return;
    }
    // Callback must be either a function or an object with handleEvent (checked
    // lazily at dispatch time). Reject obviously-bad types like numbers/booleans.
    if (cb_tid != LMD_TYPE_FUNC && cb_tid != LMD_TYPE_MAP &&
        cb_tid != LMD_TYPE_OBJECT && cb_tid != LMD_TYPE_ELEMENT) {
        log_debug("js_dom_add_event_listener: callback must be function or object (got tid=%d)", cb_tid);
        return;
    }

    // Per spec: if signal is an already-aborted AbortSignal, do not add.
    if (signal_is_aborted(signal)) {
        return;
    }

    void* key = get_event_target_key(elem_item);

    // HTML "default passive" rule: when `passive` is omitted in the options,
    // listeners for touchstart/touchmove/wheel/mousewheel on window /
    // document / documentElement / body default to passive=true.
    // https://dom.spec.whatwg.org/#default-passive-value
    if (!has_passive) {
        bool is_passive_event = (strcmp(type, "touchstart") == 0 ||
                                 strcmp(type, "touchmove") == 0 ||
                                 strcmp(type, "wheel") == 0 ||
                                 strcmp(type, "mousewheel") == 0);
        if (is_passive_event) {
            bool is_root_target = false;
            if (key == (void*)&_window_sentinel ||
                key == (void*)&_document_sentinel) {
                is_root_target = true;
            } else {
                DomElement* el = (DomElement*)js_dom_unwrap_element(elem_item);
                if (el && el->tag_name &&
                    (strcasecmp(el->tag_name, "html") == 0 ||
                     strcasecmp(el->tag_name, "body") == 0)) {
                    is_root_target = true;
                }
            }
            if (is_root_target) {
                passive = true;
                has_passive = true;
            }
        }
    }

    NodeListeners* nl = get_or_create_listeners(key);

    // check for duplicate (same type + callback + capture); ignore tombstones
    for (int i = 0; i < nl->count; i++) {
        EventListener* el = &nl->items[i];
        if (el->removed) continue;
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
    listener.signal = signal;
    listener.capture = capture;
    listener.once = once;
    listener.passive = passive;
    listener.has_passive = has_passive;
    listener.removed = false;

    nl_push(nl, listener);
    log_debug("js_dom_add_event_listener: added '%s' listener (capture=%d, once=%d, passive=%d) on %p",
              type, (int)capture, (int)once, (int)passive, key);
}

void js_dom_remove_event_listener(Item elem_item, Item type_item, Item cb_item, Item opts_item) {
    const char* type = fn_to_cstr(type_item);
    if (!type) return;

    // removeEventListener only reads capture from options (per spec). Do NOT
    // read passive/signal getters here — feature-detection tests rely on this.
    bool capture = false;
    if (opts_item.item != 0) {
        TypeId opt_tid = get_type_id(opts_item);
        if (opt_tid == LMD_TYPE_MAP || opt_tid == LMD_TYPE_OBJECT) {
            Item cap_key = (Item){.item = s2it(heap_create_name("capture"))};
            Item cap_val = js_property_get(opts_item, cap_key);
            if (cap_val.item != 0 && get_type_id(cap_val) != LMD_TYPE_UNDEFINED)
                capture = js_is_truthy(cap_val);
        } else {
            // Non-dictionary: ToBoolean.
            capture = js_is_truthy(opts_item);
        }
    }

    void* key = get_event_target_key(elem_item);
    NodeListeners* nl = find_listeners(key);
    if (!nl) return;

    for (int i = 0; i < nl->count; i++) {
        EventListener* el = &nl->items[i];
        if (el->removed) continue;
        if (strcmp(el->type, type) == 0 && el->capture == capture &&
            el->callback.item == cb_item.item) {
            // tombstone — actual storage is reclaimed at next opportunity.
            // This protects in-flight dispatch loops walking the array.
            el->removed = true;
            log_debug("js_dom_remove_event_listener: removed '%s' listener from %p", type, key);
            return;
        }
    }
}

// Compact tombstoned listeners from a NodeListeners array. Safe to call only
// when no dispatch is walking this list.
static void nl_compact(NodeListeners* nl) {
    int w = 0;
    for (int r = 0; r < nl->count; r++) {
        if (nl->items[r].removed) {
            if (nl->items[r].type) mem_free(nl->items[r].type);
            continue;
        }
        if (w != r) nl->items[w] = nl->items[r];
        w++;
    }
    nl->count = w;
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
    }
    _default_prevented = true;
    return make_js_undefined();
}

extern "C" Item js_event_stop_propagation() {
    Item ev = js_get_this();
    if (get_type_id(ev) == LMD_TYPE_MAP) {
        event_set_bool(ev, "__stop_prop", true);
    }
    _stop_propagation = true;
    return make_js_undefined();
}

extern "C" Item js_event_stop_immediate_propagation() {
    Item ev = js_get_this();
    if (get_type_id(ev) == LMD_TYPE_MAP) {
        event_set_bool(ev, "__stop_prop", true);
        event_set_bool(ev, "__stop_imm", true);
    }
    _stop_propagation = true;
    _stop_immediate = true;
    return make_js_undefined();
}

// Legacy aliases / setters

// returnValue accessor: getter returns !canceled; setter (when cancelable
// and not in passive listener) sets defaultPrevented if value is false.
extern "C" Item js_event_returnvalue_get(void) {
    Item ev = js_get_this();
    if (get_type_id(ev) != LMD_TYPE_MAP) return (Item){.item = b2it(true)};
    bool dp = event_flag_get(ev, "__default_prevented");
    return (Item){.item = b2it(!dp)};
}

extern "C" Item js_event_returnvalue_set(Item value) {
    Item ev = js_get_this();
    if (get_type_id(ev) != LMD_TYPE_MAP) return make_js_undefined();
    bool truthy = js_is_truthy(value);
    if (!truthy) {
        Item cancelable = js_property_get(ev, (Item){.item = s2it(heap_create_name("cancelable"))});
        if (js_is_truthy(cancelable) && !event_flag_get(ev, "__in_passive")) {
            event_set_bool(ev, "__default_prevented", true);
        }
    }
    return make_js_undefined();
}

// cancelBubble accessor: getter returns stop-propagation flag; setter sets
// stop-propagation flag when value is truthy (false is a no-op).
extern "C" Item js_event_cancelbubble_get(void) {
    Item ev = js_get_this();
    if (get_type_id(ev) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    bool sp = event_flag_get(ev, "__stop_prop");
    return (Item){.item = b2it(sp)};
}

extern "C" Item js_event_cancelbubble_set(Item value) {
    Item ev = js_get_this();
    if (get_type_id(ev) != LMD_TYPE_MAP) return make_js_undefined();
    if (js_is_truthy(value)) {
        event_set_bool(ev, "__stop_prop", true);
        _stop_propagation = true;
    }
    return make_js_undefined();
}

// defaultPrevented getter: reflects the canceled flag.
extern "C" Item js_event_defaultprevented_get(void) {
    Item ev = js_get_this();
    if (get_type_id(ev) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    bool dp = event_flag_get(ev, "__default_prevented");
    return (Item){.item = b2it(dp)};
}

// composedPath() — returns array of targets in the dispatch path. With no
// shadow DOM, this is just [target, ...ancestors..., document, window].
// We compute lazily from the stored `target` slot at call time.
extern "C" Item js_event_composed_path() {
    Item ev = js_get_this();
    Item out = js_array_new(0);
    if (get_type_id(ev) != LMD_TYPE_MAP) return out;
    // Per spec: composedPath() returns the path stored on the event during
    // dispatch and is empty when not dispatching. We approximate by checking
    // whether currentTarget is non-null (set during dispatch, cleared after).
    Item ct = js_property_get(ev, (Item){.item = s2it(heap_create_name("currentTarget"))});
    if (ct.item == 0 || get_type_id(ct) == LMD_TYPE_NULL ||
        get_type_id(ct) == LMD_TYPE_UNDEFINED) {
        return out;
    }
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
    // Per spec, type is mandatory — throw TypeError if missing/undefined.
    TypeId tt = get_type_id(type_arg);
    if (type_arg.item == 0 || tt == LMD_TYPE_UNDEFINED) {
        Item n = (Item){.item = s2it(heap_create_name("TypeError"))};
        Item m = (Item){.item = s2it(heap_create_name(
            "Failed to execute 'initEvent' on 'Event': "
            "1 argument required, but only 0 present."))};
        js_throw_value(js_new_error_with_name(n, m));
        return make_js_undefined();
    }
    Item ev = js_get_this();
    if (get_type_id(ev) != LMD_TYPE_MAP) return make_js_undefined();
    if (event_flag_get(ev, "__dispatch_flag")) return make_js_undefined();
    const char* type = fn_to_cstr(type_arg);
    if (type) event_set_str(ev, "type", type);
    bool bub = js_is_truthy(b_arg);
    bool can = js_is_truthy(c_arg);
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
    if (get_type_id(ev) == LMD_TYPE_MAP) {
        // Per spec, omitted detail defaults to null (not undefined).
        TypeId dt = get_type_id(detail_arg);
        if (detail_arg.item == 0 || dt == LMD_TYPE_UNDEFINED) detail_arg = ItemNull;
        event_set_item(ev, "detail", detail_arg);
    }
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

    // Install accessor properties for legacy spec'd setters/getters
    // (returnValue, cancelBubble, defaultPrevented). These override the
    // plain data properties set above.
    {
        extern Item js_object_define_property(Item obj, Item name, Item descriptor);
        Item get_key = (Item){.item = s2it(heap_create_name("get"))};
        Item set_key = (Item){.item = s2it(heap_create_name("set"))};
        Item conf_key = (Item){.item = s2it(heap_create_name("configurable"))};
        Item enum_key = (Item){.item = s2it(heap_create_name("enumerable"))};
        Item true_v  = (Item){.item = b2it(true)};

        // returnValue: get + set
        Item rv_desc = js_new_object();
        js_property_set(rv_desc, get_key, js_new_function((void*)js_event_returnvalue_get, 0));
        js_property_set(rv_desc, set_key, js_new_function((void*)js_event_returnvalue_set, 1));
        js_property_set(rv_desc, conf_key, true_v);
        js_property_set(rv_desc, enum_key, true_v);
        js_object_define_property(event,
            (Item){.item = s2it(heap_create_name("returnValue"))}, rv_desc);

        // cancelBubble: get + set
        Item cb_desc = js_new_object();
        js_property_set(cb_desc, get_key, js_new_function((void*)js_event_cancelbubble_get, 0));
        js_property_set(cb_desc, set_key, js_new_function((void*)js_event_cancelbubble_set, 1));
        js_property_set(cb_desc, conf_key, true_v);
        js_property_set(cb_desc, enum_key, true_v);
        js_object_define_property(event,
            (Item){.item = s2it(heap_create_name("cancelBubble"))}, cb_desc);

        // defaultPrevented: get only
        Item dp_desc = js_new_object();
        js_property_set(dp_desc, get_key, js_new_function((void*)js_event_defaultprevented_get, 0));
        js_property_set(dp_desc, conf_key, true_v);
        js_property_set(dp_desc, enum_key, true_v);
        js_object_define_property(event,
            (Item){.item = s2it(heap_create_name("defaultPrevented"))}, dp_desc);
    }

    // Stamp class name so `event instanceof Event` resolves via the name fallback
    // in js_instanceof_classname (any class name ending in "Event" matches).
    js_property_set(event,
        (Item){.item = s2it(heap_create_name("__class_name__"))},
        (Item){.item = s2it(heap_create_name("Event"))});

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
    js_property_set(event,
        (Item){.item = s2it(heap_create_name("__class_name__"))},
        (Item){.item = s2it(heap_create_name("CustomEvent"))});
    return event;
}

Item js_create_custom_event(const char* type, bool bubbles, bool cancelable, Item detail) {
    return js_create_custom_event_init(type, bubbles, cancelable, false, detail);
}

// ============================================================================
// Generic EventTarget — plain JS object with addEventListener / removeEventListener
// / dispatchEvent methods bound such that `this` is the storage key.
// ============================================================================

extern "C" Item js_eventtarget_add_listener(Item type, Item callback, Item opts) {
    Item self = js_get_this();
    js_dom_add_event_listener(self, type, callback, opts);
    return make_js_undefined();
}

extern "C" Item js_eventtarget_remove_listener(Item type, Item callback, Item opts) {
    Item self = js_get_this();
    js_dom_remove_event_listener(self, type, callback, opts);
    return make_js_undefined();
}

extern "C" Item js_eventtarget_dispatch(Item event_item) {
    Item self = js_get_this();
    return js_dom_dispatch_event(self, event_item);
}

Item js_create_event_target(void) {
    Item et = js_new_object();
    Item ael = (Item){.item = s2it(heap_create_name("addEventListener"))};
    Item rel = (Item){.item = s2it(heap_create_name("removeEventListener"))};
    Item dis = (Item){.item = s2it(heap_create_name("dispatchEvent"))};
    js_property_set(et, ael, js_new_function((void*)js_eventtarget_add_listener, 3));
    js_property_set(et, rel, js_new_function((void*)js_eventtarget_remove_listener, 3));
    js_property_set(et, dis, js_new_function((void*)js_eventtarget_dispatch, 1));
    js_property_set(et,
        (Item){.item = s2it(heap_create_name("__class_name__"))},
        (Item){.item = s2it(heap_create_name("EventTarget"))});
    return et;
}

// ============================================================================
// Event subclasses (UIEvent / MouseEvent / KeyboardEvent / FocusEvent /
// WheelEvent / CompositionEvent). Each is a thin wrapper that builds the
// base Event object then stamps in the dictionary fields with the spec
// defaults. __class_name__ is set so `instanceof` works via the name fallback.
// ============================================================================

static Item read_init(Item init, const char* key) {
    if (init.item == 0) return ItemNull;
    TypeId t = get_type_id(init);
    if (t != LMD_TYPE_MAP && t != LMD_TYPE_OBJECT && t != LMD_TYPE_VMAP) return ItemNull;
    Item k = (Item){.item = s2it(heap_create_name(key))};
    return js_property_get(init, k);
}

static bool init_present(Item v) {
    if (v.item == 0) return false;
    TypeId t = get_type_id(v);
    return t != LMD_TYPE_NULL && t != LMD_TYPE_UNDEFINED;
}

static bool init_bool(Item init, const char* key, bool def) {
    Item v = read_init(init, key);
    if (!init_present(v)) return def;
    return js_is_truthy(v);
}

static int init_int(Item init, const char* key, int def) {
    Item v = read_init(init, key);
    if (!init_present(v)) return def;
    Item num = js_to_number(v);
    TypeId t = get_type_id(num);
    if (t == LMD_TYPE_INT) return (int)it2i(num);
    if (t == LMD_TYPE_INT64) return (int)it2l(num);
    if (t == LMD_TYPE_FLOAT) return (int)it2d(num);
    return def;
}

static double init_double(Item init, const char* key, double def) {
    Item v = read_init(init, key);
    if (!init_present(v)) return def;
    Item num = js_to_number(v);
    TypeId t = get_type_id(num);
    if (t == LMD_TYPE_FLOAT) return it2d(num);
    if (t == LMD_TYPE_INT) return (double)it2i(num);
    if (t == LMD_TYPE_INT64) return (double)it2l(num);
    return def;
}

static const char* init_str(Item init, const char* key, const char* def) {
    Item v = read_init(init, key);
    if (!init_present(v)) return def;
    const char* s = fn_to_cstr(v);
    return s ? s : def;
}

static Item init_item(Item init, const char* key) {
    Item v = read_init(init, key);
    if (!init_present(v)) return ItemNull;
    return v;
}

static void stamp_class(Item ev, const char* name) {
    js_property_set(ev,
        (Item){.item = s2it(heap_create_name("__class_name__"))},
        (Item){.item = s2it(heap_create_name(name))});
}

// EventModifierInit dict members shared by Mouse/Keyboard.
static void stamp_modifiers(Item ev, Item init) {
    event_set_bool(ev, "ctrlKey",  init_bool(init, "ctrlKey", false));
    event_set_bool(ev, "shiftKey", init_bool(init, "shiftKey", false));
    event_set_bool(ev, "altKey",   init_bool(init, "altKey", false));
    event_set_bool(ev, "metaKey",  init_bool(init, "metaKey", false));
    event_set_bool(ev, "modifierAltGraph", init_bool(init, "modifierAltGraph", false));
    event_set_bool(ev, "modifierCapsLock", init_bool(init, "modifierCapsLock", false));
    event_set_bool(ev, "modifierFn", init_bool(init, "modifierFn", false));
    event_set_bool(ev, "modifierFnLock", init_bool(init, "modifierFnLock", false));
    event_set_bool(ev, "modifierHyper", init_bool(init, "modifierHyper", false));
    event_set_bool(ev, "modifierNumLock", init_bool(init, "modifierNumLock", false));
    event_set_bool(ev, "modifierScrollLock", init_bool(init, "modifierScrollLock", false));
    event_set_bool(ev, "modifierSuper", init_bool(init, "modifierSuper", false));
    event_set_bool(ev, "modifierSymbol", init_bool(init, "modifierSymbol", false));
    event_set_bool(ev, "modifierSymbolLock", init_bool(init, "modifierSymbolLock", false));
}

extern "C" Item js_event_get_modifier_state(Item key_arg) {
    Item ev = js_get_this();
    if (get_type_id(ev) != LMD_TYPE_MAP) return (Item){.item = ITEM_FALSE};
    const char* key = fn_to_cstr(key_arg);
    if (!key) return (Item){.item = ITEM_FALSE};
    char buf[64]; buf[0] = 0;
    if (strcmp(key, "Alt") == 0) snprintf(buf, sizeof(buf), "altKey");
    else if (strcmp(key, "Control") == 0) snprintf(buf, sizeof(buf), "ctrlKey");
    else if (strcmp(key, "Shift") == 0)   snprintf(buf, sizeof(buf), "shiftKey");
    else if (strcmp(key, "Meta") == 0)    snprintf(buf, sizeof(buf), "metaKey");
    else snprintf(buf, sizeof(buf), "modifier%s", key);
    Item k = (Item){.item = s2it(heap_create_name(buf))};
    Item v = js_property_get(ev, k);
    return (Item){.item = (js_is_truthy(v)) ? ITEM_TRUE : ITEM_FALSE};
}

// Build a UIEvent base. `view` is constrained to be Window, null, or undefined
// (per IDL); throws TypeError if a non-Window/non-null value is supplied.
static Item build_ui_event(const char* type, Item init, const char* class_name) {
    Item ev = js_create_event_init(type ? type : "",
        init_bool(init, "bubbles", false),
        init_bool(init, "cancelable", false),
        init_bool(init, "composed", false));
    Item view = init_item(init, "view");
    // Per IDL view is Window? — we accept null/undefined or a value that looks
    // like the global window object. Reject other types with TypeError.
    if (init_present(view)) {
        TypeId vt = get_type_id(view);
        if (vt != LMD_TYPE_MAP && vt != LMD_TYPE_OBJECT && vt != LMD_TYPE_VMAP) {
            Item n = (Item){.item = s2it(heap_create_name("TypeError"))};
            Item m = (Item){.item = s2it(heap_create_name(
                "Failed to construct event: view member is not of type Window."))};
            js_throw_value(js_new_error_with_name(n, m));
            return make_js_undefined();
        }
        event_set_item(ev, "view", view);
    } else {
        event_set_item(ev, "view", ItemNull);
    }
    event_set_int(ev, "detail", init_int(init, "detail", 0));
    stamp_class(ev, class_name);
    return ev;
}

extern "C" Item js_ctor_ui_event_fn(Item type_arg, Item init_arg) {
    return build_ui_event(fn_to_cstr(type_arg), init_arg, "UIEvent");
}

extern "C" Item js_ctor_focus_event_fn(Item type_arg, Item init_arg) {
    Item ev = build_ui_event(fn_to_cstr(type_arg), init_arg, "FocusEvent");
    if (js_check_exception()) return make_js_undefined();
    event_set_item(ev, "relatedTarget", init_item(init_arg, "relatedTarget"));
    return ev;
}

extern "C" Item js_ctor_mouse_event_fn(Item type_arg, Item init_arg) {
    Item ev = build_ui_event(fn_to_cstr(type_arg), init_arg, "MouseEvent");
    if (js_check_exception()) return make_js_undefined();
    stamp_modifiers(ev, init_arg);
    event_set_int(ev, "screenX", init_int(init_arg, "screenX", 0));
    event_set_int(ev, "screenY", init_int(init_arg, "screenY", 0));
    event_set_int(ev, "clientX", init_int(init_arg, "clientX", 0));
    event_set_int(ev, "clientY", init_int(init_arg, "clientY", 0));
    event_set_int(ev, "pageX",   init_int(init_arg, "pageX", 0));
    event_set_int(ev, "pageY",   init_int(init_arg, "pageY", 0));
    event_set_int(ev, "x",       init_int(init_arg, "clientX", 0));
    event_set_int(ev, "y",       init_int(init_arg, "clientY", 0));
    event_set_int(ev, "offsetX", 0);
    event_set_int(ev, "offsetY", 0);
    event_set_int(ev, "movementX", init_int(init_arg, "movementX", 0));
    event_set_int(ev, "movementY", init_int(init_arg, "movementY", 0));
    event_set_int(ev, "button",  init_int(init_arg, "button", 0));
    event_set_int(ev, "buttons", init_int(init_arg, "buttons", 0));
    event_set_item(ev, "relatedTarget", init_item(init_arg, "relatedTarget"));
    Item gms_key = (Item){.item = s2it(heap_create_name("getModifierState"))};
    js_property_set(ev, gms_key, js_new_function((void*)js_event_get_modifier_state, 1));
    return ev;
}

extern "C" Item js_ctor_wheel_event_fn(Item type_arg, Item init_arg) {
    Item ev = js_ctor_mouse_event_fn(type_arg, init_arg);
    if (js_check_exception()) return make_js_undefined();
    stamp_class(ev, "WheelEvent");
    event_set_double(ev, "deltaX", init_double(init_arg, "deltaX", 0.0));
    event_set_double(ev, "deltaY", init_double(init_arg, "deltaY", 0.0));
    event_set_double(ev, "deltaZ", init_double(init_arg, "deltaZ", 0.0));
    event_set_int(ev, "deltaMode", init_int(init_arg, "deltaMode", 0));
    event_set_int(ev, "DOM_DELTA_PIXEL", 0);
    event_set_int(ev, "DOM_DELTA_LINE", 1);
    event_set_int(ev, "DOM_DELTA_PAGE", 2);
    return ev;
}

extern "C" Item js_ctor_keyboard_event_fn(Item type_arg, Item init_arg) {
    Item ev = build_ui_event(fn_to_cstr(type_arg), init_arg, "KeyboardEvent");
    if (js_check_exception()) return make_js_undefined();
    stamp_modifiers(ev, init_arg);
    event_set_str(ev, "key",  init_str(init_arg, "key", ""));
    event_set_str(ev, "code", init_str(init_arg, "code", ""));
    event_set_int(ev, "location",     init_int(init_arg, "location", 0));
    event_set_bool(ev, "repeat",      init_bool(init_arg, "repeat", false));
    event_set_bool(ev, "isComposing", init_bool(init_arg, "isComposing", false));
    event_set_int(ev, "charCode",     init_int(init_arg, "charCode", 0));
    event_set_int(ev, "keyCode",      init_int(init_arg, "keyCode", 0));
    event_set_int(ev, "which",        init_int(init_arg, "which", 0));
    event_set_int(ev, "DOM_KEY_LOCATION_STANDARD", 0);
    event_set_int(ev, "DOM_KEY_LOCATION_LEFT", 1);
    event_set_int(ev, "DOM_KEY_LOCATION_RIGHT", 2);
    event_set_int(ev, "DOM_KEY_LOCATION_NUMPAD", 3);
    Item gms_key = (Item){.item = s2it(heap_create_name("getModifierState"))};
    js_property_set(ev, gms_key, js_new_function((void*)js_event_get_modifier_state, 1));
    return ev;
}

extern "C" Item js_ctor_composition_event_fn(Item type_arg, Item init_arg) {
    Item ev = build_ui_event(fn_to_cstr(type_arg), init_arg, "CompositionEvent");
    if (js_check_exception()) return make_js_undefined();
    event_set_str(ev, "data", init_str(init_arg, "data", ""));
    return ev;
}

extern "C" Item js_ctor_input_event_fn(Item type_arg, Item init_arg) {
    Item ev = build_ui_event(fn_to_cstr(type_arg), init_arg, "InputEvent");
    if (js_check_exception()) return make_js_undefined();
    event_set_str(ev, "data", init_str(init_arg, "data", ""));
    event_set_str(ev, "inputType", init_str(init_arg, "inputType", ""));
    event_set_bool(ev, "isComposing", init_bool(init_arg, "isComposing", false));
    event_set_item(ev, "dataTransfer", init_item(init_arg, "dataTransfer"));
    return ev;
}

extern "C" Item js_ctor_pointer_event_fn(Item type_arg, Item init_arg) {
    Item ev = js_ctor_mouse_event_fn(type_arg, init_arg);
    if (js_check_exception()) return make_js_undefined();
    stamp_class(ev, "PointerEvent");
    event_set_int(ev, "pointerId", init_int(init_arg, "pointerId", 0));
    event_set_double(ev, "width",  init_double(init_arg, "width", 1.0));
    event_set_double(ev, "height", init_double(init_arg, "height", 1.0));
    event_set_double(ev, "pressure", init_double(init_arg, "pressure", 0.0));
    event_set_double(ev, "tangentialPressure", init_double(init_arg, "tangentialPressure", 0.0));
    event_set_int(ev, "tiltX", init_int(init_arg, "tiltX", 0));
    event_set_int(ev, "tiltY", init_int(init_arg, "tiltY", 0));
    event_set_int(ev, "twist", init_int(init_arg, "twist", 0));
    event_set_str(ev, "pointerType", init_str(init_arg, "pointerType", ""));
    event_set_bool(ev, "isPrimary",  init_bool(init_arg, "isPrimary", false));
    return ev;
}

// Build a synthetic click MouseEvent (composed=true, bubbles=true, cancelable=true)
// for `HTMLElement.prototype.click()`. Per spec, all coordinate / button fields
// default to 0; modifiers all false; detail = 1.
extern "C" Item js_create_click_mouse_event(void) {
    Item init = js_new_object();
    event_set_bool(init, "bubbles", true);
    event_set_bool(init, "cancelable", true);
    event_set_bool(init, "composed", true);
    event_set_int(init, "detail", 1);
    Item type_str = (Item){.item = s2it(heap_create_name("click"))};
    return js_ctor_mouse_event_fn(type_str, init);
}

// ============================================================================
// Native event factories — entry points used by the Radiant input bridge.
// All set isTrusted=true (per spec, browser-fired events are trusted) and
// stamp standard EventInit defaults appropriate for each interface.
// ============================================================================

static void stamp_modifier_init(Item init, bool ctrl, bool shift, bool alt, bool meta) {
    event_set_bool(init, "ctrlKey",  ctrl);
    event_set_bool(init, "shiftKey", shift);
    event_set_bool(init, "altKey",   alt);
    event_set_bool(init, "metaKey",  meta);
}

extern "C" Item js_create_native_mouse_event(const char* type,
    int client_x, int client_y,
    int button, int buttons,
    bool ctrl, bool shift, bool alt, bool meta,
    int detail, Item related_target)
{
    Item init = js_new_object();
    event_set_bool(init, "bubbles", true);
    event_set_bool(init, "cancelable", true);
    event_set_bool(init, "composed", true);
    event_set_int(init, "detail", detail);
    event_set_int(init, "clientX", client_x);
    event_set_int(init, "clientY", client_y);
    event_set_int(init, "screenX", client_x);
    event_set_int(init, "screenY", client_y);
    event_set_int(init, "pageX", client_x);
    event_set_int(init, "pageY", client_y);
    event_set_int(init, "button", button);
    event_set_int(init, "buttons", buttons);
    stamp_modifier_init(init, ctrl, shift, alt, meta);
    if (related_target.item != 0) {
        event_set_item(init, "relatedTarget", related_target);
    }
    Item type_str = (Item){.item = s2it(heap_create_name(type ? type : ""))};
    Item ev = js_ctor_mouse_event_fn(type_str, init);
    event_set_bool(ev, "isTrusted", true);
    return ev;
}

extern "C" Item js_create_native_keyboard_event(const char* type,
    const char* key, const char* code,
    bool ctrl, bool shift, bool alt, bool meta,
    bool repeat)
{
    Item init = js_new_object();
    event_set_bool(init, "bubbles", true);
    event_set_bool(init, "cancelable", true);
    event_set_bool(init, "composed", true);
    if (key) event_set_str(init, "key", key);
    if (code) event_set_str(init, "code", code);
    event_set_bool(init, "repeat", repeat);
    stamp_modifier_init(init, ctrl, shift, alt, meta);
    Item type_str = (Item){.item = s2it(heap_create_name(type ? type : ""))};
    Item ev = js_ctor_keyboard_event_fn(type_str, init);
    event_set_bool(ev, "isTrusted", true);
    return ev;
}

extern "C" Item js_create_native_focus_event(const char* type, Item related_target) {
    Item init = js_new_object();
    // focus/blur do NOT bubble; focusin/focusout DO. Caller decides via type.
    bool bubbles = (type && (strcmp(type, "focusin") == 0 || strcmp(type, "focusout") == 0));
    event_set_bool(init, "bubbles", bubbles);
    event_set_bool(init, "cancelable", false);
    event_set_bool(init, "composed", true);
    if (related_target.item != 0) {
        event_set_item(init, "relatedTarget", related_target);
    }
    Item type_str = (Item){.item = s2it(heap_create_name(type ? type : ""))};
    Item ev = js_ctor_focus_event_fn(type_str, init);
    event_set_bool(ev, "isTrusted", true);
    return ev;
}

extern "C" Item js_create_native_wheel_event(const char* type,
    int client_x, int client_y,
    double delta_x, double delta_y,
    int buttons,
    bool ctrl, bool shift, bool alt, bool meta)
{
    Item init = js_new_object();
    event_set_bool(init, "bubbles", true);
    event_set_bool(init, "cancelable", true);
    event_set_bool(init, "composed", true);
    event_set_int(init, "clientX", client_x);
    event_set_int(init, "clientY", client_y);
    event_set_int(init, "screenX", client_x);
    event_set_int(init, "screenY", client_y);
    event_set_int(init, "buttons", buttons);
    event_set_double(init, "deltaX", delta_x);
    event_set_double(init, "deltaY", delta_y);
    event_set_int(init, "deltaMode", 0); // DOM_DELTA_PIXEL
    stamp_modifier_init(init, ctrl, shift, alt, meta);
    Item type_str = (Item){.item = s2it(heap_create_name(type ? type : "wheel"))};
    Item ev = js_ctor_wheel_event_fn(type_str, init);
    event_set_bool(ev, "isTrusted", true);
    return ev;
}

extern "C" bool js_event_is_default_prevented(Item event) {
    if (get_type_id(event) != LMD_TYPE_MAP) return false;
    return event_flag_get(event, "__default_prevented");
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
        // window currentTarget is globalThis (the window object).
        return js_get_global_this();
    }
    if (key == (void*)&_document_sentinel) {
        return js_get_document_object_value();
    }
    // Try DOM node first
    Item dom = js_dom_wrap_element(key);
    if (dom.item != 0 && get_type_id(dom) != LMD_TYPE_NULL) return dom;
    // Plain JS-object EventTarget: key is a container pointer (Map/Object/VMap).
    // Container types store the TypeId at offset 0, so a raw container pointer
    // round-trips as an untagged Item that get_type_id reads from *key.
    if (key) {
        Item it; it.item = 0; it.container = (Container*)key;
        TypeId tid = get_type_id(it);
        if (tid == LMD_TYPE_MAP || tid == LMD_TYPE_OBJECT || tid == LMD_TYPE_VMAP) {
            return it;
        }
    }
    return ItemNull;
}

// fire listeners on a specific node for a given phase. `reported_phase`, if
// non-zero, overrides the eventPhase value visible to listeners (used at the
// target node so capture-then-bubble sub-passes both report AT_TARGET).
static void fire_listeners(void* key, const char* type, Item event, int phase,
                           int reported_phase = 0) {
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

    // set eventPhase (use reported_phase if specified; otherwise raw phase)
    event_set_int(event, "eventPhase", reported_phase ? reported_phase : phase);

    // set currentTarget
    Item ct_key = (Item){.item = s2it(heap_create_name("currentTarget"))};
    js_property_set(event, ct_key, wrap_path_key(key));

    // Forward declarations (defined elsewhere).
    extern int js_check_exception(void);
    extern Item js_clear_exception(void);

    // Check stop-immediate flag against per-event slot.
    #define _STOP_IMM event_flag_get(event, "__stop_imm")

    // Fire on<type> IDL handler first (per HTML spec ordering when the IDL
    // handler was set first).
    if (has_on && !_STOP_IMM) {
        Item args[1] = { event };
        js_call_function(on_handler, target_item, args, 1);
        if (js_check_exception()) {
            // Report and swallow — dispatch must continue.
            Item err = js_clear_exception();
            log_error("event handler (on%s) threw an exception", type);
            report_exception_to_window_onerror(err, type);
        }
    }

    if (!nl) return;

    // Build snapshot of listener pointers — mutations during dispatch must
    // not perturb iteration order. Skip tombstoned/aborted/non-matching/wrong-phase
    // entries up front.
    EventListener** snap = (EventListener**)alloca(sizeof(EventListener*) * nl->count);
    int snap_count = 0;
    for (int i = 0; i < nl->count; i++) {
        EventListener* el = &nl->items[i];
        if (el->removed) continue;
        if (strcmp(el->type, type) != 0) continue;
        // phase filter — capturing fires only capture listeners; bubbling only
        // non-capture; AT_TARGET fires both.
        if (phase == 1 && !el->capture) continue;
        if (phase == 3 && el->capture) continue;
        // signal — if its AbortSignal aborted, treat as removed
        if (signal_is_aborted(el->signal)) { el->removed = true; continue; }
        snap[snap_count++] = el;
    }

    // dispatch loop over snapshot
    for (int i = 0; i < snap_count; i++) {
        if (_STOP_IMM) break;
        EventListener* el = snap[i];
        // re-check tombstone in case a prior listener removed this one
        if (el->removed) continue;
        if (signal_is_aborted(el->signal)) { el->removed = true; continue; }

        // Resolve callback — function or {handleEvent}
        Item callback = el->callback;
        Item this_for_call = wrap_path_key(key);
        TypeId ct = get_type_id(callback);
        if (ct != LMD_TYPE_FUNC) {
            // EventListener WebIDL: if value is an object, call handleEvent on it
            Item he_key = (Item){.item = s2it(heap_create_name("handleEvent"))};
            Item he = js_property_get(callback, he_key);
            if (get_type_id(he) != LMD_TYPE_FUNC) continue; // not callable
            // per spec, `this` is the EventListener object itself
            this_for_call = callback;
            callback = he;
        }

        // Set passive flag on the event so preventDefault no-ops within this
        // listener (per HTML spec, passive listeners cannot cancel).
        bool was_passive = event_flag_get(event, "__in_passive");
        event_set_bool(event, "__in_passive", el->passive);

        // Mark for once-removal BEFORE invocation so that recursion / re-add
        // sees the slot as removed.
        if (el->once) el->removed = true;

        // call the callback with event as argument; isolate exceptions per spec
        Item args[1] = { event };
        js_call_function(callback, this_for_call, args, 1);
        if (js_check_exception()) {
            Item err = js_clear_exception();
            log_error("event listener for '%s' threw; continuing dispatch", type);
            report_exception_to_window_onerror(err, type);
        }

        // restore previous passive context
        event_set_bool(event, "__in_passive", was_passive);
    }

    #undef _STOP_IMM
}

Item js_dom_dispatch_event(Item elem_item, Item event_item) {
    // Per spec: dispatchEvent(null) / dispatchEvent(non-Event) throws TypeError.
    TypeId evt_tid = get_type_id(event_item);
    if (event_item.item == 0 || evt_tid == LMD_TYPE_NULL ||
        evt_tid == LMD_TYPE_UNDEFINED) {
        Item n = (Item){.item = s2it(heap_create_name("TypeError"))};
        Item m = (Item){.item = s2it(heap_create_name(
            "Failed to execute 'dispatchEvent' on 'EventTarget': "
            "parameter 1 is not of type 'Event'."))};
        js_throw_value(js_new_error_with_name(n, m));
        return (Item){.item = ITEM_FALSE};
    }
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

    // ------------------------------------------------------------------
    // HTML §6.4.4 click activation behavior — pre-activation hook.
    // For checkbox/radio, toggle the live "checkedness" before dispatch
    // so listeners see the new value. If the event is canceled we will
    // restore. We run pre-activation even on disabled elements (per
    // wpt: "disabled checkbox should still be checked when clicked").
    // ------------------------------------------------------------------
    void* act_target = nullptr;        // DomElement* target of activation, NULL if none
    int act_kind = 0;                  // 1 = checkbox/radio toggle, 2 = submit
    bool act_old_checked = false;
    bool act_disabled = false;         // disabled at pre-activation time
    if (strcmp(type, "click") == 0) {
        // Per HTML §6.4.4 + §4.10.5.3, activation behavior only fires
        // when the event's class is `MouseEvent` (or descendant such as
        // PointerEvent). A plain `Event("click")` does not trigger any
        // legacy-pre-activation steps.
        Item cls_key = (Item){.item = s2it(heap_create_name("__class_name__"))};
        Item cls_val = js_property_get(event_item, cls_key);
        const char* cls = fn_to_cstr(cls_val);
        bool is_mouse_event = cls && (
            strcmp(cls, "MouseEvent") == 0 ||
            strcmp(cls, "PointerEvent") == 0 ||
            strcmp(cls, "WheelEvent") == 0);
        void* node_ptr = is_mouse_event ? js_dom_unwrap_element(elem_item) : nullptr;
        if (node_ptr) {
            DomNode* node = (DomNode*)node_ptr;
            if (node->is_element()) {
                void* el = node_ptr;  // DomElement*
                act_disabled = js_dom_is_disabled(el);
                if (js_dom_is_checkbox_or_radio(el)) {
                    act_target = el;
                    act_kind = 1;
                    act_old_checked = js_dom_get_checkedness(el);
                    const char* itype = js_dom_input_type_lower(el);
                    if (strcmp(itype, "radio") == 0) {
                        // Per HTML, clicking a radio sets it to checked
                        // (group exclusion not implemented headlessly).
                        js_dom_set_checkedness(el, true);
                    } else {
                        // Checkbox: toggle.
                        js_dom_set_checkedness(el, !act_old_checked);
                    }
                }
                // Submit-button activation kind is identified for
                // post-activation form submission.
                const char* tag = js_dom_tag_name_raw(el);
                if (!act_kind && tag) {
                    if (strcasecmp(tag, "input") == 0) {
                        const char* itype = js_dom_input_type_lower(el);
                        if (strcmp(itype, "submit") == 0 || strcmp(itype, "image") == 0) {
                            act_target = el; act_kind = 2;
                        }
                    } else if (strcasecmp(tag, "button") == 0) {
                        // Default button type is "submit".
                        const char* btype = js_dom_input_type_lower(el);
                        if (strcmp(btype, "text") == 0 /* default */ ||
                            strcmp(btype, "submit") == 0) {
                            act_target = el; act_kind = 2;
                        }
                    }
                }
            }
        }
    }

    // Spec: throw InvalidStateError DOMException if event is already being dispatched.
    if (event_flag_get(event_item, "__dispatch_flag")) {
        Item n = (Item){.item = s2it(heap_create_name("InvalidStateError"))};
        Item m = (Item){.item = s2it(heap_create_name(
            "Failed to execute 'dispatchEvent' on 'EventTarget': "
            "The event is already being dispatched."))};
        js_throw_value(js_new_error_with_name(n, m));
        return (Item){.item = ITEM_FALSE};
    }

    // set target / srcElement (per DOM spec, dispatch sets target to the
    // current dispatch target — re-dispatch updates it).
    Item target_key = (Item){.item = s2it(heap_create_name("target"))};
    Item src_key = (Item){.item = s2it(heap_create_name("srcElement"))};
    js_property_set(event_item, target_key, elem_item);
    js_property_set(event_item, src_key, elem_item);

    // Mark event as dispatching.
    event_set_bool(event_item, "__dispatch_flag", true);

    // Per DOM spec, the propagation flags (stop, stop-immediate, canceled)
    // are NOT reset at the start of dispatch. They persist whether set
    // before-dispatch (legacy: stopPropagation()/preventDefault() called
    // before dispatchEvent) or by a previous re-dispatch.

    // reset propagation state (legacy thread-locals — transitional)
    _stop_propagation = event_flag_get(event_item, "__stop_prop");
    _stop_immediate = event_flag_get(event_item, "__stop_imm");
    _default_prevented = event_flag_get(event_item, "__default_prevented");

    // build propagation path (target → ... → document → window)
    void* path[128];
    int path_len = build_path(elem_item, path, 128);

    if (path_len == 0) {
        event_set_bool(event_item, "__dispatch_flag", false);
        return (Item){.item = ITEM_TRUE};
    }

    // Legacy IE-style `window.event`: set to the in-flight event for the
    // duration of dispatch, restored to its prior value (typically
    // `undefined`) afterwards. Per HTML, the slot must read `undefined`
    // when called inside a Shadow Tree listener (we don't model Shadow
    // DOM headlessly, so we always set it).
    Item global = js_get_global_this();
    Item event_key = (Item){.item = s2it(heap_create_name("event"))};
    Item prev_global_event = js_property_get(global, event_key);
    js_property_set(global, event_key, event_item);

    #define _STOP_PROP (_stop_propagation || event_flag_get(event_item, "__stop_prop") \
        || event_flag_get(event_item, "cancelBubble"))

    // path[0] = target, path[1] = parent, ... path[n-1] = window
    // Phase 1: Capture — from root down to target (exclusive)
    for (int i = path_len - 1; i > 0; i--) {
        if (_STOP_PROP) break;
        fire_listeners(path[i], type, event_item, 1);  // CAPTURING_PHASE
    }

    // Phase 2: Target — per spec, capture-listeners run first then bubble
    // listeners, both reported with eventPhase = AT_TARGET (2).
    if (!_STOP_PROP) {
        fire_listeners(path[0], type, event_item, 1, 2);
    }
    if (!_STOP_PROP) {
        fire_listeners(path[0], type, event_item, 3, 2);
    }

    // Phase 3: Bubble — from target parent up to root
    if (bubbles) {
        for (int i = 1; i < path_len; i++) {
            if (_STOP_PROP) break;
            fire_listeners(path[i], type, event_item, 3);  // BUBBLING_PHASE
        }
    }

    #undef _STOP_PROP

    // set eventPhase to NONE after dispatch
    event_set_int(event_item, "eventPhase", 0);

    // currentTarget is reset to null after dispatch (per spec).
    Item ct_key = (Item){.item = s2it(heap_create_name("currentTarget"))};
    js_property_set(event_item, ct_key, ItemNull);

    // Per DOM spec §2.10 step 26: at the end of dispatch, unset stop
    // propagation flag, stop immediate propagation flag, and dispatch flag.
    // (canceled / defaultPrevented flag PERSISTS across dispatches.)
    event_set_bool(event_item, "__stop_prop", false);
    event_set_bool(event_item, "__stop_imm", false);
    event_set_bool(event_item, "cancelBubble", false);

    // Clear dispatching flag.
    event_set_bool(event_item, "__dispatch_flag", false);

    // Restore the previous `window.event` value (legacy IE-style).
    js_property_set(global, event_key, prev_global_event);

    // Compact tombstoned listeners now that dispatch is done. Walk all
    // touched nodes in the path.
    for (int i = 0; i < path_len; i++) {
        NodeListeners* nl = find_listeners(path[i]);
        if (nl) nl_compact(nl);
    }

    bool prevented = _default_prevented || event_flag_get(event_item, "__default_prevented");
    if (prevented) {
        event_set_bool(event_item, "defaultPrevented", true);
        event_set_bool(event_item, "returnValue", false);
    }

    // ------------------------------------------------------------------
    // HTML §6.4.4 click activation behavior — post-activation hook.
    // - Canceled: undo any pre-activation state changes.
    // - Otherwise: run the activation behavior. For checkbox/radio
    //   that means firing `input` then `change` events synchronously.
    //   For submit buttons it means firing `submit` on the form.
    // We skip the post-activation steps when the element was disabled
    // at pre-activation time (disabled controls don't fire change events
    // or submit forms).
    // ------------------------------------------------------------------
    if (act_kind == 1 && act_target) {
        if (prevented) {
            // Restore prior checkedness.
            js_dom_set_checkedness(act_target, act_old_checked);
        } else if (!act_disabled) {
            // Fire `input` (bubbles, non-cancelable) then `change`.
            Item self_item = js_dom_wrap_element(act_target);
            Item input_ev = js_create_event("input", true, false);
            js_dom_dispatch_event(self_item, input_ev);
            Item change_ev = js_create_event("change", true, false);
            js_dom_dispatch_event(self_item, change_ev);
        }
    } else if (act_kind == 2 && act_target && !prevented && !act_disabled) {
        // Submit-button activation: walk up to the owning <form> and
        // dispatch a `submit` event (bubbles, cancelable). Headless mode
        // performs no navigation regardless of cancel state.
        // Re-check disabled in case the click listener disabled us.
        if (js_dom_is_disabled(act_target)) {
            // listener disabled the control mid-flight — skip submit.
        } else if (!js_dom_is_connected(act_target)) {
            // disconnected forms must not submit (HTML §4.10.21.3).
        } else {
            DomElement* el = (DomElement*)act_target;
            DomNode* p = el->parent;
            while (p && p->is_element()) {
                DomElement* pe = (DomElement*)p;
                if (pe->tag_name && strcasecmp(pe->tag_name, "form") == 0) {
                    Item form_item = js_dom_wrap_element(pe);
                    Item submit_ev = js_create_event("submit", true, true);
                    js_dom_dispatch_event(form_item, submit_ev);
                    break;
                }
                p = p->parent;
            }
        }
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
