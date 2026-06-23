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
#include "js_class.h"
#include "js_property_attrs.h"
#include "js_typed_array.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/strbuf.h"

#include <cstdio>
#include <cstring>

// forward declarations
extern "C" Item js_throw_error_with_code(const char* code, const char* message);
extern "C" Item js_throw_type_error_code(const char* code, const char* message);
extern "C" Item js_throw_invalid_arg_type(const char* name, const char* expected, Item actual);
extern "C" Item js_new_error_with_name(Item error_name, Item message);
extern "C" int js_check_exception(void);
extern "C" Item js_clear_exception(void);
extern "C" Item js_process_emit(Item event_name, Item arg1);
extern "C" Item js_ordinary_has_instance(Item left, Item right);
extern "C" void js_function_set_prototype(Item fn_item, Item proto);
extern "C" Item js_buffer_from(Item data, Item encoding, Item length_item);
extern "C" Item js_buffer_isBuffer(Item obj);
extern "C" Item js_buffer_concat(Item list, Item total_length_item);
extern "C" Item js_buffer_toString(Item buf, Item encoding, Item start_item, Item end_item);
extern "C" Item js_buffer_slice(Item buf, Item start_item, Item end_item);
extern "C" Item js_util_inspect(Item obj_item, Item options_item);
extern "C" Item js_to_string(Item value);
extern "C" Item js_symbol_for(Item key);
extern "C" Item js_promise_with_resolvers(void);
extern "C" Item js_promise_resolve(Item value);
extern "C" Item js_promise_reject(Item reason);
extern "C" Item js_promise_then(Item promise, Item on_fulfilled, Item on_rejected);
extern "C" Item js_setTimeout(Item callback, Item delay);
extern "C" Item js_get_async_iterator(Item iterable);
extern "C" Item js_async_iterator_step_result(Item iterator);
extern "C" int64_t js_iterator_result_done(Item result);
extern "C" Item js_iterator_result_value(Item result);
extern "C" Item js_als_capture_context(void);
extern "C" Item js_als_context_call(Item context, Item callback, Item this_val, Item arg1, int64_t has_arg);
extern Item js_current_this;
extern "C" Item js_get_this(void);
extern "C" Item js_writable_stream_new(void);
extern "C" Item js_get_global_property(Item key);

static Item make_string_item(const char* str, int len) {
    if (!str) return ItemNull;
    String* s = heap_create_name(str, (size_t)(len > 0 ? len : 0));
    return (Item){.item = s2it(s)};
}

static Item make_string_item(const char* str) {
    if (!str) return ItemNull;
    return make_string_item(str, (int)strlen(str));
}

static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

// noop function for use as callbacks
extern "C" Item js_noop(void) {
    return make_js_undefined();
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
static Item key_readable_state;
static Item key_writable_state;
static Item key_end_pending;
static Item key_end_emitted;
static Item key_reading;
static Item key_paused;
static Item key_finish_emitted;
static Item key_close_emitted;
static Item key_capture_rejections;
static Item key_auto_destroy;
static bool keys_init = false;
static Item stream_readable_prototype = {0};
static Item stream_writable_prototype = {0};
static Item stream_duplex_prototype = {0};
static Item stream_transform_prototype = {0};
static Item stream_passthrough_prototype = {0};
static Item internal_stream_state_namespace = {0};
static Item stream_iter_namespace = {0};
static int64_t js_stream_default_byte_hwm = 16 * 1024;
static int64_t js_stream_default_object_hwm = 16;

static Item js_stream_make_error_with_code(const char* code, const char* message);
static Item js_stream_make_type_error_with_code(const char* code, const char* message);
static bool js_stream_readable_is_object_mode(Item self);
static int64_t js_stream_readable_chunk_length(Item self, Item chunk);
static bool js_stream_readable_accepts_more(Item self, Item buf);
static bool js_stream_readable_buffer_has_string(Item buf);
static Item js_stream_concat_decoded_chunks(Item buf, Item encoding);
static bool js_stream_prepare_readable_chunk(Item self, Item* chunk, Item encoding);
extern "C" Item js_readable_push(Item self, Item chunk);
extern "C" Item js_readable_new(Item opts);
extern "C" Item js_stream_destroy(Item self, Item err);
static void js_stream_flush_buffered_data(Item self);
static void js_stream_schedule_close(Item self);
static void js_stream_async_iterators_drain(Item stream, Item err);
static Item js_readable_iterator(Item self, Item options);
static void js_stream_iter_maybe_drain(Item readable);
static void js_stream_iter_resolve_drain(Item writer, Item value);
static void js_stream_iter_reject_drain(Item writer, Item err);
static void js_stream_iter_resolve_end_if_drained(Item writer);
static void js_stream_iter_reject_end(Item writer, Item err);
static void js_stream_iter_reject_pending_writes(Item writer, Item err);

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
    key_readable_state = make_string_item("_readableState");
    key_writable_state = make_string_item("_writableState");
    key_end_pending = make_string_item("__end_pending__");
    key_end_emitted = make_string_item("__end_emitted__");
    key_reading = make_string_item("__reading__");
    key_paused = make_string_item("__paused__");
    key_finish_emitted = make_string_item("__finish_emitted__");
    key_close_emitted = make_string_item("__close_emitted__");
    key_capture_rejections = make_string_item("__capture_rejections__");
    key_auto_destroy = make_string_item("__auto_destroy__");
    keys_init = true;
}

static inline Item js_bool_item(bool value) {
    return (Item){.item = b2it(value)};
}

static void js_readable_clear_pipe(Item self) {
    js_property_set(self, make_string_item("__piped__"), js_bool_item(false));
    js_property_set(self, make_string_item("__pipe_dest__"), make_js_undefined());
}

static bool js_item_is_true(Item item) {
    return get_type_id(item) == LMD_TYPE_BOOL && it2b(item);
}

static bool js_stream_string_equals(Item item, const char* literal) {
    if (get_type_id(item) != LMD_TYPE_STRING || !literal) return false;
    String* str = it2s(item);
    size_t len = strlen(literal);
    return str->len == len && memcmp(str->chars, literal, len) == 0;
}

static void js_state_set_bool(Item state, const char* name, bool value) {
    if (get_type_id(state) != LMD_TYPE_MAP) return;
    js_property_set(state, make_string_item(name), js_bool_item(value));
}

static void js_state_set_item(Item state, const char* name, Item value) {
    if (get_type_id(state) != LMD_TYPE_MAP) return;
    js_property_set(state, make_string_item(name), value);
}

static void js_stream_set_readable_buffer(Item self, Item buffer) {
    js_property_set(self, key_buffer, buffer);
    js_property_set(self, make_string_item("readableBuffer"), buffer);
}

static bool js_state_get_bool(Item state, const char* name) {
    if (get_type_id(state) != LMD_TYPE_MAP) return false;
    return js_item_is_true(js_property_get(state, make_string_item(name)));
}

static Item js_create_readable_state(void) {
    Item state = js_new_object();
    js_state_set_bool(state, "ended", false);
    js_state_set_bool(state, "endEmitted", false);
    js_state_set_bool(state, "errorEmitted", false);
    js_state_set_bool(state, "objectMode", false);
    js_state_set_bool(state, "readableListening", false);
    js_state_set_item(state, "errored", ItemNull);
    js_state_set_item(state, "highWaterMark", (Item){.item = i2it(js_stream_default_byte_hwm)});
    js_property_set(state, make_string_item("encoding"), make_string_item("utf8"));
    return state;
}

static Item js_create_writable_state(void) {
    Item state = js_new_object();
    js_state_set_bool(state, "ending", false);
    js_state_set_bool(state, "ended", false);
    js_state_set_bool(state, "finished", false);
    js_state_set_bool(state, "errorEmitted", false);
    js_state_set_bool(state, "objectMode", false);
    js_state_set_item(state, "errored", ItemNull);
    js_state_set_item(state, "corked", (Item){.item = i2it(0)});
    js_state_set_item(state, "bufferedRequestCount", (Item){.item = i2it(0)});
    js_state_set_item(state, "length", (Item){.item = i2it(0)});
    js_state_set_bool(state, "needDrain", false);
    js_state_set_item(state, "highWaterMark", (Item){.item = i2it(js_stream_default_byte_hwm)});
    return state;
}

static void js_stream_set_writable_corked(Item self, int64_t count) {
    if (count < 0) count = 0;
    Item value = (Item){.item = i2it(count)};
    js_property_set(self, make_string_item("_corked"), value);
    js_state_set_item(js_property_get(self, key_writable_state), "corked", value);
}

static void js_stream_set_buffered_request_count(Item self, int64_t count) {
    if (count < 0) count = 0;
    js_state_set_item(js_property_get(self, key_writable_state), "bufferedRequestCount",
                      (Item){.item = i2it(count)});
}

static int64_t js_stream_state_get_int(Item state, const char* name, int64_t fallback) {
    if (get_type_id(state) != LMD_TYPE_MAP) return fallback;
    Item value = js_property_get(state, make_string_item(name));
    if (get_type_id(value) == LMD_TYPE_INT) return it2i(value);
    return fallback;
}

static int64_t js_stream_chunk_length(Item self, Item chunk) {
    Item state = js_property_get(self, key_writable_state);
    if (js_state_get_bool(state, "objectMode")) return 1;
    if (get_type_id(chunk) == LMD_TYPE_STRING) {
        String* str = it2s(chunk);
        return str ? (int64_t)str->len : 0;
    }
    Item length = js_property_get(chunk, make_string_item("length"));
    if (get_type_id(length) == LMD_TYPE_INT) return it2i(length);
    Item byte_length = js_property_get(chunk, make_string_item("byteLength"));
    if (get_type_id(byte_length) == LMD_TYPE_INT) return it2i(byte_length);
    return 1;
}

static bool js_stream_begin_write(Item self, Item chunk) {
    Item state = js_property_get(self, key_writable_state);
    int64_t current = js_stream_state_get_int(state, "length", 0);
    int64_t chunk_len = js_stream_chunk_length(self, chunk);
    int64_t next = current + chunk_len;
    int64_t hwm = js_stream_state_get_int(state, "highWaterMark", 16 * 1024);
    js_state_set_item(state, "length", (Item){.item = i2it(next)});
    bool need_drain = hwm > 0 && next >= hwm;
    if (need_drain) js_state_set_bool(state, "needDrain", true);
    return !need_drain;
}

static void js_stream_set_readable_object_mode(Item obj, bool value) {
    js_property_set(obj, make_string_item("readableObjectMode"), js_bool_item(value));
    js_state_set_bool(js_property_get(obj, key_readable_state), "objectMode", value);
}

static void js_stream_set_writable_object_mode(Item obj, bool value) {
    js_property_set(obj, make_string_item("writableObjectMode"), js_bool_item(value));
    js_state_set_bool(js_property_get(obj, key_writable_state), "objectMode", value);
}

static void js_stream_set_readable_high_water_mark(Item obj, Item value) {
    js_property_set(obj, make_string_item("readableHighWaterMark"), value);
    js_state_set_item(js_property_get(obj, key_readable_state), "highWaterMark", value);
}

static void js_stream_set_writable_high_water_mark(Item obj, Item value) {
    js_property_set(obj, make_string_item("writableHighWaterMark"), value);
    js_state_set_item(js_property_get(obj, key_writable_state), "highWaterMark", value);
}

static void js_stream_init_readable_options(Item obj) {
    js_stream_set_readable_object_mode(obj, false);
    js_stream_set_readable_high_water_mark(obj, (Item){.item = i2it(js_stream_default_byte_hwm)});
}

static void js_stream_init_writable_options(Item obj) {
    js_stream_set_writable_object_mode(obj, false);
    js_stream_set_writable_high_water_mark(obj, (Item){.item = i2it(js_stream_default_byte_hwm)});
}

static void js_stream_set_readable_open(Item self, bool open) {
    js_property_set(self, key_readable, js_bool_item(open));
}

static void js_stream_set_writable_open(Item self, bool open) {
    js_property_set(self, key_writable, js_bool_item(open));
}

static void js_stream_mark_destroyed(Item self) {
    js_property_set(self, key_destroyed, js_bool_item(true));
    js_property_set(self, make_string_item("destroyed"), js_bool_item(true));
    js_stream_set_readable_open(self, false);
    js_stream_set_writable_open(self, false);
}

static void js_stream_set_error_state(Item self, Item err) {
    js_property_set(self, make_string_item("errored"), err);
    Item readable_state = js_property_get(self, key_readable_state);
    js_state_set_item(readable_state, "errored", err);
    Item writable_state = js_property_get(self, key_writable_state);
    js_state_set_item(writable_state, "errored", err);
}

static void js_stream_set_error_emitted(Item self, bool emitted) {
    Item readable_state = js_property_get(self, key_readable_state);
    js_state_set_bool(readable_state, "errorEmitted", emitted);
    Item writable_state = js_property_get(self, key_writable_state);
    js_state_set_bool(writable_state, "errorEmitted", emitted);
}

static void js_stream_mark_readable_end_emitted(Item self) {
    Item state = js_property_get(self, key_readable_state);
    js_state_set_bool(state, "endEmitted", true);
    js_property_set(self, key_end_emitted, js_bool_item(true));
    js_property_set(self, make_string_item("readableEnded"), js_bool_item(true));
    js_stream_set_readable_open(self, false);
    js_property_set(self, make_string_item("readableAborted"), js_bool_item(false));
}

static void js_stream_mark_writable_ended(Item self) {
    Item state = js_property_get(self, key_writable_state);
    js_state_set_bool(state, "ending", true);
    js_state_set_bool(state, "ended", true);
    js_property_set(self, make_string_item("writableEnded"), js_bool_item(true));
    js_stream_set_writable_open(self, false);
}

static void js_stream_mark_writable_finished(Item self) {
    Item state = js_property_get(self, key_writable_state);
    js_state_set_bool(state, "finished", true);
    js_property_set(self, key_finished, js_bool_item(true));
    js_property_set(self, make_string_item("writableFinished"), js_bool_item(true));
}

// =============================================================================
// EventEmitter-like helpers for stream objects
// =============================================================================

static void js_stream_schedule_error(Item self, Item err);
static void js_stream_schedule_callback(Item callback);
static void js_stream_schedule_callback_error(Item callback, Item err);
static void js_stream_flush_pending_writes(Item self);

static Item js_stream_capture_rejection(Item self, Item err) {
    ensure_keys();
    if (js_item_is_true(js_property_get(self, key_destroyed))) {
        return make_js_undefined();
    }
    js_stream_mark_destroyed(self);
    js_stream_schedule_error(self, err);
    return make_js_undefined();
}

static void js_stream_maybe_capture_rejection(Item self, const char* event, Item result) {
    if (!js_item_is_true(js_property_get(self, key_capture_rejections))) return;
    if (event && strcmp(event, "error") == 0) return;
    TypeId result_type = get_type_id(result);
    if (result.item == 0 || result_type == LMD_TYPE_UNDEFINED ||
        result_type == LMD_TYPE_NULL) {
        return;
    }
    Item catch_fn = js_property_get(result, make_string_item("catch"));
    if (get_type_id(catch_fn) != LMD_TYPE_FUNC) return;
    Item bound_args[1] = { self };
    Item handler = js_bind_function(js_new_function((void*)js_stream_capture_rejection, 2),
                                    make_js_undefined(), bound_args, 1);
    js_call_function(catch_fn, result, &handler, 1);
}

static void stream_emit(Item self, const char* event, Item* args, int argc) {
    Item listeners_map = js_property_get(self, key_listeners);
    if (get_type_id(listeners_map) != LMD_TYPE_MAP) {
        if (event && strcmp(event, "error") == 0 && args && argc > 0) {
            js_process_emit(make_string_item("uncaughtException"), args[0]);
        }
        return;
    }
    Item event_key = make_string_item(event);
    Item arr = js_property_get(listeners_map, event_key);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) {
        if (event && strcmp(event, "error") == 0 && args && argc > 0) {
            js_process_emit(make_string_item("uncaughtException"), args[0]);
        }
        return;
    }
    int64_t len = js_array_length(arr);
    if (len == 0 && event && strcmp(event, "error") == 0 && args && argc > 0) {
        js_process_emit(make_string_item("uncaughtException"), args[0]);
        return;
    }
    for (int64_t i = 0; i < len; i++) {
        Item listener = js_array_get_int(arr, i);
        if (get_type_id(listener) == LMD_TYPE_FUNC) {
            Item result = js_call_function(listener, self, args, argc);
            js_stream_maybe_capture_rejection(self, event, result);
        }
    }
}

static bool js_stream_has_callback_error(Item err) {
    return err.item != 0 && get_type_id(err) != LMD_TYPE_UNDEFINED &&
           get_type_id(err) != LMD_TYPE_NULL;
}

static Item js_stream_make_write_request(Item chunk, Item encoding, Item callback) {
    Item request = js_new_object();
    js_property_set(request, make_string_item("chunk"), chunk);
    js_property_set(request, make_string_item("encoding"), encoding);
    js_property_set(request, make_string_item("callback"), callback);
    return request;
}

static void js_stream_buffer_write_request(Item self, Item chunk, Item encoding, Item callback) {
    Item pending = js_property_get(self, make_string_item("_pendingWrites"));
    if (get_type_id(pending) != LMD_TYPE_ARRAY) {
        pending = js_array_new(0);
        js_property_set(self, make_string_item("_pendingWrites"), pending);
    }
    js_array_push(pending, js_stream_make_write_request(chunk, encoding, callback));
    js_stream_set_buffered_request_count(self, js_array_length(pending));
}

static Item js_stream_after_write(Item self, Item callback, Item err) {
    ensure_keys();
    Item state = js_property_get(self, key_writable_state);
    bool need_drain = js_state_get_bool(state, "needDrain");
    bool has_error = js_stream_has_callback_error(err);
    js_property_set(self, make_string_item("_writing"), js_bool_item(false));
    js_state_set_item(state, "length", (Item){.item = i2it(0)});
    js_state_set_bool(state, "needDrain", false);
    if (has_error) {
        js_stream_set_writable_open(self, false);
        js_stream_schedule_error(self, err);
    }

    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        if (has_error) {
            js_call_function(callback, self, &err, 1);
        } else {
            js_call_function(callback, self, NULL, 0);
        }
    }

    if (!has_error && need_drain &&
        !js_state_get_bool(state, "ended") &&
        !js_item_is_true(js_property_get(self, key_destroyed)) &&
        !js_item_is_true(js_property_get(self, key_finish_emitted))) {
        stream_emit(self, "drain", NULL, 0);
    }
    if (!has_error) js_stream_flush_pending_writes(self);
    return make_js_undefined();
}

static Item js_stream_make_write_callback(Item self, Item callback) {
    Item bound_args[2] = { self, callback };
    return js_bind_function(js_new_function((void*)js_stream_after_write, 3),
                            make_js_undefined(), bound_args, 2);
}

static Item js_stream_after_transform_write(Item self, Item callback, Item err, Item data) {
    ensure_keys();
    bool has_error = js_stream_has_callback_error(err);
    if (!has_error && data.item != 0 &&
        get_type_id(data) != LMD_TYPE_UNDEFINED &&
        get_type_id(data) != LMD_TYPE_NULL) {
        js_readable_push(self, data);
        if (js_check_exception()) return ItemNull;
    }
    return js_stream_after_write(self, callback, err);
}

static Item js_stream_make_transform_write_callback(Item self, Item callback) {
    Item bound_args[2] = { self, callback };
    return js_bind_function(js_new_function((void*)js_stream_after_transform_write, 4),
                            make_js_undefined(), bound_args, 2);
}

static Item js_stream_after_writev(Item self, Item pending, Item err) {
    ensure_keys();
    Item state = js_property_get(self, key_writable_state);
    bool need_drain = js_state_get_bool(state, "needDrain");
    bool has_error = js_stream_has_callback_error(err);
    js_property_set(self, make_string_item("_writing"), js_bool_item(false));
    js_state_set_item(state, "length", (Item){.item = i2it(0)});
    js_state_set_bool(state, "needDrain", false);
    if (has_error) {
        js_stream_set_writable_open(self, false);
        js_stream_schedule_error(self, err);
    }

    if (get_type_id(pending) == LMD_TYPE_ARRAY) {
        int64_t plen = js_array_length(pending);
        for (int64_t i = 0; i < plen; i++) {
            Item request = js_array_get_int(pending, i);
            Item callback = js_property_get(request, make_string_item("callback"));
            if (get_type_id(callback) == LMD_TYPE_FUNC) {
                if (has_error) {
                    js_call_function(callback, self, &err, 1);
                } else {
                    js_call_function(callback, self, NULL, 0);
                }
            }
        }
    }

    if (!has_error && need_drain &&
        !js_state_get_bool(state, "ended") &&
        !js_item_is_true(js_property_get(self, key_destroyed)) &&
        !js_item_is_true(js_property_get(self, key_finish_emitted))) {
        stream_emit(self, "drain", NULL, 0);
    }
    if (!has_error) js_stream_flush_pending_writes(self);
    return make_js_undefined();
}

static Item js_stream_make_writev_callback(Item self, Item pending) {
    Item bound_args[2] = { self, pending };
    return js_bind_function(js_new_function((void*)js_stream_after_writev, 3),
                            make_js_undefined(), bound_args, 2);
}

static void js_stream_flush_pending_writes(Item self) {
    if (js_item_is_true(js_property_get(self, make_string_item("_writing")))) return;

    Item pending = js_property_get(self, make_string_item("_pendingWrites"));
    if (get_type_id(pending) != LMD_TYPE_ARRAY) return;
    int64_t plen = js_array_length(pending);
    if (plen <= 0) return;

    js_property_set(self, make_string_item("_pendingWrites"), js_array_new(0));
    js_stream_set_buffered_request_count(self, 0);

    Item writev_fn = js_property_get(self, make_string_item("_writev"));
    Item write_handler = js_property_get(self, make_string_item("_write"));
    if (get_type_id(write_handler) != LMD_TYPE_FUNC) {
        write_handler = js_property_get(self, make_string_item("__write_handler__"));
    }
    if (get_type_id(writev_fn) == LMD_TYPE_FUNC &&
        (plen > 1 || get_type_id(write_handler) != LMD_TYPE_FUNC)) {
        Item writev_cb = js_stream_make_writev_callback(self, pending);
        Item args[2] = {pending, writev_cb};
        js_property_set(self, make_string_item("_writing"), js_bool_item(true));
        js_call_function(writev_fn, self, args, 2);
        return;
    }

    if (get_type_id(write_handler) != LMD_TYPE_FUNC) return;

    for (int64_t i = 0; i < plen; i++) {
        Item request = js_array_get_int(pending, i);
        Item chunk = js_property_get(request, make_string_item("chunk"));
        Item encoding = js_property_get(request, make_string_item("encoding"));
        Item callback = js_property_get(request, make_string_item("callback"));
        Item write_cb = js_stream_make_write_callback(self, callback);
        Item args[3] = {chunk, encoding, write_cb};
        js_property_set(self, make_string_item("_writing"), js_bool_item(true));
        js_call_function(write_handler, self, args, 3);
    }
}

static Item js_stream_emit_end_tick(Item self) {
    ensure_keys();
    if (js_item_is_true(js_property_get(self, key_destroyed)) ||
        js_item_is_true(js_property_get(self, key_end_emitted))) {
        return make_js_undefined();
    }
    Item buf = js_property_get(self, key_buffer);
    if (get_type_id(buf) == LMD_TYPE_ARRAY && js_array_length(buf) > 0) {
        return make_js_undefined();
    }
    js_property_set(self, key_end_pending, js_bool_item(false));
    js_stream_mark_readable_end_emitted(self);
    stream_emit(self, "end", NULL, 0);
    if (js_item_is_true(js_property_get(self, key_auto_destroy)) &&
        !js_item_is_true(js_property_get(self, key_destroyed))) {
        js_stream_mark_destroyed(self);
        js_stream_schedule_close(self);
    }
    js_stream_async_iterators_drain(self, make_js_undefined());
    return make_js_undefined();
}

static Item js_stream_emit_close_tick(Item self) {
    ensure_keys();
    if (js_item_is_true(js_property_get(self, key_close_emitted))) {
        return make_js_undefined();
    }
    js_property_set(self, key_close_emitted, js_bool_item(true));
    stream_emit(self, "close", NULL, 0);
    return make_js_undefined();
}

