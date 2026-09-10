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
#include "../dom/dom.h"
#include "js_runtime.h"
#include "js_runtime_state.hpp"
#include "js_class.h"
#include "../lambda-data.hpp"
#include "../runtime/transpiler.hpp"
#include "../runtime/concurrency.h"
#include "../runtime/concurrency_js.h"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"
#include "../../lib/arraylist.h"

#include <cstring>
#include <cmath>
#include "../../lib/mem.h"
#include <cstdio>

extern __thread EvalContext* context;
extern Item js_make_number(double value);
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
extern "C" Item js_domain_capture_stack(void);
extern "C" Item js_domain_capture_async_stack(void);
extern "C" Item js_domain_set_stack(Item stack);
extern "C" void js_domain_restore_stack(Item previous);

// =============================================================================
// Task Queues
// =============================================================================

#define TASK_FLUSH_WORK_BUDGET 8192
#define next_tick_queue (js_runtime_state.event_loop->next_tick_queue)
#define microtask_queue (js_runtime_state.event_loop->microtask_queue)
#define animation_frame_queue (js_runtime_state.event_loop->animation_frame_queue)
#define microtask_running (js_runtime_state.event_loop->microtask_running)
#define next_raf_id (js_runtime_state.event_loop->next_raf_id)
#define auto_close_mode (js_runtime_state.event_loop->auto_close_mode)
#define auto_close_after_load (js_runtime_state.event_loop->auto_close_after_load)
#define auto_close_settle_ms (js_runtime_state.event_loop->auto_close_settle_ms)
#define event_loop_shutting_down (js_runtime_state.event_loop->shutting_down)

extern "C" void js_event_loop_set_auto_close_mode(bool enabled) {
    // Layout config is applied before a document Runtime exists.  There is no
    // semantic event-loop state to mutate until that Runtime binds a capsule.
    if (!js_active_runtime_state) return;
    auto_close_mode = enabled;
}

extern "C" void js_event_loop_set_auto_close_after_load(bool enabled) {
    if (!js_active_runtime_state) return;
    auto_close_after_load = enabled;
}

extern "C" void js_event_loop_set_auto_close_settle_ms(double settle_ms) {
    if (!js_active_runtime_state) return;
    auto_close_settle_ms = settle_ms > 0.0 ? settle_ms : 0.0;
}

JS_FORWARD_EXPRESSION(bool, js_event_loop_auto_close_mode, (void),
    js_active_runtime_state ? auto_close_mode : false)
JS_FORWARD_EXPRESSION(bool, js_event_loop_is_shutting_down, (void),
    js_active_runtime_state ? event_loop_shutting_down : false)

static bool js_async_queue_push(RuntimeJobQueue* queue, Item cb,
                                RuntimeJobKind kind) {
    if (!js_root_vector_ensure_registered(&js_runtime_state.event_loop_queue_roots)) {
        return false;
    }
    JS_ROOTS(roots,
        callback_root, cb,
        resource_root, js_async_hooks_get_current_resource(),
        als_root, js_als_capture_context(),
        domain_root, js_domain_capture_async_stack());
    RuntimeJob job = {};
    job.callback = callback_root.get();
    job.context.resource = resource_root.get();
    job.context.als_context = als_root.get();
    job.context.domain = domain_root.get();
    job.kind = kind;
    return runtime_job_queue_push(queue, &job);
}

static void js_async_queue_enqueue(RuntimeJobQueue* queue, Item callback,
                                   RuntimeJobKind kind, const char* queue_name) {
    if (!js_is_callable(callback)) {
        log_error("event_loop: %s enqueue called with non-function (type=%d)",
            queue_name, get_type_id(callback));
        return;
    }
    if (!js_async_queue_push(queue, callback, kind)) {
        log_error("event_loop: failed to grow %s queue", queue_name);
    }
}
JS_FORWARD_VOID( js_microtask_enqueue, (Item callback), js_async_queue_enqueue,
    (&microtask_queue, callback, RUNTIME_JOB_MICROTASK, "microtask"))
JS_FORWARD_VOID( js_next_tick_enqueue, (Item callback), js_async_queue_enqueue,
    (&next_tick_queue, callback, RUNTIME_JOB_NEXT_TICK, "nextTick"))

// Js57 P2c: visible queue size for the bounded-await drain heuristic.
// Returns the combined pending nextTick + microtask count so js_await_sync can
// detect whether a drain turn made progress (no progress = give up early).
JS_FORWARD_EXPRESSION(int, js_microtask_pending_count, (void),
    (int)(runtime_job_queue_size(&next_tick_queue) +
        runtime_job_queue_size(&microtask_queue)))
JS_FORWARD_EXPRESSION(bool, js_microtask_is_running, (void), (js_active_runtime_state && microtask_running))

struct JsEventLoopCallbackScope {
    bool previous;

    JsEventLoopCallbackScope() : previous(js_runtime_state.event_loop->callback_running) {
        js_runtime_state.event_loop->callback_running = true;
    }
    ~JsEventLoopCallbackScope() {
        js_runtime_state.event_loop->callback_running = previous;
    }
    JsEventLoopCallbackScope(const JsEventLoopCallbackScope&) = delete;
    JsEventLoopCallbackScope& operator=(const JsEventLoopCallbackScope&) = delete;
};

static Item js_run_queued_callback(const RuntimeJob* job) {
    RootFrame roots(7);
    // Queue pop clears the persistent slots before context setup can allocate;
    // keep the dequeued callback graph exact-rooted for the entire invocation.
    Rooted<Item> callback_root(roots, job->callback);
    Rooted<Item> arguments_root(roots, job->arguments);
    Rooted<Item> resource_root(roots, job->context.resource);
    Rooted<Item> als_root(roots, job->context.als_context);
    Rooted<Item> domain_root(roots, job->context.domain);
    Rooted<Item> previous_resource_root(roots, ItemNull);
    Rooted<Item> previous_domain_root(roots, ItemNull);
    if (!js_is_callable(callback_root.get())) return make_js_undefined();

    previous_resource_root.set(js_async_hooks_enter_resource(resource_root.get()));
    previous_domain_root.set(js_domain_set_stack(domain_root.get()));
    JsEventLoopCallbackScope callback_scope;
    Item result = make_js_undefined();
    if (get_type_id(arguments_root.get()) == LMD_TYPE_ARRAY &&
            arguments_root.get().array->length > 0) {
        Array* args = arguments_root.get().array;
        result = js_als_context_call_args(als_root.get(), callback_root.get(),
            ItemNull, args->items, args->length);
    } else {
        result = js_als_context_call(als_root.get(), callback_root.get(),
            ItemNull, ItemNull, 0);
    }
    js_domain_restore_stack(previous_domain_root.get());
    js_async_hooks_restore_resource(previous_resource_root.get());
    return result;
}

extern "C" void js_microtask_flush(void) {
    (void)js_microtask_flush_result();
}

static void js_drain_async_queue(RuntimeJobQueue* queue,
        Rooted<Item>& first_error_root, int& safety, int limit) {
    int drained = 0;
    while (runtime_job_queue_size(queue) > 0 &&
            safety < TASK_FLUSH_WORK_BUDGET && drained < limit) {
        RuntimeJob job = {};
        if (!runtime_job_queue_pop(queue, &job)) break;
        bool previous_running = microtask_running;
        microtask_running = true;
        Item result = js_run_queued_callback(&job);
        microtask_running = previous_running;
        if (item_is_error(result) && !item_is_error(first_error_root.get())) {
            first_error_root.set(result);
        }
        safety++;
        drained++;
    }
}

