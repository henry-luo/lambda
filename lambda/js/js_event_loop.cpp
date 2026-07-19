/**
 * JavaScript Event Loop for Lambda v15
 *
 * libuv-backed event loop implementation:
 * - nextTick queue: drained before Promise microtasks
 * - Microtask queue: FIFO ring buffer, flushed at uv phase checkpoints
 * - Animation frame queue: flushed by Radiant's frame clock
 * - Timers: uv_timer_t handles (unlimited, cross-platform)
 * - Drain: uv_run(UV_RUN_DEFAULT) — runs until no active handles
 */
#include "js_event_loop.h"
#include "js_dom.h"
#include "js_runtime.h"
#include "js_class.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../concurrency.h"
#include "../concurrency_js.h"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"
#include "../../lib/arraylist.h"

#include <cstring>
#include <cmath>
#include "../../lib/mem.h"
#include <setjmp.h>
#include <signal.h>
#include <cstdio>

extern __thread EvalContext* context;
extern "C" bool js_dom_is_host_driven_loop(void);  // defined in lambda/js/js_dom.cpp
extern "C" Item js_async_hooks_get_current_resource(void);
extern "C" Item js_async_hooks_enter_resource(Item resource);
extern "C" void js_async_hooks_restore_resource(Item previous);
extern "C" Item js_async_hooks_create_resource(const char* type_chars, int type_len);
extern "C" void js_async_hooks_emit_destroy_resource(Item resource);
extern "C" Item js_util_promisify_custom_symbol(void);
extern "C" Item js_als_capture_context(void);
extern "C" Item js_als_context_call(Item context, Item callback, Item this_val, Item arg1, int64_t has_arg);
extern "C" Item js_als_context_call_args(Item context, Item callback, Item this_val, Item* args, int argc);
extern "C" Item js_process_emit(Item event_name, Item arg1);
extern "C" void js_promise_flush_unhandled_checks(void);
extern "C" bool js_process_exit_requested(void);
extern "C" Item js_domain_get_current(void);
extern "C" Item js_domain_set_current(Item domain);
extern "C" void js_domain_restore(Item previous);
extern "C" Item js_domain_capture_stack(void);
extern "C" Item js_domain_capture_async_stack(void);
extern "C" Item js_domain_set_stack(Item stack);
extern "C" void js_domain_restore_stack(Item previous);
extern "C" Context* _lambda_rt;
extern Runtime* js_source_runtime;
extern Item js_make_number(double value);

// =============================================================================
// Task Queues
// =============================================================================

#define MICROTASK_CAPACITY 1024
#define RAF_CAPACITY 1024
#define TASK_FLUSH_SAFETY_LIMIT (MICROTASK_CAPACITY * 8)

static Item next_tick_ring[MICROTASK_CAPACITY];
static Item next_tick_resource_ring[MICROTASK_CAPACITY];
static Item next_tick_als_ring[MICROTASK_CAPACITY];
static Item next_tick_domain_ring[MICROTASK_CAPACITY];
static int  next_tick_head = 0;
static int  next_tick_tail = 0;
static int  next_tick_count = 0;
static Item microtask_ring[MICROTASK_CAPACITY];
static Item microtask_resource_ring[MICROTASK_CAPACITY];
static Item microtask_als_ring[MICROTASK_CAPACITY];
static Item microtask_domain_ring[MICROTASK_CAPACITY];
static int  microtask_head = 0;   // read index
static int  microtask_tail = 0;   // write index
static int  microtask_count = 0;

static Item raf_callback_ring[RAF_CAPACITY];
static int64_t raf_id_ring[RAF_CAPACITY];
static int raf_head = 0;
static int raf_tail = 0;
static int raf_count = 0;
static int64_t next_raf_id = 1;
static bool auto_close_mode = false;
static bool event_loop_shutting_down = false;

extern "C" void js_event_loop_set_auto_close_mode(bool enabled) {
    auto_close_mode = enabled;
}

extern "C" bool js_event_loop_auto_close_mode(void) {
    return auto_close_mode;
}

extern "C" bool js_event_loop_is_shutting_down(void) {
    return event_loop_shutting_down;
}

static void next_tick_push(Item cb) {
    if (next_tick_count >= MICROTASK_CAPACITY) {
        log_error("event_loop: nextTick queue overflow (%d)", MICROTASK_CAPACITY);
        return;
    }
    next_tick_ring[next_tick_tail] = cb;
    next_tick_resource_ring[next_tick_tail] = js_async_hooks_get_current_resource();
    next_tick_als_ring[next_tick_tail] = js_als_capture_context();
    next_tick_domain_ring[next_tick_tail] = js_domain_capture_async_stack();
    next_tick_tail = (next_tick_tail + 1) % MICROTASK_CAPACITY;
    next_tick_count++;
}

static Item next_tick_pop(Item* out_resource, Item* out_als_context, Item* out_domain) {
    if (next_tick_count == 0) return ItemNull;
    Item cb = next_tick_ring[next_tick_head];
    if (out_resource) *out_resource = next_tick_resource_ring[next_tick_head];
    if (out_als_context) *out_als_context = next_tick_als_ring[next_tick_head];
    if (out_domain) *out_domain = next_tick_domain_ring[next_tick_head];
    next_tick_ring[next_tick_head] = ItemNull;
    next_tick_resource_ring[next_tick_head] = ItemNull;
    next_tick_als_ring[next_tick_head] = ItemNull;
    next_tick_domain_ring[next_tick_head] = ItemNull;
    next_tick_head = (next_tick_head + 1) % MICROTASK_CAPACITY;
    next_tick_count--;
    return cb;
}

static void microtask_push(Item cb) {
    if (microtask_count >= MICROTASK_CAPACITY) {
        log_error("event_loop: microtask queue overflow (%d)", MICROTASK_CAPACITY);
        return;
    }
    microtask_ring[microtask_tail] = cb;
    microtask_resource_ring[microtask_tail] = js_async_hooks_get_current_resource();
    microtask_als_ring[microtask_tail] = js_als_capture_context();
    microtask_domain_ring[microtask_tail] = js_domain_capture_async_stack();
    microtask_tail = (microtask_tail + 1) % MICROTASK_CAPACITY;
    microtask_count++;
}

static Item microtask_pop(Item* out_resource, Item* out_als_context, Item* out_domain) {
    if (microtask_count == 0) return ItemNull;
    Item cb = microtask_ring[microtask_head];
    if (out_resource) *out_resource = microtask_resource_ring[microtask_head];
    if (out_als_context) *out_als_context = microtask_als_ring[microtask_head];
    if (out_domain) *out_domain = microtask_domain_ring[microtask_head];
    microtask_ring[microtask_head] = ItemNull;
    microtask_resource_ring[microtask_head] = ItemNull;
    microtask_als_ring[microtask_head] = ItemNull;
    microtask_domain_ring[microtask_head] = ItemNull;
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

extern "C" void js_next_tick_enqueue(Item callback) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        log_error("event_loop: nextTick enqueue called with non-function (type=%d)", get_type_id(callback));
        return;
    }
    next_tick_push(callback);
}

// Js57 P2c: visible queue size for the bounded-await drain heuristic.
// Returns the combined pending nextTick + microtask count so js_await_sync can
// detect whether a drain turn made progress (no progress = give up early).
extern "C" int js_microtask_pending_count(void) {
    return next_tick_count + microtask_count;
}

static void js_run_queued_callback(Item cb, Item resource, Item als_context, Item domain) {
    RootFrame roots((Context*)context, 6);
    // Queue pop clears the persistent slots before context setup can allocate;
    // keep the dequeued callback graph exact-rooted for the entire invocation.
    Rooted<Item> callback_root(roots, cb);
    Rooted<Item> resource_root(roots, resource);
    Rooted<Item> als_root(roots, als_context);
    Rooted<Item> domain_root(roots, domain);
    Rooted<Item> previous_resource_root(roots, ItemNull);
    Rooted<Item> previous_domain_root(roots, ItemNull);
    if (get_type_id(callback_root.get()) != LMD_TYPE_FUNC) return;

    previous_resource_root.set(js_async_hooks_enter_resource(resource_root.get()));
    previous_domain_root.set(js_domain_set_stack(domain_root.get()));
    js_als_context_call(als_root.get(), callback_root.get(), ItemNull, ItemNull, 0);
    js_domain_restore_stack(previous_domain_root.get());
    js_async_hooks_restore_resource(previous_resource_root.get());
}

