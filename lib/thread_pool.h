// lib/thread_pool.h - Generic pthread + condvar thread pool.
//
// Provides a single API for the recurring "submit N independent jobs, then
// wait for all to finish" pattern. The two existing pools in this tree
// (network/network_thread_pool, radiant/tile_pool) and the ad-hoc
// pthread_create loops can be reframed as thin wrappers on top of this.
//
// Job priority is a hint: higher priorities run before lower ones when the
// scheduler picks the next job, but starvation is avoided by FIFO within each
// priority bucket.
//
// Threads are created on tp_create() and idle on a condvar until work arrives.

#ifndef LIB_THREAD_POOL_H
#define LIB_THREAD_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ThreadPool ThreadPool;

typedef void (*TpJobFn)(void* arg);

typedef enum {
    TP_PRIORITY_LOW    = 0,
    TP_PRIORITY_NORMAL = 1,
    TP_PRIORITY_HIGH   = 2,
} TpPriority;

// Create a pool with `threads` workers. If threads <= 0, auto-detects via
// hardware concurrency. Returns NULL on failure.
ThreadPool* tp_create(int threads);

// Same as tp_create but lets callers pin a per-thread stack size. Useful for
// recursive workloads that overflow the default stack. stack_size <= 0 uses
// the platform default.
ThreadPool* tp_create_with_stack(int threads, size_t stack_size);

// Submit a job. Returns false if the pool has been shut down or on OOM.
bool tp_submit(ThreadPool* tp, TpJobFn fn, void* arg);
bool tp_submit_priority(ThreadPool* tp, TpJobFn fn, void* arg, TpPriority priority);

// Wait for all queued jobs to drain. After this returns, the pool can still
// accept new submissions.
void tp_wait_all(ThreadPool* tp);

// Shut down: stop accepting new jobs, drain the queue, join worker threads.
// Safe to call multiple times.
void tp_shutdown(ThreadPool* tp);

// Shutdown + free.
void tp_destroy(ThreadPool* tp);

// Introspection.
int tp_thread_count(const ThreadPool* tp);
size_t tp_pending(const ThreadPool* tp);

#ifdef __cplusplus
}
#endif

#endif
