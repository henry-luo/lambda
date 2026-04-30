/**
 * JavaScript Event Loop for Lambda v15
 *
 * libuv-backed event loop implementation:
 * - Microtask queue: FIFO ring buffer, flushed via uv_prepare_t
 * - Timers: uv_timer_t handles (unlimited, cross-platform)
 * - Drain: uv_run(UV_RUN_DEFAULT) — runs until no active handles
 */
#include "js_event_loop.h"
#include "js_runtime.h"
#include "js_class.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"

#include <cstring>
#include <cmath>
#include "../../lib/mem.h"
#include <setjmp.h>
#include <signal.h>

// =============================================================================
// Microtask Queue (FIFO ring buffer — unchanged from v14)
// =============================================================================

#define MICROTASK_CAPACITY 1024

static Item microtask_ring[MICROTASK_CAPACITY];
static int  microtask_head = 0;   // read index
static int  microtask_tail = 0;   // write index
static int  microtask_count = 0;

static void microtask_push(Item cb) {
    if (microtask_count >= MICROTASK_CAPACITY) {
        log_error("event_loop: microtask queue overflow (%d)", MICROTASK_CAPACITY);
        return;
    }
    microtask_ring[microtask_tail] = cb;
    microtask_tail = (microtask_tail + 1) % MICROTASK_CAPACITY;
    microtask_count++;
}

static Item microtask_pop() {
    if (microtask_count == 0) return ItemNull;
    Item cb = microtask_ring[microtask_head];
    microtask_head = (microtask_head + 1) % MICROTASK_CAPACITY;
    microtask_count--;
    return cb;
}

extern "C" void js_microtask_enqueue(Item callback) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        log_error("event_loop: microtask_enqueue called with non-function (type=%d)", get_type_id(callback));
        return;
    }
    microtask_push(callback);
}

extern "C" void js_microtask_flush(void) {
    // drain all microtasks (including ones enqueued during execution)
    int safety = 0;
    while (microtask_count > 0 && safety < MICROTASK_CAPACITY * 2) {
        Item cb = microtask_pop();
        if (get_type_id(cb) == LMD_TYPE_FUNC) {
            js_call_function(cb, ItemNull, NULL, 0);
        }
        safety++;
    }
    if (safety >= MICROTASK_CAPACITY * 2) {
        log_error("event_loop: microtask flush exceeded safety limit");
    }
}

// =============================================================================
// Timer Management (libuv-backed)
// =============================================================================

typedef struct JsTimerHandle {
    uv_timer_t timer;
    int64_t    id;
    Item       callback;
    bool       is_interval;
    Item       extra_args[8];   // extra args to pass to callback
    int        extra_count;     // number of extra args
} JsTimerHandle;

#define MAX_TIMER_HANDLES 1024
static JsTimerHandle *timer_handles[MAX_TIMER_HANDLES];
static int timer_handle_count = 0;
static int64_t next_timer_id = 1;

static void timer_close_cb(uv_handle_t *handle) {
    JsTimerHandle *th = (JsTimerHandle *)handle->data;
    // unregister GC roots before freeing
    extern void heap_unregister_gc_root(uint64_t* slot);
    heap_unregister_gc_root(&th->callback.item);
    for (int j = 0; j < th->extra_count; j++) {
        heap_unregister_gc_root(&th->extra_args[j].item);
    }
    // remove from tracking array
    for (int i = 0; i < timer_handle_count; i++) {
        if (timer_handles[i] == th) {
            timer_handles[i] = timer_handles[timer_handle_count - 1];
            timer_handle_count--;
            break;
        }
    }
    mem_free(th);
}

// register a timer handle's callback and extra_args as GC roots so they
// survive garbage collection while the timer is pending
static void timer_register_gc_roots(JsTimerHandle *th) {
    extern void heap_register_gc_root(uint64_t* slot);
    heap_register_gc_root(&th->callback.item);
    for (int j = 0; j < th->extra_count; j++) {
        heap_register_gc_root(&th->extra_args[j].item);
    }
}

