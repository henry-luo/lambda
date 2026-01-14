#include "network_integration.h"
#include "network_resource_manager.h"
#include "network_thread_pool.h"
#include "enhanced_file_cache.h"
#include "resource_loaders.h"
#include "../input/css/dom_element.hpp"
#include "../../lib/log.h"
#include <time.h>

// Initialize network support for a document
int radiant_init_network_support(DomDocument* doc,
                                  NetworkThreadPool* thread_pool,
                                  EnhancedFileCache* file_cache) {
    if (!doc) {
        log_error("network: radiant_init_network_support called with NULL document");
        return -1;
    }

    if (doc->resource_manager) {
        log_warn("network: document already has network support initialized");
        return 0;
    }

    log_debug("network: initializing network support for document");

    // Create resource manager
    doc->resource_manager = resource_manager_create(doc, thread_pool, file_cache);
    if (!doc->resource_manager) {
        log_error("network: failed to create resource manager");
        return -1;
    }

    // Record load start time
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    doc->load_start_time = ts.tv_sec + ts.tv_nsec / 1000000000.0;
    doc->fully_loaded = false;

    log_debug("network: network support initialized successfully");
    return 0;
}

// Helper to query selector all matching elements
static void find_elements_by_selector(DomElement* root, const char* tag_name,
                                      void (*callback)(DomElement*, void*), void* user_data) {
    if (!root || !callback) return;

    // Check current element
    if (root->tag_name && strcmp(root->tag_name, tag_name) == 0) {
        callback(root, user_data);
    }

    // Recursively check children
    DomNode* child = root->first_child;
    while (child) {
        if (child->is_element()) {
            find_elements_by_selector(child->as_element(), tag_name, callback, user_data);
        }
        child = child->next_sibling;
    }
}

// Callback for <link rel="stylesheet"> discovery
static void discover_link_callback(DomElement* link, void* user_data) {
    DomDocument* doc = (DomDocument*)user_data;

    // Get rel attribute
    const char* rel = dom_element_get_attribute(link, "rel");
    if (!rel || strcmp(rel, "stylesheet") != 0) {
        return; // Not a stylesheet link
    }

    // Get href attribute
    const char* href = dom_element_get_attribute(link, "href");
    if (!href) {
        log_debug("network: <link rel=stylesheet> without href attribute");
        return;
    }

    log_debug("network: discovered stylesheet: %s", href);

    // Queue for download with HIGH priority (CSS blocks rendering)
    NetworkResource* res = resource_manager_load(doc->resource_manager,
                                                 href,
                                                 RESOURCE_CSS,
                                                 PRIORITY_HIGH,
                                                 link);

    // Set completion callback
    if (res) {
        res->on_complete = (void (*)(NetworkResource*, void*))process_css_resource;
        res->user_data = (void*)doc;
    }
}

// Callback for <img> discovery
static void discover_img_callback(DomElement* img, void* user_data) {
    DomDocument* doc = (DomDocument*)user_data;

    // Get src attribute
    const char* src = dom_element_get_attribute(img, "src");
    if (!src) {
        log_debug("network: <img> without src attribute");
        return;
    }

    log_debug("network: discovered image: %s", src);

    // Queue for download with NORMAL priority
    NetworkResource* res = resource_manager_load(doc->resource_manager,
                                                 src,
                                                 RESOURCE_IMAGE,
                                                 PRIORITY_NORMAL,
                                                 img);

    // Set completion callback
    if (res) {
        res->on_complete = (void (*)(NetworkResource*, void*))process_image_resource;
        res->user_data = (void*)img;
    }
}

// Callback for <svg><use> discovery
static void discover_use_callback(DomElement* use, void* user_data) {
    DomDocument* doc = (DomDocument*)user_data;

    // Get xlink:href or href attribute (SVG 2 uses href)
    const char* href = dom_element_get_attribute(use, "xlink:href");
    if (!href) {
        href = dom_element_get_attribute(use, "href");
    }

    if (!href) {
        log_debug("network: <use> without href attribute");
        return;
    }

    // Only load if external reference (contains # but doesn't start with #)
    if (!strchr(href, '#') || href[0] == '#') {
        return; // Internal reference, no network load needed
    }

    log_debug("network: discovered external SVG reference: %s", href);

    // Queue for download with NORMAL priority
    NetworkResource* res = resource_manager_load(doc->resource_manager,
                                                 href,
                                                 RESOURCE_SVG,
                                                 PRIORITY_NORMAL,
                                                 use);

    // Set completion callback
    if (res) {
        res->on_complete = (void (*)(NetworkResource*, void*))process_svg_resource;
        res->user_data = (void*)use;
    }
}

// Discover and queue all network resources in a document
void radiant_discover_document_resources(DomDocument* doc) {
    if (!doc || !doc->resource_manager) {
        log_debug("network: discover called on document without network support");
        return;
    }

    if (!doc->root) {
        log_debug("network: document has no root element");
        return;
    }

    log_debug("network: discovering document resources");

    // Find all <link rel="stylesheet">
    find_elements_by_selector(doc->root, "link", discover_link_callback, doc);

    // Find all <img>
    find_elements_by_selector(doc->root, "img", discover_img_callback, doc);

    // Find all <svg><use>
    find_elements_by_selector(doc->root, "use", discover_use_callback, doc);

    // TODO: Discover @font-face url() in stylesheets
    // This requires iterating through doc->stylesheets array and parsing @font-face rules
    // For now, font loading will be handled separately during font resolution

    log_debug("network: resource discovery complete");
}

// Check if document is fully loaded
int radiant_is_document_loaded(DomDocument* doc) {
    if (!doc || !doc->resource_manager) {
        return 1; // No network support = consider loaded
    }

    return resource_manager_is_fully_loaded(doc->resource_manager) ? 1 : 0;
}

// Get document load progress
float radiant_get_document_progress(DomDocument* doc) {
    if (!doc || !doc->resource_manager) {
        return 1.0f; // No network support = 100% loaded
    }

    return resource_manager_get_load_progress(doc->resource_manager);
}

// Cleanup network support
void radiant_cleanup_network_support(DomDocument* doc) {
    if (!doc || !doc->resource_manager) {
        return;
    }

    log_debug("network: cleaning up network support");

    resource_manager_destroy(doc->resource_manager);
    doc->resource_manager = nullptr;
    doc->fully_loaded = true;
}
