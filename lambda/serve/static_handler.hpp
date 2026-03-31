/**
 * @file static_handler.hpp
 * @brief Static file serving with conditional responses and range requests
 *
 * Full-featured static file server supporting:
 *   - ETag / Last-Modified conditional responses (304 Not Modified)
 *   - Range requests (206 Partial Content) for large file downloads
 *   - Directory index files
 *   - MIME type detection via mime.hpp
 *   - Cache-Control headers
 *
 * Compatible with:
 *   Express:  express.static('public', {maxAge: 3600})
 *   Flask:    send_from_directory() / send_file()
 *   Nginx:    location /static/ { root /var/www; }
 */

#pragma once

#include "serve_types.hpp"
#include "http_request.hpp"
#include "http_response.hpp"

// ============================================================================
// Static Handler Configuration
// ============================================================================

struct StaticHandlerConfig {
    const char *root_dir;           // document root directory
    const char *index_file;         // default index file (NULL = "index.html")
    int enable_etag;                // generate ETag headers (default: 1)
    int enable_last_modified;       // send Last-Modified header (default: 1)
    int enable_range;               // allow Range requests (default: 1)
    int max_age;                    // Cache-Control max-age seconds (default: 0)
    int enable_directory_listing;   // list directory contents (default: 0)
};

StaticHandlerConfig static_handler_config_default(void);

// ============================================================================
// Static Handler API
// ============================================================================

// serve a static file for the given request path.
// returns 0 if file was served, -1 if file not found (caller should continue).
int static_serve_file(const StaticHandlerConfig *config,
                      HttpRequest *req, HttpResponse *resp);

// send a single file by absolute path with appropriate headers.
// detects MIME type, sets ETag/Last-Modified if config allows.
int static_send_file(const StaticHandlerConfig *config,
                     const char *filepath,
                     HttpRequest *req, HttpResponse *resp);

// generate a weak ETag from file path, size, and mtime
// returns malloc'd string (caller must free)
char* static_generate_etag(const char *filepath);
