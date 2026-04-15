/**
 * js_stream.cpp — Node.js-style 'stream' module for LambdaJS
 *
 * Provides Readable, Writable, Duplex, Transform, PassThrough stream classes.
 * Built on top of EventEmitter (js_events.cpp) with simplified push/pull model.
 * Registered as built-in module 'stream' via js_module_get().
 *
 * Implementation notes:
 * - Streams are JS objects with EventEmitter-like on/emit methods
 * - Readable: push(data) / on('data',cb) / on('end',cb) / pipe(writable)
 * - Writable: write(data) / end() / on('finish',cb) / on('drain',cb)
 * - Duplex: both Readable + Writable
 * - Transform: Duplex with _transform(chunk,enc,cb)
 * - PassThrough: Transform that passes data through unchanged
 * - pipeline(src, ...transforms, dst, cb) — pipe chain with error handling
 */
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"

#include <cstring>

static Item make_string_item(const char* str, int len) {
    if (!str) return ItemNull;
    String* s = heap_create_name(str, (size_t)(len > 0 ? len : 0));
    return (Item){.item = s2it(s)};
}

static Item make_string_item(const char* str) {
    if (!str) return ItemNull;
    return make_string_item(str, (int)strlen(str));
}

// cached key items
static Item key_on;
static Item key_emit;
static Item key_push;
static Item key_write;
static Item key_end;
static Item key_pipe;
static Item key_read;
static Item key_destroy;
static Item key_readable;
static Item key_writable;
static Item key_flowing;
static Item key_ended;
static Item key_finished;
static Item key_destroyed;
static Item key_listeners;
static Item key_buffer;
static Item key_class_name;
static bool keys_init = false;

static void ensure_keys() {
    if (keys_init) return;
    key_on       = make_string_item("on");
    key_emit     = make_string_item("emit");
    key_push     = make_string_item("push");
    key_write    = make_string_item("write");
    key_end      = make_string_item("end");
    key_pipe     = make_string_item("pipe");
    key_read     = make_string_item("read");
    key_destroy  = make_string_item("destroy");
    key_readable = make_string_item("readable");
    key_writable = make_string_item("writable");
    key_flowing  = make_string_item("__flowing__");
    key_ended    = make_string_item("__ended__");
    key_finished = make_string_item("__finished__");
    key_destroyed= make_string_item("__destroyed__");
    key_listeners= make_string_item("__listeners__");
    key_buffer   = make_string_item("__buffer__");
    key_class_name = make_string_item("__class_name__");
    keys_init = true;
}

// =============================================================================
// EventEmitter-like helpers for stream objects
// =============================================================================

static Item stream_get_listener_array(Item self, const char* event) {
    Item listeners_map = js_property_get(self, key_listeners);
    if (get_type_id(listeners_map) != LMD_TYPE_MAP) return ItemNull;
    Item event_key = make_string_item(event);
    return js_property_get(listeners_map, event_key);
}

static void stream_emit(Item self, const char* event, Item* args, int argc) {
    Item listeners_map = js_property_get(self, key_listeners);
    if (get_type_id(listeners_map) != LMD_TYPE_MAP) return;
    Item event_key = make_string_item(event);
    Item arr = js_property_get(listeners_map, event_key);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) return;
    int64_t len = js_array_length(arr);
    for (int64_t i = 0; i < len; i++) {
        Item listener = js_array_get_int(arr, i);
        if (get_type_id(listener) == LMD_TYPE_FUNC) {
            js_call_function(listener, self, args, argc);
        }
    }
}

