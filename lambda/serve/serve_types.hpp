/**
 * @file serve_types.hpp
 * @brief Core types and constants for the Lambda web server
 *
 * Unified type definitions shared across all server components.
 * Follows C+ convention — structs with C-compatible layout.
 */

#pragma once

#include <uv.h>
#include <stddef.h>
#include <stdint.h>

// ============================================================================
// HTTP Methods
// ============================================================================

enum HttpMethod {
    HTTP_GET     = 1 << 0,
    HTTP_POST    = 1 << 1,
    HTTP_PUT     = 1 << 2,
    HTTP_DELETE  = 1 << 3,
    HTTP_HEAD    = 1 << 4,
    HTTP_OPTIONS = 1 << 5,
    HTTP_PATCH   = 1 << 6,
    HTTP_CONNECT = 1 << 7,
    HTTP_TRACE   = 1 << 8,
    HTTP_UNKNOWN = 0,
    HTTP_ALL     = 0x1FF   // all methods combined
};

// ============================================================================
// HTTP Status Codes
// ============================================================================

enum HttpStatus {
    // 1xx informational
    HTTP_100_CONTINUE            = 100,
    HTTP_101_SWITCHING_PROTOCOLS = 101,

    // 2xx success
    HTTP_200_OK                  = 200,
    HTTP_201_CREATED             = 201,
    HTTP_202_ACCEPTED            = 202,
    HTTP_204_NO_CONTENT          = 204,
    HTTP_206_PARTIAL_CONTENT     = 206,

    // 3xx redirection
    HTTP_301_MOVED_PERMANENTLY   = 301,
    HTTP_302_FOUND               = 302,
    HTTP_304_NOT_MODIFIED        = 304,
    HTTP_307_TEMPORARY_REDIRECT  = 307,
    HTTP_308_PERMANENT_REDIRECT  = 308,

    // 4xx client error
    HTTP_400_BAD_REQUEST         = 400,
    HTTP_401_UNAUTHORIZED        = 401,
    HTTP_403_FORBIDDEN           = 403,
    HTTP_404_NOT_FOUND           = 404,
    HTTP_405_METHOD_NOT_ALLOWED  = 405,
    HTTP_408_REQUEST_TIMEOUT     = 408,
    HTTP_409_CONFLICT            = 409,
    HTTP_413_PAYLOAD_TOO_LARGE   = 413,
    HTTP_415_UNSUPPORTED_MEDIA   = 415,
    HTTP_422_UNPROCESSABLE       = 422,
    HTTP_429_TOO_MANY_REQUESTS   = 429,

    // 5xx server error
    HTTP_500_INTERNAL_ERROR      = 500,
    HTTP_501_NOT_IMPLEMENTED     = 501,
    HTTP_502_BAD_GATEWAY         = 502,
    HTTP_503_SERVICE_UNAVAILABLE = 503,
    HTTP_504_GATEWAY_TIMEOUT     = 504
};

// ============================================================================
// Header Key-Value Pair (linked list node)
// ============================================================================

struct HttpHeader {
    char *name;
    char *value;
    HttpHeader *next;
};

HttpHeader* http_header_add(HttpHeader *list, const char *name, const char *value);
const char* http_header_find(const HttpHeader *list, const char *name);
void        http_header_free(HttpHeader *list);
HttpHeader* http_header_remove(HttpHeader *list, const char *name);

// ============================================================================
// Server Configuration
// ============================================================================

struct ServerConfig {
    int port;                   // http port (0 to disable)
    int ssl_port;               // https port (0 to disable)
    char *bind_address;         // ip address to bind to (NULL for "0.0.0.0")
    char *ssl_cert_file;        // path to SSL certificate file
    char *ssl_key_file;         // path to SSL private key file
    int max_connections;        // maximum concurrent connections (default: 1024)
    int timeout_seconds;        // connection idle timeout (default: 60)
    char *document_root;        // document root for static files
    size_t max_header_size;     // max header size in bytes (default: 8192)
    size_t max_body_size;       // max body size in bytes (default: 10MB)
    int keep_alive;             // enable keep-alive (default: 1)
    int keep_alive_timeout;     // keep-alive timeout seconds (default: 5)
    int max_requests_per_conn;  // max requests per keep-alive connection (default: 100)
};

ServerConfig server_config_default(void);
int          server_config_validate(const ServerConfig *config);
void         server_config_cleanup(ServerConfig *config);

// ============================================================================
// Content Type Constants
// ============================================================================

#define CONTENT_TYPE_HTML       "text/html; charset=utf-8"
#define CONTENT_TYPE_JSON       "application/json; charset=utf-8"
#define CONTENT_TYPE_TEXT       "text/plain; charset=utf-8"
#define CONTENT_TYPE_XML        "application/xml; charset=utf-8"
#define CONTENT_TYPE_FORM       "application/x-www-form-urlencoded"
#define CONTENT_TYPE_MULTIPART  "multipart/form-data"
#define CONTENT_TYPE_OCTET      "application/octet-stream"

// ============================================================================
// Conversion Helpers
// ============================================================================

HttpMethod  http_method_from_string(const char *str);
const char* http_method_to_string(HttpMethod method);
const char* http_status_string(int status_code);
