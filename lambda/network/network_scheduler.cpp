// network_scheduler.cpp
// Network scheduler with a dedicated blocking-curl worker backend.

#include "network_scheduler.h"
#include "curl_multi_backend.h"
#include "network_downloader.h"
#include "network_resource_manager.h"
#include "../../lib/arraylist.h"
#include "../../lib/hashmap.h"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/url.h"
#include <pthread.h>
#include <string.h>

#define NETWORK_PRIORITY_COUNT 4
#define NETWORK_DEFAULT_GLOBAL_TRANSFERS 8
#define NETWORK_DEFAULT_TRANSFERS_PER_ORIGIN 6

typedef struct ScheduledTask {
    NetworkScheduler* scheduler;
    TaskFunction task_fn;
    TaskCompletionFunction completion_fn;
    void* task_data;
    char* origin;
    ResourcePriority priority;
} ScheduledTask;

typedef struct OriginCounter {
    char* origin;
    int active_count;
} OriginCounter;

struct NetworkScheduler {
    NetworkThreadPool* backend_pool;
    NetworkSchedulerConfig config;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t* workers;
    int worker_count;
    CurlMultiBackend* curl_multi_backend;
    bool use_curl_multi_backend;
    ArrayList* queues[NETWORK_PRIORITY_COUNT];
    void* origin_counters;
    int active_count;
    int queued_count;
    bool shutdown_flag;
    bool stop_workers;
};

static uint64_t origin_counter_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const OriginCounter* counter = (const OriginCounter*)item;
    return hashmap_sip(counter->origin, strlen(counter->origin), seed0, seed1);
}

static int origin_counter_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const OriginCounter* ca = (const OriginCounter*)a;
    const OriginCounter* cb = (const OriginCounter*)b;
    return strcmp(ca->origin, cb->origin);
}

static void origin_counter_free(void* item) {
    OriginCounter* counter = (OriginCounter*)item;
    if (counter->origin) mem_free(counter->origin);
}

static char* scheduler_origin_from_url(const char* url_text) {
    if (!url_text || url_text[0] == '\0') {
        return mem_strdup("<unknown>", MEM_CAT_NETWORK);
    }

    Url* url = url_parse(url_text);
    if (!url || !url_is_valid(url)) {
        if (url) url_destroy(url);
        return mem_strdup("<unknown>", MEM_CAT_NETWORK);
    }

    const char* origin = url_get_origin(url);
    char* copied = mem_strdup((origin && origin[0] != '\0') ? origin : "<unknown>", MEM_CAT_NETWORK);
    url_destroy(url);
    return copied;
}

static void scheduled_task_free(ScheduledTask* task) {
    if (!task) return;
    if (task->origin) mem_free(task->origin);
    mem_free(task);
}

static bool remove_queued_task_locked(NetworkScheduler* scheduler, void* task_data) {
    if (!scheduler) return false;

    for (int priority = PRIORITY_CRITICAL; priority <= PRIORITY_LOW; priority++) {
        ArrayList* queue = scheduler->queues[priority];
        if (!queue) continue;

        for (int i = 0; i < queue->length; i++) {
            ScheduledTask* task = (ScheduledTask*)queue->data[i];
            if (task && task->task_data == task_data) {
                arraylist_remove(queue, i);
                scheduler->queued_count--;
                scheduled_task_free(task);
                return true;
            }
        }
    }

    return false;
}

static OriginCounter* get_origin_counter_locked(NetworkScheduler* scheduler,
                                                const char* origin,
                                                bool create) {
    if (!scheduler || !scheduler->origin_counters || !origin) return NULL;

    OriginCounter key = { .origin = (char*)origin, .active_count = 0 };
    OriginCounter* found = (OriginCounter*)hashmap_get((struct hashmap*)scheduler->origin_counters, &key);
    if (found || !create) return found;

    OriginCounter entry = { .origin = mem_strdup(origin, MEM_CAT_NETWORK), .active_count = 0 };
    if (!entry.origin) return NULL;

    hashmap_set((struct hashmap*)scheduler->origin_counters, &entry);
    return (OriginCounter*)hashmap_get((struct hashmap*)scheduler->origin_counters, &key);
}