extern "C" void js_microtask_flush(void) {
    int safety = 0;
    while ((next_tick_count > 0 || microtask_count > 0) &&
           safety < TASK_FLUSH_SAFETY_LIMIT) {
        while (next_tick_count > 0 && safety < TASK_FLUSH_SAFETY_LIMIT) {
            Item resource = ItemNull;
            Item als_context = ItemNull;
            Item domain = ItemNull;
            Item cb = next_tick_pop(&resource, &als_context, &domain);
            js_run_queued_callback(cb, resource, als_context, domain);
            safety++;
        }
        while (microtask_count > 0 && safety < TASK_FLUSH_SAFETY_LIMIT) {
            Item resource = ItemNull;
            Item als_context = ItemNull;
            Item domain = ItemNull;
            Item cb = microtask_pop(&resource, &als_context, &domain);
            js_run_queued_callback(cb, resource, als_context, domain);
            safety++;
        }
    }
    if (safety >= TASK_FLUSH_SAFETY_LIMIT) {
        log_error("event_loop: nextTick/microtask flush exceeded safety limit");
    }
    js_promise_flush_unhandled_checks();
}

static bool raf_push(Item cb, int64_t id) {
    if (raf_count >= RAF_CAPACITY) {
        log_error("event_loop: animation frame queue overflow (%d)", RAF_CAPACITY);
        return false;
    }
    raf_callback_ring[raf_tail] = cb;
    raf_id_ring[raf_tail] = id;
    raf_tail = (raf_tail + 1) % RAF_CAPACITY;
    raf_count++;
    return true;
}

static Item raf_pop(int64_t* out_id) {
    if (raf_count == 0) {
        if (out_id) *out_id = -1;
        return ItemNull;
    }
    Item cb = raf_callback_ring[raf_head];
    if (out_id) *out_id = raf_id_ring[raf_head];
    raf_callback_ring[raf_head] = ItemNull;
    raf_id_ring[raf_head] = -1;
    raf_head = (raf_head + 1) % RAF_CAPACITY;
    raf_count--;
    return cb;
}

extern "C" Item js_requestAnimationFrame(Item callback) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        extern Item js_throw_type_error_code(const char*, const char*);
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "The \"callback\" argument must be of type function.");
    }
    int64_t id = next_raf_id++;
    if (!raf_push(callback, id)) return ItemNull;
    return (Item){.item = i2it(id)};
}

extern "C" void js_cancelAnimationFrame(Item request_id) {
    if (get_type_id(request_id) != LMD_TYPE_INT) return;
    int64_t id = it2i(request_id);
    for (int i = 0; i < raf_count; i++) {
        int idx = (raf_head + i) % RAF_CAPACITY;
        if (raf_id_ring[idx] == id) {
            raf_callback_ring[idx] = ItemNull;
            raf_id_ring[idx] = -1;
            return;
        }
    }
}

extern "C" int js_animation_frame_has_pending(void) {
    return raf_count > 0 ? 1 : 0;
}

extern "C" int js_animation_frame_flush(double timestamp_ms) {
    int pending = raf_count;
    int called = 0;
    if (pending <= 0) return 0;

    // The frame clock supplies absolute monotonic time; DOMHighResTimeStamp is
    // relative to the same document origin as performance.now().
    Item timestamp = js_make_number(js_performance_monotonic_to_relative(timestamp_ms));
    // rAF callbacks and performance.now() share one document frame clock.
    // Headless draining advances synthetic frames faster than wall time, so
    // exposing wall time here prevented animation libraries from completing.
    js_performance_frame_clock_begin(timestamp_ms);

    for (int i = 0; i < pending; i++) {
        int64_t id = -1;
        Item cb = raf_pop(&id);
        (void)id;
        if (get_type_id(cb) == LMD_TYPE_FUNC) {
            js_call_function(cb, ItemNull, &timestamp, 1);
            called++;
        }
    }
    js_performance_frame_clock_end();
    js_microtask_flush();
    return called;
}

extern "C" int js_animation_frame_drain(int max_frames) {
    // Auto-close cancels timers in js_event_loop_drain(); rAF draining is
    // bounded and needed to settle headless layout/reftest-wait snapshots.
    if (max_frames <= 0) max_frames = 1;
    int frames = 0;
    int called = 0;
    double timestamp_ms = js_performance_monotonic_now_ms();
    while (raf_count > 0 && frames < max_frames) {
        timestamp_ms += 16.6667;
        called += js_animation_frame_flush(timestamp_ms);
        js_event_loop_drain();
        frames++;
    }
    if (raf_count > 0) {
        log_error("event_loop: animation frame drain stopped with %d callback(s) pending", raf_count);
    }
    return called;
}

// =============================================================================
// Timer Management (libuv-backed)
// =============================================================================

typedef struct JsTimerHandle {
    uv_timer_t timer;
    int64_t    id;
    Item       callback;
    Item       object;
    Item       async_resource;
    Item       als_context;
    Item       domain;
    bool       is_interval;
    Item       extra_args[8];   // extra args to pass to callback
    int        extra_count;     // number of extra args
    Heap*      runtime_heap;
    NamePool*  runtime_name_pool;
    Pool*      runtime_pool;
    void*      runtime_doc;
    bool       roots_registered;
    bool       closing;
    double     virtual_due_ms;
    double     virtual_repeat_ms;
    bool       virtual_active;
    bool       virtual_refed;
} JsTimerHandle;

#define MAX_TIMER_HANDLES 1024
static JsTimerHandle *timer_handles[MAX_TIMER_HANDLES];
static int timer_handle_count = 0;
static int64_t next_timer_id = 1;
static uint64_t timer_progress_generation = 0;
static bool timer_force_shutdown = false;
static bool timer_nan_warning_emitted = false;
static bool timer_negative_warning_emitted = false;
static bool virtual_clock_enabled = false;
static double virtual_clock_ms = 0.0;

#define MAX_MOCK_SCHEDULER_WAITS 128
typedef struct JsMockSchedulerWait {
    Item    promise;
    Item    resolve;
    Item    reject;
    Item    signal;
    int64_t due_ms;
    bool    active;
} JsMockSchedulerWait;

static bool mock_scheduler_enabled = false;
static int64_t mock_scheduler_now_ms = 0;
static JsMockSchedulerWait mock_scheduler_waits[MAX_MOCK_SCHEDULER_WAITS];
extern "C" uint64_t js_get_heap_epoch(void);
static uint64_t mock_scheduler_roots_epoch = 0;

static void close_all_timer_handles(void);
static void timer_register_gc_roots(JsTimerHandle* th);

typedef struct JsTimerRuntimeScope {
    EvalContext runtime_ctx;
    EvalContext* saved_context;
    Context* saved_lambda_rt;
    ArrayList* type_list;
    void* saved_doc;
    bool active;
    bool rt_active;
    bool doc_active;
} JsTimerRuntimeScope;

static void timer_capture_runtime(JsTimerHandle* th, const char* resource_name, int resource_len) {
    if (!th) return;
    RootFrame roots((Context*)context, (size_t)(5 + th->extra_count));
    Rooted<Item> callback_root(roots, th->callback);
    Rooted<Item> object_root(roots, th->object);
    Rooted<Item> resource_root(roots, th->async_resource);
    Rooted<Item> als_root(roots, th->als_context);
    Rooted<Item> domain_root(roots, th->domain);
    uint64_t* extra_roots[8] = {nullptr};
    for (int i = 0; i < th->extra_count; i++) {
        extra_roots[i] = roots.take_slot();
        if (extra_roots[i]) *extra_roots[i] = th->extra_args[i].item;
    }

    // The native timer record is not a GC object. Publish its callback and
    // captured values before building more async state, then transfer them to
    // registered persistent slots before this temporary frame is released.
    resource_root.set(js_async_hooks_create_resource(resource_name, resource_len));
    als_root.set(js_als_capture_context());
    domain_root.set(js_domain_capture_async_stack());
    th->callback = callback_root.get();
    th->object = object_root.get();
    th->async_resource = resource_root.get();
    th->als_context = als_root.get();
    th->domain = domain_root.get();
    for (int i = 0; i < th->extra_count; i++) {
        th->extra_args[i] = (Item){.item = extra_roots[i] ? *extra_roots[i] : 0};
    }
    if (context) {
        th->runtime_heap = context->heap;
        th->runtime_name_pool = context->name_pool;
        th->runtime_pool = context->pool;
    }
    th->runtime_doc = js_dom_get_document();
    timer_register_gc_roots(th);
}

static bool timer_runtime_enter(JsTimerHandle* th, JsTimerRuntimeScope* scope) {
    if (!th || !scope) return false;
    memset(scope, 0, sizeof(JsTimerRuntimeScope));
    scope->saved_doc = js_dom_get_document();
    bool needs_runtime_scope =
        !context || (th->runtime_heap && context->heap != th->runtime_heap);
    if (needs_runtime_scope) {
        if (!th->runtime_heap || !th->runtime_name_pool) {
            return false;
        }
        scope->runtime_ctx.heap = th->runtime_heap;
        scope->runtime_ctx.name_pool = th->runtime_name_pool;
        scope->runtime_ctx.pool = th->runtime_pool ?
            th->runtime_pool : th->runtime_heap->pool;
        scope->type_list = arraylist_new(16);
        scope->runtime_ctx.type_list = scope->type_list;
        scope->saved_context = context;
        context = &scope->runtime_ctx;
        scope->active = true;
    }
    scope->saved_lambda_rt = _lambda_rt;
    if (context) {
        _lambda_rt = (Context*)context;
        scope->rt_active = true;
    }
    if (th->runtime_doc) {
        js_dom_set_document(th->runtime_doc);
        scope->doc_active = true;
    }
    return true;
}

