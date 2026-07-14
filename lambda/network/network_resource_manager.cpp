// network_resource_manager.cpp
// Full implementation with hashmap-based deduplication and arraylist scheduling

#include "network_resource_manager.h"
#include "network_downloader.h"
#include "network_scheduler.h"
#include "resource_loaders.h"
#include "cookie_jar.h"
#include "../input/css/dom_element.hpp"
#include "../input/css/css_font_face.hpp"
#include "../../radiant/view.hpp"
#include "../../radiant/event.hpp"
#include "../../lib/url.h"
#include "../../lib/log.h"
#include "../../lib/hashmap.h"
#include "../../lib/time_util.h"
#include "../../lib/hashmap_helpers.h"
#include "../../lib/arraylist.h"
#include "../../lib/mem.h"
#include <string.h>
#include <time.h>

// hashmap entry for URL→NetworkResource* lookup
typedef struct {
    char* url;              // key (owned)
    NetworkResource* res;   // value (not owned - managed separately)
} ResourceEntry;

static uint64_t g_next_document_id = 1;

HASHMAP_DEFINE_STRKEY(resource_entry, ResourceEntry, url)

// free function for resource entries (only frees URL, not resource)
static void resource_entry_free(void* item) {
    ResourceEntry* entry = (ResourceEntry*)item;
    if (entry->url) mem_free(entry->url);
    // note: resource itself freed via resource_release
}

// CPP internal helpers
namespace {

// get current time in seconds (delegates to lib/time_util)
double get_time_seconds() {
    return time_now_seconds();
}

// create network resource
NetworkResource* create_network_resource(const char* url,
                                                ResourceType type,
                                                ResourcePriority priority,
                                                struct DomElement* owner) {
    NetworkResource* res = (NetworkResource*)mem_calloc(1, sizeof(NetworkResource), MEM_CAT_NETWORK);
    if (!res) return NULL;