extern "C" Item js_microtask_flush_result(void) {
    RootFrame roots(1);
    Rooted<Item> first_error_root(roots, ItemNull);
    int safety = 0;
    while ((runtime_job_queue_size(&next_tick_queue) > 0 ||
            runtime_job_queue_size(&microtask_queue) > 0) &&
           safety < TASK_FLUSH_WORK_BUDGET) {
        js_drain_async_queue(&next_tick_queue, first_error_root, safety,
            TASK_FLUSH_WORK_BUDGET);
        js_drain_async_queue(&microtask_queue, first_error_root, safety,
            TASK_FLUSH_WORK_BUDGET);
    }
    if (runtime_job_queue_size(&next_tick_queue) == 0 &&
            runtime_job_queue_size(&microtask_queue) == 0) {
        js_promise_flush_unhandled_checks();
    }
    return first_error_root.get();
}

// Advance exactly one queued job.  Resumable language runtimes use this to
// wait for their own promise without consuming unrelated jobs from the shared
// page turn.
extern "C" Item js_microtask_step(void) {
    RootFrame roots(1);
    Rooted<Item> first_error_root(roots, ItemNull);
    int safety = 0;
    if (runtime_job_queue_size(&next_tick_queue) > 0) {
        js_drain_async_queue(&next_tick_queue, first_error_root, safety, 1);
    } else if (runtime_job_queue_size(&microtask_queue) > 0) {
        js_drain_async_queue(&microtask_queue, first_error_root, safety, 1);
    }
    return first_error_root.get();
}

static bool raf_push(Item cb, int64_t id) {
    RootFrame roots(1);
    Rooted<Item> callback_root(roots, cb);
    if (!js_root_vector_ensure_registered(&js_runtime_state.event_loop_queue_roots)) {
        return false;
    }
    RuntimeJob job = {};
    job.callback = callback_root.get();
    job.id = id;
    job.kind = RUNTIME_JOB_ANIMATION_FRAME;
    return runtime_job_queue_push(&animation_frame_queue, &job);
}

static Item raf_pop(int64_t* out_id) {
    RuntimeJob job = {};
    if (!runtime_job_queue_pop(&animation_frame_queue, &job)) {
        if (out_id) *out_id = -1;
        return ItemNull;
    }
    if (out_id) *out_id = job.id;
    return job.callback;
}

extern "C" Item js_requestAnimationFrame(Item callback) {
    if (!js_is_callable(callback)) {
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "The \"callback\" argument must be of type function.");
    }
    int64_t id = next_raf_id++;
    if (!raf_push(callback, id)) return ItemNull;
    return (Item){.item = i2it(id)};
}

extern "C" void js_cancelAnimationFrame(Item request_id) {
    if (get_type_id(request_id) != LMD_TYPE_INT) return;
    runtime_job_queue_cancel(&animation_frame_queue, it2i(request_id));
}

JS_FORWARD_EXPRESSION(int, js_animation_frame_has_pending, (void),
    // the host loop can outlive a document's JS capsule, so do not read its
    // per-runtime animation-frame queue after script teardown.
    js_active_runtime_state && runtime_job_queue_size(&animation_frame_queue) > 0 ? 1 : 0)

static void js_event_loop_render_checkpoint(void) {
    if (!dom_get_ui_context() || dom_is_host_driven_loop()) {
        js_microtask_flush();
        return;
    }

    // One-shot DOM tasks need a rendering opportunity before dependent library
    // continuations inspect geometry. Repeat because observer microtasks can mutate DOM.
    for (int turn = 0; turn < 8; turn++) {
        bool committed = dom_commit_headless_layout_checkpoint();
        bool had_microtasks = js_microtask_pending_count() > 0;
        if (had_microtasks) js_microtask_flush();
        if (!committed && !had_microtasks) break;
    }
}

extern "C" int js_animation_frame_flush(double timestamp_ms) {
    int pending = (int)runtime_job_queue_size(&animation_frame_queue);
    int called = 0;
    if (pending <= 0) return 0;
    // The frame clock supplies absolute monotonic time; DOMHighResTimeStamp is
    // relative to the same document origin as performance.now().
    RootFrame roots(2);
    Rooted<Item> timestamp_root(roots,
        js_make_number(js_performance_monotonic_to_relative(timestamp_ms)));
    Rooted<Item> callback_root(roots, ItemNull);
    // rAF callbacks and performance.now() share one document frame clock.
    // Headless draining advances synthetic frames faster than wall time, so
    // exposing wall time here prevented animation libraries from completing.
    js_performance_frame_clock_begin(timestamp_ms);

    for (int i = 0; i < pending; i++) {
        int64_t id = -1;
        callback_root.set(raf_pop(&id));
        (void)id;
        if (js_is_callable(callback_root.get())) {
            JsEventLoopCallbackScope callback_scope;
            Item timestamp = timestamp_root.get();
            js_call_function(callback_root.get(), ItemNull, &timestamp, 1);
            called++;
        }
    }
    js_performance_frame_clock_end();
    js_event_loop_render_checkpoint();
    return called;
}

extern "C" int js_animation_frame_drain(int max_frames) {
    // Auto-close cancels timers in js_event_loop_drain(); rAF draining is
    // bounded and needed to settle headless layout/reftest-wait snapshots.
    if (max_frames <= 0) max_frames = 1;
    int frames = 0;
    int called = 0;
    double timestamp_ms = js_performance_monotonic_now_ms();
    while (runtime_job_queue_size(&animation_frame_queue) > 0 && frames < max_frames) {
        timestamp_ms += 16.6667;
        called += js_animation_frame_flush(timestamp_ms);
        js_event_loop_drain();
        frames++;
    }
    if (runtime_job_queue_size(&animation_frame_queue) > 0) {
        log_error("event_loop: animation frame drain stopped with %d callback(s) pending",
            (int)runtime_job_queue_size(&animation_frame_queue));
    }
    return called;
}

// =============================================================================
// Timer Management (libuv-backed)
// =============================================================================

typedef struct JsTimerHandle {
    uv_timer_t timer;
    RuntimeJob job;
    uint32_t   resource_id;
    bool       is_interval;
    Heap*      runtime_heap;
    EvalContext* runtime_context;
    NamePool*  runtime_name_pool;
    Pool*      runtime_pool;
    void*      runtime_doc;
    bool       closing;
    double     virtual_due_ms;
    double     virtual_repeat_ms;
    bool       virtual_active;
    bool       virtual_refed;
} JsTimerHandle;

#define timer_resources (js_runtime_state.resources)
#define timer_handle_count runtime_resource_table_active_count_owned(\
    &timer_resources, js_runtime_state.timers)
#define timer_slot_count runtime_resource_table_slot_count(&timer_resources)
#define next_timer_id (js_runtime_state.timers->next_id)
#define timer_progress_generation (js_runtime_state.timers->progress_generation)
#define timer_force_shutdown (js_runtime_state.timers->force_shutdown)
#define timer_nan_warning_emitted (js_runtime_state.timers->nan_warning_emitted)
#define timer_negative_warning_emitted (js_runtime_state.timers->negative_warning_emitted)
#define virtual_clock_enabled (js_runtime_state.timers->virtual_clock_enabled)
#define virtual_clock_ms (js_runtime_state.timers->virtual_clock_ms)
#define mock_scheduler_enabled (js_runtime_state.timers->mock_scheduler_enabled)
#define mock_scheduler_now_ms (js_runtime_state.timers->mock_scheduler_now_ms)
#define mock_scheduler_waits (js_runtime_state.timers->mock_waits)

static void close_all_timer_handles(void);
static void timer_close_native_handle(JsTimerHandle* th);

