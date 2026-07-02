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

struct JsStreamFuncFlagsAccess {
    TypeId type_id;
    void* func_ptr;
    int param_count;
    Item* env;
    int env_size;
    Item prototype;
    Item bound_this;
    Item* bound_args;
    int bound_argc;
    String* name;
    int builtin_id;
    Item properties_map;
    uint8_t flags;
};

#define JS_STREAM_FUNC_FLAG_GENERATOR 1
#define JS_STREAM_FUNC_FLAG_ASYNC_GEN 64
#define JS_STREAM_FUNC_FLAG_ASYNC 128

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
extern "C" Item js_blob_new(Item parts, Item options);
extern "C" Item js_arraybuffer_new(int byte_length);
extern "C" Item js_typed_array_new_from_buffer(int type_id, Item buffer_item, int byte_offset, int length);
extern "C" Item js_json_parse(Item str_item);
extern "C" Item js_util_inspect(Item obj_item, Item options_item);
extern "C" Item js_to_string(Item value);
extern "C" Item js_symbol_for(Item key);
extern "C" Item js_symbol_create(Item description);
extern "C" Item js_symbol_get_description(Item sym);
extern "C" Item js_object_get_own_property_symbols(Item object);
extern "C" Item js_promise_with_resolvers(void);
extern "C" Item js_promise_resolve(Item value);
extern "C" Item js_promise_reject(Item reason);
extern "C" Item js_promise_then(Item promise, Item on_fulfilled, Item on_rejected);
extern "C" Item js_setTimeout(Item callback, Item delay);
extern "C" Item js_setImmediate(Item callback);
extern "C" Item js_get_async_iterator(Item iterable);
extern "C" Item js_async_iterator_step_result(Item iterator);
extern "C" int64_t js_iterator_result_done(Item result);
extern "C" Item js_iterator_result_value(Item result);
extern "C" bool js_is_async_generator(Item obj);
extern "C" Item js_als_capture_context(void);
extern "C" Item js_als_context_call(Item context, Item callback, Item this_val, Item arg1, int64_t has_arg);
extern "C" Item js_async_hooks_create_resource(const char* type_chars, int type_len);
extern "C" Item js_async_hooks_enter_resource(Item resource);
extern "C" void js_async_hooks_restore_resource(Item previous);
extern Item js_current_this;
extern "C" Item js_get_this(void);
extern "C" Item js_writable_stream_new(Item underlying_sink);
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
static Item key_reading_sync;
static Item key_paused;
static Item key_finish_emitted;
static Item key_close_emitted;
static Item key_closed;
static Item key_capture_rejections;
static Item key_auto_destroy;
static Item key_readable_side_enabled;
static Item key_writable_side_enabled;
static Item key_destroy_pending;
static Item key_listener_fn;
static Item key_listener_context;
static bool keys_init = false;
static Item stream_readable_prototype = {0};
static Item stream_writable_prototype = {0};
static Item stream_duplex_prototype = {0};
static Item stream_transform_prototype = {0};
static Item stream_passthrough_prototype = {0};
static Item internal_stream_state_namespace = {0};
static Item internal_stream_end_of_stream_namespace = {0};
static Item stream_iter_namespace = {0};
static Item stream_web_namespace = {0};
static int64_t js_stream_default_byte_hwm = 16 * 1024;
static int64_t js_stream_default_object_hwm = 16;

static Item js_stream_make_error_with_code(const char* code, const char* message);
static Item js_stream_make_type_error_with_code(const char* code, const char* message);
static bool js_stream_readable_is_object_mode(Item self);
static int64_t js_stream_readable_chunk_length(Item self, Item chunk);
static int64_t js_stream_readable_buffer_length(Item self, Item buf);
static bool js_stream_readable_accepts_more(Item self, Item buf);
static bool js_stream_mark_transform_readable_backpressure(Item self);
static void js_stream_maybe_drain_transform_readable_backpressure(Item self);
static bool js_stream_readable_buffer_has_string(Item buf);
static Item js_stream_concat_decoded_chunks(Item buf, Item encoding);
static bool js_stream_prepare_readable_chunk(Item self, Item* chunk, Item encoding);
extern "C" Item js_readable_push(Item self, Item chunk);
extern "C" Item js_readable_pipe(Item self, Item dest);
extern "C" Item js_readable_new(Item opts);
extern "C" Item js_passthrough_new(Item opts);
extern "C" Item js_writable_end(Item self, Item chunk, Item callback);
extern "C" Item js_stream_destroy(Item self, Item err);
extern "C" Item js_stream_emit(Item self, Item event_item, Item arg1);
extern "C" Item js_stream_on(Item self, Item event_item, Item listener);
static void js_stream_flush_buffered_data(Item self);
static void js_stream_schedule_close(Item self);
static void js_stream_emit_or_schedule_drain(Item self);
static void js_stream_async_iterators_drain(Item stream, Item err);
static void js_writable_maybe_finish_deferred(Item self);
static void js_transform_maybe_finish_deferred(Item self);
static Item js_readable_iterator(Item self, Item options);
static Item js_readable_compose(Item self, Item stream, Item options);
static void js_stream_iter_maybe_drain(Item readable);
static void js_stream_iter_attach_abort(Item options, Item readable);
static bool js_stream_chunk_is_buffer(Item chunk);
static bool js_stream_chunk_is_arraybuffer_view(Item chunk);
static bool js_stream_is_abort_signal(Item signal);
static Item js_stream_attach_abort_signal(Item signal, Item stream);
static bool js_stream_is_stream_like(Item stream);
static bool js_stream_is_native_stream(Item stream);
static Item js_stream_iter_push(Item options_or_transform);
static Item js_stream_iter_make_abort_error(void);
static int64_t js_stream_iter_chunk_byte_length(Item chunk);
static bool js_stream_has_error(Item err);
extern "C" Item js_readable_from(Item iterable);
static Item js_readable_from_pump(Item env_item);
static bool js_readable_from_is_iterable(Item value);
static void js_stream_iter_resolve_drain(Item writer, Item value);
static void js_stream_iter_reject_drain(Item writer, Item err);
static void js_stream_iter_resolve_end_if_drained(Item writer);
static void js_stream_iter_reject_end(Item writer, Item err);
static void js_stream_iter_reject_pending_writes(Item writer, Item err);
static Item js_stream_duplex_pair(void);
static Item js_duplex_from(Item source);
static Item js_stream_compose_rest(Item rest_args);

static bool js_stream_source_keeps_pipe_on_backpressure(Item self) {
    JsClass cls = js_class_id(self);
    return cls == JS_CLASS_DUPLEX || cls == JS_CLASS_TRANSFORM ||
           cls == JS_CLASS_PASS_THROUGH;
}

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
    key_reading_sync = make_string_item("__reading_sync__");
    key_paused = make_string_item("__paused__");
    key_finish_emitted = make_string_item("__finish_emitted__");
    key_close_emitted = make_string_item("__close_emitted__");
    key_closed = make_string_item("closed");
    key_capture_rejections = make_string_item("__capture_rejections__");
    key_auto_destroy = make_string_item("__auto_destroy__");
    key_readable_side_enabled = make_string_item("__readable_side_enabled__");
    key_writable_side_enabled = make_string_item("__writable_side_enabled__");
    key_destroy_pending = make_string_item("__destroy_pending__");
    key_listener_fn = make_string_item("__stream_listener_fn__");
    key_listener_context = make_string_item("__stream_listener_als_context__");
    keys_init = true;
}

static inline Item js_bool_item(bool value) {
    return (Item){.item = b2it(value)};
}

static void js_stream_set_flowing(Item self, bool flowing);
static void js_stream_set_readable_buffer(Item self, Item buffer);
static Item js_stream_pipe_data_noop(Item chunk);
static bool js_stream_has_event_listeners(Item self, const char* event);
static void js_stream_schedule_data_flush(Item self);
static void js_stream_schedule_read(Item self);
static bool js_item_is_true(Item item);
static bool js_state_get_bool(Item state, const char* name);

static void js_readable_clear_pipe(Item self) {
    js_property_set(self, make_string_item("__piped__"), js_bool_item(false));
    js_property_set(self, make_string_item("__pipe_dest__"), make_js_undefined());
    Item state = js_property_get(self, key_readable_state);
    if (get_type_id(state) == LMD_TYPE_MAP)
        js_property_set(state, make_string_item("awaitDrainWriters"), ItemNull);
}

static Item js_stream_await_drain_writers(Item state) {
    if (get_type_id(state) != LMD_TYPE_MAP) return ItemNull;
    return js_property_get(state, make_string_item("awaitDrainWriters"));
}

static bool js_stream_await_drain_set_contains(Item set_like, Item dest) {
    Item writers = js_property_get(set_like, make_string_item("__writers__"));
    if (get_type_id(writers) != LMD_TYPE_ARRAY) return false;
    int64_t len = js_array_length(writers);
    for (int64_t i = 0; i < len; i++) {
        if (js_array_get_int(writers, i).item == dest.item) return true;
    }
    return false;
}

static Item js_stream_make_empty_await_drain_set(void) {
    Item set_like = js_new_object();
    js_property_set(set_like, make_string_item("__writers__"), js_array_new(0));
    js_property_set(set_like, make_string_item("size"), (Item){.item = i2it(0)});
    return set_like;
}

static Item js_stream_make_await_drain_set(Item first, Item second) {
    Item set_like = js_new_object();
    Item writers = js_array_new(0);
    js_array_push(writers, first);
    if (second.item != first.item) js_array_push(writers, second);
    js_property_set(set_like, make_string_item("__writers__"), writers);
    js_property_set(set_like, make_string_item("size"),
                    (Item){.item = i2it(js_array_length(writers))});
    return set_like;
}

static void js_stream_await_drain_add(Item source, Item dest) {
    Item state = js_property_get(source, key_readable_state);
    if (get_type_id(state) != LMD_TYPE_MAP) return;
    Item current = js_stream_await_drain_writers(state);
    if (current.item == 0 ||
        get_type_id(current) == LMD_TYPE_NULL ||
        get_type_id(current) == LMD_TYPE_UNDEFINED) {
        js_property_set(dest, make_string_item("size"), (Item){.item = i2it(1)});
        js_property_set(state, make_string_item("awaitDrainWriters"), dest);
        return;
    }
    if (current.item == dest.item) return;
    Item writers = js_property_get(current, make_string_item("__writers__"));
    if (get_type_id(current) == LMD_TYPE_MAP && get_type_id(writers) == LMD_TYPE_ARRAY) {
        if (!js_stream_await_drain_set_contains(current, dest)) {
            js_array_push(writers, dest);
            js_property_set(current, make_string_item("size"),
                            (Item){.item = i2it(js_array_length(writers))});
        }
        return;
    }
    js_property_set(state, make_string_item("awaitDrainWriters"),
                    js_stream_make_await_drain_set(current, dest));
}

static bool js_stream_await_drain_remove(Item source, Item dest) {
    Item state = js_property_get(source, key_readable_state);
    if (get_type_id(state) != LMD_TYPE_MAP) return false;
    Item current = js_stream_await_drain_writers(state);
    if (current.item == 0 ||
        get_type_id(current) == LMD_TYPE_NULL ||
        get_type_id(current) == LMD_TYPE_UNDEFINED) {
        return false;
    }
    if (current.item == dest.item) {
        js_property_set(dest, make_string_item("size"), make_js_undefined());
        js_property_set(state, make_string_item("awaitDrainWriters"), ItemNull);
        return true;
    }
    Item writers = js_property_get(current, make_string_item("__writers__"));
    if (get_type_id(current) != LMD_TYPE_MAP || get_type_id(writers) != LMD_TYPE_ARRAY) {
        return false;
    }
    Item next = js_array_new(0);
    bool removed = false;
    int64_t len = js_array_length(writers);
    for (int64_t i = 0; i < len; i++) {
        Item writer = js_array_get_int(writers, i);
        if (writer.item == dest.item) {
            removed = true;
            js_property_set(writer, make_string_item("size"), make_js_undefined());
        } else {
            js_array_push(next, writer);
        }
    }
    if (!removed) return false;
    int64_t next_len = js_array_length(next);
    if (next_len == 0) {
        js_property_set(current, make_string_item("__writers__"), next);
        js_property_set(current, make_string_item("size"), (Item){.item = i2it(0)});
    } else {
        js_property_set(current, make_string_item("__writers__"), next);
        js_property_set(current, make_string_item("size"), (Item){.item = i2it(next_len)});
    }
    return true;
}

static bool js_stream_await_drain_pending(Item source) {
    Item state = js_property_get(source, key_readable_state);
    Item current = js_stream_await_drain_writers(state);
    if (current.item == 0 ||
        get_type_id(current) == LMD_TYPE_NULL ||
        get_type_id(current) == LMD_TYPE_UNDEFINED) {
        return false;
    }
    Item writers = js_property_get(current, make_string_item("__writers__"));
    if (get_type_id(current) == LMD_TYPE_MAP && get_type_id(writers) == LMD_TYPE_ARRAY) {
        return js_array_length(writers) > 0;
    }
    return true;
}

static Item js_readable_pipes(Item self) {
    Item state = js_property_get(self, key_readable_state);
    if (get_type_id(state) != LMD_TYPE_MAP) return ItemNull;
    Item pipes_key = make_string_item("pipes");
    Item pipes = js_property_get(state, pipes_key);
    if (get_type_id(pipes) != LMD_TYPE_ARRAY) {
        pipes = js_array_new(0);
        js_property_set(state, pipes_key, pipes);
    }
    return pipes;
}

static bool js_readable_has_pipe(Item self, Item dest) {
    Item pipes = js_readable_pipes(self);
    if (get_type_id(pipes) != LMD_TYPE_ARRAY) return false;
    int64_t len = js_array_length(pipes);
    for (int64_t i = 0; i < len; i++) {
        if (js_array_get_int(pipes, i).item == dest.item) return true;
    }
    return false;
}

static Item js_legacy_stream_pipe_on_data(Item env_item, Item chunk) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item dest = env[0];
    Item write_fn = js_property_get(dest, key_write);
    if (get_type_id(write_fn) != LMD_TYPE_FUNC) return make_js_undefined();
    return js_call_function(write_fn, dest, &chunk, 1);
}

static Item js_legacy_stream_pipe_on_end(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item dest = env[0];
    Item end_fn = js_property_get(dest, key_end);
    if (get_type_id(end_fn) != LMD_TYPE_FUNC) return make_js_undefined();
    return js_call_function(end_fn, dest, NULL, 0);
}

static void js_legacy_stream_pipe_add_listener(Item source, const char* event_name, Item listener) {
    Item on_fn = js_property_get(source, key_on);
    Item args[2] = { make_string_item(event_name), listener };
    if (get_type_id(on_fn) == LMD_TYPE_FUNC) {
        js_call_function(on_fn, source, args, 2);
    } else {
        js_stream_on(source, args[0], listener);
    }
}

static Item js_legacy_stream_pipe(Item source, Item dest) {
    Item* env = js_alloc_env(1);
    env[0] = dest;
    Item on_data = js_new_closure((void*)js_legacy_stream_pipe_on_data, 1, env, 1);
    Item on_end = js_new_closure((void*)js_legacy_stream_pipe_on_end, 0, env, 1);
    js_legacy_stream_pipe_add_listener(source, "data", on_data);
    js_legacy_stream_pipe_add_listener(source, "end", on_end);

    Item resume_fn = js_property_get(source, make_string_item("resume"));
    if (get_type_id(resume_fn) == LMD_TYPE_FUNC) {
        js_call_function(resume_fn, source, NULL, 0);
    }
    return dest;
}

static void js_readable_emit_unpipe(Item dest, Item source) {
    Item unpipe_event = make_string_item("unpipe");
    Item emit_fn = js_property_get(dest, key_emit);
    if (get_type_id(emit_fn) == LMD_TYPE_FUNC) {
        Item args[2] = {unpipe_event, source};
        js_call_function(emit_fn, dest, args, 2);
    } else {
        js_stream_emit(dest, unpipe_event, source);
    }
}

static void js_readable_remove_one_data_listener(Item self) {
    Item listeners_map = js_property_get(self, make_string_item("_events"));
    if (get_type_id(listeners_map) != LMD_TYPE_MAP) return;
    Item data_key = make_string_item("data");
    Item arr = js_property_get(listeners_map, data_key);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) return;
    int64_t len = js_array_length(arr);
    if (len <= 0) return;
    Item next = js_array_new(0);
    for (int64_t i = 1; i < len; i++) {
        js_array_push(next, js_array_get_int(arr, i));
    }
    if (js_array_length(next) == 0) {
        js_property_set(listeners_map, data_key, make_js_undefined());
    } else {
        js_property_set(listeners_map, data_key, next);
    }
}

static void js_readable_add_pipe_data_event(Item self) {
    Item events = js_property_get(self, make_string_item("_events"));
    if (get_type_id(events) != LMD_TYPE_MAP) {
        events = js_new_object();
        js_property_set(self, make_string_item("_events"), events);
    }
    Item data_key = make_string_item("data");
    Item arr = js_property_get(events, data_key);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) {
        arr = js_array_new(0);
        js_property_set(events, data_key, arr);
    }
    js_array_push(arr, js_new_function((void*)js_stream_pipe_data_noop, 1));
}

static bool js_readable_remove_pipe(Item self, Item dest, bool emit_unpipe) {
    Item pipes = js_readable_pipes(self);
    if (get_type_id(pipes) != LMD_TYPE_ARRAY) return false;
    Item next = js_array_new(0);
    Item removed_items = js_array_new(0);
    bool removed = false;
    bool remove_all = dest.item == 0 || get_type_id(dest) == LMD_TYPE_UNDEFINED;
    int64_t len = js_array_length(pipes);
    for (int64_t i = 0; i < len; i++) {
        Item current = js_array_get_int(pipes, i);
        bool matches = false;
        if (remove_all) {
            matches = true;
        } else {
            matches = !removed && current.item == dest.item;
        }
        if (matches) {
            removed = true;
            js_array_push(removed_items, current);
            js_readable_remove_one_data_listener(self);
        } else {
            js_array_push(next, current);
        }
    }
    Item state = js_property_get(self, key_readable_state);
    if (get_type_id(state) == LMD_TYPE_MAP) {
        js_property_set(state, make_string_item("pipes"), next);
    }
    if (removed && js_array_length(next) == 0) {
        if (js_stream_has_event_listeners(self, "data")) {
            js_stream_set_flowing(self, true);
            js_property_set(self, key_paused, js_bool_item(false));
            js_stream_schedule_data_flush(self);
        } else {
            js_stream_set_flowing(self, false);
        }
    }
    Item current_dest = js_property_get(self, make_string_item("__pipe_dest__"));
    if (removed && (remove_all || current_dest.item == dest.item)) {
        js_readable_clear_pipe(self);
    }
    if (emit_unpipe) {
        int64_t removed_len = js_array_length(removed_items);
        for (int64_t i = 0; i < removed_len; i++) {
            js_readable_emit_unpipe(js_array_get_int(removed_items, i), self);
        }
    }
    return removed;
}

static bool js_item_is_true(Item item) {
    return get_type_id(item) == LMD_TYPE_BOOL && it2b(item);
}

static Item js_readable_pipe_on_drain(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item source = env[0];
    Item dest = env[1];
    if (js_item_is_true(js_property_get(source, key_destroyed)) ||
        js_item_is_true(js_property_get(source, key_end_pending)) ||
        js_item_is_true(js_property_get(source, key_end_emitted))) {
        return make_js_undefined();
    }
    Item dest_readable_state = js_property_get(dest, key_readable_state);
    bool dest_readable_ended = get_type_id(dest_readable_state) == LMD_TYPE_MAP &&
        (js_state_get_bool(dest_readable_state, "ended") ||
         js_item_is_true(js_property_get(dest, key_end_pending)) ||
         js_item_is_true(js_property_get(dest, key_end_emitted)));
    if (dest_readable_ended) {
        return make_js_undefined();
    }
    if (!js_readable_has_pipe(source, dest)) return make_js_undefined();
    js_stream_await_drain_remove(source, dest);
    if (js_stream_await_drain_pending(source)) return make_js_undefined();
    js_stream_set_flowing(source, true);
    js_property_set(source, key_paused, js_bool_item(false));
    js_stream_schedule_data_flush(source);
    js_stream_schedule_read(source);
    return make_js_undefined();
}

static bool js_stream_string_equals(Item item, const char* literal) {
    if (get_type_id(item) != LMD_TYPE_STRING || !literal) return false;
    String* str = it2s(item);
    size_t len = strlen(literal);
    return str->len == len && memcmp(str->chars, literal, len) == 0;
}

static bool js_stream_is_listener_record(Item value) {
    if (get_type_id(value) != LMD_TYPE_MAP) return false;
    Item fn = js_property_get(value, key_listener_fn);
    return get_type_id(fn) == LMD_TYPE_FUNC;
}

static Item js_stream_listener_fn(Item value) {
    if (js_stream_is_listener_record(value)) return js_property_get(value, key_listener_fn);
    return value;
}

static Item js_stream_listener_context(Item value) {
    if (js_stream_is_listener_record(value)) return js_property_get(value, key_listener_context);
    return ItemNull;
}

static Item js_stream_make_listener_record(Item listener) {
    Item record = js_new_object();
    js_property_set(record, key_listener_fn, listener);
    js_property_set(record, key_listener_context, js_als_capture_context());
    return record;
}

static Item js_stream_pipe_data_noop(Item chunk) {
    (void)chunk;
    return make_js_undefined();
}

static bool js_stream_listener_matches(Item stored, Item listener) {
    if (stored.item == listener.item) return true;
    Item fn = js_stream_listener_fn(stored);
    return fn.item == listener.item;
}

static void js_state_set_bool(Item state, const char* name, bool value) {
    if (get_type_id(state) != LMD_TYPE_MAP) return;
    js_property_set(state, make_string_item(name), js_bool_item(value));
}

static void js_state_set_item(Item state, const char* name, Item value) {
    if (get_type_id(state) != LMD_TYPE_MAP) return;
    js_property_set(state, make_string_item(name), value);
}

static void js_stream_set_flowing(Item self, bool flowing) {
    js_property_set(self, key_flowing, js_bool_item(flowing));
    js_state_set_bool(js_property_get(self, key_readable_state), "flowing", flowing);
}

static void js_stream_set_readable_buffer(Item self, Item buffer) {
    js_property_set(self, key_buffer, buffer);
    js_property_set(self, make_string_item("readableBuffer"), buffer);
    js_state_set_item(js_property_get(self, key_readable_state), "length",
                      (Item){.item = i2it(js_stream_readable_buffer_length(self, buffer))});
}

static int64_t js_stream_readable_cached_length(Item self, Item buf) {
    Item state = js_property_get(self, key_readable_state);
    if (get_type_id(state) == LMD_TYPE_MAP) {
        Item length = js_property_get(state, make_string_item("length"));
        if (get_type_id(length) == LMD_TYPE_INT) return it2i(length);
    }
    return js_stream_readable_buffer_length(self, buf);
}

static void js_stream_adjust_readable_length(Item self, int64_t delta) {
    Item state = js_property_get(self, key_readable_state);
    if (get_type_id(state) != LMD_TYPE_MAP) return;
    Item current_item = js_property_get(state, make_string_item("length"));
    int64_t current = get_type_id(current_item) == LMD_TYPE_INT ? it2i(current_item) : 0;
    int64_t next = current + delta;
    if (next < 0) next = 0;
    js_state_set_item(state, "length", (Item){.item = i2it(next)});
}

static void js_stream_append_readable_chunk(Item self, Item buf, Item chunk) {
    js_array_push(buf, chunk);
    // append-heavy sync _read() paths rely on length staying O(1), not rescanning the buffer.
    js_stream_adjust_readable_length(self, js_stream_readable_chunk_length(self, chunk));
}

static bool js_state_get_bool(Item state, const char* name) {
    if (get_type_id(state) != LMD_TYPE_MAP) return false;
    return js_item_is_true(js_property_get(state, make_string_item(name)));
}

static Item js_writable_state_getBuffer(void);

static Item js_create_readable_state(void) {
    Item state = js_new_object();
    js_state_set_bool(state, "ended", false);
    js_state_set_bool(state, "endEmitted", false);
    js_state_set_bool(state, "errorEmitted", false);
    js_state_set_bool(state, "objectMode", false);
    js_state_set_bool(state, "readableListening", false);
    js_state_set_bool(state, "needReadable", false);
    js_state_set_bool(state, "flowing", false);
    js_state_set_bool(state, "emittedReadable", false);
    js_state_set_bool(state, "resumeScheduled", false);
    js_state_set_bool(state, "reading", false);
    js_state_set_bool(state, "readingMore", false);
    js_state_set_bool(state, "didRead", false);
    js_state_set_item(state, "errored", ItemNull);
    js_state_set_item(state, "awaitDrainWriters", ItemNull);
    js_state_set_item(state, "pipes", js_array_new(0));
    js_state_set_item(state, "length", (Item){.item = i2it(0)});
    js_state_set_item(state, "highWaterMark", (Item){.item = i2it(js_stream_default_byte_hwm)});
    js_property_set(state, make_string_item("encoding"), make_string_item("utf8"));
    return state;
}

static Item js_create_writable_state(Item owner) {
    Item state = js_new_object();
    js_state_set_bool(state, "ending", false);
    js_state_set_bool(state, "ended", false);
    js_state_set_bool(state, "finished", false);
    js_state_set_bool(state, "errorEmitted", false);
    js_state_set_bool(state, "objectMode", false);
    js_state_set_item(state, "errored", ItemNull);
    js_state_set_item(state, "corked", (Item){.item = i2it(0)});
    js_state_set_item(state, "bufferedRequestCount", (Item){.item = i2it(0)});
    js_state_set_item(state, "pendingcb", (Item){.item = i2it(0)});
    js_state_set_item(state, "length", (Item){.item = i2it(0)});
    js_state_set_bool(state, "needDrain", false);
    js_state_set_item(state, "highWaterMark", (Item){.item = i2it(js_stream_default_byte_hwm)});
    js_property_set(state, make_string_item("__stream__"), owner);
    Item get_buffer = js_new_function((void*)js_writable_state_getBuffer, 0);
    js_property_set(state, make_string_item("getBuffer"), get_buffer);
    js_mark_non_enumerable(state, make_string_item("__stream__"));
    js_mark_non_enumerable(state, make_string_item("getBuffer"));
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

static int64_t js_stream_pending_writes_count(Item self) {
    Item pending = js_property_get(self, make_string_item("_pendingWrites"));
    return get_type_id(pending) == LMD_TYPE_ARRAY ? js_array_length(pending) : 0;
}

static Item js_writable_state_getBuffer(void) {
    ensure_keys();
    Item state = js_get_this();
    Item self = js_property_get(state, make_string_item("__stream__"));
    Item pending = js_property_get(self, make_string_item("_pendingWrites"));
    if (get_type_id(pending) == LMD_TYPE_ARRAY) return pending;
    return js_array_new(0);
}

static int64_t js_stream_state_get_int(Item state, const char* name, int64_t fallback) {
    if (get_type_id(state) != LMD_TYPE_MAP) return fallback;
    Item value = js_property_get(state, make_string_item(name));
    if (get_type_id(value) == LMD_TYPE_INT) return it2i(value);
    return fallback;
}

static bool js_stream_writable_state_has_pendingcb(Item state) {
    if (get_type_id(state) != LMD_TYPE_MAP) return false;
    Item value = js_property_get(state, make_string_item("pendingcb"));
    return value.item != 0 && get_type_id(value) != LMD_TYPE_UNDEFINED;
}

static void js_stream_set_writable_pendingcb(Item self, int64_t count) {
    if (count < 0) count = 0;
    Item state = js_property_get(self, key_writable_state);
    if (get_type_id(state) != LMD_TYPE_MAP) return;
    js_state_set_item(state, "pendingcb", (Item){.item = i2it(count)});
}

static void js_stream_adjust_writable_pendingcb(Item self, int64_t delta) {
    Item state = js_property_get(self, key_writable_state);
    if (get_type_id(state) != LMD_TYPE_MAP) return;
    int64_t current = js_stream_state_get_int(state, "pendingcb", 0);
    js_stream_set_writable_pendingcb(self, current + delta);
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
    bool need_drain = next > 0 && next >= hwm;
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

static bool js_stream_readable_side_enabled(Item self) {
    Item enabled = js_property_get(self, key_readable_side_enabled);
    if (get_type_id(enabled) == LMD_TYPE_BOOL) return it2b(enabled);
    Item readable = js_property_get(self, key_readable);
    if (get_type_id(readable) == LMD_TYPE_BOOL) return it2b(readable);
    return get_type_id(js_property_get(self, key_readable_state)) == LMD_TYPE_MAP;
}

static bool js_stream_writable_side_enabled(Item self) {
    Item enabled = js_property_get(self, key_writable_side_enabled);
    if (get_type_id(enabled) == LMD_TYPE_BOOL) return it2b(enabled);
    Item writable = js_property_get(self, key_writable);
    if (get_type_id(writable) == LMD_TYPE_BOOL) return it2b(writable);
    return get_type_id(js_property_get(self, key_writable_state)) == LMD_TYPE_MAP;
}

static void js_stream_set_readable_side_enabled(Item self, bool enabled) {
    js_property_set(self, key_readable_side_enabled, js_bool_item(enabled));
    js_stream_set_readable_open(self, enabled);
}

static void js_stream_set_writable_side_enabled(Item self, bool enabled) {
    js_property_set(self, key_writable_side_enabled, js_bool_item(enabled));
    js_stream_set_writable_open(self, enabled);
}

static bool js_stream_destroy_pending(Item self) {
    return js_item_is_true(js_property_get(self, key_destroy_pending));
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

static bool js_stream_error_value_present(Item err) {
    return err.item != 0 && get_type_id(err) != LMD_TYPE_UNDEFINED &&
           get_type_id(err) != LMD_TYPE_NULL;
}

static bool js_stream_stored_error_value_present(Item err) {
    return js_stream_error_value_present(err) && get_type_id(err) != LMD_TYPE_BOOL;
}

static Item js_stream_get_stored_error(Item self) {
    Item err = js_property_get(self, make_string_item("errored"));
    if (js_stream_stored_error_value_present(err)) return err;

    Item readable_state = js_property_get(self, key_readable_state);
    err = js_property_get(readable_state, make_string_item("errored"));
    if (js_stream_stored_error_value_present(err)) return err;

    Item writable_state = js_property_get(self, key_writable_state);
    err = js_property_get(writable_state, make_string_item("errored"));
    if (js_stream_stored_error_value_present(err)) return err;

    err = js_property_get(self, make_string_item("__error__"));
    if (js_stream_stored_error_value_present(err)) return err;

    return make_js_undefined();
}

static bool js_stream_has_stored_error(Item self) {
    return js_stream_stored_error_value_present(js_stream_get_stored_error(self));
}

static bool js_stream_is_finished_for_destroy_export(Item self) {
    if (js_item_is_true(js_property_get(self, key_destroyed))) return true;

    Item readable = js_property_get(self, key_readable);
    if (get_type_id(readable) == LMD_TYPE_BOOL && it2b(readable) &&
        !js_item_is_true(js_property_get(self, key_end_emitted))) {
        return false;
    }

    Item writable = js_property_get(self, key_writable);
    if (get_type_id(writable) == LMD_TYPE_BOOL && it2b(writable) &&
        !js_item_is_true(js_property_get(self, key_finish_emitted))) {
        return false;
    }

    return true;
}

static bool js_stream_has_readable_side(Item self) {
    return get_type_id(js_property_get(self, key_readable_state)) == LMD_TYPE_MAP;
}

static bool js_stream_has_writable_side(Item self) {
    return get_type_id(js_property_get(self, key_writable_state)) == LMD_TYPE_MAP;
}

static bool js_stream_can_auto_destroy_after_readable_end(Item self) {
    if (!js_item_is_true(js_property_get(self, key_auto_destroy)) ||
        js_item_is_true(js_property_get(self, key_destroyed))) {
        return false;
    }
    return !js_stream_has_writable_side(self) ||
           js_item_is_true(js_property_get(self, key_finish_emitted));
}

static bool js_stream_can_auto_destroy_after_writable_finish(Item self) {
    if (!js_item_is_true(js_property_get(self, key_auto_destroy)) ||
        js_item_is_true(js_property_get(self, key_destroyed))) {
        return false;
    }
    return !js_stream_has_readable_side(self) ||
           js_item_is_true(js_property_get(self, key_end_emitted));
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
    js_create_data_property(self, make_string_item("readableEnded"), js_bool_item(true));
    js_stream_set_readable_open(self, false);
    js_property_set(self, make_string_item("readableAborted"), js_bool_item(false));
}

static Item js_stream_end_writable_side_tick(Item self) {
    ensure_keys();
    Item writable_state = js_property_get(self, key_writable_state);
    if (!js_item_is_true(js_property_get(self, key_writable)) ||
        js_state_get_bool(writable_state, "ended") ||
        js_item_is_true(js_property_get(self, key_finish_emitted))) {
        return make_js_undefined();
    }
    return js_writable_end(self, make_js_undefined(), make_js_undefined());
}

static Item js_stream_end_writable_side_tick_closure(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    return js_stream_end_writable_side_tick(env[0]);
}

static void js_stream_maybe_end_writable_after_readable_end(Item self) {
    ensure_keys();
    Item allow_half_open = js_property_get(self, make_string_item("allowHalfOpen"));
    if (get_type_id(allow_half_open) != LMD_TYPE_BOOL || it2b(allow_half_open)) return;
    if (!js_item_is_true(js_property_get(self, key_writable))) return;
    Item writable_state = js_property_get(self, key_writable_state);
    if (get_type_id(writable_state) != LMD_TYPE_MAP ||
        js_state_get_bool(writable_state, "ended") ||
        js_item_is_true(js_property_get(self, key_finish_emitted))) {
        return;
    }
    Item* env = js_alloc_env(1);
    env[0] = self;
    Item tick = js_new_closure((void*)js_stream_end_writable_side_tick_closure, 0, env, 1);
    js_next_tick_enqueue(tick);
}

static void js_stream_mark_writable_ended(Item self) {
    Item state = js_property_get(self, key_writable_state);
    js_state_set_bool(state, "ending", true);
    js_state_set_bool(state, "ended", true);
    js_create_data_property(self, make_string_item("writableEnded"), js_bool_item(true));
    js_stream_set_writable_open(self, false);
}

static void js_stream_mark_writable_finished(Item self) {
    Item state = js_property_get(self, key_writable_state);
    js_state_set_bool(state, "finished", true);
    js_property_set(self, key_finished, js_bool_item(true));
    js_create_data_property(self, make_string_item("writableFinished"), js_bool_item(true));
}

// =============================================================================
// EventEmitter-like helpers for stream objects
// =============================================================================

static void js_stream_schedule_error(Item self, Item err);
static bool js_stream_schedule_error_once(Item self, Item err);
static void js_stream_schedule_callback_error(Item callback, Item err);
static void js_stream_flush_pending_writes(Item self);
static void js_stream_call_writable_end_callbacks(Item self, Item err);
static Item js_stream_after_destroy(Item self, Item err);
static Item js_stream_make_error_with_code(const char* code, const char* message);
static void js_stream_auto_destroy_after_terminal(Item self);
static void js_stream_auto_destroy_after_error_emit(Item self, Item err);

static Item js_stream_construct_callback_once(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item self = env[0];
    if (js_item_is_true(env[1])) {
        Item multi = js_stream_make_error_with_code("ERR_MULTIPLE_CALLBACK",
            "Callback called multiple times");
        js_stream_schedule_error(self, multi);
        return make_js_undefined();
    }
    env[1] = js_bool_item(true);
    js_property_set(self, make_string_item("__constructing__"), js_bool_item(false));
    js_property_set(self, make_string_item("__constructed__"), js_bool_item(true));
    if (js_stream_error_value_present(err)) {
        js_stream_schedule_error(self, err);
    }
    return make_js_undefined();
}

static void js_stream_call_construct(Item obj) {
    Item construct_fn = js_property_get(obj, make_string_item("_construct"));
    if (get_type_id(construct_fn) != LMD_TYPE_FUNC) {
        js_property_set(obj, make_string_item("__constructing__"), js_bool_item(false));
        js_property_set(obj, make_string_item("__constructed__"), js_bool_item(true));
        return;
    }

    js_property_set(obj, make_string_item("__constructing__"), js_bool_item(true));
    js_property_set(obj, make_string_item("__constructed__"), js_bool_item(false));
    Item* env = js_alloc_env(2);
    env[0] = obj;
    env[1] = js_bool_item(false);
    Item callback = js_new_closure((void*)js_stream_construct_callback_once, 1, env, 2);
    js_call_function(construct_fn, obj, &callback, 1);
    if (js_check_exception()) {
        Item err = js_clear_exception();
        js_stream_construct_callback_once((Item){.item = (uint64_t)(uintptr_t)env}, err);
    }
}

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
            js_stream_auto_destroy_after_error_emit(self, args[0]);
            js_process_emit(make_string_item("uncaughtException"), args[0]);
        }
        return;
    }
    Item event_key = make_string_item(event);
    Item arr = js_property_get(listeners_map, event_key);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) {
        if (event && strcmp(event, "error") == 0 && args && argc > 0) {
            js_stream_auto_destroy_after_error_emit(self, args[0]);
            js_process_emit(make_string_item("uncaughtException"), args[0]);
        }
        return;
    }
    int64_t len = js_array_length(arr);
    if (len == 0 && event && strcmp(event, "error") == 0 && args && argc > 0) {
        js_stream_auto_destroy_after_error_emit(self, args[0]);
        js_process_emit(make_string_item("uncaughtException"), args[0]);
        return;
    }
    for (int64_t i = 0; i < len; i++) {
        Item entry = js_array_get_int(arr, i);
        Item listener = js_stream_listener_fn(entry);
        Item context = js_stream_listener_context(entry);
        if (get_type_id(listener) == LMD_TYPE_FUNC) {
            Item result;
            if (argc == 0) {
                result = js_als_context_call(context, listener, self, ItemNull, 0);
            } else if (argc == 1) {
                result = js_als_context_call(context, listener, self, args[0], 1);
            } else {
                result = js_call_function(listener, self, args, argc);
            }
            js_stream_maybe_capture_rejection(self, event, result);
        }
    }
    if (event && strcmp(event, "error") == 0 && args && argc > 0) {
        js_stream_auto_destroy_after_error_emit(self, args[0]);
    }
}

