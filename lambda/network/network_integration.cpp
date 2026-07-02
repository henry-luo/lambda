#include "network_integration.h"
#include "network_resource_manager.h"
#include "network_thread_pool.h"
#include "enhanced_file_cache.h"
#include "resource_loaders.h"
#include "../input/css/dom_element.hpp"
#include "../../lib/log.h"
#include "../../lib/url.h"
#include "../../lib/mem.h"
#include "../../lib/time_util.h"
#include "../input/css/css_font_face.hpp"
#include <strings.h>
#include <time.h>

static bool is_supported_web_font_source(const char* url, const char* format) {
    if (format && *format) {
        if (strcasecmp(format, "woff2") == 0 ||
            strcasecmp(format, "woff") == 0 ||
            strcasecmp(format, "truetype") == 0 ||
            strcasecmp(format, "opentype") == 0 ||
            strcasecmp(format, "ttf") == 0 ||
            strcasecmp(format, "otf") == 0) {
            return true;
        }
        return false;
    }

    if (!url) return false;
    const char* clean_end = url + strlen(url);
    const char* query = strchr(url, '?');
    const char* fragment = strchr(url, '#');
    if (query && query < clean_end) clean_end = query;
    if (fragment && fragment < clean_end) clean_end = fragment;

    size_t len = (size_t)(clean_end - url);
    // Browser font selection skips fallback formats this loader cannot decode;
    // downloading them first turns ordinary legacy src lists into hard 404s.
    return (len >= 6 && strncasecmp(clean_end - 6, ".woff2", 6) == 0) ||
           (len >= 5 && strncasecmp(clean_end - 5, ".woff", 5) == 0) ||
           (len >= 4 && strncasecmp(clean_end - 4, ".ttf", 4) == 0) ||
           (len >= 4 && strncasecmp(clean_end - 4, ".otf", 4) == 0) ||
           (len >= 4 && strncasecmp(clean_end - 4, ".ttc", 4) == 0);
}

// Helper: resolve a potentially relative URL against the document's base URL.
// Returns a heap-allocated absolute URL string (caller must free with mem_free).
// If resolution fails, returns a copy of the input.
static char* resolve_url(const char* href, DomDocument* doc) {
    if (!href) return nullptr;
    if (url_is_absolute_url(href)) {
        return mem_strdup(href, MEM_CAT_NETWORK);
    }
    if (!doc || !doc->url) {
        return mem_strdup(href, MEM_CAT_NETWORK);
    }
    Url* resolved = url_resolve_relative(href, doc->url);
    if (resolved && resolved->href) {
        char* result = mem_strdup(resolved->href->chars, MEM_CAT_NETWORK);
        url_destroy(resolved);
        return result;
    }
    if (resolved) url_destroy(resolved);
    return mem_strdup(href, MEM_CAT_NETWORK);
}

static char* resolve_font_resource_url(const char* href, const char* stylesheet_url,
                                       DomDocument* doc) {
    if (!href) return nullptr;
    if (url_is_absolute_url(href)) {
        return mem_strdup(href, MEM_CAT_NETWORK);
    }
    if (stylesheet_url && url_is_absolute_url(stylesheet_url)) {
        Url* base = url_parse(stylesheet_url);
        Url* resolved = base ? url_resolve_relative(href, base) : NULL;
        if (base) url_destroy(base);
        if (resolved && resolved->href) {
            char* result = mem_strdup(resolved->href->chars, MEM_CAT_NETWORK);
            url_destroy(resolved);
            return result;
        }
        if (resolved) url_destroy(resolved);
    }
    return resolve_url(href, doc);
}

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
    doc->load_start_time = time_now_seconds();
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
    if (!rel) return;

    // Get href attribute
    const char* href = dom_element_get_attribute(link, "href");
    if (!href) return;

    if (strcmp(rel, "stylesheet") == 0) {
        log_debug("network: discovered stylesheet: %s", href);

        // Resolve relative URL against document base
        char* abs_url = resolve_url(href, doc);

        // Queue for download with HIGH priority (CSS blocks rendering).
        // Main-thread processing is handled by resource_manager_flush_layout_updates().
        resource_manager_load(doc->resource_manager,
                              abs_url,
                              RESOURCE_CSS,
                              PRIORITY_HIGH,
                              link);
        mem_free(abs_url);
    }
    else if (strcmp(rel, "preload") == 0) {
        // <link rel="preload" href="..." as="...">
        const char* as_type = dom_element_get_attribute(link, "as");
        if (!as_type) return;

        char* abs_url = resolve_url(href, doc);
        ResourceType rtype = RESOURCE_IMAGE;  // default
        ResourcePriority prio = PRIORITY_NORMAL;

        if (strcmp(as_type, "style") == 0) {
            rtype = RESOURCE_CSS;
            prio = PRIORITY_HIGH;
        } else if (strcmp(as_type, "font") == 0) {
            rtype = RESOURCE_FONT;
            prio = PRIORITY_HIGH;
        } else if (strcmp(as_type, "image") == 0) {
            rtype = RESOURCE_IMAGE;
            prio = PRIORITY_NORMAL;
        } else if (strcmp(as_type, "script") == 0) {
            rtype = RESOURCE_SCRIPT;
            prio = PRIORITY_NORMAL;
        }

        log_debug("network: discovered preload: %s (as=%s)", href, as_type);
        resource_manager_load(doc->resource_manager, abs_url, rtype, prio, link);
        mem_free(abs_url);
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
    if (strncmp(src, "data:", 5) == 0) {
        // Data URI images are decoded synchronously by load_image(); queuing
        // them as network transfers produces false URL-format failures.
        log_debug("network: skipping data URI image for async discovery");
        return;
    }

    log_debug("network: discovered image: %s", src);

    // Resolve relative URL against document base
    char* abs_url = resolve_url(src, doc);

    // Queue for download with NORMAL priority. Main-thread processing is
    // handled by resource_manager_flush_layout_updates().
    resource_manager_load(doc->resource_manager,
                          abs_url,
                          RESOURCE_IMAGE,
                          PRIORITY_NORMAL,
                          img);
    mem_free(abs_url);
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

    // Resolve relative URL against document base
    char* abs_url = resolve_url(href, doc);

    // Queue for download with NORMAL priority. Main-thread processing is
    // handled by resource_manager_flush_layout_updates().
    resource_manager_load(doc->resource_manager,
                          abs_url,
                          RESOURCE_SVG,
                          PRIORITY_NORMAL,
                          use);
    mem_free(abs_url);
}

