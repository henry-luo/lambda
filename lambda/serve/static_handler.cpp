/**
 * @file static_handler.cpp
 * @brief Static file serving implementation
 *
 * Full static file handler with ETag, Last-Modified, Range requests,
 * and MIME type detection. Used by the static middleware and can also
 * be called directly from route handlers.
 */

#include "static_handler.hpp"
#include "serve_utils.hpp"
#include "mime.hpp"
#include "../../lib/log.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

// ============================================================================
// Configuration defaults
// ============================================================================

StaticHandlerConfig static_handler_config_default(void) {
    StaticHandlerConfig config;
    memset(&config, 0, sizeof(config));
    config.index_file = "index.html";
    config.enable_etag = 1;
    config.enable_last_modified = 1;
    config.enable_range = 1;
    config.max_age = 0;
    config.enable_directory_listing = 0;
    return config;
}

// ============================================================================
// ETag generation
// ============================================================================

char* static_generate_etag(const char *filepath) {
    if (!filepath) return NULL;

    struct stat st;
    if (stat(filepath, &st) != 0) return NULL;

    // weak ETag based on size + mtime (hex encoded)
    char etag[128];
    snprintf(etag, sizeof(etag), "W/\"%lx-%lx\"",
             (unsigned long)st.st_size, (unsigned long)st.st_mtime);
    return serve_strdup(etag);
}

// ============================================================================
// Conditional response checks
// ============================================================================

static int check_not_modified(const StaticHandlerConfig *config,
                              const char *filepath,
                              HttpRequest *req, HttpResponse *resp) {
    // check If-None-Match (ETag)
    if (config->enable_etag) {
        const char *if_none_match = http_request_header(req, "If-None-Match");
        if (if_none_match) {
            char *etag = static_generate_etag(filepath);
            if (etag && strcmp(if_none_match, etag) == 0) {
                serve_free(etag);
                http_response_status(resp, HTTP_304_NOT_MODIFIED);
                http_response_send(resp);
                return 1;
            }
            serve_free(etag);
        }
    }

    // check If-Modified-Since
    if (config->enable_last_modified) {
        const char *if_mod_since = http_request_header(req, "If-Modified-Since");
        if (if_mod_since) {
            struct stat st;
            if (stat(filepath, &st) == 0) {
                // parse If-Modified-Since header
                struct tm tm_req;
                memset(&tm_req, 0, sizeof(tm_req));
                if (strptime(if_mod_since, "%a, %d %b %Y %H:%M:%S GMT", &tm_req)) {
                    time_t req_time = timegm(&tm_req);
                    if (st.st_mtime <= req_time) {
                        http_response_status(resp, HTTP_304_NOT_MODIFIED);
                        http_response_send(resp);
                        return 1;
                    }
                }
            }
        }
    }

    return 0; // not cached — proceed with full response
}

// ============================================================================
// Range request handling
// ============================================================================

// parse "bytes=START-END" range header
// returns 0 on success, -1 on parse failure
static int parse_range_header(const char *range_str, size_t file_size,
                              size_t *out_start, size_t *out_end) {
    if (!range_str || strncmp(range_str, "bytes=", 6) != 0) return -1;

    const char *spec = range_str + 6;
    if (spec[0] == '-') {
        // suffix range: -500 means last 500 bytes
        size_t suffix = (size_t)atol(spec + 1);
        if (suffix == 0 || suffix > file_size) return -1;
        *out_start = file_size - suffix;
        *out_end = file_size - 1;
    } else {
        *out_start = (size_t)atol(spec);
        const char *dash = strchr(spec, '-');
        if (!dash) return -1;
        if (dash[1] == '\0') {
            *out_end = file_size - 1; // open-ended
        } else {
            *out_end = (size_t)atol(dash + 1);
        }
    }

    if (*out_start > *out_end || *out_end >= file_size) return -1;
    return 0;
}

// ============================================================================
// Send file with headers
// ============================================================================