static void timer_runtime_exit(JsTimerRuntimeScope* scope) {
    if (!scope) return;
    if (scope->doc_active) {
        EvalContext* active_context = context;
        if (scope->active && scope->saved_context) {
            context = scope->saved_context;
        }
        js_dom_set_document(scope->saved_doc);
        context = active_context;
        scope->doc_active = false;
    }
    if (scope->active) {
        context = scope->saved_context;
        if (scope->type_list) {
            arraylist_free(scope->type_list);
            scope->type_list = nullptr;
        }
        scope->active = false;
    }
    if (scope->rt_active) {
        _lambda_rt = scope->saved_lambda_rt;
        scope->rt_active = false;
    }
}

static void timer_unregister_gc_roots(JsTimerHandle *th) {
    if (!th || !th->roots_registered) return;
    extern void heap_unregister_gc_root(uint64_t* slot);

    // timer roots belong to the captured heap. they do not need the captured
    // document, and re-entering a detached document can rebuild DOM globals on
    // a runtime that is being torn down.
    void* saved_doc = th->runtime_doc;
    th->runtime_doc = nullptr;
    JsTimerRuntimeScope scope;
    if (timer_runtime_enter(th, &scope)) {
        heap_unregister_gc_root(&th->callback.item);
        if (th->object.item) heap_unregister_gc_root(&th->object.item);
        heap_unregister_gc_root(&th->async_resource.item);
        heap_unregister_gc_root(&th->als_context.item);
        heap_unregister_gc_root(&th->domain.item);
        for (int j = 0; j < th->extra_count; j++) {
            heap_unregister_gc_root(&th->extra_args[j].item);
        }
        timer_runtime_exit(&scope);
    }
    th->runtime_doc = saved_doc;
    th->roots_registered = false;
}

static void timer_close_cb(uv_handle_t *handle) {
    JsTimerHandle *th = (JsTimerHandle *)handle->data;
    timer_unregister_gc_roots(th);
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
    JsTimerRuntimeScope scope;
    if (!timer_runtime_enter(th, &scope)) return;
    heap_register_gc_root(&th->callback.item);
    if (th->object.item) heap_register_gc_root(&th->object.item);
    heap_register_gc_root(&th->async_resource.item);
    heap_register_gc_root(&th->als_context.item);
    heap_register_gc_root(&th->domain.item);
    for (int j = 0; j < th->extra_count; j++) {
        heap_register_gc_root(&th->extra_args[j].item);
    }
    timer_runtime_exit(&scope);
    th->roots_registered = true;
}

static void timer_close_handle(JsTimerHandle *th) {
    if (!th || th->closing) return;
    th->closing = true;
    th->virtual_active = false;
    // close callbacks may run after document/runtime teardown; unregister roots
    // while the captured heap is still valid so late libuv cleanup only frees.
    timer_unregister_gc_roots(th);
    if (!timer_force_shutdown) {
        JsTimerRuntimeScope scope;
        if (timer_runtime_enter(th, &scope)) {
            js_async_hooks_emit_destroy_resource(th->async_resource);
            timer_runtime_exit(&scope);
        }
    }
    uv_timer_stop(&th->timer);
    uv_close((uv_handle_t *)&th->timer, timer_close_cb);
}

static void timer_mark_object_destroyed(JsTimerHandle* th) {
    if (!th || !th->object.item) return;
    // The JS Timeout object is separate from the libuv handle; update it on
    // observable clear/fire paths, not during process-exit cleanup of unref'd intervals.
    js_property_set(th->object, (Item){.item = s2it(heap_create_name("_destroyed", 10))},
	                    (Item){.item = b2it(true)});
}

static void timer_forget_unsafe_handle(JsTimerHandle* th) {
    if (!th) return;
    th->runtime_doc = nullptr;
    th->runtime_heap = nullptr;
    th->runtime_name_pool = nullptr;
    th->runtime_pool = nullptr;
    th->roots_registered = false;
    th->callback = ItemNull;
    th->object = ItemNull;
    th->async_resource = ItemNull;
    th->als_context = ItemNull;
    th->domain = ItemNull;
    for (int j = 0; j < th->extra_count; j++) {
        th->extra_args[j] = ItemNull;
    }
    th->extra_count = 0;
    th->closing = true;
    mem_free(th);
}

static void timer_abandon_all_without_uv(const char* reason_prefix) {
    for (int i = timer_handle_count - 1; i >= 0; i--) {
        JsTimerHandle* th = timer_handles[i];
        if (!th) continue;
        log_debug("%s freeing timer %lld without libuv close",
                  reason_prefix ? reason_prefix : "[JS_TIMER_ABANDON_UNSAFE]",
                  (long long)th->id);
        timer_forget_unsafe_handle(th);
        timer_handles[i] = nullptr;
    }
    timer_handle_count = 0;
    // Signal watchdog recovery can corrupt libuv queue links; abandon the loop
    // with the timer records so later global cleanup does not walk stale handles.
    lambda_uv_abandon();
}

static void timer_fire_cb(uv_timer_t *handle) {
    JsTimerHandle *th = (JsTimerHandle *)handle->data;
    timer_progress_generation++;
    bool close_after_fire = th && !th->is_interval;
    JsTimerRuntimeScope scope;
    if (timer_runtime_enter(th, &scope)) {
        Item previous_resource = js_async_hooks_enter_resource(th->async_resource);
        Item previous_domain = js_domain_set_stack(th->domain);
        if (get_type_id(th->callback) == LMD_TYPE_FUNC) {
            if (th->extra_count > 0) {
                if (th->extra_count == 1) {
                    js_als_context_call(th->als_context, th->callback, ItemNull,
                                        th->extra_args[0], 1);
                } else {
                    js_als_context_call_args(th->als_context, th->callback, ItemNull,
                                             th->extra_args, th->extra_count);
                }
            } else {
                js_als_context_call(th->als_context, th->callback, ItemNull, ItemNull, 0);
            }
        }
        js_domain_restore_stack(previous_domain);
        js_async_hooks_restore_resource(previous_resource);
        timer_runtime_exit(&scope);
    } else {
        log_error("event_loop: timer fired without captured JS runtime");
    }
    if (th && th->is_interval && js_check_exception() && !th->closing) {
        // An interval callback that throws before its clearInterval call can
        // otherwise re-enter forever and starve the drain watchdog.
        timer_mark_object_destroyed(th);
        timer_close_handle(th);
    }
    if (close_after_fire && th && !th->closing) {
        // one-shot timers must close even when their callback throws and an
        // uncaughtException listener handles it, or the refed handle never drains.
        timer_mark_object_destroyed(th);
        timer_close_handle(th);
    }
    js_microtask_flush();
}
static double item_to_ms(Item delay) {
    if (get_type_id(delay) == LMD_TYPE_FLOAT) {
        return it2d(delay);
    } else if (get_type_id(delay) == LMD_TYPE_INT) {
        return (double)it2i(delay);
    }
    return 0;
}

static void timer_format_delay(double value, char* buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;
    if (isnan(value)) {
        snprintf(buf, buf_size, "NaN");
    } else if (isinf(value)) {
        snprintf(buf, buf_size, value > 0 ? "Infinity" : "-Infinity");
    } else if (floor(value) == value) {
        snprintf(buf, buf_size, "%.0f", value);
    } else {
        snprintf(buf, buf_size, "%.15g", value);
    }
}

static void timer_emit_duration_warning(const char* name, const char* first_line) {
    char message[256];
    int len = snprintf(message, sizeof(message), "%s\nTimeout duration was set to 1.",
                       first_line ? first_line : "");
    if (len < 0) len = 0;
    if (len >= (int)sizeof(message)) len = (int)sizeof(message) - 1;

    Item warning = js_new_object();
    js_property_set(warning,
        (Item){.item = s2it(heap_create_name("name", 4))},
        (Item){.item = s2it(heap_create_name(name, (int)strlen(name)))});
    js_property_set(warning,
        (Item){.item = s2it(heap_create_name("message", 7))},
        (Item){.item = s2it(heap_create_name(message, len))});
    js_process_emit(
        (Item){.item = s2it(heap_create_name("warning", 7))},
        warning);
}