    res->url = mem_strdup(url, MEM_CAT_NETWORK);
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

void detach_image_surface_from_tree(DomNode* node, ImageSurface* surface) {
    while (node) {
        if (node->is_element()) {
            DomElement* elem = node->as_element();
            if (elem->embed) {
                if (elem->embed->img == surface) {
                    elem->embed->img = nullptr;
                }
                if (elem->embed->poster == surface) {
                    elem->embed->poster = nullptr;
                }
            }
            detach_image_surface_from_tree(elem->first_child, surface);
        }
        node = node->next_sibling;
    }
}

// free network resource
void free_network_resource(NetworkResource* res) {
    if (!res) return;

    if (res->type == RESOURCE_IMAGE && res->image_surface) {
        // NetworkResource-owned images and UI-cache-owned SVG images share DOM
        // borrow slots, so always detach before releasing whichever owner applies.
        if (res->manager && res->manager->document) {
            DomDocument* doc = res->manager->document;
            detach_image_surface_from_tree((DomNode*)doc->root, res->image_surface);
            if (doc->view_tree && doc->view_tree->root &&
                    doc->view_tree->root != (View*)doc->root) {
                detach_image_surface_from_tree((DomNode*)doc->view_tree->root, res->image_surface);
            }
        }
        if (!res->image_surface_borrowed) {
            image_surface_destroy(res->image_surface);
        }
        res->image_surface = nullptr;
    } else if (res->type == RESOURCE_IMAGE && res->owner_element && res->owner_element->embed) {
        EmbedProp* embed = res->owner_element->embed;
        // Image resources processed by the async loader attach directly to the
        // owner element and may be detached from the final DOM/view cleanup path.
        if (embed->img && !embed->img->url) {
            image_surface_destroy(embed->img);
            embed->img = nullptr;
        }
    }

    if (res->type == RESOURCE_FONT && res->user_data) {
        // Font downloads own a transient @font-face descriptor until the main
        // thread registers the cached file path with the font resolver.
        css_font_face_descriptor_free((CssFontFaceDescriptor*)res->user_data);
        res->user_data = NULL;
    }
    
    mem_free(res->url);
    mem_free(res->local_path);
    mem_free(res->error_message);
    mem_free(res);
}

}  // namespace

static void queue_ready_resource_locked(NetworkResourceManager* mgr, NetworkResource* res) {
    if (!mgr || !res || !mgr->pending_ready) return;
    arraylist_append((ArrayList*)mgr->pending_ready, res);
}

static void queue_failed_resource_locked(NetworkResourceManager* mgr, NetworkResource* res) {
    if (!mgr || !res || !mgr->pending_failed) return;
    arraylist_append((ArrayList*)mgr->pending_failed, res);
}

static bool resource_cancel_requested(NetworkResource* res) {
    return res && atomic_load(&res->cancel_requested);
}

static void cancel_resource_locked(NetworkResourceManager* mgr,
                                   NetworkResource* res,
                                   const char* reason,
                                   bool queue_for_main) {
    if (!mgr || !res) return;

    atomic_store(&res->cancel_requested, true);

    if (res->state == STATE_PENDING || res->state == STATE_DOWNLOADING) {
        res->state = STATE_FAILED;
        if (res->error_message) mem_free(res->error_message);
        res->error_message = mem_strdup(reason ? reason : "Cancelled", MEM_CAT_NETWORK);
        res->end_time = get_time_seconds();
        if (!res->stats_counted) {
            mgr->failed_resources++;
            res->stats_counted = true;
        }
        if (queue_for_main) {
            queue_failed_resource_locked(mgr, res);
        }
    }
}

static void notify_wake_callback(NetworkResourceManager* mgr) {
    if (!mgr) return;

    pthread_mutex_lock(&mgr->mutex);
    NetworkWakeCallback callback = mgr->wake_callback;
    void* user_data = mgr->wake_callback_data;
    pthread_mutex_unlock(&mgr->mutex);

    if (callback) {
        callback(user_data);
    }
}

static void count_completed_if_needed(NetworkResourceManager* mgr, NetworkResource* res) {
    if (!mgr || !res) return;
    pthread_mutex_lock(&mgr->mutex);
    if (!res->stats_counted) {
        mgr->completed_resources++;
        res->stats_counted = true;
        log_debug("network: resource completed: %s (%d/%d)",
                  res->url, mgr->completed_resources, mgr->total_resources);
    }
    pthread_mutex_unlock(&mgr->mutex);
}

static void count_failed_if_needed(NetworkResourceManager* mgr, NetworkResource* res) {
    if (!mgr || !res) return;
    pthread_mutex_lock(&mgr->mutex);
    if (!res->stats_counted) {
        mgr->failed_resources++;
        res->stats_counted = true;
        log_debug("network: resource failed: %s (%d/%d)",
                  res->url, mgr->failed_resources, mgr->total_resources);
    }
    pthread_mutex_unlock(&mgr->mutex);
}

static void mark_processed(NetworkResource* res) {
    if (!res || !res->manager) return;
    pthread_mutex_lock(&res->manager->mutex);
    res->processed = true;
    pthread_mutex_unlock(&res->manager->mutex);
}

static bool is_processed(NetworkResource* res) {
    if (!res || !res->manager) return true;
    pthread_mutex_lock(&res->manager->mutex);
    bool processed = res->processed;
    pthread_mutex_unlock(&res->manager->mutex);
    return processed;
}

static void process_ready_resource_on_main(NetworkResourceManager* mgr, NetworkResource* res) {
    if (!mgr || !res || is_processed(res)) return;
    if (res->state != STATE_COMPLETED && res->state != STATE_CACHED) return;

    DomDocument* doc = mgr->document;
    log_debug("network: main-thread processing resource: %s", res->url);

    if (res->on_complete) {
        res->on_complete(res, res->user_data);
    } else if (doc) {
        switch (res->type) {
            case RESOURCE_HTML:
                process_html_resource(res, doc);
                break;
            case RESOURCE_CSS:
                process_css_resource(res, doc);
                break;
            case RESOURCE_IMAGE:
                if (res->owner_element) {
                    process_image_resource(res, res->owner_element);
                }
                break;
            case RESOURCE_FONT:
                if (doc->root) {
                    resource_manager_schedule_reflow(mgr, doc->root);
                }
                break;
            case RESOURCE_SVG:
                if (res->owner_element) {
                    process_svg_resource(res, res->owner_element);
                }
                break;
            case RESOURCE_SCRIPT:
                process_script_resource(res, doc);
                break;
        }
    }

    mark_processed(res);
    count_completed_if_needed(mgr, res);
}

static bool resource_failure_is_optional(NetworkResource* res) {
    if (!res) return false;
    return res->type == RESOURCE_CSS || res->type == RESOURCE_IMAGE ||
           res->type == RESOURCE_FONT || res->type == RESOURCE_SVG ||
           res->type == RESOURCE_SCRIPT;
}

static void configure_resource_timeout_and_retry(NetworkResourceManager* mgr, NetworkResource* res) {
    if (!mgr || !res) return;
    res->timeout_ms = mgr->default_timeout_ms;
    res->max_retries = 3;
    if (resource_failure_is_optional(res)) {
        // Optional browser subresources already have CSS/font/image/script
        // fallbacks; retrying each timeout can block page layout for minutes.
        res->max_retries = 0;
    }
}

static void process_failed_resource_on_main(NetworkResourceManager* mgr, NetworkResource* res) {
    if (!mgr || !res || is_processed(res)) return;
    if (res->state != STATE_FAILED) return;

    log_debug("network: main-thread handling failed resource: %s", res->url);

    if (mgr->error_callback) {
        mgr->error_callback(res, mgr->error_callback_data);
    }
    if (mgr->document) {
        handle_resource_failure(res, mgr->document);
    }

    if (resource_failure_is_optional(res)) {
        pthread_mutex_lock(&mgr->mutex);
        // Subresources on public pages can be blocked, stale, or timed out; once
        // the document itself loaded, fall back instead of failing the page.
        res->optional_failure = true;
        res->state = STATE_COMPLETED;
        pthread_mutex_unlock(&mgr->mutex);
    }

    mark_processed(res);
    if (res->optional_failure) {
        count_completed_if_needed(mgr, res);
    } else {
        count_failed_if_needed(mgr, res);
    }
}

// download completion function (called by the scheduler backend)
static void download_completion_fn(void* task_data, bool success) {
    NetworkResource* res = (NetworkResource*)task_data;
    if (!res) return;
    
    if (resource_cancel_requested(res)) {
        log_debug("network: cancelled download task finished without delivery: %s", res->url);
        return;
    }
    
    if (success) {
        bool queued_for_main = false;
        if (res->manager) {
            pthread_mutex_lock(&res->manager->mutex);
            if (res->state == STATE_DOWNLOADING || res->state == STATE_PENDING) {
                res->state = STATE_COMPLETED;
                res->end_time = get_time_seconds();
                queue_ready_resource_locked(res->manager, res);
                queued_for_main = true;
                log_debug("network: download complete: %s (%.3fs), queued for main thread",
                          res->url, res->end_time - res->start_time);
            } else {
                log_debug("network: completed download discarded because resource state changed: %s", res->url);
            }
            pthread_mutex_unlock(&res->manager->mutex);
            if (queued_for_main) {
                notify_wake_callback(res->manager);
            }
        } else {
            res->state = STATE_COMPLETED;
            res->end_time = get_time_seconds();
        }
    } else {
        // check if error is retryable
        bool should_retry = is_http_error_retryable(res->http_status_code);
        
        if (should_retry && res->retry_count < res->max_retries) {
            // retry with exponential backoff
            if (res->manager) {
                resource_manager_retry_download(res->manager, res);
            }
        } else {
            bool queued_for_main = false;
            if (res->manager) {
                pthread_mutex_lock(&res->manager->mutex);
                if (res->state == STATE_DOWNLOADING || res->state == STATE_PENDING) {
                    res->state = STATE_FAILED;
                    res->end_time = get_time_seconds();
                    queue_failed_resource_locked(res->manager, res);
                    queued_for_main = true;
                    if (resource_failure_is_optional(res)) {
                        // Missing linked subresources should degrade rendering,
                        // not surface as a hard online page-load error.
                        res->optional_failure = true;
                        log_warn("network: optional subresource unavailable: %s - %s, queued for fallback",
                                 res->url, res->error_message ? res->error_message : "unknown error");
                    } else {
                        log_error("network: download failed: %s - %s, queued for main thread",
                                  res->url, res->error_message ? res->error_message : "unknown error");
                    }
                } else {
                    log_debug("network: failed download discarded because resource state changed: %s", res->url);
                }
                pthread_mutex_unlock(&res->manager->mutex);
                if (queued_for_main) {
                    notify_wake_callback(res->manager);
                }
            } else {
                res->state = STATE_FAILED;
                res->end_time = get_time_seconds();
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
    if (!doc) return NULL;
    
    NetworkResourceManager* mgr = (NetworkResourceManager*)mem_calloc(1, sizeof(NetworkResourceManager), MEM_CAT_NETWORK);
    if (!mgr) return NULL;
    
    mgr->document = doc;
    mgr->thread_pool = pool;
    mgr->file_cache = cache;
    mgr->document_id = __sync_fetch_and_add(&g_next_document_id, 1);
    mgr->navigation_id = mgr->document_id;

    NetworkSchedulerConfig scheduler_config = {};
    scheduler_config.max_global_transfers = 8;
    scheduler_config.max_transfers_per_origin = 6;
    scheduler_config.max_cache_writes = 4;
    scheduler_config.use_curl_multi_backend = true;
    mgr->scheduler = network_scheduler_create(pool, &scheduler_config);
    if (!mgr->scheduler) {
        mem_free(mgr);
        return NULL;
    }
    if (mgr->file_cache) {
        enhanced_cache_set_max_concurrent_writes(mgr->file_cache,
                                                 scheduler_config.max_cache_writes);
    }
    
    // create hashmap for URL→NetworkResource* lookup (deduplication)
    mgr->resources = resource_entry_new_with_free(0, resource_entry_free);
    
    if (!mgr->resources) {
        network_scheduler_destroy(mgr->scheduler);
        mem_free(mgr);
        return NULL;
    }
    
    // create arraylists for main-thread delivery and pending layout work
    mgr->pending_ready = arraylist_new(16);
    mgr->pending_failed = arraylist_new(16);
    mgr->pending_reflows = arraylist_new(16);
    mgr->pending_repaints = arraylist_new(16);
    
    if (!mgr->pending_ready || !mgr->pending_failed ||
        !mgr->pending_reflows || !mgr->pending_repaints ||
        pthread_mutex_init(&mgr->mutex, NULL) != 0) {
        hashmap_free((struct hashmap*)mgr->resources);
        if (mgr->pending_ready) arraylist_free((ArrayList*)mgr->pending_ready);
        if (mgr->pending_failed) arraylist_free((ArrayList*)mgr->pending_failed);
        if (mgr->pending_reflows) arraylist_free((ArrayList*)mgr->pending_reflows);
        if (mgr->pending_repaints) arraylist_free((ArrayList*)mgr->pending_repaints);
        network_scheduler_destroy(mgr->scheduler);
        mem_free(mgr);
        return NULL;
    }
    
    mgr->load_start_time = get_time_seconds();
    mgr->total_resources = 0;
    mgr->completed_resources = 0;
    mgr->failed_resources = 0;
    
    // Phase 5: timeout configuration
    mgr->default_timeout_ms = 30000;  // 30 seconds per resource
    mgr->page_load_timeout_ms = 60000;  // 60 seconds total page load
    
    // Phase 4: cookie jar for session management
    mgr->cookie_jar = cookie_jar_create("./temp/cookies.dat");
    
    log_debug("network: created resource manager (doc=%llu, nav=%llu, timeouts: per-resource=%dms, page=%dms)",
              (unsigned long long)mgr->document_id,
              (unsigned long long)mgr->navigation_id,
              mgr->default_timeout_ms,
              mgr->page_load_timeout_ms);
    
    return mgr;
}

// destroy resource manager
void resource_manager_destroy(NetworkResourceManager* mgr) {
    if (!mgr) return;
    
    log_debug("network: destroying resource manager");

    // workers still reference NetworkResource and manager pointers; wait before
    // freeing document-owned network state.
    if (mgr->scheduler) {
        network_scheduler_wait_all(mgr->scheduler);
    }
    
    // Phase 4: destroy cookie jar (saves persistent cookies to disk)
    if (mgr->cookie_jar) {
        cookie_jar_destroy(mgr->cookie_jar);
        mgr->cookie_jar = NULL;
    }
    
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
    if (mgr->pending_ready) arraylist_free((ArrayList*)mgr->pending_ready);
    if (mgr->pending_failed) arraylist_free((ArrayList*)mgr->pending_failed);
    if (mgr->pending_reflows) arraylist_free((ArrayList*)mgr->pending_reflows);
    if (mgr->pending_repaints) arraylist_free((ArrayList*)mgr->pending_repaints);

    if (mgr->scheduler) {
        network_scheduler_destroy(mgr->scheduler);
    }
    
    pthread_mutex_destroy(&mgr->mutex);
    mem_free(mgr);
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

void resource_manager_set_wake_callback(NetworkResourceManager* mgr,
                                        NetworkWakeCallback callback,
                                        void* user_data) {
    if (!mgr) return;

    pthread_mutex_lock(&mgr->mutex);
    mgr->wake_callback = callback;
    mgr->wake_callback_data = user_data;
    pthread_mutex_unlock(&mgr->mutex);

    log_debug("network: wake callback set for resource manager");
}

// load resource (with deduplication and actual download integration)
NetworkResource* resource_manager_load(NetworkResourceManager* mgr,
                                      const char* url,
                                      ResourceType type,
                                      ResourcePriority priority,
                                      struct DomElement* owner) {
    if (!mgr || !url) return NULL;
    
    pthread_mutex_lock(&mgr->mutex);
    
    // enforce maximum resources per page (500) to prevent runaway loading
    if (mgr->total_resources >= 500) {
        log_warn("network: max resources per page (500) reached, rejecting: %s", url);
        pthread_mutex_unlock(&mgr->mutex);
        return NULL;
    }
    
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
            // Discovery callers do not own returned resources; retaining here
            // leaked duplicate URL requests until shutdown memtrack.
            pthread_mutex_unlock(&mgr->mutex);
            return res;
        }
        
        // if still downloading, just wait for it
        if (res->state == STATE_DOWNLOADING || res->state == STATE_PENDING) {
            log_debug("network: resource already loading: %s", url);
            // The manager hashmap is the owning reference; duplicate callers
            // only need the shared load to continue.
            pthread_mutex_unlock(&mgr->mutex);
            return res;
        }
        
        // if failed, might want to retry - for now, return failed resource
        if (res->state == STATE_FAILED) {
            log_debug("network: returning previously failed resource: %s", url);
            // Failed resources stay manager-owned so diagnostics remain
            // available without leaking an unbalanced caller reference.
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
                res->document_id = mgr->document_id;
                res->navigation_id = mgr->navigation_id;
                atomic_store(&res->cancel_requested, false);
                res->end_time = get_time_seconds();
                
                // add to hashmap
                ResourceEntry entry = { .url = mem_strdup(url, MEM_CAT_NETWORK), .res = res };
                hashmap_set((struct hashmap*)mgr->resources, &entry);
                
                mgr->total_resources++;
                mgr->completed_resources++;
                res->stats_counted = true;
                queue_ready_resource_locked(mgr, res);
                
                log_debug("network: cache hit for: %s -> %s, queued for main thread",
                          url, cached_path);
                
                pthread_mutex_unlock(&mgr->mutex);
                return res;
            }
            mem_free(cached_path);
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
    configure_resource_timeout_and_retry(mgr, res);
    res->document_id = mgr->document_id;
    res->navigation_id = mgr->navigation_id;
    atomic_store(&res->cancel_requested, false);
    
    // add to hashmap
    ResourceEntry entry = { .url = mem_strdup(url, MEM_CAT_NETWORK), .res = res };
    hashmap_set((struct hashmap*)mgr->resources, &entry);
    
    mgr->total_resources++;
    
    log_debug("network: loading resource: %s (type=%d, priority=%d)", url, type, priority);
    
    // queue HTTP transfer via scheduler
    res->state = STATE_DOWNLOADING;
    bool submit_failed = false;
    if (!network_scheduler_submit_download(mgr->scheduler,
                                           res,
                                           download_completion_fn,
                                           res->url,
                                           priority)) {
        res->state = STATE_FAILED;
        res->error_message = mem_strdup("Failed to submit network task", MEM_CAT_NETWORK);
        queue_failed_resource_locked(mgr, res);
        submit_failed = true;
        log_error("network: failed to submit resource load: %s", url);
    }
    
    pthread_mutex_unlock(&mgr->mutex);
    if (submit_failed) {
        notify_wake_callback(mgr);
    }
    
    return res;
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

// flush pending layout updates (called on main thread from render loop)
void resource_manager_flush_layout_updates(NetworkResourceManager* mgr) {
    if (!mgr) return;
    
    pthread_mutex_lock(&mgr->mutex);
    
    ArrayList* ready = (ArrayList*)mgr->pending_ready;
    ArrayList* failed = (ArrayList*)mgr->pending_failed;
    ArrayList* reflows = (ArrayList*)mgr->pending_reflows;
    ArrayList* repaints = (ArrayList*)mgr->pending_repaints;
    
    int ready_count = ready ? ready->length : 0;
    int failed_count = failed ? failed->length : 0;
    int reflow_count = reflows ? reflows->length : 0;
    int repaint_count = repaints ? repaints->length : 0;
    
    if (ready_count == 0 && failed_count == 0 &&
        reflow_count == 0 && repaint_count == 0) {
        pthread_mutex_unlock(&mgr->mutex);
        return;
    }
    
    log_debug("network: flushing updates (ready: %d, failed: %d, reflows: %d, repaints: %d)",
              ready_count, failed_count, reflow_count, repaint_count);

    ArrayList* ready_local = ready_count > 0 ? arraylist_new(ready_count) : NULL;
    ArrayList* failed_local = failed_count > 0 ? arraylist_new(failed_count) : NULL;

    if (ready_local && ready) {
        for (int i = 0; i < ready->length; i++) {
            arraylist_append(ready_local, ready->data[i]);
        }
        arraylist_clear(ready);
    }
    if (failed_local && failed) {
        for (int i = 0; i < failed->length; i++) {
            arraylist_append(failed_local, failed->data[i]);
        }
        arraylist_clear(failed);
    }
    
    // If any reflows are pending, do a full document reflow
    // (CSS changes typically affect the whole document)
    bool needs_reflow = reflow_count > 0;
    bool needs_repaint = repaint_count > 0;
    
    // clear lists before releasing lock (elements processed below)
    if (reflows) arraylist_clear(reflows);
    if (repaints) arraylist_clear(repaints);
    
    pthread_mutex_unlock(&mgr->mutex);

    bool processed_completions = (ready_local != NULL || failed_local != NULL);

    if (ready_local) {
        for (int i = 0; i < ready_local->length; i++) {
            process_ready_resource_on_main(mgr, (NetworkResource*)ready_local->data[i]);
        }
        arraylist_free(ready_local);
    }

    if (failed_local) {
        for (int i = 0; i < failed_local->length; i++) {
            process_failed_resource_on_main(mgr, (NetworkResource*)failed_local->data[i]);
        }
        arraylist_free(failed_local);
    }

    if (processed_completions) {
        // Resource processing may have scheduled reflow/repaint work. Drain that
        // work in the same main-thread turn so first layout after network load
        // sees newly parsed styles/images without waiting for another frame.
        resource_manager_flush_layout_updates(mgr);
    }
    
    // trigger reflow/repaint via document state (main thread safe)
    DomDocument* doc = mgr->document;
    if (doc && doc->state) {
        DocState* state = (DocState*)doc->state;
        if (needs_reflow) {
            state->needs_reflow = true;
            state->is_dirty = true;
            log_debug("network: triggered document reflow from resource completion");
        } else if (needs_repaint) {
            state->is_dirty = true;
            log_debug("network: triggered document repaint from resource completion");
        }
    }
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
    if (resource_cancel_requested(res)) return false;
    
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
    
    // Do not sleep inside scheduler/backend threads. The curl-multi backend is
    // a single network driver, so retry delay needs a scheduler timer rather
    // than blocking this callback path. For now, requeue immediately and keep
    // the computed backoff visible in diagnostics.
    if (resource_cancel_requested(res)) return false;
    
    pthread_mutex_lock(&mgr->mutex);
    if (resource_cancel_requested(res)) {
        pthread_mutex_unlock(&mgr->mutex);
        return false;
    }
    res->retry_count++;
    res->state = STATE_PENDING;
    res->start_time = get_time_seconds();
    pthread_mutex_unlock(&mgr->mutex);
    
    // re-queue for download
    if (!network_scheduler_submit_download(mgr->scheduler,
                                           res,
                                           download_completion_fn,
                                           res->url,
                                           res->priority)) {
        pthread_mutex_lock(&mgr->mutex);
        res->state = STATE_FAILED;
        if (res->error_message) mem_free(res->error_message);
        res->error_message = mem_strdup("Failed to submit retry network task", MEM_CAT_NETWORK);
        queue_failed_resource_locked(mgr, res);
        pthread_mutex_unlock(&mgr->mutex);
        notify_wake_callback(mgr);
        log_error("network: failed to submit retry for %s", res->url);
        return false;
    }
    
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
    cancel_resource_locked(mgr, res, "Cancelled", true);
    pthread_mutex_unlock(&mgr->mutex);

    network_scheduler_cancel(mgr->scheduler, res);
    notify_wake_callback(mgr);
    log_debug("network: cancelled resource: %s", res->url);
}

// cancel all resources owned by a specific element
void resource_manager_cancel_for_element(NetworkResourceManager* mgr, struct DomElement* elmt) {
    if (!mgr || !elmt) return;

    ArrayList* cancelled_resources = arraylist_new(4);
    if (!cancelled_resources) return;
    
    pthread_mutex_lock(&mgr->mutex);
    
    int cancelled = 0;
    size_t iter = 0;
    void* item;
    while (hashmap_iter((struct hashmap*)mgr->resources, &iter, &item)) {
        ResourceEntry* entry = (ResourceEntry*)item;
        if (entry->res && entry->res->owner_element == elmt) {
            if (entry->res->state == STATE_PENDING || entry->res->state == STATE_DOWNLOADING) {
                cancel_resource_locked(mgr, entry->res, "Owner element removed", true);
                arraylist_append(cancelled_resources, entry->res);
                cancelled++;
            }
        }
    }
    
    if (cancelled > 0) {
        log_debug("network: cancelled %d resources for element", cancelled);
    }
    
    pthread_mutex_unlock(&mgr->mutex);

    if (cancelled > 0) {
        for (int i = 0; i < cancelled_resources->length; i++) {
            network_scheduler_cancel(mgr->scheduler, cancelled_resources->data[i]);
        }
        notify_wake_callback(mgr);
    }

    arraylist_free(cancelled_resources);
}

// cancel all in-flight downloads (called on navigation to abort old page's resources)
void resource_manager_cancel_all(NetworkResourceManager* mgr) {
    if (!mgr) return;
    
    pthread_mutex_lock(&mgr->mutex);
    
    int cancelled = 0;
    size_t iter = 0;
    void* item;
    while (hashmap_iter((struct hashmap*)mgr->resources, &iter, &item)) {
        ResourceEntry* entry = (ResourceEntry*)item;
        if (entry->res) {
            if (entry->res->state == STATE_PENDING || entry->res->state == STATE_DOWNLOADING) {
                cancel_resource_locked(mgr, entry->res, "Navigation cancelled", false);
                cancelled++;
            }
        }
    }
    
    if (cancelled > 0) {
        log_info("network: cancelled %d in-flight resources due to navigation", cancelled);
    }
    
    pthread_mutex_unlock(&mgr->mutex);

    if (cancelled > 0) {
        network_scheduler_shutdown(mgr->scheduler);
    }
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
