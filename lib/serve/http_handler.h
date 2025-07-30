/**
 * @file http_handler.h
 * @brief HTTP request handling and response generation
 *
 * this file defines functions for handling HTTP requests, generating responses,
 * and managing HTTP-specific functionality like headers and status codes.
 */

#ifndef SERVE_HTTP_HANDLER_H
#define SERVE_HTTP_HANDLER_H

#include <event2/http.h>
#include <event2/buffer.h>

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
    HTTP_METHOD_GET = 1 << 0,
    HTTP_METHOD_POST = 1 << 1,
    HTTP_METHOD_PUT = 1 << 2,
    HTTP_METHOD_DELETE = 1 << 3,
    HTTP_METHOD_HEAD = 1 << 4,
    HTTP_METHOD_OPTIONS = 1 << 5,
    HTTP_METHOD_PATCH = 1 << 6
} http_method_t;

/**
 * request context structure
 */
typedef struct {
    struct evhttp_request *req;
    const char *uri;
    const char *path;
    const char *query;
    enum evhttp_cmd_type method;
    struct evkeyvalq *headers;
    struct evkeyvalq *query_params;
    struct evbuffer *input_buffer;
    void *user_data;
} http_request_t;

/**
 * response helper structure
 */
typedef struct {
    struct evhttp_request *req;
    struct evbuffer *output_buffer;
    struct evkeyvalq *headers;
    int status_code;
    int headers_sent;
} http_response_t;

/**
 * request parsing functions
 */

/**
 * create request context from evhttp request
 * @param req libevent http request
 * @return request context or NULL on error
 */
http_request_t* http_request_create(struct evhttp_request *req);

/**
 * destroy request context
 * @param request request context
 */
void http_request_destroy(http_request_t *request);

/**
 * get query parameter value
 * @param request request context
 * @param name parameter name
 * @return parameter value or NULL if not found
 */
const char* http_request_get_param(http_request_t *request, const char *name);

/**
 * get request header value
 * @param request request context
 * @param name header name (case-insensitive)
 * @return header value or NULL if not found
 */
const char* http_request_get_header(http_request_t *request, const char *name);

/**
 * get request body as string
 * @param request request context
 * @return body content or NULL if empty (must be freed)
 */
char* http_request_get_body(http_request_t *request);

/**
 * get request body size
 * @param request request context
 * @return body size in bytes
 */
size_t http_request_get_body_size(http_request_t *request);

/**
 * response generation functions
 */

/**
 * create response context
 * @param req libevent http request
 * @return response context or NULL on error
 */
http_response_t* http_response_create(struct evhttp_request *req);

/**
 * destroy response context (automatically sends response if not sent)
 * @param response response context
 */
void http_response_destroy(http_response_t *response);

/**
 * set response status code
 * @param response response context
 * @param status_code http status code
 */
void http_response_set_status(http_response_t *response, int status_code);

/**
 * set response header
 * @param response response context
 * @param name header name
 * @param value header value
 */
void http_response_set_header(http_response_t *response, 
                             const char *name, const char *value);

/**
 * add content to response body
 * @param response response context
 * @param data content data
 * @param size content size
 */
void http_response_add_content(http_response_t *response, 
                              const void *data, size_t size);

/**
 * add string content to response body
 * @param response response context
 * @param content string content
 */
void http_response_add_string(http_response_t *response, const char *content);

/**
 * add formatted content to response body
 * @param response response context
 * @param format printf-style format string
 * @param ... format arguments
 */
void http_response_add_printf(http_response_t *response, 
                             const char *format, ...);

/**
 * send the response to client
 * @param response response context
 */
void http_response_send(http_response_t *response);

/**
 * convenience functions
 */

/**
 * send simple text response
 * @param req libevent http request
 * @param status_code http status code
 * @param content_type content type header
 * @param content response content
 */
void http_send_simple_response(struct evhttp_request *req, int status_code,
                              const char *content_type, const char *content);

/**
 * send error response
 * @param req libevent http request
 * @param status_code http status code
 * @param message error message
 */
void http_send_error(struct evhttp_request *req, int status_code, 
                    const char *message);

/**
 * send file response
 * @param req libevent http request
 * @param filepath path to file
 * @return 0 on success, -1 on error
 */
int http_send_file(struct evhttp_request *req, const char *filepath);

/**
 * send redirect response
 * @param req libevent http request
 * @param location redirect location
 * @param permanent whether redirect is permanent (301 vs 302)
 */
void http_send_redirect(struct evhttp_request *req, const char *location, 
                       int permanent);

/**
 * utility functions
 */

/**
 * get http method string
 * @param method libevent method enum
 * @return method string
 */
const char* http_method_string(enum evhttp_cmd_type method);

/**
 * get http status string
 * @param status_code status code
 * @return status string
 */
const char* http_status_string(int status_code);

/**
 * parse query string into key-value pairs
 * @param query query string
 * @param params output key-value structure
 * @return 0 on success, -1 on error
 */
int http_parse_query(const char *query, struct evkeyvalq *params);

/**
 * url decode string in place
 * @param str string to decode
 * @return decoded length
 */
size_t http_url_decode(char *str);

/**
 * check if method is allowed
 * @param method actual method
 * @param allowed_methods bitmask of allowed methods
 * @return 1 if allowed, 0 if not
 */
int http_method_allowed(enum evhttp_cmd_type method, int allowed_methods);

#ifdef __cplusplus
}
#endif

#endif /* SERVE_HTTP_HANDLER_H */
