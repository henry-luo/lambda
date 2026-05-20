// lib/thread_pool.c - see thread_pool.h
//
// Implementation: per-priority FIFO queues backed by a singly-linked list,
// shared mutex+condvar pair, plus a "all-done" condvar for tp_wait_all().
// active_jobs is the count of jobs currently being executed by workers; we
// keep it separate from queue depth so tp_wait_all is precise.

#include "thread_pool.h"
#include "log.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__) || defined(__linux__)
#include <unistd.h>
#endif

#define TP_PRIO_COUNT 3

typedef struct TpJob {
    TpJobFn fn;
    void* arg;
    struct TpJob* next;
} TpJob;

typedef struct {
    TpJob* head;
    TpJob* tail;
} TpQueue;

struct ThreadPool {
    pthread_t* threads;
    int thread_count;
    TpQueue queues[TP_PRIO_COUNT];
    size_t pending;       // queued but not yet picked up
    size_t active;        // picked up but not yet finished
    bool shutdown;
    bool joined;
    pthread_mutex_t mutex;
    pthread_cond_t work_available;
    pthread_cond_t all_done;
};

static int tp_detect_threads(void) {
#if defined(_SC_NPROCESSORS_ONLN)
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n > 0) return (int)n;
#endif
    return 4;
}

// caller must hold the mutex.
static TpJob* tp_dequeue_locked(ThreadPool* tp) {
    for (int p = TP_PRIO_COUNT - 1; p >= 0; --p) {
        TpQueue* q = &tp->queues[p];
        if (q->head) {
            TpJob* j = q->head;
            q->head = j->next;
            if (!q->head) q->tail = NULL;
            tp->pending--;
            return j;
        }
    }
    return NULL;
}

static void* tp_worker_main(void* arg) {
    ThreadPool* tp = (ThreadPool*)arg;
    for (;;) {
        pthread_mutex_lock(&tp->mutex);
        while (!tp->shutdown && tp->pending == 0) {
            pthread_cond_wait(&tp->work_available, &tp->mutex);
        }
        TpJob* job = tp_dequeue_locked(tp);
        if (!job) {
            // shutdown && queue empty
            pthread_mutex_unlock(&tp->mutex);
            break;
        }
        tp->active++;
        pthread_mutex_unlock(&tp->mutex);

        job->fn(job->arg);
        free(job);

        pthread_mutex_lock(&tp->mutex);
        tp->active--;
        if (tp->pending == 0 && tp->active == 0) {
            pthread_cond_broadcast(&tp->all_done);
        }
        pthread_mutex_unlock(&tp->mutex);
    }
    return NULL;
}

ThreadPool* tp_create_with_stack(int threads, size_t stack_size) {
    if (threads <= 0) threads = tp_detect_threads();

    ThreadPool* tp = (ThreadPool*)calloc(1, sizeof(ThreadPool));
    if (!tp) return NULL;
    tp->thread_count = threads;
    tp->threads = (pthread_t*)calloc((size_t)threads, sizeof(pthread_t));
    if (!tp->threads) { free(tp); return NULL; }

    if (pthread_mutex_init(&tp->mutex, NULL) != 0) goto fail_mutex;
    if (pthread_cond_init(&tp->work_available, NULL) != 0) goto fail_cv1;
    if (pthread_cond_init(&tp->all_done, NULL) != 0) goto fail_cv2;

    pthread_attr_t attr;
    pthread_attr_t* attr_ptr = NULL;
    if (stack_size > 0) {
        if (pthread_attr_init(&attr) != 0) goto fail_threads;
        pthread_attr_setstacksize(&attr, stack_size);
        attr_ptr = &attr;
    }

    for (int i = 0; i < threads; ++i) {
        if (pthread_create(&tp->threads[i], attr_ptr, tp_worker_main, tp) != 0) {
            pthread_mutex_lock(&tp->mutex);
            tp->shutdown = true;
            pthread_cond_broadcast(&tp->work_available);
            pthread_mutex_unlock(&tp->mutex);
            for (int j = 0; j < i; ++j) pthread_join(tp->threads[j], NULL);
            if (attr_ptr) pthread_attr_destroy(attr_ptr);
            goto fail_threads;
        }
    }
    if (attr_ptr) pthread_attr_destroy(attr_ptr);
    return tp;

fail_threads:
    pthread_cond_destroy(&tp->all_done);
fail_cv2:
    pthread_cond_destroy(&tp->work_available);
fail_cv1:
    pthread_mutex_destroy(&tp->mutex);
fail_mutex:
    free(tp->threads);
    free(tp);
    return NULL;
}

