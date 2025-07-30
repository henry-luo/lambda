/**
 * @file http_handler.c
 * @brief HTTP request handling and response generation implementation
 */

#include "http_handler.h"
#include "utils.h"
#include <stdarg.h>
#include <sys/queue.h>
#include <event2/keyvalq_struct.h>

/**
 * request parsing implementation
 */

http_request_t* http_request_create(struct evhttp_request *req) {
    if (!req) {
        serve_set_error("null request");
        return NULL;
    }
    
    http_request_t *request = serve_malloc(sizeof(http_request_t));
    if (!request) {
        serve_set_error("failed to allocate request context");
        return NULL;
    }
    
    request->req = req;
    request->uri = evhttp_request_get_uri(req);
    request->method = evhttp_request_get_command(req);
    request->headers = evhttp_request_get_input_headers(req);
    request->input_buffer = evhttp_request_get_input_buffer(req);
    
    // parse uri to extract path and query
    if (request->uri) {
        struct evhttp_uri *uri_struct = evhttp_uri_parse(request->uri);
        if (uri_struct) {
            request->path = evhttp_uri_get_path(uri_struct);
            request->query = evhttp_uri_get_query(uri_struct);
            
            // note: we don't free uri_struct here as it's tied to the request
            // it will be cleaned up when the request is destroyed
        }
    }
    
    // parse query parameters
    request->query_params = serve_malloc(sizeof(struct evkeyvalq));
    if (request->query_params) {
        TAILQ_INIT(request->query_params);
        if (request->query) {
            http_parse_query(request->query, request->query_params);
        }
    }
    
    return request;
}

void http_request_destroy(http_request_t *request) {
    if (!request) {
        return;
    }
    
    if (request->query_params) {
        evhttp_clear_headers(request->query_params);
        serve_free(request->query_params);
    }
    
    serve_free(request);
}

const char* http_request_get_param(http_request_t *request, const char *name) {
    if (!request || !name || !request->query_params) {
        return NULL;
    }
    
    return evhttp_find_header(request->query_params, name);
}

const char* http_request_get_header(http_request_t *request, const char *name) {
    if (!request || !name || !request->headers) {
        return NULL;
    }
    
    return evhttp_find_header(request->headers, name);
}

char* http_request_get_body(http_request_t *request) {
    if (!request || !request->input_buffer) {
        return NULL;
    }
    
    size_t len = evbuffer_get_length(request->input_buffer);
    if (len == 0) {
        return NULL;
    }
    
    char *body = serve_malloc(len + 1);
    if (!body) {
        return NULL;
    }
    
    evbuffer_copyout(request->input_buffer, body, len);
    body[len] = '\0';
    
    return body;
}

size_t http_request_get_body_size(http_request_t *request) {
    if (!request || !request->input_buffer) {
        return 0;
    }
    
    return evbuffer_get_length(request->input_buffer);
}

/**
 * response generation implementation
 */

http_response_t* http_response_create(struct evhttp_request *req) {
    if (!req) {
        serve_set_error("null request");
        return NULL;
    }
    
    http_response_t *response = serve_malloc(sizeof(http_response_t));
    if (!response) {
        serve_set_error("failed to allocate response context");
        return NULL;
    }
    
    response->req = req;
    response->output_buffer = evbuffer_new();
    response->headers = evhttp_request_get_output_headers(req);
    response->status_code = HTTP_STATUS_OK;
    response->headers_sent = 0;
    
    if (!response->output_buffer) {
        serve_free(response);
        serve_set_error("failed to create output buffer");
        return NULL;
    }
    
    return response;
}

void http_response_destroy(http_response_t *response) {
    if (!response) {
        return;
    }
    
    // send response if not already sent
    if (!response->headers_sent) {
        http_response_send(response);
    }
    
    if (response->output_buffer) {
        evbuffer_free(response->output_buffer);
    }
    
    serve_free(response);
}

void http_response_set_status(http_response_t *response, int status_code) {
    if (!response || response->headers_sent) {
        return;
    }
    
    response->status_code = status_code;
}