static bool js_stream_has_event_listeners(Item self, const char* event) {
    Item listeners_map = js_property_get(self, key_listeners);
    if (get_type_id(listeners_map) != LMD_TYPE_MAP) return false;
    Item arr = js_property_get(listeners_map, make_string_item(event));
    return get_type_id(arr) == LMD_TYPE_ARRAY && js_array_length(arr) > 0;
}

static Item js_stream_emit_drain_tick(Item self) {
    ensure_keys();
    if (js_item_is_true(js_property_get(self, key_destroyed)) ||
        js_item_is_true(js_property_get(self, key_finish_emitted))) {
        return make_js_undefined();
    }
    stream_emit(self, "drain", NULL, 0);
    return make_js_undefined();
}

static Item js_stream_emit_drain_tick_closure(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    return js_stream_emit_drain_tick(env[0]);
}

static Item js_stream_transform_deferred_drain_key(void) {
    return make_string_item("__transform_deferred_drain__");
}

static Item js_stream_transform_deferred_drain_tick(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item self = env[0];
    Item pending_key = js_stream_transform_deferred_drain_key();
    if (!js_item_is_true(js_property_get(self, pending_key))) {
        return make_js_undefined();
    }
    js_property_set(self, pending_key, js_bool_item(false));
    Item state = js_property_get(self, key_writable_state);
    if (!js_state_get_bool(state, "needDrain")) return make_js_undefined();
    js_state_set_bool(state, "needDrain", false);
    js_stream_emit_or_schedule_drain(self);
    return make_js_undefined();
}

static void js_stream_defer_transform_drain(Item self) {
    Item pending_key = js_stream_transform_deferred_drain_key();
    if (js_item_is_true(js_property_get(self, pending_key))) return;
    js_property_set(self, pending_key, js_bool_item(true));
    Item* env = js_alloc_env(1);
    env[0] = self;
    Item tick = js_new_closure((void*)js_stream_transform_deferred_drain_tick, 0, env, 1);
    js_next_tick_enqueue(tick);
}

static Item js_stream_drain_on_listener_key(void) {
    return make_string_item("__drain_on_listener__");
}

extern "C" void js_stream_transform_flush_drained(Item self) {
    ensure_keys();
    Item state = js_property_get(self, key_writable_state);
    Item deferred_key = js_stream_transform_deferred_drain_key();
    bool need_drain = js_state_get_bool(state, "needDrain");
    bool deferred_drain = js_item_is_true(js_property_get(self, deferred_key));
    if (!need_drain && !deferred_drain) return;
    js_state_set_bool(state, "needDrain", false);
    js_property_set(self, deferred_key, js_bool_item(false));
    if (!js_stream_has_event_listeners(self, "drain")) {
        js_property_set(self, js_stream_drain_on_listener_key(), js_bool_item(true));
        return;
    }
    Item* env = js_alloc_env(1);
    env[0] = self;
    Item tick = js_new_closure((void*)js_stream_emit_drain_tick_closure, 0, env, 1);
    js_next_tick_enqueue(tick);
}

static void js_stream_emit_or_schedule_drain(Item self) {
    Item state = js_property_get(self, key_readable_state);
    bool readable_pending = js_state_get_bool(state, "readableListening") &&
                            js_state_get_bool(state, "emittedReadable");
    if (readable_pending) {
        js_property_set(self, make_string_item("__pending_drain_after_readable__"),
                        js_bool_item(true));
        return;
    }
    stream_emit(self, "drain", NULL, 0);
}

static void js_stream_schedule_pending_drain_after_readable(Item self) {
    Item key = make_string_item("__pending_drain_after_readable__");
    if (!js_item_is_true(js_property_get(self, key))) return;
    js_property_set(self, key, js_bool_item(false));
    Item* env = js_alloc_env(1);
    env[0] = self;
    Item tick = js_new_closure((void*)js_stream_emit_drain_tick_closure, 0, env, 1);
    js_setImmediate(tick);
}

static Item js_stream_emit_readable_tick(Item self) {
    ensure_keys();
    Item state = js_property_get(self, key_readable_state);
    if (js_item_is_true(js_property_get(self, key_destroyed)) ||
        js_item_is_true(js_property_get(self, key_flowing))) {
        js_state_set_bool(state, "emittedReadable", false);
        return make_js_undefined();
    }
    if (!js_stream_has_event_listeners(self, "readable")) {
        js_state_set_bool(state, "emittedReadable", false);
        return make_js_undefined();
    }
    Item buf = js_property_get(self, key_buffer);
    bool has_buffered = get_type_id(buf) == LMD_TYPE_ARRAY && js_array_length(buf) > 0;
    if (!has_buffered &&
        (js_item_is_true(js_property_get(self, key_end_pending)) ||
         js_item_is_true(js_property_get(self, key_end_emitted)))) {
        js_state_set_bool(state, "emittedReadable", false);
        return make_js_undefined();
    }
    stream_emit(self, "readable", NULL, 0);
    js_stream_schedule_pending_drain_after_readable(self);
    return make_js_undefined();
}

static Item js_stream_emit_readable_tick_closure(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    return js_stream_emit_readable_tick(env[0]);
}

static void js_stream_emit_readable(Item self) {
    Item state = js_property_get(self, key_readable_state);
    js_state_set_bool(state, "needReadable", false);
    if (js_item_is_true(js_property_get(self, key_flowing))) {
        js_state_set_bool(state, "emittedReadable", false);
        return;
    }
    if (js_state_get_bool(state, "emittedReadable")) return;
    js_state_set_bool(state, "emittedReadable", true);
    if (js_item_is_true(js_property_get(self, make_string_item("__defer_readable_emit__"))) ||
        js_item_is_true(js_property_get(self, key_reading_sync))) {
        // sync _read() may push before read() unwinds; defer readable to avoid recursive user callbacks.
        Item* env = js_alloc_env(1);
        env[0] = self;
        Item tick = js_new_closure((void*)js_stream_emit_readable_tick_closure, 0, env, 1);
        js_next_tick_enqueue(tick);
        return;
    }
    stream_emit(self, "readable", NULL, 0);
}

static void js_stream_mark_readable_needed(Item self, bool needed) {
    Item state = js_property_get(self, key_readable_state);
    js_state_set_bool(state, "needReadable", needed);
}

static void js_stream_mark_readable_did_read(Item self) {
    js_state_set_bool(js_property_get(self, key_readable_state), "didRead", true);
}

static void js_stream_set_reading(Item self, bool reading) {
    js_property_set(self, key_reading, js_bool_item(reading));
    js_state_set_bool(js_property_get(self, key_readable_state), "reading", reading);
}

static bool js_stream_is_empty_byte_chunk(Item chunk) {
    if (get_type_id(chunk) == LMD_TYPE_STRING) {
        String* str = it2s(chunk);
        return !str || str->len == 0;
    }
    if (!js_stream_chunk_is_arraybuffer_view(chunk)) return false;
    Item byte_length = js_property_get(chunk, make_string_item("byteLength"));
    if (get_type_id(byte_length) == LMD_TYPE_INT) return it2i(byte_length) == 0;
    Item length = js_property_get(chunk, make_string_item("length"));
    return get_type_id(length) == LMD_TYPE_INT && it2i(length) == 0;
}

static Item js_stream_maybe_emit_manual_data(Item self, Item chunk) {
    bool async_iterator_reading =
        js_item_is_true(js_property_get(self, make_string_item("__async_iterator_reading__")));
    if ((!js_item_is_true(js_property_get(self, key_flowing)) || async_iterator_reading) &&
        js_stream_has_event_listeners(self, "data")) {
        // async iterators can consume a pushed chunk before the flowing data
        // flush; mirror that read so existing data listeners do not miss it.
        js_property_set(self, make_string_item("__emitting_data__"), js_bool_item(true));
        stream_emit(self, "data", &chunk, 1);
        js_property_set(self, make_string_item("__emitting_data__"), js_bool_item(false));
    }
    return chunk;
}

static void js_stream_update_need_after_read(Item self) {
    if (js_item_is_true(js_property_get(self, key_end_pending)) ||
        js_item_is_true(js_property_get(self, key_end_emitted)) ||
        js_item_is_true(js_property_get(self, key_flowing))) {
        js_stream_mark_readable_needed(self, false);
        return;
    }
    Item buf = js_property_get(self, key_buffer);
    bool empty = get_type_id(buf) != LMD_TYPE_ARRAY || js_array_length(buf) == 0;
    js_stream_mark_readable_needed(self, empty);
}

static Item js_stream_decode_object_readable_chunk(Item self, Item chunk) {
    if (!js_stream_readable_is_object_mode(self)) return chunk;
    Item encoding = js_property_get(self, make_string_item("_encoding"));
    if (get_type_id(encoding) != LMD_TYPE_STRING) return chunk;
    if (!js_stream_chunk_is_arraybuffer_view(chunk)) return chunk;
    return js_buffer_toString(chunk, encoding, make_js_undefined(), make_js_undefined());
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
    js_stream_adjust_writable_pendingcb(self, -1);
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
    if (has_error) {
        js_property_set(self, make_string_item("__writable_end_pending__"), js_bool_item(false));
        js_stream_call_writable_end_callbacks(self, err);
    }

    if (!has_error && need_drain &&
        !js_state_get_bool(state, "ended") &&
        !js_item_is_true(js_property_get(self, make_string_item("__transform_end_pending__"))) &&
        !js_item_is_true(js_property_get(self, key_destroyed)) &&
        !js_item_is_true(js_property_get(self, key_finish_emitted))) {
        js_stream_emit_or_schedule_drain(self);
    }
    if (!has_error) {
        js_stream_flush_pending_writes(self);
        js_writable_maybe_finish_deferred(self);
        js_transform_maybe_finish_deferred(self);
    }
    return make_js_undefined();
}

static Item js_stream_write_callback_once(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item self = env[0];
    if (js_item_is_true(env[2])) {
        Item multi = js_stream_make_error_with_code("ERR_MULTIPLE_CALLBACK",
            "Callback called multiple times");
        js_stream_schedule_error(self, multi);
        return make_js_undefined();
    }
    env[2] = js_bool_item(true);
    return js_stream_after_write(self, env[1], err);
}

static Item js_stream_make_write_callback(Item self, Item callback) {
    js_stream_adjust_writable_pendingcb(self, 1);
    Item* env = js_alloc_env(3);
    env[0] = self;
    env[1] = callback;
    env[2] = js_bool_item(false);
    return js_new_closure((void*)js_stream_write_callback_once, 1, env, 3);
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

static Item js_stream_transform_write_callback_once(Item env_item, Item err, Item data) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item self = env[0];
    if (js_item_is_true(env[2])) {
        Item multi = js_stream_make_error_with_code("ERR_MULTIPLE_CALLBACK",
            "Callback called multiple times");
        js_stream_schedule_error(self, multi);
        return make_js_undefined();
    }
    env[2] = js_bool_item(true);
    return js_stream_after_transform_write(self, env[1], err, data);
}

static Item js_stream_make_transform_write_callback(Item self, Item callback) {
    js_stream_adjust_writable_pendingcb(self, 1);
    Item* env = js_alloc_env(3);
    env[0] = self;
    env[1] = callback;
    env[2] = js_bool_item(false);
    return js_new_closure((void*)js_stream_transform_write_callback_once, 2, env, 3);
}

static Item js_stream_after_writev(Item self, Item pending, Item err) {
    ensure_keys();
    Item state = js_property_get(self, key_writable_state);
    bool need_drain = js_state_get_bool(state, "needDrain");
    bool has_error = js_stream_has_callback_error(err);
    js_property_set(self, make_string_item("_writing"), js_bool_item(false));
    js_stream_adjust_writable_pendingcb(self, -1);
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
    if (has_error) {
        js_property_set(self, make_string_item("__writable_end_pending__"), js_bool_item(false));
        js_stream_call_writable_end_callbacks(self, err);
    }

    if (!has_error && need_drain &&
        !js_state_get_bool(state, "ended") &&
        !js_item_is_true(js_property_get(self, make_string_item("__transform_end_pending__"))) &&
        !js_item_is_true(js_property_get(self, key_destroyed)) &&
        !js_item_is_true(js_property_get(self, key_finish_emitted))) {
        js_stream_emit_or_schedule_drain(self);
    }
    if (!has_error) {
        js_stream_flush_pending_writes(self);
        js_writable_maybe_finish_deferred(self);
        js_transform_maybe_finish_deferred(self);
    }
    return make_js_undefined();
}

static Item js_stream_writev_callback_once(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item self = env[0];
    if (js_item_is_true(env[2])) {
        Item multi = js_stream_make_error_with_code("ERR_MULTIPLE_CALLBACK",
            "Callback called multiple times");
        js_stream_schedule_error(self, multi);
        return make_js_undefined();
    }
    env[2] = js_bool_item(true);
    return js_stream_after_writev(self, env[1], err);
}