static uint64_t normalize_timer_delay(Item delay) {
    const double timeout_max = 2147483647.0;
    double ms = item_to_ms(delay);

    if (isnan(ms)) {
        if (!timer_nan_warning_emitted) {
            timer_nan_warning_emitted = true;
            char value_buf[32];
            char line[128];
            timer_format_delay(ms, value_buf, sizeof(value_buf));
            snprintf(line, sizeof(line), "%s is not a number.", value_buf);
            timer_emit_duration_warning("TimeoutNaNWarning", line);
        }
        return 1;
    }
    if (ms < 0) {
        if (!timer_negative_warning_emitted) {
            timer_negative_warning_emitted = true;
            char value_buf[32];
            char line[128];
            timer_format_delay(ms, value_buf, sizeof(value_buf));
            snprintf(line, sizeof(line), "%s is a negative number.", value_buf);
            timer_emit_duration_warning("TimeoutNegativeWarning", line);
        }
        return 1;
    }
    if (ms > timeout_max) {
        char value_buf[32];
        char line[160];
        timer_format_delay(ms, value_buf, sizeof(value_buf));
        snprintf(line, sizeof(line), "%s does not fit into a 32-bit signed integer.",
                 value_buf);
        timer_emit_duration_warning("TimeoutOverflowWarning", line);
        return 1;
    }
    if (ms < 1) return 0;
    return (uint64_t)ms;
}

// =============================================================================
// Timeout/Immediate object helpers
// =============================================================================

static JsTimerHandle* find_timer_handle(Item timer_id);

static Item timeout_this_or_arg(Item this_val) {
    if (get_type_id(this_val) == LMD_TYPE_MAP || get_type_id(this_val) == LMD_TYPE_INT) {
        return this_val;
    }
    return js_get_this();
}

// Timeout.ref()
extern "C" Item js_timeout_ref(Item this_val) {
    Item self = timeout_this_or_arg(this_val);
    JsTimerHandle* th = find_timer_handle(self);
    if (th && virtual_clock_enabled) {
        th->virtual_refed = true;
    } else if (th && !uv_is_closing((uv_handle_t*)&th->timer)) {
        uv_ref((uv_handle_t*)&th->timer);
    }
    return self;
}

// Timeout.unref()
extern "C" Item js_timeout_unref(Item this_val) {
    Item self = timeout_this_or_arg(this_val);
    JsTimerHandle* th = find_timer_handle(self);
    if (th && virtual_clock_enabled) {
        th->virtual_refed = false;
    } else if (th && !uv_is_closing((uv_handle_t*)&th->timer)) {
        uv_unref((uv_handle_t*)&th->timer);
    }
    return self;
}

// Timeout.hasRef()
extern "C" Item js_timeout_hasRef(Item this_val) {
    Item self = timeout_this_or_arg(this_val);
    JsTimerHandle* th = find_timer_handle(self);
    bool has_ref = th && (virtual_clock_enabled
        ? th->virtual_refed
        : (!uv_is_closing((uv_handle_t*)&th->timer) && uv_has_ref((uv_handle_t*)&th->timer)));
    return (Item){.item = b2it(has_ref)};
}

// Timeout.refresh() — no-op, returns this
extern "C" Item js_timeout_refresh(Item this_val) {
    return this_val;
}

// Timeout[Symbol.toPrimitive]() — returns the timer id
extern "C" Item js_timeout_toPrimitive(Item this_val) {
    Item self = timeout_this_or_arg(this_val);
    // Native method calls pass the coercion hint in the first ABI slot; recover
    // the receiver so numeric timer-handle coercion cannot yield undefined.
    Item id = js_property_get(self, (Item){.item = s2it(heap_create_name("_timerId", 8))});
    return id;
}

static Item make_timer_object(int64_t id, JsClass cls) {
    Item obj = js_new_object();
    js_property_set(obj, (Item){.item = s2it(heap_create_name("_timerId", 8))},
                    (Item){.item = i2it(id)});
    js_property_set(obj, (Item){.item = s2it(heap_create_name("_destroyed", 10))},
                    (Item){.item = b2it(false)});

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
    js_class_stamp(obj, cls);  // A3-T3b

    return obj;
}

// Extract timer id from either a plain integer or a Timeout object
static int64_t extract_timer_id(Item timer_id) {
    TypeId tid = get_type_id(timer_id);
    if (tid == LMD_TYPE_INT) {
        return it2i(timer_id);
    } else if (tid == LMD_TYPE_MAP || tid == LMD_TYPE_OBJECT || tid == LMD_TYPE_VMAP) {
        // Timeout objects may be class-stamped object shapes; clearInterval
        // must still recover _timerId or the active interval survives throws.
        Item id = js_property_get(timer_id, (Item){.item = s2it(heap_create_name("_timerId", 8))});
        if (get_type_id(id) == LMD_TYPE_INT) return it2i(id);
    }
    return -1;
}

static JsTimerHandle* find_timer_handle(Item timer_id) {
    int64_t id = extract_timer_id(timer_id);
    if (id < 0) return NULL;
    for (int i = 0; i < timer_handle_count; i++) {
        JsTimerHandle* th = timer_handles[i];
        if (th && th->id == id && !th->closing) return th;
    }
    return NULL;
}

static void timer_start(uv_loop_t* loop, JsTimerHandle* timer,
                        uint64_t timeout_ms, uint64_t repeat_ms) {
    if (!loop || !timer) return;
    if (virtual_clock_enabled) {
        // A virtual timer stays initialized as a libuv handle for common close
        // cleanup, but readiness is driven exclusively by the headless clock.
        timer->virtual_due_ms = virtual_clock_ms + (double)timeout_ms;
        timer->virtual_repeat_ms = (double)repeat_ms;
        timer->virtual_active = true;
        timer->virtual_refed = true;
        return;
    }
    // JS can schedule timers after a long document compile before libuv's first
    // turn; refresh its cached clock or the elapsed compile time makes them due.
    uv_update_time(loop);
    uv_timer_start(&timer->timer, timer_fire_cb, timeout_ms, repeat_ms);
}

extern "C" void js_event_loop_set_virtual_clock(bool enabled, double monotonic_ms) {
    virtual_clock_enabled = enabled;
    virtual_clock_ms = monotonic_ms >= 0.0 ? monotonic_ms : 0.0;
    js_performance_virtual_clock_set(enabled, virtual_clock_ms);
}

extern "C" bool js_event_loop_virtual_clock_enabled(void) {
    return virtual_clock_enabled;
}

extern "C" double js_event_loop_virtual_clock_now_ms(void) {
    return virtual_clock_ms;
}

static JsTimerHandle* virtual_timer_next_due(double target_ms) {
    JsTimerHandle* next = nullptr;
    for (int index = 0; index < timer_handle_count; index++) {
        JsTimerHandle* timer = timer_handles[index];
        if (!timer || timer->closing || !timer->virtual_active ||
            timer->virtual_due_ms > target_ms) continue;
        if (!next || timer->virtual_due_ms < next->virtual_due_ms ||
            (timer->virtual_due_ms == next->virtual_due_ms && timer->id < next->id)) {
            next = timer;
        }
    }
    return next;
}

static int virtual_timer_fire_due(double target_ms) {
    const int callback_limit = MAX_TIMER_HANDLES * 64;
    int fired = 0;
    while (fired < callback_limit) {
        JsTimerHandle* timer = virtual_timer_next_due(target_ms);
        if (!timer) break;

        virtual_clock_ms = timer->virtual_due_ms;
        js_performance_virtual_clock_set(true, virtual_clock_ms);
        if (timer->is_interval) {
            // Publish the next interval deadline before the callback so
            // clearInterval and nested timer creation see browser ordering.
            timer->virtual_due_ms += timer->virtual_repeat_ms > 0.0
                ? timer->virtual_repeat_ms : 1.0;
        } else {
            timer->virtual_active = false;
        }
        timer_fire_cb(&timer->timer);
        fired++;

        uv_loop_t* loop = lambda_uv_loop();
        if (loop) uv_run(loop, UV_RUN_NOWAIT);
    }
    if (virtual_timer_next_due(target_ms)) {
        log_error("event_loop: virtual timer drain exceeded %d callbacks", callback_limit);
    }
    return fired;
}

static int virtual_clock_advance_slice(double target_ms, bool animation_frame) {
    int progress = virtual_timer_fire_due(target_ms);
    virtual_clock_ms = target_ms;
    js_performance_virtual_clock_set(true, virtual_clock_ms);

    if (animation_frame) {
        progress += js_animation_frame_flush(virtual_clock_ms);
        if (js_dom_tick_headless_animation_frame()) progress++;
    }
    js_microtask_flush();
    // rAF and microtasks may queue zero-delay timers at this same timestamp.
    progress += virtual_timer_fire_due(target_ms);
    return progress;
}

