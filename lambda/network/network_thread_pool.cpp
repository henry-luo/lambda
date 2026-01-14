// network_thread_pool.cpp
// Thread pool implementation for network resource downloads

#include "network_thread_pool.h"
#include "../../lib/priority_queue.h"
#include "../../lib/log.h"
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

// Default number of threads
#define DEFAULT_THREAD_COUNT 4

// Get current time in seconds
static double get_time_seconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

// Worker thread function
static void* worker_thread_func(void* arg) {
    NetworkThreadPool* pool = (NetworkThreadPool*)arg;
    
    log_debug("network: worker thread %lu started", pthread_self());
    
    while (true) {
        pthread_mutex_lock(&pool->queue_mutex);
        
        // Wait for tasks or shutdown
        while (priority_queue_is_empty((PriorityQueue*)pool->task_queue) && 
               !pool->shutdown_flag) {
            pthread_cond_wait(&pool->queue_cond, &pool->queue_mutex);
        }
        
        // Check for shutdown
        if (pool->shutdown_flag && 
            priority_queue_is_empty((PriorityQueue*)pool->task_queue)) {
            pthread_mutex_unlock(&pool->queue_mutex);
            break;  // Exit thread
        }
        
        // Dequeue highest priority task
        DownloadTask* task = (DownloadTask*)priority_queue_pop((PriorityQueue*)pool->task_queue);
        
        if (task) {
            pool->active_count++;
            pool->queued_count--;
        }
        
        pthread_mutex_unlock(&pool->queue_mutex);
        
        // Execute task outside of lock
        if (task) {
            double wait_time = get_time_seconds() - task->enqueue_time;
            log_debug("network: executing task (priority %d, waited %.3fs) on thread %lu",
                      task->priority, wait_time, pthread_self());
            
            // Execute the task function
            if (task->task_fn) {
                task->task_fn(task->resource);
            }
            
            free(task);
            
            // Decrement active count
            pthread_mutex_lock(&pool->queue_mutex);
            pool->active_count--;
            pthread_mutex_unlock(&pool->queue_mutex);
        }
    }
    
    log_debug("network: worker thread %lu exiting", pthread_self());
    return NULL;
}

// Create thread pool
NetworkThreadPool* thread_pool_create(int num_threads) {
    if (num_threads <= 0) {
        num_threads = DEFAULT_THREAD_COUNT;
    }
    
    NetworkThreadPool* pool = (NetworkThreadPool*)calloc(1, sizeof(NetworkThreadPool));
    if (!pool) return NULL;
    
    pool->num_threads = num_threads;
    pool->shutdown_flag = false;
    pool->active_count = 0;
    pool->queued_count = 0;
    
    // Create priority queue
    pool->task_queue = priority_queue_create(32);
    if (!pool->task_queue) {
        free(pool);
        return NULL;
    }
    
    // Initialize mutex and condition variable
    if (pthread_mutex_init(&pool->queue_mutex, NULL) != 0) {
        priority_queue_destroy((PriorityQueue*)pool->task_queue);
        free(pool);
        return NULL;
    }
    
    if (pthread_cond_init(&pool->queue_cond, NULL) != 0) {
        pthread_mutex_destroy(&pool->queue_mutex);
        priority_queue_destroy((PriorityQueue*)pool->task_queue);
        free(pool);
        return NULL;
    }
    
    // Create worker threads
    pool->threads = (pthread_t*)calloc(num_threads, sizeof(pthread_t));
    if (!pool->threads) {
        pthread_cond_destroy(&pool->queue_cond);
        pthread_mutex_destroy(&pool->queue_mutex);
        priority_queue_destroy((PriorityQueue*)pool->task_queue);
        free(pool);
        return NULL;
    }
    
    // Start worker threads
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_thread_func, pool) != 0) {
            log_error("network: failed to create worker thread %d", i);
            // Cleanup already created threads
            pool->shutdown_flag = true;
            pthread_cond_broadcast(&pool->queue_cond);
            
            for (int j = 0; j < i; j++) {
                pthread_join(pool->threads[j], NULL);
            }
            
            free(pool->threads);
            pthread_cond_destroy(&pool->queue_cond);
            pthread_mutex_destroy(&pool->queue_mutex);
            priority_queue_destroy((PriorityQueue*)pool->task_queue);
            free(pool);
            return NULL;
        }
    }
    
    log_debug("network: created thread pool with %d workers", num_threads);
    
    return pool;
}