static Item js_stream_make_writev_callback(Item self, Item pending) {
    js_stream_adjust_writable_pendingcb(self, 1);
    Item* env = js_alloc_env(3);
    env[0] = self;
    env[1] = pending;
    env[2] = js_bool_item(false);
    return js_new_closure((void*)js_stream_writev_callback_once, 1, env, 3);
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
    Item transform_fn = js_property_get(self, make_string_item("_transform"));
    if (get_type_id(write_handler) != LMD_TYPE_FUNC &&
        get_type_id(transform_fn) == LMD_TYPE_FUNC) {
        Item request = js_array_get_int(pending, 0);
        Item chunk = js_property_get(request, make_string_item("chunk"));
        Item encoding = js_property_get(request, make_string_item("encoding"));
        Item callback = js_property_get(request, make_string_item("callback"));
        Item next_pending = js_array_new(0);
        for (int64_t i = 1; i < plen; i++) {
            js_array_push(next_pending, js_array_get_int(pending, i));
        }
        js_property_set(self, make_string_item("_pendingWrites"), next_pending);
        js_stream_set_buffered_request_count(self, js_array_length(next_pending));
        Item write_cb = js_stream_make_transform_write_callback(self, callback);
        Item args[3] = {chunk, encoding, write_cb};
        js_property_set(self, make_string_item("_writing"), js_bool_item(true));
        js_call_function(transform_fn, self, args, 3);
        if (js_check_exception()) {
            Item err = js_clear_exception();
            js_stream_after_write(self, callback, err);
        }
        return;
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
    js_stream_maybe_end_writable_after_readable_end(self);
    js_stream_async_iterators_drain(self, make_js_undefined());
    if (js_stream_can_auto_destroy_after_readable_end(self)) {
        js_stream_auto_destroy_after_terminal(self);
    }
    return make_js_undefined();
}

static Item js_stream_emit_close_tick(Item self) {
    ensure_keys();
    if (js_item_is_true(js_property_get(self, key_close_emitted))) {
        return make_js_undefined();
    }
    js_property_set(self, key_close_emitted, js_bool_item(true));
    js_property_set(self, key_closed, js_bool_item(true));
    stream_emit(self, "close", NULL, 0);
    return make_js_undefined();
}

static Item js_stream_emit_end_tick_closure(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    return js_stream_emit_end_tick(env[0]);
}

static Item js_stream_emit_close_tick_closure(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    return js_stream_emit_close_tick(env[0]);
}

static void js_stream_schedule_close(Item self) {
    Item* env = js_alloc_env(1);
    env[0] = self;
    Item tick = js_new_closure((void*)js_stream_emit_close_tick_closure, 0, env, 1);
    js_next_tick_enqueue(tick);
}

static void js_stream_schedule_end(Item self) {
    if (js_item_is_true(js_property_get(self, key_end_emitted))) return;
    Item* env = js_alloc_env(1);
    env[0] = self;
    Item tick = js_new_closure((void*)js_stream_emit_end_tick_closure, 0, env, 1);
    js_next_tick_enqueue(tick);
}

static void js_stream_schedule_end_immediate(Item self) {
    if (js_item_is_true(js_property_get(self, key_end_emitted))) return;
    Item* env = js_alloc_env(1);
    env[0] = self;
    Item tick = js_new_closure((void*)js_stream_emit_end_tick_closure, 0, env, 1);
    js_setTimeout(tick, (Item){.item = i2it(0)});
}

static Item js_stream_emit_error_tick(Item self, Item err) {
    ensure_keys();
    js_stream_set_error_emitted(self, true);
    stream_emit(self, "error", &err, 1);
    return make_js_undefined();
}

static Item js_stream_emit_error_tick_closure(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    return js_stream_emit_error_tick(env[0], env[1]);
}

static void js_stream_schedule_error(Item self, Item err) {
    js_stream_set_error_state(self, err);
    js_property_set(self, make_string_item("__error__"), err);
    js_stream_async_iterators_drain(self, err);
    Item* env = js_alloc_env(2);
    env[0] = self;
    env[1] = err;
    Item tick = js_new_closure((void*)js_stream_emit_error_tick_closure, 0, env, 2);
    js_next_tick_enqueue(tick);
}

static bool js_stream_schedule_error_once(Item self, Item err) {
    if (js_stream_has_stored_error(self)) return false;
    js_stream_schedule_error(self, err);
    return true;
}

static void js_stream_schedule_error_immediate(Item self, Item err) {
    js_stream_set_error_state(self, err);
    js_property_set(self, make_string_item("__error__"), err);
    js_stream_async_iterators_drain(self, err);
    Item* env = js_alloc_env(2);
    env[0] = self;
    env[1] = err;
    Item tick = js_new_closure((void*)js_stream_emit_error_tick_closure, 0, env, 2);
    // destroy() queues terminal events before user nextTicks registered after
    // destroy; timers let late listeners observe close/error incorrectly.
    js_next_tick_enqueue(tick);
}

static void js_stream_schedule_close_immediate(Item self) {
    Item* env = js_alloc_env(1);
    env[0] = self;
    Item tick = js_new_closure((void*)js_stream_emit_close_tick_closure, 0, env, 1);
    // destroy() close is a terminal nextTick, not a timer, so late nextTick
    // listeners attached after destroy() do not see stale close events.
    js_next_tick_enqueue(tick);
}

static void js_stream_invoke_destroy_callback(Item self, Item err) {
    Item callback_key = make_string_item("__destroy_callback__");
    Item callback = js_property_get(self, callback_key);
    if (get_type_id(callback) != LMD_TYPE_FUNC) return;
    js_property_set(self, callback_key, make_js_undefined());
    if (js_stream_has_callback_error(err)) {
        js_call_function(callback, self, &err, 1);
    } else {
        js_call_function(callback, self, NULL, 0);
    }
}

static Item js_stream_after_destroy(Item self, Item err) {
    ensure_keys();
    if (js_stream_has_callback_error(err)) {
        js_stream_set_error_state(self, err);
        js_property_set(self, make_string_item("__error__"), err);
    }
    if (js_item_is_true(js_property_get(self, make_string_item("__destroying_sync__")))) {
        js_property_set(self, make_string_item("__destroy_cb_done__"), js_bool_item(true));
        js_property_set(self, make_string_item("__destroy_cb_error__"), err);
        return make_js_undefined();
    }
    js_property_set(self, key_destroy_pending, js_bool_item(false));
    if (js_stream_has_callback_error(err)) {
        // Iterators created while _destroy(cb) is pending wait for cb's error;
        // scheduling 'error' alone leaves their next() promises unresolved.
        js_stream_async_iterators_drain(self, err);
        js_stream_schedule_error(self, err);
    } else {
        Item iterators = js_property_get(self, make_string_item("__async_iterators__"));
        if (get_type_id(iterators) == LMD_TYPE_ARRAY && js_array_length(iterators) > 0) {
            Item close_err = js_stream_make_error_with_code("ERR_STREAM_PREMATURE_CLOSE",
                "Premature close");
            js_stream_async_iterators_drain(self, close_err);
        }
    }
    js_stream_invoke_destroy_callback(self, err);
    js_stream_schedule_close(self);
    return make_js_undefined();
}

static void js_stream_auto_destroy_after_terminal(Item self) {
    ensure_keys();
    if (js_item_is_true(js_property_get(self, key_destroyed))) return;

    bool readable_aborted = js_stream_has_readable_side(self) &&
                            !js_item_is_true(js_property_get(self, key_end_emitted));
    bool writable_aborted = js_stream_has_writable_side(self) &&
                            !js_item_is_true(js_property_get(self, key_finish_emitted));
    js_stream_mark_destroyed(self);
    js_property_set(self, make_string_item("readableAborted"), js_bool_item(readable_aborted));
    js_property_set(self, make_string_item("writableAborted"), js_bool_item(writable_aborted));

    Item destroy_fn = js_property_get(self, make_string_item("_destroy"));
    if (get_type_id(destroy_fn) == LMD_TYPE_FUNC) {
        js_property_set(self, key_destroy_pending, js_bool_item(true));
        Item destroy_cb = js_bind_function(js_new_function((void*)js_stream_after_destroy, 2),
                                           make_js_undefined(), &self, 1);
        Item args[2] = { ItemNull, destroy_cb };
        js_call_function(destroy_fn, self, args, 2);
        if (js_check_exception()) {
            Item err = js_clear_exception();
            js_property_set(self, key_destroy_pending, js_bool_item(false));
            js_stream_schedule_error_immediate(self, err);
            js_stream_schedule_close_immediate(self);
        }
        return;
    }

    js_stream_schedule_close_immediate(self);
}

static void js_stream_auto_destroy_after_error_emit(Item self, Item err) {
    ensure_keys();
    if (!js_item_is_true(js_property_get(self, key_auto_destroy)) ||
        js_item_is_true(js_property_get(self, key_destroyed))) {
        return;
    }

    bool readable_aborted = js_stream_has_readable_side(self) &&
                            !js_item_is_true(js_property_get(self, key_end_emitted));
    bool writable_aborted = js_stream_has_writable_side(self) &&
                            !js_item_is_true(js_property_get(self, key_finish_emitted));
    js_stream_mark_destroyed(self);
    js_property_set(self, make_string_item("readableAborted"), js_bool_item(readable_aborted));
    js_property_set(self, make_string_item("writableAborted"), js_bool_item(writable_aborted));
    js_stream_set_error_state(self, err);
    js_property_set(self, make_string_item("__error__"), err);
    js_stream_async_iterators_drain(self, err);

    Item destroy_fn = js_property_get(self, make_string_item("_destroy"));
    if (get_type_id(destroy_fn) == LMD_TYPE_FUNC) {
        js_property_set(self, key_destroy_pending, js_bool_item(true));
        Item destroy_cb = js_bind_function(js_new_function((void*)js_stream_after_destroy, 2),
                                           make_js_undefined(), &self, 1);
        Item destroy_err = js_stream_has_callback_error(err) ? err : ItemNull;
        Item args[2] = { destroy_err, destroy_cb };
        js_call_function(destroy_fn, self, args, 2);
        if (js_check_exception()) {
            Item thrown = js_clear_exception();
            js_property_set(self, key_destroy_pending, js_bool_item(false));
            js_stream_schedule_error_immediate(self, thrown);
            js_stream_schedule_close_immediate(self);
        }
        return;
    }

    js_stream_schedule_close_immediate(self);
}

static int64_t js_stream_read_size_hint(Item self, Item size_item) {
    Item state = js_property_get(self, key_readable_state);
    int64_t hwm = js_stream_state_get_int(state, "highWaterMark", js_stream_default_byte_hwm);
    if (get_type_id(size_item) == LMD_TYPE_INT && it2i(size_item) > 0 &&
        !js_state_get_bool(state, "objectMode")) {
        int64_t requested = it2i(size_item);
        if (requested > hwm) {
            int64_t next = 1;
            while (next < requested && next < 0x40000000) next <<= 1;
            hwm = next < requested ? requested : next;
            Item hwm_item = (Item){.item = i2it(hwm)};
            js_property_set(self, make_string_item("readableHighWaterMark"), hwm_item);
            js_state_set_item(state, "highWaterMark", hwm_item);
        }
    }
    return hwm;
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
    js_stream_set_reading(self, true);
    js_property_set(self, key_reading_sync, js_bool_item(true));
    Item size = (Item){.item = i2it(js_stream_read_size_hint(self, size_item))};
    js_call_function(read_fn, self, &size, 1);
    js_property_set(self, key_reading_sync, js_bool_item(false));
    if (js_check_exception()) {
        Item err = js_clear_exception();
        js_stream_set_reading(self, false);
        js_stream_destroy(self, err);
        return;
    }
    Item after_buf = js_property_get(self, key_buffer);
    int64_t after_len = get_type_id(after_buf) == LMD_TYPE_ARRAY ? js_array_length(after_buf) : 0;
    if (after_len > before_len || js_item_is_true(js_property_get(self, key_end_pending))) {
        js_stream_set_reading(self, false);
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

static Item js_stream_call_read_tick_closure(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    return js_stream_call_read_tick(env[0]);
}

static void js_stream_schedule_read(Item self) {
    Item* env = js_alloc_env(1);
    env[0] = self;
    Item tick = js_new_closure((void*)js_stream_call_read_tick_closure, 0, env, 1);
    js_next_tick_enqueue(tick);
}

static bool js_stream_encoding_is_utf8(Item encoding) {
    if (get_type_id(encoding) != LMD_TYPE_STRING) return false;
    String* enc = it2s(encoding);
    if (!enc) return false;
    return (enc->len == 4 && strncmp(enc->chars, "utf8", 4) == 0) ||
           (enc->len == 5 && strncmp(enc->chars, "utf-8", 5) == 0);
}

static bool js_stream_encoding_is_base64(Item encoding) {
    if (get_type_id(encoding) != LMD_TYPE_STRING) return false;
    String* enc = it2s(encoding);
    return enc && enc->len == 6 && strncmp(enc->chars, "base64", 6) == 0;
}

static int js_stream_utf8_expected_len(uint8_t b0) {
    if (b0 < 0x80) return 1;
    if (b0 >= 0xC2 && b0 <= 0xDF) return 2;
    if (b0 >= 0xE0 && b0 <= 0xEF) return 3;
    if (b0 >= 0xF0 && b0 <= 0xF4) return 4;
    return 0;
}

static bool js_stream_utf8_prefix_valid(uint8_t* data, int available, int expected) {
    if (!data || available <= 0 || expected <= 0) return false;
    uint8_t b0 = data[0];
    if (expected == 1) return true;
    if (available < 2) return true;
    uint8_t b1 = data[1];
    bool b1_ok = false;
    if (expected == 2) b1_ok = (b1 & 0xC0) == 0x80;
    else if (b0 == 0xE0) b1_ok = b1 >= 0xA0 && b1 <= 0xBF;
    else if (b0 == 0xED) b1_ok = b1 >= 0x80 && b1 <= 0x9F;
    else if (b0 == 0xF0) b1_ok = b1 >= 0x90 && b1 <= 0xBF;
    else if (b0 == 0xF4) b1_ok = b1 >= 0x80 && b1 <= 0x8F;
    else b1_ok = (b1 & 0xC0) == 0x80;
    if (!b1_ok) return false;
    for (int i = 2; i < available; i++) {
        if ((data[i] & 0xC0) != 0x80) return false;
    }
    return true;
}

static int js_stream_utf8_incomplete_suffix(uint8_t* data, int len) {
    if (!data || len <= 0) return 0;
    int start = len - 1;
    while (start > 0 && (data[start] & 0xC0) == 0x80 && len - start < 4) {
        start--;
    }
    int available = len - start;
    int expected = js_stream_utf8_expected_len(data[start]);
    if (expected <= 1 || available >= expected) return 0;
    return js_stream_utf8_prefix_valid(data + start, available, expected) ? available : 0;
}

static Item js_stream_decode_utf8_chunk(Item self, Item chunk, Item encoding) {
    Item pending = js_property_get(self, make_string_item("__decode_pending__"));
    if (js_is_typed_array(pending)) {
        Item parts = js_array_new(0);
        js_array_push(parts, pending);
        js_array_push(parts, chunk);
        chunk = js_buffer_concat(parts, make_js_undefined());
        js_property_set(self, make_string_item("__decode_pending__"), make_js_undefined());
    }

    if (!js_is_typed_array(chunk)) {
        return js_buffer_toString(chunk, encoding, make_js_undefined(), make_js_undefined());
    }
    int byte_len = js_typed_array_byte_length(chunk);
    uint8_t* data = (uint8_t*)js_typed_array_current_data_ptr(chunk);
    int hold = js_stream_utf8_incomplete_suffix(data, byte_len);
    if (hold <= 0) {
        return js_buffer_toString(chunk, encoding, make_js_undefined(), make_js_undefined());
    }

    int head_len = byte_len - hold;
    Item tail = js_buffer_slice(chunk, (Item){.item = i2it(head_len)}, make_js_undefined());
    js_property_set(self, make_string_item("__decode_pending__"), tail);
    if (head_len <= 0) return make_string_item("");
    Item head = js_buffer_slice(chunk, (Item){.item = i2it(0)}, (Item){.item = i2it(head_len)});
    return js_buffer_toString(head, encoding, make_js_undefined(), make_js_undefined());
}

static Item js_stream_decode_readable_chunk(Item self, Item chunk) {
    if (js_stream_readable_is_object_mode(self)) return chunk;
    Item encoding = js_property_get(self, make_string_item("_encoding"));
    if (get_type_id(encoding) != LMD_TYPE_STRING) return chunk;
    if (get_type_id(chunk) == LMD_TYPE_STRING) return chunk;
    if (js_stream_encoding_is_utf8(encoding)) return js_stream_decode_utf8_chunk(self, chunk, encoding);
    return js_buffer_toString(chunk, encoding, make_js_undefined(), make_js_undefined());
}

static void js_stream_flush_pending_decode(Item self) {
    Item encoding = js_property_get(self, make_string_item("_encoding"));
    if (!js_stream_encoding_is_utf8(encoding)) return;
    Item pending = js_property_get(self, make_string_item("__decode_pending__"));
    if (!js_is_typed_array(pending)) return;
    js_property_set(self, make_string_item("__decode_pending__"), make_js_undefined());
    Item emitted = js_buffer_toString(pending, encoding, make_js_undefined(), make_js_undefined());
    if (get_type_id(emitted) == LMD_TYPE_STRING) {
        String* str = it2s(emitted);
        if (str && str->len > 0) {
            js_property_set(self, make_string_item("__emitting_data__"), js_bool_item(true));
            stream_emit(self, "data", &emitted, 1);
            js_property_set(self, make_string_item("__emitting_data__"), js_bool_item(false));
        }
    }
}

static void js_stream_coalesce_readable_buffer_for_encoding(Item self, Item encoding) {
    if (js_stream_readable_is_object_mode(self)) return;
    if (get_type_id(encoding) != LMD_TYPE_STRING) return;
    Item buf = js_property_get(self, key_buffer);
    if (get_type_id(buf) != LMD_TYPE_ARRAY) return;
    int64_t blen = js_array_length(buf);
    if (blen <= 1) return;

    Item joined = js_stream_readable_buffer_has_string(buf)
        ? js_stream_concat_decoded_chunks(buf, encoding)
        : js_buffer_concat(buf, make_js_undefined());
    if (js_check_exception()) return;
    if (joined.item == 0 || get_type_id(joined) == LMD_TYPE_UNDEFINED ||
        get_type_id(joined) == LMD_TYPE_NULL) {
        return;
    }
    Item next_buf = js_array_new(0);
    js_array_push(next_buf, joined);
    js_stream_set_readable_buffer(self, next_buf);
}

static void js_stream_flush_buffered_data(Item self) {
    for (;;) {
        Item buf = js_property_get(self, key_buffer);
        if (get_type_id(buf) != LMD_TYPE_ARRAY) return;
        int64_t blen = js_array_length(buf);
        if (blen <= 0) {
            if (js_item_is_true(js_property_get(self, key_end_pending)) &&
                !js_item_is_true(js_property_get(self, key_end_emitted))) {
                js_stream_flush_pending_decode(self);
                Item flowing = js_property_get(self, key_flowing);
                if (flowing.item != 0 && it2b(flowing)) {
                    js_stream_schedule_end_immediate(self);
                } else {
                    js_stream_schedule_end(self);
                }
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
        if (get_type_id(emitted) == LMD_TYPE_STRING) {
            String* str = it2s(emitted);
            if (!str || str->len == 0) continue;
        }
        js_stream_mark_readable_did_read(self);
        js_property_set(self, make_string_item("__emitting_data__"), js_bool_item(true));
        stream_emit(self, "data", &emitted, 1);
        js_property_set(self, make_string_item("__emitting_data__"), js_bool_item(false));
        js_stream_maybe_drain_transform_readable_backpressure(self);
        if (!js_item_is_true(js_property_get(self, key_flowing))) return;
    }
}

static void js_stream_read_more_if_flowing(Item self) {
    if (!js_item_is_true(js_property_get(self, key_flowing))) return;
    Item buf = js_property_get(self, key_buffer);
    if (js_stream_readable_accepts_more(self, buf)) {
        js_stream_call_read_if_needed(self, make_js_undefined());
    }
}

static void js_stream_flow_tick_drain(Item self) {
    js_stream_flush_buffered_data(self);
    js_stream_read_more_if_flowing(self);
    Item buf = js_property_get(self, key_buffer);
    if (js_item_is_true(js_property_get(self, key_flowing)) &&
        get_type_id(buf) == LMD_TYPE_ARRAY && js_array_length(buf) > 0) {
        js_stream_flush_buffered_data(self);
        js_stream_read_more_if_flowing(self);
    }
}

static Item js_stream_flush_data_tick(Item self) {
    ensure_keys();
    js_state_set_bool(js_property_get(self, key_readable_state), "resumeScheduled", false);
    if (js_item_is_true(js_property_get(self, key_destroyed))) {
        return make_js_undefined();
    }
    js_stream_flow_tick_drain(self);
    return make_js_undefined();
}

static Item js_stream_flush_data_tick_closure(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    return js_stream_flush_data_tick(env[0]);
}

static void js_stream_schedule_data_flush(Item self) {
    Item state = js_property_get(self, key_readable_state);
    if (js_state_get_bool(state, "resumeScheduled")) return;
    js_state_set_bool(state, "resumeScheduled", true);
    Item* env = js_alloc_env(1);
    env[0] = self;
    Item tick = js_new_closure((void*)js_stream_flush_data_tick_closure, 0, env, 1);
    js_next_tick_enqueue(tick);
}

static Item js_stream_resume_tick(Item self) {
    ensure_keys();
    Item state = js_property_get(self, key_readable_state);
    js_state_set_bool(state, "resumeScheduled", false);
    if (js_item_is_true(js_property_get(self, key_destroyed))) {
        return make_js_undefined();
    }
    stream_emit(self, "resume", NULL, 0);
    js_stream_flow_tick_drain(self);
    return make_js_undefined();
}

static Item js_stream_resume_tick_closure(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    return js_stream_resume_tick(env[0]);
}

static void js_stream_schedule_resume(Item self) {
    Item state = js_property_get(self, key_readable_state);
    if (js_state_get_bool(state, "resumeScheduled")) return;
    js_state_set_bool(state, "resumeScheduled", true);
    Item* env = js_alloc_env(1);
    env[0] = self;
    Item tick = js_new_closure((void*)js_stream_resume_tick_closure, 0, env, 1);
    js_next_tick_enqueue(tick);
}

extern "C" void js_stream_flush_data_now(Item self) {
    ensure_keys();
    if (js_item_is_true(js_property_get(self, key_destroyed))) return;
    js_stream_flush_buffered_data(self);
}

extern "C" void js_stream_flush_data_if_flowing(Item self) {
    ensure_keys();
    if (!js_item_is_true(js_property_get(self, key_flowing))) return;
    js_stream_flush_data_now(self);
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
    js_array_push(arr, js_stream_make_listener_record(listener));

    // if adding 'data' listener to readable, start flowing mode
    bool is_data_event = js_stream_string_equals(event_item, "data");
    bool is_readable_event = js_stream_string_equals(event_item, "readable");
    bool is_finish_event = js_stream_string_equals(event_item, "finish");
    bool is_drain_event = js_stream_string_equals(event_item, "drain");

    if (is_readable_event) {
        Item state = js_property_get(self, key_readable_state);
        js_state_set_bool(state, "readableListening", true);
        js_state_set_bool(state, "needReadable", true);
        Item buf = js_property_get(self, key_buffer);
        bool has_buffered = get_type_id(buf) == LMD_TYPE_ARRAY && js_array_length(buf) > 0;
        if (has_buffered) {
            js_stream_emit_readable(self);
            if (!js_item_is_true(js_property_get(self, key_end_pending)) &&
                js_stream_readable_accepts_more(self, buf)) {
                js_stream_call_read_if_needed(self, make_js_undefined());
            }
        } else {
            js_property_set(self, make_string_item("__defer_readable_emit__"), js_bool_item(true));
            js_stream_call_read_if_needed(self, make_js_undefined());
            js_property_set(self, make_string_item("__defer_readable_emit__"), js_bool_item(false));
        }
    }

    if (is_data_event) {
        if (js_state_get_bool(js_property_get(self, key_readable_state), "readableListening")) {
            js_stream_set_flowing(self, false);
        } else {
            js_state_set_bool(js_property_get(self, key_readable_state), "needReadable", false);
            js_state_set_bool(js_property_get(self, key_readable_state), "emittedReadable", false);
            js_stream_set_flowing(self, true);
            js_property_set(self, key_paused, js_bool_item(false));
            if (js_item_is_true(js_property_get(self, key_capture_rejections))) {
                js_stream_schedule_data_flush(self);
            } else {
                js_stream_schedule_data_flush(self);
            }
        }
    }
    if (is_finish_event &&
        js_item_is_true(js_property_get(self, key_finish_emitted))) {
        if (get_type_id(listener) == LMD_TYPE_FUNC) {
            Item result = js_call_function(listener, self, NULL, 0);
            js_stream_maybe_capture_rejection(self, "finish", result);
        }
    }
    if (is_drain_event && js_item_is_true(js_property_get(self, js_stream_drain_on_listener_key()))) {
        js_property_set(self, js_stream_drain_on_listener_key(), js_bool_item(false));
        if (get_type_id(listener) == LMD_TYPE_FUNC) {
            // zlib flush can clear backpressure before user code attaches drain;
            // replay the pending drain once so the edge is not lost.
            Item result = js_call_function(listener, self, NULL, 0);
            js_stream_maybe_capture_rejection(self, "drain", result);
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
        if (!js_stream_listener_matches(current, listener)) js_array_push(next, current);
    }
    if (js_array_length(next) == 0) {
        js_property_set(listeners_map, event_item, make_js_undefined());
    } else {
        js_property_set(listeners_map, event_item, next);
    }
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

extern "C" Item js_stream_listeners(Item self, Item event_item) {
    ensure_keys();
    Item result = js_array_new(0);
    if (get_type_id(event_item) != LMD_TYPE_STRING) return result;
    Item listeners_map = js_property_get(self, key_listeners);
    if (get_type_id(listeners_map) != LMD_TYPE_MAP) return result;
    Item arr = js_property_get(listeners_map, event_item);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) return result;
    int64_t len = js_array_length(arr);
    for (int64_t i = 0; i < len; i++) {
        js_array_push(result, js_stream_listener_fn(js_array_get_int(arr, i)));
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
        js_stream_set_reading(self, false);
        if (js_item_is_true(js_property_get(self, key_destroyed)) ||
            js_item_is_true(js_property_get(self, key_end_pending)) ||
            js_item_is_true(js_property_get(self, key_end_emitted))) {
            return js_bool_item(true);
        }

        js_property_set(self, key_ended, js_bool_item(true));
        Item state = js_property_get(self, key_readable_state);
        js_state_set_bool(state, "ended", true);
        Item pipes = js_readable_pipes(self);
        if (get_type_id(pipes) == LMD_TYPE_ARRAY) {
            int64_t plen = js_array_length(pipes);
            for (int64_t i = 0; i < plen; i++) {
                Item pipe_dest = js_array_get_int(pipes, i);
                if (js_item_is_true(js_property_get(pipe_dest, key_destroyed))) {
                    continue;
                }
                Item end_fn = js_property_get(pipe_dest, key_end);
                if (get_type_id(end_fn) == LMD_TYPE_FUNC) {
                    js_call_function(end_fn, pipe_dest, NULL, 0);
                }
            }
            js_readable_remove_pipe(self, make_js_undefined(), true);
        }

        js_property_set(self, key_end_pending, js_bool_item(true));
        js_state_set_bool(state, "needReadable", false);
        Item buf = js_property_get(self, key_buffer);
        bool has_buffered = get_type_id(buf) == LMD_TYPE_ARRAY && js_array_length(buf) > 0;
        bool readable_listening = js_state_get_bool(state, "readableListening");
        Item flowing = js_property_get(self, key_flowing);
        if (has_buffered) {
            if (flowing.item != 0 && it2b(flowing)) {
                js_stream_schedule_data_flush(self);
            } else if (readable_listening) {
                js_stream_emit_readable(self);
            }
        } else if (flowing.item != 0 && it2b(flowing)) {
            js_stream_schedule_end_immediate(self);
        } else if (readable_listening &&
                   js_stream_state_get_int(state, "highWaterMark", js_stream_default_byte_hwm) == 0) {
            js_stream_emit_readable(self);
        }
        js_stream_async_iterators_drain(self, make_js_undefined());
        return js_bool_item(true);
    }

    js_stream_set_reading(self, false);

    if (js_item_is_true(js_property_get(self, key_end_pending)) ||
        js_item_is_true(js_property_get(self, key_end_emitted))) {
        if (!js_stream_has_event_listeners(self, "error")) {
            return js_throw_error_with_code("ERR_STREAM_PUSH_AFTER_EOF",
                                            "stream.push() after EOF");
        }
        Item err = js_stream_make_error_with_code("ERR_STREAM_PUSH_AFTER_EOF",
            "stream.push() after EOF");
        js_stream_schedule_error_once(self, err);
        return js_bool_item(false);
    }
    if (!js_item_is_true(js_property_get(self, key_readable))) {
        Item err = js_stream_make_error_with_code("ERR_STREAM_PUSH_AFTER_EOF",
            "stream.push() after EOF");
        js_stream_schedule_error_once(self, err);
        return js_bool_item(false);
    }

    if (!js_stream_prepare_readable_chunk(self, &chunk, encoding)) return ItemNull;
    if (!js_stream_readable_is_object_mode(self) &&
        js_stream_is_empty_byte_chunk(chunk)) {
        if (!js_item_is_true(js_property_get(self, key_end_pending)) &&
            !js_item_is_true(js_property_get(self, key_end_emitted))) {
            js_stream_schedule_read(self);
        }
        Item buf = js_property_get(self, key_buffer);
        return js_bool_item(js_stream_readable_accepts_more(self, buf));
    }

    if (js_stream_await_drain_pending(self)) {
        // backpressured pipes may still prefetch one readable chunk; while
        // awaiting drain, that chunk belongs in the source buffer, not dest.write().
        Item buf = js_property_get(self, key_buffer);
        if (get_type_id(buf) != LMD_TYPE_ARRAY) {
            buf = js_array_new(0);
            js_stream_set_readable_buffer(self, buf);
        }
        js_stream_append_readable_chunk(self, buf, chunk);
        if (js_state_get_bool(js_property_get(self, key_readable_state), "readableListening")) {
            // the prefetch is now readable even though the pipe is paused for drain.
            js_stream_emit_readable(self);
        }
        js_stream_async_iterators_drain(self, make_js_undefined());
        return js_bool_item(false);
    }

    Item pipes = js_readable_pipes(self);
    if (get_type_id(pipes) == LMD_TYPE_ARRAY) {
        bool removed_destroyed_pipe = false;
        bool wrote_to_pipe = false;
        bool backpressured = false;
        int64_t plen = js_array_length(pipes);
        for (int64_t i = 0; i < plen; i++) {
            Item pipe_dest = js_array_get_int(pipes, i);
            if (js_item_is_true(js_property_get(pipe_dest, key_destroyed))) {
                js_readable_remove_pipe(self, pipe_dest, true);
                removed_destroyed_pipe = true;
                continue;
            }
            Item write_fn = js_property_get(pipe_dest, key_write);
            if (get_type_id(write_fn) == LMD_TYPE_FUNC) {
                wrote_to_pipe = true;
                bool dest_readable_end_first = false;
                Item result = js_call_function(write_fn, pipe_dest, &chunk, 1);
                if (js_check_exception()) {
                    Item err = js_clear_exception();
                    js_stream_schedule_error(pipe_dest, err);
                    return js_bool_item(false);
                }
                Item dest_readable_state = js_property_get(pipe_dest, key_readable_state);
                if (get_type_id(dest_readable_state) == LMD_TYPE_MAP &&
                    (js_state_get_bool(dest_readable_state, "ended") ||
                     js_item_is_true(js_property_get(pipe_dest, key_end_pending)) ||
                     js_item_is_true(js_property_get(pipe_dest, key_end_emitted)))) {
                    Item ended_seen_key = make_string_item("__pipe_dest_readable_ended_seen__");
                    if (!js_item_is_true(js_property_get(self, ended_seen_key))) {
                        js_property_set(self, ended_seen_key, js_bool_item(true));
                        dest_readable_end_first = true;
                    } else {
                        js_stream_set_flowing(self, false);
                        js_property_set(self, key_paused, js_bool_item(true));
                        return js_bool_item(false);
                    }
                }
                if (get_type_id(result) == LMD_TYPE_BOOL && !it2b(result)) {
                    Item dest_error = js_property_get(pipe_dest, make_string_item("__error__"));
                    if (js_stream_has_callback_error(dest_error)) {
                        js_property_set(self, make_string_item("__piped__"), js_bool_item(false));
                        js_property_set(self, make_string_item("__pipe_dest__"), make_js_undefined());
                        return js_bool_item(false);
                    }
                    js_stream_await_drain_add(self, pipe_dest);
                    backpressured = true;
                    if (dest_readable_end_first) js_stream_schedule_read(self);
                    if (!js_stream_source_keeps_pipe_on_backpressure(self)) {
                        js_property_set(self, make_string_item("__piped__"), js_bool_item(false));
                        js_property_set(self, make_string_item("__pipe_dest__"), make_js_undefined());
                    }
                }
            }
        }
        if (backpressured) {
            js_stream_set_flowing(self, false);
            js_property_set(self, key_paused, js_bool_item(true));
            js_stream_schedule_read(self);
            return js_bool_item(false);
        }
        if (removed_destroyed_pipe && !wrote_to_pipe &&
            !js_item_is_true(js_property_get(self, key_flowing)) &&
            !js_stream_has_event_listeners(self, "data") &&
            !js_state_get_bool(js_property_get(self, key_readable_state), "readableListening")) {
            js_property_set(self, key_paused, js_bool_item(true));
            return js_bool_item(false);
        }
    }

    Item buf = js_property_get(self, key_buffer);
    if (get_type_id(buf) != LMD_TYPE_ARRAY) {
        buf = js_array_new(0);
        js_stream_set_readable_buffer(self, buf);
    }
    js_stream_append_readable_chunk(self, buf, chunk);
    Item flowing = js_property_get(self, key_flowing);
    js_stream_async_iterators_drain(self, make_js_undefined());
    if (flowing.item != 0 && it2b(flowing)) {
        js_state_set_bool(js_property_get(self, key_readable_state), "needReadable", false);
        js_state_set_bool(js_property_get(self, key_readable_state), "emittedReadable", false);
        Item transform_fn = js_property_get(self, make_string_item("_transform"));
        if (get_type_id(transform_fn) == LMD_TYPE_FUNC &&
            js_item_is_true(js_property_get(self, make_string_item("_writing")))) {
            js_stream_flush_buffered_data(self);
        } else {
            // flowing streams must expose pushed chunks before unrelated async work can end the process.
            js_stream_flush_buffered_data(self);
        }
    } else if (js_state_get_bool(js_property_get(self, key_readable_state), "readableListening")) {
        bool defer_readable = js_stream_readable_is_object_mode(self) &&
            !js_item_is_true(js_property_get(self, key_reading_sync));
        if (defer_readable) {
            js_property_set(self, make_string_item("__defer_readable_emit__"), js_bool_item(true));
        }
        js_stream_emit_readable(self);
        if (defer_readable) {
            js_property_set(self, make_string_item("__defer_readable_emit__"), js_bool_item(false));
        }
    } else if (!js_item_is_true(js_property_get(self, key_end_pending)) &&
               js_stream_readable_accepts_more(self, buf)) {
        js_stream_schedule_read(self);
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
        js_stream_is_empty_byte_chunk(chunk)) {
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
        js_state_set_bool(js_property_get(self, key_readable_state), "needReadable", false);
        js_state_set_bool(js_property_get(self, key_readable_state), "emittedReadable", false);
        js_stream_schedule_data_flush(self);
    } else if (js_state_get_bool(js_property_get(self, key_readable_state), "readableListening")) {
        js_stream_emit_readable(self);
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
    int64_t length = js_stream_readable_cached_length(self, buf);
    if (length == 0) return true;
    Item state = js_property_get(self, key_readable_state);
    int64_t hwm = js_stream_state_get_int(state, "highWaterMark", js_stream_default_byte_hwm);
    return length < hwm;
}

static bool js_stream_readable_buffer_backpressured(Item self) {
    Item state = js_property_get(self, key_readable_state);
    if (get_type_id(state) != LMD_TYPE_MAP) return false;
    Item buf = js_property_get(self, key_buffer);
    int64_t length = js_stream_readable_cached_length(self, buf);
    int64_t hwm = js_stream_state_get_int(state, "highWaterMark", js_stream_default_byte_hwm);
    return hwm > 0 && length >= hwm;
}

static bool js_stream_mark_transform_readable_backpressure(Item self) {
    if (!js_stream_readable_buffer_backpressured(self)) return false;
    Item state = js_property_get(self, key_writable_state);
    if (get_type_id(state) == LMD_TYPE_MAP) {
        js_state_set_bool(state, "needDrain", true);
    }
    return true;
}

static void js_stream_maybe_drain_transform_readable_backpressure(Item self) {
    Item state = js_property_get(self, key_writable_state);
    if (get_type_id(state) != LMD_TYPE_MAP) return;
    if (!js_state_get_bool(state, "needDrain")) return;
    if (js_stream_readable_buffer_backpressured(self)) return;
    if (js_state_get_bool(state, "ended") ||
        js_item_is_true(js_property_get(self, key_destroyed)) ||
        js_item_is_true(js_property_get(self, key_finish_emitted))) {
        return;
    }
    js_state_set_bool(state, "needDrain", false);
    js_stream_emit_or_schedule_drain(self);
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
        Item read_fn = js_property_get(self, make_string_item("_read"));
        Item pull_size = (Item){.item = i2it(js_stream_read_size_hint(self, (Item){.item = i2it(want)}))};
        while (available < want && !js_item_is_true(js_property_get(self, key_end_pending))) {
            if (get_type_id(read_fn) != LMD_TYPE_FUNC ||
                js_item_is_true(js_property_get(self, key_destroyed)) ||
                js_item_is_true(js_property_get(self, key_reading))) {
                break;
            }
            int64_t before_len = blen;
            js_stream_set_reading(self, true);
            js_property_set(self, key_reading_sync, js_bool_item(true));
            js_call_function(read_fn, self, &pull_size, 1);
            js_property_set(self, key_reading_sync, js_bool_item(false));
            if (js_check_exception()) {
                Item err = js_clear_exception();
                js_stream_set_reading(self, false);
                js_stream_destroy(self, err);
                return ItemNull;
            }
            buf = js_property_get(self, key_buffer);
            if (get_type_id(buf) != LMD_TYPE_ARRAY) return ItemNull;
            blen = js_array_length(buf);
            // sync _read() can satisfy one byte at a time; keep pulling until progress stops or EOF.
            if (blen > before_len || js_item_is_true(js_property_get(self, key_end_pending))) {
                js_stream_set_reading(self, false);
            }
            if (blen <= before_len) break;
            for (int64_t i = before_len; i < blen && available < want; i++) {
                available += js_stream_readable_chunk_length(self, js_array_get_int(buf, i));
            }
        }
        if (available < want && !js_item_is_true(js_property_get(self, key_end_pending))) {
            return ItemNull;
        }
    }
    if (available == 0) return ItemNull;
    if (want > available) want = available;
    if (want == available) {
        Item consumed = buf;
        js_stream_set_readable_buffer(self, js_array_new(0));
        if (js_item_is_true(js_property_get(self, key_end_pending)))
            js_stream_schedule_end(self);
        else
            js_stream_schedule_read(self);
        // reading the full buffer must not duplicate hundreds of thousands of chunk refs.
        if (blen == 1) return js_array_get_int(consumed, 0);
        return js_buffer_concat(consumed, (Item){.item = i2it(want)});
    }

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
    if (get_type_id(size_item) == LMD_TYPE_INT && it2i(size_item) == 0) {
        js_stream_call_read_if_needed(self, size_item);
        if (get_type_id(js_property_get(self, make_string_item("_encoding"))) == LMD_TYPE_STRING) {
            Item buf = js_property_get(self, key_buffer);
            while (!js_item_is_true(js_property_get(self, key_end_pending)) &&
                   !js_item_is_true(js_property_get(self, key_end_emitted)) &&
                   js_stream_readable_accepts_more(self, buf)) {
                int64_t before_len = get_type_id(buf) == LMD_TYPE_ARRAY ? js_array_length(buf) : 0;
                js_stream_call_read_if_needed(self, size_item);
                buf = js_property_get(self, key_buffer);
                int64_t after_len = get_type_id(buf) == LMD_TYPE_ARRAY ? js_array_length(buf) : 0;
                if (after_len <= before_len) break;
            }
        }
        return ItemNull;
    }
    if (js_item_is_true(js_property_get(self, make_string_item("__emitting_data__")))) {
        return ItemNull;
    }

    js_state_set_bool(js_property_get(self, key_readable_state), "emittedReadable", false);
    Item buf = js_property_get(self, key_buffer);
    if (get_type_id(buf) != LMD_TYPE_ARRAY || js_array_length(buf) == 0) {
        js_stream_call_read_if_needed(self, size_item);
        buf = js_property_get(self, key_buffer);
    }
    if (get_type_id(buf) != LMD_TYPE_ARRAY) {
        js_stream_update_need_after_read(self);
        return ItemNull;
    }
    int64_t blen = js_array_length(buf);

    if (blen == 0) {
        if (js_item_is_true(js_property_get(self, key_end_pending)))
            js_stream_schedule_end(self);
        js_stream_update_need_after_read(self);
        return ItemNull;
    }

    if (get_type_id(size_item) == LMD_TYPE_INT && it2i(size_item) > 0 &&
        !js_stream_readable_is_object_mode(self)) {
        Item exact = js_readable_read_exact(self, buf, blen, it2i(size_item));
        if (exact.item == 0 || get_type_id(exact) == LMD_TYPE_NULL ||
            get_type_id(exact) == LMD_TYPE_UNDEFINED) {
            if (js_item_is_true(js_property_get(self, key_end_pending)))
                js_stream_schedule_end(self);
            else
                js_stream_mark_readable_needed(self, true);
            return ItemNull;
        }
        Item decoded = js_stream_decode_readable_chunk(self, exact);
        js_stream_update_need_after_read(self);
        js_stream_mark_readable_did_read(self);
        js_stream_maybe_drain_transform_readable_backpressure(self);
        return js_stream_maybe_emit_manual_data(self, decoded);
    }

    if (blen == 0 && !js_item_is_true(js_property_get(self, key_end_pending))) {
        js_stream_call_read_if_needed(self, size_item);
        buf = js_property_get(self, key_buffer);
        if (get_type_id(buf) != LMD_TYPE_ARRAY) {
            js_stream_update_need_after_read(self);
            return ItemNull;
        }
        blen = js_array_length(buf);
        if (blen == 0) {
            js_stream_update_need_after_read(self);
            return ItemNull;
        }
    }

    Item encoding = js_property_get(self, make_string_item("_encoding"));
    if (get_type_id(encoding) == LMD_TYPE_STRING &&
        !js_stream_readable_is_object_mode(self)) {
        if (!js_item_is_true(js_property_get(self, key_end_pending)) &&
            !js_item_is_true(js_property_get(self, key_end_emitted))) {
            js_stream_call_read_if_needed(self, make_js_undefined());
            buf = js_property_get(self, key_buffer);
            if (get_type_id(buf) != LMD_TYPE_ARRAY) {
                js_stream_update_need_after_read(self);
                return ItemNull;
            }
            blen = js_array_length(buf);
            if (blen == 0) {
                js_stream_update_need_after_read(self);
                return ItemNull;
            }
        }
        if (js_stream_encoding_is_base64(encoding)) {
            int64_t available = js_stream_readable_buffer_length(self, buf);
            while (available < 3 &&
                   !js_item_is_true(js_property_get(self, key_end_pending)) &&
                   !js_item_is_true(js_property_get(self, key_end_emitted))) {
                int64_t before_available = available;
                js_stream_call_read_if_needed(self, make_js_undefined());
                buf = js_property_get(self, key_buffer);
                if (get_type_id(buf) != LMD_TYPE_ARRAY) {
                    js_stream_update_need_after_read(self);
                    return ItemNull;
                }
                blen = js_array_length(buf);
                available = js_stream_readable_buffer_length(self, buf);
                if (available <= before_available) break;
            }
            if (available < 3 &&
                !js_item_is_true(js_property_get(self, key_end_pending)) &&
                !js_item_is_true(js_property_get(self, key_end_emitted))) {
                // base64 needs a complete 3-byte group; an empty push is not EOF, so keep the tail buffered.
                js_stream_mark_readable_needed(self, true);
                return ItemNull;
            }
            int64_t remainder = available % 3;
            if (available > 3 && remainder != 0) {
                // base64 StringDecoder holds incomplete 3-byte groups until the
                // final read; encoding the whole buffer here folds two reads into one.
                Item exact = js_readable_read_exact(self, buf, blen, available - remainder);
                Item decoded = js_buffer_toString(exact, encoding, make_js_undefined(), make_js_undefined());
                js_stream_update_need_after_read(self);
                js_stream_mark_readable_did_read(self);
                js_stream_maybe_drain_transform_readable_backpressure(self);
                return js_stream_maybe_emit_manual_data(self, decoded);
            }
        }
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
        js_stream_update_need_after_read(self);
        js_stream_mark_readable_did_read(self);
        js_stream_maybe_drain_transform_readable_backpressure(self);
        if (get_type_id(joined) == LMD_TYPE_STRING)
            return js_stream_maybe_emit_manual_data(self, joined);
        Item decoded = js_buffer_toString(joined, encoding, make_js_undefined(), make_js_undefined());
        return js_stream_maybe_emit_manual_data(self, decoded);
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
    js_stream_update_need_after_read(self);
    result = js_stream_decode_object_readable_chunk(self, result);
    js_stream_mark_readable_did_read(self);
    js_stream_maybe_drain_transform_readable_backpressure(self);
    return js_stream_maybe_emit_manual_data(self, result);
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

static void js_stream_async_iterator_detach(Item iterator) {
    Item stream = js_property_get(iterator, make_string_item("__stream__"));
    if (get_type_id(stream) != LMD_TYPE_MAP && get_type_id(stream) != LMD_TYPE_ELEMENT) return;
    Item iterators_key = make_string_item("__async_iterators__");
    Item iterators = js_property_get(stream, iterators_key);
    if (get_type_id(iterators) != LMD_TYPE_ARRAY) return;

    Item next_iterators = js_array_new(0);
    int64_t len = js_array_length(iterators);
    for (int64_t i = 0; i < len; i++) {
        Item current = js_array_get_int(iterators, i);
        if (current.item != iterator.item) {
            js_array_push(next_iterators, current);
        }
    }
    js_property_set(stream, iterators_key, next_iterators);
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
    js_stream_async_iterator_detach(iterator);
}

static void js_stream_async_iterator_reject_all(Item iterator, Item err) {
    js_property_set(iterator, make_string_item("__done__"), js_bool_item(true));
    while (js_stream_async_iterator_pending_count(iterator) > 0) {
        js_stream_async_iterator_reject(iterator, err);
    }
    js_stream_async_iterator_detach(iterator);
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
    if (js_item_is_true(js_property_get(stream, key_destroyed))) {
        // rejected for-await calls both the error cleanup and iterator.return();
        // only the first path may invoke a legacy public destroy hook.
        return;
    }
    Item destroy = js_property_get(stream, key_destroy);
    if (get_type_id(destroy) == LMD_TYPE_FUNC) {
        if (!js_stream_is_native_stream(stream)) {
            // legacy destroy hooks may not update stream flags, so mark before
            // cleanup to keep pipeline's later error teardown idempotent.
            js_stream_mark_destroyed(stream);
        }
        js_call_function(destroy, stream, NULL, 0);
        return;
    }
    Item close = js_property_get(stream, make_string_item("close"));
    if (get_type_id(close) == LMD_TYPE_FUNC) {
        if (!js_stream_is_native_stream(stream)) {
            // legacy close hooks may not update stream flags, so mark before
            // cleanup to keep pipeline's later error teardown idempotent.
            js_stream_mark_destroyed(stream);
        }
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

static Item js_stream_async_iterator_read_chunk(Item stream) {
    js_property_set(stream, make_string_item("__async_iterator_reading__"), js_bool_item(true));
    Item chunk = js_readable_read(stream);
    js_property_set(stream, make_string_item("__async_iterator_reading__"), js_bool_item(false));
    return chunk;
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
            Item chunk = js_stream_async_iterator_read_chunk(stream);
            if (js_stream_async_iterator_has_value(chunk)) {
                js_stream_async_iterator_resolve(iterator,
                    js_stream_iterator_result(chunk, false));
                continue;
            }
            if (js_stream_async_iterator_stream_done(stream)) {
                if (js_stream_destroy_pending(stream)) {
                    // destroyed streams with async _destroy(cb) are not terminal
                    // for iterators until the callback supplies success or error.
                    break;
                }
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
        js_stream_async_iterator_detach(iterator);
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
        js_stream_async_iterator_detach(iterator);
        return js_promise_reject(stored_error);
    }
    if (js_item_is_true(js_property_get(stream, make_string_item("__iter_failed__")))) {
        js_property_set(iterator, make_string_item("__done__"), js_bool_item(true));
        js_stream_async_iterator_detach(iterator);
        return js_promise_reject(js_property_get(stream, make_string_item("__iter_error__")));
    }
    if (js_item_is_true(js_property_get(stream, key_destroyed)) &&
        !js_item_is_true(js_property_get(stream, key_end_pending)) &&
        !js_item_is_true(js_property_get(stream, key_end_emitted)) &&
        !js_item_is_true(js_property_get(stream, key_ended))) {
        if (js_stream_destroy_pending(stream)) {
            // async _destroy(cb) can still supply the terminal error; rejecting
            // now would hide the callback error from iterators created mid-destroy.
            return js_stream_async_iterator_pending_promise(iterator, stream);
        }
        js_property_set(iterator, make_string_item("__done__"), js_bool_item(true));
        js_stream_async_iterator_detach(iterator);
        return js_promise_reject(js_stream_make_error_with_code("ERR_STREAM_PREMATURE_CLOSE",
            "Premature close"));
    }

    if (js_stream_async_iterator_is_legacy_stream(stream)) {
        js_stream_async_iterator_setup_legacy(iterator, stream);
        Item event_error = js_property_get(iterator, make_string_item("__event_error__"));
        if (js_stream_has_callback_error(event_error)) {
            js_property_set(iterator, make_string_item("__done__"), js_bool_item(true));
            js_stream_async_iterator_detach(iterator);
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
            js_stream_async_iterator_detach(iterator);
            js_stream_async_iterator_cleanup_stream(stream);
            return js_promise_resolve(js_stream_iterator_result(make_js_undefined(), true));
        }
        return js_stream_async_iterator_pending_promise(iterator, stream);
    }

    Item chunk = js_stream_async_iterator_read_chunk(stream);
    if (js_stream_async_iterator_has_value(chunk)) {
        return js_promise_resolve(js_stream_iterator_result(chunk, false));
    }
    stored_error = js_property_get(stream, make_string_item("__error__"));
    if (js_stream_has_callback_error(stored_error)) {
        js_property_set(iterator, make_string_item("__done__"), js_bool_item(true));
        js_stream_async_iterator_detach(iterator);
        return js_promise_reject(stored_error);
    }

    bool readable_done = js_stream_async_iterator_stream_done(stream);
    if (!readable_done) {
        js_stream_call_read_if_needed(stream, make_js_undefined());
        stored_error = js_property_get(stream, make_string_item("__error__"));
        if (js_stream_has_callback_error(stored_error)) {
            js_property_set(iterator, make_string_item("__done__"), js_bool_item(true));
            js_stream_async_iterator_detach(iterator);
            return js_promise_reject(stored_error);
        }
        chunk = js_stream_async_iterator_read_chunk(stream);
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
    js_stream_async_iterator_detach(iterator);
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
    js_stream_async_iterator_detach(iterator);
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
    js_stream_async_iterator_detach(iterator);
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

static Item js_stream_reject_with_exception(Item reject) {
    Item err = js_clear_exception();
    if (get_type_id(reject) == LMD_TYPE_FUNC) {
        Item args[1] = { err };
        js_call_function(reject, make_js_undefined(), args, 1);
    }
    return make_js_undefined();
}

static Item js_stream_make_callback_options(Item signal) {
    Item options = js_new_object();
    if (signal.item != 0 && get_type_id(signal) != LMD_TYPE_UNDEFINED) {
        js_property_set(options, make_string_item("signal"), signal);
    }
    return options;
}

static Item js_stream_collect_next(Item env_item);

static Item js_stream_collect_reject(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    env[5] = js_bool_item(true);
    Item reject = env[2];
    if (get_type_id(reject) == LMD_TYPE_FUNC) {
        Item args[1] = { err };
        js_call_function(reject, make_js_undefined(), args, 1);
    }
    return make_js_undefined();
}

static Item js_stream_collect_step(Item env_item, Item result) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    if (js_item_is_true(env[5])) return make_js_undefined();
    if (js_iterator_result_done(result)) {
        Item resolve = env[1];
        Item values = env[3];
        env[5] = js_bool_item(true);
        if (get_type_id(resolve) == LMD_TYPE_FUNC) {
            Item args[1] = { values };
            js_call_function(resolve, make_js_undefined(), args, 1);
        }
        return make_js_undefined();
    }
    js_array_push(env[3], js_iterator_result_value(result));
    return js_stream_collect_next(env_item);
}

static Item js_stream_collect_next(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    if (js_item_is_true(env[5])) return make_js_undefined();
    Item step = js_stream_async_iterator_next(env[0]);
    if (js_check_exception()) return js_stream_reject_with_exception(env[2]);
    Item on_step = js_new_closure((void*)js_stream_collect_step, 1, env, 7);
    Item on_error = js_new_closure((void*)js_stream_collect_reject, 1, env, 7);
    js_promise_then(step, on_step, on_error);
    return make_js_undefined();
}

static Item js_stream_collect_abort(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[5])) return make_js_undefined();
    env[5] = js_bool_item(true);
    Item signal = env[6];
    Item reason = js_property_get(signal, make_string_item("reason"));
    if (reason.item == 0 || get_type_id(reason) == LMD_TYPE_UNDEFINED)
        reason = js_stream_iter_make_abort_error();
    js_stream_destroy(env[4], reason);
    Item reject = env[2];
    if (get_type_id(reject) == LMD_TYPE_FUNC) {
        Item args[1] = { reason };
        js_call_function(reject, make_js_undefined(), args, 1);
    }
    return make_js_undefined();
}

static bool js_readable_toArray_validate_options(Item options) {
    TypeId tid = get_type_id(options);
    if (options.item == 0 || tid == LMD_TYPE_UNDEFINED || tid == LMD_TYPE_NULL)
        return true;
    if (tid != LMD_TYPE_MAP && tid != LMD_TYPE_ELEMENT) {
        js_throw_invalid_arg_type("options", "object", options);
        return false;
    }
    Item signal = js_property_get(options, make_string_item("signal"));
    TypeId signal_tid = get_type_id(signal);
    if (signal.item == 0 || signal_tid == LMD_TYPE_UNDEFINED) return true;
    if (!js_stream_is_abort_signal(signal)) {
        js_throw_invalid_arg_type("options.signal", "AbortSignal", signal);
        return false;
    }
    return true;
}

static Item js_readable_toArray(Item readable, Item options) {
    if (!js_readable_toArray_validate_options(options))
        return js_promise_reject(js_clear_exception());
    Item capability = js_promise_with_resolvers();
    if (js_check_exception()) return ItemNull;
    Item* env = js_alloc_env(7);
    env[0] = js_stream_async_iterator(readable);
    env[1] = js_property_get(capability, make_string_item("resolve"));
    env[2] = js_property_get(capability, make_string_item("reject"));
    env[3] = js_array_new(0);
    env[4] = readable;
    env[5] = js_bool_item(false);
    env[6] = make_js_undefined();
    if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_ELEMENT) {
        Item signal = js_property_get(options, make_string_item("signal"));
        if (js_stream_is_abort_signal(signal)) {
            Item aborted = js_property_get(signal, make_string_item("aborted"));
            if (get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted)) {
                Item reason = js_property_get(signal, make_string_item("reason"));
                if (reason.item == 0 || get_type_id(reason) == LMD_TYPE_UNDEFINED)
                    reason = js_stream_iter_make_abort_error();
                js_stream_destroy(readable, reason);
                return js_promise_reject(reason);
            }
            Item add_event = js_property_get(signal, make_string_item("addEventListener"));
            if (get_type_id(add_event) == LMD_TYPE_FUNC) {
                env[6] = signal;
                Item listener = js_new_closure((void*)js_stream_collect_abort, 0, env, 7);
                Item args[2] = { make_string_item("abort"), listener };
                js_call_function(add_event, signal, args, 2);
            }
        }
    }
    js_stream_collect_next((Item){.item = (uint64_t)(uintptr_t)env});
    return js_property_get(capability, make_string_item("promise"));
}

static Item js_readable_transform_pump(Item env_item);

static Item js_readable_transform_fail(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    js_stream_destroy(env[1], err);
    return make_js_undefined();
}

static Item js_readable_transform_value(Item env_item, Item mapped) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item out = env[1];
    if (js_item_is_true(js_property_get(out, key_destroyed))) return make_js_undefined();

    int64_t mode = get_type_id(env[3]) == LMD_TYPE_INT ? it2i(env[3]) : 0;
    if (mode == 0 || js_item_is_true(mapped)) {
        Item chunk = mode == 0 ? mapped : env[5];
        js_readable_push(out, chunk);
    }
    return js_readable_transform_pump(env_item);
}

static Item js_readable_transform_step(Item env_item, Item result) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item out = env[1];
    if (js_item_is_true(js_property_get(out, key_destroyed))) return make_js_undefined();
    if (js_iterator_result_done(result)) {
        js_readable_push(out, ItemNull);
        return make_js_undefined();
    }

    Item chunk = js_iterator_result_value(result);
    env[5] = chunk;
    Item mapper = env[2];
    Item signal = env[4];
    Item options = js_stream_make_callback_options(signal);
    Item args[2] = { chunk, options };
    Item mapped = js_call_function(mapper, make_js_undefined(), args, 2);
    if (js_check_exception()) {
        Item err = js_clear_exception();
        js_stream_destroy(out, err);
        return make_js_undefined();
    }
    Item mapped_promise = js_promise_resolve(mapped);
    Item on_value = js_new_closure((void*)js_readable_transform_value, 1, env, 6);
    Item on_error = js_new_closure((void*)js_readable_transform_fail, 1, env, 6);
    js_promise_then(mapped_promise, on_value, on_error);
    return make_js_undefined();
}

static Item js_readable_transform_pump(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item out = env[1];
    if (js_item_is_true(js_property_get(out, key_destroyed))) return make_js_undefined();
    Item step = js_stream_async_iterator_next(env[0]);
    if (js_check_exception()) {
        Item err = js_clear_exception();
        js_stream_destroy(out, err);
        return make_js_undefined();
    }
    Item on_step = js_new_closure((void*)js_readable_transform_step, 1, env, 6);
    Item on_error = js_new_closure((void*)js_readable_transform_fail, 1, env, 6);
    js_promise_then(step, on_step, on_error);
    return make_js_undefined();
}

static bool js_stream_validate_helper_fn(Item fn) {
    if (get_type_id(fn) == LMD_TYPE_FUNC) return true;
    js_throw_invalid_arg_type("fn", "function", fn);
    return false;
}

static bool js_stream_validate_helper_options(Item options) {
    TypeId tid = get_type_id(options);
    if (options.item == 0 || tid == LMD_TYPE_UNDEFINED || tid == LMD_TYPE_NULL ||
        tid == LMD_TYPE_MAP || tid == LMD_TYPE_ELEMENT) {
        return true;
    }
    js_throw_invalid_arg_type("options", "object", options);
    return false;
}

static bool js_stream_validate_helper_signal(Item options) {
    TypeId tid = get_type_id(options);
    if (tid != LMD_TYPE_MAP && tid != LMD_TYPE_ELEMENT) return true;
    Item signal = js_property_get(options, make_string_item("signal"));
    TypeId signal_tid = get_type_id(signal);
    if (signal.item == 0 || signal_tid == LMD_TYPE_UNDEFINED || signal_tid == LMD_TYPE_NULL)
        return true;
    if (js_stream_is_abort_signal(signal)) return true;
    js_throw_invalid_arg_type("options.signal", "AbortSignal", signal);
    return false;
}

static bool js_stream_validate_concurrency(Item options) {
    if (get_type_id(options) != LMD_TYPE_MAP && get_type_id(options) != LMD_TYPE_ELEMENT)
        return true;
    Item concurrency = js_property_get(options, make_string_item("concurrency"));
    TypeId tid = get_type_id(concurrency);
    if (concurrency.item == 0 || tid == LMD_TYPE_UNDEFINED) return true;
    if (tid != LMD_TYPE_INT || it2i(concurrency) < 1) {
        js_throw_error_with_code("ERR_OUT_OF_RANGE",
            "The value of \"options.concurrency\" is out of range.");
        return false;
    }
    return true;
}

static Item js_readable_transform_helper(Item readable, Item fn, Item options, int64_t mode) {
    if (!js_stream_validate_helper_fn(fn)) return ItemNull;
    if (!js_stream_validate_helper_options(options)) return ItemNull;
    if (!js_stream_validate_helper_signal(options)) return ItemNull;
    if (!js_stream_validate_concurrency(options)) return ItemNull;

    Item opts = js_new_object();
    js_property_set(opts, make_string_item("objectMode"), js_bool_item(true));
    Item out = js_readable_new(opts);
    Item signal = make_js_undefined();
    if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_ELEMENT) {
        signal = js_property_get(options, make_string_item("signal"));
        js_stream_iter_attach_abort(options, out);
    }

    Item* env = js_alloc_env(6);
    env[0] = js_stream_async_iterator(readable);
    env[1] = out;
    env[2] = fn;
    env[3] = (Item){.item = i2it(mode)};
    env[4] = signal;
    env[5] = make_js_undefined();
    js_readable_transform_pump((Item){.item = (uint64_t)(uintptr_t)env});
    return out;
}

static Item js_readable_map(Item readable, Item fn, Item options) {
    return js_readable_transform_helper(readable, fn, options, 0);
}

static Item js_readable_filter(Item readable, Item fn, Item options) {
    return js_readable_transform_helper(readable, fn, options, 1);
}

static Item js_readable_forEach_next(Item env_item);

static Item js_readable_forEach_fail(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item reject = env[2];
    if (get_type_id(reject) == LMD_TYPE_FUNC) {
        Item args[1] = { err };
        js_call_function(reject, make_js_undefined(), args, 1);
    }
    return make_js_undefined();
}

static Item js_readable_forEach_continue(Item env_item, Item ignored) {
    (void)ignored;
    return js_readable_forEach_next(env_item);
}

static Item js_readable_forEach_step(Item env_item, Item result) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    if (js_iterator_result_done(result)) {
        Item resolve = env[1];
        if (get_type_id(resolve) == LMD_TYPE_FUNC)
            js_call_function(resolve, make_js_undefined(), NULL, 0);
        return make_js_undefined();
    }
    Item chunk = js_iterator_result_value(result);
    Item options = js_stream_make_callback_options(env[4]);
    Item args[2] = { chunk, options };
    Item call_result = js_call_function(env[3], make_js_undefined(), args, 2);
    if (js_check_exception()) return js_stream_reject_with_exception(env[2]);
    Item promise = js_promise_resolve(call_result);
    Item on_done = js_new_closure((void*)js_readable_forEach_continue, 1, env, 5);
    Item on_error = js_new_closure((void*)js_readable_forEach_fail, 1, env, 5);
    js_promise_then(promise, on_done, on_error);
    return make_js_undefined();
}

static Item js_readable_forEach_next(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item step = js_stream_async_iterator_next(env[0]);
    if (js_check_exception()) return js_stream_reject_with_exception(env[2]);
    Item on_step = js_new_closure((void*)js_readable_forEach_step, 1, env, 5);
    Item on_error = js_new_closure((void*)js_readable_forEach_fail, 1, env, 5);
    js_promise_then(step, on_step, on_error);
    return make_js_undefined();
}

static Item js_readable_forEach(Item readable, Item fn, Item options) {
    if (!js_stream_validate_helper_fn(fn)) return js_promise_reject(js_clear_exception());
    if (!js_stream_validate_helper_options(options)) return js_promise_reject(js_clear_exception());
    if (!js_stream_validate_helper_signal(options)) return js_promise_reject(js_clear_exception());
    if (!js_stream_validate_concurrency(options)) return js_promise_reject(js_clear_exception());
    Item signal = make_js_undefined();
    if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_ELEMENT)
        signal = js_property_get(options, make_string_item("signal"));
    Item capability = js_promise_with_resolvers();
    if (js_check_exception()) return ItemNull;
    Item* env = js_alloc_env(5);
    env[0] = js_stream_async_iterator(readable);
    env[1] = js_property_get(capability, make_string_item("resolve"));
    env[2] = js_property_get(capability, make_string_item("reject"));
    env[3] = fn;
    env[4] = signal;
    js_readable_forEach_next((Item){.item = (uint64_t)(uintptr_t)env});
    return js_property_get(capability, make_string_item("promise"));
}

static Item js_readable_reduce_next(Item env_item);

static Item js_readable_reduce_fail(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item reject = env[2];
    if (get_type_id(reject) == LMD_TYPE_FUNC) {
        Item args[1] = { err };
        js_call_function(reject, make_js_undefined(), args, 1);
    }
    return make_js_undefined();
}

static Item js_readable_reduce_continue(Item env_item, Item value) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    env[4] = value;
    env[5] = js_bool_item(true);
    return js_readable_reduce_next(env_item);
}

static Item js_readable_reduce_step(Item env_item, Item result) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    if (js_iterator_result_done(result)) {
        Item resolve = env[1];
        Item value = js_item_is_true(env[5]) ? env[4] : make_js_undefined();
        if (get_type_id(resolve) == LMD_TYPE_FUNC) {
            Item args[1] = { value };
            js_call_function(resolve, make_js_undefined(), args, 1);
        }
        return make_js_undefined();
    }

    Item chunk = js_iterator_result_value(result);
    if (!js_item_is_true(env[5])) {
        env[4] = chunk;
        env[5] = js_bool_item(true);
        return js_readable_reduce_next(env_item);
    }

    Item options = js_stream_make_callback_options(env[6]);
    Item args[3] = { env[4], chunk, options };
    Item call_result = js_call_function(env[3], make_js_undefined(), args, 3);
    if (js_check_exception()) return js_stream_reject_with_exception(env[2]);
    Item promise = js_promise_resolve(call_result);
    Item on_done = js_new_closure((void*)js_readable_reduce_continue, 1, env, 7);
    Item on_error = js_new_closure((void*)js_readable_reduce_fail, 1, env, 7);
    js_promise_then(promise, on_done, on_error);
    return make_js_undefined();
}

