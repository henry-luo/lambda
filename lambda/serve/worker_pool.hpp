/**
 * @file worker_pool.hpp
 * @brief PHP-FPM-style worker pool using libuv thread pool
 *
 * Dispatches HTTP request handlers to worker threads via uv_queue_work(),
 * keeping the event loop non-blocking. All handler execution (including
 * Lambda JIT, Python WSGI, Bash CGI) runs on worker threads.
 *
 * Compatible with:
 *   PHP-FPM:   pre-forked worker processes → here: libuv thread pool
 *   Gunicorn:  worker processes with sync handlers
 *   Express:   single-threaded (we improve on this with workers)
 *
 * Flow:
 *   1. Event loop accepts connection + parses HTTP request
 *   2. worker_pool_dispatch() queues work via uv_queue_work()
 *   3. Worker thread runs: route match → middleware → handler
 *   4. After callback (on event loop): sends response via libuv
 */

#pragma once

#include <uv.h>
#include "serve_types.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "language_backend.hpp"
#include "middleware.hpp"
#include "router.hpp"

// ============================================================================
// Worker Task (passed to uv_queue_work)
// ============================================================================

// completion callback: called on the event loop thread after worker finishes
typedef void (*WorkerCompleteFn)(HttpRequest *req, HttpResponse *resp,
                                 int status, void *user_data);

struct WorkerTask {
    uv_work_t           work;           // libuv work request (must be first)
    HttpRequest        *req;
    HttpResponse       *resp;
    RequestHandler      handler;        // route handler to execute
    void               *handler_data;   // user_data for the handler
    MiddlewareStack    *middleware;      // middleware chain to run
    WorkerCompleteFn    on_complete;    // completion callback
    void               *user_data;      // user context for completion
    int                 result;         // execution result (BackendResult)
};

// ============================================================================
// Worker Pool
// ============================================================================

struct WorkerPool {
    uv_loop_t          *loop;
    int                 pool_size;          // number of threads (maps to UV_THREADPOOL_SIZE)
    int                 max_queue_length;   // max pending tasks (0 = unlimited)
    int                 request_timeout_ms; // per-request timeout (0 = no timeout)
    BackendRegistry    *backends;           // language backend registry

    // stats
    int                 active_tasks;
    int                 queued_tasks;
    int64_t             total_requests;
    int64_t             total_errors;
};

// create/destroy worker pool
WorkerPool* worker_pool_create(uv_loop_t *loop, int pool_size);
void        worker_pool_destroy(WorkerPool *pool);

// configure
void worker_pool_set_max_queue(WorkerPool *pool, int max_queue_length);
void worker_pool_set_timeout(WorkerPool *pool, int timeout_ms);
void worker_pool_set_backends(WorkerPool *pool, BackendRegistry *backends);

// dispatch a request to the worker pool
// handler + middleware run on the worker thread
// on_complete runs on the event loop thread afterwards
int worker_pool_dispatch(WorkerPool *pool,
                         HttpRequest *req, HttpResponse *resp,
                         RequestHandler handler, void *handler_data,
                         MiddlewareStack *middleware,
                         WorkerCompleteFn on_complete, void *user_data);

// get pool statistics
int  worker_pool_active_count(const WorkerPool *pool);
int  worker_pool_queued_count(const WorkerPool *pool);