static JsTimerHandle* timer_handle_at(int index) {
    const RuntimeResourceEntry* entry = runtime_resource_table_entry_at(
        &timer_resources, index);
    return entry && entry->lifecycle_owner == js_runtime_state.timers
        ? (JsTimerHandle*)entry->close_user : NULL;
}

static void timer_resource_close(void* user) {
    timer_close_native_handle((JsTimerHandle*)user);
}

static Item timer_resource_owner(JsTimerHandle* handle) {
    if (!handle || handle->resource_id == 0) return ItemNull;
    const RuntimeResourceEntry* entry = runtime_resource_table_entry_owned(
        &timer_resources, js_runtime_state.timers, handle->resource_id);
    return runtime_resource_table_value(&timer_resources, entry);
}

static bool timer_registry_append(JsTimerHandle* handle, Item owner) {
    if (!handle) return false;
    JS_ROOTS(roots,
        owner_root,
        owner.item ? owner : handle->job.callback);
    Rooted<Item> callback_root(roots, handle->job.callback);
    Rooted<Item> arguments_root(roots, handle->job.arguments);
    Rooted<Item> resource_root(roots, handle->job.context.resource);
    Rooted<Item> als_root(roots, handle->job.context.als_context);
    Rooted<Item> domain_root(roots, handle->job.context.domain);
    Item root_values[] = {
        owner_root.get(), callback_root.get(), arguments_root.get(),
        resource_root.get(), als_root.get(), domain_root.get(),
    };
    const RuntimeResourceDescriptor* descriptor =
        runtime_resource_descriptor_from_legacy_name("timer");
    handle->resource_id = runtime_resource_table_add_root_span_owned(
        &timer_resources, js_runtime_state.timers, root_values, 6, descriptor,
        timer_resource_close, handle, true);
    return handle->resource_id != 0;
}

static void timer_registry_clear(void) {
    runtime_resource_table_clear_owned(&timer_resources, js_runtime_state.timers);
}

typedef struct JsTimerRuntimeScope {
    void* saved_doc;
    bool doc_active;
} JsTimerRuntimeScope;

static void timer_capture_runtime(JsTimerHandle* th, const char* resource_name, int resource_len) {
    if (!th) return;
    RootFrame roots(5);
    Rooted<Item> callback_root(roots, th->job.callback);
    Rooted<Item> arguments_root(roots, th->job.arguments);
    Rooted<Item> resource_root(roots, th->job.context.resource);
    Rooted<Item> als_root(roots, th->job.context.als_context);
    Rooted<Item> domain_root(roots, th->job.context.domain);

    // The native timer record is not a GC object. Publish its callback and
    // captured values before building more async state, then transfer them to
    // registered persistent slots before this temporary frame is released.
    resource_root.set(js_async_hooks_create_resource(resource_name, resource_len));
    als_root.set(js_als_capture_context());
    domain_root.set(js_domain_capture_async_stack());
    th->job.callback = callback_root.get();
    th->job.arguments = arguments_root.get();
    th->job.context.resource = resource_root.get();
    th->job.context.als_context = als_root.get();
    th->job.context.domain = domain_root.get();
    if (context) {
        th->runtime_heap = context->heap;
        th->runtime_context = context;
        th->runtime_name_pool = context->name_pool;
        th->runtime_pool = context->pool;
    }
    th->runtime_doc = dom_get_document();
}

static bool timer_runtime_enter(JsTimerHandle* th, JsTimerRuntimeScope* scope) {
    if (!th || !scope) return false;
    memset(scope, 0, sizeof(JsTimerRuntimeScope));
    scope->saved_doc = dom_get_document();
    if (!th->runtime_context || !th->runtime_heap || !th->runtime_name_pool ||
            !eval_context_matches(th->runtime_context) ||
            !js_runtime_state_thread_matches(th->runtime_context)) {
        // Timer ownership is a routing check. A loop callback cannot borrow a
        // different evaluator and restore the previous one afterward.
        log_error("js-timer-runtime: callback arrived on non-owner thread");
        return false;
    }
    if (th->runtime_doc) {
        dom_set_document(th->runtime_doc);
        scope->doc_active = true;
    }
    return true;
}

static void timer_runtime_exit(JsTimerRuntimeScope* scope) {
    if (!scope) return;
    if (scope->doc_active) {
        dom_set_document(scope->saved_doc);
        scope->doc_active = false;
    }
}

static void timer_close_cb(uv_handle_t *handle) {
    JsTimerHandle *th = (JsTimerHandle *)handle->data;
    mem_free(th);
}

static void timer_close_native_handle(JsTimerHandle *th) {
    if (!th || th->closing) return;
    th->closing = true;
    th->virtual_active = false;
    if (!timer_force_shutdown) {
        JsTimerRuntimeScope scope;
        if (timer_runtime_enter(th, &scope)) {
            js_async_hooks_emit_destroy_resource(th->job.context.resource);
            timer_runtime_exit(&scope);
        }
    }
    uv_timer_stop(&th->timer);
    uv_close((uv_handle_t *)&th->timer, timer_close_cb);
}

static void timer_close_handle(JsTimerHandle *th) {
    if (!th || th->closing) return;
    if (th->resource_id != 0) {
        uint32_t resource_id = th->resource_id;
        th->resource_id = 0;
        runtime_resource_table_remove_owned(&timer_resources,
            js_runtime_state.timers, resource_id);
        return;
    }
    timer_close_native_handle(th);
}

static void timer_mark_object_destroyed(JsTimerHandle* th) {
    Item owner = timer_resource_owner(th);
    if (!owner.item) return;
    // The JS Timeout object is separate from the libuv handle; update it on
    // observable clear/fire paths, not during process-exit cleanup of unref'd intervals.
    js_set_key_cstr(owner, "_destroyed", (Item){.item = b2it(true)});
}

static void timer_forget_unsafe_handle(JsTimerHandle* th) {
    if (!th) return;
    th->runtime_doc = nullptr;
    th->runtime_heap = nullptr;
    th->runtime_name_pool = nullptr;
    th->runtime_pool = nullptr;
    th->job.callback = ItemNull;
    th->job.arguments = ItemNull;
    th->job.context.resource = ItemNull;
    th->job.context.als_context = ItemNull;
    th->job.context.domain = ItemNull;
    th->job.kind = RUNTIME_JOB_NONE;
    th->closing = true;
    mem_free(th);
}

static void timer_abandon_all_without_uv(const char* reason_prefix) {
    for (int index = timer_slot_count - 1; index >= 0; index--) {
        JsTimerHandle* th = timer_handle_at(index);
        if (!th) continue;
        log_debug("%s freeing timer %lld without libuv close",
                  reason_prefix ? reason_prefix : "[JS_TIMER_ABANDON_UNSAFE]",
                  (long long)th->job.id);
        uint32_t resource_id = th->resource_id;
        th->resource_id = 0;
        runtime_resource_table_forget_owned(&timer_resources,
            js_runtime_state.timers, resource_id);
        timer_forget_unsafe_handle(th);
    }
    timer_registry_clear();
    // Signal watchdog recovery can corrupt libuv queue links; abandon the loop
    // with the timer records so later global cleanup does not walk stale handles.
    lambda_uv_abandon();
}