static Item js_readable_reduce_next(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item step = js_stream_async_iterator_next(env[0]);
    if (js_check_exception()) return js_stream_reject_with_exception(env[2]);
    Item on_step = js_new_closure((void*)js_readable_reduce_step, 1, env, 7);
    Item on_error = js_new_closure((void*)js_readable_reduce_fail, 1, env, 7);
    js_promise_then(step, on_step, on_error);
    return make_js_undefined();
}

static Item js_readable_reduce(Item readable, Item fn, Item initial, Item options) {
    if (!js_stream_validate_helper_fn(fn)) return js_promise_reject(js_clear_exception());
    if (!js_stream_validate_helper_options(options)) return js_promise_reject(js_clear_exception());
    if (!js_stream_validate_helper_signal(options)) return js_promise_reject(js_clear_exception());
    Item signal = make_js_undefined();
    if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_ELEMENT)
        signal = js_property_get(options, make_string_item("signal"));
    Item capability = js_promise_with_resolvers();
    if (js_check_exception()) return ItemNull;
    Item* env = js_alloc_env(7);
    env[0] = js_stream_async_iterator(readable);
    env[1] = js_property_get(capability, make_string_item("resolve"));
    env[2] = js_property_get(capability, make_string_item("reject"));
    env[3] = fn;
    env[4] = initial;
    env[5] = js_bool_item(get_type_id(initial) != LMD_TYPE_UNDEFINED);
    env[6] = signal;
    js_readable_reduce_next((Item){.item = (uint64_t)(uintptr_t)env});
    return js_property_get(capability, make_string_item("promise"));
}

static bool js_readable_compose_is_duplex_like(Item stream) {
    JsClass cls = js_class_id(stream);
    if (cls == JS_CLASS_DUPLEX || cls == JS_CLASS_TRANSFORM ||
        cls == JS_CLASS_PASS_THROUGH) {
        return true;
    }
    Item readable_state = js_property_get(stream, key_readable_state);
    Item writable_state = js_property_get(stream, key_writable_state);
    return (get_type_id(readable_state) == LMD_TYPE_MAP ||
            get_type_id(readable_state) == LMD_TYPE_ELEMENT) &&
           (get_type_id(writable_state) == LMD_TYPE_MAP ||
            get_type_id(writable_state) == LMD_TYPE_ELEMENT);
}

static void js_readable_compose_bridge_start(Item* env);

static Item js_readable_compose_pipe_tick(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    js_readable_pipe(env[0], env[1]);
    return make_js_undefined();
}

static Item js_readable_compose_forward_error(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    js_stream_destroy(env[0], err);
    return make_js_undefined();
}

static void js_readable_compose_attach_error_forward(Item source, Item out) {
    Item* env = js_alloc_env(1);
    env[0] = out;
    Item listener = js_new_closure((void*)js_readable_compose_forward_error, 1, env, 1);
    js_stream_on(source, make_string_item("error"), listener);
}

static Item js_readable_compose_bridge_write(Item env_item, Item chunk, Item encoding, Item callback) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item source = env[0];
    Item input = env[2];
    Item write_fn = js_property_get(source, key_write);
    Item result = make_js_undefined();
    if (get_type_id(write_fn) == LMD_TYPE_FUNC) {
        Item args[3] = { chunk, encoding, callback };
        result = js_call_function(write_fn, source, args, 3);
        if (js_check_exception()) return ItemNull;
    }
    if (input.item != source.item &&
        (get_type_id(input) == LMD_TYPE_MAP || get_type_id(input) == LMD_TYPE_ELEMENT)) {
        Item input_write = js_property_get(input, key_write);
        if (get_type_id(input_write) == LMD_TYPE_FUNC) {
            Item input_args[3] = { chunk, encoding, make_js_undefined() };
            js_call_function(input_write, input, input_args, 3);
            if (js_check_exception()) return ItemNull;
        }
    }
    js_readable_compose_bridge_start(env);
    return result;
}

static Item js_readable_compose_bridge_end(Item env_item, Item chunk, Item callback) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item source = env[0];
    Item input = env[2];
    Item end_fn = js_property_get(source, key_end);
    if (get_type_id(end_fn) == LMD_TYPE_FUNC) {
        Item args[2] = { chunk, callback };
        js_call_function(end_fn, source, args, 2);
        if (js_check_exception()) return ItemNull;
    }
    if (input.item != source.item &&
        (get_type_id(input) == LMD_TYPE_MAP || get_type_id(input) == LMD_TYPE_ELEMENT)) {
        Item input_end = js_property_get(input, key_end);
        if (get_type_id(input_end) == LMD_TYPE_FUNC) {
            Item input_args[2] = { chunk, make_js_undefined() };
            js_call_function(input_end, input, input_args, 2);
            if (js_check_exception()) return ItemNull;
        }
    }
    js_readable_compose_bridge_start(env);
    return env[1];
}

static Item js_readable_compose_bridge_cork(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item source = env[0];
    Item cork_fn = js_property_get(source, make_string_item("cork"));
    if (get_type_id(cork_fn) == LMD_TYPE_FUNC)
        js_call_function(cork_fn, source, NULL, 0);
    return env[1];
}

static Item js_readable_compose_bridge_uncork(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item source = env[0];
    Item uncork_fn = js_property_get(source, make_string_item("uncork"));
    if (get_type_id(uncork_fn) == LMD_TYPE_FUNC)
        js_call_function(uncork_fn, source, NULL, 0);
    return env[1];
}

static Item js_readable_compose_bridge_destroy(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item source = env[0];
    Item destroy_fn = js_property_get(source, key_destroy);
    if (get_type_id(destroy_fn) == LMD_TYPE_FUNC) {
        Item args[1] = { err };
        js_call_function(destroy_fn, source, args, 1);
    }
    return env[1];
}

static void js_readable_compose_bridge_start(Item* env) {
    if (!env || js_item_is_true(env[4])) return;
    env[4] = js_bool_item(true);

    Item out = env[1];
    Item input = env[2];
    Item transform = env[3];
    if (get_type_id(transform) != LMD_TYPE_FUNC) return;

    Item args[1] = { input };
    Item composed = js_call_function(transform, make_js_undefined(), args, 1);
    if (js_check_exception()) {
        Item err = js_clear_exception();
        js_stream_destroy(out, err);
        return;
    }

    Item iterator = js_get_async_iterator(composed);
    if (js_check_exception()) {
        Item err = js_clear_exception();
        js_stream_destroy(out, err);
        return;
    }

    Item* pump_env = js_alloc_env(2);
    pump_env[0] = out;
    pump_env[1] = iterator;
    js_readable_from_pump((Item){.item = (uint64_t)(uintptr_t)pump_env});
}

static void js_readable_compose_attach_writable_bridge(Item out, Item source, Item input, Item transform) {
    if (!js_readable_compose_is_duplex_like(source)) return;

    Item* env = js_alloc_env(5);
    env[0] = source;
    env[1] = out;
    env[2] = input;
    env[3] = transform;
    env[4] = js_bool_item(false);

    js_property_set(out, key_writable, js_bool_item(true));
    js_property_set(out, key_writable_state, js_property_get(source, key_writable_state));
    js_property_set(out, key_write,
                    js_new_closure((void*)js_readable_compose_bridge_write, 3, env, 5));
    js_property_set(out, key_end,
                    js_new_closure((void*)js_readable_compose_bridge_end, 2, env, 5));
    js_property_set(out, make_string_item("cork"),
                    js_new_closure((void*)js_readable_compose_bridge_cork, 0, env, 5));
    js_property_set(out, make_string_item("uncork"),
                    js_new_closure((void*)js_readable_compose_bridge_uncork, 0, env, 5));
    js_property_set(out, key_destroy,
                    js_new_closure((void*)js_readable_compose_bridge_destroy, 1, env, 5));
}

static bool js_readable_compose_result_source_failed(Item* env) {
    if (!env) return true;
    Item source = env[2];
    Item err = js_stream_get_stored_error(source);
    if (!js_stream_has_error(err)) return false;
    js_stream_destroy(env[0], err);
    return true;
}

static Item js_readable_compose_result_pump(Item env_item);

static Item js_readable_compose_result_on_step(Item env_item, Item result) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item readable = env[0];
    if (js_item_is_true(js_property_get(readable, key_destroyed))) return make_js_undefined();
    if (js_readable_compose_result_source_failed(env)) return make_js_undefined();

    if (js_iterator_result_done(result)) {
        if (js_check_exception()) {
            Item err = js_clear_exception();
            js_stream_destroy(readable, err);
        } else {
            js_readable_push(readable, ItemNull);
        }
        return make_js_undefined();
    }
    if (js_check_exception()) {
        Item err = js_clear_exception();
        js_stream_destroy(readable, err);
        return make_js_undefined();
    }
    if (js_readable_compose_result_source_failed(env)) return make_js_undefined();

    Item value = js_iterator_result_value(result);
    if (js_check_exception()) {
        Item err = js_clear_exception();
        js_stream_destroy(readable, err);
        return make_js_undefined();
    }
    if (get_type_id(value) == LMD_TYPE_NULL) {
        Item err = js_stream_make_type_error_with_code("ERR_STREAM_NULL_VALUES",
            "May not write null values to stream");
        js_stream_destroy(readable, err);
        return make_js_undefined();
    }
    js_readable_push(readable, value);
    return js_readable_compose_result_pump(env_item);
}

static Item js_readable_compose_result_on_error(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    js_stream_destroy(env[0], err);
    return make_js_undefined();
}

static Item js_readable_compose_result_pump(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item readable = env[0];
    if (js_item_is_true(js_property_get(readable, key_destroyed))) return make_js_undefined();
    if (js_readable_compose_result_source_failed(env)) return make_js_undefined();

    Item step = js_async_iterator_step_result(env[1]);
    if (js_check_exception()) {
        Item err = js_clear_exception();
        js_stream_destroy(readable, err);
        return make_js_undefined();
    }

    Item on_step = js_new_closure((void*)js_readable_compose_result_on_step, 1, env, 3);
    Item on_error = js_new_closure((void*)js_readable_compose_result_on_error, 1, env, 3);
    js_promise_then(step, on_step, on_error);
    return make_js_undefined();
}

static Item js_readable_compose_from_result(Item source, Item composed, Item options) {
    Item opts = js_new_object();
    js_property_set(opts, make_string_item("objectMode"), js_bool_item(true));
    Item out = js_readable_new(opts);
    if (get_type_id(out) != LMD_TYPE_MAP && get_type_id(out) != LMD_TYPE_ELEMENT) return out;

    js_stream_set_readable_object_mode(out, true);
    js_property_set(out, key_writable, js_bool_item(false));
    js_readable_compose_attach_error_forward(source, out);
    if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_ELEMENT)
        js_stream_iter_attach_abort(options, out);

    TypeId composed_type = get_type_id(composed);
    if (composed_type == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(composed);
        for (int64_t i = 0; i < len; i++) {
            Item value = js_array_get_int(composed, i);
            if (get_type_id(value) == LMD_TYPE_NULL) {
                Item err = js_stream_make_type_error_with_code("ERR_STREAM_NULL_VALUES",
                    "May not write null values to stream");
                js_stream_destroy(out, err);
                return out;
            }
            js_readable_push(out, value);
        }
        js_readable_push(out, ItemNull);
        return out;
    }

    if (composed_type == LMD_TYPE_STRING) {
        js_readable_push(out, composed);
        js_readable_push(out, ItemNull);
        return out;
    }

    if (js_readable_from_is_iterable(composed)) {
        Item iterator = js_get_async_iterator(composed);
        if (js_check_exception()) {
            Item err = js_clear_exception();
            js_stream_destroy(out, err);
            return out;
        }
        Item* env = js_alloc_env(3);
        env[0] = out;
        env[1] = iterator;
        env[2] = source;
        js_readable_compose_result_pump((Item){.item = (uint64_t)(uintptr_t)env});
    }

    return out;
}

static Item js_readable_compose(Item self, Item stream, Item options) {
    if (!js_stream_validate_helper_options(options)) return ItemNull;
    if (!js_stream_validate_helper_signal(options)) return ItemNull;

    TypeId stream_type = get_type_id(stream);
    if (stream.item == 0 || stream_type == LMD_TYPE_UNDEFINED) {
        return js_throw_invalid_arg_type("stream", "function or stream", stream);
    }

    if (stream_type == LMD_TYPE_FUNC) {
        if (js_readable_compose_is_duplex_like(self)) {
            Item input_opts = js_new_object();
            js_property_set(input_opts, make_string_item("objectMode"), js_bool_item(true));
            Item input = js_passthrough_new(input_opts);
            if (js_check_exception()) return ItemNull;
            Item out_opts = js_new_object();
            js_property_set(out_opts, make_string_item("objectMode"), js_bool_item(true));
            Item out = js_readable_new(out_opts);
            if (get_type_id(out) == LMD_TYPE_MAP || get_type_id(out) == LMD_TYPE_ELEMENT) {
                js_stream_set_readable_object_mode(out, true);
                js_property_set(out, key_writable, js_bool_item(false));
                js_readable_compose_attach_error_forward(self, out);
                js_readable_compose_attach_writable_bridge(out, self, input, stream);
                if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_ELEMENT)
                    js_stream_iter_attach_abort(options, out);
            }
            return out;
        }
        Item args[1] = { self };
        Item composed = js_call_function(stream, make_js_undefined(), args, 1);
        if (js_check_exception()) return ItemNull;

        return js_readable_compose_from_result(self, composed, options);
    }

    if (js_readable_compose_is_duplex_like(stream)) {
        if (!js_readable_compose_is_duplex_like(self)) {
            js_property_set(stream, key_writable, js_bool_item(false));
        }
        Item* env = js_alloc_env(2);
        env[0] = self;
        env[1] = stream;
        js_next_tick_enqueue(js_new_closure((void*)js_readable_compose_pipe_tick, 0, env, 2));
        return stream;
    }

    if (js_stream_is_stream_like(stream)) {
        return js_throw_type_error_code("ERR_INVALID_ARG_VALUE",
            "The argument 'stream' must be writable and readable.");
    }

    return js_throw_invalid_arg_type("stream", "function or stream", stream);
}

static bool js_stream_consumer_chunk_is_byte_input(Item chunk) {
    TypeId tid = get_type_id(chunk);
    return tid == LMD_TYPE_STRING ||
           js_is_arraybuffer(chunk) ||
           js_stream_chunk_is_arraybuffer_view(chunk) ||
           js_stream_chunk_is_buffer(chunk);
}

static Item js_stream_consumer_buffer_from_array(Item chunks, bool stringify_other) {
    Item parts = js_array_new(0);
    int64_t len = get_type_id(chunks) == LMD_TYPE_ARRAY ? js_array_length(chunks) : 0;
    for (int64_t i = 0; i < len; i++) {
        Item chunk = js_array_get_int(chunks, i);
        Item part = chunk;
        if (!js_stream_consumer_chunk_is_byte_input(part)) {
            if (!stringify_other) {
                js_throw_invalid_arg_type("chunk", "string, Buffer, ArrayBuffer, or ArrayBufferView", part);
                return ItemNull;
            }
            part = js_to_string(part);
            if (js_check_exception()) return ItemNull;
        }
        if (!js_stream_chunk_is_buffer(part)) {
            part = js_buffer_from(part, make_string_item("utf8"), make_js_undefined());
            if (js_check_exception()) return ItemNull;
        }
        js_array_push(parts, part);
    }
    return js_buffer_concat(parts, make_js_undefined());
}

static Item js_stream_consumer_buffer_finish(Item chunks) {
    Item buf = js_stream_consumer_buffer_from_array(chunks, true);
    if (js_check_exception()) return ItemNull;
    return buf;
}

static Item js_stream_consumer_text_finish(Item chunks) {
    Item buf = js_stream_consumer_buffer_from_array(chunks, false);
    if (js_check_exception()) return ItemNull;
    return js_buffer_toString(buf, make_string_item("utf8"),
                              make_js_undefined(), make_js_undefined());
}

static Item js_stream_consumer_json_finish(Item chunks) {
    Item text = js_stream_consumer_text_finish(chunks);
    if (js_check_exception()) return ItemNull;
    return js_json_parse(text);
}

static Item js_stream_consumer_arrayBuffer_finish(Item chunks) {
    Item buf = js_stream_consumer_buffer_from_array(chunks, true);
    if (js_check_exception()) return ItemNull;
    if (!js_is_typed_array(buf)) return js_property_get(buf, make_string_item("buffer"));
    int byte_length = js_typed_array_byte_length(buf);
    if (byte_length < 0) byte_length = 0;
    Item array_buffer = js_arraybuffer_new(byte_length);
    JsArrayBuffer* ab = js_get_arraybuffer_ptr_item(array_buffer);
    void* src = js_typed_array_current_data_ptr(buf);
    if (ab && ab->data && src && byte_length > 0) {
        memcpy(ab->data, src, (size_t)byte_length);
    }
    return array_buffer;
}

static Item js_stream_consumer_bytes_finish(Item chunks) {
    Item array_buffer = js_stream_consumer_arrayBuffer_finish(chunks);
    if (js_check_exception()) return ItemNull;
    Item byte_length = js_property_get(array_buffer, make_string_item("byteLength"));
    int length = get_type_id(byte_length) == LMD_TYPE_INT ? (int)it2i(byte_length) : 0;
    return js_typed_array_new_from_buffer(JS_TYPED_UINT8, array_buffer, 0, length);
}

static Item js_stream_consumer_blob_finish(Item chunks) {
    Item buf = js_stream_consumer_buffer_from_array(chunks, true);
    if (js_check_exception()) return ItemNull;
    Item parts = js_array_new(0);
    js_array_push(parts, buf);
    return js_blob_new(parts, make_js_undefined());
}

static Item js_stream_consumer_finish_value(Item env_item, Item chunks) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    int64_t mode = get_type_id(env[0]) == LMD_TYPE_INT ? it2i(env[0]) : 0;
    switch (mode) {
        case 1: return js_stream_consumer_arrayBuffer_finish(chunks);
        case 2: return js_stream_consumer_buffer_finish(chunks);
        case 3: return js_stream_consumer_bytes_finish(chunks);
        case 4: return js_stream_consumer_json_finish(chunks);
        case 5: return js_stream_consumer_blob_finish(chunks);
        default: return js_stream_consumer_text_finish(chunks);
    }
}

static Item js_stream_consumer(Item readable, int64_t mode) {
    if (js_item_is_true(js_property_get(readable, make_string_item("__web_readable__")))) {
        if (js_item_is_true(js_property_get(readable, make_string_item("__web_disturbed__")))) {
            return js_promise_reject(js_stream_make_error_with_code("ERR_INVALID_STATE",
                "ReadableStream is locked or disturbed"));
        }
        js_property_set(readable, make_string_item("__web_disturbed__"), js_bool_item(true));
    }
    Item promise = js_readable_toArray(readable, make_js_undefined());
    Item* env = js_alloc_env(1);
    env[0] = (Item){.item = i2it(mode)};
    Item finish = js_new_closure((void*)js_stream_consumer_finish_value, 1, env, 1);
    return js_promise_then(promise, finish, make_js_undefined());
}

static Item js_stream_consumer_text(Item readable) {
    return js_stream_consumer(readable, 0);
}

static Item js_stream_consumer_arrayBuffer(Item readable) {
    return js_stream_consumer(readable, 1);
}

static Item js_stream_consumer_buffer(Item readable) {
    return js_stream_consumer(readable, 2);
}

static Item js_stream_consumer_bytes(Item readable) {
    return js_stream_consumer(readable, 3);
}

static Item js_stream_consumer_json(Item readable) {
    return js_stream_consumer(readable, 4);
}

static Item js_stream_consumer_blob(Item readable) {
    return js_stream_consumer(readable, 5);
}

static Item js_stream_iter_to_readable(Item source) {
    if (js_stream_is_stream_like(source)) return source;
    return js_readable_from(source);
}

static Item js_stream_iter_result(Item value, bool done) {
    Item result = js_new_object();
    js_property_set(result, make_string_item("value"), value);
    js_property_set(result, make_string_item("done"), js_bool_item(done));
    return result;
}

static Item js_stream_iter_identity(void) {
    return js_get_this();
}

static bool js_stream_iter_has_method(Item value, const char* name, int len) {
    TypeId tid = get_type_id(value);
    if (tid != LMD_TYPE_MAP && tid != LMD_TYPE_ELEMENT && tid != LMD_TYPE_ARRAY)
        return false;
    Item method = js_property_get(value, make_string_item(name, len));
    return get_type_id(method) == LMD_TYPE_FUNC;
}

static bool js_stream_iter_apply_protocol(Item* source, const char* symbol_name) {
    Item key = js_symbol_for(make_string_item(symbol_name));
    if (js_check_exception()) return false;
    Item method = js_property_get(*source, key);
    if (get_type_id(method) != LMD_TYPE_FUNC) return true;
    Item next = js_call_function(method, *source, NULL, 0);
    if (js_check_exception()) return false;
    *source = next;
    return true;
}

static bool js_stream_iter_source_is_single_chunk(Item source) {
    TypeId tid = get_type_id(source);
    return tid == LMD_TYPE_STRING || js_is_arraybuffer(source) ||
           js_stream_chunk_is_arraybuffer_view(source) ||
           js_stream_chunk_is_buffer(source);
}

static bool js_stream_iter_append_normalized(Item batch, Item value);

static Item js_stream_iter_to_byte_chunk(Item value) {
    if (get_type_id(value) == LMD_TYPE_STRING) {
        return js_buffer_from(value, make_string_item("utf8"), make_js_undefined());
    }
    if (js_is_arraybuffer(value)) {
        int length = js_arraybuffer_byte_length(value);
        return js_typed_array_new_from_buffer(JS_TYPED_UINT8, value, 0, length);
    }
    if (js_stream_chunk_is_buffer(value)) return value;
    if (js_stream_chunk_is_arraybuffer_view(value)) {
        if (js_is_typed_array(value)) return value;
        return js_buffer_from(value, make_js_undefined(), make_js_undefined());
    }
    return ItemNull;
}

static bool js_stream_iter_append_array_values(Item batch, Item array) {
    int64_t len = js_array_length(array);
    for (int64_t i = 0; i < len; i++) {
        if (!js_stream_iter_append_normalized(batch, js_array_get_int(array, i)))
            return false;
    }
    return true;
}

static bool js_stream_iter_append_normalized(Item batch, Item value) {
    if (!js_stream_iter_apply_protocol(&value, "Stream.toStreamable"))
        return false;

    if (get_type_id(value) == LMD_TYPE_ARRAY)
        return js_stream_iter_append_array_values(batch, value);

    Item chunk = js_stream_iter_to_byte_chunk(value);
    if (chunk.item != 0 && get_type_id(chunk) != LMD_TYPE_NULL) {
        if (js_check_exception()) return false;
        js_array_push(batch, chunk);
        return true;
    }

    js_throw_invalid_arg_type("chunk", "string, Buffer, ArrayBuffer, or ArrayBufferView", value);
    return false;
}

static Item js_stream_iter_batch_next(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[2]))
        return js_stream_iter_result(make_js_undefined(), true);

    Item batch = js_array_new(0);
    if (js_item_is_true(env[3])) {
        env[2] = js_bool_item(true);
        if (!js_stream_iter_append_normalized(batch, env[0])) return ItemNull;
        return js_stream_iter_result(batch, false);
    }

    bool collect_all = js_item_is_true(env[4]);
    while (collect_all || js_array_length(batch) == 0) {
        Item value = js_iterator_step(env[1]);
        if (js_check_exception()) return ItemNull;
        if (value.item == JS_ITER_DONE_SENTINEL) {
            env[2] = js_bool_item(true);
            break;
        }
        if (!js_stream_iter_append_normalized(batch, value)) return ItemNull;
    }

    if (js_array_length(batch) == 0)
        return js_stream_iter_result(make_js_undefined(), true);
    return js_stream_iter_result(batch, false);
}

static Item js_stream_iter_make_batch_iterable(Item source, bool async_iterable, bool collect_all_sync) {
    Item iterator = make_js_undefined();
    bool single = js_stream_iter_source_is_single_chunk(source);
    if (!single) {
        iterator = js_get_iterator(source);
        if (js_check_exception()) return ItemNull;
    }

    Item* env = js_alloc_env(5);
    env[0] = source;
    env[1] = iterator;
    env[2] = js_bool_item(false);
    env[3] = js_bool_item(single);
    env[4] = js_bool_item(get_type_id(source) == LMD_TYPE_ARRAY || collect_all_sync);

    Item obj = js_new_object();
    js_property_set(obj, make_string_item("next"), js_new_closure((void*)js_stream_iter_batch_next, 0, env, 5));
    js_property_set(obj, make_string_item("__sym_1"), js_new_function((void*)js_stream_iter_identity, 0));
    if (async_iterable)
        js_property_set(obj, make_string_item("__sym_5"), js_new_function((void*)js_stream_iter_identity, 0));
    return obj;
}

static bool js_stream_iter_value_can_sync(Item source) {
    TypeId tid = get_type_id(source);
    if (source.item == ITEM_JS_UNDEFINED || tid == LMD_TYPE_NULL)
        return false;
    if (js_is_async_generator(source)) return false;
    if (js_stream_iter_source_is_single_chunk(source)) return true;
    if (tid == LMD_TYPE_ARRAY) return true;
    if (tid == LMD_TYPE_MAP || tid == LMD_TYPE_ELEMENT)
        return js_stream_iter_has_method(source, "__sym_1", 7);
    return false;
}

static Item js_stream_iter_from(Item source) {
    if (!js_stream_iter_apply_protocol(&source, "Stream.toAsyncStreamable"))
        return ItemNull;
    if (js_stream_iter_has_method(source, "__sym_5", 7) && !js_stream_iter_has_method(source, "__sym_1", 7))
        return js_stream_iter_to_readable(source);
    if (!js_stream_iter_apply_protocol(&source, "Stream.toStreamable"))
        return ItemNull;
    if (js_stream_iter_value_can_sync(source))
        return js_stream_iter_make_batch_iterable(source, true, false);
    return js_stream_iter_to_readable(source);
}

static Item js_stream_iter_fromSync(Item source) {
    if (!js_stream_iter_apply_protocol(&source, "Stream.toStreamable"))
        return ItemNull;
    if (!js_stream_iter_value_can_sync(source)) {
        js_throw_invalid_arg_type("source", "a sync streamable value", source);
        return ItemNull;
    }
    return js_stream_iter_make_batch_iterable(source, false, true);
}

static bool js_stream_iter_is_byte_array(Item value) {
    if (get_type_id(value) != LMD_TYPE_ARRAY) return false;
    int64_t len = js_array_length(value);
    for (int64_t i = 0; i < len; i++) {
        if (get_type_id(js_array_get_int(value, i)) != LMD_TYPE_INT) return false;
    }
    return true;
}

static Item js_stream_iter_sync_array(Item source) {
    Item chunks = js_array_new(0);
    if (get_type_id(source) == LMD_TYPE_STRING || js_stream_chunk_is_arraybuffer_view(source) ||
        js_stream_chunk_is_buffer(source)) {
        js_array_push(chunks, source);
        return chunks;
    }
    if (get_type_id(source) == LMD_TYPE_ARRAY) return source;
    if (js_stream_is_stream_like(source)) {
        while (true) {
            Item chunk = js_readable_read(source);
            if (js_stream_async_iterator_has_value(chunk)) {
                js_array_push(chunks, chunk);
                continue;
            }
            break;
        }
        return chunks;
    }

    Item iterator = js_get_iterator(source);
    if (js_check_exception()) return ItemNull;
    while (true) {
        Item value = js_iterator_step(iterator);
        if (js_check_exception()) return ItemNull;
        if (value.item == JS_ITER_DONE_SENTINEL) break;
        js_array_push(chunks, value);
    }
    return chunks;
}

static Item js_stream_iter_flatten_for_bytes(Item chunks) {
    Item flat = js_array_new(0);
    int64_t len = get_type_id(chunks) == LMD_TYPE_ARRAY ? js_array_length(chunks) : 0;
    for (int64_t i = 0; i < len; i++) {
        Item chunk = js_array_get_int(chunks, i);
        if (get_type_id(chunk) == LMD_TYPE_ARRAY && !js_stream_iter_is_byte_array(chunk)) {
            int64_t inner_len = js_array_length(chunk);
            for (int64_t j = 0; j < inner_len; j++) {
                js_array_push(flat, js_array_get_int(chunk, j));
            }
        } else {
            js_array_push(flat, chunk);
        }
    }
    return flat;
}

static Item js_stream_iter_check_limit(Item chunks, Item options) {
    if (get_type_id(options) != LMD_TYPE_MAP && get_type_id(options) != LMD_TYPE_ELEMENT)
        return make_js_undefined();
    Item limit = js_property_get(options, make_string_item("limit"));
    if (get_type_id(limit) != LMD_TYPE_INT) return make_js_undefined();
    int64_t total = 0;
    int64_t len = get_type_id(chunks) == LMD_TYPE_ARRAY ? js_array_length(chunks) : 0;
    for (int64_t i = 0; i < len; i++) {
        total += js_stream_iter_chunk_byte_length(js_array_get_int(chunks, i));
        if (total > it2i(limit)) {
            Item err = js_stream_make_error_with_code("ERR_OUT_OF_RANGE", "stream/iter limit exceeded");
            js_property_set(err, make_string_item("name"), make_string_item("RangeError"));
            return err;
        }
    }
    return make_js_undefined();
}

static Item js_stream_iter_textSync(Item source, Item options) {
    Item chunks = js_stream_iter_sync_array(source);
    if (js_check_exception()) return ItemNull;
    chunks = js_stream_iter_flatten_for_bytes(chunks);
    Item err = js_stream_iter_check_limit(chunks, options);
    if (js_stream_has_error(err)) {
        js_throw_value(err);
        return ItemNull;
    }
    return js_stream_consumer_text_finish(chunks);
}

static Item js_stream_iter_bytesSync(Item source, Item options) {
    Item chunks = js_stream_iter_sync_array(source);
    if (js_check_exception()) return ItemNull;
    chunks = js_stream_iter_flatten_for_bytes(chunks);
    Item err = js_stream_iter_check_limit(chunks, options);
    if (js_stream_has_error(err)) {
        js_throw_value(err);
        return ItemNull;
    }
    return js_stream_consumer_bytes_finish(chunks);
}

static Item js_stream_iter_arrayBufferSync(Item source, Item options) {
    Item chunks = js_stream_iter_sync_array(source);
    if (js_check_exception()) return ItemNull;
    chunks = js_stream_iter_flatten_for_bytes(chunks);
    Item err = js_stream_iter_check_limit(chunks, options);
    if (js_stream_has_error(err)) {
        js_throw_value(err);
        return ItemNull;
    }
    return js_stream_consumer_arrayBuffer_finish(chunks);
}

static Item js_stream_iter_arraySync(Item source, Item options) {
    Item chunks = js_stream_iter_sync_array(source);
    if (js_check_exception()) return ItemNull;
    Item err = js_stream_iter_check_limit(chunks, options);
    if (js_stream_has_error(err)) {
        js_throw_value(err);
        return ItemNull;
    }
    return chunks;
}

static Item js_stream_iter_consumer_done(Item env_item, Item chunks) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    int64_t mode = get_type_id(env[0]) == LMD_TYPE_INT ? it2i(env[0]) : 0;
    Item options = env[1];
    if (mode != 3) chunks = js_stream_iter_flatten_for_bytes(chunks);
    Item err = js_stream_iter_check_limit(chunks, options);
    if (js_stream_has_error(err)) return js_promise_reject(err);
    switch (mode) {
        case 1: return js_stream_consumer_bytes_finish(chunks);
        case 2: return js_stream_consumer_arrayBuffer_finish(chunks);
        case 3: return chunks;
        default: return js_stream_consumer_text_finish(chunks);
    }
}

