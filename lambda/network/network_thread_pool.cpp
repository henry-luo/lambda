// network_thread_pool.cpp
// Thread pool implementation backed by libuv's uv_queue_work.
// Replaces custom pthreads + priority queue with libuv's built-in threadpool.

#include "network_thread_pool.h"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"
#include "../../lib/mem.h"
#ifdef _WIN32
#include <windows.h>
static inline int setenv(const char* name, const char* value, int overwrite) {
    if (!overwrite && getenv(name)) return 0;
    return _putenv_s(name, value) == 0 ? 0 : -1;
}
#else
#include <unistd.h>
#endif
#include <time.h>
#include <uv.h>

// Default number of threads
#define DEFAULT_THREAD_COUNT 4

// Per-task work request — wraps uv_work_t with task metadata
typedef struct {
    uv_work_t           req;
    NetworkThreadPool*   pool;
    TaskFunction         task_fn;
    void*                resource;
    ResourcePriority     priority;
} WorkRequest;

// Worker callback — runs on libuv thread pool thread
static void work_cb(uv_work_t* req) {
    WorkRequest* wr = (WorkRequest*)req->data;
    if (!wr || !wr->pool) return;

    atomic_fetch_sub(&wr->pool->queued_count, 1);
    atomic_fetch_add(&wr->pool->active_count, 1);

    if (wr->task_fn) {
        wr->task_fn(wr->resource);
    }

    atomic_fetch_sub(&wr->pool->active_count, 1);
}

// After-work callback — runs on main thread after work_cb completes
static void after_work_cb(uv_work_t* req, int status) {
    WorkRequest* wr = (WorkRequest*)req->data;
    if (!wr) return;

    atomic_fetch_sub(&wr->pool->pending_count, 1);
    mem_free(wr);
}

// Create thread pool
NetworkThreadPool* thread_pool_create(int num_threads) {
    if (num_threads <= 0) {
        num_threads = DEFAULT_THREAD_COUNT;
    }

    // Set UV_THREADPOOL_SIZE if larger than default (4)
    if (num_threads > 4) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", num_threads);
        // only effective before the first uv_queue_work call
        setenv("UV_THREADPOOL_SIZE", buf, 0);
    }

    NetworkThreadPool* pool = (NetworkThreadPool*)mem_calloc(1, sizeof(NetworkThreadPool), MEM_CAT_NETWORK);
    if (!pool) return NULL;

    pool->num_threads = num_threads;
    pool->shutdown_flag = false;
    atomic_store(&pool->active_count, 0);
    atomic_store(&pool->queued_count, 0);
    atomic_store(&pool->pending_count, 0);

    // ensure libuv loop is initialized
    lambda_uv_init();

    log_debug("network: created thread pool with %d workers (libuv-backed)", num_threads);
    return pool;
}

// Destroy thread pool
void thread_pool_destroy(NetworkThreadPool* pool) {
    if (!pool) return;

    log_debug("network: destroying thread pool");

    // wait for outstanding work to complete
    thread_pool_wait_all(pool);

    mem_free(pool);
    log_debug("network: thread pool destroyed");
}

// Enqueue task
bool thread_pool_enqueue(NetworkThreadPool* pool,
                        TaskFunction task_fn,
                        void* task_data,
                        ResourcePriority priority) {
    if (!pool || !task_fn) return false;

    if (pool->shutdown_flag) return false;

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        log_error("network: uv loop not initialized for thread_pool_enqueue");
        return false;
    }

    WorkRequest* wr = (WorkRequest*)mem_calloc(1, sizeof(WorkRequest), MEM_CAT_NETWORK);
    if (!wr) return false;

    wr->pool = pool;
    wr->task_fn = task_fn;
    wr->resource = task_data;
    wr->priority = priority;
    wr->req.data = wr;

    atomic_fetch_add(&pool->queued_count, 1);
    atomic_fetch_add(&pool->pending_count, 1);

    int r = uv_queue_work(loop, &wr->req, work_cb, after_work_cb);
    if (r != 0) {
        log_error("network: uv_queue_work failed: %s", uv_strerror(r));
        atomic_fetch_sub(&pool->queued_count, 1);
        atomic_fetch_sub(&pool->pending_count, 1);
        mem_free(wr);
        return false;
    }

    log_debug("network: enqueued task with priority %d (%d pending)",
              priority, atomic_load(&pool->pending_count));
    return true;
}

// Wait for all tasks to complete
void thread_pool_wait_all(NetworkThreadPool* pool) {
    if (!pool) return;

    log_debug("network: waiting for all tasks to complete");

    // run the event loop until all pending tasks are done
    uv_loop_t* loop = lambda_uv_loop();
    if (loop) {
        while (atomic_load(&pool->pending_count) > 0) {
            uv_run(loop, UV_RUN_ONCE);
        }
    }

    log_debug("network: all tasks completed");
}

// Shutdown thread pool (stop accepting new tasks)
void thread_pool_shutdown(NetworkThreadPool* pool) {
    if (!pool) return;

    pool->shutdown_flag = true;
    log_debug("network: thread pool shutdown initiated");
}

// Get active worker count
int thread_pool_get_active_count(const NetworkThreadPool* pool) {
    if (!pool) return 0;
    return atomic_load(&pool->active_count);
}

// Get queued task count
int thread_pool_get_queued_count(const NetworkThreadPool* pool) {
    if (!pool) return 0;
    return atomic_load(&pool->queued_count);
}
