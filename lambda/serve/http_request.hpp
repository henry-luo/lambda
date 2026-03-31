/**
 * @file http_request.hpp
 * @brief HTTP request representation — unified design inspired by
 *        Node.js IncomingMessage, Flask Request, and FastAPI Request
 *
 * Design goals:
 *   - Single struct covers all frameworks' request concepts
 *   - Lazy parsing: body/cookies/params parsed on first access
 *   - Zero-copy where possible: headers point into the raw buffer
 *   - Extensible via user_data and parsed_body for middleware
 *
 * Node.js equivalents:     req.method, req.url, req.headers, req.params,
 *                          req.query, req.body, req.cookies, req.ip
 * Flask equivalents:       request.method, request.path, request.args,
 *                          request.form, request.json, request.cookies
 * FastAPI equivalents:     request.method, request.url, request.headers,
 *                          request.path_params, request.query_params, request.body()
 */

#pragma once

#include "serve_types.hpp"

// ============================================================================
// Parsed body types (set by body-parser middleware)
// ============================================================================

enum BodyType {
    BODY_NONE       = 0,
    BODY_RAW        = 1,   // unparsed raw bytes
    BODY_JSON       = 2,   // parsed JSON (stored as void* to framework-specific type)
    BODY_FORM       = 3,   // application/x-www-form-urlencoded → HttpHeader list
    BODY_MULTIPART  = 4,   // multipart/form-data → MultipartPart list
    BODY_TEXT       = 5,   // text/plain
    BODY_XML        = 6    // application/xml
};

// ============================================================================
// Multipart file upload part
// ============================================================================

struct MultipartPart {
    char *field_name;       // form field name
    char *filename;         // original filename (NULL for non-file fields)
    char *content_type;     // MIME type of the part
    char *data;             // part data (in-memory for small, temp file path for large)
    size_t data_len;        // data length
    int is_file;            // 1 if stored to temp file, 0 if in-memory
    MultipartPart *next;
};

void multipart_free(MultipartPart *parts);

// ============================================================================
// Cookie entry
// ============================================================================

struct CookieEntry {
    char *name;
    char *value;
    CookieEntry *next;
};

void cookie_entries_free(CookieEntry *list);

// ============================================================================
// HTTP Request
// ============================================================================

struct HttpRequest {
    // -- connection info --
    uv_tcp_t *client;           // libuv client handle
    char remote_addr[64];       // client IP address (e.g., "127.0.0.1")
    int  remote_port;           // client port

    // -- request line --
    HttpMethod method;          // GET, POST, PUT, etc.
    char *uri;                  // full URI as received (e.g., "/users/42?page=1")
    char *path;                 // URL-decoded path (e.g., "/users/42")
    char *query_string;         // raw query string without '?' (e.g., "page=1&limit=10")
    char *fragment;             // URL fragment (rare in HTTP, but supported)

    // -- protocol --
    int http_version_major;     // 1
    int http_version_minor;     // 0 or 1

    // -- headers --
    HttpHeader *headers;        // all request headers (case-insensitive lookup)

    // -- parsed query parameters (lazy, populated on first access) --
    HttpHeader *query_params;   // ?key=value pairs

    // -- route parameters (populated by router) --
    //    Express: req.params.id    Flask: <id>    FastAPI: path params
    HttpHeader *route_params;

    // -- cookies (lazy, parsed from Cookie header on first access) --
    CookieEntry *cookies;
    int cookies_parsed;         // 0 = not yet parsed

    // -- body --
    char *body;                 // raw body bytes
    size_t body_len;            // raw body length

    // -- parsed body (set by body-parser middleware) --
    BodyType parsed_body_type;
    void *parsed_body;          // type depends on parsed_body_type:
                                //   BODY_JSON → framework-specific parsed object
                                //   BODY_FORM → HttpHeader* key-value list
                                //   BODY_MULTIPART → MultipartPart* list

    // -- middleware / handler state --
    void *user_data;            // per-request user-defined data
    void *app;                  // back-pointer to server/application

    // -- internal --
    int _keep_alive;            // 1 if connection should be kept alive
};

// ============================================================================
// Request Lifecycle
// ============================================================================

// parse raw HTTP data into an HttpRequest. returns NULL on malformed input.
HttpRequest* http_request_parse(const char *data, size_t len);

// destroy request and all owned memory
void http_request_destroy(HttpRequest *req);

// ============================================================================
// Accessors (unified API matching Node/Flask/FastAPI conventions)
// ============================================================================

// get request header value (case-insensitive)
//   Node: req.get('Content-Type')   Flask: request.headers['Content-Type']
const char* http_request_header(HttpRequest *req, const char *name);

// get query parameter value
//   Node: req.query.page            Flask: request.args.get('page')
const char* http_request_query(HttpRequest *req, const char *name);

// get route parameter value (set by router after matching)
//   Node: req.params.id             Flask: view_func(id=...)
const char* http_request_param(HttpRequest *req, const char *name);

// get cookie value (lazy-parses Cookie header on first call)
//   Node: req.cookies.session       Flask: request.cookies.get('session')
const char* http_request_cookie(HttpRequest *req, const char *name);

// get raw body pointer (do not free — owned by request)
//   Node: req.body                  Flask: request.data
const char* http_request_body(HttpRequest *req);

// get Content-Type header value
const char* http_request_content_type(HttpRequest *req);

// get Content-Length as integer (-1 if missing)
int64_t http_request_content_length(HttpRequest *req);

// check if request wants JSON response (Accept header inspection)
int http_request_accepts_json(HttpRequest *req);

// check if request is secure (HTTPS)
int http_request_is_secure(HttpRequest *req);

// get the full URL (reconstructed from host header + path + query)
// caller must free the returned string
char* http_request_full_url(HttpRequest *req);