static Item js_stream_iter_consumer_async(Item source, Item options, int64_t mode) {
    if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_ELEMENT) {
        Item signal = js_property_get(options, make_string_item("signal"));
        if (js_stream_is_abort_signal(signal)) {
            Item aborted = js_property_get(signal, make_string_item("aborted"));
            if (get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted)) {
                Item reason = js_property_get(signal, make_string_item("reason"));
                if (reason.item == 0 || get_type_id(reason) == LMD_TYPE_UNDEFINED)
                    reason = js_stream_iter_make_abort_error();
                return js_promise_reject(reason);
            }
        }
    }
    Item readable = js_stream_iter_to_readable(source);
    Item promise = js_readable_toArray(readable, options);
    Item* env = js_alloc_env(2);
    env[0] = (Item){.item = i2it(mode)};
    env[1] = options;
    Item finish = js_new_closure((void*)js_stream_iter_consumer_done, 1, env, 2);
    return js_promise_then(promise, finish, make_js_undefined());
}

static Item js_stream_iter_text_consume(Item source, Item options) {
    return js_stream_iter_consumer_async(source, options, 0);
}

static Item js_stream_iter_bytes(Item source, Item options) {
    return js_stream_iter_consumer_async(source, options, 1);
}

static Item js_stream_iter_arrayBuffer(Item source, Item options) {
    return js_stream_iter_consumer_async(source, options, 2);
}

static Item js_stream_iter_array(Item source, Item options) {
    return js_stream_iter_consumer_async(source, options, 3);
}

static Item js_stream_iter_tap_callback(Item env_item, Item chunks) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return chunks;
    Item callback = env[0];
    Item result = js_call_function(callback, make_js_undefined(), &chunks, 1);
    if (js_check_exception()) return ItemNull;
    (void)result;
    return chunks;
}

static Item js_stream_iter_tap_async_done(Item chunks) {
    return chunks;
}

static Item js_stream_iter_tap_async_callback(Item env_item, Item chunks) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return js_promise_resolve(chunks);
    Item callback = env[0];
    Item result = js_call_function(callback, make_js_undefined(), &chunks, 1);
    if (js_check_exception()) return ItemNull;
    Item promise = js_promise_resolve(result);
    Item done = js_bind_function(js_new_function((void*)js_stream_iter_tap_async_done, 1),
                                 make_js_undefined(), &chunks, 1);
    return js_promise_then(promise, done, make_js_undefined());
}

static Item js_stream_iter_tapSync(Item callback) {
    if (get_type_id(callback) != LMD_TYPE_FUNC)
        return js_throw_invalid_arg_type("fn", "function", callback);
    Item* env = js_alloc_env(1);
    env[0] = callback;
    return js_new_closure((void*)js_stream_iter_tap_callback, 1, env, 1);
}

static Item js_stream_iter_tap(Item callback) {
    if (get_type_id(callback) != LMD_TYPE_FUNC)
        return js_throw_invalid_arg_type("fn", "function", callback);
    Item* env = js_alloc_env(1);
    env[0] = callback;
    return js_new_closure((void*)js_stream_iter_tap_async_callback, 1, env, 1);
}

static bool js_stream_iter_is_transform_object(Item transform, Item* method) {
    TypeId tid = get_type_id(transform);
    if (tid != LMD_TYPE_MAP && tid != LMD_TYPE_ELEMENT) return false;
    Item fn = js_property_get(transform, make_string_item("transform"));
    if (get_type_id(fn) != LMD_TYPE_FUNC) return false;
    if (method) *method = fn;
    return true;
}

static bool js_stream_iter_transform_is_present(Item transform) {
    return transform.item != 0 && get_type_id(transform) != LMD_TYPE_UNDEFINED;
}

static bool js_stream_iter_validate_transform(Item transform) {
    if (!js_stream_iter_transform_is_present(transform)) return true;
    if (get_type_id(transform) == LMD_TYPE_FUNC) return true;
    if (js_stream_iter_is_transform_object(transform, NULL)) return true;
    js_throw_invalid_arg_type("transform", "function or transform object", transform);
    return false;
}

static void js_stream_iter_append_transform_value(Item output, Item value) {
    TypeId tid = get_type_id(value);
    if (value.item == 0 || tid == LMD_TYPE_UNDEFINED || tid == LMD_TYPE_NULL) return;
    js_array_push(output, value);
}

static Item js_stream_iter_transform_input(Item chunks) {
    Item input = js_array_new(0);
    int64_t len = get_type_id(chunks) == LMD_TYPE_ARRAY ? js_array_length(chunks) : 0;
    for (int64_t i = 0; i < len; i++) {
        js_array_push(input, js_array_get_int(chunks, i));
    }
    js_array_push(input, ItemNull);
    return input;
}

static Item js_stream_iter_apply_stateless_transform(Item chunks, Item transform) {
    Item output = js_array_new(0);
    int64_t len = get_type_id(chunks) == LMD_TYPE_ARRAY ? js_array_length(chunks) : 0;
    for (int64_t i = 0; i < len; i++) {
        Item batch = js_array_get_int(chunks, i);
        Item result = js_call_function(transform, make_js_undefined(), &batch, 1);
        if (js_check_exception()) return ItemNull;
        js_stream_iter_append_transform_value(output, result);
    }
    Item flush = ItemNull;
    Item flush_result = js_call_function(transform, make_js_undefined(), &flush, 1);
    if (js_check_exception()) return ItemNull;
    js_stream_iter_append_transform_value(output, flush_result);
    return output;
}

static Item js_stream_iter_apply_stateful_transform(Item chunks, Item transform_obj, Item method) {
    Item input = js_stream_iter_transform_input(chunks);
    Item result = js_call_function(method, transform_obj, &input, 1);
    if (js_check_exception()) return ItemNull;
    Item iterator = js_get_iterator(result);
    if (js_check_exception()) return ItemNull;
    Item output = js_array_new(0);
    while (true) {
        Item value = js_iterator_step(iterator);
        if (js_check_exception()) return ItemNull;
        if (value.item == JS_ITER_DONE_SENTINEL) break;
        js_stream_iter_append_transform_value(output, value);
    }
    return output;
}

static Item js_stream_iter_apply_sync_transform(Item chunks, Item transform) {
    if (!js_stream_iter_transform_is_present(transform)) return chunks;
    if (!js_stream_iter_validate_transform(transform)) return ItemNull;
    if (get_type_id(transform) == LMD_TYPE_FUNC)
        return js_stream_iter_apply_stateless_transform(chunks, transform);
    Item method = make_js_undefined();
    if (js_stream_iter_is_transform_object(transform, &method))
        return js_stream_iter_apply_stateful_transform(chunks, transform, method);
    return chunks;
}

static Item js_stream_iter_pullSync(Item source, Item transform1, Item transform2, Item transform3,
                                    Item transform4, Item transform5, Item transform6, Item transform7) {
    Item chunks = js_stream_iter_sync_array(source);
    if (js_check_exception()) return ItemNull;
    Item transforms[7] = { transform1, transform2, transform3, transform4,
                           transform5, transform6, transform7 };
    for (int i = 0; i < 7; i++) {
        chunks = js_stream_iter_apply_sync_transform(chunks, transforms[i]);
        if (js_check_exception()) return ItemNull;
    }
    return chunks;
}

static Item js_stream_iter_pull_transform_done(Item env_item, Item chunks) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return chunks;
    Item transform = env[0];
    if (get_type_id(transform) != LMD_TYPE_FUNC) return chunks;
    return js_call_function(transform, make_js_undefined(), &chunks, 1);
}

static Item js_stream_iter_pull(Item source, Item transform) {
    Item readable = js_stream_iter_to_readable(source);
    Item promise = js_readable_toArray(readable, make_js_undefined());
    Item* env = js_alloc_env(1);
    env[0] = transform;
    Item done = js_new_closure((void*)js_stream_iter_pull_transform_done, 1, env, 1);
    return js_promise_then(promise, done, make_js_undefined());
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

static Item js_stream_iter_pipe_next(Item env_item);

static Item js_stream_iter_pipe_reject(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[6])) return make_js_undefined();
    env[6] = js_bool_item(true);
    Item reject = env[5];
    if (get_type_id(reject) == LMD_TYPE_FUNC) {
        Item args[1] = { err };
        js_call_function(reject, make_js_undefined(), args, 1);
    }
    return make_js_undefined();
}

static Item js_stream_iter_pipe_resolve(Item env_item, Item value) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[6])) return make_js_undefined();
    env[6] = js_bool_item(true);
    Item resolve = env[4];
    if (get_type_id(resolve) == LMD_TYPE_FUNC) {
        Item args[1] = { value };
        js_call_function(resolve, make_js_undefined(), args, 1);
    }
    return make_js_undefined();
}

static Item js_stream_iter_pipe_after_write(Item env_item, Item ignored) {
    (void)ignored;
    return js_stream_iter_pipe_next(env_item);
}

static Item js_stream_iter_pipe_finish(Item env_item, Item result) {
    return js_stream_iter_pipe_resolve(env_item, result);
}

static Item js_stream_iter_pipe_end(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[6])) return make_js_undefined();
    Item writer = env[1];
    Item end_sync = js_property_get(writer, make_string_item("endSync"));
    if (get_type_id(end_sync) == LMD_TYPE_FUNC) {
        Item result = js_call_function(end_sync, writer, NULL, 0);
        if (js_check_exception()) {
            Item err = js_clear_exception();
            return js_stream_iter_pipe_reject(env_item, err);
        }
        return js_stream_iter_pipe_resolve(env_item, result);
    }
    Item end_fn = js_property_get(writer, make_string_item("end"));
    if (get_type_id(end_fn) == LMD_TYPE_FUNC) {
        Item result = js_call_function(end_fn, writer, NULL, 0);
        if (js_check_exception()) {
            Item err = js_clear_exception();
            return js_stream_iter_pipe_reject(env_item, err);
        }
        Item promise = js_promise_resolve(result);
        Item on_done = js_new_closure((void*)js_stream_iter_pipe_finish, 1, env, 7);
        Item on_error = js_new_closure((void*)js_stream_iter_pipe_reject, 1, env, 7);
        js_promise_then(promise, on_done, on_error);
        return make_js_undefined();
    }
    return js_stream_iter_pipe_resolve(env_item, make_js_undefined());
}

static Item js_stream_iter_pipe_write(Item env_item, Item chunk) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[6])) return make_js_undefined();
    Item err = make_js_undefined();
    if (js_stream_iter_signal_aborted(env[3], &err)) {
        return js_stream_iter_pipe_reject(env_item, err);
    }

    Item transform = env[2];
    if (get_type_id(transform) == LMD_TYPE_FUNC) {
        chunk = js_call_function(transform, make_js_undefined(), &chunk, 1);
        if (js_check_exception()) {
            Item thrown = js_clear_exception();
            return js_stream_iter_pipe_reject(env_item, thrown);
        }
    }

    Item writer = env[1];
    Item write_sync = js_property_get(writer, make_string_item("writeSync"));
    if (get_type_id(write_sync) == LMD_TYPE_FUNC) {
        js_call_function(write_sync, writer, &chunk, 1);
        if (js_check_exception()) {
            Item thrown = js_clear_exception();
            return js_stream_iter_pipe_reject(env_item, thrown);
        }
        return js_stream_iter_pipe_next(env_item);
    }
    Item write_fn = js_property_get(writer, make_string_item("write"));
    if (get_type_id(write_fn) == LMD_TYPE_FUNC) {
        Item result = js_call_function(write_fn, writer, &chunk, 1);
        if (js_check_exception()) {
            Item thrown = js_clear_exception();
            return js_stream_iter_pipe_reject(env_item, thrown);
        }
        Item promise = js_promise_resolve(result);
        Item on_done = js_new_closure((void*)js_stream_iter_pipe_after_write, 1, env, 7);
        Item on_error = js_new_closure((void*)js_stream_iter_pipe_reject, 1, env, 7);
        js_promise_then(promise, on_done, on_error);
        return make_js_undefined();
    }
    return js_stream_iter_pipe_reject(env_item,
        js_stream_make_type_error_with_code("ERR_INVALID_ARG_TYPE", "writer.write is not a function"));
}

static Item js_stream_iter_pipe_step(Item env_item, Item result) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[6])) return make_js_undefined();
    Item err = make_js_undefined();
    if (js_stream_iter_signal_aborted(env[3], &err)) {
        return js_stream_iter_pipe_reject(env_item, err);
    }
    if (js_iterator_result_done(result)) return js_stream_iter_pipe_end(env_item);
    return js_stream_iter_pipe_write(env_item, js_iterator_result_value(result));
}

static Item js_stream_iter_pipe_next(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[6])) return make_js_undefined();
    Item err = make_js_undefined();
    if (js_stream_iter_signal_aborted(env[3], &err)) {
        return js_stream_iter_pipe_reject(env_item, err);
    }
    Item step = js_async_iterator_step_result(env[0]);
    if (js_check_exception()) {
        Item thrown = js_clear_exception();
        return js_stream_iter_pipe_reject(env_item, thrown);
    }
    step = js_promise_resolve(step);
    Item on_step = js_new_closure((void*)js_stream_iter_pipe_step, 1, env, 7);
    Item on_error = js_new_closure((void*)js_stream_iter_pipe_reject, 1, env, 7);
    js_promise_then(step, on_step, on_error);
    return make_js_undefined();
}

static Item js_stream_iter_pipe_abort(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[6])) return make_js_undefined();
    Item signal = env[7];
    Item reason = js_property_get(signal, make_string_item("reason"));
    if (reason.item == 0 || get_type_id(reason) == LMD_TYPE_UNDEFINED) reason = js_stream_iter_make_abort_error();
    return js_stream_iter_pipe_reject(env_item, reason);
}

static void js_stream_iter_pipe_attach_abort(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return;
    Item options = env[3];
    if (get_type_id(options) != LMD_TYPE_MAP && get_type_id(options) != LMD_TYPE_ELEMENT) return;
    Item signal = js_property_get(options, make_string_item("signal"));
    if (get_type_id(signal) != LMD_TYPE_MAP && get_type_id(signal) != LMD_TYPE_ELEMENT) return;
    env[7] = signal;
    Item add_event = js_property_get(signal, make_string_item("addEventListener"));
    if (get_type_id(add_event) != LMD_TYPE_FUNC) return;
    Item listener = js_new_closure((void*)js_stream_iter_pipe_abort, 0, env, 8);
    Item args[2] = { make_string_item("abort"), listener };
    js_call_function(add_event, signal, args, 2);
}

static Item js_stream_iter_pipeTo(Item source, Item transform_or_writer, Item writer_or_options, Item maybe_options) {
    Item transform = make_js_undefined();
    Item writer = transform_or_writer;
    Item options = writer_or_options;
    if (get_type_id(transform_or_writer) == LMD_TYPE_FUNC &&
        (get_type_id(maybe_options) == LMD_TYPE_MAP || get_type_id(maybe_options) == LMD_TYPE_ELEMENT)) {
        transform = transform_or_writer;
        writer = writer_or_options;
        options = maybe_options;
    }

    Item capability = js_promise_with_resolvers();
    if (js_check_exception()) return ItemNull;
    Item promise = js_property_get(capability, make_string_item("promise"));
    Item err = make_js_undefined();
    if (js_stream_iter_signal_aborted(options, &err)) return js_promise_reject(err);

    Item iterator = js_get_async_iterator(source);
    if (js_check_exception()) return js_promise_reject(js_clear_exception());

    Item* env = js_alloc_env(8);
    env[0] = iterator;
    env[1] = writer;
    env[2] = transform;
    env[3] = options;
    env[4] = js_property_get(capability, make_string_item("resolve"));
    env[5] = js_property_get(capability, make_string_item("reject"));
    env[6] = js_bool_item(false);
    env[7] = make_js_undefined();
    Item env_item = (Item){.item = (uint64_t)(uintptr_t)env};
    js_stream_iter_pipe_attach_abort(env_item);
    js_stream_iter_pipe_next(env_item);
    return promise;
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

static Item js_web_writable_get_writer(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return js_new_object();
    return env[0];
}

extern "C" Item js_transform_stream_new(Item transformer) {
    ensure_keys();
    Item stream_transform = make_js_undefined();
    if (get_type_id(transformer) == LMD_TYPE_FUNC) {
        stream_transform = transformer;
    } else if (get_type_id(transformer) == LMD_TYPE_MAP || get_type_id(transformer) == LMD_TYPE_ELEMENT) {
        Item transform_method = js_property_get(transformer, make_string_item("transform"));
        if (get_type_id(transform_method) == LMD_TYPE_FUNC) {
            stream_transform = js_bind_function(transform_method, transformer, NULL, 0);
        }
    }

    Item pair = js_stream_iter_push(stream_transform);
    if (js_check_exception()) return ItemNull;
    Item writer = js_property_get(pair, make_string_item("writer"));
    Item readable = js_property_get(pair, make_string_item("readable"));
    js_property_set(readable, make_string_item("__web_readable__"), js_bool_item(true));
    js_property_set(writer, make_string_item("close"),
                    js_new_function((void*)js_stream_iter_writer_end, 0));
    js_property_set(writer, make_string_item("abort"),
                    js_new_function((void*)js_stream_iter_writer_fail, 1));

    Item writable = js_writable_stream_new(make_js_undefined());
    Item* env = js_alloc_env(1);
    env[0] = writer;
    js_property_set(writable, make_string_item("__writer__"), writer);
    js_property_set(writable, make_string_item("getWriter"),
                    js_new_closure((void*)js_web_writable_get_writer, 0, env, 1));

    Item obj = js_new_object();
    js_property_set(obj, make_string_item("readable"), readable);
    js_property_set(obj, make_string_item("writable"), writable);
    return obj;
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

static Item js_stream_abort_signal_reason(Item signal) {
    Item reason = js_property_get(signal, make_string_item("reason"));
    if (reason.item == 0 || get_type_id(reason) == LMD_TYPE_UNDEFINED) {
        reason = js_stream_iter_make_abort_error();
    }
    return reason;
}

static Item js_stream_abort_signal_destroy_stream(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item signal = env[0];
    Item stream = env[1];
    js_stream_destroy(stream, js_stream_abort_signal_reason(signal));
    return make_js_undefined();
}

static Item js_stream_attach_abort_signal(Item signal, Item stream) {
    Item aborted = js_property_get(signal, make_string_item("aborted"));
    if (get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted)) {
        js_stream_destroy(stream, js_stream_abort_signal_reason(signal));
        return stream;
    }

    Item add_event = js_property_get(signal, make_string_item("addEventListener"));
    if (get_type_id(add_event) != LMD_TYPE_FUNC) return stream;
    Item* env = js_alloc_env(2);
    env[0] = signal;
    env[1] = stream;
    Item listener = js_new_closure((void*)js_stream_abort_signal_destroy_stream, 0, env, 2);
    Item args[2] = { make_string_item("abort"), listener };
    js_call_function(add_event, signal, args, 2);
    return stream;
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
    Item pipe_event = make_string_item("pipe");
    Item emit_fn = js_property_get(dest, key_emit);
    if (get_type_id(emit_fn) == LMD_TYPE_FUNC) {
        Item emit_args[2] = {pipe_event, self};
        js_call_function(emit_fn, dest, emit_args, 2);
    } else {
        js_stream_emit(dest, pipe_event, self);
    }

    if (get_type_id(js_property_get(self, key_readable_state)) != LMD_TYPE_MAP) {
        return js_legacy_stream_pipe(self, dest);
    }

    // set up data listener that writes to dest
    // store dest reference
    js_property_set(self, make_string_item("__pipe_dest__"), dest);
    js_stream_set_flowing(self, true);
    Item pipes = js_readable_pipes(self);
    if (get_type_id(pipes) == LMD_TYPE_ARRAY) {
        js_array_push(pipes, dest);
        if (js_array_length(pipes) > 1) {
            Item state = js_property_get(self, key_readable_state);
            Item current = js_stream_await_drain_writers(state);
            if (current.item == 0 ||
                get_type_id(current) == LMD_TYPE_NULL ||
                get_type_id(current) == LMD_TYPE_UNDEFINED) {
                js_property_set(state, make_string_item("awaitDrainWriters"),
                                js_stream_make_empty_await_drain_set());
            }
        }
    }
    js_readable_add_pipe_data_event(self);
    Item* drain_env = js_alloc_env(2);
    drain_env[0] = self;
    drain_env[1] = dest;
    Item drain_listener = js_new_closure((void*)js_readable_pipe_on_drain, 0, drain_env, 2);
    Item drain_args[2] = {make_string_item("drain"), drain_listener};
    Item on_fn = js_property_get(dest, key_on);
    if (get_type_id(on_fn) == LMD_TYPE_FUNC) {
        js_call_function(on_fn, dest, drain_args, 2);
    } else {
        js_stream_on(dest, drain_args[0], drain_listener);
    }

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
                    js_stream_await_drain_add(self, dest);
                    js_stream_set_flowing(self, false);
                    js_property_set(self, key_paused, js_bool_item(true));
                    js_stream_schedule_read(self);
                    if (!js_stream_source_keeps_pipe_on_backpressure(self)) {
                        js_property_set(self, make_string_item("__piped__"), js_bool_item(false));
                        js_property_set(self, make_string_item("__pipe_dest__"), make_js_undefined());
                    }
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
    js_stream_schedule_resume(self);

    return dest;
}

// destroy([err]) — destroy stream
extern "C" Item js_stream_destroy(Item self, Item err) {
    ensure_keys();
    if (js_item_is_true(js_property_get(self, key_destroyed))) {
        js_stream_invoke_destroy_callback(self, err);
        return self;
    }
    if (js_item_is_true(js_property_get(self, key_flowing))) {
        js_stream_flush_buffered_data(self);
    }
    bool readable_aborted = js_item_is_true(js_property_get(self, key_readable)) &&
                            !js_item_is_true(js_property_get(self, key_end_emitted));
    Item writable_state = js_property_get(self, key_writable_state);
    bool writable_aborted = get_type_id(writable_state) == LMD_TYPE_MAP &&
                            !js_item_is_true(js_property_get(self, key_finish_emitted));
    js_stream_mark_destroyed(self);
    js_property_set(self, make_string_item("readableAborted"), js_bool_item(readable_aborted));
    js_property_set(self, make_string_item("writableAborted"), js_bool_item(writable_aborted));

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
        js_property_set(self, key_destroy_pending, js_bool_item(true));
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
                js_property_set(self, key_destroy_pending, js_bool_item(false));
                if (js_stream_has_callback_error(cb_err)) {
                    js_property_set(self, make_string_item("__error__"), cb_err);
                    // Synchronous _destroy(cb) errors have the same iterator
                    // contract as async callbacks: pending next() observes cb_err.
                    js_stream_async_iterators_drain(self, cb_err);
                    js_stream_schedule_error_immediate(self, cb_err);
                }
                js_stream_invoke_destroy_callback(self, cb_err);
                js_stream_schedule_close_immediate(self);
            }
            return self;
        }
        js_property_set(self, key_destroy_pending, js_bool_item(false));
    }

    if (err.item != 0 && get_type_id(err) != LMD_TYPE_UNDEFINED &&
        get_type_id(err) != LMD_TYPE_NULL) {
        js_stream_schedule_error_immediate(self, err);
    }
    js_stream_invoke_destroy_callback(self, err);
    js_stream_schedule_close_immediate(self);
    return self;
}

// resume() — start flowing mode, call _read if set
extern "C" Item js_readable_resume(Item self) {
    ensure_keys();
    js_stream_set_flowing(self, true);
    js_property_set(self, key_paused, js_bool_item(false));
    js_stream_schedule_resume(self);
    return self;
}

// pause() — stop flowing mode
extern "C" Item js_readable_pause(Item self) {
    ensure_keys();
    js_stream_set_flowing(self, false);
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
        js_stream_coalesce_readable_buffer_for_encoding(self, next_encoding);
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
    // construct → _construct
    Item construct_opt = js_property_get(opts, make_string_item("construct"));
    if (get_type_id(construct_opt) == LMD_TYPE_FUNC)
        js_property_set(obj, make_string_item("_construct"), construct_opt);
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
        js_stream_set_readable_side_enabled(obj, it2b(readable));
    Item writable = js_property_get(opts, make_string_item("writable"));
    if (get_type_id(writable) == LMD_TYPE_BOOL)
        js_stream_set_writable_side_enabled(obj, it2b(writable));
    Item capture_rejections = js_property_get(opts, make_string_item("captureRejections"));
    if (get_type_id(capture_rejections) == LMD_TYPE_BOOL)
        js_property_set(obj, key_capture_rejections, capture_rejections);
    Item auto_destroy = js_property_get(opts, make_string_item("autoDestroy"));
    if (get_type_id(auto_destroy) == LMD_TYPE_BOOL)
        js_property_set(obj, key_auto_destroy, auto_destroy);
    Item allow_half_open = js_property_get(opts, make_string_item("allowHalfOpen"));
    if (is_duplex_like && get_type_id(allow_half_open) == LMD_TYPE_BOOL)
        js_property_set(obj, make_string_item("allowHalfOpen"), allow_half_open);
    Item signal = js_property_get(opts, make_string_item("signal"));
    TypeId signal_type = get_type_id(signal);
    if (signal.item != 0 && signal_type != LMD_TYPE_UNDEFINED &&
        signal_type != LMD_TYPE_NULL) {
        if (!js_stream_is_abort_signal(signal)) {
            js_throw_invalid_arg_type("options.signal", "AbortSignal", signal);
            return false;
        }
        js_stream_attach_abort_signal(signal, obj);
    }
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
static Item js_stream_inst_listeners(Item event_item) {
    return js_stream_listeners(js_get_this(), event_item);
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
static Item js_readable_inst_unpipe(Item dest) {
    js_readable_remove_pipe(js_get_this(), dest, true);
    return js_get_this();
}
static Item js_stream_base_constructor(void) {
    return make_js_undefined();
}
static Item js_stream_inst_destroy(Item err, Item callback) {
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_property_set(js_get_this(), make_string_item("__destroy_callback__"), callback);
    }
    return js_stream_destroy(js_get_this(), err);
}
static Item js_stream_inst_undestroy(void) {
    ensure_keys();
    Item self = js_get_this();

    js_property_set(self, key_destroyed, js_bool_item(false));
    js_property_set(self, make_string_item("destroyed"), js_bool_item(false));
    js_property_set(self, key_destroy_pending, js_bool_item(false));
    js_property_set(self, key_close_emitted, js_bool_item(false));
    js_property_set(self, key_closed, js_bool_item(false));
    js_property_set(self, make_string_item("errored"), ItemNull);
    js_property_set(self, make_string_item("__error__"), make_js_undefined());

    Item readable_state = js_property_get(self, key_readable_state);
    if (get_type_id(readable_state) == LMD_TYPE_MAP) {
        js_stream_set_readable_open(self, js_stream_readable_side_enabled(self));
        js_property_set(self, key_ended, js_bool_item(false));
        js_property_set(self, key_end_pending, js_bool_item(false));
        js_property_set(self, key_end_emitted, js_bool_item(false));
        js_property_set(self, key_reading, js_bool_item(false));
        js_create_data_property(self, make_string_item("readableEnded"), js_bool_item(false));
        js_property_set(self, make_string_item("readableAborted"), js_bool_item(false));
        js_state_set_bool(readable_state, "ended", false);
        js_state_set_bool(readable_state, "endEmitted", false);
        js_state_set_bool(readable_state, "errorEmitted", false);
        js_state_set_bool(readable_state, "reading", false);
        js_state_set_item(readable_state, "errored", ItemNull);
    }

    Item writable_state = js_property_get(self, key_writable_state);
    if (get_type_id(writable_state) == LMD_TYPE_MAP) {
        js_stream_set_writable_open(self, js_stream_writable_side_enabled(self));
        js_property_set(self, key_finished, js_bool_item(false));
        js_property_set(self, key_finish_emitted, js_bool_item(false));
        js_create_data_property(self, make_string_item("writableEnded"), js_bool_item(false));
        js_create_data_property(self, make_string_item("writableFinished"), js_bool_item(false));
        js_property_set(self, make_string_item("writableAborted"), js_bool_item(false));
        js_state_set_bool(writable_state, "ending", false);
        js_state_set_bool(writable_state, "ended", false);
        js_state_set_bool(writable_state, "finished", false);
        js_state_set_bool(writable_state, "errorEmitted", false);
        js_state_set_item(writable_state, "errored", ItemNull);
    }

    return make_js_undefined();
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
static Item js_readable_inst_toArray(Item options) {
    return js_readable_toArray(js_get_this(), options);
}
static Item js_readable_inst_map(Item fn, Item options) {
    return js_readable_map(js_get_this(), fn, options);
}
static Item js_readable_inst_filter(Item fn, Item options) {
    return js_readable_filter(js_get_this(), fn, options);
}
static Item js_readable_inst_forEach(Item fn, Item options) {
    return js_readable_forEach(js_get_this(), fn, options);
}
static Item js_readable_inst_reduce(Item fn, Item initial, Item options) {
    return js_readable_reduce(js_get_this(), fn, initial, options);
}
static Item js_readable_inst_compose(Item stream, Item options) {
    return js_readable_compose(js_get_this(), stream, options);
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

static void js_stream_install_readable_helpers(Item obj) {
    js_property_set(obj, make_string_item("toArray"), js_new_function((void*)js_readable_inst_toArray, 1));
    js_property_set(obj, make_string_item("map"), js_new_function((void*)js_readable_inst_map, 2));
    js_property_set(obj, make_string_item("filter"), js_new_function((void*)js_readable_inst_filter, 2));
    js_property_set(obj, make_string_item("forEach"), js_new_function((void*)js_readable_inst_forEach, 2));
    js_property_set(obj, make_string_item("reduce"), js_new_function((void*)js_readable_inst_reduce, 3));
    js_property_set(obj, make_string_item("compose"), js_new_function((void*)js_readable_inst_compose, 2));
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
    js_property_set(obj, key_readable_side_enabled, js_bool_item(true));
    js_stream_set_flowing(obj, false);
    js_property_set(obj, key_ended, js_bool_item(false));
    js_property_set(obj, key_destroyed, js_bool_item(false));
    js_property_set(obj, make_string_item("destroyed"), js_bool_item(false));
    js_property_set(obj, make_string_item("errored"), ItemNull);
    js_property_set(obj, make_string_item("readableAborted"), js_bool_item(false));
    js_property_set(obj, key_end_pending, js_bool_item(false));
    js_property_set(obj, key_end_emitted, js_bool_item(false));
    js_property_set(obj, key_reading, js_bool_item(false));
    js_property_set(obj, key_reading_sync, js_bool_item(false));
    js_property_set(obj, key_paused, js_bool_item(false));
    js_property_set(obj, key_close_emitted, js_bool_item(false));
    js_property_set(obj, key_closed, js_bool_item(false));
    js_property_set(obj, key_auto_destroy, js_bool_item(true));
    js_property_set(obj, key_readable_state, js_create_readable_state());
    js_stream_define_bool(obj, "readableEnded", false);
    js_stream_set_readable_buffer(obj, js_array_new(0));
    Item listeners = js_new_object();
    js_property_set(obj, key_listeners, listeners);
    js_property_set(obj, make_string_item("_events"), js_new_object());
    js_stream_init_readable_options(obj);

    js_property_set(obj, key_on, js_new_function((void*)js_stream_inst_on, 2));
    js_property_set(obj, make_string_item("once"), js_new_function((void*)js_stream_inst_once, 2));
    Item off_fn = js_new_function((void*)js_stream_inst_off, 2);
    js_property_set(obj, make_string_item("off"), off_fn);
    js_property_set(obj, make_string_item("removeListener"), off_fn);
    js_property_set(obj, make_string_item("removeAllListeners"), js_new_function((void*)js_stream_inst_removeAllListeners, 1));
    js_property_set(obj, key_emit, js_new_function((void*)js_stream_inst_emit, 2));
    js_property_set(obj, make_string_item("eventNames"), js_new_function((void*)js_stream_inst_eventNames, 0));
    js_property_set(obj, make_string_item("listeners"), js_new_function((void*)js_stream_inst_listeners, 1));
    js_property_set(obj, make_string_item("listenerCount"), js_new_function((void*)js_stream_inst_listenerCount, 2));
    js_property_set(obj, key_push, js_new_function((void*)js_readable_inst_push, 2));
    js_property_set(obj, make_string_item("unshift"), js_new_function((void*)js_readable_inst_unshift, 2));
    js_property_set(obj, key_read, js_new_function((void*)js_readable_inst_read, 1));
    js_property_set(obj, key_pipe, js_new_function((void*)js_readable_inst_pipe, 1));
    js_property_set(obj, make_string_item("unpipe"), js_new_function((void*)js_readable_inst_unpipe, 1));
    js_property_set(obj, key_destroy, js_new_function((void*)js_stream_inst_destroy, 2));
    js_property_set(obj, make_string_item("_undestroy"), js_new_function((void*)js_stream_inst_undestroy, 0));
    js_property_set(obj, make_string_item("resume"), js_new_function((void*)js_readable_inst_resume, 0));
    js_property_set(obj, make_string_item("pause"), js_new_function((void*)js_readable_inst_pause, 0));
    js_property_set(obj, make_string_item("isPaused"), js_new_function((void*)js_readable_inst_isPaused, 0));
    js_property_set(obj, make_string_item("setEncoding"), js_new_function((void*)js_stream_inst_setEncoding, 1));
    js_property_set(obj, make_string_item("iterator"), js_new_function((void*)js_readable_inst_iterator, 1));
    js_stream_install_async_iterator(obj);
    js_stream_install_readable_helpers(obj);

    if (!propagate_stream_options(obj, opts)) return ItemNull;
    js_stream_call_construct(obj);
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
        js_stream_schedule_error_once(self, err);
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
        js_item_is_true(js_property_get(self, key_finish_emitted)) ||
        js_stream_has_stored_error(self)) {
        return make_js_undefined();
    }
    js_property_set(self, key_finish_emitted, js_bool_item(true));
    stream_emit(self, "finish", NULL, 0);
    if (js_stream_can_auto_destroy_after_writable_finish(self)) {
        js_stream_auto_destroy_after_terminal(self);
    }
    return make_js_undefined();
}

static Item js_stream_emit_finish_tick_closure(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    return js_stream_emit_finish_tick(env[0]);
}

static void js_stream_schedule_finish(Item self) {
    Item* env = js_alloc_env(1);
    env[0] = self;
    Item tick = js_new_closure((void*)js_stream_emit_finish_tick_closure, 0, env, 1);
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

static Item js_stream_call_callback_error_tick(Item callback, Item err) {
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, ItemNull, &err, 1);
    }
    return make_js_undefined();
}

static Item js_stream_call_callback_error_tick_closure(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    return js_stream_call_callback_error_tick(env[0], env[1]);
}

static void js_stream_schedule_callback_error(Item callback, Item err) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) return;
    Item* env = js_alloc_env(2);
    env[0] = callback;
    env[1] = err;
    Item tick = js_new_closure((void*)js_stream_call_callback_error_tick_closure, 0, env, 2);
    js_next_tick_enqueue(tick);
}

static void js_stream_add_writable_end_callback(Item self, Item callback) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) return;
    Item callbacks = js_property_get(self, make_string_item("__writable_end_callbacks__"));
    if (get_type_id(callbacks) != LMD_TYPE_ARRAY) {
        callbacks = js_array_new(0);
        js_property_set(self, make_string_item("__writable_end_callbacks__"), callbacks);
    }
    js_array_push(callbacks, callback);
}

static void js_stream_call_writable_end_callbacks(Item self, Item err) {
    Item callbacks = js_property_get(self, make_string_item("__writable_end_callbacks__"));
    if (get_type_id(callbacks) != LMD_TYPE_ARRAY) return;
    js_property_set(self, make_string_item("__writable_end_callbacks__"), js_array_new(0));
    bool has_error = js_stream_has_error(err);
    Item null_arg = ItemNull;
    int64_t len = js_array_length(callbacks);
    for (int64_t i = 0; i < len; i++) {
        Item callback = js_array_get_int(callbacks, i);
        if (get_type_id(callback) != LMD_TYPE_FUNC) continue;
        if (has_error) {
            js_call_function(callback, self, &err, 1);
        } else {
            js_call_function(callback, self, &null_arg, 1);
        }
    }
}

static Item js_stream_finish_after_final(Item self, Item callback, Item err) {
    ensure_keys();
    if (js_stream_has_error(err)) {
        js_property_set(self, make_string_item("__writable_end_pending__"), js_bool_item(false));
        js_stream_call_writable_end_callbacks(self, err);
        js_stream_schedule_error(self, err);
        return make_js_undefined();
    }

    if (js_item_is_true(js_property_get(self, key_destroyed))) {
        return make_js_undefined();
    }

    stream_emit(self, "prefinish", NULL, 0);
    Item stored_error = js_stream_get_stored_error(self);
    if (js_stream_error_value_present(stored_error)) {
        js_stream_call_writable_end_callbacks(self, stored_error);
        return make_js_undefined();
    }
    js_stream_mark_writable_finished(self);
    js_stream_call_writable_end_callbacks(self, make_js_undefined());
    js_stream_emit_finish_tick(self);
    return make_js_undefined();
}

static Item js_stream_final_callback_once(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item self = env[0];
    if (js_item_is_true(env[2])) {
        Item multi = js_stream_make_error_with_code("ERR_MULTIPLE_CALLBACK",
            "Callback called multiple times");
        js_stream_schedule_error(self, multi);
        return make_js_undefined();
    }
    env[2] = js_bool_item(true);
    return js_stream_finish_after_final(self, env[1], err);
}

static Item js_stream_make_final_callback(Item self, Item callback) {
    Item* env = js_alloc_env(3);
    env[0] = self;
    env[1] = callback;
    env[2] = js_bool_item(false);
    return js_new_closure((void*)js_stream_final_callback_once, 1, env, 3);
}

static Item js_stream_complete_finish_tick(Item self) {
    ensure_keys();
    if (js_item_is_true(js_property_get(self, key_destroyed)) ||
        js_stream_has_stored_error(self)) {
        return make_js_undefined();
    }
    js_stream_mark_writable_finished(self);
    js_stream_call_writable_end_callbacks(self, make_js_undefined());
    js_stream_schedule_finish(self);
    return make_js_undefined();
}