void http_response_set_header(http_response_t *response, 
                             const char *name, const char *value) {
    if (!response || !name || !value || response->headers_sent) {
        return;
    }
    
    evhttp_add_header(response->headers, name, value);
}

void http_response_add_content(http_response_t *response, 
                              const void *data, size_t size) {
    if (!response || !data || size == 0 || response->headers_sent) {
        return;
    }
    
    evbuffer_add(response->output_buffer, data, size);
}

void http_response_add_string(http_response_t *response, const char *content) {
    if (!response || !content || response->headers_sent) {
        return;
    }
    
    evbuffer_add_printf(response->output_buffer, "%s", content);
}

void http_response_add_printf(http_response_t *response, 
                             const char *format, ...) {
    if (!response || !format || response->headers_sent) {
        return;
    }
    
    va_list args;
    va_start(args, format);
    evbuffer_add_vprintf(response->output_buffer, format, args);
    va_end(args);
}

void http_response_send(http_response_t *response) {
    if (!response || response->headers_sent) {
        return;
    }
    
    // set content length if not already set
    if (!evhttp_find_header(response->headers, "Content-Length")) {
        size_t content_length = evbuffer_get_length(response->output_buffer);
        char length_str[32];
        snprintf(length_str, sizeof(length_str), "%zu", content_length);
        evhttp_add_header(response->headers, "Content-Length", length_str);
    }
    
    // add server header if not present
    if (!evhttp_find_header(response->headers, "Server")) {
        evhttp_add_header(response->headers, "Server", "Jubily/1.0");
    }
    
    // add date header if not present
    if (!evhttp_find_header(response->headers, "Date")) {
        char date_buffer[32];
        serve_get_timestamp(date_buffer);
        evhttp_add_header(response->headers, "Date", date_buffer);
    }
    
    evhttp_send_reply(response->req, response->status_code,
                     http_status_string(response->status_code),
                     response->output_buffer);
    
    response->headers_sent = 1;
}

/**
 * convenience functions implementation
 */

void http_send_simple_response(struct evhttp_request *req, int status_code,
                              const char *content_type, const char *content) {
    if (!req) {
        return;
    }
    
    http_response_t *response = http_response_create(req);
    if (!response) {
        return;
    }
    
    http_response_set_status(response, status_code);
    
    if (content_type) {
        http_response_set_header(response, "Content-Type", content_type);
    }
    
    if (content) {
        http_response_add_string(response, content);
    }
    
    http_response_destroy(response);
}

void http_send_error(struct evhttp_request *req, int status_code, 
                    const char *message) {
    if (!req) {
        return;
    }
    
    const char *status_text = http_status_string(status_code);
    char *html_content = NULL;
    
    if (message) {
        size_t content_size = strlen(message) + strlen(status_text) + 200;
        html_content = serve_malloc(content_size);
        if (html_content) {
            snprintf(html_content, content_size,
                    "<!DOCTYPE html>\n"
                    "<html><head><title>%d %s</title></head>\n"
                    "<body><h1>%d %s</h1><p>%s</p></body></html>\n",
                    status_code, status_text, status_code, status_text, message);
        }
    } else {
        size_t content_size = strlen(status_text) + 150;
        html_content = serve_malloc(content_size);
        if (html_content) {
            snprintf(html_content, content_size,
                    "<!DOCTYPE html>\n"
                    "<html><head><title>%d %s</title></head>\n"
                    "<body><h1>%d %s</h1></body></html>\n",
                    status_code, status_text, status_code, status_text);
        }
    }
    
    http_send_simple_response(req, status_code, "text/html", 
                             html_content ? html_content : status_text);
    
    serve_free(html_content);
}

