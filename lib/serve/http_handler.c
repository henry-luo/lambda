/**
 * @file http_handler.c
 * @brief HTTP request handling and response generation (libuv-based)
 */

#include "http_handler.h"
#include "utils.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

// =============================================================================
// Header List Helpers
// =============================================================================

http_header_t* http_header_add(http_header_t *list, const char *name, const char *value) {
    http_header_t *h = (http_header_t *)serve_malloc(sizeof(http_header_t));
    if (!h) return list;
    h->name = serve_strdup(name);
    h->value = serve_strdup(value);
    h->next = list;
    return h;
}

const char* http_header_find(http_header_t *list, const char *name) {
    for (http_header_t *h = list; h; h = h->next) {
        if (h->name && name) {
            // case-insensitive comparison
            const char *a = h->name, *b = name;
            while (*a && *b && (tolower((unsigned char)*a) == tolower((unsigned char)*b))) {
                a++; b++;
            }
            if (*a == '\0' && *b == '\0') return h->value;
        }
    }
    return NULL;
}

void http_header_free(http_header_t *list) {
    while (list) {
        http_header_t *next = list->next;
        serve_free(list->name);
        serve_free(list->value);
        serve_free(list);
        list = next;
    }
}

// =============================================================================
// HTTP Method Helpers
// =============================================================================

http_method_t http_method_from_string(const char *method_str) {
    if (!method_str) return HTTP_METHOD_UNKNOWN;
    if (strcmp(method_str, "GET") == 0) return HTTP_METHOD_GET;
    if (strcmp(method_str, "POST") == 0) return HTTP_METHOD_POST;
    if (strcmp(method_str, "PUT") == 0) return HTTP_METHOD_PUT;
    if (strcmp(method_str, "DELETE") == 0) return HTTP_METHOD_DELETE;
    if (strcmp(method_str, "HEAD") == 0) return HTTP_METHOD_HEAD;
    if (strcmp(method_str, "OPTIONS") == 0) return HTTP_METHOD_OPTIONS;
    if (strcmp(method_str, "PATCH") == 0) return HTTP_METHOD_PATCH;
    return HTTP_METHOD_UNKNOWN;
}

const char* http_method_string(http_method_t method) {
    switch (method) {
        case HTTP_METHOD_GET: return "GET";
        case HTTP_METHOD_POST: return "POST";
        case HTTP_METHOD_PUT: return "PUT";
        case HTTP_METHOD_DELETE: return "DELETE";
        case HTTP_METHOD_HEAD: return "HEAD";
        case HTTP_METHOD_OPTIONS: return "OPTIONS";
        case HTTP_METHOD_PATCH: return "PATCH";
        default: return "UNKNOWN";
    }
}

int http_method_allowed(http_method_t method, int allowed_methods) {
    return (method & allowed_methods) != 0;
}

// =============================================================================
// URL Decode
// =============================================================================

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

size_t http_url_decode(char *str) {
    if (!str) return 0;
    char *src = str, *dst = str;
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            int hi = hex_digit(src[1]);
            int lo = hex_digit(src[2]);
            if (hi >= 0 && lo >= 0) {
                *dst++ = (char)((hi << 4) | lo);
                src += 3;
                continue;
            }
        }
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    return (size_t)(dst - str);
}

// =============================================================================
// Request Parsing
// =============================================================================