static void js_stream_schedule_close(Item self) {
    Item bound_args[1] = { self };
    Item tick = js_bind_function(js_new_function((void*)js_stream_emit_close_tick, 1),
                                 make_js_undefined(), bound_args, 1);
    js_next_tick_enqueue(tick);
}

static void js_stream_schedule_end(Item self) {
    if (js_item_is_true(js_property_get(self, key_end_emitted))) return;
    Item bound_args[1] = { self };
    Item tick = js_bind_function(js_new_function((void*)js_stream_emit_end_tick, 1),
                                 make_js_undefined(), bound_args, 1);
    js_next_tick_enqueue(tick);
}

static Item js_stream_emit_error_tick(Item self, Item err) {
    ensure_keys();
    js_stream_set_error_emitted(self, true);
    stream_emit(self, "error", &err, 1);
    return make_js_undefined();
}

static void js_stream_schedule_error(Item self, Item err) {
    js_stream_set_error_state(self, err);
    js_property_set(self, make_string_item("__error__"), err);
    js_stream_async_iterators_drain(self, err);
    Item bound_args[2] = { self, err };
    Item tick = js_bind_function(js_new_function((void*)js_stream_emit_error_tick, 2),
                                 make_js_undefined(), bound_args, 2);
    js_next_tick_enqueue(tick);
}

static void js_stream_schedule_error_immediate(Item self, Item err) {
    js_stream_set_error_state(self, err);
    js_property_set(self, make_string_item("__error__"), err);
    js_stream_async_iterators_drain(self, err);
    Item bound_args[2] = { self, err };
    Item tick = js_bind_function(js_new_function((void*)js_stream_emit_error_tick, 2),
                                 make_js_undefined(), bound_args, 2);
    js_setTimeout(tick, (Item){.item = i2it(1)});
}

static void js_stream_schedule_close_immediate(Item self) {
    Item bound_args[1] = { self };
    Item tick = js_bind_function(js_new_function((void*)js_stream_emit_close_tick, 1),
                                 make_js_undefined(), bound_args, 1);
    js_setTimeout(tick, (Item){.item = i2it(1)});
}

static Item js_stream_after_destroy(Item self, Item err) {
    ensure_keys();
    if (js_stream_has_callback_error(err)) {
        js_stream_set_error_state(self, err);
    }
    if (js_item_is_true(js_property_get(self, make_string_item("__destroying_sync__")))) {
        js_property_set(self, make_string_item("__destroy_cb_done__"), js_bool_item(true));
        js_property_set(self, make_string_item("__destroy_cb_error__"), err);
        return make_js_undefined();
    }
    if (js_stream_has_callback_error(err)) {
        js_stream_schedule_error(self, err);
    }
    js_stream_schedule_close(self);
    return make_js_undefined();
}

static int64_t js_stream_read_size_hint(Item self, Item size_item) {
    if (get_type_id(size_item) == LMD_TYPE_INT && it2i(size_item) > 0)
        return it2i(size_item);
    Item state = js_property_get(self, key_readable_state);
    return js_stream_state_get_int(state, "highWaterMark", js_stream_default_byte_hwm);
}

static void js_stream_call_read_if_needed(Item self, Item size_item) {
    if (js_item_is_true(js_property_get(self, key_destroyed)) ||
        js_item_is_true(js_property_get(self, key_end_pending)) ||
        js_item_is_true(js_property_get(self, key_end_emitted)) ||
        js_item_is_true(js_property_get(self, key_reading))) {
        return;
    }

    Item read_fn = js_property_get(self, make_string_item("_read"));
    if (get_type_id(read_fn) != LMD_TYPE_FUNC) return;

    Item before_buf = js_property_get(self, key_buffer);
    int64_t before_len = get_type_id(before_buf) == LMD_TYPE_ARRAY ? js_array_length(before_buf) : 0;
    js_property_set(self, key_reading, js_bool_item(true));
    Item size = (Item){.item = i2it(js_stream_read_size_hint(self, size_item))};
    js_call_function(read_fn, self, &size, 1);
    Item after_buf = js_property_get(self, key_buffer);
    int64_t after_len = get_type_id(after_buf) == LMD_TYPE_ARRAY ? js_array_length(after_buf) : 0;
    if (after_len > before_len || js_item_is_true(js_property_get(self, key_end_pending))) {
        js_property_set(self, key_reading, js_bool_item(false));
    }

    if (js_item_is_true(js_property_get(self, key_end_pending))) {
        Item flowing = js_property_get(self, key_flowing);
        if (flowing.item != 0 && it2b(flowing)) {
            js_stream_flush_buffered_data(self);
        }
        js_stream_schedule_end(self);
    }
}

static Item js_stream_call_read_tick(Item self) {
    ensure_keys();
    js_stream_call_read_if_needed(self, make_js_undefined());
    return make_js_undefined();
}

static void js_stream_schedule_read(Item self) {
    Item bound_args[1] = { self };
    Item tick = js_bind_function(js_new_function((void*)js_stream_call_read_tick, 1),
                                 make_js_undefined(), bound_args, 1);
    js_next_tick_enqueue(tick);
}

static Item js_stream_decode_readable_chunk(Item self, Item chunk) {
    if (js_stream_readable_is_object_mode(self)) return chunk;
    Item encoding = js_property_get(self, make_string_item("_encoding"));
    if (get_type_id(encoding) != LMD_TYPE_STRING) return chunk;
    if (get_type_id(chunk) == LMD_TYPE_STRING) return chunk;
    return js_buffer_toString(chunk, encoding, make_js_undefined(), make_js_undefined());
}

static void js_stream_flush_buffered_data(Item self) {
    for (;;) {
        Item buf = js_property_get(self, key_buffer);
        if (get_type_id(buf) != LMD_TYPE_ARRAY) return;
        int64_t blen = js_array_length(buf);
        if (blen <= 0) {
            if (js_item_is_true(js_property_get(self, key_end_pending)) &&
                !js_item_is_true(js_property_get(self, key_end_emitted))) {
                js_stream_schedule_end(self);
            }
            return;
        }

        Item chunk = js_array_get_int(buf, 0);
        Item next_buf = js_array_new(0);
        for (int64_t i = 1; i < blen; i++) {
            js_array_push(next_buf, js_array_get_int(buf, i));
        }
        js_stream_set_readable_buffer(self, next_buf);

        Item emitted = js_stream_decode_readable_chunk(self, chunk);
        stream_emit(self, "data", &emitted, 1);
    }
}

static Item js_stream_flush_data_tick(Item self) {
    ensure_keys();
    if (js_item_is_true(js_property_get(self, key_destroyed))) {
        return make_js_undefined();
    }
    js_stream_flush_buffered_data(self);
    js_stream_call_read_if_needed(self, make_js_undefined());
    return make_js_undefined();
}

static void js_stream_schedule_data_flush(Item self) {
    Item bound_args[1] = { self };
    Item tick = js_bind_function(js_new_function((void*)js_stream_flush_data_tick, 1),
                                 make_js_undefined(), bound_args, 1);
    js_next_tick_enqueue(tick);
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
        arr = js_array_new(0);
        js_property_set(listeners_map, event_item, arr);
    }
    js_array_push(arr, listener);

    // if adding 'data' listener to readable, start flowing mode
    bool is_data_event = js_stream_string_equals(event_item, "data");
    bool is_readable_event = js_stream_string_equals(event_item, "readable");
    bool is_end_event = js_stream_string_equals(event_item, "end");
    bool is_finish_event = js_stream_string_equals(event_item, "finish");

    if (is_readable_event) {
        js_state_set_bool(js_property_get(self, key_readable_state), "readableListening", true);
        js_stream_call_read_if_needed(self, make_js_undefined());
    }

    if (is_data_event) {
        js_property_set(self, key_flowing, (Item){.item = b2it(true)});
        js_property_set(self, key_paused, js_bool_item(false));
        if (js_item_is_true(js_property_get(self, key_capture_rejections))) {
            js_stream_schedule_data_flush(self);
        } else {
            js_stream_flush_buffered_data(self);
            js_stream_call_read_if_needed(self, make_js_undefined());
        }
    }
    if (is_end_event &&
        js_item_is_true(js_property_get(self, key_end_pending)) &&
        !js_item_is_true(js_property_get(self, key_end_emitted))) {
        js_stream_emit_end_tick(self);
    }
    if (is_finish_event &&
        (js_item_is_true(js_property_get(self, key_finish_emitted)) ||
         js_state_get_bool(js_property_get(self, key_writable_state), "finished"))) {
        if (get_type_id(listener) == LMD_TYPE_FUNC) {
            Item result = js_call_function(listener, self, NULL, 0);
            js_stream_maybe_capture_rejection(self, "finish", result);
        }
    }
    return self;
}

// off(event, listener)
extern "C" Item js_stream_off(Item self, Item event_item, Item listener) {
    ensure_keys();
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;

    Item listeners_map = js_property_get(self, key_listeners);
    if (get_type_id(listeners_map) != LMD_TYPE_MAP) return self;
    Item arr = js_property_get(listeners_map, event_item);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) return self;

    Item next = js_array_new(0);
    int64_t len = js_array_length(arr);
    for (int64_t i = 0; i < len; i++) {
        Item current = js_array_get_int(arr, i);
        if (current.item != listener.item) js_array_push(next, current);
    }
    js_property_set(listeners_map, event_item, next);
    if (js_stream_string_equals(event_item, "readable") && js_array_length(next) == 0) {
        js_state_set_bool(js_property_get(self, key_readable_state), "readableListening", false);
    }
    return self;
}

extern "C" Item js_stream_removeAllListeners(Item self, Item event_item) {
    ensure_keys();
    Item listeners_map = js_property_get(self, key_listeners);
    if (get_type_id(listeners_map) != LMD_TYPE_MAP) return self;
    if (event_item.item == 0 || get_type_id(event_item) == LMD_TYPE_UNDEFINED) {
        js_property_set(self, key_listeners, js_new_object());
        js_state_set_bool(js_property_get(self, key_readable_state), "readableListening", false);
        js_stream_call_read_if_needed(self, make_js_undefined());
        return self;
    }
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    js_property_set(listeners_map, event_item, js_array_new(0));
    if (js_stream_string_equals(event_item, "readable")) {
        js_state_set_bool(js_property_get(self, key_readable_state), "readableListening", false);
        js_stream_call_read_if_needed(self, make_js_undefined());
    }
    return self;
}

static Item js_stream_once_wrapper(Item env_item, Item arg1) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item self = env[0];
    Item event_item = env[1];
    Item listener = env[2];
    Item wrapper = env[3];
    js_stream_off(self, event_item, wrapper);
    if (get_type_id(listener) != LMD_TYPE_FUNC) return make_js_undefined();
    if (arg1.item == 0 || get_type_id(arg1) == LMD_TYPE_UNDEFINED) {
        return js_call_function(listener, self, NULL, 0);
    }
    return js_call_function(listener, self, &arg1, 1);
}

extern "C" Item js_stream_once(Item self, Item event_item, Item listener) {
    ensure_keys();
    if (get_type_id(event_item) != LMD_TYPE_STRING ||
        get_type_id(listener) != LMD_TYPE_FUNC) {
        return self;
    }
    Item* env = js_alloc_env(4);
    env[0] = self;
    env[1] = event_item;
    env[2] = listener;
    env[3] = make_js_undefined();
    Item wrapper = js_new_closure((void*)js_stream_once_wrapper, 1, env, 4);
    env[3] = wrapper;
    return js_stream_on(self, event_item, wrapper);
}

// eventNames() — return currently registered event names.
extern "C" Item js_stream_eventNames(Item self) {
    ensure_keys();
    Item listeners_map = js_property_get(self, key_listeners);
    if (get_type_id(listeners_map) != LMD_TYPE_MAP) return js_array_new(0);

    Item all_keys = js_object_keys(listeners_map);
    Item result = js_array_new(0);
    int64_t len = js_array_length(all_keys);
    for (int64_t i = len; i > 0; i--) {
        Item key = js_array_get_int(all_keys, i - 1);
        Item listeners = js_property_get(listeners_map, key);
        if (get_type_id(listeners) == LMD_TYPE_ARRAY && js_array_length(listeners) > 0) {
            js_array_push(result, key);
        }
    }
    return result;
}

extern "C" Item js_stream_listenerCount(Item self, Item event_item, Item listener) {
    ensure_keys();
    if (get_type_id(event_item) != LMD_TYPE_STRING) return (Item){.item = i2it(0)};
    Item listeners_map = js_property_get(self, key_listeners);
    if (get_type_id(listeners_map) != LMD_TYPE_MAP) return (Item){.item = i2it(0)};
    Item arr = js_property_get(listeners_map, event_item);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) return (Item){.item = i2it(0)};
    int64_t len = js_array_length(arr);
    if (listener.item == 0 || get_type_id(listener) == LMD_TYPE_UNDEFINED) {
        return (Item){.item = i2it(len)};
    }
    int64_t count = 0;
    for (int64_t i = 0; i < len; i++) {
        Item current = js_array_get_int(arr, i);
        if (current.item == listener.item) count++;
    }
    return (Item){.item = i2it(count)};
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

// push(chunk[, encoding]) — add data to readable stream
static Item js_readable_push_encoded(Item self, Item chunk, Item encoding) {
    ensure_keys();

    // null signals end of stream
    if (chunk.item == 0 || get_type_id(chunk) == LMD_TYPE_NULL) {
        js_property_set(self, key_reading, js_bool_item(false));
        if (js_item_is_true(js_property_get(self, key_destroyed)) ||
            js_item_is_true(js_property_get(self, key_end_pending)) ||
            js_item_is_true(js_property_get(self, key_end_emitted))) {
            return js_bool_item(true);
        }

        js_property_set(self, key_ended, js_bool_item(true));
        Item state = js_property_get(self, key_readable_state);
        js_state_set_bool(state, "ended", true);
        Item pipe_dest = js_property_get(self, make_string_item("__pipe_dest__"));
        if (js_item_is_true(js_property_get(pipe_dest, key_destroyed))) {
            js_readable_clear_pipe(self);
        } else {
            Item end_fn = js_property_get(pipe_dest, key_end);
            if (get_type_id(end_fn) == LMD_TYPE_FUNC) {
                js_call_function(end_fn, pipe_dest, NULL, 0);
            }
        }

        js_property_set(self, key_end_pending, js_bool_item(true));
        Item buf = js_property_get(self, key_buffer);
        bool has_buffered = get_type_id(buf) == LMD_TYPE_ARRAY && js_array_length(buf) > 0;
        if (has_buffered) {
            Item flowing = js_property_get(self, key_flowing);
            if (flowing.item != 0 && it2b(flowing)) {
                js_stream_schedule_data_flush(self);
            } else if (js_state_get_bool(state, "readableListening")) {
                stream_emit(self, "readable", NULL, 0);
            }
        }
        js_stream_async_iterators_drain(self, make_js_undefined());
        js_stream_schedule_end(self);
        return js_bool_item(true);
    }

    js_property_set(self, key_reading, js_bool_item(false));

    if (js_item_is_true(js_property_get(self, key_end_pending)) ||
        js_item_is_true(js_property_get(self, key_end_emitted))) {
        return js_throw_error_with_code("ERR_STREAM_PUSH_AFTER_EOF",
                                        "stream.push() after EOF");
    }
    if (!js_item_is_true(js_property_get(self, key_readable))) {
        Item err = js_stream_make_error_with_code("ERR_STREAM_PUSH_AFTER_EOF",
            "stream.push() after EOF");
        js_stream_schedule_error(self, err);
        return js_bool_item(false);
    }

    if (!js_stream_prepare_readable_chunk(self, &chunk, encoding)) return ItemNull;

    Item pipe_dest = js_property_get(self, make_string_item("__pipe_dest__"));
    if (js_item_is_true(js_property_get(pipe_dest, key_destroyed))) {
        js_readable_clear_pipe(self);
        return js_bool_item(false);
    }
    Item write_fn = js_property_get(pipe_dest, key_write);
    if (get_type_id(write_fn) == LMD_TYPE_FUNC) {
        Item result = js_call_function(write_fn, pipe_dest, &chunk, 1);
        if (js_check_exception()) {
            Item err = js_clear_exception();
            js_stream_schedule_error(pipe_dest, err);
            return js_bool_item(false);
        }
        if (get_type_id(result) == LMD_TYPE_BOOL && !it2b(result)) {
            Item dest_error = js_property_get(pipe_dest, make_string_item("__error__"));
            if (js_stream_has_callback_error(dest_error)) {
                js_property_set(self, make_string_item("__piped__"), js_bool_item(false));
                js_property_set(self, make_string_item("__pipe_dest__"), make_js_undefined());
                return js_bool_item(false);
            }
            js_property_set(self, key_flowing, js_bool_item(false));
            js_property_set(self, key_paused, js_bool_item(true));
            js_property_set(self, make_string_item("__piped__"), js_bool_item(false));
            js_property_set(self, make_string_item("__pipe_dest__"), make_js_undefined());
            js_stream_call_read_if_needed(self, make_js_undefined());
            return js_bool_item(false);
        }
    }

    Item buf = js_property_get(self, key_buffer);
    if (get_type_id(buf) != LMD_TYPE_ARRAY) {
        buf = js_array_new(0);
        js_stream_set_readable_buffer(self, buf);
    }
    js_array_push(buf, chunk);
    Item flowing = js_property_get(self, key_flowing);
    js_stream_async_iterators_drain(self, make_js_undefined());
    if (flowing.item != 0 && it2b(flowing)) {
        js_stream_schedule_data_flush(self);
    } else if (js_state_get_bool(js_property_get(self, key_readable_state), "readableListening")) {
        stream_emit(self, "readable", NULL, 0);
    }
    return js_bool_item(js_stream_readable_accepts_more(self, buf));
}

extern "C" Item js_readable_push(Item self, Item chunk) {
    return js_readable_push_encoded(self, chunk, make_js_undefined());
}

// unshift(chunk) — prepend data to readable stream
extern "C" Item js_readable_unshift_encoded(Item self, Item chunk, Item encoding) {
    ensure_keys();
    if (chunk.item == 0 || get_type_id(chunk) == LMD_TYPE_NULL) {
        return js_readable_push(self, chunk);
    }
    if (!js_item_is_true(js_property_get(self, key_readable)) ||
        js_item_is_true(js_property_get(self, key_end_emitted))) {
        Item err = js_stream_make_error_with_code("ERR_STREAM_PUSH_AFTER_EOF",
            "stream.unshift() after end event");
        js_stream_schedule_error(self, err);
        return js_bool_item(false);
    }
    if (!js_stream_prepare_readable_chunk(self, &chunk, encoding)) return ItemNull;
    if (!js_stream_readable_is_object_mode(self) &&
        js_stream_readable_chunk_length(self, chunk) == 0) {
        return js_bool_item(true);
    }

    Item buf = js_property_get(self, key_buffer);
    if (get_type_id(buf) != LMD_TYPE_ARRAY) {
        buf = js_array_new(0);
    }
    int64_t blen = js_array_length(buf);
    Item new_buf = js_array_new(0);
    js_array_push(new_buf, chunk);
    for (int64_t i = 0; i < blen; i++) {
        js_array_push(new_buf, js_array_get_int(buf, i));
    }
    js_stream_set_readable_buffer(self, new_buf);
    js_stream_iter_maybe_drain(self);
    if (js_item_is_true(js_property_get(self, key_end_pending)) &&
        !js_item_is_true(js_property_get(self, key_end_emitted))) {
        return js_bool_item(true);
    }
    Item flowing = js_property_get(self, key_flowing);
    if (flowing.item != 0 && it2b(flowing)) {
        if (js_stream_readable_is_object_mode(self) ||
            js_item_is_true(js_property_get(self, key_capture_rejections))) {
            js_stream_schedule_data_flush(self);
        } else {
            js_stream_flush_buffered_data(self);
            js_stream_call_read_if_needed(self, make_js_undefined());
        }
    } else if (js_state_get_bool(js_property_get(self, key_readable_state), "readableListening")) {
        stream_emit(self, "readable", NULL, 0);
    }
    return js_bool_item(js_stream_readable_accepts_more(self, new_buf));
}

extern "C" Item js_readable_unshift(Item self, Item chunk) {
    return js_readable_unshift_encoded(self, chunk, make_js_undefined());
}

static int64_t js_stream_readable_chunk_length(Item self, Item chunk) {
    Item state = js_property_get(self, key_readable_state);
    if (js_state_get_bool(state, "objectMode")) return 1;
    if (get_type_id(chunk) == LMD_TYPE_STRING) {
        String* str = it2s(chunk);
        return str ? (int64_t)str->len : 0;
    }
    Item byte_length = js_property_get(chunk, make_string_item("byteLength"));
    if (get_type_id(byte_length) == LMD_TYPE_INT) return it2i(byte_length);
    Item length = js_property_get(chunk, make_string_item("length"));
    if (get_type_id(length) == LMD_TYPE_INT) return it2i(length);
    return 0;
}

static int64_t js_stream_readable_buffer_length(Item self, Item buf) {
    if (get_type_id(buf) != LMD_TYPE_ARRAY) return 0;
    int64_t total = 0;
    int64_t len = js_array_length(buf);
    for (int64_t i = 0; i < len; i++) {
        total += js_stream_readable_chunk_length(self, js_array_get_int(buf, i));
    }
    return total;
}

static bool js_stream_readable_accepts_more(Item self, Item buf) {
    int64_t length = js_stream_readable_buffer_length(self, buf);
    if (length == 0) return true;
    Item state = js_property_get(self, key_readable_state);
    int64_t hwm = js_stream_state_get_int(state, "highWaterMark", js_stream_default_byte_hwm);
    return length < hwm;
}

static bool js_stream_readable_buffer_has_string(Item buf) {
    if (get_type_id(buf) != LMD_TYPE_ARRAY) return false;
    int64_t len = js_array_length(buf);
    for (int64_t i = 0; i < len; i++) {
        if (get_type_id(js_array_get_int(buf, i)) == LMD_TYPE_STRING) return true;
    }
    return false;
}

static Item js_stream_concat_decoded_chunks(Item buf, Item encoding) {
    if (get_type_id(buf) != LMD_TYPE_ARRAY) return ItemNull;
    StrBuf* sb = strbuf_new_cap(64);
    if (!sb) return ItemNull;
    int64_t len = js_array_length(buf);
    for (int64_t i = 0; i < len; i++) {
        Item chunk = js_array_get_int(buf, i);
        Item text = chunk;
        if (get_type_id(text) != LMD_TYPE_STRING) {
            text = js_buffer_toString(chunk, encoding, make_js_undefined(), make_js_undefined());
            if (js_check_exception()) {
                strbuf_free(sb);
                return ItemNull;
            }
        }
        if (get_type_id(text) != LMD_TYPE_STRING) {
            strbuf_free(sb);
            return ItemNull;
        }
        String* str = it2s(text);
        if (str && str->len > 0) {
            strbuf_append_str_n(sb, str->chars, str->len);
        }
    }
    String* result = heap_create_name(sb->str, sb->length);
    strbuf_free(sb);
    return (Item){.item = s2it(result)};
}

static Item js_readable_read_exact(Item self, Item buf, int64_t blen, int64_t want) {
    if (want <= 0 || js_stream_readable_is_object_mode(self)) return ItemNull;

    int64_t available = 0;
    for (int64_t i = 0; i < blen; i++) {
        available += js_stream_readable_chunk_length(self, js_array_get_int(buf, i));
        if (available >= want) break;
    }
    if (available < want && !js_item_is_true(js_property_get(self, key_end_pending))) {
        js_stream_call_read_if_needed(self, (Item){.item = i2it(want)});
        return ItemNull;
    }
    if (available == 0) return ItemNull;
    if (want > available) want = available;

    Item parts = js_array_new(0);
    Item new_buf = js_array_new(0);
    int64_t remaining = want;
    bool split = false;
    for (int64_t i = 0; i < blen; i++) {
        Item chunk = js_array_get_int(buf, i);
        if (split) {
            js_array_push(new_buf, chunk);
            continue;
        }
        int64_t chunk_len = js_stream_readable_chunk_length(self, chunk);
        if (chunk_len <= remaining) {
            js_array_push(parts, chunk);
            remaining -= chunk_len;
            if (remaining == 0) split = true;
            continue;
        }
        Item head = js_buffer_slice(chunk, (Item){.item = i2it(0)}, (Item){.item = i2it(remaining)});
        Item tail = js_buffer_slice(chunk, (Item){.item = i2it(remaining)}, make_js_undefined());
        js_array_push(parts, head);
        js_array_push(new_buf, tail);
        remaining = 0;
        split = true;
    }
    js_stream_set_readable_buffer(self, new_buf);
    if (js_array_length(new_buf) == 0) {
        if (js_item_is_true(js_property_get(self, key_end_pending)))
            js_stream_schedule_end(self);
        else
            js_stream_schedule_read(self);
    }

    int64_t part_count = js_array_length(parts);
    if (part_count == 1) return js_array_get_int(parts, 0);
    return js_buffer_concat(parts, (Item){.item = i2it(want)});
}

