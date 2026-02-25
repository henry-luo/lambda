# Radiant Network Support - Design Proposal

**Author:** GitHub Copilot  
**Date:** January 14, 2026  
**Status:** Proposal  

---

## 1. Executive Summary

This proposal outlines the architecture for adding comprehensive network support to Radiant, Lambda's HTML/CSS rendering engine. The design integrates and enhances existing Lambda input network capabilities to enable loading HTML, CSS, SVG, images, fonts, and other resources over HTTP/HTTPS with proper caching, threading, and error handling.

### Key Features
- **Asynchronous Resource Loading** - Thread pool for non-blocking downloads
- **Smart Caching** - Content-addressable storage with LRU eviction
- **Progressive Rendering** - Load and display resources as they arrive
- **Timeout & Error Handling** - Graceful degradation for missing/failed resources
- **CORS & Security** - Same-origin policy and HTTPS validation

---

## 2. Current State Analysis

### 2.1 Existing Components

#### Lambda Input HTTP (`lambda/input/input_http.cpp`)
✅ **Already Implemented:**
- libcurl integration for HTTP/HTTPS downloads
- Basic caching to `./temp/cache` directory
- SSL/TLS support with verification
- Compression (gzip, deflate)
- Response header parsing
- `http_fetch()` - Full fetch API with POST/PUT/DELETE support

⚠️ **Limitations:**
- Synchronous/blocking downloads only
- Simple hash-based cache keys (no expiration)
- No thread safety
- No dependency tracking between resources

#### File Cache (`lambda/input/input_file_cache.cpp`)
⚠️ **Partially Implemented:**
- Basic structure defined (`FileCacheManager`, `FileCacheEntry`)
- LRU eviction skeleton exists
- **Missing:** Hash table lookup, expiration logic, thread safety

#### Radiant Layout Engine (`radiant/`)
✅ **Current Architecture:**
- Synchronous layout pipeline
- Loads all resources from filesystem via `input_from_file()`
- No callback mechanism for async resource loading
- View tree updated via reflow

---

## 3. Proposed Architecture

### 3.1 System Overview

```
┌─────────────────────────────────────────────────────────────┐
│                      Radiant Document                       │
│  ┌────────────────────────────────────────────────────┐    │
│  │  DomDocument (main HTML)                           │    │
│  │    ├── Stylesheets (CSS)                           │    │
│  │    ├── Images (PNG, JPEG, SVG)                     │    │
│  │    ├── Fonts (TTF, WOFF, WOFF2)                    │    │
│  │    └── Scripts (deferred feature)                  │    │
│  └────────────────────────────────────────────────────┘    │
│           │                        ▲                         │
│           │ request resources      │ notify complete        │
│           ▼                        │                         │
│  ┌────────────────────────────────────────────────────┐    │
│  │        NetworkResourceManager                      │    │
│  │  - Resource queue & dependency tracking            │    │
│  │  - Reflow/repaint scheduling                       │    │
│  │  - Error aggregation                               │    │
│  └────────────────────────────────────────────────────┘    │
└──────────────────┬────────────────────────────▲─────────────┘
                   │                             │
                   │ download request            │ completion callback
                   ▼                             │
         ┌─────────────────────────────────────────────┐
         │      NetworkThreadPool                      │
         │  - Worker threads (4-8 threads)             │
         │  - Task queue with priority                 │
         │  - Connection pooling (libcurl multi)       │
         └────────────┬──────────────────▲─────────────┘
                      │                  │
                      │ cache miss       │ cache hit
                      ▼                  │
         ┌─────────────────────────────────────────────┐
         │      EnhancedFileCache                      │
         │  - Content-addressable storage              │
         │  - LRU eviction with size limits            │
         │  - HTTP cache headers (ETag, max-age)       │
         │  - Thread-safe hash table                   │
         └────────────┬──────────────────▲─────────────┘
                      │                  │
                      │ HTTP request     │ HTTP response
                      ▼                  │
         ┌─────────────────────────────────────────────┐
         │      Lambda Input HTTP (libcurl)            │
         │  - HTTP/HTTPS protocol handling             │
         │  - SSL verification & compression           │
         │  - Timeout & retry logic                    │
         └─────────────────────────────────────────────┘
```

### 3.2 Resource Loading Flow

```
User Opens URL
    │
    ▼
┌───────────────────────────────────────────────────────┐
│ 1. NetworkResourceManager::load_document(url)        │
│    - Create DomDocument placeholder                  │
│    - Queue main HTML download (priority: CRITICAL)   │
│    - Return document handle immediately              │
└──────────────────┬────────────────────────────────────┘
                   │
                   ▼
┌───────────────────────────────────────────────────────┐
│ 2. NetworkThreadPool downloads HTML                  │
│    - Check EnhancedFileCache first                   │
│    - Download via input_http if cache miss           │
│    - Parse HTML → DomDocument                        │
└──────────────────┬────────────────────────────────────┘
                   │
                   ▼
┌───────────────────────────────────────────────────────┐
│ 3. Discover Dependencies                             │
│    - <link rel="stylesheet">  → CSS (priority: HIGH) │
│    - <img src="...">          → Images (NORMAL)      │
│    - @font-face url()         → Fonts (HIGH)         │
│    - <svg><use xlink:href>    → SVG defs (NORMAL)    │
│    - Queue all discovered resources                  │
└──────────────────┬────────────────────────────────────┘
                   │
                   ▼
┌───────────────────────────────────────────────────────┐
│ 4. Progressive Layout & Paint                        │
│    - Layout with available resources                 │
│    - As each resource loads:                         │
│      * Reflow affected subtrees                      │
│      * Repaint affected regions                      │
│    - Mark document "fully loaded" when queue empty   │
└───────────────────────────────────────────────────────┘
```

