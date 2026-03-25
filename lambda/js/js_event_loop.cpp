/**
 * JavaScript Event Loop for Lambda v14
 *
 * Lightweight event loop implementation:
 * - Microtask queue: FIFO ring buffer, flushed after every macro‑task
 * - Timer heap: simple min-heap of (deadline, callback) entries
 * - Drain loop: runs until both queues are empty
 */
#include "js_event_loop.h"
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../../lib/log.h"

#include <cstring>
#include <cstdlib>
#include <sys/time.h>

// =============================================================================
// Microtask Queue (FIFO ring buffer)
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
    // Drain all microtasks (including ones enqueued during execution)
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
// Timer Heap
// =============================================================================

typedef struct {
    int64_t  id;
    int64_t  deadline_us;   // absolute deadline in microseconds
    int64_t  interval_us;   // 0 for setTimeout, >0 for setInterval
    Item     callback;
    bool     cancelled;
} JsTimer;

#define MAX_TIMERS 256

static JsTimer timer_heap[MAX_TIMERS];
static int     timer_count = 0;
static int64_t next_timer_id = 1;

static int64_t now_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000LL + (int64_t)tv.tv_usec;
}

// Min-heap helpers (1-indexed conceptually, 0-indexed in array)
static void timer_swap(int a, int b) {
    JsTimer tmp = timer_heap[a];
    timer_heap[a] = timer_heap[b];
    timer_heap[b] = tmp;
}

static void timer_sift_up(int i) {
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (timer_heap[i].deadline_us < timer_heap[parent].deadline_us) {
            timer_swap(i, parent);
            i = parent;
        } else break;
    }
}

static void timer_sift_down(int i) {
    while (true) {
        int smallest = i;
        int left = 2 * i + 1;
        int right = 2 * i + 2;
        if (left < timer_count && timer_heap[left].deadline_us < timer_heap[smallest].deadline_us)
            smallest = left;
        if (right < timer_count && timer_heap[right].deadline_us < timer_heap[smallest].deadline_us)
            smallest = right;
        if (smallest != i) {
            timer_swap(i, smallest);
            i = smallest;
        } else break;
    }
}

static void timer_insert(JsTimer t) {
    if (timer_count >= MAX_TIMERS) {
        log_error("event_loop: timer heap full (%d)", MAX_TIMERS);
        return;
    }
    timer_heap[timer_count] = t;
    timer_sift_up(timer_count);
    timer_count++;
}

static void timer_remove_top() {
    if (timer_count <= 0) return;
    timer_count--;
    timer_heap[0] = timer_heap[timer_count];
    if (timer_count > 0) timer_sift_down(0);
}

extern "C" Item js_setTimeout(Item callback, Item delay) {
    double ms = 0;
    if (get_type_id(delay) == LMD_TYPE_FLOAT) {
        ms = *((double*)delay.item);
    } else if (get_type_id(delay) == LMD_TYPE_INT) {
        ms = (double)it2i(delay);
    }
    if (ms < 0) ms = 0;

    JsTimer t;
    t.id = next_timer_id++;
    t.deadline_us = now_us() + (int64_t)(ms * 1000.0);
    t.interval_us = 0;
    t.callback = callback;
    t.cancelled = false;
    timer_insert(t);

    return (Item){.item = i2it(t.id)};
}

extern "C" Item js_setInterval(Item callback, Item delay) {
    double ms = 0;
    if (get_type_id(delay) == LMD_TYPE_FLOAT) {
        ms = *((double*)delay.item);
    } else if (get_type_id(delay) == LMD_TYPE_INT) {
        ms = (double)it2i(delay);
    }
    if (ms < 1) ms = 1; // minimum interval

    int64_t interval = (int64_t)(ms * 1000.0);

    JsTimer t;
    t.id = next_timer_id++;
    t.deadline_us = now_us() + interval;
    t.interval_us = interval;
    t.callback = callback;
    t.cancelled = false;
    timer_insert(t);

    return (Item){.item = i2it(t.id)};
}

extern "C" void js_clearTimeout(Item timer_id) {
    int64_t id = it2i(timer_id);
    for (int i = 0; i < timer_count; i++) {
        if (timer_heap[i].id == id) {
            timer_heap[i].cancelled = true;
            return;
        }
    }
}

extern "C" void js_clearInterval(Item timer_id) {
    js_clearTimeout(timer_id);
}

// =============================================================================
// Main Drain Loop
// =============================================================================

extern "C" void js_event_loop_init(void) {
    microtask_head = 0;
    microtask_tail = 0;
    microtask_count = 0;
    timer_count = 0;
    next_timer_id = 1;
}

extern "C" int js_event_loop_drain(void) {
    // Flush any microtasks first (from synchronous Promise resolutions)
    js_microtask_flush();

    int iterations = 0;
    int max_iterations = 100000; // safety limit

    while (iterations < max_iterations) {
        // Skip cancelled timers at the top
        while (timer_count > 0 && timer_heap[0].cancelled) {
            timer_remove_top();
        }

        if (timer_count == 0 && microtask_count == 0) {
            break; // nothing left to do
        }

        if (timer_count > 0) {
            int64_t current = now_us();
            if (timer_heap[0].deadline_us <= current) {
                // Fire the top timer
                JsTimer fired = timer_heap[0];
                timer_remove_top();

                if (!fired.cancelled && get_type_id(fired.callback) == LMD_TYPE_FUNC) {
                    js_call_function(fired.callback, ItemNull, NULL, 0);

                    // If interval, re-insert with next deadline
                    if (fired.interval_us > 0 && !fired.cancelled) {
                        fired.deadline_us = now_us() + fired.interval_us;
                        timer_insert(fired);
                    }
                }

                // Flush microtasks after each timer callback (per spec)
                js_microtask_flush();
            } else {
                // Sleep until next deadline (with 1ms granularity)
                int64_t wait_us = timer_heap[0].deadline_us - current;
                if (wait_us > 1000) wait_us = 1000; // cap at 1ms to stay responsive
                struct timeval tv;
                tv.tv_sec = 0;
                tv.tv_usec = (int)wait_us;
                select(0, NULL, NULL, NULL, &tv);
            }
        }

        iterations++;
    }

    if (iterations >= max_iterations) {
        log_error("event_loop: drain exceeded max iterations (%d)", max_iterations);
    }

    return 0;
}
