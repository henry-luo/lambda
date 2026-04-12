/**
 * @file http_request.cpp
 * @brief HTTP request parsing and accessor implementation
 */

#include "http_request.hpp"
#include "serve_utils.hpp"
#include <string.h>
#include "../../lib/mem.h"
#include <ctype.h>

// ============================================================================
// Multipart / Cookie cleanup
// ============================================================================

void multipart_free(MultipartPart *parts) {
    while (parts) {
        MultipartPart *next = parts->next;
        serve_free(parts->field_name);
        serve_free(parts->filename);
        serve_free(parts->content_type);
        serve_free(parts->data);
        serve_free(parts);
        parts = next;
    }
}

void cookie_entries_free(CookieEntry *list) {
    while (list) {
        CookieEntry *next = list->next;
        serve_free(list->name);
        serve_free(list->value);
        serve_free(list);
        list = next;
    }
}

// ============================================================================
// Internal helpers
// ============================================================================

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static size_t url_decode_inplace(char *str) {
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

static void parse_query_params(HttpRequest *req) {
    if (!req->query_string) return;
    char *copy = serve_strdup(req->query_string);
    if (!copy) return;

    char *saveptr = NULL;
    char *pair = strtok_r(copy, "&", &saveptr);
    while (pair) {
        char *eq = strchr(pair, '=');
        if (eq) {
            *eq = '\0';
            char *key = pair;
            char *val = eq + 1;
            url_decode_inplace(key);
            url_decode_inplace(val);
            req->query_params = http_header_add(req->query_params, key, val);
        } else {
            url_decode_inplace(pair);
            req->query_params = http_header_add(req->query_params, pair, "");
        }
        pair = strtok_r(NULL, "&", &saveptr);
    }
    serve_free(copy);
}

static void parse_cookies(HttpRequest *req) {
    if (req->cookies_parsed) return;
    req->cookies_parsed = 1;

    const char *cookie_hdr = http_header_find(req->headers, "Cookie");
    if (!cookie_hdr) return;

    char *copy = serve_strdup(cookie_hdr);
    if (!copy) return;

    char *saveptr = NULL;
    char *pair = strtok_r(copy, ";", &saveptr);
    while (pair) {
        // skip leading whitespace
        while (*pair == ' ') pair++;

        char *eq = strchr(pair, '=');
        if (eq) {
            *eq = '\0';
            char *name = pair;
            char *value = eq + 1;
            // trim trailing whitespace from name
            char *end = name + strlen(name) - 1;
            while (end > name && *end == ' ') { *end = '\0'; end--; }

            CookieEntry *entry = (CookieEntry *)serve_malloc(sizeof(CookieEntry));
            if (entry) {
                entry->name = serve_strdup(name);
                entry->value = serve_strdup(value);
                entry->next = req->cookies;
                req->cookies = entry;
            }
        }
        pair = strtok_r(NULL, ";", &saveptr);
    }
    serve_free(copy);
}

// ============================================================================
// Request Parsing
// ============================================================================

HttpRequest* http_request_parse(const char *data, size_t len) {
    if (!data || len == 0) return NULL;

    HttpRequest *req = (HttpRequest *)serve_malloc(sizeof(HttpRequest));
    if (!req) return NULL;
    memset(req, 0, sizeof(HttpRequest));
    req->http_version_major = 1;
    req->http_version_minor = 1;

    // find request line end
    const char *line_end = (const char *)memchr(data, '\r', len);
    if (!line_end || (size_t)(line_end - data + 1) >= len || line_end[1] != '\n') {
        line_end = (const char *)memchr(data, '\n', len);
        if (!line_end) {
            serve_free(req);
            return NULL;
        }
    }

    // parse request line: METHOD SP URI SP HTTP/x.x
    size_t rline_len = (size_t)(line_end - data);
    char *rline = (char *)serve_malloc(rline_len + 1);
    if (!rline) { serve_free(req); return NULL; }
    memcpy(rline, data, rline_len);
    rline[rline_len] = '\0';

    // extract method
    char *sp = strchr(rline, ' ');
    if (!sp) { serve_free(rline); serve_free(req); return NULL; }
    *sp = '\0';
    req->method = http_method_from_string(rline);

    // extract URI
    char *uri_start = sp + 1;
    char *sp2 = strchr(uri_start, ' ');
    if (sp2) {
        *sp2 = '\0';
        // parse HTTP version
        char *ver = sp2 + 1;
        if (strncmp(ver, "HTTP/", 5) == 0) {
            req->http_version_major = ver[5] - '0';
            if (ver[6] == '.') req->http_version_minor = ver[7] - '0';
        }
    }

    req->uri = serve_strdup(uri_start);

    // split URI: path ? query # fragment
    char *work = serve_strdup(uri_start);
    if (work) {
        char *frag = strchr(work, '#');
        if (frag) {
            *frag = '\0';
            req->fragment = serve_strdup(frag + 1);
        }
        char *qmark = strchr(work, '?');
        if (qmark) {
            *qmark = '\0';
            req->query_string = serve_strdup(qmark + 1);
        }
        req->path = serve_strdup(work);
        if (req->path) url_decode_inplace(req->path);
        serve_free(work);
    }

    serve_free(rline);

    // parse query params eagerly (cheap, usually small)
    parse_query_params(req);

    // parse headers
    const char *hdr_start = line_end;
    if (*hdr_start == '\r') hdr_start++;
    if (*hdr_start == '\n') hdr_start++;

    const char *end_ptr = data + len;
    while (hdr_start < end_ptr) {
        if (*hdr_start == '\r' || *hdr_start == '\n') {
            if (*hdr_start == '\r') hdr_start++;
            if (hdr_start < end_ptr && *hdr_start == '\n') hdr_start++;
            break;
        }

        const char *hdr_end = hdr_start;
        while (hdr_end < end_ptr && *hdr_end != '\r' && *hdr_end != '\n') hdr_end++;

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
                    req->headers = http_header_add(req->headers, name, value);
                    serve_free(value);
                }
                serve_free(name);
            }
        }

        hdr_start = hdr_end;
        if (hdr_start < end_ptr && *hdr_start == '\r') hdr_start++;
        if (hdr_start < end_ptr && *hdr_start == '\n') hdr_start++;
    }

    // body is everything after blank line
    if (hdr_start < end_ptr) {
        size_t body_len = (size_t)(end_ptr - hdr_start);
        if (body_len > 0) {
            req->body = (char *)serve_malloc(body_len + 1);
            if (req->body) {
                memcpy(req->body, hdr_start, body_len);
                req->body[body_len] = '\0';
                req->body_len = body_len;
            }
        }
    }

    // determine keep-alive from headers
    const char *conn = http_header_find(req->headers, "Connection");
    if (conn) {
        req->_keep_alive = (serve_strcasecmp(conn, "keep-alive") == 0) ? 1 : 0;
    } else {
        // HTTP/1.1 defaults to keep-alive
        req->_keep_alive = (req->http_version_minor >= 1) ? 1 : 0;
    }

    return req;
}