---

## 4. Component Design

### 4.1 NetworkResourceManager

**Location:** `radiant/network_resource_manager.hpp/cpp`

**Responsibilities:**
- Central coordinator for all network resource loading
- Dependency tracking between resources (CSS blocks layout)
- Scheduling reflows and repaints
- Error aggregation and reporting

**Core Structure:**
```cpp
enum ResourcePriority {
    PRIORITY_CRITICAL = 0,  // Main HTML
    PRIORITY_HIGH     = 1,  // CSS, fonts (block rendering)
    PRIORITY_NORMAL   = 2,  // Images, SVG
    PRIORITY_LOW      = 3   // Prefetch, async scripts
};

enum ResourceState {
    STATE_PENDING,      // Queued but not started
    STATE_DOWNLOADING,  // In flight
    STATE_COMPLETED,    // Successfully loaded
    STATE_FAILED,       // Download failed (timeout/404/etc)
    STATE_CACHED        // Served from cache
};

struct NetworkResource {
    char* url;                  // Absolute URL
    char* local_path;           // Cache file path (if cached)
    ResourceType type;          // HTML, CSS, IMAGE, FONT, SVG
    ResourceState state;
    ResourcePriority priority;
    
    // Dependency tracking
    DomElement* owner_element;  // Element that requested this (<link>, <img>)
    List* dependents;           // Resources blocked by this one
    
    // Timing & error info
    double start_time;
    double end_time;
    int http_status_code;
    char* error_message;
    
    // Callback on completion
    void (*on_complete)(NetworkResource* res, void* user_data);
    void* user_data;
};

class NetworkResourceManager {
public:
    // Lifecycle
    NetworkResourceManager(DomDocument* doc, NetworkThreadPool* pool, 
                          EnhancedFileCache* cache);
    ~NetworkResourceManager();
    
    // Resource loading
    NetworkResource* load_resource(const char* url, ResourceType type, 
                                   ResourcePriority priority,
                                   DomElement* owner = nullptr);
    
    void cancel_resource(NetworkResource* res);
    void cancel_all_for_element(DomElement* elmt);
    
    // Status queries
    bool is_fully_loaded() const;
    int get_pending_count() const;
    float get_load_progress() const;  // 0.0 - 1.0
    
    // Reflow scheduling (batches updates)
    void schedule_reflow(DomElement* root);
    void schedule_repaint(DomElement* element);
    void flush_layout_updates();  // Called on main thread
    
    // Error handling
    List* get_failed_resources() const;
    void set_error_callback(void (*cb)(NetworkResource*, void*), void* data);

private:
    DomDocument* document_;
    NetworkThreadPool* thread_pool_;
    EnhancedFileCache* file_cache_;
    
    HashMap* resources_;          // URL → NetworkResource*
    List* pending_reflows_;       // Elements needing reflow
    List* pending_repaints_;      // Elements needing repaint
    
    pthread_mutex_t mutex_;       // Protects resource list
    
    void on_resource_complete_(NetworkResource* res);
    void propagate_completion_(NetworkResource* res);
};
```

**Key Methods:**

#### `load_resource()`
```cpp
NetworkResource* NetworkResourceManager::load_resource(
    const char* url, ResourceType type, ResourcePriority priority,
    DomElement* owner)
{
    // 1. Resolve relative URLs against document base
    Url* abs_url = resolve_url(document_->base_url, url);
    
    // 2. Check if already loading/loaded
    NetworkResource* existing = hashmap_get(resources_, abs_url->href);
    if (existing) {
        if (existing->state == STATE_COMPLETED) {
            return existing;  // Instant return
        }
        // Add to dependents if different owner
        if (owner && owner != existing->owner_element) {
            arraylist_add(existing->dependents, owner);
        }
        return existing;
    }
    
    // 3. Create new resource entry
    NetworkResource* res = (NetworkResource*)calloc(1, sizeof(NetworkResource));
    res->url = strdup(abs_url->href);
    res->type = type;
    res->priority = priority;
    res->owner_element = owner;
    res->state = STATE_PENDING;
    res->dependents = arraylist_create();
    res->start_time = get_time_seconds();
    
    hashmap_put(resources_, res->url, res);
    
    // 4. Check cache first (synchronous, fast)
    char* cached_path = file_cache_lookup(file_cache_, res->url);
    if (cached_path) {
        res->local_path = cached_path;
        res->state = STATE_CACHED;
        log_debug("network: cache hit for %s", res->url);
        
        // Process immediately on main thread (no async needed)
        process_cached_resource(res);
        return res;
    }
    
    // 5. Queue for download
    res->state = STATE_DOWNLOADING;
    log_debug("network: queuing download for %s (priority %d)", res->url, priority);
    
    thread_pool_enqueue(thread_pool_, download_task, res, priority);
    
    return res;
}
```

