// network_resource_manager.cpp
// Full implementation with hashmap-based deduplication and arraylist scheduling

#include "network_resource_manager.h"
#include "network_downloader.h"
#include "resource_loaders.h"
#include "../../lib/url.h"
#include "../../lib/log.h"
#include "../../lib/hashmap.h"
#include "../../lib/arraylist.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// hashmap entry for URL→NetworkResource* lookup
typedef struct {
    char* url;              // key (owned)
    NetworkResource* res;   // value (not owned - managed separately)
} ResourceEntry;

// hash function for resource entries
static uint64_t resource_entry_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const ResourceEntry* entry = (const ResourceEntry*)item;
    return hashmap_sip(entry->url, strlen(entry->url), seed0, seed1);
}

// compare function for resource entries
static int resource_entry_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const ResourceEntry* ea = (const ResourceEntry*)a;
    const ResourceEntry* eb = (const ResourceEntry*)b;
    return strcmp(ea->url, eb->url);
}

// free function for resource entries (only frees URL, not resource)
static void resource_entry_free(void* item) {
    ResourceEntry* entry = (ResourceEntry*)item;
    if (entry->url) free(entry->url);
    // note: resource itself freed via resource_release
}

// CPP internal helpers
namespace {

// get current time in seconds
double get_time_seconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

// create network resource
NetworkResource* create_network_resource(const char* url,
                                                ResourceType type,
                                                ResourcePriority priority,
                                                struct DomElement* owner) {
    NetworkResource* res = (NetworkResource*)calloc(1, sizeof(NetworkResource));
    if (!res) return NULL;
    
    res->url = strdup(url);
    res->type = type;
    res->priority = priority;
    res->owner_element = owner;
    res->state = STATE_PENDING;
    res->dependents = NULL;  // could use arraylist here for dependencies
    res->start_time = get_time_seconds();
    res->ref_count = 1;
    res->http_status_code = 0;
    
    // Phase 5: timeout and retry defaults
    res->timeout_ms = 30000;  // 30 seconds default
    res->retry_count = 0;
    res->max_retries = 3;
    
    return res;
}

// free network resource
void free_network_resource(NetworkResource* res) {
    if (!res) return;
    
    free(res->url);
    free(res->local_path);
    free(res->error_message);
    free(res);
}

}  // namespace

// download task function (called by thread pool worker)
static void download_task_fn(void* task_data) {
    NetworkResource* res = (NetworkResource*)task_data;
    if (!res) return;
    
    log_debug("network: download task started: %s", res->url);
    
    // perform download with timeout enforcement
    bool success = network_download_resource(res);
    
    if (success) {
        res->state = STATE_COMPLETED;
        res->end_time = get_time_seconds();
        
        log_debug("network: download complete: %s (%.3fs)",
                  res->url, res->end_time - res->start_time);
        
        // invoke completion callback
        if (res->on_complete) {
            res->on_complete(res, res->user_data);
        }
        
        // process resource based on type
        if (res->manager && res->manager->document) {
            switch (res->type) {
                case RESOURCE_HTML:
                    process_html_resource(res, res->manager->document);
                    break;
                case RESOURCE_CSS:
                    process_css_resource(res, res->manager->document);
                    break;
                case RESOURCE_IMAGE:
                    if (res->owner_element) {
                        process_image_resource(res, res->owner_element);
                    }
                    break;
                case RESOURCE_FONT:
                    // Font requires FontFaceRule, not implemented yet
                    log_debug("network: font resource processing not yet implemented");
                    break;
                case RESOURCE_SVG:
                    if (res->owner_element) {
                        process_svg_resource(res, res->owner_element);
                    }
                    break;
                case RESOURCE_SCRIPT:
                    log_debug("network: script resource processing not yet implemented");
                    break;
            }
        }
        
        // update manager statistics
        if (res->manager) {
            pthread_mutex_lock(&res->manager->mutex);
            res->manager->completed_resources++;
            pthread_mutex_unlock(&res->manager->mutex);
        }
    } else {
        res->state = STATE_FAILED;
        res->end_time = get_time_seconds();
        
        log_error("network: download failed: %s - %s",
                  res->url, res->error_message ? res->error_message : "unknown error");
        
        // check if error is retryable
        bool should_retry = is_http_error_retryable(res->http_status_code);
        
        if (should_retry && res->retry_count < res->max_retries) {
            // retry with exponential backoff
            if (res->manager) {
                resource_manager_retry_download(res->manager, res);
            }
        } else {
            // handle failure
            if (res->manager && res->manager->document) {
                handle_resource_failure(res, res->manager->document);
            }
            
            // update manager statistics
            if (res->manager) {
                pthread_mutex_lock(&res->manager->mutex);
                res->manager->failed_resources++;
                pthread_mutex_unlock(&res->manager->mutex);
            }
        }
    }
}