int static_send_file(const StaticHandlerConfig *config,
                     const char *filepath,
                     HttpRequest *req, HttpResponse *resp) {
    if (!filepath || !req || !resp) return -1;

    struct stat st;
    if (stat(filepath, &st) != 0) return -1;
    if (!S_ISREG(st.st_mode)) return -1;

    // check conditional headers before reading file
    if (config) {
        if (check_not_modified(config, filepath, req, resp)) {
            return 0; // 304 sent
        }
    }

    // detect MIME type
    const char *content_type = mime_detect(NULL, filepath, NULL, 0);
    if (!content_type) content_type = "application/octet-stream";
    http_response_set_header(resp, "Content-Type", content_type);

    // set ETag
    if (config && config->enable_etag) {
        char *etag = static_generate_etag(filepath);
        if (etag) {
            http_response_set_header(resp, "ETag", etag);
            serve_free(etag);
        }
    }

    // set Last-Modified
    if (config && config->enable_last_modified) {
        char mtime_str[64];
        serve_file_mtime_str(filepath, mtime_str, sizeof(mtime_str));
        if (mtime_str[0]) {
            http_response_set_header(resp, "Last-Modified", mtime_str);
        }
    }

    // set Cache-Control
    if (config && config->max_age > 0) {
        char cache_str[64];
        snprintf(cache_str, sizeof(cache_str), "public, max-age=%d", config->max_age);
        http_response_set_header(resp, "Cache-Control", cache_str);
    }

    // Accept-Ranges header
    if (config && config->enable_range) {
        http_response_set_header(resp, "Accept-Ranges", "bytes");
    }

    // check Range request
    const char *range_str = http_request_header(req, "Range");
    if (range_str && config && config->enable_range) {
        size_t range_start, range_end;
        if (parse_range_header(range_str, (size_t)st.st_size,
                               &range_start, &range_end) == 0) {
            size_t range_len = range_end - range_start + 1;

            // read the requested range
            FILE *f = fopen(filepath, "rb");
            if (!f) return -1;

            char *range_buf = (char*)serve_malloc(range_len);
            fseek(f, (long)range_start, SEEK_SET);
            size_t read = fread(range_buf, 1, range_len, f);
            fclose(f);

            if (read != range_len) {
                serve_free(range_buf);
                return -1;
            }

            // Content-Range header
            char cr_str[128];
            snprintf(cr_str, sizeof(cr_str), "bytes %zu-%zu/%zu",
                     range_start, range_end, (size_t)st.st_size);
            http_response_set_header(resp, "Content-Range", cr_str);

            http_response_status(resp, HTTP_206_PARTIAL_CONTENT);
            http_response_write(resp, range_buf, range_len);
            http_response_send(resp);
            serve_free(range_buf);
            return 0;
        }
        // invalid range — fall through to full response
    }

    // full file response
    http_response_file(resp, filepath, content_type);
    return 0;
}

// ============================================================================
// Serve static files from directory
// ============================================================================

int static_serve_file(const StaticHandlerConfig *config,
                      HttpRequest *req, HttpResponse *resp) {
    if (!config || !config->root_dir || !req || !resp) return -1;

    // only serve GET and HEAD
    if (req->method != HTTP_GET && req->method != HTTP_HEAD) return -1;

    // prevent directory traversal
    if (strstr(req->path, "..")) return -1;

    // build filesystem path
    char filepath[4096];
    snprintf(filepath, sizeof(filepath), "%s%s", config->root_dir, req->path);

    // check if it's a directory — try index file
    struct stat st;
    if (stat(filepath, &st) == 0 && S_ISDIR(st.st_mode)) {
        size_t len = strlen(filepath);
        const char *idx = config->index_file ? config->index_file : "index.html";
        if (len > 0 && filepath[len - 1] != '/') {
            snprintf(filepath + len, sizeof(filepath) - len, "/%s", idx);
        } else {
            snprintf(filepath + len, sizeof(filepath) - len, "%s", idx);
        }

        if (stat(filepath, &st) != 0) return -1; // no index file
    }

    if (!serve_file_exists(filepath)) return -1;

    return static_send_file(config, filepath, req, resp);
}