static void timer_fire_cb(uv_timer_t *handle) {
    JsTimerHandle *th = (JsTimerHandle *)handle->data;
    if (get_type_id(th->callback) == LMD_TYPE_FUNC) {
        if (th->extra_count > 0) {
            js_call_function(th->callback, ItemNull, th->extra_args, th->extra_count);
        } else {
            js_call_function(th->callback, ItemNull, NULL, 0);
        }
    }
    // microtasks are drained by the uv_prepare_t handle after each callback

    if (!th->is_interval) {
        uv_timer_stop(&th->timer);
        uv_close((uv_handle_t *)&th->timer, timer_close_cb);
    }
}

static double item_to_ms(Item delay) {
    if (get_type_id(delay) == LMD_TYPE_FLOAT) {
        return *((double *)delay.item);
    } else if (get_type_id(delay) == LMD_TYPE_INT) {
        return (double)it2i(delay);
    }
    return 0;
}

// =============================================================================
// Timeout/Immediate object helpers
// =============================================================================

// Timeout.ref() — no-op, returns this
extern "C" Item js_timeout_ref(Item this_val) {
    return this_val;
}

// Timeout.unref() — no-op, returns this
extern "C" Item js_timeout_unref(Item this_val) {
    return this_val;
}

// Timeout.hasRef() — always true
extern "C" Item js_timeout_hasRef(Item this_val) {
    return (Item){.item = b2it(true)};
}

// Timeout.refresh() — no-op, returns this
extern "C" Item js_timeout_refresh(Item this_val) {
    return this_val;
}

// Timeout[Symbol.toPrimitive]() — returns the timer id
extern "C" Item js_timeout_toPrimitive(Item this_val) {
    Item id = js_property_get(this_val, (Item){.item = s2it(heap_create_name("_timerId", 8))});
    return id;
}

static Item make_timer_object(int64_t id) {
    Item obj = js_new_object();
    js_property_set(obj, (Item){.item = s2it(heap_create_name("_timerId", 8))},
                    (Item){.item = i2it(id)});

    // bind methods
    extern Item js_new_function(void* fn, int nargs);
    Item ref_fn = js_new_function((void*)js_timeout_ref, 0);
    Item unref_fn = js_new_function((void*)js_timeout_unref, 0);
    Item hasRef_fn = js_new_function((void*)js_timeout_hasRef, 0);
    Item refresh_fn = js_new_function((void*)js_timeout_refresh, 0);

    js_property_set(obj, (Item){.item = s2it(heap_create_name("ref", 3))}, ref_fn);
    js_property_set(obj, (Item){.item = s2it(heap_create_name("unref", 5))}, unref_fn);
    js_property_set(obj, (Item){.item = s2it(heap_create_name("hasRef", 6))}, hasRef_fn);
    js_property_set(obj, (Item){.item = s2it(heap_create_name("refresh", 7))}, refresh_fn);

    // Symbol.toPrimitive → stored as __sym_2 internally
    Item toPrim_fn = js_new_function((void*)js_timeout_toPrimitive, 0);
    js_property_set(obj, (Item){.item = s2it(heap_create_name("__sym_2", 7))}, toPrim_fn);

    // class identity (T5b: typed JsClass byte; legacy `__class_name__`
    // string write retired).
    js_class_stamp(obj, JS_CLASS_TIMEOUT);  // A3-T3b

    return obj;
}

// Extract timer id from either a plain integer or a Timeout object
static int64_t extract_timer_id(Item timer_id) {
    TypeId tid = get_type_id(timer_id);
    if (tid == LMD_TYPE_INT) {
        return it2i(timer_id);
    } else if (tid == LMD_TYPE_MAP) {
        Item id = js_property_get(timer_id, (Item){.item = s2it(heap_create_name("_timerId", 8))});
        if (get_type_id(id) == LMD_TYPE_INT) return it2i(id);
    }
    return -1;
}