#### `schedule_reflow()` / `schedule_repaint()`
Batches updates to avoid redundant layout passes:
```cpp
void NetworkResourceManager::schedule_reflow(DomElement* root) {
    pthread_mutex_lock(&mutex_);
    
    // Deduplicate: if ancestor already queued, skip
    for (DomElement* queued : pending_reflows_) {
        if (dom_element_is_ancestor(queued, root)) {
            pthread_mutex_unlock(&mutex_);
            return;  // Already covered
        }
    }
    
    // Remove descendants (this root supersedes them)
    arraylist_remove_if(pending_reflows_, [root](DomElement* e) {
        return dom_element_is_ancestor(root, e);
    });
    
    arraylist_add(pending_reflows_, root);
    pthread_mutex_unlock(&mutex_);
}

void NetworkResourceManager::flush_layout_updates() {
    // MUST be called on main thread
    pthread_mutex_lock(&mutex_);
    
    // Process reflows first (may trigger more repaints)
    for (DomElement* root : pending_reflows_) {
        log_debug("network: reflowing %s", dom_element_tag_name(root));
        layout_element_subtree(root);  // Calls layout_block() recursively
    }
    arraylist_clear(pending_reflows_);
    
    // Then repaints
    for (DomElement* elmt : pending_repaints_) {
        log_debug("network: repainting %s", dom_element_tag_name(elmt));
        mark_view_dirty(elmt->view);  // Trigger render on next frame
    }
    arraylist_clear(pending_repaints_);
    
    pthread_mutex_unlock(&mutex_);
}
```

---

### 4.2 NetworkThreadPool

**Location:** `radiant/network_thread_pool.hpp/cpp`

**Responsibilities:**
- Manage worker threads (4-8 threads typical)
- Priority queue for download tasks
- Connection pooling via libcurl multi interface
- Graceful shutdown

**Core Structure:**
```cpp
typedef struct DownloadTask {
    NetworkResource* resource;
    ResourcePriority priority;
    void (*callback)(NetworkResource*, void*);
    void* user_data;
    double enqueue_time;
} DownloadTask;

class NetworkThreadPool {
public:
    NetworkThreadPool(int num_threads = 4);
    ~NetworkThreadPool();
    
    // Task management
    void enqueue(void (*task_fn)(void*), void* task_data, ResourcePriority priority);
    void wait_all();      // Block until all tasks complete
    void shutdown();      // Stop accepting new tasks
    
    // Statistics
    int get_active_count() const;
    int get_queued_count() const;
    
private:
    int num_threads_;
    pthread_t* threads_;
    
    PriorityQueue* task_queue_;    // Sorted by priority
    pthread_mutex_t queue_mutex_;
    pthread_cond_t queue_cond_;
    
    bool shutdown_flag_;
    
    static void* worker_thread_func_(void* arg);
    void process_download_task_(DownloadTask* task);
};
```

**Thread Function:**
```cpp
void* NetworkThreadPool::worker_thread_func_(void* arg) {
    NetworkThreadPool* pool = (NetworkThreadPool*)arg;
    
    while (true) {
        pthread_mutex_lock(&pool->queue_mutex_);
        
        // Wait for tasks or shutdown
        while (priority_queue_is_empty(pool->task_queue_) && 
               !pool->shutdown_flag_) {
            pthread_cond_wait(&pool->queue_cond_, &pool->queue_mutex_);
        }
        
        if (pool->shutdown_flag_ && priority_queue_is_empty(pool->task_queue_)) {
            pthread_mutex_unlock(&pool->queue_mutex_);
            break;  // Exit thread
        }
        
        // Dequeue highest priority task
        DownloadTask* task = (DownloadTask*)priority_queue_pop(pool->task_queue_);
        pthread_mutex_unlock(&pool->queue_mutex_);
        
        if (task) {
            pool->process_download_task_(task);
            free(task);
        }
    }
    
    return NULL;
}

void NetworkThreadPool::process_download_task_(DownloadTask* task) {
    NetworkResource* res = task->resource;
    
    log_debug("network: downloading %s on thread %lu", 
              res->url, pthread_self());
    
    // Download via Lambda input HTTP module
    size_t content_size;
    char* content = download_http_content(res->url, &content_size, NULL);
    
    if (content) {
        // Save to cache
        char* cache_path = file_cache_store(cache_, res->url, content, content_size);
        res->local_path = cache_path;
        res->state = STATE_COMPLETED;
        res->end_time = get_time_seconds();
        
        log_debug("network: downloaded %zu bytes for %s", content_size, res->url);
        
        free(content);  // Cache has its own copy
    } else {
        res->state = STATE_FAILED;
        res->error_message = strdup("Download failed");
        log_error("network: failed to download %s", res->url);
    }
    
    // Invoke callback (thread-safe)
    if (task->callback) {
        task->callback(res, task->user_data);
    }
}
```

**Priority Queue Implementation:**
Uses min-heap with priority as key. Lower priority number = higher urgency.

---

### 4.3 EnhancedFileCache

**Location:** `radiant/enhanced_file_cache.hpp/cpp`

Enhances existing `input_file_cache.cpp` with:
- Thread-safe hash table lookup
- HTTP cache header support (ETag, Cache-Control, max-age)
- LRU eviction with configurable size limits
- Content-addressable storage (SHA-256 keys)

**Core Structure:**
```cpp
struct CacheMetadata {
    char* url;                  // Original URL
    char* etag;                 // HTTP ETag header
    time_t expires;             // Expiration timestamp
    time_t last_modified;       // HTTP Last-Modified
    size_t content_size;
    time_t last_accessed;
};

class EnhancedFileCache {
public:
    EnhancedFileCache(const char* cache_dir, 
                     size_t max_size_bytes = 100 * 1024 * 1024,  // 100 MB
                     int max_entries = 1000);
    ~EnhancedFileCache();
    
    // Lookup (thread-safe)
    char* lookup(const char* url);  // Returns cache file path or NULL
    
    // Store (thread-safe)
    char* store(const char* url, const char* content, size_t size,
                const HttpCacheHeaders* headers = nullptr);
    
    // Eviction
    void evict_lru();         // Remove least recently used
    void evict_expired();     // Remove expired entries
    void clear();             // Remove all entries
    
    // Statistics
    size_t get_size() const;
    int get_entry_count() const;
    float get_hit_rate() const;
    
private:
    char* cache_dir_;
    size_t max_size_;
    int max_entries_;
    
    HashMap* metadata_;       // URL → CacheMetadata*
    DoublyLinkedList* lru_;   // LRU list (head = most recent)
    
    pthread_rwlock_t rwlock_; // Read-write lock for thread safety
    
    size_t current_size_;
    int hit_count_;
    int miss_count_;
    
    char* compute_cache_filename_(const char* url);
    void update_lru_(const char* url);
    void ensure_capacity_(size_t needed_size);
};
```

