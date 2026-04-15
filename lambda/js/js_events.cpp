/**
 * js_events.cpp — Node.js-style 'events' module for LambdaJS
 *
 * Provides EventEmitter class with on, once, off, emit, etc.
 * Registered as built-in module 'events' via js_module_get().
 *
 * EventEmitter instances store listeners in a hidden __events__ property
 * which maps event names to arrays of listener functions.
 */
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"

#include <cstring>

extern Input* js_input;

static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

static Item make_string_item(const char* str, int len) {
    if (!str) return ItemNull;
    String* s = heap_create_name(str, (size_t)len);
    return (Item){.item = s2it(s)};
}

static Item make_string_item(const char* str) {
    if (!str) return ItemNull;
    return make_string_item(str, (int)strlen(str));
}

static Item events_key;       // "__events__"
static Item once_key;         // "__once__"
static Item max_listeners_key; // "__maxListeners__"
static bool keys_initialized = false;

static void ensure_keys() {
    if (keys_initialized) return;
    events_key = make_string_item("__events__");
    once_key = make_string_item("__once__");
    max_listeners_key = make_string_item("__maxListeners__");
    keys_initialized = true;
}

// Get or create the __events__ map on an emitter object
static Item get_events_map(Item emitter) {
    ensure_keys();
    Item map = js_property_get(emitter, events_key);
    if (map.item == 0 || get_type_id(map) == LMD_TYPE_UNDEFINED) {
        map = js_new_object();
        js_property_set(emitter, events_key, map);
    }
    return map;
}

// Get or create the __once__ set (array of functions marked as once) on an emitter
static Item get_once_set(Item emitter) {
    ensure_keys();
    Item set = js_property_get(emitter, once_key);
    if (set.item == 0 || get_type_id(set) == LMD_TYPE_UNDEFINED) {
        set = js_array_new(0);
        js_property_set(emitter, once_key, set);
    }
    return set;
}

// Check if a function is in the once set
static bool is_once_listener(Item emitter, Item fn) {
    Item set = get_once_set(emitter);
    int64_t len = js_array_length(set);
    for (int64_t i = 0; i < len; i++) {
        Item f = js_array_get_int(set, i);
        if (f.item == fn.item) return true;
    }
    return false;
}

// Get listeners array for a given event name
static Item get_listeners_array(Item emitter, Item event_name) {
    Item map = get_events_map(emitter);
    Item arr = js_property_get(map, event_name);
    if (arr.item == 0 || get_type_id(arr) == LMD_TYPE_UNDEFINED) {
        arr = js_array_new(0);
        js_property_set(map, event_name, arr);
    }
    return arr;
}

// ─── emitter.on(event, listener) ─────────────────────────────────────────────
// Adds listener to end of listeners array. Returns emitter for chaining.
extern "C" Item js_ee_on(Item emitter, Item event_name, Item listener) {
    if (emitter.item == 0) return ItemNull;
    Item arr = get_listeners_array(emitter, event_name);
    js_array_push(arr, listener);
    return emitter;
}

// ─── emitter.once(event, listener) ───────────────────────────────────────────
// Adds a one-time listener. Returns emitter for chaining.
extern "C" Item js_ee_once(Item emitter, Item event_name, Item listener) {
    if (emitter.item == 0) return ItemNull;
    Item arr = get_listeners_array(emitter, event_name);
    js_array_push(arr, listener);
    // mark this function as once
    Item set = get_once_set(emitter);
    js_array_push(set, listener);
    return emitter;
}

// ─── emitter.off(event, listener) / removeListener ──────────────────────────
// Removes most-recently added instance of listener. Returns emitter.
extern "C" Item js_ee_off(Item emitter, Item event_name, Item listener) {
    if (emitter.item == 0) return ItemNull;
    Item map = get_events_map(emitter);
    Item arr = js_property_get(map, event_name);
    if (arr.item == 0 || get_type_id(arr) == LMD_TYPE_UNDEFINED) return emitter;

    int64_t len = js_array_length(arr);
    // find last occurrence
    for (int64_t i = len - 1; i >= 0; i--) {
        Item f = js_array_get_int(arr, i);
        if (f.item == listener.item) {
            // rebuild array without this element
            Item new_arr = js_array_new(0);
            for (int64_t j = 0; j < len; j++) {
                if (j != i) js_array_push(new_arr, js_array_get_int(arr, j));
            }
            js_property_set(map, event_name, new_arr);
            break;
        }
    }
    return emitter;
}