extern "C" Item js_setTimeout(Item callback, Item delay) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        extern Item js_throw_type_error_code(const char*, const char*);
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "The \"callback\" argument must be of type function.");
    }
    uv_loop_t *loop = lambda_uv_loop();
    if (!loop) {
        log_error("event_loop: uv loop not initialized for setTimeout");
        return ItemNull;
    }

    double ms = item_to_ms(delay);
    if (ms < 0) ms = 0;

    JsTimerHandle *th = (JsTimerHandle *)mem_calloc(1, sizeof(JsTimerHandle), MEM_CAT_JS_RUNTIME);
    if (!th) return ItemNull;

    th->id = next_timer_id++;
    th->callback = callback;
    th->is_interval = false;
    th->extra_count = 0;
    th->timer.data = th;

    uv_timer_init(loop, &th->timer);
    uv_timer_start(&th->timer, timer_fire_cb, (uint64_t)ms, 0);
    timer_register_gc_roots(th);

    if (timer_handle_count < MAX_TIMER_HANDLES) {
        timer_handles[timer_handle_count++] = th;
    }

    return make_timer_object(th->id);
}

// setTimeout with extra args passed as a JS array
extern "C" Item js_setTimeout_args(Item callback, Item delay, Item args_array) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        extern Item js_throw_type_error_code(const char*, const char*);
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "The \"callback\" argument must be of type function.");
    }
    uv_loop_t *loop = lambda_uv_loop();
    if (!loop) {
        log_error("event_loop: uv loop not initialized for setTimeout");
        return ItemNull;
    }

    double ms = item_to_ms(delay);
    if (ms < 0) ms = 0;

    JsTimerHandle *th = (JsTimerHandle *)mem_calloc(1, sizeof(JsTimerHandle), MEM_CAT_JS_RUNTIME);
    if (!th) return ItemNull;

    th->id = next_timer_id++;
    th->callback = callback;
    th->is_interval = false;
    th->extra_count = 0;
    th->timer.data = th;

    // extract extra args from the array
    if (get_type_id(args_array) == LMD_TYPE_ARRAY) {
        Array* arr = args_array.array;
        int count = (int)arr->length;
        if (count > 8) count = 8;
        for (int i = 0; i < count; i++) {
            th->extra_args[i] = arr->items[i];
        }
        th->extra_count = count;
    }

    uv_timer_init(loop, &th->timer);
    uv_timer_start(&th->timer, timer_fire_cb, (uint64_t)ms, 0);
    timer_register_gc_roots(th);

    if (timer_handle_count < MAX_TIMER_HANDLES) {
        timer_handles[timer_handle_count++] = th;
    }

    return make_timer_object(th->id);
}

// Helper: create a JS array from 1-4 items (used by transpiler for setTimeout extra args)
extern "C" Item js_pack_args_1(Item a1) {
    Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
    arr->type_id = LMD_TYPE_ARRAY;
    arr->items = nullptr;
    arr->length = 0;
    arr->capacity = 0;
    array_push(arr, a1);
    return (Item){.array = arr};
}
extern "C" Item js_pack_args_2(Item a1, Item a2) {
    Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
    arr->type_id = LMD_TYPE_ARRAY;
    arr->items = nullptr;
    arr->length = 0;
    arr->capacity = 0;
    array_push(arr, a1);
    array_push(arr, a2);
    return (Item){.array = arr};
}
extern "C" Item js_pack_args_3(Item a1, Item a2, Item a3) {
    Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
    arr->type_id = LMD_TYPE_ARRAY;
    arr->items = nullptr;
    arr->length = 0;
    arr->capacity = 0;
    array_push(arr, a1);
    array_push(arr, a2);
    array_push(arr, a3);
    return (Item){.array = arr};
}
extern "C" Item js_pack_args_4(Item a1, Item a2, Item a3, Item a4) {
    Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
    arr->type_id = LMD_TYPE_ARRAY;
    arr->items = nullptr;
    arr->length = 0;
    arr->capacity = 0;
    array_push(arr, a1);
    array_push(arr, a2);
    array_push(arr, a3);
    array_push(arr, a4);
    return (Item){.array = arr};
}

