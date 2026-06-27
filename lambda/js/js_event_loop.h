/**
 * JavaScript Event Loop for Lambda v15
 *
 * libuv-backed event loop with:
 * - nextTick queue for Node-compatible process.nextTick ordering
 * - Microtask queue (FIFO) for Promise callbacks
 * - Animation frame queue for requestAnimationFrame
 * - Timers via uv_timer_t for setTimeout/setInterval
 * - Main drain via uv_run() for post-script execution
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

// =============================================================================
// Event Loop Lifecycle
// =============================================================================

void js_event_loop_init(void);
int  js_event_loop_drain(void);
void js_event_loop_shutdown(void);
void js_event_loop_set_auto_close_mode(bool enabled);
bool js_event_loop_auto_close_mode(void);
bool js_event_loop_is_shutting_down(void);

// =============================================================================
// Microtask Queue
// =============================================================================

void js_microtask_enqueue(Item callback);
void js_next_tick_enqueue(Item callback);
void js_microtask_flush(void);
int  js_microtask_pending_count(void);
// Js57 P2c: bounded loop drain — runs uv_run(UV_RUN_NOWAIT) + microtask flush in
// tight turns until predicate(user) is non-zero or one of three bounds expires
// (watchdog_ms, max_no_progress turns, max_turns). See impl for details.
int  js_await_bounded_drain(int (*predicate)(void*), void* user,
                            int watchdog_ms, int max_no_progress, int max_turns);

// =============================================================================
// Animation Frame Queue
// =============================================================================

Item js_requestAnimationFrame(Item callback);
void js_cancelAnimationFrame(Item request_id);
int  js_animation_frame_has_pending(void);
int  js_animation_frame_flush(double timestamp_ms);
int  js_animation_frame_drain(int max_frames);

// =============================================================================
// Timers
// =============================================================================

Item js_setTimeout(Item callback, Item delay);
Item js_setTimeout_args(Item callback, Item delay, Item args_array);
Item js_setInterval(Item callback, Item delay);
Item js_setInterval_args(Item callback, Item delay, Item args_array);
void js_clearTimeout(Item timer_id);
void js_clearInterval(Item timer_id);
void js_event_loop_cancel_document_timers(void* dom_doc);
void js_event_loop_abandon_document_timers(void* dom_doc);

// Helper: pack 1-4 items into a JS array (used by transpiler for timer extra args)
Item js_pack_args_1(Item a1);
Item js_pack_args_2(Item a1, Item a2);
Item js_pack_args_3(Item a1, Item a2, Item a3);
Item js_pack_args_4(Item a1, Item a2, Item a3, Item a4);

#ifdef __cplusplus
}
#endif
