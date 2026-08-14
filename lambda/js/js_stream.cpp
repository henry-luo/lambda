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
#include "js_runtime_state.hpp"
#include "js_class.h"
#include "js_property_attrs.h"
#include "js_function.hpp"
#include "js_typed_array.h"
#include "../jube/jube_registry.h"
#include "../lambda-data.hpp"
#include "../runtime/transpiler.hpp"

extern "C" void js_function_set_prototype(Item fn_item, Item proto);
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/strbuf.h"

#include <cstdio>
#include <cstring>
#include <math.h>

#define JS_STREAM_FUNC_FLAG_GENERATOR 1
#define JS_STREAM_FUNC_FLAG_ASYNC_GEN 64
#define JS_STREAM_FUNC_FLAG_ASYNC 128

// forward declarations
extern "C" Item js_process_emit(Item event_name, Item arg1);
extern "C" Item js_ordinary_has_instance(Item left, Item right);
extern "C" Item js_buffer_from(Item data, Item encoding, Item length_item);
extern "C" Item js_buffer_isBuffer(Item obj);
extern "C" Item js_buffer_concat(Item list, Item total_length_item);
extern "C" Item js_buffer_toString(Item buf, Item encoding, Item start_item, Item end_item);
extern "C" Item js_buffer_slice(Item buf, Item start_item, Item end_item);
extern "C" Item js_blob_new(Item parts, Item options);
extern "C" Item js_util_inspect(Item obj_item, Item options_item);
extern "C" Item js_async_iterator_step_result(Item iterator);
extern "C" Item js_iterator_result_done(Item result);
extern "C" Item js_iterator_result_value(Item result);
extern "C" bool js_is_async_generator(Item obj);
extern "C" Item js_als_capture_context(void);
extern "C" Item js_als_context_call(Item context, Item callback, Item this_val, Item arg1, int64_t has_arg);
extern "C" Item js_async_hooks_create_resource(const char* type_chars, int type_len);
extern "C" Item js_async_hooks_enter_resource(Item resource);
extern "C" void js_async_hooks_restore_resource(Item previous);

// cached key items
#define key_on (js_runtime_state.stream.key_on)
#define key_emit (js_runtime_state.stream.key_emit)
#define key_push (js_runtime_state.stream.key_push)
#define key_write (js_runtime_state.stream.key_write)
#define key_end (js_runtime_state.stream.key_end)
#define key_pipe (js_runtime_state.stream.key_pipe)
#define key_read (js_runtime_state.stream.key_read)
#define key_destroy (js_runtime_state.stream.key_destroy)
#define key_readable (js_runtime_state.stream.key_readable)
#define key_writable (js_runtime_state.stream.key_writable)
#define key_flowing (js_runtime_state.stream.key_flowing)
#define key_ended (js_runtime_state.stream.key_ended)
#define key_finished (js_runtime_state.stream.key_finished)
#define key_destroyed (js_runtime_state.stream.key_destroyed)
#define key_listeners (js_runtime_state.stream.key_listeners)
#define key_buffer (js_runtime_state.stream.key_buffer)
#define key_readable_state (js_runtime_state.stream.key_readable_state)
#define key_writable_state (js_runtime_state.stream.key_writable_state)
#define key_end_pending (js_runtime_state.stream.key_end_pending)
#define key_end_emitted (js_runtime_state.stream.key_end_emitted)
#define key_reading (js_runtime_state.stream.key_reading)
#define key_reading_sync (js_runtime_state.stream.key_reading_sync)
#define key_paused (js_runtime_state.stream.key_paused)
#define key_finish_emitted (js_runtime_state.stream.key_finish_emitted)
#define key_close_emitted (js_runtime_state.stream.key_close_emitted)
#define key_closed (js_runtime_state.stream.key_closed)
#define key_capture_rejections (js_runtime_state.stream.key_capture_rejections)
#define key_auto_destroy (js_runtime_state.stream.key_auto_destroy)
#define key_readable_side_enabled (js_runtime_state.stream.key_readable_side_enabled)
#define key_writable_side_enabled (js_runtime_state.stream.key_writable_side_enabled)
#define key_destroy_pending (js_runtime_state.stream.key_destroy_pending)
#define key_listener_fn (js_runtime_state.stream.key_listener_fn)
#define key_listener_context (js_runtime_state.stream.key_listener_context)
#define keys_init (js_runtime_state.stream.keys_initialized)
#define stream_readable_prototype (js_runtime_state.stream.readable_prototype)
#define stream_writable_prototype (js_runtime_state.stream.writable_prototype)
#define stream_duplex_prototype (js_runtime_state.stream.duplex_prototype)
#define stream_transform_prototype (js_runtime_state.stream.transform_prototype)
#define stream_passthrough_prototype (js_runtime_state.stream.passthrough_prototype)
#define internal_stream_state_namespace (js_runtime_state.stream.internal_state_namespace)
#define internal_stream_end_of_stream_namespace (js_runtime_state.stream.internal_end_of_stream_namespace)
#define stream_iter_namespace (js_runtime_state.stream.iterator_namespace)
#define stream_web_namespace (js_runtime_state.stream.web_namespace)
#define js_stream_default_byte_hwm (js_runtime_state.stream.default_byte_hwm)
#define js_stream_default_object_hwm (js_runtime_state.stream.default_object_hwm)

template <typename Target>
JS_FORWARD_STATIC_VOID( js_stream_set_default_method, (Item object, const char* name, Target target), js_set_native_method, (object, name, target))

static bool stream_ensure_roots(void) {
    return js_active_runtime_state &&
        js_root_range_ensure_registered(&js_runtime_state.stream.roots);
}

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
static Item js_stream_prepare_readable_chunk(Item self, Item* chunk, Item encoding);
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
    if (!stream_ensure_roots()) return;
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
template <typename Target>
static void js_stream_set_method(Item object, Item key, Target target,
                                 int adapter_arity);
static Item js_stream_pipe_data_noop(Item chunk);
static bool js_stream_has_event_listeners(Item self, const char* event);
static void js_stream_schedule_data_flush(Item self);
static void js_stream_schedule_read(Item self);
static bool js_item_is_true(Item item);
static bool js_state_get_bool(Item state, const char* name);

static void js_readable_clear_pipe(Item self) {
    js_set_key_default(self, make_string_item("__piped__"), js_bool_item(false));
    js_set_key_default(self, make_string_item("__pipe_dest__"), make_js_undefined());
    Item state = js_get_key_default(self, key_readable_state);
    if (get_type_id(state) == LMD_TYPE_MAP)
        js_set_key_default(state, make_string_item("awaitDrainWriters"), ItemNull);
}

static Item js_stream_await_drain_writers(Item state) {
    if (get_type_id(state) != LMD_TYPE_MAP) return ItemNull;
    return js_get_key_default(state, make_string_item("awaitDrainWriters"));
}

static bool js_stream_await_drain_set_contains(Item set_like, Item dest) {
    Item writers = js_get_key_default(set_like, make_string_item("__writers__"));
    if (get_type_id(writers) != LMD_TYPE_ARRAY) return false;
    int64_t len = js_array_length(writers);
    for (int64_t i = 0; i < len; i++) {
        if (js_elements_get_int(writers, i).item == dest.item) return true;
    }
    return false;
}

static Item js_stream_make_empty_await_drain_set(void) {
    Item set_like = js_new_object();
    js_set_key_default(set_like, make_string_item("__writers__"), js_array_new(0));
    js_set_key_default(set_like, make_string_item("size"), (Item){.item = i2it(0)});
    return set_like;
}

static Item js_stream_make_await_drain_set(Item first, Item second) {
    Item set_like = js_new_object();
    Item writers = js_array_new(0);
    js_array_push(writers, first);
    if (second.item != first.item) js_array_push(writers, second);
    js_set_key_default(set_like, make_string_item("__writers__"), writers);
    js_set_key_default(set_like, make_string_item("size"),
                    (Item){.item = i2it(js_array_length(writers))});
    return set_like;
}

static void js_stream_await_drain_add(Item source, Item dest) {
    Item state = js_get_key_default(source, key_readable_state);
    if (get_type_id(state) != LMD_TYPE_MAP) return;
    Item current = js_stream_await_drain_writers(state);
    if (current.item == 0 ||
        get_type_id(current) == LMD_TYPE_NULL ||
        get_type_id(current) == LMD_TYPE_UNDEFINED) {
        js_set_key_default(dest, make_string_item("size"), (Item){.item = i2it(1)});
        js_set_key_default(state, make_string_item("awaitDrainWriters"), dest);
        return;
    }
    if (current.item == dest.item) return;
    Item writers = js_get_key_default(current, make_string_item("__writers__"));
    if (get_type_id(current) == LMD_TYPE_MAP && get_type_id(writers) == LMD_TYPE_ARRAY) {
        if (!js_stream_await_drain_set_contains(current, dest)) {
            js_array_push(writers, dest);
            js_set_key_default(current, make_string_item("size"),
                            (Item){.item = i2it(js_array_length(writers))});
        }
        return;
    }
    js_set_key_default(state, make_string_item("awaitDrainWriters"),
                    js_stream_make_await_drain_set(current, dest));
}

static bool js_stream_await_drain_remove(Item source, Item dest) {
    Item state = js_get_key_default(source, key_readable_state);
    if (get_type_id(state) != LMD_TYPE_MAP) return false;
    Item current = js_stream_await_drain_writers(state);
    if (current.item == 0 ||
        get_type_id(current) == LMD_TYPE_NULL ||
        get_type_id(current) == LMD_TYPE_UNDEFINED) {
        return false;
    }
    if (current.item == dest.item) {
        js_set_key_default(dest, make_string_item("size"), make_js_undefined());
        js_set_key_default(state, make_string_item("awaitDrainWriters"), ItemNull);
        return true;
    }
    Item writers = js_get_key_default(current, make_string_item("__writers__"));
    if (get_type_id(current) != LMD_TYPE_MAP || get_type_id(writers) != LMD_TYPE_ARRAY) {
        return false;
    }
    Item next = js_array_new(0);
    bool removed = false;
    int64_t len = js_array_length(writers);
    for (int64_t i = 0; i < len; i++) {
        Item writer = js_elements_get_int(writers, i);
        if (writer.item == dest.item) {
            removed = true;
            js_set_key_default(writer, make_string_item("size"), make_js_undefined());
        } else {
            js_array_push(next, writer);
        }
    }
    if (!removed) return false;
    int64_t next_len = js_array_length(next);
    if (next_len == 0) {
        js_set_key_default(current, make_string_item("__writers__"), next);
        js_set_key_default(current, make_string_item("size"), (Item){.item = i2it(0)});
    } else {
        js_set_key_default(current, make_string_item("__writers__"), next);
        js_set_key_default(current, make_string_item("size"), (Item){.item = i2it(next_len)});
    }
    return true;
}

static bool js_stream_await_drain_pending(Item source) {
    Item state = js_get_key_default(source, key_readable_state);
    Item current = js_stream_await_drain_writers(state);
    if (current.item == 0 ||
        get_type_id(current) == LMD_TYPE_NULL ||
        get_type_id(current) == LMD_TYPE_UNDEFINED) {
        return false;
    }
    Item writers = js_get_key_default(current, make_string_item("__writers__"));
    if (get_type_id(current) == LMD_TYPE_MAP && get_type_id(writers) == LMD_TYPE_ARRAY) {
        return js_array_length(writers) > 0;
    }
    return true;
}

static Item js_readable_pipes(Item self) {
    Item state = js_get_key_default(self, key_readable_state);
    if (get_type_id(state) != LMD_TYPE_MAP) return ItemNull;
    Item pipes_key = make_string_item("pipes");
    Item pipes = js_get_key_default(state, pipes_key);
    if (get_type_id(pipes) != LMD_TYPE_ARRAY) {
        pipes = js_array_new(0);
        js_set_key_default(state, pipes_key, pipes);
    }
    return pipes;
}

static bool js_readable_has_pipe(Item self, Item dest) {
    Item pipes = js_readable_pipes(self);
    if (get_type_id(pipes) != LMD_TYPE_ARRAY) return false;
    int64_t len = js_array_length(pipes);
    for (int64_t i = 0; i < len; i++) {
        if (js_elements_get_int(pipes, i).item == dest.item) return true;
    }
    return false;
}

static Item js_legacy_stream_pipe_on_data(Item env_item, Item chunk) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item dest = env[0];
    Item write_fn = js_get_key_default(dest, key_write);
    if (!js_is_callable(write_fn)) return make_js_undefined();
    return js_call_function(write_fn, dest, &chunk, 1);
}

static Item js_legacy_stream_pipe_on_end(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item dest = env[0];
    Item end_fn = js_get_key_default(dest, key_end);
    if (!js_is_callable(end_fn)) return make_js_undefined();
    return js_call_function(end_fn, dest, NULL, 0);
}

static void js_legacy_stream_pipe_add_listener(Item source, const char* event_name, Item listener) {
    Item on_fn = js_get_key_default(source, key_on);
    Item args[2] = { make_string_item(event_name), listener };
    if (js_is_callable(on_fn)) {
        js_call_function(on_fn, source, args, 2);
    } else {
        js_stream_on(source, args[0], listener);
    }
}

static Item js_legacy_stream_pipe(Item source, Item dest) {
    Item* env = js_alloc_env(1);
    env[0] = dest;
    Item on_data = js_new_native_closure(js_legacy_stream_pipe_on_data, 1, env, 1);
    Item on_end = js_new_native_closure(js_legacy_stream_pipe_on_end, 0, env, 1);
    js_legacy_stream_pipe_add_listener(source, "data", on_data);
    js_legacy_stream_pipe_add_listener(source, "end", on_end);

    Item resume_fn = js_get_key_default(source, make_string_item("resume"));
    if (js_is_callable(resume_fn)) {
        js_call_function(resume_fn, source, NULL, 0);
    }
    return dest;
}

static void js_readable_emit_unpipe(Item dest, Item source) {
    Item unpipe_event = make_string_item("unpipe");
    Item emit_fn = js_get_key_default(dest, key_emit);
    if (js_is_callable(emit_fn)) {
        Item args[2] = {unpipe_event, source};
        js_call_function(emit_fn, dest, args, 2);
    } else {
        js_stream_emit(dest, unpipe_event, source);
    }
}

static void js_readable_remove_one_data_listener(Item self) {
    Item listeners_map = js_get_key_default(self, make_string_item("_events"));
    if (get_type_id(listeners_map) != LMD_TYPE_MAP) return;
    Item data_key = make_string_item("data");
    Item arr = js_get_key_default(listeners_map, data_key);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) return;
    int64_t len = js_array_length(arr);
    if (len <= 0) return;
    Item next = js_array_new(0);
    for (int64_t i = 1; i < len; i++) {
        js_array_push(next, js_elements_get_int(arr, i));
    }
    if (js_array_length(next) == 0) {
        js_set_key_default(listeners_map, data_key, make_js_undefined());
    } else {
        js_set_key_default(listeners_map, data_key, next);
    }
}

static void js_readable_add_pipe_data_event(Item self) {
    Item events = js_get_key_default(self, make_string_item("_events"));
    if (get_type_id(events) != LMD_TYPE_MAP) {
        events = js_new_object();
        js_set_key_default(self, make_string_item("_events"), events);
    }
    Item data_key = make_string_item("data");
    Item arr = js_get_key_default(events, data_key);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) {
        arr = js_array_new(0);
        js_set_key_default(events, data_key, arr);
    }
    js_array_push(arr, js_new_native_function(js_stream_pipe_data_noop));
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
        Item current = js_elements_get_int(pipes, i);
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
    Item state = js_get_key_default(self, key_readable_state);
    if (get_type_id(state) == LMD_TYPE_MAP) {
        js_set_key_default(state, make_string_item("pipes"), next);
    }
    if (removed && js_array_length(next) == 0) {
        if (js_stream_has_event_listeners(self, "data")) {
            js_stream_set_flowing(self, true);
            js_set_key_default(self, key_paused, js_bool_item(false));
            js_stream_schedule_data_flush(self);
        } else {
            js_stream_set_flowing(self, false);
        }
    }
    Item current_dest = js_get_key_default(self, make_string_item("__pipe_dest__"));
    if (removed && (remove_all || current_dest.item == dest.item)) {
        js_readable_clear_pipe(self);
    }
    if (emit_unpipe) {
        int64_t removed_len = js_array_length(removed_items);
        for (int64_t i = 0; i < removed_len; i++) {
            js_readable_emit_unpipe(js_elements_get_int(removed_items, i), self);
        }
    }
    return removed;
}
JS_FORWARD_STATIC_RETURN(bool, js_item_is_true, (Item item), get_type_id, (item) == LMD_TYPE_BOOL && it2b(item))
JS_FORWARD_STATIC_RETURN(bool, js_stream_item_to_int64, (Item value, int64_t* out), js_item_to_integral_int64, (value, out, false))

static Item js_readable_pipe_on_drain(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item source = env[0];
    Item dest = env[1];
    if (js_item_is_true(js_get_key_default(source, key_destroyed)) ||
        js_item_is_true(js_get_key_default(source, key_end_pending)) ||
        js_item_is_true(js_get_key_default(source, key_end_emitted))) {
        return make_js_undefined();
    }
    Item dest_readable_state = js_get_key_default(dest, key_readable_state);
    bool dest_readable_ended = get_type_id(dest_readable_state) == LMD_TYPE_MAP &&
        (js_state_get_bool(dest_readable_state, "ended") ||
         js_item_is_true(js_get_key_default(dest, key_end_pending)) ||
         js_item_is_true(js_get_key_default(dest, key_end_emitted)));
    if (dest_readable_ended) {
        return make_js_undefined();
    }
    if (!js_readable_has_pipe(source, dest)) return make_js_undefined();
    js_stream_await_drain_remove(source, dest);
    if (js_stream_await_drain_pending(source)) return make_js_undefined();
    js_stream_set_flowing(source, true);
    js_set_key_default(source, key_paused, js_bool_item(false));
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
    Item fn = js_get_key_default(value, key_listener_fn);
    return js_is_callable(fn);
}

static Item js_stream_listener_fn(Item value) {
    if (js_stream_is_listener_record(value)) return js_get_key_default(value, key_listener_fn);
    return value;
}

static Item js_stream_listener_context(Item value) {
    if (js_stream_is_listener_record(value)) return js_get_key_default(value, key_listener_context);
    return ItemNull;
}

static Item js_stream_make_listener_record(Item listener) {
    RootFrame roots(3);
    Rooted<Item> record_root(roots, js_new_object());
    Rooted<Item> listener_root(roots, listener);
    Rooted<Item> context_root(roots, js_als_capture_context());
    js_set_key_default(record_root.get(), key_listener_fn, listener_root.get());
    js_set_key_default(record_root.get(), key_listener_context, context_root.get());
    return record_root.get();
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
    js_set_key_default(state, make_string_item(name), js_bool_item(value));
}

static void js_state_set_item(Item state, const char* name, Item value) {
    if (get_type_id(state) != LMD_TYPE_MAP) return;
    js_set_key_default(state, make_string_item(name), value);
}

static void js_stream_set_flowing(Item self, bool flowing) {
    js_set_key_default(self, key_flowing, js_bool_item(flowing));
    js_state_set_bool(js_get_key_default(self, key_readable_state), "flowing", flowing);
}

static void js_stream_set_readable_buffer(Item self, Item buffer) {
    js_set_key_default(self, key_buffer, buffer);
    js_set_key_default(self, make_string_item("readableBuffer"), buffer);
    js_state_set_item(js_get_key_default(self, key_readable_state), "length",
                      (Item){.item = i2it(js_stream_readable_buffer_length(self, buffer))});
}

static int64_t js_stream_readable_cached_length(Item self, Item buf) {
    Item state = js_get_key_default(self, key_readable_state);
    if (get_type_id(state) == LMD_TYPE_MAP) {
        Item length = js_get_key_default(state, make_string_item("length"));
        int64_t length_int = 0;
        if (js_stream_item_to_int64(length, &length_int)) return length_int;
    }
    return js_stream_readable_buffer_length(self, buf);
}

static void js_stream_adjust_readable_length(Item self, int64_t delta) {
    Item state = js_get_key_default(self, key_readable_state);
    if (get_type_id(state) != LMD_TYPE_MAP) return;
    Item current_item = js_get_key_default(state, make_string_item("length"));
    int64_t current = 0;
    js_stream_item_to_int64(current_item, &current);
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
    return js_item_is_true(js_get_key_default(state, make_string_item(name)));
}

static Item js_writable_state_getBuffer(void);

static Item js_create_readable_state(void) {
    RootFrame roots(3);
    Rooted<Item> state_root(roots, js_new_object());
    Rooted<Item> pipes_root(roots, ItemNull);
    Rooted<Item> encoding_root(roots, ItemNull);
    // property allocation can collect while the state is only partially built;
    // retain its map and compound fields until construction is complete.
    js_state_set_bool(state_root.get(), "ended", false);
    js_state_set_bool(state_root.get(), "endEmitted", false);
    js_state_set_bool(state_root.get(), "errorEmitted", false);
    js_state_set_bool(state_root.get(), "objectMode", false);
    js_state_set_bool(state_root.get(), "readableListening", false);
    js_state_set_bool(state_root.get(), "needReadable", false);
    js_state_set_bool(state_root.get(), "flowing", false);
    js_state_set_bool(state_root.get(), "emittedReadable", false);
    js_state_set_bool(state_root.get(), "resumeScheduled", false);
    js_state_set_bool(state_root.get(), "reading", false);
    js_state_set_bool(state_root.get(), "readingMore", false);
    js_state_set_bool(state_root.get(), "didRead", false);
    js_state_set_item(state_root.get(), "errored", ItemNull);
    js_state_set_item(state_root.get(), "awaitDrainWriters", ItemNull);
    pipes_root.set(js_array_new(0));
    js_state_set_item(state_root.get(), "pipes", pipes_root.get());
    js_state_set_item(state_root.get(), "length", (Item){.item = i2it(0)});
    js_state_set_item(state_root.get(), "highWaterMark", (Item){.item = i2it(js_stream_default_byte_hwm)});
    encoding_root.set(make_string_item("utf8"));
    js_set_key_default(state_root.get(), make_string_item("encoding"), encoding_root.get());
    return state_root.get();
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
    js_set_key_default(state, make_string_item("__stream__"), owner);
    Item get_buffer = js_new_native_function(js_writable_state_getBuffer);
    js_set_key_default(state, make_string_item("getBuffer"), get_buffer);
    js_mark_non_enumerable(state, make_string_item("__stream__"));
    js_mark_non_enumerable(state, make_string_item("getBuffer"));
    return state;
}

static void js_stream_set_writable_corked(Item self, int64_t count) {
    if (count < 0) count = 0;
    Item value = (Item){.item = i2it(count)};
    js_set_key_default(self, make_string_item("_corked"), value);
    js_state_set_item(js_get_key_default(self, key_writable_state), "corked", value);
}

static void js_stream_set_buffered_request_count(Item self, int64_t count) {
    if (count < 0) count = 0;
    js_state_set_item(js_get_key_default(self, key_writable_state), "bufferedRequestCount",
                      (Item){.item = i2it(count)});
}

static int64_t js_stream_pending_writes_count(Item self) {
    Item pending = js_get_key_default(self, make_string_item("_pendingWrites"));
    return get_type_id(pending) == LMD_TYPE_ARRAY ? js_array_length(pending) : 0;
}

static Item js_writable_state_getBuffer(void) {
    ensure_keys();
    Item state = js_get_this();
    Item self = js_get_key_default(state, make_string_item("__stream__"));
    Item pending = js_get_key_default(self, make_string_item("_pendingWrites"));
    if (get_type_id(pending) == LMD_TYPE_ARRAY) return pending;
    return js_array_new(0);
}

static int64_t js_stream_state_get_int(Item state, const char* name, int64_t fallback) {
    if (get_type_id(state) != LMD_TYPE_MAP) return fallback;
    Item value = js_get_key_default(state, make_string_item(name));
    int64_t value_int = 0;
    if (js_stream_item_to_int64(value, &value_int)) return value_int;
    return fallback;
}

static bool js_stream_writable_state_has_pendingcb(Item state) {
    if (get_type_id(state) != LMD_TYPE_MAP) return false;
    Item value = js_get_key_default(state, make_string_item("pendingcb"));
    return value.item != 0 && get_type_id(value) != LMD_TYPE_UNDEFINED;
}

static void js_stream_set_writable_pendingcb(Item self, int64_t count) {
    if (count < 0) count = 0;
    Item state = js_get_key_default(self, key_writable_state);
    if (get_type_id(state) != LMD_TYPE_MAP) return;
    js_state_set_item(state, "pendingcb", (Item){.item = i2it(count)});
}

static void js_stream_adjust_writable_pendingcb(Item self, int64_t delta) {
    Item state = js_get_key_default(self, key_writable_state);
    if (get_type_id(state) != LMD_TYPE_MAP) return;
    int64_t current = js_stream_state_get_int(state, "pendingcb", 0);
    js_stream_set_writable_pendingcb(self, current + delta);
}

static int64_t js_stream_chunk_length(Item self, Item chunk, bool readable) {
    Item state = js_get_key_default(self, readable ? key_readable_state : key_writable_state);
    if (js_state_get_bool(state, "objectMode")) return 1;
    if (get_type_id(chunk) == LMD_TYPE_STRING) {
        String* str = it2s(chunk);
        return str ? (int64_t)str->len : 0;
    }
    if (readable) {
        Item byte_length = js_get_key_default(chunk, make_string_item("byteLength"));
        int64_t byte_length_int = 0;
        if (js_stream_item_to_int64(byte_length, &byte_length_int)) return byte_length_int;
        Item length = js_get_key_default(chunk, make_string_item("length"));
        int64_t length_int = 0;
        if (js_stream_item_to_int64(length, &length_int)) return length_int;
        return 0;
    }
    Item length = js_get_key_default(chunk, make_string_item("length"));
    int64_t length_int = 0;
    if (js_stream_item_to_int64(length, &length_int)) return length_int;
    Item byte_length = js_get_key_default(chunk, make_string_item("byteLength"));
    int64_t byte_length_int = 0;
    if (js_stream_item_to_int64(byte_length, &byte_length_int)) return byte_length_int;
    return 1;
}
JS_FORWARD_STATIC_RETURN(int64_t, js_stream_writable_chunk_length, (Item self, Item chunk), js_stream_chunk_length, (self, chunk, false))
JS_FORWARD_STATIC_RETURN(int64_t, js_stream_readable_chunk_length, (Item self, Item chunk), js_stream_chunk_length, (self, chunk, true))

static bool js_stream_begin_write(Item self, Item chunk) {
    Item state = js_get_key_default(self, key_writable_state);
    int64_t current = js_stream_state_get_int(state, "length", 0);
    int64_t chunk_len = js_stream_writable_chunk_length(self, chunk);
    int64_t next = current + chunk_len;
    int64_t hwm = js_stream_state_get_int(state, "highWaterMark", 16 * 1024);
    js_state_set_item(state, "length", (Item){.item = i2it(next)});
    bool need_drain = next > 0 && next >= hwm;
    if (need_drain) js_state_set_bool(state, "needDrain", true);
    return !need_drain;
}
JS_FORWARD_STATIC_ITEM(js_stream_side_state, (Item self, bool readable), js_get_key_default, (self, readable ? key_readable_state : key_writable_state))

static void js_stream_set_side_object_mode(Item obj, bool readable, bool value) {
    js_set_key_default(obj, make_string_item(readable ? "readableObjectMode" : "writableObjectMode"),
                       js_bool_item(value));
    js_state_set_bool(js_stream_side_state(obj, readable), "objectMode", value);
}

static void js_stream_set_side_high_water_mark(Item obj, bool readable, Item value) {
    js_set_key_default(obj, make_string_item(readable ? "readableHighWaterMark" : "writableHighWaterMark"), value);
    js_state_set_item(js_stream_side_state(obj, readable), "highWaterMark", value);
}

static void js_stream_init_readable_options(Item obj) {
    js_stream_set_side_object_mode(obj, true, false);
    js_stream_set_side_high_water_mark(obj, true, (Item){.item = i2it(js_stream_default_byte_hwm)});
}

static void js_stream_init_writable_options(Item obj) {
    js_stream_set_side_object_mode(obj, false, false);
    js_stream_set_side_high_water_mark(obj, false, (Item){.item = i2it(js_stream_default_byte_hwm)});
}
JS_FORWARD_STATIC_VOID( js_stream_set_side_open, (Item self, bool readable, bool open), js_set_key_default, (self, readable ? key_readable : key_writable, js_bool_item(open)))

static bool js_stream_side_enabled(Item self, bool readable) {
    Item enabled = js_get_key_default(self,
        readable ? key_readable_side_enabled : key_writable_side_enabled);
    if (get_type_id(enabled) == LMD_TYPE_BOOL) return it2b(enabled);
    Item side = js_get_key_default(self, readable ? key_readable : key_writable);
    if (get_type_id(side) == LMD_TYPE_BOOL) return it2b(side);
    return get_type_id(js_stream_side_state(self, readable)) == LMD_TYPE_MAP;
}

static void js_stream_set_side_enabled(Item self, bool readable, bool enabled) {
    js_set_key_default(self,
        readable ? key_readable_side_enabled : key_writable_side_enabled,
        js_bool_item(enabled));
    js_stream_set_side_open(self, readable, enabled);
}

#define js_stream_set_readable_object_mode(obj, value) js_stream_set_side_object_mode(obj, true, value)
#define js_stream_set_writable_object_mode(obj, value) js_stream_set_side_object_mode(obj, false, value)
#define js_stream_set_readable_high_water_mark(obj, value) js_stream_set_side_high_water_mark(obj, true, value)
#define js_stream_set_writable_high_water_mark(obj, value) js_stream_set_side_high_water_mark(obj, false, value)
#define js_stream_set_readable_open(self, open) js_stream_set_side_open(self, true, open)
#define js_stream_set_writable_open(self, open) js_stream_set_side_open(self, false, open)
#define js_stream_readable_side_enabled(self) js_stream_side_enabled(self, true)
#define js_stream_writable_side_enabled(self) js_stream_side_enabled(self, false)
#define js_stream_set_readable_side_enabled(self, enabled) js_stream_set_side_enabled(self, true, enabled)
#define js_stream_set_writable_side_enabled(self, enabled) js_stream_set_side_enabled(self, false, enabled)
JS_FORWARD_STATIC_RETURN(bool, js_stream_destroy_pending, (Item self), js_item_is_true, (js_get_key_default(self, key_destroy_pending)))

static void js_stream_mark_destroyed(Item self) {
    js_set_key_default(self, key_destroyed, js_bool_item(true));
    js_set_key_default(self, make_string_item("destroyed"), js_bool_item(true));
    js_stream_set_readable_open(self, false);
    js_stream_set_writable_open(self, false);
}

static void js_stream_set_error_state(Item self, Item err) {
    js_set_key_default(self, make_string_item("errored"), err);
    Item readable_state = js_get_key_default(self, key_readable_state);
    js_state_set_item(readable_state, "errored", err);
    Item writable_state = js_get_key_default(self, key_writable_state);
    js_state_set_item(writable_state, "errored", err);
}
JS_FORWARD_STATIC_EXPRESSION(bool, js_stream_error_value_present, (Item err), (err.item != 0 && get_type_id(err) != LMD_TYPE_UNDEFINED && get_type_id(err) != LMD_TYPE_NULL))
JS_FORWARD_STATIC_EXPRESSION(bool, js_stream_stored_error_value_present, (Item err), (js_stream_error_value_present(err) && get_type_id(err) != LMD_TYPE_BOOL))

static Item js_stream_get_stored_error(Item self) {
    Item err = js_get_key_default(self, make_string_item("errored"));
    if (js_stream_stored_error_value_present(err)) return err;

    Item readable_state = js_get_key_default(self, key_readable_state);
    err = js_get_key_default(readable_state, make_string_item("errored"));
    if (js_stream_stored_error_value_present(err)) return err;

    Item writable_state = js_get_key_default(self, key_writable_state);
    err = js_get_key_default(writable_state, make_string_item("errored"));
    if (js_stream_stored_error_value_present(err)) return err;

    err = js_get_key_default(self, make_string_item("__error__"));
    if (js_stream_stored_error_value_present(err)) return err;

    return make_js_undefined();
}
JS_FORWARD_STATIC_RETURN(bool, js_stream_has_stored_error, (Item self), js_stream_stored_error_value_present, (js_stream_get_stored_error(self)))

static bool js_stream_is_finished_for_destroy_export(Item self) {
    if (js_item_is_true(js_get_key_default(self, key_destroyed))) return true;

    Item readable = js_get_key_default(self, key_readable);
    if (get_type_id(readable) == LMD_TYPE_BOOL && it2b(readable) &&
        !js_item_is_true(js_get_key_default(self, key_end_emitted))) {
        return false;
    }

    Item writable = js_get_key_default(self, key_writable);
    if (get_type_id(writable) == LMD_TYPE_BOOL && it2b(writable) &&
        !js_item_is_true(js_get_key_default(self, key_finish_emitted))) {
        return false;
    }

    return true;
}
JS_FORWARD_STATIC_EXPRESSION(bool, js_stream_has_readable_side, (Item self), (get_type_id(js_get_key_default(self, key_readable_state)) == LMD_TYPE_MAP))
JS_FORWARD_STATIC_EXPRESSION(bool, js_stream_has_writable_side, (Item self), (get_type_id(js_get_key_default(self, key_writable_state)) == LMD_TYPE_MAP))

static bool js_stream_can_auto_destroy(Item self, bool after_readable_end) {
    if (!js_item_is_true(js_get_key_default(self, key_auto_destroy)) ||
        js_item_is_true(js_get_key_default(self, key_destroyed))) {
        return false;
    }
    return after_readable_end
        ? (!js_stream_has_writable_side(self) ||
            js_item_is_true(js_get_key_default(self, key_finish_emitted)))
        : (!js_stream_has_readable_side(self) ||
            js_item_is_true(js_get_key_default(self, key_end_emitted)));
}
JS_FORWARD_STATIC_RETURN(bool, js_stream_can_auto_destroy_after_readable_end, (Item self), js_stream_can_auto_destroy, (self, true))
JS_FORWARD_STATIC_RETURN(bool, js_stream_can_auto_destroy_after_writable_finish, (Item self), js_stream_can_auto_destroy, (self, false))

static void js_stream_set_error_emitted(Item self, bool emitted) {
    Item readable_state = js_get_key_default(self, key_readable_state);
    js_state_set_bool(readable_state, "errorEmitted", emitted);
    Item writable_state = js_get_key_default(self, key_writable_state);
    js_state_set_bool(writable_state, "errorEmitted", emitted);
}

static void js_stream_mark_readable_end_emitted(Item self) {
    Item state = js_get_key_default(self, key_readable_state);
    js_state_set_bool(state, "endEmitted", true);
    js_set_key_default(self, key_end_emitted, js_bool_item(true));
    js_create_data_property(self, make_string_item("readableEnded"), js_bool_item(true));
    js_stream_set_readable_open(self, false);
    js_set_key_default(self, make_string_item("readableAborted"), js_bool_item(false));
}

static Item js_stream_end_writable_side_tick(Item self) {
    ensure_keys();
    Item writable_state = js_get_key_default(self, key_writable_state);
    if (!js_item_is_true(js_get_key_default(self, key_writable)) ||
        js_state_get_bool(writable_state, "ended") ||
        js_item_is_true(js_get_key_default(self, key_finish_emitted))) {
        return make_js_undefined();
    }
    return js_writable_end(self, make_js_undefined(), make_js_undefined());
}

#define JS_STREAM_ENV_UNARY_CLOSURE(name, target) \
static Item name(Item env_item) { \
    Item* env = (Item*)(uintptr_t)env_item.item; \
    if (!env) return make_js_undefined(); \
    return target(env[0]); \
}
#define JS_STREAM_ENV_BINARY_CLOSURE(name, target) \
static Item name(Item env_item) { \
    Item* env = (Item*)(uintptr_t)env_item.item; \
    if (!env) return make_js_undefined(); \
    return target(env[0], env[1]); \
}

JS_STREAM_ENV_UNARY_CLOSURE(js_stream_end_writable_side_tick_closure,
    js_stream_end_writable_side_tick)

static void js_stream_maybe_end_writable_after_readable_end(Item self) {
    ensure_keys();
    Item allow_half_open = js_get_key_default(self, make_string_item("allowHalfOpen"));
    if (get_type_id(allow_half_open) != LMD_TYPE_BOOL || it2b(allow_half_open)) return;
    if (!js_item_is_true(js_get_key_default(self, key_writable))) return;
    Item writable_state = js_get_key_default(self, key_writable_state);
    if (get_type_id(writable_state) != LMD_TYPE_MAP ||
        js_state_get_bool(writable_state, "ended") ||
        js_item_is_true(js_get_key_default(self, key_finish_emitted))) {
        return;
    }
    Item* env = js_alloc_env(1);
    env[0] = self;
    Item tick = js_new_native_closure(js_stream_end_writable_side_tick_closure, 0, env, 1);
    js_next_tick_enqueue(tick);
}

static void js_stream_mark_writable_ended(Item self) {
    Item state = js_get_key_default(self, key_writable_state);
    js_state_set_bool(state, "ending", true);
    js_state_set_bool(state, "ended", true);
    js_create_data_property(self, make_string_item("writableEnded"), js_bool_item(true));
    js_stream_set_writable_open(self, false);
}

static void js_stream_mark_writable_finished(Item self) {
    Item state = js_get_key_default(self, key_writable_state);
    js_state_set_bool(state, "finished", true);
    js_set_key_default(self, key_finished, js_bool_item(true));
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
    js_set_key_default(self, make_string_item("__constructing__"), js_bool_item(false));
    js_set_key_default(self, make_string_item("__constructed__"), js_bool_item(true));
    if (js_stream_error_value_present(err)) {
        js_stream_schedule_error(self, err);
    }
    return make_js_undefined();
}

static void js_stream_call_construct(Item obj) {
    Item construct_fn = js_get_key_default(obj, make_string_item("_construct"));
    if (!js_is_callable(construct_fn)) {
        js_set_key_default(obj, make_string_item("__constructing__"), js_bool_item(false));
        js_set_key_default(obj, make_string_item("__constructed__"), js_bool_item(true));
        return;
    }

    js_set_key_default(obj, make_string_item("__constructing__"), js_bool_item(true));
    js_set_key_default(obj, make_string_item("__constructed__"), js_bool_item(false));
    Item* env = js_alloc_env(2);
    env[0] = obj;
    env[1] = js_bool_item(false);
    Item callback = js_new_native_closure(js_stream_construct_callback_once, 1, env, 2);
    Item construct_result = js_call_function(construct_fn, obj, &callback, 1);
    if (item_is_error(construct_result)) {
        Item err = js_error_lane_payload(construct_result);
        js_stream_construct_callback_once((Item){.item = (uint64_t)(uintptr_t)env}, err);
    }
}

static Item js_stream_capture_rejection(Item self, Item err) {
    ensure_keys();
    if (js_item_is_true(js_get_key_default(self, key_destroyed))) {
        return make_js_undefined();
    }
    js_stream_mark_destroyed(self);
    js_stream_schedule_error(self, err);
    return make_js_undefined();
}

static void js_stream_maybe_capture_rejection(Item self, const char* event, Item result) {
    if (!js_item_is_true(js_get_key_default(self, key_capture_rejections))) return;
    if (event && strcmp(event, "error") == 0) return;
    TypeId result_type = get_type_id(result);
    if (result.item == 0 || result_type == LMD_TYPE_UNDEFINED ||
        result_type == LMD_TYPE_NULL) {
        return;
    }
    Item catch_fn = js_get_key_default(result, make_string_item("catch"));
    if (!js_is_callable(catch_fn)) return;
    Item bound_args[1] = { self };
    Item handler = js_bind_function(js_new_native_function(js_stream_capture_rejection),
                                    make_js_undefined(), bound_args, 1);
    js_call_function(catch_fn, result, &handler, 1);
}

static void stream_emit(Item self, const char* event, Item* args, int argc) {
    Item listeners_map = js_get_key_default(self, key_listeners);
    if (get_type_id(listeners_map) != LMD_TYPE_MAP) {
        if (event && strcmp(event, "error") == 0 && args && argc > 0) {
            js_stream_auto_destroy_after_error_emit(self, args[0]);
            js_process_emit(make_string_item("uncaughtException"), args[0]);
        }
        return;
    }
    Item event_key = make_string_item(event);
    Item arr = js_get_key_default(listeners_map, event_key);
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
        Item entry = js_elements_get_int(arr, i);
        Item listener = js_stream_listener_fn(entry);
        Item context = js_stream_listener_context(entry);
        if (js_is_callable(listener)) {
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
    Item listeners_map = js_get_key_default(self, key_listeners);
    if (get_type_id(listeners_map) != LMD_TYPE_MAP) return false;
    Item arr = js_get_key_default(listeners_map, make_string_item(event));
    return get_type_id(arr) == LMD_TYPE_ARRAY && js_array_length(arr) > 0;
}

static Item js_stream_emit_drain_tick(Item self) {
    ensure_keys();
    if (js_item_is_true(js_get_key_default(self, key_destroyed)) ||
        js_item_is_true(js_get_key_default(self, key_finish_emitted))) {
        return make_js_undefined();
    }
    stream_emit(self, "drain", NULL, 0);
    return make_js_undefined();
}

JS_STREAM_ENV_UNARY_CLOSURE(js_stream_emit_drain_tick_closure, js_stream_emit_drain_tick)
JS_FORWARD_STATIC_ITEM(js_stream_transform_deferred_drain_key, (void), make_string_item, ("__transform_deferred_drain__"))

static Item js_stream_transform_deferred_drain_tick(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item self = env[0];
    Item pending_key = js_stream_transform_deferred_drain_key();
    if (!js_item_is_true(js_get_key_default(self, pending_key))) {
        return make_js_undefined();
    }
    js_set_key_default(self, pending_key, js_bool_item(false));
    Item state = js_get_key_default(self, key_writable_state);
    if (!js_state_get_bool(state, "needDrain")) return make_js_undefined();
    js_state_set_bool(state, "needDrain", false);
    js_stream_emit_or_schedule_drain(self);
    return make_js_undefined();
}

static void js_stream_defer_transform_drain(Item self) {
    Item pending_key = js_stream_transform_deferred_drain_key();
    if (js_item_is_true(js_get_key_default(self, pending_key))) return;
    js_set_key_default(self, pending_key, js_bool_item(true));
    Item* env = js_alloc_env(1);
    env[0] = self;
    Item tick = js_new_native_closure(js_stream_transform_deferred_drain_tick, 0, env, 1);
    js_next_tick_enqueue(tick);
}
JS_FORWARD_STATIC_ITEM(js_stream_drain_on_listener_key, (void), make_string_item, ("__drain_on_listener__"))

extern "C" void js_stream_transform_flush_drained(Item self) {
    ensure_keys();
    Item state = js_get_key_default(self, key_writable_state);
    Item deferred_key = js_stream_transform_deferred_drain_key();
    bool need_drain = js_state_get_bool(state, "needDrain");
    bool deferred_drain = js_item_is_true(js_get_key_default(self, deferred_key));
    if (!need_drain && !deferred_drain) return;
    js_state_set_bool(state, "needDrain", false);
    js_set_key_default(self, deferred_key, js_bool_item(false));
    if (!js_stream_has_event_listeners(self, "drain")) {
        js_set_key_default(self, js_stream_drain_on_listener_key(), js_bool_item(true));
        return;
    }
    Item* env = js_alloc_env(1);
    env[0] = self;
    Item tick = js_new_native_closure(js_stream_emit_drain_tick_closure, 0, env, 1);
    js_next_tick_enqueue(tick);
}

static void js_stream_emit_or_schedule_drain(Item self) {
    Item state = js_get_key_default(self, key_readable_state);
    bool readable_pending = js_state_get_bool(state, "readableListening") &&
                            js_state_get_bool(state, "emittedReadable");
    if (readable_pending) {
        js_set_key_default(self, make_string_item("__pending_drain_after_readable__"),
                        js_bool_item(true));
        return;
    }
    stream_emit(self, "drain", NULL, 0);
}

static void js_stream_schedule_pending_drain_after_readable(Item self) {
    Item key = make_string_item("__pending_drain_after_readable__");
    if (!js_item_is_true(js_get_key_default(self, key))) return;
    js_set_key_default(self, key, js_bool_item(false));
    Item* env = js_alloc_env(1);
    env[0] = self;
    Item tick = js_new_native_closure(js_stream_emit_drain_tick_closure, 0, env, 1);
    js_setImmediate(tick);
}

static Item js_stream_emit_readable_tick(Item self) {
    ensure_keys();
    Item state = js_get_key_default(self, key_readable_state);
    if (js_item_is_true(js_get_key_default(self, key_destroyed)) ||
        js_item_is_true(js_get_key_default(self, key_flowing))) {
        js_state_set_bool(state, "emittedReadable", false);
        return make_js_undefined();
    }
    if (!js_stream_has_event_listeners(self, "readable")) {
        js_state_set_bool(state, "emittedReadable", false);
        return make_js_undefined();
    }
    Item buf = js_get_key_default(self, key_buffer);
    bool has_buffered = get_type_id(buf) == LMD_TYPE_ARRAY && js_array_length(buf) > 0;
    if (!has_buffered &&
        (js_item_is_true(js_get_key_default(self, key_end_pending)) ||
         js_item_is_true(js_get_key_default(self, key_end_emitted)))) {
        js_state_set_bool(state, "emittedReadable", false);
        return make_js_undefined();
    }
    stream_emit(self, "readable", NULL, 0);
    js_stream_schedule_pending_drain_after_readable(self);
    return make_js_undefined();
}

JS_STREAM_ENV_UNARY_CLOSURE(js_stream_emit_readable_tick_closure, js_stream_emit_readable_tick)

static void js_stream_emit_readable(Item self) {
    Item state = js_get_key_default(self, key_readable_state);
    js_state_set_bool(state, "needReadable", false);
    if (js_item_is_true(js_get_key_default(self, key_flowing))) {
        js_state_set_bool(state, "emittedReadable", false);
        return;
    }
    if (js_state_get_bool(state, "emittedReadable")) return;
    js_state_set_bool(state, "emittedReadable", true);
    if (js_item_is_true(js_get_key_default(self, make_string_item("__defer_readable_emit__"))) ||
        js_item_is_true(js_get_key_default(self, key_reading_sync))) {
        // sync _read() may push before read() unwinds; defer readable to avoid recursive user callbacks.
        Item* env = js_alloc_env(1);
        env[0] = self;
        Item tick = js_new_native_closure(js_stream_emit_readable_tick_closure, 0, env, 1);
        js_next_tick_enqueue(tick);
        return;
    }
    stream_emit(self, "readable", NULL, 0);
}

static void js_stream_mark_readable_needed(Item self, bool needed) {
    Item state = js_get_key_default(self, key_readable_state);
    js_state_set_bool(state, "needReadable", needed);
}
JS_FORWARD_STATIC_VOID( js_stream_mark_readable_did_read, (Item self), js_state_set_bool, (js_get_key_default(self, key_readable_state), "didRead", true))

static void js_stream_set_reading(Item self, bool reading) {
    js_set_key_default(self, key_reading, js_bool_item(reading));
    js_state_set_bool(js_get_key_default(self, key_readable_state), "reading", reading);
}

static bool js_stream_is_empty_byte_chunk(Item chunk) {
    if (get_type_id(chunk) == LMD_TYPE_STRING) {
        String* str = it2s(chunk);
        return !str || str->len == 0;
    }
    if (!js_stream_chunk_is_arraybuffer_view(chunk)) return false;
    Item byte_length = js_get_key_default(chunk, make_string_item("byteLength"));
    int64_t byte_length_int = 0;
    if (js_stream_item_to_int64(byte_length, &byte_length_int)) return byte_length_int == 0;
    Item length = js_get_key_default(chunk, make_string_item("length"));
    int64_t length_int = 0;
    return js_stream_item_to_int64(length, &length_int) && length_int == 0;
}

static Item js_stream_maybe_emit_manual_data(Item self, Item chunk) {
    bool async_iterator_reading =
        js_item_is_true(js_get_key_default(self, make_string_item("__async_iterator_reading__")));
    if ((!js_item_is_true(js_get_key_default(self, key_flowing)) || async_iterator_reading) &&
        js_stream_has_event_listeners(self, "data")) {
        // async iterators can consume a pushed chunk before the flowing data
        // flush; mirror that read so existing data listeners do not miss it.
        js_set_key_default(self, make_string_item("__emitting_data__"), js_bool_item(true));
        stream_emit(self, "data", &chunk, 1);
        js_set_key_default(self, make_string_item("__emitting_data__"), js_bool_item(false));
    }
    return chunk;
}

static void js_stream_update_need_after_read(Item self) {
    if (js_item_is_true(js_get_key_default(self, key_end_pending)) ||
        js_item_is_true(js_get_key_default(self, key_end_emitted)) ||
        js_item_is_true(js_get_key_default(self, key_flowing))) {
        js_stream_mark_readable_needed(self, false);
        return;
    }
    Item buf = js_get_key_default(self, key_buffer);
    bool empty = get_type_id(buf) != LMD_TYPE_ARRAY || js_array_length(buf) == 0;
    js_stream_mark_readable_needed(self, empty);
}

static Item js_stream_decode_object_readable_chunk(Item self, Item chunk) {
    if (!js_stream_readable_is_object_mode(self)) return chunk;
    Item encoding = js_get_key_default(self, make_string_item("_encoding"));
    if (get_type_id(encoding) != LMD_TYPE_STRING) return chunk;
    if (!js_stream_chunk_is_arraybuffer_view(chunk)) return chunk;
    return js_buffer_toString(chunk, encoding, make_js_undefined(), make_js_undefined());
}
JS_FORWARD_STATIC_EXPRESSION(bool, js_stream_has_callback_error, (Item err), (err.item != 0 && get_type_id(err) != LMD_TYPE_UNDEFINED && get_type_id(err) != LMD_TYPE_NULL))

static Item js_stream_make_write_request(Item chunk, Item encoding, Item callback) {
    Item request = js_new_object();
    js_set_key_default(request, make_string_item("chunk"), chunk);
    js_set_key_default(request, make_string_item("encoding"), encoding);
    js_set_key_default(request, make_string_item("callback"), callback);
    return request;
}

static void js_stream_finish_write_cycle(Item self, Item state,
        bool need_drain, bool has_error, Item err) {
    if (has_error) {
        js_set_key_default(self, make_string_item("__writable_end_pending__"), js_bool_item(false));
        js_stream_call_writable_end_callbacks(self, err);
    }
    if (!has_error && need_drain &&
        !js_state_get_bool(state, "ended") &&
        !js_item_is_true(js_get_key_default(self, make_string_item("__transform_end_pending__"))) &&
        !js_item_is_true(js_get_key_default(self, key_destroyed)) &&
        !js_item_is_true(js_get_key_default(self, key_finish_emitted))) {
        js_stream_emit_or_schedule_drain(self);
    }
    if (!has_error) {
        js_stream_flush_pending_writes(self);
        js_writable_maybe_finish_deferred(self);
        js_transform_maybe_finish_deferred(self);
    }
}

static void js_stream_buffer_write_request(Item self, Item chunk, Item encoding, Item callback) {
    Item pending = js_get_key_default(self, make_string_item("_pendingWrites"));
    if (get_type_id(pending) != LMD_TYPE_ARRAY) {
        pending = js_array_new(0);
        js_set_key_default(self, make_string_item("_pendingWrites"), pending);
    }
    js_array_push(pending, js_stream_make_write_request(chunk, encoding, callback));
    js_stream_set_buffered_request_count(self, js_array_length(pending));
}

static Item js_stream_prepare_write_completion(Item self, Item err,
        bool* need_drain_out, bool* has_error_out) {
    ensure_keys();
    Item state = js_get_key_default(self, key_writable_state);
    bool need_drain = js_state_get_bool(state, "needDrain");
    bool has_error = js_stream_has_callback_error(err);
    js_set_key_default(self, make_string_item("_writing"), js_bool_item(false));
    js_stream_adjust_writable_pendingcb(self, -1);
    js_state_set_item(state, "length", (Item){.item = i2it(0)});
    js_state_set_bool(state, "needDrain", false);
    if (has_error) {
        js_stream_set_writable_open(self, false);
        js_stream_schedule_error(self, err);
    }
    *need_drain_out = need_drain;
    *has_error_out = has_error;
    return state;
}

static Item js_stream_after_write(Item self, Item callback, Item err) {
    bool need_drain = false;
    bool has_error = false;
    Item state = js_stream_prepare_write_completion(self, err,
        &need_drain, &has_error);

    if (js_is_callable(callback)) {
        if (has_error) {
            js_call_function(callback, self, &err, 1);
        } else {
            js_call_function(callback, self, NULL, 0);
        }
    }
    js_stream_finish_write_cycle(self, state, need_drain, has_error, err);
    return make_js_undefined();
}

static bool js_stream_claim_once_callback(Item* env) {
    if (!env) return false;
    if (js_item_is_true(env[2])) {
        Item multi = js_stream_make_error_with_code("ERR_MULTIPLE_CALLBACK",
            "Callback called multiple times");
        js_stream_schedule_error(env[0], multi);
        return false;
    }
    env[2] = js_bool_item(true);
    return true;
}

static Item* js_stream_alloc_once_callback_env(Item self, Item payload) {
    Item* env = js_alloc_env(3);
    env[0] = self;
    env[1] = payload;
    env[2] = js_bool_item(false);
    return env;
}

typedef Item (*JsStreamOnceOperation)(Item, Item, Item);

static Item js_stream_once_callback(Item env_item, Item err,
                                    JsStreamOnceOperation operation) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!js_stream_claim_once_callback(env)) return make_js_undefined();
    return operation(env[0], env[1], err);
}
JS_FORWARD_STATIC_ITEM(js_stream_write_callback_once, (Item env_item, Item err), js_stream_once_callback, (env_item, err, js_stream_after_write))

static Item js_stream_make_write_callback(Item self, Item callback) {
    js_stream_adjust_writable_pendingcb(self, 1);
    Item* env = js_stream_alloc_once_callback_env(self, callback);
    return js_new_native_closure(js_stream_write_callback_once, 1, env, 3);
}

static Item js_stream_after_transform_write(Item self, Item callback, Item err, Item data) {
    ensure_keys();
    bool has_error = js_stream_has_callback_error(err);
    if (!has_error && data.item != 0 &&
        get_type_id(data) != LMD_TYPE_UNDEFINED &&
        get_type_id(data) != LMD_TYPE_NULL) {
        JS_ASSIGN_OR_RETURN(push_result, js_readable_push(self, data));
    }
    return js_stream_after_write(self, callback, err);
}

static Item js_stream_transform_write_callback_once(Item env_item, Item err, Item data) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!js_stream_claim_once_callback(env)) return make_js_undefined();
    return js_stream_after_transform_write(env[0], env[1], err, data);
}

static Item js_stream_make_transform_write_callback(Item self, Item callback) {
    js_stream_adjust_writable_pendingcb(self, 1);
    Item* env = js_stream_alloc_once_callback_env(self, callback);
    return js_new_native_closure(js_stream_transform_write_callback_once, 2, env, 3);
}

static Item js_stream_after_writev(Item self, Item pending, Item err) {
    bool need_drain = false;
    bool has_error = false;
    Item state = js_stream_prepare_write_completion(self, err,
        &need_drain, &has_error);

    if (get_type_id(pending) == LMD_TYPE_ARRAY) {
        int64_t plen = js_array_length(pending);
        for (int64_t i = 0; i < plen; i++) {
            Item request = js_elements_get_int(pending, i);
            Item callback = js_get_key_default(request, make_string_item("callback"));
            if (js_is_callable(callback)) {
                if (has_error) {
                    js_call_function(callback, self, &err, 1);
                } else {
                    js_call_function(callback, self, NULL, 0);
                }
            }
        }
    }
    js_stream_finish_write_cycle(self, state, need_drain, has_error, err);
    return make_js_undefined();
}
JS_FORWARD_STATIC_ITEM(js_stream_writev_callback_once, (Item env_item, Item err), js_stream_once_callback, (env_item, err, js_stream_after_writev))

static Item js_stream_make_writev_callback(Item self, Item pending) {
    js_stream_adjust_writable_pendingcb(self, 1);
    Item* env = js_stream_alloc_once_callback_env(self, pending);
    return js_new_native_closure(js_stream_writev_callback_once, 1, env, 3);
}

static void js_stream_flush_pending_writes(Item self) {
    if (js_item_is_true(js_get_key_default(self, make_string_item("_writing")))) return;

    Item pending = js_get_key_default(self, make_string_item("_pendingWrites"));
    if (get_type_id(pending) != LMD_TYPE_ARRAY) return;
    int64_t plen = js_array_length(pending);
    if (plen <= 0) return;

    js_set_key_default(self, make_string_item("_pendingWrites"), js_array_new(0));
    js_stream_set_buffered_request_count(self, 0);

    Item writev_fn = js_get_key_default(self, make_string_item("_writev"));
    Item write_handler = js_get_key_default(self, make_string_item("_write"));
    if (!js_is_callable(write_handler)) {
        write_handler = js_get_key_default(self, make_string_item("__write_handler__"));
    }
    Item transform_fn = js_get_key_default(self, make_string_item("_transform"));
    if (!js_is_callable(write_handler) &&
        js_is_callable(transform_fn)) {
        Item request = js_elements_get_int(pending, 0);
        Item chunk = js_get_key_default(request, make_string_item("chunk"));
        Item encoding = js_get_key_default(request, make_string_item("encoding"));
        Item callback = js_get_key_default(request, make_string_item("callback"));
        Item next_pending = js_array_new(0);
        for (int64_t i = 1; i < plen; i++) {
            js_array_push(next_pending, js_elements_get_int(pending, i));
        }
        js_set_key_default(self, make_string_item("_pendingWrites"), next_pending);
        js_stream_set_buffered_request_count(self, js_array_length(next_pending));
        Item write_cb = js_stream_make_transform_write_callback(self, callback);
        Item args[3] = {chunk, encoding, write_cb};
        js_set_key_default(self, make_string_item("_writing"), js_bool_item(true));
        Item transform_result = js_call_function(transform_fn, self, args, 3);
        if (item_is_error(transform_result)) {
            Item err = js_error_lane_payload(transform_result);
            js_stream_after_write(self, callback, err);
        }
        return;
    }
    if (js_is_callable(writev_fn) &&
        (plen > 1 || !js_is_callable(write_handler))) {
        Item writev_cb = js_stream_make_writev_callback(self, pending);
        Item args[2] = {pending, writev_cb};
        js_set_key_default(self, make_string_item("_writing"), js_bool_item(true));
        js_call_function(writev_fn, self, args, 2);
        return;
    }

    if (!js_is_callable(write_handler)) return;

    for (int64_t i = 0; i < plen; i++) {
        Item request = js_elements_get_int(pending, i);
        Item chunk = js_get_key_default(request, make_string_item("chunk"));
        Item encoding = js_get_key_default(request, make_string_item("encoding"));
        Item callback = js_get_key_default(request, make_string_item("callback"));
        Item write_cb = js_stream_make_write_callback(self, callback);
        Item args[3] = {chunk, encoding, write_cb};
        js_set_key_default(self, make_string_item("_writing"), js_bool_item(true));
        js_call_function(write_handler, self, args, 3);
    }
}

static Item js_stream_emit_end_tick(Item self) {
    ensure_keys();
    if (js_item_is_true(js_get_key_default(self, key_destroyed)) ||
        js_item_is_true(js_get_key_default(self, key_end_emitted))) {
        return make_js_undefined();
    }
    Item buf = js_get_key_default(self, key_buffer);
    if (get_type_id(buf) == LMD_TYPE_ARRAY && js_array_length(buf) > 0) {
        return make_js_undefined();
    }
    js_set_key_default(self, key_end_pending, js_bool_item(false));
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
    if (js_item_is_true(js_get_key_default(self, key_close_emitted))) {
        return make_js_undefined();
    }
    js_set_key_default(self, key_close_emitted, js_bool_item(true));
    js_set_key_default(self, key_closed, js_bool_item(true));
    stream_emit(self, "close", NULL, 0);
    return make_js_undefined();
}

JS_STREAM_ENV_UNARY_CLOSURE(js_stream_emit_end_tick_closure, js_stream_emit_end_tick)
JS_STREAM_ENV_UNARY_CLOSURE(js_stream_emit_close_tick_closure, js_stream_emit_close_tick)

static void js_stream_schedule_unary(Item self, JsNativeP1 target) {
    Item* env = js_alloc_env(1);
    env[0] = self;
    Item tick = js_new_native_closure(target, 0, env, 1);
    js_next_tick_enqueue(tick);
}
JS_FORWARD_STATIC_VOID( js_stream_schedule_close, (Item self), js_stream_schedule_unary, (self, js_stream_emit_close_tick_closure))

static void js_stream_schedule_end(Item self) {
    if (js_item_is_true(js_get_key_default(self, key_end_emitted))) return;
    js_stream_schedule_unary(self, js_stream_emit_end_tick_closure);
}

static Item js_stream_emit_error_tick(Item self, Item err) {
    ensure_keys();
    js_stream_set_error_emitted(self, true);
    stream_emit(self, "error", &err, 1);
    return make_js_undefined();
}

JS_STREAM_ENV_BINARY_CLOSURE(js_stream_emit_error_tick_closure, js_stream_emit_error_tick)

static void js_stream_schedule_error(Item self, Item err) {
    js_stream_set_error_state(self, err);
    js_set_key_default(self, make_string_item("__error__"), err);
    js_stream_async_iterators_drain(self, err);
    Item* env = js_alloc_env(2);
    env[0] = self;
    env[1] = err;
    Item tick = js_new_native_closure(js_stream_emit_error_tick_closure, 0, env, 2);
    js_next_tick_enqueue(tick);
}

static bool js_stream_schedule_error_once(Item self, Item err) {
    if (js_stream_has_stored_error(self)) return false;
    js_stream_schedule_error(self, err);
    return true;
}

static void js_stream_invoke_destroy_callback(Item self, Item err) {
    Item callback_key = make_string_item("__destroy_callback__");
    Item callback = js_get_key_default(self, callback_key);
    if (!js_is_callable(callback)) return;
    js_set_key_default(self, callback_key, make_js_undefined());
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
        js_set_key_default(self, make_string_item("__error__"), err);
    }
    if (js_item_is_true(js_get_key_default(self, make_string_item("__destroying_sync__")))) {
        js_set_key_default(self, make_string_item("__destroy_cb_done__"), js_bool_item(true));
        js_set_key_default(self, make_string_item("__destroy_cb_error__"), err);
        return make_js_undefined();
    }
    js_set_key_default(self, key_destroy_pending, js_bool_item(false));
    if (js_stream_has_callback_error(err)) {
        // Iterators created while _destroy(cb) is pending wait for cb's error;
        // scheduling 'error' alone leaves their next() promises unresolved.
        js_stream_async_iterators_drain(self, err);
        js_stream_schedule_error(self, err);
    } else {
        Item iterators = js_get_key_default(self, make_string_item("__async_iterators__"));
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

static void js_stream_auto_destroy(Item self, Item err, bool error_emit) {
    ensure_keys();
    if ((error_emit && !js_item_is_true(js_get_key_default(self, key_auto_destroy))) ||
        js_item_is_true(js_get_key_default(self, key_destroyed))) return;

    bool readable_aborted = js_stream_has_readable_side(self) &&
                            !js_item_is_true(js_get_key_default(self, key_end_emitted));
    bool writable_aborted = js_stream_has_writable_side(self) &&
                            !js_item_is_true(js_get_key_default(self, key_finish_emitted));
    js_stream_mark_destroyed(self);
    js_set_key_default(self, make_string_item("readableAborted"), js_bool_item(readable_aborted));
    js_set_key_default(self, make_string_item("writableAborted"), js_bool_item(writable_aborted));
    if (error_emit) {
        js_stream_set_error_state(self, err);
        js_set_key_default(self, make_string_item("__error__"), err);
        js_stream_async_iterators_drain(self, err);
    }

    Item destroy_fn = js_get_key_default(self, make_string_item("_destroy"));
    if (js_is_callable(destroy_fn)) {
        js_set_key_default(self, key_destroy_pending, js_bool_item(true));
        Item destroy_cb = js_bind_function(js_new_native_function(js_stream_after_destroy),
                                           make_js_undefined(), &self, 1);
        Item destroy_err = error_emit && js_stream_has_callback_error(err) ? err : ItemNull;
        Item args[2] = { destroy_err, destroy_cb };
        Item destroy_result = js_call_function(destroy_fn, self, args, 2);
        if (item_is_error(destroy_result)) {
            Item err = js_error_lane_payload(destroy_result);
            js_set_key_default(self, key_destroy_pending, js_bool_item(false));
            js_stream_schedule_error(self, err);
            js_stream_schedule_close(self);
        }
        return;
    }

        js_stream_schedule_close(self);
}
JS_FORWARD_STATIC_VOID( js_stream_auto_destroy_after_terminal, (Item self), js_stream_auto_destroy, (self, ItemNull, false))
JS_FORWARD_STATIC_VOID( js_stream_auto_destroy_after_error_emit, (Item self, Item err), js_stream_auto_destroy, (self, err, true))

static int64_t js_stream_read_size_hint(Item self, Item size_item) {
    Item state = js_get_key_default(self, key_readable_state);
    int64_t hwm = js_stream_state_get_int(state, "highWaterMark", js_stream_default_byte_hwm);
    int64_t requested = 0;
    if (js_stream_item_to_int64(size_item, &requested) && requested > 0 &&
        !js_state_get_bool(state, "objectMode")) {
        if (requested > hwm) {
            int64_t next = 1;
            while (next < requested && next < 0x40000000) next <<= 1;
            hwm = next < requested ? requested : next;
            Item hwm_item = (Item){.item = i2it(hwm)};
            js_set_key_default(self, make_string_item("readableHighWaterMark"), hwm_item);
            js_state_set_item(state, "highWaterMark", hwm_item);
        }
    }
    return hwm;
}

static void js_stream_call_read_if_needed(Item self, Item size_item) {
    if (js_item_is_true(js_get_key_default(self, key_destroyed)) ||
        js_item_is_true(js_get_key_default(self, key_end_pending)) ||
        js_item_is_true(js_get_key_default(self, key_end_emitted)) ||
        js_item_is_true(js_get_key_default(self, key_reading))) {
        return;
    }

    Item read_fn = js_get_key_default(self, make_string_item("_read"));
    if (!js_is_callable(read_fn)) return;

    Item before_buf = js_get_key_default(self, key_buffer);
    int64_t before_len = get_type_id(before_buf) == LMD_TYPE_ARRAY ? js_array_length(before_buf) : 0;
    js_stream_set_reading(self, true);
    js_set_key_default(self, key_reading_sync, js_bool_item(true));
    Item size = (Item){.item = i2it(js_stream_read_size_hint(self, size_item))};
    Item read_result = js_call_function(read_fn, self, &size, 1);
    js_set_key_default(self, key_reading_sync, js_bool_item(false));
    if (item_is_error(read_result)) {
        Item err = js_error_lane_payload(read_result);
        js_stream_set_reading(self, false);
        js_stream_destroy(self, err);
        return;
    }
    Item after_buf = js_get_key_default(self, key_buffer);
    int64_t after_len = get_type_id(after_buf) == LMD_TYPE_ARRAY ? js_array_length(after_buf) : 0;
    if (after_len > before_len || js_item_is_true(js_get_key_default(self, key_end_pending))) {
        js_stream_set_reading(self, false);
    }

    if (js_item_is_true(js_get_key_default(self, key_end_pending))) {
        Item flowing = js_get_key_default(self, key_flowing);
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

JS_STREAM_ENV_UNARY_CLOSURE(js_stream_call_read_tick_closure, js_stream_call_read_tick)
JS_FORWARD_STATIC_VOID( js_stream_schedule_read, (Item self), js_stream_schedule_unary, (self, js_stream_call_read_tick_closure))

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
    Item pending = js_get_key_default(self, make_string_item("__decode_pending__"));
    if (js_is_typed_array(pending)) {
        Item parts = js_array_new(0);
        js_array_push(parts, pending);
        js_array_push(parts, chunk);
        chunk = js_buffer_concat(parts, make_js_undefined());
        js_set_key_default(self, make_string_item("__decode_pending__"), make_js_undefined());
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
    js_set_key_default(self, make_string_item("__decode_pending__"), tail);
    if (head_len <= 0) return make_string_item("");
    Item head = js_buffer_slice(chunk, (Item){.item = i2it(0)}, (Item){.item = i2it(head_len)});
    return js_buffer_toString(head, encoding, make_js_undefined(), make_js_undefined());
}

static Item js_stream_decode_readable_chunk(Item self, Item chunk) {
    if (js_stream_readable_is_object_mode(self)) return chunk;
    Item encoding = js_get_key_default(self, make_string_item("_encoding"));
    if (get_type_id(encoding) != LMD_TYPE_STRING) return chunk;
    if (get_type_id(chunk) == LMD_TYPE_STRING) return chunk;
    if (js_stream_encoding_is_utf8(encoding)) return js_stream_decode_utf8_chunk(self, chunk, encoding);
    return js_buffer_toString(chunk, encoding, make_js_undefined(), make_js_undefined());
}

static void js_stream_flush_pending_decode(Item self) {
    Item encoding = js_get_key_default(self, make_string_item("_encoding"));
    if (!js_stream_encoding_is_utf8(encoding)) return;
    Item pending = js_get_key_default(self, make_string_item("__decode_pending__"));
    if (!js_is_typed_array(pending)) return;
    js_set_key_default(self, make_string_item("__decode_pending__"), make_js_undefined());
    Item emitted = js_buffer_toString(pending, encoding, make_js_undefined(), make_js_undefined());
    if (get_type_id(emitted) == LMD_TYPE_STRING) {
        String* str = it2s(emitted);
        if (str && str->len > 0) {
            js_set_key_default(self, make_string_item("__emitting_data__"), js_bool_item(true));
            stream_emit(self, "data", &emitted, 1);
            js_set_key_default(self, make_string_item("__emitting_data__"), js_bool_item(false));
        }
    }
}

static void js_stream_coalesce_readable_buffer_for_encoding(Item self, Item encoding) {
    if (js_stream_readable_is_object_mode(self)) return;
    if (get_type_id(encoding) != LMD_TYPE_STRING) return;
    Item buf = js_get_key_default(self, key_buffer);
    if (get_type_id(buf) != LMD_TYPE_ARRAY) return;
    int64_t blen = js_array_length(buf);
    if (blen <= 1) return;

    Item joined = js_stream_readable_buffer_has_string(buf)
        ? js_stream_concat_decoded_chunks(buf, encoding)
        : js_buffer_concat(buf, make_js_undefined());
    if (item_is_error(joined)) return;
    if (joined.item == 0 || get_type_id(joined) == LMD_TYPE_UNDEFINED ||
        get_type_id(joined) == LMD_TYPE_NULL) {
        return;
    }
    Item next_buf = js_array_new(0);
    js_array_push(next_buf, joined);
    js_stream_set_readable_buffer(self, next_buf);
}

static void js_stream_flush_buffered_data(Item self) {
    RootFrame roots(5);
    Rooted<Item> self_root(roots, self);
    Rooted<Item> buffer_root(roots, ItemNull);
    Rooted<Item> chunk_root(roots, ItemNull);
    Rooted<Item> next_buffer_root(roots, ItemNull);
    Rooted<Item> emitted_root(roots, ItemNull);
    self = self_root.get();
    for (;;) {
        buffer_root.set(js_get_key_default(self, key_buffer));
        Item buf = buffer_root.get();
        if (get_type_id(buf) != LMD_TYPE_ARRAY) return;
        int64_t blen = js_array_length(buf);
        if (blen <= 0) {
            if (js_item_is_true(js_get_key_default(self, key_end_pending)) &&
                !js_item_is_true(js_get_key_default(self, key_end_emitted))) {
                js_stream_flush_pending_decode(self);
                js_stream_schedule_end(self);
            }
            return;
        }

        chunk_root.set(js_elements_get_int(buf, 0));
        next_buffer_root.set(js_array_new(0));
        Item next_buf = next_buffer_root.get();
        for (int64_t i = 1; i < blen; i++) {
            js_array_push(next_buf, js_elements_get_int(buf, i));
        }
        js_stream_set_readable_buffer(self, next_buf);

        emitted_root.set(js_stream_decode_readable_chunk(self, chunk_root.get()));
        Item emitted = emitted_root.get();
        if (get_type_id(emitted) == LMD_TYPE_STRING) {
            String* str = it2s(emitted);
            if (!str || str->len == 0) continue;
        }
        js_stream_mark_readable_did_read(self);
        js_set_key_default(self, make_string_item("__emitting_data__"), js_bool_item(true));
        stream_emit(self, "data", &emitted, 1);
        js_set_key_default(self, make_string_item("__emitting_data__"), js_bool_item(false));
        js_stream_maybe_drain_transform_readable_backpressure(self);
        if (!js_item_is_true(js_get_key_default(self, key_flowing))) return;
    }
}

static void js_stream_read_more_if_flowing(Item self) {
    if (!js_item_is_true(js_get_key_default(self, key_flowing))) return;
    Item buf = js_get_key_default(self, key_buffer);
    if (js_stream_readable_accepts_more(self, buf)) {
        js_stream_call_read_if_needed(self, make_js_undefined());
    }
}

static void js_stream_flow_tick_drain(Item self) {
    js_stream_flush_buffered_data(self);
    js_stream_read_more_if_flowing(self);
    Item buf = js_get_key_default(self, key_buffer);
    if (js_item_is_true(js_get_key_default(self, key_flowing)) &&
        get_type_id(buf) == LMD_TYPE_ARRAY && js_array_length(buf) > 0) {
        js_stream_flush_buffered_data(self);
        js_stream_read_more_if_flowing(self);
    }
}

static Item js_stream_flush_data_tick(Item self) {
    ensure_keys();
    js_state_set_bool(js_get_key_default(self, key_readable_state), "resumeScheduled", false);
    if (js_item_is_true(js_get_key_default(self, key_destroyed))) {
        return make_js_undefined();
    }
    js_stream_flow_tick_drain(self);
    return make_js_undefined();
}

JS_STREAM_ENV_UNARY_CLOSURE(js_stream_flush_data_tick_closure, js_stream_flush_data_tick)

static void js_stream_schedule_data_flush(Item self) {
    RootFrame roots(3);
    Rooted<Item> self_root(roots, self);
    Rooted<Item> state_root(roots, js_get_key_default(self_root.get(), key_readable_state));
    Rooted<Item> tick_root(roots, ItemNull);
    if (js_state_get_bool(state_root.get(), "resumeScheduled")) return;
    js_state_set_bool(state_root.get(), "resumeScheduled", true);
    Item* env = js_alloc_env(1);
    env[0] = self_root.get();
    // Enqueue may allocate and collect before it retains the task; keep the
    // closure and its captured stream alive until queue ownership is installed.
    tick_root.set(js_new_native_closure(js_stream_flush_data_tick_closure, 0, env, 1));
    js_next_tick_enqueue(tick_root.get());
}

static Item js_stream_resume_tick(Item self) {
    ensure_keys();
    Item state = js_get_key_default(self, key_readable_state);
    js_state_set_bool(state, "resumeScheduled", false);
    if (js_item_is_true(js_get_key_default(self, key_destroyed))) {
        return make_js_undefined();
    }
    stream_emit(self, "resume", NULL, 0);
    js_stream_flow_tick_drain(self);
    return make_js_undefined();
}

JS_STREAM_ENV_UNARY_CLOSURE(js_stream_resume_tick_closure, js_stream_resume_tick)

static void js_stream_schedule_resume(Item self) {
    Item state = js_get_key_default(self, key_readable_state);
    if (js_state_get_bool(state, "resumeScheduled")) return;
    js_state_set_bool(state, "resumeScheduled", true);
    Item* env = js_alloc_env(1);
    env[0] = self;
    Item tick = js_new_native_closure(js_stream_resume_tick_closure, 0, env, 1);
    js_next_tick_enqueue(tick);
}

extern "C" void js_stream_flush_data_now(Item self) {
    ensure_keys();
    if (js_item_is_true(js_get_key_default(self, key_destroyed))) return;
    js_stream_flush_buffered_data(self);
}

extern "C" void js_stream_flush_data_if_flowing(Item self) {
    ensure_keys();
    if (!js_item_is_true(js_get_key_default(self, key_flowing))) return;
    js_stream_flush_data_now(self);
}

// on(event, listener)
extern "C" Item js_stream_on(Item self, Item event_item, Item listener) {
    RootFrame roots(3);
    Rooted<Item> self_root(roots, self);
    Rooted<Item> event_root(roots, event_item);
    Rooted<Item> listener_root(roots, listener);
    self = self_root.get();
    event_item = event_root.get();
    listener = listener_root.get();
    ensure_keys();
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;

    Item listeners_map = js_get_key_default(self, key_listeners);
    if (get_type_id(listeners_map) != LMD_TYPE_MAP) {
        listeners_map = js_new_object();
        js_set_key_default(self, key_listeners, listeners_map);
    }

    Item arr = js_get_key_default(listeners_map, event_item);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) {
        arr = js_array_new(0);
        js_set_key_default(listeners_map, event_item, arr);
    }
    js_array_push(arr, js_stream_make_listener_record(listener));

    // if adding 'data' listener to readable, start flowing mode
    bool is_data_event = js_stream_string_equals(event_item, "data");
    bool is_readable_event = js_stream_string_equals(event_item, "readable");
    bool is_finish_event = js_stream_string_equals(event_item, "finish");
    bool is_drain_event = js_stream_string_equals(event_item, "drain");

    if (is_readable_event) {
        Item state = js_get_key_default(self, key_readable_state);
        js_state_set_bool(state, "readableListening", true);
        js_state_set_bool(state, "needReadable", true);
        Item buf = js_get_key_default(self, key_buffer);
        bool has_buffered = get_type_id(buf) == LMD_TYPE_ARRAY && js_array_length(buf) > 0;
        if (has_buffered) {
            js_stream_emit_readable(self);
            if (!js_item_is_true(js_get_key_default(self, key_end_pending)) &&
                js_stream_readable_accepts_more(self, buf)) {
                js_stream_call_read_if_needed(self, make_js_undefined());
            }
        } else {
            js_set_key_default(self, make_string_item("__defer_readable_emit__"), js_bool_item(true));
            js_stream_call_read_if_needed(self, make_js_undefined());
            js_set_key_default(self, make_string_item("__defer_readable_emit__"), js_bool_item(false));
        }
    }

    if (is_data_event) {
        if (js_state_get_bool(js_get_key_default(self, key_readable_state), "readableListening")) {
            js_stream_set_flowing(self, false);
        } else {
            js_state_set_bool(js_get_key_default(self, key_readable_state), "needReadable", false);
            js_state_set_bool(js_get_key_default(self, key_readable_state), "emittedReadable", false);
            js_stream_set_flowing(self, true);
            js_set_key_default(self, key_paused, js_bool_item(false));
            if (js_item_is_true(js_get_key_default(self, key_capture_rejections))) {
                js_stream_schedule_data_flush(self);
            } else {
                js_stream_schedule_data_flush(self);
            }
        }
    }
    if (is_finish_event &&
        js_item_is_true(js_get_key_default(self, key_finish_emitted))) {
        if (js_is_callable(listener)) {
            Item result = js_call_function(listener, self, NULL, 0);
            js_stream_maybe_capture_rejection(self, "finish", result);
        }
    }
    if (is_drain_event && js_item_is_true(js_get_key_default(self, js_stream_drain_on_listener_key()))) {
        js_set_key_default(self, js_stream_drain_on_listener_key(), js_bool_item(false));
        if (js_is_callable(listener)) {
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

    Item listeners_map = js_get_key_default(self, key_listeners);
    if (get_type_id(listeners_map) != LMD_TYPE_MAP) return self;
    Item arr = js_get_key_default(listeners_map, event_item);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) return self;

    Item next = js_array_new(0);
    int64_t len = js_array_length(arr);
    for (int64_t i = 0; i < len; i++) {
        Item current = js_elements_get_int(arr, i);
        if (!js_stream_listener_matches(current, listener)) js_array_push(next, current);
    }
    if (js_array_length(next) == 0) {
        js_set_key_default(listeners_map, event_item, make_js_undefined());
    } else {
        js_set_key_default(listeners_map, event_item, next);
    }
    if (js_stream_string_equals(event_item, "readable") && js_array_length(next) == 0) {
        js_state_set_bool(js_get_key_default(self, key_readable_state), "readableListening", false);
    }
    return self;
}

extern "C" Item js_stream_removeAllListeners(Item self, Item event_item) {
    ensure_keys();
    Item listeners_map = js_get_key_default(self, key_listeners);
    if (get_type_id(listeners_map) != LMD_TYPE_MAP) return self;
    if (event_item.item == 0 || get_type_id(event_item) == LMD_TYPE_UNDEFINED) {
        js_set_key_default(self, key_listeners, js_new_object());
        js_state_set_bool(js_get_key_default(self, key_readable_state), "readableListening", false);
        js_stream_call_read_if_needed(self, make_js_undefined());
        return self;
    }
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    js_set_key_default(listeners_map, event_item, js_array_new(0));
    if (js_stream_string_equals(event_item, "readable")) {
        js_state_set_bool(js_get_key_default(self, key_readable_state), "readableListening", false);
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
    if (!js_is_callable(listener)) return make_js_undefined();
    if (arg1.item == 0 || get_type_id(arg1) == LMD_TYPE_UNDEFINED) {
        return js_call_function(listener, self, NULL, 0);
    }
    return js_call_function(listener, self, &arg1, 1);
}

extern "C" Item js_stream_once(Item self, Item event_item, Item listener) {
    ensure_keys();
    if (get_type_id(event_item) != LMD_TYPE_STRING ||
        !js_is_callable(listener)) {
        return self;
    }
    Item* env = js_alloc_env(4);
    env[0] = self;
    env[1] = event_item;
    env[2] = listener;
    env[3] = make_js_undefined();
    Item wrapper = js_new_native_closure(js_stream_once_wrapper, 1, env, 4);
    env[3] = wrapper;
    return js_stream_on(self, event_item, wrapper);
}

// eventNames() — return currently registered event names.
extern "C" Item js_stream_eventNames(Item self) {
    ensure_keys();
    Item listeners_map = js_get_key_default(self, key_listeners);
    if (get_type_id(listeners_map) != LMD_TYPE_MAP) return js_array_new(0);

    Item all_keys = js_object_keys(listeners_map);
    Item result = js_array_new(0);
    int64_t len = js_array_length(all_keys);
    for (int64_t i = len; i > 0; i--) {
        Item key = js_elements_get_int(all_keys, i - 1);
        Item listeners = js_get_key_default(listeners_map, key);
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
    Item listeners_map = js_get_key_default(self, key_listeners);
    if (get_type_id(listeners_map) != LMD_TYPE_MAP) return result;
    Item arr = js_get_key_default(listeners_map, event_item);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) return result;
    int64_t len = js_array_length(arr);
    for (int64_t i = 0; i < len; i++) {
        js_array_push(result, js_stream_listener_fn(js_elements_get_int(arr, i)));
    }
    return result;
}

extern "C" Item js_stream_listenerCount(Item self, Item event_item, Item listener) {
    ensure_keys();
    if (get_type_id(event_item) != LMD_TYPE_STRING) return (Item){.item = i2it(0)};
    Item listeners_map = js_get_key_default(self, key_listeners);
    if (get_type_id(listeners_map) != LMD_TYPE_MAP) return (Item){.item = i2it(0)};
    Item arr = js_get_key_default(listeners_map, event_item);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) return (Item){.item = i2it(0)};
    int64_t len = js_array_length(arr);
    if (listener.item == 0 || get_type_id(listener) == LMD_TYPE_UNDEFINED) {
        return (Item){.item = i2it(len)};
    }
    int64_t count = 0;
    for (int64_t i = 0; i < len; i++) {
        Item current = js_elements_get_int(arr, i);
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
    RootFrame roots(3);
    Rooted<Item> self_root(roots, self);
    Rooted<Item> chunk_root(roots, chunk);
    Rooted<Item> encoding_root(roots, encoding);
    self = self_root.get();
    chunk = chunk_root.get();
    encoding = encoding_root.get();
    ensure_keys();

    // null signals end of stream
    if (chunk.item == 0 || get_type_id(chunk) == LMD_TYPE_NULL) {
        js_stream_set_reading(self, false);
        if (js_item_is_true(js_get_key_default(self, key_destroyed)) ||
            js_item_is_true(js_get_key_default(self, key_end_pending)) ||
            js_item_is_true(js_get_key_default(self, key_end_emitted))) {
            return js_bool_item(true);
        }

        js_set_key_default(self, key_ended, js_bool_item(true));
        Item state = js_get_key_default(self, key_readable_state);
        js_state_set_bool(state, "ended", true);
        Item pipes = js_readable_pipes(self);
        if (get_type_id(pipes) == LMD_TYPE_ARRAY) {
            int64_t plen = js_array_length(pipes);
            for (int64_t i = 0; i < plen; i++) {
                Item pipe_dest = js_elements_get_int(pipes, i);
                if (js_item_is_true(js_get_key_default(pipe_dest, key_destroyed))) {
                    continue;
                }
                Item end_fn = js_get_key_default(pipe_dest, key_end);
                if (js_is_callable(end_fn)) {
                    js_call_function(end_fn, pipe_dest, NULL, 0);
                }
            }
            js_readable_remove_pipe(self, make_js_undefined(), true);
        }

        js_set_key_default(self, key_end_pending, js_bool_item(true));
        js_state_set_bool(state, "needReadable", false);
        Item buf = js_get_key_default(self, key_buffer);
        bool has_buffered = get_type_id(buf) == LMD_TYPE_ARRAY && js_array_length(buf) > 0;
        bool readable_listening = js_state_get_bool(state, "readableListening");
        Item flowing = js_get_key_default(self, key_flowing);
        if (has_buffered) {
            if (flowing.item != 0 && it2b(flowing)) {
                js_stream_schedule_data_flush(self);
            } else if (readable_listening) {
                js_stream_emit_readable(self);
            }
        } else if (flowing.item != 0 && it2b(flowing)) {
            js_stream_schedule_end(self);
        } else if (readable_listening &&
                   js_stream_state_get_int(state, "highWaterMark", js_stream_default_byte_hwm) == 0) {
            js_stream_emit_readable(self);
        }
        js_stream_async_iterators_drain(self, make_js_undefined());
        return js_bool_item(true);
    }

    js_stream_set_reading(self, false);

    if (js_item_is_true(js_get_key_default(self, key_end_pending)) ||
        js_item_is_true(js_get_key_default(self, key_end_emitted))) {
        if (!js_stream_has_event_listeners(self, "error")) {
            return js_throw_error_with_code("ERR_STREAM_PUSH_AFTER_EOF",
                                            "stream.push() after EOF");
        }
        Item err = js_stream_make_error_with_code("ERR_STREAM_PUSH_AFTER_EOF",
            "stream.push() after EOF");
        js_stream_schedule_error_once(self, err);
        return js_bool_item(false);
    }
    if (!js_item_is_true(js_get_key_default(self, key_readable))) {
        Item err = js_stream_make_error_with_code("ERR_STREAM_PUSH_AFTER_EOF",
            "stream.push() after EOF");
        js_stream_schedule_error_once(self, err);
        return js_bool_item(false);
    }

    JS_ASSIGN_OR_RETURN(preparation, js_stream_prepare_readable_chunk(self, &chunk, encoding));
    chunk_root.set(chunk);
    chunk = chunk_root.get();
    if (!js_stream_readable_is_object_mode(self) &&
        js_stream_is_empty_byte_chunk(chunk)) {
        if (!js_item_is_true(js_get_key_default(self, key_end_pending)) &&
            !js_item_is_true(js_get_key_default(self, key_end_emitted))) {
            js_stream_schedule_read(self);
        }
        Item buf = js_get_key_default(self, key_buffer);
        return js_bool_item(js_stream_readable_accepts_more(self, buf));
    }

    if (js_stream_await_drain_pending(self)) {
        // backpressured pipes may still prefetch one readable chunk; while
        // awaiting drain, that chunk belongs in the source buffer, not dest.write().
        Item buf = js_get_key_default(self, key_buffer);
        if (get_type_id(buf) != LMD_TYPE_ARRAY) {
            buf = js_array_new(0);
            js_stream_set_readable_buffer(self, buf);
        }
        js_stream_append_readable_chunk(self, buf, chunk);
        if (js_state_get_bool(js_get_key_default(self, key_readable_state), "readableListening")) {
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
            Item pipe_dest = js_elements_get_int(pipes, i);
            if (js_item_is_true(js_get_key_default(pipe_dest, key_destroyed))) {
                js_readable_remove_pipe(self, pipe_dest, true);
                removed_destroyed_pipe = true;
                continue;
            }
            Item write_fn = js_get_key_default(pipe_dest, key_write);
            if (js_is_callable(write_fn)) {
                wrote_to_pipe = true;
                bool dest_readable_end_first = false;
                Item result = js_call_function(write_fn, pipe_dest, &chunk, 1);
                if (item_is_error(result)) {
                    Item err = js_error_lane_payload(result);
                    js_stream_schedule_error(pipe_dest, err);
                    return js_bool_item(false);
                }
                Item dest_readable_state = js_get_key_default(pipe_dest, key_readable_state);
                if (get_type_id(dest_readable_state) == LMD_TYPE_MAP &&
                    (js_state_get_bool(dest_readable_state, "ended") ||
                     js_item_is_true(js_get_key_default(pipe_dest, key_end_pending)) ||
                     js_item_is_true(js_get_key_default(pipe_dest, key_end_emitted)))) {
                    Item ended_seen_key = make_string_item("__pipe_dest_readable_ended_seen__");
                    if (!js_item_is_true(js_get_key_default(self, ended_seen_key))) {
                        js_set_key_default(self, ended_seen_key, js_bool_item(true));
                        dest_readable_end_first = true;
                    } else {
                        js_stream_set_flowing(self, false);
                        js_set_key_default(self, key_paused, js_bool_item(true));
                        return js_bool_item(false);
                    }
                }
                if (get_type_id(result) == LMD_TYPE_BOOL && !it2b(result)) {
                    Item dest_error = js_get_key_default(pipe_dest, make_string_item("__error__"));
                    if (js_stream_has_callback_error(dest_error)) {
                        js_set_key_default(self, make_string_item("__piped__"), js_bool_item(false));
                        js_set_key_default(self, make_string_item("__pipe_dest__"), make_js_undefined());
                        return js_bool_item(false);
                    }
                    js_stream_await_drain_add(self, pipe_dest);
                    backpressured = true;
                    if (dest_readable_end_first) js_stream_schedule_read(self);
                    if (!js_stream_source_keeps_pipe_on_backpressure(self)) {
                        js_set_key_default(self, make_string_item("__piped__"), js_bool_item(false));
                        js_set_key_default(self, make_string_item("__pipe_dest__"), make_js_undefined());
                    }
                }
            }
        }
        if (backpressured) {
            js_stream_set_flowing(self, false);
            js_set_key_default(self, key_paused, js_bool_item(true));
            js_stream_schedule_read(self);
            return js_bool_item(false);
        }
        if (removed_destroyed_pipe && !wrote_to_pipe &&
            !js_item_is_true(js_get_key_default(self, key_flowing)) &&
            !js_stream_has_event_listeners(self, "data") &&
            !js_state_get_bool(js_get_key_default(self, key_readable_state), "readableListening")) {
            js_set_key_default(self, key_paused, js_bool_item(true));
            return js_bool_item(false);
        }
    }

    Item buf = js_get_key_default(self, key_buffer);
    if (get_type_id(buf) != LMD_TYPE_ARRAY) {
        buf = js_array_new(0);
        js_stream_set_readable_buffer(self, buf);
    }
    js_stream_append_readable_chunk(self, buf, chunk);
    Item flowing = js_get_key_default(self, key_flowing);
    js_stream_async_iterators_drain(self, make_js_undefined());
    if (flowing.item != 0 && it2b(flowing)) {
        js_state_set_bool(js_get_key_default(self, key_readable_state), "needReadable", false);
        js_state_set_bool(js_get_key_default(self, key_readable_state), "emittedReadable", false);
        Item transform_fn = js_get_key_default(self, make_string_item("_transform"));
        if (js_is_callable(transform_fn) &&
            js_item_is_true(js_get_key_default(self, make_string_item("_writing")))) {
            js_stream_flush_buffered_data(self);
        } else {
            // flowing streams must expose pushed chunks before unrelated async work can end the process.
            js_stream_flush_buffered_data(self);
        }
    } else if (js_state_get_bool(js_get_key_default(self, key_readable_state), "readableListening")) {
        bool defer_readable = js_stream_readable_is_object_mode(self) &&
            !js_item_is_true(js_get_key_default(self, key_reading_sync));
        if (defer_readable) {
            js_set_key_default(self, make_string_item("__defer_readable_emit__"), js_bool_item(true));
        }
        js_stream_emit_readable(self);
        if (defer_readable) {
            js_set_key_default(self, make_string_item("__defer_readable_emit__"), js_bool_item(false));
        }
    } else if (!js_item_is_true(js_get_key_default(self, key_end_pending)) &&
               js_stream_readable_accepts_more(self, buf)) {
        js_stream_schedule_read(self);
    }
    return js_bool_item(js_stream_readable_accepts_more(self, buf));
}
JS_FORWARD_ITEM(js_readable_push, (Item self, Item chunk), js_readable_push_encoded, (self, chunk, make_js_undefined()))

// unshift(chunk) — prepend data to readable stream
extern "C" Item js_readable_unshift_encoded(Item self, Item chunk, Item encoding) {
    ensure_keys();
    if (chunk.item == 0 || get_type_id(chunk) == LMD_TYPE_NULL) {
        return js_readable_push(self, chunk);
    }
    if (!js_item_is_true(js_get_key_default(self, key_readable)) ||
        js_item_is_true(js_get_key_default(self, key_end_emitted))) {
        Item err = js_stream_make_error_with_code("ERR_STREAM_PUSH_AFTER_EOF",
            "stream.unshift() after end event");
        js_stream_schedule_error(self, err);
        return js_bool_item(false);
    }
    JS_ASSIGN_OR_RETURN(preparation, js_stream_prepare_readable_chunk(self, &chunk, encoding));
    if (!js_stream_readable_is_object_mode(self) &&
        js_stream_is_empty_byte_chunk(chunk)) {
        return js_bool_item(true);
    }

    Item buf = js_get_key_default(self, key_buffer);
    if (get_type_id(buf) != LMD_TYPE_ARRAY) {
        buf = js_array_new(0);
    }
    int64_t blen = js_array_length(buf);
    Item new_buf = js_array_new(0);
    js_array_push(new_buf, chunk);
    for (int64_t i = 0; i < blen; i++) {
        js_array_push(new_buf, js_elements_get_int(buf, i));
    }
    js_stream_set_readable_buffer(self, new_buf);
    js_stream_iter_maybe_drain(self);
    if (js_item_is_true(js_get_key_default(self, key_end_pending)) &&
        !js_item_is_true(js_get_key_default(self, key_end_emitted))) {
        return js_bool_item(true);
    }
    Item flowing = js_get_key_default(self, key_flowing);
    if (flowing.item != 0 && it2b(flowing)) {
        js_state_set_bool(js_get_key_default(self, key_readable_state), "needReadable", false);
        js_state_set_bool(js_get_key_default(self, key_readable_state), "emittedReadable", false);
        js_stream_schedule_data_flush(self);
    } else if (js_state_get_bool(js_get_key_default(self, key_readable_state), "readableListening")) {
        js_stream_emit_readable(self);
    }
    return js_bool_item(js_stream_readable_accepts_more(self, new_buf));
}


static int64_t js_stream_readable_buffer_length(Item self, Item buf) {
    if (get_type_id(buf) != LMD_TYPE_ARRAY) return 0;
    int64_t total = 0;
    int64_t len = js_array_length(buf);
    for (int64_t i = 0; i < len; i++) {
        total += js_stream_readable_chunk_length(self, js_elements_get_int(buf, i));
    }
    return total;
}

static bool js_stream_readable_accepts_more(Item self, Item buf) {
    int64_t length = js_stream_readable_cached_length(self, buf);
    if (length == 0) return true;
    Item state = js_get_key_default(self, key_readable_state);
    int64_t hwm = js_stream_state_get_int(state, "highWaterMark", js_stream_default_byte_hwm);
    return length < hwm;
}

static bool js_stream_readable_buffer_backpressured(Item self) {
    Item state = js_get_key_default(self, key_readable_state);
    if (get_type_id(state) != LMD_TYPE_MAP) return false;
    Item buf = js_get_key_default(self, key_buffer);
    int64_t length = js_stream_readable_cached_length(self, buf);
    int64_t hwm = js_stream_state_get_int(state, "highWaterMark", js_stream_default_byte_hwm);
    return hwm > 0 && length >= hwm;
}

static bool js_stream_mark_transform_readable_backpressure(Item self) {
    if (!js_stream_readable_buffer_backpressured(self)) return false;
    Item state = js_get_key_default(self, key_writable_state);
    if (get_type_id(state) == LMD_TYPE_MAP) {
        js_state_set_bool(state, "needDrain", true);
    }
    return true;
}

static void js_stream_maybe_drain_transform_readable_backpressure(Item self) {
    Item state = js_get_key_default(self, key_writable_state);
    if (get_type_id(state) != LMD_TYPE_MAP) return;
    if (!js_state_get_bool(state, "needDrain")) return;
    if (js_stream_readable_buffer_backpressured(self)) return;
    if (js_state_get_bool(state, "ended") ||
        js_item_is_true(js_get_key_default(self, key_destroyed)) ||
        js_item_is_true(js_get_key_default(self, key_finish_emitted))) {
        return;
    }
    js_state_set_bool(state, "needDrain", false);
    js_stream_emit_or_schedule_drain(self);
}

static bool js_stream_readable_buffer_has_string(Item buf) {
    if (get_type_id(buf) != LMD_TYPE_ARRAY) return false;
    int64_t len = js_array_length(buf);
    for (int64_t i = 0; i < len; i++) {
        if (get_type_id(js_elements_get_int(buf, i)) == LMD_TYPE_STRING) return true;
    }
    return false;
}

static bool js_stream_collapse_exact_read_buffer(Item self, Item* buf, int64_t* blen,
                                                int64_t available, int64_t want) {
    if (!buf || !blen || *blen < 4096) return true;
    int64_t total = js_stream_readable_cached_length(self, *buf);
    if (total != available || total <= 0 || total > want) return true;
    Item collapsed = js_buffer_concat(*buf, (Item){.item = i2it(total)});
    if (item_is_error(collapsed)) return false;
    Item next_buf = js_array_new(0);
    js_array_push(next_buf, collapsed);
    js_stream_set_readable_buffer(self, next_buf);
    *buf = next_buf;
    *blen = 1;
    return true;
}

static Item js_stream_concat_decoded_chunks(Item buf, Item encoding) {
    if (get_type_id(buf) != LMD_TYPE_ARRAY) return ItemNull;
    StrBuf* sb = strbuf_new_cap(64);
    if (!sb) return ItemNull;
    int64_t len = js_array_length(buf);
    for (int64_t i = 0; i < len; i++) {
        Item chunk = js_elements_get_int(buf, i);
        Item text = chunk;
        if (get_type_id(text) != LMD_TYPE_STRING) {
            text = js_buffer_toString(chunk, encoding, make_js_undefined(), make_js_undefined());
            if (item_is_error(text)) {
                strbuf_free(sb);
                return text;
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
        available += js_stream_readable_chunk_length(self, js_elements_get_int(buf, i));
        if (available >= want) break;
    }
    if (available < want && !js_item_is_true(js_get_key_default(self, key_end_pending))) {
        Item read_fn = js_get_key_default(self, make_string_item("_read"));
        Item pull_size = (Item){.item = i2it(js_stream_read_size_hint(self, (Item){.item = i2it(want)}))};
        while (available < want && !js_item_is_true(js_get_key_default(self, key_end_pending))) {
            if (!js_is_callable(read_fn) ||
                js_item_is_true(js_get_key_default(self, key_destroyed)) ||
                js_item_is_true(js_get_key_default(self, key_reading))) {
                break;
            }
            int64_t before_len = blen;
            js_stream_set_reading(self, true);
            js_set_key_default(self, key_reading_sync, js_bool_item(true));
            Item read_result = js_call_function(read_fn, self, &pull_size, 1);
            js_set_key_default(self, key_reading_sync, js_bool_item(false));
            if (item_is_error(read_result)) {
                Item err = js_error_lane_payload(read_result);
                js_stream_set_reading(self, false);
                js_stream_destroy(self, err);
                return ItemNull;
            }
            buf = js_get_key_default(self, key_buffer);
            if (get_type_id(buf) != LMD_TYPE_ARRAY) return ItemNull;
            blen = js_array_length(buf);
            // sync _read() can satisfy one byte at a time; keep pulling until progress stops or EOF.
            if (blen > before_len || js_item_is_true(js_get_key_default(self, key_end_pending))) {
                js_stream_set_reading(self, false);
            }
            if (blen <= before_len) break;
            for (int64_t i = before_len; i < blen && available < want; i++) {
                available += js_stream_readable_chunk_length(self, js_elements_get_int(buf, i));
            }
            // sync exact reads may produce tiny chunks until EOF; collapsing
            // proven-consumed data keeps the readable buffer from becoming a
            // hundreds-thousand-entry array before the final read completes.
            if (!js_stream_collapse_exact_read_buffer(self, &buf, &blen, available, want)) {
                return ItemNull;
            }
        }
        if (available < want && !js_item_is_true(js_get_key_default(self, key_end_pending))) {
            return ItemNull;
        }
    }
    if (available == 0) return ItemNull;
    if (want > available) want = available;
    if (want == available) {
        // concat can allocate and trigger GC, so keep the chunk list rooted on the stream until after it returns.
        JS_ASSIGN_OR_RETURN(consumed, blen == 1 ? js_elements_get_int(buf, 0)
                                  : js_buffer_concat(buf, (Item){.item = i2it(want)}));
        js_stream_set_readable_buffer(self, js_array_new(0));
        if (js_item_is_true(js_get_key_default(self, key_end_pending)))
            js_stream_schedule_end(self);
        else
            js_stream_schedule_read(self);
        return consumed;
    }

    Item parts = js_array_new(0);
    Item new_buf = js_array_new(0);
    int64_t remaining = want;
    bool split = false;
    for (int64_t i = 0; i < blen; i++) {
        Item chunk = js_elements_get_int(buf, i);
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
        if (js_item_is_true(js_get_key_default(self, key_end_pending)))
            js_stream_schedule_end(self);
        else
            js_stream_schedule_read(self);
    }

    int64_t part_count = js_array_length(parts);
    if (part_count == 1) return js_elements_get_int(parts, 0);
    return js_buffer_concat(parts, (Item){.item = i2it(want)});
}

// read() — pull one chunk from buffer (non-flowing mode)
extern "C" Item js_readable_read_size(Item self, Item size_item) {
    ensure_keys();
    int64_t read_size = 0;
    bool has_read_size = js_stream_item_to_int64(size_item, &read_size);
    if (has_read_size && read_size == 0) {
        js_stream_call_read_if_needed(self, size_item);
        if (get_type_id(js_get_key_default(self, make_string_item("_encoding"))) == LMD_TYPE_STRING) {
            Item buf = js_get_key_default(self, key_buffer);
            while (!js_item_is_true(js_get_key_default(self, key_end_pending)) &&
                   !js_item_is_true(js_get_key_default(self, key_end_emitted)) &&
                   js_stream_readable_accepts_more(self, buf)) {
                int64_t before_len = get_type_id(buf) == LMD_TYPE_ARRAY ? js_array_length(buf) : 0;
                js_stream_call_read_if_needed(self, size_item);
                buf = js_get_key_default(self, key_buffer);
                int64_t after_len = get_type_id(buf) == LMD_TYPE_ARRAY ? js_array_length(buf) : 0;
                if (after_len <= before_len) break;
            }
        }
        return ItemNull;
    }
    if (js_item_is_true(js_get_key_default(self, make_string_item("__emitting_data__")))) {
        return ItemNull;
    }

    js_state_set_bool(js_get_key_default(self, key_readable_state), "emittedReadable", false);
    Item buf = js_get_key_default(self, key_buffer);
    if (get_type_id(buf) != LMD_TYPE_ARRAY || js_array_length(buf) == 0) {
        js_stream_call_read_if_needed(self, size_item);
        buf = js_get_key_default(self, key_buffer);
    }
    if (get_type_id(buf) != LMD_TYPE_ARRAY) {
        js_stream_update_need_after_read(self);
        return ItemNull;
    }
    int64_t blen = js_array_length(buf);

    if (blen == 0) {
        if (js_item_is_true(js_get_key_default(self, key_end_pending)))
            js_stream_schedule_end(self);
        js_stream_update_need_after_read(self);
        return ItemNull;
    }

    if (has_read_size && read_size > 0 &&
        !js_stream_readable_is_object_mode(self)) {
        Item exact = js_readable_read_exact(self, buf, blen, read_size);
        if (exact.item == 0 || get_type_id(exact) == LMD_TYPE_NULL ||
            get_type_id(exact) == LMD_TYPE_UNDEFINED) {
            if (js_item_is_true(js_get_key_default(self, key_end_pending)))
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

    if (blen == 0 && !js_item_is_true(js_get_key_default(self, key_end_pending))) {
        js_stream_call_read_if_needed(self, size_item);
        buf = js_get_key_default(self, key_buffer);
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

    Item encoding = js_get_key_default(self, make_string_item("_encoding"));
    if (get_type_id(encoding) == LMD_TYPE_STRING &&
        !js_stream_readable_is_object_mode(self)) {
        if (!js_item_is_true(js_get_key_default(self, key_end_pending)) &&
            !js_item_is_true(js_get_key_default(self, key_end_emitted))) {
            js_stream_call_read_if_needed(self, make_js_undefined());
            buf = js_get_key_default(self, key_buffer);
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
                   !js_item_is_true(js_get_key_default(self, key_end_pending)) &&
                   !js_item_is_true(js_get_key_default(self, key_end_emitted))) {
                int64_t before_available = available;
                js_stream_call_read_if_needed(self, make_js_undefined());
                buf = js_get_key_default(self, key_buffer);
                if (get_type_id(buf) != LMD_TYPE_ARRAY) {
                    js_stream_update_need_after_read(self);
                    return ItemNull;
                }
                blen = js_array_length(buf);
                available = js_stream_readable_buffer_length(self, buf);
                if (available <= before_available) break;
            }
            if (available < 3 &&
                !js_item_is_true(js_get_key_default(self, key_end_pending)) &&
                !js_item_is_true(js_get_key_default(self, key_end_emitted))) {
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
        JS_ASSIGN_OR_RETURN(joined, blen == 1
            ? js_elements_get_int(buf, 0)
            : (js_stream_readable_buffer_has_string(buf)
                ? js_stream_concat_decoded_chunks(buf, encoding)
                : js_buffer_concat(buf, make_js_undefined())));
        js_stream_set_readable_buffer(self, js_array_new(0));
        js_stream_iter_maybe_drain(self);
        if (js_item_is_true(js_get_key_default(self, key_end_pending)))
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
    Item result = js_elements_get_int(buf, 0);
    Item new_buf = js_array_new(0);
    for (int64_t i = 1; i < blen; i++) {
        js_array_push(new_buf, js_elements_get_int(buf, i));
    }
    js_stream_set_readable_buffer(self, new_buf);
    js_stream_iter_maybe_drain(self);
    if (js_array_length(new_buf) == 0) {
        if (js_item_is_true(js_get_key_default(self, key_end_pending)))
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
JS_FORWARD_ITEM(js_readable_read, (Item self), js_readable_read_size, (self, make_js_undefined()))

static Item js_stream_iterator_result(Item value, bool done) {
    Item result = js_new_object();
    js_set_key_default(result, make_string_item("value"), value);
    js_set_key_default(result, make_string_item("done"), js_bool_item(done));
    return result;
}
JS_FORWARD_STATIC_EXPRESSION(bool, js_stream_async_iterator_has_value, (Item value), (value.item != 0 && get_type_id(value) != LMD_TYPE_NULL && get_type_id(value) != LMD_TYPE_UNDEFINED))
JS_FORWARD_STATIC_EXPRESSION(bool, js_stream_async_iterator_stream_done, (Item stream), (!js_item_is_true(js_get_key_default(stream, key_readable)) || js_item_is_true(js_get_key_default(stream, key_end_pending)) || js_item_is_true(js_get_key_default(stream, key_end_emitted)) || js_item_is_true(js_get_key_default(stream, key_ended))))

static Item js_stream_async_iterator_pending_queue(Item iterator) {
    Item key = make_string_item("__pending_queue__");
    Item queue = js_get_key_default(iterator, key);
    if (get_type_id(queue) != LMD_TYPE_ARRAY) {
        queue = js_array_new(0);
        js_set_key_default(iterator, key, queue);
    }
    return queue;
}

static Item js_stream_array_shift_property(Item obj, Item key) {
    Item queue = js_get_key_default(obj, key);
    if (get_type_id(queue) != LMD_TYPE_ARRAY) return make_js_undefined();
    int64_t len = js_array_length(queue);
    if (len <= 0) return make_js_undefined();

    Item value = js_elements_get_int(queue, 0);
    Item next_queue = js_array_new(0);
    for (int64_t i = 1; i < len; i++) {
        js_array_push(next_queue, js_elements_get_int(queue, i));
    }
    js_set_key_default(obj, key, next_queue);
    return value;
}

static int64_t js_stream_async_iterator_pending_count(Item iterator) {
    Item queue = js_get_key_default(iterator, make_string_item("__pending_queue__"));
    if (get_type_id(queue) != LMD_TYPE_ARRAY) return 0;
    return js_array_length(queue);
}

static void js_stream_async_iterator_detach(Item iterator) {
    Item stream = js_get_key_default(iterator, make_string_item("__stream__"));
    if (get_type_id(stream) != LMD_TYPE_MAP && get_type_id(stream) != LMD_TYPE_ELEMENT) return;
    Item iterators_key = make_string_item("__async_iterators__");
    Item iterators = js_get_key_default(stream, iterators_key);
    if (get_type_id(iterators) != LMD_TYPE_ARRAY) return;

    Item next_iterators = js_array_new(0);
    int64_t len = js_array_length(iterators);
    for (int64_t i = 0; i < len; i++) {
        Item current = js_elements_get_int(iterators, i);
        if (current.item != iterator.item) {
            js_array_push(next_iterators, current);
        }
    }
    js_set_key_default(stream, iterators_key, next_iterators);
}

static Item js_stream_async_iterator_shift_pending(Item iterator) {
    Item key = make_string_item("__pending_queue__");
    Item queue = js_get_key_default(iterator, key);
    if (get_type_id(queue) != LMD_TYPE_ARRAY) return make_js_undefined();
    int64_t len = js_array_length(queue);
    if (len <= 0) return make_js_undefined();

    Item capability = js_stream_array_shift_property(iterator, key);
    js_set_key_default(iterator, make_string_item("__pending__"), js_bool_item(len > 1));
    return capability;
}

static void js_stream_async_iterator_clear_pending(Item iterator) {
    js_set_key_default(iterator, make_string_item("__pending__"), js_bool_item(false));
    js_set_key_default(iterator, make_string_item("__resolve__"), make_js_undefined());
    js_set_key_default(iterator, make_string_item("__reject__"), make_js_undefined());
    js_set_key_default(iterator, make_string_item("__pending_queue__"), js_array_new(0));
}

static void js_stream_async_iterator_settle(Item iterator, Item result, bool reject) {
    Item capability = js_stream_async_iterator_shift_pending(iterator);
    const char* method = reject ? "reject" : "resolve";
    const char* fallback = reject ? "__reject__" : "__resolve__";
    Item callback = js_get_key_default(capability, make_string_item(method));
    if (!js_is_callable(callback)) {
        callback = js_get_key_default(iterator, make_string_item(fallback));
    }
    if (reject) js_set_key_default(iterator, make_string_item("__done__"), js_bool_item(true));
    if (js_is_callable(callback)) {
        Item args[1] = { result };
        js_call_function(callback, make_js_undefined(), args, 1);
    }
}
JS_FORWARD_STATIC_VOID( js_stream_async_iterator_resolve, (Item iterator, Item result), js_stream_async_iterator_settle, (iterator, result, false))
JS_FORWARD_STATIC_VOID( js_stream_async_iterator_reject, (Item iterator, Item err), js_stream_async_iterator_settle, (iterator, err, true))

static void js_stream_async_iterator_resolve_all_done(Item iterator) {
    js_set_key_default(iterator, make_string_item("__done__"), js_bool_item(true));
    while (js_stream_async_iterator_pending_count(iterator) > 0) {
        js_stream_async_iterator_resolve(iterator,
            js_stream_iterator_result(make_js_undefined(), true));
    }
    js_stream_async_iterator_detach(iterator);
}

static void js_stream_async_iterator_reject_all(Item iterator, Item err) {
    js_set_key_default(iterator, make_string_item("__done__"), js_bool_item(true));
    while (js_stream_async_iterator_pending_count(iterator) > 0) {
        js_stream_async_iterator_reject(iterator, err);
    }
    js_stream_async_iterator_detach(iterator);
}

static bool js_stream_async_iterator_is_legacy_stream(Item stream) {
    Item state = js_get_key_default(stream, key_readable_state);
    TypeId state_type = get_type_id(state);
    if (state_type == LMD_TYPE_MAP || state_type == LMD_TYPE_ELEMENT) return false;
    if (js_item_is_true(js_get_key_default(stream, key_readable))) return false;
    return js_is_callable(js_get_key_default(stream, key_on));
}

static void js_stream_async_iterator_cleanup_stream(Item stream) {
    if (get_type_id(stream) != LMD_TYPE_MAP && get_type_id(stream) != LMD_TYPE_ELEMENT) return;
    if (js_item_is_true(js_get_key_default(stream, key_destroyed))) {
        // rejected for-await calls both the error cleanup and iterator.return();
        // only the first path may invoke a legacy public destroy hook.
        return;
    }
    Item destroy = js_get_key_default(stream, key_destroy);
    if (js_is_callable(destroy)) {
        if (!js_stream_is_native_stream(stream)) {
            // legacy destroy hooks may not update stream flags, so mark before
            // cleanup to keep pipeline's later error teardown idempotent.
            js_stream_mark_destroyed(stream);
        }
        js_call_function(destroy, stream, NULL, 0);
        return;
    }
    Item close = js_get_key_default(stream, make_string_item("close"));
    if (js_is_callable(close)) {
        if (!js_stream_is_native_stream(stream)) {
            // legacy close hooks may not update stream flags, so mark before
            // cleanup to keep pipeline's later error teardown idempotent.
            js_stream_mark_destroyed(stream);
        }
        js_call_function(close, stream, NULL, 0);
    }
}

static void js_stream_async_iterator_drain_legacy(Item iterator) {
    Item err = js_get_key_default(iterator, make_string_item("__event_error__"));
    if (js_stream_has_callback_error(err) && js_stream_async_iterator_pending_count(iterator) > 0) {
        js_stream_async_iterator_reject(iterator, err);
        js_stream_async_iterator_resolve_all_done(iterator);
        Item stream = js_get_key_default(iterator, make_string_item("__stream__"));
        js_stream_async_iterator_cleanup_stream(stream);
        return;
    }

    Item buffer_key = make_string_item("__event_buffer__");
    Item buffer = js_get_key_default(iterator, buffer_key);
    while (get_type_id(buffer) == LMD_TYPE_ARRAY &&
           js_array_length(buffer) > 0 &&
           js_stream_async_iterator_pending_count(iterator) > 0) {
        Item chunk = js_stream_array_shift_property(iterator, buffer_key);
        js_stream_async_iterator_resolve(iterator,
            js_stream_iterator_result(chunk, false));
        buffer = js_get_key_default(iterator, buffer_key);
    }
    if (js_item_is_true(js_get_key_default(iterator, make_string_item("__event_done__"))) &&
        js_stream_async_iterator_pending_count(iterator) > 0) {
        js_stream_async_iterator_resolve_all_done(iterator);
        Item stream = js_get_key_default(iterator, make_string_item("__stream__"));
        js_stream_async_iterator_cleanup_stream(stream);
    }
}

static Item js_stream_async_iterator_legacy_data(Item env_item, Item chunk) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item iterator = env[0];
    Item buffer_key = make_string_item("__event_buffer__");
    Item buffer = js_get_key_default(iterator, buffer_key);
    if (get_type_id(buffer) != LMD_TYPE_ARRAY) {
        buffer = js_array_new(0);
        js_set_key_default(iterator, buffer_key, buffer);
    }
    js_array_push(buffer, chunk);
    return make_js_undefined();
}

static Item js_stream_async_iterator_legacy_end(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item iterator = env[0];
    js_set_key_default(iterator, make_string_item("__event_done__"), js_bool_item(true));
    js_stream_async_iterator_drain_legacy(iterator);
    return make_js_undefined();
}

static Item js_stream_async_iterator_legacy_error(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item iterator = env[0];
    js_set_key_default(iterator, make_string_item("__event_error__"), err);
    js_stream_async_iterator_drain_legacy(iterator);
    return make_js_undefined();
}

static void js_stream_async_iterator_setup_legacy(Item iterator, Item stream) {
    if (js_item_is_true(js_get_key_default(iterator, make_string_item("__legacy_listening__")))) return;
    Item on = js_get_key_default(stream, key_on);
    if (!js_is_callable(on)) return;

    Item* env = js_alloc_env(1);
    env[0] = iterator;
    Item data = js_new_native_closure(js_stream_async_iterator_legacy_data, 1, env, 1);
    Item end = js_new_native_closure(js_stream_async_iterator_legacy_end, 0, env, 1);
    Item error = js_new_native_closure(js_stream_async_iterator_legacy_error, 1, env, 1);

    Item args[2] = { make_string_item("data"), data };
    js_call_function(on, stream, args, 2);
    args[0] = make_string_item("end"); args[1] = end;
    js_call_function(on, stream, args, 2);
    args[0] = make_string_item("error"); args[1] = error;
    js_call_function(on, stream, args, 2);

    js_set_key_default(iterator, make_string_item("__legacy_listening__"), js_bool_item(true));
}

static Item js_stream_async_iterator_pending_promise(Item iterator, Item stream) {
    JS_ASSIGN_OR_RETURN(capability, js_promise_with_resolvers());
    Item queue = js_stream_async_iterator_pending_queue(iterator);
    js_array_push(queue, capability);
    js_set_key_default(iterator, make_string_item("__pending__"), js_bool_item(true));
    js_set_key_default(iterator, make_string_item("__resolve__"),
                    js_get_key_default(capability, make_string_item("resolve")));
    js_set_key_default(iterator, make_string_item("__reject__"),
                    js_get_key_default(capability, make_string_item("reject")));
    if (js_stream_async_iterator_is_legacy_stream(stream)) {
        js_stream_async_iterator_setup_legacy(iterator, stream);
        js_stream_async_iterator_drain_legacy(iterator);
    } else {
        js_stream_call_read_if_needed(stream, make_js_undefined());
        js_stream_async_iterators_drain(stream, make_js_undefined());
    }
    return js_get_key_default(capability, make_string_item("promise"));
}

static Item js_stream_async_iterator_read_chunk(Item stream) {
    js_set_key_default(stream, make_string_item("__async_iterator_reading__"), js_bool_item(true));
    Item chunk = js_readable_read(stream);
    js_set_key_default(stream, make_string_item("__async_iterator_reading__"), js_bool_item(false));
    return chunk;
}

static void js_stream_async_iterators_drain(Item stream, Item err) {
    ensure_keys();
    Item iterators = js_get_key_default(stream, make_string_item("__async_iterators__"));
    if (get_type_id(iterators) != LMD_TYPE_ARRAY) return;

    bool has_error = err.item != 0 &&
                     get_type_id(err) != LMD_TYPE_UNDEFINED &&
                     get_type_id(err) != LMD_TYPE_NULL;
    int64_t len = js_array_length(iterators);
    for (int64_t i = 0; i < len; i++) {
        Item iterator = js_elements_get_int(iterators, i);
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
                if (js_item_is_true(js_get_key_default(stream, key_end_pending)) &&
                    !js_item_is_true(js_get_key_default(stream, key_end_emitted))) {
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
    if (js_item_is_true(js_get_key_default(iterator, make_string_item("__done__")))) {
        return js_promise_resolve(js_stream_iterator_result(make_js_undefined(), true));
    }
    Item stored_error = js_get_key_default(iterator, make_string_item("__error__"));
    if (js_stream_has_callback_error(stored_error)) {
        js_set_key_default(iterator, make_string_item("__done__"), js_bool_item(true));
        js_stream_async_iterator_detach(iterator);
        return js_promise_reject(stored_error);
    }

    Item stream = js_get_key_default(iterator, make_string_item("__stream__"));
    TypeId stream_tid = get_type_id(stream);
    if (stream_tid != LMD_TYPE_MAP && stream_tid != LMD_TYPE_ELEMENT) {
        js_set_key_default(iterator, make_string_item("__done__"), js_bool_item(true));
        return js_promise_resolve(js_stream_iterator_result(make_js_undefined(), true));
    }
    stored_error = js_get_key_default(stream, make_string_item("__error__"));
    if (js_stream_has_callback_error(stored_error)) {
        js_set_key_default(iterator, make_string_item("__done__"), js_bool_item(true));
        js_stream_async_iterator_detach(iterator);
        return js_promise_reject(stored_error);
    }
    if (js_item_is_true(js_get_key_default(stream, make_string_item("__iter_failed__")))) {
        js_set_key_default(iterator, make_string_item("__done__"), js_bool_item(true));
        js_stream_async_iterator_detach(iterator);
        return js_promise_reject(js_get_key_default(stream, make_string_item("__iter_error__")));
    }
    if (js_item_is_true(js_get_key_default(stream, key_destroyed)) &&
        !js_item_is_true(js_get_key_default(stream, key_end_pending)) &&
        !js_item_is_true(js_get_key_default(stream, key_end_emitted)) &&
        !js_item_is_true(js_get_key_default(stream, key_ended))) {
        if (js_stream_destroy_pending(stream)) {
            // async _destroy(cb) can still supply the terminal error; rejecting
            // now would hide the callback error from iterators created mid-destroy.
            return js_stream_async_iterator_pending_promise(iterator, stream);
        }
        js_set_key_default(iterator, make_string_item("__done__"), js_bool_item(true));
        js_stream_async_iterator_detach(iterator);
        return js_promise_reject(js_stream_make_error_with_code("ERR_STREAM_PREMATURE_CLOSE",
            "Premature close"));
    }

    if (js_stream_async_iterator_is_legacy_stream(stream)) {
        js_stream_async_iterator_setup_legacy(iterator, stream);
        Item event_error = js_get_key_default(iterator, make_string_item("__event_error__"));
        if (js_stream_has_callback_error(event_error)) {
            js_set_key_default(iterator, make_string_item("__done__"), js_bool_item(true));
            js_stream_async_iterator_detach(iterator);
            js_stream_async_iterator_cleanup_stream(stream);
            return js_promise_reject(event_error);
        }
        Item buffer = js_get_key_default(iterator, make_string_item("__event_buffer__"));
        if (get_type_id(buffer) == LMD_TYPE_ARRAY && js_array_length(buffer) > 0) {
            Item chunk = js_stream_array_shift_property(iterator, make_string_item("__event_buffer__"));
            return js_promise_resolve(js_stream_iterator_result(chunk, false));
        }
        if (js_item_is_true(js_get_key_default(iterator, make_string_item("__event_done__")))) {
            js_set_key_default(iterator, make_string_item("__done__"), js_bool_item(true));
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
    stored_error = js_get_key_default(stream, make_string_item("__error__"));
    if (js_stream_has_callback_error(stored_error)) {
        js_set_key_default(iterator, make_string_item("__done__"), js_bool_item(true));
        js_stream_async_iterator_detach(iterator);
        return js_promise_reject(stored_error);
    }

    bool readable_done = js_stream_async_iterator_stream_done(stream);
    if (!readable_done) {
        js_stream_call_read_if_needed(stream, make_js_undefined());
        stored_error = js_get_key_default(stream, make_string_item("__error__"));
        if (js_stream_has_callback_error(stored_error)) {
            js_set_key_default(iterator, make_string_item("__done__"), js_bool_item(true));
            js_stream_async_iterator_detach(iterator);
            return js_promise_reject(stored_error);
        }
        chunk = js_stream_async_iterator_read_chunk(stream);
        if (js_stream_async_iterator_has_value(chunk)) {
            return js_promise_resolve(js_stream_iterator_result(chunk, false));
        }
        return js_stream_async_iterator_pending_promise(iterator, stream);
    }

    js_set_key_default(iterator, make_string_item("__done__"), js_bool_item(true));
    if (js_item_is_true(js_get_key_default(stream, key_end_pending)) &&
        !js_item_is_true(js_get_key_default(stream, key_end_emitted))) {
        js_stream_emit_end_tick(stream);
    }
    js_stream_async_iterator_detach(iterator);
    return js_promise_resolve(js_stream_iterator_result(make_js_undefined(), true));
}
JS_FORWARD_STATIC_ITEM(js_stream_async_iterator_inst_next, (void), js_stream_async_iterator_next, (js_get_this()))
JS_FORWARD_STATIC_ITEM(js_stream_iterator_identity, (void), js_get_this, ())

static Item js_stream_async_iterator_inst_return(void) {
    Item iterator = js_get_this();
    js_set_key_default(iterator, make_string_item("__done__"), js_bool_item(true));
    js_stream_async_iterator_resolve_all_done(iterator);
    js_stream_async_iterator_clear_pending(iterator);

    Item stream = js_get_key_default(iterator, make_string_item("__stream__"));
    Item writer = js_get_key_default(stream, make_string_item("__iter_writer__"));
    if (get_type_id(writer) == LMD_TYPE_MAP || get_type_id(writer) == LMD_TYPE_ELEMENT) {
        js_stream_iter_resolve_drain(writer, js_bool_item(false));
        js_stream_iter_reject_pending_writes(writer,
            js_stream_make_error_with_code("ERR_INVALID_STATE", "WritableStream is closed"));
    }
    if (js_item_is_true(js_get_key_default(iterator, make_string_item("__destroy_on_return__")))) {
        js_stream_async_iterator_cleanup_stream(stream);
    }
    js_stream_async_iterator_detach(iterator);
    return js_promise_resolve(js_stream_iterator_result(make_js_undefined(), true));
}

static Item js_stream_async_iterator_inst_throw(Item err) {
    Item iterator = js_get_this();
    Item stream = js_get_key_default(iterator, make_string_item("__stream__"));
    js_set_key_default(iterator, make_string_item("__done__"), js_bool_item(true));
    js_stream_async_iterator_reject_all(iterator, err);
    if (get_type_id(stream) == LMD_TYPE_MAP || get_type_id(stream) == LMD_TYPE_ELEMENT) {
        js_set_key_default(stream, make_string_item("__iter_failed__"), js_bool_item(true));
        js_set_key_default(stream, make_string_item("__iter_error__"), err);
        js_set_key_default(stream, key_destroyed, js_bool_item(true));
        js_set_key_default(stream, make_string_item("destroyed"), js_bool_item(true));
        Item writer = js_get_key_default(stream, make_string_item("__iter_writer__"));
        if (get_type_id(writer) == LMD_TYPE_MAP || get_type_id(writer) == LMD_TYPE_ELEMENT) {
            js_set_key_default(writer, make_string_item("__error__"), err);
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
    js_set_key_default(iterator, make_string_item("__stream__"), self);
    js_set_key_default(iterator, make_string_item("stream"), self);
    js_set_key_default(iterator, make_string_item("__done__"), js_bool_item(false));
    js_set_key_default(iterator, make_string_item("__pending__"), js_bool_item(false));
    js_set_key_default(iterator, make_string_item("__pending_queue__"), js_array_new(0));
    js_set_key_default(iterator, make_string_item("__event_buffer__"), js_array_new(0));
    js_set_key_default(iterator, make_string_item("__event_done__"), js_bool_item(false));
    js_set_key_default(iterator, make_string_item("__legacy_listening__"), js_bool_item(false));
    js_set_key_default(iterator, make_string_item("__destroy_on_return__"), js_bool_item(true));
    js_set_key_default(iterator, make_string_item("__error__"),
                    js_get_key_default(self, make_string_item("__error__")));
    js_set_native_key(iterator, make_string_item("next"), js_stream_async_iterator_inst_next);
    js_set_native_key(iterator, make_string_item("return"), js_stream_async_iterator_inst_return);
    js_set_native_key(iterator, make_string_item("throw"), js_stream_async_iterator_inst_throw);

    Item iterators = js_get_key_default(self, make_string_item("__async_iterators__"));
    if (get_type_id(iterators) != LMD_TYPE_ARRAY) {
        iterators = js_array_new(0);
        js_set_key_default(self, make_string_item("__async_iterators__"), iterators);
    }
    js_array_push(iterators, iterator);

    Item identity_fn = js_new_native_function(js_stream_iterator_identity);
    Item async_key = js_well_known_symbol_key(5);
    Item iter_key = js_well_known_symbol_key(1);
    js_set_key_default(iterator, async_key, identity_fn);
    js_set_key_default(iterator, iter_key, identity_fn);
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
        Item destroy_on_return = js_get_key_default(options, make_string_item("destroyOnReturn"));
        if (get_type_id(destroy_on_return) == LMD_TYPE_BOOL && !it2b(destroy_on_return)) {
            js_set_key_default(iterator, make_string_item("__destroy_on_return__"), js_bool_item(false));
        }
    }
    return iterator;
}

static Item js_stream_reject_with_error(Item reject, Item err) {
    if (js_is_callable(reject)) {
        Item args[1] = { err };
        js_call_function(reject, make_js_undefined(), args, 1);
    }
    return make_js_undefined();
}

static Item js_stream_make_callback_options(Item signal) {
    Item options = js_new_object();
    if (signal.item != 0 && get_type_id(signal) != LMD_TYPE_UNDEFINED) {
        js_set_key_default(options, make_string_item("signal"), signal);
    }
    return options;
}

static Item js_stream_collect_next(Item env_item);

static Item js_stream_collect_reject(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    env[5] = js_bool_item(true);
    return js_stream_reject_with_error(env[2], err);
}

static Item js_stream_collect_step(Item env_item, Item result) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    if (js_item_is_true(env[5])) return make_js_undefined();
    Item done = js_iterator_result_done(result);
    if (item_is_error(done)) return js_stream_reject_with_error(env[2], done);
    if (js_is_truthy(done)) {
        Item resolve = env[1];
        Item values = env[3];
        env[5] = js_bool_item(true);
        if (js_is_callable(resolve)) {
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
    if (item_is_error(step)) return js_stream_reject_with_error(env[2], step);
    Item on_step = js_new_native_closure(js_stream_collect_step, 1, env, 7);
    Item on_error = js_new_native_closure(js_stream_collect_reject, 1, env, 7);
    js_promise_then(step, on_step, on_error);
    return make_js_undefined();
}

static Item js_stream_collect_abort(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[5])) return make_js_undefined();
    env[5] = js_bool_item(true);
    Item signal = env[6];
    Item reason = js_get_key_default(signal, make_string_item("reason"));
    if (reason.item == 0 || get_type_id(reason) == LMD_TYPE_UNDEFINED)
        reason = js_stream_iter_make_abort_error();
    js_stream_destroy(env[4], reason);
    Item reject = env[2];
    if (js_is_callable(reject)) {
        Item args[1] = { reason };
        js_call_function(reject, make_js_undefined(), args, 1);
    }
    return make_js_undefined();
}

static Item js_readable_toArray_validate_options(Item options) {
    TypeId tid = get_type_id(options);
    if (options.item == 0 || tid == LMD_TYPE_UNDEFINED || tid == LMD_TYPE_NULL)
        return js_bool_item(true);
    if (tid != LMD_TYPE_MAP && tid != LMD_TYPE_ELEMENT) {
        return js_throw_invalid_arg_type("options", "object", options);
    }
    JS_ASSIGN_OR_RETURN(signal, js_get_key_default(options, make_string_item("signal")));
    TypeId signal_tid = get_type_id(signal);
    if (signal.item == 0 || signal_tid == LMD_TYPE_UNDEFINED) return js_bool_item(true);
    if (!js_stream_is_abort_signal(signal)) {
        return js_throw_invalid_arg_type("options.signal", "AbortSignal", signal);
    }
    return js_bool_item(true);
}

static Item js_readable_toArray(Item readable, Item options) {
    Item options_result = js_readable_toArray_validate_options(options);
    if (item_is_error(options_result))
        return js_promise_reject(js_error_lane_payload(options_result));
    JS_ASSIGN_OR_RETURN(capability, js_promise_with_resolvers());
    Item* env = js_alloc_env(7);
    env[0] = js_stream_async_iterator(readable);
    env[1] = js_get_key_default(capability, make_string_item("resolve"));
    env[2] = js_get_key_default(capability, make_string_item("reject"));
    env[3] = js_array_new(0);
    env[4] = readable;
    env[5] = js_bool_item(false);
    env[6] = make_js_undefined();
    if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_ELEMENT) {
        Item signal = js_get_key_default(options, make_string_item("signal"));
        if (js_stream_is_abort_signal(signal)) {
            Item aborted = js_get_key_default(signal, make_string_item("aborted"));
            if (get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted)) {
                Item reason = js_get_key_default(signal, make_string_item("reason"));
                if (reason.item == 0 || get_type_id(reason) == LMD_TYPE_UNDEFINED)
                    reason = js_stream_iter_make_abort_error();
                js_stream_destroy(readable, reason);
                return js_promise_reject(reason);
            }
            Item add_event = js_get_key_default(signal, make_string_item("addEventListener"));
            if (js_is_callable(add_event)) {
                env[6] = signal;
                Item listener = js_new_native_closure(js_stream_collect_abort, 0, env, 7);
                Item args[2] = { make_string_item("abort"), listener };
                js_call_function(add_event, signal, args, 2);
            }
        }
    }
    js_stream_collect_next((Item){.item = (uint64_t)(uintptr_t)env});
    return js_get_key_default(capability, make_string_item("promise"));
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
    if (js_item_is_true(js_get_key_default(out, key_destroyed))) return make_js_undefined();

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
    if (js_item_is_true(js_get_key_default(out, key_destroyed))) return make_js_undefined();
    Item done = js_iterator_result_done(result);
    if (item_is_error(done)) {
        Item err = done;
        js_stream_destroy(out, err);
        return make_js_undefined();
    }
    if (js_is_truthy(done)) {
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
    if (item_is_error(mapped)) {
        Item err = mapped;
        js_stream_destroy(out, err);
        return make_js_undefined();
    }
    Item mapped_promise = js_promise_resolve(mapped);
    Item on_value = js_new_native_closure(js_readable_transform_value, 1, env, 6);
    Item on_error = js_new_native_closure(js_readable_transform_fail, 1, env, 6);
    js_promise_then(mapped_promise, on_value, on_error);
    return make_js_undefined();
}

static Item js_readable_transform_pump(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item out = env[1];
    if (js_item_is_true(js_get_key_default(out, key_destroyed))) return make_js_undefined();
    Item step = js_stream_async_iterator_next(env[0]);
    if (item_is_error(step)) {
        Item err = step;
        js_stream_destroy(out, err);
        return make_js_undefined();
    }
    Item on_step = js_new_native_closure(js_readable_transform_step, 1, env, 6);
    Item on_error = js_new_native_closure(js_readable_transform_fail, 1, env, 6);
    js_promise_then(step, on_step, on_error);
    return make_js_undefined();
}

static Item js_stream_validate_helper_fn(Item fn) {
    if (js_is_callable(fn)) return js_bool_item(true);
    return js_throw_invalid_arg_type("fn", "function", fn);
}

static Item js_stream_validate_helper_options(Item options) {
    TypeId tid = get_type_id(options);
    if (options.item == 0 || tid == LMD_TYPE_UNDEFINED || tid == LMD_TYPE_NULL ||
        tid == LMD_TYPE_MAP || tid == LMD_TYPE_ELEMENT) {
        return js_bool_item(true);
    }
    return js_throw_invalid_arg_type("options", "object", options);
}

static Item js_stream_validate_helper_signal(Item options) {
    TypeId tid = get_type_id(options);
    if (tid != LMD_TYPE_MAP && tid != LMD_TYPE_ELEMENT) return js_bool_item(true);
    JS_ASSIGN_OR_RETURN(signal, js_get_key_default(options, make_string_item("signal")));
    TypeId signal_tid = get_type_id(signal);
    if (signal.item == 0 || signal_tid == LMD_TYPE_UNDEFINED || signal_tid == LMD_TYPE_NULL)
        return js_bool_item(true);
    if (js_stream_is_abort_signal(signal)) return js_bool_item(true);
    return js_throw_invalid_arg_type("options.signal", "AbortSignal", signal);
}

static Item js_stream_validate_concurrency(Item options) {
    if (get_type_id(options) != LMD_TYPE_MAP && get_type_id(options) != LMD_TYPE_ELEMENT)
        return js_bool_item(true);
    JS_ASSIGN_OR_RETURN(concurrency, js_get_key_default(options, make_string_item("concurrency")));
    TypeId tid = get_type_id(concurrency);
    if (concurrency.item == 0 || tid == LMD_TYPE_UNDEFINED) return js_bool_item(true);
    if (tid != LMD_TYPE_INT || it2i(concurrency) < 1) {
        return js_throw_error_with_code("ERR_OUT_OF_RANGE",
            "The value of \"options.concurrency\" is out of range.");
    }
    return js_bool_item(true);
}

static Item js_readable_transform_helper(Item readable, Item fn, Item options, int64_t mode) {
    JS_ASSIGN_OR_RETURN(validation, js_stream_validate_helper_fn(fn));
    JS_ASSIGN_OR_RETURN_INTO(validation, js_stream_validate_helper_options(options));
    JS_ASSIGN_OR_RETURN_INTO(validation, js_stream_validate_helper_signal(options));
    JS_ASSIGN_OR_RETURN_INTO(validation, js_stream_validate_concurrency(options));

    Item opts = js_new_object();
    js_set_key_default(opts, make_string_item("objectMode"), js_bool_item(true));
    Item out = js_readable_new(opts);
    Item signal = make_js_undefined();
    if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_ELEMENT) {
        signal = js_get_key_default(options, make_string_item("signal"));
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

#define JS_READABLE_TRANSFORM_HELPER(name, mode) \
static Item name(Item readable, Item fn, Item options) { \
    return js_readable_transform_helper(readable, fn, options, mode); \
}
JS_READABLE_TRANSFORM_HELPER(js_readable_map, 0)
JS_READABLE_TRANSFORM_HELPER(js_readable_filter, 1)
#undef JS_READABLE_TRANSFORM_HELPER

static Item js_readable_forEach_next(Item env_item);

static Item js_readable_async_helper_fail(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item reject = env[2];
    if (js_is_callable(reject)) {
        Item args[1] = { err };
        js_call_function(reject, make_js_undefined(), args, 1);
    }
    return make_js_undefined();
}

static Item js_readable_validate_helper(Item fn, Item options,
        bool validate_concurrency, Item* signal_out) {
    Item validation = js_stream_validate_helper_fn(fn);
    if (item_is_error(validation)) return validation;
    validation = js_stream_validate_helper_options(options);
    if (item_is_error(validation)) return validation;
    validation = js_stream_validate_helper_signal(options);
    if (item_is_error(validation)) return validation;
    if (validate_concurrency) {
        validation = js_stream_validate_concurrency(options);
        if (item_is_error(validation)) return validation;
    }
    *signal_out = make_js_undefined();
    if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_ELEMENT)
        *signal_out = js_get_key_default(options, make_string_item("signal"));
    return ItemNull;
}

static Item js_readable_forEach_continue(Item env_item, Item ignored) {
    (void)ignored;
    return js_readable_forEach_next(env_item);
}

static Item js_readable_forEach_step(Item env_item, Item result) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item done = js_iterator_result_done(result);
    if (item_is_error(done)) return js_stream_reject_with_error(env[2], done);
    if (js_is_truthy(done)) {
        Item resolve = env[1];
        if (js_is_callable(resolve))
            js_call_function(resolve, make_js_undefined(), NULL, 0);
        return make_js_undefined();
    }
    Item chunk = js_iterator_result_value(result);
    Item options = js_stream_make_callback_options(env[4]);
    Item args[2] = { chunk, options };
    Item call_result = js_call_function(env[3], make_js_undefined(), args, 2);
    if (item_is_error(call_result)) return js_stream_reject_with_error(env[2], call_result);
    Item promise = js_promise_resolve(call_result);
    Item on_done = js_new_native_closure(js_readable_forEach_continue, 1, env, 5);
    Item on_error = js_new_native_closure(js_readable_async_helper_fail, 1, env, 5);
    js_promise_then(promise, on_done, on_error);
    return make_js_undefined();
}

static Item js_readable_forEach_next(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item step = js_stream_async_iterator_next(env[0]);
    if (item_is_error(step)) return js_stream_reject_with_error(env[2], step);
    Item on_step = js_new_native_closure(js_readable_forEach_step, 1, env, 5);
    Item on_error = js_new_native_closure(js_readable_async_helper_fail, 1, env, 5);
    js_promise_then(step, on_step, on_error);
    return make_js_undefined();
}

static Item js_readable_forEach(Item readable, Item fn, Item options) {
    Item signal = make_js_undefined();
    Item validation = js_readable_validate_helper(fn, options, true, &signal);
    if (item_is_error(validation)) return js_promise_reject(js_error_lane_payload(validation));
    JS_ASSIGN_OR_RETURN(capability, js_promise_with_resolvers());
    Item* env = js_alloc_env(5);
    env[0] = js_stream_async_iterator(readable);
    env[1] = js_get_key_default(capability, make_string_item("resolve"));
    env[2] = js_get_key_default(capability, make_string_item("reject"));
    env[3] = fn;
    env[4] = signal;
    js_readable_forEach_next((Item){.item = (uint64_t)(uintptr_t)env});
    return js_get_key_default(capability, make_string_item("promise"));
}

static Item js_readable_reduce_next(Item env_item);

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
    Item done = js_iterator_result_done(result);
    if (item_is_error(done)) return js_stream_reject_with_error(env[2], done);
    if (js_is_truthy(done)) {
        Item resolve = env[1];
        Item value = js_item_is_true(env[5]) ? env[4] : make_js_undefined();
        if (js_is_callable(resolve)) {
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
    if (item_is_error(call_result)) return js_stream_reject_with_error(env[2], call_result);
    Item promise = js_promise_resolve(call_result);
    Item on_done = js_new_native_closure(js_readable_reduce_continue, 1, env, 7);
    Item on_error = js_new_native_closure(js_readable_async_helper_fail, 1, env, 7);
    js_promise_then(promise, on_done, on_error);
    return make_js_undefined();
}

static Item js_readable_reduce_next(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item step = js_stream_async_iterator_next(env[0]);
    if (item_is_error(step)) return js_stream_reject_with_error(env[2], step);
    Item on_step = js_new_native_closure(js_readable_reduce_step, 1, env, 7);
    Item on_error = js_new_native_closure(js_readable_async_helper_fail, 1, env, 7);
    js_promise_then(step, on_step, on_error);
    return make_js_undefined();
}

static Item js_readable_reduce(Item readable, Item fn, Item initial, Item options) {
    Item signal = make_js_undefined();
    Item validation = js_readable_validate_helper(fn, options, false, &signal);
    if (item_is_error(validation)) return js_promise_reject(js_error_lane_payload(validation));
    JS_ASSIGN_OR_RETURN(capability, js_promise_with_resolvers());
    Item* env = js_alloc_env(7);
    env[0] = js_stream_async_iterator(readable);
    env[1] = js_get_key_default(capability, make_string_item("resolve"));
    env[2] = js_get_key_default(capability, make_string_item("reject"));
    env[3] = fn;
    env[4] = initial;
    env[5] = js_bool_item(get_type_id(initial) != LMD_TYPE_UNDEFINED);
    env[6] = signal;
    js_readable_reduce_next((Item){.item = (uint64_t)(uintptr_t)env});
    return js_get_key_default(capability, make_string_item("promise"));
}

static bool js_readable_compose_is_duplex_like(Item stream) {
    JsClass cls = js_class_id(stream);
    if (cls == JS_CLASS_DUPLEX || cls == JS_CLASS_TRANSFORM ||
        cls == JS_CLASS_PASS_THROUGH) {
        return true;
    }
    Item readable_state = js_get_key_default(stream, key_readable_state);
    Item writable_state = js_get_key_default(stream, key_writable_state);
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
    Item listener = js_new_native_closure(js_readable_compose_forward_error, 1, env, 1);
    js_stream_on(source, make_string_item("error"), listener);
}

static Item js_readable_compose_bridge_write(Item env_item, Item chunk, Item encoding, Item callback) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item source = env[0];
    Item input = env[2];
    Item write_fn = js_get_key_default(source, key_write);
    Item result = make_js_undefined();
    if (js_is_callable(write_fn)) {
        Item args[3] = { chunk, encoding, callback };
        JS_ASSIGN_OR_RETURN_INTO(result, js_call_function(write_fn, source, args, 3));
    }
    if (input.item != source.item &&
        (get_type_id(input) == LMD_TYPE_MAP || get_type_id(input) == LMD_TYPE_ELEMENT)) {
        Item input_write = js_get_key_default(input, key_write);
        if (js_is_callable(input_write)) {
            Item input_args[3] = { chunk, encoding, make_js_undefined() };
            JS_ASSIGN_OR_RETURN(input_result, js_call_function(input_write, input, input_args, 3));
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
    Item end_fn = js_get_key_default(source, key_end);
    if (js_is_callable(end_fn)) {
        Item args[2] = { chunk, callback };
        JS_ASSIGN_OR_RETURN(end_result, js_call_function(end_fn, source, args, 2));
    }
    if (input.item != source.item &&
        (get_type_id(input) == LMD_TYPE_MAP || get_type_id(input) == LMD_TYPE_ELEMENT)) {
        Item input_end = js_get_key_default(input, key_end);
        if (js_is_callable(input_end)) {
            Item input_args[2] = { chunk, make_js_undefined() };
            JS_ASSIGN_OR_RETURN(input_result, js_call_function(input_end, input, input_args, 2));
        }
    }
    js_readable_compose_bridge_start(env);
    return env[1];
}

static Item js_readable_compose_bridge_flush(Item env_item, const char* method) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item source = env[0];
    Item callback = js_get_key_default(source, make_string_item(method));
    if (js_is_callable(callback)) js_call_function(callback, source, NULL, 0);
    return env[1];
}
JS_FORWARD_STATIC_ITEM(js_readable_compose_bridge_cork, (Item env_item), js_readable_compose_bridge_flush, (env_item, "cork"))
JS_FORWARD_STATIC_ITEM(js_readable_compose_bridge_uncork, (Item env_item), js_readable_compose_bridge_flush, (env_item, "uncork"))

static Item js_readable_compose_bridge_destroy(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item source = env[0];
    Item destroy_fn = js_get_key_default(source, key_destroy);
    if (js_is_callable(destroy_fn)) {
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
    if (!js_is_callable(transform)) return;

    Item args[1] = { input };
    Item composed = js_call_function(transform, make_js_undefined(), args, 1);
    if (item_is_error(composed)) {
        Item err = composed;
        js_stream_destroy(out, err);
        return;
    }

    Item iterator = js_get_async_iterator(composed);
    if (item_is_error(iterator)) {
        Item err = iterator;
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

    js_set_key_default(out, key_writable, js_bool_item(true));
    js_set_key_default(out, key_writable_state, js_get_key_default(source, key_writable_state));
    js_set_key_default(out, key_write,
                    js_new_native_closure(js_readable_compose_bridge_write, 3, env, 5));
    js_set_key_default(out, key_end,
                    js_new_native_closure(js_readable_compose_bridge_end, 2, env, 5));
    js_set_key_default(out, make_string_item("cork"),
                    js_new_native_closure(js_readable_compose_bridge_cork, 0, env, 5));
    js_set_key_default(out, make_string_item("uncork"),
                    js_new_native_closure(js_readable_compose_bridge_uncork, 0, env, 5));
    js_set_key_default(out, key_destroy,
                    js_new_native_closure(js_readable_compose_bridge_destroy, 1, env, 5));
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
    if (js_item_is_true(js_get_key_default(readable, key_destroyed))) return make_js_undefined();
    if (js_readable_compose_result_source_failed(env)) return make_js_undefined();

    Item done = js_iterator_result_done(result);
    if (item_is_error(done)) {
        Item err = done;
        js_stream_destroy(readable, err);
        return make_js_undefined();
    }
    if (js_is_truthy(done)) {
        js_readable_push(readable, ItemNull);
        return make_js_undefined();
    }
    if (item_is_error(result)) {
        Item err = result;
        js_stream_destroy(readable, err);
        return make_js_undefined();
    }
    if (js_readable_compose_result_source_failed(env)) return make_js_undefined();

    Item value = js_iterator_result_value(result);
    if (item_is_error(value)) {
        Item err = value;
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

static Item js_readable_compose_result_pump(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item readable = env[0];
    if (js_item_is_true(js_get_key_default(readable, key_destroyed))) return make_js_undefined();
    if (js_readable_compose_result_source_failed(env)) return make_js_undefined();

    Item step = js_async_iterator_step_result(env[1]);
    if (item_is_error(step)) {
        Item err = step;
        js_stream_destroy(readable, err);
        return make_js_undefined();
    }

    Item on_step = js_new_native_closure(js_readable_compose_result_on_step, 1, env, 3);
    Item on_error = js_new_native_closure(js_readable_compose_forward_error, 1, env, 3);
    js_promise_then(step, on_step, on_error);
    return make_js_undefined();
}

static Item js_readable_compose_from_result(Item source, Item composed, Item options) {
    Item opts = js_new_object();
    js_set_key_default(opts, make_string_item("objectMode"), js_bool_item(true));
    Item out = js_readable_new(opts);
    if (get_type_id(out) != LMD_TYPE_MAP && get_type_id(out) != LMD_TYPE_ELEMENT) return out;

    js_stream_set_readable_object_mode(out, true);
    js_set_key_default(out, key_writable, js_bool_item(false));
    js_readable_compose_attach_error_forward(source, out);
    if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_ELEMENT)
        js_stream_iter_attach_abort(options, out);

    TypeId composed_type = get_type_id(composed);
    if (composed_type == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(composed);
        for (int64_t i = 0; i < len; i++) {
            Item value = js_elements_get_int(composed, i);
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
        if (item_is_error(iterator)) {
            Item err = iterator;
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
    JS_ASSIGN_OR_RETURN(options_result, js_stream_validate_helper_options(options));
    JS_ASSIGN_OR_RETURN(signal_result, js_stream_validate_helper_signal(options));

    TypeId stream_type = get_type_id(stream);
    if (stream.item == 0 || stream_type == LMD_TYPE_UNDEFINED) {
        return js_throw_invalid_arg_type("stream", "function or stream", stream);
    }

    if (stream_type == LMD_TYPE_FUNC) {
        if (js_readable_compose_is_duplex_like(self)) {
            Item input_opts = js_new_object();
            js_set_key_default(input_opts, make_string_item("objectMode"), js_bool_item(true));
            JS_ASSIGN_OR_RETURN(input, js_passthrough_new(input_opts));
            Item out_opts = js_new_object();
            js_set_key_default(out_opts, make_string_item("objectMode"), js_bool_item(true));
            Item out = js_readable_new(out_opts);
            if (get_type_id(out) == LMD_TYPE_MAP || get_type_id(out) == LMD_TYPE_ELEMENT) {
                js_stream_set_readable_object_mode(out, true);
                js_set_key_default(out, key_writable, js_bool_item(false));
                js_readable_compose_attach_error_forward(self, out);
                js_readable_compose_attach_writable_bridge(out, self, input, stream);
                if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_ELEMENT)
                    js_stream_iter_attach_abort(options, out);
            }
            return out;
        }
        Item args[1] = { self };
        JS_ASSIGN_OR_RETURN(composed, js_call_function(stream, make_js_undefined(), args, 1));

        return js_readable_compose_from_result(self, composed, options);
    }

    if (js_readable_compose_is_duplex_like(stream)) {
        if (!js_readable_compose_is_duplex_like(self)) {
            js_set_key_default(stream, key_writable, js_bool_item(false));
        }
        Item* env = js_alloc_env(2);
        env[0] = self;
        env[1] = stream;
        js_next_tick_enqueue(js_new_native_closure(js_readable_compose_pipe_tick, 0, env, 2));
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
        Item chunk = js_elements_get_int(chunks, i);
        Item part = chunk;
        if (!js_stream_consumer_chunk_is_byte_input(part)) {
            if (!stringify_other) {
                return js_throw_invalid_arg_type("chunk", "string, Buffer, ArrayBuffer, or ArrayBufferView", part);
            }
            JS_ASSIGN_OR_RETURN_INTO(part, js_to_string(part));
        }
        if (!js_stream_chunk_is_buffer(part)) {
            JS_ASSIGN_OR_RETURN_INTO(part, js_buffer_from(part, make_string_item("utf8"), make_js_undefined()));
        }
        js_array_push(parts, part);
    }
    return js_buffer_concat(parts, make_js_undefined());
}

static Item js_stream_consumer_buffer_finish(Item chunks) {
    JS_ASSIGN_OR_RETURN(buf, js_stream_consumer_buffer_from_array(chunks, true));
    return buf;
}

static Item js_stream_consumer_text_finish(Item chunks) {
    JS_ASSIGN_OR_RETURN(buf, js_stream_consumer_buffer_from_array(chunks, false));
    return js_buffer_toString(buf, make_string_item("utf8"),
                              make_js_undefined(), make_js_undefined());
}

static Item js_stream_consumer_json_finish(Item chunks) {
    JS_ASSIGN_OR_RETURN(text, js_stream_consumer_text_finish(chunks));
    return js_json_parse(text);
}

static Item js_stream_consumer_arrayBuffer_finish(Item chunks) {
    JS_ASSIGN_OR_RETURN(buf, js_stream_consumer_buffer_from_array(chunks, true));
    if (!js_is_typed_array(buf)) return js_get_key_default(buf, make_string_item("buffer"));
    int byte_length = js_typed_array_byte_length(buf);
    if (byte_length < 0) byte_length = 0;
    Item array_buffer = js_arraybuffer_new(byte_length);
    JsArrayBuffer* ab = js_get_arraybuffer_ptr_item(array_buffer);
    void* src = js_typed_array_current_data_ptr(buf);
    uint8_t* destination = js_arraybuffer_prepare_write(ab);
    if (destination && src && byte_length > 0) {
        memcpy(destination, src, (size_t)byte_length);
    }
    return array_buffer;
}

static Item js_stream_consumer_bytes_finish(Item chunks) {
    JS_ASSIGN_OR_RETURN(array_buffer, js_stream_consumer_arrayBuffer_finish(chunks));
    Item byte_length = js_get_key_default(array_buffer, make_string_item("byteLength"));
    int length = get_type_id(byte_length) == LMD_TYPE_INT ? (int)it2i(byte_length) : 0;
    return js_typed_array_new_from_buffer(JS_TYPED_UINT8, array_buffer, 0, length);
}

static Item js_stream_consumer_blob_finish(Item chunks) {
    JS_ASSIGN_OR_RETURN(buf, js_stream_consumer_buffer_from_array(chunks, true));
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
    if (js_item_is_true(js_get_key_default(readable, make_string_item("__web_readable__")))) {
        if (js_item_is_true(js_get_key_default(readable, make_string_item("__web_disturbed__")))) {
            return js_promise_reject(js_stream_make_error_with_code("ERR_INVALID_STATE",
                "ReadableStream is locked or disturbed"));
        }
        js_set_key_default(readable, make_string_item("__web_disturbed__"), js_bool_item(true));
    }
    Item promise = js_readable_toArray(readable, make_js_undefined());
    Item* env = js_alloc_env(1);
    env[0] = (Item){.item = i2it(mode)};
    Item finish = js_new_native_closure(js_stream_consumer_finish_value, 1, env, 1);
    return js_promise_then(promise, finish, make_js_undefined());
}

#define JS_STREAM_CONSUMER(name, mode) \
static Item name(Item readable) { \
    return js_stream_consumer(readable, mode); \
}
JS_STREAM_CONSUMER(js_stream_consumer_text, 0)
JS_STREAM_CONSUMER(js_stream_consumer_arrayBuffer, 1)
JS_STREAM_CONSUMER(js_stream_consumer_buffer, 2)
JS_STREAM_CONSUMER(js_stream_consumer_bytes, 3)
JS_STREAM_CONSUMER(js_stream_consumer_json, 4)
JS_STREAM_CONSUMER(js_stream_consumer_blob, 5)
#undef JS_STREAM_CONSUMER

static Item js_stream_iter_to_readable(Item source) {
    if (js_stream_is_stream_like(source)) return source;
    return js_readable_from(source);
}
JS_FORWARD_STATIC_ITEM(js_stream_iter_identity, (void), js_get_this, ())

static bool js_stream_iter_has_method(Item value, Item key) {
    TypeId tid = get_type_id(value);
    if (tid != LMD_TYPE_MAP && tid != LMD_TYPE_ELEMENT && tid != LMD_TYPE_ARRAY)
        return false;
    Item method = js_get_key_default(value, key);
    return js_is_callable(method);
}

static Item js_stream_iter_apply_protocol(Item* source, const char* symbol_name) {
    JS_ASSIGN_OR_RETURN(key, js_symbol_for(make_string_item(symbol_name)));
    JS_ASSIGN_OR_RETURN(method, js_get_key_default(*source, key));
    if (!js_is_callable(method)) return js_status_ok();
    JS_ASSIGN_OR_RETURN(next, js_call_function(method, *source, NULL, 0));
    *source = next;
    return js_status_ok();
}

static bool js_stream_iter_source_is_single_chunk(Item source) {
    TypeId tid = get_type_id(source);
    return tid == LMD_TYPE_STRING || js_is_arraybuffer(source) ||
           js_stream_chunk_is_arraybuffer_view(source) ||
           js_stream_chunk_is_buffer(source);
}

static Item js_stream_iter_append_normalized(Item batch, Item value);

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

static Item js_stream_iter_append_array_values(Item batch, Item array) {
    int64_t len = js_array_length(array);
    for (int64_t i = 0; i < len; i++) {
        JS_ASSIGN_OR_RETURN(value, js_elements_get_int(array, i));
        JS_ASSIGN_OR_RETURN(status, js_stream_iter_append_normalized(batch, value));
    }
    return js_status_ok();
}

static Item js_stream_iter_append_normalized(Item batch, Item value) {
    JS_ASSIGN_OR_RETURN(status, js_stream_iter_apply_protocol(&value, "Stream.toStreamable"));

    if (get_type_id(value) == LMD_TYPE_ARRAY)
        return js_stream_iter_append_array_values(batch, value);

    Item chunk = js_stream_iter_to_byte_chunk(value);
    if (chunk.item != 0 && get_type_id(chunk) != LMD_TYPE_NULL) {
        if (item_is_error(chunk)) return chunk;
        JS_ASSIGN_OR_RETURN(push_result, js_array_push(batch, chunk));
        return js_status_ok();
    }

    return js_throw_invalid_arg_type("chunk", "string, Buffer, ArrayBuffer, or ArrayBufferView", value);
}

static Item js_stream_iter_batch_next(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[2]))
        return js_stream_iterator_result(make_js_undefined(), true);

    Item batch = js_array_new(0);
    if (js_item_is_true(env[3])) {
        env[2] = js_bool_item(true);
        JS_RETURN_IF_ERROR(js_stream_iter_append_normalized(batch, env[0]));
        return js_stream_iterator_result(batch, false);
    }

    bool collect_all = js_item_is_true(env[4]);
    while (collect_all || js_array_length(batch) == 0) {
        JS_ASSIGN_OR_RETURN(value, js_iterator_step(env[1]));
        if (value.item == JS_ITER_DONE_SENTINEL) {
            env[2] = js_bool_item(true);
            break;
        }
        JS_RETURN_IF_ERROR(js_stream_iter_append_normalized(batch, value));
    }

    if (js_array_length(batch) == 0)
        return js_stream_iterator_result(make_js_undefined(), true);
    return js_stream_iterator_result(batch, false);
}

static Item js_stream_iter_make_batch_iterable(Item source, bool async_iterable, bool collect_all_sync) {
    Item iterator = make_js_undefined();
    bool single = js_stream_iter_source_is_single_chunk(source);
    if (!single) {
        JS_ASSIGN_OR_RETURN_INTO(iterator, js_get_iterator(source));
    }

    Item* env = js_alloc_env(5);
    env[0] = source;
    env[1] = iterator;
    env[2] = js_bool_item(false);
    env[3] = js_bool_item(single);
    env[4] = js_bool_item(get_type_id(source) == LMD_TYPE_ARRAY || collect_all_sync);

    Item obj = js_new_object();
    js_set_key_default(obj, make_string_item("next"), js_new_native_closure(js_stream_iter_batch_next, 0, env, 5));
    js_set_native_key(obj, js_well_known_symbol_key(1), js_stream_iter_identity);
    if (async_iterable)
        js_set_native_key(obj, js_well_known_symbol_key(5), js_stream_iter_identity);
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
        return js_stream_iter_has_method(source, js_well_known_symbol_key(1));
    return false;
}

static Item js_stream_iter_from(Item source) {
    JS_RETURN_IF_ERROR(js_stream_iter_apply_protocol(&source, "Stream.toAsyncStreamable"));
    if (js_stream_iter_has_method(source, js_well_known_symbol_key(5)) &&
        !js_stream_iter_has_method(source, js_well_known_symbol_key(1)))
        return js_stream_iter_to_readable(source);
    JS_RETURN_IF_ERROR(js_stream_iter_apply_protocol(&source, "Stream.toStreamable"));
    if (js_stream_iter_value_can_sync(source))
        return js_stream_iter_make_batch_iterable(source, true, false);
    return js_stream_iter_to_readable(source);
}

static Item js_stream_iter_fromSync(Item source) {
    JS_RETURN_IF_ERROR(js_stream_iter_apply_protocol(&source, "Stream.toStreamable"));
    if (!js_stream_iter_value_can_sync(source)) {
        return js_throw_invalid_arg_type("source", "a sync streamable value", source);
    }
    return js_stream_iter_make_batch_iterable(source, false, true);
}

static bool js_stream_iter_is_byte_array(Item value) {
    if (get_type_id(value) != LMD_TYPE_ARRAY) return false;
    int64_t len = js_array_length(value);
    for (int64_t i = 0; i < len; i++) {
        if (get_type_id(js_elements_get_int(value, i)) != LMD_TYPE_INT) return false;
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

    JS_ASSIGN_OR_RETURN(iterator, js_get_iterator(source));
    while (true) {
        JS_ASSIGN_OR_RETURN(value, js_iterator_step(iterator));
        if (value.item == JS_ITER_DONE_SENTINEL) break;
        js_array_push(chunks, value);
    }
    return chunks;
}

static Item js_stream_iter_flatten_for_bytes(Item chunks) {
    Item flat = js_array_new(0);
    int64_t len = get_type_id(chunks) == LMD_TYPE_ARRAY ? js_array_length(chunks) : 0;
    for (int64_t i = 0; i < len; i++) {
        Item chunk = js_elements_get_int(chunks, i);
        if (get_type_id(chunk) == LMD_TYPE_ARRAY && !js_stream_iter_is_byte_array(chunk)) {
            int64_t inner_len = js_array_length(chunk);
            for (int64_t j = 0; j < inner_len; j++) {
                js_array_push(flat, js_elements_get_int(chunk, j));
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
    Item limit = js_get_key_default(options, make_string_item("limit"));
    if (get_type_id(limit) != LMD_TYPE_INT) return make_js_undefined();
    int64_t total = 0;
    int64_t len = get_type_id(chunks) == LMD_TYPE_ARRAY ? js_array_length(chunks) : 0;
    for (int64_t i = 0; i < len; i++) {
        total += js_stream_iter_chunk_byte_length(js_elements_get_int(chunks, i));
        if (total > it2i(limit)) {
            Item err = js_stream_make_error_with_code("ERR_OUT_OF_RANGE", "stream/iter limit exceeded");
            js_set_key_default(err, make_string_item("name"), make_string_item("RangeError"));
            return err;
        }
    }
    return make_js_undefined();
}

static Item js_stream_iter_sync_consumer(Item source, Item options,
        JsNativeP1 finish, bool flatten) {
    JS_ASSIGN_OR_RETURN(chunks, js_stream_iter_sync_array(source));
    if (flatten) chunks = js_stream_iter_flatten_for_bytes(chunks);
    Item err = js_stream_iter_check_limit(chunks, options);
    if (js_stream_has_error(err)) return js_throw_value(err);
    return finish ? finish(chunks) : chunks;
}

#define JS_STREAM_ITER_SYNC_CONSUMER(name, finish, flatten) \
static Item name(Item source, Item options) { \
    return js_stream_iter_sync_consumer(source, options, finish, flatten); \
}
JS_STREAM_ITER_SYNC_CONSUMER(js_stream_iter_textSync, js_stream_consumer_text_finish, true)
JS_STREAM_ITER_SYNC_CONSUMER(js_stream_iter_bytesSync, js_stream_consumer_bytes_finish, true)
JS_STREAM_ITER_SYNC_CONSUMER(js_stream_iter_arrayBufferSync, js_stream_consumer_arrayBuffer_finish, true)
JS_STREAM_ITER_SYNC_CONSUMER(js_stream_iter_arraySync, nullptr, false)
#undef JS_STREAM_ITER_SYNC_CONSUMER

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
        Item signal = js_get_key_default(options, make_string_item("signal"));
        if (js_stream_is_abort_signal(signal)) {
            Item aborted = js_get_key_default(signal, make_string_item("aborted"));
            if (get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted)) {
                Item reason = js_get_key_default(signal, make_string_item("reason"));
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
    Item finish = js_new_native_closure(js_stream_iter_consumer_done, 1, env, 2);
    return js_promise_then(promise, finish, make_js_undefined());
}

#define JS_STREAM_ITER_CONSUMER(name, mode) \
static Item name(Item source, Item options) { \
    return js_stream_iter_consumer_async(source, options, mode); \
}
JS_STREAM_ITER_CONSUMER(js_stream_iter_text_consume, 0)
JS_STREAM_ITER_CONSUMER(js_stream_iter_bytes, 1)
JS_STREAM_ITER_CONSUMER(js_stream_iter_arrayBuffer, 2)
JS_STREAM_ITER_CONSUMER(js_stream_iter_array, 3)
#undef JS_STREAM_ITER_CONSUMER

static Item js_stream_iter_tap_callback(Item env_item, Item chunks) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return chunks;
    Item callback = env[0];
    JS_ASSIGN_OR_RETURN(result, js_call_function(callback, make_js_undefined(), &chunks, 1));
    (void)result;
    return chunks;
}
JS_FORWARD_STATIC_EXPRESSION(Item, js_stream_iter_tap_async_done, (Item chunks), (chunks))

static Item js_stream_iter_tap_async_callback(Item env_item, Item chunks) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return js_promise_resolve(chunks);
    Item callback = env[0];
    JS_ASSIGN_OR_RETURN(result, js_call_function(callback, make_js_undefined(), &chunks, 1));
    Item promise = js_promise_resolve(result);
    Item done = js_bind_function(js_new_native_function(js_stream_iter_tap_async_done),
                                 make_js_undefined(), &chunks, 1);
    return js_promise_then(promise, done, make_js_undefined());
}

static Item js_stream_iter_tap_make(Item callback, JsNativeP2 target) {
    if (!js_is_callable(callback))
        return js_throw_invalid_arg_type("fn", "function", callback);
    Item* env = js_alloc_env(1);
    env[0] = callback;
    return js_new_native_closure(target, 1, env, 1);
}
JS_FORWARD_STATIC_ITEM(js_stream_iter_tapSync, (Item callback), js_stream_iter_tap_make, (callback, js_stream_iter_tap_callback))
JS_FORWARD_STATIC_ITEM(js_stream_iter_tap, (Item callback), js_stream_iter_tap_make, (callback, js_stream_iter_tap_async_callback))

static bool js_stream_iter_is_transform_object(Item transform, Item* method) {
    TypeId tid = get_type_id(transform);
    if (tid != LMD_TYPE_MAP && tid != LMD_TYPE_ELEMENT) return false;
    Item fn = js_get_key_default(transform, make_string_item("transform"));
    if (!js_is_callable(fn)) return false;
    if (method) *method = fn;
    return true;
}
JS_FORWARD_STATIC_EXPRESSION(bool, js_stream_iter_transform_is_present, (Item transform), (transform.item != 0 && get_type_id(transform) != LMD_TYPE_UNDEFINED))

static Item js_stream_iter_validate_transform(Item transform) {
    if (!js_stream_iter_transform_is_present(transform)) return js_status_ok();
    if (js_is_callable(transform)) return js_status_ok();
    if (js_stream_iter_is_transform_object(transform, NULL)) return js_status_ok();
    return js_throw_invalid_arg_type("transform", "function or transform object", transform);
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
        js_array_push(input, js_elements_get_int(chunks, i));
    }
    js_array_push(input, ItemNull);
    return input;
}

static Item js_stream_iter_apply_stateless_transform(Item chunks, Item transform) {
    Item output = js_array_new(0);
    int64_t len = get_type_id(chunks) == LMD_TYPE_ARRAY ? js_array_length(chunks) : 0;
    for (int64_t i = 0; i < len; i++) {
        Item batch = js_elements_get_int(chunks, i);
        JS_ASSIGN_OR_RETURN(result, js_call_function(transform, make_js_undefined(), &batch, 1));
        js_stream_iter_append_transform_value(output, result);
    }
    Item flush = ItemNull;
    JS_ASSIGN_OR_RETURN(flush_result, js_call_function(transform, make_js_undefined(), &flush, 1));
    js_stream_iter_append_transform_value(output, flush_result);
    return output;
}

static Item js_stream_iter_apply_stateful_transform(Item chunks, Item transform_obj, Item method) {
    Item input = js_stream_iter_transform_input(chunks);
    JS_ASSIGN_OR_RETURN(result, js_call_function(method, transform_obj, &input, 1));
    JS_ASSIGN_OR_RETURN(iterator, js_get_iterator(result));
    Item output = js_array_new(0);
    while (true) {
        JS_ASSIGN_OR_RETURN(value, js_iterator_step(iterator));
        if (value.item == JS_ITER_DONE_SENTINEL) break;
        js_stream_iter_append_transform_value(output, value);
    }
    return output;
}

static Item js_stream_iter_apply_sync_transform(Item chunks, Item transform) {
    if (!js_stream_iter_transform_is_present(transform)) return chunks;
    JS_RETURN_IF_ERROR(js_stream_iter_validate_transform(transform));
    if (js_is_callable(transform))
        return js_stream_iter_apply_stateless_transform(chunks, transform);
    Item method = make_js_undefined();
    if (js_stream_iter_is_transform_object(transform, &method))
        return js_stream_iter_apply_stateful_transform(chunks, transform, method);
    return chunks;
}

static Item js_stream_iter_pullSync(Item source, Item transform1, Item transform2, Item transform3,
                                    Item transform4, Item transform5, Item transform6, Item transform7) {
    JS_ASSIGN_OR_RETURN(chunks, js_stream_iter_sync_array(source));
    Item transforms[7] = { transform1, transform2, transform3, transform4,
                           transform5, transform6, transform7 };
    for (int i = 0; i < 7; i++) {
        JS_ASSIGN_OR_RETURN_INTO(chunks, js_stream_iter_apply_sync_transform(chunks, transforms[i]));
    }
    return chunks;
}

static Item js_stream_iter_pull_transform_done(Item env_item, Item chunks) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return chunks;
    Item transform = env[0];
    if (!js_is_callable(transform)) return chunks;
    return js_call_function(transform, make_js_undefined(), &chunks, 1);
}

static Item js_stream_iter_pull(Item source, Item transform) {
    Item readable = js_stream_iter_to_readable(source);
    Item promise = js_readable_toArray(readable, make_js_undefined());
    Item* env = js_alloc_env(1);
    env[0] = transform;
    Item done = js_new_native_closure(js_stream_iter_pull_transform_done, 1, env, 1);
    return js_promise_then(promise, done, make_js_undefined());
}

static int64_t js_stream_iter_hwm(Item options) {
    Item hwm = js_get_key_default(options, make_string_item("highWaterMark"));
    if (get_type_id(hwm) == LMD_TYPE_INT && it2i(hwm) >= 0) return it2i(hwm);
    return 16;
}

static bool js_stream_iter_closed(Item writer) {
    if (js_item_is_true(js_get_key_default(writer, make_string_item("__closed__")))) return true;
    Item readable = js_get_key_default(writer, make_string_item("__readable__"));
    return js_item_is_true(js_get_key_default(readable, key_destroyed));
}

static int64_t js_stream_iter_desired_size_value(Item writer) {
    if (js_stream_iter_closed(writer)) return INT64_MIN;
    Item hwm = js_get_key_default(writer, make_string_item("__hwm__"));
    int64_t hwm_int = get_type_id(hwm) == LMD_TYPE_INT ? it2i(hwm) : 16;
    Item readable = js_get_key_default(writer, make_string_item("__readable__"));
    Item buf = js_get_key_default(readable, key_buffer);
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
    Item buf = js_get_key_default(readable, key_buffer);
    return get_type_id(buf) != LMD_TYPE_ARRAY || js_array_length(buf) == 0;
}

static Item js_stream_iter_writer_total(Item writer) {
    Item total = js_get_key_default(writer, make_string_item("__total__"));
    if (get_type_id(total) == LMD_TYPE_INT) return total;
    return (Item){.item = i2it(0)};
}

static int64_t js_stream_iter_chunk_byte_length(Item chunk) {
    if (get_type_id(chunk) == LMD_TYPE_STRING) {
        String* str = it2s(chunk);
        return str ? (int64_t)str->len : 0;
    }
    Item byte_length = js_get_key_default(chunk, make_string_item("byteLength"));
    if (get_type_id(byte_length) == LMD_TYPE_INT) return it2i(byte_length);
    Item length = js_get_key_default(chunk, make_string_item("length"));
    if (get_type_id(length) == LMD_TYPE_INT) return it2i(length);
    return 0;
}

static void js_stream_iter_settle_capability(Item writer, const char* capability_name,
        const char* method_name, Item value) {
    Item capability = js_get_key_default(writer, make_string_item(capability_name));
    if (get_type_id(capability) != LMD_TYPE_MAP && get_type_id(capability) != LMD_TYPE_ELEMENT) return;
    js_set_key_default(writer, make_string_item(capability_name), make_js_undefined());
    Item settle = js_get_key_default(capability, make_string_item(method_name));
    if (js_is_callable(settle)) {
        Item args[1] = { value };
        js_call_function(settle, make_js_undefined(), args, 1);
    }
}
JS_FORWARD_STATIC_VOID( js_stream_iter_resolve_drain, (Item writer, Item value), js_stream_iter_settle_capability, (writer, "__drain__", "resolve", value))
JS_FORWARD_STATIC_VOID( js_stream_iter_reject_drain, (Item writer, Item err), js_stream_iter_settle_capability, (writer, "__drain__", "reject", err))

static void js_stream_iter_resolve_end_if_drained(Item writer) {
    Item capability = js_get_key_default(writer, make_string_item("__end__"));
    if (get_type_id(capability) != LMD_TYPE_MAP && get_type_id(capability) != LMD_TYPE_ELEMENT) return;
    Item readable = js_get_key_default(writer, make_string_item("__readable__"));
    if (!js_stream_iter_readable_buffer_empty(readable)) return;
    js_stream_iter_settle_capability(writer, "__end__", "resolve",
        js_stream_iter_writer_total(writer));
}
JS_FORWARD_STATIC_VOID( js_stream_iter_reject_end, (Item writer, Item err), js_stream_iter_settle_capability, (writer, "__end__", "reject", err))

static void js_stream_iter_reject_pending_writes(Item writer, Item err) {
    Item pending = js_get_key_default(writer, make_string_item("__pending_writes__"));
    if (get_type_id(pending) != LMD_TYPE_ARRAY) return;
    int64_t len = js_array_length(pending);
    for (int64_t i = 0; i < len; i++) {
        Item capability = js_elements_get_int(pending, i);
        Item reject = js_get_key_default(capability, make_string_item("reject"));
        if (js_is_callable(reject)) {
            Item args[1] = { err };
            js_call_function(reject, make_js_undefined(), args, 1);
        }
    }
    js_set_key_default(writer, make_string_item("__pending_writes__"), js_array_new(0));
}

static void js_stream_iter_maybe_drain(Item readable) {
    Item writer = js_get_key_default(readable, make_string_item("__iter_writer__"));
    if (get_type_id(writer) != LMD_TYPE_MAP && get_type_id(writer) != LMD_TYPE_ELEMENT) return;
    int64_t desired = js_stream_iter_desired_size_value(writer);
    if (desired > 0) js_stream_iter_resolve_drain(writer, js_bool_item(true));
    js_stream_iter_resolve_end_if_drained(writer);
}

static Item js_stream_iter_make_abort_error(void) {
    Item err = js_stream_make_error_with_code("ABORT_ERR", "The operation was aborted");
    js_set_key_default(err, make_string_item("name"), make_string_item("AbortError"));
    return err;
}

static Item js_stream_iter_writer_emit(Item writer, Item chunk) {
    if (js_stream_iter_closed(writer)) return js_bool_item(false);
    Item readable = js_get_key_default(writer, make_string_item("__readable__"));
    Item transform = js_get_key_default(writer, make_string_item("__transform__"));
    if (js_is_callable(transform)) {
        Item input = js_array_new(0);
        Item transformed_chunk = chunk;
        if (get_type_id(chunk) == LMD_TYPE_STRING) {
            transformed_chunk = js_buffer_from(chunk, make_string_item("utf8"), make_js_undefined());
        }
        js_array_push(input, transformed_chunk);
        Item result = js_call_function(transform, make_js_undefined(), &input, 1);
        if (item_is_error(result)) {
            Item err = js_error_lane_payload(result);
            js_stream_destroy(readable, err);
            return js_bool_item(false);
        }
        if (get_type_id(result) == LMD_TYPE_ARRAY) {
            int64_t len = js_array_length(result);
            for (int64_t i = 0; i < len; i++) {
                js_readable_push(readable, js_elements_get_int(result, i));
            }
        } else if (result.item == 0 || get_type_id(result) == LMD_TYPE_NULL) {
            js_readable_push(readable, ItemNull);
            js_set_key_default(writer, make_string_item("__closed__"), js_bool_item(true));
        } else {
            js_readable_push(readable, result);
        }
    } else {
        js_readable_push(readable, chunk);
    }
    Item total = js_get_key_default(writer, make_string_item("__total__"));
    int64_t total_int = get_type_id(total) == LMD_TYPE_INT ? it2i(total) : 0;
    total_int += js_stream_iter_chunk_byte_length(chunk);
    js_set_key_default(writer, make_string_item("__total__"), (Item){.item = i2it(total_int)});
    return js_bool_item(!js_stream_iter_closed(writer));
}

static bool js_stream_iter_signal_aborted(Item options, Item* reason_out) {
    if (get_type_id(options) != LMD_TYPE_MAP && get_type_id(options) != LMD_TYPE_ELEMENT) return false;
    Item signal = js_get_key_default(options, make_string_item("signal"));
    if (get_type_id(signal) != LMD_TYPE_MAP && get_type_id(signal) != LMD_TYPE_ELEMENT) return false;
    Item aborted = js_get_key_default(signal, make_string_item("aborted"));
    if (get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted)) {
        Item reason = js_get_key_default(signal, make_string_item("reason"));
        if (reason.item == 0 || get_type_id(reason) == LMD_TYPE_UNDEFINED) reason = js_stream_iter_make_abort_error();
        *reason_out = reason;
        return true;
    }
    return false;
}

static Item js_stream_iter_pipe_next(Item env_item);

static Item js_stream_iter_pipe_settle(Item env_item, Item value, bool reject) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[6])) return make_js_undefined();
    env[6] = js_bool_item(true);
    Item callback = env[reject ? 5 : 4];
    if (js_is_callable(callback)) {
        Item args[1] = { value };
        js_call_function(callback, make_js_undefined(), args, 1);
    }
    return make_js_undefined();
}
JS_FORWARD_STATIC_ITEM(js_stream_iter_pipe_reject, (Item env_item, Item err), js_stream_iter_pipe_settle, (env_item, err, true))
JS_FORWARD_STATIC_ITEM(js_stream_iter_pipe_resolve, (Item env_item, Item value), js_stream_iter_pipe_settle, (env_item, value, false))

static Item js_stream_iter_pipe_after_write(Item env_item, Item ignored) {
    (void)ignored;
    return js_stream_iter_pipe_next(env_item);
}
JS_FORWARD_STATIC_ITEM(js_stream_iter_pipe_finish, (Item env_item, Item result), js_stream_iter_pipe_resolve, (env_item, result))

static Item js_stream_iter_pipe_end(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[6])) return make_js_undefined();
    Item writer = env[1];
    Item end_sync = js_get_key_default(writer, make_string_item("endSync"));
    if (js_is_callable(end_sync)) {
        Item result = js_call_function(end_sync, writer, NULL, 0);
        if (item_is_error(result)) {
            Item err = js_error_lane_payload(result);
            return js_stream_iter_pipe_reject(env_item, err);
        }
        return js_stream_iter_pipe_resolve(env_item, result);
    }
    Item end_fn = js_get_key_default(writer, make_string_item("end"));
    if (js_is_callable(end_fn)) {
        Item result = js_call_function(end_fn, writer, NULL, 0);
        if (item_is_error(result)) {
            Item err = js_error_lane_payload(result);
            return js_stream_iter_pipe_reject(env_item, err);
        }
        Item promise = js_promise_resolve(result);
        Item on_done = js_new_native_closure(js_stream_iter_pipe_finish, 1, env, 7);
        Item on_error = js_new_native_closure(js_stream_iter_pipe_reject, 1, env, 7);
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
    if (js_is_callable(transform)) {
        chunk = js_call_function(transform, make_js_undefined(), &chunk, 1);
        if (item_is_error(chunk)) {
            Item thrown = chunk;
            return js_stream_iter_pipe_reject(env_item, thrown);
        }
    }

    Item writer = env[1];
    Item write_sync = js_get_key_default(writer, make_string_item("writeSync"));
    if (js_is_callable(write_sync)) {
        Item write_sync_result = js_call_function(write_sync, writer, &chunk, 1);
        if (item_is_error(write_sync_result)) {
            Item thrown = write_sync_result;
            return js_stream_iter_pipe_reject(env_item, thrown);
        }
        return js_stream_iter_pipe_next(env_item);
    }
    Item write_fn = js_get_key_default(writer, make_string_item("write"));
    if (js_is_callable(write_fn)) {
        Item result = js_call_function(write_fn, writer, &chunk, 1);
        if (item_is_error(result)) {
            Item thrown = result;
            return js_stream_iter_pipe_reject(env_item, thrown);
        }
        Item promise = js_promise_resolve(result);
        Item on_done = js_new_native_closure(js_stream_iter_pipe_after_write, 1, env, 7);
        Item on_error = js_new_native_closure(js_stream_iter_pipe_reject, 1, env, 7);
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
    Item done = js_iterator_result_done(result);
    if (item_is_error(done)) return js_stream_iter_pipe_reject(env_item, done);
    if (js_is_truthy(done)) return js_stream_iter_pipe_end(env_item);
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
    if (item_is_error(step)) {
        Item thrown = step;
        return js_stream_iter_pipe_reject(env_item, thrown);
    }
    step = js_promise_resolve(step);
    Item on_step = js_new_native_closure(js_stream_iter_pipe_step, 1, env, 7);
    Item on_error = js_new_native_closure(js_stream_iter_pipe_reject, 1, env, 7);
    js_promise_then(step, on_step, on_error);
    return make_js_undefined();
}

static Item js_stream_iter_pipe_abort(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[6])) return make_js_undefined();
    Item signal = env[7];
    Item reason = js_get_key_default(signal, make_string_item("reason"));
    if (reason.item == 0 || get_type_id(reason) == LMD_TYPE_UNDEFINED) reason = js_stream_iter_make_abort_error();
    return js_stream_iter_pipe_reject(env_item, reason);
}

static void js_stream_iter_pipe_attach_abort(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return;
    Item options = env[3];
    if (get_type_id(options) != LMD_TYPE_MAP && get_type_id(options) != LMD_TYPE_ELEMENT) return;
    Item signal = js_get_key_default(options, make_string_item("signal"));
    if (get_type_id(signal) != LMD_TYPE_MAP && get_type_id(signal) != LMD_TYPE_ELEMENT) return;
    env[7] = signal;
    Item add_event = js_get_key_default(signal, make_string_item("addEventListener"));
    if (!js_is_callable(add_event)) return;
    Item listener = js_new_native_closure(js_stream_iter_pipe_abort, 0, env, 8);
    Item args[2] = { make_string_item("abort"), listener };
    js_call_function(add_event, signal, args, 2);
}

static Item js_stream_iter_pipeTo(Item source, Item transform_or_writer, Item writer_or_options, Item maybe_options) {
    Item transform = make_js_undefined();
    Item writer = transform_or_writer;
    Item options = writer_or_options;
    if (js_is_callable(transform_or_writer) &&
        (get_type_id(maybe_options) == LMD_TYPE_MAP || get_type_id(maybe_options) == LMD_TYPE_ELEMENT)) {
        transform = transform_or_writer;
        writer = writer_or_options;
        options = maybe_options;
    }

    JS_ASSIGN_OR_RETURN(capability, js_promise_with_resolvers());
    Item promise = js_get_key_default(capability, make_string_item("promise"));
    Item err = make_js_undefined();
    if (js_stream_iter_signal_aborted(options, &err)) return js_promise_reject(err);

    Item iterator = js_get_async_iterator(source);
    if (item_is_error(iterator)) return js_promise_reject(iterator);

    Item* env = js_alloc_env(8);
    env[0] = iterator;
    env[1] = writer;
    env[2] = transform;
    env[3] = options;
    env[4] = js_get_key_default(capability, make_string_item("resolve"));
    env[5] = js_get_key_default(capability, make_string_item("reject"));
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
    Item reason = js_get_key_default(signal, make_string_item("reason"));
    if (reason.item == 0 || get_type_id(reason) == LMD_TYPE_UNDEFINED) reason = js_stream_iter_make_abort_error();
    Item reject = js_get_key_default(capability, make_string_item("reject"));
    if (js_is_callable(reject)) {
        Item args[1] = { reason };
        js_call_function(reject, make_js_undefined(), args, 1);
    }
    return make_js_undefined();
}

static void js_stream_iter_attach_pending_abort(Item options, Item capability) {
    if (get_type_id(options) != LMD_TYPE_MAP && get_type_id(options) != LMD_TYPE_ELEMENT) return;
    Item signal = js_get_key_default(options, make_string_item("signal"));
    if (get_type_id(signal) != LMD_TYPE_MAP && get_type_id(signal) != LMD_TYPE_ELEMENT) return;
    Item add_event = js_get_key_default(signal, make_string_item("addEventListener"));
    if (!js_is_callable(add_event)) return;
    Item* env = js_alloc_env(2);
    env[0] = signal;
    env[1] = capability;
    Item listener = js_new_native_closure(js_stream_iter_pending_write_abort, 0, env, 2);
    Item args[2] = { make_string_item("abort"), listener };
    js_call_function(add_event, signal, args, 2);
}

static Item js_stream_iter_writer_write(Item chunk, Item options) {
    Item writer = js_get_this();
    Item err = make_js_undefined();
    if (js_stream_iter_signal_aborted(options, &err)) return js_promise_reject(err);
    if (js_stream_iter_closed(writer)) {
        Item stored = js_get_key_default(writer, make_string_item("__error__"));
        if (stored.item != 0 && get_type_id(stored) != LMD_TYPE_UNDEFINED) return js_promise_reject(stored);
        return js_promise_reject(js_stream_make_error_with_code("ERR_INVALID_STATE",
            "WritableStream is closed"));
    }
    if (js_stream_iter_desired_size_value(writer) <= 0) {
        Item capability = js_promise_with_resolvers();
        Item pending = js_get_key_default(writer, make_string_item("__pending_writes__"));
        if (get_type_id(pending) != LMD_TYPE_ARRAY) {
            pending = js_array_new(0);
            js_set_key_default(writer, make_string_item("__pending_writes__"), pending);
        }
        js_array_push(pending, capability);
        js_stream_iter_attach_pending_abort(options, capability);
        return js_get_key_default(capability, make_string_item("promise"));
    }
    js_stream_iter_writer_emit(writer, chunk);
    return js_promise_resolve(js_bool_item(true));
}
JS_FORWARD_STATIC_ITEM(js_stream_iter_writer_writeSync, (Item chunk), js_stream_iter_writer_emit, (js_get_this(), chunk))

static Item js_stream_iter_writer_writev(Item chunks) {
    Item writer = js_get_this();
    if (get_type_id(chunks) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(chunks);
        for (int64_t i = 0; i < len; i++)
            js_stream_iter_writer_emit(writer, js_elements_get_int(chunks, i));
    }
    return js_promise_resolve(js_bool_item(true));
}

static Item js_stream_iter_writer_writevSync(Item chunks) {
    Item writer = js_get_this();
    if (get_type_id(chunks) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(chunks);
        for (int64_t i = 0; i < len; i++)
            js_stream_iter_writer_emit(writer, js_elements_get_int(chunks, i));
    }
    return js_bool_item(!js_stream_iter_closed(writer));
}

static Item js_stream_iter_writer_end(void) {
    Item writer = js_get_this();
    Item stored = js_get_key_default(writer, make_string_item("__error__"));
    if (stored.item != 0 && get_type_id(stored) != LMD_TYPE_UNDEFINED) return js_promise_reject(stored);
    if (!js_stream_iter_closed(writer)) {
        Item readable = js_get_key_default(writer, make_string_item("__readable__"));
        js_readable_push(readable, ItemNull);
        js_set_key_default(writer, make_string_item("__closed__"), js_bool_item(true));
    }
    js_stream_iter_reject_pending_writes(writer,
        js_stream_make_error_with_code("ERR_INVALID_STATE", "WritableStream is closed"));
    Item existing = js_get_key_default(writer, make_string_item("__end__"));
    if (get_type_id(existing) == LMD_TYPE_MAP || get_type_id(existing) == LMD_TYPE_ELEMENT) {
        return js_get_key_default(existing, make_string_item("promise"));
    }
    Item readable = js_get_key_default(writer, make_string_item("__readable__"));
    if (js_stream_iter_readable_buffer_empty(readable)) {
        return js_promise_resolve(js_stream_iter_writer_total(writer));
    }
    Item capability = js_promise_with_resolvers();
    js_set_key_default(writer, make_string_item("__end__"), capability);
    return js_get_key_default(capability, make_string_item("promise"));
}

static Item js_stream_iter_writer_endSync(void) {
    Item writer = js_get_this();
    if (!js_stream_iter_closed(writer)) {
        Item readable = js_get_key_default(writer, make_string_item("__readable__"));
        js_readable_push(readable, ItemNull);
        js_set_key_default(writer, make_string_item("__closed__"), js_bool_item(true));
    }
    js_stream_iter_reject_pending_writes(writer,
        js_stream_make_error_with_code("ERR_INVALID_STATE", "WritableStream is closed"));
    Item readable = js_get_key_default(writer, make_string_item("__readable__"));
    if (js_stream_iter_readable_buffer_empty(readable)) {
        return js_stream_iter_writer_total(writer);
    }
    return (Item){.item = i2it(-1)};
}

static Item js_stream_iter_writer_fail(Item err) {
    Item writer = js_get_this();
    Item readable = js_get_key_default(writer, make_string_item("__readable__"));
    js_set_key_default(writer, make_string_item("__closed__"), js_bool_item(true));
    js_set_key_default(writer, make_string_item("__error__"), err);
    js_set_key_default(readable, make_string_item("__iter_failed__"), js_bool_item(true));
    js_set_key_default(readable, make_string_item("__iter_error__"), err);
    Item iterators = js_get_key_default(readable, make_string_item("__async_iterators__"));
    if (get_type_id(iterators) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(iterators);
        for (int64_t i = 0; i < len; i++) {
            js_stream_async_iterator_reject_all(js_elements_get_int(iterators, i), err);
        }
    }
    if (js_stream_has_callback_error(err)) js_stream_destroy(readable, err);
    js_stream_iter_reject_pending_writes(writer, err);
    js_stream_iter_reject_end(writer, err);
    return make_js_undefined();
}

static Item js_stream_iter_writer_dispose(bool asynchronous) {
    js_stream_iter_writer_fail(make_js_undefined());
    return asynchronous ? js_promise_resolve(make_js_undefined()) : make_js_undefined();
}
JS_FORWARD_STATIC_ITEM(js_stream_iter_writer_async_dispose, (void), js_stream_iter_writer_dispose, (true))
JS_FORWARD_STATIC_ITEM(js_stream_iter_writer_sync_dispose, (void), js_stream_iter_writer_dispose, (false))

static Item js_stream_iter_ondrain(Item writer) {
    if (get_type_id(writer) != LMD_TYPE_MAP && get_type_id(writer) != LMD_TYPE_ELEMENT) return ItemNull;
    Item protocol_key = js_symbol_for(make_string_item("Stream.drainableProtocol"));
    Item protocol = js_get_key_default(writer, protocol_key);
    if (js_is_callable(protocol)) {
        JS_ASSIGN_OR_RETURN(result, js_call_function(protocol, writer, NULL, 0));
        return result;
    }
    Item readable = js_get_key_default(writer, make_string_item("__readable__"));
    if (get_type_id(readable) != LMD_TYPE_MAP && get_type_id(readable) != LMD_TYPE_ELEMENT) return ItemNull;
    int64_t desired = js_stream_iter_desired_size_value(writer);
    if (desired == INT64_MIN) return ItemNull;
    if (desired > 0) return js_promise_resolve(js_bool_item(true));
    Item existing = js_get_key_default(writer, make_string_item("__drain__"));
    if (get_type_id(existing) == LMD_TYPE_MAP || get_type_id(existing) == LMD_TYPE_ELEMENT) {
        return js_get_key_default(existing, make_string_item("promise"));
    }
    Item capability = js_promise_with_resolvers();
    js_set_key_default(writer, make_string_item("__drain__"), capability);
    return js_get_key_default(capability, make_string_item("promise"));
}

static Item js_web_writable_get_writer(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return js_new_object();
    return env[0];
}

extern "C" Item js_transform_stream_new(Item transformer) {
    ensure_keys();
    Item stream_transform = make_js_undefined();
    if (js_is_callable(transformer)) {
        stream_transform = transformer;
    } else if (get_type_id(transformer) == LMD_TYPE_MAP || get_type_id(transformer) == LMD_TYPE_ELEMENT) {
        Item transform_method = js_get_key_default(transformer, make_string_item("transform"));
        if (js_is_callable(transform_method)) {
            stream_transform = js_bind_function(transform_method, transformer, NULL, 0);
        }
    }

    JS_ASSIGN_OR_RETURN(pair, js_stream_iter_push(stream_transform));
    Item writer = js_get_key_default(pair, make_string_item("writer"));
    Item readable = js_get_key_default(pair, make_string_item("readable"));
    js_set_key_default(readable, make_string_item("__web_readable__"), js_bool_item(true));
    js_set_native_key(writer, make_string_item("close"), js_stream_iter_writer_end);
    js_set_native_key(writer, make_string_item("abort"), js_stream_iter_writer_fail);

    Item writable = js_writable_stream_new(make_js_undefined());
    Item* env = js_alloc_env(1);
    env[0] = writer;
    js_set_key_default(writable, make_string_item("__writer__"), writer);
    js_set_key_default(writable, make_string_item("getWriter"),
                    js_new_native_closure(js_web_writable_get_writer, 0, env, 1));

    Item obj = js_new_object();
    js_set_key_default(obj, make_string_item("readable"), readable);
    js_set_key_default(obj, make_string_item("writable"), writable);
    return obj;
}

static Item js_stream_iter_abort(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item signal = env[0];
    Item readable = env[1];
    Item reason = js_get_key_default(signal, make_string_item("reason"));
    if (reason.item == 0 || get_type_id(reason) == LMD_TYPE_UNDEFINED) {
        reason = js_stream_make_error_with_code("AbortError", "The operation was aborted");
        js_set_key_default(reason, make_string_item("name"), make_string_item("AbortError"));
    }
    js_stream_destroy(readable, reason);
    return make_js_undefined();
}

static void js_stream_iter_attach_abort(Item options, Item readable) {
    Item signal = js_get_key_default(options, make_string_item("signal"));
    if (get_type_id(signal) != LMD_TYPE_MAP && get_type_id(signal) != LMD_TYPE_ELEMENT) return;
    Item aborted = js_get_key_default(signal, make_string_item("aborted"));
    if (get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted)) {
        Item reason = js_get_key_default(signal, make_string_item("reason"));
        if (reason.item == 0 || get_type_id(reason) == LMD_TYPE_UNDEFINED) {
            reason = js_stream_make_error_with_code("AbortError", "The operation was aborted");
            js_set_key_default(reason, make_string_item("name"), make_string_item("AbortError"));
        }
        js_stream_destroy(readable, reason);
        return;
    }
    Item add_event = js_get_key_default(signal, make_string_item("addEventListener"));
    if (!js_is_callable(add_event)) return;
    Item* env = js_alloc_env(2);
    env[0] = signal;
    env[1] = readable;
    Item listener = js_new_native_closure(js_stream_iter_abort, 0, env, 2);
    Item args[2] = { make_string_item("abort"), listener };
    js_call_function(add_event, signal, args, 2);
}

static Item js_stream_abort_signal_reason(Item signal) {
    Item reason = js_get_key_default(signal, make_string_item("reason"));
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
    Item aborted = js_get_key_default(signal, make_string_item("aborted"));
    if (get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted)) {
        js_stream_destroy(stream, js_stream_abort_signal_reason(signal));
        return stream;
    }

    Item add_event = js_get_key_default(signal, make_string_item("addEventListener"));
    if (!js_is_callable(add_event)) return stream;
    Item* env = js_alloc_env(2);
    env[0] = signal;
    env[1] = stream;
    Item listener = js_new_native_closure(js_stream_abort_signal_destroy_stream, 0, env, 2);
    Item args[2] = { make_string_item("abort"), listener };
    js_call_function(add_event, signal, args, 2);
    return stream;
}

static Item js_stream_iter_push(Item options_or_transform) {
    ensure_keys();
    TypeId opt_type = get_type_id(options_or_transform);
    Item options = make_js_undefined();
    Item transform = make_js_undefined();
    if (js_is_callable(options_or_transform)) {
        transform = options_or_transform;
    } else if (opt_type == LMD_TYPE_MAP || opt_type == LMD_TYPE_ELEMENT) {
        options = options_or_transform;
        Item backpressure = js_get_key_default(options, make_string_item("backpressure"));
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
    js_set_key_default(readable_opts, make_string_item("objectMode"), js_bool_item(true));
    Item readable = js_readable_new(readable_opts);
    if (opt_type == LMD_TYPE_MAP || opt_type == LMD_TYPE_ELEMENT) {
        js_stream_iter_attach_abort(options, readable);
    }

    Item writer = js_new_object();
    js_set_key_default(writer, make_string_item("__readable__"), readable);
    js_set_key_default(writer, make_string_item("__transform__"), transform);
    js_set_key_default(writer, make_string_item("__closed__"), js_bool_item(false));
    js_set_key_default(writer, make_string_item("__total__"), (Item){.item = i2it(0)});
    js_set_key_default(writer, make_string_item("__hwm__"), (Item){.item = i2it(js_stream_iter_hwm(options))});
    js_set_key_default(readable, make_string_item("__iter_writer__"), writer);
    js_stream_set_default_method(writer, "write", js_stream_iter_writer_write);
    js_stream_set_default_method(writer, "writeSync", js_stream_iter_writer_writeSync);
    js_stream_set_default_method(writer, "writev", js_stream_iter_writer_writev);
    js_stream_set_default_method(writer, "writevSync", js_stream_iter_writer_writevSync);
    js_stream_set_default_method(writer, "end", js_stream_iter_writer_end);
    js_stream_set_default_method(writer, "endSync", js_stream_iter_writer_endSync);
    js_stream_set_default_method(writer, "fail", js_stream_iter_writer_fail);
    js_set_native_key(writer, js_well_known_symbol_key(14), js_stream_iter_writer_async_dispose);
    js_set_native_key(writer, js_well_known_symbol_key(15), js_stream_iter_writer_sync_dispose);
    js_install_native_accessor(writer, make_string_item("desiredSize"),
                               js_new_native_function(js_stream_iter_writer_desired_size),
                               ItemNull, 0);

    Item pair = js_new_object();
    js_set_key_default(pair, make_string_item("writer"), writer);
    js_set_key_default(pair, make_string_item("readable"), readable);
    return pair;
}

// pipe(destination) — pipe this readable to a writable
extern "C" Item js_readable_pipe(Item self, Item dest) {
    ensure_keys();
    Item pipe_event = make_string_item("pipe");
    Item emit_fn = js_get_key_default(dest, key_emit);
    if (js_is_callable(emit_fn)) {
        Item emit_args[2] = {pipe_event, self};
        js_call_function(emit_fn, dest, emit_args, 2);
    } else {
        js_stream_emit(dest, pipe_event, self);
    }

    if (get_type_id(js_get_key_default(self, key_readable_state)) != LMD_TYPE_MAP) {
        return js_legacy_stream_pipe(self, dest);
    }

    // set up data listener that writes to dest
    // store dest reference
    js_set_key_default(self, make_string_item("__pipe_dest__"), dest);
    js_stream_set_flowing(self, true);
    Item pipes = js_readable_pipes(self);
    if (get_type_id(pipes) == LMD_TYPE_ARRAY) {
        js_array_push(pipes, dest);
        if (js_array_length(pipes) > 1) {
            Item state = js_get_key_default(self, key_readable_state);
            Item current = js_stream_await_drain_writers(state);
            if (current.item == 0 ||
                get_type_id(current) == LMD_TYPE_NULL ||
                get_type_id(current) == LMD_TYPE_UNDEFINED) {
                js_set_key_default(state, make_string_item("awaitDrainWriters"),
                                js_stream_make_empty_await_drain_set());
            }
        }
    }
    js_readable_add_pipe_data_event(self);
    Item* drain_env = js_alloc_env(2);
    drain_env[0] = self;
    drain_env[1] = dest;
    Item drain_listener = js_new_native_closure(js_readable_pipe_on_drain, 0, drain_env, 2);
    Item drain_args[2] = {make_string_item("drain"), drain_listener};
    Item on_fn = js_get_key_default(dest, key_on);
    if (js_is_callable(on_fn)) {
        js_call_function(on_fn, dest, drain_args, 2);
    } else {
        js_stream_on(dest, drain_args[0], drain_listener);
    }

    // flush buffer to dest
    Item buf = js_get_key_default(self, key_buffer);
    if (get_type_id(buf) == LMD_TYPE_ARRAY) {
        int64_t blen = js_array_length(buf);
        Item write_fn = js_get_key_default(dest, key_write);
        if (js_is_callable(write_fn)) {
            for (int64_t i = 0; i < blen; i++) {
                Item chunk = js_elements_get_int(buf, i);
                Item result = js_call_function(write_fn, dest, &chunk, 1);
                if (item_is_error(result)) {
                    Item err = js_error_lane_payload(result);
                    js_stream_schedule_error(dest, err);
                    break;
                }
                if (get_type_id(result) == LMD_TYPE_BOOL && !it2b(result)) {
                    Item dest_error = js_get_key_default(dest, make_string_item("__error__"));
                    if (js_stream_has_callback_error(dest_error)) break;
                    js_stream_await_drain_add(self, dest);
                    js_stream_set_flowing(self, false);
                    js_set_key_default(self, key_paused, js_bool_item(true));
                    js_stream_schedule_read(self);
                    if (!js_stream_source_keeps_pipe_on_backpressure(self)) {
                        js_set_key_default(self, make_string_item("__piped__"), js_bool_item(false));
                        js_set_key_default(self, make_string_item("__pipe_dest__"), make_js_undefined());
                    }
                    break;
                }
            }
        }
        js_stream_set_readable_buffer(self, js_array_new(0));
    }

    if (js_item_is_true(js_get_key_default(self, key_end_pending)) ||
        js_item_is_true(js_get_key_default(self, key_ended)) ||
        js_state_get_bool(js_get_key_default(self, key_readable_state), "ended")) {
        Item end_fn = js_get_key_default(dest, key_end);
        if (js_is_callable(end_fn)) {
            js_call_function(end_fn, dest, NULL, 0);
        }
    }

    // register data handler to forward
    // store that pipe is active via marker
    js_set_key_default(self, make_string_item("__piped__"), (Item){.item = b2it(true)});
    js_stream_schedule_resume(self);

    return dest;
}

// destroy([err]) — destroy stream
extern "C" Item js_stream_destroy(Item self, Item err) {
    ensure_keys();
    if (js_item_is_true(js_get_key_default(self, key_destroyed))) {
        js_stream_invoke_destroy_callback(self, err);
        return self;
    }
    if (js_item_is_true(js_get_key_default(self, key_flowing))) {
        js_stream_flush_buffered_data(self);
    }
    bool readable_aborted = js_item_is_true(js_get_key_default(self, key_readable)) &&
                            !js_item_is_true(js_get_key_default(self, key_end_emitted));
    Item writable_state = js_get_key_default(self, key_writable_state);
    bool writable_aborted = get_type_id(writable_state) == LMD_TYPE_MAP &&
                            !js_item_is_true(js_get_key_default(self, key_finish_emitted));
    js_stream_mark_destroyed(self);
    js_set_key_default(self, make_string_item("readableAborted"), js_bool_item(readable_aborted));
    js_set_key_default(self, make_string_item("writableAborted"), js_bool_item(writable_aborted));

    if (err.item != 0 && get_type_id(err) != LMD_TYPE_UNDEFINED &&
        get_type_id(err) != LMD_TYPE_NULL) {
        js_stream_set_error_state(self, err);
        js_set_key_default(self, make_string_item("__error__"), err);
        js_stream_async_iterators_drain(self, err);
    } else {
        Item iterators = js_get_key_default(self, make_string_item("__async_iterators__"));
        if (get_type_id(iterators) == LMD_TYPE_ARRAY && js_array_length(iterators) > 0) {
            Item close_err = js_stream_make_error_with_code("ERR_STREAM_PREMATURE_CLOSE",
                "Premature close");
            js_stream_async_iterators_drain(self, close_err);
        }
    }

    Item destroy_fn = js_get_key_default(self, make_string_item("_destroy"));
    if (js_is_callable(destroy_fn)) {
        js_set_key_default(self, key_destroy_pending, js_bool_item(true));
        Item destroy_cb = js_bind_function(js_new_native_function(js_stream_after_destroy),
                                           make_js_undefined(), &self, 1);
        Item destroy_err = js_stream_has_callback_error(err) ? err : ItemNull;
        Item args[2] = { destroy_err, destroy_cb };
        js_set_key_default(self, make_string_item("__destroying_sync__"), js_bool_item(true));
        js_set_key_default(self, make_string_item("__destroy_cb_done__"), js_bool_item(false));
        js_set_key_default(self, make_string_item("__destroy_cb_error__"), make_js_undefined());
        Item destroy_result = js_call_function(destroy_fn, self, args, 2);
        js_set_key_default(self, make_string_item("__destroying_sync__"), js_bool_item(false));
        if (!item_is_error(destroy_result)) {
            if (js_item_is_true(js_get_key_default(self, make_string_item("__destroy_cb_done__")))) {
                Item cb_err = js_get_key_default(self, make_string_item("__destroy_cb_error__"));
                js_set_key_default(self, key_destroy_pending, js_bool_item(false));
                if (js_stream_has_callback_error(cb_err)) {
                    js_set_key_default(self, make_string_item("__error__"), cb_err);
                    // Synchronous _destroy(cb) errors have the same iterator
                    // contract as async callbacks: pending next() observes cb_err.
                    js_stream_async_iterators_drain(self, cb_err);
                    js_stream_schedule_error(self, cb_err);
                }
                js_stream_invoke_destroy_callback(self, cb_err);
                js_stream_schedule_close(self);
            }
            return self;
        }
        js_set_key_default(self, key_destroy_pending, js_bool_item(false));
    }

    if (err.item != 0 && get_type_id(err) != LMD_TYPE_UNDEFINED &&
        get_type_id(err) != LMD_TYPE_NULL) {
        js_stream_schedule_error(self, err);
    }
    js_stream_invoke_destroy_callback(self, err);
    js_stream_schedule_close(self);
    return self;
}

// resume() — start flowing mode, call _read if set
extern "C" Item js_readable_resume(Item self) {
    ensure_keys();
    js_stream_set_flowing(self, true);
    js_set_key_default(self, key_paused, js_bool_item(false));
    js_stream_schedule_resume(self);
    return self;
}

// pause() — stop flowing mode
extern "C" Item js_readable_pause(Item self) {
    ensure_keys();
    js_stream_set_flowing(self, false);
    js_set_key_default(self, key_paused, js_bool_item(true));
    return self;
}

extern "C" Item js_readable_isPaused(Item self) {
    ensure_keys();
    return js_bool_item(js_item_is_true(js_get_key_default(self, key_paused)));
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
        js_set_key_default(self, make_string_item("_encoding"), next_encoding);
        Item state = js_get_key_default(self, key_readable_state);
        if (get_type_id(state) == LMD_TYPE_MAP) {
            js_set_key_default(state, make_string_item("encoding"), next_encoding);
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
        if (!item_is_error(inspected) && get_type_id(inspected) == LMD_TYPE_STRING)
            return inspected;
    }
    Item coerced = js_to_string(encoding);
    if (!item_is_error(coerced) && get_type_id(coerced) == LMD_TYPE_STRING)
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
    Item default_encoding = js_get_key_default(self, make_string_item("_defaultEncoding"));
    if (get_type_id(default_encoding) == LMD_TYPE_STRING) return default_encoding;
    return make_string_item("utf8");
}

static bool js_stream_writable_is_object_mode(Item self) {
    Item state = js_get_key_default(self, key_writable_state);
    return js_state_get_bool(state, "objectMode");
}

static bool js_stream_writable_should_decode_strings(Item self) {
    Item decode_strings = js_get_key_default(self, make_string_item("_decodeStrings"));
    return get_type_id(decode_strings) != LMD_TYPE_BOOL || it2b(decode_strings);
}

static bool js_stream_chunk_is_buffer(Item chunk) {
    Item result = js_buffer_isBuffer(chunk);
    return get_type_id(result) == LMD_TYPE_BOOL && it2b(result);
}
JS_FORWARD_STATIC_RETURN(bool, js_stream_chunk_is_arraybuffer_view, (Item chunk), js_is_typed_array, (chunk) || js_is_dataview(chunk))

static bool js_stream_readable_is_object_mode(Item self) {
    Item state = js_get_key_default(self, key_readable_state);
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

static Item js_stream_convert_view_to_buffer(Item* chunk) {
    if (!js_stream_chunk_is_arraybuffer_view(*chunk) ||
        js_stream_chunk_is_buffer(*chunk)) {
        return js_bool_item(true);
    }
    JS_ASSIGN_OR_RETURN(buffer, js_buffer_from(*chunk, make_js_undefined(), make_js_undefined()));
    if (buffer.item != 0 &&
        get_type_id(buffer) != LMD_TYPE_UNDEFINED &&
        get_type_id(buffer) != LMD_TYPE_NULL) {
        *chunk = buffer;
    }
    return js_bool_item(true);
}

static Item js_stream_prepare_readable_chunk(Item self, Item* chunk, Item encoding) {
    if (js_stream_readable_is_object_mode(self)) return js_bool_item(true);
    if (get_type_id(*chunk) == LMD_TYPE_STRING) {
        Item chunk_encoding = encoding;
        if (chunk_encoding.item == 0 ||
            get_type_id(chunk_encoding) == LMD_TYPE_UNDEFINED ||
            get_type_id(chunk_encoding) == LMD_TYPE_NULL) {
            chunk_encoding = js_stream_resolve_write_encoding(self, chunk_encoding);
        }
        if (get_type_id(chunk_encoding) != LMD_TYPE_STRING ||
            !js_stream_is_valid_encoding(chunk_encoding)) {
            return js_stream_throw_unknown_encoding(chunk_encoding);
        }
        Item stream_encoding = js_get_key_default(self, make_string_item("_encoding"));
        if (js_stream_encoding_items_equal(chunk_encoding, stream_encoding)) {
            return js_bool_item(true);
        }
        JS_ASSIGN_OR_RETURN(buffer, js_buffer_from(*chunk, chunk_encoding, make_js_undefined()));
        *chunk = buffer;
        return js_bool_item(true);
    }
    return js_stream_convert_view_to_buffer(chunk);
}

static Item js_stream_prepare_writable_chunk(Item self, Item* chunk, Item* encoding) {
    if (js_stream_writable_is_object_mode(self)) return js_bool_item(true);

    if (js_stream_chunk_is_buffer(*chunk)) {
        *encoding = make_string_item("buffer");
        return js_bool_item(true);
    }

    if (js_stream_chunk_is_arraybuffer_view(*chunk)) {
        JS_ASSIGN_OR_RETURN(converted, js_stream_convert_view_to_buffer(chunk));
        *encoding = make_string_item("buffer");
        return js_bool_item(true);
    }

    Item write_encoding = js_stream_resolve_write_encoding(self, *encoding);
    if (get_type_id(*chunk) == LMD_TYPE_STRING &&
        !js_stream_writable_is_object_mode(self)) {
        if (!js_stream_is_valid_encoding(write_encoding)) {
            return js_stream_throw_unknown_encoding(write_encoding);
        }
        if (js_stream_writable_should_decode_strings(self)) {
            JS_ASSIGN_OR_RETURN(buffer, js_buffer_from(*chunk, write_encoding, make_js_undefined()));
            if (buffer.item != 0 &&
                get_type_id(buffer) != LMD_TYPE_UNDEFINED &&
                get_type_id(buffer) != LMD_TYPE_NULL) {
                *chunk = buffer;
                *encoding = make_string_item("buffer");
                return js_bool_item(true);
            }
        }
    }

    *encoding = write_encoding;
    return js_bool_item(true);
}

static Item js_stream_validate_writable_chunk(Item self, Item chunk) {
    if (get_type_id(chunk) == LMD_TYPE_NULL) {
        return js_throw_type_error_code("ERR_STREAM_NULL_VALUES",
                                        "May not write null values to stream");
    }
    if (js_stream_writable_is_object_mode(self)) return js_bool_item(true);
    TypeId tid = get_type_id(chunk);
    if (tid == LMD_TYPE_STRING || js_stream_chunk_is_arraybuffer_view(chunk)) {
        return js_bool_item(true);
    }
    return js_throw_invalid_arg_type("chunk", "string, Buffer, or Uint8Array", chunk);
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
    js_set_key_default(self, make_string_item("_defaultEncoding"),
                    js_stream_canonical_encoding(next_encoding));
    return self;
}
JS_FORWARD_STATIC_RETURN(bool, js_stream_hwm_object_mode_arg, (Item object_mode), get_type_id, (object_mode) == LMD_TYPE_BOOL && it2b(object_mode))
JS_FORWARD_EXPRESSION(Item, js_stream_getDefaultHighWaterMark, (Item object_mode), ((Item){.item = i2it(js_stream_hwm_object_mode_arg(object_mode) ? js_stream_default_object_hwm : js_stream_default_byte_hwm)}))

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

static Item js_stream_validate_hwm_option(const char* name, Item value) {
    if (!js_stream_item_is_nan_number(value)) return js_status_ok();
    char msg[160];
    snprintf(msg, sizeof(msg),
             "The property 'options.%s' is invalid. Received NaN", name);
    return js_throw_type_error_code("ERR_INVALID_ARG_VALUE", msg);
}
JS_FORWARD_STATIC_VOID( js_stream_define_bool, (Item obj, const char* name, bool value), js_create_data_property, (obj, make_string_item(name), js_bool_item(value)))

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

static Item js_stream_create_instance(Item prototype, JsClass class_id) {
    Item self = js_get_this();
    if (js_stream_called_as_constructor() && js_stream_is_object_like(self)) {
        return self;
    }

    Item obj = js_new_object_with_class(class_id);
    if (js_stream_is_object_like(prototype)) {
        js_set_prototype(obj, prototype);
    }
    return obj;
}

static bool js_stream_ordinary_has_instance(Item value) {
    Item result = js_ordinary_has_instance(value, js_get_this());
    return js_item_is_true(result);
}

static Item js_stream_has_instance_classes(Item value, int class_count,
        JsClass class1, JsClass class2, JsClass class3, JsClass class4) {
    if (js_stream_ordinary_has_instance(value)) return js_bool_item(true);
    JsClass cls = js_class_id(value);
    bool match = (class_count > 0 && cls == class1) ||
                 (class_count > 1 && cls == class2) ||
                 (class_count > 2 && cls == class3) ||
                 (class_count > 3 && cls == class4);
    return js_bool_item(match);
}
JS_FORWARD_STATIC_ITEM(js_stream_readable_has_instance, (Item value), js_stream_has_instance_classes, (value, 4, JS_CLASS_READABLE, JS_CLASS_DUPLEX, JS_CLASS_TRANSFORM, JS_CLASS_PASS_THROUGH))
JS_FORWARD_STATIC_ITEM(js_stream_writable_has_instance, (Item value), js_stream_has_instance_classes, (value, 4, JS_CLASS_WRITABLE, JS_CLASS_DUPLEX, JS_CLASS_TRANSFORM, JS_CLASS_PASS_THROUGH))
JS_FORWARD_STATIC_ITEM(js_stream_duplex_has_instance, (Item value), js_stream_has_instance_classes, (value, 3, JS_CLASS_DUPLEX, JS_CLASS_TRANSFORM, JS_CLASS_PASS_THROUGH, JS_CLASS_NONE))
JS_FORWARD_STATIC_ITEM(js_stream_transform_has_instance, (Item value), js_stream_has_instance_classes, (value, 2, JS_CLASS_TRANSFORM, JS_CLASS_PASS_THROUGH, JS_CLASS_NONE, JS_CLASS_NONE))
JS_FORWARD_STATIC_ITEM(js_stream_passthrough_has_instance, (Item value), js_stream_has_instance_classes, (value, 1, JS_CLASS_PASS_THROUGH, JS_CLASS_NONE, JS_CLASS_NONE, JS_CLASS_NONE))

// Helper: propagate stream constructor options to instance methods
static Item propagate_stream_options(Item obj, Item opts) {
    if (get_type_id(opts) != LMD_TYPE_MAP) return js_status_ok();
    JsClass cls = js_class_id(obj);
    bool is_duplex_like = cls == JS_CLASS_DUPLEX || cls == JS_CLASS_TRANSFORM ||
                          cls == JS_CLASS_PASS_THROUGH;
    // read → _read
    Item read_opt = js_get_key_default(opts, make_string_item("read"));
    if (js_is_callable(read_opt))
        js_set_key_default(obj, make_string_item("_read"), read_opt);
    // write → _write
    Item write_opt = js_get_key_default(opts, make_string_item("write"));
    if (js_is_callable(write_opt))
        js_set_key_default(obj, make_string_item("_write"), write_opt);
    // writev → _writev
    Item writev_opt = js_get_key_default(opts, make_string_item("writev"));
    if (js_is_callable(writev_opt))
        js_set_key_default(obj, make_string_item("_writev"), writev_opt);
    // transform → _transform
    Item transform_opt = js_get_key_default(opts, make_string_item("transform"));
    if (js_is_callable(transform_opt))
        js_set_key_default(obj, make_string_item("_transform"), transform_opt);
    // flush → _flush
    Item flush_opt = js_get_key_default(opts, make_string_item("flush"));
    if (js_is_callable(flush_opt))
        js_set_key_default(obj, make_string_item("_flush"), flush_opt);
    // final → _final
    Item final_opt = js_get_key_default(opts, make_string_item("final"));
    if (js_is_callable(final_opt))
        js_set_key_default(obj, make_string_item("_final"), final_opt);
    // destroy → _destroy
    Item destroy_opt = js_get_key_default(opts, make_string_item("destroy"));
    if (js_is_callable(destroy_opt))
        js_set_key_default(obj, make_string_item("_destroy"), destroy_opt);
    // construct → _construct
    Item construct_opt = js_get_key_default(opts, make_string_item("construct"));
    if (js_is_callable(construct_opt))
        js_set_key_default(obj, make_string_item("_construct"), construct_opt);
    Item object_mode_hwm = (Item){.item = i2it(js_stream_default_object_hwm)};
    // highWaterMark
    JS_ASSIGN_OR_RETURN(hwm, js_get_key_default(opts, make_string_item("highWaterMark")));
    JS_RETURN_IF_ERROR(js_stream_validate_hwm_option("highWaterMark", hwm));
    bool has_hwm = js_stream_item_is_number(hwm);
    if (has_hwm) {
        js_set_key_default(obj, make_string_item("_highWaterMark"), hwm);
        js_stream_set_readable_high_water_mark(obj, hwm);
        js_stream_set_writable_high_water_mark(obj, hwm);
    }
    // objectMode
    Item om = js_get_key_default(opts, make_string_item("objectMode"));
    bool object_mode_true = get_type_id(om) == LMD_TYPE_BOOL && it2b(om);
    if (get_type_id(om) == LMD_TYPE_BOOL) {
        js_set_key_default(obj, make_string_item("_objectMode"), om);
        js_stream_set_readable_object_mode(obj, it2b(om));
        js_stream_set_writable_object_mode(obj, it2b(om));
        if (object_mode_true && !has_hwm) {
            js_stream_set_readable_high_water_mark(obj, object_mode_hwm);
            js_stream_set_writable_high_water_mark(obj, object_mode_hwm);
        }
    }
    // readable side highWaterMark/objectMode
    JS_ASSIGN_OR_RETURN(readable_hwm, js_get_key_default(opts, make_string_item("readableHighWaterMark")));
    JS_RETURN_IF_ERROR(js_stream_validate_hwm_option("readableHighWaterMark", readable_hwm));
    bool has_readable_hwm = js_stream_item_is_number(readable_hwm);
    Item readable_om = js_get_key_default(opts, make_string_item("readableObjectMode"));
    if (is_duplex_like && get_type_id(readable_om) == LMD_TYPE_BOOL) {
        js_stream_set_readable_object_mode(obj, it2b(readable_om));
        if (it2b(readable_om) && !has_readable_hwm && !has_hwm)
            js_stream_set_readable_high_water_mark(obj, object_mode_hwm);
    }
    if (is_duplex_like && has_readable_hwm && !has_hwm)
        js_stream_set_readable_high_water_mark(obj, readable_hwm);
    // writable side highWaterMark/objectMode
    JS_ASSIGN_OR_RETURN(writable_hwm, js_get_key_default(opts, make_string_item("writableHighWaterMark")));
    JS_RETURN_IF_ERROR(js_stream_validate_hwm_option("writableHighWaterMark", writable_hwm));
    bool has_writable_hwm = js_stream_item_is_number(writable_hwm);
    Item writable_om = js_get_key_default(opts, make_string_item("writableObjectMode"));
    if (is_duplex_like && get_type_id(writable_om) == LMD_TYPE_BOOL) {
        js_stream_set_writable_object_mode(obj, it2b(writable_om));
        if (it2b(writable_om) && !has_writable_hwm && !has_hwm)
            js_stream_set_writable_high_water_mark(obj, object_mode_hwm);
    }
    if (is_duplex_like && has_writable_hwm && !has_hwm)
        js_stream_set_writable_high_water_mark(obj, writable_hwm);
    // encoding
    Item enc = js_get_key_default(opts, make_string_item("encoding"));
    if (get_type_id(enc) == LMD_TYPE_STRING) {
        js_set_key_default(obj, make_string_item("_encoding"), enc);
        Item rstate = js_get_key_default(obj, key_readable_state);
        if (get_type_id(rstate) == LMD_TYPE_MAP)
            js_set_key_default(rstate, make_string_item("encoding"), enc);
    }
    // defaultEncoding
    Item default_enc = js_get_key_default(opts, make_string_item("defaultEncoding"));
    if (get_type_id(default_enc) == LMD_TYPE_STRING) {
        if (!js_stream_is_valid_encoding(default_enc)) {
            return js_stream_throw_unknown_encoding(default_enc);
        }
        js_set_key_default(obj, make_string_item("_defaultEncoding"),
                        js_stream_canonical_encoding(default_enc));
    }
    Item decode_strings = js_get_key_default(opts, make_string_item("decodeStrings"));
    if (get_type_id(decode_strings) == LMD_TYPE_BOOL)
        js_set_key_default(obj, make_string_item("_decodeStrings"), decode_strings);
    // readable/writable side switches used by Duplex options.
    Item readable = js_get_key_default(opts, make_string_item("readable"));
    if (get_type_id(readable) == LMD_TYPE_BOOL)
        js_stream_set_readable_side_enabled(obj, it2b(readable));
    Item writable = js_get_key_default(opts, make_string_item("writable"));
    if (get_type_id(writable) == LMD_TYPE_BOOL)
        js_stream_set_writable_side_enabled(obj, it2b(writable));
    Item capture_rejections = js_get_key_default(opts, make_string_item("captureRejections"));
    if (get_type_id(capture_rejections) == LMD_TYPE_BOOL)
        js_set_key_default(obj, key_capture_rejections, capture_rejections);
    Item auto_destroy = js_get_key_default(opts, make_string_item("autoDestroy"));
    if (get_type_id(auto_destroy) == LMD_TYPE_BOOL)
        js_set_key_default(obj, key_auto_destroy, auto_destroy);
    Item allow_half_open = js_get_key_default(opts, make_string_item("allowHalfOpen"));
    if (is_duplex_like && get_type_id(allow_half_open) == LMD_TYPE_BOOL)
        js_set_key_default(obj, make_string_item("allowHalfOpen"), allow_half_open);
    Item signal = js_get_key_default(opts, make_string_item("signal"));
    TypeId signal_type = get_type_id(signal);
    if (signal.item != 0 && signal_type != LMD_TYPE_UNDEFINED &&
        signal_type != LMD_TYPE_NULL) {
        if (!js_stream_is_abort_signal(signal)) {
            return js_throw_invalid_arg_type("options.signal", "AbortSignal", signal);
        }
        js_stream_attach_abort_signal(signal, obj);
    }
    return js_status_ok();
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

#define JS_STREAM_THIS0(name, target) \
    static Item name(void) { return target(js_get_this()); }
#define JS_STREAM_THIS1(name, target) \
    static Item name(Item a1) { return target(js_get_this(), a1); }
#define JS_STREAM_THIS2(name, target) \
    static Item name(Item a1, Item a2) { return target(js_get_this(), a1, a2); }
#define JS_STREAM_THIS3(name, target) \
    static Item name(Item a1, Item a2, Item a3) { \
        return target(js_get_this(), a1, a2, a3); \
    }

JS_STREAM_THIS2(js_stream_inst_on, js_stream_on)
JS_STREAM_THIS2(js_stream_inst_once, js_stream_once)
JS_STREAM_THIS2(js_stream_inst_off, js_stream_off)
JS_STREAM_THIS1(js_stream_inst_removeAllListeners, js_stream_removeAllListeners)
JS_STREAM_THIS2(js_stream_inst_emit, js_stream_emit)
JS_STREAM_THIS0(js_stream_inst_eventNames, js_stream_eventNames)
JS_STREAM_THIS1(js_stream_inst_listeners, js_stream_listeners)
JS_STREAM_THIS2(js_stream_inst_listenerCount, js_stream_listenerCount)
JS_STREAM_THIS2(js_readable_inst_push, js_readable_push_encoded)
JS_STREAM_THIS2(js_readable_inst_unshift, js_readable_unshift_encoded)
JS_STREAM_THIS1(js_readable_inst_read, js_readable_read_size)
JS_STREAM_THIS1(js_readable_inst_pipe, js_readable_pipe)
static Item js_readable_inst_unpipe(Item dest) {
    js_readable_remove_pipe(js_get_this(), dest, true);
    return js_get_this();
}
JS_FORWARD_STATIC_ITEM(js_stream_base_constructor, (void), make_js_undefined, ())
static Item js_stream_inst_destroy(Item err, Item callback) {
    if (js_is_callable(callback)) {
        js_set_key_default(js_get_this(), make_string_item("__destroy_callback__"), callback);
    }
    return js_stream_destroy(js_get_this(), err);
}
static Item js_stream_inst_undestroy(void) {
    ensure_keys();
    Item self = js_get_this();

    js_set_key_default(self, key_destroyed, js_bool_item(false));
    js_set_key_default(self, make_string_item("destroyed"), js_bool_item(false));
    js_set_key_default(self, key_destroy_pending, js_bool_item(false));
    js_set_key_default(self, key_close_emitted, js_bool_item(false));
    js_set_key_default(self, key_closed, js_bool_item(false));
    js_set_key_default(self, make_string_item("errored"), ItemNull);
    js_set_key_default(self, make_string_item("__error__"), make_js_undefined());

    Item readable_state = js_get_key_default(self, key_readable_state);
    if (get_type_id(readable_state) == LMD_TYPE_MAP) {
        js_stream_set_readable_open(self, js_stream_readable_side_enabled(self));
        js_set_key_default(self, key_ended, js_bool_item(false));
        js_set_key_default(self, key_end_pending, js_bool_item(false));
        js_set_key_default(self, key_end_emitted, js_bool_item(false));
        js_set_key_default(self, key_reading, js_bool_item(false));
        js_create_data_property(self, make_string_item("readableEnded"), js_bool_item(false));
        js_set_key_default(self, make_string_item("readableAborted"), js_bool_item(false));
        js_state_set_bool(readable_state, "ended", false);
        js_state_set_bool(readable_state, "endEmitted", false);
        js_state_set_bool(readable_state, "errorEmitted", false);
        js_state_set_bool(readable_state, "reading", false);
        js_state_set_item(readable_state, "errored", ItemNull);
    }

    Item writable_state = js_get_key_default(self, key_writable_state);
    if (get_type_id(writable_state) == LMD_TYPE_MAP) {
        js_stream_set_writable_open(self, js_stream_writable_side_enabled(self));
        js_set_key_default(self, key_finished, js_bool_item(false));
        js_set_key_default(self, key_finish_emitted, js_bool_item(false));
        js_create_data_property(self, make_string_item("writableEnded"), js_bool_item(false));
        js_create_data_property(self, make_string_item("writableFinished"), js_bool_item(false));
        js_set_key_default(self, make_string_item("writableAborted"), js_bool_item(false));
        js_state_set_bool(writable_state, "ending", false);
        js_state_set_bool(writable_state, "ended", false);
        js_state_set_bool(writable_state, "finished", false);
        js_state_set_bool(writable_state, "errorEmitted", false);
        js_state_set_item(writable_state, "errored", ItemNull);
    }

    return make_js_undefined();
}
JS_STREAM_THIS0(js_readable_inst_resume, js_readable_resume)
JS_STREAM_THIS0(js_readable_inst_pause, js_readable_pause)
JS_STREAM_THIS0(js_readable_inst_isPaused, js_readable_isPaused)
JS_STREAM_THIS1(js_stream_inst_setEncoding, js_stream_setEncoding)
JS_STREAM_THIS1(js_stream_inst_setDefaultEncoding, js_stream_setDefaultEncoding)
JS_STREAM_THIS1(js_readable_inst_iterator, js_readable_iterator)
JS_STREAM_THIS1(js_readable_inst_toArray, js_readable_toArray)
JS_STREAM_THIS2(js_readable_inst_map, js_readable_map)
JS_STREAM_THIS2(js_readable_inst_filter, js_readable_filter)
JS_STREAM_THIS2(js_readable_inst_forEach, js_readable_forEach)
JS_STREAM_THIS3(js_readable_inst_reduce, js_readable_reduce)
JS_STREAM_THIS2(js_readable_inst_compose, js_readable_compose)
JS_STREAM_THIS0(js_stream_inst_asyncIterator, js_stream_async_iterator)

static void js_stream_install_async_iterator(Item obj) {
    RootFrame roots(4);
    Rooted<Item> object_root(roots, obj);
    Rooted<Item> iterator_root(roots, js_new_native_function(js_stream_inst_asyncIterator));
    Rooted<Item> async_key_root(roots, js_well_known_symbol_key(5));
    Rooted<Item> iter_key_root(roots, js_well_known_symbol_key(1));
    js_set_key_default(object_root.get(), async_key_root.get(), iterator_root.get());
    js_set_key_default(object_root.get(), iter_key_root.get(), iterator_root.get());
    js_mark_non_enumerable(object_root.get(), async_key_root.get());
    js_mark_non_enumerable(object_root.get(), iter_key_root.get());
}

static void js_stream_install_readable_helpers(Item obj) {
    js_stream_set_method(obj, make_string_item("toArray"), js_readable_inst_toArray, 1);
    js_stream_set_method(obj, make_string_item("map"), js_readable_inst_map, 2);
    js_stream_set_method(obj, make_string_item("filter"), js_readable_inst_filter, 2);
    js_stream_set_method(obj, make_string_item("forEach"), js_readable_inst_forEach, 2);
    js_stream_set_method(obj, make_string_item("reduce"), js_readable_inst_reduce, 3);
    js_stream_set_method(obj, make_string_item("compose"), js_readable_inst_compose, 2);
}
JS_STREAM_THIS3(js_writable_inst_write, js_writable_write)
JS_STREAM_THIS2(js_writable_inst_end, js_writable_end)
JS_STREAM_THIS0(js_writable_inst_cork, js_writable_cork)
JS_STREAM_THIS0(js_writable_inst_uncork, js_writable_uncork)
JS_STREAM_THIS3(js_transform_inst_write, js_transform_write)
JS_STREAM_THIS2(js_transform_inst_end, js_transform_end)

#undef JS_STREAM_THIS0
#undef JS_STREAM_THIS1
#undef JS_STREAM_THIS2
#undef JS_STREAM_THIS3

template <typename Target>
static void js_stream_set_method(Item object, Item key, Target target,
        int adapter_arity) {
    RootFrame roots(3);
    Rooted<Item> object_root(roots, object);
    Rooted<Item> key_root(roots, key);
    Rooted<Item> function_root(roots,
        js_new_native_function(target, adapter_arity));
    js_set_key_default(object_root.get(), key_root.get(), function_root.get());
}

// Readable constructor
static Item js_readable_new_internal(Item opts, JsClass class_id) {
    RootFrame roots(4);
    Rooted<Item> options_root(roots, opts);
    Rooted<Item> readable_root(roots, ItemNull);
    Rooted<Item> listeners_root(roots, ItemNull);
    Rooted<Item> off_root(roots, ItemNull);
    ensure_keys();
    readable_root.set(js_stream_create_instance(stream_readable_prototype,
        class_id));
    // Readable construction creates properties, callbacks, and helper objects;
    // retain both the instance and options across every compacting allocation.
#define obj readable_root.get()
#define opts options_root.get()

    js_set_key_default(obj, key_readable, js_bool_item(true));
    js_set_key_default(obj, key_readable_side_enabled, js_bool_item(true));
    js_stream_set_flowing(obj, false);
    js_set_key_default(obj, key_ended, js_bool_item(false));
    js_set_key_default(obj, key_destroyed, js_bool_item(false));
    js_set_key_default(obj, make_string_item("destroyed"), js_bool_item(false));
    js_set_key_default(obj, make_string_item("errored"), ItemNull);
    js_set_key_default(obj, make_string_item("readableAborted"), js_bool_item(false));
    js_set_key_default(obj, key_end_pending, js_bool_item(false));
    js_set_key_default(obj, key_end_emitted, js_bool_item(false));
    js_set_key_default(obj, key_reading, js_bool_item(false));
    js_set_key_default(obj, key_reading_sync, js_bool_item(false));
    js_set_key_default(obj, key_paused, js_bool_item(false));
    js_set_key_default(obj, key_close_emitted, js_bool_item(false));
    js_set_key_default(obj, key_closed, js_bool_item(false));
    js_set_key_default(obj, key_auto_destroy, js_bool_item(true));
    js_set_key_default(obj, key_readable_state, js_create_readable_state());
    js_stream_define_bool(obj, "readableEnded", false);
    js_stream_set_readable_buffer(obj, js_array_new(0));
    listeners_root.set(js_new_object());
    js_set_key_default(obj, key_listeners, listeners_root.get());
    js_set_key_default(obj, make_string_item("_events"), js_new_object());
    js_stream_init_readable_options(obj);

    js_stream_set_method(obj, key_on, js_stream_inst_on, 2);
    js_stream_set_method(obj, make_string_item("once"), js_stream_inst_once, 2);
    off_root.set(js_new_native_function(js_stream_inst_off));
    js_set_key_default(obj, make_string_item("off"), off_root.get());
    js_set_key_default(obj, make_string_item("removeListener"), off_root.get());
    js_stream_set_method(obj, make_string_item("removeAllListeners"), js_stream_inst_removeAllListeners, 1);
    js_stream_set_method(obj, key_emit, js_stream_inst_emit, 2);
    js_stream_set_method(obj, make_string_item("eventNames"), js_stream_inst_eventNames, 0);
    js_stream_set_method(obj, make_string_item("listeners"), js_stream_inst_listeners, 1);
    js_stream_set_method(obj, make_string_item("listenerCount"), js_stream_inst_listenerCount, 2);
    js_stream_set_method(obj, key_push, js_readable_inst_push, 2);
    js_stream_set_method(obj, make_string_item("unshift"), js_readable_inst_unshift, 2);
    js_stream_set_method(obj, key_read, js_readable_inst_read, 1);
    js_stream_set_method(obj, key_pipe, js_readable_inst_pipe, 1);
    js_stream_set_method(obj, make_string_item("unpipe"), js_readable_inst_unpipe, 1);
    js_stream_set_method(obj, key_destroy, js_stream_inst_destroy, 2);
    js_stream_set_method(obj, make_string_item("_undestroy"), js_stream_inst_undestroy, 0);
    js_stream_set_method(obj, make_string_item("resume"), js_readable_inst_resume, 0);
    js_stream_set_method(obj, make_string_item("pause"), js_readable_inst_pause, 0);
    js_stream_set_method(obj, make_string_item("isPaused"), js_readable_inst_isPaused, 0);
    js_stream_set_method(obj, make_string_item("setEncoding"), js_stream_inst_setEncoding, 1);
    js_stream_set_method(obj, make_string_item("iterator"), js_readable_inst_iterator, 1);
    js_stream_install_async_iterator(obj);
    js_stream_install_readable_helpers(obj);

    JS_RETURN_IF_ERROR(propagate_stream_options(obj, opts));
    js_stream_call_construct(obj);
    Item result = obj;
#undef opts
#undef obj
    return result;
}

// =============================================================================
// Writable stream
// =============================================================================

// write(chunk) — write data to writable stream
extern "C" Item js_writable_write(Item self, Item chunk, Item encoding, Item callback) {
    ensure_keys();
    if (js_is_callable(encoding) &&
        (callback.item == 0 || get_type_id(callback) == LMD_TYPE_UNDEFINED)) {
        callback = encoding;
        encoding = make_js_undefined();
    }

    Item writable_state = js_get_key_default(self, key_writable_state);
    if (!js_item_is_true(js_get_key_default(self, key_writable)) ||
        js_state_get_bool(writable_state, "ended")) {
        Item err = js_stream_make_error_with_code("ERR_STREAM_WRITE_AFTER_END",
            "write after end");
        js_stream_schedule_callback_error(callback, err);
        js_stream_schedule_error_once(self, err);
        return js_bool_item(false);
    }

    bool write_in_progress = js_item_is_true(js_get_key_default(self, make_string_item("_writing")));
    JS_ASSIGN_OR_RETURN(validation, js_stream_validate_writable_chunk(self, chunk));
    JS_ASSIGN_OR_RETURN(preparation, js_stream_prepare_writable_chunk(self, &chunk, &encoding));
    bool accepted = js_stream_begin_write(self, chunk);

    // if corked, buffer the write
    Item corked = js_get_key_default(self, make_string_item("_corked"));
    if ((get_type_id(corked) == LMD_TYPE_INT && it2i(corked) > 0) ||
        write_in_progress) {
        js_stream_buffer_write_request(self, chunk, encoding, callback);
        return js_bool_item(accepted);
    }

    // call _write handler if set
    Item write_handler = js_get_key_default(self, make_string_item("_write"));
    if (!js_is_callable(write_handler)) {
        // legacy: try __write_handler__
        write_handler = js_get_key_default(self, make_string_item("__write_handler__"));
    }
    if (js_is_callable(write_handler)) {
        Item write_cb = js_stream_make_write_callback(self, callback);
        Item args[3] = {chunk, encoding, write_cb};
        js_set_key_default(self, make_string_item("_writing"), js_bool_item(true));
        Item write_result = js_call_function(write_handler, self, args, 3);
        if (item_is_error(write_result)) {
            Item err = js_error_lane_payload(write_result);
            js_stream_after_write(self, callback, err);
            return js_bool_item(false);
        }
    } else {
        Item writev_fn = js_get_key_default(self, make_string_item("_writev"));
        if (js_is_callable(writev_fn)) {
            js_stream_buffer_write_request(self, chunk, encoding, callback);
            js_stream_flush_pending_writes(self);
            return js_bool_item(accepted);
        }
        // no _write method — throw ERR_METHOD_NOT_IMPLEMENTED
        Item state = js_get_key_default(self, key_writable_state);
        js_state_set_item(state, "length", (Item){.item = i2it(0)});
        js_state_set_bool(state, "needDrain", false);
        return js_throw_error_with_code("ERR_METHOD_NOT_IMPLEMENTED",
                                        "The _write() method is not implemented");
    }

    return js_bool_item(accepted);
}

static Item js_stream_emit_finish_tick(Item self) {
    ensure_keys();
    if (js_item_is_true(js_get_key_default(self, key_destroyed)) ||
        js_item_is_true(js_get_key_default(self, key_finish_emitted)) ||
        js_stream_has_stored_error(self)) {
        return make_js_undefined();
    }
    js_set_key_default(self, key_finish_emitted, js_bool_item(true));
    stream_emit(self, "finish", NULL, 0);
    if (js_stream_can_auto_destroy_after_writable_finish(self)) {
        js_stream_auto_destroy_after_terminal(self);
    }
    return make_js_undefined();
}

JS_STREAM_ENV_UNARY_CLOSURE(js_stream_emit_finish_tick_closure, js_stream_emit_finish_tick)
JS_FORWARD_STATIC_VOID( js_stream_schedule_finish, (Item self), js_stream_schedule_unary, (self, js_stream_emit_finish_tick_closure))
JS_FORWARD_STATIC_EXPRESSION(bool, js_stream_has_error, (Item err), (err.item != 0 && get_type_id(err) != LMD_TYPE_UNDEFINED && get_type_id(err) != LMD_TYPE_NULL))

static Item js_stream_make_error_with_code(const char* code, const char* message) {
    Item err = js_new_error(make_string_item(message));
    js_set_key_default(err, make_string_item("code"), make_string_item(code));
    return err;
}

static Item js_stream_make_type_error_with_code(const char* code, const char* message) {
    Item err = js_new_error_with_name(make_string_item("TypeError"), make_string_item(message));
    js_set_key_default(err, make_string_item("code"), make_string_item(code));
    return err;
}

static Item js_stream_call_callback_error_tick(Item callback, Item err) {
    if (js_is_callable(callback)) {
        js_call_function(callback, ItemNull, &err, 1);
    }
    return make_js_undefined();
}

JS_STREAM_ENV_BINARY_CLOSURE(js_stream_call_callback_error_tick_closure,
    js_stream_call_callback_error_tick)

static void js_stream_schedule_callback_error(Item callback, Item err) {
    if (!js_is_callable(callback)) return;
    Item* env = js_alloc_env(2);
    env[0] = callback;
    env[1] = err;
    Item tick = js_new_native_closure(js_stream_call_callback_error_tick_closure, 0, env, 2);
    js_next_tick_enqueue(tick);
}

static void js_stream_add_writable_end_callback(Item self, Item callback) {
    if (!js_is_callable(callback)) return;
    Item callbacks = js_get_key_default(self, make_string_item("__writable_end_callbacks__"));
    if (get_type_id(callbacks) != LMD_TYPE_ARRAY) {
        callbacks = js_array_new(0);
        js_set_key_default(self, make_string_item("__writable_end_callbacks__"), callbacks);
    }
    js_array_push(callbacks, callback);
}

static void js_stream_call_writable_end_callbacks(Item self, Item err) {
    Item callbacks = js_get_key_default(self, make_string_item("__writable_end_callbacks__"));
    if (get_type_id(callbacks) != LMD_TYPE_ARRAY) return;
    js_set_key_default(self, make_string_item("__writable_end_callbacks__"), js_array_new(0));
    bool has_error = js_stream_has_error(err);
    Item null_arg = ItemNull;
    int64_t len = js_array_length(callbacks);
    for (int64_t i = 0; i < len; i++) {
        Item callback = js_elements_get_int(callbacks, i);
        if (!js_is_callable(callback)) continue;
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
        js_set_key_default(self, make_string_item("__writable_end_pending__"), js_bool_item(false));
        js_stream_call_writable_end_callbacks(self, err);
        js_stream_schedule_error(self, err);
        return make_js_undefined();
    }

    if (js_item_is_true(js_get_key_default(self, key_destroyed))) {
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
JS_FORWARD_STATIC_ITEM(js_stream_final_callback_once, (Item env_item, Item err), js_stream_once_callback, (env_item, err, js_stream_finish_after_final))

static Item js_stream_make_final_callback(Item self, Item callback) {
    Item* env = js_stream_alloc_once_callback_env(self, callback);
    return js_new_native_closure(js_stream_final_callback_once, 1, env, 3);
}

static Item js_stream_complete_finish_tick(Item self) {
    ensure_keys();
    if (js_item_is_true(js_get_key_default(self, key_destroyed)) ||
        js_stream_has_stored_error(self)) {
        return make_js_undefined();
    }
    js_stream_mark_writable_finished(self);
    js_stream_call_writable_end_callbacks(self, make_js_undefined());
    js_stream_schedule_finish(self);
    return make_js_undefined();
}

JS_STREAM_ENV_UNARY_CLOSURE(js_stream_complete_finish_tick_closure,
    js_stream_complete_finish_tick)
JS_FORWARD_STATIC_VOID( js_stream_schedule_finish_ready, (Item self), js_stream_schedule_unary, (self, js_stream_complete_finish_tick_closure))

#undef JS_STREAM_ENV_BINARY_CLOSURE
#undef JS_STREAM_ENV_UNARY_CLOSURE

static void js_writable_finish_now(Item self, Item callback) {
    ensure_keys();
    if (js_item_is_true(js_get_key_default(self, key_destroyed)) ||
        js_item_is_true(js_get_key_default(self, key_finish_emitted)) ||
        js_stream_has_stored_error(self) ||
        js_state_get_bool(js_get_key_default(self, key_writable_state), "finished")) {
        return;
    }

    js_set_key_default(self, make_string_item("__writable_end_pending__"), js_bool_item(false));

    Item final_fn = js_get_key_default(self, make_string_item("_final"));
    if (js_is_callable(final_fn)) {
        Item final_cb = js_stream_make_final_callback(self, callback);
        Item final_result = js_call_function(final_fn, self, &final_cb, 1);
        if (item_is_error(final_result)) {
            Item err = js_error_lane_payload(final_result);
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
    if (!js_item_is_true(js_get_key_default(self, make_string_item("__writable_end_pending__")))) return;
    if (js_item_is_true(js_get_key_default(self, make_string_item("_writing")))) return;
    if (js_stream_pending_writes_count(self) > 0) {
        js_stream_flush_pending_writes(self);
        if (js_item_is_true(js_get_key_default(self, make_string_item("_writing"))) ||
            js_stream_pending_writes_count(self) > 0) {
            return;
        }
    }
    js_writable_finish_now(self, make_js_undefined());
}

// end([chunk][, callback]) — signal end of writes
extern "C" Item js_writable_end(Item self, Item chunk, Item callback) {
    ensure_keys();
    if (js_is_callable(chunk) &&
        (callback.item == 0 || get_type_id(callback) == LMD_TYPE_UNDEFINED)) {
        callback = chunk;
        chunk = make_js_undefined();
    }

    if (js_item_is_true(js_get_key_default(self, key_finish_emitted))) {
        Item err = js_stream_make_error_with_code("ERR_STREAM_ALREADY_FINISHED",
            "Cannot call end after a stream was finished");
        js_stream_schedule_callback_error(callback, err);
        return self;
    }

    Item wstate = js_get_key_default(self, key_writable_state);
    if (js_state_get_bool(wstate, "ended")) {
        js_stream_add_writable_end_callback(self, callback);
        if (chunk.item != 0 && get_type_id(chunk) != LMD_TYPE_UNDEFINED &&
            get_type_id(chunk) != LMD_TYPE_NULL) {
            Item err = js_stream_make_error_with_code("ERR_STREAM_WRITE_AFTER_END",
                "write after end");
            js_set_key_default(self, make_string_item("__writable_end_pending__"), js_bool_item(false));
            js_stream_mark_destroyed(self);
            js_stream_call_writable_end_callbacks(self, err);
            js_stream_schedule_error(self, err);
        }
        return self;
    }

    // write final chunk if provided
    if (chunk.item != 0 && get_type_id(chunk) != LMD_TYPE_UNDEFINED &&
        get_type_id(chunk) != LMD_TYPE_NULL) {
        Item write_result = js_writable_write(self, chunk, make_js_undefined(), make_js_undefined());
        // if write threw (e.g. ERR_METHOD_NOT_IMPLEMENTED), propagate the exception
        if (item_is_error(write_result)) return write_result;
        if (js_stream_has_stored_error(self)) return self;
    }

    // uncork all if corked
    Item corked = js_get_key_default(self, make_string_item("_corked"));
    if (get_type_id(corked) == LMD_TYPE_INT && it2i(corked) > 0) {
        js_stream_set_writable_corked(self, 0);
        Item pending = js_get_key_default(self, make_string_item("_pendingWrites"));
        if (get_type_id(pending) == LMD_TYPE_ARRAY && js_array_length(pending) > 0) {
            js_stream_flush_pending_writes(self);
            if (js_stream_has_stored_error(self)) return self;
        }
    }

    js_stream_mark_writable_ended(self);

    js_stream_add_writable_end_callback(self, callback);
    js_set_key_default(self, make_string_item("__writable_end_pending__"), js_bool_item(true));
    js_writable_maybe_finish_deferred(self);
    return self;
}

// cork() — buffer writes
extern "C" Item js_writable_cork(Item self) {
    ensure_keys();
    Item count = js_get_key_default(self, make_string_item("_corked"));
    int64_t c = (get_type_id(count) == LMD_TYPE_INT) ? it2i(count) : 0;
    js_stream_set_writable_corked(self, c + 1);
    return make_js_undefined();
}

// uncork() — flush corked writes
extern "C" Item js_writable_uncork(Item self) {
    ensure_keys();
    Item count = js_get_key_default(self, make_string_item("_corked"));
    int64_t c = (get_type_id(count) == LMD_TYPE_INT) ? it2i(count) : 0;
    if (c > 0) c--;
    js_stream_set_writable_corked(self, c);
    // when fully uncorked, flush pending writes
    if (c == 0) {
        Item pending = js_get_key_default(self, make_string_item("_pendingWrites"));
        if (get_type_id(pending) == LMD_TYPE_ARRAY && js_array_length(pending) > 0) {
            js_stream_flush_pending_writes(self);
        }
    }
    return make_js_undefined();
}

static void js_stream_install_event_methods(Item obj) {
    js_stream_set_default_method(obj, "on", js_stream_inst_on);
    js_stream_set_default_method(obj, "once", js_stream_inst_once);
    Item off_fn = js_new_native_function(js_stream_inst_off);
    js_set_key_default(obj, make_string_item("off"), off_fn);
    js_set_key_default(obj, make_string_item("removeListener"), off_fn);
    js_stream_set_default_method(obj, "removeAllListeners", js_stream_inst_removeAllListeners);
    js_stream_set_default_method(obj, "emit", js_stream_inst_emit);
    js_stream_set_default_method(obj, "eventNames", js_stream_inst_eventNames);
    js_stream_set_default_method(obj, "listeners", js_stream_inst_listeners);
    js_stream_set_default_method(obj, "listenerCount", js_stream_inst_listenerCount);
}

static void js_stream_install_writable_methods(Item obj, JsNativeP3 write_target,
        JsNativeP2 end_target) {
    js_stream_set_default_method(obj, "write", write_target);
    js_stream_set_default_method(obj, "end", end_target);
    js_stream_set_default_method(obj, "destroy", js_stream_inst_destroy);
    js_stream_set_default_method(obj, "_undestroy", js_stream_inst_undestroy);
    js_stream_set_default_method(obj, "cork", js_writable_inst_cork);
    js_stream_set_default_method(obj, "uncork", js_writable_inst_uncork);
    js_stream_set_default_method(obj, "setEncoding", js_stream_inst_setEncoding);
    js_stream_set_default_method(obj, "setDefaultEncoding", js_stream_inst_setDefaultEncoding);
}

static void js_stream_install_readable_methods(Item obj) {
    js_stream_set_default_method(obj, "push", js_readable_inst_push);
    js_stream_set_default_method(obj, "unshift", js_readable_inst_unshift);
    js_stream_set_default_method(obj, "read", js_readable_inst_read);
    js_stream_set_default_method(obj, "pipe", js_readable_inst_pipe);
    js_stream_set_default_method(obj, "unpipe", js_readable_inst_unpipe);
    js_stream_set_default_method(obj, "resume", js_readable_inst_resume);
    js_stream_set_default_method(obj, "pause", js_readable_inst_pause);
    js_stream_set_default_method(obj, "isPaused", js_readable_inst_isPaused);
}

// Writable constructor
extern "C" Item js_writable_new(Item opts) {
    ensure_keys();
    Item obj = js_stream_create_instance(stream_writable_prototype,
        JS_CLASS_WRITABLE);

    js_set_key_default(obj, key_writable, js_bool_item(true));
    js_set_key_default(obj, key_writable_side_enabled, js_bool_item(true));
    js_set_key_default(obj, key_finished, js_bool_item(false));
    js_set_key_default(obj, key_destroyed, js_bool_item(false));
    js_set_key_default(obj, make_string_item("destroyed"), js_bool_item(false));
    js_set_key_default(obj, make_string_item("errored"), ItemNull);
    js_set_key_default(obj, make_string_item("writableAborted"), js_bool_item(false));
    js_set_key_default(obj, key_finish_emitted, js_bool_item(false));
    js_set_key_default(obj, key_close_emitted, js_bool_item(false));
    js_set_key_default(obj, key_closed, js_bool_item(false));
    js_set_key_default(obj, key_auto_destroy, js_bool_item(true));
    js_set_key_default(obj, key_writable_state, js_create_writable_state(obj));
    js_stream_set_writable_corked(obj, 0);
    js_stream_set_buffered_request_count(obj, 0);
    js_stream_define_bool(obj, "writableEnded", false);
    js_stream_define_bool(obj, "writableFinished", false);
    Item listeners = js_new_object();
    js_set_key_default(obj, key_listeners, listeners);
    js_set_key_default(obj, make_string_item("_events"), js_new_object());
    js_stream_init_writable_options(obj);

    js_stream_install_event_methods(obj);
    js_stream_install_writable_methods(obj, js_writable_inst_write, js_writable_inst_end);

    JS_RETURN_IF_ERROR(propagate_stream_options(obj, opts));
    js_stream_call_construct(obj);
    return obj;
}
JS_FORWARD_ITEM(js_readable_new, (Item opts), js_readable_new_internal, (opts, JS_CLASS_READABLE))
JS_FORWARD_ITEM(js_readable_new_with_class, (Item opts, int class_id), js_readable_new_internal, (opts, (JsClass)class_id))

// =============================================================================
// Duplex stream (Readable + Writable)
// =============================================================================

static void js_stream_init_duplex_like(Item obj, bool transform) {
    js_set_key_default(obj, key_readable, js_bool_item(true));
    js_set_key_default(obj, key_writable, js_bool_item(true));
    js_set_key_default(obj, key_readable_side_enabled, js_bool_item(true));
    js_set_key_default(obj, key_writable_side_enabled, js_bool_item(true));
    js_stream_set_flowing(obj, false);
    js_set_key_default(obj, key_ended, js_bool_item(false));
    js_set_key_default(obj, key_finished, js_bool_item(false));
    js_set_key_default(obj, key_destroyed, js_bool_item(false));
    js_set_key_default(obj, make_string_item("destroyed"), js_bool_item(false));
    js_set_key_default(obj, make_string_item("errored"), ItemNull);
    js_set_key_default(obj, make_string_item("readableAborted"), js_bool_item(false));
    js_set_key_default(obj, make_string_item("writableAborted"), js_bool_item(false));
    js_set_key_default(obj, key_finish_emitted, js_bool_item(false));
    js_set_key_default(obj, key_end_pending, js_bool_item(false));
    js_set_key_default(obj, key_end_emitted, js_bool_item(false));
    js_set_key_default(obj, key_reading, js_bool_item(false));
    js_set_key_default(obj, key_paused, js_bool_item(false));
    js_set_key_default(obj, key_close_emitted, js_bool_item(false));
    js_set_key_default(obj, key_closed, js_bool_item(false));
    js_set_key_default(obj, key_auto_destroy, js_bool_item(true));
    js_set_key_default(obj, make_string_item("allowHalfOpen"), js_bool_item(true));
    js_set_key_default(obj, key_readable_state, js_create_readable_state());
    js_set_key_default(obj, key_writable_state, js_create_writable_state(obj));
    js_stream_set_writable_corked(obj, 0);
    js_stream_set_buffered_request_count(obj, 0);
    js_stream_define_bool(obj, "readableEnded", false);
    js_stream_define_bool(obj, "writableEnded", false);
    js_stream_define_bool(obj, "writableFinished", false);
    js_stream_set_readable_buffer(obj, js_array_new(0));
    Item listeners = js_new_object();
    js_set_key_default(obj, key_listeners, listeners);
    js_set_key_default(obj, make_string_item("_events"), js_new_object());
    js_stream_init_readable_options(obj);
    js_stream_init_writable_options(obj);

    js_stream_install_event_methods(obj);
    js_stream_install_readable_methods(obj);

    JsNativeP3 write_target = transform ? js_transform_inst_write :
        js_writable_inst_write;
    JsNativeP2 end_target = transform ? js_transform_inst_end :
        js_writable_inst_end;
    js_stream_install_writable_methods(obj, write_target, end_target);
    js_stream_install_async_iterator(obj);
    js_stream_install_readable_helpers(obj);
}

extern "C" Item js_duplex_new(Item opts) {
    ensure_keys();
    Item obj = js_stream_create_instance(stream_duplex_prototype,
        JS_CLASS_DUPLEX);
    js_stream_init_duplex_like(obj, false);

    JS_RETURN_IF_ERROR(propagate_stream_options(obj, opts));
    js_stream_call_construct(obj);
    return obj;
}

static Item js_stream_duplex_pair_write(Item env_item, Item chunk, Item encoding, Item callback) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item peer = env[0];
    JS_ASSIGN_OR_RETURN(push_result, js_readable_push_encoded(peer, chunk, encoding));
    if (js_is_callable(callback)) {
        js_call_function(callback, make_js_undefined(), NULL, 0);
    }
    return make_js_undefined();
}

static Item js_stream_duplex_pair_final(Item env_item, Item callback) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item peer = env[0];
    JS_ASSIGN_OR_RETURN(push_result, js_readable_push(peer, ItemNull));
    if (js_is_callable(callback)) {
        js_call_function(callback, make_js_undefined(), NULL, 0);
    }
    return make_js_undefined();
}
JS_FORWARD_STATIC_ITEM(js_stream_duplex_pair_read, (void), make_js_undefined, ())

static void js_stream_duplex_pair_attach(Item endpoint, Item peer) {
    Item* env = js_alloc_env(1);
    env[0] = peer;
    js_set_key_default(endpoint, make_string_item("_write"),
                    js_new_native_closure(js_stream_duplex_pair_write, 3, env, 1));
    js_set_key_default(endpoint, make_string_item("_final"),
                    js_new_native_closure(js_stream_duplex_pair_final, 1, env, 1));
    js_set_native_key(endpoint, make_string_item("_read"), js_stream_duplex_pair_read);
}

static Item js_stream_duplex_pair(void) {
    ensure_keys();
    Item opts = js_new_object();
    JS_ASSIGN_OR_RETURN(first, js_duplex_new(opts));
    JS_ASSIGN_OR_RETURN(second, js_duplex_new(opts));

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
    if (js_is_callable(callback)) {
        if (js_stream_has_error(err)) {
            js_call_function(callback, make_js_undefined(), &err, 1);
        } else {
            js_call_function(callback, make_js_undefined(), NULL, 0);
        }
    }
    return make_js_undefined();
}

static Item js_duplex_from_make_forward_callback(Item callback) {
    if (!js_is_callable(callback)) return callback;
    Item* env = js_alloc_env(2);
    env[0] = callback;
    env[1] = js_bool_item(false);
    return js_new_native_closure(js_duplex_from_forward_callback_once, 1, env, 2);
}

static void js_duplex_from_attach_readable(Item duplex, Item readable, Item writable) {
    Item* env = js_alloc_env(2);
    env[0] = duplex;
    env[1] = writable;
    js_stream_on(readable, make_string_item("data"),
                 js_new_native_closure(js_duplex_from_readable_data, 1, env, 2));
    js_stream_on(readable, make_string_item("end"),
                 js_new_native_closure(js_duplex_from_readable_end, 0, env, 2));
    js_stream_on(readable, make_string_item("error"),
                 js_new_native_closure(js_duplex_from_forward_error, 1, env, 2));
}

static Item js_duplex_from_writable_write(Item env_item, Item chunk, Item encoding, Item callback) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item write_fn = js_get_key_default(env[0], key_write);
    if (!js_is_callable(write_fn)) {
        if (js_is_callable(callback))
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
    Item end_fn = js_get_key_default(env[0], key_end);
    if (js_is_callable(end_fn)) {
        Item args[1] = { js_duplex_from_make_forward_callback(callback) };
        js_call_function(end_fn, env[0], args, 1);
    } else if (js_is_callable(callback)) {
        js_call_function(callback, make_js_undefined(), NULL, 0);
    }
    return make_js_undefined();
}

static void js_duplex_from_attach_writable(Item duplex, Item writable) {
    Item* env = js_alloc_env(1);
    env[0] = writable;
    js_set_key_default(duplex, make_string_item("_write"),
                    js_new_native_closure(js_duplex_from_writable_write, 3, env, 1));
    js_set_key_default(duplex, make_string_item("_final"),
                    js_new_native_closure(js_duplex_from_writable_final, 1, env, 1));

    Item* err_env = js_alloc_env(2);
    err_env[0] = duplex;
    err_env[1] = ItemNull;
    js_stream_on(writable, make_string_item("error"),
                 js_new_native_closure(js_duplex_from_forward_error, 1, err_env, 2));
}

static Item js_duplex_from_destroy(Item env_item, Item err, Item callback) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item readable = env[0];
    Item writable = env[1];
    Item duplex = env[2];
    if (js_stream_is_stream_like(readable) &&
        !js_item_is_true(js_get_key_default(readable, key_destroyed))) {
        js_stream_destroy(readable, make_js_undefined());
    }
    if (writable.item != readable.item &&
        js_stream_is_stream_like(writable) &&
        !js_item_is_true(js_get_key_default(writable, key_destroyed))) {
        js_stream_destroy(writable, make_js_undefined());
    }
    if (js_is_callable(callback)) {
        Item readable_state = js_get_key_default(duplex, key_readable_state);
        Item writable_state = js_get_key_default(duplex, key_writable_state);
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
    js_set_key_default(opts, make_string_item("readable"), js_bool_item(true));
    js_set_key_default(opts, make_string_item("writable"), js_bool_item(false));
    JS_ASSIGN_OR_RETURN(duplex, js_duplex_new(opts));
    js_set_native_key(duplex, make_string_item("_read"), js_stream_duplex_pair_read);

    Item* env = js_alloc_env(1);
    env[0] = duplex;
    Item on_fulfilled = js_new_native_closure(js_duplex_from_promise_fulfilled, 1, env, 1);
    Item on_rejected = js_new_native_closure(js_duplex_from_promise_rejected, 1, env, 1);
    js_promise_then(promise, on_fulfilled, on_rejected);
    return duplex;
}

static Item js_duplex_from_readable_value(Item value) {
    Item opts = js_new_object();
    js_set_key_default(opts, make_string_item("readable"), js_bool_item(true));
    js_set_key_default(opts, make_string_item("writable"), js_bool_item(false));
    JS_ASSIGN_OR_RETURN(duplex, js_duplex_new(opts));
    js_set_native_key(duplex, make_string_item("_read"), js_stream_duplex_pair_read);
    if (get_type_id(value) != LMD_TYPE_UNDEFINED && get_type_id(value) != LMD_TYPE_NULL) {
        js_readable_push(duplex, value);
    }
    js_readable_push(duplex, ItemNull);
    return duplex;
}

static Item js_duplex_from_blob(Item blob) {
    Item text = js_get_key_default(blob, make_string_item("_text"));
    if (get_type_id(text) != LMD_TYPE_STRING) {
        return js_duplex_from_readable_value(make_js_undefined());
    }
    String* str = it2s(text);
    int len = str ? (int)str->len : 0;
    Item array_buffer = js_arraybuffer_new(len);
    if (len > 0 && get_type_id(array_buffer) == LMD_TYPE_MAP) {
        JsArrayBuffer* ab = js_get_arraybuffer_ptr_item(array_buffer);
        uint8_t* destination = js_arraybuffer_prepare_write(ab);
        if (destination) memcpy(destination, str->chars, (size_t)len);
    }
    return js_duplex_from_readable_value(array_buffer);
}

static Item js_duplex_from_web_readable(Item readable_stream) {
    Item node_readable = js_get_key_default(readable_stream, make_string_item("__node_readable__"));
    if (js_stream_is_stream_like(node_readable)) return js_duplex_from(node_readable);

    Item opts = js_new_object();
    js_set_key_default(opts, make_string_item("readable"), js_bool_item(true));
    js_set_key_default(opts, make_string_item("writable"), js_bool_item(false));
    JS_ASSIGN_OR_RETURN(duplex, js_duplex_new(opts));
    js_set_native_key(duplex, make_string_item("_read"), js_stream_duplex_pair_read);

    Item chunks = js_get_key_default(readable_stream, make_string_item("__chunks__"));
    if (get_type_id(chunks) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(chunks);
        for (int64_t i = 0; i < len; i++) {
            js_readable_push(duplex, js_elements_get_int(chunks, i));
        }
    }
    js_readable_push(duplex, ItemNull);
    return duplex;
}

static Item js_duplex_from_web_writable_write(Item env_item, Item chunk, Item encoding, Item callback) {
    (void)encoding;
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item sink = js_get_key_default(env[0], make_string_item("__sink__"));
    if (get_type_id(sink) != LMD_TYPE_MAP && get_type_id(sink) != LMD_TYPE_ELEMENT) {
        sink = js_get_key_default(env[0], make_string_item("__writer__"));
    }
    Item write_fn = js_get_key_default(sink, key_write);
    if (!js_is_callable(write_fn)) {
        write_fn = js_get_key_default(sink, make_string_item("write"));
    }
    if (js_is_callable(write_fn)) {
        JS_ASSIGN_OR_RETURN(write_result, js_call_function(write_fn, sink, &chunk, 1));
    }
    if (js_is_callable(callback)) {
        js_call_function(callback, make_js_undefined(), NULL, 0);
    }
    return make_js_undefined();
}

static Item js_duplex_from_web_writable_final(Item env_item, Item callback) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item sink = js_get_key_default(env[0], make_string_item("__sink__"));
    if (get_type_id(sink) != LMD_TYPE_MAP && get_type_id(sink) != LMD_TYPE_ELEMENT) {
        sink = js_get_key_default(env[0], make_string_item("__writer__"));
    }
    Item close_fn = js_get_key_default(sink, make_string_item("close"));
    if (js_is_callable(close_fn)) {
        JS_ASSIGN_OR_RETURN(close_result, js_call_function(close_fn, sink, NULL, 0));
    }
    if (js_is_callable(callback)) {
        js_call_function(callback, make_js_undefined(), NULL, 0);
    }
    return make_js_undefined();
}

static Item js_duplex_from_web_writable(Item writable_stream) {
    Item node_writable = js_get_key_default(writable_stream, make_string_item("__node_stream__"));
    if (js_stream_is_stream_like(node_writable)) return js_duplex_from(node_writable);

    Item opts = js_new_object();
    js_set_key_default(opts, make_string_item("readable"), js_bool_item(false));
    js_set_key_default(opts, make_string_item("writable"), js_bool_item(true));
    JS_ASSIGN_OR_RETURN(duplex, js_duplex_new(opts));
    Item* env = js_alloc_env(1);
    env[0] = writable_stream;
    js_set_key_default(duplex, make_string_item("_write"),
                    js_new_native_closure(js_duplex_from_web_writable_write, 3, env, 1));
    js_set_key_default(duplex, make_string_item("_final"),
                    js_new_native_closure(js_duplex_from_web_writable_final, 1, env, 1));
    return duplex;
}

static Item js_duplex_from_function(Item fn) {
    Item opts = js_new_object();
    js_set_key_default(opts, make_string_item("objectMode"), js_bool_item(true));
    JS_ASSIGN_OR_RETURN(input, js_passthrough_new(opts));

    Item args[1] = { input };
    JS_ASSIGN_OR_RETURN(result, js_call_function(fn, make_js_undefined(), args, 1));
    if (get_type_id(result) == LMD_TYPE_UNDEFINED) {
        return js_throw_type_error_code("ERR_INVALID_RETURN_VALUE",
            "Expected a stream, iterable, or promise to be returned from the function");
    }

    Item then_fn = js_get_key_default(result, make_string_item("then"));
    JS_ASSIGN_OR_RETURN(readable, js_is_callable(then_fn)
        ? js_duplex_from_promise(js_promise_resolve(result))
        : js_readable_compose_from_result(input, result, make_js_undefined()));
    Item pair = js_new_object();
    js_set_key_default(pair, make_string_item("readable"), readable);
    js_set_key_default(pair, make_string_item("writable"), input);
    return js_duplex_from(pair);
}

static Item js_duplex_from(Item source) {
    ensure_keys();
    if (js_is_callable(source)) {
        return js_duplex_from_function(source);
    }

    Item then_fn = js_get_key_default(source, make_string_item("then"));
    if (js_is_callable(then_fn)) {
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
        Item candidate_readable = js_get_key_default(source, make_string_item("readable"));
        Item candidate_writable = js_get_key_default(source, make_string_item("writable"));
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
        js_set_key_default(pair, make_string_item("readable"), readable_duplex);
        js_set_key_default(pair, make_string_item("writable"), writable_duplex);
        return js_duplex_from(pair);
    }

    bool has_readable = js_stream_is_stream_like(readable) && js_stream_has_readable_side(readable);
    bool has_writable = js_stream_is_stream_like(writable) && js_stream_has_writable_side(writable);
    if (!has_readable && !has_writable) {
        return js_throw_invalid_arg_type("body", "Stream", source);
    }

    Item opts = js_new_object();
    js_set_key_default(opts, make_string_item("readable"), js_bool_item(has_readable));
    js_set_key_default(opts, make_string_item("writable"), js_bool_item(has_writable));
    if (has_readable) {
        Item readable_state = js_get_key_default(readable, key_readable_state);
        js_set_key_default(opts, make_string_item("readableObjectMode"),
                        js_bool_item(js_state_get_bool(readable_state, "objectMode")));
    }
    if (has_writable) {
        Item writable_state = js_get_key_default(writable, key_writable_state);
        js_set_key_default(opts, make_string_item("writableObjectMode"),
                        js_bool_item(js_state_get_bool(writable_state, "objectMode")));
    }
    JS_ASSIGN_OR_RETURN(duplex, js_duplex_new(opts));
    js_set_native_key(duplex, make_string_item("_read"), js_stream_duplex_pair_read);

    if (has_readable) js_duplex_from_attach_readable(duplex, readable, has_writable ? writable : ItemNull);
    if (has_writable) js_duplex_from_attach_writable(duplex, writable);
    Item* destroy_env = js_alloc_env(3);
    destroy_env[0] = readable;
    destroy_env[1] = writable;
    destroy_env[2] = duplex;
    js_set_key_default(duplex, make_string_item("_destroy"),
                    js_new_native_closure(js_duplex_from_destroy, 2, destroy_env, 3));
    return duplex;
}

// =============================================================================
// Transform stream (Duplex with _transform)
// =============================================================================

static Item js_transform_finish_after_flush(Item self, Item callback, Item err) {
    ensure_keys();
    js_set_key_default(self, make_string_item("__transform_end_pending__"), js_bool_item(false));
    if (js_stream_has_error(err)) {
        if (js_is_callable(callback)) {
            js_call_function(callback, self, &err, 1);
        }
        js_stream_schedule_error(self, err);
        return make_js_undefined();
    }
    js_stream_mark_writable_ended(self);
    stream_emit(self, "prefinish", NULL, 0);
    js_stream_mark_writable_finished(self);
    js_readable_push(self, ItemNull);
    if (js_is_callable(callback)) {
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
    Item flush_fn = js_get_key_default(self, make_string_item("_flush"));
    if (js_is_callable(flush_fn)) {
        Item bound_args[2] = { self, callback };
        Item flush_cb = js_bind_function(js_new_native_function(js_transform_finish_after_flush),
                                         make_js_undefined(), bound_args, 2);
        js_call_function(flush_fn, self, &flush_cb, 1);
        return make_js_undefined();
    }
    return js_transform_finish_after_flush(self, callback, make_js_undefined());
}

static void js_transform_finish_now(Item self, Item callback) {
    ensure_keys();
    if (js_item_is_true(js_get_key_default(self, key_finish_emitted)) ||
        js_item_is_true(js_get_key_default(self, key_finished))) {
        return;
    }
    Item final_fn = js_get_key_default(self, make_string_item("_final"));
    if (js_is_callable(final_fn)) {
        Item bound_args[2] = { self, callback };
        Item final_cb = js_bind_function(js_new_native_function(js_transform_finish_after_final),
                                         make_js_undefined(), bound_args, 2);
        js_call_function(final_fn, self, &final_cb, 1);
        return;
    }
    js_transform_finish_after_final(self, callback, make_js_undefined());
}

static void js_transform_maybe_finish_deferred(Item self) {
    ensure_keys();
    if (!js_item_is_true(js_get_key_default(self, make_string_item("__transform_end_pending__")))) return;
    if (js_item_is_true(js_get_key_default(self, make_string_item("_writing")))) return;
    if (js_stream_pending_writes_count(self) > 0) return;
    Item callback = js_get_key_default(self, make_string_item("__transform_end_callback__"));
    js_transform_finish_now(self, callback);
}

// _transform(chunk, encoding, callback) override
extern "C" Item js_transform_write(Item self, Item chunk, Item encoding, Item callback) {
    ensure_keys();
    if (js_is_callable(encoding) &&
        (callback.item == 0 || get_type_id(callback) == LMD_TYPE_UNDEFINED)) {
        callback = encoding;
        encoding = make_js_undefined();
    }

    bool write_in_progress = js_item_is_true(js_get_key_default(self, make_string_item("_writing")));
    Item corked = js_get_key_default(self, make_string_item("_corked"));
    if ((get_type_id(corked) == LMD_TYPE_INT && it2i(corked) > 0) || write_in_progress) {
        Item validation = js_stream_validate_writable_chunk(self, chunk);
        if (item_is_error(validation)) {
            Item err = js_error_lane_payload(validation);
            js_stream_schedule_callback_error(callback, err);
            js_stream_schedule_error(self, err);
            return js_bool_item(false);
        }
        Item preparation = js_stream_prepare_writable_chunk(self, &chunk, &encoding);
        if (item_is_error(preparation)) {
            Item err = js_error_lane_payload(preparation);
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
    Item transform_fn = js_get_key_default(self, make_string_item("_transform"));
    if (js_is_callable(transform_fn)) {
        Item validation = js_stream_validate_writable_chunk(self, chunk);
        if (item_is_error(validation)) {
            Item err = js_error_lane_payload(validation);
            js_stream_schedule_callback_error(callback, err);
            js_stream_schedule_error(self, err);
            return js_bool_item(false);
        }
        Item preparation = js_stream_prepare_writable_chunk(self, &chunk, &encoding);
        if (item_is_error(preparation)) {
            Item err = js_error_lane_payload(preparation);
            js_stream_schedule_callback_error(callback, err);
            js_stream_schedule_error(self, err);
            return js_bool_item(false);
        }
        bool accepted = js_stream_begin_write(self, chunk);
        Item write_cb = js_stream_make_transform_write_callback(self, callback);
        Item args[3] = {chunk, encoding, write_cb};
        js_set_key_default(self, make_string_item("_writing"), js_bool_item(true));
        Item result = js_call_function(transform_fn, self, args, 3);
        if (item_is_error(result)) {
            Item err = js_error_lane_payload(result);
            js_stream_after_write(self, callback, err);
            return js_bool_item(false);
        }
        // if _transform returns data, push it
        if (result.item != 0 && get_type_id(result) != LMD_TYPE_UNDEFINED) {
            Item push_result = js_readable_push(self, result);
            if (item_is_error(push_result)) {
                Item err = js_error_lane_payload(push_result);
                js_stream_after_write(self, callback, err);
                return js_bool_item(false);
            }
        }
        if (js_stream_mark_transform_readable_backpressure(self)) accepted = false;
        if (!accepted && !js_state_get_bool(js_get_key_default(self, key_writable_state), "needDrain") &&
            !js_item_is_true(js_get_key_default(self, make_string_item("_writing")))) {
            // synchronous _transform callbacks can clear needDrain before write() returns;
            // defer the drain so callers observe the backpressure edge they just caused.
            js_state_set_bool(js_get_key_default(self, key_writable_state), "needDrain", true);
            js_stream_defer_transform_drain(self);
        }
        return js_bool_item(accepted);
    } else {
        // no _transform method — throw ERR_METHOD_NOT_IMPLEMENTED
        return js_throw_error_with_code("ERR_METHOD_NOT_IMPLEMENTED",
                                        "The _transform() method is not implemented");
    }
}

extern "C" Item js_transform_end(Item self, Item chunk, Item callback) {
    ensure_keys();
    if (js_is_callable(chunk) &&
        (callback.item == 0 || get_type_id(callback) == LMD_TYPE_UNDEFINED)) {
        callback = chunk;
        chunk = make_js_undefined();
    }

    if (chunk.item != 0 && get_type_id(chunk) != LMD_TYPE_UNDEFINED &&
        get_type_id(chunk) != LMD_TYPE_NULL) {
        Item write_result = js_transform_write(self, chunk, make_js_undefined(),
                                               make_js_undefined());
        // if transform threw (e.g. ERR_METHOD_NOT_IMPLEMENTED), propagate the exception
        if (item_is_error(write_result)) return write_result;
    }

    js_set_key_default(self, make_string_item("__transform_end_pending__"), js_bool_item(true));
    js_set_key_default(self, make_string_item("__transform_end_callback__"), callback);
    js_transform_maybe_finish_deferred(self);
    return self;
}

static Item js_transform_new_internal(Item opts, JsClass class_id) {
    ensure_keys();
    Item obj = js_stream_create_instance(stream_transform_prototype, class_id);
    js_stream_init_duplex_like(obj, true);

    JS_RETURN_IF_ERROR(propagate_stream_options(obj, opts));
    js_stream_call_construct(obj);
    return obj;
}
JS_FORWARD_ITEM(js_transform_new, (Item opts), js_transform_new_internal, (opts, JS_CLASS_TRANSFORM))

// =============================================================================
// PassThrough — Transform that passes data unchanged
// =============================================================================

// PassThrough default _transform: just push data through
extern "C" Item js_passthrough_transform(Item chunk, Item encoding, Item callback) {
    (void)encoding;
    Item self = js_get_this();
    js_readable_push(self, chunk);
    if (js_is_callable(callback)) {
        js_call_function(callback, ItemNull, NULL, 0);
    }
    return make_js_undefined();
}

extern "C" Item js_passthrough_new(Item opts) {
    Item obj = js_transform_new_internal(opts, JS_CLASS_PASS_THROUGH);
    if (!js_stream_called_as_constructor() && js_stream_is_object_like(stream_passthrough_prototype)) {
        js_set_prototype(obj, stream_passthrough_prototype);
    }
    // set default _transform for pass-through behavior
    js_set_native_key(obj, make_string_item("_transform"), js_passthrough_transform);
    return obj;
}

// =============================================================================
// pipeline(source, ...transforms, destination, callback) — pipe chain
// =============================================================================

static bool js_stream_pipeline_source_ended(Item source) {
    ensure_keys();
    return js_item_is_true(js_get_key_default(source, key_end_emitted)) ||
           js_item_is_true(js_get_key_default(source, key_end_pending)) ||
           js_state_get_bool(js_get_key_default(source, key_readable_state), "endEmitted");
}

static Item js_stream_pipeline_pair_streams(Item source, Item dest) {
    Item streams = js_array_new(0);
    js_array_push(streams, source);
    js_array_push(streams, dest);
    return streams;
}
JS_FORWARD_STATIC_EXPRESSION(bool, js_stream_pipeline_is_undefined, (Item value), (value.item == 0 || value.item == ITEM_JS_UNDEFINED || get_type_id(value) == LMD_TYPE_UNDEFINED))

static bool js_stream_pipeline_is_readable_input(Item value) {
    TypeId type = get_type_id(value);
    return js_stream_is_stream_like(value) ||
           type == LMD_TYPE_ARRAY ||
           type == LMD_TYPE_STRING ||
           js_readable_from_is_iterable(value);
}
JS_FORWARD_STATIC_ITEM(js_stream_pipeline_invalid_return_value, (void), js_throw_type_error_code, ("ERR_INVALID_RETURN_VALUE", "Expected a stream, iterable, or promise to be returned from the function"))

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
        js_item_is_true(js_get_key_default(stream, key_destroyed))) {
        return false;
    }

    Item destroy = js_get_key_default(stream, key_destroy);
    if (js_is_callable(destroy)) {
        js_stream_mark_destroyed(stream);
        // legacy streams keep cleanup on public destroy(); stored-error streams
        // were skipped to avoid duplicate error emission, so invoke it directly.
        js_call_function(destroy, stream, NULL, 0);
        return true;
    }

    Item close = js_get_key_default(stream, make_string_item("close"));
    if (js_is_callable(close)) {
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
        Item stream = js_elements_get_int(streams, i);
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
        if (js_item_is_true(js_get_key_default(dest, key_readable))) {
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
            Item stream = js_elements_get_int(streams, i);
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
    if (!js_is_callable(callback)) return make_js_undefined();
    Item err = env[8];
    Item callback_result;
    if (js_stream_has_error(err)) {
        callback_result = js_call_function(callback, ItemNull, &err, 1);
    } else {
        callback_result = js_call_function(callback, ItemNull, NULL, 0);
    }
    if (item_is_error(callback_result)) {
        Item thrown = js_error_lane_payload(callback_result);
        JS_ASSIGN_OR_RETURN(emit_result, js_process_emit(make_string_item("uncaughtException"), thrown));
    }
    return make_js_undefined();
}

static Item js_stream_pipeline_call_once(Item* env, Item err) {
    if (!env || js_item_is_true(env[3])) return make_js_undefined();
    env[3] = js_bool_item(true);
    js_stream_pipeline_cleanup(env, js_stream_has_error(err));
    Item callback = env[2];
    if (!js_is_callable(callback)) return make_js_undefined();
    env[8] = err;
    js_next_tick_enqueue(js_new_native_closure(js_stream_pipeline_invoke_callback, 0, env, 10));
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
    if (js_is_callable(dest) && js_is_callable(callback)) {
        actual_dest = js_passthrough_new(make_js_undefined());
        if (get_type_id(streams) == LMD_TYPE_ARRAY && js_array_length(streams) >= 2) {
            js_elements_set_int(streams, js_array_length(streams) - 1, actual_dest);
        }
    }
    // simplified two-argument pipeline (most common case)
    // for multi-step, chain pipe() calls
    if (js_is_callable(callback)) {
        Item* env = js_alloc_env(10);
        env[0] = source;
        env[1] = actual_dest;
        env[2] = callback;
        env[3] = js_bool_item(false);
        Item source_close = js_new_native_closure(js_stream_pipeline_on_close, 0, env, 10);
        Item source_error = js_new_native_closure(js_stream_pipeline_on_error, 1, env, 10);
        Item dest_finish = js_new_native_closure(js_stream_pipeline_on_finish, 0, env, 10);
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
                js_stream_on(js_elements_get_int(streams, i), make_string_item("error"), source_error);
            }
        } else {
            js_stream_on(source, make_string_item("error"), source_error);
            js_stream_on(actual_dest, make_string_item("error"), source_error);
        }
        js_stream_on(actual_dest, make_string_item("finish"), dest_finish);
        if (js_item_is_true(js_get_key_default(actual_dest, key_readable)) &&
            !js_item_is_true(js_get_key_default(actual_dest, key_writable))) {
            js_stream_on(actual_dest, make_string_item("end"), dest_finish);
        }
    }
    Item pipe_fn = js_get_key_default(source, key_pipe);
    if (connect && js_is_callable(pipe_fn)) {
        js_call_function(pipe_fn, source, &actual_dest, 1);
    }
    return actual_dest;
}
JS_FORWARD_STATIC_ITEM(js_stream_pipeline_pair, (Item source, Item dest, Item callback), js_stream_pipeline_pair_impl, (source, dest, callback, true, js_stream_pipeline_pair_streams(source, dest)))

static Item js_stream_pipeline_function_sink_call_done(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[3])) return make_js_undefined();
    env[3] = js_bool_item(true);
    Item callback = env[1];
    if (!js_is_callable(callback)) return make_js_undefined();
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
JS_FORWARD_STATIC_ITEM(js_stream_pipeline_function_sink_on_error, (Item env_item, Item err), js_stream_pipeline_function_sink_call_done, (env_item, err))

static Item js_stream_pipeline_function_dest(Item source, Item dest, Item callback, Item streams) {
    Item args[1] = { source };
    Item result = js_call_function(dest, make_js_undefined(), args, 1);
    if (item_is_error(result)) {
        Item err = js_error_lane_payload(result);
        js_stream_pipeline_destroy_stream_array(streams, err);
        if (js_is_callable(callback)) {
            JS_ASSIGN_OR_RETURN(callback_result, js_call_function(callback, ItemNull, &err, 1));
        }
        return make_js_undefined();
    }

    if (js_stream_pipeline_is_undefined(result)) {
        return js_stream_pipeline_invalid_return_value();
    }

    Item then_fn = js_get_key_default(result, make_string_item("then"));
    if (js_is_callable(then_fn)) {
        Item* env = js_alloc_env(4);
        env[0] = make_js_undefined();
        env[1] = callback;
        env[2] = streams;
        env[3] = js_bool_item(false);
        Item on_done = js_new_native_closure(js_stream_pipeline_function_sink_call_done, 1, env, 4);
        Item on_error = js_new_native_closure(js_stream_pipeline_function_sink_on_error, 1, env, 4);
        js_promise_then(result, on_done, on_error);
        return make_js_undefined();
    }

    JS_ASSIGN_OR_RETURN(stream, js_stream_pipeline_to_stream(result));
    if (get_type_id(streams) == LMD_TYPE_ARRAY) js_array_push(streams, stream);
    return js_stream_pipeline_pair_impl(source, stream, callback, false, streams);
}

static Item js_stream_pipeline_prepare_source(Item source) {
    if (!js_stream_is_stream_like(source) && js_is_callable(source)) {
        JS_ASSIGN_OR_RETURN_INTO(source, js_call_function(source, make_js_undefined(), NULL, 0));
        JS_ASSIGN_OR_RETURN_INTO(source, js_stream_pipeline_to_stream(source));
        return source;
    }
    if (!js_stream_is_stream_like(source)) {
        JS_ASSIGN_OR_RETURN_INTO(source, js_readable_from(source));
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

    Item callback = js_elements_get_int(rest_args, argc - 1);
    if (!js_is_callable(callback)) {
        return js_throw_invalid_arg_type("callback", "function", callback);
    }
    if (argc < 3) {
        if (argc == 2) {
            Item first_arg = js_elements_get_int(rest_args, 0);
            if (get_type_id(first_arg) == LMD_TYPE_ARRAY) {
                int64_t array_len = js_array_length(first_arg);
                if (array_len >= 2) {
                    Item expanded = js_array_new(0);
                    for (int64_t i = 0; i < array_len; i++) {
                        js_array_push(expanded, js_elements_get_int(first_arg, i));
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
        Item source = js_elements_get_int(rest_args, 0);
        Item dest = js_elements_get_int(rest_args, 1);
        JS_ASSIGN_OR_RETURN_INTO(source, js_stream_pipeline_prepare_source(source));
        if (js_is_callable(dest)) {
            Item streams = js_array_new(0);
            js_array_push(streams, source);
            return js_stream_pipeline_function_dest(source, dest, callback, streams);
        }
        return js_stream_pipeline_pair(source, dest, callback);
    }

    Item first = js_elements_get_int(rest_args, 0);
    JS_ASSIGN_OR_RETURN_INTO(first, js_stream_pipeline_prepare_source(first));

    Item streams = js_array_new(0);
    js_array_push(streams, first);
    Item previous = first;
    for (int64_t i = 1; i < stream_count; i++) {
        Item dest = js_elements_get_int(rest_args, i);
        if (i == stream_count - 1 && js_is_callable(dest)) {
            return js_stream_pipeline_function_dest(previous, dest, callback, streams);
        }
        if (js_is_callable(dest)) {
            Item args[1] = { previous };
            JS_ASSIGN_OR_RETURN(result, js_call_function(dest, make_js_undefined(), args, 1));
            JS_ASSIGN_OR_RETURN_INTO(dest, js_stream_pipeline_to_stream(result));
            js_array_push(streams, dest);
            previous = dest;
            continue;
        }
        Item pipe_fn = js_get_key_default(previous, key_pipe);
        if (!js_is_callable(pipe_fn)) {
            return js_throw_invalid_arg_type("streams", "stream", previous);
        }
        JS_ASSIGN_OR_RETURN(pipe_result, js_call_function(pipe_fn, previous, &dest, 1));
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
    if (js_item_is_true(js_get_key_default(readable, key_destroyed))) {
        return make_js_undefined();
    }

    Item done = js_iterator_result_done(result);
    if (item_is_error(done)) {
        Item err = done;
        js_stream_destroy(readable, err);
        return make_js_undefined();
    }
    if (js_is_truthy(done)) {
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
    if (js_item_is_true(js_get_key_default(readable, key_destroyed))) {
        return make_js_undefined();
    }

    Item step = js_async_iterator_step_result(env[1]);
    if (item_is_error(step)) {
        Item err = js_error_lane_payload(step);
        js_stream_destroy(readable, err);
        return make_js_undefined();
    }
    step = js_promise_resolve(step);

    Item on_step = js_new_native_closure(js_readable_from_on_step, 1, env, 2);
    Item on_error = js_new_native_closure(js_readable_from_on_error, 1, env, 2);
    js_promise_then(step, on_step, on_error);
    return make_js_undefined();
}

static bool js_readable_from_is_iterable(Item value) {
    TypeId type = get_type_id(value);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_ELEMENT && type != LMD_TYPE_ARRAY) {
        return false;
    }
    Item async_iter = js_get_key_default(value, js_well_known_symbol_key(5));
    if (js_is_callable(async_iter)) return true;
    Item iter = js_get_key_default(value, js_well_known_symbol_key(1));
    return js_is_callable(iter);
}

// Readable.from(iterable) — create readable from iterable values
extern "C" Item js_readable_from(Item iterable) {
    ensure_keys();
    Item opts = js_new_object();
    js_set_key_default(opts, make_string_item("objectMode"), js_bool_item(true));
    Item readable = js_readable_new(opts);
    js_stream_set_readable_object_mode(readable, true);

    if (get_type_id(iterable) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(iterable);
        for (int64_t i = 0; i < len; i++) {
            Item value = js_elements_get_int(iterable, i);
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
        if (item_is_error(iterator)) {
            Item err = js_error_lane_payload(iterator);
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
JS_FORWARD_STATIC_ITEM(js_stream_finished_wrapper_key, (void), make_string_item, ("__lambda_stream_finished_context_callback__"))

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
    Item wrapper = js_new_native_closure(js_stream_finished_context_callback, 1, env, 3);
    js_set_key_default(callback, js_stream_finished_wrapper_key(), wrapper);
    return wrapper;
}

static bool js_stream_finished_side_done(Item stream, bool check_readable, bool check_writable) {
    if (js_item_is_true(js_get_key_default(stream, make_string_item("__compose_pending__")))) {
        return false;
    }
    if (!check_readable && !check_writable) return true;
    bool readable_done = true;
    bool writable_done = true;
    if (check_readable && js_stream_has_readable_side(stream) &&
        js_stream_readable_side_enabled(stream)) {
        bool native_readable_state =
            get_type_id(js_get_key_default(stream, key_readable_side_enabled)) == LMD_TYPE_BOOL ||
            get_type_id(js_get_key_default(stream, key_readable)) == LMD_TYPE_BOOL;
        readable_done = js_item_is_true(js_get_key_default(stream, key_end_emitted)) ||
                        (native_readable_state &&
                         js_state_get_bool(js_get_key_default(stream, key_readable_state), "endEmitted"));
    }
    if (check_writable && js_stream_has_writable_side(stream) &&
        js_stream_writable_side_enabled(stream)) {
        Item writable_state = js_get_key_default(stream, key_writable_state);
        bool native_writable_state =
            get_type_id(js_get_key_default(stream, key_writable_side_enabled)) == LMD_TYPE_BOOL ||
            get_type_id(js_get_key_default(stream, key_writable)) == LMD_TYPE_BOOL;
        bool legacy_no_pendingcb_done =
            native_writable_state &&
            js_state_get_bool(writable_state, "ended") &&
            !js_stream_writable_state_has_pendingcb(writable_state);
        writable_done = js_item_is_true(js_get_key_default(stream, key_finish_emitted)) ||
                        js_item_is_true(js_get_key_default(stream, key_finished)) ||
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
        bool readable_terminal = js_item_is_true(js_get_key_default(stream, key_end_emitted)) ||
                                 (!destroyed &&
                                  js_item_is_true(js_get_key_default(stream, key_end_pending))) ||
                                 js_state_get_bool(js_get_key_default(stream, key_readable_state), "endEmitted");
        if (!readable_terminal) return true;
    }
    if (check_writable && js_stream_has_writable_side(stream) &&
        js_stream_writable_side_enabled(stream)) {
        bool writable_terminal = js_item_is_true(js_get_key_default(stream, key_finish_emitted)) ||
                                 js_item_is_true(js_get_key_default(stream, key_finished));
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
    Item value = js_get_key_default(options, make_string_item(name));
    if (js_stream_finished_option_has_value(value)) {
        *check_side = js_stream_finished_option_to_bool(value);
    }
}

static Item js_stream_finished_abort_error(Item signal) {
    Item reason = js_get_key_default(signal, make_string_item("reason"));
    if (js_stream_has_error(reason)) return reason;
    return js_stream_iter_make_abort_error();
}

static Item js_stream_finished_options_sync_callback(Item options, bool* out_sync) {
    *out_sync = false;
    if (get_type_id(options) != LMD_TYPE_MAP && get_type_id(options) != LMD_TYPE_ELEMENT) {
        return js_status_ok();
    }

    JS_ASSIGN_OR_RETURN(symbols, js_object_get_own_property_symbols(options));
    if (get_type_id(symbols) != LMD_TYPE_ARRAY) return js_status_ok();

    int64_t len = js_array_length(symbols);
    for (int64_t i = 0; i < len; i++) {
        Item symbol = js_elements_get_int(symbols, i);
        JS_ASSIGN_OR_RETURN(description, js_symbol_get_description(symbol));
        if (js_stream_string_equals(description, "kEosNodeSynchronousCallback")) {
            JS_ASSIGN_OR_RETURN(value, js_get_key_default(options, symbol));
            *out_sync = js_item_is_true(value);
            return js_status_ok();
        }
    }
    return js_status_ok();
}

static bool js_stream_is_native_stream(Item stream) {
    JsClass cls = js_class_id(stream);
    return cls == JS_CLASS_READABLE || cls == JS_CLASS_WRITABLE ||
           cls == JS_CLASS_DUPLEX || cls == JS_CLASS_TRANSFORM ||
           cls == JS_CLASS_PASS_THROUGH;
}

static bool js_stream_finished_expects_close(Item stream) {
    if (js_item_is_true(js_get_key_default(stream, key_close_emitted)) ||
        js_item_is_true(js_get_key_default(stream, key_closed))) {
        return false;
    }
    Item auto_destroy = js_get_key_default(stream, key_auto_destroy);
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
    Item on_fn = js_get_key_default(stream, key_on);
    if (js_is_callable(on_fn)) {
        Item args[2] = {event_item, listener};
        js_call_function(on_fn, stream, args, 2);
    }
}

static void js_stream_finished_remove_listener(Item stream, Item event_item, Item listener) {
    if (js_stream_is_native_stream(stream)) {
        js_stream_off(stream, event_item, listener);
        return;
    }
    Item off_fn = js_get_key_default(stream, make_string_item("removeListener"));
    if (!js_is_callable(off_fn)) {
        off_fn = js_get_key_default(stream, make_string_item("off"));
    }
    if (js_is_callable(off_fn)) {
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
    Item remove_event = js_get_key_default(signal, make_string_item("removeEventListener"));
    if (js_is_callable(remove_event) &&
        js_is_callable(abort_listener)) {
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
    if (!js_is_callable(callback)) return make_js_undefined();
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
JS_FORWARD_STATIC_ITEM(js_stream_finished_on_finish, (Item env_item), js_stream_finished_on_end, (env_item))

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
    bool destroyed = js_item_is_true(js_get_key_default(env[0], key_destroyed));
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
    js_next_tick_enqueue(js_new_native_closure(js_stream_finished_emit_callback_tick, 0, env, 13));
}

static Item js_stream_finished_options_cleanup(Item options, bool* cleanup) {
    *cleanup = false;
    if (get_type_id(options) != LMD_TYPE_MAP && get_type_id(options) != LMD_TYPE_ELEMENT) {
        return js_status_ok();
    }
    JS_ASSIGN_OR_RETURN(cleanup_item, js_get_key_default(options, make_string_item("cleanup")));
    if (cleanup_item.item == 0 || get_type_id(cleanup_item) == LMD_TYPE_UNDEFINED) {
        return js_status_ok();
    }
    if (get_type_id(cleanup_item) != LMD_TYPE_BOOL) {
        return js_throw_invalid_arg_type("options.cleanup", "boolean", cleanup_item);
    }
    *cleanup = it2b(cleanup_item);
    return js_status_ok();
}

static Item js_stream_finished_impl(Item stream, Item options, Item callback) {
    ensure_keys();
    if (!js_is_callable(callback)) {
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
        signal = js_get_key_default(options, make_string_item("signal"));
        if (signal.item != 0 && get_type_id(signal) != LMD_TYPE_UNDEFINED &&
            get_type_id(signal) != LMD_TYPE_NULL && !js_stream_is_abort_signal(signal)) {
            return js_throw_invalid_arg_type("options.signal", "AbortSignal", signal);
        }
        JS_RETURN_IF_ERROR(js_stream_finished_options_cleanup(options, &cleanup));
        js_stream_finished_apply_side_option(options, "readable", &check_readable);
        js_stream_finished_apply_side_option(options, "writable", &check_writable);
    } else if (options.item != 0 && get_type_id(options) != LMD_TYPE_UNDEFINED &&
               get_type_id(options) != LMD_TYPE_NULL) {
        return js_throw_invalid_arg_type("options", "object", options);
    }

    Item registered_callback = js_stream_finished_context_wrapper(callback);
    bool sync_callback = false;
    JS_RETURN_IF_ERROR(js_stream_finished_options_sync_callback(options, &sync_callback));
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
    Item dispose = js_new_native_closure(js_stream_finished_dispose, 0, env, 13);

    if (js_stream_is_abort_signal(signal)) {
        Item aborted = js_get_key_default(signal, make_string_item("aborted"));
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
        Item add_event = js_get_key_default(signal, make_string_item("addEventListener"));
        if (js_is_callable(add_event)) {
            Item abort_listener = js_new_native_closure(js_stream_finished_on_abort, 0, env, 13);
            env[8] = abort_listener;
            Item args[2] = { make_string_item("abort"), abort_listener };
            js_call_function(add_event, signal, args, 2);
        }
    }

    // check if already finished/destroyed
    Item fin = js_get_key_default(stream, key_finished);
    Item des = js_get_key_default(stream, key_destroyed);
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
            js_item_is_true(js_get_key_default(stream, key_destroyed))) {
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

    env[3] = js_new_native_closure(js_stream_finished_on_end, 0, env, 13);
    env[4] = js_new_native_closure(js_stream_finished_on_finish, 0, env, 13);
    env[5] = js_new_native_closure(js_stream_finished_on_error, 1, env, 13);
    env[6] = js_new_native_closure(js_stream_finished_on_close, 0, env, 13);

    js_stream_finished_add_listener(stream, end_event, env[3]);
    js_stream_finished_add_listener(stream, finish_event, env[4]);
    js_stream_finished_add_listener(stream, error_event, env[5]);
    js_stream_finished_add_listener(stream, close_event, env[6]);

    return dispose;
}

static Item js_stream_finished_rest(Item rest_args) {
    int64_t argc = js_array_length(rest_args);
    if (argc < 2) {
        Item callback = argc > 1 ? js_elements_get_int(rest_args, 1) : make_js_undefined();
        return js_throw_invalid_arg_type("callback", "function", callback);
    }
    Item stream = js_elements_get_int(rest_args, 0);
    Item second = js_elements_get_int(rest_args, 1);
    if (js_is_callable(second)) {
        return js_stream_finished_impl(stream, make_js_undefined(), second);
    }
    Item options = second;
    Item callback = argc > 2 ? js_elements_get_int(rest_args, 2) : make_js_undefined();
    return js_stream_finished_impl(stream, options, callback);
}

static bool js_stream_is_abort_signal(Item signal) {
    if (js_class_id(signal) == JS_CLASS_ABORT_SIGNAL) return true;
    Item aborted = js_get_key_default(signal, make_string_item("aborted"));
    Item add_event = js_get_key_default(signal, make_string_item("addEventListener"));
    return get_type_id(aborted) == LMD_TYPE_BOOL && js_is_callable(add_event);
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
    Item on_fn = js_get_key_default(stream, key_on);
    if (!js_is_callable(on_fn)) return false;
    if (js_stream_has_readable_side(stream) || js_stream_has_writable_side(stream)) return true;
    Item pipe_fn = js_get_key_default(stream, key_pipe);
    Item destroy_fn = js_get_key_default(stream, key_destroy);
    return js_is_callable(pipe_fn) || js_is_callable(destroy_fn);
}

static Item js_stream_compose_normalize(Item stream) {
    if (js_is_callable(stream)) {
        Item opts = js_new_object();
        js_set_key_default(opts, make_string_item("objectMode"), js_bool_item(true));
        JS_ASSIGN_OR_RETURN(input, js_passthrough_new(opts));
        return js_readable_compose(input, stream, make_js_undefined());
    }
    if (get_type_id(stream) == LMD_TYPE_MAP || get_type_id(stream) == LMD_TYPE_ELEMENT) {
        Item readable = js_get_key_default(stream, make_string_item("readable"));
        Item writable = js_get_key_default(stream, make_string_item("writable"));
        if (js_class_id(readable) == JS_CLASS_READABLE_STREAM ||
            js_class_id(writable) == JS_CLASS_WRITABLE_STREAM) {
            return js_duplex_from(stream);
        }
    }
    if (js_stream_is_stream_like(stream)) return stream;
    return js_readable_from(stream);
}

static bool js_stream_compose_is_async_sink_function(Item stream) {
    if (!js_is_callable(stream)) return false;
    JsFunction* fn = (JsFunction*)stream.function;
    if (!fn) return false;
    return (fn->flags & JS_STREAM_FUNC_FLAG_ASYNC) &&
           !(fn->flags & JS_STREAM_FUNC_FLAG_GENERATOR) &&
           !(fn->flags & JS_STREAM_FUNC_FLAG_ASYNC_GEN);
}

static Item js_stream_compose_maybe_complete(Item* env, bool emit_finish) {
    if (!env || js_item_is_true(env[5])) return make_js_undefined();
    if (!js_item_is_true(env[3])) return make_js_undefined();
    if (!js_item_is_true(env[4])) return make_js_undefined();

    env[5] = js_bool_item(true);
    Item out = env[0];
    Item callback = env[2];
    Item err = env[6];
    js_set_key_default(out, make_string_item("__compose_pending__"), js_bool_item(false));
    if (js_is_callable(callback)) {
        if (js_stream_has_error(err)) {
            js_call_function(callback, make_js_undefined(), &err, 1);
        } else {
            js_call_function(callback, make_js_undefined(), NULL, 0);
        }
    } else if (emit_finish && !js_stream_has_error(err)) {
        stream_emit(out, "finish", NULL, 0);
    }
    if (js_stream_has_error(err)) js_stream_destroy(out, err);
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
    return js_stream_compose_maybe_complete(env, true);
}

static Item js_stream_compose_sink_rejected(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[3])) return make_js_undefined();
    env[3] = js_bool_item(true);
    env[6] = err;
    return js_stream_compose_maybe_complete(env, true);
}

static Item js_stream_compose_write(Item env_item, Item chunk, Item encoding,
        Item callback, bool forward_callback) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item write_fn = js_get_key_default(env[1], key_write);
    if (!js_is_callable(write_fn)) {
        if (js_is_callable(callback))
            js_call_function(callback, make_js_undefined(), NULL, 0);
        return make_js_undefined();
    }
    Item args[3] = { chunk, encoding,
        forward_callback ? js_duplex_from_make_forward_callback(callback) : callback };
    js_call_function(write_fn, env[1], args, 3);
    return make_js_undefined();
}
JS_FORWARD_STATIC_ITEM(js_stream_compose_sink_write, (Item env_item, Item chunk, Item encoding, Item callback), js_stream_compose_write, (env_item, chunk, encoding, callback, false))
JS_FORWARD_STATIC_ITEM(js_stream_compose_tail_write, (Item env_item, Item chunk, Item encoding, Item callback), js_stream_compose_write, (env_item, chunk, encoding, callback, true))

static Item js_stream_compose_final_common(Item env_item, Item callback,
        bool emit_finish) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    env[2] = callback;
    env[4] = js_bool_item(true);
    Item writable = env[1];
    Item end_fn = js_get_key_default(writable, key_end);
    if (js_is_callable(end_fn)) {
        Item args[1] = { make_js_undefined() };
        JS_ASSIGN_OR_RETURN(end_result, js_call_function(end_fn, writable, args, 1));
    }
    return js_stream_compose_maybe_complete(env, emit_finish);
}
JS_FORWARD_STATIC_ITEM(js_stream_compose_sink_final, (Item env_item, Item callback), js_stream_compose_final_common, (env_item, callback, true))
JS_FORWARD_STATIC_ITEM(js_stream_compose_tail_final, (Item env_item, Item callback), js_stream_compose_final_common, (env_item, callback, false))

static Item js_stream_compose_sink_destroy(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    Item writable = env[1];
    Item destroy_fn = js_get_key_default(writable, key_destroy);
    if (js_is_callable(destroy_fn)) {
        Item args[1] = { err };
        js_call_function(destroy_fn, writable, args, 1);
    }
    return env[0];
}

static Item js_stream_compose_async_sink(Item first, Item source, Item sink) {
    bool has_writable = js_stream_has_writable_side(first);
    Item opts = js_new_object();
    js_set_key_default(opts, make_string_item("readable"), js_bool_item(false));
    js_set_key_default(opts, make_string_item("writable"), js_bool_item(has_writable));
    JS_ASSIGN_OR_RETURN(out, js_duplex_new(opts));
    js_set_key_default(out, make_string_item("__compose_pending__"), js_bool_item(true));

    Item* env = js_alloc_env(7);
    env[0] = out;
    env[1] = first;
    env[2] = make_js_undefined();
    env[3] = js_bool_item(false);
    env[4] = js_bool_item(!has_writable);
    env[5] = js_bool_item(false);
    env[6] = make_js_undefined();

    if (has_writable) {
        js_set_key_default(out, make_string_item("_write"),
                        js_new_native_closure(js_stream_compose_sink_write, 3, env, 7));
        js_set_key_default(out, make_string_item("_final"),
                        js_new_native_closure(js_stream_compose_sink_final, 1, env, 7));
        js_set_key_default(out, make_string_item("_destroy"),
                        js_new_native_closure(js_stream_compose_sink_destroy, 1, env, 7));
    }

    Item args[1] = { source };
    Item result = js_call_function(sink, make_js_undefined(), args, 1);
    if (item_is_error(result)) {
        env[3] = js_bool_item(true);
        env[6] = js_error_lane_payload(result);
        js_stream_compose_maybe_complete(env, true);
        return out;
    }
    Item then_fn = js_get_key_default(result, make_string_item("then"));
    if (!js_is_callable(then_fn)) {
        env[3] = js_bool_item(true);
        env[6] = js_stream_make_type_error_with_code("ERR_INVALID_RETURN_VALUE",
            "Expected a promise to be returned from the function");
        js_stream_compose_maybe_complete(env, true);
        return out;
    }

    Item on_done = js_new_native_closure(js_stream_compose_sink_fulfilled, 1, env, 7);
    Item on_error = js_new_native_closure(js_stream_compose_sink_rejected, 1, env, 7);
    js_promise_then(result, on_done, on_error);
    return out;
}

static Item js_stream_compose_tail_on_finish(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    env[3] = js_bool_item(true);
    return js_stream_compose_maybe_complete(env, false);
}

static Item js_stream_compose_tail_on_error(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined();
    env[3] = js_bool_item(true);
    env[6] = err;
    return js_stream_compose_maybe_complete(env, false);
}

static Item js_stream_compose_writable_tail(Item first, Item last) {
    if (!js_item_is_true(js_get_key_default(first, key_writable))) return last;
    Item opts = js_new_object();
    js_set_key_default(opts, make_string_item("readable"), js_bool_item(false));
    js_set_key_default(opts, make_string_item("writable"), js_bool_item(true));
    Item first_writable_state = js_get_key_default(first, key_writable_state);
    js_set_key_default(opts, make_string_item("writableObjectMode"),
                    js_bool_item(js_state_get_bool(first_writable_state, "objectMode")));
    JS_ASSIGN_OR_RETURN(out, js_duplex_new(opts));

    Item* env = js_alloc_env(7);
    env[0] = out;
    env[1] = first;
    env[2] = make_js_undefined();
    env[3] = js_bool_item(false);
    env[4] = js_bool_item(false);
    env[5] = js_bool_item(false);
    env[6] = make_js_undefined();

    js_set_key_default(out, make_string_item("_write"),
                    js_new_native_closure(js_stream_compose_tail_write, 3, env, 7));
    js_set_key_default(out, make_string_item("_final"),
                    js_new_native_closure(js_stream_compose_tail_final, 1, env, 7));
    js_stream_once(last, make_string_item("finish"),
                   js_new_native_closure(js_stream_compose_tail_on_finish, 0, env, 7));
    js_stream_once(last, make_string_item("error"),
                   js_new_native_closure(js_stream_compose_tail_on_error, 1, env, 7));
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
        Item only = js_elements_get_int(rest_args, 0);
        if (js_stream_compose_is_async_sink_function(only)) {
            Item opts = js_new_object();
            js_set_key_default(opts, make_string_item("objectMode"), js_bool_item(true));
            JS_ASSIGN_OR_RETURN(input, js_passthrough_new(opts));
            return js_stream_compose_async_sink(input, input, only);
        }
    }

    JS_ASSIGN_OR_RETURN(first, js_stream_compose_normalize(js_elements_get_int(rest_args, 0)));
    Item previous = first;
    Item last = first;
    for (int64_t i = 1; i < argc; i++) {
        Item raw_next = js_elements_get_int(rest_args, i);
        if (i == argc - 1 && js_stream_compose_is_async_sink_function(raw_next)) {
            return js_stream_compose_async_sink(first, previous, raw_next);
        }
        JS_ASSIGN_OR_RETURN(next, js_stream_compose_normalize(raw_next));
        if (!js_item_is_true(js_get_key_default(previous, key_readable)) ||
            !js_item_is_true(js_get_key_default(next, key_writable))) {
            return js_throw_type_error_code("ERR_INVALID_ARG_VALUE",
                "The argument 'stream' must be writable and readable.");
        }
        js_readable_pipe(previous, next);
        js_readable_compose_attach_error_forward(previous, next);
        previous = next;
        last = next;
    }

    bool first_writable_open = js_item_is_true(js_get_key_default(first, key_writable));
    bool last_readable_open = js_item_is_true(js_get_key_default(last, key_readable));
    if (first.item == last.item) return first;
    if (!first_writable_open && !last_readable_open) {
        return last;
    }
    if (!last_readable_open) {
        return js_stream_compose_writable_tail(first, last);
    }

    Item pair = js_new_object();
    if (first_writable_open)
        js_set_key_default(pair, make_string_item("writable"), first);
    if (last_readable_open)
        js_set_key_default(pair, make_string_item("readable"), last);
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

static Item js_stream_state_bool_get(Item state_key, const char* field,
        Item fallback_key) {
    ensure_keys();
    Item self = js_get_this();
    Item state = js_get_key_default(self, state_key);
    bool value = js_state_get_bool(state, field);
    if (fallback_key.item != 0 && fallback_key.item != ITEM_NULL) {
        value = value || js_item_is_true(js_get_key_default(self, fallback_key));
    }
    return js_bool_item(value);
}

static Item js_stream_state_int_get(Item state_key, const char* field) {
    ensure_keys();
    Item state = js_get_key_default(js_get_this(), state_key);
    Item value = js_get_key_default(state, make_string_item(field));
    return get_type_id(value) == LMD_TYPE_INT ? value
        : (Item){.item = i2it(0)};
}
JS_FORWARD_ITEM(js_stream_get_readableEnded, (void), js_stream_state_bool_get, (key_readable_state, "endEmitted", key_end_emitted))

extern "C" Item js_stream_get_readableLength(void) {
    ensure_keys();
    Item self = js_get_this();
    Item buf = js_get_key_default(self, key_buffer);
    return (Item){.item = i2it(js_stream_readable_cached_length(self, buf))};
}

extern "C" Item js_stream_get_readableFlowing(void) {
    ensure_keys();
    Item self = js_get_this();
    if (js_item_is_true(js_get_key_default(self, key_flowing))) return js_bool_item(true);
    if (js_item_is_true(js_get_key_default(self, key_paused)) ||
        js_state_get_bool(js_get_key_default(self, key_readable_state), "readableListening")) {
        return js_bool_item(false);
    }
    return ItemNull;
}
JS_FORWARD_ITEM(js_stream_get_readableDidRead, (void), js_stream_state_bool_get, (key_readable_state, "didRead", ItemNull))
JS_FORWARD_ITEM(js_stream_get_writableEnded, (void), js_stream_state_bool_get, (key_writable_state, "ended", ItemNull))
JS_FORWARD_ITEM(js_stream_get_writableFinished, (void), js_stream_state_bool_get, (key_writable_state, "finished", key_finished))
JS_FORWARD_ITEM(js_stream_get_writableCorked, (void), js_stream_state_int_get, (key_writable_state, "corked"))

extern "C" Item js_stream_get_writableNeedDrain(void) {
    ensure_keys();
    Item self = js_get_this();
    Item state = js_get_key_default(self, key_writable_state);
    bool need_drain = js_state_get_bool(state, "needDrain") &&
                      !js_state_get_bool(state, "ended") &&
                      !js_item_is_true(js_get_key_default(self, key_destroyed));
    return js_bool_item(need_drain);
}
JS_FORWARD_ITEM(js_stream_get_writableLength, (void), js_stream_state_int_get, (key_writable_state, "length"))

extern "C" Item js_stream_isDisturbed(Item stream) {
    ensure_keys();
    if (!js_stream_is_stream_like(stream)) return js_bool_item(false);
    if (js_state_get_bool(js_get_key_default(stream, key_readable_state), "didRead"))
        return js_bool_item(true);
    return js_bool_item(js_item_is_true(js_get_key_default(stream, make_string_item("__web_disturbed__"))));
}

static Item js_stream_is_active(Item stream, bool readable) {
    ensure_keys();
    if (!js_stream_is_stream_like(stream) ||
            !(readable ? js_stream_has_readable_side(stream) :
                js_stream_has_writable_side(stream))) return ItemNull;
    if (js_item_is_true(js_get_key_default(stream, key_destroyed))) return js_bool_item(false);
    Item terminal_key = readable ? key_end_emitted : key_finished;
    const char* terminal_state = readable ? "endEmitted" : "finished";
    Item state_key = readable ? key_readable_state : key_writable_state;
    if (js_item_is_true(js_get_key_default(stream, terminal_key)) ||
        js_state_get_bool(js_get_key_default(stream, state_key), terminal_state)) {
        return js_bool_item(false);
    }
    return js_bool_item(js_item_is_true(js_get_key_default(stream,
        readable ? key_readable : key_writable)));
}
JS_FORWARD_ITEM(js_stream_isReadable, (Item stream), js_stream_is_active, (stream, true))
JS_FORWARD_ITEM(js_stream_isWritable, (Item stream), js_stream_is_active, (stream, false))

extern "C" Item js_stream_isErrored(Item stream) {
    ensure_keys();
    if (!js_stream_is_stream_like(stream)) return ItemNull;
    Item err = js_get_key_default(stream, make_string_item("errored"));
    if (js_stream_has_callback_error(err)) return js_bool_item(true);
    err = js_get_key_default(js_get_key_default(stream, key_readable_state), make_string_item("errored"));
    if (js_stream_has_callback_error(err)) return js_bool_item(true);
    err = js_get_key_default(js_get_key_default(stream, key_writable_state), make_string_item("errored"));
    return js_bool_item(js_stream_has_callback_error(err));
}

extern "C" Item js_stream_isDestroyed(Item stream) {
    ensure_keys();
    if (!js_stream_is_stream_like(stream)) return ItemNull;
    return js_bool_item(js_item_is_true(js_get_key_default(stream, key_destroyed)) ||
                        js_item_is_true(js_get_key_default(stream, make_string_item("destroyed"))));
}

static Item js_stream_constructor_prototype(Item ctor, JsClass class_id) {
    Item proto_key = make_string_item("prototype");
    Item proto = js_get_key_default(ctor, proto_key);
    if (get_type_id(proto) != LMD_TYPE_MAP) {
        proto = js_new_object_with_class(class_id);
        js_set_key_default(ctor, proto_key, proto);
    }
    return proto;
}

static Item js_stream_constructor_prototype(Item ctor) {
    Item proto = js_get_key_default(ctor, make_string_item("prototype"));
    JsClass class_id = get_type_id(proto) == LMD_TYPE_MAP
        ? js_class_id(proto) : JS_CLASS_READABLE;
    return js_stream_constructor_prototype(ctor, class_id);
}

static void js_stream_mark_constructor_prototype(Item ctor, Item proto) {
    js_set_key_default(proto, make_string_item("constructor"), ctor);
    js_mark_non_enumerable(proto, make_string_item("constructor"));
    if (get_type_id(ctor) == LMD_TYPE_FUNC) {
        js_function_set_prototype(ctor, proto);
    }
}

template <typename Target>
static void js_stream_install_has_instance(Item ctor, Target target) {
    Item key = js_well_known_symbol_key(3);
    Item fn = js_new_native_function(target);
    js_create_data_property(ctor, key, fn);
    js_mark_non_enumerable(ctor, key);
}

template <typename Target>
static void js_stream_install_accessor(Item ctor, const char* name,
        Target getter) {
    Item proto = js_stream_constructor_prototype(ctor);
    Item getter_fn = js_new_native_function(getter);
    js_install_native_accessor(proto, make_string_item(name), getter_fn, ItemNull, JSPD_NON_ENUMERABLE);
}

static void js_stream_install_state_accessors(Item readable_ctor, Item writable_ctor,
                                             Item duplex_ctor, Item transform_ctor) {
    struct JsStreamAccessorSpec {
        const char* name;
        Item (*getter)(void);
        bool writable;
    } specs[] = {
        {"readableEnded", js_stream_get_readableEnded, false},
        {"readableLength", js_stream_get_readableLength, false},
        {"readableFlowing", js_stream_get_readableFlowing, false},
        {"readableDidRead", js_stream_get_readableDidRead, false},
        {"writableEnded", js_stream_get_writableEnded, true},
        {"writableFinished", js_stream_get_writableFinished, true},
        {"writableCorked", js_stream_get_writableCorked, true},
        {"writableNeedDrain", js_stream_get_writableNeedDrain, true},
        {"writableLength", js_stream_get_writableLength, true},
    };
    for (size_t i = 0; i < sizeof(specs) / sizeof(specs[0]); i++) {
        const JsStreamAccessorSpec* spec = &specs[i];
        js_stream_install_accessor(spec->writable ? writable_ctor : readable_ctor,
            spec->name, spec->getter);
        js_stream_install_accessor(duplex_ctor, spec->name, spec->getter);
        js_stream_install_accessor(transform_ctor, spec->name, spec->getter);
    }
}

// =============================================================================
// stream Module Namespace
// =============================================================================

#define stream_namespace (js_runtime_state.stream.namespace_object)
#define stream_promises_namespace (js_runtime_state.stream.promises_namespace)

template <bool Constructable = false, typename Target>
static Item stream_set_method(Item ns, const char* name, Target target,
        int adapter_arity) {
    Item key = make_string_item(name);
    Item fn = ItemNull;
    if constexpr (Constructable) {
        fn = js_new_native_constructor(target);
    } else {
        fn = js_new_native_function(target, adapter_arity);
    }
    js_set_function_name(fn, key);
    js_set_key_default(ns, key, fn);
    return fn;
}
JS_FORWARD_STATIC_ITEM(js_stream_promisify_custom_symbol, (void), js_symbol_for, (make_string_item("nodejs.util.promisify.custom")))

static void js_stream_promises_remove_abort_listener(Item* env) {
    if (!env) return;
    Item signal = env[3];
    Item abort_listener = env[4];
    Item remove_event = js_get_key_default(signal, make_string_item("removeEventListener"));
    if (js_is_callable(remove_event) && js_is_callable(abort_listener)) {
        Item remove_args[2] = { make_string_item("abort"), abort_listener };
        js_call_function(remove_event, signal, remove_args, 2);
    }
}

static Item js_stream_promises_callback(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[2])) return make_js_undefined();
    env[2] = js_bool_item(true);
    js_stream_promises_remove_abort_listener(env);

    Item resolve = env[0];
    Item reject = env[1];
    Item callback = js_stream_has_error(err) ? reject : resolve;
    if (js_is_callable(callback)) {
        Item value = js_stream_has_error(err) ? err : make_js_undefined();
        js_call_function(callback, make_js_undefined(), &value, 1);
    }
    return make_js_undefined();
}

static Item js_stream_promises_pipeline_on_abort(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[2])) return make_js_undefined();
    env[2] = js_bool_item(true);
    js_stream_promises_remove_abort_listener(env);

    Item signal = env[3];
    Item err = js_stream_finished_abort_error(signal);
    Item reject = env[1];
    if (js_is_callable(reject)) {
        js_call_function(reject, make_js_undefined(), &err, 1);
    }
    return make_js_undefined();
}

static Item js_stream_pipeline_promises_parse_options(Item options, Item* signal) {
    *signal = make_js_undefined();
    if (get_type_id(options) != LMD_TYPE_MAP && get_type_id(options) != LMD_TYPE_ELEMENT) {
        return js_bool_item(false);
    }
    if (js_stream_is_stream_like(options)) return js_bool_item(false);

    Item maybe_signal = js_get_key_default(options, make_string_item("signal"));
    if (maybe_signal.item != 0 && get_type_id(maybe_signal) != LMD_TYPE_UNDEFINED &&
        get_type_id(maybe_signal) != LMD_TYPE_NULL) {
        if (!js_stream_is_abort_signal(maybe_signal)) {
            return js_throw_invalid_arg_type("options.signal", "AbortSignal", maybe_signal);
        }
        *signal = maybe_signal;
    }
    return js_bool_item(true);
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
        Item last = js_elements_get_int(rest_args, argc - 1);
        Item parsed_signal = make_js_undefined();
        Item parse_result = js_stream_pipeline_promises_parse_options(last, &parsed_signal);
        if (item_is_error(parse_result)) {
            return js_promise_reject(js_error_lane_payload(parse_result));
        }
        if (js_item_is_true(parse_result)) {
            signal = parsed_signal;
            stream_count--;
        }
    }
    if (stream_count < 2) {
        return js_promise_reject(js_stream_make_type_error_with_code("ERR_MISSING_ARGS",
            "The \"streams\" argument is required"));
    }

    Item capability = js_promise_with_resolvers();
    Item promise = js_get_key_default(capability, make_string_item("promise"));
    Item* env = js_alloc_env(5);
    env[0] = js_get_key_default(capability, make_string_item("resolve"));
    env[1] = js_get_key_default(capability, make_string_item("reject"));
    env[2] = js_bool_item(false);
    env[3] = signal;
    env[4] = make_js_undefined();
    Item callback = js_new_native_closure(js_stream_promises_callback, 1, env, 5);

    if (js_stream_is_abort_signal(signal)) {
        Item aborted = js_get_key_default(signal, make_string_item("aborted"));
        if (get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted)) {
            Item err = js_stream_finished_abort_error(signal);
            Item reject = env[1];
            if (js_is_callable(reject)) {
                js_call_function(reject, make_js_undefined(), &err, 1);
            }
            return promise;
        }

        Item add_event = js_get_key_default(signal, make_string_item("addEventListener"));
        if (js_is_callable(add_event)) {
            Item abort_listener = js_new_native_closure(js_stream_promises_pipeline_on_abort, 0, env, 5);
            env[4] = abort_listener;
            Item add_args[2] = { make_string_item("abort"), abort_listener };
            js_call_function(add_event, signal, add_args, 2);
        }
    }

    Item pipeline_args = js_array_new(0);
    for (int64_t i = 0; i < stream_count; i++) {
        js_array_push(pipeline_args, js_elements_get_int(rest_args, i));
    }
    js_array_push(pipeline_args, callback);
    Item pipeline_result = js_stream_pipeline_rest(pipeline_args);
    if (item_is_error(pipeline_result)) {
        Item err = js_error_lane_payload(pipeline_result);
        Item reject = env[1];
        if (js_is_callable(reject)) {
            JS_ASSIGN_OR_RETURN(reject_result, js_call_function(reject, make_js_undefined(), &err, 1));
        }
    }
    return promise;
}

static Item js_stream_finished_parse_options(Item options, bool* cleanup) {
    *cleanup = false;
    if (options.item == 0 || get_type_id(options) == LMD_TYPE_UNDEFINED ||
        get_type_id(options) == LMD_TYPE_NULL) {
        return js_bool_item(true);
    }
    if (get_type_id(options) != LMD_TYPE_MAP && get_type_id(options) != LMD_TYPE_ELEMENT) {
        return js_throw_invalid_arg_type("options", "object", options);
    }
    Item cleanup_item = js_get_key_default(options, make_string_item("cleanup"));
    if (cleanup_item.item == 0 || get_type_id(cleanup_item) == LMD_TYPE_UNDEFINED) {
        return js_bool_item(true);
    }
    if (get_type_id(cleanup_item) != LMD_TYPE_BOOL) {
        return js_throw_invalid_arg_type("options.cleanup", "boolean", cleanup_item);
    }
    *cleanup = it2b(cleanup_item);
    return js_bool_item(true);
}

static void js_stream_finished_cleanup(Item stream, Item callback) {
    js_stream_off(stream, make_string_item("end"), callback);
    js_stream_off(stream, make_string_item("finish"), callback);
    js_stream_off(stream, make_string_item("error"), callback);
    js_stream_off(stream, make_string_item("close"), callback);
    Item wrapper = js_get_key_default(callback, js_stream_finished_wrapper_key());
    if (js_is_callable(wrapper) && wrapper.item != callback.item) {
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
        Item stored_error = js_get_key_default(stream, make_string_item("__error__"));
        if (js_stream_has_error(stored_error)) event_error = stored_error;
    }

    Item resolve = env[3];
    Item reject = env[5];
    Item fn = js_stream_has_error(event_error) ? reject : resolve;
    if (js_is_callable(fn)) {
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

    Item stream = js_elements_get_int(rest_args, 0);
    Item options = argc > 1 ? js_elements_get_int(rest_args, 1) : make_js_undefined();
    bool cleanup = false;
    JS_ASSIGN_OR_RETURN(parse_result, js_stream_finished_parse_options(options, &cleanup));

    Item capability = js_promise_with_resolvers();
    Item promise = js_get_key_default(capability, make_string_item("promise"));
    Item* env = js_alloc_env(6);
    env[0] = stream;
    env[1] = make_js_undefined();
    env[2] = js_bool_item(cleanup);
    env[3] = js_get_key_default(capability, make_string_item("resolve"));
    env[4] = js_bool_item(false);
    env[5] = js_get_key_default(capability, make_string_item("reject"));
    Item callback = js_new_native_closure(js_stream_promises_finished_callback, 1, env, 6);
    env[1] = callback;

    Item finished_result = js_stream_finished_impl(stream, options, callback);
    if (item_is_error(finished_result)) {
        Item err = js_error_lane_payload(finished_result);
        Item reject = env[5];
        if (js_is_callable(reject)) {
            JS_ASSIGN_OR_RETURN(reject_result, js_call_function(reject, make_js_undefined(), &err, 1));
        }
    }
    return promise;
}

extern "C" Item js_get_stream_promises_namespace(void) {
    if (!stream_ensure_roots()) return ItemError;
    if (stream_promises_namespace.item != 0) return stream_promises_namespace;
    ensure_keys();

    stream_promises_namespace = js_new_object();
    Item pipeline = stream_set_method(stream_promises_namespace, "pipeline",
                                      js_stream_promises_pipeline, -1);
    Item finished = stream_set_method(stream_promises_namespace, "finished",
                                      js_stream_promises_finished, -1);
    js_set_function_name(pipeline, make_string_item("pipeline"));
    js_set_function_name(finished, make_string_item("finished"));
    return stream_promises_namespace;
}

static Item js_writable_toWeb(Item writable) {
    Item web = js_writable_stream_new(make_js_undefined());
    Item ctor = js_get_global_property(make_string_item("WritableStream"));
    Item proto = js_get_key_default(ctor, make_string_item("prototype"));
    if (get_type_id(proto) == LMD_TYPE_MAP || get_type_id(proto) == LMD_TYPE_ELEMENT) {
        js_set_prototype(web, proto);
    }
    js_set_key_default(web, make_string_item("__node_stream__"), writable);
    return web;
}

static Item js_readable_to_web_result(Item value, bool done) {
    Item result = js_new_object();
    js_set_key_default(result, make_string_item("value"), done ? make_js_undefined() : value);
    js_set_key_default(result, make_string_item("done"), js_bool_item(done));
    return result;
}

static bool js_readable_to_web_is_ended(Item readable) {
    Item state = js_get_key_default(readable, key_readable_state);
    return js_item_is_true(js_get_key_default(readable, key_end_pending)) ||
           js_item_is_true(js_get_key_default(readable, key_end_emitted)) ||
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
    void* dst = js_typed_array_prepare_write_ptr(view);
    if (src && dst) {
        memcpy(dst, src, (size_t)copy_len);
    }
    return view;
}

static Item js_readable_to_web_read_now(Item reader, Item view) {
    Item readable = js_get_key_default(reader, make_string_item("__node_readable__"));
    if (get_type_id(readable) != LMD_TYPE_MAP && get_type_id(readable) != LMD_TYPE_ELEMENT) {
        return js_readable_to_web_result(make_js_undefined(), true);
    }

    bool is_byob = js_item_is_true(js_get_key_default(reader, make_string_item("__byob__"))) &&
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
    if (js_is_callable(resolve)) {
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
    if (js_is_callable(resolve)) {
        js_call_function(resolve, make_js_undefined(), &result, 1);
    }
    return make_js_undefined();
}

static Item js_readable_to_web_on_error(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || js_item_is_true(env[3])) return make_js_undefined();
    env[3] = js_bool_item(true);
    Item reject = env[2];
    if (js_is_callable(reject)) {
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
    env[1] = js_get_key_default(capability, make_string_item("resolve"));
    env[2] = js_get_key_default(capability, make_string_item("reject"));
    env[3] = js_bool_item(false);
    env[4] = view;

    Item readable = js_get_key_default(reader, make_string_item("__node_readable__"));
    Item readable_listener = js_new_native_closure(js_readable_to_web_on_readable, 0, env, 5);
    Item end_listener = js_new_native_closure(js_readable_to_web_on_end, 0, env, 5);
    Item error_listener = js_new_native_closure(js_readable_to_web_on_error, 1, env, 5);
    js_stream_once(readable, make_string_item("readable"), readable_listener);
    js_stream_once(readable, make_string_item("end"), end_listener);
    js_stream_once(readable, make_string_item("close"), end_listener);
    js_stream_once(readable, make_string_item("error"), error_listener);
    js_stream_call_read_if_needed(readable, make_js_undefined());
    return js_get_key_default(capability, make_string_item("promise"));
}

static Item js_readable_to_web_reader_cancel(Item reason) {
    (void)reason;
    Item reader = js_get_this();
    Item readable = js_get_key_default(reader, make_string_item("__node_readable__"));
    if (get_type_id(readable) == LMD_TYPE_MAP || get_type_id(readable) == LMD_TYPE_ELEMENT) {
        js_stream_destroy(readable, make_js_undefined());
    }
    return js_promise_resolve(make_js_undefined());
}

static Item js_readable_to_web_get_reader(Item options) {
    Item web = js_get_this();
    Item reader = js_new_object();
    js_set_key_default(reader, make_string_item("__node_readable__"),
                    js_get_key_default(web, make_string_item("__node_readable__")));
    js_set_key_default(reader, make_string_item("__byob__"),
                    js_bool_item(get_type_id(js_get_key_default(options, make_string_item("mode"))) == LMD_TYPE_STRING));
    js_set_native_key(reader, make_string_item("read"), js_readable_to_web_reader_read);
    js_set_native_key(reader, make_string_item("cancel"), js_readable_to_web_reader_cancel);
    return reader;
}

static Item js_readable_toWeb(Item readable, Item options) {
    Item type = js_get_key_default(options, make_string_item("type"));
    if (get_type_id(type) == LMD_TYPE_STRING && !js_stream_string_equals(type, "bytes")) {
        return js_throw_type_error_code("ERR_INVALID_ARG_VALUE",
                                        "The property 'options.type' is invalid");
    }
    Item web = js_new_object_with_class(JS_CLASS_READABLE_STREAM);
    Item ctor = js_get_global_property(make_string_item("ReadableStream"));
    Item proto = js_get_key_default(ctor, make_string_item("prototype"));
    if (get_type_id(proto) == LMD_TYPE_MAP || get_type_id(proto) == LMD_TYPE_ELEMENT) {
        js_set_prototype(web, proto);
    }
    js_set_key_default(web, make_string_item("__node_readable__"), readable);
    js_set_native_key(web, make_string_item("getReader"), js_readable_to_web_get_reader);
    return web;
}

static Item js_stream_destroy_export(Item stream, Item err) {
    Item reason = err;
    if (reason.item == 0 || get_type_id(reason) == LMD_TYPE_UNDEFINED ||
        get_type_id(reason) == LMD_TYPE_NULL) {
        reason = js_stream_is_finished_for_destroy_export(stream) ?
                 make_js_undefined() : js_stream_iter_make_abort_error();
    }
    Item destroy_fn = js_get_key_default(stream, key_destroy);
    if (js_is_callable(destroy_fn)) {
        js_call_function(destroy_fn, stream, &reason, 1);
        return stream;
    }
    return js_stream_destroy(stream, reason);
}

extern "C" Item js_get_stream_namespace(void) {
    if (!stream_ensure_roots()) return ItemError;
    if (stream_namespace.item != 0) return stream_namespace;
    ensure_keys();

    stream_namespace = js_new_object();

    Item readable_constructor =
        stream_set_method<true>(stream_namespace, "Readable", js_readable_new,
            1);
    Item writable_constructor =
        stream_set_method<true>(stream_namespace, "Writable", js_writable_new,
            1);
    Item duplex_constructor =
        stream_set_method<true>(stream_namespace, "Duplex", js_duplex_new, 1);
    Item transform_constructor =
        stream_set_method<true>(stream_namespace, "Transform", js_transform_new,
            1);
    Item passthrough_constructor =
        stream_set_method<true>(stream_namespace, "PassThrough",
            js_passthrough_new, 1);
    Item pipeline_fn = stream_set_method(stream_namespace, "pipeline", js_stream_pipeline_rest, -1);
    Item finished_fn = stream_set_method(stream_namespace, "finished", js_stream_finished_rest, -1);
    stream_set_method(stream_namespace, "compose", js_stream_compose_rest, -1);
    stream_set_method(stream_namespace, "duplexPair", js_stream_duplex_pair, 0);
    stream_set_method(stream_namespace, "destroy", js_stream_destroy_export, 2);
    stream_set_method(stream_namespace, "addAbortSignal", js_stream_addAbortSignal, 2);
    stream_set_method(stream_namespace, "getDefaultHighWaterMark",
                      js_stream_getDefaultHighWaterMark, 1);
    stream_set_method(stream_namespace, "setDefaultHighWaterMark",
                      js_stream_setDefaultHighWaterMark, 2);
    stream_set_method(stream_namespace, "isReadable", js_stream_isReadable, 1);
    stream_set_method(stream_namespace, "isWritable", js_stream_isWritable, 1);
    stream_set_method(stream_namespace, "isDisturbed", js_stream_isDisturbed, 1);
    stream_set_method(stream_namespace, "isErrored", js_stream_isErrored, 1);
    stream_set_method(stream_namespace, "isDestroyed", js_stream_isDestroyed, 1);
    stream_set_method(stream_namespace, "arrayBuffer", js_stream_consumer_arrayBuffer, 1);
    stream_set_method(stream_namespace, "blob", js_stream_consumer_blob, 1);
    stream_set_method(stream_namespace, "buffer", js_stream_consumer_buffer, 1);
    stream_set_method(stream_namespace, "bytes", js_stream_consumer_bytes, 1);
    stream_set_method(stream_namespace, "json", js_stream_consumer_json, 1);
    stream_set_method(stream_namespace, "text", js_stream_consumer_text, 1);

    Item promises_ns = js_get_stream_promises_namespace();
    js_set_key_default(stream_namespace, make_string_item("promises"), promises_ns);
    Item custom_key = js_stream_promisify_custom_symbol();
    js_set_key_default(pipeline_fn, custom_key, js_get_key_default(promises_ns, make_string_item("pipeline")));
    js_set_key_default(finished_fn, custom_key, js_get_key_default(promises_ns, make_string_item("finished")));
    js_mark_non_enumerable(pipeline_fn, custom_key);
    js_mark_non_enumerable(finished_fn, custom_key);

    // Stream — base class that inherits from EventEmitter and provides pipe().
    Item events_ctor = ItemNull;
    jube_specifier_resolve("events", &events_ctor);
    Item stream_base = js_new_native_constructor(js_stream_base_constructor);
    Item stream_base_proto = js_new_object();
    Item events_proto = js_get_key_default(events_ctor, make_string_item("prototype"));
    if (js_stream_is_object_like(events_proto)) {
        js_set_prototype(stream_base_proto, events_proto);
    }
    if (js_stream_is_object_like(events_ctor)) {
        js_set_prototype(stream_base, events_ctor);
    }
    js_set_native_key(stream_base_proto, key_pipe, js_readable_inst_pipe);
    js_set_native_key(stream_base_proto, make_string_item("unpipe"), js_readable_inst_unpipe);
    // legacy Stream instances have no readable state, so for-await must enter
    // the event-listener iterator path from the base prototype.
    js_stream_install_async_iterator(stream_base_proto);
    js_set_key_default(stream_base_proto, make_string_item("constructor"), stream_base);
    js_mark_non_enumerable(stream_base_proto, make_string_item("constructor"));
    js_set_function_name(stream_base, make_string_item("Stream"));
    js_set_key_default(stream_base, make_string_item("prototype"), stream_base_proto);
    js_function_set_prototype(stream_base, stream_base_proto);
    js_set_key_default(stream_namespace, make_string_item("Stream"), stream_base);

    if (get_type_id(readable_constructor) == LMD_TYPE_FUNC) {
        js_set_native_key(readable_constructor, make_string_item("from"), js_readable_from);
        js_set_native_key(readable_constructor, make_string_item("toWeb"), js_readable_toWeb);
    }
    if (get_type_id(writable_constructor) == LMD_TYPE_FUNC) {
        js_set_native_key(writable_constructor, make_string_item("toWeb"), js_writable_toWeb);
    }
    if (get_type_id(duplex_constructor) == LMD_TYPE_FUNC) {
        js_set_native_key(duplex_constructor, make_string_item("from"), js_duplex_from);
    }

    if (get_type_id(readable_constructor) == LMD_TYPE_FUNC &&
        get_type_id(writable_constructor) == LMD_TYPE_FUNC &&
        get_type_id(duplex_constructor) == LMD_TYPE_FUNC &&
        get_type_id(transform_constructor) == LMD_TYPE_FUNC &&
        get_type_id(passthrough_constructor) == LMD_TYPE_FUNC) {
        stream_readable_prototype = js_stream_constructor_prototype(readable_constructor,
            JS_CLASS_READABLE);
        stream_writable_prototype = js_stream_constructor_prototype(writable_constructor,
            JS_CLASS_WRITABLE);
        stream_duplex_prototype = js_stream_constructor_prototype(duplex_constructor,
            JS_CLASS_DUPLEX);
        stream_transform_prototype = js_stream_constructor_prototype(transform_constructor,
            JS_CLASS_TRANSFORM);
        stream_passthrough_prototype = js_stream_constructor_prototype(passthrough_constructor,
            JS_CLASS_PASS_THROUGH);
        js_stream_install_async_iterator(stream_readable_prototype);
        js_set_native_key(stream_readable_prototype, make_string_item("iterator"), js_readable_inst_iterator);
        js_stream_install_readable_helpers(stream_readable_prototype);

        js_stream_mark_constructor_prototype(readable_constructor, stream_readable_prototype);
        js_stream_mark_constructor_prototype(writable_constructor, stream_writable_prototype);
        js_stream_mark_constructor_prototype(duplex_constructor, stream_duplex_prototype);
        js_stream_mark_constructor_prototype(transform_constructor, stream_transform_prototype);
        js_stream_mark_constructor_prototype(passthrough_constructor, stream_passthrough_prototype);
        js_set_key_default(stream_readable_prototype, make_string_item("destroyed"), js_bool_item(false));
        js_set_key_default(stream_writable_prototype, make_string_item("destroyed"), js_bool_item(false));
        js_set_key_default(stream_duplex_prototype, make_string_item("destroyed"), js_bool_item(false));
        js_set_key_default(stream_transform_prototype, make_string_item("destroyed"), js_bool_item(false));
        js_set_key_default(stream_passthrough_prototype, make_string_item("destroyed"), js_bool_item(false));

        js_stream_install_state_accessors(readable_constructor, writable_constructor,
                                          duplex_constructor, transform_constructor);

        js_set_prototype(stream_duplex_prototype, stream_readable_prototype);
        js_set_prototype(stream_transform_prototype, stream_duplex_prototype);
        js_set_prototype(stream_passthrough_prototype, stream_transform_prototype);

        js_stream_install_has_instance(readable_constructor, js_stream_readable_has_instance);
        js_stream_install_has_instance(writable_constructor, js_stream_writable_has_instance);
        js_stream_install_has_instance(duplex_constructor, js_stream_duplex_has_instance);
        js_stream_install_has_instance(transform_constructor, js_stream_transform_has_instance);
        js_stream_install_has_instance(passthrough_constructor, js_stream_passthrough_has_instance);
    }

    Item default_key = make_string_item("default");
    js_set_key_default(stream_namespace, default_key, stream_namespace);

    return stream_namespace;
}

extern "C" Item js_get_stream_iter_namespace(void) {
    if (!stream_ensure_roots()) return ItemError;
    if (stream_iter_namespace.item != 0) return stream_iter_namespace;
    ensure_keys();
    stream_iter_namespace = js_new_object();
    stream_set_method(stream_iter_namespace, "from", js_stream_iter_from, 1);
    stream_set_method(stream_iter_namespace, "fromSync", js_stream_iter_fromSync, 1);
    stream_set_method(stream_iter_namespace, "pipeTo", js_stream_iter_pipeTo, 4);
    stream_set_method(stream_iter_namespace, "pull", js_stream_iter_pull, 2);
    stream_set_method(stream_iter_namespace, "pullSync", js_stream_iter_pullSync, 8);
    stream_set_method(stream_iter_namespace, "push", js_stream_iter_push, 1);
    stream_set_method(stream_iter_namespace, "ondrain", js_stream_iter_ondrain, 1);
    stream_set_method(stream_iter_namespace, "text", js_stream_iter_text_consume, 2);
    stream_set_method(stream_iter_namespace, "textSync", js_stream_iter_textSync, 2);
    stream_set_method(stream_iter_namespace, "bytes", js_stream_iter_bytes, 2);
    stream_set_method(stream_iter_namespace, "bytesSync", js_stream_iter_bytesSync, 2);
    stream_set_method(stream_iter_namespace, "arrayBuffer", js_stream_iter_arrayBuffer, 2);
    stream_set_method(stream_iter_namespace, "arrayBufferSync", js_stream_iter_arrayBufferSync, 2);
    stream_set_method(stream_iter_namespace, "array", js_stream_iter_array, 2);
    stream_set_method(stream_iter_namespace, "arraySync", js_stream_iter_arraySync, 2);
    stream_set_method(stream_iter_namespace, "tap", js_stream_iter_tap, 1);
    stream_set_method(stream_iter_namespace, "tapSync", js_stream_iter_tapSync, 1);
    js_set_key_default(stream_iter_namespace, make_string_item("default"), stream_iter_namespace);
    return stream_iter_namespace;
}

extern "C" Item js_get_stream_web_namespace(void) {
    if (!stream_ensure_roots()) return ItemError;
    if (stream_web_namespace.item != 0) return stream_web_namespace;
    ensure_keys();
    stream_web_namespace = js_new_object();

    Item readable_ctor = js_new_native_constructor(js_readable_stream_new);
    Item writable_ctor = js_new_native_constructor(js_writable_stream_new);
    js_set_function_name(readable_ctor, make_string_item("ReadableStream"));
    js_set_function_name(writable_ctor, make_string_item("WritableStream"));
    Item transform_ctor = js_get_global_property(make_string_item("TransformStream"));
    if (get_type_id(transform_ctor) != LMD_TYPE_FUNC) {
        transform_ctor = js_new_native_constructor(js_transform_stream_new);
    }

    js_set_key_default(stream_web_namespace, make_string_item("ReadableStream"), readable_ctor);
    js_set_key_default(stream_web_namespace, make_string_item("WritableStream"), writable_ctor);
    js_set_key_default(stream_web_namespace, make_string_item("TransformStream"), transform_ctor);
    js_set_key_default(stream_web_namespace, make_string_item("default"), stream_web_namespace);
    return stream_web_namespace;
}

extern "C" Item js_get_internal_stream_add_abort_signal_namespace(void) {
    static Item add_abort_ns = {0};
    if (add_abort_ns.item != 0) return add_abort_ns;
    add_abort_ns = js_new_object();
    js_set_native_key(add_abort_ns, make_string_item("addAbortSignalNoValidate"), js_stream_addAbortSignalNoValidate);
    js_set_key_default(add_abort_ns, make_string_item("default"), add_abort_ns);
    return add_abort_ns;
}

extern "C" Item js_get_internal_stream_state_namespace(void) {
    if (internal_stream_state_namespace.item != 0) return internal_stream_state_namespace;
    internal_stream_state_namespace = js_new_object();
    js_set_native_key(internal_stream_state_namespace, make_string_item("getDefaultHighWaterMark"), js_stream_getDefaultHighWaterMark);
    js_set_native_key(internal_stream_state_namespace, make_string_item("setDefaultHighWaterMark"), js_stream_setDefaultHighWaterMark);
    js_set_key_default(internal_stream_state_namespace, make_string_item("default"),
                    internal_stream_state_namespace);
    return internal_stream_state_namespace;
}

extern "C" Item js_get_internal_stream_end_of_stream_namespace(void) {
    if (internal_stream_end_of_stream_namespace.item != 0)
        return internal_stream_end_of_stream_namespace;
    internal_stream_end_of_stream_namespace = js_new_object();
    Item eos_fn = js_new_native_rest_function(js_stream_finished_rest);
    Item finished_fn = js_new_native_rest_function(js_stream_promises_finished);
    js_set_key_default(internal_stream_end_of_stream_namespace, make_string_item("eos"), eos_fn);
    // Node's internal EOS module exports callback eos() and Promise finished();
    // sharing eos here makes stream/promises.finished wait forever for a callback.
    js_set_key_default(internal_stream_end_of_stream_namespace, make_string_item("finished"), finished_fn);
    // Node's internal EOS module exports this unique symbol; stream.finished
    // checks its description so native options can request synchronous callback.
    js_set_key_default(internal_stream_end_of_stream_namespace,
                    make_string_item("kEosNodeSynchronousCallback"),
                    js_symbol_create(make_string_item("kEosNodeSynchronousCallback")));
    js_set_key_default(internal_stream_end_of_stream_namespace, make_string_item("default"),
                    internal_stream_end_of_stream_namespace);
    return internal_stream_end_of_stream_namespace;
}

extern "C" void js_stream_reset(void) {
    if (!js_active_runtime_state) return;
    stream_namespace = (Item){0};
    stream_promises_namespace = (Item){0};
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