extern "C" Item js_setInterval(Item callback, Item delay) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        extern Item js_throw_type_error_code(const char*, const char*);
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "The \"callback\" argument must be of type function.");
    }
    uv_loop_t *loop = lambda_uv_loop();
    if (!loop) {
        log_error("event_loop: uv loop not initialized for setInterval");
        return ItemNull;
    }

    double ms = item_to_ms(delay);
    if (ms < 1) ms = 1; // minimum interval

    JsTimerHandle *th = (JsTimerHandle *)mem_calloc(1, sizeof(JsTimerHandle), MEM_CAT_JS_RUNTIME);
    if (!th) return ItemNull;

    th->id = next_timer_id++;
    th->callback = callback;
    th->is_interval = true;
    th->extra_count = 0;
    th->timer.data = th;

    uv_timer_init(loop, &th->timer);
    uv_timer_start(&th->timer, timer_fire_cb, (uint64_t)ms, (uint64_t)ms);
    timer_register_gc_roots(th);

    if (timer_handle_count < MAX_TIMER_HANDLES) {
        timer_handles[timer_handle_count++] = th;
    }

    return make_timer_object(th->id);
}

// setInterval with extra args passed as a JS array
extern "C" Item js_setInterval_args(Item callback, Item delay, Item args_array) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        extern Item js_throw_type_error_code(const char*, const char*);
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "The \"callback\" argument must be of type function.");
    }
    uv_loop_t *loop = lambda_uv_loop();
    if (!loop) {
        log_error("event_loop: uv loop not initialized for setInterval");
        return ItemNull;
    }

    double ms = item_to_ms(delay);
    if (ms < 1) ms = 1;

    JsTimerHandle *th = (JsTimerHandle *)mem_calloc(1, sizeof(JsTimerHandle), MEM_CAT_JS_RUNTIME);
    if (!th) return ItemNull;

    th->id = next_timer_id++;
    th->callback = callback;
    th->is_interval = true;
    th->extra_count = 0;
    th->timer.data = th;

    if (get_type_id(args_array) == LMD_TYPE_ARRAY) {
        Array* arr = args_array.array;
        int count = (int)arr->length;
        if (count > 8) count = 8;
        for (int i = 0; i < count; i++) {
            th->extra_args[i] = arr->items[i];
        }
        th->extra_count = count;
    }

    uv_timer_init(loop, &th->timer);
    uv_timer_start(&th->timer, timer_fire_cb, (uint64_t)ms, (uint64_t)ms);
    timer_register_gc_roots(th);

    if (timer_handle_count < MAX_TIMER_HANDLES) {
        timer_handles[timer_handle_count++] = th;
    }

    return make_timer_object(th->id);
}

// =============================================================================
// Promise-based timers (for timers/promises module)
// =============================================================================

// helper: create an AbortError for promise rejection
static Item make_abort_error(Item signal) {
    Item err = js_new_object();
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(err, JS_CLASS_ABORT_ERROR);  // A3-T3b
    js_property_set(err, (Item){.item = s2it(heap_create_name("name", 4))},
                    (Item){.item = s2it(heap_create_name("AbortError", 10))});
    js_property_set(err, (Item){.item = s2it(heap_create_name("code", 4))},
                    (Item){.item = s2it(heap_create_name("ABORT_ERR", 9))});
    js_property_set(err, (Item){.item = s2it(heap_create_name("message", 7))},
                    (Item){.item = s2it(heap_create_name("The operation was aborted", 25))});
    // propagate cause from signal.reason if available
    if (get_type_id(signal) == LMD_TYPE_MAP) {
        Item reason = js_property_get(signal, (Item){.item = s2it(heap_create_name("reason", 6))});
        if (get_type_id(reason) != LMD_TYPE_UNDEFINED && get_type_id(reason) != LMD_TYPE_NULL) {
            js_property_set(err, (Item){.item = s2it(heap_create_name("cause", 5))}, reason);
        }
    }
    return err;
}