static bool can_dispatch_locked(NetworkScheduler* scheduler, ScheduledTask* task) {
    if (!scheduler || !task) return false;
    if (scheduler->active_count >= scheduler->config.max_global_transfers) return false;

    OriginCounter* counter = get_origin_counter_locked(scheduler, task->origin, false);
    int origin_active_count = counter ? counter->active_count : 0;
    return origin_active_count < scheduler->config.max_transfers_per_origin;
}

static ScheduledTask* pop_dispatchable_locked(NetworkScheduler* scheduler) {
    if (!scheduler) return NULL;

    for (int priority = PRIORITY_CRITICAL; priority <= PRIORITY_LOW; priority++) {
        ArrayList* queue = scheduler->queues[priority];
        if (!queue) continue;

        for (int i = 0; i < queue->length; i++) {
            ScheduledTask* task = (ScheduledTask*)queue->data[i];
            if (!can_dispatch_locked(scheduler, task)) continue;

            arraylist_remove(queue, i);
            scheduler->queued_count--;
            return task;
        }
    }

    return NULL;
}

static void decrement_origin_active_locked(NetworkScheduler* scheduler, const char* origin) {
    OriginCounter* counter = get_origin_counter_locked(scheduler, origin, false);
    if (counter && counter->active_count > 0) {
        counter->active_count--;
    }
}

static void mark_task_active_locked(NetworkScheduler* scheduler, ScheduledTask* task) {
    if (!scheduler || !task) return;

    OriginCounter* counter = get_origin_counter_locked(scheduler, task->origin, true);
    scheduler->active_count++;
    if (counter) counter->active_count++;
}

static void finish_active_task(NetworkScheduler* scheduler, ScheduledTask* task) {
    if (!scheduler || !task) return;

    pthread_mutex_lock(&scheduler->mutex);
    if (scheduler->active_count > 0) scheduler->active_count--;
    decrement_origin_active_locked(scheduler, task->origin);
    pthread_cond_broadcast(&scheduler->cond);
    pthread_mutex_unlock(&scheduler->mutex);
}

static void scheduler_curl_multi_complete(void* request_data, bool success, void* user_data) {
    NetworkScheduler* scheduler = (NetworkScheduler*)user_data;
    ScheduledTask* task = (ScheduledTask*)request_data;
    if (!scheduler || !task) return;

    if (task->completion_fn) {
        task->completion_fn(task->task_data, success);
    }

    finish_active_task(scheduler, task);
    scheduled_task_free(task);
}

static void* scheduler_worker_main(void* arg) {
    NetworkScheduler* scheduler = (NetworkScheduler*)arg;
    if (!scheduler) return NULL;

    while (true) {
        pthread_mutex_lock(&scheduler->mutex);

        ScheduledTask* task = NULL;
        while (!scheduler->stop_workers) {
            task = pop_dispatchable_locked(scheduler);
            if (task) {
                mark_task_active_locked(scheduler, task);
                break;
            }
            pthread_cond_wait(&scheduler->cond, &scheduler->mutex);
        }

        if (scheduler->stop_workers && !task) {
            pthread_mutex_unlock(&scheduler->mutex);
            break;
        }

        pthread_mutex_unlock(&scheduler->mutex);

        if (task->completion_fn) {
            bool success = network_download_resource((NetworkResource*)task->task_data);
            task->completion_fn(task->task_data, success);
        } else if (task->task_fn) {
            task->task_fn(task->task_data);
        }

        finish_active_task(scheduler, task);

        scheduled_task_free(task);
    }

    return NULL;
}

static void* scheduler_multi_dispatcher_main(void* arg) {
    NetworkScheduler* scheduler = (NetworkScheduler*)arg;
    if (!scheduler) return NULL;

    while (true) {
        pthread_mutex_lock(&scheduler->mutex);

        ScheduledTask* task = NULL;
        while (!scheduler->stop_workers) {
            task = pop_dispatchable_locked(scheduler);
            if (task) {
                mark_task_active_locked(scheduler, task);
                break;
            }
            pthread_cond_wait(&scheduler->cond, &scheduler->mutex);
        }

        if (scheduler->stop_workers && !task) {
            pthread_mutex_unlock(&scheduler->mutex);
            break;
        }

        pthread_mutex_unlock(&scheduler->mutex);

        bool submitted = false;
        if (task->completion_fn && scheduler->curl_multi_backend) {
            submitted = curl_multi_backend_submit(scheduler->curl_multi_backend,
                                                  (NetworkResource*)task->task_data,
                                                  task);
        }

        if (!submitted) {
            if (task->completion_fn) {
                task->completion_fn(task->task_data, false);
            } else if (task->task_fn) {
                task->task_fn(task->task_data);
            }
            finish_active_task(scheduler, task);
            scheduled_task_free(task);
        }
    }

    return NULL;
}

