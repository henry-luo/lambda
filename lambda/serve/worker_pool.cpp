/**
 * @file worker_pool.cpp
 * @brief Worker pool implementation using uv_queue_work
 *
 * Dispatches handler execution to libuv's thread pool so the event loop
 * stays responsive for I/O. Worker threads execute the full middleware
 * chain + route handler synchronously.
 */

#include "worker_pool.hpp"
#include "serve_utils.hpp"
#include "../../lib/log.h"

#include <string.h>
#include <stdlib.h>

// ============================================================================
// Worker pool lifecycle
// ============================================================================

WorkerPool* worker_pool_create(uv_loop_t *loop, int pool_size) {
    if (!loop) return NULL;

    WorkerPool *pool = (WorkerPool*)serve_calloc(1, sizeof(WorkerPool));
    if (!pool) return NULL;

    pool->loop = loop;
    pool->pool_size = pool_size > 0 ? pool_size : 4;
    pool->max_queue_length = 0;     // unlimited by default
    pool->request_timeout_ms = 0;   // no timeout by default

    // set libuv thread pool size via environment variable
    // must be set before the first uv_queue_work() call
    char size_str[16];
    snprintf(size_str, sizeof(size_str), "%d", pool->pool_size);
    setenv("UV_THREADPOOL_SIZE", size_str, 0); // don't override if already set

    log_info("worker pool created with %d threads", pool->pool_size);
    return pool;
}

void worker_pool_destroy(WorkerPool *pool) {
    if (!pool) return;
    log_info("worker pool shutdown: %lld requests, %lld errors",
             (long long)pool->total_requests, (long long)pool->total_errors);
    serve_free(pool);
}

// ============================================================================
// Configuration
// ============================================================================

void worker_pool_set_max_queue(WorkerPool *pool, int max_queue_length) {
    if (pool) pool->max_queue_length = max_queue_length;
}

void worker_pool_set_timeout(WorkerPool *pool, int timeout_ms) {
    if (pool) pool->request_timeout_ms = timeout_ms;
}

void worker_pool_set_backends(WorkerPool *pool, BackendRegistry *backends) {
    if (pool) pool->backends = backends;
}

// ============================================================================
// Worker thread callback (runs on thread pool)
// ============================================================================

static void worker_execute(uv_work_t *work) {
    WorkerTask *task = (WorkerTask*)work;

    // run middleware chain first (if any), then the handler
    if (task->middleware && task->middleware->count > 0) {
        // the handler itself should be the last middleware or called
        // after the middleware chain completes
        middleware_stack_run(task->middleware, task->req, task->resp, task->handler_data);
    }

    // if response wasn't sent by middleware, run the route handler
    if (task->handler && !task->resp->headers_sent) {
        task->handler(task->req, task->resp, task->handler_data);
    }

    task->result = BACKEND_OK;
}

// ============================================================================
// Completion callback (runs on event loop thread)
// ============================================================================

static void worker_after(uv_work_t *work, int status) {
    WorkerTask *task = (WorkerTask*)work;
    WorkerPool *pool = (WorkerPool*)task->user_data;

    if (pool) {
        pool->active_tasks--;
        if (status != 0 || task->result != BACKEND_OK) {
            pool->total_errors++;
        }
    }

    // map uv status to backend result
    int result = task->result;
    if (status == UV_ECANCELED) {
        result = BACKEND_TIMEOUT;
        log_error("worker task cancelled for %s %s",
                  http_method_to_string(task->req->method), task->req->path);
    } else if (status != 0) {
        result = BACKEND_ERROR;
        log_error("worker task failed for %s %s: %s",
                  http_method_to_string(task->req->method), task->req->path,
                  uv_strerror(status));
    }

    // invoke completion callback
    if (task->on_complete) {
        task->on_complete(task->req, task->resp, result, task->user_data);
    }

    serve_free(task);
}

// ============================================================================
// Dispatch
// ============================================================================

int worker_pool_dispatch(WorkerPool *pool,
                         HttpRequest *req, HttpResponse *resp,
                         RequestHandler handler, void *handler_data,
                         MiddlewareStack *middleware,
                         WorkerCompleteFn on_complete, void *user_data) {
    if (!pool || !req || !resp) return -1;

    // check queue limit
    if (pool->max_queue_length > 0 && pool->queued_tasks >= pool->max_queue_length) {
        log_error("worker pool queue full (%d tasks), rejecting request",
                  pool->queued_tasks);
        return -1;
    }

    WorkerTask *task = (WorkerTask*)serve_calloc(1, sizeof(WorkerTask));
    if (!task) return -1;

    task->req = req;
    task->resp = resp;
    task->handler = handler;
    task->handler_data = handler_data;
    task->middleware = middleware;
    task->on_complete = on_complete;
    task->user_data = user_data;
    task->result = BACKEND_ERROR; // default until worker completes

    pool->total_requests++;
    pool->active_tasks++;
    pool->queued_tasks++;

    int r = uv_queue_work(pool->loop, &task->work, worker_execute, worker_after);
    if (r != 0) {
        log_error("uv_queue_work failed: %s", uv_strerror(r));
        pool->active_tasks--;
        pool->queued_tasks--;
        pool->total_errors++;
        serve_free(task);
        return -1;
    }

    pool->queued_tasks--;
    return 0;
}

// ============================================================================
// Stats
// ============================================================================

int worker_pool_active_count(const WorkerPool *pool) {
    return pool ? pool->active_tasks : 0;
}

int worker_pool_queued_count(const WorkerPool *pool) {
    return pool ? pool->queued_tasks : 0;
}