**SHA-256 Cache Keys:**
```cpp
char* EnhancedFileCache::compute_cache_filename_(const char* url) {
    // Use SHA-256 for collision-free cache keys
    unsigned char hash[32];
    sha256((unsigned char*)url, strlen(url), hash);
    
    // Convert to hex string
    char hex[65];
    for (int i = 0; i < 32; i++) {
        sprintf(&hex[i*2], "%02x", hash[i]);
    }
    
    // Store in subdirectories (first 2 chars) for filesystem performance
    // e.g., cache/a3/a3f2e1b4c5...
    char* path = (char*)malloc(strlen(cache_dir_) + 80);
    sprintf(path, "%s/%c%c/%s.cache", cache_dir_, hex[0], hex[1], hex);
    
    return path;
}
```

**HTTP Cache Headers Support:**
```cpp
struct HttpCacheHeaders {
    char* etag;
    time_t max_age;           // Seconds from now
    time_t expires;           // Absolute timestamp
    char* last_modified;
    bool no_cache;
    bool no_store;
};

char* EnhancedFileCache::store(const char* url, const char* content, size_t size,
                                const HttpCacheHeaders* headers) {
    pthread_rwlock_wrlock(&rwlock_);
    
    // Compute cache path
    char* cache_path = compute_cache_filename_(url);
    
    // Create subdirectory if needed
    char* dir = dirname_copy(cache_path);
    create_dir_recursive(dir);
    free(dir);
    
    // Write content to file
    FILE* f = fopen(cache_path, "wb");
    if (!f) {
        pthread_rwlock_unlock(&rwlock_);
        return NULL;
    }
    fwrite(content, 1, size, f);
    fclose(f);
    
    // Store metadata
    CacheMetadata* meta = (CacheMetadata*)calloc(1, sizeof(CacheMetadata));
    meta->url = strdup(url);
    meta->content_size = size;
    meta->last_accessed = time(NULL);
    
    if (headers) {
        meta->etag = headers->etag ? strdup(headers->etag) : NULL;
        
        if (headers->max_age > 0) {
            meta->expires = time(NULL) + headers->max_age;
        } else if (headers->expires > 0) {
            meta->expires = headers->expires;
        } else {
            meta->expires = time(NULL) + 86400;  // Default 24 hours
        }
        
        // Don't cache if no-store
        if (headers->no_store) {
            free(meta);
            pthread_rwlock_unlock(&rwlock_);
            return cache_path;
        }
    }
    
    hashmap_put(metadata_, url, meta);
    doubly_list_push_front(lru_, meta);
    
    current_size_ += size;
    
    // Evict if over capacity
    ensure_capacity_(0);
    
    pthread_rwlock_unlock(&rwlock_);
    
    log_debug("cache: stored %zu bytes for %s at %s", size, url, cache_path);
    
    return cache_path;
}
```

---

### 4.4 Resource Type Handlers

**Location:** `radiant/resource_loaders.hpp/cpp`

Type-specific processing for loaded resources:

```cpp
// CSS Handler
void process_css_resource(NetworkResource* res, DomDocument* doc) {
    if (res->state != STATE_COMPLETED) return;
    
    // Parse CSS from file
    CssStylesheet* sheet = css_parse_stylesheet_from_file(res->local_path);
    if (!sheet) {
        log_error("network: failed to parse CSS %s", res->url);
        return;
    }
    
    // Append to document stylesheets
    arraylist_add(doc->stylesheets, sheet);
    
    // Recompute styles and reflow
    DomElement* root = doc->root_element;
    resolve_css_styles(root);  // Recompute all styles
    
    // Schedule full reflow (CSS affects entire tree)
    NetworkResourceManager* mgr = doc->resource_manager;
    mgr->schedule_reflow(root);
}

// Image Handler
void process_image_resource(NetworkResource* res, DomElement* img_element) {
    if (res->state != STATE_COMPLETED) return;
    
    // Load image data
    ImageData* img = image_load_from_file(res->local_path);
    if (!img) {
        log_error("network: failed to load image %s", res->url);
        
        // Show alt text or broken image indicator
        img_element->cached_image = NULL;
        return;
    }
    
    // Cache in element
    img_element->cached_image = img;
    
    // Trigger reflow if image has intrinsic dimensions
    NetworkResourceManager* mgr = img_element->doc->resource_manager;
    mgr->schedule_reflow(img_element);
}

// Font Handler
void process_font_resource(NetworkResource* res, FontFaceRule* font_face) {
    if (res->state != STATE_COMPLETED) return;
    
    // Register font with FreeType
    FT_Face face;
    if (FT_New_Face(ft_library, res->local_path, 0, &face) != 0) {
        log_error("network: failed to load font %s", res->url);
        return;
    }
    
    // Add to font manager
    font_manager_register_face(font_face->family, face);
    
    // Reflow all text using this font
    // (detected via font-family matching)
    DomDocument* doc = font_face->stylesheet->document;
    NetworkResourceManager* mgr = doc->resource_manager;
    mgr->schedule_reflow(doc->root_element);
}

// SVG Handler (for <use xlink:href="external.svg#id">)
void process_svg_resource(NetworkResource* res, DomElement* use_element) {
    if (res->state != STATE_COMPLETED) return;
    
    // Parse external SVG document
    Input* svg_input = input_from_file(res->local_path, "svg", NULL);
    if (!svg_input || !svg_input->root) {
        log_error("network: failed to parse SVG %s", res->url);
        return;
    }
    
    // Extract referenced element by ID
    const char* fragment = strrchr(res->url, '#');
    if (fragment) {
        DomElement* target = dom_find_element_by_id(svg_input->root, fragment + 1);
        if (target) {
            // Clone into <use> shadow tree
            clone_svg_subtree(use_element, target);
            
            // Reflow <use> element
            NetworkResourceManager* mgr = use_element->doc->resource_manager;
            mgr->schedule_reflow(use_element);
        }
    }
}
```