http_request_t* http_request_parse(const char *data, size_t len) {
    if (!data || len == 0) return NULL;

    http_request_t *request = (http_request_t *)serve_malloc(sizeof(http_request_t));
    if (!request) return NULL;
    memset(request, 0, sizeof(http_request_t));

    // find the request line end
    const char *line_end = (const char *)memchr(data, '\r', len);
    if (!line_end || (size_t)(line_end - data + 1) >= len || line_end[1] != '\n') {
        // try just \n
        line_end = (const char *)memchr(data, '\n', len);
        if (!line_end) {
            serve_free(request);
            return NULL;
        }
    }

    // parse request line: METHOD SP URI SP HTTP/x.x
    size_t rline_len = (size_t)(line_end - data);
    char *rline = (char *)serve_malloc(rline_len + 1);
    if (!rline) { serve_free(request); return NULL; }
    memcpy(rline, data, rline_len);
    rline[rline_len] = '\0';

    // extract method
    char *sp = strchr(rline, ' ');
    if (!sp) { serve_free(rline); serve_free(request); return NULL; }
    *sp = '\0';
    request->method = http_method_from_string(rline);

    // extract URI
    char *uri_start = sp + 1;
    char *sp2 = strchr(uri_start, ' ');
    if (sp2) *sp2 = '\0';

    // split URI into path and query
    char *qmark = strchr(uri_start, '?');
    if (qmark) {
        *qmark = '\0';
        request->query = serve_strdup(qmark + 1);
        // parse query params
        char *query_copy = serve_strdup(qmark + 1);
        if (query_copy) {
            char *pair = strtok(query_copy, "&");
            while (pair) {
                char *eq = strchr(pair, '=');
                if (eq) {
                    *eq = '\0';
                    char *key = pair;
                    char *val = eq + 1;
                    http_url_decode(key);
                    http_url_decode(val);
                    request->query_params = http_header_add(request->query_params, key, val);
                }
                pair = strtok(NULL, "&");
            }
            serve_free(query_copy);
        }
    }

    // url-decode path
    request->path = serve_strdup(uri_start);
    if (request->path) http_url_decode(request->path);
    request->uri = request->path;

    serve_free(rline);

    // parse headers
    const char *hdr_start = line_end;
    // skip \r\n or \n
    if (*hdr_start == '\r') hdr_start++;
    if (*hdr_start == '\n') hdr_start++;

    const char *end_ptr = data + len;
    while (hdr_start < end_ptr) {
        // check for empty line (end of headers)
        if (*hdr_start == '\r' || *hdr_start == '\n') {
            if (*hdr_start == '\r') hdr_start++;
            if (hdr_start < end_ptr && *hdr_start == '\n') hdr_start++;
            break;
        }

        // find end of this header line
        const char *hdr_end = hdr_start;
        while (hdr_end < end_ptr && *hdr_end != '\r' && *hdr_end != '\n') hdr_end++;

        // parse "Name: Value"
        const char *colon = (const char *)memchr(hdr_start, ':', (size_t)(hdr_end - hdr_start));
        if (colon) {
            size_t name_len = (size_t)(colon - hdr_start);
            char *name = (char *)serve_malloc(name_len + 1);
            if (name) {
                memcpy(name, hdr_start, name_len);
                name[name_len] = '\0';

                const char *val_start = colon + 1;
                while (val_start < hdr_end && *val_start == ' ') val_start++;
                size_t val_len = (size_t)(hdr_end - val_start);
                char *value = (char *)serve_malloc(val_len + 1);
                if (value) {
                    memcpy(value, val_start, val_len);
                    value[val_len] = '\0';
                    request->headers = http_header_add(request->headers, name, value);
                    serve_free(value);
                }
                serve_free(name);
            }
        }

        hdr_start = hdr_end;
        if (hdr_start < end_ptr && *hdr_start == '\r') hdr_start++;
        if (hdr_start < end_ptr && *hdr_start == '\n') hdr_start++;
    }

    // body is everything after the blank line
    if (hdr_start < end_ptr) {
        size_t body_len = (size_t)(end_ptr - hdr_start);
        if (body_len > 0) {
            request->body = (char *)serve_malloc(body_len + 1);
            if (request->body) {
                memcpy(request->body, hdr_start, body_len);
                request->body[body_len] = '\0';
                request->body_len = body_len;
            }
        }
    }

    return request;
}

void http_request_destroy(http_request_t *request) {
    if (!request) return;
    serve_free(request->path);
    serve_free(request->query);
    serve_free(request->body);
    http_header_free(request->headers);
    http_header_free(request->query_params);
    serve_free(request);
}