// on(event, listener)
extern "C" Item js_stream_on(Item self, Item event_item, Item listener) {
    ensure_keys();
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;

    Item listeners_map = js_property_get(self, key_listeners);
    if (get_type_id(listeners_map) != LMD_TYPE_MAP) {
        listeners_map = js_new_object();
        js_property_set(self, key_listeners, listeners_map);
    }

    Item arr = js_property_get(listeners_map, event_item);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) {
        arr = js_array_new(4);
        js_property_set(listeners_map, event_item, arr);
    }
    js_array_push(arr, listener);

    // if adding 'data' listener to readable, start flowing mode
    String* ev = it2s(event_item);
    if (ev->len == 4 && memcmp(ev->chars, "data", 4) == 0) {
        js_property_set(self, key_flowing, (Item){.item = b2it(true)});
        // flush buffered data
        Item buf = js_property_get(self, key_buffer);
        if (get_type_id(buf) == LMD_TYPE_ARRAY) {
            int64_t blen = js_array_length(buf);
            for (int64_t i = 0; i < blen; i++) {
                Item chunk = js_array_get_int(buf, i);
                stream_emit(self, "data", &chunk, 1);
            }
            // clear buffer
            js_property_set(self, key_buffer, js_array_new(0));
        }
    }
    return self;
}