static Item js_stream_complete_finish_tick_closure(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    return js_stream_complete_finish_tick(env[0]);
}

static void js_stream_schedule_finish_ready(Item self) {
    Item* env = js_alloc_env(1);
    env[0] = self;
    Item tick = js_new_closure((void*)js_stream_complete_finish_tick_closure, 0, env, 1);
    js_next_tick_enqueue(tick);
}

static void js_writable_finish_now(Item self, Item callback) {
    ensure_keys();
    if (js_item_is_true(js_property_get(self, key_destroyed)) ||
        js_item_is_true(js_property_get(self, key_finish_emitted)) ||
        js_stream_has_stored_error(self) ||
        js_state_get_bool(js_property_get(self, key_writable_state), "finished")) {
        return;
    }

    js_property_set(self, make_string_item("__writable_end_pending__"), js_bool_item(false));

    Item final_fn = js_property_get(self, make_string_item("_final"));
    if (get_type_id(final_fn) == LMD_TYPE_FUNC) {
        Item final_cb = js_stream_make_final_callback(self, callback);
        js_call_function(final_fn, self, &final_cb, 1);
        if (js_check_exception()) {
            Item err = js_clear_exception();
            js_stream_finish_after_final(self, callback, err);
        }
        return;
    }

    stream_emit(self, "prefinish", NULL, 0);
    Item stored_error = js_stream_get_stored_error(self);
    if (js_stream_error_value_present(stored_error)) {
        js_stream_call_writable_end_callbacks(self, stored_error);
        return;
    }
    js_stream_schedule_finish_ready(self);
}

static void js_writable_maybe_finish_deferred(Item self) {
    ensure_keys();
    if (!js_item_is_true(js_property_get(self, make_string_item("__writable_end_pending__")))) return;
    if (js_item_is_true(js_property_get(self, make_string_item("_writing")))) return;
    if (js_stream_pending_writes_count(self) > 0) {
        js_stream_flush_pending_writes(self);
        if (js_item_is_true(js_property_get(self, make_string_item("_writing"))) ||
            js_stream_pending_writes_count(self) > 0) {
            return;
        }
    }
    js_writable_finish_now(self, make_js_undefined());
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
        js_stream_add_writable_end_callback(self, callback);
        if (chunk.item != 0 && get_type_id(chunk) != LMD_TYPE_UNDEFINED &&
            get_type_id(chunk) != LMD_TYPE_NULL) {
            Item err = js_stream_make_error_with_code("ERR_STREAM_WRITE_AFTER_END",
                "write after end");
            js_property_set(self, make_string_item("__writable_end_pending__"), js_bool_item(false));
            js_stream_mark_destroyed(self);
            js_stream_call_writable_end_callbacks(self, err);
            js_stream_schedule_error(self, err);
        }
        return self;
    }

    // write final chunk if provided
    if (chunk.item != 0 && get_type_id(chunk) != LMD_TYPE_UNDEFINED &&
        get_type_id(chunk) != LMD_TYPE_NULL) {
        js_writable_write(self, chunk, make_js_undefined(), make_js_undefined());
        // if write threw (e.g. ERR_METHOD_NOT_IMPLEMENTED), propagate the exception
        if (js_check_exception()) return ItemNull;
        if (js_stream_has_stored_error(self)) return self;
    }

    // uncork all if corked
    Item corked = js_property_get(self, make_string_item("_corked"));
    if (get_type_id(corked) == LMD_TYPE_INT && it2i(corked) > 0) {
        js_stream_set_writable_corked(self, 0);
        Item pending = js_property_get(self, make_string_item("_pendingWrites"));
        if (get_type_id(pending) == LMD_TYPE_ARRAY && js_array_length(pending) > 0) {
            js_stream_flush_pending_writes(self);
            if (js_stream_has_stored_error(self)) return self;
        }
    }

    js_stream_mark_writable_ended(self);

    js_stream_add_writable_end_callback(self, callback);
    js_property_set(self, make_string_item("__writable_end_pending__"), js_bool_item(true));
    js_writable_maybe_finish_deferred(self);
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
    js_property_set(obj, key_writable_side_enabled, js_bool_item(true));
    js_property_set(obj, key_finished, js_bool_item(false));
    js_property_set(obj, key_destroyed, js_bool_item(false));
    js_property_set(obj, make_string_item("destroyed"), js_bool_item(false));
    js_property_set(obj, make_string_item("errored"), ItemNull);
    js_property_set(obj, make_string_item("writableAborted"), js_bool_item(false));
    js_property_set(obj, key_finish_emitted, js_bool_item(false));
    js_property_set(obj, key_close_emitted, js_bool_item(false));
    js_property_set(obj, key_closed, js_bool_item(false));
    js_property_set(obj, key_auto_destroy, js_bool_item(true));
    js_property_set(obj, key_writable_state, js_create_writable_state(obj));
    js_stream_set_writable_corked(obj, 0);
    js_stream_set_buffered_request_count(obj, 0);
    js_stream_define_bool(obj, "writableEnded", false);
    js_stream_define_bool(obj, "writableFinished", false);
    Item listeners = js_new_object();
    js_property_set(obj, key_listeners, listeners);
    js_property_set(obj, make_string_item("_events"), js_new_object());
    js_stream_init_writable_options(obj);

    js_property_set(obj, key_on, js_new_function((void*)js_stream_inst_on, 2));
    js_property_set(obj, make_string_item("once"), js_new_function((void*)js_stream_inst_once, 2));
    Item off_fn = js_new_function((void*)js_stream_inst_off, 2);
    js_property_set(obj, make_string_item("off"), off_fn);
    js_property_set(obj, make_string_item("removeListener"), off_fn);
    js_property_set(obj, make_string_item("removeAllListeners"), js_new_function((void*)js_stream_inst_removeAllListeners, 1));
    js_property_set(obj, key_emit, js_new_function((void*)js_stream_inst_emit, 2));
    js_property_set(obj, make_string_item("eventNames"), js_new_function((void*)js_stream_inst_eventNames, 0));
    js_property_set(obj, make_string_item("listeners"), js_new_function((void*)js_stream_inst_listeners, 1));
    js_property_set(obj, make_string_item("listenerCount"), js_new_function((void*)js_stream_inst_listenerCount, 2));
    js_property_set(obj, key_write, js_new_function((void*)js_writable_inst_write, 3));
    js_property_set(obj, key_end, js_new_function((void*)js_writable_inst_end, 2));
    js_property_set(obj, key_destroy, js_new_function((void*)js_stream_inst_destroy, 2));
    js_property_set(obj, make_string_item("_undestroy"), js_new_function((void*)js_stream_inst_undestroy, 0));
    js_property_set(obj, make_string_item("cork"), js_new_function((void*)js_writable_inst_cork, 0));
    js_property_set(obj, make_string_item("uncork"), js_new_function((void*)js_writable_inst_uncork, 0));
    js_property_set(obj, make_string_item("setEncoding"), js_new_function((void*)js_stream_inst_setEncoding, 1));
    js_property_set(obj, make_string_item("setDefaultEncoding"), js_new_function((void*)js_stream_inst_setDefaultEncoding, 1));

    if (!propagate_stream_options(obj, opts)) return ItemNull;
    js_stream_call_construct(obj);
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
    js_property_set(obj, key_readable_side_enabled, js_bool_item(true));
    js_property_set(obj, key_writable_side_enabled, js_bool_item(true));
    js_stream_set_flowing(obj, false);
    js_property_set(obj, key_ended, js_bool_item(false));
    js_property_set(obj, key_finished, js_bool_item(false));
    js_property_set(obj, key_destroyed, js_bool_item(false));
    js_property_set(obj, make_string_item("destroyed"), js_bool_item(false));
    js_property_set(obj, make_string_item("errored"), ItemNull);
    js_property_set(obj, make_string_item("readableAborted"), js_bool_item(false));
    js_property_set(obj, make_string_item("writableAborted"), js_bool_item(false));
    js_property_set(obj, key_finish_emitted, js_bool_item(false));
    js_property_set(obj, key_end_pending, js_bool_item(false));
    js_property_set(obj, key_end_emitted, js_bool_item(false));
    js_property_set(obj, key_reading, js_bool_item(false));
    js_property_set(obj, key_paused, js_bool_item(false));
    js_property_set(obj, key_close_emitted, js_bool_item(false));
    js_property_set(obj, key_closed, js_bool_item(false));
    js_property_set(obj, key_auto_destroy, js_bool_item(true));
    js_property_set(obj, make_string_item("allowHalfOpen"), js_bool_item(true));
    js_property_set(obj, key_readable_state, js_create_readable_state());
    js_property_set(obj, key_writable_state, js_create_writable_state(obj));
    js_stream_set_writable_corked(obj, 0);
    js_stream_set_buffered_request_count(obj, 0);
    js_stream_define_bool(obj, "readableEnded", false);
    js_stream_define_bool(obj, "writableEnded", false);
    js_stream_define_bool(obj, "writableFinished", false);
    js_stream_set_readable_buffer(obj, js_array_new(0));
    Item listeners = js_new_object();
    js_property_set(obj, key_listeners, listeners);
    js_property_set(obj, make_string_item("_events"), js_new_object());
    js_stream_init_readable_options(obj);
    js_stream_init_writable_options(obj);

    // Readable methods
    js_property_set(obj, key_on, js_new_function((void*)js_stream_inst_on, 2));
    js_property_set(obj, make_string_item("once"), js_new_function((void*)js_stream_inst_once, 2));
    Item off_fn = js_new_function((void*)js_stream_inst_off, 2);
    js_property_set(obj, make_string_item("off"), off_fn);
    js_property_set(obj, make_string_item("removeListener"), off_fn);
    js_property_set(obj, make_string_item("removeAllListeners"), js_new_function((void*)js_stream_inst_removeAllListeners, 1));
    js_property_set(obj, key_emit, js_new_function((void*)js_stream_inst_emit, 2));
    js_property_set(obj, make_string_item("eventNames"), js_new_function((void*)js_stream_inst_eventNames, 0));
    js_property_set(obj, make_string_item("listeners"), js_new_function((void*)js_stream_inst_listeners, 1));
    js_property_set(obj, make_string_item("listenerCount"), js_new_function((void*)js_stream_inst_listenerCount, 2));
    js_property_set(obj, key_push, js_new_function((void*)js_readable_inst_push, 2));
    js_property_set(obj, make_string_item("unshift"), js_new_function((void*)js_readable_inst_unshift, 2));
    js_property_set(obj, key_read, js_new_function((void*)js_readable_inst_read, 1));
    js_property_set(obj, key_pipe, js_new_function((void*)js_readable_inst_pipe, 1));
    js_property_set(obj, make_string_item("unpipe"), js_new_function((void*)js_readable_inst_unpipe, 1));
    js_property_set(obj, make_string_item("resume"), js_new_function((void*)js_readable_inst_resume, 0));
    js_property_set(obj, make_string_item("pause"), js_new_function((void*)js_readable_inst_pause, 0));
    js_property_set(obj, make_string_item("isPaused"), js_new_function((void*)js_readable_inst_isPaused, 0));

    // Writable methods
    js_property_set(obj, key_write, js_new_function((void*)js_writable_inst_write, 3));
    js_property_set(obj, key_end, js_new_function((void*)js_writable_inst_end, 2));
    js_property_set(obj, key_destroy, js_new_function((void*)js_stream_inst_destroy, 2));
    js_property_set(obj, make_string_item("_undestroy"), js_new_function((void*)js_stream_inst_undestroy, 0));
    js_property_set(obj, make_string_item("cork"), js_new_function((void*)js_writable_inst_cork, 0));
    js_property_set(obj, make_string_item("uncork"), js_new_function((void*)js_writable_inst_uncork, 0));
    js_property_set(obj, make_string_item("setEncoding"), js_new_function((void*)js_stream_inst_setEncoding, 1));
    js_property_set(obj, make_string_item("setDefaultEncoding"), js_new_function((void*)js_stream_inst_setDefaultEncoding, 1));
    js_stream_install_async_iterator(obj);
    js_stream_install_readable_helpers(obj);

    if (!propagate_stream_options(obj, opts)) return ItemNull;
    js_stream_call_construct(obj);
    return obj;
}

static Item js_stream_duplex_pair_write(Item env_item, Item chunk, Item encoding, Item callback) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item peer = env[0];
    js_readable_push_encoded(peer, chunk, encoding);
    if (js_check_exception()) return ItemNull;
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, make_js_undefined(), NULL, 0);
    }
    return make_js_undefined();
}

static Item js_stream_duplex_pair_final(Item env_item, Item callback) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item peer = env[0];
    js_readable_push(peer, ItemNull);
    if (js_check_exception()) return ItemNull;
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, make_js_undefined(), NULL, 0);
    }
    return make_js_undefined();
}

static Item js_stream_duplex_pair_read(void) {
    return make_js_undefined();
}

static void js_stream_duplex_pair_attach(Item endpoint, Item peer) {
    Item* env = js_alloc_env(1);
    env[0] = peer;
    js_property_set(endpoint, make_string_item("_write"),
                    js_new_closure((void*)js_stream_duplex_pair_write, 3, env, 1));
    js_property_set(endpoint, make_string_item("_final"),
                    js_new_closure((void*)js_stream_duplex_pair_final, 1, env, 1));
    js_property_set(endpoint, make_string_item("_read"),
                    js_new_function((void*)js_stream_duplex_pair_read, 0));
}

static Item js_stream_duplex_pair(void) {
    ensure_keys();
    Item opts = js_new_object();
    Item first = js_duplex_new(opts);
    if (js_check_exception()) return ItemNull;
    Item second = js_duplex_new(opts);
    if (js_check_exception()) return ItemNull;

    js_stream_duplex_pair_attach(first, second);
    js_stream_duplex_pair_attach(second, first);

    Item pair = js_array_new(0);
    js_array_push(pair, first);
    js_array_push(pair, second);
    return pair;
}

static Item js_duplex_from_readable_data(Item env_item, Item chunk) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    js_readable_push(env[0], chunk);
    return make_js_undefined();
}

static Item js_duplex_from_readable_end(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    js_readable_push(env[0], ItemNull);
    return make_js_undefined();
}

static Item js_duplex_from_forward_error(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    if (js_stream_has_stored_error(env[0])) return make_js_undefined();
    if (js_stream_is_stream_like(env[1]) && env[1].item != env[0].item) {
        js_stream_destroy(env[1], err);
    }
    js_stream_destroy(env[0], err);
    return make_js_undefined();
}

static Item js_duplex_from_forward_callback_once(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[1])) return make_js_undefined();
    env[1] = js_bool_item(true);
    Item callback = env[0];
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        if (js_stream_has_error(err)) {
            js_call_function(callback, make_js_undefined(), &err, 1);
        } else {
            js_call_function(callback, make_js_undefined(), NULL, 0);
        }
    }
    return make_js_undefined();
}

static Item js_duplex_from_make_forward_callback(Item callback) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) return callback;
    Item* env = js_alloc_env(2);
    env[0] = callback;
    env[1] = js_bool_item(false);
    return js_new_closure((void*)js_duplex_from_forward_callback_once, 1, env, 2);
}

static void js_duplex_from_attach_readable(Item duplex, Item readable, Item writable) {
    Item* env = js_alloc_env(2);
    env[0] = duplex;
    env[1] = writable;
    js_stream_on(readable, make_string_item("data"),
                 js_new_closure((void*)js_duplex_from_readable_data, 1, env, 2));
    js_stream_on(readable, make_string_item("end"),
                 js_new_closure((void*)js_duplex_from_readable_end, 0, env, 2));
    js_stream_on(readable, make_string_item("error"),
                 js_new_closure((void*)js_duplex_from_forward_error, 1, env, 2));
}

static Item js_duplex_from_writable_write(Item env_item, Item chunk, Item encoding, Item callback) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item write_fn = js_property_get(env[0], key_write);
    if (get_type_id(write_fn) != LMD_TYPE_FUNC) {
        if (get_type_id(callback) == LMD_TYPE_FUNC)
            js_call_function(callback, make_js_undefined(), NULL, 0);
        return make_js_undefined();
    }
    Item args[3] = { chunk, encoding, js_duplex_from_make_forward_callback(callback) };
    js_call_function(write_fn, env[0], args, 3);
    return make_js_undefined();
}

static Item js_duplex_from_writable_final(Item env_item, Item callback) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item end_fn = js_property_get(env[0], key_end);
    if (get_type_id(end_fn) == LMD_TYPE_FUNC) {
        Item args[1] = { js_duplex_from_make_forward_callback(callback) };
        js_call_function(end_fn, env[0], args, 1);
    } else if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, make_js_undefined(), NULL, 0);
    }
    return make_js_undefined();
}

static void js_duplex_from_attach_writable(Item duplex, Item writable) {
    Item* env = js_alloc_env(1);
    env[0] = writable;
    js_property_set(duplex, make_string_item("_write"),
                    js_new_closure((void*)js_duplex_from_writable_write, 3, env, 1));
    js_property_set(duplex, make_string_item("_final"),
                    js_new_closure((void*)js_duplex_from_writable_final, 1, env, 1));

    Item* err_env = js_alloc_env(2);
    err_env[0] = duplex;
    err_env[1] = ItemNull;
    js_stream_on(writable, make_string_item("error"),
                 js_new_closure((void*)js_duplex_from_forward_error, 1, err_env, 2));
}

static Item js_duplex_from_destroy(Item env_item, Item err, Item callback) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item readable = env[0];
    Item writable = env[1];
    Item duplex = env[2];
    if (js_stream_is_stream_like(readable) &&
        !js_item_is_true(js_property_get(readable, key_destroyed))) {
        js_stream_destroy(readable, make_js_undefined());
    }
    if (writable.item != readable.item &&
        js_stream_is_stream_like(writable) &&
        !js_item_is_true(js_property_get(writable, key_destroyed))) {
        js_stream_destroy(writable, make_js_undefined());
    }
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        Item readable_state = js_property_get(duplex, key_readable_state);
        Item writable_state = js_property_get(duplex, key_writable_state);
        bool error_emitted = js_state_get_bool(readable_state, "errorEmitted") ||
                             js_state_get_bool(writable_state, "errorEmitted");
        if (js_stream_has_callback_error(err) && !error_emitted) {
            js_call_function(callback, make_js_undefined(), &err, 1);
        } else {
            js_call_function(callback, make_js_undefined(), NULL, 0);
        }
    }
    return make_js_undefined();
}

static Item js_duplex_from_promise_fulfilled(Item env_item, Item value) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    if (get_type_id(value) != LMD_TYPE_UNDEFINED && get_type_id(value) != LMD_TYPE_NULL) {
        js_readable_push(env[0], value);
    }
    js_readable_push(env[0], ItemNull);
    return make_js_undefined();
}

static Item js_duplex_from_promise_rejected(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    js_stream_destroy(env[0], err);
    return make_js_undefined();
}

static Item js_duplex_from_promise(Item promise) {
    Item opts = js_new_object();
    js_property_set(opts, make_string_item("readable"), js_bool_item(true));
    js_property_set(opts, make_string_item("writable"), js_bool_item(false));
    Item duplex = js_duplex_new(opts);
    if (js_check_exception()) return ItemNull;
    js_property_set(duplex, make_string_item("_read"),
                    js_new_function((void*)js_stream_duplex_pair_read, 0));

    Item* env = js_alloc_env(1);
    env[0] = duplex;
    Item on_fulfilled = js_new_closure((void*)js_duplex_from_promise_fulfilled, 1, env, 1);
    Item on_rejected = js_new_closure((void*)js_duplex_from_promise_rejected, 1, env, 1);
    js_promise_then(promise, on_fulfilled, on_rejected);
    return duplex;
}

static Item js_duplex_from_readable_value(Item value) {
    Item opts = js_new_object();
    js_property_set(opts, make_string_item("readable"), js_bool_item(true));
    js_property_set(opts, make_string_item("writable"), js_bool_item(false));
    Item duplex = js_duplex_new(opts);
    if (js_check_exception()) return ItemNull;
    js_property_set(duplex, make_string_item("_read"),
                    js_new_function((void*)js_stream_duplex_pair_read, 0));
    if (get_type_id(value) != LMD_TYPE_UNDEFINED && get_type_id(value) != LMD_TYPE_NULL) {
        js_readable_push(duplex, value);
    }
    js_readable_push(duplex, ItemNull);
    return duplex;
}

static Item js_duplex_from_blob(Item blob) {
    Item text = js_property_get(blob, make_string_item("_text"));
    if (get_type_id(text) != LMD_TYPE_STRING) {
        return js_duplex_from_readable_value(make_js_undefined());
    }
    String* str = it2s(text);
    int len = str ? (int)str->len : 0;
    Item array_buffer = js_arraybuffer_new(len);
    if (len > 0 && get_type_id(array_buffer) == LMD_TYPE_MAP) {
        JsArrayBuffer* ab = js_get_arraybuffer_ptr_item(array_buffer);
        if (ab && ab->data) memcpy(ab->data, str->chars, (size_t)len);
    }
    return js_duplex_from_readable_value(array_buffer);
}

static Item js_duplex_from_web_readable(Item readable_stream) {
    Item node_readable = js_property_get(readable_stream, make_string_item("__node_readable__"));
    if (js_stream_is_stream_like(node_readable)) return js_duplex_from(node_readable);

    Item opts = js_new_object();
    js_property_set(opts, make_string_item("readable"), js_bool_item(true));
    js_property_set(opts, make_string_item("writable"), js_bool_item(false));
    Item duplex = js_duplex_new(opts);
    if (js_check_exception()) return ItemNull;
    js_property_set(duplex, make_string_item("_read"),
                    js_new_function((void*)js_stream_duplex_pair_read, 0));

    Item chunks = js_property_get(readable_stream, make_string_item("__chunks__"));
    if (get_type_id(chunks) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(chunks);
        for (int64_t i = 0; i < len; i++) {
            js_readable_push(duplex, js_array_get_int(chunks, i));
        }
    }
    js_readable_push(duplex, ItemNull);
    return duplex;
}

static Item js_duplex_from_web_writable_write(Item env_item, Item chunk, Item encoding, Item callback) {
    (void)encoding;
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item sink = js_property_get(env[0], make_string_item("__sink__"));
    if (get_type_id(sink) != LMD_TYPE_MAP && get_type_id(sink) != LMD_TYPE_ELEMENT) {
        sink = js_property_get(env[0], make_string_item("__writer__"));
    }
    Item write_fn = js_property_get(sink, key_write);
    if (get_type_id(write_fn) != LMD_TYPE_FUNC) {
        write_fn = js_property_get(sink, make_string_item("write"));
    }
    if (get_type_id(write_fn) == LMD_TYPE_FUNC) {
        js_call_function(write_fn, sink, &chunk, 1);
        if (js_check_exception()) return ItemNull;
    }
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, make_js_undefined(), NULL, 0);
    }
    return make_js_undefined();
}

static Item js_duplex_from_web_writable_final(Item env_item, Item callback) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item sink = js_property_get(env[0], make_string_item("__sink__"));
    if (get_type_id(sink) != LMD_TYPE_MAP && get_type_id(sink) != LMD_TYPE_ELEMENT) {
        sink = js_property_get(env[0], make_string_item("__writer__"));
    }
    Item close_fn = js_property_get(sink, make_string_item("close"));
    if (get_type_id(close_fn) == LMD_TYPE_FUNC) {
        js_call_function(close_fn, sink, NULL, 0);
        if (js_check_exception()) return ItemNull;
    }
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, make_js_undefined(), NULL, 0);
    }
    return make_js_undefined();
}

static Item js_duplex_from_web_writable(Item writable_stream) {
    Item node_writable = js_property_get(writable_stream, make_string_item("__node_stream__"));
    if (js_stream_is_stream_like(node_writable)) return js_duplex_from(node_writable);

    Item opts = js_new_object();
    js_property_set(opts, make_string_item("readable"), js_bool_item(false));
    js_property_set(opts, make_string_item("writable"), js_bool_item(true));
    Item duplex = js_duplex_new(opts);
    if (js_check_exception()) return ItemNull;
    Item* env = js_alloc_env(1);
    env[0] = writable_stream;
    js_property_set(duplex, make_string_item("_write"),
                    js_new_closure((void*)js_duplex_from_web_writable_write, 3, env, 1));
    js_property_set(duplex, make_string_item("_final"),
                    js_new_closure((void*)js_duplex_from_web_writable_final, 1, env, 1));
    return duplex;
}

static Item js_duplex_from_function(Item fn) {
    Item opts = js_new_object();
    js_property_set(opts, make_string_item("objectMode"), js_bool_item(true));
    Item input = js_passthrough_new(opts);
    if (js_check_exception()) return ItemNull;

    Item args[1] = { input };
    Item result = js_call_function(fn, make_js_undefined(), args, 1);
    if (js_check_exception()) return ItemNull;
    if (get_type_id(result) == LMD_TYPE_UNDEFINED) {
        return js_throw_type_error_code("ERR_INVALID_RETURN_VALUE",
            "Expected a stream, iterable, or promise to be returned from the function");
    }

    Item then_fn = js_property_get(result, make_string_item("then"));
    Item readable = get_type_id(then_fn) == LMD_TYPE_FUNC
        ? js_duplex_from_promise(js_promise_resolve(result))
        : js_readable_compose_from_result(input, result, make_js_undefined());
    if (js_check_exception()) return ItemNull;
    Item pair = js_new_object();
    js_property_set(pair, make_string_item("readable"), readable);
    js_property_set(pair, make_string_item("writable"), input);
    return js_duplex_from(pair);
}

static Item js_duplex_from(Item source) {
    ensure_keys();
    if (get_type_id(source) == LMD_TYPE_FUNC) {
        return js_duplex_from_function(source);
    }

    Item then_fn = js_property_get(source, make_string_item("then"));
    if (get_type_id(then_fn) == LMD_TYPE_FUNC) {
        return js_duplex_from_promise(source);
    }

    JsClass source_cls = js_class_id(source);
    if (source_cls == JS_CLASS_BLOB) return js_duplex_from_blob(source);
    if (source_cls == JS_CLASS_READABLE_STREAM) return js_duplex_from_web_readable(source);
    if (source_cls == JS_CLASS_WRITABLE_STREAM) return js_duplex_from_web_writable(source);

    if (js_readable_compose_is_duplex_like(source)) return source;

    Item readable = source;
    Item writable = source;
    TypeId source_type = get_type_id(source);
    if (source_type == LMD_TYPE_MAP || source_type == LMD_TYPE_ELEMENT) {
        Item candidate_readable = js_property_get(source, make_string_item("readable"));
        Item candidate_writable = js_property_get(source, make_string_item("writable"));
        if (js_stream_is_stream_like(candidate_readable) ||
            js_class_id(candidate_readable) == JS_CLASS_READABLE_STREAM) {
            readable = candidate_readable;
        }
        if (js_stream_is_stream_like(candidate_writable) ||
            js_class_id(candidate_writable) == JS_CLASS_WRITABLE_STREAM) {
            writable = candidate_writable;
        }
    }

    if (js_class_id(readable) == JS_CLASS_READABLE_STREAM && writable.item == source.item)
        return js_duplex_from_web_readable(readable);
    if (js_class_id(writable) == JS_CLASS_WRITABLE_STREAM && readable.item == source.item)
        return js_duplex_from_web_writable(writable);
    if (js_class_id(readable) == JS_CLASS_READABLE_STREAM ||
        js_class_id(writable) == JS_CLASS_WRITABLE_STREAM) {
        Item readable_duplex = js_class_id(readable) == JS_CLASS_READABLE_STREAM ?
            js_duplex_from_web_readable(readable) : readable;
        Item writable_duplex = js_class_id(writable) == JS_CLASS_WRITABLE_STREAM ?
            js_duplex_from_web_writable(writable) : writable;
        Item pair = js_new_object();
        js_property_set(pair, make_string_item("readable"), readable_duplex);
        js_property_set(pair, make_string_item("writable"), writable_duplex);
        return js_duplex_from(pair);
    }

    bool has_readable = js_stream_is_stream_like(readable) && js_stream_has_readable_side(readable);
    bool has_writable = js_stream_is_stream_like(writable) && js_stream_has_writable_side(writable);
    if (!has_readable && !has_writable) {
        return js_throw_invalid_arg_type("body", "Stream", source);
    }

    Item opts = js_new_object();
    js_property_set(opts, make_string_item("readable"), js_bool_item(has_readable));
    js_property_set(opts, make_string_item("writable"), js_bool_item(has_writable));
    if (has_readable) {
        Item readable_state = js_property_get(readable, key_readable_state);
        js_property_set(opts, make_string_item("readableObjectMode"),
                        js_bool_item(js_state_get_bool(readable_state, "objectMode")));
    }
    if (has_writable) {
        Item writable_state = js_property_get(writable, key_writable_state);
        js_property_set(opts, make_string_item("writableObjectMode"),
                        js_bool_item(js_state_get_bool(writable_state, "objectMode")));
    }
    Item duplex = js_duplex_new(opts);
    if (js_check_exception()) return ItemNull;
    js_property_set(duplex, make_string_item("_read"),
                    js_new_function((void*)js_stream_duplex_pair_read, 0));

    if (has_readable) js_duplex_from_attach_readable(duplex, readable, has_writable ? writable : ItemNull);
    if (has_writable) js_duplex_from_attach_writable(duplex, writable);
    Item* destroy_env = js_alloc_env(3);
    destroy_env[0] = readable;
    destroy_env[1] = writable;
    destroy_env[2] = duplex;
    js_property_set(duplex, make_string_item("_destroy"),
                    js_new_closure((void*)js_duplex_from_destroy, 2, destroy_env, 3));
    return duplex;
}

// =============================================================================
// Transform stream (Duplex with _transform)
// =============================================================================

static Item js_transform_finish_after_flush(Item self, Item callback, Item err) {
    ensure_keys();
    js_property_set(self, make_string_item("__transform_end_pending__"), js_bool_item(false));
    if (js_stream_has_error(err)) {
        if (get_type_id(callback) == LMD_TYPE_FUNC) {
            js_call_function(callback, self, &err, 1);
        }
        js_stream_schedule_error(self, err);
        return make_js_undefined();
    }
    js_stream_mark_writable_ended(self);
    stream_emit(self, "prefinish", NULL, 0);
    js_stream_mark_writable_finished(self);
    js_readable_push(self, ItemNull);
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, self, NULL, 0);
    }
    js_stream_schedule_finish(self);
    return make_js_undefined();
}

static Item js_transform_finish_after_final(Item self, Item callback, Item err) {
    ensure_keys();
    if (js_stream_has_error(err)) {
        return js_transform_finish_after_flush(self, callback, err);
    }
    Item flush_fn = js_property_get(self, make_string_item("_flush"));
    if (get_type_id(flush_fn) == LMD_TYPE_FUNC) {
        Item bound_args[2] = { self, callback };
        Item flush_cb = js_bind_function(js_new_function((void*)js_transform_finish_after_flush, 3),
                                         make_js_undefined(), bound_args, 2);
        js_call_function(flush_fn, self, &flush_cb, 1);
        return make_js_undefined();
    }
    return js_transform_finish_after_flush(self, callback, make_js_undefined());
}

static void js_transform_finish_now(Item self, Item callback) {
    ensure_keys();
    if (js_item_is_true(js_property_get(self, key_finish_emitted)) ||
        js_item_is_true(js_property_get(self, key_finished))) {
        return;
    }
    Item final_fn = js_property_get(self, make_string_item("_final"));
    if (get_type_id(final_fn) == LMD_TYPE_FUNC) {
        Item bound_args[2] = { self, callback };
        Item final_cb = js_bind_function(js_new_function((void*)js_transform_finish_after_final, 3),
                                         make_js_undefined(), bound_args, 2);
        js_call_function(final_fn, self, &final_cb, 1);
        return;
    }
    js_transform_finish_after_final(self, callback, make_js_undefined());
}

static void js_transform_maybe_finish_deferred(Item self) {
    ensure_keys();
    if (!js_item_is_true(js_property_get(self, make_string_item("__transform_end_pending__")))) return;
    if (js_item_is_true(js_property_get(self, make_string_item("_writing")))) return;
    if (js_stream_pending_writes_count(self) > 0) return;
    Item callback = js_property_get(self, make_string_item("__transform_end_callback__"));
    js_transform_finish_now(self, callback);
}