const char* http_request_get_param(http_request_t *request, const char *name) {
    if (!request || !name) return NULL;
    return http_header_find(request->query_params, name);
}

const char* http_request_get_header(http_request_t *request, const char *name) {
    if (!request || !name) return NULL;
    return http_header_find(request->headers, name);
}

const char* http_request_get_body(http_request_t *request) {
    if (!request) return NULL;
    return request->body;
}

size_t http_request_get_body_size(http_request_t *request) {
    if (!request) return 0;
    return request->body_len;
}

// =============================================================================
// Response Generation
// =============================================================================

http_response_t* http_response_create(uv_tcp_t *client) {
    if (!client) return NULL;

    http_response_t *response = (http_response_t *)serve_malloc(sizeof(http_response_t));
    if (!response) return NULL;
    memset(response, 0, sizeof(http_response_t));

    response->client = client;
    response->status_code = HTTP_STATUS_OK;
    response->body_cap = 4096;
    response->body = (char *)serve_malloc(response->body_cap);
    if (!response->body) {
        serve_free(response);
        return NULL;
    }
    response->body_len = 0;

    return response;
}

void http_response_destroy(http_response_t *response) {
    if (!response) return;
    if (!response->headers_sent) {
        http_response_send(response);
    }
    http_header_free(response->headers);
    serve_free(response->body);
    serve_free(response);
}

void http_response_set_status(http_response_t *response, int status_code) {
    if (!response || response->headers_sent) return;
    response->status_code = status_code;
}

void http_response_set_header(http_response_t *response,
                             const char *name, const char *value) {
    if (!response || !name || !value || response->headers_sent) return;
    response->headers = http_header_add(response->headers, name, value);
}

static void ensure_body_capacity(http_response_t *response, size_t additional) {
    if (response->body_len + additional >= response->body_cap) {
        size_t new_cap = (response->body_len + additional) * 2;
        char *new_body = (char *)serve_malloc(new_cap);
        if (!new_body) return;
        if (response->body_len > 0)
            memcpy(new_body, response->body, response->body_len);
        serve_free(response->body);
        response->body = new_body;
        response->body_cap = new_cap;
    }
}

void http_response_add_content(http_response_t *response,
                              const void *data, size_t size) {
    if (!response || !data || size == 0 || response->headers_sent) return;
    ensure_body_capacity(response, size);
    memcpy(response->body + response->body_len, data, size);
    response->body_len += size;
}

void http_response_add_string(http_response_t *response, const char *content) {
    if (!response || !content || response->headers_sent) return;
    http_response_add_content(response, content, strlen(content));
}

void http_response_add_printf(http_response_t *response,
                             const char *format, ...) {
    if (!response || !format || response->headers_sent) return;
    char buf[4096];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    if (n > 0) {
        http_response_add_content(response, buf, (size_t)n);
    }
}

static void on_write_done(uv_write_t *req, int status) {
    free(req->data); // free the buffer
    free(req);
}