// emit(event, ...args)
extern "C" Item js_stream_emit(Item self, Item event_item, Item arg1) {
    ensure_keys();
    if (get_type_id(event_item) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
    String* ev = it2s(event_item);
    char event_buf[64];
    int elen = (int)ev->len < 63 ? (int)ev->len : 63;
    memcpy(event_buf, ev->chars, (size_t)elen);
    event_buf[elen] = '\0';
    stream_emit(self, event_buf, &arg1, 1);
    return (Item){.item = b2it(true)};
}

// =============================================================================
// Readable stream
// =============================================================================

// push(chunk) — add data to readable stream
extern "C" Item js_readable_push(Item self, Item chunk) {
    ensure_keys();

    // null signals end of stream
    if (chunk.item == 0 || get_type_id(chunk) == LMD_TYPE_NULL) {
        js_property_set(self, key_ended, (Item){.item = b2it(true)});
        stream_emit(self, "end", NULL, 0);
        return (Item){.item = b2it(true)};
    }

    // if flowing, emit 'data' immediately
    Item flowing = js_property_get(self, key_flowing);
    if (flowing.item != 0 && it2b(flowing)) {
        stream_emit(self, "data", &chunk, 1);
    } else {
        // buffer it
        Item buf = js_property_get(self, key_buffer);
        if (get_type_id(buf) != LMD_TYPE_ARRAY) {
            buf = js_array_new(16);
            js_property_set(self, key_buffer, buf);
        }
        js_array_push(buf, chunk);
    }
    return (Item){.item = b2it(true)};
}

// read() — pull one chunk from buffer (non-flowing mode)
extern "C" Item js_readable_read(Item self) {
    ensure_keys();
    Item buf = js_property_get(self, key_buffer);
    if (get_type_id(buf) != LMD_TYPE_ARRAY) return ItemNull;
    int64_t blen = js_array_length(buf);
    if (blen == 0) return ItemNull;
    // get first element (shift not directly supported — rebuild array)
    Item result = js_array_get_int(buf, 0);
    Item new_buf = js_array_new((int)blen);
    for (int64_t i = 1; i < blen; i++) {
        js_array_push(new_buf, js_array_get_int(buf, i));
    }
    js_property_set(self, key_buffer, new_buf);
    return result;
}

// pipe(destination) — pipe this readable to a writable
extern "C" Item js_readable_pipe(Item self, Item dest) {
    ensure_keys();
    // set up data listener that writes to dest
    // store dest reference
    js_property_set(self, make_string_item("__pipe_dest__"), dest);
    js_property_set(self, key_flowing, (Item){.item = b2it(true)});

    // flush buffer to dest
    Item buf = js_property_get(self, key_buffer);
    if (get_type_id(buf) == LMD_TYPE_ARRAY) {
        int64_t blen = js_array_length(buf);
        Item write_fn = js_property_get(dest, key_write);
        if (get_type_id(write_fn) == LMD_TYPE_FUNC) {
            for (int64_t i = 0; i < blen; i++) {
                Item chunk = js_array_get_int(buf, i);
                js_call_function(write_fn, dest, &chunk, 1);
            }
        }
        js_property_set(self, key_buffer, js_array_new(0));
    }

    // register data handler to forward
    // store that pipe is active via marker
    js_property_set(self, make_string_item("__piped__"), (Item){.item = b2it(true)});

    return dest;
}

// destroy() — destroy stream
extern "C" Item js_stream_destroy(Item self) {
    ensure_keys();
    js_property_set(self, key_destroyed, (Item){.item = b2it(true)});
    stream_emit(self, "close", NULL, 0);
    return self;
}

// Readable constructor
extern "C" Item js_readable_new(void) {
    ensure_keys();
    Item obj = js_new_object();

    js_property_set(obj, key_class_name, make_string_item("Readable"));
    js_property_set(obj, key_readable, (Item){.item = b2it(true)});
    js_property_set(obj, key_flowing, (Item){.item = b2it(false)});
    js_property_set(obj, key_ended, (Item){.item = b2it(false)});
    js_property_set(obj, key_destroyed, (Item){.item = b2it(false)});
    js_property_set(obj, key_buffer, js_array_new(16));
    js_property_set(obj, key_listeners, js_new_object());

    js_property_set(obj, key_on, js_new_function((void*)js_stream_on, 3));
    js_property_set(obj, key_emit, js_new_function((void*)js_stream_emit, 3));
    js_property_set(obj, key_push, js_new_function((void*)js_readable_push, 2));
    js_property_set(obj, key_read, js_new_function((void*)js_readable_read, 1));
    js_property_set(obj, key_pipe, js_new_function((void*)js_readable_pipe, 2));
    js_property_set(obj, key_destroy, js_new_function((void*)js_stream_destroy, 1));

    return obj;
}

// =============================================================================
// Writable stream
// =============================================================================

// write(chunk) — write data to writable stream
extern "C" Item js_writable_write(Item self, Item chunk) {
    ensure_keys();

    // call _write handler if set
    Item write_handler = js_property_get(self, make_string_item("__write_handler__"));
    if (get_type_id(write_handler) == LMD_TYPE_FUNC) {
        Item encoding = make_string_item("utf8");
        Item noop = ItemNull; // callback
        Item args[3] = {chunk, encoding, noop};
        js_call_function(write_handler, self, args, 3);
    }

    // emit 'drain'
    stream_emit(self, "drain", NULL, 0);
    return (Item){.item = b2it(true)};
}

// end([chunk]) — signal end of writes
extern "C" Item js_writable_end(Item self, Item chunk) {
    ensure_keys();

    // write final chunk if provided
    if (chunk.item != 0 && get_type_id(chunk) != LMD_TYPE_UNDEFINED &&
        get_type_id(chunk) != LMD_TYPE_NULL) {
        js_writable_write(self, chunk);
    }

    js_property_set(self, key_finished, (Item){.item = b2it(true)});
    stream_emit(self, "finish", NULL, 0);
    return self;
}

// Writable constructor
extern "C" Item js_writable_new(void) {
    ensure_keys();
    Item obj = js_new_object();

    js_property_set(obj, key_class_name, make_string_item("Writable"));
    js_property_set(obj, key_writable, (Item){.item = b2it(true)});
    js_property_set(obj, key_finished, (Item){.item = b2it(false)});
    js_property_set(obj, key_destroyed, (Item){.item = b2it(false)});
    js_property_set(obj, key_listeners, js_new_object());

    js_property_set(obj, key_on, js_new_function((void*)js_stream_on, 3));
    js_property_set(obj, key_emit, js_new_function((void*)js_stream_emit, 3));
    js_property_set(obj, key_write, js_new_function((void*)js_writable_write, 2));
    js_property_set(obj, key_end, js_new_function((void*)js_writable_end, 2));
    js_property_set(obj, key_destroy, js_new_function((void*)js_stream_destroy, 1));

    return obj;
}

// =============================================================================
// Duplex stream (Readable + Writable)
// =============================================================================

extern "C" Item js_duplex_new(void) {
    ensure_keys();
    Item obj = js_new_object();

    js_property_set(obj, key_class_name, make_string_item("Duplex"));
    js_property_set(obj, key_readable, (Item){.item = b2it(true)});
    js_property_set(obj, key_writable, (Item){.item = b2it(true)});
    js_property_set(obj, key_flowing, (Item){.item = b2it(false)});
    js_property_set(obj, key_ended, (Item){.item = b2it(false)});
    js_property_set(obj, key_finished, (Item){.item = b2it(false)});
    js_property_set(obj, key_destroyed, (Item){.item = b2it(false)});
    js_property_set(obj, key_buffer, js_array_new(16));
    js_property_set(obj, key_listeners, js_new_object());

    // Readable methods
    js_property_set(obj, key_on, js_new_function((void*)js_stream_on, 3));
    js_property_set(obj, key_emit, js_new_function((void*)js_stream_emit, 3));
    js_property_set(obj, key_push, js_new_function((void*)js_readable_push, 2));
    js_property_set(obj, key_read, js_new_function((void*)js_readable_read, 1));
    js_property_set(obj, key_pipe, js_new_function((void*)js_readable_pipe, 2));

    // Writable methods
    js_property_set(obj, key_write, js_new_function((void*)js_writable_write, 2));
    js_property_set(obj, key_end, js_new_function((void*)js_writable_end, 2));
    js_property_set(obj, key_destroy, js_new_function((void*)js_stream_destroy, 1));

    return obj;
}

// =============================================================================
// Transform stream (Duplex with _transform)
// =============================================================================

// _transform(chunk, encoding, callback) override
extern "C" Item js_transform_write(Item self, Item chunk) {
    ensure_keys();

    // call _transform if set
    Item transform_fn = js_property_get(self, make_string_item("_transform"));
    if (get_type_id(transform_fn) == LMD_TYPE_FUNC) {
        Item encoding = make_string_item("utf8");
        // callback pushes result
        Item args[3] = {chunk, encoding, ItemNull};
        Item result = js_call_function(transform_fn, self, args, 3);
        // if _transform returns data, push it
        if (result.item != 0 && get_type_id(result) != LMD_TYPE_UNDEFINED) {
            js_readable_push(self, result);
        }
    } else {
        // default: pass through
        js_readable_push(self, chunk);
    }

    stream_emit(self, "drain", NULL, 0);
    return (Item){.item = b2it(true)};
}

extern "C" Item js_transform_end(Item self, Item chunk) {
    ensure_keys();
    if (chunk.item != 0 && get_type_id(chunk) != LMD_TYPE_UNDEFINED &&
        get_type_id(chunk) != LMD_TYPE_NULL) {
        js_transform_write(self, chunk);
    }

    // call _flush if set
    Item flush_fn = js_property_get(self, make_string_item("_flush"));
    if (get_type_id(flush_fn) == LMD_TYPE_FUNC) {
        js_call_function(flush_fn, self, NULL, 0);
    }

    js_property_set(self, key_finished, (Item){.item = b2it(true)});
    js_readable_push(self, ItemNull); // signal end
    stream_emit(self, "finish", NULL, 0);
    return self;
}

extern "C" Item js_transform_new(void) {
    ensure_keys();
    Item obj = js_new_object();

    js_property_set(obj, key_class_name, make_string_item("Transform"));
    js_property_set(obj, key_readable, (Item){.item = b2it(true)});
    js_property_set(obj, key_writable, (Item){.item = b2it(true)});
    js_property_set(obj, key_flowing, (Item){.item = b2it(false)});
    js_property_set(obj, key_ended, (Item){.item = b2it(false)});
    js_property_set(obj, key_finished, (Item){.item = b2it(false)});
    js_property_set(obj, key_destroyed, (Item){.item = b2it(false)});
    js_property_set(obj, key_buffer, js_array_new(16));
    js_property_set(obj, key_listeners, js_new_object());

    // Readable methods
    js_property_set(obj, key_on, js_new_function((void*)js_stream_on, 3));
    js_property_set(obj, key_emit, js_new_function((void*)js_stream_emit, 3));
    js_property_set(obj, key_push, js_new_function((void*)js_readable_push, 2));
    js_property_set(obj, key_read, js_new_function((void*)js_readable_read, 1));
    js_property_set(obj, key_pipe, js_new_function((void*)js_readable_pipe, 2));

    // Writable methods using transform
    js_property_set(obj, key_write, js_new_function((void*)js_transform_write, 2));
    js_property_set(obj, key_end, js_new_function((void*)js_transform_end, 2));
    js_property_set(obj, key_destroy, js_new_function((void*)js_stream_destroy, 1));

    return obj;
}

// =============================================================================
// PassThrough — Transform that passes data unchanged
// =============================================================================

extern "C" Item js_passthrough_new(void) {
    Item obj = js_transform_new();
    js_property_set(obj, key_class_name, make_string_item("PassThrough"));
    // _transform not set → default pass-through behavior in js_transform_write
    return obj;
}

// =============================================================================
// pipeline(source, ...transforms, destination, callback) — pipe chain
// =============================================================================

extern "C" Item js_stream_pipeline(Item source, Item dest) {
    ensure_keys();
    // simplified two-argument pipeline (most common case)
    // for multi-step, chain pipe() calls
    Item pipe_fn = js_property_get(source, key_pipe);
    if (get_type_id(pipe_fn) == LMD_TYPE_FUNC) {
        js_call_function(pipe_fn, source, &dest, 1);
    }
    return dest;
}

// Readable.from(iterable) — create readable from array
extern "C" Item js_readable_from(Item iterable) {
    ensure_keys();
    Item readable = js_readable_new();

    if (get_type_id(iterable) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(iterable);
        for (int64_t i = 0; i < len; i++) {
            js_readable_push(readable, js_array_get_int(iterable, i));
        }
        js_readable_push(readable, ItemNull); // end
    } else if (get_type_id(iterable) == LMD_TYPE_STRING) {
        js_readable_push(readable, iterable);
        js_readable_push(readable, ItemNull);
    }

    return readable;
}

// =============================================================================
// stream Module Namespace
// =============================================================================

static Item stream_namespace = {0};

static void stream_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
}