// read() — pull one chunk from buffer (non-flowing mode)
extern "C" Item js_readable_read_size(Item self, Item size_item) {
    ensure_keys();
    Item buf = js_property_get(self, key_buffer);
    if (get_type_id(buf) != LMD_TYPE_ARRAY || js_array_length(buf) == 0) {
        js_stream_call_read_if_needed(self, size_item);
        buf = js_property_get(self, key_buffer);
    }
    if (get_type_id(buf) != LMD_TYPE_ARRAY) return ItemNull;
    int64_t blen = js_array_length(buf);

    if (get_type_id(size_item) == LMD_TYPE_INT && it2i(size_item) == 0) {
        while (!js_item_is_true(js_property_get(self, key_end_pending)) &&
               !js_item_is_true(js_property_get(self, key_end_emitted)) &&
               js_stream_readable_accepts_more(self, buf)) {
            int64_t before_len = js_array_length(buf);
            js_stream_call_read_if_needed(self, size_item);
            buf = js_property_get(self, key_buffer);
            if (get_type_id(buf) != LMD_TYPE_ARRAY) return ItemNull;
            int64_t after_len = js_array_length(buf);
            if (after_len <= before_len) break;
        }
        return ItemNull;
    }

    if (blen == 0) return ItemNull;

    if (get_type_id(size_item) == LMD_TYPE_INT && it2i(size_item) > 0) {
        Item exact = js_readable_read_exact(self, buf, blen, it2i(size_item));
        if (exact.item == 0 || get_type_id(exact) == LMD_TYPE_NULL ||
            get_type_id(exact) == LMD_TYPE_UNDEFINED) {
            return ItemNull;
        }
        return js_stream_decode_readable_chunk(self, exact);
    }

    if (blen == 0 && !js_item_is_true(js_property_get(self, key_end_pending))) {
        js_stream_call_read_if_needed(self, size_item);
        buf = js_property_get(self, key_buffer);
        if (get_type_id(buf) != LMD_TYPE_ARRAY) return ItemNull;
        blen = js_array_length(buf);
        if (blen == 0) return ItemNull;
    }

    Item encoding = js_property_get(self, make_string_item("_encoding"));
    if (get_type_id(encoding) == LMD_TYPE_STRING) {
        Item joined = blen == 1
            ? js_array_get_int(buf, 0)
            : (js_stream_readable_buffer_has_string(buf)
                ? js_stream_concat_decoded_chunks(buf, encoding)
                : js_buffer_concat(buf, make_js_undefined()));
        if (js_check_exception()) return ItemNull;
        js_stream_set_readable_buffer(self, js_array_new(0));
        js_stream_iter_maybe_drain(self);
        if (js_item_is_true(js_property_get(self, key_end_pending)))
            js_stream_schedule_end(self);
        else
            js_stream_schedule_read(self);
        if (get_type_id(joined) == LMD_TYPE_STRING) return joined;
        return js_buffer_toString(joined, encoding, make_js_undefined(), make_js_undefined());
    }

    // get first element (shift not directly supported — rebuild array)
    Item result = js_array_get_int(buf, 0);
    Item new_buf = js_array_new(0);
    for (int64_t i = 1; i < blen; i++) {
        js_array_push(new_buf, js_array_get_int(buf, i));
    }
    js_stream_set_readable_buffer(self, new_buf);
    js_stream_iter_maybe_drain(self);
    if (js_array_length(new_buf) == 0) {
        if (js_item_is_true(js_property_get(self, key_end_pending)))
            js_stream_schedule_end(self);
        else
            js_stream_schedule_read(self);
    }
    return result;
}

extern "C" Item js_readable_read(Item self) {
    return js_readable_read_size(self, make_js_undefined());
}

static Item js_stream_iterator_result(Item value, bool done) {
    Item result = js_new_object();
    js_property_set(result, make_string_item("value"), value);
    js_property_set(result, make_string_item("done"), js_bool_item(done));
    return result;
}

static bool js_stream_async_iterator_has_value(Item value) {
    return value.item != 0 &&
           get_type_id(value) != LMD_TYPE_NULL &&
           get_type_id(value) != LMD_TYPE_UNDEFINED;
}

static bool js_stream_async_iterator_stream_done(Item stream) {
    return !js_item_is_true(js_property_get(stream, key_readable)) ||
           js_item_is_true(js_property_get(stream, key_end_pending)) ||
           js_item_is_true(js_property_get(stream, key_end_emitted)) ||
           js_item_is_true(js_property_get(stream, key_ended));
}

static Item js_stream_async_iterator_pending_queue(Item iterator) {
    Item key = make_string_item("__pending_queue__");
    Item queue = js_property_get(iterator, key);
    if (get_type_id(queue) != LMD_TYPE_ARRAY) {
        queue = js_array_new(0);
        js_property_set(iterator, key, queue);
    }
    return queue;
}

static Item js_stream_array_shift_property(Item obj, Item key) {
    Item queue = js_property_get(obj, key);
    if (get_type_id(queue) != LMD_TYPE_ARRAY) return make_js_undefined();
    int64_t len = js_array_length(queue);
    if (len <= 0) return make_js_undefined();

    Item value = js_array_get_int(queue, 0);
    Item next_queue = js_array_new(0);
    for (int64_t i = 1; i < len; i++) {
        js_array_push(next_queue, js_array_get_int(queue, i));
    }
    js_property_set(obj, key, next_queue);
    return value;
}

static int64_t js_stream_async_iterator_pending_count(Item iterator) {
    Item queue = js_property_get(iterator, make_string_item("__pending_queue__"));
    if (get_type_id(queue) != LMD_TYPE_ARRAY) return 0;
    return js_array_length(queue);
}

static Item js_stream_async_iterator_shift_pending(Item iterator) {
    Item key = make_string_item("__pending_queue__");
    Item queue = js_property_get(iterator, key);
    if (get_type_id(queue) != LMD_TYPE_ARRAY) return make_js_undefined();
    int64_t len = js_array_length(queue);
    if (len <= 0) return make_js_undefined();

    Item capability = js_stream_array_shift_property(iterator, key);
    js_property_set(iterator, make_string_item("__pending__"), js_bool_item(len > 1));
    return capability;
}

static void js_stream_async_iterator_clear_pending(Item iterator) {
    js_property_set(iterator, make_string_item("__pending__"), js_bool_item(false));
    js_property_set(iterator, make_string_item("__resolve__"), make_js_undefined());
    js_property_set(iterator, make_string_item("__reject__"), make_js_undefined());
    js_property_set(iterator, make_string_item("__pending_queue__"), js_array_new(0));
}

static void js_stream_async_iterator_resolve(Item iterator, Item result) {
    Item capability = js_stream_async_iterator_shift_pending(iterator);
    Item resolve = js_property_get(capability, make_string_item("resolve"));
    if (get_type_id(resolve) != LMD_TYPE_FUNC) {
        resolve = js_property_get(iterator, make_string_item("__resolve__"));
    }
    if (get_type_id(resolve) == LMD_TYPE_FUNC) {
        Item args[1] = { result };
        js_call_function(resolve, make_js_undefined(), args, 1);
    }
}

static void js_stream_async_iterator_reject(Item iterator, Item err) {
    Item capability = js_stream_async_iterator_shift_pending(iterator);
    Item reject = js_property_get(capability, make_string_item("reject"));
    if (get_type_id(reject) != LMD_TYPE_FUNC) {
        reject = js_property_get(iterator, make_string_item("__reject__"));
    }
    js_property_set(iterator, make_string_item("__done__"), js_bool_item(true));
    if (get_type_id(reject) == LMD_TYPE_FUNC) {
        Item args[1] = { err };
        js_call_function(reject, make_js_undefined(), args, 1);
    }
}

static void js_stream_async_iterator_resolve_all_done(Item iterator) {
    js_property_set(iterator, make_string_item("__done__"), js_bool_item(true));
    while (js_stream_async_iterator_pending_count(iterator) > 0) {
        js_stream_async_iterator_resolve(iterator,
            js_stream_iterator_result(make_js_undefined(), true));
    }
}

static void js_stream_async_iterator_reject_all(Item iterator, Item err) {
    js_property_set(iterator, make_string_item("__done__"), js_bool_item(true));
    while (js_stream_async_iterator_pending_count(iterator) > 0) {
        js_stream_async_iterator_reject(iterator, err);
    }
}

static bool js_stream_async_iterator_is_legacy_stream(Item stream) {
    Item state = js_property_get(stream, key_readable_state);
    TypeId state_type = get_type_id(state);
    if (state_type == LMD_TYPE_MAP || state_type == LMD_TYPE_ELEMENT) return false;
    if (js_item_is_true(js_property_get(stream, key_readable))) return false;
    return get_type_id(js_property_get(stream, key_on)) == LMD_TYPE_FUNC;
}

static void js_stream_async_iterator_cleanup_stream(Item stream) {
    if (get_type_id(stream) != LMD_TYPE_MAP && get_type_id(stream) != LMD_TYPE_ELEMENT) return;
    Item destroy = js_property_get(stream, key_destroy);
    if (get_type_id(destroy) == LMD_TYPE_FUNC) {
        js_call_function(destroy, stream, NULL, 0);
        return;
    }
    Item close = js_property_get(stream, make_string_item("close"));
    if (get_type_id(close) == LMD_TYPE_FUNC) {
        js_call_function(close, stream, NULL, 0);
    }
}

static void js_stream_async_iterator_drain_legacy(Item iterator) {
    Item err = js_property_get(iterator, make_string_item("__event_error__"));
    if (js_stream_has_callback_error(err) && js_stream_async_iterator_pending_count(iterator) > 0) {
        js_stream_async_iterator_reject(iterator, err);
        js_stream_async_iterator_resolve_all_done(iterator);
        Item stream = js_property_get(iterator, make_string_item("__stream__"));
        js_stream_async_iterator_cleanup_stream(stream);
        return;
    }

    Item buffer_key = make_string_item("__event_buffer__");
    Item buffer = js_property_get(iterator, buffer_key);
    while (get_type_id(buffer) == LMD_TYPE_ARRAY &&
           js_array_length(buffer) > 0 &&
           js_stream_async_iterator_pending_count(iterator) > 0) {
        Item chunk = js_stream_array_shift_property(iterator, buffer_key);
        js_stream_async_iterator_resolve(iterator,
            js_stream_iterator_result(chunk, false));
        buffer = js_property_get(iterator, buffer_key);
    }
    if (js_item_is_true(js_property_get(iterator, make_string_item("__event_done__"))) &&
        js_stream_async_iterator_pending_count(iterator) > 0) {
        js_stream_async_iterator_resolve_all_done(iterator);
        Item stream = js_property_get(iterator, make_string_item("__stream__"));
        js_stream_async_iterator_cleanup_stream(stream);
    }
}

static Item js_stream_async_iterator_legacy_data(Item env_item, Item chunk) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item iterator = env[0];
    Item buffer_key = make_string_item("__event_buffer__");
    Item buffer = js_property_get(iterator, buffer_key);
    if (get_type_id(buffer) != LMD_TYPE_ARRAY) {
        buffer = js_array_new(0);
        js_property_set(iterator, buffer_key, buffer);
    }
    js_array_push(buffer, chunk);
    return make_js_undefined();
}

static Item js_stream_async_iterator_legacy_end(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item iterator = env[0];
    js_property_set(iterator, make_string_item("__event_done__"), js_bool_item(true));
    js_stream_async_iterator_drain_legacy(iterator);
    return make_js_undefined();
}

static Item js_stream_async_iterator_legacy_error(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item iterator = env[0];
    js_property_set(iterator, make_string_item("__event_error__"), err);
    js_stream_async_iterator_drain_legacy(iterator);
    return make_js_undefined();
}

static void js_stream_async_iterator_setup_legacy(Item iterator, Item stream) {
    if (js_item_is_true(js_property_get(iterator, make_string_item("__legacy_listening__")))) return;
    Item on = js_property_get(stream, key_on);
    if (get_type_id(on) != LMD_TYPE_FUNC) return;

    Item* env = js_alloc_env(1);
    env[0] = iterator;
    Item data = js_new_closure((void*)js_stream_async_iterator_legacy_data, 1, env, 1);
    Item end = js_new_closure((void*)js_stream_async_iterator_legacy_end, 0, env, 1);
    Item error = js_new_closure((void*)js_stream_async_iterator_legacy_error, 1, env, 1);

    Item args[2] = { make_string_item("data"), data };
    js_call_function(on, stream, args, 2);
    args[0] = make_string_item("end"); args[1] = end;
    js_call_function(on, stream, args, 2);
    args[0] = make_string_item("error"); args[1] = error;
    js_call_function(on, stream, args, 2);

    js_property_set(iterator, make_string_item("__legacy_listening__"), js_bool_item(true));
}

static Item js_stream_async_iterator_pending_promise(Item iterator, Item stream) {
    Item capability = js_promise_with_resolvers();
    if (js_check_exception()) return ItemNull;
    Item queue = js_stream_async_iterator_pending_queue(iterator);
    js_array_push(queue, capability);
    js_property_set(iterator, make_string_item("__pending__"), js_bool_item(true));
    js_property_set(iterator, make_string_item("__resolve__"),
                    js_property_get(capability, make_string_item("resolve")));
    js_property_set(iterator, make_string_item("__reject__"),
                    js_property_get(capability, make_string_item("reject")));
    if (js_stream_async_iterator_is_legacy_stream(stream)) {
        js_stream_async_iterator_setup_legacy(iterator, stream);
        js_stream_async_iterator_drain_legacy(iterator);
    } else {
        js_stream_call_read_if_needed(stream, make_js_undefined());
        js_stream_async_iterators_drain(stream, make_js_undefined());
    }
    return js_property_get(capability, make_string_item("promise"));
}

static void js_stream_async_iterators_drain(Item stream, Item err) {
    ensure_keys();
    Item iterators = js_property_get(stream, make_string_item("__async_iterators__"));
    if (get_type_id(iterators) != LMD_TYPE_ARRAY) return;

    bool has_error = err.item != 0 &&
                     get_type_id(err) != LMD_TYPE_UNDEFINED &&
                     get_type_id(err) != LMD_TYPE_NULL;
    int64_t len = js_array_length(iterators);
    for (int64_t i = 0; i < len; i++) {
        Item iterator = js_array_get_int(iterators, i);
        if (js_stream_async_iterator_pending_count(iterator) <= 0) {
            continue;
        }
        if (has_error) {
            js_stream_async_iterator_reject(iterator, err);
            js_stream_async_iterator_resolve_all_done(iterator);
            continue;
        }

        while (js_stream_async_iterator_pending_count(iterator) > 0) {
            Item chunk = js_readable_read(stream);
            if (js_stream_async_iterator_has_value(chunk)) {
                js_stream_async_iterator_resolve(iterator,
                    js_stream_iterator_result(chunk, false));
                continue;
            }
            if (js_stream_async_iterator_stream_done(stream)) {
                if (js_item_is_true(js_property_get(stream, key_end_pending)) &&
                    !js_item_is_true(js_property_get(stream, key_end_emitted))) {
                    js_stream_emit_end_tick(stream);
                } else {
                    js_stream_async_iterator_resolve_all_done(iterator);
                }
            }
            break;
        }
    }
}

static Item js_stream_async_iterator_next(Item iterator) {
    ensure_keys();
    if (js_item_is_true(js_property_get(iterator, make_string_item("__done__")))) {
        return js_promise_resolve(js_stream_iterator_result(make_js_undefined(), true));
    }
    Item stored_error = js_property_get(iterator, make_string_item("__error__"));
    if (js_stream_has_callback_error(stored_error)) {
        js_property_set(iterator, make_string_item("__done__"), js_bool_item(true));
        return js_promise_reject(stored_error);
    }

    Item stream = js_property_get(iterator, make_string_item("__stream__"));
    TypeId stream_tid = get_type_id(stream);
    if (stream_tid != LMD_TYPE_MAP && stream_tid != LMD_TYPE_ELEMENT) {
        js_property_set(iterator, make_string_item("__done__"), js_bool_item(true));
        return js_promise_resolve(js_stream_iterator_result(make_js_undefined(), true));
    }
    stored_error = js_property_get(stream, make_string_item("__error__"));
    if (js_stream_has_callback_error(stored_error)) {
        js_property_set(iterator, make_string_item("__done__"), js_bool_item(true));
        return js_promise_reject(stored_error);
    }
    if (js_item_is_true(js_property_get(stream, make_string_item("__iter_failed__")))) {
        js_property_set(iterator, make_string_item("__done__"), js_bool_item(true));
        return js_promise_reject(js_property_get(stream, make_string_item("__iter_error__")));
    }
    if (js_item_is_true(js_property_get(stream, key_destroyed)) &&
        !js_item_is_true(js_property_get(stream, key_end_pending)) &&
        !js_item_is_true(js_property_get(stream, key_end_emitted)) &&
        !js_item_is_true(js_property_get(stream, key_ended))) {
        js_property_set(iterator, make_string_item("__done__"), js_bool_item(true));
        return js_promise_reject(js_stream_make_error_with_code("ERR_STREAM_PREMATURE_CLOSE",
            "Premature close"));
    }

    if (js_stream_async_iterator_is_legacy_stream(stream)) {
        js_stream_async_iterator_setup_legacy(iterator, stream);
        Item event_error = js_property_get(iterator, make_string_item("__event_error__"));
        if (js_stream_has_callback_error(event_error)) {
            js_property_set(iterator, make_string_item("__done__"), js_bool_item(true));
            js_stream_async_iterator_cleanup_stream(stream);
            return js_promise_reject(event_error);
        }
        Item buffer = js_property_get(iterator, make_string_item("__event_buffer__"));
        if (get_type_id(buffer) == LMD_TYPE_ARRAY && js_array_length(buffer) > 0) {
            Item chunk = js_stream_array_shift_property(iterator, make_string_item("__event_buffer__"));
            return js_promise_resolve(js_stream_iterator_result(chunk, false));
        }
        if (js_item_is_true(js_property_get(iterator, make_string_item("__event_done__")))) {
            js_property_set(iterator, make_string_item("__done__"), js_bool_item(true));
            js_stream_async_iterator_cleanup_stream(stream);
            return js_promise_resolve(js_stream_iterator_result(make_js_undefined(), true));
        }
        return js_stream_async_iterator_pending_promise(iterator, stream);
    }

    Item chunk = js_readable_read(stream);
    if (js_stream_async_iterator_has_value(chunk)) {
        return js_promise_resolve(js_stream_iterator_result(chunk, false));
    }
    stored_error = js_property_get(stream, make_string_item("__error__"));
    if (js_stream_has_callback_error(stored_error)) {
        js_property_set(iterator, make_string_item("__done__"), js_bool_item(true));
        return js_promise_reject(stored_error);
    }

    bool readable_done = js_stream_async_iterator_stream_done(stream);
    if (!readable_done) {
        js_stream_call_read_if_needed(stream, make_js_undefined());
        stored_error = js_property_get(stream, make_string_item("__error__"));
        if (js_stream_has_callback_error(stored_error)) {
            js_property_set(iterator, make_string_item("__done__"), js_bool_item(true));
            return js_promise_reject(stored_error);
        }
        chunk = js_readable_read(stream);
        if (js_stream_async_iterator_has_value(chunk)) {
            return js_promise_resolve(js_stream_iterator_result(chunk, false));
        }
        return js_stream_async_iterator_pending_promise(iterator, stream);
    }

    js_property_set(iterator, make_string_item("__done__"), js_bool_item(true));
    if (js_item_is_true(js_property_get(stream, key_end_pending)) &&
        !js_item_is_true(js_property_get(stream, key_end_emitted))) {
        js_stream_emit_end_tick(stream);
    }
    return js_promise_resolve(js_stream_iterator_result(make_js_undefined(), true));
}

static Item js_stream_async_iterator_inst_next(void) {
    return js_stream_async_iterator_next(js_get_this());
}

static Item js_stream_iterator_identity(void) {
    return js_get_this();
}

static Item js_stream_async_iterator_inst_return(void) {
    Item iterator = js_get_this();
    js_property_set(iterator, make_string_item("__done__"), js_bool_item(true));
    js_stream_async_iterator_resolve_all_done(iterator);
    js_stream_async_iterator_clear_pending(iterator);

    Item stream = js_property_get(iterator, make_string_item("__stream__"));
    Item writer = js_property_get(stream, make_string_item("__iter_writer__"));
    if (get_type_id(writer) == LMD_TYPE_MAP || get_type_id(writer) == LMD_TYPE_ELEMENT) {
        js_stream_iter_resolve_drain(writer, js_bool_item(false));
        js_stream_iter_reject_pending_writes(writer,
            js_stream_make_error_with_code("ERR_INVALID_STATE", "WritableStream is closed"));
    }
    if (js_item_is_true(js_property_get(iterator, make_string_item("__destroy_on_return__")))) {
        js_stream_async_iterator_cleanup_stream(stream);
    }
    return js_promise_resolve(js_stream_iterator_result(make_js_undefined(), true));
}

static Item js_stream_async_iterator_inst_throw(Item err) {
    Item iterator = js_get_this();
    Item stream = js_property_get(iterator, make_string_item("__stream__"));
    js_property_set(iterator, make_string_item("__done__"), js_bool_item(true));
    js_stream_async_iterator_reject_all(iterator, err);
    if (get_type_id(stream) == LMD_TYPE_MAP || get_type_id(stream) == LMD_TYPE_ELEMENT) {
        js_property_set(stream, make_string_item("__iter_failed__"), js_bool_item(true));
        js_property_set(stream, make_string_item("__iter_error__"), err);
        js_property_set(stream, key_destroyed, js_bool_item(true));
        js_property_set(stream, make_string_item("destroyed"), js_bool_item(true));
        Item writer = js_property_get(stream, make_string_item("__iter_writer__"));
        if (get_type_id(writer) == LMD_TYPE_MAP || get_type_id(writer) == LMD_TYPE_ELEMENT) {
            js_property_set(writer, make_string_item("__error__"), err);
            js_stream_iter_reject_drain(writer, err);
            js_stream_iter_reject_pending_writes(writer, err);
            js_stream_iter_reject_end(writer, err);
        }
    }
    return js_promise_resolve(js_stream_iterator_result(make_js_undefined(), true));
}

static Item js_stream_async_iterator(Item self) {
    ensure_keys();
    Item iterator = js_new_object();
    js_property_set(iterator, make_string_item("__stream__"), self);
    js_property_set(iterator, make_string_item("stream"), self);
    js_property_set(iterator, make_string_item("__done__"), js_bool_item(false));
    js_property_set(iterator, make_string_item("__pending__"), js_bool_item(false));
    js_property_set(iterator, make_string_item("__pending_queue__"), js_array_new(0));
    js_property_set(iterator, make_string_item("__event_buffer__"), js_array_new(0));
    js_property_set(iterator, make_string_item("__event_done__"), js_bool_item(false));
    js_property_set(iterator, make_string_item("__legacy_listening__"), js_bool_item(false));
    js_property_set(iterator, make_string_item("__destroy_on_return__"), js_bool_item(true));
    js_property_set(iterator, make_string_item("__error__"),
                    js_property_get(self, make_string_item("__error__")));
    js_property_set(iterator, make_string_item("next"),
                    js_new_function((void*)js_stream_async_iterator_inst_next, 0));
    js_property_set(iterator, make_string_item("return"),
                    js_new_function((void*)js_stream_async_iterator_inst_return, 0));
    js_property_set(iterator, make_string_item("throw"),
                    js_new_function((void*)js_stream_async_iterator_inst_throw, 1));

    Item iterators = js_property_get(self, make_string_item("__async_iterators__"));
    if (get_type_id(iterators) != LMD_TYPE_ARRAY) {
        iterators = js_array_new(0);
        js_property_set(self, make_string_item("__async_iterators__"), iterators);
    }
    js_array_push(iterators, iterator);

    Item identity_fn = js_new_function((void*)js_stream_iterator_identity, 0);
    Item async_key = make_string_item("__sym_5");
    Item iter_key = make_string_item("__sym_1");
    js_property_set(iterator, async_key, identity_fn);
    js_property_set(iterator, iter_key, identity_fn);
    js_mark_non_enumerable(iterator, async_key);
    js_mark_non_enumerable(iterator, iter_key);
    return iterator;
}