static void timer_fire_cb(uv_timer_t *handle) {
    JsTimerHandle *th = (JsTimerHandle *)handle->data;
    timer_progress_generation++;
    bool close_after_fire = th && !th->is_interval;
    // D5.3/D5.4.3: the timer callback may collect before the saved async
    // context is restored, so callback results and prior context snapshots
    // must remain exact roots across the callback boundary.
    JS_ROOTS(roots,
        callback_result_root, ItemNull,
        previous_resource_root, ItemNull,
        previous_domain_root, ItemNull,
        arguments_root, th ? th->job.arguments : ItemNull);
    JsTimerRuntimeScope scope;
    if (timer_runtime_enter(th, &scope)) {
        JsEventLoopCallbackScope callback_scope;
        previous_resource_root.set(js_async_hooks_enter_resource(th->job.context.resource));
        previous_domain_root.set(js_domain_set_stack(th->job.context.domain));
        if (js_is_callable(th->job.callback)) {
            if (get_type_id(arguments_root.get()) == LMD_TYPE_ARRAY &&
                    arguments_root.get().array->length > 0) {
                Array* args = arguments_root.get().array;
                callback_result_root.set(js_als_context_call_args(
                    th->job.context.als_context, th->job.callback, ItemNull,
                    args->items, args->length));
            } else {
                callback_result_root.set(js_als_context_call(th->job.context.als_context,
                    th->job.callback, ItemNull, ItemNull, 0));
            }
        }
        js_domain_restore_stack(previous_domain_root.get());
        js_async_hooks_restore_resource(previous_resource_root.get());
        timer_runtime_exit(&scope);
    } else {
        log_error("event_loop: timer fired without captured JS runtime");
    }
    if (th && th->is_interval && item_is_error(callback_result_root.get()) && !th->closing) {
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
    js_event_loop_render_checkpoint();
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
    js_set_key_cstr(warning, "name", js_name_item(name, (int)strlen(name)));
    js_set_key_cstr(warning, "message", js_name_item(message, len));
    js_process_emit(
        js_name_item("warning", 7),
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

static Item js_timeout_ref_or_unref(Item this_val, bool do_ref) {
    Item self = timeout_this_or_arg(this_val);
    JsTimerHandle* th = find_timer_handle(self);
    if (th && virtual_clock_enabled) {
        th->virtual_refed = do_ref;
    } else if (th && !uv_is_closing((uv_handle_t*)&th->timer)) {
        if (do_ref) uv_ref((uv_handle_t*)&th->timer);
        else uv_unref((uv_handle_t*)&th->timer);
    }
    return self;
}

// Timeout.ref()
JS_FORWARD_ITEM(js_timeout_ref, (Item this_val), js_timeout_ref_or_unref, (this_val, true))

// Timeout.unref()
JS_FORWARD_ITEM(js_timeout_unref, (Item this_val), js_timeout_ref_or_unref, (this_val, false))

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
JS_FORWARD_ITEM(js_timeout_refresh, (Item this_val), timeout_this_or_arg, (this_val))

// Timeout[Symbol.toPrimitive]() — returns the timer id
extern "C" Item js_timeout_toPrimitive(Item this_val) {
    Item self = timeout_this_or_arg(this_val);
    // Native method calls pass the coercion hint in the first ABI slot; recover
    // the receiver so numeric timer-handle coercion cannot yield undefined.
    Item id = js_get_key_cstr(self, "_timerId");
    return id;
}

static Item make_timer_object(int64_t id, JsClass cls) {
    Item obj = js_new_object_with_class(cls);
    js_set_key_cstr(obj, "_timerId", (Item){.item = i2it(id)});
    js_set_key_cstr(obj, "_destroyed", (Item){.item = b2it(false)});

    // bind methods
    // Native timeout methods use one Item ABI argument (the JS argument or
    // undefined). Declaring zero made the invoke trampoline call a P0 function
    // even though these handlers read Item this_val, corrupting `@@toPrimitive`.
    Item ref_fn = js_new_native_function(js_timeout_ref);
    Item unref_fn = js_new_native_function(js_timeout_unref);
    Item hasRef_fn = js_new_native_function(js_timeout_hasRef);
    Item refresh_fn = js_new_native_function(js_timeout_refresh);

    js_set_key_cstr(obj, "ref", ref_fn);
    js_set_key_cstr(obj, "unref", unref_fn);
    js_set_key_cstr(obj, "hasRef", hasRef_fn);
    js_set_key_cstr(obj, "refresh", refresh_fn);

    // Symbol.toPrimitive uses its realm-local identity key.
    // @@toPrimitive receives the coercion hint. Declaring that ABI argument
    // prevents the generic call trampoline from invoking this one-argument C
    // function as P0 while it recovers the Timeout receiver from `this`.
    Item toPrim_fn = js_new_native_function(js_timeout_toPrimitive);
    js_set_key_default(obj, js_well_known_symbol_key(2), toPrim_fn);

    return obj;
}

// Extract timer id from either a plain integer or a Timeout object
static int64_t extract_timer_id(Item timer_id) {
    TypeId tid = get_type_id(timer_id);
    if (tid == LMD_TYPE_INT) {
        return it2i(timer_id);
    } else if (tid == LMD_TYPE_MAP || tid == LMD_TYPE_VMAP) {
        // Timeout objects may be class-stamped object shapes; clearInterval
        // must still recover _timerId or the active interval survives throws.
        Item id = js_get_key_cstr(timer_id, "_timerId");
        if (get_type_id(id) == LMD_TYPE_INT) return it2i(id);
    }
    return -1;
}

static JsTimerHandle* find_timer_handle(Item timer_id) {
    int64_t id = extract_timer_id(timer_id);
    if (id < 0) return NULL;
    for (int i = 0; i < timer_slot_count; i++) {
        JsTimerHandle* th = timer_handle_at(i);
        if (th && th->job.id == id && !th->closing) return th;
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
    // Host setup can run before a document Runtime exists.  There is no event
    // loop owner in that phase, so never publish clock semantics globally.
    if (!js_active_runtime_state) return;
    virtual_clock_enabled = enabled;
    virtual_clock_ms = monotonic_ms >= 0.0 ? monotonic_ms : 0.0;
    js_performance_virtual_clock_set(enabled, virtual_clock_ms);
}

JS_FORWARD_EXPRESSION(bool, js_event_loop_virtual_clock_enabled, (void),
    js_active_runtime_state ? virtual_clock_enabled : false)
JS_FORWARD_EXPRESSION(double, js_event_loop_virtual_clock_now_ms, (void),
    js_active_runtime_state ? virtual_clock_ms : 0.0)

static JsTimerHandle* virtual_timer_next_due(double target_ms) {
    JsTimerHandle* next = nullptr;
    for (int index = 0; index < timer_slot_count; index++) {
        JsTimerHandle* timer = timer_handle_at(index);
        if (!timer || timer->closing || !timer->virtual_active ||
            timer->virtual_due_ms > target_ms) continue;
        if (!next || timer->virtual_due_ms < next->virtual_due_ms ||
            (timer->virtual_due_ms == next->virtual_due_ms &&
                timer->job.id < next->job.id)) {
            next = timer;
        }
    }
    return next;
}

static int virtual_timer_fire_due(double target_ms) {
    // The work budget limits a runaway virtual clock, not timer capacity.
    const int callback_limit = (timer_handle_count > 0 ? timer_handle_count : 1) * 64;
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
        if (dom_tick_headless_animation_frame()) progress++;
    }
    js_microtask_flush();
    // rAF and microtasks may queue zero-delay timers at this same timestamp.
    progress += virtual_timer_fire_due(target_ms);
    return progress;
}

extern "C" int js_event_loop_advance_virtual_time(double delta_ms, int frame_steps) {
    if (!js_active_runtime_state) return 0;
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

static uv_loop_t* js_timer_get_loop(const char* name) {
    uv_loop_t *loop = lambda_uv_loop();
    if (!loop) {
        log_error("event_loop: uv loop not initialized for %s", name);
    }
    return loop;
}

static void js_timer_capture_argument_pack(JsTimerHandle* th, Item args_array,
        bool has_args) {
    if (!th) return;
    th->job.arguments = has_args && get_type_id(args_array) == LMD_TYPE_ARRAY
        ? args_array : ItemNull;
}

static Item js_timer_finish_create(uv_loop_t* loop, JsTimerHandle* th,
        JsClass timer_class, uint64_t delay, uint64_t repeat,
        const char* capture_name, int capture_name_len) {
    timer_capture_runtime(th, capture_name, capture_name_len);
    // The native timer is not GC-managed. Keep its whole job envelope exact
    // while constructing the public handle, then publish that same envelope
    // through the resource row below.
    JS_ROOTS(roots,
        callback_root, th->job.callback,
        arguments_root, th->job.arguments,
        resource_root, th->job.context.resource,
        als_root, th->job.context.als_context,
        domain_root, th->job.context.domain,
        timer_root, ItemNull);
    uv_timer_init(loop, &th->timer);
    timer_start(loop, th, delay, repeat);
    timer_root.set(make_timer_object(th->job.id, timer_class));
    th->job.callback = callback_root.get();
    th->job.arguments = arguments_root.get();
    th->job.context.resource = resource_root.get();
    th->job.context.als_context = als_root.get();
    th->job.context.domain = domain_root.get();
    if (!timer_registry_append(th, timer_root.get())) {
        timer_close_handle(th);
        return ItemNull;
    }
    return timer_root.get();
}

static Item js_timer_run_string_handler(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return ItemError;
    // Timer strings execute as a classic script in the captured document realm.
    return js_builtin_eval(env[0], 1);
}

static Item js_timer_normalize_handler(Item handler) {
    if (js_is_callable(handler)) return handler;
    if (!dom_get_document()) {
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "The \"callback\" argument must be of type function.");
    }

    // HTML timers accept TimerHandler strings; retain compiled-source input in
    // a closure so it is evaluated only when the timer task runs.
    RootFrame roots(2);
    Rooted<Item> handler_root(roots, handler);
    Rooted<Item> source_root(roots, js_to_string(handler_root.get()));
    if (item_is_error(source_root.get())) return source_root.get();

    Item* env = js_alloc_env(1);
    if (!env) return ItemError;
    env[0] = source_root.get();
    return js_new_native_closure(js_timer_run_string_handler, 0, env, 1);
}

static Item js_schedule_timer(Item callback, Item delay, Item args_array,
                              bool has_args, bool is_interval) {
    RootFrame roots(1);
    Rooted<Item> callback_root(roots, js_timer_normalize_handler(callback));
    if (item_is_error(callback_root.get())) return callback_root.get();
    uv_loop_t *loop = js_timer_get_loop(
        is_interval ? "setInterval" : "setTimeout");
    if (!loop) return ItemNull;

    uint64_t ms = normalize_timer_delay(delay);
    if (is_interval && ms < 1) ms = 1;

    JsTimerHandle *th = (JsTimerHandle *)mem_calloc(1, sizeof(JsTimerHandle), MEM_CAT_JS_RUNTIME);
    if (!th) return ItemNull;

    th->job.id = next_timer_id++;
    th->job.callback = callback_root.get();
    th->job.kind = RUNTIME_JOB_TIMER;
    th->is_interval = is_interval;
    th->timer.data = th;
    js_timer_capture_argument_pack(th, args_array, has_args);
    return js_timer_finish_create(loop, th, JS_CLASS_TIMEOUT, ms,
        is_interval ? ms : 0, "Timeout", 7);
}

#define JS_TIMER_FORWARD(name, args, has_args, interval) \
extern "C" Item name(Item callback, Item delay) { \
    return js_schedule_timer(callback, delay, args, has_args, interval); \
}
#define JS_TIMER_FORWARD_ARGS(name, interval) \
extern "C" Item name(Item callback, Item delay, Item args_array) { \
    return js_schedule_timer(callback, delay, args_array, true, interval); \
}
JS_TIMER_FORWARD(js_setTimeout, ItemNull, false, false)
JS_TIMER_FORWARD_ARGS(js_setTimeout_args, false)

static Item js_setImmediate_impl(Item callback, Item args_array, bool has_args) {
    if (!js_is_callable(callback)) {
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "The \"callback\" argument must be of type function.");
    }
    uv_loop_t *loop = js_timer_get_loop("setImmediate");
    if (!loop) return ItemNull;

    JsTimerHandle *th = (JsTimerHandle *)mem_calloc(1, sizeof(JsTimerHandle), MEM_CAT_JS_RUNTIME);
    if (!th) return ItemNull;

    th->job.id = next_timer_id++;
    th->job.callback = callback;
    th->job.kind = RUNTIME_JOB_TIMER;
    th->is_interval = false;
    th->timer.data = th;
    js_timer_capture_argument_pack(th, args_array, has_args);
    // immediates queued while draining the current check phase belong to the next turn.
    return js_timer_finish_create(loop, th, JS_CLASS_IMMEDIATE, 1, 0,
        "Immediate", 9);
}
JS_FORWARD_ITEM(js_setImmediate_timer, (Item callback), js_setImmediate_impl, (callback, ItemNull, false))
JS_FORWARD_ITEM(js_setImmediate_timer_args, (Item callback, Item args_array), js_setImmediate_impl, (callback, args_array, true))

// Helper: create a JS array from the fixed argument packs emitted for timers.
extern "C" Item js_pack_args_span(Item* values, int count) {
    if (count < 0 || (count > 0 && !values)) return ItemNull;
    RootSpan value_roots(count > 0 ? (size_t)count : 0);
    if (count > 0 && !value_roots.valid()) return ItemNull;
    for (int i = 0; i < count; i++) {
        value_roots.words()[i] = values[i].item;
    }
    Array* arr = (Array*)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
    if (!arr) return ItemNull;
    arr->type_id = LMD_TYPE_ARRAY;
    arr->items = nullptr;
    arr->length = 0;
    arr->capacity = 0;
    for (int i = 0; i < count; i++) {
        array_push(arr, (Item){.item = value_roots.words()[i]});
    }
    return (Item){.array = arr};
}
JS_TIMER_FORWARD(js_setInterval, ItemNull, false, true)
JS_TIMER_FORWARD_ARGS(js_setInterval_args, true)
#undef JS_TIMER_FORWARD_ARGS
#undef JS_TIMER_FORWARD

// =============================================================================
// Promise-based timers (for timers/promises module)
// =============================================================================

// helper: create an AbortError for promise rejection
static Item make_abort_error(Item signal) {
    Item err = js_new_object_with_class(JS_CLASS_ABORT_ERROR);
    js_set_key_cstr(err, "name", js_name_item("AbortError", 10));
    js_set_key_cstr(err, "code", js_name_item("ABORT_ERR", 9));
    js_set_key_cstr(err, "message", js_name_item("The operation was aborted", 25));
    // propagate cause from signal.reason if available
    if (get_type_id(signal) == LMD_TYPE_MAP) {
    Item reason = js_get_key_cstr(signal, "reason");
        if (get_type_id(reason) != LMD_TYPE_UNDEFINED && get_type_id(reason) != LMD_TYPE_NULL) {
            js_set_key_cstr(err, "cause", reason);
        }
    }
    return err;
}

// helper: check if signal is aborted, validate options types
// returns 0=ok, 1=already aborted (reject_out set), -1=type error thrown
static Item check_timer_options(Item options, Item* reject_out, int* result_code) {
    if (result_code) *result_code = 0;
    auto reject_reason = [reject_out](Item reason) -> int {
        if (item_is_error(reason)) reason = js_error_lane_payload(reason);
        *reject_out = js_promise_reject(reason);
        return -1;
    };

    if (get_type_id(options) == LMD_TYPE_UNDEFINED || get_type_id(options) == LMD_TYPE_NULL) {
        return js_status_ok(); // no options
    }
    TypeId opt_type = get_type_id(options);
    if (opt_type != LMD_TYPE_MAP) {
        // options must be an object if provided (non-nullish)
        if (result_code) *result_code = reject_reason(js_throw_type_error_code(
            "ERR_INVALID_ARG_TYPE", "The \"options\" argument must be of type object."));
        return js_status_ok();
    }
    // validate signal if present
    Item signal = js_get_key_cstr(options, "signal");
    if (item_is_error(signal)) {
        if (result_code) *result_code = reject_reason(signal);
        return js_status_ok();
    }
    if (get_type_id(signal) != LMD_TYPE_UNDEFINED && get_type_id(signal) != LMD_TYPE_NULL) {
        // signal must be an AbortSignal (object with 'aborted' property)
        TypeId sig_type = get_type_id(signal);
        if (sig_type != LMD_TYPE_MAP) {
            if (result_code) *result_code = reject_reason(js_throw_type_error_code(
                "ERR_INVALID_ARG_TYPE", "The \"options.signal\" property must be an instance of AbortSignal."));
            return js_status_ok();
        }
        // check if already aborted
        Item aborted = js_get_key_cstr(signal, "aborted");
        if (item_is_error(aborted)) {
            if (result_code) *result_code = reject_reason(aborted);
            return js_status_ok();
        }
        if (get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted)) {
            *reject_out = js_promise_reject(make_abort_error(signal));
            if (result_code) *result_code = 1;
            return js_status_ok();
        }
    }
    // validate ref if present
    Item ref = js_get_key_cstr(options, "ref");
    if (item_is_error(ref)) {
        if (result_code) *result_code = reject_reason(ref);
        return js_status_ok();
    }
    if (get_type_id(ref) != LMD_TYPE_UNDEFINED && get_type_id(ref) != LMD_TYPE_NULL) {
        if (get_type_id(ref) != LMD_TYPE_BOOL) {
            if (result_code) *result_code = reject_reason(js_throw_type_error_code(
                "ERR_INVALID_ARG_TYPE", "The \"options.ref\" property must be of type boolean."));
            return js_status_ok();
        }
    }
    return js_status_ok();
}

enum JsMockSchedulerWaitValue {
    JS_MOCK_WAIT_PROMISE = 0,
    JS_MOCK_WAIT_RESOLVE,
    JS_MOCK_WAIT_REJECT,
    JS_MOCK_WAIT_SIGNAL,
    JS_MOCK_WAIT_VALUE_COUNT,
};

static Item mock_scheduler_wait_value(const JsMockSchedulerWait* wait,
        int value_index) {
    Item* value = wait ? root_vector_at((RootVector*)&wait->values, value_index) : NULL;
    return value ? *value : ItemNull;
}

static void mock_scheduler_wait_destroy(JsMockSchedulerWait* wait) {
    if (!wait) return;
    root_vector_destroy(&wait->values);
    mem_free(wait);
}

static void mock_scheduler_clear_state(JsEventLoopTimerState* state) {
    if (!state || !state->mock_waits) return;
    for (int i = state->mock_waits->length - 1; i >= 0; i--) {
        mock_scheduler_wait_destroy((JsMockSchedulerWait*)
            state->mock_waits->data[i]);
    }
    arraylist_free(state->mock_waits);
    state->mock_waits = NULL;
}

extern "C" void js_event_loop_timer_state_destroy(JsEventLoopTimerState* state) {
    mock_scheduler_clear_state(state);
}

static void mock_scheduler_clear(void) {
    mock_scheduler_clear_state(js_active_runtime_state
        ? js_runtime_state.timers : NULL);
}

static void mock_scheduler_wait_remove(int index) {
    if (!mock_scheduler_waits || index < 0 || index >= mock_scheduler_waits->length)
        return;
    mock_scheduler_wait_destroy((JsMockSchedulerWait*)
        mock_scheduler_waits->data[index]);
    arraylist_remove(mock_scheduler_waits, index);
}

static bool mock_scheduler_wait_append(Item promise, Item resolve, Item reject,
        Item signal, int64_t due_ms) {
    JsMockSchedulerWait* wait = (JsMockSchedulerWait*)mem_calloc(1,
        sizeof(JsMockSchedulerWait), MEM_CAT_JS_RUNTIME);
    if (!wait) return false;
    root_vector_init(&wait->values, (Context*)context, "JS mock scheduler wait");
    if (!root_vector_push(&wait->values, promise) ||
            !root_vector_push(&wait->values, resolve) ||
            !root_vector_push(&wait->values, reject) ||
            !root_vector_push(&wait->values, signal)) {
        mock_scheduler_wait_destroy(wait);
        return false;
    }
    if (!mock_scheduler_waits) {
        mock_scheduler_waits = arraylist_new(8);
        if (!mock_scheduler_waits) {
            mock_scheduler_wait_destroy(wait);
            return false;
        }
    }
    if (!arraylist_append(mock_scheduler_waits, wait)) {
        mock_scheduler_wait_destroy(wait);
        return false;
    }
    wait->due_ms = due_ms;
    return true;
}

static void js_mock_scheduler_set_enabled(bool enabled) {
    mock_scheduler_clear();
    mock_scheduler_enabled = enabled;
    mock_scheduler_now_ms = 0;
}
JS_FORWARD_VOID( js_mock_scheduler_enable, (void), js_mock_scheduler_set_enabled, (true))
JS_FORWARD_VOID( js_mock_scheduler_reset, (void), js_mock_scheduler_set_enabled, (false))

extern "C" void js_mock_scheduler_tick(Item delay) {
    if (!mock_scheduler_enabled) return;
    mock_scheduler_now_ms += (int64_t)normalize_timer_delay(delay);
    Item undef = (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};

    for (int i = mock_scheduler_waits ? mock_scheduler_waits->length - 1 : -1;
            i >= 0; i--) {
        JsMockSchedulerWait* wait = (JsMockSchedulerWait*)mock_scheduler_waits->data[i];
        if (!wait || wait->due_ms > mock_scheduler_now_ms) continue;
        Item signal = mock_scheduler_wait_value(wait, JS_MOCK_WAIT_SIGNAL);
        if (get_type_id(signal) == LMD_TYPE_MAP) {
            Item aborted = js_get_key_cstr(signal, "aborted");
            if (get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted)) {
                Item err = make_abort_error(signal);
                Item args[1] = { err };
                js_call_function(mock_scheduler_wait_value(wait, JS_MOCK_WAIT_REJECT),
                    ItemNull, args, 1);
                mock_scheduler_wait_remove(i);
                continue;
            }
        }
        Item args[1] = { undef };
        js_call_function(mock_scheduler_wait_value(wait, JS_MOCK_WAIT_RESOLVE),
            ItemNull, args, 1);
        mock_scheduler_wait_remove(i);
    }
    js_microtask_flush();
}

