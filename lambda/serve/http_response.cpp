/**
 * @file http_response.cpp
 * @brief HTTP response builder implementation
 */

#include "http_response.hpp"
#include "serve_utils.hpp"
#include "mime.hpp"
#include <string.h>
#include "../../lib/mem.h"
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

// ============================================================================
// Cookie Options
// ============================================================================

CookieOptions cookie_options_default(void) {
    CookieOptions opts;
    memset(&opts, 0, sizeof(CookieOptions));
    opts.max_age = -1;
    opts.path = "/";
    opts.http_only = 1;
    opts.same_site = "Lax";
    return opts;
}

// ============================================================================
// libuv write callback
// ============================================================================

static void on_write_done(uv_write_t *req, int status) {
    mem_free(req->data);
    mem_free(req);
}

static void uv_send_raw(uv_tcp_t *client, const char *data, size_t len) {
    if (!client || !data || len == 0) return;

    char *buf = (char *)mem_alloc(len, MEM_CAT_SERVE);
    if (!buf) return;
    memcpy(buf, data, len);

    uv_write_t *write_req = (uv_write_t *)mem_alloc(sizeof(uv_write_t), MEM_CAT_SERVE);
    if (!write_req) { mem_free(buf); return; }
    write_req->data = buf;

    uv_buf_t uv_buf = uv_buf_init(buf, (unsigned int)len);
    uv_write(write_req, (uv_stream_t *)client, &uv_buf, 1, on_write_done);
}

// ============================================================================
// Response Lifecycle
// ============================================================================

HttpResponse* http_response_create(uv_tcp_t *client) {
    if (!client) return NULL;

    HttpResponse *resp = (HttpResponse *)serve_malloc(sizeof(HttpResponse));
    if (!resp) return NULL;
    memset(resp, 0, sizeof(HttpResponse));

    resp->client = client;
    resp->status_code = HTTP_200_OK;
    resp->body_cap = 4096;
    resp->body = (char *)serve_malloc(resp->body_cap);
    if (!resp->body) {
        serve_free(resp);
        return NULL;
    }
    return resp;
}

void http_response_destroy(HttpResponse *resp) {
    if (!resp) return;
    if (!resp->finished && !resp->headers_sent) {
        http_response_send(resp);
    }
    http_header_free(resp->headers);
    serve_free(resp->body);
    serve_free(resp);
}

// ============================================================================
// Status & Headers
// ============================================================================

HttpResponse* http_response_status(HttpResponse *resp, int status_code) {
    if (!resp || resp->headers_sent) return resp;
    resp->status_code = status_code;
    return resp;
}

HttpResponse* http_response_set_header(HttpResponse *resp, const char *name, const char *value) {
    if (!resp || !name || !value || resp->headers_sent) return resp;
    // remove existing header with same name, then add
    resp->headers = http_header_remove(resp->headers, name);
    resp->headers = http_header_add(resp->headers, name, value);
    return resp;
}

HttpResponse* http_response_append_header(HttpResponse *resp, const char *name, const char *value) {
    if (!resp || !name || !value || resp->headers_sent) return resp;
    resp->headers = http_header_add(resp->headers, name, value);
    return resp;
}

HttpResponse* http_response_remove_header(HttpResponse *resp, const char *name) {
    if (!resp || !name || resp->headers_sent) return resp;
    resp->headers = http_header_remove(resp->headers, name);
    return resp;
}

const char* http_response_get_header(HttpResponse *resp, const char *name) {
    return resp ? http_header_find(resp->headers, name) : NULL;
}

// ============================================================================
// Body building
// ============================================================================

static void ensure_body_cap(HttpResponse *resp, size_t additional) {
    if (resp->body_len + additional >= resp->body_cap) {
        size_t new_cap = (resp->body_len + additional) * 2;
        char *new_body = (char *)serve_malloc(new_cap);
        if (!new_body) return;
        if (resp->body_len > 0) memcpy(new_body, resp->body, resp->body_len);
        serve_free(resp->body);
        resp->body = new_body;
        resp->body_cap = new_cap;
    }
}

void http_response_write(HttpResponse *resp, const void *data, size_t len) {
    if (!resp || !data || len == 0 || resp->headers_sent) return;
    ensure_body_cap(resp, len);
    memcpy(resp->body + resp->body_len, data, len);
    resp->body_len += len;
}

void http_response_write_str(HttpResponse *resp, const char *str) {
    if (str) http_response_write(resp, str, strlen(str));
}