// ─── emitter.emit(event, ...args) ───────────────────────────────────────────
// Calls each listener with args. Returns true if had listeners.
extern "C" Item js_ee_emit(Item emitter, Item event_name, Item args_rest) {
    if (emitter.item == 0) return (Item){.item = b2it(false)};
    Item map = get_events_map(emitter);
    Item arr = js_property_get(map, event_name);

    bool has_listeners = (arr.item != 0 && get_type_id(arr) != LMD_TYPE_UNDEFINED &&
                          js_array_length(arr) > 0);

    // Node.js error event behavior: throw if no listeners for 'error'
    if (!has_listeners) {
        if (get_type_id(event_name) == LMD_TYPE_STRING) {
            String* en = it2s(event_name);
            if (en->len == 5 && memcmp(en->chars, "error", 5) == 0) {
                // throw the error argument, or a generic error
                extern void js_throw_value(Item error);
                extern Item js_new_error_with_name(Item type_name, Item message);
                Item err_arg = ItemNull;
                if (args_rest.item != 0 && get_type_id(args_rest) != LMD_TYPE_UNDEFINED) {
                    int64_t argc = js_array_length(args_rest);
                    if (argc > 0) err_arg = js_array_get_int(args_rest, 0);
                }
                if (err_arg.item == 0 || get_type_id(err_arg) == LMD_TYPE_UNDEFINED) {
                    Item type_n = make_string_item("Error");
                    Item msg = make_string_item("Unhandled 'error' event");
                    err_arg = js_new_error_with_name(type_n, msg);
                }
                js_throw_value(err_arg);
                return (Item){.item = b2it(false)};
            }
        }
        return (Item){.item = b2it(false)};
    }

    int64_t len = js_array_length(arr);

    // collect args from rest parameter array
    int64_t argc = 0;
    Item args[32];
    if (args_rest.item != 0 && get_type_id(args_rest) != LMD_TYPE_UNDEFINED) {
        argc = js_array_length(args_rest);
        if (argc > 32) argc = 32;
        for (int64_t i = 0; i < argc; i++) {
            args[i] = js_array_get_int(args_rest, i);
        }
    }

    // call each listener — snapshot the array first since once-listeners modify it
    Item snapshot = js_array_new(0);
    for (int64_t i = 0; i < len; i++) {
        js_array_push(snapshot, js_array_get_int(arr, i));
    }

    for (int64_t i = 0; i < len; i++) {
        Item fn = js_array_get_int(snapshot, i);
        js_call_function(fn, emitter, args, (int)argc);

        // if once-listener, remove it
        if (is_once_listener(emitter, fn)) {
            js_ee_off(emitter, event_name, fn);
            // also remove from once-set
            Item set = get_once_set(emitter);
            int64_t slen = js_array_length(set);
            Item new_set = js_array_new(0);
            for (int64_t j = 0; j < slen; j++) {
                Item f = js_array_get_int(set, j);
                if (f.item != fn.item) js_array_push(new_set, f);
            }
            js_property_set(emitter, once_key, new_set);
        }
    }

    return (Item){.item = b2it(true)};
}

// ─── emitter.removeAllListeners(event?) ─────────────────────────────────────
extern "C" Item js_ee_removeAllListeners(Item emitter, Item event_name) {
    if (emitter.item == 0) return emitter;
    Item map = get_events_map(emitter);
    if (event_name.item == 0 || get_type_id(event_name) == LMD_TYPE_UNDEFINED) {
        // remove all events
        js_property_set(emitter, events_key, js_new_object());
        js_property_set(emitter, once_key, js_array_new(0));
    } else {
        js_property_set(map, event_name, js_array_new(0));
    }
    return emitter;
}

// ─── emitter.listeners(event) ───────────────────────────────────────────────
// Returns a copy of the listeners array for event.
extern "C" Item js_ee_listeners(Item emitter, Item event_name) {
    if (emitter.item == 0) return js_array_new(0);
    Item map = get_events_map(emitter);
    Item arr = js_property_get(map, event_name);
    if (arr.item == 0 || get_type_id(arr) == LMD_TYPE_UNDEFINED) {
        return js_array_new(0);
    }
    // return a copy
    int64_t len = js_array_length(arr);
    Item copy = js_array_new(0);
    for (int64_t i = 0; i < len; i++) {
        js_array_push(copy, js_array_get_int(arr, i));
    }
    return copy;
}

// ─── emitter.listenerCount(event) ──────────────────────────────────────────
extern "C" Item js_ee_listenerCount(Item emitter, Item event_name) {
    if (emitter.item == 0) return (Item){.item = i2it(0)};
    Item map = get_events_map(emitter);
    Item arr = js_property_get(map, event_name);
    if (arr.item == 0 || get_type_id(arr) == LMD_TYPE_UNDEFINED) {
        return (Item){.item = i2it(0)};
    }
    return (Item){.item = i2it(js_array_length(arr))};
}

// ─── emitter.eventNames() ──────────────────────────────────────────────────
extern "C" Item js_ee_eventNames(Item emitter) {
    if (emitter.item == 0) return js_array_new(0);
    Item map = get_events_map(emitter);
    return js_object_keys(map);
}

// ─── emitter.setMaxListeners(n) ─────────────────────────────────────────────
extern "C" Item js_ee_setMaxListeners(Item emitter, Item n) {
    if (emitter.item == 0) return emitter;
    ensure_keys();
    js_property_set(emitter, max_listeners_key, n);
    return emitter;
}

