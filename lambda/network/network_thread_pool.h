// network_thread_pool.h
// Thread pool for asynchronous network resource downloads
// Manages worker threads with priority queue for task scheduling

#ifndef NETWORK_THREAD_POOL_H
#define NETWORK_THREAD_POOL_H

#include <pthread.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Resource priority (lower value = higher priority)
typedef enum {
    PRIORITY_CRITICAL = 0,  // Main HTML document
    PRIORITY_HIGH     = 1,  // CSS, fonts (block rendering)
    PRIORITY_NORMAL   = 2,  // Images, SVG
    PRIORITY_LOW      = 3   // Prefetch, async scripts
} ResourcePriority;

// Task function signature
typedef void (*TaskFunction)(void* task_data);

// Download task structure
typedef struct DownloadTask {
    void* resource;              // NetworkResource pointer
    TaskFunction task_fn;        // Task function to execute
    ResourcePriority priority;   // Task priority
    double enqueue_time;         // When task was queued
} DownloadTask;

// Thread pool structure
typedef struct NetworkThreadPool {
    int num_threads;             // Number of worker threads
    pthread_t* threads;          // Worker thread handles
    
    void* task_queue;            // PriorityQueue of DownloadTask*
    pthread_mutex_t queue_mutex; // Protects task queue
    pthread_cond_t queue_cond;   // Signals new tasks
    
    bool shutdown_flag;          // Shutdown requested
    int active_count;            // Number of active workers
    int queued_count;            // Number of queued tasks
} NetworkThreadPool;

// Create and destroy thread pool
NetworkThreadPool* thread_pool_create(int num_threads);
void thread_pool_destroy(NetworkThreadPool* pool);

// Task management
bool thread_pool_enqueue(NetworkThreadPool* pool,
                        TaskFunction task_fn,
                        void* task_data,
                        ResourcePriority priority);

void thread_pool_wait_all(NetworkThreadPool* pool);
void thread_pool_shutdown(NetworkThreadPool* pool);

// Statistics
int thread_pool_get_active_count(const NetworkThreadPool* pool);
int thread_pool_get_queued_count(const NetworkThreadPool* pool);

#ifdef __cplusplus
}
#endif

#endif // NETWORK_THREAD_POOL_H
