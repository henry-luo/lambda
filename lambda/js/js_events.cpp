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
#include "js_class.h"
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
static Item new_listener_key; // "newListener"
static Item remove_listener_key; // "removeListener"
static bool keys_initialized = false;

static void ensure_keys() {
    if (keys_initialized) return;
    events_key = make_string_item("__events__");
    once_key = make_string_item("__once__");
    max_listeners_key = make_string_item("__maxListeners__");
    new_listener_key = make_string_item("newListener");
    remove_listener_key = make_string_item("removeListener");
    keys_initialized = true;
}

// Get or create the __events__ map on an emitter object
// Also exposes as _events for Node.js compatibility
static Item get_events_map(Item emitter) {
    ensure_keys();
    Item map = js_property_get(emitter, events_key);
    if (map.item == 0 || get_type_id(map) == LMD_TYPE_UNDEFINED) {
        map = js_new_object();
        js_property_set(emitter, events_key, map);
        // Also set _events alias for Node.js compatibility
        js_property_set(emitter, make_string_item("_events"), map);
        js_property_set(emitter, make_string_item("_eventsCount"), (Item){.item = i2it(0)});
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

// Update _eventsCount on an emitter
static void update_events_count(Item emitter) {
    Item map = get_events_map(emitter);
    Item all_keys = js_object_keys(map);
    int64_t count = 0;
    int64_t klen = js_array_length(all_keys);
    for (int64_t i = 0; i < klen; i++) {
        Item key = js_array_get_int(all_keys, i);
        Item arr = js_property_get(map, key);
        if (arr.item != 0 && get_type_id(arr) != LMD_TYPE_UNDEFINED && js_array_length(arr) > 0) {
            count++;
        }
    }
    js_property_set(emitter, make_string_item("_eventsCount"), (Item){.item = i2it(count)});
}

// Emit newListener event (before adding listener, per Node.js spec)
static void emit_new_listener(Item emitter, Item event_name, Item listener) {
    // Don't emit newListener for the newListener event itself (avoid infinite recursion)
    if (get_type_id(event_name) == LMD_TYPE_STRING) {
        String* en = it2s(event_name);
        if (en->len == 11 && memcmp(en->chars, "newListener", 11) == 0) return;
    }
    Item map = get_events_map(emitter);
    Item nl_arr = js_property_get(map, new_listener_key);
    if (nl_arr.item == 0 || get_type_id(nl_arr) == LMD_TYPE_UNDEFINED) return;
    int64_t nl_len = js_array_length(nl_arr);
    if (nl_len == 0) return;
    Item args[2] = {event_name, listener};
    for (int64_t i = 0; i < nl_len; i++) {
        Item fn = js_array_get_int(nl_arr, i);
        js_call_function(fn, emitter, args, 2);
    }
}

// Emit removeListener event (after removing listener, per Node.js spec)
static void emit_remove_listener(Item emitter, Item event_name, Item listener) {
    // Don't emit removeListener for the removeListener event itself
    if (get_type_id(event_name) == LMD_TYPE_STRING) {
        String* en = it2s(event_name);
        if (en->len == 14 && memcmp(en->chars, "removeListener", 14) == 0) return;
    }
    Item map = get_events_map(emitter);
    Item rl_arr = js_property_get(map, remove_listener_key);
    if (rl_arr.item == 0 || get_type_id(rl_arr) == LMD_TYPE_UNDEFINED) return;
    int64_t rl_len = js_array_length(rl_arr);
    if (rl_len == 0) return;
    Item args[2] = {event_name, listener};
    for (int64_t i = 0; i < rl_len; i++) {
        Item fn = js_array_get_int(rl_arr, i);
        js_call_function(fn, emitter, args, 2);
    }
}

// ─── emitter.on(event, listener) ─────────────────────────────────────────────
// Adds listener to end of listeners array. Returns emitter for chaining.
extern "C" Item js_ee_on(Item emitter, Item event_name, Item listener) {
    if (emitter.item == 0) return ItemNull;
    emit_new_listener(emitter, event_name, listener);
    Item arr = get_listeners_array(emitter, event_name);
    js_array_push(arr, listener);
    update_events_count(emitter);
    return emitter;
}

// ─── emitter.once(event, listener) ───────────────────────────────────────────
// Adds a one-time listener. Returns emitter for chaining.
extern "C" Item js_ee_once(Item emitter, Item event_name, Item listener) {
    if (emitter.item == 0) return ItemNull;
    emit_new_listener(emitter, event_name, listener);
    Item arr = get_listeners_array(emitter, event_name);
    js_array_push(arr, listener);
    // mark this function as once
    Item set = get_once_set(emitter);
    js_array_push(set, listener);
    update_events_count(emitter);
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
            update_events_count(emitter);
            emit_remove_listener(emitter, event_name, listener);
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
                // Node.js: throw error arg if it's an Error, else wrap in ERR_UNHANDLED_ERROR
                extern void js_throw_value(Item error);
                extern Item js_new_error_with_name(Item type_name, Item message);
                extern Item js_to_string(Item value);
                extern Item js_util_inspect(Item obj_item, Item options_item);
                Item err_arg = ItemNull;
                if (args_rest.item != 0 && get_type_id(args_rest) != LMD_TYPE_UNDEFINED) {
                    int64_t argc = js_array_length(args_rest);
                    if (argc > 0) err_arg = js_array_get_int(args_rest, 0);
                }
                // check if err_arg is an Error instance
                bool is_error = false;
                if (get_type_id(err_arg) == LMD_TYPE_MAP) {
                    Item cn = js_property_get(err_arg, make_string_item("__class_name__"));
                    if (get_type_id(cn) == LMD_TYPE_STRING) {
                        String* cns = it2s(cn);
                        if (cns->len >= 5 && memcmp(cns->chars + cns->len - 5, "Error", 5) == 0)
                            is_error = true;
                    }
                }
                if (is_error) {
                    // Error instance: throw directly
                    js_throw_value(err_arg);
                } else {
                    // wrap in ERR_UNHANDLED_ERROR
                    char buf[512];
                    int len;
                    if (err_arg.item == 0 || get_type_id(err_arg) == LMD_TYPE_UNDEFINED) {
                        len = snprintf(buf, sizeof(buf), "Unhandled error.");
                    } else {
                        Item inspected = js_util_inspect(err_arg, make_js_undefined());
                        String* is_str = it2s(inspected);
                        len = snprintf(buf, sizeof(buf), "Unhandled error. (%.*s)", is_str->len, is_str->chars);
                    }
                    Item wrapped = js_new_error_with_name(make_string_item("Error"),
                        (Item){.item = s2it(heap_create_name(buf, len))});
                    js_property_set(wrapped, make_string_item("code"), make_string_item("ERR_UNHANDLED_ERROR"));
                    if (err_arg.item != 0 && get_type_id(err_arg) != LMD_TYPE_UNDEFINED) {
                        js_property_set(wrapped, make_string_item("context"), err_arg);
                    }
                    js_throw_value(wrapped);
                }
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

    // Remove ALL once-listeners from the emitter BEFORE calling any of them.
    // This prevents recursive emit() from re-firing once-listeners.
    for (int64_t i = 0; i < len; i++) {
        Item fn = js_array_get_int(snapshot, i);
        if (is_once_listener(emitter, fn)) {
            js_ee_off(emitter, event_name, fn);
            // remove from once-set
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

    // Now call all listeners from the snapshot
    for (int64_t i = 0; i < len; i++) {
        Item fn = js_array_get_int(snapshot, i);

        js_call_function(fn, emitter, args, (int)argc);
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
        js_property_set(emitter, make_string_item("_events"), js_new_object());
        js_property_set(emitter, once_key, js_array_new(0));
        js_property_set(emitter, make_string_item("_eventsCount"), (Item){.item = i2it(0)});
    } else {
        // Emit removeListener for each removed listener
        Item arr = js_property_get(map, event_name);
        if (arr.item != 0 && get_type_id(arr) != LMD_TYPE_UNDEFINED) {
            int64_t len = js_array_length(arr);
            for (int64_t i = len - 1; i >= 0; i--) {
                emit_remove_listener(emitter, event_name, js_array_get_int(arr, i));
            }
        }
        js_property_set(map, event_name, js_array_new(0));
        update_events_count(emitter);
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

// ─── emitter.listenerCount(event [, listener]) ─────────────────────────────
extern "C" Item js_ee_listenerCount(Item emitter, Item event_name, Item listener) {
    if (emitter.item == 0) return (Item){.item = i2it(0)};
    Item map = get_events_map(emitter);
    Item arr = js_property_get(map, event_name);
    if (arr.item == 0 || get_type_id(arr) == LMD_TYPE_UNDEFINED) {
        return (Item){.item = i2it(0)};
    }
    int64_t len = js_array_length(arr);
    // if no specific listener requested, return total count
    if (listener.item == 0 || get_type_id(listener) == LMD_TYPE_UNDEFINED) {
        return (Item){.item = i2it(len)};
    }
    // count only matching listeners
    int64_t count = 0;
    for (int64_t i = 0; i < len; i++) {
        Item f = js_array_get_int(arr, i);
        if (f.item == listener.item) count++;
    }
    return (Item){.item = i2it(count)};
}

// ─── emitter.eventNames() ──────────────────────────────────────────────────
extern "C" Item js_ee_eventNames(Item emitter) {
    if (emitter.item == 0) return js_array_new(0);
    Item map = get_events_map(emitter);
    Item all_keys = js_object_keys(map);
    // filter out event names with 0 listeners
    Item result = js_array_new(0);
    int64_t klen = js_array_length(all_keys);
    for (int64_t i = 0; i < klen; i++) {
        Item key = js_array_get_int(all_keys, i);
        Item arr = js_property_get(map, key);
        if (arr.item != 0 && get_type_id(arr) != LMD_TYPE_UNDEFINED && js_array_length(arr) > 0) {
            js_array_push(result, key);
        }
    }
    return result;
}

// ─── emitter.setMaxListeners(n) ─────────────────────────────────────────────
extern "C" Item js_ee_setMaxListeners(Item emitter, Item n) {
    if (emitter.item == 0) return emitter;
    ensure_keys();
    js_property_set(emitter, max_listeners_key, n);
    return emitter;
}

// ─── static events.setMaxListeners(n, ...eventTargets) ──────────────────────
// Namespace version uses same (emitter, n) signature as other static methods
// for consistency with events.on(emitter, event, fn) pattern.
// (same order, but listed here for clarity)

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
    emit_new_listener(emitter, event_name, listener);
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
    update_events_count(emitter);
    return emitter;
}

// ─── emitter.prependOnceListener(event, listener) ───────────────────────────
extern "C" Item js_ee_prependOnceListener(Item emitter, Item event_name, Item listener) {
    if (emitter.item == 0) return ItemNull;
    emit_new_listener(emitter, event_name, listener);
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
    update_events_count(emitter);
    return emitter;
}

// ─── Instance method wrappers (use js_get_this() for emitter) ────────────────
// These wrappers allow prototype methods to work as instance.method(event, fn)
// where 'this' provides the emitter instead of the first explicit argument.

extern "C" Item js_get_this(void);

static Item js_ee_inst_on(Item event_name, Item listener) {
    return js_ee_on(js_get_this(), event_name, listener);
}
static Item js_ee_inst_once(Item event_name, Item listener) {
    return js_ee_once(js_get_this(), event_name, listener);
}
static Item js_ee_inst_off(Item event_name, Item listener) {
    return js_ee_off(js_get_this(), event_name, listener);
}
static Item js_ee_inst_emit(Item event_name, Item args_rest) {
    // emit is variadic — re-build args with emitter prepended
    Item emitter = js_get_this();
    // Build full args: [emitter, event_name, ...rest]
    // The original js_ee_emit expects (emitter, event, ...rest) as variadic
    // For now use direct call since we have the rest array
    return js_ee_emit(emitter, event_name, args_rest);
}
static Item js_ee_inst_removeAllListeners(Item event_name) {
    return js_ee_removeAllListeners(js_get_this(), event_name);
}
static Item js_ee_inst_listeners(Item event_name) {
    return js_ee_listeners(js_get_this(), event_name);
}
static Item js_ee_inst_listenerCount(Item event_name, Item listener) {
    return js_ee_listenerCount(js_get_this(), event_name, listener);
}
static Item js_ee_inst_eventNames(void) {
    return js_ee_eventNames(js_get_this());
}
static Item js_ee_inst_setMaxListeners(Item n) {
    return js_ee_setMaxListeners(js_get_this(), n);
}
static Item js_ee_inst_getMaxListeners(void) {
    return js_ee_getMaxListeners(js_get_this());
}
static Item js_ee_inst_prependListener(Item event_name, Item listener) {
    return js_ee_prependListener(js_get_this(), event_name, listener);
}
static Item js_ee_inst_prependOnceListener(Item event_name, Item listener) {
    return js_ee_prependOnceListener(js_get_this(), event_name, listener);
}

// ─── new EventEmitter() constructor ─────────────────────────────────────────
static Item ee_prototype = {0};

extern "C" void js_function_set_prototype(Item fn_item, Item proto);

extern "C" Item js_ee_constructor(void) {
    ensure_keys();
    // Check if called via 'new EventEmitter()' — the runtime pre-builds an object
    // with __proto__ set. Detect this by checking if this_val's prototype is ee_prototype.
    Item this_val = js_get_this();
    if (get_type_id(this_val) == LMD_TYPE_MAP) {
        Item proto = js_get_prototype(this_val);
        if (proto.item == ee_prototype.item && ee_prototype.item != 0) {
            // Called via 'new' — initialize the pre-built object
            js_property_set(this_val, make_string_item("__class_name__"), make_string_item("EventEmitter"));
            js_class_stamp(this_val, JS_CLASS_EVENT_EMITTER);  // A3-T3b
            Item events_map = js_new_object();
            js_property_set(this_val, events_key, events_map);
            js_property_set(this_val, make_string_item("_events"), events_map);
            js_property_set(this_val, make_string_item("_eventsCount"), (Item){.item = i2it(0)});
            js_property_set(this_val, once_key, js_array_new(0));
            return make_js_undefined();
        }
    }
    // Direct call (not via new) — create a new object with prototype
    Item emitter = js_new_object();
    js_property_set(emitter, make_string_item("__class_name__"), make_string_item("EventEmitter"));
    js_class_stamp(emitter, JS_CLASS_EVENT_EMITTER);  // A3-T3b
    Item events_map = js_new_object();
    js_property_set(emitter, events_key, events_map);
    js_property_set(emitter, make_string_item("_events"), events_map);
    js_property_set(emitter, make_string_item("_eventsCount"), (Item){.item = i2it(0)});
    js_property_set(emitter, once_key, js_array_new(0));
    if (ee_prototype.item != 0) {
        js_set_prototype(emitter, ee_prototype);
    }
    return emitter;
}

// events.once(emitter, eventName) — static, returns a Promise that resolves
// with an array of args when the event fires, or rejects on 'error'.
static Item js_ee_static_once(Item emitter, Item event_name) {
    extern Item js_promise_with_resolvers(void);
    extern Item js_new_function(void* fptr, int param_count);
    extern Item js_bind_function(Item func, Item this_val, Item* args, int arg_count);

    Item resolvers = js_promise_with_resolvers();
    Item promise = js_property_get(resolvers, make_string_item("promise"));
    Item resolve_fn = js_property_get(resolvers, make_string_item("resolve"));
    Item reject_fn = js_property_get(resolvers, make_string_item("reject"));

    // Create a wrapper that collects all args into an array and resolves.
    // Use a variadic lambda-like approach: bind resolve_fn as first bound arg.
    // The wrapper receives (...args) and calls resolve([...args]).
    struct OnceWrapper {
        static Item handler(Item rest_args) {
            // rest_args is the variadic args array
            // 'this' is the emitter, but we stored resolve_fn via bind
            // We need resolve_fn — it's bound as first arg in rest
            // Actually, we'll use a different approach: store resolve in a closure

            // The first element is resolve_fn (we prepended it via bind)
            Item resolve = js_array_get_int(rest_args, 0);
            // remaining elements are the actual event args
            int64_t total = js_array_length(rest_args);
            Item args_array = js_array_new(0);
            for (int64_t i = 1; i < total; i++) {
                js_array_push(args_array, js_array_get_int(rest_args, i));
            }
            Item call_args[1] = {args_array};
            js_call_function(resolve, make_js_undefined(), call_args, 1);
            return make_js_undefined();
        }
    };

    // Create wrapper fn(-1 = variadic), bind resolve_fn as first bound arg
    Item wrapper = js_new_function((void*)OnceWrapper::handler, -1);
    Item bound = js_bind_function(wrapper, make_js_undefined(), &resolve_fn, 1);

    // Call emitter.once(eventName, bound_wrapper)
    Item once_method = js_property_get(emitter, make_string_item("once"));
    if (get_type_id(once_method) == LMD_TYPE_FUNC) {
        Item args[2] = {event_name, bound};
        js_call_function(once_method, emitter, args, 2);
    }

    // Also listen for 'error' event to reject the promise (unless event IS 'error')
    bool is_error_event = false;
    if (get_type_id(event_name) == LMD_TYPE_STRING) {
        String* en = it2s(event_name);
        if (en->len == 5 && memcmp(en->chars, "error", 5) == 0) is_error_event = true;
    }
    if (!is_error_event) {
        // On 'error', reject the promise
        struct ErrorWrapper {
            static Item handler(Item rest_args) {
                Item reject = js_array_get_int(rest_args, 0);
                int64_t total = js_array_length(rest_args);
                Item err = (total > 1) ? js_array_get_int(rest_args, 1) : make_js_undefined();
                Item call_args[1] = {err};
                js_call_function(reject, make_js_undefined(), call_args, 1);
                return make_js_undefined();
            }
        };
        Item err_wrapper = js_new_function((void*)ErrorWrapper::handler, -1);
        Item err_bound = js_bind_function(err_wrapper, make_js_undefined(), &reject_fn, 1);
        Item err_once_args[2] = {make_string_item("error"), err_bound};
        js_call_function(once_method, emitter, err_once_args, 2);
    }

    return promise;
}

// Combined events.once — 3-arg form adds listener, 2-arg form returns Promise
static Item js_ee_once_combined(Item emitter, Item event_name, Item listener) {
    if (get_type_id(listener) == LMD_TYPE_FUNC) {
        return js_ee_once(emitter, event_name, listener);
    }
    return js_ee_static_once(emitter, event_name);
}

// ─── Namespace ───────────────────────────────────────────────────────────────

// static EventEmitter.getEventListeners(emitter, eventName)
static Item js_ee_static_getEventListeners(Item emitter, Item event_name) {
    return js_ee_listeners(emitter, event_name);
}

// static EventEmitter.listenerCount(emitter, eventName)
static Item js_ee_static_listenerCount(Item emitter, Item event_name) {
    return js_ee_listenerCount(emitter, event_name, ItemNull);
}

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
    js_class_stamp(events_namespace, JS_CLASS_EVENT_EMITTER);  // A3-T3b

    // Create prototype object with instance methods (this-based wrappers)
    ee_prototype = js_new_object();
    ee_set_method(ee_prototype, "on",                  (void*)js_ee_inst_on, 2);
    ee_set_method(ee_prototype, "addListener",         (void*)js_ee_inst_on, 2);
    ee_set_method(ee_prototype, "once",                (void*)js_ee_inst_once, 2);
    ee_set_method(ee_prototype, "off",                 (void*)js_ee_inst_off, 2);
    ee_set_method(ee_prototype, "removeListener",      (void*)js_ee_inst_off, 2);
    ee_set_method(ee_prototype, "emit",                (void*)js_ee_inst_emit, -2);
    ee_set_method(ee_prototype, "removeAllListeners",  (void*)js_ee_inst_removeAllListeners, 1);
    ee_set_method(ee_prototype, "listeners",           (void*)js_ee_inst_listeners, 1);
    ee_set_method(ee_prototype, "listenerCount",       (void*)js_ee_inst_listenerCount, 2);
    ee_set_method(ee_prototype, "eventNames",          (void*)js_ee_inst_eventNames, 0);
    ee_set_method(ee_prototype, "setMaxListeners",     (void*)js_ee_inst_setMaxListeners, 1);
    ee_set_method(ee_prototype, "getMaxListeners",     (void*)js_ee_inst_getMaxListeners, 0);
    ee_set_method(ee_prototype, "prependListener",     (void*)js_ee_inst_prependListener, 2);
    ee_set_method(ee_prototype, "prependOnceListener", (void*)js_ee_inst_prependOnceListener, 2);
    ee_set_method(ee_prototype, "rawListeners",        (void*)js_ee_inst_listeners, 1);

    // EventEmitter constructor
    ee_set_method(events_namespace, "EventEmitter", (void*)js_ee_constructor, 0);

    // Also put methods on namespace for direct use (e.g. events.on(emitter, event, fn))
    ee_set_method(events_namespace, "on",                  (void*)js_ee_on, 3);
    ee_set_method(events_namespace, "addListener",         (void*)js_ee_on, 3);
    ee_set_method(events_namespace, "once",                (void*)js_ee_once, 3);
    ee_set_method(events_namespace, "off",                 (void*)js_ee_off, 3);
    ee_set_method(events_namespace, "removeListener",      (void*)js_ee_off, 3);
    ee_set_method(events_namespace, "emit",                (void*)js_ee_emit, -3);
    ee_set_method(events_namespace, "removeAllListeners",  (void*)js_ee_removeAllListeners, 2);
    ee_set_method(events_namespace, "listeners",           (void*)js_ee_listeners, 2);
    ee_set_method(events_namespace, "listenerCount",       (void*)js_ee_listenerCount, 3);
    ee_set_method(events_namespace, "eventNames",          (void*)js_ee_eventNames, 1);
    ee_set_method(events_namespace, "setMaxListeners",     (void*)js_ee_setMaxListeners, 2);
    ee_set_method(events_namespace, "getMaxListeners",     (void*)js_ee_getMaxListeners, 1);
    ee_set_method(events_namespace, "prependListener",     (void*)js_ee_prependListener, 3);
    ee_set_method(events_namespace, "prependOnceListener", (void*)js_ee_prependOnceListener, 3);
    ee_set_method(events_namespace, "rawListeners",        (void*)js_ee_listeners, 2);

    // Set EventEmitter.prototype to the prototype object
    js_property_set(events_namespace, make_string_item("prototype"), ee_prototype);
    // Set __instance_proto__ so 'new EventEmitter()' (MAP constructor path)
    // sets up the prototype chain on instances correctly
    js_property_set(events_namespace, make_string_item("__instance_proto__"), ee_prototype);
    // Set __ctor__ so 'new EventEmitter()' calls the constructor to init storage
    Item ee_ctor = js_property_get(events_namespace, make_string_item("EventEmitter"));
    js_property_set(events_namespace, make_string_item("__ctor__"), ee_ctor);
    // Also set prototype on the constructor function's internal field
    if (get_type_id(ee_ctor) == LMD_TYPE_FUNC) {
        js_function_set_prototype(ee_ctor, ee_prototype);
    }

    // static property: defaultMaxListeners = 10
    js_property_set(events_namespace, make_string_item("defaultMaxListeners"), (Item){.item = i2it(10)});

    // static property: captureRejections = false
    js_property_set(events_namespace, make_string_item("captureRejections"), (Item){.item = b2it(false)});

    // static property: captureRejectionSymbol
    js_property_set(events_namespace, make_string_item("captureRejectionSymbol"),
        make_string_item("nodejs.rejection"));

    // static method: getEventListeners
    ee_set_method(events_namespace, "getEventListeners", (void*)js_ee_static_getEventListeners, 2);

    // static EventEmitter.listenerCount(emitter, event) — the 3-arg form at line ~627
    // already handles this: when called with 2 args, 3rd param (listener) is null/0,
    // and the function falls through to return total count. No override needed.

    // default export is the constructor
    js_property_set(events_namespace, make_string_item("default"), events_namespace);

    // static events.once — combined: 3 args = listener, 2 args = Promise
    ee_set_method(events_namespace, "once", (void*)js_ee_once_combined, 3);

    return events_namespace;
}

extern "C" void js_reset_events_module(void) {
    events_namespace = (Item){0};
    ee_prototype = (Item){0};
    keys_initialized = false;
}