---

## 5. Integration with Radiant Layout

### 5.1 Modified Layout Pipeline

**Entry Point:** `radiant/layout_document.cpp`

```cpp
// Before: Synchronous layout
void layout_document(DomDocument* doc) {
    resolve_css_styles(doc->root_element);
    layout_block(doc->root_element);
}

// After: Async-aware layout
void layout_document_async(DomDocument* doc, const char* url) {
    // 1. Initialize network support
    doc->resource_manager = new NetworkResourceManager(doc, thread_pool, file_cache);
    
    // 2. Load main HTML
    NetworkResource* html = doc->resource_manager->load_resource(
        url, RESOURCE_HTML, PRIORITY_CRITICAL);
    
    // 3. Initial layout (may be incomplete)
    if (html->state == STATE_COMPLETED || html->state == STATE_CACHED) {
        resolve_css_styles(doc->root_element);
        layout_block(doc->root_element);
    }
    
    // 4. Discover linked resources
    discover_document_resources(doc);
    
    // 5. Layout updates happen via callbacks as resources load
    // (NetworkResourceManager::flush_layout_updates() called each frame)
}

void discover_document_resources(DomDocument* doc) {
    NetworkResourceManager* mgr = doc->resource_manager;
    
    // Find all <link rel="stylesheet">
    List* links = dom_query_selector_all(doc->root_element, "link[rel=stylesheet]");
    for (DomElement* link : links) {
        const char* href = dom_element_get_attribute(link, "href");
        if (href) {
            NetworkResource* css = mgr->load_resource(
                href, RESOURCE_CSS, PRIORITY_HIGH, link);
            
            css->on_complete = [](NetworkResource* res, void* data) {
                process_css_resource(res, (DomDocument*)data);
            };
            css->user_data = doc;
        }
    }
    
    // Find all <img>
    List* images = dom_query_selector_all(doc->root_element, "img");
    for (DomElement* img : images) {
        const char* src = dom_element_get_attribute(img, "src");
        if (src) {
            NetworkResource* image = mgr->load_resource(
                src, RESOURCE_IMAGE, PRIORITY_NORMAL, img);
            
            image->on_complete = [](NetworkResource* res, void* data) {
                process_image_resource(res, (DomElement*)data);
            };
            image->user_data = img;
        }
    }
    
    // Find @font-face in stylesheets
    for (CssStylesheet* sheet : doc->stylesheets) {
        for (FontFaceRule* font_face : sheet->font_faces) {
            if (font_face->src_url) {
                NetworkResource* font = mgr->load_resource(
                    font_face->src_url, RESOURCE_FONT, PRIORITY_HIGH);
                
                font->on_complete = [](NetworkResource* res, void* data) {
                    process_font_resource(res, (FontFaceRule*)data);
                };
                font->user_data = font_face;
            }
        }
    }
    
    // Find SVG <use xlink:href="external.svg#id">
    List* uses = dom_query_selector_all(doc->root_element, "use[*|href]");
    for (DomElement* use : uses) {
        const char* href = dom_element_get_attribute_ns(use, XMLNS_XLINK, "href");
        if (href && strchr(href, '#') && strncmp(href, "#", 1) != 0) {
            // External reference
            NetworkResource* svg = mgr->load_resource(
                href, RESOURCE_SVG, PRIORITY_NORMAL, use);
            
            svg->on_complete = [](NetworkResource* res, void* data) {
                process_svg_resource(res, (DomElement*)data);
            };
            svg->user_data = use;
        }
    }
}
```

### 5.2 Main Loop Integration

**Modified:** `radiant/view_window.cpp` (GLFW event loop)

```cpp
void render_loop(ViewWindow* window) {
    DomDocument* doc = window->document;
    
    while (!glfwWindowShouldClose(window->glfw_window)) {
        // 1. Process network callbacks (MAIN THREAD ONLY)
        if (doc->resource_manager) {
            doc->resource_manager->flush_layout_updates();
        }
        
        // 2. Handle user input
        glfwPollEvents();
        
        // 3. Layout if needed (reflows triggered by resource loads)
        if (doc->needs_layout) {
            layout_document(doc);
            doc->needs_layout = false;
        }
        
        // 4. Render
        if (doc->needs_paint) {
            render_document(doc);
            doc->needs_paint = false;
        }
        
        // 5. Show loading indicator if not fully loaded
        if (!doc->resource_manager->is_fully_loaded()) {
            float progress = doc->resource_manager->get_load_progress();
            draw_loading_spinner(progress);
        }
        
        glfwSwapBuffers(window->glfw_window);
    }
}
```

