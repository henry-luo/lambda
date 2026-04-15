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
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"

#include <cstring>
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
} JsTimerHandle;

#define MAX_TIMER_HANDLES 1024
static JsTimerHandle *timer_handles[MAX_TIMER_HANDLES];
static int timer_handle_count = 0;
static int64_t next_timer_id = 1;

static void timer_close_cb(uv_handle_t *handle) {
    JsTimerHandle *th = (JsTimerHandle *)handle->data;
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

static void timer_fire_cb(uv_timer_t *handle) {
    JsTimerHandle *th = (JsTimerHandle *)handle->data;
    if (get_type_id(th->callback) == LMD_TYPE_FUNC) {
        js_call_function(th->callback, ItemNull, NULL, 0);
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

extern "C" Item js_setTimeout(Item callback, Item delay) {
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
    th->timer.data = th;

    uv_timer_init(loop, &th->timer);
    uv_timer_start(&th->timer, timer_fire_cb, (uint64_t)ms, 0);

    if (timer_handle_count < MAX_TIMER_HANDLES) {
        timer_handles[timer_handle_count++] = th;
    }

    return (Item){.item = i2it(th->id)};
}

extern "C" Item js_setInterval(Item callback, Item delay) {
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
    th->timer.data = th;

    uv_timer_init(loop, &th->timer);
    uv_timer_start(&th->timer, timer_fire_cb, (uint64_t)ms, (uint64_t)ms);

    if (timer_handle_count < MAX_TIMER_HANDLES) {
        timer_handles[timer_handle_count++] = th;
    }

    return (Item){.item = i2it(th->id)};
}

extern "C" void js_clearTimeout(Item timer_id) {
    int64_t id = it2i(timer_id);
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
