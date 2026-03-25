/**
 * JavaScript Event Loop for Lambda v15
 *
 * libuv-backed event loop with:
 * - Microtask queue (FIFO) for Promise callbacks
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

// =============================================================================
// Microtask Queue
// =============================================================================

void js_microtask_enqueue(Item callback);
void js_microtask_flush(void);

// =============================================================================
// Timers
// =============================================================================

Item js_setTimeout(Item callback, Item delay);
Item js_setInterval(Item callback, Item delay);
void js_clearTimeout(Item timer_id);
void js_clearInterval(Item timer_id);

#ifdef __cplusplus
}
#endif