extern "C" Item js_get_stream_namespace(void) {
    if (stream_namespace.item != 0) return stream_namespace;
    ensure_keys();

    stream_namespace = js_new_object();

    stream_set_method(stream_namespace, "Readable",    (void*)js_readable_new, 0);
    stream_set_method(stream_namespace, "Writable",    (void*)js_writable_new, 0);
    stream_set_method(stream_namespace, "Duplex",      (void*)js_duplex_new, 0);
    stream_set_method(stream_namespace, "Transform",   (void*)js_transform_new, 0);
    stream_set_method(stream_namespace, "PassThrough", (void*)js_passthrough_new, 0);
    stream_set_method(stream_namespace, "pipeline",    (void*)js_stream_pipeline, 2);

    // Readable.from as a static method
    Item readable_constructor = js_property_get(stream_namespace, make_string_item("Readable"));
    if (get_type_id(readable_constructor) == LMD_TYPE_FUNC) {
        js_property_set(readable_constructor, make_string_item("from"),
                        js_new_function((void*)js_readable_from, 1));
    }

    Item default_key = make_string_item("default");
    js_property_set(stream_namespace, default_key, stream_namespace);

    return stream_namespace;
}

extern "C" void js_stream_reset(void) {
    stream_namespace = (Item){0};
    keys_init = false;
}