// Destroy thread pool
void thread_pool_destroy(NetworkThreadPool* pool) {
    if (!pool) return;
    
    log_debug("network: destroying thread pool");
    
    // Signal shutdown
    pthread_mutex_lock(&pool->queue_mutex);
    pool->shutdown_flag = true;
    pthread_cond_broadcast(&pool->queue_cond);
    pthread_mutex_unlock(&pool->queue_mutex);
    
    // Wait for all threads to finish
    for (int i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->threads[i], NULL);
    }
    
    // Cleanup remaining tasks
    pthread_mutex_lock(&pool->queue_mutex);
    while (!priority_queue_is_empty((PriorityQueue*)pool->task_queue)) {
        DownloadTask* task = (DownloadTask*)priority_queue_pop((PriorityQueue*)pool->task_queue);
        free(task);
    }
    pthread_mutex_unlock(&pool->queue_mutex);
    
    // Cleanup resources
    free(pool->threads);
    priority_queue_destroy((PriorityQueue*)pool->task_queue);
    pthread_cond_destroy(&pool->queue_cond);
    pthread_mutex_destroy(&pool->queue_mutex);
    free(pool);
    
    log_debug("network: thread pool destroyed");
}

// Enqueue task
bool thread_pool_enqueue(NetworkThreadPool* pool,
                        TaskFunction task_fn,
                        void* task_data,
                        ResourcePriority priority) {
    if (!pool || !task_fn) return false;
    
    // Create task
    DownloadTask* task = (DownloadTask*)malloc(sizeof(DownloadTask));
    if (!task) return false;
    
    task->resource = task_data;
    task->task_fn = task_fn;
    task->priority = priority;
    task->enqueue_time = get_time_seconds();
    
    // Add to queue
    pthread_mutex_lock(&pool->queue_mutex);
    
    if (pool->shutdown_flag) {
        pthread_mutex_unlock(&pool->queue_mutex);
        free(task);
        return false;
    }
    
    bool success = priority_queue_push((PriorityQueue*)pool->task_queue, task, priority);
    
    if (success) {
        pool->queued_count++;
        pthread_cond_signal(&pool->queue_cond);  // Wake up one worker
    }
    
    pthread_mutex_unlock(&pool->queue_mutex);
    
    if (!success) {
        free(task);
        log_error("network: failed to enqueue task");
        return false;
    }
    
    log_debug("network: enqueued task with priority %d (%d tasks queued)",
              priority, pool->queued_count);
    
    return true;
}

// Wait for all tasks to complete
void thread_pool_wait_all(NetworkThreadPool* pool) {
    if (!pool) return;
    
    log_debug("network: waiting for all tasks to complete");
    
    while (true) {
        pthread_mutex_lock(&pool->queue_mutex);
        
        bool idle = (pool->active_count == 0 && 
                    priority_queue_is_empty((PriorityQueue*)pool->task_queue));
        
        pthread_mutex_unlock(&pool->queue_mutex);
        
        if (idle) {
            break;
        }
        
        usleep(10000);  // Sleep 10ms
    }
    
    log_debug("network: all tasks completed");
}

// Shutdown thread pool (stop accepting new tasks)
void thread_pool_shutdown(NetworkThreadPool* pool) {
    if (!pool) return;
    
    pthread_mutex_lock(&pool->queue_mutex);
    pool->shutdown_flag = true;
    pthread_cond_broadcast(&pool->queue_cond);
    pthread_mutex_unlock(&pool->queue_mutex);
    
    log_debug("network: thread pool shutdown initiated");
}

// Get active worker count
int thread_pool_get_active_count(const NetworkThreadPool* pool) {
    if (!pool) return 0;
    return pool->active_count;
}

// Get queued task count
int thread_pool_get_queued_count(const NetworkThreadPool* pool) {
    if (!pool) return 0;
    return pool->queued_count;
}