void http_request_destroy(HttpRequest *req) {
    if (!req) return;
    serve_free(req->uri);
    serve_free(req->path);
    serve_free(req->query_string);
    serve_free(req->fragment);
    serve_free(req->body);
    http_header_free(req->headers);
    http_header_free(req->query_params);
    http_header_free(req->route_params);
    cookie_entries_free(req->cookies);

    if (req->parsed_body) {
        switch (req->parsed_body_type) {
            case BODY_FORM:
                http_header_free((HttpHeader *)req->parsed_body);
                break;
            case BODY_MULTIPART:
                multipart_free((MultipartPart *)req->parsed_body);
                break;
            default:
                serve_free(req->parsed_body);
                break;
        }
    }

    serve_free(req);
}

// ============================================================================
// Accessors
// ============================================================================

const char* http_request_header(HttpRequest *req, const char *name) {
    return req ? http_header_find(req->headers, name) : NULL;
}

const char* http_request_query(HttpRequest *req, const char *name) {
    return req ? http_header_find(req->query_params, name) : NULL;
}

const char* http_request_param(HttpRequest *req, const char *name) {
    return req ? http_header_find(req->route_params, name) : NULL;
}

const char* http_request_cookie(HttpRequest *req, const char *name) {
    if (!req || !name) return NULL;
    parse_cookies(req);
    for (CookieEntry *c = req->cookies; c; c = c->next) {
        if (c->name && strcmp(c->name, name) == 0) return c->value;
    }
    return NULL;
}

const char* http_request_body(HttpRequest *req) {
    return req ? req->body : NULL;
}

const char* http_request_content_type(HttpRequest *req) {
    return http_request_header(req, "Content-Type");
}

int64_t http_request_content_length(HttpRequest *req) {
    const char *cl = http_request_header(req, "Content-Length");
    if (!cl) return -1;
    return (int64_t)atoll(cl);
}

int http_request_accepts_json(HttpRequest *req) {
    const char *accept = http_request_header(req, "Accept");
    if (!accept) return 0;
    return (strstr(accept, "application/json") != NULL ||
            strstr(accept, "*/*") != NULL);
}

int http_request_is_secure(HttpRequest *req) {
    // check X-Forwarded-Proto or actual TLS connection flag
    const char *proto = http_request_header(req, "X-Forwarded-Proto");
    if (proto && serve_strcasecmp(proto, "https") == 0) return 1;
    // actual TLS detection would be set by the server on accept
    return 0;
}

char* http_request_full_url(HttpRequest *req) {
    if (!req) return NULL;
    const char *host = http_request_header(req, "Host");
    if (!host) host = "localhost";
    const char *scheme = http_request_is_secure(req) ? "https" : "http";

    size_t len = strlen(scheme) + 3 + strlen(host) + strlen(req->path ? req->path : "/");
    if (req->query_string) len += 1 + strlen(req->query_string);
    len += 1; // null terminator

    char *url = (char *)serve_malloc(len);
    if (!url) return NULL;

    if (req->query_string) {
        snprintf(url, len, "%s://%s%s?%s", scheme, host,
                 req->path ? req->path : "/", req->query_string);
    } else {
        snprintf(url, len, "%s://%s%s", scheme, host,
                 req->path ? req->path : "/");
    }
    return url;
}
