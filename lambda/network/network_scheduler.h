// network_scheduler.h
// Scheduler facade for Radiant network resource work.

#ifndef NETWORK_SCHEDULER_H
#define NETWORK_SCHEDULER_H

#include <stdbool.h>
#include "network_thread_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NetworkScheduler NetworkScheduler;

typedef struct NetworkSchedulerConfig {
    int max_global_transfers;
    int max_transfers_per_origin;
    int max_cache_writes;
    int max_decode_jobs;
    bool use_curl_multi_backend;
} NetworkSchedulerConfig;

typedef void (*TaskCompletionFunction)(void* task_data, bool success);

NetworkScheduler* network_scheduler_create(NetworkThreadPool* backend_pool,
                                           const NetworkSchedulerConfig* config);
void network_scheduler_destroy(NetworkScheduler* scheduler);

bool network_scheduler_submit(NetworkScheduler* scheduler,
                              TaskFunction task_fn,
                              void* task_data,
                              const char* url,
                              ResourcePriority priority);

bool network_scheduler_submit_download(NetworkScheduler* scheduler,
                                       void* resource,
                                       TaskCompletionFunction completion_fn,
                                       const char* url,
                                       ResourcePriority priority);

bool network_scheduler_cancel(NetworkScheduler* scheduler, void* task_data);
void network_scheduler_wait_all(NetworkScheduler* scheduler);
void network_scheduler_shutdown(NetworkScheduler* scheduler);

int network_scheduler_get_active_count(const NetworkScheduler* scheduler);
int network_scheduler_get_queued_count(const NetworkScheduler* scheduler);

#ifdef __cplusplus
}
#endif

#endif // NETWORK_SCHEDULER_H