extern "C" int js_event_loop_advance_virtual_time(double delta_ms, int frame_steps) {
    if (!virtual_clock_enabled) return 0;
    if (delta_ms < 0.0) delta_ms = 0.0;

    const double frame_ms = 1000.0 / 60.0;
    const int frame_limit = 4096;
    double start_ms = virtual_clock_ms;
    double target_ms = start_ms + delta_ms;
    int frames = frame_steps;
    if (frames <= 0 && delta_ms > 0.0) {
        frames = (int)ceil(delta_ms / frame_ms);
    }
    if (frames > frame_limit) {
        log_error("event_loop: virtual animation drain limited from %d to %d frames",
                  frames, frame_limit);
        frames = frame_limit;
    }

    int progress = 0;
    for (int frame = 1; frame <= frames; frame++) {
        double slice_ms = start_ms + delta_ms * ((double)frame / (double)frames);
        progress += virtual_clock_advance_slice(slice_ms, true);
    }
    if (frames == 0) {
        progress += virtual_clock_advance_slice(target_ms, false);
    } else if (virtual_clock_ms < target_ms) {
        progress += virtual_clock_advance_slice(target_ms, false);
    }
    return progress;
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

    uint64_t ms = normalize_timer_delay(delay);

    JsTimerHandle *th = (JsTimerHandle *)mem_calloc(1, sizeof(JsTimerHandle), MEM_CAT_JS_RUNTIME);
    if (!th) return ItemNull;

    th->id = next_timer_id++;
    th->callback = callback;
    th->is_interval = false;
    th->extra_count = 0;
    th->timer.data = th;
    timer_capture_runtime(th, "Timeout", 7);

    uv_timer_init(loop, &th->timer);
    timer_start(loop, th, ms, 0);
    Item timer_obj = make_timer_object(th->id, JS_CLASS_TIMEOUT);
    th->object = timer_obj;
    timer_register_gc_roots(th);

    if (timer_handle_count < MAX_TIMER_HANDLES) {
        timer_handles[timer_handle_count++] = th;
    }

    return timer_obj;
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

    uint64_t ms = normalize_timer_delay(delay);

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
    timer_capture_runtime(th, "Timeout", 7);

    uv_timer_init(loop, &th->timer);
    timer_start(loop, th, ms, 0);
    Item timer_obj = make_timer_object(th->id, JS_CLASS_TIMEOUT);
    th->object = timer_obj;
    timer_register_gc_roots(th);

    if (timer_handle_count < MAX_TIMER_HANDLES) {
        timer_handles[timer_handle_count++] = th;
    }

    return timer_obj;
}

static Item js_setImmediate_impl(Item callback, Item args_array, bool has_args) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        extern Item js_throw_type_error_code(const char*, const char*);
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "The \"callback\" argument must be of type function.");
    }
    uv_loop_t *loop = lambda_uv_loop();
    if (!loop) {
        log_error("event_loop: uv loop not initialized for setImmediate");
        return ItemNull;
    }

    JsTimerHandle *th = (JsTimerHandle *)mem_calloc(1, sizeof(JsTimerHandle), MEM_CAT_JS_RUNTIME);
    if (!th) return ItemNull;

    th->id = next_timer_id++;
    th->callback = callback;
    th->is_interval = false;
    th->extra_count = 0;
    th->timer.data = th;

    if (has_args && get_type_id(args_array) == LMD_TYPE_ARRAY) {
        Array* arr = args_array.array;
        int count = (int)arr->length;
        if (count > 8) count = 8;
        for (int i = 0; i < count; i++) {
            th->extra_args[i] = arr->items[i];
        }
        th->extra_count = count;
    }

    timer_capture_runtime(th, "Immediate", 9);

    uv_timer_init(loop, &th->timer);
    // Immediates queued while draining the current check phase belong to the next turn.
    timer_start(loop, th, 1, 0);
    Item timer_obj = make_timer_object(th->id, JS_CLASS_IMMEDIATE);
    th->object = timer_obj;
    timer_register_gc_roots(th);

    if (timer_handle_count < MAX_TIMER_HANDLES) {
        timer_handles[timer_handle_count++] = th;
    }

    return timer_obj;
}

extern "C" Item js_setImmediate_timer(Item callback) {
    return js_setImmediate_impl(callback, ItemNull, false);
}

extern "C" Item js_setImmediate_timer_args(Item callback, Item args_array) {
    return js_setImmediate_impl(callback, args_array, true);
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

    uint64_t ms = normalize_timer_delay(delay);
    if (ms < 1) ms = 1; // minimum interval

    JsTimerHandle *th = (JsTimerHandle *)mem_calloc(1, sizeof(JsTimerHandle), MEM_CAT_JS_RUNTIME);
    if (!th) return ItemNull;

    th->id = next_timer_id++;
    th->callback = callback;
    th->is_interval = true;
    th->extra_count = 0;
    th->timer.data = th;
    timer_capture_runtime(th, "Timeout", 7);

    uv_timer_init(loop, &th->timer);
    timer_start(loop, th, ms, ms);
    Item timer_obj = make_timer_object(th->id, JS_CLASS_TIMEOUT);
    th->object = timer_obj;
    timer_register_gc_roots(th);

    if (timer_handle_count < MAX_TIMER_HANDLES) {
        timer_handles[timer_handle_count++] = th;
    }

    return timer_obj;
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

    uint64_t ms = normalize_timer_delay(delay);
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
    timer_capture_runtime(th, "Timeout", 7);

    uv_timer_init(loop, &th->timer);
    timer_start(loop, th, ms, ms);
    Item timer_obj = make_timer_object(th->id, JS_CLASS_TIMEOUT);
    th->object = timer_obj;
    timer_register_gc_roots(th);

    if (timer_handle_count < MAX_TIMER_HANDLES) {
        timer_handles[timer_handle_count++] = th;
    }

    return timer_obj;
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

extern "C" void js_mock_scheduler_enable(void) {
    mock_scheduler_enabled = true;
    mock_scheduler_now_ms = 0;
    memset(mock_scheduler_waits, 0, sizeof(mock_scheduler_waits));
}

extern "C" void js_mock_scheduler_reset(void) {
    mock_scheduler_enabled = false;
    mock_scheduler_now_ms = 0;
    memset(mock_scheduler_waits, 0, sizeof(mock_scheduler_waits));
}

extern "C" void js_mock_scheduler_tick(Item delay) {
    if (!mock_scheduler_enabled) return;
    mock_scheduler_now_ms += (int64_t)normalize_timer_delay(delay);
    Item undef = (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};

    for (int i = 0; i < MAX_MOCK_SCHEDULER_WAITS; i++) {
        JsMockSchedulerWait* wait = &mock_scheduler_waits[i];
        if (!wait->active || wait->due_ms > mock_scheduler_now_ms) continue;
        wait->active = false;
        if (get_type_id(wait->signal) == LMD_TYPE_MAP ||
            get_type_id(wait->signal) == LMD_TYPE_OBJECT) {
            Item aborted = js_property_get(wait->signal,
                (Item){.item = s2it(heap_create_name("aborted", 7))});
            if (get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted)) {
                Item err = make_abort_error(wait->signal);
                Item args[1] = { err };
                js_call_function(wait->reject, ItemNull, args, 1);
                continue;
            }
        }
        Item args[1] = { undef };
        js_call_function(wait->resolve, ItemNull, args, 1);
    }
    js_microtask_flush();
}

static Item js_mock_scheduler_wait(Item delay, Item options) {
    extern Item js_promise_with_resolvers(void);
    extern Item js_promise_reject(Item reason);

    Item reject_out = ItemNull;
    int opt_rc = check_timer_options(options, &reject_out);
    if (opt_rc != 0) return reject_out;

    Item resolvers = js_promise_with_resolvers();
    Item promise = js_property_get(resolvers, (Item){.item = s2it(heap_create_name("promise", 7))});
    Item resolve_fn = js_property_get(resolvers, (Item){.item = s2it(heap_create_name("resolve", 7))});
    Item reject_fn = js_property_get(resolvers, (Item){.item = s2it(heap_create_name("reject", 6))});

    Item signal = (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
    if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_OBJECT) {
        signal = js_property_get(options, (Item){.item = s2it(heap_create_name("signal", 6))});
    }

    for (int i = 0; i < MAX_MOCK_SCHEDULER_WAITS; i++) {
        JsMockSchedulerWait* wait = &mock_scheduler_waits[i];
        if (wait->active) continue;
        wait->promise = promise;
        wait->resolve = resolve_fn;
        wait->reject = reject_fn;
        wait->signal = signal;
        wait->due_ms = mock_scheduler_now_ms + (int64_t)normalize_timer_delay(delay);
        wait->active = true;
        // Mock scheduler waits must not allocate real uv timers; otherwise
        // official fake-timer tests sleep for the virtual delay before passing.
        return promise;
    }

    return js_promise_reject(js_new_error(
        (Item){.item = s2it(heap_create_name("Mock scheduler wait queue full", 35))}));
}