---

## 6. Timeout & Error Handling

### 6.1 Timeout Configuration

**Per-Resource Timeout:**
```cpp
struct TimeoutConfig {
    int connect_timeout_ms;   // TCP connection (default: 5000)
    int download_timeout_ms;  // Per-resource download (default: 30000)
    int total_timeout_ms;     // Total page load (default: 60000)
};

// Applied in download_task_fn()
void NetworkThreadPool::process_download_task_(DownloadTask* task) {
    NetworkResource* res = task->resource;
    
    // Set libcurl timeouts
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 30000);
    
    // Check total page load timeout
    double elapsed = get_time_seconds() - doc->load_start_time;
    if (elapsed > 60.0) {
        log_warn("network: total page load timeout exceeded, cancelling %s", res->url);
        res->state = STATE_FAILED;
        res->error_message = strdup("Page load timeout");
        return;
    }
    
    // ... perform download ...
}
```

### 6.2 Error Handling Strategies

**CSS Failure:**
- **Behavior:** Document still loads without styling
- **Fallback:** Use browser default styles
- **User Feedback:** Log warning, show in developer console

**Image Failure:**
- **Behavior:** Show alt text or broken image icon
- **Fallback:** Placeholder dimensions (width/height attributes)
- **User Feedback:** Dashed border around failed image

**Font Failure:**
- **Behavior:** Fallback to next font in font-family stack
- **Fallback Chain:** Specified fonts → Generic family (serif, sans-serif)
- **User Feedback:** Silent (standard browser behavior)

**HTML Failure:**
- **Behavior:** Cannot load document
- **Fallback:** Show error page with retry button
- **User Feedback:** Full-screen error message

**Implementation:**
```cpp
void handle_resource_failure(NetworkResource* res, DomDocument* doc) {
    switch (res->type) {
        case RESOURCE_HTML:
            show_error_page(doc, "Failed to load page", res->error_message);
            break;
            
        case RESOURCE_CSS:
            log_warn("network: CSS failed: %s (%s)", res->url, res->error_message);
            // Continue without this stylesheet
            break;
            
        case RESOURCE_IMAGE:
            if (res->owner_element) {
                DomElement* img = res->owner_element;
                img->cached_image = get_broken_image_placeholder();
                
                // Show alt text
                const char* alt = dom_element_get_attribute(img, "alt");
                if (alt) {
                    create_alt_text_overlay(img, alt);
                }
                
                doc->resource_manager->schedule_repaint(img);
            }
            break;
            
        case RESOURCE_FONT:
            // Already handled by font-family fallback chain
            log_warn("network: Font failed: %s (%s)", res->url, res->error_message);
            break;
            
        case RESOURCE_SVG:
            log_warn("network: SVG failed: %s (%s)", res->url, res->error_message);
            // <use> element remains empty
            break;
    }
}
```

### 6.3 Retry Logic

**Automatic Retry:** For transient errors (timeouts, 5xx errors)
```cpp
#define MAX_RETRIES 3
#define RETRY_DELAY_MS 1000

void NetworkThreadPool::process_download_task_(DownloadTask* task) {
    NetworkResource* res = task->resource;
    
    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        if (attempt > 0) {
            log_debug("network: retry %d/%d for %s", attempt, MAX_RETRIES, res->url);
            usleep(RETRY_DELAY_MS * 1000 * attempt);  // Exponential backoff
        }
        
        char* content = download_http_content(res->url, &res->content_size, NULL);
        
        if (content) {
            // Success!
            process_successful_download(res, content);
            return;
        }
        
        // Check if error is retryable
        if (res->http_status_code >= 400 && res->http_status_code < 500) {
            // Client error (4xx) - don't retry
            break;
        }
    }
    
    // All retries failed
    res->state = STATE_FAILED;
    handle_resource_failure(res, doc);
}
```

---

## 7. Security Considerations

### 7.1 Same-Origin Policy

**CORS Implementation:**
```cpp
bool check_cors_policy(const char* resource_url, const char* document_url) {
    Url* res_url = url_parse(resource_url);
    Url* doc_url = url_parse(document_url);
    
    // Check same origin
    bool same_origin = (strcmp(res_url->scheme, doc_url->scheme) == 0 &&
                       strcmp(res_url->host, doc_url->host) == 0 &&
                       res_url->port == doc_url->port);
    
    if (same_origin) {
        return true;  // Always allow same origin
    }
    
    // Check CORS headers (Access-Control-Allow-Origin)
    // This requires storing response headers in NetworkResource
    const char* allow_origin = find_response_header(res, "Access-Control-Allow-Origin");
    if (allow_origin) {
        if (strcmp(allow_origin, "*") == 0) {
            return true;  // Public resource
        }
        if (strcmp(allow_origin, doc_url->origin) == 0) {
            return true;  // Explicitly allowed
        }
    }
    
    log_error("network: CORS policy violation: %s from %s", 
              resource_url, document_url);
    return false;
}
```

**Apply before download:**
```cpp
NetworkResource* NetworkResourceManager::load_resource(...) {
    // ... create resource entry ...
    
    // Check CORS for cross-origin requests
    if (!check_cors_policy(abs_url->href, document_->base_url->href)) {
        res->state = STATE_FAILED;
        res->error_message = strdup("CORS policy blocked request");
        return res;
    }
    
    // ... proceed with download ...
}
```

### 7.2 HTTPS Verification