// _transform(chunk, encoding, callback) override
extern "C" Item js_transform_write(Item self, Item chunk, Item encoding, Item callback) {
    ensure_keys();
    if (get_type_id(encoding) == LMD_TYPE_FUNC &&
        (callback.item == 0 || get_type_id(callback) == LMD_TYPE_UNDEFINED)) {
        callback = encoding;
        encoding = make_js_undefined();
    }

    bool write_in_progress = js_item_is_true(js_property_get(self, make_string_item("_writing")));
    Item corked = js_property_get(self, make_string_item("_corked"));
    if ((get_type_id(corked) == LMD_TYPE_INT && it2i(corked) > 0) || write_in_progress) {
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
        js_stream_buffer_write_request(self, chunk, encoding, callback);
        if (js_stream_mark_transform_readable_backpressure(self)) accepted = false;
        return js_bool_item(accepted);
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
        js_property_set(self, make_string_item("_writing"), js_bool_item(true));
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
        if (js_stream_mark_transform_readable_backpressure(self)) accepted = false;
        if (!accepted && !js_state_get_bool(js_property_get(self, key_writable_state), "needDrain") &&
            !js_item_is_true(js_property_get(self, make_string_item("_writing")))) {
            // synchronous _transform callbacks can clear needDrain before write() returns;
            // defer the drain so callers observe the backpressure edge they just caused.
            js_state_set_bool(js_property_get(self, key_writable_state), "needDrain", true);
            js_stream_defer_transform_drain(self);
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

    js_property_set(self, make_string_item("__transform_end_pending__"), js_bool_item(true));
    js_property_set(self, make_string_item("__transform_end_callback__"), callback);
    js_transform_maybe_finish_deferred(self);
    return self;
}

extern "C" Item js_transform_new(Item opts) {
    ensure_keys();
    Item obj = js_stream_create_instance(stream_transform_prototype);

    js_class_stamp(obj, JS_CLASS_TRANSFORM);
    js_property_set(obj, key_readable, js_bool_item(true));
    js_property_set(obj, key_writable, js_bool_item(true));
    js_property_set(obj, key_readable_side_enabled, js_bool_item(true));
    js_property_set(obj, key_writable_side_enabled, js_bool_item(true));
    js_stream_set_flowing(obj, false);
    js_property_set(obj, key_ended, js_bool_item(false));
    js_property_set(obj, key_finished, js_bool_item(false));
    js_property_set(obj, key_destroyed, js_bool_item(false));
    js_property_set(obj, make_string_item("destroyed"), js_bool_item(false));
    js_property_set(obj, make_string_item("errored"), ItemNull);
    js_property_set(obj, make_string_item("readableAborted"), js_bool_item(false));
    js_property_set(obj, make_string_item("writableAborted"), js_bool_item(false));
    js_property_set(obj, key_finish_emitted, js_bool_item(false));
    js_property_set(obj, key_end_pending, js_bool_item(false));
    js_property_set(obj, key_end_emitted, js_bool_item(false));
    js_property_set(obj, key_reading, js_bool_item(false));
    js_property_set(obj, key_paused, js_bool_item(false));
    js_property_set(obj, key_close_emitted, js_bool_item(false));
    js_property_set(obj, key_closed, js_bool_item(false));
    js_property_set(obj, key_auto_destroy, js_bool_item(true));
    js_property_set(obj, make_string_item("allowHalfOpen"), js_bool_item(true));
    js_property_set(obj, key_readable_state, js_create_readable_state());
    js_property_set(obj, key_writable_state, js_create_writable_state(obj));
    js_stream_set_writable_corked(obj, 0);
    js_stream_set_buffered_request_count(obj, 0);
    js_stream_define_bool(obj, "readableEnded", false);
    js_stream_define_bool(obj, "writableEnded", false);
    js_stream_define_bool(obj, "writableFinished", false);
    js_stream_set_readable_buffer(obj, js_array_new(0));
    Item listeners = js_new_object();
    js_property_set(obj, key_listeners, listeners);
    js_property_set(obj, make_string_item("_events"), js_new_object());
    js_stream_init_readable_options(obj);
    js_stream_init_writable_options(obj);

    // Readable methods
    js_property_set(obj, key_on, js_new_function((void*)js_stream_inst_on, 2));
    js_property_set(obj, make_string_item("once"), js_new_function((void*)js_stream_inst_once, 2));
    Item off_fn = js_new_function((void*)js_stream_inst_off, 2);
    js_property_set(obj, make_string_item("off"), off_fn);
    js_property_set(obj, make_string_item("removeListener"), off_fn);
    js_property_set(obj, make_string_item("removeAllListeners"), js_new_function((void*)js_stream_inst_removeAllListeners, 1));
    js_property_set(obj, key_emit, js_new_function((void*)js_stream_inst_emit, 2));
    js_property_set(obj, make_string_item("eventNames"), js_new_function((void*)js_stream_inst_eventNames, 0));
    js_property_set(obj, make_string_item("listeners"), js_new_function((void*)js_stream_inst_listeners, 1));
    js_property_set(obj, make_string_item("listenerCount"), js_new_function((void*)js_stream_inst_listenerCount, 2));
    js_property_set(obj, key_push, js_new_function((void*)js_readable_inst_push, 2));
    js_property_set(obj, make_string_item("unshift"), js_new_function((void*)js_readable_inst_unshift, 2));
    js_property_set(obj, key_read, js_new_function((void*)js_readable_inst_read, 1));
    js_property_set(obj, key_pipe, js_new_function((void*)js_readable_inst_pipe, 1));
    js_property_set(obj, make_string_item("unpipe"), js_new_function((void*)js_readable_inst_unpipe, 1));
    js_property_set(obj, make_string_item("resume"), js_new_function((void*)js_readable_inst_resume, 0));
    js_property_set(obj, make_string_item("pause"), js_new_function((void*)js_readable_inst_pause, 0));
    js_property_set(obj, make_string_item("isPaused"), js_new_function((void*)js_readable_inst_isPaused, 0));

    // Writable methods using transform
    js_property_set(obj, key_write, js_new_function((void*)js_transform_inst_write, 3));
    js_property_set(obj, key_end, js_new_function((void*)js_transform_inst_end, 2));
    js_property_set(obj, key_destroy, js_new_function((void*)js_stream_inst_destroy, 2));
    js_property_set(obj, make_string_item("_undestroy"), js_new_function((void*)js_stream_inst_undestroy, 0));
    js_property_set(obj, make_string_item("cork"), js_new_function((void*)js_writable_inst_cork, 0));
    js_property_set(obj, make_string_item("uncork"), js_new_function((void*)js_writable_inst_uncork, 0));
    js_property_set(obj, make_string_item("setEncoding"), js_new_function((void*)js_stream_inst_setEncoding, 1));
    js_property_set(obj, make_string_item("setDefaultEncoding"), js_new_function((void*)js_stream_inst_setDefaultEncoding, 1));
    js_stream_install_async_iterator(obj);
    js_stream_install_readable_helpers(obj);

    if (!propagate_stream_options(obj, opts)) return ItemNull;
    js_stream_call_construct(obj);
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

static Item js_stream_pipeline_pair_streams(Item source, Item dest) {
    Item streams = js_array_new(0);
    js_array_push(streams, source);
    js_array_push(streams, dest);
    return streams;
}

static bool js_stream_pipeline_is_undefined(Item value) {
    return value.item == 0 || value.item == ITEM_JS_UNDEFINED ||
           get_type_id(value) == LMD_TYPE_UNDEFINED;
}

static bool js_stream_pipeline_is_readable_input(Item value) {
    TypeId type = get_type_id(value);
    return js_stream_is_stream_like(value) ||
           type == LMD_TYPE_ARRAY ||
           type == LMD_TYPE_STRING ||
           js_readable_from_is_iterable(value);
}

static Item js_stream_pipeline_invalid_return_value(void) {
    return js_throw_type_error_code("ERR_INVALID_RETURN_VALUE",
        "Expected a stream, iterable, or promise to be returned from the function");
}

static Item js_stream_pipeline_to_stream(Item value) {
    if (js_stream_pipeline_is_undefined(value) ||
        !js_stream_pipeline_is_readable_input(value)) {
        return js_stream_pipeline_invalid_return_value();
    }
    if (js_stream_is_stream_like(value)) return value;
    return js_readable_from(value);
}

static bool js_stream_pipeline_destroy_erred_legacy_stream(Item stream) {
    if (js_stream_is_native_stream(stream) ||
        js_item_is_true(js_property_get(stream, key_destroyed))) {
        return false;
    }

    Item destroy = js_property_get(stream, key_destroy);
    if (get_type_id(destroy) == LMD_TYPE_FUNC) {
        js_stream_mark_destroyed(stream);
        // legacy streams keep cleanup on public destroy(); stored-error streams
        // were skipped to avoid duplicate error emission, so invoke it directly.
        js_call_function(destroy, stream, NULL, 0);
        return true;
    }

    Item close = js_property_get(stream, make_string_item("close"));
    if (get_type_id(close) == LMD_TYPE_FUNC) {
        js_stream_mark_destroyed(stream);
        js_call_function(close, stream, NULL, 0);
        return true;
    }

    return false;
}

static void js_stream_pipeline_destroy_stream_array(Item streams, Item err) {
    if (!js_stream_has_error(err) || get_type_id(streams) != LMD_TYPE_ARRAY) return;
    int64_t len = js_array_length(streams);
    for (int64_t i = 0; i < len; i++) {
        Item stream = js_array_get_int(streams, i);
        if (js_stream_has_stored_error(stream)) {
            js_stream_pipeline_destroy_erred_legacy_stream(stream);
            continue;
        }
        js_stream_destroy(stream, err);
    }
}

static void js_stream_pipeline_cleanup(Item* env, bool terminal_error) {
    if (!env) return;
    Item dest = env[1];
    if (!terminal_error) {
        if (js_item_is_true(js_property_get(dest, key_readable))) {
            js_stream_off(dest, make_string_item("error"), env[5]);
            js_stream_off(dest, make_string_item("finish"), env[7]);
            js_stream_off(dest, make_string_item("end"), env[7]);
        }
        return;
    }

    Item streams = env[9];
    Item error_listener = env[5];
    Item error_event = make_string_item("error");
    if (get_type_id(streams) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(streams);
        for (int64_t i = 0; i < len; i++) {
            Item stream = js_array_get_int(streams, i);
            js_stream_off(stream, error_event, error_listener);
        }
    } else {
        js_stream_off(env[0], error_event, error_listener);
        js_stream_off(env[1], error_event, error_listener);
    }
    js_stream_off(env[0], make_string_item("close"), env[4]);
    js_stream_off(env[1], make_string_item("finish"), env[7]);
    js_stream_off(env[1], make_string_item("end"), env[7]);
}

static void js_stream_pipeline_destroy_streams(Item* env, Item err) {
    if (!env || !js_stream_has_error(err)) return;
    Item streams = env[9];
    if (get_type_id(streams) != LMD_TYPE_ARRAY) {
        js_stream_destroy(env[1], err);
        return;
    }
    js_stream_pipeline_destroy_stream_array(streams, err);
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
    js_stream_pipeline_cleanup(env, js_stream_has_error(err));
    Item callback = env[2];
    if (get_type_id(callback) != LMD_TYPE_FUNC) return make_js_undefined();
    env[8] = err;
    js_next_tick_enqueue(js_new_closure((void*)js_stream_pipeline_invoke_callback, 0, env, 10));
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
    js_stream_pipeline_destroy_streams(env, err);
    return make_js_undefined();
}

static Item js_stream_pipeline_on_error(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    js_stream_pipeline_destroy_streams(env, err);
    return js_stream_pipeline_call_once(env, err);
}

static Item js_stream_pipeline_on_finish(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    return js_stream_pipeline_call_once(env, make_js_undefined());
}

static Item js_stream_pipeline_pair_impl(Item source, Item dest, Item callback, bool connect,
                                         Item streams) {
    ensure_keys();
    Item actual_dest = dest;
    if (get_type_id(dest) == LMD_TYPE_FUNC && get_type_id(callback) == LMD_TYPE_FUNC) {
        actual_dest = js_passthrough_new(make_js_undefined());
        if (get_type_id(streams) == LMD_TYPE_ARRAY && js_array_length(streams) >= 2) {
            js_array_set_int(streams, js_array_length(streams) - 1, actual_dest);
        }
    }
    // simplified two-argument pipeline (most common case)
    // for multi-step, chain pipe() calls
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        Item* env = js_alloc_env(10);
        env[0] = source;
        env[1] = actual_dest;
        env[2] = callback;
        env[3] = js_bool_item(false);
        Item source_close = js_new_closure((void*)js_stream_pipeline_on_close, 0, env, 10);
        Item source_error = js_new_closure((void*)js_stream_pipeline_on_error, 1, env, 10);
        Item dest_finish = js_new_closure((void*)js_stream_pipeline_on_finish, 0, env, 10);
        env[4] = source_close;
        env[5] = source_error;
        env[6] = source_error;
        env[7] = dest_finish;
        env[8] = make_js_undefined();
        env[9] = streams;
        js_stream_once(source, make_string_item("close"), source_close);
        if (get_type_id(streams) == LMD_TYPE_ARRAY) {
            int64_t len = js_array_length(streams);
            for (int64_t i = 0; i < len; i++) {
                js_stream_on(js_array_get_int(streams, i), make_string_item("error"), source_error);
            }
        } else {
            js_stream_on(source, make_string_item("error"), source_error);
            js_stream_on(actual_dest, make_string_item("error"), source_error);
        }
        js_stream_on(actual_dest, make_string_item("finish"), dest_finish);
        if (js_item_is_true(js_property_get(actual_dest, key_readable)) &&
            !js_item_is_true(js_property_get(actual_dest, key_writable))) {
            js_stream_on(actual_dest, make_string_item("end"), dest_finish);
        }
    }
    Item pipe_fn = js_property_get(source, key_pipe);
    if (connect && get_type_id(pipe_fn) == LMD_TYPE_FUNC) {
        js_call_function(pipe_fn, source, &actual_dest, 1);
    }
    return actual_dest;
}

static Item js_stream_pipeline_pair(Item source, Item dest, Item callback) {
    return js_stream_pipeline_pair_impl(source, dest, callback, true,
                                        js_stream_pipeline_pair_streams(source, dest));
}

extern "C" Item js_stream_pipeline(Item source, Item dest, Item callback) {
    return js_stream_pipeline_pair(source, dest, callback);
}

static Item js_stream_pipeline_function_sink_call_done(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[3])) return make_js_undefined();
    env[3] = js_bool_item(true);
    Item callback = env[1];
    if (get_type_id(callback) != LMD_TYPE_FUNC) return make_js_undefined();
    if (js_stream_has_error(err)) {
        js_stream_pipeline_destroy_stream_array(env[2], err);
        js_call_function(callback, ItemNull, &err, 1);
    } else {
        if (js_stream_pipeline_is_undefined(err)) {
            js_call_function(callback, ItemNull, NULL, 0);
        } else {
            Item args[2] = { make_js_undefined(), err };
            js_call_function(callback, ItemNull, args, 2);
        }
    }
    return make_js_undefined();
}

static Item js_stream_pipeline_function_sink_on_error(Item env_item, Item err) {
    return js_stream_pipeline_function_sink_call_done(env_item, err);
}

static Item js_stream_pipeline_function_dest(Item source, Item dest, Item callback, Item streams) {
    Item args[1] = { source };
    Item result = js_call_function(dest, make_js_undefined(), args, 1);
    if (js_check_exception()) {
        Item err = js_clear_exception();
        js_stream_pipeline_destroy_stream_array(streams, err);
        if (get_type_id(callback) == LMD_TYPE_FUNC) js_call_function(callback, ItemNull, &err, 1);
        return make_js_undefined();
    }

    if (js_stream_pipeline_is_undefined(result)) {
        return js_stream_pipeline_invalid_return_value();
    }

    Item then_fn = js_property_get(result, make_string_item("then"));
    if (get_type_id(then_fn) == LMD_TYPE_FUNC) {
        Item* env = js_alloc_env(4);
        env[0] = make_js_undefined();
        env[1] = callback;
        env[2] = streams;
        env[3] = js_bool_item(false);
        Item on_done = js_new_closure((void*)js_stream_pipeline_function_sink_call_done, 1, env, 4);
        Item on_error = js_new_closure((void*)js_stream_pipeline_function_sink_on_error, 1, env, 4);
        js_promise_then(result, on_done, on_error);
        return make_js_undefined();
    }

    Item stream = js_stream_pipeline_to_stream(result);
    if (js_check_exception()) return ItemNull;
    if (get_type_id(streams) == LMD_TYPE_ARRAY) js_array_push(streams, stream);
    return js_stream_pipeline_pair_impl(source, stream, callback, false, streams);
}

static Item js_stream_pipeline_prepare_source(Item source) {
    if (!js_stream_is_stream_like(source) && get_type_id(source) == LMD_TYPE_FUNC) {
        source = js_call_function(source, make_js_undefined(), NULL, 0);
        if (js_check_exception()) return ItemNull;
        source = js_stream_pipeline_to_stream(source);
        if (js_check_exception()) return ItemNull;
        return source;
    }
    if (!js_stream_is_stream_like(source)) {
        source = js_readable_from(source);
        if (js_check_exception()) return ItemNull;
    }
    return source;
}

static Item js_stream_pipeline_rest(Item rest_args) {
    ensure_keys();
    int64_t argc = js_array_length(rest_args);
    if (argc == 0) {
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "ERR_INVALID_ARG_TYPE: The \"callback\" argument must be of type function. Received undefined");
    }

    Item callback = js_array_get_int(rest_args, argc - 1);
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        return js_throw_invalid_arg_type("callback", "function", callback);
    }
    if (argc < 3) {
        if (argc == 2) {
            Item first_arg = js_array_get_int(rest_args, 0);
            if (get_type_id(first_arg) == LMD_TYPE_ARRAY) {
                int64_t array_len = js_array_length(first_arg);
                if (array_len >= 2) {
                    Item expanded = js_array_new(0);
                    for (int64_t i = 0; i < array_len; i++) {
                        js_array_push(expanded, js_array_get_int(first_arg, i));
                    }
                    js_array_push(expanded, callback);
                    return js_stream_pipeline_rest(expanded);
                }
            }
        }
        return js_throw_type_error_code("ERR_MISSING_ARGS",
            "ERR_MISSING_ARGS: The \"streams\" argument is required");
    }

    int64_t stream_count = argc - 1;
    if (stream_count == 2) {
        Item source = js_array_get_int(rest_args, 0);
        Item dest = js_array_get_int(rest_args, 1);
        source = js_stream_pipeline_prepare_source(source);
        if (js_check_exception()) return ItemNull;
        if (get_type_id(dest) == LMD_TYPE_FUNC) {
            Item streams = js_array_new(0);
            js_array_push(streams, source);
            return js_stream_pipeline_function_dest(source, dest, callback, streams);
        }
        return js_stream_pipeline_pair(source, dest, callback);
    }

    Item first = js_array_get_int(rest_args, 0);
    first = js_stream_pipeline_prepare_source(first);
    if (js_check_exception()) return ItemNull;

    Item streams = js_array_new(0);
    js_array_push(streams, first);
    Item previous = first;
    for (int64_t i = 1; i < stream_count; i++) {
        Item dest = js_array_get_int(rest_args, i);
        if (i == stream_count - 1 && get_type_id(dest) == LMD_TYPE_FUNC) {
            return js_stream_pipeline_function_dest(previous, dest, callback, streams);
        }
        if (get_type_id(dest) == LMD_TYPE_FUNC) {
            Item args[1] = { previous };
            Item result = js_call_function(dest, make_js_undefined(), args, 1);
            if (js_check_exception()) return ItemNull;
            dest = js_stream_pipeline_to_stream(result);
            if (js_check_exception()) return ItemNull;
            js_array_push(streams, dest);
            previous = dest;
            continue;
        }
        Item pipe_fn = js_property_get(previous, key_pipe);
        if (get_type_id(pipe_fn) != LMD_TYPE_FUNC) {
            return js_throw_invalid_arg_type("streams", "stream", previous);
        }
        js_call_function(pipe_fn, previous, &dest, 1);
        if (js_check_exception()) return ItemNull;
        js_array_push(streams, dest);
        previous = dest;
    }

    return js_stream_pipeline_pair_impl(first, previous, callback, false, streams);
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
    step = js_promise_resolve(step);

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
    Item opts = js_new_object();
    js_property_set(opts, make_string_item("objectMode"), js_bool_item(true));
    Item readable = js_readable_new(opts);
    js_stream_set_readable_object_mode(readable, true);

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
    Item resource = env[2];
    int64_t has_arg = js_stream_has_error(err) ? 1 : 0;
    Item previous = js_async_hooks_enter_resource(resource);
    Item result;
    if (get_type_id(context) == LMD_TYPE_ARRAY && js_array_length(context) > 0) {
        result = js_als_context_call(context, callback, js_get_this(), err, has_arg);
    } else {
        Item args[1] = {err};
        result = js_call_function(callback, js_get_this(), args, (int)has_arg);
    }
    js_async_hooks_restore_resource(previous);
    return result;
}

static Item js_stream_finished_context_wrapper(Item callback) {
    Item context = js_als_capture_context();
    Item resource = js_async_hooks_create_resource("STREAM_END_OF_STREAM", 20);
    Item* env = js_alloc_env(3);
    env[0] = callback;
    env[1] = context;
    env[2] = resource;
    Item wrapper = js_new_closure((void*)js_stream_finished_context_callback, 1, env, 3);
    js_property_set(callback, js_stream_finished_wrapper_key(), wrapper);
    return wrapper;
}

static bool js_stream_finished_side_done(Item stream, bool check_readable, bool check_writable) {
    if (js_item_is_true(js_property_get(stream, make_string_item("__compose_pending__")))) {
        return false;
    }
    if (!check_readable && !check_writable) return true;
    bool readable_done = true;
    bool writable_done = true;
    if (check_readable && js_stream_has_readable_side(stream) &&
        js_stream_readable_side_enabled(stream)) {
        bool native_readable_state =
            get_type_id(js_property_get(stream, key_readable_side_enabled)) == LMD_TYPE_BOOL ||
            get_type_id(js_property_get(stream, key_readable)) == LMD_TYPE_BOOL;
        readable_done = js_item_is_true(js_property_get(stream, key_end_emitted)) ||
                        (native_readable_state &&
                         js_state_get_bool(js_property_get(stream, key_readable_state), "endEmitted"));
    }
    if (check_writable && js_stream_has_writable_side(stream) &&
        js_stream_writable_side_enabled(stream)) {
        Item writable_state = js_property_get(stream, key_writable_state);
        bool native_writable_state =
            get_type_id(js_property_get(stream, key_writable_side_enabled)) == LMD_TYPE_BOOL ||
            get_type_id(js_property_get(stream, key_writable)) == LMD_TYPE_BOOL;
        bool legacy_no_pendingcb_done =
            native_writable_state &&
            js_state_get_bool(writable_state, "ended") &&
            !js_stream_writable_state_has_pendingcb(writable_state);
        writable_done = js_item_is_true(js_property_get(stream, key_finish_emitted)) ||
                        js_item_is_true(js_property_get(stream, key_finished)) ||
                        legacy_no_pendingcb_done ||
                        (native_writable_state &&
                         js_state_get_bool(writable_state, "finished"));
    }
    return readable_done && writable_done;
}

static bool js_stream_finished_missing_terminal_on_close(Item stream,
                                                        bool check_readable,
                                                        bool check_writable,
                                                        bool destroyed) {
    if (check_readable && js_stream_has_readable_side(stream) &&
        js_stream_readable_side_enabled(stream)) {
        bool readable_terminal = js_item_is_true(js_property_get(stream, key_end_emitted)) ||
                                 (!destroyed &&
                                  js_item_is_true(js_property_get(stream, key_end_pending))) ||
                                 js_state_get_bool(js_property_get(stream, key_readable_state), "endEmitted");
        if (!readable_terminal) return true;
    }
    if (check_writable && js_stream_has_writable_side(stream) &&
        js_stream_writable_side_enabled(stream)) {
        bool writable_terminal = js_item_is_true(js_property_get(stream, key_finish_emitted)) ||
                                 js_item_is_true(js_property_get(stream, key_finished));
        if (!writable_terminal) return true;
    }
    return false;
}

static bool js_stream_finished_option_has_value(Item value) {
    if (value.item == 0) return false;
    TypeId type = get_type_id(value);
    return type != LMD_TYPE_UNDEFINED && type != LMD_TYPE_NULL;
}

static bool js_stream_finished_option_to_bool(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_BOOL) return it2b(value);
    if (type == LMD_TYPE_INT) return it2i(value) != 0;
    if (type == LMD_TYPE_INT64) return it2l(value) != 0;
    if (type == LMD_TYPE_FLOAT) {
        double number = it2d(value);
        return number != 0.0 && number == number;
    }
    if (type == LMD_TYPE_STRING) {
        String* str = it2s(value);
        return str && str->len > 0;
    }
    return true;
}

static void js_stream_finished_apply_side_option(Item options, const char* name, bool* check_side) {
    Item value = js_property_get(options, make_string_item(name));
    if (js_stream_finished_option_has_value(value)) {
        *check_side = js_stream_finished_option_to_bool(value);
    }
}

static Item js_stream_finished_abort_error(Item signal) {
    Item reason = js_property_get(signal, make_string_item("reason"));
    if (js_stream_has_error(reason)) return reason;
    return js_stream_iter_make_abort_error();
}

static bool js_stream_finished_options_sync_callback(Item options) {
    if (get_type_id(options) != LMD_TYPE_MAP && get_type_id(options) != LMD_TYPE_ELEMENT) {
        return false;
    }

    Item symbols = js_object_get_own_property_symbols(options);
    if (js_check_exception()) {
        js_clear_exception();
        return false;
    }
    if (get_type_id(symbols) != LMD_TYPE_ARRAY) return false;

    int64_t len = js_array_length(symbols);
    for (int64_t i = 0; i < len; i++) {
        Item symbol = js_array_get_int(symbols, i);
        Item description = js_symbol_get_description(symbol);
        if (js_stream_string_equals(description, "kEosNodeSynchronousCallback")) {
            return js_item_is_true(js_property_get(options, symbol));
        }
    }
    return false;
}

static bool js_stream_is_native_stream(Item stream) {
    JsClass cls = js_class_id(stream);
    return cls == JS_CLASS_READABLE || cls == JS_CLASS_WRITABLE ||
           cls == JS_CLASS_DUPLEX || cls == JS_CLASS_TRANSFORM ||
           cls == JS_CLASS_PASS_THROUGH;
}

static bool js_stream_finished_expects_close(Item stream) {
    if (js_item_is_true(js_property_get(stream, key_close_emitted)) ||
        js_item_is_true(js_property_get(stream, key_closed))) {
        return false;
    }
    Item auto_destroy = js_property_get(stream, key_auto_destroy);
    if (get_type_id(auto_destroy) == LMD_TYPE_BOOL) return it2b(auto_destroy);
    return !js_stream_is_native_stream(stream);
}

static bool js_stream_finished_expects_close_for_checks(Item stream,
                                                        bool check_readable,
                                                        bool check_writable) {
    if (!check_readable || !check_writable) return false;
    return js_stream_finished_expects_close(stream);
}

static void js_stream_finished_add_listener(Item stream, Item event_item, Item listener) {
    if (js_stream_is_native_stream(stream)) {
        js_stream_on(stream, event_item, listener);
        return;
    }
    Item on_fn = js_property_get(stream, key_on);
    if (get_type_id(on_fn) == LMD_TYPE_FUNC) {
        Item args[2] = {event_item, listener};
        js_call_function(on_fn, stream, args, 2);
    }
}

static void js_stream_finished_remove_listener(Item stream, Item event_item, Item listener) {
    if (js_stream_is_native_stream(stream)) {
        js_stream_off(stream, event_item, listener);
        return;
    }
    Item off_fn = js_property_get(stream, make_string_item("removeListener"));
    if (get_type_id(off_fn) != LMD_TYPE_FUNC) {
        off_fn = js_property_get(stream, make_string_item("off"));
    }
    if (get_type_id(off_fn) == LMD_TYPE_FUNC) {
        Item args[2] = {event_item, listener};
        js_call_function(off_fn, stream, args, 2);
    }
}

static void js_stream_finished_remove_all(Item* env) {
    if (!env) return;
    Item stream = env[0];
    js_stream_finished_remove_listener(stream, make_string_item("end"), env[3]);
    js_stream_finished_remove_listener(stream, make_string_item("finish"), env[4]);
    js_stream_finished_remove_listener(stream, make_string_item("error"), env[5]);
    js_stream_finished_remove_listener(stream, make_string_item("close"), env[6]);
    Item signal = env[7];
    Item abort_listener = env[8];
    Item remove_event = js_property_get(signal, make_string_item("removeEventListener"));
    if (get_type_id(remove_event) == LMD_TYPE_FUNC &&
        get_type_id(abort_listener) == LMD_TYPE_FUNC) {
        Item args[2] = { make_string_item("abort"), abort_listener };
        js_call_function(remove_event, signal, args, 2);
    }
}

static Item js_stream_finished_dispose(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    js_stream_finished_remove_all(env);
    if (env) env[2] = js_bool_item(true);
    return make_js_undefined();
}

static Item js_stream_finished_emit_callback(Item* env, Item err) {
    if (!env || js_item_is_true(env[2])) return make_js_undefined();
    env[2] = js_bool_item(true);
    if (js_item_is_true(env[10])) {
        js_stream_finished_remove_all(env);
    }
    Item callback = env[1];
    if (get_type_id(callback) != LMD_TYPE_FUNC) return make_js_undefined();
    if (js_stream_has_error(err)) {
        js_call_function(callback, ItemNull, &err, 1);
    } else {
        js_call_function(callback, ItemNull, NULL, 0);
    }
    return make_js_undefined();
}

static Item js_stream_finished_emit_callback_tick(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    return js_stream_finished_emit_callback(env, env[9]);
}

static Item js_stream_finished_on_end(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    if (!js_stream_finished_side_done(env[0], js_item_is_true(env[11]),
                                      js_item_is_true(env[12]))) {
        return make_js_undefined();
    }
    if (js_stream_finished_expects_close_for_checks(env[0], js_item_is_true(env[11]),
                                                    js_item_is_true(env[12]))) {
        return make_js_undefined();
    }
    return js_stream_finished_emit_callback(env, make_js_undefined());
}

static Item js_stream_finished_on_finish(Item env_item) {
    return js_stream_finished_on_end(env_item);
}

static Item js_stream_finished_on_error(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    env[9] = err;
    if (js_stream_finished_expects_close_for_checks(env[0], js_item_is_true(env[11]),
                                                    js_item_is_true(env[12]))) {
        return make_js_undefined();
    }
    return js_stream_finished_emit_callback(env, err);
}

static Item js_stream_finished_on_close(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    bool check_readable = js_item_is_true(env[11]);
    bool check_writable = js_item_is_true(env[12]);
    Item err = js_stream_get_stored_error(env[0]);
    if (!js_stream_has_error(err) && js_stream_has_error(env[9])) {
        err = env[9];
    }
    bool destroyed = js_item_is_true(js_property_get(env[0], key_destroyed));
    if (!js_stream_has_error(err) &&
        js_stream_finished_missing_terminal_on_close(env[0], check_readable, check_writable,
                                                    destroyed)) {
        err = js_stream_make_error_with_code("ERR_STREAM_PREMATURE_CLOSE",
            "Premature close");
    }
    return js_stream_finished_emit_callback(env, err);
}

static Item js_stream_finished_on_abort(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item err = js_stream_finished_abort_error(env[7]);
    return js_stream_finished_emit_callback(env, err);
}

static Item js_stream_finished_call_now(Item callback, Item err) {
    Item registered_callback = js_stream_finished_context_wrapper(callback);
    if (js_stream_has_error(err)) {
        return js_call_function(registered_callback, ItemNull, &err, 1);
    }
    return js_call_function(registered_callback, ItemNull, NULL, 0);
}

static void js_stream_finished_call_later(Item* env, Item err) {
    if (!env) return;
    env[9] = err;
    js_next_tick_enqueue(js_new_closure((void*)js_stream_finished_emit_callback_tick, 0, env, 13));
}

static bool js_stream_finished_options_cleanup(Item options, bool* cleanup) {
    *cleanup = false;
    if (get_type_id(options) != LMD_TYPE_MAP && get_type_id(options) != LMD_TYPE_ELEMENT) {
        return true;
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

static Item js_stream_finished_impl(Item stream, Item options, Item callback) {
    ensure_keys();
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        return js_throw_invalid_arg_type("callback", "function", callback);
    }
    if (!js_stream_is_stream_like(stream)) {
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "ERR_INVALID_ARG_TYPE: The \"stream\" argument must be an instance of stream.Stream.");
    }

    Item signal = make_js_undefined();
    bool cleanup = false;
    bool check_readable = true;
    bool check_writable = true;
    if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_ELEMENT) {
        signal = js_property_get(options, make_string_item("signal"));
        if (signal.item != 0 && get_type_id(signal) != LMD_TYPE_UNDEFINED &&
            get_type_id(signal) != LMD_TYPE_NULL && !js_stream_is_abort_signal(signal)) {
            return js_throw_invalid_arg_type("options.signal", "AbortSignal", signal);
        }
        if (!js_stream_finished_options_cleanup(options, &cleanup)) return ItemNull;
        js_stream_finished_apply_side_option(options, "readable", &check_readable);
        js_stream_finished_apply_side_option(options, "writable", &check_writable);
    } else if (options.item != 0 && get_type_id(options) != LMD_TYPE_UNDEFINED &&
               get_type_id(options) != LMD_TYPE_NULL) {
        return js_throw_invalid_arg_type("options", "object", options);
    }

    Item registered_callback = js_stream_finished_context_wrapper(callback);
    bool sync_callback = js_stream_finished_options_sync_callback(options);
    Item* env = js_alloc_env(13);
    env[0] = stream;
    env[1] = registered_callback;
    env[2] = js_bool_item(false);
    env[3] = make_js_undefined();
    env[4] = make_js_undefined();
    env[5] = make_js_undefined();
    env[6] = make_js_undefined();
    env[7] = signal;
    env[8] = make_js_undefined();
    env[9] = make_js_undefined();
    env[10] = js_bool_item(cleanup);
    env[11] = js_bool_item(check_readable);
    env[12] = js_bool_item(check_writable);
    Item dispose = js_new_closure((void*)js_stream_finished_dispose, 0, env, 13);

    if (js_stream_is_abort_signal(signal)) {
        Item aborted = js_property_get(signal, make_string_item("aborted"));
        if (get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted)) {
            Item err = js_stream_finished_abort_error(signal);
            if (sync_callback) {
                js_stream_finished_call_now(callback, err);
                env[2] = js_bool_item(true);
            } else {
                js_stream_finished_call_later(env, err);
            }
            return dispose;
        }
        Item add_event = js_property_get(signal, make_string_item("addEventListener"));
        if (get_type_id(add_event) == LMD_TYPE_FUNC) {
            Item abort_listener = js_new_closure((void*)js_stream_finished_on_abort, 0, env, 13);
            env[8] = abort_listener;
            Item args[2] = { make_string_item("abort"), abort_listener };
            js_call_function(add_event, signal, args, 2);
        }
    }

    // check if already finished/destroyed
    Item fin = js_property_get(stream, key_finished);
    Item des = js_property_get(stream, key_destroyed);
    bool side_done = js_stream_finished_side_done(stream, check_readable, check_writable);
    bool expects_close = js_stream_finished_expects_close_for_checks(stream, check_readable, check_writable);
    bool is_done = (side_done && !expects_close) ||
                   (get_type_id(fin) == LMD_TYPE_BOOL && it2b(fin) && !expects_close) ||
                   (get_type_id(des) == LMD_TYPE_BOOL && it2b(des) &&
                    !js_stream_destroy_pending(stream) && !expects_close);

    if (is_done) {
        Item err = js_stream_get_stored_error(stream);
        if (!js_stream_has_error(err)) err = make_js_undefined();
        if (!js_stream_has_error(err) && !side_done &&
            js_item_is_true(js_property_get(stream, key_destroyed))) {
            err = js_stream_make_error_with_code("ERR_STREAM_PREMATURE_CLOSE",
                "Premature close");
        }
        if (sync_callback) {
            js_stream_finished_call_now(callback, err);
            env[2] = js_bool_item(true);
        } else {
            js_stream_finished_call_later(env, err);
        }
        return dispose;
    }

    // register on stream-local events; stream_emit() reads this listener table.
    Item end_event = make_string_item("end");
    Item finish_event = make_string_item("finish");
    Item error_event = make_string_item("error");
    Item close_event = make_string_item("close");

    env[3] = js_new_closure((void*)js_stream_finished_on_end, 0, env, 13);
    env[4] = js_new_closure((void*)js_stream_finished_on_finish, 0, env, 13);
    env[5] = js_new_closure((void*)js_stream_finished_on_error, 1, env, 13);
    env[6] = js_new_closure((void*)js_stream_finished_on_close, 0, env, 13);

    js_stream_finished_add_listener(stream, end_event, env[3]);
    js_stream_finished_add_listener(stream, finish_event, env[4]);
    js_stream_finished_add_listener(stream, error_event, env[5]);
    js_stream_finished_add_listener(stream, close_event, env[6]);

    return dispose;
}

extern "C" Item js_stream_finished(Item stream, Item callback) {
    return js_stream_finished_impl(stream, make_js_undefined(), callback);
}

static Item js_stream_finished_rest(Item rest_args) {
    int64_t argc = js_array_length(rest_args);
    if (argc < 2) {
        Item callback = argc > 1 ? js_array_get_int(rest_args, 1) : make_js_undefined();
        return js_throw_invalid_arg_type("callback", "function", callback);
    }
    Item stream = js_array_get_int(rest_args, 0);
    Item second = js_array_get_int(rest_args, 1);
    if (get_type_id(second) == LMD_TYPE_FUNC) {
        return js_stream_finished_impl(stream, make_js_undefined(), second);
    }
    Item options = second;
    Item callback = argc > 2 ? js_array_get_int(rest_args, 2) : make_js_undefined();
    return js_stream_finished_impl(stream, options, callback);
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
    if (cls == JS_CLASS_CLIENT_REQUEST || cls == JS_CLASS_INCOMING_MESSAGE ||
        cls == JS_CLASS_SERVER_RESPONSE || cls == JS_CLASS_SOCKET ||
        cls == JS_CLASS_TLS_SOCKET) {
        return true;
    }
    if (get_type_id(stream) != LMD_TYPE_MAP) return false;
    Item on_fn = js_property_get(stream, key_on);
    if (get_type_id(on_fn) != LMD_TYPE_FUNC) return false;
    if (js_stream_has_readable_side(stream) || js_stream_has_writable_side(stream)) return true;
    Item pipe_fn = js_property_get(stream, key_pipe);
    Item destroy_fn = js_property_get(stream, key_destroy);
    return get_type_id(pipe_fn) == LMD_TYPE_FUNC || get_type_id(destroy_fn) == LMD_TYPE_FUNC;
}

static Item js_stream_compose_normalize(Item stream) {
    if (get_type_id(stream) == LMD_TYPE_FUNC) {
        Item opts = js_new_object();
        js_property_set(opts, make_string_item("objectMode"), js_bool_item(true));
        Item input = js_passthrough_new(opts);
        if (js_check_exception()) return ItemNull;
        return js_readable_compose(input, stream, make_js_undefined());
    }
    if (get_type_id(stream) == LMD_TYPE_MAP || get_type_id(stream) == LMD_TYPE_ELEMENT) {
        Item readable = js_property_get(stream, make_string_item("readable"));
        Item writable = js_property_get(stream, make_string_item("writable"));
        if (js_class_id(readable) == JS_CLASS_READABLE_STREAM ||
            js_class_id(writable) == JS_CLASS_WRITABLE_STREAM) {
            return js_duplex_from(stream);
        }
    }
    if (js_stream_is_stream_like(stream)) return stream;
    return js_readable_from(stream);
}

static bool js_stream_compose_is_async_sink_function(Item stream) {
    if (get_type_id(stream) != LMD_TYPE_FUNC) return false;
    JsStreamFuncFlagsAccess* fn = (JsStreamFuncFlagsAccess*)stream.function;
    if (!fn) return false;
    return (fn->flags & JS_STREAM_FUNC_FLAG_ASYNC) &&
           !(fn->flags & JS_STREAM_FUNC_FLAG_GENERATOR) &&
           !(fn->flags & JS_STREAM_FUNC_FLAG_ASYNC_GEN);
}

static Item js_stream_compose_sink_maybe_complete(Item* env) {
    if (!env || js_item_is_true(env[5])) return make_js_undefined();
    if (!js_item_is_true(env[3])) return make_js_undefined();
    if (!js_item_is_true(env[4])) return make_js_undefined();

    env[5] = js_bool_item(true);
    Item out = env[0];
    Item callback = env[2];
    Item err = env[6];
    js_property_set(out, make_string_item("__compose_pending__"), js_bool_item(false));
    if (js_stream_has_error(err)) {
        if (get_type_id(callback) == LMD_TYPE_FUNC) {
            js_call_function(callback, make_js_undefined(), &err, 1);
        }
        js_stream_destroy(out, err);
        return make_js_undefined();
    }

    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, make_js_undefined(), NULL, 0);
    } else {
        stream_emit(out, "finish", NULL, 0);
    }
    return make_js_undefined();
}

static Item js_stream_compose_sink_fulfilled(Item env_item, Item value) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[3])) return make_js_undefined();
    env[3] = js_bool_item(true);
    if (get_type_id(value) != LMD_TYPE_UNDEFINED && get_type_id(value) != LMD_TYPE_NULL) {
        env[6] = js_stream_make_type_error_with_code("ERR_INVALID_RETURN_VALUE",
            "Expected undefined to be returned from the function");
    }
    return js_stream_compose_sink_maybe_complete(env);
}

static Item js_stream_compose_sink_rejected(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[3])) return make_js_undefined();
    env[3] = js_bool_item(true);
    env[6] = err;
    return js_stream_compose_sink_maybe_complete(env);
}

static Item js_stream_compose_sink_write(Item env_item, Item chunk, Item encoding, Item callback) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item writable = env[1];
    Item write_fn = js_property_get(writable, key_write);
    if (get_type_id(write_fn) != LMD_TYPE_FUNC) {
        if (get_type_id(callback) == LMD_TYPE_FUNC)
            js_call_function(callback, make_js_undefined(), NULL, 0);
        return make_js_undefined();
    }
    Item args[3] = { chunk, encoding, callback };
    js_call_function(write_fn, writable, args, 3);
    return make_js_undefined();
}