static Item js_readable_iterator(Item self, Item options) {
    TypeId options_type = get_type_id(options);
    if (options.item != 0 && options_type != LMD_TYPE_UNDEFINED && options_type != LMD_TYPE_NULL &&
        options_type != LMD_TYPE_MAP && options_type != LMD_TYPE_ELEMENT) {
        char msg[160];
        if (options_type == LMD_TYPE_INT) {
            snprintf(msg, sizeof(msg),
                "The \"options\" argument must be of type object. Received type number (%lld)",
                (long long)it2i(options));
        } else {
            snprintf(msg, sizeof(msg),
                "The \"options\" argument must be of type object.");
        }
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE", msg);
    }

    Item iterator = js_stream_async_iterator(self);
    if (options_type == LMD_TYPE_MAP || options_type == LMD_TYPE_ELEMENT) {
        Item destroy_on_return = js_property_get(options, make_string_item("destroyOnReturn"));
        if (get_type_id(destroy_on_return) == LMD_TYPE_BOOL && !it2b(destroy_on_return)) {
            js_property_set(iterator, make_string_item("__destroy_on_return__"), js_bool_item(false));
        }
    }
    return iterator;
}

static Item js_stream_iter_chunk_to_text(Item chunk) {
    if (get_type_id(chunk) == LMD_TYPE_STRING) return chunk;
    if (js_is_typed_array(chunk)) {
        return js_buffer_toString(chunk, make_string_item("utf8"),
                                  make_js_undefined(), make_js_undefined());
    }
    if (get_type_id(chunk) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(chunk);
        StrBuf* sb = strbuf_new();
        for (int64_t i = 0; i < len; i++) {
            Item byte_item = js_array_get_int(chunk, i);
            if (get_type_id(byte_item) != LMD_TYPE_INT) return js_to_string(chunk);
            char ch = (char)(it2i(byte_item) & 0xff);
            strbuf_append_char(sb, ch);
        }
        return (Item){.item = s2it(heap_create_name(sb->str, sb->length))};
    }
    return js_to_string(chunk);
}

static void js_stream_iter_append_text(Item state, Item chunk) {
    Item prev = js_property_get(state, make_string_item("text"));
    Item next = js_stream_iter_chunk_to_text(chunk);
    if (get_type_id(prev) != LMD_TYPE_STRING) prev = make_string_item("");
    if (get_type_id(next) != LMD_TYPE_STRING) next = js_to_string(next);
    String* a = it2s(prev);
    String* b = it2s(next);
    StrBuf* sb = strbuf_new();
    strbuf_append_str_n(sb, a->chars, a->len);
    strbuf_append_str_n(sb, b->chars, b->len);
    js_property_set(state, make_string_item("text"),
                    (Item){.item = s2it(heap_create_name(sb->str, sb->length))});
}

static Item js_stream_iter_text_on_data(Item env_item, Item chunk) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    js_stream_iter_append_text(env[0], chunk);
    return make_js_undefined();
}

static Item js_stream_iter_text_on_end(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item state = env[0];
    Item resolve = env[1];
    Item value = js_property_get(state, make_string_item("text"));
    if (get_type_id(value) != LMD_TYPE_STRING) value = make_string_item("");
    if (get_type_id(resolve) == LMD_TYPE_FUNC) {
        Item args[1] = { value };
        js_call_function(resolve, make_js_undefined(), args, 1);
    }
    return make_js_undefined();
}

static Item js_stream_iter_text_on_error(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item reject = env[2];
    if (get_type_id(reject) == LMD_TYPE_FUNC) {
        Item args[1] = { err };
        js_call_function(reject, make_js_undefined(), args, 1);
    }
    return make_js_undefined();
}

static Item js_stream_iter_text(Item readable) {
    Item capability = js_promise_with_resolvers();
    if (js_check_exception()) return ItemNull;
    Item promise = js_property_get(capability, make_string_item("promise"));
    Item resolve = js_property_get(capability, make_string_item("resolve"));
    Item reject = js_property_get(capability, make_string_item("reject"));
    Item state = js_new_object();
    js_property_set(state, make_string_item("text"), make_string_item(""));

    Item* env = js_alloc_env(3);
    env[0] = state;
    env[1] = resolve;
    env[2] = reject;
    Item on_data = js_new_closure((void*)js_stream_iter_text_on_data, 1, env, 3);
    Item on_end = js_new_closure((void*)js_stream_iter_text_on_end, 0, env, 3);
    Item on_error = js_new_closure((void*)js_stream_iter_text_on_error, 1, env, 3);
    js_stream_on(readable, make_string_item("data"), on_data);
    js_stream_on(readable, make_string_item("end"), on_end);
    js_stream_on(readable, make_string_item("error"), on_error);
    return promise;
}

static int64_t js_stream_iter_hwm(Item options) {
    Item hwm = js_property_get(options, make_string_item("highWaterMark"));
    if (get_type_id(hwm) == LMD_TYPE_INT && it2i(hwm) >= 0) return it2i(hwm);
    return 16;
}

static bool js_stream_iter_closed(Item writer) {
    if (js_item_is_true(js_property_get(writer, make_string_item("__closed__")))) return true;
    Item readable = js_property_get(writer, make_string_item("__readable__"));
    return js_item_is_true(js_property_get(readable, key_destroyed));
}

static int64_t js_stream_iter_desired_size_value(Item writer) {
    if (js_stream_iter_closed(writer)) return INT64_MIN;
    Item hwm = js_property_get(writer, make_string_item("__hwm__"));
    int64_t hwm_int = get_type_id(hwm) == LMD_TYPE_INT ? it2i(hwm) : 16;
    Item readable = js_property_get(writer, make_string_item("__readable__"));
    Item buf = js_property_get(readable, key_buffer);
    int64_t len = get_type_id(buf) == LMD_TYPE_ARRAY ? js_array_length(buf) : 0;
    return hwm_int - len;
}

static Item js_stream_iter_writer_desired_size(void) {
    Item writer = js_get_this();
    int64_t desired = js_stream_iter_desired_size_value(writer);
    if (desired == INT64_MIN) return ItemNull;
    return (Item){.item = i2it(desired)};
}

static bool js_stream_iter_readable_buffer_empty(Item readable) {
    Item buf = js_property_get(readable, key_buffer);
    return get_type_id(buf) != LMD_TYPE_ARRAY || js_array_length(buf) == 0;
}

static Item js_stream_iter_writer_total(Item writer) {
    Item total = js_property_get(writer, make_string_item("__total__"));
    if (get_type_id(total) == LMD_TYPE_INT) return total;
    return (Item){.item = i2it(0)};
}

static int64_t js_stream_iter_chunk_byte_length(Item chunk) {
    if (get_type_id(chunk) == LMD_TYPE_STRING) {
        String* str = it2s(chunk);
        return str ? (int64_t)str->len : 0;
    }
    Item byte_length = js_property_get(chunk, make_string_item("byteLength"));
    if (get_type_id(byte_length) == LMD_TYPE_INT) return it2i(byte_length);
    Item length = js_property_get(chunk, make_string_item("length"));
    if (get_type_id(length) == LMD_TYPE_INT) return it2i(length);
    return 0;
}

static void js_stream_iter_resolve_drain(Item writer, Item value) {
    Item capability = js_property_get(writer, make_string_item("__drain__"));
    if (get_type_id(capability) != LMD_TYPE_MAP && get_type_id(capability) != LMD_TYPE_ELEMENT) return;
    js_property_set(writer, make_string_item("__drain__"), make_js_undefined());
    Item resolve = js_property_get(capability, make_string_item("resolve"));
    if (get_type_id(resolve) == LMD_TYPE_FUNC) {
        Item args[1] = { value };
        js_call_function(resolve, make_js_undefined(), args, 1);
    }
}

static void js_stream_iter_reject_drain(Item writer, Item err) {
    Item capability = js_property_get(writer, make_string_item("__drain__"));
    if (get_type_id(capability) != LMD_TYPE_MAP && get_type_id(capability) != LMD_TYPE_ELEMENT) return;
    js_property_set(writer, make_string_item("__drain__"), make_js_undefined());
    Item reject = js_property_get(capability, make_string_item("reject"));
    if (get_type_id(reject) == LMD_TYPE_FUNC) {
        Item args[1] = { err };
        js_call_function(reject, make_js_undefined(), args, 1);
    }
}

static void js_stream_iter_resolve_end_if_drained(Item writer) {
    Item capability = js_property_get(writer, make_string_item("__end__"));
    if (get_type_id(capability) != LMD_TYPE_MAP && get_type_id(capability) != LMD_TYPE_ELEMENT) return;
    Item readable = js_property_get(writer, make_string_item("__readable__"));
    if (!js_stream_iter_readable_buffer_empty(readable)) return;
    js_property_set(writer, make_string_item("__end__"), make_js_undefined());
    Item resolve = js_property_get(capability, make_string_item("resolve"));
    if (get_type_id(resolve) == LMD_TYPE_FUNC) {
        Item args[1] = { js_stream_iter_writer_total(writer) };
        js_call_function(resolve, make_js_undefined(), args, 1);
    }
}

static void js_stream_iter_reject_end(Item writer, Item err) {
    Item capability = js_property_get(writer, make_string_item("__end__"));
    if (get_type_id(capability) != LMD_TYPE_MAP && get_type_id(capability) != LMD_TYPE_ELEMENT) return;
    js_property_set(writer, make_string_item("__end__"), make_js_undefined());
    Item reject = js_property_get(capability, make_string_item("reject"));
    if (get_type_id(reject) == LMD_TYPE_FUNC) {
        Item args[1] = { err };
        js_call_function(reject, make_js_undefined(), args, 1);
    }
}

static void js_stream_iter_reject_pending_writes(Item writer, Item err) {
    Item pending = js_property_get(writer, make_string_item("__pending_writes__"));
    if (get_type_id(pending) != LMD_TYPE_ARRAY) return;
    int64_t len = js_array_length(pending);
    for (int64_t i = 0; i < len; i++) {
        Item capability = js_array_get_int(pending, i);
        Item reject = js_property_get(capability, make_string_item("reject"));
        if (get_type_id(reject) == LMD_TYPE_FUNC) {
            Item args[1] = { err };
            js_call_function(reject, make_js_undefined(), args, 1);
        }
    }
    js_property_set(writer, make_string_item("__pending_writes__"), js_array_new(0));
}

static void js_stream_iter_maybe_drain(Item readable) {
    Item writer = js_property_get(readable, make_string_item("__iter_writer__"));
    if (get_type_id(writer) != LMD_TYPE_MAP && get_type_id(writer) != LMD_TYPE_ELEMENT) return;
    int64_t desired = js_stream_iter_desired_size_value(writer);
    if (desired > 0) js_stream_iter_resolve_drain(writer, js_bool_item(true));
    js_stream_iter_resolve_end_if_drained(writer);
}

static Item js_stream_iter_make_abort_error(void) {
    Item err = js_stream_make_error_with_code("ABORT_ERR", "The operation was aborted");
    js_property_set(err, make_string_item("name"), make_string_item("AbortError"));
    return err;
}

static Item js_stream_iter_writer_emit(Item writer, Item chunk) {
    if (js_stream_iter_closed(writer)) return js_bool_item(false);
    Item readable = js_property_get(writer, make_string_item("__readable__"));
    Item transform = js_property_get(writer, make_string_item("__transform__"));
    if (get_type_id(transform) == LMD_TYPE_FUNC) {
        Item input = js_array_new(0);
        Item transformed_chunk = chunk;
        if (get_type_id(chunk) == LMD_TYPE_STRING) {
            transformed_chunk = js_buffer_from(chunk, make_string_item("utf8"), make_js_undefined());
        }
        js_array_push(input, transformed_chunk);
        Item result = js_call_function(transform, make_js_undefined(), &input, 1);
        if (js_check_exception()) {
            Item err = js_clear_exception();
            js_stream_destroy(readable, err);
            return js_bool_item(false);
        }
        if (get_type_id(result) == LMD_TYPE_ARRAY) {
            int64_t len = js_array_length(result);
            for (int64_t i = 0; i < len; i++) {
                js_readable_push(readable, js_array_get_int(result, i));
            }
        } else if (result.item == 0 || get_type_id(result) == LMD_TYPE_NULL) {
            js_readable_push(readable, ItemNull);
            js_property_set(writer, make_string_item("__closed__"), js_bool_item(true));
        } else {
            js_readable_push(readable, result);
        }
    } else {
        js_readable_push(readable, chunk);
    }
    Item total = js_property_get(writer, make_string_item("__total__"));
    int64_t total_int = get_type_id(total) == LMD_TYPE_INT ? it2i(total) : 0;
    total_int += js_stream_iter_chunk_byte_length(chunk);
    js_property_set(writer, make_string_item("__total__"), (Item){.item = i2it(total_int)});
    return js_bool_item(!js_stream_iter_closed(writer));
}

static bool js_stream_iter_signal_aborted(Item options, Item* reason_out) {
    if (get_type_id(options) != LMD_TYPE_MAP && get_type_id(options) != LMD_TYPE_ELEMENT) return false;
    Item signal = js_property_get(options, make_string_item("signal"));
    if (get_type_id(signal) != LMD_TYPE_MAP && get_type_id(signal) != LMD_TYPE_ELEMENT) return false;
    Item aborted = js_property_get(signal, make_string_item("aborted"));
    if (get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted)) {
        Item reason = js_property_get(signal, make_string_item("reason"));
        if (reason.item == 0 || get_type_id(reason) == LMD_TYPE_UNDEFINED) reason = js_stream_iter_make_abort_error();
        *reason_out = reason;
        return true;
    }
    return false;
}

static Item js_stream_iter_pending_write_abort(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item signal = env[0];
    Item capability = env[1];
    Item reason = js_property_get(signal, make_string_item("reason"));
    if (reason.item == 0 || get_type_id(reason) == LMD_TYPE_UNDEFINED) reason = js_stream_iter_make_abort_error();
    Item reject = js_property_get(capability, make_string_item("reject"));
    if (get_type_id(reject) == LMD_TYPE_FUNC) {
        Item args[1] = { reason };
        js_call_function(reject, make_js_undefined(), args, 1);
    }
    return make_js_undefined();
}

static void js_stream_iter_attach_pending_abort(Item options, Item capability) {
    if (get_type_id(options) != LMD_TYPE_MAP && get_type_id(options) != LMD_TYPE_ELEMENT) return;
    Item signal = js_property_get(options, make_string_item("signal"));
    if (get_type_id(signal) != LMD_TYPE_MAP && get_type_id(signal) != LMD_TYPE_ELEMENT) return;
    Item add_event = js_property_get(signal, make_string_item("addEventListener"));
    if (get_type_id(add_event) != LMD_TYPE_FUNC) return;
    Item* env = js_alloc_env(2);
    env[0] = signal;
    env[1] = capability;
    Item listener = js_new_closure((void*)js_stream_iter_pending_write_abort, 0, env, 2);
    Item args[2] = { make_string_item("abort"), listener };
    js_call_function(add_event, signal, args, 2);
}

static Item js_stream_iter_writer_write(Item chunk, Item options) {
    Item writer = js_get_this();
    Item err = make_js_undefined();
    if (js_stream_iter_signal_aborted(options, &err)) return js_promise_reject(err);
    if (js_stream_iter_closed(writer)) {
        Item stored = js_property_get(writer, make_string_item("__error__"));
        if (stored.item != 0 && get_type_id(stored) != LMD_TYPE_UNDEFINED) return js_promise_reject(stored);
        return js_promise_reject(js_stream_make_error_with_code("ERR_INVALID_STATE",
            "WritableStream is closed"));
    }
    if (js_stream_iter_desired_size_value(writer) <= 0) {
        Item capability = js_promise_with_resolvers();
        Item pending = js_property_get(writer, make_string_item("__pending_writes__"));
        if (get_type_id(pending) != LMD_TYPE_ARRAY) {
            pending = js_array_new(0);
            js_property_set(writer, make_string_item("__pending_writes__"), pending);
        }
        js_array_push(pending, capability);
        js_stream_iter_attach_pending_abort(options, capability);
        return js_property_get(capability, make_string_item("promise"));
    }
    js_stream_iter_writer_emit(writer, chunk);
    return js_promise_resolve(js_bool_item(true));
}

static Item js_stream_iter_writer_writeSync(Item chunk) {
    return js_stream_iter_writer_emit(js_get_this(), chunk);
}

static Item js_stream_iter_writer_writev(Item chunks) {
    if (get_type_id(chunks) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(chunks);
        for (int64_t i = 0; i < len; i++) {
            js_stream_iter_writer_emit(js_get_this(), js_array_get_int(chunks, i));
        }
    }
    return js_promise_resolve(js_bool_item(true));
}

static Item js_stream_iter_writer_writevSync(Item chunks) {
    if (get_type_id(chunks) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(chunks);
        for (int64_t i = 0; i < len; i++) {
            js_stream_iter_writer_emit(js_get_this(), js_array_get_int(chunks, i));
        }
    }
    return js_bool_item(!js_stream_iter_closed(js_get_this()));
}

static Item js_stream_iter_writer_end(void) {
    Item writer = js_get_this();
    Item stored = js_property_get(writer, make_string_item("__error__"));
    if (stored.item != 0 && get_type_id(stored) != LMD_TYPE_UNDEFINED) return js_promise_reject(stored);
    if (!js_stream_iter_closed(writer)) {
        Item readable = js_property_get(writer, make_string_item("__readable__"));
        js_readable_push(readable, ItemNull);
        js_property_set(writer, make_string_item("__closed__"), js_bool_item(true));
    }
    js_stream_iter_reject_pending_writes(writer,
        js_stream_make_error_with_code("ERR_INVALID_STATE", "WritableStream is closed"));
    Item existing = js_property_get(writer, make_string_item("__end__"));
    if (get_type_id(existing) == LMD_TYPE_MAP || get_type_id(existing) == LMD_TYPE_ELEMENT) {
        return js_property_get(existing, make_string_item("promise"));
    }
    Item readable = js_property_get(writer, make_string_item("__readable__"));
    if (js_stream_iter_readable_buffer_empty(readable)) {
        return js_promise_resolve(js_stream_iter_writer_total(writer));
    }
    Item capability = js_promise_with_resolvers();
    js_property_set(writer, make_string_item("__end__"), capability);
    return js_property_get(capability, make_string_item("promise"));
}

static Item js_stream_iter_writer_endSync(void) {
    Item writer = js_get_this();
    if (!js_stream_iter_closed(writer)) {
        Item readable = js_property_get(writer, make_string_item("__readable__"));
        js_readable_push(readable, ItemNull);
        js_property_set(writer, make_string_item("__closed__"), js_bool_item(true));
    }
    js_stream_iter_reject_pending_writes(writer,
        js_stream_make_error_with_code("ERR_INVALID_STATE", "WritableStream is closed"));
    Item readable = js_property_get(writer, make_string_item("__readable__"));
    if (js_stream_iter_readable_buffer_empty(readable)) {
        return js_stream_iter_writer_total(writer);
    }
    return (Item){.item = i2it(-1)};
}

static Item js_stream_iter_writer_fail(Item err) {
    Item writer = js_get_this();
    Item readable = js_property_get(writer, make_string_item("__readable__"));
    js_property_set(writer, make_string_item("__closed__"), js_bool_item(true));
    js_property_set(writer, make_string_item("__error__"), err);
    js_property_set(readable, make_string_item("__iter_failed__"), js_bool_item(true));
    js_property_set(readable, make_string_item("__iter_error__"), err);
    Item iterators = js_property_get(readable, make_string_item("__async_iterators__"));
    if (get_type_id(iterators) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(iterators);
        for (int64_t i = 0; i < len; i++) {
            js_stream_async_iterator_reject_all(js_array_get_int(iterators, i), err);
        }
    }
    if (js_stream_has_callback_error(err)) js_stream_destroy(readable, err);
    js_stream_iter_reject_pending_writes(writer, err);
    js_stream_iter_reject_end(writer, err);
    return make_js_undefined();
}

static Item js_stream_iter_writer_async_dispose(void) {
    js_stream_iter_writer_fail(make_js_undefined());
    return js_promise_resolve(make_js_undefined());
}

static Item js_stream_iter_writer_sync_dispose(void) {
    js_stream_iter_writer_fail(make_js_undefined());
    return make_js_undefined();
}

static Item js_stream_iter_ondrain(Item writer) {
    if (get_type_id(writer) != LMD_TYPE_MAP && get_type_id(writer) != LMD_TYPE_ELEMENT) return ItemNull;
    Item protocol_key = js_symbol_for(make_string_item("Stream.drainableProtocol"));
    Item protocol = js_property_get(writer, protocol_key);
    if (get_type_id(protocol) == LMD_TYPE_FUNC) {
        Item result = js_call_function(protocol, writer, NULL, 0);
        if (js_check_exception()) return ItemNull;
        return result;
    }
    Item readable = js_property_get(writer, make_string_item("__readable__"));
    if (get_type_id(readable) != LMD_TYPE_MAP && get_type_id(readable) != LMD_TYPE_ELEMENT) return ItemNull;
    int64_t desired = js_stream_iter_desired_size_value(writer);
    if (desired == INT64_MIN) return ItemNull;
    if (desired > 0) return js_promise_resolve(js_bool_item(true));
    Item existing = js_property_get(writer, make_string_item("__drain__"));
    if (get_type_id(existing) == LMD_TYPE_MAP || get_type_id(existing) == LMD_TYPE_ELEMENT) {
        return js_property_get(existing, make_string_item("promise"));
    }
    Item capability = js_promise_with_resolvers();
    js_property_set(writer, make_string_item("__drain__"), capability);
    return js_property_get(capability, make_string_item("promise"));
}

static Item js_stream_iter_abort(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item signal = env[0];
    Item readable = env[1];
    Item reason = js_property_get(signal, make_string_item("reason"));
    if (reason.item == 0 || get_type_id(reason) == LMD_TYPE_UNDEFINED) {
        reason = js_stream_make_error_with_code("AbortError", "The operation was aborted");
        js_property_set(reason, make_string_item("name"), make_string_item("AbortError"));
    }
    js_stream_destroy(readable, reason);
    return make_js_undefined();
}

static void js_stream_iter_attach_abort(Item options, Item readable) {
    Item signal = js_property_get(options, make_string_item("signal"));
    if (get_type_id(signal) != LMD_TYPE_MAP && get_type_id(signal) != LMD_TYPE_ELEMENT) return;
    Item aborted = js_property_get(signal, make_string_item("aborted"));
    if (get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted)) {
        Item reason = js_property_get(signal, make_string_item("reason"));
        if (reason.item == 0 || get_type_id(reason) == LMD_TYPE_UNDEFINED) {
            reason = js_stream_make_error_with_code("AbortError", "The operation was aborted");
            js_property_set(reason, make_string_item("name"), make_string_item("AbortError"));
        }
        js_stream_destroy(readable, reason);
        return;
    }
    Item add_event = js_property_get(signal, make_string_item("addEventListener"));
    if (get_type_id(add_event) != LMD_TYPE_FUNC) return;
    Item* env = js_alloc_env(2);
    env[0] = signal;
    env[1] = readable;
    Item listener = js_new_closure((void*)js_stream_iter_abort, 0, env, 2);
    Item args[2] = { make_string_item("abort"), listener };
    js_call_function(add_event, signal, args, 2);
}

