/**
 * JavaScript Event Loop for Lambda v14
 *
 * Minimal custom event loop with:
 * - Microtask queue (FIFO) for Promise callbacks
 * - Timer heap for setTimeout/setInterval
 * - Main drain loop for post-script execution
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