static Item js_stream_compose_sink_final(Item env_item, Item callback) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    env[2] = callback;
    env[4] = js_bool_item(true);
    Item writable = env[1];
    Item end_fn = js_property_get(writable, key_end);
    if (get_type_id(end_fn) == LMD_TYPE_FUNC) {
        Item args[1] = { make_js_undefined() };
        js_call_function(end_fn, writable, args, 1);
        if (js_check_exception()) return ItemNull;
    }
    return js_stream_compose_sink_maybe_complete(env);
}

static Item js_stream_compose_sink_destroy(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item writable = env[1];
    Item destroy_fn = js_property_get(writable, key_destroy);
    if (get_type_id(destroy_fn) == LMD_TYPE_FUNC) {
        Item args[1] = { err };
        js_call_function(destroy_fn, writable, args, 1);
    }
    return env[0];
}

static Item js_stream_compose_async_sink(Item first, Item source, Item sink) {
    bool has_writable = js_stream_has_writable_side(first);
    Item opts = js_new_object();
    js_property_set(opts, make_string_item("readable"), js_bool_item(false));
    js_property_set(opts, make_string_item("writable"), js_bool_item(has_writable));
    Item out = js_duplex_new(opts);
    if (js_check_exception()) return ItemNull;
    js_property_set(out, make_string_item("__compose_pending__"), js_bool_item(true));

    Item* env = js_alloc_env(7);
    env[0] = out;
    env[1] = first;
    env[2] = make_js_undefined();
    env[3] = js_bool_item(false);
    env[4] = js_bool_item(!has_writable);
    env[5] = js_bool_item(false);
    env[6] = make_js_undefined();

    if (has_writable) {
        js_property_set(out, make_string_item("_write"),
                        js_new_closure((void*)js_stream_compose_sink_write, 3, env, 7));
        js_property_set(out, make_string_item("_final"),
                        js_new_closure((void*)js_stream_compose_sink_final, 1, env, 7));
        js_property_set(out, make_string_item("_destroy"),
                        js_new_closure((void*)js_stream_compose_sink_destroy, 1, env, 7));
    }

    Item args[1] = { source };
    Item result = js_call_function(sink, make_js_undefined(), args, 1);
    if (js_check_exception()) {
        env[3] = js_bool_item(true);
        env[6] = js_clear_exception();
        js_stream_compose_sink_maybe_complete(env);
        return out;
    }
    Item then_fn = js_property_get(result, make_string_item("then"));
    if (get_type_id(then_fn) != LMD_TYPE_FUNC) {
        env[3] = js_bool_item(true);
        env[6] = js_stream_make_type_error_with_code("ERR_INVALID_RETURN_VALUE",
            "Expected a promise to be returned from the function");
        js_stream_compose_sink_maybe_complete(env);
        return out;
    }

    Item on_done = js_new_closure((void*)js_stream_compose_sink_fulfilled, 1, env, 7);
    Item on_error = js_new_closure((void*)js_stream_compose_sink_rejected, 1, env, 7);
    js_promise_then(result, on_done, on_error);
    return out;
}

static Item js_stream_compose_tail_maybe_complete(Item* env) {
    if (!env || js_item_is_true(env[5])) return make_js_undefined();
    if (!js_item_is_true(env[3]) || !js_item_is_true(env[4])) return make_js_undefined();
    env[5] = js_bool_item(true);
    Item callback = env[2];
    Item err = env[6];
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        if (js_stream_has_error(err)) {
            js_call_function(callback, make_js_undefined(), &err, 1);
        } else {
            js_call_function(callback, make_js_undefined(), NULL, 0);
        }
    }
    if (js_stream_has_error(err)) js_stream_destroy(env[0], err);
    return make_js_undefined();
}

static Item js_stream_compose_tail_on_finish(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    env[3] = js_bool_item(true);
    return js_stream_compose_tail_maybe_complete(env);
}

static Item js_stream_compose_tail_on_error(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    env[3] = js_bool_item(true);
    env[6] = err;
    return js_stream_compose_tail_maybe_complete(env);
}

static Item js_stream_compose_tail_write(Item env_item, Item chunk, Item encoding, Item callback) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item write_fn = js_property_get(env[1], key_write);
    if (get_type_id(write_fn) != LMD_TYPE_FUNC) {
        if (get_type_id(callback) == LMD_TYPE_FUNC)
            js_call_function(callback, make_js_undefined(), NULL, 0);
        return make_js_undefined();
    }
    Item args[3] = { chunk, encoding, js_duplex_from_make_forward_callback(callback) };
    js_call_function(write_fn, env[1], args, 3);
    return make_js_undefined();
}

static Item js_stream_compose_tail_final(Item env_item, Item callback) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    env[2] = callback;
    env[4] = js_bool_item(true);
    Item end_fn = js_property_get(env[1], key_end);
    if (get_type_id(end_fn) == LMD_TYPE_FUNC) {
        Item args[1] = { make_js_undefined() };
        js_call_function(end_fn, env[1], args, 1);
        if (js_check_exception()) return ItemNull;
    }
    return js_stream_compose_tail_maybe_complete(env);
}

static Item js_stream_compose_writable_tail(Item first, Item last) {
    if (!js_item_is_true(js_property_get(first, key_writable))) return last;
    Item opts = js_new_object();
    js_property_set(opts, make_string_item("readable"), js_bool_item(false));
    js_property_set(opts, make_string_item("writable"), js_bool_item(true));
    Item first_writable_state = js_property_get(first, key_writable_state);
    js_property_set(opts, make_string_item("writableObjectMode"),
                    js_bool_item(js_state_get_bool(first_writable_state, "objectMode")));
    Item out = js_duplex_new(opts);
    if (js_check_exception()) return ItemNull;

    Item* env = js_alloc_env(7);
    env[0] = out;
    env[1] = first;
    env[2] = make_js_undefined();
    env[3] = js_bool_item(false);
    env[4] = js_bool_item(false);
    env[5] = js_bool_item(false);
    env[6] = make_js_undefined();

    js_property_set(out, make_string_item("_write"),
                    js_new_closure((void*)js_stream_compose_tail_write, 3, env, 7));
    js_property_set(out, make_string_item("_final"),
                    js_new_closure((void*)js_stream_compose_tail_final, 1, env, 7));
    js_stream_once(last, make_string_item("finish"),
                   js_new_closure((void*)js_stream_compose_tail_on_finish, 0, env, 7));
    js_stream_once(last, make_string_item("error"),
                   js_new_closure((void*)js_stream_compose_tail_on_error, 1, env, 7));
    return out;
}

static Item js_stream_compose_rest(Item rest_args) {
    ensure_keys();
    int64_t argc = js_array_length(rest_args);
    if (argc <= 0) {
        return js_throw_type_error_code("ERR_MISSING_ARGS",
            "The \"streams\" argument must be specified");
    }

    if (argc == 1) {
        Item only = js_array_get_int(rest_args, 0);
        if (js_stream_compose_is_async_sink_function(only)) {
            Item opts = js_new_object();
            js_property_set(opts, make_string_item("objectMode"), js_bool_item(true));
            Item input = js_passthrough_new(opts);
            if (js_check_exception()) return ItemNull;
            return js_stream_compose_async_sink(input, input, only);
        }
    }

    Item first = js_stream_compose_normalize(js_array_get_int(rest_args, 0));
    if (js_check_exception()) return ItemNull;
    Item previous = first;
    Item last = first;
    for (int64_t i = 1; i < argc; i++) {
        Item raw_next = js_array_get_int(rest_args, i);
        if (i == argc - 1 && js_stream_compose_is_async_sink_function(raw_next)) {
            return js_stream_compose_async_sink(first, previous, raw_next);
        }
        Item next = js_stream_compose_normalize(raw_next);
        if (js_check_exception()) return ItemNull;
        if (!js_item_is_true(js_property_get(previous, key_readable)) ||
            !js_item_is_true(js_property_get(next, key_writable))) {
            return js_throw_type_error_code("ERR_INVALID_ARG_VALUE",
                "The argument 'stream' must be writable and readable.");
        }
        js_readable_pipe(previous, next);
        js_readable_compose_attach_error_forward(previous, next);
        previous = next;
        last = next;
    }

    bool first_writable_open = js_item_is_true(js_property_get(first, key_writable));
    bool last_readable_open = js_item_is_true(js_property_get(last, key_readable));
    if (first.item == last.item) return first;
    if (!first_writable_open && !last_readable_open) {
        return last;
    }
    if (!last_readable_open) {
        return js_stream_compose_writable_tail(first, last);
    }

    Item pair = js_new_object();
    if (first_writable_open)
        js_property_set(pair, make_string_item("writable"), first);
    if (last_readable_open)
        js_property_set(pair, make_string_item("readable"), last);
    return js_duplex_from(pair);
}

extern "C" Item js_stream_addAbortSignalNoValidate(Item signal, Item stream) {
    ensure_keys();
    return js_stream_attach_abort_signal(signal, stream);
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

extern "C" Item js_stream_get_readableLength(void) {
    ensure_keys();
    Item self = js_get_this();
    Item buf = js_property_get(self, key_buffer);
    return (Item){.item = i2it(js_stream_readable_cached_length(self, buf))};
}

extern "C" Item js_stream_get_readableFlowing(void) {
    ensure_keys();
    Item self = js_get_this();
    if (js_item_is_true(js_property_get(self, key_flowing))) return js_bool_item(true);
    if (js_item_is_true(js_property_get(self, key_paused)) ||
        js_state_get_bool(js_property_get(self, key_readable_state), "readableListening")) {
        return js_bool_item(false);
    }
    return ItemNull;
}

extern "C" Item js_stream_get_readableDidRead(void) {
    ensure_keys();
    Item self = js_get_this();
    return js_bool_item(js_state_get_bool(js_property_get(self, key_readable_state), "didRead"));
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

extern "C" Item js_stream_isDisturbed(Item stream) {
    ensure_keys();
    if (!js_stream_is_stream_like(stream)) return js_bool_item(false);
    if (js_state_get_bool(js_property_get(stream, key_readable_state), "didRead"))
        return js_bool_item(true);
    return js_bool_item(js_item_is_true(js_property_get(stream, make_string_item("__web_disturbed__"))));
}

extern "C" Item js_stream_isReadable(Item stream) {
    ensure_keys();
    if (!js_stream_is_stream_like(stream) || !js_stream_has_readable_side(stream)) return ItemNull;
    if (js_item_is_true(js_property_get(stream, key_destroyed))) return js_bool_item(false);
    if (js_item_is_true(js_property_get(stream, key_end_emitted)) ||
        js_state_get_bool(js_property_get(stream, key_readable_state), "endEmitted")) {
        return js_bool_item(false);
    }
    return js_bool_item(js_item_is_true(js_property_get(stream, key_readable)));
}

extern "C" Item js_stream_isWritable(Item stream) {
    ensure_keys();
    if (!js_stream_is_stream_like(stream) || !js_stream_has_writable_side(stream)) return ItemNull;
    if (js_item_is_true(js_property_get(stream, key_destroyed))) return js_bool_item(false);
    if (js_item_is_true(js_property_get(stream, key_finished)) ||
        js_state_get_bool(js_property_get(stream, key_writable_state), "finished")) {
        return js_bool_item(false);
    }
    return js_bool_item(js_item_is_true(js_property_get(stream, key_writable)));
}

extern "C" Item js_stream_isErrored(Item stream) {
    ensure_keys();
    if (!js_stream_is_stream_like(stream)) return ItemNull;
    Item err = js_property_get(stream, make_string_item("errored"));
    if (js_stream_has_callback_error(err)) return js_bool_item(true);
    err = js_property_get(js_property_get(stream, key_readable_state), make_string_item("errored"));
    if (js_stream_has_callback_error(err)) return js_bool_item(true);
    err = js_property_get(js_property_get(stream, key_writable_state), make_string_item("errored"));
    return js_bool_item(js_stream_has_callback_error(err));
}

extern "C" Item js_stream_isDestroyed(Item stream) {
    ensure_keys();
    if (!js_stream_is_stream_like(stream)) return ItemNull;
    return js_bool_item(js_item_is_true(js_property_get(stream, key_destroyed)) ||
                        js_item_is_true(js_property_get(stream, make_string_item("destroyed"))));
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

    js_stream_install_accessor(readable_ctor, "readableLength", (void*)js_stream_get_readableLength);
    js_stream_install_accessor(duplex_ctor, "readableLength", (void*)js_stream_get_readableLength);
    js_stream_install_accessor(transform_ctor, "readableLength", (void*)js_stream_get_readableLength);

    js_stream_install_accessor(readable_ctor, "readableFlowing", (void*)js_stream_get_readableFlowing);
    js_stream_install_accessor(duplex_ctor, "readableFlowing", (void*)js_stream_get_readableFlowing);
    js_stream_install_accessor(transform_ctor, "readableFlowing", (void*)js_stream_get_readableFlowing);

    js_stream_install_accessor(readable_ctor, "readableDidRead", (void*)js_stream_get_readableDidRead);
    js_stream_install_accessor(duplex_ctor, "readableDidRead", (void*)js_stream_get_readableDidRead);
    js_stream_install_accessor(transform_ctor, "readableDidRead", (void*)js_stream_get_readableDidRead);

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

    Item signal = env[3];
    Item abort_listener = env[4];
    Item remove_event = js_property_get(signal, make_string_item("removeEventListener"));
    if (get_type_id(remove_event) == LMD_TYPE_FUNC &&
        get_type_id(abort_listener) == LMD_TYPE_FUNC) {
        Item remove_args[2] = { make_string_item("abort"), abort_listener };
        js_call_function(remove_event, signal, remove_args, 2);
    }

    Item resolve = env[0];
    Item reject = env[1];
    Item callback = js_stream_has_error(err) ? reject : resolve;
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        Item value = js_stream_has_error(err) ? err : make_js_undefined();
        js_call_function(callback, make_js_undefined(), &value, 1);
    }
    return make_js_undefined();
}

static Item js_stream_promises_pipeline_on_abort(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[2])) return make_js_undefined();
    env[2] = js_bool_item(true);

    Item signal = env[3];
    Item abort_listener = env[4];
    Item remove_event = js_property_get(signal, make_string_item("removeEventListener"));
    if (get_type_id(remove_event) == LMD_TYPE_FUNC &&
        get_type_id(abort_listener) == LMD_TYPE_FUNC) {
        Item remove_args[2] = { make_string_item("abort"), abort_listener };
        js_call_function(remove_event, signal, remove_args, 2);
    }

    Item err = js_stream_finished_abort_error(signal);
    Item reject = env[1];
    if (get_type_id(reject) == LMD_TYPE_FUNC) {
        js_call_function(reject, make_js_undefined(), &err, 1);
    }
    return make_js_undefined();
}

static bool js_stream_pipeline_promises_parse_options(Item options, Item* signal) {
    *signal = make_js_undefined();
    if (get_type_id(options) != LMD_TYPE_MAP && get_type_id(options) != LMD_TYPE_ELEMENT) {
        return false;
    }
    if (js_stream_is_stream_like(options)) return false;

    Item maybe_signal = js_property_get(options, make_string_item("signal"));
    if (maybe_signal.item != 0 && get_type_id(maybe_signal) != LMD_TYPE_UNDEFINED &&
        get_type_id(maybe_signal) != LMD_TYPE_NULL) {
        if (!js_stream_is_abort_signal(maybe_signal)) {
            js_throw_invalid_arg_type("options.signal", "AbortSignal", maybe_signal);
            return true;
        }
        *signal = maybe_signal;
    }
    return true;
}

static Item js_stream_promises_pipeline(Item rest_args) {
    int64_t argc = js_array_length(rest_args);
    if (argc < 2) {
        return js_promise_reject(js_stream_make_type_error_with_code("ERR_MISSING_ARGS",
            "The \"streams\" argument is required"));
    }

    Item signal = make_js_undefined();
    int64_t stream_count = argc;
    if (argc >= 3) {
        Item last = js_array_get_int(rest_args, argc - 1);
        Item parsed_signal = make_js_undefined();
        if (js_stream_pipeline_promises_parse_options(last, &parsed_signal)) {
            if (js_check_exception()) {
                Item err = js_clear_exception();
                return js_promise_reject(err);
            }
            signal = parsed_signal;
            stream_count--;
        }
    }
    if (stream_count < 2) {
        return js_promise_reject(js_stream_make_type_error_with_code("ERR_MISSING_ARGS",
            "The \"streams\" argument is required"));
    }

    Item capability = js_promise_with_resolvers();
    Item promise = js_property_get(capability, make_string_item("promise"));
    Item* env = js_alloc_env(5);
    env[0] = js_property_get(capability, make_string_item("resolve"));
    env[1] = js_property_get(capability, make_string_item("reject"));
    env[2] = js_bool_item(false);
    env[3] = signal;
    env[4] = make_js_undefined();
    Item callback = js_new_closure((void*)js_stream_promises_callback, 1, env, 5);

    if (js_stream_is_abort_signal(signal)) {
        Item aborted = js_property_get(signal, make_string_item("aborted"));
        if (get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted)) {
            Item err = js_stream_finished_abort_error(signal);
            Item reject = env[1];
            if (get_type_id(reject) == LMD_TYPE_FUNC) {
                js_call_function(reject, make_js_undefined(), &err, 1);
            }
            return promise;
        }

        Item add_event = js_property_get(signal, make_string_item("addEventListener"));
        if (get_type_id(add_event) == LMD_TYPE_FUNC) {
            Item abort_listener = js_new_closure((void*)js_stream_promises_pipeline_on_abort, 0, env, 5);
            env[4] = abort_listener;
            Item add_args[2] = { make_string_item("abort"), abort_listener };
            js_call_function(add_event, signal, add_args, 2);
        }
    }

    Item pipeline_args = js_array_new(0);
    for (int64_t i = 0; i < stream_count; i++) {
        js_array_push(pipeline_args, js_array_get_int(rest_args, i));
    }
    js_array_push(pipeline_args, callback);
    js_stream_pipeline_rest(pipeline_args);
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

    js_stream_finished_impl(stream, options, callback);
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
    Item web = js_writable_stream_new(make_js_undefined());
    Item ctor = js_get_global_property(make_string_item("WritableStream"));
    Item proto = js_property_get(ctor, make_string_item("prototype"));
    if (get_type_id(proto) == LMD_TYPE_MAP || get_type_id(proto) == LMD_TYPE_ELEMENT) {
        js_set_prototype(web, proto);
    }
    js_property_set(web, make_string_item("__node_stream__"), writable);
    return web;
}

static Item js_readable_to_web_result(Item value, bool done) {
    Item result = js_new_object();
    js_property_set(result, make_string_item("value"), done ? make_js_undefined() : value);
    js_property_set(result, make_string_item("done"), js_bool_item(done));
    return result;
}

static bool js_readable_to_web_is_ended(Item readable) {
    Item state = js_property_get(readable, key_readable_state);
    return js_item_is_true(js_property_get(readable, key_end_pending)) ||
           js_item_is_true(js_property_get(readable, key_end_emitted)) ||
           js_state_get_bool(state, "ended") ||
           js_state_get_bool(state, "endEmitted");
}

static Item js_readable_to_web_copy_to_byob_view(Item chunk, Item view) {
    if (!js_is_typed_array(chunk) || !js_is_typed_array(view)) return chunk;
    if (js_typed_array_is_out_of_bounds_item(chunk) ||
        js_typed_array_is_out_of_bounds_item(view)) {
        return chunk;
    }

    int chunk_len = js_typed_array_byte_length(chunk);
    int view_len = js_typed_array_byte_length(view);
    if (chunk_len <= 0 || view_len <= 0) return view;

    int copy_len = chunk_len < view_len ? chunk_len : view_len;
    void* src = js_typed_array_current_data_ptr(chunk);
    void* dst = js_typed_array_current_data_ptr(view);
    if (src && dst) {
        memcpy(dst, src, (size_t)copy_len);
    }
    return view;
}

static Item js_readable_to_web_read_now(Item reader, Item view) {
    Item readable = js_property_get(reader, make_string_item("__node_readable__"));
    if (get_type_id(readable) != LMD_TYPE_MAP && get_type_id(readable) != LMD_TYPE_ELEMENT) {
        return js_readable_to_web_result(make_js_undefined(), true);
    }

    bool is_byob = js_item_is_true(js_property_get(reader, make_string_item("__byob__"))) &&
                   js_is_typed_array(view);
    Item size = make_js_undefined();
    if (is_byob) {
        int view_len = js_typed_array_byte_length(view);
        if (view_len > 0) size = (Item){.item = i2it(view_len)};
    }

    Item chunk = is_byob ? js_readable_read_size(readable, size) : js_readable_read(readable);
    TypeId chunk_type = get_type_id(chunk);
    if (chunk.item != 0 && chunk_type != LMD_TYPE_NULL &&
        chunk_type != LMD_TYPE_UNDEFINED) {
        if (is_byob) chunk = js_readable_to_web_copy_to_byob_view(chunk, view);
        return js_readable_to_web_result(chunk, false);
    }
    if (js_readable_to_web_is_ended(readable)) {
        return js_readable_to_web_result(make_js_undefined(), true);
    }

    js_stream_call_read_if_needed(readable, size);
    chunk = is_byob ? js_readable_read_size(readable, size) : js_readable_read(readable);
    chunk_type = get_type_id(chunk);
    if (chunk.item != 0 && chunk_type != LMD_TYPE_NULL &&
        chunk_type != LMD_TYPE_UNDEFINED) {
        if (is_byob) chunk = js_readable_to_web_copy_to_byob_view(chunk, view);
        return js_readable_to_web_result(chunk, false);
    }
    if (js_readable_to_web_is_ended(readable)) {
        return js_readable_to_web_result(make_js_undefined(), true);
    }
    return ItemNull;
}

static Item js_readable_to_web_on_readable(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[3])) return make_js_undefined();
    Item result = js_readable_to_web_read_now(env[0], env[4]);
    if (result.item == 0 || get_type_id(result) == LMD_TYPE_NULL) {
        return make_js_undefined();
    }
    env[3] = js_bool_item(true);
    Item resolve = env[1];
    if (get_type_id(resolve) == LMD_TYPE_FUNC) {
        js_call_function(resolve, make_js_undefined(), &result, 1);
    }
    return make_js_undefined();
}

static Item js_readable_to_web_on_end(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[3])) return make_js_undefined();
    env[3] = js_bool_item(true);
    Item result = js_readable_to_web_result(make_js_undefined(), true);
    Item resolve = env[1];
    if (get_type_id(resolve) == LMD_TYPE_FUNC) {
        js_call_function(resolve, make_js_undefined(), &result, 1);
    }
    return make_js_undefined();
}

static Item js_readable_to_web_on_error(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[3])) return make_js_undefined();
    env[3] = js_bool_item(true);
    Item reject = env[2];
    if (get_type_id(reject) == LMD_TYPE_FUNC) {
        js_call_function(reject, make_js_undefined(), &err, 1);
    }
    return make_js_undefined();
}

static Item js_readable_to_web_reader_read(Item view) {
    ensure_keys();
    Item reader = js_get_this();
    Item immediate = js_readable_to_web_read_now(reader, view);
    if (immediate.item != 0 && get_type_id(immediate) != LMD_TYPE_NULL) {
        return js_promise_resolve(immediate);
    }

    Item capability = js_promise_with_resolvers();
    Item* env = js_alloc_env(5);
    env[0] = reader;
    env[1] = js_property_get(capability, make_string_item("resolve"));
    env[2] = js_property_get(capability, make_string_item("reject"));
    env[3] = js_bool_item(false);
    env[4] = view;

    Item readable = js_property_get(reader, make_string_item("__node_readable__"));
    Item readable_listener = js_new_closure((void*)js_readable_to_web_on_readable, 0, env, 5);
    Item end_listener = js_new_closure((void*)js_readable_to_web_on_end, 0, env, 5);
    Item error_listener = js_new_closure((void*)js_readable_to_web_on_error, 1, env, 5);
    js_stream_once(readable, make_string_item("readable"), readable_listener);
    js_stream_once(readable, make_string_item("end"), end_listener);
    js_stream_once(readable, make_string_item("close"), end_listener);
    js_stream_once(readable, make_string_item("error"), error_listener);
    js_stream_call_read_if_needed(readable, make_js_undefined());
    return js_property_get(capability, make_string_item("promise"));
}

static Item js_readable_to_web_reader_cancel(Item reason) {
    (void)reason;
    Item reader = js_get_this();
    Item readable = js_property_get(reader, make_string_item("__node_readable__"));
    if (get_type_id(readable) == LMD_TYPE_MAP || get_type_id(readable) == LMD_TYPE_ELEMENT) {
        js_stream_destroy(readable, make_js_undefined());
    }
    return js_promise_resolve(make_js_undefined());
}

static Item js_readable_to_web_get_reader(Item options) {
    Item web = js_get_this();
    Item reader = js_new_object();
    js_property_set(reader, make_string_item("__node_readable__"),
                    js_property_get(web, make_string_item("__node_readable__")));
    js_property_set(reader, make_string_item("__byob__"),
                    js_bool_item(get_type_id(js_property_get(options, make_string_item("mode"))) == LMD_TYPE_STRING));
    js_property_set(reader, make_string_item("read"),
                    js_new_function((void*)js_readable_to_web_reader_read, 1));
    js_property_set(reader, make_string_item("cancel"),
                    js_new_function((void*)js_readable_to_web_reader_cancel, 1));
    return reader;
}

static Item js_readable_toWeb(Item readable, Item options) {
    Item type = js_property_get(options, make_string_item("type"));
    if (get_type_id(type) == LMD_TYPE_STRING && !js_stream_string_equals(type, "bytes")) {
        return js_throw_type_error_code("ERR_INVALID_ARG_VALUE",
                                        "The property 'options.type' is invalid");
    }
    Item web = js_new_object();
    Item ctor = js_get_global_property(make_string_item("ReadableStream"));
    Item proto = js_property_get(ctor, make_string_item("prototype"));
    if (get_type_id(proto) == LMD_TYPE_MAP || get_type_id(proto) == LMD_TYPE_ELEMENT) {
        js_set_prototype(web, proto);
    }
    js_class_stamp(web, JS_CLASS_READABLE_STREAM);
    js_property_set(web, make_string_item("__node_readable__"), readable);
    js_property_set(web, make_string_item("getReader"),
                    js_new_function((void*)js_readable_to_web_get_reader, 1));
    return web;
}

static Item js_stream_destroy_export(Item stream, Item err) {
    Item reason = err;
    if (reason.item == 0 || get_type_id(reason) == LMD_TYPE_UNDEFINED ||
        get_type_id(reason) == LMD_TYPE_NULL) {
        reason = js_stream_is_finished_for_destroy_export(stream) ?
                 make_js_undefined() : js_stream_iter_make_abort_error();
    }
    Item destroy_fn = js_property_get(stream, key_destroy);
    if (get_type_id(destroy_fn) == LMD_TYPE_FUNC) {
        js_call_function(destroy_fn, stream, &reason, 1);
        return stream;
    }
    return js_stream_destroy(stream, reason);
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
    Item pipeline_fn = stream_set_method(stream_namespace, "pipeline", (void*)js_stream_pipeline_rest, -1);
    Item finished_fn = stream_set_method(stream_namespace, "finished", (void*)js_stream_finished_rest, -1);
    stream_set_method(stream_namespace, "compose", (void*)js_stream_compose_rest, -1);
    stream_set_method(stream_namespace, "duplexPair", (void*)js_stream_duplex_pair, 0);
    stream_set_method(stream_namespace, "destroy", (void*)js_stream_destroy_export, 2);
    stream_set_method(stream_namespace, "addAbortSignal", (void*)js_stream_addAbortSignal, 2);
    stream_set_method(stream_namespace, "getDefaultHighWaterMark",
                      (void*)js_stream_getDefaultHighWaterMark, 1);
    stream_set_method(stream_namespace, "setDefaultHighWaterMark",
                      (void*)js_stream_setDefaultHighWaterMark, 2);
    stream_set_method(stream_namespace, "isReadable", (void*)js_stream_isReadable, 1);
    stream_set_method(stream_namespace, "isWritable", (void*)js_stream_isWritable, 1);
    stream_set_method(stream_namespace, "isDisturbed", (void*)js_stream_isDisturbed, 1);
    stream_set_method(stream_namespace, "isErrored", (void*)js_stream_isErrored, 1);
    stream_set_method(stream_namespace, "isDestroyed", (void*)js_stream_isDestroyed, 1);
    stream_set_method(stream_namespace, "arrayBuffer", (void*)js_stream_consumer_arrayBuffer, 1);
    stream_set_method(stream_namespace, "blob", (void*)js_stream_consumer_blob, 1);
    stream_set_method(stream_namespace, "buffer", (void*)js_stream_consumer_buffer, 1);
    stream_set_method(stream_namespace, "bytes", (void*)js_stream_consumer_bytes, 1);
    stream_set_method(stream_namespace, "json", (void*)js_stream_consumer_json, 1);
    stream_set_method(stream_namespace, "text", (void*)js_stream_consumer_text, 1);

    Item promises_ns = js_get_stream_promises_namespace();
    js_property_set(stream_namespace, make_string_item("promises"), promises_ns);
    Item custom_key = js_stream_promisify_custom_symbol();
    js_property_set(pipeline_fn, custom_key, js_property_get(promises_ns, make_string_item("pipeline")));
    js_property_set(finished_fn, custom_key, js_property_get(promises_ns, make_string_item("finished")));
    js_mark_non_enumerable(pipeline_fn, custom_key);
    js_mark_non_enumerable(finished_fn, custom_key);

    // Stream — base class that inherits from EventEmitter and provides pipe().
    extern Item js_get_events_namespace(void);
    Item events_ctor = js_get_events_namespace();
    Item stream_base = js_new_function((void*)js_stream_base_constructor, 0);
    Item stream_base_proto = js_new_object();
    Item events_proto = js_property_get(events_ctor, make_string_item("prototype"));
    if (js_stream_is_object_like(events_proto)) {
        js_set_prototype(stream_base_proto, events_proto);
    }
    if (js_stream_is_object_like(events_ctor)) {
        js_set_prototype(stream_base, events_ctor);
    }
    js_property_set(stream_base_proto, key_pipe, js_new_function((void*)js_readable_inst_pipe, 1));
    js_property_set(stream_base_proto, make_string_item("unpipe"),
                    js_new_function((void*)js_readable_inst_unpipe, 1));
    // legacy Stream instances have no readable state, so for-await must enter
    // the event-listener iterator path from the base prototype.
    js_stream_install_async_iterator(stream_base_proto);
    js_property_set(stream_base_proto, make_string_item("constructor"), stream_base);
    js_mark_non_enumerable(stream_base_proto, make_string_item("constructor"));
    js_set_function_name(stream_base, make_string_item("Stream"));
    js_property_set(stream_base, make_string_item("prototype"), stream_base_proto);
    js_property_set(stream_base, make_string_item("__instance_proto__"), stream_base_proto);
    js_function_set_prototype(stream_base, stream_base_proto);
    js_property_set(stream_namespace, make_string_item("Stream"), stream_base);

    if (get_type_id(readable_constructor) == LMD_TYPE_FUNC) {
        js_property_set(readable_constructor, make_string_item("from"),
                        js_new_function((void*)js_readable_from, 1));
        js_property_set(readable_constructor, make_string_item("toWeb"),
                        js_new_function((void*)js_readable_toWeb, 2));
    }
    if (get_type_id(writable_constructor) == LMD_TYPE_FUNC) {
        js_property_set(writable_constructor, make_string_item("toWeb"),
                        js_new_function((void*)js_writable_toWeb, 1));
    }
    if (get_type_id(duplex_constructor) == LMD_TYPE_FUNC) {
        js_property_set(duplex_constructor, make_string_item("from"),
                        js_new_function((void*)js_duplex_from, 1));
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
        js_stream_install_readable_helpers(stream_readable_prototype);

        js_stream_mark_constructor_prototype(readable_constructor, stream_readable_prototype, JS_CLASS_READABLE);
        js_stream_mark_constructor_prototype(writable_constructor, stream_writable_prototype, JS_CLASS_WRITABLE);
        js_stream_mark_constructor_prototype(duplex_constructor, stream_duplex_prototype, JS_CLASS_DUPLEX);
        js_stream_mark_constructor_prototype(transform_constructor, stream_transform_prototype, JS_CLASS_TRANSFORM);
        js_stream_mark_constructor_prototype(passthrough_constructor, stream_passthrough_prototype,
                                             JS_CLASS_PASS_THROUGH);
        js_property_set(stream_readable_prototype, make_string_item("destroyed"), js_bool_item(false));
        js_property_set(stream_writable_prototype, make_string_item("destroyed"), js_bool_item(false));
        js_property_set(stream_duplex_prototype, make_string_item("destroyed"), js_bool_item(false));
        js_property_set(stream_transform_prototype, make_string_item("destroyed"), js_bool_item(false));
        js_property_set(stream_passthrough_prototype, make_string_item("destroyed"), js_bool_item(false));

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
    stream_set_method(stream_iter_namespace, "from", (void*)js_stream_iter_from, 1);
    stream_set_method(stream_iter_namespace, "fromSync", (void*)js_stream_iter_fromSync, 1);
    stream_set_method(stream_iter_namespace, "pipeTo", (void*)js_stream_iter_pipeTo, 4);
    stream_set_method(stream_iter_namespace, "pull", (void*)js_stream_iter_pull, 2);
    stream_set_method(stream_iter_namespace, "pullSync", (void*)js_stream_iter_pullSync, 8);
    stream_set_method(stream_iter_namespace, "push", (void*)js_stream_iter_push, 1);
    stream_set_method(stream_iter_namespace, "ondrain", (void*)js_stream_iter_ondrain, 1);
    stream_set_method(stream_iter_namespace, "text", (void*)js_stream_iter_text_consume, 2);
    stream_set_method(stream_iter_namespace, "textSync", (void*)js_stream_iter_textSync, 2);
    stream_set_method(stream_iter_namespace, "bytes", (void*)js_stream_iter_bytes, 2);
    stream_set_method(stream_iter_namespace, "bytesSync", (void*)js_stream_iter_bytesSync, 2);
    stream_set_method(stream_iter_namespace, "arrayBuffer", (void*)js_stream_iter_arrayBuffer, 2);
    stream_set_method(stream_iter_namespace, "arrayBufferSync", (void*)js_stream_iter_arrayBufferSync, 2);
    stream_set_method(stream_iter_namespace, "array", (void*)js_stream_iter_array, 2);
    stream_set_method(stream_iter_namespace, "arraySync", (void*)js_stream_iter_arraySync, 2);
    stream_set_method(stream_iter_namespace, "tap", (void*)js_stream_iter_tap, 1);
    stream_set_method(stream_iter_namespace, "tapSync", (void*)js_stream_iter_tapSync, 1);
    js_property_set(stream_iter_namespace, make_string_item("default"), stream_iter_namespace);
    return stream_iter_namespace;
}

extern "C" Item js_get_stream_web_namespace(void) {
    if (stream_web_namespace.item != 0) return stream_web_namespace;
    ensure_keys();
    stream_web_namespace = js_new_object();

    Item readable_ctor = js_new_function((void*)js_readable_stream_new, 1);
    Item writable_ctor = js_new_function((void*)js_writable_stream_new, 1);
    js_set_function_name(readable_ctor, make_string_item("ReadableStream"));
    js_set_function_name(writable_ctor, make_string_item("WritableStream"));
    Item transform_ctor = js_get_global_property(make_string_item("TransformStream"));
    if (get_type_id(transform_ctor) != LMD_TYPE_FUNC) {
        transform_ctor = js_new_function((void*)js_transform_stream_new, 1);
    }

    js_property_set(stream_web_namespace, make_string_item("ReadableStream"), readable_ctor);
    js_property_set(stream_web_namespace, make_string_item("WritableStream"), writable_ctor);
    js_property_set(stream_web_namespace, make_string_item("TransformStream"), transform_ctor);
    js_property_set(stream_web_namespace, make_string_item("default"), stream_web_namespace);
    return stream_web_namespace;
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

extern "C" Item js_get_internal_stream_end_of_stream_namespace(void) {
    if (internal_stream_end_of_stream_namespace.item != 0)
        return internal_stream_end_of_stream_namespace;
    internal_stream_end_of_stream_namespace = js_new_object();
    Item eos_fn = js_new_function((void*)js_stream_finished_rest, -1);
    Item finished_fn = js_new_function((void*)js_stream_promises_finished, -1);
    js_property_set(internal_stream_end_of_stream_namespace, make_string_item("eos"), eos_fn);
    // Node's internal EOS module exports callback eos() and Promise finished();
    // sharing eos here makes stream/promises.finished wait forever for a callback.
    js_property_set(internal_stream_end_of_stream_namespace, make_string_item("finished"), finished_fn);
    // Node's internal EOS module exports this unique symbol; stream.finished
    // checks its description so native options can request synchronous callback.
    js_property_set(internal_stream_end_of_stream_namespace,
                    make_string_item("kEosNodeSynchronousCallback"),
                    js_symbol_create(make_string_item("kEosNodeSynchronousCallback")));
    js_property_set(internal_stream_end_of_stream_namespace, make_string_item("default"),
                    internal_stream_end_of_stream_namespace);
    return internal_stream_end_of_stream_namespace;
}

extern "C" void js_stream_reset(void) {
    stream_namespace = (Item){0};
    stream_web_namespace = (Item){0};
    keys_init = false;
    stream_readable_prototype = (Item){0};
    stream_writable_prototype = (Item){0};
    stream_duplex_prototype = (Item){0};
    stream_transform_prototype = (Item){0};
    stream_passthrough_prototype = (Item){0};
    internal_stream_state_namespace = (Item){0};
    internal_stream_end_of_stream_namespace = (Item){0};
    stream_iter_namespace = (Item){0};
    js_stream_default_byte_hwm = 16 * 1024;
    js_stream_default_object_hwm = 16;
}