static Item js_stream_iter_push(Item options_or_transform) {
    ensure_keys();
    TypeId opt_type = get_type_id(options_or_transform);
    Item options = make_js_undefined();
    Item transform = make_js_undefined();
    if (opt_type == LMD_TYPE_FUNC) {
        transform = options_or_transform;
    } else if (opt_type == LMD_TYPE_MAP || opt_type == LMD_TYPE_ELEMENT) {
        options = options_or_transform;
        Item backpressure = js_property_get(options, make_string_item("backpressure"));
        if (get_type_id(backpressure) == LMD_TYPE_STRING) {
            String* bp = it2s(backpressure);
            bool valid = (bp->len == 6 && memcmp(bp->chars, "strict", 6) == 0) ||
                         (bp->len == 5 && memcmp(bp->chars, "block", 5) == 0) ||
                         (bp->len == 11 && memcmp(bp->chars, "drop-oldest", 11) == 0) ||
                         (bp->len == 11 && memcmp(bp->chars, "drop-newest", 11) == 0);
            if (!valid) {
                return js_throw_error_with_code("ERR_INVALID_ARG_VALUE",
                    "The property 'options.backpressure' is invalid.");
            }
        }
    }

    Item readable_opts = js_new_object();
    js_property_set(readable_opts, make_string_item("objectMode"), js_bool_item(true));
    Item readable = js_readable_new(readable_opts);
    if (opt_type == LMD_TYPE_MAP || opt_type == LMD_TYPE_ELEMENT) {
        js_stream_iter_attach_abort(options, readable);
    }

    Item writer = js_new_object();
    js_property_set(writer, make_string_item("__readable__"), readable);
    js_property_set(writer, make_string_item("__transform__"), transform);
    js_property_set(writer, make_string_item("__closed__"), js_bool_item(false));
    js_property_set(writer, make_string_item("__total__"), (Item){.item = i2it(0)});
    js_property_set(writer, make_string_item("__hwm__"), (Item){.item = i2it(js_stream_iter_hwm(options))});
    js_property_set(readable, make_string_item("__iter_writer__"), writer);
    js_property_set(writer, make_string_item("write"), js_new_function((void*)js_stream_iter_writer_write, 2));
    js_property_set(writer, make_string_item("writeSync"), js_new_function((void*)js_stream_iter_writer_writeSync, 1));
    js_property_set(writer, make_string_item("writev"), js_new_function((void*)js_stream_iter_writer_writev, 1));
    js_property_set(writer, make_string_item("writevSync"), js_new_function((void*)js_stream_iter_writer_writevSync, 1));
    js_property_set(writer, make_string_item("end"), js_new_function((void*)js_stream_iter_writer_end, 0));
    js_property_set(writer, make_string_item("endSync"), js_new_function((void*)js_stream_iter_writer_endSync, 0));
    js_property_set(writer, make_string_item("fail"), js_new_function((void*)js_stream_iter_writer_fail, 1));
    js_property_set(writer, make_string_item("__sym_14"),
                    js_new_function((void*)js_stream_iter_writer_async_dispose, 0));
    js_property_set(writer, make_string_item("__sym_15"),
                    js_new_function((void*)js_stream_iter_writer_sync_dispose, 0));
    js_install_native_accessor(writer, make_string_item("desiredSize"),
                               js_new_function((void*)js_stream_iter_writer_desired_size, 0),
                               ItemNull, 0);

    Item pair = js_new_object();
    js_property_set(pair, make_string_item("writer"), writer);
    js_property_set(pair, make_string_item("readable"), readable);
    return pair;
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
                Item result = js_call_function(write_fn, dest, &chunk, 1);
                if (js_check_exception()) {
                    Item err = js_clear_exception();
                    js_stream_schedule_error(dest, err);
                    break;
                }
                if (get_type_id(result) == LMD_TYPE_BOOL && !it2b(result)) {
                    Item dest_error = js_property_get(dest, make_string_item("__error__"));
                    if (js_stream_has_callback_error(dest_error)) break;
                    js_property_set(self, key_flowing, js_bool_item(false));
                    js_property_set(self, key_paused, js_bool_item(true));
                    js_property_set(self, make_string_item("__piped__"), js_bool_item(false));
                    js_property_set(self, make_string_item("__pipe_dest__"), make_js_undefined());
                    js_stream_call_read_if_needed(self, make_js_undefined());
                    break;
                }
            }
        }
        js_stream_set_readable_buffer(self, js_array_new(0));
    }

    if (js_item_is_true(js_property_get(self, key_end_pending)) ||
        js_item_is_true(js_property_get(self, key_ended)) ||
        js_state_get_bool(js_property_get(self, key_readable_state), "ended")) {
        Item end_fn = js_property_get(dest, key_end);
        if (get_type_id(end_fn) == LMD_TYPE_FUNC) {
            js_call_function(end_fn, dest, NULL, 0);
        }
    }

    // register data handler to forward
    // store that pipe is active via marker
    js_property_set(self, make_string_item("__piped__"), (Item){.item = b2it(true)});
    js_stream_schedule_read(self);

    return dest;
}

// destroy([err]) — destroy stream
extern "C" Item js_stream_destroy(Item self, Item err) {
    ensure_keys();
    if (js_item_is_true(js_property_get(self, key_destroyed))) return self;
    if (js_item_is_true(js_property_get(self, key_flowing))) {
        js_stream_flush_buffered_data(self);
    }
    bool readable_aborted = js_item_is_true(js_property_get(self, key_readable)) &&
                            !js_item_is_true(js_property_get(self, key_end_emitted));
    js_stream_mark_destroyed(self);
    js_property_set(self, make_string_item("readableAborted"), js_bool_item(readable_aborted));

    if (err.item != 0 && get_type_id(err) != LMD_TYPE_UNDEFINED &&
        get_type_id(err) != LMD_TYPE_NULL) {
        js_stream_set_error_state(self, err);
        js_property_set(self, make_string_item("__error__"), err);
        js_stream_async_iterators_drain(self, err);
    } else {
        Item iterators = js_property_get(self, make_string_item("__async_iterators__"));
        if (get_type_id(iterators) == LMD_TYPE_ARRAY && js_array_length(iterators) > 0) {
            Item close_err = js_stream_make_error_with_code("ERR_STREAM_PREMATURE_CLOSE",
                "Premature close");
            js_stream_async_iterators_drain(self, close_err);
        }
    }

    Item destroy_fn = js_property_get(self, make_string_item("_destroy"));
    if (get_type_id(destroy_fn) == LMD_TYPE_FUNC) {
        Item destroy_cb = js_bind_function(js_new_function((void*)js_stream_after_destroy, 2),
                                           make_js_undefined(), &self, 1);
        Item destroy_err = js_stream_has_callback_error(err) ? err : ItemNull;
        Item args[2] = { destroy_err, destroy_cb };
        js_property_set(self, make_string_item("__destroying_sync__"), js_bool_item(true));
        js_property_set(self, make_string_item("__destroy_cb_done__"), js_bool_item(false));
        js_property_set(self, make_string_item("__destroy_cb_error__"), make_js_undefined());
        js_call_function(destroy_fn, self, args, 2);
        js_property_set(self, make_string_item("__destroying_sync__"), js_bool_item(false));
        if (!js_check_exception()) {
            if (js_item_is_true(js_property_get(self, make_string_item("__destroy_cb_done__")))) {
                Item cb_err = js_property_get(self, make_string_item("__destroy_cb_error__"));
                if (js_stream_has_callback_error(cb_err)) js_stream_schedule_error_immediate(self, cb_err);
                js_stream_schedule_close_immediate(self);
            }
            return self;
        }
    }

    if (err.item != 0 && get_type_id(err) != LMD_TYPE_UNDEFINED &&
        get_type_id(err) != LMD_TYPE_NULL) {
        js_stream_schedule_error_immediate(self, err);
    }
    js_stream_schedule_close_immediate(self);
    return self;
}

// resume() — start flowing mode, call _read if set
extern "C" Item js_readable_resume(Item self) {
    ensure_keys();
    js_property_set(self, key_flowing, js_bool_item(true));
    js_property_set(self, key_paused, js_bool_item(false));
    // flush buffered data
    Item buf = js_property_get(self, key_buffer);
    if (get_type_id(buf) == LMD_TYPE_ARRAY) {
        int64_t blen = js_array_length(buf);
        for (int64_t i = 0; i < blen; i++) {
            Item chunk = js_array_get_int(buf, i);
            stream_emit(self, "data", &chunk, 1);
        }
        js_stream_set_readable_buffer(self, js_array_new(0));
    }
    js_stream_call_read_if_needed(self, make_js_undefined());
    return self;
}

// pause() — stop flowing mode
extern "C" Item js_readable_pause(Item self) {
    ensure_keys();
    js_property_set(self, key_flowing, js_bool_item(false));
    js_property_set(self, key_paused, js_bool_item(true));
    return self;
}

extern "C" Item js_readable_isPaused(Item self) {
    ensure_keys();
    return js_bool_item(js_item_is_true(js_property_get(self, key_paused)));
}

// setEncoding(encoding) — stub for compatibility
extern "C" Item js_stream_setEncoding(Item self, Item encoding) {
    ensure_keys();
    // store encoding for later use, but our implementation doesn't use it
    Item next_encoding = encoding;
    if (encoding.item == 0 || get_type_id(encoding) == LMD_TYPE_UNDEFINED ||
        get_type_id(encoding) == LMD_TYPE_NULL) {
        next_encoding = make_string_item("utf8");
    }
    if (get_type_id(next_encoding) == LMD_TYPE_STRING) {
        js_property_set(self, make_string_item("_encoding"), next_encoding);
        Item state = js_property_get(self, key_readable_state);
        if (get_type_id(state) == LMD_TYPE_MAP) {
            js_property_set(state, make_string_item("encoding"), next_encoding);
        }
    }
    return self;
}

static char js_stream_ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static Item js_stream_canonical_encoding(Item encoding) {
    if (get_type_id(encoding) != LMD_TYPE_STRING) return encoding;
    String* enc = it2s(encoding);
    if (!enc) return encoding;
    char lower[64];
    int len = enc->len < (int)sizeof(lower) ? (int)enc->len : (int)sizeof(lower);
    for (int i = 0; i < len; i++)
        lower[i] = js_stream_ascii_lower(enc->chars[i]);
    return make_string_item(lower, len);
}

static bool js_stream_encoding_equals(String* enc, const char* literal) {
    if (!enc || !literal) return false;
    int lit_len = (int)strlen(literal);
    if ((int)enc->len != lit_len) return false;
    for (int i = 0; i < lit_len; i++) {
        if (js_stream_ascii_lower(enc->chars[i]) != literal[i]) return false;
    }
    return true;
}

static bool js_stream_is_valid_encoding(Item encoding) {
    if (get_type_id(encoding) != LMD_TYPE_STRING) return true;
    String* enc = it2s(encoding);
    return js_stream_encoding_equals(enc, "utf8") ||
           js_stream_encoding_equals(enc, "utf-8") ||
           js_stream_encoding_equals(enc, "hex") ||
           js_stream_encoding_equals(enc, "base64") ||
           js_stream_encoding_equals(enc, "latin1") ||
           js_stream_encoding_equals(enc, "binary") ||
           js_stream_encoding_equals(enc, "ascii") ||
           js_stream_encoding_equals(enc, "ucs2") ||
           js_stream_encoding_equals(enc, "ucs-2") ||
           js_stream_encoding_equals(enc, "utf16le") ||
           js_stream_encoding_equals(enc, "utf-16le");
}

static Item js_stream_unknown_encoding_label(Item encoding) {
    if (get_type_id(encoding) == LMD_TYPE_STRING) return encoding;
    TypeId tid = get_type_id(encoding);
    if (tid == LMD_TYPE_MAP || tid == LMD_TYPE_ARRAY || tid == LMD_TYPE_FUNC) {
        Item inspected = js_util_inspect(encoding, make_js_undefined());
        if (!js_check_exception() && get_type_id(inspected) == LMD_TYPE_STRING)
            return inspected;
    }
    Item coerced = js_to_string(encoding);
    if (!js_check_exception() && get_type_id(coerced) == LMD_TYPE_STRING)
        return coerced;
    return make_string_item("");
}

static Item js_stream_throw_unknown_encoding(Item encoding) {
    Item label = js_stream_unknown_encoding_label(encoding);
    String* enc = get_type_id(label) == LMD_TYPE_STRING ? it2s(label) : NULL;
    char msg[128];
    int len = enc && enc->len < 96 ? (int)enc->len : (enc ? 96 : 0);
    memcpy(msg, "Unknown encoding: ", 18);
    if (enc && len > 0) memcpy(msg + 18, enc->chars, (size_t)len);
    msg[18 + len] = '\0';
    return js_throw_type_error_code("ERR_UNKNOWN_ENCODING", msg);
}

static Item js_stream_resolve_write_encoding(Item self, Item encoding) {
    if (get_type_id(encoding) == LMD_TYPE_STRING) return encoding;
    Item default_encoding = js_property_get(self, make_string_item("_defaultEncoding"));
    if (get_type_id(default_encoding) == LMD_TYPE_STRING) return default_encoding;
    return make_string_item("utf8");
}

static bool js_stream_writable_is_object_mode(Item self) {
    Item state = js_property_get(self, key_writable_state);
    return js_state_get_bool(state, "objectMode");
}

static bool js_stream_writable_should_decode_strings(Item self) {
    Item decode_strings = js_property_get(self, make_string_item("_decodeStrings"));
    return get_type_id(decode_strings) != LMD_TYPE_BOOL || it2b(decode_strings);
}

static bool js_stream_chunk_is_buffer(Item chunk) {
    Item result = js_buffer_isBuffer(chunk);
    return get_type_id(result) == LMD_TYPE_BOOL && it2b(result);
}

static bool js_stream_chunk_is_arraybuffer_view(Item chunk) {
    return js_is_typed_array(chunk) || js_is_dataview(chunk);
}

static bool js_stream_readable_is_object_mode(Item self) {
    Item state = js_property_get(self, key_readable_state);
    return js_state_get_bool(state, "objectMode");
}

static bool js_stream_encoding_items_equal(Item a, Item b) {
    if (get_type_id(a) != LMD_TYPE_STRING || get_type_id(b) != LMD_TYPE_STRING)
        return false;
    Item ca = js_stream_canonical_encoding(a);
    Item cb = js_stream_canonical_encoding(b);
    if (get_type_id(ca) != LMD_TYPE_STRING || get_type_id(cb) != LMD_TYPE_STRING)
        return false;
    String* as = it2s(ca);
    String* bs = it2s(cb);
    return as && bs && as->len == bs->len &&
           memcmp(as->chars, bs->chars, as->len) == 0;
}

static bool js_stream_convert_view_to_buffer(Item* chunk) {
    if (!js_stream_chunk_is_arraybuffer_view(*chunk) ||
        js_stream_chunk_is_buffer(*chunk)) {
        return true;
    }
    Item buffer = js_buffer_from(*chunk, make_js_undefined(), make_js_undefined());
    if (js_check_exception()) return false;
    if (buffer.item != 0 &&
        get_type_id(buffer) != LMD_TYPE_UNDEFINED &&
        get_type_id(buffer) != LMD_TYPE_NULL) {
        *chunk = buffer;
    }
    return true;
}

static bool js_stream_prepare_readable_chunk(Item self, Item* chunk, Item encoding) {
    if (js_stream_readable_is_object_mode(self)) return true;
    if (get_type_id(*chunk) == LMD_TYPE_STRING) {
        Item chunk_encoding = encoding;
        if (chunk_encoding.item == 0 ||
            get_type_id(chunk_encoding) == LMD_TYPE_UNDEFINED ||
            get_type_id(chunk_encoding) == LMD_TYPE_NULL) {
            chunk_encoding = js_stream_resolve_write_encoding(self, chunk_encoding);
        }
        if (get_type_id(chunk_encoding) != LMD_TYPE_STRING ||
            !js_stream_is_valid_encoding(chunk_encoding)) {
            js_stream_throw_unknown_encoding(chunk_encoding);
            return false;
        }
        Item stream_encoding = js_property_get(self, make_string_item("_encoding"));
        if (js_stream_encoding_items_equal(chunk_encoding, stream_encoding)) {
            return true;
        }
        Item buffer = js_buffer_from(*chunk, chunk_encoding, make_js_undefined());
        if (js_check_exception()) return false;
        *chunk = buffer;
        return true;
    }
    return js_stream_convert_view_to_buffer(chunk);
}

static bool js_stream_prepare_writable_chunk(Item self, Item* chunk, Item* encoding) {
    if (js_stream_writable_is_object_mode(self)) return true;

    if (js_stream_chunk_is_buffer(*chunk)) {
        *encoding = make_string_item("buffer");
        return true;
    }

    if (js_stream_chunk_is_arraybuffer_view(*chunk)) {
        if (!js_stream_convert_view_to_buffer(chunk)) return false;
        *encoding = make_string_item("buffer");
        return true;
    }

    Item write_encoding = js_stream_resolve_write_encoding(self, *encoding);
    if (get_type_id(*chunk) == LMD_TYPE_STRING &&
        !js_stream_writable_is_object_mode(self)) {
        if (!js_stream_is_valid_encoding(write_encoding)) {
            js_stream_throw_unknown_encoding(write_encoding);
            return false;
        }
        if (js_stream_writable_should_decode_strings(self)) {
            Item buffer = js_buffer_from(*chunk, write_encoding, make_js_undefined());
            if (js_check_exception()) return false;
            if (buffer.item != 0 &&
                get_type_id(buffer) != LMD_TYPE_UNDEFINED &&
                get_type_id(buffer) != LMD_TYPE_NULL) {
                *chunk = buffer;
                *encoding = make_string_item("buffer");
                return true;
            }
        }
    }

    *encoding = write_encoding;
    return true;
}

static bool js_stream_validate_writable_chunk(Item self, Item chunk) {
    if (get_type_id(chunk) == LMD_TYPE_NULL) {
        js_throw_type_error_code("ERR_STREAM_NULL_VALUES",
                                 "May not write null values to stream");
        return false;
    }
    if (js_stream_writable_is_object_mode(self)) return true;
    TypeId tid = get_type_id(chunk);
    if (tid == LMD_TYPE_STRING || js_stream_chunk_is_arraybuffer_view(chunk)) return true;
    js_throw_invalid_arg_type("chunk", "string, Buffer, or Uint8Array", chunk);
    return false;
}

extern "C" Item js_stream_setDefaultEncoding(Item self, Item encoding) {
    ensure_keys();
    Item next_encoding = encoding;
    if (next_encoding.item == 0 ||
        get_type_id(next_encoding) == LMD_TYPE_UNDEFINED ||
        get_type_id(next_encoding) == LMD_TYPE_NULL) {
        next_encoding = make_string_item("utf8");
    }
    if (get_type_id(next_encoding) != LMD_TYPE_STRING ||
        !js_stream_is_valid_encoding(next_encoding)) {
        return js_stream_throw_unknown_encoding(next_encoding);
    }
    js_property_set(self, make_string_item("_defaultEncoding"),
                    js_stream_canonical_encoding(next_encoding));
    return self;
}

static bool js_stream_hwm_object_mode_arg(Item object_mode) {
    return get_type_id(object_mode) == LMD_TYPE_BOOL && it2b(object_mode);
}

extern "C" Item js_stream_getDefaultHighWaterMark(Item object_mode) {
    return (Item){.item = i2it(js_stream_hwm_object_mode_arg(object_mode)
                               ? js_stream_default_object_hwm
                               : js_stream_default_byte_hwm)};
}

extern "C" Item js_stream_setDefaultHighWaterMark(Item object_mode, Item value) {
    TypeId tid = get_type_id(value);
    int64_t next = 0;
    if (tid == LMD_TYPE_INT) {
        next = it2i(value);
    } else if (tid == LMD_TYPE_FLOAT) {
        next = (int64_t)it2d(value);
    } else {
        return js_throw_invalid_arg_type("value", "number", value);
    }
    if (next < 0) next = 0;
    if (js_stream_hwm_object_mode_arg(object_mode)) {
        js_stream_default_object_hwm = next;
    } else {
        js_stream_default_byte_hwm = next;
    }
    return make_js_undefined();
}

static bool js_stream_item_is_number(Item item) {
    TypeId tid = get_type_id(item);
    return tid == LMD_TYPE_INT || tid == LMD_TYPE_FLOAT;
}

static bool js_stream_item_is_nan_number(Item item) {
    if (get_type_id(item) != LMD_TYPE_FLOAT) return false;
    double value = it2d(item);
    return value != value;
}

static bool js_stream_validate_hwm_option(const char* name, Item value) {
    if (!js_stream_item_is_nan_number(value)) return true;
    char msg[160];
    snprintf(msg, sizeof(msg),
             "The property 'options.%s' is invalid. Received NaN", name);
    js_throw_type_error_code("ERR_INVALID_ARG_VALUE", msg);
    return false;
}

static void js_stream_define_bool(Item obj, const char* name, bool value) {
    js_create_data_property(obj, make_string_item(name), js_bool_item(value));
}

static bool js_stream_is_object_like(Item item) {
    TypeId type = get_type_id(item);
    return type == LMD_TYPE_MAP || type == LMD_TYPE_ARRAY ||
           type == LMD_TYPE_ELEMENT || type == LMD_TYPE_FUNC;
}

static bool js_stream_called_as_constructor(void) {
    Item new_target = js_get_new_target();
    TypeId type = get_type_id(new_target);
    return new_target.item != 0 && new_target.item != ItemNull.item &&
           type != LMD_TYPE_UNDEFINED && js_stream_is_object_like(new_target);
}

static Item js_stream_create_instance(Item prototype) {
    Item self = js_get_this();
    if (js_stream_called_as_constructor() && js_stream_is_object_like(self)) {
        return self;
    }

    Item obj = js_new_object();
    if (js_stream_is_object_like(prototype)) {
        js_set_prototype(obj, prototype);
    }
    return obj;
}

static bool js_stream_ordinary_has_instance(Item value) {
    Item result = js_ordinary_has_instance(value, js_get_this());
    return js_item_is_true(result);
}

static Item js_stream_readable_has_instance(Item value) {
    if (js_stream_ordinary_has_instance(value)) return js_bool_item(true);
    JsClass cls = js_class_id(value);
    return js_bool_item(cls == JS_CLASS_READABLE || cls == JS_CLASS_DUPLEX ||
                        cls == JS_CLASS_TRANSFORM || cls == JS_CLASS_PASS_THROUGH);
}

static Item js_stream_writable_has_instance(Item value) {
    if (js_stream_ordinary_has_instance(value)) return js_bool_item(true);
    JsClass cls = js_class_id(value);
    return js_bool_item(cls == JS_CLASS_WRITABLE || cls == JS_CLASS_DUPLEX ||
                        cls == JS_CLASS_TRANSFORM || cls == JS_CLASS_PASS_THROUGH);
}

static Item js_stream_duplex_has_instance(Item value) {
    if (js_stream_ordinary_has_instance(value)) return js_bool_item(true);
    JsClass cls = js_class_id(value);
    return js_bool_item(cls == JS_CLASS_DUPLEX || cls == JS_CLASS_TRANSFORM ||
                        cls == JS_CLASS_PASS_THROUGH);
}

static Item js_stream_transform_has_instance(Item value) {
    if (js_stream_ordinary_has_instance(value)) return js_bool_item(true);
    JsClass cls = js_class_id(value);
    return js_bool_item(cls == JS_CLASS_TRANSFORM || cls == JS_CLASS_PASS_THROUGH);
}

static Item js_stream_passthrough_has_instance(Item value) {
    if (js_stream_ordinary_has_instance(value)) return js_bool_item(true);
    return js_bool_item(js_class_id(value) == JS_CLASS_PASS_THROUGH);
}