// helper: check if signal is aborted, validate options types
// returns 0=ok, 1=already aborted (reject_out set), -1=type error thrown
static int check_timer_options(Item options, Item* reject_out) {
    extern Item js_promise_reject(Item reason);
    extern Item js_throw_type_error_code(const char*, const char*);

    if (get_type_id(options) == LMD_TYPE_UNDEFINED || get_type_id(options) == LMD_TYPE_NULL) {
        return 0; // no options
    }
    TypeId opt_type = get_type_id(options);
    if (opt_type != LMD_TYPE_MAP && opt_type != LMD_TYPE_OBJECT) {
        // options must be an object if provided (non-nullish)
        *reject_out = js_promise_reject(
            js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
                "The \"options\" argument must be of type object."));
        return -1;
    }
    // validate signal if present
    Item signal = js_property_get(options, (Item){.item = s2it(heap_create_name("signal", 6))});
    if (get_type_id(signal) != LMD_TYPE_UNDEFINED && get_type_id(signal) != LMD_TYPE_NULL) {
        // signal must be an AbortSignal (object with 'aborted' property)
        TypeId sig_type = get_type_id(signal);
        if (sig_type != LMD_TYPE_MAP && sig_type != LMD_TYPE_OBJECT) {
            *reject_out = js_promise_reject(
                js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
                    "The \"options.signal\" property must be an instance of AbortSignal."));
            return -1;
        }
        // check if already aborted
        Item aborted = js_property_get(signal, (Item){.item = s2it(heap_create_name("aborted", 7))});
        if (get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted)) {
            *reject_out = js_promise_reject(make_abort_error(signal));
            return 1;
        }
    }
    // validate ref if present
    Item ref = js_property_get(options, (Item){.item = s2it(heap_create_name("ref", 3))});
    if (get_type_id(ref) != LMD_TYPE_UNDEFINED && get_type_id(ref) != LMD_TYPE_NULL) {
        if (get_type_id(ref) != LMD_TYPE_BOOL) {
            *reject_out = js_promise_reject(
                js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
                    "The \"options.ref\" property must be of type boolean."));
            return -1;
        }
    }
    return 0;
}