void http_response_write_fmt(HttpResponse *resp, const char *fmt, ...) {
    if (!resp || !fmt || resp->headers_sent) return;
    char buf[4096];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n > 0) http_response_write(resp, buf, (size_t)n);
}

// ============================================================================
// Send (finalize)
// ============================================================================

void http_response_send(HttpResponse *resp) {
    if (!resp || resp->headers_sent || !resp->client) return;

    // estimate total size
    size_t hdr_size = 256;
    for (HttpHeader *h = resp->headers; h; h = h->next) {
        hdr_size += strlen(h->name) + strlen(h->value) + 4;
    }
    hdr_size += 128; // content-length, server, date

    size_t total = hdr_size + resp->body_len;
    char *buf = (char *)mem_alloc(total, MEM_CAT_SERVE);
    if (!buf) return;

    // status line
    int off = snprintf(buf, total, "HTTP/1.1 %d %s\r\n",
                       resp->status_code, http_status_string(resp->status_code));

    // content-length
    off += snprintf(buf + off, total - (size_t)off,
                    "Content-Length: %zu\r\n", resp->body_len);

    // server header (if not already set)
    if (!http_header_find(resp->headers, "Server")) {
        off += snprintf(buf + off, total - (size_t)off, "Server: Lambda/1.0\r\n");
    }

    // custom headers
    for (HttpHeader *h = resp->headers; h; h = h->next) {
        off += snprintf(buf + off, total - (size_t)off, "%s: %s\r\n", h->name, h->value);
    }

    // end of headers
    off += snprintf(buf + off, total - (size_t)off, "\r\n");

    // body
    if (resp->body_len > 0) {
        memcpy(buf + off, resp->body, resp->body_len);
        off += (int)resp->body_len;
    }

    // send via libuv
    uv_write_t *write_req = (uv_write_t *)mem_alloc(sizeof(uv_write_t), MEM_CAT_SERVE);
    if (!write_req) { mem_free(buf); return; }
    write_req->data = buf;

    uv_buf_t uv_buf = uv_buf_init(buf, (unsigned int)off);
    uv_write(write_req, (uv_stream_t *)resp->client, &uv_buf, 1, on_write_done);

    resp->headers_sent = 1;
    resp->finished = 1;
}

// ============================================================================
// Convenience Methods
// ============================================================================

void http_response_json(HttpResponse *resp, int status, const char *json_str) {
    if (!resp) return;
    http_response_status(resp, status);
    http_response_set_header(resp, "Content-Type", CONTENT_TYPE_JSON);
    if (json_str) http_response_write_str(resp, json_str);
    http_response_send(resp);
}

void http_response_html(HttpResponse *resp, int status, const char *html_str) {
    if (!resp) return;
    http_response_status(resp, status);
    http_response_set_header(resp, "Content-Type", CONTENT_TYPE_HTML);
    if (html_str) http_response_write_str(resp, html_str);
    http_response_send(resp);
}

void http_response_text(HttpResponse *resp, int status, const char *text) {
    if (!resp) return;
    http_response_status(resp, status);
    http_response_set_header(resp, "Content-Type", CONTENT_TYPE_TEXT);
    if (text) http_response_write_str(resp, text);
    http_response_send(resp);
}

int http_response_file(HttpResponse *resp, const char *filepath,
                       const char *content_type) {
    if (!resp || !filepath) return -1;

    size_t file_size = 0;
    char *content = serve_read_file(filepath, &file_size);
    if (!content) {
        http_response_error(resp, HTTP_500_INTERNAL_ERROR, "failed to read file");
        return -1;
    }

    const char *mime = content_type ? content_type : mime_detect(NULL, filepath, NULL, 0);
    http_response_set_header(resp, "Content-Type", mime);
    http_response_write(resp, content, file_size);
    http_response_send(resp);
    serve_free(content);
    return 0;
}

void http_response_redirect(HttpResponse *resp, const char *url, int status) {
    if (!resp || !url) return;
    if (status == 0) status = HTTP_302_FOUND;
    http_response_status(resp, status);
    http_response_set_header(resp, "Location", url);
    http_response_write_str(resp, "Redirecting...");
    http_response_send(resp);
}

void http_response_error(HttpResponse *resp, int status, const char *message) {
    if (!resp) return;
    const char *status_text = http_status_string(status);
    char html[512];
    snprintf(html, sizeof(html),
             "<!DOCTYPE html>\n"
             "<html><head><title>%d %s</title></head>\n"
             "<body><h1>%d %s</h1>%s%s%s</body></html>\n",
             status, status_text, status, status_text,
             message ? "<p>" : "", message ? message : "", message ? "</p>" : "");
    http_response_html(resp, status, html);
}