static void mock_scheduler_register_gc_roots(void) {
    uint64_t epoch = js_get_heap_epoch();
    if (mock_scheduler_roots_epoch == epoch) return;
    for (int i = 0; i < MAX_MOCK_SCHEDULER_WAITS; i++) {
        heap_register_gc_root(&mock_scheduler_waits[i].promise.item);
        heap_register_gc_root(&mock_scheduler_waits[i].resolve.item);
        heap_register_gc_root(&mock_scheduler_waits[i].reject.item);
        heap_register_gc_root(&mock_scheduler_waits[i].signal.item);
    }
    mock_scheduler_roots_epoch = epoch;
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

    uint64_t ms = normalize_timer_delay(delay);

    JsTimerHandle *th = (JsTimerHandle *)mem_calloc(1, sizeof(JsTimerHandle), MEM_CAT_JS_RUNTIME);
    if (!th) return promise;

    th->id = next_timer_id++;
    th->callback = resolve_fn;
    th->is_interval = false;
    th->extra_args[0] = value;
    th->extra_count = 1;
    th->timer.data = th;
    timer_capture_runtime(th, "Timeout", 7);

    uv_timer_init(loop, &th->timer);
    timer_start(loop, th, ms, 0);
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

extern "C" Item js_setTimeout_promisified(Item delay, Item value) {
    Item undef = (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
    return js_setTimeout_promise(delay, value, undef);
}

extern "C" void js_timer_install_promisify_custom(Item fn_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    Item custom_fn = js_new_function((void*)js_setTimeout_promisified, 2);
    js_property_set(fn_item, js_util_promisify_custom_symbol(), custom_fn);
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
    timer_capture_runtime(th, "Immediate", 9);

    uv_timer_init(loop, &th->timer);
    // Promise immediates share setImmediate's next-turn scheduling invariant.
    timer_start(loop, th, 1, 0);
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
    if (mock_scheduler_enabled) return js_mock_scheduler_wait(delay, options);
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
            timer_mark_object_destroyed(th);
            timer_close_handle(th);
            return;
        }
    }
}

extern "C" void js_clearInterval(Item timer_id) {
    js_clearTimeout(timer_id);
}

extern "C" void js_event_loop_cancel_document_timers(void* dom_doc) {
    if (!dom_doc) return;

    for (int i = 0; i < timer_handle_count; i++) {
        JsTimerHandle *th = timer_handles[i];
        if (!th || th->runtime_doc != dom_doc) continue;

        log_debug("[JS_TIMER_DETACH] canceling timer %lld for document %p",
                  (long long)th->id, dom_doc);
        th->runtime_doc = nullptr;
        timer_unregister_gc_roots(th);
        th->callback = ItemNull;
        th->async_resource = ItemNull;
        th->als_context = ItemNull;
        for (int j = 0; j < th->extra_count; j++) {
            th->extra_args[j] = ItemNull;
        }
        th->extra_count = 0;
        timer_close_handle(th);
    }
}

extern "C" void js_event_loop_abandon_document_timers(void* dom_doc) {
    if (!dom_doc) return;

    bool found = false;
    for (int i = 0; i < timer_handle_count; i++) {
        JsTimerHandle *th = timer_handles[i];
        if (th && th->runtime_doc == dom_doc) {
            found = true;
            break;
        }
    }
    if (found) {
        log_debug("[JS_TIMER_ABANDON] unsafe document %p owns timer handles", dom_doc);
        timer_abandon_all_without_uv("[JS_TIMER_ABANDON]");
    }
}

extern "C" void js_event_loop_abandon_all_timers(void) {
    timer_abandon_all_without_uv("[JS_TIMER_ABANDON_ALL]");
}

// =============================================================================
// Event Loop Lifecycle
// =============================================================================

extern "C" void js_event_loop_init(void) {
    lambda_concurrency_js_init();
    if (context && !context->scheduler && js_source_runtime &&
            js_source_runtime->js_runtime_used) {
        // Pure-JS contexts must retain their existing loop footprint. Attach a
        // Lambda scheduler only after a cross-language module activates the
        // membrane, so exported procedures can progress on this libuv loop.
        context->scheduler = js_source_runtime && js_source_runtime->scheduler
            ? js_source_runtime->scheduler
            : lambda_scheduler_create(LAMBDA_MAILBOX_DEFAULT_CAPACITY);
        if (!js_source_runtime->scheduler) {
            js_source_runtime->scheduler = context->scheduler;
        }
    }
    if (timer_handle_count > 0) {
        // Host-driven sessions (Radiant `view`) share ONE event loop across all
        // of a page's script executions, matching the browser. A later page
        // script re-entering init must not discard timers a prior script queued
        // (e.g. an editor's post-mount setTimeout(0)); the host drains them after
        // committing layout. The loop is already live here (timers exist), so
        // preserve it. The first init of a session runs fully (timer_count == 0).
        if (js_dom_is_host_driven_loop()) {
            event_loop_shutting_down = false;
            return;
        }
        js_event_loop_shutdown();
    }
    event_loop_shutting_down = false;

    // reset task queues — zero ring buffers to prevent GC scanning stale Items
    memset(next_tick_ring, 0, sizeof(next_tick_ring));
    memset(next_tick_resource_ring, 0, sizeof(next_tick_resource_ring));
    memset(next_tick_als_ring, 0, sizeof(next_tick_als_ring));
    memset(next_tick_domain_ring, 0, sizeof(next_tick_domain_ring));
    memset(microtask_ring, 0, sizeof(microtask_ring));
    memset(microtask_resource_ring, 0, sizeof(microtask_resource_ring));
    memset(microtask_als_ring, 0, sizeof(microtask_als_ring));
    memset(microtask_domain_ring, 0, sizeof(microtask_domain_ring));
    memset(raf_callback_ring, 0, sizeof(raf_callback_ring));
    next_tick_head = 0;
    next_tick_tail = 0;
    next_tick_count = 0;
    microtask_head = 0;
    microtask_tail = 0;
    microtask_count = 0;
    raf_head = 0;
    raf_tail = 0;
    raf_count = 0;
    next_raf_id = 1;
    // reset timers — clear stale callback pointers
    for (int i = 0; i < timer_handle_count; i++) {
        timer_handles[i] = NULL;
    }
    timer_handle_count = 0;
    next_timer_id = 1;
    timer_nan_warning_emitted = false;
    timer_negative_warning_emitted = false;

    // register task ring buffers as GC roots (static memory invisible to stack scanning)
    static struct gc_heap* statics_rooted_gc = NULL;
    struct gc_heap* active_gc = context && context->heap ? context->heap->gc : NULL;
    if (active_gc && statics_rooted_gc != active_gc) {
        // Batch heap replacement creates a new root registry; static JS queues
        // must be registered with that heap even though their addresses persist.
        heap_register_gc_root_range((uint64_t*)next_tick_ring, MICROTASK_CAPACITY);
        heap_register_gc_root_range((uint64_t*)next_tick_resource_ring, MICROTASK_CAPACITY);
        heap_register_gc_root_range((uint64_t*)next_tick_als_ring, MICROTASK_CAPACITY);
        heap_register_gc_root_range((uint64_t*)next_tick_domain_ring, MICROTASK_CAPACITY);
        heap_register_gc_root_range((uint64_t*)microtask_ring, MICROTASK_CAPACITY);
        heap_register_gc_root_range((uint64_t*)microtask_resource_ring, MICROTASK_CAPACITY);
        heap_register_gc_root_range((uint64_t*)microtask_als_ring, MICROTASK_CAPACITY);
        heap_register_gc_root_range((uint64_t*)microtask_domain_ring, MICROTASK_CAPACITY);
        heap_register_gc_root_range((uint64_t*)raf_callback_ring, RAF_CAPACITY);
        // Mock scheduler waits are static, so queued promise resolvers must be
        // rooted individually rather than via the whole struct with non-Item fields.
        mock_scheduler_register_gc_roots();
        statics_rooted_gc = active_gc;
    }

    // initialize libuv loop
    lambda_uv_init();

    // register task drain to run at libuv phase checkpoints
    lambda_uv_set_microtask_drain(js_microtask_flush);
}

extern "C" void js_event_loop_shutdown(void) {
    uv_loop_t* loop = lambda_uv_loop();

    event_loop_shutting_down = true;
    timer_force_shutdown = true;
    close_all_timer_handles();
    if (loop) {
        int safety = MAX_TIMER_HANDLES + 16;
        while (timer_handle_count > 0 && safety-- > 0) {
            uv_run(loop, UV_RUN_NOWAIT);
        }
    }
    timer_force_shutdown = false;

    if (timer_handle_count > 0) {
        log_error("event_loop: shutdown left %d timer handle(s) pending close",
                  timer_handle_count);
    }

    memset(next_tick_ring, 0, sizeof(next_tick_ring));
    memset(next_tick_resource_ring, 0, sizeof(next_tick_resource_ring));
    memset(next_tick_als_ring, 0, sizeof(next_tick_als_ring));
    memset(microtask_ring, 0, sizeof(microtask_ring));
    memset(microtask_resource_ring, 0, sizeof(microtask_resource_ring));
    memset(microtask_als_ring, 0, sizeof(microtask_als_ring));
    memset(raf_callback_ring, 0, sizeof(raf_callback_ring));
    memset(raf_id_ring, 0, sizeof(raf_id_ring));
    next_tick_head = 0;
    next_tick_tail = 0;
    next_tick_count = 0;
    microtask_head = 0;
    microtask_tail = 0;
    microtask_count = 0;
    raf_head = 0;
    raf_tail = 0;
    raf_count = 0;
}