ThreadPool* tp_create(int threads) {
    return tp_create_with_stack(threads, 0);
}

bool tp_submit_priority(ThreadPool* tp, TpJobFn fn, void* arg, TpPriority priority) {
    if (!tp || !fn) return false;
    if (priority < 0) priority = TP_PRIORITY_LOW;
    if (priority >= TP_PRIO_COUNT) priority = (TpPriority)(TP_PRIO_COUNT - 1);

    TpJob* job = (TpJob*)malloc(sizeof(TpJob));
    if (!job) return false;
    job->fn = fn; job->arg = arg; job->next = NULL;

    pthread_mutex_lock(&tp->mutex);
    if (tp->shutdown) {
        pthread_mutex_unlock(&tp->mutex);
        free(job);
        return false;
    }
    TpQueue* q = &tp->queues[priority];
    if (q->tail) q->tail->next = job; else q->head = job;
    q->tail = job;
    tp->pending++;
    pthread_cond_signal(&tp->work_available);
    pthread_mutex_unlock(&tp->mutex);
    return true;
}

bool tp_submit(ThreadPool* tp, TpJobFn fn, void* arg) {
    return tp_submit_priority(tp, fn, arg, TP_PRIORITY_NORMAL);
}

void tp_wait_all(ThreadPool* tp) {
    if (!tp) return;
    pthread_mutex_lock(&tp->mutex);
    while (tp->pending > 0 || tp->active > 0) {
        pthread_cond_wait(&tp->all_done, &tp->mutex);
    }
    pthread_mutex_unlock(&tp->mutex);
}

void tp_shutdown(ThreadPool* tp) {
    if (!tp) return;
    pthread_mutex_lock(&tp->mutex);
    if (tp->joined) { pthread_mutex_unlock(&tp->mutex); return; }
    tp->shutdown = true;
    pthread_cond_broadcast(&tp->work_available);
    pthread_mutex_unlock(&tp->mutex);

    for (int i = 0; i < tp->thread_count; ++i) {
        pthread_join(tp->threads[i], NULL);
    }

    pthread_mutex_lock(&tp->mutex);
    tp->joined = true;
    // drain any orphaned jobs (submitted after shutdown was set: should be none)
    for (int p = 0; p < TP_PRIO_COUNT; ++p) {
        TpJob* j = tp->queues[p].head;
        while (j) { TpJob* n = j->next; free(j); j = n; }
        tp->queues[p].head = tp->queues[p].tail = NULL;
    }
    tp->pending = 0;
    pthread_mutex_unlock(&tp->mutex);
}

void tp_destroy(ThreadPool* tp) {
    if (!tp) return;
    tp_shutdown(tp);
    pthread_cond_destroy(&tp->all_done);
    pthread_cond_destroy(&tp->work_available);
    pthread_mutex_destroy(&tp->mutex);
    free(tp->threads);
    free(tp);
}

int tp_thread_count(const ThreadPool* tp) {
    return tp ? tp->thread_count : 0;
}

size_t tp_pending(const ThreadPool* tp) {
    if (!tp) return 0;
    // const_cast on the mutex: snapshot, not a long-term observation
    pthread_mutex_lock((pthread_mutex_t*)&tp->mutex);
    size_t n = tp->pending;
    pthread_mutex_unlock((pthread_mutex_t*)&tp->mutex);
    return n;
}