// Callback for <script src="..."> discovery
static void discover_script_callback(DomElement* script, void* user_data) {
    DomDocument* doc = (DomDocument*)user_data;

    const char* src = dom_element_get_attribute(script, "src");
    if (!src) return;  // inline script, no download needed

    // Determine priority: defer/async scripts are lower priority
    const char* defer_attr = dom_element_get_attribute(script, "defer");
    const char* async_attr = dom_element_get_attribute(script, "async");
    ResourcePriority priority = (defer_attr || async_attr) ? PRIORITY_NORMAL : PRIORITY_HIGH;

    char* abs_url = resolve_url(src, doc);
    log_debug("network: discovered <script src>: %s (priority=%d)", abs_url, priority);

    resource_manager_load(doc->resource_manager, abs_url, RESOURCE_SCRIPT, priority, script);
    mem_free(abs_url);
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

    // Find all <script src="...">
    find_elements_by_selector(doc->root, "script", discover_script_callback, doc);

    // Find all <picture><source srcset="..."> and <img srcset="...">
    // Note: srcset parsing is simplified — only uses first URL, ignores descriptors
    // Full srcset handling would need viewport/DPR-aware selection

    // Discover @font-face url() in stylesheets
    if (doc->stylesheets && doc->stylesheet_count > 0) {
        for (int s = 0; s < doc->stylesheet_count; s++) {
            if (!doc->stylesheets[s]) continue;
            int face_count = 0;
            CssStylesheet* sheet = doc->stylesheets[s];
            Pool* face_pool = doc->pool ? doc->pool : sheet->pool;
            bool free_faces = (face_pool == NULL);
            const char* font_base_url = sheet->origin_url ? sheet->origin_url : NULL;
            CssFontFaceDescriptor** faces = css_extract_font_faces(
                sheet, font_base_url, face_pool, &face_count);
            for (int f = 0; f < face_count; f++) {
                if (!faces[f]) continue;
                // Try each src URL in order — queue the first HTTP URL found
                for (int u = 0; u < faces[f]->src_count; u++) {
                    const char* font_url = faces[f]->src_urls[u].url;
                    if (!font_url) continue;
                    if (!is_supported_web_font_source(font_url, faces[f]->src_urls[u].format)) {
                        log_debug("network: skipping unsupported @font-face source: %s (format: %s)",
                                  font_url, faces[f]->src_urls[u].format ? faces[f]->src_urls[u].format : "?");
                        continue;
                    }
                    char* abs_url = resolve_font_resource_url(font_url, font_base_url, doc);
                    if (abs_url && url_is_absolute_url(abs_url) &&
                        (strncmp(abs_url, "http://", 7) == 0 || strncmp(abs_url, "https://", 8) == 0)) {
                        log_debug("network: discovered @font-face url: %s (family: %s)",
                                  abs_url, faces[f]->family_name ? faces[f]->family_name : "?");
                        resource_manager_load(doc->resource_manager,
                                              abs_url, RESOURCE_FONT, PRIORITY_HIGH, NULL);
                        mem_free(abs_url);
                        break;  // only queue first viable URL per @font-face
                    }
                    mem_free(abs_url);
                }
                // Also try the fallback src_url field
                if (faces[f]->src_url && faces[f]->src_count == 0) {
                    char* abs_url = resolve_font_resource_url(faces[f]->src_url, font_base_url, doc);
                    if (abs_url && url_is_absolute_url(abs_url) &&
                        (strncmp(abs_url, "http://", 7) == 0 || strncmp(abs_url, "https://", 8) == 0)) {
                        log_debug("network: discovered @font-face src_url: %s", abs_url);
                        resource_manager_load(doc->resource_manager,
                                              abs_url, RESOURCE_FONT, PRIORITY_HIGH, NULL);
                    }
                    mem_free(abs_url);
                }
            }
            if (free_faces && faces) {
                // css_extract_font_faces() heap-allocates when no pool is
                // available; async discovery owns those transient descriptors.
                for (int f = 0; f < face_count; f++) {
                    css_font_face_descriptor_free(faces[f]);
                }
                mem_free(faces);
            }
        }
    }

    // Process cache hits immediately on the main thread so first layout can
    // see cached CSS/images even when no async wait loop runs.
    resource_manager_flush_layout_updates(doc->resource_manager);

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

    // cancel all in-flight downloads before destroying the manager
    resource_manager_cancel_all(doc->resource_manager);

    resource_manager_destroy(doc->resource_manager);
    doc->resource_manager = nullptr;
    doc->fully_loaded = true;
}