void http_response_send(http_response_t *response) {
    if (!response || response->headers_sent || !response->client) return;

    // build the HTTP response
    // estimate header size
    size_t hdr_size = 256;
    for (http_header_t *h = response->headers; h; h = h->next) {
        hdr_size += strlen(h->name) + strlen(h->value) + 4;
    }
    hdr_size += 64; // content-length, server, date

    size_t total_size = hdr_size + response->body_len;
    char *buf = (char *)malloc(total_size);
    if (!buf) return;

    // status line
    int offset = snprintf(buf, total_size, "HTTP/1.1 %d %s\r\n",
                          response->status_code,
                          http_status_string(response->status_code));

    // content-length
    offset += snprintf(buf + offset, total_size - (size_t)offset,
                      "Content-Length: %zu\r\n", response->body_len);

    // server header
    int has_server = 0;
    for (http_header_t *h = response->headers; h; h = h->next) {
        if (h->name && tolower((unsigned char)h->name[0]) == 's' &&
            strcasecmp(h->name, "Server") == 0) {
            has_server = 1;
            break;
        }
    }
    if (!has_server) {
        offset += snprintf(buf + offset, total_size - (size_t)offset,
                          "Server: Lambda/1.0\r\n");
    }

    // custom headers
    for (http_header_t *h = response->headers; h; h = h->next) {
        offset += snprintf(buf + offset, total_size - (size_t)offset,
                          "%s: %s\r\n", h->name, h->value);
    }

    // end of headers
    offset += snprintf(buf + offset, total_size - (size_t)offset, "\r\n");

    // body
    if (response->body_len > 0) {
        memcpy(buf + offset, response->body, response->body_len);
        offset += (int)response->body_len;
    }

    // send via libuv
    uv_write_t *write_req = (uv_write_t *)malloc(sizeof(uv_write_t));
    if (!write_req) { free(buf); return; }
    write_req->data = buf;

    uv_buf_t uv_buf = uv_buf_init(buf, (unsigned int)offset);
    uv_write(write_req, (uv_stream_t *)response->client, &uv_buf, 1, on_write_done);

    response->headers_sent = 1;
}

// =============================================================================
// Convenience Functions
// =============================================================================

void http_send_simple_response(uv_tcp_t *client, int status_code,
                              const char *content_type, const char *content) {
    if (!client) return;
    http_response_t *response = http_response_create(client);
    if (!response) return;

    http_response_set_status(response, status_code);
    if (content_type) {
        http_response_set_header(response, "Content-Type", content_type);
    }
    if (content) {
        http_response_add_string(response, content);
    }
    http_response_destroy(response);
}

void http_send_error(uv_tcp_t *client, int status_code, const char *message) {
    if (!client) return;

    const char *status_text = http_status_string(status_code);
    char html[512];
    if (message) {
        snprintf(html, sizeof(html),
                "<!DOCTYPE html>\n"
                "<html><head><title>%d %s</title></head>\n"
                "<body><h1>%d %s</h1><p>%s</p></body></html>\n",
                status_code, status_text, status_code, status_text, message);
    } else {
        snprintf(html, sizeof(html),
                "<!DOCTYPE html>\n"
                "<html><head><title>%d %s</title></head>\n"
                "<body><h1>%d %s</h1></body></html>\n",
                status_code, status_text, status_code, status_text);
    }

    http_send_simple_response(client, status_code, "text/html", html);
}

int http_send_file(uv_tcp_t *client, const char *filepath) {
    if (!client || !filepath) {
        http_send_error(client, 400, "invalid request");
        return -1;
    }

    if (!serve_file_exists(filepath)) {
        http_send_error(client, HTTP_STATUS_NOT_FOUND, "file not found");
        return -1;
    }

    size_t file_size;
    char *content = serve_read_file(filepath, &file_size);
    if (!content) {
        http_send_error(client, HTTP_STATUS_INTERNAL_ERROR, "failed to read file");
        return -1;
    }

    const char *extension = serve_get_file_extension(filepath);
    const char *content_type = serve_get_mime_type(extension);

    http_response_t *response = http_response_create(client);
    if (!response) {
        serve_free(content);
        return -1;
    }

    http_response_set_header(response, "Content-Type", content_type);
    http_response_add_content(response, content, file_size);
    http_response_destroy(response);

    serve_free(content);
    return 0;
}

void http_send_redirect(uv_tcp_t *client, const char *location, int permanent) {
    if (!client || !location) return;

    http_response_t *response = http_response_create(client);
    if (!response) return;

    http_response_set_status(response, permanent ? HTTP_STATUS_MOVED_PERMANENTLY : HTTP_STATUS_FOUND);
    http_response_set_header(response, "Location", location);
    http_response_add_string(response, "Redirecting...");
    http_response_destroy(response);
}

// =============================================================================
// Status String
// =============================================================================

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
        default:  return "Unknown";
    }
}