// Exported C API functions
extern "C" {

// create resource manager
NetworkResourceManager* resource_manager_create(struct DomDocument* doc,
                                                NetworkThreadPool* pool,
                                                EnhancedFileCache* cache) {
    if (!doc || !pool) return NULL;
    
    NetworkResourceManager* mgr = (NetworkResourceManager*)calloc(1, sizeof(NetworkResourceManager));
    if (!mgr) return NULL;
    
    mgr->document = doc;
    mgr->thread_pool = pool;
    mgr->file_cache = cache;
    
    // create hashmap for URL→NetworkResource* lookup (deduplication)
    mgr->resources = hashmap_new(
        sizeof(ResourceEntry),
        0,                      // initial capacity (auto)
        0, 0,                   // seeds (default)
        resource_entry_hash,
        resource_entry_compare,
        resource_entry_free,
        NULL                    // udata
    );
    
    if (!mgr->resources) {
        free(mgr);
        return NULL;
    }
    
    // create arraylists for pending reflows and repaints
    mgr->pending_reflows = arraylist_new(16);
    mgr->pending_repaints = arraylist_new(16);
    
    if (pthread_mutex_init(&mgr->mutex, NULL) != 0) {
        hashmap_free((struct hashmap*)mgr->resources);
        if (mgr->pending_reflows) arraylist_free((ArrayList*)mgr->pending_reflows);
        if (mgr->pending_repaints) arraylist_free((ArrayList*)mgr->pending_repaints);
        free(mgr);
        return NULL;
    }
    
    mgr->load_start_time = get_time_seconds();
    mgr->total_resources = 0;
    mgr->completed_resources = 0;
    mgr->failed_resources = 0;
    
    // Phase 5: timeout configuration
    mgr->default_timeout_ms = 30000;  // 30 seconds per resource
    mgr->page_load_timeout_ms = 60000;  // 60 seconds total page load
    
    log_debug("network: created resource manager (timeouts: per-resource=%dms, page=%dms)",
              mgr->default_timeout_ms, mgr->page_load_timeout_ms);
    
    return mgr;
}

// destroy resource manager
void resource_manager_destroy(NetworkResourceManager* mgr) {
    if (!mgr) return;
    
    log_debug("network: destroying resource manager");
    
    // free all resources in hashmap
    if (mgr->resources) {
        size_t iter = 0;
        void* item;
        while (hashmap_iter((struct hashmap*)mgr->resources, &iter, &item)) {
            ResourceEntry* entry = (ResourceEntry*)item;
            if (entry->res) {
                resource_release(entry->res);
            }
        }
        hashmap_free((struct hashmap*)mgr->resources);
    }
    
    // free arraylists
    if (mgr->pending_reflows) arraylist_free((ArrayList*)mgr->pending_reflows);
    if (mgr->pending_repaints) arraylist_free((ArrayList*)mgr->pending_repaints);
    
    pthread_mutex_destroy(&mgr->mutex);
    free(mgr);
}

// set CSS engine for stylesheet parsing
void resource_manager_set_css_engine(NetworkResourceManager* mgr, struct CssEngine* engine) {
    if (!mgr) return;
    mgr->css_engine = engine;
    log_debug("network: CSS engine set for resource manager");
}

// set UI context for font loading
void resource_manager_set_ui_context(NetworkResourceManager* mgr, void* uicon) {
    if (!mgr) return;
    mgr->ui_context = uicon;
    log_debug("network: UI context set for resource manager");
}

// load resource (with deduplication and actual download integration)
NetworkResource* resource_manager_load(NetworkResourceManager* mgr,
                                      const char* url,
                                      ResourceType type,
                                      ResourcePriority priority,
                                      struct DomElement* owner) {
    if (!mgr || !url) return NULL;
    
    pthread_mutex_lock(&mgr->mutex);
    
    // check for existing resource (deduplication)
    ResourceEntry key = { .url = (char*)url, .res = NULL };
    const ResourceEntry* existing = (const ResourceEntry*)hashmap_get(
        (struct hashmap*)mgr->resources, &key);
    
    if (existing && existing->res) {
        // resource already exists
        NetworkResource* res = existing->res;
        
        // if already completed or cached, return immediately
        if (res->state == STATE_COMPLETED || res->state == STATE_CACHED) {
            log_debug("network: reusing completed resource: %s", url);
            resource_retain(res);
            pthread_mutex_unlock(&mgr->mutex);
            return res;
        }
        
        // if still downloading, just wait for it
        if (res->state == STATE_DOWNLOADING || res->state == STATE_PENDING) {
            log_debug("network: resource already loading: %s", url);
            resource_retain(res);
            pthread_mutex_unlock(&mgr->mutex);
            return res;
        }
        
        // if failed, might want to retry - for now, return failed resource
        if (res->state == STATE_FAILED) {
            log_debug("network: returning previously failed resource: %s", url);
            resource_retain(res);
            pthread_mutex_unlock(&mgr->mutex);
            return res;
        }
    }
    
    // check cache first
    if (mgr->file_cache) {
        char* cached_path = enhanced_cache_lookup(mgr->file_cache, url);
        if (cached_path) {
            // create resource from cache
            NetworkResource* res = create_network_resource(url, type, priority, owner);
            if (res) {
                res->state = STATE_CACHED;
                res->local_path = cached_path;
                res->manager = mgr;
                res->cache = mgr->file_cache;
                res->end_time = get_time_seconds();
                
                // add to hashmap
                ResourceEntry entry = { .url = strdup(url), .res = res };
                hashmap_set((struct hashmap*)mgr->resources, &entry);
                
                mgr->total_resources++;
                mgr->completed_resources++;
                
                log_debug("network: cache hit for: %s -> %s", url, cached_path);
                
                pthread_mutex_unlock(&mgr->mutex);
                return res;
            }
            free(cached_path);
        }
    }
    
    // create new resource
    NetworkResource* res = create_network_resource(url, type, priority, owner);
    if (!res) {
        pthread_mutex_unlock(&mgr->mutex);
        return NULL;
    }
    
    // set manager reference and cache
    res->manager = mgr;
    res->cache = mgr->file_cache;
    res->timeout_ms = mgr->default_timeout_ms;
    
    // add to hashmap
    ResourceEntry entry = { .url = strdup(url), .res = res };
    hashmap_set((struct hashmap*)mgr->resources, &entry);
    
    mgr->total_resources++;
    
    log_debug("network: loading resource: %s (type=%d, priority=%d)", url, type, priority);
    
    // queue task via thread pool with download function
    thread_pool_enqueue(mgr->thread_pool, download_task_fn, res, priority);
    res->state = STATE_DOWNLOADING;
    
    pthread_mutex_unlock(&mgr->mutex);
    
    return res;
}

// mark resource as completed
void resource_manager_mark_completed(NetworkResourceManager* mgr, NetworkResource* res) {
    if (!mgr || !res) return;
    
    pthread_mutex_lock(&mgr->mutex);
    
    res->state = STATE_COMPLETED;
    mgr->completed_resources++;
    
    log_debug("network: resource completed: %s (%d/%d)", 
              res->url, mgr->completed_resources, mgr->total_resources);
    
    // invoke callback if set
    if (res->on_complete) {
        res->on_complete(res, res->user_data);
    }
    
    pthread_mutex_unlock(&mgr->mutex);
}

// mark resource as failed
void resource_manager_mark_failed(NetworkResourceManager* mgr, NetworkResource* res, const char* error) {
    if (!mgr || !res) return;
    
    pthread_mutex_lock(&mgr->mutex);
    
    res->state = STATE_FAILED;
    res->error_message = strdup(error ? error : "Unknown error");
    mgr->failed_resources++;
    
    log_error("network: resource failed: %s - %s", res->url, res->error_message);
    
    pthread_mutex_unlock(&mgr->mutex);
}

// schedule reflow
void resource_manager_schedule_reflow(NetworkResourceManager* mgr, struct DomElement* element) {
    if (!mgr || !element) return;
    
    pthread_mutex_lock(&mgr->mutex);
    
    if (mgr->pending_reflows) {
        // check if element already in list (avoid duplicates)
        ArrayList* list = (ArrayList*)mgr->pending_reflows;
        bool found = false;
        for (int i = 0; i < list->length; i++) {
            if (list->data[i] == element) {
                found = true;
                break;
            }
        }
        if (!found) {
            arraylist_append(list, element);
            log_debug("network: scheduled reflow for element (pending: %d)", list->length);
        }
    }
    
    pthread_mutex_unlock(&mgr->mutex);
}

// schedule repaint
void resource_manager_schedule_repaint(NetworkResourceManager* mgr, struct DomElement* element) {
    if (!mgr || !element) return;
    
    pthread_mutex_lock(&mgr->mutex);
    
    if (mgr->pending_repaints) {
        // check if element already in list (avoid duplicates)
        ArrayList* list = (ArrayList*)mgr->pending_repaints;
        bool found = false;
        for (int i = 0; i < list->length; i++) {
            if (list->data[i] == element) {
                found = true;
                break;
            }
        }
        if (!found) {
            arraylist_append(list, element);
            log_debug("network: scheduled repaint for element (pending: %d)", list->length);
        }
    }
    
    pthread_mutex_unlock(&mgr->mutex);
}

// flush pending layout updates
void resource_manager_flush_layout_updates(NetworkResourceManager* mgr) {
    if (!mgr) return;
    
    pthread_mutex_lock(&mgr->mutex);
    
    ArrayList* reflows = (ArrayList*)mgr->pending_reflows;
    ArrayList* repaints = (ArrayList*)mgr->pending_repaints;
    
    int reflow_count = reflows ? reflows->length : 0;
    int repaint_count = repaints ? repaints->length : 0;
    
    if (reflow_count == 0 && repaint_count == 0) {
        pthread_mutex_unlock(&mgr->mutex);
        return;
    }
    
    log_debug("network: flushing layout updates (reflows: %d, repaints: %d)",
              reflow_count, repaint_count);
    
    // process reflows first (they may trigger repaints)
    // TODO: call actual layout engine here
    // for (int i = 0; i < reflows->length; i++) {
    //     DomElement* elem = (DomElement*)reflows->data[i];
    //     layout_element(elem);
    // }
    
    // clear reflow list
    if (reflows) {
        arraylist_clear(reflows);
    }
    
    // process repaints
    // TODO: call actual paint engine here
    // for (int i = 0; i < repaints->length; i++) {
    //     DomElement* elem = (DomElement*)repaints->data[i];
    //     repaint_element(elem);
    // }
    
    // clear repaint list
    if (repaints) {
        arraylist_clear(repaints);
    }
    
    pthread_mutex_unlock(&mgr->mutex);
}

// check if all resources loaded
bool resource_manager_is_fully_loaded(const NetworkResourceManager* mgr) {
    if (!mgr) return true;
    
    pthread_mutex_lock((pthread_mutex_t*)&mgr->mutex);
    bool loaded = (mgr->completed_resources + mgr->failed_resources) >= mgr->total_resources;
    pthread_mutex_unlock((pthread_mutex_t*)&mgr->mutex);
    
    return loaded;
}

// get load statistics
void resource_manager_get_stats(NetworkResourceManager* mgr, int* total, int* completed, int* failed) {
    if (!mgr) return;
    
    pthread_mutex_lock(&mgr->mutex);
    if (total) *total = mgr->total_resources;
    if (completed) *completed = mgr->completed_resources;
    if (failed) *failed = mgr->failed_resources;
    pthread_mutex_unlock(&mgr->mutex);
}

// get load progress (0.0 to 1.0)
float resource_manager_get_load_progress(const NetworkResourceManager* mgr) {
    if (!mgr) return 1.0f;
    if (mgr->total_resources == 0) return 1.0f;
    return (float)(mgr->completed_resources + mgr->failed_resources) / mgr->total_resources;
}

// check if page load has timed out (Phase 5)
bool resource_manager_check_page_timeout(NetworkResourceManager* mgr) {
    if (!mgr) return false;
    
    double elapsed_ms = (get_time_seconds() - mgr->load_start_time) * 1000.0;
    if (elapsed_ms > mgr->page_load_timeout_ms) {
        log_error("network: page load timeout exceeded (%.0f ms > %d ms)",
                  elapsed_ms, mgr->page_load_timeout_ms);
        return true;
    }
    
    return false;
}

// retry resource download with exponential backoff (Phase 5)
bool resource_manager_retry_download(NetworkResourceManager* mgr, NetworkResource* res) {
    if (!mgr || !res) return false;
    
    // check if retries exhausted
    if (res->retry_count >= res->max_retries) {
        log_error("network: max retries exceeded for %s (%d attempts)",
                  res->url, res->retry_count + 1);
        return false;
    }
    
    // exponential backoff: 1s, 2s, 4s, 8s...
    int backoff_ms = 1000 * (1 << res->retry_count);
    
    log_warn("network: retrying %s (attempt %d/%d, backoff %dms)",
             res->url, res->retry_count + 1, res->max_retries, backoff_ms);
    
    // sleep for backoff period
    struct timespec sleep_time;
    sleep_time.tv_sec = backoff_ms / 1000;
    sleep_time.tv_nsec = (backoff_ms % 1000) * 1000000;
    nanosleep(&sleep_time, NULL);
    
    // increment retry counter
    res->retry_count++;
    res->state = STATE_PENDING;
    res->start_time = get_time_seconds();
    
    // re-queue for download
    thread_pool_enqueue(mgr->thread_pool, download_task_fn, res, res->priority);
    
    return true;
}

// increment reference count
void resource_retain(NetworkResource* res) {
    if (!res) return;
    __sync_fetch_and_add(&res->ref_count, 1);
}

// decrement reference count and free if zero
void resource_release(NetworkResource* res) {
    if (!res) return;
    if (__sync_sub_and_fetch(&res->ref_count, 1) == 0) {
        free_network_resource(res);
    }
}

// cancel a specific resource download
void resource_manager_cancel(NetworkResourceManager* mgr, NetworkResource* res) {
    if (!mgr || !res) return;
    
    pthread_mutex_lock(&mgr->mutex);
    
    if (res->state == STATE_PENDING || res->state == STATE_DOWNLOADING) {
        res->state = STATE_FAILED;
        res->error_message = strdup("Cancelled");
        mgr->failed_resources++;
        
        log_debug("network: cancelled resource: %s", res->url);
    }
    
    pthread_mutex_unlock(&mgr->mutex);
}

// cancel all resources owned by a specific element
void resource_manager_cancel_for_element(NetworkResourceManager* mgr, struct DomElement* elmt) {
    if (!mgr || !elmt) return;
    
    pthread_mutex_lock(&mgr->mutex);
    
    int cancelled = 0;
    size_t iter = 0;
    void* item;
    while (hashmap_iter((struct hashmap*)mgr->resources, &iter, &item)) {
        ResourceEntry* entry = (ResourceEntry*)item;
        if (entry->res && entry->res->owner_element == elmt) {
            if (entry->res->state == STATE_PENDING || entry->res->state == STATE_DOWNLOADING) {
                entry->res->state = STATE_FAILED;
                entry->res->error_message = strdup("Owner element removed");
                mgr->failed_resources++;
                cancelled++;
            }
        }
    }
    
    if (cancelled > 0) {
        log_debug("network: cancelled %d resources for element", cancelled);
    }
    
    pthread_mutex_unlock(&mgr->mutex);
}

// get count of pending (not yet completed) resources
int resource_manager_get_pending_count(const NetworkResourceManager* mgr) {
    if (!mgr) return 0;
    
    pthread_mutex_lock((pthread_mutex_t*)&mgr->mutex);
    int pending = mgr->total_resources - mgr->completed_resources - mgr->failed_resources;
    pthread_mutex_unlock((pthread_mutex_t*)&mgr->mutex);
    
    return pending > 0 ? pending : 0;
}

// get list of failed resources
void* resource_manager_get_failed_resources(const NetworkResourceManager* mgr) {
    if (!mgr) return NULL;
    
    pthread_mutex_lock((pthread_mutex_t*)&mgr->mutex);
    
    ArrayList* failed_list = arraylist_new(mgr->failed_resources > 0 ? mgr->failed_resources : 4);
    
    size_t iter = 0;
    void* item;
    while (hashmap_iter((struct hashmap*)mgr->resources, &iter, &item)) {
        ResourceEntry* entry = (ResourceEntry*)item;
        if (entry->res && entry->res->state == STATE_FAILED) {
            arraylist_append(failed_list, entry->res);
        }
    }
    
    pthread_mutex_unlock((pthread_mutex_t*)&mgr->mutex);
    
    return failed_list;
}

// set error callback for resource failures
void resource_manager_set_error_callback(NetworkResourceManager* mgr,
                                        void (*callback)(NetworkResource*, void*),
                                        void* user_data) {
    if (!mgr) return;
    
    pthread_mutex_lock(&mgr->mutex);
    mgr->error_callback = callback;
    mgr->error_callback_data = user_data;
    pthread_mutex_unlock(&mgr->mutex);
}

}  // extern "C"