// Helper: propagate stream constructor options to instance methods
static bool propagate_stream_options(Item obj, Item opts) {
    if (get_type_id(opts) != LMD_TYPE_MAP) return true;
    JsClass cls = js_class_id(obj);
    bool is_duplex_like = cls == JS_CLASS_DUPLEX || cls == JS_CLASS_TRANSFORM ||
                          cls == JS_CLASS_PASS_THROUGH;
    // read → _read
    Item read_opt = js_property_get(opts, make_string_item("read"));
    if (get_type_id(read_opt) == LMD_TYPE_FUNC)
        js_property_set(obj, make_string_item("_read"), read_opt);
    // write → _write
    Item write_opt = js_property_get(opts, make_string_item("write"));
    if (get_type_id(write_opt) == LMD_TYPE_FUNC)
        js_property_set(obj, make_string_item("_write"), write_opt);
    // writev → _writev
    Item writev_opt = js_property_get(opts, make_string_item("writev"));
    if (get_type_id(writev_opt) == LMD_TYPE_FUNC)
        js_property_set(obj, make_string_item("_writev"), writev_opt);
    // transform → _transform
    Item transform_opt = js_property_get(opts, make_string_item("transform"));
    if (get_type_id(transform_opt) == LMD_TYPE_FUNC)
        js_property_set(obj, make_string_item("_transform"), transform_opt);
    // flush → _flush
    Item flush_opt = js_property_get(opts, make_string_item("flush"));
    if (get_type_id(flush_opt) == LMD_TYPE_FUNC)
        js_property_set(obj, make_string_item("_flush"), flush_opt);
    // final → _final
    Item final_opt = js_property_get(opts, make_string_item("final"));
    if (get_type_id(final_opt) == LMD_TYPE_FUNC)
        js_property_set(obj, make_string_item("_final"), final_opt);
    // destroy → _destroy
    Item destroy_opt = js_property_get(opts, make_string_item("destroy"));
    if (get_type_id(destroy_opt) == LMD_TYPE_FUNC)
        js_property_set(obj, make_string_item("_destroy"), destroy_opt);
    Item object_mode_hwm = (Item){.item = i2it(js_stream_default_object_hwm)};
    // highWaterMark
    Item hwm = js_property_get(opts, make_string_item("highWaterMark"));
    if (!js_stream_validate_hwm_option("highWaterMark", hwm)) return false;
    bool has_hwm = js_stream_item_is_number(hwm);
    if (has_hwm) {
        js_property_set(obj, make_string_item("_highWaterMark"), hwm);
        js_stream_set_readable_high_water_mark(obj, hwm);
        js_stream_set_writable_high_water_mark(obj, hwm);
    }
    // objectMode
    Item om = js_property_get(opts, make_string_item("objectMode"));
    bool object_mode_true = get_type_id(om) == LMD_TYPE_BOOL && it2b(om);
    if (get_type_id(om) == LMD_TYPE_BOOL) {
        js_property_set(obj, make_string_item("_objectMode"), om);
        js_stream_set_readable_object_mode(obj, it2b(om));
        js_stream_set_writable_object_mode(obj, it2b(om));
        if (object_mode_true && !has_hwm) {
            js_stream_set_readable_high_water_mark(obj, object_mode_hwm);
            js_stream_set_writable_high_water_mark(obj, object_mode_hwm);
        }
    }
    // readable side highWaterMark/objectMode
    Item readable_hwm = js_property_get(opts, make_string_item("readableHighWaterMark"));
    if (!js_stream_validate_hwm_option("readableHighWaterMark", readable_hwm)) return false;
    bool has_readable_hwm = js_stream_item_is_number(readable_hwm);
    Item readable_om = js_property_get(opts, make_string_item("readableObjectMode"));
    if (is_duplex_like && get_type_id(readable_om) == LMD_TYPE_BOOL) {
        js_stream_set_readable_object_mode(obj, it2b(readable_om));
        if (it2b(readable_om) && !has_readable_hwm && !has_hwm)
            js_stream_set_readable_high_water_mark(obj, object_mode_hwm);
    }
    if (is_duplex_like && has_readable_hwm && !has_hwm)
        js_stream_set_readable_high_water_mark(obj, readable_hwm);
    // writable side highWaterMark/objectMode
    Item writable_hwm = js_property_get(opts, make_string_item("writableHighWaterMark"));
    if (!js_stream_validate_hwm_option("writableHighWaterMark", writable_hwm)) return false;
    bool has_writable_hwm = js_stream_item_is_number(writable_hwm);
    Item writable_om = js_property_get(opts, make_string_item("writableObjectMode"));
    if (is_duplex_like && get_type_id(writable_om) == LMD_TYPE_BOOL) {
        js_stream_set_writable_object_mode(obj, it2b(writable_om));
        if (it2b(writable_om) && !has_writable_hwm && !has_hwm)
            js_stream_set_writable_high_water_mark(obj, object_mode_hwm);
    }
    if (is_duplex_like && has_writable_hwm && !has_hwm)
        js_stream_set_writable_high_water_mark(obj, writable_hwm);
    // encoding
    Item enc = js_property_get(opts, make_string_item("encoding"));
    if (get_type_id(enc) == LMD_TYPE_STRING) {
        js_property_set(obj, make_string_item("_encoding"), enc);
        Item rstate = js_property_get(obj, key_readable_state);
        if (get_type_id(rstate) == LMD_TYPE_MAP)
            js_property_set(rstate, make_string_item("encoding"), enc);
    }
    // defaultEncoding
    Item default_enc = js_property_get(opts, make_string_item("defaultEncoding"));
    if (get_type_id(default_enc) == LMD_TYPE_STRING) {
        if (!js_stream_is_valid_encoding(default_enc)) {
            js_stream_throw_unknown_encoding(default_enc);
            return false;
        }
        js_property_set(obj, make_string_item("_defaultEncoding"),
                        js_stream_canonical_encoding(default_enc));
    }
    Item decode_strings = js_property_get(opts, make_string_item("decodeStrings"));
    if (get_type_id(decode_strings) == LMD_TYPE_BOOL)
        js_property_set(obj, make_string_item("_decodeStrings"), decode_strings);
    // readable/writable side switches used by Duplex options.
    Item readable = js_property_get(opts, make_string_item("readable"));
    if (get_type_id(readable) == LMD_TYPE_BOOL)
        js_stream_set_readable_open(obj, it2b(readable));
    Item writable = js_property_get(opts, make_string_item("writable"));
    if (get_type_id(writable) == LMD_TYPE_BOOL)
        js_stream_set_writable_open(obj, it2b(writable));
    Item capture_rejections = js_property_get(opts, make_string_item("captureRejections"));
    if (get_type_id(capture_rejections) == LMD_TYPE_BOOL)
        js_property_set(obj, key_capture_rejections, capture_rejections);
    Item auto_destroy = js_property_get(opts, make_string_item("autoDestroy"));
    if (get_type_id(auto_destroy) == LMD_TYPE_BOOL)
        js_property_set(obj, key_auto_destroy, auto_destroy);
    return true;
}

// =============================================================================
// Instance method wrappers (for JS method calls — uses js_get_this())
// =============================================================================

// Forward declarations for functions defined later
extern "C" Item js_writable_write(Item self, Item chunk, Item encoding, Item callback);
extern "C" Item js_writable_end(Item self, Item chunk, Item callback);
extern "C" Item js_writable_cork(Item self);
extern "C" Item js_writable_uncork(Item self);
extern "C" Item js_transform_write(Item self, Item chunk, Item encoding, Item callback);
extern "C" Item js_transform_end(Item self, Item chunk, Item callback);

static Item js_stream_inst_on(Item event_item, Item listener) {
    return js_stream_on(js_get_this(), event_item, listener);
}
static Item js_stream_inst_once(Item event_item, Item listener) {
    return js_stream_once(js_get_this(), event_item, listener);
}
static Item js_stream_inst_off(Item event_item, Item listener) {
    return js_stream_off(js_get_this(), event_item, listener);
}
static Item js_stream_inst_removeAllListeners(Item event_item) {
    return js_stream_removeAllListeners(js_get_this(), event_item);
}
static Item js_stream_inst_emit(Item event_item, Item arg1) {
    return js_stream_emit(js_get_this(), event_item, arg1);
}
static Item js_stream_inst_eventNames(void) {
    return js_stream_eventNames(js_get_this());
}
static Item js_stream_inst_listenerCount(Item event_item, Item listener) {
    return js_stream_listenerCount(js_get_this(), event_item, listener);
}
static Item js_readable_inst_push(Item chunk, Item encoding) {
    return js_readable_push_encoded(js_get_this(), chunk, encoding);
}
static Item js_readable_inst_unshift(Item chunk, Item encoding) {
    return js_readable_unshift_encoded(js_get_this(), chunk, encoding);
}
static Item js_readable_inst_read(Item size_item) {
    return js_readable_read_size(js_get_this(), size_item);
}
static Item js_readable_inst_pipe(Item dest) {
    return js_readable_pipe(js_get_this(), dest);
}
static Item js_stream_inst_destroy(Item err) {
    return js_stream_destroy(js_get_this(), err);
}
static Item js_readable_inst_resume(void) {
    return js_readable_resume(js_get_this());
}
static Item js_readable_inst_pause(void) {
    return js_readable_pause(js_get_this());
}
static Item js_readable_inst_isPaused(void) {
    return js_readable_isPaused(js_get_this());
}
static Item js_stream_inst_setEncoding(Item encoding) {
    return js_stream_setEncoding(js_get_this(), encoding);
}
static Item js_stream_inst_setDefaultEncoding(Item encoding) {
    return js_stream_setDefaultEncoding(js_get_this(), encoding);
}
static Item js_readable_inst_iterator(Item options) {
    return js_readable_iterator(js_get_this(), options);
}
static Item js_stream_inst_asyncIterator(void) {
    return js_stream_async_iterator(js_get_this());
}

static void js_stream_install_async_iterator(Item obj) {
    Item iterator_fn = js_new_function((void*)js_stream_inst_asyncIterator, 0);
    Item async_key = make_string_item("__sym_5");
    Item iter_key = make_string_item("__sym_1");
    js_property_set(obj, async_key, iterator_fn);
    js_property_set(obj, iter_key, iterator_fn);
    js_mark_non_enumerable(obj, async_key);
    js_mark_non_enumerable(obj, iter_key);
}
static Item js_writable_inst_write(Item chunk, Item encoding, Item callback) {
    return js_writable_write(js_get_this(), chunk, encoding, callback);
}
static Item js_writable_inst_end(Item chunk, Item callback) {
    return js_writable_end(js_get_this(), chunk, callback);
}
static Item js_writable_inst_cork(void) {
    return js_writable_cork(js_get_this());
}
static Item js_writable_inst_uncork(void) {
    return js_writable_uncork(js_get_this());
}
static Item js_transform_inst_write(Item chunk, Item encoding, Item callback) {
    return js_transform_write(js_get_this(), chunk, encoding, callback);
}
static Item js_transform_inst_end(Item chunk, Item callback) {
    return js_transform_end(js_get_this(), chunk, callback);
}

// Readable constructor
extern "C" Item js_readable_new(Item opts) {
    ensure_keys();
    Item obj = js_stream_create_instance(stream_readable_prototype);

    js_class_stamp(obj, JS_CLASS_READABLE);
    js_property_set(obj, key_readable, js_bool_item(true));
    js_property_set(obj, key_flowing, js_bool_item(false));
    js_property_set(obj, key_ended, js_bool_item(false));
    js_property_set(obj, key_destroyed, js_bool_item(false));
    js_property_set(obj, make_string_item("destroyed"), js_bool_item(false));
    js_property_set(obj, make_string_item("errored"), ItemNull);
    js_property_set(obj, make_string_item("readableAborted"), js_bool_item(false));
    js_property_set(obj, key_end_pending, js_bool_item(false));
    js_property_set(obj, key_end_emitted, js_bool_item(false));
    js_property_set(obj, key_reading, js_bool_item(false));
    js_property_set(obj, key_paused, js_bool_item(false));
    js_property_set(obj, key_close_emitted, js_bool_item(false));
    js_property_set(obj, key_auto_destroy, js_bool_item(true));
    js_property_set(obj, key_readable_state, js_create_readable_state());
    js_stream_define_bool(obj, "readableEnded", false);
    js_stream_set_readable_buffer(obj, js_array_new(0));
    js_property_set(obj, key_listeners, js_new_object());
    js_stream_init_readable_options(obj);

    js_property_set(obj, key_on, js_new_function((void*)js_stream_inst_on, 2));
    js_property_set(obj, make_string_item("once"), js_new_function((void*)js_stream_inst_once, 2));
    js_property_set(obj, make_string_item("off"), js_new_function((void*)js_stream_inst_off, 2));
    js_property_set(obj, make_string_item("removeListener"), js_new_function((void*)js_stream_inst_off, 2));
    js_property_set(obj, make_string_item("removeAllListeners"), js_new_function((void*)js_stream_inst_removeAllListeners, 1));
    js_property_set(obj, key_emit, js_new_function((void*)js_stream_inst_emit, 2));
    js_property_set(obj, make_string_item("eventNames"), js_new_function((void*)js_stream_inst_eventNames, 0));
    js_property_set(obj, make_string_item("listenerCount"), js_new_function((void*)js_stream_inst_listenerCount, 2));
    js_property_set(obj, key_push, js_new_function((void*)js_readable_inst_push, 2));
    js_property_set(obj, make_string_item("unshift"), js_new_function((void*)js_readable_inst_unshift, 2));
    js_property_set(obj, key_read, js_new_function((void*)js_readable_inst_read, 1));
    js_property_set(obj, key_pipe, js_new_function((void*)js_readable_inst_pipe, 1));
    js_property_set(obj, key_destroy, js_new_function((void*)js_stream_inst_destroy, 1));
    js_property_set(obj, make_string_item("resume"), js_new_function((void*)js_readable_inst_resume, 0));
    js_property_set(obj, make_string_item("pause"), js_new_function((void*)js_readable_inst_pause, 0));
    js_property_set(obj, make_string_item("isPaused"), js_new_function((void*)js_readable_inst_isPaused, 0));
    js_property_set(obj, make_string_item("setEncoding"), js_new_function((void*)js_stream_inst_setEncoding, 1));
    js_property_set(obj, make_string_item("iterator"), js_new_function((void*)js_readable_inst_iterator, 1));
    js_stream_install_async_iterator(obj);

    if (!propagate_stream_options(obj, opts)) return ItemNull;
    return obj;
}

// =============================================================================
// Writable stream
// =============================================================================

// write(chunk) — write data to writable stream
extern "C" Item js_writable_write(Item self, Item chunk, Item encoding, Item callback) {
    ensure_keys();
    if (get_type_id(encoding) == LMD_TYPE_FUNC &&
        (callback.item == 0 || get_type_id(callback) == LMD_TYPE_UNDEFINED)) {
        callback = encoding;
        encoding = make_js_undefined();
    }

    Item writable_state = js_property_get(self, key_writable_state);
    if (!js_item_is_true(js_property_get(self, key_writable)) ||
        js_state_get_bool(writable_state, "ended")) {
        Item err = js_stream_make_error_with_code("ERR_STREAM_WRITE_AFTER_END",
            "write after end");
        js_stream_schedule_callback_error(callback, err);
        js_stream_schedule_error(self, err);
        return js_bool_item(false);
    }

    bool write_in_progress = js_item_is_true(js_property_get(self, make_string_item("_writing")));
    if (!js_stream_validate_writable_chunk(self, chunk)) return ItemNull;
    if (!js_stream_prepare_writable_chunk(self, &chunk, &encoding)) return ItemNull;
    bool accepted = js_stream_begin_write(self, chunk);

    // if corked, buffer the write
    Item corked = js_property_get(self, make_string_item("_corked"));
    if ((get_type_id(corked) == LMD_TYPE_INT && it2i(corked) > 0) ||
        write_in_progress) {
        js_stream_buffer_write_request(self, chunk, encoding, callback);
        return js_bool_item(accepted);
    }

    // call _write handler if set
    Item write_handler = js_property_get(self, make_string_item("_write"));
    if (get_type_id(write_handler) != LMD_TYPE_FUNC) {
        // legacy: try __write_handler__
        write_handler = js_property_get(self, make_string_item("__write_handler__"));
    }
    if (get_type_id(write_handler) == LMD_TYPE_FUNC) {
        Item write_cb = js_stream_make_write_callback(self, callback);
        Item args[3] = {chunk, encoding, write_cb};
        js_property_set(self, make_string_item("_writing"), js_bool_item(true));
        js_call_function(write_handler, self, args, 3);
        if (js_check_exception()) {
            Item err = js_clear_exception();
            js_stream_after_write(self, callback, err);
            return js_bool_item(false);
        }
    } else {
        Item writev_fn = js_property_get(self, make_string_item("_writev"));
        if (get_type_id(writev_fn) == LMD_TYPE_FUNC) {
            js_stream_buffer_write_request(self, chunk, encoding, callback);
            js_stream_flush_pending_writes(self);
            return js_bool_item(accepted);
        }
        // no _write method — throw ERR_METHOD_NOT_IMPLEMENTED
        Item state = js_property_get(self, key_writable_state);
        js_state_set_item(state, "length", (Item){.item = i2it(0)});
        js_state_set_bool(state, "needDrain", false);
        js_throw_error_with_code("ERR_METHOD_NOT_IMPLEMENTED",
                                 "The _write() method is not implemented");
        return ItemNull;
    }

    return js_bool_item(accepted);
}

static Item js_stream_emit_finish_tick(Item self) {
    ensure_keys();
    if (js_item_is_true(js_property_get(self, key_destroyed)) ||
        js_item_is_true(js_property_get(self, key_finish_emitted))) {
        return make_js_undefined();
    }
    js_property_set(self, key_finish_emitted, js_bool_item(true));
    stream_emit(self, "finish", NULL, 0);
    return make_js_undefined();
}

static void js_stream_schedule_finish(Item self) {
    Item bound_args[1] = { self };
    Item tick = js_bind_function(js_new_function((void*)js_stream_emit_finish_tick, 1),
                                 make_js_undefined(), bound_args, 1);
    js_next_tick_enqueue(tick);
}

static bool js_stream_has_error(Item err) {
    return err.item != 0 && get_type_id(err) != LMD_TYPE_UNDEFINED &&
           get_type_id(err) != LMD_TYPE_NULL;
}

static Item js_stream_make_error_with_code(const char* code, const char* message) {
    Item err = js_new_error(make_string_item(message));
    js_property_set(err, make_string_item("code"), make_string_item(code));
    return err;
}

static Item js_stream_make_type_error_with_code(const char* code, const char* message) {
    Item err = js_new_error_with_name(make_string_item("TypeError"), make_string_item(message));
    js_property_set(err, make_string_item("code"), make_string_item(code));
    return err;
}

static Item js_stream_call_callback_tick(Item callback) {
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, ItemNull, NULL, 0);
    }
    return make_js_undefined();
}

static Item js_stream_call_callback_error_tick(Item callback, Item err) {
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, ItemNull, &err, 1);
    }
    return make_js_undefined();
}

static void js_stream_schedule_callback(Item callback) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) return;
    Item tick = js_bind_function(js_new_function((void*)js_stream_call_callback_tick, 1),
                                 make_js_undefined(), &callback, 1);
    js_next_tick_enqueue(tick);
}

static void js_stream_schedule_callback_error(Item callback, Item err) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) return;
    Item bound_args[2] = { callback, err };
    Item tick = js_bind_function(js_new_function((void*)js_stream_call_callback_error_tick, 2),
                                 make_js_undefined(), bound_args, 2);
    js_next_tick_enqueue(tick);
}

static Item js_stream_finish_after_final(Item self, Item callback, Item err) {
    ensure_keys();
    if (js_stream_has_error(err)) {
        if (get_type_id(callback) == LMD_TYPE_FUNC) {
            js_call_function(callback, self, &err, 1);
        }
        js_process_emit(make_string_item("uncaughtException"), err);
        return make_js_undefined();
    }

    if (js_item_is_true(js_property_get(self, key_destroyed))) {
        return make_js_undefined();
    }

    stream_emit(self, "prefinish", NULL, 0);
    js_stream_mark_writable_finished(self);
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, self, NULL, 0);
    }
    js_stream_emit_finish_tick(self);
    return make_js_undefined();
}

static Item js_stream_complete_finish_tick(Item self, Item callback) {
    ensure_keys();
    if (js_item_is_true(js_property_get(self, key_destroyed))) {
        return make_js_undefined();
    }
    js_stream_mark_writable_finished(self);
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, self, NULL, 0);
    }
    js_stream_schedule_finish(self);
    return make_js_undefined();
}

static void js_stream_schedule_finish_ready(Item self, Item callback) {
    Item bound_args[2] = { self, callback };
    Item tick = js_bind_function(js_new_function((void*)js_stream_complete_finish_tick, 2),
                                 make_js_undefined(), bound_args, 2);
    js_next_tick_enqueue(tick);
}

// end([chunk][, callback]) — signal end of writes
extern "C" Item js_writable_end(Item self, Item chunk, Item callback) {
    ensure_keys();
    if (get_type_id(chunk) == LMD_TYPE_FUNC &&
        (callback.item == 0 || get_type_id(callback) == LMD_TYPE_UNDEFINED)) {
        callback = chunk;
        chunk = make_js_undefined();
    }

    if (js_item_is_true(js_property_get(self, key_finish_emitted))) {
        Item err = js_stream_make_error_with_code("ERR_STREAM_ALREADY_FINISHED",
            "Cannot call end after a stream was finished");
        js_stream_schedule_callback_error(callback, err);
        return self;
    }

    Item wstate = js_property_get(self, key_writable_state);
    if (js_state_get_bool(wstate, "ended")) {
        js_stream_schedule_callback(callback);
        return self;
    }

    // write final chunk if provided
    if (chunk.item != 0 && get_type_id(chunk) != LMD_TYPE_UNDEFINED &&
        get_type_id(chunk) != LMD_TYPE_NULL) {
        js_writable_write(self, chunk, make_js_undefined(), make_js_undefined());
        // if write threw (e.g. ERR_METHOD_NOT_IMPLEMENTED), propagate the exception
        if (js_check_exception()) return ItemNull;
    }

    // uncork all if corked
    Item corked = js_property_get(self, make_string_item("_corked"));
    if (get_type_id(corked) == LMD_TYPE_INT && it2i(corked) > 0) {
        js_stream_set_writable_corked(self, 0);
        Item pending = js_property_get(self, make_string_item("_pendingWrites"));
        if (get_type_id(pending) == LMD_TYPE_ARRAY && js_array_length(pending) > 0) {
            js_stream_flush_pending_writes(self);
        }
    }

    js_stream_mark_writable_ended(self);

    // call _final if set
    Item final_fn = js_property_get(self, make_string_item("_final"));
    if (get_type_id(final_fn) == LMD_TYPE_FUNC) {
        Item bound_args[2] = { self, callback };
        Item final_cb = js_bind_function(js_new_function((void*)js_stream_finish_after_final, 3),
                                         make_js_undefined(), bound_args, 2);
        js_call_function(final_fn, self, &final_cb, 1);
        return self;
    }

    stream_emit(self, "prefinish", NULL, 0);
    js_stream_schedule_finish_ready(self, callback);
    return self;
}

// cork() — buffer writes
extern "C" Item js_writable_cork(Item self) {
    ensure_keys();
    Item count = js_property_get(self, make_string_item("_corked"));
    int64_t c = (get_type_id(count) == LMD_TYPE_INT) ? it2i(count) : 0;
    js_stream_set_writable_corked(self, c + 1);
    return make_js_undefined();
}

// uncork() — flush corked writes
extern "C" Item js_writable_uncork(Item self) {
    ensure_keys();
    Item count = js_property_get(self, make_string_item("_corked"));
    int64_t c = (get_type_id(count) == LMD_TYPE_INT) ? it2i(count) : 0;
    if (c > 0) c--;
    js_stream_set_writable_corked(self, c);
    // when fully uncorked, flush pending writes
    if (c == 0) {
        Item pending = js_property_get(self, make_string_item("_pendingWrites"));
        if (get_type_id(pending) == LMD_TYPE_ARRAY && js_array_length(pending) > 0) {
            js_stream_flush_pending_writes(self);
        }
    }
    return make_js_undefined();
}