// Recovery mechanism for SIGSEGV during event loop drain.
// Heap corruption in timer callbacks (pre-existing bug) can crash the process
// after tests have completed. We catch the signal and abort the event loop
// gracefully instead of terminating.
#ifndef _WIN32
static sigjmp_buf event_loop_jmpbuf;
static volatile sig_atomic_t event_loop_guarded = 0;
static volatile sig_atomic_t event_loop_fault_signal = 0;
static struct sigaction event_loop_old_sa;

static void event_loop_sigsegv_handler(int sig, siginfo_t* info, void* ctx) {
    if (event_loop_guarded) {
        // Signal handlers cannot safely log or mutate the allocator. Record the
        // fault and restore runtime watermarks at the sigsetjmp landing point.
        event_loop_fault_signal = sig;
        event_loop_guarded = 0;
        siglongjmp(event_loop_jmpbuf, 1);
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
#endif  // !_WIN32

// Maximum time (ms) the event loop drain is allowed to run before being
// forcefully stopped.  Prevents infinite blocking from setInterval() or
// long-running timers in document scripts (e.g. CSS animation test pages).
#define EVENT_LOOP_DRAIN_TIMEOUT_MS 5000
#define EVENT_LOOP_PROCESS_DRAIN_TIMEOUT_MS 30000

#ifndef NDEBUG
typedef struct JsUvDumpState {
    int count;
} JsUvDumpState;

static void event_loop_dump_handle_cb(uv_handle_t* h, void* arg) {
    JsUvDumpState* state = (JsUvDumpState*)arg;
    if (!state || !h) return;
    state->count++;

    const char* type_name = uv_handle_type_name(h->type);
    if (!type_name) type_name = "unknown";

    log_debug("event_loop: drain handle #%d type=%s handle=%p data=%p active=%d closing=%d ref=%d",
              state->count, type_name, h, h->data,
              uv_is_active(h), uv_is_closing(h), uv_has_ref(h));
}

static void event_loop_dump_active_handles(void) {
    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) return;

    JsUvDumpState state;
    state.count = 0;
    log_debug("event_loop: drain active handle dump begin");
    uv_walk(loop, event_loop_dump_handle_cb, &state);
    log_debug("event_loop: drain active handle dump end count=%d", state.count);
}
#endif

static void drain_watchdog_cb(uv_timer_t* handle) {
    log_debug("event_loop: drain watchdog fired — stopping loop");
#ifndef NDEBUG
    event_loop_dump_active_handles();
#endif
    lambda_uv_stop();
}

typedef struct JsCloseRefedState {
    uv_handle_t* skip;
    int closed;
} JsCloseRefedState;

static void event_loop_close_refed_handle_cb(uv_handle_t* h, void* arg) {
    JsCloseRefedState* state = (JsCloseRefedState*)arg;
    if (!state || !h || h == state->skip || uv_is_closing(h) || !uv_has_ref(h)) return;
    for (int i = 0; i < timer_handle_count; i++) {
        JsTimerHandle* th = timer_handles[i];
        if (!th || h != (uv_handle_t*)&th->timer) continue;
        // Watchdog cleanup must update the JS timer owner before closing libuv;
        // a raw close leaves the registry live and clearTimeout closes it twice.
        timer_close_handle(th);
        state->closed++;
        return;
    }
    // Watchdog exit leaves stale refed native handles behind; close them here
    // or global uv cleanup repeats the same wait during process teardown.
    uv_close(h, NULL);
    state->closed++;
}

static int event_loop_close_refed_handles_after_watchdog(uv_loop_t* loop, uv_handle_t* skip) {
    if (!loop) return 0;
    JsCloseRefedState state;
    state.skip = skip;
    state.closed = 0;
    event_loop_shutting_down = true;
    uv_walk(loop, event_loop_close_refed_handle_cb, &state);
    for (int i = 0; i < 8 && state.closed > 0; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }
    return state.closed;
}

typedef struct JsRefedHandleState {
    bool has_refed;
} JsRefedHandleState;

static void event_loop_refed_handle_cb(uv_handle_t* h, void* arg) {
    JsRefedHandleState* state = (JsRefedHandleState*)arg;
    if (!state || state->has_refed || !h || uv_is_closing(h) || !uv_is_active(h)) return;
    if (uv_has_ref(h)) state->has_refed = true;
}

extern "C" bool js_event_loop_has_refed_handles(void) {
    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) return false;
    JsRefedHandleState state;
    state.has_refed = false;
    uv_walk(loop, event_loop_refed_handle_cb, &state);
    return state.has_refed;
}

static void event_loop_refed_process_handle_cb(uv_handle_t* h, void* arg) {
    bool* has_process = (bool*)arg;
    if (!has_process || *has_process || !h || h->type != UV_PROCESS ||
        uv_is_closing(h) || !uv_is_active(h)) {
        return;
    }
    if (uv_has_ref(h)) *has_process = true;
}

static bool event_loop_has_refed_process_handles(uv_loop_t* loop) {
    if (!loop) return false;
    bool has_process = false;
    uv_walk(loop, event_loop_refed_process_handle_cb, &has_process);
    return has_process;
}

typedef struct JsDrainWatchdogState {
    bool fired;
    uint64_t start_ns;
    uint64_t progress_generation;
} JsDrainWatchdogState;

static void drain_watchdog_timer_cb(uv_timer_t* handle) {
    JsDrainWatchdogState* state = handle ? (JsDrainWatchdogState*)handle->data : NULL;
    uint64_t elapsed_ms = state
        ? (uv_hrtime() - state->start_ns) / 1000000ULL
        : (uint64_t)EVENT_LOOP_PROCESS_DRAIN_TIMEOUT_MS;
    if (state && state->progress_generation != timer_progress_generation &&
        elapsed_ms < (uint64_t)EVENT_LOOP_PROCESS_DRAIN_TIMEOUT_MS) {
        // The short watchdog bounds an idle drain, not a productive timer chain;
        // slow debug layouts can cross five seconds while still making progress.
        state->progress_generation = timer_progress_generation;
        uint64_t remaining = (uint64_t)EVENT_LOOP_PROCESS_DRAIN_TIMEOUT_MS - elapsed_ms;
        uint64_t next_ms = remaining < (uint64_t)EVENT_LOOP_DRAIN_TIMEOUT_MS
            ? remaining
            : (uint64_t)EVENT_LOOP_DRAIN_TIMEOUT_MS;
        if (next_ms == 0) next_ms = 1;
        uv_timer_start(handle, drain_watchdog_timer_cb, next_ms, 0);
        return;
    }
    // Process handles get the longer startup grace only while they still exist;
    // once they close, remaining TCP/timer leaks should hit the normal watchdog.
    if (event_loop_has_refed_process_handles(lambda_uv_loop()) &&
        elapsed_ms < (uint64_t)EVENT_LOOP_PROCESS_DRAIN_TIMEOUT_MS) {
        uint64_t remaining = (uint64_t)EVENT_LOOP_PROCESS_DRAIN_TIMEOUT_MS - elapsed_ms;
        uint64_t next_ms = remaining < (uint64_t)EVENT_LOOP_DRAIN_TIMEOUT_MS
            ? remaining
            : (uint64_t)EVENT_LOOP_DRAIN_TIMEOUT_MS;
        if (next_ms == 0) next_ms = 1;
        uv_timer_start(handle, drain_watchdog_timer_cb, next_ms, 0);
        return;
    }
    if (state) state->fired = true;
    drain_watchdog_cb(handle);
}

// Stop and close all active interval timers so they don't keep the event
// loop alive after drain completes or times out.
static void stop_all_interval_timers(void) {
    for (int i = timer_handle_count - 1; i >= 0; i--) {
        JsTimerHandle* th = timer_handles[i];
        if (th && th->is_interval) {
            timer_close_handle(th);
        }
    }
}

// Auto-close mode matches closing the page after load/onload: immediate tasks
// queued by onload get a browser-like macrotask turn, but pending timers must
// not keep the static layout pass alive.
static void close_all_timer_handles(void) {
    for (int i = timer_handle_count - 1; i >= 0; i--) {
        JsTimerHandle* th = timer_handles[i];
        if (th) {
            timer_close_handle(th);
        }
    }
}