**Always verify SSL certificates by default:**
```cpp
HttpConfig secure_config = {
    .verify_ssl = true,        // Enforce SSL verification
    .enable_compression = true,
    .timeout_seconds = 30,
    .max_redirects = 5,
    .user_agent = "Radiant/1.0"
};

// Allow override for development only (with warning)
if (getenv("RADIANT_INSECURE_SSL")) {
    log_warn("network: SSL verification DISABLED (insecure!)");
    secure_config.verify_ssl = false;
}
```

### 7.3 Content Security Policy (CSP)

**Future Enhancement:** Parse `Content-Security-Policy` HTTP header
- Block inline scripts/styles if CSP disallows
- Restrict image/font sources to allowed domains
- Log CSP violations

---

## 8. API Reference

### 8.1 Public C++ API

**High-Level API (recommended):**
```cpp
// Load document from URL
DomDocument* radiant_load_url(const char* url, 
                              const NetworkConfig* config = nullptr);

// Check load status
bool radiant_document_is_loaded(DomDocument* doc);
float radiant_document_get_load_progress(DomDocument* doc);

// Force resource reload (bypass cache)
void radiant_reload_resource(DomDocument* doc, const char* url);

// Configure network behavior
struct NetworkConfig {
    int thread_pool_size;           // Default: 4
    size_t cache_max_size;          // Default: 100 MB
    int cache_max_entries;          // Default: 1000
    const char* cache_directory;    // Default: "./temp/radiant_cache"
    int connect_timeout_ms;
    int download_timeout_ms;
    bool enable_cors;               // Default: true
    bool verify_ssl;                // Default: true
};
```

### 8.2 Lambda Script API

**Expose to Lambda scripts:**
```lambda
// Load URL and return Document element
doc = load_url("https://example.com/page.html")

// Query load status
is_loaded = doc.is_loaded()
progress = doc.load_progress()  // 0.0 - 1.0

// Get failed resources
failed = doc.failed_resources()
// => [{ url: "https://...", error: "timeout", code: 0 }]

// Register load callbacks
doc.on_load = fn() {
    print("Document fully loaded!")
}

doc.on_error = fn(resource) {
    print("Failed to load: " + resource.url)
}
```

---

## 9. Implementation Plan

### Phase 1: Foundation (Week 1-2)
- [ ] Implement `EnhancedFileCache` with thread-safe hash table
- [ ] Complete `input_file_cache.cpp` (LRU eviction, expiration)
- [ ] Add HTTP cache header parsing to `input_http.cpp`
- [ ] Unit tests for cache operations

### Phase 2: Threading (Week 3-4)
- [ ] Implement `NetworkThreadPool` with priority queue
- [ ] Add `PriorityQueue` data structure to `lib/`
- [ ] Integrate libcurl multi interface for connection pooling
- [ ] Stress test with 100+ concurrent downloads

### Phase 3: Resource Manager (Week 5-6)
- [ ] Implement `NetworkResourceManager` core
- [ ] Add resource dependency tracking
- [ ] Implement reflow/repaint batching
- [ ] Resource type handlers (CSS, images, fonts, SVG)

### Phase 4: Radiant Integration (Week 7-8)
- [ ] Modify `layout_document()` for async loading
- [ ] Add `discover_document_resources()` function
- [ ] Integrate `flush_layout_updates()` into render loop
- [ ] Test progressive rendering

### Phase 5: Error Handling (Week 9-10)
- [ ] Timeout implementation (per-resource, total page)
- [ ] Retry logic with exponential backoff
- [ ] Error UI (broken image icons, error pages)
- [ ] Comprehensive error logging

### Phase 6: Security (Week 11)
- [ ] CORS policy enforcement
- [ ] HTTPS certificate verification
- [ ] Same-origin checks
- [ ] Security audit

### Phase 7: Testing & Optimization (Week 12)
- [ ] Integration tests with real websites
- [ ] Performance benchmarking
- [ ] Memory leak detection (Valgrind)
- [ ] Documentation and examples

---

## 10. Testing Strategy

### 10.1 Unit Tests

**Cache Tests (`test/test_file_cache_gtest.cpp`):**
- SHA-256 key generation
- LRU eviction under memory pressure
- Thread safety (concurrent reads/writes)
- Cache hit/miss statistics
- Expiration handling

**Thread Pool Tests (`test/test_thread_pool_gtest.cpp`):**
- Priority ordering (HIGH tasks before LOW)
- Graceful shutdown
- Task cancellation
- Worker thread lifecycle

**Resource Manager Tests (`test/test_resource_manager_gtest.cpp`):**
- Dependency tracking
- Duplicate request deduplication
- Reflow batching
- Progress reporting

### 10.2 Integration Tests

**Network Loading Tests (`test/test_network_loading.cpp`):**
```cpp
TEST(NetworkLoading, LoadSimplePage) {
    DomDocument* doc = radiant_load_url("http://localhost:8000/test.html");
    
    // Wait for full load
    while (!radiant_document_is_loaded(doc)) {
        sleep_ms(100);
    }
    
    EXPECT_EQ(doc->resource_manager->get_pending_count(), 0);
    EXPECT_GT(arraylist_size(doc->stylesheets), 0);  // CSS loaded
    
    // Check image loaded
    DomElement* img = dom_query_selector(doc->root_element, "img");
    EXPECT_NE(img->cached_image, nullptr);
}

TEST(NetworkLoading, HandleTimeout) {
    // Server delays response by 35 seconds (exceeds 30s timeout)
    DomDocument* doc = radiant_load_url("http://localhost:8000/slow.html");
    
    sleep_seconds(35);
    
    List* failed = doc->resource_manager->get_failed_resources();
    EXPECT_GT(arraylist_size(failed), 0);
    
    NetworkResource* res = (NetworkResource*)arraylist_get(failed, 0);
    EXPECT_EQ(res->state, STATE_FAILED);
    EXPECT_STREQ(res->error_message, "timeout");
}
```