// setTimeout(delay, value, options) → Promise that resolves to value after delay ms
extern "C" Item js_setTimeout_promise(Item delay, Item value, Item options) {
    extern Item js_promise_with_resolvers(void);
    extern Item js_promise_reject(Item reason);

    // check options for signal before creating timer
    Item reject_out = ItemNull;
    int opt_rc = check_timer_options(options, &reject_out);
    if (opt_rc != 0) return reject_out;

    Item resolvers = js_promise_with_resolvers();
    Item k_promise = (Item){.item = s2it(heap_create_name("promise", 7))};
    Item k_resolve = (Item){.item = s2it(heap_create_name("resolve", 7))};
    Item k_reject = (Item){.item = s2it(heap_create_name("reject", 6))};
    Item promise = js_property_get(resolvers, k_promise);
    Item resolve_fn = js_property_get(resolvers, k_resolve);
    Item reject_fn = js_property_get(resolvers, k_reject);

    // store resolve_fn as callback, value as extra_arg
    uv_loop_t *loop = lambda_uv_loop();
    if (!loop) return promise;

    double ms = item_to_ms(delay);
    // emit process 'warning' for NaN delay (Node.js compat)
    if (get_type_id(delay) == LMD_TYPE_FLOAT && isnan(*((double *)delay.item))) {
        extern Item js_process_emit(Item event_name, Item arg1);
        Item warning_obj = js_new_object();
        js_property_set(warning_obj,
            (Item){.item = s2it(heap_create_name("name", 4))},
            (Item){.item = s2it(heap_create_name("TimeoutNaNWarning", 17))});
        js_property_set(warning_obj,
            (Item){.item = s2it(heap_create_name("message", 7))},
            (Item){.item = s2it(heap_create_name("NaN milliseconds is not a valid timeout", 39))});
        js_process_emit(
            (Item){.item = s2it(heap_create_name("warning", 7))},
            warning_obj);
    }
    if (ms < 0 || isnan(ms)) ms = 0;

    JsTimerHandle *th = (JsTimerHandle *)mem_calloc(1, sizeof(JsTimerHandle), MEM_CAT_JS_RUNTIME);
    if (!th) return promise;

    th->id = next_timer_id++;
    th->callback = resolve_fn;
    th->is_interval = false;
    th->extra_args[0] = value;
    th->extra_count = 1;
    th->timer.data = th;

    uv_timer_init(loop, &th->timer);
    uv_timer_start(&th->timer, timer_fire_cb, (uint64_t)ms, 0);
    timer_register_gc_roots(th);

    if (timer_handle_count < MAX_TIMER_HANDLES) {
        timer_handles[timer_handle_count++] = th;
    }

    // if signal present, add abort listener to reject promise and clear timer
    if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_OBJECT) {
        Item signal = js_property_get(options, (Item){.item = s2it(heap_create_name("signal", 6))});
        if (get_type_id(signal) == LMD_TYPE_MAP || get_type_id(signal) == LMD_TYPE_OBJECT) {
            // create an abort handler closure that captures timer id and reject_fn
            // we store timer_id and reject_fn in a wrapper object on the signal
            Item timer_id_item = (Item){.item = i2it(th->id)};
            // add 'abort' event listener — when aborted, reject the promise
            Item listeners = js_property_get(signal, (Item){.item = s2it(heap_create_name("__listeners__", 13))});
            if (get_type_id(listeners) == LMD_TYPE_ARRAY) {
                // store reject_fn and timer_id in the abort entry for manual dispatch
                Item entry = js_new_object();
                js_property_set(entry, (Item){.item = s2it(heap_create_name("type", 4))},
                                (Item){.item = s2it(heap_create_name("abort", 5))});
                js_property_set(entry, (Item){.item = s2it(heap_create_name("__timer_reject__", 16))}, reject_fn);
                js_property_set(entry, (Item){.item = s2it(heap_create_name("__timer_id__", 12))}, timer_id_item);
                js_property_set(entry, (Item){.item = s2it(heap_create_name("__timer_signal__", 16))}, signal);
                // use a handler function that rejects with AbortError
                // for now, store a dummy; the abort dispatch in js_abort_controller_abort handles the __timer_reject__ path
                js_property_set(entry, (Item){.item = s2it(heap_create_name("handler", 7))}, reject_fn);
                js_array_push(listeners, entry);
            }
        }
    }

    return promise;
}