// Js57 P2c: bounded loop drain for js_await_sync on a pending promise.
//
// Drains microtasks + libuv loop in tight non-blocking turns, polling the caller's
// `predicate` after each turn. Returns 0 if the predicate ever returns non-zero
// (loop made forward progress), or -1 if the bound expired with predicate still 0.
//
// Three independent bounds prevent the Js55 P23(b) catastrophe (full-loop drain
// from inside js_await_sync went 155s → 1675s on the suite):
//   * watchdog_ms      — hard wall-clock cap per call (suggested 100ms);
//   * max_no_progress  — successive turns where nothing was popped from
//                        microtask/nextTick queues count as "no progress"; on
//                        the Nth such turn we give up immediately;
//   * max_turns        — absolute upper bound on uv_run iterations.
//
// This is intentionally narrower than js_event_loop_drain: no watchdog timer,
// no signal handler installation, no interval-timer cleanup. The intent is a
// brief "hand off control so cross-module promise resolution can happen", not
// a full event loop run.
extern "C" int js_await_bounded_drain(int (*predicate)(void*), void* user,
                                      int watchdog_ms, int max_no_progress,
                                      int max_turns) {
    if (!predicate) return -1;
    if (predicate(user)) return 0;
    js_microtask_flush();
    if (predicate(user)) return 0;

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) return -1;

    if (watchdog_ms <= 0) watchdog_ms = 100;
    if (max_no_progress <= 0) max_no_progress = 3;
    if (max_turns <= 0) max_turns = 64;

    uint64_t start_ns = uv_hrtime();
    uint64_t watchdog_ns = (uint64_t)watchdog_ms * 1000000ULL;
    int no_progress = 0;

    for (int turn = 0; turn < max_turns; turn++) {
        int before = next_tick_count + microtask_count;
        uv_run(loop, UV_RUN_NOWAIT);
        int after_uv = next_tick_count + microtask_count;
        js_microtask_flush();
        if (predicate(user)) return 0;

        bool made_progress = (after_uv > before) || (after_uv != 0);
        if (made_progress) {
            no_progress = 0;
        } else {
            no_progress++;
            if (no_progress >= max_no_progress) break;
        }

        if (uv_hrtime() - start_ns > watchdog_ns) break;
    }
    return predicate(user) ? 0 : -1;
}

// Bounded, non-blocking pump: fire currently-ready timers and flush microtasks
// WITHOUT blocking to the watchdog (unlike js_event_loop_drain, which runs the
// loop to completion / watchdog). Used by the headless event simulator to
// deliver setTimeout(0)-scheduled callbacks (e.g. the coalesced
// `selectionchange` dispatch) between simulated events, without spinning when a
// callback re-schedules itself.
extern "C" void js_event_loop_pump_nowait(void) {
    if (virtual_clock_enabled) {
        js_event_loop_advance_virtual_time(0.0, 0);
    }
    js_microtask_flush();
    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) return;
    for (int turn = 0; turn < 4; turn++) {
        int active = uv_run(loop, UV_RUN_NOWAIT);
        js_microtask_flush();
        if (!active) break;
    }
}

typedef struct JsPumpWaitState {
    bool cap_fired;
} JsPumpWaitState;

static void event_loop_pump_wait_cap_cb(uv_timer_t* handle) {
    JsPumpWaitState* state = handle ? (JsPumpWaitState*)handle->data : NULL;
    if (state) state->cap_fired = true;
}

extern "C" bool js_event_loop_pump_wait(int max_wait_ms) {
    if (virtual_clock_enabled) {
        return js_event_loop_advance_virtual_time((double)(max_wait_ms > 0 ? max_wait_ms : 0), 0) > 0;
    }
    bool had_microtasks = js_microtask_pending_count() > 0;
    js_microtask_flush();
    if (had_microtasks) return true;

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop || max_wait_ms <= 0) {
        js_event_loop_pump_nowait();
        return false;
    }

    // A one-shot cap lets libuv wake on the next real timer/I/O event without
    // turning assertion waits into a fixed-interval nanosleep loop.
    JsPumpWaitState state;
    state.cap_fired = false;
    uv_timer_t cap;
    if (uv_timer_init(loop, &cap) != 0) {
        js_event_loop_pump_nowait();
        return false;
    }
    cap.data = &state;
    uv_timer_start(&cap, event_loop_pump_wait_cap_cb, (uint64_t)max_wait_ms, 0);
    uv_run(loop, UV_RUN_ONCE);
    uv_timer_stop(&cap);
    uv_close((uv_handle_t*)&cap, NULL);
    uv_run(loop, UV_RUN_NOWAIT);
    js_microtask_flush();
    return !state.cap_fired;
}

extern "C" int js_event_loop_drain(void) {
    // flush any synchronous microtasks first (from Promise resolutions)
    js_microtask_flush();

    // Host-driven sessions (Radiant `view`) own the timer/rAF cadence and pump
    // the loop via js_event_loop_pump_nowait() AFTER committing the first layout.
    // A drain here — reached from the document loader before that commit — would
    // fire load-time setTimeout(0) callbacks too early, against an uncommitted
    // document, so geometry queries read zero boxes. Microtasks (promise jobs)
    // are already flushed above; leave timers queued for the host's pump.
    if (js_dom_is_host_driven_loop()) return 0;

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) return 0;

    if (js_process_exit_requested()) {
        // process.exit() must bypass ordinary libuv liveness; Node does not wait
        // for servers or sockets once user code requested hard termination.
        close_all_timer_handles();
        uv_run(loop, UV_RUN_NOWAIT);
        return 0;
    }

    if (auto_close_mode) {
        for (int turn = 0; turn < 4; turn++) {
            int active = uv_run(loop, UV_RUN_NOWAIT);
            js_microtask_flush();
            if (!active || timer_handle_count == 0) break;
        }
        close_all_timer_handles();
        uv_run(loop, UV_RUN_NOWAIT);
        js_microtask_flush();
        return 0;
    }

#ifndef _WIN32
    // install crash guard for event loop drain
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = event_loop_sigsegv_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, &event_loop_old_sa);
    event_loop_guarded = 1;
    event_loop_fault_signal = 0;

    int result = 0;
    LambdaRecoveryCheckpoint recovery_checkpoint =
        lambda_recovery_checkpoint_capture((Context*)context);
    if (sigsetjmp(event_loop_jmpbuf, 1) == 0) {
#else
    int result = 0;
    {
#endif
        // install watchdog timer to prevent infinite blocking from setInterval
        uv_timer_t watchdog;
        JsDrainWatchdogState watchdog_state;
        watchdog_state.fired = false;
        watchdog_state.start_ns = uv_hrtime();
        watchdog_state.progress_generation = timer_progress_generation;
        uv_timer_init(loop, &watchdog);
        watchdog.data = &watchdog_state;
        uv_unref((uv_handle_t*)&watchdog); // don't let watchdog itself keep loop alive when alone
        uv_timer_start(&watchdog, drain_watchdog_timer_cb,
                       EVENT_LOOP_DRAIN_TIMEOUT_MS, 0);

        // Browser page-load drains should not wait for persistent intervals; close
        // them before blocking so recurring timers cannot stall static rendering.
        stop_all_interval_timers();
        uv_run(loop, UV_RUN_NOWAIT);

        // run libuv event loop until all one-shot timers/handles are done (or watchdog fires)
        result = lambda_uv_run();

        int animation_frames = 0;
        while (!watchdog_state.fired && js_dom_tick_headless_animation_frame() &&
               animation_frames < 256) {
            js_microtask_flush();
            uv_run(loop, UV_RUN_NOWAIT);
            animation_frames++;
        }
        if (animation_frames >= 256 && js_dom_tick_headless_animation_frame()) {
            log_error("event_loop: headless CSS animation drain exceeded 256 frames");
        }

        if (watchdog_state.fired) {
            event_loop_close_refed_handles_after_watchdog(loop, (uv_handle_t*)&watchdog);
        }

        // clean up watchdog
        uv_timer_stop(&watchdog);
        uv_close((uv_handle_t*)&watchdog, NULL);
        // run once more to process the close callback
        uv_run(loop, UV_RUN_NOWAIT);

        // stop any interval timers created by one-shot callbacks during drain
        stop_all_interval_timers();
        // drain close callbacks from stopped intervals
        uv_run(loop, UV_RUN_NOWAIT);

        // final microtask flush after loop exits
        js_microtask_flush();
        lambda_recovery_checkpoint_disarm(&recovery_checkpoint);
    }
#ifndef _WIN32
    else {
        lambda_recovery_checkpoint_restore(&recovery_checkpoint);
        log_error("event_loop: recovered from signal %d during drain",
                  (int)event_loop_fault_signal);
        result = -1;
    }

    // restore previous signal handler
    event_loop_guarded = 0;
    sigaction(SIGSEGV, &event_loop_old_sa, NULL);
#endif

    return result;
}
