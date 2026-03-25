/**
 * @file http_handler.h
 * @brief HTTP request handling and response generation
 *
 * this file defines functions for handling HTTP requests, generating responses,
 * and managing HTTP-specific functionality like headers and status codes.
 * v15: libuv-based, no libevent dependency.
 */

#ifndef SERVE_HTTP_HANDLER_H
#define SERVE_HTTP_HANDLER_H

#include <uv.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * http status codes
 */
typedef enum {
    HTTP_STATUS_OK = 200,
    HTTP_STATUS_CREATED = 201,
    HTTP_STATUS_NO_CONTENT = 204,
    HTTP_STATUS_MOVED_PERMANENTLY = 301,
    HTTP_STATUS_FOUND = 302,
    HTTP_STATUS_NOT_MODIFIED = 304,
    HTTP_STATUS_BAD_REQUEST = 400,
    HTTP_STATUS_UNAUTHORIZED = 401,
    HTTP_STATUS_FORBIDDEN = 403,
    HTTP_STATUS_NOT_FOUND = 404,
    HTTP_STATUS_METHOD_NOT_ALLOWED = 405,
    HTTP_STATUS_INTERNAL_ERROR = 500,
    HTTP_STATUS_NOT_IMPLEMENTED = 501,
    HTTP_STATUS_SERVICE_UNAVAILABLE = 503
} http_status_t;

/**
 * http methods
 */
typedef enum {
    HTTP_METHOD_GET     = 1 << 0,
    HTTP_METHOD_POST    = 1 << 1,
    HTTP_METHOD_PUT     = 1 << 2,
    HTTP_METHOD_DELETE  = 1 << 3,
    HTTP_METHOD_HEAD    = 1 << 4,
    HTTP_METHOD_OPTIONS = 1 << 5,
    HTTP_METHOD_PATCH   = 1 << 6,
    HTTP_METHOD_UNKNOWN = 0
} http_method_t;

/**
 * key-value pair for headers and query params
 */
typedef struct http_header {
    char *name;
    char *value;
    struct http_header *next;
} http_header_t;

/**
 * request context structure
 */
typedef struct http_request_s {
    uv_tcp_t *client;          // back-pointer to client connection
    const char *uri;
    char *path;
    char *query;
    http_method_t method;
    http_header_t *headers;
    http_header_t *query_params;
    char *body;
    size_t body_len;
    void *user_data;
} http_request_t;

/**
 * response helper structure
 */
typedef struct http_response_s {
    uv_tcp_t *client;          // client to write response to
    http_header_t *headers;
    char *body;
    size_t body_len;
    size_t body_cap;
    int status_code;
    int headers_sent;
} http_response_t;

/**
 * request parsing functions
 */

/**
 * parse raw HTTP data into request structure
 * @param data raw HTTP request data
 * @param len data length
 * @return request context or NULL on error
 */
http_request_t* http_request_parse(const char *data, size_t len);

/**
 * destroy request context
 * @param request request context
 */
void http_request_destroy(http_request_t *request);

/**
 * get query parameter value
 */
const char* http_request_get_param(http_request_t *request, const char *name);

/**
 * get request header value (case-insensitive)
 */
const char* http_request_get_header(http_request_t *request, const char *name);

/**
 * get request body as string
 * @return body content or NULL if empty (do NOT free — owned by request)
 */
const char* http_request_get_body(http_request_t *request);

/**
 * get request body size
 */
size_t http_request_get_body_size(http_request_t *request);

/**
 * response generation functions
 */

/**
 * create response context for a client connection
 */
http_response_t* http_response_create(uv_tcp_t *client);

/**
 * destroy response context (automatically sends if not sent)
 */
void http_response_destroy(http_response_t *response);

void http_response_set_status(http_response_t *response, int status_code);

void http_response_set_header(http_response_t *response,
                             const char *name, const char *value);

void http_response_add_content(http_response_t *response,
                              const void *data, size_t size);

void http_response_add_string(http_response_t *response, const char *content);

void http_response_add_printf(http_response_t *response,
                             const char *format, ...);

/**
 * send the response to client
 */
void http_response_send(http_response_t *response);

/**
 * convenience functions
 */

void http_send_simple_response(uv_tcp_t *client, int status_code,
                              const char *content_type, const char *content);

void http_send_error(uv_tcp_t *client, int status_code,
                    const char *message);

int http_send_file(uv_tcp_t *client, const char *filepath);

void http_send_redirect(uv_tcp_t *client, const char *location,
                       int permanent);

/**
 * utility functions
 */

const char* http_method_string(http_method_t method);

http_method_t http_method_from_string(const char *method_str);

const char* http_status_string(int status_code);

size_t http_url_decode(char *str);

int http_method_allowed(http_method_t method, int allowed_methods);

/**
 * header list helpers
 */
http_header_t* http_header_add(http_header_t *list, const char *name, const char *value);
const char* http_header_find(http_header_t *list, const char *name);
void http_header_free(http_header_t *list);

#ifdef __cplusplus
}
#endif

#endif /* SERVE_HTTP_HANDLER_H */
