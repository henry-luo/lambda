#ifndef NETWORK_INTEGRATION_H
#define NETWORK_INTEGRATION_H

#include "../input/css/dom_element.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct NetworkResourceManager;
struct NetworkThreadPool;
struct EnhancedFileCache;

/**
 * Initialize network support for a document.
 * Must be called before any network resources are loaded.
 * 
 * @param doc Document to enable network support for
 * @param thread_pool Shared thread pool (or NULL to create one)
 * @param file_cache Shared file cache (or NULL to create one)
 * @return 0 on success, -1 on error
 */
int radiant_init_network_support(DomDocument* doc,
                                  struct NetworkThreadPool* thread_pool,
                                  struct EnhancedFileCache* file_cache);

/**
 * Discover and queue all network resources in a document.
 * Traverses DOM tree to find:
 * - <link rel="stylesheet" href="...">
 * - <img src="...">
 * - @font-face url() in stylesheets
 * - <svg><use xlink:href="external.svg#id">
 * 
 * @param doc Document to discover resources for
 */
void radiant_discover_document_resources(DomDocument* doc);

/**
 * Check if document is fully loaded (all network resources completed).
 * 
 * @param doc Document to check
 * @return 1 if fully loaded, 0 if still loading
 */
int radiant_is_document_loaded(DomDocument* doc);

/**
 * Get document load progress (0.0 - 1.0).
 * 
 * @param doc Document to check
 * @return Progress as float (0.0 = nothing loaded, 1.0 = fully loaded)
 */
float radiant_get_document_progress(DomDocument* doc);

/**
 * Cleanup network support for a document.
 * Must be called before document destruction.
 * 
 * @param doc Document to cleanup
 */
void radiant_cleanup_network_support(DomDocument* doc);

#ifdef __cplusplus
}
#endif

#endif // NETWORK_INTEGRATION_H
