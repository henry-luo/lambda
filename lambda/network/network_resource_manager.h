// network_resource_manager.h
// Central coordinator for network resource loading in Radiant
// Handles dependency tracking, reflow/repaint scheduling, and error aggregation

#ifndef NETWORK_RESOURCE_MANAGER_H
#define NETWORK_RESOURCE_MANAGER_H

#include <pthread.h>
#include <stdbool.h>
#include "network_thread_pool.h"
#include "enhanced_file_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct DomDocument;
struct DomElement;
struct CssEngine;

// Resource types
typedef enum {
    RESOURCE_HTML,
    RESOURCE_CSS,
    RESOURCE_IMAGE,
    RESOURCE_FONT,
    RESOURCE_SVG,
    RESOURCE_SCRIPT
} ResourceType;

// Resource states
typedef enum {
    STATE_PENDING,      // Queued but not started
    STATE_DOWNLOADING,  // In flight
    STATE_COMPLETED,    // Successfully loaded
    STATE_FAILED,       // Download failed (timeout/404/etc)
    STATE_CACHED        // Served from cache
} ResourceState;

// Network resource structure
typedef struct NetworkResource {
    char* url;                      // Absolute URL
    char* local_path;               // Cache file path (if cached)
    ResourceType type;
    ResourceState state;
    ResourcePriority priority;
    
    // Dependency tracking
    struct DomElement* owner_element;  // Element that requested this
    void* dependents;               // ArrayList of dependent resources
    
    // Timing & error info
    double start_time;
    double end_time;
    int http_status_code;
    char* error_message;
    
    // Timeout and retry (Phase 5)
    int timeout_ms;                 // Timeout in milliseconds (default 30000)
    int retry_count;                // Current retry attempt (0 = first try)
    int max_retries;                // Maximum retry attempts (default 3)
    
    // Manager and cache references
    struct NetworkResourceManager* manager;  // Back-reference to manager
    EnhancedFileCache* cache;                // Cache for storing downloaded content
    
    // Callback on completion
    void (*on_complete)(struct NetworkResource* res, void* user_data);
    void* user_data;
    
    // Reference count for cleanup
    int ref_count;
} NetworkResource;

// Network resource manager
typedef struct NetworkResourceManager {
    struct DomDocument* document;
    NetworkThreadPool* thread_pool;
    EnhancedFileCache* file_cache;
    
    // CSS parsing context (for processing network-loaded stylesheets)
    struct CssEngine* css_engine;
    
    // UI context (for font loading and HiDPI support)
    void* ui_context;  // UiContext* (opaque to avoid header dependency)
    
    void* resources;                // HashMap: URL → NetworkResource*
    void* pending_reflows;          // ArrayList of DomElement*
    void* pending_repaints;         // ArrayList of DomElement*
    
    pthread_mutex_t mutex;          // Protects resource list
    
    // Load tracking
    double load_start_time;
    int total_resources;
    int completed_resources;
    int failed_resources;
    
    // Timeout configuration (Phase 5)
    int default_timeout_ms;         // Default per-resource timeout (30000ms)
    int page_load_timeout_ms;       // Total page load timeout (60000ms)
    
    // Error callback
    void (*error_callback)(NetworkResource*, void*);
    void* error_callback_data;
} NetworkResourceManager;

// Create and destroy
NetworkResourceManager* resource_manager_create(struct DomDocument* doc,
                                                NetworkThreadPool* pool,
                                                EnhancedFileCache* cache);
void resource_manager_destroy(NetworkResourceManager* mgr);

// Context setup (called after UiContext and CssEngine are initialized)
void resource_manager_set_css_engine(NetworkResourceManager* mgr, struct CssEngine* engine);
void resource_manager_set_ui_context(NetworkResourceManager* mgr, void* uicon);

// Resource loading
NetworkResource* resource_manager_load(NetworkResourceManager* mgr,
                                      const char* url,
                                      ResourceType type,
                                      ResourcePriority priority,
                                      struct DomElement* owner);

void resource_manager_cancel(NetworkResourceManager* mgr, NetworkResource* res);
void resource_manager_cancel_for_element(NetworkResourceManager* mgr, struct DomElement* elmt);

// Status queries
bool resource_manager_is_fully_loaded(const NetworkResourceManager* mgr);
int resource_manager_get_pending_count(const NetworkResourceManager* mgr);
float resource_manager_get_load_progress(const NetworkResourceManager* mgr);
void resource_manager_get_stats(NetworkResourceManager* mgr, int* total, int* completed, int* failed);

// Phase 5: Timeout and retry
bool resource_manager_check_page_timeout(NetworkResourceManager* mgr);
bool resource_manager_retry_download(NetworkResourceManager* mgr, NetworkResource* res);

// Reflow/repaint scheduling (called from worker threads)
void resource_manager_schedule_reflow(NetworkResourceManager* mgr, struct DomElement* root);
void resource_manager_schedule_repaint(NetworkResourceManager* mgr, struct DomElement* element);

// Layout updates (called on main thread)
void resource_manager_flush_layout_updates(NetworkResourceManager* mgr);

// Error handling
void* resource_manager_get_failed_resources(const NetworkResourceManager* mgr);
void resource_manager_set_error_callback(NetworkResourceManager* mgr,
                                        void (*callback)(NetworkResource*, void*),
                                        void* user_data);

// Resource reference counting
void resource_retain(NetworkResource* res);
void resource_release(NetworkResource* res);

#ifdef __cplusplus
}
#endif

#endif // NETWORK_RESOURCE_MANAGER_H