// Writable constructor
extern "C" Item js_writable_new(Item opts) {
    ensure_keys();
    Item obj = js_stream_create_instance(stream_writable_prototype);

    js_class_stamp(obj, JS_CLASS_WRITABLE);
    js_property_set(obj, key_writable, js_bool_item(true));
    js_property_set(obj, key_finished, js_bool_item(false));
    js_property_set(obj, key_destroyed, js_bool_item(false));
    js_property_set(obj, make_string_item("destroyed"), js_bool_item(false));
    js_property_set(obj, make_string_item("errored"), ItemNull);
    js_property_set(obj, key_finish_emitted, js_bool_item(false));
    js_property_set(obj, key_close_emitted, js_bool_item(false));
    js_property_set(obj, key_writable_state, js_create_writable_state());
    js_stream_set_writable_corked(obj, 0);
    js_stream_set_buffered_request_count(obj, 0);
    js_stream_define_bool(obj, "writableEnded", false);
    js_stream_define_bool(obj, "writableFinished", false);
    js_property_set(obj, key_listeners, js_new_object());
    js_stream_init_writable_options(obj);

    js_property_set(obj, key_on, js_new_function((void*)js_stream_inst_on, 2));
    js_property_set(obj, make_string_item("once"), js_new_function((void*)js_stream_inst_once, 2));
    js_property_set(obj, make_string_item("off"), js_new_function((void*)js_stream_inst_off, 2));
    js_property_set(obj, make_string_item("removeListener"), js_new_function((void*)js_stream_inst_off, 2));
    js_property_set(obj, make_string_item("removeAllListeners"), js_new_function((void*)js_stream_inst_removeAllListeners, 1));
    js_property_set(obj, key_emit, js_new_function((void*)js_stream_inst_emit, 2));
    js_property_set(obj, make_string_item("eventNames"), js_new_function((void*)js_stream_inst_eventNames, 0));
    js_property_set(obj, make_string_item("listenerCount"), js_new_function((void*)js_stream_inst_listenerCount, 2));
    js_property_set(obj, key_write, js_new_function((void*)js_writable_inst_write, 3));
    js_property_set(obj, key_end, js_new_function((void*)js_writable_inst_end, 2));
    js_property_set(obj, key_destroy, js_new_function((void*)js_stream_inst_destroy, 1));
    js_property_set(obj, make_string_item("cork"), js_new_function((void*)js_writable_inst_cork, 0));
    js_property_set(obj, make_string_item("uncork"), js_new_function((void*)js_writable_inst_uncork, 0));
    js_property_set(obj, make_string_item("setEncoding"), js_new_function((void*)js_stream_inst_setEncoding, 1));
    js_property_set(obj, make_string_item("setDefaultEncoding"), js_new_function((void*)js_stream_inst_setDefaultEncoding, 1));

    if (!propagate_stream_options(obj, opts)) return ItemNull;
    return obj;
}

// =============================================================================
// Duplex stream (Readable + Writable)
// =============================================================================

extern "C" Item js_duplex_new(Item opts) {
    ensure_keys();
    Item obj = js_stream_create_instance(stream_duplex_prototype);

    js_class_stamp(obj, JS_CLASS_DUPLEX);
    js_property_set(obj, key_readable, js_bool_item(true));
    js_property_set(obj, key_writable, js_bool_item(true));
    js_property_set(obj, key_flowing, js_bool_item(false));
    js_property_set(obj, key_ended, js_bool_item(false));
    js_property_set(obj, key_finished, js_bool_item(false));
    js_property_set(obj, key_destroyed, js_bool_item(false));
    js_property_set(obj, make_string_item("destroyed"), js_bool_item(false));
    js_property_set(obj, make_string_item("errored"), ItemNull);
    js_property_set(obj, make_string_item("readableAborted"), js_bool_item(false));
    js_property_set(obj, key_finish_emitted, js_bool_item(false));
    js_property_set(obj, key_end_pending, js_bool_item(false));
    js_property_set(obj, key_end_emitted, js_bool_item(false));
    js_property_set(obj, key_reading, js_bool_item(false));
    js_property_set(obj, key_paused, js_bool_item(false));
    js_property_set(obj, key_close_emitted, js_bool_item(false));
    js_property_set(obj, key_auto_destroy, js_bool_item(true));
    js_property_set(obj, key_readable_state, js_create_readable_state());
    js_property_set(obj, key_writable_state, js_create_writable_state());
    js_stream_set_writable_corked(obj, 0);
    js_stream_set_buffered_request_count(obj, 0);
    js_stream_define_bool(obj, "readableEnded", false);
    js_stream_define_bool(obj, "writableEnded", false);
    js_stream_define_bool(obj, "writableFinished", false);
    js_stream_set_readable_buffer(obj, js_array_new(0));
    js_property_set(obj, key_listeners, js_new_object());
    js_stream_init_readable_options(obj);
    js_stream_init_writable_options(obj);

    // Readable methods
    js_property_set(obj, key_on, js_new_function((void*)js_stream_inst_on, 2));
    js_property_set(obj, make_string_item("once"), js_new_function((void*)js_stream_inst_once, 2));
    js_property_set(obj, make_string_item("off"), js_new_function((void*)js_stream_inst_off, 2));
    js_property_set(obj, make_string_item("removeListener"), js_new_function((void*)js_stream_inst_off, 2));
    js_property_set(obj, make_string_item("removeAllListeners"), js_new_function((void*)js_stream_inst_removeAllListeners, 1));
    js_property_set(obj, key_emit, js_new_function((void*)js_stream_inst_emit, 2));
    js_property_set(obj, make_string_item("eventNames"), js_new_function((void*)js_stream_inst_eventNames, 0));
    js_property_set(obj, make_string_item("listenerCount"), js_new_function((void*)js_stream_inst_listenerCount, 2));
    js_property_set(obj, key_push, js_new_function((void*)js_readable_inst_push, 2));
    js_property_set(obj, make_string_item("unshift"), js_new_function((void*)js_readable_inst_unshift, 2));
    js_property_set(obj, key_read, js_new_function((void*)js_readable_inst_read, 1));
    js_property_set(obj, key_pipe, js_new_function((void*)js_readable_inst_pipe, 1));
    js_property_set(obj, make_string_item("resume"), js_new_function((void*)js_readable_inst_resume, 0));
    js_property_set(obj, make_string_item("pause"), js_new_function((void*)js_readable_inst_pause, 0));
    js_property_set(obj, make_string_item("isPaused"), js_new_function((void*)js_readable_inst_isPaused, 0));

    // Writable methods
    js_property_set(obj, key_write, js_new_function((void*)js_writable_inst_write, 3));
    js_property_set(obj, key_end, js_new_function((void*)js_writable_inst_end, 2));
    js_property_set(obj, key_destroy, js_new_function((void*)js_stream_inst_destroy, 1));
    js_property_set(obj, make_string_item("cork"), js_new_function((void*)js_writable_inst_cork, 0));
    js_property_set(obj, make_string_item("uncork"), js_new_function((void*)js_writable_inst_uncork, 0));
    js_property_set(obj, make_string_item("setEncoding"), js_new_function((void*)js_stream_inst_setEncoding, 1));
    js_property_set(obj, make_string_item("setDefaultEncoding"), js_new_function((void*)js_stream_inst_setDefaultEncoding, 1));
    js_stream_install_async_iterator(obj);

    if (!propagate_stream_options(obj, opts)) return ItemNull;
    return obj;
}

// =============================================================================
// Transform stream (Duplex with _transform)
// =============================================================================

// _transform(chunk, encoding, callback) override
extern "C" Item js_transform_write(Item self, Item chunk, Item encoding, Item callback) {
    ensure_keys();
    if (get_type_id(encoding) == LMD_TYPE_FUNC &&
        (callback.item == 0 || get_type_id(callback) == LMD_TYPE_UNDEFINED)) {
        callback = encoding;
        encoding = make_js_undefined();
    }

    // call _transform if set
    Item transform_fn = js_property_get(self, make_string_item("_transform"));
    if (get_type_id(transform_fn) == LMD_TYPE_FUNC) {
        if (!js_stream_validate_writable_chunk(self, chunk)) {
            Item err = js_clear_exception();
            js_stream_schedule_callback_error(callback, err);
            js_stream_schedule_error(self, err);
            return js_bool_item(false);
        }
        if (!js_stream_prepare_writable_chunk(self, &chunk, &encoding)) {
            Item err = js_clear_exception();
            js_stream_schedule_callback_error(callback, err);
            js_stream_schedule_error(self, err);
            return js_bool_item(false);
        }
        bool accepted = js_stream_begin_write(self, chunk);
        Item write_cb = js_stream_make_transform_write_callback(self, callback);
        Item args[3] = {chunk, encoding, write_cb};
        Item result = js_call_function(transform_fn, self, args, 3);
        if (js_check_exception()) {
            Item err = js_clear_exception();
            js_stream_after_write(self, callback, err);
            return js_bool_item(false);
        }
        // if _transform returns data, push it
        if (result.item != 0 && get_type_id(result) != LMD_TYPE_UNDEFINED) {
            js_readable_push(self, result);
            if (js_check_exception()) {
                Item err = js_clear_exception();
                js_stream_after_write(self, callback, err);
                return js_bool_item(false);
            }
        }
        return js_bool_item(accepted);
    } else {
        // no _transform method — throw ERR_METHOD_NOT_IMPLEMENTED
        js_throw_error_with_code("ERR_METHOD_NOT_IMPLEMENTED",
                                 "The _transform() method is not implemented");
        return ItemNull;
    }
}

extern "C" Item js_transform_end(Item self, Item chunk, Item callback) {
    ensure_keys();
    if (get_type_id(chunk) == LMD_TYPE_FUNC &&
        (callback.item == 0 || get_type_id(callback) == LMD_TYPE_UNDEFINED)) {
        callback = chunk;
        chunk = make_js_undefined();
    }

    if (chunk.item != 0 && get_type_id(chunk) != LMD_TYPE_UNDEFINED &&
        get_type_id(chunk) != LMD_TYPE_NULL) {
        js_transform_write(self, chunk, make_js_undefined(), make_js_undefined());
        // if transform threw (e.g. ERR_METHOD_NOT_IMPLEMENTED), propagate the exception
        if (js_check_exception()) return ItemNull;
    }

    // call _flush if set, with callback
    Item flush_fn = js_property_get(self, make_string_item("_flush"));
    if (get_type_id(flush_fn) == LMD_TYPE_FUNC) {
        Item noop = js_new_function((void*)js_noop, 0);
        js_call_function(flush_fn, self, &noop, 1);
    }

    // call _final if set, with callback
    Item final_fn = js_property_get(self, make_string_item("_final"));
    if (get_type_id(final_fn) == LMD_TYPE_FUNC) {
        Item noop = js_new_function((void*)js_noop, 0);
        js_call_function(final_fn, self, &noop, 1);
    }

    js_stream_mark_writable_ended(self);
    stream_emit(self, "prefinish", NULL, 0);
    js_stream_mark_writable_finished(self);
    js_readable_push(self, ItemNull); // signal end
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, self, NULL, 0);
    }
    js_stream_schedule_finish(self);
    return self;
}

extern "C" Item js_transform_new(Item opts) {
    ensure_keys();
    Item obj = js_stream_create_instance(stream_transform_prototype);

    js_class_stamp(obj, JS_CLASS_TRANSFORM);
    js_property_set(obj, key_readable, js_bool_item(true));
    js_property_set(obj, key_writable, js_bool_item(true));
    js_property_set(obj, key_flowing, js_bool_item(false));
    js_property_set(obj, key_ended, js_bool_item(false));
    js_property_set(obj, key_finished, js_bool_item(false));
    js_property_set(obj, key_destroyed, js_bool_item(false));
    js_property_set(obj, make_string_item("destroyed"), js_bool_item(false));
    js_property_set(obj, make_string_item("errored"), ItemNull);
    js_property_set(obj, make_string_item("readableAborted"), js_bool_item(false));
    js_property_set(obj, key_finish_emitted, js_bool_item(false));
    js_property_set(obj, key_end_pending, js_bool_item(false));
    js_property_set(obj, key_end_emitted, js_bool_item(false));
    js_property_set(obj, key_reading, js_bool_item(false));
    js_property_set(obj, key_paused, js_bool_item(false));
    js_property_set(obj, key_close_emitted, js_bool_item(false));
    js_property_set(obj, key_auto_destroy, js_bool_item(true));
    js_property_set(obj, key_readable_state, js_create_readable_state());
    js_property_set(obj, key_writable_state, js_create_writable_state());
    js_stream_set_writable_corked(obj, 0);
    js_stream_set_buffered_request_count(obj, 0);
    js_stream_define_bool(obj, "readableEnded", false);
    js_stream_define_bool(obj, "writableEnded", false);
    js_stream_define_bool(obj, "writableFinished", false);
    js_stream_set_readable_buffer(obj, js_array_new(0));
    js_property_set(obj, key_listeners, js_new_object());
    js_stream_init_readable_options(obj);
    js_stream_init_writable_options(obj);

    // Readable methods
    js_property_set(obj, key_on, js_new_function((void*)js_stream_inst_on, 2));
    js_property_set(obj, make_string_item("once"), js_new_function((void*)js_stream_inst_once, 2));
    js_property_set(obj, make_string_item("off"), js_new_function((void*)js_stream_inst_off, 2));
    js_property_set(obj, make_string_item("removeListener"), js_new_function((void*)js_stream_inst_off, 2));
    js_property_set(obj, make_string_item("removeAllListeners"), js_new_function((void*)js_stream_inst_removeAllListeners, 1));
    js_property_set(obj, key_emit, js_new_function((void*)js_stream_inst_emit, 2));
    js_property_set(obj, make_string_item("eventNames"), js_new_function((void*)js_stream_inst_eventNames, 0));
    js_property_set(obj, make_string_item("listenerCount"), js_new_function((void*)js_stream_inst_listenerCount, 2));
    js_property_set(obj, key_push, js_new_function((void*)js_readable_inst_push, 2));
    js_property_set(obj, make_string_item("unshift"), js_new_function((void*)js_readable_inst_unshift, 2));
    js_property_set(obj, key_read, js_new_function((void*)js_readable_inst_read, 1));
    js_property_set(obj, key_pipe, js_new_function((void*)js_readable_inst_pipe, 1));
    js_property_set(obj, make_string_item("resume"), js_new_function((void*)js_readable_inst_resume, 0));
    js_property_set(obj, make_string_item("pause"), js_new_function((void*)js_readable_inst_pause, 0));
    js_property_set(obj, make_string_item("isPaused"), js_new_function((void*)js_readable_inst_isPaused, 0));

    // Writable methods using transform
    js_property_set(obj, key_write, js_new_function((void*)js_transform_inst_write, 3));
    js_property_set(obj, key_end, js_new_function((void*)js_transform_inst_end, 2));
    js_property_set(obj, key_destroy, js_new_function((void*)js_stream_inst_destroy, 1));
    js_property_set(obj, make_string_item("cork"), js_new_function((void*)js_writable_inst_cork, 0));
    js_property_set(obj, make_string_item("uncork"), js_new_function((void*)js_writable_inst_uncork, 0));
    js_property_set(obj, make_string_item("setEncoding"), js_new_function((void*)js_stream_inst_setEncoding, 1));
    js_property_set(obj, make_string_item("setDefaultEncoding"), js_new_function((void*)js_stream_inst_setDefaultEncoding, 1));
    js_stream_install_async_iterator(obj);

    if (!propagate_stream_options(obj, opts)) return ItemNull;
    return obj;
}

// =============================================================================
// PassThrough — Transform that passes data unchanged
// =============================================================================

// PassThrough default _transform: just push data through
extern "C" Item js_passthrough_transform(Item chunk, Item encoding, Item callback) {
    (void)encoding;
    Item self = js_get_this();
    js_readable_push(self, chunk);
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, ItemNull, NULL, 0);
    }
    return make_js_undefined();
}

extern "C" Item js_passthrough_new(Item opts) {
    Item obj = js_transform_new(opts);
    js_class_stamp(obj, JS_CLASS_PASS_THROUGH);
    if (!js_stream_called_as_constructor() && js_stream_is_object_like(stream_passthrough_prototype)) {
        js_set_prototype(obj, stream_passthrough_prototype);
    }
    // set default _transform for pass-through behavior
    js_property_set(obj, make_string_item("_transform"),
                    js_new_function((void*)js_passthrough_transform, 3));
    return obj;
}

// =============================================================================
// pipeline(source, ...transforms, destination, callback) — pipe chain
// =============================================================================

static bool js_stream_pipeline_source_ended(Item source) {
    ensure_keys();
    return js_item_is_true(js_property_get(source, key_end_emitted)) ||
           js_item_is_true(js_property_get(source, key_end_pending)) ||
           js_state_get_bool(js_property_get(source, key_readable_state), "endEmitted");
}

static Item js_stream_pipeline_invoke_callback(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item callback = env[2];
    if (get_type_id(callback) != LMD_TYPE_FUNC) return make_js_undefined();
    Item err = env[8];
    if (js_stream_has_error(err)) {
        js_call_function(callback, ItemNull, &err, 1);
    } else {
        js_call_function(callback, ItemNull, NULL, 0);
    }
    if (js_check_exception()) {
        Item thrown = js_clear_exception();
        js_process_emit(make_string_item("uncaughtException"), thrown);
    }
    return make_js_undefined();
}

static Item js_stream_pipeline_call_once(Item* env, Item err) {
    if (!env || js_item_is_true(env[3])) return make_js_undefined();
    env[3] = js_bool_item(true);
    Item dest = env[1];
    if (js_item_is_true(js_property_get(dest, key_readable))) {
        js_stream_off(dest, make_string_item("error"), env[6]);
        js_stream_off(dest, make_string_item("finish"), env[7]);
    }
    Item callback = env[2];
    if (get_type_id(callback) != LMD_TYPE_FUNC) return make_js_undefined();
    env[8] = err;
    js_next_tick_enqueue(js_new_closure((void*)js_stream_pipeline_invoke_callback, 0, env, 9));
    return make_js_undefined();
}

static Item js_stream_pipeline_destroy_dest(Item dest, Item err) {
    if (js_item_is_true(js_property_get(dest, key_destroyed))) return make_js_undefined();
    Item destroy_fn = js_property_get(dest, key_destroy);
    if (get_type_id(destroy_fn) != LMD_TYPE_FUNC) return make_js_undefined();
    js_call_function(destroy_fn, dest, &err, 1);
    return make_js_undefined();
}

static Item js_stream_pipeline_on_close(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item source = env[0];
    if (js_stream_pipeline_source_ended(source)) {
        return make_js_undefined();
    }

    Item err = js_stream_make_error_with_code("ERR_STREAM_PREMATURE_CLOSE",
        "Premature close");
    js_stream_pipeline_call_once(env, err);
    js_stream_pipeline_destroy_dest(env[1], err);
    return make_js_undefined();
}

static Item js_stream_pipeline_on_error(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    return js_stream_pipeline_call_once(env, err);
}

static Item js_stream_pipeline_on_finish(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    return js_stream_pipeline_call_once(env, make_js_undefined());
}

extern "C" Item js_stream_pipeline(Item source, Item dest, Item callback) {
    ensure_keys();
    Item actual_dest = dest;
    if (get_type_id(dest) == LMD_TYPE_FUNC && get_type_id(callback) == LMD_TYPE_FUNC) {
        actual_dest = js_passthrough_new(make_js_undefined());
    }
    // simplified two-argument pipeline (most common case)
    // for multi-step, chain pipe() calls
    Item pipe_fn = js_property_get(source, key_pipe);
    if (get_type_id(pipe_fn) == LMD_TYPE_FUNC) {
        js_call_function(pipe_fn, source, &actual_dest, 1);
    }
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        Item* env = js_alloc_env(9);
        env[0] = source;
        env[1] = actual_dest;
        env[2] = callback;
        env[3] = js_bool_item(false);
        Item source_close = js_new_closure((void*)js_stream_pipeline_on_close, 0, env, 9);
        Item source_error = js_new_closure((void*)js_stream_pipeline_on_error, 1, env, 9);
        Item dest_error = js_new_closure((void*)js_stream_pipeline_on_error, 1, env, 9);
        Item dest_finish = js_new_closure((void*)js_stream_pipeline_on_finish, 0, env, 9);
        env[4] = source_close;
        env[5] = source_error;
        env[6] = dest_error;
        env[7] = dest_finish;
        env[8] = make_js_undefined();
        js_stream_once(source, make_string_item("close"), source_close);
        js_stream_once(source, make_string_item("error"), source_error);
        js_stream_on(actual_dest, make_string_item("error"), dest_error);
        js_stream_on(actual_dest, make_string_item("finish"), dest_finish);
    }
    return actual_dest;
}

static Item js_readable_from_pump(Item env_item);

static Item js_readable_from_on_step(Item env_item, Item result) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item readable = env[0];
    if (js_item_is_true(js_property_get(readable, key_destroyed))) {
        return make_js_undefined();
    }

    if (js_iterator_result_done(result)) {
        js_readable_push(readable, ItemNull);
        return make_js_undefined();
    }

    Item value = js_iterator_result_value(result);
    if (get_type_id(value) == LMD_TYPE_NULL) {
        Item err = js_stream_make_type_error_with_code("ERR_STREAM_NULL_VALUES",
            "May not write null values to stream");
        js_stream_destroy(readable, err);
        return make_js_undefined();
    }
    js_readable_push(readable, value);
    return js_readable_from_pump(env_item);
}

static Item js_readable_from_on_error(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    js_stream_destroy(env[0], err);
    return make_js_undefined();
}

static Item js_readable_from_pump(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item readable = env[0];
    if (js_item_is_true(js_property_get(readable, key_destroyed))) {
        return make_js_undefined();
    }

    Item step = js_async_iterator_step_result(env[1]);
    if (js_check_exception()) {
        Item err = js_clear_exception();
        js_stream_destroy(readable, err);
        return make_js_undefined();
    }

    Item on_step = js_new_closure((void*)js_readable_from_on_step, 1, env, 2);
    Item on_error = js_new_closure((void*)js_readable_from_on_error, 1, env, 2);
    js_promise_then(step, on_step, on_error);
    return make_js_undefined();
}

static bool js_readable_from_is_iterable(Item value) {
    TypeId type = get_type_id(value);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_ELEMENT && type != LMD_TYPE_ARRAY) {
        return false;
    }
    Item async_iter = js_property_get(value, make_string_item("__sym_5"));
    if (get_type_id(async_iter) == LMD_TYPE_FUNC) return true;
    Item iter = js_property_get(value, make_string_item("__sym_1"));
    return get_type_id(iter) == LMD_TYPE_FUNC;
}

// Readable.from(iterable) — create readable from iterable values
extern "C" Item js_readable_from(Item iterable) {
    ensure_keys();
    Item readable = js_readable_new(ItemNull);

    if (get_type_id(iterable) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(iterable);
        for (int64_t i = 0; i < len; i++) {
            Item value = js_array_get_int(iterable, i);
            if (get_type_id(value) == LMD_TYPE_NULL) {
                Item err = js_stream_make_type_error_with_code("ERR_STREAM_NULL_VALUES",
                    "May not write null values to stream");
                js_stream_destroy(readable, err);
                return readable;
            }
            js_readable_push(readable, value);
        }
        js_readable_push(readable, ItemNull); // end
    } else if (get_type_id(iterable) == LMD_TYPE_STRING) {
        js_readable_push(readable, iterable);
        js_readable_push(readable, ItemNull);
    } else if (js_readable_from_is_iterable(iterable)) {
        Item iterator = js_get_async_iterator(iterable);
        if (js_check_exception()) {
            Item err = js_clear_exception();
            js_stream_destroy(readable, err);
            return readable;
        }
        Item* env = js_alloc_env(2);
        env[0] = readable;
        env[1] = iterator;
        js_readable_from_pump((Item){.item = (uint64_t)(uintptr_t)env});
    }

    return readable;
}

// ─── stream.finished(stream, callback) ──────────────────────────────────────
// Detect when a stream is no longer readable/writable/errored. Calls callback
// when the stream is consumed or an error occurs.
static Item js_stream_finished_wrapper_key(void) {
    return make_string_item("__lambda_stream_finished_context_callback__");
}

static Item js_stream_finished_context_callback(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item callback = env[0];
    Item context = env[1];
    int64_t has_arg = js_stream_has_error(err) ? 1 : 0;
    return js_als_context_call(context, callback, js_get_this(), err, has_arg);
}

