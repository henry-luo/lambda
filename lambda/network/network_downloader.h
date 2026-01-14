// network_downloader.h
// Network download implementation using libcurl with timeout support

#ifndef NETWORK_DOWNLOADER_H
#define NETWORK_DOWNLOADER_H

#include "network_resource_manager.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Download a network resource using libcurl with timeout enforcement.
 * This function is called by the thread pool worker threads.
 * 
 * @param res The network resource to download
 * @return true if download succeeded, false otherwise
 */
bool network_download_resource(NetworkResource* res);

/**
 * Check if an HTTP status code indicates a retryable error.
 * 
 * @param http_code HTTP status code (e.g., 404, 503)
 * @return true if the error is retryable (5xx errors), false otherwise (4xx errors)
 */
bool is_http_error_retryable(long http_code);

#ifdef __cplusplus
}
#endif

#endif // NETWORK_DOWNLOADER_H