**CORS Tests:**
```cpp
TEST(CORS, AllowSameOrigin) {
    DomDocument* doc = radiant_load_url("http://localhost:8000/page.html");
    // page.html loads image from http://localhost:8000/image.png
    // Should succeed (same origin)
    
    wait_for_load(doc);
    EXPECT_EQ(get_failed_count(doc), 0);
}

TEST(CORS, BlockCrossOrigin) {
    DomDocument* doc = radiant_load_url("http://localhost:8000/page.html");
    // page.html loads image from http://other-domain.com/image.png
    // Should fail (no CORS header)
    
    wait_for_load(doc);
    EXPECT_GT(get_failed_count(doc), 0);
}
```

### 10.3 Performance Benchmarks

**Metrics to track:**
- Page load time (first paint, fully loaded)
- Memory usage (peak, average)
- Cache hit rate
- Thread utilization
- Network bandwidth usage

**Test Suite:**
```bash
make test-network              # All network tests
make test-network-perf         # Performance benchmarks
make test-network-stress       # 1000+ concurrent downloads
```

---

## 11. Performance Optimization

### 11.1 HTTP/2 Support

**Future Enhancement:** Upgrade to HTTP/2 for multiplexing
- Single TCP connection for all resources
- Request prioritization
- Server push support

**libcurl Configuration:**
```cpp
curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
```

### 11.2 Preloading & Prefetching

**Support `<link rel="preload">` and `<link rel="prefetch">`:**
```cpp
void discover_preload_hints(DomDocument* doc) {
    List* preloads = dom_query_selector_all(doc->root_element, "link[rel=preload]");
    
    for (DomElement* link : preloads) {
        const char* href = dom_element_get_attribute(link, "href");
        const char* as = dom_element_get_attribute(link, "as");
        
        ResourceType type = parse_resource_type(as);
        ResourcePriority priority = PRIORITY_HIGH;
        
        // Queue for download (before layout discovers it)
        doc->resource_manager->load_resource(href, type, priority);
    }
}
```

### 11.3 Lazy Loading

**Support `loading="lazy"` on images:**
```cpp
void discover_images(DomDocument* doc) {
    List* images = dom_query_selector_all(doc->root_element, "img");
    
    for (DomElement* img : images) {
        const char* loading = dom_element_get_attribute(img, "loading");
        
        if (loading && strcmp(loading, "lazy") == 0) {
            // Only load when scrolled into viewport
            register_lazy_image(doc, img);
        } else {
            // Load immediately
            const char* src = dom_element_get_attribute(img, "src");
            doc->resource_manager->load_resource(src, RESOURCE_IMAGE, PRIORITY_NORMAL, img);
        }
    }
}
```

---

## 12. Open Questions & Design Decisions

### 12.1 Current Implementation Scope

**data: URLs and blob: URLs**
- `URL_SCHEME_DATA` already defined in `lib/url.h` but not implemented in input system
- These are **loader/decoder concerns**, not network concerns (no HTTP requests needed)
- data: URLs contain embedded content (base64-encoded images, inline SVG)
- blob: URLs require browser-like object store (out of scope)
- **Decision:** Exclude from network module scope. Handle in future loader/decoder enhancement.

**CDN Awareness**
- Respect standard HTTP cache headers (Cache-Control, ETag, Vary, Age)
- Multi-region cache invalidation beyond current scope
- **Decision:** Implement standard HTTP caching (Phase 1), defer CDN-specific features

### 12.2 Deferred to Future Enhancements

**Service Workers for Offline Caching**
- Complex PWA-like behavior requiring JavaScript runtime
- Requires request interception and custom cache strategies
- **Timeline:** Post-MVP (6+ months out)

**WebSocket Support for Real-Time Updates**
- Persistent bidirectional connections
- Different protocol from HTTP/HTTPS resource loading
- Better suited for Lambda Script async networking layer
- **Timeline:** Consider for Lambda Script v2.0

**Large Downloads & Partial Content Requests**
- Videos, large assets (100+ MB)
- HTTP Range header support (byte-range requests)
- Stream-to-disk instead of in-memory buffering
- Progress reporting for chunked downloads
- **Timeline:** Phase 8 (post-baseline, 3-4 months)

---

## 13. Conclusion

This proposal outlines a comprehensive network support system for Radiant that:

✅ **Leverages existing Lambda infrastructure** - Builds on `input_http.cpp` and file cache  
✅ **Non-blocking architecture** - Thread pool prevents UI freezing  
✅ **Progressive enhancement** - Layout and paint as resources arrive  
✅ **Robust error handling** - Timeouts, retries, fallbacks  
✅ **Production-ready security** - CORS, HTTPS verification  
✅ **High performance** - Caching, connection pooling, priority queues  

**Next Steps:**
1. Review and approve proposal
2. Create tracking issues for each phase
3. Begin Phase 1 implementation (EnhancedFileCache)
4. Set up integration test infrastructure

**Estimated Timeline:** 12 weeks to full production deployment

---

**References:**
- [Chromium Network Stack](https://www.chromium.org/developers/design-documents/network-stack/)
- [libcurl Multi Interface](https://curl.se/libcurl/c/libcurl-multi.html)
- [HTTP Caching RFC 7234](https://httpwg.org/specs/rfc7234.html)
- [CORS Specification](https://fetch.spec.whatwg.org/#http-cors-protocol)