// setImmediate(value, options) → Promise that resolves to value immediately
extern "C" Item js_setImmediate_promise(Item value, Item options) {
    extern Item js_promise_with_resolvers(void);
    extern Item js_promise_reject(Item reason);

    // check options for signal before creating timer
    Item reject_out = ItemNull;
    int opt_rc = check_timer_options(options, &reject_out);
    if (opt_rc != 0) return reject_out;

    Item resolvers = js_promise_with_resolvers();
    Item k_promise = (Item){.item = s2it(heap_create_name("promise", 7))};
    Item k_resolve = (Item){.item = s2it(heap_create_name("resolve", 7))};
    Item k_reject = (Item){.item = s2it(heap_create_name("reject", 6))};
    Item promise = js_property_get(resolvers, k_promise);
    Item resolve_fn = js_property_get(resolvers, k_resolve);
    Item reject_fn = js_property_get(resolvers, k_reject);

    uv_loop_t *loop = lambda_uv_loop();
    if (!loop) return promise;

    JsTimerHandle *th = (JsTimerHandle *)mem_calloc(1, sizeof(JsTimerHandle), MEM_CAT_JS_RUNTIME);
    if (!th) return promise;

    th->id = next_timer_id++;
    th->callback = resolve_fn;
    th->is_interval = false;
    th->extra_args[0] = value;
    th->extra_count = 1;
    th->timer.data = th;

    uv_timer_init(loop, &th->timer);
    uv_timer_start(&th->timer, timer_fire_cb, 0, 0);
    timer_register_gc_roots(th);

    if (timer_handle_count < MAX_TIMER_HANDLES) {
        timer_handles[timer_handle_count++] = th;
    }

    // if signal present, add abort listener
    if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_OBJECT) {
        Item signal = js_property_get(options, (Item){.item = s2it(heap_create_name("signal", 6))});
        if (get_type_id(signal) == LMD_TYPE_MAP || get_type_id(signal) == LMD_TYPE_OBJECT) {
            Item timer_id_item = (Item){.item = i2it(th->id)};
            Item listeners = js_property_get(signal, (Item){.item = s2it(heap_create_name("__listeners__", 13))});
            if (get_type_id(listeners) == LMD_TYPE_ARRAY) {
                Item entry = js_new_object();
                js_property_set(entry, (Item){.item = s2it(heap_create_name("type", 4))},
                                (Item){.item = s2it(heap_create_name("abort", 5))});
                js_property_set(entry, (Item){.item = s2it(heap_create_name("__timer_reject__", 16))}, reject_fn);
                js_property_set(entry, (Item){.item = s2it(heap_create_name("__timer_id__", 12))}, timer_id_item);
                js_property_set(entry, (Item){.item = s2it(heap_create_name("__timer_signal__", 16))}, signal);
                js_property_set(entry, (Item){.item = s2it(heap_create_name("handler", 7))}, reject_fn);
                js_array_push(listeners, entry);
            }
        }
    }

    return promise;
}