extern "C" {

NetworkScheduler* network_scheduler_create(NetworkThreadPool* backend_pool,
                                           const NetworkSchedulerConfig* config) {
    NetworkScheduler* scheduler =
        (NetworkScheduler*)mem_calloc(1, sizeof(NetworkScheduler), MEM_CAT_NETWORK);
    if (!scheduler) return NULL;

    scheduler->backend_pool = backend_pool;
    scheduler->shutdown_flag = false;
    if (config) {
        scheduler->config = *config;
    } else {
        scheduler->config.max_global_transfers = 0;
        scheduler->config.max_transfers_per_origin = 0;
        scheduler->config.max_cache_writes = 4;
        scheduler->config.max_decode_jobs = 0;
    }

    if (scheduler->config.max_global_transfers <= 0) {
        scheduler->config.max_global_transfers = backend_pool && backend_pool->num_threads > 0
            ? backend_pool->num_threads
            : NETWORK_DEFAULT_GLOBAL_TRANSFERS;
    }
    if (scheduler->config.max_transfers_per_origin <= 0) {
        scheduler->config.max_transfers_per_origin = NETWORK_DEFAULT_TRANSFERS_PER_ORIGIN;
    }
    if (scheduler->config.max_cache_writes <= 0) {
        scheduler->config.max_cache_writes = 4;
    }
    scheduler->use_curl_multi_backend = scheduler->config.use_curl_multi_backend;

    if (pthread_mutex_init(&scheduler->mutex, NULL) != 0) {
        mem_free(scheduler);
        return NULL;
    }
    if (pthread_cond_init(&scheduler->cond, NULL) != 0) {
        pthread_mutex_destroy(&scheduler->mutex);
        mem_free(scheduler);
        return NULL;
    }

    for (int i = 0; i < NETWORK_PRIORITY_COUNT; i++) {
        scheduler->queues[i] = arraylist_new(16);
        if (!scheduler->queues[i]) {
            for (int j = 0; j < i; j++) arraylist_free(scheduler->queues[j]);
            pthread_cond_destroy(&scheduler->cond);
            pthread_mutex_destroy(&scheduler->mutex);
            mem_free(scheduler);
            return NULL;
        }
    }

    scheduler->origin_counters = hashmap_new(
        sizeof(OriginCounter),
        0,
        0, 0,
        origin_counter_hash,
        origin_counter_compare,
        origin_counter_free,
        NULL
    );
    if (!scheduler->origin_counters) {
        for (int i = 0; i < NETWORK_PRIORITY_COUNT; i++) arraylist_free(scheduler->queues[i]);
        pthread_cond_destroy(&scheduler->cond);
        pthread_mutex_destroy(&scheduler->mutex);
        mem_free(scheduler);
        return NULL;
    }

    if (scheduler->use_curl_multi_backend) {
        scheduler->curl_multi_backend =
            curl_multi_backend_create(scheduler_curl_multi_complete, scheduler);
        if (!scheduler->curl_multi_backend) {
            log_warn("network-scheduler: curl multi backend unavailable, using blocking workers");
            scheduler->use_curl_multi_backend = false;
        }
    }

    scheduler->worker_count = scheduler->use_curl_multi_backend
        ? 1
        : scheduler->config.max_global_transfers;
    scheduler->workers = (pthread_t*)mem_calloc((size_t)scheduler->worker_count,
                                                sizeof(pthread_t),
                                                MEM_CAT_NETWORK);
    if (!scheduler->workers) {
        hashmap_free((struct hashmap*)scheduler->origin_counters);
        for (int i = 0; i < NETWORK_PRIORITY_COUNT; i++) arraylist_free(scheduler->queues[i]);
        pthread_cond_destroy(&scheduler->cond);
        pthread_mutex_destroy(&scheduler->mutex);
        mem_free(scheduler);
        return NULL;
    }

    for (int i = 0; i < scheduler->worker_count; i++) {
        void* (*thread_fn)(void*) = scheduler->use_curl_multi_backend
            ? scheduler_multi_dispatcher_main
            : scheduler_worker_main;
        if (pthread_create(&scheduler->workers[i], NULL, thread_fn, scheduler) != 0) {
            pthread_mutex_lock(&scheduler->mutex);
            scheduler->stop_workers = true;
            pthread_cond_broadcast(&scheduler->cond);
            pthread_mutex_unlock(&scheduler->mutex);
            for (int j = 0; j < i; j++) {
                pthread_join(scheduler->workers[j], NULL);
            }
            mem_free(scheduler->workers);
            if (scheduler->curl_multi_backend) {
                curl_multi_backend_destroy(scheduler->curl_multi_backend);
            }
            hashmap_free((struct hashmap*)scheduler->origin_counters);
            for (int j = 0; j < NETWORK_PRIORITY_COUNT; j++) arraylist_free(scheduler->queues[j]);
            pthread_cond_destroy(&scheduler->cond);
            pthread_mutex_destroy(&scheduler->mutex);
            mem_free(scheduler);
            return NULL;
        }
    }

    log_debug("network-scheduler: created %s backend (workers=%d, global=%d, per-origin=%d)",
              scheduler->use_curl_multi_backend ? "curl-multi" : "dedicated blocking-curl",
              scheduler->worker_count,
              scheduler->config.max_global_transfers,
              scheduler->config.max_transfers_per_origin);
    return scheduler;
}

void network_scheduler_destroy(NetworkScheduler* scheduler) {
    if (!scheduler) return;
    log_debug("network-scheduler: destroying scheduler");

    pthread_mutex_lock(&scheduler->mutex);
    scheduler->shutdown_flag = true;
    scheduler->stop_workers = true;
    for (int priority = 0; priority < NETWORK_PRIORITY_COUNT; priority++) {
        ArrayList* queue = scheduler->queues[priority];
        if (!queue) continue;
        for (int i = 0; i < queue->length; i++) {
            scheduled_task_free((ScheduledTask*)queue->data[i]);
        }
        arraylist_clear(queue);
    }
    scheduler->queued_count = 0;
    pthread_cond_broadcast(&scheduler->cond);
    pthread_mutex_unlock(&scheduler->mutex);

    for (int i = 0; i < scheduler->worker_count; i++) {
        pthread_join(scheduler->workers[i], NULL);
    }
    if (scheduler->workers) mem_free(scheduler->workers);

    if (scheduler->curl_multi_backend) {
        curl_multi_backend_destroy(scheduler->curl_multi_backend);
        scheduler->curl_multi_backend = NULL;
    }

    for (int priority = 0; priority < NETWORK_PRIORITY_COUNT; priority++) {
        if (scheduler->queues[priority]) {
            arraylist_free(scheduler->queues[priority]);
            scheduler->queues[priority] = NULL;
        }
    }

    if (scheduler->origin_counters) {
        hashmap_free((struct hashmap*)scheduler->origin_counters);
    }
    pthread_cond_destroy(&scheduler->cond);
    pthread_mutex_destroy(&scheduler->mutex);
    mem_free(scheduler);
}

bool network_scheduler_submit(NetworkScheduler* scheduler,
                              TaskFunction task_fn,
                              void* task_data,
                              const char* url,
                              ResourcePriority priority) {
    if (!scheduler || !task_fn) return false;

    if (priority < PRIORITY_CRITICAL || priority > PRIORITY_LOW) {
        priority = PRIORITY_NORMAL;
    }

    ScheduledTask* task = (ScheduledTask*)mem_calloc(1, sizeof(ScheduledTask), MEM_CAT_NETWORK);
    if (!task) return false;

    task->scheduler = scheduler;
    task->task_fn = task_fn;
    task->task_data = task_data;
    task->priority = priority;
    task->origin = scheduler_origin_from_url(url);
    if (!task->origin) {
        scheduled_task_free(task);
        return false;
    }

    pthread_mutex_lock(&scheduler->mutex);
    if (scheduler->shutdown_flag) {
        pthread_mutex_unlock(&scheduler->mutex);
        scheduled_task_free(task);
        return false;
    }

    bool ok = arraylist_append(scheduler->queues[priority], task) != 0;
    if (ok) {
        scheduler->queued_count++;
        pthread_cond_broadcast(&scheduler->cond);
    }
    pthread_mutex_unlock(&scheduler->mutex);

    if (!ok) {
        log_error("network-scheduler: submit failed for priority %d", priority);
        scheduled_task_free(task);
    }
    return ok;
}

bool network_scheduler_submit_download(NetworkScheduler* scheduler,
                                       void* resource,
                                       TaskCompletionFunction completion_fn,
                                       const char* url,
                                       ResourcePriority priority) {
    if (!scheduler || !resource || !completion_fn) return false;

    if (priority < PRIORITY_CRITICAL || priority > PRIORITY_LOW) {
        priority = PRIORITY_NORMAL;
    }

    ScheduledTask* task = (ScheduledTask*)mem_calloc(1, sizeof(ScheduledTask), MEM_CAT_NETWORK);
    if (!task) return false;

    task->scheduler = scheduler;
    task->task_data = resource;
    task->completion_fn = completion_fn;
    task->priority = priority;
    task->origin = scheduler_origin_from_url(url);
    if (!task->origin) {
        scheduled_task_free(task);
        return false;
    }

    pthread_mutex_lock(&scheduler->mutex);
    if (scheduler->shutdown_flag) {
        pthread_mutex_unlock(&scheduler->mutex);
        scheduled_task_free(task);
        return false;
    }

    bool ok = arraylist_append(scheduler->queues[priority], task) != 0;
    if (ok) {
        scheduler->queued_count++;
        pthread_cond_broadcast(&scheduler->cond);
    }
    pthread_mutex_unlock(&scheduler->mutex);

    if (!ok) {
        log_error("network-scheduler: submit download failed for priority %d", priority);
        scheduled_task_free(task);
    }
    return ok;
}

bool network_scheduler_cancel(NetworkScheduler* scheduler, void* task_data) {
    if (!scheduler || !task_data) return false;

    pthread_mutex_lock(&scheduler->mutex);
    bool cancelled = remove_queued_task_locked(scheduler, task_data);
    if (cancelled) pthread_cond_broadcast(&scheduler->cond);
    pthread_mutex_unlock(&scheduler->mutex);

    if (cancelled) {
        if (scheduler->curl_multi_backend) {
            curl_multi_backend_cancel(scheduler->curl_multi_backend, task_data);
        }
        log_debug("network-scheduler: cancelled queued task");
    }
    return cancelled;
}

void network_scheduler_wait_all(NetworkScheduler* scheduler) {
    if (!scheduler) return;

    log_debug("network-scheduler: waiting for all scheduled tasks");
    pthread_mutex_lock(&scheduler->mutex);
    while (scheduler->active_count > 0 || scheduler->queued_count > 0) {
        pthread_cond_wait(&scheduler->cond, &scheduler->mutex);
    }
    pthread_mutex_unlock(&scheduler->mutex);
    if (scheduler->curl_multi_backend) {
        curl_multi_backend_wait_all(scheduler->curl_multi_backend);
    }
    log_debug("network-scheduler: all scheduled tasks completed");
}

void network_scheduler_shutdown(NetworkScheduler* scheduler) {
    if (!scheduler) return;
    pthread_mutex_lock(&scheduler->mutex);
    scheduler->shutdown_flag = true;
    for (int priority = 0; priority < NETWORK_PRIORITY_COUNT; priority++) {
        ArrayList* queue = scheduler->queues[priority];
        if (!queue) continue;
        for (int i = 0; i < queue->length; i++) {
            scheduled_task_free((ScheduledTask*)queue->data[i]);
        }
        arraylist_clear(queue);
    }
    scheduler->queued_count = 0;
    pthread_cond_broadcast(&scheduler->cond);
    pthread_mutex_unlock(&scheduler->mutex);
    if (scheduler->curl_multi_backend) {
        curl_multi_backend_shutdown(scheduler->curl_multi_backend);
    }
    log_debug("network-scheduler: shutdown requested");
}

int network_scheduler_get_active_count(const NetworkScheduler* scheduler) {
    if (!scheduler) return 0;
    pthread_mutex_lock((pthread_mutex_t*)&scheduler->mutex);
    int active_count = scheduler->active_count;
    pthread_mutex_unlock((pthread_mutex_t*)&scheduler->mutex);
    return active_count;
}

int network_scheduler_get_queued_count(const NetworkScheduler* scheduler) {
    if (!scheduler) return 0;
    pthread_mutex_lock((pthread_mutex_t*)&scheduler->mutex);
    int queued_count = scheduler->queued_count;
    pthread_mutex_unlock((pthread_mutex_t*)&scheduler->mutex);
    return queued_count;
}

}  // extern "C"