static Item js_stream_finished_context_wrapper(Item callback) {
    Item context = js_als_capture_context();
    if (get_type_id(context) != LMD_TYPE_ARRAY || js_array_length(context) <= 0) {
        return callback;
    }
    Item* env = js_alloc_env(2);
    env[0] = callback;
    env[1] = context;
    Item wrapper = js_new_closure((void*)js_stream_finished_context_callback, 1, env, 2);
    js_property_set(callback, js_stream_finished_wrapper_key(), wrapper);
    return wrapper;
}

static void js_stream_finished_call_now(Item callback) {
    Item context = js_als_capture_context();
    Item args[1] = { ItemNull };
    if (get_type_id(context) == LMD_TYPE_ARRAY && js_array_length(context) > 0) {
        js_als_context_call(context, callback, ItemNull, ItemNull, 1);
        return;
    }
    js_call_function(callback, ItemNull, args, 1);
}

extern "C" Item js_stream_finished(Item stream, Item callback) {
    ensure_keys();
    if (get_type_id(callback) != LMD_TYPE_FUNC) return make_js_undefined();

    // check if already finished/destroyed
    Item fin = js_property_get(stream, key_finished);
    Item des = js_property_get(stream, key_destroyed);
    bool is_done = (get_type_id(fin) == LMD_TYPE_BOOL && it2b(fin)) ||
                   (get_type_id(des) == LMD_TYPE_BOOL && it2b(des));

    if (is_done) {
        // call callback immediately with no error
        js_stream_finished_call_now(callback);
        return make_js_undefined();
    }

    Item registered_callback = js_stream_finished_context_wrapper(callback);

    // register on stream-local events; stream_emit() reads this listener table.
    Item end_event = make_string_item("end");
    Item finish_event = make_string_item("finish");
    Item error_event = make_string_item("error");
    Item close_event = make_string_item("close");

    js_stream_on(stream, end_event, registered_callback);
    js_stream_on(stream, finish_event, registered_callback);
    js_stream_on(stream, error_event, registered_callback);
    js_stream_on(stream, close_event, registered_callback);

    return make_js_undefined();
}

static bool js_stream_is_abort_signal(Item signal) {
    if (js_class_id(signal) == JS_CLASS_ABORT_SIGNAL) return true;
    Item aborted = js_property_get(signal, make_string_item("aborted"));
    Item add_event = js_property_get(signal, make_string_item("addEventListener"));
    return get_type_id(aborted) == LMD_TYPE_BOOL && get_type_id(add_event) == LMD_TYPE_FUNC;
}

static bool js_stream_is_stream_like(Item stream) {
    JsClass cls = js_class_id(stream);
    if (cls == JS_CLASS_READABLE || cls == JS_CLASS_WRITABLE || cls == JS_CLASS_DUPLEX ||
        cls == JS_CLASS_TRANSFORM || cls == JS_CLASS_PASS_THROUGH) {
        return true;
    }
    if (get_type_id(stream) != LMD_TYPE_MAP) return false;
    Item on_fn = js_property_get(stream, key_on);
    Item destroy_fn = js_property_get(stream, key_destroy);
    return get_type_id(on_fn) == LMD_TYPE_FUNC || get_type_id(destroy_fn) == LMD_TYPE_FUNC;
}

extern "C" Item js_stream_addAbortSignalNoValidate(Item signal, Item stream) {
    ensure_keys();
    Item aborted = js_property_get(signal, make_string_item("aborted"));
    if (get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted)) {
        Item destroy_fn = js_property_get(stream, key_destroy);
        if (get_type_id(destroy_fn) == LMD_TYPE_FUNC) {
            Item reason = js_property_get(signal, make_string_item("reason"));
            js_call_function(destroy_fn, stream, &reason, 1);
        }
    }
    return stream;
}

extern "C" Item js_stream_addAbortSignal(Item signal, Item stream) {
    ensure_keys();
    if (!js_stream_is_abort_signal(signal)) {
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "ERR_INVALID_ARG_TYPE: The \"signal\" argument must be an instance of AbortSignal.");
    }
    if (!js_stream_is_stream_like(stream)) {
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "ERR_INVALID_ARG_TYPE: The \"stream\" argument must be an instance of stream.Stream.");
    }
    return js_stream_addAbortSignalNoValidate(signal, stream);
}

extern "C" Item js_stream_get_readableEnded(void) {
    ensure_keys();
    Item self = js_get_this();
    Item state = js_property_get(self, key_readable_state);
    bool ended = js_state_get_bool(state, "endEmitted") ||
                 js_item_is_true(js_property_get(self, key_end_emitted));
    return js_bool_item(ended);
}

extern "C" Item js_stream_get_writableEnded(void) {
    ensure_keys();
    Item self = js_get_this();
    Item state = js_property_get(self, key_writable_state);
    bool ended = js_state_get_bool(state, "ended");
    return js_bool_item(ended);
}

extern "C" Item js_stream_get_writableFinished(void) {
    ensure_keys();
    Item self = js_get_this();
    Item state = js_property_get(self, key_writable_state);
    bool finished = js_state_get_bool(state, "finished") ||
                    js_item_is_true(js_property_get(self, key_finished));
    return js_bool_item(finished);
}

extern "C" Item js_stream_get_writableCorked(void) {
    ensure_keys();
    Item self = js_get_this();
    Item state = js_property_get(self, key_writable_state);
    Item corked = js_property_get(state, make_string_item("corked"));
    if (get_type_id(corked) == LMD_TYPE_INT) return corked;
    return (Item){.item = i2it(0)};
}

extern "C" Item js_stream_get_writableNeedDrain(void) {
    ensure_keys();
    Item self = js_get_this();
    Item state = js_property_get(self, key_writable_state);
    bool need_drain = js_state_get_bool(state, "needDrain") &&
                      !js_state_get_bool(state, "ended") &&
                      !js_item_is_true(js_property_get(self, key_destroyed));
    return js_bool_item(need_drain);
}

extern "C" Item js_stream_get_writableLength(void) {
    ensure_keys();
    Item self = js_get_this();
    Item state = js_property_get(self, key_writable_state);
    Item length = js_property_get(state, make_string_item("length"));
    if (get_type_id(length) == LMD_TYPE_INT) return length;
    return (Item){.item = i2it(0)};
}

static Item js_stream_constructor_prototype(Item ctor) {
    Item proto_key = make_string_item("prototype");
    Item proto = js_property_get(ctor, proto_key);
    if (get_type_id(proto) != LMD_TYPE_MAP) {
        proto = js_new_object();
        js_property_set(ctor, proto_key, proto);
    }
    return proto;
}

static void js_stream_mark_constructor_prototype(Item ctor, Item proto, JsClass cls) {
    js_class_stamp(proto, cls);
    js_property_set(proto, make_string_item("constructor"), ctor);
    js_mark_non_enumerable(proto, make_string_item("constructor"));
    if (get_type_id(ctor) == LMD_TYPE_FUNC) {
        js_function_set_prototype(ctor, proto);
    }
}

static void js_stream_install_has_instance(Item ctor, void* has_instance) {
    Item key = make_string_item("__sym_3");
    Item fn = js_new_function(has_instance, 1);
    js_create_data_property(ctor, key, fn);
    js_mark_non_enumerable(ctor, key);
}

static void js_stream_install_accessor(Item ctor, const char* name, void* getter) {
    Item proto = js_stream_constructor_prototype(ctor);
    Item getter_fn = js_new_function(getter, 0);
    js_install_native_accessor(proto, make_string_item(name), getter_fn, ItemNull, JSPD_NON_ENUMERABLE);
}

static void js_stream_install_state_accessors(Item readable_ctor, Item writable_ctor,
                                             Item duplex_ctor, Item transform_ctor) {
    js_stream_install_accessor(readable_ctor, "readableEnded", (void*)js_stream_get_readableEnded);
    js_stream_install_accessor(duplex_ctor, "readableEnded", (void*)js_stream_get_readableEnded);
    js_stream_install_accessor(transform_ctor, "readableEnded", (void*)js_stream_get_readableEnded);

    js_stream_install_accessor(writable_ctor, "writableEnded", (void*)js_stream_get_writableEnded);
    js_stream_install_accessor(duplex_ctor, "writableEnded", (void*)js_stream_get_writableEnded);
    js_stream_install_accessor(transform_ctor, "writableEnded", (void*)js_stream_get_writableEnded);

    js_stream_install_accessor(writable_ctor, "writableFinished", (void*)js_stream_get_writableFinished);
    js_stream_install_accessor(duplex_ctor, "writableFinished", (void*)js_stream_get_writableFinished);
    js_stream_install_accessor(transform_ctor, "writableFinished", (void*)js_stream_get_writableFinished);

    js_stream_install_accessor(writable_ctor, "writableCorked", (void*)js_stream_get_writableCorked);
    js_stream_install_accessor(duplex_ctor, "writableCorked", (void*)js_stream_get_writableCorked);
    js_stream_install_accessor(transform_ctor, "writableCorked", (void*)js_stream_get_writableCorked);

    js_stream_install_accessor(writable_ctor, "writableNeedDrain", (void*)js_stream_get_writableNeedDrain);
    js_stream_install_accessor(duplex_ctor, "writableNeedDrain", (void*)js_stream_get_writableNeedDrain);
    js_stream_install_accessor(transform_ctor, "writableNeedDrain", (void*)js_stream_get_writableNeedDrain);

    js_stream_install_accessor(writable_ctor, "writableLength", (void*)js_stream_get_writableLength);
    js_stream_install_accessor(duplex_ctor, "writableLength", (void*)js_stream_get_writableLength);
    js_stream_install_accessor(transform_ctor, "writableLength", (void*)js_stream_get_writableLength);
}

// =============================================================================
// stream Module Namespace
// =============================================================================

static Item stream_namespace = {0};
static Item stream_promises_namespace = {0};

static Item stream_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_set_function_name(fn, key);
    js_property_set(ns, key, fn);
    return fn;
}

static Item js_stream_promisify_custom_symbol(void) {
    return js_symbol_for(make_string_item("nodejs.util.promisify.custom"));
}

static Item js_stream_promises_callback(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[2])) return make_js_undefined();
    env[2] = js_bool_item(true);

    Item resolve = env[0];
    Item reject = env[1];
    Item callback = js_stream_has_error(err) ? reject : resolve;
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        Item value = js_stream_has_error(err) ? err : make_js_undefined();
        js_call_function(callback, make_js_undefined(), &value, 1);
    }
    return make_js_undefined();
}

static Item js_stream_promises_pipeline(Item rest_args) {
    int64_t argc = js_array_length(rest_args);
    if (argc < 2) {
        return js_promise_reject(js_stream_make_type_error_with_code("ERR_MISSING_ARGS",
            "The \"streams\" argument is required"));
    }

    Item capability = js_promise_with_resolvers();
    Item promise = js_property_get(capability, make_string_item("promise"));
    Item* env = js_alloc_env(3);
    env[0] = js_property_get(capability, make_string_item("resolve"));
    env[1] = js_property_get(capability, make_string_item("reject"));
    env[2] = js_bool_item(false);
    Item callback = js_new_closure((void*)js_stream_promises_callback, 1, env, 3);

    Item source = js_array_get_int(rest_args, 0);
    Item dest = js_array_get_int(rest_args, argc - 1);
    js_stream_pipeline(source, dest, callback);
    if (js_check_exception()) {
        Item err = js_clear_exception();
        Item reject = env[1];
        if (get_type_id(reject) == LMD_TYPE_FUNC) {
            js_call_function(reject, make_js_undefined(), &err, 1);
        }
    }
    return promise;
}

static bool js_stream_finished_parse_options(Item options, bool* cleanup) {
    *cleanup = false;
    if (options.item == 0 || get_type_id(options) == LMD_TYPE_UNDEFINED ||
        get_type_id(options) == LMD_TYPE_NULL) {
        return true;
    }
    if (get_type_id(options) != LMD_TYPE_MAP && get_type_id(options) != LMD_TYPE_ELEMENT) {
        js_throw_invalid_arg_type("options", "object", options);
        return false;
    }
    Item cleanup_item = js_property_get(options, make_string_item("cleanup"));
    if (cleanup_item.item == 0 || get_type_id(cleanup_item) == LMD_TYPE_UNDEFINED) {
        return true;
    }
    if (get_type_id(cleanup_item) != LMD_TYPE_BOOL) {
        js_throw_invalid_arg_type("options.cleanup", "boolean", cleanup_item);
        return false;
    }
    *cleanup = it2b(cleanup_item);
    return true;
}

static void js_stream_finished_cleanup(Item stream, Item callback) {
    js_stream_off(stream, make_string_item("end"), callback);
    js_stream_off(stream, make_string_item("finish"), callback);
    js_stream_off(stream, make_string_item("error"), callback);
    js_stream_off(stream, make_string_item("close"), callback);
    Item wrapper = js_property_get(callback, js_stream_finished_wrapper_key());
    if (get_type_id(wrapper) == LMD_TYPE_FUNC && wrapper.item != callback.item) {
        js_stream_off(stream, make_string_item("end"), wrapper);
        js_stream_off(stream, make_string_item("finish"), wrapper);
        js_stream_off(stream, make_string_item("error"), wrapper);
        js_stream_off(stream, make_string_item("close"), wrapper);
    }
}

static Item js_stream_promises_finished_callback(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[4])) return make_js_undefined();
    env[4] = js_bool_item(true);

    Item stream = env[0];
    Item callback = env[1];
    bool cleanup = js_item_is_true(env[2]);
    if (cleanup) {
        js_stream_finished_cleanup(stream, callback);
    }

    Item event_error = err;
    if (!js_stream_has_error(event_error)) {
        Item stored_error = js_property_get(stream, make_string_item("__error__"));
        if (js_stream_has_error(stored_error)) event_error = stored_error;
    }

    Item resolve = env[3];
    Item reject = env[5];
    Item fn = js_stream_has_error(event_error) ? reject : resolve;
    if (get_type_id(fn) == LMD_TYPE_FUNC) {
        Item value = js_stream_has_error(event_error) ? event_error : make_js_undefined();
        js_call_function(fn, make_js_undefined(), &value, 1);
    }
    return make_js_undefined();
}

static Item js_stream_promises_finished(Item rest_args) {
    int64_t argc = js_array_length(rest_args);
    if (argc < 1) {
        return js_promise_reject(js_stream_make_type_error_with_code("ERR_MISSING_ARGS",
            "The \"stream\" argument is required"));
    }

    Item stream = js_array_get_int(rest_args, 0);
    Item options = argc > 1 ? js_array_get_int(rest_args, 1) : make_js_undefined();
    bool cleanup = false;
    if (!js_stream_finished_parse_options(options, &cleanup)) return ItemNull;

    Item capability = js_promise_with_resolvers();
    Item promise = js_property_get(capability, make_string_item("promise"));
    Item* env = js_alloc_env(6);
    env[0] = stream;
    env[1] = make_js_undefined();
    env[2] = js_bool_item(cleanup);
    env[3] = js_property_get(capability, make_string_item("resolve"));
    env[4] = js_bool_item(false);
    env[5] = js_property_get(capability, make_string_item("reject"));
    Item callback = js_new_closure((void*)js_stream_promises_finished_callback, 1, env, 6);
    env[1] = callback;

    js_stream_finished(stream, callback);
    if (js_check_exception()) {
        Item err = js_clear_exception();
        Item reject = env[5];
        if (get_type_id(reject) == LMD_TYPE_FUNC) {
            js_call_function(reject, make_js_undefined(), &err, 1);
        }
    }
    return promise;
}

extern "C" Item js_get_stream_promises_namespace(void) {
    if (stream_promises_namespace.item != 0) return stream_promises_namespace;
    ensure_keys();

    stream_promises_namespace = js_new_object();
    Item pipeline = stream_set_method(stream_promises_namespace, "pipeline",
                                      (void*)js_stream_promises_pipeline, -1);
    Item finished = stream_set_method(stream_promises_namespace, "finished",
                                      (void*)js_stream_promises_finished, -1);
    js_set_function_name(pipeline, make_string_item("pipeline"));
    js_set_function_name(finished, make_string_item("finished"));
    return stream_promises_namespace;
}

static Item js_writable_toWeb(Item writable) {
    Item web = js_writable_stream_new();
    Item ctor = js_get_global_property(make_string_item("WritableStream"));
    Item proto = js_property_get(ctor, make_string_item("prototype"));
    if (get_type_id(proto) == LMD_TYPE_MAP || get_type_id(proto) == LMD_TYPE_ELEMENT) {
        js_set_prototype(web, proto);
    }
    js_property_set(web, make_string_item("__node_stream__"), writable);
    return web;
}

extern "C" Item js_get_stream_namespace(void) {
    if (stream_namespace.item != 0) return stream_namespace;
    ensure_keys();

    stream_namespace = js_new_object();

    Item readable_constructor =
        stream_set_method(stream_namespace, "Readable",    (void*)js_readable_new, 1);
    Item writable_constructor =
        stream_set_method(stream_namespace, "Writable",    (void*)js_writable_new, 1);
    Item duplex_constructor =
        stream_set_method(stream_namespace, "Duplex",      (void*)js_duplex_new, 1);
    Item transform_constructor =
        stream_set_method(stream_namespace, "Transform",   (void*)js_transform_new, 1);
    Item passthrough_constructor =
        stream_set_method(stream_namespace, "PassThrough", (void*)js_passthrough_new, 1);
    Item pipeline_fn = stream_set_method(stream_namespace, "pipeline", (void*)js_stream_pipeline, 3);
    Item finished_fn = stream_set_method(stream_namespace, "finished", (void*)js_stream_finished, 2);
    stream_set_method(stream_namespace, "addAbortSignal", (void*)js_stream_addAbortSignal, 2);
    stream_set_method(stream_namespace, "getDefaultHighWaterMark",
                      (void*)js_stream_getDefaultHighWaterMark, 1);
    stream_set_method(stream_namespace, "setDefaultHighWaterMark",
                      (void*)js_stream_setDefaultHighWaterMark, 2);

    Item promises_ns = js_get_stream_promises_namespace();
    js_property_set(stream_namespace, make_string_item("promises"), promises_ns);
    Item custom_key = js_stream_promisify_custom_symbol();
    js_property_set(pipeline_fn, custom_key, js_property_get(promises_ns, make_string_item("pipeline")));
    js_property_set(finished_fn, custom_key, js_property_get(promises_ns, make_string_item("finished")));
    js_mark_non_enumerable(pipeline_fn, custom_key);
    js_mark_non_enumerable(finished_fn, custom_key);

    // Stream — base class (alias for EventEmitter with pipe method)
    // In Node.js, stream.Stream inherits from EventEmitter
    extern Item js_get_events_namespace(void);
    Item stream_base = js_get_events_namespace();
    js_property_set(stream_namespace, make_string_item("Stream"), stream_base);

    if (get_type_id(readable_constructor) == LMD_TYPE_FUNC) {
        js_property_set(readable_constructor, make_string_item("from"),
                        js_new_function((void*)js_readable_from, 1));
    }
    if (get_type_id(writable_constructor) == LMD_TYPE_FUNC) {
        js_property_set(writable_constructor, make_string_item("toWeb"),
                        js_new_function((void*)js_writable_toWeb, 1));
    }

    if (get_type_id(readable_constructor) == LMD_TYPE_FUNC &&
        get_type_id(writable_constructor) == LMD_TYPE_FUNC &&
        get_type_id(duplex_constructor) == LMD_TYPE_FUNC &&
        get_type_id(transform_constructor) == LMD_TYPE_FUNC &&
        get_type_id(passthrough_constructor) == LMD_TYPE_FUNC) {
        stream_readable_prototype = js_stream_constructor_prototype(readable_constructor);
        stream_writable_prototype = js_stream_constructor_prototype(writable_constructor);
        stream_duplex_prototype = js_stream_constructor_prototype(duplex_constructor);
        stream_transform_prototype = js_stream_constructor_prototype(transform_constructor);
        stream_passthrough_prototype = js_stream_constructor_prototype(passthrough_constructor);
        js_stream_install_async_iterator(stream_readable_prototype);
        js_property_set(stream_readable_prototype, make_string_item("iterator"),
                        js_new_function((void*)js_readable_inst_iterator, 1));

        js_stream_mark_constructor_prototype(readable_constructor, stream_readable_prototype, JS_CLASS_READABLE);
        js_stream_mark_constructor_prototype(writable_constructor, stream_writable_prototype, JS_CLASS_WRITABLE);
        js_stream_mark_constructor_prototype(duplex_constructor, stream_duplex_prototype, JS_CLASS_DUPLEX);
        js_stream_mark_constructor_prototype(transform_constructor, stream_transform_prototype, JS_CLASS_TRANSFORM);
        js_stream_mark_constructor_prototype(passthrough_constructor, stream_passthrough_prototype,
                                             JS_CLASS_PASS_THROUGH);

        js_stream_install_state_accessors(readable_constructor, writable_constructor,
                                          duplex_constructor, transform_constructor);

        js_set_prototype(stream_duplex_prototype, stream_readable_prototype);
        js_set_prototype(stream_transform_prototype, stream_duplex_prototype);
        js_set_prototype(stream_passthrough_prototype, stream_transform_prototype);

        js_stream_install_has_instance(readable_constructor, (void*)js_stream_readable_has_instance);
        js_stream_install_has_instance(writable_constructor, (void*)js_stream_writable_has_instance);
        js_stream_install_has_instance(duplex_constructor, (void*)js_stream_duplex_has_instance);
        js_stream_install_has_instance(transform_constructor, (void*)js_stream_transform_has_instance);
        js_stream_install_has_instance(passthrough_constructor, (void*)js_stream_passthrough_has_instance);
    }

    Item default_key = make_string_item("default");
    js_property_set(stream_namespace, default_key, stream_namespace);

    return stream_namespace;
}

extern "C" Item js_get_stream_iter_namespace(void) {
    if (stream_iter_namespace.item != 0) return stream_iter_namespace;
    ensure_keys();
    stream_iter_namespace = js_new_object();
    stream_set_method(stream_iter_namespace, "push", (void*)js_stream_iter_push, 1);
    stream_set_method(stream_iter_namespace, "ondrain", (void*)js_stream_iter_ondrain, 1);
    stream_set_method(stream_iter_namespace, "text", (void*)js_stream_iter_text, 1);
    js_property_set(stream_iter_namespace, make_string_item("default"), stream_iter_namespace);
    return stream_iter_namespace;
}

extern "C" Item js_get_internal_stream_add_abort_signal_namespace(void) {
    static Item add_abort_ns = {0};
    if (add_abort_ns.item != 0) return add_abort_ns;
    add_abort_ns = js_new_object();
    js_property_set(add_abort_ns, make_string_item("addAbortSignalNoValidate"),
                    js_new_function((void*)js_stream_addAbortSignalNoValidate, 2));
    js_property_set(add_abort_ns, make_string_item("default"), add_abort_ns);
    return add_abort_ns;
}

extern "C" Item js_get_internal_stream_state_namespace(void) {
    if (internal_stream_state_namespace.item != 0) return internal_stream_state_namespace;
    internal_stream_state_namespace = js_new_object();
    js_property_set(internal_stream_state_namespace, make_string_item("getDefaultHighWaterMark"),
                    js_new_function((void*)js_stream_getDefaultHighWaterMark, 1));
    js_property_set(internal_stream_state_namespace, make_string_item("setDefaultHighWaterMark"),
                    js_new_function((void*)js_stream_setDefaultHighWaterMark, 2));
    js_property_set(internal_stream_state_namespace, make_string_item("default"),
                    internal_stream_state_namespace);
    return internal_stream_state_namespace;
}

extern "C" void js_stream_reset(void) {
    stream_namespace = (Item){0};
    keys_init = false;
    stream_readable_prototype = (Item){0};
    stream_writable_prototype = (Item){0};
    stream_duplex_prototype = (Item){0};
    stream_transform_prototype = (Item){0};
    stream_passthrough_prototype = (Item){0};
    internal_stream_state_namespace = (Item){0};
    stream_iter_namespace = (Item){0};
    js_stream_default_byte_hwm = 16 * 1024;
    js_stream_default_object_hwm = 16;
}