// scheduler.wait(delay, options) → setTimeout promise with undefined value
extern "C" Item js_scheduler_wait(Item delay, Item options) {
    Item undef = (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
    return js_setTimeout_promise(delay, undef, options);
}

// scheduler.yield() → setImmediate promise with undefined value
extern "C" Item js_scheduler_yield(void) {
    Item undef = (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
    return js_setImmediate_promise(undef, undef);
}

extern "C" void js_clearTimeout(Item timer_id) {
    int64_t id = extract_timer_id(timer_id);
    if (id < 0) return;
    for (int i = 0; i < timer_handle_count; i++) {
        if (timer_handles[i]->id == id) {
            JsTimerHandle *th = timer_handles[i];
            uv_timer_stop(&th->timer);
            uv_close((uv_handle_t *)&th->timer, timer_close_cb);
            return;
        }
    }
}

extern "C" void js_clearInterval(Item timer_id) {
    js_clearTimeout(timer_id);
}

// =============================================================================
// Event Loop Lifecycle
// =============================================================================

extern "C" void js_event_loop_init(void) {
    // reset microtask queue — zero ring buffer to prevent GC scanning stale Items
    memset(microtask_ring, 0, sizeof(microtask_ring));
    microtask_head = 0;
    microtask_tail = 0;
    microtask_count = 0;
    // reset timers — clear stale callback pointers
    for (int i = 0; i < timer_handle_count; i++) {
        timer_handles[i] = NULL;
    }
    timer_handle_count = 0;
    next_timer_id = 1;

    // register microtask ring buffer as GC root (static memory invisible to stack scanning)
    static bool statics_rooted = false;
    if (!statics_rooted) {
        heap_register_gc_root_range((uint64_t*)microtask_ring, MICROTASK_CAPACITY);
        statics_rooted = true;
    }

    // initialize libuv loop
    lambda_uv_init();

    // register microtask drain to run before each event loop iteration
    lambda_uv_set_microtask_drain(js_microtask_flush);
}

// Recovery mechanism for SIGSEGV during event loop drain.
// Heap corruption in timer callbacks (pre-existing bug) can crash the process
// after tests have completed. We catch the signal and abort the event loop
// gracefully instead of terminating.
static jmp_buf event_loop_jmpbuf;
static volatile sig_atomic_t event_loop_guarded = 0;
static struct sigaction event_loop_old_sa;

static void event_loop_sigsegv_handler(int sig, siginfo_t* info, void* ctx) {
    if (event_loop_guarded) {
        log_error("event_loop: caught SIGSEGV at %p during drain, aborting event loop",
                  info ? info->si_addr : NULL);
        event_loop_guarded = 0;
        // restore the old handler before longjmp
        sigaction(SIGSEGV, &event_loop_old_sa, NULL);
        longjmp(event_loop_jmpbuf, 1);
    }
    // not guarded, call previous handler
    if (event_loop_old_sa.sa_flags & SA_SIGINFO) {
        event_loop_old_sa.sa_sigaction(sig, info, ctx);
    } else if (event_loop_old_sa.sa_handler != SIG_DFL && event_loop_old_sa.sa_handler != SIG_IGN) {
        event_loop_old_sa.sa_handler(sig);
    } else {
        signal(SIGSEGV, SIG_DFL);
        raise(SIGSEGV);
    }
}

// Maximum time (ms) the event loop drain is allowed to run before being
// forcefully stopped.  Prevents infinite blocking from setInterval() or
// long-running timers in document scripts (e.g. CSS animation test pages).
#define EVENT_LOOP_DRAIN_TIMEOUT_MS 5000

static void drain_watchdog_cb(uv_timer_t* handle) {
    log_debug("event_loop: drain timeout (%d ms) — stopping loop", EVENT_LOOP_DRAIN_TIMEOUT_MS);
    lambda_uv_stop();
}

// Stop and close all active interval timers so they don't keep the event
// loop alive after drain completes or times out.
static void stop_all_interval_timers(void) {
    for (int i = timer_handle_count - 1; i >= 0; i--) {
        JsTimerHandle* th = timer_handles[i];
        if (th && th->is_interval) {
            uv_timer_stop(&th->timer);
            uv_close((uv_handle_t*)&th->timer, timer_close_cb);
        }
    }
}

extern "C" int js_event_loop_drain(void) {
    // flush any synchronous microtasks first (from Promise resolutions)
    js_microtask_flush();

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) return 0;

    // install crash guard for event loop drain
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = event_loop_sigsegv_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, &event_loop_old_sa);
    event_loop_guarded = 1;

    int result = 0;
    if (setjmp(event_loop_jmpbuf) == 0) {
        // install watchdog timer to prevent infinite blocking from setInterval
        uv_timer_t watchdog;
        uv_timer_init(loop, &watchdog);
        uv_unref((uv_handle_t*)&watchdog); // don't let watchdog itself keep loop alive when alone
        uv_timer_start(&watchdog, drain_watchdog_cb, EVENT_LOOP_DRAIN_TIMEOUT_MS, 0);

        // run libuv event loop until all timers/handles are done (or watchdog fires)
        result = lambda_uv_run();

        // clean up watchdog
        uv_timer_stop(&watchdog);
        uv_close((uv_handle_t*)&watchdog, NULL);
        // run once more to process the close callback
        uv_run(loop, UV_RUN_NOWAIT);

        // stop any remaining interval timers that would keep the loop alive
        stop_all_interval_timers();
        // drain close callbacks from stopped intervals
        uv_run(loop, UV_RUN_NOWAIT);

        // final microtask flush after loop exits
        js_microtask_flush();
    } else {
        log_error("event_loop: recovered from crash during drain");
        result = -1;
    }

    // restore previous signal handler
    event_loop_guarded = 0;
    sigaction(SIGSEGV, &event_loop_old_sa, NULL);

    return result;
}
