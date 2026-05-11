/**
 * JavaScript job queue helpers.
 *
 * PromiseJobs are routed through the host microtask queue so Promises, async
 * functions, queueMicrotask(), and the event loop share one FIFO drain model.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

void js_enqueue_promise_job(Item job);
void js_run_microtasks(void);

#ifdef __cplusplus
}
#endif
