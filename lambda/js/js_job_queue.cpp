#include "js_job_queue.h"
#include "js_event_loop.h"
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../../lib/log.h"

extern "C" void js_enqueue_promise_job(Item job) {
    if (!js_is_callable(job)) {
        log_error("js-job-queue: promise job enqueue received non-function (type=%d)", get_type_id(job));
        return;
    }
    js_microtask_enqueue(job);
}

extern "C" void js_run_microtasks(void) {
    js_microtask_flush();
}