static Item js_mock_scheduler_wait(Item delay, Item options) {
    Item reject_out = ItemNull;
    int opt_rc = 0;
    JS_ASSIGN_OR_RETURN(options_status, check_timer_options(options, &reject_out, &opt_rc));
    if (opt_rc != 0) return reject_out;

    Item resolvers = js_promise_with_resolvers();
    Item promise = js_get_key_cstr(resolvers, "promise");
    Item resolve_fn = js_get_key_cstr(resolvers, "resolve");
    Item reject_fn = js_get_key_cstr(resolvers, "reject");
    Item signal = get_type_id(options) == LMD_TYPE_MAP
        ? js_get_key_cstr(options, "signal")
        : (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
    JS_ROOTS(roots,
        promise_root, promise,
        resolve_root, resolve_fn,
        reject_root, reject_fn,
        signal_root, signal);

    if (!mock_scheduler_wait_append(promise_root.get(), resolve_root.get(),
            reject_root.get(), signal_root.get(), mock_scheduler_now_ms +
            (int64_t)normalize_timer_delay(delay))) {
        return js_promise_reject(js_new_error(js_name_item(
            "Mock scheduler wait allocation failed", 35)));
    }
    // Mock scheduler waits must not allocate real uv timers; otherwise
    // official fake-timer tests sleep for the virtual delay before passing.
    return promise_root.get();
}

static Item js_set_promise_timer(Item delay, Item value, Item options,
        const char* timer_name, int timer_name_len, uint64_t start_delay) {

    // check options for signal before creating timer
    Item reject_out = ItemNull;
    int opt_rc = 0;
    JS_ASSIGN_OR_RETURN(options_status, check_timer_options(options, &reject_out, &opt_rc));
    if (opt_rc != 0) return reject_out;

    Item resolvers = js_promise_with_resolvers();
    Item k_promise = js_name_item("promise", 7);
    Item k_resolve = js_name_item("resolve", 7);
    Item k_reject = js_name_item("reject", 6);
    Item promise = js_get_key_default(resolvers, k_promise);
    Item resolve_fn = js_get_key_default(resolvers, k_resolve);
    Item reject_fn = js_get_key_default(resolvers, k_reject);
    JS_ROOTS(roots,
        promise_root, promise,
        resolve_root, resolve_fn,
        reject_root, reject_fn,
        value_root, value,
        options_root, options);

    // The resolve callback and delayed value use the same timer job envelope
    // as public setTimeout callbacks.
    uv_loop_t *loop = lambda_uv_loop();
    if (!loop) return promise_root.get();

    uint64_t ms = start_delay ? start_delay : normalize_timer_delay(delay);

    JsTimerHandle *th = (JsTimerHandle *)mem_calloc(1, sizeof(JsTimerHandle), MEM_CAT_JS_RUNTIME);
    if (!th) return promise_root.get();

    th->job.id = next_timer_id++;
    th->job.callback = resolve_root.get();
    Item delayed_value = value_root.get();
    th->job.arguments = js_pack_args_span(&delayed_value, 1);
    th->job.kind = RUNTIME_JOB_TIMER;
    th->is_interval = false;
    th->timer.data = th;
    timer_capture_runtime(th, timer_name, timer_name_len);

    uv_timer_init(loop, &th->timer);
    timer_start(loop, th, ms, 0);

    if (!timer_registry_append(th, promise_root.get())) {
        timer_close_handle(th);
        return promise_root.get();
    }

    // if signal present, add abort listener to reject promise and clear timer
    if (get_type_id(options_root.get()) == LMD_TYPE_MAP) {
        Item signal = js_get_key_cstr(options_root.get(), "signal");
        if (get_type_id(signal) == LMD_TYPE_MAP) {
            // create an abort handler closure that captures timer id and reject_fn
            // we store timer_id and reject_fn in a wrapper object on the signal
            Item timer_id_item = (Item){.item = i2it(th->job.id)};
            // add 'abort' event listener — when aborted, reject the promise
        Item listeners = js_get_key_cstr(signal, "__listeners__");
            if (get_type_id(listeners) == LMD_TYPE_ARRAY) {
                // store reject_fn and timer_id in the abort entry for manual dispatch
                Item entry = js_new_object();
                js_set_key_cstr(entry, "type", js_name_item("abort", 5));
                js_set_key_cstr(entry, "__timer_reject__", reject_root.get());
                js_set_key_cstr(entry, "__timer_id__", timer_id_item);
                js_set_key_cstr(entry, "__timer_signal__", signal);
                // the abort dispatcher handles the stored rejection path
                js_set_key_cstr(entry, "handler", reject_root.get());
                js_array_push(listeners, entry);
            }
        }
    }

    return promise_root.get();
}

// setTimeout(delay, value, options) → Promise that resolves to value after delay ms
JS_FORWARD_ITEM(js_setTimeout_promise, (Item delay, Item value, Item options), js_set_promise_timer, (delay, value, options, "Timeout", 7, 0))

extern "C" Item js_setTimeout_promisified(Item delay, Item value) {
    Item undef = (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
    return js_setTimeout_promise(delay, value, undef);
}

extern "C" void js_timer_install_promisify_custom(Item fn_item) {
    if (get_type_id(fn_item) != LMD_TYPE_FUNC) return;
    Item custom_fn = js_new_native_function(js_setTimeout_promisified);
    js_set_key_default(fn_item, js_util_promisify_custom_symbol(), custom_fn);
}

extern "C" Item js_setImmediate_promise(Item value, Item options) {
    // Promise immediates use a one-millisecond next-turn timer.
    Item undef = (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
    return js_set_promise_timer(undef, value, options, "Immediate", 9, 1);
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
    for (int i = 0; i < timer_slot_count; i++) {
        JsTimerHandle* th = timer_handle_at(i);
        if (th && th->job.id == id) {
            timer_mark_object_destroyed(th);
            timer_close_handle(th);
            return;
        }
    }
}
JS_FORWARD_VOID( js_clearInterval, (Item timer_id), js_clearTimeout, (timer_id))

extern "C" void js_event_loop_cancel_document_timers(void* dom_doc) {
    // A timer queue is owned by its bound document Runtime.  Callers that no
    // longer have that owner cannot safely inspect a different capsule.
    if (!js_active_runtime_state || !dom_doc) return;

    for (int i = 0; i < timer_slot_count; i++) {
        JsTimerHandle *th = timer_handle_at(i);
        if (!th || th->runtime_doc != dom_doc) continue;

        log_debug("[JS_TIMER_DETACH] canceling timer %lld for document %p",
                  (long long)th->job.id, dom_doc);
        th->runtime_doc = nullptr;
        th->job.callback = ItemNull;
        th->job.arguments = ItemNull;
        th->job.context.resource = ItemNull;
        th->job.context.als_context = ItemNull;
        th->job.context.domain = ItemNull;
        timer_close_handle(th);
    }
}

extern "C" void js_event_loop_abandon_document_timers(void* dom_doc) {
    if (!js_active_runtime_state || !dom_doc) return;

    bool found = false;
    for (int i = 0; i < timer_slot_count; i++) {
        JsTimerHandle *th = timer_handle_at(i);
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
JS_FORWARD_VOID( js_event_loop_abandon_all_timers, (void), timer_abandon_all_without_uv, ("[JS_TIMER_ABANDON_ALL]"))

// =============================================================================
// Event Loop Lifecycle
// =============================================================================

extern "C" void js_event_loop_attach_lambda_scheduler(void) {
    lambda_concurrency_js_init();
    Runtime* runtime = context ? context->runtime : NULL;
    if (context && !context->scheduler && runtime && runtime->js_runtime_used) {
        // Pure-JS contexts must retain their existing loop footprint. Attach a
        // Lambda scheduler only after a cross-language module activates the
        // membrane, so exported procedures can progress on this libuv loop.
        context->scheduler = runtime_scheduler(runtime)
            ? runtime_scheduler(runtime)
            : lambda_scheduler_create(LAMBDA_MAILBOX_DEFAULT_CAPACITY);
        if (!runtime_scheduler(runtime)) {
            runtime_set_scheduler(runtime, context->scheduler);
        }
    }
}

extern "C" void js_event_loop_init(void) {
    js_event_loop_attach_lambda_scheduler();
    if (timer_handle_count > 0) {
        // Timers belong to the active document realm. Nested script entries in
        // a static capture must retain them until the document load boundary.
        event_loop_shutting_down = false;
        return;
    }
    event_loop_shutting_down = false;

    // Reset the policy-specific queues without retaining job-owned Items.
    runtime_job_queue_clear(&next_tick_queue);
    runtime_job_queue_clear(&microtask_queue);
    runtime_job_queue_clear(&animation_frame_queue);
    (void)js_root_vector_ensure_registered(&js_runtime_state.event_loop_queue_roots);
    next_raf_id = 1;
    // No timer is live here; release the retired registry allocation rather
    // than retaining a historical fixed-capacity table between documents.
    timer_registry_clear();
    next_timer_id = 1;
    timer_nan_warning_emitted = false;
    timer_negative_warning_emitted = false;

    mock_scheduler_clear();

    // initialize libuv loop
    lambda_uv_init();

    // register task drain to run at libuv phase checkpoints
    lambda_uv_set_microtask_drain(js_microtask_flush);
}

extern "C" void js_event_loop_shutdown(void) {
    // Hosts may finish a plain document after its transient JS Runtime has
    // already been released.  With no active capsule there are no queues or
    // timer roots owned by this call.
    if (!js_active_runtime_state) return;
    uv_loop_t* loop = lambda_uv_loop();

    event_loop_shutting_down = true;
    timer_force_shutdown = true;
    close_all_timer_handles();
    if (loop) {
        int safety = timer_handle_count + 16;
        while (timer_handle_count > 0 && safety-- > 0) {
            uv_run(loop, UV_RUN_NOWAIT);
        }
    }
    timer_force_shutdown = false;

    if (timer_handle_count > 0) {
        log_error("event_loop: shutdown left %d timer handle(s) pending close",
                  timer_handle_count);
    } else {
        timer_registry_clear();
    }

    runtime_job_queue_clear(&next_tick_queue);
    runtime_job_queue_clear(&microtask_queue);
    runtime_job_queue_clear(&animation_frame_queue);
}

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
    for (int i = 0; i < timer_slot_count; i++) {
        JsTimerHandle* th = timer_handle_at(i);
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
    for (int i = timer_slot_count - 1; i >= 0; i--) {
        JsTimerHandle* th = timer_handle_at(i);
        if (th && th->is_interval) {
            timer_close_handle(th);
        }
    }
}

// Auto-close mode matches closing the page after load/onload: immediate tasks
// queued by onload get a browser-like macrotask turn, but pending timers must
// not keep the static layout pass alive.
static void close_all_timer_handles(void) {
    for (int i = timer_slot_count - 1; i >= 0; i--) {
        JsTimerHandle* th = timer_handle_at(i);
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
    if (js_animation_frame_has_pending()) {
        js_animation_frame_flush(js_performance_monotonic_now_ms());
    }
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
        int before = js_microtask_pending_count();
        uv_run(loop, UV_RUN_NOWAIT);
        int after_uv = js_microtask_pending_count();
        int frame_callbacks = js_animation_frame_has_pending()
            ? js_animation_frame_flush(js_performance_monotonic_now_ms()) : 0;
        js_microtask_flush();
        if (predicate(user)) return 0;

        bool made_progress = frame_callbacks > 0 || (after_uv > before) || (after_uv != 0);
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
    // Static documents can be hosted without a JS Runtime.  Their native input
    // loop may still request a pump, but no context-local queue exists to read.
    if (!js_active_runtime_state) return;
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
    if (!js_active_runtime_state) return false;
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
    // D5.4.1: the drain may resume parent timers after a rendering checkpoint,
    // so it is not a quiescent handoff boundary for nested document realms.
    RuntimeExecutionScope execution_scope(context);
    // Commit script mutations before its first queued task. This is the
    // one-shot equivalent of the host render opportunity between event tasks.
    js_event_loop_render_checkpoint();

    // Host-driven sessions (Radiant `view`) own the timer/rAF cadence and pump
    // the loop via js_event_loop_pump_nowait() AFTER committing the first layout.
    // A drain here — reached from the document loader before that commit — would
    // fire load-time setTimeout(0) callbacks too early, against an uncommitted
    // document, so geometry queries read zero boxes. Microtasks (promise jobs)
    // are already flushed above; leave timers queued for the host's pump.
    if (dom_is_host_driven_loop()) return 0;

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) return 0;

    if (auto_close_mode && !auto_close_after_load) {
        // Parsing may re-enter the script runner; only service ready work until
        // the final load boundary decides which pending timers to retain.
        for (int turn = 0; turn < 4; turn++) {
            int active = uv_run(loop, UV_RUN_NOWAIT);
            js_event_loop_render_checkpoint();
            js_microtask_flush();
            if (!active) break;
        }
        return 0;
    }

    if (js_process_exit_requested()) {
        // process.exit() must bypass ordinary libuv liveness; Node does not wait
        // for servers or sockets once user code requested hard termination.
        close_all_timer_handles();
        uv_run(loop, UV_RUN_NOWAIT);
        return 0;
    }

    if (auto_close_mode) {
        if (virtual_clock_enabled && auto_close_settle_ms > 0.0) {
            // Resource-completion tasks run at the load boundary. Settle their
            // zero-delay work first, so the requested window starts after load.
            js_event_loop_advance_virtual_time(0.0, 0);
            js_event_loop_advance_virtual_time(auto_close_settle_ms, 0);
            // Browser snapshots observe the next rendering opportunity after
            // their wait completes, including timer callbacks due this frame.
            js_event_loop_advance_virtual_time(1000.0 / 60.0, 1);
        }
        for (int turn = 0; turn < 4; turn++) {
            int active = uv_run(loop, UV_RUN_NOWAIT);
            js_event_loop_render_checkpoint();
            // Headless layout has no native frame clock; drain queued rAF work
            // so scripts using two-frame rendering checkpoints can commit their
            // final DOM mutations before the one-shot document is serialized.
            int frame_callbacks = js_animation_frame_has_pending()
                ? js_animation_frame_flush(js_performance_monotonic_now_ms()) : 0;
            js_microtask_flush();
            if (!active && timer_handle_count == 0 && frame_callbacks == 0 &&
                !js_animation_frame_has_pending()) break;
        }
        close_all_timer_handles();
        uv_run(loop, UV_RUN_NOWAIT);
        js_event_loop_render_checkpoint();
        return 0;
    }

    int result = 0;
    // A parallel event-loop SIGSEGV guard used to replace the runtime's stack
    // handler and continue after arbitrary memory corruption. C14 faults use
    // the active execution frame; all other memory faults must fail-stop.
    {
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
        js_event_loop_render_checkpoint();

        int animation_frames = 0;
        while (!watchdog_state.fired && dom_tick_headless_animation_frame() &&
               animation_frames < 256) {
            js_event_loop_render_checkpoint();
            uv_run(loop, UV_RUN_NOWAIT);
            animation_frames++;
        }
        if (animation_frames >= 256 && dom_tick_headless_animation_frame()) {
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
    }

    return result;
}

extern "C" void js_event_loop_drain_script_turn(bool has_dom_document,
                                                  bool drain_timers) {
    if (dom_is_host_driven_loop()) {
        js_microtask_flush();
        return;
    }

    if (drain_timers) {
        if (has_dom_document) dom_commit_headless_layout();
        js_event_loop_drain();
    }
    if (has_dom_document) {
        // headless documents have no native frame clock; flush queued rAF work
        // before the transient script realm is torn down.
        js_animation_frame_drain(64);
    }
}