// ============================================================================
// Cookies
// ============================================================================

void http_response_set_cookie(HttpResponse *resp, const char *name,
                              const char *value, const CookieOptions *opts) {
    if (!resp || !name || !value) return;

    CookieOptions defaults = cookie_options_default();
    const CookieOptions *o = opts ? opts : &defaults;

    char buf[1024];
    int off = snprintf(buf, sizeof(buf), "%s=%s", name, value);

    if (o->max_age >= 0) {
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "; Max-Age=%d", o->max_age);
    }
    if (o->domain) {
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "; Domain=%s", o->domain);
    }
    if (o->path) {
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "; Path=%s", o->path);
    }
    if (o->secure) {
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "; Secure");
    }
    if (o->http_only) {
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "; HttpOnly");
    }
    if (o->same_site) {
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "; SameSite=%s", o->same_site);
    }

    http_response_append_header(resp, "Set-Cookie", buf);
}

void http_response_clear_cookie(HttpResponse *resp, const char *name,
                                const char *path, const char *domain) {
    CookieOptions opts = cookie_options_default();
    opts.max_age = 0;
    opts.path = path ? path : "/";
    opts.domain = domain;
    http_response_set_cookie(resp, name, "", &opts);
}

// ============================================================================
// Chunked / Streaming
// ============================================================================

void http_response_start_chunked(HttpResponse *resp) {
    if (!resp || resp->headers_sent) return;
    resp->chunked = 1;

    // build and send headers
    size_t hdr_size = 256;
    for (HttpHeader *h = resp->headers; h; h = h->next) {
        hdr_size += strlen(h->name) + strlen(h->value) + 4;
    }

    char *buf = (char *)mem_alloc(hdr_size, MEM_CAT_SERVE);
    if (!buf) return;

    int off = snprintf(buf, hdr_size, "HTTP/1.1 %d %s\r\n",
                       resp->status_code, http_status_string(resp->status_code));
    off += snprintf(buf + off, hdr_size - (size_t)off, "Transfer-Encoding: chunked\r\n");

    if (!http_header_find(resp->headers, "Server")) {
        off += snprintf(buf + off, hdr_size - (size_t)off, "Server: Lambda/1.0\r\n");
    }

    for (HttpHeader *h = resp->headers; h; h = h->next) {
        off += snprintf(buf + off, hdr_size - (size_t)off, "%s: %s\r\n", h->name, h->value);
    }

    off += snprintf(buf + off, hdr_size - (size_t)off, "\r\n");

    uv_send_raw(resp->client, buf, (size_t)off);
    mem_free(buf);

    resp->headers_sent = 1;
}

void http_response_write_chunk(HttpResponse *resp, const void *data, size_t len) {
    if (!resp || !resp->chunked || !resp->headers_sent || !data || len == 0) return;

    // format: hex-length CRLF data CRLF
    char header[32];
    int hdr_len = snprintf(header, sizeof(header), "%zx\r\n", len);

    size_t total = (size_t)hdr_len + len + 2;
    char *buf = (char *)mem_alloc(total, MEM_CAT_SERVE);
    if (!buf) return;

    memcpy(buf, header, (size_t)hdr_len);
    memcpy(buf + hdr_len, data, len);
    memcpy(buf + hdr_len + len, "\r\n", 2);

    uv_send_raw(resp->client, buf, total);
    mem_free(buf);
}

void http_response_end_chunked(HttpResponse *resp) {
    if (!resp || !resp->chunked || !resp->headers_sent) return;
    uv_send_raw(resp->client, "0\r\n\r\n", 5);
    resp->finished = 1;
}

// ============================================================================
// Server-Sent Events
// ============================================================================

void http_response_start_sse(HttpResponse *resp) {
    if (!resp) return;
    http_response_set_header(resp, "Content-Type", "text/event-stream");
    http_response_set_header(resp, "Cache-Control", "no-cache");
    http_response_set_header(resp, "Connection", "keep-alive");
    http_response_start_chunked(resp);
}

void http_response_send_event(HttpResponse *resp, const char *event,
                              const char *data, const char *id) {
    if (!resp || !resp->chunked || !data) return;

    char buf[4096];
    int off = 0;
    if (id) {
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "id: %s\n", id);
    }
    if (event) {
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "event: %s\n", event);
    }
    off += snprintf(buf + off, sizeof(buf) - (size_t)off, "data: %s\n\n", data);

    http_response_write_chunk(resp, buf, (size_t)off);
}