int http_send_file(struct evhttp_request *req, const char *filepath) {
    if (!req || !filepath) {
        http_send_error(req, 400, "invalid request");
        return -1;
    }
    
    // check if file exists
    if (!serve_file_exists(filepath)) {
        http_send_error(req, HTTP_STATUS_NOT_FOUND, "file not found");
        return -1;
    }
    
    // read file content
    size_t file_size;
    char *content = serve_read_file(filepath, &file_size);
    if (!content) {
        http_send_error(req, HTTP_STATUS_INTERNAL_ERROR, "failed to read file");
        return -1;
    }
    
    // determine content type from file extension
    const char *extension = serve_get_file_extension(filepath);
    const char *content_type = serve_get_mime_type(extension);
    
    // create response
    http_response_t *response = http_response_create(req);
    if (!response) {
        serve_free(content);
        http_send_error(req, HTTP_STATUS_INTERNAL_ERROR, "failed to create response");
        return -1;
    }
    
    // set headers
    http_response_set_header(response, "Content-Type", content_type);
    
    // add last-modified header
    char time_buffer[32];
    if (serve_get_file_time(filepath, time_buffer)) {
        http_response_set_header(response, "Last-Modified", time_buffer);
    }
    
    // add content
    http_response_add_content(response, content, file_size);
    
    // send response
    http_response_destroy(response);
    serve_free(content);
    
    return 0;
}

void http_send_redirect(struct evhttp_request *req, const char *location, 
                       int permanent) {
    if (!req || !location) {
        return;
    }
    
    int status_code = permanent ? HTTP_STATUS_MOVED_PERMANENTLY : HTTP_STATUS_FOUND;
    http_response_t *response = http_response_create(req);
    if (!response) {
        return;
    }
    
    http_response_set_status(response, status_code);
    http_response_set_header(response, "Location", location);
    http_response_destroy(response);
}

/**
 * utility functions implementation
 */

const char* http_method_string(enum evhttp_cmd_type method) {
    switch (method) {
        case EVHTTP_REQ_GET: return "GET";
        case EVHTTP_REQ_POST: return "POST";
        case EVHTTP_REQ_HEAD: return "HEAD";
        case EVHTTP_REQ_PUT: return "PUT";
        case EVHTTP_REQ_DELETE: return "DELETE";
        case EVHTTP_REQ_OPTIONS: return "OPTIONS";
        case EVHTTP_REQ_TRACE: return "TRACE";
        case EVHTTP_REQ_CONNECT: return "CONNECT";
        case EVHTTP_REQ_PATCH: return "PATCH";
        default: return "UNKNOWN";
    }
}

const char* http_status_string(int status_code) {
    switch (status_code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        default: return "Unknown";
    }
}

int http_parse_query(const char *query, struct evkeyvalq *params) {
    if (!query || !params) {
        return -1;
    }
    
    char *query_copy = serve_strdup(query);
    if (!query_copy) {
        return -1;
    }
    
    char *token = strtok(query_copy, "&");
    while (token) {
        char *equals = strchr(token, '=');
        if (equals) {
            *equals = '\0';
            char *key = token;
            char *value = equals + 1;
            
            // url decode key and value
            serve_url_decode(key);
            serve_url_decode(value);
            
            evhttp_add_header(params, key, value);
        } else {
            // parameter without value
            serve_url_decode(token);
            evhttp_add_header(params, token, "");
        }
        
        token = strtok(NULL, "&");
    }
    
    serve_free(query_copy);
    return 0;
}

size_t http_url_decode(char *str) {
    return serve_url_decode(str);
}

int http_method_allowed(enum evhttp_cmd_type method, int allowed_methods) {
    switch (method) {
        case EVHTTP_REQ_GET:
            return (allowed_methods & HTTP_METHOD_GET) != 0;
        case EVHTTP_REQ_POST:
            return (allowed_methods & HTTP_METHOD_POST) != 0;
        case EVHTTP_REQ_PUT:
            return (allowed_methods & HTTP_METHOD_PUT) != 0;
        case EVHTTP_REQ_DELETE:
            return (allowed_methods & HTTP_METHOD_DELETE) != 0;
        case EVHTTP_REQ_HEAD:
            return (allowed_methods & HTTP_METHOD_HEAD) != 0;
        case EVHTTP_REQ_OPTIONS:
            return (allowed_methods & HTTP_METHOD_OPTIONS) != 0;
        case EVHTTP_REQ_PATCH:
            return (allowed_methods & HTTP_METHOD_PATCH) != 0;
        default:
            return 0;
    }
}