// ─── emitter.getMaxListeners() ──────────────────────────────────────────────
extern "C" Item js_ee_getMaxListeners(Item emitter) {
    if (emitter.item == 0) return (Item){.item = i2it(10)};
    ensure_keys();
    Item val = js_property_get(emitter, max_listeners_key);
    if (val.item == 0 || get_type_id(val) == LMD_TYPE_UNDEFINED) {
        return (Item){.item = i2it(10)}; // default
    }
    return val;
}

// ─── emitter.prependListener(event, listener) ───────────────────────────────
// Adds listener to beginning of listeners array.
extern "C" Item js_ee_prependListener(Item emitter, Item event_name, Item listener) {
    if (emitter.item == 0) return ItemNull;
    Item arr = get_listeners_array(emitter, event_name);
    // rebuild with listener at front
    int64_t len = js_array_length(arr);
    Item new_arr = js_array_new((int)(len + 1));
    js_array_push(new_arr, listener);
    for (int64_t i = 0; i < len; i++) {
        js_array_push(new_arr, js_array_get_int(arr, i));
    }
    Item map = get_events_map(emitter);
    js_property_set(map, event_name, new_arr);
    return emitter;
}

// ─── emitter.prependOnceListener(event, listener) ───────────────────────────
extern "C" Item js_ee_prependOnceListener(Item emitter, Item event_name, Item listener) {
    if (emitter.item == 0) return ItemNull;
    Item arr = get_listeners_array(emitter, event_name);
    int64_t len = js_array_length(arr);
    Item new_arr = js_array_new((int)(len + 1));
    js_array_push(new_arr, listener);
    for (int64_t i = 0; i < len; i++) {
        js_array_push(new_arr, js_array_get_int(arr, i));
    }
    Item map = get_events_map(emitter);
    js_property_set(map, event_name, new_arr);
    // mark as once
    Item set = get_once_set(emitter);
    js_array_push(set, listener);
    return emitter;
}

// ─── new EventEmitter() constructor ─────────────────────────────────────────
extern "C" Item js_ee_constructor(void) {
    Item emitter = js_new_object();
    js_property_set(emitter, make_string_item("__class_name__"), make_string_item("EventEmitter"));
    // initialize storage
    js_property_set(emitter, events_key, js_new_object());
    js_property_set(emitter, once_key, js_array_new(0));
    return emitter;
}

// ─── Namespace ───────────────────────────────────────────────────────────────

static Item events_namespace = {0};

static void ee_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
}

extern "C" Item js_get_events_namespace(void) {
    if (events_namespace.item != 0) return events_namespace;
    ensure_keys();

    events_namespace = js_new_object();
    js_property_set(events_namespace, make_string_item("__class_name__"), make_string_item("EventEmitter"));

    // EventEmitter constructor
    ee_set_method(events_namespace, "EventEmitter", (void*)js_ee_constructor, 0);

    // Prototype methods (used as EventEmitter.prototype.*)
    // Also put on namespace for direct use
    ee_set_method(events_namespace, "on",                  (void*)js_ee_on, 3);
    ee_set_method(events_namespace, "addListener",         (void*)js_ee_on, 3); // alias
    ee_set_method(events_namespace, "once",                (void*)js_ee_once, 3);
    ee_set_method(events_namespace, "off",                 (void*)js_ee_off, 3);
    ee_set_method(events_namespace, "removeListener",      (void*)js_ee_off, 3); // alias
    ee_set_method(events_namespace, "emit",                (void*)js_ee_emit, -3); // emitter, event, ...rest args
    ee_set_method(events_namespace, "removeAllListeners",  (void*)js_ee_removeAllListeners, 2);
    ee_set_method(events_namespace, "listeners",           (void*)js_ee_listeners, 2);
    ee_set_method(events_namespace, "listenerCount",       (void*)js_ee_listenerCount, 2);
    ee_set_method(events_namespace, "eventNames",          (void*)js_ee_eventNames, 1);
    ee_set_method(events_namespace, "setMaxListeners",     (void*)js_ee_setMaxListeners, 2);
    ee_set_method(events_namespace, "getMaxListeners",     (void*)js_ee_getMaxListeners, 1);
    ee_set_method(events_namespace, "prependListener",     (void*)js_ee_prependListener, 3);
    ee_set_method(events_namespace, "prependOnceListener", (void*)js_ee_prependOnceListener, 3);
    ee_set_method(events_namespace, "rawListeners",        (void*)js_ee_listeners, 2); // same as listeners (no wrappers)

    // static property: defaultMaxListeners = 10
    js_property_set(events_namespace, make_string_item("defaultMaxListeners"), (Item){.item = i2it(10)});

    // default export is the constructor
    js_property_set(events_namespace, make_string_item("default"), events_namespace);

    return events_namespace;
}

extern "C" void js_reset_events_module(void) {
    events_namespace = (Item){0};
    keys_initialized = false;
}
